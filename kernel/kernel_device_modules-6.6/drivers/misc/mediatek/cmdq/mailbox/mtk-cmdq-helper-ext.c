// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2015 MediaTek Inc.
 */

#include "linux/stddef.h"
#include <linux/completion.h>
#include <linux/errno.h>
#include <linux/of_address.h>
#include <linux/soc/mediatek/mtk-cmdq-ext.h>
#include <linux/mailbox_controller.h>
#include <linux/dma-mapping.h>
#include <linux/dmapool.h>
#include <linux/sched/clock.h>
#include <linux/timer.h>
#include <linux/delay.h>

#if !IS_ENABLED(CONFIG_MTK_CMDQ_DEBUG)
#include <linux/mmdebug.h>
#endif
#include <iommu_debug.h>
#ifdef CMDQ_DCACHE_INVAL
#include <asm/cacheflush.h>
#include <asm/set_memory.h>
#endif
#include "vcp.h"
#include "vcp_status.h"
#include "vcp_reg.h"
#if IS_ENABLED(CONFIG_VHOST_CMDQ)
#include "cmdq.h"
#endif

#if IS_ENABLED(CONFIG_MTK_CMDQ_MBOX_EXT)
#include "cmdq-util.h"
struct cmdq_util_helper_fp *cmdq_util_helper;

#ifdef CMDQ_SECURE_SUPPORT
#include "cmdq-sec.h"
struct cmdq_sec_helper_fp *cmdq_sec_helper;
#endif

#endif

#ifndef cmdq_util_msg
#define cmdq_util_msg(f, args...) cmdq_msg(f, ##args)
#endif
#ifndef cmdq_util_err
#define cmdq_util_err(f, args...) cmdq_dump(f, ##args)
#endif
#ifndef cmdq_util_aee
#define cmdq_util_aee(k, f, args...) cmdq_dump(f, ##args)
#endif
#ifndef cmdq_util_aee_ex
#define cmdq_util_aee_ex(aee, k, f, args...) cmdq_dump(f, ##args)
#endif

#define CMDQ_ARG_A_WRITE_MASK	0xffff
#define CMDQ_WRITE_ENABLE_MASK	BIT(0)
#define CMDQ_EOC_IRQ_EN		BIT(0)
#define CMDQ_EOC_IRQ_DIS	(0)
#define CMDQ_EOC_CMD		((u64)((CMDQ_CODE_EOC << CMDQ_OP_CODE_SHIFT)) \
				<<32)
#if IS_ENABLED(CONFIG_ARCH_DMA_ADDR_T_64BIT)
#define CMDQ_EOC_MASK		GENMASK(63, 1)
#else
#define CMDQ_EOC_MASK		GENMASK(31, 1)
#endif
#define CMDQ_MBOX_BUF_LIMIT	16 /* default limit count */
#define CMDQ_HW_MAX			2

/* sleep for 312 tick, which around 12us */
#define CMDQ_POLL_TICK			312
/* sleep for 8 bit, which around 9.85us */
#define CMDQ_POLL_SLEEP			0x8

#define CMDQ_GET_ADDR_H(addr)		(sizeof(addr) > 32 ? (addr >> 32) : 0)
#define CMDQ_GET_ARG_B(arg)		(((arg) & GENMASK(31, 16)) >> 16)
#define CMDQ_GET_ARG_C(arg)		((arg) & GENMASK(15, 0))
#define CMDQ_GET_32B_VALUE(argb, argc)	((u32)((argb) << 16) | (argc))
#define CMDQ_REG_IDX_PREFIX(type)	((type) ? "Reg Index " : "")
#define CMDQ_GET_REG_BASE(addr)		((addr) & GENMASK(31, 16))
#define CMDQ_GET_REG_OFFSET(addr)	((addr) & GENMASK(15, 0))
#define CMDQ_GET_ADDR_HIGH(addr)	((u32)((addr >> 16) & GENMASK(31, 0)))
#define CMDQ_ADDR_LOW_BIT		BIT(1)
#define CMDQ_GET_ADDR_LOW(addr)		((u16)(addr & GENMASK(15, 0)) | \
					CMDQ_ADDR_LOW_BIT)
#define CMDQ_IMMEDIATE_VALUE		0
#define CMDQ_REG_TYPE			1
#define CMDQ_WFE_OPTION			(CMDQ_WFE_UPDATE | CMDQ_WFE_WAIT | \
					CMDQ_WFE_WAIT_VALUE)
#define CMDQ_TPR_MASK			0xD0
#define CMDQ_TPR_TIMEOUT_EN		0xDC
#define CMDQ_GPR_R0_OFF			0x80

#define CMDQ_OPERAND_GET_IDX_VALUE(operand) \
	((operand)->reg ? (operand)->idx : (operand)->value)
#define CMDQ_OPERAND_TYPE(operand) \
	((operand)->reg ? CMDQ_REG_TYPE : CMDQ_IMMEDIATE_VALUE)

#define CMDQ_DBG_PERFBEGIN		CMDQ_CMD_BUFFER_SIZE
#define CMDQ_DBG_PERFEND		(CMDQ_DBG_PERFBEGIN + 4)

#define CMDQ_BUF_INIT_VAL		((u64)0xdeadbeafdeadbeaf)
#define CMDQ_PACK_IOVA(addr)     ((uint32_t)((addr) | (((addr) >> 32) & 0xF)))
#define	CMDQ_UNPACK_IOVA(addr)   \
	((uint64_t)(addr & 0xFFFFFFF0) | (((uint64_t)(addr) & 0xF) << 32))

#define VCP_TO_SPM_REG_PA			(0x1ec24098)
#define VCP_USER_CNT		(8)
#define MMINFRA_BASE		((dma_addr_t)0x1e800000)
#define MMINFRA_MBIST_DELSEL10	0xa28
#define MMINFRA_MBIST_DELSEL11	0xa2c
#define MMINFRA_MBIST_DELSEL12	0xa30
#define MMINFRA_MBIST_DELSEL13	0xa34
#define MMINFRA_MBIST_DELSEL14	0xa38
#define MMINFRA_MBIST_DELSEL15	0xa3c
#define MMINFRA_MBIST_DELSEL16	0xa40
#define MMINFRA_MBIST_DELSEL17	0xa44
#define MMINFRA_MBIST_DELSEL18	0xa48
#define MMINFRA_MBIST_DELSEL19	0xa4c


#define	MDP_VCP_BUF_SIZE 0x80000

#define	VCP_OFF_DELAY 1000 /* 1000ms */

#define EVENT_DEBUG_TIMES	5

u32 BUF_SIZE[CMDQ_HW_MAX];
u32 BUF_SIZE_THRD[CMDQ_HW_MAX][CMDQ_THR_MAX_COUNT];

u32 BUF_SIZE_MAX[CMDQ_HW_MAX];
u32 BUF_SIZE_THRD_MAX[CMDQ_HW_MAX][CMDQ_THR_MAX_COUNT];

u32 PKT_SIZE_THRD[CMDQ_HW_MAX][CMDQ_THR_MAX_COUNT];

struct mutex pkt_size_mutex;
struct workqueue_struct *cmdq_pkt_destroy_wq;

struct vcp_control {
	struct mutex vcp_mutex;
	struct timer_list	vcp_timer;
	struct work_struct	vcp_work;
	struct workqueue_struct *vcp_wq;
	atomic_t		vcp_usage;
	atomic_t		vcp_power;
	void __iomem	*mminfra_base;
};
struct vcp_control vcp;


struct client_priv {
	struct dma_pool *buf_pool;
	u32 pool_limit;
	atomic_t buf_cnt;
	struct workqueue_struct *flushq;
};

struct cmdq_instruction {
	u16 arg_c:16;
	u16 arg_b:16;
	u16 arg_a:16;
	u8 s_op:5;
	u8 arg_c_type:1;
	u8 arg_b_type:1;
	u8 arg_a_type:1;
	u8 op:8;
};

struct cmdq_vcp_inst {
	uint32_t offset : 18;
	uint16_t size : 9;
	uint8_t enable : 1;
	uint8_t enter : 1;
	uint8_t test : 2;
	uint8_t error : 1;
};

struct cmdq_flush_item {
	struct work_struct work;
	struct cmdq_pkt *pkt;
	cmdq_async_flush_cb cb;
	void *data;
	cmdq_async_flush_cb err_cb;
	void *err_data;
	s32 err;
	bool done;
};

#if IS_ENABLED(CONFIG_VIRTIO_CMDQ)
bool is_virtio_client(struct cmdq_client *cl)
{
	struct cmdq_thread *thread = cl->chan->con_priv;

	return thread->is_virtio;
}
#endif

#ifdef CMDQ_DCACHE_INVAL
void __weak dcache_inval_poc(unsigned long start, unsigned long end)
{
}

int __weak set_memory_valid(unsigned long addr, int numpages, int enable)
{
	return 0;
}
#endif

struct mutex buffer_size_mutex;

void cmdq_set_thrd_pkt_buf(struct cmdq_client *client, u32 debug_id, bool add)
{
	struct cmdq_thread *thread = cmdq_client_get_thread(client);

	if (!cmdq_dump_buf_size || debug_id >= CMDQ_THRD_PKT_ARR_MAX)
		return;

	if(thread) {
		mutex_lock(&thread->pkt_id_mutex);
		thread->pkt_id_arr[debug_id][CMDQ_PKT_BUFFER_CNT] += add ? 1 : -1;
		mutex_unlock(&thread->pkt_id_mutex);
	} else {
		cmdq_err("%s get thread fail", __func__);
		dump_stack();
	}
}

void cmdq_set_thrd_pkt_id(struct cmdq_pkt *pkt, bool add)
{
	struct cmdq_thread *thread;

	if (!cmdq_dump_buf_size || !pkt || pkt->debug_id >= CMDQ_THRD_PKT_ARR_MAX)
		return;

	thread = cmdq_client_get_thread(pkt->cl);

	if (thread) {
		mutex_lock(&thread->pkt_id_mutex);
		thread->pkt_id_arr[pkt->debug_id][CMDQ_PKT_ID_CNT] += add ? 1 : -1;
		thread->pkt_id_arr[pkt->debug_id][CMDQ_PKT_BUFFER_CNT] += add ?
			pkt->buf_cnt : (-pkt->buf_cnt);
		mutex_unlock(&thread->pkt_id_mutex);
	} else {
		cmdq_err("%s get thread fail", __func__);
		dump_stack();
	}
}

#if IS_ENABLED(CONFIG_VHOST_CMDQ)
struct vhost_cmdq_platform_fp *vhost_cmdq_platform;
void vhost_cmdq_util_set_fp(struct vhost_cmdq_platform_fp *cust_cmdq_platform)
{
	if (!cust_cmdq_platform) {
		cmdq_err("%s cmdq_util_platform_fp is NULL ", __func__);
		return;
	}
	vhost_cmdq_platform = cust_cmdq_platform;
}
EXPORT_SYMBOL(vhost_cmdq_util_set_fp);

void vhost_cmdq_set_client(void *client, uint32_t hwid)
{
	if (vhost_cmdq_platform)
		vhost_cmdq_platform->vhost_cmdq_set_client(client, hwid);
}
EXPORT_SYMBOL(vhost_cmdq_set_client);
#endif

void cmdq_hw_trace_dump(void *chan)
{
	u32 hwid = cmdq_util_get_hw_id((u32)cmdq_mbox_get_base_pa(chan));

	cmdq_util_hw_trace_dump(
		hwid, cmdq_util_get_bit_feature() & CMDQ_LOG_FEAT_PERF);
}
EXPORT_SYMBOL(cmdq_hw_trace_dump);

void cmdq_dump_buffer_size(void)
{
	int i, j;

	for (i = 0; i < CMDQ_HW_MAX; i++) {
		cmdq_msg("%s hwid:%d MAX total buf size:%u", __func__, i, BUF_SIZE_MAX[i]);
		for (j = 0; j < CMDQ_THR_MAX_COUNT; j += 4) {
			cmdq_msg("%s thr%d=[%u] thr%d=[%u] thr%d=[%u] thr%d=[%u]", __func__,
				j, BUF_SIZE_THRD_MAX[i][j],
				j + 1, BUF_SIZE_THRD_MAX[i][j + 1],
				j + 2, BUF_SIZE_THRD_MAX[i][j + 2],
				j + 3, BUF_SIZE_THRD_MAX[i][j + 3]);
		}
	}
}
EXPORT_SYMBOL(cmdq_dump_buffer_size);

char *cmdq_dump_buffer_size_seq(char *buf_start, char *buf_end)
{
	int i, j;

	for (i = 0; i < CMDQ_HW_MAX; i++) {
		buf_start += scnprintf(buf_start, buf_end - buf_start,
			"%s hwid:%d max total buf size:%u\n", __func__, i, BUF_SIZE_MAX[i]);
		for (j = 0; j < CMDQ_THR_MAX_COUNT; j += 4) {
			buf_start += scnprintf(buf_start, buf_end - buf_start,
				"%s max total buf size thr%d=[%u][%u][%u] thr%d=[%u][%u][%u] thr%d=[%u][%u][%u] thr%d=[%u][%u][%u]\n",
				__func__,
				j, PKT_SIZE_THRD[i][j] , BUF_SIZE_THRD[i][j],
				BUF_SIZE_THRD_MAX[i][j],
				j + 1, PKT_SIZE_THRD[i][j + 1], BUF_SIZE_THRD[i][j + 1],
				BUF_SIZE_THRD_MAX[i][j + 1],
				j + 2, PKT_SIZE_THRD[i][j + 2], BUF_SIZE_THRD[i][j + 2],
				BUF_SIZE_THRD_MAX[i][j + 2],
				j + 3, PKT_SIZE_THRD[i][j + 3], BUF_SIZE_THRD[i][j + 3],
				BUF_SIZE_THRD_MAX[i][j + 3]);
		}
	}
	for (i = 0; i < CMDQ_HW_MAX; i++)
		buf_start = cmdq_dump_pkt_usage(i, buf_start, buf_end);

	return buf_start;
}
EXPORT_SYMBOL(cmdq_dump_buffer_size_seq);

void cmdq_set_buffer_size(struct cmdq_client *cl, bool b)
{
	s32 hwid, thd;

	if (!cmdq_dump_buf_size)
		return;

	if (!cl) {
		struct cmdq_thread *thread;

		hwid = 0;
		thread = cmdq_client_get_thread(cl);
		if (!thread) {
			cmdq_err("Get mdp thread fail");
			return;
		}
		thd = thread->idx;
	} else {
		hwid = (s32)cmdq_util_get_hw_id((u32)cmdq_mbox_get_base_pa(cl->chan));
		thd = (s32)cmdq_mbox_chan_id(cl->chan);
	}

	if (hwid < 0 || thd < 0) {
		cmdq_err("hwid:%d thd:%d", hwid, thd);
		return;
	}
	mutex_lock(&buffer_size_mutex);
	if (b) {
		BUF_SIZE_THRD[hwid][thd]++;
		BUF_SIZE[hwid]++;

		if (BUF_SIZE_THRD[hwid][thd] >= BUF_SIZE_THRD_MAX[hwid][thd])
			BUF_SIZE_THRD_MAX[hwid][thd] = BUF_SIZE_THRD[hwid][thd];
		if (BUF_SIZE[hwid] >= BUF_SIZE_MAX[hwid])
			BUF_SIZE_MAX[hwid] = BUF_SIZE[hwid];
	} else {
		BUF_SIZE_THRD[hwid][thd]--;
		BUF_SIZE[hwid]--;
	}
	mutex_unlock(&buffer_size_mutex);
}

void cmdq_set_pkt_size(struct cmdq_client *cl, bool b)
{
	s32 hwid, thd;

	if (!cmdq_dump_buf_size)
		return;

	if (!cl || !cl->chan) {
		struct cmdq_thread *thread;

		hwid = 0;
		thread = cmdq_client_get_thread(cl);
		if (!thread) {
			cmdq_err("Get mdp thread fail");
			return;
		}
		thd = thread->idx;
	} else {
		hwid = (s32)cmdq_util_get_hw_id((u32)cmdq_mbox_get_base_pa(cl->chan));
		thd = (s32)cmdq_mbox_chan_id(cl->chan);
	}

	if (hwid < 0 || thd < 0) {
		cmdq_err("hwid:%d thd:%d", hwid, thd);
		return;
	}
	mutex_lock(&pkt_size_mutex);
	PKT_SIZE_THRD[hwid][thd] += b ? 1 : -1;
	mutex_unlock(&pkt_size_mutex);
}

static s8 cmdq_subsys_base_to_id(struct cmdq_base *clt_base, u32 base)
{
	u8 i;

	if (!clt_base)
		return -EINVAL;

	base = base & 0xFFFF0000;
	for (i = 0; i < clt_base->count; i++) {
		if (clt_base->subsys[i].base == base)
			return clt_base->subsys[i].id;
	}

	return -EINVAL;
}

u32 cmdq_subsys_id_to_base(struct cmdq_base *clt_base, int id)
{
	u32 i;

	if (!clt_base)
		return 0;

	for (i = 0; i < clt_base->count; i++) {
		if (clt_base->subsys[i].id == id)
			return clt_base->subsys[i].base;
	}

	return 0;
}
EXPORT_SYMBOL(cmdq_subsys_id_to_base);

int cmdq_pkt_realloc_cmd_buffer(struct cmdq_pkt *pkt, size_t size)
{
	while (pkt->buf_size < size)
		cmdq_pkt_add_cmd_buffer(pkt);
	return 0;
}
EXPORT_SYMBOL(cmdq_pkt_realloc_cmd_buffer);

struct cmdq_base *cmdq_register_device(struct device *dev)
{
	struct cmdq_base *clt_base;
	struct of_phandle_args spec;
	u32 vals[2] = {0}, idx;
	s32 ret;

	clt_base = devm_kzalloc(dev, sizeof(*clt_base), GFP_KERNEL);
	if (!clt_base)
		return NULL;

	/* parse subsys */
	for (idx = 0; idx < ARRAY_SIZE(clt_base->subsys); idx++) {
		if (of_parse_phandle_with_args(dev->of_node, "gce-subsys",
			"#gce-subsys-cells", idx, &spec))
			break;
		clt_base->subsys[idx].base = spec.args[0];
		clt_base->subsys[idx].id = spec.args[1];
	}
	clt_base->count = idx;

	/* parse CPR range */
	ret = of_property_read_u32_array(dev->of_node, "gce-cpr-range",
		vals, 2);
	if (!ret) {
		clt_base->cpr_base = vals[0] + CMDQ_CPR_STRAT_ID;
		clt_base->cpr_cnt = vals[1];
		cmdq_msg("support cpr:%d count:%d", vals[0], vals[1]);
	}

	return clt_base;
}
EXPORT_SYMBOL(cmdq_register_device);

struct cmdq_client *cmdq_mbox_create(struct device *dev, int index)
{
	struct cmdq_client *client;
	struct client_priv *priv;
	struct device_node *iommu;
	struct device *mbox_dev;
	struct device *device;

	client = kzalloc(sizeof(*client), GFP_KERNEL);
	if (!client)
		return ERR_PTR(-ENOMEM);

	client->client.dev = dev;
	client->client.tx_block = false;
	client->client.knows_txdone = true;
	client->chan = mbox_request_channel(&client->client, index);
	if (IS_ERR(client->chan)) {
		cmdq_err("channel request fail:%ld idx:%d",
			PTR_ERR(client->chan), index);
#if !IS_ENABLED(CONFIG_VHOST_CMDQ)
		dump_stack();
#endif
		kfree(client);
		return NULL;
	}

	if (of_property_read_bool(dev->of_node, "gce-skip-fast-mtcmos")) {
		struct cmdq_thread *thread = (struct cmdq_thread *)client->chan->con_priv;

		thread->skip_fast_mtcmos = true;
	}

	mbox_dev = client->chan->mbox->dev;
	device = mtk_smmu_get_shared_device(mbox_dev);
	client->share_dev = device;
	iommu = of_parse_phandle(device->of_node, "iommus", 0);
	client->use_iommu = iommu ? true : false;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		cmdq_mbox_destroy(client);
		return ERR_PTR(-ENOMEM);
	}

	priv->pool_limit = CMDQ_MBOX_BUF_LIMIT;
	priv->flushq = create_singlethread_workqueue("cmdq_flushq");
	client->cl_priv = (void *)priv;

	mutex_init(&client->chan_mutex);

	return client;
}
EXPORT_SYMBOL(cmdq_mbox_create);

#if IS_ENABLED(CONFIG_VHOST_CMDQ)
struct cmdq_client *virtio_cmdq_mbox_create(struct device *dev, int index)
{
	struct cmdq_client *client;
	struct client_priv *priv;

	client = kzalloc(sizeof(*client), GFP_KERNEL);
	if (!client)
		return ERR_PTR(-ENOMEM);

	client->client.dev = dev;
	client->client.tx_block = false;
	client->chan = mbox_request_channel(&client->client, index);
	if (IS_ERR(client->chan)) {
		cmdq_err("channel request fail:%d, idx:%d",
			PTR_ERR(client->chan), index);
		dump_stack();
		kfree(client);
		return NULL;
	}

	client->is_virtio = true;
	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		cmdq_mbox_destroy(client);
		return ERR_PTR(-ENOMEM);
	}

	priv->pool_limit = CMDQ_MBOX_BUF_LIMIT;
	priv->flushq = create_singlethread_workqueue("cmdq_flushq");
	client->cl_priv = (void *)priv;

	mutex_init(&client->chan_mutex);

	return client;
}
EXPORT_SYMBOL(virtio_cmdq_mbox_create);
#endif
void cmdq_mbox_stop(struct cmdq_client *cl)
{
#if IS_ENABLED(CONFIG_VIRTIO_CMDQ)
	if (is_virtio_client(cl)) {
		virtio_cmdq_mbox_channel_stop(cl->chan);
		return;
	}
#elif IS_ENABLED(CONFIG_VHOST_CMDQ)
	if (cl->is_virtio) {
		vhost_cmdq_platform->vhost_cmdq_mbox_channel_stop(cl->chan);
		return;
}
#endif

	cmdq_mbox_channel_stop(cl->chan);
}
EXPORT_SYMBOL(cmdq_mbox_stop);

static void cmdq_vcp_off_work(struct work_struct *work_item)
{
	mutex_lock(&vcp.vcp_mutex);
	if (!atomic_read(&vcp.vcp_usage) && (atomic_read(&vcp.vcp_power) > 0)) {
		cmdq_msg("[VCP] power off VCP");
#if IS_ENABLED(CONFIG_MTK_TINYSYS_VCP_SUPPORT)
		vcp_deregister_feature_ex(GCE_FEATURE_ID);
#endif
		atomic_dec(&vcp.vcp_power);
	}
	mutex_unlock(&vcp.vcp_mutex);
}

static void cmdq_vcp_off(struct timer_list *t)
{
	if (!work_pending(&vcp.vcp_work))
		queue_work(vcp.vcp_wq, &vcp.vcp_work);
}

#if IS_ENABLED(CONFIG_MTK_TINYSYS_VCP_SUPPORT)
static void cmdq_vcp_is_ready(void)
{
	int i = 0;

	/* wait mmup ready */
	while (!is_vcp_ready_ex(GCE_FEATURE_ID)) {
		cmdq_log("[VCP] wait ready");
		i += 5;
		msleep_interruptible(5);
		if (i > 100) {
			cmdq_err("[VCP] wait ready timeout");
			break;
		}
	}

}
#endif

void cmdq_vcp_enable(bool en)
{
	if (!vcp.mminfra_base) {
		cmdq_msg("%s not support", __func__);
		return;
	}
	mutex_lock(&vcp.vcp_mutex);
	if (en) {
		if (atomic_inc_return(&vcp.vcp_usage) == 1)
			del_timer(&vcp.vcp_timer);
		if (atomic_read(&vcp.vcp_power) <= 0) {
#if IS_ENABLED(CONFIG_MTK_TINYSYS_VCP_SUPPORT)
			dma_addr_t buf_pa = vcp_get_reserve_mem_phys_ex(GCE_MEM_ID);

			vcp_register_feature_ex(GCE_FEATURE_ID);
			atomic_inc(&vcp.vcp_power);
			mutex_unlock(&vcp.vcp_mutex);
			cmdq_msg("[VCP] power on VCP");
			cmdq_vcp_is_ready();
			writel(CMDQ_PACK_IOVA(buf_pa), vcp.mminfra_base + MMINFRA_MBIST_DELSEL10);
#endif
			return;
		}
	} else {
		if (atomic_dec_return(&vcp.vcp_usage) == 0)
			mod_timer(&vcp.vcp_timer, jiffies +
				msecs_to_jiffies(VCP_OFF_DELAY));
	}
	mutex_unlock(&vcp.vcp_mutex);
}
EXPORT_SYMBOL(cmdq_vcp_enable);

void *cmdq_get_vcp_buf(enum CMDQ_VCP_ENG_ENUM engine, dma_addr_t *pa_out)
{
	void *va;

	switch (engine) {
#if IS_ENABLED(CONFIG_MTK_TINYSYS_VCP_SUPPORT)
	case CMDQ_VCP_ENG_MDP_HDR0 ... CMDQ_VCP_ENG_MDP_AAL1:
		*pa_out = vcp_get_reserve_mem_phys_ex(GCE_MEM_ID);
		va = (void *)vcp_get_reserve_mem_virt_ex(GCE_MEM_ID);
		break;
	case CMDQ_VCP_ENG_MML_HDR0 ... CMDQ_VCP_ENG_MML_AAL1:
		*pa_out = vcp_get_reserve_mem_phys_ex(GCE_MEM_ID) + MDP_VCP_BUF_SIZE;
		va = (void *)(vcp_get_reserve_mem_virt_ex(GCE_MEM_ID) + MDP_VCP_BUF_SIZE);
		break;
#endif
	default:
		return NULL;
	}
	return va;
}
EXPORT_SYMBOL(cmdq_get_vcp_buf);

static dma_addr_t cmdq_get_vcp_dummy(enum CMDQ_VCP_ENG_ENUM engine)
{
	const dma_addr_t offset[VCP_USER_CNT] = {
		MMINFRA_MBIST_DELSEL12, MMINFRA_MBIST_DELSEL13,
		MMINFRA_MBIST_DELSEL14, MMINFRA_MBIST_DELSEL15,
		MMINFRA_MBIST_DELSEL16, MMINFRA_MBIST_DELSEL17,
		MMINFRA_MBIST_DELSEL18, MMINFRA_MBIST_DELSEL19,};

	if (engine >= VCP_USER_CNT)
		return 0;

	return MMINFRA_BASE + offset[engine];
}

void cmdq_pkt_set_noirq(struct cmdq_pkt *pkt, const bool noirq)
{
	pkt->no_irq = noirq;
}
EXPORT_SYMBOL(cmdq_pkt_set_noirq);

u32 cmdq_pkt_vcp_reuse_val(enum CMDQ_VCP_ENG_ENUM engine, u32 buf_offset, u16 size)
{
	u32 offset = 0;
	u32 val;
	struct cmdq_vcp_inst *vcp_inst = (struct cmdq_vcp_inst *)&val;

	switch (engine) {
	case CMDQ_VCP_ENG_MDP_HDR0:
	case CMDQ_VCP_ENG_MDP_HDR1:
	case CMDQ_VCP_ENG_MDP_AAL0:
	case CMDQ_VCP_ENG_MDP_AAL1:
		offset = buf_offset >> 2;
		break;
	case CMDQ_VCP_ENG_MML_HDR0:
	case CMDQ_VCP_ENG_MML_HDR1:
	case CMDQ_VCP_ENG_MML_AAL0:
	case CMDQ_VCP_ENG_MML_AAL1:
		offset = (buf_offset + MDP_VCP_BUF_SIZE) >> 2;
		break;
	}
	vcp_inst->enable = 1;
	vcp_inst->offset = offset & 0x3FFFF;
	vcp_inst->size = size & 0x1FF;
	return val;
}
EXPORT_SYMBOL(cmdq_pkt_vcp_reuse_val);

s32 cmdq_pkt_readback(struct cmdq_pkt *pkt, enum CMDQ_VCP_ENG_ENUM engine,
	u32 buf_offset, u16 size, u16 reg_gpr,
	struct cmdq_reuse *reuse,
	struct cmdq_poll_reuse *poll_reuse)
{
	s32 err = 0;
	dma_addr_t addr;
	u32 value;

	pkt->vcp_eng |= 1 << (u32)engine;
	value = cmdq_pkt_vcp_reuse_val(engine, buf_offset, size);
	addr = cmdq_get_vcp_dummy(engine);
	if (!addr)
		return -1;

	cmdq_pkt_write(pkt, NULL, addr, value, ~0);
	if (reuse) {
		reuse->op = CMDQ_CODE_WRITE_S;
		reuse->va = cmdq_pkt_get_curr_buf_va(pkt) - CMDQ_INST_SIZE;
		reuse->offset = pkt->cmd_buf_size - CMDQ_INST_SIZE;
	}
#if IS_ENABLED(CONFIG_MTK_TINYSYS_VCP_SUPPORT)
	/* vcp irq B_GIPC2_SETCLR_0 */
	cmdq_pkt_write(pkt, NULL, VCP_TO_SPM_REG_PA, B_GIPC2_SETCLR_0, B_GIPC2_SETCLR_0);
#endif
	cmdq_pkt_poll_timeout_reuse(pkt, 0, SUBSYS_NO_SUPPORT,
		addr, ~0, U16_MAX, reg_gpr, poll_reuse);

	return err;
}
EXPORT_SYMBOL(cmdq_pkt_readback);

static void cmdq_dump_vcp_reg(struct cmdq_pkt *pkt)
{
	int eng;
	unsigned long eng_bit;
	dma_addr_t addr;
	u32 val;
	struct cmdq_vcp_inst *vcp_inst = (struct cmdq_vcp_inst *)&val;

	if (!vcp.mminfra_base) {
		cmdq_msg("%s not support", __func__);
		return;
	}

	if (!pkt)
		return;

	eng_bit = pkt->vcp_eng;
	for_each_set_bit(eng, &eng_bit, 32) {
		addr = cmdq_get_vcp_dummy(eng);
		if (!addr)
			continue;
		val = readl(vcp.mminfra_base + (addr - MMINFRA_BASE));
		cmdq_msg("%s vcp eng:%d, addr[%pa]=%#x enable:%d enter:%d error:%d",
			__func__, eng, &addr, val,
			vcp_inst->enable, vcp_inst->enter, vcp_inst->error);
	}
}

static bool cmdq_pkt_is_exec(struct cmdq_pkt *pkt)
{
	if (pkt && pkt->task_alloc && !pkt->rec_irq)
		return true;
	return false;
}

void cmdq_mbox_pool_set_limit(struct cmdq_client *cl, u32 limit)
{
	struct client_priv *priv = (struct client_priv *)cl->cl_priv;

	priv->pool_limit = limit;
}
EXPORT_SYMBOL(cmdq_mbox_pool_set_limit);

void cmdq_mbox_pool_create(struct cmdq_client *cl)
{
	struct client_priv *priv = (struct client_priv *)cl->cl_priv;

	if (unlikely(priv->buf_pool)) {
		cmdq_msg("buffer pool already created");
		return;
	}

	priv->buf_pool = dma_pool_create("cmdq", cl->share_dev,
		CMDQ_BUF_ALLOC_SIZE, 0, 0);
}
EXPORT_SYMBOL(cmdq_mbox_pool_create);

void cmdq_mbox_pool_clear(struct cmdq_client *cl)
{
	struct client_priv *priv = (struct client_priv *)cl->cl_priv;

	/* check pool still in use */
	if (unlikely((atomic_read(&priv->buf_cnt)))) {
		cmdq_msg("buffers still in use:%d",
			atomic_read(&priv->buf_cnt));
		return;
	}

	dma_pool_destroy(priv->buf_pool);
	priv->buf_pool = NULL;
}
EXPORT_SYMBOL(cmdq_mbox_pool_clear);

static void *cmdq_mbox_pool_alloc_impl(struct dma_pool *pool,
	dma_addr_t *pa_out, atomic_t *cnt, u32 limit)
{
	void *va;
	dma_addr_t pa;

	if (atomic_inc_return(cnt) > limit) {
		/* not use pool, decrease to value before call */
		atomic_dec(cnt);
		return NULL;
	}

	va = dma_pool_alloc(pool, GFP_KERNEL, &pa);
	if (!va) {
		atomic_dec(cnt);
		cmdq_err(
			"alloc buffer from pool fail va:0x%p pa:%pa pool:0x%p count:%d",
			va, &pa, pool,
			(s32)atomic_read(cnt));
		return NULL;
	}

	*pa_out = pa;

	return va;
}

static void cmdq_mbox_pool_free_impl(struct dma_pool *pool, void *va,
	dma_addr_t pa, atomic_t *cnt)
{
	if (unlikely(atomic_read(cnt) <= 0 || !pool)) {
		cmdq_err("free pool cnt:%d pool:0x%p",
			(s32)atomic_read(cnt), pool);
		return;
	}

	dma_pool_free(pool, va, pa);
	atomic_dec(cnt);
}

static void *cmdq_mbox_pool_alloc(struct cmdq_client *cl, dma_addr_t *pa_out)
{
	struct client_priv *priv = (struct client_priv *)cl->cl_priv;

	if (unlikely(!priv->buf_pool)) {
		cmdq_mbox_pool_create(cl);
		if (unlikely(!priv->buf_pool)) {
			cmdq_err("fail to create dma pool dev:0x%p",
				cl->chan->mbox->dev);
			return NULL;
		}
	}

	return cmdq_mbox_pool_alloc_impl(priv->buf_pool,
		pa_out, &priv->buf_cnt, priv->pool_limit);
}

static void cmdq_mbox_pool_free(struct cmdq_client *cl, void *va, dma_addr_t pa)
{
	struct client_priv *priv = (struct client_priv *)cl->cl_priv;

	cmdq_mbox_pool_free_impl(priv->buf_pool, va, pa, &priv->buf_cnt);
}

static void *cmdq_mbox_buf_alloc_dev(struct device *dev, dma_addr_t *pa_out)
{
	void *va = NULL;
	dma_addr_t pa = 0;

	if (dev)
		va = dma_alloc_coherent(dev, CMDQ_BUF_ALLOC_SIZE, &pa, GFP_KERNEL);

	if (!va) {
		cmdq_err("alloc dma buffer fail dev:0x%p", dev);
		dump_stack();
		return NULL;
	}

	*pa_out = pa;
	return va;
}

void *cmdq_mbox_buf_alloc(struct cmdq_client *cl, dma_addr_t *pa_out)
{
	void *va;
	dma_addr_t pa = 0;
	struct device *mbox_dev;
#if IS_ENABLED(CONFIG_MTK_CMDQ_DEBUG)
	struct cmdq_thread *thread;
	s32 hwid;
	u32 tf_high_addr;
#endif

	if (!cl) {
		cmdq_err("cl is NULL");
		dump_stack();
		return NULL;
	}
	mbox_dev = cl->chan->mbox->dev;

	va = cmdq_mbox_buf_alloc_dev(cl->share_dev, &pa);
	if (!va) {
		cmdq_err("alloc dma buffer fail dev:0x%p", mbox_dev);
		return NULL;
	}

	cmdq_set_buffer_size(cl, true);
	cmdq_set_thrd_pkt_buf(cl, CMDQ_THRD_PKT_ARR_MAX - 1, true);
	*pa_out = pa + gce_mminfra;
#if IS_ENABLED(CONFIG_MTK_CMDQ_DEBUG)
	if(!cmdq_util_is_secure_client(cl)) {
		hwid = (s32)cmdq_util_get_hw_id((u32)cmdq_mbox_get_base_pa(cl->chan));
		thread = (struct cmdq_thread *)cl->chan->con_priv;
		tf_high_addr = cmdq_get_tf_high_addr(cl->chan);
		if(tf_high_addr &&CMDQ_GET_ADDR_HIGH(pa) == tf_high_addr)
			cmdq_err("hwid:%d thrd_id:%d iova:%pad", hwid, thread->idx, &pa);
		cmdq_util_user_buf_track(cl, pa, true);
	}
#endif
	return va;
}
EXPORT_SYMBOL(cmdq_mbox_buf_alloc);

static void cmdq_mbox_buf_free_dev(struct device *dev, void *va, dma_addr_t pa)
{
	dma_free_coherent(dev, CMDQ_BUF_ALLOC_SIZE, va, pa);
}

void cmdq_mbox_buf_free(struct cmdq_client *cl, void *va, dma_addr_t pa)
{
#if IS_ENABLED(CONFIG_MTK_CMDQ_DEBUG)
	struct cmdq_thread *thread;
	s32 hwid;
	u32 tf_high_addr;
#endif
	if (!cl) {
		cmdq_err("cl is NULL");
		dump_stack();
		return;
	}

	cmdq_mbox_buf_free_dev(cl->share_dev, va, pa - gce_mminfra);
	cmdq_set_buffer_size(cl, false);
	cmdq_set_thrd_pkt_buf(cl, CMDQ_THRD_PKT_ARR_MAX - 1, false);
#if IS_ENABLED(CONFIG_MTK_CMDQ_DEBUG)
	if(!cmdq_util_is_secure_client(cl)) {
		thread = (struct cmdq_thread *)cl->chan->con_priv;
		hwid = (s32)cmdq_util_get_hw_id((u32)cmdq_mbox_get_base_pa(cl->chan));
		tf_high_addr = cmdq_get_tf_high_addr(cl->chan);
		if(tf_high_addr &&
			CMDQ_GET_ADDR_HIGH((pa - gce_mminfra)) == tf_high_addr)
			cmdq_err("hwid:%d thrd_id:%d iova:%llx", hwid, thread->idx, pa - gce_mminfra);
		cmdq_util_user_buf_track(cl, pa, false);
	}
#endif
}
EXPORT_SYMBOL(cmdq_mbox_buf_free);

/* parse event from dts
 *
 * Example
 *
 * dts:
 * gce-event-names = "disp_rdma0_sof",
 *	"disp_rdma1_sof",
 *	"mdp_rdma0_sof";
 * gce-events = <&gce_mbox CMDQ_EVENT_DISP_RDMA0_SOF>,
 *	<&gce_mbox CMDQ_EVENT_DISP_RDMA1_SOF>,
 *	<&gce_mbox CMDQ_EVENT_MDP_RDMA0_SOF>;
 *
 * call:
 * s32 rdma0_sof_event_id = cmdq_dev_get_event(dev, "disp_rdma0_sof");
 */
s32 cmdq_dev_get_event(struct device *dev, const char *name)
{
	s32 index = 0;
	struct of_phandle_args spec = {0};
	s32 result;

	if (!dev) {
		cmdq_err("no device node");
		return -EINVAL;
	}

	index = of_property_match_string(dev->of_node, "gce-event-names", name);
	if (index < 0) {
		cmdq_err("no gce-event-names property or no such event:%s",
			name);
		return index;
	}

	if (of_parse_phandle_with_args(dev->of_node, "gce-events",
		"#gce-event-cells", index, &spec)) {
		cmdq_err("can't parse gce-events property");
		return -ENODEV;
	}

	result = spec.args[0];
	of_node_put(spec.np);

	return result;
}
EXPORT_SYMBOL(cmdq_dev_get_event);

struct cmdq_pkt_buffer *cmdq_pkt_alloc_buf(struct cmdq_pkt *pkt)
{
	struct cmdq_client *cl = (struct cmdq_client *)pkt->cl;
	struct cmdq_pkt_buffer *buf;
	bool use_iommu = false;
#if IS_ENABLED(CONFIG_MTK_CMDQ_DEBUG)
	struct cmdq_thread *thread;
	s32 hwid;
	u32 tf_high_addr;
	u64 start = sched_clock(), end[6];
	u32 end_cnt = 0;
#endif
#ifdef CMDQ_DCACHE_INVAL
	void *va;
#endif
	struct device *device;

	if (!pkt->share_dev && pkt->dev)
		pkt->share_dev = mtk_smmu_get_shared_device(pkt->dev);
	device = pkt->share_dev;

	buf = kzalloc(sizeof(*buf), GFP_KERNEL);
	if (!buf)
		return ERR_PTR(-ENOMEM);
#if IS_ENABLED(CONFIG_MTK_CMDQ_DEBUG)
	end[end_cnt++] = sched_clock();
#endif
	if (cl)
		use_iommu = cl->use_iommu;
	else if (device && device->of_node) {
		struct device_node *iommu;

		cmdq_log("%s cl is NULL, use pkt->dev", __func__);
		iommu = of_parse_phandle(device->of_node, "iommus", 0);
		use_iommu = iommu?true:false;
	} else {
		cmdq_err("pkt->dev is NULL");
		kfree(buf);
		dump_stack();
		return ERR_PTR(-ENODEV);
	}

	cmdq_pkt_set_spr3_timer(pkt);
	cmdq_set_buffer_size(cl, true);

	/* try dma pool if available */
	if (pkt->cur_pool.pool && !pkt->no_pool)
		buf->va_base = cmdq_mbox_pool_alloc_impl(pkt->cur_pool.pool,
			use_iommu ? &buf->iova_base : &buf->pa_base,
			pkt->cur_pool.cnt, *pkt->cur_pool.limit);
	else if (cl && !pkt->no_pool) {
		struct client_priv *priv = (struct client_priv *)cl->cl_priv;

		buf->va_base = cmdq_mbox_pool_alloc(cl,
			use_iommu ? &buf->iova_base : &buf->pa_base);
		if (buf->va_base) {
			pkt->cur_pool.pool = priv->buf_pool;
			pkt->cur_pool.cnt = &priv->buf_cnt;
			pkt->cur_pool.limit = &priv->pool_limit;
		}
	}

	if (buf->va_base)
		buf->use_pool = true;
	else	/* allocate directly */
		buf->va_base = cmdq_mbox_buf_alloc_dev(device,
			use_iommu ? &buf->iova_base : &buf->pa_base);
#if IS_ENABLED(CONFIG_MTK_CMDQ_DEBUG)
	end[end_cnt++] = sched_clock();
	if(cl) {
		if(!cmdq_util_is_secure_client(cl)) {
			tf_high_addr = cmdq_get_tf_high_addr(cl->chan);
			thread = (struct cmdq_thread *)cl->chan->con_priv;
			hwid = (s32)cmdq_util_get_hw_id((u32)cmdq_mbox_get_base_pa(cl->chan));
			if(tf_high_addr &&
				CMDQ_GET_ADDR_HIGH(CMDQ_BUF_ADDR(buf)) == tf_high_addr) {
				cmdq_err("hwid:%d thrd_id:%d iova:%llx",
					hwid, thread->idx, (unsigned long long)CMDQ_BUF_ADDR(buf));
			}
		}
	} else {
		tf_high_addr = cmdq_get_tf_high_addr_by_dev(pkt->dev);
		if(tf_high_addr &&
			CMDQ_GET_ADDR_HIGH(CMDQ_BUF_ADDR(buf)) == tf_high_addr) {
			cmdq_err("no client iova:%llx", (unsigned long long)CMDQ_BUF_ADDR(buf));
			dump_stack();
		}
	}
	end[end_cnt++] = sched_clock();
#endif


	if (!buf->va_base) {
		cmdq_err("allocate cmd buffer failed");
		kfree(buf);
		return ERR_PTR(-ENOMEM);
	}
	if (use_iommu) {
		struct iommu_domain *domain = NULL;

		if (device)
			domain = iommu_get_domain_for_dev(device);

		if (domain)
			buf->pa_base =
				iommu_iova_to_phys(domain, buf->iova_base);
		else
			cmdq_err("cannot get dev:%p domain", device);
	}
#if IS_ENABLED(CONFIG_MTK_CMDQ_DEBUG)
	end[end_cnt++] = sched_clock();
#endif
#ifdef CMDQ_DCACHE_INVAL
	va = phys_to_virt((unsigned long)buf->pa_base);
	dcache_inval_poc((unsigned long)va,
		(unsigned long)(va + CMDQ_BUF_ALLOC_SIZE));
	set_memory_valid((unsigned long)va, 1, false);
	dcache_inval_poc((unsigned long)buf->va_base,
		(unsigned long)(buf->va_base + CMDQ_BUF_ALLOC_SIZE));
#endif
#if IS_ENABLED(CONFIG_MTK_CMDQ_DEBUG)
	end[end_cnt++] = sched_clock();
	*((u64 *)buf->va_base) = CMDQ_BUF_INIT_VAL;
	*((u64 *)buf->va_base + 1) = CMDQ_BUF_INIT_VAL;
#endif
	buf->alloc_time = sched_clock();
	list_add_tail(&buf->list_entry, &pkt->buf);
	pkt->buf_cnt += 1;
	cmdq_set_thrd_pkt_buf(cl, pkt->debug_id, true);
	pkt->avail_buf_size += CMDQ_CMD_BUFFER_SIZE;
	pkt->buf_size += CMDQ_CMD_BUFFER_SIZE;
#if IS_ENABLED(CONFIG_MTK_CMDQ_DEBUG)
	end[end_cnt] = sched_clock();
	if (end[end_cnt] - start >= 10000000) /* 10ms */
		cmdq_msg("%s cost time %llu alloc:%llu buf:%llu debug:%llu iommu:%llu dcache:%llu list:%llu",
			__func__, end[end_cnt] - start, end[0] - start, end[1] - end[0],
			end[2] - end[1], end[3] - end[2], end[4] - end[3], end[5] - end[4]);
#endif
	return buf;
}
EXPORT_SYMBOL(cmdq_pkt_alloc_buf);

void cmdq_pkt_free_buf(struct cmdq_pkt *pkt)
{
	struct cmdq_client *cl = (struct cmdq_client *)pkt->cl;
	struct cmdq_pkt_buffer *buf, *tmp;
#ifdef CMDQ_DCACHE_INVAL
	void *va;
#endif

	list_for_each_entry_safe(buf, tmp, &pkt->buf, list_entry) {
		list_del(&buf->list_entry);
		pkt->buf_cnt -= 1;
		if (!pkt->dev || !buf->va_base || !CMDQ_BUF_ADDR(buf))
			cmdq_err("pkt:0x%p pa:%pa iova:%pa",
			pkt, &buf->pa_base, &buf->iova_base);
#ifdef CMDQ_DCACHE_INVAL
		va = phys_to_virt((unsigned long)buf->pa_base);
		set_memory_valid((unsigned long)va, 1, true);
#endif
		if (buf->iova_base) {
			struct iommu_domain *domain = iommu_get_domain_for_dev(
						pkt->share_dev);

			if (domain) {
				dma_addr_t pa_base =
					iommu_iova_to_phys(domain, buf->iova_base);

				if (!pa_base)
					cmdq_err("unmap iova: pool:%d pa:%pa->%pa iova:%pa",
						buf->use_pool,
						&buf->pa_base, &pa_base, &buf->iova_base);
			} else
				cmdq_err("cannot get dev:%p domain", pkt->dev);
		}

		buf->alloc_time = 0;
		if (!buf->va_base) {
			kfree(buf);
			return;
		}

		if (buf->use_pool) {
			if (pkt->cur_pool.pool)
				cmdq_mbox_pool_free_impl(pkt->cur_pool.pool,
					buf->va_base,
					CMDQ_BUF_ADDR(buf),
					pkt->cur_pool.cnt);
			else {
				cmdq_err("free pool:%s dev:%#lx pa:%pa iova:%pa cl:%p",
					buf->use_pool ? "true" : "false",
					(unsigned long)pkt->dev,
					&buf->pa_base, &buf->iova_base, cl);
				cmdq_mbox_pool_free(cl, buf->va_base,
					CMDQ_BUF_ADDR(buf));
			}
		} else if (pkt->dev)
			cmdq_mbox_buf_free_dev(
				pkt->share_dev, buf->va_base,
				CMDQ_BUF_ADDR(buf));
		kfree(buf);
		cmdq_set_buffer_size(cl, false);
	}
}

s32 cmdq_pkt_add_cmd_buffer(struct cmdq_pkt *pkt)
{
	s32 status = 0;
	struct cmdq_pkt_buffer *buf, *prev;
	u64 *prev_va;

	if (list_empty(&pkt->buf))
		prev = NULL;
	else
		prev = list_last_entry(&pkt->buf, typeof(*prev), list_entry);

	buf = cmdq_pkt_alloc_buf(pkt);
	if (IS_ERR(buf)) {
		status = PTR_ERR(buf);
		cmdq_err("alloc singe buffer fail status:%d pkt:0x%p",
			status, pkt);
		return status;
	}

	/* if no previous buffer, success return */
	if (!prev)
		return 0;

	/* copy last instruction to head of new buffer and
	 * use jump to replace
	 */
	prev_va = (u64 *)(prev->va_base + CMDQ_CMD_BUFFER_SIZE -
		CMDQ_INST_SIZE);
	*((u64 *)buf->va_base) = *prev_va;
#if IS_ENABLED(CONFIG_MTK_CMDQ_DEBUG)
	*((u64 *)buf->va_base + 1) = CMDQ_BUF_INIT_VAL;
#endif
	/* insert jump to jump start of new buffer.
	 * jump to absolute addr
	 */
	*prev_va = ((u64)(CMDQ_CODE_JUMP << 24 | 1) << 32) |
		(CMDQ_REG_SHIFT_ADDR(CMDQ_BUF_ADDR(buf)) & 0xFFFFFFFF);
	if (*prev_va != (((u64)(CMDQ_CODE_JUMP << 24 | 1) << 32) |
		(CMDQ_REG_SHIFT_ADDR(CMDQ_BUF_ADDR(buf)) & 0xFFFFFFFF))) {
		cmdq_err("insert jump fail, prev inst:%#llx", *prev_va);
		BUG_ON(1);
	}

	/* decrease available size since insert 1 jump */
	pkt->avail_buf_size -= CMDQ_INST_SIZE;
	/* +1 for jump instruction */
	pkt->cmd_buf_size += CMDQ_INST_SIZE;

	return 0;
}
EXPORT_SYMBOL(cmdq_pkt_add_cmd_buffer);

void cmdq_mbox_destroy(struct cmdq_client *client)
{
	if (client->backup_va)
		cmdq_mbox_buf_free(client, client->backup_va, client->backup_pa);
	mbox_free_channel(client->chan);
	kfree(client->cl_priv);
	kfree(client);
}
EXPORT_SYMBOL(cmdq_mbox_destroy);

struct cmdq_pkt *cmdq_pkt_create_with_id(struct cmdq_client *client, u32 debug_id)
{
	struct cmdq_pkt *pkt;

	pkt = cmdq_pkt_create(client);

	if(IS_ERR(pkt))
		return pkt;

	pkt->debug_id = debug_id;
	cmdq_set_thrd_pkt_id(pkt, true);
	return pkt;
}
EXPORT_SYMBOL(cmdq_pkt_create_with_id);

struct cmdq_pkt *cmdq_pkt_create(struct cmdq_client *client)
{
	struct cmdq_pkt *pkt;
#if IS_ENABLED(CONFIG_MTK_CMDQ_DEBUG)
	u64 start = sched_clock(), end[3];
	u32 end_cnt = 0;
#endif

	pkt = kzalloc(sizeof(*pkt), GFP_KERNEL);
	if (!pkt)
		return ERR_PTR(-ENOMEM);
	pkt->debug_id = UINT_MAX;
#if IS_ENABLED(CONFIG_MTK_CMDQ_DEBUG)
	end[end_cnt++] = sched_clock();
#endif
	INIT_LIST_HEAD(&pkt->buf);
	init_completion(&pkt->cmplt);
	pkt->cl = (void *)client;
	pkt->no_irq = false;
	if (client) {
		pkt->dev = client->chan->mbox->dev;
		pkt->share_dev = client->share_dev;
	}
#if IS_ENABLED(CONFIG_MTK_CMDQ_DEBUG)
	end[end_cnt++] = sched_clock();
#endif
#if IS_ENABLED(CONFIG_MTK_CMDQ_MBOX_EXT)
	if (client)
		cmdq_pkt_perf_begin(pkt);

	if (!cmdq_util_is_prebuilt_client(client))
		cmdq_pkt_hw_trace(pkt, 0);
#endif
	pkt->task_alive = true;
	pkt->create_instr_cnt = pkt->cmd_buf_size / CMDQ_INST_SIZE;
#if IS_ENABLED(CONFIG_MTK_CMDQ_DEBUG)
	end[end_cnt] = sched_clock();

	if (end[end_cnt] - start >= 50000000) /* 50ms */
		cmdq_msg("%s cost time %llu alloc:%llu init:%llu cmd:%llu",
			__func__, end[end_cnt] - start, end[0] - start,
			end[1] - end[0], end[2] - end[1]);
#endif

	cmdq_set_pkt_size(client, true);
	return pkt;
}
EXPORT_SYMBOL(cmdq_pkt_create);

static void cmdq_pkt_destroy_work(struct work_struct *work_item)
{
	struct cmdq_pkt *pkt =
		container_of(work_item, struct cmdq_pkt, destroy_work);
#if IS_ENABLED(CONFIG_MTK_CMDQ_DEBUG)
	u64 start = sched_clock(), end[3];
	u32 end_cnt = 0;
#endif
	struct cmdq_client *cl = pkt->cl;
	s32 hwid, thd;

	if (cl) {
		hwid = (s32)cmdq_util_get_hw_id((u32)cmdq_mbox_get_base_pa(cl->chan));
		thd = (s32)cmdq_mbox_chan_id(cl->chan);
		cmdq_trace_begin("%s hwid:%d thrd:%d pkt:%p",
			__func__, hwid, thd, pkt);
	} else
		cmdq_trace_begin("%s pkt:%p", __func__, pkt);
	cmdq_log("%s pkt:%p ", __func__, pkt);
#if IS_ENABLED(CONFIG_MTK_CMDQ_DEBUG)
	end[end_cnt++] = sched_clock();
#endif
	cmdq_pkt_free_buf(pkt);
#if IS_ENABLED(CONFIG_MTK_CMDQ_DEBUG)
	end[end_cnt++] = sched_clock();
#endif
	kfree(pkt->flush_item);
#if IS_ENABLED(CONFIG_MTK_CMDQ_MBOX_EXT)
#ifdef CMDQ_SECURE_SUPPORT
	if (pkt->sec_data)
		cmdq_sec_helper->sec_pkt_free_data_fp(pkt);
#endif
#endif
	kfree(pkt);
#if IS_ENABLED(CONFIG_MTK_CMDQ_DEBUG)
	end[end_cnt] = sched_clock();
	if (end[end_cnt] - start >= 4000000) /* 4 ms*/
		cmdq_msg("%s cost time %llu trace:%llu free buf:%llu kfree:%llu ms",
			__func__, end[end_cnt] - start, end[0] - start,
			end[1] - end[0], end[2] - end[1]);
#endif
	cmdq_trace_end();
}

void cmdq_pkt_destroy_no_wq(struct cmdq_pkt *pkt)
{
	struct cmdq_client *client = pkt->cl;
#if IS_ENABLED(CONFIG_MTK_CMDQ_DEBUG)
	u64 start = sched_clock(), end[3];
	u32 end_cnt = 0;
#endif
	s32 hwid, thd;

	cmdq_log("%s pkt:%p ", __func__, pkt);
	if (client) {
		mutex_lock(&client->chan_mutex);
		hwid = (s32)cmdq_util_get_hw_id((u32)cmdq_mbox_get_base_pa(client->chan));
		thd = (s32)cmdq_mbox_chan_id(client->chan);
		cmdq_trace_begin("%s hwid:%d thrd:%d pkt:%p",
			__func__, hwid, thd, pkt);
	} else
		cmdq_trace_begin("%s pkt:%p", __func__, pkt);
#if IS_ENABLED(CONFIG_MTK_CMDQ_MBOX_EXT)
	if (cmdq_pkt_is_exec(pkt)) {
		if (client && client->chan) {
			s32 thread_id = cmdq_mbox_chan_id(client->chan);

			cmdq_err("invalid destroy, pkt:%p thd:%d", pkt, thread_id);
		} else
			cmdq_err("invalid pkt_destroy, pkt:0x%p", pkt);
		dump_stack();
	}
#endif
	if (client)
		mutex_unlock(&client->chan_mutex);

	pkt->task_alive = false;
	cmdq_set_thrd_pkt_id(pkt, false);
#if IS_ENABLED(CONFIG_MTK_CMDQ_DEBUG)
	end[end_cnt++] = sched_clock();
#endif
	cmdq_pkt_free_buf(pkt);
#if IS_ENABLED(CONFIG_MTK_CMDQ_DEBUG)
	end[end_cnt++] = sched_clock();
#endif
	kfree(pkt->flush_item);
#if IS_ENABLED(CONFIG_MTK_CMDQ_MBOX_EXT)
#ifdef CMDQ_SECURE_SUPPORT
	if (pkt->sec_data)
		cmdq_sec_helper->sec_pkt_free_data_fp(pkt);
#endif
#endif
	kfree(pkt);
	cmdq_set_pkt_size(client, false);
#if IS_ENABLED(CONFIG_MTK_CMDQ_DEBUG)
	end[end_cnt] = sched_clock();

	if (end[end_cnt] - start >= 4000000) /* 4 ms*/
		cmdq_msg("%s cost time %llu trace:%llu free buf:%llu kfree:%llu ms",
			__func__, end[end_cnt] - start, end[0] - start,
			end[1] - end[0], end[2] - end[1]);
#endif
	cmdq_trace_end();
}
EXPORT_SYMBOL(cmdq_pkt_destroy_no_wq);

void cmdq_pkt_destroy(struct cmdq_pkt *pkt)
{
	struct cmdq_client *client = pkt->cl;
	u64 start = sched_clock(), diff;

#if IS_ENABLED(CONFIG_VIRTIO_CMDQ)
	if (client && is_virtio_client(client))
		virtio_cmdq_pkt_destroy(pkt);
#elif IS_ENABLED(CONFIG_VHOST_CMDQ)
	if (client && client->is_virtio)
		vhost_cmdq_platform->vhost_cmdq_pkt_destroy(pkt);
#endif

	cmdq_log("%s pkt:%p ", __func__, pkt);
	if (client)
		mutex_lock(&client->chan_mutex);
#if IS_ENABLED(CONFIG_MTK_CMDQ_MBOX_EXT)
	if (cmdq_pkt_is_exec(pkt)) {
		if (client && client->chan) {
			s32 thread_id = cmdq_mbox_chan_id(client->chan);

			cmdq_err("invalid destroy, pkt:%p thd:%d", pkt, thread_id);
		} else
			cmdq_err("invalid pkt_destroy, pkt:0x%p", pkt);
		dump_stack();
	}
#endif
	if (client)
		mutex_unlock(&client->chan_mutex);

	pkt->task_alive = false;
	cmdq_set_thrd_pkt_id(pkt, false);

	INIT_WORK(&pkt->destroy_work, cmdq_pkt_destroy_work);
	queue_work(cmdq_pkt_destroy_wq, &pkt->destroy_work);

	diff = sched_clock() - start;
	if (diff >= 4000000) /* 4 ms*/
		cmdq_msg("%s cost time %llu ms ", __func__, div_u64(diff, 1000000));

	cmdq_set_pkt_size(client, false);
}
EXPORT_SYMBOL(cmdq_pkt_destroy);

size_t cmdq_pkt_get_curr_offset(struct cmdq_pkt *pkt)
{
	pkt->write_addr_high = 0;
	return pkt->cmd_buf_size - CMDQ_INST_SIZE;
}
EXPORT_SYMBOL(cmdq_pkt_get_curr_offset);

u64 *cmdq_pkt_get_va_by_offset(struct cmdq_pkt *pkt, size_t offset)
{
	size_t offset_remaind = offset;
	struct cmdq_pkt_buffer *buf;

	pkt->write_addr_high = 0;
	list_for_each_entry(buf, &pkt->buf, list_entry) {
		if (offset_remaind >= CMDQ_CMD_BUFFER_SIZE) {
			offset_remaind -= CMDQ_CMD_BUFFER_SIZE;
			continue;
		}
		return (u64 *)(buf->va_base + offset_remaind);
	}

	return NULL;
}
EXPORT_SYMBOL(cmdq_pkt_get_va_by_offset);

dma_addr_t cmdq_pkt_get_pa_by_offset(struct cmdq_pkt *pkt, u32 offset)
{
	u32 offset_remaind = offset;
	struct cmdq_pkt_buffer *buf;

	pkt->write_addr_high = 0;

	list_for_each_entry(buf, &pkt->buf, list_entry) {
		if (offset_remaind >= CMDQ_CMD_BUFFER_SIZE) {
			offset_remaind -= CMDQ_CMD_BUFFER_SIZE;
			continue;
		}

		return CMDQ_BUF_ADDR(buf) + offset_remaind;
	}

	return 0;
}
EXPORT_SYMBOL(cmdq_pkt_get_pa_by_offset);

dma_addr_t cmdq_pkt_get_curr_buf_pa(struct cmdq_pkt *pkt)
{
	struct cmdq_pkt_buffer *buf;

	pkt->write_addr_high = 0;
	if (unlikely(!pkt->avail_buf_size))
		if (cmdq_pkt_add_cmd_buffer(pkt) < 0)
			return -ENOMEM;

	buf = list_last_entry(&pkt->buf, typeof(*buf), list_entry);

	return CMDQ_BUF_ADDR(buf) + CMDQ_CMD_BUFFER_SIZE - pkt->avail_buf_size;
}
EXPORT_SYMBOL(cmdq_pkt_get_curr_buf_pa);

void *cmdq_pkt_get_curr_buf_va(struct cmdq_pkt *pkt)
{
	struct cmdq_pkt_buffer *buf;

	pkt->write_addr_high = 0;
	if (unlikely(!pkt->avail_buf_size))
		if (cmdq_pkt_add_cmd_buffer(pkt) < 0)
			return NULL;

	buf = list_last_entry(&pkt->buf, typeof(*buf), list_entry);

	return buf->va_base + CMDQ_CMD_BUFFER_SIZE - pkt->avail_buf_size;
}
EXPORT_SYMBOL(cmdq_pkt_get_curr_buf_va);

static bool cmdq_pkt_is_finalized(struct cmdq_pkt *pkt)
{
	u64 *expect_eoc;

	if (pkt->cmd_buf_size < CMDQ_INST_SIZE * 2)
		return false;

	expect_eoc = cmdq_pkt_get_va_by_offset(pkt,
		pkt->cmd_buf_size - CMDQ_INST_SIZE * 2);
	if (((struct cmdq_instruction *)expect_eoc)->op == CMDQ_CODE_JUMP)
		expect_eoc = cmdq_pkt_get_va_by_offset(pkt,
			pkt->cmd_buf_size - CMDQ_INST_SIZE * 3);
	if (expect_eoc && (*expect_eoc & CMDQ_EOC_MASK) == CMDQ_EOC_CMD)
		return true;

	return false;
}

static void cmdq_pkt_instr_encoder(void *buf, u16 arg_c, u16 arg_b,
	u16 arg_a, u8 s_op, u8 arg_c_type, u8 arg_b_type, u8 arg_a_type, u8 op)
{
	struct cmdq_instruction *cmdq_inst;

	cmdq_inst = buf;
	cmdq_inst->op = op;
	cmdq_inst->arg_a_type = arg_a_type;
	cmdq_inst->arg_b_type = arg_b_type;
	cmdq_inst->arg_c_type = arg_c_type;
	cmdq_inst->s_op = s_op;
	cmdq_inst->arg_a = arg_a;
	cmdq_inst->arg_b = arg_b;
	cmdq_inst->arg_c = arg_c;
	if (cmdq_inst->op >> 4 == 7 || cmdq_inst->op != op) {
		cmdq_err("cmdq_inst->op:%#x, op::%#x", cmdq_inst->op, op);
		dump_stack();
	}
}

s32 cmdq_pkt_append_command(struct cmdq_pkt *pkt, u16 arg_c, u16 arg_b,
	u16 arg_a, u8 s_op, u8 arg_c_type, u8 arg_b_type, u8 arg_a_type,
	enum cmdq_code code)
{
	struct cmdq_pkt_buffer *buf;
	void *va;

	if (!pkt)
		return -EINVAL;

	if (unlikely(!pkt->avail_buf_size)) {
		if (cmdq_pkt_add_cmd_buffer(pkt) < 0)
			return -ENOMEM;
	}

	buf = list_last_entry(&pkt->buf, typeof(*buf), list_entry);
	va = buf->va_base + CMDQ_CMD_BUFFER_SIZE - pkt->avail_buf_size;
#if IS_ENABLED(CONFIG_MTK_CMDQ_DEBUG)
	if (pkt->avail_buf_size >= CMDQ_CMD_BUFFER_SIZE - CMDQ_INST_SIZE &&
		!buf->copy && *((u64 *)va) != CMDQ_BUF_INIT_VAL) {
		struct cmdq_client *client = pkt->cl;

		cmdq_util_err(
			"%s: pkt:%p avail:%lu va:%p iova:%pa pa:%pa alloc_time:%llu va:%p inst:%#llx",
			__func__, pkt, (unsigned long)pkt->avail_buf_size, buf->va_base,
			&buf->iova_base, &buf->pa_base, buf->alloc_time,
			va, *((u64 *)va));

		if (pkt->cl)
			cmdq_mbox_check_buffer(client->chan, buf);
	}
#endif
	cmdq_pkt_instr_encoder(va, arg_c, arg_b, arg_a, s_op, arg_c_type,
		arg_b_type, arg_a_type, code);
	pkt->cmd_buf_size += CMDQ_INST_SIZE;
	pkt->avail_buf_size -= CMDQ_INST_SIZE;

	if (unlikely(!pkt->avail_buf_size)) {
		if (cmdq_pkt_add_cmd_buffer(pkt) < 0)
			return -ENOMEM;
	}

	return 0;
}
EXPORT_SYMBOL(cmdq_pkt_append_command);

static s32 cmdq_pkt_assign_addr_high_command(struct cmdq_pkt *pkt, u32 value)
{
	s32 err = 0;

	if (unlikely(pkt->write_addr_high != value)) {
		err = cmdq_pkt_assign_command(pkt, CMDQ_SPR_FOR_TEMP, value);
		if (!err)
			pkt->write_addr_high = value;
	}
	return err;
}

s32 cmdq_pkt_move(struct cmdq_pkt *pkt, u16 reg_idx, u64 value)
{
	return cmdq_pkt_append_command(pkt, CMDQ_GET_ARG_C(value),
		CMDQ_GET_ARG_B(value), (u16)(value >> 32), reg_idx,
		0, 0, 0, CMDQ_CODE_MASK);
}
EXPORT_SYMBOL(cmdq_pkt_move);

s32 cmdq_pkt_read(struct cmdq_pkt *pkt, struct cmdq_base *clt_base,
	dma_addr_t src_addr, u16 dst_reg_idx)
{
	s8 subsys;

	if (!(CMDQ_GET_ADDR_H(src_addr))) {
		subsys = cmdq_subsys_base_to_id(clt_base, src_addr);
		if (subsys >= 0)
			return cmdq_pkt_read_reg(pkt,
				clt_base->subsys[subsys].id,
				CMDQ_GET_REG_OFFSET(src_addr), dst_reg_idx);
	}

	return cmdq_pkt_read_addr(pkt, src_addr, dst_reg_idx);
}
EXPORT_SYMBOL(cmdq_pkt_read);

s32 cmdq_pkt_read_reg(struct cmdq_pkt *pkt, u8 subsys, u16 offset,
	u16 dst_reg_idx)
{
	return cmdq_pkt_append_command(pkt, 0, offset, dst_reg_idx, subsys,
		CMDQ_IMMEDIATE_VALUE, CMDQ_IMMEDIATE_VALUE, CMDQ_REG_TYPE,
		CMDQ_CODE_READ_S);
}
EXPORT_SYMBOL(cmdq_pkt_read_reg);

s32 cmdq_pkt_read_addr(struct cmdq_pkt *pkt, dma_addr_t addr, u16 dst_reg_idx)
{
	s32 err;
	const u16 src_reg_idx = CMDQ_SPR_FOR_TEMP;

	err = cmdq_pkt_assign_addr_high_command(pkt, CMDQ_GET_ADDR_HIGH(addr));

	if (err != 0)
		return err;

	return cmdq_pkt_append_command(pkt, 0, CMDQ_GET_ADDR_LOW(addr),
		dst_reg_idx, src_reg_idx,
		CMDQ_IMMEDIATE_VALUE, CMDQ_IMMEDIATE_VALUE, CMDQ_REG_TYPE,
		CMDQ_CODE_READ_S);
}
EXPORT_SYMBOL(cmdq_pkt_read_addr);

s32 cmdq_pkt_write_reg(struct cmdq_pkt *pkt, u8 subsys,
	u16 offset, u16 src_reg_idx, u32 mask)
{
	int err = 0;
	enum cmdq_code op = CMDQ_CODE_WRITE_S;

	if (mask != 0xffffffff) {
		err = cmdq_pkt_append_command(pkt, CMDQ_GET_ARG_C(~mask),
			CMDQ_GET_ARG_B(~mask), 0, 0, 0, 0, 0, CMDQ_CODE_MASK);
		if (err != 0)
			return err;

		op = CMDQ_CODE_WRITE_S_W_MASK;
	}

	return cmdq_pkt_append_command(pkt, 0, src_reg_idx, offset, subsys,
		CMDQ_IMMEDIATE_VALUE, CMDQ_REG_TYPE, CMDQ_IMMEDIATE_VALUE, op);
}
EXPORT_SYMBOL(cmdq_pkt_write_reg);

s32 cmdq_pkt_write_value(struct cmdq_pkt *pkt, u8 subsys,
	u16 offset, u32 value, u32 mask)
{
	int err = 0;
	enum cmdq_code op = CMDQ_CODE_WRITE_S;

	if (mask != 0xffffffff) {
		err = cmdq_pkt_append_command(pkt, CMDQ_GET_ARG_C(~mask),
			CMDQ_GET_ARG_B(~mask), 0, 0, 0, 0, 0, CMDQ_CODE_MASK);
		if (err != 0)
			return err;

		op = CMDQ_CODE_WRITE_S_W_MASK;
	}

	return cmdq_pkt_append_command(pkt, CMDQ_GET_ARG_C(value),
		CMDQ_GET_ARG_B(value), offset, subsys,
		CMDQ_IMMEDIATE_VALUE, CMDQ_IMMEDIATE_VALUE,
		CMDQ_IMMEDIATE_VALUE, op);
}
EXPORT_SYMBOL(cmdq_pkt_write_value);

s32 cmdq_pkt_write_reg_addr(struct cmdq_pkt *pkt, dma_addr_t addr,
	u16 src_reg_idx, u32 mask)
{
	s32 err;
	const u16 dst_reg_idx = CMDQ_SPR_FOR_TEMP;

	err = cmdq_pkt_assign_addr_high_command(pkt, CMDQ_GET_ADDR_HIGH(addr));

	if (err != 0)
		return err;

	return cmdq_pkt_store_value_reg(pkt, dst_reg_idx,
		CMDQ_GET_ADDR_LOW(addr), src_reg_idx, mask);
}
EXPORT_SYMBOL(cmdq_pkt_write_reg_addr);

s32 cmdq_pkt_write_value_addr(struct cmdq_pkt *pkt, dma_addr_t addr,
	u32 value, u32 mask)
{
	s32 err;
	const u16 dst_reg_idx = CMDQ_SPR_FOR_TEMP;

	err = cmdq_pkt_assign_addr_high_command(pkt, CMDQ_GET_ADDR_HIGH(addr));

	if (err != 0)
		return err;

	err = cmdq_pkt_store_value(pkt, dst_reg_idx, CMDQ_GET_ADDR_LOW(addr),
		value, mask);

	return err;
}
EXPORT_SYMBOL(cmdq_pkt_write_value_addr);

s32 cmdq_pkt_assign_command_reuse(struct cmdq_pkt *pkt, u16 reg_idx, u32 value,
	struct cmdq_reuse *reuse)
{
	s32 err;

	err = cmdq_pkt_assign_command(pkt, reg_idx, value);

	reuse->op = CMDQ_CODE_LOGIC;
	reuse->va = cmdq_pkt_get_curr_buf_va(pkt) - CMDQ_INST_SIZE;
	reuse->offset = pkt->cmd_buf_size - CMDQ_INST_SIZE;
	return err;
}
EXPORT_SYMBOL(cmdq_pkt_assign_command_reuse);

s32 cmdq_pkt_write_reg_addr_reuse(struct cmdq_pkt *pkt, dma_addr_t addr,
	u16 src_reg_idx, u32 mask, struct cmdq_reuse *reuse)
{
	s32 err;
	const u16 dst_reg_idx = CMDQ_SPR_FOR_TEMP;

	err = cmdq_pkt_assign_addr_high_command(pkt, CMDQ_GET_ADDR_HIGH(addr));

	if (err != 0)
		return err;

	err = cmdq_pkt_store_value_reg(pkt, dst_reg_idx,
		CMDQ_GET_ADDR_LOW(addr), src_reg_idx, mask);

	reuse->op = CMDQ_CODE_WRITE_S;
	reuse->va = cmdq_pkt_get_curr_buf_va(pkt) - CMDQ_INST_SIZE;
	reuse->offset = pkt->cmd_buf_size - CMDQ_INST_SIZE;
	return err;
}
EXPORT_SYMBOL(cmdq_pkt_write_reg_addr_reuse);

s32 cmdq_pkt_write_value_addr_reuse(struct cmdq_pkt *pkt, dma_addr_t addr,
	u32 value, u32 mask, struct cmdq_reuse *reuse)
{
	s32 err;
	const u16 dst_reg_idx = CMDQ_SPR_FOR_TEMP;

	err = cmdq_pkt_assign_addr_high_command(pkt, CMDQ_GET_ADDR_HIGH(addr));

	if (err != 0)
		return err;

	err = cmdq_pkt_store_value(pkt, dst_reg_idx, CMDQ_GET_ADDR_LOW(addr),
		value, mask);

	reuse->op = CMDQ_CODE_WRITE_S;
	reuse->va = cmdq_pkt_get_curr_buf_va(pkt) - CMDQ_INST_SIZE;
	reuse->offset = pkt->cmd_buf_size - CMDQ_INST_SIZE;
	cmdq_log("%s: curr_buf_va:%p idx:%u",
		__func__, reuse->va, reuse->offset);
	return err;
}
EXPORT_SYMBOL(cmdq_pkt_write_value_addr_reuse);

s32 cmdq_pkt_backup(struct cmdq_pkt *pkt, dma_addr_t addr, struct cmdq_backup *backup)
{
	struct cmdq_client *cl = pkt->cl;
	dma_addr_t pa_addr;

	if (unlikely(!cl)) {
		cmdq_err("client is NULL");
		return -EINVAL;
	}

	if (!cl->backup_va && !cl->backup_pa) {
		cl->backup_va = cmdq_mbox_buf_alloc(cl, &cl->backup_pa);
		cmdq_msg("%s client va %p pa %pa", __func__, cl->backup_va, &cl->backup_pa);
	}

	if (unlikely(!cl->backup_va) || unlikely(!cl->backup_pa)) {
		cmdq_err("backup buffer unavailable");
		return -ENOMEM;
	}

	pa_addr = cl->backup_pa + cl->backup_idx * 4;
	backup->val_idx = cl->backup_idx++;
	if (cl->backup_idx >= CMDQ_BACKUP_CNT)
		cl->backup_idx = 0;

	/* read reg value to spr : CMDQ_THR_SPR_IDX2*/
	cmdq_pkt_read_addr(pkt, addr, CMDQ_THR_SPR_IDX2);

	/* write spr to dram pa */
	cmdq_pkt_write_indriect(pkt, NULL, pa_addr, CMDQ_THR_SPR_IDX2, U32_MAX);
	backup->inst_offset = pkt->cmd_buf_size - CMDQ_INST_SIZE;

	return 0;
}
EXPORT_SYMBOL(cmdq_pkt_backup);

s32 cmdq_pkt_backup_stamp(struct cmdq_pkt *pkt, struct cmdq_backup *backup)
{
	struct cmdq_client *cl = pkt->cl;
	dma_addr_t pa_addr;

	if (unlikely(!cl)) {
		cmdq_err("client is NULL");
		return -EINVAL;
	}

	if (!cl->backup_va && !cl->backup_pa) {
		cl->backup_va = cmdq_mbox_buf_alloc(cl, &cl->backup_pa);
		cmdq_msg("%s client va %p pa %pa", __func__, cl->backup_va, &cl->backup_pa);
	}

	if (unlikely(!cl->backup_va) || unlikely(!cl->backup_pa)) {
		cmdq_err("backup buffer unavailable");
		return -ENOMEM;
	}

	pa_addr = cl->backup_pa + cl->backup_idx * 4;
	backup->val_idx = cl->backup_idx++;
	if (cl->backup_idx >= CMDQ_BACKUP_CNT)
		cl->backup_idx = 0;

	/* TPR value to dram pa */
	cmdq_pkt_write_indriect(pkt, NULL, pa_addr, CMDQ_TPR_ID, U32_MAX);
	backup->inst_offset = pkt->cmd_buf_size - CMDQ_INST_SIZE;

	return 0;
}
EXPORT_SYMBOL(cmdq_pkt_backup_stamp);

s32 cmdq_pkt_backup_update(struct cmdq_pkt *pkt, struct cmdq_backup *backup)
{
	struct cmdq_client *cl = pkt->cl;
	dma_addr_t pa_addr;
	u32 *inst;

	if (unlikely(!cl)) {
		cmdq_err("client is NULL");
		return -EINVAL;
	}

	if (unlikely(!cl->backup_va) || unlikely(!cl->backup_pa)) {
		cmdq_err("backup buffer unavailable");
		return -EINVAL;
	}

	pa_addr = cl->backup_pa + cl->backup_idx * 4;
	backup->val_idx = cl->backup_idx++;
	if (cl->backup_idx >= CMDQ_BACKUP_CNT)
		cl->backup_idx = 0;

	inst = (u32 *)cmdq_pkt_get_va_by_offset(pkt, backup->inst_offset);
	inst[1] = (inst[1] & 0xffff0000) | CMDQ_GET_ADDR_LOW(pa_addr);

	return 0;
}
EXPORT_SYMBOL(cmdq_pkt_backup_update);

u32 cmdq_pkt_backup_get(struct cmdq_pkt *pkt, struct cmdq_backup *backup)
{
	struct cmdq_client *cl = pkt->cl;
	u32 *va, value;

	if (unlikely(!cl)) {
		cmdq_err("client is NULL");
		return -EINVAL;
	}

	if (unlikely(!cl->backup_va) || unlikely(!cl->backup_pa)) {
		cmdq_err("backup buffer unavailabl");
		return -EINVAL;
	}

	va = cl->backup_va + backup->val_idx;
	value = readl(va);

	return value;
}
EXPORT_SYMBOL(cmdq_pkt_backup_get);

void cmdq_pkt_reuse_jump(struct cmdq_pkt *pkt, struct cmdq_reuse *reuse)
{
	struct cmdq_instruction *inst;
	dma_addr_t cmd_pa;
	u32 shift_pa;

	cmd_pa = cmdq_pkt_get_pa_by_offset(pkt, reuse->val);
	shift_pa = CMDQ_REG_SHIFT_ADDR(cmd_pa);
	inst = (struct cmdq_instruction *)cmdq_pkt_get_va_by_offset(
		pkt, reuse->offset);
	inst->arg_b = CMDQ_GET_ARG_B(shift_pa);
	inst->arg_c = CMDQ_GET_ARG_C(shift_pa);
}

void cmdq_pkt_reuse_value(struct cmdq_pkt *pkt, struct cmdq_reuse *reuse)
{
	*reuse->va = (*reuse->va & GENMASK_ULL(63, 32)) | reuse->val;
}
EXPORT_SYMBOL(cmdq_pkt_reuse_value);

void cmdq_pkt_reuse_buf_va(struct cmdq_pkt *pkt, struct cmdq_reuse *reuse,
	const u32 count)
{
	s32 i;

	for (i = 0; i < count; i++) {
		switch (reuse[i].op) {
		case 0:
			/* for case client update itself, skip it */
			break;
		case CMDQ_CODE_READ:
		case CMDQ_CODE_MOVE:
		case CMDQ_CODE_WRITE:
		case CMDQ_CODE_POLL:
		case CMDQ_CODE_WFE:
		case CMDQ_CODE_SET_TOKEN:
		case CMDQ_CODE_WAIT_NO_CLEAR:
		case CMDQ_CODE_CLEAR_TOKEN:
		case CMDQ_CODE_READ_S:
		case CMDQ_CODE_WRITE_S:
		case CMDQ_CODE_WRITE_S_W_MASK:
		case CMDQ_CODE_LOGIC:
			cmdq_pkt_reuse_value(pkt, &reuse[i]);
			break;
		case CMDQ_CODE_JUMP:
		case CMDQ_CODE_JUMP_C_ABSOLUTE:
		case CMDQ_CODE_JUMP_C_RELATIVE:
			cmdq_pkt_reuse_jump(pkt, &reuse[i]);
			break;
		default:
			cmdq_err("idx:%d, invalid op:%#x, va:%p val:%#x inst:%#llx",
			i, reuse[i].op, reuse[i].va, reuse[i].val, *reuse[i].va);
			break;
		}
		cmdq_log("%s:curr reuse:%d op:%#x va:%p val:%#x inst:%#llx",
			__func__, i, reuse[i].op, reuse[i].va, reuse[i].val, *reuse[i].va);
	}
}
EXPORT_SYMBOL(cmdq_pkt_reuse_buf_va);

void cmdq_pkt_reuse_poll(struct cmdq_pkt *pkt, struct cmdq_poll_reuse *poll_reuse)
{
	cmdq_pkt_reuse_buf_va(pkt, &poll_reuse->jump_to_begin, 1);
	cmdq_pkt_reuse_buf_va(pkt, &poll_reuse->jump_to_end, 1);
	if (poll_reuse->sleep_jump_to_end.op)
		cmdq_pkt_reuse_buf_va(pkt, &poll_reuse->sleep_jump_to_end, 1);
}
EXPORT_SYMBOL(cmdq_pkt_reuse_poll);

void cmdq_reuse_refresh(struct cmdq_pkt *pkt, struct cmdq_reuse *reuse, u32 cnt)
{
	struct cmdq_pkt_buffer *buf;
	u32 reuse_idx = 0;
	u32 cur_off = 0;
	u32 next_off;

	list_for_each_entry(buf, &pkt->buf, list_entry) {
		next_off = cur_off + CMDQ_CMD_BUFFER_SIZE;
		while (reuse_idx < cnt &&
			reuse[reuse_idx].offset >= cur_off &&
			reuse[reuse_idx].offset < next_off) {
			reuse[reuse_idx].va = (u64 *)(buf->va_base +
				(reuse[reuse_idx].offset % CMDQ_CMD_BUFFER_SIZE));
			reuse_idx++;
		}

		if (reuse_idx >= cnt)
			break;

		cur_off = next_off;
	}
}
EXPORT_SYMBOL(cmdq_reuse_refresh);

s32 cmdq_pkt_copy(struct cmdq_pkt *dst, struct cmdq_pkt *src)
{
	struct list_head entry;
	struct cmdq_pkt_buffer *buf, *new, *tmp;
	struct completion cmplt;
	struct device *dev = dst->dev;
	void *cl = dst->cl;
	u64 *va;
	u32 cmd_size, copy_size;
	int reduce_size = 0, last_page = 0;
	u32 debug_id = dst->debug_id;
	u32 buf_cnt;

	cmdq_pkt_free_buf(dst);
	cmdq_set_thrd_pkt_buf(cl, dst->debug_id, false);
	buf_cnt = dst->buf_cnt;
	INIT_LIST_HEAD(&dst->buf);

	memcpy(&entry, &dst->buf, sizeof(entry));
	memcpy(&cmplt, &dst->cmplt, sizeof(cmplt));
	memcpy(dst, src, sizeof(*dst));
	memcpy(&dst->cmplt, &cmplt, sizeof(cmplt));
	memcpy(&dst->buf, &entry, sizeof(entry));
	dst->debug_id = debug_id;
	dst->buf_cnt = buf_cnt;
	dst->flush_item = NULL;
	cmd_size = src->cmd_buf_size;

	if (cmdq_pkt_is_finalized(src)) {
		reduce_size += 2 * CMDQ_INST_SIZE;
		if (append_by_event && !src->sec_data)
			reduce_size += 2 * CMDQ_INST_SIZE;
#if IS_ENABLED(CONFIG_MTK_CMDQ_MBOX_EXT)
		if (cmdq_util_helper->is_feature_en(CMDQ_LOG_FEAT_PERF))
			reduce_size += 2 * CMDQ_INST_SIZE;
#endif
	}
	if (cmd_size)
		last_page = cmd_size % CMDQ_CMD_BUFFER_SIZE;
	if ((last_page > 0) && ((last_page - reduce_size) <= 0))
		reduce_size += CMDQ_INST_SIZE;
	cmd_size -= reduce_size;

	/* copy buf */
	list_for_each_entry(buf, &src->buf, list_entry) {
		new = cmdq_pkt_alloc_buf(dst);
		if (IS_ERR(new)) {
			cmdq_err("alloc singe buffer fail status:%ld pkt:0x%p",
				PTR_ERR(new), dst);
			return PTR_ERR(new);
		}
		copy_size = cmd_size > CMDQ_CMD_BUFFER_SIZE ?
			CMDQ_CMD_BUFFER_SIZE : cmd_size;
		memcpy(new->va_base, buf->va_base, copy_size);
		cmd_size -= copy_size;
		new->copy = true;

#if IS_ENABLED(CONFIG_MTK_CMDQ_MBOX_EXT)
		if (new == list_first_entry(&dst->buf, typeof(*buf), list_entry) &&
			cmdq_util_helper->is_feature_en(CMDQ_LOG_FEAT_PERF)) {

			dst->avail_buf_size = CMDQ_CMD_BUFFER_SIZE;
			dst->cmd_buf_size = 0;
			dst->buf_size = CMDQ_CMD_BUFFER_SIZE;
			cmdq_pkt_perf_begin(dst);
		}
#endif
		/* if last buf contains only perf or eoc, cmd_size goes to 0 so
		 * stops copy here.
		 */
		if (!cmd_size)
			break;
	}

	list_for_each_entry_safe(buf, tmp, &dst->buf, list_entry) {
		cmdq_log("%s: buf:%p va:%p pa:%pa iova_base:%pa tmp:%p",
			__func__, buf, buf->va_base, &buf->pa_base, &buf->iova_base, tmp);

		if (!list_is_last(&buf->list_entry, &dst->buf)) {
			va = buf->va_base + CMDQ_CMD_BUFFER_SIZE -
				CMDQ_INST_SIZE;
			*va = ((u64)(CMDQ_CODE_JUMP << 24 | 1) << 32) |
				CMDQ_REG_SHIFT_ADDR(CMDQ_BUF_ADDR(tmp));
			cmdq_log("%s: va:%p inst:%#llx", __func__, va, *va);
		}
	}

	dst->pause_offset = src->pause_offset;
	dst->avail_buf_size = src->avail_buf_size + reduce_size;
	dst->cmd_buf_size = src->cmd_buf_size - reduce_size;
	dst->buf_size = src->buf_size;
	if (dst->avail_buf_size > CMDQ_CMD_BUFFER_SIZE) {
		dst->avail_buf_size -= CMDQ_CMD_BUFFER_SIZE;
		dst->buf_size -= CMDQ_CMD_BUFFER_SIZE;
	}

	dst->task_alloc = false;
	dst->rec_irq = 0;
	cmdq_pkt_refinalize(dst);

	dst->cl = cl;
	dst->dev = dev;
	return 0;
}
EXPORT_SYMBOL(cmdq_pkt_copy);


s32 cmdq_pkt_store_value(struct cmdq_pkt *pkt, u16 indirect_dst_reg_idx,
	u16 dst_addr_low, u32 value, u32 mask)
{
	int err = 0;
	enum cmdq_code op = CMDQ_CODE_WRITE_S;

	if (mask != 0xffffffff) {
		err = cmdq_pkt_append_command(pkt, CMDQ_GET_ARG_C(~mask),
			CMDQ_GET_ARG_B(~mask), 0, 0, 0, 0, 0, CMDQ_CODE_MASK);
		if (err != 0)
			return err;

		op = CMDQ_CODE_WRITE_S_W_MASK;
	}

	return cmdq_pkt_append_command(pkt, CMDQ_GET_ARG_C(value),
		CMDQ_GET_ARG_B(value), dst_addr_low,
		indirect_dst_reg_idx, CMDQ_IMMEDIATE_VALUE,
		CMDQ_IMMEDIATE_VALUE, CMDQ_IMMEDIATE_VALUE, op);
}
EXPORT_SYMBOL(cmdq_pkt_store_value);

s32 cmdq_pkt_store_value_reg(struct cmdq_pkt *pkt, u16 indirect_dst_reg_idx,
	u16 dst_addr_low, u16 indirect_src_reg_idx, u32 mask)
{
	int err = 0;
	enum cmdq_code op = CMDQ_CODE_WRITE_S;

	if (mask != 0xffffffff) {
		err = cmdq_pkt_append_command(pkt, CMDQ_GET_ARG_C(~mask),
			CMDQ_GET_ARG_B(~mask), 0, 0, 0, 0, 0, CMDQ_CODE_MASK);
		if (err != 0)
			return err;

		op = CMDQ_CODE_WRITE_S_W_MASK;
	}

	if (dst_addr_low) {
		return cmdq_pkt_append_command(pkt, 0, indirect_src_reg_idx,
			dst_addr_low, indirect_dst_reg_idx,
			CMDQ_IMMEDIATE_VALUE, CMDQ_REG_TYPE,
			CMDQ_IMMEDIATE_VALUE, op);
	}

	return cmdq_pkt_append_command(pkt, 0,
		indirect_src_reg_idx, indirect_dst_reg_idx, 0,
		CMDQ_IMMEDIATE_VALUE, CMDQ_REG_TYPE, CMDQ_REG_TYPE, op);
}
EXPORT_SYMBOL(cmdq_pkt_store_value_reg);

s32 cmdq_pkt_store64_value_reg(struct cmdq_pkt *pkt,
	u16 indirect_dst_reg_idx, u16 indirect_src_reg_idx)
{
	return cmdq_pkt_append_command(pkt, indirect_src_reg_idx, 0,
		0, indirect_dst_reg_idx, CMDQ_IMMEDIATE_VALUE, CMDQ_REG_TYPE,
		CMDQ_REG_TYPE, CMDQ_CODE_WRITE);
}
EXPORT_SYMBOL(cmdq_pkt_store64_value_reg);

s32 cmdq_pkt_write_dummy(struct cmdq_pkt *pkt, dma_addr_t addr)
{
	s32 err;
	const u16 dst_reg_idx = CMDQ_SPR_FOR_TEMP;
	struct cmdq_client *cl = pkt->cl;
	u32 dummy_addr;

	if (!cl || !gce_mminfra)
		return 0;

	dummy_addr = (u32)cmdq_mbox_get_dummy_reg(cl->chan);
	if (addr > (dma_addr_t)gce_mminfra) {
		/* assign bit 47:16 to spr temp */
		err = cmdq_pkt_assign_addr_high_command(pkt, CMDQ_GET_ADDR_HIGH(dummy_addr));
		if (err != 0)
			return err;

		return cmdq_pkt_store_value(pkt, dst_reg_idx, CMDQ_GET_ADDR_LOW(dummy_addr),
			1, ~0);
	}
	return 0;
}
EXPORT_SYMBOL(cmdq_pkt_write_dummy);

s32 cmdq_pkt_write_indriect(struct cmdq_pkt *pkt, struct cmdq_base *clt_base,
	dma_addr_t addr, u16 src_reg_idx, u32 mask)
{
	const u32 base = CMDQ_GET_ADDR_H(addr) ? 0 : addr & 0xFFFF0000;
	s32 subsys;

	subsys = cmdq_subsys_base_to_id(clt_base, base);
	if (subsys >= 0) {
		return cmdq_pkt_write_reg(pkt, subsys,
			base, src_reg_idx, mask);
	}

	return cmdq_pkt_write_reg_addr(pkt, addr, src_reg_idx, mask);
}
EXPORT_SYMBOL(cmdq_pkt_write_indriect);

s32 cmdq_pkt_write_reg_indriect(struct cmdq_pkt *pkt, u16 addr_reg_idx,
	u16 src_reg_idx, u32 mask)
{
	enum cmdq_code op = CMDQ_CODE_WRITE_S;

	if (mask != U32_MAX) {
		int err = cmdq_pkt_append_command(pkt, CMDQ_GET_ARG_C(~mask),
			CMDQ_GET_ARG_B(~mask), 0, 0, 0, 0, 0, CMDQ_CODE_MASK);
		if (err != 0)
			return err;

		op = CMDQ_CODE_WRITE_S_W_MASK;
	}

	return cmdq_pkt_append_command(pkt, 0,
		src_reg_idx, addr_reg_idx, 0,
		CMDQ_IMMEDIATE_VALUE, CMDQ_REG_TYPE, CMDQ_REG_TYPE, op);
}
EXPORT_SYMBOL(cmdq_pkt_write_reg_indriect);

s32 cmdq_pkt_write(struct cmdq_pkt *pkt, struct cmdq_base *clt_base,
	dma_addr_t addr, u32 value, u32 mask)
{
	const u32 base = CMDQ_GET_ADDR_H(addr) ? 0 : addr & 0xFFFF0000;
	s32 subsys;

	subsys = cmdq_subsys_base_to_id(clt_base, base);
	if (subsys >= 0) {
		return cmdq_pkt_write_value(pkt, subsys,
			CMDQ_GET_REG_OFFSET(addr), value, mask);
	}

	return cmdq_pkt_write_value_addr(pkt, addr, value, mask);
}
EXPORT_SYMBOL(cmdq_pkt_write);

s32 cmdq_pkt_mem_move(struct cmdq_pkt *pkt, struct cmdq_base *clt_base,
	dma_addr_t src_addr, dma_addr_t dst_addr, u16 swap_reg_idx)
{
	s32 err;

	err = cmdq_pkt_read(pkt, clt_base, src_addr, swap_reg_idx);
	if (err != 0)
		return err;

	return cmdq_pkt_write_indriect(pkt, clt_base, dst_addr,
		swap_reg_idx, ~0);
}
EXPORT_SYMBOL(cmdq_pkt_mem_move);

s32 cmdq_pkt_assign_command(struct cmdq_pkt *pkt, u16 reg_idx, u32 value)
{
	return cmdq_pkt_append_command(pkt, CMDQ_GET_ARG_C(value),
		CMDQ_GET_ARG_B(value), reg_idx,
		CMDQ_LOGIC_ASSIGN, CMDQ_IMMEDIATE_VALUE,
		CMDQ_IMMEDIATE_VALUE, CMDQ_REG_TYPE,
		CMDQ_CODE_LOGIC);
}
EXPORT_SYMBOL(cmdq_pkt_assign_command);

s32 cmdq_pkt_logic_command(struct cmdq_pkt *pkt, enum CMDQ_LOGIC_ENUM s_op,
	u16 result_reg_idx,
	struct cmdq_operand *left_operand,
	struct cmdq_operand *right_operand)
{
	u32 left_idx_value;
	u32 right_idx_value;

	if (!left_operand || !right_operand)
		return -EINVAL;

	left_idx_value = CMDQ_OPERAND_GET_IDX_VALUE(left_operand);
	right_idx_value = CMDQ_OPERAND_GET_IDX_VALUE(right_operand);

	return cmdq_pkt_append_command(pkt, right_idx_value, left_idx_value,
		result_reg_idx, s_op, CMDQ_OPERAND_TYPE(right_operand),
		CMDQ_OPERAND_TYPE(left_operand), CMDQ_REG_TYPE,
		CMDQ_CODE_LOGIC);
}
EXPORT_SYMBOL(cmdq_pkt_logic_command);

s32 cmdq_pkt_jump(struct cmdq_pkt *pkt, s32 offset)
{
	s64 off = offset >> gce_shift_bit;

	return cmdq_pkt_append_command(pkt, CMDQ_GET_ARG_C(off),
		CMDQ_GET_ARG_B(off), 0, 0, 0, 0, 0, CMDQ_CODE_JUMP);
}
EXPORT_SYMBOL(cmdq_pkt_jump);

s32 cmdq_pkt_jump_addr(struct cmdq_pkt *pkt, dma_addr_t addr)
{
	dma_addr_t to_addr = CMDQ_REG_SHIFT_ADDR(addr);

	return cmdq_pkt_append_command(pkt, CMDQ_GET_ARG_C(to_addr),
		CMDQ_GET_ARG_B(to_addr), 1, 0, 0, 0, 0, CMDQ_CODE_JUMP);
}
EXPORT_SYMBOL(cmdq_pkt_jump_addr);

s32 cmdq_pkt_cond_jump(struct cmdq_pkt *pkt,
	u16 offset_reg_idx,
	struct cmdq_operand *left_operand,
	struct cmdq_operand *right_operand,
	enum CMDQ_CONDITION_ENUM condition_operator)
{
	u32 left_idx_value;
	u32 right_idx_value;

	if (!left_operand || !right_operand)
		return -EINVAL;

	left_idx_value = CMDQ_OPERAND_GET_IDX_VALUE(left_operand);
	right_idx_value = CMDQ_OPERAND_GET_IDX_VALUE(right_operand);
	pkt->write_addr_high = 0;

	return cmdq_pkt_append_command(pkt, right_idx_value, left_idx_value,
		offset_reg_idx, condition_operator,
		CMDQ_OPERAND_TYPE(right_operand),
		CMDQ_OPERAND_TYPE(left_operand),
		CMDQ_REG_TYPE, CMDQ_CODE_JUMP_C_RELATIVE);
}
EXPORT_SYMBOL(cmdq_pkt_cond_jump);

s32 cmdq_pkt_cond_jump_abs(struct cmdq_pkt *pkt,
	u16 addr_reg_idx,
	struct cmdq_operand *left_operand,
	struct cmdq_operand *right_operand,
	enum CMDQ_CONDITION_ENUM condition_operator)
{
	u16 left_idx_value;
	u16 right_idx_value;

	if (!left_operand || !right_operand)
		return -EINVAL;

	left_idx_value = CMDQ_OPERAND_GET_IDX_VALUE(left_operand);
	right_idx_value = CMDQ_OPERAND_GET_IDX_VALUE(right_operand);
	pkt->write_addr_high = 0;

	return cmdq_pkt_append_command(pkt, right_idx_value, left_idx_value,
		addr_reg_idx, condition_operator,
		CMDQ_OPERAND_TYPE(right_operand),
		CMDQ_OPERAND_TYPE(left_operand),
		CMDQ_REG_TYPE, CMDQ_CODE_JUMP_C_ABSOLUTE);
}
EXPORT_SYMBOL(cmdq_pkt_cond_jump_abs);

s32 cmdq_pkt_poll_addr(struct cmdq_pkt *pkt, u32 value, u32 addr, u32 mask,
	u8 reg_gpr)
{
	s32 err;
	u8 use_mask = 0;

	if (mask != 0xffffffff) {
		err = cmdq_pkt_append_command(pkt, CMDQ_GET_ARG_C(~mask),
			CMDQ_GET_ARG_B(~mask), 0, 0, 0, 0, 0, CMDQ_CODE_MASK);
		if (err != 0)
			return err;
		use_mask = 1;
	}

	/* Move extra handle APB address to GPR */
	err = cmdq_pkt_append_command(pkt, CMDQ_GET_ARG_C(addr),
		CMDQ_GET_ARG_B(addr), 0, reg_gpr,
		0, 0, 1, CMDQ_CODE_MOVE);
	if (err != 0)
		cmdq_err("%s fail append command move addr to reg err:%d",
			__func__, err);

	err = cmdq_pkt_append_command(pkt, CMDQ_GET_ARG_C(value),
		CMDQ_GET_ARG_B(value), use_mask, reg_gpr,
		0, 0, 1, CMDQ_CODE_POLL);
	if (err != 0)
		cmdq_err("%s fail append command poll err:%d",
			__func__, err);

	return err;
}
EXPORT_SYMBOL(cmdq_pkt_poll_addr);

s32 cmdq_pkt_poll_reg(struct cmdq_pkt *pkt, u32 value, u8 subsys,
	u16 offset, u32 mask)
{
	s32 err;

	if (mask != 0xffffffff) {
		err = cmdq_pkt_append_command(pkt, CMDQ_GET_ARG_C(~mask),
			CMDQ_GET_ARG_B(~mask), 0, 0, 0, 0, 0, CMDQ_CODE_MASK);
		if (err != 0)
			return err;

		offset = offset | 0x1;
	}

	return cmdq_pkt_append_command(pkt, CMDQ_GET_ARG_C(value),
		CMDQ_GET_ARG_B(value), offset, subsys, 0, 0, 0, CMDQ_CODE_POLL);
}
EXPORT_SYMBOL(cmdq_pkt_poll_reg);

s32 cmdq_pkt_poll(struct cmdq_pkt *pkt, struct cmdq_base *clt_base,
	u32 value, u32 addr, u32 mask, u8 reg_gpr)
{
	const u32 base = addr & 0xFFFF0000;
	s8 subsys;

	subsys = cmdq_subsys_base_to_id(clt_base, base);
	if (subsys >= 0)
		return cmdq_pkt_poll_reg(pkt, value, subsys,
			CMDQ_GET_REG_OFFSET(addr), mask);

	return cmdq_pkt_poll_addr(pkt, value, addr, mask, reg_gpr);
}
EXPORT_SYMBOL(cmdq_pkt_poll);

s32 cmdq_pkt_poll_sleep(struct cmdq_pkt *pkt, u32 value,
	u32 addr, u32 mask)
{
	s32 err;
	const u16 reg_idx = CMDQ_THR_SPR_IDX1;
	bool spr3_timer = pkt->support_spr3_timer;

	if (!spr3_timer) {
		cmdq_msg("pkt:0x%p skip poll_sleep", pkt);
		return -EINVAL;
	}

	if (mask != 0xffffffff) {
		err = cmdq_pkt_append_command(pkt, CMDQ_GET_ARG_C(~mask),
			CMDQ_GET_ARG_B(~mask), 0, 0, 0, 0, 0, CMDQ_CODE_MASK);
		if (err != 0)
			return err;
	}

	cmdq_pkt_assign_command(pkt, reg_idx, (dma_addr_t)addr | CMDQ_ADDR_LOW_BIT);

	err = cmdq_pkt_append_command(pkt, CMDQ_GET_ARG_C(value),
		CMDQ_GET_ARG_B(value), reg_idx, CMDQ_POLL_SLEEP,
		0, 0, 1, CMDQ_CODE_POLL_SLEEP);
	if (err != 0)
		cmdq_err("%s fail append command poll_sleep err:%d",
			__func__, err);

	return err;
}
EXPORT_SYMBOL(cmdq_pkt_poll_sleep);

int cmdq_pkt_timer_en(struct cmdq_pkt *pkt)
{
	struct cmdq_client *cl = pkt->cl;
	const u32 en = 0x8000000;
	phys_addr_t gce_pa;

	if (!cl)
		return -EINVAL;

	gce_pa = cmdq_mbox_get_base_pa(cl->chan);

	return cmdq_pkt_write(pkt, NULL, gce_pa + CMDQ_TPR_MASK, en, en);
}
EXPORT_SYMBOL(cmdq_pkt_timer_en);

s32 cmdq_pkt_sleep(struct cmdq_pkt *pkt, u32 tick, u16 reg_gpr)
{
		return cmdq_pkt_sleep_reuse(pkt, tick, reg_gpr, NULL);
}
EXPORT_SYMBOL(cmdq_pkt_sleep);

s32 cmdq_pkt_sleep_reuse(struct cmdq_pkt *pkt, u32 tick, u16 reg_gpr,
	struct cmdq_reuse *sleep_jump_to_end)
{
	struct cmdq_client *cl = (struct cmdq_client *)pkt->cl;
	struct cmdq_operand lop, rop;
	const u32 timeout_en = (cl ? cmdq_mbox_get_base_pa(cl->chan) :
		cmdq_dev_get_base_pa(pkt->dev)) + CMDQ_TPR_TIMEOUT_EN;
	bool spr3_timer = pkt->support_spr3_timer;
	u32 tpr_en;
	u16 event;
	u32 end_addr_mark;
	u64 *inst;

	if (spr3_timer) {
		s32 thread_id = cl ? cmdq_mbox_chan_id(cl->chan) : -1;

		if (thread_id < 0)
			return -EPERM;

		tpr_en = 1 << thread_id;
		event = (u16)CMDQ_EVENT_SPR_TIMER + (u16)thread_id;
	} else {
		tpr_en = 1 << reg_gpr;
		event = (u16)CMDQ_EVENT_GPR_TIMER + reg_gpr;
	}

	pkt->write_addr_high = 0;
	/* set target gpr value to max to avoid event trigger
	 * before new value write to gpr
	 */
	lop.reg = true;
	lop.idx = CMDQ_TPR_ID;
	rop.reg = false;
	rop.value = 1;
	if (spr3_timer) {
		cmdq_pkt_logic_command(pkt, CMDQ_LOGIC_SUBTRACT,
			CMDQ_THR_SPR_IDX3, &lop, &rop);
		cmdq_pkt_write(pkt, NULL, timeout_en, tpr_en, tpr_en);
	} else {
		cmdq_pkt_logic_command(pkt, CMDQ_LOGIC_SUBTRACT,
			CMDQ_GPR_CNT_ID + reg_gpr, &lop, &rop);

		lop.reg = true;
		lop.idx = CMDQ_CPR_TPR_MASK;
		rop.reg = false;
		rop.value = tpr_en;
		cmdq_pkt_logic_command(pkt, CMDQ_LOGIC_OR, CMDQ_CPR_TPR_MASK,
			&lop, &rop);
		cmdq_pkt_write_indriect(pkt, NULL, timeout_en, CMDQ_CPR_TPR_MASK, ~0);
	}
	cmdq_pkt_read(pkt, NULL, timeout_en, CMDQ_SPR_FOR_TEMP);
	cmdq_pkt_clear_event(pkt, event);

	if (tick < U16_MAX) {
		lop.reg = true;
		lop.idx = CMDQ_TPR_ID;
		rop.reg = false;
		rop.value = tick;
		if (spr3_timer)
			cmdq_pkt_logic_command(pkt, CMDQ_LOGIC_ADD,
				CMDQ_THR_SPR_IDX3, &lop, &rop);
		else
			cmdq_pkt_logic_command(pkt, CMDQ_LOGIC_ADD,
				CMDQ_GPR_CNT_ID + reg_gpr, &lop, &rop);
	} else {
		cmdq_pkt_assign_command(pkt, CMDQ_SPR_FOR_TEMP, tick);
		lop.reg = true;
		lop.idx = CMDQ_TPR_ID;
		rop.reg = true;
		rop.value = CMDQ_SPR_FOR_TEMP;
		if (spr3_timer)
			cmdq_pkt_logic_command(pkt, CMDQ_LOGIC_ADD,
				CMDQ_THR_SPR_IDX3, &lop, &rop);
		else
			cmdq_pkt_logic_command(pkt, CMDQ_LOGIC_ADD,
				CMDQ_GPR_CNT_ID + reg_gpr, &lop, &rop);
	}

	cmdq_pkt_assign_command(pkt, CMDQ_SPR_FOR_TEMP, 0);
	end_addr_mark = pkt->cmd_buf_size - 8;
	if (sleep_jump_to_end) {
		sleep_jump_to_end->op = CMDQ_CODE_JUMP_C_ABSOLUTE;
		sleep_jump_to_end->offset = pkt->cmd_buf_size - 8;
	}

	lop.reg = true;
	lop.idx = CMDQ_TPR_ID;
	rop.reg = true;
	if (spr3_timer)
		rop.idx = CMDQ_THR_SPR_IDX3;
	else
		rop.idx = CMDQ_GPR_CNT_ID + reg_gpr;
	cmdq_pkt_cond_jump_abs(pkt, CMDQ_SPR_FOR_TEMP, &lop, &rop,
		CMDQ_GREATER_THAN_AND_EQUAL);

	cmdq_pkt_assign_command(pkt, CMDQ_CPR_SLP_GPR_MAX, 0xFFFFFF00);
	lop.reg = true;
	if (spr3_timer)
		lop.idx = CMDQ_THR_SPR_IDX3;
	else
		lop.idx = CMDQ_GPR_CNT_ID + reg_gpr;
	rop.reg = true;
	rop.idx = CMDQ_CPR_SLP_GPR_MAX;
	cmdq_pkt_cond_jump_abs(pkt, CMDQ_SPR_FOR_TEMP, &lop, &rop,
		CMDQ_GREATER_THAN_AND_EQUAL);

	cmdq_pkt_wfe(pkt, event);

	/* read current buffer pa as end mark and fill preview assign */
	if (sleep_jump_to_end)
		sleep_jump_to_end->val = pkt->cmd_buf_size;
	inst = cmdq_pkt_get_va_by_offset(pkt, end_addr_mark);
	*inst |= CMDQ_REG_SHIFT_ADDR(cmdq_pkt_get_curr_buf_pa(pkt));

	if (spr3_timer) {
		cmdq_pkt_write(pkt, NULL, timeout_en, ~tpr_en, tpr_en);
	} else {
		lop.reg = true;
		lop.idx = CMDQ_CPR_TPR_MASK;
		rop.reg = false;
		rop.value = ~tpr_en;
		cmdq_pkt_logic_command(pkt, CMDQ_LOGIC_AND, CMDQ_CPR_TPR_MASK,
			&lop, &rop);
	}

	return 0;
}
EXPORT_SYMBOL(cmdq_pkt_sleep_reuse);

s32 cmdq_pkt_poll_timeout_reuse(struct cmdq_pkt *pkt, u32 value, u8 subsys,
	phys_addr_t addr, u32 mask, u16 count, u16 reg_gpr, struct cmdq_poll_reuse *poll_reuse)
{
	const u16 reg_tmp = CMDQ_SPR_FOR_TEMP;
	const u16 reg_val = CMDQ_THR_SPR_IDX1;
	const u16 reg_poll = CMDQ_THR_SPR_IDX2;
	bool spr3_timer = pkt->support_spr3_timer;
	u16 reg_counter;
	u32 begin_mark, end_addr_mark, shift_pa;
	dma_addr_t cmd_pa;
	struct cmdq_operand lop, rop;
	struct cmdq_instruction *inst;

	if (spr3_timer)
		reg_counter = CMDQ_GPR_CNT_ID + reg_gpr;
	else
		reg_counter = CMDQ_THR_SPR_IDX3;
	pkt->write_addr_high = 0;
	/* assign compare value as compare target later */
	cmdq_pkt_assign_command(pkt, reg_val, value);

	/* init loop counter as 0, counter can be count poll limit or debug */
	cmdq_pkt_assign_command(pkt, reg_counter, 0);

	/* mark begin offset of this operation */
	begin_mark = pkt->cmd_buf_size;
	if (poll_reuse)
		poll_reuse->jump_to_begin.val = pkt->cmd_buf_size;
	/* read target address */
	if (subsys != SUBSYS_NO_SUPPORT)
		cmdq_pkt_read_reg(pkt, subsys, CMDQ_GET_REG_OFFSET(addr),
			reg_poll);
	else
		cmdq_pkt_read_addr(pkt, addr, reg_poll);

	/* mask it */
	if (mask != ~0) {
		lop.reg = true;
		lop.idx = reg_poll;
		rop.reg = true;
		rop.idx = reg_tmp;

		cmdq_pkt_assign_command(pkt, reg_tmp, mask);
		cmdq_pkt_logic_command(pkt, CMDQ_LOGIC_AND, reg_poll,
			&lop, &rop);
	}

	/* assign temp spr as empty, shoudl fill in end addr later */
	if (unlikely(!pkt->avail_buf_size))
		if (cmdq_pkt_add_cmd_buffer(pkt) < 0)
			return -ENOMEM;
	end_addr_mark = pkt->cmd_buf_size;
	if (poll_reuse) {
		poll_reuse->jump_to_end.op = CMDQ_CODE_JUMP_C_ABSOLUTE;
		poll_reuse->jump_to_end.offset = pkt->cmd_buf_size;
	}
	cmdq_pkt_assign_command(pkt, reg_tmp, 0);

	/* compare and jump to end if equal
	* note that end address will fill in later into last instruction
	*/
	lop.reg = true;
	lop.idx = reg_poll;
	rop.reg = true;
	rop.idx = reg_val;

	cmdq_pkt_cond_jump_abs(pkt, reg_tmp, &lop, &rop, CMDQ_EQUAL);

	/* check if timeup and inc counter */
	if (count != U16_MAX) {
		lop.reg = true;
		lop.idx = reg_counter;
		rop.reg = false;
		rop.value = count;
		cmdq_pkt_cond_jump_abs(pkt, reg_tmp, &lop, &rop,
			CMDQ_GREATER_THAN_AND_EQUAL);
	}

	/* always inc counter */
	lop.reg = true;
	lop.idx = reg_counter;
	rop.reg = false;
	rop.value = 1;
	cmdq_pkt_logic_command(pkt, CMDQ_LOGIC_ADD, reg_counter, &lop,
		&rop);

	if (!skip_poll_sleep && poll_reuse)
		cmdq_pkt_sleep_reuse(pkt, CMDQ_POLL_TICK, reg_gpr, &poll_reuse->sleep_jump_to_end);
	else if (!skip_poll_sleep)
		cmdq_pkt_sleep(pkt, CMDQ_POLL_TICK, reg_gpr);

	/* loop to begin */
	if (poll_reuse) {
		poll_reuse->jump_to_begin.op = CMDQ_CODE_JUMP;
		poll_reuse->jump_to_begin.offset = pkt->cmd_buf_size;
	}
	cmd_pa = cmdq_pkt_get_pa_by_offset(pkt, begin_mark);
	cmdq_pkt_jump_addr(pkt, cmd_pa);

	/* read current buffer pa as end mark and fill preview assign */
	cmd_pa = cmdq_pkt_get_curr_buf_pa(pkt);
	if (poll_reuse)
		poll_reuse->jump_to_end.val = pkt->cmd_buf_size;
	inst = (struct cmdq_instruction *)cmdq_pkt_get_va_by_offset(
		pkt, end_addr_mark);
	/* instruction may hit boundary case,
	* check if op code is jump and get next instruction if necessary
	*/
	if (inst->op == CMDQ_CODE_JUMP)
		inst = (struct cmdq_instruction *)cmdq_pkt_get_va_by_offset(
			pkt, end_addr_mark + CMDQ_INST_SIZE);
	shift_pa = CMDQ_REG_SHIFT_ADDR(cmd_pa);

	inst->arg_b = CMDQ_GET_ARG_B(shift_pa);
	inst->arg_c = CMDQ_GET_ARG_C(shift_pa);

	return 0;
}
EXPORT_SYMBOL(cmdq_pkt_poll_timeout_reuse);

s32 cmdq_pkt_poll_timeout(struct cmdq_pkt *pkt, u32 value, u8 subsys,
	phys_addr_t addr, u32 mask, u16 count, u16 reg_gpr)
{
	return cmdq_pkt_poll_timeout_reuse(pkt, value, subsys,
		addr, mask, count, reg_gpr, NULL);
}
EXPORT_SYMBOL(cmdq_pkt_poll_timeout);

void cmdq_pkt_perf_begin(struct cmdq_pkt *pkt)
{
	dma_addr_t pa;
	struct cmdq_pkt_buffer *buf;

	if (!cmdq_util_helper->is_feature_en(CMDQ_LOG_FEAT_PERF))
		return;

	if (!pkt->buf_size)
		if (cmdq_pkt_add_cmd_buffer(pkt) < 0)
			return;

	pa = cmdq_pkt_get_pa_by_offset(pkt, 0) + CMDQ_DBG_PERFBEGIN;
	cmdq_pkt_write_indriect(pkt, NULL, pa + gce_mminfra, CMDQ_TPR_ID, ~0);

	buf = list_first_entry(&pkt->buf, typeof(*buf), list_entry);
	*(u32 *)(buf->va_base + CMDQ_DBG_PERFBEGIN) = 0xdeaddead;
}
EXPORT_SYMBOL(cmdq_pkt_perf_begin);

void cmdq_pkt_perf_end(struct cmdq_pkt *pkt)
{
	dma_addr_t pa;
	struct cmdq_pkt_buffer *buf;

	if (!cmdq_util_helper->is_feature_en(CMDQ_LOG_FEAT_PERF))
		return;

	if (!pkt->buf_size)
		if (cmdq_pkt_add_cmd_buffer(pkt) < 0)
			return;

	pa = cmdq_pkt_get_pa_by_offset(pkt, 0) + CMDQ_DBG_PERFEND;
	cmdq_pkt_write_indriect(pkt, NULL, pa + gce_mminfra, CMDQ_TPR_ID, ~0);

	buf = list_first_entry(&pkt->buf, typeof(*buf), list_entry);
	*(u32 *)(buf->va_base + CMDQ_DBG_PERFEND) = 0xdeaddead;
}
EXPORT_SYMBOL(cmdq_pkt_perf_end);

u32 *cmdq_pkt_get_perf_ret(struct cmdq_pkt *pkt)
{
	struct cmdq_pkt_buffer *buf;

	if (!pkt->cmd_buf_size)
		return NULL;

	buf = list_first_entry(&pkt->buf, typeof(*buf),
		list_entry);

	return (u32 *)(buf->va_base + CMDQ_DBG_PERFBEGIN);
}
EXPORT_SYMBOL(cmdq_pkt_get_perf_ret);

void cmdq_hw_trace_check_inst(struct cmdq_pkt *pkt)
{
	struct cmdq_instruction *cmdq_inst, inst;

	cmdq_inst = (struct cmdq_instruction *)cmdq_pkt_get_va_by_offset(pkt,
		pkt->cmd_buf_size - CMDQ_INST_SIZE);
	if (cmdq_inst && cmdq_inst->op == CMDQ_CODE_LOGIC) {
		inst = *cmdq_inst;
		if (cmdq_inst->arg_c != CMDQ_SPR_FOR_TEMP ||
			cmdq_inst->arg_b != CMDQ_THR_SPR_IDX2) {
			cmdq_inst->arg_c = CMDQ_SPR_FOR_TEMP;
			cmdq_inst->arg_b = CMDQ_THR_SPR_IDX2;
			cmdq_err("wrong hw trace inst:%#018llx -> %#018llx",
				*((unsigned long long *)&inst), *((u64 *)cmdq_inst));
		}
	}
}
EXPORT_SYMBOL(cmdq_hw_trace_check_inst);

static bool cmdq_pkt_hw_trace_event(struct cmdq_pkt *pkt, const u16 event)
{
	struct cmdq_client *client;
	u32 hwid;

	if (!pkt || !pkt->cl || !cmdq_hw_trace)
		return false;

	client = (struct cmdq_client *)pkt->cl;
	hwid = cmdq_util_get_hw_id((u32)cmdq_mbox_get_base_pa(client->chan));
	if (hw_trace_built_in[hwid])
		return false;

	switch (event) {
	case CMDQ_TOKEN_HW_TRACE_WAIT:
	case CMDQ_TOKEN_HW_TRACE_LOCK:
	case CMDQ_TOKEN_PAUSE_TASK_0 ... CMDQ_TOKEN_PAUSE_TASK_32:
		return false;
	}


	return cmdq_util_helper->hw_trace_thread(client->chan);
}

int cmdq_pkt_wfe(struct cmdq_pkt *pkt, u16 event)
{
	u32 arg_b;
	int ret;

	if (event >= CMDQ_EVENT_MAX)
		return -EINVAL;

	if ((event >= CMDQ_TOKEN_PAUSE_TASK_0)
		&& (event <= CMDQ_TOKEN_PAUSE_TASK_32))
		cmdq_err("use invalid event:%d", event);
	/*
	 * WFE arg_b
	 * bit 0-11: wait value
	 * bit 15: 1 - wait, 0 - no wait
	 * bit 16-27: update value
	 * bit 31: 1 - update, 0 - no update
	 */
	arg_b = CMDQ_WFE_UPDATE | CMDQ_WFE_WAIT | CMDQ_WFE_WAIT_VALUE;
	ret = cmdq_pkt_append_command(pkt, CMDQ_GET_ARG_C(arg_b),
		CMDQ_GET_ARG_B(arg_b), event,
		0, 0, 0, 0, CMDQ_CODE_WFE);

	if (cmdq_pkt_hw_trace_event(pkt, event))
		cmdq_pkt_hw_trace(pkt, event);
	return ret;
}
EXPORT_SYMBOL(cmdq_pkt_wfe);

int cmdq_pkt_wait_no_clear(struct cmdq_pkt *pkt, u16 event)
{
	u32 arg_b;
	int ret;

	if (event >= CMDQ_EVENT_MAX)
		return -EINVAL;

	/*
	 * WFE arg_b
	 * bit 0-11: wait value
	 * bit 15: 1 - wait, 0 - no wait
	 * bit 16-27: update value
	 * bit 31: 1 - update, 0 - no update
	 */
	arg_b = CMDQ_WFE_WAIT | CMDQ_WFE_WAIT_VALUE;
	ret = cmdq_pkt_append_command(pkt, CMDQ_GET_ARG_C(arg_b),
		CMDQ_GET_ARG_B(arg_b), event,
		0, 0, 0, 0, CMDQ_CODE_WFE);

	if (cmdq_pkt_hw_trace_event(pkt, event))
		cmdq_pkt_hw_trace(pkt, event);

	if (event == CMDQ_TOKEN_PAUSE_TASK_32)
		pkt->pause_offset = pkt->cmd_buf_size;

	return ret;
}
EXPORT_SYMBOL(cmdq_pkt_wait_no_clear);

int cmdq_pkt_acquire_event(struct cmdq_pkt *pkt, u16 event)
{
	u32 arg_b;

	if (event >= CMDQ_EVENT_MAX)
		return -EINVAL;

	/*
	 * WFE arg_b
	 * bit 0-11: wait value
	 * bit 15: 1 - wait, 0 - no wait
	 * bit 16-27: update value
	 * bit 31: 1 - update, 0 - no update
	 */
	arg_b = CMDQ_WFE_UPDATE | CMDQ_WFE_UPDATE_VALUE | CMDQ_WFE_WAIT;
	return cmdq_pkt_append_command(pkt, CMDQ_GET_ARG_C(arg_b),
		CMDQ_GET_ARG_B(arg_b), event,
		0, 0, 0, 0, CMDQ_CODE_WFE);
}
EXPORT_SYMBOL(cmdq_pkt_acquire_event);

s32 cmdq_pkt_clear_event(struct cmdq_pkt *pkt, u16 event)
{
	if (event >= CMDQ_EVENT_MAX)
		return -EINVAL;

	if ((event >= CMDQ_TOKEN_PAUSE_TASK_0)
		&& (event <= CMDQ_TOKEN_PAUSE_TASK_32))
		cmdq_err("use invalid event:%d", event);

	return cmdq_pkt_append_command(pkt, CMDQ_GET_ARG_C(CMDQ_WFE_UPDATE),
		CMDQ_GET_ARG_B(CMDQ_WFE_UPDATE), event,
		0, 0, 0, 0, CMDQ_CODE_WFE);
}
EXPORT_SYMBOL(cmdq_pkt_clear_event);

s32 cmdq_pkt_set_event(struct cmdq_pkt *pkt, u16 event)
{
	u32 arg_b;

	if (event >= CMDQ_EVENT_MAX)
		return -EINVAL;

	arg_b = CMDQ_WFE_UPDATE | CMDQ_WFE_UPDATE_VALUE;
	return cmdq_pkt_append_command(pkt, CMDQ_GET_ARG_C(arg_b),
		CMDQ_GET_ARG_B(arg_b), event,
		0, 0, 0, 0, CMDQ_CODE_WFE);
}
EXPORT_SYMBOL(cmdq_pkt_set_event);

s32 cmdq_pkt_handshake_event(struct cmdq_pkt *pkt, u16 event)
{
	u16 shake_bit = 1 << (event - CMDQ_EVENT_HANDSHAKE);

	return cmdq_pkt_assign_command(pkt, CMDQ_HANDSHAKE_REG, shake_bit);
}
EXPORT_SYMBOL(cmdq_pkt_handshake_event);

s32 cmdq_pkt_eoc(struct cmdq_pkt *pkt, bool cnt_inc)
{
	/* set to 1 to NOT inc exec count */
	const u8 exec_inc = cnt_inc ? 0 : 1;

	return cmdq_pkt_append_command(pkt,
		pkt->no_irq ? CMDQ_EOC_IRQ_DIS : CMDQ_EOC_IRQ_EN,
		0, 0, exec_inc, 0, 0, 0, CMDQ_CODE_EOC);
}
EXPORT_SYMBOL(cmdq_pkt_eoc);

s32 cmdq_pkt_finalize(struct cmdq_pkt *pkt)
{
	int err;

	if (cmdq_pkt_is_finalized(pkt))
		return 0;

#if IS_ENABLED(CONFIG_MTK_CMDQ_MBOX_EXT)
	cmdq_pkt_perf_end(pkt);

#ifdef CMDQ_SECURE_SUPPORT
	if (pkt->sec_data) {
		err = cmdq_sec_helper->sec_insert_backup_cookie_fp(pkt);
		if (err)
			return err;
	}
#endif
#endif	/* end of CONFIG_MTK_CMDQ_MBOX_EXT */

	if (append_by_event && !pkt->sec_data) {
		err = cmdq_pkt_wait_no_clear(pkt, CMDQ_TOKEN_PAUSE_TASK_32);
		if (err < 0)
			return err;

		/* JUMP to EOC */
		err = cmdq_pkt_jump(pkt, CMDQ_JUMP_PASS);
		if (err < 0)
			return err;
	}
	/* insert EOC and generate IRQ for each command iteration */
	err = cmdq_pkt_eoc(pkt, true);
	if (err < 0)
		return err;

	/* JUMP to end */
	err = cmdq_pkt_jump(pkt, CMDQ_JUMP_PASS);
	if (err < 0)
		return err;

	cmdq_log("finalize: add EOC and JUMP cmd");

	return 0;
}
EXPORT_SYMBOL(cmdq_pkt_finalize);

s32 cmdq_pkt_refinalize(struct cmdq_pkt *pkt)
{
	struct cmdq_pkt_buffer *buf;
	struct cmdq_instruction *inst;
	s64 off;

	if (!cmdq_pkt_is_finalized(pkt))
		return 0;

	if (cmdq_pkt_is_exec(pkt)) {
		cmdq_err("pkt still running, skip refinalize");
		dump_stack();
		return 0;
	}

	if (pkt->loop)
		return 0;

	buf = list_last_entry(&pkt->buf, typeof(*buf), list_entry);
	inst = buf->va_base + CMDQ_CMD_BUFFER_SIZE - pkt->avail_buf_size - CMDQ_INST_SIZE;
	if (inst->op != CMDQ_CODE_JUMP || inst->arg_a != 1)
		return 0;

	off = CMDQ_JUMP_PASS >> gce_shift_bit;
	inst->arg_a = 0;
	inst->arg_c = CMDQ_GET_ARG_C(off);
	inst->arg_b = CMDQ_GET_ARG_B(off);

	return 0;
}
EXPORT_SYMBOL(cmdq_pkt_refinalize);

s32 cmdq_pkt_finalize_loop(struct cmdq_pkt *pkt)
{
	dma_addr_t start_pa;
	s32 err;

	if (cmdq_pkt_is_finalized(pkt))
		return 0;

	/* insert EOC and generate IRQ for each command iteration */
	err = cmdq_pkt_eoc(pkt, true);
	if (err < 0)
		return err;

	/* JUMP to start of pkt */
	start_pa = cmdq_pkt_get_pa_by_offset(pkt, 0);
	err = cmdq_pkt_jump_addr(pkt, start_pa);
	if (err < 0)
		return err;

	/* mark pkt as loop */
	pkt->loop = true;

	cmdq_log("finalize: add EOC and JUMP begin cmd");

	return 0;
}
EXPORT_SYMBOL(cmdq_pkt_finalize_loop);

#if IS_ENABLED(CONFIG_MTK_CMDQ_MBOX_EXT)
static struct cmdq_flush_item *cmdq_prepare_flush_tiem(struct cmdq_pkt *pkt)
{
	struct cmdq_flush_item *item;

	kfree(pkt->flush_item);
	pkt->flush_item = NULL;

	item = kzalloc(sizeof(*item), GFP_KERNEL);
	if (!item)
		return ERR_PTR(-ENOMEM);

	pkt->flush_item = item;

	return item;
}
#endif

#if IS_ENABLED(CONFIG_MTK_CMDQ_MBOX_EXT)
static void cmdq_pkt_err_irq_dump(struct cmdq_pkt *pkt)
{
	struct cmdq_client *client = pkt->cl;
	dma_addr_t pc = 0;
	struct cmdq_instruction *inst = NULL;
	const char *mod = "CMDQ";
	struct cmdq_pkt_buffer *buf;
	u32 size = pkt->cmd_buf_size, cnt = 0;
	s32 thread_id = cmdq_mbox_chan_id(client->chan);
	u32 hwid = cmdq_util_get_hw_id((u32)cmdq_mbox_get_base_pa(client->chan));
	static u8 err_num[CMDQ_HW_MAX];
#if !IS_ENABLED(CONFIG_MTK_CMDQ_DEBUG)
	struct page *page;
#endif
	struct cmdq_thread *thread = (struct cmdq_thread *)client->chan->con_priv;

	cmdq_msg("%s pkt:%p", __func__, pkt);

	cmdq_util_helper->error_enable((u8)hwid);

	if (cmdq_dump_buf_size)
		cmdq_dump_buffer_size();

	cmdq_util_user_err(client ? client->chan : NULL,
		"hwid:%d begin of error irq %u", hwid, err_num[hwid]++);
	cmdq_chan_dump_dbg(client->chan);
	cmdq_task_get_thread_pc(client->chan, &pc);
	cmdq_util_user_err(client ? client->chan : NULL,
		"pkt:%lx thread:%d pc:%lx",
		(unsigned long)pkt, thread_id, (unsigned long)pc);
	cmdq_thread_dump_spr(thread);

	if (pc) {
		list_for_each_entry(buf, &pkt->buf, list_entry) {
			if (pc < CMDQ_BUF_ADDR(buf) ||
				pc > CMDQ_BUF_ADDR(buf) + CMDQ_CMD_BUFFER_SIZE) {
				size -= CMDQ_CMD_BUFFER_SIZE;
				cmdq_util_user_msg(client ? client->chan : NULL,
					"buf %u:%p va:0x%p pa:%pa iova:%pa time:%llu pool:%d",
					cnt, buf, buf->va_base, &buf->pa_base,
					&buf->iova_base, buf->alloc_time, buf->use_pool);
				cnt++;
				continue;
			}
			inst  = (struct cmdq_instruction *)(
				buf->va_base + (pc - CMDQ_BUF_ADDR(buf)));

			if (size > CMDQ_CMD_BUFFER_SIZE)
				size = CMDQ_CMD_BUFFER_SIZE;

			cmdq_util_user_msg(client ? client->chan : NULL,
				"err irq buf %u:%p va:0x%p pa:%pa iova:%pa time:%llu pool:%d",
				cnt, buf, buf->va_base, &buf->pa_base,
				&buf->iova_base, buf->alloc_time, buf->use_pool);
			cmdq_buf_cmd_parse(buf->va_base, CMDQ_NUM_CMD(size),
				CMDQ_BUF_ADDR(buf), pc, NULL, client->chan);
#if !IS_ENABLED(CONFIG_MTK_CMDQ_DEBUG)
			page = phys_to_page((unsigned long)buf->pa_base);
			cmdq_msg("%s dump page pa:%pa page:%p", __func__, &buf->pa_base,
				page);
			dump_page(page, "cmdq_curruption");
#endif
			break;
		}
	}

	cmdq_util_helper->dump_smi();
	if (client)
		cmdq_util_helper->dump_error_irq_debug(client->chan);

	if (inst) {
		/* not sync case, print raw */
		cmdq_util_aee_ex(CMDQ_AEE_EXCEPTION, mod,
			"%s(%s) inst:%#018llx thread:%d",
			mod, cmdq_util_helper->hw_name(client->chan),
			*(u64 *)inst, thread_id);
	} else {
		/* no inst available */
		cmdq_util_aee_ex(CMDQ_AEE_EXCEPTION, mod,
			"%s(%s) instruction not available pc:%pad thread:%d",
			mod, cmdq_util_helper->hw_name(client->chan), &pc, thread_id);
	}

	cmdq_util_helper->error_disable((u8)hwid);
}

static void cmdq_flush_async_cb(struct cmdq_cb_data data)
{
	struct cmdq_pkt *pkt = (struct cmdq_pkt *)data.data;
	struct cmdq_flush_item *item = pkt->flush_item;
	struct cmdq_cb_data user_data = {
		.data = item->data, .err = data.err };
#if IS_ENABLED(CONFIG_MTK_IRQ_MONITOR_DEBUG)
#ifdef CMDQ_SECURE_SUPPORT
	u64 debug_end[2] = {0};
	u32 debug_cnt = 0;
	u8 irq_long_times;
	struct cmdq_client *client = pkt->cl;
	struct cmdq_thread *thread = (struct cmdq_thread *)client->chan->con_priv;
#endif
#endif

	cmdq_log("%s pkt:%p", __func__, pkt);

	if (data.err == -EINVAL) {
		cmdq_pkt_err_irq_dump(pkt);
#if !IS_ENABLED(CONFIG_VHOST_CMDQ)
#if !IS_ENABLED(CONFIG_MTK_CMDQ_DEBUG)
		if (error_irq_bug_on)
			BUG_ON(1);
#else
		BUG_ON(1);
#endif
#endif
	}

#if IS_ENABLED(CONFIG_MTK_IRQ_MONITOR_DEBUG)
#ifdef CMDQ_SECURE_SUPPORT
	debug_end[debug_cnt++] = sched_clock();
#endif
#endif
	if (item->cb)
		item->cb(user_data);
#if IS_ENABLED(CONFIG_MTK_IRQ_MONITOR_DEBUG)
#ifdef CMDQ_SECURE_SUPPORT
	debug_end[debug_cnt++] = sched_clock();
#endif
#endif

#if IS_ENABLED(CONFIG_MTK_IRQ_MONITOR_DEBUG)
#ifdef CMDQ_SECURE_SUPPORT
	if (!pkt->sec_data) {
		irq_long_times = cmdq_get_irq_long_times(client->chan);
		if (debug_end[1] - debug_end[0] >= 500000 && !irq_long_times)
			thread->user_cb_cost = debug_end[1] - debug_end[0];
	}
#endif
#endif
	complete(&pkt->cmplt);
}
#endif

#if IS_ENABLED(CONFIG_MTK_CMDQ_MBOX_EXT)
static void cmdq_print_wait_summary(void *chan, dma_addr_t pc,
	struct cmdq_instruction *inst)
{
#define txt_len 128
	char text[txt_len];
	char text_gpr[30] = {0};
	void *base;
	u32 gprid, val, len;

	if (!chan) {
		cmdq_err("chan is null");
		return;
	}

	cmdq_buf_print_wfe(text, txt_len, (u32)(pc & 0xFFFF), (void *)inst);

	if (inst->arg_a >= CMDQ_EVENT_GPR_TIMER &&
		inst->arg_a <= CMDQ_EVENT_GPR_TIMER + CMDQ_GPR_R15) {
		base = cmdq_mbox_get_base(chan);
		gprid = inst->arg_a - CMDQ_EVENT_GPR_TIMER;
		val = readl(base + CMDQ_GPR_R0_OFF + gprid * 4);

		len = snprintf(text_gpr, ARRAY_SIZE(text_gpr),
			" GPR R%u:%#x", gprid, val);
		if (len >= ARRAY_SIZE(text_gpr))
			cmdq_log("len:%d over text_gpr size:%lu",
				len, (unsigned long)ARRAY_SIZE(text_gpr));
	} else if (inst->arg_a >= CMDQ_TOKEN_TZMP_ISP_WAIT &&
		inst->arg_a <= CMDQ_TOKEN_TZMP_AIE_SET) {
		const u16 mod = (inst->arg_a - CMDQ_TOKEN_TZMP_ISP_WAIT) / 2;
		const u16 event = CMDQ_TOKEN_TZMP_ISP_WAIT + mod * 2;

		cmdq_util_user_msg(chan,
				"CMDQ_TZMP: mod:%hu event %hu:%#x %#x",
			mod, event, cmdq_get_event(chan, event),
			cmdq_get_event(chan, event + 1));
	} else if (inst->arg_a >= CMDQ_TOKEN_PREBUILT_MDP_WAIT &&
		inst->arg_a <= CMDQ_TOKEN_DISP_VA_END) {
		const u16 mod =
			(inst->arg_a - CMDQ_TOKEN_PREBUILT_MDP_WAIT) / 3;
		const u16 event = CMDQ_TOKEN_PREBUILT_MDP_WAIT + mod * 3;

		cmdq_util_user_msg(chan,
				"CMDQ_PREBUILT: mod:%hu event %hu:%#x %#x %#x",
			mod, event, cmdq_get_event(chan, event),
			cmdq_get_event(chan, event + 1),
			cmdq_get_event(chan, event + 2));
	}

	cmdq_util_user_msg(chan, "curr inst: %s value:%u%s",
		text, cmdq_get_event(chan, inst->arg_a), text_gpr);
}

static void cmdq_pkt_call_item_cb(struct cmdq_flush_item *item)
{
	struct cmdq_cb_data cb_data = {
		.data = item->err_data,
		.err = item->err,
	};

	if (!item->err_cb)
		return;
	item->err_cb(cb_data);
}
#endif

void cmdq_pkt_err_dump_cb(struct cmdq_cb_data data)
{
#if IS_ENABLED(CONFIG_MTK_CMDQ_MBOX_EXT)

	static u32 err_num[CMDQ_HW_MAX];
	struct cmdq_pkt *pkt = (struct cmdq_pkt *)data.data;
	struct cmdq_client *client = pkt->cl;
	struct cmdq_flush_item *item =
		(struct cmdq_flush_item *)pkt->flush_item;
	struct cmdq_instruction *inst = NULL;
	dma_addr_t pc = 0;
	phys_addr_t gce_pa = cmdq_mbox_get_base_pa(client->chan);
	const char *mod = NULL;
	s32 thread_id = cmdq_mbox_chan_id(client->chan);
	enum cmdq_aee_type aee;
	u32 hwid = cmdq_util_get_hw_id(gce_pa);
#ifdef CMDQ_SECURE_SUPPORT
	u32 i;
#endif

	/* assign error during dump cb */
	item->err = data.err;

	/* The self loop case, callback to caller with -EBUSY without all other
	 * error message dump.
	 */
	if (pkt->self_loop && data.err == -EBUSY) {
		cmdq_pkt_call_item_cb(item);
		return;
	}

	cmdq_util_helper->dump_lock();

	if (cmdq_dump_buf_size)
		cmdq_dump_buffer_size();

	cmdq_util_user_err(client->chan, "hwid:%d Begin of Error %u",
		hwid, err_num[hwid]);
	if (!err_num[hwid])
		cmdq_util_helper->error_enable((u8)hwid);


#ifdef CMDQ_SECURE_SUPPORT
	/* for secure path dump more detail */
	if (pkt->sec_data) {
		cmdq_util_msg("thd:%d Hidden thread info since it's secure",
			thread_id);
		cmdq_sec_helper->sec_err_dump_fp(
			pkt, client, (u64 **)&inst, &mod);
	} else {
		cmdq_dump_core(client->chan);
		cmdq_thread_dump(client->chan, pkt, (u64 **)&inst, &pc);
	}

	if (data.err == -ECONNABORTED) {
		cmdq_util_msg("skip since abort");
		goto done;
	}

#else
	cmdq_thread_dump(client->chan, pkt, (u64 **)&inst, &pc);
#endif

	if (inst && inst->op == CMDQ_CODE_WFE) {
#ifdef CMDQ_SECURE_SUPPORT
		if (!pkt->sec_data)
			cmdq_print_wait_summary(client->chan, pc, inst);
#else
		cmdq_print_wait_summary(client->chan, pc, inst);
#endif

		if (inst->arg_a >= CMDQ_TOKEN_PREBUILT_MDP_WAIT &&
			inst->arg_a <= CMDQ_TOKEN_DISP_VA_END)
			cmdq_util_prebuilt_dump(
				hwid, inst->arg_a);
	} else if (inst)
		cmdq_buf_cmd_parse((u64 *)inst, 1, pc, pc,
			"curr inst:", client->chan);
	else
		cmdq_util_msg("curr inst: Not Available");

	cmdq_pkt_call_item_cb(item);

	pkt->err_data.offset = cmdq_task_current_offset(pc, pkt);
	if (inst && inst->op == CMDQ_CODE_WFE) {
		pkt->err_data.wfe_timeout = true;
		pkt->err_data.event = inst->arg_a;
	}

	if (!pkt->aee_cb)
		aee = CMDQ_AEE_WARN;
	else
		aee = pkt->aee_cb(data);

	if (aee != CMDQ_NO_AEE) {
		cmdq_dump_pkt(pkt, pc, true);

#ifdef CMDQ_SECURE_SUPPORT
		if (!pkt->sec_data && (hwid || !err_num[hwid]))
			cmdq_util_helper->dump_smi();
#else
		cmdq_util_helper->dump_smi();
#endif
	}

	if (inst && inst->op == CMDQ_CODE_WFE && aee != CMDQ_NO_AEE_DUMP) {
		mod = cmdq_util_helper->event_module_dispatch(gce_pa, inst->arg_a,
			thread_id);
		cmdq_util_aee_ex(aee, mod,
			"DISPATCH:%s(%s) inst:%#018llx OP:WAIT EVENT:%hu thread:%d",
			mod, cmdq_util_helper->hw_name(client->chan),
			*(u64 *)inst, inst->arg_a, thread_id);
#ifdef CMDQ_SECURE_SUPPORT
		if (!pkt->sec_data && aee != CMDQ_NO_AEE) {
			for (i = 0; i < EVENT_DEBUG_TIMES; i++)
				cmdq_event_dump_and_clr(client->chan);
		}
#endif
	} else if (inst) {
		if (!mod)
			mod = cmdq_util_helper->thread_module_dispatch(gce_pa, thread_id);

		/* not sync case, print raw */
		cmdq_util_aee_ex(aee, mod,
			"DISPATCH:%s(%s) inst:%#018llx OP:%#04hhx thread:%d",
			mod, cmdq_util_helper->hw_name(client->chan),
			*(u64 *)inst, inst->op, thread_id);
	} else {
		if (!mod)
			mod = "CMDQ";

		/* no inst available */
		if (aee == CMDQ_AEE_EXCEPTION)
			cmdq_util_aee_ex(aee, mod,
				"DISPATCH:%s(%s) unknown instruction thread:%d",
				mod, cmdq_util_helper->hw_name(client->chan), thread_id);
		else
			cmdq_util_aee_ex(CMDQ_AEE_WARN, mod,
				"DISPATCH:%s(%s) unknown instruction thread:%d",
				mod, cmdq_util_helper->hw_name(client->chan), thread_id);
	}
#ifdef CMDQ_SECURE_SUPPORT
done:
#endif

	cmdq_util_user_err(client->chan, "hwid:%d End of Error %u", hwid, err_num[hwid]);
	if (!err_num[hwid]) {
		cmdq_util_helper->error_disable((u8)hwid);
		cmdq_util_helper->set_first_err_mod(client->chan, mod);
	}
	err_num[hwid]++;
	cmdq_util_helper->dump_unlock();

#else
	cmdq_err("cmdq error:%d", data.err);
#endif	/* CONFIG_MTK_CMDQ_MBOX_EXT */
}

s32 cmdq_pkt_flush_async(struct cmdq_pkt *pkt,
	cmdq_async_flush_cb cb, void *data)
{
#if IS_ENABLED(CONFIG_MTK_CMDQ_DEBUG)
	u64 start = sched_clock(), end[5];
	u32 end_cnt = 0;
#endif
#if IS_ENABLED(CONFIG_MTK_CMDQ_MBOX_EXT)
#if IS_ENABLED(CONFIG_VIRTIO_CMDQ)
	struct cmdq_flush_item *item;
#else
	struct cmdq_flush_item *item = cmdq_prepare_flush_tiem(pkt);
#endif
#endif
	struct cmdq_client *client = pkt->cl;
	s32 err;

	if (!client) {
		cmdq_err("client is NULL");
		dump_stack();
		return -EINVAL;
	}

#if IS_ENABLED(CONFIG_VIRTIO_CMDQ)
	if (is_virtio_client(client)) {
		item = cmdq_prepare_flush_tiem(pkt);
		if (IS_ERR(item))
			return -ENOMEM;
		return virtio_cmdq_pkt_flush_async(pkt, cb, data);
	}
#endif
#if IS_ENABLED(CONFIG_VHOST_CMDQ)
	if (client->is_virtio)
		return vhost_cmdq_platform->vhost_cmdq_pkt_flush_async(pkt, cb, data);
#endif

#if IS_ENABLED(CONFIG_MTK_CMDQ_MBOX_EXT)
#if IS_ENABLED(CONFIG_VIRTIO_CMDQ)
	item  = cmdq_prepare_flush_tiem(pkt);
#endif
	if (IS_ERR(item))
		return -ENOMEM;
#endif
#if IS_ENABLED(CONFIG_MTK_CMDQ_DEBUG)
	end[end_cnt++] = sched_clock();
#endif
	err = cmdq_pkt_finalize(pkt);
	if (err < 0)
		return err;
#if IS_ENABLED(CONFIG_MTK_CMDQ_DEBUG)
	end[end_cnt++] = sched_clock();
#endif
	if (pkt->create_instr_cnt == (pkt->cmd_buf_size / CMDQ_INST_SIZE)) {
		cmdq_err("pkt is non-cmd");
		return -EINVAL;
	}

	if (append_by_event && pkt->pause_offset) {
		struct cmdq_instruction *cmdq_inst, inst;
		cmdq_inst = (void *)cmdq_pkt_get_va_by_offset(pkt,
			pkt->pause_offset - CMDQ_INST_SIZE);
		if (cmdq_inst) {
			inst = *cmdq_inst;
			cmdq_inst->arg_a = CMDQ_TOKEN_PAUSE_TASK_0
				+ cmdq_mbox_chan_id(client->chan);
			if (cmdq_inst->op != CMDQ_CODE_WFE) {
				cmdq_err("wrong pause inst:%#018llx -> %#018llx",
					*((unsigned long long *)&inst), *((u64 *)cmdq_inst));
				BUG_ON(1);
			}
		} else
			cmdq_err("inst is NULL,offset:%zu cmd_size:%zu buf_size:%zu buf_cnt:%u",
				pkt->pause_offset, pkt->cmd_buf_size, pkt->buf_size, pkt->buf_cnt);
	}
#if IS_ENABLED(CONFIG_MTK_CMDQ_DEBUG)
	end[end_cnt++] = sched_clock();
#endif
#if IS_ENABLED(CONFIG_MTK_CMDQ_MBOX_EXT)
	item->cb = cb;
	item->data = data;
	pkt->cb.cb = cmdq_flush_async_cb;
	pkt->cb.data = pkt;

	item->err_cb = pkt->err_cb.cb;
	item->err_data = pkt->err_cb.data;
	pkt->err_cb.cb = cmdq_pkt_err_dump_cb;
	pkt->err_cb.data = pkt;

	pkt->rec_irq = 0;
	pkt->rec_submit = sched_clock();
#else
	pkt->cb.cb = cb;
	pkt->cb.data = data;
#endif
#if IS_ENABLED(CONFIG_MTK_CMDQ_DEBUG)
	end[end_cnt++] = sched_clock();
#endif
	mutex_lock(&client->chan_mutex);
	err = mbox_send_message(client->chan, pkt);
	if (!pkt->task_alloc) {
#if IS_ENABLED(CONFIG_MTK_CMDQ_MBOX_EXT)
		if (!pkt->rec_irq)
			err = -ECONNABORTED;
#else
		err = -ECONNABORTED;
#endif
	}
	/* We can send next packet immediately, so just call txdone. */
	mbox_client_txdone(client->chan, 0);
	mutex_unlock(&client->chan_mutex);
#if IS_ENABLED(CONFIG_MTK_CMDQ_DEBUG)
	end[end_cnt] = sched_clock();
	if (end[end_cnt] - start >= 4000000) /* 4 ms*/
		cmdq_msg("%s cost time %llu alloc item:%llu finalize:%llu inst:%llu cb:%llu mbox:%llu ms",
			__func__, end[end_cnt] - start, end[0] - start, end[1] - end[0],
			end[2] - end[1], end[3] - end[2], end[4] - end[3]);
#endif

	return err;
}
EXPORT_SYMBOL(cmdq_pkt_flush_async);

#if IS_ENABLED(CONFIG_MTK_CMDQ_MBOX_EXT)
void cmdq_dump_summary(struct cmdq_client *client, struct cmdq_pkt *pkt)
{
	struct cmdq_instruction *inst = NULL;
	dma_addr_t pc;

	cmdq_dump_core(client->chan);
	cmdq_thread_dump(client->chan, pkt, (u64 **)&inst, &pc);
	if (inst && inst->op == CMDQ_CODE_WFE)
		cmdq_print_wait_summary(client->chan, pc, inst);
	else if (inst)
		cmdq_buf_cmd_parse((u64 *)inst, 1, pc, pc,
			"curr inst:", client->chan);
	else
		cmdq_msg("curr inst: Not Available");
	cmdq_dump_pkt(pkt, pc, false);
}
EXPORT_SYMBOL(cmdq_dump_summary);

static int cmdq_pkt_wait_complete_loop(struct cmdq_pkt *pkt)
{
	struct cmdq_client *client = pkt->cl;
	struct cmdq_flush_item *item = pkt->flush_item;
	unsigned long ret;
	int cnt = 0;
	u32 timeout_ms = cmdq_mbox_get_thread_timeout((void *)client->chan);
	bool skip = false, clock = false;

#if IS_ENABLED(CONFIG_CMDQ_MMPROFILE_SUPPORT)
	cmdq_mmp_wait(client->chan, pkt);
#endif

	while (!pkt->task_alloc) {
		ret = wait_for_completion_timeout(&pkt->cmplt,
			msecs_to_jiffies(CMDQ_PREDUMP_MS(timeout_ms)));
		if (ret) {
			/* task alloc failed then skip predump */
			skip = true;
			break;
		}
		cmdq_msg("wait before submit pkt:%p, task_alloc: %d",
			pkt, pkt->task_alloc);
	}

	while (!skip) {
		if (timeout_ms == CMDQ_NO_TIMEOUT) {
			wait_for_completion(&pkt->cmplt);
			break;
		}

		ret = wait_for_completion_timeout(&pkt->cmplt,
			msecs_to_jiffies(CMDQ_PREDUMP_MS(timeout_ms)));
		if (ret)
			break;

		if (!clock)   {
			cmdq_mbox_enable(client->chan);
			clock = true;
		}

		cmdq_util_helper->dump_lock();
		cmdq_util_user_msg(client->chan,
			"===== SW timeout Pre-dump %u =====", cnt);
		++cnt;
		cmdq_dump_summary(client, pkt);
		cmdq_util_helper->dump_unlock();

		if (cnt == 1 && cmdq_hw_trace && !cmdq_is_hw_trace_thread(client->chan))
			cmdq_util_hw_trace_dump(cmdq_util_get_hw_id(
				(u32)cmdq_mbox_get_base_pa(client->chan)),
				cmdq_util_get_bit_feature() &
				CMDQ_LOG_FEAT_PERF);
	}

	pkt->task_alloc = false;

	if (clock)
		cmdq_mbox_disable(client->chan);

	return item->err;
}

int cmdq_pkt_wait_complete(struct cmdq_pkt *pkt)
{
	struct cmdq_flush_item *item = pkt->flush_item;

#if IS_ENABLED(CONFIG_VIRTIO_CMDQ)
	struct cmdq_client *client = pkt->cl;

	if (is_virtio_client(client))
		return virtio_cmdq_pkt_wait_complete(pkt);
#elif IS_ENABLED(CONFIG_VHOST_CMDQ)
	struct cmdq_client *client = pkt->cl;
	if (client->is_virtio)
		return vhost_cmdq_platform->vhost_cmdq_pkt_wait_complete(pkt);
#endif

	if (!item) {
		cmdq_err("pkt need flush from flush async ex:0x%p", pkt);
		return -EINVAL;
	}

	pkt->rec_wait = sched_clock();
	cmdq_trace_begin("%s pkt:%p", __func__, pkt);

#ifdef CMDQ_SECURE_SUPPORT
	if (pkt->sec_data)
		cmdq_sec_helper->sec_pkt_wait_complete_fp(pkt);
	else
		cmdq_pkt_wait_complete_loop(pkt);
#else
	cmdq_pkt_wait_complete_loop(pkt);
#endif

	cmdq_trace_end("%s pkt:%p", __func__, pkt);
	cmdq_util_helper->track(pkt);

	return item->err;
}
EXPORT_SYMBOL(cmdq_pkt_wait_complete);
#endif

#if IS_ENABLED(CONFIG_MTK_CMDQ_MBOX_EXT)
static void cmdq_pkt_flush_q_wait_work(struct work_struct *w)
{
	struct cmdq_flush_item *item_q = container_of(w,
		struct cmdq_flush_item, work);
	int ret;
	struct cmdq_client *cl = item_q->pkt->cl;
	s32 hwid, thd;

	ret = cmdq_pkt_wait_complete(item_q->pkt);
	if (cl) {
		hwid = (s32)cmdq_util_get_hw_id((u32)cmdq_mbox_get_base_pa(cl->chan));
		thd = (s32)cmdq_mbox_chan_id(cl->chan);
		cmdq_trace_begin("%s hwid:%d thrd:%d pkt:%p",
			__func__, hwid, thd, item_q->pkt);
	} else {
		cmdq_trace_begin("%s pkt:%p", __func__, item_q->pkt);
	}

	if (item_q->cb) {
		struct cmdq_cb_data data = {.data = item_q->data, .err = ret};

		item_q->cb(data);
	}
	cmdq_trace_end("%s pkt:%p", __func__, item_q->pkt);
	kfree(item_q);
}
#else
static void cmdq_pkt_flush_q_cb_work(struct work_struct *w)
{
	struct cmdq_flush_item *item_q = container_of(w,
		struct cmdq_flush_item, work);
	struct cmdq_cb_data data;

	data.data = item_q->data;
	data.err = item_q->err;
	item_q->cb(data);
	kfree(item_q);
}
#endif

static void cmdq_pkt_flush_q_cb(struct cmdq_cb_data data)
{
	struct cmdq_flush_item *item_q = (struct cmdq_flush_item *)data.data;
	struct cmdq_client *cl = item_q->pkt->cl;
	struct client_priv *priv = cl->cl_priv;

	item_q->err = data.err;
	queue_work(priv->flushq, &item_q->work);
}

s32 cmdq_pkt_flush_threaded(struct cmdq_pkt *pkt,
	cmdq_async_flush_cb cb, void *data)
{
	struct cmdq_flush_item *item_q = kzalloc(sizeof(*item_q), GFP_KERNEL);
	s32 err;
	u64 start = sched_clock(), diff;

	if (!item_q)
		return -ENOMEM;

	item_q->cb = cb;
	item_q->data = data;
	item_q->pkt = pkt;

#if IS_ENABLED(CONFIG_MTK_CMDQ_MBOX_EXT)

	INIT_WORK(&item_q->work, cmdq_pkt_flush_q_wait_work);
	err = cmdq_pkt_flush_async(pkt, NULL, NULL);
	if (err >= 0) {
		struct cmdq_cb_data data = {.data = item_q, .err = 0};

		cmdq_pkt_flush_q_cb(data);
	}
#else
	INIT_WORK(&item_q->work, cmdq_pkt_flush_q_cb_work);
	err = cmdq_pkt_flush_async(pkt, cmdq_pkt_flush_q_cb, item_q);
#endif

	diff = sched_clock() - start;
	if (diff >= 4000000) /* 4 ms*/
		cmdq_msg("%s cost time %llu ms ", __func__, div_u64(diff, 1000000));
	return err;
}
EXPORT_SYMBOL(cmdq_pkt_flush_threaded);

#if !IS_ENABLED(CONFIG_MTK_CMDQ_MBOX_EXT)
static void cmdq_pkt_flush_cb(struct cmdq_cb_data data)
{
	struct cmdq_flush_completion *cmplt = data.data;

	cmplt->err = !data.err ? false : true;
	complete(&cmplt->cmplt);
}
#endif

int cmdq_pkt_flush(struct cmdq_pkt *pkt)
{
#if IS_ENABLED(CONFIG_MTK_CMDQ_MBOX_EXT)
	int err;

	err = cmdq_pkt_flush_async(pkt, NULL, NULL);
	if (err < 0)
		return err;
	return cmdq_pkt_wait_complete(pkt);
#else
	struct cmdq_flush_completion cmplt;
	int err;

	cmdq_log("start");

	init_completion(&cmplt.cmplt);
	cmplt.pkt = pkt;
	err = cmdq_pkt_flush_async(pkt, cmdq_pkt_flush_cb, &cmplt);
	if (err < 0)
		return err;

	wait_for_completion(&cmplt.cmplt);

	cmdq_log("done pkt:0x%p err:%d", cmplt.pkt, cmplt.err);
	return cmplt.err ? -EFAULT : 0;
#endif
}
EXPORT_SYMBOL(cmdq_pkt_flush);

static void cmdq_buf_print_read(char *text, u32 txt_sz,
	u32 offset, struct cmdq_instruction *cmdq_inst)
{
	u32 addr, len;

	if (cmdq_inst->arg_b_type == CMDQ_IMMEDIATE_VALUE &&
		(cmdq_inst->arg_b & CMDQ_ADDR_LOW_BIT)) {
		/* 48bit format case */
		addr = cmdq_inst->arg_b & 0xfffc;

		len = snprintf(text, txt_sz,
			"%#06x %#018llx [Read ] Reg Index %#010x = addr(low) %#06x",
			offset, *((u64 *)cmdq_inst), cmdq_inst->arg_a, addr);
	} else {
		addr = ((u32)(cmdq_inst->arg_b |
			(cmdq_inst->s_op << CMDQ_SUBSYS_SHIFT)));

		len = snprintf(text, txt_sz,
			"%#06x %#018llx [Read ] Reg Index %#010x = %s%#010x",
			offset, *((u64 *)cmdq_inst), cmdq_inst->arg_a,
			cmdq_inst->arg_b_type ? "*Reg Index " : "SubSys Reg ",
			addr);
	}
	if (len >= txt_sz)
		cmdq_log("len:%d over txt_sz:%d", len, txt_sz);
}

static void cmdq_buf_print_write(char *text, u32 txt_sz,
	u32 offset, struct cmdq_instruction *cmdq_inst, u16 *spr_cache)
{
	u32 addr, len;

	if (cmdq_inst->arg_a_type == CMDQ_IMMEDIATE_VALUE &&
		(cmdq_inst->arg_a & CMDQ_ADDR_LOW_BIT)) {
		/* 48bit format case */
		addr = cmdq_inst->arg_a & 0xfffc;
		if (cmdq_inst->s_op < 4) {
			addr = (spr_cache[cmdq_inst->s_op] << 16) | addr;
			len = snprintf(text, txt_sz,
				"%#06x %#018llx [Write] addr %#010x = %s%#010x%s",
				offset, *((u64 *)cmdq_inst), addr,
				CMDQ_REG_IDX_PREFIX(cmdq_inst->arg_b_type),
				cmdq_inst->arg_b_type ? cmdq_inst->arg_b :
				CMDQ_GET_32B_VALUE(cmdq_inst->arg_b,
					cmdq_inst->arg_c),
					cmdq_inst->op == CMDQ_CODE_WRITE_S_W_MASK ?
					" with mask" : "");
		} else {
			len = snprintf(text, txt_sz,
				"%#06x %#018llx [Write] addr(low) %#06x = %s%#010x%s",
				offset, *((u64 *)cmdq_inst), addr,
				CMDQ_REG_IDX_PREFIX(cmdq_inst->arg_b_type),
				cmdq_inst->arg_b_type ? cmdq_inst->arg_b :
				CMDQ_GET_32B_VALUE(cmdq_inst->arg_b, cmdq_inst->arg_c),
				cmdq_inst->op == CMDQ_CODE_WRITE_S_W_MASK ?
				" with mask" : "");
		}
	} else {
		addr = ((u32)(cmdq_inst->arg_a |
			(cmdq_inst->s_op << CMDQ_SUBSYS_SHIFT)));

		len = snprintf(text, txt_sz,
			"%#06x %#018llx [Write] %s%#010x = %s%#010x%s",
			offset, *((u64 *)cmdq_inst),
			cmdq_inst->arg_a_type ? "*Reg Index " : "SubSys Reg ",
			addr, CMDQ_REG_IDX_PREFIX(cmdq_inst->arg_b_type),
			cmdq_inst->arg_b_type ? cmdq_inst->arg_b :
			CMDQ_GET_32B_VALUE(cmdq_inst->arg_b, cmdq_inst->arg_c),
			cmdq_inst->op == CMDQ_CODE_WRITE_S_W_MASK ?
			" with mask" : "");
	}
	if (len >= txt_sz)
		cmdq_log("len:%d over txt_sz:%d", len, txt_sz);
}

void cmdq_buf_print_wfe(char *text, u32 txt_sz,
	u32 offset, void *inst)
{
	struct cmdq_instruction *cmdq_inst = inst;
	u32 len, cmd = CMDQ_GET_32B_VALUE(cmdq_inst->arg_b, cmdq_inst->arg_c);
	u32 event_op = cmd & 0x80008000;
	u16 update_to = cmdq_inst->arg_b & GENMASK(11, 0);
	u16 wait_to = cmdq_inst->arg_c & GENMASK(11, 0);

	switch (event_op) {
	case 0x80000000:
		len = snprintf(text, txt_sz,
			"%#06x %#018llx [Sync ] %s event %u to %u",
			offset, *((u64 *)cmdq_inst),
			update_to ? "set" : "clear",
			cmdq_inst->arg_a,
			update_to);
		break;
	case 0x8000:
		len = snprintf(text, txt_sz,
			"%#06x %#018llx [Sync ] wait for event %u become %u",
			offset, *((u64 *)cmdq_inst),
			cmdq_inst->arg_a,
			wait_to);
		break;
	case 0x80008000:
	default:
		len = snprintf(text, txt_sz,
			"%#06x %#018llx [Sync ] wait for event %u become %u and %s to %u",
			offset, *((u64 *)cmdq_inst),
			cmdq_inst->arg_a,
			wait_to,
			update_to ? "set" : "clear",
			update_to);
		break;
	}
	if (len >= txt_sz)
		cmdq_log("len:%d over txt_sz:%d", len, txt_sz);
}

static const char *cmdq_parse_logic_sop(enum CMDQ_LOGIC_ENUM s_op)
{
	switch (s_op) {
	case CMDQ_LOGIC_ASSIGN:
		return "= ";
	case CMDQ_LOGIC_ADD:
		return "+ ";
	case CMDQ_LOGIC_SUBTRACT:
		return "- ";
	case CMDQ_LOGIC_MULTIPLY:
		return "* ";
	case CMDQ_LOGIC_XOR:
		return "^";
	case CMDQ_LOGIC_NOT:
		return "= ~";
	case CMDQ_LOGIC_OR:
		return "| ";
	case CMDQ_LOGIC_AND:
		return "& ";
	case CMDQ_LOGIC_LEFT_SHIFT:
		return "<< ";
	case CMDQ_LOGIC_RIGHT_SHIFT:
		return ">> ";
	default:
		return "<error: unsupported logic sop>";
	}
}

static const char *cmdq_parse_jump_c_sop(enum CMDQ_CONDITION_ENUM s_op)
{
	switch (s_op) {
	case CMDQ_EQUAL:
		return "==";
	case CMDQ_NOT_EQUAL:
		return "!=";
	case CMDQ_GREATER_THAN_AND_EQUAL:
		return ">=";
	case CMDQ_LESS_THAN_AND_EQUAL:
		return "<=";
	case CMDQ_GREATER_THAN:
		return ">";
	case CMDQ_LESS_THAN:
		return "<";
	default:
		return "<error: unsupported jump conditional sop>";
	}
}

static void cmdq_buf_print_move(char *text, u32 txt_sz,
	u32 offset, struct cmdq_instruction *cmdq_inst)
{
	u64 len, val = (u64)cmdq_inst->arg_a |
		CMDQ_GET_32B_VALUE(cmdq_inst->arg_b, cmdq_inst->arg_c);

	if (cmdq_inst->s_op)
		len = snprintf(text, txt_sz,
			"%#06x %#018llx [Move ] move %#llx to %s%hhu",
			offset, *((u64 *)cmdq_inst), val,
			"Reg Index GPR R", cmdq_inst->s_op);
	else
		len = snprintf(text, txt_sz,
			"%#06x %#018llx [Move ] mask %#010x",
			offset, *((u64 *)cmdq_inst), (u32)~val);
	if (len >= txt_sz)
		cmdq_log("len:%llu over txt_sz:%d", len, txt_sz);
}

static void cmdq_buf_print_logic(char *text, u32 txt_sz,
	u32 offset, struct cmdq_instruction *cmdq_inst, u16 *spr_cache)
{
	u32 len;

	switch (cmdq_inst->s_op) {
	case CMDQ_LOGIC_ASSIGN:
		len = snprintf(text, txt_sz,
			"%#06x %#018llx [Logic] Reg Index %#06x %s%s%#010x",
			offset, *((u64 *)cmdq_inst), cmdq_inst->arg_a,
			cmdq_parse_logic_sop(cmdq_inst->s_op),
			CMDQ_REG_IDX_PREFIX(cmdq_inst->arg_b_type),
			CMDQ_GET_32B_VALUE(cmdq_inst->arg_b, cmdq_inst->arg_c));
		if (cmdq_inst->arg_a < 4)
			spr_cache[cmdq_inst->arg_a] = cmdq_inst->arg_c;
		break;
	case CMDQ_LOGIC_NOT:
		len = snprintf(text, txt_sz,
			"%#06x %#018llx [Logic] Reg Index %#06x %s%s%#010x",
			offset, *((u64 *)cmdq_inst), cmdq_inst->arg_a,
			cmdq_parse_logic_sop(cmdq_inst->s_op),
			CMDQ_REG_IDX_PREFIX(cmdq_inst->arg_b_type),
			cmdq_inst->arg_b);
		break;
	default:
		len = snprintf(text, txt_sz,
			"%#06x %#018llx [Logic] %s%#010x = %s%#010x %s%s%#010x",
			offset, *((u64 *)cmdq_inst),
			CMDQ_REG_IDX_PREFIX(cmdq_inst->arg_a_type),
			cmdq_inst->arg_a,
			CMDQ_REG_IDX_PREFIX(cmdq_inst->arg_b_type),
			cmdq_inst->arg_b, cmdq_parse_logic_sop(cmdq_inst->s_op),
			CMDQ_REG_IDX_PREFIX(cmdq_inst->arg_c_type),
			cmdq_inst->arg_c);
		break;
	}
	if (len >= txt_sz)
		cmdq_log("len:%d over txt_sz:%d", len, txt_sz);
}

static void cmdq_buf_print_write_jump_c(char *text, u32 txt_sz,
	u32 offset, struct cmdq_instruction *cmdq_inst)
{
	u32 len;

	len = snprintf(text, txt_sz,
		"%#06x %#018llx [Jumpc] %s if (%s%#010x %s %s%#010x) jump %s%#010x",
		offset, *((u64 *)cmdq_inst),
		cmdq_inst->op == CMDQ_CODE_JUMP_C_ABSOLUTE ?
		"absolute" : "relative",
		CMDQ_REG_IDX_PREFIX(cmdq_inst->arg_b_type),
		cmdq_inst->arg_b, cmdq_parse_jump_c_sop(cmdq_inst->s_op),
		CMDQ_REG_IDX_PREFIX(cmdq_inst->arg_c_type), cmdq_inst->arg_c,
		CMDQ_REG_IDX_PREFIX(cmdq_inst->arg_a_type), cmdq_inst->arg_a);
	if (len >= txt_sz)
		cmdq_log("len:%d over txt_sz:%d", len, txt_sz);
}

static void cmdq_buf_print_poll(char *text, u32 txt_sz,
	u32 offset, struct cmdq_instruction *cmdq_inst)
{
	u32 len, addr = ((u32)(cmdq_inst->arg_a |
		(cmdq_inst->s_op << CMDQ_SUBSYS_SHIFT)));

	len = snprintf(text, txt_sz,
		"%#06x %#018llx [Poll ] poll %s%#010x = %s%#010x",
		offset, *((u64 *)cmdq_inst),
		cmdq_inst->arg_a_type ? "*Reg Index " : "SubSys Reg ",
		addr,
		CMDQ_REG_IDX_PREFIX(cmdq_inst->arg_b_type),
		CMDQ_GET_32B_VALUE(cmdq_inst->arg_b, cmdq_inst->arg_c));
	if (len >= txt_sz)
		cmdq_log("len:%d over txt_sz:%d", len, txt_sz);
}

static void cmdq_buf_print_jump(char *text, u32 txt_sz,
	u32 offset, struct cmdq_instruction *cmdq_inst)
{
	u32 len, dst = ((u32)cmdq_inst->arg_b) << 16 | cmdq_inst->arg_c;

	len = snprintf(text, txt_sz,
		"%#06x %#018llx [Jump ] jump %s %#llx",
		offset, *((u64 *)cmdq_inst),
		cmdq_inst->arg_a ? "absolute addr" : "relative offset",
		cmdq_inst->arg_a ? CMDQ_REG_REVERT_ADDR((u64)dst) :
		CMDQ_REG_REVERT_ADDR((s64)(s32)dst));
	if (len >= txt_sz)
		cmdq_log("len:%d over txt_sz:%d", len, txt_sz);
}

static void cmdq_buf_print_misc(char *text, u32 txt_sz,
	u32 offset, struct cmdq_instruction *cmdq_inst)
{
	char *cmd_str;
	u32 len;

	switch (cmdq_inst->op) {
	case CMDQ_CODE_EOC:
		cmd_str = "eoc";
		break;
	default:
		cmd_str = "unknown";
		break;
	}

	len = snprintf(text, txt_sz, "%#06x %#018llx %s",
		offset, *((u64 *)cmdq_inst), cmd_str);
	if (len >= txt_sz)
		cmdq_log("len:%d over txt_sz:%d", len, txt_sz);
}

void cmdq_buf_cmd_parse(u64 *buf, u32 cmd_nr, dma_addr_t buf_pa,
	dma_addr_t cur_pa, const char *info, void *chan)
{
#define txt_sz 128
	static char text[txt_sz];
	struct cmdq_instruction *cmdq_inst = (struct cmdq_instruction *)buf;
	u32 i;
	u16 spr_cache[4] = {0};

	for (i = 0; i < cmd_nr; i++) {
		switch (cmdq_inst[i].op) {
		case CMDQ_CODE_WRITE_S:
		case CMDQ_CODE_WRITE_S_W_MASK:
			cmdq_buf_print_write(text, txt_sz, (u32)buf_pa,
				&cmdq_inst[i], spr_cache);
			break;
		case CMDQ_CODE_WFE:
			cmdq_buf_print_wfe(text, txt_sz, (u32)buf_pa,
				(void *)&cmdq_inst[i]);
			break;
		case CMDQ_CODE_MOVE:
			cmdq_buf_print_move(text, txt_sz, (u32)buf_pa,
				&cmdq_inst[i]);
			break;
		case CMDQ_CODE_READ_S:
			cmdq_buf_print_read(text, txt_sz, (u32)buf_pa,
				&cmdq_inst[i]);
			break;
		case CMDQ_CODE_LOGIC:
			cmdq_buf_print_logic(text, txt_sz, (u32)buf_pa,
				&cmdq_inst[i], spr_cache);
			break;
		case CMDQ_CODE_JUMP_C_ABSOLUTE:
		case CMDQ_CODE_JUMP_C_RELATIVE:
			cmdq_buf_print_write_jump_c(text, txt_sz, (u32)buf_pa,
				&cmdq_inst[i]);
			break;
		case CMDQ_CODE_POLL:
		case CMDQ_CODE_POLL_SLEEP:
			cmdq_buf_print_poll(text, txt_sz, (u32)buf_pa,
				&cmdq_inst[i]);
			break;
		case CMDQ_CODE_JUMP:
			cmdq_buf_print_jump(text, txt_sz, (u32)buf_pa,
				&cmdq_inst[i]);
			break;
		default:
			cmdq_buf_print_misc(text, txt_sz, (u32)buf_pa,
				&cmdq_inst[i]);
			break;
		}
		cmdq_util_user_msg(chan, "%s%s",
			info ? info : (buf_pa == cur_pa ? ">>" : "  "),
			text);
		buf_pa += CMDQ_INST_SIZE;
	}
}
EXPORT_SYMBOL(cmdq_buf_cmd_parse);

bool alldump;

void cmdq_set_alldump(bool on)
{
	alldump = on;
}
EXPORT_SYMBOL(cmdq_set_alldump);

u32 cmdq_buf_cmd_parse_buf(u64 *buf, u32 cmd_nr, dma_addr_t buf_pa,
	dma_addr_t cur_pa, const char *info, void *chan, void *buf_out, u32 buf_out_sz)
{
#define txt_sz 128
	static char text[txt_sz];
	struct cmdq_instruction *cmdq_inst = (struct cmdq_instruction *)buf;
	u32 i;
	u16 spr_cache[4] = {0};
	u32 buf_cur = 0;
	s32 len;

	for (i = 0; i < cmd_nr; i++) {
		switch (cmdq_inst[i].op) {
		case CMDQ_CODE_WRITE_S:
		case CMDQ_CODE_WRITE_S_W_MASK:
			cmdq_buf_print_write(text, txt_sz, (u32)buf_pa,
				&cmdq_inst[i], spr_cache);
			break;
		case CMDQ_CODE_WFE:
			cmdq_buf_print_wfe(text, txt_sz, (u32)buf_pa,
				(void *)&cmdq_inst[i]);
			break;
		case CMDQ_CODE_MOVE:
			cmdq_buf_print_move(text, txt_sz, (u32)buf_pa,
				&cmdq_inst[i]);
			break;
		case CMDQ_CODE_READ_S:
			cmdq_buf_print_read(text, txt_sz, (u32)buf_pa,
				&cmdq_inst[i]);
			break;
		case CMDQ_CODE_LOGIC:
			cmdq_buf_print_logic(text, txt_sz, (u32)buf_pa,
				&cmdq_inst[i], spr_cache);
			break;
		case CMDQ_CODE_JUMP_C_ABSOLUTE:
		case CMDQ_CODE_JUMP_C_RELATIVE:
			cmdq_buf_print_write_jump_c(text, txt_sz, (u32)buf_pa,
				&cmdq_inst[i]);
			break;
		case CMDQ_CODE_POLL:
			cmdq_buf_print_poll(text, txt_sz, (u32)buf_pa,
				&cmdq_inst[i]);
			break;
		case CMDQ_CODE_JUMP:
			cmdq_buf_print_jump(text, txt_sz, (u32)buf_pa,
				&cmdq_inst[i]);
			break;
		default:
			cmdq_buf_print_misc(text, txt_sz, (u32)buf_pa,
				&cmdq_inst[i]);
			break;
		}
		len = snprintf(buf_out + buf_cur, buf_out_sz - buf_cur, "%s\n", text);

		if (len < 0)
			cmdq_err("snprintf fail");

		buf_cur += len < 0 ? 0 : len;

		if (buf_cur >= buf_out_sz) {
			cmdq_msg("%s out buf full %u", __func__, buf_cur);
			break;
		}
		buf_pa += CMDQ_INST_SIZE;
	}

	return buf_cur;
}
EXPORT_SYMBOL(cmdq_buf_cmd_parse_buf);

s32 cmdq_pkt_dump_buf(struct cmdq_pkt *pkt, dma_addr_t curr_pa)
{
	struct cmdq_client *client;
	struct cmdq_pkt_buffer *buf;
	u32 size, cnt = 0;

	if (!pkt) {
		cmdq_err("pkt is empty");
		return -EINVAL;
	}
	if (!pkt->task_alive) {
		cmdq_err("task_alive:%d", pkt->task_alive);
		return -EINVAL;
	}

	client = (struct cmdq_client *)pkt->cl;
	list_for_each_entry(buf, &pkt->buf, list_entry) {
		if (list_is_last(&buf->list_entry, &pkt->buf)) {
			size = CMDQ_CMD_BUFFER_SIZE - pkt->avail_buf_size;
		} else if (cnt > 0 && !(curr_pa >= CMDQ_BUF_ADDR(buf) &&
			curr_pa < CMDQ_BUF_ADDR(buf) + CMDQ_BUF_ALLOC_SIZE) && !alldump) {
			cmdq_util_user_msg(client ? client->chan : NULL,
				"buffer %u:%p va:0x%p pa:%pa iova:%pa alloc_time:%llu %#018llx (skip detail) %#018llx",
				cnt, buf, buf->va_base, &buf->pa_base,
				&buf->iova_base, buf->alloc_time,
				*((u64 *)buf->va_base),
				*((u64 *)(buf->va_base +
				CMDQ_CMD_BUFFER_SIZE - CMDQ_INST_SIZE)));
			cnt++;
			continue;
		} else {
			size = CMDQ_CMD_BUFFER_SIZE;
		}
		cmdq_util_user_msg(client ? client->chan : NULL,
			"buffer %u:%p va:0x%p pa:%pa iova:%pa alloc_time:%llu pool:%d",
			cnt, buf, buf->va_base, &buf->pa_base,
			&buf->iova_base, buf->alloc_time, buf->use_pool);
		if (buf->va_base && client) {
			cmdq_buf_cmd_parse(buf->va_base, CMDQ_NUM_CMD(size),
				CMDQ_BUF_ADDR(buf), curr_pa, NULL, client->chan);
		}
		cnt++;
	}

	return 0;
}
EXPORT_SYMBOL(cmdq_pkt_dump_buf);

int cmdq_dump_pkt(struct cmdq_pkt *pkt, dma_addr_t pc, bool dump_ist)
{
	struct cmdq_client *client;

	if (!pkt) {
		cmdq_err("pkt is empty");
		return -EINVAL;
	}
	if (!pkt->task_alive) {
		cmdq_err("task_alive:%d", pkt->task_alive);
		return -EINVAL;
	}

	client = (struct cmdq_client *)pkt->cl;
	if (client) {
		struct cmdq_thread *thread = (struct cmdq_thread *)client->chan->con_priv;

		cmdq_util_user_msg(client->chan,
			"pkt:0x%p(%#x) size:%zu/%zu avail size:%zu priority:%u%s",
			pkt, (u32)(unsigned long)pkt, pkt->cmd_buf_size,
			pkt->buf_size, pkt->avail_buf_size,
			pkt->priority, pkt->loop ? " loop" : "");
#if IS_ENABLED(CONFIG_MTK_CMDQ_MBOX_EXT)
		cmdq_util_user_msg(client->chan,
			"mbox_enable:%llu mbox_disable:%llu submit:%llu trigger:%llu wait:%llu irq:%llu",
			thread->mbox_en, thread->mbox_dis, pkt->rec_submit, pkt->rec_trigger,
			pkt->rec_wait, pkt->rec_irq);
		cmdq_util_dump_fast_mtcmos();
#endif
		cmdq_util_user_msg(client->chan,
			"append info pc:%pa->%pa end:%pa suspend:%d last_inst:%#018llx",
			&pkt->append.pc[0], &pkt->append.pc[1],
			&pkt->append.end, pkt->append.suspend,
			pkt->append.pre_last_inst);
	}

	if (dump_ist)
		cmdq_pkt_dump_buf(pkt, pc);
	cmdq_dump_vcp_reg(pkt);
	return 0;
}
EXPORT_SYMBOL(cmdq_dump_pkt);

#define CMDQ_INST_STR_SIZE 20

char *cmdq_pkt_parse_buf(struct cmdq_pkt *pkt, u32 *size_out, void **raw_out, u32 *size_raw_out)
{
	u32 buf_size = CMDQ_NUM_CMD(pkt->cmd_buf_size) * CMDQ_INST_STR_SIZE + 1;
	char *insts = kmalloc(buf_size, GFP_KERNEL);
	u32 cur_buf = 0, cur_inst = 0, size;
	int len;
	struct cmdq_pkt_buffer *buf;
	u8 *raw = NULL;
	u32 raw_sz = 0;

	cmdq_msg("dump buffer size %u(%zu) pkt %p inst %p raw %s",
		buf_size, pkt->cmd_buf_size, pkt, insts, (raw_out && size_raw_out) ? "true" : "false");

	if (!insts) {
		*size_out = 0;
		return NULL;
	}

	if (raw_out && size_raw_out) {
		raw = kzalloc(pkt->cmd_buf_size, GFP_KERNEL);
		*raw_out = (void *)raw;
	}

	list_for_each_entry(buf, &pkt->buf, list_entry) {
		if (list_is_last(&buf->list_entry, &pkt->buf))
			size = CMDQ_CMD_BUFFER_SIZE - pkt->avail_buf_size;
		else
			size = CMDQ_CMD_BUFFER_SIZE - CMDQ_INST_SIZE;

		cur_inst = 0;
		if (buf == list_first_entry(&pkt->buf, typeof(*buf), list_entry)) {
#if IS_ENABLED(CONFIG_MTK_CMDQ_MBOX_EXT)
			if (cmdq_util_helper->is_feature_en(CMDQ_LOG_FEAT_PERF))
				cur_inst = 2;
#endif
		}

		if (raw) {
			memcpy(raw + raw_sz, buf->va_base, size);
			raw_sz += size;
		}

		for (; cur_inst < CMDQ_NUM_CMD(size) && cur_buf < buf_size;
			cur_inst++) {
			len = snprintf(insts + cur_buf, buf_size - cur_buf,
				"%#018llx,\n", *((u64 *)buf->va_base + cur_inst));
			if (len > 0)
				cur_buf += len;
			if (len != CMDQ_INST_STR_SIZE) {
				cmdq_err(
					"%s wrong in dump len %d cur size %u buf size %u inst idx %u",
					__func__, len, cur_buf, size, cur_inst);
				break;
			}
		}
	}

	*size_out = cur_buf + 1;
	*size_raw_out = raw_sz;
	return insts;
}
EXPORT_SYMBOL(cmdq_pkt_parse_buf);

void cmdq_pkt_set_err_cb(struct cmdq_pkt *pkt,
	cmdq_async_flush_cb cb, void *data)
{
	pkt->err_cb.cb = cb;
	pkt->err_cb.data = (void *)data;
}
EXPORT_SYMBOL(cmdq_pkt_set_err_cb);

#if IS_ENABLED(CONFIG_MTK_CMDQ_MBOX_EXT)
void cmdq_helper_set_fp(struct cmdq_util_helper_fp *cust_cmdq_util)
{
	cmdq_util_helper = cust_cmdq_util;
}
EXPORT_SYMBOL(cmdq_helper_set_fp);

#ifdef CMDQ_SECURE_SUPPORT
void cmdq_sec_helper_set_fp(struct cmdq_sec_helper_fp *cust_cmdq_sec)
{
	cmdq_sec_helper = cust_cmdq_sec;
}
EXPORT_SYMBOL(cmdq_sec_helper_set_fp);
#endif
#endif

static int cmdq_record_buffer_usage(void *data)
{
	while (!kthread_should_stop()) {
		cmdq_util_buff_track(&BUF_SIZE_THRD_MAX[0][0], CMDQ_HW_MAX, CMDQ_THR_MAX_COUNT);
		msleep(1000 * 60);
	}
	return 0;
}

int cmdq_helper_init(void)
{
	struct task_struct *kthr;

	cmdq_msg("%s enter", __func__);
	mutex_init(&vcp.vcp_mutex);
	if (gce_in_vcp)
		vcp.mminfra_base = ioremap(MMINFRA_BASE, 0x1000);

	timer_setup(&vcp.vcp_timer, cmdq_vcp_off, 0);
	INIT_WORK(&vcp.vcp_work, cmdq_vcp_off_work);
	vcp.vcp_wq = create_singlethread_workqueue(
		"cmdq_vcp_power_handler");

	cmdq_pkt_destroy_wq = create_singlethread_workqueue(
		"cmdq_pkt_destroy_wq");

	kthr = kthread_run(cmdq_record_buffer_usage, NULL, "cmdq_buffer_usage");

	if (IS_ERR(kthr))
		cmdq_err("Unable  to run kthread err %ld", PTR_ERR(kthr));

	mutex_init(&buffer_size_mutex);
	mutex_init(&pkt_size_mutex);

	return 0;
}
EXPORT_SYMBOL(cmdq_helper_init);

MODULE_LICENSE("GPL v2");
