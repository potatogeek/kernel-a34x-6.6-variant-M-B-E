/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2021 MediaTek Inc.
 */

#ifndef MTK_DRM_CRTC_H
#define MTK_DRM_CRTC_H

#include <linux/types.h>
#include <linux/workqueue.h>
#include <linux/wait.h>
#include <linux/pm_wakeup.h>
#include <linux/timer.h>

#include <drm/drm_crtc.h>
#include <drm/drm_writeback.h>

#ifndef DRM_CMDQ_DISABLE
#include <linux/soc/mediatek/mtk-cmdq-ext.h>
#else
#include "mtk-cmdq-ext.h"
#endif

#include "mtk_drm_ddp_comp.h"
#include "mtk_drm_plane.h"
#include "mtk_debug.h"
#include "mtk_log.h"
#include "mtk_panel_ext.h"
#include "mtk_drm_lowpower.h"
#include "mtk_disp_recovery.h"
#include "mtk_drm_ddp_addon.h"
#include "mtk_disp_pmqos.h"
#include "slbc_ops.h"
#include "mtk_disp_pq_helper.h"

#if IS_ENABLED(CONFIG_ARM64)
#if IS_ENABLED(CONFIG_DRM_MEDIATEK_AUTO_YCT)
#define MAX_CRTC 7
#else
#define MAX_CRTC 4
#endif
#define OVL_LAYER_NR 15L
#define MAX_LAYER_NR 20
#else
#define MAX_CRTC 3
#define OVL_LAYER_NR 12L
#define MAX_LAYER_NR 20
#endif
#define OVL_PHY_LAYER_NR 4L
#define RDMA_LAYER_NR 1UL
#define EXTERNAL_INPUT_LAYER_NR 2UL
#define MEMORY_INPUT_LAYER_NR 2UL
#define SP_INPUT_LAYER_NR 2UL
#define MAX_PLANE_NR                                                           \
	((OVL_LAYER_NR) + (EXTERNAL_INPUT_LAYER_NR) + (MEMORY_INPUT_LAYER_NR) + (SP_INPUT_LAYER_NR))
#define MTK_PLANE_INPUT_LAYER_COUNT (OVL_LAYER_NR)
#define MTK_LUT_SIZE 512
#define MTK_MAX_BPC 10
#define MTK_MIN_BPC 3
#define BW_MODULE 21
#define COLOR_MATRIX_PARAMS 17
#define CWB_BUFFER_NUM 10

#define PRIMARY_OVL_PHY_LAYER_NR 6L

#define PRIMARY_OVL_EXT_LAYER_NR 6L

#define SPR_TYPE_FENCE_MAX 5

#define pgc	_get_context()

#ifndef IF_ONE
#define IF_ONE
#endif

#define HBM_BYPASS_PQ 0x10000
#define DOZE_BYPASS_PQ 0x1

/* TODO: BW report module should not hardcode */
enum DISP_PMQOS_SLOT {
	DISP_PMQOS_OVL0_BW = 0,
	DISP_PMQOS_OVL0_FBDC_BW,
	DISP_PMQOS_OVL1_BW,
	DISP_PMQOS_OVL1_FBDC_BW,
	DISP_PMQOS_OVL0_2L_BW,
	DISP_PMQOS_OVL0_2L_FBDC_BW,
	DISP_PMQOS_OVL1_2L_BW,
	DISP_PMQOS_OVL1_2L_FBDC_BW,
	DISP_PMQOS_OVL2_2L_BW,
	DISP_PMQOS_OVL2_2L_FBDC_BW,
	DISP_PMQOS_OVL3_2L_BW,
	DISP_PMQOS_OVL3_2L_FBDC_BW,
	DISP_PMQOS_OVL0_2L_NWCG_BW,
	DISP_PMQOS_OVL0_2L_NWCG_FBDC_BW,
	DISP_PMQOS_OVL1_2L_NWCG_BW,
	DISP_PMQOS_OVL1_2L_NWCG_FBDC_BW,
	DISP_PMQOS_RDMA0_BW,
	DISP_PMQOS_RDMA1_BW,
	DISP_PMQOS_RDMA2_BW,
	DISP_PMQOS_WDMA0_BW,
	DISP_PMQOS_WDMA1_BW,
	DISP_PMQOS_OVLSYS_WDMA2_BW
};

#define MAX_DISP_VBLANK_REC_THREAD 3
#define MAX_DISP_VBLANK_REC_JOB 8

/* Vblank record GCE thread type */
enum DISP_VBLANK_REC_DATA_TYPE {
	T_START = 0,
	T_END,
	T_DURATION,
	DISP_VBLANK_REC_DATA_TYPE_MAX
};

/* Vblank record GCE thread type */
/* Add type need modify MAX_DISP_REC_THREAD at same time */
enum DISP_VBLANK_REC_THREAD_TYPE {
	CLIENT_CFG_REC = 0,
	CLIENT_SUB_CFG_REC,
	CLIENT_DSI_CFG_REC,
	DISP_REC_THREAD_TYPE_MAX
};

/* Vblank record GCE job type */
/* Add type need modify MAX_DISP_REC_JOB at same time */
enum DISP_VBLANK_REC_JOB_TYPE {
	FRAME_CONFIG = 0,
	USER_COMMAND,
	PQ_HELPER_CONFIG,
	SET_BL,
	ESD_CHECK,
	WRITE_DDIC,
	READ_DDIC,
	MIPI_HOP,
	DISP_REC_JOB_TYPE_MAX
};

#define IGNORE_MODULE_IRQ

#define DISP_SLOT_CUR_CONFIG_FENCE_BASE 0x0000
#define DISP_SLOT_CUR_CONFIG_FENCE(n)                                          \
	(DISP_SLOT_CUR_CONFIG_FENCE_BASE + (0x4 * (n)))
#define DISP_SLOT_PRESENT_FENCE(n)                                          \
	(DISP_SLOT_CUR_CONFIG_FENCE(MAX_PLANE_NR) + (0x4 * (n)))
#define DISP_SLOT_SUBTRACTOR_WHEN_FREE_BASE                                    \
	(DISP_SLOT_PRESENT_FENCE(MAX_CRTC))
#define DISP_SLOT_SUBTRACTOR_WHEN_FREE(n)                                      \
	(DISP_SLOT_SUBTRACTOR_WHEN_FREE_BASE + (0x4 * (n)))
#define DISP_SLOT_RDMA_FB_IDX (DISP_SLOT_SUBTRACTOR_WHEN_FREE(MAX_PLANE_NR))
#define DISP_SLOT_RDMA_FB_ID (DISP_SLOT_RDMA_FB_IDX + 0x4)
#define DISP_SLOT_CUR_HRT_IDX (DISP_SLOT_RDMA_FB_ID + 0x4)
#define DISP_SLOT_CUR_HRT_LEVEL (DISP_SLOT_CUR_HRT_IDX + 0x4)
#define DISP_SLOT_CUR_LARB_HRT (DISP_SLOT_CUR_HRT_LEVEL + 0x4)
#define DISP_SLOT_CUR_CHAN_HRT(n)                                      \
	(DISP_SLOT_CUR_LARB_HRT + 0x4 + (0x4 * (n)))
#define DISP_SLOT_CUR_BW_VAL(n)                                      \
	(DISP_SLOT_CUR_CHAN_HRT(BW_CHANNEL_NR) + (0x4 * (n)))
#define DISP_SLOT_CUR_HDR_BW_VAL(n)                                      \
	(DISP_SLOT_CUR_BW_VAL(MAX_LAYER_NR) + (0x4 * (n)))
#define DISP_SLOT_CUR_STASH_BW_VAL(n)                                      \
	(DISP_SLOT_CUR_HDR_BW_VAL(MAX_LAYER_NR) + (0x4 * (n)))
#define DISP_SLOT_CUR_HDR_STASH_BW_VAL(n)                                      \
	(DISP_SLOT_CUR_STASH_BW_VAL(MAX_LAYER_NR) + (0x4 * (n)))
#define DISP_SLOT_CUR_OUTPUT_FENCE (DISP_SLOT_CUR_HDR_STASH_BW_VAL(MAX_LAYER_NR))
#define DISP_SLOT_CUR_INTERFACE_FENCE (DISP_SLOT_CUR_OUTPUT_FENCE + 0x4)
#define DISP_SLOT_OVL_STATUS						       \
	((DISP_SLOT_CUR_INTERFACE_FENCE + 0x4))
#define DISP_SLOT_READ_DDIC_BASE (DISP_SLOT_OVL_STATUS + 0x4)
#define DISP_SLOT_READ_DDIC_BASE_END		\
	(DISP_SLOT_READ_DDIC_BASE + READ_DDIC_SLOT_NUM * 0x4)
#define DISP_SLOT_OVL_DSI_SEQ (DISP_SLOT_READ_DDIC_BASE_END)
#define DISP_SLOT_OVL_WDMA_SEQ (DISP_SLOT_OVL_DSI_SEQ + 0x4)
/* For Dynamic OVL feature */
#define DISP_OVL_ROI_SIZE 0x20
#define DISP_OVL_DATAPATH_CON 0x24

/* TODO: figure out Display pipe which need report PMQOS BW */
/*Msync 2.0*/
#define DISP_SLOT_VFP_PERIOD (DISP_SLOT_OVL_WDMA_SEQ + 0x4)
#define DISP_SLOT_DSI_STATE_DBG7 (DISP_SLOT_VFP_PERIOD + 0x4)
#define DISP_SLOT_DSI_STATE_DBG7_2 (DISP_SLOT_DSI_STATE_DBG7 + 0x4)

#define DISP_SLOT_TE1_EN (DISP_SLOT_DSI_STATE_DBG7_2 + 0x4)
#define DISP_SLOT_REQUEST_TE_PREPARE (DISP_SLOT_TE1_EN + 0x4)
#define DISP_SLOT_REQUEST_TE_EN (DISP_SLOT_REQUEST_TE_PREPARE + 0x4)

/* PC */
#define DISP_SLOT_PANEL_SPR_EN (DISP_SLOT_REQUEST_TE_EN + 0x4)
#define DISP_SLOT_CUR_HRT_VAL_DMRR (DISP_SLOT_PANEL_SPR_EN + 0x4)
#define DISP_SLOT_CUR_HRT_VAL_DBIR (DISP_SLOT_CUR_HRT_VAL_DMRR + 0x4)
#define DISP_SLOT_CUR_HRT_VAL_ODRW (DISP_SLOT_CUR_HRT_VAL_DBIR + 0x4)

#define DISP_SLOT_TRIGGER_LOOP_SKIP_MERGE (DISP_SLOT_CUR_HRT_VAL_ODRW + 0x4)

/* For idlemgr by wb*/
#define DISP_SLOT_IDLEMGR_BY_WB_STATUS (DISP_SLOT_TRIGGER_LOOP_SKIP_MERGE + 0x4)
#define DISP_SLOT_IDLEMGR_BY_WB_TRACE (DISP_SLOT_IDLEMGR_BY_WB_STATUS + 0x4)
#define DISP_SLOT_IDLEMGR_BY_WB_BREAK  (DISP_SLOT_IDLEMGR_BY_WB_TRACE + 0x4)

/* reset OVL log */
#define OVL_RT_LOG_NR 10
#define DISP_SLOT_OVL_COMP_ID(n) (DISP_SLOT_IDLEMGR_BY_WB_BREAK + 0x4 + (0x4 * (n)))
#define DISP_SLOT_OVL_GREQ_CNT(n) (DISP_SLOT_OVL_COMP_ID(OVL_RT_LOG_NR) + (0x4 * (n)))
#define DISP_SLOT_OVL_RT_LOG_END DISP_SLOT_OVL_GREQ_CNT(OVL_RT_LOG_NR)

#define DISP_SLOT_LAYER_AVG_RATIO(n)                                          \
	(DISP_SLOT_OVL_RT_LOG_END + 0x14 + (0x8 * (n)))
#define DISP_SLOT_LAYER_PEAK_RATIO(n)                                          \
	(DISP_SLOT_LAYER_AVG_RATIO(n) + 0x4)

/*
 *	Vblank Configure Duration Record
 *
 *		Vblank one GCE thread record data:
 *			--Ts					:Configure Start
 *			--Te					:Configure End
 *			--Td[MAX_DISP_REC_JOB]	:Configure Duration (Record MAX_DISP_REC_JOB gce job)
 *
 *		Vblank recore MAX_DISP_REC_THREAD gce thread data
 *
 */
#define DISP_SLOT_VBLANK_REC(n)		\
	(DISP_SLOT_LAYER_PEAK_RATIO(MAX_FRAME_RATIO_NUMBER * MAX_LAYER_RATIO_NUMBER) + 0x4 + (0x4 * (n)))

#define DISP_SLOT_LAYER_PRE_AVG_RATIO(n)                                          \
	(DISP_SLOT_VBLANK_REC(MAX_DISP_VBLANK_REC_THREAD * (MAX_DISP_VBLANK_REC_JOB + 2)) \
	+ 0x4 + (0x8 * (n)))
#define DISP_SLOT_LAYER_PRE_PEAK_RATIO(n)                                          \
	(DISP_SLOT_LAYER_PRE_AVG_RATIO(n) + 0x4)
#define DISP_SLOT_EXT_LAYER_PRE_AVG_RATIO(n)                                          \
	(DISP_SLOT_LAYER_PRE_PEAK_RATIO(MAX_LAYER_RATIO_NUMBER) + 0x4 + (0x8 * (n)))
#define DISP_SLOT_EXT_LAYER_PRE_PEAK_RATIO(n)                                          \
	(DISP_SLOT_EXT_LAYER_PRE_AVG_RATIO(n) + 0x4)


#define DISP_SLOT_SIZE            \
	(DISP_SLOT_EXT_LAYER_PRE_PEAK_RATIO(0x18) + 0x4)

#if DISP_SLOT_SIZE > CMDQ_BUF_ALLOC_SIZE
#error "DISP_SLOT_SIZE exceed CMDQ_BUF_ALLOC_SIZE"
#endif

#define to_mtk_crtc(x) container_of(x, struct mtk_drm_crtc, base)

#define to_mtk_crtc_state(x) container_of(x, struct mtk_crtc_state, base)

#define _MTK_CRTC_COLOR_FMT_ID_SHIFT 0
#define _MTK_CRTC_COLOR_FMT_ID_WIDTH 8
#define _MTK_CRTC_COLOR_FMT_RGBSWAP_SHIFT                                      \
	(_MTK_CRTC_COLOR_FMT_ID_SHIFT + _MTK_CRTC_COLOR_FMT_ID_WIDTH)
#define _MTK_CRTC_COLOR_FMT_RGBSWAP_WIDTH 1
#define _MTK_CRTC_COLOR_FMT_BYTESWAP_SHIFT                                     \
	(_MTK_CRTC_COLOR_FMT_RGBSWAP_SHIFT + _MTK_CRTC_COLOR_FMT_RGBSWAP_WIDTH)
#define _MTK_CRTC_COLOR_FMT_BYTESWAP_WIDTH 1
#define _MTK_CRTC_COLOR_FMT_FORMAT_SHIFT                                       \
	(_MTK_CRTC_COLOR_FMT_BYTESWAP_SHIFT +                                  \
	 _MTK_CRTC_COLOR_FMT_BYTESWAP_WIDTH)
#define _MTK_CRTC_COLOR_FMT_FORMAT_WIDTH 5
#define _MTK_CRTC_COLOR_FMT_VDO_SHIFT                                          \
	(_MTK_CRTC_COLOR_FMT_FORMAT_SHIFT + _MTK_CRTC_COLOR_FMT_FORMAT_WIDTH)
#define _MTK_CRTC_COLOR_FMT_VDO_WIDTH 1
#define _MTK_CRTC_COLOR_FMT_BLOCK_SHIT                                         \
	(_MTK_CRTC_COLOR_FMT_VDO_SHIFT + _MTK_CRTC_COLOR_FMT_VDO_WIDTH)
#define _MTK_CRTC_COLOR_FMT_BLOCK_WIDTH 1
#define _MTK_CRTC_COLOR_FMT_bpp_SHIFT                                          \
	(_MTK_CRTC_COLOR_FMT_BLOCK_SHIT + _MTK_CRTC_COLOR_FMT_BLOCK_WIDTH)
#define _MTK_CRTC_COLOR_FMT_bpp_WIDTH 6
#define _MTK_CRTC_COLOR_FMT_RGB_SHIFT                                          \
	(_MTK_CRTC_COLOR_FMT_bpp_SHIFT + _MTK_CRTC_COLOR_FMT_bpp_WIDTH)
#define _MTK_CRTC_COLOR_FMT_RGB_WIDTH 1

#define GCE_BASE_ADDR 0x10228000
#define GCE_GCTL_VALUE 0x48
#define GCE_DEBUG_START_ADDR 0x1104
#define GCE_DDR_EN BIT(16)

#define _MASK_SHIFT(val, width, shift)                                         \
	(((val) >> (shift)) & ((1 << (width)) - 1))

#define REG_FLD(width, shift)                                                  \
	((unsigned int)((((width)&0xFF) << 16) | ((shift)&0xFF)))

#define REG_FLD_MSB_LSB(msb, lsb) REG_FLD((msb) - (lsb) + 1, (lsb))

#define REG_FLD_WIDTH(field) ((unsigned int)(((field) >> 16) & 0xFF))

#define REG_FLD_SHIFT(field) ((unsigned int)((field)&0xFF))

#define REG_FLD_MASK(field)                                                    \
	((unsigned int)((1ULL << REG_FLD_WIDTH(field)) - 1)                    \
	 << REG_FLD_SHIFT(field))

#define REG_FLD_VAL(field, val)                                                \
	(((val) << REG_FLD_SHIFT(field)) & REG_FLD_MASK(field))

#define REG_FLD_VAL_GET(field, regval)                                         \
	(((regval)&REG_FLD_MASK(field)) >> REG_FLD_SHIFT(field))

#define DISP_REG_GET_FIELD(field, reg32)                                       \
	REG_FLD_VAL_GET(field, __raw_readl((unsigned long *)(reg32)))

#define SET_VAL_MASK(o_val, o_mask, i_val, i_fld)                              \
	do {                                                                   \
		o_val |= (i_val << REG_FLD_SHIFT(i_fld));                      \
		o_mask |= (REG_FLD_MASK(i_fld));                               \
	} while (0)

#define MAKE_CRTC_COLOR_FMT(rgb, bpp, block, vdo, format, byteswap, rgbswap,   \
			    id)                                                \
	(((rgb) << _MTK_CRTC_COLOR_FMT_RGB_SHIFT) |                            \
	 ((bpp) << _MTK_CRTC_COLOR_FMT_bpp_SHIFT) |                            \
	 ((block) << _MTK_CRTC_COLOR_FMT_BLOCK_SHIT) |                         \
	 ((vdo) << _MTK_CRTC_COLOR_FMT_VDO_SHIFT) |                            \
	 ((format) << _MTK_CRTC_COLOR_FMT_FORMAT_SHIFT) |                      \
	 ((byteswap) << _MTK_CRTC_COLOR_FMT_BYTESWAP_SHIFT) |                  \
	 ((rgbswap) << _MTK_CRTC_COLOR_FMT_RGBSWAP_SHIFT) |                    \
	 ((id) << _MTK_CRTC_COLOR_FMT_ID_SHIFT))

#define MTK_CRTC_COLOR_FMT_GET_RGB(fmt)                                        \
	_MASK_SHIFT(fmt, _MTK_CRTC_COLOR_FMT_RGB_WIDTH,                        \
		    _MTK_CRTC_COLOR_FMT_RGB_SHIFT)
#define MTK_CRTC_COLOR_FMT_GET_bpp(fmt)                                        \
	_MASK_SHIFT(fmt, _MTK_CRTC_COLOR_FMT_bpp_WIDTH,                        \
		    _MTK_CRTC_COLOR_FMT_bpp_SHIFT)
#define MTK_CRTC_COLOR_FMT_GET_BLOCK(fmt)                                      \
	_MASK_SHIFT(fmt, _MTK_CRTC_COLOR_FMT_BLOCK_WIDTH,                      \
		    _MTK_CRTC_COLOR_FMT_BLOCK_SHIT)
#define MTK_CRTC_COLOR_FMT_GET_VDO(fmt)                                        \
	_MASK_SHIFT(fmt, _MTK_CRTC_COLOR_FMT_VDO_WIDTH,                        \
		    _MTK_CRTC_COLOR_FMT_VDO_SHIFT)
#define MTK_CRTC_COLOR_FMT_GET_FORMAT(fmt)                                     \
	_MASK_SHIFT(fmt, _MTK_CRTC_COLOR_FMT_FORMAT_WIDTH,                     \
		    _MTK_CRTC_COLOR_FMT_FORMAT_SHIFT)
#define MTK_CRTC_COLOR_FMT_GET_BYTESWAP(fmt)                                   \
	_MASK_SHIFT(fmt, _MTK_CRTC_COLOR_FMT_BYTESWAP_WIDTH,                   \
		    _MTK_CRTC_COLOR_FMT_BYTESWAP_SHIFT)
#define MTK_CRTC_COLOR_FMT_GET_RGBSWAP(fmt)                                    \
	_MASK_SHIFT(fmt, _MTK_CRTC_COLOR_FMT_RGBSWAP_WIDTH,                    \
		    _MTK_CRTC_COLOR_FMT_RGBSWAP_SHIFT)
#define MTK_CRTC_COLOR_FMT_GET_ID(fmt)                                         \
	_MASK_SHIFT(fmt, _MTK_CRTC_COLOR_FMT_ID_WIDTH,                         \
		    _MTK_CRTC_COLOR_FMT_ID_SHIFT)
#define MTK_CRTC_COLOR_FMT_GET_Bpp(fmt) (MTK_CRTC_COLOR_FMT_GET_bpp(fmt) / 8)

#define MAX_CRTC_DC_FB 3

#define __mtk_crtc_path_len(mtk_crtc, ddp_mode, ddp_path) \
	(((ddp_mode < DDP_MODE_NR) && (ddp_path < DDP_PATH_NR)) ? \
	((mtk_crtc)->ddp_ctx[ddp_mode].ovl_comp_nr[ddp_path] + \
	(mtk_crtc)->ddp_ctx[ddp_mode].ddp_comp_nr[ddp_path]) : 0)

#define __mtk_crtc_dual_path_len(mtk_crtc, ddp_path) \
	((mtk_crtc)->dual_pipe_ddp_ctx.ovl_comp_nr[ddp_path] + \
	(mtk_crtc)->dual_pipe_ddp_ctx.ddp_comp_nr[ddp_path])

#define __mtk_crtc_path_comp(mtk_crtc, ddp_mode, ddp_path, __idx) \
	((__idx) < (mtk_crtc)->ddp_ctx[ddp_mode].ovl_comp_nr[ddp_path] ? \
	(mtk_crtc)->ddp_ctx[ddp_mode].ovl_comp[ddp_path][__idx] : \
	(mtk_crtc)->ddp_ctx[ddp_mode].ddp_comp[ddp_path] \
	[__idx - (mtk_crtc)->ddp_ctx[ddp_mode].ovl_comp_nr[ddp_path]])

#define for_each_comp_in_target_ddp_mode_bound(comp, mtk_crtc, __i, __j,       \
					       ddp_mode, offset)               \
	for ((__i) = 0; (__i) < DDP_PATH_NR; (__i)++)                          \
		for ((__j) = 0;                         \
			(offset) <                          \
			(__mtk_crtc_path_len(mtk_crtc, ddp_mode, (__i))) &&  \
			(__j) <                             \
			(__mtk_crtc_path_len(mtk_crtc, ddp_mode, (__i))) -  \
			offset &&                           \
			((comp) = __mtk_crtc_path_comp(mtk_crtc, ddp_mode, (__i), (__j)), 1) ; \
			(__j)++)                            \
			for_each_if(comp)

#define for_each_comp_in_target_ddp_mode(comp, mtk_crtc, __i, __j, ddp_mode)   \
	for_each_comp_in_target_ddp_mode_bound(comp, mtk_crtc, __i, __j,       \
			ddp_mode, 0)

#define for_each_comp_in_crtc_path_bound(comp, mtk_crtc, __i, __j, offset)     \
	for_each_comp_in_target_ddp_mode_bound(comp, mtk_crtc, __i, __j,       \
		(mtk_crtc->ddp_mode < DDP_MODE_NR ? mtk_crtc->ddp_mode : 0), offset)

#define for_each_comp_in_cur_crtc_path(comp, mtk_crtc, __i, __j)               \
	for_each_comp_in_crtc_path_bound(comp, mtk_crtc, __i, __j, 0)

#define for_each_comp_in_crtc_target_path(comp, mtk_crtc, __i, ddp_path)       \
	for ((__i) = 0;                           \
		(__i) < __mtk_crtc_path_len(mtk_crtc, mtk_crtc->ddp_mode, ddp_path) &&   \
		((comp) = __mtk_crtc_path_comp(mtk_crtc, mtk_crtc->ddp_mode, \
		ddp_path, (__i)), 1) ; (__i)++)                              \
		for_each_if(comp)

#define for_each_comp_in_crtc_target_mode(comp, mtk_crtc, __i, __j, ddp_mode)  \
	for ((__i) = 0; (__i) < DDP_PATH_NR; (__i)++)                          \
		for ((__j) = 0;                       \
			(__j) <  __mtk_crtc_path_len(mtk_crtc, ddp_mode, (__i)) &&  \
			((comp) = __mtk_crtc_path_comp(mtk_crtc, ddp_mode, (__i), (__j)), 1); \
			(__j)++)                          \
			for_each_if(comp)

#define for_each_comp_in_crtc_path_reverse(comp, mtk_crtc, __i, __j)           \
	for ((__i) = DDP_PATH_NR - 1; (__i) >= 0; (__i)--)                     \
		for ((__j) = __mtk_crtc_path_len(mtk_crtc, mtk_crtc->ddp_mode, (__i)) - 1;     \
			(__j) >= 0 && ((comp) = __mtk_crtc_path_comp(mtk_crtc, \
			mtk_crtc->ddp_mode, (__i), (__j)), 1) ; (__j)--)                     \
			for_each_if(comp)

#define for_each_comp_in_dual_pipe_reverse(comp, mtk_crtc, __i, __j)           \
	for ((__i) = DDP_SECOND_PATH - 1; (__i) >= 0; (__i)--)					   \
		for ((__j) = ((mtk_crtc)->dual_pipe_ddp_ctx.ovl_comp_nr[(__i)] + \
			(mtk_crtc)->dual_pipe_ddp_ctx.ddp_comp_nr[(__i)]) - 1;	 \
			(__j) >= 0 &&                    \
			((comp) = ((__j) < (mtk_crtc)->dual_pipe_ddp_ctx.ovl_comp_nr[(__i)]) ? \
			(mtk_crtc)->dual_pipe_ddp_ctx.ovl_comp[(__i)][(__j)] : \
			(mtk_crtc)->dual_pipe_ddp_ctx.ddp_comp[(__i)] \
			[(__j) - (mtk_crtc)->dual_pipe_ddp_ctx.ovl_comp_nr[(__i)]], \
			1) ; (__j)--)					  \
			for_each_if(comp)

/* this macro gets current display ctx's comp with all ddp_mode */
#define for_each_comp_in_all_crtc_mode(comp, mtk_crtc, __i, __j, p_mode)       \
	for ((p_mode) = 0; (p_mode) < DDP_MODE_NR; (p_mode)++)                 \
		for ((__i) = 0; (__i) < DDP_PATH_NR; (__i)++)                  \
			for ((__j) = 0; (__j) < __mtk_crtc_path_len(mtk_crtc, p_mode, (__i)) &&   \
				((comp) = \
					__mtk_crtc_path_comp(mtk_crtc, p_mode, (__i), (__j)), 1) ; \
				(__j)++)                      \
				for_each_if(comp)

/* this macro gets current display ctx with specific ddp_mode's comp */
#define for_each_comp_in_crtc_target_mode_path(comp, mtk_crtc, __i, p_mode, ddp_path)       \
	for ((__i) = 0;                           \
		(__i) < __mtk_crtc_path_len(mtk_crtc, p_mode, ddp_path) &&   \
		((comp) = __mtk_crtc_path_comp(mtk_crtc, p_mode, ddp_path, (__i)), 1) ;\
		(__i)++)                              \
		for_each_if(comp)

/* this macro gets all ddp_mode's comp id in constant path data */
#define for_each_comp_id_in_path_data(comp_id, path_data, __i, __j, p_mode)    \
	for ((p_mode) = 0; (p_mode) < DDP_MODE_NR; (p_mode)++)        \
		for ((__i) = 0; (__i) < DDP_PATH_NR; (__i)++)             \
			for ((__j) = 0;                   \
				(__j) < ((path_data)->ovl_path_len[p_mode][__i] + \
					(path_data)->path_len[p_mode][__i]) &&  \
				((comp_id) = (__j < (path_data)->ovl_path_len[p_mode][__i]) ? \
					(path_data)->ovl_path[p_mode][__i][__j] : \
					(path_data)->path[p_mode][__i] \
					[__j - (path_data)->ovl_path_len[p_mode][__i]], \
				1);                           \
				(__j)++)

/* this macro fetches specific ddp_mode and ddp_path's comp id in constant path data */
#define for_each_comp_id_target_mode_path_in_path_data(comp_id, path_data, __j, p_mode, ddp_path) \
	for ((__j) = 0; (__j) < ((path_data)->ovl_path_len[p_mode][ddp_path] + \
			(path_data)->path_len[p_mode][ddp_path]) &&  \
		((comp_id) = (__j < (path_data)->ovl_path_len[p_mode][ddp_path]) ? \
			(path_data)->ovl_path[p_mode][ddp_path][__j] : \
			(path_data)->path[p_mode][ddp_path] \
			[__j - (path_data)->ovl_path_len[p_mode][ddp_path]], \
		1);                           \
		(__j)++)

#define for_each_comp_id_in_dual_pipe(comp_id, path_data, __i, __j)    \
	for ((__i) = 0; (__i) < DDP_SECOND_PATH; (__i)++) \
		for ((__j) = 0;				  \
			(__j) <	((path_data)->dual_ovl_path_len[__i] + \
				(path_data)->dual_path_len[__i]) &&  \
			((comp_id) = (__j < (path_data)->dual_ovl_path_len[__i]) ? \
				(path_data)->dual_ovl_path[__i][__j] : \
				(path_data)->dual_path[__i] \
				[__j - (path_data)->dual_ovl_path_len[__i]],	  \
			1);						  \
			(__j)++)

#define for_each_comp_in_dual_pipe(comp, mtk_crtc, __i, __j)       \
	for ((__i) = 0; (__i) < DDP_SECOND_PATH; (__i)++)		   \
		for ((__j) = 0; (__j) < ((mtk_crtc)->dual_pipe_ddp_ctx.ovl_comp_nr[__i] + \
				(mtk_crtc)->dual_pipe_ddp_ctx.ddp_comp_nr[__i]) &&		  \
			((comp) = (__j < (mtk_crtc)->dual_pipe_ddp_ctx.ovl_comp_nr[__i]) ? \
				(mtk_crtc)->dual_pipe_ddp_ctx.ovl_comp[__i][__j] : \
				(mtk_crtc)->dual_pipe_ddp_ctx.ddp_comp[__i] \
				[__j - (mtk_crtc)->dual_pipe_ddp_ctx.ovl_comp_nr[__i]], \
			1);						  \
			(__j)++)					  \
			for_each_if(comp)

#define for_each_wb_comp_id_in_path_data(comp_id, path_data, __i, p_mode)      \
	for ((p_mode) = 0; (p_mode) < DDP_MODE_NR; (p_mode)++)        \
		for ((__i) = 0;                       \
			(__i) < (path_data)->wb_path_len[p_mode] &&  \
			((comp_id) = (path_data)          \
			->wb_path[p_mode][__i], 1);       \
			(__i)++)

enum MTK_CRTC_PROP {
	CRTC_PROP_OVERLAP_LAYER_NUM,
	CRTC_PROP_LYE_IDX,
	CRTC_PROP_PRES_FENCE_IDX,
	CRTC_PROP_SF_PRES_FENCE_IDX,
	CRTC_PROP_DOZE_ACTIVE,
	CRTC_PROP_OUTPUT_ENABLE,
	CRTC_PROP_OUTPUT_FENCE_IDX,
	CRTC_PROP_OUTPUT_X,
	CRTC_PROP_OUTPUT_Y,
	CRTC_PROP_OUTPUT_WIDTH,
	CRTC_PROP_OUTPUT_HEIGHT,
	CRTC_PROP_OUTPUT_DST_WIDTH,
	CRTC_PROP_OUTPUT_DST_HEIGHT,
	CRTC_PROP_OUTPUT_FB_ID,
	CRTC_PROP_INTF_FENCE_IDX,
	CRTC_PROP_DISP_MODE_IDX,
	CRTC_PROP_HBM_ENABLE,
	CRTC_PROP_COLOR_TRANSFORM,
	CRTC_PROP_USER_SCEN,
	CRTC_PROP_HDR_ENABLE,
	/*Msync 2.0*/
	CRTC_PROP_MSYNC2_0_ENABLE,
	CRTC_PROP_MSYNC2_0_EPT,
	CRTC_PROP_OVL_DSI_SEQ,
	CRTC_PROP_OUTPUT_SCENARIO,
	CRTC_PROP_CAPS_BLOB_ID,
	CRTC_PROP_AOSP_CCORR_LINEAR,
	CRTC_PROP_PARTIAL_UPDATE_ENABLE,
	CRTC_PROP_BL_SYNC_GAMMA_GAIN,
	CRTC_PROP_DYNAMIC_WCG_OFF,
	CRTC_PROP_WCG_BY_COLOR_MODE,
	CRTC_PROP_STYLUS,
	CRTC_PROP_MAX,
};

#define USER_SCEN_BLANK (BIT(0))
#define USER_SCEN_SKIP_PANEL_SWITCH (BIT(1))
#define USER_SCEN_SAME_POWER_MODE (BIT(2))

enum MTK_CRTC_COLOR_FMT {
	CRTC_COLOR_FMT_UNKNOWN = 0,
	CRTC_COLOR_FMT_Y8 = MAKE_CRTC_COLOR_FMT(0, 8, 0, 0, 7, 0, 0, 1),
	CRTC_COLOR_FMT_RGBA4444 = MAKE_CRTC_COLOR_FMT(1, 16, 0, 0, 4, 0, 0, 2),
	CRTC_COLOR_FMT_RGBA5551 = MAKE_CRTC_COLOR_FMT(1, 16, 0, 0, 0, 0, 0, 3),
	CRTC_COLOR_FMT_RGB565 = MAKE_CRTC_COLOR_FMT(1, 16, 0, 0, 0, 0, 0, 4),
	CRTC_COLOR_FMT_BGR565 = MAKE_CRTC_COLOR_FMT(1, 16, 0, 0, 0, 1, 0, 5),
	CRTC_COLOR_FMT_RGB888 = MAKE_CRTC_COLOR_FMT(1, 24, 0, 0, 1, 1, 0, 6),
	CRTC_COLOR_FMT_BGR888 = MAKE_CRTC_COLOR_FMT(1, 24, 0, 0, 1, 0, 0, 7),
	CRTC_COLOR_FMT_RGBA8888 = MAKE_CRTC_COLOR_FMT(1, 32, 0, 0, 2, 1, 0, 8),
	CRTC_COLOR_FMT_BGRA8888 = MAKE_CRTC_COLOR_FMT(1, 32, 0, 0, 2, 0, 0, 9),
	CRTC_COLOR_FMT_ARGB8888 = MAKE_CRTC_COLOR_FMT(1, 32, 0, 0, 3, 1, 0, 10),
	CRTC_COLOR_FMT_ABGR8888 = MAKE_CRTC_COLOR_FMT(1, 32, 0, 0, 3, 0, 0, 11),
	CRTC_COLOR_FMT_RGBX8888 = MAKE_CRTC_COLOR_FMT(1, 32, 0, 0, 0, 0, 0, 12),
	CRTC_COLOR_FMT_BGRX8888 = MAKE_CRTC_COLOR_FMT(1, 32, 0, 0, 0, 0, 0, 13),
	CRTC_COLOR_FMT_XRGB8888 = MAKE_CRTC_COLOR_FMT(1, 32, 0, 0, 0, 0, 0, 14),
	CRTC_COLOR_FMT_XBGR8888 = MAKE_CRTC_COLOR_FMT(1, 32, 0, 0, 0, 0, 0, 15),
	CRTC_COLOR_FMT_AYUV = MAKE_CRTC_COLOR_FMT(0, 0, 0, 0, 0, 0, 0, 16),
	CRTC_COLOR_FMT_YUV = MAKE_CRTC_COLOR_FMT(0, 0, 0, 0, 0, 0, 0, 17),
	CRTC_COLOR_FMT_UYVY = MAKE_CRTC_COLOR_FMT(0, 16, 0, 0, 4, 0, 0, 18),
	CRTC_COLOR_FMT_VYUY = MAKE_CRTC_COLOR_FMT(0, 16, 0, 0, 4, 1, 0, 19),
	CRTC_COLOR_FMT_YUYV = MAKE_CRTC_COLOR_FMT(0, 16, 0, 0, 5, 0, 0, 20),
	CRTC_COLOR_FMT_YVYU = MAKE_CRTC_COLOR_FMT(0, 16, 0, 0, 5, 1, 0, 21),
	CRTC_COLOR_FMT_UYVY_BLK = MAKE_CRTC_COLOR_FMT(0, 16, 1, 0, 4, 0, 0, 22),
	CRTC_COLOR_FMT_VYUY_BLK = MAKE_CRTC_COLOR_FMT(0, 16, 1, 0, 4, 1, 0, 23),
	CRTC_COLOR_FMT_YUY2_BLK = MAKE_CRTC_COLOR_FMT(0, 16, 1, 0, 5, 0, 0, 24),
	CRTC_COLOR_FMT_YVYU_BLK = MAKE_CRTC_COLOR_FMT(0, 16, 1, 0, 5, 1, 0, 25),
	CRTC_COLOR_FMT_YV12 = MAKE_CRTC_COLOR_FMT(0, 8, 0, 0, 8, 1, 0, 26),
	CRTC_COLOR_FMT_I420 = MAKE_CRTC_COLOR_FMT(0, 8, 0, 0, 8, 0, 0, 27),
	CRTC_COLOR_FMT_YV16 = MAKE_CRTC_COLOR_FMT(0, 8, 0, 0, 9, 1, 0, 28),
	CRTC_COLOR_FMT_I422 = MAKE_CRTC_COLOR_FMT(0, 8, 0, 0, 9, 0, 0, 29),
	CRTC_COLOR_FMT_YV24 = MAKE_CRTC_COLOR_FMT(0, 8, 0, 0, 10, 1, 0, 30),
	CRTC_COLOR_FMT_I444 = MAKE_CRTC_COLOR_FMT(0, 8, 0, 0, 10, 0, 0, 31),
	CRTC_COLOR_FMT_NV12 = MAKE_CRTC_COLOR_FMT(0, 8, 0, 0, 12, 0, 0, 32),
	CRTC_COLOR_FMT_NV21 = MAKE_CRTC_COLOR_FMT(0, 8, 0, 0, 12, 1, 0, 33),
	CRTC_COLOR_FMT_NV12_BLK = MAKE_CRTC_COLOR_FMT(0, 8, 1, 0, 12, 0, 0, 34),
	CRTC_COLOR_FMT_NV21_BLK = MAKE_CRTC_COLOR_FMT(0, 8, 1, 0, 12, 1, 0, 35),
	CRTC_COLOR_FMT_NV12_BLK_FLD =
		MAKE_CRTC_COLOR_FMT(0, 8, 1, 1, 12, 0, 0, 36),
	CRTC_COLOR_FMT_NV21_BLK_FLD =
		MAKE_CRTC_COLOR_FMT(0, 8, 1, 1, 12, 1, 0, 37),
	CRTC_COLOR_FMT_NV16 = MAKE_CRTC_COLOR_FMT(0, 8, 0, 0, 13, 0, 0, 38),
	CRTC_COLOR_FMT_NV61 = MAKE_CRTC_COLOR_FMT(0, 8, 0, 0, 13, 1, 0, 39),
	CRTC_COLOR_FMT_NV24 = MAKE_CRTC_COLOR_FMT(0, 8, 0, 0, 14, 0, 0, 40),
	CRTC_COLOR_FMT_NV42 = MAKE_CRTC_COLOR_FMT(0, 8, 0, 0, 14, 1, 0, 41),
	CRTC_COLOR_FMT_PARGB8888 =
		MAKE_CRTC_COLOR_FMT(1, 32, 0, 0, 3, 1, 0, 42),
	CRTC_COLOR_FMT_PABGR8888 =
		MAKE_CRTC_COLOR_FMT(1, 32, 0, 0, 3, 1, 1, 43),
	CRTC_COLOR_FMT_PRGBA8888 =
		MAKE_CRTC_COLOR_FMT(1, 32, 0, 0, 3, 0, 1, 44),
	CRTC_COLOR_FMT_PBGRA8888 =
		MAKE_CRTC_COLOR_FMT(1, 32, 0, 0, 3, 0, 0, 45),
};

/*
 * use CLIENT_DSI_CFG guideline :
 * 1. send DSI VM CMD
 * 2. process register operation which not invovle stop DSI &
 *    enable DSI and intend process with lower priority
 */
/* CLIENT_SODI_LOOP for sw workaround to fix gce hw bug */
#define DECLARE_GCE_CLIENT(EXPR)                                               \
	EXPR(CLIENT_CFG)                                                       \
	EXPR(CLIENT_TRIG_LOOP)                                                 \
	EXPR(CLIENT_SODI_LOOP)                                                 \
	EXPR(CLIENT_EVENT_LOOP)                                                 \
	EXPR(CLIENT_SUB_CFG)                                                   \
	EXPR(CLIENT_DSI_CFG)                                                   \
	EXPR(CLIENT_SEC_CFG)                                                   \
	EXPR(CLIENT_PQ_EOF)                                                        \
	EXPR(CLIENT_PQ)                                                    \
	EXPR(CLIENT_BWM_LOOP)                                                    \
	EXPR(CLIENT_TYPE_MAX)

enum CRTC_GCE_CLIENT_TYPE { DECLARE_GCE_CLIENT(DECLARE_NUM) };

enum CRTC_GCE_EVENT_TYPE {
	EVENT_CMD_EOF,
	EVENT_VDO_EOF,
	EVENT_STREAM_EOF,
	EVENT_STREAM_DIRTY,
	EVENT_SYNC_TOKEN_SODI,
	EVENT_TE,
	EVENT_ESD_EOF,
	EVENT_RDMA0_EOF,
	EVENT_WDMA0_EOF,
	EVENT_WDMA1_EOF,
	EVENT_STREAM_BLOCK,
	EVENT_CABC_EOF,
	EVENT_DSI_SOF,
	/*Msync 2.0*/
	EVENT_SYNC_TOKEN_VFP_PERIOD,
	EVENT_GPIO_TE0,
	EVENT_GPIO_TE1,
	EVENT_SYNC_TOKEN_DISP_VA_START,
	EVENT_SYNC_TOKEN_DISP_VA_END,
	EVENT_SYNC_TOKEN_TE,
	EVENT_SYNC_TOKEN_PREFETCH_TE,
	EVENT_DSI0_TARGET_LINE,
	EVENT_OVLSYS_WDMA0_EOF,
	EVENT_OVLSYS1_WDMA0_EOF,
	EVENT_SYNC_TOKEN_VIDLE_POWER_ON,
	EVENT_SYNC_TOKEN_CHECK_TRIGGER_MERGE,
	EVENT_DPC_DISP1_PRETE,
	EVENT_OVLSYS_WDMA1_EOF,
	EVENT_MDP_RDMA0_EOF,
	EVENT_MDP_RDMA1_EOF,
	EVENT_Y2R_EOF,
	EVENT_MML_DISP_DONE_EVENT,
	EVENT_AAL_EOF,
	EVENT_UFBC_WDMA1_EOF,
	EVENT_UFBC_WDMA3_EOF,
	EVENT_OVLSYS_UFBC_WDMA0_EOF,
	EVENT_MUTEX0_SOF,
	EVENT_TYPE_MAX,
};

enum CRTC_DDP_MODE {
	DDP_MAJOR,
	DDP_MINOR,
	DDP_MODE_NR,
	DDP_NO_USE,
};

enum CRTC_DDP_PATH {
	DDP_FIRST_PATH,
	DDP_SECOND_PATH,
	DDP_PATH_NR,
};

/**
 * enum CWB_BUFFER_TYPE - user want to use buffer type
 * @IMAGE_ONLY: u8 *image
 * @CARRY_METADATA: struct user_cwb_buffer
 */
enum CWB_BUFFER_TYPE {
	IMAGE_ONLY,
	CARRY_METADATA,
	BUFFER_TYPE_NR,
};

enum MML_LINK_STATE {
	NON_MML,
	MML_IR_ENTERING,
	MML_DIRECT_LINKING,
	MML_STOP_LINKING,
	MML_IR_IDLE,
	MML_DC_ENTERING,
	MML_STOP_DC,
};

enum PF_TS_TYPE {
	IRQ_RDMA_EOF,
	IRQ_DSI_EOF,
	IRQ_CMDQ_CB,
	NOT_SPECIFIED,
};

enum SLBC_STATE {
	SLBC_UNREGISTER,
	SLBC_NEED_FREE,
	SLBC_CAN_ALLOC,
};

enum DISP_SMC_CMD {
	DISP_CMD_CRTC_FIRST_ENABLE,
	DISP_CMD_CRTC_ENABLE,
	DISP_CMD_MAX,
};

enum SET_DIRTY_INDEX {
	ODDMR_CHECK_TRIGGER = 0,
	ESD_RECOVERY,
	SWITCH_SPR,
	DC_PRIM_PATH,
	SPR_ENABLE,
	SPR_DISABLE,
	SET_DIRTY,	//6
	SET_DIRTY_DONE,
	DISCRETE_UPDATE,
	GCE_FLUSH,	//9
	COMPOSITION_WB,
	PRIM_PATH,
	WAIT_CMD_FRAME_DONE,
	DDIC_HANDLER_BY_GCE,
	HOT_PLUG_THREAD,
	ESD_CHECK_TEST,
	CRTC_ENABLE,
	FIRST_ENABLE_DDP_CONFIG,
	TEST_SHOW,
	CLR_SET_DIRTY = 0xfffffff,
	CLR_SET_DIRTY_CB = 0x1000,
	INDEX_MAX,
};

struct mtk_crtc_path_data {
	bool is_fake_path;
	bool is_discrete_path;
	bool is_exdma_dual_layer;
	const enum mtk_ddp_comp_id *ovl_path[DDP_MODE_NR][DDP_PATH_NR];
	unsigned int ovl_path_len[DDP_MODE_NR][DDP_PATH_NR];
	const enum mtk_ddp_comp_id *path[DDP_MODE_NR][DDP_PATH_NR];
	unsigned int path_len[DDP_MODE_NR][DDP_PATH_NR];
	bool path_req_hrt[DDP_MODE_NR][DDP_PATH_NR];
	const enum mtk_ddp_comp_id *wb_path[DDP_MODE_NR];
	unsigned int wb_path_len[DDP_MODE_NR];
	const struct mtk_addon_scenario_data *addon_data;
	const enum mtk_ddp_comp_id *scaling_data;
	//for dual path
	const enum mtk_ddp_comp_id *dual_ovl_path[DDP_PATH_NR];
	unsigned int dual_ovl_path_len[DDP_PATH_NR];
	const enum mtk_ddp_comp_id *dual_path[DDP_PATH_NR];
	unsigned int dual_path_len[DDP_PATH_NR];
	const struct mtk_addon_scenario_data *addon_data_dual;
	const enum mtk_ddp_comp_id *scaling_data_dual;
};

struct mtk_crtc_gce_obj {
	struct cmdq_client *client[CLIENT_TYPE_MAX];
	struct cmdq_pkt_buffer buf;
	struct cmdq_base *base;
	int event[EVENT_TYPE_MAX];
};

/**
 * struct mtk_crtc_ddp_ctx - MediaTek specific ddp structure for crtc path
 * control.
 * @mutex: handle to one of the ten disp_mutex streams
 * @ddp_comp_nr: number of components in ddp_comp
 * @ddp_comp: array of pointers the mtk_ddp_comp structures used by this crtc
 * @wb_comp_nr: number of components in 1to2 path
 * @wb_comp: array of pointers the mtk_ddp_comp structures used for 1to2 path
 * @wb_fb: temp wdma output buffer in 1to2 path
 * @dc_fb: frame buffer for decouple mode
 * @dc_fb_idx: the index of latest used fb
 */
struct mtk_crtc_ddp_ctx {
	struct mtk_disp_mutex *mutex;
	unsigned int ovl_comp_nr[DDP_PATH_NR];
	struct mtk_ddp_comp **ovl_comp[DDP_PATH_NR];
	unsigned int ddp_comp_nr[DDP_PATH_NR];
	struct mtk_ddp_comp **ddp_comp[DDP_PATH_NR];
	bool req_hrt[DDP_PATH_NR];
	unsigned int wb_comp_nr;
	struct mtk_ddp_comp **wb_comp;
	struct drm_framebuffer *wb_fb;
	struct drm_framebuffer *dc_fb;
	unsigned int dc_fb_idx;
};

struct mtk_scaling_ctx {
	bool scaling_en;
	int lcm_width;
	int lcm_height;
	struct drm_display_mode *scaling_mode;
	bool cust_mode_mapping;
	int mode_mapping[MAX_MODES];
};

struct mtk_drm_fake_vsync {
	struct task_struct *fvsync_task;
	wait_queue_head_t fvsync_wq;
	atomic_t fvsync_active;
};

struct mtk_drm_fake_layer {
	unsigned int fake_layer_mask;
	struct drm_framebuffer *fake_layer_buf[PRIMARY_OVL_PHY_LAYER_NR];
	bool init;
	bool first_dis;
};


struct disp_ccorr_config {
	int mode;
	int color_matrix[16];
	bool featureFlag;
};

struct user_cwb_image {
	u8 *image;
	int width, height;
};

struct user_cwb_metadata {
	unsigned long long timestamp;
	unsigned int frameIndex;
};

struct user_cwb_buffer {
	struct user_cwb_image data;
	struct user_cwb_metadata meta;
};

struct mtk_cwb_buffer_info {
	struct mtk_rect dst_roi;
	dma_addr_t addr_mva;
	u64 addr_va;
	struct drm_framebuffer *fb;
	unsigned long long timestamp;
};

struct mtk_cwb_funcs {
	/**
	 * @get_buffer:
	 *
	 * This function is optional.
	 *
	 * If user hooks this callback, driver will use this first when
	 * wdma irq is arrived. (capture done)
	 * User need fill buffer address to *buffer.
	 *
	 * If user not hooks this callback driver will confirm whether
	 * mtk_wdma_capture_info->user_buffer is NULL or not.
	 * User can use setUserBuffer() assigned this param.
	 */
	void (*get_buffer)(void **buffer);

	/**
	 * @copy_done:
	 *
	 * When Buffer copy done will be use this callback to notify user.
	 */
	void (*copy_done)(void *buffer, enum CWB_BUFFER_TYPE type);
};

struct mtk_cwb_info {
	unsigned int enable;

	struct mtk_rect src_roi;
	unsigned int count;
	bool is_sec;

	unsigned int buf_idx;
	struct mtk_cwb_buffer_info buffer[CWB_BUFFER_NUM];
	unsigned int copy_w;
	unsigned int copy_h;

	enum addon_scenario scn;
	struct mtk_ddp_comp *comp;

	void *user_buffer;
	enum CWB_BUFFER_TYPE type;
	const struct mtk_cwb_funcs *funcs;
};

struct mtk_crtc_static_plane {
	int index;
	struct mtk_plane_state state[OVL_LAYER_NR];
};

struct mtk_crtc_se_plane {
	int panel_id;
	struct mtk_plane_state state;
};

#define MTK_FB_SE_NUM 3

enum DISP_SE_STATE {
	DISP_SE_IDLE,
	DISP_SE_START,
	DISP_SE_RUNNING,
	DISP_SE_STOP,
	DISP_SE_STOPPED,
};

#define MSYNC_MAX_RECORD 5
#define MSYNC_LOWFRAME_THRESHOLD 3
#define MSYNC_MIN_FPS 4610

enum MSYNC_RECORD_TYPE {
	INVALID,
	ENABLE_MSYNC,
	DISABLE_MSYNC,
	FRAME_TIME,
};

struct msync_record {
	enum MSYNC_RECORD_TYPE type;
	u64 time;
	bool low_frame;
};

struct mtk_msync2_dy {
	int dy_en;
	struct msync_record record[MSYNC_MAX_RECORD];
	int record_index;
};

struct mtk_msync2 {
	struct mtk_msync2_dy msync_dy;
	bool msync_disabled;
	bool LFR_disabled;
	bool msync_on;
	bool msync_frame_status;
	atomic_t LFR_final_state;
};

struct mtk_pre_te_cfg {
	bool prefetch_te_en;
	bool vidle_apsrc_off_en;
	bool vidle_dsi_pll_off_en;
	bool merge_trigger_en;
};

struct dual_te {
	bool en;
	atomic_t te_switched;
	atomic_t esd_te1_en;
	int te1;
};

struct mtk_mml_cb_para {
	atomic_t mml_job_submit_done;
	wait_queue_head_t mml_job_submit_wq;
};

struct mtk_drm_sram_list {
	struct list_head head;
	unsigned int hrt_idx;
};

struct mtk_drm_sram {
	struct slbc_data data;
	struct mutex lock;
	struct kref ref;
	unsigned int expiry_hrt_idx;
};

struct pixel_type_map {
	struct mtk_pixel_type_fence map[SPR_TYPE_FENCE_MAX];
	unsigned int head;
};

struct pq_common_data {
	atomic_t pq_get_irq;
	atomic_t pq_irq_trig_src;
	wait_queue_head_t pq_get_irq_wq;
	unsigned int old_persist_property[32];
	unsigned int new_persist_property[32];
	struct pq_tuning_pa_base tuning_pa_table[TUNING_REG_MAX];
	struct pixel_type_map pixel_types;
	int tdshp_flag; /* 0: normal, 1: tuning mode */
	atomic_t pq_hw_relay_cfg_done;
	wait_queue_head_t pq_hw_relay_cb_wq;
	atomic_t pipe_info_filled;
	int c3d_per_crtc;
	bool opt_bypass_pq;
	wait_queue_head_t cfg_done_wq;
	atomic_t cfg_done;
};

struct mtk_vblank_config_rec {
	struct task_struct *vblank_rec_task;
	wait_queue_head_t vblank_rec_wq;
	atomic_t vblank_rec_event;
	int job_dur[DISP_REC_THREAD_TYPE_MAX][DISP_REC_JOB_TYPE_MAX];
	struct list_head top_list;
	struct mutex lock;
};

struct mtk_vblank_config_node {
	int total_dur;
	struct list_head link;
};

struct mtk_tui_ovl_stat {
	unsigned int aid_setting;
	unsigned int cb_reg;
	unsigned int mutex_bit;
};

/**
 * struct mtk_drm_crtc - MediaTek specific crtc structure.
 * @base: crtc object.
 * @enabled: records whether crtc_enable succeeded
 * @bpc: Maximum bits per color channel.
 * @lock: Mutex lock for critical section in crtc
 * @gce_obj: the elements for controlling GCE engine.
 * @planes: array of 4 drm_plane structures, one for each overlay plane
 * @pending_planes: whether any plane has pending changes to be applied
 * @config_regs: memory mapped mmsys configuration register space
 * @ddp_ctx: contain path components and mutex
 * @mutex: handle to one of the ten disp_mutex streams
 * @ddp_mode: the currently selected ddp path
 * @panel_ext: contain extended panel extended information and callback function
 * @esd_ctx: ESD check task context
 * @qos_ctx: BW Qos task context
 */
struct mtk_drm_crtc {
	struct drm_crtc base;
	bool enabled;
	unsigned int bpc;
	bool pending_needs_vblank;
	struct mutex lock;
	struct drm_pending_vblank_event *event;
	struct mtk_crtc_gce_obj gce_obj;
	struct cmdq_pkt *trig_loop_cmdq_handle;
	struct cmdq_pkt *sodi_loop_cmdq_handle;
	struct cmdq_pkt *event_loop_cmdq_handle;
	struct cmdq_pkt *bwm_loop_cmdq_handle;
	struct mtk_drm_plane *planes;
	unsigned int layer_nr;
	bool pending_planes;
	unsigned int ovl_usage_status;
	void __iomem *ovlsys0_regs;
	resource_size_t ovlsys0_regs_pa;
	void __iomem *ovlsys1_regs;
	resource_size_t ovlsys1_regs_pa;
	unsigned int ovlsys_num;
	void __iomem *config_regs;
	resource_size_t config_regs_pa;
	unsigned int dispsys_num;
	void __iomem *side_config_regs;
	resource_size_t side_config_regs_pa;
	const struct mtk_mmsys_reg_data *mmsys_reg_data;
	struct mtk_crtc_ddp_ctx ddp_ctx[DDP_MODE_NR];
	struct mtk_disp_mutex *mutex[DDP_PATH_NR];
	unsigned int ddp_mode;
	unsigned int cur_config_fence[OVL_LAYER_NR];


	struct drm_writeback_connector wb_connector;
	bool wb_enable;
	bool wb_hw_enable;
	bool wb_error;

	struct mtk_drm_crtc_caps crtc_caps;
	const struct mtk_crtc_path_data *path_data;
	struct mtk_crtc_ddp_ctx dual_pipe_ddp_ctx;
	bool is_dual_pipe;

	struct mtk_drm_idlemgr *idlemgr;
	wait_queue_head_t crtc_status_wq;
	struct mtk_panel_ext *panel_ext;
	struct mtk_drm_esd_ctx *esd_ctx;
	struct mtk_drm_gem_obj *round_corner_gem;
	struct mtk_drm_gem_obj *round_corner_gem_l;
	struct mtk_drm_gem_obj *round_corner_gem_r;
	struct mtk_drm_qos_ctx *qos_ctx;
	bool sec_on;
	struct task_struct *vblank_enable_task;
	wait_queue_head_t vblank_enable_wq;
	atomic_t vblank_enable_task_active;

	char *wk_lock_name;
	struct wakeup_source *wk_lock;

	struct mtk_drm_fake_vsync *fake_vsync;
	struct mtk_drm_fake_layer fake_layer;

	/* DC mode - RDMA config thread*/
	struct task_struct *dc_main_path_commit_task;
	wait_queue_head_t dc_main_path_commit_wq;
	atomic_t dc_main_path_commit_event;
	struct task_struct *trigger_event_task;
	struct task_struct *trigger_delay_task;
	struct task_struct *trig_cmdq_task;
	struct task_struct *repaint_task;
	atomic_t trig_event_act;
	atomic_t trig_delay_act;
	atomic_t delayed_trig;
	atomic_t cmdq_trig;
	atomic_t repaint_act;
	wait_queue_head_t trigger_delay;
	wait_queue_head_t trigger_event;
	wait_queue_head_t trigger_cmdq;
	wait_queue_head_t repaint_event;

	atomic_t fence_change;
	atomic_t mml_trigger;
	int check_trigger_type;

	unsigned int avail_modes_num;
	struct drm_display_mode *avail_modes;
	struct mtk_panel_params **panel_params;
	struct timespec64 vblank_time;

	bool mipi_hopping_sta;
	bool panel_osc_hopping_sta;
	bool vblank_en;
	unsigned int hwvsync_en;

	atomic_t already_config;

	bool layer_rec_en;
	unsigned int mode_change_index;
	int mode_idx;
	int mode_chg;
	enum RES_SWITCH_TYPE res_switch;
	struct mtk_scaling_ctx scaling_ctx;
	int fbt_layer_id;

	wait_queue_head_t state_wait_queue;
	bool crtc_blank;
	struct mutex blank_lock;

	wait_queue_head_t present_fence_wq;
	struct task_struct *pf_release_thread;
	atomic_t pf_event;

	wait_queue_head_t sf_present_fence_wq;
	struct task_struct *sf_pf_release_thread;
	atomic_t sf_pf_event;

	/*thread of dump SMI log (SMI larb, sub common, common: OSTDL, bw throttle)*/
	wait_queue_head_t smi_info_dump_wq;
	struct task_struct *smi_info_dump_thread;
	atomic_t smi_info_dump_event;

	/*capture write back ctx*/
	struct mutex cwb_lock;
	struct mtk_cwb_info *cwb_info;
	struct task_struct *cwb_task;
	wait_queue_head_t cwb_wq;
	atomic_t cwb_task_active;

	ktime_t pf_time;
	ktime_t sof_time;
	spinlock_t pf_time_lock;
	struct task_struct *signal_present_fece_task;
	struct cmdq_cb_data cb_data;
	atomic_t cmdq_done;
	wait_queue_head_t signal_fence_task_wq;

	struct mtk_pre_te_cfg pre_te_cfg;

	struct mtk_msync2 msync2;
	struct mtk_panel_spr_params *panel_spr_params;
	struct mtk_panel_cm_params *panel_cm_params;
	struct dual_te d_te;

	// MML inline rotate SRAM
	struct mtk_drm_sram mml_ir_sram;
	struct mml_submit *mml_cfg;
	struct mml_submit *mml_cfg_dc;
	struct mml_submit *mml_cfg_pq;
	struct mtk_mml_cb_para mml_cb;

	atomic_t wait_mml_last_job_is_flushed;
	wait_queue_head_t signal_mml_last_job_is_flushed_wq;
	bool is_mml;
	bool is_mml_dl;
	bool skip_check_trigger;
	bool is_mml_dc;
	unsigned int mml_debug;
	bool is_force_mml_scen;
	bool mml_cmd_ir;
	bool mml_prefer_dc;
	enum MML_LINK_STATE mml_link_state;
	enum SLBC_STATE slbc_state;

	atomic_t signal_irq_for_pre_fence;
	wait_queue_head_t signal_irq_for_pre_fence_wq;
	enum PF_TS_TYPE pf_ts_type;
	struct list_head lyeblob_head;
	unsigned long long last_aee_trigger_ts;

	atomic_t force_high_step;
	int force_high_enabled;
	struct total_tile_overhead tile_overhead;
	struct total_tile_overhead_v tile_overhead_v;

	struct task_struct *mode_switch_task;
	wait_queue_head_t mode_switch_wq;
	wait_queue_head_t mode_switch_end_wq;
	wait_queue_head_t mode_switch_end_timeout_wq;
	atomic_t singal_for_mode_switch;

	struct drm_crtc_state *old_mode_switch_state;

	//discrete
	struct cmdq_pkt *pending_handle;

	struct pq_common_data *pq_data;

	bool skip_frame;
	bool is_dsc_output_swap;

	bool capturing;

	int dli_relay_1tnp;

	unsigned int total_srt;

	unsigned int aod_scp_spr_switch;
	unsigned int spr_is_on;
	wait_queue_head_t spr_switch_wait_queue;
	atomic_t spr_switching;
	atomic_t spr_switch_cb_done;
	unsigned int spr_switch_type;
	atomic_t get_data_type;
	atomic_t postalign_relay;

	struct mtk_vblank_config_rec *vblank_rec;

	unsigned int usage_ovl_fmt[MAX_LAYER_NR]; // for mt6989 hrt by larb
	unsigned int usage_ovl_compr[MAX_LAYER_NR];

	struct mtk_ddp_comp *last_blender;

	struct mtk_ddp_comp *first_exdma;
	struct mtk_ddp_comp *first_blender;

	wait_queue_head_t esd_notice_wq;
	atomic_t esd_notice_status;

	struct mtk_tui_ovl_stat tui_ovl_stat;

	bool virtual_path;
	void *phys_mtk_crtc;
	unsigned int panel_offset;
	unsigned int mutex_id;
	unsigned int offset_x;
	unsigned int offset_y;

	int se_panel;	/* 1 << */
	int sideband_layer;
	struct mtk_crtc_static_plane static_plane;
	struct mtk_crtc_se_plane se_plane[MTK_FB_SE_NUM];
	enum DISP_SE_STATE se_state;

	bool is_plane0_updated;

	struct mutex sol_lock;

	/* For simple_api */
	int need_lock_tid;
	int customer_lock_tid;
	int frame_update_cnt;

	/* For Frame start / end */
	struct frame_condition_wq frame_start;
	struct frame_condition_wq frame_done;

	enum mtk_set_lcm_sceanario set_lcm_scn;

	bool cust_skip_frame;
	bool reset_path;

};

enum BL_GAMMA_GAIN {
	GAMMA_GAIN_0,
	GAMMA_GAIN_1,
	GAMMA_GAIN_2,
	GAMMA_GAIN_RANGE,
	GAMMA_GAIN_MAX,
};

struct mtk_crtc_state {
	struct drm_crtc_state base;
	struct cmdq_pkt *cmdq_handle;

	bool pending_config;
	unsigned int pending_width;
	unsigned int pending_height;
	unsigned int pending_vrefresh;

	struct mtk_lye_ddp_state lye_state;
	struct mtk_rect rsz_src_roi;
	struct mtk_rect rsz_dst_roi;
	struct mtk_rect mml_src_roi[2];
	struct mtk_rect mml_dst_roi;
	struct mtk_rect mml_dst_roi_dual[2];
	struct mtk_rsz_param rsz_param[2];
	struct mtk_rect ovl_partial_roi;
	bool ovl_partial_dirty;
	atomic_t plane_enabled_num;
	bool pending_usage_update;
	unsigned int pending_usage_list;
	unsigned int bl_sync_gamma_gain[GAMMA_GAIN_MAX];

	/* property */
	uint64_t prop_val[CRTC_PROP_MAX];
	bool doze_changed;
};

struct mtk_cmdq_cb_data {
	struct drm_crtc_state		*state;
	struct cmdq_pkt			*cmdq_handle;
	struct drm_crtc			*crtc;
	unsigned int misc;
	unsigned int mmclk_req_idx;
	unsigned int msync2_enable;
	void __iomem *mutex_reg_va;
	void __iomem *disp_reg_va;
	void __iomem *disp_mutex_reg_va;
	void __iomem *mmlsys_reg_va;
	bool is_mml;
	bool is_mml_dl;
	unsigned int pres_fence_idx;
	struct drm_framebuffer *wb_fb;
	unsigned int wb_fence_idx;
	enum addon_scenario wb_scn;
	unsigned int hrt_idx;
	struct mtk_lcm_dsi_cmd_packet *ddic_packet;
	ktime_t signal_ts;
	struct cb_data_store *store_cb_data;
	uint64_t pts;
};
#define TIGGER_INTERVAL_S(x) ((unsigned long long)x*1000*1000*1000)
extern unsigned int disp_spr_bypass;

int mtk_drm_crtc_enable_vblank(struct drm_crtc *crtc);
void mtk_drm_crtc_disable_vblank(struct drm_crtc *crtc);
bool mtk_crtc_get_vblank_timestamp(struct drm_crtc *crtc,
				 int *max_error,
				 ktime_t *vblank_time,
				 bool in_vblank_irq);
void mtk_drm_crtc_commit(struct drm_crtc *crtc);
void mtk_crtc_ddp_irq(struct drm_crtc *crtc, struct mtk_ddp_comp *comp);
void mtk_crtc_vblank_irq(struct drm_crtc *crtc);
int mtk_drm_crtc_create(struct drm_device *drm_dev,
			const struct mtk_crtc_path_data *path_data);
void mtk_drm_crtc_plane_update(struct drm_crtc *crtc, struct drm_plane *plane,
			       struct mtk_plane_state *state);
void mtk_drm_crtc_plane_disable(struct drm_crtc *crtc, struct drm_plane *plane,
			       struct mtk_plane_state *state);

void mtk_drm_crtc_mini_dump(struct drm_crtc *crtc);
void mtk_drm_crtc_dump(struct drm_crtc *crtc);
void mtk_drm_crtc_mini_analysis(struct drm_crtc *crtc);
void mtk_drm_crtc_analysis(struct drm_crtc *crtc);
void mtk_drm_crtc_diagnose(void);
bool mtk_crtc_is_frame_trigger_mode(struct drm_crtc *crtc);
void mtk_crtc_clr_comp_done(struct mtk_drm_crtc *mtk_crtc,
			      struct cmdq_pkt *cmdq_handle,
			      struct mtk_ddp_comp *comp,
			      int clear_event);
void mtk_crtc_wait_comp_done(struct mtk_drm_crtc *mtk_crtc,
			      struct cmdq_pkt *cmdq_handle,
			      struct mtk_ddp_comp *comp,
			      int clear_event);
void mtk_crtc_wait_frame_done(struct mtk_drm_crtc *mtk_crtc,
			      struct cmdq_pkt *cmdq_handle,
			      enum CRTC_DDP_PATH ddp_path,
			      int clear_event);
struct mtk_ddp_comp *mtk_crtc_get_comp(struct drm_crtc *crtc,
				       unsigned int mode_id,
				       unsigned int path_id,
				       unsigned int comp_idx);

struct mtk_ddp_comp *mtk_crtc_get_dual_comp(struct drm_crtc *crtc,
				       unsigned int path_id,
				       unsigned int comp_idx);


struct mtk_ddp_comp *mtk_ddp_comp_request_output(struct mtk_drm_crtc *mtk_crtc);

/* get fence */
int mtk_drm_crtc_getfence_ioctl(struct drm_device *dev, void *data,
				struct drm_file *file_priv);
int mtk_drm_crtc_get_sf_fence_ioctl(struct drm_device *dev, void *data,
				    struct drm_file *file_priv);
int mtk_drm_crtc_fence_release_ioctl(struct drm_device *dev, void *data,
				struct drm_file *file_priv);

long mtk_crtc_wait_status(struct drm_crtc *crtc, bool status, long timeout);
void mtk_crtc_cwb_path_disconnect(struct drm_crtc *crtc);
int mtk_crtc_path_switch(struct drm_crtc *crtc, unsigned int path_sel,
			 int need_lock);
void mtk_need_vds_path_switch(struct drm_crtc *crtc);

void mtk_drm_crtc_first_enable(struct drm_crtc *crtc);
void mtk_drm_crtc_enable(struct drm_crtc *crtc);
void mtk_drm_crtc_disable(struct drm_crtc *crtc, bool need_wait);
bool mtk_crtc_with_sub_path(struct drm_crtc *crtc, unsigned int ddp_mode);

struct mtk_ddp_comp *mtk_crtc_get_plane_comp(struct drm_crtc *crtc,
					struct mtk_plane_state *plane_state);

void mtk_crtc_ddp_prepare(struct mtk_drm_crtc *mtk_crtc);
void mtk_crtc_ddp_unprepare(struct mtk_drm_crtc *mtk_crtc);
void mtk_crtc_all_layer_off(struct mtk_drm_crtc *mtk_crtc,
				   struct cmdq_pkt *cmdq_handle);
void mtk_crtc_stop(struct mtk_drm_crtc *mtk_crtc, bool need_wait);
void mtk_crtc_connect_default_path(struct mtk_drm_crtc *mtk_crtc);
void mtk_crtc_disconnect_default_path(struct mtk_drm_crtc *mtk_crtc);
void mtk_crtc_config_default_path(struct mtk_drm_crtc *mtk_crtc);
void __mtk_crtc_restore_plane_setting(struct mtk_drm_crtc *mtk_crtc, struct cmdq_pkt *cmdq_handle);
void mtk_crtc_restore_plane_setting(struct mtk_drm_crtc *mtk_crtc);
bool mtk_crtc_set_status(struct drm_crtc *crtc, bool status);
int mtk_crtc_attach_addon_path_comp(struct drm_crtc *crtc,
	const struct mtk_addon_module_data *module_data, bool is_attach);
void mtk_crtc_connect_addon_module(struct drm_crtc *crtc, bool skip_cwb);
void mtk_crtc_disconnect_addon_module(struct drm_crtc *crtc);
int mtk_crtc_gce_flush(struct drm_crtc *crtc, void *gce_cb, void *cb_data,
			struct cmdq_pkt *cmdq_handle);
/*Msync 2.0*/
int mtk_drm_set_msync_cmd_level_table(unsigned int level_id, unsigned int level_fps,
		unsigned int max_fps, unsigned int min_fps);
void mtk_drm_get_msync_cmd_level_table(void);
void mtk_drm_clear_msync_cmd_level_table(void);
int mtk_drm_set_msync_params_ioctl(struct drm_device *dev, void *data,
				struct drm_file *file_priv);
int mtk_drm_get_msync_params_ioctl(struct drm_device *dev, void *data,
				struct drm_file *file_priv);
struct cmdq_pkt *mtk_crtc_gce_commit_begin(struct drm_crtc *crtc,
						struct drm_crtc_state *old_crtc_state,
						struct mtk_crtc_state *crtc_state,
						bool need_sync_mml);
void mtk_crtc_pkt_create(struct cmdq_pkt **cmdq_handle,
	struct drm_crtc *crtc, struct cmdq_client *cl);
int mtk_crtc_get_mutex_id(struct drm_crtc *crtc, unsigned int ddp_mode,
			  enum mtk_ddp_comp_id find_comp);
void mtk_crtc_disconnect_path_between_component(struct drm_crtc *crtc,
						unsigned int ddp_mode,
						enum mtk_ddp_comp_id prev,
						enum mtk_ddp_comp_id next,
						struct cmdq_pkt *cmdq_handle);
void mtk_crtc_connect_path_between_component(struct drm_crtc *crtc,
					     unsigned int ddp_mode,
					     enum mtk_ddp_comp_id prev,
					     enum mtk_ddp_comp_id next,
					     struct cmdq_pkt *cmdq_handle);
int mtk_crtc_find_comp(struct drm_crtc *crtc, unsigned int ddp_mode,
		       enum mtk_ddp_comp_id comp_id);
int mtk_crtc_find_next_comp(struct drm_crtc *crtc, unsigned int ddp_mode,
			    enum mtk_ddp_comp_id comp_id);
int mtk_crtc_find_prev_comp(struct drm_crtc *crtc, unsigned int ddp_mode,
		enum mtk_ddp_comp_id comp_id);
void mtk_drm_fake_vsync_switch(struct drm_crtc *crtc, bool enable);
int mtk_crtc_set_check_trigger_type(struct mtk_drm_crtc *mtk_crtc, int type);
void mtk_crtc_check_trigger(struct mtk_drm_crtc *mtk_crtc, bool delay,
		bool need_lock);

bool mtk_crtc_is_dc_mode(struct drm_crtc *crtc);
void mtk_crtc_clear_wait_event(struct drm_crtc *crtc);
void mtk_crtc_hw_block_ready(struct drm_crtc *crtc);
int mtk_crtc_lcm_ATA(struct drm_crtc *crtc);
int mtk_crtc_mipi_freq_switch(struct drm_crtc *crtc, unsigned int en,
			unsigned int userdata);
int mtk_crtc_osc_freq_switch(struct drm_crtc *crtc, unsigned int en,
			unsigned int userdata);
int mtk_crtc_enter_tui(struct drm_crtc *crtc);
int mtk_crtc_exit_tui(struct drm_crtc *crtc);

void mtk_drm_crtc_mode_check(struct drm_crtc *crtc,
	struct drm_crtc_state *old_state, struct drm_crtc_state *new_state);
struct drm_display_mode *mtk_drm_crtc_avail_disp_mode(struct drm_crtc *crtc,
	unsigned int idx);
int mtk_drm_crtc_get_panel_original_size(struct drm_crtc *crtc, unsigned int *width,
	unsigned int *height);
unsigned int mtk_drm_primary_frame_bw(struct drm_crtc *crtc);

unsigned int mtk_drm_primary_display_get_debug_state(
	struct mtk_drm_private *priv, char *stringbuf, int buf_len);

bool mtk_crtc_with_trigger_loop(struct drm_crtc *crtc);
void mtk_crtc_stop_trig_loop(struct drm_crtc *crtc);
void mtk_crtc_start_trig_loop(struct drm_crtc *crtc);

void mtk_crtc_stop_bwm_ratio_loop(struct drm_crtc *crtc);
void mtk_crtc_start_bwm_ratio_loop(struct drm_crtc *crtc);

bool mtk_crtc_with_sodi_loop(struct drm_crtc *crtc);
void mtk_crtc_stop_sodi_loop(struct drm_crtc *crtc);
void mtk_crtc_start_sodi_loop(struct drm_crtc *crtc);

bool mtk_crtc_with_event_loop(struct drm_crtc *crtc);
void mtk_crtc_stop_event_loop(struct drm_crtc *crtc);
void mtk_crtc_start_event_loop(struct drm_crtc *crtc);

void mtk_crtc_change_output_mode(struct drm_crtc *crtc, int aod_en);
int mtk_crtc_user_cmd_impl(struct drm_crtc *crtc, struct mtk_ddp_comp *comp,
		unsigned int cmd, void *params, bool need_lock);
int mtk_crtc_user_cmd(struct drm_crtc *crtc, struct mtk_ddp_comp *comp,
		unsigned int cmd, void *params);
unsigned int mtk_drm_dump_wk_lock(struct mtk_drm_private *priv,
	char *stringbuf, int buf_len);
char *mtk_crtc_index_spy(int crtc_index);
bool mtk_drm_get_hdr_property(void);
int mtk_drm_aod_setbacklight(struct drm_crtc *crtc, unsigned int level);
int mtk_drm_aod_scp_get_dsi_ulps_wakeup_prd(struct drm_crtc *crtc);

int get_comp_wait_event(struct mtk_drm_crtc *mtk_crtc,
			struct mtk_ddp_comp *comp);
void mtk_crtc_all_layer_off(struct mtk_drm_crtc *mtk_crtc, struct cmdq_pkt *cmdq_handle);
void _mtk_crtc_atmoic_addon_module_connect(
				      struct drm_crtc *crtc,
				      unsigned int ddp_mode,
				      struct mtk_lye_ddp_state *lye_state,
				      struct cmdq_pkt *cmdq_handle, bool skip_cwb);
void _mtk_crtc_atmoic_addon_module_disconnect(
	struct drm_crtc *crtc, unsigned int ddp_mode,
	struct mtk_lye_ddp_state *lye_state, struct cmdq_pkt *cmdq_handle);

int mtk_drm_crtc_wait_blank(struct mtk_drm_crtc *mtk_crtc);
void mtk_drm_crtc_init_para(struct drm_crtc *crtc);
void trigger_without_cmdq(struct drm_crtc *crtc);
void mtk_crtc_set_dirty(struct mtk_drm_crtc *mtk_crtc);
void mtk_crtc_clr_set_dirty(struct mtk_drm_crtc *mtk_crtc);
void mtk_drm_layer_dispatch_to_dual_pipe(
	struct mtk_drm_crtc *mtk_crtc, unsigned int mmsys_id,
	struct mtk_plane_state *plane_state,
	struct mtk_plane_state *plane_state_l,
	struct mtk_plane_state *plane_state_r,
	unsigned int w);
bool mtk_drm_crtc_check_dual_exdma(struct mtk_drm_crtc *mtk_crtc,
	struct mtk_plane_state *plane_state);
void mtk_crtc_dual_layer_config(struct mtk_drm_crtc *mtk_crtc,
		struct mtk_ddp_comp *comp, unsigned int idx,
		struct mtk_plane_state *plane_state, struct cmdq_pkt *cmdq_handle);
unsigned int dual_pipe_comp_mapping(unsigned int mmsys_id, unsigned int comp_id);

int mtk_drm_crtc_set_panel_hbm(struct drm_crtc *crtc, bool en);
int mtk_drm_crtc_hbm_wait(struct drm_crtc *crtc, bool en);

unsigned int mtk_get_mmsys_id(struct drm_crtc *crtc);
int mtk_crtc_ability_chk(struct mtk_drm_crtc *mtk_crtc, enum MTK_CRTC_ABILITY ability);
unsigned int *mtk_get_gce_backup_slot_va(struct mtk_drm_crtc *mtk_crtc,
			unsigned int slot_index);

dma_addr_t mtk_get_gce_backup_slot_pa(struct mtk_drm_crtc *mtk_crtc,
			unsigned int slot_index);

unsigned int mtk_get_plane_slot_idx(struct mtk_drm_crtc *mtk_crtc, unsigned int idx);
void mtk_gce_backup_slot_init(struct mtk_drm_crtc *mtk_crtc);

void mtk_crtc_mml_racing_resubmit(struct drm_crtc *crtc, struct cmdq_pkt *_cmdq_handle);
void mtk_crtc_mml_racing_stop_sync(struct drm_crtc *crtc, struct cmdq_pkt *_cmdq_handle,
				   bool force);

bool mtk_crtc_alloc_sram(struct mtk_drm_crtc *mtk_crtc, unsigned int hrt_idx);
int mtk_crtc_attach_ddp_comp(struct drm_crtc *crtc, int ddp_mode, bool is_attach);
void mtk_crtc_addon_connector_connect(struct drm_crtc *crtc, struct cmdq_pkt *handle);

void mtk_crtc_store_total_overhead(struct mtk_drm_crtc *mtk_crtc,
	struct total_tile_overhead info);
struct total_tile_overhead mtk_crtc_get_total_overhead(struct mtk_drm_crtc *mtk_crtc);
void mtk_crtc_store_total_overhead_v(struct mtk_drm_crtc *mtk_crtc,
	struct total_tile_overhead_v info);
struct total_tile_overhead_v mtk_crtc_get_total_overhead_v(struct mtk_drm_crtc *mtk_crtc);
bool mtk_crtc_check_is_scaling_comp(struct mtk_drm_crtc *mtk_crtc,
		enum mtk_ddp_comp_id comp_id);
void mtk_crtc_divide_default_path_by_rsz(struct mtk_drm_crtc *mtk_crtc);
struct drm_display_mode *mtk_crtc_get_display_mode_by_comp(
	const char *source,
	struct drm_crtc *crtc,
	struct mtk_ddp_comp *comp,
	bool in_scaling_path);
unsigned int mtk_crtc_get_width_by_comp(
	const char *source,
	struct drm_crtc *crtc,
	struct mtk_ddp_comp *comp,
	bool in_scaling_path);
unsigned int mtk_crtc_get_height_by_comp(
	const char *source,
	struct drm_crtc *crtc,
	struct mtk_ddp_comp *comp,
	bool in_scaling_path);
void mtk_crtc_set_width_height(
	int *w,
	int *h,
	struct drm_crtc *crtc,
	bool is_scaling_path);

int mtk_drm_crtc_set_partial_update(struct drm_crtc *crtc,
	struct drm_crtc_state *old_crtc_state,
	struct cmdq_pkt *cmdq_handle, unsigned int enable);

bool msync_is_on(struct mtk_drm_private *priv, struct mtk_panel_params *params,
		unsigned int crtc_id, struct mtk_crtc_state *state,
		struct mtk_crtc_state *old_state);

/* ********************* Legacy DISP API *************************** */
unsigned int DISP_GetScreenWidth(void);
unsigned int DISP_GetScreenHeight(void);

void mtk_crtc_disable_secure_state(struct drm_crtc *crtc);
int mtk_crtc_check_out_sec(struct drm_crtc *crtc);
struct golden_setting_context *
	__get_golden_setting_context(struct mtk_drm_crtc *mtk_crtc);
struct golden_setting_context *
	__get_scaling_golden_setting_context(struct mtk_drm_crtc *mtk_crtc);
/***********************  PanelMaster  ********************************/
void mtk_crtc_start_for_pm(struct drm_crtc *crtc);
void mtk_crtc_stop_for_pm(struct mtk_drm_crtc *mtk_crtc, bool need_wait);
bool mtk_crtc_frame_buffer_existed(void);

/* ********************* Legacy DRM API **************************** */
int mtk_drm_format_plane_cpp(uint32_t format, unsigned int plane);

int mtk_drm_switch_te(struct drm_crtc *crtc, int te_num, bool need_lock);
void mtk_crtc_prepare_instr(struct drm_crtc *crtc);
unsigned int check_dsi_underrun_event(void);
void clear_dsi_underrun_event(void);

int mtk_drm_setbacklight(struct drm_crtc *crtc, unsigned int level,
			unsigned int panel_ext_param, unsigned int cfg_flag, unsigned int lock);
int mtk_drm_setbacklight_at_te(struct drm_crtc *crtc, unsigned int level,
	unsigned int panel_ext_param, unsigned int cfg_flag);
int mtk_drm_setbacklight_grp(struct drm_crtc *crtc, unsigned int level,
			unsigned int panel_ext_param, unsigned int cfg_flag);
void mtk_crtc_update_gce_event(struct mtk_drm_crtc *mtk_crtc);

void mtk_addon_get_comp(struct drm_crtc *crtc, u32 addon, u32 *comp_id, u8 *layer_idx);
void mtk_addon_set_comp(struct drm_crtc *crtc, u32 *addon, const u32 comp_id, const u8 layer_idx);
void mtk_addon_get_module(const enum addon_scenario scn,
			 struct mtk_drm_crtc *mtk_crtc,
			 const struct mtk_addon_module_data **addon_module,
			 const struct mtk_addon_module_data **addon_module_dual);
void mtk_crtc_exec_atf_prebuilt_instr(struct mtk_drm_crtc *mtk_crtc,
			struct cmdq_pkt *handle);

unsigned int mtk_get_cur_spr_type(struct drm_crtc *crtc);

int mtk_drm_switch_spr(struct drm_crtc *crtc, unsigned int en, unsigned int need_lock);

int mtk_vblank_config_rec_init(struct drm_crtc *crtc);
dma_addr_t mtk_vblank_config_rec_get_slot_pa(struct mtk_drm_crtc *mtk_crtc,
	struct cmdq_pkt *pkt, enum DISP_VBLANK_REC_JOB_TYPE job_type, enum DISP_VBLANK_REC_DATA_TYPE data_type);
unsigned int *mtk_vblank_config_rec_get_slot_va(struct mtk_drm_crtc *mtk_crtc,
	enum DISP_VBLANK_REC_THREAD_TYPE thread_type,
	enum DISP_VBLANK_REC_JOB_TYPE job_type,
	enum DISP_VBLANK_REC_DATA_TYPE data_type);
int mtk_vblank_config_rec_start(struct mtk_drm_crtc *mtk_crtc,
	struct cmdq_pkt *cmdq_handle, enum DISP_VBLANK_REC_JOB_TYPE job_type);
int mtk_vblank_config_rec_end_cal(struct mtk_drm_crtc *mtk_crtc,
	struct cmdq_pkt *cmdq_handle, enum DISP_VBLANK_REC_JOB_TYPE job_type);
unsigned int mtk_drm_dump_vblank_config_rec(
	struct mtk_drm_private *priv, char *stringbuf, int buf_len);
void mtk_crtc_default_path_rst(struct drm_crtc *crtc);
void mtk_disp_set_module_hrt(struct mtk_drm_crtc *mtk_crtc, unsigned int bw_base,
	struct cmdq_pkt *handle, enum mtk_ddp_io_cmd event);

void mtk_drm_crtc_exdma_ovl_path(struct mtk_drm_crtc *mtk_crtc,
	struct mtk_ddp_comp *comp, unsigned int blender_id,
	struct cmdq_pkt *cmdq_handle, bool reset_flag, bool rpo_flag);
void mtk_drm_crtc_blender_ovl_path(struct mtk_drm_crtc *mtk_crtc,
	struct mtk_ddp_comp *comp, struct cmdq_pkt *cmdq_handle, bool reset_flag);
void mtk_drm_crtc_exdma_ovl_path_out(struct mtk_drm_crtc *mtk_crtc,
	struct cmdq_pkt *cmdq_handle);
void mtk_drm_crtc_exdma_path_setting_reset(struct mtk_drm_crtc *mtk_crtc,
	struct cmdq_pkt *cmdq_handle);
void mtk_drm_crtc_exdma_path_setting_reset_without_cmdq(struct mtk_drm_crtc *mtk_crtc);

void mtk_crtc_gce_event_config(struct drm_crtc *crtc);
void mtk_crtc_vdisp_ao_config(struct drm_crtc *crtc);
void mml_cmdq_pkt_init(struct drm_crtc *crtc, struct cmdq_pkt *cmdq_handle);
struct mtk_ddp_comp *mtk_disp_get_wdma_comp_by_scn(struct drm_crtc *crtc, enum addon_scenario scn);
void release_fence_frame_skip(struct drm_crtc *crtc);
#endif /* MTK_DRM_CRTC_H */
