// SPDX-License-Identifier: GPL-2.0
//
// Copyright (c) 2019 MediaTek Inc.

#include <linux/clk.h>
#include <linux/component.h>
#include <linux/interrupt.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/vmalloc.h>

#include <soc/mediatek/smi.h>

#include "mtk_cam.h"
#include "mtk_cam-mraw-regs.h"
#include "mtk_cam-mraw.h"
#include "mtk_cam-trace.h"
#include "mtk_cam-plat.h"

#include "iommu_debug.h"

static int debug_cam_mraw;
module_param(debug_cam_mraw, int, 0644);

static int debug_ddren_mraw_hw_mode;
module_param(debug_ddren_mraw_hw_mode, int, 0644);
MODULE_PARM_DESC(debug_ddren_mraw_hw_mode, "debug: 1 : active mraw hw mode");

#undef dev_dbg
#define dev_dbg(dev, fmt, arg...)		\
	do {					\
		if (debug_cam_mraw >= 1)	\
			dev_info(dev, fmt,	\
				## arg);	\
	} while (0)

#define MTK_MRAW_STOP_HW_TIMEOUT			(33 * USEC_PER_MSEC)


static const struct of_device_id mtk_mraw_of_ids[] = {
	{.compatible = "mediatek,mraw",},
	{}
};
MODULE_DEVICE_TABLE(of, mtk_mraw_of_ids);

static int mraw_process_fsm(struct mtk_mraw_device *mraw_dev,
			    struct mtk_camsys_irq_info *irq_info,
			    int *recovered_done)
{
	struct engine_fsm *fsm = &mraw_dev->fsm;
	int sof_type, done_type;
	int cookie_done;
	int ret;
	int recovered = 0, postponed = 0;

	sof_type = irq_info->irq_type & BIT(CAMSYS_IRQ_FRAME_START);
	done_type = irq_info->irq_type & BIT(CAMSYS_IRQ_FRAME_DONE);

	if (done_type) {

		ret = engine_fsm_hw_done(fsm, &cookie_done);
		if (ret > 0)
			irq_info->cookie_done = cookie_done;
		else if (sof_type && (irq_info->fbc_empty == 0))
			postponed = 1;
		else {
			/* handle for fake p1 done */
			dev_info_ratelimited(mraw_dev->dev, "warn: fake done in/out: 0x%x 0x%x\n",
					     irq_info->frame_idx_inner,
					     irq_info->frame_idx);
			irq_info->irq_type &= ~done_type;
			irq_info->cookie_done = 0;
		}
	}

	if (sof_type)
		recovered = engine_fsm_sof(fsm, irq_info->frame_idx_inner,
					   irq_info->frame_idx,
					   irq_info->fbc_empty,
					   recovered_done);

	if (postponed) {
		irq_info->cookie_done = engine_update_for_done(fsm);
		dev_info(mraw_dev->dev, "postponed sof in/out: 0x%x 0x%x\n",
			 irq_info->frame_idx_inner,
			 irq_info->frame_idx);
	}

	if (recovered)
		dev_info(mraw_dev->dev, "recovered done 0x%x in/out: 0x%x 0x%x\n",
			 *recovered_done,
			 irq_info->frame_idx_inner,
			 irq_info->frame_idx);

	return recovered;
}

int mtk_mraw_translation_fault_callback(int port, dma_addr_t mva, void *data)
{
	struct mtk_mraw_device *mraw_dev = (struct mtk_mraw_device *)data;
	unsigned int frame_idx_inner;

	frame_idx_inner = readl_relaxed(mraw_dev->base_inner + REG_MRAW_FRAME_SEQ_NUM);

	dev_info_ratelimited(mraw_dev->dev, "seq_no:%d_%d tg_sen_mode:0x%x tg_vf_con:0x%x tg_path_cfg:0x%x tg_grab_pxl:0x%x tg_grab_lin:0x%x\n",
		readl_relaxed(mraw_dev->base_inner + REG_MRAW_FRAME_SEQ_NUM),
		readl_relaxed(mraw_dev->base + REG_MRAW_FRAME_SEQ_NUM),
		readl_relaxed(mraw_dev->base_inner + REG_MRAW_TG_SEN_MODE),
		readl_relaxed(mraw_dev->base_inner + REG_MRAW_TG_VF_CON),
		readl_relaxed(mraw_dev->base_inner + REG_MRAW_TG_PATH_CFG),
		readl_relaxed(mraw_dev->base_inner + REG_MRAW_TG_SEN_GRAB_PXL),
		readl_relaxed(mraw_dev->base_inner + REG_MRAW_TG_SEN_GRAB_LIN));

	dev_info_ratelimited(mraw_dev->dev, "mod_en:0x%x mod2_en:0x%x mod3_en:0x%x mod4_en:0x%x cq_thr0_addr:0x%x_%x cq_thr0_desc_size:0x%x\n",
		readl_relaxed(mraw_dev->base_inner + REG_MRAW_CTL_MOD_EN),
		readl_relaxed(mraw_dev->base_inner + REG_MRAW_CTL_MOD2_EN),
		readl_relaxed(mraw_dev->base_inner + REG_MRAW_CTL_MOD3_EN),
		readl_relaxed(mraw_dev->base_inner + REG_MRAW_CTL_MOD4_EN),
		readl_relaxed(mraw_dev->base_inner + REG_MRAW_CQ_SUB_THR0_BASEADDR_2_MSB),
		readl_relaxed(mraw_dev->base_inner + REG_MRAW_CQ_SUB_THR0_BASEADDR_2),
		readl_relaxed(mraw_dev->base_inner + REG_MRAW_CQ_SUB_THR0_DESC_SIZE_2));

	dev_info_ratelimited(mraw_dev->dev, "imgo_xsize:0x%x imgo_ysize:0x%x imgo_stride:0x%x imgo_addr:0x%x_%x imgo_ofst_addr:0x%x_%x\n",
		readl_relaxed(mraw_dev->base_inner + REG_MRAW_IMGO_XSIZE),
		readl_relaxed(mraw_dev->base_inner + REG_MRAW_IMGO_YSIZE),
		readl_relaxed(mraw_dev->base_inner + REG_MRAW_IMGO_STRIDE),
		readl_relaxed(mraw_dev->base_inner + REG_MRAW_IMGO_BASE_ADDR_MSB),
		readl_relaxed(mraw_dev->base_inner + REG_MRAW_IMGO_BASE_ADDR),
		readl_relaxed(mraw_dev->base_inner + REG_MRAW_IMGO_OFST_ADDR_MSB),
		readl_relaxed(mraw_dev->base_inner + REG_MRAW_IMGO_OFST_ADDR));

	dev_info_ratelimited(mraw_dev->dev, "imgbo_xsize:0x%x imgbo_ysize:0x%x imgbo_stride:0x%x imgbo_addr:0x%x_%x imgbo_ofst_addr:0x%x_%x\n",
		readl_relaxed(mraw_dev->base_inner + REG_MRAW_IMGBO_XSIZE),
		readl_relaxed(mraw_dev->base_inner + REG_MRAW_IMGBO_YSIZE),
		readl_relaxed(mraw_dev->base_inner + REG_MRAW_IMGBO_STRIDE),
		readl_relaxed(mraw_dev->base_inner + REG_MRAW_IMGBO_BASE_ADDR_MSB),
		readl_relaxed(mraw_dev->base_inner + REG_MRAW_IMGBO_BASE_ADDR),
		readl_relaxed(mraw_dev->base_inner + REG_MRAW_IMGBO_OFST_ADDR_MSB),
		readl_relaxed(mraw_dev->base_inner + REG_MRAW_IMGBO_OFST_ADDR));

	dev_info_ratelimited(mraw_dev->dev, "cpio_xsize:0x%x cpio_ysize:0x%x cpio_stride:0x%x cpio_addr:0x%x_%x cpio_ofst_addr:0x%x_%x\n",
		readl_relaxed(mraw_dev->base_inner + REG_MRAW_CPIO_XSIZE),
		readl_relaxed(mraw_dev->base_inner + REG_MRAW_CPIO_YSIZE),
		readl_relaxed(mraw_dev->base_inner + REG_MRAW_CPIO_STRIDE),
		readl_relaxed(mraw_dev->base_inner + REG_MRAW_CPIO_BASE_ADDR_MSB),
		readl_relaxed(mraw_dev->base_inner + REG_MRAW_CPIO_BASE_ADDR),
		readl_relaxed(mraw_dev->base_inner + REG_MRAW_CPIO_OFST_ADDR_MSB),
		readl_relaxed(mraw_dev->base_inner + REG_MRAW_CPIO_OFST_ADDR));

	mtk_cam_ctrl_dump_request(mraw_dev->cam, CAMSYS_ENGINE_MRAW, mraw_dev->id,
		frame_idx_inner, MSG_M4U_TF);

	return 0;
}

void apply_mraw_cq(struct mtk_mraw_device *mraw_dev,
	      dma_addr_t cq_addr, unsigned int cq_size, unsigned int cq_offset,
	      int initial)
{
#define CQ_VADDR_MASK 0xFFFFFFFF
	u32 cq_addr_lsb = (cq_addr + cq_offset) & CQ_VADDR_MASK;
	u32 cq_addr_msb = ((cq_addr + cq_offset) >> 32);

	if (cq_size == 0)
		return;

	writel_relaxed(cq_addr_lsb, mraw_dev->base + REG_MRAW_CQ_SUB_THR0_BASEADDR_2);
	writel_relaxed(cq_addr_msb, mraw_dev->base + REG_MRAW_CQ_SUB_THR0_BASEADDR_2_MSB);
	writel_relaxed(cq_size, mraw_dev->base + REG_MRAW_CQ_SUB_THR0_DESC_SIZE_2);
	writel(1, mraw_dev->base + REG_MRAW_CTL_START);
	wmb(); /* TBC */

	if (initial)
		dev_info(mraw_dev->dev,
			"apply 1st mraw scq: addr_msb:0x%x addr_lsb:0x%x size:%d\n",
			cq_addr_msb, cq_addr_lsb, cq_size);
	else
		dev_dbg(mraw_dev->dev,
			"apply mraw scq: addr_msb:0x%x addr_lsb:0x%x size:%d\n",
			cq_addr_msb, cq_addr_lsb, cq_size);
}

static unsigned int mtk_cam_mraw_powi(unsigned int x, unsigned int n)
{
	unsigned int rst = 1;
	unsigned int m = n;

	while (m--)
		rst *= x;

	return rst;
}

static unsigned int mtk_cam_mraw_xsize_cal(unsigned int length)
{
	return length * 16 / 8;
}

static unsigned int mtk_cam_mraw_dbg_xsize_cal(unsigned int length, unsigned int imgo_fmt)
{
	switch (imgo_fmt) {
	case MTKCAM_IPI_IMG_FMT_BAYER8:
		return length * 8 / 8;
	case MTKCAM_IPI_IMG_FMT_BAYER10:
		return length * 10 / 8;
	case MTKCAM_IPI_IMG_FMT_BAYER12:
		return length * 12 / 8;
	case MTKCAM_IPI_IMG_FMT_BAYER14:
		return length * 14 / 8;
	default:
		break;
	}

	return length * 16 / 8;
}

static unsigned int mtk_cam_mraw_xsize_cal_cpio(unsigned int length)
{
	return (length + 7) / 8;
}

static void mtk_cam_mraw_set_dense_fmt(
	struct device *dev, unsigned int *tg_width_temp,
	unsigned int *tg_height_temp,
	struct mraw_stats_cfg_param *param, unsigned int dmao_id)
{
	if (dmao_id == imgo_m1) {
		if (param->mbn_pow < 2 || param->mbn_pow > 6) {
			dev_info(dev, "%s:Invalid mbn_pow: %d",
				__func__, param->mbn_pow);
			return;
		}
		switch (param->mbn_dir) {
		case MBN_POW_VERTICAL:
			*tg_height_temp /= mtk_cam_mraw_powi(2, param->mbn_pow);
			break;
		case MBN_POW_HORIZONTAL:
			*tg_width_temp /= mtk_cam_mraw_powi(2, param->mbn_pow);
			break;
		default:
			dev_info(dev, "%s:MBN's dir %d %s fail",
				__func__, param->mbn_dir, "unknown idx");
			return;
		}
		// divided for 2 path from MBN
		*tg_width_temp /= 2;
	} else if (dmao_id == cpio_m1) {
		if (param->cpi_pow < 2 || param->cpi_pow > 6) {
			dev_info(dev, "Invalid cpi_pow: %d", param->cpi_pow);
			return;
		}
		switch (param->cpi_dir) {
		case CPI_POW_VERTICAL:
			*tg_height_temp /= mtk_cam_mraw_powi(2, param->cpi_pow);
			break;
		case CPI_POW_HORIZONTAL:
			*tg_width_temp /= mtk_cam_mraw_powi(2, param->cpi_pow);
			break;
		default:
			dev_info(dev, "%s:CPI's dir %d %s fail",
				__func__, param->cpi_dir, "unknown idx");
			return;
		}
	}
}

static void mtk_cam_mraw_set_concatenation_fmt(
	struct device *dev, unsigned int *tg_width_temp,
	unsigned int *tg_height_temp,
	struct mraw_stats_cfg_param *param, unsigned int dmao_id)
{
	if (dmao_id == imgo_m1) {
		if (param->mbn_spar_pow < 1 || param->mbn_spar_pow > 6) {
			dev_info(dev, "%s:Invalid mbn_spar_pow: %d",
				__func__, param->mbn_spar_pow);
			return;
		}
		// concatenated
		*tg_width_temp *= param->mbn_spar_fac;
		*tg_height_temp /= param->mbn_spar_fac;

		// vertical binning
		*tg_height_temp /= mtk_cam_mraw_powi(2, param->mbn_spar_pow);

		// divided for 2 path from MBN
		*tg_width_temp /= 2;
	} else if (dmao_id == cpio_m1) {
		if (param->cpi_spar_pow < 1 || param->cpi_spar_pow > 6) {
			dev_info(dev, "%s:Invalid cpi_spar_pow: %d",
				__func__, param->cpi_spar_pow);
			return;
		}
		// concatenated
		*tg_width_temp *= param->cpi_spar_fac;
		*tg_height_temp /= param->cpi_spar_fac;

		// vertical binning
		*tg_height_temp /= mtk_cam_mraw_powi(2, param->cpi_spar_pow);
	}
}

static void mtk_cam_mraw_set_interleving_fmt(
	unsigned int *tg_width_temp,
	unsigned int *tg_height_temp, unsigned int dmao_id)
{
	if (dmao_id == imgo_m1) {
		// divided for 2 path from MBN
		*tg_height_temp /= 2;
	} else if (dmao_id == cpio_m1) {
		// concatenated
		*tg_width_temp *= 2;
		*tg_height_temp /= 2;
	}
}

static void mtk_cam_mraw_set_mraw_dmao_info(
	struct mtk_cam_device *cam, unsigned int pipe_id,
	struct dma_info *info, unsigned int imgo_fmt)
{
	unsigned int width_mbn = 0, height_mbn = 0, height_dbg = 0, width_dbg = 0;
	unsigned int width_cpi = 0, height_cpi = 0;
	int i;
	struct mtk_mraw_pipeline *pipe =
		&cam->pipelines.mraw[pipe_id - MTKCAM_SUBDEV_MRAW_START];

	struct mraw_stats_cfg_param *param = &pipe->res_config.stats_cfg_param;

	if (param->dbg_en) {
		mtk_cam_mraw_get_dbg_size(cam, pipe_id, &width_dbg, &height_dbg);
		mtk_cam_mraw_get_cpi_size(cam, pipe_id, &width_cpi, &height_cpi);
		mtk_cam_mraw_get_mbn_size(cam, pipe_id, &width_mbn, &height_mbn);
	} else {
		mtk_cam_mraw_get_mbn_size(cam, pipe_id, &width_mbn, &height_mbn);
		mtk_cam_mraw_get_cpi_size(cam, pipe_id, &width_cpi, &height_cpi);
	}

	/* IMGO */
	if (param->dbg_en) {
		info[imgo_m1].width = mtk_cam_mraw_dbg_xsize_cal(width_dbg, imgo_fmt);
		info[imgo_m1].height = height_dbg;
		info[imgo_m1].xsize = mtk_cam_mraw_dbg_xsize_cal(width_dbg, imgo_fmt);
		info[imgo_m1].stride = info[imgo_m1].xsize;
	} else {
		info[imgo_m1].width = mtk_cam_mraw_xsize_cal(width_mbn);
		info[imgo_m1].height = height_mbn;
		info[imgo_m1].xsize = mtk_cam_mraw_xsize_cal(width_mbn);
		info[imgo_m1].stride = info[imgo_m1].xsize;
	}

	/* IMGBO */
	info[imgbo_m1].width = mtk_cam_mraw_xsize_cal(width_mbn);
	info[imgbo_m1].height = height_mbn;
	info[imgbo_m1].xsize = mtk_cam_mraw_xsize_cal(width_mbn);
	info[imgbo_m1].stride = info[imgbo_m1].xsize;

	/* CPIO */
	info[cpio_m1].width = mtk_cam_mraw_xsize_cal_cpio(width_cpi);
	info[cpio_m1].height = height_cpi;
	info[cpio_m1].xsize = mtk_cam_mraw_xsize_cal_cpio(width_cpi);
	info[cpio_m1].stride = info[cpio_m1].xsize;

	if (atomic_read(&pipe->res_config.is_fmt_change) == 1) {
		for (i = 0; i < mraw_dmao_num; i++)
			pipe->res_config.mraw_dma_size[i] = info[i].stride * info[i].height;

		dev_info(cam->dev, "%s imgo_size:%d imgbo_size:%d cpio_size:%d", __func__,
		pipe->res_config.mraw_dma_size[imgo_m1],
		pipe->res_config.mraw_dma_size[imgbo_m1],
		pipe->res_config.mraw_dma_size[cpio_m1]);
		atomic_set(&pipe->res_config.is_fmt_change, 0);
	}

	for (i = 0; i < mraw_dmao_num; i++) {
		dev_dbg(cam->dev, "dma_id:%d, w:%d s:%d xsize:%d stride:%d\n",
			i, info[i].width, info[i].height, info[i].xsize, info[i].stride);
	}
}

void mtk_cam_mraw_copy_user_input_param(struct mtk_cam_device *cam,
	void *vaddr, struct mtk_mraw_pipeline *mraw_pipe)
{
	struct mraw_stats_cfg_param *param =
		&mraw_pipe->res_config.stats_cfg_param;

	CALL_PLAT_V4L2(
		get_mraw_stats_cfg_param, vaddr, param);

	if (mraw_pipe->res_config.tg_crop.s.w < param->crop_width ||
		mraw_pipe->res_config.tg_crop.s.h < param->crop_height)
		dev_info(cam->dev, "%s tg size smaller than crop size", __func__);

	dev_dbg(cam->dev, "%s:enable:(%d,%d,%d,%d,%d) crop:(%d,%d) mqe:%d mbn:0x%x_%x_%x_%x_%x_%x_%x_%x cpi:0x%x_%x_%x_%x_%x_%x_%x_%x sel:0x%x_%x lm_ctl:%d\n",
		__func__,
		param->mqe_en,
		param->mobc_en,
		param->plsc_en,
		param->lm_en,
		param->dbg_en,
		param->crop_width,
		param->crop_height,
		param->mqe_mode,
		param->mbn_hei,
		param->mbn_pow,
		param->mbn_dir,
		param->mbn_spar_hei,
		param->mbn_spar_pow,
		param->mbn_spar_fac,
		param->mbn_spar_con1,
		param->mbn_spar_con0,
		param->cpi_th,
		param->cpi_pow,
		param->cpi_dir,
		param->cpi_spar_hei,
		param->cpi_spar_pow,
		param->cpi_spar_fac,
		param->cpi_spar_con1,
		param->cpi_spar_con0,
		param->img_sel,
		param->imgo_sel,
		param->lm_mode_ctrl);
}

static void mtk_cam_mraw_set_frame_param_dmao(
	struct mtk_cam_device *cam,
	struct mtkcam_ipi_mraw_frame_param *mraw_param,
	struct dma_info *info, int pipe_id,
	dma_addr_t buf_daddr)
{
	struct mtkcam_ipi_img_output *mraw_img_outputs;
	int i;
	unsigned long offset;
	struct mtk_mraw_pipeline *pipe =
		&cam->pipelines.mraw[pipe_id - MTKCAM_SUBDEV_MRAW_START];

	mraw_param->pipe_id = pipe_id;
	offset =
		(((buf_daddr + GET_PLAT_V4L2(meta_mraw_ext_size) + 15) >> 4) << 4) -
		buf_daddr;

	for (i = 0; i < mraw_dmao_num; i++) {
		mraw_img_outputs = &mraw_param->mraw_img_outputs[i];

		mraw_img_outputs->uid.id = MTKCAM_IPI_MRAW_META_STATS_0;
		mraw_img_outputs->uid.pipe_id = pipe_id;

		mraw_img_outputs->fmt.stride[0] = info[i].stride;
		mraw_img_outputs->fmt.s.w = info[i].width;
		mraw_img_outputs->fmt.s.h = info[i].height;

		mraw_img_outputs->crop.p.x = 0;
		mraw_img_outputs->crop.p.y = 0;
		mraw_img_outputs->crop.s.w = info[i].width;
		mraw_img_outputs->crop.s.h = info[i].height;

		mraw_img_outputs->buf[0][0].iova = buf_daddr + offset;
		mraw_img_outputs->buf[0][0].size =
			mraw_img_outputs->fmt.stride[0] * mraw_img_outputs->fmt.s.h;

		offset = offset + (((pipe->res_config.mraw_dma_size[i] + 15) >> 4) << 4);


		dev_dbg(cam->dev, "%s:dmao_id:%d iova:0x%llx stride:0x%x height:0x%x size:%d offset:%lu\n",
			__func__, i, mraw_img_outputs->buf[0][0].iova,
			mraw_img_outputs->fmt.stride[0], mraw_img_outputs->fmt.s.h,
			pipe->res_config.mraw_dma_size[i], offset);
	}
}

static void mtk_cam_mraw_set_meta_stats_info(
	void *vaddr, struct dma_info *info)
{
	CALL_PLAT_V4L2(
		set_mraw_meta_stats_info, MTKCAM_IPI_MRAW_META_STATS_0, vaddr, info);
}

int mtk_cam_mraw_cal_cfg_info(struct mtk_cam_device *cam,
	unsigned int pipe_id, struct mtkcam_ipi_mraw_frame_param *mraw_param,
	unsigned int imgo_fmt)
{
	struct dma_info info[mraw_dmao_num];
	struct mtk_mraw_pipeline *pipe =
		&cam->pipelines.mraw[pipe_id - MTKCAM_SUBDEV_MRAW_START];

	mtk_cam_mraw_set_mraw_dmao_info(cam, pipe_id, info, imgo_fmt);
	mtk_cam_mraw_set_frame_param_dmao(cam, mraw_param,
		info, pipe_id,
		pipe->res_config.daddr[MTKCAM_IPI_MRAW_META_STATS_0
			- MTKCAM_IPI_MRAW_ID_START]);
	mtk_cam_mraw_set_meta_stats_info(
		pipe->res_config.vaddr[MTKCAM_IPI_MRAW_META_STATS_0
			- MTKCAM_IPI_MRAW_ID_START], info);

	return 0;
}

void mtk_cam_mraw_get_mqe_size(struct mtk_cam_device *cam, unsigned int pipe_id,
	unsigned int *width, unsigned int *height)
{
	struct mtk_mraw_pipeline *pipe =
		&cam->pipelines.mraw[pipe_id - MTKCAM_SUBDEV_MRAW_START];
	struct mraw_stats_cfg_param *param = &pipe->res_config.stats_cfg_param;

	*width = param->crop_width;
	*height = param->crop_height;

	if (param->lm_en && param->lm_mode_ctrl) {
		*width = param->crop_width * (2 * param->lm_mode_ctrl);
		*height = param->crop_height / (2 * param->lm_mode_ctrl);
	}

	if (param->mqe_en) {
		switch (param->mqe_mode) {
		case UL_MODE:
		case UR_MODE:
		case DL_MODE:
		case DR_MODE:
			*width /= 2;
			*height /= 2;
			break;
		case PD_L_MODE:
		case PD_R_MODE:
		case PD_M_MODE:
		case PD_B01_MODE:
			*width /= 2;
			break;
		case PD_B02_MODE:
			*height /= 2;
			break;
		default:
			dev_info(cam->dev, "%s:MQE-Mode %d %s fail\n",
				__func__, param->mqe_mode, "unknown idx");
			return;
		}
	}
}

void mtk_cam_mraw_get_mbn_size(struct mtk_cam_device *cam, unsigned int pipe_id,
	unsigned int *width, unsigned int *height)
{
	struct mtk_mraw_pipeline *pipe =
		&cam->pipelines.mraw[pipe_id - MTKCAM_SUBDEV_MRAW_START];
	struct mraw_stats_cfg_param *param = &pipe->res_config.stats_cfg_param;

	mtk_cam_mraw_get_mqe_size(cam, pipe_id, width, height);

	switch (param->mbn_dir) {
	case MBN_POW_VERTICAL:
	case MBN_POW_HORIZONTAL:
		mtk_cam_mraw_set_dense_fmt(
			cam->engines.mraw_devs[pipe_id - MTKCAM_SUBDEV_MRAW_START],
			width, height, param, imgo_m1);
		break;
	case MBN_POW_SPARSE_CONCATENATION:
		mtk_cam_mraw_set_concatenation_fmt(
			cam->engines.mraw_devs[pipe_id - MTKCAM_SUBDEV_MRAW_START],
			width, height, param, imgo_m1);
		break;
	case MBN_POW_SPARSE_INTERLEVING:
		mtk_cam_mraw_set_interleving_fmt(width, height, imgo_m1);
		break;
	default:
		dev_info(cam->engines.mraw_devs[pipe_id - MTKCAM_SUBDEV_MRAW_START],
			"%s:MBN's dir %d %s fail",
			__func__, param->mbn_dir, "unknown idx");
		return;
	}
}

void mtk_cam_mraw_get_cpi_size(struct mtk_cam_device *cam, unsigned int pipe_id,
	unsigned int *width, unsigned int *height)
{
	struct mtk_mraw_pipeline *pipe =
		&cam->pipelines.mraw[pipe_id - MTKCAM_SUBDEV_MRAW_START];
	struct mraw_stats_cfg_param *param = &pipe->res_config.stats_cfg_param;

	mtk_cam_mraw_get_mqe_size(cam, pipe_id, width, height);

	switch (param->cpi_dir) {
	case CPI_POW_VERTICAL:
	case CPI_POW_HORIZONTAL:
		mtk_cam_mraw_set_dense_fmt(
			cam->engines.mraw_devs[pipe_id - MTKCAM_SUBDEV_MRAW_START],
			width, height, param, cpio_m1);
		break;
	case CPI_POW_SPARSE_CONCATENATION:
		mtk_cam_mraw_set_concatenation_fmt(
			cam->engines.mraw_devs[pipe_id - MTKCAM_SUBDEV_MRAW_START],
			width, height, param, cpio_m1);
		break;
	case CPI_POW_SPARSE_INTERLEVING:
		mtk_cam_mraw_set_interleving_fmt(width, height, cpio_m1);
		break;
	default:
		dev_info(cam->engines.mraw_devs[pipe_id - MTKCAM_SUBDEV_MRAW_START],
			"%s:CPI's dir %d %s fail",
			__func__, param->cpi_dir, "unknown idx");
		return;
	}
}

void mtk_cam_mraw_get_dbg_size(struct mtk_cam_device *cam, unsigned int pipe_id,
	unsigned int *width, unsigned int *height)
{
	struct mtk_mraw_pipeline *pipe =
		&cam->pipelines.mraw[pipe_id - MTKCAM_SUBDEV_MRAW_START];
	struct mraw_stats_cfg_param *param = &pipe->res_config.stats_cfg_param;

	switch (param->img_sel) {
	case 0:  // CRP output
		*width = param->crop_width;
		*height = param->crop_height;
		break;
	case 1:  // MQE output
	case 2:  // PLSC output
	case 3:  // SGG output
		mtk_cam_mraw_get_mqe_size(cam, pipe_id, width, height);
		break;
	default:
		dev_info(cam->dev, "%s:DBG's img_sel %d %s fail",
			__func__, param->img_sel, "unknown idx");
		return;
	}
}

int mtk_cam_mraw_reset_msgfifo(struct mtk_mraw_device *mraw_dev)
{
	atomic_set(&mraw_dev->is_fifo_overflow, 0);
	return kfifo_init(&mraw_dev->msg_fifo, mraw_dev->msg_buffer, mraw_dev->fifo_size);
}

static int push_msgfifo(struct mtk_mraw_device *mraw_dev,
			struct mtk_camsys_irq_info *info)
{
	int len;

	if (unlikely(kfifo_avail(&mraw_dev->msg_fifo) < sizeof(*info))) {
		atomic_set(&mraw_dev->is_fifo_overflow, 1);
		return -1;
	}

	len = kfifo_in(&mraw_dev->msg_fifo, info, sizeof(*info));
	WARN_ON(len != sizeof(*info));

	return 0;
}

void mraw_reset_by_mraw_top(struct mtk_mraw_device *mraw_dev)
{
	writel(0, mraw_dev->top + REG_CAMSYS_MRAW_SW_RST);
	writel(3 << ((mraw_dev->id) * 2 + 4), mraw_dev->top + REG_CAMSYS_MRAW_SW_RST);
	writel(0, mraw_dev->top + REG_CAMSYS_MRAW_SW_RST);
	wmb(); /* make sure committed */
}

void mraw_reset(struct mtk_mraw_device *mraw_dev)
{
	int sw_ctl;
	int ret;

	writel(0, mraw_dev->base + REG_MRAW_CTL_SW_CTL);
	writel(1, mraw_dev->base + REG_MRAW_CTL_SW_CTL);
	wmb(); /* make sure committed */

	ret = readx_poll_timeout(readl, mraw_dev->base + REG_MRAW_CTL_SW_CTL, sw_ctl,
				 sw_ctl & 0x2,
				 1 /* delay, us */,
				 100000 /* timeout, us */);
	if (ret < 0) {
		dev_info(mraw_dev->dev, "%s: timeout\n", __func__);

		dev_info(mraw_dev->dev,
			 "tg_sen_mode: 0x%x, ctl_en: 0x%x, ctl_sw_ctl:0x%x, frame_no:0x%x\n",
			 readl(mraw_dev->base + REG_MRAW_TG_SEN_MODE),
			 readl(mraw_dev->base + REG_MRAW_CTL_MOD_EN),
			 readl(mraw_dev->base + REG_MRAW_CTL_SW_CTL),
			 readl(mraw_dev->base + REG_MRAW_FRAME_SEQ_NUM)
			);
		dev_info(mraw_dev->dev,
			"%s  cam_main_gals_dbg_status 0x%x, cam_main_ppc_prot_rdy_0 0x%x, cam_main_ppc_prot_rdy_1 0x%x",
			__func__,
			readl(mraw_dev->cam->base + 0x414),
			readl(mraw_dev->cam->base + 0x588),
			readl(mraw_dev->cam->base + 0x58c)
		);
		mtk_smi_dbg_hang_detect("camsys-mraw");

		goto RESET_FAILURE;
	}

	// do hw rst
	writel(4, mraw_dev->base + REG_MRAW_CTL_SW_CTL);
	writel(0, mraw_dev->base + REG_MRAW_CTL_SW_CTL);
	wmb(); /* make sure committed */

RESET_FAILURE:
	return;
}

int mtk_cam_mraw_top_config(struct mtk_mraw_device *mraw_dev)
{
	int ret = 0;

	unsigned int int_en1 = (MRAW_INT_EN1_TG_ERR_EN |
							MRAW_INT_EN1_TG_GBERR_EN |
							MRAW_INT_EN1_TG_SOF_INT_EN |
							MRAW_INT_EN1_CQ_SUB_CODE_ERR_EN |
							MRAW_INT_EN1_CQ_DB_LOAD_ERR_EN |
							MRAW_INT_EN1_CQ_SUB_VS_ERR_EN |
							MRAW_INT_EN1_CQ_TRIG_DLY_INT_EN |
							MRAW_INT_EN1_SW_PASS1_DONE_EN |
							MRAW_INT_EN1_DMA_ERR_EN |
							MRAW_INT_EN1_SW_ENQUE_ERR_EN
							);

	unsigned int int_en5 = (MRAW_INT_EN5_IMGO_M1_ERR_EN |
							MRAW_INT_EN5_IMGBO_M1_ERR_EN |
							MRAW_INT_EN5_CPIO_M1_ERR_EN
							);

	/* int en */
	MRAW_WRITE_REG(mraw_dev->base + REG_MRAW_CTL_INT_EN, int_en1);
	MRAW_WRITE_REG(mraw_dev->base + REG_MRAW_CTL_INT5_EN, int_en5);

	/* db load src */
	MRAW_WRITE_BITS(mraw_dev->base + REG_MRAW_CTL_MISC,
		MRAW_CTL_MISC, MRAWCTL_DB_LOAD_SRC, MRAW_DB_SRC_SOF);

	/* reset sof count */
	mraw_dev->sof_count = 0;

	return ret;
}

int mtk_cam_mraw_dma_config(struct mtk_mraw_device *mraw_dev)
{
	struct mraw_dma_th_setting mraw_th_setting[mraw_dmao_num];
	struct mraw_cq_th_setting mraw_cq_setting;

	memset(mraw_th_setting, 0, sizeof(mraw_th_setting));
	memset(&mraw_cq_setting, 0, sizeof(mraw_cq_setting));


	CALL_PLAT_V4L2(
		get_mraw_dmao_common_setting, mraw_th_setting, &mraw_cq_setting);
	/* imgo con */
	MRAW_WRITE_REG(mraw_dev->base + REG_MRAW_IMGO_ORIWDMA_CON0,
		mraw_th_setting[imgo_m1].fifo_size);  // BURST_LEN and FIFO_SIZE
	MRAW_WRITE_REG(mraw_dev->base + REG_MRAW_IMGO_ORIWDMA_CON1,
		mraw_th_setting[imgo_m1].pultra_th);  // Threshold for pre-ultra
	MRAW_WRITE_REG(mraw_dev->base + REG_MRAW_IMGO_ORIWDMA_CON2,
		mraw_th_setting[imgo_m1].ultra_th);  // Threshold for ultra
	MRAW_WRITE_REG(mraw_dev->base + REG_MRAW_IMGO_ORIWDMA_CON3,
		mraw_th_setting[imgo_m1].urgent_th);  // Threshold for urgent
	MRAW_WRITE_REG(mraw_dev->base + REG_MRAW_IMGO_ORIWDMA_CON4,
		mraw_th_setting[imgo_m1].dvfs_th);  // Threshold for DVFS

	/* imgbo con */
	MRAW_WRITE_REG(mraw_dev->base + REG_MRAW_IMGBO_ORIWDMA_CON0,
		mraw_th_setting[imgbo_m1].fifo_size);  // BURST_LEN and FIFO_SIZE
	MRAW_WRITE_REG(mraw_dev->base + REG_MRAW_IMGBO_ORIWDMA_CON1,
		mraw_th_setting[imgbo_m1].pultra_th);  // Threshold for pre-ultra
	MRAW_WRITE_REG(mraw_dev->base + REG_MRAW_IMGBO_ORIWDMA_CON2,
		mraw_th_setting[imgbo_m1].ultra_th);  // Threshold for ultra
	MRAW_WRITE_REG(mraw_dev->base + REG_MRAW_IMGBO_ORIWDMA_CON3,
		mraw_th_setting[imgbo_m1].urgent_th);  // Threshold for urgent
	MRAW_WRITE_REG(mraw_dev->base + REG_MRAW_IMGBO_ORIWDMA_CON4,
		mraw_th_setting[imgbo_m1].dvfs_th);  // Threshold for DVFS

	/* cpio con */
	MRAW_WRITE_REG(mraw_dev->base + REG_MRAW_CPIO_ORIWDMA_CON0,
		mraw_th_setting[cpio_m1].fifo_size);  // BURST_LEN and FIFO_SIZE
	MRAW_WRITE_REG(mraw_dev->base + REG_MRAW_CPIO_ORIWDMA_CON1,
		mraw_th_setting[cpio_m1].pultra_th);  // Threshold for pre-ultra
	MRAW_WRITE_REG(mraw_dev->base + REG_MRAW_CPIO_ORIWDMA_CON2,
		mraw_th_setting[cpio_m1].ultra_th);  // Threshold for ultra
	MRAW_WRITE_REG(mraw_dev->base + REG_MRAW_CPIO_ORIWDMA_CON3,
		mraw_th_setting[cpio_m1].urgent_th);  // Threshold for urgent
	MRAW_WRITE_REG(mraw_dev->base + REG_MRAW_CPIO_ORIWDMA_CON4,
		mraw_th_setting[cpio_m1].dvfs_th);  // Threshold for DVFS

	/* cqi con */
	MRAW_WRITE_REG(mraw_dev->base + REG_MRAW_M1_CQI_ORIRDMA_CON0,
		mraw_cq_setting.cq1_fifo_size);  // BURST_LEN and FIFO_SIZE
	MRAW_WRITE_REG(mraw_dev->base + REG_MRAW_M1_CQI_ORIRDMA_CON1,
		mraw_cq_setting.cq1_pultra_th);  // Threshold for pre-ultra
	MRAW_WRITE_REG(mraw_dev->base + REG_MRAW_M1_CQI_ORIRDMA_CON2,
		mraw_cq_setting.cq1_ultra_th);  // Threshold for ultra
	MRAW_WRITE_REG(mraw_dev->base + REG_MRAW_M1_CQI_ORIRDMA_CON3,
		mraw_cq_setting.cq1_urgent_th);  // Threshold for urgent
	MRAW_WRITE_REG(mraw_dev->base + REG_MRAW_M1_CQI_ORIRDMA_CON4,
		mraw_cq_setting.cq1_dvfs_th);  // Threshold for DVFS

	MRAW_WRITE_REG(mraw_dev->base + REG_MRAW_M2_CQI_ORIRDMA_CON0,
		mraw_cq_setting.cq2_fifo_size);  // BURST_LEN and FIFO_SIZE
	MRAW_WRITE_REG(mraw_dev->base + REG_MRAW_M2_CQI_ORIRDMA_CON1,
		mraw_cq_setting.cq2_pultra_th);  // Threshold for pre-ultra
	MRAW_WRITE_REG(mraw_dev->base + REG_MRAW_M2_CQI_ORIRDMA_CON2,
		mraw_cq_setting.cq2_ultra_th);  // Threshold for ultra
	MRAW_WRITE_REG(mraw_dev->base + REG_MRAW_M2_CQI_ORIRDMA_CON3,
		mraw_cq_setting.cq2_urgent_th);  // Threshold for urgent
	MRAW_WRITE_REG(mraw_dev->base + REG_MRAW_M2_CQI_ORIRDMA_CON4,
		mraw_cq_setting.cq2_dvfs_th);  // Threshold for DVFS

	/* stg */
	MRAW_WRITE_REG(mraw_dev->base + REG_MRAW_STG_EN_CTRL,
		0xF);
	MRAW_WRITE_REG(mraw_dev->base + REG_MRAW_STG_NONE_SAME_PG_SEND_EN_CTRL,
		0xF);

	return 0;
}

int mtk_cam_mraw_fbc_config(
	struct mtk_mraw_device *mraw_dev)
{
	int ret = 0;

	MRAW_WRITE_REG(mraw_dev->base + REG_MRAW_CTL_WFBC_EN, 0);
	MRAW_WRITE_REG(mraw_dev->base + REG_MRAW_CTL_FBC_GROUP,
		(MRAW_WFBC_IMGO | MRAW_WFBC_IMGBO | MRAW_WFBC_CPIO));

	return ret;
}

int mtk_cam_mraw_toggle_tg_db(struct mtk_mraw_device *mraw_dev)
{
	MRAW_WRITE_BITS(mraw_dev->base + REG_MRAW_TG_PATH_CFG,
		MRAW_TG_PATH_CFG, TG_M1_DB_LOAD_DIS, 1);
	MRAW_WRITE_BITS(mraw_dev->base + REG_MRAW_TG_PATH_CFG,
		MRAW_TG_PATH_CFG, TG_M1_DB_LOAD_DIS, 0);

	return 0;
}

int mtk_cam_mraw_toggle_db(struct mtk_mraw_device *mraw_dev)
{
	MRAW_WRITE_BITS(mraw_dev->base + REG_MRAW_CTL_MISC,
		MRAW_CTL_MISC, MRAWCTL_DB_EN, 0);
	MRAW_WRITE_BITS(mraw_dev->base + REG_MRAW_CTL_MISC,
		MRAW_CTL_MISC, MRAWCTL_DB_EN, 1);

	return 0;
}

int mtk_cam_mraw_trigger_wfbc_inc(struct mtk_mraw_device *mraw_dev)
{
	MRAW_WRITE_REG(mraw_dev->base + REG_MRAW_CTL_WFBC_INC,
		(MRAW_WFBC_IMGO | MRAW_WFBC_IMGBO | MRAW_WFBC_CPIO));

	return 0;
}

int mtk_cam_mraw_top_enable(struct mtk_mraw_device *mraw_dev)
{
	int ret = 0;

	/* toggle db */
	mtk_cam_mraw_toggle_db(mraw_dev);

	/* re-trigger wfbc inc due to inner csr becomes zero after db load */
	mtk_cam_mraw_trigger_wfbc_inc(mraw_dev);

	/* toggle tg db */
	mtk_cam_mraw_toggle_tg_db(mraw_dev);

	/* enable cmos */
	dev_dbg(mraw_dev->dev, "%s: enable CMOS and VF\n", __func__);
	MRAW_WRITE_BITS(mraw_dev->base + REG_MRAW_TG_SEN_MODE,
		MRAW_TG_SEN_MODE, TG_CMOS_EN, 1);

	/* enable vf */
	if (MRAW_READ_BITS(mraw_dev->base + REG_MRAW_TG_SEN_MODE,
			MRAW_TG_SEN_MODE, TG_CMOS_EN) &&
		atomic_read(&mraw_dev->is_vf_on))
		mtk_cam_mraw_vf_on(mraw_dev, true);
	else
		dev_info(mraw_dev->dev, "%s, cmos_en:%d is_vf_on:%d\n", __func__,
			MRAW_READ_BITS(mraw_dev->base + REG_MRAW_TG_SEN_MODE,
				MRAW_TG_SEN_MODE, TG_CMOS_EN),
			atomic_read(&mraw_dev->is_vf_on));

	return ret;
}

int mtk_cam_mraw_fbc_enable(struct mtk_mraw_device *mraw_dev)
{
	int ret = 0;

	if (MRAW_READ_BITS(mraw_dev->base + REG_MRAW_TG_VF_CON,
			MRAW_TG_VF_CON, TG_M1_VFDATA_EN) == 1) {
		ret = -1;
		dev_dbg(mraw_dev->dev, "cannot enable fbc when streaming");
		goto EXIT;
	}

	MRAW_WRITE_REG(mraw_dev->base + REG_MRAW_CTL_WFBC_EN,
		(MRAW_WFBC_IMGO | MRAW_WFBC_IMGBO | MRAW_WFBC_CPIO));

EXIT:
	return ret;
}

int mtk_cam_mraw_cq_config(struct mtk_mraw_device *mraw_dev,
	unsigned int sub_ratio)
{
	u32 val;

	/* cq en */
	val = readl_relaxed(mraw_dev->base + REG_MRAW_CQ_EN);
	val = val | MRAWCQ_DB_EN;
	if (sub_ratio) {
		val = val | MRAWSCQ_SUBSAMPLE_EN;
		val = val | MRAWCQ_SOF_SEL;
	} else {
		val = val & ~MRAWSCQ_SUBSAMPLE_EN;
		val = val & ~MRAWCQ_SOF_SEL;
	}
	writel_relaxed(val, mraw_dev->base + REG_MRAW_CQ_EN);

	/* cq sub en */
	val = readl_relaxed(mraw_dev->base + REG_MRAW_CQ_SUB_EN);
	val = val | MRAWCQ_SUB_DB_EN;
	writel_relaxed(val, mraw_dev->base + REG_MRAW_CQ_SUB_EN);

	/* scq start period */
	writel_relaxed(0xFFFFFFFF, mraw_dev->base + REG_MRAW_SCQ_START_PERIOD);

	/* cq sub thr0 ctl */
	writel_relaxed(MRAWCQ_SUB_THR0_MODE_IMMEDIATE | MRAWCQ_SUB_THR0_EN,
		       mraw_dev->base + REG_MRAW_CQ_SUB_THR0_CTL);

	/* cq int en */
	writel_relaxed(MRAWCTL_CQ_SUB_THR0_DONE_EN,
		       mraw_dev->base + REG_MRAW_CTL_INT6_EN);
	wmb(); /* TBC */

	dev_dbg(mraw_dev->dev, "[%s] cq_en:0x%x_%x start_period:0x%x cq_sub_thr0_ctl:0x%x cq_int_en:0x%x\n",
		__func__,
		readl_relaxed(mraw_dev->base + REG_MRAW_CQ_EN),
		readl_relaxed(mraw_dev->base + REG_MRAW_CQ_SUB_EN),
		readl_relaxed(mraw_dev->base + REG_MRAW_SCQ_START_PERIOD),
		readl_relaxed(mraw_dev->base + REG_MRAW_CQ_SUB_THR0_CTL),
		readl_relaxed(mraw_dev->base + REG_MRAW_CTL_INT6_EN));

	return 0;
}

#define HW_TIMER_INC_PERIOD   0x2
#define DDR_GEN_BEFORE_US     3
#define QOS_GEN_BEFORE_US     100
#define MARGIN                3000
int mtk_cam_mraw_ddren_qos_config(struct mtk_mraw_device *mraw_dev, int frm_time_us)
{
	int ddr_gen_pulse, qos_gen_pulse;

	if (CAM_DEBUG_ENABLED(MMQOS))
		pr_info("%s frm_time_us %d\n", __func__, frm_time_us);

	ddr_gen_pulse = (frm_time_us - DDR_GEN_BEFORE_US - MARGIN) * SCQ_DEFAULT_CLK_RATE /
		(2 * (HW_TIMER_INC_PERIOD + 1)) - 1;

	qos_gen_pulse = (frm_time_us - QOS_GEN_BEFORE_US - MARGIN) * SCQ_DEFAULT_CLK_RATE /
		(2 * (HW_TIMER_INC_PERIOD + 1)) - 1;

	/* sw ddr en */
	MRAW_WRITE_BITS(mraw_dev->base + REG_MRAW_CTL_DDREN_CTL,
		MRAW_CTL_DDREN_CTL, MRAWCTL_DDREN_SW_SET, 1);
	MRAW_WRITE_BITS(mraw_dev->base + REG_MRAW_CTL_BW_QOS_CTL,
		MRAW_CTL_BW_QOS_CTL, MRAWCTL_BW_QOS_SW_SET, 1);

	if (ddr_gen_pulse < 0 || qos_gen_pulse < 0) {
		pr_info("%s: framelength too small, so use sw mode\n", __func__);
		atomic_set(&mraw_dev->is_sw_clr, 3);
		return 0;
	}

	if (debug_ddren_mraw_hw_mode) {
		MRAW_WRITE_BITS(mraw_dev->base + REG_MRAW_CTL_DDREN_CTL,
			MRAW_CTL_DDREN_CTL, MRAWCTL_DDREN_HW_EN, 1);
		MRAW_WRITE_BITS(mraw_dev->base + REG_MRAW_TG_HW_TIMER_CTL,
			MRAW_TG_TIMER_CTL, TG_HW_TIMER_EN, 1);
		MRAW_WRITE_REG(mraw_dev->base + REG_MRAW_TG_HW_TIMER_INC_PERIOD,
			HW_TIMER_INC_PERIOD);
		MRAW_WRITE_REG(mraw_dev->base + REG_MRAW_TG_HW_DDR_GEN_PLUS_CNT,
			ddr_gen_pulse);
		MRAW_WRITE_BITS(mraw_dev->base + REG_MRAW_CTL_BW_QOS_CTL,
			MRAW_CTL_BW_QOS_CTL, MRAWCTL_BW_QOS_HW_EN, 1);
		MRAW_WRITE_REG(mraw_dev->base + REG_MRAW_TG_HW_QOS_GEN_PLUS_CNT,
			qos_gen_pulse);
	}
	return 0;
}

#define MRAW_TS_CNT 0x2
void mtk_cam_mraw_update_start_period(
	struct mtk_mraw_device *mraw_dev, int scq_ms)
{
	u32 scq_cnt_rate, start_period;

	writel(0x101, mraw_dev->base + REG_MRAW_TG_TIME_STAMP_CTL);
	writel(MRAW_TS_CNT, mraw_dev->base + REG_MRAW_TG_TIME_STAMP_CNT);

	/* scq count rate(khz) */
	scq_cnt_rate = SCQ_DEFAULT_CLK_RATE * 1000 / ((MRAW_TS_CNT + 1) * 2);
	start_period = (scq_ms == -1) ? 0xFFFFFFFF : scq_ms * scq_cnt_rate;

	/* scq start period */
	writel_relaxed(start_period,
		mraw_dev->base + REG_MRAW_SCQ_START_PERIOD);

	dev_info(mraw_dev->dev, "[%s] start_period:0x%x ts_cnt:%d, scq_ms:%d\n",
		__func__,
		readl_relaxed(mraw_dev->base + REG_MRAW_SCQ_START_PERIOD),
		MRAW_TS_CNT, scq_ms);
}

int mtk_cam_mraw_cq_disable(struct mtk_mraw_device *mraw_dev)
{
	int ret = 0;

	writel_relaxed(0, mraw_dev->base + REG_MRAW_CQ_SUB_THR0_CTL);
	wmb(); /* TBC */

	return ret;
}

int mtk_cam_mraw_tg_disable(struct mtk_mraw_device *mraw_dev)
{
	int ret = 0;
	u32 val;

	dev_dbg(mraw_dev->dev, "stream off, disable CMOS\n");
	val = readl(mraw_dev->base + REG_MRAW_TG_SEN_MODE);
	writel(val & (~MRAWTG_CMOS_EN),
		mraw_dev->base + REG_MRAW_TG_SEN_MODE);

	return ret;
}

int mtk_cam_mraw_top_disable(struct mtk_mraw_device *mraw_dev)
{
	int ret = 0;

	if (MRAW_READ_BITS(mraw_dev->base + REG_MRAW_TG_VF_CON,
			MRAW_TG_VF_CON, TG_M1_VFDATA_EN)) {
		MRAW_WRITE_BITS(mraw_dev->base + REG_MRAW_TG_VF_CON,
			MRAW_TG_VF_CON, TG_M1_VFDATA_EN, 0);
		mtk_cam_mraw_toggle_tg_db(mraw_dev);
	}

	mraw_reset(mraw_dev);

	MRAW_WRITE_BITS(mraw_dev->base + REG_MRAW_CTL_MISC,
		MRAW_CTL_MISC, MRAWCTL_DB_EN, 0);
	MRAW_WRITE_REG(mraw_dev->base + REG_MRAW_CTL_MISC, 0);
	MRAW_WRITE_REG(mraw_dev->base + REG_MRAW_CTL_FMT_SEL, 0);
	MRAW_WRITE_REG(mraw_dev->base + REG_MRAW_CTL_INT_EN, 0);
	MRAW_WRITE_REG(mraw_dev->base + REG_MRAW_CTL_INT5_EN, 0);

	MRAW_WRITE_BITS(mraw_dev->base + REG_MRAW_CTL_MISC,
		MRAW_CTL_MISC, MRAWCTL_DB_EN, 1);
	return ret;
}

int mtk_cam_mraw_dma_disable(struct mtk_mraw_device *mraw_dev)
{
	int ret = 0;

	MRAW_WRITE_REG(mraw_dev->base + REG_MRAW_CTL_MOD3_EN, 0);

	return ret;
}

int mtk_cam_mraw_fbc_disable(struct mtk_mraw_device *mraw_dev)
{
	int ret = 0;

	MRAW_WRITE_REG(mraw_dev->base + REG_MRAW_CTL_WFBC_EN, 0);
	MRAW_WRITE_REG(mraw_dev->base + REG_MRAW_CTL_FBC_GROUP, 0);

	return ret;
}

int mtk_cam_mraw_vf_on(struct mtk_mraw_device *mraw_dev, bool on)
{
	int ret = 0;

	if (on) {
		if (MRAW_READ_BITS(mraw_dev->base + REG_MRAW_TG_VF_CON,
			MRAW_TG_VF_CON, TG_M1_VFDATA_EN) == 0)
			MRAW_WRITE_BITS(mraw_dev->base + REG_MRAW_TG_VF_CON,
				MRAW_TG_VF_CON, TG_M1_VFDATA_EN, 1);
	} else {
		if (MRAW_READ_BITS(mraw_dev->base + REG_MRAW_TG_VF_CON,
			MRAW_TG_VF_CON, TG_M1_VFDATA_EN) == 1)
			MRAW_WRITE_BITS(mraw_dev->base + REG_MRAW_TG_VF_CON,
				MRAW_TG_VF_CON, TG_M1_VFDATA_EN, 0);
	}

	return ret;
}

int mtk_cam_mraw_is_vf_on(struct mtk_mraw_device *mraw_dev)
{
	if (MRAW_READ_BITS(mraw_dev->base + REG_MRAW_TG_VF_CON,
			MRAW_TG_VF_CON, TG_M1_VFDATA_EN) == 1)
		return 1;
	else
		return 0;
}

int mtk_cam_mraw_dev_config(struct mtk_mraw_device *mraw_dev,
	unsigned int sub_ratio, int frm_time_us)
{
	engine_fsm_reset(&mraw_dev->fsm, mraw_dev->dev);
	mraw_dev->cq_ref = NULL;

	/* reset vf on status */
	atomic_set(&mraw_dev->is_vf_on, 0);
	atomic_set(&mraw_dev->is_sw_clr, 0);

	mtk_cam_mraw_top_config(mraw_dev);
	mtk_cam_mraw_dma_config(mraw_dev);
	mtk_cam_mraw_fbc_config(mraw_dev);
	mtk_cam_mraw_fbc_enable(mraw_dev);
	mtk_cam_mraw_cq_config(mraw_dev, sub_ratio);
	mtk_cam_mraw_ddren_qos_config(mraw_dev, frm_time_us);

	dev_dbg(mraw_dev->dev, "[%s] sub_ratio:%d\n", __func__, sub_ratio);

	return 0;
}

int mtk_cam_mraw_dev_stream_on(struct mtk_mraw_device *mraw_dev, bool on)
{
	int ret = 0;

	if (on)
		ret = mtk_cam_mraw_top_enable(mraw_dev);
	else {
		/* reset vf on status */
		atomic_set(&mraw_dev->is_vf_on, 0);

		ret = mtk_cam_mraw_top_disable(mraw_dev) ||
			mtk_cam_mraw_cq_disable(mraw_dev) ||
			mtk_cam_mraw_fbc_disable(mraw_dev) ||
			mtk_cam_mraw_dma_disable(mraw_dev) ||
			mtk_cam_mraw_tg_disable(mraw_dev);
	}

	dev_info(mraw_dev->dev, "%s: mraw-%d en(%d)\n",
		__func__, mraw_dev->id, (on) ? 1 : 0);

	return ret;
}

void mtk_cam_mraw_debug_dump(struct mtk_mraw_device *mraw_dev)
{
	int imgo_error_status, imgbo_error_status, cpio_error_status;

	dev_info_ratelimited(mraw_dev->dev,
		"seq_no:%d_%d tg_sen_mode:0x%x tg_vf_con:0x%x tg_path_cfg:0x%x frm_size:0x%x frm_size_r:0x%x grab_pix:0x%x grab_lin:0x%x\n",
		readl_relaxed(mraw_dev->base_inner + REG_MRAW_FRAME_SEQ_NUM),
		readl_relaxed(mraw_dev->base + REG_MRAW_FRAME_SEQ_NUM),
		readl_relaxed(mraw_dev->base_inner + REG_MRAW_TG_SEN_MODE),
		readl_relaxed(mraw_dev->base_inner + REG_MRAW_TG_VF_CON),
		readl_relaxed(mraw_dev->base_inner + REG_MRAW_TG_PATH_CFG),
		readl_relaxed(mraw_dev->base_inner + REG_MRAW_TG_FRMSIZE_ST),
		readl_relaxed(mraw_dev->base_inner + REG_MRAW_TG_FRMSIZE_ST_R),
		readl_relaxed(mraw_dev->base_inner + REG_MRAW_TG_SEN_GRAB_PXL),
		readl_relaxed(mraw_dev->base_inner + REG_MRAW_TG_SEN_GRAB_LIN));

	dev_info_ratelimited(mraw_dev->dev, "mod_en:0x%x mod2_en:0x%x mod3_en:0x%x mod4_en:0x%x mod_ctl:0x%x sel:0x%x fmt_sel:0x%x done_sel:0x%x\n",
		readl_relaxed(mraw_dev->base_inner + REG_MRAW_CTL_MOD_EN),
		readl_relaxed(mraw_dev->base_inner + REG_MRAW_CTL_MOD2_EN),
		readl_relaxed(mraw_dev->base_inner + REG_MRAW_CTL_MOD3_EN),
		readl_relaxed(mraw_dev->base_inner + REG_MRAW_CTL_MOD4_EN),
		readl_relaxed(mraw_dev->base_inner + REG_MRAW_CTL_MODE_CTL),
		readl_relaxed(mraw_dev->base_inner + REG_MRAW_CTL_SEL),
		readl_relaxed(mraw_dev->base_inner + REG_MRAW_CTL_FMT_SEL),
		readl_relaxed(mraw_dev->base_inner + REG_MRAW_CTL_DONE_SEL));

	dev_info_ratelimited(mraw_dev->dev, "imgo_xsize:0x%x imgo_ysize:0x%x imgo_stride:0x%x imgo_addr:0x%x_%x imgo_ofst_addr:0x%x_%x\n",
		readl_relaxed(mraw_dev->base_inner + REG_MRAW_IMGO_XSIZE),
		readl_relaxed(mraw_dev->base_inner + REG_MRAW_IMGO_YSIZE),
		readl_relaxed(mraw_dev->base_inner + REG_MRAW_IMGO_STRIDE),
		readl_relaxed(mraw_dev->base_inner + REG_MRAW_IMGO_BASE_ADDR_MSB),
		readl_relaxed(mraw_dev->base_inner + REG_MRAW_IMGO_BASE_ADDR),
		readl_relaxed(mraw_dev->base_inner + REG_MRAW_IMGO_OFST_ADDR_MSB),
		readl_relaxed(mraw_dev->base_inner + REG_MRAW_IMGO_OFST_ADDR));

	dev_info_ratelimited(mraw_dev->dev, "imgbo_xsize:0x%x imgbo_ysize:0x%x imgbo_stride:0x%x imgbo_addr:0x%x_%x imgbo_ofst_addr:0x%x_%x\n",
		readl_relaxed(mraw_dev->base_inner + REG_MRAW_IMGBO_XSIZE),
		readl_relaxed(mraw_dev->base_inner + REG_MRAW_IMGBO_YSIZE),
		readl_relaxed(mraw_dev->base_inner + REG_MRAW_IMGBO_STRIDE),
		readl_relaxed(mraw_dev->base_inner + REG_MRAW_IMGBO_BASE_ADDR_MSB),
		readl_relaxed(mraw_dev->base_inner + REG_MRAW_IMGBO_BASE_ADDR),
		readl_relaxed(mraw_dev->base_inner + REG_MRAW_IMGBO_OFST_ADDR_MSB),
		readl_relaxed(mraw_dev->base_inner + REG_MRAW_IMGBO_OFST_ADDR));

	dev_info_ratelimited(mraw_dev->dev, "cpio_xsize:0x%x cpio_ysize:0x%x cpio_stride:0x%x cpio_addr:0x%x_%x cpio_ofst_addr:0x%x_%x\n",
		readl_relaxed(mraw_dev->base_inner + REG_MRAW_CPIO_XSIZE),
		readl_relaxed(mraw_dev->base_inner + REG_MRAW_CPIO_YSIZE),
		readl_relaxed(mraw_dev->base_inner + REG_MRAW_CPIO_STRIDE),
		readl_relaxed(mraw_dev->base_inner + REG_MRAW_CPIO_BASE_ADDR_MSB),
		readl_relaxed(mraw_dev->base_inner + REG_MRAW_CPIO_BASE_ADDR),
		readl_relaxed(mraw_dev->base_inner + REG_MRAW_CPIO_OFST_ADDR_MSB),
		readl_relaxed(mraw_dev->base_inner + REG_MRAW_CPIO_OFST_ADDR));

	imgo_error_status = readl_relaxed(mraw_dev->base_inner + REG_MRAW_IMGO_ERR_STAT);
	imgbo_error_status = readl_relaxed(mraw_dev->base_inner + REG_MRAW_IMGBO_ERR_STAT);
	cpio_error_status = readl_relaxed(mraw_dev->base_inner + REG_MRAW_CPIO_ERR_STAT);
	dev_info_ratelimited(mraw_dev->dev,
		"imgo_error_status:0x%x imgbo_error_status:0x%x cpio_error_status:0x%x\n",
		readl_relaxed(mraw_dev->base_inner + REG_MRAW_IMGO_ERR_STAT),
		readl_relaxed(mraw_dev->base_inner + REG_MRAW_IMGBO_ERR_STAT),
		readl_relaxed(mraw_dev->base_inner + REG_MRAW_CPIO_ERR_STAT));

	if (imgo_error_status || imgbo_error_status || cpio_error_status) {
		writel(0x3000103, mraw_dev->base + REG_MRAW_DMA_DBG_SEL);
		dev_info(mraw_dev->dev, "state checksum 0x%x",
			readl_relaxed(mraw_dev->base + REG_MRAW_DMA_DBG_PORT));
		writel(0x3000203, mraw_dev->base + REG_MRAW_DMA_DBG_SEL);
		dev_info(mraw_dev->dev, "line_pix_cnt_tmp 0x%x",
			readl_relaxed(mraw_dev->base + REG_MRAW_DMA_DBG_PORT));
		writel(0x3000303, mraw_dev->base + REG_MRAW_DMA_DBG_SEL);
		dev_info(mraw_dev->dev, "line_pix_cnt 0x%x",
			readl_relaxed(mraw_dev->base + REG_MRAW_DMA_DBG_PORT));
		writel(0x3000403, mraw_dev->base + REG_MRAW_DMA_DBG_SEL);
		dev_info(mraw_dev->dev, "important_status 0x%x",
			readl_relaxed(mraw_dev->base + REG_MRAW_DMA_DBG_PORT));
		writel(0x3000503, mraw_dev->base + REG_MRAW_DMA_DBG_SEL);
		dev_info(mraw_dev->dev, "cmd_data_cnt 0x%x",
			readl_relaxed(mraw_dev->base + REG_MRAW_DMA_DBG_PORT));
		writel(0x3000603, mraw_dev->base + REG_MRAW_DMA_DBG_SEL);
		dev_info(mraw_dev->dev, "cmd_cnt_for_bvalid_phase 0x%x",
			readl_relaxed(mraw_dev->base + REG_MRAW_DMA_DBG_PORT));
		writel(0x3000703, mraw_dev->base + REG_MRAW_DMA_DBG_SEL);
		dev_info(mraw_dev->dev, "input_h_cnt 0x%x",
			readl_relaxed(mraw_dev->base + REG_MRAW_DMA_DBG_PORT));
		writel(0x3000803, mraw_dev->base + REG_MRAW_DMA_DBG_SEL);
		dev_info(mraw_dev->dev, "input_v_cnt 0x%x",
			readl_relaxed(mraw_dev->base + REG_MRAW_DMA_DBG_PORT));
		writel(0x3000903, mraw_dev->base + REG_MRAW_DMA_DBG_SEL);
		dev_info(mraw_dev->dev, "xfer_y_cnt 0x%x",
			readl_relaxed(mraw_dev->base + REG_MRAW_DMA_DBG_PORT));
		writel(0x3000A03, mraw_dev->base + REG_MRAW_DMA_DBG_SEL);
		dev_info(mraw_dev->dev, "pcrp_debug_data 0x%x",
			readl_relaxed(mraw_dev->base + REG_MRAW_DMA_DBG_PORT));
		writel(0x3000B03, mraw_dev->base + REG_MRAW_DMA_DBG_SEL);
		dev_info(mraw_dev->dev, "ag_rdy sram_fifo_full 0x%x",
			readl_relaxed(mraw_dev->base + REG_MRAW_DMA_DBG_PORT));

		writel(0x3000104, mraw_dev->base + REG_MRAW_DMA_DBG_SEL);
		dev_info(mraw_dev->dev, "state checksum 0x%x",
			readl_relaxed(mraw_dev->base + REG_MRAW_DMA_DBG_PORT));
		writel(0x3000204, mraw_dev->base + REG_MRAW_DMA_DBG_SEL);
		dev_info(mraw_dev->dev, "line_pix_cnt_tmp 0x%x",
			readl_relaxed(mraw_dev->base + REG_MRAW_DMA_DBG_PORT));
		writel(0x3000304, mraw_dev->base + REG_MRAW_DMA_DBG_SEL);
		dev_info(mraw_dev->dev, "line_pix_cnt 0x%x",
			readl_relaxed(mraw_dev->base + REG_MRAW_DMA_DBG_PORT));
		writel(0x3000404, mraw_dev->base + REG_MRAW_DMA_DBG_SEL);
		dev_info(mraw_dev->dev, "important_status 0x%x",
			readl_relaxed(mraw_dev->base + REG_MRAW_DMA_DBG_PORT));
		writel(0x3000504, mraw_dev->base + REG_MRAW_DMA_DBG_SEL);
		dev_info(mraw_dev->dev, "cmd_data_cnt 0x%x",
			readl_relaxed(mraw_dev->base + REG_MRAW_DMA_DBG_PORT));
		writel(0x3000604, mraw_dev->base + REG_MRAW_DMA_DBG_SEL);
		dev_info(mraw_dev->dev, "cmd_cnt_for_bvalid_phase 0x%x",
			readl_relaxed(mraw_dev->base + REG_MRAW_DMA_DBG_PORT));
		writel(0x3000704, mraw_dev->base + REG_MRAW_DMA_DBG_SEL);
		dev_info(mraw_dev->dev, "input_h_cnt 0x%x",
			readl_relaxed(mraw_dev->base + REG_MRAW_DMA_DBG_PORT));
		writel(0x3000804, mraw_dev->base + REG_MRAW_DMA_DBG_SEL);
		dev_info(mraw_dev->dev, "input_v_cnt 0x%x",
			readl_relaxed(mraw_dev->base + REG_MRAW_DMA_DBG_PORT));
		writel(0x3000904, mraw_dev->base + REG_MRAW_DMA_DBG_SEL);
		dev_info(mraw_dev->dev, "xfer_y_cnt 0x%x",
			readl_relaxed(mraw_dev->base + REG_MRAW_DMA_DBG_PORT));
		writel(0x3000A04, mraw_dev->base + REG_MRAW_DMA_DBG_SEL);
		dev_info(mraw_dev->dev, "pcrp_debug_data 0x%x",
			readl_relaxed(mraw_dev->base + REG_MRAW_DMA_DBG_PORT));
		writel(0x3000B04, mraw_dev->base + REG_MRAW_DMA_DBG_SEL);
		dev_info(mraw_dev->dev, "ag_rdy sram_fifo_full 0x%x",
			readl_relaxed(mraw_dev->base + REG_MRAW_DMA_DBG_PORT));

		writel(0x3000105, mraw_dev->base + REG_MRAW_DMA_DBG_SEL);
		dev_info(mraw_dev->dev, "state checksum 0x%x",
			readl_relaxed(mraw_dev->base + REG_MRAW_DMA_DBG_PORT));
		writel(0x3000205, mraw_dev->base + REG_MRAW_DMA_DBG_SEL);
		dev_info(mraw_dev->dev, "line_pix_cnt_tmp 0x%x",
			readl_relaxed(mraw_dev->base + REG_MRAW_DMA_DBG_PORT));
		writel(0x3000305, mraw_dev->base + REG_MRAW_DMA_DBG_SEL);
		dev_info(mraw_dev->dev, "line_pix_cnt 0x%x",
			readl_relaxed(mraw_dev->base + REG_MRAW_DMA_DBG_PORT));
		writel(0x3000405, mraw_dev->base + REG_MRAW_DMA_DBG_SEL);
		dev_info(mraw_dev->dev, "important_status 0x%x",
			readl_relaxed(mraw_dev->base + REG_MRAW_DMA_DBG_PORT));
		writel(0x3000505, mraw_dev->base + REG_MRAW_DMA_DBG_SEL);
		dev_info(mraw_dev->dev, "cmd_data_cnt 0x%x",
			readl_relaxed(mraw_dev->base + REG_MRAW_DMA_DBG_PORT));
		writel(0x3000605, mraw_dev->base + REG_MRAW_DMA_DBG_SEL);
		dev_info(mraw_dev->dev, "cmd_cnt_for_bvalid_phase 0x%x",
			readl_relaxed(mraw_dev->base + REG_MRAW_DMA_DBG_PORT));
		writel(0x3000705, mraw_dev->base + REG_MRAW_DMA_DBG_SEL);
		dev_info(mraw_dev->dev, "input_h_cnt 0x%x",
			readl_relaxed(mraw_dev->base + REG_MRAW_DMA_DBG_PORT));
		writel(0x3000805, mraw_dev->base + REG_MRAW_DMA_DBG_SEL);
		dev_info(mraw_dev->dev, "input_v_cnt 0x%x",
			readl_relaxed(mraw_dev->base + REG_MRAW_DMA_DBG_PORT));
		writel(0x3000905, mraw_dev->base + REG_MRAW_DMA_DBG_SEL);
		dev_info(mraw_dev->dev, "xfer_y_cnt 0x%x",
			readl_relaxed(mraw_dev->base + REG_MRAW_DMA_DBG_PORT));
		writel(0x3000A05, mraw_dev->base + REG_MRAW_DMA_DBG_SEL);
		dev_info(mraw_dev->dev, "pcrp_debug_data 0x%x",
			readl_relaxed(mraw_dev->base + REG_MRAW_DMA_DBG_PORT));
		writel(0x3000B05, mraw_dev->base + REG_MRAW_DMA_DBG_SEL);
		dev_info(mraw_dev->dev, "ag_rdy sram_fifo_full 0x%x",
			readl_relaxed(mraw_dev->base + REG_MRAW_DMA_DBG_PORT));
	}

	dev_info_ratelimited(mraw_dev->dev, "sep_ctl:0x%x sep_crop:0x%x sep_vsize:0x%x\n",
		readl_relaxed(mraw_dev->base_inner + REG_MRAW_SEP_CTL),
		readl_relaxed(mraw_dev->base_inner + REG_MRAW_SEP_CROP),
		readl_relaxed(mraw_dev->base_inner + REG_MRAW_SEP_VSIZE));

	dev_info_ratelimited(mraw_dev->dev, "mbn_cfg_0:0x%x mbn_cfg_1:0x%x mbn_cfg_2:0x%x\n",
		readl_relaxed(mraw_dev->base_inner + REG_MRAW_MBN_CFG_0),
		readl_relaxed(mraw_dev->base_inner + REG_MRAW_MBN_CFG_1),
		readl_relaxed(mraw_dev->base_inner + REG_MRAW_MBN_CFG_2));

	dev_info_ratelimited(mraw_dev->dev, "cpi_cfg_0:0x%x cpi_cfg_1:0x%x\n",
		readl_relaxed(mraw_dev->base_inner + REG_MRAW_CPI_CFG_0),
		readl_relaxed(mraw_dev->base_inner + REG_MRAW_CPI_CFG_1));

	dev_info_ratelimited(mraw_dev->dev, "mqe_cfg:0x%x mqe_in_img:0x%x\n",
		readl_relaxed(mraw_dev->base_inner + REG_MRAW_MQE_CFG),
		readl_relaxed(mraw_dev->base_inner + REG_MRAW_MQE_IN_IMG));
}

void mraw_handle_tg_overrun_error(struct mtk_mraw_device *mraw_dev)
{
	int val;

	val = readl_relaxed(mraw_dev->base + REG_MRAW_TG_PATH_CFG);
	val = val | MRAWTG_FULL_SEL;
	writel_relaxed(val, mraw_dev->base + REG_MRAW_TG_PATH_CFG);
	wmb(); /* TBC */
	val = readl_relaxed(mraw_dev->base + REG_MRAW_TG_SEN_MODE);
	val = val | MRAWTG_CMOS_RDY_SEL;
	writel_relaxed(val, mraw_dev->base + REG_MRAW_TG_SEN_MODE);
	wmb(); /* TBC */

	/* check dmao overflow status */
	dev_info_ratelimited(mraw_dev->dev, "dmao_overflow_status:0x%x\n",
		readl_relaxed(mraw_dev->base_inner + REG_MRAW_CTL_INT5_STATUSX));
}

void mraw_handle_error(struct mtk_mraw_device *mraw_dev,
			     struct mtk_camsys_irq_info *data)
{
	struct mtk_cam_ctx *ctx;
	unsigned int ctx_id;
	int err_status = data->e.err_status;
	int frame_idx_inner = data->frame_idx_inner;

	ctx_id = ctx_from_fh_cookie(frame_idx_inner);
	ctx = &mraw_dev->cam->ctxs[ctx_id];

	/* dump error status */
	dev_info_ratelimited(mraw_dev->dev, "error_status:0x%x\n", err_status);

	/* handle tg overrun error */
	if (err_status & MRAWCTL_TG_ERR_ST)
		mraw_handle_tg_overrun_error(mraw_dev);

	/* dump mraw debug data */
	mtk_cam_mraw_debug_dump(mraw_dev);

	/* dump seninf debug data */
	if (ctx && ctx->seninf) {
		ctx->is_sv_mraw_error = 1;
		mraw_dev->mraw_error_count += 1;
		if (mraw_dev->mraw_error_count >= 2 && !ctx->is_seninf_error_trigger)
			ctx->is_seninf_error_trigger = mtk_cam_seninf_dump_current_status(ctx->seninf,
				true);
		else
			ctx->is_seninf_error_trigger = mtk_cam_seninf_dump_current_status(ctx->seninf,
				false);
	}
	dev_info_ratelimited(mraw_dev->dev, "fbc empty or not:%d\n",
		(data->fbc_empty) ? 1 : 0);
}

static irqreturn_t mtk_irq_mraw(int irq, void *data)
{
	struct mtk_mraw_device *mraw_dev = (struct mtk_mraw_device *)data;
	struct device *dev = mraw_dev->dev;
	struct mtk_camsys_irq_info irq_info;
	unsigned int dequeued_imgo_seq_no, dequeued_imgo_seq_no_inner;
	unsigned int irq_status, irq_status2, irq_status3;
	unsigned int irq_status5, irq_status6;
	unsigned int err_status, dma_err_status, sw_enque_err_status;
	unsigned int imgo_overr_status, imgbo_overr_status, cpio_overr_status;
	unsigned int irq_flag = 0;
	bool wake_thread = 0;

	irq_status	= readl_relaxed(mraw_dev->base + REG_MRAW_CTL_INT_STATUS);
	/*
	 * [ISP 7.0/7.1] HW Bug Workaround: read MRAWCTL_INT2_STATUS every irq
	 * Because MRAWCTL_INT2_EN is attach to OTF_OVER_FLOW ENABLE incorrectly
	 */
	irq_status2	= readl_relaxed(mraw_dev->base + REG_MRAW_CTL_INT2_STATUS);
	irq_status3	= readl_relaxed(mraw_dev->base + REG_MRAW_CTL_INT3_STATUS);
	irq_status5 = readl_relaxed(mraw_dev->base + REG_MRAW_CTL_INT5_STATUS);
	irq_status6	= readl_relaxed(mraw_dev->base + REG_MRAW_CTL_INT6_STATUS);
	dequeued_imgo_seq_no =
		readl_relaxed(mraw_dev->base + REG_MRAW_FRAME_SEQ_NUM);
	dequeued_imgo_seq_no_inner =
		readl_relaxed(mraw_dev->base_inner + REG_MRAW_FRAME_SEQ_NUM);

	err_status = irq_status & INT_ST_MASK_MRAW_ERR;
	dma_err_status = irq_status & MRAWCTL_DMA_ERR_ST;
	sw_enque_err_status = irq_status & MRAWCTL_SW_ENQUE_ERR_ST;
	imgo_overr_status = irq_status5 & MRAWCTL_IMGO_M1_OTF_OVERFLOW_ST;
	imgbo_overr_status = irq_status5 & MRAWCTL_IMGBO_M1_OTF_OVERFLOW_ST;
	cpio_overr_status = irq_status5 & MRAWCTL_CPIO_M1_OTF_OVERFLOW_ST;
	if (CAM_DEBUG_ENABLED(RAW_INT))
		dev_info(dev,
		"%i status:0x%x_%x(err:0x%x)/0x%x dma_err:0x%x seq_num:%d/%d\n",
		mraw_dev->id, irq_status, irq_status2, err_status, irq_status6, dma_err_status,
		dequeued_imgo_seq_no_inner, dequeued_imgo_seq_no);

	trace_mraw_irq(mraw_dev->dev,
		       irq_status, irq_status2, err_status, irq_status6,
		       dma_err_status,
		       dequeued_imgo_seq_no_inner, dequeued_imgo_seq_no);

	dev_dbg(dev,
		"%i sw_enque_err:%d dma_overr:0x%x_0x%x_0x%x dma_addr:0x%x%x_0x%x%x_0x%x%x\n",
		mraw_dev->id, sw_enque_err_status,
		imgo_overr_status, imgbo_overr_status, cpio_overr_status,
		readl_relaxed(mraw_dev->base_inner + REG_MRAW_IMGO_BASE_ADDR_MSB),
		readl_relaxed(mraw_dev->base_inner + REG_MRAW_IMGO_BASE_ADDR),
		readl_relaxed(mraw_dev->base_inner + REG_MRAW_IMGBO_BASE_ADDR_MSB),
		readl_relaxed(mraw_dev->base_inner + REG_MRAW_IMGBO_BASE_ADDR),
		readl_relaxed(mraw_dev->base_inner + REG_MRAW_CPIO_BASE_ADDR_MSB),
		readl_relaxed(mraw_dev->base_inner + REG_MRAW_CPIO_BASE_ADDR));

	/*
	 * In normal case, the next SOF ISR should come after HW PASS1 DONE ISR.
	 * If these two ISRs come together, print warning msg to hint.
	 */
	irq_info.irq_type = 0;
	irq_info.ts_ns = ktime_get_boottime_ns();
	irq_info.frame_idx = dequeued_imgo_seq_no;
	irq_info.frame_idx_inner = dequeued_imgo_seq_no_inner;
	irq_info.fbc_empty = 0;

	if ((irq_status & MRAWCTL_SOF_INT_ST) &&
		(irq_status & MRAWCTL_PASS1_DONE_ST))
		dev_dbg(dev, "sof_done block cnt:%d\n", mraw_dev->sof_count);

	/* Frame done */
	if (irq_status & MRAWCTL_SW_PASS1_DONE_ST) {
		irq_info.irq_type |= (1 << CAMSYS_IRQ_FRAME_DONE);
		dev_dbg(dev, "p1_done sof_cnt:%d timer_ctl:0x%x timer_inc:0x%x ddr_gen_plus_cnt0x%x ddren_ctl0x%x ddren_st0x%x\n",
			mraw_dev->sof_count,
			readl_relaxed(mraw_dev->base + REG_MRAW_TG_HW_TIMER_CTL),
			readl_relaxed(mraw_dev->base + REG_MRAW_TG_HW_TIMER_INC_PERIOD),
			readl_relaxed(mraw_dev->base + REG_MRAW_TG_HW_DDR_GEN_PLUS_CNT),
			readl_relaxed(mraw_dev->base + REG_MRAW_CTL_DDREN_CTL),
			readl_relaxed(mraw_dev->base + REG_MRAW_CTL_DDREN_ST));
		mraw_dev->mraw_error_count = 0;
	}
	/* Frame start */
	if (irq_status & MRAWCTL_SOF_INT_ST) {
		irq_info.irq_type |= (1 << CAMSYS_IRQ_FRAME_START);
		mraw_dev->sof_count++;
		dev_dbg(dev, "sof cnt:%d\n", mraw_dev->sof_count);
		if (debug_ddren_mraw_hw_mode && atomic_read(&mraw_dev->is_sw_clr) < 2) {
			atomic_inc(&mraw_dev->is_sw_clr);
			if (atomic_read(&mraw_dev->is_sw_clr) == 2) {
				MRAW_WRITE_BITS(mraw_dev->base + REG_MRAW_CTL_DDREN_CTL,
					MRAW_CTL_DDREN_CTL, MRAWCTL_DDREN_SW_CLR, 1);
				MRAW_WRITE_BITS(mraw_dev->base + REG_MRAW_CTL_BW_QOS_CTL,
					MRAW_CTL_BW_QOS_CTL, MRAWCTL_BW_QOS_SW_CLR, 1);
				dev_dbg(dev,"mraw do swclr");
			}
		}
		irq_info.fbc_empty = (sw_enque_err_status) ? 1 : 0;
		engine_handle_sof(&mraw_dev->cq_ref,
				  bit_map_bit(MAP_HW_MRAW, mraw_dev->id),
				  irq_info.frame_idx_inner);
	}

	/* CQ done */
	if (irq_status6 & MRAWCTL_CQ_SUB_THR0_DONE_ST) {
		if (mraw_dev->cq_ref != NULL) {
			long mask = bit_map_bit(MAP_HW_MRAW, mraw_dev->id);

			if (engine_handle_cq_done(&mraw_dev->cq_ref, mask))
				irq_info.irq_type |= 1 << CAMSYS_IRQ_SETTING_DONE;
		}
		dev_dbg(dev, "CQ done:%d\n", mraw_dev->sof_count);
	}
	irq_flag = irq_info.irq_type;
	if (irq_flag && push_msgfifo(mraw_dev, &irq_info) == 0)
		wake_thread = 1;

	/* Check ISP error status */
	if (err_status) {
		struct mtk_camsys_irq_info err_info;

		err_info.irq_type = 1 << CAMSYS_IRQ_ERROR;
		err_info.ts_ns = irq_info.ts_ns;
		err_info.frame_idx = irq_info.frame_idx;
		err_info.frame_idx_inner = irq_info.frame_idx_inner;
		err_info.e.err_status = err_status;

		if (push_msgfifo(mraw_dev, &err_info) == 0)
			wake_thread = 1;
	}

	return wake_thread ? IRQ_WAKE_THREAD : IRQ_HANDLED;
}

static irqreturn_t mtk_thread_irq_mraw(int irq, void *data)
{
	struct mtk_mraw_device *mraw_dev = (struct mtk_mraw_device *)data;
	struct mtk_camsys_irq_info irq_info;
	int recovered_done;
	int do_recover;

	if (unlikely(atomic_cmpxchg(&mraw_dev->is_fifo_overflow, 1, 0)))
		dev_info(mraw_dev->dev, "msg fifo overflow\n");

	while (kfifo_len(&mraw_dev->msg_fifo) >= sizeof(irq_info)) {
		int len = kfifo_out(&mraw_dev->msg_fifo, &irq_info, sizeof(irq_info));

		WARN_ON(len != sizeof(irq_info));
		if (CAM_DEBUG_ENABLED(CTRL))
			dev_info(mraw_dev->dev, "ts=%llu irq_type %d, req:0x%x/0x%x, tg_cnt:%d\n",
					irq_info.ts_ns / 1000,
					irq_info.irq_type,
					irq_info.frame_idx_inner,
					irq_info.frame_idx,
					irq_info.tg_cnt);

		/* error case */
		if (unlikely(irq_info.irq_type == (1 << CAMSYS_IRQ_ERROR))) {
			mraw_handle_error(mraw_dev, &irq_info);
			continue;
		}

		/* normal case */
		do_recover = mraw_process_fsm(mraw_dev, &irq_info,
					      &recovered_done);

		/* inform interrupt information to camsys controller */
		mtk_cam_ctrl_isr_event(mraw_dev->cam,
				       CAMSYS_ENGINE_MRAW, mraw_dev->id,
				       &irq_info);

		if (do_recover) {
			irq_info.irq_type = BIT(CAMSYS_IRQ_FRAME_DONE);
			irq_info.cookie_done = recovered_done;

			mtk_cam_ctrl_isr_event(mraw_dev->cam,
					       CAMSYS_ENGINE_MRAW, mraw_dev->id,
					       &irq_info);
		}
	}

	return IRQ_HANDLED;
}

static int mtk_mraw_pm_suspend(struct device *dev)
{
	struct mtk_mraw_device *mraw_dev = dev_get_drvdata(dev);
	u32 val;
	int ret;

	dev_info_ratelimited(dev, "- %s\n", __func__);

	if (pm_runtime_suspended(dev))
		return 0;

	/* Disable ISP's view finder and wait for TG idle */
	dev_info_ratelimited(dev, "mraw suspend, disable VF\n");
	val = readl(mraw_dev->base + REG_MRAW_TG_VF_CON);
	writel(val & (~MRAWTG_VFDATA_EN),
		mraw_dev->base + REG_MRAW_TG_VF_CON);
	ret = readl_poll_timeout_atomic(
					mraw_dev->base + REG_MRAW_TG_INTER_ST, val,
					(val & MRAWTG_CS_MASK) == MRAWTG_IDLE_ST,
					USEC_PER_MSEC, MTK_MRAW_STOP_HW_TIMEOUT);
	if (ret)
		dev_dbg(dev, "can't stop HW:%d:0x%x\n", ret, val);

	/* Disable CMOS */
	val = readl(mraw_dev->base + REG_MRAW_TG_SEN_MODE);
	writel(val & (~MRAWTG_CMOS_EN),
		mraw_dev->base + REG_MRAW_TG_SEN_MODE);

	/* Force ISP HW to idle */
	ret = pm_runtime_put_sync(dev);
	return ret;
}

static int mtk_mraw_pm_resume(struct device *dev)
{
	struct mtk_mraw_device *mraw_dev = dev_get_drvdata(dev);
	u32 val;
	int ret;

	dev_info_ratelimited(dev, "- %s\n", __func__);

	if (pm_runtime_suspended(dev))
		return 0;

	/* Force ISP HW to resume */
	ret = pm_runtime_get_sync(dev);
	if (ret)
		return ret;

	/* Enable CMOS */
	dev_info_ratelimited(dev, "mraw resume, enable CMOS/VF\n");
	val = readl(mraw_dev->base + REG_MRAW_TG_SEN_MODE);
	writel(val | MRAWTG_CMOS_EN,
		mraw_dev->base + REG_MRAW_TG_SEN_MODE);

	/* Enable VF */
	val = readl(mraw_dev->base + REG_MRAW_TG_VF_CON);
	writel(val | MRAWTG_VFDATA_EN,
		mraw_dev->base + REG_MRAW_TG_VF_CON);

	return 0;
}

static int mtk_mraw_suspend_pm_event(struct notifier_block *notifier,
			unsigned long pm_event, void *unused)
{
	struct mtk_mraw_device *mraw_dev =
		container_of(notifier, struct mtk_mraw_device, notifier_blk);
	struct device *dev = mraw_dev->dev;

	switch (pm_event) {
	case PM_HIBERNATION_PREPARE:
		return NOTIFY_DONE;
	case PM_RESTORE_PREPARE:
		return NOTIFY_DONE;
	case PM_POST_HIBERNATION:
		return NOTIFY_DONE;
	case PM_SUSPEND_PREPARE: /* before enter suspend */
		mtk_mraw_pm_suspend(dev);
		return NOTIFY_DONE;
	case PM_POST_SUSPEND: /* after resume */
		mtk_mraw_pm_resume(dev);
		return NOTIFY_DONE;
	}
	return NOTIFY_OK;
}

static int mtk_mraw_of_probe(struct platform_device *pdev,
			    struct mtk_mraw_device *mraw_dev)
{
	struct device *dev = &pdev->dev;
	struct platform_device *larb_pdev;
	struct device_node *larb_node;
	struct of_phandle_args args;
	struct device_link *link;
	struct resource *res;
	unsigned int i;
	int ret, num_clks, num_larbs, num_ports, smmus;
	int camsys_mraw_baseaddr = 0;

	ret = of_property_read_u32(dev->of_node, "mediatek,mraw-id",
						       &mraw_dev->id);
	if (ret) {
		dev_dbg(dev, "missing camid property\n");
		return ret;
	}

	ret = of_property_read_u32(dev->of_node, "mediatek,cammux-id",
						       &mraw_dev->cammux_id);
	if (ret) {
		dev_dbg(dev, "missing cammux id property\n");
		return ret;
	}

	/* base outer register */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "base");
	if (!res) {
		dev_info(dev, "failed to get mem\n");
		return -ENODEV;
	}

	mraw_dev->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(mraw_dev->base)) {
		dev_dbg(dev, "failed to map register base\n");
		return PTR_ERR(mraw_dev->base);
	}
	dev_dbg(dev, "mraw, map_addr=0x%pK\n", mraw_dev->base);

	/* base inner register */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "inner_base");
	if (!res) {
		dev_dbg(dev, "failed to get mem\n");
		return -ENODEV;
	}

	mraw_dev->base_inner = devm_ioremap_resource(dev, res);
	if (IS_ERR(mraw_dev->base_inner)) {
		dev_dbg(dev, "failed to map register inner base\n");
		return PTR_ERR(mraw_dev->base_inner);
	}
	dev_dbg(dev, "mraw, map_addr(inner)=0x%pK\n", mraw_dev->base_inner);

	CALL_PLAT_HW(query_module_base, CAMSYS_MRAW, &camsys_mraw_baseaddr);
	mraw_dev->top = ioremap(camsys_mraw_baseaddr, 0x1000);

	mraw_dev->irq = platform_get_irq(pdev, 0);
	if (!mraw_dev->irq) {
		dev_dbg(dev, "failed to get irq\n");
		return -ENODEV;
	}

	ret = devm_request_threaded_irq(dev, mraw_dev->irq,
				mtk_irq_mraw, mtk_thread_irq_mraw,
				0, dev_name(dev), mraw_dev);
	if (ret) {
		dev_dbg(dev, "failed to request irq=%d\n", mraw_dev->irq);
		return ret;
	}
	dev_dbg(dev, "registered irq=%d\n", mraw_dev->irq);
	disable_irq(mraw_dev->irq);

	num_clks = of_count_phandle_with_args(pdev->dev.of_node, "clocks",
			"#clock-cells");
	mraw_dev->num_clks = (num_clks < 0) ? 0 : num_clks;
	dev_info(dev, "clk_num:%d\n", mraw_dev->num_clks);
	if (mraw_dev->num_clks) {
		mraw_dev->clks = devm_kcalloc(dev, mraw_dev->num_clks, sizeof(*mraw_dev->clks),
					 GFP_KERNEL);
		if (!mraw_dev->clks)
			return -ENOMEM;
	}

	for (i = 0; i < mraw_dev->num_clks; i++) {
		mraw_dev->clks[i] = of_clk_get(pdev->dev.of_node, i);
		if (IS_ERR(mraw_dev->clks[i])) {
			dev_info(dev, "failed to get clk %d\n", i);
			return -ENODEV;
		}
	}

	num_larbs = of_count_phandle_with_args(
					pdev->dev.of_node, "mediatek,larbs", NULL);
	num_larbs = (num_larbs < 0) ? 0 : num_larbs;
	dev_info(dev, "larb_num:%d\n", num_larbs);

	for (i = 0; i < num_larbs; i++) {
		larb_node = of_parse_phandle(
					pdev->dev.of_node, "mediatek,larbs", i);
		if (!larb_node) {
			dev_info(dev, "failed to get larb node\n");
			continue;
		}

		larb_pdev = of_find_device_by_node(larb_node);
		if (WARN_ON(!larb_pdev)) {
			of_node_put(larb_node);
			dev_info(dev, "failed to get larb pdev\n");
			continue;
		}
		of_node_put(larb_node);

		link = device_link_add(&pdev->dev, &larb_pdev->dev,
						DL_FLAG_PM_RUNTIME | DL_FLAG_STATELESS);
		if (!link)
			dev_info(dev, "unable to link smi larb%d\n", i);
	}

	num_ports = of_count_phandle_with_args(
					pdev->dev.of_node, "iommus", "#iommu-cells");
	num_ports = (num_ports < 0) ? 0 : num_ports;
	dev_info(dev, "port_num:%d\n", num_ports);

	for (i = 0; i < num_ports; i++) {
		if (!of_parse_phandle_with_args(pdev->dev.of_node,
			"iommus", "#iommu-cells", i, &args)) {
			mtk_iommu_register_fault_callback(
				args.args[0],
				mtk_mraw_translation_fault_callback,
				(void *)mraw_dev, false);
		}
	}

	smmus = of_property_count_u32_elems(
		pdev->dev.of_node, "mediatek,smmu-dma-axid");
	smmus = (smmus > 0) ? smmus : 0;
	dev_info(dev, "smmu_num:%d\n", smmus);
	for (i = 0; i < smmus; i++) {
		u32 axid;

		if (!of_property_read_u32_index(
			pdev->dev.of_node, "mediatek,smmu-dma-axid", i, &axid)) {
			mtk_iommu_register_fault_callback(
				axid, mtk_mraw_translation_fault_callback,
				(void *)mraw_dev, false);
		}
	}
#ifdef CONFIG_PM_SLEEP
	mraw_dev->notifier_blk.notifier_call = mtk_mraw_suspend_pm_event;
	mraw_dev->notifier_blk.priority = 0;
	ret = register_pm_notifier(&mraw_dev->notifier_blk);
	if (ret) {
		dev_info(dev, "Failed to register PM notifier");
		return -ENODEV;
	}
#endif
	return 0;
}

static int mtk_mraw_component_bind(
	struct device *dev,
	struct device *master,
	void *data)
{
	struct mtk_mraw_device *mraw_dev = dev_get_drvdata(dev);
	struct mtk_cam_device *cam_dev = data;

	mraw_dev->cam = cam_dev;
	return mtk_cam_set_dev_mraw(cam_dev->dev, mraw_dev->id, dev);
}

static void mtk_mraw_component_unbind(
	struct device *dev,
	struct device *master,
	void *data)
{
}


static const struct component_ops mtk_mraw_component_ops = {
	.bind = mtk_mraw_component_bind,
	.unbind = mtk_mraw_component_unbind,
};

static int mtk_mraw_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mtk_mraw_device *mraw_dev;
	int ret;

	mraw_dev = devm_kzalloc(dev, sizeof(*mraw_dev), GFP_KERNEL);
	if (!mraw_dev)
		return -ENOMEM;

	mraw_dev->dev = dev;
	dev_set_drvdata(dev, mraw_dev);

	ret = mtk_mraw_of_probe(pdev, mraw_dev);
	if (ret)
		return ret;

	ret = mtk_cam_qos_probe(dev, &mraw_dev->qos, SMI_PORT_MRAW_NUM);
	if (ret)
		goto UNREGISTER_PM_NOTIFIER;

	mraw_dev->fifo_size =
		roundup_pow_of_two(8 * sizeof(struct mtk_camsys_irq_info));
	mraw_dev->msg_buffer = devm_kzalloc(dev, mraw_dev->fifo_size, GFP_KERNEL);
	if (!mraw_dev->msg_buffer) {
		ret = -ENOMEM;
		goto UNREGISTER_PM_NOTIFIER;
	}

	pm_runtime_enable(dev);

	ret = component_add(dev, &mtk_mraw_component_ops);
	if (ret)
		goto UNREGISTER_PM_NOTIFIER;

	return ret;

UNREGISTER_PM_NOTIFIER:
	unregister_pm_notifier(&mraw_dev->notifier_blk);
	return ret;
}

static int mtk_mraw_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mtk_mraw_device *mraw_dev = dev_get_drvdata(dev);

#ifdef CONFIG_PM_SLEEP
	unregister_pm_notifier(&mraw_dev->notifier_blk);
#endif
	pm_runtime_disable(dev);

	mtk_cam_qos_remove(&mraw_dev->qos);

	component_del(dev, &mtk_mraw_component_ops);
	return 0;
}

int mtk_mraw_runtime_suspend(struct device *dev)
{
	struct mtk_mraw_device *mraw_dev = dev_get_drvdata(dev);
	int i;

	dev_info_ratelimited(dev, "%s:disable clock\n", __func__);

	mtk_cam_reset_qos(dev, &mraw_dev->qos);

	mtk_cam_bwr_set_chn_bw(mraw_dev->cam->bwr,
		ENGINE_MRAW, get_mraw_axi_port(mraw_dev->id),
		0, -mraw_dev->mraw_avg_applied_bw_w,
		0, -mraw_dev->mraw_peak_applied_bw_w, false);
	mtk_cam_bwr_set_ttl_bw(mraw_dev->cam->bwr,
		ENGINE_MRAW, -mraw_dev->mraw_avg_applied_bw_w,
		-mraw_dev->mraw_peak_applied_bw_w, false);

	mraw_dev->mraw_avg_applied_bw_w = 0;
	mraw_dev->mraw_peak_applied_bw_w = 0;

	for (i = mraw_dev->num_clks - 1; i >= 0; i--)
		clk_disable_unprepare(mraw_dev->clks[i]);

	return 0;
}

int mtk_mraw_runtime_resume(struct device *dev)
{
	struct mtk_mraw_device *mraw_dev = dev_get_drvdata(dev);
	int i, ret;

	/* reset_msgfifo before enable_irq */
	ret = mtk_cam_mraw_reset_msgfifo(mraw_dev);
	if (ret)
		return ret;

	dev_info_ratelimited(dev, "%s:enable clock\n", __func__);
	for (i = 0; i < mraw_dev->num_clks; i++) {
		ret = clk_prepare_enable(mraw_dev->clks[i]);
		if (ret) {
			dev_info(dev, "enable failed at clk #%d, ret = %d\n",
				 i, ret);
			i--;
			while (i >= 0)
				clk_disable_unprepare(mraw_dev->clks[i--]);

			return ret;
		}
	}
	mraw_reset_by_mraw_top(mraw_dev);

	enable_irq(mraw_dev->irq);
	dev_dbg(dev, "%s:enable irq\n", __func__);

	return 0;
}

static const struct dev_pm_ops mtk_mraw_pm_ops = {
	SET_RUNTIME_PM_OPS(mtk_mraw_runtime_suspend, mtk_mraw_runtime_resume,
			   NULL)
};

struct platform_driver mtk_cam_mraw_driver = {
	.probe   = mtk_mraw_probe,
	.remove  = mtk_mraw_remove,
	.driver  = {
		.name  = "mtk-cam mraw",
		.of_match_table = of_match_ptr(mtk_mraw_of_ids),
		.pm     = &mtk_mraw_pm_ops,
	}
};

