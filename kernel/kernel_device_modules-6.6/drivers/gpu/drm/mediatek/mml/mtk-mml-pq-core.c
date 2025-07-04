// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2021 MediaTek Inc.
 */

#include <linux/err.h>
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>

#include "mtk-mml-pq.h"
#include "mtk-mml-pq-core.h"

#undef pr_fmt
#define pr_fmt(fmt) "[mml_pq_core]" fmt

int mml_pq_msg;
module_param(mml_pq_msg, int, 0644);

int mml_pq_dump;
module_param(mml_pq_dump, int, 0644);

int mml_pq_trace;
module_param(mml_pq_trace, int, 0644);

int mml_pq_rb_msg;
module_param(mml_pq_rb_msg, int, 0644);

int mml_pq_set_msg;
module_param(mml_pq_set_msg, int, 0644);

int mml_pq_debug_mode;
module_param(mml_pq_debug_mode, int, 0644);

int mml_pq_buf_num;
module_param(mml_pq_buf_num, int, 0644);

int mml_pq_ir_log;
module_param(mml_pq_ir_log, int, 0644);

struct mml_pq_chan {
	struct wait_queue_head msg_wq;
	atomic_t msg_cnt;
	struct list_head msg_list;
	struct mutex msg_lock;

	struct list_head job_list;
	struct mutex job_lock;
	u64 job_idx;
};

struct mml_pq_mbox {
	struct mml_pq_chan tile_init_chan;
	struct mml_pq_chan comp_config_chan;
	struct mml_pq_chan aal_readback_chan;
	struct mml_pq_chan hdr_readback_chan;
	struct mml_pq_chan wrot_callback_chan;
	struct mml_pq_chan clarity_readback_chan;
	struct mml_pq_chan dc_readback_chan;
};


#define FG_PER_FRAME_NEED (4)
#define RB_PER_FRAME_NEED (3)
#define FRAME_TOLERANCE (5)

#define DMA_BUF_LIMIT (FG_PER_FRAME_NEED * FRAME_TOLERANCE)
#define RB_BUF_LIMIT (RB_PER_FRAME_NEED * FRAME_TOLERANCE)



struct mml_pq_dev_data {
	struct mutex aal_hist_mutex;
	atomic_t aal_hist_done;
	atomic_t aal_hist_read_cnt;
	u32 aal_cnt;

	struct mutex hdr_hist_mutex;
	atomic_t hdr_hist_done;
	atomic_t hdr_hist_read_cnt;
	u32 hdr_cnt;

	struct mutex tdshp_hist_mutex;
	atomic_t tdshp_hist_done;
	atomic_t tdshp_hist_read_cnt;
	u32 tdshp_cnt;
};

static struct mutex fg_buf_grain_mutex;
static struct mutex fg_buf_scaling_mutex;
static struct list_head fg_buf_grain_list;
static struct list_head fg_buf_scaling_list;
static u32 dma_buf_num;

static struct mml_pq_mbox *pq_mbox;
static struct mutex rb_buf_list_mutex;
static struct list_head rb_buf_list;
static u32 buffer_num;
static u32 rb_buf_pool[TOTAL_RB_BUF_NUM];
static struct mutex rb_buf_pool_mutex;
static struct mml_pq_dev_data *dev_data[MML_MAX_OUTPUTS];

#define MML_CLARITY_RB_ENG_NUM (2)

int check_pq_buf_limit(u32 buffer_num,int default_buf_limit)
{
	int exceed_flag = 0;
	int buf_limit;

	if (mml_pq_debug_mode & MML_PQ_BUFFER_CHECK)
		buf_limit = mml_pq_buf_num;
	else
		buf_limit = default_buf_limit;

	if (buffer_num > buf_limit) {
		mml_pq_err("%s buf_num[%d] exceeds limit[%d]",
			__func__, buffer_num, buf_limit);
			exceed_flag = 1;
	}
	return exceed_flag;
}
static void init_pq_chan(struct mml_pq_chan *chan)
{
	init_waitqueue_head(&chan->msg_wq);
	INIT_LIST_HEAD(&chan->msg_list);
	mutex_init(&chan->msg_lock);

	INIT_LIST_HEAD(&chan->job_list);
	mutex_init(&chan->job_lock);
	chan->job_idx = 1;
}

static void queue_msg(struct mml_pq_chan *chan,
			struct mml_pq_sub_task *sub_task)
{
	mml_pq_trace_ex_begin("%s", __func__);
	mml_pq_msg("%s sub_task[%p] sub_task->mbox_list[%lx] chan[%p] chan->msg_list[%p]",
		__func__, sub_task, (unsigned long)&sub_task->mbox_list, chan, &chan->msg_list);

	mutex_lock(&chan->msg_lock);
	list_add_tail(&sub_task->mbox_list, &chan->msg_list);
	atomic_inc(&chan->msg_cnt);
	sub_task->job_id = chan->job_idx++;
	mutex_unlock(&chan->msg_lock);

	mml_pq_msg("%s wake up channel message queue", __func__);
	wake_up_interruptible(&chan->msg_wq);

	mml_pq_trace_ex_end();
}

static s32 dequeue_msg(struct mml_pq_chan *chan,
			struct mml_pq_sub_task **out_sub_task,
			bool err_print)
{
	struct mml_pq_sub_task *temp = NULL;

	mml_pq_trace_ex_begin("%s", __func__);
	mml_pq_msg("%s chan[%p] chan->msg_list[%p]", __func__, chan, &chan->msg_list);

	mutex_lock(&chan->msg_lock);
	temp = list_first_entry_or_null(&chan->msg_list,
		typeof(*temp), mbox_list);
	if (temp) {
		atomic_dec(&chan->msg_cnt);
		list_del(&temp->mbox_list);
	} else if (!err_print) {
		mml_pq_log("%s msg_cnt[%d]", __func__, atomic_read(&chan->msg_cnt));
		atomic_dec_if_positive(&chan->msg_cnt);
	}
	mutex_unlock(&chan->msg_lock);

	if (!temp) {
		if (err_print)
			mml_pq_err("%s temp is null", __func__);
		mml_pq_trace_ex_end();
		return -ENOENT;
	}

	if (unlikely(!atomic_read(&temp->queued))) {
		mml_pq_log("%s sub_task not queued", __func__);
		mml_pq_trace_ex_end();
		return -EFAULT;
	}

	mml_pq_msg("%s temp[%p] temp->result[%p] sub_task->job_id[%llu] chan[%p] chan_job_id[%llx]",
		__func__, temp, temp->result, temp->job_id, chan, chan->job_idx);
	mml_pq_msg("%s chan_job_id[%llu] temp->mbox_list[%p] chan->job_list[%p]",
		__func__, chan->job_idx, &temp->mbox_list, &chan->job_list);

	mutex_lock(&chan->job_lock);
	list_add_tail(&temp->mbox_list, &chan->job_list);
	mutex_unlock(&chan->job_lock);
	*out_sub_task = temp;

	mml_pq_trace_ex_end();
	return 0;
}

static s32 find_sub_task(struct mml_pq_chan *chan, u64 job_id,
			struct mml_pq_sub_task **out_sub_task)
{
	struct mml_pq_sub_task *sub_task = NULL, *tmp = NULL;
	s32 ret = 0;

	mml_pq_trace_ex_begin("%s", __func__);
	mml_pq_msg("%s chan[%p] job_id[%llx]", __func__, chan, job_id);

	mutex_lock(&chan->job_lock);
	list_for_each_entry_safe(sub_task, tmp, &chan->job_list, mbox_list) {
		mml_pq_msg("%s sub_task[%p] chan->job_list[%p] sub_job_id[%llx]",
			__func__, sub_task, &chan->job_list, sub_task->job_id);
		if (sub_task->job_id == job_id) {
			*out_sub_task = sub_task;
			mml_pq_msg("%s find sub_task:%p id:%llx",
				__func__, sub_task, job_id);
			list_del(&sub_task->mbox_list);
			break;
		}
	}
	if (!(*out_sub_task))
		ret = -ENOENT;
	mutex_unlock(&chan->job_lock);

	mml_pq_trace_ex_end();
	return ret;
}

static s32 init_sub_task(struct mml_pq_sub_task *sub_task)
{
	mutex_init(&sub_task->lock);
	init_waitqueue_head(&sub_task->wq);
	INIT_LIST_HEAD(&sub_task->mbox_list);
	sub_task->first_job = true;
	sub_task->aee_dump_done = false;
	sub_task->frame_data = kzalloc(sizeof(struct mml_pq_frame_data),
		GFP_KERNEL);
	if (unlikely(!sub_task->frame_data)) {
		mml_pq_err("%s create frame_data failed", __func__);
		return -ENOMEM;
	}
	return 0;
}

s32 mml_pq_task_create(struct mml_task *task)
{
	struct mml_pq_task *pq_task = NULL;
	s32 ret = 0;

	mml_pq_trace_ex_begin("%s pq_task kzalloc", __func__);
	pq_task = kzalloc(sizeof(*pq_task), GFP_KERNEL);
	mml_pq_trace_ex_end();
	if (unlikely(!pq_task)) {
		mml_pq_err("%s create pq_task failed", __func__);
		return -ENOMEM;
	}

	mml_pq_msg("%s jobid[%d]", __func__, task->job.jobid);

	mml_pq_trace_ex_begin("%s", __func__);

	pq_task->task = task;
	kref_init(&pq_task->ref);
	mutex_init(&pq_task->buffer_mutex);
	mutex_init(&pq_task->fg_buffer_mutex);
	mutex_init(&pq_task->tdshp_comp_lock);

	mutex_init(&pq_task->ref_lock);

	task->pq_task = pq_task;
	ret = init_sub_task(&pq_task->tile_init);
	if (ret) {
		kfree(pq_task);
		task->pq_task = NULL;
		goto exit;
	}

	ret = init_sub_task(&pq_task->comp_config);
	if (ret) {
		kfree(pq_task->tile_init.frame_data);
		kfree(pq_task);
		task->pq_task = NULL;
		goto exit;
	}

	ret = init_sub_task(&pq_task->aal_readback);
	if (ret) {
		kfree(pq_task->tile_init.frame_data);
		kfree(pq_task->comp_config.frame_data);
		kfree(pq_task);
		task->pq_task = NULL;
		goto exit;
	}

	ret = init_sub_task(&pq_task->hdr_readback);
	if (ret) {
		kfree(pq_task->tile_init.frame_data);
		kfree(pq_task->comp_config.frame_data);
		kfree(pq_task->aal_readback.frame_data);
		kfree(pq_task);
		task->pq_task = NULL;
		goto exit;
	}

	ret = init_sub_task(&pq_task->wrot_callback);
	if (ret) {
		kfree(pq_task->tile_init.frame_data);
		kfree(pq_task->comp_config.frame_data);
		kfree(pq_task->aal_readback.frame_data);
		kfree(pq_task->hdr_readback.frame_data);
		kfree(pq_task);
		task->pq_task = NULL;
		goto exit;
	}

	pq_task->aal_readback.readback_data.pipe0_hist =
		kzalloc(sizeof(u32)*(AAL_HIST_NUM + AAL_DUAL_INFO_NUM),
		GFP_KERNEL);
	pq_task->aal_readback.readback_data.pipe1_hist =
		kzalloc(sizeof(u32)*(AAL_HIST_NUM + AAL_DUAL_INFO_NUM),
		GFP_KERNEL);
	pq_task->hdr_readback.readback_data.pipe0_hist =
		kzalloc(sizeof(u32)*HDR_HIST_NUM, GFP_KERNEL);
	pq_task->hdr_readback.readback_data.pipe1_hist =
		kzalloc(sizeof(u32)*HDR_HIST_NUM, GFP_KERNEL);

	init_completion(&pq_task->hdr_curve_ready[0]);
	init_completion(&pq_task->hdr_curve_ready[1]);

exit:
	mml_pq_trace_ex_end();
	return ret;
}

static void release_tile_init_result(void *data)
{
	struct mml_pq_tile_init_result *result = data;

	mml_pq_msg("%s called", __func__);

	if (!result)
		return;
	kfree(result->rsz_regs[0]);
	if (result->rsz_param_cnt == MML_MAX_OUTPUTS)
		kfree(result->rsz_regs[1]);
	kfree(result);
}

static void release_comp_config_result(void *data)
{
	struct mml_pq_comp_config_result *result = data;

	if (!result)
		return;
	kfree(result->hdr_regs);
	kfree(result->hdr_curve);
	kfree(result->aal_param);
	kfree(result->aal_regs);
	kfree(result->aal_curve);
	kfree(result->ds_regs);
	kfree(result->color_regs);
	kfree(result->c3d_regs);
	kfree(result->c3d_lut);
	kfree(result);
}

static void release_pq_task(struct kref *ref)
{
	struct mml_pq_task *pq_task = container_of(ref, struct mml_pq_task, ref);

	mml_pq_trace_ex_begin("%s", __func__);

	release_tile_init_result(pq_task->tile_init.result);
	release_comp_config_result(pq_task->comp_config.result);

	mml_pq_msg("%s aal_hist[%p] hdr_hist[%p]",
		__func__, pq_task->aal_hist[0], pq_task->hdr_hist[0]);

	kfree(pq_task->aal_readback.readback_data.pipe0_hist);
	kfree(pq_task->aal_readback.readback_data.pipe1_hist);
	kfree(pq_task->hdr_readback.readback_data.pipe0_hist);
	kfree(pq_task->hdr_readback.readback_data.pipe1_hist);

	kfree(pq_task->tile_init.frame_data);
	kfree(pq_task->comp_config.frame_data);
	kfree(pq_task->aal_readback.frame_data);
	kfree(pq_task->hdr_readback.frame_data);
	kfree(pq_task->wrot_callback.frame_data);
	pq_task->task = NULL;

	mutex_unlock(&pq_task->ref_lock);

	kfree(pq_task);
	mml_pq_trace_ex_end();
}

void mml_pq_get_pq_task(struct mml_pq_task *pq_task)
{
	mutex_lock(&pq_task->ref_lock);
	kref_get(&pq_task->ref);
	mutex_unlock(&pq_task->ref_lock);
}

int mml_pq_put_pq_task(struct mml_pq_task *pq_task)
{
	int ret = -1;

	if (!pq_task) {
		mml_pq_log("%s pq_task[%p]", __func__, pq_task);
		return ret;
	}

	ret = kref_put_mutex(&pq_task->ref, release_pq_task, &pq_task->ref_lock);

	return ret;
}

void mml_pq_comp_config_clear(struct mml_task *task)
{
	struct mml_pq_chan *chan = &pq_mbox->comp_config_chan;
	struct mml_pq_sub_task *sub_task = NULL, *tmp = NULL;
	u64 job_id = task->pq_task->comp_config.job_id;
	struct mml_pq_task *pq_task = task->pq_task;
	bool need_put = false;

	mml_pq_log("%s task_job_id[%d] job_id[%llx] dual[%d]",
		__func__, task->job.jobid, job_id, task->config->dual);
	mutex_lock(&chan->msg_lock);
	if (atomic_read(&chan->msg_cnt)) {
		list_for_each_entry_safe(sub_task, tmp, &chan->msg_list, mbox_list) {
			mml_pq_log("%s msg sub_task[%p] msg_list[%08lx] sub_job_id[%llx]",
				__func__, sub_task, (unsigned long)&chan->msg_list,
				sub_task->job_id);
			if (sub_task->job_id == job_id) {
				list_del(&sub_task->mbox_list);
				atomic_dec_if_positive(&chan->msg_cnt);
				atomic_dec_if_positive(&sub_task->queued);
				need_put = true;
				break;
			}
		}
		mutex_unlock(&chan->msg_lock);
	} else {
		mutex_unlock(&chan->msg_lock);
		mutex_lock(&chan->job_lock);
		list_for_each_entry_safe(sub_task, tmp, &chan->job_list, mbox_list) {
			mml_pq_log("%s job sub_task[%p] job_list[%08lx] sub_job_id[%llx]",
				__func__, sub_task, (unsigned long)&chan->job_list,
				sub_task->job_id);
			if (sub_task->job_id == job_id) {
				list_del(&sub_task->mbox_list);
				atomic_dec_if_positive(&sub_task->queued);
				need_put = true;
				break;
			}
		}
		mutex_unlock(&chan->job_lock);
	}
	if (need_put)
		mml_pq_put_pq_task(pq_task);
}

static void remove_sub_task(struct mml_pq_chan *chan, struct mml_pq_sub_task *sub_task)
{
	mml_pq_trace_ex_begin("pq core %s", __func__);
	mml_pq_msg("%s chan[%p] sub_task[%p]", __func__, chan, sub_task);

	mutex_lock(&chan->job_lock);
	list_del(&sub_task->mbox_list);
	mutex_unlock(&chan->job_lock);

	mml_pq_trace_ex_end();
}

void mml_pq_get_vcp_buf_offset(struct mml_task *task, u32 engine,
			       struct mml_pq_readback_buffer *hist)
{
	s32 engine_start = engine*MAX_ENG_RB_BUF;

	mml_pq_rb_msg("%s get offset job_id[%d] engine_start[%d]",
				__func__, task->job.jobid, engine_start);
	mutex_lock(&rb_buf_pool_mutex);
	while (engine_start < (engine+1)*MAX_ENG_RB_BUF) {
		if (rb_buf_pool[engine_start] != INVALID_OFFSET_ADDR) {
			hist->va_offset = rb_buf_pool[engine_start];
			rb_buf_pool[engine_start] = INVALID_OFFSET_ADDR;
			 mml_pq_rb_msg("%s get offset job_id[%d]engine[%d]offset[%d]eng_start[%d]",
				__func__, task->job.jobid, engine, hist->va_offset, engine_start);
			break;
		}
		engine_start++;
	}
	mutex_unlock(&rb_buf_pool_mutex);

	mml_pq_rb_msg("%s all end job_id[%d] engine[%d] hist_va[%p] hist_pa[%pad]",
		__func__, task->job.jobid, engine, hist->va, &hist->pa);
}

void mml_pq_put_vcp_buf_offset(struct mml_task *task, u32 engine,
			       struct mml_pq_readback_buffer *hist)
{
	s32 engine_start = engine*MAX_ENG_RB_BUF;

	mml_pq_rb_msg("%s start job_id[%d] hist_va[%p] engine_start[%08x] offset[%d]",
		__func__, task->job.jobid, hist->va, engine_start, hist->va_offset);

	mutex_lock(&rb_buf_pool_mutex);
	while (engine_start < (engine+1)*MAX_ENG_RB_BUF) {
		if (rb_buf_pool[engine_start] == INVALID_OFFSET_ADDR) {
			rb_buf_pool[engine_start] = hist->va_offset;
			hist->va_offset = INVALID_OFFSET_ADDR;
			break;
		}
		engine_start++;
	}
	mutex_unlock(&rb_buf_pool_mutex);
}

void mml_pq_get_readback_buffer(struct mml_task *task, u8 pipe,
				 struct mml_pq_readback_buffer **hist)
{
	struct cmdq_client *clt = task->config->path[pipe]->clt;
	struct mml_pq_readback_buffer *temp_buffer = NULL;
	int is_buf_limit_exceed = 0;
	mml_pq_msg("%s job_id[%d]", __func__, task->job.jobid);

	mutex_lock(&rb_buf_list_mutex);
	temp_buffer = list_first_entry_or_null(&rb_buf_list,
		typeof(*temp_buffer), buffer_list);
	if (temp_buffer) {
		*hist = temp_buffer;
		list_del(&temp_buffer->buffer_list);
		mml_pq_rb_msg("%s aal get buffer from list jobid[%d] va[%p] pa[%pad]",
			__func__, task->job.jobid, temp_buffer->va, &temp_buffer->pa);
	} else {
		temp_buffer = kzalloc(sizeof(struct mml_pq_readback_buffer),
			GFP_KERNEL);

		if (unlikely(!temp_buffer)) {
			mml_pq_err("%s create buffer failed", __func__);
			mutex_unlock(&rb_buf_list_mutex);
			return;
		}
		INIT_LIST_HEAD(&temp_buffer->buffer_list);
		buffer_num++;
		is_buf_limit_exceed = check_pq_buf_limit(buffer_num, RB_BUF_LIMIT);
		if (is_buf_limit_exceed)
			mml_pq_err("%s buffer pool size exceed", __func__);
		*hist = temp_buffer;
		mml_pq_rb_msg("%s aal reallocate jobid[%d] va[%p] pa[%pad]", __func__,
			task->job.jobid, temp_buffer->va, &temp_buffer->pa);
	}
	mutex_unlock(&rb_buf_list_mutex);

	if (!temp_buffer->va && !temp_buffer->pa) {
		mutex_lock(&task->pq_task->buffer_mutex);
		temp_buffer->va = (u32 *)cmdq_mbox_buf_alloc(clt, &temp_buffer->pa);
		mutex_unlock(&task->pq_task->buffer_mutex);
	}
	mml_pq_rb_msg("%s job_id[%d] va[%p] pa[%pad] buffer_num[%d]", __func__,
		task->job.jobid, temp_buffer->va, &temp_buffer->pa, buffer_num);
}

void mml_pq_put_readback_buffer(struct mml_task *task, u8 pipe,
				struct mml_pq_readback_buffer **hist)
{

	struct cmdq_client *clt = task->config->path[pipe]->clt;
	int is_buf_limit_exceed = 0;

	if (!(*hist)) {
		mml_pq_err("%s buffer hist is null jobid[%d]", __func__, task->job.jobid);
		return;
	}

	mml_pq_rb_msg("%s all end job_id[%d] hist_va[%p] hist_pa[%pad]",
		__func__, task->job.jobid, (*hist)->va, &(*hist)->pa);
	mutex_lock(&rb_buf_list_mutex);

	is_buf_limit_exceed = check_pq_buf_limit(buffer_num, RB_BUF_LIMIT);
	if (is_buf_limit_exceed) {
		cmdq_mbox_buf_free(clt, (*hist)->va, (*hist)->pa);
		kfree(*hist);
		buffer_num--;
	} else {
		list_add_tail(&((*hist)->buffer_list), &rb_buf_list);
	}
	mutex_unlock(&rb_buf_list_mutex);
	*hist = NULL;
}

void get_dma_buffer(struct mml_task *task, u8 pipe,
	struct device *dev, struct mml_pq_dma_buffer **buf, u32 size)
{
	struct mml_pq_dma_buffer *temp_buffer = NULL;
	struct mutex *list_lock = NULL;
	int is_buf_limit_exceed = 0;
	mml_pq_msg("%s job_id[%d]", __func__, task->job.jobid);

	/* set mutex & buf_list */
	if (size == FG_BUF_SCALING_SIZE) {
		list_lock = &fg_buf_scaling_mutex;
		mutex_lock(list_lock);
		temp_buffer = list_first_entry_or_null(&fg_buf_scaling_list,
			typeof(*temp_buffer), buffer_list);
	} else if (size == FG_BUF_GRAIN_SIZE) {
		list_lock = &fg_buf_grain_mutex;
		mutex_lock(list_lock);
		temp_buffer = list_first_entry_or_null(&fg_buf_grain_list,
			typeof(*temp_buffer), buffer_list);
	}

	if (temp_buffer) {
		*buf = temp_buffer;
		list_del(&temp_buffer->buffer_list);
		mml_pq_msg("%s get buffer from list jobid[%d] va[%p] pa[%pad]",
			__func__, task->job.jobid, temp_buffer->va, &temp_buffer->pa);
	} else {
		temp_buffer = kzalloc(sizeof(struct mml_pq_dma_buffer), GFP_KERNEL);

		if (unlikely(!temp_buffer)) {
			mml_pq_err("%s create buffer failed", __func__);
			if (list_lock)
				mutex_unlock(list_lock);
			return;
		}
		INIT_LIST_HEAD(&temp_buffer->buffer_list);
		dma_buf_num++;
		is_buf_limit_exceed = check_pq_buf_limit(dma_buf_num, DMA_BUF_LIMIT);
		if (is_buf_limit_exceed)
			mml_pq_err("%s buffer pool size exceed", __func__);
		*buf = temp_buffer;
	}

	if (list_lock)
		mutex_unlock(list_lock);

	if (!temp_buffer->va && !temp_buffer->pa) {
		if (size == FG_BUF_SCALING_SIZE || size == FG_BUF_GRAIN_SIZE) {
			mutex_lock(&task->pq_task->fg_buffer_mutex);
			temp_buffer->va = dma_alloc_noncoherent(
				dev, size, &temp_buffer->pa, DMA_TO_DEVICE, GFP_KERNEL);
			mutex_unlock(&task->pq_task->fg_buffer_mutex);
		}
	}

	mml_pq_msg("%s job_id[%d] va[%p] pa[%pad] dma_buf_num[%d] size[%u]",
		__func__, task->job.jobid, temp_buffer->va, &temp_buffer->pa,
		dma_buf_num, size);
}

void put_dma_buffer(struct mml_task *task, u8 pipe,
	struct device *dev, struct mml_pq_dma_buffer **buf, u32 size)
{
	struct mutex *list_lock = NULL;
	int is_buf_limit_exceed = 0;

	if (!(*buf)) {
		mml_pq_err("%s buffer buf is null jobid[%d]", __func__, task->job.jobid);
		return;
	}

	mml_pq_msg("%s end job_id[%d] buf_va[%p] buf_pa[%pad] size[%u]",
		__func__, task->job.jobid, (*buf)->va, &(*buf)->pa, size);

	if (size == FG_BUF_SCALING_SIZE)
		list_lock = &fg_buf_scaling_mutex;
	else if (size == FG_BUF_GRAIN_SIZE)
		list_lock = &fg_buf_grain_mutex;

	if (list_lock)
		mutex_lock(list_lock);

	is_buf_limit_exceed = check_pq_buf_limit(dma_buf_num, DMA_BUF_LIMIT);
	if (is_buf_limit_exceed) {
		dma_free_noncoherent(dev, size, (*buf)->va, (*buf)->pa, DMA_TO_DEVICE);
		kfree(*buf);
		dma_buf_num--;
	} else {
		if (size == FG_BUF_SCALING_SIZE)
			list_add_tail(&((*buf)->buffer_list), &fg_buf_scaling_list);
		else if (size == FG_BUF_GRAIN_SIZE)
			list_add_tail(&((*buf)->buffer_list), &fg_buf_grain_list);
	}

	if (list_lock)
		mutex_unlock(list_lock);
	*buf = NULL;
}

void mml_pq_get_fg_buffer(struct mml_task *task, u8 pipe, struct device *dev)
{
	/* get 3 buffer: lumn, cb, cr */
	for (int i = 0; i < FG_BUF_NUM - 1; i++)
		get_dma_buffer(task, pipe, dev, &(task->pq_task->fg_table[i]),
			FG_BUF_GRAIN_SIZE);
	/* get buffer for scaling table */
	get_dma_buffer(task, pipe, dev, &(task->pq_task->fg_table[FG_BUF_NUM-1]),
		FG_BUF_SCALING_SIZE);
}

void mml_pq_put_fg_buffer(struct mml_task *task, u8 pipe, struct device *dev)
{
	/* put 3 buffer: lumn, cb, cr */
	for (int i = 0; i < FG_BUF_NUM - 1; i++)
		put_dma_buffer(task, pipe, dev, &(task->pq_task->fg_table[i]),
			FG_BUF_GRAIN_SIZE);
	/* put buffer for scaling table */
	put_dma_buffer(task, pipe, dev, &(task->pq_task->fg_table[FG_BUF_NUM-1]),
		FG_BUF_SCALING_SIZE);
}

void mml_pq_task_release(struct mml_task *task)
{
	struct mml_pq_task *pq_task = task->pq_task;

	if (IS_ERR_OR_NULL(pq_task))
		mml_pq_err("%s jobid[%d] task[%p] pq_task[%p]", __func__,
			task->job.jobid, task, task->pq_task);

	mml_pq_put_pq_task(pq_task);
	task->pq_task = NULL;
}

static struct mml_pq_task *from_tile_init(struct mml_pq_sub_task *sub_task)
{
	return container_of(sub_task, struct mml_pq_task, tile_init);
}

static struct mml_pq_task *from_comp_config(struct mml_pq_sub_task *sub_task)
{
	return container_of(sub_task, struct mml_pq_task, comp_config);
}

static struct mml_pq_task *from_aal_readback(struct mml_pq_sub_task *sub_task)
{
	return container_of(sub_task, struct mml_pq_task, aal_readback);
}

static struct mml_pq_task *from_hdr_readback(struct mml_pq_sub_task *sub_task)
{
	return container_of(sub_task,
		struct mml_pq_task, hdr_readback);
}

static struct mml_pq_task *from_wrot_callback(struct mml_pq_sub_task *sub_task)
{
	return container_of(sub_task,
		struct mml_pq_task, wrot_callback);
}

static struct mml_pq_task *from_clarity_readback(struct mml_pq_sub_task *sub_task)
{
	return container_of(sub_task,
		struct mml_pq_task, clarity_readback);
}

static struct mml_pq_task *from_dc_readback(struct mml_pq_sub_task *sub_task)
{
	return container_of(sub_task,
		struct mml_pq_task, dc_readback);
}

static void dump_sub_task(struct mml_pq_sub_task *sub_task, int new_job_id,
	bool is_dual, u32 cut_pos_x, u32 readback_size)
{
	if (!sub_task)
		return;

	mml_pq_set_msg("%s new_job_id[%d]", __func__, new_job_id);

	mml_pq_set_msg("%s src.width[%d] src.secure[%d]",
		__func__,
		sub_task->frame_data->info.src.width,
		sub_task->frame_data->info.src.secure);

	mml_pq_set_msg("%s out0 width[%d] height[%d], out1 width[%d] height[%d]",
		__func__,
		sub_task->frame_data->frame_out[0].width,
		sub_task->frame_data->frame_out[0].height,
		sub_task->frame_data->frame_out[1].width,
		sub_task->frame_data->frame_out[1].height);

	mml_pq_set_msg("%s pq_param0 enable[%d] user_info[%d]",
		__func__, sub_task->frame_data->pq_param[0].enable,
		sub_task->frame_data->pq_param[0].user_info);

	mml_pq_set_msg("%s pq_param1 enable[%d] user_info[%d]",
		__func__, sub_task->frame_data->pq_param[1].enable,
		sub_task->frame_data->pq_param[1].user_info);

	if (readback_size)
		mml_pq_set_msg("%s pipe0_hist[0][%u] pipe0_hist[%d][%u]",
			__func__,
			sub_task->readback_data.pipe0_hist[0],
			readback_size-1,
			sub_task->readback_data.pipe0_hist[readback_size-1]);

	mml_pq_set_msg("%s cut_pos_x:%d is_dual:%d",
		__func__, cut_pos_x, is_dual);

	if (readback_size && is_dual)
		mml_pq_set_msg("%s pipe1_hist[0][%u] pipe1_hist[%d][%u]",
			__func__,
			sub_task->readback_data.pipe1_hist[0],
			readback_size-1,
			sub_task->readback_data.pipe1_hist[readback_size-1]);

}

static void dump_pq_param(struct mml_pq_param *pq_param)
{
	if (!pq_param)
		return;

	mml_pq_dump(" enable=%u", pq_param->enable);
	mml_pq_dump(" time_stamp=%u", pq_param->time_stamp);
	mml_pq_dump(" scenario=%u", pq_param->scenario);
	mml_pq_dump(" layer_id=%u disp_id=%d",
		pq_param->layer_id, pq_param->disp_id);
	mml_pq_dump(" src_gamut=%u dst_gamut=%u",
		pq_param->src_gamut, pq_param->dst_gamut);
	mml_pq_dump(" video_mode=%u", pq_param->src_hdr_video_mode);
	mml_pq_dump(" user=%u", pq_param->user_info);
}

static void mml_pq_check_dup_node(struct mml_pq_chan *chan, struct mml_pq_sub_task *sub_task)
{
	struct mml_pq_sub_task *tmp_sub_task = NULL, *tmp = NULL;
	u64 job_id = sub_task->job_id;

	mutex_lock(&chan->msg_lock);
	if (!list_empty(&chan->msg_list)) {
		list_for_each_entry_safe(tmp_sub_task, tmp, &chan->msg_list, mbox_list) {
			mml_pq_msg("%s sub_task[%p] chan->job_list[%p] sub_job_id[%llx]",
				__func__, tmp_sub_task, &chan->msg_list, tmp_sub_task->job_id);
			if (tmp_sub_task->job_id == job_id) {
				mml_pq_log("%s find sub_task:%p id:%llx",
					__func__, tmp_sub_task, job_id);
				list_del(&tmp_sub_task->mbox_list);
				atomic_dec_if_positive(&chan->msg_cnt);
				break;
			}
		}
	}
	mutex_unlock(&chan->msg_lock);

	mutex_lock(&chan->job_lock);
	if (!list_empty(&chan->job_list)) {
		list_for_each_entry_safe(tmp_sub_task, tmp, &chan->job_list, mbox_list) {
			mml_pq_msg("%s sub_task[%p] chan->job_list[%lx] sub_job_id[%llx]",
				__func__, tmp_sub_task, (unsigned long)&chan->job_list, tmp_sub_task->job_id);
			if (tmp_sub_task->job_id == job_id) {
				mml_pq_log("%s find sub_task:%p id:%llx",
					__func__, tmp_sub_task, job_id);
				list_del(&tmp_sub_task->mbox_list);
				break;
			}
		}
	}
	mutex_unlock(&chan->job_lock);

}

static int set_sub_task(struct mml_task *task,
			struct mml_pq_sub_task *sub_task,
			struct mml_pq_chan *chan,
			struct mml_pq_param *pq_param,
			bool is_dup_check)
{
	struct mml_pq_task *pq_task = task->pq_task;
	u64 random_num = 0;
	u32 i = 0;

	mml_pq_msg("%s called queued[%d] result_ref[%d] job_id[%llu, %d] first_job[%d]",
		__func__, atomic_read(&sub_task->queued),
		atomic_read(&sub_task->result_ref),
		sub_task->job_id, task->job.jobid,
		sub_task->first_job);

	if ((mml_pq_debug_mode & MML_PQ_STABILITY_TEST) &&
		task->config->dual) {
		get_random_bytes(&random_num, sizeof(u64));
		mdelay((random_num % 50)+1);
	}


	mutex_lock(&sub_task->lock);

	if (sub_task->mml_task_jobid != task->job.jobid || sub_task->first_job) {
		sub_task->mml_task_jobid = task->job.jobid;
		sub_task->first_job = false;
	} else {
		mutex_unlock(&sub_task->lock);
		mml_pq_msg("%s already queue queued[%d] job_id[%llu, %d]",
			__func__, atomic_read(&sub_task->queued),
			sub_task->job_id, task->job.jobid);

		return 0;
	}

	if (!atomic_fetch_add_unless(&sub_task->queued, 1, 1)) {
		//WARN_ON(atomic_read(&sub_task->result_ref));
		if (atomic_read(&sub_task->result_ref) && !sub_task->aee_dump_done) {
			mml_pq_log("%s ref[%d] job_id[%llu, %d] mode[%d] dual[%d]",
				__func__, atomic_read(&sub_task->result_ref),
				sub_task->job_id, task->job.jobid,
				task->config->info.mode,
				task->config->dual);
			mml_pq_util_aee("MMLPQ ref is not zero",
				"jobid:%d",
				task->job.jobid);
			sub_task->aee_dump_done = true;
		}
		atomic_set(&sub_task->result_ref, 0);
		mml_pq_get_pq_task(pq_task);
		sub_task->frame_data->info = task->config->info;
		for (i = 0; i < MML_MAX_OUTPUTS; i++)
			sub_task->frame_data->size_info.out_rotate[i] =
				task->config->out_rotate[i];
		memcpy(&sub_task->frame_data->pq_param, task->pq_param,
			MML_MAX_OUTPUTS * sizeof(struct mml_pq_param));
		memcpy(&sub_task->frame_data->frame_out, &task->config->frame_out,
			MML_MAX_OUTPUTS * sizeof(struct mml_frame_size));
		memcpy(&sub_task->frame_data->size_info.frame_in_crop_s[0],
			&task->config->frame_in_crop[0],
			MML_MAX_OUTPUTS *sizeof(struct mml_crop));

		sub_task->frame_data->size_info.crop_size_s.width =
			task->config->frame_tile_sz.width;
		sub_task->frame_data->size_info.crop_size_s.height=
			task->config->frame_tile_sz.height;
		sub_task->frame_data->size_info.frame_size_s.width = task->config->frame_in.width;
		sub_task->frame_data->size_info.frame_size_s.height = task->config->frame_in.height;

		sub_task->readback_data.is_dual = task->config->dual;

		mml_pq_msg("%s out_rotate[%d %d]", __func__,
			sub_task->frame_data->size_info.out_rotate[0],
			sub_task->frame_data->size_info.out_rotate[1]);
		mml_pq_msg("%s called, job_id[%d] sub_task[%p] mode[%d] format[%d]", __func__,
			task->job.jobid, &pq_task->comp_config,
			task->config->info.mode, task->config->info.src.format);
		mutex_unlock(&sub_task->lock);

		if (is_dup_check)
			mml_pq_check_dup_node(chan, sub_task);
		queue_msg(chan, sub_task);
		dump_pq_param(pq_param);
	} else
		mutex_unlock(&sub_task->lock);

	mml_pq_msg("%s end queued[%d] result_ref[%d] job_id[%llx, %d] first_job[%d]",
		__func__, atomic_read(&sub_task->queued),
		atomic_read(&sub_task->result_ref),
		sub_task->job_id, task->job.jobid,
		sub_task->first_job);
	return 0;
}

#if !IS_ENABLED(CONFIG_MTK_MML_LEGACY)
static int set_readback_sub_task(struct mml_pq_task *pq_task,
			struct mml_pq_sub_task *sub_task,
			struct mml_pq_chan *chan,
			struct mml_pq_frame_data frame_data,
			bool dual, u32 mml_jobid,
			bool is_dup_check)
{
	u64 random_num = 0;

	mml_pq_msg("%s called queued[%d] result_ref[%d] job_id[%llu, %d] first_job[%d]",
		__func__, atomic_read(&sub_task->queued),
		atomic_read(&sub_task->result_ref),
		sub_task->job_id, mml_jobid,
		sub_task->first_job);

	if ((mml_pq_debug_mode & MML_PQ_STABILITY_TEST) &&
		dual) {
		get_random_bytes(&random_num, sizeof(u64));
		mdelay((random_num % 50)+1);
	}

	mutex_lock(&sub_task->lock);
	if (sub_task->mml_task_jobid != mml_jobid || sub_task->first_job) {
		sub_task->mml_task_jobid = mml_jobid;
		sub_task->first_job = false;
	} else {
		mutex_unlock(&sub_task->lock);
		mml_pq_msg("%s already queue queued[%d] job_id[%llu, %d]",
			__func__, atomic_read(&sub_task->queued),
			sub_task->job_id, mml_jobid);

		return 0;
	}

	if (!atomic_fetch_add_unless(&sub_task->queued, 1, 1)) {
		//WARN_ON(atomic_read(&sub_task->result_ref));
		if (atomic_read(&sub_task->result_ref) && !sub_task->aee_dump_done) {
			mml_pq_log("%s ref[%d] job_id[%llu, %d] mode[%d] dual[%d]",
				__func__, atomic_read(&sub_task->result_ref),
				sub_task->job_id, mml_jobid,
				frame_data.info.mode, dual);
			mml_pq_util_aee("MMLPQ ref is not zero",
				"jobid:%d", mml_jobid);
			sub_task->aee_dump_done = true;
		}
		atomic_set(&sub_task->result_ref, 0);
		mml_pq_get_pq_task(pq_task);

		memcpy(&sub_task->frame_data->pq_param, frame_data.pq_param,
			MML_MAX_OUTPUTS * sizeof(struct mml_pq_param));
		memcpy(&sub_task->frame_data->info, &frame_data.info,
			sizeof(struct mml_frame_info));
		memcpy(&sub_task->frame_data->frame_out, &frame_data.frame_out,
			MML_MAX_OUTPUTS * sizeof(struct mml_frame_size));
		memcpy(&sub_task->frame_data->size_info, &frame_data.size_info,
			sizeof(struct mml_pq_frame_info));
		sub_task->readback_data.is_dual = dual;

		mml_pq_msg("%s called, job_id[%d] sub_task[%p] mode[%d] format[%d]", __func__,
			mml_jobid, &pq_task->comp_config,
			frame_data.info.mode, frame_data.info.src.format);
		mutex_unlock(&sub_task->lock);

		if (is_dup_check)
			mml_pq_check_dup_node(chan, sub_task);
		queue_msg(chan, sub_task);
		dump_pq_param(&(frame_data.pq_param[0]));
	} else
		mutex_unlock(&sub_task->lock);

	mml_pq_msg("%s end queued[%d] result_ref[%d] job_id[%llx, %d] first_job[%d]",
		__func__, atomic_read(&sub_task->queued),
		atomic_read(&sub_task->result_ref),
		sub_task->job_id, mml_jobid,
		sub_task->first_job);
	return 0;
}
#endif

static int get_sub_task_result(struct mml_pq_task *pq_task,
			       struct mml_pq_sub_task *sub_task, u32 timeout_ms,
			       void (*dump_func)(void *data))
{
	s32 ret;
	u64 random_num = 0;

	if (unlikely(!atomic_read(&sub_task->queued)))
		mml_pq_msg("%s already handled or not queued", __func__);

	mml_pq_msg("begin wait for result");
	ret = wait_event_timeout(sub_task->wq,
				(!atomic_read(&sub_task->queued)) && (sub_task->result),
				msecs_to_jiffies(timeout_ms));

	if (mml_pq_debug_mode & MML_PQ_STABILITY_TEST) {
		get_random_bytes(&random_num, sizeof(u64));
		mdelay((random_num % 50)+1);
	}
	if (mml_pq_debug_mode & MML_PQ_TIMEOUT_TEST) {
		mdelay(51);
		ret = 0;
	}

	mml_pq_msg("wait for result: %d, mml_pq_debug_mode: %d",
		ret, mml_pq_debug_mode);
	mutex_lock(&sub_task->lock);	/* To wait unlock of handle thread */
	atomic_inc(&sub_task->result_ref);
	mutex_unlock(&sub_task->lock);
	if (ret && dump_func)
		dump_func(sub_task->result);

	if (ret)
		return 0;
	else
		return -EBUSY;
}

static void put_sub_task_result(struct mml_pq_sub_task *sub_task, struct mml_pq_chan *chan)
{
	mml_pq_msg("%s result_ref[%d] queued[%d] msg_cnt[%d]",
		__func__, atomic_read(&sub_task->result_ref),
		atomic_read(&sub_task->queued),
		atomic_read(&chan->msg_cnt));

	if (!atomic_dec_if_positive(&sub_task->result_ref))
		if (!atomic_dec_if_positive(&sub_task->queued))
			atomic_dec_if_positive(&chan->msg_cnt);

}

static void dump_tile_init(void *data)
{
	u32 i;
	struct mml_pq_tile_init_result *result = data;

	if (!result)
		return;

	mml_pq_dump("[tile][param] count=%u", result->rsz_param_cnt);
	for (i = 0; i < result->rsz_param_cnt && i < MML_MAX_OUTPUTS; i++) {
		mml_pq_dump("[tile][%u] coeff_step_x=%u", i,
				result->rsz_param[i].coeff_step_x);
		mml_pq_dump("[tile][%u] coeff_step_y=%u", i,
				result->rsz_param[i].coeff_step_y);
		mml_pq_dump("[tile][%u] precision_x=%u", i,
				result->rsz_param[i].precision_x);
		mml_pq_dump("[tile][%u] precision_y=%u", i,
				result->rsz_param[i].precision_y);
		mml_pq_dump("[tile][%u] crop_offset_x=%u", i,
				result->rsz_param[i].crop_offset_x);
		mml_pq_dump("[tile][%u] crop_subpix_x=%u", i,
				result->rsz_param[i].crop_subpix_x);
		mml_pq_dump("[tile][%u] crop_offset_y=%u", i,
				result->rsz_param[i].crop_offset_y);
		mml_pq_dump("[tile][%u] crop_subpix_y=%u", i,
				result->rsz_param[i].crop_subpix_y);
		mml_pq_dump("[tile][%u] hor_dir_scale=%u", i,
				result->rsz_param[i].hor_dir_scale);
		mml_pq_dump("[tile][%u] hor_algorithm=%u", i,
				result->rsz_param[i].hor_algorithm);
		mml_pq_dump("[tile][%u] ver_dir_scale=%u", i,
				result->rsz_param[i].ver_dir_scale);
		mml_pq_dump("[tile][%u] ver_algorithm=%u", i,
				result->rsz_param[i].ver_algorithm);
		mml_pq_dump("[tile][%u] vertical_first=%u", i,
				result->rsz_param[i].vertical_first);
		mml_pq_dump("[tile][%u] ver_cubic_trunc=%u", i,
				result->rsz_param[i].ver_cubic_trunc);
	}
	mml_pq_dump("[tile] count=%lx", (unsigned long)result->rsz_reg_cnt);
}

int mml_pq_set_tile_init(struct mml_task *task)
{
	struct mml_pq_task *pq_task = task->pq_task;
	struct mml_pq_chan *chan = &pq_mbox->tile_init_chan;
	int ret = 0;

	mml_pq_trace_ex_begin("%s", __func__);
	mml_pq_msg("%s called, job_id[%d]", __func__, task->job.jobid);
	ret = set_sub_task(task, &pq_task->tile_init, chan,
			   &task->pq_param[0], false);
	mml_pq_trace_ex_end();
	return ret;
}

int mml_pq_get_tile_init_result(struct mml_task *task, u32 timeout_ms)
{
	struct mml_pq_task *pq_task = task->pq_task;
	s32 ret = 0;

	mml_pq_trace_ex_begin("%s", __func__);
	mml_pq_msg("%s called, %d job_id[%d]",
		__func__, timeout_ms, task->job.jobid);
	ret = get_sub_task_result(pq_task, &pq_task->tile_init, timeout_ms,
				  dump_tile_init);
	mml_pq_trace_ex_end();
	return ret;
}

void mml_pq_put_tile_init_result(struct mml_task *task)
{
	struct mml_pq_chan *chan = &pq_mbox->tile_init_chan;

	mml_pq_msg("%s called job_id[%d]\n", __func__,
		task->job.jobid);

	put_sub_task_result(&task->pq_task->tile_init, chan);
}

static void dump_comp_config(void *data)
{
	u32 i;
	struct mml_pq_comp_config_result *result = data;

	if (!result)
		return;

	mml_pq_dump("[comp] count=%u", result->param_cnt);
	for (i = 0; i < result->param_cnt && i < MML_MAX_OUTPUTS; i++) {
		mml_pq_dump("[comp][%u] dre_blk_width=%u", i,
			result->aal_param[i].dre_blk_width);
		mml_pq_dump("[comp][%u] dre_blk_height=%u", i,
			result->aal_param[i].dre_blk_height);
	}
	mml_pq_dump("[comp] count=%u", result->aal_reg_cnt);
	for (i = 0; i < AAL_CURVE_NUM; i += 8)
		mml_pq_dump("[curve][%d - %d] = [%08x, %08x, %08x, %08x, %08x, %08x, %08x, %08x]",
			i, i+7,
			result->aal_curve[i], result->aal_curve[i+1],
			result->aal_curve[i+2], result->aal_curve[i+3],
			result->aal_curve[i+4], result->aal_curve[i+5],
			result->aal_curve[i+6], result->aal_curve[i+7]);
}

int mml_pq_set_comp_config(struct mml_task *task)
{
	struct mml_pq_task *pq_task = task->pq_task;
	struct mml_pq_chan *chan = &pq_mbox->comp_config_chan;
	int ret = 0;

	mml_pq_trace_ex_begin("%s", __func__);
	mml_pq_msg("%s called, job_id[%d] sub_task[%p] mode[%d] format[%d]", __func__,
		task->job.jobid, &pq_task->comp_config,
		task->config->info.mode, task->config->info.src.format);
	ret = set_sub_task(task, &pq_task->comp_config, chan,
			   &task->pq_param[0], false);
	mml_pq_trace_ex_end();
	return ret;
}

static bool set_hist(struct mml_pq_sub_task *sub_task,
		     bool dual, u8 pipe, u32 *phist, u32 arr_idx,
		     u32 size, u32 rb_eng_cnt)
{
	bool ready;

	mutex_lock(&sub_task->lock);
	if (dual && pipe)
		memcpy(&(sub_task->readback_data.pipe1_hist[arr_idx]), phist,
			sizeof(u32)*size);
	else
		memcpy(&(sub_task->readback_data.pipe0_hist[arr_idx]), phist,
			sizeof(u32)*size);


	atomic_inc(&sub_task->readback_data.pipe_cnt);
	ready =
		(dual && atomic_read(&sub_task->readback_data.pipe_cnt) ==
		rb_eng_cnt*MML_PIPE_CNT) ||
		(!dual && atomic_read(&sub_task->readback_data.pipe_cnt) ==
		rb_eng_cnt);

	mutex_unlock(&sub_task->lock);
	return ready;
}

static bool check_hist_ready(bool dual, u32 rb_eng_thread_cnt, u32 *count)
{
	bool ready = false;

	*count = *count + 1;

	ready =
		(dual && *count == rb_eng_thread_cnt*MML_PIPE_CNT) ||
		(!dual && *count == rb_eng_thread_cnt);

	return ready;
}

void mml_pq_set_aal_status(struct mml_pq_task *pq_task, u8 out_idx)
{
	mutex_lock(&dev_data[out_idx]->aal_hist_mutex);

	mml_pq_ir_log("%s pq_task[%p] hist_read_cnt[%d] hist_done[%d] status[%d] pq_ref[%d]",
		__func__, pq_task,
		atomic_read(&dev_data[out_idx]->aal_hist_read_cnt),
		atomic_read(&dev_data[out_idx]->aal_hist_done),
		pq_task->read_status.aal_comp,
		kref_read(&pq_task->ref));

	if (pq_task->read_status.aal_comp == MML_PQ_HIST_INIT) {
		if (!atomic_read(&dev_data[out_idx]->aal_hist_done) &&
			!atomic_read(&dev_data[out_idx]->aal_hist_read_cnt)) {
			pq_task->read_status.aal_comp =
				MML_PQ_HIST_IDLE;
			dev_data[out_idx]->aal_cnt = 0;
			atomic_inc(&dev_data[out_idx]->aal_hist_read_cnt);
		} else
			pq_task->read_status.aal_comp =
				MML_PQ_HIST_READING;
	}
	mutex_unlock(&dev_data[out_idx]->aal_hist_mutex);
}

bool mml_pq_aal_hist_reading(struct mml_pq_task *pq_task, u8 out_idx, u8 pipe)
{
	u32 read_value = 0;

	mutex_lock(&dev_data[out_idx]->aal_hist_mutex);

	read_value = atomic_read(&dev_data[out_idx]->aal_hist_done);
	mml_pq_ir_log("%s pq_task[%p] hist_read_cnt[%d] hist_done[%d] status[%d] pq_ref[%d]",
		__func__, pq_task,
		atomic_read(&dev_data[out_idx]->aal_hist_read_cnt),
		atomic_read(&dev_data[out_idx]->aal_hist_done),
		pq_task->read_status.aal_comp,
		kref_read(&pq_task->ref));

	if (!atomic_read(&dev_data[out_idx]->aal_hist_read_cnt)) {
		mutex_unlock(&dev_data[out_idx]->aal_hist_mutex);
		return true;
	} else if (read_value & (1 << pipe)) {
		mutex_unlock(&dev_data[out_idx]->aal_hist_mutex);
		return true;
	}

	read_value = read_value | (1 << pipe);
	atomic_set(&dev_data[out_idx]->aal_hist_done, read_value);

	mutex_unlock(&dev_data[out_idx]->aal_hist_mutex);

	return false;

}

void mml_pq_aal_flag_check(bool dual, u8 out_idx)
{

	mutex_lock(&dev_data[out_idx]->aal_hist_mutex);
	if (check_hist_ready(dual, 2, &dev_data[out_idx]->aal_cnt)) {
		atomic_set(&dev_data[out_idx]->aal_hist_done, 0);
		atomic_dec_if_positive(&dev_data[out_idx]->aal_hist_read_cnt);
	}
	mutex_unlock(&dev_data[out_idx]->aal_hist_mutex);
}

#if !IS_ENABLED(CONFIG_MTK_MML_LEGACY)
int mml_pq_ir_aal_readback(struct mml_pq_task *pq_task, struct mml_pq_frame_data frame_data,
			u8 pipe, u32 *phist, u32 mml_jobid,
			bool dual)
{
	struct mml_pq_sub_task *sub_task = &pq_task->aal_readback;
	struct mml_pq_chan *chan = &pq_mbox->aal_readback_chan;
	int ret = 0;

	mml_pq_msg("%s called job_id[%d] pipe[%d] sub_task->job_id[%llu]",
		__func__, mml_jobid, pipe, sub_task->job_id);

	if (!pipe) {
		mml_pq_rb_msg("%s job_id[%d] hist[0~4]={%08x, %08x, %08x, %08x, %08x}",
			__func__, mml_jobid, phist[0], phist[1],
			phist[2], phist[3], phist[4]);

		mml_pq_rb_msg("%s job_id[%d] hist[10~14]={%08x, %08x, %08x, %08x, %08x}",
			__func__, mml_jobid, phist[10], phist[11],
			phist[12], phist[13], phist[14]);
	} else {
		mml_pq_rb_msg("%s job_id[%d] hist[600~604]={%08x, %08x, %08x, %08x, %08x}",
			__func__, mml_jobid, phist[600], phist[601],
			phist[602], phist[603], phist[604]);

		mml_pq_rb_msg("%s job_id[%d] hist[610~614]={%08x, %08x, %08x, %08x, %08x}",
			__func__, mml_jobid, phist[610], phist[611],
			phist[612], phist[613], phist[614]);
	}

	if (set_hist(sub_task, dual, pipe, phist,
		0, (AAL_HIST_NUM+AAL_DUAL_INFO_NUM), 1))
		ret = set_readback_sub_task(pq_task, sub_task, chan,
			frame_data, dual, mml_jobid, true);

	return ret;
}
#endif

int mml_pq_dc_aal_readback(struct mml_task *task, u8 pipe, u32 *phist)
{
	struct mml_pq_task *pq_task = task->pq_task;
	struct mml_pq_sub_task *sub_task = &pq_task->aal_readback;
	struct mml_pq_chan *chan = &pq_mbox->aal_readback_chan;
	int ret = 0;

	mml_pq_trace_ex_begin("%s", __func__);
	mml_pq_msg("%s called job_id[%d] pipe[%d] sub_task->job_id[%llu]",
		__func__, task->job.jobid, pipe, sub_task->job_id);

	if (!pipe) {
		mml_pq_rb_msg("%s job_id[%d] hist[0~4]={%08x, %08x, %08x, %08x, %08x}",
			__func__, task->job.jobid, phist[0], phist[1],
			phist[2], phist[3], phist[4]);

		mml_pq_rb_msg("%s job_id[%d] hist[10~14]={%08x, %08x, %08x, %08x, %08x}",
			__func__, task->job.jobid, phist[10], phist[11],
			phist[12], phist[13], phist[14]);
	} else {
		mml_pq_rb_msg("%s job_id[%d] hist[600~604]={%08x, %08x, %08x, %08x, %08x}",
			__func__, task->job.jobid, phist[600], phist[601],
			phist[602], phist[603], phist[604]);

		mml_pq_rb_msg("%s job_id[%d] hist[610~614]={%08x, %08x, %08x, %08x, %08x}",
			__func__, task->job.jobid, phist[610], phist[611],
			phist[612], phist[613], phist[614]);
	}

	if (set_hist(sub_task, task->config->dual, pipe, phist,
		0, (AAL_HIST_NUM+AAL_DUAL_INFO_NUM), 1))
		ret = set_sub_task(task, sub_task, chan, &task->pq_param[0], true);

	mml_pq_msg("%s end", __func__);
	mml_pq_trace_ex_end();
	return ret;
}

void mml_pq_set_hdr_status(struct mml_pq_task *pq_task, u8 out_idx)
{
	mutex_lock(&dev_data[out_idx]->hdr_hist_mutex);

	mml_pq_ir_log("%s pq_task[%p] hist_read_cnt[%d] hist_done[%d] status[%d] pq_ref[%d]",
		__func__, pq_task,
		atomic_read(&dev_data[out_idx]->hdr_hist_read_cnt),
		atomic_read(&dev_data[out_idx]->hdr_hist_done),
		pq_task->read_status.hdr_comp,
		kref_read(&pq_task->ref));

	if (pq_task->read_status.hdr_comp == MML_PQ_HIST_INIT) {
		if (!atomic_read(&dev_data[out_idx]->hdr_hist_done) &&
			!atomic_read(&dev_data[out_idx]->hdr_hist_read_cnt)) {
			pq_task->read_status.hdr_comp =
				MML_PQ_HIST_IDLE;
			dev_data[out_idx]->hdr_cnt = 0;
			atomic_inc(&dev_data[out_idx]->hdr_hist_read_cnt);
		} else
			pq_task->read_status.hdr_comp =
				MML_PQ_HIST_READING;
	}
	mutex_unlock(&dev_data[out_idx]->hdr_hist_mutex);
}

bool mml_pq_hdr_hist_reading(struct mml_pq_task *pq_task, u8 out_idx, u8 pipe)
{
	u32 read_value = 0;

	mutex_lock(&dev_data[out_idx]->hdr_hist_mutex);

	read_value = atomic_read(&dev_data[out_idx]->hdr_hist_done);
	mml_pq_ir_log("%s pq_task[%p] hist_read_cnt[%d] hist_done[%d] status[%d] pq_ref[%d]",
		__func__, pq_task,
		atomic_read(&dev_data[out_idx]->hdr_hist_read_cnt),
		atomic_read(&dev_data[out_idx]->hdr_hist_done),
		pq_task->read_status.hdr_comp,
		kref_read(&pq_task->ref));

	if (!atomic_read(&dev_data[out_idx]->hdr_hist_read_cnt)) {
		mutex_unlock(&dev_data[out_idx]->hdr_hist_mutex);
		return true;
	} else if (read_value & (1 << pipe)) {
		mutex_unlock(&dev_data[out_idx]->hdr_hist_mutex);
		return true;
	}

	read_value = read_value | (1 << pipe);
	atomic_set(&dev_data[out_idx]->hdr_hist_done, read_value);

	mutex_unlock(&dev_data[out_idx]->hdr_hist_mutex);

	return false;
}

void mml_pq_hdr_flag_check(bool dual, u8 out_idx)
{
	mutex_lock(&dev_data[out_idx]->hdr_hist_mutex);
	if (check_hist_ready(dual, 1, &dev_data[out_idx]->hdr_cnt)) {
		atomic_set(&dev_data[out_idx]->hdr_hist_done, 0);
		atomic_dec_if_positive(&dev_data[out_idx]->hdr_hist_read_cnt);
	}
	mutex_unlock(&dev_data[out_idx]->hdr_hist_mutex);
}

#if !IS_ENABLED(CONFIG_MTK_MML_LEGACY)
int mml_pq_ir_hdr_readback(struct mml_pq_task *pq_task, struct mml_pq_frame_data frame_data,
			u8 pipe, u32 *phist, u32 mml_jobid,
			bool dual)
{
	struct mml_pq_sub_task *sub_task = &pq_task->hdr_readback;
	struct mml_pq_chan *chan = &pq_mbox->hdr_readback_chan;
	int ret = 0;

	mml_pq_trace_ex_begin("%s pipe[%d]", __func__, pipe);

	if (unlikely(!pq_task)) {
		mml_pq_trace_ex_end();
		return -EINVAL;
	}

	mml_pq_msg("%s called job_id[%d] pipe[%d] sub_task->job_id[%llu]",
		__func__, mml_jobid, pipe, sub_task->job_id);

	if (set_hist(sub_task, dual, pipe, phist, 0, HDR_HIST_NUM, 1))
		ret = set_readback_sub_task(pq_task, sub_task, chan,
			frame_data, dual, mml_jobid, true);

	mml_pq_msg("%s end job_id[%d]", __func__, mml_jobid);
	mml_pq_trace_ex_end();
	return ret;
}
#endif

int mml_pq_dc_hdr_readback(struct mml_task *task, u8 pipe, u32 *phist)
{
	struct mml_pq_task *pq_task = task->pq_task;
	struct mml_pq_sub_task *sub_task = &pq_task->hdr_readback;
	struct mml_pq_chan *chan = &pq_mbox->hdr_readback_chan;
	int ret = 0;

	mml_pq_msg("%s called pipe[%d]\n", __func__, pipe);
	if (unlikely(!task))
		return -EINVAL;

	dump_pq_param(&task->pq_param[0]);

	if (set_hist(sub_task, task->config->dual, pipe, phist,
		0, HDR_HIST_NUM, 1))
		ret = set_sub_task(task, sub_task, chan, &task->pq_param[0], true);

	mml_pq_msg("%s end pipe[%d] job_id[%d]\n", __func__, pipe, task->job.jobid);
	return ret;
}

void mml_pq_set_tdshp_status(struct mml_pq_task *pq_task, u8 out_idx)
{
	mutex_lock(&dev_data[out_idx]->tdshp_hist_mutex);

	mml_pq_ir_log("%s pq_task[%p] hist_read_cnt[%d] hist_done[%d] status[%d] pq_ref[%d]",
		__func__, pq_task,
		atomic_read(&dev_data[out_idx]->tdshp_hist_read_cnt),
		atomic_read(&dev_data[out_idx]->tdshp_hist_done),
		pq_task->read_status.tdshp_comp,
		kref_read(&pq_task->ref));

	if (pq_task->read_status.tdshp_comp == MML_PQ_HIST_INIT) {
		if (!atomic_read(&dev_data[out_idx]->tdshp_hist_done) &&
			!atomic_read(&dev_data[out_idx]->tdshp_hist_read_cnt)) {
			pq_task->read_status.tdshp_comp =
				MML_PQ_HIST_IDLE;
			dev_data[out_idx]->tdshp_cnt = 0;
			atomic_inc(&dev_data[out_idx]->tdshp_hist_read_cnt);
		} else
			pq_task->read_status.tdshp_comp =
				MML_PQ_HIST_READING;
	}
	mutex_unlock(&dev_data[out_idx]->tdshp_hist_mutex);
}

bool mml_pq_tdshp_hist_reading(struct mml_pq_task *pq_task, u8 out_idx, u8 pipe)
{
	u32 read_value = 0;

	mutex_lock(&dev_data[out_idx]->tdshp_hist_mutex);

	read_value = atomic_read(&dev_data[out_idx]->tdshp_hist_done);
	mml_pq_ir_log("%s pq_task[%p] hist_read_cnt[%d] hist_done[%d] status[%d] pq_ref[%d]",
		__func__, pq_task,
		atomic_read(&dev_data[out_idx]->tdshp_hist_read_cnt),
		atomic_read(&dev_data[out_idx]->tdshp_hist_done),
		pq_task->read_status.tdshp_comp,
		kref_read(&pq_task->ref));

	if (!atomic_read(&dev_data[out_idx]->tdshp_hist_read_cnt)) {
		mutex_unlock(&dev_data[out_idx]->tdshp_hist_mutex);
		return true;
	} else if (read_value & (1 << pipe)) {
		mutex_unlock(&dev_data[out_idx]->tdshp_hist_mutex);
		return true;
	}

	read_value = read_value | (1 << pipe);
	atomic_set(&dev_data[out_idx]->tdshp_hist_done, read_value);

	mutex_unlock(&dev_data[out_idx]->tdshp_hist_mutex);

	return false;

}

void mml_pq_tdshp_flag_check(bool dual, u8 out_idx)
{

	mutex_lock(&dev_data[out_idx]->tdshp_hist_mutex);
	if (check_hist_ready(dual, 1, &dev_data[out_idx]->tdshp_cnt)) {
		atomic_set(&dev_data[out_idx]->tdshp_hist_done, 0);
		atomic_dec_if_positive(&dev_data[out_idx]->tdshp_hist_read_cnt);
	}
	mutex_unlock(&dev_data[out_idx]->tdshp_hist_mutex);
}


int mml_pq_clarity_readback(struct mml_task *task, u8 pipe, u32 *phist, u32 arr_idx, u32 size)
{
	struct mml_pq_task *pq_task = task->pq_task;
	struct mml_pq_sub_task *sub_task = &pq_task->clarity_readback;
	struct mml_pq_chan *chan = &pq_mbox->clarity_readback_chan;
	struct mml_frame_dest *dest = NULL;
	int ret = 0;
	u32 rb_eng_num = 0;

	mml_pq_msg("%s called pipe[%d]\n", __func__, pipe);
	if (unlikely(!task))
		return -EINVAL;

	dump_pq_param(&task->pq_param[0]);
	dest = &task->config->info.dest[0];

	if (dest->pq_config.en_dre && dest->pq_config.en_sharp)
		rb_eng_num = MML_CLARITY_RB_ENG_NUM;
	else if (dest->pq_config.en_dre || dest->pq_config.en_sharp)
		rb_eng_num = 1;

	if (set_hist(sub_task, task->config->dual, pipe, phist,
		arr_idx, size, rb_eng_num))
		ret = set_sub_task(task, sub_task, chan, &task->pq_param[0], true);

	mml_pq_msg("%s end pipe[%d] job_id[%d]\n", __func__, pipe, task->job.jobid);
	return ret;
}

#if !IS_ENABLED(CONFIG_MTK_MML_LEGACY)
int mml_pq_ir_clarity_readback(struct mml_pq_task *pq_task, struct mml_pq_frame_data frame_data,
			u8 pipe, u32 *phist, u32 mml_jobid, u32 size, u32 arr_idx,
			bool dual)
{
	struct mml_pq_sub_task *sub_task = &pq_task->clarity_readback;
	struct mml_pq_chan *chan = &pq_mbox->clarity_readback_chan;
	int ret = 0;

	mml_pq_msg("%s called pipe[%d]\n", __func__, pipe);

	if (set_hist(sub_task, dual, pipe, phist,
		arr_idx, size, MML_CLARITY_RB_ENG_NUM))
		ret = set_readback_sub_task(pq_task, sub_task, chan,
			frame_data, dual, mml_jobid, true);

	mml_pq_msg("%s end pipe[%d]", __func__, pipe);
	return ret;
}
#endif

int mml_pq_dc_readback(struct mml_task *task, u8 pipe, u32 *phist)
{
	struct mml_pq_task *pq_task = task->pq_task;
	struct mml_pq_sub_task *sub_task = &pq_task->dc_readback;
	struct mml_pq_chan *chan = &pq_mbox->dc_readback_chan;
	int ret = 0;

	mml_pq_msg("%s called pipe[%d]\n", __func__, pipe);
	if (unlikely(!task))
		return -EINVAL;

	dump_pq_param(&task->pq_param[0]);

	if (set_hist(sub_task, task->config->dual, pipe, phist,
		0, TDSHP_CONTOUR_HIST_NUM, 1))
		ret = set_sub_task(task, sub_task, chan, &task->pq_param[0], true);

	mml_pq_msg("%s end pipe[%d] job_id[%d]\n", __func__, pipe, task->job.jobid);
	return ret;
}

#if !IS_ENABLED(CONFIG_MTK_MML_LEGACY)
int mml_pq_ir_dc_readback(struct mml_pq_task *pq_task, struct mml_pq_frame_data frame_data,
			u8 pipe, u32 *phist, u32 mml_jobid, u32 arr_idx,
			bool dual)
{
	struct mml_pq_sub_task *sub_task = &pq_task->dc_readback;
	struct mml_pq_chan *chan = &pq_mbox->dc_readback_chan;
	int ret = 0;

	mml_pq_trace_ex_begin("%s pipe[%d]", __func__, pipe);
	mml_pq_msg("%s called job_id[%d] pipe[%d] sub_task->job_id[%llu]",
		__func__, mml_jobid, pipe, sub_task->job_id);

	if (unlikely(!pq_task))
		return -EINVAL;

	if (set_hist(sub_task, dual, pipe, phist, 0,
		TDSHP_CONTOUR_HIST_NUM, 1))
		ret = set_readback_sub_task(pq_task, sub_task, chan,
			frame_data, dual, mml_jobid, true);

	mml_pq_ir_log("%s end job_id[%d]", __func__, mml_jobid);
	mml_pq_trace_ex_end();
	return ret;
}
#endif

#if !IS_ENABLED(CONFIG_MTK_MML_LEGACY)
int mml_pq_ir_wrot_callback(struct mml_pq_task *pq_task, struct mml_pq_frame_data frame_data,
			    u32 mml_jobid, bool dual)
{
	struct mml_pq_sub_task *sub_task = NULL;
	struct mml_pq_chan *chan = NULL;
	int ret = 0;

	if (unlikely(!pq_task))
		return -EINVAL;

	sub_task = &pq_task->wrot_callback;
	chan = &pq_mbox->wrot_callback_chan;

	mml_pq_msg("%s second outoput done.\n", __func__);
	ret = set_readback_sub_task(pq_task, sub_task, chan,
		frame_data, dual, mml_jobid, true);
	return ret;
}
#endif

int mml_pq_wrot_callback(struct mml_task *task)
{
	struct mml_pq_task *pq_task = task->pq_task;
	struct mml_pq_sub_task *sub_task = NULL;
	struct mml_pq_chan *chan = NULL;
	int ret = 0;

	if (unlikely(!pq_task))
		return -EINVAL;

	sub_task = &pq_task->wrot_callback;
	chan = &pq_mbox->wrot_callback_chan;

	mml_pq_msg("%s second outoput done.\n", __func__);
	ret = set_sub_task(task, sub_task, chan, &task->pq_param[1], true);
	mml_pq_msg("%s end\n", __func__);
	return ret;
}

int mml_pq_get_comp_config_result(struct mml_task *task, u32 timeout_ms)
{
	struct mml_pq_task *pq_task = task->pq_task;
	s32 ret = 0;

	mml_pq_trace_ex_begin("%s", __func__);
	mml_pq_msg("%s called, %d job_id[%d]", __func__,
		timeout_ms, task->job.jobid);

	ret = get_sub_task_result(pq_task, &pq_task->comp_config, timeout_ms,
				  dump_comp_config);
	mml_pq_trace_ex_end();
	return ret;
}

void mml_pq_put_comp_config_result(struct mml_task *task)
{
	struct mml_pq_chan *chan = &pq_mbox->comp_config_chan;

	mml_pq_msg("%s called job_id[%d]\n", __func__,
		task->job.jobid);

	put_sub_task_result(&task->pq_task->comp_config, chan);
}

static void handle_sub_task_result(struct mml_pq_sub_task *sub_task,
	void *result, void (*release_result)(void *result))
{
	mutex_lock(&sub_task->lock);
	if (atomic_read(&sub_task->result_ref)) {
		/* old result still in-use, abandon new result or else? */
		release_result(result);
	} else {
		if (sub_task->result)	/* destroy old result */
			release_result(sub_task->result);
		sub_task->result = result;
	}
	atomic_dec_if_positive(&sub_task->queued);
	mutex_unlock(&sub_task->lock);
}

static void wake_up_sub_task(struct mml_pq_sub_task *sub_task,
			     struct mml_pq_task *pq_task)
{
	mutex_lock(&sub_task->lock);
	wake_up(&sub_task->wq);
	mutex_unlock(&sub_task->lock);

	/* decrease pq_task ref after finish the result */
	mml_pq_put_pq_task(pq_task);
}

static struct mml_pq_sub_task *wait_next_sub_task(struct mml_pq_chan *chan)
{
	struct mml_pq_sub_task *sub_task = NULL;
	s32 ret;
	u32 error_cnt = 0;
	bool err_print = true;

	mml_pq_trace_ex_begin("%s", __func__);
	mml_pq_msg("%s called msg_cnt[%d]", __func__,
		atomic_read(&chan->msg_cnt));
	for (;;) {
		mml_pq_msg("start wait event!");

		ret = wait_event_interruptible(chan->msg_wq,
			(atomic_read(&chan->msg_cnt) > 0));
		if (ret) {
			if (ret != -ERESTARTSYS)
				mml_pq_log("%s wakeup wait_result[%d], msg_cnt[%d]",
					__func__, ret, atomic_read(&chan->msg_cnt));
			break;
		}

		mml_pq_msg("%s finish wait event! wait_result[%d], msg_cnt[%d]",
			__func__, ret, atomic_read(&chan->msg_cnt));

		ret = dequeue_msg(chan, &sub_task, err_print);
		if (unlikely(ret)) {
			if (error_cnt < 5)
				mml_pq_err("%s err: dequeue msg failed: %d msg_cnt[%d]",
					__func__, ret, atomic_read(&chan->msg_cnt));
			if (error_cnt <= 5)
				error_cnt++;
			else
				err_print = false;
			usleep_range(5000, 5100);
			continue;
		}

		mml_pq_msg("after dequeue msg!");
		break;
	}

	if (error_cnt)
		mml_pq_err("%s err[%d] recover", __func__, error_cnt);

	mml_pq_msg("%s end %d task=%p", __func__, ret, sub_task);
	mml_pq_trace_ex_end();
	return sub_task;
}

static void cancel_sub_task(struct mml_pq_sub_task *sub_task)
{
	mutex_lock(&sub_task->lock);
	sub_task->job_cancelled = true;
	wake_up(&sub_task->wq);
	mutex_unlock(&sub_task->lock);
}

static void handle_tile_init_result(struct mml_pq_chan *chan,
				    struct mml_pq_tile_init_job *job)
{
	struct mml_pq_sub_task *sub_task = NULL;
	struct mml_pq_tile_init_result *result;
	struct mml_pq_reg *rsz_regs[MML_MAX_OUTPUTS];
	s32 ret;

	mml_pq_trace_ex_begin("%s", __func__);
	mml_pq_log("%s called, %d", __func__, job->result_job_id);
	ret = find_sub_task(chan, job->result_job_id, &sub_task);
	if (unlikely(ret)) {
		mml_pq_err("finish tile sub_task failed!: %d id: %d", ret,
			job->result_job_id);
		mml_pq_trace_ex_end();
		return;
	}

	result = kmalloc(sizeof(*result), GFP_KERNEL);
	if (unlikely(!result)) {
		mml_pq_err("err: create result failed");
		goto wake_up_prev_tile_init_task;
	}

	ret = copy_from_user(result, job->result, sizeof(*result));
	if (unlikely(ret)) {
		mml_pq_err("copy job result failed!: %d", ret);
		goto free_tile_init_result;
	}

	if (unlikely(result->rsz_param_cnt > MML_MAX_OUTPUTS)) {
		mml_pq_err("invalid rsz param count!: %d",
			result->rsz_param_cnt);
		goto free_tile_init_result;
	}

	rsz_regs[0] = kmalloc_array(result->rsz_reg_cnt[0], sizeof(struct mml_pq_reg),
				GFP_KERNEL);
	if (unlikely(!rsz_regs[0])) {
		mml_pq_err("err: create rsz_regs failed, size:%d",
			result->rsz_reg_cnt[0]);
		goto free_tile_init_result;
	}

	ret = copy_from_user(rsz_regs[0], result->rsz_regs[0],
		result->rsz_reg_cnt[0] * sizeof(struct mml_pq_reg));
	if (unlikely(ret)) {
		mml_pq_err("copy rsz config failed!: %d", ret);
		goto free_rsz_regs_0;
	}
	result->rsz_regs[0] = rsz_regs[0];

	/* second output */
	if (result->rsz_param_cnt == MML_MAX_OUTPUTS) {
		rsz_regs[1] = kmalloc_array(result->rsz_reg_cnt[1], sizeof(struct mml_pq_reg),
					GFP_KERNEL);
		if (unlikely(!rsz_regs[1])) {
			mml_pq_err("err: create rsz_regs failed, size:%d",
				result->rsz_reg_cnt[1]);
			goto free_rsz_regs_0;
		}

		ret = copy_from_user(rsz_regs[1], result->rsz_regs[1],
			result->rsz_reg_cnt[1] * sizeof(struct mml_pq_reg));
		if (unlikely(ret)) {
			mml_pq_err("copy rsz config failed!: %d", ret);
			goto free_rsz_regs_1;
		}
		result->rsz_regs[1] = rsz_regs[1];
	}

	handle_sub_task_result(sub_task, result, release_tile_init_result);
	goto wake_up_prev_tile_init_task;
free_rsz_regs_1:
	kfree(rsz_regs[1]);
free_rsz_regs_0:
	kfree(rsz_regs[0]);
free_tile_init_result:
	kfree(result);
wake_up_prev_tile_init_task:
	wake_up_sub_task(sub_task, from_tile_init(sub_task));
	mml_pq_msg("%s end, %d", __func__, ret);
	mml_pq_trace_ex_end();
}

static int mml_pq_tile_init_ioctl(unsigned long data)
{
	struct mml_pq_chan *chan = &pq_mbox->tile_init_chan;
	struct mml_pq_sub_task *new_sub_task = NULL;
	struct mml_pq_tile_init_job *job;
	struct mml_pq_tile_init_job *user_job;
	u32 new_job_id;
	s32 ret;

	mml_pq_trace_ex_begin("%s", __func__);
	mml_pq_msg("%s called", __func__);
	user_job = (struct mml_pq_tile_init_job *)data;
	if (unlikely(!user_job)) {
		mml_pq_trace_ex_end();
		return -EINVAL;
	}

	job = kmalloc(sizeof(*job), GFP_KERNEL);
	if (unlikely(!job)) {
		mml_pq_trace_ex_end();
		return -ENOMEM;
	}

	ret = copy_from_user(job, user_job, sizeof(*job));
	if (unlikely(ret)) {
		mml_pq_err("copy_from_user failed: %d", ret);
		kfree(job);
		mml_pq_trace_ex_end();
		return -EINVAL;
	}
	mml_pq_msg("%s result_job_id[%d]", __func__, job->result_job_id);
	if (job->result_job_id)
		handle_tile_init_result(chan, job);
	kfree(job);
	mml_pq_trace_ex_end();

	mml_pq_trace_ex_begin("%s start wait sub_task", __func__);
	new_sub_task = wait_next_sub_task(chan);
	if (!new_sub_task) {
		mml_pq_msg("%s Get sub task failed", __func__);
		mml_pq_trace_ex_end();
		return -ERESTARTSYS;
	}

	new_job_id = new_sub_task->job_id;

	ret = copy_to_user(&user_job->new_job_id, &new_job_id, sizeof(u32));

	ret = copy_to_user(&user_job->info, &new_sub_task->frame_data->info,
		sizeof(struct mml_frame_info));
	if (unlikely(ret)) {
		mml_pq_err("err: fail to copy to user frame info: %d", ret);
		goto wake_up_tile_init_task;
	}

	ret = copy_to_user(user_job->dst, new_sub_task->frame_data->frame_out,
		MML_MAX_OUTPUTS * sizeof(struct mml_frame_size));
	if (unlikely(ret)) {
		mml_pq_err("err: fail to copy to user frame out: %d", ret);
		goto wake_up_tile_init_task;
	}

	ret = copy_to_user(&user_job->size_info, &new_sub_task->frame_data->size_info,
		sizeof(struct mml_pq_frame_info));
	if (unlikely(ret)) {
		mml_pq_err("err: fail to copy to user frame size_info: %d\n", ret);
		goto wake_up_tile_init_task;
	}

	ret = copy_to_user(user_job->param, new_sub_task->frame_data->pq_param,
		MML_MAX_OUTPUTS * sizeof(struct mml_pq_param));
	if (unlikely(ret)) {
		mml_pq_err("err: fail to copy to user pq param: %d", ret);
		goto wake_up_tile_init_task;
	}
	dump_sub_task(new_sub_task, new_job_id, 0, 0, 0);
	mml_pq_msg("%s end", __func__);
	mml_pq_trace_ex_end();
	return 0;

wake_up_tile_init_task:
	cancel_sub_task(new_sub_task);
	mml_pq_msg("%s end %d", __func__, ret);
	mml_pq_trace_ex_end();
	return ret;
}

static void handle_comp_config_result(struct mml_pq_chan *chan,
				      struct mml_pq_comp_config_job *job)
{
	struct mml_pq_sub_task *sub_task = NULL;
	struct mml_pq_comp_config_result *result = NULL;
	struct mml_pq_aal_config_param *aal_param = NULL;
	struct mml_pq_reg *aal_regs = NULL;
	struct mml_pq_reg *hdr_regs = NULL;
	struct mml_pq_reg *ds_regs = NULL;
	struct mml_pq_reg *color_regs = NULL;
	struct mml_pq_reg *c3d_regs = NULL;
	u32 *aal_curve = NULL;
	u32 *hdr_curve = NULL;
	u32 *c3d_lut = NULL;
	s32 ret;

	mml_pq_trace_ex_begin("%s", __func__);
	mml_pq_msg("%s called, %d", __func__, job->result_job_id);
	ret = find_sub_task(chan, job->result_job_id, &sub_task);
	if (unlikely(ret)) {
		mml_pq_err("finish comp sub_task failed!: %d id: %d", ret,
			job->result_job_id);
		mml_pq_trace_ex_end();
		return;
	}
	mml_pq_msg("%s end %d task=%p sub_task->id[%llu]", __func__, ret,
			sub_task, sub_task->job_id);

	result = kmalloc(sizeof(*result), GFP_KERNEL);
	if (unlikely(!result)) {
		mml_pq_err("err: create result failed");
		goto wake_up_prev_comp_config_task;
	}

	ret = copy_from_user(result, job->result, sizeof(*result));
	if (unlikely(ret)) {
		mml_pq_err("copy job result failed!: %d", ret);
		goto free_comp_config_result;
	}

	if (unlikely(result->param_cnt > MML_MAX_OUTPUTS)) {
		mml_pq_err("invalid param count!: %d",
			result->param_cnt);
		goto free_comp_config_result;
	}

	aal_param = kmalloc_array(result->param_cnt, sizeof(*aal_param),
				  GFP_KERNEL);
	if (unlikely(!aal_param)) {
		mml_pq_err("err: create aal_param failed, size:%d",
			result->param_cnt);
		goto free_comp_config_result;
	}

	ret = copy_from_user(aal_param, result->aal_param,
		result->param_cnt * sizeof(*aal_param));
	if (unlikely(ret)) {
		mml_pq_err("copy aal param failed!: %d", ret);
		goto free_aal_param;
	}

	aal_regs = kmalloc_array(result->aal_reg_cnt, sizeof(*aal_regs),
				 GFP_KERNEL);
	if (unlikely(!aal_regs)) {
		mml_pq_err("err: create aal_regs failed, size:%d",
			result->aal_reg_cnt);
		goto free_aal_param;
	}

	ret = copy_from_user(aal_regs, result->aal_regs,
		result->aal_reg_cnt * sizeof(*aal_regs));
	if (unlikely(ret)) {
		mml_pq_err("copy aal config failed!: %d", ret);
		goto free_aal_regs;
	}

	aal_curve = kmalloc_array(AAL_CURVE_NUM, sizeof(u32), GFP_KERNEL);
	if (unlikely(!aal_curve)) {
		mml_pq_err("err: create aal_curve failed, size:%d",
			AAL_CURVE_NUM);
		goto free_aal_regs;
	}

	ret = copy_from_user(aal_curve, result->aal_curve,
		AAL_CURVE_NUM * sizeof(u32));
	if (unlikely(ret)) {
		mml_pq_err("copy aal curve failed!: %d", ret);
		goto free_aal_curve;
	}

	hdr_regs = kmalloc_array(result->hdr_reg_cnt, sizeof(*hdr_regs),
				 GFP_KERNEL);
	if (unlikely(!hdr_regs)) {
		mml_pq_err("err: create hdr_regs failed, size:%d\n",
			result->hdr_reg_cnt);
		goto free_aal_curve;
	}

	ret = copy_from_user(hdr_regs, result->hdr_regs,
		result->hdr_reg_cnt * sizeof(*hdr_regs));
	if (unlikely(ret)) {
		mml_pq_err("copy aal config failed!: %d\n", ret);
		goto free_hdr_regs;
	}

	hdr_curve = kmalloc_array(HDR_CURVE_NUM, sizeof(u32),
				  GFP_KERNEL);
	if (unlikely(!hdr_curve)) {
		mml_pq_err("err: create hdr_curve failed, size:%d\n",
			HDR_CURVE_NUM);
		goto free_hdr_regs;
	}

	ret = copy_from_user(hdr_curve, result->hdr_curve,
		HDR_CURVE_NUM * sizeof(u32));
	if (unlikely(ret)) {
		mml_pq_err("copy hdr curve failed!: %d\n", ret);
		goto free_hdr_curve;
	}

	ds_regs = kmalloc_array(result->ds_reg_cnt, sizeof(*ds_regs),
				GFP_KERNEL);
	if (unlikely(!ds_regs)) {
		mml_pq_err("err: create ds_regs failed, size:%d\n",
			result->ds_reg_cnt);
		goto free_hdr_curve;
	}

	ret = copy_from_user(ds_regs, result->ds_regs,
		result->ds_reg_cnt * sizeof(*ds_regs));
	if (unlikely(ret)) {
		mml_pq_err("copy ds config failed!: %d\n", ret);
		goto free_ds_regs;
	}

	color_regs = kmalloc_array(result->color_reg_cnt, sizeof(*color_regs),
				   GFP_KERNEL);
	if (unlikely(!color_regs)) {
		mml_pq_err("err: create color_regs failed, size:%d\n",
			result->ds_reg_cnt);
		goto free_ds_regs;
	}

	ret = copy_from_user(color_regs, result->color_regs,
		result->color_reg_cnt * sizeof(*color_regs));
	if (unlikely(ret)) {
		mml_pq_err("copy color config failed!: %d\n", ret);
		goto free_color_regs;
	}

	c3d_regs = kmalloc_array(result->c3d_reg_cnt, sizeof(*c3d_regs),
				   GFP_KERNEL);
	if (unlikely(!c3d_regs)) {
		mml_pq_err("err: create c3d_regs failed, size:%d\n",
			result->ds_reg_cnt);
		goto free_color_regs;
	}

	ret = copy_from_user(c3d_regs, result->c3d_regs,
		result->c3d_reg_cnt * sizeof(*c3d_regs));
	if (unlikely(ret)) {
		mml_pq_err("copy color config failed!: %d\n", ret);
		goto free_c3d_regs;
	}

	c3d_lut = kmalloc_array(C3D_LUT_NUM, sizeof(u32),
				  GFP_KERNEL);
	if (unlikely(!c3d_lut)) {
		mml_pq_err("err: create c3d_lut failed, size:%d\n",
			C3D_LUT_NUM);
		goto free_c3d_regs;
	}

	ret = copy_from_user(c3d_lut, result->c3d_lut,
		C3D_LUT_NUM * sizeof(u32));
	if (unlikely(ret)) {
		mml_pq_err("copy c3d_lut failed!: %d\n", ret);
		goto free_c3d_lut_curve;
	}

	result->aal_param = aal_param;
	result->aal_regs = aal_regs;
	result->aal_curve = aal_curve;
	result->hdr_regs = hdr_regs;
	result->hdr_curve = hdr_curve;
	result->ds_regs = ds_regs;
	result->color_regs = color_regs;
	result->c3d_regs = c3d_regs;
	result->c3d_lut = c3d_lut;

	handle_sub_task_result(sub_task, result, release_comp_config_result);
	mml_pq_msg("%s result end, result_id[%d] sub_task[%p]",
		__func__, job->result_job_id, sub_task);
	goto wake_up_prev_comp_config_task;
free_c3d_lut_curve:
	kfree(c3d_lut);
free_c3d_regs:
	kfree(c3d_regs);
free_color_regs:
	kfree(color_regs);
free_ds_regs:
	kfree(ds_regs);
free_hdr_curve:
	kfree(hdr_curve);
free_hdr_regs:
	kfree(hdr_regs);
free_aal_curve:
	kfree(aal_curve);
free_aal_regs:
	kfree(aal_regs);
free_aal_param:
	kfree(aal_param);
free_comp_config_result:
	kfree(result);
wake_up_prev_comp_config_task:
	wake_up_sub_task(sub_task, from_comp_config(sub_task));
	mml_pq_msg("%s end, %d", __func__, ret);
	mml_pq_trace_ex_end();
}

static int mml_pq_comp_config_ioctl(unsigned long data)
{
	struct mml_pq_chan *chan = &pq_mbox->comp_config_chan;
	struct mml_pq_sub_task *new_sub_task = NULL;
	struct mml_pq_comp_config_job *job;
	struct mml_pq_comp_config_job *user_job;
	u32 new_job_id;
	s32 ret;

	mml_pq_trace_ex_begin("%s", __func__);
	mml_pq_msg("%s called", __func__);
	user_job = (struct mml_pq_comp_config_job *)data;
	if (unlikely(!user_job)) {
		mml_pq_trace_ex_end();
		return -EINVAL;
	}

	job = kmalloc(sizeof(*job), GFP_KERNEL);
	if (unlikely(!job)) {
		mml_pq_trace_ex_end();
		return -ENOMEM;
	}

	ret = copy_from_user(job, user_job, sizeof(*job));
	if (unlikely(ret)) {
		mml_pq_err("copy_from_user failed: %d", ret);
		kfree(job);
		mml_pq_trace_ex_end();
		return -EINVAL;
	}
	mml_pq_msg("%s new_job_id[%d] result_job_id[%d]", __func__,
		job->new_job_id, job->result_job_id);

	if (job->result_job_id)
		handle_comp_config_result(chan, job);
	kfree(job);
	mml_pq_trace_ex_end();

	mml_pq_trace_ex_begin("%s start wait sub_task", __func__);
	new_sub_task = wait_next_sub_task(chan);
	if (!new_sub_task) {
		mml_pq_msg("%s Get sub task failed", __func__);
		mml_pq_trace_ex_end();
		return -ERESTARTSYS;
	}

	new_job_id = new_sub_task->job_id;

	ret = copy_to_user(&user_job->new_job_id, &new_job_id, sizeof(u32));

	ret = copy_to_user(&user_job->info, &new_sub_task->frame_data->info,
		sizeof(struct mml_frame_info));
	if (unlikely(ret)) {
		mml_pq_err("err: fail to copy to user frame info: %d", ret);
		goto wake_up_comp_config_task;
	}

	ret = copy_to_user(user_job->dst, new_sub_task->frame_data->frame_out,
		MML_MAX_OUTPUTS * sizeof(struct mml_frame_size));
	if (unlikely(ret)) {
		mml_pq_err("err: fail to copy to user frame out: %d", ret);
		goto wake_up_comp_config_task;
	}

	ret = copy_to_user(&user_job->size_info, &new_sub_task->frame_data->size_info,
		sizeof(struct mml_pq_frame_info));
	if (unlikely(ret)) {
		mml_pq_err("err: fail to copy to user frame size_info: %d\n", ret);
		goto wake_up_comp_config_task;
	}

	ret = copy_to_user(user_job->param, new_sub_task->frame_data->pq_param,
		MML_MAX_OUTPUTS * sizeof(struct mml_pq_param));
	if (unlikely(ret)) {
		mml_pq_err("err: fail to copy to user pq param: %d", ret);
		goto wake_up_comp_config_task;
	}
	dump_sub_task(new_sub_task, new_job_id, 0, 0, 0);
	mml_pq_msg("%s end", __func__);
	mml_pq_trace_ex_end();
	return 0;

wake_up_comp_config_task:
	cancel_sub_task(new_sub_task);
	mml_pq_msg("%s end %d", __func__, ret);
	mml_pq_trace_ex_end();
	return ret;
}

static int mml_pq_aal_readback_ioctl(unsigned long data)
{
	struct mml_pq_chan *chan = &pq_mbox->aal_readback_chan;
	struct mml_pq_sub_task *new_sub_task = NULL;
	struct mml_pq_task *new_pq_task = NULL;
	struct mml_pq_aal_readback_job *job;
	struct mml_pq_aal_readback_job *user_job;
	struct mml_pq_aal_readback_result *readback = NULL;
	u32 new_job_id;
	s32 ret;

	mml_pq_msg("%s called\n", __func__);
	user_job = (struct mml_pq_aal_readback_job *)data;
	if (unlikely(!user_job))
		return -EINVAL;

	job = kmalloc(sizeof(*job), GFP_KERNEL);
	if (unlikely(!job))
		return -ENOMEM;

	ret = copy_from_user(job, user_job, sizeof(*job));
	if (unlikely(ret)) {
		mml_pq_err("copy_from_user failed: %d", ret);
		kfree(job);
		return -EINVAL;
	}

	readback = kmalloc(sizeof(*readback), GFP_KERNEL);
	if (unlikely(!readback)) {
		kfree(job);
		return -ENOMEM;
	}

	ret = copy_from_user(readback, job->result, sizeof(*readback));
	if (unlikely(ret)) {
		mml_pq_err("copy_from_user failed: %d\n", ret);
		kfree(job);
		kfree(readback);
		return -EINVAL;
	}

	new_sub_task = wait_next_sub_task(chan);
	if (!new_sub_task) {
		kfree(job);
		kfree(readback);
		mml_pq_msg("%s Get sub task failed", __func__);
		return -ERESTARTSYS;
	}

	new_pq_task = from_aal_readback(new_sub_task);
	new_job_id = new_sub_task->job_id;

	ret = copy_to_user(&user_job->new_job_id, &new_job_id, sizeof(u32));

	ret = copy_to_user(&user_job->info, &new_sub_task->frame_data->info,
		sizeof(struct mml_frame_info));
	if (unlikely(ret)) {
		mml_pq_err("err: fail to copy to user frame info: %d\n", ret);
		goto wake_up_aal_readback_task;
	}

	ret = copy_to_user(&user_job->size_info, &new_sub_task->frame_data->size_info,
		sizeof(struct mml_pq_frame_info));
	if (unlikely(ret)) {
		mml_pq_err("err: fail to copy to user frame size_info: %d\n", ret);
		goto wake_up_aal_readback_task;
	}

	ret = copy_to_user(user_job->param, new_sub_task->frame_data->pq_param,
		MML_MAX_OUTPUTS * sizeof(struct mml_pq_param));
	if (unlikely(ret)) {
		mml_pq_err("err: fail to copy to user pq param: %d\n", ret);
		goto wake_up_aal_readback_task;
	}

	readback->is_dual = new_sub_task->readback_data.is_dual;
	readback->cut_pos_x = new_sub_task->readback_data.cut_pos_x;

	mml_pq_msg("%s is_dual[%d] cut_pos_x[%d]", __func__,
		readback->is_dual, readback->cut_pos_x);

	ret = copy_to_user(&job->result->is_dual, &readback->is_dual, sizeof(bool));
	if (unlikely(ret)) {
		mml_pq_err("err: fail to copy to width: %d\n", ret);
		goto wake_up_aal_readback_task;
	}

	ret = copy_to_user(&job->result->cut_pos_x, &readback->cut_pos_x, sizeof(u32));
	if (unlikely(ret)) {
		mml_pq_err("err: fail to copy to width: %d\n", ret);
		goto wake_up_aal_readback_task;
	}

	if (new_sub_task->readback_data.pipe0_hist) {
		ret = copy_to_user(readback->aal_pipe0_hist,
			new_sub_task->readback_data.pipe0_hist,
			(AAL_HIST_NUM + AAL_DUAL_INFO_NUM) * sizeof(u32));
		atomic_dec_if_positive(&new_sub_task->readback_data.pipe_cnt);
		if (unlikely(ret)) {
			mml_pq_err("err: fail to copy to aal_pipe0_hist: %d\n", ret);
			goto wake_up_aal_readback_task;
		}
	} else {
		mml_pq_err("err: fail to copy to aal_pipe0_hist because of null pointer");
	}

	if (readback->is_dual && new_sub_task->readback_data.pipe1_hist) {
		ret = copy_to_user(readback->aal_pipe1_hist,
			new_sub_task->readback_data.pipe1_hist,
			(AAL_HIST_NUM + AAL_DUAL_INFO_NUM) * sizeof(u32));
		atomic_dec_if_positive(&new_sub_task->readback_data.pipe_cnt);
		if (unlikely(ret)) {
			mml_pq_err("err: fail to copy to aal_pipe1_hist: %d\n", ret);
			goto wake_up_aal_readback_task;
		}
	} else if (!readback->is_dual) {
		mml_pq_msg("single pipe");
	}

	remove_sub_task(chan, new_sub_task);
	atomic_dec_if_positive(&new_sub_task->queued);
	mml_pq_put_pq_task(new_pq_task);


	dump_sub_task(new_sub_task, new_job_id, readback->is_dual, readback->cut_pos_x,
		AAL_HIST_NUM + AAL_DUAL_INFO_NUM);
	mml_pq_msg("%s end job_id[%d]\n", __func__, job->new_job_id);
	kfree(job);
	kfree(readback);
	return 0;

wake_up_aal_readback_task:
	remove_sub_task(chan, new_sub_task);
	atomic_dec_if_positive(&new_sub_task->queued);
	mml_pq_put_pq_task(new_pq_task);
	kfree(readback);
	kfree(job);
	cancel_sub_task(new_sub_task);
	mml_pq_msg("%s end %d", __func__, ret);
	return ret;
}

static int mml_pq_hdr_readback_ioctl(unsigned long data)
{
	struct mml_pq_chan *chan = &pq_mbox->hdr_readback_chan;
	struct mml_pq_sub_task *new_sub_task = NULL;
	struct mml_pq_task *new_pq_task = NULL;
	struct mml_pq_hdr_readback_job *job;
	struct mml_pq_hdr_readback_job *user_job;
	struct mml_pq_hdr_readback_result *readback = NULL;
	u32 new_job_id;
	s32 ret = 0;

	mml_pq_msg("%s called\n", __func__);
	user_job = (struct mml_pq_hdr_readback_job *)data;
	if (unlikely(!user_job))
		return -EINVAL;

	job = kmalloc(sizeof(*job), GFP_KERNEL);
	if (unlikely(!job))
		return -ENOMEM;

	ret = copy_from_user(job, user_job, sizeof(*job));
	if (unlikely(ret)) {
		mml_pq_err("copy_from_user failed: %d\n", ret);
		kfree(job);
		return -EINVAL;
	}

	readback = kmalloc(sizeof(*readback), GFP_KERNEL);
	if (unlikely(!readback)) {
		kfree(job);
		return -ENOMEM;
	}

	ret = copy_from_user(readback, job->result, sizeof(*readback));

	if (unlikely(ret)) {
		mml_pq_err("copy_from_user failed: %d\n", ret);
		kfree(job);
		kfree(readback);
		return -EINVAL;
	}

	new_sub_task = wait_next_sub_task(chan);

	if (!new_sub_task) {
		kfree(job);
		kfree(readback);
		mml_pq_msg("%s Get sub task failed", __func__);
		return -ERESTARTSYS;
	}

	new_pq_task = from_hdr_readback(new_sub_task);
	new_job_id = new_sub_task->job_id;

	ret = copy_to_user(&user_job->new_job_id, &new_job_id, sizeof(u32));

	ret = copy_to_user(&user_job->info, &new_sub_task->frame_data->info,
		sizeof(struct mml_frame_info));
	if (unlikely(ret)) {
		mml_pq_err("err: fail to copy to user frame info: %d\n", ret);
		goto wake_up_hdr_readback_task;
	}

	ret = copy_to_user(&user_job->size_info, &new_sub_task->frame_data->size_info,
		sizeof(struct mml_pq_frame_info));
	if (unlikely(ret)) {
		mml_pq_err("err: fail to copy to user frame size_info: %d\n", ret);
		goto wake_up_hdr_readback_task;
	}

	ret = copy_to_user(user_job->param, new_sub_task->frame_data->pq_param,
		MML_MAX_OUTPUTS * sizeof(struct mml_pq_param));
	if (unlikely(ret)) {
		mml_pq_err("err: fail to copy to user pq param: %d\n", ret);
		goto wake_up_hdr_readback_task;
	}

	readback->is_dual = new_sub_task->readback_data.is_dual;
	readback->cut_pos_x = new_sub_task->readback_data.cut_pos_x;

	ret = copy_to_user(&job->result->is_dual, &readback->is_dual, sizeof(bool));
	if (unlikely(ret)) {
		mml_pq_err("err: fail to copy to width: %d\n", ret);
		goto wake_up_hdr_readback_task;
	}

	ret = copy_to_user(&job->result->cut_pos_x, &readback->cut_pos_x, sizeof(u32));
	if (unlikely(ret)) {
		mml_pq_err("err: fail to copy to width: %d\n", ret);
		goto wake_up_hdr_readback_task;
	}

	if (new_sub_task->readback_data.pipe0_hist) {
		ret = copy_to_user(readback->hdr_pipe0_hist,
			new_sub_task->readback_data.pipe0_hist,
			HDR_HIST_NUM * sizeof(u32));
		atomic_dec_if_positive(&new_sub_task->readback_data.pipe_cnt);
		if (unlikely(ret)) {
			mml_pq_err("err: fail to copy to hdr_pipe0_hist: %d\n", ret);
			goto wake_up_hdr_readback_task;
		}
	} else {
		mml_pq_err("err: fail to copy to hdr_pipe0_hist because of null pointer");
	}

	if (readback->is_dual && new_sub_task->readback_data.pipe1_hist) {
		ret = copy_to_user(readback->hdr_pipe1_hist,
			new_sub_task->readback_data.pipe1_hist,
			HDR_HIST_NUM * sizeof(u32));
		atomic_dec_if_positive(&new_sub_task->readback_data.pipe_cnt);
		if (unlikely(ret)) {
			mml_pq_err("err: fail to copy to hdr_pipe1_hist: %d\n", ret);
			goto wake_up_hdr_readback_task;
		}
	} else {
		mml_pq_msg("single pipe");
	}

	remove_sub_task(chan, new_sub_task);
	atomic_dec_if_positive(&new_sub_task->queued);
	mml_pq_put_pq_task(new_pq_task);

	dump_sub_task(new_sub_task, new_job_id, readback->is_dual, readback->cut_pos_x,
		HDR_HIST_NUM);
	mml_pq_msg("%s end job_id[%d]\n", __func__, job->new_job_id);
	kfree(job);
	kfree(readback);
	return 0;

wake_up_hdr_readback_task:
	remove_sub_task(chan, new_sub_task);
	atomic_dec_if_positive(&new_sub_task->queued);
	mml_pq_put_pq_task(new_pq_task);
	kfree(job);
	kfree(readback);
	cancel_sub_task(new_sub_task);
	mml_pq_msg("%s end %d\n", __func__, ret);
	return ret;
}

static int mml_pq_wrot_callback_ioctl(unsigned long data)
{
	struct mml_pq_chan *chan = &pq_mbox->wrot_callback_chan;
	struct mml_pq_sub_task *new_sub_task = NULL;
	struct mml_pq_task *new_pq_task = NULL;
	struct mml_pq_wrot_callback_job *job;
	struct mml_pq_wrot_callback_job *user_job;
	u32 new_job_id;
	s32 ret = 0;

	mml_pq_msg("%s called chan[%08lx]", __func__, (unsigned long)chan);
	user_job = (struct mml_pq_wrot_callback_job *)data;
	if (unlikely(!user_job))
		return -EINVAL;

	job = kmalloc(sizeof(*job), GFP_KERNEL);
	if (unlikely(!job))
		return -ENOMEM;

	ret = copy_from_user(job, user_job, sizeof(*job));
	if (unlikely(ret)) {
		mml_pq_err("%s copy_from_user failed: %d\n", __func__, ret);
		kfree(job);
		return -EINVAL;
	}

	new_sub_task = wait_next_sub_task(chan);

	if (!new_sub_task) {
		kfree(job);
		mml_pq_msg("%s Get sub task failed", __func__);
		return -ERESTARTSYS;
	}

	new_pq_task = from_wrot_callback(new_sub_task);
	new_job_id = new_sub_task->job_id;

	ret = copy_to_user(&user_job->new_job_id, &new_job_id, sizeof(u32));

	ret = copy_to_user(&user_job->info, &new_sub_task->frame_data->info,
		sizeof(struct mml_frame_info));
	if (unlikely(ret)) {
		mml_pq_err("%s err: fail to copy to user frame info: %d\n",
			__func__, ret);
		goto wake_up_wrot_callback_task;
	}

	ret = copy_to_user(&user_job->size_info, &new_sub_task->frame_data->size_info,
		sizeof(struct mml_pq_frame_info));
	if (unlikely(ret)) {
		mml_pq_err("err: fail to copy to user frame size_info: %d\n", ret);
		goto wake_up_wrot_callback_task;
	}

	ret = copy_to_user(user_job->param, new_sub_task->frame_data->pq_param,
		MML_MAX_OUTPUTS * sizeof(struct mml_pq_param));
	if (unlikely(ret)) {
		mml_pq_err("%s err: fail to copy to user pq param: %d\n",
			__func__, ret);
		goto wake_up_wrot_callback_task;
	}

	remove_sub_task(chan, new_sub_task);
	atomic_dec_if_positive(&new_sub_task->queued);
	mml_pq_put_pq_task(new_pq_task);

	mml_pq_msg("%s end job_id[%d]\n", __func__, job->new_job_id);
	kfree(job);
	return 0;

wake_up_wrot_callback_task:
	remove_sub_task(chan, new_sub_task);
	atomic_dec_if_positive(&new_sub_task->queued);
	mml_pq_put_pq_task(new_pq_task);
	kfree(job);
	cancel_sub_task(new_sub_task);
	mml_pq_msg("%s end %d\n", __func__, ret);
	return ret;
}

static int mml_pq_clarity_readback_ioctl(unsigned long data)
{
	struct mml_pq_chan *chan = &pq_mbox->clarity_readback_chan;
	struct mml_pq_sub_task *new_sub_task = NULL;
	struct mml_pq_task *new_pq_task = NULL;
	struct mml_pq_clarity_readback_job *job;
	struct mml_pq_clarity_readback_job *user_job;
	struct mml_pq_clarity_readback_result *readback = NULL;
	u32 new_job_id;
	s32 ret = 0;

	mml_pq_msg("%s called chan[%08lx]", __func__, (unsigned long)chan);
	user_job = (struct mml_pq_clarity_readback_job *)data;
	if (unlikely(!user_job))
		return -EINVAL;

	job = kmalloc(sizeof(*job), GFP_KERNEL);
	if (unlikely(!job))
		return -ENOMEM;

	ret = copy_from_user(job, user_job, sizeof(*job));
	if (unlikely(ret)) {
		mml_pq_err("copy_from_user failed: %d\n", ret);
		kfree(job);
		return -EINVAL;
	}

	readback = kmalloc(sizeof(*readback), GFP_KERNEL);
	if (unlikely(!readback)) {
		kfree(job);
		return -ENOMEM;
	}

	ret = copy_from_user(readback, job->result, sizeof(*readback));

	if (unlikely(ret)) {
		mml_pq_err("copy_from_user failed: %d\n", ret);
		kfree(job);
		kfree(readback);
		return -EINVAL;
	}

	new_sub_task = wait_next_sub_task(chan);

	if (!new_sub_task) {
		kfree(job);
		kfree(readback);
		mml_pq_msg("%s Get sub task failed", __func__);
		return -ERESTARTSYS;
	}

	new_pq_task = from_clarity_readback(new_sub_task);
	new_job_id = new_sub_task->job_id;

	ret = copy_to_user(&user_job->new_job_id, &new_job_id, sizeof(u32));

	ret = copy_to_user(&user_job->info, &new_sub_task->frame_data->info,
		sizeof(struct mml_frame_info));
	if (unlikely(ret)) {
		mml_pq_err("err: fail to copy to user frame info: %d\n", ret);
		goto wake_up_clarity_readback_task;
	}

	ret = copy_to_user(&user_job->size_info, &new_sub_task->frame_data->size_info,
		sizeof(struct mml_pq_frame_info));
	if (unlikely(ret)) {
		mml_pq_err("err: fail to copy to user frame size_info: %d\n", ret);
		goto wake_up_clarity_readback_task;
	}

	ret = copy_to_user(user_job->param, new_sub_task->frame_data->pq_param,
		MML_MAX_OUTPUTS * sizeof(struct mml_pq_param));
	if (unlikely(ret)) {
		mml_pq_err("err: fail to copy to user pq param: %d\n", ret);
		goto wake_up_clarity_readback_task;
	}

	readback->is_dual = new_sub_task->readback_data.is_dual;
	readback->cut_pos_x =
		(new_sub_task->frame_data->info.dest[0].crop.r.width / 2) - 1;

	ret = copy_to_user(&job->result->is_dual, &readback->is_dual, sizeof(bool));
	if (unlikely(ret)) {
		mml_pq_err("err: fail to copy to width: %d\n", ret);
		goto wake_up_clarity_readback_task;
	}

	ret = copy_to_user(&job->result->cut_pos_x, &readback->cut_pos_x, sizeof(u32));
	if (unlikely(ret)) {
		mml_pq_err("err: fail to copy to width: %d\n", ret);
		goto wake_up_clarity_readback_task;
	}

	if (new_sub_task->readback_data.pipe0_hist) {
		ret = copy_to_user(readback->clarity_pipe0_hist,
			new_sub_task->readback_data.pipe0_hist,
			(AAL_CLARITY_STATUS_NUM + TDSHP_CLARITY_STATUS_NUM) * sizeof(u32));

		if (new_sub_task->frame_data->info.dest[0].pq_config.en_dre)
			atomic_dec_if_positive(&new_sub_task->readback_data.pipe_cnt);
		if (new_sub_task->frame_data->info.dest[0].pq_config.en_sharp)
			atomic_dec_if_positive(&new_sub_task->readback_data.pipe_cnt);
		if (unlikely(ret)) {
			mml_pq_err("err: fail to copy to hdr_pipe0_hist: %d\n", ret);
			goto wake_up_clarity_readback_task;
		}
	} else {
		mml_pq_err("err: fail to copy to hdr_pipe0_hist because of null pointer");
	}

	if (readback->is_dual && new_sub_task->readback_data.pipe1_hist) {
		ret = copy_to_user(readback->clarity_pipe1_hist,
			new_sub_task->readback_data.pipe1_hist,
			(AAL_CLARITY_STATUS_NUM + TDSHP_CLARITY_STATUS_NUM) * sizeof(u32));

		if (new_sub_task->frame_data->info.dest[0].pq_config.en_dre)
			atomic_dec_if_positive(&new_sub_task->readback_data.pipe_cnt);
		if (new_sub_task->frame_data->info.dest[0].pq_config.en_sharp)
			atomic_dec_if_positive(&new_sub_task->readback_data.pipe_cnt);

		if (unlikely(ret)) {
			mml_pq_err("err: fail to copy to clarity_pipe1_hist: %d\n", ret);
			goto wake_up_clarity_readback_task;
		}
	} else {
		mml_pq_msg("single pipe");
	}

	remove_sub_task(chan, new_sub_task);
	atomic_dec_if_positive(&new_sub_task->queued);
	mml_pq_put_pq_task(new_pq_task);

	dump_sub_task(new_sub_task, new_job_id, readback->is_dual, readback->cut_pos_x,
		HDR_HIST_NUM);
	mml_pq_msg("%s end job_id[%d]\n", __func__, job->new_job_id);
	kfree(job);
	kfree(readback);
	return 0;

wake_up_clarity_readback_task:
	remove_sub_task(chan, new_sub_task);
	atomic_dec_if_positive(&new_sub_task->queued);
	mml_pq_put_pq_task(new_pq_task);
	kfree(job);
	kfree(readback);
	cancel_sub_task(new_sub_task);
	mml_pq_msg("%s end %d\n", __func__, ret);
	return ret;
}

static int mml_pq_dc_readback_ioctl(unsigned long data)
{
	struct mml_pq_chan *chan = &pq_mbox->dc_readback_chan;
	struct mml_pq_sub_task *new_sub_task = NULL;
	struct mml_pq_task *new_pq_task = NULL;
	struct mml_pq_dc_readback_job *job;
	struct mml_pq_dc_readback_job *user_job;
	struct mml_pq_dc_readback_result *readback = NULL;
	u32 new_job_id;
	s32 ret = 0;

	mml_pq_msg("%s called chan[%08lx]", __func__, (unsigned long)chan);
	user_job = (struct mml_pq_dc_readback_job *)data;
	if (unlikely(!user_job))
		return -EINVAL;

	job = kmalloc(sizeof(*job), GFP_KERNEL);
	if (unlikely(!job))
		return -ENOMEM;

	ret = copy_from_user(job, user_job, sizeof(*job));
	if (unlikely(ret)) {
		mml_pq_err("copy_from_user failed: %d\n", ret);
		kfree(job);
		return -EINVAL;
	}

	readback = kmalloc(sizeof(*readback), GFP_KERNEL);
	if (unlikely(!readback)) {
		kfree(job);
		return -ENOMEM;
	}

	ret = copy_from_user(readback, job->result, sizeof(*readback));

	if (unlikely(ret)) {
		mml_pq_err("copy_from_user failed: %d\n", ret);
		kfree(job);
		kfree(readback);
		return -EINVAL;
	}

	new_sub_task = wait_next_sub_task(chan);

	if (!new_sub_task) {
		kfree(job);
		kfree(readback);
		mml_pq_msg("%s Get sub task failed", __func__);
		return -ERESTARTSYS;
	}

	new_pq_task = from_dc_readback(new_sub_task);
	new_job_id = new_sub_task->job_id;

	ret = copy_to_user(&user_job->new_job_id, &new_job_id, sizeof(u32));

	ret = copy_to_user(&user_job->info, &new_sub_task->frame_data->info,
		sizeof(struct mml_frame_info));
	if (unlikely(ret)) {
		mml_pq_err("err: fail to copy to user frame info: %d\n", ret);
		goto wake_up_dc_readback_task;
	}

	ret = copy_to_user(&user_job->size_info, &new_sub_task->frame_data->size_info,
		sizeof(struct mml_pq_frame_info));
	if (unlikely(ret)) {
		mml_pq_err("err: fail to copy to user frame size_info: %d\n", ret);
		goto wake_up_dc_readback_task;
	}

	ret = copy_to_user(user_job->param, new_sub_task->frame_data->pq_param,
		MML_MAX_OUTPUTS * sizeof(struct mml_pq_param));
	if (unlikely(ret)) {
		mml_pq_err("err: fail to copy to user pq param: %d\n", ret);
		goto wake_up_dc_readback_task;
	}

	readback->is_dual = new_sub_task->readback_data.is_dual;
	readback->cut_pos_x =
		(new_sub_task->frame_data->info.dest[0].crop.r.width / 2) - 1;

	ret = copy_to_user(&job->result->is_dual, &readback->is_dual, sizeof(bool));
	if (unlikely(ret)) {
		mml_pq_err("err: fail to copy to width: %d\n", ret);
		goto wake_up_dc_readback_task;
	}

	ret = copy_to_user(&job->result->cut_pos_x, &readback->cut_pos_x, sizeof(u32));
	if (unlikely(ret)) {
		mml_pq_err("err: fail to copy to width: %d\n", ret);
		goto wake_up_dc_readback_task;
	}

	if (new_sub_task->readback_data.pipe0_hist) {
		ret = copy_to_user(readback->dc_pipe0_hist,
			new_sub_task->readback_data.pipe0_hist,
			TDSHP_CONTOUR_HIST_NUM * sizeof(u32));
		atomic_dec_if_positive(&new_sub_task->readback_data.pipe_cnt);
		if (unlikely(ret)) {
			mml_pq_err("err: fail to copy to hdr_pipe0_hist: %d\n", ret);
			goto wake_up_dc_readback_task;
		}
	} else {
		mml_pq_err("err: fail to copy to hdr_pipe0_hist because of null pointer");
	}

	if (readback->is_dual && new_sub_task->readback_data.pipe1_hist) {
		ret = copy_to_user(readback->dc_pipe1_hist,
			new_sub_task->readback_data.pipe1_hist,
			TDSHP_CONTOUR_HIST_NUM * sizeof(u32));
		atomic_dec_if_positive(&new_sub_task->readback_data.pipe_cnt);
		if (unlikely(ret)) {
			mml_pq_err("err: fail to copy to dc_pipe1_hist: %d\n", ret);
			goto wake_up_dc_readback_task;
		}
	} else {
		mml_pq_msg("single pipe");
	}

	remove_sub_task(chan, new_sub_task);
	atomic_dec_if_positive(&new_sub_task->queued);
	mml_pq_put_pq_task(new_pq_task);

	dump_sub_task(new_sub_task, new_job_id, readback->is_dual, readback->cut_pos_x,
		HDR_HIST_NUM);
	mml_pq_msg("%s end job_id[%d]\n", __func__, job->new_job_id);
	kfree(job);
	kfree(readback);
	return 0;

wake_up_dc_readback_task:
	remove_sub_task(chan, new_sub_task);
	atomic_dec_if_positive(&new_sub_task->queued);
	mml_pq_put_pq_task(new_pq_task);
	kfree(job);
	kfree(readback);
	cancel_sub_task(new_sub_task);
	mml_pq_msg("%s end %d\n", __func__, ret);
	return ret;
}

void mml_pq_reset_hist_status(struct mml_task *task)
{
	u32 i = 0;
	task->pq_task->read_status.aal_comp = MML_PQ_HIST_INIT;
	task->pq_task->read_status.hdr_comp = MML_PQ_HIST_INIT;
	task->pq_task->read_status.tdshp_comp = MML_PQ_HIST_INIT;

	for (i = 0; i < MML_PIPE_CNT; i++)
		reinit_completion(&task->pq_task->hdr_curve_ready[i]);
}

static long mml_pq_ioctl(struct file *file, unsigned int cmd,
			  unsigned long arg)
{
	mml_pq_msg("%s called %#x", __func__, cmd);
	mml_pq_msg("%s tile_init=%#lx comp_config=%#lx",
		__func__, (unsigned long)MML_PQ_IOC_TILE_INIT, (unsigned long)MML_PQ_IOC_COMP_CONFIG);
	switch (cmd) {
	case MML_PQ_IOC_TILE_INIT:
		return mml_pq_tile_init_ioctl(arg);
	case MML_PQ_IOC_COMP_CONFIG:
#if IS_ENABLED(CONFIG_MTK_MML_DEBUG)
		mml_pq_msg("%s MML_PQ_IOC_COMP_CONFIG called cmd[%#x]", __func__, cmd);
#endif
		return mml_pq_comp_config_ioctl(arg);
	case MML_PQ_IOC_AAL_READBACK:
		return mml_pq_aal_readback_ioctl(arg);
	case MML_PQ_IOC_HDR_READBACK:
		return mml_pq_hdr_readback_ioctl(arg);
	case MML_PQ_IOC_WROT_CALLBACK:
		return mml_pq_wrot_callback_ioctl(arg);
	case MML_PQ_IOC_CLARITY_READBACK:
		return mml_pq_clarity_readback_ioctl(arg);
	case MML_PQ_IOC_DC_READBACK:
		return mml_pq_dc_readback_ioctl(arg);
	default:
		return -ENOTTY;
	}
}

static long mml_pq_compat_ioctl(struct file *file, unsigned int cmd,
			  unsigned long arg)
{
	mml_pq_msg("%s called %#x", __func__, cmd);
	mml_pq_msg("%s tile_init=%#lx comp_config=%#lx",
		__func__, (unsigned long)MML_PQ_IOC_TILE_INIT, (unsigned long)MML_PQ_IOC_COMP_CONFIG);
	return -EFAULT;
}

const struct file_operations mml_pq_fops = {
	.unlocked_ioctl = mml_pq_ioctl,
	.compat_ioctl	= mml_pq_compat_ioctl,
};

static struct miscdevice mml_pq_dev = {
	.minor	= MISC_DYNAMIC_MINOR,
	.name	= "mml_pq",
	.fops	= &mml_pq_fops,
};

int mml_pq_core_init(void)
{
	s32 ret = 0;
	s32 buf_idx = 0;
	s32 i = 0;


	INIT_LIST_HEAD(&rb_buf_list);
	INIT_LIST_HEAD(&fg_buf_grain_list);
	INIT_LIST_HEAD(&fg_buf_scaling_list);
	mutex_init(&rb_buf_list_mutex);
	mutex_init(&fg_buf_grain_mutex);
	mutex_init(&fg_buf_scaling_mutex);
	mutex_init(&rb_buf_pool_mutex);

	pq_mbox = kzalloc(sizeof(*pq_mbox), GFP_KERNEL);
	if (unlikely(!pq_mbox))
		return -ENOMEM;
	init_pq_chan(&pq_mbox->tile_init_chan);
	init_pq_chan(&pq_mbox->comp_config_chan);
	init_pq_chan(&pq_mbox->aal_readback_chan);
	init_pq_chan(&pq_mbox->hdr_readback_chan);
	init_pq_chan(&pq_mbox->wrot_callback_chan);
	init_pq_chan(&pq_mbox->clarity_readback_chan);
	init_pq_chan(&pq_mbox->dc_readback_chan);
	buffer_num = 0;
	dma_buf_num = 0;

	for (buf_idx = 0; buf_idx < TOTAL_RB_BUF_NUM; buf_idx++)
		rb_buf_pool[buf_idx] = buf_idx*4096;

	for (i = 0; i < MML_MAX_OUTPUTS; i++) {
		dev_data[i] = kzalloc(sizeof(struct mml_pq_dev_data), GFP_KERNEL);
		if (unlikely(!dev_data[i])) {
			mml_pq_err("%s dev_data[%d] allocate failed", __func__, i);
			ret = -ENOMEM;
			continue;
		}
		mutex_init(&dev_data[i]->aal_hist_mutex);
		mutex_init(&dev_data[i]->hdr_hist_mutex);
		mutex_init(&dev_data[i]->tdshp_hist_mutex);
	}

	if (ret < 0)
		return ret;

	ret = misc_register(&mml_pq_dev);
	mml_pq_log("%s result: %d", __func__, ret);
	if (unlikely(ret)) {
		kfree(pq_mbox);
		pq_mbox = NULL;
	}
	return ret;
}

void mml_pq_core_uninit(void)
{
	misc_deregister(&mml_pq_dev);
	kfree(pq_mbox);
	pq_mbox = NULL;
}

static s32 ut_case;
static bool ut_inited;
static struct list_head ut_mml_tasks;
static u32 ut_task_cnt;

static void ut_init(void)
{
	if (ut_inited)
		return;

	INIT_LIST_HEAD(&ut_mml_tasks);
	ut_inited = true;
}

static void destroy_ut_task(struct mml_task *task)
{
	mml_pq_log("destroy mml_task for PQ UT [%lld.%lu]",
		task->end_time.tv_sec, task->end_time.tv_nsec);
	list_del(&task->entry);
	ut_task_cnt--;
	mml_pq_log("[mml] %s: after-- ut_task_cnt[%d]\n", __func__, ut_task_cnt);
	kfree(task->config);
	kfree(task);
}

static int run_ut_task_threaded(void *data)
{
	struct mml_task *task = data;
	struct mml_task *task_check = mml_core_create_task(0);
	s32 ret;

	mml_pq_log("start run mml_task for PQ UT [%lld.%lu]\n",
		task->end_time.tv_sec, task->end_time.tv_nsec);

	if (memcmp(task, task_check, sizeof(struct mml_task)))
		mml_pq_err("task check error");

	task->config = kzalloc(sizeof(struct mml_frame_config), GFP_KERNEL);

	if (!task->config)
		goto exit;

	ret = mml_pq_set_tile_init(task);
	mml_pq_log("tile_init result: %d\n", ret);

	ret = mml_pq_get_tile_init_result(task, 100);
	mml_pq_log("get result: %d\n", ret);

	mml_pq_put_tile_init_result(task);

exit:
	destroy_ut_task(task);
	return 0;
}

static void create_ut_task(const char *case_name)
{
	struct mml_task *task = NULL;
	struct task_struct *thr = NULL;

	if (ut_task_cnt != 0) {
		mml_pq_err("[mml] unexpected ut_task_cnt[%d]\n", ut_task_cnt);
		return;
	}

	task = mml_core_create_task(0);
	mml_pq_log("start create task for %s\n", case_name);
	INIT_LIST_HEAD(&task->entry);
	ktime_get_ts64(&task->end_time);
	list_add_tail(&task->entry, &ut_mml_tasks);
	ut_task_cnt++;
	mml_pq_log("[mml] %s: after++ ut_task_cnt[%d]\n", __func__, ut_task_cnt);
	mml_pq_log("[mml] created mml_task for PQ UT [%lld.%lu]\n",
		task->end_time.tv_sec, task->end_time.tv_nsec);
	thr = kthread_run(run_ut_task_threaded, task, "ut-%s", case_name);
	if (IS_ERR(thr)) {
		mml_pq_err("create thread failed, thread:%s\n", case_name);
		destroy_ut_task(task);
	}
}

static s32 ut_set(const char *val, const struct kernel_param *kp)
{
	s32 result;

	ut_init();
	result = sscanf(val, "%d", &ut_case);
	if (result != 1) {
		mml_pq_err("invalid input: %s, result(%d)\n", val, result);
		return -EINVAL;
	}
	mml_pq_log("[mml] %s: case_id=%d\n", __func__, ut_case);

	switch (ut_case) {
	case 0:
		create_ut_task("basic_pq");
		break;
	default:
		mml_pq_err("invalid case_id: %d\n", ut_case);
		break;
	}

	mml_pq_log("%s END with case %d\n", __func__, ut_case);
	return 0;
}

static s32 ut_get(char *buf, const struct kernel_param *kp)
{
	s32 length = 0;
	u32 i = 0;
	struct mml_task *task;

	ut_init();
	switch (ut_case) {
	case 0:
		length += snprintf(buf + length, PAGE_SIZE - length,
			"current UT task count: %d\n", ut_task_cnt);
		list_for_each_entry(task, &ut_mml_tasks, entry) {
			length += snprintf(buf + length, PAGE_SIZE - length,
				"  - [%d] task submit time: %lld.%lu\n", i,
				task->end_time.tv_sec, task->end_time.tv_nsec);
		}
		break;
	default:
		pr_notice("not support read for case_id: %d\n", ut_case);
		break;
	}
	buf[length] = '\0';

	return length;
}

static struct kernel_param_ops ut_param_ops = {
	.set = ut_set,
	.get = ut_get,
};
module_param_cb(pq_ut_case, &ut_param_ops, NULL, 0644);
MODULE_PARM_DESC(pq_ut_case, "mml pq core UT test case");

MODULE_DESCRIPTION("MTK MML PQ Core Driver");
MODULE_AUTHOR("Aaron Wu<ycw.wu@mediatek.com>");
MODULE_LICENSE("GPL");
