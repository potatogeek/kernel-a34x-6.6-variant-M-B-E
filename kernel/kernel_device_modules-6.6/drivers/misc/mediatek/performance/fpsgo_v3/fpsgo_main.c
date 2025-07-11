// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 MediaTek Inc.
 */
#include <linux/kthread.h>
#include <linux/sched/cputime.h>
#include <sched/sched.h>
#include <linux/string.h>
#include <linux/unistd.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/sched/clock.h>
#include <linux/cpufreq.h>
#include <linux/topology.h>
#include <linux/arch_topology.h>
#include <linux/cpumask.h>
#include <trace/events/power.h>
#include <linux/tracepoint.h>
#include <linux/kallsyms.h>
#include <uapi/linux/sched/types.h>
#include <trace/hooks/cpufreq.h>
#include "sugov/cpufreq.h"

#include "mt-plat/fpsgo_common.h"
#include "fpsgo_base.h"
#include "fpsgo_sysfs.h"
#include "fpsgo_usedext.h"
#include "fpsgo_cpu_policy.h"
#include "fbt_cpu.h"
#include "fstb.h"
#include "fps_composer.h"
#include "xgf.h"
#include "mtk_drm_arr.h"
#include "fbt_cpu_ux.h"
#include "fpsgo_frame_info.h"
#include "fpsgo_adpf.h"

#define CREATE_TRACE_POINTS
#define MAX_MAGT_TARGET_FPS_NUM 10
#define MAX_MAGT_DEP_LIST_NUM 10
#define TARGET_UNLIMITED_FPS 240

enum FPSGO_NOTIFIER_PUSH_TYPE {
	FPSGO_NOTIFIER_SWITCH_FPSGO			= 0x00,
	FPSGO_NOTIFIER_QUEUE_DEQUEUE		= 0x01,
	FPSGO_NOTIFIER_CONNECT				= 0x02,
	FPSGO_NOTIFIER_DFRC_FPS				= 0x03,
	FPSGO_NOTIFIER_BQID				= 0x04,
	FPSGO_NOTIFIER_VSYNC				= 0x05,
	FPSGO_NOTIFIER_SWAP_BUFFER          = 0x06,
	FPSGO_NOTIFIER_SBE_RESCUE           = 0x07,
	FPSGO_NOTIFIER_ACQUIRE              = 0x08,
	FPSGO_NOTIFIER_BUFFER_QUOTA         = 0x09,
	FPSGO_NOTIFIER_MAGT_TARGET_FPS      = 0x0b,
	FPSGO_NOTIFIER_MAGT_DEP_LIST        = 0x0c,
	FPSGO_NOTIFIER_VSYNC_PERIOD         = 0x0d,
	FPSGO_NOTIFIER_FRAME_HINT			= 0x10,
	FPSGO_NOTIFIER_SBE_POLICY			= 0x11,
	FPSGO_NOTIFIER_SBE_BUFFER_COUNT     = 0x12,

};

struct fpsgo_magt_target_fps {
	int pid_arr[MAX_MAGT_TARGET_FPS_NUM];
	int tid_arr[MAX_MAGT_TARGET_FPS_NUM];
	int tfps_arr[MAX_MAGT_TARGET_FPS_NUM];
	int num;
};

struct fpsgo_magt_dep_list {
	int pid;
	struct dep_and_prio dep_task_arr[MAX_MAGT_DEP_LIST_NUM];
	int dep_task_num;
};

struct fpsgo_magt_l2q_time {
	int pid;
	unsigned int frameid;
	unsigned int type;
	unsigned int status;
	unsigned long long tv_ts;
};

/* TODO: use union*/
struct FPSGO_NOTIFIER_PUSH_TAG {
	enum FPSGO_NOTIFIER_PUSH_TYPE ePushType;

	int pid;
	unsigned long long cur_ts;

	int enable;

	int qudeq_cmd;
	unsigned int queue_arg;

	unsigned long long bufID;
	int connectedAPI;
	int queue_SF;
	unsigned long long identifier;
	int create;

	int dfrc_fps;
	int buffer_quota;

	int enhance;
	int rescue_type;
	long long frameID;
	long long frame_flags;
	unsigned long long rescue_target;
	struct list_head queue_list;

	int consumer_pid;
	int consumer_tid;
	int producer_pid;

	char name[16];
	unsigned long mask;
	char specific_name[1000];
	int num;
	int mode;

	unsigned long long sf_buf_id;
	unsigned int frameid;
	unsigned int type;
	unsigned int status;
	unsigned long long tv_ts;
	unsigned long long que_end_sys_time_ns;

	struct fpsgo_magt_target_fps *magt_tfps_hint;
	struct fpsgo_magt_dep_list *magt_dep_hint;
};

static struct mutex notify_lock;
static struct task_struct *kfpsgo_tsk;
static int fpsgo_enable;
static int fpsgo_force_onoff;
static int perfserv_ta;
static int sbe2fpsgo_query_is_running;
static bool cam_status;

int powerhal_tid;

#if !IS_ENABLED(CONFIG_ARM64)
int cap_ready;
#endif

/* TODO: event register & dispatch */
int fpsgo_is_enable(void)
{
	int enable;

	mutex_lock(&notify_lock);
	enable = fpsgo_enable;
	mutex_unlock(&notify_lock);

	FPSGO_LOGI("[FPSGO_CTRL] isenable %d\n", enable);
	return enable;
}

static void fpsgo_notifier_wq_cb_vsync(unsigned long long ts)
{
	FPSGO_LOGI("[FPSGO_CB] vsync: %llu\n", ts);

	if (!fpsgo_is_enable())
		return;

	fpsgo_ctrl2fbt_vsync(ts);
}

static void fpsgo_notifier_wq_cb_vsync_period(unsigned long long period_ts)
{
	FPSGO_LOGI("[FPSGO_CB] vsync: %llu\n", period_ts);

	if (!fpsgo_is_enable())
		return;

	fpsgo_ctrl2fbt_vsync_period(period_ts);
}

static void fpsgo_notifier_wq_cb_swap_buffer(int pid)
{
	FPSGO_LOGI("[FPSGO_CB] swap_buffer: %d\n", pid);

	if (!fpsgo_is_enable())
		return;

	fpsgo_update_swap_buffer(pid);
}

static void fpsgo_notifier_wq_cb_sbe_rescue(int pid, int start, int enhance,
		int rescue_type, unsigned long long rescue_target, unsigned long long frameID)
{
	FPSGO_LOGI("[FPSGO_CB] sbe_rescue: %d\n", pid);
	if (!fpsgo_is_enable())
		return;
	fpsgo_sbe_rescue_traverse(pid, start, enhance, rescue_type, rescue_target, frameID);
}

static void fpsgo_notifier_wq_cb_buffer_quota(unsigned long long curr_ts,
		int pid, int quota, unsigned long long id)
{
	FPSGO_LOGI("[FPSGO_CB] buffer_quota: ts %llu, pid %d,quota %d, id %llu\n",
		curr_ts, pid, quota, id);
	if (!fpsgo_is_enable())
		return;
	fpsgo_ctrl2fbt_buffer_quota(curr_ts, pid, quota, id);
}


static void fpsgo_notifier_wq_cb_dfrc_fps(int dfrc_fps)
{
	FPSGO_LOGI("[FPSGO_CB] dfrc_fps %d\n", dfrc_fps);

	fpsgo_ctrl2fstb_dfrc_fps(dfrc_fps);
	fpsgo_ctrl2fbt_dfrc_fps(dfrc_fps);
	fpsgo_ctrl2uxfbt_dfrc_fps(dfrc_fps);
}

static void fpsgo_notifier_wq_cb_connect(int pid,
		int connectedAPI, unsigned long long id)
{
	FPSGO_LOGI(
		"[FPSGO_CB] connect: pid %d, API %d, id %llu\n",
		pid, connectedAPI, id);

	if (connectedAPI == WINDOW_DISCONNECT)
		fpsgo_ctrl2comp_disconnect_api(pid, connectedAPI, id);
	else
		fpsgo_ctrl2comp_connect_api(pid, connectedAPI, id);
}

static void fpsgo_notifier_wq_cb_bqid(int pid, unsigned long long bufID,
	int queue_SF, unsigned long long id, int create)
{
	FPSGO_LOGI(
		"[FPSGO_CB] bqid: pid %d, bufID %llu, queue_SF %d, id %llu, create %d\n",
		pid, bufID, queue_SF, id, create);

	fpsgo_ctrl2comp_bqid(pid, bufID, queue_SF, id, create);
}

static void fpsgo_notify_wq_cb_acquire(int consumer_pid, int consumer_tid,
	int producer_pid, int connectedAPI, unsigned long long buffer_id,
	unsigned long long ts)
{
	FPSGO_LOGI(
		"[FPSGO_CB] acquire: p_pid %d, c_pid:%d, c_tid:%d, api:%d, bufID:0x%llx\n",
		producer_pid, consumer_pid, consumer_tid, connectedAPI, buffer_id);

	if (!fpsgo_is_enable())
		return;

	if (!fpsgo_get_acquire_hint_is_enable())
		return;

	fpsgo_ctrl2comp_acquire(producer_pid, consumer_pid, consumer_tid,
		connectedAPI, buffer_id, ts);
}

static void fpsgo_notifier_wq_cb_qudeq(int qudeq,
		unsigned int startend, int cur_pid,
		unsigned long long curr_ts, unsigned long long id,
		unsigned long long sf_buf_id)
{
	FPSGO_LOGI("[FPSGO_CB] qudeq: %d-%d, pid %d, ts %llu, id %llu\n",
		qudeq, startend, cur_pid, curr_ts, id);

	if (!fpsgo_is_enable())
		return;

	switch (qudeq) {
	case 1:
		if (startend) {
			FPSGO_LOGI("[FPSGO_CB] QUEUE Start: pid %d\n",
					cur_pid);
			fpsgo_ctrl2comp_enqueue_start(cur_pid,
					curr_ts, id);
		} else {
			FPSGO_LOGI("[FPSGO_CB] QUEUE End: pid %d\n",
					cur_pid);
			fpsgo_ctrl2comp_enqueue_end(cur_pid, curr_ts,
					id, sf_buf_id);
		}
		break;
	case 0:
		if (startend) {
			FPSGO_LOGI("[FPSGO_CB] DEQUEUE Start: pid %d\n",
					cur_pid);
			fpsgo_ctrl2comp_dequeue_start(cur_pid,
					curr_ts, id);
		} else {
			FPSGO_LOGI("[FPSGO_CB] DEQUEUE End: pid %d\n",
					cur_pid);
			fpsgo_ctrl2comp_dequeue_end(cur_pid,
					curr_ts, id);
		}
		break;
	default:
		break;
	}
}

static void fpsgo_notifier_wq_cb_enable(int enable)
{
	FPSGO_LOGI(
	"[FPSGO_CB] enable %d, fpsgo_enable %d, force_onoff %d\n",
	enable, fpsgo_enable, fpsgo_force_onoff);

	mutex_lock(&notify_lock);
	if (enable == fpsgo_enable) {
		mutex_unlock(&notify_lock);
		return;
	}

	if (fpsgo_force_onoff != FPSGO_FREE &&
			enable != fpsgo_force_onoff) {
		mutex_unlock(&notify_lock);
		return;
	}

	fpsgo_ctrl2fbt_switch_fbt(enable);
	fpsgo_ctrl2fstb_switch_fstb(enable);
	fpsgo_ctrl2xgf_switch_xgf(enable);

	fpsgo_enable = enable;

	if (!fpsgo_enable)
		fpsgo_clear();

	FPSGO_LOGI("[FPSGO_CB] fpsgo_enable %d\n",
			fpsgo_enable);
	mutex_unlock(&notify_lock);

	if (!enable)
		fpsgo_com_notify_fpsgo_is_boost(0);
}

static void fpsgo_notifier_wq_cb_hint_frame(int qudeq,
		int cur_pid, unsigned long long frameID,
		unsigned long long curr_ts, unsigned long long id,
		int dep_mode, char *dep_name, int dep_num, long long frame_flags)
{
	FPSGO_LOGI("[FPSGO_CB] uxframe: %d, pid %d, ts %llu, id %llu\n",
		qudeq, cur_pid, curr_ts, id);

	if (!fpsgo_is_enable())
		return;

	switch (qudeq) {
	case -1:
		FPSGO_LOGI("[FPSGO_CB] uxframe UX err (no queue): pid %d\n",
				cur_pid);
		fpsgo_ctrl2comp_hint_frame_err(cur_pid, frameID, curr_ts, id);
		break;
	case 0:
		FPSGO_LOGI("[FPSGO_CB] uxframe UX Start: pid %d\n",
				cur_pid);
		fpsgo_ctrl2comp_hint_frame_start(cur_pid, frameID, curr_ts, id);
		break;
	case 1:
		FPSGO_LOGI("[FPSGO_CB] uxframe UX End: pid %d\n",
				cur_pid);
		fpsgo_ctrl2comp_hint_frame_end(cur_pid, frameID, curr_ts, id);
		break;
	case 2:
		FPSGO_LOGI("[FPSGO_CB] uxframe UX doframe End: pid %d\n",
				cur_pid);
		fpsgo_ctrl2comp_hint_doframe_end(cur_pid, frameID, curr_ts, id, frame_flags);
		break;
	default:
		break;
	}

	if (dep_mode && dep_num > 0)
		fpsgo_ctrl2comp_hint_frame_dep_task(cur_pid, id,
			dep_mode, dep_name, dep_num);

}

static void fpsgo_notifier_wq_cb_hint_buffer(int pid, int count, int max_count)
{
	if (!fpsgo_is_enable())
		return;

	FPSGO_LOGI("[FPSGO_CB] uxbuffer pid %d, cur_count %d, max_count %d\n",
			pid, count, max_count);
	fpsgo_ctrl2comp_hint_buffer_count(pid, count, max_count);
}

static void fpsgo_notifier_wq_cb_magt_target_fps(struct fpsgo_magt_target_fps *iter)
{
	if (!iter || !fpsgo_is_enable())
		return;

	fpsgo_ctrl2fstb_magt_set_target_fps(iter->pid_arr, iter->tid_arr, iter->tfps_arr, iter->num);
	fpsgo_free(iter, sizeof(struct fpsgo_magt_target_fps));
}

static void fpsgo_notifier_wq_cb_magt_dep_list(struct fpsgo_magt_dep_list *iter)
{
	if (!iter || !fpsgo_is_enable())
		return;

	fpsgo_ctrl2xgf_magt_set_dep_list(iter->pid, iter->dep_task_arr, iter->dep_task_num, XGF_ADD_DEP);
	fpsgo_free(iter, sizeof(struct fpsgo_magt_dep_list));
}

static LIST_HEAD(head);
static int condition_notifier_wq;
static DEFINE_MUTEX(notifier_wq_lock);
static DECLARE_WAIT_QUEUE_HEAD(notifier_wq_queue);
static void fpsgo_queue_work(struct FPSGO_NOTIFIER_PUSH_TAG *vpPush)
{
	mutex_lock(&notifier_wq_lock);
	list_add_tail(&vpPush->queue_list, &head);
	condition_notifier_wq = 1;
	mutex_unlock(&notifier_wq_lock);

	wake_up_interruptible(&notifier_wq_queue);
}

static void fpsgo_notifier_wq_cb(void)
{
	struct FPSGO_NOTIFIER_PUSH_TAG *vpPush;

	wait_event_interruptible(notifier_wq_queue, condition_notifier_wq);
	mutex_lock(&notifier_wq_lock);

	if (!list_empty(&head)) {
		vpPush = list_first_entry(&head,
			struct FPSGO_NOTIFIER_PUSH_TAG, queue_list);
		list_del(&vpPush->queue_list);
		if (list_empty(&head))
			condition_notifier_wq = 0;
		mutex_unlock(&notifier_wq_lock);
	} else {
		condition_notifier_wq = 0;
		mutex_unlock(&notifier_wq_lock);
		return;
	}
	switch (vpPush->ePushType) {
	case FPSGO_NOTIFIER_SWITCH_FPSGO:
		fpsgo_notifier_wq_cb_enable(vpPush->enable);
		break;
	case FPSGO_NOTIFIER_QUEUE_DEQUEUE:
		fpsgo_notifier_wq_cb_qudeq(vpPush->qudeq_cmd,
				vpPush->queue_arg, vpPush->pid,
				vpPush->cur_ts, vpPush->identifier, vpPush->sf_buf_id);
		break;
	case FPSGO_NOTIFIER_CONNECT:
		fpsgo_notifier_wq_cb_connect(vpPush->pid,
				vpPush->connectedAPI, vpPush->identifier);
		break;
	case FPSGO_NOTIFIER_DFRC_FPS:
		fpsgo_notifier_wq_cb_dfrc_fps(vpPush->dfrc_fps);
		break;
	case FPSGO_NOTIFIER_BQID:
		fpsgo_notifier_wq_cb_bqid(vpPush->pid, vpPush->bufID,
			vpPush->queue_SF, vpPush->identifier, vpPush->create);
		break;
	case FPSGO_NOTIFIER_VSYNC:
		fpsgo_notifier_wq_cb_vsync(vpPush->cur_ts);
		break;
	case FPSGO_NOTIFIER_VSYNC_PERIOD:
		fpsgo_notifier_wq_cb_vsync_period(vpPush->cur_ts);
		break;
	case FPSGO_NOTIFIER_SWAP_BUFFER:
		fpsgo_notifier_wq_cb_swap_buffer(vpPush->pid);
		break;
	case FPSGO_NOTIFIER_SBE_RESCUE:
		fpsgo_notifier_wq_cb_sbe_rescue(vpPush->pid, vpPush->enable, vpPush->enhance,
						vpPush->rescue_type, vpPush->rescue_target, vpPush->frameID);
		break;
	case FPSGO_NOTIFIER_ACQUIRE:
		fpsgo_notify_wq_cb_acquire(vpPush->consumer_pid,
			vpPush->consumer_tid, vpPush->producer_pid,
			vpPush->connectedAPI, vpPush->bufID, vpPush->cur_ts);
		break;
	case FPSGO_NOTIFIER_BUFFER_QUOTA:
		fpsgo_notifier_wq_cb_buffer_quota(vpPush->cur_ts,
				vpPush->pid,
				vpPush->buffer_quota,
				vpPush->identifier);
		break;
	case FPSGO_NOTIFIER_FRAME_HINT:
		fpsgo_notifier_wq_cb_hint_frame(vpPush->qudeq_cmd,
				vpPush->pid, vpPush->frameID,
				vpPush->cur_ts, vpPush->identifier,
				vpPush->mode, vpPush->specific_name, vpPush->num, vpPush->frame_flags);
		break;
	case FPSGO_NOTIFIER_SBE_POLICY:
		fpsgo_ctrl2comp_set_sbe_policy(vpPush->pid,
				vpPush->name, vpPush->mask,
				vpPush->cur_ts, vpPush->qudeq_cmd,
				vpPush->specific_name, vpPush->num);
		break;
	case FPSGO_NOTIFIER_SBE_BUFFER_COUNT:
		fpsgo_notifier_wq_cb_hint_buffer(vpPush->pid, vpPush->num, vpPush->mode);
		break;
	case FPSGO_NOTIFIER_MAGT_TARGET_FPS:
		fpsgo_notifier_wq_cb_magt_target_fps(vpPush->magt_tfps_hint);
		break;
	case FPSGO_NOTIFIER_MAGT_DEP_LIST:
		fpsgo_notifier_wq_cb_magt_dep_list(vpPush->magt_dep_hint);
		break;
	default:
		FPSGO_LOGE("[FPSGO_CTRL] unhandled push type = %d\n",
				vpPush->ePushType);
		break;
	}
	fpsgo_free(vpPush, sizeof(struct FPSGO_NOTIFIER_PUSH_TAG));

}

int fpsgo_get_kfpsgo_tid(void)
{
	return kfpsgo_tsk ? kfpsgo_tsk->pid : 0;
}
EXPORT_SYMBOL(fpsgo_get_kfpsgo_tid);

static int kfpsgo(void *arg)
{
	struct sched_attr attr = {};

	attr.sched_policy = -1;
	attr.sched_flags =
		SCHED_FLAG_KEEP_ALL |
		SCHED_FLAG_UTIL_CLAMP |
		SCHED_FLAG_RESET_ON_FORK;
	attr.sched_util_min = 1;
	attr.sched_util_max = 1024;
	if (sched_setattr_nocheck(current, &attr) != 0)
		FPSGO_LOGE("[FPSGO_CTRL] %s set uclamp fail\n", __func__);

	set_user_nice(current, -20);

	while (!kthread_should_stop())
		fpsgo_notifier_wq_cb();

	return 0;
}
int fpsgo_notify_qudeq(int qudeq,
		unsigned int startend,
		int pid, unsigned long long id, unsigned long long sf_buf_id)
{
	unsigned long long cur_ts;
	struct FPSGO_NOTIFIER_PUSH_TAG *vpPush;

	FPSGO_LOGI("[FPSGO_CTRL] qudeq %d-%d, id %llu pid %d\n",
		qudeq, startend, id, pid);

	if (!fpsgo_is_enable())
		return 0;

	vpPush =
		(struct FPSGO_NOTIFIER_PUSH_TAG *)
		fpsgo_alloc_atomic(sizeof(struct FPSGO_NOTIFIER_PUSH_TAG));
	if (!vpPush) {
		FPSGO_LOGE("[FPSGO_CTRL] OOM\n");
		return 0;
	}

	if (!kfpsgo_tsk) {
		FPSGO_LOGE("[FPSGO_CTRL] NULL WorkQueue\n");
		fpsgo_free(vpPush, sizeof(struct FPSGO_NOTIFIER_PUSH_TAG));
		return 0;
	}

	cur_ts = fpsgo_get_time();

	vpPush->ePushType = FPSGO_NOTIFIER_QUEUE_DEQUEUE;
	vpPush->pid = pid;
	vpPush->cur_ts = cur_ts;
	vpPush->qudeq_cmd = qudeq;
	vpPush->queue_arg = startend;
	vpPush->identifier = id;
	vpPush->sf_buf_id = sf_buf_id;

	fpsgo_queue_work(vpPush);

#if !IS_ENABLED(CONFIG_ARM64)
	if (!cap_ready) {
		fbt_update_pwr_tbl();
		cap_ready = 1;
	}
#endif

	return FPSGO_VERSION_CODE;
}
void fpsgo_notify_connect(int pid,
		int connectedAPI, unsigned long long id)
{
	struct FPSGO_NOTIFIER_PUSH_TAG *vpPush;

	FPSGO_LOGI(
		"[FPSGO_CTRL] connect pid %d, id %llu, API %d\n",
		pid, id, connectedAPI);

	vpPush =
		(struct FPSGO_NOTIFIER_PUSH_TAG *)
		fpsgo_alloc_atomic(sizeof(struct FPSGO_NOTIFIER_PUSH_TAG));

	if (!vpPush) {
		FPSGO_LOGE("[FPSGO_CTRL] OOM\n");
		return;
	}

	if (!kfpsgo_tsk) {
		FPSGO_LOGE("[FPSGO_CTRL] NULL WorkQueue\n");
		fpsgo_free(vpPush, sizeof(struct FPSGO_NOTIFIER_PUSH_TAG));
		return;
	}

	vpPush->ePushType = FPSGO_NOTIFIER_CONNECT;
	vpPush->pid = pid;
	vpPush->connectedAPI = connectedAPI;
	vpPush->identifier = id;

	fpsgo_queue_work(vpPush);
}

void fpsgo_notify_bqid(int pid, unsigned long long bufID,
	int queue_SF, unsigned long long id, int create)
{
	struct FPSGO_NOTIFIER_PUSH_TAG *vpPush;

	FPSGO_LOGI("[FPSGO_CTRL] bqid pid %d, buf %llu, queue_SF %d, id %llu\n",
		pid, bufID, queue_SF, id);

	vpPush = (struct FPSGO_NOTIFIER_PUSH_TAG *)
		fpsgo_alloc_atomic(sizeof(struct FPSGO_NOTIFIER_PUSH_TAG));

	if (!vpPush) {
		FPSGO_LOGE("[FPSGO_CTRL] OOM\n");
		return;
	}

	if (!kfpsgo_tsk) {
		FPSGO_LOGE("[FPSGO_CTRL] NULL WorkQueue\n");
		fpsgo_free(vpPush, sizeof(struct FPSGO_NOTIFIER_PUSH_TAG));
		return;
	}

	vpPush->ePushType = FPSGO_NOTIFIER_BQID;
	vpPush->pid = pid;
	vpPush->bufID = bufID;
	vpPush->queue_SF = queue_SF;
	vpPush->identifier = id;
	vpPush->create = create;

	fpsgo_queue_work(vpPush);
}

void fpsgo_notify_acquire(int consumer_pid, int producer_pid,
	int connectedAPI, unsigned long long buffer_id)
{
	struct FPSGO_NOTIFIER_PUSH_TAG *vpPush;

	vpPush = (struct FPSGO_NOTIFIER_PUSH_TAG *)
		fpsgo_alloc_atomic(sizeof(struct FPSGO_NOTIFIER_PUSH_TAG));

	if (!vpPush) {
		FPSGO_LOGE("[FPSGO_CTRL] OOM\n");
		return;
	}

	if (!kfpsgo_tsk) {
		FPSGO_LOGE("[FPSGO_CTRL] NULL WorkQueue\n");
		fpsgo_free(vpPush, sizeof(struct FPSGO_NOTIFIER_PUSH_TAG));
		return;
	}

	vpPush->ePushType = FPSGO_NOTIFIER_ACQUIRE;
	vpPush->consumer_pid = consumer_pid;
	vpPush->consumer_tid = current->pid;
	vpPush->producer_pid = producer_pid;
	vpPush->connectedAPI = connectedAPI;
	vpPush->bufID = buffer_id;
	vpPush->cur_ts = fpsgo_get_time();

	fpsgo_queue_work(vpPush);
}

int fpsgo_perfserv_ta_value(void)
{
	int value;

	mutex_lock(&notify_lock);
	value = perfserv_ta;
	mutex_unlock(&notify_lock);

	return value;
}

void fpsgo_set_perfserv_ta(int value)
{
	mutex_lock(&notify_lock);
	perfserv_ta = value;
	mutex_unlock(&notify_lock);
	fpsgo_ctrl2fbt_switch_uclamp(!value);
}


void fpsgo_notify_vsync(void)
{
	struct FPSGO_NOTIFIER_PUSH_TAG *vpPush;

	FPSGO_LOGI("[FPSGO_CTRL] vsync\n");

	if (!fpsgo_is_enable())
		return;

	vpPush = (struct FPSGO_NOTIFIER_PUSH_TAG *)
		fpsgo_alloc_atomic(sizeof(struct FPSGO_NOTIFIER_PUSH_TAG));

	if (!vpPush) {
		FPSGO_LOGE("[FPSGO_CTRL] OOM\n");
		return;
	}

	if (!kfpsgo_tsk) {
		FPSGO_LOGE("[FPSGO_CTRL] NULL WorkQueue\n");
		fpsgo_free(vpPush, sizeof(struct FPSGO_NOTIFIER_PUSH_TAG));
		return;
	}

	vpPush->ePushType = FPSGO_NOTIFIER_VSYNC;
	vpPush->cur_ts = fpsgo_get_time();

	fpsgo_queue_work(vpPush);
}

void fpsgo_notify_vsync_period(unsigned long long period)
{
	struct FPSGO_NOTIFIER_PUSH_TAG *vpPush;

	FPSGO_LOGI("[FPSGO_CTRL] vsync period\n");

	if (!fpsgo_is_enable())
		return;

	vpPush = (struct FPSGO_NOTIFIER_PUSH_TAG *)
		fpsgo_alloc_atomic(sizeof(struct FPSGO_NOTIFIER_PUSH_TAG));

	if (!vpPush) {
		FPSGO_LOGE("[FPSGO_CTRL] OOM\n");
		return;
	}

	if (!kfpsgo_tsk) {
		FPSGO_LOGE("[FPSGO_CTRL] NULL WorkQueue\n");
		fpsgo_free(vpPush, sizeof(struct FPSGO_NOTIFIER_PUSH_TAG));
		return;
	}

	vpPush->ePushType = FPSGO_NOTIFIER_VSYNC_PERIOD;
	vpPush->cur_ts = period;

	fpsgo_queue_work(vpPush);
}


void fpsgo_notify_swap_buffer(int pid)
{
	struct FPSGO_NOTIFIER_PUSH_TAG *vpPush;

	FPSGO_LOGI("[FPSGO_CTRL] swap_buffer\n");

	if (!fpsgo_is_enable())
		return;

	vpPush = (struct FPSGO_NOTIFIER_PUSH_TAG *)
		fpsgo_alloc_atomic(sizeof(struct FPSGO_NOTIFIER_PUSH_TAG));

	if (!vpPush) {
		FPSGO_LOGE("[FPSGO_CTRL] OOM\n");
		return;
	}

	if (!kfpsgo_tsk) {
		FPSGO_LOGE("[FPSGO_CTRL] NULL WorkQueue\n");
		fpsgo_free(vpPush, sizeof(struct FPSGO_NOTIFIER_PUSH_TAG));
		return;
	}

	vpPush->ePushType = FPSGO_NOTIFIER_SWAP_BUFFER;
	vpPush->pid = pid;

	fpsgo_queue_work(vpPush);
}

void fpsgo_notify_sbe_rescue(int pid, int start, int enhance,
		int rescue_type, unsigned long long rescue_target, unsigned long long frameID)
{
	struct FPSGO_NOTIFIER_PUSH_TAG *vpPush;

	FPSGO_LOGI("[FPSGO_CTRL] sbe_rescue\n");

	if (!fpsgo_is_enable())
		return;

	vpPush = (struct FPSGO_NOTIFIER_PUSH_TAG *)
		fpsgo_alloc_atomic(sizeof(struct FPSGO_NOTIFIER_PUSH_TAG));

	if (!vpPush) {
		FPSGO_LOGE("[FPSGO_CTRL] OOM\n");
		return;
	}

	if (!kfpsgo_tsk) {
		FPSGO_LOGE("[FPSGO_CTRL] NULL WorkQueue\n");
		fpsgo_free(vpPush, sizeof(struct FPSGO_NOTIFIER_PUSH_TAG));
		return;
	}

	vpPush->ePushType = FPSGO_NOTIFIER_SBE_RESCUE;
	vpPush->pid = pid;
	vpPush->enable = start;
	vpPush->enhance = enhance;
	vpPush->rescue_type = rescue_type;
	vpPush->rescue_target = rescue_target;
	vpPush->frameID = frameID;

	fpsgo_queue_work(vpPush);
}

void fpsgo_get_fps(int *pid, int *fps)
{
	//int pid = -1, fps = -1;
	if (unlikely(powerhal_tid == 0))
		powerhal_tid = current->pid;

	fpsgo_ctrl2fstb_get_fps(pid, fps);

	FPSGO_LOGI("[FPSGO_CTRL] get_fps %d %d\n", *pid, *fps);

	//return fps;
}

void fpsgo_get_cmd(int *cmd, int *value1, int *value2)
{
	int _cmd = -1, _value1 = -1, _value2 = -1;

	fpsgo_ctrl2base_get_pwr_cmd(&_cmd, &_value1, &_value2);


	FPSGO_LOGI("[FPSGO_CTRL] get_cmd %d %d %d\n", _cmd, _value1, _value2);
	*cmd = _cmd;
	*value1 = _value1;
	*value2 = _value2;

}

int fpsgo_get_fstb_active(long long time_diff)
{
	return is_fstb_active(time_diff);
}

int fpsgo_wait_fstb_active(void)
{
	return fpsgo_ctrl2fstb_wait_fstb_active();
}

bool get_cam_status_for_task_turbo(void)
{
	return cam_status;
}
EXPORT_SYMBOL(get_cam_status_for_task_turbo);

void fpsgo_get_pid(int cmd, int *pid, int value1, int value2)
{
	unsigned long long cur_ts;

	if (!pid)
		return;

	switch (cmd) {
	case CAMERA_CLOSE:
		fpsgo_ctrl2base_notify_cam_close();
		cam_status = false;
		break;
	case CAMERA_APK:
		cam_status = true;
		fallthrough;
	case CAMERA_SERVER:
		fpsgo_ctrl2base_get_cam_pid(cmd, pid);
		break;
	case CAMERA_DO_FRAME:
		cur_ts = fpsgo_get_time();
		fpsgo_ctrl2fstb_cam_queue_time_update(cur_ts);
		break;
	case CAMERA_APP_MIN_FPS:
		cur_ts = fpsgo_get_time();
		fpsgo_ctrl2comp_set_app_meta_fps(value1, value2, cur_ts);
		break;
	case CAMERA_APP_SELF_CTRL:
		cur_ts = fpsgo_get_time();
		*pid = (int)fpsgo_ctrl2base_get_app_self_ctrl_time(value1, cur_ts);
		break;
	default:
		FPSGO_LOGE("[FPSGO_CTRL] wrong cmd:%d\n", cmd);
		break;
	}
}

void fpsgo_notify_buffer_quota(int pid, int quota, unsigned long long identifier)
{
	unsigned long long cur_ts;
	struct FPSGO_NOTIFIER_PUSH_TAG *vpPush;

	FPSGO_LOGI("[FPSGO_CTRL] buffer_quota %d, pid %d, id %llu\n",
		quota, pid, identifier);

	if (!fpsgo_is_enable())
		return;

	vpPush =
		(struct FPSGO_NOTIFIER_PUSH_TAG *)
		fpsgo_alloc_atomic(sizeof(struct FPSGO_NOTIFIER_PUSH_TAG));
	if (!vpPush) {
		FPSGO_LOGE("[FPSGO_CTRL] OOM\n");
		return;
	}

	if (!kfpsgo_tsk) {
		FPSGO_LOGE("[FPSGO_CTRL] NULL WorkQueue\n");
		fpsgo_free(vpPush, sizeof(struct FPSGO_NOTIFIER_PUSH_TAG));
		return;
	}

	cur_ts = fpsgo_get_time();

	vpPush->ePushType = FPSGO_NOTIFIER_BUFFER_QUOTA;
	vpPush->cur_ts = cur_ts;
	vpPush->pid = pid;
	vpPush->buffer_quota = quota;
	vpPush->identifier = identifier;

	fpsgo_queue_work(vpPush);
}

int fpsgo_notify_frame_hint(int qudeq,
	int pid, int frameID, unsigned long long id,
	int dep_mode, char *dep_name, int dep_num, long long frame_flags)
{
	int ret;
	unsigned long long cur_ts;
	struct FPSGO_NOTIFIER_PUSH_TAG *vpPush;

	FPSGO_LOGI("[FPSGO_CTRL] ux_qudeq %d, id %llu pid %d\n",
		qudeq, id, pid);

	if (!fpsgo_is_enable())
		return -EINVAL;

	vpPush =
		(struct FPSGO_NOTIFIER_PUSH_TAG *)
		fpsgo_alloc_atomic(sizeof(struct FPSGO_NOTIFIER_PUSH_TAG));
	if (!vpPush) {
		FPSGO_LOGE("[FPSGO_CTRL] OOM\n");
		return -ENOMEM;
	}

	if (!kfpsgo_tsk) {
		FPSGO_LOGE("[FPSGO_CTRL] NULL WorkQueue\n");
		fpsgo_free(vpPush, sizeof(struct FPSGO_NOTIFIER_PUSH_TAG));
		return -ENOMEM;
	}

	cur_ts = fpsgo_get_time();

	vpPush->ePushType = FPSGO_NOTIFIER_FRAME_HINT;
	vpPush->pid = pid;
	vpPush->cur_ts = cur_ts;
	vpPush->qudeq_cmd = qudeq;
	vpPush->frameID = frameID;
	vpPush->frame_flags = frame_flags;
	// FPSGO UX: bufid magic number.
	vpPush->identifier = 5566;
	vpPush->mode = dep_mode;
	if (dep_mode && dep_num > 0) {
		memcpy(vpPush->specific_name, dep_name, 1000);
		vpPush->num = dep_num;
	} else
		vpPush->num = 0;

	fpsgo_queue_work(vpPush);

	ret = fpsgo_ctrl2ux_get_perf();

	return ret;
}

int fpsgo_notify_sbe_policy(int pid, char *name, unsigned long mask,
	int start, char *specific_name, int num)
{
	unsigned long long cur_ts;
	struct FPSGO_NOTIFIER_PUSH_TAG *vpPush;

	if (!fpsgo_is_enable())
		return -EINVAL;

	vpPush =
		(struct FPSGO_NOTIFIER_PUSH_TAG *)
		fpsgo_alloc_atomic(sizeof(struct FPSGO_NOTIFIER_PUSH_TAG));
	if (!vpPush) {
		FPSGO_LOGE("[FPSGO_CTRL] OOM\n");
		return -ENOMEM;
	}

	if (!kfpsgo_tsk) {
		FPSGO_LOGE("[FPSGO_CTRL] NULL WorkQueue\n");
		fpsgo_free(vpPush, sizeof(struct FPSGO_NOTIFIER_PUSH_TAG));
		return -ENOMEM;
	}

	if (test_bit(FPSGO_RUNNING_QUERY, &mask)) {
		fpsgo_free(vpPush, sizeof(struct FPSGO_NOTIFIER_PUSH_TAG));
		sbe2fpsgo_query_is_running = fpsgo_ctrl2base_query_sbe_spid_loading();
		return sbe2fpsgo_query_is_running ? 10001 : 0;
	}

	cur_ts = fpsgo_get_time();

	vpPush->ePushType = FPSGO_NOTIFIER_SBE_POLICY;
	vpPush->pid = pid;
	vpPush->cur_ts = cur_ts;
	vpPush->mask = mask;
	vpPush->qudeq_cmd = start;
	memcpy(vpPush->name, name, 16);
	memcpy(vpPush->specific_name, specific_name, 1000);
	vpPush->num = num;
	fpsgo_queue_work(vpPush);

	return 0;
}

int fpsgo_notify_ux_buffer_count(int pid, int count, int maxCount)
{
	struct FPSGO_NOTIFIER_PUSH_TAG *vpPush;

	if (!fpsgo_is_enable())
		return -EINVAL;

	vpPush =
		(struct FPSGO_NOTIFIER_PUSH_TAG *)
		fpsgo_alloc_atomic(sizeof(struct FPSGO_NOTIFIER_PUSH_TAG));
	if (!vpPush) {
		FPSGO_LOGE("[FPSGO_CTRL] OOM\n");
		return -ENOMEM;
	}

	if (!kfpsgo_tsk) {
		FPSGO_LOGE("[FPSGO_CTRL] NULL WorkQueue\n");
		fpsgo_free(vpPush, sizeof(struct FPSGO_NOTIFIER_PUSH_TAG));
		return -ENOMEM;
	}

	vpPush->ePushType = FPSGO_NOTIFIER_SBE_BUFFER_COUNT;
	vpPush->pid = pid;
	vpPush->num = count;
	vpPush->mode = maxCount;
	fpsgo_queue_work(vpPush);

	return 0;
}

int fpsgo_notify_magt_target_fps(int *pid_arr, int *tid_arr,
	int *tfps_arr, int num)
{
	struct FPSGO_NOTIFIER_PUSH_TAG *vpPush = NULL;

	if (!pid_arr|| !tid_arr|| num < 0 ||
		num > MAX_MAGT_TARGET_FPS_NUM)
		return -EINVAL;

	if (!kfpsgo_tsk)
		return -ENOMEM;

	vpPush = fpsgo_alloc_atomic(sizeof(struct FPSGO_NOTIFIER_PUSH_TAG));
	if (!vpPush)
		return -ENOMEM;

	vpPush->magt_tfps_hint = NULL;
	vpPush->magt_tfps_hint = fpsgo_alloc_atomic(sizeof(struct fpsgo_magt_target_fps));
	if (!vpPush->magt_tfps_hint) {
		fpsgo_free(vpPush, sizeof(struct FPSGO_NOTIFIER_PUSH_TAG));
		return -ENOMEM;
	}

	memset(vpPush->magt_tfps_hint->pid_arr, 0, MAX_MAGT_TARGET_FPS_NUM * sizeof(int));
	memcpy(vpPush->magt_tfps_hint->pid_arr, pid_arr, num * sizeof(int));
	memset(vpPush->magt_tfps_hint->tid_arr, 0, MAX_MAGT_TARGET_FPS_NUM * sizeof(int));
	memcpy(vpPush->magt_tfps_hint->tid_arr, tid_arr, num * sizeof(int));
	memset(vpPush->magt_tfps_hint->tfps_arr, 0, MAX_MAGT_TARGET_FPS_NUM * sizeof(int));
	memcpy(vpPush->magt_tfps_hint->tfps_arr, tfps_arr, num * sizeof(int));
	vpPush->magt_tfps_hint->num = num;
	vpPush->ePushType = FPSGO_NOTIFIER_MAGT_TARGET_FPS;

	fpsgo_queue_work(vpPush);

	return 0;
}

int fpsgo_notify_magt_dep_list(int pid, void *dep_task_arr, int dep_task_num)
{
	struct dep_and_prio *param = (struct dep_and_prio *)dep_task_arr;
	struct FPSGO_NOTIFIER_PUSH_TAG *vpPush = NULL;
	int i;

	if (dep_task_num < 0 || dep_task_num > MAX_MAGT_DEP_LIST_NUM ||
		(!param && dep_task_num != 0))
		return -EINVAL;

	if (!kfpsgo_tsk)
		return -ENOMEM;

	vpPush = fpsgo_alloc_atomic(sizeof(struct FPSGO_NOTIFIER_PUSH_TAG));
	if (!vpPush)
		return -ENOMEM;

	vpPush->magt_dep_hint = NULL;
	vpPush->magt_dep_hint = fpsgo_alloc_atomic(sizeof(struct fpsgo_magt_dep_list));
	if (!vpPush->magt_dep_hint) {
		fpsgo_free(vpPush, sizeof(struct FPSGO_NOTIFIER_PUSH_TAG));
		return -ENOMEM;
	}

	vpPush->magt_dep_hint->pid = pid;
	memset(vpPush->magt_dep_hint->dep_task_arr, 0,
		MAX_MAGT_DEP_LIST_NUM * sizeof(struct dep_and_prio));
	if (param) {
		for (i = 0; i < dep_task_num; i++) {
			vpPush->magt_dep_hint->dep_task_arr[i].pid = param[i].pid;
			vpPush->magt_dep_hint->dep_task_arr[i].prio = param[i].prio;
			vpPush->magt_dep_hint->dep_task_arr[i].timeout = param[i].timeout;
		}
	}

	vpPush->magt_dep_hint->dep_task_num = dep_task_num;
	vpPush->ePushType = FPSGO_NOTIFIER_MAGT_DEP_LIST;

	fpsgo_queue_work(vpPush);

	return 0;
}

int get_fpsgo_frame_info(int max_num, unsigned long mask,
	struct render_frame_info *frame_info_arr)
{
	int ret = 0;

	if (max_num <= 0 || mask == 0 ||
		mask >= 1 << FPSGO_FRAME_INFO_MAX_NUM || !frame_info_arr)
		return -EINVAL;

	ret = fpsgo_ctrl2base_get_render_frame_info(max_num, mask, frame_info_arr);

	return ret;
}
EXPORT_SYMBOL(get_fpsgo_frame_info);

void dfrc_fps_limit_cb(unsigned int fps_limit)
{
	unsigned int vTmp = TARGET_UNLIMITED_FPS;
	struct FPSGO_NOTIFIER_PUSH_TAG *vpPush;

	if (fps_limit > 0 && fps_limit <= TARGET_UNLIMITED_FPS)
		vTmp = fps_limit;

	FPSGO_LOGI("[FPSGO_CTRL] dfrc_fps %d\n", vTmp);

	vpPush =
		(struct FPSGO_NOTIFIER_PUSH_TAG *)
		fpsgo_alloc_atomic(sizeof(struct FPSGO_NOTIFIER_PUSH_TAG));

	if (!vpPush) {
		FPSGO_LOGE("[FPSGO_CTRL] OOM\n");
		return;
	}

	if (!kfpsgo_tsk) {
		FPSGO_LOGE("[FPSGO_CTRL] NULL WorkQueue\n");
		fpsgo_free(vpPush, sizeof(struct FPSGO_NOTIFIER_PUSH_TAG));
		return;
	}

	vpPush->ePushType = FPSGO_NOTIFIER_DFRC_FPS;
	vpPush->dfrc_fps = vTmp;

	fpsgo_queue_work(vpPush);
}

/* FPSGO control */
void fpsgo_switch_enable(int enable)
{
	struct FPSGO_NOTIFIER_PUSH_TAG *vpPush = NULL;

	if (!kfpsgo_tsk) {
		FPSGO_LOGE("[FPSGO_CTRL] NULL WorkQueue\n");
		return;
	}

	if (fpsgo_base2fbt_get_cluster_num() <= 0) {
		FPSGO_LOGE("[%s] DON'T ENABLE FPSGO: nr_cluster <= 0", __func__);
		return;
	}

	FPSGO_LOGI("[FPSGO_CTRL] switch enable %d\n", enable);

	if (fpsgo_is_force_enable() !=
			FPSGO_FREE && enable !=
			fpsgo_is_force_enable())
		return;

	vpPush =
		(struct FPSGO_NOTIFIER_PUSH_TAG *)
		fpsgo_alloc_atomic(sizeof(struct FPSGO_NOTIFIER_PUSH_TAG));

	if (!vpPush) {
		FPSGO_LOGE("[FPSGO_CTRL] OOM\n");
		return;
	}

	vpPush->ePushType = FPSGO_NOTIFIER_SWITCH_FPSGO;
	vpPush->enable = enable;

	fpsgo_queue_work(vpPush);
}

int fpsgo_is_force_enable(void)
{
	int temp_onoff;

	mutex_lock(&notify_lock);
	temp_onoff = fpsgo_force_onoff;
	mutex_unlock(&notify_lock);

	return temp_onoff;
}

void fpsgo_force_switch_enable(int enable)
{
	mutex_lock(&notify_lock);
	fpsgo_force_onoff = enable;
	mutex_unlock(&notify_lock);

	fpsgo_switch_enable(enable?1:0);
}

static void fpsgo_notify_cpufreq_cap(unsigned int first_cpu_id, unsigned int last_cpu_id)
{
	int cluster = 0;
	int max_capacity = 0, tmp_capacity = 0;
	unsigned int cpu;
	struct cpufreq_policy *policy = NULL;

	for_each_possible_cpu(cpu) {
		policy = cpufreq_cpu_get(cpu);
		if (!policy)
			break;
		cpu = cpumask_first(policy->related_cpus);
		if (cpu == first_cpu_id) {
			cpufreq_cpu_put(policy);
			break;
		}
		cpu = cpumask_last(policy->related_cpus);
		cluster++;
		cpufreq_cpu_put(policy);
	}

	for (cpu = first_cpu_id; cpu <= last_cpu_id; cpu++) {
#if IS_ENABLED(CONFIG_MTK_CPUFREQ_SUGOV_EXT)
		tmp_capacity = get_curr_cap(cpu);
#endif
		if (tmp_capacity > max_capacity)
			max_capacity = tmp_capacity;
	}

	if (max_capacity > 0)
		fpsgo_ctrl2fbt_cpufreq_cb_cap(cluster, max_capacity);
}

#if FPSGO_DYNAMIC_WL
static void fpsgo_cpu_frequency_cap_tracer(void *ignore, struct cpufreq_policy *policy)
{
	int first_cpu_id = 0, last_cpu_id = 0;

#if IS_ENABLED(CONFIG_MTK_IRQ_MONITOR_DEBUG)
	u64 ts[2];

	ts[0] = sched_clock();
#endif

	if (!fpsgo_enable || !policy)
		goto out;

	first_cpu_id = cpumask_first(policy->related_cpus);
	last_cpu_id = cpumask_last(policy->related_cpus);
	fpsgo_notify_cpufreq_cap(first_cpu_id, last_cpu_id);

out:
#if IS_ENABLED(CONFIG_MTK_IRQ_MONITOR_DEBUG)
	ts[1] = sched_clock();

	if ((ts[1] - ts[0] > 100000ULL) && in_hardirq()) {
		printk_deferred("%s duration %llu, ts[0]=%llu, ts[1]=%llu\n",
			__func__, ts[1] - ts[0], ts[0], ts[1]);
	}
#endif
}

void register_fpsgo_android_cpufreq_transition_hook(void)
{
	int ret = 0;

	ret = register_trace_android_rvh_cpufreq_transition(fpsgo_cpu_frequency_cap_tracer, NULL);
	if (ret)
		pr_info("register android_rvh_cpufreq_transition hooks failed, returned %d\n", ret);
}

#else
struct tracepoints_table {
	const char *name;
	void *func;
	struct tracepoint *tp;
	bool registered;
};

#define MAX_NR_CPUS CONFIG_MAX_NR_CPUS
static void fpsgo_cpu_frequency_tracer(void *ignore, unsigned int frequency, unsigned int cpu_id)
{
	unsigned int first_cpu_id, last_cpu_id;
	struct cpufreq_policy *policy = NULL;

#if IS_ENABLED(CONFIG_MTK_IRQ_MONITOR_DEBUG)
	u64 ts[2];

	ts[0] = sched_clock();
#endif

	if (!fpsgo_enable)
		return;

	policy = cpufreq_cpu_get(cpu_id);
	if (!policy)
		return;
	if (cpu_id != cpumask_first(policy->related_cpus)) {
		cpufreq_cpu_put(policy);
		return;
	}

	first_cpu_id = cpumask_first(policy->related_cpus);
	last_cpu_id = cpumask_last(policy->related_cpus);
	first_cpu_id = clamp(first_cpu_id, 0, MAX_NR_CPUS - 1);
	last_cpu_id = clamp(last_cpu_id, 0, MAX_NR_CPUS - 1);
	cpufreq_cpu_put(policy);
	fpsgo_notify_cpufreq_cap(first_cpu_id, last_cpu_id);

#if IS_ENABLED(CONFIG_MTK_IRQ_MONITOR_DEBUG)
	ts[1] = sched_clock();

	if ((ts[1] - ts[0] > 100000ULL) && in_hardirq()) {
		printk_deferred("%s duration %llu, ts[0]=%llu, ts[1]=%llu\n",
			__func__, ts[1] - ts[0], ts[0], ts[1]);
	}
#endif
}

struct tracepoints_table fpsgo_tracepoints[] = {
	{.name = "cpu_frequency", .func = fpsgo_cpu_frequency_tracer},
};

#define FOR_EACH_INTEREST(i) \
	for (i = 0; i < sizeof(fpsgo_tracepoints) / sizeof(struct tracepoints_table); i++)

static void lookup_tracepoints(struct tracepoint *tp, void *ignore)
{
	int i;

	FOR_EACH_INTEREST(i) {
		if (strcmp(fpsgo_tracepoints[i].name, tp->name) == 0)
			fpsgo_tracepoints[i].tp = tp;
	}
}

static void tracepoint_cleanup(void)
{
	int i;

	FOR_EACH_INTEREST(i) {
		if (fpsgo_tracepoints[i].registered) {
			tracepoint_probe_unregister(
				fpsgo_tracepoints[i].tp,
				fpsgo_tracepoints[i].func, NULL);
			fpsgo_tracepoints[i].registered = false;
		}
	}
}

void register_fpsgo_cpufreq_transition_hook(void)
{
	int i, ret = 0;

	for_each_kernel_tracepoint(lookup_tracepoints, NULL);

	FOR_EACH_INTEREST(i) {
		if (fpsgo_tracepoints[i].tp == NULL) {
			FPSGO_LOGE("FPSGO Error, %s not found\n", fpsgo_tracepoints[i].name);
			tracepoint_cleanup();
			return;
		}
	}
	ret = tracepoint_probe_register(fpsgo_tracepoints[0].tp, fpsgo_tracepoints[0].func,  NULL);
	if (ret) {
		FPSGO_LOGE("cpu_frequency: Couldn't activate tracepoint\n");
		return;
	}
	fpsgo_tracepoints[0].registered = true;
}
#endif  // FPSGO_DYNAMIC_WL

static void __exit fpsgo_exit(void)
{
	fpsgo_notifier_wq_cb_enable(0);

	if (kfpsgo_tsk)
		kthread_stop(kfpsgo_tsk);

#if IS_ENABLED(CONFIG_DEVICE_MODULES_DRM_MEDIATEK)
	drm_unregister_fps_chg_callback(dfrc_fps_limit_cb);
#endif
	fbt_cpu_exit();
	mtk_fstb_exit();
	fpsgo_composer_exit();
	fpsgo_sysfs_exit();
}

static int __init fpsgo_init(void)
{
#if !IS_ENABLED(CONFIG_ARM64)
	cap_ready = 0;
#endif

	fpsgo_cpu_policy_init();

	fpsgo_sysfs_init();

	kfpsgo_tsk = kthread_create(kfpsgo, NULL, "kfps");
	if (kfpsgo_tsk == NULL)
		return -EFAULT;
	wake_up_process(kfpsgo_tsk);

#if FPSGO_DYNAMIC_WL
	register_fpsgo_android_cpufreq_transition_hook();
#else  // FPSGO_DYNAMIC_WL
	register_fpsgo_cpufreq_transition_hook();
#endif  // FPSGO_DYNAMIC_WL

	mutex_init(&notify_lock);

	fpsgo_force_onoff = FPSGO_FREE;

	init_fpsgo_common();
	mtk_fstb_init();
	fpsgo_composer_init();
	fbt_cpu_init();
	fpsgo_adpf_init();

	if (fpsgo_arch_nr_clusters() > 0)
		fpsgo_switch_enable(1);

	fpsgo_notify_vsync_fp = fpsgo_notify_vsync;
	fpsgo_notify_vsync_period_fp = fpsgo_notify_vsync_period;

	fpsgo_notify_qudeq_fp = fpsgo_notify_qudeq;
	fpsgo_notify_connect_fp = fpsgo_notify_connect;
	fpsgo_notify_bqid_fp = fpsgo_notify_bqid;
	fpsgo_notify_acquire_fp = fpsgo_notify_acquire;

	fpsgo_notify_swap_buffer_fp = fpsgo_notify_swap_buffer;
	fpsgo_notify_sbe_rescue_fp = fpsgo_notify_sbe_rescue;
	fpsgo_notify_frame_hint_fp = fpsgo_notify_frame_hint;
	fpsgo_notify_sbe_policy_fp = fpsgo_notify_sbe_policy;
	fpsgo_notify_ux_buffer_count_fp = fpsgo_notify_ux_buffer_count;

	fpsgo_get_fps_fp = fpsgo_get_fps;
	fpsgo_get_cmd_fp = fpsgo_get_cmd;
	fpsgo_get_fstb_active_fp = fpsgo_get_fstb_active;
	fpsgo_wait_fstb_active_fp = fpsgo_wait_fstb_active;
	fpsgo_notify_buffer_quota_fp = fpsgo_notify_buffer_quota;
	fpsgo_get_pid_fp = fpsgo_get_pid;
#if IS_ENABLED(CONFIG_MTK_PERF_IOCTL_MAGT)
	magt2fpsgo_notify_target_fps_fp = fpsgo_notify_magt_target_fps;
	magt2fpsgo_notify_dep_list_fp = fpsgo_notify_magt_dep_list;
	magt2fpsgo_get_fpsgo_frame_info = get_fpsgo_frame_info;
#endif
	fpsgo_get_lr_pair_fp = fpsgo_get_lr_pair;
	fpsgo_set_rl_l2q_enable_fp = fpsgo_set_rl_l2q_enable;
	fpsgo_set_rl_expected_l2q_us_fp = fpsgo_set_expected_l2q_us;

#if IS_ENABLED(CONFIG_DEVICE_MODULES_DRM_MEDIATEK)
	drm_register_fps_chg_callback(dfrc_fps_limit_cb);
#endif

	return 0;
}

#if !IS_ENABLED(CONFIG_ARM64)
late_initcall_sync(fpsgo_init);
#else
module_init(fpsgo_init);
#endif
module_exit(fpsgo_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MediaTek FPSGO");
MODULE_AUTHOR("MediaTek Inc.");
MODULE_VERSION(FPSGO_VERSION_MODULE);
