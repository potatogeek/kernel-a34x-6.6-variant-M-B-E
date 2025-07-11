// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2015 MediaTek Inc.
 */

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/dma-mapping.h>
#include <linux/dmapool.h>

#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/mailbox_controller.h>
#include <linux/mailbox/mtk-cmdq-mailbox-ext.h>
#include <linux/soc/mediatek/mtk-cmdq-ext.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/atomic.h>
#include <linux/sched/clock.h>
#include <linux/suspend.h>
#include <linux/pm_runtime.h>
#include <linux/pm_domain.h>
#include <linux/soc/mediatek/mtk_sip_svc.h>
#include <linux/arm-smccc.h>
#if IS_ENABLED(CONFIG_VHOST_CMDQ)
#include <linux/libnvdimm.h>
#endif

#include <iommu_debug.h>
#include <mt-plat/mtk_irq_mon.h>

#if IS_ENABLED(CONFIG_MTK_CMDQ_MBOX_EXT)
#include "cmdq-util.h"
struct cmdq_util_controller_fp *cmdq_util_controller;
#endif

#ifndef cmdq_util_msg
#define cmdq_util_msg(f, args...) cmdq_msg(f, ##args)
#endif

#ifndef cmdq_util_err
#define cmdq_util_err(f, args...) cmdq_dump(f, ##args)
#endif

/* ddp main/sub, mdp path 0/1/2/3, general(misc) */
#define CMDQ_OP_CODE_MASK		(0xff << CMDQ_OP_CODE_SHIFT)
#define CMDQ_IRQ_MASK			GENMASK(gce_thread_nr - 1, 0)

#define CMDQ_CORE_REST			0x0
#define CMDQ_CURR_IRQ_STATUS		0x10
#define CMDQ_CURR_LOADED_THR		0x18
#define CMDQ_THR_SLOT_CYCLES		0x30
#define CMDQ_THR_EXEC_CYCLES		0x34
#define CMDQ_THR_TIMEOUT_TIMER		0x38
#define CMDQ_SYNC_TOKEN_ID		0x60
#define CMDQ_SYNC_TOKEN_VAL		0x64
#define CMDQ_SYNC_TOKEN_UPD		0x68
#define CMDQ_PREFETCH_GSIZE		0xC0
#define CMDQ_TPR_MASK			0xD0
#define CMDQ_TPR_TIMEOUT_EN		0xDC
#define CMDQ_ULTRA_EN			BIT(0)
#define CMDQ_PREULTRA_EN		BIT(1)
#define CMDQ_DDR_URGENT			BIT(19)
#define DDR_SEL_WLA			BIT(0)

#define CMDQ_THR_BASE			0x100
#define CMDQ_THR_SIZE			0x80
#define CMDQ_THR_WARM_RESET		0x00
#define CMDQ_THR_ENABLE_TASK		0x04
#define CMDQ_THR_SUSPEND_TASK		0x08
#define CMDQ_THR_CURR_STATUS		0x0c
#define CMDQ_THR_IRQ_STATUS		0x10
#define CMDQ_THR_IRQ_ENABLE		0x14
#define CMDQ_THR_CURR_ADDR		0x20
#define CMDQ_THR_END_ADDR		0x24
#define CMDQ_THR_CNT			0x28
#define CMDQ_THR_WAIT_TOKEN		0x30
#define CMDQ_THR_CFG			0x40
#define CMDQ_THR_PREFETCH		0x44
#define CMDQ_THR_INST_CYCLES		0x50
#define CMDQ_THR_INST_THRESX		0x54
#define CMDQ_THR_SPR			0x60

#define GCE_BUS_GCTL			0x40
#define GCE_GCTL_VALUE			0x48
#define GCE_OUTPIN_EVENT		0x4c
#define GCE_GPR_R0_START		0x80
#define GCE_DEBUG_START_ADDR		0x1104
#define GCE_DEBUG_END_ADDR		0x1108

#define CMDQ_THR_ENABLED		0x1
#define CMDQ_THR_DISABLED		0x0
#define CMDQ_THR_SUSPEND		0x1
#define CMDQ_THR_RESUME			0x0
#define CMDQ_THR_STATUS_SUSPENDED	BIT(1)
#define CMDQ_THR_DO_WARM_RESET		BIT(0)
#define CMDQ_THR_DO_HARD_RESET		BIT(16)
#define CMDQ_THR_ACTIVE_SLOT_CYCLES	0x3200
#define CMDQ_INST_CYCLE_TIMEOUT		0x0
#define CMDQ_THR_IRQ_DONE		0x1
#define CMDQ_THR_IRQ_ERROR		0x12
#define CMDQ_THR_IRQ_EN			(CMDQ_THR_IRQ_ERROR | CMDQ_THR_IRQ_DONE)
#define CMDQ_THR_IS_WAITING		BIT(31)
#define CMDQ_THR_PRIORITY		0x7
#define CMDQ_TPR_EN			BIT(31)
#define CMDQ_HW_TRACE_EN		BIT(31)

#define GCE_DBG_CTL			0x3000
#define GCE_DBG0			0x3004
#define GCE_DBG2			0x300C
#define GCE_DBG3			0x3010
#define GCE_READ_TPR_CTL_VAL	0x500

#define GCE_VM6_OFFSET		0x60000
#define GCE_HOST_VM_IRQ_STATUS			0x3014
#define GCE_CPR_GSIZE		0x50C4
#define GCE_VM_ID_MAP0		0x5018
#define GCE_VM_ID_MAP1		0x501C
#define GCE_VM_ID_MAP2		0x5020
#define GCE_VM_ID_MAP3		0x5024

#define GCE_DDREN_BIT		(0)
#define GCE_DDRSRC_BIT		(1)
#define GCE_EMI_BIT			(2)

#define CMDQ_JUMP_BY_OFFSET		0x10000000
#define CMDQ_JUMP_BY_PA			0x10000001

#define CMDQ_MIN_AGE_VALUE              (5)	/* currently disable age */
#define CMDQ_INIT_BUF_SIZE		8484

#define GCED_HWID				(0)
#define GCEM_HWID				(1)

#define CMDQ_DRIVER_NAME		"mtk_cmdq_mbox"

/* pc and end shift bit for gce, should be config in probe */
int gce_shift_bit;
EXPORT_SYMBOL(gce_shift_bit);

u32 gce_thread_nr;
EXPORT_SYMBOL(gce_thread_nr);

unsigned long long gce_mminfra;
EXPORT_SYMBOL(gce_mminfra);

bool skip_poll_sleep;
EXPORT_SYMBOL(skip_poll_sleep);

bool gce_in_vcp;
EXPORT_SYMBOL(gce_in_vcp);

bool cpr_not_support_cookie;
EXPORT_SYMBOL(cpr_not_support_cookie);

bool append_by_event;
EXPORT_SYMBOL(append_by_event);

bool cmdq_tfa_read_dbg;
EXPORT_SYMBOL(cmdq_tfa_read_dbg);

bool hw_trace_built_in[2];
EXPORT_SYMBOL(hw_trace_built_in);

bool hw_trace_vm;
EXPORT_SYMBOL(hw_trace_vm);

/* CMDQ log flag */
int mtk_cmdq_log;
EXPORT_SYMBOL(mtk_cmdq_log);

int cmdq_pwr_log;
EXPORT_SYMBOL(cmdq_pwr_log);
module_param(cmdq_pwr_log, int, 0644);

int mtk_cmdq_msg;
EXPORT_SYMBOL(mtk_cmdq_msg);

int mtk_cmdq_err = 1;
EXPORT_SYMBOL(mtk_cmdq_err);
module_param(mtk_cmdq_log, int, 0644);

int cmdq_trace;
EXPORT_SYMBOL(cmdq_trace);
module_param(cmdq_trace, int, 0644);

int cmdq_hw_trace;
EXPORT_SYMBOL(cmdq_hw_trace);

int cmdq_dump_buf_size;
EXPORT_SYMBOL(cmdq_dump_buf_size);
module_param(cmdq_dump_buf_size, int, 0644);

int error_irq_bug_on;
EXPORT_SYMBOL(error_irq_bug_on);
module_param(error_irq_bug_on, int, 0644);

int cmdq_ut_count;
EXPORT_SYMBOL(cmdq_ut_count);
module_param(cmdq_ut_count, int, 0644);

int cmdq_ut_sleep_time;
EXPORT_SYMBOL(cmdq_ut_sleep_time);
module_param(cmdq_ut_sleep_time, int, 0644);

int cmdq_print_debug;
EXPORT_SYMBOL(cmdq_print_debug);
module_param(cmdq_print_debug, int, 0644);

int cmdq_proc_debug_off;
EXPORT_SYMBOL(cmdq_proc_debug_off);
module_param(cmdq_proc_debug_off, int, 0644);

int cmdq_ftrace_ena;
EXPORT_SYMBOL(cmdq_ftrace_ena);
module_param(cmdq_ftrace_ena, int, 0644);

struct cmdq_hw_trace_bit {
	uint8_t enable : 1;
	uint8_t dump : 1;
	uint8_t hwid : 1;
	uint8_t built_in : 1;
	uint32_t unused : 28;
};


struct cmdq_task {
	struct cmdq		*cmdq;
	struct list_head	list_entry;
	dma_addr_t		pa_base;
	struct cmdq_thread	*thread;
	struct cmdq_pkt		*pkt; /* the packet sent from mailbox client */
	u64			exec_time;
	u64			end_time;
};

struct cmdq_buf_dump {
	struct cmdq		*cmdq;
	struct work_struct	dump_work;
	bool			timeout; /* 0: error, 1: timeout */
	void			*cmd_buf;
	size_t			cmd_buf_size;
	dma_addr_t		pa_offset; /* pa_curr - pa_base */
};

#define mmp_event unsigned int

struct cmdq_mmp_event {
	mmp_event cmdq;
	mmp_event cmdq_irq;
	mmp_event loop_irq;
	mmp_event thread_en;
	mmp_event thread_suspend;
	mmp_event submit;
	mmp_event wait;
	mmp_event wait_done;
	mmp_event warning;
	mmp_event hw_exec;
	mmp_event pkt_size;
};
struct cmdq_mmp_event	cmdq_mmp;

struct cmdq {
	struct mbox_controller	mbox;
	void __iomem		*base;
	phys_addr_t		base_pa;
	u8			hwid;
	u32			irq;
	u32			irq_vm6;
	struct list_head	irq_removes;
	spinlock_t		irq_removes_lock;
	struct wait_queue_head	err_irq_wq;
	unsigned long		err_irq_idx;
	spinlock_t		irq_idx_lock;
	struct workqueue_struct	*buf_dump_wq;
	struct cmdq_thread	thread[CMDQ_THR_MAX_COUNT];
	u32			prefetch;
	struct clk		*clock;
	struct clk		*clock_timer;
	bool			suspended;
	atomic_t		usage;
	struct workqueue_struct *timeout_wq;
	spinlock_t		lock;
	spinlock_t		event_lock;
	u32			token_cnt;
	u16			*tokens;
#if IS_ENABLED(CONFIG_CMDQ_MMPROFILE_SUPPORT)
	struct cmdq_mmp_event	mmp;
#endif
	void			*init_cmds_base;
	dma_addr_t		init_cmds;
	bool			sw_ddr_en;
	bool			sw_ddr_urgent;
	bool			outpin_en;
	bool			prebuilt_enable;
	struct cmdq_client	*prebuilt_clt;
	struct cmdq_client	*hw_trace_clt;
	struct mutex mbox_mutex;
	struct device	*share_dev;
	u8			irq_long_times;
	/* fast mtcmos */
	bool		fast_mtcmos;
	spinlock_t	fast_mtcmos_lock;
	atomic_t	fast_mtcmos_usage;
	u64			fast_en;
	u64			fast_dis;
#if IS_ENABLED(CONFIG_MTK_CMDQ_DEBUG)
	u32			tf_high_addr;
#endif
	bool		event_debug;
	u32			event_dump_range[2];
	u32			event_clr_range[2];
	u32			sid;
	u32			axid;
	u32			tbu;
	bool		smmu_v3_enabled;
	bool		err_irq;
	void __iomem	*dram_pwr_base;
	void __iomem	*mminfra_ao_base;
	bool		error_irq_sw_req;
	bool		gce_vm;
	bool		spr3_timer;
	bool		ao_ctrl_by_mminfra;
	struct device	*pd_mminfra_1;
	struct device	*pd_mminfra_ao;
	bool		gce_ddr_sel_wla;
	bool		gce_req_wa;
	unsigned int	dbg3;
	bool		gce_res_sw_mode;
};

struct gce_plat {
	u32 thread_nr;
	u8 shift;
	u64 mminfra;
};

#if IS_ENABLED(CONFIG_CMDQ_MMPROFILE_SUPPORT)
#include "../misc/mediatek/mmp/mmprofile.h"

#define MMP_THD(t, c)	((t)->idx | ((c)->hwid << 5))
#endif

static struct cmdq *g_cmdq[2];

#define TRACE_MSG_LEN	1024

static noinline int tracing_mark_write(const char *buf)
{
#if IS_ENABLED(CONFIG_MTK_CMDQ_DEBUG)
#if IS_ENABLED(CONFIG_TRACING)
	trace_puts(buf);
#endif
#else
	if(cmdq_ftrace_ena)
		trace_puts(buf);
#endif
	return 0;
}

void cmdq_print_trace(char *fmt, ...)
{
	char buf[TRACE_MSG_LEN];
	va_list args;
	int len;

	va_start(args, fmt);
	len = vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);
	if (len >= TRACE_MSG_LEN) {
		pr_notice("%s trace size %u exceed limit\n", __func__, len);
		return;
	}
	tracing_mark_write(buf);
}
EXPORT_SYMBOL(cmdq_print_trace);

int cmdq_hw_trace_set(const char *val, const struct kernel_param *kp)
{
	struct cmdq_hw_trace_bit bit;
	struct cmdq *cmdq;
	int ret;

	ret = kstrtouint(val, 0, (u32 *)(void *)&bit);
	cmdq_msg("%s: bit:%#x enable:%d dump:%d hwid:%#x built_in:%d ret:%d",
		__func__, *((unsigned int *)&bit), bit.enable, bit.dump, bit.hwid,
		bit.built_in, ret);

	if (ret)
		return ret;

	if (!cmdq_util_check_hw_trace_work(bit.hwid)) {
		cmdq_err("hw trace disable");
		return -EINVAL;
	}

	cmdq_hw_trace = bit.enable ? 1 : 0;
	cmdq_util_hw_trace_mode_update(bit.hwid, (bool)bit.built_in);

	cmdq = g_cmdq[bit.hwid];
	// power
	if (mminfra_power_cb && !mminfra_power_cb()) {
		cmdq_err("hwid:%u mminfra power not enable", cmdq->hwid);
		return -EINVAL;
	}
	// clock
	if (mminfra_gce_cg && !mminfra_gce_cg(cmdq->hwid)) {
		cmdq_err("hwid:%u gce clock not enable", cmdq->hwid);
		return -EINVAL;
	}

	if (bit.dump)
		cmdq_util_hw_trace_dump(
			bit.hwid, cmdq_util_get_bit_feature() & CMDQ_LOG_FEAT_PERF);

	return ret;
}

bool cmdq_is_hw_trace_thread(struct mbox_chan *chan)
{
	struct cmdq *cmdq = container_of(chan->mbox, typeof(*cmdq), mbox);

	if (chan == cmdq->hw_trace_clt->chan)
		return true;
	return false;
}
EXPORT_SYMBOL(cmdq_is_hw_trace_thread);

static const struct kernel_param_ops cmdq_hw_trace_ops = {
	.set = cmdq_hw_trace_set,
};
module_param_cb(cmdq_hw_trace, &cmdq_hw_trace_ops, NULL, 0644);

u32 cmdq_mbox_get_tpr(void *chan)
{
	struct cmdq *cmdq = container_of(((struct mbox_chan *)chan)->mbox,
		typeof(*cmdq), mbox);
	u32 id;
	unsigned long flags;
	void *base = cmdq->base;
	u32 dbg3;
	u64 dbg[5] = {0};

	if (!cmdq_tfa_read_dbg && !base) {
		cmdq_util_msg("no cmdq dbg since no base");
		return U32_MAX;
	}

	spin_lock_irqsave(&cmdq->lock, flags);
	if (atomic_read(&cmdq->usage) <= 0 ||
		(mminfra_power_cb && !mminfra_power_cb()) ||
		(mminfra_gce_cg && !mminfra_gce_cg(cmdq->hwid))) {
		cmdq_util_msg("no cmdq dbg since mbox disable");
		spin_unlock_irqrestore(&cmdq->lock, flags);
		return U32_MAX;
	}

	id = cmdq_util_get_hw_id((u32)cmdq->base_pa);
	cmdq_util_enable_dbg(id);
	if (!cmdq_tfa_read_dbg) {
		/* debug select */
		writel(GCE_READ_TPR_CTL_VAL, base + GCE_DBG_CTL);
		dbg3 = readl(base + GCE_DBG3);
	} else {
		cmdq_util_return_dbg(id, dbg);
		dbg3 = (u32)dbg[1];
	}
	spin_unlock_irqrestore(&cmdq->lock, flags);

	return dbg3 == U32_MAX ? 0 : dbg3;
}
EXPORT_SYMBOL(cmdq_mbox_get_tpr);

u8 cmdq_get_irq_long_times(void *chan)
{
	struct cmdq *cmdq = container_of(((struct mbox_chan *)chan)->mbox,
		typeof(*cmdq), mbox);
	return cmdq->irq_long_times;
}
EXPORT_SYMBOL(cmdq_get_irq_long_times);

#if IS_ENABLED(CONFIG_MTK_CMDQ_DEBUG)
u32 cmdq_get_tf_high_addr(void *chan)
{
	struct cmdq *cmdq = container_of(((struct mbox_chan *)chan)->mbox,
		typeof(*cmdq), mbox);
	return cmdq->tf_high_addr;
}
EXPORT_SYMBOL(cmdq_get_tf_high_addr);

u32 cmdq_get_tf_high_addr_by_dev(struct device *dev)
{
	struct cmdq *cmdq = dev_get_drvdata(dev);
	u32 i;

	for (i = 0; i < 2; i++) {
		if(g_cmdq[i] == cmdq)
			return g_cmdq[i]->tf_high_addr;
	}
	return 0;
}
EXPORT_SYMBOL(cmdq_get_tf_high_addr_by_dev);
#endif

void cmdq_event_dump_and_clr(void *chan)
{
	u32 i;
	struct cmdq *cmdq = container_of(((struct mbox_chan *)chan)->mbox,
		typeof(*cmdq), mbox);

	if (!cmdq->event_debug)
		return;

	for (i = cmdq->event_dump_range[0]; i <= cmdq->event_dump_range[1]; i++) {
		if (cmdq_get_event(chan, i)) {
			cmdq_msg("%s event %u is set", __func__, i);
			if (i >= cmdq->event_clr_range[0] && i <= cmdq->event_clr_range[1]) {
				cmdq_msg("%s clr event:%d", __func__, i);
				cmdq_clear_event(chan, i);
			}
		}
	}
}
EXPORT_SYMBOL(cmdq_event_dump_and_clr);

void cmdq_get_usage_cb(struct mbox_chan *chan, cmdq_usage_cb usage_cb)
{
	struct cmdq *cmdq = container_of(((struct mbox_chan *)chan)->mbox,
		typeof(*cmdq), mbox);
	u32 i;

	for (i = 0; i < ARRAY_SIZE(cmdq->thread); i++)
		if (cmdq->thread[i].chan == chan)
			break;

	if (i == ARRAY_SIZE(cmdq->thread))
		return;
	cmdq->thread[i].usage_cb = usage_cb;
}
EXPORT_SYMBOL(cmdq_get_usage_cb);

void cmdq_dump_usage(void)
{
	s32 i, j, usage[CMDQ_THR_MAX_COUNT];

	for (i = 0; i < 2; i++) {
		if (!g_cmdq[i])
			break;

		cmdq_msg("%s: hwid:%d suspend:%d usage:%d fast_usage:%d",
			__func__, g_cmdq[i]->hwid, g_cmdq[i]->suspended,
			atomic_read(&g_cmdq[i]->usage),
			atomic_read(&g_cmdq[i]->fast_mtcmos_usage));

		if (!atomic_read(&g_cmdq[i]->usage))
			continue;

		for (j = 0; j < ARRAY_SIZE(g_cmdq[i]->thread); j++) {
			usage[j] = atomic_read(&g_cmdq[i]->thread[j].usage);
			if (usage[j] > 0 && g_cmdq[i]->thread[j].usage_cb)
				g_cmdq[i]->thread[j].usage_cb(j);
		}

		cmdq_msg(
			"%s: usage:%d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d",
			__func__,
			usage[0], usage[1], usage[2], usage[3], usage[4],
			usage[5], usage[6], usage[7], usage[8], usage[9],
			usage[10], usage[11], usage[12], usage[13], usage[14],
			usage[15], usage[16], usage[17], usage[18], usage[19],
			usage[20], usage[21], usage[22], usage[23]);
	}
}
EXPORT_SYMBOL(cmdq_dump_usage);

bool cmdq_get_support_vm(u8 hwid)
{
	return g_cmdq[hwid]->gce_vm;
}
EXPORT_SYMBOL(cmdq_get_support_vm);

static void cmdq_init_cpu(struct cmdq *cmdq)
{
	int i;

	cmdq_trace_ex_begin("%s", __func__);

	writel(CMDQ_THR_ACTIVE_SLOT_CYCLES, cmdq->base + CMDQ_THR_SLOT_CYCLES);
	for (i = 0; i <= CMDQ_EVENT_MAX; i++)
		writel(i, cmdq->base + CMDQ_SYNC_TOKEN_UPD);

	/* some of events need default 1 */
	for (i = 0; i < cmdq->token_cnt; i++)
		writel(cmdq->tokens[i] | BIT(16),
			cmdq->base + CMDQ_SYNC_TOKEN_UPD);

	cmdq_trace_ex_end();
}

#if !IS_ENABLED(CONFIG_VIRTIO_CMDQ)
static void cmdq_init(struct cmdq *cmdq)
{
	if (cmdq->init_cmds_base)
		cmdq_init_cmds(cmdq);
	else
		cmdq_init_cpu(cmdq);
}
#endif

static inline void cmdq_mmp_init(void)
{
#if IS_ENABLED(CONFIG_CMDQ_MMPROFILE_SUPPORT)
	mmprofile_enable(1);
	if (cmdq_mmp.cmdq) {
		mmprofile_start(1);
		return;
	}

	cmdq_mmp.cmdq = mmprofile_register_event(MMP_ROOT_EVENT, "CMDQ");
	cmdq_mmp.cmdq_irq = mmprofile_register_event(cmdq_mmp.cmdq, "cmdq_irq");
	cmdq_mmp.hw_exec = mmprofile_register_event(cmdq_mmp.cmdq, "hw_exec");
	cmdq_mmp.loop_irq = mmprofile_register_event(cmdq_mmp.cmdq, "loop_irq");
	cmdq_mmp.thread_en =
		mmprofile_register_event(cmdq_mmp.cmdq, "thread_en");
	cmdq_mmp.thread_suspend =
		mmprofile_register_event(cmdq_mmp.cmdq, "thread_suspend");
	cmdq_mmp.submit = mmprofile_register_event(cmdq_mmp.cmdq, "submit");
	cmdq_mmp.pkt_size = mmprofile_register_event(cmdq_mmp.cmdq, "pkt_size");
	cmdq_mmp.wait = mmprofile_register_event(cmdq_mmp.cmdq, "wait");
	cmdq_mmp.warning = mmprofile_register_event(cmdq_mmp.cmdq, "warning");
	mmprofile_enable_event_recursive(cmdq_mmp.cmdq, 1);
	mmprofile_start(1);
#endif
}

static void cmdq_mtcmos_mminfra_ao(struct cmdq *cmdq, bool on)
{
	u32 onoff = on ? 1 : 0;

	if (cmdq->ao_ctrl_by_mminfra) {
		int ret;

		if (on)
			ret = pm_runtime_get_sync(cmdq->pd_mminfra_ao);
		else
			ret = pm_runtime_put_sync(cmdq->pd_mminfra_ao);

		if (ret != 0)
			cmdq_err("%s err:%d",
				on ? "pm_runtime_get_sync" : "pm_runtime_put_sync",
				ret);
	} else {
		//Vcore
		struct arm_smccc_res res;

		cmdq_log("%s on:%d", __func__, on);
		arm_smccc_smc(MTK_SIP_CMDQ_CONTROL, CMDQ_VCORE_REQ, onoff,
			0, 0, 0, 0, 0, &res);
	}
}

static void cmdq_mtcmos_by_fast(struct cmdq *cmdq, bool on)
{
	unsigned long flags;
	s32 usage;

	if (!cmdq || !cmdq->fast_mtcmos)
		return;

	spin_lock_irqsave(&cmdq->fast_mtcmos_lock, flags);
	cmdq_log("%s on:%d fast_usage:%d", __func__,
		on, atomic_read(&cmdq->fast_mtcmos_usage));
	if (on) {
		usage = atomic_inc_return(&cmdq->fast_mtcmos_usage);
		if (usage == 1) {
			int ret;

			ret = pm_runtime_get_sync(cmdq->pd_mminfra_1);
			if (ret < 0)
				cmdq_err("pm_runtime_get_sync err:%d", ret);
			if (mminfra_power_cb && !mminfra_power_cb())
				cmdq_err("hwid:%hu usage:%d mminfra power not enable",
					cmdq->hwid, usage);
			cmdq->fast_en = sched_clock();
		}
	} else {
		usage = atomic_dec_return(&cmdq->fast_mtcmos_usage);
		if (usage == 0) {
			int ret;

			cmdq->fast_dis = sched_clock();
			if (mminfra_power_cb && !mminfra_power_cb())
				cmdq_err("hwid:%hu usage:%d mminfra power not enable",
					cmdq->hwid, usage);
			ret = pm_runtime_put_sync(cmdq->pd_mminfra_1);
			if (ret < 0)
				cmdq_err("pm_runtime_get_sync err:%d", ret);
		} else if (usage < 0)
			cmdq_err("hwid:%u usage:%d cannot below zero",
				cmdq->hwid, usage);
	}
	spin_unlock_irqrestore(&cmdq->fast_mtcmos_lock, flags);
}

void cmdq_mbox_mtcmos_by_fast_chan(struct mbox_chan *chan, bool on)
{
	struct cmdq *cmdq = container_of(chan->mbox, typeof(*cmdq), mbox);

	cmdq_mtcmos_by_fast(cmdq, on);
}
EXPORT_SYMBOL(cmdq_mbox_mtcmos_by_fast_chan);

void cmdq_mbox_mtcmos_by_fast(void *cmdq_mbox, bool on)
{
	struct cmdq *cmdq;

	if (cmdq_mbox)
		cmdq = cmdq_mbox;
	else
		cmdq = g_cmdq[1];

	cmdq_mtcmos_by_fast(cmdq, on);
}
EXPORT_SYMBOL(cmdq_mbox_mtcmos_by_fast);

void cmdq_mbox_dump_fast_mtcmos(void *cmdq_mbox)
{
	struct cmdq *cmdq = cmdq_mbox;

	if (!cmdq || !cmdq->fast_mtcmos)
		return;

	cmdq_util_msg("hwid:%u fast_en:%llu fast_dis:%llu fast_usage:%d",
		cmdq->hwid, cmdq->fast_en, cmdq->fast_dis,
		atomic_read(&cmdq->fast_mtcmos_usage));
}
EXPORT_SYMBOL(cmdq_mbox_dump_fast_mtcmos);

void cmdq_mbox_dump_gce_req(struct mbox_chan *chan)
{
	struct cmdq *cmdq = container_of(chan->mbox, typeof(*cmdq), mbox);

	cmdq_mtcmos_by_fast(cmdq, true);
	cmdq_msg("%s dump GCE_GCTL_VALUE:%#x", __func__, (u32)readl(cmdq->base + GCE_GCTL_VALUE));
	cmdq_mtcmos_by_fast(cmdq, false);
}
EXPORT_SYMBOL(cmdq_mbox_dump_gce_req);

char *cmdq_dump_pkt_usage(u32 hwid, char *buf_start, char *buf_end)
{
	struct cmdq *cmdq;
	uint i, j;

	cmdq = g_cmdq[hwid];
	if (!cmdq)
		return buf_start;

	for (i = 0; i < ARRAY_SIZE(cmdq->thread); i++) {
		for (j = 0; j < CMDQ_THRD_PKT_ARR_MAX; j++) {
			if(cmdq->thread[i].pkt_id_arr[j][CMDQ_PKT_ID_CNT] ||
				cmdq->thread[i].pkt_id_arr[j][CMDQ_PKT_BUFFER_CNT])
				buf_start += scnprintf(buf_start, buf_end - buf_start,
					"hwid %d thrd:%d id:%d [%u][%u]\n", hwid, i, j,
					cmdq->thread[i].pkt_id_arr[j][CMDQ_PKT_ID_CNT],
					cmdq->thread[i].pkt_id_arr[j][CMDQ_PKT_BUFFER_CNT]);
		}
	}

	return buf_start;
}
EXPORT_SYMBOL(cmdq_dump_pkt_usage);

struct cmdq_thread *cmdq_get_thread(u8 thread_idx, u8 hwid)
{
	if(hwid >= 2 || thread_idx >= ARRAY_SIZE(g_cmdq[hwid]->thread))
		return NULL;

	return &g_cmdq[hwid]->thread[thread_idx];
}
EXPORT_SYMBOL(cmdq_get_thread);

dma_addr_t cmdq_thread_get_pc(struct cmdq_thread *thread)
{
	struct cmdq *cmdq = container_of(thread->chan->mbox, struct cmdq, mbox);

	if (atomic_read(&cmdq->usage) <= 0)
		return 0;

	return CMDQ_REG_REVERT_ADDR((dma_addr_t)readl(thread->base + CMDQ_THR_CURR_ADDR));
}

dma_addr_t cmdq_thread_get_end(struct cmdq_thread *thread)
{
	dma_addr_t end = readl(thread->base + CMDQ_THR_END_ADDR);

	return CMDQ_REG_REVERT_ADDR(end);
}

static void cmdq_thread_set_pc(struct cmdq_thread *thread, dma_addr_t pc)
{
	writel(CMDQ_REG_SHIFT_ADDR(pc), thread->base + CMDQ_THR_CURR_ADDR);
}

static void cmdq_thread_set_end(struct cmdq_thread *thread, dma_addr_t end)
{
	writel(CMDQ_REG_SHIFT_ADDR(end), thread->base + CMDQ_THR_END_ADDR);
}

void cmdq_thread_set_spr(struct mbox_chan *chan, u8 id, u32 val)
{
	struct cmdq *cmdq = container_of(chan->mbox, typeof(*cmdq), mbox);

	if(id < CMDQ_GPR_CNT_ID)
		return;
	cmdq_mtcmos_by_fast(cmdq, true);
	writel(val, cmdq->base + GCE_GPR_R0_START + (id - CMDQ_GPR_CNT_ID) * 4);
	cmdq_mtcmos_by_fast(cmdq, false);
}
EXPORT_SYMBOL(cmdq_thread_set_spr);

static void cmdq_mbox_set_resource_req(u8 hwid, bool sw_mode, bool req_on, u8 req_bit)
{
	u32 val;
	u32 req_sel = (0x1 << req_bit);
	u32 req = (0x10000 << req_bit);

	cmdq_log("%s in, hwid:%d sw_mode:%d on:%d req_bit:%d",
		__func__, hwid, sw_mode, req_on, req_bit);

	if (!g_cmdq[hwid]) {
		cmdq_err("%s g_cmdq[%d] is NULL", __func__, hwid);
		return;
	}
	val = readl(g_cmdq[hwid]->base + GCE_GCTL_VALUE);
	if (sw_mode) {
		if (req_on)
			writel((val | req_sel) | req,
				g_cmdq[hwid]->base + GCE_GCTL_VALUE);
		else
			writel((val | req_sel) & ~req,
				g_cmdq[hwid]->base + GCE_GCTL_VALUE);
	} else {
		writel((val & ~req_sel) & ~req,
			g_cmdq[hwid]->base + GCE_GCTL_VALUE);
	}
	val = readl(g_cmdq[hwid]->base + GCE_GCTL_VALUE);
	cmdq_log("%s hwid:%d val:%#x", __func__, hwid, val);
}

static int cmdq_core_reset(struct cmdq *cmdq)
{
	cmdq_msg("%s hwid:%d", __func__, cmdq->hwid);
	writel(CMDQ_THR_DO_HARD_RESET, cmdq->base + CMDQ_CORE_REST);
	writel(0, cmdq->base + CMDQ_CORE_REST);
	return 0;
}

static void cmdq_task_hw_trace_check(struct cmdq_task *task)
{
	if (!cmdq_hw_trace || hw_trace_built_in[task->cmdq->hwid])
		return;

	if (cmdq_util_is_secure_client(task->pkt->cl))
		return;

	if (!cmdq_get_event(task->thread->chan, CMDQ_TOKEN_HW_TRACE_LOCK)) {
		cmdq_err("event %d set:%d, event %d set:%d",
			CMDQ_TOKEN_HW_TRACE_WAIT, CMDQ_TOKEN_HW_TRACE_LOCK,
			cmdq_get_event(task->thread->chan, CMDQ_TOKEN_HW_TRACE_WAIT),
			cmdq_get_event(task->thread->chan, CMDQ_TOKEN_HW_TRACE_LOCK));
		cmdq_set_event(task->thread->chan, CMDQ_TOKEN_HW_TRACE_LOCK);
	}
}

static int cmdq_thread_suspend(struct cmdq *cmdq, struct cmdq_thread *thread)
{
	u32 status;

#if IS_ENABLED(CONFIG_CMDQ_MMPROFILE_SUPPORT)
	mmprofile_log_ex(cmdq_mmp.thread_suspend, MMPROFILE_FLAG_PULSE,
		MMP_THD(thread, cmdq), CMDQ_THR_SUSPEND);
#endif
	if (cmdq_hw_trace)
		cmdq_log("%s: hwid:%hu idx:%hu",
			__func__, cmdq->hwid, thread->idx);

	writel(CMDQ_THR_SUSPEND, thread->base + CMDQ_THR_SUSPEND_TASK);

	/* If already disabled, treat as suspended successful. */
	if (!(readl(thread->base + CMDQ_THR_ENABLE_TASK) & CMDQ_THR_ENABLED))
		return 0;

	if (readl_poll_timeout_atomic(thread->base + CMDQ_THR_CURR_STATUS,
			status, status & CMDQ_THR_STATUS_SUSPENDED, 0, 100)) {
		cmdq_err("suspend GCE %u thread %u failed",
			cmdq->hwid, thread->idx);
		if (thread->chan)
			cmdq_chan_dump_dbg(thread->chan);
		return -EFAULT;
	}

	return 0;
}

static void cmdq_thread_resume(struct cmdq_thread *thread)
{
	struct cmdq *cmdq = container_of(
		thread->chan->mbox, typeof(*cmdq), mbox);

	writel(CMDQ_THR_RESUME, thread->base + CMDQ_THR_SUSPEND_TASK);
#if IS_ENABLED(CONFIG_CMDQ_MMPROFILE_SUPPORT)
	mmprofile_log_ex(cmdq_mmp.thread_suspend, MMPROFILE_FLAG_PULSE,
		MMP_THD(thread, cmdq), CMDQ_THR_RESUME);
#endif
	if (cmdq_hw_trace)
		cmdq_log("%s: hwid:%hu idx:%hu",
			__func__, cmdq->hwid, thread->idx);
}

int cmdq_thread_reset(struct cmdq *cmdq, struct cmdq_thread *thread)
{
	u32 warm_reset;

	writel(CMDQ_THR_DO_WARM_RESET, thread->base + CMDQ_THR_WARM_RESET);
	if (readl_poll_timeout_atomic(thread->base + CMDQ_THR_WARM_RESET,
			warm_reset, !(warm_reset & CMDQ_THR_DO_WARM_RESET),
			0, 10)) {
		cmdq_err("reset GCE thread %u failed", thread->idx);
		if (thread->chan)
			cmdq_chan_dump_dbg(thread->chan);
		return -EFAULT;
	}
	writel(CMDQ_THR_ACTIVE_SLOT_CYCLES, cmdq->base + CMDQ_THR_SLOT_CYCLES);
	return 0;
}

static void cmdq_thread_err_reset(struct cmdq *cmdq, struct cmdq_thread *thread,
	dma_addr_t pc, u32 thd_pri)
{
	u32 i, spr[4], cookie;
	dma_addr_t end;

	for (i = 0; i < 4; i++)
		spr[i] = readl(thread->base + CMDQ_THR_SPR + i * 4);
	end = cmdq_thread_get_end(thread);
	cookie = readl(thread->base + CMDQ_THR_CNT);

	cmdq_msg(
		"reset backup pc:%pa end:%pa cookie:0x%08x spr:0x%x 0x%x 0x%x 0x%x",
		&pc, &end, cookie, spr[0], spr[1], spr[2], spr[3]);
	WARN_ON(cmdq_thread_reset(cmdq, thread) < 0);

	for (i = 0; i < 4; i++)
		writel(spr[i], thread->base + CMDQ_THR_SPR + i * 4);
	writel(CMDQ_INST_CYCLE_TIMEOUT, thread->base + CMDQ_THR_INST_CYCLES);
	cmdq_thread_set_end(thread, end);
	cmdq_thread_set_pc(thread, pc);
	writel(cookie, thread->base + CMDQ_THR_CNT);
	writel(thd_pri, thread->base + CMDQ_THR_CFG);
	writel(CMDQ_THR_IRQ_EN, thread->base + CMDQ_THR_IRQ_ENABLE);
	writel(CMDQ_THR_ENABLED, thread->base + CMDQ_THR_ENABLE_TASK);
#if IS_ENABLED(CONFIG_CMDQ_MMPROFILE_SUPPORT)
	mmprofile_log_ex(cmdq_mmp.thread_en, MMPROFILE_FLAG_PULSE,
		MMP_THD(thread, cmdq), CMDQ_THR_ENABLED);
#endif
}

static void cmdq_thread_disable(struct cmdq *cmdq, struct cmdq_thread *thread)
{
#if IS_ENABLED(CONFIG_CMDQ_MMPROFILE_SUPPORT)
	mmprofile_log_ex(cmdq_mmp.thread_en, MMPROFILE_FLAG_PULSE,
		MMP_THD(thread, cmdq), CMDQ_THR_DISABLED);
#endif
	// power
	if (mminfra_power_cb && !mminfra_power_cb())
		cmdq_err("hwid:%u idx:%u mbox_enable:%llu mbox_disable:%llu usage:%d mminfra power not enable",
			cmdq->hwid, thread->idx, thread->mbox_en, thread->mbox_dis,
			atomic_read(&thread->usage));
	// clock
	if (mminfra_gce_cg && !mminfra_gce_cg(cmdq->hwid))
		cmdq_err("hwid:%u idx:%u mbox_enable:%llu mbox_disable:%llu usage:%d gce clock not enable",
			cmdq->hwid, thread->idx, thread->mbox_en, thread->mbox_dis,
			atomic_read(&thread->usage));

	cmdq_thread_reset(cmdq, thread);
	writel(CMDQ_THR_DISABLED, thread->base + CMDQ_THR_ENABLE_TASK);
#if IS_ENABLED(CONFIG_MTK_CMDQ_MBOX_EXT)
	if (cmdq->sw_ddr_en && cmdq_util_controller->thread_ddr_module(thread->idx)) {
		unsigned long flags;

		spin_lock_irqsave(&cmdq->lock, flags);
		writel(readl(cmdq->base + GCE_DEBUG_START_ADDR) - 1,
			cmdq->base + GCE_DEBUG_START_ADDR);
		spin_unlock_irqrestore(&cmdq->lock, flags);
	}
#endif
}

/* notify GCE to re-fetch commands by setting GCE thread PC */
static void cmdq_thread_invalidate_fetched_data(struct cmdq_thread *thread)
{
	cmdq_thread_set_pc(thread, cmdq_thread_get_pc(thread));
}

static void cmdq_task_connect_buffer(struct cmdq_task *task,
	struct cmdq_task *next_task)
{
	u64 *task_base;
	struct cmdq_pkt_buffer *buf;
	u64 inst;

	/* let previous task jump to this task */
	buf = list_last_entry(&task->pkt->buf, typeof(*buf), list_entry);
	task_base = (u64 *)(buf->va_base + CMDQ_CMD_BUFFER_SIZE -
		task->pkt->avail_buf_size - CMDQ_INST_SIZE);
	inst = *task_base;

	if (!next_task) {
		*task_base = (u64)CMDQ_JUMP_BY_OFFSET << 32 | 0x00000001;
		cmdq_log("%s connect to null change last inst %#018llx to %#018llx connect 0x%p -> NULL",
			__func__, inst, *task_base, task->pkt);
#if IS_ENABLED(CONFIG_VHOST_CMDQ)
		arch_wb_cache_pmem(task_base, CMDQ_INST_SIZE);
#endif
		return;
	}

	*task_base = (u64)CMDQ_JUMP_BY_PA << 32 |
		CMDQ_REG_SHIFT_ADDR(next_task->pa_base);

#if IS_ENABLED(CONFIG_VHOST_CMDQ)
	arch_wb_cache_pmem(task_base, CMDQ_INST_SIZE);
#endif

	next_task->pkt->append.pre_last_inst = *task_base;
	cmdq_log("change last inst %#018llx to %#018llx connect 0x%p -> 0x%p",
		inst, *task_base, task->pkt, next_task->pkt);

	if (inst == *task_base)
		cmdq_err("change inst fail: %#018llx to %#018llx connect 0x%p -> 0x%p",
			inst, *task_base, task->pkt, next_task->pkt);
}

static void *cmdq_task_current_va(dma_addr_t pa, struct cmdq_pkt *pkt)
{
	struct cmdq_pkt_buffer *buf;
	dma_addr_t end;

	list_for_each_entry(buf, &pkt->buf, list_entry) {
		if (list_is_last(&buf->list_entry, &pkt->buf))
			end = CMDQ_BUF_ADDR(buf) + CMDQ_CMD_BUFFER_SIZE -
				pkt->avail_buf_size;
		else
			end = CMDQ_BUF_ADDR(buf) + CMDQ_CMD_BUFFER_SIZE;
		if (pa >= CMDQ_BUF_ADDR(buf) && pa < end)
			return buf->va_base + (pa - CMDQ_BUF_ADDR(buf));
	}

	return NULL;
}

size_t cmdq_task_current_offset(dma_addr_t pa, struct cmdq_pkt *pkt)
{
	struct cmdq_pkt_buffer *buf;
	dma_addr_t end;
	size_t offset = 0;

	list_for_each_entry(buf, &pkt->buf, list_entry) {
		if (list_is_last(&buf->list_entry, &pkt->buf))
			end = CMDQ_BUF_ADDR(buf) + CMDQ_CMD_BUFFER_SIZE -
				pkt->avail_buf_size;
		else
			end = CMDQ_BUF_ADDR(buf) + CMDQ_CMD_BUFFER_SIZE;
		if (pa >= CMDQ_BUF_ADDR(buf) && pa < end)
			return offset + (pa - CMDQ_BUF_ADDR(buf));
		offset += CMDQ_CMD_BUFFER_SIZE;
	}

	return 0;
}
EXPORT_SYMBOL(cmdq_task_current_offset);

static bool cmdq_task_is_current_run(dma_addr_t pa, struct cmdq_pkt *pkt)
{
	if (cmdq_task_current_va(pa, pkt))
		return true;
	return false;
}

static void cmdq_task_insert_into_thread(dma_addr_t curr_pa,
	struct cmdq_task *task, struct list_head **insert_pos)
{
	struct cmdq_thread *thread = task->thread;
	struct cmdq_task *prev_task = NULL, *next_task = NULL, *cursor_task;

	list_for_each_entry_reverse(cursor_task, &thread->task_busy_list,
		list_entry) {
		prev_task = cursor_task;
		if (next_task)
			next_task->pkt->priority += CMDQ_MIN_AGE_VALUE;
		/* stop if we found current running task */
		if (cmdq_task_is_current_run(curr_pa, prev_task->pkt))
			break;
		/* stop if new task priority lower than this one */
		if (prev_task->pkt->priority >= task->pkt->priority)
			break;
		next_task = prev_task;
	}

	*insert_pos = &prev_task->list_entry;
	cmdq_task_connect_buffer(prev_task, task);
	if (next_task && next_task != prev_task) {
		cmdq_msg("reorder pkt:0x%p(%u) next pkt:0x%p(%u) pc:%pa",
			task->pkt, task->pkt->priority,
			next_task->pkt, next_task->pkt->priority, &curr_pa);
		cmdq_task_connect_buffer(task, next_task);
	}
}

static void cmdq_task_callback(struct cmdq_pkt *pkt, s32 err)
{
	struct cmdq_cb_data cmdq_cb_data;

	if (pkt->cb.cb) {
		cmdq_cb_data.err = err;
		cmdq_cb_data.data = pkt->cb.data;
		pkt->cb.cb(cmdq_cb_data);
	}
}

static void cmdq_task_err_callback(struct cmdq_pkt *pkt, s32 err)
{
	struct cmdq_cb_data cmdq_cb_data;

	if (pkt->err_cb.cb) {
		cmdq_cb_data.err = err;
		cmdq_cb_data.data = pkt->err_cb.data;
		pkt->err_cb.cb(cmdq_cb_data);
	}
}

static dma_addr_t cmdq_task_get_end_pa(struct cmdq_pkt *pkt)
{
	struct cmdq_pkt_buffer *buf;

	/* let previous task jump to this task */
	buf = list_last_entry(&pkt->buf, typeof(*buf), list_entry);
	return CMDQ_BUF_ADDR(buf) + CMDQ_CMD_BUFFER_SIZE - pkt->avail_buf_size;
}

static void *cmdq_task_get_end_va(struct cmdq_pkt *pkt)
{
	struct cmdq_pkt_buffer *buf;

	/* let previous task jump to this task */
	buf = list_last_entry(&pkt->buf, typeof(*buf), list_entry);
	return buf->va_base + CMDQ_CMD_BUFFER_SIZE - pkt->avail_buf_size;
}

void cmdq_init_cmds(void *dev_cmdq)
{
	struct cmdq *cmdq = dev_cmdq;
	struct cmdq_thread *thread = &cmdq->thread[0];
	dma_addr_t pc, end;
	u32 status;

	cmdq_trace_ex_begin("%s", __func__);

	pc = cmdq->init_cmds;
	end = cmdq->init_cmds + CMDQ_EVENT_MAX * CMDQ_INST_SIZE;

	cmdq_thread_reset(cmdq, thread);
	cmdq_thread_set_end(thread, end);
	cmdq_thread_set_pc(thread, pc);
	writel(CMDQ_THR_ENABLED, thread->base + CMDQ_THR_ENABLE_TASK);

	end = CMDQ_REG_SHIFT_ADDR(end);
	if (readl_poll_timeout_atomic(thread->base + CMDQ_THR_CURR_ADDR,
		pc, pc == end, 0, 100)) {
		cmdq_err("clear event instructions timeout pc:%#lx end:%#lx",
			(unsigned long)pc,
			(unsigned long)end);
		writel(CMDQ_THR_SUSPEND, thread->base + CMDQ_THR_SUSPEND_TASK);
		if (readl_poll_timeout_atomic(thread->base + CMDQ_THR_CURR_STATUS,
				status, status & CMDQ_THR_STATUS_SUSPENDED, 0, 1000)) {
			cmdq_err("suspend GCE thread 0x%x failed",
				(u32)(thread->base - cmdq->base));
		}
		cmdq_core_reset(cmdq);
		cmdq_thread_reset(cmdq, thread);
		cmdq_init_cpu(cmdq);
	}
	writel(CMDQ_THR_DISABLED, thread->base + CMDQ_THR_ENABLE_TASK);

	cmdq_trace_ex_end();
}

void cmdq_thread_pause(struct cmdq_thread *thread, bool lock)
{
	struct cmdq *cmdq;
	unsigned long flags;
	u32 event_val = 0;

	if (!thread || !append_by_event)
		return;
	cmdq = dev_get_drvdata(thread->chan->mbox->dev);
	if (!cmdq)
		return;

	if (lock) {
		spin_lock_irqsave(&cmdq->lock, flags);
		cmdq_clear_event(thread->chan, CMDQ_TOKEN_PAUSE_TASK_0 + thread->idx);
		event_val = cmdq_get_event(thread->chan,
			CMDQ_TOKEN_PAUSE_TASK_0 + thread->idx);
		spin_unlock_irqrestore(&cmdq->lock, flags);
		if (event_val != 0)
			cmdq_log("%s clear event fail, event[%u] = %u", __func__,
				(CMDQ_TOKEN_PAUSE_TASK_0 + thread->idx), event_val);
	} else {
		spin_lock_irqsave(&cmdq->lock, flags);
		cmdq_set_event(thread->chan, CMDQ_TOKEN_PAUSE_TASK_0 + thread->idx);
		event_val = cmdq_get_event(thread->chan,
			CMDQ_TOKEN_PAUSE_TASK_0 + thread->idx);
		spin_unlock_irqrestore(&cmdq->lock, flags);
		if (event_val == 0)
			cmdq_log("%s set event fail, event[%u] = %u", __func__,
				(CMDQ_TOKEN_PAUSE_TASK_0 + thread->idx), event_val);
	}
}

static void cmdq_task_exec(struct cmdq_pkt *pkt, struct cmdq_thread *thread)
{
	struct cmdq *cmdq;
	struct cmdq_task *task, *last_task;
	dma_addr_t curr_pa, pkt_end_pa, end_pa, dma_handle;
	struct list_head *insert_pos;
	struct cmdq_pkt_buffer *buf;
	size_t offset = 0;
	bool thrd_suspend = false;
	unsigned long flags;
	s32 i = 0, alloc_retry_cnt = 5;

	cmdq = dev_get_drvdata(thread->chan->mbox->dev);

	cmdq_trace_begin("%s hwid:%d thrd:%d pkt:%p",
		__func__, cmdq->hwid, thread->idx , pkt);

	/* Client should not flush new tasks if suspended. */
	WARN_ON(cmdq->suspended);

	buf = list_first_entry_or_null(&pkt->buf, typeof(*buf),
			list_entry);
	if (!buf) {
		cmdq_err("no command to execute");
		cmdq_trace_end("%s pkt:%p", __func__, pkt);
		return;
	}
	dma_handle = CMDQ_BUF_ADDR(buf);

	cmdq_mtcmos_by_fast(cmdq, true);

	if (list_empty(&thread->task_busy_list)) {
		// power
		if (mminfra_power_cb && !mminfra_power_cb()) {
			cmdq_err("hwid:%u idx:%u mbox_enable:%llu mbox_disable:%llu usage:%d mminfra power not enable",
				cmdq->hwid, thread->idx, thread->mbox_en, thread->mbox_dis,
				atomic_read(&thread->usage));
			cmdq_mtcmos_by_fast(cmdq, false);
			cmdq_trace_end("%s pkt:%p", __func__, pkt);
			return;
		}
		// clock
		if (mminfra_gce_cg && !mminfra_gce_cg(cmdq->hwid)) {
			cmdq_err("hwid:%u idx:%u mbox_enable:%llu mbox_disable:%llu usage:%d gce clock not enable",
				cmdq->hwid, thread->idx, thread->mbox_en, thread->mbox_dis,
				atomic_read(&thread->usage));
			cmdq_mtcmos_by_fast(cmdq, false);
			cmdq_trace_end("%s pkt:%p", __func__, pkt);
			return;
		}
	}

	do {
		task = kzalloc(sizeof(*task), GFP_ATOMIC);
		if (task)
			break;
		cmdq_err("alloc task fail, retry cnt:%d", i);
	} while (++i < alloc_retry_cnt);

	if (!task) {
		cmdq_task_callback(pkt, -ENOMEM);
		cmdq_mtcmos_by_fast(cmdq, false);
		cmdq_trace_end("%s pkt:%p", __func__, pkt);
		return;
	}
	pkt->task_alloc = true;

#if IS_ENABLED(CONFIG_CMDQ_MMPROFILE_SUPPORT)
	mmprofile_log_ex(cmdq_mmp.submit, MMPROFILE_FLAG_PULSE,
		MMP_THD(thread, cmdq), (unsigned long)pkt);
	mmprofile_log_ex(cmdq_mmp.pkt_size, MMPROFILE_FLAG_PULSE,
		(unsigned long)pkt, (unsigned long)pkt->cmd_buf_size);
#endif

	task->cmdq = cmdq;
	INIT_LIST_HEAD(&task->list_entry);
	task->pa_base = dma_handle;
	task->thread = thread;
	task->pkt = pkt;
	task->exec_time = sched_clock();

	if (atomic_read(&cmdq->usage) <= 0)
		cmdq_err("hwid:%hu usage:%d idx:%u usage:%d gce off",
			cmdq->hwid, atomic_read(&cmdq->usage),
			thread->idx, atomic_read(&thread->usage));

	if (list_empty(&thread->task_busy_list)) {

		WARN_ON(cmdq_thread_reset(cmdq, thread) < 0);

		cmdq_log(
			"%s: pa:%pa iova:%pa size:%zu hwid:%hu usage:%d idx:%u usage:%d",
			__func__,
			&buf->pa_base, &buf->iova_base, pkt->cmd_buf_size,
			cmdq->hwid, atomic_read(&cmdq->usage),
			thread->idx, atomic_read(&thread->usage));

#if IS_ENABLED(CONFIG_MTK_CMDQ_MBOX_EXT)
		if (cmdq->sw_ddr_en &&
			cmdq_util_controller->thread_ddr_module(thread->idx)) {

			spin_lock_irqsave(&cmdq->lock, flags);
			writel(readl(cmdq->base + GCE_GCTL_VALUE) | (1 << 16),
				cmdq->base + GCE_GCTL_VALUE);
			writel(readl(cmdq->base + GCE_DEBUG_START_ADDR) + 1,
				cmdq->base + GCE_DEBUG_START_ADDR);
			spin_unlock_irqrestore(&cmdq->lock, flags);
		}
#endif

		writel(CMDQ_INST_CYCLE_TIMEOUT,
			thread->base + CMDQ_THR_INST_CYCLES);
		writel(thread->priority & CMDQ_THR_PRIORITY,
			thread->base + CMDQ_THR_CFG);
		cmdq_thread_set_end(thread, cmdq_task_get_end_pa(pkt)
			- (pkt->loop ? 0 : CMDQ_INST_SIZE));
		cmdq_thread_set_pc(thread, task->pa_base);

		if (append_by_event)
			cmdq_thread_pause(thread, false);

		writel(CMDQ_THR_IRQ_EN, thread->base + CMDQ_THR_IRQ_ENABLE);
		writel(CMDQ_THR_ENABLED, thread->base + CMDQ_THR_ENABLE_TASK);

#if IS_ENABLED(CONFIG_CMDQ_MMPROFILE_SUPPORT)
		mmprofile_log_ex(cmdq_mmp.thread_en, MMPROFILE_FLAG_PULSE,
			MMP_THD(thread, cmdq), CMDQ_THR_ENABLED);
#endif

		pkt_end_pa = cmdq_task_get_end_pa(pkt);
		cmdq_log("set pc:%pa end:%pa pkt:0x%p",
			&task->pa_base,
			&end_pa,
			pkt);

		if (thread->timeout_ms != CMDQ_NO_TIMEOUT) {
			mod_timer(&thread->timeout, jiffies +
				msecs_to_jiffies(thread->timeout_ms));
			thread->timer_mod = sched_clock();
			thread->cookie = readl(thread->base + CMDQ_THR_CNT);
		}
		list_move_tail(&task->list_entry, &thread->task_busy_list);
	} else {
		/* no warn on here to prevent slow down cpu */

		if (append_by_event) {
			cmdq_thread_pause(thread, true);

			/* reorder case */
			last_task = list_last_entry(&thread->task_busy_list,
				typeof(*task), list_entry);
			if (last_task->pkt->priority < task->pkt->priority) {
				cmdq_thread_suspend(cmdq, thread);
				thrd_suspend = true;
			}
		} else {
			cmdq_thread_suspend(cmdq, thread);
				thrd_suspend = true;
		}

		curr_pa = cmdq_thread_get_pc(thread);
		end_pa = cmdq_thread_get_end(thread);

		if (!thrd_suspend) {
			offset = cmdq_task_current_offset(curr_pa, last_task->pkt);
			if (offset >= last_task->pkt->pause_offset) {
				cmdq_thread_suspend(cmdq, thread);
				thrd_suspend = true;
				curr_pa = cmdq_thread_get_pc(thread);
				end_pa = cmdq_thread_get_end(thread);
			}
		}

		task->pkt->append.suspend = thrd_suspend;
		task->pkt->append.pc[0] = curr_pa;
		task->pkt->append.end = end_pa;

		cmdq_log("curr task %pa~%pa thread->base:0x%p thread:%u",
			&curr_pa, &end_pa, thread->base, thread->idx);

		/* check boundary */
		if (curr_pa == end_pa) {
			/* set to this task directly */
			cmdq_thread_set_pc(thread, task->pa_base);
			last_task = list_last_entry(&thread->task_busy_list,
				typeof(*task), list_entry);
			insert_pos = &last_task->list_entry;
			cmdq_log("set pc:%pa pkt:0x%p",
				&task->pa_base, task->pkt);
		} else {
			cmdq_task_insert_into_thread(curr_pa, task,
				&insert_pos);
			if (thrd_suspend)
				cmdq_thread_invalidate_fetched_data(thread);
			smp_mb(); /* modify jump before enable thread */
		}
		list_add(&task->list_entry, insert_pos);
		last_task = list_last_entry(&thread->task_busy_list,
			typeof(*task), list_entry);
		cmdq_thread_set_end(thread,
			cmdq_task_get_end_pa(last_task->pkt)
			- (pkt->loop ? 0 : CMDQ_INST_SIZE));
		pkt_end_pa = cmdq_task_get_end_pa(last_task->pkt);
		cmdq_log("set end:%pa pkt:0x%p",
			&pkt_end_pa,
			last_task->pkt);

		curr_pa = cmdq_thread_get_pc(thread);
		task->pkt->append.pc[1] = curr_pa;

		if (append_by_event)
			cmdq_thread_pause(thread, false);

		if (thread->dirty) {
			cmdq_err("new task during error on thread:%u",
				thread->idx);
		} else {
			/* safe to go */
			if (thrd_suspend)
				cmdq_thread_resume(thread);
		}
	}

#if IS_ENABLED(CONFIG_MTK_CMDQ_MBOX_EXT)
	pkt->rec_trigger = sched_clock();
#endif
	cmdq_mtcmos_by_fast(cmdq, false);
	cmdq_trace_end("%s pkt:%p", __func__, pkt);
}

static void cmdq_task_exec_done(struct cmdq_task *task, s32 err)
{
	u32 *perf, hw_time = 0, exec_begin = 0, exec_end = 0;
	unsigned long hw_time_rem = 0;

	perf = cmdq_pkt_get_perf_ret(task->pkt);
	if (perf) {
		exec_begin = perf[0];
		exec_end = perf[1];
		hw_time = exec_end > exec_begin ?
			exec_end - exec_begin :
			~exec_begin + 1 + exec_end;
		hw_time_rem = (u32)CMDQ_TICK_TO_US(hw_time);
	}
#if IS_ENABLED(CONFIG_CMDQ_MMPROFILE_SUPPORT)
	mmprofile_log_ex(cmdq_mmp.hw_exec, MMPROFILE_FLAG_PULSE,
		(unsigned long)task->pkt, (unsigned long)hw_time);
#endif
#if IS_ENABLED(CONFIG_MTK_CMDQ_MBOX_EXT)
	task->pkt->rec_irq = sched_clock();
#endif
	cmdq_trace_begin("%s hwid:%d thrd:%d pkt:%p err:%d submit:%llu rec_irq:%llu hw_time:%u.%06lu",
		__func__, task->cmdq->hwid, task->thread->idx , task->pkt, err,
		task->exec_time, task->pkt->rec_irq, hw_time, hw_time_rem);
	cmdq_task_callback(task->pkt, err);
	cmdq_log("pkt:0x%p done err:%d", task->pkt, err);
	task->end_time = sched_clock();
	list_del_init(&task->list_entry);
	cmdq_trace_end("%s pkt:%p", __func__, task->pkt);
}

static void cmdq_buf_dump_schedule(struct cmdq_task *task, bool timeout,
				   dma_addr_t pa_curr)
{
	struct cmdq_pkt_buffer *buf;
	u64 *inst = NULL;

	list_for_each_entry(buf, &task->pkt->buf, list_entry) {
		if (!(pa_curr >= CMDQ_BUF_ADDR(buf) &&
			pa_curr < CMDQ_BUF_ADDR(buf) + CMDQ_CMD_BUFFER_SIZE)) {
			continue;
		}
		inst = (u64 *)(buf->va_base + (pa_curr - CMDQ_BUF_ADDR(buf)));
		break;
	}

	cmdq_util_user_err(task->thread->chan,
		"task:0x%p timeout:%s pkt:0x%p size:%zu pc:%pa inst:0x%016llx",
		task, timeout ? "true" : "false", task->pkt,
		task->pkt->cmd_buf_size, &pa_curr,
		inst ? *inst : -1);
}

static void cmdq_task_handle_error(struct cmdq_task *task)
{
	struct cmdq_thread *thread = task->thread;
	struct cmdq_task *next_task;

	cmdq_err("task 0x%p pkt 0x%p error", task, task->pkt);
	WARN_ON(cmdq_thread_suspend(task->cmdq, thread) < 0);
	next_task = list_first_entry_or_null(&thread->task_busy_list,
			struct cmdq_task, list_entry);
	if (next_task)
		cmdq_thread_set_pc(thread, next_task->pa_base);
	cmdq_thread_resume(thread);
}

static void cmdq_thread_dump_pkt_by_pc(struct cmdq_thread *thread, const u64 pc,
	const bool prev)
{
	struct cmdq_task *task;
	struct cmdq_pkt_buffer *buf;

	list_for_each_entry(task, &thread->task_busy_list, list_entry) {
		if (prev)
			cmdq_dump_pkt(task->pkt, pc, true);
		list_for_each_entry(buf, &task->pkt->buf, list_entry) {
			if ((pc >= CMDQ_BUF_ADDR(buf)) &&
				(pc < (CMDQ_BUF_ADDR(buf) + CMDQ_CMD_BUFFER_SIZE))) {
				if (!prev)
					cmdq_dump_pkt(task->pkt, pc, true);
				return;
			}
		}
	}
}

static void cmdq_thread_irq_handler(struct cmdq *cmdq,
	struct cmdq_thread *thread, struct list_head *removes)
{
	struct cmdq_task *task, *tmp, *curr_task = NULL;
	u32 irq_flag;
	dma_addr_t curr_pa, task_end_pa;
	s32 err = 0;
	unsigned long flags;
	bool task_done = false;
#if IS_ENABLED(CONFIG_DEVICE_MODULES_ARM_SMMU_V3)
	bool ret = false;
	u32 axid[1];
#endif
#if IS_ENABLED(CONFIG_MTK_IRQ_MONITOR_DEBUG)
	u64 start = sched_clock(), end[4];
	u32 end_cnt = 0;
#endif

	cmdq_thrd_irq_history_record(cmdq->hwid, thread->idx);

	cmdq_mtcmos_by_fast(cmdq, true);
	if (atomic_read(&cmdq->usage) <= 0 ||
		(mminfra_power_cb && !mminfra_power_cb()) ||
		(mminfra_gce_cg && !mminfra_gce_cg(cmdq->hwid))) {
		cmdq_log("irq handling during gce off gce:%lx thread:%u",
			(unsigned long)cmdq->base_pa, thread->idx);
		cmdq_mtcmos_by_fast(cmdq, false);
		return;
	}
	cmdq_mtcmos_by_fast(cmdq, false);

	irq_flag = readl(thread->base + CMDQ_THR_IRQ_STATUS);
	if (cmdq->error_irq_sw_req && (irq_flag & CMDQ_THR_IRQ_ERROR)) {
		//dump smmu rg
#if IS_ENABLED(CONFIG_DEVICE_MODULES_ARM_SMMU_V3)
		if (cmdq->smmu_v3_enabled) {
			axid[0] = cmdq->axid;
			ret = cmdq_util_controller->check_tf(cmdq->mbox.dev,
				cmdq->sid, cmdq->tbu, axid);
		}
#endif
		//gce sw mode ddr_en/apsrc/emi
		writel(readl(cmdq->base + GCE_GCTL_VALUE) | ((0x7 << 16) + 0x7),
			cmdq->base + GCE_GCTL_VALUE);
		cmdq->err_irq = true;
		//check dram pwr on(spm)
		if (cmdq->dram_pwr_base) {
			while ((readl(cmdq->dram_pwr_base + 0x104) & 0x4000) != 0x4000)
				cmdq_err("apsrc ack:%#x", readl(cmdq->dram_pwr_base + 0x104));
			while ((readl(cmdq->dram_pwr_base + 0x190) & 0x40000000) == 0x40000000)
				cmdq_err("ddren ack:%#x", readl(cmdq->dram_pwr_base + 0x190));
		}
	}

	writel(~irq_flag, thread->base + CMDQ_THR_IRQ_STATUS);

	cmdq_log("irq flag:%#x gce:%lx idx:%u",
		irq_flag, (unsigned long)cmdq->base_pa, thread->idx);

	/*
	 * When ISR call this function, another CPU core could run
	 * "release task" right before we acquire the spin lock, and thus
	 * reset / disable this GCE thread, so we need to check the enable
	 * bit of this GCE thread.
	 */
	if (!(readl(thread->base + CMDQ_THR_ENABLE_TASK) & CMDQ_THR_ENABLED)) {
		if (cmdq->err_irq && cmdq->error_irq_sw_req)
			writel(readl(cmdq->base + GCE_GCTL_VALUE) & ~(0x7),
				cmdq->base + GCE_GCTL_VALUE);
		return;
	}

	if (irq_flag & CMDQ_THR_IRQ_ERROR)
		err = -EINVAL;
	else
		err = 0;

	if (list_empty(&thread->task_busy_list))
		cmdq_err("empty! may we hang later?");

	curr_pa = cmdq_thread_get_pc(thread);
	task_end_pa = cmdq_thread_get_end(thread);

	if (err < 0) {
		struct iommu_domain *domain;
		phys_addr_t pa;

		cmdq_err("pc:%pa end:%pa err:%d gce base:%lx thread:%u",
			&curr_pa, &task_end_pa, err,
			(unsigned long)cmdq->base_pa, thread->idx);
		if (cmdq->dram_pwr_base && cmdq->error_irq_sw_req)
			cmdq_err("GCE_GCTL_VALUE:%#x apsrc ack:%#x ddren ack:%#x",
				readl(cmdq->base + GCE_GCTL_VALUE), readl(cmdq->dram_pwr_base + 0x104),
				readl(cmdq->dram_pwr_base + 0x190));

		cmdq_util_prebuilt_dump(
			cmdq->hwid, CMDQ_TOKEN_PREBUILT_DISP_WAIT); // set iova

		domain = iommu_get_domain_for_dev(cmdq->share_dev);
		if (domain) {
			pa = iommu_iova_to_phys(domain, curr_pa);
			cmdq_err("iova:%pa iommu pa:%pa", &curr_pa, &pa);
		} else
			cmdq_err("cannot get dev:%p domain", cmdq->mbox.dev);

		cmdq_thread_dump_pkt_by_pc(thread, curr_pa, true);
	}
#if IS_ENABLED(CONFIG_DEVICE_MODULES_ARM_SMMU_V3)
	if (cmdq->smmu_v3_enabled) {
		axid[0] = cmdq->axid;
		ret = cmdq_util_controller->check_tf(cmdq->mbox.dev,
			cmdq->sid, cmdq->tbu, axid);
		if (ret) {
			cmdq_err("mtk_smmu_tf_detect hwid:%d sid:0x%x axid:0x%x tbu:%x ret:%d",
				cmdq->hwid, cmdq->sid, cmdq->axid, cmdq->tbu, ret);
			cmdq_set_alldump(true);
			cmdq_thread_dump_pkt_by_pc(thread, curr_pa, true);
			cmdq_set_alldump(false);
		}
	}
#endif

	cmdq_log("task status %pa~%pa err:%d",
		&curr_pa, &task_end_pa, err);
#if IS_ENABLED(CONFIG_MTK_IRQ_MONITOR_DEBUG)
	end[end_cnt++] = sched_clock();
#endif
	task = list_first_entry_or_null(&thread->task_busy_list,
		struct cmdq_task, list_entry);
	if (task && task->pkt->loop) {
		cmdq_trace_begin("%s hwid:%d thrd:%d pkt:%p loop",
			__func__, cmdq->hwid, thread->idx , task->pkt);
		cmdq_log("task loop %p", &task->pkt);
		if (err)
			cmdq_err("irq flag:%#x hwid:%hu idx:%u pkt:%p loop",
				irq_flag, cmdq->hwid, thread->idx, task->pkt);

		cmdq_task_callback(task->pkt, err);

#if IS_ENABLED(CONFIG_MTK_CMDQ_MBOX_EXT)
		task->pkt->rec_irq = sched_clock();
#endif

#if IS_ENABLED(CONFIG_CMDQ_MMPROFILE_SUPPORT)
		mmprofile_log_ex(cmdq_mmp.loop_irq, MMPROFILE_FLAG_PULSE,
			MMP_THD(thread, cmdq), (unsigned long)task->pkt);
#endif

		if (!err) {
			cmdq_trace_end("%s pkt:%p", __func__, task->pkt);
			return;
		}
		cmdq_trace_end("%s pkt:%p", __func__, task->pkt);
	}

#if IS_ENABLED(CONFIG_VHOST_CMDQ)
	if (task && ret) {
		cmdq_err("[host] cmdq tf fault err: %d irq flag:%#x gce:%lx idx:%u hwid:%u pkt:%p",
			err, irq_flag, (unsigned long)cmdq->base_pa, thread->idx, cmdq->hwid, task->pkt);
		cmdq_task_callback(task->pkt, -EINVAL);
	}
#endif

	if (thread->dirty) {
		cmdq_log("task in error dump thread:%u pkt:0x%p",
			thread->idx, task ? task->pkt : NULL);
		if (cmdq->err_irq && cmdq->error_irq_sw_req)
			writel(readl(cmdq->base + GCE_GCTL_VALUE) & ~(0x7),
				cmdq->base + GCE_GCTL_VALUE);
		return;
	}

#if IS_ENABLED(CONFIG_CMDQ_MMPROFILE_SUPPORT)
	mmprofile_log_ex(cmdq_mmp.cmdq_irq, MMPROFILE_FLAG_PULSE,
		MMP_THD(thread, cmdq), task ? (unsigned long)task->pkt : 0);
#endif
#if IS_ENABLED(CONFIG_MTK_IRQ_MONITOR_DEBUG)
	end[end_cnt++] = sched_clock();
#endif
	list_for_each_entry_safe(task, tmp, &thread->task_busy_list,
				 list_entry) {
		thread->irq_task += 1;

		if (task->end_time)
			cmdq_util_err("thd:%d pkt:%p is done,start:%llu end:%llu",
				thread->idx, task->pkt, task->exec_time, task->end_time);
		task_end_pa = cmdq_task_get_end_pa(task->pkt);
		if (cmdq_task_is_current_run(curr_pa, task->pkt)) {
			curr_task = task;
		/* for some self trigger loop, notify it is still working */
			if (curr_task->pkt->self_loop && irq_flag) {
				cmdq_task_err_callback(curr_task->pkt, -EBUSY);
				task_done = true;
			}
		}

		if (!curr_task || curr_pa == task_end_pa - CMDQ_INST_SIZE) {
			if (curr_task && (curr_pa != task_end_pa)) {
				cmdq_log(
					"remove task that not ending pkt:0x%p %pa to %pa",
					curr_task->pkt, &curr_pa, &task_end_pa);
			}
			cmdq_task_exec_done(task, 0);
			spin_lock_irqsave(&cmdq->irq_removes_lock, flags);
			list_add_tail(&task->list_entry, &cmdq->irq_removes);
			spin_unlock_irqrestore(&cmdq->irq_removes_lock, flags);
			task_done = true;
		} else if (err) {
			cmdq_err("pkt:0x%p thread:%u err:%d",
				curr_task->pkt, thread->idx, err);
			cmdq_buf_dump_schedule(task, false, curr_pa);
			cmdq_task_hw_trace_check(task);
			cmdq_task_exec_done(task, err);
			cmdq_task_handle_error(curr_task);
			spin_lock_irqsave(&cmdq->irq_removes_lock, flags);
			list_add_tail(&task->list_entry, &cmdq->irq_removes);
			spin_unlock_irqrestore(&cmdq->irq_removes_lock, flags);
			task_done = true;
		}

		if (curr_task)
			break;
	}
#if IS_ENABLED(CONFIG_MTK_IRQ_MONITOR_DEBUG)
	end[end_cnt++] = sched_clock();
#endif
	task = list_first_entry_or_null(&thread->task_busy_list,
		struct cmdq_task, list_entry);
	if (!task) {
		cmdq_thread_disable(cmdq, thread);
		cmdq_log("empty task thread:%u", thread->idx);
	} else {
		if (task_done) {
			mod_timer(&thread->timeout, jiffies +
				msecs_to_jiffies(thread->timeout_ms));
			thread->timer_mod = sched_clock();
			thread->cookie = readl(thread->base + CMDQ_THR_CNT);
			cmdq_log("mod_timer pkt:0x%p timeout:%u thread:%u cookie:%d",
				task->pkt, thread->timeout_ms, thread->idx, thread->cookie);
		}
	}
	//gce hw mode ddr_en/apsrc/emi
	if (cmdq->err_irq && cmdq->error_irq_sw_req)
		writel(readl(cmdq->base + GCE_GCTL_VALUE) & ~(0x7),
			cmdq->base + GCE_GCTL_VALUE);
#if IS_ENABLED(CONFIG_MTK_IRQ_MONITOR_DEBUG)
	end[end_cnt] = sched_clock();
	if (end[end_cnt] - start >= 1000000 && !cmdq->irq_long_times) /* 1ms */
		cmdq_util_err(
			"IRQ_LONG:%llu reg:%llu loop:%llu list:%llu dis:%llu",
			end[end_cnt] - start, end[0] - start,
			end[1] - end[0], end[2] - end[1], end[3] - end[2]);
#endif
}

static irqreturn_t cmdq_irq_handler(int irq, void *dev)
{
	struct cmdq *cmdq = dev;
	unsigned long irq_status, flags = 0L, irq_idx_flag;
	int bit, i;
	bool secure_irq = false;
#if IS_ENABLED(CONFIG_MTK_IRQ_MONITOR_DEBUG)
	u32 thd_cnt = 0;
	u64 start = sched_clock(), end[4];
	u32 end_cnt = 0;
#endif

	cmdq_trace_begin("%s hwid:%d", __func__, cmdq->hwid);

	cmdq_mtcmos_by_fast(cmdq, true);
	if (atomic_read(&cmdq->usage) <= 0 ||
		(mminfra_gce_cg && !mminfra_gce_cg(cmdq->hwid))) {
		for (i = 0; i < ARRAY_SIZE(cmdq->thread); i++)
			if (!list_empty(&cmdq->thread[i].task_busy_list))
				cmdq_err(
					"hwid:%hu suspend:%d usage:%d idx:%d usage:%d still has tasks",
					cmdq->hwid, cmdq->suspended,
					atomic_read(&cmdq->usage), i,
					atomic_read(&cmdq->thread[i].usage));
		cmdq_mtcmos_by_fast(cmdq, false);
		return IRQ_HANDLED;
	}

#if IS_ENABLED(CONFIG_MTK_IRQ_MONITOR_DEBUG)
	end[end_cnt++] = sched_clock();
#endif
	if (cmdq->gce_vm) {
		irq_status = readl(cmdq->base + GCE_HOST_VM_IRQ_STATUS) & CMDQ_IRQ_MASK;
	} else
		irq_status = readl(cmdq->base + CMDQ_CURR_IRQ_STATUS) & CMDQ_IRQ_MASK;
	cmdq_log("gce:%lx irq: %#x, %#x",
		(unsigned long)cmdq->base_pa, (u32)irq_status,
		(u32)(irq_status ^ CMDQ_IRQ_MASK));
	if (!(irq_status ^ CMDQ_IRQ_MASK)) {
		cmdq_msg("not handle for empty status:0x%x",
			(u32)irq_status);
		cmdq_mtcmos_by_fast(cmdq, false);
		cmdq_trace_end();
		return IRQ_NONE;
	}
#if IS_ENABLED(CONFIG_MTK_IRQ_MONITOR_DEBUG)
	end[end_cnt++] = sched_clock();
#endif
	for (i = 0; i < ARRAY_SIZE(cmdq->thread); i++) {
		cmdq->thread[i].irq_time = 0;
		cmdq->thread[i].irq_task = 0;
	}

	for_each_clear_bit(bit, &irq_status, fls(CMDQ_IRQ_MASK)) {
		struct cmdq_thread *thread = &cmdq->thread[bit];
		u64 irq_time;

		cmdq_log("bit=%d, thread->base=%p", bit, thread->base);
		if (!thread->occupied) {
			secure_irq = true;
			continue;
		}

		irq_time = sched_clock();
		spin_lock_irqsave(&thread->chan->lock, flags);
		thread->lock_time = sched_clock() - irq_time;
		cmdq_thread_irq_handler(cmdq, thread, &cmdq->irq_removes);
		spin_unlock_irqrestore(&thread->chan->lock, flags);
		thread->irq_time = sched_clock() - irq_time;
#if IS_ENABLED(CONFIG_MTK_IRQ_MONITOR_DEBUG)
		thd_cnt += 1;
#endif
	}

	cmdq_mtcmos_by_fast(cmdq, false);
#if IS_ENABLED(CONFIG_MTK_IRQ_MONITOR_DEBUG)
	end[end_cnt++] = sched_clock();
#endif
	spin_lock_irqsave(&cmdq->irq_idx_lock, irq_idx_flag);
	set_bit(gce_thread_nr, &cmdq->err_irq_idx);
	spin_unlock_irqrestore(&cmdq->irq_idx_lock, irq_idx_flag);
	wake_up_interruptible(&cmdq->err_irq_wq);
#if IS_ENABLED(CONFIG_MTK_IRQ_MONITOR_DEBUG)
	end[end_cnt] = sched_clock();
	if (end[end_cnt] - start >= 5000000 && !cmdq->irq_long_times) { /* 5ms */
		cmdq_util_err(
			"IRQ_LONG:%llu atomic:%llu readl:%llu bit:%llu wakeup:%llu",
			end[end_cnt] - start, end[0] - start,
			end[1] - end[0], end[2] - end[1], end[3] - end[2]);
		for (i = 0; i < ARRAY_SIZE(cmdq->thread); i += 8) {
			struct cmdq_thread *thread = &cmdq->thread[i];

			cmdq_util_err(
				" hwid:%d thread:%u:%d %llu:%llu:%u:%llu %llu:%llu:%u:%llu %llu:%llu:%u:%llu %llu:%llu:%u:%llu %llu:%llu:%u:%llu %llu:%llu:%u:%llu %llu:%llu:%u:%llu %llu:%llu:%u:%llu",
				cmdq->hwid, thd_cnt, i, thread->lock_time, thread->irq_time,
				thread->irq_task, thread->user_cb_cost,
				(thread + 1)->lock_time, (thread + 1)->irq_time,
				(thread + 1)->irq_task, (thread + 1)->user_cb_cost,
				(thread + 2)->lock_time, (thread + 2)->irq_time,
				(thread + 2)->irq_task, (thread + 2)->user_cb_cost,
				(thread + 3)->lock_time, (thread + 3)->irq_time,
				(thread + 3)->irq_task, (thread + 3)->user_cb_cost,
				(thread + 4)->lock_time, (thread + 4)->irq_time,
				(thread + 4)->irq_task, (thread + 4)->user_cb_cost,
				(thread + 5)->lock_time, (thread + 5)->irq_time,
				(thread + 5)->irq_task, (thread + 5)->user_cb_cost,
				(thread + 6)->lock_time, (thread + 6)->irq_time,
				(thread + 6)->irq_task, (thread + 6)->user_cb_cost,
				(thread + 7)->lock_time, (thread + 7)->irq_time,
				(thread + 7)->irq_task, (thread + 7)->user_cb_cost);
		}
	}

	if (end[end_cnt] - start >= 5000000)
		cmdq->irq_long_times += 1;
#endif
	cmdq_trace_end();
	return secure_irq ? IRQ_NONE : IRQ_HANDLED;
}

static irqreturn_t cmdq_vm_irq_handler(int irq, void *dev)
{
	struct cmdq *cmdq = dev;
	unsigned long irq_status_vm, flags = 0L, irq_idx_flag;
	int bit, i;
	bool secure_irq = false;
#if IS_ENABLED(CONFIG_MTK_IRQ_MONITOR_DEBUG)
	u32 thd_cnt = 0;
	u64 start = sched_clock(), end[4];
	u32 end_cnt = 0;
#endif

	cmdq_trace_begin("%s hwid:%d", __func__, cmdq->hwid);

	cmdq_mtcmos_by_fast(cmdq, true);
	if (atomic_read(&cmdq->usage) <= 0 ||
		(mminfra_gce_cg && !mminfra_gce_cg(cmdq->hwid))) {
		for (i = 0; i < ARRAY_SIZE(cmdq->thread); i++)
			if (!list_empty(&cmdq->thread[i].task_busy_list))
				cmdq_err(
					"hwid:%hu suspend:%d usage:%d idx:%d usage:%d still has tasks",
					cmdq->hwid, cmdq->suspended,
					atomic_read(&cmdq->usage), i,
					atomic_read(&cmdq->thread[i].usage));
		cmdq_mtcmos_by_fast(cmdq, false);
		return IRQ_HANDLED;
	}

#if IS_ENABLED(CONFIG_MTK_IRQ_MONITOR_DEBUG)
	end[end_cnt++] = sched_clock();
#endif

	irq_status_vm = readl(cmdq->base + GCE_VM6_OFFSET + GCE_HOST_VM_IRQ_STATUS) & CMDQ_IRQ_MASK;
	cmdq_log("gce:%lx irq: %#x, %#x",
		(unsigned long)cmdq->base_pa, (u32)irq_status_vm,
		(u32)(irq_status_vm ^ CMDQ_IRQ_MASK));
	if (!(irq_status_vm ^ CMDQ_IRQ_MASK)) {
		cmdq_msg("not handle for empty status:0x%x",
			(u32)irq_status_vm);
		cmdq_mtcmos_by_fast(cmdq, false);
		cmdq_trace_end();
		return IRQ_NONE;
	}
#if IS_ENABLED(CONFIG_MTK_IRQ_MONITOR_DEBUG)
	end[end_cnt++] = sched_clock();
#endif
	for (i = 0; i < ARRAY_SIZE(cmdq->thread); i++) {
		cmdq->thread[i].irq_time = 0;
		cmdq->thread[i].irq_task = 0;
	}

	for_each_clear_bit(bit, &irq_status_vm, fls(CMDQ_IRQ_MASK)) {
		struct cmdq_thread *thread = &cmdq->thread[bit];
		u64 irq_time;

		cmdq_log("bit=%d, thread->base=%p", bit, thread->base);
		if (!thread->occupied) {
			secure_irq = true;
			continue;
		}

		irq_time = sched_clock();
		spin_lock_irqsave(&thread->chan->lock, flags);
		thread->lock_time = sched_clock() - irq_time;
		cmdq_thread_irq_handler(cmdq, thread, &cmdq->irq_removes);
		spin_unlock_irqrestore(&thread->chan->lock, flags);
		thread->irq_time = sched_clock() - irq_time;
#if IS_ENABLED(CONFIG_MTK_IRQ_MONITOR_DEBUG)
		thd_cnt += 1;
#endif
	}

	cmdq_mtcmos_by_fast(cmdq, false);
#if IS_ENABLED(CONFIG_MTK_IRQ_MONITOR_DEBUG)
	end[end_cnt++] = sched_clock();
#endif
	spin_lock_irqsave(&cmdq->irq_idx_lock, irq_idx_flag);
	set_bit(gce_thread_nr, &cmdq->err_irq_idx);
	spin_unlock_irqrestore(&cmdq->irq_idx_lock, irq_idx_flag);
	wake_up_interruptible(&cmdq->err_irq_wq);
#if IS_ENABLED(CONFIG_MTK_IRQ_MONITOR_DEBUG)
	end[end_cnt] = sched_clock();
	if (end[end_cnt] - start >= 5000000 && !cmdq->irq_long_times) { /* 5ms */
		cmdq_util_err(
			"IRQ_LONG:%llu atomic:%llu readl:%llu bit:%llu wakeup:%llu",
			end[end_cnt] - start, end[0] - start,
			end[1] - end[0], end[2] - end[1], end[3] - end[2]);
		for (i = 0; i < ARRAY_SIZE(cmdq->thread); i += 8) {
			struct cmdq_thread *thread = &cmdq->thread[i];

			cmdq_util_err(
				" hwid:%hu thread:%u:%d %llu:%llu:%u:%llu %llu:%llu:%u:%llu %llu:%llu:%u:%llu %llu:%llu:%u:%llu %llu:%llu:%u:%llu %llu:%llu:%u:%llu %llu:%llu:%u:%llu %llu:%llu:%u:%llu",
				cmdq->hwid, thd_cnt, i, thread->lock_time, thread->irq_time,
				thread->irq_task, thread->user_cb_cost,
				(thread + 1)->lock_time, (thread + 1)->irq_time,
				(thread + 1)->irq_task, (thread + 1)->user_cb_cost,
				(thread + 2)->lock_time, (thread + 2)->irq_time,
				(thread + 2)->irq_task, (thread + 2)->user_cb_cost,
				(thread + 3)->lock_time, (thread + 3)->irq_time,
				(thread + 3)->irq_task, (thread + 3)->user_cb_cost,
				(thread + 4)->lock_time, (thread + 4)->irq_time,
				(thread + 4)->irq_task, (thread + 4)->user_cb_cost,
				(thread + 5)->lock_time, (thread + 5)->irq_time,
				(thread + 5)->irq_task, (thread + 5)->user_cb_cost,
				(thread + 6)->lock_time, (thread + 6)->irq_time,
				(thread + 6)->irq_task, (thread + 6)->user_cb_cost,
				(thread + 7)->lock_time, (thread + 7)->irq_time,
				(thread + 7)->irq_task, (thread + 7)->user_cb_cost);
		}
	}

	if (end[end_cnt] - start >= 5000000)
		cmdq->irq_long_times += 1;
#endif
	cmdq_trace_end();
	return secure_irq ? IRQ_NONE : IRQ_HANDLED;
}

static int cmdq_irq_handler_thread(void *data)
{
	struct cmdq *cmdq = data;
	unsigned long irq, flags, irq_idx_flag;

	while (!kthread_should_stop()) {
		/*
		 * read cmdq->err_irq_idx maybe encounter data race.
		 * use data_race to bypass
		 */
		if (wait_event_interruptible(cmdq->err_irq_wq, data_race(cmdq->err_irq_idx)) < 0)
			continue;

		spin_lock_irqsave(&cmdq->irq_idx_lock, irq_idx_flag);

		irq = cmdq->err_irq_idx;

		if (irq & BIT(gce_thread_nr)) {
			struct cmdq_task *task, *tmp;

			spin_lock_irqsave(&cmdq->irq_removes_lock, flags);
			list_for_each_entry_safe(task, tmp, &cmdq->irq_removes,
				list_entry) {
				list_del(&task->list_entry);

				spin_unlock_irqrestore(
					&cmdq->irq_removes_lock, flags);
				kfree(task);
				spin_lock_irqsave(
					&cmdq->irq_removes_lock, flags);
			}
			spin_unlock_irqrestore(&cmdq->irq_removes_lock, flags);

			clear_bit(gce_thread_nr, &cmdq->err_irq_idx);
		}
		spin_unlock_irqrestore(&cmdq->irq_idx_lock, irq_idx_flag);
	}

	return 0;
}

static bool cmdq_thread_timeout_excceed(struct cmdq_thread *thread)
{
	struct cmdq *cmdq = container_of(thread->chan->mbox, struct cmdq, mbox);
	u64 duration;

	/* If first time exec time stamp smaller than timeout value,
	 * it is last round timeout. Skip it.
	 */
	duration = div_s64(sched_clock() - thread->timer_mod, 1000000);
	if (duration < thread->timeout_ms) {
		mod_timer(&thread->timeout, jiffies +
			msecs_to_jiffies(thread->timeout_ms - duration));
		thread->cookie = readl(thread->base + CMDQ_THR_CNT);

		cmdq_msg(
			"thread:%u usage:%d mod time:%llu dur:%llu cookie:%d timeout not excceed",
			thread->idx, atomic_read(&cmdq->usage),
			thread->timer_mod, duration, thread->cookie);
		return false;
	}

	return true;
}

static bool cmdq_thread_skip_timeout_by_cookie(struct cmdq_thread *thread)
{
	u32 cookie;
	struct cmdq_task *task;
	struct cmdq *cmdq = container_of(thread->chan->mbox, typeof(*cmdq), mbox);

	cookie = readl(thread->base + CMDQ_THR_CNT);
	task = list_first_entry_or_null(&thread->task_busy_list,
		struct cmdq_task, list_entry);
	if (task) {
		if (task->pkt->self_loop && cookie != thread->cookie) {
#if IS_ENABLED(CONFIG_CMDQ_MMPROFILE_SUPPORT)
			mmprofile_log_ex(cmdq_mmp.warning, MMPROFILE_FLAG_PULSE,
				MMP_THD(thread, cmdq), cookie);
#endif
			mod_timer(&thread->timeout, jiffies +
				msecs_to_jiffies(thread->timeout_ms));
			cmdq_msg("%s pre_cookie:%d cookie:%d pass timeout flow",
				__func__, thread->cookie, cookie);
			thread->cookie = cookie;
			return true;
		}
	}

	return false;
}

static void cmdq_thread_handle_timeout_work(struct work_struct *work_item)
{
	struct cmdq_thread *thread = container_of(work_item,
		struct cmdq_thread, timeout_work);
	struct cmdq *cmdq = container_of(thread->chan->mbox, struct cmdq, mbox);
	struct cmdq_task *task, *tmp, *timeout_task = NULL;
	unsigned long flags;
	bool first_task = true;
	dma_addr_t pa_curr;
	struct list_head removes;

	INIT_LIST_HEAD(&removes);

	spin_lock_irqsave(&thread->chan->lock, flags);
	if (list_empty(&thread->task_busy_list)) {
		spin_unlock_irqrestore(&thread->chan->lock, flags);
		return;
	}

	cmdq_mtcmos_by_fast(cmdq, true);
	/* Check before suspend thread to prevent hurt performance. */
	if (!cmdq_thread_timeout_excceed(thread)) {
		spin_unlock_irqrestore(&thread->chan->lock, flags);
		cmdq_mtcmos_by_fast(cmdq, false);
		return;
	}

	/* After IRQ, first task may change. */
	if (cmdq_thread_skip_timeout_by_cookie(thread)) {
		spin_unlock_irqrestore(&thread->chan->lock, flags);
		cmdq_mtcmos_by_fast(cmdq, false);
		return;
	}

	WARN_ON(cmdq_thread_suspend(cmdq, thread) < 0);

	/*
	 * Although IRQ is disabled, GCE continues to execute.
	 * It may have pending IRQ before GCE thread is suspended,
	 * so check this condition again.
	 */
	cmdq_thread_irq_handler(cmdq, thread, &removes);

	if (list_empty(&thread->task_busy_list)) {
		cmdq_msg("thread:%u empty after irq handle in timeout",
			thread->idx);
		goto unlock_free_done;
	}

	/* After IRQ, first task may change. */
	if (!cmdq_thread_timeout_excceed(thread)) {
		cmdq_thread_resume(thread);
		goto unlock_free_done;
	}

	cmdq_util_user_err(thread->chan,
		"timeout for thread:0x%p idx:%u usage:%d",
		thread->base, thread->idx, atomic_read(&cmdq->usage));

	pa_curr = cmdq_thread_get_pc(thread);

	list_for_each_entry_safe(task, tmp, &thread->task_busy_list,
		list_entry) {
		bool curr_task = cmdq_task_is_current_run(pa_curr, task->pkt);

		if (first_task) {
			cmdq_buf_dump_schedule(task, true, pa_curr);
			first_task = false;
		}

		if (curr_task) {
			timeout_task = task;
			break;
		}

		cmdq_msg("ending not curr in timeout pkt:0x%p curr_pa:%pa",
			task->pkt, &pa_curr);
		cmdq_task_exec_done(task, 0);
		kfree(task);
	}

#if IS_ENABLED(CONFIG_CMDQ_MMPROFILE_SUPPORT)
	mmprofile_log_ex(cmdq_mmp.warning, MMPROFILE_FLAG_PULSE,
		MMP_THD(thread, cmdq),
		timeout_task ? (unsigned long)timeout_task : 0);
#endif

	if (timeout_task) {
		thread->dirty = true;
		spin_unlock_irqrestore(&thread->chan->lock, flags);

		cmdq_task_err_callback(timeout_task->pkt, -ETIMEDOUT);

		if(timeout_task->pkt->timeout_dump_hw_trace)
			cmdq_util_hw_trace_dump(
				cmdq->hwid, cmdq_util_get_bit_feature() & CMDQ_LOG_FEAT_PERF);

		spin_lock_irqsave(&thread->chan->lock, flags);
		thread->dirty = false;

		task = list_first_entry_or_null(&thread->task_busy_list,
			struct cmdq_task, list_entry);
		if (timeout_task == task) {
			cmdq_task_hw_trace_check(task);
			cmdq_task_exec_done(task, -ETIMEDOUT);
			kfree(task);
		} else {
			cmdq_err("task list changed");
		}
	}

	task = list_first_entry_or_null(&thread->task_busy_list,
		struct cmdq_task, list_entry);
	if (task) {
		mod_timer(&thread->timeout, jiffies +
			msecs_to_jiffies(thread->timeout_ms));
		thread->timer_mod = sched_clock();
		thread->cookie = readl(thread->base + CMDQ_THR_CNT);
		cmdq_thread_err_reset(cmdq, thread,
			task->pa_base, thread->priority);
		cmdq_thread_resume(thread);
	} else {
		cmdq_thread_resume(thread);
		cmdq_thread_disable(cmdq, thread);
	}

unlock_free_done:
	spin_unlock_irqrestore(&thread->chan->lock, flags);

	list_for_each_entry_safe(task, tmp, &removes, list_entry) {
		list_del(&task->list_entry);
		kfree(task);
	}
	cmdq_mtcmos_by_fast(cmdq, false);
}
static void cmdq_thread_handle_timeout(struct timer_list *t)
{
	struct cmdq_thread *thread = from_timer(thread, t, timeout);
	struct cmdq *cmdq = container_of(thread->chan->mbox, struct cmdq, mbox);
	unsigned long flags;
	bool empty;

	spin_lock_irqsave(&thread->chan->lock, flags);
	empty = list_empty(&thread->task_busy_list);
	spin_unlock_irqrestore(&thread->chan->lock, flags);
	if (empty)
		return;

	if (!work_pending(&thread->timeout_work)) {
		cmdq_log("queue cmdq timeout thread:%u", thread->idx);
		queue_work(cmdq->timeout_wq, &thread->timeout_work);
	} else {
		cmdq_msg("ignore cmdq timeout thread:%u", thread->idx);
	}
}

void cmdq_dump_core(struct mbox_chan *chan)
{
	struct cmdq *cmdq = dev_get_drvdata(chan->mbox->dev);
	struct cmdq_thread *thread = (struct cmdq_thread *)chan->con_priv;
	u32 irq, loaded, cycle, thd_timer, tpr_mask, tpr_en, bus_gctl;

	cmdq_mtcmos_by_fast(cmdq, true);

	if (atomic_read(&cmdq->usage) <= 0 ||
		(mminfra_power_cb && !mminfra_power_cb()) ||
		(mminfra_gce_cg && !mminfra_gce_cg(cmdq->hwid))) {
		cmdq_err("gce off cmdq:%p thread:%u", cmdq, thread->idx);
		dump_stack();
		cmdq_mtcmos_by_fast(cmdq, false);
		return;
	}

	irq = readl(cmdq->base + CMDQ_CURR_IRQ_STATUS);
	loaded = readl(cmdq->base + CMDQ_CURR_LOADED_THR);
	cycle = readl(cmdq->base + CMDQ_THR_EXEC_CYCLES);
	thd_timer = readl(cmdq->base + CMDQ_THR_TIMEOUT_TIMER);
	tpr_mask = readl(cmdq->base + CMDQ_TPR_MASK);
	tpr_en = readl(cmdq->base + CMDQ_TPR_TIMEOUT_EN);
	bus_gctl = readl(cmdq->base + GCE_BUS_GCTL);

	cmdq_mtcmos_by_fast(cmdq, false);

	cmdq_util_user_msg(chan,
		"irq:%#x loaded:%#x cycle:%#x thd timer:%#x mask:%#x en:%#x bus_gctl:%#x",
		irq, loaded, cycle, thd_timer, tpr_mask, tpr_en, bus_gctl);
#if IS_ENABLED(CONFIG_MTK_CMDQ_MBOX_EXT)
	cmdq_chan_dump_dbg(chan);
#endif
}
EXPORT_SYMBOL(cmdq_dump_core);

void cmdq_thread_dump_spr(struct cmdq_thread *thread)
{
	struct cmdq *cmdq = container_of(thread->chan->mbox, struct cmdq, mbox);
	u32 i, spr[4] = {0}, dbg[2] = {0}, gpr[16] = {0};

	for (i = 0; i < 4; i++)
		spr[i] = readl(thread->base + CMDQ_THR_SPR + i * 4);
	dbg[0] = readl(cmdq->base + GCE_DEBUG_START_ADDR);
	dbg[1] = readl(cmdq->base + GCE_DEBUG_END_ADDR);

	cmdq_util_user_msg(thread->chan,
		"thrd:%u spr:%#x %#x %#x %#x dbg:%#x %#x",
		thread->idx, spr[0], spr[1], spr[2], spr[3], dbg[0], dbg[1]);

	for (i = 0; i < 16; i++)
		gpr[i] = readl(cmdq->base + GCE_GPR_R0_START + i * 4);

	cmdq_util_user_msg(thread->chan,
		"cmdq:%pa gpr:%#x %#x %#x %#x %#x %#x %#x %#x %#x %#x %#x %#x %#x %#x %#x %#x",
		&cmdq->base_pa, gpr[0], gpr[1], gpr[2], gpr[3], gpr[4], gpr[5],
		gpr[6], gpr[7], gpr[8], gpr[9], gpr[10], gpr[11], gpr[12],
		gpr[13], gpr[14], gpr[15]);
}
EXPORT_SYMBOL(cmdq_thread_dump_spr);

void cmdq_thread_dump(struct mbox_chan *chan, struct cmdq_pkt *cl_pkt,
	u64 **inst_out, dma_addr_t *pc_out)
{
	struct cmdq_thread *thread = (struct cmdq_thread *)chan->con_priv;
	struct cmdq *cmdq = container_of(thread->chan->mbox, struct cmdq, mbox);
	unsigned long flags;
	struct cmdq_task *task;
	struct cmdq_pkt_buffer *buf;

	struct cmdq_pkt *pkt = NULL;
	u32 warn_rst, en, suspend, status, irq, irq_en, cnt,
		wait_token, cfg, prefetch, pri = 0;
	size_t size = 0;
	u64 *end_va, *curr_va = NULL, inst = 0, last_inst = 0;
	void *va_base = NULL;
	dma_addr_t curr_pa, end_pa, pa_base;
	bool empty = true;
	u32 timeout_ms = cmdq_mbox_get_thread_timeout(chan);

	cmdq_mtcmos_by_fast(cmdq, true);

	/* lock channel and get info */
	spin_lock_irqsave(&chan->lock, flags);

	if (atomic_read(&cmdq->usage) <= 0 ||
		(mminfra_power_cb && !mminfra_power_cb()) ||
		(mminfra_gce_cg && !mminfra_gce_cg(cmdq->hwid))) {
		cmdq_err("%s gce off cmdq:%p thread:%u",
			__func__, cmdq, thread->idx);
		dump_stack();
		spin_unlock_irqrestore(&chan->lock, flags);
		cmdq_mtcmos_by_fast(cmdq, false);
		return;
	}

	warn_rst = readl(thread->base + CMDQ_THR_WARM_RESET);
	en = readl(thread->base + CMDQ_THR_ENABLE_TASK);
	suspend = readl(thread->base + CMDQ_THR_SUSPEND_TASK);
	status = readl(thread->base + CMDQ_THR_CURR_STATUS);
	irq = readl(thread->base + CMDQ_THR_IRQ_STATUS);
	irq_en = readl(thread->base + CMDQ_THR_IRQ_ENABLE);
	curr_pa = cmdq_thread_get_pc(thread);
	end_pa = cmdq_thread_get_end(thread);
	cnt = readl(thread->base + CMDQ_THR_CNT);
	wait_token = readl(thread->base + CMDQ_THR_WAIT_TOKEN);
	cfg = readl(thread->base + CMDQ_THR_CFG);
	prefetch = readl(thread->base + CMDQ_THR_PREFETCH);

	list_for_each_entry(task, &thread->task_busy_list, list_entry) {
		empty = false;

		if (curr_pa == cmdq_task_get_end_pa(task->pkt))
			curr_va = (u64 *)cmdq_task_get_end_va(task->pkt);
		else
			curr_va = (u64 *)cmdq_task_current_va(curr_pa,
				task->pkt);
		if (!curr_va)
			continue;
		inst = *curr_va;
		pkt = task->pkt;
		size = pkt->cmd_buf_size;
		pri = pkt->priority;

		buf = list_first_entry(&pkt->buf, typeof(*buf), list_entry);
		va_base = buf->va_base;
		pa_base = CMDQ_BUF_ADDR(buf);

		buf = list_last_entry(&pkt->buf, typeof(*buf), list_entry);
		end_va = (u64 *)(buf->va_base + CMDQ_CMD_BUFFER_SIZE -
			pkt->avail_buf_size - CMDQ_INST_SIZE);
		last_inst = *end_va;
		break;
	}
	spin_unlock_irqrestore(&chan->lock, flags);

	cmdq_util_user_msg(chan,
		"thd:%u timeout:0x%x pc:%pa(%p) inst:%#018llx end:%pa cnt:%#x token:%#010x",
		thread->idx, timeout_ms, &curr_pa, curr_va, inst, &end_pa, cnt, wait_token);
	cmdq_util_user_msg(chan,
		"rst:%#x en:%#x suspend:%#x status:%#x irq:%x en:%#x cfg:%#x prefetch:%#x",
		warn_rst, en, suspend, status, irq, irq_en, cfg, prefetch);
	cmdq_thread_dump_spr(thread);

	if (pkt) {
		cmdq_util_user_msg(chan,
			"cur pkt:0x%p size:%zu va:0x%p pa:%pa priority:%u",
			pkt, size, va_base, &pa_base, pri);
		cmdq_util_user_msg(chan, "last inst %#018llx", last_inst);

		if (cl_pkt && cl_pkt != pkt) {
			buf = list_first_entry(&cl_pkt->buf, typeof(*buf),
				list_entry);
			cmdq_util_user_msg(chan,
				"expect pkt:0x%p size:%zu va:0x%p pa:%pa iova:%pa priority:%u",
				cl_pkt, cl_pkt->cmd_buf_size, buf->va_base,
				&buf->pa_base, &buf->iova_base, cl_pkt->priority);

			curr_va = NULL;
			curr_pa = 0;
		}
	} else {
		/* empty or not found case is critical */
		cmdq_util_msg("pkt not available (%s)",
			empty ? "thread empty" : "pc not match");
	}

/* if pc match end and irq flag on, dump irq status */
	if (curr_pa == end_pa && irq) {
		cmdq_util_msg("gic dump not support irq id:%u\n", cmdq->irq);
		mt_irq_dump_status(cmdq->irq);
	}

	if (inst_out)
		*inst_out = curr_va;
	if (pc_out)
		*pc_out = curr_pa;

	cmdq_mtcmos_by_fast(cmdq, false);
}
EXPORT_SYMBOL(cmdq_thread_dump);

void cmdq_mbox_dump_dbg(void *mbox_cmdq, void *chan, const bool lock)
{
	struct cmdq *cmdq = mbox_cmdq;
	u32 id;
	unsigned long flags;
	void *base = cmdq->base;
	u32 dbg0[3], dbg2[6], dbg3, i;
	u64 dbg[5];

	if (!cmdq_tfa_read_dbg && !base) {
		cmdq_util_msg("no cmdq dbg since no base");
		return;
	}

	if (lock)
		spin_lock_irqsave(&cmdq->lock, flags);
	if (atomic_read(&cmdq->usage) <= 0 ||
		(mminfra_power_cb && !mminfra_power_cb()) ||
		(mminfra_gce_cg && !mminfra_gce_cg(cmdq->hwid))) {
		cmdq_util_msg("no cmdq dbg since mbox disable");
		if (lock)
			spin_unlock_irqrestore(&cmdq->lock, flags);
		return;
	}

	id = cmdq_util_get_hw_id((u32)cmdq->base_pa);
	cmdq_util_enable_dbg(id);
	if (!cmdq_tfa_read_dbg) {
		/* debug select */
		for (i = 0; i < 6; i++) {
			if (i < 3) {
				writel((i << 8) | i, base + GCE_DBG_CTL);
				dbg0[i] = readl(base + GCE_DBG0);
			} else {
				/* only other part */
				writel(i << 8, base + GCE_DBG_CTL);
			}
			dbg2[i] = readl(base + GCE_DBG2);
		}

		dbg3 = readl(base + GCE_DBG3);
	} else
		cmdq_util_return_dbg(id, dbg);

	if (lock)
		spin_unlock_irqrestore(&cmdq->lock, flags);

	if (chan)
		if (cmdq_tfa_read_dbg)
			cmdq_util_user_msg(chan,
			"id:%u dbg0:%#x %#x %#x dbg2:%#x %#x %#x %#x %#x %#x dbg3:%#x",
			id,
			(u32)(dbg[0]>>32), (u32)dbg[0], (u32)(dbg[1]>>32),
			(u32)(dbg[2]>>32), (u32)dbg[2], (u32)(dbg[3]>>32),
			(u32)dbg[3], (u32)(dbg[4]>>32), (u32)dbg[4],
			(u32)dbg[1]);
		else
			cmdq_util_user_msg(chan,
			"id:%u dbg0:%#x %#x %#x dbg2:%#x %#x %#x %#x %#x %#x dbg3:%#x",
			id,
			dbg0[0], dbg0[1], dbg0[2],
			dbg2[0], dbg2[1], dbg2[2], dbg2[3], dbg2[4], dbg2[5],
			dbg3);
	else
		if (cmdq_tfa_read_dbg)
			cmdq_util_msg(
				"id:%u dbg0:%#x %#x %#x dbg2:%#x %#x %#x %#x %#x %#x dbg3:%#x",
				id,
				(u32)(dbg[0]>>32), (u32)dbg[0], (u32)(dbg[1]>>32),
				(u32)(dbg[2]>>32), (u32)dbg[2], (u32)(dbg[3]>>32),
				(u32)dbg[3], (u32)(dbg[4]>>32), (u32)dbg[4],
				(u32)dbg[1]);
		else
			cmdq_util_msg(
				"id:%u dbg0:%#x %#x %#x dbg2:%#x %#x %#x %#x %#x %#x dbg3:%#x",
				id,
				dbg0[0], dbg0[1], dbg0[2],
				dbg2[0], dbg2[1], dbg2[2], dbg2[3], dbg2[4], dbg2[5],
				dbg3);
}
EXPORT_SYMBOL(cmdq_mbox_dump_dbg);

void cmdq_chan_dump_dbg(void *chan)
{
	struct cmdq *cmdq = container_of(((struct mbox_chan *)chan)->mbox,
		typeof(*cmdq), mbox);

	cmdq_mtcmos_by_fast(cmdq, true);
	cmdq_mbox_dump_dbg(cmdq, chan, false);
	cmdq_mtcmos_by_fast(cmdq, false);
}
EXPORT_SYMBOL(cmdq_chan_dump_dbg);

void cmdq_thread_dump_all(void *mbox_cmdq, const bool lock, const bool dump_pkt,
	const bool dump_prev)
{
	struct cmdq *cmdq = mbox_cmdq;
	s32 usage = atomic_read(&cmdq->usage), i;

	cmdq_util_msg("%s: cmdq:%pa hwid:%hu usage:%d",
		__func__, &cmdq->base_pa, cmdq->hwid, usage);

	if (usage <= 0)
		return;

	cmdq_mtcmos_by_fast(cmdq, true);
	for (i = 0; i < ARRAY_SIZE(cmdq->thread); i++) {
		struct cmdq_thread *thread = &cmdq->thread[i];
		unsigned long flags = 0L;
		dma_addr_t curr_pa, end_pa;

		if (!thread->occupied || !thread->chan)
			continue;

		if (lock)
			spin_lock_irqsave(&thread->chan->lock, flags);

		if (list_empty(&thread->task_busy_list) ||
			!readl(thread->base + CMDQ_THR_ENABLE_TASK)) {
			if (lock)
				spin_unlock_irqrestore(
					&thread->chan->lock, flags);
			continue;
		}

		curr_pa = cmdq_thread_get_pc(thread);
		end_pa = cmdq_thread_get_end(thread);

		cmdq_util_msg("thd idx:%u pc:%pa end:%pa",
			thread->idx, &curr_pa, &end_pa);
		cmdq_thread_dump_spr(thread);

		if (dump_pkt)
			cmdq_thread_dump_pkt_by_pc(thread, curr_pa, dump_prev);

		if (lock)
			spin_unlock_irqrestore(&thread->chan->lock, flags);
	}
	cmdq_mtcmos_by_fast(cmdq, false);
}
EXPORT_SYMBOL(cmdq_thread_dump_all);

void cmdq_thread_dump_all_seq(void *mbox_cmdq, struct seq_file *seq)
{
	struct cmdq *cmdq = mbox_cmdq;
	u32 i;
	u32 en;
	dma_addr_t curr_pa, end_pa;
	s32 usage = atomic_read(&cmdq->usage);

	seq_printf(seq, "[cmdq] cmdq:%#x usage:%d\n",
		(u32)cmdq->base_pa, usage);
	if (usage <= 0)
		return;

	cmdq_mtcmos_by_fast(cmdq, true);
	for (i = 0; i < ARRAY_SIZE(cmdq->thread); i++) {
		struct cmdq_thread *thread = &cmdq->thread[i];

		if (!thread->occupied || list_empty(&thread->task_busy_list))
			continue;

		en = readl(thread->base + CMDQ_THR_ENABLE_TASK);
		if (!en)
			continue;

		curr_pa = cmdq_thread_get_pc(thread);
		end_pa = cmdq_thread_get_end(thread);

		seq_printf(seq, "[cmdq] thd idx:%u pc:%pa end:%pa\n",
			thread->idx, &curr_pa, &end_pa);
	}
	cmdq_mtcmos_by_fast(cmdq, false);
}
EXPORT_SYMBOL(cmdq_thread_dump_all_seq);

void cmdq_mbox_thread_remove_task(struct mbox_chan *chan,
	struct cmdq_pkt *pkt)
{
	struct cmdq_thread *thread = (struct cmdq_thread *)chan->con_priv;
	struct cmdq *cmdq = container_of(thread->chan->mbox, struct cmdq, mbox);
	struct cmdq_task *task, *tmp, *next_task, *prev_task;
	unsigned long flags;
	dma_addr_t pa_curr;
	bool curr_task = false;
	bool last_task = false;
	struct list_head removes;

	INIT_LIST_HEAD(&removes);

	cmdq_mtcmos_by_fast(cmdq, true);
	spin_lock_irqsave(&thread->chan->lock, flags);
	if (list_empty(&thread->task_busy_list)) {
		spin_unlock_irqrestore(&thread->chan->lock, flags);
		cmdq_mtcmos_by_fast(cmdq, false);
		return;
	}

	cmdq_msg("remove task from thread idx:%u usage:%d pkt:0x%p",
		thread->idx, atomic_read(&cmdq->usage), pkt);

	WARN_ON(cmdq_thread_suspend(cmdq, thread) < 0);

	/*
	 * Although IRQ is disabled, GCE continues to execute.
	 * It may have pending IRQ before GCE thread is suspended,
	 * so check this condition again.
	 */
	cmdq_thread_irq_handler(cmdq, thread, &removes);

	if (list_empty(&thread->task_busy_list)) {
		cmdq_err("thread:%u empty after irq handle in timeout",
			thread->idx);
		cmdq_thread_resume(thread);
		spin_unlock_irqrestore(&thread->chan->lock, flags);
		cmdq_mtcmos_by_fast(cmdq, false);
		return;
	}

	pa_curr = cmdq_thread_get_pc(thread);

	list_for_each_entry_safe(task, tmp, &thread->task_busy_list,
		list_entry) {
		if (task->pkt != pkt)
			continue;

		curr_task = cmdq_task_is_current_run(pa_curr, task->pkt);
		if (list_is_last(&task->list_entry, &thread->task_busy_list))
			last_task = true;

		if (task == list_first_entry(&thread->task_busy_list,
			typeof(*task), list_entry) &&
			thread->dirty) {
			/* task during error handling, skip */
			spin_unlock_irqrestore(&thread->chan->lock, flags);
			cmdq_mtcmos_by_fast(cmdq, false);
			return;
		}

		if (!curr_task) {
			next_task = last_task ? NULL : list_next_entry(task, list_entry);
			prev_task = list_prev_entry(task, list_entry);
			cmdq_task_connect_buffer(prev_task, next_task);
		}

		cmdq_task_exec_done(task, curr_task ? -ECONNABORTED : 0);
		kfree(task);
		break;
	}

	if (list_empty(&thread->task_busy_list)) {
		cmdq_thread_resume(thread);
		cmdq_thread_disable(cmdq, thread);
		spin_unlock_irqrestore(&thread->chan->lock, flags);
		cmdq_mtcmos_by_fast(cmdq, false);
		return;
	}

	if (curr_task) {
		task = list_first_entry(&thread->task_busy_list,
			typeof(*task), list_entry);

		cmdq_thread_set_pc(thread, task->pa_base);
		mod_timer(&thread->timeout, jiffies +
			msecs_to_jiffies(thread->timeout_ms));
		thread->timer_mod = sched_clock();
		thread->cookie = readl(thread->base + CMDQ_THR_CNT);
	}

	if (last_task) {
		/* reset end addr again if remove last task */
		task = list_last_entry(&thread->task_busy_list,
			typeof(*task), list_entry);
		cmdq_thread_set_end(thread, cmdq_task_get_end_pa(task->pkt));
	}

	cmdq_thread_resume(thread);
	spin_unlock_irqrestore(&thread->chan->lock, flags);
	cmdq_mtcmos_by_fast(cmdq, false);

	list_for_each_entry_safe(task, tmp, &removes, list_entry) {
		list_del(&task->list_entry);
		kfree(task);
	}
}
EXPORT_SYMBOL(cmdq_mbox_thread_remove_task);

void cmdq_check_thread_complete(struct mbox_chan *chan)
{
	struct cmdq *cmdq = container_of(chan->mbox, typeof(*cmdq), mbox);
	struct cmdq_thread *thread = (struct cmdq_thread *)chan->con_priv;
	unsigned long flags;

	spin_lock_irqsave(&thread->chan->lock, flags);
	if (list_empty(&thread->task_busy_list)) {
		spin_unlock_irqrestore(&thread->chan->lock, flags);
		return;
	}
	cmdq_mtcmos_by_fast(cmdq, true);
	cmdq_thread_irq_handler(cmdq, thread, &cmdq->irq_removes);
	cmdq_mtcmos_by_fast(cmdq, false);
	spin_unlock_irqrestore(&thread->chan->lock, flags);
}
EXPORT_SYMBOL(cmdq_check_thread_complete);

static void cmdq_mbox_thread_stop(struct cmdq_thread *thread)
{
	struct cmdq *cmdq = container_of(thread->chan->mbox, struct cmdq, mbox);
	struct cmdq_task *task, *tmp;
	unsigned long flags;
	struct list_head removes;
	struct cmdq_task *timeout_task = NULL;

	INIT_LIST_HEAD(&removes);

	cmdq_mtcmos_by_fast(cmdq, true);
	spin_lock_irqsave(&thread->chan->lock, flags);
	if (list_empty(&thread->task_busy_list)) {
		cmdq_log("stop empty thread:%u", thread->idx);
		spin_unlock_irqrestore(&thread->chan->lock, flags);
		cmdq_mtcmos_by_fast(cmdq, false);
		return;
	}

	WARN_ON(cmdq_thread_suspend(cmdq, thread) < 0);

	/*
	 * Although IRQ is disabled, GCE continues to execute.
	 * It may have pending IRQ before GCE thread is suspended,
	 * so check this condition again.
	 */
	cmdq_thread_irq_handler(cmdq, thread, &removes);
	if (list_empty(&thread->task_busy_list)) {
		cmdq_err("thread:%u empty after irq handle in disable thread",
			thread->idx);
		spin_unlock_irqrestore(&thread->chan->lock, flags);
		cmdq_mtcmos_by_fast(cmdq, false);
		return;
	}

	/* find timeout task */
	if (thread->dirty) {
		dma_addr_t pa_curr = cmdq_thread_get_pc(thread);

		list_for_each_entry_safe(task, tmp, &thread->task_busy_list,
			list_entry) {
			if (cmdq_task_is_current_run(pa_curr, task->pkt)) {
				timeout_task = task;
				break;
			}
		}
	}

	list_for_each_entry_safe(task, tmp, &thread->task_busy_list,
		list_entry) {
		/* ignore timeout task */
		if (timeout_task && (timeout_task == task))
			continue;

		cmdq_task_hw_trace_check(task);
		cmdq_task_exec_done(task, -ECONNABORTED);
		kfree(task);
	}

	if (list_empty(&thread->task_busy_list))
		cmdq_thread_disable(cmdq, thread);

	spin_unlock_irqrestore(&thread->chan->lock, flags);
	cmdq_mtcmos_by_fast(cmdq, false);

	list_for_each_entry_safe(task, tmp, &removes, list_entry) {
		list_del(&task->list_entry);
		kfree(task);
	}
}

void cmdq_mbox_channel_stop(struct mbox_chan *chan)
{
	cmdq_mbox_thread_stop(chan->con_priv);
}
EXPORT_SYMBOL(cmdq_mbox_channel_stop);

static int cmdq_suspend(struct device *dev)
{
	struct cmdq *cmdq = dev_get_drvdata(dev);
	s32 usage, i;

	usage = atomic_read(&cmdq->usage);
	if (cmdq->suspended)
		cmdq_err("hwid:%hu usage:%d suspended:%d not enable",
			cmdq->hwid, usage, cmdq->suspended);

	for (i = 0; i < ARRAY_SIZE(cmdq->thread); i++)
		if (atomic_read(&cmdq->thread[i].usage))
			cmdq_err("hwid:%hu usage:%d idx:%d usage:%d not zero",
				cmdq->hwid, usage,
				i, atomic_read(&cmdq->thread[i].usage));

	cmdq->suspended = true;
	cmdq_log("%s: hwid:%hu usage:%d suspended:%d",
		__func__, cmdq->hwid, usage, cmdq->suspended);
	return 0;
}

static int cmdq_resume(struct device *dev)
{
	struct cmdq *cmdq = dev_get_drvdata(dev);
	s32 usage, i;

	usage = atomic_read(&cmdq->usage);
	if (!cmdq->suspended)
		cmdq_err("hwid:%hu usage:%d suspended:%d not disable",
			cmdq->hwid, usage, cmdq->suspended);

	for (i = 0; i < ARRAY_SIZE(cmdq->thread); i++)
		if (atomic_read(&cmdq->thread[i].usage))
			cmdq_err("hwid:%hu usage:%d idx:%d usage:%d not zero",
				cmdq->hwid, usage,
				i, atomic_read(&cmdq->thread[i].usage));

	cmdq->suspended = false;
	cmdq_log("%s: hwid:%hu usage:%d suspended:%d",
		__func__, cmdq->hwid, usage, cmdq->suspended);
	return 0;
}

static int cmdq_remove(struct platform_device *pdev)
{
	struct cmdq *cmdq = platform_get_drvdata(pdev);

	destroy_workqueue(cmdq->buf_dump_wq);
	mbox_controller_unregister(&cmdq->mbox);
	return 0;
}

static int cmdq_mbox_send_data(struct mbox_chan *chan, void *data)
{
	cmdq_task_exec(data, chan->con_priv);
	return 0;
}

static int cmdq_mbox_startup(struct mbox_chan *chan)
{
	struct cmdq_thread *thread = chan->con_priv;

	thread->occupied = true;
	return 0;
}

static void cmdq_mbox_shutdown(struct mbox_chan *chan)
{
	struct cmdq_thread *thread = chan->con_priv;

	cmdq_mbox_thread_stop(chan->con_priv);
	thread->occupied = false;
}

static bool cmdq_mbox_last_tx_done(struct mbox_chan *chan)
{
	return true;
}

static const struct mbox_chan_ops cmdq_mbox_chan_ops = {
	.send_data = cmdq_mbox_send_data,
	.startup = cmdq_mbox_startup,
	.shutdown = cmdq_mbox_shutdown,
	.last_tx_done = cmdq_mbox_last_tx_done,
};

u32 cmdq_thread_timeout_backup(struct cmdq_thread *thread, const u32 ms)
{
	u32 backup = thread->timeout_ms;

	thread->timeout_ms = ms;
	return backup;
}
EXPORT_SYMBOL(cmdq_thread_timeout_backup);

void cmdq_thread_timeout_restore(struct cmdq_thread *thread, const u32 ms)
{
	thread->timeout_ms = ms;
}
EXPORT_SYMBOL(cmdq_thread_timeout_restore);

struct dma_pool *cmdq_alloc_user_pool(const char *name, struct device *dev)
{
	if (unlikely(!name))
		name = "cmdq";

	return dma_pool_create(name, mtk_smmu_get_shared_device(dev),
		CMDQ_BUF_ALLOC_SIZE, 0, 0);
}
EXPORT_SYMBOL(cmdq_alloc_user_pool);

static struct mbox_chan *cmdq_xlate(struct mbox_controller *mbox,
		const struct of_phandle_args *sp)
{
	int ind = sp->args[0];
	struct cmdq_thread *thread;

	if (ind >= mbox->num_chans)
		return ERR_PTR(-EINVAL);

	thread = mbox->chans[ind].con_priv;
	thread->timeout_ms = sp->args[1] != 0 ?
		sp->args[1] : CMDQ_TIMEOUT_DEFAULT;
	thread->priority = sp->args[2];
	thread->chan = &mbox->chans[ind];

	return &mbox->chans[ind];
}

static s32 cmdq_config_prefetch(struct device_node *np, struct cmdq *cmdq)
{
	u32 i, prefetch_cnt = 0, prefetchs[4] = {0};
	s32 err;

	cmdq->prefetch = 0;
	of_property_read_u32(np, "max_prefetch_cnt", &prefetch_cnt);
	if (!prefetch_cnt)
		return 0;

	if (prefetch_cnt > ARRAY_SIZE(prefetchs)) {
		cmdq_err("prefetch count more than expect:%u",
			prefetch_cnt);
		prefetch_cnt = ARRAY_SIZE(prefetchs);
	}

	err = of_property_read_u32_array(np, "prefetch_size",
		prefetchs, prefetch_cnt);
	if (err != 0) {
		/* print log but do notify error hw setting */
		cmdq_err("read prefetch count:%u size error:%d",
			prefetch_cnt, err);
		return -EINVAL;
	}

	if (!prefetch_cnt)
		return 0;

	for (i = 0; i < prefetch_cnt; i++)
		cmdq->prefetch |= (prefetchs[i] / 32 - 1) << (i * 4);

	cmdq_msg("prefetch size configure:0x%x", cmdq->prefetch);
	return 0;
}

static void cmdq_config_dma_mask(struct device *dev)
{
	u32 dma_mask_bit = 0;
	s32 ret;

	ret = of_property_read_u32(dev->of_node, "dma-mask-bit",
		&dma_mask_bit);
	/* if not assign from dts, give default 32bit for legacy chip */
	if (ret != 0 || !dma_mask_bit)
		dma_mask_bit = 32;
	ret = dma_set_coherent_mask(dev, DMA_BIT_MASK(dma_mask_bit));
	cmdq_msg("mbox set dma mask bit:%u result:%d\n",
		dma_mask_bit, ret);
}

static void cmdq_config_default_token(struct device *dev, struct cmdq *cmdq)
{
	int count, ret;

	count = of_property_count_u16_elems(dev->of_node, "default-tokens");
	if (count <= 0) {
		cmdq_err("no default tokens:%d", count);
		return;
	}

	cmdq->token_cnt = count;
	cmdq->tokens = devm_kcalloc(dev, count, sizeof(*cmdq->tokens),
		GFP_KERNEL);
	ret = of_property_read_u16_array(dev->of_node,
		"default-tokens", cmdq->tokens, count);
	if (ret < 0) {
		cmdq_err("of_property_read_u16_array fail err:%d", ret);
		cmdq->token_cnt = 0;
	}
}

static void cmdq_config_init_buf(struct device *dev, struct cmdq *cmdq)
{
	u32 i, *va;

	cmdq->init_cmds_base = dma_alloc_coherent(dev, CMDQ_INIT_BUF_SIZE,
		&cmdq->init_cmds, GFP_KERNEL);
	cmdq_msg("init cmd buffer:%#lx (%#lx)",
		(unsigned long)cmdq->init_cmds_base,
		(unsigned long)cmdq->init_cmds);

	if (!cmdq->init_cmds_base) {
		cmdq_err("fail to alloc init cmd buffer");
		return;
	}

	va = (u32 *)cmdq->init_cmds_base;
	for (i = 0; i < CMDQ_EVENT_MAX; i++) {
		va[i * 2] = 0x80000000;
		va[i * 2 + 1] = 0x20000000 | i;
	}

	/* some token default set to 1, config in dts */
	for (i = 0; i < cmdq->token_cnt; i++)
		va[cmdq->tokens[i] * 2] = 0x80010000;
}

int cmdq_iommu_fault_callback(int port, dma_addr_t mva, void *cb_data)
{
	struct cmdq *cmdq = (struct cmdq *)cb_data;
	struct iommu_domain *domain = iommu_get_domain_for_dev(
				mtk_smmu_get_shared_device(cmdq->mbox.dev));
	phys_addr_t pa = domain ? iommu_iova_to_phys(domain, mva) : 0;
	s32 i;

	cmdq_msg("%s: port:%d mva:%pa cmdq hwid:%hu iommu domain:%p pa:%pa",
		__func__, port, &mva, cmdq->hwid, domain, &pa);

	for (i = 0; i < ARRAY_SIZE(cmdq->thread); i++) {
		if (!cmdq->thread[i].occupied || !cmdq->thread[i].chan)
			continue;

		cmdq_dump_core(cmdq->thread[i].chan);
		break;
	}

	cmdq_set_alldump(true);
	cmdq_thread_dump_all(cmdq, true, true, true);
	cmdq_set_alldump(false);

	if (cmdq->err_irq) {
		cmdq_msg("%s error irq flag:%d", __func__, cmdq->err_irq);
#if !IS_ENABLED(CONFIG_VHOST_CMDQ)
		BUG_ON(1);
#endif
	}

	return 0;
}

static s32 cmdq_genpd_init(struct device *dev, struct cmdq *cmdq)
{
	int genpd_num = 0;
	s32 err = 0;

	genpd_num = of_count_phandle_with_args(dev->of_node,
						"power-domains",
						"#power-domain-cells");

	cmdq_msg("%s num:%d pm_domain:%p", __func__, genpd_num, dev->pm_domain);
	if (genpd_num <= 1) {
		cmdq->pd_mminfra_1 = dev;
		if (cmdq->fast_mtcmos)
			pm_runtime_irq_safe(cmdq->pd_mminfra_1);
	} else {
		cmdq->pd_mminfra_1 = dev_pm_domain_attach_by_id(dev, 0);
		if (IS_ERR_OR_NULL(cmdq->pd_mminfra_1)) {
			err = PTR_ERR(cmdq->pd_mminfra_1) ? : -ENODATA;
			cmdq_err("failed to get MMINFRA_1 error: %d", err);
			return err;
		}

		if (cmdq->fast_mtcmos)
			pm_runtime_irq_safe(cmdq->pd_mminfra_1);
		cmdq->pd_mminfra_ao = dev_pm_domain_attach_by_id(dev, 1);
		if (IS_ERR_OR_NULL(cmdq->pd_mminfra_ao)) {
			err = PTR_ERR(cmdq->pd_mminfra_ao) ? : -ENODATA;
			cmdq_err("failed to get MMINFRA_AO error: %d", err);
			return err;
		}
		pm_runtime_irq_safe(cmdq->pd_mminfra_ao);
	}

	pm_runtime_enable(dev);

	return 0;
}

static void cmdq_shutdown(struct platform_device *pdev)
{
	struct cmdq *cmdq = platform_get_drvdata(pdev);
	s32 usage, i;

	cmdq_msg("%s hwid:%d enter", __func__, cmdq->hwid);

	usage = atomic_read(&cmdq->usage);
	for (i = 0; i < ARRAY_SIZE(cmdq->thread); i++)
		if (!list_empty(&cmdq->thread[i].task_busy_list)) {
			cmdq_msg(
				"%s hwid:%hu usage:%d idx:%d still has tasks",
				__func__, cmdq->hwid, usage, i);
			cmdq_mbox_thread_stop(cmdq->thread);
		}
}

#if IS_ENABLED(CONFIG_MTK_IRQ_MONITOR)
static int cmdq_burst_irq_callback(unsigned int irq, enum irq_mon_aee_type type)
{
	u32 i;

	if (type != IRQ_MON_AEE_TYPE_BURST_IRQ)
		return -1;

	for (i = 0; i < 2; i++) {
		if (!g_cmdq[i])
			break;

		if(irq == g_cmdq[i]->irq)
			cmdq_dump_thrd_irq_history(g_cmdq[i]->hwid);
	}

	return 0;
}
#endif

static int cmdq_probe(struct platform_device *pdev)
{
	struct device_node *node;
	struct platform_device *smi;
	struct device_link *link;
	struct device *dev = &pdev->dev;
	struct resource *res;
	struct of_phandle_args args;
	struct cmdq *cmdq;
	struct task_struct *kthr;
	int err, i, smi_cnt;
	struct gce_plat *plat_data;
	static u8 hwid;
#if !IS_ENABLED(CONFIG_VIRTIO_CMDQ)
	int port;
#endif
	u32 dram_pwr_pa, mminfra_ao_pa;

	plat_data = (struct gce_plat *)of_device_get_match_data(dev);
	if (!plat_data) {
		cmdq_err("failed to get match data\n");
		return -EINVAL;
	}

	cmdq = devm_kzalloc(dev, sizeof(*cmdq), GFP_KERNEL);
	if (!cmdq)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		cmdq_err("failed to get resource");
		return -EINVAL;
	}

	cmdq->base = devm_ioremap_resource(dev, res);
	cmdq->base_pa = res->start;
	if (IS_ERR(cmdq->base)) {
		cmdq_err("failed to ioremap gce");
		return PTR_ERR(cmdq->base);
	}

	cmdq->irq = platform_get_irq(pdev, 0);
	if (!cmdq->irq) {
		cmdq_err("failed to get irq");
		return -EINVAL;
	}
#if IS_ENABLED(CONFIG_MTK_IRQ_MONITOR)
	irq_mon_aee_callback_register(cmdq->irq, cmdq_burst_irq_callback);
#endif
	err = devm_request_irq(dev, cmdq->irq, cmdq_irq_handler, IRQF_SHARED,
			       "mtk_cmdq", cmdq);
	if (err < 0) {
		cmdq_err("failed to register ISR (%d)", err);
		return err;
	}

	INIT_LIST_HEAD(&cmdq->irq_removes);
	spin_lock_init(&cmdq->irq_removes_lock);

	init_waitqueue_head(&cmdq->err_irq_wq);
	kthr = kthread_run(cmdq_irq_handler_thread, cmdq, "cmdq_irq_thread");

	if (IS_ERR(kthr))
		cmdq_err("Unable  to run kthread err %ld", PTR_ERR(kthr));

	gce_shift_bit = plat_data->shift;
	gce_mminfra = plat_data->mminfra;
	gce_thread_nr = plat_data->thread_nr;
	if (of_property_read_bool(dev->of_node, "skip-poll-sleep"))
		skip_poll_sleep = true;

	if (of_property_read_bool(dev->of_node, "gce-in-vcp"))
		gce_in_vcp = true;

	if (of_property_read_bool(dev->of_node, "support-gce-vm"))
		cmdq->gce_vm = true;

	if (of_property_read_bool(dev->of_node, "support-spr3-timer"))
		cmdq->spr3_timer = true;

#if IS_ENABLED(CONFIG_MTK_CMDQ_DEBUG)
	if (!of_property_read_bool(dev->of_node, "cmdq-log-perf-off"))
		cmdq_util_log_feature_set(NULL, CMDQ_LOG_FEAT_PERF);
#endif

	if (of_property_read_bool(dev->of_node, "cpr-not-support-cookie"))
		cpr_not_support_cookie = true;

	if (!of_property_read_bool(dev->of_node, "no-append-by-event"))
		append_by_event = true;

	if(of_property_read_bool(dev->of_node, "hw-trace-built-in"))
		hw_trace_built_in[hwid] = true;

	if(of_property_read_bool(dev->of_node, "hw-trace-vm"))
		hw_trace_vm = true;

	if (of_property_read_bool(dev->of_node, "cmdq-tfa-read-dbg"))
		cmdq_tfa_read_dbg = true;

	if (of_property_read_bool(dev->of_node, "event-debug")) {
		cmdq->event_debug = true;
		of_property_read_u32_array(dev->of_node, "event-dump-range",
			cmdq->event_dump_range, 2);
		of_property_read_u32_array(dev->of_node, "event-clr-range",
			cmdq->event_clr_range, 2);
		cmdq_msg("%s dump range: %d ~ %d", __func__,
			cmdq->event_dump_range[0], cmdq->event_dump_range[1]);
		cmdq_msg("%s clr range: %d ~ %d", __func__,
			cmdq->event_clr_range[0], cmdq->event_clr_range[1]);
	}

	of_property_read_u32(dev->of_node, "cmdq-dump-buf-size", &cmdq_dump_buf_size);
	of_property_read_u32(dev->of_node, "error-irq-bug-on", &error_irq_bug_on);
	of_property_read_u32(dev->of_node, "cmdq-pwr-log", &cmdq_pwr_log);
	of_property_read_u32(dev->of_node, "cmdq-proc-debug-off", &cmdq_proc_debug_off);
	of_property_read_u32(dev->of_node, "cmdq-print-debug", &cmdq_print_debug);

	cmdq_msg("dump_buf_size %d error irq %d tfa_read_dbg:%d proc_debug_off:%d print_debug:%d",
		cmdq_dump_buf_size,
		error_irq_bug_on,
		cmdq_tfa_read_dbg,
		cmdq_proc_debug_off,
		cmdq_print_debug);

	if (of_property_read_bool(dev->of_node, "gce-fast-mtcmos")) {
		cmdq->fast_mtcmos = true;
	}
	cmdq_proc_create();
	spin_lock_init(&cmdq->fast_mtcmos_lock);

	dev_notice(dev,
		"cmdq thread:%u shift:%u mminfra:0x%llx base:0x%lx pa:0x%lx\n",
		plat_data->thread_nr, plat_data->shift, plat_data->mminfra,
		(unsigned long)cmdq->base,
		(unsigned long)cmdq->base_pa);

	cmdq_config_prefetch(dev->of_node, cmdq);
	cmdq_config_default_token(dev, cmdq);
	cmdq_config_dma_mask(dev);
	cmdq_config_init_buf(mtk_smmu_get_shared_device(dev), cmdq);

	cmdq->clock = devm_clk_get(dev, "gce");
	if (IS_ERR(cmdq->clock)) {
		cmdq_err("failed to get gce clk");
		cmdq->clock = NULL;
	}

	cmdq->clock_timer = devm_clk_get(dev, "gce-timer");
	if (IS_ERR(cmdq->clock_timer)) {
		cmdq_err("failed to get gce timer clk");
		cmdq->clock_timer = NULL;
	}

	mutex_init(&cmdq->mbox_mutex);
	smi_cnt = of_count_phandle_with_args(dev->of_node, "mediatek,smi", NULL);
	for (i = 0; i < smi_cnt; i++) {
		node = of_parse_phandle(dev->of_node, "mediatek,smi", i);
		if (!node)
			cmdq_msg("failed to get mediatek,smi");

		smi = of_find_device_by_node(node);
		if (!smi)
			cmdq_msg("failed to find smi node");
		else {
			link = device_link_add(dev, &smi->dev,
				DL_FLAG_PM_RUNTIME | DL_FLAG_STATELESS);
			cmdq_msg("%s [CMDQ] device link to %s\n", __func__, dev_name(&smi->dev));
			if (!link)
				cmdq_msg("failed to create device link with smi");
		}
	}

	g_cmdq[hwid] = cmdq;
	cmdq->hwid = hwid++;
	cmdq->prebuilt_enable =
		of_property_read_bool(dev->of_node, "prebuilt-enable");
	cmdq->sw_ddr_urgent =
		of_property_read_bool(dev->of_node, "ddr-urgent");
	cmdq->gce_res_sw_mode =
		of_property_read_bool(dev->of_node, "gce-res-sw-mode");
	cmdq->sw_ddr_en = of_property_read_bool(dev->of_node, "sw-ddr-en");
	cmdq->mbox.dev = dev;
	cmdq->share_dev = mtk_smmu_get_shared_device(dev);
	cmdq_genpd_init(dev, cmdq);
	cmdq->mbox.chans = devm_kcalloc(dev, CMDQ_THR_MAX_COUNT,
					sizeof(*cmdq->mbox.chans), GFP_KERNEL);
	if (!cmdq->mbox.chans)
		return -ENOMEM;

	cmdq->mbox.num_chans = CMDQ_THR_MAX_COUNT;
	cmdq->mbox.ops = &cmdq_mbox_chan_ops;
	cmdq->mbox.of_xlate = cmdq_xlate;

	/* make use of TXDONE_BY_ACK */
	cmdq->mbox.txdone_irq = false;
	cmdq->mbox.txdone_poll = false;

	for (i = 0; i < ARRAY_SIZE(cmdq->thread); i++) {
		cmdq->thread[i].base = cmdq->base + CMDQ_THR_BASE +
				CMDQ_THR_SIZE * i;
		cmdq->thread[i].gce_pa = cmdq->base_pa;
		INIT_LIST_HEAD(&cmdq->thread[i].task_busy_list);
		timer_setup(&cmdq->thread[i].timeout,
			cmdq_thread_handle_timeout, 0);
		cmdq->thread[i].idx = i;
		cmdq->mbox.chans[i].con_priv = &cmdq->thread[i];
		cmdq->thread[i].usage_cb = NULL;
		mutex_init(&cmdq->thread[i].pkt_id_mutex);
		INIT_WORK(&cmdq->thread[i].timeout_work,
			cmdq_thread_handle_timeout_work);
	}


	err = mbox_controller_register(&cmdq->mbox);
	if (err < 0) {
		cmdq_err("failed to register mailbox:%d", err);
		return err;
	}
	dev_notice(dev, "register mailbox successfully\n");

	cmdq->buf_dump_wq = alloc_ordered_workqueue(
			"%s", WQ_MEM_RECLAIM | WQ_HIGHPRI,
			"cmdq_buf_dump");

	cmdq->timeout_wq = create_singlethread_workqueue(
		"cmdq_timeout_handler");

	platform_set_drvdata(pdev, cmdq);

	spin_lock_init(&cmdq->lock);
	spin_lock_init(&cmdq->event_lock);
	spin_lock_init(&cmdq->irq_idx_lock);

	cmdq_mmp_init();

	if (cmdq->gce_vm && hw_trace_vm) {
		cmdq->irq_vm6 = platform_get_irq(pdev, 1);
		if (!cmdq->irq_vm6) {
			cmdq_err("failed to get irq");
			return -EINVAL;
		}
		err = devm_request_irq(dev, cmdq->irq_vm6, cmdq_vm_irq_handler, IRQF_SHARED,
					"mtk_cmdq_vm6", cmdq);
		if (err < 0) {
			cmdq_err("failed to register ISR (%d)", err);
			return err;
		}
	}

#if IS_ENABLED(CONFIG_MTK_CMDQ_MBOX_EXT)
	cmdq_util_controller->track_ctrl(cmdq, cmdq->base_pa, false);
#endif
	cmdq->prebuilt_clt = cmdq_mbox_create(&pdev->dev, 0);
	cmdq->hw_trace_clt = cmdq_mbox_create(&pdev->dev, 1);
	if (cmdq->prebuilt_clt && cmdq->gce_vm && hw_trace_built_in[cmdq->hwid]) {
		struct cmdq_thread *thread = (struct cmdq_thread *)cmdq->prebuilt_clt->chan->con_priv;

		cmdq->hw_trace_clt = cmdq->prebuilt_clt;
		if (hw_trace_vm) {
			cmdq->thread[thread->idx].base += GCE_VM6_OFFSET;
			cmdq->thread[thread->idx].gce_pa += GCE_VM6_OFFSET;
		}
		cmdq_msg("%s hw_trace thrd:%d base_va:0x%lx 0x%lx", __func__, thread->idx,
			(unsigned long)cmdq->thread[thread->idx].base,
			(unsigned long)cmdq->thread[thread->idx].gce_pa);
	}
	if (cmdq->gce_vm && hw_trace_built_in[cmdq->hwid])
		of_property_read_u32(dev->of_node, "cmdq-dump-hw-trace", &cmdq_hw_trace);
#if IS_ENABLED(CONFIG_MTK_CMDQ_DEBUG)
	else
		of_property_read_u32(dev->of_node, "cmdq-dump-hw-trace", &cmdq_hw_trace);
	of_property_read_u32(dev->of_node, "tf-high-addr", &cmdq->tf_high_addr);
	cmdq_msg("%s hw_trace_built_in:%d cmdq_hw_trace_dump:%d tf_high_addr:%x", __func__,
		hw_trace_built_in[cmdq->hwid], cmdq_hw_trace, cmdq->tf_high_addr);
#endif

#if IS_ENABLED(CONFIG_DEVICE_MODULES_ARM_SMMU_V3)
	cmdq->smmu_v3_enabled = smmu_v3_enabled();
	cmdq_msg("%s smmu_v3_enable:%d", __func__, cmdq->smmu_v3_enabled);
#endif
	if (!of_parse_phandle_with_args(
		cmdq->share_dev->of_node, "iommus", "#iommu-cells", 0, &args))
		cmdq->sid = args.args[0];
	if (of_property_read_u32(dev->of_node, "axid", &cmdq->axid))
		cmdq->axid = 0;
	if (of_property_read_u32(dev->of_node, "smmu-tbu", &cmdq->tbu))
		cmdq->tbu = 0;
	cmdq_msg("%s sid:%x axid:%x tbu:%x", __func__, cmdq->sid, cmdq->axid, cmdq->tbu);

#if !IS_ENABLED(CONFIG_VIRTIO_CMDQ)
	if (!of_parse_phandle_with_args(
		dev->of_node, "iommus", "#iommu-cells", 0, &args)) {
		mtk_iommu_register_fault_callback(
			args.args[0], cmdq_iommu_fault_callback, cmdq, false);
	} else if (!of_property_read_u32(
		dev->of_node, "mtk,iommu-dma-axid", &port)) {
		mtk_iommu_register_fault_callback(
			port, cmdq_iommu_fault_callback, cmdq, false);
	}
#endif
	if (of_property_read_bool(dev->of_node, "error-irq-sw-req")) {
		cmdq->error_irq_sw_req = true;
		if (!of_property_read_u32(dev->of_node, "dram-pwr-base", &dram_pwr_pa)) {
			cmdq_msg("dram_pwr_base:%#x", dram_pwr_pa);
			cmdq->dram_pwr_base = ioremap(dram_pwr_pa, 0x1000);
		}
	}

	if(of_property_read_bool(dev->of_node, "ao-ctrl-by-mminfra"))
		cmdq->ao_ctrl_by_mminfra = true;

	if (of_property_read_bool(dev->of_node, "gce-ddr-sel-wla")) {
		cmdq->gce_ddr_sel_wla = true;
		if (!of_property_read_u32(dev->of_node, "mminfra-ao-base", &mminfra_ao_pa)) {
			cmdq_msg("mminfra-ao-base:%#x", mminfra_ao_pa);
			cmdq->mminfra_ao_base = ioremap(mminfra_ao_pa, 0x1000);
		}
	}

	cmdq->gce_req_wa = of_property_read_bool(dev->of_node, "gce-req-wa");

	if (cmdq->hwid == 0 && cmdq_print_debug)
		cmdq_util_reserved_memory_lookup(dev);

	return 0;
}

static const struct dev_pm_ops cmdq_pm_ops = {
	.suspend = cmdq_suspend,
	.resume = cmdq_resume,
};

static const struct gce_plat gce_plat_v2 = {.thread_nr = 16};
static const struct gce_plat gce_plat_v4 = {.thread_nr = 24, .shift = 3};
static const struct gce_plat gce_plat_v5 = {
	.thread_nr = 32, .shift = 3, .mminfra = BIT(30)};
static const struct gce_plat gce_plat_v5_1 = {
	.thread_nr = 32, .shift = 3};
static const struct gce_plat gce_plat_v6 = {
	.thread_nr = 32, .shift = 3, .mminfra = BIT(31)};

static const struct of_device_id cmdq_of_ids[] = {
	{.compatible = "mediatek,mt8173-gce", .data = (void *)&gce_plat_v2},
	{.compatible = "mediatek,mt8168-gce", .data = (void *)&gce_plat_v2},
	{.compatible = "mediatek,mt6739-gce", .data = (void *)&gce_plat_v2},
	{.compatible = "mediatek,mt6761-gce", .data = (void *)&gce_plat_v2},
	{.compatible = "mediatek,mt6765-gce", .data = (void *)&gce_plat_v2},
	{.compatible = "mediatek,mt6768-gce", .data = (void *)&gce_plat_v2},
	{.compatible = "mediatek,mt6771-gce", .data = (void *)&gce_plat_v2},
	{.compatible = "mediatek,mt6779-gce", .data = (void *)&gce_plat_v4},
	{.compatible = "mediatek,mt6785-gce", .data = (void *)&gce_plat_v4},
	{.compatible = "mediatek,mt6833-gce", .data = (void *)&gce_plat_v4},
	{.compatible = "mediatek,mt6853-gce", .data = (void *)&gce_plat_v4},
	{.compatible = "mediatek,mt6781-gce", .data = (void *)&gce_plat_v4},
	{.compatible = "mediatek,mt6873-gce", .data = (void *)&gce_plat_v4},
	{.compatible = "mediatek,mt6877-gce", .data = (void *)&gce_plat_v4},
	{.compatible = "mediatek,mt6879-gce", .data = (void *)&gce_plat_v5},
	{.compatible = "mediatek,mt6885-gce", .data = (void *)&gce_plat_v4},
	{.compatible = "mediatek,mt6886-gce", .data = (void *)&gce_plat_v5_1},
	{.compatible = "mediatek,mt6893-gce", .data = (void *)&gce_plat_v4},
	{.compatible = "mediatek,mt6895-gce", .data = (void *)&gce_plat_v5},
	{.compatible = "mediatek,mt6983-gce", .data = (void *)&gce_plat_v5},
	{.compatible = "mediatek,mt6855-gce", .data = (void *)&gce_plat_v5},
	{.compatible = "mediatek,mt6789-gce", .data = (void *)&gce_plat_v4},
	{.compatible = "mediatek,mt6985-gce", .data = (void *)&gce_plat_v5},
	{.compatible = "mediatek,mt6897-gce", .data = (void *)&gce_plat_v5},
	{.compatible = "mediatek,mt6989-gce", .data = (void *)&gce_plat_v5},
	{.compatible = "mediatek,mt6878-gce", .data = (void *)&gce_plat_v5},
	{.compatible = "mediatek,mt6991-gce", .data = (void *)&gce_plat_v6},
	{.compatible = "mediatek,mt6899-gce", .data = (void *)&gce_plat_v5},
	{}
};

static struct platform_driver cmdq_drv = {
	.probe = cmdq_probe,
	.remove = cmdq_remove,
	.shutdown = cmdq_shutdown,
	.driver = {
		.name = CMDQ_DRIVER_NAME,
		.pm = &cmdq_pm_ops,
		.of_match_table = cmdq_of_ids,
	}
};

static __init int cmdq_drv_init(void)
{
	u32 err = 0;

	cmdq_msg("%s enter", __func__);

	cmdq_util_init();
	err = platform_driver_register(&cmdq_drv);
	if (err) {
		cmdq_err("platform driver register failed:%d", err);
		return err;
	}
	cmdq_helper_init();

	return 0;
}

void cmdq_mbox_enable(void *chan)
{
#if IS_ENABLED(CONFIG_VIRTIO_CMDQ)
	virtio_cmdq_mbox_enable(chan);
#else
	struct cmdq *cmdq = container_of(((struct mbox_chan *)chan)->mbox,
		typeof(*cmdq), mbox);
	struct cmdq_thread *thread;
	s32 usage, i, ret, thd_usage;

	mutex_lock(&cmdq->mbox_mutex);

	usage = atomic_read(&cmdq->usage);
	if (cmdq->suspended) {
		cmdq_err("hwid:%u usage:%d suspended:%d not enable",
			cmdq->hwid, usage, cmdq->suspended);
		/*cmdq_util_aee("CMDQ", "hwid:%u usage:%d suspended:%d not enable",
			cmdq->hwid, usage, cmdq->suspended);*/
		WARN_ON(1);
		mutex_unlock(&cmdq->mbox_mutex);
		return;
	}

	for (i = 0; i < ARRAY_SIZE(cmdq->thread); i++)
		if (cmdq->thread[i].chan == chan)
			break;

	if (i == ARRAY_SIZE(cmdq->thread)) {
		cmdq_err("hwid:%u usage:%d idx:%d wrong chan:%p",
			cmdq->hwid, usage, i, chan);
		/*cmdq_util_aee("CMDQ", "hwid:%u usage:%d idx:%d wrong chan:%p",
			cmdq->hwid, usage, i, chan);*/
		WARN_ON(1);
		mutex_unlock(&cmdq->mbox_mutex);
		return;
	}
	cmdq_log("%s: hwid:%hu usage:%d idx:%d usage:%d", __func__,
		cmdq->hwid, usage, i, atomic_read(&cmdq->thread[i].usage));

	thd_usage = atomic_inc_return(&cmdq->thread[i].usage);

	//skip_fast_mtcmos
	if (thd_usage == 1 && cmdq->fast_mtcmos && cmdq->thread[i].skip_fast_mtcmos) {
		int ret;

		ret = pm_runtime_get_sync(cmdq->pd_mminfra_1);
		if (ret < 0)
			cmdq_err("pm_runtime_get_sync err:%d", ret);
		if (mminfra_power_cb && !mminfra_power_cb())
			cmdq_err("hwid:%hu usage:%d mminfra power not enable",
				cmdq->hwid, thd_usage);
	}

	usage = atomic_inc_return(&cmdq->usage);
	if (usage == 1) {
		unsigned long flags;
		cmdq_log_level(CMDQ_PWR_CHECK, "%s: hwid:%hu usage:%d idx:%d usage:%d",
			__func__, cmdq->hwid, usage, i,
			atomic_read(&cmdq->thread[i].usage));

		if (cmdq->fast_mtcmos)
			cmdq_mtcmos_mminfra_ao(cmdq, true);

		//power
		if (!cmdq->fast_mtcmos) {
			int ret;

			ret = pm_runtime_get_sync(cmdq->pd_mminfra_1);
			if (ret < 0)
				cmdq_err("pm_runtime_get_sync err:%d", ret);
			if (mminfra_power_cb && !mminfra_power_cb())
				cmdq_err("hwid:%hu usage:%d mminfra power not enable",
					cmdq->hwid, usage);
		}

		cmdq_mtcmos_by_fast(cmdq, true);

		// clock
		if (cmdq->clock) {
			ret = clk_prepare_enable(cmdq->clock);
			if (ret)
				cmdq_err("hwid:%hu usage:%d clock cannot enable:%d",
					cmdq->hwid, usage, ret);
		}

		if (cmdq->clock_timer) {
			ret = clk_prepare_enable(cmdq->clock_timer);
			if (ret)
				cmdq_err(
					"hwid:%hu usage:%d clock_timer cannot enable:%d",
					cmdq->hwid, usage, ret);
		}

		if (mminfra_gce_cg && !mminfra_gce_cg(cmdq->hwid))
			cmdq_err("hwid:%hu usage:%d gce clock not enable",
				cmdq->hwid, usage);

		if (cmdq->mminfra_ao_base && cmdq->gce_ddr_sel_wla)
			writel(readl(cmdq->mminfra_ao_base + 0x418) | DDR_SEL_WLA,
				cmdq->mminfra_ao_base + 0x418);

		if (hw_trace_built_in[cmdq->hwid] && cmdq->gce_vm) {
			writel(readl(cmdq->base + GCE_CPR_GSIZE) | 0x8000000F,
				cmdq->base + GCE_CPR_GSIZE);
		} else if (hw_trace_built_in[cmdq->hwid]) {
			writel(readl(cmdq->base + GCE_DBG_CTL) | CMDQ_HW_TRACE_EN,
				cmdq->base + GCE_DBG_CTL);
		} else if (cmdq->gce_vm) {
			writel(readl(cmdq->base + GCE_CPR_GSIZE) | 0xf,
				cmdq->base + GCE_CPR_GSIZE);
		}

		if (cmdq->gce_vm) {
			//config VMID
			writel(0x3fffffff, cmdq->base + GCE_VM_ID_MAP0);
			if (hw_trace_vm)
				writel(0x3ffffdff, cmdq->base + GCE_VM_ID_MAP1); //thread 13 map to vm6
			else
				writel(0x3fffffff, cmdq->base + GCE_VM_ID_MAP1);
			writel(0x3fffffff, cmdq->base + GCE_VM_ID_MAP2);
			writel(0x3f, cmdq->base + GCE_VM_ID_MAP3);
		}
		cmdq_util_set_domain(cmdq->hwid, 13);

		// core
		spin_lock_irqsave(&cmdq->lock, flags);
		writel(readl(cmdq->base + GCE_BUS_GCTL) | CMDQ_ULTRA_EN,
			cmdq->base + GCE_BUS_GCTL);
		if (cmdq->sw_ddr_urgent)
			writel(readl(cmdq->base + GCE_GCTL_VALUE) | CMDQ_DDR_URGENT,
				cmdq->base + GCE_GCTL_VALUE);
		if(cmdq->gce_res_sw_mode)
			writel(readl(cmdq->base + GCE_GCTL_VALUE) | ((0x7 << 16) + 0x7),
				cmdq->base + GCE_GCTL_VALUE);
		if (cmdq->sw_ddr_en) {
			writel((0x7 << 16) + 0x7, cmdq->base + GCE_GCTL_VALUE);
			writel(0, cmdq->base + GCE_DEBUG_START_ADDR);
		}
		if (cmdq->outpin_en)
			writel(0x3, cmdq->base + GCE_OUTPIN_EVENT);
		if (cmdq->prefetch)
			writel(cmdq->prefetch,
				cmdq->base + CMDQ_PREFETCH_GSIZE);
		writel(CMDQ_TPR_EN, cmdq->base + CMDQ_TPR_MASK);
		spin_unlock_irqrestore(&cmdq->lock, flags);

		// thread
		if (cmdq->prebuilt_enable) {
			cmdq_init_cpu(cmdq);
			cmdq_util_prebuilt_enable(cmdq->hwid);
		} else
			cmdq_init(cmdq);

		if (cmdq_hw_trace)
			cmdq_util_hw_trace_enable(cmdq->hwid,
				cmdq_util_get_bit_feature() &
				CMDQ_LOG_FEAT_PERF);

		if (cmdq->gce_req_wa) {
			cmdq_mbox_set_resource_req(GCED_HWID, true, true, GCE_DDREN_BIT);
			cmdq_mbox_set_resource_req(GCEM_HWID, true, true, GCE_DDREN_BIT);
			cmdq_mbox_set_resource_req(GCEM_HWID, true, true, GCE_DDRSRC_BIT);
			cmdq_mbox_set_resource_req(GCEM_HWID, true, true, GCE_EMI_BIT);
		}

		cmdq_mtcmos_by_fast(cmdq, false);
	}

	if (thd_usage == 1) {
		thread = &cmdq->thread[i];
		thread->mbox_en = sched_clock();
	}
	mutex_unlock(&cmdq->mbox_mutex);
#endif
}
EXPORT_SYMBOL(cmdq_mbox_enable);

void cmdq_mbox_disable(void *chan)
{
	struct cmdq *cmdq = container_of(((struct mbox_chan *)chan)->mbox,
		typeof(*cmdq), mbox);
	struct cmdq_thread *thread;
	s32 usage, i, thd_usage;

#if IS_ENABLED(CONFIG_VIRTIO_CMDQ)
	return;
#endif

	mutex_lock(&cmdq->mbox_mutex);

	usage = atomic_read(&cmdq->usage);
	if (cmdq->suspended) {
		cmdq_err("hwid:%u usage:%d suspended:%d not enable",
			cmdq->hwid, usage, cmdq->suspended);
		/*cmdq_util_aee("CMDQ", "hwid:%u usage:%d suspended:%d not enable",
			cmdq->hwid, usage, cmdq->suspended);*/
		WARN_ON(1);
		mutex_unlock(&cmdq->mbox_mutex);
		return;
	}

	for (i = 0; i < ARRAY_SIZE(cmdq->thread); i++)
		if (cmdq->thread[i].chan == chan)
			break;
	thread = &cmdq->thread[i];

	if (i == ARRAY_SIZE(cmdq->thread)) {
		cmdq_err("hwid:%u usage:%d idx:%d wrong chan:%p",
			cmdq->hwid, usage, i, chan);
		/*cmdq_util_aee("CMDQ", "hwid:%u usage:%d idx:%d wrong chan:%p",
			cmdq->hwid, usage, i, chan);*/
		WARN_ON(1);
		mutex_unlock(&cmdq->mbox_mutex);
		return;
	}
	cmdq_log("%s: hwid:%u usage:%d idx:%d usage:%d", __func__,
		cmdq->hwid, usage, i, atomic_read(&cmdq->thread[i].usage));

	thd_usage = atomic_dec_return(&cmdq->thread[i].usage);
	if (!thd_usage && !list_empty(&cmdq->thread[i].task_busy_list)) {
		cmdq_err("hwid:%u idx:%d usage:%d still has tasks",
			cmdq->hwid, i, thd_usage);
		dump_stack();
	} else if (thd_usage == 0) {
		thread = &cmdq->thread[i];
		thread->mbox_dis = sched_clock();
	} else if (thd_usage < 0) {
		cmdq_err("hwid:%u idx:%d usage:%d cannot below zero",
			cmdq->hwid, i, thd_usage);
		WARN_ON(1);
	}

	usage = atomic_read(&cmdq->usage);
	if (cmdq_hw_trace && usage == 1)
		cmdq_util_hw_trace_disable(cmdq->hwid);

	if (usage <= 0) {
		cmdq_err("hwid:%u usage:%d cannot below zero",
			cmdq->hwid, usage);
		/*cmdq_util_aee("CMDQ", "hwid:%u usage:%d cannot below zero",
			cmdq->hwid, usage);*/
		WARN_ON(1);
	} else if (usage == 1) {
		unsigned long flags;

		cmdq_log_level(CMDQ_PWR_CHECK, "%s: hwid:%hu usage:%d idx:%d usage:%d",
			__func__, cmdq->hwid, usage, i,
			atomic_read(&cmdq->thread[i].usage));

		cmdq_mtcmos_by_fast(cmdq, true);

		// thread
		if (cmdq->prebuilt_enable)
			cmdq_util_prebuilt_disable(cmdq->hwid);

		for (i = 0; i < ARRAY_SIZE(cmdq->thread); i++)
			if (!list_empty(&cmdq->thread[i].task_busy_list))
				cmdq_err(
					"hwid:%hu usage:%d idx:%d still has tasks",
					cmdq->hwid, usage, i);

		// core
		spin_lock_irqsave(&cmdq->lock, flags);
		writel(0, cmdq->base + CMDQ_TPR_MASK);
		if (cmdq->sw_ddr_en || cmdq->gce_res_sw_mode)
			writel(0x7, cmdq->base + GCE_GCTL_VALUE);
		spin_unlock_irqrestore(&cmdq->lock, flags);

		// clock : no need to check
		clk_disable_unprepare(cmdq->clock_timer);
		clk_disable_unprepare(cmdq->clock);

		if (cmdq->gce_req_wa) {
			cmdq_mbox_set_resource_req(GCEM_HWID, true, false, GCE_DDREN_BIT);
			cmdq_mbox_set_resource_req(GCEM_HWID, true, false, GCE_DDRSRC_BIT);
			cmdq_mbox_set_resource_req(GCEM_HWID, true, false, GCE_EMI_BIT);
			cmdq_mbox_set_resource_req(GCED_HWID, false, false, GCE_DDREN_BIT);
		}
		cmdq_mtcmos_by_fast(cmdq, false);

		// power
		if (!cmdq->fast_mtcmos) {
			int ret;

			if (mminfra_power_cb && !mminfra_power_cb())
				cmdq_err("hwid:%hu usage:%d mminfra power not enable",
					cmdq->hwid, usage);
			ret = pm_runtime_put_sync(cmdq->pd_mminfra_1);
			if (ret != 0)
				cmdq_err("pm_runtime_put_sync err:%d", ret);
		}



		if (cmdq->fast_mtcmos)
			cmdq_mtcmos_mminfra_ao(cmdq, false);
	}

	//skip_fast_mtcmos
	if ((thd_usage == 0)
		&& (cmdq->fast_mtcmos) && (thread->skip_fast_mtcmos)) {
		int ret;

		if (mminfra_power_cb && !mminfra_power_cb())
			cmdq_err("hwid:%hu thd_usage:%d mminfra power not enable",
				cmdq->hwid, thd_usage);
		ret = pm_runtime_put_sync(cmdq->pd_mminfra_1);
		if (ret != 0)
			cmdq_err("pm_runtime_put_sync err:%d", ret);
	}
	atomic_dec(&cmdq->usage);
	mutex_unlock(&cmdq->mbox_mutex);
}
EXPORT_SYMBOL(cmdq_mbox_disable);

s32 cmdq_mbox_get_usage(void *chan)
{
	struct cmdq *cmdq = container_of(((struct mbox_chan *)chan)->mbox,
		typeof(*cmdq), mbox);

	return atomic_read(&cmdq->usage);
}
EXPORT_SYMBOL(cmdq_mbox_get_usage);

void *cmdq_mbox_get_base(void *chan)
{
	struct cmdq *cmdq = container_of(((struct mbox_chan *)chan)->mbox,
		typeof(*cmdq), mbox);

	return (void *)cmdq->base;
}
EXPORT_SYMBOL(cmdq_mbox_get_base);

phys_addr_t cmdq_mbox_get_base_pa(void *chan)
{
	struct cmdq *cmdq = container_of(((struct mbox_chan *)chan)->mbox,
		typeof(*cmdq), mbox);

	return cmdq->base_pa;
}
EXPORT_SYMBOL(cmdq_mbox_get_base_pa);

phys_addr_t cmdq_dev_get_base_pa(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct resource *res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	if (!res) {
		cmdq_err("failed to get resource from dev:%p", dev);
		return -EINVAL;
	}
	cmdq_log("%s: res:%p start:%pa", __func__, res, &res->start);

	return res->start;
}
EXPORT_SYMBOL(cmdq_dev_get_base_pa);

phys_addr_t cmdq_mbox_get_dummy_reg(void *chan)
{
	struct cmdq *cmdq = container_of(((struct mbox_chan *)chan)->mbox,
		typeof(*cmdq), mbox);

	return cmdq->thread[22].gce_pa
		+ CMDQ_THR_BASE + CMDQ_THR_SIZE * 22
		+ CMDQ_THR_SPR + 4*CMDQ_THR_SPR_IDX3;
}
EXPORT_SYMBOL(cmdq_mbox_get_dummy_reg);

phys_addr_t cmdq_mbox_get_spr_pa(void *chan, u8 spr)
{
	struct cmdq_thread *thread =
		(struct cmdq_thread *)((struct mbox_chan *)chan)->con_priv;

	return thread->gce_pa + CMDQ_THR_BASE + CMDQ_THR_SIZE * ((u64)thread->idx) +
		CMDQ_THR_SPR + 4 * spr;
}
EXPORT_SYMBOL(cmdq_mbox_get_spr_pa);

struct device *cmdq_mbox_get_dev(void *chan)
{
	struct cmdq *cmdq = container_of(((struct mbox_chan *)chan)->mbox,
		typeof(*cmdq), mbox);

	return cmdq->mbox.dev;
}
EXPORT_SYMBOL(cmdq_mbox_get_dev);

s32 cmdq_mbox_set_hw_id(void *cmdq_mbox)
{
	struct cmdq *cmdq = cmdq_mbox;
	s32 ret;

	if (!cmdq)
		return -EINVAL;
	cmdq->hwid = (u8)cmdq_util_get_hw_id(cmdq->base_pa);
	cmdq_util_prebuilt_set_client(cmdq->hwid, cmdq->prebuilt_clt);
	ret = cmdq_util_hw_trace_set_client(cmdq->hwid, cmdq->hw_trace_clt);
	if (ret)
		return ret;
	return 0;
}

s32 cmdq_mbox_reset_hw_id(void *cmdq_mbox)
{
	struct cmdq *cmdq = cmdq_mbox;

	if (!cmdq)
		return -EINVAL;
	cmdq_util_prebuilt_set_client(cmdq->hwid, NULL);
	cmdq->hwid = 0;
	return 0;
}

s32 cmdq_mbox_thread_reset(void *chan)
{
	struct cmdq_thread *thread = ((struct mbox_chan *)chan)->con_priv;
	struct cmdq *cmdq = container_of(thread->chan->mbox, struct cmdq,
		mbox);

	return cmdq_thread_reset(cmdq, thread);
}
EXPORT_SYMBOL(cmdq_mbox_thread_reset);

s32 cmdq_mbox_thread_suspend(void *chan)
{
	struct cmdq_thread *thread = ((struct mbox_chan *)chan)->con_priv;
	struct cmdq *cmdq = container_of(thread->chan->mbox, struct cmdq,
		mbox);
	int ret;

	cmdq_mtcmos_by_fast(cmdq, true);
	ret = cmdq_thread_suspend(cmdq, thread);
	cmdq_mtcmos_by_fast(cmdq, false);

	return ret;
}
EXPORT_SYMBOL(cmdq_mbox_thread_suspend);

void cmdq_mbox_thread_disable(void *chan)
{
	struct cmdq_thread *thread = ((struct mbox_chan *)chan)->con_priv;
	struct cmdq *cmdq = container_of(thread->chan->mbox, struct cmdq,
		mbox);

	cmdq_thread_disable(cmdq, thread);
}
EXPORT_SYMBOL(cmdq_mbox_thread_disable);

u32 cmdq_mbox_get_thread_timeout(void *chan)
{
	struct cmdq_thread *thread = ((struct mbox_chan *)chan)->con_priv;

	return thread->timeout_ms;
}
EXPORT_SYMBOL(cmdq_mbox_get_thread_timeout);

u32 cmdq_mbox_set_thread_timeout(void *chan, u32 timeout)
{
	struct cmdq_thread *thread = ((struct mbox_chan *)chan)->con_priv;
	unsigned long flags;
	u32 timeout_prv;

	spin_lock_irqsave(&thread->chan->lock, flags);
	timeout_prv = thread->timeout_ms;
	thread->timeout_ms = timeout;
	spin_unlock_irqrestore(&thread->chan->lock, flags);

	return timeout_prv;
}
EXPORT_SYMBOL(cmdq_mbox_set_thread_timeout);

s32 cmdq_mbox_chan_id(void *chan)
{
	struct cmdq_thread *thread = ((struct mbox_chan *)chan)->con_priv;

	if (!thread || !thread->occupied)
		return -1;

	return thread->idx;
}
EXPORT_SYMBOL(cmdq_mbox_chan_id);

void cmdq_mbox_check_buffer(struct mbox_chan *chan,
	struct cmdq_pkt_buffer *buffer)
{
	struct cmdq *cmdq = container_of(chan->mbox, typeof(*cmdq), mbox);
	bool err = false;
	s32 i;

	for (i = 0; i < ARRAY_SIZE(cmdq->thread) && !err; i++) {
		struct cmdq_thread *thread = &cmdq->thread[i];
		struct cmdq_task *task;
		struct cmdq_pkt_buffer *buf;
		unsigned long flags;

		if (!thread->occupied && !thread->chan)
			continue;

		spin_lock_irqsave(&thread->chan->lock, flags);
		list_for_each_entry(task, &thread->task_busy_list, list_entry) {
			list_for_each_entry(buf, &task->pkt->buf, list_entry) {
				if (CMDQ_BUF_ADDR(buffer) ==
					CMDQ_BUF_ADDR(buf)) {
					cmdq_util_err(
						"hwid:%hu thread:%u cur:%p va:%p iova:%pa pa:%pa alloc_time:%llu",
						cmdq->hwid, thread->idx, buffer,
						buffer->va_base,
						&buffer->iova_base,
						&buffer->pa_base,
						buffer->alloc_time);
					cmdq_util_err(
						"hwid:%hu thread:%u buf:%p va:%p iova:%pa pa:%pa alloc_time:%llu allocated",
						cmdq->hwid, thread->idx, buf,
						buf->va_base, &buf->iova_base,
						&buf->pa_base, buf->alloc_time);
					err = true;
					break;
				}
			}
			if (err)
				break;
		}
		spin_unlock_irqrestore(&thread->chan->lock, flags);
	}

}
EXPORT_SYMBOL(cmdq_mbox_check_buffer);

s32 cmdq_task_get_thread_pc(struct mbox_chan *chan, dma_addr_t *pc_out)
{
	struct cmdq *cmdq;
	struct cmdq_thread *thread;
	dma_addr_t pc = 0;

	if (!pc_out || !chan)
		return -EINVAL;

	cmdq = container_of(chan->mbox, typeof(*cmdq), mbox);
	thread = chan->con_priv;
	cmdq_mtcmos_by_fast(cmdq, true);
	pc = cmdq_thread_get_pc(thread);
	cmdq_mtcmos_by_fast(cmdq, false);
	*pc_out = pc;

	return 0;
}
EXPORT_SYMBOL(cmdq_task_get_thread_pc);

s32 cmdq_task_get_thread_irq(struct mbox_chan *chan, u32 *irq_out)
{
	struct cmdq *cmdq;
	struct cmdq_thread *thread;

	if (!irq_out || !chan)
		return -EINVAL;

	cmdq = container_of(chan->mbox, typeof(*cmdq), mbox);
	thread = chan->con_priv;
	cmdq_mtcmos_by_fast(cmdq, true);
	*irq_out = readl(thread->base + CMDQ_THR_IRQ_STATUS);
	cmdq_mtcmos_by_fast(cmdq, false);

	return 0;
}
EXPORT_SYMBOL(cmdq_task_get_thread_irq);

s32 cmdq_task_get_thread_irq_en(struct mbox_chan *chan, u32 *irq_en_out)
{
	struct cmdq *cmdq;
	struct cmdq_thread *thread;

	if (!irq_en_out || !chan)
		return -EINVAL;

	cmdq = container_of(chan->mbox, typeof(*cmdq), mbox);
	thread = chan->con_priv;
	cmdq_mtcmos_by_fast(cmdq, true);
	*irq_en_out = readl(thread->base + CMDQ_THR_IRQ_ENABLE);
	cmdq_mtcmos_by_fast(cmdq, false);

	return 0;
}

s32 cmdq_task_get_thread_end_addr(struct mbox_chan *chan,
	dma_addr_t *end_addr_out)
{
	struct cmdq *cmdq;
	struct cmdq_thread *thread;

	if (!end_addr_out || !chan)
		return -EINVAL;

	cmdq = container_of(chan->mbox, typeof(*cmdq), mbox);
	thread = chan->con_priv;
	cmdq_mtcmos_by_fast(cmdq, true);
	*end_addr_out = cmdq_thread_get_end(thread);
	cmdq_mtcmos_by_fast(cmdq, false);

	return 0;
}

s32 cmdq_task_get_task_info_from_thread_unlock(struct mbox_chan *chan,
	struct list_head *task_list_out, u32 *task_num_out)
{
	struct cmdq_thread *thread;
	struct cmdq_task *task;
	u32 task_num = 0;

	if (!chan || !task_list_out)
		return -EINVAL;

	thread = chan->con_priv;
	list_for_each_entry(task, &thread->task_busy_list, list_entry) {
		struct cmdq_thread_task_info *task_info;

		task_info = kzalloc(sizeof(*task_info), GFP_ATOMIC);
		if (!task_info)
			continue;

		task_info->pa_base = task->pa_base;

		/* copy pkt here to avoid released */
		task_info->pkt = kzalloc(sizeof(*task_info->pkt), GFP_ATOMIC);
		if (!task_info->pkt) {
			kfree(task_info);
			continue;
		}
		memcpy(task_info->pkt, task->pkt, sizeof(*task->pkt));

		INIT_LIST_HEAD(&task_info->list_entry);
		list_add_tail(&task_info->list_entry, task_list_out);
		task_num++;
	}

	if (task_num_out)
		*task_num_out = task_num;

	return 0;
}

s32 cmdq_task_get_pkt_from_thread(struct mbox_chan *chan,
	struct cmdq_pkt **pkt_list_out, u32 pkt_list_size, u32 *pkt_count_out)
{
	struct cmdq_thread *thread;
	struct cmdq_task *task;
	u32 pkt_num = 0;
	unsigned long flags;

	if (!chan || !pkt_list_out || !pkt_count_out) {
		if (pkt_count_out)
			*pkt_count_out = pkt_num;

		return -EINVAL;
	}

	thread = chan->con_priv;

	spin_lock_irqsave(&thread->chan->lock, flags);

	if (list_empty(&thread->task_busy_list)) {
		*pkt_count_out = pkt_num;
		spin_unlock_irqrestore(&thread->chan->lock, flags);
		return 0;
	}

	list_for_each_entry(task, &thread->task_busy_list, list_entry) {
		if (pkt_list_size == pkt_num)
			break;
		pkt_list_out[pkt_num] = task->pkt;
		pkt_num++;
	}

	spin_unlock_irqrestore(&thread->chan->lock, flags);

	*pkt_count_out = pkt_num;

	return 0;
}
EXPORT_SYMBOL(cmdq_task_get_pkt_from_thread);

void cmdq_set_event(void *chan, u16 event_id)
{
	struct cmdq *cmdq = container_of(((struct mbox_chan *)chan)->mbox,
		typeof(*cmdq), mbox);
	unsigned long flags;

	cmdq_mtcmos_by_fast(cmdq, true);
	spin_lock_irqsave(&cmdq->event_lock, flags);
	writel((1L << 16) | event_id, cmdq->base + CMDQ_SYNC_TOKEN_UPD);
	spin_unlock_irqrestore(&cmdq->event_lock, flags);
	cmdq_mtcmos_by_fast(cmdq, false);
}
EXPORT_SYMBOL(cmdq_set_event);

void cmdq_clear_event(void *chan, u16 event_id)
{
	struct cmdq *cmdq = container_of(((struct mbox_chan *)chan)->mbox,
		typeof(*cmdq), mbox);
	unsigned long flags;

	cmdq_mtcmos_by_fast(cmdq, true);
	spin_lock_irqsave(&cmdq->event_lock, flags);
	writel(event_id, cmdq->base + CMDQ_SYNC_TOKEN_UPD);
	spin_unlock_irqrestore(&cmdq->event_lock, flags);
	cmdq_mtcmos_by_fast(cmdq, false);
}
EXPORT_SYMBOL(cmdq_clear_event);

u32 cmdq_get_event(void *chan, u16 event_id)
{
	struct cmdq *cmdq = container_of(((struct mbox_chan *)chan)->mbox,
		typeof(*cmdq), mbox);
	u32 event_val = 0;
	unsigned long flags;

	cmdq_mtcmos_by_fast(cmdq, true);
	spin_lock_irqsave(&cmdq->event_lock, flags);
	writel(0x3FF & event_id, cmdq->base + CMDQ_SYNC_TOKEN_ID);
	event_val = readl(cmdq->base + CMDQ_SYNC_TOKEN_VAL);
	spin_unlock_irqrestore(&cmdq->event_lock, flags);
	cmdq_mtcmos_by_fast(cmdq, false);
	return event_val;
}
EXPORT_SYMBOL(cmdq_get_event);

void cmdq_dump_event(void *chan)
{
	u32 i;

	for (i = 0; i < CMDQ_EVENT_MAX; i++)
		if (cmdq_get_event(chan, i))
			cmdq_msg("%s event %u is set", __func__, i);
}
EXPORT_SYMBOL(cmdq_dump_event);

void cmdq_event_verify(void *chan, u16 event_id)
{
	struct cmdq *cmdq = container_of(((struct mbox_chan *)chan)->mbox,
		typeof(*cmdq), mbox);
	/* should be CMDQ_SYNC_TOKEN_USER_0 */
	const u16 test_token = 649;
	u32 i;

	cmdq_msg("chan:%lx cmdq:%lx event:%u",
		(unsigned long)chan, (unsigned long)cmdq, event_id);

	if (event_id > 512)
		event_id = 512;

	cmdq_mtcmos_by_fast(cmdq, true);
	/* check if this event can be set and clear */
	writel((1L << 16) | event_id, cmdq->base + CMDQ_SYNC_TOKEN_UPD);
	writel(event_id, cmdq->base + CMDQ_SYNC_TOKEN_UPD);
	if (!readl(cmdq->base + CMDQ_SYNC_TOKEN_VAL))
		cmdq_msg("event cannot be set:%u", event_id);

	writel(event_id, cmdq->base + CMDQ_SYNC_TOKEN_UPD);
	if (readl(cmdq->base + CMDQ_SYNC_TOKEN_VAL))
		cmdq_msg("event cannot be clear:%u", event_id);

	/* check if sw token can be set and clear */
	writel((1L << 16) | test_token, cmdq->base + CMDQ_SYNC_TOKEN_UPD);
	writel(test_token, cmdq->base + CMDQ_SYNC_TOKEN_UPD);
	if (!readl(cmdq->base + CMDQ_SYNC_TOKEN_VAL))
		cmdq_msg("event cannot be set:%u", test_token);

	writel(test_token, cmdq->base + CMDQ_SYNC_TOKEN_UPD);
	if (readl(cmdq->base + CMDQ_SYNC_TOKEN_VAL))
		cmdq_msg("event cannot be clear:%u", test_token);

	/* clear all event first */
	for (i = 0; i < event_id + 20; i++)
		writel(i, cmdq->base + CMDQ_SYNC_TOKEN_UPD);

	/* now see if any event unable to clear */
	for (i = 0; i < event_id + 20; i++) {
		writel(i, cmdq->base + CMDQ_SYNC_TOKEN_UPD);
		if (readl(cmdq->base + CMDQ_SYNC_TOKEN_VAL))
			cmdq_msg("event still on:%u", i);
	}
	cmdq_mtcmos_by_fast(cmdq, false);

	cmdq_msg("end debug event for %u", event_id);
}
EXPORT_SYMBOL(cmdq_event_verify);

s32 cmdq_pkt_hw_trace(struct cmdq_pkt *pkt, const u16 event_id)
{
	struct cmdq_thread *thread;
	struct cmdq_operand lop, rop;
	struct cmdq *cmdq;

	if (!pkt->cl) {
		cmdq_log("%s: pkt:%p without client", __func__, pkt);
		return -EINVAL;
	}

	thread = (struct cmdq_thread *)
		((struct cmdq_client *)pkt->cl)->chan->con_priv;
	cmdq = dev_get_drvdata(thread->chan->mbox->dev);
	if (!cmdq_hw_trace || hw_trace_built_in[cmdq->hwid])
		return 0;

	if (cmdq_util_is_secure_client(pkt->cl))
		return 0;

	cmdq_log("%s: pkt:%p idx:%hu", __func__, pkt, thread->idx);

	// spr = (CMDQ_TPR_ID >> 14) | (idx << 24)
	cmdq_pkt_assign_command(pkt, CMDQ_SPR_FOR_TEMP,
		thread->idx << 27 | (event_id & GENMASK(8, 0)) << 18);
	pkt->write_addr_high = 0;

	lop.reg = true;
	lop.idx = CMDQ_TPR_ID;
	rop.reg = false;
	rop.value = 14;
	cmdq_pkt_logic_command(
		pkt, CMDQ_LOGIC_RIGHT_SHIFT, CMDQ_THR_SPR_IDX2, &lop, &rop);

	cmdq_pkt_wfe(pkt, CMDQ_TOKEN_HW_TRACE_LOCK);

	lop.reg = true;
	lop.idx = CMDQ_THR_SPR_IDX2;
	rop.reg = true;
	rop.idx = CMDQ_SPR_FOR_TEMP;
	cmdq_pkt_logic_command(
		pkt, CMDQ_LOGIC_OR, CMDQ_CPR_HW_TRACE_TEMP, &lop, &rop);

	cmdq_hw_trace_check_inst(pkt);

	cmdq_pkt_set_event(pkt, CMDQ_TOKEN_HW_TRACE_WAIT);
	cmdq_pkt_set_event(pkt, CMDQ_TOKEN_HW_TRACE_LOCK);
	return 0;
}
EXPORT_SYMBOL(cmdq_pkt_hw_trace);

s32 cmdq_pkt_set_spr3_timer(struct cmdq_pkt *pkt)
{
	struct cmdq_client *client = pkt->cl;
	struct cmdq *cmdq;

	if (client) {
		cmdq = container_of(client->chan->mbox, struct cmdq, mbox);
	} else if (pkt->dev) {
		cmdq = dev_get_drvdata(pkt->dev);
	} else {
		cmdq_err("cl/dev is null");
		return -EINVAL;
	}

#if IS_ENABLED(CONFIG_VIRTIO_CMDQ)
	if (g_cmdq[cmdq->hwid]) {
		if (g_cmdq[cmdq->hwid]->spr3_timer)
			pkt->support_spr3_timer = true;
	} else {
		cmdq_log("g_cmdq[%d] is null", cmdq->hwid);
	}
#else
	if (g_cmdq[cmdq->hwid]->spr3_timer)
		pkt->support_spr3_timer = true;
#endif

	return 0;
}
EXPORT_SYMBOL(cmdq_pkt_set_spr3_timer);

#if IS_ENABLED(CONFIG_CMDQ_MMPROFILE_SUPPORT)
void cmdq_mmp_wait(struct mbox_chan *chan, void *pkt)
{
	struct cmdq_thread *thread = chan->con_priv;
	struct cmdq *cmdq = container_of(chan->mbox, typeof(*cmdq), mbox);

	mmprofile_log_ex(cmdq_mmp.wait, MMPROFILE_FLAG_PULSE,
		MMP_THD(thread, cmdq), (unsigned long)pkt);
}
EXPORT_SYMBOL(cmdq_mmp_wait);
#endif

#if IS_ENABLED(CONFIG_MTK_CMDQ_MBOX_EXT)
void cmdq_controller_set_fp(struct cmdq_util_controller_fp *cust_cmdq_util)
{
	cmdq_util_controller = cust_cmdq_util;
}
EXPORT_SYMBOL(cmdq_controller_set_fp);
#endif

void cmdq_set_outpin_event(struct cmdq_client *cl, bool ena)
{
	struct cmdq *cmdq = container_of(cl->chan->mbox, struct cmdq, mbox);

	cmdq->outpin_en = ena;
}
EXPORT_SYMBOL(cmdq_set_outpin_event);

module_init(cmdq_drv_init);

MODULE_LICENSE("GPL v2");
