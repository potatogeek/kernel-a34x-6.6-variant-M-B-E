// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2021 MediaTek Inc.
 */

#include <linux/clk.h>
#include <linux/component.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>

#ifndef DRM_CMDQ_DISABLE
#include <linux/soc/mediatek/mtk-cmdq-ext.h>
#else
#include "mtk-cmdq-ext.h"
#endif

#include "mtk_drm_crtc.h"
#include "mtk_drm_ddp_comp.h"
#include "mtk_dump.h"
#include "mtk_drm_mmp.h"
#include "mtk_drm_gem.h"
#include "mtk_drm_fb.h"
#include "mtk_drm_trace.h"
#include "mtk_drm_drv.h"
#include "mtk_disp_wdma.h"
#include "mtk_disp_pmqos.h"
#include "platform/mtk_drm_platform.h"

#define DO_DIV_ROUND_UP(n, d) DO_COMMON_DIV(((n) + (d) - 1), (d))
#define DISP_REG_WDMA_INTEN 0x0000
#define INTEN_FLD_FME_CPL_INTEN REG_FLD_MSB_LSB(0, 0)
#define INTEN_FLD_FME_UND_INTEN REG_FLD_MSB_LSB(1, 1)
#define DISP_REG_WDMA_INTSTA 0x0004
#define DISP_REG_WDMA_EN 0x0008
#define WDMA_EN BIT(0)
#define DISP_REG_WDMA_RST 0x000c
#define DISP_REG_WDMA_SMI_CON 0x0010
#define SMI_CON_FLD_THRESHOLD REG_FLD_MSB_LSB(3, 0)
#define SMI_CON_FLD_SLOW_ENABLE REG_FLD_MSB_LSB(4, 4)
#define SMI_CON_FLD_SLOW_LEVEL REG_FLD_MSB_LSB(7, 5)
#define SMI_CON_FLD_SLOW_COUNT REG_FLD_MSB_LSB(15, 8)
#define SMI_CON_FLD_SMI_Y_REPEAT_NUM REG_FLD_MSB_LSB(19, 16)
#define SMI_CON_FLD_SMI_U_REPEAT_NUM REG_FLD_MSB_LSB(23, 20)
#define SMI_CON_FLD_SMI_V_REPEAT_NUM REG_FLD_MSB_LSB(27, 24)
#define SMI_CON_FLD_SMI_OBUF_FULL_REQ REG_FLD_MSB_LSB(28, 28)
#define DISP_REG_WDMA_CFG 0x0014
#define CFG_FLD_UFO_DCP_ENABLE REG_FLD_MSB_LSB(18, 18)
#define WDMA_OUT_FMT (0xf << 4)
#define WDMA_CT_EN BIT(11)
#define WDMA_CON_SWAP BIT(16)
#define WDMA_UFO_DCP_ENABLE BIT(18)
#define WDMA_BKGD_ENABLE BIT(20)
#define WDMA_INT_MTX_SEL (0xf << 24)
#define WDMA_DEBUG_SEL (0xf << 28)
#define DISP_REG_WDMA_SRC_SIZE 0x0018
#define DISP_REG_WDMA_CLIP_SIZE 0x001c
#define DISP_REG_WDMA_CLIP_COORD 0x0020
#define DISP_REG_WDMA_SHADOW_CTRL 0x0024
#define WDMA_BYPASS_SHADOW BIT(1)
#define WDMA_READ_WRK_REG BIT(2)
#define DISP_REG_WDMA_DST_WIN_BYTE 0x0028
#define DISP_REG_WDMA_ALPHA 0x002C
#define DISP_REG_WDMA_DST_UV_PITCH 0x0078
#define DISP_REG_WDMA_BKGD 0x0030
#define DISP_REG_WDMA_BKGD_MSB 0x0034
#define WDMA_A_Value (0xff)
#define WDMA_A_Sel BIT(31)
#define DISP_REG_WDMA_DST_ADDR_OFFSETX(n) (0x0080 + 0x04 * (n))
#define WDMA_SECURITY_DISABLE 0xF10

#define DISP_REG_WDMA_FLOW_CTRL_DBG 0x00A0
#define FLOW_CTRL_DBG_FLD_WDMA_STA_FLOW_CTRL REG_FLD_MSB_LSB(9, 0)
#define FLOW_CTRL_DBG_FLD_WDMA_IN_READY REG_FLD_MSB_LSB(14, 14)
#define FLOW_CTRL_DBG_FLD_WDMA_IN_VALID REG_FLD_MSB_LSB(15, 15)
#define DISP_REG_WDMA_EXEC_DBG 0x000A4
#define EXEC_DBG_FLD_WDMA_STA_EXEC REG_FLD_MSB_LSB(31, 0)
#define DISP_REG_WDMA_INPUT_CNT_DBG 0x000A8
#define DISP_REG_WDMA_SMI_TRAFFIC_DBG 0x000AC
#define DISP_REG_WDMA_DEBUG 0x000B8
#define DISP_REG_WDMA_BUF_CON3 0x0104
#define BUF_CON3_FLD_ISSUE_REQ_TH_Y REG_FLD_MSB_LSB(8, 0)
#define BUF_CON3_FLD_ISSUE_REQ_TH_U REG_FLD_MSB_LSB(24, 16)
#define DISP_REG_WDMA_BUF_CON4 0x0108
#define BUF_CON4_FLD_ISSUE_REQ_TH_V REG_FLD_MSB_LSB(8, 0)
#define DISP_REG_WDMA_BUF_CON1 0x0038
#define BUF_CON1_FLD_FIFO_PSEUDO_SIZE REG_FLD_MSB_LSB(9, 0)
#define BUF_CON1_FLD_FIFO_PSEUDO_SIZE_UV REG_FLD_MSB_LSB(18, 10)
#define BUF_CON1_FLD_URGENT_EN REG_FLD_MSB_LSB(26, 26)
#define BUF_CON1_FLD_ALPHA_MASK_EN REG_FLD_MSB_LSB(27, 27)
#define BUF_CON1_FLD_FRAME_END_ULTRA REG_FLD_MSB_LSB(28, 28)
#define BUF_CON1_FLD_GCLAST_EN REG_FLD_MSB_LSB(29, 29)
#define BUF_CON1_FLD_PRE_ULTRA_ENABLE REG_FLD_MSB_LSB(30, 30)
#define BUF_CON1_FLD_ULTRA_ENABLE REG_FLD_MSB_LSB(31, 31)
#define DISP_REG_WDMA_BUF_CON5 0x0200
#define DISP_REG_WDMA_BUF_CON6 0x0204
#define DISP_REG_WDMA_BUF_CON7 0x0208
#define DISP_REG_WDMA_BUF_CON8 0x020C
#define DISP_REG_WDMA_BUF_CON9 0x0210
#define DISP_REG_WDMA_BUF_CON10 0x0214
#define DISP_REG_WDMA_BUF_CON11 0x0218
#define DISP_REG_WDMA_BUF_CON12 0x021C
#define DISP_REG_WDMA_BUF_CON13 0x0220
#define DISP_REG_WDMA_BUF_CON14 0x0224
#define DISP_REG_WDMA_BUF_CON15 0x0228
#define BUF_CON_FLD_PRE_ULTRA_LOW REG_FLD_MSB_LSB(9, 0)
#define BUF_CON_FLD_ULTRA_LOW REG_FLD_MSB_LSB(25, 16)
#define DISP_REG_WDMA_BUF_CON16 0x022C
#define BUF_CON_FLD_PRE_ULTRA_HIGH REG_FLD_MSB_LSB(9, 0)
#define BUF_CON_FLD_ULTRA_HIGH REG_FLD_MSB_LSB(25, 16)
#define DISP_REG_WDMA_BUF_CON17 0x0230
#define BUF_CON17_FLD_WDMA_DVFS_EN REG_FLD_MSB_LSB(0, 0)
#define BUF_CON17_FLD_DVFS_TH_Y REG_FLD_MSB_LSB(25, 16)
#define DISP_REG_WDMA_BUF_CON18 0x0234
#define BUF_CON18_FLD_DVFS_TH_U REG_FLD_MSB_LSB(9, 0)
#define BUF_CON18_FLD_DVFS_TH_V REG_FLD_MSB_LSB(25, 16)
#define DISP_REG_WDMA_DRS_CON0 0x0250
#define DISP_REG_WDMA_DRS_CON1 0x0254
#define DISP_REG_WDMA_DRS_CON2 0x0258
#define DISP_REG_WDMA_DRS_CON3 0x025C

#define DISP_REG_WDMA_URGENT_CON0 0x0260
#define FLD_WDMA_URGENT_LOW_Y REG_FLD_MSB_LSB(9, 0)
#define FLD_WDMA_URGENT_HIGH_Y REG_FLD_MSB_LSB(25, 16)
#define DISP_REG_WDMA_URGENT_CON1 0x0264
#define FLD_WDMA_URGENT_LOW_U REG_FLD_MSB_LSB(9, 0)
#define FLD_WDMA_URGENT_HIGH_U REG_FLD_MSB_LSB(25, 16)
#define DISP_REG_WDMA_URGENT_CON2 0x0268
#define FLD_WDMA_URGENT_LOW_V REG_FLD_MSB_LSB(9, 0)
#define FLD_WDMA_URGENT_HIGH_V REG_FLD_MSB_LSB(25, 16)
#define DISP_REG_WDMA_VCSEL 0x0F44

#define DISP_REG_WDMA_DST_ADDRX(n) (0x0f00 + 0x04 * (n))
#define DISP_REG_WDMA_DST_ADDRX_MSB(n) (0x0f20 + 0x04 * (n))
#define DISP_REG_WDMA_DST_ADDR_OFFSETX_MSB(n) (0x0f30 + 0x04 * (n))

#define DISP_REG_WDMA_RGB888 0x10
#define DISP_REG_WDMA_DUMMY 0x100

#define MEM_MODE_INPUT_FORMAT_RGB565 0x0U
#define MEM_MODE_INPUT_FORMAT_RGB888 (0x001U << 4)
#define MEM_MODE_INPUT_FORMAT_RGBA8888 (0x002U << 4)
#define MEM_MODE_INPUT_FORMAT_ARGB8888 (0x003U << 4)
#define MEM_MODE_INPUT_FORMAT_UYVY (0x004U << 4)
#define MEM_MODE_INPUT_FORMAT_YUYV (0x005U << 4)
#define MEM_MODE_INPUT_FORMAT_IYUV (0x008U << 4)
#define MEM_MODE_INPUT_FORMAT_ARGB2101010 (0x00BU << 4)
#define MEM_MODE_INPUT_SWAP BIT(16)

/* UFBC WDMA */
#define DISP_REG_UFBC_WDMA_EN 0x00
#define DISP_REG_UFBC_WDMA_INTEN 0x10
#define DISP_REG_UFBC_WDMA_INTSTA 0x14
#define DISP_REG_UFBC_WDMA_SHADOW_CTRL 0x18

#define DISP_REG_UFBC_WDMA_PAYLOAD_OFFSET 0x40
#define DISP_REG_UFBC_WDMA_AFBC_SETTING	0x44
#define UFBC_WDMA_AFBC_YUV_TRANSFORM	BIT(4)

#define DISP_REG_UFBC_WDMA_FMT 0x48
#define UFBC_WDMA_FMT_ABGR8888 2
#define UFBC_WDMA_FMT_ABGR2101010 3

#define DISP_REG_UFBC_WDMA_BG_SIZE 0x50
#define DISP_REG_UFBC_WDMA_TILE_OFFSET 0x54
#define DISP_REG_UFBC_WDMA_TILE_SIZE 0x58
#define DISP_REG_UFBC_WDMA_PREULTRA_D0 0x60
#define DISP_REG_UFBC_WDMA_PREULTRA_H0 0x68
#define DISP_REG_UFBC_WDMA_ULTRA_D0 0x70
#define DISP_REG_UFBC_WDMA_ULTRA_H0 0x78
#define DISP_REG_UFBC_WDMA_URGENT_D0 0x80
#define DISP_REG_UFBC_WDMA_URGENT_H0 0x88
#define DISP_REG_UFBC_WDMA_DMA_CON 0x90
#define DISP_REG_UFBC_WDMA_DST_ADDRX_MSB(n) (0x0f08 + 0x04 * (n))
#define DISP_REG_UFBC_WDMA_ADDR0 0xf00
#define DISP_REG_UFBC_WDMA_ADDR1 0xf04
#define DISP_REG_UFBC_WDMA_ADDR0_MSB 0xf08
#define DISP_REG_UFBC_WDMA_ADDR1_MSB 0xf0c

/* AID offset in mmsys config */
#define MT6983_OVL1_2L_NWCG	0x1401B000
#define MT6983_OVL_DUMMY_REG	(0x200UL)

#define MT6985_OVL1_2L	0x14403000
#define MT6985_OVLSYS_LARB20	0x1460C000
#define MT6985_OVLSYS_LARB0	0x1440C000
#define OVLSYS_WDMA0_PORT		6
#define MT6985_MMSYS_LARB32		0x1401D000
#define MMSYS_WDMA1_PORT		5
#define MT6985_SEC_LARB_OFFSET	0xF80
#define MT6985_OVL_DUMMY_REG	0x200
#define MT6985_OVLSYS_DUMMY_OFFSET	0x400
#define MT6985_OVLSYS_DUMMY1_OFFSET	0x404

#define MT6989_OVLSYS1_WDMA0_AID_MANU 0xB84
#define MT6989_DISP1_AID_SEL_MANUAL 0xB10
#define DISP_WDMA0_AID_SEL_MANUAL	BIT(2)
#define MT6989_DISP1_WDMA0_AID_SETTING 0xB20

#define MT6991_OVLSYS1_WDMA0_AID_MANU 0x000
#define MT6991_DISP1_WDMA1_AID_SETTING 0xB20
#define MT6991_DISP1_AID_SEL_MANUAL 0x10004

/* AID offset in mmsys config */
#define MT6895_WDMA0_AID_SEL	(0xB1CUL)
#define MT6895_WDMA1_AID_SEL	(0xB20UL)

#define MT6886_OVL_DUMMY_REG	(0x200UL)
#define MT6886_WDMA0_AID_SEL	(0xB1CUL)
#define MT6886_WDMA1_AID_SEL	(0xB20UL)

#define MT6879_WDMA0_AID_SEL	(0xB1CUL)
#define MT6879_WDMA1_AID_SEL	(0xB20UL)

#define MT6989_OVLSYS1_WDMA0_AID_SEL	(0xB80UL)
#define MT6991_OVLSYS1_WDMA0_AID_SEL	(0xBD8UL)
	#define L_CON_FLD_AID		REG_FLD_MSB_LSB(2, 0)

#define MT6991_OVLSYS_SEC_OFFSET 0x10000

#define PARSE_FROM_DTS 0xFFFFFFFF

enum GS_WDMA_FLD {
	GS_WDMA_SMI_CON = 0, /* whole reg */
	GS_WDMA_BUF_CON1,    /* whole reg */
	GS_WDMA_UFBC_DMA_CON,/* whole reg */
	GS_WDMA_PRE_ULTRA_LOW_Y,
	GS_WDMA_ULTRA_LOW_Y,
	GS_WDMA_PRE_ULTRA_HIGH_Y,
	GS_WDMA_ULTRA_HIGH_Y,
	GS_WDMA_PRE_ULTRA_LOW_U,
	GS_WDMA_ULTRA_LOW_U,
	GS_WDMA_PRE_ULTRA_HIGH_U,
	GS_WDMA_ULTRA_HIGH_U,
	GS_WDMA_PRE_ULTRA_LOW_V,
	GS_WDMA_ULTRA_LOW_V,
	GS_WDMA_PRE_ULTRA_HIGH_V,
	GS_WDMA_ULTRA_HIGH_V,
	GS_WDMA_PRE_ULTRA_LOW_Y_DVFS,
	GS_WDMA_ULTRA_LOW_Y_DVFS,
	GS_WDMA_PRE_ULTRA_HIGH_Y_DVFS,
	GS_WDMA_ULTRA_HIGH_Y_DVFS,
	GS_WDMA_PRE_ULTRA_LOW_U_DVFS,
	GS_WDMA_ULTRA_LOW_U_DVFS,
	GS_WDMA_PRE_ULTRA_HIGH_U_DVFS,
	GS_WDMA_ULTRA_HIGH_U_DVFS,
	GS_WDMA_PRE_ULTRA_LOW_V_DVFS,
	GS_WDMA_ULTRA_LOW_V_DVFS,
	GS_WDMA_PRE_ULTRA_HIGH_V_DVFS,
	GS_WDMA_ULTRA_HIGH_V_DVFS,
	GS_WDMA_DVFS_EN,
	GS_WDMA_DVFS_TH_Y,
	GS_WDMA_DVFS_TH_U,
	GS_WDMA_DVFS_TH_V,
	GS_WDMA_URGENT_LOW_Y,
	GS_WDMA_URGENT_HIGH_Y,
	GS_WDMA_URGENT_LOW_U,
	GS_WDMA_URGENT_HIGH_U,
	GS_WDMA_URGENT_LOW_V,
	GS_WDMA_URGENT_HIGH_V,
	GS_WDMA_ISSUE_REG_TH_Y,
	GS_WDMA_ISSUE_REG_TH_U,
	GS_WDMA_ISSUE_REG_TH_V,
	GS_WDMA_UFBC_PREULTRA_EN,
	GS_WDMA_UFBC_ULTRA_EN,
	GS_WDMA_UFBC_URGENT_EN,
	GS_WDMA_FLD_NUM,
};

struct mtk_wdma_cfg_info {
	dma_addr_t addr;
	unsigned int width;
	unsigned int height;
	unsigned int fmt;
	unsigned int count;
};

/**
 * struct mtk_disp_wdma - DISP_RDMA driver structure
 * @ddp_comp - structure containing type enum and hardware resources
 * @crtc - associated crtc to report irq events to
 */
struct mtk_disp_wdma {
	struct mtk_ddp_comp ddp_comp;
	const struct mtk_disp_wdma_data *data;
	struct mtk_wdma_cfg_info cfg_info;
	struct mtk_disp_wdma_data *info_data;
	int wdma_sec_first_time_install;
	int wdma_sec_cur_state_chk;
};


bool is_right_wdma_comp_MT6897(struct mtk_ddp_comp *comp)
{
	switch (comp->id) {
	case DDP_COMPONENT_OVLSYS_WDMA0:
		return false;
	case DDP_COMPONENT_OVLSYS_WDMA2:
		return true;
	default:
		DDPPR_ERR("%s invalid wdma module=%d\n", __func__, comp->id);
		return false;
	}
}

static irqreturn_t mtk_wdma_irq_handler(int irq, void *dev_id)
{
	struct mtk_disp_wdma *priv = dev_id;
	struct mtk_ddp_comp *wdma = NULL;
	struct mtk_cwb_info *cwb_info = NULL;
	struct mtk_drm_private *drm_priv = NULL;
	static unsigned long long underrun_old_ts;
	unsigned long long underrun_new_ts = 0;
	bool ufbc = priv->info_data->is_support_ufbc;
	unsigned int reg_insta = ufbc ? DISP_REG_UFBC_WDMA_INTSTA : DISP_REG_WDMA_INTSTA;
	unsigned int buf_idx;
	unsigned int val = 0;
	unsigned int ret = 0;

	if (IS_ERR_OR_NULL(priv))
		return IRQ_NONE;

	wdma = &priv->ddp_comp;
	if (IS_ERR_OR_NULL(wdma))
		return IRQ_NONE;

	if (mtk_drm_top_clk_isr_get(wdma) == false) {
		DDPIRQ("%s, top clk off\n", __func__);
		return IRQ_NONE;
	}

	val = readl(wdma->regs + reg_insta);
	if (!val) {
		ret = IRQ_NONE;
		goto out;
	}
	DRM_MMP_MARK(wdma, wdma->regs_pa, val);

	if (wdma->id == DDP_COMPONENT_WDMA0)
		DRM_MMP_MARK(wdma0, val, 0);
	else if (wdma->id == DDP_COMPONENT_WDMA1)
		DRM_MMP_MARK(wdma1, val, 0);
	else if (wdma->id == DDP_COMPONENT_OVLSYS_WDMA2)
		DRM_MMP_MARK(wdma12, val, 0);
	else if (wdma->id == DDP_COMPONENT_OVLSYS_UFBC_WDMA0)
		DRM_MMP_MARK(ufbc_wdma0, val, 0);
	else if (wdma->id == DDP_COMPONENT_OVLSYS_UFBC_WDMA1)
		DRM_MMP_MARK(ufbc_wdma1, val, 0);

	if (!priv->info_data->is_support_ufbc) {
		if (val & 0x2)
			DRM_MMP_MARK(abnormal_irq, val, wdma->id);
	}
	DDPIRQ("%s irq, val:0x%x\n", mtk_dump_comp_str(wdma), val);

	writel(~val, wdma->regs + reg_insta);

	if (val & BIT(0)) {
		struct mtk_drm_crtc *mtk_crtc = wdma->mtk_crtc;

		DDPIRQ("[IRQ] %s: frame complete!, ufbc:%d\n",
			mtk_dump_comp_str(wdma), ufbc);

		if (mtk_crtc) {
			drm_priv = mtk_crtc->base.dev->dev_private;
			cwb_info = mtk_crtc->cwb_info;
			if (cwb_info && cwb_info->enable &&
				cwb_info->comp->id == wdma->id &&
				drm_priv && !drm_priv->cwb_is_preempted) {
				buf_idx = cwb_info->buf_idx;
				cwb_info->buffer[buf_idx].timestamp = 100;
				atomic_set(&mtk_crtc->cwb_task_active, 1);
				wake_up_interruptible(&mtk_crtc->cwb_wq);
			}
		}
		if (mtk_crtc && mtk_crtc->dc_main_path_commit_task) {
			atomic_set(
				&mtk_crtc->dc_main_path_commit_event, 1);
			wake_up_interruptible(
				&mtk_crtc->dc_main_path_commit_wq);
		}
		MMPathTraceDRM(wdma);
	}
	if ((val & BIT(1)) && !ufbc) {
		DDPPR_ERR("[IRQ] %s: frame underrun!\n",
			  mtk_dump_comp_str(wdma));

		if (wdma->mtk_crtc)
			drm_priv = wdma->mtk_crtc->base.dev->dev_private;
		if (wdma->mtk_crtc && drm_priv &&
			drm_priv->data->mmsys_id == MMSYS_MT6899) {
			mtk_dump_analysis(wdma);
			mtk_dump_reg(wdma);
		}

		underrun_new_ts = sched_clock();
		if (wdma->mtk_crtc && &(wdma->mtk_crtc->base)
			&& (underrun_new_ts - underrun_old_ts > 1000*1000*1000)) { //1s
			mtk_drm_crtc_analysis(&(wdma->mtk_crtc->base));
			mtk_drm_crtc_dump(&(wdma->mtk_crtc->base));
			DDPMSG("new: %llu, old: %llu", underrun_new_ts, underrun_old_ts);
			underrun_old_ts = underrun_new_ts;
			mtk_smi_dbg_hang_detect("wdma-underrun");
		}
	}

	ret = IRQ_HANDLED;

out:
	mtk_drm_top_clk_isr_put(wdma);

	return ret;
}

static inline struct mtk_disp_wdma *comp_to_wdma(struct mtk_ddp_comp *comp)
{
	return container_of(comp, struct mtk_disp_wdma, ddp_comp);
}

resource_size_t mtk_wdma_check_sec_reg_MT6983(struct mtk_ddp_comp *comp)
{
	switch (comp->id) {
	case DDP_COMPONENT_WDMA0:
		return 0;
	case DDP_COMPONENT_WDMA1:
		return MT6983_OVL1_2L_NWCG + MT6983_OVL_DUMMY_REG;
	default:
		return 0;
	}
}

resource_size_t mtk_wdma_check_sec_reg_MT6985(struct mtk_ddp_comp *comp)
{
	switch (comp->id) {
	case DDP_COMPONENT_WDMA1:
	case DDP_COMPONENT_OVLSYS_WDMA0:
	case DDP_COMPONENT_OVLSYS_WDMA1:
	case DDP_COMPONENT_OVLSYS_WDMA3:
		return 0;
	case DDP_COMPONENT_WDMA0: // w/ TDSHP
	case DDP_COMPONENT_OVLSYS_WDMA2: // w/o TDSHP
		return MT6985_OVL1_2L + MT6985_OVL_DUMMY_REG;
	default:
		return 0;
	}
}

resource_size_t mtk_wdma_check_sec_reg_MT6989(struct mtk_ddp_comp *comp)
{
	switch (comp->id) {
	case DDP_COMPONENT_WDMA1:
	case DDP_COMPONENT_OVLSYS_WDMA0:
	case DDP_COMPONENT_OVLSYS_WDMA1:
	case DDP_COMPONENT_OVLSYS_WDMA3:
		return 0;
	case DDP_COMPONENT_WDMA0: // w/ TDSHP
	case DDP_COMPONENT_OVLSYS_WDMA2: // w/o TDSHP
		return MT6985_OVL1_2L + MT6985_OVL_DUMMY_REG;
	default:
		return 0;
	}
}

resource_size_t mtk_wdma_check_sec_reg_MT6897(struct mtk_ddp_comp *comp)
{
	switch (comp->id) {
	case DDP_COMPONENT_WDMA1:
	case DDP_COMPONENT_OVLSYS_WDMA1:
	case DDP_COMPONENT_OVLSYS_WDMA3:
		return 0;
	case DDP_COMPONENT_WDMA0: // w/ TDSHP
	case DDP_COMPONENT_OVLSYS_WDMA0: // w/o TDSHP
		return MT6985_OVL1_2L + MT6985_OVL_DUMMY_REG;
	default:
		return 0;
	}
}

unsigned int mtk_wdma_aid_sel_MT6989(struct mtk_ddp_comp *comp)
{
	switch (comp->id) {
	case DDP_COMPONENT_OVLSYS_WDMA2:
		return MT6989_OVLSYS1_WDMA0_AID_SEL;
	default:
		return 0;
	}
}

unsigned int mtk_wdma_aid_sel_MT6991(struct mtk_ddp_comp *comp)
{
	switch (comp->id) {
	case DDP_COMPONENT_OVLSYS_WDMA2:
		return MT6991_OVLSYS1_WDMA0_AID_SEL;
	default:
		return 0;
	}
}

unsigned int mtk_wdma_aid_sel_MT6895(struct mtk_ddp_comp *comp)
{
	switch (comp->id) {
	case DDP_COMPONENT_WDMA1:
		return MT6895_WDMA1_AID_SEL;
	default:
		return 0;
	}
}

unsigned int mtk_wdma_hrt_channel_MT6991(struct mtk_ddp_comp *comp)
{
	switch (comp->id) {
	case DDP_COMPONENT_OVLSYS_WDMA2:
		return 3;
	case DDP_COMPONENT_WDMA0:
	case DDP_COMPONENT_OVLSYS_WDMA1:
		return 7;
	case DDP_COMPONENT_OVLSYS_WDMA0:
		return 11;
	case DDP_COMPONENT_WDMA1:
	case DDP_COMPONENT_WDMA2:
	case DDP_COMPONENT_WDMA3:
	case DDP_COMPONENT_WDMA4:
	case DDP_COMPONENT_OVLSYS_WDMA3:
		return 15;
	default:
		return 0;
	}
}


resource_size_t mtk_wdma_check_sec_reg_MT6886(struct mtk_ddp_comp *comp)
{
	struct mtk_ddp_comp *comp_sec;
	resource_size_t base;
	struct mtk_drm_private *priv = comp->mtk_crtc->base.dev->dev_private;
	switch (comp->id) {
	case DDP_COMPONENT_WDMA0:
		return 0;
	case DDP_COMPONENT_WDMA1:
		comp_sec = priv->ddp_comp[DDP_COMPONENT_OVL0_2L];
		base = comp_sec->regs_pa;
		return base + MT6886_OVL_DUMMY_REG;
	default:
		return 0;
	}
}

unsigned int mtk_wdma_aid_sel_MT6879(struct mtk_ddp_comp *comp)
{
	switch (comp->id) {
	case DDP_COMPONENT_WDMA1:
		return MT6879_WDMA1_AID_SEL;
	default:
		return 0;
	}
}

static int mtk_wdma_store_sec_state(struct mtk_disp_wdma *wdma, int state)
{
	int first_time_initial = 0;

	if (wdma->wdma_sec_first_time_install) {
		wdma->wdma_sec_first_time_install = 0;
		first_time_initial = 1;
	}

	if (wdma->wdma_sec_cur_state_chk != state || first_time_initial) {
		wdma->wdma_sec_cur_state_chk = state;
		return 1;
	}

	return 0;
}

static void mtk_wdma_start(struct mtk_ddp_comp *comp, struct cmdq_pkt *handle)
{
	struct mtk_disp_wdma *wdma = comp_to_wdma(comp);
	const struct mtk_disp_wdma_data *data = wdma->data;
	unsigned int inten;
	bool en = 1;
	unsigned int aid_sel_offset = 0;
	struct mtk_drm_private *priv = comp->mtk_crtc->base.dev->dev_private;
	resource_size_t mmsys_reg = priv->config_regs_pa;
	int crtc_idx = drm_crtc_index(&comp->mtk_crtc->base);
	struct mtk_ddp_comp *output_comp;

	if (wdma->info_data->is_support_ufbc) {
		inten = REG_FLD_VAL(INTEN_FLD_FME_CPL_INTEN, 1);
		mtk_ddp_write(comp, WDMA_EN, DISP_REG_UFBC_WDMA_EN, handle);
		mtk_ddp_write(comp, inten, DISP_REG_UFBC_WDMA_INTEN, handle);
	} else {
		inten = REG_FLD_VAL(INTEN_FLD_FME_CPL_INTEN, 1) |
			REG_FLD_VAL(INTEN_FLD_FME_UND_INTEN, 1);
		mtk_ddp_write(comp, WDMA_EN, DISP_REG_WDMA_EN, handle);
		mtk_ddp_write(comp, inten, DISP_REG_WDMA_INTEN, handle);
	}

	output_comp = mtk_ddp_comp_request_output(comp->mtk_crtc);
	if (unlikely(!output_comp)) {
		DDPPR_ERR("%s:invalid output comp\n", __func__);
		return;
	}

	if (data && data->use_larb_control_sec && crtc_idx == 2) {
		if (disp_sec_cb.cb != NULL) {
			void __iomem *ovl0_2l_reg_addr;
			void __iomem *ovl0_2l_reg_addr1;

			switch (priv->data->mmsys_id) {
			case MMSYS_MT6985:
			case MMSYS_MT6897:
				ovl0_2l_reg_addr =
					comp->mtk_crtc->ovlsys0_regs + MT6985_OVLSYS_DUMMY_OFFSET;
				ovl0_2l_reg_addr1 =
					comp->mtk_crtc->ovlsys0_regs + MT6985_OVLSYS_DUMMY1_OFFSET;
				if (output_comp->id == DDP_COMPONENT_WDMA0)
					writel(MT6985_MMSYS_LARB32 + MT6985_SEC_LARB_OFFSET
						+ (MMSYS_WDMA1_PORT * 0x4), ovl0_2l_reg_addr);
				else if (output_comp->id == DDP_COMPONENT_OVLSYS_WDMA2)
					writel(MT6985_OVLSYS_LARB20 + MT6985_SEC_LARB_OFFSET
						+ (OVLSYS_WDMA0_PORT * 0x4), ovl0_2l_reg_addr);
				else if (output_comp->id == DDP_COMPONENT_OVLSYS_WDMA0) {
					writel(MT6985_OVLSYS_LARB0 + MT6985_SEC_LARB_OFFSET
						+ (OVLSYS_WDMA0_PORT * 0x4), ovl0_2l_reg_addr1);
					writel(MT6985_OVLSYS_LARB20 + MT6985_SEC_LARB_OFFSET
						+ (OVLSYS_WDMA0_PORT * 0x4), ovl0_2l_reg_addr);
				}
				break;
			default:
				DDPMSG("Not multi condition platform\n");
			}
			if (disp_sec_cb.cb(DISP_SEC_START, NULL, 0, NULL))
				wdma->wdma_sec_first_time_install = 1;
		}
	} else {
		if (data && data->aid_sel) {
			aid_sel_offset = data->aid_sel(comp);
			if (priv->data->mmsys_id == MMSYS_MT6989 ||
				priv->data->mmsys_id == MMSYS_MT6899)
				mmsys_reg = priv->ovlsys1_regs_pa;
			else if (priv->data->mmsys_id == MMSYS_MT6991)
				mmsys_reg = priv->ovlsys1_regs_pa + MT6991_OVLSYS_SEC_OFFSET;
		}
		if (aid_sel_offset) {
			if (priv->data->mmsys_id == MMSYS_MT6989 ||
				priv->data->mmsys_id == MMSYS_MT6899)
				cmdq_pkt_write(handle, comp->cmdq_base,
					mmsys_reg + MT6989_OVLSYS1_WDMA0_AID_MANU, BIT(0), BIT(0));
			else if (priv->data->mmsys_id == MMSYS_MT6991) {
				cmdq_pkt_write(handle, comp->cmdq_base,
					mmsys_reg + MT6991_OVLSYS1_WDMA0_AID_MANU, BIT(0), BIT(0));
			} else
				cmdq_pkt_write(handle, comp->cmdq_base,
					mmsys_reg + aid_sel_offset, BIT(1), BIT(1));
		}
	}

	if (data && data->sodi_config)
		data->sodi_config(comp->mtk_crtc->base.dev, comp->id, handle,
				  &en);
}

static void mtk_wdma_stop(struct mtk_ddp_comp *comp, struct cmdq_pkt *handle)
{
	struct mtk_disp_wdma *wdma = comp_to_wdma(comp);
	const struct mtk_disp_wdma_data *data = wdma->data;
	bool en = 0;
	int crtc_idx = drm_crtc_index(&comp->mtk_crtc->base);
	struct mtk_drm_private *priv = comp->mtk_crtc->base.dev->dev_private;

	if (wdma->info_data->is_support_ufbc) {
		mtk_ddp_write(comp, 0, DISP_REG_UFBC_WDMA_EN, handle);
		if (priv && !mtk_drm_helper_get_opt(priv->helper_opt,
				   MTK_DRM_OPT_IDLEMGR_DISABLE_ROUTINE_IRQ))
			mtk_ddp_write(comp, 0, DISP_REG_UFBC_WDMA_INTEN, handle);
		comp->qos_bw = 0;
		comp->fbdc_bw = 0;
		comp->hrt_bw = 0;
	} else {
		mtk_ddp_write(comp, 0x0, DISP_REG_WDMA_INTEN, handle);
		mtk_ddp_write(comp, 0x0, DISP_REG_WDMA_EN, handle);
		mtk_ddp_write(comp, 0x0, DISP_REG_WDMA_INTSTA, handle);
		mtk_ddp_write(comp, 0x01, DISP_REG_WDMA_RST, handle);
		mtk_ddp_write(comp, 0x00, DISP_REG_WDMA_RST, handle);
	}

	if (data && data->sodi_config)
		data->sodi_config(comp->mtk_crtc->base.dev,
			comp->id, handle, &en);
	if (data && data->use_larb_control_sec && crtc_idx == 2) {
		if (disp_sec_cb.cb != NULL)
			disp_sec_cb.cb(DISP_SEC_STOP, NULL, 0, NULL);
	}
}

static int mtk_wdma_is_busy(struct mtk_ddp_comp *comp)
{
	int ret, tmp;

	/* wdma status can be referenced to wdma_get_state() or CODA */
	tmp = readl(comp->regs + DISP_REG_WDMA_FLOW_CTRL_DBG);
	ret = (REG_FLD_VAL_GET(FLOW_CTRL_DBG_FLD_WDMA_STA_FLOW_CTRL,
				tmp) != 0x1) ? 1 : 0;

	DDPINFO("%s:%d is:%d regs:0x%x\n", __func__, __LINE__, ret, tmp);

	return ret;
}

static void mtk_wdma_prepare(struct mtk_ddp_comp *comp)
{
	struct mtk_disp_wdma *wdma = comp_to_wdma(comp);
	unsigned int offset = wdma->info_data->is_support_ufbc ?
			DISP_REG_UFBC_WDMA_SHADOW_CTRL : DISP_REG_WDMA_SHADOW_CTRL;
	unsigned int val = wdma->data->need_bypass_shadow ?
			WDMA_BYPASS_SHADOW : 0;

	mtk_ddp_comp_clk_prepare(comp);

	/* Bypass shadow register and read shadow register */
	mtk_ddp_write_mask_cpu(comp, val, offset, WDMA_BYPASS_SHADOW);

}

static void mtk_wdma_unprepare(struct mtk_ddp_comp *comp)
{
	mtk_ddp_comp_clk_unprepare(comp);
}

static void mtk_wdma_calc_golden_setting(struct golden_setting_context *gsc,
					 struct mtk_ddp_comp *comp,
					 unsigned int is_primary_flag,
					 unsigned int *gs)
{
	struct mtk_disp_wdma *wdma = comp_to_wdma(comp);
	struct mtk_drm_private *priv = comp->mtk_crtc->base.dev->dev_private;
	unsigned int format = comp->fb->format->format;
	unsigned int preultra_low_us = 7, preultra_high_us = 6;
	unsigned int ultra_low_us = 6, ultra_high_us = 4;
	unsigned int dvfs_offset = 2;
	unsigned int urgent_low_offset = 4, urgent_high_offset = 3;
	unsigned int Bpp = 3;
	unsigned int FP = 100;
	unsigned int res = 0;
	unsigned int frame_rate = 0;
	unsigned long long consume_rate = 0;
	unsigned int fifo_size = wdma->info_data->fifo_size_1plane;
	unsigned int fifo_size_uv = wdma->info_data->fifo_size_uv_1plane;
	unsigned int fifo;
	unsigned int factor1 = 4;
	unsigned int factor2 = 4;
	unsigned int tmp, field;

	if (gsc->vrefresh == 0 || priv->data->mmsys_id == MMSYS_MT6886)
		frame_rate = 60;
	else
		frame_rate = gsc->vrefresh;
	res = gsc->dst_width * gsc->dst_height;

	consume_rate = (unsigned long long)res * frame_rate;
	do_div(consume_rate, 1000);
	consume_rate *= 125; /* PF = 100 */
	do_div(consume_rate, 16 * 1000);

	DDPINFO("%s vrefresh:%d, %dx%d, consume_rate:%lld\n", __func__,
		gsc->vrefresh, gsc->dst_width, gsc->dst_height, consume_rate);

	if (wdma->info_data->is_support_ufbc) {
		gs[GS_WDMA_UFBC_DMA_CON] = 0xf0;

		Bpp = 4;
		/* For UFBC_WDMA, Y represent Payload, U represent Header */
		tmp = DIV_ROUND_UP(consume_rate * Bpp * preultra_low_us, FP);
		gs[GS_WDMA_PRE_ULTRA_LOW_Y] = (fifo_size > tmp) ? (fifo_size - tmp) : 1;

		tmp = DIV_ROUND_UP(consume_rate * Bpp * preultra_high_us, FP);
		gs[GS_WDMA_PRE_ULTRA_HIGH_Y] =
			(fifo_size > tmp) ? (fifo_size - tmp) : 1;

		tmp = DIV_ROUND_UP(consume_rate * Bpp * preultra_low_us, FP * 1024);
		gs[GS_WDMA_PRE_ULTRA_LOW_U] = (fifo_size_uv > tmp) ? (fifo_size_uv - tmp) : 1;

		tmp = DIV_ROUND_UP(consume_rate * Bpp * preultra_high_us, FP * 1024);
		gs[GS_WDMA_PRE_ULTRA_HIGH_U] = (fifo_size_uv > tmp) ? (fifo_size_uv - tmp) : 1;
		gs[GS_WDMA_UFBC_PREULTRA_EN] = 1;
		gs[GS_WDMA_UFBC_ULTRA_EN] = 0;
		gs[GS_WDMA_UFBC_URGENT_EN] = 0;
		return;
	}

	/* WDMA_SMI_CON */
	if (format == DRM_FORMAT_YVU420 || format == DRM_FORMAT_YUV420)
		gs[GS_WDMA_SMI_CON] = 0x11140007;
	else
		gs[GS_WDMA_SMI_CON] = 0x12240007;

	/* WDMA_BUF_CON1 */
	if (!gsc->is_dc)
		gs[GS_WDMA_BUF_CON1] = 0xD4000000;
	else
		gs[GS_WDMA_BUF_CON1] = 0x40000000;

	switch (format) {
	case DRM_FORMAT_YVU420:
	case DRM_FORMAT_YUV420:
		/* 3 plane */
		fifo_size = wdma->info_data->fifo_size_3plane;
		fifo_size_uv = wdma->info_data->fifo_size_uv_3plane;
		fifo = fifo_size_uv;
		factor1 = 4;
		factor2 = 4;
		Bpp = 1;

		break;
	case DRM_FORMAT_NV12:
	case DRM_FORMAT_NV21:
		/* 2 plane */
		fifo_size = wdma->info_data->fifo_size_2plane;
		fifo_size_uv = wdma->info_data->fifo_size_uv_2plane;
		fifo = fifo_size_uv;
		factor1 = 2;
		factor2 = 4;
		Bpp = 1;

		break;
	default:
		/* 1 plane */
		/* fifo_size keep default */
		/* Bpp keep default */
		factor1 = 4;
		factor2 = 4;
		fifo = fifo_size / 4;

		break;
	}

	field = BUF_CON1_FLD_FIFO_PSEUDO_SIZE;
	tmp = REG_FLD_VAL(field, fifo_size);
	if (wdma->info_data->buf_con1_fld_fifo_pseudo_size)
		field = wdma->info_data->buf_con1_fld_fifo_pseudo_size;
	fifo_size = REG_FLD_VAL(field, fifo_size);

	field = BUF_CON1_FLD_FIFO_PSEUDO_SIZE_UV;
	tmp += REG_FLD_VAL(field, fifo_size_uv);
	if (wdma->info_data->buf_con1_fld_fifo_pseudo_size_uv)
		field = wdma->info_data->buf_con1_fld_fifo_pseudo_size_uv;
	fifo_size_uv = REG_FLD_VAL(field, fifo_size_uv);

	tmp += gs[GS_WDMA_BUF_CON1];
	gs[GS_WDMA_BUF_CON1] += fifo_size_uv + fifo_size;

	/* WDMA_BUF_CON5 */
	tmp = DO_DIV_ROUND_UP(consume_rate * Bpp * preultra_low_us, FP);
	gs[GS_WDMA_PRE_ULTRA_LOW_Y] = (fifo_size > tmp) ? (fifo_size - tmp) : 1;

	tmp = DO_DIV_ROUND_UP(consume_rate * Bpp * ultra_low_us, FP);
	gs[GS_WDMA_ULTRA_LOW_Y] = (fifo_size > tmp) ? (fifo_size - tmp) : 1;

	/* WDMA_BUF_CON6 */
	tmp = DO_DIV_ROUND_UP(consume_rate * Bpp * preultra_high_us, FP);
	gs[GS_WDMA_PRE_ULTRA_HIGH_Y] =
		(fifo_size > tmp) ? (fifo_size - tmp) : 1;

	tmp = DO_DIV_ROUND_UP(consume_rate * Bpp * ultra_high_us, FP);
	gs[GS_WDMA_ULTRA_HIGH_Y] = (fifo_size > tmp) ? (fifo_size - tmp) : 1;

	/* WDMA_BUF_CON7 */
	tmp = DO_DIV_ROUND_UP(consume_rate * preultra_low_us, FP * factor1);
	gs[GS_WDMA_PRE_ULTRA_LOW_U] = (fifo > tmp) ? (fifo - tmp) : 1;

	tmp = DO_DIV_ROUND_UP(consume_rate * ultra_low_us, FP * factor1);
	gs[GS_WDMA_ULTRA_LOW_U] = (fifo > tmp) ? (fifo - tmp) : 1;

	/* WDMA_BUF_CON8 */
	tmp = DO_DIV_ROUND_UP(consume_rate * preultra_high_us, FP * factor1);
	gs[GS_WDMA_PRE_ULTRA_HIGH_U] = (fifo > tmp) ? (fifo - tmp) : 1;

	tmp = DO_DIV_ROUND_UP(consume_rate * ultra_high_us, FP * factor1);
	gs[GS_WDMA_ULTRA_HIGH_U] = (fifo > tmp) ? (fifo - tmp) : 1;

	/* WDMA_BUF_CON9 */
	tmp = DO_DIV_ROUND_UP(consume_rate * preultra_low_us, FP * factor2);
	gs[GS_WDMA_PRE_ULTRA_LOW_V] = (fifo > tmp) ? (fifo - tmp) : 1;

	tmp = DO_DIV_ROUND_UP(consume_rate * ultra_low_us, FP * factor2);
	gs[GS_WDMA_ULTRA_LOW_V] = (fifo > tmp) ? (fifo - tmp) : 1;

	/* WDMA_BUF_CON10 */
	tmp = DO_DIV_ROUND_UP(consume_rate * preultra_high_us, FP * factor2);
	gs[GS_WDMA_PRE_ULTRA_HIGH_V] = (fifo > tmp) ? (fifo - tmp) : 1;

	tmp = DO_DIV_ROUND_UP(consume_rate * ultra_high_us, FP * factor2);
	gs[GS_WDMA_ULTRA_HIGH_V] = (fifo > tmp) ? (fifo - tmp) : 1;

	/* WDMA_BUF_CON11 */
	tmp = DO_DIV_ROUND_UP(consume_rate * Bpp * (preultra_low_us + dvfs_offset),
			   FP);
	gs[GS_WDMA_PRE_ULTRA_LOW_Y_DVFS] =
		(fifo_size > tmp) ? (fifo_size - tmp) : 1;
	tmp = DO_DIV_ROUND_UP(consume_rate * Bpp * (ultra_low_us + dvfs_offset),
			   FP);
	gs[GS_WDMA_ULTRA_LOW_Y_DVFS] =
		(fifo_size > tmp) ? (fifo_size - tmp) : 1;

	/* WDMA_BUF_CON12 */
	tmp = DO_DIV_ROUND_UP(
		consume_rate * Bpp * (preultra_high_us + dvfs_offset), FP);
	gs[GS_WDMA_PRE_ULTRA_HIGH_Y_DVFS] =
		(fifo_size > tmp) ? (fifo_size - tmp) : 1;
	tmp = DO_DIV_ROUND_UP(consume_rate * Bpp * (ultra_high_us + dvfs_offset),
			   FP);
	gs[GS_WDMA_ULTRA_HIGH_Y_DVFS] =
		(fifo_size > tmp) ? (fifo_size - tmp) : 1;

	/* WDMA_BUF_CON13 */
	tmp = DO_DIV_ROUND_UP(consume_rate * (preultra_low_us + dvfs_offset),
			   FP * factor1);
	gs[GS_WDMA_PRE_ULTRA_LOW_U_DVFS] = (fifo > tmp) ? (fifo - tmp) : 1;

	tmp = DO_DIV_ROUND_UP(consume_rate * (ultra_low_us + dvfs_offset),
			   FP * factor1);
	gs[GS_WDMA_ULTRA_LOW_U_DVFS] = (fifo > tmp) ? (fifo - tmp) : 1;

	/* WDMA_BUF_CON14 */
	tmp = DO_DIV_ROUND_UP(consume_rate * (preultra_high_us + dvfs_offset),
			   FP * factor1);
	gs[GS_WDMA_PRE_ULTRA_HIGH_U_DVFS] = (fifo > tmp) ? (fifo - tmp) : 1;

	tmp = DO_DIV_ROUND_UP(consume_rate * (ultra_high_us + dvfs_offset),
			   FP * factor1);
	gs[GS_WDMA_ULTRA_HIGH_U_DVFS] = (fifo > tmp) ? (fifo - tmp) : 1;

	/* WDMA_BUF_CON15 */
	tmp = DO_DIV_ROUND_UP(consume_rate * (preultra_low_us + dvfs_offset),
			   FP * factor2);
	gs[GS_WDMA_PRE_ULTRA_LOW_V_DVFS] = (fifo > tmp) ? (fifo - tmp) : 1;

	tmp = DO_DIV_ROUND_UP(consume_rate * (ultra_low_us + dvfs_offset),
			   FP * factor2);
	gs[GS_WDMA_ULTRA_LOW_V_DVFS] = (fifo > tmp) ? (fifo - tmp) : 1;

	/* WDMA_BUF_CON16 */
	tmp = DO_DIV_ROUND_UP(consume_rate * (preultra_high_us + dvfs_offset),
			   FP * factor2);
	gs[GS_WDMA_PRE_ULTRA_HIGH_V_DVFS] = (fifo > tmp) ? (fifo - tmp) : 1;

	tmp = DO_DIV_ROUND_UP(consume_rate * (ultra_high_us + dvfs_offset),
			   FP * factor2);
	gs[GS_WDMA_ULTRA_HIGH_V_DVFS] = (fifo > tmp) ? (fifo - tmp) : 1;

	/* WDMA_BUF_CON17 */
	gs[GS_WDMA_DVFS_EN] = 1;
	gs[GS_WDMA_DVFS_TH_Y] = gs[GS_WDMA_ULTRA_HIGH_Y_DVFS];

	/* WDMA_BUF_CON18 */
	gs[GS_WDMA_DVFS_TH_U] = gs[GS_WDMA_ULTRA_HIGH_U_DVFS];
	gs[GS_WDMA_DVFS_TH_V] = gs[GS_WDMA_ULTRA_HIGH_V_DVFS];

	/* WDMA URGENT CONTROL 0 */
	tmp = DO_DIV_ROUND_UP(consume_rate * Bpp * urgent_low_offset, FP);
	gs[GS_WDMA_URGENT_LOW_Y] = (fifo_size > tmp) ? (fifo_size - tmp) : 1;

	tmp = DO_DIV_ROUND_UP(consume_rate * Bpp * urgent_high_offset, FP);
	gs[GS_WDMA_URGENT_HIGH_Y] = (fifo_size > tmp) ? (fifo_size - tmp) : 1;

	/* WDMA URGENT CONTROL 1 */
	tmp = DO_DIV_ROUND_UP(consume_rate * urgent_low_offset, FP * factor1);
	gs[GS_WDMA_URGENT_LOW_U] = (fifo > tmp) ? (fifo - tmp) : 1;

	tmp = DO_DIV_ROUND_UP(consume_rate * urgent_high_offset, FP * factor1);
	gs[GS_WDMA_URGENT_HIGH_U] = (fifo > tmp) ? (fifo - tmp) : 1;

	/* WDMA URGENT CONTROL 2 */
	tmp = DO_DIV_ROUND_UP(consume_rate * urgent_low_offset, FP * factor2);
	gs[GS_WDMA_URGENT_LOW_V] = (fifo > tmp) ? (fifo - tmp) : 1;

	tmp = DO_DIV_ROUND_UP(consume_rate * urgent_high_offset, FP * factor2);
	gs[GS_WDMA_URGENT_HIGH_V] = (fifo > tmp) ? (fifo - tmp) : 1;

	/* WDMA Buf Constant 3 */
	gs[GS_WDMA_ISSUE_REG_TH_Y] = 16;
	gs[GS_WDMA_ISSUE_REG_TH_U] = 16;

	/* WDMA Buf Constant 4 */
	gs[GS_WDMA_ISSUE_REG_TH_V] = 16;
}

static void mtk_wdma_golden_setting(struct mtk_ddp_comp *comp,
				    struct golden_setting_context *gsc,
				    struct cmdq_pkt *handle)
{
	struct mtk_disp_wdma *wdma = comp_to_wdma(comp);
	unsigned int gs[GS_WDMA_FLD_NUM];
	unsigned int value = 0;

	if (!gsc) {
		DDPPR_ERR("golden setting is null, %s,%d\n", __FILE__,
			  __LINE__);
		return;
	}
	mtk_wdma_calc_golden_setting(gsc, comp, true, gs);

#ifdef IF_ZERO
	mtk_ddp_write(comp, 0x800000ff, 0x2C, handle);

	mtk_ddp_write(comp, 0xd4000529, 0x38, handle);

	mtk_ddp_write(comp, 0x00640043, 0x200, handle);
	mtk_ddp_write(comp, 0x00a50064, 0x204, handle);
	mtk_ddp_write(comp, 0x00390036, 0x208, handle);
	mtk_ddp_write(comp, 0x003f0039, 0x20C, handle);
	mtk_ddp_write(comp, 0x00390036, 0x210, handle);
	mtk_ddp_write(comp, 0x003f0039, 0x214, handle);
	mtk_ddp_write(comp, 0x00220001, 0x218, handle);
	mtk_ddp_write(comp, 0x00640022, 0x21C, handle);
	mtk_ddp_write(comp, 0x00340031, 0x220, handle);
	mtk_ddp_write(comp, 0x00390034, 0x224, handle);
	mtk_ddp_write(comp, 0x00340031, 0x228, handle);
	mtk_ddp_write(comp, 0x00390034, 0x22C, handle);

	mtk_ddp_write(comp, 0x00640001, 0x230, handle);
	mtk_ddp_write(comp, 0x00390039, 0x234, handle);

	mtk_ddp_write(comp, 0x00300000, 0x250, handle);
	mtk_ddp_write(comp, 0x00300030, 0x254, handle);
	mtk_ddp_write(comp, 0x00300000, 0x258, handle);
	mtk_ddp_write(comp, 0x00300030, 0x25C, handle);

	mtk_ddp_write(comp, 165 | (198 << 16), 0x260, handle);
	mtk_ddp_write(comp, 63 | (65 << 16), 0x264, handle);
	mtk_ddp_write(comp, 63 | (65 << 16), 0x268, handle);
#else
	if (wdma->info_data->is_support_ufbc) {
		value = gs[GS_WDMA_UFBC_DMA_CON];
		mtk_ddp_write(comp, value, DISP_REG_UFBC_WDMA_DMA_CON, handle);

		value = gs[GS_WDMA_PRE_ULTRA_LOW_Y] + (gs[GS_WDMA_PRE_ULTRA_HIGH_Y] << 16) +
			(gs[GS_WDMA_UFBC_PREULTRA_EN] << 31);
		mtk_ddp_write(comp, value, DISP_REG_UFBC_WDMA_PREULTRA_D0, handle);

		value = gs[GS_WDMA_PRE_ULTRA_LOW_U] + (gs[GS_WDMA_PRE_ULTRA_HIGH_U] << 16);
		mtk_ddp_write(comp, value, DISP_REG_UFBC_WDMA_PREULTRA_H0, handle);

		value = gs[GS_WDMA_ULTRA_LOW_Y] + (gs[GS_WDMA_ULTRA_HIGH_Y] << 16) +
			(gs[GS_WDMA_UFBC_ULTRA_EN] << 31);
		mtk_ddp_write(comp, value, DISP_REG_UFBC_WDMA_ULTRA_D0, handle);

		value = gs[GS_WDMA_ULTRA_LOW_U] + (gs[GS_WDMA_ULTRA_HIGH_U] << 16);
		mtk_ddp_write(comp, value, DISP_REG_UFBC_WDMA_ULTRA_H0, handle);

		value = gs[GS_WDMA_URGENT_LOW_Y] + (gs[GS_WDMA_URGENT_HIGH_Y] << 16) +
			(gs[GS_WDMA_UFBC_URGENT_EN] << 31);
		mtk_ddp_write(comp, value, DISP_REG_UFBC_WDMA_URGENT_D0, handle);

		value = gs[GS_WDMA_URGENT_LOW_U] + (gs[GS_WDMA_URGENT_HIGH_U] << 16);
		mtk_ddp_write(comp, value, DISP_REG_UFBC_WDMA_URGENT_H0, handle);

		DDPINFO("DMA_CON:0x%x, PREULTRA:%d~%d,%d~%d, ULTRA:%d~%d,%d~%d, URGENT:%d~%d,%d~%d\n",
			gs[GS_WDMA_UFBC_DMA_CON],
			gs[GS_WDMA_PRE_ULTRA_LOW_Y], gs[GS_WDMA_PRE_ULTRA_HIGH_Y],
			gs[GS_WDMA_PRE_ULTRA_LOW_U], gs[GS_WDMA_PRE_ULTRA_HIGH_U],
			gs[GS_WDMA_ULTRA_LOW_Y], gs[GS_WDMA_ULTRA_HIGH_Y],
			gs[GS_WDMA_ULTRA_LOW_U], gs[GS_WDMA_ULTRA_HIGH_U],
			gs[GS_WDMA_URGENT_LOW_Y], gs[GS_WDMA_URGENT_HIGH_Y],
			gs[GS_WDMA_URGENT_LOW_U], gs[GS_WDMA_URGENT_HIGH_U]);
		return;
	}
	/* WDMA_SMI_CON */
	value = gs[GS_WDMA_SMI_CON];
	mtk_ddp_write(comp, value, DISP_REG_WDMA_SMI_CON, handle);
	// DISP_REG_SET(cmdq, offset + DISP_REG_WDMA_SMI_CON, value);

	/* WDMA_BUF_CON1 */
	value = gs[GS_WDMA_BUF_CON1];
	mtk_ddp_write(comp, value, DISP_REG_WDMA_BUF_CON1, handle);
	//	DISP_REG_SET(cmdq, offset + DISP_REG_WDMA_BUF_CON1, value);

	/* WDMA BUF CONST 5 */
	value = gs[GS_WDMA_PRE_ULTRA_LOW_Y] + (gs[GS_WDMA_ULTRA_LOW_Y] << 16);
	mtk_ddp_write(comp, value, DISP_REG_WDMA_BUF_CON5, handle);
	// DISP_REG_SET(cmdq, offset + DISP_REG_WDMA_BUF_CON5, value);

	/* WDMA BUF CONST 6 */
	value = gs[GS_WDMA_PRE_ULTRA_HIGH_Y] + (gs[GS_WDMA_ULTRA_HIGH_Y] << 16);
	mtk_ddp_write(comp, value, DISP_REG_WDMA_BUF_CON6, handle);
	// DISP_REG_SET(cmdq, offset + DISP_REG_WDMA_BUF_CON6, value);

	/* WDMA BUF CONST 7 */
	value = gs[GS_WDMA_PRE_ULTRA_LOW_U] + (gs[GS_WDMA_ULTRA_LOW_U] << 16);
	mtk_ddp_write(comp, value, DISP_REG_WDMA_BUF_CON7, handle);
	// DISP_REG_SET(cmdq, offset + DISP_REG_WDMA_BUF_CON7, value);

	/* WDMA BUF CONST 8 */
	value = gs[GS_WDMA_PRE_ULTRA_HIGH_U] + (gs[GS_WDMA_ULTRA_HIGH_U] << 16);
	mtk_ddp_write(comp, value, DISP_REG_WDMA_BUF_CON8, handle);
	// DISP_REG_SET(cmdq, offset + DISP_REG_WDMA_BUF_CON8, value);

	/* WDMA BUF CONST 9 */
	value = gs[GS_WDMA_PRE_ULTRA_LOW_V] + (gs[GS_WDMA_ULTRA_LOW_V] << 16);
	mtk_ddp_write(comp, value, DISP_REG_WDMA_BUF_CON9, handle);
	// DISP_REG_SET(cmdq, offset + DISP_REG_WDMA_BUF_CON9, value);

	/* WDMA BUF CONST 10 */
	value = gs[GS_WDMA_PRE_ULTRA_HIGH_V] + (gs[GS_WDMA_ULTRA_HIGH_V] << 16);
	mtk_ddp_write(comp, value, DISP_REG_WDMA_BUF_CON10, handle);

	/* WDMA BUF CONST 11 */
	value = gs[GS_WDMA_PRE_ULTRA_LOW_Y_DVFS] +
		(gs[GS_WDMA_ULTRA_LOW_Y_DVFS] << 16);
	mtk_ddp_write(comp, value, DISP_REG_WDMA_BUF_CON11, handle);

	/* WDMA BUF CONST 12 */
	value = gs[GS_WDMA_PRE_ULTRA_HIGH_Y_DVFS] +
		(gs[GS_WDMA_ULTRA_HIGH_Y_DVFS] << 16);
	mtk_ddp_write(comp, value, DISP_REG_WDMA_BUF_CON12, handle);

	/* WDMA BUF CONST 13 */
	value = gs[GS_WDMA_PRE_ULTRA_LOW_U_DVFS] +
		(gs[GS_WDMA_ULTRA_LOW_U_DVFS] << 16);
	mtk_ddp_write(comp, value, DISP_REG_WDMA_BUF_CON13, handle);

	/* WDMA BUF CONST 14 */
	value = gs[GS_WDMA_PRE_ULTRA_HIGH_U_DVFS] +
		(gs[GS_WDMA_ULTRA_HIGH_U_DVFS] << 16);
	mtk_ddp_write(comp, value, DISP_REG_WDMA_BUF_CON14, handle);

	/* WDMA BUF CONST 15 */
	value = gs[GS_WDMA_PRE_ULTRA_LOW_V_DVFS] +
		(gs[GS_WDMA_ULTRA_LOW_V_DVFS] << 16);
	mtk_ddp_write(comp, value, DISP_REG_WDMA_BUF_CON15, handle);

	/* WDMA BUF CONST 16 */
	value = gs[GS_WDMA_PRE_ULTRA_HIGH_V_DVFS] +
		(gs[GS_WDMA_ULTRA_HIGH_V_DVFS] << 16);
	mtk_ddp_write(comp, value, DISP_REG_WDMA_BUF_CON16, handle);

	/* WDMA BUF CONST 17 */
	value = gs[GS_WDMA_DVFS_EN] + (gs[GS_WDMA_DVFS_TH_Y] << 16);
	mtk_ddp_write(comp, value, DISP_REG_WDMA_BUF_CON17, handle);

	/* WDMA BUF CONST 18 */
	value = gs[GS_WDMA_DVFS_TH_U] + (gs[GS_WDMA_DVFS_TH_V] << 16);
	mtk_ddp_write(comp, value, DISP_REG_WDMA_BUF_CON18, handle);

	/* WDMA URGENT CON0 */
	value = gs[GS_WDMA_URGENT_LOW_Y] + (gs[GS_WDMA_URGENT_HIGH_Y] << 16);
	mtk_ddp_write(comp, value, DISP_REG_WDMA_URGENT_CON0, handle);

	/* WDMA URGENT CON1 */
	value = gs[GS_WDMA_URGENT_LOW_U] + (gs[GS_WDMA_URGENT_HIGH_U] << 16);
	mtk_ddp_write(comp, value, DISP_REG_WDMA_URGENT_CON1, handle);

	/* WDMA URGENT CON2 */
	value = gs[GS_WDMA_URGENT_LOW_V] + (gs[GS_WDMA_URGENT_HIGH_V] << 16);
	mtk_ddp_write(comp, value, DISP_REG_WDMA_URGENT_CON2, handle);

	/* WDMA_BUF_CON3 */
	value = gs[GS_WDMA_ISSUE_REG_TH_Y] + (gs[GS_WDMA_ISSUE_REG_TH_U] << 16);
	mtk_ddp_write(comp, value, DISP_REG_WDMA_BUF_CON3, handle);

	/* WDMA_BUF_CON4 */
	value = gs[GS_WDMA_ISSUE_REG_TH_V];
	mtk_ddp_write(comp, value, DISP_REG_WDMA_BUF_CON4, handle);
#endif
}

static unsigned int wdma_fmt_convert(unsigned int fmt)
{
	switch (fmt) {
	default:
	case DRM_FORMAT_RGB565:
		return MEM_MODE_INPUT_FORMAT_RGB565;
	case DRM_FORMAT_BGR565:
		return MEM_MODE_INPUT_FORMAT_RGB565 | MEM_MODE_INPUT_SWAP;
	case DRM_FORMAT_RGB888:
		return MEM_MODE_INPUT_FORMAT_RGB888;
	case DRM_FORMAT_BGR888:
		return MEM_MODE_INPUT_FORMAT_RGB888 | MEM_MODE_INPUT_SWAP;
	case DRM_FORMAT_RGBX8888:
	case DRM_FORMAT_RGBA8888:
		return MEM_MODE_INPUT_FORMAT_ARGB8888;
	case DRM_FORMAT_BGRX8888:
	case DRM_FORMAT_BGRA8888:
		return MEM_MODE_INPUT_FORMAT_ARGB8888 | MEM_MODE_INPUT_SWAP;
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_ARGB8888:
		return MEM_MODE_INPUT_FORMAT_RGBA8888;
	case DRM_FORMAT_XBGR8888:
	case DRM_FORMAT_ABGR8888:
		return MEM_MODE_INPUT_FORMAT_RGBA8888 | MEM_MODE_INPUT_SWAP;
	case DRM_FORMAT_UYVY:
		return MEM_MODE_INPUT_FORMAT_UYVY;
	case DRM_FORMAT_YUYV:
		return MEM_MODE_INPUT_FORMAT_YUYV;
	case DRM_FORMAT_YUV420:
		return MEM_MODE_INPUT_FORMAT_IYUV;
	case DRM_FORMAT_YVU420:
		return MEM_MODE_INPUT_FORMAT_IYUV | MEM_MODE_INPUT_SWAP;
	case DRM_FORMAT_ARGB2101010:
	case DRM_FORMAT_ABGR2101010:
		return MEM_MODE_INPUT_FORMAT_ARGB2101010;
	}
}

static void write_dst_addr(struct mtk_ddp_comp *comp, struct cmdq_pkt *handle,
			 int id, dma_addr_t addr)
{
	struct mtk_disp_wdma *wdma = comp_to_wdma(comp);

	mtk_ddp_write(comp, addr, DISP_REG_WDMA_DST_ADDRX(id), handle);

	if (wdma->data->is_support_34bits) {
		unsigned int offset = wdma->info_data->is_support_ufbc ?
				DISP_REG_UFBC_WDMA_DST_ADDRX_MSB(id) :
				DISP_REG_WDMA_DST_ADDRX_MSB(id);
		mtk_ddp_write(comp, (addr >> 32), offset, handle);
	}
}

static dma_addr_t read_dst_addr(struct mtk_ddp_comp *comp, int id)
{
	struct mtk_disp_wdma *wdma = comp_to_wdma(comp);
	dma_addr_t addr = 0;

	if (wdma->data->is_support_34bits) {
		unsigned int offset = wdma->info_data->is_support_ufbc ?
				DISP_REG_UFBC_WDMA_DST_ADDRX_MSB(id) :
				DISP_REG_WDMA_DST_ADDRX_MSB(id);
		addr = readl(comp->regs + offset);
		addr = (addr << 32);
	}

	addr += readl(comp->regs + DISP_REG_WDMA_DST_ADDRX(id));

	return addr;
}

static int wdma_config_yuv420(struct mtk_ddp_comp *comp,
			      uint32_t fmt, unsigned int dstPitch,
			      unsigned int Height, dma_addr_t dstAddress,
			      uint32_t sec, void *handle)
{
	/* size_t size; */
	unsigned int u_off = 0;
	unsigned int v_off = 0;
	unsigned int u_stride = 0;
	unsigned int y_size = 0;
	unsigned int u_size = 0;
	/* unsigned int v_size = 0; */
	unsigned int stride = dstPitch;
	int has_v = 1;
	unsigned int aid_sel_offset = 0;
	struct mtk_drm_private *priv = comp->mtk_crtc->base.dev->dev_private;
	resource_size_t mmsys_reg = priv->config_regs_pa;
	struct mtk_disp_wdma *wdma = comp_to_wdma(comp);
	resource_size_t larb_ctl_dummy = 0;
	unsigned int value = 0, mask = 0;

	if (!wdma || !wdma->data) {
		DDPPR_ERR("Invalid address, %s,%d\n", __FILE__, __LINE__);
		return -1;
	}

	if (fmt != DRM_FORMAT_YUV420 && fmt != DRM_FORMAT_YVU420 &&
		fmt != DRM_FORMAT_NV12 && fmt != DRM_FORMAT_NV21)
		return 0;

	if (fmt == DRM_FORMAT_YUV420 || fmt == DRM_FORMAT_YVU420) {
		y_size = stride * Height;
		u_stride = ALIGN_TO(stride / 2, 16);
		u_size = u_stride * Height / 2;
		u_off = y_size;
		v_off = y_size + u_size;
	} else if (fmt == DRM_FORMAT_NV12 || fmt == DRM_FORMAT_NV21) {
		y_size = stride * Height;
		u_stride = stride / 2;
		u_size = u_stride * Height / 2;
		u_off = y_size;
		has_v = 0;
	}

	if (wdma->data->use_larb_control_sec) {
		if (wdma->data->check_wdma_sec_reg)
			larb_ctl_dummy = wdma->data->check_wdma_sec_reg(comp);
		if (larb_ctl_dummy) {
			if (mtk_wdma_store_sec_state(wdma, sec) && disp_sec_cb.cb != NULL) {
				if (sec) {
					disp_sec_cb.cb(DISP_SEC_ENABLE, handle,
							larb_ctl_dummy, NULL);
					mtk_crtc_exec_atf_prebuilt_instr(comp->mtk_crtc, handle);
				} else {
					disp_sec_cb.cb(DISP_SEC_DISABLE, handle,
							larb_ctl_dummy, NULL);
				}
			}
		}
	} else {
		if (wdma->data->aid_sel) {
			aid_sel_offset = wdma->data->aid_sel(comp);
			if (priv->data->mmsys_id == MMSYS_MT6989 ||
				priv->data->mmsys_id == MMSYS_MT6899 ||
				priv->data->mmsys_id == MMSYS_MT6991)
				mmsys_reg = priv->ovlsys1_regs_pa;
		}
		if (aid_sel_offset) {
			if (sec) {
				if (priv->data->mmsys_id == MMSYS_MT6991) {
					SET_VAL_MASK(value, mask, 0x3, L_CON_FLD_AID);
					cmdq_pkt_write(handle, comp->cmdq_base,
						mmsys_reg + aid_sel_offset, value, mask);
				} else
					cmdq_pkt_write(handle, comp->cmdq_base,
						mmsys_reg + aid_sel_offset,
						BIT(0), BIT(0));
			} else {
				if (priv->data->mmsys_id == MMSYS_MT6991) {
					SET_VAL_MASK(value, mask, 0, L_CON_FLD_AID);
					cmdq_pkt_write(handle, comp->cmdq_base,
						mmsys_reg + aid_sel_offset, value, mask);
				} else
					cmdq_pkt_write(handle, comp->cmdq_base,
						mmsys_reg + aid_sel_offset, 0, BIT(0));
			}
		}
	}

	if (disp_mtee_cb.cb != NULL && sec) {
		disp_mtee_cb.cb(DISP_SEC_ENABLE, 0, NULL, handle, comp, 0,
				comp->regs_pa + DISP_REG_WDMA_DST_ADDRX(1),
				dstAddress, u_off, u_size);
		if (has_v) {
			disp_mtee_cb.cb(DISP_SEC_ENABLE, 0, NULL, handle, comp, 0,
				comp->regs_pa + DISP_REG_WDMA_DST_ADDRX(2),
				dstAddress, v_off, u_size);
		}
	} else {
		write_dst_addr(comp, handle, 1, dstAddress + u_off);
		if (has_v)
			write_dst_addr(comp, handle, 2, dstAddress + v_off);
	}

	mtk_ddp_write_mask(comp, u_stride,
			DISP_REG_WDMA_DST_UV_PITCH, 0xFFFF, handle);
	return 0;
}

static bool is_yuv(uint32_t format)
{
	switch (format) {
	case DRM_FORMAT_YUV420:
	case DRM_FORMAT_YVU420:
	case DRM_FORMAT_NV12:
	case DRM_FORMAT_NV21:
	case DRM_FORMAT_YUYV:
	case DRM_FORMAT_YVYU:
	case DRM_FORMAT_UYVY:
	case DRM_FORMAT_VYUY:
		return true;
	default:
		break;
	}

	return false;
}

void mtk_wdma_blank_output(struct mtk_ddp_comp *comp,
				struct cmdq_pkt *handle, unsigned int enable_blank)
{
	DDPINFO("%s enable blank %d\n", __func__, enable_blank);
	if (enable_blank) {
		mtk_ddp_write_mask(comp, WDMA_BKGD_ENABLE,
				DISP_REG_WDMA_CFG, WDMA_BKGD_ENABLE, handle);

		mtk_ddp_write(comp, 0, DISP_REG_WDMA_BKGD, handle);

		mtk_ddp_write(comp, 0, DISP_REG_WDMA_BKGD_MSB, handle);
	} else {
		mtk_ddp_write_mask(comp, 0,
				DISP_REG_WDMA_CFG, WDMA_BKGD_ENABLE, handle);
	}
}

static void mtk_ufbc_wdma_config(struct mtk_ddp_comp *comp,
				 union mtk_addon_config *addon_config,
				 struct cmdq_pkt *handle)
{
	unsigned int w, h, subblock_nr;
	size_t size_header;
	u32 val;

	struct drm_crtc *crtc;
	struct mtk_crtc_state *state;
	unsigned long long temp_bw, temp_peak_bw;
	u32 vrefresh;

	if (comp->fb->modifier != DRM_FORMAT_MOD_ARM_AFBC(
			AFBC_FORMAT_MOD_BLOCK_SIZE_32x8 | AFBC_FORMAT_MOD_SPARSE))
		DDPPR_ERR("%s:%d ufbc_wdma not support non AFBC\n", __func__, __LINE__);

	switch (comp->fb->format->format) {
	case DRM_FORMAT_ABGR8888:
	case DRM_FORMAT_XBGR8888:
		val = UFBC_WDMA_FMT_ABGR8888;
		break;
	case DRM_FORMAT_ABGR2101010:
	case DRM_FORMAT_XBGR2101010:
		val = UFBC_WDMA_FMT_ABGR2101010;
		break;
	default:
		DDPPR_ERR("%s:fmt:%d not support\n", __func__,
			comp->fb->format->format);
		return;
	}
	mtk_ddp_write(comp, val, DISP_REG_UFBC_WDMA_FMT, handle);

	w = ALIGN(addon_config->addon_wdma_config.wdma_dst_roi.width, 32);
	h = ALIGN(addon_config->addon_wdma_config.wdma_dst_roi.height, 8);
	subblock_nr = w * h / 256;
	size_header = ALIGN(subblock_nr * 16, 1024);
	mtk_ddp_write(comp, UFBC_WDMA_AFBC_YUV_TRANSFORM,
		      DISP_REG_UFBC_WDMA_AFBC_SETTING, handle);
	mtk_ddp_write(comp, size_header, DISP_REG_UFBC_WDMA_PAYLOAD_OFFSET, handle);
	mtk_ddp_write(comp, (h << 16) | w, DISP_REG_UFBC_WDMA_BG_SIZE, handle);
	mtk_ddp_write(comp, 0, DISP_REG_UFBC_WDMA_TILE_OFFSET, handle);
	mtk_ddp_write(comp, (addon_config->addon_wdma_config.wdma_dst_roi.height << 16) |
		      addon_config->addon_wdma_config.wdma_dst_roi.width,
		      DISP_REG_UFBC_WDMA_TILE_SIZE, handle);
	write_dst_addr(comp, handle, 0, addon_config->addon_wdma_config.addr);

	//calculate qos_bw
	crtc = &comp->mtk_crtc->base;
	state = to_mtk_crtc_state(crtc->state);
	temp_bw = (unsigned long long)w * h;
	temp_bw *= mtk_get_format_bpp(comp->fb->format->format);
	vrefresh = crtc->state ? drm_mode_vrefresh(&crtc->state->adjusted_mode) : 60;
	/* COMPRESS ratio */
	if (comp->fb->modifier == DRM_FORMAT_MOD_ARM_AFBC(
	    AFBC_FORMAT_MOD_BLOCK_SIZE_32x8 | AFBC_FORMAT_MOD_SPARSE)) {
		temp_bw *= 7;
		do_div(temp_bw, 10);
	}
	do_div(temp_bw, 1000);
	temp_bw *= 125;
	do_div(temp_bw, 100);
	temp_bw = temp_bw * vrefresh;
	do_div(temp_bw, 1000);
	comp->qos_bw = temp_bw;

	//calculate hrt_bw
	if (crtc->state) {
		temp_peak_bw = (unsigned long long)crtc->state->adjusted_mode.vdisplay *
				crtc->state->adjusted_mode.hdisplay;
	} else {
		temp_peak_bw = (unsigned long long)w * h;
	}

	temp_peak_bw *= mtk_get_format_bpp(comp->fb->format->format);

	do_div(temp_peak_bw, 1000);
	temp_peak_bw *= 125;
	do_div(temp_peak_bw, 100);
	temp_peak_bw = temp_peak_bw * vrefresh;
	do_div(temp_peak_bw, 1000);
	comp->hrt_bw = temp_peak_bw;
}

static void mtk_wdma_config(struct mtk_ddp_comp *comp,
				struct mtk_ddp_config *cfg, struct cmdq_pkt *handle)
{
	unsigned int size = 0;
	unsigned int con = 0;
	dma_addr_t addr = 0;
	struct mtk_disp_wdma *wdma = comp_to_wdma(comp);
	struct mtk_wdma_cfg_info *cfg_info = &wdma->cfg_info;
	unsigned int crtc_idx = drm_crtc_index(&comp->mtk_crtc->base);
	int clip_w, clip_h;
	struct golden_setting_context *gsc;
	u32 sec;
	unsigned int frame_cnt = cfg_info->count + 1;
	unsigned int aid_sel_offset = 0;
	struct mtk_drm_private *priv = comp->mtk_crtc->base.dev->dev_private;
	resource_size_t mmsys_reg = priv->config_regs_pa;
	resource_size_t larb_ctl_dummy = 0;
	unsigned int value = 0, mask = 0;

	if (crtc_idx >= MAX_CRTC) {
		DDPPR_ERR("%s, invalid crtc:%u\n", __func__, crtc_idx);
		return;
	}
	if (!comp->fb) {
		if (crtc_idx != 2)
			DDPPR_ERR("%s fb is empty, CRTC%d\n", __func__, crtc_idx);
		return;
	}

	addr = mtk_fb_get_dma(comp->fb);
	if (!addr) {
		DDPPR_ERR("%s:%d C%d no dma_buf\n",
				__func__, __LINE__,
				crtc_idx);
		return;
	}
	sec = mtk_drm_fb_is_secure(comp->fb);

	addr += comp->fb->offsets[0];
	con = wdma_fmt_convert(comp->fb->format->format);
	DDPINFO("%s comp_id:%d fmt:0x%x, con:0x%x addr:0x%lx\n", __func__, comp->id,
		comp->fb->format->format, con, (unsigned long)addr);
	if (!addr) {
		DDPPR_ERR("%s wdma dst addr is zero\n", __func__);
		return;
	}

	clip_w = cfg->w;
	clip_h = cfg->h;
	if (is_yuv(comp->fb->format->format)) {
		if ((cfg->x + cfg->w) % 2) {
			if (crtc_idx == 2 && comp->mtk_crtc->is_dual_pipe &&
					wdma->data && wdma->data->is_right_wdma_comp(comp))
				clip_w += 1;
			else
				clip_w -= 1;
		}

		if ((cfg->y + cfg->h) % 2)
			clip_h -= 1;
	}

	if (crtc_idx == 2 && comp->mtk_crtc->is_dual_pipe && ((cfg->x + cfg->w) % 2))
		size = (clip_w & 0x3FFFU) + ((cfg->h << 16U) & 0x3FFF0000U);
	else
		size = (cfg->w & 0x3FFFU) + ((cfg->h << 16U) & 0x3FFF0000U);
	mtk_ddp_write(comp, size, DISP_REG_WDMA_SRC_SIZE, handle);
	mtk_ddp_write(comp, (cfg->y << 16) | cfg->x,
		DISP_REG_WDMA_CLIP_COORD, handle);
	mtk_ddp_write(comp, (clip_h << 16) | clip_w,
		DISP_REG_WDMA_CLIP_SIZE, handle);
	mtk_ddp_write_mask(comp, con, DISP_REG_WDMA_CFG,
		WDMA_OUT_FMT | WDMA_CON_SWAP, handle);

	if (is_yuv(comp->fb->format->format)) {
		wdma_config_yuv420(comp, comp->fb->format->format,
				comp->fb->pitches[0], cfg->h,
				addr, sec, handle);
		mtk_ddp_write_mask(comp, 0,
				DISP_REG_WDMA_CFG, WDMA_UFO_DCP_ENABLE, handle);
		mtk_ddp_write_mask(comp, WDMA_CT_EN,
				DISP_REG_WDMA_CFG, WDMA_CT_EN, handle);
		mtk_ddp_write_mask(comp, 0x02000000,
				DISP_REG_WDMA_CFG, WDMA_INT_MTX_SEL, handle);
	} else {
		mtk_ddp_write_mask(comp, 0,
				DISP_REG_WDMA_CFG, WDMA_UFO_DCP_ENABLE, handle);
		mtk_ddp_write_mask(comp, 0,
				DISP_REG_WDMA_CFG, WDMA_CT_EN, handle);
	}

	/* Debug WDMA status */
	mtk_ddp_write_mask(comp, 0xe0000000,
			DISP_REG_WDMA_CFG, WDMA_DEBUG_SEL, handle);

	if (priv->usage[crtc_idx] == DISP_OPENING)
		mtk_wdma_blank_output(comp, handle, 1);
	else
		mtk_wdma_blank_output(comp, handle, 0);

	mtk_ddp_write(comp, comp->fb->pitches[0],
		DISP_REG_WDMA_DST_WIN_BYTE, handle);

	if (wdma->data && wdma->data->use_larb_control_sec) {
		if (wdma->data && wdma->data->check_wdma_sec_reg)
			larb_ctl_dummy = wdma->data->check_wdma_sec_reg(comp);
		if (larb_ctl_dummy) {
			if (mtk_wdma_store_sec_state(wdma, sec) && disp_sec_cb.cb != NULL) {
				if (sec) {
					disp_sec_cb.cb(DISP_SEC_ENABLE, handle,
							larb_ctl_dummy, NULL);
					mtk_crtc_exec_atf_prebuilt_instr(comp->mtk_crtc, handle);
				} else
					disp_sec_cb.cb(DISP_SEC_DISABLE, handle,
							larb_ctl_dummy, NULL);
			}
		}
	} else {
		if (wdma->data && wdma->data->aid_sel) {
			aid_sel_offset = wdma->data->aid_sel(comp);
			if (priv->data->mmsys_id == MMSYS_MT6989 ||
				priv->data->mmsys_id == MMSYS_MT6899 ||
				priv->data->mmsys_id == MMSYS_MT6991)
				mmsys_reg = priv->ovlsys1_regs_pa;
		}
		if (aid_sel_offset) {
			if (sec) {
				if (priv->data->mmsys_id == MMSYS_MT6991) {
					SET_VAL_MASK(value, mask, 0x3, L_CON_FLD_AID);
					cmdq_pkt_write(handle, comp->cmdq_base,
						mmsys_reg + aid_sel_offset, value, mask);
				} else
					cmdq_pkt_write(handle, comp->cmdq_base,
						mmsys_reg + aid_sel_offset, BIT(0), BIT(0));
			} else {
				if (priv->data->mmsys_id == MMSYS_MT6991) {
					SET_VAL_MASK(value, mask, 0, L_CON_FLD_AID);
					cmdq_pkt_write(handle, comp->cmdq_base,
						mmsys_reg + aid_sel_offset, value, mask);
				} else
					cmdq_pkt_write(handle, comp->cmdq_base,
						mmsys_reg + aid_sel_offset, 0, BIT(0));
			}
		}
	}
	write_dst_addr(comp, handle, 0, addr);

	if (crtc_idx == 2 && comp->mtk_crtc->is_dual_pipe &&
		wdma->data && wdma->data->is_right_wdma_comp(comp)) {
		if (comp->fb->format->format == DRM_FORMAT_YUV420 ||
			comp->fb->format->format == DRM_FORMAT_YVU420) {
			if ((cfg->x + cfg->w) % 2)
				mtk_ddp_write(comp, cfg->w - 1,
					DISP_REG_WDMA_DST_ADDR_OFFSETX(0),  handle);
			else
				mtk_ddp_write(comp, cfg->w,
					DISP_REG_WDMA_DST_ADDR_OFFSETX(0),  handle);
			mtk_ddp_write(comp, cfg->w / 2,
				DISP_REG_WDMA_DST_ADDR_OFFSETX(1),  handle);
			mtk_ddp_write(comp, cfg->w / 2,
				DISP_REG_WDMA_DST_ADDR_OFFSETX(2),  handle);
		} else if (comp->fb->format->format == DRM_FORMAT_NV12 ||
					comp->fb->format->format == DRM_FORMAT_NV21) {
			if ((cfg->x + cfg->w) % 2) {
				mtk_ddp_write(comp, cfg->w - 1,
					DISP_REG_WDMA_DST_ADDR_OFFSETX(0),  handle);
				mtk_ddp_write(comp, cfg->w - 1,
					DISP_REG_WDMA_DST_ADDR_OFFSETX(1),  handle);
			} else {
				mtk_ddp_write(comp, cfg->w,
					DISP_REG_WDMA_DST_ADDR_OFFSETX(0),  handle);
				mtk_ddp_write(comp, cfg->w,
					DISP_REG_WDMA_DST_ADDR_OFFSETX(1),  handle);
			}
		}
	}

	if (disp_mtee_cb.cb != NULL && sec) {
		u32 buffer_size = clip_w * comp->fb->pitches[0];

		disp_mtee_cb.cb(DISP_SEC_ENABLE, 0, NULL, handle, comp, 0,
			comp->regs_pa + DISP_REG_WDMA_DST_ADDRX(0),
			addr & 0xFFFFFFFFU, 0, buffer_size);
	} else {
		write_dst_addr(comp, handle, 0, addr);
		mtk_ddp_write(comp, frame_cnt, DISP_REG_WDMA_DUMMY, handle);
	}

	gsc = cfg->p_golden_setting_context;
	mtk_wdma_golden_setting(comp, gsc, handle);
	mtk_ddp_write(comp, 0x1, DISP_REG_WDMA_VCSEL, handle);

	cfg_info->addr = addr;
	cfg_info->width = cfg->w;
	cfg_info->height = cfg->h;
	cfg_info->fmt = comp->fb->format->format;
	cfg_info->count = frame_cnt;
}

static void mtk_wdma_addon_config(struct mtk_ddp_comp *comp,
				 enum mtk_ddp_comp_id prev,
				 enum mtk_ddp_comp_id next,
				 union mtk_addon_config *addon_config,
				 struct cmdq_pkt *handle)
{
	unsigned int size = 0;
	unsigned int con = 0;
	unsigned long long bw_base;
	dma_addr_t addr = 0;
	struct mtk_disp_wdma *wdma = comp_to_wdma(comp);
	struct mtk_wdma_cfg_info *cfg_info = &wdma->cfg_info;
	int crtc_idx = drm_crtc_index(&comp->mtk_crtc->base);
	int src_w, src_h, clip_w, clip_h, clip_x, clip_y, pitch;
	int hact, vtotal, vact, vrefresh, bpp;
	bool is_secure;
	struct golden_setting_context *gsc;
	struct mtk_drm_private *priv = comp->mtk_crtc->base.dev->dev_private;
	struct mtk_drm_crtc *mtk_crtc = comp->mtk_crtc;
	resource_size_t mmsys_reg = priv->config_regs_pa;

	comp->fb = addon_config->addon_wdma_config.fb;
	if (!comp->fb) {
		DDPPR_ERR("%s fb is empty, CRTC%d\n", __func__, crtc_idx);
		return;
	}

	src_w = addon_config->addon_wdma_config.wdma_src_roi.width;
	src_h = addon_config->addon_wdma_config.wdma_src_roi.height;
	clip_w = addon_config->addon_wdma_config.wdma_dst_roi.width;
	clip_h = addon_config->addon_wdma_config.wdma_dst_roi.height;
	clip_x = addon_config->addon_wdma_config.wdma_dst_roi.x;
	clip_y = addon_config->addon_wdma_config.wdma_dst_roi.y;
	pitch = addon_config->addon_wdma_config.pitch;
	is_secure = addon_config->addon_wdma_config.is_secure;

	addr = addon_config->addon_wdma_config.addr;
	if (!addr) {
		DDPPR_ERR("%s:%d C%d no dma_buf\n", __func__, __LINE__, crtc_idx);
		return;
	}

	if (wdma->info_data->is_support_ufbc) {
		mtk_ufbc_wdma_config(comp, addon_config, handle);
		goto golden_setting;
	}

	// WDMA bandwidth setting
	bpp = mtk_get_format_bpp(comp->fb->format->format);
	hact = mtk_crtc->base.state->adjusted_mode.hdisplay;
	vtotal = mtk_crtc->base.state->adjusted_mode.vtotal;
	vact = mtk_crtc->base.state->adjusted_mode.vdisplay;
	vrefresh = drm_mode_vrefresh(&mtk_crtc->base.state->adjusted_mode);
	bw_base = div_u64((unsigned long long)vact * hact * vrefresh * bpp, 1000);
	bw_base = div_u64(bw_base, 1000) * 2;

	mtk_ddp_comp_io_cmd(comp, NULL, PMQOS_SET_HRT_BW, &bw_base);

	DDPINFO("%s WDMA config iommu, CRTC%d\n", __func__, crtc_idx);
	mtk_ddp_comp_iommu_enable(comp, handle);

	write_dst_addr(comp, handle, 0, addr);

	con = wdma_fmt_convert(comp->fb->format->format);

	size = (src_w & 0x3FFFU) + ((src_h << 16U) & 0x3FFF0000U);
	mtk_ddp_write(comp, size, DISP_REG_WDMA_SRC_SIZE, handle);
	mtk_ddp_write(comp, (clip_y << 16) | clip_x,
		DISP_REG_WDMA_CLIP_COORD, handle);
	mtk_ddp_write(comp, (clip_h << 16) | clip_w,
		DISP_REG_WDMA_CLIP_SIZE, handle);
	mtk_ddp_write_mask(comp, con, DISP_REG_WDMA_CFG,
		WDMA_OUT_FMT | WDMA_CON_SWAP, handle);

	mtk_ddp_write_mask(comp, 0,
			DISP_REG_WDMA_CFG, WDMA_UFO_DCP_ENABLE, handle);
	mtk_ddp_write_mask(comp, 0,
			DISP_REG_WDMA_CFG, WDMA_CT_EN, handle);

	/* Debug WDMA status */
	mtk_ddp_write_mask(comp, 0xe0000000,
			DISP_REG_WDMA_CFG, WDMA_DEBUG_SEL, handle);

	mtk_ddp_write(comp, pitch,
		DISP_REG_WDMA_DST_WIN_BYTE, handle);

	/* WDMA secure memory buffer config */
	if (is_secure) {
		if (priv->data->mmsys_id == MMSYS_MT6989 ||
			priv->data->mmsys_id == MMSYS_MT6899) {
			mtk_ddp_write(comp, 0x0,
				WDMA_SECURITY_DISABLE, handle);
			mmsys_reg = priv->side_config_regs_pa;
			cmdq_pkt_write(handle, comp->cmdq_base,
							mmsys_reg + MT6989_DISP1_AID_SEL_MANUAL,
								DISP_WDMA0_AID_SEL_MANUAL, DISP_WDMA0_AID_SEL_MANUAL);
			cmdq_pkt_write(handle, comp->cmdq_base,
							mmsys_reg + MT6989_DISP1_WDMA0_AID_SETTING, BIT(0), BIT(0));
		} else if (priv->data->mmsys_id == MMSYS_MT6991) {
			mtk_ddp_write(comp, 0x0,
				WDMA_SECURITY_DISABLE, handle);
			mmsys_reg = priv->side_config_regs_pa;
			// DISP1_AID_SEL_MANUAL
			cmdq_pkt_write(handle, comp->cmdq_base,
							mmsys_reg + MT6991_DISP1_AID_SEL_MANUAL,
								DISP_WDMA0_AID_SEL_MANUAL, DISP_WDMA0_AID_SEL_MANUAL);
			// DISP1_WDMA1_AID_SETTING
			cmdq_pkt_write(handle, comp->cmdq_base,
							mmsys_reg + MT6991_DISP1_WDMA1_AID_SETTING, BIT(0), BIT(0));
		}
	} else {
		if (priv->data->mmsys_id == MMSYS_MT6989 ||
			priv->data->mmsys_id == MMSYS_MT6899) {
			mtk_ddp_write(comp, 0x1,
				WDMA_SECURITY_DISABLE, handle);
			mmsys_reg = priv->side_config_regs_pa;
			cmdq_pkt_write(handle, comp->cmdq_base,
							mmsys_reg + MT6989_DISP1_AID_SEL_MANUAL,
								0, DISP_WDMA0_AID_SEL_MANUAL);
			cmdq_pkt_write(handle, comp->cmdq_base,
							mmsys_reg + MT6989_DISP1_WDMA0_AID_SETTING, 0, BIT(0));
		} else if (priv->data->mmsys_id == MMSYS_MT6991) {
			mtk_ddp_write(comp, 0x7,
				WDMA_SECURITY_DISABLE, handle);
			mmsys_reg = priv->side_config_regs_pa;
			// DISP1_AID_SEL_MANUAL
			cmdq_pkt_write(handle, comp->cmdq_base,
							mmsys_reg + MT6991_DISP1_AID_SEL_MANUAL,
								0, DISP_WDMA0_AID_SEL_MANUAL);
			// DISP1_WDMA1_AID_SETTING
			cmdq_pkt_write(handle, comp->cmdq_base,
							mmsys_reg + MT6991_DISP1_WDMA1_AID_SETTING, 0, BIT(0));
		}
	}

	switch (comp->fb->format->format) {
	case DRM_FORMAT_RGBA8888:
	case DRM_FORMAT_BGRA8888:
	case DRM_FORMAT_ARGB8888:
	case DRM_FORMAT_ABGR8888:
	case DRM_FORMAT_ARGB2101010:
		mtk_ddp_write_mask(comp, WDMA_A_Sel,
					DISP_REG_WDMA_ALPHA, WDMA_A_Sel, handle);
		mtk_ddp_write_mask(comp, WDMA_A_Value,
					DISP_REG_WDMA_ALPHA, WDMA_A_Value, handle);
		break;
	default:
		mtk_ddp_write_mask(comp, 0,
					DISP_REG_WDMA_ALPHA, WDMA_A_Sel, handle);
		break;
	}

golden_setting:
	gsc = addon_config->addon_wdma_config.p_golden_setting_context;
	mtk_wdma_golden_setting(comp, gsc, handle);

	DDPINFO("%s:comp:%u,addr:0x%lx,roi:(%d,%d,%d,%d),fmt:0x%x\n",
		__func__, comp->id, (unsigned long)addr, clip_x, clip_y,
		clip_w, clip_h, comp->fb->format->format);
	cfg_info->addr = addr;
	cfg_info->width = clip_w;
	cfg_info->height = clip_h;
	cfg_info->fmt = comp->fb->format->format;
}

void mtk_wdma_dump_golden_setting(struct mtk_ddp_comp *comp)
{
	struct mtk_disp_wdma *wdma = comp_to_wdma(comp);
	void __iomem *baddr = comp->regs;
	unsigned int value;
	int i;

	DDPDUMP("-- %s Golden Setting --\n", mtk_dump_comp_str(comp));
	if (comp->mtk_crtc && comp->mtk_crtc->sec_on) {
		DDPDUMP("Skip dump secure wdma!\n");
		return;
	}
	if (wdma->info_data->is_support_ufbc) {
		DDPDUMP("0x060:0x%08x 0x068:0x%08x\n",
			readl(DISP_REG_UFBC_WDMA_PREULTRA_D0 + baddr),
			readl(DISP_REG_UFBC_WDMA_PREULTRA_H0 + baddr));
		DDPDUMP("0x070:0x%08x 0x078:0x%08x\n",
			readl(DISP_REG_UFBC_WDMA_ULTRA_D0 + baddr),
			readl(DISP_REG_UFBC_WDMA_ULTRA_H0 + baddr));
		DDPDUMP("0x080:0x%08x 0x088:0x%08x\n",
			readl(DISP_REG_UFBC_WDMA_URGENT_D0 + baddr),
			readl(DISP_REG_UFBC_WDMA_URGENT_H0 + baddr));
		DDPDUMP("0x090:0x%08x\n",
			readl(DISP_REG_UFBC_WDMA_DMA_CON + baddr));
		return;
	}

	DDPDUMP("0x%03x:0x%08x 0x%03x:0x%08x\n",
		0x10, readl(DISP_REG_WDMA_SMI_CON + baddr),
		0x38, readl(DISP_REG_WDMA_BUF_CON1 + baddr));
	for (i = 0; i < 3; i++)
		DDPDUMP("0x%03x:0x%08x 0x%08x 0x%08x 0x%08x\n",
			0x200 + i * 0x10,
			readl(DISP_REG_WDMA_BUF_CON5 +
				     baddr + i * 0x10),
			readl(DISP_REG_WDMA_BUF_CON6 +
				     baddr + i * 0x10),
			readl(DISP_REG_WDMA_BUF_CON7 +
				     baddr + i * 0x10),
			readl(DISP_REG_WDMA_BUF_CON8 +
				     baddr + i * 0x10));
	DDPDUMP("0x%03x:0x%08x 0x%08x\n",
		0x230, readl(DISP_REG_WDMA_BUF_CON17 + baddr),
		readl(DISP_REG_WDMA_BUF_CON18 + baddr));
	DDPDUMP("0x%03x:0x%08x 0x%08x 0x%08x 0x%08x\n",
		0x250, readl(DISP_REG_WDMA_DRS_CON0 + baddr),
		readl(DISP_REG_WDMA_DRS_CON1 + baddr),
		readl(DISP_REG_WDMA_DRS_CON2 + baddr),
		readl(DISP_REG_WDMA_DRS_CON3 + baddr));
	DDPDUMP("0x%03x:0x%08x 0x%08x\n",
		0x104, readl(DISP_REG_WDMA_BUF_CON3 + baddr),
		readl(DISP_REG_WDMA_BUF_CON4 + baddr));

	value = readl(DISP_REG_WDMA_SMI_CON + baddr);
	DDPDUMP("WDMA_SMI_CON:[3:0]:%x [4:4]:%x [7:5]:%x [15:8]:%x\n",
		REG_FLD_VAL_GET(SMI_CON_FLD_THRESHOLD, value),
		REG_FLD_VAL_GET(SMI_CON_FLD_SLOW_ENABLE, value),
		REG_FLD_VAL_GET(SMI_CON_FLD_SLOW_LEVEL, value),
		REG_FLD_VAL_GET(SMI_CON_FLD_SLOW_COUNT, value));
	DDPDUMP("WDMA_SMI_CON:[19:16]:%u [23:20]:%u [27:24]:%u [28]:%u\n",
		REG_FLD_VAL_GET(SMI_CON_FLD_SMI_Y_REPEAT_NUM, value),
		REG_FLD_VAL_GET(SMI_CON_FLD_SMI_U_REPEAT_NUM, value),
		REG_FLD_VAL_GET(SMI_CON_FLD_SMI_V_REPEAT_NUM, value),
		REG_FLD_VAL_GET(SMI_CON_FLD_SMI_OBUF_FULL_REQ, value));

	value = readl(DISP_REG_WDMA_BUF_CON1 + baddr);
	DDPDUMP("WDMA_BUF_CON1:[31]:%x [30]:%x [28]:%x [26]%d\n",
		REG_FLD_VAL_GET(BUF_CON1_FLD_ULTRA_ENABLE, value),
		REG_FLD_VAL_GET(BUF_CON1_FLD_PRE_ULTRA_ENABLE, value),
		REG_FLD_VAL_GET(BUF_CON1_FLD_FRAME_END_ULTRA, value),
		REG_FLD_VAL_GET(BUF_CON1_FLD_URGENT_EN, value));
	DDPDUMP("WDMA_BUF_CON1:[18:10]:%d [9:0]:%d\n",
		REG_FLD_VAL_GET(BUF_CON1_FLD_FIFO_PSEUDO_SIZE_UV, value),
		REG_FLD_VAL_GET(BUF_CON1_FLD_FIFO_PSEUDO_SIZE, value));

	value = readl(DISP_REG_WDMA_BUF_CON5 + baddr);
	DDPDUMP("WDMA_BUF_CON5:[9:0]:%d [25:16]:%d\n",
		REG_FLD_VAL_GET(BUF_CON_FLD_PRE_ULTRA_LOW, value),
		REG_FLD_VAL_GET(BUF_CON_FLD_ULTRA_LOW, value));

	value = readl(DISP_REG_WDMA_BUF_CON6 + baddr);
	DDPDUMP("WDMA_BUF_CON6:[9:0]:%d [25:16]:%d\n",
		REG_FLD_VAL_GET(BUF_CON_FLD_PRE_ULTRA_HIGH, value),
		REG_FLD_VAL_GET(BUF_CON_FLD_ULTRA_HIGH, value));

	value = readl(DISP_REG_WDMA_BUF_CON7 + baddr);
	DDPDUMP("WDMA_BUF_CON7:[9:0]:%d [25:16]:%d\n",
		REG_FLD_VAL_GET(BUF_CON_FLD_PRE_ULTRA_LOW, value),
		REG_FLD_VAL_GET(BUF_CON_FLD_ULTRA_LOW, value));

	value = readl(DISP_REG_WDMA_BUF_CON8 + baddr);
	DDPDUMP("WDMA_BUF_CON8:[9:0]:%d [25:16]:%d\n",
		REG_FLD_VAL_GET(BUF_CON_FLD_PRE_ULTRA_HIGH, value),
		REG_FLD_VAL_GET(BUF_CON_FLD_ULTRA_HIGH, value));

	value = readl(DISP_REG_WDMA_BUF_CON9 + baddr);
	DDPDUMP("WDMA_BUF_CON9:[9:0]:%d [25:16]:%d\n",
		REG_FLD_VAL_GET(BUF_CON_FLD_PRE_ULTRA_LOW, value),
		REG_FLD_VAL_GET(BUF_CON_FLD_ULTRA_LOW, value));

	value = readl(DISP_REG_WDMA_BUF_CON10 + baddr);
	DDPDUMP("WDMA_BUF_CON10:[9:0]:%d [25:16]:%d\n",
		REG_FLD_VAL_GET(BUF_CON_FLD_PRE_ULTRA_HIGH, value),
		REG_FLD_VAL_GET(BUF_CON_FLD_ULTRA_HIGH, value));

	value = readl(DISP_REG_WDMA_BUF_CON11 + baddr);
	DDPDUMP("WDMA_BUF_CON11:[9:0]:%d [25:16]:%d\n",
		REG_FLD_VAL_GET(BUF_CON_FLD_PRE_ULTRA_LOW, value),
		REG_FLD_VAL_GET(BUF_CON_FLD_ULTRA_LOW, value));

	value = readl(DISP_REG_WDMA_BUF_CON12 + baddr);
	DDPDUMP("WDMA_BUF_CON12:[9:0]:%d [25:16]:%d\n",
		REG_FLD_VAL_GET(BUF_CON_FLD_PRE_ULTRA_HIGH, value),
		REG_FLD_VAL_GET(BUF_CON_FLD_ULTRA_HIGH, value));

	value = readl(DISP_REG_WDMA_BUF_CON13 + baddr);
	DDPDUMP("WDMA_BUF_CON13:[9:0]:%d [25:16]:%d\n",
		REG_FLD_VAL_GET(BUF_CON_FLD_PRE_ULTRA_LOW, value),
		REG_FLD_VAL_GET(BUF_CON_FLD_ULTRA_LOW, value));

	value = readl(DISP_REG_WDMA_BUF_CON14 + baddr);
	DDPDUMP("WDMA_BUF_CON14:[9:0]:%d [25:16]:%d\n",
		REG_FLD_VAL_GET(BUF_CON_FLD_PRE_ULTRA_HIGH, value),
		REG_FLD_VAL_GET(BUF_CON_FLD_ULTRA_HIGH, value));

	value = readl(DISP_REG_WDMA_BUF_CON15 + baddr);
	DDPDUMP("WDMA_BUF_CON15:[9:0]:%d [25:16]:%d\n",
		REG_FLD_VAL_GET(BUF_CON_FLD_PRE_ULTRA_LOW, value),
		REG_FLD_VAL_GET(BUF_CON_FLD_ULTRA_LOW, value));

	value = readl(DISP_REG_WDMA_BUF_CON16 + baddr);
	DDPDUMP("WDMA_BUF_CON16:[9:0]:%d [25:16]:%d\n",
		REG_FLD_VAL_GET(BUF_CON_FLD_PRE_ULTRA_HIGH, value),
		REG_FLD_VAL_GET(BUF_CON_FLD_ULTRA_HIGH, value));

	value = readl(DISP_REG_WDMA_BUF_CON17 + baddr);
	DDPDUMP("WDMA_BUF_CON17:[0]:%d [25:16]:%d\n",
		REG_FLD_VAL_GET(BUF_CON17_FLD_WDMA_DVFS_EN, value),
		REG_FLD_VAL_GET(BUF_CON17_FLD_DVFS_TH_Y, value));

	value = readl(DISP_REG_WDMA_BUF_CON18 + baddr);
	DDPDUMP("WDMA_BUF_CON18:[9:0]:%d [25:16]:%d\n",
		REG_FLD_VAL_GET(BUF_CON18_FLD_DVFS_TH_U, value),
		REG_FLD_VAL_GET(BUF_CON18_FLD_DVFS_TH_V, value));

	value = readl(DISP_REG_WDMA_URGENT_CON0 + baddr);
	DDPDUMP("WDMA_URGENT_CON0:[9:0]:%d [25:16]:%d\n",
		REG_FLD_VAL_GET(FLD_WDMA_URGENT_LOW_Y, value),
		REG_FLD_VAL_GET(FLD_WDMA_URGENT_HIGH_Y, value));

	value = readl(DISP_REG_WDMA_URGENT_CON1 + baddr);
	DDPDUMP("WDMA_URGENT_CON1:[9:0]:%d [25:16]:%d\n",
		REG_FLD_VAL_GET(FLD_WDMA_URGENT_LOW_U, value),
		REG_FLD_VAL_GET(FLD_WDMA_URGENT_HIGH_U, value));

	value = readl(DISP_REG_WDMA_URGENT_CON2 + baddr);
	DDPDUMP("WDMA_URGENT_CON2:[9:0]:%d [25:16]:%d\n",
		REG_FLD_VAL_GET(FLD_WDMA_URGENT_LOW_V, value),
		REG_FLD_VAL_GET(FLD_WDMA_URGENT_HIGH_V, value));

	value = readl(DISP_REG_WDMA_BUF_CON3 + baddr);
	DDPDUMP("WDMA_BUF_CON3:[8:0]:%d [25:16]:%d\n",
		REG_FLD_VAL_GET(BUF_CON3_FLD_ISSUE_REQ_TH_Y, value),
		REG_FLD_VAL_GET(BUF_CON3_FLD_ISSUE_REQ_TH_U, value));

	value = readl(DISP_REG_WDMA_BUF_CON4 + baddr);
	DDPDUMP("WDMA_BUF_CON4:[8:0]:%d\n",
		REG_FLD_VAL_GET(BUF_CON4_FLD_ISSUE_REQ_TH_V, value));
}

int mtk_wdma_dump(struct mtk_ddp_comp *comp)
{
	void __iomem *baddr = comp->regs;
	struct mtk_disp_wdma *wdma = comp_to_wdma(comp);

	if (!baddr) {
		DDPDUMP("%s, %s is NULL!\n", __func__, mtk_dump_comp_str(comp));
		return 0;
	}

	DDPDUMP("== %s REGS:0x%pa ==\n", mtk_dump_comp_str(comp), &comp->regs_pa);

	if (comp->mtk_crtc && comp->mtk_crtc->sec_on) {
		DDPDUMP("Skip dump secure wdma!\n");
		return 0;
	}

	if (mtk_ddp_comp_helper_get_opt(comp,
					MTK_DRM_OPT_REG_PARSER_RAW_DUMP)) {
		unsigned int i = 0;

		for (i = 0; i < 0x300; i += 0x10)
			mtk_serial_dump_reg(baddr, i, 4);
	} else if (wdma->info_data->is_support_ufbc) {
		DDPDUMP("0x000:0x%08x 0x010:0x%08x 0x014:0x%08x 0x018:0x%08x\n",
			readl(DISP_REG_UFBC_WDMA_EN + baddr),
			readl(DISP_REG_UFBC_WDMA_INTEN + baddr),
			readl(DISP_REG_UFBC_WDMA_INTSTA + baddr),
			readl(DISP_REG_UFBC_WDMA_SHADOW_CTRL + baddr));
		DDPDUMP("0x040:0x%08x 0x%08x 0x%08x\n",
			readl(DISP_REG_UFBC_WDMA_PAYLOAD_OFFSET + baddr),
			readl(DISP_REG_UFBC_WDMA_AFBC_SETTING + baddr),
			readl(DISP_REG_UFBC_WDMA_FMT + baddr));
		DDPDUMP("0x050:0x%08x 0x%08x 0x%08x\n",
			readl(DISP_REG_UFBC_WDMA_BG_SIZE + baddr),
			readl(DISP_REG_UFBC_WDMA_TILE_OFFSET + baddr),
			readl(DISP_REG_UFBC_WDMA_TILE_SIZE + baddr));
		DDPDUMP("0xf00:0x%08x 0x%08x 0x%08x 0x%08x\n",
			readl(DISP_REG_UFBC_WDMA_ADDR0 + baddr),
			readl(DISP_REG_UFBC_WDMA_ADDR1 + baddr),
			readl(DISP_REG_UFBC_WDMA_ADDR0_MSB + baddr),
			readl(DISP_REG_UFBC_WDMA_ADDR1_MSB + baddr));
	} else {
		DDPDUMP("0x000:0x%08x 0x%08x 0x%08x 0x%08x\n",
			readl(DISP_REG_WDMA_INTEN + baddr),
			readl(DISP_REG_WDMA_INTSTA + baddr),
			readl(DISP_REG_WDMA_EN + baddr),
			readl(DISP_REG_WDMA_RST + baddr));

		DDPDUMP("0x010:0x%08x 0x%08x 0x%08x 0x%08x\n",
			readl(DISP_REG_WDMA_SMI_CON + baddr),
			readl(DISP_REG_WDMA_CFG + baddr),
			readl(DISP_REG_WDMA_SRC_SIZE + baddr),
			readl(DISP_REG_WDMA_CLIP_SIZE + baddr));

		DDPDUMP("0x020:0x%08x 0x%08x 0x%08x 0x%08x\n",
			readl(DISP_REG_WDMA_CLIP_COORD + baddr),
			readl(DISP_REG_WDMA_SHADOW_CTRL + baddr),
			readl(DISP_REG_WDMA_DST_WIN_BYTE + baddr),
			readl(DISP_REG_WDMA_ALPHA + baddr));

		DDPDUMP("0x030:0x%08x 0x034:0x%08x\n",
			readl(DISP_REG_WDMA_BKGD + baddr),
			readl(DISP_REG_WDMA_BKGD_MSB + baddr));

		DDPDUMP("0x038:0x%08x 0x078:0x%08x\n",
			readl(DISP_REG_WDMA_BUF_CON1 + baddr),
			readl(DISP_REG_WDMA_DST_UV_PITCH + baddr));

		DDPDUMP("0x080:0x%08x 0x%08x 0x%08x\n",
			readl(DISP_REG_WDMA_DST_ADDR_OFFSETX(0) + baddr),
			readl(DISP_REG_WDMA_DST_ADDR_OFFSETX(1) + baddr),
			readl(DISP_REG_WDMA_DST_ADDR_OFFSETX(2) + baddr));

		if (wdma->data->is_support_34bits)
			DDPDUMP("0xf30:0x%08x 0x%08x 0x%08x\n",
				readl(DISP_REG_WDMA_DST_ADDR_OFFSETX_MSB(0) + baddr),
				readl(DISP_REG_WDMA_DST_ADDR_OFFSETX_MSB(1) + baddr),
				readl(DISP_REG_WDMA_DST_ADDR_OFFSETX_MSB(2) + baddr));

		DDPDUMP("0x0a0:0x%08x 0x%08x 0x%08x 0x%08x\n",
			readl(DISP_REG_WDMA_FLOW_CTRL_DBG + baddr),
			readl(DISP_REG_WDMA_EXEC_DBG + baddr),
			readl(DISP_REG_WDMA_INPUT_CNT_DBG + baddr),
			readl(DISP_REG_WDMA_SMI_TRAFFIC_DBG + baddr));

		DDPDUMP("0x0b8:0x%08x\n",
			readl(DISP_REG_WDMA_DEBUG + baddr));

		DDPDUMP("0xf00:0x%08x 0x%08x 0x%08x\n",
			readl(DISP_REG_WDMA_DST_ADDRX(0) + baddr),
			readl(DISP_REG_WDMA_DST_ADDRX(1) + baddr),
			readl(DISP_REG_WDMA_DST_ADDRX(2) + baddr));

		if (wdma->data->is_support_34bits)
			DDPDUMP("0xf20:0x%08x 0x%08x 0x%08x\n",
				readl(DISP_REG_WDMA_DST_ADDRX_MSB(0) + baddr),
				readl(DISP_REG_WDMA_DST_ADDRX_MSB(1) + baddr),
				readl(DISP_REG_WDMA_DST_ADDRX_MSB(2) + baddr));
	}

	mtk_wdma_dump_golden_setting(comp);

	return 0;
}

static char *wdma_get_state(unsigned int status)
{
	switch (status) {
	case 0x1:
		return "idle";
	case 0x2:
		return "clear";
	case 0x4:
		return "prepare1";
	case 0x8:
		return "prepare2";
	case 0x10:
		return "data_transmit";
	case 0x20:
		return "eof_wait";
	case 0x40:
		return "soft_reset_wait";
	case 0x80:
		return "eof_done";
	case 0x100:
		return "soft_reset_done";
	case 0x200:
		return "frame_complete";
	}
	return "unknown-state";
}

int mtk_wdma_analysis(struct mtk_ddp_comp *comp)
{
	struct mtk_disp_wdma *wdma = comp_to_wdma(comp);
	void __iomem *baddr = comp->regs;

	if (!baddr) {
		DDPDUMP("%s, %s is NULL!\n", __func__, mtk_dump_comp_str(comp));
		return 0;
	}

	DDPDUMP("== DISP %s ANALYSIS:0x%pa ==\n", mtk_dump_comp_str(comp), &comp->regs_pa);

	if (comp->mtk_crtc && comp->mtk_crtc->sec_on) {
		DDPDUMP("Skip dump secure wdma!\n");
		return 0;
	}
	if (wdma->info_data->is_support_ufbc) {
		DDPDUMP("en=%d,dst=(%d,%d,%dx%d),bg(%dx%d)\n",
			readl(baddr + DISP_REG_UFBC_WDMA_EN) & 0x01,
			readl(baddr + DISP_REG_UFBC_WDMA_TILE_OFFSET) & 0xffff,
			(readl(baddr + DISP_REG_UFBC_WDMA_TILE_OFFSET) >> 16) & 0xffff,
			readl(baddr + DISP_REG_UFBC_WDMA_TILE_SIZE) & 0xffff,
			(readl(baddr + DISP_REG_UFBC_WDMA_TILE_SIZE) >> 16) & 0xffff,
			readl(baddr + DISP_REG_UFBC_WDMA_BG_SIZE) & 0xffff,
			(readl(baddr + DISP_REG_UFBC_WDMA_BG_SIZE) >> 16) & 0xffff);
		DDPDUMP("addr=0x%llx,payload offset:0x%x,format:%d,YUV trans:%d\n",
			read_dst_addr(comp, 0),
			readl(baddr + DISP_REG_UFBC_WDMA_PAYLOAD_OFFSET),
			readl(baddr + DISP_REG_UFBC_WDMA_FMT),
			REG_FLD_VAL_GET(UFBC_WDMA_AFBC_YUV_TRANSFORM,
					readl(baddr + DISP_REG_UFBC_WDMA_AFBC_SETTING)));
		return 0;
	}
	DDPDUMP("en=%d,src(%dx%d),clip=(%d,%d,%dx%d)\n",
		readl(baddr + DISP_REG_WDMA_EN) & 0x01,
		readl(baddr + DISP_REG_WDMA_SRC_SIZE) & 0x3fff,
		(readl(baddr + DISP_REG_WDMA_SRC_SIZE) >> 16) & 0x3fff,
		readl(baddr + DISP_REG_WDMA_CLIP_COORD) & 0x3fff,
		(readl(baddr + DISP_REG_WDMA_CLIP_COORD) >> 16) & 0x3fff,
		readl(baddr + DISP_REG_WDMA_CLIP_SIZE) & 0x3fff,
		(readl(baddr + DISP_REG_WDMA_CLIP_SIZE) >> 16) & 0x3fff);
	DDPDUMP("pitch=(W=%d,UV=%d),addr=(0x%llx,0x%llx,0x%llx),cfg=0x%x\n",
		readl(baddr + DISP_REG_WDMA_DST_WIN_BYTE),
		readl(baddr + DISP_REG_WDMA_DST_UV_PITCH),
		read_dst_addr(comp, 0),
		read_dst_addr(comp, 1),
		read_dst_addr(comp, 2),
		readl(baddr + DISP_REG_WDMA_CFG));
	DDPDUMP("state=%s,in_req=%d(prev sent data)\n",
		wdma_get_state(DISP_REG_GET_FIELD(
			FLOW_CTRL_DBG_FLD_WDMA_STA_FLOW_CTRL,
			baddr + DISP_REG_WDMA_FLOW_CTRL_DBG)),
		REG_FLD_VAL_GET(FLOW_CTRL_DBG_FLD_WDMA_IN_VALID,
				readl(baddr + DISP_REG_WDMA_FLOW_CTRL_DBG)));
	DDPDUMP("in_ack=%d(ask data to prev),start=%d,end=%d,pos:in(%d,%d)\n",
		REG_FLD_VAL_GET(FLOW_CTRL_DBG_FLD_WDMA_IN_READY,
				readl(baddr + DISP_REG_WDMA_FLOW_CTRL_DBG)),
		readl(baddr + DISP_REG_WDMA_EXEC_DBG) & 0x3f,
		readl(baddr + DISP_REG_WDMA_EXEC_DBG) >> 16 & 0x3f,
		readl(baddr + DISP_REG_WDMA_INPUT_CNT_DBG) & 0x3fff,
		(readl(baddr + DISP_REG_WDMA_INPUT_CNT_DBG) >> 16) & 0x3fff);

	return 0;
}

int MMPathTraceWDMA(struct mtk_ddp_comp *ddp_comp, char *str,
	unsigned int strlen, unsigned int n)
{
	struct mtk_disp_wdma *wdma = comp_to_wdma(ddp_comp);
	struct mtk_wdma_cfg_info *cfg_info = &wdma->cfg_info;

	n += scnprintf(str + n, strlen - n,
		"out=0x%lx, out_width=%d, out_height=%d, out_fmt=%s, out_bpp=%d",
		(unsigned long)cfg_info->addr,
		cfg_info->width,
		cfg_info->height,
		mtk_get_format_name(cfg_info->fmt),
		mtk_get_format_bpp(cfg_info->fmt));

	return n;
}

struct mtk_ddp_comp *mtk_disp_get_wdma_comp_by_scn(struct drm_crtc *crtc, enum addon_scenario scn)
{
	const struct mtk_addon_scenario_data *addon_data = NULL;
	const struct mtk_addon_module_data *addon_module = NULL;
	const struct mtk_addon_path_data *path_data = NULL;
	struct mtk_drm_private *priv = NULL;
	struct mtk_ddp_comp *comp = NULL;

	if (IS_ERR_OR_NULL(crtc))
		return NULL;

	priv = crtc->dev->dev_private;
	if (IS_ERR_OR_NULL(priv))
		return NULL;

	addon_data = mtk_addon_get_scenario_data(__func__, crtc, scn);
	if (IS_ERR_OR_NULL(addon_data))
		return NULL;

	addon_module = &addon_data->module_data[0];
	path_data = mtk_addon_module_get_path(addon_module->module);
	comp = priv->ddp_comp[path_data->path[path_data->path_len - 1]];

	if (IS_ERR_OR_NULL(comp)) {
		DDPMSG("%s, invalid wdma comp for scn:%d\n", __func__, scn);
		return NULL;
	}

	return comp;
}

static int mtk_wdma_io_cmd(struct mtk_ddp_comp *comp, struct cmdq_pkt *handle,
			  enum mtk_ddp_io_cmd cmd, void *params)
{
	struct mtk_disp_wdma *wdma = container_of(comp, struct mtk_disp_wdma, ddp_comp);
	struct drm_crtc *crtc;
	struct mtk_drm_crtc *mtk_crtc;
	struct mtk_drm_private *priv;
	int ret = 0;

	mtk_crtc = comp->mtk_crtc;
	if (!mtk_crtc)
		return -1;

	crtc = &mtk_crtc->base;
	if (!crtc)
		return -1;

	priv = crtc->dev->dev_private;
	if (!priv) {
		DDPMSG("%s:%d priv is NULL\n", __func__, __LINE__);
		return -1;
	}

	switch (cmd) {
	case WDMA_WRITE_DST_ADDR0:
	{
		dma_addr_t addr = *(dma_addr_t *)params;

		write_dst_addr(comp, handle, 0, addr);
		wdma->cfg_info.addr = addr;
	}
		break;
	case WDMA_READ_DST_SIZE:
	{
		unsigned int val, w, h;
		struct mtk_cwb_info *cwb_info = (struct mtk_cwb_info *)params;

		val = readl(comp->regs + DISP_REG_WDMA_CLIP_SIZE);
		w = val & 0x3fff;
		h = (val >> 16) & 0x3fff;
		cwb_info->copy_w = w;
		cwb_info->copy_h = h;
		DDPDBG("[capture] sof get (w,h)=(%d,%d)\n", w, h);
	}
		break;
	case IRQ_LEVEL_IDLE: {
		unsigned int offset = wdma->info_data->is_support_ufbc ?
				DISP_REG_UFBC_WDMA_INTEN : DISP_REG_WDMA_INTEN;

		mtk_ddp_write(comp, 0x0, offset, handle);
		break;
	}
	case IRQ_LEVEL_ALL: {
		unsigned int inten, offset;

		if (wdma->info_data->is_support_ufbc) {
			inten = REG_FLD_VAL(INTEN_FLD_FME_CPL_INTEN, 1);
			offset = DISP_REG_UFBC_WDMA_INTEN;
		} else {
			inten = REG_FLD_VAL(INTEN_FLD_FME_CPL_INTEN, 1) |
				REG_FLD_VAL(INTEN_FLD_FME_UND_INTEN, 1);
			offset = DISP_REG_WDMA_INTEN;
		}

		mtk_ddp_write(comp, inten, offset, handle);
		break;
	}
	case IRQ_LEVEL_NORMAL: {
		unsigned int inten, offset;

		if (wdma->info_data->is_support_ufbc) {
			inten = REG_FLD_VAL(INTEN_FLD_FME_CPL_INTEN, 1);
			offset = DISP_REG_UFBC_WDMA_INTEN;
		} else {
			inten = REG_FLD_VAL(INTEN_FLD_FME_CPL_INTEN, 1) |
				REG_FLD_VAL(INTEN_FLD_FME_UND_INTEN, 1);
			offset = DISP_REG_WDMA_INTEN;
		}

		mtk_ddp_write(comp, inten, offset, handle);
		break;
	}
	case PMQOS_GET_LARB_PORT_HRT_BW: {
		struct mtk_larb_port_bw *data = (struct mtk_larb_port_bw *)params;

		data->larb_id = -1;
		data->bw = 0;
		if (data->type != CHANNEL_HRT_RW)
			break;

		if (comp->larb_num == 1)
			data->larb_id = comp->larb_id;
		else if (comp->larb_num > 1)
			data->larb_id = comp->larb_ids[0];

		if (data->larb_id < 0) {
			DDPMSG("%s, comp:%d, invalid larb id:%d, num:%d\n",
				__func__, comp->id, data->larb_id, comp->larb_num);
			break;
		}
		if (priv->data->mmsys_id == MMSYS_MT6899 &&
			wdma->info_data->force_ostdl_bw &&
			!wdma->info_data->is_support_ufbc)
			data->bw = wdma->info_data->force_ostdl_bw;
		else
			data->bw = comp->hrt_bw;
		if (data->bw > 0)
			DDPDBG("%s, wdma comp:%d, larb:%d, bw:%d\n",
				__func__, comp->id, data->larb_id, data->bw);
		break;
	}
	case PMQOS_SET_HRT_BW: {
		unsigned int bw = *(unsigned int *)params;
		unsigned int ostdl_bw = 0;
		struct mtk_disp_wdma *wdma = comp_to_wdma(comp);

		if (!wdma || !mtk_drm_helper_get_opt(priv->helper_opt,
				MTK_DRM_OPT_MMQOS_SUPPORT))
			break;

		if (wdma->info_data->force_ostdl_bw &&
			!wdma->info_data->is_support_ufbc)
			ostdl_bw = wdma->info_data->force_ostdl_bw;
		else
			ostdl_bw = bw;

		if (comp->last_hrt_bw == ostdl_bw)
			break;

		if (priv->data->respective_ostdl) {
			if (!IS_ERR_OR_NULL(comp->hrt_qos_req)) {
				__mtk_disp_set_module_hrt(comp->hrt_qos_req, comp->id, ostdl_bw,
					priv->data->respective_ostdl);
				DDPINFO("%s: %s:%d update port hrt BW:%u->%u/%u,force:%d,bw:%u\n",
					__func__, mtk_dump_comp_str(comp), comp->id,
					comp->last_hrt_bw, comp->hrt_bw, ostdl_bw,
					wdma->info_data->force_ostdl_bw, bw);
				comp->last_hrt_bw = ostdl_bw;
			}
			if (wdma->data->hrt_channel)
				mtk_vidle_channel_bw_set(bw, wdma->data->hrt_channel(comp));
		}
		ret = WDMA_REQ_HRT;
		break;
	}
	//tempory solution: only for ufbc
	case PMQOS_UPDATE_BW: {
		struct drm_crtc *crtc;
		struct mtk_drm_crtc *mtk_crtc;
		unsigned int force_update = 0; /* force_update repeat last qos BW */
		unsigned int update_pending = 0;
		struct mtk_drm_private *priv;
		struct mtk_disp_wdma *wdma = comp_to_wdma(comp);

		if (!wdma->info_data->is_support_ufbc)
			break;

		priv = comp->mtk_crtc->base.dev->dev_private;

		if (!mtk_drm_helper_get_opt(priv->helper_opt, MTK_DRM_OPT_MMQOS_SUPPORT))
			break;

		mtk_crtc = comp->mtk_crtc;
		crtc = &mtk_crtc->base;

		if (params) {
			force_update = *(unsigned int *)params;
			/* tricky way use variable force update */
			update_pending = (force_update == DISP_BW_UPDATE_PENDING);
			force_update = (force_update == DISP_BW_FORCE_UPDATE) ? 1 : 0;
		}

		if (!force_update && !update_pending) {
			mtk_crtc->total_srt += comp->qos_bw;
		}

		/* process normal */
		if (!force_update && comp->last_qos_bw == comp->qos_bw)
			break;

		if (!IS_ERR_OR_NULL(comp->qos_req)) {
			__mtk_disp_set_module_srt(comp->qos_req, comp->id, comp->qos_bw, 0,
						  DISP_BW_NORMAL_MODE, priv->data->real_srt_ostdl);
			DDPINFO("%s: %s:%d update port srt BW:%u->%u\n", __func__,
				mtk_dump_comp_str(comp), comp->id, comp->last_qos_bw, comp->qos_bw);
			comp->last_qos_bw = comp->qos_bw;
		}
		break;
	}
	default:
		break;
	}
	return 0;
}

static const struct mtk_ddp_comp_funcs mtk_disp_wdma_funcs = {
	.config = mtk_wdma_config,
	.addon_config = mtk_wdma_addon_config,
	.start = mtk_wdma_start,
	.stop = mtk_wdma_stop,
	.prepare = mtk_wdma_prepare,
	.unprepare = mtk_wdma_unprepare,
	.is_busy = mtk_wdma_is_busy,
	.io_cmd = mtk_wdma_io_cmd,
};

static int mtk_disp_wdma_bind(struct device *dev, struct device *master,
			      void *data)
{
	struct mtk_disp_wdma *priv = dev_get_drvdata(dev);
	struct drm_device *drm_dev = data;
	struct mtk_drm_private *private = drm_dev->dev_private;
	int ret;
	char buf[64];

	ret = mtk_ddp_comp_register(drm_dev, &priv->ddp_comp);
	if (ret < 0) {
		dev_err(dev, "Failed to register component %s: %d\n",
			dev->of_node->full_name, ret);
		return ret;
	}
	if (mtk_drm_helper_get_opt(private->helper_opt,
			MTK_DRM_OPT_MMQOS_SUPPORT)) {
		mtk_disp_pmqos_get_icc_path_name(buf, sizeof(buf), &priv->ddp_comp, "qos");
		priv->ddp_comp.qos_req = of_mtk_icc_get(dev, buf);
		if (!IS_ERR(priv->ddp_comp.qos_req))
			DDPMSG("%s,%s create success, dev:%s\n", __func__, buf, dev_name(dev));
		else
			DDPMSG("%s,%d, comp:%u failed to create qos_req, name:%s\n",
				__func__, __LINE__, priv->ddp_comp.id, buf);

		mtk_disp_pmqos_get_icc_path_name(buf, sizeof(buf),
				&priv->ddp_comp, "hrt_qos");
		priv->ddp_comp.hrt_qos_req = of_mtk_icc_get(dev, buf);
		if (!IS_ERR(priv->ddp_comp.hrt_qos_req))
			DDPMSG("%s,%s create success, dev:%s\n", __func__, buf, dev_name(dev));
		else
			DDPMSG("%s,%d comp:%u failed to create hrt_qos_req, name:%s\n",
				__func__, __LINE__, priv->ddp_comp.id, buf);
	}
	return 0;
}

static void mtk_disp_wdma_unbind(struct device *dev, struct device *master,
				 void *data)
{
	struct mtk_disp_wdma *priv = dev_get_drvdata(dev);
	struct drm_device *drm_dev = data;

	mtk_ddp_comp_unregister(drm_dev, &priv->ddp_comp);
}

static const struct component_ops mtk_disp_wdma_component_ops = {
	.bind = mtk_disp_wdma_bind, .unbind = mtk_disp_wdma_unbind,
};

static int mtk_disp_wdma_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mtk_disp_wdma *priv;
	enum mtk_ddp_comp_id comp_id;
	const char *compatible;
	int irq;
	int ret;

	DDPMSG("%s+\n", __func__);
	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	comp_id = mtk_ddp_comp_get_id(dev->of_node, MTK_DISP_WDMA);
	if ((int)comp_id < 0) {
		dev_err(dev, "Failed to identify by alias: %d\n", comp_id);
		return comp_id;
	}

	ret = mtk_ddp_comp_init(dev, dev->of_node, &priv->ddp_comp, comp_id,
				&mtk_disp_wdma_funcs);
	if (ret) {
		dev_err(dev, "Failed to initialize component: %d\n", ret);
		return ret;
	}

	platform_set_drvdata(pdev, priv);

	ret = devm_request_irq(dev, irq, mtk_wdma_irq_handler,
			       IRQF_TRIGGER_NONE | IRQF_SHARED, dev_name(dev),
			       priv);
	if (ret < 0) {
		DDPAEE("%s:%d, failed to request irq:%d ret:%d comp_id:%d\n",
				__func__, __LINE__,
				irq, ret, comp_id);
		return ret;
	}

	priv->data = of_device_get_match_data(dev);
	priv->info_data = devm_kzalloc(dev, sizeof(*priv->info_data), GFP_KERNEL);

	if (priv->info_data == NULL) {
		DDPPR_ERR("priv->info_data is NULL\n");
		return -1;
	}

	compatible = of_get_property(dev->of_node, "compatible", NULL);
	if (!IS_ERR_OR_NULL(compatible)) {
		if (strstr(compatible, "ufbc"))
			priv->info_data->is_support_ufbc = true;
	}

	priv->info_data->fifo_size_1plane = priv->data->fifo_size_1plane;
	priv->info_data->fifo_size_uv_1plane = priv->data->fifo_size_uv_1plane;
	priv->info_data->fifo_size_2plane = priv->data->fifo_size_2plane;
	priv->info_data->fifo_size_uv_2plane = priv->data->fifo_size_uv_2plane;
	priv->info_data->fifo_size_3plane = priv->data->fifo_size_3plane;
	priv->info_data->fifo_size_uv_3plane = priv->data->fifo_size_uv_3plane;
	priv->info_data->force_ostdl_bw = priv->data->force_ostdl_bw;
	priv->info_data->buf_con1_fld_fifo_pseudo_size =
			priv->data->buf_con1_fld_fifo_pseudo_size;
	priv->info_data->buf_con1_fld_fifo_pseudo_size_uv =
			priv->data->buf_con1_fld_fifo_pseudo_size_uv;

	if (priv->data->fifo_size_1plane == PARSE_FROM_DTS) {
		ret = of_property_read_u32(dev->of_node,
				"fifo-size-1plane", &(priv->info_data->fifo_size_1plane));
		if (ret) {
			DDPPR_ERR("Failed to parse fifo-size-1plane parse failed from dts\n");
			return -1;
		}
	}
	if (priv->data->fifo_size_uv_1plane == PARSE_FROM_DTS) {
		ret = of_property_read_u32(dev->of_node,
				"fifo-size-uv-1plane", &(priv->info_data->fifo_size_uv_1plane));
		if (ret) {
			DDPPR_ERR("Failed to parse fifo-size-uv-1plane from dts\n");
			return -1;
		}
	}
	if (priv->data->fifo_size_2plane == PARSE_FROM_DTS) {
		ret = of_property_read_u32(dev->of_node,
				"fifo-size-2plane", &(priv->info_data->fifo_size_2plane));
		if (ret) {
			DDPPR_ERR("Failed to parse fifo-size-2plane from dts\n");
			return -1;
		}
	}
	if (priv->data->fifo_size_uv_2plane == PARSE_FROM_DTS) {
		ret = of_property_read_u32(dev->of_node,
				"fifo-size-uv-2plane", &(priv->info_data->fifo_size_uv_2plane));
		if (ret) {
			DDPPR_ERR("Failed to parse fifo-size-uv-2plane from dts\n");
			return -1;
		}
	}
	if (priv->data->fifo_size_3plane == PARSE_FROM_DTS) {
		ret = of_property_read_u32(dev->of_node,
				"fifo-size-3plane", &(priv->info_data->fifo_size_3plane));
		if (ret) {
			DDPPR_ERR("Failed to parse fifo-size-3plane from dts\n");
			return -1;
		}
	}
	if (priv->data->fifo_size_uv_3plane == PARSE_FROM_DTS) {
		ret = of_property_read_u32(dev->of_node,
				"fifo-size-uv-3plane", &(priv->info_data->fifo_size_uv_3plane));
		if (ret) {
			DDPPR_ERR("Failed to parse fifo-size-uv-3plane from dts\n");
			return -1;
		}

	}
	mtk_ddp_comp_pm_enable(&priv->ddp_comp);

	ret = component_add(dev, &mtk_disp_wdma_component_ops);
	if (ret != 0) {
		dev_err(dev, "Failed to add component: %d\n", ret);
		mtk_ddp_comp_pm_disable(&priv->ddp_comp);
	}

	DDPMSG("%s-id:%d\n", __func__, priv->ddp_comp.id);

	return ret;
}

static int mtk_disp_wdma_remove(struct platform_device *pdev)
{
	struct mtk_disp_wdma *priv = dev_get_drvdata(&pdev->dev);

	component_del(&pdev->dev, &mtk_disp_wdma_component_ops);
	mtk_ddp_comp_pm_disable(&priv->ddp_comp);

	return 0;
}

static const struct mtk_disp_wdma_data mt6779_wdma_driver_data = {
	.sodi_config = mt6779_mtk_sodi_config,
	.support_shadow = false,
	.need_bypass_shadow = false,
	.is_support_34bits = false,
};

static const struct mtk_disp_wdma_data mt6768_wdma_driver_data = {
	.fifo_size_1plane = 325,
	.fifo_size_uv_1plane = 31,
	.fifo_size_2plane = 228,
	.fifo_size_uv_2plane = 109,
	.fifo_size_3plane = 228,
	.fifo_size_uv_3plane = 50,
	.sodi_config = mt6768_mtk_sodi_config,
	.support_shadow = false,
	.need_bypass_shadow = false,
	.is_support_34bits = false,
};

static const struct mtk_disp_wdma_data mt6761_wdma_driver_data = {
	.fifo_size_1plane = 325,
	.fifo_size_uv_1plane = 31,
	.fifo_size_2plane = 228,
	.fifo_size_uv_2plane = 109,
	.fifo_size_3plane = 228,
	.fifo_size_uv_3plane = 50,
	.sodi_config = mt6768_mtk_sodi_config,
	.support_shadow = false,
	.need_bypass_shadow = false,
	.is_support_34bits = false,
};

static const struct mtk_disp_wdma_data mt6765_wdma_driver_data = {
	.fifo_size_1plane = 325,
	.fifo_size_uv_1plane = 31,
	.fifo_size_2plane = 228,
	.fifo_size_uv_2plane = 109,
	.fifo_size_3plane = 228,
	.fifo_size_uv_3plane = 50,
	.sodi_config = mt6768_mtk_sodi_config,
	.support_shadow = false,
	.need_bypass_shadow = true,
	.is_support_34bits = false,
};

static const struct mtk_disp_wdma_data mt6885_wdma_driver_data = {
	.fifo_size_1plane = 325,
	.fifo_size_uv_1plane = 31,
	.fifo_size_2plane = 228,
	.fifo_size_uv_2plane = 109,
	.fifo_size_3plane = 228,
	.fifo_size_uv_3plane = 50,
	.sodi_config = mt6885_mtk_sodi_config,
	.support_shadow = false,
	.need_bypass_shadow = false,
	.is_support_34bits = false,
};

static const struct mtk_disp_wdma_data mt6873_wdma_driver_data = {
	.fifo_size_1plane = 578,
	.fifo_size_uv_1plane = 29,
	.fifo_size_2plane = 402,
	.fifo_size_uv_2plane = 201,
	.fifo_size_3plane = 402,
	.fifo_size_uv_3plane = 99,
	.sodi_config = mt6873_mtk_sodi_config,
	.support_shadow = false,
	.need_bypass_shadow = true,
	.is_support_34bits = false,
};

static const struct mtk_disp_wdma_data mt6853_wdma_driver_data = {
	.fifo_size_1plane = 578,
	.fifo_size_uv_1plane = 29,
	.fifo_size_2plane = 402,
	.fifo_size_uv_2plane = 201,
	.fifo_size_3plane = 402,
	.fifo_size_uv_3plane = 99,
	.sodi_config = mt6853_mtk_sodi_config,
	.support_shadow = false,
	.need_bypass_shadow = true,
	.is_support_34bits = false,
};

static const struct mtk_disp_wdma_data mt6833_wdma_driver_data = {
	.fifo_size_1plane = 578,
	.fifo_size_uv_1plane = 29,
	.fifo_size_2plane = 402,
	.fifo_size_uv_2plane = 201,
	.fifo_size_3plane = 402,
	.fifo_size_uv_3plane = 99,
	.sodi_config = mt6833_mtk_sodi_config,
	.support_shadow = false,
	.need_bypass_shadow = true,
	.is_support_34bits = false,
};

static const struct mtk_disp_wdma_data mt6877_wdma_driver_data = {
	.fifo_size_1plane = 578,
	.fifo_size_uv_1plane = 29,
	.fifo_size_2plane = 402,
	.fifo_size_uv_2plane = 201,
	.fifo_size_3plane = 402,
	.fifo_size_uv_3plane = 99,
	.sodi_config = mt6877_mtk_sodi_config,
	.support_shadow = false,
	.need_bypass_shadow = true,
	.is_support_34bits = false,
};

static const struct mtk_disp_wdma_data mt6781_wdma_driver_data = {
	.fifo_size_1plane = 410,
	.fifo_size_uv_1plane = 200,
	.fifo_size_2plane = 402,
	.fifo_size_uv_2plane = 201,
	.fifo_size_3plane = 402,
	.fifo_size_uv_3plane = 99,
	.sodi_config = mt6781_mtk_sodi_config,
	.support_shadow = false,
	.need_bypass_shadow = true,
	.is_support_34bits = false,
};

static const struct mtk_disp_wdma_data mt6879_wdma_driver_data = {
	.fifo_size_1plane = 465,
	.fifo_size_uv_1plane = 29,
	.fifo_size_2plane = 305,
	.fifo_size_uv_2plane = 152,
	.fifo_size_3plane = 302,
	.fifo_size_uv_3plane = 74,
	.sodi_config = mt6879_mtk_sodi_config,
	.aid_sel = &mtk_wdma_aid_sel_MT6879,
	.support_shadow = false,
	.need_bypass_shadow = true,
	.is_support_34bits = true,
	.use_larb_control_sec = false,
};

static const struct mtk_disp_wdma_data mt6855_wdma_driver_data = {
	.fifo_size_1plane = 578,
	.fifo_size_uv_1plane = 29,
	.fifo_size_2plane = 402,
	.fifo_size_uv_2plane = 201,
	.fifo_size_3plane = 402,
	.fifo_size_uv_3plane = 99,
	.sodi_config = mt6853_mtk_sodi_config,
	.support_shadow = false,
	.need_bypass_shadow = true,
	.is_support_34bits = false,
};

static const struct mtk_disp_wdma_data mt6983_wdma_driver_data = {
	.fifo_size_1plane = 905,
	.fifo_size_uv_1plane = 29,
	.fifo_size_2plane = 599,
	.fifo_size_uv_2plane = 299,
	.fifo_size_3plane = 596,
	.fifo_size_uv_3plane = 148,
	.sodi_config = mt6983_mtk_sodi_config,
	.check_wdma_sec_reg = &mtk_wdma_check_sec_reg_MT6983,
	.support_shadow = false,
	.need_bypass_shadow = true,
	.is_support_34bits = true,
	.use_larb_control_sec = true,
};

static const struct mtk_disp_wdma_data mt6895_wdma_driver_data = {
	.fifo_size_1plane = 905,
	.fifo_size_uv_1plane = 29,
	.fifo_size_2plane = 599,
	.fifo_size_uv_2plane = 299,
	.fifo_size_3plane = 596,
	.fifo_size_uv_3plane = 148,
	.sodi_config = mt6895_mtk_sodi_config,
	.aid_sel = &mtk_wdma_aid_sel_MT6895,
	.support_shadow = false,
	.need_bypass_shadow = true,
	.is_support_34bits = true,
	.use_larb_control_sec = false,
};

static const struct mtk_disp_wdma_data mt6886_wdma_driver_data = {
	.fifo_size_1plane = PARSE_FROM_DTS,
	.fifo_size_uv_1plane = PARSE_FROM_DTS,
	.fifo_size_2plane = PARSE_FROM_DTS,
	.fifo_size_uv_2plane = PARSE_FROM_DTS,
	.fifo_size_3plane = PARSE_FROM_DTS,
	.fifo_size_uv_3plane = PARSE_FROM_DTS,
	/* sodi is same as mt6895 */
	.sodi_config = mt6895_mtk_sodi_config,
	.check_wdma_sec_reg = &mtk_wdma_check_sec_reg_MT6886,
	.support_shadow = false,
	.need_bypass_shadow = true,
	.is_support_34bits = true,
	.use_larb_control_sec = true,
};

static const struct mtk_disp_wdma_data mt6985_wdma_driver_data = {
	.fifo_size_1plane = PARSE_FROM_DTS,
	.fifo_size_uv_1plane = 29,
	.fifo_size_2plane = PARSE_FROM_DTS,
	.fifo_size_uv_2plane = PARSE_FROM_DTS,
	.fifo_size_3plane = PARSE_FROM_DTS,
	.fifo_size_uv_3plane = PARSE_FROM_DTS,
	.sodi_config = mt6985_mtk_sodi_config,
	.check_wdma_sec_reg = &mtk_wdma_check_sec_reg_MT6985,
	.support_shadow = false,
	.need_bypass_shadow = true,
	.is_support_34bits = true,
	.use_larb_control_sec = true,
};

static const struct mtk_disp_wdma_data mt6989_wdma_driver_data = {
	.fifo_size_1plane = PARSE_FROM_DTS,
	.fifo_size_uv_1plane = PARSE_FROM_DTS,
	.fifo_size_2plane = PARSE_FROM_DTS,
	.fifo_size_uv_2plane = PARSE_FROM_DTS,
	.fifo_size_3plane = PARSE_FROM_DTS,
	.fifo_size_uv_3plane = PARSE_FROM_DTS,
	.sodi_config = mt6989_mtk_sodi_config,
	.aid_sel = &mtk_wdma_aid_sel_MT6989,
	.check_wdma_sec_reg = &mtk_wdma_check_sec_reg_MT6989,
	.support_shadow = false,
	.need_bypass_shadow = true,
	.is_support_34bits = true,
	.use_larb_control_sec = false,
};

static const struct mtk_disp_wdma_data mt6899_wdma_driver_data = {
	.fifo_size_1plane = PARSE_FROM_DTS,
	.fifo_size_uv_1plane = PARSE_FROM_DTS,
	.fifo_size_2plane = PARSE_FROM_DTS,
	.fifo_size_uv_2plane = PARSE_FROM_DTS,
	.fifo_size_3plane = PARSE_FROM_DTS,
	.fifo_size_uv_3plane = PARSE_FROM_DTS,
	.force_ostdl_bw = 3000,
	.buf_con1_fld_fifo_pseudo_size = REG_FLD_MSB_LSB(11, 0),
	.buf_con1_fld_fifo_pseudo_size_uv = REG_FLD_MSB_LSB(22, 12),
	.sodi_config = mt6989_mtk_sodi_config,
	.aid_sel = &mtk_wdma_aid_sel_MT6989,
	.check_wdma_sec_reg = &mtk_wdma_check_sec_reg_MT6989,
	.support_shadow = false,
	.need_bypass_shadow = true,
	.is_support_34bits = true,
	.use_larb_control_sec = false,
};

static const struct mtk_disp_wdma_data mt6991_wdma_driver_data = {
	.fifo_size_1plane = PARSE_FROM_DTS,
	.fifo_size_uv_1plane = 29,
	.fifo_size_2plane = PARSE_FROM_DTS,
	.fifo_size_uv_2plane = PARSE_FROM_DTS,
	.fifo_size_3plane = PARSE_FROM_DTS,
	.fifo_size_uv_3plane = PARSE_FROM_DTS,
	.force_ostdl_bw = 7000,
	.buf_con1_fld_fifo_pseudo_size = REG_FLD_MSB_LSB(11, 0),
	.buf_con1_fld_fifo_pseudo_size_uv = REG_FLD_MSB_LSB(22, 12),
	.sodi_config = mt6989_mtk_sodi_config,
	.aid_sel = &mtk_wdma_aid_sel_MT6991,
	.check_wdma_sec_reg = &mtk_wdma_check_sec_reg_MT6989,
	.support_shadow = false,
	.need_bypass_shadow = true,
	.is_support_34bits = true,
	.use_larb_control_sec = false,
	.hrt_channel = &mtk_wdma_hrt_channel_MT6991,
};

static const struct mtk_disp_wdma_data mt6897_wdma_driver_data = {
	.fifo_size_1plane = PARSE_FROM_DTS,
	.fifo_size_uv_1plane = 29,
	.fifo_size_2plane = PARSE_FROM_DTS,
	.fifo_size_uv_2plane = PARSE_FROM_DTS,
	.fifo_size_3plane = PARSE_FROM_DTS,
	.fifo_size_uv_3plane = PARSE_FROM_DTS,
	.sodi_config = mt6985_mtk_sodi_config,
	.check_wdma_sec_reg = &mtk_wdma_check_sec_reg_MT6897,
	.support_shadow = false,
	.need_bypass_shadow = true,
	.is_support_34bits = true,
	.use_larb_control_sec = true,
	.is_right_wdma_comp = &is_right_wdma_comp_MT6897,
};

static const struct of_device_id mtk_disp_wdma_driver_dt_match[] = {
	{.compatible = "mediatek,mt2701-disp-wdma"},
	{.compatible = "mediatek,mt6779-disp-wdma",
	 .data = &mt6779_wdma_driver_data},
	{.compatible = "mediatek,mt6761-disp-wdma",
	 .data = &mt6761_wdma_driver_data},
	{.compatible = "mediatek,mt6765-disp-wdma",
	 .data = &mt6765_wdma_driver_data},
	{.compatible = "mediatek,mt6768-disp-wdma",
	 .data = &mt6768_wdma_driver_data},
	{.compatible = "mediatek,mt8173-disp-wdma"},
	{.compatible = "mediatek,mt6885-disp-wdma",
	 .data = &mt6885_wdma_driver_data},
	{.compatible = "mediatek,mt6873-disp-wdma",
	 .data = &mt6873_wdma_driver_data},
	{.compatible = "mediatek,mt6853-disp-wdma",
	 .data = &mt6853_wdma_driver_data},
	{.compatible = "mediatek,mt6833-disp-wdma",
	 .data = &mt6833_wdma_driver_data},
	{.compatible = "mediatek,mt6877-disp-wdma",
	 .data = &mt6877_wdma_driver_data},
	{.compatible = "mediatek,mt6781-disp-wdma",
	 .data = &mt6781_wdma_driver_data},
	{.compatible = "mediatek,mt6879-disp-wdma",
	 .data = &mt6879_wdma_driver_data},
	{.compatible = "mediatek,mt6983-disp-wdma",
	 .data = &mt6983_wdma_driver_data},
	{.compatible = "mediatek,mt6895-disp-wdma",
	 .data = &mt6895_wdma_driver_data},
	{.compatible = "mediatek,mt6886-disp-wdma",
	 .data = &mt6886_wdma_driver_data},
	{.compatible = "mediatek,mt6855-disp-wdma",
	 .data = &mt6855_wdma_driver_data},
	{.compatible = "mediatek,mt6985-disp-wdma",
	 .data = &mt6985_wdma_driver_data},
	{.compatible = "mediatek,mt6897-disp-wdma",
	 .data = &mt6897_wdma_driver_data},
	{.compatible = "mediatek,mt6835-disp-wdma",
	 .data = &mt6835_wdma_driver_data},
	{.compatible = "mediatek,mt6989-disp-wdma",
	 .data = &mt6989_wdma_driver_data},
	{.compatible = "mediatek,mt6899-disp-wdma",
	 .data = &mt6899_wdma_driver_data},
	{.compatible = "mediatek,mt6991-disp-wdma",
	 .data = &mt6991_wdma_driver_data},
	{},
};
MODULE_DEVICE_TABLE(of, mtk_disp_wdma_driver_dt_match);

struct platform_driver mtk_disp_wdma_driver = {
	.probe = mtk_disp_wdma_probe,
	.remove = mtk_disp_wdma_remove,
	.driver = {

			.name = "mediatek-disp-wdma",
			.owner = THIS_MODULE,
			.of_match_table = mtk_disp_wdma_driver_dt_match,
		},
};
