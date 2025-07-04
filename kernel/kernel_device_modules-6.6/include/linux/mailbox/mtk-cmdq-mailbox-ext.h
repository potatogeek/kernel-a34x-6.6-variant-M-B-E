/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2015 MediaTek Inc.
 */

#ifndef __MTK_CMDQ_MAILBOX_H__
#define __MTK_CMDQ_MAILBOX_H__

#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/trace_events.h>

#if IS_ENABLED(CONFIG_MTK_CMDQ_MBOX_EXT)
typedef void (*util_dump_dbg_reg)(void *chan);
typedef u8 (*util_track_ctrl)(void *cmdq, phys_addr_t base, bool sec);
typedef bool (*util_thread_ddr_module)(const s32 thread);
typedef bool (*util_check_tf)(struct device	*dev,
	u32 sid, u32 tbu, u32 *axids);
struct cmdq_util_controller_fp {
	util_track_ctrl track_ctrl;
	util_thread_ddr_module thread_ddr_module;
	util_check_tf check_tf;
};

typedef bool (*cmdq_mminfra_power)(void);
typedef bool (*cmdq_mminfra_gce_cg)(u32);

typedef void (*cmdq_usage_cb)(u32);

void cmdq_controller_set_fp(struct cmdq_util_controller_fp *cust_cmdq_util);
#endif

/* see also gce platform binding header */
#define CMDQ_NO_TIMEOUT			0xffffffff
#define CMDQ_TIMEOUT_DEFAULT		1000
#define CMDQ_PREDUMP_DEFAULT_MS		200
#define CMDQ_PREDUMP_MS(timeout_ms)	\
	((timeout_ms == CMDQ_NO_TIMEOUT) ? CMDQ_PREDUMP_DEFAULT_MS : timeout_ms / 5)

#define CMDQ_THR_MAX_COUNT		32

#define CMDQ_INST_SIZE			8 /* instruction is 64-bit */
#define CMDQ_SUBSYS_SHIFT		16
#define CMDQ_OP_CODE_SHIFT		24
#define CMDQ_JUMP_PASS			(CMDQ_INST_SIZE)
#define CMDQ_CMD_BUFFER_SIZE		(PAGE_SIZE - 32 * CMDQ_INST_SIZE)
#define CMDQ_BUF_ALLOC_SIZE		(PAGE_SIZE)
#define CMDQ_NUM_CMD(cmd_size)		((cmd_size) / CMDQ_INST_SIZE)

#define CMDQ_WFE_UPDATE			BIT(31)
#define CMDQ_WFE_UPDATE_VALUE		BIT(16)
#define CMDQ_WFE_WAIT			BIT(15)
#define CMDQ_WFE_WAIT_VALUE		0x1
#define CMDQ_EVENT_MAX			0x3FF

#define CMDQ_THRD_PKT_ARR_MAX	1024

enum CMDQ_PKT_ID_ARR_IDX {
	CMDQ_PKT_ID_CNT,
	CMDQ_PKT_BUFFER_CNT,
	CMDQ_PKT_ID_ARR_IDX_END,
};

#define CMDQ_BUF_ADDR(buf) \
	((dma_addr_t)(buf->iova_base ? buf->iova_base : buf->pa_base))

/*
 * CMDQ_CODE_MASK:
 *   set write mask
 *   format: op mask
 * CMDQ_CODE_WRITE:
 *   write value into target register
 *   format: op subsys address value
 * CMDQ_CODE_JUMP:
 *   jump by offset
 *   format: op offset
 * CMDQ_CODE_WFE:
 *   wait for event and clear
 *   it is just clear if no wait
 *   format: [wait]  op event update:1 to_wait:1 wait:1
 *           [clear] op event update:1 to_wait:0 wait:0
 * CMDQ_CODE_EOC:
 *   end of command
 *   format: op irq_flag
 */
enum cmdq_code {
	CMDQ_CODE_READ  = 0x01,
	CMDQ_CODE_MASK = 0x02,
	CMDQ_CODE_MOVE = 0x02,
	CMDQ_CODE_WRITE = 0x04,
	CMDQ_CODE_POLL  = 0x08,
	CMDQ_CODE_JUMP = 0x10,
	CMDQ_CODE_WFE = 0x20,
	CMDQ_CODE_POLL_SLEEP = 0x30,
	CMDQ_CODE_EOC = 0x40,

	/* these are pseudo op code defined by SW */
	/* for instruction generation */
	CMDQ_CODE_WRITE_FROM_MEM = 0x05,
	CMDQ_CODE_WRITE_FROM_REG = 0x07,
	CMDQ_CODE_SET_TOKEN = 0x21,	/* set event */
	CMDQ_CODE_WAIT_NO_CLEAR = 0x22,	/* wait event, but don't clear it */
	CMDQ_CODE_CLEAR_TOKEN = 0x23,	/* clear event */
	CMDQ_CODE_RAW = 0x24,	/* allow entirely custom arg_a/arg_b */
	CMDQ_CODE_PREFETCH_ENABLE = 0x41,	/* enable prefetch marker */
	CMDQ_CODE_PREFETCH_DISABLE = 0x42,	/* disable prefetch marker */

	CMDQ_CODE_READ_S = 0x80,	/* read operation (v3 only) */
	CMDQ_CODE_WRITE_S = 0x90,	/* write operation (v3 only) */
	/* write with mask operation (v3 only) */
	CMDQ_CODE_WRITE_S_W_MASK = 0x91,
	CMDQ_CODE_LOGIC = 0xa0,	/* logic operation */
	CMDQ_CODE_JUMP_C_ABSOLUTE = 0xb0, /* conditional jump (absolute) */
	CMDQ_CODE_JUMP_C_RELATIVE = 0xb1, /* conditional jump (related) */
};

struct cmdq_cb_data {
	s32		err;
	void		*data;
};

enum cmdq_aee_type {
	CMDQ_AEE_WARN = 0x0,
	CMDQ_NO_AEE = 0x1,
	CMDQ_AEE_EXCEPTION = 0x2,
	CMDQ_NO_AEE_DUMP = 0x3,
};

enum cmdq_log_type {
	CMDQ_PWR_CHECK = 0,
};


typedef int (*cmdq_aee_cb)(struct cmdq_cb_data data);

typedef void (*cmdq_async_flush_cb)(struct cmdq_cb_data data);

struct cmdq_task_cb {
	cmdq_async_flush_cb	cb;
	void			*data;
};

struct cmdq_pkt_buffer {
	struct list_head	list_entry;
	void			*va_base;
	dma_addr_t		iova_base;
	dma_addr_t		pa_base;
	bool			use_pool;
	bool			map;
	u64			alloc_time;
	bool			copy;
};

struct cmdq_buf_pool {
	struct dma_pool *pool;
	atomic_t *cnt;
	u32 *limit;
};

struct cmdq_pkt_err {
	bool	wfe_timeout;
	u16		event;
	size_t		offset;
};

struct cmdq_append {
	bool	suspend;
	dma_addr_t pc[2];
	dma_addr_t end;
	u64 pre_last_inst;
};

struct cmdq_pkt {
	struct list_head	buf;
	size_t			avail_buf_size; /* available buf size */
	size_t			cmd_buf_size; /* command occupied size */
	size_t			buf_size; /* real buffer size */
	u32			buf_cnt;
	u32			priority;
	struct cmdq_task_cb	cb;
	struct cmdq_task_cb	err_cb;
	void			*user_data;
	void			*cl;
	struct device		*dev;	/* client assigned dev */
	bool			loop;
	bool			self_loop;
	void			*flush_item;
	struct completion	cmplt;
	struct cmdq_buf_pool	cur_pool;
#if IS_ENABLED(CONFIG_MTK_CMDQ_MBOX_EXT)
	bool			mdp;
	u64			rec_submit;
	u64			rec_trigger;
	u64			rec_wait;
	u64			rec_irq;
	void			*sec_data;
#endif	/* end of CONFIG_MTK_CMDQ_MBOX_EXT */
	bool			task_alloc;
	bool			task_alive;
	size_t			pause_offset;
	struct cmdq_pkt_err	err_data;
	cmdq_aee_cb		aee_cb;
	void			*user_priv; /* cmdq user use only */
	u32			vcp_eng;
#if IS_ENABLED(CONFIG_MTK_MT6382_BDG)
	void			*bdg_data;
	bool			reuse;
#endif
	bool		no_pool;
	bool		no_irq;
	struct cmdq_append append;

	struct work_struct	destroy_work;
	u32			write_addr_high;
	struct device	*share_dev;
	size_t			create_instr_cnt;
	bool			timeout_dump_hw_trace;
	bool		support_spr3_timer;
	u32				debug_id;
};

struct cmdq_thread {
	struct mbox_chan	*chan;
	void __iomem		*base;
	phys_addr_t		gce_pa;
	struct list_head	task_busy_list;
	struct timer_list	timeout;
	u32			timeout_ms;
	struct work_struct	timeout_work;
	u32			priority;
	u32			idx;
	bool			occupied;
	bool			dirty;
	atomic_t		usage;
	u64			timer_mod;
	u64			lock_time;
	u64			irq_time;
	u32			irq_task;
	u64			mbox_en;
	u64			mbox_dis;
	u32			cookie;
	cmdq_usage_cb usage_cb;
	bool			skip_fast_mtcmos;
	u64			user_cb_cost;
	u32			pkt_id_arr[CMDQ_THRD_PKT_ARR_MAX][CMDQ_PKT_ID_ARR_IDX_END];
	struct mutex	pkt_id_mutex;
#if IS_ENABLED(CONFIG_VIRTIO_CMDQ)
	bool			is_virtio;
#endif
};

extern int mtk_cmdq_log;
#define cmdq_log(fmt, args...) \
do { \
	if (mtk_cmdq_log) \
		pr_notice("[cmdq] "fmt" @%s,%u\n", \
		##args, __func__, __LINE__); \
} while (0)

extern int cmdq_pwr_log;
#define cmdq_log_level(idx, fmt, args...) \
do { \
	if (mtk_cmdq_log) { \
		cmdq_log(fmt, ##args); \
		continue; \
	} \
	switch (idx) { \
	case CMDQ_PWR_CHECK: \
		if (cmdq_pwr_log) \
			cmdq_log_ex(fmt, ##args); \
		break; \
	} \
} while (0)


/* MTK only functions */
#define cmdq_log_ex(fmt, args...) \
	pr_notice("[cmdq] "fmt" @%s,%u\n", ##args, __func__, __LINE__)

#define cmdq_msg(fmt, args...) \
	pr_notice("[cmdq] "fmt"\n", ##args)

#define cmdq_err(fmt, args...) \
	pr_notice("[cmdq][err] "fmt" @%s,%u\n", ##args, __func__, __LINE__)

#define cmdq_dump(fmt, args...) \
	pr_notice("[cmdq][err] "fmt"\n", ##args)

#ifndef CMDQ_TRACE_DISABLE
/* CMDQ FTRACE */
#define CMDQ_DRV_TRACE_FORCE_BEGIN_TID(tid, fmt, args...) \
	cmdq_print_trace("B|%d|" fmt "\n", tid, ##args) \

#define CMDQ_DRV_TRACE_FORCE_END_TID(tid, fmt, args...) \
	cmdq_print_trace("E|%d|" fmt "\n", tid, ##args) \

#define cmdq_trace_begin(fmt, args...) do { \
	preempt_disable(); \
	CMDQ_DRV_TRACE_FORCE_BEGIN_TID(current->tgid, fmt, ##args); \
	preempt_enable();\
} while (0)

#define cmdq_trace_end(fmt, args...) do { \
	preempt_disable(); \
	CMDQ_DRV_TRACE_FORCE_END_TID(current->tgid, fmt, ##args); \
	preempt_enable(); \
} while (0)

extern int cmdq_trace;
#define cmdq_trace_ex_begin(fmt, args...) do { \
	if (cmdq_trace) { \
		preempt_disable(); \
		CMDQ_DRV_TRACE_FORCE_BEGIN_TID(current->tgid, fmt, ##args); \
		preempt_enable();\
	} \
} while (0)

#define cmdq_trace_ex_end() do { \
	if (cmdq_trace) { \
		preempt_disable(); \
		cmdq_print_trace("E\n"); \
		preempt_enable(); \
	} \
} while (0)

#define cmdq_trace_c(fmt, args...) do { \
	preempt_disable(); \
	event_trace_printk(cmdq_get_tracing_mark(), \
		"C|"fmt, ##args); \
	preempt_enable(); \
} while (0)
#else
/* CMDQ FTRACE */
#define cmdq_trace_begin(fmt, args...) do { \
} while (0)

#define cmdq_trace_end(fmt, args...) do { \
} while (0)

extern int cmdq_trace;
#define cmdq_trace_ex_begin(fmt, args...) do { \
	if (cmdq_trace) \
		cmdq_trace_begin(fmt, ##args); \
} while (0)

#define cmdq_trace_ex_end(fmt, args...) do { \
	if (cmdq_trace) \
		cmdq_trace_end(fmt, ##args); \
} while (0)
#endif

void cmdq_print_trace(char *fmt, ...);
struct cmdq_thread *cmdq_get_thread(u8 thread_idx, u8 hwid);
char *cmdq_dump_pkt_usage(u32 hwid, char *buf_start, char *buf_end);
void cmdq_mbox_mtcmos_by_fast_chan(struct mbox_chan *chan, bool on);
void cmdq_mbox_mtcmos_by_fast(void *cmdq_mbox, bool on);
void cmdq_mbox_dump_fast_mtcmos(void *cmdq_mbox);
void cmdq_mbox_dump_gce_req(struct mbox_chan *chan);
dma_addr_t cmdq_thread_get_pc(struct cmdq_thread *thread);
dma_addr_t cmdq_thread_get_end(struct cmdq_thread *thread);
void cmdq_thread_set_spr(struct mbox_chan *chan, u8 id, u32 val);
void cmdq_init_cmds(void *dev_cmdq);
void cmdq_mbox_channel_stop(struct mbox_chan *chan);
void cmdq_dump_core(struct mbox_chan *chan);
void cmdq_thread_dump_spr(struct cmdq_thread *thread);
size_t cmdq_task_current_offset(dma_addr_t pa, struct cmdq_pkt *pkt);
void cmdq_thread_dump(struct mbox_chan *chan, struct cmdq_pkt *cl_pkt,
	u64 **inst_out, dma_addr_t *pc_out);
void cmdq_thread_dump_all(void *mbox_cmdq, const bool lock, const bool dump_pkt,
	const bool dump_prev);
void cmdq_thread_dump_all_seq(void *mbox_cmdq, struct seq_file *seq);
void cmdq_mbox_thread_remove_task(struct mbox_chan *chan,
	struct cmdq_pkt *pkt);
void cmdq_mbox_enable(void *chan);
void cmdq_mbox_disable(void *chan);
s32 cmdq_mbox_get_usage(void *chan);
void *cmdq_mbox_get_base(void *chan);
struct device *cmdq_mbox_get_dev(void *chan);
phys_addr_t cmdq_mbox_get_base_pa(void *chan);
phys_addr_t cmdq_dev_get_base_pa(struct device *dev);
phys_addr_t cmdq_mbox_get_dummy_reg(void *chan);
phys_addr_t cmdq_mbox_get_spr_pa(void *chan, u8 spr);
s32 cmdq_mbox_thread_reset(void *chan);
s32 cmdq_mbox_thread_suspend(void *chan);
void cmdq_mbox_thread_disable(void *chan);
u32 cmdq_mbox_get_thread_timeout(void *chan);
u32 cmdq_mbox_set_thread_timeout(void *chan, u32 timeout);
s32 cmdq_mbox_chan_id(void *chan);
void cmdq_mbox_check_buffer(struct mbox_chan *chan,
	struct cmdq_pkt_buffer *buffer);
s32 cmdq_task_get_thread_pc(struct mbox_chan *chan, dma_addr_t *pc_out);
s32 cmdq_task_get_thread_irq(struct mbox_chan *chan, u32 *irq_out);
s32 cmdq_task_get_thread_irq_en(struct mbox_chan *chan, u32 *irq_en_out);
s32 cmdq_task_get_thread_end_addr(struct mbox_chan *chan,
	dma_addr_t *end_addr_out);
s32 cmdq_task_get_task_info_from_thread_unlock(struct mbox_chan *chan,
	struct list_head *task_list_out, u32 *task_num_out);
s32 cmdq_task_get_pkt_from_thread(struct mbox_chan *chan,
	struct cmdq_pkt **pkt_list_out, u32 pkt_list_size, u32 *pkt_count_out);
void cmdq_set_event(void *chan, u16 event_id);
void cmdq_clear_event(void *chan, u16 event_id);
u32 cmdq_get_event(void *chan, u16 event_id);
void cmdq_dump_event(void *chan);
void cmdq_event_verify(void *chan, u16 event_id);
unsigned long cmdq_get_tracing_mark(void);
u32 cmdq_thread_timeout_backup(struct cmdq_thread *thread, const u32 ms);
void cmdq_thread_timeout_restore(struct cmdq_thread *thread, const u32 ms);
struct dma_pool *cmdq_alloc_user_pool(const char *name, struct device *dev);
s32 cmdq_mbox_set_hw_id(void *cmdq);
s32 cmdq_mbox_reset_hw_id(void *cmdq);
s32 cmdq_pkt_hw_trace(struct cmdq_pkt *pkt, const u16 event_id);
s32 cmdq_pkt_set_spr3_timer(struct cmdq_pkt *pkt);
s32 cmdq_thread_set_vm(struct cmdq_thread *thread);

#if IS_ENABLED(CONFIG_MMPROFILE)
void cmdq_mmp_wait(struct mbox_chan *chan, void *pkt);
#endif

s32 cmdq_sec_insert_backup_cookie(struct cmdq_pkt *pkt);
void cmdq_mbox_dump_dbg(void *mbox_cmdq, void *chan, const bool lock);
void cmdq_chan_dump_dbg(void *chan);
void cmdq_get_usage_cb(struct mbox_chan *chan, cmdq_usage_cb usage_cb);
void cmdq_get_mminfra_cb(cmdq_mminfra_power cb);
void cmdq_get_mminfra_gce_cg_cb(cmdq_mminfra_gce_cg cb);
void cmdq_dump_usage(void);
bool cmdq_get_support_vm(u8 hwid);
void cmdq_check_thread_complete(struct mbox_chan *chan);
u8 cmdq_get_irq_long_times(void *chan);
#if IS_ENABLED(CONFIG_MTK_CMDQ_DEBUG)
u32 cmdq_get_tf_high_addr(void *chan);
u32 cmdq_get_tf_high_addr_by_dev(struct device *dev);
#endif
void cmdq_event_dump_and_clr(void *chan);
u32 cmdq_mbox_get_tpr(void *chan);
bool cmdq_is_hw_trace_thread(struct mbox_chan *chan);
#endif /* __MTK_CMDQ_MAILBOX_H__ */
