// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015 Pengutronix, Sascha Hauer <kernel@pengutronix.de>
 */
#include <linux/clk.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/mfd/syscon.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/pm_opp.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/pm_runtime.h>
#include <linux/arm-smccc.h>
#include <linux/soc/mediatek/mtk_sip_svc.h> /* for SMC ID table */

#include "scpsys.h"
#include "mtk-scpsys.h"
#include "clkchk.h"

#include <dt-bindings/power/mt2701-power.h>
#include <dt-bindings/power/mt2712-power.h>
#include <dt-bindings/power/mt6797-power.h>
#include <dt-bindings/power/mt7622-power.h>
#include <dt-bindings/power/mt7623a-power.h>
#include <dt-bindings/power/mt8173-power.h>
#include <dt-bindings/power/mt8192-power.h>

#define MTK_POLL_DELAY_US		10
#define MTK_POLL_TIMEOUT		USEC_PER_SEC
#define MTK_POLL_DELAY_US		10
#define MTK_POLL_300MS_TIMEOUT		(300 * USEC_PER_MSEC)
#define MTK_POLL_1S_TIMEOUT		USEC_PER_SEC
#define MTK_POLL_IRQ_DELAY_US		3
#define MTK_POLL_IRQ_TIMEOUT		USEC_PER_SEC
#define MTK_POLL_HWV_PREPARE_CNT	2500
#define MTK_POLL_HWV_PREPARE_US		2
#define MTK_PROFILE_TIMEOUT		10

#define SPM_VDE_PWR_CON			0x0210
#define SPM_MFG_PWR_CON			0x0214
#define SPM_VEN_PWR_CON			0x0230
#define SPM_ISP_PWR_CON			0x0238
#define SPM_DIS_PWR_CON			0x023c
#define SPM_CONN_PWR_CON		0x0280
#define SPM_VEN2_PWR_CON		0x0298
#define SPM_AUDIO_PWR_CON		0x029c	/* MT8173, MT2712 */
#define SPM_BDP_PWR_CON			0x029c	/* MT2701 */
#define SPM_ETH_PWR_CON			0x02a0
#define SPM_HIF_PWR_CON			0x02a4
#define SPM_IFR_MSC_PWR_CON		0x02a8
#define SPM_MFG_2D_PWR_CON		0x02c0
#define SPM_MFG_ASYNC_PWR_CON		0x02c4
#define SPM_USB_PWR_CON			0x02cc
#define SPM_USB2_PWR_CON		0x02d4	/* MT2712 */
#define SPM_ETHSYS_PWR_CON		0x02e0	/* MT7622 */
#define SPM_HIF0_PWR_CON		0x02e4	/* MT7622 */
#define SPM_HIF1_PWR_CON		0x02e8	/* MT7622 */
#define SPM_WB_PWR_CON			0x02ec	/* MT7622 */

#define SPM_PWR_STATUS			0x060c
#define SPM_PWR_STATUS_2ND		0x0610

#define PWR_RST_B_BIT			BIT(0)
#define PWR_ISO_BIT			BIT(1)
#define PWR_ON_BIT			BIT(2)
#define PWR_ON_2ND_BIT			BIT(3)
#define PWR_CLK_DIS_BIT			BIT(4)
#define PWR_SRAM_CLKISO_BIT		BIT(5)
#define PWR_SRAM_ISOINT_B_BIT		BIT(6)
#define SSYS_SEL_SPM_OR_SUB_PM		BIT(16)
#define SSYS_PWR_ON_OFF			BIT(17)
#define SSYS_RTFF_GRP_EN_0		BIT(18)
#define SSYS_RTFF_GRP_EN_1		BIT(19)
#define SSYS_SRAM_DORMANT_PD_0		BIT(20)
#define SSYS_SRAM_DORMANT_PD_1		BIT(21)
#define SSYS_SRAM_DORMANT_PD_2		BIT(22)
#define SSYS_SRAM_DORMANT_PD_3		BIT(23)
#define PWR_RTFF_SAVE			BIT(24)
#define PWR_RTFF_NRESTORE		BIT(25)
#define PWR_RTFF_CLK_DIS		BIT(26)
#define PWR_RTFF_SAVE_FLAG		BIT(27)
#define PWR_RTFF_UFS_CLK_DIS		BIT(28)
#define SYSS_PWR_ACK			BIT(29)
#define PWR_ACK				BIT(30)
#define PWR_ACK_2ND			BIT(31)

#define PWR_STATUS_CONN			BIT(1)
#define PWR_STATUS_DISP			BIT(3)
#define PWR_STATUS_MFG			BIT(4)
#define PWR_STATUS_ISP			BIT(5)
#define PWR_STATUS_VDEC			BIT(7)
#define PWR_STATUS_BDP			BIT(14)
#define PWR_STATUS_ETH			BIT(15)
#define PWR_STATUS_HIF			BIT(16)
#define PWR_STATUS_IFR_MSC		BIT(17)
#define PWR_STATUS_USB2			BIT(19)	/* MT2712 */
#define PWR_STATUS_VENC_LT		BIT(20)
#define PWR_STATUS_VENC			BIT(21)
#define PWR_STATUS_MFG_2D		BIT(22)	/* MT8173 */
#define PWR_STATUS_MFG_ASYNC		BIT(23)	/* MT8173 */
#define PWR_STATUS_AUDIO		BIT(24)	/* MT8173, MT2712 */
#define PWR_STATUS_USB			BIT(25)	/* MT8173, MT2712 */
#define PWR_STATUS_ETHSYS		BIT(24)	/* MT7622 */
#define PWR_STATUS_HIF0			BIT(25)	/* MT7622 */
#define PWR_STATUS_HIF1			BIT(26)	/* MT7622 */
#define PWR_STATUS_WB			BIT(27)	/* MT7622 */

#define SSYSPM_MTCMOS_DEFAULT_ON	BIT(0)
#define MTCMOS_SSYS_PWR_ON_OFF		BIT(4)
#define SSYS_RTFF_GRP_EN		(BIT(8) | BIT(9) | BIT(10) | BIT(11))
#define SSYS_PWR_ACK			BIT(31)

enum regmap_type {
	INVALID_TYPE = 0,
	IFR_TYPE,
	VLP_TYPE,
	BUS_TYPE_NUM,
};

static const char *bus_list[BUS_TYPE_NUM] = {
	[INVALID_TYPE] = "invalid",
	[IFR_TYPE] = "infracfg",
	[VLP_TYPE] = "vlpcfg",
};

static bool bypass_first_cg_off;
static BLOCKING_NOTIFIER_HEAD(scpsys_notifier_list);

static const struct scpsys_plat_ops *scpsys_ops;

static void turn_onoff_infra(enum infra_onoff_enum on);
static DEFINE_SPINLOCK(hwv_infra_lock);
#define hwccf_lock(flags)   spin_lock_irqsave(&hwv_infra_lock, flags)
#define hwccf_unlock(flags) spin_unlock_irqrestore(&hwv_infra_lock, flags)

int register_scpsys_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&scpsys_notifier_list, nb);
}
EXPORT_SYMBOL_GPL(register_scpsys_notifier);

int unregister_scpsys_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&scpsys_notifier_list, nb);
}
EXPORT_SYMBOL_GPL(unregister_scpsys_notifier);

static struct apu_callbacks *g_apucb;

void register_apu_callback(struct apu_callbacks *apucb)
{
	g_apucb = apucb;
}
EXPORT_SYMBOL_GPL(register_apu_callback);

static int __scpsys_domain_is_on(void __iomem *addr, u32 sta_mask)
{
	u32 status = readl(addr) & sta_mask;

	/*
	 * A domain is on when both status bits are set. If only one is set
	 * return an error. This happens while powering up a domain
	 */

	if (status == sta_mask)
		return true;
	if (!status)
		return false;

	return -EINVAL;
}

static int scpsys_sta_is_on(struct scp_domain *scpd)
{
	struct scp *scp = scpd->scp;

	return __scpsys_domain_is_on(scp->base + scp->ctrl_reg.pwr_sta_offs,
			scpd->data->sta_mask);
}

static int scpsys_sta_2nd_is_on(struct scp_domain *scpd)
{
	struct scp *scp = scpd->scp;

	return __scpsys_domain_is_on(scp->base + scp->ctrl_reg.pwr_sta2nd_offs,
			scpd->data->sta_mask);
}

static int scpsys_pwr_ack_is_on(struct scp_domain *scpd)
{
	struct scp *scp = scpd->scp;

	return __scpsys_domain_is_on(scp->base + scpd->data->ctl_offs,
			PWR_ACK);
}

static int scpsys_pwr_ack_2nd_is_on(struct scp_domain *scpd)
{
	struct scp *scp = scpd->scp;

	return __scpsys_domain_is_on(scp->base + scpd->data->ctl_offs,
			PWR_ACK_2ND);
}

static int scpsys_pbus_pwr_ack_is_on(struct scp_domain *scpd)
{
	struct scp *scp = scpd->scp;

	return __scpsys_domain_is_on(scp->base + scpd->data->ctl_offs,
			SSYS_PWR_ACK);
}

static int scpsys_regulator_is_enabled(struct scp_domain *scpd)
{
	if (!scpd->supply)
		return 0;

	return regulator_is_enabled(scpd->supply);
}

static int scpsys_regulator_enable(struct scp_domain *scpd)
{
	if (!scpd->supply)
		return 0;

	return regulator_enable(scpd->supply);
}

static int scpsys_regulator_disable(struct scp_domain *scpd)
{
	if (!scpd->supply)
		return 0;

	return regulator_disable(scpd->supply);
}

static void scpsys_clk_disable(struct clk *clk[], int max_num)
{
	int i;

	for (i = max_num - 1; i >= 0; i--)
		clk_disable_unprepare(clk[i]);
}

static int scpsys_clk_enable(struct clk *clk[], int max_num)
{
	int i, ret = 0;

	for (i = 0; i < max_num && clk[i]; i++) {
		ret = clk_prepare_enable(clk[i]);
		if (ret) {
			scpsys_clk_disable(clk, i);
			break;
		}
	}

	return ret;
}

static int scpsys_sram_on(struct scp_domain *scpd, void __iomem *ctl_addr,
			u32 set_bits, u32 ack_bits, bool wait_ack)
{
	u32 ack_mask, ack_sta;
	u32 val;
	int tmp;
	int ret = 0;

	if (MTK_SCPD_CAPS(scpd, MTK_SCPD_SRAM_SLP)) {
		ack_mask = ack_bits;
		ack_sta = ack_mask;
		val = readl(ctl_addr) | set_bits;
	} else {
		ack_mask = ack_bits;
		ack_sta = 0;
		val = readl(ctl_addr) & ~set_bits;
	}

	writel(val, ctl_addr);

	/* Either wait until SRAM_PDN_ACK all 0 or have a force wait */
	if (MTK_SCPD_CAPS(scpd, MTK_SCPD_FWAIT_SRAM)) {
		/*
		 * Currently, MTK_SCPD_FWAIT_SRAM is necessary only for
		 * MT7622_POWER_DOMAIN_WB and thus just a trivial setup
		 * is applied here.
		 */
		usleep_range(12000, 12100);
	} else {
		/* Either wait until SRAM_PDN_ACK all 1 or 0 */
		if (wait_ack) {
			ret = readl_poll_timeout_atomic(ctl_addr, tmp,
				(tmp & ack_mask) == ack_sta,
				MTK_POLL_DELAY_US, MTK_POLL_TIMEOUT);
			if (ret < 0)
				return ret;
		}
	}

	return ret;
}

static int scpsys_sram_off(struct scp_domain *scpd, void __iomem *ctl_addr,
			u32 set_bits, u32 ack_bits, bool wait_ack)
{
	u32 val;
	u32 ack_mask, ack_sta;
	int tmp;
	int ret = 0;

	if (MTK_SCPD_CAPS(scpd, MTK_SCPD_SRAM_SLP)) {
		ack_mask = ack_bits;
		ack_sta = 0;
		val = readl(ctl_addr) & ~set_bits;
	} else {
		ack_mask = ack_bits;
		ack_sta = ack_mask;
		val = readl(ctl_addr) | set_bits;
	}
	writel(val, ctl_addr);

	/* Either wait until SRAM_PDN_ACK all 1 or 0 */
	if (wait_ack)
		ret = readl_poll_timeout_atomic(ctl_addr, tmp,
				(tmp & ack_mask) == ack_sta,
				MTK_POLL_DELAY_US, MTK_POLL_TIMEOUT);

	return ret;
}

static int scpsys_sram_enable(struct scp_domain *scpd, void __iomem *ctl_addr)
{
	u32 val;
	int ret = 0;

	if (MTK_SCPD_CAPS(scpd, MTK_SCPD_SRAM_SLP))
		ret = scpsys_sram_on(scpd, ctl_addr, scpd->data->sram_slp_bits,
				scpd->data->sram_slp_ack_bits, true);
	else
		ret = scpsys_sram_on(scpd, ctl_addr, scpd->data->sram_pdn_bits,
				scpd->data->sram_pdn_ack_bits, true);

	if (MTK_SCPD_CAPS(scpd, MTK_SCPD_SRAM_ISO)) {
		val = readl(ctl_addr) | PWR_SRAM_ISOINT_B_BIT;
		writel(val, ctl_addr);
		udelay(1);
		val &= ~PWR_SRAM_CLKISO_BIT;
		writel(val, ctl_addr);
	}

	return ret;
}

static int scpsys_sram_disable(struct scp_domain *scpd, void __iomem *ctl_addr)
{
	u32 val;
	int ret = 0;

	if (MTK_SCPD_CAPS(scpd, MTK_SCPD_SRAM_ISO)) {
		val = readl(ctl_addr) | PWR_SRAM_CLKISO_BIT;
		writel(val, ctl_addr);
		val &= ~PWR_SRAM_ISOINT_B_BIT;
		writel(val, ctl_addr);
		udelay(1);
	}

	if (MTK_SCPD_CAPS(scpd, MTK_SCPD_SRAM_SLP))
		ret = scpsys_sram_off(scpd, ctl_addr, scpd->data->sram_slp_bits,
				scpd->data->sram_slp_ack_bits, true);
	else
		ret = scpsys_sram_off(scpd, ctl_addr, scpd->data->sram_pdn_bits,
				scpd->data->sram_pdn_ack_bits, true);

	return ret;
}

static int scpsys_sram_table_enable(struct scp_domain *scpd)
{
	const struct sram_ctl *sram_table = scpd->data->sram_table;
	struct scp *scp = scpd->scp;
	int ret = 0;
	int i;

	for (i = 0; i < MAX_SRAM_STEPS; i++) {
		if (sram_table[i].offs) {
			void __iomem *ctl_addr = scp->base + sram_table[i].offs;

			ret = scpsys_sram_on(scpd, ctl_addr, sram_table[i].msk,
					sram_table[i].ack_msk, sram_table[i].wait_ack);
			if (ret)
				return ret;
		}
	}

	return ret;
}

static int scpsys_sram_table_disable(struct scp_domain *scpd)
{
	const struct sram_ctl *sram_table = scpd->data->sram_table;
	struct scp *scp = scpd->scp;
	int ret = 0;
	int i;

	for (i = 0; i < MAX_SRAM_STEPS; i++) {
		if (sram_table[i].offs) {
			void __iomem *ctl_addr = scp->base + sram_table[i].offs;

			ret = scpsys_sram_off(scpd, ctl_addr, sram_table[i].msk,
					sram_table[i].ack_msk, sram_table[i].wait_ack);
			if (ret)
				return ret;
		}
	}

	return ret;
}

static int set_bus_protection(struct regmap *map, struct bus_prot *bp)
{
	u32 val = 0;
	u32 set_ofs = bp->set_ofs;
	u32 en_ofs = bp->en_ofs;
	u32 sta_ofs = bp->sta_ofs;
	u32 mask = bp->mask;
	u32 ack_mask = bp->ack_mask;
	int retry = 0;
	int ret = 0;

	while (retry <= 10) {
		if (set_ofs)
			regmap_write(map, set_ofs, mask);
		else
			regmap_update_bits(map, en_ofs, mask, mask);

		/* check bus protect enable setting */
		regmap_read(map, en_ofs, &val);
		if ((val & mask) == mask)
			break;

		retry++;
	}

	ret = regmap_read_poll_timeout_atomic(map, sta_ofs,
			val, (val & ack_mask) == ack_mask,
			MTK_POLL_DELAY_US, MTK_POLL_TIMEOUT);

	if (ret < 0) {
		pr_err("%s val=0x%x, mask=0x%x, (val & mask)=0x%x\n",
			__func__, val, ack_mask, (val & ack_mask));
	}

	return ret;
}

static int clear_bus_protection(struct regmap *map, struct bus_prot *bp)
{
	u32 val = 0;
	u32 clr_ofs = bp->clr_ofs;
	u32 en_ofs = bp->en_ofs;
	u32 sta_ofs = bp->sta_ofs;
	u32 mask = bp->mask;
	u32 ack_mask = bp->ack_mask;
	bool ignore_ack = bp->ignore_clr_ack;
	int ret = 0;

	if (clr_ofs)
		regmap_write(map, clr_ofs, mask);
	else
		regmap_update_bits(map, en_ofs, mask, 0);

	if (ignore_ack)
		return 0;

	ret = regmap_read_poll_timeout_atomic(map, sta_ofs,
			val, !(val & ack_mask),
			MTK_POLL_DELAY_US, MTK_POLL_TIMEOUT);

	if (ret < 0) {
		pr_err("%s val=0x%x, mask=0x%x, (val & mask)=0x%x\n",
			__func__, val, ack_mask, (val & ack_mask));
	}
	return ret;
}

static int scpsys_bus_protect_disable(struct scp_domain *scpd, unsigned int index)
{
	struct scp *scp = scpd->scp;
	const struct bus_prot *bp_table = scpd->data->bp_table;
	unsigned long flags = 0;
	int ret = 0;
	int i;

	if (scpd->data->lock)
		spin_lock_irqsave(scpd->data->lock, flags);

	for (i = index; i >= 0; i--) {
		struct regmap *map;
		struct bus_prot bp = bp_table[i];

		if (bp.type > INVALID_TYPE && bp.type < scp->num_bus_type)
			map = scp->bus_regmap[bp.type];
		else
			continue;

		if (!map)
			continue;

		if (index != (MAX_STEPS - 1)) {
			unsigned int val = 0, val2 = 0;

			/* reserve bus register status */
			regmap_read(map, bp.en_ofs, &val);
			regmap_read(map, bp.sta_ofs, &val2);
			pr_notice("[%d] bus en: 0x%08x, sta: 0x%08x before restore\n",
					i, val, val2);
			/* restore bus protect setting */
			clear_bus_protection(map, &bp);
		} else {
			ret = clear_bus_protection(map, &bp);

			if (ret)
				goto ERR;
		}
	}

ERR:
	if (scpd->data->lock)
		spin_unlock_irqrestore(scpd->data->lock, flags);

	return ret;
}

static int scpsys_bus_protect_enable(struct scp_domain *scpd)
{
	struct scp *scp = scpd->scp;
	const struct bus_prot *bp_table = scpd->data->bp_table;
	unsigned long flags = 0;
	int ret = 0;
	int i;

	if (scpd->data->lock)
		spin_lock_irqsave(scpd->data->lock, flags);

	for (i = 0; i < MAX_STEPS; i++) {
		struct regmap *map;
		struct bus_prot bp = bp_table[i];

		if (bp.type > INVALID_TYPE && bp.type < scp->num_bus_type)
			map = scp->bus_regmap[bp.type];
		else
			continue;
		if (!map)
			continue;

		ret = set_bus_protection(map, &bp);

		if (ret) {
			if (scpd->data->lock)
				spin_unlock_irqrestore(scpd->data->lock, flags);
			scpsys_bus_protect_disable(scpd, i);
			return ret;
		}
	}

	if (scpd->data->lock)
		spin_unlock_irqrestore(scpd->data->lock, flags);

	return ret;
}

static void scpsys_extb_iso_down(struct scp_domain *scpd)
{
	u32 val;
	struct scp *scp;
	void __iomem *ctl_addr;

	if (!scpd->data->extb_iso_offs)
		return;

	scp = scpd->scp;
	ctl_addr = scp->base + scpd->data->extb_iso_offs;
	val = readl(ctl_addr) & ~scpd->data->extb_iso_bits;
	writel(val, ctl_addr);
}

static void scpsys_extb_iso_up(struct scp_domain *scpd)
{
	u32 val;
	struct scp *scp;
	void __iomem *ctl_addr;

	if (!scpd->data->extb_iso_offs)
		return;

	scp = scpd->scp;
	ctl_addr = scp->base + scpd->data->extb_iso_offs;
	val = readl(ctl_addr) | scpd->data->extb_iso_bits;
	writel(val, ctl_addr);
}

static int scpsys_power_on(struct generic_pm_domain *genpd)
{
	struct scp_domain *scpd = container_of(genpd, struct scp_domain, genpd);
	struct scp *scp = scpd->scp;
	void __iomem *ctl_addr = scp->base + scpd->data->ctl_offs;
	u32 val;
	int ret, tmp;
	unsigned int pd_start_time, pd_end_time;

	if (MTK_SCPD_CAPS(scpd, MTK_SCPD_PROFILE))
		pd_start_time = jiffies_to_msecs(jiffies);

	ret = scpsys_regulator_enable(scpd);
	if (ret < 0)
		goto err_regulator;

	ret = scpsys_clk_enable(scpd->clk, MAX_CLKS);
	if (ret)
		goto err_clk;

	ret = scpsys_clk_enable(scpd->lp_clk, MAX_CLKS);
	if (ret)
		goto err_lp_clk;

	/* subsys power on */
	val = readl(ctl_addr);
	val |= PWR_ON_BIT;
	writel(val, ctl_addr);
	/* wait until PWR_ACK = 1 */
	if (MTK_SCPD_CAPS(scpd, MTK_SCPD_IS_PWR_CON_ON))
		ret = readx_poll_timeout_atomic(scpsys_pwr_ack_is_on, scpd, tmp, tmp > 0,
				MTK_POLL_DELAY_US, MTK_POLL_TIMEOUT);
	else
		ret = readx_poll_timeout_atomic(scpsys_sta_is_on, scpd, tmp, tmp > 0,
				MTK_POLL_DELAY_US, MTK_POLL_TIMEOUT);
	if (ret < 0)
		goto err_pwr_ack;

	udelay(50);

	/* subsys 2nd power on */
	val |= PWR_ON_2ND_BIT;
	writel(val, ctl_addr);
	/* wait until PWR_ACK = 1 */
	if (MTK_SCPD_CAPS(scpd, MTK_SCPD_IS_PWR_CON_ON))
		ret = readx_poll_timeout_atomic(scpsys_pwr_ack_2nd_is_on, scpd, tmp, tmp > 0,
				MTK_POLL_DELAY_US, MTK_POLL_TIMEOUT);
	else
		ret = readx_poll_timeout_atomic(scpsys_sta_2nd_is_on, scpd, tmp, tmp > 0,
				MTK_POLL_DELAY_US, MTK_POLL_TIMEOUT);
	if (ret < 0)
		goto err_pwr_ack;

	if (MTK_SCPD_CAPS(scpd, MTK_SCPD_PEXTP_PHY_RTFF) && scpd->rtff_flag) {
		val |= PWR_RTFF_CLK_DIS;
		writel(val, ctl_addr);
	}

	val &= ~PWR_CLK_DIS_BIT;
	writel(val, ctl_addr);

	val &= ~PWR_ISO_BIT;
	writel(val, ctl_addr);

	if (MTK_SCPD_CAPS(scpd, MTK_SCPD_RTFF_DELAY) && scpd->rtff_flag)
		udelay(10);

	val |= PWR_RST_B_BIT;
	writel(val, ctl_addr);

	if (MTK_SCPD_CAPS(scpd, MTK_SCPD_NON_CPU_RTFF)) {
		val = readl(ctl_addr);
		if (val & PWR_RTFF_SAVE_FLAG) {
			val &= ~PWR_RTFF_SAVE_FLAG;
			writel(val, ctl_addr);

			val |= PWR_RTFF_CLK_DIS;
			writel(val, ctl_addr);

			val &= ~PWR_RTFF_NRESTORE;
			writel(val, ctl_addr);

			val |= PWR_RTFF_NRESTORE;
			writel(val, ctl_addr);

			val &= ~PWR_RTFF_CLK_DIS;
			writel(val, ctl_addr);
		}
	} else if (MTK_SCPD_CAPS(scpd, MTK_SCPD_PEXTP_PHY_RTFF)) {
		val = readl(ctl_addr);
		if (val & PWR_RTFF_SAVE_FLAG) {
			val &= ~PWR_RTFF_SAVE_FLAG;
			writel(val, ctl_addr);

			val &= ~PWR_RTFF_NRESTORE;
			writel(val, ctl_addr);

			val |= PWR_RTFF_NRESTORE;
			writel(val, ctl_addr);

			val &= ~PWR_RTFF_CLK_DIS;
			writel(val, ctl_addr);
		}
	} else if (MTK_SCPD_CAPS(scpd, MTK_SCPD_UFS_RTFF) && scpd->rtff_flag) {
		val |= PWR_RTFF_UFS_CLK_DIS;
		writel(val, ctl_addr);

		val &= ~PWR_RTFF_NRESTORE;
		writel(val, ctl_addr);

		val |= PWR_RTFF_NRESTORE;
		writel(val, ctl_addr);

		val &= ~PWR_RTFF_UFS_CLK_DIS;
		writel(val, ctl_addr);

		scpd->rtff_flag = false;
	}

	if (!MTK_SCPD_CAPS(scpd, MTK_SCPD_BYPASS_CLK)) {
		ret = scpsys_clk_enable(scpd->subsys_clk, MAX_SUBSYS_CLKS);
		if (ret < 0)
			goto err_pwr_ack;

		ret = scpsys_clk_enable(scpd->subsys_lp_clk, MAX_SUBSYS_CLKS);
		if (ret < 0)
			goto err_pwr_ack;
	}

	if (MTK_SCPD_CAPS(scpd, MTK_SCPD_L2TCM_SRAM)) {
		ret = scpsys_sram_table_enable(scpd);
		if (ret < 0)
			goto err_sram;
	}

	ret = scpsys_sram_enable(scpd, ctl_addr);
	if (ret < 0)
		goto err_sram;

	ret = scpsys_bus_protect_disable(scpd, MAX_STEPS - 1);
	if (ret < 0)
		goto err_sram;

	if (!bypass_first_cg_off)
		scpsys_clk_disable(scpd->subsys_lp_clk, MAX_SUBSYS_CLKS);

	scpsys_clk_disable(scpd->lp_clk, MAX_CLKS);

	if (MTK_SCPD_CAPS(scpd, MTK_SCPD_PROFILE)) {
		pd_end_time = jiffies_to_msecs(jiffies);
		if ((pd_end_time - pd_start_time) > MTK_PROFILE_TIMEOUT)
			dev_err(scp->dev, "%s profile time(%u)\n",
				genpd->name, (pd_end_time - pd_start_time));
	}

	return 0;

err_sram:
	dev_err(scp->dev, "Failed to power on sram/bus %s(%d)\n", genpd->name, ret);
err_pwr_ack:
	dev_err(scp->dev, "Failed to power on mtcmos %s(%d)\n", genpd->name, ret);
err_lp_clk:
	dev_err(scp->dev, "Failed to enable lp_clk %s(%d)\n", genpd->name, ret);
err_clk:
	val = scpsys_regulator_is_enabled(scpd);
	dev_err(scp->dev, "Failed to enable clk %s(%d %d)\n", genpd->name, ret, val);
err_regulator:
	dev_err(scp->dev, "Failed to power on regulator %s(%d)\n", genpd->name, ret);

	return ret;
}

static int scpsys_power_off(struct generic_pm_domain *genpd)
{
	struct scp_domain *scpd = container_of(genpd, struct scp_domain, genpd);
	struct scp *scp = scpd->scp;
	void __iomem *ctl_addr = scp->base + scpd->data->ctl_offs;
	u32 val;
	int ret, tmp;
	unsigned int pd_start_time, pd_end_time;

	if (MTK_SCPD_CAPS(scpd, MTK_SCPD_PROFILE))
		pd_start_time = jiffies_to_msecs(jiffies);

	if (MTK_SCPD_CAPS(scpd, MTK_SCPD_BYPASS_OFF)) {
		dev_err(scp->dev, "bypass power off %s for bringup\n", genpd->name);
		return 0;
	}

	ret = scpsys_clk_enable(scpd->lp_clk, MAX_CLKS);
	if (ret)
		goto err_lp_clk;

	ret = scpsys_clk_enable(scpd->subsys_lp_clk, MAX_SUBSYS_CLKS);
	if (ret < 0)
		goto err_subsys_lp_clk;

	ret = scpsys_bus_protect_enable(scpd);
	if (ret < 0)
		goto out;

	if (MTK_SCPD_CAPS(scpd, MTK_SCPD_L2TCM_SRAM)) {
		ret = scpsys_sram_table_disable(scpd);
		if (ret < 0)
			goto out;
	}

	ret = scpsys_sram_disable(scpd, ctl_addr);
	if (ret < 0)
		goto out;

	if (!MTK_SCPD_CAPS(scpd, MTK_SCPD_BYPASS_CLK)) {
		scpsys_clk_disable(scpd->subsys_clk, MAX_SUBSYS_CLKS);
		scpsys_clk_disable(scpd->subsys_lp_clk, MAX_SUBSYS_CLKS);
	}

	val = readl(ctl_addr);

	if (MTK_SCPD_CAPS(scpd, MTK_SCPD_NON_CPU_RTFF) ||
			MTK_SCPD_CAPS(scpd, MTK_SCPD_PEXTP_PHY_RTFF)) {
		val |= PWR_RTFF_CLK_DIS;
		writel(val, ctl_addr);

		val |= PWR_RTFF_SAVE;
		writel(val, ctl_addr);

		val &= ~PWR_RTFF_SAVE;
		writel(val, ctl_addr);

		val &= ~PWR_RTFF_CLK_DIS;
		writel(val, ctl_addr);

		val |= PWR_RTFF_SAVE_FLAG;
		writel(val, ctl_addr);
	} else if (MTK_SCPD_CAPS(scpd, MTK_SCPD_UFS_RTFF)) {
		val |= PWR_RTFF_UFS_CLK_DIS;
		writel(val, ctl_addr);

		val |= PWR_RTFF_SAVE;
		writel(val, ctl_addr);

		val &= ~PWR_RTFF_SAVE;
		writel(val, ctl_addr);

		val &= ~PWR_RTFF_UFS_CLK_DIS;
		writel(val, ctl_addr);
		if (MTK_SCPD_CAPS(scpd, MTK_SCPD_UFS_RTFF))
			scpd->rtff_flag = true;
	}

	/* subsys power off */
	val |= PWR_ISO_BIT;
	writel(val, ctl_addr);

	if (MTK_SCPD_CAPS(scpd, MTK_SCPD_RTFF_DELAY) && scpd->rtff_flag)
		udelay(1);

	val &= ~PWR_RST_B_BIT;
	writel(val, ctl_addr);

	val |= PWR_CLK_DIS_BIT;
	writel(val, ctl_addr);

	/* subsys power off */
	val &= ~PWR_ON_BIT;
	writel(val, ctl_addr);
	/* wait until PWR_ACK = 1 */
	if (MTK_SCPD_CAPS(scpd, MTK_SCPD_IS_PWR_CON_ON))
		ret = readx_poll_timeout_atomic(scpsys_pwr_ack_is_on, scpd, tmp, tmp == 0,
				MTK_POLL_DELAY_US, MTK_POLL_TIMEOUT);
	else
		ret = readx_poll_timeout_atomic(scpsys_sta_is_on, scpd, tmp, tmp == 0,
				MTK_POLL_DELAY_US, MTK_POLL_TIMEOUT);
	if (ret < 0)
		goto out;

	/* subsys 2nd power off */
	val &= ~PWR_ON_2ND_BIT;
	writel(val, ctl_addr);

	/* wait until PWR_ACK = 0 */
	if (MTK_SCPD_CAPS(scpd, MTK_SCPD_IS_PWR_CON_ON))
		ret = readx_poll_timeout_atomic(scpsys_pwr_ack_2nd_is_on, scpd, tmp, tmp == 0,
				MTK_POLL_DELAY_US, MTK_POLL_TIMEOUT);
	else
		ret = readx_poll_timeout_atomic(scpsys_sta_2nd_is_on, scpd, tmp, tmp == 0,
				MTK_POLL_DELAY_US, MTK_POLL_TIMEOUT);
	if (ret < 0)
		goto out;

	scpsys_clk_disable(scpd->clk, MAX_CLKS);

	scpsys_clk_disable(scpd->lp_clk, MAX_CLKS);

	ret = scpsys_regulator_disable(scpd);
	if (ret < 0)
		goto out;

	if (MTK_SCPD_CAPS(scpd, MTK_SCPD_PROFILE)) {
		pd_end_time = jiffies_to_msecs(jiffies);
		if ((pd_end_time - pd_start_time) > MTK_PROFILE_TIMEOUT)
			dev_err(scp->dev, "%s profile time(%u)\n",
				genpd->name, (pd_end_time - pd_start_time));
	}

	return 0;

err_subsys_lp_clk:
	scpsys_clk_disable(scpd->subsys_lp_clk, MAX_SUBSYS_CLKS);
err_lp_clk:
	scpsys_clk_disable(scpd->lp_clk, MAX_CLKS);
out:
	val = scpsys_regulator_is_enabled(scpd);
	dev_err(scp->dev, "Failed to power off domain %s(%d %d)\n", genpd->name, ret, val);

	return ret;
}

static int scpsys_md_power_on(struct generic_pm_domain *genpd)
{
	struct scp_domain *scpd = container_of(genpd, struct scp_domain, genpd);
	struct scp *scp = scpd->scp;
	void __iomem *ctl_addr = scp->base + scpd->data->ctl_offs;
	u32 val;
	int ret, tmp;

	ret = scpsys_regulator_enable(scpd);
	if (ret < 0)
		return ret;

	scpsys_extb_iso_down(scpd);

	ret = scpsys_clk_enable(scpd->clk, MAX_CLKS);
	if (ret)
		goto err_clk;

	val = readl(ctl_addr);

	/* for md subsys, reset_b is prior to power_on bit */
	if (!MTK_SCPD_CAPS(scpd, MTK_SCPD_REMOVE_MD_RSTB)) {
		val |= PWR_RST_B_BIT;
		writel(val, ctl_addr);
	}

	/* subsys power on */
	val |= PWR_ON_BIT;
	writel(val, ctl_addr);

	/* wait until PWR_ACK = 1 */
	if (MTK_SCPD_CAPS(scpd, MTK_SCPD_IS_PWR_CON_ON))
		ret = readx_poll_timeout_atomic(scpsys_pwr_ack_is_on, scpd, tmp, tmp > 0,
				MTK_POLL_DELAY_US, MTK_POLL_TIMEOUT);
	else
		ret = readx_poll_timeout_atomic(scpsys_sta_is_on, scpd, tmp, tmp > 0,
				 MTK_POLL_DELAY_US, MTK_POLL_TIMEOUT);
	if (ret < 0)
		goto err_pwr_ack;

	ret = scpsys_clk_enable(scpd->subsys_clk, MAX_SUBSYS_CLKS);
	if (ret < 0)
		goto err_pwr_ack;

	ret = scpsys_sram_enable(scpd, ctl_addr);
	if (ret < 0)
		goto err_sram;

	ret = scpsys_bus_protect_disable(scpd, MAX_STEPS - 1);
	if (ret < 0)
		goto err_sram;

	return 0;

err_sram:
	scpsys_clk_disable(scpd->subsys_clk, MAX_SUBSYS_CLKS);
err_pwr_ack:
	scpsys_clk_disable(scpd->clk, MAX_CLKS);
err_clk:
	scpsys_extb_iso_up(scpd);
	scpsys_regulator_disable(scpd);

	dev_notice(scp->dev, "Failed to power on domain %s\n", genpd->name);

	return ret;
}

static int scpsys_md_power_off(struct generic_pm_domain *genpd)
{
	struct scp_domain *scpd = container_of(genpd, struct scp_domain, genpd);
	struct scp *scp = scpd->scp;
	void __iomem *ctl_addr = scp->base + scpd->data->ctl_offs;
	u32 val;
	int ret, tmp;

	ret = scpsys_bus_protect_enable(scpd);
	if (ret < 0)
		goto out;

	ret = scpsys_sram_disable(scpd, ctl_addr);
	if (ret < 0)
		goto out;

	scpsys_clk_disable(scpd->subsys_clk, MAX_SUBSYS_CLKS);

	/* subsys power off */
	val = readl(ctl_addr) & ~PWR_ON_BIT;
	writel(val, ctl_addr);

	/* wait until PWR_ACK = 0 */
	if (MTK_SCPD_CAPS(scpd, MTK_SCPD_IS_PWR_CON_ON))
		ret = readx_poll_timeout_atomic(scpsys_pwr_ack_is_on, scpd, tmp, tmp == 0,
				MTK_POLL_DELAY_US, MTK_POLL_TIMEOUT);
	else
		ret = readx_poll_timeout_atomic(scpsys_sta_is_on, scpd, tmp, tmp == 0,
				 MTK_POLL_DELAY_US, MTK_POLL_TIMEOUT);
	if (ret < 0)
		goto out;

	/* for md subsys, the isolation is prior to RST_B operation */
	scpsys_extb_iso_up(scpd);
	if (!MTK_SCPD_CAPS(scpd, MTK_SCPD_REMOVE_MD_RSTB)) {
		val &= ~PWR_RST_B_BIT;
		writel(val, ctl_addr);
	}

	scpsys_clk_disable(scpd->clk, MAX_CLKS);

	ret = scpsys_regulator_disable(scpd);
	if (ret < 0)
		goto out;

	return 0;

out:
	dev_notice(scp->dev, "Failed to power off domain %s\n", genpd->name);

	return ret;
}

static int scpsys_apu_power_on(struct generic_pm_domain *genpd)
{
	struct scp_domain *scpd = container_of(genpd, struct scp_domain, genpd);
	struct scp *scp = scpd->scp;
	int ret = 0;

	if (g_apucb && g_apucb->apu_power_on) {
		ret = g_apucb->apu_power_on();
		if (ret) {
			dev_notice(scp->dev,
				"Failed to power on domain %s\n", genpd->name);
		}
	}
	return ret;
}

static int scpsys_apu_power_off(struct generic_pm_domain *genpd)
{
	struct scp_domain *scpd = container_of(genpd, struct scp_domain, genpd);
	struct scp *scp = scpd->scp;
	int ret = 0;

	if (g_apucb && g_apucb->apu_power_off) {
		ret = g_apucb->apu_power_off();
		if (ret) {
			dev_notice(scp->dev,
				"Failed to power off domain %s\n", genpd->name);
		}
	}
	return ret;
}

static int mtk_hwv_is_done(struct scp_domain *scpd)
{
	struct scp *scp = scpd->scp;
	struct regmap *hwv_regmap;
	u32 val = 0, mask = 0;

	if (scpd->hwv_regmap)
		hwv_regmap = scpd->hwv_regmap;
	else if (scp->hwv_regmap)
		hwv_regmap = scp->hwv_regmap;
	else
		return 0;

	regmap_read(hwv_regmap , scpd->data->hwv_done_ofs, &val);

	mask = BIT(scpd->data->hwv_shift);
	if ((val & mask) == mask)
		return 1;

	return 0;
}

static int mtk_hwv_is_enable_done(struct scp_domain *scpd)
{
	struct scp *scp = scpd->scp;
	struct regmap *hwv_regmap;
	u32 val = 0, val2 = 0, val3 = 0;

	if (scpd->hwv_regmap)
		hwv_regmap = scpd->hwv_regmap;
	else if (scp->hwv_regmap)
		hwv_regmap = scp->hwv_regmap;
	else
		return 0;

	regmap_read(hwv_regmap, scpd->data->hwv_done_ofs, &val);
	regmap_read(hwv_regmap, scpd->data->hwv_en_ofs, &val2);
	regmap_read(hwv_regmap, scpd->data->hwv_set_sta_ofs, &val3);

	if ((val & BIT(scpd->data->hwv_shift)) && (val2 & BIT(scpd->data->hwv_shift))
			&& ((val3 & BIT(scpd->data->hwv_shift)) == 0x0))
		return 1;

	return 0;
}

static int mtk_hwv_is_disable_done(struct scp_domain *scpd)
{
	struct scp *scp = scpd->scp;
	struct regmap *hwv_regmap;
	u32 val = 0, val2 = 0;

	if (scpd->hwv_regmap)
		hwv_regmap = scpd->hwv_regmap;
	else if (scp->hwv_regmap)
		hwv_regmap = scp->hwv_regmap;
	else
		return 0;

	regmap_read(hwv_regmap, scpd->data->hwv_done_ofs, &val);
	regmap_read(hwv_regmap, scpd->data->hwv_clr_sta_ofs, &val2);

	if ((val & BIT(scpd->data->hwv_shift)) && ((val2 & BIT(scpd->data->hwv_shift)) == 0x0))
		return 1;

	return 0;
}

static bool mtk_check_vcp_is_ready(struct scp_domain *scpd)
{
	if (scpsys_ops == NULL || scpsys_ops->is_vcp_ready == NULL)
		return true;

	return scpsys_ops->is_vcp_ready(scpd);
}

static bool mtk_hwv_is_bus_busy(struct scp_domain *scpd)
{
	if (scpsys_ops == NULL || scpsys_ops->is_bus_busy == NULL)
		return false;

	return scpsys_ops->is_bus_busy(scpd);
}

static int scpsys_hwv_power_on(struct generic_pm_domain *genpd)
{
	struct scp_domain *scpd = container_of(genpd, struct scp_domain, genpd);
	struct scp *scp = scpd->scp;
	struct regmap *hwv_regmap;
	u32 val = 0;
	int ret = 0;
	int tmp;
	int i = 0;

	if (scpd->hwv_regmap)
		hwv_regmap = scpd->hwv_regmap;
	else
		hwv_regmap = scp->hwv_regmap;

	if (MTK_SCPD_CAPS(scpd, MTK_SCPD_INFRA_REQ_OPT))
		turn_onoff_infra(INFRA_ON);

	/* check if bus busy */
	if (mtk_hwv_is_bus_busy(scpd))
		goto err_busy_busy;

	if (MTK_SCPD_CAPS(scpd, MTK_SCPD_WAIT_VCP)) {
		/* wait until vcp is ready, check 0x1c00091c[1] = 1 */
		ret = readx_poll_timeout_atomic(mtk_check_vcp_is_ready, scpd,
				tmp, tmp > 0, MTK_POLL_DELAY_US, MTK_POLL_1S_TIMEOUT);
		if (ret < 0)
			goto err_vcp_ready;
	}

	ret = scpsys_regulator_enable(scpd);
	if (ret < 0)
		goto err_regulator;

	ret = scpsys_clk_enable(scpd->clk, MAX_CLKS);
	if (ret)
		goto err_clk;

	ret = scpsys_clk_enable(scpd->lp_clk, MAX_CLKS);
	if (ret)
		goto err_lp_clk;

	/* wait for irq status idle */
	ret = readx_poll_timeout_atomic(mtk_hwv_is_done, scpd, tmp, tmp > 0,
			MTK_POLL_DELAY_US, MTK_POLL_IRQ_TIMEOUT);
	if (ret < 0)
		goto err_hwv_prepare;

	val = BIT(scpd->data->hwv_shift);
	regmap_write(hwv_regmap, scpd->data->hwv_set_ofs, val);
	do {
		regmap_read(hwv_regmap, scpd->data->hwv_set_ofs, &val);
		if ((val & BIT(scpd->data->hwv_shift)) != 0)
			break;

		if (i > MTK_POLL_HWV_PREPARE_CNT)
			goto err_hwv_vote;

		udelay(MTK_POLL_HWV_PREPARE_US);
		i++;
	} while (1);

	/* add debounce time */
	udelay(1);

	if (scpd->data->hwv_debug_history_ofs) {
		regmap_read(hwv_regmap, scpd->data->hwv_debug_history_ofs, &val);
		regmap_write(hwv_regmap, scpd->data->hwv_debug_history_ofs, val + 1);
	}

	/* wait until VOTER_ACK = 1 */
	ret = readx_poll_timeout_atomic(mtk_hwv_is_enable_done, scpd, tmp, tmp > 0,
			MTK_POLL_DELAY_US, MTK_POLL_300MS_TIMEOUT);
	if (ret < 0)
		goto err_hwv_done;

	scpsys_clk_disable(scpd->lp_clk, MAX_CLKS);

	scpd->is_on = true;

	if (MTK_SCPD_CAPS(scpd, MTK_SCPD_INFRA_REQ_OPT))
		turn_onoff_infra(INFRA_OFF);

	return 0;

err_busy_busy:
	dev_notice(scp->dev, "Failed to keep bus idle %s(%d)\n", genpd->name, ret);
err_vcp_ready:
	dev_notice(scp->dev, "Failed to vcp ready timeout %s\n", genpd->name);
err_hwv_done:
	dev_err(scp->dev, "Failed to hwv done timeout %s(%d)\n", genpd->name, ret);
err_hwv_vote:
	dev_err(scp->dev, "Failed to hwv vote timeout %s(%d %x)\n", genpd->name, ret, val);
err_hwv_prepare:
	regmap_read(hwv_regmap, scpd->data->hwv_done_ofs, &val);
	dev_err(scp->dev, "Failed to hwv prepare timeout %s(%d %x)\n", genpd->name, ret, val);
err_lp_clk:
	dev_err(scp->dev, "Failed to enable lp clk %s(%d)\n", genpd->name, ret);
err_clk:
	dev_err(scp->dev, "Failed to enable clk %s(%d)\n", genpd->name, ret);
err_regulator:
	dev_err(scp->dev, "Failed to power on domain %s(%d)\n", genpd->name, ret);

	return ret;
}

static int scpsys_hwv_power_off(struct generic_pm_domain *genpd)
{
	struct scp_domain *scpd = container_of(genpd, struct scp_domain, genpd);
	struct scp *scp = scpd->scp;
	struct regmap *hwv_regmap;
	struct regmap *cksys2_regmap = NULL;
	u32 val = 0;
	int ret = 0;
	int tmp;
	int i = 0;

	if (scpd->hwv_regmap)
		hwv_regmap = scpd->hwv_regmap;
	else
		hwv_regmap = scp->hwv_regmap;

	if (scpd->cksys2_regmap)
		cksys2_regmap = scpd->cksys2_regmap;

	if (MTK_SCPD_CAPS(scpd, MTK_SCPD_INFRA_REQ_OPT))
		turn_onoff_infra(INFRA_ON);

	if (MTK_SCPD_CAPS(scpd, MTK_SCPD_WAIT_VCP)) {
		/* wait until vcp is ready, check 0x1c00091c[1] = 1 */
		ret = readx_poll_timeout_atomic(mtk_check_vcp_is_ready, scpd,
				tmp, tmp > 0, MTK_POLL_DELAY_US, MTK_POLL_1S_TIMEOUT);
		if (ret < 0)
			goto err_vcp_ready;
	}

	ret = scpsys_clk_enable(scpd->lp_clk, MAX_CLKS);
	if (ret)
		goto err_lp_clk;

	if (MTK_SCPD_CAPS(scpd, MTK_SCPD_HWV_CHK_MUX_OPT)) {
		if (cksys2_regmap) {
			/* chk mux is on */
			regmap_read(cksys2_regmap, scpd->data->chk_data.hwv_debug_mux_ofs_opt, &val);
			if ((val & scpd->data->chk_data.hwv_debug_mux_shift_opt) == 0) {
				ret = -EINVAL;
				goto err_mux_off;
			}
		}
	}

	/* wait for irq status idle */
	ret = readx_poll_timeout_atomic(mtk_hwv_is_done, scpd, tmp, tmp > 0,
			MTK_POLL_DELAY_US, MTK_POLL_IRQ_TIMEOUT);
	if (ret < 0)
		goto err_hwv_prepare;

	val = BIT(scpd->data->hwv_shift);
	regmap_write(hwv_regmap, scpd->data->hwv_clr_ofs, val);
	do {
		regmap_read(hwv_regmap, scpd->data->hwv_clr_ofs, &val);
		if ((val & BIT(scpd->data->hwv_shift)) == 0)
			break;

		if (i > MTK_POLL_HWV_PREPARE_CNT) {
			ret = -ETIMEDOUT;
			goto err_hwv_vote;
		}
		i++;
		udelay(MTK_POLL_HWV_PREPARE_US);
	} while (1);

	/* delay 100us for stable status */
	udelay(100);

	if (scpd->data->hwv_debug_history_ofs) {
		regmap_read(hwv_regmap, scpd->data->hwv_debug_history_ofs, &val);
		regmap_write(hwv_regmap, scpd->data->hwv_debug_history_ofs, val + 1);
	}

	/* wait until VOTER_ACK = 0 */
	ret = readx_poll_timeout_atomic(mtk_hwv_is_disable_done, scpd, tmp, tmp > 0,
			MTK_POLL_DELAY_US, MTK_POLL_300MS_TIMEOUT);
	if (ret < 0)
		goto err_hwv_done;

	scpsys_clk_disable(scpd->clk, MAX_CLKS);

	scpsys_clk_disable(scpd->lp_clk, MAX_CLKS);

	ret = scpsys_regulator_disable(scpd);
	if (ret < 0)
		goto err_regulator;

	scpd->is_on = false;

	if (MTK_SCPD_CAPS(scpd, MTK_SCPD_INFRA_REQ_OPT))
		turn_onoff_infra(INFRA_OFF);

	return 0;

err_mux_off:
	regmap_read(cksys2_regmap, scpd->data->chk_data.hwv_debug_mux_ofs_opt, &val);
	dev_err(scp->dev, "Mux is off %s(%x)\n", genpd->name, val);
err_vcp_ready:
	dev_notice(scp->dev, "Failed to vcp ready timeout %s\n", genpd->name);
err_regulator:
	dev_err(scp->dev, "Failed to regulator disable %s(%d)\n", genpd->name, ret);
err_hwv_done:
	dev_err(scp->dev, "Failed to hwv done timeout %s(%d)\n", genpd->name, ret);
err_hwv_vote:
	dev_err(scp->dev, "Failed to hwv vote timeout %s(%d %x)\n", genpd->name, ret, val);
err_hwv_prepare:
	regmap_read(hwv_regmap, scpd->data->hwv_done_ofs, &val);
	dev_err(scp->dev, "Failed to hwv prepare timeout %s(%d %x)\n", genpd->name, ret, val);
	scpsys_clk_disable(scpd->lp_clk, MAX_CLKS);
err_lp_clk:
	dev_err(scp->dev, "Failed to power off domain %s(%d)\n", genpd->name, ret);

	return ret;
}

static int scpsys_pbus_power_on(struct generic_pm_domain *genpd)
{
	struct scp_domain *scpd = container_of(genpd, struct scp_domain, genpd);
	struct scp *scp = scpd->scp;
	void __iomem *ctl_addr = scp->base + scpd->data->ctl_offs;
	u32 val;
	int ret, tmp;
	unsigned int pd_start_time, pd_end_time;

	if (MTK_SCPD_CAPS(scpd, MTK_SCPD_PROFILE))
		pd_start_time = jiffies_to_msecs(jiffies);

	/* Turn on MTCMOS */
	val = readl(ctl_addr);
	val |= MTCMOS_SSYS_PWR_ON_OFF;
	writel(val, ctl_addr);

	/* wait until PWR_ACK == 1 */
	ret = readx_poll_timeout_atomic(scpsys_pbus_pwr_ack_is_on, scpd, tmp, tmp > 0,
			MTK_POLL_DELAY_US, MTK_POLL_TIMEOUT);

	if (ret < 0)
		goto err_pwr_ack;

	/* P-Bus Mode MTCMOS Tie On */
	val = readl(ctl_addr);
	val |= SSYSPM_MTCMOS_DEFAULT_ON;
	writel(val, ctl_addr);

	if (MTK_SCPD_CAPS(scpd, MTK_SCPD_PROFILE)) {
		pd_end_time = jiffies_to_msecs(jiffies);
		if ((pd_end_time - pd_start_time) > MTK_PROFILE_TIMEOUT)
			dev_err(scp->dev, "%s profile time(%u)\n",
				genpd->name, (pd_end_time - pd_start_time));
	}

	return 0;

err_pwr_ack:
	dev_err(scp->dev, "Failed to power on(pbus mode) mtcmos %s(%d)\n", genpd->name, ret);
	return ret;
}

static int scpsys_pbus_power_off(struct generic_pm_domain *genpd)
{
	struct scp_domain *scpd = container_of(genpd, struct scp_domain, genpd);
	struct scp *scp = scpd->scp;
	void __iomem *ctl_addr = scp->base + scpd->data->ctl_offs;
	u32 val;
	int ret, tmp;
	unsigned int pd_start_time, pd_end_time;

	if (MTK_SCPD_CAPS(scpd, MTK_SCPD_PROFILE))
		pd_start_time = jiffies_to_msecs(jiffies);

	if (MTK_SCPD_CAPS(scpd, MTK_SCPD_BYPASS_OFF)) {
		dev_err(scp->dev, "bypass power off %s for bringup\n", genpd->name);
		return 0;
	}

	/* Disable P-Bus Mode Tie Value */
	val = readl(ctl_addr);
	val &= ~SSYSPM_MTCMOS_DEFAULT_ON;
	writel(val, ctl_addr);

	/* Turn off MTCMOS */
	val = readl(ctl_addr);
	val &= ~MTCMOS_SSYS_PWR_ON_OFF;
	writel(val, ctl_addr);

	/* wait until PWR_ACK == 0 */
	ret = readx_poll_timeout_atomic(scpsys_pbus_pwr_ack_is_on, scpd, tmp, tmp == 0,
			MTK_POLL_DELAY_US, MTK_POLL_TIMEOUT);

	if (ret < 0)
		goto err_pwr_ack;

	if (MTK_SCPD_CAPS(scpd, MTK_SCPD_PROFILE)) {
		pd_end_time = jiffies_to_msecs(jiffies);
		if ((pd_end_time - pd_start_time) > MTK_PROFILE_TIMEOUT)
			dev_err(scp->dev, "%s profile time(%u)\n",
				genpd->name, (pd_end_time - pd_start_time));
	}

	return 0;

err_pwr_ack:
	dev_err(scp->dev, "Failed to power off(pbus mode) mtcmos %s(%d)\n", genpd->name, ret);
	return ret;
}

static int mtk_mminfra_hwv_is_enable_done(struct scp_domain *scpd)
{
	u32 val = 0;

	regmap_read(scpd->hwv_regmap, scpd->data->hwv_done_ofs, &val);

	if (val == scpd->data->sta_mask)
		return 1;

	return 0;
}

int __mminfra_hwv_power_ctrl(struct scp_domain *scpd, struct regmap *regmap,
			 struct device *dev, const char *name, bool onoff)
{
	u32 vote_ofs;
	u32 vote_sta;
	u32 vote_msk;
	u32 vote_ack;
	u32 val = 0, val2 = 0;
	int ret = 0;
	int tmp = 0;
	int i = 0;

	/* wait until vcp is ready, check 0x1c00091c[1] = 1 */
	if (MTK_SCPD_CAPS(scpd, MTK_SCPD_WAIT_VCP)) {
		ret = readx_poll_timeout_atomic(mtk_check_vcp_is_ready, scpd, tmp, tmp > 0,
				MTK_POLL_DELAY_US, MTK_POLL_1S_TIMEOUT);
		if (ret < 0)
			goto err_vcp_ready;
	}

	val = BIT(scpd->data->hwv_shift);
	vote_msk = BIT(scpd->data->hwv_shift);
	vote_sta = scpd->data->hwv_ofs;
	if (onoff) {
		vote_ofs = scpd->data->hwv_set_ofs;
		vote_ack = BIT(scpd->data->hwv_shift);
	} else {
		vote_ofs = scpd->data->hwv_clr_ofs;
		vote_ack = 0x0;
	}

	/* write twice to prevent clk idle */
	regmap_write(regmap, vote_ofs, val);

	do {
		regmap_write(regmap, vote_ofs, val);
		regmap_read(regmap, vote_sta, &val);
		if ((val & vote_msk) == vote_ack)
			break;

		if (i > MTK_POLL_HWV_PREPARE_CNT)
			goto err_hwv_vote;

		udelay(MTK_POLL_HWV_PREPARE_US);
		i++;
	} while (1);

	udelay(10);

	if (onoff) {
		tmp = 0;
		/* wait until VOTER_ACK = 1 */
		ret = readx_poll_timeout_atomic(mtk_mminfra_hwv_is_enable_done, scpd, tmp, tmp > 0,
				MTK_POLL_DELAY_US, MTK_POLL_300MS_TIMEOUT);
		if (ret < 0)
			goto err_hwv_done;
	}

	return 0;

err_vcp_ready:
	regmap_read(regmap, scpd->data->hwv_done_ofs, &val2);
	dev_err(dev, "Failed to vcp ready timeout %s(%d)\n", name, val2);
err_hwv_done:
	regmap_read(regmap, scpd->data->hwv_done_ofs, &val2);
	dev_err(dev, "Failed to hwv done timeout %s(%d)\n", name, val2);
err_hwv_vote:
	dev_err(dev, "Failed to hwv vote timeout %s(%d %x)\n", name, ret, val);

	return ret;
}
EXPORT_SYMBOL_GPL(__mminfra_hwv_power_ctrl);

static int scpsys_mminfra_hwv_power_on(struct generic_pm_domain *genpd)
{
	struct scp_domain *scpd = container_of(genpd, struct scp_domain, genpd);
	struct scp *scp = scpd->scp;

	return __mminfra_hwv_power_ctrl(scpd, scpd->hwv_regmap,
			scp->dev, genpd->name, true);
}

static int scpsys_mminfra_hwv_power_off(struct generic_pm_domain *genpd)
{
	struct scp_domain *scpd = container_of(genpd, struct scp_domain, genpd);
	struct scp *scp = scpd->scp;

	return __mminfra_hwv_power_ctrl(scpd, scpd->hwv_regmap,
			 scp->dev, genpd->name, false);
}

static int init_subsys_clks(struct platform_device *pdev,
		const char *prefix, struct clk **clk)
{
	struct device_node *node = pdev->dev.of_node;
	u32 prefix_len, sub_clk_cnt = 0;
	struct property *prop;
	const char *clk_name;

	if (!node) {
		dev_notice(&pdev->dev, "Cannot find scpsys node: %ld\n",
			PTR_ERR(node));
		return PTR_ERR(node);
	}

	prefix_len = strlen(prefix);

	of_property_for_each_string(node, "clock-names", prop, clk_name) {
		if (!strncmp(clk_name, prefix, prefix_len) &&
				(clk_name[prefix_len] == '-')) {
			if (sub_clk_cnt >= MAX_SUBSYS_CLKS) {
				dev_notice(&pdev->dev,
					"subsys clk out of range %d\n",
					sub_clk_cnt);
				return -EINVAL;
			}

			clk[sub_clk_cnt] = devm_clk_get(&pdev->dev,
						clk_name);

			if (IS_ERR(clk[sub_clk_cnt])) {
				dev_notice(&pdev->dev,
					"Subsys clk get fail %ld\n",
					PTR_ERR(clk[sub_clk_cnt]));
				return PTR_ERR(clk[sub_clk_cnt]);
			}
			sub_clk_cnt++;
		}
	}

	return sub_clk_cnt;
}

static int init_basic_clks(struct platform_device *pdev, struct clk **clk,
			const char * const *name)
{
	int i;

	for (i = 0; i < MAX_CLKS && name[i]; i++) {
		clk[i] = devm_clk_get(&pdev->dev, name[i]);

		if (IS_ERR(clk[i])) {
			dev_notice(&pdev->dev,
				"get basic clk %s fail %ld\n",
				name[i], PTR_ERR(clk[i]));
			return PTR_ERR(clk[i]);
		}
	}

	return 0;
}

static int mtk_pd_set_performance(struct generic_pm_domain *genpd,
				  unsigned int state)
{
	int i;
	struct scp_domain *scpd =
		container_of(genpd, struct scp_domain, genpd);
	struct scp_event_data scpe;
	struct scp *scp = scpd->scp;
	struct genpd_onecell_data *pd_data = &scp->pd_data;

	for (i = 0; i < pd_data->num_domains; i++) {
		if (genpd == pd_data->domains[i]) {
			dev_dbg(scp->dev, "%d. %s = %d\n",
				i, genpd->name, state);
			break;
		}
	}

	if (i == pd_data->num_domains)
		return 0;

	scpe.event_type = MTK_SCPSYS_PSTATE;
	scpe.genpd = genpd;
	scpe.domain_id = i;
	blocking_notifier_call_chain(&scpsys_notifier_list, state, &scpe);

	return 0;
}

static unsigned int mtk_pd_get_performance(struct generic_pm_domain *genpd,
					   struct dev_pm_opp *opp)
{
	return dev_pm_opp_get_level(opp);
}

static int mtk_pd_get_regmap(struct platform_device *pdev, struct regmap **regmap,
			const char *name)
{
	if (!strcmp("smi_comm", name)) {
		struct device_node *smi_node;

		smi_node = of_parse_phandle(pdev->dev.of_node, name, 0);
		if (smi_node)
			*regmap = device_node_to_regmap(smi_node);
		else
			*regmap = ERR_PTR(-ENODEV);
	} else {
		*regmap = syscon_regmap_lookup_by_phandle(pdev->dev.of_node, name);
	}

	if (PTR_ERR(*regmap) == -ENODEV) {
		dev_notice(&pdev->dev, "%s regmap is null(%ld)\n",
				name, PTR_ERR(*regmap));

		*regmap = NULL;
	} else if (IS_ERR(*regmap)) {
		dev_notice(&pdev->dev, "Cannot find %s controller: %ld\n",
				name, PTR_ERR(*regmap));

		return PTR_ERR(*regmap);
	}

	return 0;
}

static int smc_turn_on_infra(void)
{
	struct arm_smccc_res res;

	arm_smccc_smc(MTK_SIP_KERNEL_HWCCF_CONTROL, INFRA_TURN_ON,
		0, 0, 0, 0, 0, 0, &res);
	return res.a0;
}

static int smc_turn_off_infra(void)
{
	struct arm_smccc_res res;

	arm_smccc_smc(MTK_SIP_KERNEL_HWCCF_CONTROL, INFRA_TURN_OFF,
		0, 0, 0, 0, 0, 0, &res);
	return res.a0;
}

static void turn_onoff_infra(enum infra_onoff_enum on)
{
	static int ref_cnt;
	unsigned long flags;

	hwccf_lock(flags);
	if (on) {
		ref_cnt++;
		if (ref_cnt == 1)
			smc_turn_on_infra();
	} else {
		ref_cnt--;
		if (ref_cnt == 0)
			smc_turn_off_infra();
		else if (ref_cnt < 0)
			pr_err("infra ref_cnt < 0: %d\n", ref_cnt);
	}
	hwccf_unlock(flags);

}

void set_scpsys_ops(const struct scpsys_plat_ops *ops)
{
	scpsys_ops = ops;
}
EXPORT_SYMBOL(set_scpsys_ops);

struct scp *init_scp(struct platform_device *pdev,
			const struct scp_domain_data *scp_domain_data, int num,
			const struct scp_ctrl_reg *scp_ctrl_reg,
			const char *bus_list[],
			unsigned int type_num)
{
	struct genpd_onecell_data *pd_data;
	struct resource *res;
	int i, ret;
	struct scp *scp;

	scp = devm_kzalloc(&pdev->dev, sizeof(*scp), GFP_KERNEL);
	if (!scp)
		return ERR_PTR(-ENOMEM);

	scp->ctrl_reg.pwr_sta_offs = scp_ctrl_reg->pwr_sta_offs;
	scp->ctrl_reg.pwr_sta2nd_offs = scp_ctrl_reg->pwr_sta2nd_offs;

	scp->dev = &pdev->dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	scp->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(scp->base)) {
		if (scp->base == IOMEM_ERR_PTR(-EBUSY))
			scp->base = devm_ioremap(&pdev->dev, res->start, resource_size(res));
		else
			return ERR_CAST(scp->base);
	}

	scp->domains = devm_kcalloc(&pdev->dev,
				num, sizeof(*scp->domains), GFP_KERNEL);
	if (!scp->domains)
		return ERR_PTR(-ENOMEM);

	pd_data = &scp->pd_data;

	pd_data->domains = devm_kcalloc(&pdev->dev,
			num, sizeof(*pd_data->domains), GFP_KERNEL);
	if (!pd_data->domains)
		return ERR_PTR(-ENOMEM);

	scp->num_bus_type = type_num;
	scp->bus_regmap = devm_kcalloc(&pdev->dev,
			type_num, sizeof(*scp->bus_regmap), GFP_KERNEL);
	if (!scp->bus_regmap)
		return ERR_PTR(-ENOMEM);

	/* get bus prot regmap from dts node, 0 means invalid bus type */
	for (i = 1; i < type_num; i++) {
		ret = mtk_pd_get_regmap(pdev, &scp->bus_regmap[i], bus_list[i]);
		if (ret)
			return ERR_PTR(ret);
	}

	/* get hw voter regmap from dts node */
	ret = mtk_pd_get_regmap(pdev, &scp->hwv_regmap, "hw-voter-regmap");
	if (ret)
		return ERR_PTR(ret);

	for (i = 0; i < num; i++) {
		struct scp_domain *scpd = &scp->domains[i];
		const struct scp_domain_data *data = &scp_domain_data[i];

		if (!data->hwv_comp)
			continue;

		ret = mtk_pd_get_regmap(pdev, &scpd->hwv_regmap, data->hwv_comp);
		if (ret)
			return ERR_PTR(ret);
	}

	for (i = 0; i < num; i++) {
		struct scp_domain *scpd = &scp->domains[i];
		const struct scp_domain_data *data = &scp_domain_data[i];

		scpd->supply = devm_regulator_get_optional(&pdev->dev, data->name);
		if (IS_ERR(scpd->supply)) {
			if (PTR_ERR(scpd->supply) == -ENODEV)
				scpd->supply = NULL;
			else
				return ERR_CAST(scpd->supply);
		}
	}

	pd_data->num_domains = num;

	for (i = 0; i < num; i++) {
		struct scp_domain *scpd = &scp->domains[i];
		struct generic_pm_domain *genpd = &scpd->genpd;
		const struct scp_domain_data *data = &scp_domain_data[i];

		pd_data->domains[i] = genpd;
		scpd->scp = scp;

		scpd->data = data;

		/* get cksys2 regmap from dts node */
		ret = mtk_pd_get_regmap(pdev, &scpd->cksys2_regmap, "cksys2-regmap");
		if (ret)
			return ERR_PTR(ret);

		ret = init_basic_clks(pdev, scpd->clk, data->basic_clk_name);
		if (ret)
			return ERR_PTR(ret);
		ret = init_basic_clks(pdev, scpd->lp_clk, data->basic_lp_clk_name);
		if (ret)
			return ERR_PTR(ret);

		if (data->subsys_clk_prefix) {
			ret = init_subsys_clks(pdev,
					data->subsys_clk_prefix,
					scpd->subsys_clk);
			if (ret < 0) {
				dev_notice(&pdev->dev,
					"%s: subsys clk unavailable\n",
					data->name);
				return ERR_PTR(ret);
			}
		}

		if (data->subsys_lp_clk_prefix) {
			ret = init_subsys_clks(pdev,
					data->subsys_lp_clk_prefix,
					scpd->subsys_lp_clk);
			if (ret < 0) {
				dev_notice(&pdev->dev,
					"%s: subsys clk unavailable\n",
					data->name);
				return ERR_PTR(ret);
			}
		}

		genpd->name = data->name;
		if (MTK_SCPD_CAPS(scpd, MTK_SCPD_MD_OPS)) {
			genpd->power_off = scpsys_md_power_off;
			genpd->power_on = scpsys_md_power_on;
		} else if (MTK_SCPD_CAPS(scpd, MTK_SCPD_APU_OPS)) {
			genpd->power_on = scpsys_apu_power_on;
			genpd->power_off = scpsys_apu_power_off;
		} else if (MTK_SCPD_CAPS(scpd, MTK_SCPD_HWV_OPS)) {
			genpd->power_on = scpsys_hwv_power_on;
			genpd->power_off = scpsys_hwv_power_off;
		} else if (MTK_SCPD_CAPS(scpd, MTK_SCPD_MMINFRA_HWV_OPS)) {
			genpd->power_on = scpsys_mminfra_hwv_power_on;
			genpd->power_off = scpsys_mminfra_hwv_power_off;
		} else if (MTK_SCPD_CAPS(scpd, MTK_SCPD_PBUS_OPS)) {
			genpd->power_on = scpsys_pbus_power_on;
			genpd->power_off = scpsys_pbus_power_off;
		} else {
			genpd->power_off = scpsys_power_off;
			genpd->power_on = scpsys_power_on;
		}

		if (MTK_SCPD_CAPS(scpd, MTK_SCPD_ACTIVE_WAKEUP))
			genpd->flags |= GENPD_FLAG_ACTIVE_WAKEUP;
		if (MTK_SCPD_CAPS(scpd, MTK_SCPD_ALWAYS_ON))
			genpd->flags |= GENPD_FLAG_ALWAYS_ON;
		if (MTK_SCPD_CAPS(scpd, MTK_SCPD_IRQ_SAVE))
			genpd->flags |= GENPD_FLAG_IRQ_SAFE;

		/* Add opp table check first to avoid OF runtime parse failed */
		if (of_count_phandle_with_args(pdev->dev.of_node,
		    "operating-points-v2", NULL) > 0) {
			genpd->set_performance_state = mtk_pd_set_performance;
			genpd->opp_to_performance_state =
				mtk_pd_get_performance;
		}
	}

	return scp;
}
EXPORT_SYMBOL_GPL(init_scp);

int mtk_register_power_domains(struct platform_device *pdev,
				struct scp *scp, int num)
{
	struct genpd_onecell_data *pd_data;
	int i, ret = 0;

	for (i = 0; i < num; i++) {
		struct scp_domain *scpd = &scp->domains[i];
		struct generic_pm_domain *genpd = &scpd->genpd;
		bool on;

		if (MTK_SCPD_CAPS(scpd, MTK_SCPD_BYPASS_CLK))
			bypass_first_cg_off = true;

		/*
		 * Initially turn on all domains to make the domains usable
		 * with !CONFIG_PM and to get the hardware in sync with the
		 * software.  The unused domains will be switched off during
		 * late_init time.
		 */
		if (MTK_SCPD_CAPS(scpd, MTK_SCPD_BYPASS_INIT_ON))
			on = false;
		else {
			ret = genpd->power_on(genpd);
			on = !WARN_ON(ret < 0);
			if (ret)
				clkchk_external_dump();
		}

		pm_genpd_init(genpd, NULL, !on);
		bypass_first_cg_off = false;
	}

	/*
	 * We are not allowed to fail here since there is no way to unregister
	 * a power domain. Once registered above we have to keep the domains
	 * valid.
	 */

	pd_data = &scp->pd_data;

	ret = of_genpd_add_provider_onecell(pdev->dev.of_node, pd_data);
	if (ret)
		dev_err(&pdev->dev, "Failed to add OF provider: %d\n", ret);

	return ret;
}
EXPORT_SYMBOL(mtk_register_power_domains);

/*
 * MT2701 power domain support
 */

static const struct scp_domain_data scp_domain_data_mt2701[] = {
	[MT2701_POWER_DOMAIN_CONN] = {
		.name = "conn",
		.sta_mask = PWR_STATUS_CONN,
		.ctl_offs = SPM_CONN_PWR_CON,
		.bp_table = {
			BUS_PROT(IFR_TYPE, 0, 0, 0x220, 0x228,
				MT2701_TOP_AXI_PROT_EN_CONN_M |
				MT2701_TOP_AXI_PROT_EN_CONN_S),
		},
		.caps = MTK_SCPD_ACTIVE_WAKEUP,
	},
	[MT2701_POWER_DOMAIN_DISP] = {
		.name = "disp",
		.sta_mask = PWR_STATUS_DISP,
		.ctl_offs = SPM_DIS_PWR_CON,
		.sram_pdn_bits = GENMASK(11, 8),
		.basic_clk_name = {"mm"},
		.bp_table = {
			BUS_PROT(IFR_TYPE, 0, 0, 0x220, 0x228,
				MT2701_TOP_AXI_PROT_EN_MM_M0),
		},
		.caps = MTK_SCPD_ACTIVE_WAKEUP,
	},
	[MT2701_POWER_DOMAIN_MFG] = {
		.name = "mfg",
		.sta_mask = PWR_STATUS_MFG,
		.ctl_offs = SPM_MFG_PWR_CON,
		.sram_pdn_bits = GENMASK(11, 8),
		.sram_pdn_ack_bits = GENMASK(12, 12),
		.basic_clk_name = {"mfg"},
		.caps = MTK_SCPD_ACTIVE_WAKEUP,
	},
	[MT2701_POWER_DOMAIN_VDEC] = {
		.name = "vdec",
		.sta_mask = PWR_STATUS_VDEC,
		.ctl_offs = SPM_VDE_PWR_CON,
		.sram_pdn_bits = GENMASK(11, 8),
		.sram_pdn_ack_bits = GENMASK(12, 12),
		.basic_clk_name = {"mm"},
		.caps = MTK_SCPD_ACTIVE_WAKEUP,
	},
	[MT2701_POWER_DOMAIN_ISP] = {
		.name = "isp",
		.sta_mask = PWR_STATUS_ISP,
		.ctl_offs = SPM_ISP_PWR_CON,
		.sram_pdn_bits = GENMASK(11, 8),
		.sram_pdn_ack_bits = GENMASK(13, 12),
		.basic_clk_name = {"mm"},
		.caps = MTK_SCPD_ACTIVE_WAKEUP,
	},
	[MT2701_POWER_DOMAIN_BDP] = {
		.name = "bdp",
		.sta_mask = PWR_STATUS_BDP,
		.ctl_offs = SPM_BDP_PWR_CON,
		.sram_pdn_bits = GENMASK(11, 8),
		.caps = MTK_SCPD_ACTIVE_WAKEUP,
	},
	[MT2701_POWER_DOMAIN_ETH] = {
		.name = "eth",
		.sta_mask = PWR_STATUS_ETH,
		.ctl_offs = SPM_ETH_PWR_CON,
		.sram_pdn_bits = GENMASK(11, 8),
		.sram_pdn_ack_bits = GENMASK(15, 12),
		.basic_clk_name = {"ethif"},
		.caps = MTK_SCPD_ACTIVE_WAKEUP,
	},
	[MT2701_POWER_DOMAIN_HIF] = {
		.name = "hif",
		.sta_mask = PWR_STATUS_HIF,
		.ctl_offs = SPM_HIF_PWR_CON,
		.sram_pdn_bits = GENMASK(11, 8),
		.sram_pdn_ack_bits = GENMASK(15, 12),
		.basic_clk_name = {"ethif"},
		.caps = MTK_SCPD_ACTIVE_WAKEUP,
	},
	[MT2701_POWER_DOMAIN_IFR_MSC] = {
		.name = "ifr_msc",
		.sta_mask = PWR_STATUS_IFR_MSC,
		.ctl_offs = SPM_IFR_MSC_PWR_CON,
		.caps = MTK_SCPD_ACTIVE_WAKEUP,
	},
};

/*
 * MT2712 power domain support
 */
static const struct scp_domain_data scp_domain_data_mt2712[] = {
	[MT2712_POWER_DOMAIN_MM] = {
		.name = "mm",
		.sta_mask = PWR_STATUS_DISP,
		.ctl_offs = SPM_DIS_PWR_CON,
		.sram_pdn_bits = GENMASK(8, 8),
		.sram_pdn_ack_bits = GENMASK(12, 12),
		.basic_clk_name = {"mm"},
		.caps = MTK_SCPD_ACTIVE_WAKEUP,
	},
	[MT2712_POWER_DOMAIN_VDEC] = {
		.name = "vdec",
		.sta_mask = PWR_STATUS_VDEC,
		.ctl_offs = SPM_VDE_PWR_CON,
		.sram_pdn_bits = GENMASK(8, 8),
		.sram_pdn_ack_bits = GENMASK(12, 12),
		.basic_clk_name = {"mm", "vdec"},
		.caps = MTK_SCPD_ACTIVE_WAKEUP,
	},
	[MT2712_POWER_DOMAIN_VENC] = {
		.name = "venc",
		.sta_mask = PWR_STATUS_VENC,
		.ctl_offs = SPM_VEN_PWR_CON,
		.sram_pdn_bits = GENMASK(11, 8),
		.sram_pdn_ack_bits = GENMASK(15, 12),
		.basic_clk_name = {"mm", "venc", "jpgdec"},
		.caps = MTK_SCPD_ACTIVE_WAKEUP,
	},
	[MT2712_POWER_DOMAIN_ISP] = {
		.name = "isp",
		.sta_mask = PWR_STATUS_ISP,
		.ctl_offs = SPM_ISP_PWR_CON,
		.sram_pdn_bits = GENMASK(11, 8),
		.sram_pdn_ack_bits = GENMASK(13, 12),
		.basic_clk_name = {"mm"},
		.caps = MTK_SCPD_ACTIVE_WAKEUP,
	},
	[MT2712_POWER_DOMAIN_AUDIO] = {
		.name = "audio",
		.sta_mask = PWR_STATUS_AUDIO,
		.ctl_offs = SPM_AUDIO_PWR_CON,
		.sram_pdn_bits = GENMASK(11, 8),
		.sram_pdn_ack_bits = GENMASK(15, 12),
		.basic_clk_name = {"audio"},
		.caps = MTK_SCPD_ACTIVE_WAKEUP,
	},
	[MT2712_POWER_DOMAIN_USB] = {
		.name = "usb",
		.sta_mask = PWR_STATUS_USB,
		.ctl_offs = SPM_USB_PWR_CON,
		.sram_pdn_bits = GENMASK(10, 8),
		.sram_pdn_ack_bits = GENMASK(14, 12),
		.caps = MTK_SCPD_ACTIVE_WAKEUP,
	},
	[MT2712_POWER_DOMAIN_USB2] = {
		.name = "usb2",
		.sta_mask = PWR_STATUS_USB2,
		.ctl_offs = SPM_USB2_PWR_CON,
		.sram_pdn_bits = GENMASK(10, 8),
		.sram_pdn_ack_bits = GENMASK(14, 12),
		.caps = MTK_SCPD_ACTIVE_WAKEUP,
	},
	[MT2712_POWER_DOMAIN_MFG] = {
		.name = "mfg",
		.sta_mask = PWR_STATUS_MFG,
		.ctl_offs = SPM_MFG_PWR_CON,
		.sram_pdn_bits = GENMASK(8, 8),
		.sram_pdn_ack_bits = GENMASK(16, 16),
		.basic_clk_name = {"mfg"},
		.bp_table = {
			BUS_PROT(IFR_TYPE, 0x260, 0x264, 0x220, 0x228,
				BIT(14) | BIT(21) | BIT(23)),
		},
		.caps = MTK_SCPD_ACTIVE_WAKEUP,
	},
	[MT2712_POWER_DOMAIN_MFG_SC1] = {
		.name = "mfg_sc1",
		.sta_mask = BIT(22),
		.ctl_offs = 0x02c0,
		.sram_pdn_bits = GENMASK(8, 8),
		.sram_pdn_ack_bits = GENMASK(16, 16),
		.caps = MTK_SCPD_ACTIVE_WAKEUP,
	},
	[MT2712_POWER_DOMAIN_MFG_SC2] = {
		.name = "mfg_sc2",
		.sta_mask = BIT(23),
		.ctl_offs = 0x02c4,
		.sram_pdn_bits = GENMASK(8, 8),
		.sram_pdn_ack_bits = GENMASK(16, 16),
		.caps = MTK_SCPD_ACTIVE_WAKEUP,
	},
	[MT2712_POWER_DOMAIN_MFG_SC3] = {
		.name = "mfg_sc3",
		.sta_mask = BIT(30),
		.ctl_offs = 0x01f8,
		.sram_pdn_bits = GENMASK(8, 8),
		.sram_pdn_ack_bits = GENMASK(16, 16),
		.caps = MTK_SCPD_ACTIVE_WAKEUP,
	},
};

static const struct scp_subdomain scp_subdomain_mt2712[] = {
	{MT2712_POWER_DOMAIN_MM, MT2712_POWER_DOMAIN_VDEC},
	{MT2712_POWER_DOMAIN_MM, MT2712_POWER_DOMAIN_VENC},
	{MT2712_POWER_DOMAIN_MM, MT2712_POWER_DOMAIN_ISP},
	{MT2712_POWER_DOMAIN_MFG, MT2712_POWER_DOMAIN_MFG_SC1},
	{MT2712_POWER_DOMAIN_MFG_SC1, MT2712_POWER_DOMAIN_MFG_SC2},
	{MT2712_POWER_DOMAIN_MFG_SC2, MT2712_POWER_DOMAIN_MFG_SC3},
};

/*
 * MT6797 power domain support
 */

static const struct scp_domain_data scp_domain_data_mt6797[] = {
	[MT6797_POWER_DOMAIN_VDEC] = {
		.name = "vdec",
		.sta_mask = BIT(7),
		.ctl_offs = 0x300,
		.sram_pdn_bits = GENMASK(8, 8),
		.sram_pdn_ack_bits = GENMASK(12, 12),
		.basic_clk_name = {"vdec"},
	},
	[MT6797_POWER_DOMAIN_VENC] = {
		.name = "venc",
		.sta_mask = BIT(21),
		.ctl_offs = 0x304,
		.sram_pdn_bits = GENMASK(11, 8),
		.sram_pdn_ack_bits = GENMASK(15, 12),
	},
	[MT6797_POWER_DOMAIN_ISP] = {
		.name = "isp",
		.sta_mask = BIT(5),
		.ctl_offs = 0x308,
		.sram_pdn_bits = GENMASK(9, 8),
		.sram_pdn_ack_bits = GENMASK(13, 12),
	},
	[MT6797_POWER_DOMAIN_MM] = {
		.name = "mm",
		.sta_mask = BIT(3),
		.ctl_offs = 0x30C,
		.sram_pdn_bits = GENMASK(8, 8),
		.sram_pdn_ack_bits = GENMASK(12, 12),
		.basic_clk_name = {"mm"},
		.bp_table = {
			BUS_PROT(IFR_TYPE, 0, 0, 0x220, 0x228,
				BIT(1) | BIT(2)),
		},
	},
	[MT6797_POWER_DOMAIN_AUDIO] = {
		.name = "audio",
		.sta_mask = BIT(24),
		.ctl_offs = 0x314,
		.sram_pdn_bits = GENMASK(11, 8),
		.sram_pdn_ack_bits = GENMASK(15, 12),
	},
	[MT6797_POWER_DOMAIN_MFG_ASYNC] = {
		.name = "mfg_async",
		.sta_mask = BIT(13),
		.ctl_offs = 0x334,
		.sram_pdn_bits = 0,
		.sram_pdn_ack_bits = 0,
		.basic_clk_name = {"mfg"},
	},
	[MT6797_POWER_DOMAIN_MJC] = {
		.name = "mjc",
		.sta_mask = BIT(20),
		.ctl_offs = 0x310,
		.sram_pdn_bits = GENMASK(8, 8),
		.sram_pdn_ack_bits = GENMASK(12, 12),
	},
};

#define SPM_PWR_STATUS_MT6797		0x0180
#define SPM_PWR_STATUS_2ND_MT6797	0x0184

static const struct scp_subdomain scp_subdomain_mt6797[] = {
	{MT6797_POWER_DOMAIN_MM, MT6797_POWER_DOMAIN_VDEC},
	{MT6797_POWER_DOMAIN_MM, MT6797_POWER_DOMAIN_ISP},
	{MT6797_POWER_DOMAIN_MM, MT6797_POWER_DOMAIN_VENC},
	{MT6797_POWER_DOMAIN_MM, MT6797_POWER_DOMAIN_MJC},
};

/*
 * MT7622 power domain support
 */

static const struct scp_domain_data scp_domain_data_mt7622[] = {
	[MT7622_POWER_DOMAIN_ETHSYS] = {
		.name = "ethsys",
		.sta_mask = PWR_STATUS_ETHSYS,
		.ctl_offs = SPM_ETHSYS_PWR_CON,
		.sram_pdn_bits = GENMASK(11, 8),
		.sram_pdn_ack_bits = GENMASK(15, 12),
		.bp_table = {
			BUS_PROT(IFR_TYPE, 0, 0, 0x220, 0x228,
				MT7622_TOP_AXI_PROT_EN_ETHSYS),
		},
		.caps = MTK_SCPD_ACTIVE_WAKEUP,
	},
	[MT7622_POWER_DOMAIN_HIF0] = {
		.name = "hif0",
		.sta_mask = PWR_STATUS_HIF0,
		.ctl_offs = SPM_HIF0_PWR_CON,
		.sram_pdn_bits = GENMASK(11, 8),
		.sram_pdn_ack_bits = GENMASK(15, 12),
		.basic_clk_name = {"hif_sel"},
		.bp_table = {
			BUS_PROT(IFR_TYPE, 0, 0, 0x220, 0x228,
				MT7622_TOP_AXI_PROT_EN_HIF0),
		},
		.caps = MTK_SCPD_ACTIVE_WAKEUP,
	},
	[MT7622_POWER_DOMAIN_HIF1] = {
		.name = "hif1",
		.sta_mask = PWR_STATUS_HIF1,
		.ctl_offs = SPM_HIF1_PWR_CON,
		.sram_pdn_bits = GENMASK(11, 8),
		.sram_pdn_ack_bits = GENMASK(15, 12),
		.basic_clk_name = {"hif_sel"},
		.bp_table = {
			BUS_PROT(IFR_TYPE, 0, 0, 0x220, 0x228,
				MT7622_TOP_AXI_PROT_EN_HIF1),
		},
		.caps = MTK_SCPD_ACTIVE_WAKEUP,
	},
	[MT7622_POWER_DOMAIN_WB] = {
		.name = "wb",
		.sta_mask = PWR_STATUS_WB,
		.ctl_offs = SPM_WB_PWR_CON,
		.sram_pdn_bits = 0,
		.sram_pdn_ack_bits = 0,
		.bp_table = {
			BUS_PROT(IFR_TYPE, 0, 0, 0x220, 0x228,
				MT7622_TOP_AXI_PROT_EN_WB),
		},
		.caps = MTK_SCPD_ACTIVE_WAKEUP | MTK_SCPD_FWAIT_SRAM,
	},
};

/*
 * MT7623A power domain support
 */

static const struct scp_domain_data scp_domain_data_mt7623a[] = {
	[MT7623A_POWER_DOMAIN_CONN] = {
		.name = "conn",
		.sta_mask = PWR_STATUS_CONN,
		.ctl_offs = SPM_CONN_PWR_CON,
		.bp_table = {
			BUS_PROT(IFR_TYPE, 0, 0, 0x220, 0x228,
				MT2701_TOP_AXI_PROT_EN_CONN_M |
				MT2701_TOP_AXI_PROT_EN_CONN_S),
		},
		.caps = MTK_SCPD_ACTIVE_WAKEUP,
	},
	[MT7623A_POWER_DOMAIN_ETH] = {
		.name = "eth",
		.sta_mask = PWR_STATUS_ETH,
		.ctl_offs = SPM_ETH_PWR_CON,
		.sram_pdn_bits = GENMASK(11, 8),
		.sram_pdn_ack_bits = GENMASK(15, 12),
		.basic_clk_name = {"ethif"},
		.caps = MTK_SCPD_ACTIVE_WAKEUP,
	},
	[MT7623A_POWER_DOMAIN_HIF] = {
		.name = "hif",
		.sta_mask = PWR_STATUS_HIF,
		.ctl_offs = SPM_HIF_PWR_CON,
		.sram_pdn_bits = GENMASK(11, 8),
		.sram_pdn_ack_bits = GENMASK(15, 12),
		.basic_clk_name = {"ethif"},
		.caps = MTK_SCPD_ACTIVE_WAKEUP,
	},
	[MT7623A_POWER_DOMAIN_IFR_MSC] = {
		.name = "ifr_msc",
		.sta_mask = PWR_STATUS_IFR_MSC,
		.ctl_offs = SPM_IFR_MSC_PWR_CON,
		.caps = MTK_SCPD_ACTIVE_WAKEUP,
	},
};

/*
 * MT8173 power domain support
 */

static const struct scp_domain_data scp_domain_data_mt8173[] = {
	[MT8173_POWER_DOMAIN_VDEC] = {
		.name = "vdec",
		.sta_mask = PWR_STATUS_VDEC,
		.ctl_offs = SPM_VDE_PWR_CON,
		.sram_pdn_bits = GENMASK(11, 8),
		.sram_pdn_ack_bits = GENMASK(12, 12),
		.basic_clk_name = {"mm"},
	},
	[MT8173_POWER_DOMAIN_VENC] = {
		.name = "venc",
		.sta_mask = PWR_STATUS_VENC,
		.ctl_offs = SPM_VEN_PWR_CON,
		.sram_pdn_bits = GENMASK(11, 8),
		.sram_pdn_ack_bits = GENMASK(15, 12),
		.basic_clk_name = {"mm", "venc"},
	},
	[MT8173_POWER_DOMAIN_ISP] = {
		.name = "isp",
		.sta_mask = PWR_STATUS_ISP,
		.ctl_offs = SPM_ISP_PWR_CON,
		.sram_pdn_bits = GENMASK(11, 8),
		.sram_pdn_ack_bits = GENMASK(13, 12),
		.basic_clk_name = {"mm"},
	},
	[MT8173_POWER_DOMAIN_MM] = {
		.name = "mm",
		.sta_mask = PWR_STATUS_DISP,
		.ctl_offs = SPM_DIS_PWR_CON,
		.sram_pdn_bits = GENMASK(11, 8),
		.sram_pdn_ack_bits = GENMASK(12, 12),
		.basic_clk_name = {"mm"},
		.bp_table = {
			BUS_PROT(IFR_TYPE, 0, 0, 0x220, 0x228,
				MT8173_TOP_AXI_PROT_EN_MM_M0 |
				MT8173_TOP_AXI_PROT_EN_MM_M1),
		},
	},
	[MT8173_POWER_DOMAIN_VENC_LT] = {
		.name = "venc_lt",
		.sta_mask = PWR_STATUS_VENC_LT,
		.ctl_offs = SPM_VEN2_PWR_CON,
		.sram_pdn_bits = GENMASK(11, 8),
		.sram_pdn_ack_bits = GENMASK(15, 12),
		.basic_clk_name = {"mm", "venc_lt"},
	},
	[MT8173_POWER_DOMAIN_AUDIO] = {
		.name = "audio",
		.sta_mask = PWR_STATUS_AUDIO,
		.ctl_offs = SPM_AUDIO_PWR_CON,
		.sram_pdn_bits = GENMASK(11, 8),
		.sram_pdn_ack_bits = GENMASK(15, 12),
	},
	[MT8173_POWER_DOMAIN_USB] = {
		.name = "usb",
		.sta_mask = PWR_STATUS_USB,
		.ctl_offs = SPM_USB_PWR_CON,
		.sram_pdn_bits = GENMASK(11, 8),
		.sram_pdn_ack_bits = GENMASK(15, 12),
		.caps = MTK_SCPD_ACTIVE_WAKEUP,
	},
	[MT8173_POWER_DOMAIN_MFG_ASYNC] = {
		.name = "mfg_async",
		.sta_mask = PWR_STATUS_MFG_ASYNC,
		.ctl_offs = SPM_MFG_ASYNC_PWR_CON,
		.sram_pdn_bits = GENMASK(11, 8),
		.sram_pdn_ack_bits = 0,
		.basic_clk_name = {"mfg"},
	},
	[MT8173_POWER_DOMAIN_MFG_2D] = {
		.name = "mfg_2d",
		.sta_mask = PWR_STATUS_MFG_2D,
		.ctl_offs = SPM_MFG_2D_PWR_CON,
		.sram_pdn_bits = GENMASK(11, 8),
		.sram_pdn_ack_bits = GENMASK(13, 12),
	},
	[MT8173_POWER_DOMAIN_MFG] = {
		.name = "mfg",
		.sta_mask = PWR_STATUS_MFG,
		.ctl_offs = SPM_MFG_PWR_CON,
		.sram_pdn_bits = GENMASK(13, 8),
		.sram_pdn_ack_bits = GENMASK(21, 16),
		.bp_table = {
			BUS_PROT(IFR_TYPE, 0, 0, 0x220, 0x228,
				MT8173_TOP_AXI_PROT_EN_MFG_S |
				MT8173_TOP_AXI_PROT_EN_MFG_M0 |
				MT8173_TOP_AXI_PROT_EN_MFG_M1 |
				MT8173_TOP_AXI_PROT_EN_MFG_SNOOP_OUT),
		},
	},
};

static const struct scp_subdomain scp_subdomain_mt8173[] = {
	{MT8173_POWER_DOMAIN_MFG_ASYNC, MT8173_POWER_DOMAIN_MFG_2D},
	{MT8173_POWER_DOMAIN_MFG_2D, MT8173_POWER_DOMAIN_MFG},
};

/*
 * MT8192 power domain support
 */

static const struct scp_domain_data scp_domain_data_mt8192[] = {
	[MT8192_POWER_DOMAIN_CONN] = {
		.name = "conn",
		.sta_mask = PWR_STATUS_CONN,
		.ctl_offs = 0x0304,
		.sram_pdn_bits = 0,
		.sram_pdn_ack_bits = 0,
		.bp_table = {
			BUS_PROT(IFR_TYPE, 0x2a0, 0x2a4, 0x220, 0x228,
				MT8192_TOP_AXI_PROT_EN_CONN),
			BUS_PROT(IFR_TYPE, 0x2a0, 0x2a4, 0x220, 0x228,
				MT8192_TOP_AXI_PROT_EN_CONN_2ND),
			BUS_PROT(IFR_TYPE, 0x2a8, 0x2ac, 0x250, 0x258,
				MT8192_TOP_AXI_PROT_EN_1_CONN),
		},
	},
	[MT8192_POWER_DOMAIN_MFG0] = {
		.name = "mfg",
		.sta_mask = BIT(2),
		.ctl_offs = 0x0308,
		.sram_pdn_bits = GENMASK(8, 8),
		.sram_pdn_ack_bits = GENMASK(12, 12),
		.basic_clk_name = {"mfg"},
	},
	[MT8192_POWER_DOMAIN_MFG1] = {
		.name = "mfg1",
		.sta_mask = BIT(3),
		.ctl_offs = 0x030c,
		.sram_pdn_bits = GENMASK(8, 8),
		.sram_pdn_ack_bits = GENMASK(12, 12),
		.bp_table = {
			BUS_PROT(IFR_TYPE, 0x2a8, 0x2ac, 0x250, 0x258,
				MT8192_TOP_AXI_PROT_EN_1_MFG1),
			BUS_PROT(IFR_TYPE, 0x714, 0x718, 0x710, 0x724,
				MT8192_TOP_AXI_PROT_EN_2_MFG1),
			BUS_PROT(IFR_TYPE, 0x2a0, 0x2a4, 0x220, 0x228,
				MT8192_TOP_AXI_PROT_EN_MFG1),
			BUS_PROT(IFR_TYPE, 0x714, 0x718, 0x710, 0x724,
				MT8192_TOP_AXI_PROT_EN_2_MFG1_2ND),
		},
	},
	[MT8192_POWER_DOMAIN_MFG2] = {
		.name = "mfg2",
		.sta_mask = BIT(4),
		.ctl_offs = 0x0310,
		.sram_pdn_bits = GENMASK(8, 8),
		.sram_pdn_ack_bits = GENMASK(12, 12),
	},
	[MT8192_POWER_DOMAIN_MFG3] = {
		.name = "mfg3",
		.sta_mask = BIT(5),
		.ctl_offs = 0x0314,
		.sram_pdn_bits = GENMASK(8, 8),
		.sram_pdn_ack_bits = GENMASK(12, 12),
	},
	[MT8192_POWER_DOMAIN_MFG4] = {
		.name = "mfg4",
		.sta_mask = BIT(6),
		.ctl_offs = 0x0318,
		.sram_pdn_bits = GENMASK(8, 8),
		.sram_pdn_ack_bits = GENMASK(12, 12),
	},
	[MT8192_POWER_DOMAIN_MFG5] = {
		.name = "mfg5",
		.sta_mask = BIT(7),
		.ctl_offs = 0x031c,
		.sram_pdn_bits = GENMASK(8, 8),
		.sram_pdn_ack_bits = GENMASK(12, 12),
	},
	[MT8192_POWER_DOMAIN_MFG6] = {
		.name = "mfg6",
		.sta_mask = BIT(8),
		.ctl_offs = 0x0320,
		.sram_pdn_bits = GENMASK(8, 8),
		.sram_pdn_ack_bits = GENMASK(12, 12),
	},
	[MT8192_POWER_DOMAIN_DISP] = {
		.name = "disp",
		.sta_mask = BIT(20),
		.ctl_offs = 0x0350,
		.sram_pdn_bits = GENMASK(8, 8),
		.sram_pdn_ack_bits = GENMASK(12, 12),
		.basic_clk_name = {"disp"},
		.subsys_clk_prefix = "disp",
		.bp_table = {
			BUS_PROT_IGN(IFR_TYPE, 0x2d4, 0x2d8, 0x2d0, 0x2ec,
				MT8192_TOP_AXI_PROT_EN_MM_DISP),
			BUS_PROT_IGN(IFR_TYPE, 0xdcc, 0xdd0, 0xdc8, 0xdd8,
				MT8192_TOP_AXI_PROT_EN_MM_2_DISP),
			BUS_PROT(IFR_TYPE, 0x2a0, 0x2a4, 0x220, 0x228,
				MT8192_TOP_AXI_PROT_EN_DISP),
			BUS_PROT(IFR_TYPE, 0x2d4, 0x2d8, 0x2d0, 0x2ec,
				MT8192_TOP_AXI_PROT_EN_MM_DISP_2ND),
			BUS_PROT(IFR_TYPE, 0xdcc, 0xdd0, 0xdc8, 0xdd8,
				MT8192_TOP_AXI_PROT_EN_MM_2_DISP_2ND),
		},
	},
	[MT8192_POWER_DOMAIN_ISP] = {
		.name = "isp",
		.sta_mask = BIT(12),
		.ctl_offs = 0x0330,
		.sram_pdn_bits = GENMASK(8, 8),
		.sram_pdn_ack_bits = GENMASK(12, 12),
		.basic_clk_name = {"isp"},
		.subsys_clk_prefix = "isp",
		.bp_table = {
			BUS_PROT(IFR_TYPE, 0xdcc, 0xdd0, 0xdc8, 0xdd8,
				MT8192_TOP_AXI_PROT_EN_MM_2_ISP),
			BUS_PROT(IFR_TYPE, 0xdcc, 0xdd0, 0xdc8, 0xdd8,
				MT8192_TOP_AXI_PROT_EN_MM_2_ISP_2ND),
		},
	},
	[MT8192_POWER_DOMAIN_ISP2] = {
		.name = "isp2",
		.sta_mask = BIT(13),
		.ctl_offs = 0x0334,
		.sram_pdn_bits = GENMASK(8, 8),
		.sram_pdn_ack_bits = GENMASK(12, 12),
		.basic_clk_name = {"isp2"},
		.subsys_clk_prefix = "isp2",
		.bp_table = {
			BUS_PROT(IFR_TYPE, 0x2d4, 0x2d8, 0x2d0, 0x2ec,
				MT8192_TOP_AXI_PROT_EN_MM_ISP2),
			BUS_PROT(IFR_TYPE, 0x2d4, 0x2d8, 0x2d0, 0x2ec,
				MT8192_TOP_AXI_PROT_EN_MM_ISP2_2ND),
		},
	},
	[MT8192_POWER_DOMAIN_IPE] = {
		.name = "ipe",
		.sta_mask = BIT(14),
		.ctl_offs = 0x0338,
		.sram_pdn_bits = GENMASK(8, 8),
		.sram_pdn_ack_bits = GENMASK(12, 12),
		.basic_clk_name = {"ipe"},
		.subsys_clk_prefix = "ipe",
		.bp_table = {
			BUS_PROT(IFR_TYPE, 0x2d4, 0x2d8, 0x2d0, 0x2ec,
				MT8192_TOP_AXI_PROT_EN_MM_IPE),
			BUS_PROT(IFR_TYPE, 0x2d4, 0x2d8, 0x2d0, 0x2ec,
				MT8192_TOP_AXI_PROT_EN_MM_IPE_2ND),
		},
	},
	[MT8192_POWER_DOMAIN_VDEC] = {
		.name = "vdec",
		.sta_mask = BIT(15),
		.ctl_offs = 0x033c,
		.sram_pdn_bits = GENMASK(8, 8),
		.sram_pdn_ack_bits = GENMASK(12, 12),
		.basic_clk_name = {"vdec"},
		.subsys_clk_prefix = "vdec",
		.bp_table = {
			BUS_PROT(IFR_TYPE, 0x2d4, 0x2d8, 0x2d0, 0x2ec,
				MT8192_TOP_AXI_PROT_EN_MM_VDEC),
			BUS_PROT(IFR_TYPE, 0x2d4, 0x2d8, 0x2d0, 0x2ec,
				MT8192_TOP_AXI_PROT_EN_MM_VDEC_2ND),
		},
	},
	[MT8192_POWER_DOMAIN_VDEC2] = {
		.name = "vdec2",
		.sta_mask = BIT(16),
		.ctl_offs = 0x0340,
		.sram_pdn_bits = GENMASK(8, 8),
		.sram_pdn_ack_bits = GENMASK(12, 12),
		.subsys_clk_prefix = "vdec2",
	},
	[MT8192_POWER_DOMAIN_VENC] = {
		.name = "venc",
		.sta_mask = BIT(17),
		.ctl_offs = 0x0344,
		.sram_pdn_bits = GENMASK(8, 8),
		.sram_pdn_ack_bits = GENMASK(12, 12),
		.basic_clk_name = {"venc"},
		.subsys_clk_prefix = "venc",
		.bp_table = {
			BUS_PROT(IFR_TYPE, 0x2d4, 0x2d8, 0x2d0, 0x2ec,
				MT8192_TOP_AXI_PROT_EN_MM_VENC),
			BUS_PROT(IFR_TYPE, 0x2d4, 0x2d8, 0x2d0, 0x2ec,
				MT8192_TOP_AXI_PROT_EN_MM_VENC_2ND),
		},
	},
	[MT8192_POWER_DOMAIN_MDP] = {
		.name = "mdp",
		.sta_mask = BIT(19),
		.ctl_offs = 0x034c,
		.sram_pdn_bits = GENMASK(8, 8),
		.sram_pdn_ack_bits = GENMASK(12, 12),
		.basic_clk_name = {"mdp"},
		.subsys_clk_prefix = "mdp",
		.bp_table = {
			BUS_PROT(IFR_TYPE, 0xdcc, 0xdd0, 0xdc8, 0xdd8,
				MT8192_TOP_AXI_PROT_EN_MM_2_MDP),
			BUS_PROT(IFR_TYPE, 0xdcc, 0xdd0, 0xdc8, 0xdd8,
				MT8192_TOP_AXI_PROT_EN_MM_2_MDP_2ND),
		},
	},
	[MT8192_POWER_DOMAIN_AUDIO] = {
		.name = "audio",
		.sta_mask = BIT(21),
		.ctl_offs = 0x0354,
		.sram_pdn_bits = GENMASK(8, 8),
		.sram_pdn_ack_bits = GENMASK(12, 12),
		.basic_clk_name = {"audio", "audio1", "audio2"},
		.bp_table = {
			BUS_PROT(IFR_TYPE, 0x714, 0x718, 0x710, 0x724,
				MT8192_TOP_AXI_PROT_EN_2_AUDIO),
		},
	},
	[MT8192_POWER_DOMAIN_CAM] = {
		.name = "cam",
		.sta_mask = BIT(23),
		.ctl_offs = 0x035c,
		.sram_pdn_bits = GENMASK(8, 8),
		.sram_pdn_ack_bits = GENMASK(12, 12),
		.basic_clk_name = {"cam"},
		.subsys_clk_prefix = "cam",
		.bp_table = {
			BUS_PROT(IFR_TYPE, 0x714, 0x718, 0x710, 0x724,
				MT8192_TOP_AXI_PROT_EN_2_CAM),
			BUS_PROT(IFR_TYPE, 0x2d4, 0x2d8, 0x2d0, 0x2ec,
				MT8192_TOP_AXI_PROT_EN_MM_CAM),
			BUS_PROT(IFR_TYPE, 0x2a8, 0x2ac, 0x250, 0x258,
				MT8192_TOP_AXI_PROT_EN_1_CAM),
			BUS_PROT(IFR_TYPE, 0x2d4, 0x2d8, 0x2d0, 0x2ec,
				MT8192_TOP_AXI_PROT_EN_MM_CAM_2ND),
			BUS_PROT(IFR_TYPE, 0xb84, 0xb88, 0xb80, 0xb90,
				MT8192_TOP_AXI_PROT_EN_VDNR_CAM),
		},
	},
	[MT8192_POWER_DOMAIN_CAM_RAWA] = {
		.name = "cam_rawa",
		.sta_mask = BIT(24),
		.ctl_offs = 0x0360,
		.sram_pdn_bits = GENMASK(8, 8),
		.sram_pdn_ack_bits = GENMASK(12, 12),
		.subsys_clk_prefix = "cam_rawa",
	},
	[MT8192_POWER_DOMAIN_CAM_RAWB] = {
		.name = "cam_rawb",
		.sta_mask = BIT(25),
		.ctl_offs = 0x0364,
		.sram_pdn_bits = GENMASK(8, 8),
		.sram_pdn_ack_bits = GENMASK(12, 12),
		.subsys_clk_prefix = "cam_rawb",
	},
	[MT8192_POWER_DOMAIN_CAM_RAWC] = {
		.name = "cam_rawc",
		.sta_mask = BIT(26),
		.ctl_offs = 0x0368,
		.sram_pdn_bits = GENMASK(8, 8),
		.sram_pdn_ack_bits = GENMASK(12, 12),
		.subsys_clk_prefix = "cam_rawc",
	},
};

static const struct scp_subdomain scp_subdomain_mt8192[] = {
	{MT8192_POWER_DOMAIN_MFG0, MT8192_POWER_DOMAIN_MFG1},
	{MT8192_POWER_DOMAIN_MFG1, MT8192_POWER_DOMAIN_MFG2},
	{MT8192_POWER_DOMAIN_MFG1, MT8192_POWER_DOMAIN_MFG3},
	{MT8192_POWER_DOMAIN_MFG1, MT8192_POWER_DOMAIN_MFG4},
	{MT8192_POWER_DOMAIN_MFG1, MT8192_POWER_DOMAIN_MFG5},
	{MT8192_POWER_DOMAIN_MFG1, MT8192_POWER_DOMAIN_MFG6},
	{MT8192_POWER_DOMAIN_DISP, MT8192_POWER_DOMAIN_ISP},
	{MT8192_POWER_DOMAIN_DISP, MT8192_POWER_DOMAIN_ISP2},
	{MT8192_POWER_DOMAIN_DISP, MT8192_POWER_DOMAIN_IPE},
	{MT8192_POWER_DOMAIN_DISP, MT8192_POWER_DOMAIN_VDEC},
	{MT8192_POWER_DOMAIN_VDEC, MT8192_POWER_DOMAIN_VDEC2},
	{MT8192_POWER_DOMAIN_DISP, MT8192_POWER_DOMAIN_VENC},
	{MT8192_POWER_DOMAIN_DISP, MT8192_POWER_DOMAIN_MDP},
	{MT8192_POWER_DOMAIN_DISP, MT8192_POWER_DOMAIN_CAM},
	{MT8192_POWER_DOMAIN_CAM, MT8192_POWER_DOMAIN_CAM_RAWA},
	{MT8192_POWER_DOMAIN_CAM, MT8192_POWER_DOMAIN_CAM_RAWB},
	{MT8192_POWER_DOMAIN_CAM, MT8192_POWER_DOMAIN_CAM_RAWC},
};

static const struct scp_soc_data mt2701_data = {
	.domains = scp_domain_data_mt2701,
	.num_domains = ARRAY_SIZE(scp_domain_data_mt2701),
	.regs = {
		.pwr_sta_offs = SPM_PWR_STATUS,
		.pwr_sta2nd_offs = SPM_PWR_STATUS_2ND
	},
};

static const struct scp_soc_data mt2712_data = {
	.domains = scp_domain_data_mt2712,
	.num_domains = ARRAY_SIZE(scp_domain_data_mt2712),
	.subdomains = scp_subdomain_mt2712,
	.num_subdomains = ARRAY_SIZE(scp_subdomain_mt2712),
	.regs = {
		.pwr_sta_offs = SPM_PWR_STATUS,
		.pwr_sta2nd_offs = SPM_PWR_STATUS_2ND
	},
};

static const struct scp_soc_data mt6797_data = {
	.domains = scp_domain_data_mt6797,
	.num_domains = ARRAY_SIZE(scp_domain_data_mt6797),
	.subdomains = scp_subdomain_mt6797,
	.num_subdomains = ARRAY_SIZE(scp_subdomain_mt6797),
	.regs = {
		.pwr_sta_offs = SPM_PWR_STATUS_MT6797,
		.pwr_sta2nd_offs = SPM_PWR_STATUS_2ND_MT6797
	},
};

static const struct scp_soc_data mt7622_data = {
	.domains = scp_domain_data_mt7622,
	.num_domains = ARRAY_SIZE(scp_domain_data_mt7622),
	.regs = {
		.pwr_sta_offs = SPM_PWR_STATUS,
		.pwr_sta2nd_offs = SPM_PWR_STATUS_2ND
	},
};

static const struct scp_soc_data mt7623a_data = {
	.domains = scp_domain_data_mt7623a,
	.num_domains = ARRAY_SIZE(scp_domain_data_mt7623a),
	.regs = {
		.pwr_sta_offs = SPM_PWR_STATUS,
		.pwr_sta2nd_offs = SPM_PWR_STATUS_2ND
	},
};

static const struct scp_soc_data mt8173_data = {
	.domains = scp_domain_data_mt8173,
	.num_domains = ARRAY_SIZE(scp_domain_data_mt8173),
	.subdomains = scp_subdomain_mt8173,
	.num_subdomains = ARRAY_SIZE(scp_subdomain_mt8173),
	.regs = {
		.pwr_sta_offs = SPM_PWR_STATUS,
		.pwr_sta2nd_offs = SPM_PWR_STATUS_2ND
	},
};

static const struct scp_soc_data mt8192_data = {
	.domains = scp_domain_data_mt8192,
	.num_domains = MT8192_POWER_DOMAIN_NR,
	.subdomains = scp_subdomain_mt8192,
	.num_subdomains = ARRAY_SIZE(scp_subdomain_mt8192),
	.regs = {
		.pwr_sta_offs = 0x016c,
		.pwr_sta2nd_offs = 0x0170
	}
};

/*
 * scpsys driver init
 */

static const struct of_device_id of_scpsys_match_tbl[] = {
	{
		.compatible = "mediatek,mt2701-scpsys",
		.data = &mt2701_data,
	}, {
		.compatible = "mediatek,mt2712-scpsys",
		.data = &mt2712_data,
	}, {
		.compatible = "mediatek,mt6797-scpsys",
		.data = &mt6797_data,
	}, {
		.compatible = "mediatek,mt7622-scpsys",
		.data = &mt7622_data,
	}, {
		.compatible = "mediatek,mt7623a-scpsys",
		.data = &mt7623a_data,
	}, {
		.compatible = "mediatek,mt8173-scpsys",
		.data = &mt8173_data,
	}, {
		.compatible = "mediatek,mt8192-scpsys",
		.data = &mt8192_data,
	}, {
		/* sentinel */
	}
};

static int scpsys_probe(struct platform_device *pdev)
{
	const struct scp_subdomain *sd;
	const struct scp_soc_data *soc;
	struct scp *scp;
	struct genpd_onecell_data *pd_data;
	int i, ret;

	soc = of_device_get_match_data(&pdev->dev);
	if (!soc)
		return -EINVAL;

	scp = init_scp(pdev, soc->domains, soc->num_domains, &soc->regs, bus_list, BUS_TYPE_NUM);
	if (IS_ERR(scp))
		return PTR_ERR(scp);

	ret = mtk_register_power_domains(pdev, scp, soc->num_domains);
	if (ret)
		return ret;

	pd_data = &scp->pd_data;

	for (i = 0, sd = soc->subdomains; i < soc->num_subdomains; i++, sd++) {
		ret = pm_genpd_add_subdomain(pd_data->domains[sd->origin],
					     pd_data->domains[sd->subdomain]);
		if (ret && IS_ENABLED(CONFIG_PM))
			dev_err(&pdev->dev, "Failed to add subdomain: %d\n",
				ret);
	}

	return 0;
}

static struct platform_driver scpsys_drv = {
	.probe = scpsys_probe,
	.driver = {
		.name = "mtk-scpsys",
		.suppress_bind_attrs = true,
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(of_scpsys_match_tbl),
	},
};

static int __init scpsys_init(void)
{
	return platform_driver_register(&scpsys_drv);
}

static void __exit scpsys_exit(void)
{
	platform_driver_unregister(&scpsys_drv);
}

arch_initcall(scpsys_init);
module_exit(scpsys_exit);
MODULE_LICENSE("GPL");
