// SPDX-License-Identifier: GPL-2.0
//
// Copyright (c) 2020 MediaTek Inc.
// Author: Owen Chen <owen.chen@mediatek.com>

#define pr_dbg(fmt, ...) \
	pr_notice("[CLKDBG] %s:%d: " fmt, __func__, __LINE__, ##__VA_ARGS__)

#include <linux/interrupt.h>
#include <linux/notifier.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/slab.h>
#include <linux/delay.h>

#include <linux/proc_fs.h>
#include <linux/fs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>
#include <linux/module.h>

#include "clk-mtk.h"
#include "clkchk.h"
#include "mt-plat/aee.h"

#define MAX_CLK_NUM			2048
#define PLL_LEN				20
#define INV_MSK				0xFFFFFFFF
#define PWR_STA_BIT			BIT(30)
#define PWR_STA_2ND_BIT			BIT(31)

bool check_bypass_status;

void __attribute__((weak)) clkchk_set_cfg(void)
{
}

static const struct clkchk_ops *clkchk_ops;
static struct provider_clk clkchk_pvd_clks[MAX_CLK_NUM], clkdbg_pvd_clks[MAX_CLK_NUM];
static struct notifier_block mtk_clkchk_notifier;
static int hwv_irq, hfrp_hwv_irq;

void set_clkchk_ops(const struct clkchk_ops *ops)
{
	clkchk_ops = ops;
}
EXPORT_SYMBOL(set_clkchk_ops);

/*
 * for mtcmos debug
 */
bool is_valid_reg(void __iomem *addr)
{
#if IS_ENABLED(CONFIG_64BIT)
	return ((u64)addr & 0xf0000000) != 0UL ||
			(((u64)addr >> 32U) & 0xf0000000) != 0UL;
#else
	return ((u32)addr & 0xf0000000) != 0U;
#endif
}
EXPORT_SYMBOL(is_valid_reg);

const struct regname *get_all_regnames(void)
{
	if (clkchk_ops == NULL || clkchk_ops->get_all_regnames == NULL)
		return NULL;

	return clkchk_ops->get_all_regnames();
}
EXPORT_SYMBOL(get_all_regnames);

void clkchk_devapc_dump(void)
{
	if (clkchk_ops == NULL || clkchk_ops->devapc_dump == NULL)
		return;

	clkchk_ops->devapc_dump();
}
EXPORT_SYMBOL(clkchk_devapc_dump);

void clkchk_external_dump(void)
{
	if (clkchk_ops == NULL || clkchk_ops->external_dump == NULL)
		return;

	clkchk_ops->external_dump();
}
EXPORT_SYMBOL(clkchk_external_dump);

static const char *get_provider_name(struct device_node *node, u32 *cells)
{
	const char *name;
	const char *p;
	u32 cc = 0;

	if (of_property_read_u32(node, "#clock-cells", &cc) != 0)
		cc = 0;

	if (cells != NULL)
		*cells = cc;

	if (cc == 0U) {
		if (of_property_read_string(node,
				"clock-output-names", &name) < 0)
			name = node->name;

		return name;
	}

	if (of_property_read_string(node, "compatible", &name) < 0)
		name = node->name;

	p = strchr(name, (int)'-');

	if (p != NULL)
		return p + 1;
	else
		return name;
}

static void setup_provider_clk(struct provider_clk *pvdck)
{
	const char *pvdname = pvdck->provider_name;
	struct pvd_msk *pm;

	if (!pvdname)
		return;

	if (clkchk_ops == NULL || clkchk_ops->get_pvd_pwr_mask == NULL) {
		pvdck->pwr_mask = INV_MSK;
		return;
	}

	pm = clkchk_ops->get_pvd_pwr_mask();

	for (; pm->pvdname != NULL; pm++) {
		if (!strcmp(pvdname, pm->pvdname)) {
			pvdck->pwr_mask = pm->pwr_mask;
			pvdck->sta_type = pm->sta_type;
			return;
		}
	}

	pvdck->pwr_mask = INV_MSK;
}

struct provider_clk *get_all_provider_clks(bool is_internal)
{
	struct provider_clk *temp_clks;
	struct device_node *node = NULL;
	unsigned int n = 0;

	if (is_internal) {
		if (clkchk_pvd_clks[0].ck != NULL)
			return clkchk_pvd_clks;

		temp_clks = &clkchk_pvd_clks[0];
	} else {
		if (clkdbg_pvd_clks[0].ck != NULL)
			return clkdbg_pvd_clks;

		temp_clks = &clkdbg_pvd_clks[0];
	}

	do {
		const char *node_name;
		u32 cells;

		node = of_find_node_with_property(node, "#clock-cells");

		if (node == NULL)
			break;

		node_name = get_provider_name(node, &cells);

		if (cells != 0U)  {
			unsigned int i;

			for (i = 0; i < MAX_CLK_NUM; i++) {
				struct of_phandle_args pa;
				struct clk *ck;

				pa.np = node;
				pa.args[0] = i;
				pa.args_count = 1;
				ck = of_clk_get_from_provider(&pa);

				if (PTR_ERR(ck) == -EINVAL)
					break;
				else if (IS_ERR_OR_NULL(ck))
					continue;

				temp_clks[n].ck = ck;
				temp_clks[n].ck_name = __clk_get_name(ck);
				temp_clks[n].idx = i;
				temp_clks[n].provider_name = node_name;
				setup_provider_clk(&temp_clks[n]);

				++n;

				if (n == MAX_CLK_NUM) {
					pr_notice("[clkchk] %s: temp_clks exceeds max size.\n", __func__);
					BUG_ON(1);
				}
			}
		}
	} while (node != NULL && n < MAX_CLK_NUM);

	return temp_clks;
}
EXPORT_SYMBOL_GPL(get_all_provider_clks);

static struct provider_clk *__clk_chk_lookup_pvdck(const char *name)
{
	struct provider_clk *pvdck = get_all_provider_clks(true);

	for (; pvdck->ck != NULL; pvdck++) {
		if (!strcmp(pvdck->ck_name, name))
			return pvdck;
	}

	return NULL;
}

static struct clk *__clk_chk_lookup(const char *name)
{
	struct provider_clk *pvdck = __clk_chk_lookup_pvdck(name);

	if (pvdck)
		return pvdck->ck;

	return NULL;
}

struct clk *clk_chk_lookup(const char *name)
{
	return __clk_chk_lookup(name);
}
EXPORT_SYMBOL(clk_chk_lookup);

static s32 *read_spm_pwr_status_array(void)
{
	if (clkchk_ops == NULL || clkchk_ops->get_spm_pwr_status_array  == NULL)
		return  ERR_PTR(-EINVAL);

	return clkchk_ops->get_spm_pwr_status_array();
}

int pwr_hw_is_on(enum PWR_STA_TYPE type, s32 val)
{
	u32 *pval = read_spm_pwr_status_array();
	u32 sta = 0;

	if (type == PWR_STA) {
		if ((pval[PWR_STA] & val) != val &&
				(pval[PWR_STA2] & val) != val)
			return 0;
		else if ((pval[PWR_STA] & val) == val &&
				(pval[PWR_STA2] & val) == val)
			return 1;
		else
			return -1;
	} else if (type == PWR_CON_STA) {
		if (clkchk_ops == NULL || clkchk_ops->get_spm_pwr_status == NULL) {
			if (clkchk_ops != NULL && clkchk_ops->get_pwr_status != NULL)
				sta = clkchk_ops->get_pwr_status(val);
		} else
			sta = clkchk_ops->get_spm_pwr_status(val);

		if ((sta & PWR_STA_BIT) != PWR_STA_BIT &&
				(sta & PWR_STA_2ND_BIT) != PWR_STA_2ND_BIT)
			return 0;
		else if ((sta & PWR_STA_BIT) == PWR_STA_BIT &&
				(sta & PWR_STA_2ND_BIT) == PWR_STA_2ND_BIT)
			return 1;
		else
			return -1;
	} else {
		if ((pval[type] & val) == val)
			return 1;
		else if ((pval[type] & val) != 0)
			return -1;

		else
			return 0;
	}
}
EXPORT_SYMBOL(pwr_hw_is_on);

int clkchk_pvdck_is_on(struct provider_clk *pvdck)
{
	int pd_idx;

	if (!pvdck)
		return -1;

	if (clkchk_ops == NULL || clkchk_ops->is_pwr_on == NULL) {
		if (pvdck->pwr_mask == INV_MSK) {
			if (clkchk_ops == NULL || clkchk_ops->get_pvd_pwr_data_idx == NULL)
				return 0;

			pd_idx = clkchk_ops->get_pvd_pwr_data_idx(pvdck->provider_name);

			if (pd_idx >= 0)
				return pwr_hw_is_on(PWR_CON_STA, pd_idx);
			else
				return 1;
		}

		if (!pvdck->pwr_mask) {
			return 1;
		}

		return pwr_hw_is_on(pvdck->sta_type, pvdck->pwr_mask);
	}

	return clkchk_ops->is_pwr_on(pvdck);
}
EXPORT_SYMBOL(clkchk_pvdck_is_on);

bool clkchk_pvdck_is_prepared(struct provider_clk *pvdck)
{
	struct clk_hw *hw;

	if (clkchk_pvdck_is_on(pvdck) == 1) {
		hw = __clk_get_hw(pvdck->ck);

		if (IS_ERR_OR_NULL(hw))
			return false;

		return clk_hw_is_prepared(hw);
	}

	return false;
}
EXPORT_SYMBOL(clkchk_pvdck_is_prepared);

bool clkchk_pvdck_is_enabled(struct provider_clk *pvdck)
{
	struct clk_hw *hw;

	if (!pvdck)
		return false;

	hw = __clk_get_hw(pvdck->ck);

	if (IS_ERR_OR_NULL(hw))
		return false;

	return clk_hw_is_enabled(hw);
}
EXPORT_SYMBOL(clkchk_pvdck_is_enabled);

static const char *ccf_state(struct provider_clk *pvdck)
{
	if (clkchk_pvdck_is_enabled(pvdck))
		return "enabled";

	if (clkchk_pvdck_is_prepared(pvdck))
		return "prepared";

	return "disabled";
}

static void dump_enabled_clks(struct provider_clk *pvdck)
{
	const char * const *pll_names;
	const char *c_name;
	const char *p_name;
	const char *comp_name;
	struct clk_hw *c_hw = __clk_get_hw(pvdck->ck);
	struct clk_hw *p_hw;

	/* If SW ref_count=0 (PLL/CG) or HW disabled (Mux), skip dump clk. */
	if (!clkchk_pvdck_is_enabled(pvdck))
		return;

	if (clkchk_ops == NULL || clkchk_ops->get_off_pll_names == NULL)
		return;

	if (IS_ERR_OR_NULL(c_hw))
		return;

	c_name = clk_hw_get_name(c_hw);

	p_hw = clk_hw_get_parent(c_hw);
	if (IS_ERR_OR_NULL(p_hw))
		return;

	p_name = clk_hw_get_name(c_hw);
	comp_name = clk_hw_get_name(p_hw);
	while (strcmp(comp_name, "clk26m")) {
		p_name = clk_hw_get_name(p_hw);
		p_hw = clk_hw_get_parent(p_hw);
		if (IS_ERR_OR_NULL(p_hw))
			return;

		comp_name = clk_hw_get_name(p_hw);
	}

	pll_names = clkchk_ops->get_off_pll_names();
	for (; *pll_names != NULL && p_name != NULL; pll_names++) {
		if (!strncmp(p_name, *pll_names, PLL_LEN)) {
			p_hw = clk_hw_get_parent(c_hw);
			pr_notice("[%-21s: %8s, %3d, %3d, %10ld, %21s]\n",
					c_name,
					ccf_state(pvdck),
					clkchk_pvdck_is_prepared(pvdck),
					clkchk_pvdck_is_enabled(pvdck),
					clk_hw_get_rate(c_hw),
					p_hw ? clk_hw_get_name(p_hw) : "None");
#if IS_ENABLED(CONFIG_MTK_DUMP_CLK_REFCNT_BY_DEVICE)
			dump_clk_user_info(c_hw);
#endif
			break;
		}
	}
}

static bool __check_pll_off(const char * const *name)
{
	int valid = 0;

	for (; *name != NULL; name++) {
		struct provider_clk *pvdck = __clk_chk_lookup_pvdck(*name);

		if (!clkchk_pvdck_is_enabled(pvdck))
			continue;

		pr_notice("suspend warning[0m: %s is on\n", *name);

		if (check_bypass_status) {
			bool bypass_name_is_equal = false;
			const char * const *bypass_name;

			bypass_name = clkchk_ops->get_bypass_pll_name();
			for (; *bypass_name != NULL; bypass_name++)
				if (!strcmp(*bypass_name, *name)) {
					bypass_name_is_equal = true;
					pr_notice("clk-chk bypass %s\n", (char*)bypass_name);
					continue;
				}
			if (bypass_name_is_equal)
				continue;
		}

		valid++;
	}

	if (valid)
		return true;

	return false;
}

static bool check_pll_off(void)
{
	const char * const *name;
	int ret = 0;

	if (clkchk_ops == NULL || clkchk_ops->get_off_pll_names == NULL)
		return false;

	name = clkchk_ops->get_off_pll_names();

	ret = __check_pll_off(name);

	if (clkchk_ops == NULL || clkchk_ops->get_notice_pll_names == NULL)
		return false;

	name = clkchk_ops->get_notice_pll_names();

	__check_pll_off(name);

	if (ret)
		return true;

	return false;
}

static bool is_pll_chk_bug_on(void)
{
	if (clkchk_ops == NULL || clkchk_ops->is_pll_chk_bug_on == NULL)
		return false;

	return clkchk_ops->is_pll_chk_bug_on();
}

static bool clkchk_is_retry_bug_on(bool reset_cnt)
{
	if (clkchk_ops == NULL || clkchk_ops->is_suspend_retry_stop == NULL)
		return true;

	return clkchk_ops->is_suspend_retry_stop(reset_cnt);
}

/*
 * clkchk vf table checking
 */

static int get_vcore_opp(void)
{
	if (clkchk_ops == NULL || clkchk_ops->get_vcore_opp == NULL)
		return VCORE_NULL;

	return clkchk_ops->get_vcore_opp();
}

static void warn_vcore(int opp, const char *clk_name, int rate, int id)
{
	int vf_opp;

	if (!clkchk_ops || !clkchk_ops->get_vf_opp)
		return;

	vf_opp = clkchk_ops->get_vf_opp(id, opp);
	if ((opp >= 0) && (id >= 0) && (vf_opp > 0) &&
			((rate/1000) > vf_opp)) {
		pr_notice("%s Choose %d FAIL!!!![MAX(%d/%d): %d]\r\n",
				clk_name, rate/1000, id, opp,
				vf_opp);

		BUG_ON(1);
	}
}

static int mtk_mux2id(const char **mux_name)
{
	int i = 0;

	if (!clkchk_ops || !clkchk_ops->get_vf_name
			|| !clkchk_ops->get_vf_num)
		return -EINVAL;

	for (i = 0; clkchk_ops->get_vf_num(); i++) {
		if (strcmp(*mux_name, clkchk_ops->get_vf_name(i)) == 0)
			return i;
	}

	return -EINVAL;
}

/* The clocks have a mechanism for synchronizing rate changes. */
static int mtk_clk_rate_change(struct notifier_block *nb,
					  unsigned long flags, void *data)
{
	struct clk_notifier_data *ndata = data;
	struct clk_hw *hw = __clk_get_hw(ndata->clk);
	const char *clk_name = __clk_get_name(hw->clk);
	int vcore_opp = get_vcore_opp();

	if (vcore_opp == VCORE_NULL)
		return -EINVAL;

	if (flags == PRE_RATE_CHANGE && clk_name) {
		warn_vcore(vcore_opp, clk_name,
			ndata->new_rate, mtk_mux2id(&clk_name));
	}

	return NOTIFY_OK;
}

static struct notifier_block mtk_clk_notifier = {
	.notifier_call = mtk_clk_rate_change,
};

int mtk_clk_check_muxes(void)
{
	struct clk *clk;
	int i;

	if (!clkchk_ops || !clkchk_ops->get_vf_name
			|| !clkchk_ops->get_vf_num)
		return -EINVAL;

	for (i = 0; i < clkchk_ops->get_vf_num(); i++) {
		const char *name = clkchk_ops->get_vf_name(i);

		if (!name)
			continue;

		pr_notice("name: %s\n", name);
		clk = __clk_chk_lookup(name);
		clk_notifier_register(clk, &mtk_clk_notifier);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(mtk_clk_check_muxes);

static void clkchk_dump_pll_reg(bool bug_on)
{
	if (clkchk_ops == NULL || clkchk_ops->dump_pll_reg == NULL)
		return;

	clkchk_ops->dump_pll_reg(bug_on);
}

static void clkchk_check_apmixed_sta(bool bug_on)
{
	if (clkchk_ops == NULL || clkchk_ops->check_apmixed_sta == NULL)
		return;

	clkchk_ops->check_apmixed_sta(bug_on);
}

static int clk_chk_dev_pm_suspend(struct device *dev)
{
	struct provider_clk *pvdck = get_all_provider_clks(true);

	if (check_pll_off()) {
		for (; pvdck->ck != NULL; pvdck++)
			dump_enabled_clks(pvdck);

		if (!clkchk_is_retry_bug_on(false))
			return -1;

		clkchk_dump_pll_reg(false);
		if (is_pll_chk_bug_on() || pdchk_get_bug_on_stat())
			BUG_ON(1);

#if IS_ENABLED(CONFIG_MTK_AEE_FEATURE)
		aee_kernel_warning_api(__FILE__, __LINE__,
				DB_OPT_DEFAULT | DB_OPT_FTRACE, "clk-chk",
				"fail to disable clk/pd in suspend\n");
#endif
	} else {
		clkchk_is_retry_bug_on(true);
	}

	return 0;
}

static int clk_chk_dev_pm_resume(struct device *dev)
{
	if (clkchk_ops == NULL || clkchk_ops->dev_pm_resume == NULL)
		return 0;

	clkchk_ops->dev_pm_resume();

	return 0;
}


const struct dev_pm_ops clk_chk_dev_pm_ops = {
	.suspend_noirq = clk_chk_dev_pm_suspend,
	.resume_noirq = clk_chk_dev_pm_resume,
};
EXPORT_SYMBOL(clk_chk_dev_pm_ops);

/*
 * for clock exception event handling
 */

static void clkchk_dump_bus_reg(struct regmap *regmap, u32 ofs)
{
	if (clkchk_ops == NULL || clkchk_ops->dump_bus_reg == NULL)
		return;

	clkchk_ops->dump_bus_reg(regmap, ofs);
}

static void clkchk_dump_vlp_reg(struct regmap *regmap, u32 shift)
{
	if (clkchk_ops == NULL || clkchk_ops->dump_vlp_reg == NULL)
		return;

	clkchk_ops->dump_vlp_reg(regmap, shift);
}

static bool clkchk_is_cg_chk_pwr_on(void)
{
	if (clkchk_ops == NULL || clkchk_ops->is_cg_chk_pwr_on == NULL)
		return false;

	return clkchk_ops->is_cg_chk_pwr_on();
}

static void clkchk_cg_chk(const char *name)
{
	struct provider_clk *pvdck;

	pvdck = __clk_chk_lookup_pvdck(name);
	if (!clkchk_pvdck_is_on(pvdck))
		pr_notice("clk %s access without power on\n", name);
}

static void clkchk_trace_clk_event(const char *name, unsigned int clk_sta)
{
	if (clkchk_ops == NULL || clkchk_ops->trace_clk_event == NULL)
		return;

	clkchk_ops->trace_clk_event(name, clk_sta);
}

static void clkchk_trigger_trace_dump(unsigned int enable)
{
	if (clkchk_ops == NULL || clkchk_ops->trigger_trace_dump == NULL)
		return;

	clkchk_ops->trigger_trace_dump(enable);
}

static void clkchk_cg_timeout_handle(struct regmap *regmap, u32 id, u32 shift)
{
	if (clkchk_ops == NULL || clkchk_ops->cg_timeout_handle == NULL)
		return;

	clkchk_ops->cg_timeout_handle(regmap, id, shift);
}

int clkchk_chk_pm_state(void)
{
	if (clkchk_ops == NULL || clkchk_ops->chk_pm_state == NULL)
		return 0;

	return clkchk_ops->chk_pm_state();
}
EXPORT_SYMBOL_GPL(clkchk_chk_pm_state);

static void clkchk_verify_debug_flow(void)
{
	if (clkchk_ops == NULL || clkchk_ops->verify_debug_flow == NULL)
		return;

	return clkchk_ops->verify_debug_flow();
}

static int clkchk_evt_handling(struct notifier_block *nb,
			unsigned long flags, void *data)
{
	struct clk_event_data *clkd;

	if (!data)
		return NOTIFY_OK;

	clkd = (struct clk_event_data *)data;

	switch (clkd->event_type) {
	case CLK_EVT_HWV_CG_TIMEOUT:
	case CLK_EVT_IPI_CG_TIMEOUT:
		clkchk_cg_timeout_handle(clkd->hwv_regmap, clkd->id, clkd->shift);
		break;
	case CLK_EVT_HWV_CG_CHK_PWR:
		if (clkchk_is_cg_chk_pwr_on())
			clkchk_cg_chk(clkd->name);
		break;
	case CLK_EVT_HWV_PLL_TIMEOUT:
		clkchk_dump_pll_reg(true);
		break;
	case CLK_EVT_CLK_TRACE:
		clkchk_trace_clk_event(clkd->name, clkd->id);
		break;
	case CLK_EVT_TRIGGER_TRACE_DUMP:
		clkchk_trigger_trace_dump(clkd->id);
		break;
	case CLK_EVT_SET_PARENT_ERR:
	case CLK_EVT_SET_PARENT_TIMEOUT:
		clkchk_dump_bus_reg(clkd->regmap, clkd->ofs);
		break;
	case CLK_EVT_BYPASS_PLL:
		if (clkd->ofs)
			check_bypass_status = true;
		else
			check_bypass_status = false;
		break;
	case CLK_EVT_MMINFRA_HWV_TIMEOUT:
		clkchk_dump_vlp_reg(clkd->regmap, clkd->shift);
		break;
	case CLK_EVT_CHECK_APMIXED_STAT:
		clkchk_check_apmixed_sta(clkd->shift);
		break;
	case CLK_EVT_DEBUG_FLOW_VERIFY:
		clkchk_verify_debug_flow();
		break;
	default:
		pr_notice("cannot get flags identify\n");
		break;
	}

	return NOTIFY_OK;
}

int set_clkchk_notify(void)
{
	int r = 0;

	mtk_clkchk_notifier.notifier_call = clkchk_evt_handling;
	r = register_mtk_clk_notifier(&mtk_clkchk_notifier);
	if (r)
		pr_err("clk-chk notifier register err(%d)\n", r);

	return r;
}
EXPORT_SYMBOL(set_clkchk_notify);

static void clkchk_check_hwv_irq_sta(void)
{
	if (clkchk_ops == NULL || clkchk_ops->check_hwv_irq_sta == NULL)
		return;

	clkchk_ops->check_hwv_irq_sta();
}

static void clkchk_check_mm_hwv_irq_sta(void)
{
	if (clkchk_ops == NULL || clkchk_ops->check_mm_hwv_irq_sta == NULL)
		return;

	clkchk_ops->check_mm_hwv_irq_sta();
}

static irqreturn_t clkchk_hwv_irq_handler(int irq, void *dev_id)
{
	disable_irq_nosync(irq);

	if (likely(irq == hwv_irq))
		clkchk_check_hwv_irq_sta();
	if (likely(irq == hfrp_hwv_irq))
		clkchk_check_mm_hwv_irq_sta();

	return IRQ_HANDLED;
}

void clkchk_hwv_irq_init(struct platform_device *pdev)
{
	int ret;

	hwv_irq = platform_get_irq_byname(pdev, "hwv_irq");
	if (hwv_irq < 0) {
		pr_notice("[clkchk] get hwv irq is not support\n");
	} else {
		ret = request_irq(hwv_irq, clkchk_hwv_irq_handler,
			IRQF_TRIGGER_NONE, "HWV IRQ", NULL);
		if (ret < 0) {
			pr_notice("[clkchk]hwv require irq fail %d %d\n",
				hwv_irq, ret);
		} else {
			ret = enable_irq_wake(hwv_irq);
			if (ret < 0)
				pr_notice("[clkchk]hwv wake fail:%d,%d\n",
					hwv_irq, ret);
		}
	}

	hfrp_hwv_irq = platform_get_irq_byname(pdev, "hfrp_hwv_irq");
	if (hfrp_hwv_irq < 0) {
		pr_notice("[clkchk] get hfrp hwv irq is not support\n");
	} else {
		ret = request_irq(hfrp_hwv_irq, clkchk_hwv_irq_handler,
			IRQF_TRIGGER_NONE, "HFRP HWV IRQ", NULL);
		if (ret < 0) {
			pr_notice("[clkchk]hfrp hwv require irq fail %d %d\n",
				hfrp_hwv_irq, ret);
		} else {
			ret = enable_irq_wake(hfrp_hwv_irq);
			if (ret < 0)
				pr_notice("[clkchk]hfrp hwv wake fail:%d,%d\n",
					hfrp_hwv_irq, ret);
		}
	}
}
EXPORT_SYMBOL(clkchk_hwv_irq_init);

