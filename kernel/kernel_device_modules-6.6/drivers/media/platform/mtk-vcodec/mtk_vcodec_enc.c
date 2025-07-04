// SPDX-License-Identifier: GPL-2.0
/*
* Copyright (c) 2016 MediaTek Inc.
* Author: PC Chen <pc.chen@mediatek.com>
*         Tiffany Lin <tiffany.lin@mediatek.com>
*/

#include <media/v4l2-event.h>
#include <media/v4l2-mem2mem.h>
#include <soc/mediatek/smi.h>
#include <linux/delay.h>
#include <linux/semaphore.h>
#include <linux/module.h>
#include <mtk_heap.h>

#include "mtk_heap.h"
#include "iommu_pseudo.h"

#include "mtk_vcodec_drv.h"
#include "mtk_vcodec_enc.h"
#include "mtk_vcodec_intr.h"
#include "mtk_vcodec_util.h"
#include "mtk_vcodec_enc_pm.h"
#include "mtk_vcodec_enc_pm_plat.h"
#include "venc_drv_if.h"
#include "mtk-smmu-v3.h"
#if IS_ENABLED(CONFIG_MTK_TINYSYS_VCP_SUPPORT)
#include "vcp_status.h"
#endif

#define MTK_VENC_MIN_W  160U
#define MTK_VENC_MIN_H  128U
#define MTK_VENC_MAX_W  1920U
#define MTK_VENC_MAX_H  1088U
#define DFT_CFG_WIDTH   MTK_VENC_MIN_W
#define DFT_CFG_HEIGHT  MTK_VENC_MIN_H

static void mtk_venc_worker(struct work_struct *work);
struct mtk_video_fmt
	mtk_venc_formats[MTK_MAX_ENC_CODECS_SUPPORT] = { {0} };
struct mtk_codec_framesizes
	mtk_venc_framesizes[MTK_MAX_ENC_CODECS_SUPPORT] = { {0} };
struct mtk_codec_capability
	mtk_venc_cap_common = {0};

static unsigned int default_out_fmt_idx;
static unsigned int default_cap_fmt_idx;
static struct vb2_mem_ops venc_dma_contig_memops;

#if (!(IS_ENABLED(CONFIG_DEVICE_MODULES_ARM_SMMU_V3)))
static struct vb2_mem_ops venc_sec_dma_contig_memops;

static int mtk_venc_sec_dc_map_dmabuf(void *mem_priv)
{
	struct vb2_dc_buf *buf = mem_priv;

	if (WARN_ON(!buf->db_attach)) {
		mtk_v4l2_err("trying to pin a non attached buffer\n");
		return -EINVAL;
	}

	if (WARN_ON(buf->dma_addr)) {
		mtk_v4l2_err("dmabuf buffer is already pinned\n");
		return 0;
	}

	buf->dma_addr = dmabuf_to_secure_handle(buf->db_attach->dmabuf);
	buf->dma_sgt = NULL;
	buf->vaddr = NULL;

	mtk_v4l2_debug(4, "%s: secure_handle=%pad", __func__, &buf->dma_addr);
	return 0;
}

static void mtk_venc_sec_dc_unmap_dmabuf(void *mem_priv)
{
	struct vb2_dc_buf *buf = mem_priv;

	if (WARN_ON(!buf->db_attach)) {
		mtk_v4l2_err("trying to unpin a not attached buffer\n");
		return;
	}

	if (WARN_ON(!buf->dma_addr)) {
		mtk_v4l2_err("dmabuf buffer is already unpinned\n");
		return;
	}

	if (buf->vaddr) {
		mtk_v4l2_err("dmabuf buffer vaddr not null\n");
		dma_buf_vunmap(buf->db_attach->dmabuf, buf->vaddr);
		buf->vaddr = NULL;
	}

	mtk_v4l2_debug(4, "%s:  secure_handle=%pad", __func__, &buf->dma_addr);
	buf->dma_addr = 0;
	buf->dma_sgt = NULL;
}
#endif

static bool mtk_venc_is_vcu(void)
{
	if (VCU_FPTR(vcu_get_plat_device)) {
		if (mtk_vcodec_is_vcp(MTK_INST_ENCODER))
			return false;
		else
			return true;
	}
	return false;
}

inline unsigned int log2_enc(__u32 value)
{
	unsigned int x = 0;

	while (value > 1) {
		value >>= 1;
		x++;
	}
	return x;
}

void mtk_venc_do_gettimeofday(struct timespec64 *tv)
{
	struct timespec64 now;

	ktime_get_real_ts64(&now);
	tv->tv_sec = now.tv_sec;
	tv->tv_nsec = now.tv_nsec; // micro sec = ((long)(now.tv_nsec)/1000);
}

#if IS_ENABLED(CONFIG_MTK_TINYSYS_VCP_SUPPORT)
static void set_venc_vcp_data(struct mtk_vcodec_ctx *ctx, enum vcp_reserve_mem_id_t id)
{
	struct venc_enc_param enc_prm;

	memset(&enc_prm, 0, sizeof(enc_prm));
	enc_prm.set_vcp_buf = kzalloc(LOG_PROPERTY_SIZE, GFP_KERNEL);
	if (!enc_prm.set_vcp_buf)
		return;

	if (id == VENC_SET_PROP_MEM_ID) {

		SNPRINTF(enc_prm.set_vcp_buf, LOG_PROPERTY_SIZE, "%s", mtk_venc_property);
		mtk_v4l2_debug(3, "[%d] mtk_venc_property %s", ctx->id, enc_prm.set_vcp_buf);
		mtk_v4l2_debug(3, "[%d] mtk_venc_property_prev %s",
					ctx->id, mtk_venc_property_prev);

		// set vcp log every time
		if (/* strcmp(mtk_venc_property_prev, enc_prm.set_vcp_buf) != 0 && */
			strlen(enc_prm.set_vcp_buf) > 0) {

			if (venc_if_set_param(ctx,
				VENC_SET_PARAM_PROPERTY,
				&enc_prm) != 0) {
				mtk_v4l2_err("Error!! Cannot set venc property");
				kfree(enc_prm.set_vcp_buf);
				return;
			}
			SNPRINTF(mtk_venc_property_prev, LOG_PROPERTY_SIZE,
				"%s", enc_prm.set_vcp_buf);
		}
	} else if (id == VENC_VCP_LOG_INFO_ID) {

		SNPRINTF(enc_prm.set_vcp_buf, LOG_PROPERTY_SIZE, "%s", mtk_venc_vcp_log);
		mtk_v4l2_debug(3, "[%d] mtk_venc_vcp_log %s", ctx->id, enc_prm.set_vcp_buf);
		mtk_v4l2_debug(3, "[%d] mtk_venc_vcp_log_prev %s", ctx->id, mtk_venc_vcp_log_prev);

		// set vcp log every time
		if (/* strcmp(mtk_venc_vcp_log_prev, enc_prm.set_vcp_buf) != 0 && */
			strlen(enc_prm.set_vcp_buf) > 0) {

			if (venc_if_set_param(ctx,
				VENC_SET_PARAM_VCP_LOG_INFO,
				&enc_prm) != 0) {
				mtk_v4l2_err("Error!! Cannot set venc vcp log info");
				kfree(enc_prm.set_vcp_buf);
				return;
			}
			SNPRINTF(mtk_venc_vcp_log_prev, LOG_PROPERTY_SIZE,
				"%s", enc_prm.set_vcp_buf);
		}
	}

	kfree(enc_prm.set_vcp_buf);
}
#endif

static void set_vcu_vpud_log(struct mtk_vcodec_ctx *ctx, void *in)
{
	struct venc_enc_param *enc_prm = NULL;

	if (!mtk_venc_is_vcu()) {
		mtk_v4l2_err("only support on vcu enc path");
		return;
	}

	enc_prm = kzalloc(sizeof(*enc_prm), GFP_KERNEL);
	if (!enc_prm)
		return;

	enc_prm->log = (char *)in;
	venc_if_set_param(ctx, VENC_SET_PARAM_VCU_VPUD_LOG, enc_prm);
	kfree(enc_prm);
}

static void get_vcu_vpud_log(struct mtk_vcodec_ctx *ctx, void *out)
{
	if (!mtk_venc_is_vcu()) {
		mtk_v4l2_err("only support on vcu dec path");
		return;
	}

	venc_if_get_param(ctx, GET_PARAM_VENC_VCU_VPUD_LOG, out);
}

static void get_supported_format(struct mtk_vcodec_ctx *ctx)
{
	unsigned int i;

	if (mtk_venc_formats[0].fourcc == 0) {
		if (venc_if_get_param(ctx,
			GET_PARAM_VENC_CAP_SUPPORTED_FORMATS,
			&mtk_venc_formats) != 0) {
			mtk_v4l2_err("Error!! Cannot get supported format");
			return;
		}
		for (i = 0; i < MTK_MAX_ENC_CODECS_SUPPORT; i++) {
			if (mtk_venc_formats[i].fourcc != 0 &&
			    mtk_venc_formats[i].type == MTK_FMT_FRAME) {
				default_out_fmt_idx = i;
				break;
			}
		}
		for (i = 0; i < MTK_MAX_ENC_CODECS_SUPPORT; i++) {
			if (mtk_venc_formats[i].fourcc != 0 &&
			    mtk_venc_formats[i].type == MTK_FMT_ENC) {
				default_cap_fmt_idx = i;
				break;
			}
		}
	}
}

static void get_supported_framesizes(struct mtk_vcodec_ctx *ctx)
{
	unsigned int i;

	if (mtk_venc_framesizes[0].fourcc == 0) {
		if (venc_if_get_param(ctx, GET_PARAM_VENC_CAP_FRAME_SIZES,
				      &mtk_venc_framesizes) != 0) {
			mtk_v4l2_err("[%d] Error!! Cannot get frame size",
				ctx->id);
			return;
		}

		for (i = 0; i < MTK_MAX_ENC_CODECS_SUPPORT; i++) {
			if (mtk_venc_framesizes[i].fourcc != 0) {
				mtk_v4l2_debug(1, "venc_fs[%d] fourcc %s(0x%x) s %d %d %d %d %d %d\n",
					i, FOURCC_STR(mtk_venc_framesizes[i].fourcc), mtk_venc_framesizes[i].fourcc,
					mtk_venc_framesizes[i].stepwise.min_width,
					mtk_venc_framesizes[i].stepwise.max_width,
					mtk_venc_framesizes[i].stepwise.step_width,
					mtk_venc_framesizes[i].stepwise.min_height,
					mtk_venc_framesizes[i].stepwise.max_height,
					mtk_venc_framesizes[i].stepwise.step_height);
			}
		}
	}
}

static void get_supported_cap_common(struct mtk_vcodec_ctx *ctx)
{
	if (venc_if_get_param(ctx, GET_PARAM_VENC_CAP_COMMON,
					&mtk_venc_cap_common) != 0) {
		mtk_v4l2_err("[%d] Error!! Cannot get cap common",
			ctx->id);
		return;
	}

	mtk_v4l2_debug(1, "venc_cap_common %d %d\n", mtk_venc_cap_common.max_b,
		mtk_venc_cap_common.max_temporal_layer);
}

static void get_free_buffers(struct mtk_vcodec_ctx *ctx,
				struct venc_done_result *pResult)
{
	venc_if_get_param(ctx,
		GET_PARAM_FREE_BUFFERS,
		pResult);
}

#if IS_ENABLED(CONFIG_MTK_TINYSYS_VCP_SUPPORT)
void mtk_venc_trigger_vcp_halt(struct venc_inst *inst)
{
	unsigned long timeout = 0;

	if (inst->vcu_inst.daemon_pid == vcp_cmd_ex(VENC_FEATURE_ID, VCP_GET_GEN, "venc_vcp_ee")) {
		vcp_cmd_ex(VENC_FEATURE_ID, VCP_SET_HALT, "venc_vcp_ee");

		while (inst->vcu_inst.daemon_pid == vcp_cmd_ex(VENC_FEATURE_ID, VCP_GET_GEN, "venc_vcp_ee")) {
			if (timeout > VCP_SYNC_TIMEOUT_MS) {
				mtk_v4l2_debug(0, "halt restart timeout %x\n", inst->vcu_inst.daemon_pid);
				break;
			}
			usleep_range(10000, 20000);
			timeout += 10;
		}
	}
}
#endif

int isVencAfbc10BFormat(enum venc_yuv_fmt format)
{
	switch (format) {
	case VENC_YUV_FORMAT_NV12_10B_AFBC:
		return 1;
	default:
		return 0;
	}
}

int isVencAfbcFormat(enum venc_yuv_fmt format)
{
	switch (format) {
	case VENC_YUV_FORMAT_32bitRGBA8888_AFBC:
	case VENC_YUV_FORMAT_32bitBGRA8888_AFBC:
	case VENC_YUV_FORMAT_32bitRGBA1010102_AFBC:
	case VENC_YUV_FORMAT_32bitBGRA1010102_AFBC:
	case VENC_YUV_FORMAT_NV12_AFBC:
	case VENC_YUV_FORMAT_NV12_10B_AFBC:
	case VENC_YUV_FORMAT_NV21_AFBC:
		return 1;
	default:
		return 0;
	}
}

int isVencAfbcRgbFormat(enum venc_yuv_fmt format)
{
	switch (format) {
	case VENC_YUV_FORMAT_32bitRGBA8888_AFBC:
	case VENC_YUV_FORMAT_32bitBGRA8888_AFBC:
	case VENC_YUV_FORMAT_32bitRGBA1010102_AFBC:
	case VENC_YUV_FORMAT_32bitBGRA1010102_AFBC:
		return 1;
	default:
		return 0;
	}
}

void venc_dump_data_section(char *pbuf, unsigned int dump_size)
{
	char debug_fb[256] = {0};
	char *pdebug_fb = debug_fb;
	unsigned int i;

	for (i = 0; i < dump_size; i++) {
		pdebug_fb += sprintf(pdebug_fb, "%02x ", pbuf[i]);
		if ((((i + 1) % 16) == 0) || (i == (dump_size - 1))) {
			mtk_v4l2_debug(0, "%s", debug_fb);
			memset(debug_fb, 0, sizeof(debug_fb));
			pdebug_fb = debug_fb;
		}
	}
}

void mtk_enc_put_buf(struct mtk_vcodec_ctx *ctx)
{
	struct venc_done_result rResult;
	struct venc_frm_buf *pfrm;
	struct mtk_vcodec_mem *pbs;
	struct mtk_video_enc_buf *bs_info, *frm_info;
	struct vb2_v4l2_buffer *dst_vb2_v4l2, *src_vb2_v4l2;
	struct vb2_buffer *dst_buf;
	struct venc_inst *inst = (struct venc_inst *)(ctx->drv_handle);
	unsigned int dump_size, header_size, superblock_width, superblock_height,
			offset_size, payload_offset;
	struct venc_vcu_config *pconfig;
	char *pbuf;

	mutex_lock(&ctx->buf_lock);
	do {
		if (inst != NULL && inst->vcu_inst.abort) {
			mtk_v4l2_err("abort when put buf");
			break;
		}

		dst_vb2_v4l2 = NULL;
		src_vb2_v4l2 = NULL;
		pfrm = NULL;
		pbs = NULL;

		memset(&rResult, 0, sizeof(rResult));
		get_free_buffers(ctx, &rResult);

		if (rResult.bs_va != 0 && virt_addr_valid((void *)rResult.bs_va)) {
			pbs = (struct mtk_vcodec_mem *)rResult.bs_va;
			bs_info = container_of(pbs,
				struct mtk_video_enc_buf, bs_buf);
			dst_vb2_v4l2 = &bs_info->vb;
		}

		if (rResult.frm_va != 0 && virt_addr_valid((void *)rResult.frm_va)) {
			pfrm = (struct venc_frm_buf *)rResult.frm_va;
			frm_info = container_of(pfrm,
				struct mtk_video_enc_buf, frm_buf);
			src_vb2_v4l2 = &frm_info->vb;

			if (rResult.flags & VENC_FLAG_ENCODE_TIMEOUT && pfrm->fb_addr[0].va != NULL) {
				mtk_v4l2_debug(0,"venc timeout dump frm_buf %d VA=%p PA=%llx Size=%zx =>",
				pfrm->index,
				pfrm->fb_addr[0].va,
				(u64)pfrm->fb_addr[0].dma_addr,
				pfrm->fb_addr[0].size);

				pbuf = (char *)pfrm->fb_addr[0].va;
				dump_size = pfrm->fb_addr[0].size < 1024 ? pfrm->fb_addr[0].size: 1024;
				venc_dump_data_section(pbuf, dump_size);

				//afbc data
				pconfig = &inst->vsi->config;
				if (isVencAfbcFormat(pconfig->input_fourcc)) {
					superblock_width = isVencAfbcRgbFormat(pconfig->input_fourcc) ? 32: 16;
					superblock_height = isVencAfbcRgbFormat(pconfig->input_fourcc) ? 8: 16;
					header_size = ROUND_N(16 * CEIL_DIV(pconfig->buf_w, superblock_width)
						* CEIL_DIV(pconfig->buf_h, superblock_height),
						4096);
					offset_size = isVencAfbcRgbFormat(pconfig->input_fourcc) ? 1024 :
						(isVencAfbc10BFormat(pconfig->input_fourcc) ? 512 : 384);

					mtk_v4l2_debug(0, "venc dump format %s(0x%x) afbc 1st mb of 1st block w/h=%d/%d offset=0x%x =>",
						FOURCC_STR(pconfig->input_fourcc), pconfig->input_fourcc,
						pconfig->buf_w, pconfig->buf_h, header_size);

					dump_size = ((header_size + 1024) < pfrm->fb_addr[0].size) ? 1024 : 0;
					pbuf = (char *)pfrm->fb_addr[0].va + (size_t)header_size;
					venc_dump_data_section(pbuf, dump_size);

					payload_offset = ROUND_N(header_size
						+ (offset_size * CEIL_DIV(pconfig->buf_w, superblock_width)),
						4096);

					mtk_v4l2_debug(0, "venc dump format %s(0x%x) afbc 2nd mb of 1st block w/h=%d/%d offset=0x%x =>",
						FOURCC_STR(pconfig->input_fourcc), pconfig->input_fourcc,
						pconfig->buf_w, pconfig->buf_h, payload_offset);

					dump_size = ((payload_offset + 1024) < pfrm->fb_addr[0].size) ? 1024 : 0;
					pbuf = (char *)pfrm->fb_addr[0].va + (size_t)payload_offset;
					venc_dump_data_section(pbuf, dump_size);

					payload_offset = ROUND_N(header_size
						+ (offset_size * CEIL_DIV(pconfig->buf_w, superblock_width) * 2),
						4096);

					mtk_v4l2_debug(0, "venc dump format %s(0x%x) afbc 3rd mb of 1st block w/h=%d/%d offset=0x%x =>",
						FOURCC_STR(pconfig->input_fourcc), pconfig->input_fourcc,
						pconfig->buf_w, pconfig->buf_h, payload_offset);

					dump_size = ((payload_offset + 1024) < pfrm->fb_addr[0].size) ? 1024 : 0;
					pbuf = (char *)pfrm->fb_addr[0].va + (size_t)payload_offset;
					venc_dump_data_section(pbuf, dump_size);

					payload_offset = ROUND_N(header_size
						+ (offset_size * (CEIL_DIV(pconfig->buf_w, superblock_width)
						* CEIL_DIV(pconfig->buf_h, superblock_height) - 1)),
						4096);

					mtk_v4l2_debug(0, "venc dump format %s(0x%x) afbc last mb of 1st block w/h=%d/%d offset=0x%x =>",
						FOURCC_STR(pconfig->input_fourcc), pconfig->input_fourcc,
						pconfig->buf_w, pconfig->buf_h, payload_offset);

					dump_size = ((payload_offset + 1024) < pfrm->fb_addr[0].size) ? 1024 : 0;
					pbuf = (char *)pfrm->fb_addr[0].va + (size_t)payload_offset;
					venc_dump_data_section(pbuf, dump_size);
				}

#if IS_ENABLED(CONFIG_MTK_TINYSYS_VCP_SUPPORT)
				if (rResult.flags & VENC_FLAG_ENCODE_HWBREAK_TIMEOUT)
					mtk_venc_trigger_vcp_halt(inst);
#endif
			}
		}

		if (src_vb2_v4l2 != NULL && dst_vb2_v4l2 != NULL) {
			if (rResult.is_key_frm)
				dst_vb2_v4l2->flags |= V4L2_BUF_FLAG_KEYFRAME;

			dst_vb2_v4l2->vb2_buf.timestamp =
				src_vb2_v4l2->vb2_buf.timestamp;
			dst_vb2_v4l2->timecode = src_vb2_v4l2->timecode;
			dst_vb2_v4l2->sequence = src_vb2_v4l2->sequence;
			dst_buf = &dst_vb2_v4l2->vb2_buf;
			dst_buf->planes[0].bytesused = rResult.bs_size;
			if (rResult.is_last_slc == 1)
				v4l2_m2m_buf_done(src_vb2_v4l2, VB2_BUF_STATE_DONE);
			else
				mtk_v4l2_debug(1, "cur slice is not last slice");
			v4l2_m2m_buf_done(dst_vb2_v4l2, VB2_BUF_STATE_DONE);

			mtk_v4l2_debug(1, "venc_if_encode bs size=%d is_last_slc=%d",
				rResult.bs_size, rResult.is_last_slc);
		} else if (src_vb2_v4l2 == NULL && dst_vb2_v4l2 != NULL) {
			dst_buf = &dst_vb2_v4l2->vb2_buf;
			dst_buf->planes[0].bytesused = rResult.bs_size;
			v4l2_m2m_buf_done(dst_vb2_v4l2,
					VB2_BUF_STATE_DONE);
			mtk_v4l2_debug(0, "[Warning] bs size=%d, frm NULL!!",
				rResult.bs_size);
		} else {
			if (src_vb2_v4l2 == NULL)
				mtk_v4l2_debug(1, "NULL enc src buffer\n");

			if (dst_vb2_v4l2 == NULL)
				mtk_v4l2_debug(1, "NULL enc dst buffer\n");
		}
	} while (rResult.bs_va != 0 || rResult.frm_va != 0);
	mutex_unlock(&ctx->buf_lock);
}

static struct mtk_video_fmt *mtk_venc_find_format(struct v4l2_format *f,
						  unsigned int t)
{
	struct mtk_video_fmt *fmt;
	unsigned int k;

	mtk_v4l2_debug(2, "fourcc %s(0x%x)", FOURCC_STR(f->fmt.pix_mp.pixelformat), f->fmt.pix_mp.pixelformat);
	for (k = 0; k < MTK_MAX_ENC_CODECS_SUPPORT &&
	     mtk_venc_formats[k].fourcc != 0; k++) {
		fmt = &mtk_venc_formats[k];
		if (fmt->fourcc == f->fmt.pix.pixelformat && fmt->type == t)
			return fmt;
	}

	return NULL;
}

static int vidioc_venc_check_supported_profile_level(__u32 fourcc,
	unsigned int pl, bool is_profile)
{
	struct v4l2_format f;
	int i = 0;

	f.fmt.pix.pixelformat = fourcc;
	if (mtk_venc_find_format(&f, MTK_FMT_ENC) == NULL)
		return false;

	for (i = 0; i < MTK_MAX_ENC_CODECS_SUPPORT; i++) {
		if (mtk_venc_framesizes[i].fourcc == fourcc) {
			if (is_profile) {
				if (mtk_venc_framesizes[i].profile & (1 << pl))
					return true;
				else
					return false;
			} else {
				if (mtk_venc_framesizes[i].level >= pl)
					return true;
				else
					return false;
			}
		}
	}
	return false;
}

static int vidioc_venc_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct mtk_vcodec_ctx *ctx = ctrl_to_ctx(ctrl);
	struct mtk_enc_params *p = &ctx->enc_params;
	struct vb2_queue *src_vq;
	int ret = 0;

	mtk_v4l2_debug(4, "[%d] id %d val %d array[0] %d array[1] %d",
				   ctx->id, ctrl->id, ctrl->val,
				   ctrl->p_new.p_u32[0], ctrl->p_new.p_u32[1]);

	switch (ctrl->id) {
	case V4L2_CID_MPEG_VIDEO_BITRATE:
		mtk_v4l2_debug(2, "V4L2_CID_MPEG_VIDEO_BITRATE val = %d",
			       ctrl->val);
		p->bitrate = ctrl->val;
		ctx->param_change |= MTK_ENCODE_PARAM_BITRATE;
		break;
	case V4L2_CID_MPEG_MTK_SEC_ENCODE: {
#if IS_ENABLED(CONFIG_MTK_TINYSYS_VCP_SUPPORT)
		struct vb2_queue *dst_vq;

		if (ctrl->val) {
			if (vcp_get_io_device_ex(VCP_IOMMU_SEC)) {
				dst_vq = v4l2_m2m_get_vq(ctx->m2m_ctx,
					V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);
				if (!dst_vq) {
					mtk_v4l2_err("fail to get dst_vq");
					return -EINVAL;
				}
				dst_vq->dev = vcp_get_io_device_ex(VCP_IOMMU_SEC);
				mtk_v4l2_debug(4, "[%d] dst_vq use VCP_IOMMU_SEC domain %p", ctx->id, dst_vq->dev);
			}

		}
#endif
		p->svp_mode = ctrl->val;
		ctx->param_change |= MTK_ENCODE_PARAM_SEC_ENCODE;
		mtk_v4l2_debug(0, "[%d] V4L2_CID_MPEG_MTK_SEC_ENCODE id %d val %d array[0] %d array[1] %d",
			ctx->id, ctrl->id, ctrl->val,
		ctrl->p_new.p_u32[0], ctrl->p_new.p_u32[1]);
		break;
	}
	case V4L2_CID_MPEG_VIDEO_B_FRAMES:
		mtk_v4l2_debug(2, "V4L2_CID_MPEG_VIDEO_B_FRAMES val = %d",
			       ctrl->val);
		p->num_b_frame = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_FRAME_RC_ENABLE:
		mtk_v4l2_debug(2, "V4L2_CID_MPEG_VIDEO_FRAME_RC_ENABLE val = %d",
			       ctrl->val);
		p->rc_frame = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_MAX_QP:
		mtk_v4l2_debug(2, "V4L2_CID_MPEG_VIDEO_H264_MAX_QP val = %d",
			       ctrl->val);
		p->h264_max_qp = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_HEADER_MODE:
		mtk_v4l2_debug(2, "V4L2_CID_MPEG_VIDEO_HEADER_MODE val = %d",
			       ctrl->val);
		p->seq_hdr_mode = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_MB_RC_ENABLE:
		mtk_v4l2_debug(2, "V4L2_CID_MPEG_VIDEO_MB_RC_ENABLE val = %d",
			       ctrl->val);
		p->rc_mb = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_PROFILE:
		mtk_v4l2_debug(2, "V4L2_CID_MPEG_VIDEO_H264_PROFILE val = %d",
			       ctrl->val);
		if (!vidioc_venc_check_supported_profile_level(
				V4L2_PIX_FMT_H264, ctrl->val, 1))
			return -EINVAL;
		p->profile = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_HEVC_PROFILE:
		mtk_v4l2_debug(2, "V4L2_CID_MPEG_VIDEO_HEVC_PROFILE val = %d",
			       ctrl->val);
		if (!vidioc_venc_check_supported_profile_level(
				V4L2_PIX_FMT_HEVC, ctrl->val, 1))
			return -EINVAL;
		p->profile = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_MPEG4_PROFILE:
		mtk_v4l2_debug(2, "V4L2_CID_MPEG_VIDEO_MPEG4_PROFILE val = %d",
			       ctrl->val);
		if (!vidioc_venc_check_supported_profile_level(
			    V4L2_PIX_FMT_MPEG4, ctrl->val, 1))
			return -EINVAL;
		p->profile = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_LEVEL:
		mtk_v4l2_debug(2, "V4L2_CID_MPEG_VIDEO_H264_LEVEL val = %d",
			       ctrl->val);
		if (!vidioc_venc_check_supported_profile_level(
				V4L2_PIX_FMT_H264, ctrl->val, 0))
			return -EINVAL;
		p->level = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_HEVC_LEVEL:
		mtk_v4l2_debug(2, "V4L2_CID_MPEG_VIDEO_HEVC_LEVEL val = %d",
			       ctrl->val);
		if (!vidioc_venc_check_supported_profile_level(
				V4L2_PIX_FMT_HEVC, ctrl->val, 0))
			return -EINVAL;
		p->level = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_HEVC_TIER:
		mtk_v4l2_debug(2, "V4L2_CID_MPEG_VIDEO_HEVC_TIER val = %d",
			ctrl->val);
		p->tier = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_MPEG4_LEVEL:
		mtk_v4l2_debug(2, "V4L2_CID_MPEG_VIDEO_MPEG4_LEVEL val = %d",
			       ctrl->val);
		if (!vidioc_venc_check_supported_profile_level(
			    V4L2_PIX_FMT_MPEG4, ctrl->val, 0))
			return -EINVAL;
		p->level = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_I_PERIOD:
		mtk_v4l2_debug(2, "V4L2_CID_MPEG_VIDEO_H264_I_PERIOD val = %d",
			       ctrl->val);
		p->intra_period = ctrl->val;
		ctx->param_change |= MTK_ENCODE_PARAM_INTRA_PERIOD;
		break;
	case V4L2_CID_MPEG_VIDEO_GOP_SIZE:
		mtk_v4l2_debug(2, "V4L2_CID_MPEG_VIDEO_GOP_SIZE val = %d",
			       ctrl->val);
		p->gop_size = ctrl->val;
		ctx->param_change |= MTK_ENCODE_PARAM_GOP_SIZE;
		break;
	case V4L2_CID_MPEG_VIDEO_FORCE_KEY_FRAME:
		mtk_v4l2_debug(2, "V4L2_CID_MPEG_VIDEO_FORCE_KEY_FRAME");
		p->force_intra = 1;
		ctx->param_change |= MTK_ENCODE_PARAM_FORCE_INTRA;
		break;
	case V4L2_CID_MPEG_MTK_ENCODE_SCENARIO:
		mtk_v4l2_debug(2,
			"V4L2_CID_MPEG_MTK_ENCODE_SCENARIO: %d",
			ctrl->val);
		p->scenario = ctrl->val;
		ctx->param_change |= MTK_ENCODE_PARAM_SCENARIO;
		if (p->scenario == 3 || p->scenario == 1) {
			src_vq = v4l2_m2m_get_vq(ctx->m2m_ctx,
				V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
			if (!src_vq) {
				mtk_v4l2_err("fail to get src_vq");
				return -EINVAL;
			}
			src_vq->mem_ops = &venc_dma_contig_memops;
#if (!(IS_ENABLED(CONFIG_DEVICE_MODULES_ARM_SMMU_V3)))
			if (ctx->enc_params.svp_mode && is_disable_map_sec() && mtk_venc_is_vcu())
				src_vq->mem_ops = &venc_sec_dma_contig_memops;
#endif
		}
		break;
	case V4L2_CID_MPEG_MTK_ENCODE_NONREFP:
		mtk_v4l2_debug(2,
			"V4L2_CID_MPEG_MTK_ENCODE_NONREFP: %d",
			ctrl->val);
		p->nonrefp = ctrl->val;
		ctx->param_change |= MTK_ENCODE_PARAM_NONREFP;
		break;
	case V4L2_CID_MPEG_MTK_ENCODE_NONREFP_FREQ:
		mtk_v4l2_debug(2,
			"V4L2_CID_MPEG_MTK_ENCODE_NONREFP_FREQ: %d",
			ctrl->val);
		p->nonrefpfreq = ctrl->val;
		ctx->param_change |= MTK_ENCODE_PARAM_NONREFPFREQ;
		break;

	case V4L2_CID_MPEG_MTK_ENCODE_DETECTED_FRAMERATE:
		mtk_v4l2_debug(2,
			"V4L2_CID_MPEG_MTK_ENCODE_DETECTED_FRAMERATE: %d",
			ctrl->val);
		p->detectframerate = ctrl->val;
		ctx->param_change |= MTK_ENCODE_PARAM_DETECTED_FRAMERATE;
		break;
	case V4L2_CID_MPEG_MTK_ENCODE_RFS_ON:
		mtk_v4l2_debug(2,
			"V4L2_CID_MPEG_MTK_ENCODE_RFS_ON: %d",
			ctrl->val);
		p->rfs = ctrl->val;
		ctx->param_change |= MTK_ENCODE_PARAM_RFS_ON;
		break;
	case V4L2_CID_MPEG_VIDEO_PREPEND_SPSPPS_TO_IDR:
		mtk_v4l2_debug(2,
			"V4L2_CID_MPEG_VIDEO_PREPEND_SPSPPS_TO_IDR: %d",
			ctrl->val);
		p->prependheader = ctrl->val;
		ctx->param_change |= MTK_ENCODE_PARAM_PREPEND_SPSPPS_TO_IDR;
		break;
	case V4L2_CID_MPEG_MTK_ENCODE_OPERATION_RATE:
		mtk_v4l2_debug(2,
			"V4L2_CID_MPEG_MTK_ENCODE_OPERATION_RATE: %d",
			ctrl->val);
		p->operationrate = ctrl->val;
		ctx->param_change |= MTK_ENCODE_PARAM_OPERATION_RATE;
		break;
	case V4L2_CID_MPEG_VIDEO_BITRATE_MODE:
		mtk_v4l2_debug(2,
			"V4L2_CID_MPEG_VIDEO_BITRATE_MODE: %d",
			ctrl->val);
		p->bitratemode = ctrl->val;
		ctx->param_change |= MTK_ENCODE_PARAM_BITRATE_MODE;
		break;
	case V4L2_CID_MPEG_MTK_ENCODE_ROI_ON:
		mtk_v4l2_debug(2,
			"V4L2_CID_MPEG_MTK_ENCODE_ROI_ON: %d",
			ctrl->val);
		p->roion = ctrl->val;
		ctx->param_change |= MTK_ENCODE_PARAM_ROI_ON;
		break;
	case V4L2_CID_MPEG_MTK_ENCODE_GRID_SIZE:
		mtk_v4l2_debug(2,
			"V4L2_CID_MPEG_MTK_ENCODE_GRID_SIZE: %d",
			ctrl->val);
		p->heif_grid_size = ctrl->val;
		ctx->param_change |= MTK_ENCODE_PARAM_GRID_SIZE;
		break;
	case V4L2_CID_MPEG_MTK_COLOR_DESC:
		mtk_v4l2_debug(2,
			"V4L2_CID_MPEG_MTK_COLOR_DESC: 0x%x",
			ctrl->val);
		memcpy(&p->color_desc, ctrl->p_new.p_u32,
		sizeof(struct mtk_color_desc));
		ctx->param_change |= MTK_ENCODE_PARAM_COLOR_DESC;
		break;
	case V4L2_CID_MPEG_MTK_MAX_WIDTH:
		mtk_v4l2_debug(2,
			"V4L2_CID_MPEG_MTK_MAX_WIDTH: %d",
			ctrl->val);
		p->max_w = ctrl->val;
		break;
	case V4L2_CID_MPEG_MTK_MAX_HEIGHT:
		mtk_v4l2_debug(2,
			"V4L2_CID_MPEG_MTK_MAX_HEIGHT: %d",
			ctrl->val);
		p->max_h = ctrl->val;
		break;
	case V4L2_CID_MPEG_MTK_ENCODE_RC_I_FRAME_QP:
		mtk_v4l2_debug(2,
			"V4L2_CID_MPEG_MTK_ENCODE_RC_I_FRAME_QP val = %d",
			ctrl->val);
		p->i_qp = ctrl->val;
		break;
	case V4L2_CID_MPEG_MTK_ENCODE_RC_P_FRAME_QP:
		mtk_v4l2_debug(2,
			"V4L2_CID_MPEG_MTK_ENCODE_RC_P_FRAME_QP val = %d",
			ctrl->val);
		p->p_qp = ctrl->val;
		break;
	case V4L2_CID_MPEG_MTK_ENCODE_RC_B_FRAME_QP:
		mtk_v4l2_debug(2,
			"V4L2_CID_MPEG_MTK_ENCODE_RC_B_FRAME_QP val = %d",
			ctrl->val);
		p->b_qp = ctrl->val;
		break;
	case V4L2_CID_MPEG_MTK_ENCODE_WPP_MODE:
		mtk_v4l2_debug(0,
			"V4L2_CID_MPEG_MTK_ENCODE_WPP_MODE: %d",
			ctrl->val);
		p->wpp_mode = ctrl->val;
		break;
	case V4L2_CID_MPEG_MTK_ENCODE_LOW_LATENCY_MODE:
		mtk_v4l2_debug(0,
			"V4L2_CID_MPEG_MTK_ENCODE_LOW_LATENCY_MODE: %d",
			ctrl->val);
		p->low_latency_mode = ctrl->val;
		if (p->low_latency_mode == 1) {
			p->use_irq = 1;
			init_waitqueue_head(&ctx->bs_wq);
		} else
			p->use_irq = 0;
		break;
	case V4L2_CID_MPEG_MTK_ENCODE_ENABLE_HIGHQUALITY:
		mtk_v4l2_debug(2,
			"V4L2_CID_MPEG_MTK_ENCODE_ENABLE_HIGHQUALITY: %d",
			ctrl->val);
		p->highquality = ctrl->val;
		ctx->param_change |= MTK_ENCODE_PARAM_HIGHQUALITY;
		break;
	case V4L2_CID_MPEG_MTK_ENCODE_RC_MAX_QP:
		mtk_v4l2_debug(0, "V4L2_CID_MPEG_MTK_ENCODE_RC_MAX_QP");
		p->max_qp = ctrl->val;
		ctx->param_change |= MTK_ENCODE_PARAM_MAXQP;
		break;
	case V4L2_CID_MPEG_MTK_ENCODE_RC_MIN_QP:
		mtk_v4l2_debug(0, "V4L2_CID_MPEG_MTK_ENCODE_RC_MIN_QP");
		p->min_qp = ctrl->val;
		ctx->param_change |= MTK_ENCODE_PARAM_MINQP;
		break;
	case V4L2_CID_MPEG_MTK_ENCODE_RC_I_P_QP_DELTA:
		mtk_v4l2_debug(0, "V4L2_CID_MPEG_MTK_ENCODE_RC_I_P_QP_DELTA");
		p->ip_qpdelta = ctrl->val;
		ctx->param_change |= MTK_ENCODE_PARAM_IP_QPDELTA;
		break;
	case V4L2_CID_MPEG_MTK_ENCODE_RC_FRAME_LEVEL_QP:
		mtk_v4l2_debug(0, "V4L2_CID_MPEG_MTK_ENCODE_RC_FRAME_LEVEL_QP");
		p->framelvl_qp = ctrl->val;
		ctx->param_change |= MTK_ENCODE_PARAM_FRAMELVLQP;
		break;
	case V4L2_CID_MPEG_MTK_ENCODE_RC_QP_CONTROL_MODE:
		mtk_v4l2_debug(0, "V4L2_CID_MPEG_MTK_ENCODE_RC_QP_CONTROL_MODE");
		p->qp_control_mode = ctrl->val;
		ctx->param_change |= MTK_ENCODE_PARAM_QP_CTRL_MODE;
		break;
	case V4L2_CID_MPEG_MTK_ENCODE_ENABLE_DUMMY_NAL:
		mtk_v4l2_debug(2,
			"V4L2_CID_MPEG_MTK_ENCODE_ENABLE_DUMMY_NAL: %d",
			ctrl->val);
		p->dummynal = ctrl->val;
		ctx->param_change |= MTK_ENCODE_PARAM_DUMMY_NAL;
		break;
	case V4L2_CID_MPEG_MTK_LOG:
		mtk_vcodec_set_log(
			ctx, ctx->dev, ctrl->p_new.p_char,
			MTK_VCODEC_LOG_INDEX_LOG, set_vcu_vpud_log);
		break;
	case V4L2_CID_MPEG_MTK_VCP_PROP:
		mtk_vcodec_set_log(
			ctx, ctx->dev, ctrl->p_new.p_char, MTK_VCODEC_LOG_INDEX_PROP, NULL);
		break;
	case V4L2_CID_MPEG_VIDEO_ENABLE_TSVC:
		mtk_v4l2_debug(0,
			"V4L2_CID_MPEG_VIDEO_ENABLE_TSVC layer: %d, type: %d\n",
			ctrl->p_new.p_u32[0], ctrl->p_new.p_u32[1]);
		p->hier_ref_layer = ctrl->p_new.p_u32[0];
		p->hier_ref_type = ctrl->p_new.p_u32[1];
		break;
	case V4L2_CID_MPEG_MTK_ENCODE_MULTI_REF:
		mtk_v4l2_debug(0,
			"V4L2_CID_MPEG_MTK_ENCODE_MULTI_REF multi_ref_en: %d\n",
			ctrl->p_new.p_u32[0]);
		memcpy(&p->multi_ref, ctrl->p_new.p_u32,
		sizeof(struct mtk_venc_multi_ref));
		break;
	case V4L2_CID_MPEG_VIDEO_H264_VUI_SAR_ENABLE:
		mtk_v4l2_debug(2,
			"V4L2_CID_MPEG_VIDEO_H264_VUI_SAR_ENABLE: 0x%x",
			ctrl->val);
		memcpy(&p->vui_info, ctrl->p_new.p_u32,
		sizeof(struct mtk_venc_vui_info));
		break;
	case V4L2_CID_MPEG_VIDEO_MULTI_SLICE_MAX_MB:
		mtk_v4l2_debug(2,
			"V4L2_CID_MPEG_VIDEO_MULTI_SLICE_MAX_MB: 0x%x",
			ctrl->val);
		p->slice_header_spacing = ctrl->val;
		break;
	case V4L2_CID_MPEG_MTK_ENCODE_TEMPORAL_LAYER_COUNT:
		mtk_v4l2_debug(2,
			"V4L2_CID_MPEG_MTK_ENCODE_TEMPORAL_LAYER_COUNT temporal_layer_pcount: %d, temporal_layer_bcount: %d\n",
			ctrl->p_new.p_u32[0], ctrl->p_new.p_u32[1]);
		p->temporal_layer_pcount = ctrl->p_new.p_u32[0];
		p->temporal_layer_bcount = ctrl->p_new.p_u32[1];
		break;
	case V4L2_CID_MPEG_MTK_ENCODE_MAX_LTR_FRAMES:
		mtk_v4l2_debug(2,
			"V4L2_CID_MPEG_MTK_ENCODE_MAX_LTR_FRAMES val = %d",
			ctrl->val);
		p->max_ltr_num = ctrl->val;
		break;
	case V4L2_CID_MPEG_MTK_ENCODE_LOW_LATENCY_WFD:
		mtk_v4l2_debug(2,
			"V4L2_CID_MPEG_MTK_ENCODE_LOW_LATENCY_WFD: %d",
			ctrl->val);
		p->lowlatencywfd = ctrl->val;
		ctx->param_change |= MTK_ENCODE_PARAM_LOW_LATENCY_WFD;
		break;
	case V4L2_CID_MPEG_MTK_ENCODE_SLICE_CNT:
		mtk_v4l2_debug(2,
			"V4L2_CID_MPEG_MTK_ENCODE_SLICE_CNT: %d",
			ctrl->val);
		p->slice_count = ctrl->val;
		ctx->param_change |= MTK_ENCODE_PARAM_SLICE_CNT;
		break;
	case V4L2_CID_MPEG_MTK_ENCODE_QPVBR:
		mtk_v4l2_debug(2,
			"V4L2_CID_MPEG_MTK_ENCODE_QPVBR: upper_enable(%d) maxqp(%d) maxbrratio(%d)",
			ctrl->p_new.p_s32[0], ctrl->p_new.p_s32[1], ctrl->p_new.p_s32[2]);
		mtk_v4l2_debug(2,
			"V4L2_CID_MPEG_MTK_ENCODE_QPVBR: lower_enable(%d) minqp(%d) minbrratio(%d)",
			ctrl->p_new.p_s32[3], ctrl->p_new.p_s32[4], ctrl->p_new.p_s32[5]);
		p->qpvbr_upper_enable = ctrl->p_new.p_s32[0];
		p->qpvbr_qp_upper_threshold = ctrl->p_new.p_s32[1];
		p->qpvbr_qp_max_brratio = ctrl->p_new.p_s32[2];
		p->qpvbr_lower_enable = ctrl->p_new.p_s32[3];
		p->qpvbr_qp_lower_threshold = ctrl->p_new.p_s32[4];
		p->qpvbr_qp_min_brratio = ctrl->p_new.p_s32[5];
		break;
	case V4L2_CID_MPEG_MTK_ENCODE_CHROMA_QP:
		mtk_v4l2_debug(2,
			"V4L2_CID_MPEG_MTK_ENCODE_CHROMA_QP: cbqp(%d) crqp(%d)",
			ctrl->p_new.p_s32[0], ctrl->p_new.p_s32[1]);
		p->cb_qp_offset = ctrl->p_new.p_s32[0];
		p->cr_qp_offset = ctrl->p_new.p_s32[1];
		ctx->param_change |= MTK_ENCODE_PARAM_CHROMAQP;
		break;
	case V4L2_CID_MPEG_MTK_ENCODE_MB_RC_TK_SPD:
		mtk_v4l2_debug(2,
			"V4L2_CID_MPEG_MTK_ENCODE_MB_RC_TK_SPD: %d",
			ctrl->val);
		p->mbrc_tk_spd = ctrl->val;
		ctx->param_change |= MTK_ENCODE_PARAM_MBRC_TKSPD;
		break;
	case V4L2_CID_MPEG_MTK_ENCODE_FRM_QP_LTR:
		mtk_v4l2_debug(2,
			"V4L2_CID_MPEG_MTK_ENCODE_FRM_QP_LTR: I(%d), P(%d), B(%d)",
			ctrl->p_new.p_s32[0], ctrl->p_new.p_s32[1], ctrl->p_new.p_s32[2]);
		p->ifrm_q_ltr = ctrl->p_new.p_s32[0];
		p->pfrm_q_ltr = ctrl->p_new.p_s32[1];
		p->bfrm_q_ltr = ctrl->p_new.p_s32[2];
		break;
	case V4L2_CID_MPEG_MTK_ENCODE_VISUAL_QUALITY:
		mtk_v4l2_debug(2,
			"V4L2_CID_MPEG_MTK_ENCODE_VISUAL_QUALITY: quant(%d), rd(%d)",
			ctrl->p_new.p_s32[0], ctrl->p_new.p_s32[1]);
		memcpy(&p->visual_quality, ctrl->p_new.p_s32,
		sizeof(struct mtk_venc_visual_quality));
		ctx->param_change |= MTK_ENCODE_PARAM_VISUAL_QUALITY;
		break;
	case V4L2_CID_MPEG_MTK_ENCODE_INIT_QP:
		mtk_v4l2_debug(2,
			"V4L2_CID_MPEG_MTK_ENCODE_INIT_QP: enable(%d), I(%d), P(%d), B(%d)",
			ctrl->p_new.p_s32[0], ctrl->p_new.p_s32[1],
			ctrl->p_new.p_s32[2], ctrl->p_new.p_s32[3]);
		memcpy(&p->init_qp, ctrl->p_new.p_s32,
		sizeof(struct mtk_venc_init_qp));
		ctx->param_change |= MTK_ENCODE_PARAM_INIT_QP;
		break;
	case V4L2_CID_MPEG_MTK_ENCODE_FRAME_QP_RANGE:
		mtk_v4l2_debug(2,
			"V4L2_CID_MPEG_MTK_ENCODE_FRAME_QP_RANGE: Enable(%d), MAX(%d), MIN(%d)",
			ctrl->p_new.p_s32[0], ctrl->p_new.p_s32[1], ctrl->p_new.p_s32[2]);
		memcpy(&p->frame_qp_range, ctrl->p_new.p_s32,
		sizeof(struct mtk_venc_frame_qp_range));
		ctx->param_change |= MTK_ENCODE_PARAM_FRAMEQP_RANGE;
		break;
	case V4L2_CID_MPEG_MTK_CALLING_PID:
		ctx->cpu_caller_pid = ctrl->val;
		break;
	case V4L2_CID_MPEG_MTK_SET_NAL_SIZE_LENGTH:
		mtk_v4l2_debug(2,
			"V4L2_CID_MPEG_MTK_SET_NAL_SIZE_LENGTH: Prefer(%d), Bytes(%d)",
			ctrl->p_new.p_u32[0], ctrl->p_new.p_u32[1]);
		memcpy(&p->nal_length, ctrl->p_new.p_u32,
		sizeof(struct mtk_venc_nal_length));
		break;
	case V4L2_CID_MPEG_MTK_ENCODE_ENABLE_MLVEC_MODE:
		mtk_v4l2_debug(2,
			"V4L2_CID_MPEG_MTK_ENCODE_ENABLE_MLVEC_MODE val = %d",
			ctrl->val);
		p->mlvec_mode = ctrl->val;
		break;
	case V4L2_CID_MPEG_MTK_ENCODE_CONFIG_DATA:
		mtk_v4l2_debug(0, "V4L2_CID_MPEG_MTK_ENCODE_CONFIG_DATA");
		ret = mtk_vcodec_enc_set_config_data(ctx, ctrl->p_new.p_u8);
		break;
	default:
		mtk_v4l2_debug(4, "ctrl-id=%d not support!", ctrl->id);
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int vidioc_venc_g_ctrl(struct v4l2_ctrl *ctrl)
{
	struct mtk_vcodec_ctx *ctx = ctrl_to_ctx(ctrl);
	int ret = 0;
	int value = 0;
	struct venc_resolution_change *reschange;

	switch (ctrl->id) {
	case V4L2_CID_MPEG_MTK_ENCODE_ROI_RC_QP:
		venc_if_get_param(ctx,
			GET_PARAM_ROI_RC_QP,
			&value);
		ctrl->val = value;
		break;
	case V4L2_CID_MPEG_MTK_RESOLUTION_CHANGE:
		reschange = (struct venc_resolution_change *)ctrl->p_new.p_u32;
		venc_if_get_param(ctx,
			GET_PARAM_RESOLUTION_CHANGE,
			reschange);
		break;
	case V4L2_CID_MPEG_MTK_GET_LOG:
		mtk_vcodec_get_log(
			ctx, ctx->dev, ctrl->p_new.p_char,
			MTK_VCODEC_LOG_INDEX_LOG, get_vcu_vpud_log);
		break;
	case V4L2_CID_MPEG_MTK_GET_VCP_PROP:
		mtk_vcodec_get_log(
			ctx, ctx->dev, ctrl->p_new.p_char, MTK_VCODEC_LOG_INDEX_PROP, NULL);
		break;
	case V4L2_CID_MPEG_MTK_ENCODE_GET_MAX_B_NUM:
		ctrl->val = mtk_venc_cap_common.max_b;
		break;
	case V4L2_CID_MPEG_MTK_ENCODE_GET_MAX_TEMPORAL_LAYER:
		ctrl->val = mtk_venc_cap_common.max_temporal_layer;
		break;
	default:
		mtk_v4l2_debug(4, "ctrl-id=%d not support!", ctrl->id);
		ret = -EINVAL;
		break;
	}

	return ret;
}

static const struct v4l2_ctrl_ops mtk_vcodec_enc_ctrl_ops = {
	.s_ctrl = vidioc_venc_s_ctrl,
	.g_volatile_ctrl = vidioc_venc_g_ctrl,
};

static int vidioc_enum_fmt(struct v4l2_fmtdesc *f, bool output_queue)
{
	struct mtk_video_fmt *fmt;
	int i, j = 0;

	for (i = 0; i < MTK_MAX_ENC_CODECS_SUPPORT &&
	     mtk_venc_formats[i].fourcc != 0; ++i) {
		if (output_queue && mtk_venc_formats[i].type != MTK_FMT_FRAME)
			continue;
		if (!output_queue && mtk_venc_formats[i].type != MTK_FMT_ENC)
			continue;

		if (j == f->index) {
			fmt = &mtk_venc_formats[i];
			f->pixelformat = fmt->fourcc;
			memset(f->reserved, 0, sizeof(f->reserved));
			v4l_fill_mtk_fmtdesc(f);
			return 0;
		}
		++j;
	}

	return -EINVAL;
}

static int vidioc_enum_framesizes(struct file *file, void *fh,
				  struct v4l2_frmsizeenum *fsize)
{
	unsigned int i = 0;

	if (fsize->index != 0)
		return -EINVAL;

	for (i = 0; i < MTK_MAX_ENC_CODECS_SUPPORT &&
	     mtk_venc_framesizes[i].fourcc != 0; ++i) {
		if (fsize->pixel_format != mtk_venc_framesizes[i].fourcc)
			continue;

		fsize->type = V4L2_FRMSIZE_TYPE_STEPWISE;
		fsize->stepwise = mtk_venc_framesizes[i].stepwise;
		fsize->reserved[0] = mtk_venc_framesizes[i].profile;
		fsize->reserved[1] = mtk_venc_framesizes[i].level;
		return 0;
	}

	return -EINVAL;
}

static int vidioc_enum_fmt_vid_cap_mplane(struct file *file, void *priv,
					  struct v4l2_fmtdesc *f)
{
	return vidioc_enum_fmt(f, false);
}

static int vidioc_enum_fmt_vid_out_mplane(struct file *file, void *priv,
					  struct v4l2_fmtdesc *f)
{
	return vidioc_enum_fmt(f, true);
}

static int vidioc_venc_querycap(struct file *file, void *priv,
				struct v4l2_capability *cap)
{
	struct mtk_vcodec_ctx *ctx = fh_to_ctx(priv);
	struct mtk_vcodec_dev *dev = ctx->dev;

	strscpy(cap->driver, MTK_VCODEC_ENC_NAME, sizeof(cap->driver));
	strscpy(cap->bus_info, dev->platform, sizeof(cap->bus_info));
	strscpy(cap->card, dev->platform, sizeof(cap->card));

	cap->device_caps  = V4L2_CAP_VIDEO_M2M_MPLANE | V4L2_CAP_STREAMING;
	cap->capabilities = cap->device_caps | V4L2_CAP_DEVICE_CAPS;

	return 0;
}

static int vidioc_venc_s_parm(struct file *file, void *priv,
			      struct v4l2_streamparm *a)
{
	struct mtk_vcodec_ctx *ctx = fh_to_ctx(priv);

	if (a->type != V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
		return -EINVAL;

	ctx->enc_params.framerate_num =
		a->parm.output.timeperframe.denominator;
	ctx->enc_params.framerate_denom =
		a->parm.output.timeperframe.numerator;
	ctx->param_change |= MTK_ENCODE_PARAM_FRAMERATE;

	a->parm.output.capability = V4L2_CAP_TIMEPERFRAME;

	return 0;
}

static int vidioc_venc_g_parm(struct file *file, void *priv,
			      struct v4l2_streamparm *a)
{
	struct mtk_vcodec_ctx *ctx = fh_to_ctx(priv);

	if (a->type != V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
		return -EINVAL;

	a->parm.output.capability = V4L2_CAP_TIMEPERFRAME;
	a->parm.output.timeperframe.denominator =
		ctx->enc_params.framerate_num;
	a->parm.output.timeperframe.numerator =
		ctx->enc_params.framerate_denom;

	return 0;
}

static struct mtk_q_data *mtk_venc_get_q_data(struct mtk_vcodec_ctx *ctx,
					      enum v4l2_buf_type type)
{
	if (ctx == NULL)
		return NULL;

	if (V4L2_TYPE_IS_OUTPUT(type))
		return &ctx->q_data[MTK_Q_DATA_SRC];

	return &ctx->q_data[MTK_Q_DATA_DST];
}

/* V4L2 specification suggests the driver corrects the format struct if any of
 * the dimensions is unsupported
 */
static int vidioc_try_fmt(struct v4l2_format *f, struct mtk_video_fmt *fmt,
			  struct mtk_vcodec_ctx *ctx)
{
	struct v4l2_pix_format_mplane *pix_fmt_mp = NULL;
	int org_w, org_h, i;
	int bitsPP = 8;  /* bits per pixel */
	__u32 bs_fourcc;
	unsigned int step_width_in_pixel;
	unsigned int step_height_in_pixel;
	unsigned int saligned;
	unsigned int imagePixels;
	// for AFBC
	unsigned int block_w = 16;
	unsigned int block_h = 16;
	unsigned int block_count;

	struct mtk_codec_framesizes *spec_size_info = NULL;

	if (IS_ERR_OR_NULL(fmt)) {
		mtk_v4l2_err("fail to get mtk_video_fmt");
		return -EINVAL;
	}
	pix_fmt_mp = &f->fmt.pix_mp;
	pix_fmt_mp->field = V4L2_FIELD_NONE;

	if (f->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
		pix_fmt_mp->num_planes = 1;
		pix_fmt_mp->plane_fmt[0].bytesperline = 0;
	} else if (f->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		if (ctx->q_data[MTK_Q_DATA_DST].fmt != NULL) {
			bs_fourcc =
				ctx->q_data[MTK_Q_DATA_DST].fmt->fourcc;
		} else {
			bs_fourcc =
				mtk_venc_formats[default_cap_fmt_idx].fourcc;
		}
		for (i = 0; i < MTK_MAX_ENC_CODECS_SUPPORT; i++) {
			if (mtk_venc_framesizes[i].fourcc == bs_fourcc)
				spec_size_info = &mtk_venc_framesizes[i];
		}
		if (!spec_size_info) {
			mtk_v4l2_err("fail to get spec_size_info");
			return -EINVAL;
		}

		mtk_v4l2_debug(1, "pix_fmt_mp->pixelformat %s(0x%x) bs fmt %s(0x%x) w %d h %d min_w %d min_h %d max_w %d max_h %d step_w %d step_h %d\n",
			FOURCC_STR(pix_fmt_mp->pixelformat), pix_fmt_mp->pixelformat,
			FOURCC_STR(bs_fourcc), bs_fourcc,
			pix_fmt_mp->width, pix_fmt_mp->height,
			spec_size_info->stepwise.min_width,
			spec_size_info->stepwise.min_height,
			spec_size_info->stepwise.max_width,
			spec_size_info->stepwise.max_height,
			spec_size_info->stepwise.step_width,
			spec_size_info->stepwise.step_height);

		if ((spec_size_info->stepwise.step_width &
		     (spec_size_info->stepwise.step_width - 1)) != 0)
			mtk_v4l2_err("Unsupport stepwise.step_width not 2^ %d\n",
				     spec_size_info->stepwise.step_width);
		if ((spec_size_info->stepwise.step_height &
		     (spec_size_info->stepwise.step_height - 1)) != 0)
			mtk_v4l2_err("Unsupport stepwise.step_height not 2^ %d\n",
				     spec_size_info->stepwise.step_height);

		if (pix_fmt_mp->pixelformat == V4L2_PIX_FMT_MT10 ||
			pix_fmt_mp->pixelformat == V4L2_PIX_FMT_MT10S) {
			step_width_in_pixel =
				spec_size_info->stepwise.step_width * 4;
			step_height_in_pixel =
				spec_size_info->stepwise.step_height;
			bitsPP = 10;
			saligned = 6;
		} else if (pix_fmt_mp->pixelformat == V4L2_PIX_FMT_P010M ||
			pix_fmt_mp->pixelformat == V4L2_PIX_FMT_P010S) {
			step_width_in_pixel =
				spec_size_info->stepwise.step_width / 2;
			step_height_in_pixel =
				spec_size_info->stepwise.step_height;
			bitsPP = 16;
			saligned = 6;
		} else if (pix_fmt_mp->pixelformat == V4L2_PIX_FMT_ABGR32 ||
			pix_fmt_mp->pixelformat == V4L2_PIX_FMT_ARGB32 ||
			pix_fmt_mp->pixelformat == V4L2_PIX_FMT_RGB32 ||
			pix_fmt_mp->pixelformat == V4L2_PIX_FMT_RGBA32 ||
			pix_fmt_mp->pixelformat == V4L2_PIX_FMT_BGR32 ||
			pix_fmt_mp->pixelformat == V4L2_PIX_FMT_ARGB1010102 ||
			pix_fmt_mp->pixelformat == V4L2_PIX_FMT_ABGR1010102 ||
			pix_fmt_mp->pixelformat == V4L2_PIX_FMT_RGBA1010102 ||
			pix_fmt_mp->pixelformat == V4L2_PIX_FMT_BGRA1010102) {
			step_width_in_pixel = 1;
			step_height_in_pixel = 1;
			bitsPP = 32;
			saligned = 4;
		} else if (pix_fmt_mp->pixelformat == V4L2_PIX_FMT_RGB24 ||
			pix_fmt_mp->pixelformat == V4L2_PIX_FMT_BGR24) {
			step_width_in_pixel = 1;
			step_height_in_pixel = 1;
			bitsPP = 24;
			saligned = 4;
		} else {
			step_width_in_pixel =
				spec_size_info->stepwise.step_width;
			step_height_in_pixel =
				spec_size_info->stepwise.step_height;
			bitsPP = 8;
			saligned = 6;
		}

		// Compute AFBC stream data size
		if (pix_fmt_mp->pixelformat == V4L2_PIX_FMT_RGB32_AFBC ||
		pix_fmt_mp->pixelformat == V4L2_PIX_FMT_BGR32_AFBC ||
		pix_fmt_mp->pixelformat == V4L2_PIX_FMT_RGBA1010102_AFBC ||
		pix_fmt_mp->pixelformat == V4L2_PIX_FMT_BGRA1010102_AFBC) {
			step_width_in_pixel = 1;
			step_height_in_pixel = 1;
			block_w = 32;
			block_h = 8;
			bitsPP = 32;
			saligned = 4;
		} else if (pix_fmt_mp->pixelformat == V4L2_PIX_FMT_NV12_AFBC ||
		pix_fmt_mp->pixelformat == V4L2_PIX_FMT_NV21_AFBC) {
			step_width_in_pixel = 1;
			step_height_in_pixel = 1;
			block_w = 16;
			block_h = 16;
			bitsPP = 12;
			saligned = 4;
		} else if (pix_fmt_mp->pixelformat ==
				V4L2_PIX_FMT_NV12_10B_AFBC) {
			step_width_in_pixel = 1;
			step_height_in_pixel = 1;
			block_w = 16;
			block_h = 16;
			bitsPP = 16;
			saligned = 4;
		}

		/* pix_fmt_mp->width and pix_fmt_mp->height is buffer size or align size, here just
		 * calculate new size with request alignment, and only make sure new size don't bigger
		 * than width_max, so that some special case could encode normal, such as real size is
		 * under spec but buffer size is over spec.
		 * overspec check will be handled in vb2ops_venc_start_streaming with real size.
		 */
		org_w = pix_fmt_mp->width;
		org_h = pix_fmt_mp->height;
		v4l_bound_align_image(&pix_fmt_mp->width,
			spec_size_info->stepwise.min_width,
			spec_size_info->stepwise.max_width,
			log2_enc(step_width_in_pixel),
			&pix_fmt_mp->height,
			spec_size_info->stepwise.min_height,
			spec_size_info->stepwise.max_width,
			log2_enc(step_height_in_pixel),
			saligned);
		if (pix_fmt_mp->width < org_w &&
			(pix_fmt_mp->width + step_width_in_pixel) <=
			spec_size_info->stepwise.max_width)
			pix_fmt_mp->width += step_width_in_pixel;
		if (pix_fmt_mp->height < org_h &&
			(pix_fmt_mp->height + step_height_in_pixel) <=
			spec_size_info->stepwise.max_width)
			pix_fmt_mp->height += step_height_in_pixel;


		pix_fmt_mp->num_planes = fmt->num_planes;
		imagePixels = pix_fmt_mp->width * pix_fmt_mp->height;

		if (pix_fmt_mp->pixelformat == V4L2_PIX_FMT_ABGR32 ||
			pix_fmt_mp->pixelformat == V4L2_PIX_FMT_ARGB32 ||
			pix_fmt_mp->pixelformat == V4L2_PIX_FMT_RGB32 ||
			pix_fmt_mp->pixelformat == V4L2_PIX_FMT_RGBA32 ||
			pix_fmt_mp->pixelformat == V4L2_PIX_FMT_BGR32 ||
		pix_fmt_mp->pixelformat == V4L2_PIX_FMT_ARGB1010102 ||
		pix_fmt_mp->pixelformat == V4L2_PIX_FMT_ABGR1010102 ||
		pix_fmt_mp->pixelformat == V4L2_PIX_FMT_RGBA1010102 ||
		pix_fmt_mp->pixelformat == V4L2_PIX_FMT_BGRA1010102 ||
			pix_fmt_mp->pixelformat == V4L2_PIX_FMT_RGB24 ||
			pix_fmt_mp->pixelformat == V4L2_PIX_FMT_BGR24) {
			pix_fmt_mp->plane_fmt[0].sizeimage =
				imagePixels * bitsPP / 8;
			pix_fmt_mp->plane_fmt[0].bytesperline =
			pix_fmt_mp->width * bitsPP / 8;
			pix_fmt_mp->num_planes = 1U;
		} else if (pix_fmt_mp->pixelformat == V4L2_PIX_FMT_RGB32_AFBC ||
		pix_fmt_mp->pixelformat == V4L2_PIX_FMT_BGR32_AFBC ||
		pix_fmt_mp->pixelformat == V4L2_PIX_FMT_RGBA1010102_AFBC ||
		pix_fmt_mp->pixelformat == V4L2_PIX_FMT_BGRA1010102_AFBC ||
		pix_fmt_mp->pixelformat == V4L2_PIX_FMT_NV12_AFBC ||
		pix_fmt_mp->pixelformat == V4L2_PIX_FMT_NV21_AFBC ||
		pix_fmt_mp->pixelformat == V4L2_PIX_FMT_NV12_10B_AFBC) {
			block_count =
			((pix_fmt_mp->width + (block_w - 1))/block_w)
			*((pix_fmt_mp->height + (block_h - 1))/block_h);

			pix_fmt_mp->plane_fmt[0].sizeimage =
			(block_count << 4) +
			(block_count * block_w * block_h * bitsPP / 8);
		mtk_v4l2_debug(0, "AFBC size:%d superblock(%dx%d) superblock_count(%d)\n",
		    pix_fmt_mp->plane_fmt[0].sizeimage,
		    block_w,
		    block_h,
		    block_count);
		} else if (pix_fmt_mp->num_planes == 1U) {
			pix_fmt_mp->plane_fmt[0].sizeimage =
				(imagePixels * bitsPP / 8) +
				(imagePixels * bitsPP / 8) / 2;
			pix_fmt_mp->plane_fmt[0].bytesperline =
				pix_fmt_mp->width * bitsPP / 8;
		} else if (pix_fmt_mp->num_planes == 2U) {
			pix_fmt_mp->plane_fmt[0].sizeimage =
				imagePixels * bitsPP / 8;
			pix_fmt_mp->plane_fmt[0].bytesperline =
				pix_fmt_mp->width * bitsPP / 8;
			pix_fmt_mp->plane_fmt[1].sizeimage =
				(imagePixels * bitsPP / 8) / 2;
			pix_fmt_mp->plane_fmt[1].bytesperline =
				pix_fmt_mp->width * bitsPP / 8;
		} else if (pix_fmt_mp->num_planes == 3U) {
			pix_fmt_mp->plane_fmt[0].sizeimage =
				imagePixels * bitsPP / 8;
			pix_fmt_mp->plane_fmt[0].bytesperline =
				pix_fmt_mp->width * bitsPP / 8;
			pix_fmt_mp->plane_fmt[1].sizeimage =
				(imagePixels * bitsPP / 8) / 4;
			pix_fmt_mp->plane_fmt[1].bytesperline =
				pix_fmt_mp->width * bitsPP / 8 / 2;
			pix_fmt_mp->plane_fmt[2].sizeimage =
				(imagePixels * bitsPP / 8) / 4;
			pix_fmt_mp->plane_fmt[2].bytesperline =
				pix_fmt_mp->width * bitsPP / 8 / 2;
		} else
			mtk_v4l2_err("Unsupport num planes = %d\n",
				     pix_fmt_mp->num_planes);

		mtk_v4l2_debug(0,
			       "w/h (%d, %d) -> (%d,%d), sizeimage[%d,%d,%d]",
			       org_w, org_h,
			       pix_fmt_mp->width, pix_fmt_mp->height,
			       pix_fmt_mp->plane_fmt[0].sizeimage,
			       pix_fmt_mp->plane_fmt[1].sizeimage,
			       pix_fmt_mp->plane_fmt[2].sizeimage);
	}

	for (i = 0; i < pix_fmt_mp->num_planes; i++)
		memset(&(pix_fmt_mp->plane_fmt[i].reserved[0]), 0x0,
		       sizeof(pix_fmt_mp->plane_fmt[0].reserved));

	pix_fmt_mp->flags = 0;
	memset(&pix_fmt_mp->reserved, 0x0,
	       sizeof(pix_fmt_mp->reserved));

	return 0;
}

static void mtk_venc_set_param(struct mtk_vcodec_ctx *ctx,
			       struct venc_enc_param *param)
{
	struct mtk_q_data *q_data_src = &ctx->q_data[MTK_Q_DATA_SRC];
	struct mtk_enc_params *enc_params = &ctx->enc_params;

	switch (q_data_src->fmt->fourcc) {
	case V4L2_PIX_FMT_YUV420M:
	case V4L2_PIX_FMT_YUV420:
		param->input_yuv_fmt = VENC_YUV_FORMAT_I420;
		break;
	case V4L2_PIX_FMT_YVU420M:
	case V4L2_PIX_FMT_YVU420:
		param->input_yuv_fmt = VENC_YUV_FORMAT_YV12;
		break;
	case V4L2_PIX_FMT_NV12M:
	case V4L2_PIX_FMT_NV12:
		param->input_yuv_fmt = VENC_YUV_FORMAT_NV12;
		break;
	case V4L2_PIX_FMT_NV21M:
	case V4L2_PIX_FMT_NV21:
		param->input_yuv_fmt = VENC_YUV_FORMAT_NV21;
		break;
	case V4L2_PIX_FMT_RGB24:
		param->input_yuv_fmt = VENC_YUV_FORMAT_24bitRGB888;
		break;
	case V4L2_PIX_FMT_BGR24:
		param->input_yuv_fmt = VENC_YUV_FORMAT_24bitBGR888;
		break;
	case V4L2_PIX_FMT_ARGB32:
		param->input_yuv_fmt = VENC_YUV_FORMAT_32bitARGB8888;
		break;
	case V4L2_PIX_FMT_ABGR32:
		param->input_yuv_fmt = VENC_YUV_FORMAT_32bitBGRA8888;
		break;
	case V4L2_PIX_FMT_BGR32:
		param->input_yuv_fmt = VENC_YUV_FORMAT_32bitABGR8888;
		break;
	case V4L2_PIX_FMT_RGB32:
	case V4L2_PIX_FMT_RGBA32:
		param->input_yuv_fmt = VENC_YUV_FORMAT_32bitRGBA8888;
		break;
	case V4L2_PIX_FMT_ARGB1010102:
		param->input_yuv_fmt = VENC_YUV_FORMAT_32bitARGB1010102;
		break;
	case V4L2_PIX_FMT_ABGR1010102:
		param->input_yuv_fmt = VENC_YUV_FORMAT_32bitABGR1010102;
		break;
	case V4L2_PIX_FMT_RGBA1010102:
		param->input_yuv_fmt = VENC_YUV_FORMAT_32bitRGBA1010102;
		break;
	case V4L2_PIX_FMT_BGRA1010102:
		param->input_yuv_fmt = VENC_YUV_FORMAT_32bitBGRA1010102;
		break;
	case V4L2_PIX_FMT_MT10:
	case V4L2_PIX_FMT_MT10S:
		param->input_yuv_fmt = VENC_YUV_FORMAT_MT10;
		break;
	case V4L2_PIX_FMT_P010M:
	case V4L2_PIX_FMT_P010S:
		param->input_yuv_fmt = VENC_YUV_FORMAT_P010;
		break;
	case V4L2_PIX_FMT_RGB32_AFBC:
		param->input_yuv_fmt = VENC_YUV_FORMAT_32bitRGBA8888_AFBC;
		break;
	case V4L2_PIX_FMT_BGR32_AFBC:
		param->input_yuv_fmt = VENC_YUV_FORMAT_32bitBGRA8888_AFBC;
		break;
	case V4L2_PIX_FMT_RGBA1010102_AFBC:
		param->input_yuv_fmt = VENC_YUV_FORMAT_32bitRGBA1010102_AFBC;
		break;
	case V4L2_PIX_FMT_BGRA1010102_AFBC:
		param->input_yuv_fmt = VENC_YUV_FORMAT_32bitBGRA1010102_AFBC;
		break;
	case V4L2_PIX_FMT_NV12_AFBC:
		param->input_yuv_fmt = VENC_YUV_FORMAT_NV12_AFBC;
		break;
	case V4L2_PIX_FMT_NV21_AFBC:
		param->input_yuv_fmt = VENC_YUV_FORMAT_NV21_AFBC;
		break;
	case V4L2_PIX_FMT_NV12_10B_AFBC:
		param->input_yuv_fmt = VENC_YUV_FORMAT_NV12_10B_AFBC;
		break;

	default:
		mtk_v4l2_err("Unsupport fourcc =0x%x default use I420",
			q_data_src->fmt->fourcc);
		param->input_yuv_fmt = VENC_YUV_FORMAT_I420;
		break;
	}
	param->profile = enc_params->profile;
	param->level = enc_params->level;
	param->tier = enc_params->tier;

	/* Config visible resolution */
	param->width = q_data_src->visible_width;
	param->height = q_data_src->visible_height;
	/* Config coded resolution */
	param->buf_width = q_data_src->coded_width;
	param->buf_height = q_data_src->coded_height;
	param->frm_rate = enc_params->framerate_num /
			  enc_params->framerate_denom;
	param->intra_period = enc_params->intra_period;
	param->gop_size = enc_params->gop_size;
	param->bitrate = enc_params->bitrate;
	param->operationrate = enc_params->operationrate;
	param->scenario = enc_params->scenario;
	param->prependheader = enc_params->prependheader;
	param->bitratemode = enc_params->bitratemode;
	param->roion = enc_params->roion;
	param->heif_grid_size = enc_params->heif_grid_size;
	// will copy to vsi, pass after streamon
	param->color_desc = &enc_params->color_desc;
	param->max_w = enc_params->max_w;
	param->max_h = enc_params->max_h;
	param->num_b_frame = enc_params->num_b_frame;
	param->slbc_ready = ctx->use_slbc;
	param->slbc_addr = ctx->slbc_addr;
	param->i_qp = enc_params->i_qp;
	param->p_qp = enc_params->p_qp;
	param->b_qp = enc_params->b_qp;
	param->svp_mode = enc_params->svp_mode;
	param->tsvc = enc_params->tsvc;
	param->highquality = enc_params->highquality;
	param->dummynal = enc_params->dummynal;
	param->lowlatencywfd = enc_params->lowlatencywfd;
	param->slice_count = enc_params->slice_count;

	param->max_qp = enc_params->max_qp;
	param->min_qp = enc_params->min_qp;
	param->framelvl_qp = enc_params->framelvl_qp;
	param->ip_qpdelta = enc_params->ip_qpdelta;
	param->qp_control_mode = enc_params->qp_control_mode;

	param->hier_ref_layer = enc_params->hier_ref_layer;
	param->hier_ref_type = enc_params->hier_ref_type;
	param->temporal_layer_pcount = enc_params->temporal_layer_pcount;
	param->temporal_layer_bcount = enc_params->temporal_layer_bcount;
	param->max_ltr_num = enc_params->max_ltr_num;
	param->slice_header_spacing = enc_params->slice_header_spacing;
	param->multi_ref = &enc_params->multi_ref;
	param->vui_info = &enc_params->vui_info;
	param->ctx_id = ctx->id;
	param->priority = ctx->enc_params.priority;
	param->codec_fmt = ctx->q_data[MTK_Q_DATA_DST].fmt->fourcc;

	param->qpvbr_upper_enable = enc_params->qpvbr_upper_enable;
	param->qpvbr_qp_upper_threshold = enc_params->qpvbr_qp_upper_threshold;
	param->qpvbr_qp_max_brratio = enc_params->qpvbr_qp_max_brratio;
	param->qpvbr_lower_enable = enc_params->qpvbr_lower_enable;
	param->qpvbr_qp_lower_threshold = enc_params->qpvbr_qp_lower_threshold;
	param->qpvbr_qp_min_brratio = enc_params->qpvbr_qp_min_brratio;
	param->cb_qp_offset = enc_params->cb_qp_offset;
	param->cr_qp_offset = enc_params->cr_qp_offset;
	param->mbrc_tk_spd = enc_params->mbrc_tk_spd;
	param->ifrm_q_ltr = enc_params->ifrm_q_ltr;
	param->pfrm_q_ltr = enc_params->pfrm_q_ltr;
	param->bfrm_q_ltr = enc_params->bfrm_q_ltr;
	param->visual_quality = &enc_params->visual_quality;
	param->init_qp = &enc_params->init_qp;
	param->frame_qp_range = &enc_params->frame_qp_range;
	param->nal_length = &enc_params->nal_length;
	param->mlvec_mode = enc_params->mlvec_mode;
}

static int vidioc_venc_subscribe_evt(struct v4l2_fh *fh,
	const struct v4l2_event_subscription *sub)
{
	switch (sub->type) {
	case V4L2_EVENT_EOS:
		return v4l2_event_subscribe(fh, sub, 2, NULL);
	case V4L2_EVENT_MTK_VENC_ERROR:
		return v4l2_event_subscribe(fh, sub, 0, NULL);
	default:
		return v4l2_ctrl_subscribe_event(fh, sub);
	}
}

static void mtk_vdec_queue_stop_enc_event(struct mtk_vcodec_ctx *ctx)
{
	static const struct v4l2_event ev_eos = {
		.type = V4L2_EVENT_EOS,
	};

	mtk_v4l2_debug(0, "[%d]", ctx->id);
	v4l2_event_queue_fh(&ctx->fh, &ev_eos);
}

void mtk_venc_queue_error_event(struct mtk_vcodec_ctx *ctx)
{
	static struct v4l2_event ev_error = {
		.type = V4L2_EVENT_MTK_VENC_ERROR,
	};
	if  (ctx->err_msg)
		memcpy((void *)ev_error.u.data, &ctx->err_msg, sizeof(ctx->err_msg));

	mtk_v4l2_debug(0, "[%d] msg %x", ctx->id, ctx->err_msg);
	v4l2_event_queue_fh(&ctx->fh, &ev_error);
}

static void mtk_venc_error_handle(struct mtk_vcodec_ctx *ctx)
{
	mtk_vcodec_set_state(ctx, MTK_STATE_ABORT);
	mutex_lock(&ctx->dev->enc_dvfs_mutex);
	mtk_vcodec_cpu_adaptive_ctrl(ctx, false);
	mutex_unlock(&ctx->dev->enc_dvfs_mutex);
	mtk_venc_queue_error_event(ctx);
}

static int vidioc_venc_s_fmt_cap(struct file *file, void *priv,
				 struct v4l2_format *f)
{
	struct mtk_vcodec_ctx *ctx = fh_to_ctx(priv);
	struct vb2_queue *vq;
	struct mtk_q_data *q_data;
	int i, ret;
	struct mtk_video_fmt *fmt;

	if (IS_ERR_OR_NULL(f)) {
		mtk_v4l2_err("fail to get v4l2_format");
		return -EINVAL;
	}

	mtk_v4l2_debug(4, "[%d] type %d", ctx->id, f->type);
	vq = v4l2_m2m_get_vq(ctx->m2m_ctx, f->type);
	if (!vq) {
		mtk_v4l2_err("fail to get vq");
		return -EINVAL;
	}

	if (vb2_is_busy(vq)) {
		mtk_v4l2_err("queue busy");
		return -EBUSY;
	}

	q_data = mtk_venc_get_q_data(ctx, f->type);
	if (!q_data) {
		mtk_v4l2_err("fail to get q data");
		return -EINVAL;
	}

	fmt = mtk_venc_find_format(f, MTK_FMT_ENC);
	if (!fmt) {
		f->fmt.pix.pixelformat =
			mtk_venc_formats[default_cap_fmt_idx].fourcc;
		fmt = mtk_venc_find_format(f, MTK_FMT_ENC);
	}
	if (fmt == NULL) {
		mtk_v4l2_err("fail to get fmt");
		return -EINVAL;
	}

	q_data->fmt = fmt;
	ret = vidioc_try_fmt(f, q_data->fmt, ctx);
	if (ret)
		return ret;

	q_data->coded_width = f->fmt.pix_mp.width;
	q_data->coded_height = f->fmt.pix_mp.height;
	q_data->field = f->fmt.pix_mp.field;

	for (i = 0; i < f->fmt.pix_mp.num_planes; i++) {
		struct v4l2_plane_pix_format    *plane_fmt;

		plane_fmt = &f->fmt.pix_mp.plane_fmt[i];
		q_data->bytesperline[i] = plane_fmt->bytesperline;
		q_data->sizeimage[i] = plane_fmt->sizeimage;
	}

	mutex_lock(&ctx->init_lock);
	if (mtk_vcodec_is_state(ctx, MTK_STATE_FREE)) {
		ret = venc_if_init(ctx, q_data->fmt->fourcc);
		if (ret) {
			mtk_v4l2_err("venc_if_init failed=%d, codec type=%s(0x%x)",
				ret, FOURCC_STR(q_data->fmt->fourcc), q_data->fmt->fourcc);
			mtk_venc_error_handle(ctx);
			mutex_unlock(&ctx->init_lock);
			return -EBUSY;
		}
		mtk_vcodec_set_state_from(ctx, MTK_STATE_INIT, MTK_STATE_FREE);
	}
	// format change, trigger encode header
	mtk_vcodec_set_state_from(ctx, MTK_STATE_INIT, MTK_STATE_STOP);
	mutex_unlock(&ctx->init_lock);

	return 0;
}

static int vidioc_venc_s_fmt_out(struct file *file, void *priv,
				 struct v4l2_format *f)
{
	struct mtk_vcodec_ctx *ctx = fh_to_ctx(priv);
	struct vb2_queue *vq;
	struct mtk_q_data *q_data;
	int ret, i;
	struct mtk_video_fmt *fmt;

	if (IS_ERR_OR_NULL(f)) {
		mtk_v4l2_err("fail to get v4l2_format");
		return -EINVAL;
	}

	mtk_v4l2_debug(4, "[%d] type %d", ctx->id, f->type);
	vq = v4l2_m2m_get_vq(ctx->m2m_ctx, f->type);
	if (!vq) {
		mtk_v4l2_err("fail to get vq");
		return -EINVAL;
	}

	if (vb2_is_busy(vq)) {
		mtk_v4l2_err("queue busy");
		return -EBUSY;
	}

	q_data = mtk_venc_get_q_data(ctx, f->type);
	if (!q_data) {
		mtk_v4l2_err("fail to get q data");
		return -EINVAL;
	}

	fmt = mtk_venc_find_format(f, MTK_FMT_FRAME);
	if (!fmt) {
		f->fmt.pix.pixelformat =
			mtk_venc_formats[default_out_fmt_idx].fourcc;
		fmt = mtk_venc_find_format(f, MTK_FMT_FRAME);
	}

	q_data->visible_width = f->fmt.pix_mp.width;
	q_data->visible_height = f->fmt.pix_mp.height;
	q_data->fmt = fmt;
	ret = vidioc_try_fmt(f, q_data->fmt, ctx);
	if (ret)
		return ret;

	q_data->coded_width = f->fmt.pix_mp.width;
	q_data->coded_height = f->fmt.pix_mp.height;

	q_data->field = f->fmt.pix_mp.field;
	ctx->colorspace = f->fmt.pix_mp.colorspace;
	ctx->ycbcr_enc = f->fmt.pix_mp.ycbcr_enc;
	ctx->quantization = f->fmt.pix_mp.quantization;
	ctx->xfer_func = f->fmt.pix_mp.xfer_func;

	for (i = 0; i < f->fmt.pix_mp.num_planes; i++) {
		struct v4l2_plane_pix_format *plane_fmt;

		plane_fmt = &f->fmt.pix_mp.plane_fmt[i];
		q_data->bytesperline[i] = plane_fmt->bytesperline;
		q_data->sizeimage[i] = plane_fmt->sizeimage;
	}

	return 0;
}

static int vidioc_venc_g_fmt(struct file *file, void *priv,
			     struct v4l2_format *f)
{
	struct v4l2_pix_format_mplane *pix = &f->fmt.pix_mp;
	struct mtk_vcodec_ctx *ctx = fh_to_ctx(priv);
	struct vb2_queue *vq;
	struct mtk_q_data *q_data;
	int i;

	if (IS_ERR_OR_NULL(f)) {
		mtk_v4l2_err("fail to get v4l2_format");
		return -EINVAL;
	}

	vq = v4l2_m2m_get_vq(ctx->m2m_ctx, f->type);
	if (!vq)
		return -EINVAL;

	q_data = mtk_venc_get_q_data(ctx, f->type);

	pix->width = q_data->coded_width;
	pix->height = q_data->coded_height;
	pix->pixelformat = q_data->fmt->fourcc;
	pix->field = q_data->field;
	pix->num_planes = q_data->fmt->num_planes;
	for (i = 0; i < pix->num_planes; i++) {
		pix->plane_fmt[i].bytesperline = q_data->bytesperline[i];
		pix->plane_fmt[i].sizeimage = q_data->sizeimage[i];
		memset(&(pix->plane_fmt[i].reserved[0]), 0x0,
		       sizeof(pix->plane_fmt[i].reserved));
	}

	pix->flags = 0;
	pix->colorspace = ctx->colorspace;
	pix->ycbcr_enc = ctx->ycbcr_enc;
	pix->quantization = ctx->quantization;
	pix->xfer_func = ctx->xfer_func;
	mtk_v4l2_debug(4, "[%d] type %d", ctx->id, f->type);

	return 0;
}

static int vidioc_try_fmt_vid_cap_mplane(struct file *file, void *priv,
					 struct v4l2_format *f)
{
	struct mtk_video_fmt *fmt;
	struct mtk_vcodec_ctx *ctx = fh_to_ctx(priv);

	if (IS_ERR_OR_NULL(f)) {
		mtk_v4l2_err("fail to get v4l2_format");
		return -EINVAL;
	}

	fmt = mtk_venc_find_format(f, MTK_FMT_ENC);
	if (!fmt) {
		f->fmt.pix.pixelformat =
			mtk_venc_formats[default_cap_fmt_idx].fourcc;
		fmt = mtk_venc_find_format(f, MTK_FMT_ENC);
	}
	if (fmt == NULL) {
		mtk_v4l2_err("fail to get fmt");
		return -EINVAL;
	}

	f->fmt.pix_mp.colorspace = ctx->colorspace;
	f->fmt.pix_mp.ycbcr_enc = ctx->ycbcr_enc;
	f->fmt.pix_mp.quantization = ctx->quantization;
	f->fmt.pix_mp.xfer_func = ctx->xfer_func;

	return vidioc_try_fmt(f, fmt, ctx);
}

static int vidioc_try_fmt_vid_out_mplane(struct file *file, void *priv,
					 struct v4l2_format *f)
{
	struct mtk_video_fmt *fmt;
	struct mtk_vcodec_ctx *ctx = fh_to_ctx(priv);

	if (IS_ERR_OR_NULL(f)) {
		mtk_v4l2_err("fail to get v4l2_format");
		return -EINVAL;
	}

	fmt = mtk_venc_find_format(f, MTK_FMT_FRAME);
	if (!fmt) {
		f->fmt.pix.pixelformat =
			mtk_venc_formats[default_out_fmt_idx].fourcc;
		fmt = mtk_venc_find_format(f, MTK_FMT_FRAME);
	}
	if (fmt == NULL) {
		mtk_v4l2_err("fail to get fmt");
		return -EINVAL;
	}

	if (!f->fmt.pix_mp.colorspace) {
		f->fmt.pix_mp.colorspace = V4L2_COLORSPACE_REC709;
		f->fmt.pix_mp.ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
		f->fmt.pix_mp.quantization = V4L2_QUANTIZATION_DEFAULT;
		f->fmt.pix_mp.xfer_func = V4L2_XFER_FUNC_DEFAULT;
	}

	return vidioc_try_fmt(f, fmt, ctx);
}

static int vidioc_venc_g_selection(struct file *file, void *priv,
				   struct v4l2_selection *s)
{
	struct mtk_vcodec_ctx *ctx = fh_to_ctx(priv);
	struct mtk_q_data *q_data;

	if (!V4L2_TYPE_IS_OUTPUT(s->type))
		return -EINVAL;

	if (s->target != V4L2_SEL_TGT_COMPOSE &&
	    s->target != V4L2_SEL_TGT_CROP)
		return -EINVAL;

	q_data = mtk_venc_get_q_data(ctx, s->type);
	if (!q_data)
		return -EINVAL;

	s->r.top = 0;
	s->r.left = 0;
	s->r.width = q_data->visible_width;
	s->r.height = q_data->visible_height;

	return 0;
}

static int vidioc_venc_s_selection(struct file *file, void *priv,
				   struct v4l2_selection *s)
{
	struct mtk_vcodec_ctx *ctx = fh_to_ctx(priv);
	struct mtk_q_data *q_data;


	if (!V4L2_TYPE_IS_OUTPUT(s->type))
		return -EINVAL;

	if (s->target != V4L2_SEL_TGT_COMPOSE &&
	    s->target != V4L2_SEL_TGT_CROP)
		return -EINVAL;

	q_data = mtk_venc_get_q_data(ctx, s->type);
	if (!q_data)
		return -EINVAL;

	s->r.top = 0;
	s->r.left = 0;
	q_data->visible_width = s->r.width;
	q_data->visible_height = s->r.height;

	return 0;
}

static int vidioc_venc_qbuf(struct file *file, void *priv,
			    struct v4l2_buffer *buf)
{
	struct mtk_vcodec_ctx *ctx = fh_to_ctx(priv);
	struct vb2_queue *vq;
	struct vb2_buffer *vb;
	struct mtk_video_enc_buf *mtkbuf;
	struct vb2_v4l2_buffer *vb2_v4l2;
	struct dma_buf *dmabuf;

	if (mtk_vcodec_is_state(ctx, MTK_STATE_ABORT)) {
		mtk_v4l2_err("[%d] Call on QBUF after unrecoverable error",
			     ctx->id);
		return -EIO;
	}

	// Check if need to proceed cache operations
	vq = v4l2_m2m_get_vq(ctx->m2m_ctx, buf->type);
	if (!vq) {
		mtk_v4l2_err("fail to get vq");
		return -EINVAL;
	}
	if (buf->index >= vq->num_buffers) {
		mtk_v4l2_err("[%d] buffer index %d out of range %d",
			ctx->id, buf->index, vq->num_buffers);
		return -EINVAL;
	}
	if (IS_ERR_OR_NULL(buf->m.planes) || buf->length == 0) {
		mtk_v4l2_err("[%d] buffer index %d planes address %p 0x%llx or length %d invalid",
			ctx->id, buf->index, buf->m.planes,
			(unsigned long long)buf->m.planes, buf->length);
		return -EINVAL;
	}
	vb = vq->bufs[buf->index];
	vb2_v4l2 = container_of(vb, struct vb2_v4l2_buffer, vb2_buf);
	mtkbuf = container_of(vb2_v4l2, struct mtk_video_enc_buf, vb);

	if (buf->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		if (!ctx->has_first_input) {
#if IS_ENABLED(CONFIG_MTK_TINYSYS_VCP_SUPPORT)
			if (!ctx->enc_params.svp_mode && ctx->dev->support_acp) {
				bool is_cached = false;

				if (vq->memory == VB2_MEMORY_DMABUF) {
					struct dma_buf *dmabuf = dma_buf_get(buf->m.planes[0].m.fd);

					if (!IS_ERR_OR_NULL(dmabuf)) {
						is_cached = !is_uncached_dmabuf(dmabuf);
						dma_buf_put(dmabuf);
					}
				}
				if (!is_cached)
					mtk_v4l2_debug(2, "[%d] src_vq use uncached heap or vq->memory %d not DMABUF",
						ctx->id, vq->memory);
				if (mtk_venc_input_acp_enable && is_cached &&
				    !(buf->flags & V4L2_BUF_FLAG_NO_CACHE_CLEAN) &&
				    vcp_get_io_device_ex(VCP_IOMMU_ACP_CODEC)) {
					vq->dev = vcp_get_io_device_ex(VCP_IOMMU_ACP_CODEC);
					mtk_v4l2_debug(2, "[%d] src_vq use VCP_IOMMU_ACP_CODEC domain %p",
						ctx->id, vq->dev);
				} else if (vq->dev != ctx->dev->smmu_dev) {
					vq->dev = ctx->dev->smmu_dev;
					mtk_v4l2_debug(4, "[%d] src_vq use smmu_dev domain %p", ctx->id, vq->dev);
				}
			}
#endif
			ctx->has_first_input = true;
		}
		if (buf->m.planes[0].bytesused == 0) {
			mtkbuf->lastframe = EOS;
			mtk_v4l2_debug(1, "[%d] index=%d Eos FB(%d,%d) vb=%p pts=%llu",
				ctx->id, buf->index,
				buf->bytesused,
				buf->length, vb, vb->timestamp);
		} else if (buf->flags & V4L2_BUF_FLAG_LAST) {
			mtkbuf->lastframe = EOS_WITH_DATA;
			mtk_v4l2_debug(1, "[%d] id=%d EarlyEos FB(%d,%d) vb=%p pts=%llu",
				ctx->id, buf->index, buf->m.planes[0].bytesused,
				buf->length, vb, vb->timestamp);
		} else {
			mtkbuf->lastframe = NON_EOS;
			mtk_v4l2_debug(1, "[%d] id=%d getdata FB(%d,%d) vb=%p pts=%llu ",
				ctx->id, buf->index,
				buf->m.planes[0].bytesused,
				buf->length, mtkbuf, vb->timestamp);
		}
	} else {
		if (buf->reserved == 0xFFFFFFFF || buf->reserved == 0)
			mtkbuf->general_user_fd = -1;
		else
			mtkbuf->general_user_fd = (int)buf->reserved;

		mtk_v4l2_debug(1, "[%d] id=%d BS (%d) vb=%p, general_buf_fd=%d, mtkbuf->general_user_fd = %d",
				ctx->id, buf->index,
				buf->length, mtkbuf,
				buf->reserved, mtkbuf->general_user_fd);
	}

	if (buf->flags & V4L2_BUF_FLAG_NO_CACHE_CLEAN) {
		mtk_v4l2_debug(4, "[%d] No need for Cache clean, buf->index:%d. mtkbuf:%p",
		   ctx->id, buf->index, mtkbuf);
		mtkbuf->flags |= NO_CAHCE_CLEAN;
	}

	if (buf->flags & V4L2_BUF_FLAG_NO_CACHE_INVALIDATE) {
		mtk_v4l2_debug(4, "[%d] No need for Cache invalidate, buf->index:%d. mtkbuf:%p",
		   ctx->id, buf->index, mtkbuf);
		mtkbuf->flags |= NO_CAHCE_INVALIDATE;
	}

	mtkbuf->frm_buf.has_qpmap = 0;
	mtkbuf->frm_buf.has_meta = 0;
	mtkbuf->frm_buf.qpmap_dma = 0;
	mtkbuf->frm_buf.metabuffer_dma = 0;
	mtkbuf->frm_buf.dyparams_dma = 0;

	if (buf->flags & V4L2_BUF_FLAG_QP_META &&
		buf->reserved > 0 &&
		buf->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		struct device *dev = NULL;

		dmabuf = dma_buf_get(buf->reserved);
		if (IS_ERR(dmabuf)) {
			mtk_v4l2_err("%s qpmap_dma is err 0x%lx\n", __func__, PTR_ERR(dmabuf));
			mtk_venc_queue_error_event(ctx);
			return -EINVAL;
		}
		mtkbuf->frm_buf.qpmap_dma = dmabuf;

		dev = ctx->m2m_ctx->cap_q_ctx.q.dev;
		/* use vcp & vcu compatible access device */

		if (mtk_vcodec_dma_attach_map(dev, dmabuf, &mtkbuf->frm_buf.qpmap_dma_att, &mtkbuf->frm_buf.qpmap_sgt,
			&mtkbuf->frm_buf.qpmap_dma_addr, DMA_TO_DEVICE, __func__, __LINE__))
			return -EINVAL;

		mtkbuf->frm_buf.has_qpmap = 1;
		mtk_v4l2_debug(1, "[%d] Have Qpmap fd, buf->index:%d, qpmap_dma:%p, fd:%u",
			ctx->id, buf->index, mtkbuf->frm_buf.qpmap_dma, buf->reserved);
	}

	if (buf->flags & V4L2_BUF_FLAG_HAS_META &&
		buf->reserved > 0 &&
		buf->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		struct dma_buf_attachment *meta_buf_att;
		struct sg_table *meta_sgt;
		struct metadata_info *meta_info;
		struct iosys_map meta_map;
		int index = 0;
		struct meta_describe meta_desc;
		struct device *dev = NULL;

		dmabuf = dma_buf_get(buf->reserved);
		if (IS_ERR(dmabuf)) {
			mtk_v4l2_err("%s metabuffer_dma is err 0x%lx\n", __func__, PTR_ERR(dmabuf));
			mtk_venc_queue_error_event(ctx);
			return -EINVAL;
		}

		dev = ctx->m2m_ctx->cap_q_ctx.q.dev;
		/* use vcp & vcu compatible access device */

		if (mtk_vcodec_dma_attach_map(dev, dmabuf, &meta_buf_att, &meta_sgt,
			&mtkbuf->frm_buf.metabuffer_addr, DMA_TO_DEVICE, __func__, __LINE__)) {
			dma_buf_put(dmabuf);
			return -EINVAL;
		}

		//check required size before doing va mapping
		if (dmabuf->size < sizeof(struct metadata_info)) {
			mtk_v4l2_err("V4L2_BUF_FLAG_HAS_META dma size check failed");
			mtk_vcodec_dma_unmap_detach(dmabuf, &meta_buf_att, &meta_sgt, DMA_TO_DEVICE);
			dma_buf_put(dmabuf);
			return -EINVAL;
		}

		if (!dma_buf_vmap_unlocked(dmabuf, &meta_map)) {
			meta_info = (struct metadata_info *)meta_map.vaddr;
		} else {
			mtk_v4l2_err("V4L2_BUF_FLAG_HAS_META meta_va is NULL");
			mtk_vcodec_dma_unmap_detach(dmabuf, &meta_buf_att, &meta_sgt, DMA_TO_DEVICE);
			dma_buf_put(dmabuf);
			return -EINVAL;
		}
		mtkbuf->frm_buf.metabuffer_dma = dmabuf;
		mtk_v4l2_debug(2, "V4L2_BUF_FLAG_HAS_META  buf->reserved:%d dma_buf=%p, DMA=%lx",
			buf->reserved, dmabuf, (unsigned long)mtkbuf->frm_buf.metabuffer_addr);

		for (; index < MTK_MAX_METADATA_NUM; index++) {
			memset(&meta_desc, 0, sizeof(meta_desc));
			meta_desc.invalid = meta_info->metadata_dsc[index].invalid;
			if (!meta_desc.invalid)
				break;

			meta_desc.fd_flag = meta_info->metadata_dsc[index].fd_flag;
			meta_desc.type = meta_info->metadata_dsc[index].type;
			meta_desc.size = meta_info->metadata_dsc[index].size;
			meta_desc.value = meta_info->metadata_dsc[index].value;

			mtk_v4l2_debug(1, "meta data info,index:%d fd_flag:%u type:%u size:%u val:%u)",
				index, meta_desc.fd_flag, meta_desc.type,
				meta_desc.size, meta_desc.value);

			if (meta_desc.type == METADATA_QPMAP && !meta_desc.fd_flag) {
				mtk_v4l2_err("qpmap should provide buffer fd");
				continue;
			} else if ((meta_desc.type == METADATA_HDR ||
						meta_desc.type == METADATA_DYNAMICPARAM) &&
						meta_desc.fd_flag) {
				mtk_v4l2_err("hdr should not provide buffer fd");
				continue;
			}

			if (meta_desc.fd_flag) {
				if (meta_desc.type != METADATA_QPMAP)
					continue;

				dmabuf = dma_buf_get(meta_desc.value);
				if (IS_ERR(dmabuf)) {
					mtk_v4l2_err("%s qpmap_dma is err 0x%lx\n", __func__, PTR_ERR(dmabuf));
					mtk_venc_queue_error_event(ctx);
					continue;
				}

				if (mtk_vcodec_dma_attach_map(dev, dmabuf, NULL, NULL,
					&mtkbuf->frm_buf.qpmap_dma_addr, DMA_TO_DEVICE, __func__, __LINE__)) {
					dma_buf_put(dmabuf);
					continue;
				}
				mtkbuf->frm_buf.qpmap_dma = dmabuf;
				mtkbuf->frm_buf.has_qpmap = 1;
				mtk_v4l2_debug(2, "[%d] Have Qpmap fd, buf->index:%d. qpmap_dma:%p, fd:%u",
					ctx->id, buf->index, mtkbuf->frm_buf.qpmap_dma, meta_desc.value);
			} else {
				if (meta_desc.type == METADATA_HDR) {
					mtkbuf->frm_buf.has_meta = 1;
					mtkbuf->frm_buf.meta_dma = mtkbuf->frm_buf.metabuffer_dma;
					mtkbuf->frm_buf.meta_addr =
						mtkbuf->frm_buf.metabuffer_addr + meta_desc.value;
					//vpud use fd to get va and pa,we should add a
					//offset to get real address of hdr
					mtkbuf->frm_buf.meta_offset = meta_desc.value;
				} else if (meta_desc.type == METADATA_DYNAMICPARAM) {
					mtkbuf->frm_buf.dyparams_dma = mtkbuf->frm_buf.metabuffer_dma;
					mtkbuf->frm_buf.dyparams_dma_addr = mtkbuf->frm_buf.metabuffer_addr;
					mtkbuf->frm_buf.dyparams_offset = meta_desc.value;
					mtk_v4l2_debug(2,"meta data:dyparams_dma:%p dyparams_dma_addr  iova 0x%lx",
						mtkbuf->frm_buf.dyparams_dma,
						(unsigned long)mtkbuf->frm_buf.dyparams_dma_addr);
				}
			}
		}
		dma_buf_vunmap_unlocked(mtkbuf->frm_buf.metabuffer_dma, &meta_map);
		mtk_vcodec_dma_unmap_detach(mtkbuf->frm_buf.metabuffer_dma, &meta_buf_att, &meta_sgt, DMA_TO_DEVICE);
	}

	return v4l2_m2m_qbuf(file, ctx->m2m_ctx, buf);
}

static int vidioc_venc_dqbuf(struct file *file, void *priv,
			     struct v4l2_buffer *buf)
{
	int ret = 0;
	struct mtk_vcodec_ctx *ctx = fh_to_ctx(priv);
	struct vb2_queue *vq;
	struct vb2_buffer *vb;
	struct mtk_video_enc_buf *mtkbuf;
	struct vb2_v4l2_buffer  *vb2_v4l2;

	if (mtk_vcodec_is_state(ctx, MTK_STATE_ABORT)) {
		mtk_v4l2_err("[%d] Call on QBUF after unrecoverable error",
			     ctx->id);
		return -EIO;
	}

	ret = v4l2_m2m_dqbuf(file, ctx->m2m_ctx, buf);
	if (buf->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE &&
		ret == 0) {
		vq = v4l2_m2m_get_vq(ctx->m2m_ctx, buf->type);
		if (vq == NULL) {
			mtk_v4l2_debug(1, "vq is NULL");
			return -EINVAL;
		}
		if (buf->index >= vq->num_buffers) {
			mtk_v4l2_err("[%d] buffer index %d out of range %d",
				ctx->id, buf->index, vq->num_buffers);
			return -EINVAL;
		}
		vb = vq->bufs[buf->index];
		vb2_v4l2 = container_of(vb, struct vb2_v4l2_buffer, vb2_buf);
		mtkbuf = container_of(vb2_v4l2, struct mtk_video_enc_buf, vb);

		if (mtkbuf->general_user_fd < 0)
			buf->reserved = 0xFFFFFFFF;
		else
			buf->reserved = mtkbuf->general_user_fd;
		mtk_v4l2_debug(2,
			"dqbuf index %d general_buf_fd=%d, mtkbuf->general_user_fd = %d",
			buf->index, buf->reserved, mtkbuf->general_user_fd);
	}

	return ret;
}

static int vidioc_try_encoder_cmd(struct file *file, void *priv,
	struct v4l2_encoder_cmd *cmd)
{
	switch (cmd->cmd) {
	case V4L2_ENC_CMD_STOP:
	case V4L2_ENC_CMD_START:
		cmd->flags = 0; // don't support flags
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int vidioc_encoder_cmd(struct file *file, void *priv,
	struct v4l2_encoder_cmd *cmd)
{
	struct mtk_vcodec_ctx *ctx = fh_to_ctx(priv);
	struct vb2_queue *src_vq, *dst_vq;
	int ret;

	ret = vidioc_try_encoder_cmd(file, priv, cmd);
	if (ret)
		return ret;

	mtk_v4l2_debug(0, "[%d] encoder cmd= %u", ctx->id, cmd->cmd);
	dst_vq = v4l2_m2m_get_vq(ctx->m2m_ctx,
		V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);
	switch (cmd->cmd) {
	case V4L2_ENC_CMD_STOP:
		src_vq = v4l2_m2m_get_vq(ctx->m2m_ctx,
			V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);

		if (!src_vq) {
			mtk_v4l2_err("fail to get src_vq");
			return -EINVAL;
		}
		if (!vb2_is_streaming(src_vq)) {
			mtk_v4l2_debug(1, "Output stream is off. No need to flush.");
			return 0;
		}

		if (!dst_vq) {
			mtk_v4l2_err("fail to get dst_vq");
			return -EINVAL;
		}
		if (!vb2_is_streaming(dst_vq)) {
			mtk_v4l2_debug(1, "Capture stream is off. No need to flush.");
			return 0;
		}
		if (ctx->enc_flush_buf->lastframe == NON_EOS) {
			ctx->enc_flush_buf->lastframe = EOS;
			v4l2_m2m_buf_queue_check(ctx->m2m_ctx, &ctx->enc_flush_buf->vb);
			v4l2_m2m_try_schedule(ctx->m2m_ctx);
		} else {
			mtk_v4l2_debug(1, "Stopping no need to queue cmd enc_flush_buf.");
			return 0;
		}
		break;

	case V4L2_ENC_CMD_START:
		if (!dst_vq) {
			mtk_v4l2_err("fail to get dst_vq");
			return -EINVAL;
		}
		vb2_clear_last_buffer_dequeued(dst_vq);
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

const struct v4l2_ioctl_ops mtk_venc_ioctl_ops = {
	.vidioc_streamon                = v4l2_m2m_ioctl_streamon,
	.vidioc_streamoff               = v4l2_m2m_ioctl_streamoff,

	.vidioc_reqbufs                 = v4l2_m2m_ioctl_reqbufs,
	.vidioc_querybuf                = v4l2_m2m_ioctl_querybuf,
	.vidioc_qbuf                    = vidioc_venc_qbuf,
	.vidioc_dqbuf                   = vidioc_venc_dqbuf,

	.vidioc_querycap                = vidioc_venc_querycap,
	.vidioc_enum_fmt_vid_cap = vidioc_enum_fmt_vid_cap_mplane,
	.vidioc_enum_fmt_vid_out = vidioc_enum_fmt_vid_out_mplane,
	.vidioc_enum_framesizes         = vidioc_enum_framesizes,

	.vidioc_try_fmt_vid_cap_mplane  = vidioc_try_fmt_vid_cap_mplane,
	.vidioc_try_fmt_vid_out_mplane  = vidioc_try_fmt_vid_out_mplane,
	.vidioc_expbuf                  = v4l2_m2m_ioctl_expbuf,

	.vidioc_s_parm                  = vidioc_venc_s_parm,
	.vidioc_g_parm                  = vidioc_venc_g_parm,
	.vidioc_s_fmt_vid_cap_mplane    = vidioc_venc_s_fmt_cap,
	.vidioc_s_fmt_vid_out_mplane    = vidioc_venc_s_fmt_out,

	.vidioc_g_fmt_vid_cap_mplane    = vidioc_venc_g_fmt,
	.vidioc_g_fmt_vid_out_mplane    = vidioc_venc_g_fmt,

	.vidioc_create_bufs             = v4l2_m2m_ioctl_create_bufs,
	.vidioc_prepare_buf             = v4l2_m2m_ioctl_prepare_buf,

	.vidioc_subscribe_event         = vidioc_venc_subscribe_evt,
	.vidioc_unsubscribe_event       = v4l2_event_unsubscribe,

	.vidioc_g_selection             = vidioc_venc_g_selection,
	.vidioc_s_selection             = vidioc_venc_s_selection,

	.vidioc_encoder_cmd             = vidioc_encoder_cmd,
	.vidioc_try_encoder_cmd         = vidioc_try_encoder_cmd,
};

static int vb2ops_venc_queue_setup(struct vb2_queue *vq,
				   unsigned int *nbuffers,
				   unsigned int *nplanes,
				   unsigned int sizes[],
				   struct device *alloc_devs[])
{
	struct mtk_vcodec_ctx *ctx;
	struct mtk_q_data *q_data;
	unsigned int i;

	if (IS_ERR_OR_NULL(vq) || IS_ERR_OR_NULL(nbuffers) ||
		IS_ERR_OR_NULL(nplanes) || IS_ERR_OR_NULL(alloc_devs)) {
		mtk_v4l2_err("vq %p, nbuffers %p, nplanes %p, alloc_devs %p",
			vq, nbuffers, nplanes, alloc_devs);
		return -EINVAL;
	}

	ctx = vb2_get_drv_priv(vq);
	q_data = mtk_venc_get_q_data(ctx, vq->type);
	if (q_data == NULL || q_data->fmt == NULL ||
		(*nplanes) > MTK_VCODEC_MAX_PLANES) {
		mtk_v4l2_err("vq->type=%d nplanes %d err", vq->type, *nplanes);
		return -EINVAL;
	}

	if (*nplanes) {
		for (i = 0; i < *nplanes; i++)
			if (sizes[i] < q_data->sizeimage[i] || sizes[i] > q_data->sizeimage[i] * 2)
				return -EINVAL;
	} else {
		*nplanes = q_data->fmt->num_planes;
		for (i = 0; i < *nplanes; i++)
			sizes[i] = q_data->sizeimage[i];
	}

	mtk_v4l2_debug(2, "[%d] nplanes %d nbuffers %d size %d %d %d sizeimage %d %d %d, state=%d",
		ctx->id, *nplanes, *nbuffers, sizes[0], sizes[1], sizes[2],
		q_data->sizeimage[0], q_data->sizeimage[1], q_data->sizeimage[2],
		mtk_vcodec_get_state(ctx));

#if (!(IS_ENABLED(CONFIG_DEVICE_MODULES_ARM_SMMU_V3)))
	if (ctx->enc_params.svp_mode && is_disable_map_sec() && mtk_venc_is_vcu()) {
		vq->mem_ops = &venc_sec_dma_contig_memops;
		mtk_v4l2_debug(1, "[%d] hook venc_sec_dma_contig_memops for queue type %d",
			ctx->id, vq->type);
	}
#endif

	// previously stream off with task not empty
	mtk_vcodec_set_state_from(ctx, MTK_STATE_FLUSH, MTK_STATE_STOP);

	return 0;
}

static struct dma_gen_buf *create_general_buffer_info(struct mtk_vcodec_ctx *ctx, int fd)
{
	struct dma_gen_buf *gen_buf_info = NULL;
	struct iosys_map map;
	struct dma_buf *dmabuf = NULL;
	struct dma_buf_attachment *buf_att = NULL;
	struct sg_table *sgt = NULL;
	dma_addr_t dma_general_addr = 0;
	void *va = NULL;
	int i = 0;

	memset(&map, 0, sizeof(struct iosys_map));

	dmabuf = dma_buf_get(fd);
	if (IS_ERR_OR_NULL(dmabuf)) {
		mtk_v4l2_err("dma_buf_get fail ret %ld", PTR_ERR(dmabuf));
		return NULL;
	}

	if (mtk_vcodec_dma_attach_map(ctx->general_dev,
		dmabuf, &buf_att, &sgt, &dma_general_addr, DMA_BIDIRECTIONAL, __func__, __LINE__)) {
		dma_buf_put(dmabuf);
		return NULL;
	}

	//save va and dmabuf
	for (i = 0; i < MAX_GEN_BUF_CNT; i++) {
		if (ctx->dma_buf_list[i].dmabuf == NULL) {
			gen_buf_info = &ctx->dma_buf_list[i];
			gen_buf_info->va = va;
			gen_buf_info->dmabuf = dmabuf;
			gen_buf_info->dma_general_addr = dma_general_addr;
			gen_buf_info->buf_att = buf_att;
			gen_buf_info->sgt = sgt;
			mtk_v4l2_debug(4, "save general buf va %p dmabuf %p addr:%llx at %d",
				va, dmabuf, (u64)dma_general_addr, i);
			break;
		}
	}
	if (gen_buf_info == NULL) {
		mtk_v4l2_err("dma_buf_list is overflow!");
		mtk_vcodec_dma_unmap_detach(dmabuf, &buf_att, &sgt, DMA_BIDIRECTIONAL);
		dma_buf_put(dmabuf);
	}

	return gen_buf_info;
}

static struct dma_gen_buf *get_general_buffer_info(struct mtk_vcodec_ctx *ctx,
	struct dma_buf *dmabuf)
{
	struct dma_gen_buf *gen_buf_info = NULL;
	int i;

	for (i = 0; i < MAX_GEN_BUF_CNT; i++) {
		if (ctx->dma_buf_list[i].dmabuf == dmabuf) {
			gen_buf_info = &ctx->dma_buf_list[i];
			mtk_v4l2_debug(4, "get general buf va %p dmabuf %p addr:%llx at %d",
				gen_buf_info->va, dmabuf, (u64)gen_buf_info->dma_general_addr, i);
			return gen_buf_info;
		}
	}
	return NULL;
}

static void release_general_buffer_info(struct dma_gen_buf *gen_buf_info)
{
	if (gen_buf_info == NULL) {
		mtk_v4l2_debug(1, "gen_buf_info NULL, may be already released");
		return;
	}

	mtk_v4l2_debug(8, "dma_buf_put general_buf %p, dmabuf:%p, dma_addr:%llx",
		gen_buf_info->va, gen_buf_info->dmabuf, (u64)gen_buf_info->dma_general_addr);

	mtk_vcodec_dma_unmap_detach(
		gen_buf_info->dmabuf, &gen_buf_info->buf_att, &gen_buf_info->sgt, DMA_BIDIRECTIONAL);
	dma_buf_put(gen_buf_info->dmabuf);

	memset((void *)gen_buf_info, 0, sizeof(struct dma_gen_buf));
}

static void set_general_buffer(struct mtk_vcodec_ctx *ctx, struct mtk_vcodec_mem *bs_buffer, int fd)
{
	struct dma_gen_buf *gen_buf_info;

	mutex_lock(&ctx->gen_buf_list_lock);
	gen_buf_info = create_general_buffer_info(ctx, fd);
	if (gen_buf_info != NULL) {
		bs_buffer->dma_general_buf  = gen_buf_info->dmabuf;
		bs_buffer->dma_general_addr = gen_buf_info->dma_general_addr;
		bs_buffer->general_buf_fd = fd;
	} else {
		bs_buffer->dma_general_buf = 0;
		bs_buffer->dma_general_addr = 0;
		bs_buffer->general_buf_fd = 0;
	}
	mutex_unlock(&ctx->gen_buf_list_lock);
}

static void release_general_buffer_info_by_dmabuf(struct mtk_vcodec_ctx *ctx,
	struct dma_buf *dmabuf)
{
	mutex_lock(&ctx->gen_buf_list_lock);
	release_general_buffer_info(get_general_buffer_info(ctx, dmabuf));
	mutex_unlock(&ctx->gen_buf_list_lock);
}

static void release_all_general_buffer_info(struct mtk_vcodec_ctx *ctx)
{
	int i;

	mutex_lock(&ctx->gen_buf_list_lock);
	for (i = 0; i < MAX_GEN_BUF_CNT; i++) {
		if (ctx->dma_buf_list[i].dmabuf)
			release_general_buffer_info(&ctx->dma_buf_list[i]);
	}
	mutex_unlock(&ctx->gen_buf_list_lock);
}

static int vb2ops_venc_buf_prepare(struct vb2_buffer *vb)
{
	struct mtk_vcodec_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	struct mtk_q_data *q_data;
	int i;
	struct mtk_video_enc_buf *mtkbuf;
	struct vb2_v4l2_buffer *vb2_v4l2;

	if (vb->vb2_queue->memory != VB2_MEMORY_DMABUF)
		return 0;

	q_data = mtk_venc_get_q_data(ctx, vb->vb2_queue->type);

	// Check if need to proceed cache operations
	vb2_v4l2 = container_of(vb, struct vb2_v4l2_buffer, vb2_buf);
	mtkbuf = container_of(vb2_v4l2, struct mtk_video_enc_buf, vb);

	for (i = 0; i < q_data->fmt->num_planes; i++) {
		if (vb2_plane_size(vb, i) < q_data->sizeimage[i]) {
			mtk_v4l2_err("data will not fit into plane %d (%lu < %d)",
				i,
				vb2_plane_size(vb, i),
				q_data->sizeimage[i]);
			return -EINVAL;
		}

		// Check if need to proceed cache operations
		vb2_v4l2 = container_of(vb, struct vb2_v4l2_buffer, vb2_buf);
		mtkbuf = container_of(vb2_v4l2, struct mtk_video_enc_buf, vb);

		if (vb->vb2_queue->type != V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
			if (mtkbuf->general_user_fd > 0)
				set_general_buffer(ctx, &mtkbuf->bs_buf, mtkbuf->general_user_fd);
			else
				mtkbuf->bs_buf.dma_general_buf = 0;

			mtk_v4l2_debug(4, "[%d] general_buf fd = %d, dma_buf = %p, DMA=%pad",
				ctx->id,
				mtkbuf->general_user_fd,
				mtkbuf->bs_buf.dma_general_buf,
				&mtkbuf->bs_buf.dma_general_addr);
		}

		if (!(mtkbuf->flags & NO_CAHCE_CLEAN)) {
			struct mtk_vcodec_mem src_mem;
			struct vb2_dc_buf *dc_buf = vb->planes[i].mem_priv;

			mtk_v4l2_debug(4, "[%d] Cache sync+", ctx->id);
			dma_sync_sg_for_device(
				vb->vb2_queue->dev,
				dc_buf->dma_sgt->sgl,
				dc_buf->dma_sgt->orig_nents,
				DMA_TO_DEVICE);

			src_mem.dma_addr = vb2_dma_contig_plane_dma_addr(vb, i);
			src_mem.size = (size_t)(vb->planes[i].bytesused - vb->planes[i].data_offset);

			mtk_v4l2_debug(4, "[%d] Cache sync TD for %lx sz=%d dev %p ",
				ctx->id,
				(unsigned long)src_mem.dma_addr,
				(unsigned int)src_mem.size,
				vb->vb2_queue->dev);
		}
	}

	return 0;
}

static void vb2ops_venc_buf_finish(struct vb2_buffer *vb)
{
	struct mtk_vcodec_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	struct mtk_video_enc_buf *mtkbuf;
	struct vb2_v4l2_buffer *vb2_v4l2;

	vb2_v4l2 = container_of(vb, struct vb2_v4l2_buffer, vb2_buf);
	mtkbuf = container_of(vb2_v4l2, struct mtk_video_enc_buf, vb);

	if (mtkbuf->bs_buf.dma_general_buf != 0) {
		release_general_buffer_info_by_dmabuf(ctx, mtkbuf->bs_buf.dma_general_buf);
		mtkbuf->bs_buf.dma_general_buf = 0;
		mtk_v4l2_debug(4, "dma_buf_put general_buf fd=%d, dma_buf=%p, DMA=%pad",
			mtkbuf->general_user_fd,
			mtkbuf->bs_buf.dma_general_buf,
			&mtkbuf->bs_buf.dma_general_addr);
	}

	if (vb2_v4l2->flags & V4L2_BUF_FLAG_LAST)
		mtk_v4l2_debug(0, "[%d] type(%d) flags=%x idx=%d pts=%llu",
			ctx->id, vb->vb2_queue->type, vb2_v4l2->flags,
			vb->index, vb->timestamp);

	if (vb->vb2_queue->memory == VB2_MEMORY_DMABUF &&
		!(mtkbuf->flags & NO_CAHCE_INVALIDATE)) {
		if (vb->vb2_queue->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
			struct mtk_vcodec_mem dst_mem;
			struct vb2_dc_buf *dc_buf = vb->planes[0].mem_priv;

			mtk_dma_sync_sg_range(dc_buf->dma_sgt, vb->vb2_queue->dev,
				ROUND_N(vb->planes[0].bytesused, 64), DMA_FROM_DEVICE);

			dst_mem.dma_addr = vb2_dma_contig_plane_dma_addr(vb, 0);
			dst_mem.size = (size_t)vb->planes[0].bytesused;
			mtk_v4l2_debug(4, "[%d] Cache sync FD for %lx sz=%d dev %p",
				ctx->id,
				(unsigned long)dst_mem.dma_addr,
				(unsigned int)dst_mem.size,
				vb->vb2_queue->dev);
		}
	}

	if (mtkbuf->frm_buf.metabuffer_dma == NULL && !IS_ERR_OR_NULL(mtkbuf->frm_buf.meta_dma)) {
		mtk_v4l2_debug(4, "dma_buf_put dma_buf=%p, DMA=%lx",
			mtkbuf->frm_buf.meta_dma,
			(unsigned long)mtkbuf->frm_buf.meta_addr);
		mtk_vcodec_dma_unmap_detach(
			mtkbuf->frm_buf.meta_dma, &mtkbuf->frm_buf.buf_att, &mtkbuf->frm_buf.sgt, DMA_TO_DEVICE);
		dma_buf_put(mtkbuf->frm_buf.meta_dma);
		mtkbuf->frm_buf.meta_dma = NULL;
	}

	if (!IS_ERR_OR_NULL(mtkbuf->frm_buf.metabuffer_dma)) {
		mtk_v4l2_debug(2, "dma_buf_put dma_buf=%p, DMA=%lx",
			mtkbuf->frm_buf.metabuffer_dma,
			(unsigned long)mtkbuf->frm_buf.metabuffer_addr);
		dma_buf_put(mtkbuf->frm_buf.metabuffer_dma);
		mtkbuf->frm_buf.metabuffer_dma = NULL;
	}

	if (!IS_ERR_OR_NULL(mtkbuf->frm_buf.qpmap_dma)) {
		mtk_v4l2_debug(2, "dma_buf_put qpmap_dma=%p, DMA=%lx",
			mtkbuf->frm_buf.qpmap_dma,
			(unsigned long)mtkbuf->frm_buf.qpmap_dma_addr);
		mtk_vcodec_dma_unmap_detach(mtkbuf->frm_buf.qpmap_dma,
			&mtkbuf->frm_buf.qpmap_dma_att, &mtkbuf->frm_buf.qpmap_sgt, DMA_TO_DEVICE);
		dma_buf_put(mtkbuf->frm_buf.qpmap_dma);
		mtkbuf->frm_buf.qpmap_dma = NULL;
	}
}


static void vb2ops_venc_buf_queue(struct vb2_buffer *vb)
{
	struct mtk_vcodec_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	struct vb2_v4l2_buffer *vb2_v4l2 =
		container_of(vb, struct vb2_v4l2_buffer, vb2_buf);

	struct mtk_video_enc_buf *mtk_buf =
		container_of(vb2_v4l2, struct mtk_video_enc_buf, vb);

	if(mtk_venc_dvfs_monitor_op_rate(ctx, vb->vb2_queue->type))
		ctx->param_change |= MTK_ENCODE_PARAM_OPERATION_RATE;


	if ((vb->vb2_queue->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) &&
	    (ctx->param_change != MTK_ENCODE_PARAM_NONE)) {
		mtk_v4l2_debug(1, "[%d] Before id=%d encode parameter change %x",
			       ctx->id,
			       mtk_buf->vb.vb2_buf.index,
			       ctx->param_change);
		mtk_buf->param_change = ctx->param_change;
		mtk_buf->enc_params = ctx->enc_params;
		ctx->param_change = MTK_ENCODE_PARAM_NONE;
	}

	if ((vb->vb2_queue->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) &&
		(ctx->enc_params.low_latency_mode == 1))
		wake_up(&ctx->bs_wq);

	v4l2_m2m_buf_queue_check(ctx->m2m_ctx, to_vb2_v4l2_buffer(vb));
}

/* chech whether encode width and height is overspec or not, must use real size(crop size)
 * to avoid some special case check fail, for example if max spec is 2560x1440, encode
 * with 1440x2560, if some format width stride is 64 alignment, buffer size will be 1472x2560,
 * check buffer size will be overspec but google case will test with this kind of resolution
 * real size(crop size) meet below rule means under spec:
 *   -- width_min <= width <= width_max
 *   -- height_min <= height <= width_max
 *   -- width_min * height_min <=width * height <= width_max * height_max
 */
static int mtk_venc_overspec_check(struct mtk_vcodec_ctx *ctx)
{
	__u32 bs_fourcc;
	int width, height, i;
	int max_resolution, min_resolution, resolution;
	struct mtk_codec_framesizes *spec_size_info = NULL;

	if (ctx->q_data[MTK_Q_DATA_DST].fmt != NULL)
		bs_fourcc = ctx->q_data[MTK_Q_DATA_DST].fmt->fourcc;
	else
		bs_fourcc = mtk_venc_formats[default_cap_fmt_idx].fourcc;

	for (i = 0; i < MTK_MAX_ENC_CODECS_SUPPORT; i++) {
		if (mtk_venc_framesizes[i].fourcc == bs_fourcc)
			spec_size_info = &mtk_venc_framesizes[i];
	}
	if (!spec_size_info) {
		mtk_v4l2_err("fail to get spec_size_info");
		return -EINVAL;
	}
	width = ctx->q_data[MTK_Q_DATA_SRC].visible_width;
	height = ctx->q_data[MTK_Q_DATA_SRC].visible_height;
	max_resolution = spec_size_info->stepwise.max_width * spec_size_info->stepwise.max_height;
	min_resolution = spec_size_info->stepwise.min_width * spec_size_info->stepwise.min_height;
	resolution = width * height;
	if (width < spec_size_info->stepwise.min_width
		|| width > spec_size_info->stepwise.max_width
		|| height < spec_size_info->stepwise.min_height
		|| height > spec_size_info->stepwise.max_width
		|| resolution > max_resolution
		|| resolution < min_resolution) {
		mtk_v4l2_err("%dx%d over spec %dx%d", width, height,
			spec_size_info->stepwise.max_width,
			spec_size_info->stepwise.max_height);
		return -EINVAL;
	}
	return 0;
}

static int vb2ops_venc_start_streaming(struct vb2_queue *q, unsigned int count)
{
	struct mtk_vcodec_ctx *ctx = vb2_get_drv_priv(q);
	struct venc_enc_param param;
	struct mtk_q_data *q_data_src = &ctx->q_data[MTK_Q_DATA_SRC];

	int ret;
	int i;

	mtk_v4l2_debug(4, "[%d] (%d) state=(%x)", ctx->id, q->type, mtk_vcodec_get_state(ctx));
	/* Once state turn into MTK_STATE_ABORT, we need stop_streaming
	  * to clear it
	  */
	if (!mtk_vcodec_state_in_range(ctx, MTK_STATE_INIT, MTK_STATE_STOP)) { // ABORT || FREE
		ret = -EIO;
		goto err_set_param;
	}

	/* Do the initialization when both start_streaming have been called */
	if (V4L2_TYPE_IS_OUTPUT(q->type)) {
		if (!vb2_start_streaming_called(&ctx->m2m_ctx->cap_q_ctx.q))
			return 0;
	} else {
		if (!vb2_start_streaming_called(&ctx->m2m_ctx->out_q_ctx.q))
			return 0;
	}

	//release slb for cpu used more perf than venc
	ctx->enc_params.slbc_cpu_used_performance =
		(isSLB_CPU_USED_PERFORMANCE_USAGE(q_data_src->visible_width, q_data_src->visible_height,
		ctx->enc_params.framerate_num/ctx->enc_params.framerate_denom, ctx->dev->enc_slb_cpu_used_perf) &&
		(ctx->dev->enc_slb_cpu_used_perf > 0) && (ctx->enc_params.operationrate < 120));

	ctx->enc_params.slbc_request_extra =
	(isENCODE_REQUEST_SLB_EXTRA(q_data_src->visible_width, q_data_src->visible_height) &&
	(ctx->dev->enc_slb_extra > 0));

	if ((ctx->use_slbc == 1) && (ctx->enc_params.slbc_cpu_used_performance == 1)) {
		mtk_v4l2_debug(0, "slbc_cpu_used_perf_release, %p\n", &ctx->sram_data);
		slbc_release(&ctx->sram_data);
		ctx->use_slbc = 0;
		ctx->slbc_addr = 0;
		mtk_v4l2_debug(0, "slbc_cpu_used_perf_release ref %d\n", ctx->sram_data.ref);
		if (ctx->sram_data.ref <= 0)
			atomic_set(&mtk_venc_slb_cb.release_slbc, 0);
	} else if ((ctx->use_slbc == 1) && (ctx->enc_params.slbc_request_extra == 1)) {
		ctx->sram_data_extra.uid = UID_MM_VENC_8K;
		ctx->sram_data_extra.type = TP_BUFFER;
		ctx->sram_data_extra.size = 0;
		ctx->sram_data_extra.flag = FG_POWER;
		if (slbc_request(&ctx->sram_data_extra) >= 0) {
			ctx->use_slbc_extra = 1;
			ctx->slbc_addr_extra = (unsigned int)(unsigned long)ctx->sram_data_extra.paddr;
		} else {
			pr_info("slbc_request_extra fail\n");
			ctx->use_slbc_extra = 0;
		}
		if (ctx->slbc_addr_extra % 256 != 0 || ctx->slbc_addr_extra == 0) {
			pr_info("slbc_addr_extra error 0x%x\n", ctx->slbc_addr_extra);
			ctx->use_slbc_extra = 0;
		}

		if (ctx->use_slbc_extra) {
			// use extra slbc address as slbc start address
			ctx->slbc_addr = ctx->slbc_addr_extra;
		} else {
			mtk_v4l2_debug(0, "request extra slbc fail and release all slbc, %p\n", &ctx->sram_data);
			slbc_release(&ctx->sram_data);
			ctx->use_slbc = 0;
			ctx->slbc_addr = 0;
			mtk_v4l2_debug(0, "request extra slbc fail and release all slbc ref %d\n", ctx->sram_data.ref);
			if (ctx->sram_data.ref <= 0)
				atomic_set(&mtk_venc_slb_cb.release_slbc, 0);
		}
		pr_info("slbc_request_extra %d, 0x%x, 0x%lx, ref %d\n",
		ctx->use_slbc_extra, ctx->slbc_addr_extra, (unsigned long)ctx->sram_data_extra.paddr,
		ctx->sram_data_extra.ref);
	}

	if (mtk_venc_overspec_check(ctx)) {
		mtk_venc_error_handle(ctx);
		goto err_set_param;
	}

	memset(&param, 0, sizeof(param));
	mtk_venc_set_param(ctx, &param);
	ret = venc_if_set_param(ctx, VENC_SET_PARAM_ENC, &param);

	mtk_v4l2_debug(0,
	"fmt 0x%x, P/L %d/%d, w/h %d/%d, buf %d/%d, fps/bps %d/%d(%d), gop %d, ip# %d opr %d async %d grid size %d/%d b#%d, slbc %d maxqp %d minqp %d",
	param.input_yuv_fmt, param.profile,
	param.level, param.width, param.height,
	param.buf_width, param.buf_height,
	param.frm_rate, param.bitrate, param.bitratemode,
	param.gop_size, param.intra_period,
	param.operationrate, ctx->async_mode,
	(param.heif_grid_size>>16), param.heif_grid_size&0xffff,
	param.num_b_frame, param.slbc_ready, param.max_qp, param.min_qp);

	ctx->enc_params.slbc_encode_performance = isENCODE_PERFORMANCE_USAGE(param.width,
		param.height, param.frm_rate, param.operationrate);

	if (ctx->use_slbc == 1) {
		if (ctx->enc_params.slbc_encode_performance)
			atomic_inc(&mtk_venc_slb_cb.perf_used_cnt);
	} else {
		if (!ctx->enc_params.slbc_cpu_used_performance) {
			atomic_inc(&mtk_venc_slb_cb.later_cnt);
			ctx->later_cnt_once = true;
		}
	}
	mtk_v4l2_debug(0, "slb_cb %d/%d perf %d cnt %d/%d/%d slb_cpu_used_perf %d",
		atomic_read(&mtk_venc_slb_cb.release_slbc),
		atomic_read(&mtk_venc_slb_cb.request_slbc),
		ctx->enc_params.slbc_encode_performance,
		atomic_read(&mtk_venc_slb_cb.perf_used_cnt),
		atomic_read(&mtk_venc_slb_cb.later_cnt),
		ctx->later_cnt_once,
		ctx->enc_params.slbc_cpu_used_performance);

	if (ret) {
		mtk_v4l2_err("venc_if_set_param failed=%d", ret);
		mtk_venc_error_handle(ctx);
		goto err_set_param;
	}
	ctx->param_change = MTK_ENCODE_PARAM_NONE;

	if ((ctx->q_data[MTK_Q_DATA_DST].fmt->fourcc == V4L2_PIX_FMT_H264 ||
	     ctx->q_data[MTK_Q_DATA_DST].fmt->fourcc == V4L2_PIX_FMT_HEVC ||
	     ctx->q_data[MTK_Q_DATA_DST].fmt->fourcc == V4L2_PIX_FMT_HEIF ||
	     ctx->q_data[MTK_Q_DATA_DST].fmt->fourcc == V4L2_PIX_FMT_MPEG4) &&
	    (ctx->enc_params.seq_hdr_mode !=
	     V4L2_MPEG_VIDEO_HEADER_MODE_SEPARATE)) {
		ret = venc_if_set_param(ctx,
					VENC_SET_PARAM_PREPEND_HEADER,
					NULL);
		if (ret) {
			mtk_v4l2_err("venc_if_set_param failed=%d", ret);
			mtk_venc_error_handle(ctx);
			goto err_set_param;
		}
		mtk_vcodec_set_state(ctx, MTK_STATE_HEADER);
	} else if (mtk_vcodec_set_state_from(ctx, MTK_STATE_HEADER, MTK_STATE_FLUSH)
			== MTK_STATE_FLUSH) // flush and reset
		mtk_v4l2_debug(1, "recover from flush");
	else
		mtk_vcodec_set_state_except(ctx, MTK_STATE_INIT, MTK_STATE_FLUSH);

	mutex_lock(&ctx->dev->enc_dvfs_mutex);
	if (ctx->dev->venc_dvfs_params.mmdvfs_in_vcp) {
		mtk_venc_prepare_vcp_dvfs_data(ctx, &param);
		ret = venc_if_set_param(ctx, VENC_SET_PARAM_MMDVFS, &param);
		if (ret != 0)
			mtk_v4l2_err("[VDVFS][%d] stream on ipi fail, ret %d", ctx->id, ret);
		mtk_venc_dvfs_sync_vsi_data(ctx);
		mtk_v4l2_debug(0, "[VDVFS][%d(%d)] start DVFS(UP):freq:%d, bw_factor:%d",
			ctx->id, mtk_vcodec_get_state(ctx),
			ctx->dev->venc_dvfs_params.target_freq,
			ctx->dev->venc_dvfs_params.target_bw_factor);
		mtk_venc_init_boost(ctx);
		mutex_unlock(&ctx->dev->enc_dvfs_mutex);

		mutex_lock(&ctx->dev->enc_qos_mutex);
		mtk_venc_pmqos_begin_inst(ctx);
		mtk_venc_pmqos_monitor_reset(ctx->dev);
		mutex_unlock(&ctx->dev->enc_qos_mutex);
	} else {
		mtk_v4l2_debug(0, "[%d][VDVFS][VENC] start ctrl DVFS in AP", ctx->id);
		mtk_venc_dvfs_begin_inst(ctx);
		mtk_venc_pmqos_begin_inst(ctx);
		mtk_venc_pmqos_monitor_reset(ctx->dev);
		mtk_venc_init_boost(ctx);
		mutex_unlock(&ctx->dev->enc_dvfs_mutex);
	}

	return 0;

err_set_param:
	for (i = 0; i < q->num_buffers; ++i) {
		if (q->bufs[i]->state == VB2_BUF_STATE_ACTIVE) {
			mtk_v4l2_debug(0, "[%d] id=%d, type=%d, %d -> VB2_BUF_STATE_QUEUED",
					ctx->id, i, q->type,
					(int)q->bufs[i]->state);
			v4l2_m2m_buf_done(to_vb2_v4l2_buffer(q->bufs[i]),
					VB2_BUF_STATE_QUEUED);
		}
	}

	mutex_lock(&ctx->buf_lock);
	if (q->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
		while (v4l2_m2m_dst_buf_remove(ctx->m2m_ctx))
			;
	else
		while (v4l2_m2m_src_buf_remove(ctx->m2m_ctx))
			;
	mutex_unlock(&ctx->buf_lock);

	return ret;
}

static void vb2ops_venc_stop_streaming(struct vb2_queue *q)
{
	struct mtk_vcodec_ctx *ctx = vb2_get_drv_priv(q);
	struct vb2_buffer *dst_buf;
	struct vb2_v4l2_buffer *src_vb2_v4l2, *dst_vb2_v4l2;
	struct mtk_video_enc_buf *srcbuf, *dstbuf;
	struct vb2_queue *srcq, *dstq;
	struct venc_done_result enc_result;
	struct venc_enc_param param;
	int i, ret;

	mtk_v4l2_debug(2, "[%d]-> type=%d", ctx->id, q->type);

	if (ctx->enc_params.low_latency_mode == 1)
		wake_up(&ctx->bs_wq);

	if (vb2_start_streaming_called(&ctx->m2m_ctx->cap_q_ctx.q) &&
		vb2_start_streaming_called(&ctx->m2m_ctx->out_q_ctx.q)) {
		ret = venc_if_encode(ctx,
		VENC_START_OPT_ENCODE_FRAME_FINAL,
		NULL, NULL, &enc_result);
		if (!ctx->async_mode)
			mtk_enc_put_buf(ctx);
		if (ret) {
			mtk_v4l2_err("venc_if_encode FINAL failed=%d", ret);
			if (ret == -EIO) {
				dstq = &ctx->m2m_ctx->cap_q_ctx.q;
				srcq = &ctx->m2m_ctx->out_q_ctx.q;
				mutex_lock(&ctx->buf_lock);
				for (i = 0; i < dstq->num_buffers; i++) {
					dst_vb2_v4l2 = container_of(
						dstq->bufs[i], struct vb2_v4l2_buffer, vb2_buf);
					dstbuf = container_of(
						dst_vb2_v4l2, struct mtk_video_enc_buf, vb);
					if (dst_vb2_v4l2->vb2_buf.state == VB2_BUF_STATE_ACTIVE)
						v4l2_m2m_buf_done(&dstbuf->vb, VB2_BUF_STATE_ERROR);
				}

				for (i = 0; i < srcq->num_buffers; i++) {
					src_vb2_v4l2 = container_of(
						srcq->bufs[i], struct vb2_v4l2_buffer, vb2_buf);
					srcbuf = container_of(
						src_vb2_v4l2, struct mtk_video_enc_buf, vb);
					if (src_vb2_v4l2->vb2_buf.state == VB2_BUF_STATE_ACTIVE)
						v4l2_m2m_buf_done(&srcbuf->vb, VB2_BUF_STATE_ERROR);
				}
				mutex_unlock(&ctx->buf_lock);

				venc_check_release_lock(ctx);
			}
		}
	}

	if (q->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
		mutex_lock(&ctx->buf_lock);
		while ((dst_vb2_v4l2 = v4l2_m2m_dst_buf_remove(ctx->m2m_ctx))) {
			dst_buf = &dst_vb2_v4l2->vb2_buf;
			dst_buf->planes[0].bytesused = 0;
			if (dst_vb2_v4l2->vb2_buf.state == VB2_BUF_STATE_ACTIVE)
				v4l2_m2m_buf_done(dst_vb2_v4l2, VB2_BUF_STATE_ERROR);
		}
		mutex_unlock(&ctx->buf_lock);
		release_all_general_buffer_info(ctx);
	} else {
		mutex_lock(&ctx->buf_lock);
		while ((src_vb2_v4l2 = v4l2_m2m_src_buf_remove(ctx->m2m_ctx))) {
			if (src_vb2_v4l2 != &ctx->enc_flush_buf->vb &&
				src_vb2_v4l2->vb2_buf.state == VB2_BUF_STATE_ACTIVE)
				v4l2_m2m_buf_done(src_vb2_v4l2, VB2_BUF_STATE_ERROR);
		}
		mutex_unlock(&ctx->buf_lock);

		ctx->enc_flush_buf->lastframe = NON_EOS;
		mutex_lock(&ctx->dev->enc_dvfs_mutex);
		if (ctx->dev->venc_dvfs_params.mmdvfs_in_vcp) {
			mtk_venc_unprepare_vcp_dvfs_data(ctx, &param);
			ret = venc_if_set_param(ctx, VENC_SET_PARAM_MMDVFS, &param);
			if (ret != 0)
				mtk_v4l2_err("[VDVFS][%d] stream off ipi fail, ret %d", ctx->id, ret);
			mtk_venc_dvfs_sync_vsi_data(ctx);
			mtk_v4l2_debug(0, "[VDVFS][%d(%d)] stop DVFS(UP):freq:%d, bw_factor%d",
				ctx->id, mtk_vcodec_get_state(ctx),
				ctx->dev->venc_dvfs_params.target_freq,
				ctx->dev->venc_dvfs_params.target_bw_factor);
			mutex_unlock(&ctx->dev->enc_dvfs_mutex);

			mutex_lock(&ctx->dev->enc_qos_mutex);
			mtk_venc_pmqos_end_inst(ctx);
			mtk_venc_pmqos_monitor_reset(ctx->dev);
			mutex_unlock(&ctx->dev->enc_qos_mutex);
		} else {
			mtk_v4l2_debug(0, "[%d][VDVFS][VENC] stop ctrl DVFS in AP", ctx->id);
			mtk_venc_dvfs_end_inst(ctx);
			mtk_venc_pmqos_end_inst(ctx);
			mtk_venc_pmqos_monitor_reset(ctx->dev);
			mutex_unlock(&ctx->dev->enc_dvfs_mutex);
		}

		ctx->has_first_input = false;
	}

	if ((q->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE &&
	     vb2_is_streaming(&ctx->m2m_ctx->out_q_ctx.q)) ||
	    (q->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE &&
	     vb2_is_streaming(&ctx->m2m_ctx->cap_q_ctx.q))) {
		mtk_v4l2_debug(1, "[%d]-> q type %d out=%d cap=%d",
			       ctx->id, q->type,
			       vb2_is_streaming(&ctx->m2m_ctx->out_q_ctx.q),
			       vb2_is_streaming(&ctx->m2m_ctx->cap_q_ctx.q));
		return;
	}
}

static const struct vb2_ops mtk_venc_vb2_ops = {
	.queue_setup            = vb2ops_venc_queue_setup,
	.buf_prepare            = vb2ops_venc_buf_prepare,
	.buf_queue              = vb2ops_venc_buf_queue,
	.wait_prepare           = vb2_ops_wait_prepare,
	.wait_finish            = vb2_ops_wait_finish,
	.buf_finish             = vb2ops_venc_buf_finish,
	.start_streaming        = vb2ops_venc_start_streaming,
	.stop_streaming         = vb2ops_venc_stop_streaming,
};

static int mtk_venc_encode_header(void *priv)
{
	struct mtk_vcodec_ctx *ctx = priv;
	int ret;
	struct vb2_buffer *dst_buf;
	struct vb2_v4l2_buffer *dst_vb2_v4l2, *src_vb2_v4l2;
	struct mtk_video_enc_buf *dst_buf_info;
	struct mtk_vcodec_mem *bs_buf;
	struct venc_done_result enc_result;
	bool already_put = false;

	memset(&enc_result, 0, sizeof(enc_result));
	dst_vb2_v4l2 = v4l2_m2m_dst_buf_remove(ctx->m2m_ctx);
	if (!dst_vb2_v4l2) {
		mtk_v4l2_debug(1, "No dst buffer");
		return -EINVAL;
	}
	dst_buf = &dst_vb2_v4l2->vb2_buf;
	dst_buf_info = container_of(dst_vb2_v4l2, struct mtk_video_enc_buf, vb);

	bs_buf = &dst_buf_info->bs_buf;
	if (mtk_v4l2_dbg_level > 0)
		bs_buf->va = vb2_plane_vaddr(dst_buf, 0);
	bs_buf->dma_addr = vb2_dma_contig_plane_dma_addr(dst_buf, 0);
	bs_buf->size = (size_t)dst_buf->planes[0].length;
	bs_buf->dmabuf = dst_buf->planes[0].dbuf;
	bs_buf->index = dst_buf->index;
	ctx->bs_list[bs_buf->index + 1] = (uintptr_t)bs_buf;

	mtk_v4l2_debug(1,
		       "[%d] buf id=%d va=0x%p dma_addr=0x%llx size=%zu",
		       ctx->id,
		       dst_buf->index, bs_buf->va,
		       (u64)bs_buf->dma_addr,
		       bs_buf->size);

	ret = venc_if_encode(ctx,
			     VENC_START_OPT_ENCODE_SEQUENCE_HEADER,
			     NULL, bs_buf, &enc_result);

	mutex_lock(&ctx->buf_lock);
	get_free_buffers(ctx, &enc_result);

	if (enc_result.bs_va == 0) {
		if (dst_vb2_v4l2->vb2_buf.state != VB2_BUF_STATE_ACTIVE) {
			already_put = true;
			mtk_v4l2_err("dst buf already put (ret %d)", ret);
		} else {
			dst_buf->planes[0].bytesused = 0;
			mtk_venc_error_handle(ctx);
			v4l2_m2m_buf_done(dst_vb2_v4l2,
					  VB2_BUF_STATE_ERROR);
			mtk_v4l2_err("venc_if_encode failed=%d", ret);
			mutex_unlock(&ctx->buf_lock);
			return -EINVAL;
		}
	}
	src_vb2_v4l2 = v4l2_m2m_next_src_buf(ctx->m2m_ctx);
	if (src_vb2_v4l2) {
		dst_vb2_v4l2->vb2_buf.timestamp = src_vb2_v4l2->vb2_buf.timestamp;
		dst_vb2_v4l2->timecode = src_vb2_v4l2->timecode;
	} else
		mtk_v4l2_err("No timestamp for the header buffer.");

	mtk_vcodec_set_state(ctx, MTK_STATE_HEADER);
	if (!already_put) {
		if (enc_result.flags&VENC_FLAG_MULTINAL)
			dst_vb2_v4l2->flags |= V4L2_BUF_FLAG_MULTINAL;
		if (enc_result.flags&VENC_FLAG_NAL_LENGTH_BS)
			dst_vb2_v4l2->flags |= V4L2_BUF_FLAG_NAL_LENGTH_BS;

		dst_buf->planes[0].bytesused = enc_result.bs_size;
		v4l2_m2m_buf_done(dst_vb2_v4l2, VB2_BUF_STATE_DONE);
	}
	mutex_unlock(&ctx->buf_lock);

	return 0;
}

static int mtk_venc_param_change(struct mtk_vcodec_ctx *ctx)
{
	struct venc_enc_param enc_prm;
	struct vb2_v4l2_buffer *vb2_v4l2 = v4l2_m2m_next_src_buf(ctx->m2m_ctx);
	struct mtk_video_enc_buf *mtk_buf =
		container_of(vb2_v4l2, struct mtk_video_enc_buf, vb);

	int ret = 0;

	memset(&enc_prm, 0, sizeof(enc_prm));
	if (mtk_buf->param_change == MTK_ENCODE_PARAM_NONE)
		return 0;

	if (mtk_buf->param_change & MTK_ENCODE_PARAM_BITRATE) {
		enc_prm.bitrate = mtk_buf->enc_params.bitrate;
		mtk_v4l2_debug(1, "[%d] id=%d, change param br=%d",
			ctx->id, mtk_buf->vb.vb2_buf.index, enc_prm.bitrate);
		ret |= venc_if_set_param(ctx, VENC_SET_PARAM_ADJUST_BITRATE, &enc_prm);
	}
	if (mtk_buf->param_change & MTK_ENCODE_PARAM_SEC_ENCODE) {
		enc_prm.svp_mode = mtk_buf->enc_params.svp_mode;
		mtk_v4l2_debug(0, "[%d] change param svp=%d", ctx->id, enc_prm.svp_mode);
		ret |= venc_if_set_param(ctx, VENC_SET_PARAM_SEC_MODE, &enc_prm);
	}
	if (!ret && mtk_buf->param_change & MTK_ENCODE_PARAM_FRAMERATE) {
		enc_prm.frm_rate = mtk_buf->enc_params.framerate_num /
				   mtk_buf->enc_params.framerate_denom;
		mtk_v4l2_debug(1, "[%d] id=%d, change param fr=%d",
			ctx->id, mtk_buf->vb.vb2_buf.index, enc_prm.frm_rate);
		ret |= venc_if_set_param(ctx, VENC_SET_PARAM_ADJUST_FRAMERATE, &enc_prm);
	}
	if (!ret && mtk_buf->param_change & MTK_ENCODE_PARAM_GOP_SIZE) {
		enc_prm.gop_size = mtk_buf->enc_params.gop_size;
		mtk_v4l2_debug(1, "[%d] change param intra period=%d", ctx->id, enc_prm.gop_size);
		ret |= venc_if_set_param(ctx, VENC_SET_PARAM_GOP_SIZE, &enc_prm);
	}
	if (!ret && mtk_buf->param_change & MTK_ENCODE_PARAM_FORCE_INTRA) {
		mtk_v4l2_debug(1, "[%d] id=%d, change param force I=%d",
			ctx->id, mtk_buf->vb.vb2_buf.index, mtk_buf->enc_params.force_intra);
		if (mtk_buf->enc_params.force_intra)
			ret |= venc_if_set_param(ctx, VENC_SET_PARAM_FORCE_INTRA, NULL);
	}

	if (!ret && mtk_buf->param_change & MTK_ENCODE_PARAM_SCENARIO) {
		enc_prm.scenario = mtk_buf->enc_params.scenario;
		if (mtk_buf->enc_params.scenario)
			ret |= venc_if_set_param(ctx, VENC_SET_PARAM_SCENARIO, &enc_prm);
		mtk_v4l2_debug(0, "[%d] idx=%d, change param scenario=%d async_mode=%d", ctx->id,
			mtk_buf->vb.vb2_buf.index, mtk_buf->enc_params.scenario, ctx->async_mode);
	}

	if (!ret && mtk_buf->param_change & MTK_ENCODE_PARAM_NONREFP) {
		enc_prm.nonrefp = mtk_buf->enc_params.nonrefp;
		mtk_v4l2_debug(1, "[%d] idx=%d, change param nonref=%d",
			ctx->id, mtk_buf->vb.vb2_buf.index, mtk_buf->enc_params.nonrefp);
		ret |= venc_if_set_param(ctx, VENC_SET_PARAM_NONREFP, &enc_prm);
	}

	if (!ret && mtk_buf->param_change & MTK_ENCODE_PARAM_NONREFPFREQ) {
		enc_prm.nonrefpfreq = mtk_buf->enc_params.nonrefpfreq;
		mtk_v4l2_debug(1, "[%d] idx=%d, change param nonrefpfreq=%d",
			 ctx->id, mtk_buf->vb.vb2_buf.index, mtk_buf->enc_params.nonrefpfreq);
		ret |= venc_if_set_param(ctx, VENC_SET_PARAM_NONREFPFREQ, &enc_prm);
	}

	if (!ret && mtk_buf->param_change & MTK_ENCODE_PARAM_DETECTED_FRAMERATE) {
		enc_prm.detectframerate = mtk_buf->enc_params.detectframerate;
		mtk_v4l2_debug(1, "[%d] idx=%d, change param detectfr=%d",
			ctx->id, mtk_buf->vb.vb2_buf.index, mtk_buf->enc_params.detectframerate);
		ret |= venc_if_set_param(ctx, VENC_SET_PARAM_DETECTED_FRAMERATE, &enc_prm);
	}

	if (!ret && mtk_buf->param_change & MTK_ENCODE_PARAM_RFS_ON) {
		enc_prm.rfs = mtk_buf->enc_params.rfs;
		mtk_v4l2_debug(1, "[%d] idx=%d, change param rfs=%d",
			ctx->id, mtk_buf->vb.vb2_buf.index, mtk_buf->enc_params.rfs);
		ret |= venc_if_set_param(ctx, VENC_SET_PARAM_RFS_ON, &enc_prm);
	}

	if (!ret && mtk_buf->param_change & MTK_ENCODE_PARAM_PREPEND_SPSPPS_TO_IDR) {
		enc_prm.prependheader = mtk_buf->enc_params.prependheader;
		mtk_v4l2_debug(1, "[%d] idx=%d, prepend spspps idr=%d",
			ctx->id, mtk_buf->vb.vb2_buf.index, mtk_buf->enc_params.prependheader);
		ret |= venc_if_set_param(ctx, VENC_SET_PARAM_PREPEND_SPSPPS_TO_IDR, &enc_prm);
	}

	if (!ret && mtk_buf->param_change & MTK_ENCODE_PARAM_OPERATION_RATE) {
		enc_prm.operationrate = mtk_buf->enc_params.operationrate;
		enc_prm.operationrate_adaptive = mtk_buf->enc_params.operationrate_adaptive;
		mtk_v4l2_debug(1, "[%d] idx=%d, operationrate=%d, adaptive=%d",
			ctx->id, mtk_buf->vb.vb2_buf.index, mtk_buf->enc_params.operationrate,
			mtk_buf->enc_params.operationrate_adaptive);
		ret |= venc_if_set_param(ctx, VENC_SET_PARAM_OPERATION_RATE, &enc_prm);
		if (ctx->dev->venc_reg == 0 && ctx->dev->venc_mmdvfs_clk == 0)
			mtk_venc_dvfs_sync_vsi_data(ctx);
	}

	if (!ret && mtk_buf->param_change & MTK_ENCODE_PARAM_BITRATE_MODE) {
		enc_prm.bitratemode = mtk_buf->enc_params.bitratemode;
		mtk_v4l2_debug(1, "[%d] idx=%d, bitratemode=%d",
			ctx->id, mtk_buf->vb.vb2_buf.index, mtk_buf->enc_params.bitratemode);
		ret |= venc_if_set_param(ctx, VENC_SET_PARAM_BITRATE_MODE, &enc_prm);
	}

	if (!ret && mtk_buf->param_change & MTK_ENCODE_PARAM_ROI_ON) {
		enc_prm.roion = mtk_buf->enc_params.roion;
		mtk_v4l2_debug(1, "[%d] idx=%d, roion=%d",
			ctx->id, mtk_buf->vb.vb2_buf.index, mtk_buf->enc_params.roion);
		ret |= venc_if_set_param(ctx, VENC_SET_PARAM_ROI_ON, &enc_prm);
	}

	if (!ret && mtk_buf->param_change & MTK_ENCODE_PARAM_GRID_SIZE) {
		enc_prm.heif_grid_size = mtk_buf->enc_params.heif_grid_size;
		mtk_v4l2_debug(0, "[%d] idx=%d, heif_grid_size=%d",
			ctx->id, mtk_buf->vb.vb2_buf.index, mtk_buf->enc_params.heif_grid_size);
		ret |= venc_if_set_param(ctx, VENC_SET_PARAM_HEIF_GRID_SIZE, &enc_prm);
	}

	if (!ret && mtk_buf->param_change & MTK_ENCODE_PARAM_COLOR_DESC) {
		// avoid much copies
		enc_prm.color_desc = &mtk_buf->enc_params.color_desc;
		mtk_v4l2_debug(0, "[%d] idx=%d, color_primaries=%d range=%d",
			ctx->id, mtk_buf->vb.vb2_buf.index,
			enc_prm.color_desc->color_primaries,
			enc_prm.color_desc->full_range);
		ret |= venc_if_set_param(ctx, VENC_SET_PARAM_COLOR_DESC, &enc_prm);
	}

	if (!ret && mtk_buf->param_change & MTK_ENCODE_PARAM_TSVC) {
		enc_prm.tsvc = mtk_buf->enc_params.tsvc;
		mtk_v4l2_debug(1, "[%d] idx=%d, tsvc=%d",
			ctx->id, mtk_buf->vb.vb2_buf.index, mtk_buf->enc_params.tsvc);
		ret |= venc_if_set_param(ctx, VENC_SET_PARAM_TSVC, &enc_prm);
	}

	if (!ret && mtk_buf->param_change & MTK_ENCODE_PARAM_HIGHQUALITY) {
		enc_prm.highquality = mtk_buf->enc_params.highquality;
		mtk_v4l2_debug(1, "[%d] idx=%d, enable highquality=%d",
			ctx->id, mtk_buf->vb.vb2_buf.index, mtk_buf->enc_params.highquality);
		ret |= venc_if_set_param(ctx, VENC_SET_PARAM_ENABLE_HIGHQUALITY, &enc_prm);
	}

	if (!ret && mtk_buf->param_change & MTK_ENCODE_PARAM_MAXQP) {
		enc_prm.max_qp = mtk_buf->enc_params.max_qp;
		mtk_v4l2_debug(1, "[%d] idx=%d, max_qp=%d",
			ctx->id, mtk_buf->vb.vb2_buf.index, mtk_buf->enc_params.max_qp);
		ret |= venc_if_set_param(ctx, VENC_SET_PARAM_ADJUST_MAX_QP, &enc_prm);
	}

	if (!ret && mtk_buf->param_change & MTK_ENCODE_PARAM_MINQP) {
		enc_prm.min_qp = mtk_buf->enc_params.min_qp;
		mtk_v4l2_debug(1, "[%d] idx=%d, min_qp=%d",
			ctx->id, mtk_buf->vb.vb2_buf.index, mtk_buf->enc_params.min_qp);
		ret |= venc_if_set_param(ctx, VENC_SET_PARAM_ADJUST_MIN_QP, &enc_prm);
	}

	if (!ret && mtk_buf->param_change & MTK_ENCODE_PARAM_IP_QPDELTA) {
		enc_prm.ip_qpdelta = mtk_buf->enc_params.ip_qpdelta;
		mtk_v4l2_debug(1, "[%d] idx=%d, ip_qpdelta=%d",
			ctx->id, mtk_buf->vb.vb2_buf.index, mtk_buf->enc_params.ip_qpdelta);
		ret |= venc_if_set_param(ctx, VENC_SET_PARAM_ADJUST_I_P_QP_DELTA, &enc_prm);
	}

	if (!ret && mtk_buf->param_change & MTK_ENCODE_PARAM_FRAMELVLQP) {
		enc_prm.framelvl_qp = mtk_buf->enc_params.framelvl_qp;
		mtk_v4l2_debug(1, "[%d] idx=%d, framelvl_qp=%d",
			ctx->id, mtk_buf->vb.vb2_buf.index, mtk_buf->enc_params.framelvl_qp);
		ret |= venc_if_set_param(ctx, VENC_SET_PARAM_ADJUST_FRAME_LEVEL_QP, &enc_prm);
	}

	if (!ret && mtk_buf->param_change & MTK_ENCODE_PARAM_MBRC_TKSPD) {
		enc_prm.mbrc_tk_spd = mtk_buf->enc_params.mbrc_tk_spd;
		mtk_v4l2_debug(0, "[%d] idx=%d, mbrc_tk_spd %d",
			ctx->id, mtk_buf->vb.vb2_buf.index, mtk_buf->enc_params.mbrc_tk_spd);
		ret |= venc_if_set_param(ctx, VENC_SET_PARAM_MBRC_TKSPD, &enc_prm);
	}

	if (!ret && mtk_buf->param_change & MTK_ENCODE_PARAM_CHROMAQP) {
		enc_prm.cb_qp_offset = mtk_buf->enc_params.cb_qp_offset;
		enc_prm.cr_qp_offset = mtk_buf->enc_params.cr_qp_offset;
		mtk_v4l2_debug(0, "[%d] idx=%d, chroma qp offset cb %d cr %d",
			ctx->id, mtk_buf->vb.vb2_buf.index, mtk_buf->enc_params.cb_qp_offset,
			mtk_buf->enc_params.cr_qp_offset);
		ret |= venc_if_set_param(ctx, VENC_SET_PARAM_ADJUST_CHROMQA_QP, &enc_prm);
	}

	if (!ret && mtk_buf->param_change & MTK_ENCODE_PARAM_DUMMY_NAL) {
		enc_prm.dummynal = mtk_buf->enc_params.dummynal;
		mtk_v4l2_debug(1, "[%d] idx=%d, dummynal=%d",
			ctx->id, mtk_buf->vb.vb2_buf.index, mtk_buf->enc_params.dummynal);
		ret |= venc_if_set_param(ctx, VENC_SET_PARAM_ENABLE_DUMMY_NAL, &enc_prm);
	}

	if (!ret && mtk_buf->param_change & MTK_ENCODE_PARAM_LOW_LATENCY_WFD) {
		enc_prm.lowlatencywfd = mtk_buf->enc_params.lowlatencywfd;
		mtk_v4l2_debug(1, "[%d] idx=%d, lowlatencywfd=%d",
			ctx->id, mtk_buf->vb.vb2_buf.index, mtk_buf->enc_params.lowlatencywfd);
		ret |= venc_if_set_param(ctx, VENC_SET_PARAM_ENABLE_LOW_LATENCY_WFD, &enc_prm);
	}

	if (!ret && mtk_buf->param_change & MTK_ENCODE_PARAM_SLICE_CNT) {
		enc_prm.slice_count = mtk_buf->enc_params.slice_count;
		mtk_v4l2_debug(1, "[%d] idx=%d, slice_count=%d",
			ctx->id, mtk_buf->vb.vb2_buf.index, mtk_buf->enc_params.slice_count);
		ret |= venc_if_set_param(ctx, VENC_SET_PARAM_SLICE_CNT, &enc_prm);
	}

	if (!ret && mtk_buf->param_change & MTK_ENCODE_PARAM_QP_CTRL_MODE) {
		enc_prm.qp_control_mode = mtk_buf->enc_params.qp_control_mode;
		mtk_v4l2_debug(0, "[%d] idx=%d, qp_control_mode=%d",
			ctx->id, mtk_buf->vb.vb2_buf.index, mtk_buf->enc_params.qp_control_mode);
		ret |= venc_if_set_param(ctx, VENC_SET_PARAM_ADJUST_QP_CONTROL_MODE, &enc_prm);
	}

	if (!ret &&
	mtk_buf->param_change & MTK_ENCODE_PARAM_VISUAL_QUALITY) {
		enc_prm.visual_quality = &mtk_buf->enc_params.visual_quality;
		mtk_v4l2_err("[%d] idx=%d, quant=%d, rd=%d",
				ctx->id,
				mtk_buf->vb.vb2_buf.index,
				enc_prm.visual_quality->quant,
				enc_prm.visual_quality->rd);
		ret |= venc_if_set_param(ctx,
					VENC_SET_PARAM_VISUAL_QUALITY,
					&enc_prm);
	}

	if (!ret &&
	mtk_buf->param_change & MTK_ENCODE_PARAM_INIT_QP) {
		enc_prm.init_qp = &mtk_buf->enc_params.init_qp;
		mtk_v4l2_err("[%d] idx=%d, initial qp enable=%d, I(%d)P(%d)B(%d)",
				ctx->id,
				mtk_buf->vb.vb2_buf.index,
				enc_prm.init_qp->enable,
				enc_prm.init_qp->qpi,
				enc_prm.init_qp->qpp,
				enc_prm.init_qp->qpb);
		ret |= venc_if_set_param(ctx,
					VENC_SET_PARAM_INIT_QP,
					&enc_prm);
	}

	if (!ret &&
	mtk_buf->param_change & MTK_ENCODE_PARAM_FRAMEQP_RANGE) {
		enc_prm.frame_qp_range = &mtk_buf->enc_params.frame_qp_range;
		mtk_v4l2_err("[%d] idx=%d, frame qp range enable=%d, max(%d), min(%d)",
				ctx->id,
				mtk_buf->vb.vb2_buf.index,
				enc_prm.frame_qp_range->enable,
				enc_prm.frame_qp_range->max,
				enc_prm.frame_qp_range->min);
		ret |= venc_if_set_param(ctx,
					VENC_SET_PARAM_FRAME_QP_RANGE,
					&enc_prm);
	}

	mtk_buf->param_change = MTK_ENCODE_PARAM_NONE;

	if (ret) {
		mtk_v4l2_err("[%d] venc_if_set_param %d failed=%d",
			ctx->id, mtk_buf->param_change, ret);
		mtk_venc_error_handle(ctx);
		return -1;
	}

	return 0;
}

void mtk_venc_check_queue_cnt(struct mtk_vcodec_ctx *ctx, struct vb2_queue *vq)
{
	int done_list_cnt = 0;
	int rdy_q_cnt = 0;
	struct vb2_buffer *vb;
	unsigned long flags;

	spin_lock_irqsave(&vq->done_lock, flags);
	list_for_each_entry(vb, &vq->done_list, queued_entry)
		done_list_cnt++;
	spin_unlock_irqrestore(&vq->done_lock, flags);

	if (vq->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
		rdy_q_cnt = ctx->m2m_ctx->out_q_ctx.num_rdy;
	else
		rdy_q_cnt = ctx->m2m_ctx->cap_q_ctx.num_rdy;

	mtk_v4l2_debug(0,
		"[%d] type %d queued_cnt %d done_cnt %d rdy_q_cnt %d tatal %d",
		ctx->id, vq->type, vq->queued_count,
		done_list_cnt, rdy_q_cnt, vq->num_buffers);
}

/*
 * v4l2_m2m_streamoff() holds dev_mutex and waits mtk_venc_worker()
 * to call v4l2_m2m_job_finish().
 * If mtk_venc_worker() tries to acquire dev_mutex, it will deadlock.
 * So this function must not try to acquire dev->dev_mutex.
 * This means v4l2 ioctls and mtk_venc_worker() can run at the same time.
 * mtk_venc_worker() should be carefully implemented to avoid bugs.
 */
static void mtk_venc_worker(struct work_struct *work)
{
	struct mtk_vcodec_ctx *ctx = container_of(work, struct mtk_vcodec_ctx,
					encode_work);
	struct vb2_buffer *src_buf, *dst_buf;
	struct venc_frm_buf *pfrm_buf;
	struct mtk_vcodec_mem *pbs_buf;
	struct venc_done_result enc_result;
	int ret, i;
	struct vb2_v4l2_buffer *dst_vb2_v4l2, *src_vb2_v4l2, *pend_src_vb2_v4l2;
	struct mtk_video_enc_buf *dst_buf_info, *src_buf_info;

	mutex_lock(&ctx->worker_lock);
	memset(&enc_result, 0, sizeof(enc_result));
	if (mtk_vcodec_is_state(ctx, MTK_STATE_ABORT)) {
		v4l2_m2m_job_finish(ctx->dev->m2m_dev_enc, ctx->m2m_ctx);
		mtk_v4l2_debug(1, " %d", mtk_vcodec_get_state(ctx));
		mutex_unlock(&ctx->worker_lock);
		return;
	}

	/* check dst_buf, dst_buf may be removed in device_run
	 * to stored encdoe header so we need check dst_buf and
	 * call job_finish here to prevent recursion
	 */
	dst_vb2_v4l2 = v4l2_m2m_dst_buf_remove(ctx->m2m_ctx);
	if (!dst_vb2_v4l2) {
		v4l2_m2m_job_finish(ctx->dev->m2m_dev_enc, ctx->m2m_ctx);
		mutex_unlock(&ctx->worker_lock);
		return;
	}

	src_vb2_v4l2 = v4l2_m2m_src_buf_remove(ctx->m2m_ctx);
	if (!src_vb2_v4l2) {
		v4l2_m2m_job_finish(ctx->dev->m2m_dev_enc, ctx->m2m_ctx);
		mutex_unlock(&ctx->worker_lock);
		return;
	}
	src_buf = &src_vb2_v4l2->vb2_buf;
	dst_buf = &dst_vb2_v4l2->vb2_buf;

	src_buf_info = container_of(src_vb2_v4l2, struct mtk_video_enc_buf, vb);
	dst_buf_info = container_of(dst_vb2_v4l2, struct mtk_video_enc_buf, vb);

	pbs_buf = &dst_buf_info->bs_buf;
	pfrm_buf = &src_buf_info->frm_buf;

	if (mtk_v4l2_dbg_level > 0)
		pbs_buf->va = vb2_plane_vaddr(dst_buf, 0);
	pbs_buf->dma_addr = vb2_dma_contig_plane_dma_addr(dst_buf, 0);
	pbs_buf->size = (size_t)dst_buf->planes[0].length;
	pbs_buf->dmabuf = dst_buf->planes[0].dbuf;
	pbs_buf->index = dst_buf->index;
	ctx->bs_list[pbs_buf->index + 1] = (uintptr_t)pbs_buf;

	if (src_buf_info->lastframe == EOS) {
		src_buf_info->lastframe = NON_EOS;
		if (ctx->oal_vcodec == 1) {
			ret = venc_if_encode(ctx,
					 VENC_START_OPT_ENCODE_FRAME_FINAL,
					 NULL, pbs_buf, &enc_result);

			pend_src_vb2_v4l2 =
				to_vb2_v4l2_buffer(ctx->pend_src_buf);
			dst_vb2_v4l2->flags |= pend_src_vb2_v4l2->flags;
			dst_vb2_v4l2->vb2_buf.timestamp =
				pend_src_vb2_v4l2->vb2_buf.timestamp;
			dst_vb2_v4l2->timecode = pend_src_vb2_v4l2->timecode;
			dst_vb2_v4l2->sequence = pend_src_vb2_v4l2->sequence;
			dst_vb2_v4l2->flags |= V4L2_BUF_FLAG_LAST;
			if (enc_result.is_key_frm)
				dst_vb2_v4l2->flags |= V4L2_BUF_FLAG_KEYFRAME;

			if (ret) {
				dst_buf->planes[0].bytesused = 0;
				v4l2_m2m_buf_done(pend_src_vb2_v4l2,
						VB2_BUF_STATE_ERROR);
				v4l2_m2m_buf_done(dst_vb2_v4l2,
						VB2_BUF_STATE_ERROR);
				mtk_v4l2_err("last venc_if_encode failed=%d",
									ret);
				if (ret == -EIO) {
					mtk_venc_error_handle(ctx);
					venc_check_release_lock(ctx);
				}
			} else {
				dst_buf->planes[0].bytesused =
							enc_result.bs_size;
				v4l2_m2m_buf_done(pend_src_vb2_v4l2,
							VB2_BUF_STATE_DONE);
				v4l2_m2m_buf_done(dst_vb2_v4l2,
							VB2_BUF_STATE_DONE);
			}

			ctx->pend_src_buf = NULL;
		} else {
			ret = venc_if_encode(ctx,
					VENC_START_OPT_ENCODE_FRAME_FINAL,
					NULL, NULL, &enc_result);
			dst_vb2_v4l2->vb2_buf.timestamp =
				src_vb2_v4l2->vb2_buf.timestamp;
			dst_vb2_v4l2->timecode = src_vb2_v4l2->timecode;
			dst_vb2_v4l2->flags |= V4L2_BUF_FLAG_LAST;
			dst_buf->planes[0].bytesused = 0;

			if (ret) {
				mtk_v4l2_err("last venc_if_encode failed=%d",
									ret);
				if (ret == -EIO) {
					mtk_venc_error_handle(ctx);
					venc_check_release_lock(ctx);
				}
			} else if (!ctx->async_mode)
				mtk_enc_put_buf(ctx);

			mtk_venc_check_queue_cnt(ctx, src_buf->vb2_queue);
			mtk_venc_check_queue_cnt(ctx, dst_buf->vb2_queue);

			v4l2_m2m_buf_done(dst_vb2_v4l2,
				VB2_BUF_STATE_DONE);
		}
		mtk_vdec_queue_stop_enc_event(ctx);

		if (src_buf->planes[0].bytesused == 0U) {
			src_vb2_v4l2->flags |= V4L2_BUF_FLAG_LAST;
			vb2_set_plane_payload(&src_buf_info->vb.vb2_buf, 0, 0);
			v4l2_m2m_buf_done(src_vb2_v4l2,
				VB2_BUF_STATE_DONE);
		}
		v4l2_m2m_job_finish(ctx->dev->m2m_dev_enc, ctx->m2m_ctx);
		mutex_unlock(&ctx->worker_lock);
		return;
	} else if (src_buf_info->lastframe == EOS_WITH_DATA) {
		/*
		 * Getting early eos frame buffer, after encode this
		 * buffer, need to flush encoder. Use the flush_buf
		 * as normal EOS, and flush encoder.
		 */
		mtk_v4l2_debug(0, "[%d] EarlyEos: encode last frame %d",
			ctx->id, src_buf->planes[0].bytesused);
		if (ctx->enc_flush_buf->lastframe == NON_EOS) {
			ctx->enc_flush_buf->lastframe = EOS;
			src_vb2_v4l2->flags |= V4L2_BUF_FLAG_LAST;
			dst_vb2_v4l2->flags |= V4L2_BUF_FLAG_LAST;
			v4l2_m2m_buf_queue_check(ctx->m2m_ctx, &ctx->enc_flush_buf->vb);
		} else {
			mtk_v4l2_debug(1, "Stopping no need to queue enc_flush_buf.");
		}
	}

	for (i = 0; i < src_buf->num_planes ; i++) {
		pfrm_buf->fb_addr[i].va = vb2_plane_vaddr(src_buf, i) +
			(size_t)src_buf->planes[i].data_offset;
		pfrm_buf->fb_addr[i].dma_addr =
			vb2_dma_contig_plane_dma_addr(src_buf, i) +
			(size_t)src_buf->planes[i].data_offset;
		pfrm_buf->fb_addr[i].size =
				(size_t)(src_buf->planes[i].bytesused-
				src_buf->planes[i].data_offset);
		pfrm_buf->fb_addr[i].dmabuf =
				src_buf->planes[i].dbuf;
		pfrm_buf->fb_addr[i].data_offset =
				src_buf->planes[i].data_offset;

		mtk_v4l2_debug(2, "fb_addr[%d].va %p, offset %d, dma_addr %p, size %d\n",
			i, pfrm_buf->fb_addr[i].va,
			src_buf->planes[i].data_offset,
			(void *)pfrm_buf->fb_addr[i].dma_addr,
			(int)pfrm_buf->fb_addr[i].size);
	}
	pfrm_buf->num_planes = src_buf->num_planes;
	pfrm_buf->timestamp = src_vb2_v4l2->vb2_buf.timestamp;
	pfrm_buf->index = src_buf->index;
	ctx->fb_list[pfrm_buf->index + 1] = (uintptr_t)pfrm_buf;

	mtk_v4l2_debug(2,
			"Framebuf %d VA=%p PA=%llx Size=0x%zx Offset=%d;VA=%p PA=0x%llx Size=0x%zx Offset=%d;VA=%p PA=0x%llx Size=%zu Offset=%d",
			pfrm_buf->index,
			pfrm_buf->fb_addr[0].va,
			(u64)pfrm_buf->fb_addr[0].dma_addr,
			pfrm_buf->fb_addr[0].size,
			src_buf->planes[0].data_offset,
			pfrm_buf->fb_addr[1].va,
			(u64)pfrm_buf->fb_addr[1].dma_addr,
			pfrm_buf->fb_addr[1].size,
			src_buf->planes[1].data_offset,
			pfrm_buf->fb_addr[2].va,
			(u64)pfrm_buf->fb_addr[2].dma_addr,
			pfrm_buf->fb_addr[2].size,
			src_buf->planes[2].data_offset);

	ret = venc_if_encode(ctx, VENC_START_OPT_ENCODE_FRAME,
				 pfrm_buf, pbs_buf, &enc_result);
	if (ret) {
		dst_buf->planes[0].bytesused = 0;
		v4l2_m2m_buf_done(src_vb2_v4l2, VB2_BUF_STATE_ERROR);
		v4l2_m2m_buf_done(dst_vb2_v4l2, VB2_BUF_STATE_ERROR);
		mtk_v4l2_err("venc_if_encode failed=%d", ret);
		if (ret == -EIO) {
			mtk_venc_error_handle(ctx);
			venc_check_release_lock(ctx);
		}
	} else if (!ctx->async_mode)
		mtk_enc_put_buf(ctx);

	mtk_v4l2_debug(1, "<=== src_buf[%d] dst_buf[%d] venc_if_encode ret=%d Size=%u===>",
			src_buf->index, dst_buf->index, ret,
			enc_result.bs_size);

	v4l2_m2m_job_finish(ctx->dev->m2m_dev_enc, ctx->m2m_ctx);

	mutex_unlock(&ctx->worker_lock);
}

static void m2mops_venc_device_run(void *priv)
{
	struct mtk_vcodec_ctx *ctx = priv;

	mtk_venc_param_change(ctx);

	if ((ctx->q_data[MTK_Q_DATA_DST].fmt->fourcc == V4L2_PIX_FMT_H264 ||
	     ctx->q_data[MTK_Q_DATA_DST].fmt->fourcc == V4L2_PIX_FMT_HEVC ||
	     ctx->q_data[MTK_Q_DATA_DST].fmt->fourcc == V4L2_PIX_FMT_HEIF ||
	     ctx->q_data[MTK_Q_DATA_DST].fmt->fourcc == V4L2_PIX_FMT_MPEG4 ||
	     ctx->q_data[MTK_Q_DATA_DST].fmt->fourcc == V4L2_PIX_FMT_H263) &&
	    mtk_vcodec_is_state(ctx, MTK_STATE_INIT)) {
		/* encode h264 sps/pps header */
		mtk_venc_encode_header(ctx);
		queue_work(ctx->dev->encode_workqueue, &ctx->encode_work);
		return;
	}

	queue_work(ctx->dev->encode_workqueue, &ctx->encode_work);
}

static int m2mops_venc_job_ready(void *m2m_priv)
{
	struct mtk_vcodec_ctx *ctx = m2m_priv;

	// FREE || STOP || ABORT
	if (!mtk_vcodec_state_in_range(ctx, MTK_STATE_INIT, MTK_STATE_FLUSH)) {
		mtk_v4l2_debug(4, "[%d] Not ready: state=0x%x.",
			ctx->id, mtk_vcodec_get_state(ctx));
		return 0;
	}

	return 1;
}

static void m2mops_venc_job_abort(void *priv)
{
	struct mtk_vcodec_ctx *ctx = priv;

	mtk_vcodec_set_state_except(ctx, MTK_STATE_STOP, MTK_STATE_FREE);
	mtk_v4l2_debug(4, "[%d] state %d", ctx->id, mtk_vcodec_get_state(ctx));
}

const struct v4l2_m2m_ops mtk_venc_m2m_ops = {
	.device_run     = m2mops_venc_device_run,
	.job_ready      = m2mops_venc_job_ready,
	.job_abort      = m2mops_venc_job_abort,
};

int mtk_vcodec_enc_set_config_data(struct mtk_vcodec_ctx *ctx, char *data)
{
	int ret = 0;
	struct venc_enc_param enc_prm;

	memset(&enc_prm, 0, sizeof(enc_prm));
	enc_prm.config_data = data;

	ret = venc_if_set_param(ctx, VENC_SET_PARAM_CONFIG, &enc_prm);
	if (ret)
		mtk_v4l2_err("[%s] failed=%d", __func__, ret);

	return ret;
}

void mtk_vcodec_enc_set_default_params(struct mtk_vcodec_ctx *ctx)
{
	struct mtk_q_data *q_data;

	ctx->m2m_ctx->q_lock = &ctx->q_mutex;
	ctx->fh.m2m_ctx = ctx->m2m_ctx;
	ctx->fh.ctrl_handler = &ctx->ctrl_hdl;
	INIT_WORK(&ctx->encode_work, mtk_venc_worker);

	ctx->colorspace = V4L2_COLORSPACE_REC709;
	ctx->ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	ctx->quantization = V4L2_QUANTIZATION_DEFAULT;
	ctx->xfer_func = V4L2_XFER_FUNC_DEFAULT;

	get_supported_format(ctx);
	get_supported_framesizes(ctx);
	get_supported_cap_common(ctx);

#if IS_ENABLED(CONFIG_MTK_TINYSYS_VCP_SUPPORT)
	if (mtk_vcodec_is_vcp(MTK_INST_ENCODER)) {
		set_venc_vcp_data(ctx, VENC_VCP_LOG_INFO_ID);
		set_venc_vcp_data(ctx, VENC_SET_PROP_MEM_ID);
	}
#endif

	q_data = &ctx->q_data[MTK_Q_DATA_SRC];
	memset(q_data, 0, sizeof(struct mtk_q_data));
	q_data->visible_width = DFT_CFG_WIDTH;
	q_data->visible_height = DFT_CFG_HEIGHT;
	q_data->coded_width = DFT_CFG_WIDTH;
	q_data->coded_height = DFT_CFG_HEIGHT;
	q_data->field = V4L2_FIELD_NONE;

	q_data->fmt = &mtk_venc_formats[default_out_fmt_idx];

	v4l_bound_align_image(&q_data->coded_width,
			      MTK_VENC_MIN_W,
			      MTK_VENC_MAX_W, 4,
			      &q_data->coded_height,
			      MTK_VENC_MIN_H,
			      MTK_VENC_MAX_H, 5, 6);

	if (q_data->coded_width < DFT_CFG_WIDTH &&
	    (q_data->coded_width + 16) <= MTK_VENC_MAX_W)
		q_data->coded_width += 16;
	if (q_data->coded_height < DFT_CFG_HEIGHT &&
	    (q_data->coded_height + 32) <= MTK_VENC_MAX_H)
		q_data->coded_height += 32;

	q_data->sizeimage[0] =
		q_data->coded_width * q_data->coded_height +
		((ALIGN(q_data->coded_width, 16) * 2) * 16);
	q_data->bytesperline[0] = q_data->coded_width;
	q_data->sizeimage[1] =
		(q_data->coded_width * q_data->coded_height) / 2 +
		(ALIGN(q_data->coded_width, 16) * 16);
	q_data->bytesperline[1] = q_data->coded_width;

	q_data = &ctx->q_data[MTK_Q_DATA_DST];
	memset(q_data, 0, sizeof(struct mtk_q_data));
	q_data->coded_width = DFT_CFG_WIDTH;
	q_data->coded_height = DFT_CFG_HEIGHT;
	q_data->fmt = &mtk_venc_formats[default_cap_fmt_idx];
	q_data->field = V4L2_FIELD_NONE;
	ctx->q_data[MTK_Q_DATA_DST].sizeimage[0] =
		DFT_CFG_WIDTH * DFT_CFG_HEIGHT;
	ctx->q_data[MTK_Q_DATA_DST].bytesperline[0] = 0;

}

void mtk_vcodec_enc_custom_ctrls_check(struct v4l2_ctrl_handler *hdl,
			const struct v4l2_ctrl_config *cfg, void *priv)
{
	v4l2_ctrl_new_custom(hdl, cfg, NULL);

	if (hdl->error) {
		mtk_v4l2_debug(0, "Adding control failed %s %x %d",
			cfg->name, cfg->id, hdl->error);
	} else {
		mtk_v4l2_debug(4, "Adding control %s %x %d",
			cfg->name, cfg->id, hdl->error);
	}
}

static const struct v4l2_ctrl_config mtk_enc_vui_sar_ctrl = {
	.ops = &mtk_vcodec_enc_ctrl_ops,
	.id = V4L2_CID_MPEG_VIDEO_H264_VUI_SAR_ENABLE,
	.name = "Video encode vui sar description for Extended_SAR",
	.type = V4L2_CTRL_TYPE_U32,
	.flags = V4L2_CTRL_FLAG_WRITE_ONLY,
	.min = 0x00000000,
	.max = 0xffffffff,
	.step = 1,
	.def = 0,
	.dims = { sizeof(struct mtk_venc_vui_info)/sizeof(u32) }
};

int mtk_vcodec_enc_ctrls_setup(struct mtk_vcodec_ctx *ctx)
{
	const struct v4l2_ctrl_ops *ops = &mtk_vcodec_enc_ctrl_ops;
	struct v4l2_ctrl_handler *handler = &ctx->ctrl_hdl;
	struct v4l2_ctrl_config cfg;

	v4l2_ctrl_handler_init(handler, MTK_MAX_CTRLS_HINT);
	ctx->enc_params.bitrate = 20000000;
	v4l2_ctrl_new_std(handler, ops, V4L2_CID_MPEG_VIDEO_BITRATE,
			  0, 400000000, 1, ctx->enc_params.bitrate);
	v4l2_ctrl_new_std(handler, ops, V4L2_CID_MPEG_VIDEO_B_FRAMES,
			  0, 3, 1, 0);
	ctx->enc_params.rc_frame = 1;
	v4l2_ctrl_new_std(handler, ops, V4L2_CID_MPEG_VIDEO_FRAME_RC_ENABLE,
			  0, 1, 1, ctx->enc_params.rc_frame);
	ctx->enc_params.h264_max_qp = 51;
	v4l2_ctrl_new_std(handler, ops, V4L2_CID_MPEG_VIDEO_H264_MAX_QP,
			  0, 51, 1, ctx->enc_params.h264_max_qp);
	v4l2_ctrl_new_std(handler, ops, V4L2_CID_MPEG_VIDEO_H264_I_PERIOD,
			  0, 65535, 1, 0);
	v4l2_ctrl_new_std(handler, ops, V4L2_CID_MPEG_VIDEO_GOP_SIZE,
			  0, 65535, 1, 0);
	v4l2_ctrl_new_std(handler, ops, V4L2_CID_MPEG_VIDEO_MB_RC_ENABLE,
			  0, 1, 1, 0);
	v4l2_ctrl_new_std(handler, ops, V4L2_CID_MPEG_VIDEO_FORCE_KEY_FRAME,
			  0, 1, 1, 0);
	v4l2_ctrl_new_std_menu(handler, ops,
		V4L2_CID_MPEG_VIDEO_HEADER_MODE,
		V4L2_MPEG_VIDEO_HEADER_MODE_JOINED_WITH_1ST_FRAME,
		0, V4L2_MPEG_VIDEO_HEADER_MODE_SEPARATE);
	v4l2_ctrl_new_std_menu(handler, ops, V4L2_CID_MPEG_VIDEO_H264_PROFILE,
		V4L2_MPEG_VIDEO_H264_PROFILE_HIGH_10,
		0, V4L2_MPEG_VIDEO_H264_PROFILE_BASELINE);
	v4l2_ctrl_new_std_menu(handler, ops, V4L2_CID_MPEG_VIDEO_HEVC_PROFILE,
		V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN,
		0, V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN);
	v4l2_ctrl_new_std_menu(handler, ops, V4L2_CID_MPEG_VIDEO_MPEG4_PROFILE,
		V4L2_MPEG_VIDEO_MPEG4_PROFILE_SIMPLE,
		0, V4L2_MPEG_VIDEO_MPEG4_PROFILE_SIMPLE);
	v4l2_ctrl_new_std_menu(handler, ops, V4L2_CID_MPEG_VIDEO_H264_LEVEL,
		V4L2_MPEG_VIDEO_H264_LEVEL_6_2,
		0, V4L2_MPEG_VIDEO_H264_LEVEL_1_0);
	v4l2_ctrl_new_std_menu(handler, ops,
		V4L2_CID_MPEG_VIDEO_HEVC_LEVEL,
		V4L2_MPEG_VIDEO_HEVC_LEVEL_6_2,
		0, V4L2_MPEG_VIDEO_HEVC_LEVEL_1);
	v4l2_ctrl_new_std_menu(handler, ops,
		V4L2_CID_MPEG_VIDEO_HEVC_TIER,
		V4L2_MPEG_VIDEO_HEVC_TIER_HIGH,
		0, V4L2_MPEG_VIDEO_HEVC_TIER_MAIN);
	v4l2_ctrl_new_std_menu(handler, ops, V4L2_CID_MPEG_VIDEO_MPEG4_LEVEL,
		V4L2_MPEG_VIDEO_MPEG4_LEVEL_5,
		0, V4L2_MPEG_VIDEO_MPEG4_LEVEL_0);
	if (handler->error)
		mtk_v4l2_debug(0, "Adding control failed V4L2_CID_MPEG_VIDEO_MPEG4_LEVEL %x %d",
			 V4L2_CID_MPEG_VIDEO_MPEG4_LEVEL, handler->error);

	v4l2_ctrl_new_std_menu(handler, ops, V4L2_CID_MPEG_VIDEO_BITRATE_MODE,
		V4L2_MPEG_VIDEO_BITRATE_MODE_CQ,
		0, V4L2_MPEG_VIDEO_BITRATE_MODE_VBR);
	if (handler->error)
		mtk_v4l2_debug(0, "Adding control failed V4L2_CID_MPEG_VIDEO_BITRATE_MODE %x %d",
			 V4L2_CID_MPEG_VIDEO_BITRATE_MODE, handler->error);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_VIDEO_MULTI_SLICE_MAX_MB;
	cfg.type = V4L2_CTRL_TYPE_INTEGER;
	cfg.flags = V4L2_CTRL_FLAG_WRITE_ONLY;
	cfg.name = "Video encode nonrefp";
	cfg.min = 0;
	cfg.max = 65535;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	mtk_vcodec_enc_custom_ctrls_check(handler, &cfg, NULL);
	if (handler->error)
		mtk_v4l2_debug(0, "Adding control failed V4L2_CID_MPEG_VIDEO_MULTI_SLICE_MAX_MB %x %d",
			 V4L2_CID_MPEG_VIDEO_BITRATE_MODE, handler->error);

	mtk_vcodec_enc_custom_ctrls_check(handler,
		&mtk_enc_vui_sar_ctrl, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_ENCODE_SCENARIO;
	cfg.type = V4L2_CTRL_TYPE_INTEGER;
	cfg.flags = V4L2_CTRL_FLAG_WRITE_ONLY;
	cfg.name = "Video encode scenario";
	cfg.min = 0;
	cfg.max = 32;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	mtk_vcodec_enc_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_ENCODE_NONREFP;
	cfg.type = V4L2_CTRL_TYPE_INTEGER;
	cfg.flags = V4L2_CTRL_FLAG_WRITE_ONLY;
	cfg.name = "Video encode nonrefp";
	cfg.min = 0;
	cfg.max = 32;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	mtk_vcodec_enc_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_ENCODE_NONREFP_FREQ;
	cfg.type = V4L2_CTRL_TYPE_INTEGER;
	cfg.flags = V4L2_CTRL_FLAG_WRITE_ONLY;
	cfg.name = "Video encode nonrefp";
	cfg.min = 0;
	cfg.max = 32;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	mtk_vcodec_enc_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_ENCODE_DETECTED_FRAMERATE;
	cfg.type = V4L2_CTRL_TYPE_INTEGER;
	cfg.flags = V4L2_CTRL_FLAG_WRITE_ONLY;
	cfg.name = "Video encode detect framerate";
	cfg.min = 0;
	cfg.max = 32;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	mtk_vcodec_enc_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_ENCODE_RFS_ON;
	cfg.type = V4L2_CTRL_TYPE_INTEGER;
	cfg.flags = V4L2_CTRL_FLAG_WRITE_ONLY;
	cfg.name = "Video encode slice loss indication";
	cfg.min = 0;
	cfg.max = 1;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	mtk_vcodec_enc_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_VIDEO_PREPEND_SPSPPS_TO_IDR;
	cfg.type = V4L2_CTRL_TYPE_INTEGER;
	cfg.flags = V4L2_CTRL_FLAG_WRITE_ONLY;
	cfg.name = "Video encode slice loss indication";
	cfg.min = 0;
	cfg.max = 1;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	mtk_vcodec_enc_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_ENCODE_OPERATION_RATE;
	cfg.type = V4L2_CTRL_TYPE_INTEGER;
	cfg.flags = V4L2_CTRL_FLAG_WRITE_ONLY;
	cfg.name = "Video encode operation rate";
	cfg.min = 0;
	cfg.max = 2048;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	mtk_vcodec_enc_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_ENCODE_ROI_ON;
	cfg.type = V4L2_CTRL_TYPE_INTEGER;
	cfg.flags = V4L2_CTRL_FLAG_WRITE_ONLY;
	cfg.name = "Video encode roi switch";
	cfg.min = 0;
	cfg.max = 8;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	mtk_vcodec_enc_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_ENCODE_GRID_SIZE;
	cfg.type = V4L2_CTRL_TYPE_INTEGER;
	cfg.flags = V4L2_CTRL_FLAG_WRITE_ONLY;
	cfg.name = "Video encode heif grid size";
	cfg.min = 0;
	cfg.max = (3840<<16)+2176;
	cfg.step = 16;
	cfg.def = 0;
	cfg.ops = ops;
	mtk_vcodec_enc_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_COLOR_DESC;
	cfg.type = V4L2_CTRL_TYPE_U32;
	cfg.flags = V4L2_CTRL_FLAG_WRITE_ONLY;
	cfg.name = "Video encode color description for HDR";
	cfg.min = 0x00000000;
	cfg.max = 0xffffffff;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	cfg.dims[0] = (sizeof(struct mtk_color_desc)/sizeof(u32));
	mtk_vcodec_enc_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_MAX_WIDTH;
	cfg.type = V4L2_CTRL_TYPE_INTEGER;
	cfg.flags = V4L2_CTRL_FLAG_WRITE_ONLY;
	cfg.name = "Video encode max width";
	cfg.min = 0;
	cfg.max = 3840;
	cfg.step = 16;
	cfg.def = 0;
	cfg.ops = ops;
	mtk_vcodec_enc_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_MAX_HEIGHT;
	cfg.type = V4L2_CTRL_TYPE_INTEGER;
	cfg.flags = V4L2_CTRL_FLAG_WRITE_ONLY;
	cfg.name = "Video encode max height";
	cfg.min = 0;
	cfg.max = 3840;
	cfg.step = 16;
	cfg.def = 0;
	cfg.ops = ops;
	mtk_vcodec_enc_custom_ctrls_check(handler, &cfg, NULL);

	ctx->enc_params.i_qp = 51;
	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_ENCODE_RC_I_FRAME_QP;
	cfg.type = V4L2_CTRL_TYPE_INTEGER;
	cfg.flags = V4L2_CTRL_FLAG_WRITE_ONLY;
	cfg.name = "I-Frame QP Value";
	cfg.min = 0;
	cfg.max = 51;
	cfg.step = 1;
	cfg.def = ctx->enc_params.i_qp;
	cfg.ops = ops;
	mtk_vcodec_enc_custom_ctrls_check(handler, &cfg, NULL);

	ctx->enc_params.p_qp = 51;
	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_ENCODE_RC_P_FRAME_QP;
	cfg.type = V4L2_CTRL_TYPE_INTEGER;
	cfg.flags = V4L2_CTRL_FLAG_WRITE_ONLY;
	cfg.name = "P-Frame QP Value";
	cfg.min = 0;
	cfg.max = 51;
	cfg.step = 1;
	cfg.def = ctx->enc_params.p_qp;
	cfg.ops = ops;
	mtk_vcodec_enc_custom_ctrls_check(handler, &cfg, NULL);

	ctx->enc_params.b_qp = 51;
	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_ENCODE_RC_B_FRAME_QP;
	cfg.type = V4L2_CTRL_TYPE_INTEGER;
	cfg.flags = V4L2_CTRL_FLAG_WRITE_ONLY;
	cfg.name = "B-Frame QP Value";
	cfg.min = 0;
	cfg.max = 51;
	cfg.step = 1;
	cfg.def = ctx->enc_params.b_qp;
	cfg.ops = ops;
	mtk_vcodec_enc_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_SEC_ENCODE;
	cfg.type = V4L2_CTRL_TYPE_INTEGER;
	cfg.flags = V4L2_CTRL_FLAG_WRITE_ONLY;
	cfg.name = "Video Sec Encode path";
	cfg.min = 0;
	cfg.max = 2;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	mtk_vcodec_enc_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_ENCODE_MAX_LTR_FRAMES;
	cfg.type = V4L2_CTRL_TYPE_INTEGER;
	cfg.flags = V4L2_CTRL_FLAG_WRITE_ONLY;
	cfg.name = "Video encode qp control mode";
	cfg.min = 0;
	cfg.max = 3;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	mtk_vcodec_enc_custom_ctrls_check(handler, &cfg, NULL);
	if (handler->error)
		mtk_v4l2_debug(0, "Adding control failed V4L2_CID_MPEG_MTK_ENCODE_MAX_LTR_FRAMES %x %d",
			 V4L2_CID_MPEG_MTK_ENCODE_MAX_LTR_FRAMES, handler->error);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_VIDEO_ENABLE_TSVC;
	cfg.type = V4L2_CTRL_TYPE_U32;
	cfg.flags = V4L2_CTRL_FLAG_WRITE_ONLY;
	cfg.name = "Video encode tsvc";
	cfg.min = 0;
	cfg.max = 15;
	cfg.step = 1;
	cfg.def = 0;
	cfg.dims[0] = 2;
	cfg.ops = ops;
	mtk_vcodec_enc_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_ENCODE_MULTI_REF;
	cfg.type = V4L2_CTRL_TYPE_U32;
	cfg.flags = V4L2_CTRL_FLAG_WRITE_ONLY;
	cfg.name = "Video encode multi ref";
	cfg.min = 0;
	cfg.max = 65535;
	cfg.step = 1;
	cfg.def = 0;
	cfg.dims[0] = sizeof(struct mtk_venc_multi_ref)/sizeof(u32);
	cfg.ops = ops;
	mtk_vcodec_enc_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_ENCODE_WPP_MODE;
	cfg.type = V4L2_CTRL_TYPE_INTEGER;
	cfg.flags = V4L2_CTRL_FLAG_WRITE_ONLY;
	cfg.name = "encode wpp";
	cfg.min = 0;
	cfg.max = 1;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	mtk_vcodec_enc_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_ENCODE_LOW_LATENCY_MODE;
	cfg.type = V4L2_CTRL_TYPE_INTEGER;
	cfg.flags = V4L2_CTRL_FLAG_WRITE_ONLY;
	cfg.name = "encode low latency";
	cfg.min = 0;
	cfg.max = 1;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	mtk_vcodec_enc_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_ENCODE_ENABLE_HIGHQUALITY;
	cfg.type = V4L2_CTRL_TYPE_INTEGER;
	cfg.flags = V4L2_CTRL_FLAG_WRITE_ONLY;
	cfg.name = "Video encode enable highquality";
	cfg.min = 0;
	cfg.max = 1;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	mtk_vcodec_enc_custom_ctrls_check(handler, &cfg, NULL);

	ctx->enc_params.max_qp = -1;
	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_ENCODE_RC_MAX_QP;
	cfg.type = V4L2_CTRL_TYPE_INTEGER;
	cfg.flags = V4L2_CTRL_FLAG_WRITE_ONLY;
	cfg.name = "Video encode max qp";
	cfg.min = -1;
	cfg.max = 51;
	cfg.step = 1;
	cfg.def = ctx->enc_params.max_qp;
	cfg.ops = ops;
	mtk_vcodec_enc_custom_ctrls_check(handler, &cfg, NULL);

	ctx->enc_params.min_qp = -1;
	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_ENCODE_RC_MIN_QP;
	cfg.type = V4L2_CTRL_TYPE_INTEGER;
	cfg.flags = V4L2_CTRL_FLAG_WRITE_ONLY;
	cfg.name = "Video encode min qp";
	cfg.min = -1;
	cfg.max = 51;
	cfg.step = 1;
	cfg.def = ctx->enc_params.min_qp;
	cfg.ops = ops;
	mtk_vcodec_enc_custom_ctrls_check(handler, &cfg, NULL);

	ctx->enc_params.ip_qpdelta = -1;
	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_ENCODE_RC_I_P_QP_DELTA;
	cfg.type = V4L2_CTRL_TYPE_INTEGER;
	cfg.flags = V4L2_CTRL_FLAG_WRITE_ONLY;
	cfg.name = "Video encode ip qp delta";
	cfg.min = -1;
	cfg.max = 51;
	cfg.step = 1;
	cfg.def = ctx->enc_params.ip_qpdelta;
	cfg.ops = ops;
	mtk_vcodec_enc_custom_ctrls_check(handler, &cfg, NULL);

	ctx->enc_params.framelvl_qp = -1;
	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_ENCODE_RC_FRAME_LEVEL_QP;
	cfg.type = V4L2_CTRL_TYPE_INTEGER;
	cfg.flags = V4L2_CTRL_FLAG_WRITE_ONLY;
	cfg.name = "Video encode frame level qp";
	cfg.min = -1;
	cfg.max = 51;
	cfg.step = 1;
	cfg.def = ctx->enc_params.framelvl_qp;
	cfg.ops = ops;
	mtk_vcodec_enc_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_ENCODE_RC_QP_CONTROL_MODE;
	cfg.type = V4L2_CTRL_TYPE_INTEGER;
	cfg.flags = V4L2_CTRL_FLAG_WRITE_ONLY;
	cfg.name = "Video encode qp control mode";
	cfg.min = 0;
	cfg.max = 8;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	mtk_vcodec_enc_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_ENCODE_ENABLE_DUMMY_NAL;
	cfg.type = V4L2_CTRL_TYPE_INTEGER;
	cfg.flags = V4L2_CTRL_FLAG_WRITE_ONLY;
	cfg.name = "Video encode enable dummynal";
	cfg.min = 0;
	cfg.max = 1;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	mtk_vcodec_enc_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_ENCODE_LOW_LATENCY_WFD;
	cfg.type = V4L2_CTRL_TYPE_INTEGER;
	cfg.flags = V4L2_CTRL_FLAG_WRITE_ONLY;
	cfg.name = "Video encode enable lowlatencywfd";
	cfg.min = 0;
	cfg.max = 1;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	mtk_vcodec_enc_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_LOG;
	cfg.type = V4L2_CTRL_TYPE_STRING;
	cfg.flags = V4L2_CTRL_FLAG_WRITE_ONLY;
	cfg.name = "Video Log";
	cfg.min = 0;
	cfg.max = 255;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	mtk_vcodec_enc_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_VCP_PROP;
	cfg.type = V4L2_CTRL_TYPE_STRING;
	cfg.flags = V4L2_CTRL_FLAG_WRITE_ONLY;
	cfg.name = "Video VCP Property";
	cfg.min = 0;
	cfg.max = 255;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	mtk_vcodec_enc_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_GET_LOG;
	cfg.type = V4L2_CTRL_TYPE_STRING;
	cfg.flags = V4L2_CTRL_FLAG_READ_ONLY | V4L2_CTRL_FLAG_VOLATILE;
	cfg.name = "Get Video Log";
	cfg.min = 0;
	cfg.max = LOG_PROPERTY_SIZE;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	mtk_vcodec_enc_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_GET_VCP_PROP;
	cfg.type = V4L2_CTRL_TYPE_STRING;
	cfg.flags = V4L2_CTRL_FLAG_READ_ONLY | V4L2_CTRL_FLAG_VOLATILE;
	cfg.name = "Get Video VCP Property";
	cfg.min = 0;
	cfg.max = LOG_PROPERTY_SIZE;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	mtk_vcodec_enc_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_ENCODE_TEMPORAL_LAYER_COUNT;
	cfg.type = V4L2_CTRL_TYPE_U32;
	cfg.flags = V4L2_CTRL_FLAG_WRITE_ONLY;
	cfg.name = "Video temporal layer count";
	cfg.min = 0;
	cfg.max = 6;
	cfg.step = 1;
	cfg.def = 0;
	cfg.dims[0] = 2;
	cfg.ops = ops;
	mtk_vcodec_enc_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_CALLING_PID;
	cfg.type = V4L2_CTRL_TYPE_INTEGER;
	cfg.flags = V4L2_CTRL_FLAG_WRITE_ONLY;
	cfg.name = "Video Caller Proccess ID";
	cfg.min = 0;
	cfg.max = 0x7fffffff;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	mtk_vcodec_enc_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_ENCODE_GET_MAX_B_NUM;
	cfg.type = V4L2_CTRL_TYPE_INTEGER;
	cfg.flags = V4L2_CTRL_FLAG_READ_ONLY | V4L2_CTRL_FLAG_VOLATILE;
	cfg.name = "Get Video Encoder Max B Number";
	cfg.min = 0;
	cfg.max = 3;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	mtk_vcodec_enc_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_ENCODE_GET_MAX_TEMPORAL_LAYER;
	cfg.type = V4L2_CTRL_TYPE_INTEGER;
	cfg.flags = V4L2_CTRL_FLAG_READ_ONLY | V4L2_CTRL_FLAG_VOLATILE;
	cfg.name = "Get Video Encoder Max Temporal Layer";
	cfg.min = 0;
	cfg.max = 6;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	mtk_vcodec_enc_custom_ctrls_check(handler, &cfg, NULL);

	if (handler->error) {
		mtk_v4l2_err("Init control handler fail %d",
			     handler->error);
		return handler->error;
	}

	v4l2_ctrl_handler_setup(&ctx->ctrl_hdl);
	ctx->param_change = MTK_ENCODE_PARAM_NONE;

	/* g_volatile_ctrl */
	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_ENCODE_ROI_RC_QP;
	cfg.type = V4L2_CTRL_TYPE_INTEGER;
	cfg.flags = V4L2_CTRL_FLAG_VOLATILE |
		V4L2_CTRL_FLAG_READ_ONLY;
	cfg.name = "Video encode roi rc qp";
	cfg.min = 0;
	cfg.max = 2048;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	mtk_vcodec_enc_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_RESOLUTION_CHANGE;
	cfg.type = V4L2_CTRL_TYPE_U32;
	cfg.flags = V4L2_CTRL_FLAG_VOLATILE |
		V4L2_CTRL_FLAG_READ_ONLY;
	cfg.name = "Video encode resolution change";
	cfg.min = 0x00000000;
	cfg.max = 0x00ffffff;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	cfg.dims[0] = sizeof(struct venc_resolution_change)/sizeof(u32);
	mtk_vcodec_enc_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_ENCODE_SLICE_CNT;
	cfg.type = V4L2_CTRL_TYPE_INTEGER;
	cfg.flags = V4L2_CTRL_FLAG_WRITE_ONLY;
	cfg.name = "Video encode set slice count";
	cfg.min = 1;
	cfg.max = 8;
	cfg.step = 1;
	cfg.def = 1;
	cfg.ops = ops;
	mtk_vcodec_enc_custom_ctrls_check(handler, &cfg, NULL);

	ctx->enc_params.qpvbr_upper_enable = -1;
	ctx->enc_params.qpvbr_qp_upper_threshold = -1;
	ctx->enc_params.qpvbr_qp_max_brratio  = -1;
	ctx->enc_params.qpvbr_lower_enable = -1;
	ctx->enc_params.qpvbr_qp_lower_threshold = -1;
	ctx->enc_params.qpvbr_qp_min_brratio  = -1;

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_ENCODE_QPVBR;
	cfg.type = V4L2_CTRL_TYPE_INTEGER;
	cfg.flags = V4L2_CTRL_FLAG_WRITE_ONLY;
	cfg.name = "Video encode QPVBR";
	cfg.min = -1;
	cfg.max = 255;
	cfg.step = 1;
	cfg.def = -1;
	cfg.dims[0] = 6;
	cfg.ops = ops;
	mtk_vcodec_enc_custom_ctrls_check(handler, &cfg, NULL);

	ctx->enc_params.cb_qp_offset = 99;
	ctx->enc_params.cr_qp_offset = 99;

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_ENCODE_CHROMA_QP;
	cfg.type = V4L2_CTRL_TYPE_INTEGER;
	cfg.flags = V4L2_CTRL_FLAG_WRITE_ONLY;
	cfg.name = "Video encode Chroma QP";
	cfg.min = -12;
	cfg.max = 99;
	cfg.step = 1;
	cfg.def = 99;
	cfg.dims[0] = 2;
	cfg.ops = ops;
	mtk_vcodec_enc_custom_ctrls_check(handler, &cfg, NULL);

	ctx->enc_params.mbrc_tk_spd = -1;
	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_ENCODE_MB_RC_TK_SPD;
	cfg.type = V4L2_CTRL_TYPE_INTEGER;
	cfg.flags = V4L2_CTRL_FLAG_WRITE_ONLY;
	cfg.name = "Video encode MB RC Tracking Speed";
	cfg.min = -1;
	cfg.max = 63;
	cfg.step = 1;
	cfg.def = -1;
	cfg.ops = ops;
	mtk_vcodec_enc_custom_ctrls_check(handler, &cfg, NULL);

	ctx->enc_params.ifrm_q_ltr = -1;
	ctx->enc_params.pfrm_q_ltr = -1;
	ctx->enc_params.bfrm_q_ltr = -1;

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_ENCODE_FRM_QP_LTR;
	cfg.type = V4L2_CTRL_TYPE_INTEGER;
	cfg.flags = V4L2_CTRL_FLAG_WRITE_ONLY;
	cfg.name = "Video encode Frame QP limiter";
	cfg.min = -1;
	cfg.max = 30;
	cfg.step = 1;
	cfg.def = -1;
	cfg.dims[0] = 3;
	cfg.ops = ops;
	mtk_vcodec_enc_custom_ctrls_check(handler, &cfg, NULL);

	ctx->enc_params.visual_quality.quant = -1;
	ctx->enc_params.visual_quality.rd = -1;

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_ENCODE_VISUAL_QUALITY;
	cfg.type = V4L2_CTRL_TYPE_INTEGER;
	cfg.flags = V4L2_CTRL_FLAG_WRITE_ONLY;
	cfg.name = "Video encode Visual Quality";
	cfg.min = -1;
	cfg.max = 63;
	cfg.step = 1;
	cfg.def = -1;
	cfg.dims[0] = (sizeof(struct mtk_venc_visual_quality)/sizeof(s32));
	cfg.ops = ops;
	mtk_vcodec_enc_custom_ctrls_check(handler, &cfg, NULL);

	ctx->enc_params.init_qp.enable = -1;
	ctx->enc_params.init_qp.qpi = -1;
	ctx->enc_params.init_qp.qpp = -1;
	ctx->enc_params.init_qp.qpb = -1;

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_ENCODE_INIT_QP;
	cfg.type = V4L2_CTRL_TYPE_INTEGER;
	cfg.flags = V4L2_CTRL_FLAG_WRITE_ONLY;
	cfg.name = "Video encode Initial QP";
	cfg.min = -1;
	cfg.max = 51;
	cfg.step = 1;
	cfg.def = -1;
	cfg.dims[0] = (sizeof(struct mtk_venc_init_qp)/sizeof(s32));
	cfg.ops = ops;
	mtk_vcodec_enc_custom_ctrls_check(handler, &cfg, NULL);

	ctx->enc_params.frame_qp_range.enable = -1;
	ctx->enc_params.frame_qp_range.max = -1;
	ctx->enc_params.frame_qp_range.min = -1;

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_ENCODE_FRAME_QP_RANGE;
	cfg.type = V4L2_CTRL_TYPE_INTEGER;
	cfg.flags = V4L2_CTRL_FLAG_WRITE_ONLY;
	cfg.name = "Video encode Frame QP Range";
	cfg.min = -1;
	cfg.max = 51;
	cfg.step = 1;
	cfg.def = -1;
	cfg.dims[0] = (sizeof(struct mtk_venc_frame_qp_range)/sizeof(s32));
	cfg.ops = ops;
	mtk_vcodec_enc_custom_ctrls_check(handler, &cfg, NULL);

	ctx->enc_params.nal_length.prefer = 0;
	ctx->enc_params.nal_length.bytes = 0;
	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_SET_NAL_SIZE_LENGTH;
	cfg.type = V4L2_CTRL_TYPE_INTEGER;
	cfg.flags = V4L2_CTRL_FLAG_WRITE_ONLY;
	cfg.name = "Video encode Nal Length";
	cfg.min = 0;
	cfg.max = 5;
	cfg.step = 1;
	cfg.def = 0;
	cfg.dims[0] = (sizeof(struct mtk_venc_nal_length)/sizeof(s32));
	cfg.ops = ops;
	mtk_vcodec_enc_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_ENCODE_ENABLE_MLVEC_MODE;
	cfg.type = V4L2_CTRL_TYPE_INTEGER;
	cfg.flags = V4L2_CTRL_FLAG_WRITE_ONLY;
	cfg.name = "Video encode inputDyanmicCtrl";
	cfg.min = 0;
	cfg.max = 1;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	mtk_vcodec_enc_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_ENCODE_CONFIG_DATA;
	cfg.type = V4L2_CTRL_TYPE_U8;
	cfg.flags = V4L2_CTRL_FLAG_WRITE_ONLY;
	cfg.name = "Video encode config data";
	cfg.min = 0x0;
	cfg.max = 0xff;
	cfg.step = 1;
	cfg.def = 0x0;
	cfg.dims[0] = VENC_CONFIG_LENGTH;
	cfg.ops = ops;
	mtk_vcodec_enc_custom_ctrls_check(handler, &cfg, NULL);

	return 0;
}

static void *mtk_venc_dc_attach_dmabuf(struct vb2_buffer *vb, struct device *dev,
	struct dma_buf *dbuf, unsigned long size)
{
	void *ptr_ret;
	struct vb2_dc_buf *buf;
	struct dma_buf_attachment *dba;

	if (vb->vb2_queue->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
		mtk_dma_buf_set_name(dbuf, "venc_frame");
	else
		mtk_dma_buf_set_name(dbuf, "venc_bs");

	ptr_ret = vb2_dma_contig_memops.attach_dmabuf(vb, dev, dbuf, size);
	if (!IS_ERR_OR_NULL(ptr_ret)) {
		buf = (struct vb2_dc_buf *)ptr_ret;
		dba = (struct dma_buf_attachment *)buf->db_attach;
		/* always skip cache operations, we handle it manually */
		dba->dma_map_attrs |= DMA_ATTR_SKIP_CPU_SYNC;
	}

	return ptr_ret;
}

int mtk_vcodec_enc_queue_init(void *priv, struct vb2_queue *src_vq,
			      struct vb2_queue *dst_vq)
{
	struct mtk_vcodec_ctx *ctx = priv;
	char name[32];
	int ret;

	/* Note: VB2_USERPTR works with dma-contig because mt8173
	 * support iommu
	 * https://patchwork.kernel.org/patch/8335461/
	 * https://patchwork.kernel.org/patch/7596181/
	 */
	SNPRINTF(name, sizeof(name), "mtk_venc-%d-out", ctx->id);
	src_vq->type            = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	src_vq->io_modes        = VB2_DMABUF | VB2_MMAP | VB2_USERPTR;
	src_vq->drv_priv        = ctx;
	src_vq->buf_struct_size = sizeof(struct mtk_video_enc_buf);
	src_vq->ops             = &mtk_venc_vb2_ops;
	venc_dma_contig_memops = vb2_dma_contig_memops;
	venc_dma_contig_memops.attach_dmabuf = mtk_venc_dc_attach_dmabuf;
	src_vq->mem_ops         = &venc_dma_contig_memops;
	mtk_v4l2_debug(4, "[%s] src_vq use venc_dma_contig_memops", name);
#if (!(IS_ENABLED(CONFIG_DEVICE_MODULES_ARM_SMMU_V3)))
	// svp_mode will be raised in vidioc_venc_s_ctrl which is later than mtk_vcodec_enc_queue_init
	// init venc_sec_dma_contig_memops without checking svp_mode value to avoid could not init sec
	// dma_contig_memops which will cause input/output buffer secure handle will be 0,
	// really mem_ops init for sec will finish at vb2ops_enc_queue_setup
	if (is_disable_map_sec() && mtk_venc_is_vcu()) {
		venc_sec_dma_contig_memops = venc_dma_contig_memops;
		venc_sec_dma_contig_memops.map_dmabuf   = mtk_venc_sec_dc_map_dmabuf;
		venc_sec_dma_contig_memops.unmap_dmabuf = mtk_venc_sec_dc_unmap_dmabuf;
	}
	if (ctx->enc_params.svp_mode && is_disable_map_sec() && mtk_venc_is_vcu()) {
		src_vq->mem_ops = &venc_sec_dma_contig_memops;
		mtk_v4l2_debug(4, "src_vq use venc_sec_dma_contig_memops");
	}
#endif
	src_vq->bidirectional   = 1;
	src_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	src_vq->lock            = &ctx->q_mutex;
	src_vq->allow_zero_bytesused = 1;
	src_vq->dev             = ctx->dev->smmu_dev;

#if IS_ENABLED(CONFIG_MTK_VCODEC_DEBUG) // only support eng & userdebug
	ret = vb2_queue_init_name(src_vq, name);
#else
	ret = vb2_queue_init(src_vq);
#endif
	if (ret)
		return ret;

	SNPRINTF(name, sizeof(name), "mtk_venc-%d-cap", ctx->id);
	dst_vq->type            = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	dst_vq->io_modes        = VB2_DMABUF | VB2_MMAP | VB2_USERPTR;
	dst_vq->drv_priv        = ctx;
	dst_vq->buf_struct_size = sizeof(struct mtk_video_enc_buf);
	dst_vq->ops             = &mtk_venc_vb2_ops;
	dst_vq->mem_ops         = &venc_dma_contig_memops;
	mtk_v4l2_debug(4, "[%s] dst_vq use venc_dma_contig_memops", name);
#if (!(IS_ENABLED(CONFIG_DEVICE_MODULES_ARM_SMMU_V3)))
	if (ctx->enc_params.svp_mode && is_disable_map_sec() && mtk_venc_is_vcu()) {
		dst_vq->mem_ops     = &venc_sec_dma_contig_memops;
		mtk_v4l2_debug(4, "dst_vq use venc_sec_dma_contig_memops");
	}
#endif
	dst_vq->bidirectional   = 1;
	dst_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	dst_vq->lock            = &ctx->q_mutex;
	dst_vq->allow_zero_bytesused = 1;
#if IS_ENABLED(CONFIG_MTK_TINYSYS_VCP_SUPPORT)
	if (ctx->dev->support_acp && mtk_venc_acp_enable &&
	    !ctx->enc_params.svp_mode && vcp_get_io_device_ex(VCP_IOMMU_ACP_VENC) != NULL) {
		dst_vq->dev     = vcp_get_io_device_ex(VCP_IOMMU_ACP_VENC);
		mtk_v4l2_debug(4, "[%s] use VCP_IOMMU_ACP_VENC domain %p", name, dst_vq->dev);
	} else if (ctx->dev->iommu_domain_swtich && (ctx->dev->enc_cnt & 1)) {
		dst_vq->dev     = vcp_get_io_device_ex(VCP_IOMMU_VDEC);
		mtk_v4l2_debug(4, "[%s] use VCP_IOMMU_VDEC domain %p", name, dst_vq->dev);
	} else {
		dst_vq->dev     = vcp_get_io_device_ex(VCP_IOMMU_VENC);
		mtk_v4l2_debug(4, "[%s] use VCP_IOMMU_VENC domain %p", name, dst_vq->dev);
	}
#if IS_ENABLED(CONFIG_VIDEO_MEDIATEK_VCU)
	if (!dst_vq->dev)
		dst_vq->dev     = ctx->dev->smmu_dev;
#endif
#else
	dst_vq->dev             = ctx->dev->smmu_dev;
#endif

#if IS_ENABLED(CONFIG_MTK_VCODEC_DEBUG) // only support eng & userdebug
	ret = vb2_queue_init_name(dst_vq, name);
#else
	ret = vb2_queue_init(dst_vq);
#endif
	return ret;
}

void mtk_venc_unlock(struct mtk_vcodec_ctx *ctx, u32 hw_id)
{

	if (hw_id >= MTK_VENC_HW_NUM)
		return;

	mtk_v4l2_debug(4, "ctx %p [%d] hw_id %d sem_cnt %d, lock: %d",
		ctx, ctx->id, hw_id, ctx->dev->enc_sem[hw_id].count,
		ctx->dev->enc_hw_locked[hw_id]);


	if (hw_id < MTK_VENC_HW_NUM) {
		ctx->core_locked[hw_id] = 0;
		up(&ctx->dev->enc_sem[hw_id]);
	}

	ctx->dev->enc_hw_locked[hw_id] = VENC_LOCK_NONE;
}

int mtk_venc_lock(struct mtk_vcodec_ctx *ctx, u32 hw_id, bool sec)
{
	int ret = -1;
	enum venc_lock lock = VENC_LOCK_NONE;

	if (hw_id >= MTK_VENC_HW_NUM)
		return ret;

	if (sec != 0)
		lock = VENC_LOCK_SEC;
	else
		lock = VENC_LOCK_NORMAL;

	mtk_v4l2_debug(4, "ctx %p [%d] hw_id %d sem_cnt %d, sec: %d, lock: %d",
		ctx, ctx->id, hw_id, ctx->dev->enc_sem[hw_id].count, sec,
		ctx->dev->enc_hw_locked[hw_id]);

	if (lock != ctx->dev->enc_hw_locked[hw_id])
		ret = down_trylock(&ctx->dev->enc_sem[hw_id]);
	else
		ret = 0;

	if (ret == 0) {
		ctx->dev->enc_hw_locked[hw_id] = lock;
		ctx->core_locked[hw_id] = 1;
	}

	return ret;
}

void mtk_vcodec_enc_empty_queues(struct file *file, struct mtk_vcodec_ctx *ctx)
{
	struct vb2_buffer *dst_buf = NULL;
	struct vb2_v4l2_buffer *src_vb2_v4l2, *dst_vb2_v4l2;
	struct v4l2_fh *fh = file->private_data;

	// error handle for release before stream-off
	//  stream off both queue mannually.
	v4l2_m2m_streamoff(file, fh->m2m_ctx,
		V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
	v4l2_m2m_streamoff(file, fh->m2m_ctx,
		V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);

	while ((src_vb2_v4l2 = v4l2_m2m_src_buf_remove(ctx->m2m_ctx))) {
		if (src_vb2_v4l2 != &ctx->enc_flush_buf->vb &&
			src_vb2_v4l2->vb2_buf.state == VB2_BUF_STATE_ACTIVE)
			v4l2_m2m_buf_done(src_vb2_v4l2, VB2_BUF_STATE_ERROR);
	}

	while ((dst_vb2_v4l2 = v4l2_m2m_dst_buf_remove(ctx->m2m_ctx))) {
		dst_buf = &dst_vb2_v4l2->vb2_buf;
		dst_buf->planes[0].bytesused = 0;
		if (dst_vb2_v4l2->vb2_buf.state == VB2_BUF_STATE_ACTIVE)
			v4l2_m2m_buf_done(dst_vb2_v4l2, VB2_BUF_STATE_ERROR);
	}

	mtk_vcodec_set_state(ctx, MTK_STATE_FREE);
}

void mtk_vcodec_enc_release(struct mtk_vcodec_ctx *ctx)
{
	int ret = venc_if_deinit(ctx);

	if (ret)
		mtk_v4l2_err("venc_if_deinit failed=%d", ret);

	release_all_general_buffer_info(ctx);
}

MODULE_IMPORT_NS(DMA_BUF);
MODULE_LICENSE("GPL v2");

