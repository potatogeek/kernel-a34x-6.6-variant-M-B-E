// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2016 MediaTek Inc.
 * Author: PC Chen <pc.chen@mediatek.com>
 *         Tiffany Lin <tiffany.lin@mediatek.com>
 */

#include <media/v4l2-event.h>
#include <media/v4l2-mem2mem.h>
#include <linux/delay.h>
#include <linux/ktime.h>
#include <linux/module.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <mtk_heap.h>

#include "mtk_heap.h"
#include "iommu_pseudo.h"

#include "mtk_vcodec_drv.h"
#include "mtk_vcodec_dec.h"
#include "mtk_vcodec_intr.h"
#include "mtk_vcodec_util.h"
#include "mtk_vcodec_dec_pm.h"
#include "mtk_vcodec_dec_pm_plat.h"
#include "vdec_drv_if.h"
#include "mtk_vcodec_dec_slc.h"
/* SMMU related header file */
#include "mtk-smmu-v3.h"
#if IS_ENABLED(CONFIG_MTK_TINYSYS_VCP_SUPPORT)
#include "vcp_status.h"
#endif


#define MTK_VDEC_MIN_W  64U
#define MTK_VDEC_MIN_H  64U
#define DFT_CFG_WIDTH   MTK_VDEC_MIN_W
#define DFT_CFG_HEIGHT  MTK_VDEC_MIN_H
#define MTK_VDEC_MAX_W  mtk_vdec_max_width
#define MTK_VDEC_MAX_H  mtk_vdec_max_heigh
#define MTK_VDEC_4K_WH  (4096 * 2176)

static unsigned int mtk_vdec_max_width;
static unsigned int mtk_vdec_max_heigh;
struct mtk_video_fmt
	mtk_vdec_formats[MTK_MAX_DEC_CODECS_SUPPORT] = { {0} };
struct mtk_codec_framesizes
	mtk_vdec_framesizes[MTK_MAX_DEC_CODECS_SUPPORT] = { {0} };
struct v4l2_vdec_max_buf_info mtk_vdec_max_buf_info = {0};
struct mtk_video_frame_frameintervals mtk_vdec_frameintervals = {0};
static unsigned int default_out_fmt_idx;
static unsigned int default_cap_fmt_idx;

int mtk_vdec_lpw_start;
int mtk_vdec_lpw_start_limit;
int mtk_vdec_lpw_limit = MTK_VDEC_GROUP_CNT;
int mtk_vdec_lpw_timeout = MTK_VDEC_WAIT_GROUP_MS;
/* For vdec low power mode (group decode) dynamic low latency enable*/
bool mtk_vdec_enable_dynll = true;

#define NUM_SUPPORTED_FRAMESIZE ARRAY_SIZE(mtk_vdec_framesizes)
#define NUM_FORMATS ARRAY_SIZE(mtk_vdec_formats)
static struct vb2_mem_ops vdec_dma_contig_memops;

#if (!(IS_ENABLED(CONFIG_DEVICE_MODULES_ARM_SMMU_V3)))
static struct vb2_mem_ops vdec_sec_dma_contig_memops;

static int mtk_vdec_sec_dc_map_dmabuf(void *mem_priv)
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
	mtk_v4l2_debug(8, "buf=%p, secure_handle=%pad", buf, &buf->dma_addr);
	return 0;
}

static void mtk_vdec_sec_dc_unmap_dmabuf(void *mem_priv)
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

	mtk_v4l2_debug(8, "buf=%p, secure_handle=%pad", buf, &buf->dma_addr);
	buf->dma_addr = 0;
	buf->dma_sgt = NULL;
}
#endif

static bool mtk_vdec_is_vcu(void)
{
	if (VCU_FPTR(vcu_get_plat_device)) {
		if (mtk_vcodec_is_vcp(MTK_INST_DECODER))
			return false;
		else
			return true;
	}
	return false;
}

static unsigned int vdec_init_no_delay(void)
{
#if IS_ENABLED(CONFIG_MTK_TINYSYS_VCP_SUPPORT)
	return is_vcp_ao_ex();
#endif
	return true;
}

static inline long long timeval_to_ns(const struct __kernel_v4l2_timeval *tv)
{
	return ((long long) tv->tv_sec * NSEC_PER_SEC) +
		tv->tv_usec * NSEC_PER_USEC;
}

void mtk_vdec_do_gettimeofday(struct timespec64 *tv)
{
	struct timespec64 now;

	ktime_get_real_ts64(&now);
	tv->tv_sec = now.tv_sec;
	tv->tv_nsec = now.tv_nsec; // micro sec = ((long)(now.tv_nsec)/1000);
}

#if IS_ENABLED(CONFIG_MTK_TINYSYS_VCP_SUPPORT)
static void set_vdec_vcp_data(struct mtk_vcodec_ctx *ctx, enum vcp_reserve_mem_id_t id)
{
	char tmp_buf[LOG_PROPERTY_SIZE] = "";

	if (id == VDEC_SET_PROP_MEM_ID) {

		SNPRINTF(tmp_buf, LOG_PROPERTY_SIZE, "%s", mtk_vdec_property);

		mtk_v4l2_debug(3, "[%d] mtk_vdec_property %s", ctx->id, tmp_buf);
		mtk_v4l2_debug(3, "[%d] mtk_vdec_property_prev %s",
					ctx->id, mtk_vdec_property_prev);

		// set vcp property every time
		if (/* strcmp(mtk_vdec_property_prev, tmp_buf) != 0 &&  */
			strlen(tmp_buf) > 0) {
			if (vdec_if_set_param(ctx,
				SET_PARAM_VDEC_PROPERTY,
				tmp_buf)  != 0) {
				mtk_v4l2_err("Error!! Cannot set vdec property");
				return;
			}
			SNPRINTF(mtk_vdec_property_prev, LOG_PROPERTY_SIZE, "%s", tmp_buf);
		}
	} else if (id == VDEC_VCP_LOG_INFO_ID) {

		SNPRINTF(tmp_buf, LOG_PROPERTY_SIZE, "%s", mtk_vdec_vcp_log);

		mtk_v4l2_debug(3, "[%d] mtk_vdec_vcp_log %s", ctx->id, tmp_buf);
		mtk_v4l2_debug(3, "[%d] mtk_vdec_vcp_log_prev %s", ctx->id, mtk_vdec_vcp_log_prev);

		// set vcp log every time
		if (/* strcmp(mtk_vdec_vcp_log_prev, tmp_buf) != 0 &&  */
			strlen(tmp_buf) > 0) {
			if (vdec_if_set_param(ctx,
				SET_PARAM_VDEC_VCP_LOG_INFO,
				tmp_buf)  != 0) {
				mtk_v4l2_err("Error!! Cannot set vdec vcp log info");
				return;
			}
			SNPRINTF(mtk_vdec_vcp_log_prev, LOG_PROPERTY_SIZE, "%s", tmp_buf);
		}
	} else
		mtk_v4l2_err("[%d] id not support %d", ctx->id, id);
}
#endif

static void set_vcu_vpud_log(struct mtk_vcodec_ctx *ctx, void *in)
{
	if (!mtk_vdec_is_vcu()) {
		mtk_v4l2_err("only support on vcu dec path");
		return;
	}

	if (vdec_if_set_param(ctx, SET_PARAM_VDEC_VCU_VPUD_LOG, in))
		mtk_v4l2_err("[%d] Error!! Cannot set param : SET_PARAM_VDEC_VCU_VPUD_LOG ERR", ctx->id);
}

static void get_vcu_vpud_log(struct mtk_vcodec_ctx *ctx, void *out)
{
	if (!mtk_vdec_is_vcu()) {
		mtk_v4l2_err("only support on vcu dec path");
		return;
	}

	if (vdec_if_get_param(ctx, GET_PARAM_VDEC_VCU_VPUD_LOG, out) != 0)
		mtk_v4l2_err("[%d] Error!! Cannot get param : GET_PARAM_VDEC_VCU_VPUD_LOG ERR", ctx->id);
}

static void get_supported_format(struct mtk_vcodec_ctx *ctx)
{
	unsigned int i;

	if (mtk_vdec_formats[0].fourcc == 0) {
		if (vdec_if_get_param(ctx,
			GET_PARAM_VDEC_CAP_SUPPORTED_FORMATS,
			&mtk_vdec_formats)  != 0) {
			mtk_v4l2_err("Error!! Cannot get supported format");
			return;
		}

		for (i = 0; i < MTK_MAX_DEC_CODECS_SUPPORT; i++) {
			if (mtk_vdec_formats[i].fourcc != 0)
				mtk_v4l2_debug(1, "fmt[%d] fourcc %s(0x%x) type %d planes %d",
					i, FOURCC_STR(mtk_vdec_formats[i].fourcc), mtk_vdec_formats[i].fourcc,
					mtk_vdec_formats[i].type,
					mtk_vdec_formats[i].num_planes);
		}
		for (i = 0; i < MTK_MAX_DEC_CODECS_SUPPORT; i++) {
			if (mtk_vdec_formats[i].fourcc != 0 &&
				mtk_vdec_formats[i].type == MTK_FMT_DEC) {
				default_out_fmt_idx = i;
				break;
			}
		}
		for (i = 0; i < MTK_MAX_DEC_CODECS_SUPPORT; i++) {
			if (mtk_vdec_formats[i].fourcc != 0 &&
				mtk_vdec_formats[i].type == MTK_FMT_FRAME) {
				default_cap_fmt_idx = i;
				break;
			}
		}
	}
}

static void get_supported_framesizes(struct mtk_vcodec_ctx *ctx)
{
	unsigned int i;

	if (mtk_vdec_framesizes[0].fourcc == 0) {
		if (vdec_if_get_param(ctx, GET_PARAM_VDEC_CAP_FRAME_SIZES,
			&mtk_vdec_framesizes) != 0) {
			mtk_v4l2_err("[%d] Error!! Cannot get frame size",
				ctx->id);
			return;
		}

		for (i = 0; i < MTK_MAX_DEC_CODECS_SUPPORT; i++) {
			if (mtk_vdec_framesizes[i].fourcc != 0) {
				mtk_v4l2_debug(1, "vdec_fs[%d] fourcc %s(0x%x) s %d %d %d %d %d %d\n",
					i, FOURCC_STR(mtk_vdec_framesizes[i].fourcc), mtk_vdec_framesizes[i].fourcc,
					mtk_vdec_framesizes[i].stepwise.min_width,
					mtk_vdec_framesizes[i].stepwise.max_width,
					mtk_vdec_framesizes[i].stepwise.step_width,
					mtk_vdec_framesizes[i].stepwise.min_height,
					mtk_vdec_framesizes[i].stepwise.max_height,
					mtk_vdec_framesizes[i].stepwise.step_height);
				if (mtk_vdec_framesizes[i].stepwise.max_width > mtk_vdec_max_width)
					mtk_vdec_max_width =
						mtk_vdec_framesizes[i].stepwise.max_width;
				if (mtk_vdec_framesizes[i].stepwise.max_height > mtk_vdec_max_heigh)
					mtk_vdec_max_heigh =
						mtk_vdec_framesizes[i].stepwise.max_height;
			}
		}
	}
}

static struct mtk_video_fmt *mtk_vdec_find_format(struct mtk_vcodec_ctx *ctx,
	struct v4l2_format *f, unsigned int t)
{
	struct mtk_video_fmt *fmt;
	unsigned int k;

	mtk_v4l2_debug(2, "[%d] fourcc %s(0x%x)",ctx->id,
		FOURCC_STR(f->fmt.pix_mp.pixelformat), f->fmt.pix_mp.pixelformat);
	for (k = 0; k < MTK_MAX_DEC_CODECS_SUPPORT &&
		 mtk_vdec_formats[k].fourcc != 0; k++) {
		fmt = &mtk_vdec_formats[k];
		if (fmt->fourcc == f->fmt.pix_mp.pixelformat &&
			mtk_vdec_formats[k].type == t)
			return fmt;
	}

	return NULL;
}

static struct mtk_video_fmt *mtk_find_fmt_by_pixel(unsigned int pixelformat)
{
	struct mtk_video_fmt *fmt;
	unsigned int k;

	for (k = 0; k < NUM_FORMATS; k++) {
		fmt = &mtk_vdec_formats[k];
		if (fmt->fourcc == pixelformat)
			return fmt;
	}
	mtk_v4l2_err("Error!! Cannot find fourcc: 0x%x use default", pixelformat);

	return &mtk_vdec_formats[default_out_fmt_idx];
}

static struct mtk_q_data *mtk_vdec_get_q_data(struct mtk_vcodec_ctx *ctx,
	enum v4l2_buf_type type)
{
	if (ctx == NULL)
		return NULL;

	if (V4L2_TYPE_IS_OUTPUT(type))
		return &ctx->q_data[MTK_Q_DATA_SRC];

	return &ctx->q_data[MTK_Q_DATA_DST];
}

static bool mtk_vdec_is_ts_valid(u64 timestamp)
{
	struct timespec64 ts;
	u64 timestamp_us;

	ts = ns_to_timespec64(timestamp);
	timestamp_us = ts.tv_sec * USEC_PER_SEC + ts.tv_nsec / NSEC_PER_USEC;

	return timestamp_us != MTK_INVALID_TIMESTAMP;
}

static void mtk_vdec_ts_reset(struct mtk_vcodec_ctx *ctx)
{
	struct mtk_detect_ts_param *param = &ctx->detect_ts_param;

	mutex_lock(&param->lock);
	if (ctx->detect_ts_param.enable_detect_ts)
		param->mode = MTK_TS_MODE_DETECTING;
	else
		param->mode = MTK_TS_MODE_PTS;
	param->read_idx = 0;
	param->recorded_size = 0;
	param->first_disp_ts = MTK_INVALID_TIMESTAMP;
	mtk_v4l2_debug(2, "[%d] reset to mode %d", ctx->id, param->mode);
	mutex_unlock(&param->lock);
}

static void mtk_vdec_ts_insert(struct mtk_vcodec_ctx *ctx, u64 timestamp)
{
	struct mtk_detect_ts_param *param = &ctx->detect_ts_param;
	int write_idx;

	mutex_lock(&param->lock);
	if (param->mode == MTK_TS_MODE_PTS) {
		mutex_unlock(&param->lock);
		return;
	}

	if (param->recorded_size) {
		int last_write_idx;
		u64 last_timestamp;

		last_write_idx = (param->read_idx + param->recorded_size - 1) % VB2_MAX_FRAME;
		last_timestamp = param->record[last_write_idx];
		if (last_timestamp >= timestamp) {
			mtk_v4l2_debug(2, "[%d] not increasing ts %lld -> %lld, choose pts mode",
				ctx->id, last_timestamp, timestamp);
			param->mode = MTK_TS_MODE_PTS;
			mutex_unlock(&param->lock);
			return;
		}
	}

	write_idx = (param->read_idx + param->recorded_size) % VB2_MAX_FRAME;
	param->record[write_idx] = timestamp;
	param->recorded_size++;
	mtk_v4l2_debug(2, "record ts %lld at index %d size %d",
		timestamp, write_idx, param->recorded_size);
	if (param->recorded_size > VB2_MAX_FRAME) {
		mtk_v4l2_err("[%d] ts record size is too large", ctx->id);
		param->recorded_size = VB2_MAX_FRAME;
	}
	mutex_unlock(&param->lock);
}

static void mtk_vdec_ts_remove_last(struct mtk_vcodec_ctx *ctx)
{
	struct mtk_detect_ts_param *param = &ctx->detect_ts_param;
	int remove_idx;

	mutex_lock(&param->lock);
	if (param->mode == MTK_TS_MODE_PTS) {
		mutex_unlock(&param->lock);
		return;
	}

	// in case bitstream is skipped before the first src_chg
	if (param->recorded_size == 0) {
		mtk_v4l2_debug(2, "[%d] skip due to ts record size is 0", ctx->id);
		mutex_unlock(&param->lock);
		return;
	}

	remove_idx = (param->read_idx + param->recorded_size - 1) % VB2_MAX_FRAME;
	mtk_v4l2_debug(2, "remove last ts %lld at index %d size %d",
		param->record[remove_idx], remove_idx, param->recorded_size);
	param->recorded_size--;
	mutex_unlock(&param->lock);
}

static u64 mtk_vdec_ts_update_mode_and_timestamp(struct mtk_vcodec_ctx *ctx, u64 pts)
{
	struct mtk_detect_ts_param *param = &ctx->detect_ts_param;
	u64 dts, timestamp;

	mutex_lock(&param->lock);

	if (param->mode == MTK_TS_MODE_PTS) {
		mutex_unlock(&param->lock);
		return pts;
	}

	/* DTV case, we may have invalid output timestamp even if all valid input timestamp */
	if (!mtk_vdec_is_ts_valid(pts)) {
		mtk_v4l2_debug(2, "[%d] got invalid pts, choose pts mode", ctx->id);
		param->mode = MTK_TS_MODE_PTS;
		mutex_unlock(&param->lock);
		return pts;
	}

	if (param->recorded_size == 0) {
		mtk_v4l2_err("[%d] ts record size is 0", ctx->id);
		mutex_unlock(&param->lock);
		return pts;
	}

	dts = param->record[param->read_idx];
	param->read_idx++;
	if (param->read_idx == VB2_MAX_FRAME)
		param->read_idx = 0;

	param->recorded_size--;

	do {
		if (param->mode != MTK_TS_MODE_DETECTING)
			break;

		if (!mtk_vdec_is_ts_valid(param->first_disp_ts)) {
			WARN_ON(!mtk_vdec_is_ts_valid(pts));
			param->first_disp_ts = pts;
			/* for DTS mode, there should be no reorder, so the first disp frame
			 * must be the first decode frame which means pts == dts
			 */
			if (pts != dts) {
				param->mode = MTK_TS_MODE_PTS;
				mtk_v4l2_debug(2, "[%d] first pts %lld != dts %lld. choose pts mode",
					ctx->id, pts, dts);
				break;
			}
		}

		if (pts < dts) {
			param->mode = MTK_TS_MODE_PTS;
			mtk_v4l2_debug(2, "[%d] pts %lld < dts %lld. choose pts mode",
				ctx->id, pts, dts);
		} else if (dts < pts) {
			param->mode = MTK_TS_MODE_DTS;
			mtk_v4l2_debug(2, "[%d] dts %lld < pts %lld. choose dts mode",
				ctx->id, dts, pts);
		}
	} while (0);

	if (param->mode == MTK_TS_MODE_DTS) {
		timestamp = dts;
		mtk_v4l2_debug(2, "use dts %lld instead of pts %lld", dts, pts);
	} else
		timestamp = pts;

	mutex_unlock(&param->lock);

	return timestamp;
}

static int mtk_vdec_get_lpw_limit(struct mtk_vcodec_ctx *ctx)
{
	int default_limit = mtk_vdec_lpw_limit;

	if (ctx->dynamic_low_latency)
		return 1;

	if (ctx->picinfo.buf_w * ctx->picinfo.buf_h > MTK_VDEC_4K_WH)
		default_limit = mtk_vdec_lpw_limit - 2;

	return (ctx->input_slot > 0) ? MAX(1, MIN(default_limit, ctx->input_slot - 2)) : default_limit;
}

static int mtk_vdec_get_lpw_start_limit(struct mtk_vcodec_ctx *ctx)
{
	if (mtk_vdec_lpw_start > 0)
		return mtk_vdec_lpw_start;

	if (mtk_vdec_lpw_start < 0)
		return ctx->dpb_size + mtk_vdec_lpw_start;

	return ctx->dpb_size;
}

static void mtk_vdec_lpw_timer_handler(struct timer_list *timer)
{
	struct mtk_vcodec_ctx *ctx = container_of(timer, struct mtk_vcodec_ctx, lpw_timer);
	int src_cnt, dst_cnt, pair_cnt;
	bool need_trigger = false;
	unsigned long flags;

	if (!ctx->low_pw_mode)
		return;

	spin_lock_irqsave(&ctx->lpw_lock, flags);
	if (ctx->lpw_timer_wait) {
		if (ctx->lpw_state == VDEC_LPW_WAIT) {
			src_cnt = v4l2_m2m_num_src_bufs_ready(ctx->m2m_ctx);
			dst_cnt = v4l2_m2m_num_dst_bufs_ready(ctx->m2m_ctx);
			pair_cnt = MIN(src_cnt, dst_cnt);
			mtk_lpw_debug(1, "[%d] timer timeup, switch lpw_state(%d) to DEC(%d)(pair cnt %d(%d,%d))",
				ctx->id, ctx->lpw_state, VDEC_LPW_DEC, pair_cnt, src_cnt, dst_cnt);
			ctx->lpw_state = VDEC_LPW_DEC;
			need_trigger = true;
		}
		ctx->lpw_timer_wait = false;
	}
	spin_unlock_irqrestore(&ctx->lpw_lock, flags);

	if (need_trigger)
		v4l2_m2m_try_schedule(ctx->m2m_ctx);
}

static void mtk_vdec_lpw_init_timer(struct mtk_vcodec_ctx *ctx)
{
	timer_setup(&ctx->lpw_timer, mtk_vdec_lpw_timer_handler, 0);
}

static void mtk_vdec_lpw_deinit_timer(struct mtk_vcodec_ctx *ctx)
{
	del_timer_sync(&ctx->lpw_timer);
}

static void mtk_vdec_lpw_set_ts(struct mtk_vcodec_ctx *ctx, u64 ts)
{
	unsigned long flags;

	if (!ctx->low_pw_mode)
		return;

	spin_lock_irqsave(&ctx->lpw_lock, flags);
	if (ctx->lpw_set_start_ts) {
		ctx->group_start_ts = ctx->group_end_ts = ts;
		ctx->lpw_set_start_ts = false;
	} else
		ctx->group_end_ts = MAX(ts, ctx->group_end_ts);

	ctx->lpw_ts_diff = ts - ctx->lpw_last_disp_ts;
	ctx->lpw_last_disp_ts = ts;
	spin_unlock_irqrestore(&ctx->lpw_lock, flags);
}

static void mtk_vdec_lpw_start_timer(struct mtk_vcodec_ctx *ctx)
{
	u64 time_diff = 0, curr_time, group_time, dec_time, delay_time, limit_time; // ns
	unsigned int limit_cnt = mtk_vdec_get_lpw_limit(ctx);
	unsigned int delay_cnt = limit_cnt / 2;

	if (!ctx->low_pw_mode || ctx->dynamic_low_latency)
		return;

	if (!ctx->in_group) {
		limit_time = ctx->lpw_ts_diff * ctx->group_dec_cnt;
		if (!ctx->lpw_timer_wait) {
			curr_time = jiffies_to_nsecs(jiffies);
			if (curr_time <= ctx->group_start_time) {
				dec_time = MS_TO_NS(10); // 10ms
				group_time = dec_time * ctx->group_dec_cnt;
				delay_time = dec_time * delay_cnt;
				mtk_lpw_err("[%d] curr_time %lld.%06d <= group_start_time %lld.%06d not valid, set dec_time to default %lld ms",
					ctx->id, NS_TO_MS(curr_time), NS_MOD_MS(curr_time),
					NS_TO_MS(ctx->group_start_time),
					NS_MOD_MS(ctx->group_start_time), NS_TO_MS(dec_time));
			} else {
				group_time = curr_time - ctx->group_start_time;
				dec_time = div_u64(group_time, ctx->group_dec_cnt);
				delay_time = dec_time * delay_cnt;
			}

			if (limit_time > delay_time)
				time_diff = MIN(MS_TO_NS(mtk_vdec_lpw_timeout), limit_time - delay_time);
			else {
				time_diff = MS_TO_NS(mtk_vdec_lpw_timeout);
				mtk_lpw_debug(1, "[%d] ts_diff %lld.%06d * group_dec_cnt %d = %lld.%06d <= delay_time %lld.%06d(dec_time %lld.%06d (= group_time %lld.%06d / group_dec_cnt %d) * delay_cnt %d), use default timeout(%lld.%06d)",
					ctx->id,
					NS_TO_MS(ctx->lpw_ts_diff), NS_MOD_MS(ctx->lpw_ts_diff),
					ctx->group_dec_cnt, NS_TO_MS(limit_time), NS_MOD_MS(limit_time),
					NS_TO_MS(delay_time), NS_MOD_MS(delay_time),
					NS_TO_MS(dec_time), NS_MOD_MS(dec_time),
					NS_TO_MS(group_time), NS_MOD_MS(group_time),
					ctx->group_dec_cnt, delay_cnt,
					NS_TO_MS(time_diff), NS_MOD_MS(time_diff));
			}
		}
		if (time_diff > 0) {
			mtk_lpw_debug(2, "[%d] start lpw_timer with timeout %lld.%06d (ts_diff %lld.%06d * group_dec_cnt %d = %lld.%06d, delay_time %lld.%06d = dec_time %lld.%06d (= group_time %lld.%06d / group_dec_cnt %d) * delay_cnt %d)",
				ctx->id, NS_TO_MS(time_diff), NS_MOD_MS(time_diff),
				NS_TO_MS(ctx->lpw_ts_diff), NS_MOD_MS(ctx->lpw_ts_diff),
				ctx->group_dec_cnt, NS_TO_MS(limit_time), NS_MOD_MS(limit_time),
				NS_TO_MS(delay_time), NS_MOD_MS(delay_time),
				NS_TO_MS(dec_time), NS_MOD_MS(dec_time),
				NS_TO_MS(group_time), NS_MOD_MS(group_time),
				ctx->group_dec_cnt, delay_cnt);
			ctx->lpw_timer_wait = true;
			mod_timer(&ctx->lpw_timer, jiffies + nsecs_to_jiffies(time_diff));
		}
	}
}

static void mtk_vdec_lpw_stop_timer(struct mtk_vcodec_ctx *ctx, bool need_lock)
{
	unsigned long flags;

	if (!ctx->low_pw_mode)
		return;

	if (need_lock)
		spin_lock_irqsave(&ctx->lpw_lock, flags);

	if (ctx->lpw_timer_wait) {
		del_timer(&ctx->lpw_timer);
		ctx->lpw_timer_wait = false;
	}

	if (need_lock)
		spin_unlock_irqrestore(&ctx->lpw_lock, flags);
}

static void mtk_vdec_lpw_switch_reset(struct mtk_vcodec_ctx *ctx, bool need_lock, char *debug_str)
{
	unsigned long flags;

	if (!ctx->low_pw_mode)
		return;

	if (need_lock)
		spin_lock_irqsave(&ctx->lpw_lock, flags);

	mtk_vdec_lpw_stop_timer(ctx, false);
	mtk_lpw_debug(1, "[%d] %s, switch lpw_state(%d) to RESET(%d)",
		ctx->id, debug_str, ctx->lpw_state, VDEC_LPW_RESET);
	ctx->lpw_state = VDEC_LPW_RESET;

	if (need_lock)
		spin_unlock_irqrestore(&ctx->lpw_lock, flags);
}

static bool mtk_vdec_lpw_check_dec_start(struct mtk_vcodec_ctx *ctx,
	bool need_lock, bool is_EOS, char *debug_str)
{
	int src_cnt, dst_cnt, pair_cnt, limit_cnt;
	unsigned long flags;
	bool has_switch = false;

	if (!ctx->low_pw_mode)
		return has_switch;

	if (need_lock)
		spin_lock_irqsave(&ctx->lpw_lock, flags);

	if (ctx->lpw_state != VDEC_LPW_WAIT && !is_EOS)
		goto check_lpw_start_done;

	src_cnt = v4l2_m2m_num_src_bufs_ready(ctx->m2m_ctx);
	dst_cnt = v4l2_m2m_num_dst_bufs_ready(ctx->m2m_ctx);
	pair_cnt = MIN(src_cnt, dst_cnt);
	limit_cnt = mtk_vdec_get_lpw_limit(ctx);

	if (is_EOS) {
		mtk_vdec_lpw_switch_reset(ctx, false, debug_str);
		has_switch = true;
	} else if (pair_cnt >= limit_cnt) {
		mtk_lpw_debug(1, "[%d] %s pair cnt %d(%d,%d) >= %d, switch lpw_state(%d) to DEC(%d)",
			ctx->id, debug_str, pair_cnt, src_cnt, dst_cnt, limit_cnt,
			ctx->lpw_state, VDEC_LPW_DEC);
		ctx->lpw_state = VDEC_LPW_DEC;
		has_switch = true;
	} else
		mtk_lpw_debug(4, "[%d] %s pair cnt %d(%d,%d) < %d, not switch lpw_state(%d)",
			ctx->id, debug_str, pair_cnt, src_cnt, dst_cnt, limit_cnt, ctx->lpw_state);

check_lpw_start_done:
	if (need_lock)
		spin_unlock_irqrestore(&ctx->lpw_lock, flags);
	return has_switch;
}

static bool mtk_vdec_lpw_check_low_latency(struct mtk_vcodec_ctx *ctx,
	int src_cnt, int dst_cnt, int pair_cnt)
{
	bool no_input = false;
	bool detect_low_latency = false;

	if (!mtk_vdec_enable_dynll)
		return false;

	if (src_cnt < dst_cnt)
		no_input = true;
	if (ctx->prev_no_input && no_input)
		detect_low_latency = true;

	if (ctx->dynamic_low_latency != detect_low_latency) {
		ctx->dynamic_low_latency = detect_low_latency;
		if (ctx->dynamic_low_latency) {
			mtk_vdec_lpw_stop_timer(ctx, false);
			ctx->lpw_state = VDEC_LPW_DEC;
			mtk_lpw_debug(0, "[%d] detect dynamic low latency, switch lpw_state to DEC(%d)(pair cnt %d(%d,%d))",
				ctx->id, ctx->lpw_state, pair_cnt, src_cnt, dst_cnt);
		} else {
			mtk_lpw_debug(0, "[%d] detect not dynamic low latency, lpw_state %d (pair cnt %d(%d,%d))",
				ctx->id, ctx->lpw_state, pair_cnt, src_cnt, dst_cnt);
		}
	}

	ctx->prev_no_input = no_input;
	return ctx->dynamic_low_latency;
}

static bool mtk_vdec_lpw_check_dec_stop(struct mtk_vcodec_ctx *ctx,
	bool need_lock, bool before_decode, char *debug_str)
{
	int src_cnt, dst_cnt, pair_cnt, limit_cnt;
	unsigned long flags;
	bool has_switch = false;

	if (!ctx->low_pw_mode)
		return has_switch;

	if (need_lock)
		spin_lock_irqsave(&ctx->lpw_lock, flags);

	src_cnt = v4l2_m2m_num_src_bufs_ready(ctx->m2m_ctx);
	dst_cnt = v4l2_m2m_num_dst_bufs_ready(ctx->m2m_ctx);
	pair_cnt = MIN(src_cnt, dst_cnt);
	limit_cnt = before_decode ? 1 : 0;
	if (before_decode)
		mtk_lpw_debug(8, "[%d] pair cnt %d(%d,%d) lpw_state(%d) lpw_dec_start_cnt %d, group_dec_cnt %d",
			ctx->id, pair_cnt, src_cnt, dst_cnt, ctx->lpw_state,
			ctx->lpw_dec_start_cnt, ctx->group_dec_cnt);

	if (!before_decode && ctx->lpw_dec_start_cnt == 0 && pair_cnt <= limit_cnt) // after decode group end
		mtk_vdec_lpw_check_low_latency(ctx, src_cnt, dst_cnt, pair_cnt);

	if (ctx->lpw_state != VDEC_LPW_DEC || ctx->dynamic_low_latency)
		goto check_lpw_stop_done;

	if (before_decode && ctx->lpw_dec_start_cnt > 0) {
		ctx->lpw_dec_start_cnt--;
		if (pair_cnt <= limit_cnt) {
			has_switch = true; // for get out of in_group
			if (ctx->lpw_dec_start_cnt <= mtk_vdec_lpw_start_limit) {
				// done driver dpb size but no more pair to decode
				mtk_lpw_debug(1, "[%d] lpw_dec_start_cnt less %d not done but no more pair cnt %d(%d,%d)",
					ctx->id, ctx->lpw_dec_start_cnt, pair_cnt, src_cnt, dst_cnt);
				ctx->lpw_dec_start_cnt = 0;
			}
		}
		if (ctx->lpw_dec_start_cnt == 0) {
			ctx->lpw_state = VDEC_LPW_WAIT;
			if (mtk_vdec_lpw_check_dec_start(ctx, false, false, debug_str) == false) {
				mtk_lpw_debug(1, "[%d] %s lpw_dec_start_cnt done, switch lpw_state to WAIT(%d)(pair cnt %d(%d,%d))",
					ctx->id, debug_str, ctx->lpw_state, pair_cnt, src_cnt, dst_cnt);
				has_switch = true;
			} else
				has_switch = false;
		}
	} else if (ctx->lpw_dec_start_cnt == 0 && pair_cnt <= limit_cnt) {
		ctx->lpw_state = VDEC_LPW_WAIT;
		has_switch = true;
		mtk_lpw_debug(1, "[%d] %s pair cnt %d(%d,%d) <= %d, switch lpw_state to WAIT(%d)",
			ctx->id, debug_str, pair_cnt, src_cnt, dst_cnt, limit_cnt, ctx->lpw_state);
	}

check_lpw_stop_done:
	if (need_lock)
		spin_unlock_irqrestore(&ctx->lpw_lock, flags);
	return has_switch;
}

static int mtk_vdec_set_frame(struct mtk_vcodec_ctx *ctx,
	struct mtk_video_dec_buf *buf)
{
	int ret = 0;

	if (ctx->input_driven == INPUT_DRIVEN_PUT_FRM) {
		ret = vdec_if_set_param(ctx, SET_PARAM_FRAME_BUFFER, buf);
		if (ret == -EIO) {
			mtk_vdec_error_handle(ctx, "set_frame");
		}
	}

	return ret;
}

static void mtk_vdec_set_frame_handler(struct work_struct *ws)
{
	struct vdec_set_frame_work_struct *sws;
	struct mtk_vcodec_ctx *ctx;
	struct mtk_video_dec_buf *buf;
	struct vb2_v4l2_buffer *dst_vb2_v4l2;

	sws = container_of(ws, struct vdec_set_frame_work_struct, work);
	ctx = sws->ctx;

	if (ctx->input_driven != INPUT_DRIVEN_PUT_FRM || ctx->is_flushing == true ||
	    !mtk_vcodec_state_in_range(ctx, MTK_STATE_HEADER, MTK_STATE_STOP))
		return;

	dst_vb2_v4l2 = v4l2_m2m_next_dst_buf(ctx->m2m_ctx);
	if (dst_vb2_v4l2 != NULL) {
		buf = container_of(dst_vb2_v4l2, struct mtk_video_dec_buf, vb);
		mtk_vdec_set_frame(ctx, buf);
	}
}

static void mtk_vdec_init_set_frame_wq(struct mtk_vcodec_ctx *ctx)
{
	ctx->vdec_set_frame_wq = create_singlethread_workqueue("vdec_set_frame");
	INIT_WORK(&ctx->vdec_set_frame_work.work, mtk_vdec_set_frame_handler);
	ctx->vdec_set_frame_work.ctx = ctx;
}

static void mtk_vdec_flush_set_frame_wq(struct mtk_vcodec_ctx *ctx)
{
	if (ctx->vdec_set_frame_wq != NULL)
		flush_workqueue(ctx->vdec_set_frame_wq);
}

static void mtk_vdec_deinit_set_frame_wq(struct mtk_vcodec_ctx *ctx)
{
	if (ctx->vdec_set_frame_wq != NULL)
		destroy_workqueue(ctx->vdec_set_frame_wq);
}

static void mtk_vdec_trigger_set_frame(struct mtk_vcodec_ctx *ctx)
{
	if (ctx->input_driven == INPUT_DRIVEN_PUT_FRM && ctx->is_flushing == false)
		queue_work(ctx->vdec_set_frame_wq, &ctx->vdec_set_frame_work.work);
}

static void mtk_vdec_cgrp_handler(struct work_struct *work_ptr)
{
	struct delayed_work *delay_work = to_delayed_work(work_ptr);
	struct mtk_vcodec_ctx *ctx = container_of(delay_work, struct mtk_vcodec_ctx, cgrp_delay_work);

	mtk_vcodec_set_cgrp(ctx, false, __func__);
}

static void mtk_vdec_trigger_cgrp(struct mtk_vcodec_ctx *ctx)
{
	if (!ctx->cgrp_wq || mtk_vdec_open_cgrp_delay <= 0)
		return;

	mtk_vcodec_set_cgrp(ctx, true, __func__);
	INIT_DELAYED_WORK(&ctx->cgrp_delay_work, mtk_vdec_cgrp_handler);
	queue_delayed_work(ctx->cgrp_wq, &ctx->cgrp_delay_work, msecs_to_jiffies(mtk_vdec_open_cgrp_delay));
}

static void mtk_vdec_cgrp_init(struct mtk_vcodec_ctx *ctx)
{
	char name[25];

	SNPRINTF(name, sizeof(name), "vdec_cgrp-%d", ctx->id);
	ctx->cgrp_wq = create_workqueue(name);
}

static void mtk_vdec_cgrp_deinit(struct mtk_vcodec_ctx *ctx)
{
	if (!ctx->cgrp_wq)
		return;

	cancel_delayed_work(&ctx->cgrp_delay_work);
	flush_workqueue(ctx->cgrp_wq);
	destroy_workqueue(ctx->cgrp_wq);
	ctx->cgrp_wq = NULL;

	mtk_vcodec_set_cgrp(ctx, false, __func__); // double check to disable cgroup
}

/*
 * This function tries to clean all display buffers, the buffers will return
 * in display order.
 * Note the buffers returned from codec driver may still be in driver's
 * reference list.
 */
static struct vb2_buffer *get_display_buffer(struct mtk_vcodec_ctx *ctx,
	bool got_early_eos)
{
	struct vdec_fb *disp_frame_buffer = NULL;
	struct mtk_video_dec_buf *dstbuf;
	unsigned int i = 0;
	unsigned int num_planes = 0;
	bool no_output = false;
	u64 max_ts;

	mtk_v4l2_debug(4, "[%d]", ctx->id);

	mutex_lock(&ctx->buf_lock);
	if (vdec_if_get_param(ctx, GET_PARAM_DISP_FRAME_BUFFER, &disp_frame_buffer)) {
		mtk_v4l2_err("[%d] Cannot get param : GET_PARAM_DISP_FRAME_BUFFER", ctx->id);
		mutex_unlock(&ctx->buf_lock);
		return NULL;
	}

	if (disp_frame_buffer == NULL) {
		mtk_v4l2_debug(4, "No display frame buffer");
		mutex_unlock(&ctx->buf_lock);
		return NULL;
	}
	if (!virt_addr_valid(disp_frame_buffer)) {
		mtk_v4l2_debug(3, "Bad display frame buffer %p",
			disp_frame_buffer);
		mutex_unlock(&ctx->buf_lock);
		return NULL;
	}
	dstbuf = container_of(disp_frame_buffer, struct mtk_video_dec_buf, frame_buffer);

	if (disp_frame_buffer->status & FB_ST_NO_GENERATED) {
		no_output = true;
		disp_frame_buffer->status &= ~FB_ST_NO_GENERATED;
	}
	if (disp_frame_buffer->status & FB_ST_CROP_CHANGED) {
		dstbuf->flags |= CROP_CHANGED;
		disp_frame_buffer->status &= ~FB_ST_CROP_CHANGED;
	}

	num_planes = dstbuf->vb.vb2_buf.num_planes;
	if (dstbuf->used) {
		for (i = 0; i < num_planes; i++)
			vb2_set_plane_payload(&dstbuf->vb.vb2_buf, i,
				no_output ? 0 : ctx->picinfo.fb_sz[i]);

		dstbuf->ready_to_display = true;

		switch (dstbuf->frame_buffer.frame_type) {
		case MTK_FRAME_I:
			dstbuf->vb.flags |= V4L2_BUF_FLAG_KEYFRAME;
			break;
		case MTK_FRAME_P:
			dstbuf->vb.flags |= V4L2_BUF_FLAG_PFRAME;
			break;
		case MTK_FRAME_B:
			dstbuf->vb.flags |= V4L2_BUF_FLAG_BFRAME;
			break;
		default:
#ifdef TV_INTEGRATION
			mtk_v4l2_debug(2, "[%d] unknown frame type %d",
				ctx->id, dstbuf->frame_buffer.frame_type);
#endif
			break;
		}

		dstbuf->vb.vb2_buf.timestamp =
			disp_frame_buffer->timestamp;

		dstbuf->vb.vb2_buf.timestamp =
			mtk_vdec_ts_update_mode_and_timestamp(ctx, dstbuf->vb.vb2_buf.timestamp);

		if (ctx->input_driven == INPUT_DRIVEN_PUT_FRM)
			max_ts = ctx->early_eos_ts;
		else
			max_ts = ctx->input_max_ts;
		if (got_early_eos && dstbuf->vb.vb2_buf.timestamp == max_ts) {
			mtk_v4l2_debug(1, "[%d] got early eos (type %d) with max_ts %llu",
				ctx->id, ctx->eos_type, max_ts);
			dstbuf->vb.flags |= V4L2_BUF_FLAG_LAST;
			ctx->eos_type = NON_EOS; // clear flag
		}
		dstbuf->vb.field = disp_frame_buffer->field;

		mtk_v4l2_debug(2,
			"[%d] status=%x queue id=%d to done_list %d %d flag=%x field %d pts=%llu",
			ctx->id, disp_frame_buffer->status,
			dstbuf->vb.vb2_buf.index,
			dstbuf->queued_in_vb2, got_early_eos,
			dstbuf->vb.flags, dstbuf->vb.field, dstbuf->vb.vb2_buf.timestamp);

		mtk_vdec_lpw_set_ts(ctx, dstbuf->vb.vb2_buf.timestamp);
		v4l2_m2m_buf_done(&dstbuf->vb, VB2_BUF_STATE_DONE);
		ctx->decoded_frame_cnt++;
	}
	mutex_unlock(&ctx->buf_lock);

	return &dstbuf->vb.vb2_buf;
}

/*
 * This function tries to clean all capture buffers that are not used as
 * reference buffers by codec driver any more
 * In this case, we need re-queue buffer to vb2 buffer if user space
 * already returns this buffer to v4l2 or this buffer is just the output of
 * previous sps/pps/resolution change decode, or do nothing if user
 * space still owns this buffer
 */
static struct vb2_v4l2_buffer *get_free_buffer(struct mtk_vcodec_ctx *ctx)
{
	struct mtk_video_dec_buf *dstbuf;
	struct vdec_fb *free_frame_buffer = NULL;
	struct vb2_buffer *vb;
	int i;
	bool new_dma = false;

	mutex_lock(&ctx->buf_lock);
	if (vdec_if_get_param(ctx,
						  GET_PARAM_FREE_FRAME_BUFFER,
						  &free_frame_buffer)) {
		mtk_v4l2_err("[%d] Error!! Cannot get param", ctx->id);
		mutex_unlock(&ctx->buf_lock);
		return NULL;
	}

	if (free_frame_buffer == NULL) {
		mtk_v4l2_debug(4, " No free frame buffer");
		mutex_unlock(&ctx->buf_lock);
		return NULL;
	}
	if (!virt_addr_valid(free_frame_buffer)) {
		mtk_v4l2_debug(3, "Bad free frame buffer %p",
			free_frame_buffer);
		mutex_unlock(&ctx->buf_lock);
		return NULL;
	}

	dstbuf = container_of(free_frame_buffer, struct mtk_video_dec_buf,
						  frame_buffer);
	mtk_v4l2_debug(4, "[%d] tmp_frame_addr = 0x%p, status 0x%x, used %d flags 0x%x, id=%d %d %d %d",
				   ctx->id, free_frame_buffer, free_frame_buffer->status,
				   dstbuf->used, dstbuf->flags, dstbuf->vb.vb2_buf.index,
				   dstbuf->queued_in_vb2, dstbuf->queued_in_v4l2,
				   dstbuf->ready_to_display);

	dstbuf->flags |= REF_FREED;

	vb = &dstbuf->vb.vb2_buf;
	for (i = 0; i < vb->num_planes; i++) {
		// real buffer changed in this slot
		if (free_frame_buffer->fb_base[i].dmabuf != vb->planes[i].dbuf) {
			new_dma = true;
			mtk_v4l2_debug(2, "[%d] id=%d is new buffer: old dma_addr[%d] = %lx %p, new dma struct[%d] = %p",
				ctx->id, vb->index, i,
				(unsigned long)free_frame_buffer->fb_base[i].dma_addr,
				free_frame_buffer->fb_base[i].dmabuf,
				i, vb->planes[i].dbuf);
		}
	}

	if (ctx->input_driven == INPUT_DRIVEN_PUT_FRM && ctx->is_flushing == false && !new_dma &&
	    dstbuf->ready_to_display == false && !(free_frame_buffer->status & FB_ST_EOS)) {
		free_frame_buffer->status &= ~FB_ST_FREE;
		dstbuf->flags &= ~REF_FREED;
		mtk_v4l2_debug(0, "[%d]status=%x not queue id=%d to rdy_queue %d %d since input driven (%d) not ready to display",
			ctx->id, free_frame_buffer->status, dstbuf->vb.vb2_buf.index,
			dstbuf->queued_in_vb2, dstbuf->queued_in_v4l2, ctx->input_driven);
	} else if (dstbuf->used) {
		for (i = 0; i < free_frame_buffer->num_planes; i++) {
			if (free_frame_buffer->fb_base[i].dmabuf)
				dma_buf_put(free_frame_buffer->fb_base[i].dmabuf);
			mtk_v4l2_debug(4, "[Ref cnt] id=%d Ref put dma %p",
				free_frame_buffer->index, free_frame_buffer->fb_base[i].dmabuf);
		}
		dstbuf->used = false;
		if ((dstbuf->queued_in_vb2) && (dstbuf->queued_in_v4l2)) {
			if ((free_frame_buffer->status & FB_ST_EOS) &&
				(ctx->input_driven == INPUT_DRIVEN_PUT_FRM)) {
				/*
				 * Buffer status has EOS flag, which is capture buffer
				 * used for EOS when input driven. So set last buffer flag
				 * and queue to done queue.
				 */
				mtk_v4l2_debug(2, "[%d]status=%x not queue id=%d to rdy_queue %d %d for EOS",
					ctx->id, free_frame_buffer->status,
					dstbuf->vb.vb2_buf.index,
					dstbuf->queued_in_vb2,
					dstbuf->queued_in_v4l2);
				free_frame_buffer->status &= ~FB_ST_EOS;

				dstbuf->vb.vb2_buf.timestamp = 0;
				memset(&dstbuf->vb.timecode, 0, sizeof(struct v4l2_timecode));
				dstbuf->vb.flags |= V4L2_BUF_FLAG_LAST;
				for (i = 0; i < free_frame_buffer->num_planes; i++)
					vb2_set_plane_payload(&dstbuf->vb.vb2_buf, i, 0);
				v4l2_m2m_buf_done(&dstbuf->vb, VB2_BUF_STATE_DONE);
			} else if (free_frame_buffer->status == FB_ST_FREE) {
				/*
				 * After decode sps/pps or non-display buffer, we don't
				 * need to return capture buffer to user space, but
				 * just re-queue this capture buffer to vb2 queue.
				 * This reduce overheads that dq/q unused capture
				 * buffer. In this case, queued_in_vb2 = true.
				 */
				mtk_v4l2_debug(2, "[%d]status=%x queue id=%d to rdy_queue %d",
					ctx->id, free_frame_buffer->status,
					dstbuf->vb.vb2_buf.index,
					dstbuf->queued_in_vb2);
				if (v4l2_m2m_buf_queue_check(ctx->m2m_ctx, &dstbuf->vb) < 0)
					goto err_in_rdyq;
				mtk_vdec_trigger_set_frame(ctx);
			} else {
				mtk_v4l2_debug(4, "[%d]status=%x reference free queue id=%d %d %d",
					ctx->id, free_frame_buffer->status,
					dstbuf->vb.vb2_buf.index,
					dstbuf->queued_in_vb2,
					dstbuf->queued_in_v4l2);
			}
		} else if ((dstbuf->queued_in_vb2 == false) &&
				   (dstbuf->queued_in_v4l2 == true)) {
			/*
			 * If buffer in v4l2 driver but not in vb2 queue yet,
			 * and we get this buffer from free_list, it means
			 * that codec driver do not use this buffer as
			 * reference buffer anymore. We should q buffer to vb2
			 * queue, so later work thread could get this buffer
			 * for decode. In this case, queued_in_vb2 = false
			 * means this buffer is not from previous decode
			 * output.
			 */
			mtk_v4l2_debug(2,
				"[%d]status=%x queue id=%d to rdy_queue",
				ctx->id, free_frame_buffer->status,
				dstbuf->vb.vb2_buf.index);
			dstbuf->queued_in_vb2 = true;
			if (v4l2_m2m_buf_queue_check(ctx->m2m_ctx, &dstbuf->vb) < 0)
				goto err_in_rdyq;
			mtk_vdec_trigger_set_frame(ctx);
		} else {
			/*
			 * Codec driver do not need to reference this capture
			 * buffer and this buffer is not in v4l2 driver.
			 * Then we don't need to do any thing, just add log when
			 * we need to debug buffer flow.
			 * When this buffer q from user space, it could
			 * directly q to vb2 buffer
			 */
			mtk_v4l2_debug(4, "[%d]status=%x reference free queue id=%d %d %d",
				ctx->id, free_frame_buffer->status,
				dstbuf->vb.vb2_buf.index,
				dstbuf->queued_in_vb2,
				dstbuf->queued_in_v4l2);
		}
	}
	mutex_unlock(&ctx->buf_lock);

	return &dstbuf->vb;
err_in_rdyq:
	for (i = 0; i < free_frame_buffer->num_planes; i++) {
		if (free_frame_buffer->fb_base[i].dmabuf)
			get_dma_buf(free_frame_buffer->fb_base[i].dmabuf);
		mtk_v4l2_debug(4, "[Ref cnt] id=%d Ref get dma %p",
			free_frame_buffer->index, free_frame_buffer->fb_base[i].dmabuf);
	}
	mutex_unlock(&ctx->buf_lock);

	mtk_vdec_trigger_set_frame(ctx);

	return &dstbuf->vb;
}

static struct vb2_buffer *get_free_bs_buffer(struct mtk_vcodec_ctx *ctx,
	struct mtk_vcodec_mem *current_bs)
{
	struct mtk_vcodec_mem *free_bs_buffer;
	struct mtk_video_dec_buf *srcbuf;

	mutex_lock(&ctx->buf_lock);
	if (vdec_if_get_param(ctx, GET_PARAM_FREE_BITSTREAM_BUFFER,
						  &free_bs_buffer) != 0) {
		mtk_v4l2_err("[%d] Cannot get param : GET_PARAM_FREE_BITSTREAM_BUFFER",
					 ctx->id);
		mutex_unlock(&ctx->buf_lock);
		return NULL;
	}
	mutex_unlock(&ctx->buf_lock);

	if (free_bs_buffer == NULL) {
		mtk_v4l2_debug(3, "No free bitstream buffer");
		return NULL;
	}
	if (current_bs == free_bs_buffer) {
		mtk_v4l2_debug(4,
			"No free bitstream buffer except current bs: %p",
			current_bs);
		return NULL;
	}
	if (!virt_addr_valid(free_bs_buffer)) {
		mtk_v4l2_debug(3, "Bad free bitstream buffer %p",
			free_bs_buffer);
		return NULL;
	}

	srcbuf = container_of(free_bs_buffer,
		struct mtk_video_dec_buf, bs_buffer);
	mtk_v4l2_debug(2,
		"[%d] length=%zu size=%zu queue idx=%d to done_list %d",
		ctx->id, free_bs_buffer->length, free_bs_buffer->size,
		srcbuf->vb.vb2_buf.index,
		srcbuf->queued_in_vb2);

	if (srcbuf->vb.flags & V4L2_BUF_FLAG_OUTPUT_NOT_GENERATED)
		mtk_vdec_ts_remove_last(ctx);
	v4l2_m2m_buf_done(&srcbuf->vb, VB2_BUF_STATE_DONE);

	return &srcbuf->vb.vb2_buf;
}

static void clean_free_bs_buffer(struct mtk_vcodec_ctx *ctx,
	struct mtk_vcodec_mem *current_bs)
{
	struct vb2_buffer *framptr;

	do {
		framptr = get_free_bs_buffer(ctx, current_bs);
	} while (framptr);
}


static void clean_display_buffer(struct mtk_vcodec_ctx *ctx, bool got_early_eos)
{
	struct vb2_buffer *framptr;

	do {
		framptr = get_display_buffer(ctx, got_early_eos);
	} while (framptr);
}

static bool clean_free_fm_buffer(struct mtk_vcodec_ctx *ctx)
{
	struct vb2_v4l2_buffer *framptr_vb;
	bool has_eos = false;

	do {
		framptr_vb = get_free_buffer(ctx);
		if (framptr_vb != NULL && (framptr_vb->flags & V4L2_BUF_FLAG_LAST))
			has_eos = true;
	} while (framptr_vb);

	return has_eos;
}

#if ENABLE_META_BUF
static dma_addr_t create_meta_buffer_info(struct mtk_vcodec_ctx *ctx, int fd)
{
	int i = 0;
	struct dma_buf *dmabuf = NULL;
	struct dma_buf_attachment *buf_att = NULL;
	struct sg_table *sgt = NULL;
	dma_addr_t dma_meta_addr = 0;

	dmabuf = dma_buf_get(fd);
	mtk_v4l2_debug(8, "dmabuf:%p", dmabuf);
	if (IS_ERR_OR_NULL(dmabuf)) {
		mtk_v4l2_err("dma_buf_get fail ret %ld", PTR_ERR(dmabuf));
		return 0;
	}

	if (mtk_vcodec_dma_attach_map(ctx->m2m_ctx->out_q_ctx.q.dev,
		dmabuf, &buf_att, &sgt, &dma_meta_addr, DMA_TO_DEVICE, __func__, __LINE__))
		return 0;

	mtk_v4l2_debug(4, "map new, dmabuf:%p, dma_addr:%pad", dmabuf, &dma_meta_addr);
	//save va and dmabuf
	if (dma_meta_addr) {
		for (i = 0; i < MAX_META_BUF_CNT; i++) {
			if (ctx->dma_meta_list[i].dma_meta_addr == 0) {
				ctx->dma_meta_list[i].dmabuf = dmabuf;
				ctx->dma_meta_list[i].dma_meta_addr = dma_meta_addr;
				ctx->dma_meta_list[i].buf_att = buf_att;
				ctx->dma_meta_list[i].sgt = sgt;
				mtk_v4l2_debug(2, "save meta buf dmabuf %p  addr:%pad at %d",
					dmabuf, &dma_meta_addr, i);
				break;
			}
		}
		if (i == MAX_META_BUF_CNT)
			mtk_v4l2_err("dma_buf_list is overflow!\n");
	}

	return dma_meta_addr;
}

static dma_addr_t get_meta_buffer_dma_addr(struct mtk_vcodec_ctx *ctx, int fd)
{
	dma_addr_t dma_addr = 0;
	int i = 0;
	struct dma_buf *dmabuf = NULL;

	dmabuf = dma_buf_get(fd);
	mtk_v4l2_debug(8, "%s, dmabuf:%p", __func__, dmabuf);
	if (IS_ERR_OR_NULL(dmabuf)) {
		mtk_v4l2_err("dma_buf_get fail ret %ld", PTR_ERR(dmabuf));
		return 0;
	}

	if (dmabuf) {
		for (i = 0; i < MAX_META_BUF_CNT; i++) {
			if (dmabuf == ctx->dma_meta_list[i].dmabuf) {
				dma_addr = ctx->dma_meta_list[i].dma_meta_addr;
				mtk_v4l2_debug(4, "reuse dma_addr %pad at %d", &dma_addr, i);
				break;
			}
		}
	}

	if (dma_addr == 0) {
		dma_addr = create_meta_buffer_info(ctx, fd);
		for (i = 0; i < MAX_META_BUF_CNT; i++) {
			if (dmabuf == ctx->dma_meta_list[i].dmabuf) {
				dma_addr = ctx->dma_meta_list[i].dma_meta_addr;
				mtk_v4l2_debug(4, "reuse dma_addr %pad at %d", &dma_addr, i);
				break;
			}
		}
	}

	if (dmabuf)
		dma_buf_put(dmabuf);

	return dma_addr;
}
#endif

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

#if ENABLE_META_BUF
	dma_buf_begin_cpu_access(dmabuf, DMA_BIDIRECTIONAL);
	if (!dma_buf_vmap_unlocked(dmabuf, &map))
		va = map.vaddr;
#endif

	if (mtk_vcodec_dma_attach_map(ctx->general_dev,
		dmabuf, &buf_att, &sgt, &dma_general_addr, DMA_BIDIRECTIONAL, __func__, __LINE__)) {
		if (va)
			dma_buf_vunmap_unlocked(dmabuf, &map);
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
		if (va)
			dma_buf_vunmap_unlocked(dmabuf, &map);
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
	if (gen_buf_info->va) {
		struct iosys_map map;

		iosys_map_set_vaddr(&map, gen_buf_info->va);
		dma_buf_vunmap_unlocked(gen_buf_info->dmabuf, &map);
		dma_buf_end_cpu_access(gen_buf_info->dmabuf, DMA_BIDIRECTIONAL);
	}
	dma_buf_put(gen_buf_info->dmabuf);

	memset((void *)gen_buf_info, 0, sizeof(struct dma_gen_buf));
}

static void set_general_buffer(struct mtk_vcodec_ctx *ctx, struct vdec_fb *frame_buffer, int fd)
{
	struct dma_gen_buf *gen_buf_info = NULL;

	mutex_lock(&ctx->gen_buf_list_lock);
	if (fd > -1)
		gen_buf_info = create_general_buffer_info(ctx, fd);

	frame_buffer->general_buf_fd = fd;
	if (gen_buf_info != NULL) {
		frame_buffer->dma_general_buf  = gen_buf_info->dmabuf;
		frame_buffer->dma_general_addr = gen_buf_info->dma_general_addr;
	} else {
		frame_buffer->dma_general_buf = 0;
		frame_buffer->dma_general_addr = 0;
	}
	mutex_unlock(&ctx->gen_buf_list_lock);
}

#if ENABLE_META_BUF
static void *get_general_buffer_va(struct mtk_vcodec_ctx *ctx, struct dma_buf *dmabuf)
{
	struct dma_gen_buf *gen_buf_info;
	void *va = NULL;

	mutex_lock(&ctx->gen_buf_list_lock);
	gen_buf_info = get_general_buffer_info(ctx, dmabuf);
	if (gen_buf_info != NULL)
		va = gen_buf_info->va;
	mutex_unlock(&ctx->gen_buf_list_lock);

	return va;
}
#endif

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

int mtk_vdec_defer_put_fb_job(struct mtk_vcodec_ctx *ctx, int type)
{
	int ret = -1;

	mutex_lock(&ctx->resched_lock);

	/* skip once due to we are in reschedule progress already */
	if (ctx->resched)
		goto unlock;

	/* allow to schedule m2m ctx if src/dst is empty */
	v4l2_m2m_set_src_buffered(ctx->m2m_ctx, true);
	v4l2_m2m_set_dst_buffered(ctx->m2m_ctx, true);
	v4l2_m2m_try_schedule(ctx->m2m_ctx);
	ctx->resched = true;
	ret = 0;
unlock:
	mutex_unlock(&ctx->resched_lock);
	return ret;
}

int mtk_vdec_cancel_put_fb_job_locked(struct mtk_vcodec_ctx *ctx)
{
	int resched = ctx->resched;

	if (resched) {
		v4l2_m2m_set_src_buffered(ctx->m2m_ctx, false);
		if (!ctx->input_driven)
			v4l2_m2m_set_dst_buffered(ctx->m2m_ctx, false);
		ctx->resched = false;
	}
	return resched;
}

void mtk_vdec_cancel_put_fb_job(struct mtk_vcodec_ctx *ctx)
{
	mutex_lock(&ctx->resched_lock);
	mtk_vdec_cancel_put_fb_job_locked(ctx);
	mutex_unlock(&ctx->resched_lock);
}

void mtk_vdec_process_put_fb_job(struct mtk_vcodec_ctx *ctx)
{
	struct mtk_video_dec_buf *src_buf_info;
	struct vb2_v4l2_buffer *src_vb2_v4l2, *dst_vb2_v4l2;
	int process = 0;

	mutex_lock(&ctx->resched_lock);

	if (!mtk_vdec_cancel_put_fb_job_locked(ctx))
		goto unlock;

	/* do nothing for low latency and output async */
	if (ctx->use_fence || ctx->output_async)
		goto unlock;

	src_vb2_v4l2 = v4l2_m2m_next_src_buf(ctx->m2m_ctx);
	dst_vb2_v4l2 = v4l2_m2m_next_dst_buf(ctx->m2m_ctx);

	/* use normal flow ,
	 * if src_vb2_v4l2 or dst_vb2_v4l2 is NULL
	 * we will send msg SET_PARAM_PUT_FB
	 * let LAT get display buffer
	 */
	if (src_vb2_v4l2 && dst_vb2_v4l2)
		goto unlock;

	src_buf_info = container_of(src_vb2_v4l2,
				    struct mtk_video_dec_buf,
				    vb);
	/*
	 * no need to clean disp/free if we are processing EOS,
	 * because EOS is handled synchronously
	 */
	if (src_vb2_v4l2 && src_buf_info->lastframe != NON_EOS)
		goto unlock;

	process = 1;

unlock:
	mutex_unlock(&ctx->resched_lock);

	if (process) {
		if (vdec_if_set_param(ctx, SET_PARAM_PUT_FB, NULL))
			mtk_v4l2_err("[%d] Error!! Cannot set param SET_PARAM_PUT_FB", ctx->id);
		clean_display_buffer(ctx, 0);
		clean_free_fm_buffer(ctx);
	}
}

static void mtk_vdec_queue_res_chg_event(struct mtk_vcodec_ctx *ctx)
{
	static const struct v4l2_event ev_src_ch = {
		.type = V4L2_EVENT_SOURCE_CHANGE,
		.u.src_change.changes =
		V4L2_EVENT_SRC_CH_RESOLUTION,
	};

	ctx->waiting_fmt = true;
	mtk_v4l2_debug(1, "[%d]", ctx->id);
	v4l2_event_queue_fh(&ctx->fh, &ev_src_ch);

	v4l2_m2m_set_dst_buffered(ctx->m2m_ctx,
		ctx->input_driven != NON_INPUT_DRIVEN);
}

static void mtk_vdec_queue_stop_play_event(struct mtk_vcodec_ctx *ctx)
{
	static const struct v4l2_event ev_eos = {
		.type = V4L2_EVENT_EOS,
	};

	mtk_v4l2_debug(1, "[%d]", ctx->id);
	v4l2_event_queue_fh(&ctx->fh, &ev_eos);
}

static void mtk_vdec_queue_noseqheader_event(struct mtk_vcodec_ctx *ctx)
{
	static const struct v4l2_event ev_eos = {
		.type = V4L2_EVENT_MTK_VDEC_NOHEADER,
	};

	mtk_v4l2_debug(1, "[%d]", ctx->id);
	v4l2_event_queue_fh(&ctx->fh, &ev_eos);
}

void mtk_vdec_queue_error_event(struct mtk_vcodec_ctx *ctx)
{
	static struct v4l2_event ev_error = {
		.type = V4L2_EVENT_MTK_VDEC_ERROR,
	};

	if  (ctx->err_msg)
		memcpy((void *)ev_error.u.data, &ctx->err_msg, sizeof(ctx->err_msg));

	mtk_v4l2_debug(0, "[%d] msg %x", ctx->id, ctx->err_msg);
	v4l2_event_queue_fh(&ctx->fh, &ev_error);
}

void mtk_vdec_queue_error_code_event(struct mtk_vcodec_ctx *ctx, unsigned int info)
{
	static struct v4l2_event ev_error = {
		.type = V4L2_EVENT_MTK_VDEC_ERROR_INFO,
	};

	memcpy((void *)ev_error.u.data, &info, sizeof(info));

	mtk_v4l2_err("[%d] error_code %d", ctx->id, info);
	v4l2_event_queue_fh(&ctx->fh, &ev_error);
}

void mtk_vdec_error_handle(struct mtk_vcodec_ctx *ctx, char *debug_str)
{
	struct mtk_vcodec_dev *dev = ctx->dev;
	int i;

	mtk_v4l2_err("[%d] start error handling %s (dvfs freq %d)(pw ref %d, %d %d)(hw active %d %d)",
		ctx->id, debug_str, dev->vdec_dvfs_params.target_freq,
		atomic_read(&dev->larb_ref_cnt),
		atomic_read(&dev->dec_clk_ref_cnt[MTK_VDEC_LAT]),
		atomic_read(&dev->dec_clk_ref_cnt[MTK_VDEC_CORE]),
		atomic_read(&dev->dec_hw_active[MTK_VDEC_LAT]),
		atomic_read(&dev->dec_hw_active[MTK_VDEC_CORE]));

	mutex_lock(&dev->dec_dvfs_mutex);
	mtk_vcodec_cpu_adaptive_ctrl(ctx, false);
	mutex_unlock(&dev->dec_dvfs_mutex);

	if (ctx == ctx->dev_ctx)
		return;

	mtk_vcodec_set_state(ctx, MTK_STATE_ABORT);
	mtk_vdec_lpw_stop_timer(ctx, true);
	vdec_check_release_lock(ctx);

	for (i = 0; i < MTK_VDEC_HW_NUM; i++)
		atomic_set(&dev->dec_hw_active[i], 0);

	mtk_vdec_queue_error_event(ctx);
}

static void mtk_vdec_set_unsupport(struct mtk_vcodec_ctx *ctx)
{
	ctx->is_unsupport = true;
	mtk_vcodec_set_state(ctx, MTK_STATE_STOP);
	mtk_vdec_queue_error_event(ctx);
}

static void mtk_vdec_reset_decoder(struct mtk_vcodec_ctx *ctx, bool is_drain,
	struct mtk_vcodec_mem *current_bs, enum v4l2_buf_type type)
{
	struct vdec_fb drain_fb;
	int i, ret = 0;
	struct vb2_v4l2_buffer *dst_vb2_v4l2, *src_vb2_v4l2;
	struct mtk_video_dec_buf *dstbuf, *srcbuf;
	struct vb2_queue *dstq, *srcq;

	mtk_v4l2_debug(2, "[%d] is_drain %d, ctx state %d, type %d, current_bs idx %d",
		ctx->id, is_drain, mtk_vcodec_get_state(ctx), type, current_bs ? current_bs->index : -1);
	mtk_vdec_lpw_switch_reset(ctx, true, "reset decoder");

#if ENABLE_META_BUF
	mutex_lock(&ctx->meta_buf_lock);
#endif
	mtk_vcodec_set_state(ctx, MTK_STATE_FLUSH);
	if (ctx->input_driven == INPUT_DRIVEN_CB_FRM)
		wake_up(&ctx->fm_wq);

	release_all_general_buffer_info(ctx);

#if ENABLE_META_BUF
	for (i = 0; i < MAX_META_BUF_CNT; i++) {
		if (ctx->dma_meta_list[i].dmabuf) {
			struct dma_buf *dmabuf = ctx->dma_meta_list[i].dmabuf;
			struct dma_buf_attachment *buf_att = ctx->dma_meta_list[i].buf_att;
			struct sg_table *sgt = ctx->dma_meta_list[i].sgt;

			mtk_vcodec_dma_unmap_detach(dmabuf, &buf_att, &sgt, DMA_TO_DEVICE);
			dma_buf_put(dmabuf);
		}
	}
	memset(ctx->dma_meta_list, 0,
		sizeof(struct dma_meta_buf) * MAX_META_BUF_CNT);
	mutex_unlock(&ctx->meta_buf_lock);
#endif

	if (is_drain) {
		memset(&drain_fb, 0, sizeof(struct vdec_fb));
		ret = vdec_if_flush(ctx, NULL, &drain_fb,
			V4L2_TYPE_IS_OUTPUT(type) ? FLUSH_BITSTREAM : FLUSH_FRAME);
	} else {
		ctx->is_flushing = true;
		mtk_vdec_set_frame(ctx, NULL);
		ret = vdec_if_flush(ctx, NULL, NULL,
			V4L2_TYPE_IS_OUTPUT(type) ? FLUSH_BITSTREAM : FLUSH_FRAME);
	}
	v4l2_m2m_set_dst_buffered(ctx->m2m_ctx, ctx->input_driven != NON_INPUT_DRIVEN);

	dstq = &ctx->m2m_ctx->cap_q_ctx.q;
	srcq = &ctx->m2m_ctx->out_q_ctx.q;
	if (ret) {
		ctx->is_flushing = false;
		mtk_v4l2_err("DecodeFinal failed, ret=%d", ret);

		if (ret == -EIO) {
			mutex_lock(&ctx->buf_lock);
			for (i = 0; i < dstq->num_buffers; i++) {
				dst_vb2_v4l2 = container_of(
					dstq->bufs[i], struct vb2_v4l2_buffer, vb2_buf);
				dstbuf = container_of(
					dst_vb2_v4l2, struct mtk_video_dec_buf, vb);
				// codec exception handling
				mtk_v4l2_debug(8, "[%d]num_buffers %d status=%x queue id=%d %p %lx q_cnt %d %d %d %d state %d",
					ctx->id, dstq->num_buffers, dstbuf->frame_buffer.status,
					dstbuf->vb.vb2_buf.index, &dstbuf->frame_buffer,
					(unsigned long)(&dstbuf->frame_buffer),
					atomic_read(&dstq->owned_by_drv_count),
					dstbuf->queued_in_vb2, dstbuf->queued_in_v4l2,
					dstbuf->used, dst_vb2_v4l2->vb2_buf.state);
				if (dst_vb2_v4l2->vb2_buf.state == VB2_BUF_STATE_ACTIVE) {
					v4l2_m2m_buf_done(&dstbuf->vb, VB2_BUF_STATE_ERROR);
					dstbuf->frame_buffer.status = FB_ST_FREE;
				}
			}
			mutex_unlock(&ctx->buf_lock);

			for (i = 0; i < srcq->num_buffers; i++) {
				src_vb2_v4l2 = container_of(
					srcq->bufs[i], struct vb2_v4l2_buffer, vb2_buf);
				srcbuf = container_of(
					src_vb2_v4l2, struct mtk_video_dec_buf, vb);
				if (src_vb2_v4l2->vb2_buf.state == VB2_BUF_STATE_ACTIVE)
					v4l2_m2m_buf_done(&srcbuf->vb, VB2_BUF_STATE_ERROR);
			}
		}
		return;
	}

	mtk_vdec_cancel_put_fb_job(ctx);
	clean_free_bs_buffer(ctx, current_bs);
	clean_display_buffer(ctx, 0);
	clean_free_fm_buffer(ctx);

	/* check buffer status */
	mutex_lock(&ctx->buf_lock);
	for (i = 0; i < dstq->num_buffers; i++) {
		dst_vb2_v4l2 = container_of(
			dstq->bufs[i], struct vb2_v4l2_buffer, vb2_buf);
		dstbuf = container_of(
			dst_vb2_v4l2, struct mtk_video_dec_buf, vb);
		mtk_v4l2_debug(4, "[%d]num_buffers %d status=%x queue id=%d %p %lx q_cnt %d %d %d %d",
			ctx->id, dstq->num_buffers, dstbuf->frame_buffer.status,
			dstbuf->vb.vb2_buf.index, &dstbuf->frame_buffer,
			(unsigned long)(&dstbuf->frame_buffer),
			atomic_read(&dstq->owned_by_drv_count),
			dstbuf->queued_in_vb2,
			dstbuf->queued_in_v4l2, dstbuf->used);
	}
	mutex_unlock(&ctx->buf_lock);
	ctx->is_flushing = false;
}

static void mtk_vdec_pic_info_update(struct mtk_vcodec_ctx *ctx)
{
	unsigned int dpbsize = 0;
	int ret;
	struct mtk_color_desc color_desc = {.hdr_type = 0};

	if (vdec_if_get_param(ctx, GET_PARAM_PIC_INFO, &ctx->last_decoded_picinfo)) {
		mtk_v4l2_err("[%d]Error!! Cannot get param : GET_PARAM_PICTURE_INFO ERR",
					 ctx->id);
		return;
	}

	if (ctx->last_decoded_picinfo.pic_w == 0 ||
		ctx->last_decoded_picinfo.pic_h == 0 ||
		ctx->last_decoded_picinfo.buf_w == 0 ||
		ctx->last_decoded_picinfo.buf_h == 0) {
		mtk_v4l2_err("Cannot get correct pic info");
		return;
	}

	ret = vdec_if_get_param(ctx, GET_PARAM_DPB_SIZE, &dpbsize);
	if (dpbsize == 0)
		mtk_v4l2_err("Incorrect dpb size, ret=%d", ret);
	ctx->last_dpb_size = dpbsize;

	ret = vdec_if_get_param(ctx, GET_PARAM_COLOR_DESC, &color_desc);
	if (ret == 0)
		ctx->last_is_hdr = color_desc.hdr_type;

	mtk_v4l2_debug(1,
				   "[%d]-> new(%d,%d),dpb(%d), old(%d,%d),dpb(%d), bit(%d) real(%d,%d) hdr(%d,%d)",
				   ctx->id, ctx->last_decoded_picinfo.pic_w,
				   ctx->last_decoded_picinfo.pic_h,
				   ctx->last_dpb_size,
				   ctx->picinfo.pic_w, ctx->picinfo.pic_h,
				   ctx->dpb_size,
				   ctx->picinfo.bitdepth,
				   ctx->last_decoded_picinfo.buf_w,
				   ctx->last_decoded_picinfo.buf_h,
				   ctx->is_hdr, ctx->last_is_hdr);
}


static struct vb2_buffer *mtk_vdec_callback_get_frame(struct mtk_vcodec_ctx *ctx)
{
	struct vb2_v4l2_buffer *dst_vb2_v4l2;
	struct vb2_buffer *dst_buf;
	int ret = 0;

	ret = wait_event_interruptible(
		ctx->fm_wq,
		v4l2_m2m_num_dst_bufs_ready(
		ctx->m2m_ctx) > 0 ||
		ctx->state == MTK_STATE_FLUSH);
	if (ret)
		mtk_v4l2_err("signaled by -ERESTARTSYS(%d)\n ", ret);

	dst_vb2_v4l2 = v4l2_m2m_next_dst_buf(ctx->m2m_ctx);
	dst_buf = &dst_vb2_v4l2->vb2_buf;
	/* update dst buf status */
	if (mtk_vcodec_is_state(ctx, MTK_STATE_FLUSH) || ret != 0 || dst_buf == NULL) {
		mtk_v4l2_err("wait EOS dst break! state %d, ret %d, dst_buf %p",
			mtk_vcodec_get_state(ctx), ret, dst_buf);
		return NULL;
	}

	return dst_buf;
}

static void mkt_vdec_put_eos_fb(struct mtk_vcodec_ctx *ctx, bool need_put_eos, bool need_stop_play)
{
	struct mtk_video_dec_buf *dst_buf_info;
	struct vb2_v4l2_buffer *dst_vb2_v4l2;
	struct vb2_buffer *dst_buf = NULL;
	struct vdec_fb *pfb;
	int i;

	if (ctx->input_driven == INPUT_DRIVEN_CB_FRM)
		dst_buf = mtk_vdec_callback_get_frame(ctx);

	if (need_put_eos && (!ctx->input_driven || dst_buf != NULL)) {
		dst_vb2_v4l2 = v4l2_m2m_dst_buf_remove(ctx->m2m_ctx);
		if (dst_vb2_v4l2 == NULL) {
			mtk_v4l2_err("dst_vb2_v4l2 is NULL");
			return;
		}
		dst_buf = &dst_vb2_v4l2->vb2_buf;
		dst_buf_info = container_of(dst_vb2_v4l2, struct mtk_video_dec_buf, vb);

		dst_buf_info->vb.vb2_buf.timestamp = 0;
		memset(&dst_buf_info->vb.timecode, 0, sizeof(struct v4l2_timecode));
		dst_vb2_v4l2->flags |= V4L2_BUF_FLAG_LAST;
		pfb = &dst_buf_info->frame_buffer;
		for (i = 0; i < pfb->num_planes; i++)
			vb2_set_plane_payload(&dst_buf_info->vb.vb2_buf, i, 0);
		mutex_lock(&ctx->buf_lock);
		if (dst_buf_info->used) {
			for (i = 0; i < pfb->num_planes; i++) {
				if (pfb->fb_base[i].dmabuf)
					dma_buf_put(pfb->fb_base[i].dmabuf);
				mtk_v4l2_debug(4, "[Ref cnt] id=%d Ref put dma %p",
					i, pfb->fb_base[i].dmabuf);
			}
			dst_buf_info->used = false;
		}
		mutex_unlock(&ctx->buf_lock);
		v4l2_m2m_buf_done(&dst_buf_info->vb, VB2_BUF_STATE_DONE);
	}

	if (need_stop_play)
		mtk_vdec_queue_stop_play_event(ctx);
}

int mtk_vdec_put_fb(struct mtk_vcodec_ctx *ctx, enum mtk_put_buffer_type type, bool no_need_put)
{
	struct mtk_video_dec_buf *src_buf_info;
	struct vb2_v4l2_buffer *src_vb2_v4l2;
	struct vb2_buffer *src_buf;
	bool got_early_eos = false, has_eos;

	mtk_v4l2_debug(1, "type = %d", type);

	if (!mtk_vcodec_state_in_range(ctx, MTK_STATE_HEADER, MTK_STATE_STOP)) { //  < HEADER
		mtk_v4l2_err("type = %d state %d no valid", type, mtk_vcodec_get_state(ctx));
		return -1;
	}

	if (no_need_put)
		goto not_put_fb;

	/* put fb in core stage? */
	if (type != PUT_BUFFER_WORKER && !(ctx->use_fence || ctx->output_async))
		return mtk_vdec_defer_put_fb_job(ctx, type);

	if ((type == PUT_BUFFER_WORKER && ctx->output_async) ||
	    (type == PUT_BUFFER_CALLBACK && !ctx->output_async)) {
		mtk_v4l2_err("type = %d no valid for output_async %d", type, ctx->output_async);
		return -1;
	}

	src_vb2_v4l2 = v4l2_m2m_next_src_buf(ctx->m2m_ctx);
	src_buf = &src_vb2_v4l2->vb2_buf;
	src_buf_info = container_of(src_vb2_v4l2, struct mtk_video_dec_buf, vb);

	if (src_buf_info == NULL || src_buf == NULL) {
		if (ctx->use_fence && type != PUT_BUFFER_WORKER
			&& !(ctx->input_driven)) {
			clean_display_buffer(ctx, false);
			clean_free_fm_buffer(ctx);
			return 0;
		}

		if (type == PUT_BUFFER_WORKER)
			return 0;
	}

	if (type == PUT_BUFFER_WORKER && (src_buf_info->lastframe == EOS_WITH_DATA ||
		(src_buf_info->lastframe == EOS && src_buf->planes[0].bytesused != 0U)))
		got_early_eos = true;
	if (ctx->output_async && ctx->eos_type == EOS_WITH_DATA)
		got_early_eos = true;

	clean_display_buffer(ctx, got_early_eos);
	has_eos = clean_free_fm_buffer(ctx);

	/* return output for EOS */
	if ((type == PUT_BUFFER_WORKER && src_buf_info->lastframe == EOS) ||
	    (type == PUT_BUFFER_CALLBACK && has_eos))
		mkt_vdec_put_eos_fb(ctx, !ctx->output_async && src_buf->planes[0].bytesused == 0U,
			(!ctx->output_async && src_buf_info->lastframe == EOS) ||
				(ctx->output_async && ctx->input_driven && has_eos));

not_put_fb:
	if (no_need_put || ctx->input_driven)
		v4l2_m2m_try_schedule(ctx->m2m_ctx);

	return 0;
}

static void mtk_vdec_worker(struct work_struct *work)
{
	struct mtk_vcodec_ctx *ctx = container_of(work, struct mtk_vcodec_ctx, decode_work);
	struct mtk_vcodec_dev *dev = ctx->dev;
	struct vb2_buffer *src_buf;
	struct mtk_vcodec_mem *buf;
	struct vdec_fb *pfb = NULL;
	unsigned int src_chg = 0;
	bool res_chg = false;
	bool need_more_output = false;
	bool mtk_vcodec_unsupport = false;
	int ret;
	struct timespec64 worktvstart;
	struct timespec64 worktvstart1;
	struct timespec64 vputvend;
	struct timespec64 ts_delta;
	struct mtk_video_dec_buf *dst_buf_info = NULL, *src_buf_info = NULL;
	struct vb2_v4l2_buffer *src_vb2_v4l2;
	unsigned int dpbsize = 0;
	struct mtk_color_desc color_desc = {.hdr_type = 0};
	struct vdec_fb drain_fb;
	int src_cnt_before, dst_cnt_before, pair_cnt_before;

	mutex_lock(&ctx->worker_lock);
	mtk_vdec_do_gettimeofday(&worktvstart);

	if (!mtk_vcodec_is_state(ctx, MTK_STATE_HEADER)) {
		mtk_v4l2_debug(1, "[%d] state %d", ctx->id, mtk_vcodec_get_state(ctx));
		goto vdec_worker_finish;
	}

	/* process deferred put fb job */
	mtk_vdec_process_put_fb_job(ctx);

	src_vb2_v4l2 = v4l2_m2m_next_src_buf(ctx->m2m_ctx);
	if (src_vb2_v4l2 == NULL) {
		mtk_v4l2_debug(1, "[%d] src_buf empty!!", ctx->id);
		goto vdec_worker_finish;
	}
	src_buf = &src_vb2_v4l2->vb2_buf;
	src_buf_info = container_of(src_vb2_v4l2, struct mtk_video_dec_buf, vb);

	if (!ctx->input_driven) {
		pfb = mtk_vcodec_get_fb(ctx);
		if (pfb == NULL) {
			mtk_v4l2_debug(1, "[%d] dst_buf empty!!", ctx->id);
			goto vdec_worker_finish;
		}
		dst_buf_info = container_of(pfb, struct mtk_video_dec_buf, frame_buffer);
	}

	/* handle EOS & EOS with data */
	if (src_buf_info->lastframe == EOS) {
		mtk_v4l2_debug(4, "===>[%d] vdec_if_decode() EOS ===> %d %d",
			ctx->id, src_buf->planes[0].bytesused, src_buf->planes[0].length);

		memset(&drain_fb, 0, sizeof(struct vdec_fb));
		if (src_buf->planes[0].bytesused == 0)
			drain_fb.status = FB_ST_EOS;
		vdec_if_flush(ctx, NULL, &drain_fb, FLUSH_BITSTREAM | FLUSH_FRAME); // drain EOS

		if (!ctx->output_async) {
			mtk_vdec_cancel_put_fb_job(ctx);
			mtk_vdec_put_fb(ctx, PUT_BUFFER_WORKER, false);
		} else if (!ctx->input_driven)
			mkt_vdec_put_eos_fb(ctx, src_buf->planes[0].bytesused == 0U, true);

		/* handle EOS input */
		v4l2_m2m_src_buf_remove(ctx->m2m_ctx);
		src_buf_info->lastframe = NON_EOS;
		src_vb2_v4l2->flags |= V4L2_BUF_FLAG_LAST;
		vb2_set_plane_payload(&src_buf_info->vb.vb2_buf, 0, 0);
		if (src_buf_info != ctx->dec_flush_buf)
			v4l2_m2m_buf_done(&src_buf_info->vb, VB2_BUF_STATE_DONE);
		clean_free_bs_buffer(ctx, NULL);

		goto vdec_worker_finish;
	} else if (src_buf_info->lastframe == EOS_WITH_DATA && ctx->output_async) {
		ctx->eos_type = EOS_WITH_DATA;
		ctx->early_eos_ts = ctx->input_max_ts;
		mtk_v4l2_debug(4, "===>[%d] vdec_if_decode() early EOS ===> %d %d ts %llu",
			ctx->id, src_buf->planes[0].bytesused,
			src_buf->planes[0].length, ctx->early_eos_ts);
	} else
		mtk_v4l2_debug(4, "===>[%d] vdec_if_decode() ===>", ctx->id);

	/* setup input */
	buf = &src_buf_info->bs_buffer;
	if (mtk_v4l2_dbg_level > 0)
		buf->va = vb2_plane_vaddr(src_buf, 0);
	buf->dma_addr = vb2_dma_contig_plane_dma_addr(src_buf, 0);
	buf->size = (size_t)src_buf->planes[0].bytesused;
	buf->length = (size_t)src_buf->planes[0].length;
	buf->data_offset = (size_t)src_buf->planes[0].data_offset;
	buf->dmabuf = src_buf->planes[0].dbuf;
	buf->flags = src_vb2_v4l2->flags;
	buf->index = src_buf->index;
	buf->hdr10plus_buf = &src_buf_info->hdr10plus_buf;

	if (src_buf->vb2_queue->memory == VB2_MEMORY_DMABUF && buf->dmabuf == NULL) {
		mtk_v4l2_err("[%d] id=%d src_addr is NULL!!", ctx->id, src_buf->index);
		goto vdec_worker_finish;
	}
	ctx->bs_list[buf->index + 1] = (uintptr_t)buf;

	ctx->timestamp = src_buf_info->vb.vb2_buf.timestamp;
	mtk_v4l2_debug(1, "[%d] Bs VA=%p DMA=%lx Size=%zx Len=%zx ion_buf = %p vb=%p eos=%d pts=%llu",
		ctx->id, buf->va, (unsigned long)buf->dma_addr,
		buf->size, buf->length, buf->dmabuf, src_buf,
		src_buf_info->lastframe, src_buf_info->vb.vb2_buf.timestamp);
	if (!ctx->output_async) {
		if (dst_buf_info == NULL)
			mtk_v4l2_err("dst_buf_info is NULL");
		else {
			dst_buf_info->flags &= ~CROP_CHANGED;
			dst_buf_info->flags &= ~COLOR_ASPECT_CHANGED;
			dst_buf_info->flags &= ~REF_FREED;

			dst_buf_info->vb.vb2_buf.timestamp
				= src_buf_info->vb.vb2_buf.timestamp;
			dst_buf_info->vb.timecode
				= src_buf_info->vb.timecode;
		}
	}

	if (ctx->low_pw_mode) {
		unsigned long flags;
		bool has_stop;

		spin_lock_irqsave(&ctx->lpw_lock, flags);
		mtk_vdec_lpw_stop_timer(ctx, false);

		if (!ctx->in_group) {
			ctx->in_group = true;
			ctx->group_start_time = jiffies_to_nsecs(jiffies);
			ctx->lpw_set_start_ts = true;
			ctx->group_dec_cnt = 0;
		}
		ctx->group_dec_cnt++;

		src_cnt_before = v4l2_m2m_num_src_bufs_ready(ctx->m2m_ctx);
		dst_cnt_before = v4l2_m2m_num_dst_bufs_ready(ctx->m2m_ctx);
		pair_cnt_before = MIN(src_cnt_before, dst_cnt_before);

		has_stop = mtk_vdec_lpw_check_dec_stop(ctx, false, true, "before dec");
		if (ctx->in_group && (ctx->lpw_state == VDEC_LPW_WAIT || ctx->dynamic_low_latency ||
					(ctx->lpw_dec_start_cnt > 0 && has_stop)))
			ctx->in_group = false;

		if (vdec_if_set_param(ctx, SET_PARAM_VDEC_IN_GROUP, (void *)ctx->in_group) != 0)
			mtk_v4l2_err("[%d] Error!! Cannot set param SET_PARAM_VDEC_IN_GROUP(%d)",
				ctx->id, SET_PARAM_VDEC_IN_GROUP);

		spin_unlock_irqrestore(&ctx->lpw_lock, flags);
	}

	if (src_buf_info->used == false)
		mtk_vdec_ts_insert(ctx, ctx->timestamp);
	src_buf_info->used = true;
	if (!ctx->input_driven && !ctx->use_fence)
		v4l2_m2m_dst_buf_remove(ctx->m2m_ctx);

	mtk_vdec_do_gettimeofday(&worktvstart1);
	ret = vdec_if_decode(ctx, buf, pfb, &src_chg);
	mtk_vdec_do_gettimeofday(&vputvend);
	ts_delta = timespec64_sub(vputvend, worktvstart1);
	mtk_vcodec_perf_log("vpud:%lld (ns)", timespec64_to_ns(&ts_delta));

	res_chg = ((src_chg & VDEC_RES_CHANGE) != 0U) ? true : false;
	need_more_output =
		((src_chg & VDEC_NEED_MORE_OUTPUT_BUF) != 0U) ? true : false;
	mtk_vcodec_unsupport = ((src_chg & VDEC_HW_NOT_SUPPORT) != 0) ?
						   true : false;
	if ((src_chg & VDEC_CROP_CHANGED) &&
	    (!ctx->output_async) && dst_buf_info != NULL)
		dst_buf_info->flags |= CROP_CHANGED;

	if (src_chg & VDEC_COLOR_ASPECT_CHANGED)
		src_vb2_v4l2->flags |= V4L2_BUF_FLAG_COLOR_ASPECT_CHANGED;
	if (src_chg & VDEC_OUTPUT_NOT_GENERATED)
		src_vb2_v4l2->flags |= V4L2_BUF_FLAG_OUTPUT_NOT_GENERATED;

	if (ctx->use_fence)
		v4l2_m2m_dst_buf_remove(ctx->m2m_ctx);
	else if (!ctx->output_async)
		mtk_vdec_put_fb(ctx, PUT_BUFFER_WORKER, false);

	if (ret < 0 || mtk_vcodec_unsupport) {
		mtk_v4l2_err(" <===[%d], src_buf[%d] last_frame = %d sz=0x%zx pts=%llu vdec_if_decode() ret=%d src_chg=%d===>",
			ctx->id, src_buf->index, src_buf_info->lastframe,
			buf->size, src_buf_info->vb.vb2_buf.timestamp, ret, src_chg);
		src_vb2_v4l2 = v4l2_m2m_src_buf_remove(ctx->m2m_ctx);
		clean_free_bs_buffer(ctx, &src_buf_info->bs_buffer);
		if (ret == -EIO) {
			/* ipi timeout / VPUD crashed ctx abort */
			mtk_vdec_error_handle(ctx, "decode");
			v4l2_m2m_buf_done(&src_buf_info->vb, VB2_BUF_STATE_ERROR);
		} else if (mtk_vcodec_unsupport) {
			/*
			 * If cncounter the src unsupport (fatal) during play,
			 * egs: width/height, bitdepth, level, then teturn
			 * error event to user to stop play it
			 */
			mtk_v4l2_err(" <=== [%d] vcodec not support the source!===>", ctx->id);
			mtk_vdec_set_unsupport(ctx);
			v4l2_m2m_buf_done(&src_buf_info->vb, VB2_BUF_STATE_DONE);
		} else
			v4l2_m2m_buf_done(&src_buf_info->vb, VB2_BUF_STATE_ERROR);
	} else if (src_buf_info->lastframe == EOS_WITH_DATA &&
		need_more_output == false) {
		/*
		 * Getting early eos bitstream buffer, after decode this
		 * buffer, need to flush decoder. Use the flush_buf
		 * as normal EOS, and flush decoder.
		 */
		mtk_v4l2_debug(0, "[%d] EarlyEos: decode last frame %d",
			ctx->id, src_buf->planes[0].bytesused);
		src_vb2_v4l2 = v4l2_m2m_src_buf_remove(ctx->m2m_ctx);
		if (src_vb2_v4l2 == NULL) {
			mtk_v4l2_err("Error!! src_vb2_v4l2 is NULL");
			goto vdec_worker_finish;
		}
		src_vb2_v4l2->flags |= V4L2_BUF_FLAG_LAST;
		clean_free_bs_buffer(ctx, NULL);
		if (ctx->dec_flush_buf->lastframe == NON_EOS) {
			ctx->dec_flush_buf->lastframe = EOS;
			ctx->dec_flush_buf->vb.vb2_buf.planes[0].bytesused = 1;
			v4l2_m2m_buf_queue_check(ctx->m2m_ctx, &ctx->dec_flush_buf->vb);
		} else {
			mtk_v4l2_debug(1, "Stopping no need to queue dec_flush_buf.");
		}
	} else if ((ret == 0) && (res_chg == false && need_more_output == false)) {
		/*
		 * we only return src buffer with VB2_BUF_STATE_DONE
		 * when decode success without resolution
		 * change.
		 */
		src_vb2_v4l2 = v4l2_m2m_src_buf_remove(ctx->m2m_ctx);
		clean_free_bs_buffer(ctx, NULL);
	} else {    /* res_chg == true || need_more_output == true*/
		clean_free_bs_buffer(ctx, &src_buf_info->bs_buffer);
		mtk_v4l2_debug(1, "Need more capture buffer  r:%d n:%d\n",
			res_chg, need_more_output);
	}

	if (ret == 0 && res_chg) {
		mtk_vdec_pic_info_update(ctx);
		/*
		 * On encountering a resolution change in the stream.
		 * The driver must first process and decode all
		 * remaining buffers from before the resolution change
		 * point, so call flush decode here
		 */
		mtk_vdec_reset_decoder(ctx, 1, NULL, V4L2_BUF_TYPE_VIDEO_CAPTURE);
		if (ctx->input_driven != NON_INPUT_DRIVEN)
			*(ctx->ipi_blocked) = true;
		/*
		 * After all buffers containing decoded frames from
		 * before the resolution change point ready to be
		 * dequeued on the CAPTURE queue, the driver sends a
		 * V4L2_EVENT_SOURCE_CHANGE event for source change
		 * type V4L2_EVENT_SRC_CH_RESOLUTION
		 */
		mtk_vdec_queue_res_chg_event(ctx);
	} else if (ret == 0) {
		ret = vdec_if_get_param(ctx, GET_PARAM_DPB_SIZE, &dpbsize);
		if (dpbsize != 0) {
			ctx->dpb_size = dpbsize;
			ctx->last_dpb_size = dpbsize;
		} else {
			mtk_v4l2_err("[%d] GET_PARAM_DPB_SIZE fail=%d",
				ctx->id, ret);
		}
		ret = vdec_if_get_param(ctx, GET_PARAM_COLOR_DESC, &color_desc);
		if (ret == 0) {
			ctx->is_hdr = color_desc.hdr_type;
			ctx->last_is_hdr = color_desc.hdr_type;
		} else {
			mtk_v4l2_err("[%d] GET_PARAM_COLOR_DESC fail=%d",
				ctx->id, ret);
		}
	}

	if (ctx->low_pw_mode) {
		int src_cnt, dst_cnt, pair_cnt;
		unsigned long flags;

		spin_lock_irqsave(&ctx->lpw_lock, flags);
		mtk_vdec_lpw_check_dec_stop(ctx, false, false, "after dec");

		src_cnt = v4l2_m2m_num_src_bufs_ready(ctx->m2m_ctx);
		dst_cnt = v4l2_m2m_num_dst_bufs_ready(ctx->m2m_ctx);
		pair_cnt = MIN(src_cnt, dst_cnt);
		if (ctx->in_group && (pair_cnt == 0 || ctx->dynamic_low_latency))
			mtk_lpw_err("[%d] pair cnt before %d(%d,%d) after %d(%d,%d) but in_group %d, lpw_state(%d), dynamic_low_latency %d, lpw_dec_start_cnt %d, group_dec_cnt %d",
				ctx->id, pair_cnt, src_cnt, dst_cnt,
				pair_cnt_before, src_cnt_before, dst_cnt_before, ctx->in_group,
				ctx->lpw_state, ctx->dynamic_low_latency, ctx->lpw_dec_start_cnt, ctx->group_dec_cnt);

		if (ctx->in_group && (ctx->lpw_state == VDEC_LPW_WAIT || ctx->dynamic_low_latency))
			ctx->in_group = false;
		mtk_vdec_lpw_start_timer(ctx);
		spin_unlock_irqrestore(&ctx->lpw_lock, flags);
	} else
		mtk_v4l2_debug(4, "[%d] state %d, src %d, dst %d",
			ctx->id, mtk_vcodec_get_state(ctx),
			v4l2_m2m_num_src_bufs_ready(ctx->m2m_ctx),
			v4l2_m2m_num_dst_bufs_ready(ctx->m2m_ctx));

vdec_worker_finish:
	v4l2_m2m_job_finish(dev->m2m_dev_dec, ctx->m2m_ctx);
	mtk_vdec_do_gettimeofday(&vputvend);
	ts_delta = timespec64_sub(vputvend, worktvstart);
	mtk_vcodec_perf_log("worker:%lld (ns)", timespec64_to_ns(&ts_delta));

	mutex_unlock(&ctx->worker_lock);
}

static int vidioc_try_decoder_cmd(struct file *file, void *priv,
	struct v4l2_decoder_cmd *cmd)
{
	switch (cmd->cmd) {
	case V4L2_DEC_CMD_STOP:
		cmd->flags = 0; // don't support flags
		break;
	case V4L2_DEC_CMD_START:
		cmd->flags = 0; // don't support flags
		if (cmd->start.speed < 0)
			cmd->start.speed = 0;
		cmd->start.format = V4L2_DEC_START_FMT_NONE;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int vidioc_decoder_cmd(struct file *file, void *priv,
	struct v4l2_decoder_cmd *cmd)
{
	struct mtk_vcodec_ctx *ctx = fh_to_ctx(priv);
	struct vb2_queue *src_vq, *dst_vq;
	int ret;

	ret = vidioc_try_decoder_cmd(file, priv, cmd);
	if (ret)
		return ret;

	mtk_v4l2_debug(1, "decoder cmd= %u", cmd->cmd);
	dst_vq = v4l2_m2m_get_vq(ctx->m2m_ctx,
		V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);
	switch (cmd->cmd) {
	case V4L2_DEC_CMD_STOP:
		src_vq = v4l2_m2m_get_vq(ctx->m2m_ctx,
			V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);

		if (mtk_vcodec_is_state(ctx, MTK_STATE_INIT))
			mtk_vdec_queue_error_event(ctx);

		if (src_vq == NULL) {
			mtk_v4l2_err("Error! src_vq is NULL!");
			return -EINVAL;
		}
		if (!vb2_is_streaming(src_vq)) {
			mtk_v4l2_debug(1, "Output stream is off. No need to flush.");
			return 0;
		}
		if (dst_vq == NULL) {
			mtk_v4l2_err("Error! dst_vq is NULL!");
			return -EINVAL;
		}
		if (!vb2_is_streaming(dst_vq)) {
			mtk_v4l2_debug(1, "Capture stream is off. No need to flush.");
			return 0;
		}
		if (ctx->dec_flush_buf->lastframe == NON_EOS) {
			ctx->dec_flush_buf->lastframe = EOS;
			ctx->dec_flush_buf->vb.vb2_buf.planes[0].bytesused = 0;
			v4l2_m2m_buf_queue_check(ctx->m2m_ctx, &ctx->dec_flush_buf->vb);
			mtk_vdec_lpw_switch_reset(ctx, true, "stop cmd EOS");
			v4l2_m2m_try_schedule(ctx->m2m_ctx);
		} else {
			mtk_v4l2_debug(1, "Stopping no need to queue cmd dec_flush_buf.");
		}
		break;

	case V4L2_DEC_CMD_START:
		if (dst_vq == NULL) {
			mtk_v4l2_err("Error! dst_vq is NULL!");
			return -EINVAL;
		}
		vb2_clear_last_buffer_dequeued(dst_vq);
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

void mtk_vdec_unlock(struct mtk_vcodec_ctx *ctx, u32 hw_id)
{
	if (hw_id >= MTK_VDEC_HW_NUM)
		return;

	mtk_v4l2_debug(4, "ctx %p [%d] hw_id %d sem_cnt %d",
		ctx, ctx->id, hw_id, ctx->dev->dec_sem[hw_id].count);

	ctx->hw_locked[hw_id] = 0;
	up(&ctx->dev->dec_sem[hw_id]);
}

int mtk_vdec_lock(struct mtk_vcodec_ctx *ctx, u32 hw_id)
{
	int ret = -1;

	if (hw_id >= MTK_VDEC_HW_NUM)
		return -1;

	mtk_v4l2_debug(4, "ctx %p [%d] hw_id %d sem_cnt %d",
		ctx, ctx->id, hw_id, ctx->dev->dec_sem[hw_id].count);

	if (mtk_vcodec_is_vcp(MTK_INST_DECODER)) {
		down(&ctx->dev->dec_sem[hw_id]);
		ret = 0;
	} else {
		while (ret != 0)
			ret = down_interruptible(&ctx->dev->dec_sem[hw_id]);
	}

	ctx->hw_locked[hw_id] = 1;

	return ret;
}

void mtk_vcodec_dec_empty_queues(struct file *file, struct mtk_vcodec_ctx *ctx)
{
	struct vb2_buffer *dst_buf = NULL;
	int i = 0;
	struct v4l2_fh *fh = file->private_data;
	struct vb2_v4l2_buffer *dst_vb2_v4l2, *src_vb2_v4l2;

	// error handle for release before stream-off
	// stream off both queue mannually.
	v4l2_m2m_streamoff(file, fh->m2m_ctx,
		V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
	v4l2_m2m_streamoff(file, fh->m2m_ctx,
		V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);

	while ((src_vb2_v4l2 = v4l2_m2m_src_buf_remove(ctx->m2m_ctx)))
		if (src_vb2_v4l2 != &ctx->dec_flush_buf->vb &&
			src_vb2_v4l2->vb2_buf.state == VB2_BUF_STATE_ACTIVE)
			v4l2_m2m_buf_done(src_vb2_v4l2, VB2_BUF_STATE_ERROR);

	while ((dst_vb2_v4l2 = v4l2_m2m_dst_buf_remove(ctx->m2m_ctx))) {
		dst_buf = &dst_vb2_v4l2->vb2_buf;

		for (i = 0; i < dst_buf->num_planes; i++)
			vb2_set_plane_payload(dst_buf, i, 0);

		if (dst_vb2_v4l2->vb2_buf.state == VB2_BUF_STATE_ACTIVE)
			v4l2_m2m_buf_done(dst_vb2_v4l2, VB2_BUF_STATE_ERROR);
	}
}

static int mtk_vcodec_dec_init(struct mtk_vcodec_ctx *ctx, struct mtk_q_data *q_data)
{
	int ret = 0;

	mutex_lock(&ctx->init_lock);

	if (!mtk_vcodec_is_state(ctx, MTK_STATE_FREE)) {
		mutex_unlock(&ctx->init_lock);
		return 0;
	}

	ret = vdec_if_init(ctx, q_data->fmt->fourcc);
	v4l2_m2m_set_dst_buffered(ctx->m2m_ctx, ctx->input_driven != NON_INPUT_DRIVEN);
	if (ctx->input_driven == INPUT_DRIVEN_CB_FRM)
		init_waitqueue_head(&ctx->fm_wq);
	if (ret) {
		mtk_v4l2_err("[%d]: vdec_if_init() fail ret=%d", ctx->id, ret);
		if (ret == -EIO)
			mtk_vdec_error_handle(ctx, "init");
		else
			mtk_vdec_set_unsupport(ctx);
	} else
		mtk_vcodec_set_state_from(ctx, MTK_STATE_INIT, MTK_STATE_FREE);

	mutex_unlock(&ctx->init_lock);
	return ret;
}

void mtk_vcodec_dec_release(struct mtk_vcodec_ctx *ctx)
{
#if ENABLE_META_BUF
	int i;
#endif

	mtk_vdec_deinit_set_frame_wq(ctx);
	mtk_vdec_lpw_deinit_timer(ctx);
	vdec_if_deinit(ctx);
	mtk_vcodec_set_state(ctx, MTK_STATE_FREE);
	vdec_check_release_lock(ctx);

	release_all_general_buffer_info(ctx);

#if ENABLE_META_BUF
	for (i = 0; i < MAX_META_BUF_CNT; i++) {
		if (ctx->dma_meta_list[i].dmabuf) {
			struct dma_buf *dmabuf = ctx->dma_meta_list[i].dmabuf;
			struct dma_buf_attachment *buf_att = ctx->dma_meta_list[i].buf_att;
			struct sg_table *sgt = ctx->dma_meta_list[i].sgt;

			mtk_vcodec_dma_unmap_detach(dmabuf, &buf_att, &sgt, DMA_TO_DEVICE);
			dma_buf_put(dmabuf);
		}
	}
	memset(ctx->dma_meta_list, 0, sizeof(struct dma_meta_buf) * MAX_META_BUF_CNT);
#endif

	mtk_vdec_cgrp_deinit(ctx);
}

static bool mtk_vdec_dvfs_params_change(struct mtk_vcodec_ctx *ctx)
{
	bool need_update = false;

	if (mtk_vcodec_is_state(ctx, MTK_STATE_HEADER)) {
		if (!ctx->is_active)
			need_update = true;

		if (ctx->dec_params.dec_param_change & MTK_DEC_PARAM_OPERATING_RATE) {
			need_update = true;
			ctx->dec_params.dec_param_change &= (~MTK_DEC_PARAM_OPERATING_RATE);
		}

	}
	return need_update;
}
/*
 * check decode ctx is active or not
 * active: ctx is decoding frame
 * inactive: ctx is not decoding frame, which is in the background
 * timer pushes this work to workqueue
 */
void mtk_vdec_check_alive_work(struct work_struct *ws)
{
	struct mtk_vcodec_dev *dev = NULL;
	struct mtk_vcodec_ctx *ctx = NULL, *valid_ctx = NULL;
	struct vcodec_inst *inst;
	struct list_head *item;
	bool mmdvfs_in_vcp, need_update = false;
	struct vdec_check_alive_work_struct *caws;
	unsigned long vcp_dvfs_data[1] = {0};
	int ret;

	caws = container_of(ws, struct vdec_check_alive_work_struct, work);
	dev = caws->dev;

	mmdvfs_in_vcp = dev->vdec_dvfs_params.mmdvfs_in_vcp;

	mutex_lock(&dev->dec_dvfs_mutex);

	if (list_empty(&dev->vdec_dvfs_inst) || dev->is_codec_suspending == 1) {
		if (caws->ctx != NULL)
			kfree(caws);
		mutex_unlock(&dev->dec_dvfs_mutex);
		return;
	}

	if (caws->ctx != NULL) { // ctx retrigger case
		ctx = caws->ctx;
		valid_ctx = ctx;
		// cur ctx should be in dvfs list
		list_for_each(item, &dev->vdec_dvfs_inst) {
			inst = list_entry(item, struct vcodec_inst, list);
			if (inst->ctx == ctx)
				need_update = true;
		}
		if (!need_update) {
			kfree(caws);
			mutex_unlock(&dev->dec_dvfs_mutex);
			return;
		}
		ctx->is_active = 1;
		mtk_vdec_dvfs_update_dvfs_params(ctx);
		kfree(caws);
		mtk_v4l2_debug(0, "[VDVFS] %s [%d] is active/update_params now", __func__, ctx->id);
	} else { // timer trigger case
		list_for_each(item, &dev->vdec_dvfs_inst) {
			inst = list_entry(item, struct vcodec_inst, list);
			ctx = inst->ctx;
			mtk_v4l2_debug(8, "[VDVFS] ctx:%d, active:%d, decoded_cnt:%d, last_decoded_cnt:%d",
				ctx->id, ctx->is_active, ctx->decoded_frame_cnt,
				ctx->last_decoded_frame_cnt);
			if (!mtk_vcodec_is_state(ctx, MTK_STATE_ABORT)) {
				valid_ctx = ctx;
				if (ctx->last_decoded_frame_cnt >= ctx->decoded_frame_cnt) {
					if (ctx->is_active) {
						need_update = true;
						ctx->is_active = 0;
						mtk_vdec_dvfs_update_dvfs_params(ctx);
						mtk_v4l2_debug(0, "[VDVFS] ctx %d inactive",
							ctx->id);
					}
				} else {
					if (!ctx->is_active) {
						need_update = true;
						ctx->is_active = 1;
						mtk_vdec_dvfs_update_dvfs_params(ctx);
						mtk_v4l2_debug(0, "[VDVFS] ctx %d active", ctx->id);
					}
					ctx->last_decoded_frame_cnt = ctx->decoded_frame_cnt;
				}
			} else
				mtk_v4l2_err("[VDVFS] ctx: %d is abort", ctx->id);
		}
	}

	if (need_update) {
		if (mmdvfs_in_vcp) {
			vcp_dvfs_data[0] = MTK_INST_SET;
			// can use any valid ctx to set param
			ctx = valid_ctx;
			ret = vdec_if_set_param(ctx, SET_PARAM_MMDVFS, vcp_dvfs_data);
			if (ret != 0)
				mtk_v4l2_err("[VDVFS][%d] alive ipi fail, ret %d", ctx->id, ret);
			mtk_vdec_dvfs_sync_vsi_data(ctx);
			mtk_v4l2_debug(0, "[VDVFS] check alive: freq: %d, op: %d",
				ctx->dev->vdec_dvfs_params.target_freq,
				ctx->dec_params.operating_rate);
		} else {
			mtk_vdec_force_update_freq(dev);
		}
		mtk_vdec_pmqos_begin_inst(ctx);
	}

	mutex_unlock(&dev->dec_dvfs_mutex);
}

void mtk_vcodec_dec_set_default_params(struct mtk_vcodec_ctx *ctx)
{
	struct mtk_q_data *q_data;

	mtk_vdec_cgrp_init(ctx);
	mtk_vdec_trigger_cgrp(ctx);

	ctx->m2m_ctx->q_lock = &ctx->q_mutex;
	ctx->fh.m2m_ctx = ctx->m2m_ctx;
	ctx->fh.ctrl_handler = &ctx->ctrl_hdl;
	INIT_WORK(&ctx->decode_work, mtk_vdec_worker);
	ctx->colorspace = V4L2_COLORSPACE_REC709;
	ctx->ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	ctx->quantization = V4L2_QUANTIZATION_DEFAULT;
	ctx->xfer_func = V4L2_XFER_FUNC_DEFAULT;

	get_supported_format(ctx);
	get_supported_framesizes(ctx);

#if IS_ENABLED(CONFIG_MTK_TINYSYS_VCP_SUPPORT)
	if (mtk_vcodec_is_vcp(MTK_INST_DECODER)) {
		set_vdec_vcp_data(ctx, VDEC_VCP_LOG_INFO_ID);
		set_vdec_vcp_data(ctx, VDEC_SET_PROP_MEM_ID);
	}
#endif

	q_data = &ctx->q_data[MTK_Q_DATA_SRC];
	memset(q_data, 0, sizeof(struct mtk_q_data));
	q_data->visible_width = DFT_CFG_WIDTH;
	q_data->visible_height = DFT_CFG_HEIGHT;
	if (default_out_fmt_idx < MTK_MAX_DEC_CODECS_SUPPORT)
		q_data->fmt = &mtk_vdec_formats[default_out_fmt_idx];
	q_data->field = V4L2_FIELD_NONE;

	q_data->sizeimage[0] = DFT_CFG_WIDTH * DFT_CFG_HEIGHT;
	q_data->bytesperline[0] = 0;

	q_data = &ctx->q_data[MTK_Q_DATA_DST];
	memset(q_data, 0, sizeof(struct mtk_q_data));
	q_data->visible_width = DFT_CFG_WIDTH;
	q_data->visible_height = DFT_CFG_HEIGHT;
	q_data->coded_width = DFT_CFG_WIDTH;
	q_data->coded_height = DFT_CFG_HEIGHT;
	if (default_cap_fmt_idx < MTK_MAX_DEC_CODECS_SUPPORT)
		q_data->fmt = &mtk_vdec_formats[default_cap_fmt_idx];
	q_data->field = V4L2_FIELD_NONE;

	v4l_bound_align_image(&q_data->coded_width,
						  MTK_VDEC_MIN_W,
						  MTK_VDEC_MAX_W, 4,
						  &q_data->coded_height,
						  MTK_VDEC_MIN_H,
						  MTK_VDEC_MAX_H, 5, 6);

	if (q_data->fmt->num_planes == 1) {
		q_data->sizeimage[0] =
			q_data->coded_width * q_data->coded_height * 3/2;
		q_data->bytesperline[0] = q_data->coded_width;

	} else if (q_data->fmt->num_planes == 2) {
		q_data->sizeimage[0] =
			q_data->coded_width * q_data->coded_height;
		q_data->bytesperline[0] = q_data->coded_width;
		q_data->sizeimage[1] = q_data->sizeimage[0] / 2;
		q_data->bytesperline[1] = q_data->coded_width;
	}

	mtk_vdec_init_set_frame_wq(ctx);
	mtk_vdec_lpw_init_timer(ctx);
}

static int mtk_vdec_set_param(struct mtk_vcodec_ctx *ctx)
{
	unsigned long in[8] = {0};
	bool need_set_param = (ctx->dec_params.dec_param_change != 0);

	vcodec_trace_begin_func();

	mtk_v4l2_debug(4,
		"[%d] param change 0x%x decode mode %d max width %d max height %d",
		ctx->id, ctx->dec_params.dec_param_change,
		ctx->dec_params.decode_mode,
		ctx->dec_params.fixed_max_frame_size_width,
		ctx->dec_params.fixed_max_frame_size_height);

	if (ctx->dec_params.dec_param_change & MTK_DEC_PARAM_DECODE_MODE) {
		in[0] = ctx->dec_params.decode_mode;
		if (vdec_if_set_param(ctx, SET_PARAM_DECODE_MODE, in) != 0) {
			mtk_v4l2_err("[%d] Error!! Cannot set param", ctx->id);
			return -EINVAL;
		}
		ctx->dec_params.dec_param_change &= (~MTK_DEC_PARAM_DECODE_MODE);
	}

	if (ctx->dec_params.dec_param_change & MTK_DEC_PARAM_FIXED_MAX_FRAME_SIZE) {
		in[0] = ctx->dec_params.fixed_max_frame_size_width;
		in[1] = ctx->dec_params.fixed_max_frame_size_height;
		in[2] = ctx->dec_params.fixed_max_frame_buffer_mode;
		if (in[0] != 0 && in[1] != 0) {
			if (vdec_if_set_param(ctx,
				SET_PARAM_SET_FIXED_MAX_OUTPUT_BUFFER,
				in) != 0) {
				mtk_v4l2_err("[%d] Error!! Cannot set param",
					ctx->id);
				return -EINVAL;
			}
		}
		ctx->dec_params.dec_param_change &= (~MTK_DEC_PARAM_FIXED_MAX_FRAME_SIZE);
	}

	if (ctx->dec_params.dec_param_change & MTK_DEC_PARAM_CRC_PATH) {
		in[0] = (unsigned long)ctx->dec_params.crc_path;
		if (vdec_if_set_param(ctx, SET_PARAM_CRC_PATH, in) != 0) {
			mtk_v4l2_err("[%d] Error!! Cannot set param", ctx->id);
			return -EINVAL;
		}
		ctx->dec_params.dec_param_change &= (~MTK_DEC_PARAM_CRC_PATH);
	}

	if (ctx->dec_params.dec_param_change & MTK_DEC_PARAM_GOLDEN_PATH) {
		in[0] = (unsigned long)ctx->dec_params.golden_path;
		if (vdec_if_set_param(ctx, SET_PARAM_GOLDEN_PATH, in) != 0) {
			mtk_v4l2_err("[%d] Error!! Cannot set param", ctx->id);
			return -EINVAL;
		}
		ctx->dec_params.dec_param_change &= (~MTK_DEC_PARAM_GOLDEN_PATH);
	}

	if (ctx->dec_params.dec_param_change & MTK_DEC_PARAM_WAIT_KEY_FRAME) {
		in[0] = (unsigned long)ctx->dec_params.wait_key_frame;
		if (vdec_if_set_param(ctx, SET_PARAM_WAIT_KEY_FRAME, in) != 0) {
			mtk_v4l2_err("[%d] Error!! Cannot set param", ctx->id);
			return -EINVAL;
		}
		ctx->dec_params.dec_param_change &= (~MTK_DEC_PARAM_WAIT_KEY_FRAME);
	}

	if (ctx->dec_params.dec_param_change & MTK_DEC_PARAM_DECODE_ERROR_HANDLE_MODE) {
		in[0] = (unsigned long)ctx->dec_params.decode_error_handle_mode;
		if (vdec_if_set_param(ctx, SET_PARAM_DECODE_ERROR_HANDLE_MODE, in) != 0) {
			mtk_v4l2_err("[%d] Error!! Cannot set param", ctx->id);
			return -EINVAL;
		}
		ctx->dec_params.dec_param_change &= (~MTK_DEC_PARAM_DECODE_ERROR_HANDLE_MODE);
	}

	if (ctx->dec_params.dec_param_change & MTK_DEC_PARAM_OPERATING_RATE) {
		in[0] = (unsigned long)ctx->dec_params.operating_rate;
		if (vdec_if_set_param(ctx, SET_PARAM_OPERATING_RATE, in) != 0) {
			mtk_v4l2_err("[%d] Error!! Cannot set param", ctx->id);
			return -EINVAL;
		}
		ctx->dec_params.dec_param_change &= (~MTK_DEC_PARAM_OPERATING_RATE);
	}

	if (need_set_param) {
		if (vdec_if_set_param(ctx, SET_PARAM_DEC_PARAMS, NULL) != 0) {
			mtk_v4l2_err("[%d] Error!! Cannot set param", ctx->id);
			return -EINVAL;
		}
	}

	if (ctx->dec_params.dec_param_change & MTK_DEC_PARAM_LINECOUNT_THRESHOLD) {
		in[0] = ctx->dec_params.linecount_threshold_mode;
		mtk_v4l2_debug(4, "[%d] MTK_DEC_PARAM_LINECOUNT_THRESHOLD mode %lu",
			ctx->id, in[0]);
		if (vdec_if_set_param(ctx, SET_PARAM_VDEC_LINECOUNT_THRESHOLD, in) != 0) {
			mtk_v4l2_err("[%d] Error!! Cannot set param", ctx->id);
			return -EINVAL;
		}
		ctx->dec_params.dec_param_change &= (~MTK_DEC_PARAM_LINECOUNT_THRESHOLD);
	}

	if (vdec_if_get_param(ctx, GET_PARAM_INPUT_DRIVEN, &ctx->input_driven)) {
		mtk_v4l2_err("[%d] Error!! Cannot get param : GET_PARAM_INPUT_DRIVEN ERR",
			ctx->id);
		return -EINVAL;
	}

	if (vdec_if_get_param(ctx, GET_PARAM_OUTPUT_ASYNC, &ctx->output_async)) {
		mtk_v4l2_err("[%d] Error!! Cannot get param : GET_PARAM_OUTPUT_ASYNC ERR",
			ctx->id);
		return -EINVAL;
	}

	if (vdec_if_get_param(ctx, GET_PARAM_LOW_POWER_MODE, &ctx->low_pw_mode)) {
		mtk_v4l2_err("[%d] Error!! Cannot get param : GET_PARAM_LOW_POWER_MODE ERR",
			ctx->id);
		return -EINVAL;
	}

	mtk_v4l2_debug(4, "[%d] input_driven %d output_async %d ipi_blocked %d, low_pw_mode %d",
		ctx->id, ctx->input_driven, ctx->output_async,
		*(ctx->ipi_blocked), ctx->low_pw_mode);

	vcodec_trace_end();

	return 0;
}

static int vidioc_vdec_reqbufs(struct file *file, void *priv, struct v4l2_requestbuffers *rb)
{
	struct mtk_vcodec_ctx *ctx = fh_to_ctx(priv);
	struct vb2_queue *vq = v4l2_m2m_get_vq(ctx->m2m_ctx, rb->type);
	bool cap_req_0 = false;
	int ret;

	if (rb->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE && rb->count == 0 && vq != NULL && vq->num_buffers > 0)
		cap_req_0 = true;

	mtk_v4l2_debug(cap_req_0 ? 0 : 1, "[%d] reqbufs count %d, type %d, ctx state %d",
		ctx->id, rb->count, rb->type, mtk_vcodec_get_state(ctx));

	if (cap_req_0 && (mtk_vcodec_is_state(ctx, MTK_STATE_HEADER) || mtk_vcodec_is_state(ctx, MTK_STATE_STOP)))
		mtk_vdec_reset_decoder(ctx, false, NULL, rb->type);

	ret = v4l2_m2m_reqbufs(file, ctx->m2m_ctx, rb);

	if (rb->count == 0) {
		if (rb->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
			memset(ctx->fb_list, 0, sizeof(ctx->fb_list));
		else if (rb->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
			memset(ctx->bs_list, 0, sizeof(ctx->bs_list));
	}

	return ret;
}

static int vidioc_vdec_qbuf(struct file *file, void *priv, struct v4l2_buffer *buf)
{
	struct mtk_vcodec_ctx *ctx = fh_to_ctx(priv);
	struct vb2_queue *vq;
	struct vb2_buffer *vb;
	struct mtk_video_dec_buf *mtkbuf;
	struct vb2_v4l2_buffer  *vb2_v4l2;

	if (mtk_vcodec_is_state(ctx, MTK_STATE_ABORT) || ctx->is_unsupport) {
		mtk_v4l2_err("[%d] Call on QBUF after unrecoverable error",
					 ctx->id);
		return -EIO;
	}

	vq = v4l2_m2m_get_vq(ctx->m2m_ctx, buf->type);
	if (vq == NULL) {
		mtk_v4l2_err("Error! vq is NULL!");
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
	mtkbuf = container_of(vb2_v4l2, struct mtk_video_dec_buf, vb);

	if (buf->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		memcpy(&mtkbuf->hdr10plus_buf, &ctx->hdr10plus_buf,
			sizeof(struct hdr10plus_info));
		memset(&ctx->hdr10plus_buf, 0,
			sizeof(struct hdr10plus_info)); // invalidate
		ctx->input_max_ts =
			(timeval_to_ns(&buf->timestamp) > ctx->input_max_ts) ?
			timeval_to_ns(&buf->timestamp) : ctx->input_max_ts;
		if (buf->m.planes[0].bytesused == 0) {
			mtkbuf->lastframe = EOS;
			mtk_v4l2_debug(1, "[%d] index=%d Eos BS(%d,%d) vb=%p pts=%llu",
				ctx->id, buf->index,
				buf->bytesused,
				buf->length, vb,
				timeval_to_ns(&buf->timestamp));
			if (mtk_vcodec_is_state(ctx, MTK_STATE_INIT))
				mtk_vdec_queue_error_event(ctx);
		} else if (buf->flags & V4L2_BUF_FLAG_LAST) {
			mtkbuf->lastframe = EOS_WITH_DATA;
			mtk_v4l2_debug(1, "[%d] id=%d EarlyEos BS(%d,%d) vb=%p pts=%llu",
				ctx->id, buf->index, buf->m.planes[0].bytesused,
				buf->length, vb,
				timeval_to_ns(&buf->timestamp));
		} else {
			mtkbuf->lastframe = NON_EOS;
			mtk_v4l2_debug(1, "[%d] id=%d getdata BS(%d,%d) vb=%p pts=%llu %llu",
				ctx->id, buf->index,
				buf->m.planes[0].bytesused,
				buf->length, vb,
				timeval_to_ns(&buf->timestamp),
				ctx->input_max_ts);
		}
	} else {
		if (buf->reserved == 0xFFFFFFFF || buf->reserved == 0)
			mtkbuf->general_user_fd = -1;
		else
			mtkbuf->general_user_fd = (int)buf->reserved;
		mtk_v4l2_debug(1, "[%d] id=%d FB (%d) vb=%p, general_buf_fd=%d, mtkbuf->general_buf_fd = %d",
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

	return v4l2_m2m_qbuf(file, ctx->m2m_ctx, buf);
}

static int vidioc_vdec_dqbuf(struct file *file, void *priv,
	struct v4l2_buffer *buf)
{
	int ret = 0;
	struct mtk_vcodec_ctx *ctx = fh_to_ctx(priv);
	struct vb2_queue *vq;
	struct vb2_buffer *vb;
	struct mtk_video_dec_buf *mtkbuf;
	struct vb2_v4l2_buffer  *vb2_v4l2;

	if (mtk_vcodec_is_state(ctx, MTK_STATE_ABORT) || ctx->is_unsupport) {
		mtk_v4l2_debug(4, "[%d] Call on DQBUF after unrecoverable error",
					 ctx->id);
		return -EIO;
	}

	ret = v4l2_m2m_dqbuf(file, ctx->m2m_ctx, buf);
	if (ctx->errormap_info[buf->index % VB2_MAX_FRAME])
		buf->flags |= V4L2_BUF_FLAG_ERROR;

	if (buf->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE &&
		ret == 0) {
		vq = v4l2_m2m_get_vq(ctx->m2m_ctx, buf->type);
		if (vq == NULL) {
			mtk_v4l2_err("Error! vq is NULL!");
			return -EINVAL;
		}
		if (buf->index >= vq->num_buffers) {
			mtk_v4l2_err("[%d] buffer index %d out of range %d",
				ctx->id, buf->index, vq->num_buffers);
			return -EINVAL;
		}
		vb = vq->bufs[buf->index];
		vb2_v4l2 = container_of(vb, struct vb2_v4l2_buffer, vb2_buf);
		mtkbuf = container_of(vb2_v4l2, struct mtk_video_dec_buf, vb);

		if (mtkbuf->flags & CROP_CHANGED)
			buf->flags |= V4L2_BUF_FLAG_CROP_CHANGED;
		if (mtkbuf->flags & COLOR_ASPECT_CHANGED)
			buf->flags |= V4L2_BUF_FLAG_COLOR_ASPECT_CHANGED;
		if (mtkbuf->flags & REF_FREED)
			buf->flags |= V4L2_BUF_FLAG_REF_FREED;
		if (mtkbuf->general_user_fd < 0)
			buf->reserved = 0xFFFFFFFF;
		else
			buf->reserved = mtkbuf->general_user_fd;
		mtk_v4l2_debug(2,
			"dqbuf index %d mtkbuf->general_buf_fd = %d, flags 0x%x(0x%x)",
			buf->index, mtkbuf->general_user_fd,
			buf->flags, mtkbuf->flags);

#if ENABLE_FENCE
#if ENABLE_META_BUF
		if (ctx->use_fence && mtkbuf->ready_to_display
			&& mtkbuf->general_dma_va) {
			int *dma_va = mtkbuf->general_dma_va;
			//create fence
			struct mtk_sync_create_fence_data f_data = {0};

			mutex_lock(&ctx->meta_buf_lock);
			if (mtk_vcodec_is_state(ctx, MTK_STATE_FLUSH)) {
				mutex_unlock(&ctx->meta_buf_lock);
				mtk_v4l2_debug(2, "invalid dma_va %p!\n",
					dma_va);
				return ret;
			}

			f_data.value = ctx->fence_idx;
			SPRINTF(f_data.name, "vdec_fence%d",
				f_data.value);
			ret = fence_create(ctx->p_timeline_obj,
				&f_data);
			mtk_v4l2_debug(2, "fence_create id %d ret %d",
				ctx->fence_idx, ret);
			if (ret == 0) {
				*dma_va = f_data.fence;
				mtk_v4l2_debug(2, "save fence fd %d  %d at %p disp %d used %d",
					f_data.fence, f_data.value, dma_va,
					mtkbuf->ready_to_display, mtkbuf->used);
				++ctx->fence_idx;
			}
			mutex_unlock(&ctx->meta_buf_lock);
		}
#endif
#endif
	}

	return ret;
}

static int vidioc_vdec_querycap(struct file *file, void *priv,
	struct v4l2_capability *cap)
{
	struct mtk_vcodec_ctx *ctx = fh_to_ctx(priv);
	struct mtk_vcodec_dev *dev = ctx->dev;

	strscpy(cap->driver, MTK_VCODEC_DEC_NAME, sizeof(cap->driver));
	strscpy(cap->bus_info, dev->platform, sizeof(cap->bus_info));
	strscpy(cap->card, dev->platform, sizeof(cap->card));

	cap->device_caps  = V4L2_CAP_VIDEO_M2M_MPLANE | V4L2_CAP_STREAMING;
	cap->capabilities = cap->device_caps | V4L2_CAP_DEVICE_CAPS;

	return 0;
}

static int vidioc_vdec_subscribe_evt(struct v4l2_fh *fh,
	const struct v4l2_event_subscription *sub)
{
	switch (sub->type) {
	case V4L2_EVENT_EOS:
		return v4l2_event_subscribe(fh, sub, 2, NULL);
	case V4L2_EVENT_SOURCE_CHANGE:
		return v4l2_src_change_event_subscribe(fh, sub);
	case V4L2_EVENT_MTK_VDEC_ERROR:
		return v4l2_event_subscribe(fh, sub, 0, NULL);
	case V4L2_EVENT_MTK_VDEC_NOHEADER:
		return v4l2_event_subscribe(fh, sub, 0, NULL);
	case V4L2_EVENT_MTK_VDEC_ERROR_INFO:
		return v4l2_event_subscribe(fh, sub, 0, NULL);
	default:
		return v4l2_ctrl_subscribe_event(fh, sub);
	}
}

static int vidioc_try_fmt(struct v4l2_format *f, struct mtk_video_fmt *fmt, struct mtk_vcodec_ctx *ctx)
{
	struct v4l2_pix_format_mplane *pix_fmt_mp = NULL;
	unsigned int i;

	if (IS_ERR_OR_NULL(fmt)) {
		mtk_v4l2_err("fail to get mtk_video_fmt");
		return -EINVAL;
	}
	pix_fmt_mp = &f->fmt.pix_mp;
	if (pix_fmt_mp->field <= V4L2_FIELD_ANY || pix_fmt_mp->field > V4L2_FIELD_INTERLACED_BT)
		pix_fmt_mp->field = V4L2_FIELD_NONE;

	if (f->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		pix_fmt_mp->num_planes = 1;
		pix_fmt_mp->plane_fmt[0].bytesperline = 0;
	} else if (f->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
		int tmp_w, tmp_h;

		pix_fmt_mp->height = clamp(pix_fmt_mp->height,
			MTK_VDEC_MIN_H,
			MTK_VDEC_MAX_H);
		pix_fmt_mp->width = clamp(pix_fmt_mp->width,
			MTK_VDEC_MIN_W,
			MTK_VDEC_MAX_W);

		/*
		 * Find next closer width align 64, heign align 64, size align
		 * 64 rectangle
		 * Note: This only get default value, the real HW needed value
		 *       only available when ctx in MTK_STATE_HEADER state
		 */
		tmp_w = pix_fmt_mp->width;
		tmp_h = pix_fmt_mp->height;
		v4l_bound_align_image(&pix_fmt_mp->width,
							  MTK_VDEC_MIN_W,
							  MTK_VDEC_MAX_W, 6,
							  &pix_fmt_mp->height,
							  MTK_VDEC_MIN_H,
							  MTK_VDEC_MAX_H, 6, 9);

		if (pix_fmt_mp->width < tmp_w &&
			(pix_fmt_mp->width + 64) <= MTK_VDEC_MAX_W)
			pix_fmt_mp->width += 64;
		if (pix_fmt_mp->height < tmp_h &&
			(pix_fmt_mp->height + 64) <= MTK_VDEC_MAX_H)
			pix_fmt_mp->height += 64;

		mtk_v4l2_debug((tmp_w != pix_fmt_mp->width || tmp_h != pix_fmt_mp->height) ? 0 : 1,
			"before resize width=%d, height=%d, after resize width=%d, height=%d, sizeimage=%d",
			tmp_w, tmp_h, pix_fmt_mp->width,
			pix_fmt_mp->height,
			pix_fmt_mp->width * pix_fmt_mp->height);

		if (fmt->num_planes > 2)
			pix_fmt_mp->num_planes = 2;
		else
			pix_fmt_mp->num_planes = fmt->num_planes;

		pix_fmt_mp->plane_fmt[0].bytesperline = pix_fmt_mp->width;
		if (pix_fmt_mp->num_planes == 2)
			pix_fmt_mp->plane_fmt[1].bytesperline = pix_fmt_mp->width;

		if (pix_fmt_mp->plane_fmt[0].sizeimage == 0 &&
			mtk_vcodec_state_in_range(ctx, MTK_STATE_HEADER, MTK_STATE_NULL)) {
			for (i = 0; i < pix_fmt_mp->num_planes; i++) {
				pix_fmt_mp->plane_fmt[i].sizeimage = ctx->picinfo.fb_sz[i];
				mtk_v4l2_debug(4, "plane %u sizeimage %u\n", i, ctx->picinfo.fb_sz[i]);
			}
		} else {
			pix_fmt_mp->plane_fmt[0].sizeimage = pix_fmt_mp->width * pix_fmt_mp->height;
			if (pix_fmt_mp->num_planes == 2)
				pix_fmt_mp->plane_fmt[1].sizeimage =
					(pix_fmt_mp->width * pix_fmt_mp->height) / 2;
			else if (pix_fmt_mp->num_planes == 1)
				pix_fmt_mp->plane_fmt[0].sizeimage +=
					(pix_fmt_mp->width * pix_fmt_mp->height) / 2;
		}
	}

	for (i = 0; i < pix_fmt_mp->num_planes; i++)
		memset(&(pix_fmt_mp->plane_fmt[i].reserved[0]), 0x0,
			   sizeof(pix_fmt_mp->plane_fmt[0].reserved));

	pix_fmt_mp->flags = 0;
	memset(&pix_fmt_mp->reserved, 0x0, sizeof(pix_fmt_mp->reserved));
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

	fmt = mtk_vdec_find_format(ctx, f, MTK_FMT_FRAME);
	if (!fmt && default_cap_fmt_idx < MTK_MAX_DEC_CODECS_SUPPORT) {
		f->fmt.pix.pixelformat =
			mtk_vdec_formats[default_cap_fmt_idx].fourcc;
		fmt = mtk_vdec_find_format(ctx, f, MTK_FMT_FRAME);
	}
	if (!fmt)
		return -EINVAL;

	return vidioc_try_fmt(f, fmt, ctx);
}

static int vidioc_try_fmt_vid_out_mplane(struct file *file, void *priv,
	struct v4l2_format *f)
{
	struct v4l2_pix_format_mplane *pix_fmt_mp = &f->fmt.pix_mp;
	struct mtk_video_fmt *fmt;
	struct mtk_vcodec_ctx *ctx = fh_to_ctx(priv);

	if (IS_ERR_OR_NULL(f)) {
		mtk_v4l2_err("fail to get v4l2_format");
		return -EINVAL;
	}

	fmt = mtk_vdec_find_format(ctx, f, MTK_FMT_DEC);
	if (!fmt && default_out_fmt_idx < MTK_MAX_DEC_CODECS_SUPPORT) {
		f->fmt.pix.pixelformat =
			mtk_vdec_formats[default_out_fmt_idx].fourcc;
		fmt = mtk_vdec_find_format(ctx, f, MTK_FMT_DEC);
	}

	if (pix_fmt_mp->plane_fmt[0].sizeimage == 0) {
		mtk_v4l2_err("sizeimage of output format must be given");
		return -EINVAL;
	}
	if (!fmt)
		return -EINVAL;

	return vidioc_try_fmt(f, fmt, ctx);
}

static int vidioc_vdec_g_selection(struct file *file, void *priv,
	struct v4l2_selection *s)
{
	struct mtk_vcodec_ctx *ctx = fh_to_ctx(priv);
	struct mtk_q_data *q_data;

	if (s->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	q_data = &ctx->q_data[MTK_Q_DATA_DST];

	switch (s->target) {
	case V4L2_SEL_TGT_COMPOSE_DEFAULT:
	case V4L2_SEL_TGT_CROP_DEFAULT:
		s->r.left = 0;
		s->r.top = 0;
		s->r.width = ctx->picinfo.pic_w;
		s->r.height = ctx->picinfo.pic_h;
		break;
	case V4L2_SEL_TGT_COMPOSE_BOUNDS:
	case V4L2_SEL_TGT_CROP_BOUNDS:
		s->r.left = 0;
		s->r.top = 0;
		s->r.width = ctx->picinfo.buf_w;
		s->r.height = ctx->picinfo.buf_h;
		break;
	case V4L2_SEL_TGT_COMPOSE:
	case V4L2_SEL_TGT_CROP:
		if (vdec_if_get_param(ctx, GET_PARAM_CROP_INFO, &(s->r))) {
			/* set to default value if header info not ready yet*/
			s->r.left = 0;
			s->r.top = 0;
			s->r.width = q_data->visible_width;
			s->r.height = q_data->visible_height;
		}
		break;
	default:
		return -EINVAL;
	}

	if (mtk_vcodec_state_in_range(ctx, MTK_STATE_NULL, MTK_STATE_INIT)) { // < HEADER
		/* set to default value if header info not ready yet*/
		s->r.left = 0;
		s->r.top = 0;
		s->r.width = q_data->visible_width;
		s->r.height = q_data->visible_height;
		return 0;
	}

	return 0;
}

static int vidioc_vdec_s_selection(struct file *file, void *priv,
	struct v4l2_selection *s)
{
	struct mtk_vcodec_ctx *ctx = fh_to_ctx(priv);

	if (s->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	switch (s->target) {
	case V4L2_SEL_TGT_COMPOSE:
	case V4L2_SEL_TGT_CROP:
		return vdec_if_set_param(ctx, SET_PARAM_CROP_INFO, &s->r);
	default:
		return -EINVAL;
	}

	return 0;
}

static int vidioc_g_parm(struct file *file, void *priv,
			 struct v4l2_streamparm *parm)
{
	struct mtk_vcodec_ctx *ctx = fh_to_ctx(priv);
	struct v4l2_captureparm *cp = &parm->parm.capture;

	if (parm->type != V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
		return -EINVAL;

	if (mtk_vcodec_state_in_range(ctx, MTK_STATE_NULL, MTK_STATE_INIT)) { // < HEADER
		mtk_v4l2_err("can't get parm at state %d", mtk_vcodec_get_state(ctx));
		return -EINVAL;
	}

	memset(cp, 0, sizeof(struct v4l2_captureparm));
	cp->capability = V4L2_CAP_TIMEPERFRAME;

	if (vdec_if_get_param(ctx, GET_PARAM_FRAME_INTERVAL,
			&cp->timeperframe) != 0) {
		mtk_v4l2_err("[%d] Error!! Cannot get frame rate",
			ctx->id);
		return -EINVAL;
	}

	return 0;
}

static int vidioc_vdec_s_fmt(struct file *file, void *priv,
							 struct v4l2_format *f)
{
	struct mtk_vcodec_ctx *ctx = fh_to_ctx(priv);
	struct v4l2_pix_format_mplane *pix_mp;
	struct mtk_q_data *q_data;
	int ret = 0;
	struct mtk_video_fmt *fmt;

	if (IS_ERR_OR_NULL(f)) {
		mtk_v4l2_err("fail to get v4l2_format");
		return -EINVAL;
	}

	mtk_v4l2_debug(4, "[%d] type %d", ctx->id, f->type);

	q_data = mtk_vdec_get_q_data(ctx, f->type);
	if (!q_data)
		return -EINVAL;

	pix_mp = &f->fmt.pix_mp;
	if ((f->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) &&
		vb2_is_busy(&ctx->m2m_ctx->out_q_ctx.q)) {
		mtk_v4l2_err("out_q_ctx buffers already requested");
		ret = -EBUSY;
	}

	if ((f->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) &&
		vb2_is_busy(&ctx->m2m_ctx->cap_q_ctx.q)) {
		// can change back to mtk_v4l2_err after c2 flow fix
		mtk_v4l2_debug(1, "cap_q_ctx buffers already requested");
		ret = -EBUSY;
	}

	fmt = mtk_vdec_find_format(ctx, f,
		(f->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) ?
		MTK_FMT_DEC : MTK_FMT_FRAME);
	if (fmt == NULL) {
		if (f->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE
			&& default_out_fmt_idx < MTK_MAX_DEC_CODECS_SUPPORT) {
			f->fmt.pix.pixelformat =
				mtk_vdec_formats[default_out_fmt_idx].fourcc;
			fmt = mtk_vdec_find_format(ctx, f, MTK_FMT_DEC);
		} else if (f->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE
			&& default_cap_fmt_idx < MTK_MAX_DEC_CODECS_SUPPORT) {
			f->fmt.pix.pixelformat =
				mtk_vdec_formats[default_cap_fmt_idx].fourcc;
			fmt = mtk_vdec_find_format(ctx, f, MTK_FMT_FRAME);
		}
	}
	if (!fmt)
		return -EINVAL;

	q_data->fmt = fmt;
	vidioc_try_fmt(f, q_data->fmt, ctx);

	if (f->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		q_data->sizeimage[0] = pix_mp->plane_fmt[0].sizeimage;
		q_data->coded_width = pix_mp->width;
		q_data->coded_height = pix_mp->height;

		ctx->colorspace = f->fmt.pix_mp.colorspace;
		ctx->ycbcr_enc = f->fmt.pix_mp.ycbcr_enc;
		ctx->quantization = f->fmt.pix_mp.quantization;
		ctx->xfer_func = f->fmt.pix_mp.xfer_func;

		if (vdec_init_no_delay()) {
			ret = mtk_vcodec_dec_init(ctx, q_data);
			if (ret)
				return -EINVAL;
		}
	}

	if (f->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
		if (vdec_if_set_param(ctx, SET_PARAM_FB_NUM_PLANES, (void *) &q_data->fmt->num_planes))
			mtk_v4l2_err("[%d] Error!! Cannot set param SET_PARAM_FB_NUM_PLANES", ctx->id);

	return 0;
}

static int vidioc_enum_frameintervals(struct file *file, void *priv,
	struct v4l2_frmivalenum *fival)
{
	struct mtk_vcodec_ctx *ctx = fh_to_ctx(priv);

	if (fival->index != 0)
		return -EINVAL;

	fival->type = V4L2_FRMIVAL_TYPE_STEPWISE;
	mutex_lock(&ctx->dev->cap_mutex);
	mtk_vdec_frameintervals.fourcc = fival->pixel_format;
	mtk_vdec_frameintervals.width = fival->width;
	mtk_vdec_frameintervals.height = fival->height;

	if (vdec_if_get_param(ctx, GET_PARAM_VDEC_CAP_FRAMEINTERVALS,
		&mtk_vdec_frameintervals) != 0) {
		mtk_v4l2_err("[%d] Error!! Cannot get frame interval", ctx->id);
		mutex_unlock(&ctx->dev->cap_mutex);
		return -EINVAL;
	}

	fival->stepwise = mtk_vdec_frameintervals.stepwise;
	mutex_unlock(&ctx->dev->cap_mutex);
	mtk_v4l2_debug(1, "vdec frm_interval fourcc %s(0x%x) width %d height %d max %d/%d min %d/%d step %d/%d\n",
		FOURCC_STR(fival->pixel_format), fival->pixel_format,
		fival->width,
		fival->height,
		fival->stepwise.max.numerator,
		fival->stepwise.max.denominator,
		fival->stepwise.min.numerator,
		fival->stepwise.min.denominator,
		fival->stepwise.step.numerator,
		fival->stepwise.step.denominator);

	return 0;
}

static int vidioc_enum_framesizes(struct file *file, void *priv,
	struct v4l2_frmsizeenum *fsize)
{
	int i = 0;

	if (fsize->index != 0)
		return -EINVAL;

	for (i = 0; i < MTK_MAX_DEC_CODECS_SUPPORT &&
		 mtk_vdec_framesizes[i].fourcc != 0; i++) {
		if (fsize->pixel_format != mtk_vdec_framesizes[i].fourcc)
			continue;

		fsize->type = V4L2_FRMSIZE_TYPE_STEPWISE;
		fsize->reserved[0] = mtk_vdec_framesizes[i].profile;
		fsize->reserved[1] = mtk_vdec_framesizes[i].level;
		fsize->stepwise = mtk_vdec_framesizes[i].stepwise;

		mtk_v4l2_debug(1, "%d %d %d %d %d %d %d %d",
					   fsize->stepwise.min_width,
					   fsize->stepwise.max_width,
					   fsize->stepwise.step_width,
					   fsize->stepwise.min_height,
					   fsize->stepwise.max_height,
					   fsize->stepwise.step_height,
					   fsize->reserved[0],
					   fsize->reserved[1]);
		return 0;
	}

	return -EINVAL;
}

static int vidioc_enum_fmt(struct mtk_vcodec_ctx *ctx, struct v4l2_fmtdesc *f,
	bool output_queue)
{
	struct mtk_video_fmt *fmt;
	int i, j = 0;

	for (i = 0; i < MTK_MAX_DEC_CODECS_SUPPORT &&
		 mtk_vdec_formats[i].fourcc != 0; i++) {
		if ((output_queue == true) &&
			(mtk_vdec_formats[i].type != MTK_FMT_DEC))
			continue;
		else if ((output_queue == false) &&
				 (mtk_vdec_formats[i].type != MTK_FMT_FRAME))
			continue;

		if (j == f->index)
			break;
		++j;
	}

	if (i == MTK_MAX_DEC_CODECS_SUPPORT ||
		mtk_vdec_formats[i].fourcc == 0)
		return -EINVAL;

	fmt = &mtk_vdec_formats[i];

	f->pixelformat = fmt->fourcc;
	f->flags = 0;
	memset(f->reserved, 0, sizeof(f->reserved));

	if (mtk_vdec_formats[i].type != MTK_FMT_DEC)
		f->flags |= V4L2_FMT_FLAG_COMPRESSED;

	if (f->pixelformat == V4L2_PIX_FMT_MPEG1 ||
		f->pixelformat == V4L2_PIX_FMT_MPEG2 ||
		f->pixelformat == V4L2_PIX_FMT_MPEG4 ||
		f->pixelformat == V4L2_PIX_FMT_VP8 ||
		f->pixelformat == V4L2_PIX_FMT_H264 ||
		f->pixelformat == V4L2_PIX_FMT_HEVC ||
		f->pixelformat == V4L2_PIX_FMT_VP9 ||
		f->pixelformat == V4L2_PIX_FMT_AV1)
		f->flags |= V4L2_FMT_FLAG_DYN_RESOLUTION;

	v4l_fill_mtk_fmtdesc(f);

	return 0;
}

static int vidioc_vdec_enum_fmt_vid_cap_mplane(struct file *file, void *priv,
	struct v4l2_fmtdesc *f)
{
	struct mtk_vcodec_ctx *ctx = fh_to_ctx(priv);

	return vidioc_enum_fmt(ctx, f, false);
}

static int vidioc_vdec_enum_fmt_vid_out_mplane(struct file *file, void *priv,
	struct v4l2_fmtdesc *f)
{
	struct mtk_vcodec_ctx *ctx = fh_to_ctx(priv);

	return vidioc_enum_fmt(ctx, f, true);
}

static int vidioc_vdec_g_fmt(struct file *file, void *priv,
							 struct v4l2_format *f)
{
	struct mtk_vcodec_ctx *ctx = fh_to_ctx(priv);
	struct v4l2_pix_format_mplane *pix_mp = &f->fmt.pix_mp;
	struct vb2_queue *vq;
	struct mtk_q_data *q_data;
	u32     fourcc;
	unsigned int i = 0;

	if (IS_ERR_OR_NULL(f)) {
		mtk_v4l2_err("fail to get v4l2_format");
		return -EINVAL;
	}

	vq = v4l2_m2m_get_vq(ctx->m2m_ctx, f->type);
	if (!vq) {
		mtk_v4l2_err("no vb2 queue for type=%d", f->type);
		return -EINVAL;
	}

	q_data = mtk_vdec_get_q_data(ctx, f->type);

	pix_mp->field = V4L2_FIELD_NONE;
	pix_mp->colorspace = ctx->colorspace;
	pix_mp->ycbcr_enc = ctx->ycbcr_enc;
	pix_mp->quantization = ctx->quantization;
	pix_mp->xfer_func = ctx->xfer_func;

	if ((f->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) &&
		mtk_vcodec_state_in_range(ctx, MTK_STATE_HEADER, MTK_STATE_NULL)) { // >= HEADER
		/* Until STREAMOFF is called on the CAPTURE queue
		 * (acknowledging the event), the driver operates as if
		 * the resolution hasn't changed yet.
		 * So we just return picinfo yet, and update picinfo in
		 * stop_streaming hook function
		 */
		ctx->waiting_fmt = false;
		for (i = 0; i < q_data->fmt->num_planes; i++) {
			q_data->sizeimage[i] = ctx->picinfo.fb_sz[i];
			q_data->bytesperline[i] =
				ctx->last_decoded_picinfo.buf_w;
		}
		q_data->coded_width = ctx->picinfo.buf_w;
		q_data->coded_height = ctx->picinfo.buf_h;
		q_data->field = ctx->picinfo.field;
		fourcc = ctx->picinfo.fourcc;
		q_data->fmt = mtk_find_fmt_by_pixel(fourcc);

		/*
		 * Width and height are set to the dimensions
		 * of the movie, the buffer is bigger and
		 * further processing stages should crop to this
		 * rectangle.
		 */
		fourcc = ctx->q_data[MTK_Q_DATA_SRC].fmt->fourcc;
		pix_mp->width = q_data->coded_width;
		pix_mp->height = q_data->coded_height;
		/*
		 * Set pixelformat to the format in which mt vcodec
		 * outputs the decoded frame
		 */
		pix_mp->num_planes = q_data->fmt->num_planes;
		pix_mp->pixelformat = q_data->fmt->fourcc;
		pix_mp->field = q_data->field;

		for (i = 0; i < pix_mp->num_planes; i++) {
			pix_mp->plane_fmt[i].bytesperline =
				q_data->bytesperline[i];
			pix_mp->plane_fmt[i].sizeimage =
				q_data->sizeimage[i];
		}

		mtk_v4l2_debug(1, "fourcc:(%s(0x%x) %s(0x%x)),field:%d,bytesperline:%d,sizeimage:%d,%d,%d\n",
			FOURCC_STR(ctx->q_data[MTK_Q_DATA_SRC].fmt->fourcc), ctx->q_data[MTK_Q_DATA_SRC].fmt->fourcc,
			FOURCC_STR(q_data->fmt->fourcc), q_data->fmt->fourcc,
			pix_mp->field,
			pix_mp->plane_fmt[0].bytesperline,
			pix_mp->plane_fmt[0].sizeimage,
			pix_mp->plane_fmt[1].bytesperline,
			pix_mp->plane_fmt[1].sizeimage);

	} else if (f->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		/*
		 * This is run on OUTPUT
		 * The buffer contains compressed image
		 * so width and height have no meaning.
		 * Assign value here to pass v4l2-compliance test
		 */
		pix_mp->width = q_data->visible_width;
		pix_mp->height = q_data->visible_height;
		pix_mp->plane_fmt[0].bytesperline = q_data->bytesperline[0];
		pix_mp->plane_fmt[0].sizeimage = q_data->sizeimage[0];
		pix_mp->pixelformat = q_data->fmt->fourcc;
		pix_mp->num_planes = q_data->fmt->num_planes;
	} else {
		pix_mp->num_planes = q_data->fmt->num_planes;
		pix_mp->pixelformat = q_data->fmt->fourcc;
		fourcc = ctx->q_data[MTK_Q_DATA_SRC].fmt->fourcc;

		pix_mp->width = q_data->coded_width;
		pix_mp->height = q_data->coded_height;
		for (i = 0; i < pix_mp->num_planes; i++) {
			pix_mp->plane_fmt[i].bytesperline =
				q_data->bytesperline[i];
			pix_mp->plane_fmt[i].sizeimage =
				q_data->sizeimage[i];
		}

		mtk_v4l2_debug(1, " [%d] type=%d state=%d Format information could not be read, not ready yet!",
			ctx->id, f->type, mtk_vcodec_get_state(ctx));
	}

	return 0;
}

static int vb2ops_vdec_queue_setup(struct vb2_queue *vq,
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
	q_data = mtk_vdec_get_q_data(ctx, vq->type);
	if (q_data == NULL || (*nplanes) > MTK_VCODEC_MAX_PLANES ||
	    (vq->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE && q_data->fmt == NULL)) {
		mtk_v4l2_err("vq->type=%d nplanes %d err", vq->type, *nplanes);
		return -EINVAL;
	}

	if (*nplanes) {
		for (i = 0; i < *nplanes; i++) {
			if (sizes[i] < q_data->sizeimage[i] || sizes[i] > q_data->sizeimage[i] * 2)
				return -EINVAL;
		}
	} else {
		if (vq->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
			*nplanes = q_data->fmt->num_planes;
		else
			*nplanes = 1;

		for (i = 0; i < *nplanes; i++)
			sizes[i] = q_data->sizeimage[i];
	}

	mtk_v4l2_debug(1, "[%d] type = %d, get %d plane(s), %d buffer(s) of size 0x%x 0x%x (sizeimage 0x%x 0x%x)",
		ctx->id, vq->type, *nplanes, *nbuffers, sizes[0], sizes[1], q_data->sizeimage[0], q_data->sizeimage[1]);

#if (!(IS_ENABLED(CONFIG_DEVICE_MODULES_ARM_SMMU_V3)))
	if (ctx->dec_params.svp_mode && is_disable_map_sec() && mtk_vdec_is_vcu()) {
		vq->mem_ops = &vdec_sec_dma_contig_memops;
		mtk_v4l2_debug(1, "[%d] hook vdec_sec_dma_contig_memops for queue type %d",
			ctx->id, vq->type);
	}
#endif

	return 0;
}

static int vb2ops_vdec_buf_prepare(struct vb2_buffer *vb)
{
	struct mtk_vcodec_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	struct mtk_q_data *q_data;
	unsigned int plane = 0;
	unsigned int i;
	struct mtk_video_dec_buf *mtkbuf;
	struct vb2_v4l2_buffer *vb2_v4l2;
#if ENABLE_META_BUF
	void *general_buf_va = NULL;
	int *pFenceFd;
	int metaBufferfd = -1;
#endif

	vcodec_trace_begin("%s(%s)", __func__,
		V4L2_TYPE_IS_CAPTURE(vb->vb2_queue->type) ? "out" : "in");

	mtk_v4l2_debug(4, "[%d] (%d) id=%d",
				   ctx->id, vb->vb2_queue->type, vb->index);

	q_data = mtk_vdec_get_q_data(ctx, vb->vb2_queue->type);

	for (i = 0; i < q_data->fmt->num_planes; i++) {
		if (vb2_plane_size(vb, i) < q_data->sizeimage[i]) {
			mtk_v4l2_err("data will not fit into plane %d (%lu < %d)",
						 i, vb2_plane_size(vb, i),
						 q_data->sizeimage[i]);
		}
	}

	// Check if need to proceed cache operations
	vb2_v4l2 = container_of(vb, struct vb2_v4l2_buffer, vb2_buf);
	mtkbuf = container_of(vb2_v4l2, struct mtk_video_dec_buf, vb);
	if (vb->vb2_queue->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
		set_general_buffer(ctx, &mtkbuf->frame_buffer, mtkbuf->general_user_fd);
		mtk_v4l2_debug(4, "general_buf fd=%d, dma_buf=%p, DMA=%pad",
			mtkbuf->general_user_fd,
			mtkbuf->frame_buffer.dma_general_buf,
			&mtkbuf->frame_buffer.dma_general_addr);

#if ENABLE_META_BUF
		mutex_lock(&ctx->meta_buf_lock);
		if (mtkbuf->meta_user_fd > 0) {
			mtkbuf->frame_buffer.dma_meta_buf =
				dma_buf_get(mtkbuf->meta_user_fd);
			if (IS_ERR_OR_NULL(mtkbuf->frame_buffer.dma_meta_buf))
				mtkbuf->frame_buffer.dma_meta_buf = NULL;
			else
				mtkbuf->frame_buffer.dma_meta_addr =
					get_meta_buffer_dma_addr(ctx, mtkbuf->meta_user_fd);
		} else
			mtkbuf->frame_buffer.dma_meta_buf = 0;

		mtkbuf->general_dma_va = NULL;
		if (mtkbuf->frame_buffer.dma_general_buf != 0) {
			// NOTE: all codec should use a common struct
			//to save dma_va, fencefd should be
			//the 1st member in struct of general buffer
			general_buf_va = get_general_buffer_va(ctx,
				mtkbuf->frame_buffer.dma_general_buf);
			pFenceFd = (int *)general_buf_va;
			if (general_buf_va != NULL && *pFenceFd == 1 &&
				ctx->dec_params.svp_mode == 0) {
				ctx->use_fence = true;
				mtkbuf->general_dma_va = general_buf_va;
			} else if (general_buf_va != NULL)
				*pFenceFd = -1;
		}
		mtkbuf->meta_user_fd = -1; //default value is -1
		if (general_buf_va != NULL) {
			// metaBufferfd should be
			//the 2st member in struct of general buffer
			metaBufferfd = *((int *)general_buf_va + 1);
			if (metaBufferfd > 0)
				mtkbuf->meta_user_fd = metaBufferfd;
			mtk_v4l2_debug(1, "[%d] id=%d FB general_buf_va=%p, metaBufferfd=%d, meta_user_fd = %d",
				ctx->id, vb->index, general_buf_va,
				metaBufferfd, mtkbuf->meta_user_fd);
		}
		mtk_v4l2_debug(4,
			"meta_buf fd=%d, dma_buf=%p, DMA=%pad",
			mtkbuf->meta_user_fd,
			mtkbuf->frame_buffer.dma_meta_buf,
			&mtkbuf->frame_buffer.dma_meta_addr);
		mutex_unlock(&ctx->meta_buf_lock);
#endif
	}
#if IS_ENABLED(CONFIG_MTK_TINYSYS_VCP_SUPPORT)
	if (vb->vb2_queue->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		struct device *io_dev = vcp_get_io_device_ex(VCP_IOMMU_VDEC);

		if (ctx->dev->support_acp && mtk_vdec_acp_enable && mtk_vdec_acp_debug && io_dev != NULL) {
			mtk_vcodec_dma_attach_map(io_dev, vb->planes[0].dbuf,
				&mtkbuf->non_acp_attach, &mtkbuf->non_acp_sgt, &mtkbuf->bs_buffer.non_acp_iova,
				DMA_BIDIRECTIONAL, __func__, __LINE__);
		} else {
			mtkbuf->non_acp_attach = NULL;
			mtkbuf->non_acp_sgt = NULL;
			mtkbuf->bs_buffer.non_acp_iova = 0;
		}
	}
#endif
	if (vb->vb2_queue->memory == VB2_MEMORY_DMABUF &&
		!(mtkbuf->flags & NO_CAHCE_CLEAN) &&
		!(ctx->dec_params.svp_mode)) {
		if (vb->vb2_queue->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
			struct mtk_vcodec_mem src_mem;
			struct vb2_dc_buf *dc_buf = vb->planes[0].mem_priv;
			mtk_v4l2_debug(4, "[%d] Cache sync+", ctx->id);

			mtk_dma_sync_sg_range(dc_buf->dma_sgt, vb->vb2_queue->dev,
				vb->planes[0].bytesused, DMA_TO_DEVICE);

			src_mem.dma_addr = vb2_dma_contig_plane_dma_addr(vb, 0);
			src_mem.size = (size_t)vb->planes[0].bytesused;

			mtk_v4l2_debug(4, "[%d] Cache sync- TD for %pad sz=%d dev %p", ctx->id,
				&src_mem.dma_addr, (unsigned int)src_mem.size, vb->vb2_queue->dev);
		} else {
			for (plane = 0; plane < vb->num_planes; plane++) {
				struct vdec_fb dst_mem;
				struct vb2_dc_buf *dc_buf = vb->planes[plane].mem_priv;
				mtk_v4l2_debug(4, "[%d] Cache sync+", ctx->id);

				dma_sync_sg_for_device(
					vb->vb2_queue->dev,
					dc_buf->dma_sgt->sgl,
					dc_buf->dma_sgt->orig_nents,
					DMA_TO_DEVICE);
				dst_mem.fb_base[plane].dma_addr =
					vb2_dma_contig_plane_dma_addr(vb,
					plane);
				dst_mem.fb_base[plane].size =
					ctx->picinfo.fb_sz[plane];
				mtk_v4l2_debug(4, "[%d] Cache sync- TD for %pad sz=%d dev %p",
					ctx->id,
					&dst_mem.fb_base[plane].dma_addr,
					(unsigned int)dst_mem.fb_base[plane].size,
					vb->vb2_queue->dev);
			}
		}
	}

	vcodec_trace_end();
	return 0;
}

static void vb2ops_vdec_buf_queue(struct vb2_buffer *vb)
{
	struct vb2_buffer *src_buf;
	struct vb2_v4l2_buffer *src_vb2_v4l2;
	struct mtk_vcodec_mem *src_mem;
	unsigned int src_chg = 0;
	bool res_chg = false;
	bool mtk_vcodec_unsupport = false;
	bool need_seq_header = false;
	bool need_log = false;
	int ret = 0;
	unsigned int i = 0;
	unsigned int dpbsize = 1;
	unsigned int bs_fourcc, fm_fourcc;
	struct mtk_vcodec_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	struct vb2_v4l2_buffer *vb2_v4l2 = NULL;
	struct mtk_video_dec_buf *buf = NULL;
	struct mtk_q_data *dst_q_data;
	u32 fourcc;
	int last_frame_type = 0;
	struct mtk_color_desc color_desc;
	struct vb2_queue *dst_vq;
	dma_addr_t new_dma_addr;
	bool new_dma = false;
	char debug_bs[50] = "";
#ifdef VDEC_CHECK_ALIVE
	struct vdec_check_alive_work_struct *retrigger_ctx_work;
#endif

	vcodec_trace_begin("%s(%s)", __func__,
		vb->vb2_queue->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE ? "out" : "in");

	mtk_v4l2_debug(4, "[%d] (%d) id=%d, vb=%p",
				   ctx->id, vb->vb2_queue->type,
				   vb->index, vb);

	ret = mtk_vcodec_dec_init(ctx, mtk_vdec_get_q_data(ctx, vb->vb2_queue->type));
	if (ret) {
		vcodec_trace_end();
		return;
	}

#ifdef VDEC_CHECK_ALIVE
	/* ctx resume, queue work for check alive timer */
	if (mtk_vdec_dvfs_params_change(ctx) ||
		mtk_vdec_dvfs_monitor_op_rate(ctx, vb->vb2_queue->type)) {
		retrigger_ctx_work = kzalloc(sizeof(*retrigger_ctx_work), GFP_KERNEL);
		INIT_WORK(&retrigger_ctx_work->work, mtk_vdec_check_alive_work);
		retrigger_ctx_work->ctx = ctx;
		retrigger_ctx_work->dev = ctx->dev;
		queue_work(ctx->dev->check_alive_workqueue, &retrigger_ctx_work->work);
		mtk_v4l2_debug(4, "%s [VDVFS] retrigger ctx work: %d", __func__, ctx->id);
		ctx->is_active = 1;
	}
#endif
	/*
	 * check if this buffer is ready to be used after decode
	 */
	vb2_v4l2 = to_vb2_v4l2_buffer(vb);
	buf = container_of(vb2_v4l2, struct mtk_video_dec_buf, vb);
	if (vb->vb2_queue->type != V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		mutex_lock(&ctx->buf_lock);
		if (buf->used == false) {
			v4l2_m2m_buf_queue_check(ctx->m2m_ctx, vb2_v4l2);
			buf->queued_in_vb2 = true;
			buf->queued_in_v4l2 = true;
			buf->ready_to_display = false;
		} else {
			buf->queued_in_vb2 = false;
			buf->queued_in_v4l2 = true;
			buf->ready_to_display = false;
		}
		if (ctx->output_async) {
			buf->flags &= ~CROP_CHANGED;
			buf->flags &= ~COLOR_ASPECT_CHANGED;
			buf->flags &= ~REF_FREED;
		}

		for (i = 0; i < vb->num_planes; i++) {
			new_dma_addr =
				vb2_dma_contig_plane_dma_addr(vb, i);
			// real buffer changed in this slot
			if (buf->frame_buffer.fb_base[i].dmabuf != vb->planes[i].dbuf) {
				new_dma = true;
				mtk_v4l2_debug(1, "[%d] id=%d get new buffer: old dma_addr[%d] = %lx %p, new dma_addr[%d] = %lx %p",
					ctx->id, vb->index, i,
					(unsigned long)buf->frame_buffer.fb_base[i].dma_addr,
					buf->frame_buffer.fb_base[i].dmabuf,
					i, (unsigned long)new_dma_addr, vb->planes[i].dbuf);
			}
		}

		if (mtk_vdec_slc_enable && ctx->dev->dec_slc_ver == VDEC_SLC_V1 &&
			mtk_vcodec_is_state(ctx, MTK_STATE_HEADER) &&
			vb->planes[0].dbuf) {
			mtk_vdec_slc_get_gid_from_dma(ctx, vb->planes[0].dbuf);
		}

		// only allow legacy buffers in this slot still referenced put to driver
		if (ctx->input_driven == INPUT_DRIVEN_PUT_FRM &&
			!new_dma && buf->used == true) {
			buf->queued_in_vb2 = true;
			v4l2_m2m_buf_queue_check(ctx->m2m_ctx, vb2_v4l2);
		}
		mutex_unlock(&ctx->buf_lock);

		if (ctx->input_driven == INPUT_DRIVEN_CB_FRM)
			wake_up(&ctx->fm_wq);

		mtk_vdec_set_frame(ctx, buf);

		mtk_vdec_lpw_check_dec_start(ctx, true, false, "qbuf dst");

		vcodec_trace_end();
		return;
	}

	buf->used = false;
	v4l2_m2m_buf_queue_check(ctx->m2m_ctx, to_vb2_v4l2_buffer(vb));

	mtk_vdec_lpw_check_dec_start(ctx, true, (buf->lastframe != NON_EOS), "qbuf src");

	if (!mtk_vcodec_is_state(ctx, MTK_STATE_INIT)) {
		mtk_v4l2_debug(4, "[%d] already init driver %d",
			ctx->id, mtk_vcodec_get_state(ctx));
		goto err_buf_prepare;
	}

	src_vb2_v4l2 = v4l2_m2m_next_src_buf(ctx->m2m_ctx);

	if (!src_vb2_v4l2 ||
		src_vb2_v4l2 == &ctx->dec_flush_buf->vb) {
		mtk_v4l2_err("No src buffer 0x%p", src_vb2_v4l2);
		goto err_buf_prepare;
	}
	src_buf = &src_vb2_v4l2->vb2_buf;

	vb2_v4l2 = to_vb2_v4l2_buffer(vb);
	buf = container_of(vb2_v4l2, struct mtk_video_dec_buf, vb);
	src_mem = &buf->bs_buffer;
	if (mtk_v4l2_dbg_level > 0)
		src_mem->va = vb2_plane_vaddr(src_buf, 0);
	src_mem->dma_addr = vb2_dma_contig_plane_dma_addr(src_buf, 0);
	src_mem->size = (size_t)src_buf->planes[0].bytesused;
	src_mem->length = (size_t)src_buf->planes[0].length;
	src_mem->data_offset = (size_t)src_buf->planes[0].data_offset;
	src_mem->dmabuf = src_buf->planes[0].dbuf;
	src_mem->flags = vb2_v4l2->flags;
	src_mem->index = vb->index;
	src_mem->hdr10plus_buf = &buf->hdr10plus_buf;
	ctx->timestamp = src_vb2_v4l2->vb2_buf.timestamp;
	ctx->bs_list[src_mem->index + 1] = (uintptr_t)src_mem;

	mtk_v4l2_debug(2,
		"[%d] buf id=%d va=%p DMA=%lx size=%zx length=%zu dmabuf=%p pts %llu",
		ctx->id, src_buf->index,
		src_mem->va, (unsigned long)src_mem->dma_addr,
		src_mem->size, src_mem->length,
		src_mem->dmabuf, ctx->timestamp);

	if (src_mem->va != NULL) {
		SPRINTF(debug_bs, "%02x %02x %02x %02x %02x %02x %02x %02x %02x",
		  ((char *)src_mem->va)[0], ((char *)src_mem->va)[1], ((char *)src_mem->va)[2],
		  ((char *)src_mem->va)[3], ((char *)src_mem->va)[4], ((char *)src_mem->va)[5],
		  ((char *)src_mem->va)[6], ((char *)src_mem->va)[7], ((char *)src_mem->va)[8]);
	}

	ret = vdec_if_decode(ctx, src_mem, NULL, &src_chg);
	mtk_vdec_set_param(ctx);

	/* src_chg bit0 for res change flag, bit1 for realloc mv buf flag,
	 * bit2 for not support flag, other bits are reserved
	 */
	res_chg = ((src_chg & VDEC_RES_CHANGE) != 0U) ? true : false;
	mtk_vcodec_unsupport = ((src_chg & VDEC_HW_NOT_SUPPORT) != 0) ?
						   true : false;
	need_seq_header = ((src_chg & VDEC_NEED_SEQ_HEADER) != 0U) ?
					  true : false;
	if (ret || !res_chg || mtk_vcodec_unsupport
		|| need_seq_header) {
		/*
		 * fb == NULL menas to parse SPS/PPS header or
		 * resolution info in src_mem. Decode can fail
		 * if there is no SPS header or picture info
		 * in bs
		 */
		vb2_v4l2 = to_vb2_v4l2_buffer(vb);
		buf = container_of(vb2_v4l2, struct mtk_video_dec_buf, vb);
		last_frame_type = buf->lastframe;

		if (need_seq_header)
			vb2_v4l2->flags |= V4L2_BUF_FLAG_OUTPUT_NOT_GENERATED;

		src_vb2_v4l2 = v4l2_m2m_src_buf_remove(ctx->m2m_ctx);
		if (!src_vb2_v4l2) {
			mtk_v4l2_err("[%d]Error!!src_buf is NULL!", ctx->id);
			goto err_buf_prepare;
		}
		src_buf = &src_vb2_v4l2->vb2_buf;
		clean_free_bs_buffer(ctx, NULL);

		need_log = ret || mtk_vcodec_unsupport || (need_seq_header && ctx->init_cnt < 5);
		mtk_v4l2_debug((need_log ? 0 : 1),
			"[%d] vdec_if_decode() src_buf=%d, size=%zu, fail=%d, res_chg=%d, mtk_vcodec_unsupport=%d, need_seq_header=%d, init_cnt=%d, BS %s",
			ctx->id, src_buf->index,
			src_mem->size, ret, res_chg,
			mtk_vcodec_unsupport, need_seq_header, ctx->init_cnt, debug_bs);

		/* If not support the source, eg: w/h,
		 * bitdepth, level, we need to stop to play it
		 */
		if (need_seq_header) {
			mtk_v4l2_debug(3, "[%d]Error!! Need seq header! (cnt %d)",
						 ctx->id, ctx->init_cnt);
			mtk_vdec_queue_noseqheader_event(ctx);
		} else if (mtk_vcodec_unsupport || last_frame_type != NON_EOS) {
			mtk_v4l2_err("[%d]Error!! Codec driver not support the file!",
						 ctx->id);
			mtk_vdec_set_unsupport(ctx);
		} else if (ret == -EIO) {
			/* ipi timeout / VPUD crashed ctx abort */
			mtk_vdec_error_handle(ctx, "dec init");
		}
		ctx->init_cnt++;
		goto err_buf_prepare;
	}

	if (res_chg) {
		mtk_v4l2_debug(3, "[%d] vdec_if_decode() res_chg: %d\n",
					   ctx->id, res_chg);
		clean_free_bs_buffer(ctx, src_mem);
		mtk_vdec_queue_res_chg_event(ctx);

		/* remove all framebuffer.
		 * framebuffer with old byteused cannot use.
		 */
		while (v4l2_m2m_dst_buf_remove(ctx->m2m_ctx) != NULL)
			mtk_v4l2_debug(3, "[%d] v4l2_m2m_dst_buf_remove()", ctx->id);
	}

	ret = vdec_if_get_param(ctx, GET_PARAM_PIC_INFO,
		&ctx->last_decoded_picinfo);
	if (ret) {
		mtk_v4l2_err("[%d]Error!! Cannot get param : GET_PARAM_PICTURE_INFO ERR",
					 ctx->id);
		goto err_buf_prepare;
	}

	dst_q_data = &ctx->q_data[MTK_Q_DATA_DST];
	fourcc = ctx->last_decoded_picinfo.fourcc;
	dst_q_data->fmt = mtk_find_fmt_by_pixel(fourcc);

	for (i = 0; i < dst_q_data->fmt->num_planes; i++) {
		dst_q_data->sizeimage[i] = ctx->last_decoded_picinfo.fb_sz[i];
		dst_q_data->bytesperline[i] = ctx->last_decoded_picinfo.buf_w;
	}

	bs_fourcc = ctx->q_data[MTK_Q_DATA_SRC].fmt->fourcc;
	fm_fourcc = ctx->q_data[MTK_Q_DATA_DST].fmt->fourcc;
	mtk_v4l2_debug(0, "[%d] Init Vdec OK bs %s fm %s, wxh=%dx%d pic wxh=%dx%d bitdepth:%d lo:%d sz[0]=0x%x sz[1]=0x%x num_planes %d, fb_sz[0] %d, fb_sz[1] %d, BS %s",
		ctx->id, FOURCC_STR(bs_fourcc), FOURCC_STR(fm_fourcc),
		ctx->last_decoded_picinfo.buf_w,
		ctx->last_decoded_picinfo.buf_h,
		ctx->last_decoded_picinfo.pic_w,
		ctx->last_decoded_picinfo.pic_h,
		ctx->last_decoded_picinfo.bitdepth,
		ctx->last_decoded_picinfo.layout_mode,
		dst_q_data->sizeimage[0], dst_q_data->sizeimage[1], dst_q_data->fmt->num_planes,
		ctx->last_decoded_picinfo.fb_sz[0],
		ctx->last_decoded_picinfo.fb_sz[1], debug_bs);

	ret = vdec_if_get_param(ctx, GET_PARAM_DPB_SIZE, &dpbsize);
	if (dpbsize == 0)
		mtk_v4l2_err("[%d] GET_PARAM_DPB_SIZE fail=%d", ctx->id, ret);

	ctx->last_dpb_size = dpbsize;

	ret = vdec_if_get_param(ctx, GET_PARAM_COLOR_DESC, &color_desc);
	if (ret == 0) {
		ctx->last_is_hdr = color_desc.hdr_type;
	} else {
		mtk_v4l2_err("[%d] GET_PARAM_COLOR_DESC fail=%d",
		ctx->id, ret);
	}

	dst_vq = v4l2_m2m_get_vq(ctx->m2m_ctx,
		V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);

	if (!vb2_is_streaming(dst_vq)) {
		ctx->picinfo = ctx->last_decoded_picinfo;
		ctx->dpb_size = dpbsize;
		ctx->is_hdr = color_desc.hdr_type;
	}

	mtk_vcodec_set_state_from(ctx, MTK_STATE_HEADER, MTK_STATE_INIT);
	mtk_v4l2_debug(1, "[%d] dpbsize=%d", ctx->id, ctx->last_dpb_size);

err_buf_prepare:
	vcodec_trace_end();
}

static void vb2ops_vdec_buf_finish(struct vb2_buffer *vb)
{
	struct mtk_vcodec_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	struct vb2_v4l2_buffer *vb2_v4l2;
	struct mtk_video_dec_buf *buf;
	unsigned int plane = 0;
	struct mtk_video_dec_buf *mtkbuf;

	vcodec_trace_begin("%s(%s)", __func__,
		vb->vb2_queue->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE ? "out" : "in");

#if IS_ENABLED(CONFIG_MTK_TINYSYS_VCP_SUPPORT)
	if (vb->vb2_queue->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		vb2_v4l2 = container_of(vb, struct vb2_v4l2_buffer, vb2_buf);
		mtkbuf = container_of(vb2_v4l2, struct mtk_video_dec_buf, vb);
		mtk_vcodec_dma_unmap_detach(
			vb->planes[0].dbuf, &mtkbuf->non_acp_attach, &mtkbuf->non_acp_sgt, DMA_BIDIRECTIONAL);
	}
#endif

	if (vb->vb2_queue->type != V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
		vcodec_trace_end();
		return;
	}

	vb2_v4l2 = container_of(vb, struct vb2_v4l2_buffer, vb2_buf);
	buf = container_of(vb2_v4l2, struct mtk_video_dec_buf, vb);
	mutex_lock(&ctx->buf_lock);
	buf->queued_in_v4l2 = false;
	buf->queued_in_vb2 = false;
	mutex_unlock(&ctx->buf_lock);

	// Check if need to proceed cache operations for Capture Queue
	vb2_v4l2 = container_of(vb, struct vb2_v4l2_buffer, vb2_buf);
	mtkbuf = container_of(vb2_v4l2, struct mtk_video_dec_buf, vb);

	if (!IS_ERR_OR_NULL(mtkbuf->frame_buffer.dma_general_buf)) {
		release_general_buffer_info_by_dmabuf(ctx, mtkbuf->frame_buffer.dma_general_buf);
		mtkbuf->frame_buffer.dma_general_buf = NULL;
		mtk_v4l2_debug(4, "dma_buf_put general_buf fd=%d, dma_buf=%p, DMA=%pad",
			mtkbuf->general_user_fd,
			mtkbuf->frame_buffer.dma_general_buf,
			&mtkbuf->frame_buffer.dma_general_addr);
	}
	if (!IS_ERR_OR_NULL(mtkbuf->frame_buffer.dma_meta_buf)) {
		dma_buf_put(mtkbuf->frame_buffer.dma_meta_buf);
		mtkbuf->frame_buffer.dma_meta_buf = NULL;
		mtk_v4l2_debug(4, "dma_buf_put meta_buf fd=%d, dma_buf=%p, DMA=%pad",
			mtkbuf->meta_user_fd,
			mtkbuf->frame_buffer.dma_meta_buf,
			&mtkbuf->frame_buffer.dma_meta_addr);
	}

	if (vb->vb2_queue->memory == VB2_MEMORY_DMABUF &&
		!(mtkbuf->flags & NO_CAHCE_INVALIDATE) &&
		!(ctx->dec_params.svp_mode)) {
		for (plane = 0; plane < buf->frame_buffer.num_planes; plane++) {
			struct vdec_fb dst_mem;
			struct vb2_dc_buf *dc_buf = vb->planes[plane].mem_priv;

			mtk_v4l2_debug(4, "[%d] Cache sync+", ctx->id);

			dma_sync_sg_for_cpu(vb->vb2_queue->dev, dc_buf->dma_sgt->sgl,
				dc_buf->dma_sgt->orig_nents, DMA_FROM_DEVICE);
			dst_mem.fb_base[plane].dma_addr =
				vb2_dma_contig_plane_dma_addr(vb, plane);
			dst_mem.fb_base[plane].size = ctx->picinfo.fb_sz[plane];

			mtk_v4l2_debug(4, "[%d] Cache sync- FD for %pad sz=%d dev %p pfb %p",
				ctx->id,
				&dst_mem.fb_base[plane].dma_addr,
				(unsigned int)dst_mem.fb_base[plane].size,
				vb->vb2_queue->dev,
				&buf->frame_buffer);
		}
	}
	vcodec_trace_end();
}

static void vb2ops_vdec_buf_cleanup(struct vb2_buffer *vb)
{
	int i;
	struct mtk_vcodec_ctx *ctx;
	struct vb2_v4l2_buffer *vb2_v4l2 = container_of(vb,
		struct vb2_v4l2_buffer, vb2_buf);
	struct mtk_video_dec_buf *buf = container_of(vb2_v4l2,
		struct mtk_video_dec_buf, vb);

	ctx = vb2_get_drv_priv(vb->vb2_queue);
	if (vb->vb2_queue->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE &&
		!vb2_is_streaming(vb->vb2_queue)) {
		mutex_lock(&ctx->buf_lock);
		if (buf->used == true) {
			for (i = 0; i < buf->frame_buffer.num_planes; i++) {
				if (buf->frame_buffer.fb_base[i].dmabuf)
					dma_buf_put(buf->frame_buffer.fb_base[i].dmabuf);
				mtk_v4l2_debug(4, "[Ref cnt] id=%d Ref put dma %p",
				    buf->frame_buffer.index, buf->frame_buffer.fb_base[i].dmabuf);
			}
			buf->used = false;
		}
		mutex_unlock(&ctx->buf_lock);
	}
}

static int vb2ops_vdec_buf_init(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vb2_v4l2 = container_of(vb,
		struct vb2_v4l2_buffer, vb2_buf);
	struct mtk_video_dec_buf *buf = container_of(vb2_v4l2,
		struct mtk_video_dec_buf, vb);

	if (vb->vb2_queue->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
		mtk_v4l2_debug(4, "[%d] pfb=%p used %d ready_to_display %d queued_in_v4l2 %d",
			vb->vb2_queue->type, &buf->frame_buffer,
			buf->used, buf->ready_to_display, buf->queued_in_v4l2);
		/* User could use different struct dma_buf*
		 * with the same index & enter this buf_init.
		 * once this buffer buf->used == true will reset in mistake
		 * VB2 use kzalloc for struct mtk_video_dec_buf,
		 * so init could be no need
		 */
		if (buf->used == false) {
			buf->ready_to_display = false;
			buf->queued_in_v4l2 = false;
		}
	} else if (vb->vb2_queue->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		/* Do not reset EOS for 1st buffer with Early EOS*/
		/* buf->lastframe = NON_EOS; */
	} else {
		mtk_v4l2_err("vb2ops_vdec_buf_init: unknown queue type");
		return -EINVAL;
	}

	return 0;
}

static int vb2ops_vdec_start_streaming(struct vb2_queue *q, unsigned int count)
{
	struct mtk_vcodec_ctx *ctx = vb2_get_drv_priv(q);
	unsigned long total_frame_bufq_count;
	unsigned long vcp_dvfs_data[1] = {0};
	unsigned long flags;
	int origin_state, ret;

	vcodec_trace_begin("%s(%s)", __func__,
		q->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE ? "out" : "in");

	origin_state = mtk_vcodec_set_state_from(ctx, MTK_STATE_HEADER, MTK_STATE_FLUSH);
	mtk_v4l2_debug(4, "[%d] (%d) state=(%x)", ctx->id, q->type, origin_state);

	if (q->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
		if (ctx->ipi_blocked != NULL)
			*(ctx->ipi_blocked) = false;
		spin_lock_irqsave(&ctx->lpw_lock, flags);
		mtk_vdec_lpw_stop_timer(ctx, false);
		ctx->lpw_state = VDEC_LPW_DEC;
		ctx->lpw_dec_start_cnt = mtk_vdec_get_lpw_start_limit(ctx);
		ctx->in_group = true;
		ctx->dynamic_low_latency = false;
		ctx->prev_no_input = false;
		ctx->lpw_last_disp_ts = 0;
		spin_unlock_irqrestore(&ctx->lpw_lock, flags);

		// SET_PARAM_TOTAL_FRAME_BUFQ_COUNT for SW DEC(VDEC_DRV_DECODER_MTK_SOFTWARE=1)
		if (!mtk_vcodec_is_vcp(MTK_INST_DECODER)) {
			total_frame_bufq_count = q->num_buffers;
			if (vdec_if_set_param(ctx,
				SET_PARAM_TOTAL_FRAME_BUFQ_COUNT,
				&total_frame_bufq_count)) {
				mtk_v4l2_err("[%d] Error!! Cannot set param", ctx->id);
			}
		}

		vcodec_trace_begin("dvfs(stream_on)");
		mutex_lock(&ctx->dev->dec_dvfs_mutex);
		if (ctx->dev->vdec_dvfs_params.mmdvfs_in_vcp) {
			mtk_vdec_prepare_vcp_dvfs_data(ctx, vcp_dvfs_data);
			ret = vdec_if_set_param(ctx, SET_PARAM_MMDVFS, vcp_dvfs_data);
			if (ret != 0)
				mtk_v4l2_err("[VDVFS][%d] stream on ipi fail, ret %d", ctx->id, ret);
			mtk_vdec_dvfs_sync_vsi_data(ctx);
			mtk_v4l2_debug(0, "[VDVFS][%d(%d)] start DVFS(UP): freq:%d, op:%d",
				ctx->id, mtk_vcodec_get_state(ctx),
				ctx->dev->vdec_dvfs_params.target_freq,
				ctx->dec_params.operating_rate);
		} else {
			mtk_v4l2_debug(0, "[%d][VDVFS][VDEC] start ctrl DVFS in AP", ctx->id);
			mtk_vdec_dvfs_begin_inst(ctx);
		}
		mtk_vdec_pmqos_begin_inst(ctx);
		mutex_unlock(&ctx->dev->dec_dvfs_mutex);
		vcodec_trace_end();

		if (mtk_vdec_slc_enable && ctx->dev->dec_slc_ver == VDEC_SLC_V1) {
			mtk_vdec_slc_gid_request(ctx, &ctx->dev->dec_slc_frame);
			mtk_vdec_slc_gid_request(ctx, &ctx->dev->dec_slc_ube);
		}

	} else if (q->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		if (!mtk_vcodec_is_vcp(MTK_INST_DECODER)) {
			// set SET_PARAM_TOTAL_BITSTREAM_BUFQ_COUNT for
			// error handling when framing
			total_frame_bufq_count = q->num_buffers;
			if (vdec_if_set_param(ctx,
				SET_PARAM_TOTAL_BITSTREAM_BUFQ_COUNT,
				&total_frame_bufq_count)) {
				mtk_v4l2_err("[%d] Error!! Cannot set param",
					ctx->id);
			}
		}
		mtk_vdec_ts_reset(ctx);
	}

	mtk_vdec_set_param(ctx);
	if (q->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
		v4l2_m2m_set_dst_buffered(ctx->m2m_ctx, ctx->input_driven != NON_INPUT_DRIVEN);

	vcodec_trace_end();

	return 0;
}

static void vb2ops_vdec_stop_streaming(struct vb2_queue *q)
{

	struct vb2_buffer *dst_vb2_buf = NULL;
	struct vb2_v4l2_buffer *src_vb2_v4l2, *dst_vb2_v4l2;
	struct mtk_vcodec_ctx *ctx = vb2_get_drv_priv(q);
	struct mtk_video_dec_buf *src_buf_info = NULL;
	unsigned int i = 0;
	struct mtk_video_dec_buf *srcbuf, *dstbuf;
	unsigned long vcp_dvfs_data[1] = {0};
	int ret;

	vcodec_trace_begin("%s(%s)", __func__,
		q->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE ? "out" : "in");

	mtk_v4l2_debug(4, "[%d] (%d) state=(%x) ctx->decoded_frame_cnt=%d",
		ctx->id, q->type, mtk_vcodec_get_state(ctx), ctx->decoded_frame_cnt);

	if (q->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		if (mtk_vcodec_state_in_range(ctx, MTK_STATE_HEADER, MTK_STATE_NULL)) { //>= HEADER
			src_vb2_v4l2 = v4l2_m2m_next_src_buf(ctx->m2m_ctx);
			if (src_vb2_v4l2 != NULL) {
				src_buf_info = container_of(src_vb2_v4l2, struct mtk_video_dec_buf, vb);
				/* for bs buffer reuse case & avoid put to done twice*/
				mtk_vdec_reset_decoder(ctx, 0, &src_buf_info->bs_buffer, q->type);
			} else {
				mtk_vdec_reset_decoder(ctx, 0, NULL, q->type);
			}
		}

		mutex_lock(&ctx->buf_lock);
		while ((src_vb2_v4l2 = v4l2_m2m_src_buf_remove(ctx->m2m_ctx)))
			if (src_vb2_v4l2 != &ctx->dec_flush_buf->vb &&
				src_vb2_v4l2->vb2_buf.state == VB2_BUF_STATE_ACTIVE)
				v4l2_m2m_buf_done(src_vb2_v4l2, VB2_BUF_STATE_ERROR);

		/* check all buffer status */
		for (i = 0; i < q->num_buffers; i++)
			if (q->bufs[i]->state == VB2_BUF_STATE_ACTIVE) {
				src_vb2_v4l2 = container_of(q->bufs[i], struct vb2_v4l2_buffer, vb2_buf);
				srcbuf = container_of(src_vb2_v4l2, struct mtk_video_dec_buf, vb);
				mtk_v4l2_err("[%d] src num_buffers %d q_cnt %d queue id=%d state %d %d %d %d",
					ctx->id, q->num_buffers,
					atomic_read(&q->owned_by_drv_count),
					srcbuf->vb.vb2_buf.index,
					q->bufs[i]->state, srcbuf->queued_in_vb2,
					srcbuf->queued_in_v4l2, srcbuf->used);
				vb2_buffer_done(q->bufs[i], VB2_BUF_STATE_ERROR);
			}
		mutex_unlock(&ctx->buf_lock);

		ctx->dec_flush_buf->lastframe = NON_EOS;
		ctx->input_max_ts = 0;

		vcodec_trace_end();
		return;
	}

	mtk_vdec_flush_set_frame_wq(ctx);

	if (mtk_vcodec_state_in_range(ctx, MTK_STATE_HEADER, MTK_STATE_NULL)) { // >= HEADER

		/* Until STREAMOFF is called on the CAPTURE queue
		 * (acknowledging the event), the driver operates
		 * as if the resolution hasn't changed yet, i.e.
		 * VIDIOC_G_FMT< etc. return previous resolution.
		 * So we update picinfo here
		 */
		ctx->picinfo = ctx->last_decoded_picinfo;
		ctx->dpb_size = ctx->last_dpb_size;
		ctx->is_hdr = ctx->last_is_hdr;

		mtk_v4l2_debug(2,
			"[%d]-> new(%d,%d), old(%d,%d), real(%d,%d) bit:%d\n",
			ctx->id, ctx->last_decoded_picinfo.pic_w,
			ctx->last_decoded_picinfo.pic_h,
			ctx->picinfo.pic_w, ctx->picinfo.pic_h,
			ctx->last_decoded_picinfo.buf_w,
			ctx->last_decoded_picinfo.buf_h,
			ctx->picinfo.bitdepth);

		mtk_vdec_reset_decoder(ctx, 0, NULL, q->type);
	}

	mutex_lock(&ctx->buf_lock);
	while ((dst_vb2_v4l2 = v4l2_m2m_dst_buf_remove(ctx->m2m_ctx))) {
		dst_vb2_buf = &dst_vb2_v4l2->vb2_buf;
		dstbuf = container_of(dst_vb2_v4l2, struct mtk_video_dec_buf, vb);

		for (i = 0; i < dst_vb2_buf->num_planes; i++)
			vb2_set_plane_payload(dst_vb2_buf, i, 0);

		if (dst_vb2_buf->state == VB2_BUF_STATE_ACTIVE)
			v4l2_m2m_buf_done(dst_vb2_v4l2, VB2_BUF_STATE_ERROR);
	}
	/* check all buffer status */
	for (i = 0; i < q->num_buffers; i++) {
		dst_vb2_buf = q->bufs[i];
		dst_vb2_v4l2 = container_of(q->bufs[i], struct vb2_v4l2_buffer, vb2_buf);
		dstbuf = container_of(dst_vb2_v4l2, struct mtk_video_dec_buf, vb);
		mtk_v4l2_debug((dst_vb2_buf->state == VB2_BUF_STATE_ACTIVE) ? 0 : 4,
			"[%d] dst num_buffers %d q_cnt %d status=%x queue id=%d %p %lx state %d %d %d %d",
			ctx->id, q->num_buffers, atomic_read(&q->owned_by_drv_count),
			dstbuf->frame_buffer.status,
			dst_vb2_buf->index, &dstbuf->frame_buffer,
			(unsigned long)(&dstbuf->frame_buffer),
			dst_vb2_buf->state, dstbuf->queued_in_vb2,
			dstbuf->queued_in_v4l2, dstbuf->used);
		if (dst_vb2_buf->state == VB2_BUF_STATE_ACTIVE)
			vb2_buffer_done(q->bufs[i], VB2_BUF_STATE_ERROR);
	}
	mutex_unlock(&ctx->buf_lock);

	vcodec_trace_begin("dvfs(stream_off)");
	mutex_lock(&ctx->dev->dec_dvfs_mutex);
	ctx->is_active = 0;
	if (ctx->dev->vdec_dvfs_params.mmdvfs_in_vcp) {
		mtk_vdec_unprepare_vcp_dvfs_data(ctx, vcp_dvfs_data);
		ret = vdec_if_set_param(ctx, SET_PARAM_MMDVFS, vcp_dvfs_data);
		if (ret != 0)
			mtk_v4l2_err("[VDVFS][%d] stream off ipi fail, ret %d", ctx->id, ret);
		mtk_vdec_dvfs_sync_vsi_data(ctx);
		mtk_v4l2_debug(0, "[VDVFS][%d(%d)] stop DVFS (UP): freq:%d, op:%d",
			ctx->id, mtk_vcodec_get_state(ctx), ctx->dev->vdec_dvfs_params.target_freq,
			ctx->dec_params.operating_rate);
	} else {
		mtk_v4l2_debug(0, "[%d][VDVFS][VDEC] stop ctrl DVFS in AP", ctx->id);
		mtk_vdec_dvfs_end_inst(ctx);
	}
	mtk_vdec_pmqos_end_inst(ctx);
	mutex_unlock(&ctx->dev->dec_dvfs_mutex);
	vcodec_trace_end();

	if (mtk_vdec_slc_enable && ctx->dev->dec_slc_ver == VDEC_SLC_V1) {
		mtk_vdec_slc_gid_release(ctx, &ctx->dev->dec_slc_frame);
		mtk_vdec_slc_gid_release(ctx, &ctx->dev->dec_slc_ube);
	}

	vcodec_trace_end();
}

static void m2mops_vdec_device_run(void *priv)
{
	struct mtk_vcodec_ctx *ctx = priv;
	struct mtk_vcodec_dev *dev = ctx->dev;

	queue_work(dev->decode_workqueue, &ctx->decode_work);
}

static int m2mops_vdec_job_ready(void *m2m_priv)
{
	struct mtk_vcodec_ctx *ctx = m2m_priv;
	unsigned long flags;
	int ret = 1;

	if (!mtk_vcodec_is_state(ctx, MTK_STATE_HEADER))
		ret = 0;

	if (ctx->waiting_fmt)
		ret = 0;

	if ((ctx->last_decoded_picinfo.pic_w != ctx->picinfo.pic_w) ||
		(ctx->last_decoded_picinfo.pic_h != ctx->picinfo.pic_h) ||
		(ctx->last_dpb_size != ctx->dpb_size) ||
		(ctx->last_is_hdr != ctx->is_hdr))
		ret = 0;

	if (ctx->input_driven != NON_INPUT_DRIVEN && (*ctx->ipi_blocked))
		ret = 0;

	spin_lock_irqsave(&ctx->lpw_lock, flags);
	if (ctx->low_pw_mode && ctx->lpw_state == VDEC_LPW_WAIT)
		ret = 0;
	spin_unlock_irqrestore(&ctx->lpw_lock, flags);

	mtk_v4l2_debug(4, "[%d] ret %d", ctx->id, ret);

	return ret;
}

static void m2mops_vdec_job_abort(void *priv)
{
	struct mtk_vcodec_ctx *ctx = priv;

	if (ctx->input_driven == INPUT_DRIVEN_PUT_FRM) {
		if (vdec_if_set_param(ctx, SET_PARAM_FRAME_BUFFER, NULL))
			mtk_v4l2_err("[%d] Error!! Cannot set param SET_PARAM_FRAME_BUFFER", ctx->id);
	} else if (ctx->input_driven == INPUT_DRIVEN_CB_FRM)
		wake_up(&ctx->fm_wq);

	mtk_vcodec_set_state_from(ctx, MTK_STATE_STOP, MTK_STATE_HEADER);
	mtk_v4l2_debug(4, "[%d] state %d", ctx->id, mtk_vcodec_get_state(ctx));
}

static int vdec_set_hdr10plus_data(struct mtk_vcodec_ctx *ctx,
				   struct v4l2_vdec_hdr10plus_data *hdr10plus_data)
{
	struct __u8 __user *buffer = u64_to_user_ptr(hdr10plus_data->addr);
	struct hdr10plus_info *hdr10plus_buf = &ctx->hdr10plus_buf;

	mtk_v4l2_debug(4, "hdr10plus_data size %d", hdr10plus_data->size);
	hdr10plus_buf->size = hdr10plus_data->size;
	if (hdr10plus_buf->size > HDR10_PLUS_MAX_SIZE)
		hdr10plus_buf->size = HDR10_PLUS_MAX_SIZE;

	if (hdr10plus_buf->size == 0)
		return 0;

	if (buffer == NULL) {
		mtk_v4l2_err("invalid null pointer");
		return -EINVAL;
	}

	if (copy_from_user(&hdr10plus_buf->data, buffer, hdr10plus_buf->size)) {
		mtk_v4l2_err("copy hdr10plus from user fails");
		return -EFAULT;
	}

	return 0;
}

static enum vdec_set_param_type CID2SetParam(u32 id)
{
	switch (id) {
	case V4L2_CID_VDEC_TRICK_MODE:
		return SET_PARAM_TRICK_MODE;
	case V4L2_CID_VDEC_HDR10_INFO:
		return SET_PARAM_HDR10_INFO;
	case V4L2_CID_VDEC_NO_REORDER:
		return SET_PARAM_NO_REORDER;
	case V4L2_CID_VDEC_COMPRESSED_MODE:
		return SET_PARAM_COMPRESSED_MODE;
	case V4L2_CID_VDEC_SUBSAMPLE_MODE:
		return SET_PARAM_PER_FRAME_SUBSAMPLE_MODE;
	case V4L2_CID_VDEC_ACQUIRE_RESOURCE:
		return SET_PARAM_ACQUIRE_RESOURCE;
	case V4L2_CID_VDEC_VPEEK_MODE:
		return SET_PARAM_VPEEK_MODE;
	case V4L2_CID_VDEC_PLUS_DROP_RATIO:
		return SET_PARAM_VDEC_PLUS_DROP_RATIO;
	case V4L2_CID_VDEC_CONTAINER_FRAMERATE:
		return SET_PARAM_CONTAINER_FRAMERATE;
	case V4L2_CID_VDEC_DISABLE_DEBLOCK:
		return SET_PARAM_DISABLE_DEBLOCK;
	case V4L2_CID_VDEC_LOW_LATENCY:
		return SET_PARAM_LOW_LATENCY;
	}

	return SET_PARAM_MAX;
}

static enum vdec_get_param_type CID2GetParam(u32 id)
{
	switch (id) {
	case V4L2_CID_VDEC_TRICK_MODE:
		return GET_PARAM_TRICK_MODE;
	}

	return GET_PARAM_MAX;
}

static int mtk_vdec_g_v_ctrl(struct v4l2_ctrl *ctrl)
{
	struct mtk_vcodec_ctx *ctx = ctrl_to_ctx(ctrl);
	int ret = 0;
	struct mtk_color_desc *color_desc;

	switch (ctrl->id) {
	case V4L2_CID_MIN_BUFFERS_FOR_CAPTURE:
		if (mtk_vcodec_state_in_range(ctx, MTK_STATE_HEADER, MTK_STATE_NULL)) // >= HEADER
			ctrl->val = ctx->dpb_size;
		else {
			mtk_v4l2_debug(1, "Seqinfo not ready");
			ctrl->val = 0;
		}
		break;
	case V4L2_CID_MPEG_MTK_COLOR_DESC:
		color_desc = (struct mtk_color_desc *)ctrl->p_new.p_u32;
		if (vdec_if_get_param(ctx, GET_PARAM_COLOR_DESC, color_desc)
		    != 0) {
			mtk_v4l2_err("[%d] Error!! Cannot get param", ctx->id);
			ret = -EINVAL;
		}
		break;
	case V4L2_CID_MPEG_MTK_FIX_BUFFERS:
		if (vdec_if_get_param(ctx,
		    GET_PARAM_PLATFORM_SUPPORTED_FIX_BUFFERS, &ctrl->val)
		    != 0) {
			mtk_v4l2_err("[%d] Error!! Cannot get param", ctx->id);
			ret = -EINVAL;
		}
		break;
	case V4L2_CID_MPEG_MTK_INTERLACING:
		if (vdec_if_get_param(ctx, GET_PARAM_INTERLACING, &ctrl->val)
		    != 0) {
			mtk_v4l2_err("[%d] Error!! Cannot get param", ctx->id);
			ret = -EINVAL;
		}
		break;
	case V4L2_CID_MPEG_MTK_INTERLACING_FIELD_SEQ:
		if (vdec_if_get_param(ctx,
			GET_PARAM_INTERLACING_FIELD_SEQ, &ctrl->val) != 0) {
			mtk_v4l2_err("[%d] Error!! Cannot get param", ctx->id);
			ret = -EINVAL;
		}
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
	case V4L2_CID_VDEC_SLC_SUPPORT_VER:
		ctrl->val = ctx->dev->dec_slc_ver;
		break;
	case V4L2_CID_VDEC_RESOURCE_METRICS: {
		struct v4l2_vdec_resource_metrics *metrics = ctrl->p_new.p;
		struct vdec_resource_info res_info;

		if (vdec_if_get_param(ctx, GET_PARAM_RES_INFO, &res_info)) {
			mtk_v4l2_err(
				"[%d]Error!! Cannot get param : GET_PARAM_RES_INFO ERR",
				ctx->id);
			ret = -EINVAL;
			break;
		}
		if (res_info.hw_used[MTK_VDEC_CORE])
			metrics->core_used |= (1 << 0);
		if (res_info.hw_used[MTK_VDEC_CORE1])
			metrics->core_used |= (1 << 1);

		metrics->core_usage = res_info.usage;
		metrics->gce = res_info.gce;
		break;
	}
	case V4L2_CID_VDEC_MAX_BUF_INFO: {
		struct v4l2_vdec_max_buf_info *info = ctrl->p_new.p;

		mutex_lock(&ctx->dev->cap_mutex);
		mtk_vdec_max_buf_info.pixelformat = ctx->max_buf_pixelformat;
		mtk_vdec_max_buf_info.max_width = ctx->max_buf_width;
		mtk_vdec_max_buf_info.max_height = ctx->max_buf_height;

		if (vdec_if_get_param(ctx, GET_PARAM_VDEC_CAP_MAX_BUF_INFO, &mtk_vdec_max_buf_info)) {
			mtk_v4l2_err(
				"[%d]Error!! Cannot get param : GET_PARAM_VDEC_CAP_MAX_BUF_INFO ERR",
				ctx->id);
			ret = -EINVAL;
			mutex_unlock(&ctx->dev->cap_mutex);
			break;
		}

		if (mtk_vdec_max_buf_info.max_internal_buf_size == 0 ||
			mtk_vdec_max_buf_info.max_dpb_count == 0 ||
			mtk_vdec_max_buf_info.max_frame_buf_size == 0) {
			mtk_v4l2_err(
				"[%d]Error!! Invalid input argument for V4L2_CID_VDEC_MAX_BUF_INFO",
				ctx->id);
			ret = -EINVAL;
			mutex_unlock(&ctx->dev->cap_mutex);
			break;
		}

		info->pixelformat = mtk_vdec_max_buf_info.pixelformat;
		info->max_width = mtk_vdec_max_buf_info.max_width;
		info->max_height = mtk_vdec_max_buf_info.max_height;
		info->max_internal_buf_size = mtk_vdec_max_buf_info.max_internal_buf_size;
		info->max_dpb_count = mtk_vdec_max_buf_info.max_dpb_count;
		info->max_frame_buf_size = mtk_vdec_max_buf_info.max_frame_buf_size;
		mutex_unlock(&ctx->dev->cap_mutex);
		break;
	}
	case V4L2_CID_VDEC_BANDWIDTH_INFO: {
		struct v4l2_vdec_bandwidth_info *info = ctrl->p_new.p;
		struct vdec_bandwidth_info bandwidth_info;
		int i;

		if (vdec_if_get_param(ctx, GET_PARAM_BANDWIDTH_INFO, &bandwidth_info)) {
			mtk_v4l2_err(
				"[%d]Error!! Cannot get param : GET_PARAM_BANDWIDTH_INFO ERR",
				ctx->id);
			ret = -EINVAL;
			break;
		}

		BUILD_BUG_ON((int)MTK_BW_COUNT != (int)V4L2_BW_COUNT);

		for (i = 0; i < MTK_BW_COUNT; i++)
			info->bandwidth_per_pixel[i] = bandwidth_info.bandwidth_per_pixel[i];

		info->compress = bandwidth_info.compress;
		info->vsd = bandwidth_info.vsd;
		memcpy(&info->modifier, &bandwidth_info.modifier, sizeof(struct v4l2_vdec_fmt_modifier));
		break;
	}
	case V4L2_CID_VDEC_TRICK_MODE: {
		enum vdec_get_param_type type = CID2GetParam(ctrl->id);

		if (ctrl->is_ptr)
			ret = vdec_if_get_param(ctx, type, ctrl->p_new.p);
		else {
			unsigned int out = 0;

			ret = vdec_if_get_param(ctx, type, &out);
			ctrl->val = out;
		}
		break;
	}
	default:
		ret = -EINVAL;
	}
	return ret;
}

static int mtk_vdec_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct mtk_vcodec_ctx *ctx = ctrl_to_ctx(ctrl);

	mtk_v4l2_debug(4, "[%d] id 0x%x val %d array[0] %d array[1] %d",
				   ctx->id, ctrl->id, ctrl->val,
				   ctrl->p_new.p_u32[0], ctrl->p_new.p_u32[1]);

	switch (ctrl->id) {
	case V4L2_CID_MPEG_MTK_DECODE_MODE:
		ctx->dec_params.decode_mode = ctrl->val;
		ctx->dec_params.dec_param_change |= MTK_DEC_PARAM_DECODE_MODE;
		break;
	case V4L2_CID_MPEG_MTK_SEC_DECODE: {
#if IS_ENABLED(CONFIG_MTK_TINYSYS_VCP_SUPPORT)
		struct vb2_queue *src_vq;

		if (ctrl->val) {
			if (vcp_get_io_device_ex(VCP_IOMMU_SEC)) {
				src_vq = v4l2_m2m_get_vq(ctx->m2m_ctx,
					V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
				if (src_vq == NULL) {
					mtk_v4l2_err("Error! src_vq is NULL!");
					return -EINVAL;
				}
				src_vq->dev = vcp_get_io_device_ex(VCP_IOMMU_SEC);
				mtk_v4l2_debug(4, "[%d] src_vq use VCP_IOMMU_SEC domain %p", ctx->id, src_vq->dev);
			}

		}
#endif
		ctx->dec_params.svp_mode = ctrl->val;
		mtk_v4l2_debug(ctrl->val ? 0 : 1, "[%d] V4L2_CID_MPEG_MTK_SEC_DECODE id %d val %d",
			ctx->id, ctrl->id, ctrl->val);
		break;
	}
	case V4L2_CID_MPEG_MTK_FIXED_MAX_FRAME_BUFFER:
		ctx->dec_params.fixed_max_frame_size_width = ctrl->p_new.p_u32[0];
		ctx->dec_params.fixed_max_frame_size_height = ctrl->p_new.p_u32[1];
		ctx->dec_params.fixed_max_frame_buffer_mode = ctrl->p_new.p_u32[2];
		ctx->dec_params.dec_param_change |= MTK_DEC_PARAM_FIXED_MAX_FRAME_SIZE;
		break;
	case V4L2_CID_MPEG_MTK_CRC_PATH:
		ctx->dec_params.crc_path = ctrl->p_new.p_char;
		ctx->dec_params.dec_param_change |= MTK_DEC_PARAM_CRC_PATH;
		break;
	case V4L2_CID_MPEG_MTK_GOLDEN_PATH:
		ctx->dec_params.golden_path = ctrl->p_new.p_char;
		ctx->dec_params.dec_param_change |= MTK_DEC_PARAM_GOLDEN_PATH;
		break;
	case V4L2_CID_MPEG_MTK_SET_WAIT_KEY_FRAME:
		ctx->dec_params.wait_key_frame = ctrl->val;
		ctx->dec_params.dec_param_change |= MTK_DEC_PARAM_WAIT_KEY_FRAME;
		break;
	case V4L2_CID_MPEG_MTK_SET_DECODE_ERROR_HANDLE_MODE:
		ctx->dec_params.decode_error_handle_mode = ctrl->val;
		ctx->dec_params.dec_param_change |= MTK_DEC_PARAM_DECODE_ERROR_HANDLE_MODE;
		break;
	case V4L2_CID_MPEG_MTK_OPERATING_RATE:
		ctx->dec_params.operating_rate = ctrl->val;
		ctx->dec_params.dec_param_change |= MTK_DEC_PARAM_OPERATING_RATE;
		break;
	case V4L2_CID_MPEG_MTK_REAL_TIME_PRIORITY:
		ctx->dec_params.priority = ctrl->val;
		break;
	case V4L2_CID_MPEG_MTK_QUEUED_FRAMEBUF_COUNT:
		ctx->dec_params.queued_frame_buf_count = ctrl->val;
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
	case V4L2_CID_VDEC_DETECT_TIMESTAMP:
		ctx->detect_ts_param.enable_detect_ts = ctrl->val;
		break;
	case V4L2_CID_MPEG_MTK_CALLING_PID:
		ctx->cpu_caller_pid = ctrl->val;
		mtk_v4l2_debug(1, "[%d] set caller pid %d", ctx->id, ctx->cpu_caller_pid);
		break;
	case V4L2_CID_VDEC_TRICK_MODE:
	case V4L2_CID_VDEC_NO_REORDER:
	case V4L2_CID_VDEC_HDR10_INFO:
	case V4L2_CID_VDEC_COMPRESSED_MODE:
	case V4L2_CID_VDEC_SUBSAMPLE_MODE:
	case V4L2_CID_VDEC_ACQUIRE_RESOURCE:
	case V4L2_CID_VDEC_VPEEK_MODE:
	case V4L2_CID_VDEC_PLUS_DROP_RATIO:
	case V4L2_CID_VDEC_CONTAINER_FRAMERATE:
	case V4L2_CID_VDEC_DISABLE_DEBLOCK:
	case V4L2_CID_VDEC_LOW_LATENCY: {
		int ret;
		enum vdec_set_param_type type = SET_PARAM_MAX;

		if (!ctx->drv_handle)
			return 0;

		type = CID2SetParam(ctrl->id);
		if (ctrl->is_ptr)
			ret = vdec_if_set_param(ctx, type, ctrl->p_new.p);
		else {
			unsigned long in = ctrl->val;

			ret = vdec_if_set_param(ctx, type, &in);
		}
		if (ctrl->id == V4L2_CID_VDEC_ACQUIRE_RESOURCE) {
			// for un-paused scenario
			v4l2_m2m_try_schedule(ctx->m2m_ctx);
		}
		if (ret < 0) {
			mtk_v4l2_debug(5, "ctrl-id=0x%x fails! ret %d",
					ctrl->id, ret);
			return ret;
		}
		break;
	}
	case V4L2_CID_VDEC_HDR10PLUS_DATA:
		return vdec_set_hdr10plus_data(ctx, ctrl->p_new.p);
	case V4L2_CID_VDEC_MAX_BUF_INFO: {
		ctx->max_buf_pixelformat = ctrl->p_new.p_u32[0];
		ctx->max_buf_width = ctrl->p_new.p_u32[1];
		ctx->max_buf_height = ctrl->p_new.p_u32[2];
		break;
	}
	case V4L2_CID_MPEG_MTK_LINECOUNT_THRESHOLD:
		if (ctrl->val) {
			ctx->dec_params.linecount_threshold_mode = ctrl->val;
			ctx->dec_params.dec_param_change |= MTK_DEC_PARAM_LINECOUNT_THRESHOLD;
		}
		mtk_v4l2_debug(ctrl->val ? 0 : 1, "[%d] V4L2_CID_MPEG_MTK_LINECOUNT_THRESHOLD id %d val %d",
			ctx->id, ctrl->id, ctrl->val);
		break;
	case V4L2_CID_VDEC_INPUT_SLOT:
		ctx->input_slot = ctrl->val;
		break;
	default:
		mtk_v4l2_debug(4, "ctrl-id=%x not support!", ctrl->id);
		return -EINVAL;
	}

	return 0;
}

static const struct v4l2_ctrl_ops mtk_vcodec_dec_ctrl_ops = {
	.g_volatile_ctrl = mtk_vdec_g_v_ctrl,
	.s_ctrl = mtk_vdec_s_ctrl,
};


void mtk_vcodec_dec_custom_ctrls_check(struct v4l2_ctrl_handler *hdl,
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

int mtk_vcodec_dec_ctrls_setup(struct mtk_vcodec_ctx *ctx)
{
	struct v4l2_ctrl *ctrl;
	const struct v4l2_ctrl_ops *ops = &mtk_vcodec_dec_ctrl_ops;
	struct v4l2_ctrl_handler *handler = &ctx->ctrl_hdl;
	struct v4l2_ctrl_config cfg;

	v4l2_ctrl_handler_init(&ctx->ctrl_hdl, MTK_MAX_CTRLS_HINT);

	/* g_volatile_ctrl */
	ctrl = v4l2_ctrl_new_std(&ctx->ctrl_hdl,
		&mtk_vcodec_dec_ctrl_ops,
		V4L2_CID_MIN_BUFFERS_FOR_CAPTURE,
		0, 32, 1, 1);
	if (ctrl)
		ctrl->flags |= V4L2_CTRL_FLAG_VOLATILE;

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_FIX_BUFFERS;
	cfg.type = V4L2_CTRL_TYPE_INTEGER;
	cfg.flags = V4L2_CTRL_FLAG_READ_ONLY | V4L2_CTRL_FLAG_VOLATILE;
	cfg.name = "Video fix buffers";
	cfg.min = 0;
	cfg.max = 0xF;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	mtk_vcodec_dec_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_INTERLACING;
	cfg.type = V4L2_CTRL_TYPE_BOOLEAN;
	cfg.flags = V4L2_CTRL_FLAG_READ_ONLY | V4L2_CTRL_FLAG_VOLATILE;
	cfg.name = "MTK Query Interlacing";
	cfg.min = 0;
	cfg.max = 1;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	mtk_vcodec_dec_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_INTERLACING_FIELD_SEQ;
	cfg.type = V4L2_CTRL_TYPE_BOOLEAN;
	cfg.flags = V4L2_CTRL_FLAG_READ_ONLY | V4L2_CTRL_FLAG_VOLATILE;
	cfg.name = "MTK Query Interlacing FieldSeq";
	cfg.min = 0;
	cfg.max = 1;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	mtk_vcodec_dec_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_COLOR_DESC;
	cfg.type = V4L2_CTRL_TYPE_U32;
	cfg.flags = V4L2_CTRL_FLAG_READ_ONLY | V4L2_CTRL_FLAG_VOLATILE;
	cfg.name = "MTK vdec Color Description for HDR";
	cfg.min = 0;
	cfg.max = 0xffffffff;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	cfg.dims[0] = (sizeof(struct mtk_color_desc)/sizeof(u32));
	mtk_vcodec_dec_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_VDEC_SLC_SUPPORT_VER;
	cfg.type = V4L2_CTRL_TYPE_INTEGER;
	cfg.flags = V4L2_CTRL_FLAG_READ_ONLY | V4L2_CTRL_FLAG_VOLATILE;
	cfg.name = "MTK vdec SLC support ver";
	cfg.min = 0;
	cfg.max = 1;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	mtk_vcodec_dec_custom_ctrls_check(handler, &cfg, NULL);

	/* s_ctrl */
	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_DECODE_MODE;
	cfg.type = V4L2_CTRL_TYPE_INTEGER;
	cfg.flags = V4L2_CTRL_FLAG_WRITE_ONLY;
	cfg.name = "Video decode mode";
	cfg.min = 0;
	cfg.max = 32;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	mtk_vcodec_dec_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_SEC_DECODE;
	cfg.type = V4L2_CTRL_TYPE_INTEGER;
	cfg.flags = V4L2_CTRL_FLAG_WRITE_ONLY;
	cfg.name = "Video Sec Decode path";
	cfg.min = 0;
	cfg.max = 32;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	mtk_vcodec_dec_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_FIXED_MAX_FRAME_BUFFER;
	cfg.type = V4L2_CTRL_TYPE_U32;
	cfg.flags = V4L2_CTRL_FLAG_WRITE_ONLY;
	cfg.name = "Video fixed maximum frame size";
	cfg.min = 0;
	cfg.max = 65535;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	/* width/height/mode */
	cfg.dims[0] = 3;
	mtk_vcodec_dec_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_CRC_PATH;
	cfg.type = V4L2_CTRL_TYPE_STRING;
	cfg.flags = V4L2_CTRL_FLAG_WRITE_ONLY;
	cfg.name = "Video crc path";
	cfg.min = 0;
	cfg.max = 255;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	mtk_vcodec_dec_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_GOLDEN_PATH;
	cfg.type = V4L2_CTRL_TYPE_STRING;
	cfg.flags = V4L2_CTRL_FLAG_WRITE_ONLY;
	cfg.name = "Video golden path";
	cfg.min = 0;
	cfg.max = 255;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	mtk_vcodec_dec_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_SET_WAIT_KEY_FRAME;
	cfg.type = V4L2_CTRL_TYPE_INTEGER;
	cfg.flags = V4L2_CTRL_FLAG_WRITE_ONLY;
	cfg.name = "Wait key frame";
	cfg.min = 0;
	cfg.max = 255;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	mtk_vcodec_dec_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_SET_DECODE_ERROR_HANDLE_MODE;
	cfg.type = V4L2_CTRL_TYPE_INTEGER;
	cfg.flags = V4L2_CTRL_FLAG_WRITE_ONLY;
	cfg.name = "Decode Error Handle Mode";
	cfg.min = 0;
	cfg.max = 255;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	mtk_vcodec_dec_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_OPERATING_RATE;
	cfg.type = V4L2_CTRL_TYPE_INTEGER;
	cfg.flags = V4L2_CTRL_FLAG_WRITE_ONLY;
	cfg.name = "Vdec Operating Rate";
	cfg.min = 0;
	cfg.max = 4096;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	mtk_vcodec_dec_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_REAL_TIME_PRIORITY;
	cfg.type = V4L2_CTRL_TYPE_INTEGER;
	cfg.flags = V4L2_CTRL_FLAG_WRITE_ONLY;
	cfg.name = "Vdec Real Time Priority";
	cfg.min = -1;
	cfg.max = 1;
	cfg.step = 1;
	cfg.def = -1;
	cfg.ops = ops;
	mtk_vcodec_dec_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_QUEUED_FRAMEBUF_COUNT;
	cfg.type = V4L2_CTRL_TYPE_INTEGER;
	cfg.flags = V4L2_CTRL_FLAG_WRITE_ONLY;
	cfg.name = "Video queued frame buf count";
	cfg.min = 0;
	cfg.max = 64;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	mtk_vcodec_dec_custom_ctrls_check(handler, &cfg, NULL);

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
	mtk_vcodec_dec_custom_ctrls_check(handler, &cfg, NULL);

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
	mtk_vcodec_dec_custom_ctrls_check(handler, &cfg, NULL);

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
	mtk_vcodec_dec_custom_ctrls_check(handler, &cfg, NULL);

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
	mtk_vcodec_dec_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_VDEC_DETECT_TIMESTAMP;
	cfg.type = V4L2_CTRL_TYPE_BOOLEAN;
	cfg.flags = V4L2_CTRL_FLAG_WRITE_ONLY;
	cfg.name = "VDEC detect timestamp mode";
	cfg.min = 0;
	cfg.max = 1;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	mtk_vcodec_dec_custom_ctrls_check(handler, &cfg, NULL);

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
	mtk_vcodec_dec_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_VDEC_HDR10_INFO;
	cfg.type = V4L2_CTRL_TYPE_U8;
	cfg.flags = V4L2_CTRL_FLAG_HAS_PAYLOAD | V4L2_CTRL_FLAG_EXECUTE_ON_WRITE;
	cfg.name = "VDEC set HDR10 information";
	cfg.min = 0;
	cfg.max = 0xFF;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	cfg.dims[0] = (sizeof(struct v4l2_vdec_hdr10_info));
	mtk_vcodec_dec_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_VDEC_HDR10PLUS_DATA;
	cfg.type = V4L2_CTRL_TYPE_U8;
	cfg.flags = V4L2_CTRL_FLAG_HAS_PAYLOAD | V4L2_CTRL_FLAG_EXECUTE_ON_WRITE;
	cfg.name = "VDEC set HDR10PLUS data";
	cfg.min = 0;
	cfg.max = 0xFF;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	cfg.dims[0] = (sizeof(struct v4l2_vdec_hdr10plus_data));
	mtk_vcodec_dec_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_VDEC_TRICK_MODE;
	cfg.type = V4L2_CTRL_TYPE_INTEGER;
	cfg.flags = V4L2_CTRL_FLAG_VOLATILE | V4L2_CTRL_FLAG_EXECUTE_ON_WRITE;
	cfg.name = "VDEC set/get trick mode";
	cfg.min = V4L2_VDEC_TRICK_MODE_ALL;
	cfg.max = V4L2_VDEC_TRICK_MODE_I;
	cfg.step = 1;
	cfg.def = V4L2_VDEC_TRICK_MODE_ALL;
	cfg.ops = ops;
	mtk_vcodec_dec_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_VDEC_NO_REORDER;
	cfg.type = V4L2_CTRL_TYPE_BOOLEAN;
	cfg.flags = V4L2_CTRL_FLAG_EXECUTE_ON_WRITE;
	cfg.name = "VDEC show frame without reorder";
	cfg.min = 0;
	cfg.max = 1;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	mtk_vcodec_dec_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_VDEC_COMPRESSED_MODE;
	cfg.type = V4L2_CTRL_TYPE_INTEGER;
	cfg.flags = V4L2_CTRL_FLAG_EXECUTE_ON_WRITE;
	cfg.name = "VDEC set compressed more";
	cfg.min = V4L2_VDEC_COMPRESSED_DEFAULT;
	cfg.max = V4L2_VDEC_COMPRESSED_OFF;
	cfg.step = 1;
	cfg.def = V4L2_VDEC_COMPRESSED_DEFAULT;
	cfg.ops = ops;
	mtk_vcodec_dec_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_VDEC_SUBSAMPLE_MODE;
	cfg.type = V4L2_CTRL_TYPE_INTEGER;
	cfg.flags = V4L2_CTRL_FLAG_EXECUTE_ON_WRITE;
	cfg.name = "VDEC set subsample mode";
	cfg.min = V4L2_VDEC_SUBSAMPLE_DEFAULT;
	cfg.max = V4L2_VDEC_SUBSAMPLE_ON;
	cfg.step = 1;
	cfg.def = V4L2_VDEC_SUBSAMPLE_DEFAULT;
	cfg.ops = ops;
	mtk_vcodec_dec_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_VDEC_ACQUIRE_RESOURCE;
	cfg.type = V4L2_CTRL_TYPE_U8;
	cfg.flags = V4L2_CTRL_FLAG_HAS_PAYLOAD | V4L2_CTRL_FLAG_EXECUTE_ON_WRITE;
	cfg.name = "VDEC acquire resource";
	cfg.min = 0;
	cfg.max = 0xFF;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	cfg.dims[0] = (sizeof(struct v4l2_vdec_resource_parameter)),
	mtk_vcodec_dec_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_VDEC_RESOURCE_METRICS;
	cfg.type = V4L2_CTRL_TYPE_U8;
	cfg.flags = V4L2_CTRL_FLAG_HAS_PAYLOAD | V4L2_CTRL_FLAG_VOLATILE | V4L2_CTRL_FLAG_READ_ONLY;
	cfg.name = "get VDEC resource metrics";
	cfg.min = 0;
	cfg.max = 0xFF;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	cfg.dims[0] = (sizeof(struct v4l2_vdec_resource_metrics)),
	mtk_vcodec_dec_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_VDEC_VPEEK_MODE;
	cfg.type = V4L2_CTRL_TYPE_BOOLEAN;
	cfg.flags = V4L2_CTRL_FLAG_EXECUTE_ON_WRITE;
	cfg.name = "VDEC show first frame directly";
	cfg.min = 0;
	cfg.max = 1;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	mtk_vcodec_dec_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_VDEC_PLUS_DROP_RATIO;
	cfg.type = V4L2_CTRL_TYPE_INTEGER;
	cfg.flags = V4L2_CTRL_FLAG_EXECUTE_ON_WRITE;
	cfg.name = "VDEC set drop ratio";
	cfg.min = 0;
	cfg.max = 4;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	mtk_vcodec_dec_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_VDEC_CONTAINER_FRAMERATE;
	cfg.type = V4L2_CTRL_TYPE_U32;
	cfg.flags = V4L2_CTRL_FLAG_EXECUTE_ON_WRITE;
	cfg.name = "VDEC set container framerate";
	cfg.min = 0;
	cfg.max = 0xFFFFFFFF;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	mtk_vcodec_dec_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_VDEC_DISABLE_DEBLOCK;
	cfg.type = V4L2_CTRL_TYPE_INTEGER;
	cfg.flags = V4L2_CTRL_FLAG_EXECUTE_ON_WRITE;
	cfg.name = "VDEC set disable deblocking";
	cfg.min = 0;
	cfg.max = 1;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	mtk_vcodec_dec_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_VDEC_LOW_LATENCY;
	cfg.type = V4L2_CTRL_TYPE_U8;
	cfg.flags = V4L2_CTRL_FLAG_HAS_PAYLOAD | V4L2_CTRL_FLAG_VOLATILE | V4L2_CTRL_FLAG_EXECUTE_ON_WRITE;
	cfg.name = "VDEC set low latency info";
	cfg.min = 0;
	cfg.max = 0xFF;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	cfg.dims[0] = (sizeof(struct v4l2_vdec_low_latency_parameter)),
	mtk_vcodec_dec_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_VDEC_MAX_BUF_INFO;
	cfg.type = V4L2_CTRL_TYPE_U8;
	cfg.flags = V4L2_CTRL_FLAG_HAS_PAYLOAD | V4L2_CTRL_FLAG_VOLATILE |
			 V4L2_CTRL_FLAG_EXECUTE_ON_WRITE;
	cfg.name = "get VDEC max buffer info";
	cfg.min = 0;
	cfg.max = 0xFF;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	cfg.dims[0] = (sizeof(struct v4l2_vdec_max_buf_info)),
	mtk_vcodec_dec_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_VDEC_BANDWIDTH_INFO;
	cfg.type = V4L2_CTRL_TYPE_U8;
	cfg.flags = V4L2_CTRL_FLAG_HAS_PAYLOAD | V4L2_CTRL_FLAG_VOLATILE | V4L2_CTRL_FLAG_READ_ONLY;
	cfg.name = "get VDEC bandwidth info";
	cfg.min = 0;
	cfg.max = 0xFF;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	cfg.dims[0] = (sizeof(struct v4l2_vdec_bandwidth_info)),
	mtk_vcodec_dec_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_LINECOUNT_THRESHOLD;
	cfg.type = V4L2_CTRL_TYPE_INTEGER;
	cfg.flags = V4L2_CTRL_FLAG_WRITE_ONLY;
	cfg.name = "Video LineCount ThresHold";
	cfg.min = 0;
	cfg.max = 32;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	mtk_vcodec_dec_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_VDEC_INPUT_SLOT;
	cfg.type = V4L2_CTRL_TYPE_INTEGER;
	cfg.flags = V4L2_CTRL_FLAG_WRITE_ONLY;
	cfg.name = "Video Input Slot";
	cfg.min = 0;
	cfg.max = 0x7fffffff;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	mtk_vcodec_dec_custom_ctrls_check(handler, &cfg, NULL);

	if (ctx->ctrl_hdl.error) {
		mtk_v4l2_err("Adding control failed %d",
					 ctx->ctrl_hdl.error);
		return ctx->ctrl_hdl.error;
	}

	v4l2_ctrl_handler_setup(&ctx->ctrl_hdl);
	return 0;
}

const struct v4l2_m2m_ops mtk_vdec_m2m_ops = {
	.device_run     = m2mops_vdec_device_run,
	.job_ready      = m2mops_vdec_job_ready,
	.job_abort      = m2mops_vdec_job_abort,
};

static const struct vb2_ops mtk_vdec_vb2_ops = {
	.queue_setup    = vb2ops_vdec_queue_setup,
	.buf_prepare    = vb2ops_vdec_buf_prepare,
	.buf_queue      = vb2ops_vdec_buf_queue,
	.wait_prepare   = vb2_ops_wait_prepare,
	.wait_finish    = vb2_ops_wait_finish,
	.buf_init       = vb2ops_vdec_buf_init,
	.buf_finish     = vb2ops_vdec_buf_finish,
	.buf_cleanup = vb2ops_vdec_buf_cleanup,
	.start_streaming        = vb2ops_vdec_start_streaming,
	.stop_streaming = vb2ops_vdec_stop_streaming,
};

const struct v4l2_ioctl_ops mtk_vdec_ioctl_ops = {
	.vidioc_streamon        = v4l2_m2m_ioctl_streamon,
	.vidioc_streamoff       = v4l2_m2m_ioctl_streamoff,
	.vidioc_reqbufs         = vidioc_vdec_reqbufs,
	.vidioc_querybuf        = v4l2_m2m_ioctl_querybuf,
	.vidioc_expbuf          = v4l2_m2m_ioctl_expbuf,

	.vidioc_qbuf            = vidioc_vdec_qbuf,
	.vidioc_dqbuf           = vidioc_vdec_dqbuf,

	.vidioc_try_fmt_vid_cap_mplane  = vidioc_try_fmt_vid_cap_mplane,
	.vidioc_try_fmt_vid_out_mplane  = vidioc_try_fmt_vid_out_mplane,

	.vidioc_s_fmt_vid_cap_mplane    = vidioc_vdec_s_fmt,
	.vidioc_s_fmt_vid_out_mplane    = vidioc_vdec_s_fmt,
	.vidioc_g_fmt_vid_cap_mplane    = vidioc_vdec_g_fmt,
	.vidioc_g_fmt_vid_out_mplane    = vidioc_vdec_g_fmt,

	.vidioc_create_bufs             = v4l2_m2m_ioctl_create_bufs,

	.vidioc_enum_fmt_vid_cap = vidioc_vdec_enum_fmt_vid_cap_mplane,
	.vidioc_enum_fmt_vid_out = vidioc_vdec_enum_fmt_vid_out_mplane,
	.vidioc_enum_framesizes = vidioc_enum_framesizes,
	.vidioc_enum_frameintervals     = vidioc_enum_frameintervals,

	.vidioc_querycap                = vidioc_vdec_querycap,
	.vidioc_subscribe_event         = vidioc_vdec_subscribe_evt,
	.vidioc_unsubscribe_event       = v4l2_event_unsubscribe,
	.vidioc_g_selection             = vidioc_vdec_g_selection,
	.vidioc_s_selection             = vidioc_vdec_s_selection,
	.vidioc_g_parm                  = vidioc_g_parm,
	.vidioc_decoder_cmd     = vidioc_decoder_cmd,
	.vidioc_try_decoder_cmd = vidioc_try_decoder_cmd,
};

static void *mtk_vdec_dc_attach_dmabuf(struct vb2_buffer *vb, struct device *dev,
	struct dma_buf *dbuf, unsigned long size)
{
	void *ptr_ret;
	struct vb2_dc_buf *buf;
	struct dma_buf_attachment *dba;

	if (vb->vb2_queue->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
		mtk_dma_buf_set_name(dbuf, "vdec_bs");
	else
		mtk_dma_buf_set_name(dbuf, "vdec_frame");

	ptr_ret = vb2_dma_contig_memops.attach_dmabuf(vb, dev, dbuf, size);
	if (!IS_ERR_OR_NULL(ptr_ret)) {
		buf = (struct vb2_dc_buf *)ptr_ret;
		dba = (struct dma_buf_attachment *)buf->db_attach;
		/* always skip cache operations, we handle it manually */
		dba->dma_map_attrs |= DMA_ATTR_SKIP_CPU_SYNC;
	}

	return ptr_ret;
}

int mtk_vcodec_dec_queue_init(void *priv, struct vb2_queue *src_vq,
	struct vb2_queue *dst_vq)
{
	struct mtk_vcodec_ctx *ctx = priv;
	char name[32];
	int ret = 0;

	mtk_v4l2_debug(4, "[%d]", ctx->id);

	SNPRINTF(name, sizeof(name), "mtk_vdec-%d-out", ctx->id);
	src_vq->type            = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	src_vq->io_modes        = VB2_DMABUF | VB2_MMAP;
	src_vq->drv_priv        = ctx;
	src_vq->buf_struct_size = sizeof(struct mtk_video_dec_buf);
	src_vq->ops             = &mtk_vdec_vb2_ops;
	vdec_dma_contig_memops  = vb2_dma_contig_memops;
	vdec_dma_contig_memops.attach_dmabuf = mtk_vdec_dc_attach_dmabuf;
	src_vq->mem_ops	        = &vdec_dma_contig_memops;
	mtk_v4l2_debug(4, "[%s] src_vq use vdec_dma_contig_memops", name);
#if (!(IS_ENABLED(CONFIG_DEVICE_MODULES_ARM_SMMU_V3)))
	// svp_mode will be raised in mtk_vdec_s_ctrl which will be later than mtk_vcodec_dec_queue_init
	// init vdec_sec_dma_contig_memops without checking svp_mode value to avoid could not init sec
	// dma_contig_memops which will cause input/output buffer secure handle will be 0,
	// really mem_ops init for sec will finish at vb2ops_vdec_queue_setup
	if (is_disable_map_sec() && mtk_vdec_is_vcu()) {
		vdec_sec_dma_contig_memops = vdec_dma_contig_memops;
		vdec_sec_dma_contig_memops.map_dmabuf   = mtk_vdec_sec_dc_map_dmabuf;
		vdec_sec_dma_contig_memops.unmap_dmabuf = mtk_vdec_sec_dc_unmap_dmabuf;
	}
	if (ctx->dec_params.svp_mode && is_disable_map_sec() && mtk_vdec_is_vcu()) {
		src_vq->mem_ops = &vdec_sec_dma_contig_memops;
		mtk_v4l2_debug(4, "src_vq use vdec_sec_dma_contig_memops");
	}
#endif
	src_vq->bidirectional   = 1;

	src_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	src_vq->lock            = &ctx->q_mutex;
#if IS_ENABLED(CONFIG_MTK_TINYSYS_VCP_SUPPORT)
	if (ctx->dev->support_acp && mtk_vdec_acp_enable &&
	    !ctx->dec_params.svp_mode && vcp_get_io_device_ex(VCP_IOMMU_ACP_VDEC) != NULL) {
		src_vq->dev     = vcp_get_io_device_ex(VCP_IOMMU_ACP_VDEC);
		mtk_v4l2_debug(4, "[%s] use VCP_IOMMU_ACP_VDEC domain %p", name, src_vq->dev);
	} else if (ctx->dev->iommu_domain_swtich && (ctx->dev->dec_cnt & 1)) {
		src_vq->dev     = vcp_get_io_device_ex(VCP_IOMMU_VENC);
		mtk_v4l2_debug(4, "[%s] use VCP_IOMMU_VENC domain %p", name, src_vq->dev);
	} else {
		src_vq->dev     = vcp_get_io_device_ex(VCP_IOMMU_VDEC);
		mtk_v4l2_debug(4, "[%s] use VCP_IOMMU_VDEC domain %p", name, src_vq->dev);
	}
#if IS_ENABLED(CONFIG_VIDEO_MEDIATEK_VCU)
	if (!src_vq->dev) {
		src_vq->dev     = ctx->dev->smmu_dev;
		mtk_v4l2_debug(4, "[%s] vcp_get_io_device NULL use smmu_dev domain %p", name, src_vq->dev);
	}
#endif
#else
	src_vq->dev             = ctx->dev->smmu_dev;
#endif
	src_vq->allow_zero_bytesused = 1;

#if IS_ENABLED(CONFIG_MTK_VCODEC_DEBUG) // only support eng & userdebug
	ret = vb2_queue_init_name(src_vq, name);
#else
	ret = vb2_queue_init(src_vq);
#endif
	if (ret) {
		mtk_v4l2_err("Failed to initialize videobuf2 queue(%s)", name);
		return ret;
	}

	SNPRINTF(name, sizeof(name), "mtk_vdec-%d-cap", ctx->id);
	dst_vq->type            = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	dst_vq->io_modes        = VB2_DMABUF | VB2_MMAP;
	dst_vq->drv_priv        = ctx;
	dst_vq->buf_struct_size = sizeof(struct mtk_video_dec_buf);
	dst_vq->ops             = &mtk_vdec_vb2_ops;
	dst_vq->mem_ops         = &vdec_dma_contig_memops;
	mtk_v4l2_debug(4, "[%s] dst_vq use vdec_dma_contig_memops", name);
#if (!(IS_ENABLED(CONFIG_DEVICE_MODULES_ARM_SMMU_V3)))
	if (ctx->dec_params.svp_mode && is_disable_map_sec() && mtk_vdec_is_vcu()) {
		dst_vq->mem_ops = &vdec_sec_dma_contig_memops;
		mtk_v4l2_debug(4, "dst_vq use vdec_sec_dma_contig_memops");
	}
#endif
	dst_vq->bidirectional   = 1;

	dst_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	dst_vq->lock            = &ctx->q_mutex;
	dst_vq->dev             = ctx->dev->smmu_dev;
	dst_vq->allow_zero_bytesused = 1;

#if IS_ENABLED(CONFIG_MTK_VCODEC_DEBUG) // only support eng & userdebug
	ret = vb2_queue_init_name(dst_vq, name);
#else
	ret = vb2_queue_init(dst_vq);
#endif
	if (ret) {
		vb2_queue_release(src_vq);
		mtk_v4l2_err("Failed to initialize videobuf2 queue(%s)", name);
	}

	return ret;
}

MODULE_IMPORT_NS(DMA_BUF);
MODULE_LICENSE("GPL v2");

