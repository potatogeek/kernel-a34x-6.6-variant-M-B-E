// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 MediaTek Inc.
 *
 * Author: Daniel Huang <daniel.huang@mediatek.com>
 *
 */

 // Standard C header file

// kernel header file
#include <linux/device.h>
#include <linux/of_address.h>
#include <linux/pm_runtime.h>
#include <linux/remoteproc.h>

// GCE header
#include <linux/soc/mediatek/mtk-cmdq-ext.h>

// mtk imgsys local header file

// Local header file
#include "mtk_imgsys-pqdip.h"
#include "mtk-hcp.h"
#include "mtk_imgsys-v4l2-debug.h"
#include "../../cmdq/isp8/mtk_imgsys-cmdq-qof.h"

/********************************************************************
 * Global Define
 ********************************************************************/
struct mtk_imgsys_pqdip_dtable {
	uint32_t empty;
	uint32_t addr;
	uint32_t addr_msb;
};

#define PQDIP_HW_SET		2

#define PQDIP_BASE_ADDR		0x34210000
#define PQDIP_BASE_ADDR_P	0x15210000
#define PQDIP_OFST			0x300000
#define PQDIP_ALL_REG_CNT	0x6000
//#define DUMP_PQ_ALL

#define PQDIP_CTL_OFST		0x0
#define PQDIP_CQ_OFST		0x200
#define PQDIP_DMA_OFST		0x1200
#define PQDIP_WROT1_OFST	0x2000
#define PQDIP_WROT2_OFST	0x2F00
#define PQDIP_RZH4N6T_OFST	0x3000
#define PQDIP_TDSHP1OFST	0x40F0
#define PQDIP_TDSHP2OFST	0x4330
#define PQDIP_TDSHP3OFST	0x4500
#define PQDIP_UNP1_OFST		0x5000
#define PQDIP_UNP2_OFST		0x5040
#define PQDIP_UNP3_OFST		0x5080
#define PQDIP_C02_OFST		0x5100
#define PQDIP_C24_OFST		0x5140
#define PQDIP_MCRP_OFST		0x5180
#define PQDIP_TCC_OFST		0x51C0
#define PQDIP_TCY_OFST		0x57C0

#define PQDIP_CTL_REG_CNT		0x100
#define PQDIP_CQ_REG_CNT		0x70
#define PQDIP_DMA_REG_CNT		0x200
#define PQDIP_WROT1_REG_CNT		0x140
#define PQDIP_WROT2_REG_CNT		0x40
#define PQDIP_RZH4N6T_REG_CNT		0x270
#define PQDIP_TDSHP1REG_CNT		0x60
#define PQDIP_TDSHP2REG_CNT		0x60
#define PQDIP_TDSHP3REG_CNT		0x110
#define PQDIP_UNP_REG_CNT		0x10
#define PQDIP_C02_REG_CNT		0x20
#define PQDIP_C24_REG_CNT		0x10
#define PQDIP_MCRP_REG_CNT		0x10
#define PQDIP_TCC_REG_CNT		0x450
#define PQDIP_TCY_REG_CNT		0xA0

#define PQDIPCTL_DBG_SEL_OFST	0xF4
#define PQDIPCTL_DBG_OUT_OFST	0xF8
#define PQDIPCTL_DMA_DBG_SEL_OFST	0x1088
#define PQDIPCTL_DMA_DBG_OUT_OFST	0x108C
#define PQ_WROT_DBG_SEL_OFST	0x2018
#define PQ_WROT_DBG_OUT_OFST	0x20D0
#define PQ_RZH4N6T_DBG_SEL_OFST	0x3044
#define PQ_RZH4N6T_DBG_OUT_OFST	0x3048

/********************************************************************
 * Global Variable
 ********************************************************************/
const struct mtk_imgsys_init_array
			mtk_imgsys_pqdip_init_ary[] = {
	{0x0068, 0x80000000},	/* PQDIPCTL_P1A_REG_PQDIPCTL_INT1_EN */
	{0x0078, 0x0},		/* PQDIPCTL_P1A_REG_PQDIPCTL_INT2_EN */
	{0x0088, 0x0},		/* PQDIPCTL_P1A_REG_PQDIPCTL_CQ_INT1_EN */
	{0x0098, 0x0},		/* PQDIPCTL_P1A_REG_PQDIPCTL_CQ_INT2_EN */
	{0x00B0, 0x0},		/* PQDIPCTL_P1A_REG_PQDIPCTL_CQ_INT3_EN */
	{0x0180, 0x1},		/* PQDIPCTL_QOF_STATUS */
	{0x0208, 0x11},		/* DIPCQ_P1A_REG_DIPCQ_CQ_THR0_CTL */
	{0x0218, 0x11},		/* DIPCQ_P1A_REG_DIPCQ_CQ_THR1_CTL */
	{0x0228, 0x11},		/* DIPCQ_P1A_REG_DIPCQ_CQ_THR2_CTL */
	{0x0238, 0x11},		/* DIPCQ_P1A_REG_DIPCQ_CQ_THR3_CTL */
	{0x0248, 0x11},		/* DIPCQ_P1A_REG_DIPCQ_CQ_THR4_CTL */
	{0x0258, 0x11},		/* DIPCQ_P1A_REG_DIPCQ_CQ_THR5_CTL */
};


#define PQDIP_INIT_ARRAY_COUNT	ARRAY_SIZE(mtk_imgsys_pqdip_init_ary)

void __iomem *gpqdipRegBA[PQDIP_HW_SET] = {0L};
unsigned int gPQDIPRegBase[PQDIP_HW_SET] = {0x34210000 , 0x34510000};
static unsigned int g_RegBaseAddrPQ = PQDIP_BASE_ADDR;
//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// Public Functions
//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
void imgsys_pqdip_set_initial_value(struct mtk_imgsys_dev *imgsys_dev)
{
	unsigned int hw_idx = 0;

    if (imgsys_pqdip_7sp_dbg_enable())
	dev_dbg(imgsys_dev->dev, "%s: +\n", __func__);

	for (hw_idx = 0 ; hw_idx < PQDIP_HW_SET ; hw_idx++) {
		/* iomap registers */
		gpqdipRegBA[hw_idx] = of_iomap(imgsys_dev->dev->of_node,
			REG_MAP_E_PQDIP_A + hw_idx);
	}

    if (imgsys_pqdip_7sp_dbg_enable())
	dev_dbg(imgsys_dev->dev, "%s: -\n", __func__);

	if (imgsys_dev->dev_ver == 1) {
		g_RegBaseAddrPQ = PQDIP_BASE_ADDR_P;
		gPQDIPRegBase[0] = 0x15210000;
		gPQDIPRegBase[1] = 0x15510000;
	}
}

void imgsys_pqdip_set_hw_initial_value(struct mtk_imgsys_dev *imgsys_dev)
{
	void __iomem *ofset = NULL;
	unsigned int hw_idx = 0;
	unsigned int i = 0;

	if (imgsys_pqdip_7sp_dbg_enable())
		dev_dbg(imgsys_dev->dev, "%s: +\n", __func__);

	for (hw_idx = 0 ; hw_idx < PQDIP_HW_SET ; hw_idx++) {
		for (i = 0 ; i < PQDIP_INIT_ARRAY_COUNT ; i++) {
			ofset = gpqdipRegBA[hw_idx]
				+ mtk_imgsys_pqdip_init_ary[i].ofset;
			writel(mtk_imgsys_pqdip_init_ary[i].val, ofset);
		}
	}

	if (imgsys_pqdip_7sp_dbg_enable())
		dev_dbg(imgsys_dev->dev, "%s: -\n", __func__);
}

void imgsys_pqdip_cmdq_set_hw_initial_value(struct mtk_imgsys_dev *imgsys_dev,
		void *pkt, int hw_idx)
{
	unsigned int ofset;
	unsigned int idx = 0;
	unsigned int i = 0;
	struct cmdq_pkt *package = NULL;

	if (imgsys_dev == NULL || pkt == NULL) {
		dump_stack();
		pr_err("[%s][%d] param fatal error!", __func__, __LINE__);
		return;
	}
	package = (struct cmdq_pkt *)pkt;

	dev_dbg(imgsys_dev->dev, "%s: +\n", __func__);

	idx = hw_idx - REG_MAP_E_PQDIP_A;
	for (i = 0 ; i < PQDIP_INIT_ARRAY_COUNT ; i++) {
		ofset = gPQDIPRegBase[idx]
			+ mtk_imgsys_pqdip_init_ary[i].ofset;
		cmdq_pkt_write(package, NULL, ofset /*address*/,
				mtk_imgsys_pqdip_init_ary[i].val, 0xffffffff);
	}

	dev_dbg(imgsys_dev->dev, "%s: -\n", __func__);
}

void imgsys_pqdip_debug_dump(struct mtk_imgsys_dev *imgsys_dev,
							unsigned int engine)
{
	void __iomem *pqdipRegBA = 0L;
	unsigned int hw_idx = 0;
	unsigned int i = 0;

	dev_info(imgsys_dev->dev, "%s: +\n", __func__);

	for (hw_idx = 0 ; hw_idx < PQDIP_HW_SET ; hw_idx++) {
		/* iomap registers */
		pqdipRegBA = gpqdipRegBA[hw_idx];
#ifdef DUMP_PQ_ALL
		for (i = 0x0; i < PQDIP_ALL_REG_CNT; i += 0x10) {
			dev_info(imgsys_dev->dev, "%s:  [0x%08x] 0x%08x 0x%08x 0x%08x 0x%08x",
			__func__, (unsigned int)(g_RegBaseAddrPQ + (PQDIP_OFST * hw_idx) + i),
			(unsigned int)ioread32((void *)(pqdipRegBA + i + 0x00)),
			(unsigned int)ioread32((void *)(pqdipRegBA + i + 0x04)),
			(unsigned int)ioread32((void *)(pqdipRegBA + i + 0x08)),
			(unsigned int)ioread32((void *)(pqdipRegBA + i + 0x0c)));
		}
#else
		dev_info(imgsys_dev->dev, "%s:  ctl_reg", __func__);
		for (i = 0x0; i < PQDIP_CTL_REG_CNT; i += 0x10) {
			dev_info(imgsys_dev->dev, "%s:  [0x%08x] 0x%08x 0x%08x 0x%08x 0x%08x",
			__func__, (unsigned int)(g_RegBaseAddrPQ + (PQDIP_OFST * hw_idx)
				+ PQDIP_CTL_OFST + i),
			(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_CTL_OFST + i + 0x00)),
			(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_CTL_OFST + i + 0x04)),
			(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_CTL_OFST + i + 0x08)),
			(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_CTL_OFST + i + 0x0c)));
		}

		dev_info(imgsys_dev->dev, "%s:  cq_reg", __func__);
		for (i = 0; i < PQDIP_CQ_REG_CNT; i += 0x10) {
			dev_info(imgsys_dev->dev, "%s:  [0x%08x] 0x%08x 0x%08x 0x%08x 0x%08x",
			__func__, (unsigned int)(g_RegBaseAddrPQ + (PQDIP_OFST * hw_idx)
				+ PQDIP_CQ_OFST + i),
			(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_CQ_OFST + i + 0x00)),
			(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_CQ_OFST + i + 0x04)),
			(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_CQ_OFST + i + 0x08)),
			(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_CQ_OFST + i + 0x0c)));
		}

		dev_info(imgsys_dev->dev, "%s:  dma_reg", __func__);
		for (i = 0; i < PQDIP_DMA_REG_CNT; i += 0x10) {
			dev_info(imgsys_dev->dev, "%s:  [0x%08x] 0x%08x 0x%08x 0x%08x 0x%08x",
			__func__, (unsigned int)(g_RegBaseAddrPQ + (PQDIP_OFST * hw_idx)
				+ PQDIP_DMA_OFST + i),
			(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_DMA_OFST + i + 0x00)),
			(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_DMA_OFST + i + 0x04)),
			(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_DMA_OFST + i + 0x08)),
			(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_DMA_OFST + i + 0x0c)));
		}

		dev_info(imgsys_dev->dev, "%s:  wrot_reg", __func__);
		for (i = 0; i < PQDIP_WROT1_REG_CNT; i += 0x10) {
			dev_info(imgsys_dev->dev, "%s:  [0x%08x] 0x%08x 0x%08x 0x%08x 0x%08x",
			__func__, (unsigned int)(g_RegBaseAddrPQ + (PQDIP_OFST * hw_idx)
				+ PQDIP_WROT1_OFST + i),
			(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_WROT1_OFST + i + 0x00)),
			(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_WROT1_OFST + i + 0x04)),
			(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_WROT1_OFST + i + 0x08)),
			(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_WROT1_OFST + i + 0x0c)));
		}
		for (i = 0; i < PQDIP_WROT2_REG_CNT; i += 0x10) {
			dev_info(imgsys_dev->dev, "%s:  [0x%08x] 0x%08x 0x%08x 0x%08x 0x%08x",
			__func__, (unsigned int)(g_RegBaseAddrPQ + (PQDIP_OFST * hw_idx)
				+ PQDIP_WROT2_OFST + i),
			(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_WROT2_OFST + i + 0x00)),
			(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_WROT2_OFST + i + 0x04)),
			(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_WROT2_OFST + i + 0x08)),
			(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_WROT2_OFST + i + 0x0c)));
		}

		dev_info(imgsys_dev->dev, "%s:  urz6t_reg", __func__);
		for (i = 0; i < PQDIP_RZH4N6T_REG_CNT; i += 0x10) {
			dev_info(imgsys_dev->dev, "%s:  [0x%08x] 0x%08x 0x%08x 0x%08x 0x%08x",
			__func__, (unsigned int)(g_RegBaseAddrPQ + (PQDIP_OFST * hw_idx)
				+ PQDIP_RZH4N6T_OFST + i),
			(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_RZH4N6T_OFST + i + 0x00)),
			(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_RZH4N6T_OFST + i + 0x04)),
			(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_RZH4N6T_OFST + i + 0x08)),
			(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_RZH4N6T_OFST + i + 0x0c)));
		}

		dev_info(imgsys_dev->dev, "%s:  tdshp_reg", __func__);
		for (i = 0; i < PQDIP_TDSHP1REG_CNT; i += 0x10) {
			dev_info(imgsys_dev->dev, "%s:  [0x%08x] 0x%08x 0x%08x 0x%08x 0x%08x",
			__func__, (unsigned int)(g_RegBaseAddrPQ + (PQDIP_OFST * hw_idx)
				+ PQDIP_TDSHP1OFST + i),
			(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_TDSHP1OFST + i + 0x00)),
			(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_TDSHP1OFST + i + 0x04)),
			(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_TDSHP1OFST + i + 0x08)),
			(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_TDSHP1OFST + i + 0x0c)));
		}
		for (i = 0; i < PQDIP_TDSHP2REG_CNT; i += 0x10) {
			dev_info(imgsys_dev->dev, "%s:  [0x%08x] 0x%08x 0x%08x 0x%08x 0x%08x",
			__func__, (unsigned int)(g_RegBaseAddrPQ + (PQDIP_OFST * hw_idx)
				+ PQDIP_TDSHP2OFST + i),
			(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_TDSHP2OFST + i + 0x00)),
			(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_TDSHP2OFST + i + 0x04)),
			(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_TDSHP2OFST + i + 0x08)),
			(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_TDSHP2OFST + i + 0x0c)));
		}
		for (i = 0; i < PQDIP_TDSHP3REG_CNT; i += 0x10) {
			dev_info(imgsys_dev->dev, "%s:  [0x%08x] 0x%08x 0x%08x 0x%08x 0x%08x",
			__func__, (unsigned int)(g_RegBaseAddrPQ + (PQDIP_OFST * hw_idx)
				+ PQDIP_TDSHP3OFST + i),
			(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_TDSHP3OFST + i + 0x00)),
			(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_TDSHP3OFST + i + 0x04)),
			(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_TDSHP3OFST + i + 0x08)),
			(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_TDSHP3OFST + i + 0x0c)));
		}

		dev_info(imgsys_dev->dev, "%s:  unp_reg", __func__);
		for (i = 0; i < PQDIP_UNP_REG_CNT; i += 0x10) {
			dev_info(imgsys_dev->dev, "%s:  [0x%08x] 0x%08x 0x%08x 0x%08x 0x%08x",
			__func__, (unsigned int)(g_RegBaseAddrPQ + (PQDIP_OFST * hw_idx)
				+ PQDIP_UNP1_OFST + i),
			(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_UNP1_OFST + i + 0x00)),
			(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_UNP1_OFST + i + 0x04)),
			(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_UNP1_OFST + i + 0x08)),
			(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_UNP1_OFST + i + 0x0c)));
		}
		for (i = 0; i < PQDIP_UNP_REG_CNT; i += 0x10) {
			dev_info(imgsys_dev->dev, "%s:  [0x%08x] 0x%08x 0x%08x 0x%08x 0x%08x",
			__func__, (unsigned int)(g_RegBaseAddrPQ + (PQDIP_OFST * hw_idx)
				+ PQDIP_UNP2_OFST + i),
			(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_UNP2_OFST + i + 0x00)),
			(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_UNP2_OFST + i + 0x04)),
			(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_UNP2_OFST + i + 0x08)),
			(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_UNP2_OFST + i + 0x0c)));
		}
		for (i = 0; i < PQDIP_UNP_REG_CNT; i += 0x10) {
			dev_info(imgsys_dev->dev, "%s:  [0x%08x] 0x%08x 0x%08x 0x%08x 0x%08x",
			__func__, (unsigned int)(g_RegBaseAddrPQ + (PQDIP_OFST * hw_idx)
				+ PQDIP_UNP3_OFST + i),
			(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_UNP3_OFST + i + 0x00)),
			(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_UNP3_OFST + i + 0x04)),
			(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_UNP3_OFST + i + 0x08)),
			(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_UNP3_OFST + i + 0x0c)));
		}

		dev_info(imgsys_dev->dev, "%s:  c02_reg", __func__);
		for (i = 0; i < PQDIP_C02_REG_CNT; i += 0x10) {
			dev_info(imgsys_dev->dev, "%s:  [0x%08x] 0x%08x 0x%08x 0x%08x 0x%08x",
			__func__, (unsigned int)(g_RegBaseAddrPQ + (PQDIP_OFST * hw_idx)
				+ PQDIP_C02_OFST + i),
			(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_C02_OFST + i + 0x00)),
			(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_C02_OFST + i + 0x04)),
			(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_C02_OFST + i + 0x08)),
			(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_C02_OFST + i + 0x0c)));
		}

		dev_info(imgsys_dev->dev, "%s:  c24_reg", __func__);
		for (i = 0; i < PQDIP_C24_REG_CNT; i += 0x10) {
			dev_info(imgsys_dev->dev, "%s:  [0x%08x] 0x%08x 0x%08x 0x%08x 0x%08x",
			__func__, (unsigned int)(g_RegBaseAddrPQ + (PQDIP_OFST * hw_idx)
				+ PQDIP_C24_OFST + i),
			(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_C24_OFST + i + 0x00)),
			(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_C24_OFST + i + 0x04)),
			(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_C24_OFST + i + 0x08)),
			(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_C24_OFST + i + 0x0c)));
		}

		dev_info(imgsys_dev->dev, "%s:  mcrp_reg", __func__);
		for (i = 0; i < PQDIP_MCRP_REG_CNT; i += 0x10) {
			dev_info(imgsys_dev->dev, "%s:  [0x%08x] 0x%08x 0x%08x 0x%08x 0x%08x",
			__func__, (unsigned int)(g_RegBaseAddrPQ + (PQDIP_OFST * hw_idx)
				+ PQDIP_MCRP_OFST + i),
			(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_MCRP_OFST + i + 0x00)),
			(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_MCRP_OFST + i + 0x04)),
			(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_MCRP_OFST + i + 0x08)),
			(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_MCRP_OFST + i + 0x0c)));
		}

		dev_info(imgsys_dev->dev, "%s:  tcc_reg", __func__);
		for (i = 0; i < PQDIP_TCC_REG_CNT; i += 0x10) {
			dev_info(imgsys_dev->dev, "%s:  [0x%08x] 0x%08x 0x%08x 0x%08x 0x%08x",
			__func__, (unsigned int)(g_RegBaseAddrPQ + (PQDIP_OFST * hw_idx)
				+ PQDIP_TCC_OFST + i),
			(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_TCC_OFST + i + 0x00)),
			(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_TCC_OFST + i + 0x04)),
			(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_TCC_OFST + i + 0x08)),
			(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_TCC_OFST + i + 0x0c)));
		}

		dev_info(imgsys_dev->dev, "%s:  tcy_reg", __func__);
		for (i = 0; i < PQDIP_TCY_REG_CNT; i += 0x10) {
			dev_info(imgsys_dev->dev, "%s:  [0x%08x] 0x%08x 0x%08x 0x%08x 0x%08x",
			__func__, (unsigned int)(g_RegBaseAddrPQ + (PQDIP_OFST * hw_idx)
				+ PQDIP_TCY_OFST + i),
			(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_TCY_OFST + i + 0x00)),
			(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_TCY_OFST + i + 0x04)),
			(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_TCY_OFST + i + 0x08)),
			(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_TCY_OFST + i + 0x0c)));
		}
#endif

		//DMA_DBG
		dev_info(imgsys_dev->dev, "%s: dma debug\n", __func__);
		for (i = 0; i <= 4; i += 1) {
			iowrite32((0x00000 + i), (void *)(pqdipRegBA + PQDIPCTL_DMA_DBG_SEL_OFST));
			dev_info(imgsys_dev->dev, "%s: sel(0x%08x): %08X", __func__,
			(unsigned int)(0x00000 + i),
			(unsigned int)ioread32((void *)(pqdipRegBA + PQDIPCTL_DMA_DBG_OUT_OFST)));
			iowrite32((0x00100 + i), (void *)(pqdipRegBA + PQDIPCTL_DMA_DBG_SEL_OFST));
			dev_info(imgsys_dev->dev, "%s: sel(0x%08x): %08X", __func__,
			(unsigned int)(0x00100 + i),
			(unsigned int)ioread32((void *)(pqdipRegBA + PQDIPCTL_DMA_DBG_OUT_OFST)));
			iowrite32((0x00200 + i), (void *)(pqdipRegBA + PQDIPCTL_DMA_DBG_SEL_OFST));
			dev_info(imgsys_dev->dev, "%s: sel(0x%08x): %08X", __func__,
			(unsigned int)(0x00200 + i),
			(unsigned int)ioread32((void *)(pqdipRegBA + PQDIPCTL_DMA_DBG_OUT_OFST)));
			iowrite32((0x00300 + i), (void *)(pqdipRegBA + PQDIPCTL_DMA_DBG_SEL_OFST));
			dev_info(imgsys_dev->dev, "%s: sel(0x%08x): %08X", __func__,
			(unsigned int)(0x00300 + i),
			(unsigned int)ioread32((void *)(pqdipRegBA + PQDIPCTL_DMA_DBG_OUT_OFST)));
			iowrite32((0x00500 + i), (void *)(pqdipRegBA + PQDIPCTL_DMA_DBG_SEL_OFST));
			dev_info(imgsys_dev->dev, "%s: sel(0x%08x): %08X", __func__,
			(unsigned int)(0x00500 + i),
			(unsigned int)ioread32((void *)(pqdipRegBA + PQDIPCTL_DMA_DBG_OUT_OFST)));
			iowrite32((0x10600 + i), (void *)(pqdipRegBA + PQDIPCTL_DMA_DBG_SEL_OFST));
			dev_info(imgsys_dev->dev, "%s: sel(0x%08x): %08X", __func__,
			(unsigned int)(0x10600 + i),
			(unsigned int)ioread32((void *)(pqdipRegBA + PQDIPCTL_DMA_DBG_OUT_OFST)));
			iowrite32((0x30600 + i), (void *)(pqdipRegBA + PQDIPCTL_DMA_DBG_SEL_OFST));
			dev_info(imgsys_dev->dev, "%s: sel(0x%08x): %08X", __func__,
			(unsigned int)(0x30600 + i),
			(unsigned int)ioread32((void *)(pqdipRegBA + PQDIPCTL_DMA_DBG_OUT_OFST)));
			iowrite32((0x00A00 + i), (void *)(pqdipRegBA + PQDIPCTL_DMA_DBG_SEL_OFST));
			dev_info(imgsys_dev->dev, "%s: sel(0x%08x): %08X", __func__,
			(unsigned int)(0x00A00 + i),
			(unsigned int)ioread32((void *)(pqdipRegBA + PQDIPCTL_DMA_DBG_OUT_OFST)));
			iowrite32((0x00B00 + i), (void *)(pqdipRegBA + PQDIPCTL_DMA_DBG_SEL_OFST));
			dev_info(imgsys_dev->dev, "%s: sel(0x%08x): %08X", __func__,
			(unsigned int)(0x00B00 + i),
			(unsigned int)ioread32((void *)(pqdipRegBA + PQDIPCTL_DMA_DBG_OUT_OFST)));
			iowrite32((0x00C00 + i), (void *)(pqdipRegBA + PQDIPCTL_DMA_DBG_SEL_OFST));
			dev_info(imgsys_dev->dev, "%s: sel(0x%08x): %08X", __func__,
			(unsigned int)(0x00C00 + i),
			(unsigned int)ioread32((void *)(pqdipRegBA + PQDIPCTL_DMA_DBG_OUT_OFST)));
		}

		dev_info(imgsys_dev->dev, "%s: smi port debug\n", __func__);
		iowrite32(0x1000046, (void *)(pqdipRegBA + PQDIPCTL_DMA_DBG_SEL_OFST));
		dev_info(imgsys_dev->dev, "%s: sel(0x1000046): %08X", __func__,
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIPCTL_DMA_DBG_OUT_OFST)));
		iowrite32(0x1000047, (void *)(pqdipRegBA + PQDIPCTL_DMA_DBG_SEL_OFST));
		dev_info(imgsys_dev->dev, "%s: sel(0x1000047): %08X", __func__,
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIPCTL_DMA_DBG_OUT_OFST)));
		iowrite32(0x1000048, (void *)(pqdipRegBA + PQDIPCTL_DMA_DBG_SEL_OFST));
		dev_info(imgsys_dev->dev, "%s: sel(0x1000048): %08X", __func__,
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIPCTL_DMA_DBG_OUT_OFST)));
		iowrite32(0x1000049, (void *)(pqdipRegBA + PQDIPCTL_DMA_DBG_SEL_OFST));
		dev_info(imgsys_dev->dev, "%s: sel(0x1000049): %08X", __func__,
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIPCTL_DMA_DBG_OUT_OFST)));
		iowrite32(0x100004A, (void *)(pqdipRegBA + PQDIPCTL_DMA_DBG_SEL_OFST));
		dev_info(imgsys_dev->dev, "%s: sel(0x100004A): %08X", __func__,
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIPCTL_DMA_DBG_OUT_OFST)));
		iowrite32(0x100004B, (void *)(pqdipRegBA + PQDIPCTL_DMA_DBG_SEL_OFST));
		dev_info(imgsys_dev->dev, "%s: sel(0x100004B): %08X", __func__,
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIPCTL_DMA_DBG_OUT_OFST)));

		dev_info(imgsys_dev->dev, "%s: smi latency debug\n", __func__);
		iowrite32(0x0080, (void *)(pqdipRegBA + PQDIPCTL_DMA_DBG_SEL_OFST));
		dev_info(imgsys_dev->dev, "%s: sel(0x0080): %08X", __func__,
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIPCTL_DMA_DBG_OUT_OFST)));
		iowrite32(0x0081, (void *)(pqdipRegBA + PQDIPCTL_DMA_DBG_SEL_OFST));
		dev_info(imgsys_dev->dev, "%s: sel(0x0081): %08X", __func__,
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIPCTL_DMA_DBG_OUT_OFST)));
		iowrite32(0x0082, (void *)(pqdipRegBA + PQDIPCTL_DMA_DBG_SEL_OFST));
		dev_info(imgsys_dev->dev, "%s: sel(0x0082): %08X", __func__,
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIPCTL_DMA_DBG_OUT_OFST)));

		dev_info(imgsys_dev->dev, "%s: smi arbiter debug\n", __func__);
		iowrite32(0x00A0, (void *)(pqdipRegBA + PQDIPCTL_DMA_DBG_SEL_OFST));
		dev_info(imgsys_dev->dev, "%s: sel(0x00A0): %08X", __func__,
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIPCTL_DMA_DBG_OUT_OFST)));
		iowrite32(0x00A1, (void *)(pqdipRegBA + PQDIPCTL_DMA_DBG_SEL_OFST));
		dev_info(imgsys_dev->dev, "%s: sel(0x00A1): %08X", __func__,
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIPCTL_DMA_DBG_OUT_OFST)));
		iowrite32(0x00A2, (void *)(pqdipRegBA + PQDIPCTL_DMA_DBG_SEL_OFST));
		dev_info(imgsys_dev->dev, "%s: sel(0x00A2): %08X", __func__,
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIPCTL_DMA_DBG_OUT_OFST)));
		iowrite32(0x00A3, (void *)(pqdipRegBA + PQDIPCTL_DMA_DBG_SEL_OFST));
		dev_info(imgsys_dev->dev, "%s: sel(0x00A3): %08X", __func__,
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIPCTL_DMA_DBG_OUT_OFST)));
		iowrite32(0x00A4, (void *)(pqdipRegBA + PQDIPCTL_DMA_DBG_SEL_OFST));
		dev_info(imgsys_dev->dev, "%s: sel(0x00A4): %08X", __func__,
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIPCTL_DMA_DBG_OUT_OFST)));
		iowrite32(0x00A5, (void *)(pqdipRegBA + PQDIPCTL_DMA_DBG_SEL_OFST));
		dev_info(imgsys_dev->dev, "%s: sel(0x00A5): %08X", __func__,
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIPCTL_DMA_DBG_OUT_OFST)));

		dev_info(imgsys_dev->dev, "%s: dma addr debug\n", __func__);
		iowrite32(0x00C0, (void *)(pqdipRegBA + PQDIPCTL_DMA_DBG_SEL_OFST));
		dev_info(imgsys_dev->dev, "%s: sel(0x00C0): %08X", __func__,
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIPCTL_DMA_DBG_OUT_OFST)));
		iowrite32(0x00C1, (void *)(pqdipRegBA + PQDIPCTL_DMA_DBG_SEL_OFST));
		dev_info(imgsys_dev->dev, "%s: sel(0x00C1): %08X", __func__,
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIPCTL_DMA_DBG_OUT_OFST)));
		iowrite32(0x01C0, (void *)(pqdipRegBA + PQDIPCTL_DMA_DBG_SEL_OFST));
		dev_info(imgsys_dev->dev, "%s: sel(0x01C0): %08X", __func__,
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIPCTL_DMA_DBG_OUT_OFST)));
		iowrite32(0x01C1, (void *)(pqdipRegBA + PQDIPCTL_DMA_DBG_SEL_OFST));
		dev_info(imgsys_dev->dev, "%s: sel(0x01C1): %08X", __func__,
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIPCTL_DMA_DBG_OUT_OFST)));
		iowrite32(0x02C0, (void *)(pqdipRegBA + PQDIPCTL_DMA_DBG_SEL_OFST));
		dev_info(imgsys_dev->dev, "%s: sel(0x02C0): %08X", __func__,
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIPCTL_DMA_DBG_OUT_OFST)));
		iowrite32(0x02C1, (void *)(pqdipRegBA + PQDIPCTL_DMA_DBG_SEL_OFST));
		dev_info(imgsys_dev->dev, "%s: sel(0x02C1): %08X", __func__,
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIPCTL_DMA_DBG_OUT_OFST)));
		iowrite32(0x03C0, (void *)(pqdipRegBA + PQDIPCTL_DMA_DBG_SEL_OFST));
		dev_info(imgsys_dev->dev, "%s: sel(0x03C0): %08X", __func__,
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIPCTL_DMA_DBG_OUT_OFST)));
		iowrite32(0x03C1, (void *)(pqdipRegBA + PQDIPCTL_DMA_DBG_SEL_OFST));
		dev_info(imgsys_dev->dev, "%s: sel(0x03C1): %08X", __func__,
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIPCTL_DMA_DBG_OUT_OFST)));
		iowrite32(0x04C0, (void *)(pqdipRegBA + PQDIPCTL_DMA_DBG_SEL_OFST));
		dev_info(imgsys_dev->dev, "%s: sel(0x04C0): %08X", __func__,
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIPCTL_DMA_DBG_OUT_OFST)));
		iowrite32(0x04C1, (void *)(pqdipRegBA + PQDIPCTL_DMA_DBG_SEL_OFST));
		dev_info(imgsys_dev->dev, "%s: sel(0x04C1): %08X", __func__,
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIPCTL_DMA_DBG_OUT_OFST)));

		//CTL_DBG
		dev_info(imgsys_dev->dev, "%s: tdr debug\n", __func__);
		iowrite32(0x00004, (void *)(pqdipRegBA + PQDIPCTL_DBG_SEL_OFST));
		dev_info(imgsys_dev->dev, "%s: tdr   sel(0x00004): %08X", __func__,
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIPCTL_DBG_OUT_OFST)));
		iowrite32(0x10004, (void *)(pqdipRegBA + PQDIPCTL_DBG_SEL_OFST));
		dev_info(imgsys_dev->dev, "%s: tdr   sel(0x10004): %08X", __func__,
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIPCTL_DBG_OUT_OFST)));
		iowrite32(0x20004, (void *)(pqdipRegBA + PQDIPCTL_DBG_SEL_OFST));
		dev_info(imgsys_dev->dev, "%s: tdr   sel(0x20004): %08X", __func__,
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIPCTL_DBG_OUT_OFST)));
		iowrite32(0x30004, (void *)(pqdipRegBA + PQDIPCTL_DBG_SEL_OFST));
		dev_info(imgsys_dev->dev, "%s: tdr   sel(0x30004): %08X", __func__,
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIPCTL_DBG_OUT_OFST)));
		iowrite32(0x40004, (void *)(pqdipRegBA + PQDIPCTL_DBG_SEL_OFST));
		dev_info(imgsys_dev->dev, "%s: tdr   sel(0x40004): %08X", __func__,
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIPCTL_DBG_OUT_OFST)));
		iowrite32(0x50004, (void *)(pqdipRegBA + PQDIPCTL_DBG_SEL_OFST));
		dev_info(imgsys_dev->dev, "%s: tdr   sel(0x50004): %08X", __func__,
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIPCTL_DBG_OUT_OFST)));
		iowrite32(0x60004, (void *)(pqdipRegBA + PQDIPCTL_DBG_SEL_OFST));
		dev_info(imgsys_dev->dev, "%s: tdr   sel(0x60004): %08X", __func__,
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIPCTL_DBG_OUT_OFST)));
		iowrite32(0x70004, (void *)(pqdipRegBA + PQDIPCTL_DBG_SEL_OFST));
		dev_info(imgsys_dev->dev, "%s: tdr   sel(0x70004): %08X", __func__,
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIPCTL_DBG_OUT_OFST)));
		iowrite32(0x80004, (void *)(pqdipRegBA + PQDIPCTL_DBG_SEL_OFST));
		dev_info(imgsys_dev->dev, "%s: tdr   sel(0x80004): %08X", __func__,
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIPCTL_DBG_OUT_OFST)));
		iowrite32(0x90004, (void *)(pqdipRegBA + PQDIPCTL_DBG_SEL_OFST));
		dev_info(imgsys_dev->dev, "%s: tdr   sel(0x90004): %08X", __func__,
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIPCTL_DBG_OUT_OFST)));
		iowrite32(0xa0004, (void *)(pqdipRegBA + PQDIPCTL_DBG_SEL_OFST));
		dev_info(imgsys_dev->dev, "%s: tdr   sel(0xa0004): %08X", __func__,
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIPCTL_DBG_OUT_OFST)));
		iowrite32(0xb0004, (void *)(pqdipRegBA + PQDIPCTL_DBG_SEL_OFST));
		dev_info(imgsys_dev->dev, "%s: tdr   sel(0xb0004): %08X", __func__,
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIPCTL_DBG_OUT_OFST)));

		dev_info(imgsys_dev->dev, "%s: module debug\n", __func__);
		iowrite32(0x00001, (void *)(pqdipRegBA + PQDIPCTL_DBG_SEL_OFST));
		dev_info(imgsys_dev->dev, "%s: rzh4n6t sel(0x00001): %08X", __func__,
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIPCTL_DBG_OUT_OFST)));
		iowrite32(0x00101, (void *)(pqdipRegBA + PQDIPCTL_DBG_SEL_OFST));
		dev_info(imgsys_dev->dev, "%s: tdshp sel(0x00101): %08X", __func__,
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIPCTL_DBG_OUT_OFST)));
		iowrite32(0x10201, (void *)(pqdipRegBA + PQDIPCTL_DBG_SEL_OFST));
		dev_info(imgsys_dev->dev, "%s: tcc   sel(0x10201): %08X", __func__,
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIPCTL_DBG_OUT_OFST)));
		iowrite32(0x20201, (void *)(pqdipRegBA + PQDIPCTL_DBG_SEL_OFST));
		dev_info(imgsys_dev->dev, "%s: tcc   sel(0x20201): %08X", __func__,
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIPCTL_DBG_OUT_OFST)));
		iowrite32(0x30201, (void *)(pqdipRegBA + PQDIPCTL_DBG_SEL_OFST));
		dev_info(imgsys_dev->dev, "%s: tcc   sel(0x30201): %08X", __func__,
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIPCTL_DBG_OUT_OFST)));
		iowrite32(0x40201, (void *)(pqdipRegBA + PQDIPCTL_DBG_SEL_OFST));
		dev_info(imgsys_dev->dev, "%s: tcc   sel(0x40201): %08X", __func__,
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIPCTL_DBG_OUT_OFST)));
		iowrite32(0x00301, (void *)(pqdipRegBA + PQDIPCTL_DBG_SEL_OFST));
		dev_info(imgsys_dev->dev, "%s: wrot  sel(0x00301): %08X", __func__,
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIPCTL_DBG_OUT_OFST)));
		iowrite32(0x10401, (void *)(pqdipRegBA + PQDIPCTL_DBG_SEL_OFST));
		dev_info(imgsys_dev->dev, "%s: unp1  sel(0x10401): %08X", __func__,
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIPCTL_DBG_OUT_OFST)));
		iowrite32(0x20401, (void *)(pqdipRegBA + PQDIPCTL_DBG_SEL_OFST));
		dev_info(imgsys_dev->dev, "%s: unp1  sel(0x20401): %08X", __func__,
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIPCTL_DBG_OUT_OFST)));
		iowrite32(0x30401, (void *)(pqdipRegBA + PQDIPCTL_DBG_SEL_OFST));
		dev_info(imgsys_dev->dev, "%s: unp1  sel(0x30401): %08X", __func__,
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIPCTL_DBG_OUT_OFST)));
		iowrite32(0x40401, (void *)(pqdipRegBA + PQDIPCTL_DBG_SEL_OFST));
		dev_info(imgsys_dev->dev, "%s: unp1  sel(0x40401): %08X", __func__,
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIPCTL_DBG_OUT_OFST)));
		iowrite32(0x50401, (void *)(pqdipRegBA + PQDIPCTL_DBG_SEL_OFST));
		dev_info(imgsys_dev->dev, "%s: unp1  sel(0x50401): %08X", __func__,
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIPCTL_DBG_OUT_OFST)));
		iowrite32(0x60401, (void *)(pqdipRegBA + PQDIPCTL_DBG_SEL_OFST));
		dev_info(imgsys_dev->dev, "%s: unp1  sel(0x60401): %08X", __func__,
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIPCTL_DBG_OUT_OFST)));
		iowrite32(0x10501, (void *)(pqdipRegBA + PQDIPCTL_DBG_SEL_OFST));
		dev_info(imgsys_dev->dev, "%s: unp2  sel(0x10501): %08X", __func__,
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIPCTL_DBG_OUT_OFST)));
		iowrite32(0x20501, (void *)(pqdipRegBA + PQDIPCTL_DBG_SEL_OFST));
		dev_info(imgsys_dev->dev, "%s: unp2  sel(0x20501): %08X", __func__,
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIPCTL_DBG_OUT_OFST)));
		iowrite32(0x30501, (void *)(pqdipRegBA + PQDIPCTL_DBG_SEL_OFST));
		dev_info(imgsys_dev->dev, "%s: unp2  sel(0x30501): %08X", __func__,
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIPCTL_DBG_OUT_OFST)));
		iowrite32(0x40501, (void *)(pqdipRegBA + PQDIPCTL_DBG_SEL_OFST));
		dev_info(imgsys_dev->dev, "%s: unp2  sel(0x40501): %08X", __func__,
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIPCTL_DBG_OUT_OFST)));
		iowrite32(0x50501, (void *)(pqdipRegBA + PQDIPCTL_DBG_SEL_OFST));
		dev_info(imgsys_dev->dev, "%s: unp2  sel(0x50501): %08X", __func__,
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIPCTL_DBG_OUT_OFST)));
		iowrite32(0x60501, (void *)(pqdipRegBA + PQDIPCTL_DBG_SEL_OFST));
		dev_info(imgsys_dev->dev, "%s: unp2  sel(0x60501): %08X", __func__,
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIPCTL_DBG_OUT_OFST)));
		iowrite32(0x10601, (void *)(pqdipRegBA + PQDIPCTL_DBG_SEL_OFST));
		dev_info(imgsys_dev->dev, "%s: unp3  sel(0x10601): %08X", __func__,
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIPCTL_DBG_OUT_OFST)));
		iowrite32(0x20601, (void *)(pqdipRegBA + PQDIPCTL_DBG_SEL_OFST));
		dev_info(imgsys_dev->dev, "%s: unp3  sel(0x20601): %08X", __func__,
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIPCTL_DBG_OUT_OFST)));
		iowrite32(0x30601, (void *)(pqdipRegBA + PQDIPCTL_DBG_SEL_OFST));
		dev_info(imgsys_dev->dev, "%s: unp3  sel(0x30601): %08X", __func__,
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIPCTL_DBG_OUT_OFST)));
		iowrite32(0x40601, (void *)(pqdipRegBA + PQDIPCTL_DBG_SEL_OFST));
		dev_info(imgsys_dev->dev, "%s: unp3  sel(0x40601): %08X", __func__,
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIPCTL_DBG_OUT_OFST)));
		iowrite32(0x50601, (void *)(pqdipRegBA + PQDIPCTL_DBG_SEL_OFST));
		dev_info(imgsys_dev->dev, "%s: unp3  sel(0x50601): %08X", __func__,
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIPCTL_DBG_OUT_OFST)));
		iowrite32(0x60601, (void *)(pqdipRegBA + PQDIPCTL_DBG_SEL_OFST));
		dev_info(imgsys_dev->dev, "%s: unp3  sel(0x60601): %08X", __func__,
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIPCTL_DBG_OUT_OFST)));
		iowrite32(0x00701, (void *)(pqdipRegBA + PQDIPCTL_DBG_SEL_OFST));
		dev_info(imgsys_dev->dev, "%s: plnr  sel(0x00701): %08X", __func__,
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIPCTL_DBG_OUT_OFST)));
		iowrite32(0x00801, (void *)(pqdipRegBA + PQDIPCTL_DBG_SEL_OFST));
		dev_info(imgsys_dev->dev, "%s: c02   sel(0x00801): %08X", __func__,
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIPCTL_DBG_OUT_OFST)));
		iowrite32(0x10801, (void *)(pqdipRegBA + PQDIPCTL_DBG_SEL_OFST));
		dev_info(imgsys_dev->dev, "%s: c02   sel(0x10801): %08X", __func__,
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIPCTL_DBG_OUT_OFST)));
		iowrite32(0x20801, (void *)(pqdipRegBA + PQDIPCTL_DBG_SEL_OFST));
		dev_info(imgsys_dev->dev, "%s: c02   sel(0x20801): %08X", __func__,
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIPCTL_DBG_OUT_OFST)));
		iowrite32(0x30801, (void *)(pqdipRegBA + PQDIPCTL_DBG_SEL_OFST));
		dev_info(imgsys_dev->dev, "%s: c02   sel(0x30801): %08X", __func__,
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIPCTL_DBG_OUT_OFST)));
		iowrite32(0x10901, (void *)(pqdipRegBA + PQDIPCTL_DBG_SEL_OFST));
		dev_info(imgsys_dev->dev, "%s: c24   sel(0x10901): %08X", __func__,
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIPCTL_DBG_OUT_OFST)));
		iowrite32(0x10a01, (void *)(pqdipRegBA + PQDIPCTL_DBG_SEL_OFST));
		dev_info(imgsys_dev->dev, "%s: mcrp  sel(0x10a01): %08X", __func__,
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIPCTL_DBG_OUT_OFST)));
		iowrite32(0x20a01, (void *)(pqdipRegBA + PQDIPCTL_DBG_SEL_OFST));
		dev_info(imgsys_dev->dev, "%s: mcrp  sel(0x20a01): %08X", __func__,
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIPCTL_DBG_OUT_OFST)));
		iowrite32(0x30a01, (void *)(pqdipRegBA + PQDIPCTL_DBG_SEL_OFST));
		dev_info(imgsys_dev->dev, "%s: mcrp  sel(0x30a01): %08X", __func__,
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIPCTL_DBG_OUT_OFST)));
		iowrite32(0x40a01, (void *)(pqdipRegBA + PQDIPCTL_DBG_SEL_OFST));
		dev_info(imgsys_dev->dev, "%s: mcrp  sel(0x40a01): %08X", __func__,
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIPCTL_DBG_OUT_OFST)));
		iowrite32(0x50a01, (void *)(pqdipRegBA + PQDIPCTL_DBG_SEL_OFST));
		dev_info(imgsys_dev->dev, "%s: mcrp  sel(0x50a01): %08X", __func__,
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIPCTL_DBG_OUT_OFST)));
		iowrite32(0x00b01, (void *)(pqdipRegBA + PQDIPCTL_DBG_SEL_OFST));
		dev_info(imgsys_dev->dev, "%s: tcy   sel(0x00b01): %08X", __func__,
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIPCTL_DBG_OUT_OFST)));
		iowrite32(0x10b01, (void *)(pqdipRegBA + PQDIPCTL_DBG_SEL_OFST));
		dev_info(imgsys_dev->dev, "%s: tcy   sel(0x10b01): %08X", __func__,
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIPCTL_DBG_OUT_OFST)));
		iowrite32(0x20b01, (void *)(pqdipRegBA + PQDIPCTL_DBG_SEL_OFST));
		dev_info(imgsys_dev->dev, "%s: tcy   sel(0x20b01): %08X", __func__,
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIPCTL_DBG_OUT_OFST)));

		iowrite32(0x00106, (void *)(pqdipRegBA + PQDIPCTL_DBG_SEL_OFST));
		dev_info(imgsys_dev->dev, "%s: wif   sel(0x00106): %08X", __func__,
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIPCTL_DBG_OUT_OFST)));
		iowrite32(0x00107, (void *)(pqdipRegBA + PQDIPCTL_DBG_SEL_OFST));
		dev_info(imgsys_dev->dev, "%s: wif   sel(0x00107): %08X", __func__,
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIPCTL_DBG_OUT_OFST)));
		iowrite32(0x00108, (void *)(pqdipRegBA + PQDIPCTL_DBG_SEL_OFST));
		dev_info(imgsys_dev->dev, "%s: wif   sel(0x00108): %08X", __func__,
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIPCTL_DBG_OUT_OFST)));

		//WROT_DBG
		dev_info(imgsys_dev->dev, "%s: wrot debug\n", __func__);
		for (i = 1; i <= 32; i += 1) {
			iowrite32(i << 8, (void *)(pqdipRegBA + PQ_WROT_DBG_SEL_OFST));
			dev_info(imgsys_dev->dev, "%s: sel(0x%04x): %08X", __func__, i << 8,
			(unsigned int)ioread32((void *)(pqdipRegBA + PQ_WROT_DBG_OUT_OFST)));
		}

		//URZ6T_DBG
		dev_info(imgsys_dev->dev, "%s: rzh4n6t debug\n", __func__);
		for (i = 0; i < 16; i += 1) {
			iowrite32(i, (void *)(pqdipRegBA + PQ_RZH4N6T_DBG_SEL_OFST));
			dev_info(imgsys_dev->dev, "%s: sel(0x%02x): %08X", __func__, i,
			(unsigned int)ioread32((void *)(pqdipRegBA + PQ_RZH4N6T_DBG_OUT_OFST)));
		}

	}
	dev_info(imgsys_dev->dev, "%s: -\n", __func__);
}

void imgsys_pqdip_uninit(struct mtk_imgsys_dev *imgsys_dev)
{
	unsigned int i;

	for (i = 0; i < PQDIP_HW_SET; i++) {
		iounmap(gpqdipRegBA[i]);
		gpqdipRegBA[i] = 0L;
	}
}

void imgsys_pqdip_updatecq(struct mtk_imgsys_dev *imgsys_dev,
			struct img_swfrm_info *user_info, int req_fd, u64 tuning_iova,
			unsigned int mode)
{
	u64 iova_addr = tuning_iova;
	u64 *cq_desc = NULL;
	struct mtk_imgsys_pqdip_dtable *dtable = NULL;
	unsigned int i = 0, tun_ofst = 0, pq_hw = IMGSYS_PQDIP_A;
	struct flush_buf_info pqdip_buf_info;
	size_t dtbl_sz = sizeof(struct mtk_imgsys_pqdip_dtable);

	/* HWID defined in hw_definition.h */
	for (pq_hw = IMGSYS_PQDIP_A; pq_hw <= IMGSYS_PQDIP_B; pq_hw++) {
		if (!user_info->priv[pq_hw].need_update_desc)
			continue;
		if (iova_addr) {
			cq_desc = (u64 *)((void *)(
				#if SMVR_DECOUPLE
				mtk_hcp_get_pqdip_mem_virt(imgsys_dev->scp_pdev, mode) +
					user_info->priv[pq_hw].desc_offset));
				#else
					mtk_hcp_get_pqdip_mem_virt(imgsys_dev->scp_pdev) +
					user_info->priv[pq_hw].desc_offset));
				#endif
			for (i = 0; i < PQDIP_CQ_DESC_NUM; i++) {
				dtable = (struct mtk_imgsys_pqdip_dtable *)cq_desc + i;
				if ((dtable->addr_msb & PSEUDO_DESC_TUNING) == PSEUDO_DESC_TUNING) {
					tun_ofst = dtable->addr;
					dtable->addr = (tun_ofst + iova_addr) & 0xFFFFFFFF;
					dtable->addr_msb = ((tun_ofst + iova_addr) >> 32) & 0xF;
                    if (imgsys_pqdip_7sp_dbg_enable()) {
					pr_debug(
						"%s: pq%d tuning_buf_iova(0x%llx) des_ofst(0x%08x) cq_kva(0x%p) dtable(0x%x/0x%x/0x%x)\n",
						__func__, pq_hw, iova_addr,
						user_info->priv[pq_hw].desc_offset,
						cq_desc, dtable->empty, dtable->addr,
						dtable->addr_msb);
				}
			}
		}
		}
		//
		#if SMVR_DECOUPLE
		pqdip_buf_info.fd = mtk_hcp_get_pqdip_mem_cq_fd(imgsys_dev->scp_pdev, mode);
		#else
		pqdip_buf_info.fd = mtk_hcp_get_pqdip_mem_cq_fd(imgsys_dev->scp_pdev);
		#endif
		pqdip_buf_info.offset = user_info->priv[pq_hw].desc_offset;
		pqdip_buf_info.len =
			((dtbl_sz * PQDIP_CQ_DESC_NUM) + PQDIP_REG_SIZE);
		pqdip_buf_info.mode = mode;
		pqdip_buf_info.is_tuning = false;
        if (imgsys_pqdip_7sp_dbg_enable()) {
		pr_debug("imgsys_fw cq pqdip_buf_info (%d/%d/%d), mode(%d)",
			pqdip_buf_info.fd, pqdip_buf_info.len,
			pqdip_buf_info.offset, pqdip_buf_info.mode);
        }
		mtk_hcp_partial_flush(imgsys_dev->scp_pdev, &pqdip_buf_info);
	}

	for (pq_hw = IMGSYS_PQDIP_A; pq_hw <= IMGSYS_PQDIP_B; pq_hw++) {
		if (user_info->priv[pq_hw].need_flush_tdr) {
			// tdr buffer
			#if SMVR_DECOUPLE
			pqdip_buf_info.fd = mtk_hcp_get_pqdip_mem_tdr_fd(imgsys_dev->scp_pdev, mode);
			#else
			pqdip_buf_info.fd = mtk_hcp_get_pqdip_mem_tdr_fd(imgsys_dev->scp_pdev);
			#endif
			pqdip_buf_info.offset = user_info->priv[pq_hw].tdr_offset;
			pqdip_buf_info.len = PQDIP_TDR_BUF_MAXSZ;
			pqdip_buf_info.mode = mode;
			pqdip_buf_info.is_tuning = false;
            if (imgsys_pqdip_7sp_dbg_enable()) {
			pr_debug("imgsys_fw tdr pqdip_buf_info (%d/%d/%d), mode(%d)",
				pqdip_buf_info.fd, pqdip_buf_info.len,
				pqdip_buf_info.offset, pqdip_buf_info.mode);
            }
			mtk_hcp_partial_flush(imgsys_dev->scp_pdev, &pqdip_buf_info);
		}
	}
}

int imgsys_pqdip_tfault_callback(int port,
			dma_addr_t mva, void *data)
{
	void __iomem *pqdipRegBA = 0L;
	u32 hw_idx = 0, i = 0, larb = 0;
	int ret = 0;
	bool is_qof = false;

	/* port: [10:5] larb / larb11: pqdipa; else: pqdipb */
	larb = ((port>>5) & 0x3F);

	pr_info("%s: iommu port:0x%x, larb:%d, idx:%d, addr:0x%08lx, +\n", __func__,
		port, larb, (port & 0x1F), (unsigned long)mva);

	/* iomap registers */
	hw_idx = (larb == 11) ? 0 : 1;
	pqdipRegBA = gpqdipRegBA[hw_idx];

	if (!pqdipRegBA) {
		pr_info("%s: PQDIP_%d, RegBA=0, can't dump, -\n", __func__, hw_idx);
		return 1;
	}

	ret = smi_isp_wpe1_eis_get_if_in_use((void *)&is_qof);
	if (ret == -1) {
		pr_info("smi_isp_wpe1_eis_get_if_in_use = -1. can't dump. return\n");
		return 1;
	}
#ifdef DUMP_PQ_ALL
	for (i = 0x0; i < PQDIP_ALL_REG_CNT; i += 0x20) {
		pr_info("%s: [0x%08x] 0x%08x 0x%08x 0x%08x 0x%08x | 0x%08x 0x%08x 0x%08x 0x%08x",
		__func__, (unsigned int)(g_RegBaseAddrPQ + (PQDIP_OFST * hw_idx) + i),
		(unsigned int)ioread32((void *)(pqdipRegBA + i + 0x00)),
		(unsigned int)ioread32((void *)(pqdipRegBA + i + 0x04)),
		(unsigned int)ioread32((void *)(pqdipRegBA + i + 0x08)),
		(unsigned int)ioread32((void *)(pqdipRegBA + i + 0x0c)),
		(unsigned int)ioread32((void *)(pqdipRegBA + i + 0x10)),
		(unsigned int)ioread32((void *)(pqdipRegBA + i + 0x14)),
		(unsigned int)ioread32((void *)(pqdipRegBA + i + 0x18)),
		(unsigned int)ioread32((void *)(pqdipRegBA + i + 0x1c)));
	}
#else
	pr_info("%s:  ctl_reg", __func__);
	for (i = 0x0; i < PQDIP_CTL_REG_CNT; i += 0x20) {
		pr_info("%s:  [0x%08x] 0x%08x 0x%08x 0x%08x 0x%08x | 0x%08x 0x%08x 0x%08x 0x%08x",
		__func__, (unsigned int)(g_RegBaseAddrPQ + (PQDIP_OFST * hw_idx)
			+ PQDIP_CTL_OFST + i),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_CTL_OFST + i + 0x00)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_CTL_OFST + i + 0x04)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_CTL_OFST + i + 0x08)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_CTL_OFST + i + 0x0c)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_CTL_OFST + i + 0x10)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_CTL_OFST + i + 0x14)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_CTL_OFST + i + 0x18)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_CTL_OFST + i + 0x1c)));
	}

	pr_info("%s:  cq_reg", __func__);
	for (i = 0; i < PQDIP_CQ_REG_CNT; i += 0x20) {
		pr_info("%s:  [0x%08x] 0x%08x 0x%08x 0x%08x 0x%08x | 0x%08x 0x%08x 0x%08x 0x%08x",
		__func__, (unsigned int)(g_RegBaseAddrPQ + (PQDIP_OFST * hw_idx)
			+ PQDIP_CQ_OFST + i),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_CQ_OFST + i + 0x00)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_CQ_OFST + i + 0x04)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_CQ_OFST + i + 0x08)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_CQ_OFST + i + 0x0c)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_CQ_OFST + i + 0x10)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_CQ_OFST + i + 0x14)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_CQ_OFST + i + 0x18)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_CQ_OFST + i + 0x1c)));
	}

	pr_info("%s:  dma_reg", __func__);
	for (i = 0; i < PQDIP_DMA_REG_CNT; i += 0x20) {
		pr_info("%s:  [0x%08x] 0x%08x 0x%08x 0x%08x 0x%08x | 0x%08x 0x%08x 0x%08x 0x%08x",
		__func__, (unsigned int)(g_RegBaseAddrPQ + (PQDIP_OFST * hw_idx)
			+ PQDIP_DMA_OFST + i),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_DMA_OFST + i + 0x00)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_DMA_OFST + i + 0x04)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_DMA_OFST + i + 0x08)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_DMA_OFST + i + 0x0c)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_DMA_OFST + i + 0x10)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_DMA_OFST + i + 0x14)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_DMA_OFST + i + 0x18)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_DMA_OFST + i + 0x1c)));
	}

	pr_info("%s:  wrot_reg", __func__);
	for (i = 0; i < PQDIP_WROT1_REG_CNT; i += 0x20) {
		pr_info("%s:  [0x%08x] 0x%08x 0x%08x 0x%08x 0x%08x | 0x%08x 0x%08x 0x%08x 0x%08x",
		__func__, (unsigned int)(g_RegBaseAddrPQ + (PQDIP_OFST * hw_idx)
			+ PQDIP_WROT1_OFST + i),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_WROT1_OFST + i + 0x00)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_WROT1_OFST + i + 0x04)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_WROT1_OFST + i + 0x08)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_WROT1_OFST + i + 0x0c)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_WROT1_OFST + i + 0x10)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_WROT1_OFST + i + 0x14)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_WROT1_OFST + i + 0x18)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_WROT1_OFST + i + 0x1c)));
	}
	for (i = 0; i < PQDIP_WROT2_REG_CNT; i += 0x20) {
		pr_info("%s:  [0x%08x] 0x%08x 0x%08x 0x%08x 0x%08x | 0x%08x 0x%08x 0x%08x 0x%08x",
		__func__, (unsigned int)(g_RegBaseAddrPQ + (PQDIP_OFST * hw_idx)
			+ PQDIP_WROT2_OFST + i),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_WROT2_OFST + i + 0x00)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_WROT2_OFST + i + 0x04)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_WROT2_OFST + i + 0x08)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_WROT2_OFST + i + 0x0c)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_WROT2_OFST + i + 0x10)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_WROT2_OFST + i + 0x14)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_WROT2_OFST + i + 0x18)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_WROT2_OFST + i + 0x1c)));
	}

	pr_info("%s:  rzh4n6t_reg", __func__);
	for (i = 0; i < PQDIP_RZH4N6T_REG_CNT; i += 0x20) {
		pr_info("%s:  [0x%08x] 0x%08x 0x%08x 0x%08x 0x%08x | 0x%08x 0x%08x 0x%08x 0x%08x",
		__func__, (unsigned int)(g_RegBaseAddrPQ + (PQDIP_OFST * hw_idx)
			+ PQDIP_RZH4N6T_OFST + i),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_RZH4N6T_OFST + i + 0x00)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_RZH4N6T_OFST + i + 0x04)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_RZH4N6T_OFST + i + 0x08)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_RZH4N6T_OFST + i + 0x0c)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_RZH4N6T_OFST + i + 0x10)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_RZH4N6T_OFST + i + 0x14)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_RZH4N6T_OFST + i + 0x18)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_RZH4N6T_OFST + i + 0x1c)));
	}

	pr_info("%s:  tdshp_reg", __func__);
	for (i = 0; i < PQDIP_TDSHP1REG_CNT; i += 0x20) {
		pr_info("%s:  [0x%08x] 0x%08x 0x%08x 0x%08x 0x%08x | 0x%08x 0x%08x 0x%08x 0x%08x",
		__func__, (unsigned int)(g_RegBaseAddrPQ + (PQDIP_OFST * hw_idx)
			+ PQDIP_TDSHP1OFST + i),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_TDSHP1OFST + i + 0x00)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_TDSHP1OFST + i + 0x04)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_TDSHP1OFST + i + 0x08)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_TDSHP1OFST + i + 0x0c)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_TDSHP1OFST + i + 0x10)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_TDSHP1OFST + i + 0x14)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_TDSHP1OFST + i + 0x18)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_TDSHP1OFST + i + 0x1c)));
	}
	for (i = 0; i < PQDIP_TDSHP2REG_CNT; i += 0x20) {
		pr_info("%s:  [0x%08x] 0x%08x 0x%08x 0x%08x 0x%08x | 0x%08x 0x%08x 0x%08x 0x%08x",
		__func__, (unsigned int)(g_RegBaseAddrPQ + (PQDIP_OFST * hw_idx)
			+ PQDIP_TDSHP2OFST + i),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_TDSHP2OFST + i + 0x00)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_TDSHP2OFST + i + 0x04)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_TDSHP2OFST + i + 0x08)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_TDSHP2OFST + i + 0x0c)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_TDSHP2OFST + i + 0x10)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_TDSHP2OFST + i + 0x14)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_TDSHP2OFST + i + 0x18)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_TDSHP2OFST + i + 0x1c)));
	}
	for (i = 0; i < PQDIP_TDSHP3REG_CNT; i += 0x20) {
		pr_info("%s:  [0x%08x] 0x%08x 0x%08x 0x%08x 0x%08x | 0x%08x 0x%08x 0x%08x 0x%08x",
		__func__, (unsigned int)(g_RegBaseAddrPQ + (PQDIP_OFST * hw_idx)
			+ PQDIP_TDSHP3OFST + i),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_TDSHP3OFST + i + 0x00)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_TDSHP3OFST + i + 0x04)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_TDSHP3OFST + i + 0x08)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_TDSHP3OFST + i + 0x0c)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_TDSHP3OFST + i + 0x10)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_TDSHP3OFST + i + 0x14)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_TDSHP3OFST + i + 0x18)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_TDSHP3OFST + i + 0x1c)));
	}

	pr_info("%s:  unp_reg", __func__);
	for (i = 0; i < PQDIP_UNP_REG_CNT; i += 0x20) {
		pr_info("%s:  [0x%08x] 0x%08x 0x%08x 0x%08x 0x%08x | 0x%08x 0x%08x 0x%08x 0x%08x",
		__func__, (unsigned int)(g_RegBaseAddrPQ + (PQDIP_OFST * hw_idx)
			+ PQDIP_UNP1_OFST + i),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_UNP1_OFST + i + 0x00)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_UNP1_OFST + i + 0x04)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_UNP1_OFST + i + 0x08)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_UNP1_OFST + i + 0x0c)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_UNP1_OFST + i + 0x10)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_UNP1_OFST + i + 0x14)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_UNP1_OFST + i + 0x18)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_UNP1_OFST + i + 0x1c)));
	}
	for (i = 0; i < PQDIP_UNP_REG_CNT; i += 0x20) {
		pr_info("%s:  [0x%08x] 0x%08x 0x%08x 0x%08x 0x%08x | 0x%08x 0x%08x 0x%08x 0x%08x",
		__func__, (unsigned int)(g_RegBaseAddrPQ + (PQDIP_OFST * hw_idx)
			+ PQDIP_UNP2_OFST + i),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_UNP2_OFST + i + 0x00)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_UNP2_OFST + i + 0x04)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_UNP2_OFST + i + 0x08)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_UNP2_OFST + i + 0x0c)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_UNP2_OFST + i + 0x10)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_UNP2_OFST + i + 0x14)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_UNP2_OFST + i + 0x18)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_UNP2_OFST + i + 0x1c)));
	}
	for (i = 0; i < PQDIP_UNP_REG_CNT; i += 0x20) {
		pr_info("%s:  [0x%08x] 0x%08x 0x%08x 0x%08x 0x%08x | 0x%08x 0x%08x 0x%08x 0x%08x",
		__func__, (unsigned int)(g_RegBaseAddrPQ + (PQDIP_OFST * hw_idx)
			+ PQDIP_UNP3_OFST + i),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_UNP3_OFST + i + 0x00)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_UNP3_OFST + i + 0x04)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_UNP3_OFST + i + 0x08)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_UNP3_OFST + i + 0x0c)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_UNP3_OFST + i + 0x10)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_UNP3_OFST + i + 0x14)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_UNP3_OFST + i + 0x18)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_UNP3_OFST + i + 0x1c)));
	}

	pr_info("%s:  c02_reg", __func__);
	for (i = 0; i < PQDIP_C02_REG_CNT; i += 0x20) {
		pr_info("%s:  [0x%08x] 0x%08x 0x%08x 0x%08x 0x%08x | 0x%08x 0x%08x 0x%08x 0x%08x",
		__func__, (unsigned int)(g_RegBaseAddrPQ + (PQDIP_OFST * hw_idx)
			+ PQDIP_C02_OFST + i),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_C02_OFST + i + 0x00)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_C02_OFST + i + 0x04)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_C02_OFST + i + 0x08)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_C02_OFST + i + 0x0c)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_C02_OFST + i + 0x10)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_C02_OFST + i + 0x14)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_C02_OFST + i + 0x18)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_C02_OFST + i + 0x1c)));
	}

	pr_info("%s:  c24_reg", __func__);
	for (i = 0; i < PQDIP_C24_REG_CNT; i += 0x20) {
		pr_info("%s:  [0x%08x] 0x%08x 0x%08x 0x%08x 0x%08x | 0x%08x 0x%08x 0x%08x 0x%08x",
		__func__, (unsigned int)(g_RegBaseAddrPQ + (PQDIP_OFST * hw_idx)
			+ PQDIP_C24_OFST + i),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_C24_OFST + i + 0x00)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_C24_OFST + i + 0x04)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_C24_OFST + i + 0x08)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_C24_OFST + i + 0x0c)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_C24_OFST + i + 0x10)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_C24_OFST + i + 0x14)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_C24_OFST + i + 0x18)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_C24_OFST + i + 0x1c)));
	}

	pr_info("%s:  mcrp_reg", __func__);
	for (i = 0; i < PQDIP_MCRP_REG_CNT; i += 0x20) {
		pr_info("%s:  [0x%08x] 0x%08x 0x%08x 0x%08x 0x%08x | 0x%08x 0x%08x 0x%08x 0x%08x",
		__func__, (unsigned int)(g_RegBaseAddrPQ + (PQDIP_OFST * hw_idx)
			+ PQDIP_MCRP_OFST + i),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_MCRP_OFST + i + 0x00)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_MCRP_OFST + i + 0x04)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_MCRP_OFST + i + 0x08)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_MCRP_OFST + i + 0x0c)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_MCRP_OFST + i + 0x10)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_MCRP_OFST + i + 0x14)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_MCRP_OFST + i + 0x18)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_MCRP_OFST + i + 0x1c)));
	}

	pr_info("%s:  tcc_reg", __func__);
	for (i = 0; i < PQDIP_TCC_REG_CNT; i += 0x20) {
		pr_info("%s:  [0x%08x] 0x%08x 0x%08x 0x%08x 0x%08x | 0x%08x 0x%08x 0x%08x 0x%08x",
		__func__, (unsigned int)(g_RegBaseAddrPQ + (PQDIP_OFST * hw_idx)
			+ PQDIP_TCC_OFST + i),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_TCC_OFST + i + 0x00)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_TCC_OFST + i + 0x04)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_TCC_OFST + i + 0x08)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_TCC_OFST + i + 0x0c)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_TCC_OFST + i + 0x10)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_TCC_OFST + i + 0x14)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_TCC_OFST + i + 0x18)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_TCC_OFST + i + 0x1c)));
	}

	pr_info("%s:  tcy_reg", __func__);
	for (i = 0; i < PQDIP_TCY_REG_CNT; i += 0x20) {
		pr_info("%s:  [0x%08x] 0x%08x 0x%08x 0x%08x 0x%08x | 0x%08x 0x%08x 0x%08x 0x%08x",
		__func__, (unsigned int)(g_RegBaseAddrPQ + (PQDIP_OFST * hw_idx)
			+ PQDIP_TCY_OFST + i),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_TCY_OFST + i + 0x00)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_TCY_OFST + i + 0x04)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_TCY_OFST + i + 0x08)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_TCY_OFST + i + 0x0c)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_TCY_OFST + i + 0x10)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_TCY_OFST + i + 0x14)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_TCY_OFST + i + 0x18)),
		(unsigned int)ioread32((void *)(pqdipRegBA + PQDIP_TCY_OFST + i + 0x1c)));
	}
#endif

	smi_isp_wpe1_eis_put((void *)&is_qof);
	pr_info("%s: -. is_qof=%d\n", __func__, is_qof);

	return 1;
}

bool imgsys_pqdip_done_chk(struct mtk_imgsys_dev *imgsys_dev, uint32_t engine)
{
	bool ret = true; //true: done

	uint32_t i = 0, hw_start = 0, hw_end = 1, value = 0;
 	uint32_t reg_ofst = 0x84; //PQDIPCTL_INT2_STATUSX

	if (engine & IMGSYS_ENG_PQDIP_B) {
		hw_start = 1;
		hw_end = PQDIP_HW_SET;
	}
	if (engine & IMGSYS_ENG_PQDIP_A) {
		hw_start = 0;
	}

	for (i = hw_start; i < hw_end; i++) {
		value = (uint32_t)ioread32((void *)(gpqdipRegBA[i] + reg_ofst));

		if (!(value & 0x1)) {
			ret = false;
			pr_info(
			"%s: hw_comb:0x%x, polling PQDIP_%d done fail!!! [0x%08x] 0x%x",
			__func__, engine, i,
			(uint32_t)(g_RegBaseAddrPQ + (PQDIP_OFST * i) + reg_ofst), value);
		}
	}

	return ret;
}