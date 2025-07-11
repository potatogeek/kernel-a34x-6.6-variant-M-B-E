// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 MediaTek Inc.
 */

#include <linux/clk.h>

#include "seninf_clk.h"
#include "platform_common.h"


#ifdef DFS_CTRL_BY_OPP
int seninf_dfs_init(struct seninf_dfs_ctx *ctx, struct device *dev)
{
	int ret, i;
	struct dev_pm_opp *opp;
	unsigned long freq;

	ctx->dev = dev;

	ret = dev_pm_opp_of_add_table(dev);
	if (ret < 0) {
		dev_info(dev, "fail to init opp table: %d\n", ret);
		return ret;
	}

	ctx->reg = devm_regulator_get_optional(dev, "dvfsrc-vcore");
	if (IS_ERR(ctx->reg)) {
		dev_info(dev, "can't get dvfsrc-vcore\n");
		return PTR_ERR(ctx->reg);
	}

	ctx->cnt = dev_pm_opp_get_opp_count(dev);

	ctx->freqs = devm_kzalloc(dev,
			sizeof(unsigned long) * ctx->cnt, GFP_KERNEL);
	ctx->volts = devm_kzalloc(dev,
			sizeof(unsigned long) * ctx->cnt, GFP_KERNEL);
	if (!ctx->freqs || !ctx->volts)
		return -ENOMEM;

	i = 0;
	freq = 0;
	while (!IS_ERR(opp = dev_pm_opp_find_freq_ceil(dev, &freq))) {
		ctx->freqs[ctx->cnt-1-i] = freq;
		do_div(ctx->freqs[ctx->cnt-1-i], 1000000); /*Hz->MHz*/
		ctx->volts[ctx->cnt-1-i] = dev_pm_opp_get_voltage(opp);
		freq++;
		i++;
		dev_pm_opp_put(opp);
	}

	return 0;
}

void seninf_dfs_exit(struct seninf_dfs_ctx *ctx)
{
	dev_pm_opp_of_remove_table(ctx->dev);
}

int seninf_dfs_ctrl(struct seninf_dfs_ctx *ctx,
		enum DFS_OPTION option, void *pbuff)
{
	int i4RetValue = 0;

	/*pr_info("%s\n", __func__);*/

	switch (option) {
	case DFS_CTRL_ENABLE:
		break;
	case DFS_CTRL_DISABLE:
		break;
	case DFS_UPDATE:
	{
		unsigned long freq, volt;
		struct dev_pm_opp *opp;

		freq = *(unsigned int *)pbuff;
		freq = freq * 1000000; /*MHz->Hz*/
		opp = dev_pm_opp_find_freq_ceil(ctx->dev, &freq);
		if (IS_ERR(opp)) {
			pr_info("Failed to find OPP for frequency %lu: %ld\n",
				freq, PTR_ERR(opp));
			return -EFAULT;
		}
		volt = dev_pm_opp_get_voltage(opp);
		dev_pm_opp_put(opp);
		pr_debug("%s: freq=%ld Hz, volt=%ld\n", __func__, freq, volt);
		regulator_set_voltage(ctx->reg, volt, INT_MAX);
	}
		break;
	case DFS_RELEASE:
		break;
	case DFS_SUPPORTED_ISP_CLOCKS:
	{
		struct IMAGESENSOR_GET_SUPPORTED_ISP_CLK *pIspclks;
		int i;

		pIspclks = (struct IMAGESENSOR_GET_SUPPORTED_ISP_CLK *) pbuff;

		pIspclks->clklevelcnt = ctx->cnt;

		if (pIspclks->clklevelcnt > ISP_CLK_LEVEL_CNT) {
			pr_debug("ERR: clklevelcnt is exceeded\n");
			i4RetValue = -EFAULT;
			break;
		}

		for (i = 0; i < pIspclks->clklevelcnt; i++)
			pIspclks->clklevel[i] = ctx->freqs[i];
	}
		break;
	case DFS_CUR_ISP_CLOCK:
	{
		unsigned int *pGetIspclk;
		int i, cur_volt;

		pGetIspclk = (unsigned int *) pbuff;
		cur_volt = regulator_get_voltage(ctx->reg);

		for (i = 0; i < ctx->cnt; i++) {
			if (ctx->volts[i] == cur_volt) {
				*pGetIspclk = (u32)ctx->freqs[i];
				break;
			}
		}
	}
		break;
	default:
		pr_info("None\n");
		break;
	}
	return i4RetValue;
}
#endif

static inline void seninf_clk_check(struct SENINF_CLK *pclk)
{
	int i;

	for (i = 0; i < SENINF_CLK_IDX_MAX_NUM; i++) {
		if (pclk->clk_sel[i] != NULL)
			WARN_ON(IS_ERR(pclk->clk_sel[i]));
	}
}

/**********************************************************
 *Common Clock Framework (CCF)
 **********************************************************/
enum SENINF_RETURN seninf_clk_init(struct SENINF_CLK *pclk)
{
	int i;

	if (pclk->pplatform_device == NULL) {
		PK_DBG("[%s] pdev is null\n", __func__);
		return SENINF_RETURN_ERROR;
	}
	/* get all possible using clocks */
	for (i = 0; i < SENINF_CLK_IDX_MAX_NUM; i++) {
		pclk->clk_sel[i] = devm_clk_get(&pclk->pplatform_device->dev,
						gseninf_clk_name[i].pctrl);
		atomic_set(&pclk->enable_cnt[i], 0);

		if ((pclk->clk_sel[i] == NULL) || IS_ERR(pclk->clk_sel[i])) {
			PK_DBG("cannot get %d clock\n", i);
			pclk->clk_sel[i] = NULL;
		}
	}
#if IS_ENABLED(CONFIG_PM_SLEEP)
	pclk->seninf_wake_lock = wakeup_source_register(
			NULL, "seninf_lock_wakelock");
	if (!pclk->seninf_wake_lock) {
		PK_DBG("failed to get seninf_wake_lock\n");
		return SENINF_RETURN_ERROR;
	}
#endif
	atomic_set(&pclk->wakelock_cnt, 0);

	return SENINF_RETURN_SUCCESS;
}

void seninf_clk_exit(struct SENINF_CLK *pclk)
{
#if IS_ENABLED(CONFIG_PM_SLEEP)
	wakeup_source_unregister(pclk->seninf_wake_lock);
#endif
}

int seninf_clk_set(struct SENINF_CLK *pclk,
					struct ACDK_SENSOR_MCLK_STRUCT *pmclk)
{
	int i, ret = 0;
	unsigned int idx_tg, idx_freq = 0;

	if (pmclk->TG >= SENINF_CLK_TG_MAX_NUM ||
	    pmclk->freq > SENINF_CLK_MCLK_FREQ_MAX ||
		pmclk->freq < SENINF_CLK_MCLK_FREQ_MIN) {
		PK_DBG(
	"[CAMERA SENSOR]kdSetSensorMclk out of range, tg = %d, freq = %d\n",
		  pmclk->TG, pmclk->freq);
		return -EFAULT;
	}

	PK_DBG("[CAMERA SENSOR] CCF kdSetSensorMclk on= %d, freq= %d, TG= %d\n",
	       pmclk->on, pmclk->freq, pmclk->TG);

	seninf_clk_check(pclk);

	for (i = 0; ((i < SENINF_CLK_IDX_FREQ_IDX_NUM) &&
				(pmclk->freq != gseninf_clk_freq[i])); i++)
		;

	if (i >= SENINF_CLK_IDX_FREQ_IDX_NUM)
		return -EFAULT;

	idx_tg = pmclk->TG + SENINF_CLK_IDX_TG_MIN_NUM;
	idx_freq = i + SENINF_CLK_IDX_FREQ_MIN_NUM;

	if (pmclk->on) {
		if (IS_MT6893(pclk->g_platform_id) || IS_MT6885(pclk->g_platform_id)) {
			/* Workaround for timestamp: TG1 always ON */
			if (pclk->clk_sel[SENINF_CLK_IDX_TG_TOP_MUX_CAMTG]
				!= NULL) {
				if (clk_prepare_enable(
					pclk->clk_sel[SENINF_CLK_IDX_TG_TOP_MUX_CAMTG]))
					PK_DBG("[CAMERA SENSOR] failed tg=%d\n",
						SENINF_CLK_IDX_TG_TOP_MUX_CAMTG);
				else
					atomic_inc(
					&pclk->enable_cnt[SENINF_CLK_IDX_TG_TOP_MUX_CAMTG]);
			}
		}

		if (pclk->clk_sel[idx_tg] != NULL) {
			if (clk_prepare_enable(pclk->clk_sel[idx_tg]))
				PK_DBG("[CAMERA SENSOR] failed tg=%d\n",
					pmclk->TG);
			else
				atomic_inc(&pclk->enable_cnt[idx_tg]);
		}

		if (pclk->clk_sel[idx_freq] != NULL) {
			if (clk_prepare_enable(pclk->clk_sel[idx_freq]))
				PK_DBG("[CAMERA SENSOR] failed freq idx= %d\n",
					i);
			else
				atomic_inc(&pclk->enable_cnt[idx_freq]);
		}

		if ((pclk->clk_sel[idx_tg] != NULL) &&
			(pclk->clk_sel[idx_freq] != NULL))
			ret = clk_set_parent(
				pclk->clk_sel[idx_tg],
				pclk->clk_sel[idx_freq]);
	} else {
		if (pclk->clk_sel[idx_freq] != NULL) {
			if (atomic_read(&pclk->enable_cnt[idx_freq]) > 0) {
				clk_disable_unprepare(pclk->clk_sel[idx_freq]);
				atomic_dec(&pclk->enable_cnt[idx_freq]);
			}
		}

		if (pclk->clk_sel[idx_tg] != NULL) {
			if (atomic_read(&pclk->enable_cnt[idx_tg]) > 0) {
				clk_disable_unprepare(pclk->clk_sel[idx_tg]);
				atomic_dec(&pclk->enable_cnt[idx_tg]);
			}
		}

		if (IS_MT6893(pclk->g_platform_id) || IS_MT6885(pclk->g_platform_id)) {
			/* Workaround for timestamp: TG1 always ON */
			if (pclk->clk_sel[SENINF_CLK_IDX_TG_TOP_MUX_CAMTG] != NULL) {
				if (atomic_read(
					&pclk->enable_cnt[SENINF_CLK_IDX_TG_TOP_MUX_CAMTG])
					> 0) {
					clk_disable_unprepare(
					pclk->clk_sel[SENINF_CLK_IDX_TG_TOP_MUX_CAMTG]);
					atomic_dec(
					&pclk->enable_cnt[SENINF_CLK_IDX_TG_TOP_MUX_CAMTG]);
				}
			}
		}
	}

	return ret;
}


int seninf_sys_clk_set(struct SENINF_CLK *pclk,
					struct ACDK_SENSOR_SENINF_CLK_STRUCT *pseninfclk)
{
	int i, ret = 0;
	unsigned int freq = pseninfclk->freq;
	unsigned int seninf_port = pseninfclk->seninf_port;
	unsigned int freq_idx = 0;
	unsigned int seninf_idx = 0;

	if (IS_MT6835(pclk->g_platform_id)) {
		PK_DBG("[update seninf_clk] platform_id = %d, start update seninf_clk\n",
			pclk->g_platform_id);
	} else {
		PK_DBG("[update seninf_clk] platform_id = %d no need update seninf_clk\n",
			pclk->g_platform_id);
		return ret;
	}

	if (seninf_port >= SENINF_CLK_SENINF_PORT_MAX_NUM ||
		freq > SENINF_CLK_SYS_TOP_MUX_SENINF_FREQ_MAX ||
		freq < SENINF_CLK_SYS_TOP_MUX_SENINF_FREQ_MIN) {
		PK_DBG("[update seninf_clk]seninf_clk out of range, freq = %d\n", freq);
		return -EFAULT;
	}

	seninf_clk_check(pclk);

	for (i = 0; ((i < SENINF_CLK_IDX_FREQ_IDX_NUM) &&
				(freq != gseninf_clk_freq[i])); i++)
		;

	if (i >= SENINF_CLK_IDX_FREQ_IDX_NUM)
		return -EFAULT;

	freq_idx = SENINF_CLK_IDX_FREQ_MIN_NUM + i;
	seninf_idx = SENINF_CLK_IDX_SYS_TOP_MUX_SENINF + seninf_port;
	PK_DBG("[update seninf_clk] seninf_port = %d , freq_idx = %d\n", seninf_port, freq_idx);

	if ((pclk->clk_sel[seninf_idx] != NULL) && (pclk->clk_sel[freq_idx] != NULL)) {
		PK_DBG("[update seninf_clk] do clk_set_parent freq_idx = %d, seninf_idx = %d\n",
			freq_idx, seninf_idx);
		ret = clk_set_parent(
			pclk->clk_sel[seninf_idx],
			pclk->clk_sel[freq_idx]);
	} else {
		PK_DBG("[update seninf_clk] failed clk_set_parent freq_idx = %d, seninf_idx = %d\n",
			freq_idx, seninf_idx);
	}
	return ret;
}

void seninf_clk_open(struct SENINF_CLK *pclk)
{
	MINT32 i;

	PK_DBG("open\n");

	if (atomic_inc_return(&pclk->wakelock_cnt) == 1) {
#if IS_ENABLED(CONFIG_PM_SLEEP)
		__pm_stay_awake(pclk->seninf_wake_lock);
#endif
	}

	for (i = SENINF_CLK_IDX_SYS_MIN_NUM;
		i < SENINF_CLK_IDX_SYS_MAX_NUM;
		i++) {
		if (pclk->clk_sel[i] != NULL) {
			if (clk_prepare_enable(pclk->clk_sel[i]))
				PK_DBG("[CAMERA SENSOR] failed sys idx= %d\n",
					i);
			else
				atomic_inc(&pclk->enable_cnt[i]);
		}
	}
}

void seninf_clk_release(struct SENINF_CLK *pclk)
{
	MINT32 i = SENINF_CLK_IDX_MAX_NUM;

	PK_DBG("release\n");

	do {
		i--;
		if (pclk->clk_sel[i] != NULL) {
			for (; atomic_read(&pclk->enable_cnt[i]) > 0;) {
				clk_disable_unprepare(pclk->clk_sel[i]);
				atomic_dec(&pclk->enable_cnt[i]);
			}
		}
	} while (i);

	if (atomic_dec_and_test(&pclk->wakelock_cnt)) {
#if IS_ENABLED(CONFIG_PM_SLEEP)
		__pm_relax(pclk->seninf_wake_lock);
#endif
	}
}

unsigned int seninf_clk_get_meter(struct SENINF_CLK *pclk, unsigned int clk)
{
/*
 *	unsigned int i = 0;
 *
 *	if (clk == 0xff) {
 *		for (i = SENINF_CLK_IDX_SYS_MIN_NUM; i < SENINF_CLK_IDX_SYS_MAX_NUM; ++i) {
 *			PK_DBG("[sensor_dump][mclk]index=%u freq=%lu HW enable=%d enable_cnt=%u\n",
 *				i,
 *				clk_get_rate(pclk->clk_sel[i]),
 *				__clk_is_enabled(pclk->clk_sel[i]),
 *				__clk_get_enable_count(pclk->clk_sel[i])
 *			);
 *		}
 *	}
 */
#ifdef SENINF_CLK_CONTROL_workaround//SENINF_CLK_CONTROL
	/* workaround */
	mt_get_ckgen_freq(1);

	if (clk == 4) {
		PK_DBG("CAMSYS_SENINF_CGPDN = %lu\n",
		clk_get_rate(
		pclk->clk_sel[SENINF_CLK_IDX_SYS_CAMSYS_SENINF_CGPDN]));
		PK_DBG("TOP_MUX_SENINF = %lu\n",
			clk_get_rate(
			pclk->clk_sel[SENINF_CLK_IDX_SYS_TOP_MUX_SENINF]));
	}
	if (clk < 64)
		return mt_get_ckgen_freq(clk);
	else
		return 0;
#else
	if (clk < 64)
		return mt_get_ckgen_freq(clk);
	else
		return 0;
#endif
}

