// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2021 MediaTek Inc.
 */

#include "mtk_drm_ddp_comp.h"
#include "mtk_dump.h"

static const char * const ddp_comp_str[] = {DECLARE_DDP_COMP(DECLARE_STR)};

const char *mtk_dump_comp_str(struct mtk_ddp_comp *comp)
{
	if (IS_ERR_OR_NULL(comp)) {
		DDPPR_ERR("%s: Invalid ddp comp\n", __func__);
		return "invalid";
	}
	if (comp->id < 0 || comp->id >= DDP_COMPONENT_ID_MAX) {
		DDPPR_ERR("%s: Invalid ddp comp id:%d\n", __func__, comp->id);
		return "invalid";
	}
	return ddp_comp_str[comp->id]+14;	/* skip prefix DDP_COMPONENT_ for shorter log */
}

const char *mtk_dump_comp_str_id(unsigned int id)
{
	if (likely(id < DDP_COMPONENT_ID_MAX))
		return ddp_comp_str[id]+14;	/* skip prefix DDP_COMPONENT_ for shorter log */

	return "invalid";
}

void mtk_dump_cur_pos(struct mtk_ddp_comp *comp)
{
	switch (comp->id) {
	case DDP_COMPONENT_OVL0:
	case DDP_COMPONENT_OVL1:
	case DDP_COMPONENT_OVL0_2L:
	case DDP_COMPONENT_OVL1_2L:
	case DDP_COMPONENT_OVL2_2L:
	case DDP_COMPONENT_OVL3_2L:
	case DDP_COMPONENT_OVL4_2L:
	case DDP_COMPONENT_OVL5_2L:
	case DDP_COMPONENT_OVL6_2L:
	case DDP_COMPONENT_OVL7_2L:
	case DDP_COMPONENT_OVL0_2L_NWCG:
	case DDP_COMPONENT_OVL1_2L_NWCG:
	case DDP_COMPONENT_OVL2_2L_NWCG:
	case DDP_COMPONENT_OVL3_2L_NWCG:
		mtk_ovl_cur_pos_dump(comp);
		break;
	case DDP_COMPONENT_DSI0:
	case DDP_COMPONENT_DSI1:
		mtk_dsi_cur_pos_dump(comp);
		break;
	default:
		return;
	}
}

int mtk_dump_reg(struct mtk_ddp_comp *comp)
{
	switch (comp->id) {
	case DDP_COMPONENT_VDISP_AO:
		mtk_vdisp_ao_dump(comp);
		break;
	case DDP_COMPONENT_OVL_EXDMA0:
	case DDP_COMPONENT_OVL_EXDMA1:
	case DDP_COMPONENT_OVL_EXDMA2:
	case DDP_COMPONENT_OVL_EXDMA3:
	case DDP_COMPONENT_OVL_EXDMA4:
	case DDP_COMPONENT_OVL_EXDMA5:
	case DDP_COMPONENT_OVL_EXDMA6:
	case DDP_COMPONENT_OVL_EXDMA7:
	case DDP_COMPONENT_OVL_EXDMA8:
	case DDP_COMPONENT_OVL_EXDMA9:
	case DDP_COMPONENT_OVL1_EXDMA0:
	case DDP_COMPONENT_OVL1_EXDMA1:
	case DDP_COMPONENT_OVL1_EXDMA2:
	case DDP_COMPONENT_OVL1_EXDMA3:
	case DDP_COMPONENT_OVL1_EXDMA4:
	case DDP_COMPONENT_OVL1_EXDMA5:
	case DDP_COMPONENT_OVL1_EXDMA6:
	case DDP_COMPONENT_OVL1_EXDMA7:
	case DDP_COMPONENT_OVL1_EXDMA8:
	case DDP_COMPONENT_OVL1_EXDMA9:
		mtk_ovl_exdma_dump(comp);
		break;
	case DDP_COMPONENT_OVL0_OUTPROC0:
	case DDP_COMPONENT_OVL0_OUTPROC1:
	case DDP_COMPONENT_OVL0_OUTPROC2:
	case DDP_COMPONENT_OVL0_OUTPROC3:
	case DDP_COMPONENT_OVL0_OUTPROC4:
	case DDP_COMPONENT_OVL0_OUTPROC5:
	case DDP_COMPONENT_OVL1_OUTPROC0:
	case DDP_COMPONENT_OVL1_OUTPROC1:
	case DDP_COMPONENT_OVL1_OUTPROC2:
	case DDP_COMPONENT_OVL1_OUTPROC3:
	case DDP_COMPONENT_OVL1_OUTPROC4:
	case DDP_COMPONENT_OVL1_OUTPROC5:
		mtk_ovl_outproc_dump(comp);
		break;
	case DDP_COMPONENT_OVL0_BLENDER0:
	case DDP_COMPONENT_OVL0_BLENDER1:
	case DDP_COMPONENT_OVL0_BLENDER2:
	case DDP_COMPONENT_OVL0_BLENDER3:
	case DDP_COMPONENT_OVL0_BLENDER4:
	case DDP_COMPONENT_OVL0_BLENDER5:
	case DDP_COMPONENT_OVL0_BLENDER6:
	case DDP_COMPONENT_OVL0_BLENDER7:
	case DDP_COMPONENT_OVL0_BLENDER8:
	case DDP_COMPONENT_OVL0_BLENDER9:
	case DDP_COMPONENT_OVL1_BLENDER0:
	case DDP_COMPONENT_OVL1_BLENDER1:
	case DDP_COMPONENT_OVL1_BLENDER2:
	case DDP_COMPONENT_OVL1_BLENDER3:
	case DDP_COMPONENT_OVL1_BLENDER4:
	case DDP_COMPONENT_OVL1_BLENDER5:
	case DDP_COMPONENT_OVL1_BLENDER6:
	case DDP_COMPONENT_OVL1_BLENDER7:
	case DDP_COMPONENT_OVL1_BLENDER8:
	case DDP_COMPONENT_OVL1_BLENDER9:
		mtk_ovl_blender_dump(comp);
		break;
	case DDP_COMPONENT_OVL0:
	case DDP_COMPONENT_OVL1:
	case DDP_COMPONENT_OVL0_2L:
	case DDP_COMPONENT_OVL1_2L:
	case DDP_COMPONENT_OVL2_2L:
	case DDP_COMPONENT_OVL3_2L:
	case DDP_COMPONENT_OVL4_2L:
	case DDP_COMPONENT_OVL5_2L:
	case DDP_COMPONENT_OVL6_2L:
	case DDP_COMPONENT_OVL7_2L:
	case DDP_COMPONENT_OVL0_2L_NWCG:
	case DDP_COMPONENT_OVL1_2L_NWCG:
	case DDP_COMPONENT_OVL2_2L_NWCG:
	case DDP_COMPONENT_OVL3_2L_NWCG:
		mtk_ovl_dump(comp);
		break;
	case DDP_COMPONENT_RDMA0:
	case DDP_COMPONENT_RDMA1:
	case DDP_COMPONENT_RDMA2:
	case DDP_COMPONENT_RDMA3:
	case DDP_COMPONENT_RDMA4:
	case DDP_COMPONENT_RDMA5:
		mtk_rdma_dump(comp);
		break;
	case DDP_COMPONENT_MDP_RDMA0:
	case DDP_COMPONENT_MDP_RDMA1:
		mtk_mdp_rdma_dump(comp);
		break;
	case DDP_COMPONENT_WDMA0:
	case DDP_COMPONENT_WDMA1:
	case DDP_COMPONENT_WDMA2:
	case DDP_COMPONENT_WDMA3:
	case DDP_COMPONENT_UFBC_WDMA0:
	case DDP_COMPONENT_UFBC_WDMA1:
	case DDP_COMPONENT_UFBC_WDMA2:
	case DDP_COMPONENT_UFBC_WDMA3:
	case DDP_COMPONENT_OVLSYS_UFBC_WDMA0:
	case DDP_COMPONENT_OVLSYS_WDMA0:
	case DDP_COMPONENT_OVLSYS_WDMA1:
	case DDP_COMPONENT_OVLSYS_WDMA2:
	case DDP_COMPONENT_OVLSYS_WDMA3:
		mtk_wdma_dump(comp);
		break;
	case DDP_COMPONENT_RSZ0:
	case DDP_COMPONENT_RSZ1:
	case DDP_COMPONENT_RSZ2:
	case DDP_COMPONENT_OVLSYS_RSZ1:
	case DDP_COMPONENT_OVLSYS_RSZ2:
		mtk_rsz_dump(comp);
		break;
	case DDP_COMPONENT_MDP_RSZ0:
	case DDP_COMPONENT_MDP_RSZ1:
	case DDP_COMPONENT_OVL0_MDP_RSZ0:
	case DDP_COMPONENT_OVL0_MDP_RSZ1:
	case DDP_COMPONENT_OVL1_MDP_RSZ0:
	case DDP_COMPONENT_OVL1_MDP_RSZ1:
		mtk_mdp_rsz_dump(comp);
		break;
	case DDP_COMPONENT_DSI0:
	case DDP_COMPONENT_DSI1:
	case DDP_COMPONENT_DSI2:
		mtk_dsi_dump(comp);
		break;
	case DDP_COMPONENT_DP_INTF0:
	case DDP_COMPONENT_DP_INTF1:
		mtk_dp_intf_dump(comp);
		break;

	case DDP_COMPONENT_TDSHP0:
	case DDP_COMPONENT_TDSHP1:
	case DDP_COMPONENT_TDSHP2:
	case DDP_COMPONENT_TDSHP3:
		disp_tdshp_dump(comp);
		break;

	case DDP_COMPONENT_COLOR0:
	case DDP_COMPONENT_COLOR1:
	case DDP_COMPONENT_COLOR2:
		disp_color_dump(comp);
		break;
	case DDP_COMPONENT_CCORR0:
	case DDP_COMPONENT_CCORR1:
	case DDP_COMPONENT_CCORR2:
	case DDP_COMPONENT_CCORR3:
		disp_ccorr_dump(comp);
		break;
	case DDP_COMPONENT_C3D0:
	case DDP_COMPONENT_C3D1:
		disp_c3d_dump(comp);
		break;
	case DDP_COMPONENT_AAL0:
	case DDP_COMPONENT_AAL1:
		disp_aal_dump(comp);
		break;
	case DDP_COMPONENT_DMDP_AAL0:
	case DDP_COMPONENT_DMDP_AAL1:
		disp_mdp_aal_dump(comp);
		break;
	case DDP_COMPONENT_DITHER0:
	case DDP_COMPONENT_DITHER1:
	case DDP_COMPONENT_DITHER2:
	case DDP_COMPONENT_DITHER3:
		disp_dither_dump(comp);
		break;
	case DDP_COMPONENT_GAMMA0:
	case DDP_COMPONENT_GAMMA1:
		disp_gamma_dump(comp);
		break;
	case DDP_COMPONENT_POSTMASK0:
	case DDP_COMPONENT_POSTMASK1:
		mtk_postmask_dump(comp);
		break;
	case DDP_COMPONENT_CM0:
		mtk_cm_dump(comp);
		break;
	case DDP_COMPONENT_SPR0:
	case DDP_COMPONENT_SPR1:
		mtk_spr_dump(comp);
		break;
	case DDP_COMPONENT_POSTALIGN0:
		mtk_postalign_dump(comp);
		break;
	case DDP_COMPONENT_DSC0:
	case DDP_COMPONENT_DSC1:
		mtk_dsc_dump(comp);
		break;
	case DDP_COMPONENT_MERGE0:
	case DDP_COMPONENT_MERGE1:
	case DDP_COMPONENT_MERGE2:
	case DDP_COMPONENT_MERGE3:
		mtk_merge_dump(comp);
		break;
	case DDP_COMPONENT_CHIST0:
	case DDP_COMPONENT_CHIST1:
		disp_chist_dump(comp);
		break;
	case DDP_COMPONENT_OVLSYS_Y2R0:
	case DDP_COMPONENT_OVLSYS_Y2R2:
	case DDP_COMPONENT_Y2R0:
	case DDP_COMPONENT_Y2R1:
		mtk_y2r_dump(comp);
		break;
	case DDP_COMPONENT_R2Y0:
		mtk_r2y_dump(comp);
		break;
	case DDP_COMPONENT_DLO_ASYNC3:
	case DDP_COMPONENT_DLO_ASYNC7:
	case DDP_COMPONENT_OVLSYS_DLO_ASYNC0:
		mtk_dlo_async_dump(comp);
		break;
	case DDP_COMPONENT_DLI_ASYNC3:
	case DDP_COMPONENT_DLI_ASYNC7:
	case DDP_COMPONENT_OVLSYS_DLI_ASYNC0:
		mtk_dli_async_dump(comp);
		break;
	case DDP_COMPONENT_INLINE_ROTATE0:
	case DDP_COMPONENT_INLINE_ROTATE1:
		mtk_inlinerotate_dump(comp);
		break;
	case DDP_COMPONENT_MMLSYS_BYPASS:
		mtk_mmlsys_bypass_dump(comp);
		break;
	case DDP_COMPONENT_ODDMR0:
	case DDP_COMPONENT_ODDMR1:
		mtk_oddmr_dump(comp);
		break;
	default:
		return 0;
	}

	return 0;
}

int mtk_dump_analysis(struct mtk_ddp_comp *comp)
{
	switch (comp->id) {
	case DDP_COMPONENT_OVL0:
	case DDP_COMPONENT_OVL1:
	case DDP_COMPONENT_OVL0_2L:
	case DDP_COMPONENT_OVL1_2L:
	case DDP_COMPONENT_OVL2_2L:
	case DDP_COMPONENT_OVL3_2L:
	case DDP_COMPONENT_OVL4_2L:
	case DDP_COMPONENT_OVL5_2L:
	case DDP_COMPONENT_OVL6_2L:
	case DDP_COMPONENT_OVL7_2L:
	case DDP_COMPONENT_OVL0_2L_NWCG:
	case DDP_COMPONENT_OVL1_2L_NWCG:
	case DDP_COMPONENT_OVL2_2L_NWCG:
	case DDP_COMPONENT_OVL3_2L_NWCG:
		mtk_ovl_analysis(comp);
		break;
	case DDP_COMPONENT_OVL_EXDMA0:
	case DDP_COMPONENT_OVL_EXDMA1:
	case DDP_COMPONENT_OVL_EXDMA2:
	case DDP_COMPONENT_OVL_EXDMA3:
	case DDP_COMPONENT_OVL_EXDMA4:
	case DDP_COMPONENT_OVL_EXDMA5:
	case DDP_COMPONENT_OVL_EXDMA6:
	case DDP_COMPONENT_OVL_EXDMA7:
	case DDP_COMPONENT_OVL_EXDMA8:
	case DDP_COMPONENT_OVL_EXDMA9:
	case DDP_COMPONENT_OVL1_EXDMA0:
	case DDP_COMPONENT_OVL1_EXDMA1:
	case DDP_COMPONENT_OVL1_EXDMA2:
	case DDP_COMPONENT_OVL1_EXDMA3:
	case DDP_COMPONENT_OVL1_EXDMA4:
	case DDP_COMPONENT_OVL1_EXDMA5:
	case DDP_COMPONENT_OVL1_EXDMA6:
	case DDP_COMPONENT_OVL1_EXDMA7:
	case DDP_COMPONENT_OVL1_EXDMA8:
	case DDP_COMPONENT_OVL1_EXDMA9:
		mtk_ovl_exdma_analysis(comp);
		break;
	case DDP_COMPONENT_OVL0_BLENDER0:
	case DDP_COMPONENT_OVL0_BLENDER1:
	case DDP_COMPONENT_OVL0_BLENDER2:
	case DDP_COMPONENT_OVL0_BLENDER3:
	case DDP_COMPONENT_OVL0_BLENDER4:
	case DDP_COMPONENT_OVL0_BLENDER5:
	case DDP_COMPONENT_OVL0_BLENDER6:
	case DDP_COMPONENT_OVL0_BLENDER7:
	case DDP_COMPONENT_OVL0_BLENDER8:
	case DDP_COMPONENT_OVL0_BLENDER9:
	case DDP_COMPONENT_OVL1_BLENDER0:
	case DDP_COMPONENT_OVL1_BLENDER1:
	case DDP_COMPONENT_OVL1_BLENDER2:
	case DDP_COMPONENT_OVL1_BLENDER3:
	case DDP_COMPONENT_OVL1_BLENDER4:
	case DDP_COMPONENT_OVL1_BLENDER5:
	case DDP_COMPONENT_OVL1_BLENDER6:
	case DDP_COMPONENT_OVL1_BLENDER7:
	case DDP_COMPONENT_OVL1_BLENDER8:
	case DDP_COMPONENT_OVL1_BLENDER9:
		mtk_ovl_blender_analysis(comp);
		break;
	case DDP_COMPONENT_VDISP_AO:
		mtk_vdisp_ao_analysis(comp);
		break;
	case DDP_COMPONENT_RDMA0:
	case DDP_COMPONENT_RDMA1:
	case DDP_COMPONENT_RDMA2:
	case DDP_COMPONENT_RDMA3:
	case DDP_COMPONENT_RDMA4:
	case DDP_COMPONENT_RDMA5:
		mtk_rdma_analysis(comp);
		break;
	case DDP_COMPONENT_MDP_RDMA0:
	case DDP_COMPONENT_MDP_RDMA1:
		mtk_mdp_rdma_analysis(comp);
		break;
	case DDP_COMPONENT_WDMA0:
	case DDP_COMPONENT_WDMA1:
	case DDP_COMPONENT_WDMA2:
	case DDP_COMPONENT_WDMA3:
	case DDP_COMPONENT_UFBC_WDMA0:
	case DDP_COMPONENT_UFBC_WDMA1:
	case DDP_COMPONENT_UFBC_WDMA2:
	case DDP_COMPONENT_UFBC_WDMA3:
	case DDP_COMPONENT_OVLSYS_UFBC_WDMA0:
	case DDP_COMPONENT_OVLSYS_WDMA0:
	case DDP_COMPONENT_OVLSYS_WDMA1:
	case DDP_COMPONENT_OVLSYS_WDMA2:
	case DDP_COMPONENT_OVLSYS_WDMA3:
		mtk_wdma_analysis(comp);
		break;
	case DDP_COMPONENT_RSZ0:
	case DDP_COMPONENT_RSZ1:
	case DDP_COMPONENT_RSZ2:
	case DDP_COMPONENT_OVLSYS_RSZ1:
	case DDP_COMPONENT_OVLSYS_RSZ2:
		mtk_rsz_analysis(comp);
		break;
	case DDP_COMPONENT_MDP_RSZ0:
	case DDP_COMPONENT_MDP_RSZ1:
	case DDP_COMPONENT_OVL0_MDP_RSZ0:
	case DDP_COMPONENT_OVL0_MDP_RSZ1:
	case DDP_COMPONENT_OVL1_MDP_RSZ0:
	case DDP_COMPONENT_OVL1_MDP_RSZ1:
		mtk_mdp_rsz_analysis(comp);
		break;
	case DDP_COMPONENT_DSI0:
	case DDP_COMPONENT_DSI1:
	case DDP_COMPONENT_DSI2:
		mtk_dsi_analysis(comp);
		break;
	case DDP_COMPONENT_DP_INTF0:
	case DDP_COMPONENT_DP_INTF1:
		mtk_dp_intf_analysis(comp);
		break;
	case DDP_COMPONENT_POSTMASK0:
	case DDP_COMPONENT_POSTMASK1:
		mtk_postmask_analysis(comp);
		break;
	case DDP_COMPONENT_CM0:
		mtk_cm_analysis(comp);
		break;
	case DDP_COMPONENT_SPR0:
		mtk_spr_analysis(comp);
		break;
	case DDP_COMPONENT_POSTALIGN0:
		mtk_postalign_analysis(comp);
		break;
	case DDP_COMPONENT_DSC0:
		mtk_dsc_analysis(comp);
		break;
	case DDP_COMPONENT_MERGE0:
	case DDP_COMPONENT_MERGE1:
	case DDP_COMPONENT_MERGE2:
	case DDP_COMPONENT_MERGE3:
		mtk_merge_analysis(comp);
		break;
	case DDP_COMPONENT_CHIST0:
	case DDP_COMPONENT_CHIST1:
		disp_chist_analysis(comp);
		break;
	case DDP_COMPONENT_ODDMR0:
	case DDP_COMPONENT_ODDMR1:
		mtk_oddmr_analysis(comp);
		break;
	default:
		return 0;
	}

	return 0;
}

#define SERIAL_REG_MAX 54
void mtk_serial_dump_reg(void __iomem *base, unsigned int offset,
			 unsigned int num)
{
	unsigned int i = 0;
	int s = 0, l = 0;
	char buf[SERIAL_REG_MAX];

	if (num > 4)
		num = 4;

	l = snprintf(buf, SERIAL_REG_MAX, "0x%03x:", offset);
	if (l < 0) {
		/* Handle snprintf() error*/
		return;
	}

	for (i = 0; i < num; i++) {
		s = snprintf(buf + l, SERIAL_REG_MAX, "0x%08x ",
			     readl(base + offset + i * 0x4));
		if (s < 0) {
			/* Handle snprintf() error*/
			return;
		}
		l += s;
	}

	DDPDUMP("%s\n", buf);
}

#define CUST_REG_MAX 84
void mtk_cust_dump_reg(void __iomem *base, int off1, int off2, int off3,
		       int off4)
{
	unsigned int i = 0;
	int s = 0, l = 0;
	int off[] = {off1, off2, off3, off4};
	char buf[CUST_REG_MAX] = {0};

	for (i = 0; i < 4; i++) {
		if (off[i] < 0)
			break;
		s = snprintf(buf + l, CUST_REG_MAX - l, "0x%03x:0x%08x ", off[i],
			     readl(base + off[i]));
		if (s < 0) {
			/* Handle snprintf() error */
			return;
		}

		l += s;
	}

	DDPDUMP("%s\n", buf);
}
