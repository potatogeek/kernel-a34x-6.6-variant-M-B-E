// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2021 MediaTek Inc.
 * Author: Dennis YC Hsieh <dennis-yc.hsieh@mediatek.com>
 */

#include <linux/component.h>
#include <linux/dma-mapping.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/math64.h>
#include <linux/delay.h>
#include <soc/mediatek/smi.h>
#include <mtk-smmu-v3.h>

#include "mtk-mml-buf.h"
#include "mtk-mml-color.h"
#include "mtk-mml-core.h"
#include "mtk-mml-driver.h"
#include "mtk-mml-dle-adaptor.h"
#include "mtk-mml-pq-core.h"

#include "tile_driver.h"
#include "mtk-mml-tile.h"
#include "tile_mdp_func.h"
#include "mtk-mml-sys.h"
#include "mtk-mml-mmp.h"

#ifdef CONFIG_MTK_SMI_EXT
#include "smi_public.h"
#endif

/* WROT register offset */
enum wrot_register {
	VIDO_CTRL,
	VIDO_DMA_PERF,
	VIDO_MAIN_BUF_SIZE,
	VIDO_SOFT_RST,
	VIDO_SOFT_RST_STAT,
	VIDO_INT_EN,
	VIDO_INT,
	VIDO_CROP_OFST,
	VIDO_TAR_SIZE,
	VIDO_FRAME_SIZE,
	VIDO_OFST_ADDR,
	VIDO_STRIDE,
	VIDO_BKGD,
	VIDO_OFST_ADDR_C,
	VIDO_STRIDE_C,
	VIDO_CTRL_2,
	VIDO_IN_LINE_ROT,
	VIDO_DITHER,
	VIDO_DITHER_CON,
	VIDO_OFST_ADDR_V,
	VIDO_STRIDE_V,
	VIDO_RSV_1,
	VIDO_DMA_PREULTRA,
	VIDO_IN_SIZE,
	VIDO_ROT_EN,
	VIDO_FIFO_TEST,
	VIDO_MAT_CTRL,
	VIDO_SHADOW_CTRL,
	VIDO_DEBUG,
	VIDO_PVRIC,
	VIDO_SCAN_10BIT,
	VIDO_PENDING_ZERO,
	VIDO_CRC_CTRL,
	VIDO_CRC_VALUE,
	VIDO_BASE_ADDR,
	VIDO_BASE_ADDR_C,
	VIDO_BASE_ADDR_V,
	VIDO_PVRIC_FMT,
	VIDO_AFBC_YUVTRANS,
	VIDO_BASE_ADDR_HIGH,
	VIDO_BASE_ADDR_HIGH_C,
	VIDO_BASE_ADDR_HIGH_V,
	VIDO_OFST_ADDR_HIGH,
	VIDO_OFST_ADDR_HIGH_C,
	VIDO_OFST_ADDR_HIGH_V,

	/* mt6991 */
	VIDO_DDREN_REQ,
	VIDO_SLC_SIDEBAND,
	VIDO_SECURITY_DISABLE,
	VIDO_INT_UP_EN,
	VIDO_INT_UP,
	VIDO_COMPRESION_VALUE,
	VIDO_DUMMY_NORMAL,
	VIDO_DUMMY_SECURE,
	VIDO_CSC_COEFFICIENT_1,
	VIDO_CSC_COEFFICIENT_2,
	VIDO_CSC_COEFFICIENT_3,
	VIDO_CSC_COEFFICIENT_4,
	VIDO_CSC_COEFFICIENT_5,
	VIDO_CSC_COEFFICIENT_6,
	VIDO_CSC_COEFFICIENT_7,
	VIDO_STASH_CMD_INTF,
	VIDO_STASH_CMD_FUNC_1,
	VIDO_STASH_OFST_ADDR,
	VIDO_STASH_OFST_ADDR_HIGH,
	VIDO_STASH_OFST_ADDR_C,
	VIDO_STASH_OFST_ADDR_HIGH_C,
	VIDO_STASH_OFST_ADDR_V,
	VIDO_STASH_OFST_ADDR_HIGH_V,
	VIDO_STASH_SW_ADDR,
	VIDO_STASH_SW_ADDR_HIGH,
	VIDO_STASH_SW_WORK,
	VIDO_STASH_DELAY_CNT,
	wrot_register_total,
};

static const u16 wrot_mt6989[] = {
	[VIDO_CTRL			] = 0x000,
	[VIDO_DMA_PERF			] = 0x004,
	[VIDO_MAIN_BUF_SIZE		] = 0x008,
	[VIDO_SOFT_RST			] = 0x010,
	[VIDO_SOFT_RST_STAT		] = 0x014,
	[VIDO_INT_EN			] = 0x018,
	[VIDO_INT			] = 0x01c,
	[VIDO_CROP_OFST			] = 0x020,
	[VIDO_TAR_SIZE			] = 0x024,
	[VIDO_FRAME_SIZE		] = 0x028,
	[VIDO_OFST_ADDR			] = 0x02c,
	[VIDO_STRIDE			] = 0x030,
	[VIDO_BKGD			] = 0x034,
	[VIDO_OFST_ADDR_C		] = 0x038,
	[VIDO_STRIDE_C			] = 0x03c,
	[VIDO_CTRL_2			] = 0x048,
	[VIDO_IN_LINE_ROT		] = 0x050,
	[VIDO_DITHER			] = 0x054,
	[VIDO_DITHER_CON		] = 0x058,
	[VIDO_OFST_ADDR_V		] = 0x068,
	[VIDO_STRIDE_V			] = 0x06c,
	[VIDO_RSV_1			] = 0x070,
	[VIDO_DMA_PREULTRA		] = 0x074,
	[VIDO_IN_SIZE			] = 0x078,
	[VIDO_ROT_EN			] = 0x07c,
	[VIDO_FIFO_TEST			] = 0x080,
	[VIDO_MAT_CTRL			] = 0x084,
	[VIDO_SHADOW_CTRL		] = 0x08c,
	[VIDO_DEBUG			] = 0x0d0,
	[VIDO_PVRIC			] = 0x0d8,
	[VIDO_SCAN_10BIT		] = 0x0dc,
	[VIDO_PENDING_ZERO		] = 0x0e0,
	[VIDO_CRC_CTRL			] = 0x0e8,
	[VIDO_CRC_VALUE			] = 0x0ec,
	[VIDO_BASE_ADDR			] = 0xf00,
	[VIDO_BASE_ADDR_C		] = 0xf04,
	[VIDO_BASE_ADDR_V		] = 0xf08,
	[VIDO_PVRIC_FMT			] = 0xf0c,
	[VIDO_AFBC_YUVTRANS		] = 0xf2c,
	[VIDO_BASE_ADDR_HIGH		] = 0xf34,
	[VIDO_BASE_ADDR_HIGH_C		] = 0xf38,
	[VIDO_BASE_ADDR_HIGH_V		] = 0xf3c,
	[VIDO_OFST_ADDR_HIGH		] = 0xf40,
	[VIDO_OFST_ADDR_HIGH_C		] = 0xf44,
	[VIDO_OFST_ADDR_HIGH_V		] = 0xf48,
};

u16 wrot_mt6991[] = {
	[VIDO_CTRL			] = 0x000,
	[VIDO_DMA_PERF			] = 0x004,
	[VIDO_MAIN_BUF_SIZE		] = 0x008,
	[VIDO_SOFT_RST			] = 0x010,
	[VIDO_SOFT_RST_STAT		] = 0x014,
	[VIDO_INT_EN			] = 0x018,
	[VIDO_INT			] = 0x01c,
	[VIDO_CROP_OFST			] = 0x020,
	[VIDO_TAR_SIZE			] = 0x024,
	[VIDO_FRAME_SIZE		] = 0x028,
	[VIDO_DDREN_REQ			] = 0x02c,
	[VIDO_STRIDE			] = 0x030,
	[VIDO_BKGD			] = 0x034,
	[VIDO_SLC_SIDEBAND		] = 0x038,
	[VIDO_STRIDE_C			] = 0x03c,
	[VIDO_CTRL_2			] = 0x048,
	[VIDO_IN_LINE_ROT		] = 0x050,
	[VIDO_DITHER			] = 0x054,
	[VIDO_DITHER_CON		] = 0x058,
	[VIDO_SECURITY_DISABLE		] = 0x05c,
	[VIDO_INT_UP_EN			] = 0x060,
	[VIDO_INT_UP			] = 0x064,
	[VIDO_STRIDE_V			] = 0x06c,
	[VIDO_RSV_1			] = 0x070,
	[VIDO_DMA_PREULTRA		] = 0x074,
	[VIDO_IN_SIZE			] = 0x078,
	[VIDO_ROT_EN			] = 0x07c,
	[VIDO_FIFO_TEST			] = 0x080,
	[VIDO_MAT_CTRL			] = 0x084,
	[VIDO_SHADOW_CTRL		] = 0x08c,
	[VIDO_DEBUG			] = 0x0d0,
	[VIDO_PVRIC			] = 0x0d8,
	[VIDO_SCAN_10BIT		] = 0x0dc,
	[VIDO_PENDING_ZERO		] = 0x0e0,
	[VIDO_CRC_CTRL			] = 0x0e8,
	[VIDO_CRC_VALUE			] = 0x0ec,
	[VIDO_COMPRESION_VALUE		] = 0x0f0,
	[VIDO_DUMMY_NORMAL		] = 0x0f4,
	[VIDO_DUMMY_SECURE		] = 0x0f8,
	[VIDO_OFST_ADDR			] = 0x100,
	[VIDO_OFST_ADDR_HIGH		] = 0x104,
	[VIDO_OFST_ADDR_C		] = 0x108,
	[VIDO_OFST_ADDR_HIGH_C		] = 0x10c,
	[VIDO_OFST_ADDR_V		] = 0x110,
	[VIDO_OFST_ADDR_HIGH_V		] = 0x114,
	[VIDO_BASE_ADDR			] = 0x118,
	[VIDO_BASE_ADDR_HIGH		] = 0x11c,
	[VIDO_BASE_ADDR_C		] = 0x120,
	[VIDO_BASE_ADDR_HIGH_C		] = 0x124,
	[VIDO_BASE_ADDR_V		] = 0x128,
	[VIDO_BASE_ADDR_HIGH_V		] = 0x12c,
	[VIDO_CSC_COEFFICIENT_1		] = 0x138,
	[VIDO_CSC_COEFFICIENT_2		] = 0x13c,
	[VIDO_CSC_COEFFICIENT_3		] = 0x140,
	[VIDO_CSC_COEFFICIENT_4		] = 0x144,
	[VIDO_CSC_COEFFICIENT_5		] = 0x148,
	[VIDO_CSC_COEFFICIENT_6		] = 0x14c,
	[VIDO_CSC_COEFFICIENT_7		] = 0x150,
	[VIDO_PVRIC_FMT			] = 0xf0c,
	[VIDO_AFBC_YUVTRANS		] = 0xf2c,
	[VIDO_STASH_CMD_INTF		] = 0xf4c,
	[VIDO_STASH_CMD_FUNC_1		] = 0xf50,
	[VIDO_STASH_OFST_ADDR		] = 0xf54,
	[VIDO_STASH_OFST_ADDR_HIGH	] = 0xf58,
	[VIDO_STASH_OFST_ADDR_C		] = 0xf5c,
	[VIDO_STASH_OFST_ADDR_HIGH_C	] = 0xf60,
	[VIDO_STASH_OFST_ADDR_V		] = 0xf64,
	[VIDO_STASH_OFST_ADDR_HIGH_V	] = 0xf68,
	[VIDO_STASH_SW_ADDR		] = 0xf6c,
	[VIDO_STASH_SW_ADDR_HIGH	] = 0xf70,
	[VIDO_STASH_SW_WORK		] = 0xf74,
	[VIDO_STASH_DELAY_CNT		] = 0xf78,
};

#define WROT_MIN_BUF_LINE_NUM		16

/* register mask */
#define VIDO_INT_MASK			0x00000007
#define VIDO_INT_EN_MASK		0x00000007

/* Inline rot offsets */
#define DISPSYS_SHADOW_CTRL		0x010
#define INLINEROT_OVLSEL		0x030
#define INLINEROT_HEIGHT0		0x034
#define INLINEROT_HEIGHT1		0x038
#define INLINEROT_WDONE			0x03C

/* SMI offset */
#define SMI_LARB_NON_SEC_CON		0x380

#define MML_WROT_RACING_MAX		64

#define WROT_POLL_SLEEP_TIME_US		(10)
#define WROT_MAX_POLL_TIME_US		(1000)

/* debug option to change sram write height */
int mml_racing_h = MML_WROT_RACING_MAX;
module_param(mml_racing_h, int, 0644);

int mml_racing_rdone;
module_param(mml_racing_rdone, int, 0644);

/* 0x1 for input crc, 0xd for output crc */
int mml_wrot_crc;
module_param(mml_wrot_crc, int, 0644);

int mml_wrot_bkgd_en;
module_param(mml_wrot_bkgd_en, int, 0644);
int mml_wrot_bkgd;
module_param(mml_wrot_bkgd, int, 0644);

int wrot_stash_delay = 20;
module_param(wrot_stash_delay, int, 0644);

/* ceil_m and floor_m helper function */
static u32 ceil_m(u64 n, u64 d)
{
	u32 reminder = do_div((n), (d));

	return n + (reminder != 0);
}

static u32 floor_m(u64 n, u64 d)
{
	do_div(n, d);
	return n;
}

enum mml_pq_read_mode {
	MML_PQ_EOF_MODE = 0,
	MML_PQ_SOF_MODE,
};

enum wrot_label {
	WROT_LABEL_ADDR = 0,
	WROT_LABEL_ADDR_HIGH,
	WROT_LABEL_ADDR_C,
	WROT_LABEL_ADDR_HIGH_C,
	WROT_LABEL_ADDR_V,
	WROT_LABEL_ADDR_HIGH_V,
	WROT_LABEL_TOTAL
};

static s32 wrot_write_ofst(struct cmdq_pkt *pkt,
			   dma_addr_t addr, dma_addr_t addr_high, u64 value)
{
	s32 ret;

	ret = cmdq_pkt_write(pkt, NULL, addr, value & GENMASK_ULL(31, 0), U32_MAX);
	if (ret)
		return ret;
	ret = cmdq_pkt_write(pkt, NULL, addr_high, value >> 32, U32_MAX);
	return ret;
}

static s32 wrot_write_addr(u32 comp_id, struct cmdq_pkt *pkt,
			   dma_addr_t addr, dma_addr_t addr_high, u64 value,
			   struct mml_task_reuse *reuse,
			   struct mml_pipe_cache *cache,
			   u16 *label_idx)
{
	s32 ret;

	ret = mml_write(comp_id, pkt, addr, value & GENMASK_ULL(31, 0), U32_MAX,
			reuse, cache, label_idx);
	if (ret)
		return ret;

	ret = mml_write(comp_id, pkt, addr_high, value >> 32, U32_MAX,
			reuse, cache, label_idx + 1);
	return ret;
}

static void wrot_update_addr(u32 comp_id, struct mml_task_reuse *reuse,
			     u16 label, u16 label_high, u64 value)
{
	mml_update(comp_id, reuse, label, value & GENMASK_ULL(31, 0));
	mml_update(comp_id, reuse, label_high, value >> 32);
}

struct wrot_data {
	const u16 *reg;
	u32 fifo;
	u32 tile_width;
	u32 sram_size;
	u8 read_mode;
	u8 px_per_tick;
	u8 rb_swap;		/* WA: version for rb channel swap behavior */
	bool yuv_pending;	/* WA: enable wrot yuv422/420 pending zero */
	bool stash;		/* enable stash prefetch with leading time */
	bool ir_sram_bw;		/* sram channel bw */
};

static const struct wrot_data mt6983_wrot_data = {
	.reg = wrot_mt6989,
	.fifo = 256,
	.tile_width = 512,
	.sram_size = 512 * 1024,
	.rb_swap = 1,
	.px_per_tick = 1,
};

static const struct wrot_data mt6985_wrot_data = {
	.reg = wrot_mt6989,
	.fifo = 256,
	.tile_width = 512,
	.sram_size = 512 * 1024,
	.read_mode = MML_PQ_EOF_MODE,
	/* .rb_swap = 2 */
	.px_per_tick = 1,
};

static const struct wrot_data mt6989_wrot_data = {
	.reg = wrot_mt6989,
	.fifo = 256,
	.tile_width = 512,
	.sram_size = 512 * 1024,
	.read_mode = MML_PQ_SOF_MODE,
	.px_per_tick = 2,
	/* .rb_swap = 2 */
	.px_per_tick = 2,
	.yuv_pending = true,
};

static const struct wrot_data mt6878_wrot_data = {
	.reg = wrot_mt6989,
	.fifo = 256,
	.tile_width = 512,
	.sram_size = 512 * 1024,
	.px_per_tick = 1,
};

static const struct wrot_data mt6991_wrot_data = {
	.reg = wrot_mt6991,
	.fifo = 256,
	.tile_width = 512,
	.sram_size = 512 * 1024,
	.px_per_tick = 2,
	.read_mode = MML_PQ_SOF_MODE,
	.px_per_tick = 2,
	.yuv_pending = true,
	.stash = true,
};

static const struct wrot_data mt6899_wrot_data = {
	.reg = wrot_mt6989,
	.fifo = 256,
	.tile_width = 512,
	.sram_size = 512 * 1024,
	.read_mode = MML_PQ_SOF_MODE,
	.px_per_tick = 2,
	/* .rb_swap = 2 */
	.px_per_tick = 2,
	.yuv_pending = true,
	.ir_sram_bw = true,
};

struct mml_comp_wrot {
	struct mtk_ddp_comp ddp_comp;
	struct mml_comp comp;
	const struct wrot_data *data;
	const u16 *reg;
	bool ddp_bound;

	u16 event_eof;		/* wrot frame done */
	u16 event_bufa;		/* notify pipe0 that pipe1 ready buf a */
	u16 event_bufb;		/* notify pipe0 that pipe1 ready buf b */
	u16 event_buf_next;	/* notify pipe1 that pipe0 ready new round */
	int idx;

	struct device *mmu_dev;	/* for dmabuf to iova */
	struct device *mmu_dev_sec; /* for secure dmabuf to secure iova */
	/* smi register to config sram/dram mode */
	phys_addr_t smi_larb_con;
	/* inline rotate base addr */
	phys_addr_t irot_base[MML_PIPE_CNT];
	void __iomem *irot_va[MML_PIPE_CNT];

	u32 sram_size;
	u32 sram_cnt;
	u64 sram_pa;
	struct mutex sram_mutex;
	int irq;
	struct mml_pq_task *pq_task;
	/* TODO: will remove out in the feature */
	struct mml_pq_frame_data frame_data;
	bool dual;
	u32 jobid;
	u8 out_idx;
	u8 dest_cnt;
	struct workqueue_struct *wrot_ai_callback_wq;
	struct work_struct wrot_ai_callback_task;
};

/* meta data for each different frame config */
struct wrot_frame_data {
	/* 0 or 1 for 1st or 2nd out port */
	u8 out_idx;

	/* width and height before rotate */
	u32 out_w;
	u32 out_h;
	struct mml_rect compose;
	u32 y_stride;
	u32 uv_stride;
	u64 iova[MML_MAX_PLANES];
	u32 plane_offset[MML_MAX_PLANES];

	/* calculate in prepare and use as tile input */
	enum mml_orientation rotate;
	bool flip;
	bool en_x_crop;
	bool en_y_crop;
	struct mml_rect out_crop;
	u8 pending_x;
	u8 pending_y;

	/* following data calculate in init and use in tile command */
	u8 mat_en;
	u8 mat_sel;
	u32 dither_con;
	/* bits per pixel y */
	u32 bbp_y;
	/* bits per pixel uv */
	u32 bbp_uv;
	/* hor right shift uv */
	u32 hor_sh_uv;
	/* vert right shift uv */
	u32 ver_sh_uv;
	/* VIDO_FILT_TYPE_V Chroma down sample filter type */
	u32 filt_v;

	/* calculate in frame, use in each tile calc */
	u32 fifo_max_sz;
	u32 max_line_cnt;

	/* dvfs and qos */
	struct mml_frame_size max_size;
	u32 datasize;		/* qos data size in bytes */
	u32 stash_srt_bw;
	u32 stash_hrt_bw;
	u16 tile_last_x;
	u16 tile_last_y;

	struct {
		bool eol:1;	/* tile is end of current line */
		u8 sram:1;	/* sram ping pong idx of this tile */
	} wdone[256];
	u8 sram_side;		/* write to left/right ovl */

	/* array of indices to one of entry in cache entry list,
	 * use in reuse command
	 */
	u16 labels[WROT_LABEL_TOTAL];

	u32 wdone_cnt;
};

static inline struct wrot_frame_data *wrot_frm_data(struct mml_comp_config *ccfg)
{
	return ccfg->data;
}

static inline struct mml_comp_wrot *comp_to_wrot(struct mml_comp *comp)
{
	return container_of(comp, struct mml_comp_wrot, comp);
}

struct wrot_ofst_addr {
	u64 y;	/* wrot_y_ofst_adr for VIDO_OFST_ADDR */
	u64 c;	/* wrot_c_ofst_adr for VIDO_OFST_ADDR_C */
	u64 v;	/* wrot_v_ofst_adr for VIDO_OFST_ADDR_V */
};

/* different wrot setting between each tile */
struct wrot_setting {
	/* parameters for calculation */
	u32 tar_xsize;
	/* result settings */
	u32 main_blk_width;
	u32 main_buf_line_num;
};

struct check_buf_param {
	u32 y_buf_size;
	u32 uv_buf_size;
	u32 y_buf_check;
	u32 uv_buf_check;
	u32 y_buf_width;
	u32 y_buf_usage;
	u32 uv_blk_width;
	u32 uv_blk_line;
	u32 uv_buf_width;
	u32 uv_buf_usage;
};

/* filt_h, filt_v, uv_xsel, uv_ysel */
static const u32 uv_table[2][4][2][4] = {
	{	/* YUV422 */
		{	/* 0 */
			{ 1 /* [1 2 1] */, 0 /* drop  */, 0, 2 },
			{ 2 /* [1 2 1] */, 0 /* drop  */, 1, 2 }, /* flip */
		}, {	/* 90 */
			{ 0 /* drop    */, 4 /* [1 1] */, 2, 1 },
			{ 0 /* drop    */, 3 /* [1 1] */, 2, 0 }, /* flip */
		}, {	/* 180 */
			{ 2 /* [1 2 1] */, 0 /* drop  */, 1, 2 },
			{ 1 /* [1 2 1] */, 0 /* drop  */, 0, 2 }, /* flip */
		}, {	/* 270 */
			{ 0 /* drop    */, 3 /* [1 1] */, 2, 0 },
			{ 0 /* drop    */, 4 /* [1 1] */, 2, 1 }, /* flip */
		},
	}, {	/* YUV420 */
		{	/* 0 */
			{ 1 /* [1 2 1] */, 3 /* [1 1] */, 0, 0 },
			{ 2 /* [1 2 1] */, 3 /* [1 1] */, 1, 0 }, /* flip */
		}, {	/* 90 */
			{ 1 /* [1 2 1] */, 4 /* [1 1] */, 0, 1 },
			{ 1 /* [1 2 1] */, 3 /* [1 1] */, 0, 0 }, /* flip */
		}, {	/* 180 */
			{ 2 /* [1 2 1] */, 4 /* [1 1] */, 1, 1 },
			{ 1 /* [1 2 1] */, 4 /* [1 1] */, 0, 1 }, /* flip */
		}, {	/* 270 */
			{ 2 /* [1 2 1] */, 3 /* [1 1] */, 1, 0 },
			{ 2 /* [1 2 1] */, 4 /* [1 1] */, 1, 1 }, /* flip */
		},
	}
};

static bool is_change_wx(u16 r, bool f)
{
	return ((r == MML_ROT_0 && f) ||
		(r == MML_ROT_180 && !f) ||
		r == MML_ROT_270);
}

static bool is_change_hy(u16 r, bool f)
{
	return ((r == MML_ROT_90 && !f) ||
		r == MML_ROT_180 ||
		(r == MML_ROT_270 && f));
}

static void wrot_config_left(struct mml_frame_dest *dest,
			     const struct mml_rect *out_crop,
			     struct wrot_frame_data *wrot_frm)
{
	wrot_frm->en_x_crop = true;
	wrot_frm->out_crop.left = 0;
	wrot_frm->out_crop.width = out_crop->width >> 1;

	if (MML_FMT_AFBC_ARGB(dest->data.format))
		wrot_frm->out_crop.width = round_up(wrot_frm->out_crop.width, 32);
	else if (MML_FMT_10BIT_PACKED(dest->data.format))
		wrot_frm->out_crop.width = round_up(wrot_frm->out_crop.width, 4);
	else if (wrot_frm->out_crop.width & 1)
		wrot_frm->out_crop.width++; /* round_up(2) */

	if (is_change_wx(wrot_frm->rotate, wrot_frm->flip))
		wrot_frm->out_crop.width = out_crop->width - wrot_frm->out_crop.width;
}

static void wrot_config_right(struct mml_frame_dest *dest,
			      const struct mml_rect *out_crop,
			      struct wrot_frame_data *wrot_frm)
{
	wrot_frm->en_x_crop = true;
	wrot_frm->out_crop.left = out_crop->width >> 1;

	if (MML_FMT_AFBC_ARGB(dest->data.format))
		wrot_frm->out_crop.left = round_up(wrot_frm->out_crop.left, 32);
	else if (MML_FMT_10BIT_PACKED(dest->data.format))
		wrot_frm->out_crop.left = round_up(wrot_frm->out_crop.left, 4);
	else if (wrot_frm->out_crop.left & 1)
		wrot_frm->out_crop.left++; /* round_up(2) */

	if (is_change_wx(wrot_frm->rotate, wrot_frm->flip))
		wrot_frm->out_crop.left = out_crop->width - wrot_frm->out_crop.left;
	wrot_frm->out_crop.width = out_crop->width - wrot_frm->out_crop.left;
}

static void wrot_config_top(struct mml_frame_data *src,
			    const struct mml_rect *out_crop,
			    struct wrot_frame_data *wrot_frm)
{
	wrot_frm->en_y_crop = true;
	wrot_frm->out_crop.top = 0;
	wrot_frm->out_crop.height = out_crop->height >> 1;

	if (MML_FMT_IS_YUV(src->format) || MML_FMT_COMPRESS(src->format))
		wrot_frm->out_crop.height = round_up(wrot_frm->out_crop.height, 16);
	else if (wrot_frm->out_crop.height & 1)
		wrot_frm->out_crop.height++; /* round_up(2) */

	if (wrot_frm->pending_y && is_change_hy(wrot_frm->rotate, wrot_frm->flip))
		wrot_frm->out_crop.height = out_crop->height - wrot_frm->out_crop.height;
	wrot_frm->out_crop.width = out_crop->width;
}

static void wrot_config_bottom(struct mml_frame_data *src,
			       const struct mml_rect *out_crop,
			       struct wrot_frame_data *wrot_frm)
{
	wrot_frm->en_y_crop = true;
	wrot_frm->out_crop.top = out_crop->height >> 1;

	if (MML_FMT_IS_YUV(src->format) || MML_FMT_COMPRESS(src->format))
		wrot_frm->out_crop.top = round_up(wrot_frm->out_crop.top, 16);
	else if (wrot_frm->out_crop.top & 1)
		wrot_frm->out_crop.top++; /* round_up(2) */

	if (wrot_frm->pending_y && is_change_hy(wrot_frm->rotate, wrot_frm->flip))
		wrot_frm->out_crop.top = out_crop->height - wrot_frm->out_crop.top;
	wrot_frm->out_crop.height = out_crop->height - wrot_frm->out_crop.top;
	wrot_frm->out_crop.width = out_crop->width;
}

static void wrot_config_pipe0(struct mml_frame_config *cfg,
			      struct mml_frame_dest *dest,
			      const struct mml_rect *out_crop,
			      struct wrot_frame_data *wrot_frm)
{
	if (cfg->info.mode == MML_MODE_RACING) {
		if ((wrot_frm->rotate == MML_ROT_90 && !wrot_frm->flip) ||
		    (wrot_frm->rotate == MML_ROT_270 && wrot_frm->flip))
			wrot_config_bottom(&cfg->info.src, out_crop, wrot_frm);
		else if ((wrot_frm->rotate == MML_ROT_90 && wrot_frm->flip) ||
			 (wrot_frm->rotate == MML_ROT_270 && !wrot_frm->flip))
			wrot_config_top(&cfg->info.src, out_crop, wrot_frm);
		else if ((wrot_frm->rotate == MML_ROT_0 && !wrot_frm->flip) ||
			 (wrot_frm->rotate == MML_ROT_180 && wrot_frm->flip))
			wrot_config_left(dest, out_crop, wrot_frm);
		else if ((wrot_frm->rotate == MML_ROT_0 && wrot_frm->flip) ||
			 (wrot_frm->rotate == MML_ROT_180 && !wrot_frm->flip))
			wrot_config_right(dest, out_crop, wrot_frm);
	} else {
		wrot_config_left(dest, out_crop, wrot_frm);
	}
}

static void wrot_config_pipe1(struct mml_frame_config *cfg,
			      struct mml_frame_dest *dest,
			      const struct mml_rect *out_crop,
			      struct wrot_frame_data *wrot_frm)
{
	if (cfg->info.mode == MML_MODE_RACING) {
		if ((wrot_frm->rotate == MML_ROT_90 && !wrot_frm->flip) ||
		    (wrot_frm->rotate == MML_ROT_270 && wrot_frm->flip))
			wrot_config_top(&cfg->info.src, out_crop, wrot_frm);
		else if ((wrot_frm->rotate == MML_ROT_90 && wrot_frm->flip) ||
			 (wrot_frm->rotate == MML_ROT_270 && !wrot_frm->flip))
			wrot_config_bottom(&cfg->info.src, out_crop, wrot_frm);
		else if ((wrot_frm->rotate == MML_ROT_0 && !wrot_frm->flip) ||
			 (wrot_frm->rotate == MML_ROT_180 && wrot_frm->flip))
			wrot_config_right(dest, out_crop, wrot_frm);
		else if ((wrot_frm->rotate == MML_ROT_0 && wrot_frm->flip) ||
			 (wrot_frm->rotate == MML_ROT_180 && !wrot_frm->flip))
			wrot_config_left(dest, out_crop, wrot_frm);
	} else {
		wrot_config_right(dest, out_crop, wrot_frm);
	}
}

static void wrot_config_smi(struct mml_comp_wrot *wrot,
	struct mml_frame_config *cfg, struct cmdq_pkt *pkt)
{
	const enum mml_mode mode = cfg->info.mode;
	u32 mask = GENMASK(19, 16) | (0x1 << 3);
	u32 value;

	if (!wrot->smi_larb_con)
		return;

	/* config smi addr to emi (iova) or sram, and bw throttling */
	if (mode == MML_MODE_RACING)
		value = 0xf << 16;
	else if (mode == MML_MODE_MML_DECOUPLE)
		value = 0x1 << 3;
	else
		value = 0;

	cmdq_pkt_write(pkt, NULL, wrot->smi_larb_con, value, mask);
}

static s32 wrot_prepare(struct mml_comp *comp, struct mml_task *task,
			struct mml_comp_config *ccfg)
{
	struct mml_comp_wrot *wrot = comp_to_wrot(comp);
	struct mml_frame_config *cfg = task->config;
	struct wrot_frame_data *wrot_frm;
	struct mml_frame_dest *dest;
	struct mml_rect out_crop;
	u8 i;

	/* initialize component frame data for current frame config */
	wrot_frm = kzalloc(sizeof(*wrot_frm), GFP_KERNEL);
	ccfg->data = wrot_frm;

	/* cache out index for easy use */
	wrot_frm->out_idx = ccfg->node->out_idx;
	wrot_frm->rotate = cfg->out_rotate[wrot_frm->out_idx];
	wrot_frm->flip = cfg->out_flip[wrot_frm->out_idx];

	/* select output port struct */
	dest = &cfg->info.dest[wrot_frm->out_idx];

	wrot_frm->compose = dest->compose;
	wrot_frm->y_stride = dest->data.y_stride;
	wrot_frm->uv_stride = dest->data.uv_stride;
	if (wrot_frm->rotate == MML_ROT_0 || wrot_frm->rotate == MML_ROT_180) {
		wrot_frm->out_w = dest->data.width;
		wrot_frm->out_h = dest->data.height;
	} else {
		wrot_frm->out_w = dest->data.height;
		wrot_frm->out_h = dest->data.width;
		swap(wrot_frm->compose.width, wrot_frm->compose.height);
	}

	/* make sure uv stride data */
	if (MML_FMT_PLANE(dest->data.format) > 1 && !wrot_frm->uv_stride)
		wrot_frm->uv_stride = mml_color_get_min_uv_stride(
			dest->data.format, dest->data.width);

	/* plane offset for later use */
	for (i = 0; i < task->buf.dest[wrot_frm->out_idx].cnt; i++)
		wrot_frm->plane_offset[i] = dest->data.plane_offset[i];

	out_crop.left = 0;
	out_crop.top = 0;
	if (wrot->data->yuv_pending && cfg->info.mode != MML_MODE_RACING) {
		out_crop.width = wrot_frm->compose.width;
		out_crop.height = wrot_frm->compose.height;
		if (MML_FMT_H_SUBSAMPLE(dest->data.format) &&
		    !MML_FMT_COMPRESS(dest->data.format)) {
			/* Enable YUV422/420 pending zero on compose != dest */
			wrot_frm->pending_x = wrot_frm->out_w - wrot_frm->compose.width;
			wrot_frm->pending_y = wrot_frm->out_h - wrot_frm->compose.height;
		}
	} else {
		out_crop.width = wrot_frm->out_w;
		out_crop.height = wrot_frm->out_h;
	}

	if (cfg->dual) {
		if (ccfg->pipe == 0)
			wrot_config_pipe0(cfg, dest, &out_crop, wrot_frm);
		else
			wrot_config_pipe1(cfg, dest, &out_crop, wrot_frm);

		if (cfg->info.mode == MML_MODE_RACING) {
			if (wrot_frm->rotate == MML_ROT_0)
				wrot_frm->sram_side = ccfg->pipe;
			else if (wrot_frm->rotate == MML_ROT_90)
				wrot_frm->sram_side = !ccfg->pipe;
			else if (wrot_frm->rotate == MML_ROT_180)
				wrot_frm->sram_side = !ccfg->pipe;
			else
				wrot_frm->sram_side = ccfg->pipe;

			if (wrot_frm->flip)
				wrot_frm->sram_side = !wrot_frm->sram_side;

			mml_msg("%s wrot pipe %u out crop %u %u %u %u",
				__func__, ccfg->pipe,
				wrot_frm->out_crop.left, wrot_frm->out_crop.top,
				wrot_frm->out_crop.width, wrot_frm->out_crop.height);
		}
	} else {
		/* assign full frame */
		wrot_frm->en_x_crop = true;
		wrot_frm->out_crop = out_crop;
	}

	return 0;
}

static s32 wrot_buf_map(struct mml_comp *comp, struct mml_task *task,
			const struct mml_path_node *node)
{
	struct mml_comp_wrot *wrot = comp_to_wrot(comp);
	struct mml_frame_config *cfg = task->config;
	struct mml_file_buf *dest_buf = &task->buf.dest[node->out_idx];
	s32 ret = 0;

	mml_trace_ex_begin("%s", __func__);

	if (cfg->info.mode == MML_MODE_RACING) {
	} else if (!dest_buf->dma[0].iova) {

		mml_mmp(buf_map, MMPROFILE_FLAG_START,
			((u64)task->job.jobid << 16) | comp->id, 0);

		/* get iova */
		ret = mml_buf_iova_get(cfg->info.src.secure ? wrot->mmu_dev_sec : wrot->mmu_dev,
			dest_buf);
		if (ret < 0)
			mml_err("%s iova fail %d", __func__, ret);

		mml_mmp(buf_map, MMPROFILE_FLAG_END,
			((u64)task->job.jobid << 16) | comp->id,
			(unsigned long)dest_buf->dma[0].iova);
	}

	mml_trace_ex_end();

	mml_msg("%s comp %u dma %p iova %#11llx (%u) %#11llx (%u) %#11llx (%u)",
		__func__, comp->id, dest_buf->dma[0].dmabuf,
		dest_buf->dma[0].iova,
		dest_buf->size[0],
		dest_buf->dma[1].iova,
		dest_buf->size[1],
		dest_buf->dma[2].iova,
		dest_buf->size[2]);

	return ret;
}

static s32 wrot_buf_prepare(struct mml_comp *comp, struct mml_task *task,
			    struct mml_comp_config *ccfg)
{
	struct mml_comp_wrot *wrot = comp_to_wrot(comp);
	struct wrot_frame_data *wrot_frm = wrot_frm_data(ccfg);
	struct mml_file_buf *dest_buf = &task->buf.dest[wrot_frm->out_idx];
	u32 i;

	if (task->config->info.mode == MML_MODE_RACING) {
		/* assign sram pa directly */
		mml_mmp(buf_prepare, MMPROFILE_FLAG_START,
			((u64)task->job.jobid << 16) | comp->id, 0);
		mutex_lock(&wrot->sram_mutex);
		if (!wrot->sram_cnt)
			wrot->sram_pa = (u64)mml_sram_get(task->config->mml, mml_sram_racing);
		wrot->sram_cnt++;
		/* store it for mml record */
		task->buf.dest[wrot_frm->out_idx].dma[0].iova = wrot->sram_pa;
		task->buf.dest[wrot_frm->out_idx].size[0] = wrot->sram_size;
		mutex_unlock(&wrot->sram_mutex);
		wrot_frm->iova[0] = wrot->sram_pa;
		mml_mmp(buf_prepare, MMPROFILE_FLAG_END,
			((u64)task->job.jobid << 16) | comp->id, wrot_frm->iova[0]);
	} else {
		for (i = 0; i < dest_buf->cnt; i++)
			wrot_frm->iova[i] = dest_buf->dma[i].iova;
	}

	if (!wrot_frm->iova[0])
		return -EINVAL;

	return 0;
}

static void wrot_buf_unprepare(struct mml_comp *comp, struct mml_task *task,
			       struct mml_comp_config *ccfg)
{
	struct mml_comp_wrot *wrot = comp_to_wrot(comp);

	if (task->config->info.mode == MML_MODE_RACING) {
		mutex_lock(&wrot->sram_mutex);
		wrot->sram_cnt--;
		if (wrot->sram_cnt == 0)
			mml_sram_put(task->config->mml, mml_sram_racing);
		mutex_unlock(&wrot->sram_mutex);
	}
}

static s32 wrot_tile_prepare(struct mml_comp *comp, struct mml_task *task,
			     struct mml_comp_config *ccfg,
			     struct tile_func_block *func,
			     union mml_tile_data *data)
{
	struct wrot_frame_data *wrot_frm = wrot_frm_data(ccfg);
	struct mml_frame_config *cfg = task->config;
	struct mml_frame_dest *dest = &cfg->info.dest[wrot_frm->out_idx];
	struct mml_comp_wrot *wrot = comp_to_wrot(comp);

	data->wrot.dest_fmt = dest->data.format;
	data->wrot.rotate = wrot_frm->rotate;
	data->wrot.flip = wrot_frm->flip;
	data->wrot.alpha = cfg->alpharot || cfg->alpharsz;
	data->wrot.racing = cfg->info.mode == MML_MODE_RACING;
	data->wrot.racing_h = max(mml_racing_h, MML_WROT_RACING_MAX);

	/* reuse wrot_frm data which processed with rotate and dual */
	data->wrot.enable_x_crop = wrot_frm->en_x_crop;
	data->wrot.enable_y_crop = wrot_frm->en_y_crop;
	data->wrot.crop = wrot_frm->out_crop;
	data->wrot.yuv_pending = (wrot->data->yuv_pending && cfg->info.mode != MML_MODE_RACING);
	if (wrot->data->yuv_pending && cfg->info.mode != MML_MODE_RACING) {
		func->full_size_x_in = wrot_frm->compose.width;
		func->full_size_y_in = wrot_frm->compose.height;
		func->full_size_x_out = wrot_frm->compose.width;
		func->full_size_y_out = wrot_frm->compose.height;
	} else {
		func->full_size_x_in = wrot_frm->out_w;
		func->full_size_y_in = wrot_frm->out_h;
		func->full_size_x_out = wrot_frm->out_w;
		func->full_size_y_out = wrot_frm->out_h;
	}

	data->wrot.max_width = wrot->data->tile_width;
	/* WROT support crop capability */
	func->type = TILE_TYPE_WDMA | TILE_TYPE_CROP_EN;
	func->init_func = tile_wrot_init;
	func->for_func = tile_wrot_for;
	func->back_func = tile_wrot_back;
	func->data = data;
	func->enable_flag = true;

	return 0;
}

static const struct mml_comp_tile_ops wrot_tile_ops = {
	.prepare = wrot_tile_prepare,
};

static u32 wrot_get_label_count(struct mml_comp *comp, struct mml_task *task,
				struct mml_comp_config *ccfg)
{
	return WROT_LABEL_TOTAL;
}

static void wrot_color_fmt(struct mml_frame_config *cfg,
			   struct wrot_frame_data *wrot_frm)
{
	u32 fmt = cfg->info.dest[wrot_frm->out_idx].data.format;
	u16 profile_in = cfg->info.src.profile;
	u16 profile_out = cfg->info.dest[wrot_frm->out_idx].data.profile;

	wrot_frm->mat_en = 0;
	wrot_frm->mat_sel = 15;
	wrot_frm->bbp_y = MML_FMT_BITS_PER_PIXEL(fmt);

	switch (fmt) {
	case MML_FMT_GREY:
		/* Y only */
		wrot_frm->bbp_uv = 0;
		wrot_frm->hor_sh_uv = 0;
		wrot_frm->ver_sh_uv = 0;
		break;
	case MML_FMT_RGB565:
	case MML_FMT_BGR565:
	case MML_FMT_RGB888:
	case MML_FMT_BGR888:
	case MML_FMT_RGBA8888:
	case MML_FMT_BGRA8888:
	case MML_FMT_ARGB8888:
	case MML_FMT_ABGR8888:
	/* HW_SUPPORT_10BIT_PATH */
	case MML_FMT_RGBA1010102:
	case MML_FMT_BGRA1010102:
	case MML_FMT_ARGB2101010:
	case MML_FMT_ABGR2101010:
	/* DMA_SUPPORT_AFBC */
	case MML_FMT_RGBA8888_AFBC:
	case MML_FMT_RGBA1010102_AFBC:
		wrot_frm->bbp_uv = 0;
		wrot_frm->hor_sh_uv = 0;
		wrot_frm->ver_sh_uv = 0;
		wrot_frm->mat_en = 1;
		break;
	case MML_FMT_UYVY:
	case MML_FMT_VYUY:
	case MML_FMT_YUYV:
	case MML_FMT_YVYU:
	case MML_FMT_YUVA8888:
	case MML_FMT_AYUV8888:
	/* HW_SUPPORT_10BIT_PATH */
	case MML_FMT_YUVA1010102:
		/* YUV422/444, 1 plane */
		wrot_frm->bbp_uv = 0;
		wrot_frm->hor_sh_uv = 0;
		wrot_frm->ver_sh_uv = 0;
		break;
	case MML_FMT_I420:
	case MML_FMT_YV12:
		/* YUV420, 3 plane */
		wrot_frm->bbp_uv = 8;
		wrot_frm->hor_sh_uv = 1;
		wrot_frm->ver_sh_uv = 1;
		break;
	case MML_FMT_I422:
	case MML_FMT_YV16:
		/* YUV422, 3 plane */
		wrot_frm->bbp_uv = 8;
		wrot_frm->hor_sh_uv = 1;
		wrot_frm->ver_sh_uv = 0;
		break;
	case MML_FMT_I444:
	case MML_FMT_YV24:
		/* YUV444, 3 plane */
		wrot_frm->bbp_uv = 8;
		wrot_frm->hor_sh_uv = 0;
		wrot_frm->ver_sh_uv = 0;
		break;
	case MML_FMT_NV12:
	case MML_FMT_NV21:
		/* YUV420, 2 plane */
		wrot_frm->bbp_uv = 16;
		wrot_frm->hor_sh_uv = 1;
		wrot_frm->ver_sh_uv = 1;
		break;
	case MML_FMT_NV16:
	case MML_FMT_NV61:
		/* YUV422, 2 plane */
		wrot_frm->bbp_uv = 16;
		wrot_frm->hor_sh_uv = 1;
		wrot_frm->ver_sh_uv = 0;
		break;
	/* HW_SUPPORT_10BIT_PATH */
	case MML_FMT_NV12_10L:
	case MML_FMT_NV21_10L:
		/* P010 YUV420, 2 plane 10bit */
		wrot_frm->bbp_uv = 32;
		wrot_frm->hor_sh_uv = 1;
		wrot_frm->ver_sh_uv = 1;
		break;
	case MML_FMT_NV15:
	case MML_FMT_NV51:
		/* MTK packed YUV420, 2 plane 10bit */
		wrot_frm->bbp_uv = 20;
		wrot_frm->hor_sh_uv = 1;
		wrot_frm->ver_sh_uv = 1;
		break;
	default:
		mml_err("[wrot] not support format %x", fmt);
		break;
	}

	/*
	 * 4'b0000:  0 RGB to JPEG
	 * 4'b0001:  1 RGB to FULL709
	 * 4'b0010:  2 RGB to BT601
	 * 4'b0011:  3 RGB to BT709
	 * 4'b0100:  4 JPEG to RGB
	 * 4'b0101:  5 FULL709 to RGB
	 * 4'b0110:  6 BT601 to RGB
	 * 4'b0111:  7 BT709 to RGB
	 * 4'b1000:  8 JPEG to BT601 / FULL709 to BT709
	 * 4'b1001:  9 JPEG to BT709
	 * 4'b1010: 10 BT601 to JPEG / BT709 to FULL709
	 * 4'b1011: 11 BT709 to JPEG
	 * 4'b1100: 12 BT709 to BT601
	 * 4'b1101: 13 BT601 to BT709
	 * 4'b1110: 14 JPEG to FULL709
	 * 4'b1111: 15 IDENTITY
	 *             FULL709 to JPEG
	 *             FULL709 to BT601
	 *             BT601 to FULL709
	 */
	if (profile_in == MML_YCBCR_PROFILE_BT2020 ||
	    profile_in == MML_YCBCR_PROFILE_FULL_BT2020)
		profile_in = MML_YCBCR_PROFILE_BT709;

	if (wrot_frm->mat_en == 1) {
		if (MML_FMT_IS_RGB(cfg->info.src.format) &&
		    !cfg->info.dest[wrot_frm->out_idx].pq_config.en)
			wrot_frm->mat_sel = 5;
		else if (profile_in == MML_YCBCR_PROFILE_BT601)
			wrot_frm->mat_sel = 6;
		else if (profile_in == MML_YCBCR_PROFILE_BT709)
			wrot_frm->mat_sel = 7;
		else if (profile_in == MML_YCBCR_PROFILE_JPEG)
			wrot_frm->mat_sel = 4;
		else if (profile_in == MML_YCBCR_PROFILE_FULL_BT709)
			wrot_frm->mat_sel = 5;
		else
			mml_err("[wrot] unknown profile conversion %x",
				profile_in);
	} else {
		if (profile_in == MML_YCBCR_PROFILE_JPEG &&
		    profile_out == MML_YCBCR_PROFILE_BT601) {
			wrot_frm->mat_en = 1;
			wrot_frm->mat_sel = 8;
		} else if (profile_in == MML_YCBCR_PROFILE_JPEG &&
			   profile_out == MML_YCBCR_PROFILE_BT709) {
			wrot_frm->mat_en = 1;
			wrot_frm->mat_sel = 9;
		} else if (profile_in == MML_YCBCR_PROFILE_BT601 &&
			   profile_out == MML_YCBCR_PROFILE_JPEG) {
			wrot_frm->mat_en = 1;
			wrot_frm->mat_sel = 10;
		} else if (profile_in == MML_YCBCR_PROFILE_BT709 &&
			   profile_out == MML_YCBCR_PROFILE_JPEG) {
			wrot_frm->mat_en = 1;
			wrot_frm->mat_sel = 11;
		} else if (profile_in == MML_YCBCR_PROFILE_BT709 &&
			   profile_out == MML_YCBCR_PROFILE_BT601) {
			wrot_frm->mat_en = 1;
			wrot_frm->mat_sel = 12;
		} else if (profile_in == MML_YCBCR_PROFILE_BT601 &&
			   profile_out == MML_YCBCR_PROFILE_BT709) {
			wrot_frm->mat_en = 1;
			wrot_frm->mat_sel = 13;
		} else if (profile_in == MML_YCBCR_PROFILE_JPEG &&
			   profile_out == MML_YCBCR_PROFILE_FULL_BT709) {
			wrot_frm->mat_en = 1;
			wrot_frm->mat_sel = 14;
		} else if (profile_in == MML_YCBCR_PROFILE_FULL_BT709 &&
			   profile_out == MML_YCBCR_PROFILE_BT709) {
			wrot_frm->mat_en = 1;
			wrot_frm->mat_sel = 8;
		} else if (profile_in == MML_YCBCR_PROFILE_BT709 &&
			   profile_out == MML_YCBCR_PROFILE_FULL_BT709) {
			wrot_frm->mat_en = 1;
			wrot_frm->mat_sel = 10;
		}
	}

	/* Enable 10-bit input */
	if (!MML_FMT_10BIT(fmt)) {
		wrot_frm->mat_en = 1;
		/* Enable 10-to-8 dither */
		if (MML_FMT_10BIT(cfg->info.src.format)) {
			wrot_frm->dither_con = (0x1 << 10) +
				 (0x0 << 8) +
				 (0x0 << 4) +
				 (0x1 << 2) +
				 (0x1 << 1) +
				 (0x1 << 0);
		}
	}
}

static void calc_plane_offset(u32 left, u32 top,
			      u32 y_stride, u32 uv_stride,
			      u32 bbp_y, u32 bbp_uv,
			      u32 hor_sh_uv, u32 ver_sh_uv,
			      u32 *offset)
{
	if (!left && !top)
		return;

	offset[0] += (left * bbp_y >> 3) + (y_stride * top);

	offset[1] += (left >> hor_sh_uv) * (bbp_uv >> 3) +
		     (top >> ver_sh_uv) * uv_stride;

	offset[2] += (left >> hor_sh_uv) * (bbp_uv >> 3) +
		     (top >> hor_sh_uv) * uv_stride;
}

static void calc_afbc_block(u32 bits_per_pixel, u32 y_stride, u32 vert_stride,
			    u64 *iova, u32 *offset,
			    u32 *block_x, u64 *addr_c, u64 *addr_v, u64 *addr)
{
	u32 block_y;
	u64 header_sz;

	*block_x = ((y_stride << 3) / bits_per_pixel + 31) >> 5;
	block_y = (vert_stride + 7) >> 3;
	header_sz = ((((*block_x * block_y) << 4) + 1023) >> 10) << 10;

	*addr_c = iova[0] + offset[0];
	*addr_v = iova[2] + offset[2];
	*addr = *addr_c + header_sz;
}

static void wrot_calc_hw_buf_setting(const struct mml_comp_wrot *wrot,
				     const struct mml_frame_config *cfg,
				     const struct mml_frame_dest *dest,
				     struct wrot_frame_data *wrot_frm)
{
	const u32 dest_fmt = dest->data.format;

	if (MML_FMT_YUV422(dest_fmt)) {
		if (MML_FMT_PLANE(dest_fmt) == 1) {
			wrot_frm->fifo_max_sz = wrot->data->tile_width * 32;
			wrot_frm->max_line_cnt = 32;
		} else {
			wrot_frm->fifo_max_sz = wrot->data->tile_width * 48;
			wrot_frm->max_line_cnt = 48;
		}
	} else if (MML_FMT_YUV420(dest_fmt)) {
		wrot_frm->fifo_max_sz = wrot->data->tile_width * 64;
		wrot_frm->max_line_cnt = 64;
	} else if (dest_fmt == MML_FMT_GREY) {
		wrot_frm->fifo_max_sz = wrot->data->tile_width * 64;
		wrot_frm->max_line_cnt = 64;
	} else if (cfg->alpharot || cfg->alpharsz) {
		wrot_frm->fifo_max_sz = wrot->data->tile_width * 16;
		wrot_frm->max_line_cnt = 16;
	} else {
		wrot_frm->fifo_max_sz = wrot->data->tile_width * 32;
		wrot_frm->max_line_cnt = 32;
	}
}

static void wrot_config_addr(const struct mml_comp_wrot *wrot,
			     const struct mml_frame_dest *dest,
			     const u32 dest_fmt,
			     const phys_addr_t base_pa,
			     struct wrot_frame_data *wrot_frm,
			     struct cmdq_pkt *pkt,
			     struct mml_task_reuse *reuse,
			     struct mml_pipe_cache *cache)
{
	u64 addr_c, addr_v, addr;

	if (MML_FMT_AFBC(dest_fmt)) {
		/* wrot afbc output case */
		u32 block_x;
		u32 frame_size;

		/* Write frame base address */
		calc_afbc_block(wrot_frm->bbp_y,
				wrot_frm->y_stride, dest->data.vert_stride,
				wrot_frm->iova, wrot_frm->plane_offset,
				&block_x, &addr_c, &addr_v, &addr);

		if (wrot_frm->rotate == MML_ROT_0 || wrot_frm->rotate == MML_ROT_180)
			frame_size = ((((wrot_frm->out_h + 31) >>
					 5) << 5) << 16) +
					 ((block_x << 5) << 0);
		else
			frame_size = ((((wrot_frm->out_w + 31) >>
					 5) << 5) << 16) +
					 ((block_x << 5) << 0);
		cmdq_pkt_write(pkt, NULL, base_pa + wrot->reg[VIDO_FRAME_SIZE],
			       frame_size, U32_MAX);

		cmdq_pkt_write(pkt, NULL, base_pa + wrot->reg[VIDO_AFBC_YUVTRANS],
			       MML_FMT_IS_RGB(dest_fmt), 0x1);
	} else {
		mml_msg("%s base %#llx+%u %#llx+%u %#llx+%u",
			__func__,
			wrot_frm->iova[0], wrot_frm->plane_offset[0],
			wrot_frm->iova[1], wrot_frm->plane_offset[1],
			wrot_frm->iova[2], wrot_frm->plane_offset[2]);

		addr = wrot_frm->iova[0] + wrot_frm->plane_offset[0];
		addr_c = wrot_frm->iova[1] + wrot_frm->plane_offset[1];
		addr_v = wrot_frm->iova[2] + wrot_frm->plane_offset[2];
	}

	if (!mml_slt) {
		/* Write frame base address */
		wrot_write_addr(wrot->comp.id, pkt,
				base_pa + wrot->reg[VIDO_BASE_ADDR],
				base_pa + wrot->reg[VIDO_BASE_ADDR_HIGH], addr,
				reuse, cache, &wrot_frm->labels[WROT_LABEL_ADDR]);
		wrot_write_addr(wrot->comp.id, pkt,
				base_pa + wrot->reg[VIDO_BASE_ADDR_C],
				base_pa + wrot->reg[VIDO_BASE_ADDR_HIGH_C], addr_c,
				reuse, cache, &wrot_frm->labels[WROT_LABEL_ADDR_C]);
		wrot_write_addr(wrot->comp.id, pkt,
				base_pa + wrot->reg[VIDO_BASE_ADDR_V],
				base_pa + wrot->reg[VIDO_BASE_ADDR_HIGH_V], addr_v,
				reuse, cache, &wrot_frm->labels[WROT_LABEL_ADDR_V]);
	}
}

static void wrot_config_ready(struct mml_comp_wrot *wrot,
	struct mml_frame_config *cfg, u32 pipe, struct cmdq_pkt *pkt,
	bool enable)
{
	const struct mml_topology_path *path = cfg->path[pipe];
	phys_addr_t sel;
	u32 shift, mask;
	u32 sel_off = mml_sys_get_reg_ready_sel(path->mmlsys);

	if (!sel_off)
		return;
	sel = path->mmlsys->base_pa + sel_off;

	if (wrot->idx == 0)
		shift = 0;
	else if (wrot->idx == 1)
		shift = 3;
	else
		return;
	mask = cfg->dual ? (0x7 << shift) | GENMASK(31, 6) : U32_MAX;

	if (mml_racing_rdone) {
		/* debug mode, make rdone to wrot tie 1 */
		cmdq_pkt_write(pkt, NULL, sel, 0x24, U32_MAX);
		return;
	}

	if (!enable) {
		cmdq_pkt_write(pkt, NULL, sel, 0, mask);
	} else if (cfg->disp_dual) {
		/* 1:2 or 2:2 monitor both disp0 and disp1, mdpsys merge ready */
		cmdq_pkt_write(pkt, NULL, sel, 2 << shift, mask);
	} else {
		/* 1:1 or 2:1 monitor only disp0 */
		cmdq_pkt_write(pkt, NULL, sel, 0, mask);
	}
}

static s32 wrot_config_frame(struct mml_comp *comp, struct mml_task *task,
			     struct mml_comp_config *ccfg)
{
	struct mml_comp_wrot *wrot = comp_to_wrot(comp);
	struct mml_frame_config *cfg = task->config;
	struct wrot_frame_data *wrot_frm = wrot_frm_data(ccfg);
	struct mml_frame_dest *dest = &cfg->info.dest[wrot_frm->out_idx];
	struct cmdq_pkt *pkt = task->pkts[ccfg->pipe];
	struct mml_task_reuse *reuse = &task->reuse[ccfg->pipe];
	struct mml_pipe_cache *cache = &cfg->cache[ccfg->pipe];

	const phys_addr_t base_pa = comp->base_pa;
	const u32 src_fmt = cfg->info.src.format;
	const u32 dest_fmt = dest->data.format;
	const u16 rotate = wrot_frm->rotate;
	const u8 flip = wrot_frm->flip ? 1 : 0;
	const u32 h_subsample = MML_FMT_H_SUBSAMPLE(dest_fmt);
	const u32 v_subsample = MML_FMT_V_SUBSAMPLE(dest_fmt);
	const u8 plane = MML_FMT_PLANE(dest_fmt);
	const u32 preultra_en = 1;	/* always enable wrot pre-ultra */
	const u32 crop_en = 1;		/* always enable crop */
	const u32 hw_fmt = MML_FMT_HW_FORMAT(dest_fmt);
	const u32 bkgden = mml_wrot_bkgd_en ? 1 : 0;

	u32 out_swap = MML_FMT_SWAP(dest_fmt);
	u32 uv_xsel, uv_ysel;
	u32 preultra, alpha;
	u32 scan_10bit = 0, bit_num = 0, pending_zero = 0, pvric = 0;

	/* clear event */
	cmdq_pkt_clear_event(pkt, wrot->event_eof);

	wrot_color_fmt(cfg, wrot_frm);

	/* calculate for later config tile use */
	wrot_calc_hw_buf_setting(wrot, cfg, dest, wrot_frm);

	if (cfg->alpharot || cfg->rgbrot) {
		wrot_frm->mat_en = 0;
		wrot_frm->mat_sel = 15;

		if (wrot->data->rb_swap == 1) {
			if (!MML_FMT_AFBC(src_fmt) && !MML_FMT_10BIT(src_fmt))
				out_swap ^= MML_FMT_SWAP(src_fmt);
			else if (MML_FMT_AFBC(src_fmt) && !MML_FMT_10BIT(src_fmt))
				out_swap =
					(MML_FMT_SWAP(src_fmt) == MML_FMT_SWAP(dest_fmt)) ? 1 : 0;
			else if (MML_FMT_AFBC(src_fmt) && MML_FMT_10BIT(src_fmt))
				out_swap = out_swap ? 0 : 1;
		}
	}

	mml_msg("use config %p wrot %p", cfg, wrot);

	if (cfg->info.mode == MML_MODE_DDP_ADDON && ccfg->node->out_idx == MML_MAX_OUTPUTS - 1 &&
			task->pq_param[0].src_hdr_video_mode == MML_PQ_AIREGION) {
		/* TODO: need to fix pq_task */
		wrot->pq_task = task->pq_task;
		wrot->dual = task->config->dual;
		wrot->out_idx = ccfg->node->out_idx;
		wrot->jobid = task->job.jobid;
		wrot->dest_cnt = cfg->info.dest_cnt;
		wrot->frame_data.size_info.out_rotate[ccfg->node->out_idx] =
			cfg->out_rotate[ccfg->node->out_idx];
		memcpy(&wrot->frame_data.pq_param, task->pq_param,
			MML_MAX_OUTPUTS * sizeof(struct mml_pq_param));
		memcpy(&wrot->frame_data.info, &task->config->info,
			sizeof(struct mml_frame_info));
		memcpy(&wrot->frame_data.frame_out, &task->config->frame_out,
			MML_MAX_OUTPUTS * sizeof(struct mml_frame_size));
		memcpy(&wrot->frame_data.size_info.frame_in_crop_s[0],
			&cfg->frame_in_crop[0],
			MML_MAX_OUTPUTS *sizeof(struct mml_crop));

		wrot->frame_data.size_info.crop_size_s.width =
			cfg->frame_tile_sz.width;
		wrot->frame_data.size_info.crop_size_s.height =
			cfg->frame_tile_sz.height;
		wrot->frame_data.size_info.frame_size_s.width = cfg->frame_in.width;
		wrot->frame_data.size_info.frame_size_s.height = cfg->frame_in.height;
	}

	/* Enable engine */
	cmdq_pkt_write(pkt, NULL, base_pa + wrot->reg[VIDO_ROT_EN], 0x01, 0x00000001);

	if (mml_wrot_crc)
		cmdq_pkt_write(pkt, NULL, base_pa + wrot->reg[VIDO_CRC_CTRL],
			mml_wrot_crc, U32_MAX);

	/* Enable shadow */
	cmdq_pkt_write(pkt, NULL, base_pa + wrot->reg[VIDO_SHADOW_CTRL],
		(cfg->shadow ? 0 : BIT(1)) | 0x1, U32_MAX);

	if (h_subsample) {	/* YUV422/420 out */
		wrot_frm->filt_v = MML_FMT_V_SUBSAMPLE(src_fmt) ||
			 MML_FMT_GROUP(src_fmt) == 2 ?
			 0 : uv_table[v_subsample][rotate][flip][1];
		uv_xsel = uv_table[v_subsample][rotate][flip][2];
		uv_ysel = uv_table[v_subsample][rotate][flip][3];
	} else if (dest_fmt == MML_FMT_GREY) {
		uv_xsel = 0;
		uv_ysel = 0;
	} else {
		uv_xsel = 2;
		uv_ysel = 2;
	}

	/* Note: check odd size roi_w & roi_h for uv_xsel/uv_ysel */
	if ((wrot_frm->compose.width & 0x1) && uv_xsel == 1)
		uv_xsel = 0;
	if ((wrot_frm->compose.height & 0x1) && uv_ysel == 1)
		uv_ysel = 0;

	/* Note: WROT not support UV swap */
	if (out_swap == 1 && MML_FMT_PLANE(dest_fmt) == 3) {
		swap(wrot_frm->iova[1], wrot_frm->iova[2]);
		swap(wrot_frm->plane_offset[1], wrot_frm->plane_offset[2]);
	}

	if (!mml_slt)
		wrot_config_smi(wrot, cfg, pkt);

	if (task->config->info.mode == MML_MODE_RACING) {
		u64 sram_addr;

		calc_plane_offset(wrot_frm->compose.left, 0,
			wrot_frm->y_stride, wrot_frm->uv_stride,
			wrot_frm->bbp_y, wrot_frm->bbp_uv,
			wrot_frm->hor_sh_uv, wrot_frm->ver_sh_uv,
			wrot_frm->plane_offset);
		sram_addr = wrot->sram_pa + wrot_frm->plane_offset[0];

		/* config ready signal from disp0 or disp1 */
		wrot_config_ready(wrot, cfg, ccfg->pipe, pkt, true);

		/* inline rotate case always write to sram pa */
		cmdq_pkt_write(pkt, NULL, base_pa + wrot->reg[VIDO_BASE_ADDR],
			sram_addr, U32_MAX);
		cmdq_pkt_write(pkt, NULL, base_pa + wrot->reg[VIDO_BASE_ADDR_HIGH],
			sram_addr >> 32, U32_MAX);
		cmdq_pkt_write(pkt, NULL, base_pa + wrot->reg[VIDO_BASE_ADDR_C],
			sram_addr, U32_MAX);
		cmdq_pkt_write(pkt, NULL, base_pa + wrot->reg[VIDO_BASE_ADDR_HIGH_C],
			sram_addr >> 32, U32_MAX);
		cmdq_pkt_write(pkt, NULL, base_pa + wrot->reg[VIDO_BASE_ADDR_V],
			sram_addr, U32_MAX);
		cmdq_pkt_write(pkt, NULL, base_pa + wrot->reg[VIDO_BASE_ADDR_HIGH_V],
			sram_addr >> 32, U32_MAX);

		cmdq_pkt_write(pkt, NULL,
			wrot->irot_base[0] + DISPSYS_SHADOW_CTRL, 0x2, U32_MAX);

		if (mml_racing_ut == 2)
			cmdq_pkt_write(pkt, NULL,
				wrot->irot_base[0] + INLINEROT_OVLSEL, 0x22, U32_MAX);
		else if (mml_racing_ut == 3)
			cmdq_pkt_write(pkt, NULL,
				wrot->irot_base[0] + INLINEROT_OVLSEL, 0xc, U32_MAX);

		mml_msg("%s sram pa %#x offset %u",
			__func__, (u32)sram_addr, wrot_frm->plane_offset[0]);
	} else {
		calc_plane_offset(wrot_frm->compose.left, wrot_frm->compose.top,
			wrot_frm->y_stride, wrot_frm->uv_stride,
			wrot_frm->bbp_y, wrot_frm->bbp_uv,
			wrot_frm->hor_sh_uv, wrot_frm->ver_sh_uv,
			wrot_frm->plane_offset);

		/* normal dram case config wrot iova with reuse */
		wrot_config_addr(wrot, dest, dest_fmt, base_pa,
				 wrot_frm, pkt, reuse, cache);
		/* always turn off ready to wrot */
		wrot_config_ready(wrot, cfg, ccfg->pipe, pkt, false);

		/* and clear inlinerot enable since last frame maybe racing mode */
		cmdq_pkt_write(pkt, NULL, base_pa + wrot->reg[VIDO_IN_LINE_ROT], 0, U32_MAX);
	}

	/* Write frame related registers */
	alpha = (cfg->alpharot || cfg->alpharsz);
	cmdq_pkt_write(pkt, NULL, base_pa + wrot->reg[VIDO_CTRL],
		       (uv_ysel		<< 30) +
		       (uv_xsel		<< 28) +
		       (flip		<< 24) +
		       (wrot_frm->rotate << 20) +
		       (alpha		<< 16) + /* alpha */
		       (preultra_en	<< 14) + /* pre-ultra */
		       (crop_en		<< 12) +
		       (out_swap	<<  8) +
		       (bkgden		<<  5) +
		       (hw_fmt		<<  0), 0xf131512f);

	if (unlikely(mml_wrot_bkgd_en)) {
		cmdq_pkt_write(pkt, NULL, base_pa + wrot->reg[VIDO_CTRL], BIT(5), BIT(5));
		cmdq_pkt_write(pkt, NULL, base_pa + wrot->reg[VIDO_BKGD], mml_wrot_bkgd, U32_MAX);
	}

	if (MML_FMT_10BIT_LOOSE(dest_fmt)) {
		if (MML_FMT_HW_FORMAT(dest_fmt) == 12)
			scan_10bit = 1;
		else
			scan_10bit = 5;
		bit_num = 1;
	} else if (MML_FMT_10BIT_PACKED(dest_fmt)) {
		if (MML_FMT_HW_FORMAT(dest_fmt) == 12)
			scan_10bit = 3;
		else
			scan_10bit = 1;
		pending_zero = BIT(26);
		bit_num = 1;
	}
	/* DMA_SUPPORT_AFBC */
	if (MML_FMT_AFBC(dest_fmt)) {
		scan_10bit = 0;
		pending_zero = BIT(26);
	}
	cmdq_pkt_write(pkt, NULL, base_pa + wrot->reg[VIDO_SCAN_10BIT], scan_10bit, U32_MAX);
	cmdq_pkt_write(pkt, NULL, base_pa + wrot->reg[VIDO_PENDING_ZERO], pending_zero, U32_MAX);
	cmdq_pkt_write(pkt, NULL, base_pa + wrot->reg[VIDO_CTRL_2], bit_num, 0x00000007);

	if (MML_FMT_AFBC(dest_fmt)) {
		pvric |= BIT(0);
		if (MML_FMT_10BIT(dest_fmt))
			pvric |= BIT(1);
	}
	cmdq_pkt_write(pkt, NULL, base_pa + wrot->reg[VIDO_PVRIC], pvric, U32_MAX);

	/* set ESL */
	if (plane == 3 || plane == 2 || hw_fmt == 7)	/* 3-plane, 2-plane, Y8 */
		preultra = (216 << 12) + (196 << 0);
	else if (hw_fmt == 0 || hw_fmt == 1)		/* RGB */
		preultra = (136 << 12) + (76 << 0);
	else if (hw_fmt == 2 || hw_fmt == 3)		/* ARGB */
		preultra = (96 << 12) + (16 << 0);
	else if (hw_fmt == 4 || hw_fmt == 5)		/* UYVY */
		preultra = (176 << 12) + (136 << 0);
	else
		preultra = 0;
	cmdq_pkt_write(pkt, NULL, base_pa + wrot->reg[VIDO_DMA_PREULTRA], preultra,
		       U32_MAX);

	/* Write frame Y stride */
	cmdq_pkt_write(pkt, NULL, base_pa + wrot->reg[VIDO_STRIDE], wrot_frm->y_stride,
		       U32_MAX);
	/* Write frame UV stride */
	cmdq_pkt_write(pkt, NULL, base_pa + wrot->reg[VIDO_STRIDE_C], wrot_frm->uv_stride,
		       U32_MAX);
	cmdq_pkt_write(pkt, NULL, base_pa + wrot->reg[VIDO_STRIDE_V], wrot_frm->uv_stride,
		       U32_MAX);

	/* Write matrix control */
	cmdq_pkt_write(pkt, NULL, base_pa + wrot->reg[VIDO_MAT_CTRL],
		       (wrot_frm->mat_sel << 4) +
		       (wrot_frm->mat_en << 0), U32_MAX);

	/* Set the fixed ALPHA as 0xff */
	cmdq_pkt_write(pkt, NULL, base_pa + wrot->reg[VIDO_DITHER], 0xff000000, U32_MAX);

	/* Set VIDO_RSV_1 */
	cmdq_pkt_write(pkt, NULL, base_pa + wrot->reg[VIDO_RSV_1], 0x80000000, 0x80000000);

	/* Set VIDO_FIFO_TEST */
	cmdq_pkt_write(pkt, NULL, base_pa + wrot->reg[VIDO_FIFO_TEST], wrot->data->fifo, U32_MAX);

	/* turn off WROT dma dcm */
	cmdq_pkt_write(pkt, NULL, base_pa + wrot->reg[VIDO_ROT_EN],
		       (0x1 << 23) + (0x1 << 20), 0x00900000);

	/* Enable dither */
	cmdq_pkt_write(pkt, NULL, base_pa + wrot->reg[VIDO_DITHER_CON], wrot_frm->dither_con, U32_MAX);

	return 0;
}

static void wrot_tile_calc_comp(const struct mml_frame_dest *dest,
				const struct wrot_frame_data *wrot_frm,
				const struct mml_tile_engine *tile,
				struct wrot_ofst_addr *ofst)
{
	/* Following data retrieve from tile calc result */
	const u64 out_xs = tile->out.xs;
	const u64 out_ys = 0; /* In decouple mode, wrot's tile->out.ys is constantly 0. */
	const u64 out_w = wrot_frm->out_w;
	const u64 out_h = wrot_frm->out_h;
	const char *msg = "";

	if (wrot_frm->rotate == MML_ROT_0 && !wrot_frm->flip) {
		/* Target Y offset */
		ofst->y = (out_ys / 8) * (wrot_frm->y_stride / 128) * 1024 +
			  out_xs * 32;
		msg = "No flip and no rotation";
	} else if (wrot_frm->rotate == MML_ROT_0 && wrot_frm->flip) {
		/* Target Y offset */
		ofst->y = ((out_ys / 8) * (wrot_frm->y_stride / 128) +
			  (out_w / 32) - (out_xs / 32) - 1) * 1024;
		msg = "Flip without rotation";
	} else if (wrot_frm->rotate == MML_ROT_90 && !wrot_frm->flip) {
		/* Target Y offset */
		ofst->y = ((out_xs / 8) * (wrot_frm->y_stride / 128) +
			  (out_h / 32) - (out_ys / 32) - 1) * 1024;
		msg = "Rotate 90 degree only";
	} else if (wrot_frm->rotate == MML_ROT_90 && wrot_frm->flip) {
		/* Target Y offset */
		ofst->y = (out_xs / 8) * (wrot_frm->y_stride / 128) * 1024 +
			  out_ys * 32;
		msg = "Flip and Rotate 90 degree";
	} else if (wrot_frm->rotate == MML_ROT_180 && !wrot_frm->flip) {
		/* Target Y offset */
		ofst->y = (((out_h / 8) - (out_ys / 8) - 1) *
			  (wrot_frm->y_stride / 128) +
			  (out_w / 32) - (out_xs / 32) - 1) * 1024;
		msg = "Rotate 180 degree only";
	} else if (wrot_frm->rotate == MML_ROT_180 && wrot_frm->flip) {
		/* Target Y offset */
		ofst->y = (((out_h / 8) - (out_ys / 8) - 1) *
			  (wrot_frm->y_stride / 128)) * 1024 + out_xs * 32;
		msg = "Flip and Rotate 180 degree";
	} else if (wrot_frm->rotate == MML_ROT_270 && !wrot_frm->flip) {
		/* Target Y offset */
		ofst->y = (((out_w / 8) - (out_xs / 8) - 1) *
			  (wrot_frm->y_stride / 128)) * 1024 + out_ys * 32;
		msg = "Rotate 270 degree only";
	} else if (wrot_frm->rotate == MML_ROT_270 && wrot_frm->flip) {
		/* Target Y offset */
		ofst->y = (((out_w / 8) - (out_xs / 8) - 1) *
			  (wrot_frm->y_stride / 128) +
			  (out_h / 32) - (out_ys / 32) - 1) * 1024;
		msg = "Flip and Rotate 270 degree";
	}
	/* Target U offset. RGBA: 64, YV12 8-bit: 24, 10-bit: 32. */
	ofst->c = ofst->y / 64;

	mml_msg("%s %s: offset Y:%#010llx U:%#010llx V:%#010llx",
		__func__, msg, ofst->y, ofst->c, ofst->v);
}

static void wrot_tile_calc(const struct mml_task *task,
			   const struct mml_comp_wrot *wrot,
			   const struct mml_frame_dest *dest,
			   const struct mml_frame_tile *tout,
			   const struct mml_tile_engine *tile,
			   const u32 idx,
			   const enum mml_mode mode,
			   struct wrot_frame_data *wrot_frm,
			   struct wrot_ofst_addr *ofst)
{
	/* Following data retrieve from tile calc result */
	u64 out_xs = wrot_frm->pending_x ? round_up(tile->out.xs, 2) : tile->out.xs;
	u64 out_ys = wrot_frm->pending_y ? round_up(tile->out.ys, 2) : tile->out.ys;
	u32 out_w = wrot_frm->out_w;
	u32 out_h = wrot_frm->out_h;
	u32 sram_block = 0;		/* buffer block number for sram */
	const char *msg = "";
	bool tile_eol = false;

	if (wrot_frm->rotate == MML_ROT_0 && !wrot_frm->flip) {
		if (mode == MML_MODE_RACING) {
			sram_block = tout->tiles[idx].v_tile_no & 0x1;
			out_ys = 0;
			tile_eol = tout->h_tile_cnt == (tout->tiles[idx].h_tile_no + 1);
		}
		/* Target Y offset */
		ofst->y = out_ys * wrot_frm->y_stride +
			  (out_xs * wrot_frm->bbp_y >> 3);

		/* Target U offset */
		ofst->c = (out_ys >> wrot_frm->ver_sh_uv) *
			  wrot_frm->uv_stride +
			  ((out_xs >> wrot_frm->hor_sh_uv) *
			  wrot_frm->bbp_uv >> 3);

		/* Target V offset */
		ofst->v = (out_ys >> wrot_frm->ver_sh_uv) *
			  wrot_frm->uv_stride +
			  ((out_xs >> wrot_frm->hor_sh_uv) *
			  wrot_frm->bbp_uv >> 3);

		msg = "No flip and no rotation";
	} else if (wrot_frm->rotate == MML_ROT_0 && wrot_frm->flip) {
		if (mode == MML_MODE_RACING) {
			sram_block = tout->tiles[idx].v_tile_no & 0x1;
			out_ys = 0;
			tile_eol = tout->h_tile_cnt == (tout->tiles[idx].h_tile_no + 1);
		}
		/* Target Y offset */
		ofst->y = out_ys * wrot_frm->y_stride +
			  ((out_w - out_xs) *
			  wrot_frm->bbp_y >> 3) - 1;

		/* Target U offset */
		ofst->c = (out_ys >> wrot_frm->ver_sh_uv) *
			  wrot_frm->uv_stride +
			  (((out_w - out_xs) >>
			  wrot_frm->hor_sh_uv) * wrot_frm->bbp_uv >> 3) - 1;

		/* Target V offset */
		ofst->v = (out_ys >> wrot_frm->ver_sh_uv) *
			  wrot_frm->uv_stride +
			  (((out_w - out_xs) >>
			  wrot_frm->hor_sh_uv) * wrot_frm->bbp_uv >> 3) - 1;

		msg = "Flip without rotation";
	} else if (wrot_frm->rotate == MML_ROT_90 && !wrot_frm->flip) {
		if (mode == MML_MODE_RACING) {
			sram_block = tout->tiles[idx].h_tile_no & 0x1;
			out_xs = 0;
			tile_eol = tout->v_tile_cnt == (tout->tiles[idx].v_tile_no + 1);
		}
		/* Target Y offset */
		ofst->y = out_xs * wrot_frm->y_stride +
			  ((out_h - out_ys) *
			  wrot_frm->bbp_y >> 3) - 1;

		/* Target U offset */
		ofst->c = (out_xs >> wrot_frm->ver_sh_uv) *
			  wrot_frm->uv_stride +
			  (((out_h - out_ys) >>
			  wrot_frm->hor_sh_uv) * wrot_frm->bbp_uv >> 3) - 1;

		/* Target V offset */
		ofst->v = (out_xs >> wrot_frm->ver_sh_uv) *
			  wrot_frm->uv_stride +
			  (((out_h - out_ys) >>
			  wrot_frm->hor_sh_uv) * wrot_frm->bbp_uv >> 3) - 1;

		msg = "Rotate 90 degree only";
	} else if (wrot_frm->rotate == MML_ROT_90 && wrot_frm->flip) {
		if (mode == MML_MODE_RACING) {
			sram_block = tout->tiles[idx].h_tile_no & 0x1;
			out_xs = 0;
			tile_eol = tout->v_tile_cnt == (tout->tiles[idx].v_tile_no + 1);
		}
		/* Target Y offset */
		ofst->y = out_xs * wrot_frm->y_stride +
			  (out_ys * wrot_frm->bbp_y >> 3);

		/* Target U offset */
		ofst->c = (out_xs >> wrot_frm->ver_sh_uv) *
			  wrot_frm->uv_stride +
			  ((out_ys >> wrot_frm->hor_sh_uv) *
			  wrot_frm->bbp_uv >> 3);

		/* Target V offset */
		ofst->v = (out_xs >> wrot_frm->ver_sh_uv) *
			  wrot_frm->uv_stride +
			  ((out_ys >> wrot_frm->hor_sh_uv) *
			  wrot_frm->bbp_uv >> 3);

		msg = "Flip and Rotate 90 degree";
	} else if (wrot_frm->rotate == MML_ROT_180 && !wrot_frm->flip) {
		if (mode == MML_MODE_RACING) {
			sram_block = (tout->v_tile_cnt - tout->tiles[idx].v_tile_no - 1) & 0x1;
			out_h = tile->out.ye + 1;
			tile_eol = tout->tiles[idx].h_tile_no == 0;
		}
		/* Target Y offset */
		ofst->y = (out_h - out_ys - 1) *
			  wrot_frm->y_stride +
			  ((out_w - out_xs) *
			  wrot_frm->bbp_y >> 3) - 1;

		/* Target U offset */
		ofst->c = ((out_h - out_ys - 1) >>
			  wrot_frm->ver_sh_uv) * wrot_frm->uv_stride +
			  (((out_w - out_xs) >>
			  wrot_frm->hor_sh_uv) * wrot_frm->bbp_uv >> 3) - 1;

		/* Target V offset */
		ofst->v = ((out_h - out_ys - 1) >>
			  wrot_frm->ver_sh_uv) * wrot_frm->uv_stride +
			  (((out_w - out_xs) >>
			  wrot_frm->hor_sh_uv) * wrot_frm->bbp_uv >> 3) - 1;

		msg = "Rotate 180 degree only";
	} else if (wrot_frm->rotate == MML_ROT_180 && wrot_frm->flip) {
		if (mode == MML_MODE_RACING) {
			sram_block = (tout->v_tile_cnt - tout->tiles[idx].v_tile_no - 1) & 0x1;
			out_h = tile->out.ye + 1;
			tile_eol = tout->tiles[idx].h_tile_no == 0;
		}
		/* Target Y offset */
		ofst->y = (out_h - out_ys - 1) *
			  wrot_frm->y_stride +
			  (out_xs * wrot_frm->bbp_y >> 3);

		/* Target U offset */
		ofst->c = ((out_h - out_ys - 1) >>
			  wrot_frm->ver_sh_uv) * wrot_frm->uv_stride +
			  ((out_xs >> wrot_frm->hor_sh_uv) *
			  wrot_frm->bbp_uv >> 3);

		/* Target V offset */
		ofst->v = ((out_h - out_ys - 1) >>
			  wrot_frm->ver_sh_uv) * wrot_frm->uv_stride +
			  ((out_xs >> wrot_frm->hor_sh_uv) *
			  wrot_frm->bbp_uv >> 3);

		msg = "Flip and Rotate 180 degree";
	} else if (wrot_frm->rotate == MML_ROT_270 && !wrot_frm->flip) {
		if (mode == MML_MODE_RACING) {
			sram_block = (tout->h_tile_cnt - tout->tiles[idx].h_tile_no - 1) & 0x1;
			out_w = tile->out.xe + 1;
			tile_eol = tout->tiles[idx].v_tile_no == 0;
		}
		/* Target Y offset */
		ofst->y = (out_w - out_xs - 1) *
			  wrot_frm->y_stride +
			  (out_ys * wrot_frm->bbp_y >> 3);

		/* Target U offset */
		ofst->c = ((out_w - out_xs - 1) >>
			  wrot_frm->ver_sh_uv) * wrot_frm->uv_stride +
			  ((out_ys >> wrot_frm->hor_sh_uv) *
			  wrot_frm->bbp_uv >> 3);

		/* Target V offset */
		ofst->v = ((out_w - out_xs - 1) >>
			  wrot_frm->ver_sh_uv) * wrot_frm->uv_stride +
			  ((out_ys >> wrot_frm->hor_sh_uv) *
			  wrot_frm->bbp_uv >> 3);

		msg = "Rotate 270 degree only";
	} else if (wrot_frm->rotate == MML_ROT_270 && wrot_frm->flip) {
		if (mode == MML_MODE_RACING) {
			sram_block = (tout->h_tile_cnt - tout->tiles[idx].h_tile_no - 1) & 0x1;
			out_w = tile->out.xe + 1;
			tile_eol = tout->tiles[idx].v_tile_no == 0;
		}
		/* Target Y offset */
		ofst->y = (out_w - out_xs - 1) *
			  wrot_frm->y_stride +
			  ((out_h - out_ys) *
			  wrot_frm->bbp_y >> 3) - 1;

		/* Target U offset */
		ofst->c = ((out_w - out_xs - 1) >>
			  wrot_frm->ver_sh_uv) * wrot_frm->uv_stride +
			  (((out_h - out_ys) >>
			  wrot_frm->hor_sh_uv) * wrot_frm->bbp_uv >> 3) - 1;

		/* Target V offset */
		ofst->v = ((out_w - out_xs - 1) >>
			  wrot_frm->ver_sh_uv) * wrot_frm->uv_stride +
			  (((out_h - out_ys) >>
			  wrot_frm->hor_sh_uv) * wrot_frm->bbp_uv >> 3) - 1;

		msg = "Flip and Rotate 270 degree";
	}

	if (mode == MML_MODE_RACING) {
		ofst->y += wrot->data->sram_size * sram_block;
		ofst->c += wrot->data->sram_size * sram_block;
		ofst->v += wrot->data->sram_size * sram_block;
		wrot_frm->wdone[idx].sram = sram_block;
		wrot_frm->wdone[idx].eol = tile_eol;
		tout->tiles[idx].eol = tile_eol;
	}

	mml_msg("%s %s: offset Y:%#010llx U:%#010llx V:%#010llx h:%hu/%hu v:%hu/%hu (%u%s)",
		__func__, msg, ofst->y, ofst->c, ofst->v,
		tout->tiles[idx].h_tile_no, tout->h_tile_cnt,
		tout->tiles[idx].v_tile_no, tout->v_tile_cnt, sram_block,
		tile_eol ? " eol" : "");
}

static void wrot_check_buf(const struct mml_frame_dest *dest,
			   struct wrot_setting *setting,
			   const struct wrot_frame_data *wrot_frm,
			   struct check_buf_param *buf)
{
	/* Checking Y buffer usage
	 * y_buf_width is just larger than main_blk_width
	 */
	buf->y_buf_width = ceil_m(setting->main_blk_width,
				  setting->main_buf_line_num) *
			   setting->main_buf_line_num;
	buf->y_buf_usage = buf->y_buf_width * setting->main_buf_line_num;
	if (buf->y_buf_usage > buf->y_buf_size) {
		setting->main_buf_line_num = setting->main_buf_line_num - 4;
		buf->y_buf_check = 0;
		buf->uv_buf_check = 0;
		return;
	}

	buf->y_buf_check = 1;

	/* Checking UV buffer usage */
	if (!MML_FMT_H_SUBSAMPLE(dest->data.format)) {
		buf->uv_blk_width = setting->main_blk_width;
		buf->uv_blk_line = setting->main_buf_line_num;
	} else {
		if (!MML_FMT_V_SUBSAMPLE(dest->data.format)) {
			/* YUV422 */
			if (wrot_frm->rotate == MML_ROT_0 ||
			    wrot_frm->rotate == MML_ROT_180) {
				buf->uv_blk_width =
					setting->main_blk_width >> 1;
				buf->uv_blk_line = setting->main_buf_line_num;
			} else {
				buf->uv_blk_width = setting->main_blk_width;
				buf->uv_blk_line =
					setting->main_buf_line_num >> 1;
			}
		} else {
			/* YUV420 */
			buf->uv_blk_width = setting->main_blk_width >> 1;
			buf->uv_blk_line = setting->main_buf_line_num >> 1;
		}
	}

	buf->uv_buf_width = ceil_m(buf->uv_blk_width, buf->uv_blk_line) *
			    buf->uv_blk_line;
	buf->uv_buf_usage = buf->uv_buf_width * buf->uv_blk_line;
	if (buf->uv_buf_usage > buf->uv_buf_size) {
		setting->main_buf_line_num = setting->main_buf_line_num - 4;
		buf->y_buf_check = 0;
		buf->uv_buf_check = 0;
	} else {
		buf->uv_buf_check = 1;
	}

	if (wrot_frm->rotate == MML_ROT_90 || wrot_frm->rotate == MML_ROT_270) {
		if (dest->data.format == MML_FMT_NV15 ||
		    dest->data.format == MML_FMT_NV51) {
			if (setting->main_buf_line_num > 32)
				setting->main_buf_line_num = 64;
			else
				setting->main_buf_line_num = 32;
		}
	}
}

static void wrot_calc_setting(struct mml_comp_wrot *wrot,
			      const struct mml_frame_dest *dest,
			      const struct wrot_frame_data *wrot_frm,
			      struct wrot_setting *setting)
{
	u32 hw_fmt = MML_FMT_HW_FORMAT(dest->data.format);
	u32 tile_width = wrot->data->tile_width;
	u32 coeff1, coeff2, temp;
	struct check_buf_param buf = {0};

	if (hw_fmt == 9 || hw_fmt == 13) {
		buf.y_buf_size = tile_width * 48;
		buf.uv_buf_size = tile_width / 2 * 48;
	} else if (hw_fmt == 8 || hw_fmt == 12) {
		buf.y_buf_size = tile_width * 64;
		buf.uv_buf_size = tile_width / 2 * 32;
	} else {
		buf.y_buf_size = tile_width * 32;
		buf.uv_buf_size = tile_width * 32;
	}

	/* Default value */
	setting->main_buf_line_num = 0;
	/* Allocate FIFO buffer */
	setting->main_blk_width = setting->tar_xsize;

	coeff1 = floor_m(wrot_frm->fifo_max_sz, setting->tar_xsize * 2) * 2;
	coeff2 = ceil_m(setting->tar_xsize, coeff1);
	temp = ceil_m(setting->tar_xsize, coeff2 * 4) * 4;

	if (temp > setting->tar_xsize)
		setting->main_buf_line_num = ceil_m(setting->tar_xsize, 4) * 4;
	else
		setting->main_buf_line_num = temp;
	if (setting->main_buf_line_num > wrot_frm->max_line_cnt)
		setting->main_buf_line_num = wrot_frm->max_line_cnt;

	/* check for internal buffer size */
	while (!buf.y_buf_check || !buf.uv_buf_check)
		wrot_check_buf(dest, setting, wrot_frm, &buf);
}

static u32 wrot_calc_stash_delay(const struct mml_frame_config *cfg,
	const struct mml_frame_dest *dest, u32 in_xsize, u32 line_num)
{
	const struct mml_topology_cache *tp = mml_topology_get_cache(cfg->mml);
	u32 opp, clk_rate, delay_interval, delay_line;

	if (unlikely(!tp)) {
		mml_err("no topology");
		return 0;
	}

	opp = tp->qos[mml_sys_frame].opp_cnt / 2;
	clk_rate = tp->qos[mml_sys_frame].opp_speeds[opp];

	if (dest->rotate == MML_ROT_0) {
		/* config only page to page delay in clock level
		 * [15:0] = 0
		 * [31:16] = wrot_delay_us * clk_rate
		 */
		return (clk_rate * wrot_stash_delay) << 16;
	}

	/* config first cmd delay in line time level,
	 * and page to page delay in clock level
	 * [15:0] = (in_xsize * line_num / clk_rate - delay_us) * clk_rate / in_xsize
	 * [31:16] = 4096 / bpp * 16
	 */
	delay_interval = (in_xsize * line_num / clk_rate - wrot_stash_delay) * clk_rate / in_xsize;
	delay_line = 524288 / MML_FMT_BITS_PER_PIXEL(dest->data.format);

	return (delay_interval << 16) | delay_line;
}

static void wrot_calc_stash_addr_boundary(struct mml_comp *comp, struct cmdq_pkt *pkt,
	enum mml_color format, enum mml_orientation rotate, u32 height, u32 ystride, u32 uvstride)
{
	struct mml_comp_wrot *wrot = comp_to_wrot(comp);
	u32 stash_offset_addr = 0, stash_offset_addr_cv = 0;

	if (rotate == MML_ROT_0 || rotate == MML_ROT_90) {
		if (MML_FMT_IS_ARGB(format)) {
			/* RGB, 1 plane */
			stash_offset_addr = ystride * height - 1;
		} else if (MML_FMT_10BIT(format)) {
			/* YUV 10bit */
			stash_offset_addr = ystride * (height + (4 - (height & 0x1))) - 1;
			if (uvstride)
				stash_offset_addr_cv =
					uvstride * (height + (4 - (height & 0x1))) - 1;
		} else {
			/* YUV 8bit */
			stash_offset_addr = ystride * (height+ 1) - 1;
			if (uvstride)
				stash_offset_addr_cv = uvstride * (height + 1) / 2 - 1;
		}
	}

	cmdq_pkt_write(pkt, NULL, comp->base_pa + wrot->reg[VIDO_STASH_OFST_ADDR],
		stash_offset_addr, U32_MAX);
	cmdq_pkt_write(pkt, NULL, comp->base_pa + wrot->reg[VIDO_STASH_OFST_ADDR_C],
		stash_offset_addr_cv, U32_MAX);
	cmdq_pkt_write(pkt, NULL, comp->base_pa + wrot->reg[VIDO_STASH_OFST_ADDR_V],
		stash_offset_addr_cv, U32_MAX);
	mml_msg("%s stash ofst addr %#010x %#010x format %#010x rot %u stride %u %u",
		__func__, stash_offset_addr, stash_offset_addr_cv, format, rotate,
		ystride, uvstride);
}

static s32 wrot_config_tile(struct mml_comp *comp, struct mml_task *task,
			    struct mml_comp_config *ccfg, u32 idx)
{
	struct mml_comp_wrot *wrot = comp_to_wrot(comp);
	struct mml_frame_config *cfg = task->config;
	struct cmdq_pkt *pkt = task->pkts[ccfg->pipe];
	struct wrot_frame_data *wrot_frm = wrot_frm_data(ccfg);
	u32 plane;

	/* frame data should not change between each tile */
	const struct mml_frame_dest *dest = &cfg->info.dest[wrot_frm->out_idx];
	const phys_addr_t base_pa = comp->base_pa;
	const enum mml_color dest_fmt = dest->data.format;

	struct mml_tile_engine *tile = config_get_tile(cfg, ccfg, idx);
	const struct mml_frame_tile *tout = cfg->frame_tile[ccfg->pipe];
	u16 tile_cnt = tout->tile_cnt;
	/* Following data retrieve from tile result */
	const u32 in_xs = tile->in.xs;
	const u32 in_xe = tile->in.xe;
	const u32 in_ys = tile->in.ys;
	const u32 in_ye = tile->in.ye;
	const u32 out_xs = tile->out.xs;
	const u32 out_xe = tile->out.xe;
	const u32 out_ys = tile->out.ys;
	const u32 out_ye = tile->out.ye;
	const u32 wrot_crop_ofst_x = tile->luma.x;
	const u32 wrot_crop_ofst_y = tile->luma.y;

	u32 wrot_in_xsize;
	u32 wrot_in_ysize;
	u32 wrot_tar_xsize;
	u32 wrot_tar_ysize;
	struct wrot_ofst_addr ofst = {0};
	struct wrot_setting setting = {0};
	u32 buf_line_num;

	/* Fill the tile settings */
	if (MML_FMT_AFBC(dest_fmt))
		wrot_tile_calc_comp(dest, wrot_frm, tile, &ofst);
	else
		wrot_tile_calc(task, wrot, dest, tout, tile, idx, cfg->info.mode,
			       wrot_frm, &ofst);

	if (cfg->info.mode == MML_MODE_RACING) {
		/* enable inline rotate and config buffer 0 or 1 */
		cmdq_pkt_write(pkt, NULL, base_pa + wrot->reg[VIDO_IN_LINE_ROT],
			(wrot_frm->wdone[idx].sram << 1) | 0x1, U32_MAX);
	}

	/* Write Y pixel offset */
	wrot_write_ofst(pkt,
			base_pa + wrot->reg[VIDO_OFST_ADDR],
			base_pa + wrot->reg[VIDO_OFST_ADDR_HIGH], ofst.y);
	/* Write U pixel offset */
	wrot_write_ofst(pkt,
			base_pa + wrot->reg[VIDO_OFST_ADDR_C],
			base_pa + wrot->reg[VIDO_OFST_ADDR_HIGH_C], ofst.c);
	/* Write V pixel offset */
	wrot_write_ofst(pkt,
			base_pa + wrot->reg[VIDO_OFST_ADDR_V],
			base_pa + wrot->reg[VIDO_OFST_ADDR_HIGH_V], ofst.v);

	/* Write source size and target size */
	wrot_in_xsize = in_xe - in_xs + 1;
	wrot_in_ysize = in_ye - in_ys + 1;
	wrot_tar_xsize = out_xe - out_xs + 1;
	wrot_tar_ysize = out_ye - out_ys + 1;

	cmdq_pkt_write(pkt, NULL, base_pa + wrot->reg[VIDO_IN_SIZE],
		       (wrot_in_ysize << 16) + (wrot_in_xsize <<  0),
		       U32_MAX);

	cmdq_pkt_write(pkt, NULL, base_pa + wrot->reg[VIDO_TAR_SIZE],
		       (wrot_tar_ysize << 16) + (wrot_tar_xsize <<  0),
		       U32_MAX);

	cmdq_pkt_write(pkt, NULL, base_pa + wrot->reg[VIDO_CROP_OFST],
		       (wrot_crop_ofst_y << 16) + (wrot_crop_ofst_x <<  0),
		       U32_MAX);

	/* config address boundary for stash prefetch */
	if (wrot->data->stash && mml_stash_en(cfg->info.mode)) {
		if (!MML_FMT_COMPRESS(dest_fmt))
			wrot_calc_stash_addr_boundary(comp, pkt, dest_fmt, dest->rotate,
				wrot_tar_ysize, dest->data.y_stride, dest->data.uv_stride);
		else
			mml_log("[warn]stash addr boundary not support for format %#010x",
				dest_fmt);
	}

	if (wrot_frm->pending_x || wrot_frm->pending_y) {
		/* Not use auto mode */
		u32 pending_zero = ((wrot_frm->pending_x & wrot_tar_xsize) << 2) +
				   ((wrot_frm->pending_y & wrot_tar_ysize) << 9);

		if (wrot_frm->pending_x && !is_change_wx(wrot_frm->rotate, wrot_frm->flip))
			pending_zero |= BIT(0);
		if (wrot_frm->pending_y && !is_change_hy(wrot_frm->rotate, wrot_frm->flip))
			pending_zero |= BIT(1);
		cmdq_pkt_write(pkt, NULL, base_pa + wrot->reg[VIDO_PENDING_ZERO],
			pending_zero, U32_MAX);
	}

	/* round up target footprint size for internal buffer and output */
	if (wrot->data->yuv_pending && cfg->info.mode != MML_MODE_RACING) {
		if (dest->rotate == MML_ROT_90 || dest->rotate == MML_ROT_270) {
			if (MML_FMT_H_SUBSAMPLE(dest->data.format)) {
				wrot_tar_xsize = round_up(wrot_tar_xsize, 2);
				wrot_tar_ysize = round_up(wrot_tar_ysize, 2);
			} else if (MML_FMT_V_SUBSAMPLE(dest->data.format)) {
				wrot_tar_xsize = round_up(wrot_tar_xsize, 2);
			}
		} else {
			if (MML_FMT_H_SUBSAMPLE(dest->data.format))
				wrot_tar_xsize = round_up(wrot_tar_xsize, 2);
			if (MML_FMT_V_SUBSAMPLE(dest->data.format))
				wrot_tar_ysize = round_up(wrot_tar_ysize, 2);
		}
	}

	/* set max internal buffer for tile usage,
	 * and check for internal buffer size
	 */
	setting.tar_xsize = wrot_tar_xsize;
	wrot_calc_setting(wrot, dest, wrot_frm, &setting);
	if (cfg->info.mode == MML_MODE_RACING) {
		/* line number for inline always set 16,
		 * since sram has no latency
		 */
		buf_line_num = WROT_MIN_BUF_LINE_NUM;
	} else {
		/* line number for each tile calculated by format */
		buf_line_num = setting.main_buf_line_num;
	}

	cmdq_pkt_write(pkt, NULL, base_pa + wrot->reg[VIDO_MAIN_BUF_SIZE],
		       (setting.main_blk_width << 16) |
		       (buf_line_num << 8) |
		       (wrot_frm->filt_v << 4), U32_MAX);

	/* Set wrot interrupt bit for debug,
	 * this bit will clear to 0 after wrot done.
	 *
	 * cmdq_pkt_write(pkt, NULL, base_pa + wrot->reg[VIDO_INT], 0x1, U32_MAX);
	 */

	/* qos accumulate tile pixel */
	if (wrot_frm->tile_last_x < tile->out.xe) {
		wrot_frm->max_size.width += wrot_tar_xsize;
		wrot_frm->tile_last_x = tile->out.xe;
	}
	if (wrot_frm->tile_last_y < tile->out.ye) {
		wrot_frm->max_size.height += wrot_tar_ysize;
		wrot_frm->tile_last_y = tile->out.ye;
	}

	/* no bandwidth for racing mode since wrot write to sram */
	if (cfg->info.mode != MML_MODE_RACING || wrot->data->ir_sram_bw) {
		/* calculate qos for later use */
		plane = MML_FMT_PLANE(dest->data.format);
		wrot_frm->datasize += mml_color_get_min_y_size(dest->data.format,
			wrot_tar_xsize, wrot_tar_ysize);
		if (plane > 1)
			wrot_frm->datasize += mml_color_get_min_uv_size(dest->data.format,
				wrot_tar_xsize, wrot_tar_ysize);
		if (plane > 2)
			wrot_frm->datasize += mml_color_get_min_uv_size(dest->data.format,
				wrot_tar_xsize, wrot_tar_ysize);
	}

	if (idx == tile_cnt - 1 && cfg->info.mode == MML_MODE_DDP_ADDON &&
			ccfg->node->out_idx == MML_MAX_OUTPUTS - 1 &&
			task->pq_param[0].src_hdr_video_mode == MML_PQ_AIREGION) {

		/* Set wrot interrupt status bit for judge if interrupt works,
		 * this bit will clear to 0 after wrot done.
		 */
		cmdq_pkt_write(pkt, NULL, base_pa + wrot->reg[VIDO_INT], 0x1, U32_MAX);

		/* Enable Frame Done IRQ in IR Mode*/
		cmdq_pkt_write(pkt, NULL, base_pa + wrot->reg[VIDO_INT_EN], 0x1, VIDO_INT_EN_MASK);
	} else
		cmdq_pkt_write(pkt, NULL, base_pa + wrot->reg[VIDO_INT_EN], 0x0, VIDO_INT_EN_MASK);

	if (wrot->data->stash) {
		if (mml_stash_en(cfg->info.mode)) {
			u32 delay_cnt = wrot_calc_stash_delay(cfg, dest, wrot_in_xsize,
				buf_line_num);

			/* enable wrot stash for expect performance
			 * VIDO_STASH_DWNSAMP_H		[31:27] 2 (default)
			 * VIDO_STASH_HW_MODE_SEL	[20:18] 2 (default)
			 * VIDO_STASH_OVERTAKE_EN	[ 5: 5] 1 (default)
			 * VIDO_STASH_LEAD_CMD_NUM	[ 4: 1] 1
			 * VIDO_STASH_FUNC_EN		[ 0: 0] 1
			 */
			cmdq_pkt_write(pkt, NULL, base_pa + wrot->reg[VIDO_STASH_CMD_FUNC_1],
				0x10080023, U32_MAX);
			cmdq_pkt_write(pkt, NULL, base_pa + wrot->reg[VIDO_STASH_DELAY_CNT],
				delay_cnt, U32_MAX);
		} else {
			/* stash off */
			cmdq_pkt_write(pkt, NULL, base_pa + wrot->reg[VIDO_STASH_CMD_FUNC_1],
				0, U32_MAX);
		}
	}

	mml_msg("%s min block width:%u min buf line num:%u dvfs size %u %u",
		__func__, setting.main_blk_width, setting.main_buf_line_num,
		wrot_frm->max_size.width, wrot_frm->max_size.height);

	return 0;
}

static inline void mml_ir_done_2to1(struct mml_comp_wrot *wrot, bool disp_dual,
	struct cmdq_pkt *pkt, u32 pipe, u32 sram, u32 irot_h_off, u32 height)
{
	u32 wdone = 1 << sram;

	if (pipe == 0) {
		if (sram == 0)
			cmdq_pkt_wfe(pkt, wrot->event_bufa);
		else
			cmdq_pkt_wfe(pkt, wrot->event_bufb);
		/* for pipe 0, wait pipe 1 and trigger wdone */
		cmdq_pkt_write(pkt, NULL, wrot->irot_base[0] + irot_h_off,
			height, U32_MAX);
		cmdq_pkt_write(pkt, NULL, wrot->irot_base[0] + INLINEROT_WDONE,
			wdone, U32_MAX);
		if (disp_dual) {
			cmdq_pkt_write(pkt, NULL, wrot->irot_base[1] + irot_h_off,
				height, U32_MAX);
			cmdq_pkt_write(pkt, NULL, wrot->irot_base[1] + INLINEROT_WDONE,
				wdone, U32_MAX);
		}
		if (sram == 1)
			cmdq_pkt_set_event(pkt, wrot->event_buf_next);
	} else {
		if (sram == 0) {
			/* notify pipe0 buf a */
			cmdq_pkt_set_event(pkt, wrot->event_bufa);
		} else {
			/* notify pipe0 buf b and wait for loop,
			 * this prevent event set twice or buf race condition
			 */
			cmdq_pkt_set_event(pkt, wrot->event_bufb);
			cmdq_pkt_wfe(pkt, wrot->event_buf_next);
		}
	}
}

static void wrot_config_inlinerot(struct mml_comp *comp, struct mml_task *task,
	struct mml_comp_config *ccfg, u32 idx)
{
	struct mml_comp_wrot *wrot = comp_to_wrot(comp);
	struct wrot_frame_data *wrot_frm = wrot_frm_data(ccfg);
	struct cmdq_pkt *pkt = task->pkts[ccfg->pipe];
	struct mml_frame_config *cfg = task->config;
	struct mml_tile_engine *tile = config_get_tile(cfg, ccfg, idx);
	u32 height;
	u32 irot_h_off;

	if (wrot_frm->rotate == MML_ROT_0 || wrot_frm->rotate == MML_ROT_180)
		height = tile->out.ye - tile->out.ys + 1;
	else
		height = tile->out.xe - tile->out.xs + 1;

	/* config height for current sram buf
	 * INLINEROT_HEIGHT0 for buf a and
	 * INLINEROT_HEIGHT1 for buf b
	 * so assign reg by sram side
	 */
	irot_h_off = INLINEROT_HEIGHT0 + 4 * wrot_frm->wdone[idx].sram;

	/* config wdone to trigger inlinerot work */
	if (cfg->dual) {
		/* 2 wrot to 1 disp: wait and trigger 1 wdone
		 * 2 wrot to 2 disp: wrot0 and wrot1 sync first
		 */
		mml_ir_done_2to1(wrot, cfg->disp_dual, pkt, ccfg->pipe,
			wrot_frm->wdone[idx].sram, irot_h_off, height);
	} else if (!cfg->dual && cfg->disp_dual) {
		/* 1 wrot to 2 disp: trigger 2 wdone (dual done) */
		cmdq_pkt_write(pkt, NULL, wrot->irot_base[0] + irot_h_off,
			height, U32_MAX);
		cmdq_pkt_write(pkt, NULL,
			wrot->irot_base[0] + INLINEROT_WDONE,
			1 << wrot_frm->wdone[idx].sram, U32_MAX);
		cmdq_pkt_write(pkt, NULL, wrot->irot_base[1] + irot_h_off,
			height, U32_MAX);
		cmdq_pkt_write(pkt, NULL,
			wrot->irot_base[1] + INLINEROT_WDONE,
			1 << wrot_frm->wdone[idx].sram, U32_MAX);
	} else {
		/* 1 wrot to 1 disp: trigger 1 wdone (by pipe)
		 * both case set disp wdone for current pipe
		 */
		cmdq_pkt_write(pkt, NULL,
			wrot->irot_base[wrot_frm->sram_side] + irot_h_off,
			height, U32_MAX);
		cmdq_pkt_write(pkt, NULL,
			wrot->irot_base[wrot_frm->sram_side] + INLINEROT_WDONE,
			1 << wrot_frm->wdone[idx].sram, U32_MAX);
	}
}

static s32 wrot_wait(struct mml_comp *comp, struct mml_task *task,
		     struct mml_comp_config *ccfg, u32 idx)
{
	struct mml_comp_wrot *wrot = comp_to_wrot(comp);
	struct wrot_frame_data *wrot_frm = wrot_frm_data(ccfg);
	struct cmdq_pkt *pkt = task->pkts[ccfg->pipe];

	/* wait wrot frame done */
	cmdq_pkt_wfe(pkt, wrot->event_eof);

	if (task->config->info.mode == MML_MODE_RACING && wrot_frm->wdone[idx].eol) {
		wrot_config_inlinerot(comp, task, ccfg, idx);
		wrot_frm->wdone_cnt++;

		/* debug, make gce send irq to cmdq and mark mmp pulse */
		if (mml_racing_eoc == 1)
			cmdq_pkt_eoc(pkt, false);
		else if (mml_racing_eoc == 2) {
			if (wrot_frm->wdone_cnt == 1 || wrot_frm->wdone_cnt == 2)
				cmdq_pkt_eoc(pkt, false);
		}

		if (!task->config->disp_vdo && wrot_frm->wdone_cnt == 1)
			cmdq_pkt_set_event(pkt,
				mml_ir_get_mml_ready_event(task->config->mml));
	}

	return 0;
}

static void wrot_backup_crc(struct mml_comp *comp, struct mml_task *task,
	struct mml_comp_config *ccfg)
{
#if IS_ENABLED(CONFIG_MTK_MML_DEBUG)
	struct mml_comp_wrot *wrot = comp_to_wrot(comp);
	int ret;

	if (likely(!mml_wrot_crc))
		return;

	ret = cmdq_pkt_backup(task->pkts[ccfg->pipe], comp->base_pa + wrot->reg[VIDO_CRC_VALUE],
		&task->backup_crc_wdma[ccfg->pipe]);
	if (ret) {
		mml_err("%s fail to backup CRC", __func__);
		mml_wrot_crc = 0;
	}
#endif
}

static void wrot_backup_crc_update(struct mml_comp *comp, struct mml_task *task,
	struct mml_comp_config *ccfg)
{
#if IS_ENABLED(CONFIG_MTK_MML_DEBUG)
	if (!mml_wrot_crc || !task->backup_crc_wdma[ccfg->pipe].inst_offset)
		return;

	cmdq_pkt_backup_update(task->pkts[ccfg->pipe], &task->backup_crc_wdma[ccfg->pipe]);
#endif
}

static s32 wrot_post(struct mml_comp *comp, struct mml_task *task,
		     struct mml_comp_config *ccfg)
{
	struct mml_comp_wrot *wrot = comp_to_wrot(comp);
	struct wrot_frame_data *wrot_frm = wrot_frm_data(ccfg);
	struct mml_pipe_cache *cache = &task->config->cache[ccfg->pipe];

	/* accmulate data size and use max pixel */
	cache->total_datasize += wrot_frm->datasize;
	dvfs_cache_sz(cache, wrot_frm->max_size.width / wrot->data->px_per_tick,
		wrot_frm->max_size.height, 0, 0);
	dvfs_cache_log(cache, comp, "wrot");

	wrot_backup_crc(comp, task, ccfg);

	if (task->config->info.mode == MML_MODE_RACING) {
		u16 event = task->config->info.disp_done_event;

		if (event)
			cmdq_pkt_wfe(task->pkts[ccfg->pipe], event);
	}

	mml_msg("%s pipe %hhu eol %u", __func__, ccfg->pipe, wrot_frm->wdone_cnt);
	return 0;
}

static s32 update_frame_addr(struct mml_comp *comp, struct mml_task *task,
			     struct mml_comp_config *ccfg)
{
	struct mml_frame_config *cfg = task->config;
	struct wrot_frame_data *wrot_frm = wrot_frm_data(ccfg);
	struct mml_frame_dest *dest = &cfg->info.dest[wrot_frm->out_idx];
	struct mml_task_reuse *reuse = &task->reuse[ccfg->pipe];

	const u32 dest_fmt = dest->data.format;
	const u32 out_swap = MML_FMT_SWAP(dest_fmt);
	u64 addr_c, addr_v, addr;

	if (out_swap == 1 && MML_FMT_PLANE(dest_fmt) == 3)
		swap(wrot_frm->iova[1], wrot_frm->iova[2]);

	/* DMA_SUPPORT_AFBC */
	if (MML_FMT_AFBC(dest_fmt)) {
		u32 block_x;

		/* Write frame base address */
		calc_afbc_block(wrot_frm->bbp_y,
				wrot_frm->y_stride, dest->data.vert_stride,
				wrot_frm->iova, wrot_frm->plane_offset,
				&block_x, &addr_c, &addr_v, &addr);
	} else {
		addr = wrot_frm->iova[0] + wrot_frm->plane_offset[0];
		addr_c = wrot_frm->iova[1] + wrot_frm->plane_offset[1];
		addr_v = wrot_frm->iova[2] + wrot_frm->plane_offset[2];
	}
	/* update frame base address to list */
	wrot_update_addr(comp->id, reuse,
			 wrot_frm->labels[WROT_LABEL_ADDR],
			 wrot_frm->labels[WROT_LABEL_ADDR_HIGH],
			 addr);
	wrot_update_addr(comp->id, reuse,
			 wrot_frm->labels[WROT_LABEL_ADDR_C],
			 wrot_frm->labels[WROT_LABEL_ADDR_HIGH_C],
			 addr_c);
	wrot_update_addr(comp->id, reuse,
			 wrot_frm->labels[WROT_LABEL_ADDR_V],
			 wrot_frm->labels[WROT_LABEL_ADDR_HIGH_V],
			 addr_v);

	wrot_backup_crc_update(comp, task, ccfg);

	return 0;
}

static s32 wrot_reconfig_frame(struct mml_comp *comp, struct mml_task *task,
			       struct mml_comp_config *ccfg)
{
	/* for reconfig case, no need update addr */
	if (task->config->info.mode == MML_MODE_RACING)
		return 0;
	return update_frame_addr(comp, task, ccfg);
}

static const struct mml_comp_config_ops wrot_cfg_ops = {
	.prepare = wrot_prepare,
	.buf_map = wrot_buf_map,
	.buf_prepare = wrot_buf_prepare,
	.buf_unprepare = wrot_buf_unprepare,
	.get_label_count = wrot_get_label_count,
	.frame = wrot_config_frame,
	.tile = wrot_config_tile,
	.wait = wrot_wait,
	.post = wrot_post,
	.reframe = wrot_reconfig_frame,
};

static void wrot_init_frame_done_event(struct mml_comp *comp, u32 event)
{
	struct mml_comp_wrot *wrot = comp_to_wrot(comp);

	if (!wrot->event_eof)
		wrot->event_eof = event;
}

u32 wrot_datasize_get(struct mml_task *task, struct mml_comp_config *ccfg)
{
	return wrot_frm_data(ccfg)->datasize;
}

static u32 wrot_qos_stash_bw_get(struct mml_comp *comp, struct mml_task *task,
	struct mml_comp_config *ccfg, u32 *srt_bw_out, u32 *hrt_bw_out)
{
	struct mml_frame_config *cfg = task->config;
	struct wrot_frame_data *wrot_frm = wrot_frm_data(ccfg);
	const struct mml_frame_dest *dest = &cfg->info.dest[wrot_frm->out_idx];
	const u32 rotate = dest->rotate;
	const u32 format = dest->data.format;
	u32 burst, srt_bw = *srt_bw_out, hrt_bw = *hrt_bw_out;

	if (wrot_frm->stash_srt_bw)
		goto done;

	if (rotate == MML_ROT_0 || rotate == MML_ROT_180) {
		/* same as rdma */
		wrot_frm->stash_srt_bw = srt_bw / 256;
		wrot_frm->stash_hrt_bw = hrt_bw / 256;
		goto done;
	}

	switch (format) {
	case MML_FMT_RGB888:
	case MML_FMT_BGR888:
		burst = 6;
		break;
	/* RGB 8bit/10bit */
	case MML_FMT_RGBA8888:
	case MML_FMT_BGRA8888:
	case MML_FMT_ARGB8888:
	case MML_FMT_ABGR8888:
	case MML_FMT_RGBA1010102:
	case MML_FMT_BGRA1010102:
	case MML_FMT_ARGB2101010:
	case MML_FMT_ABGR2101010:
	case MML_FMT_YUVA8888:
	case MML_FMT_AYUV8888:
	/* YUV422 1 plane */
	case MML_FMT_UYVY:
	case MML_FMT_VYUY:
	case MML_FMT_YUYV:
	case MML_FMT_YVYU:
		burst = 8;
		break;
	default:
		burst = 8;
		mml_log("%s unknown format burst for wrot %#010x", __func__, format);
		break;
	}

	wrot_frm->stash_srt_bw = srt_bw / burst;
	wrot_frm->stash_hrt_bw = hrt_bw / burst;

	wrot_frm->stash_srt_bw = max_t(u32, MML_QOS_MIN_STASH_BW, wrot_frm->stash_srt_bw);
	if (wrot_frm->stash_hrt_bw)
		wrot_frm->stash_hrt_bw = max_t(u32, MML_QOS_MIN_STASH_BW, wrot_frm->stash_hrt_bw);

done:
	*srt_bw_out = wrot_frm->stash_srt_bw;
	*hrt_bw_out = wrot_frm->stash_hrt_bw;

	return 0;
}

u32 wrot_format_get(struct mml_task *task, struct mml_comp_config *ccfg)
{
	return task->config->info.dest[ccfg->node->out_idx].data.format;
}

static void wrot_store_crc(struct mml_comp *comp, struct mml_task *task,
			   struct mml_comp_config *ccfg)
{
#if IS_ENABLED(CONFIG_MTK_MML_DEBUG)
	const u32 pipe = ccfg->pipe;
	struct mml_comp_wrot *wrot = comp_to_wrot(comp);

	if (!mml_wrot_crc || !task->backup_crc_wdma[pipe].inst_offset)
		return;

	task->dest_crc[pipe] =
		cmdq_pkt_backup_get(task->pkts[pipe], &task->backup_crc_wdma[pipe]);
	mml_msg("%s wrot%d component %2u job %u pipe %u crc %#010x idx %u",
		__func__, wrot->idx, comp->id, task->job.jobid,
		ccfg->pipe, task->dest_crc[pipe], task->backup_crc_wdma[pipe].val_idx);
#endif
}

static void wrot_task_done(struct mml_comp *comp, struct mml_task *task,
			   struct mml_comp_config *ccfg)
{
	struct mml_frame_config *cfg = task->config;
	const uint8_t dest_cnt = cfg->info.dest_cnt;

	if (dest_cnt == MML_MAX_OUTPUTS &&
	    ccfg->node->out_idx == MML_MAX_OUTPUTS - 1) {
		mml_msg("%s out_idx[%d] id[%d]", __func__,
			ccfg->node->out_idx, comp->id);
		mml_pq_wrot_callback(task);
	}

	wrot_store_crc(comp, task, ccfg);
}

static s32 mml_wrot_comp_clk_enable(struct mml_comp *comp)
{
	int ret;

	/* original clk enable */
	ret = mml_comp_clk_enable(comp);
	if (ret < 0)
		return ret;

	mml_mmp(clk_enable, MMPROFILE_FLAG_PULSE, comp->id, 0);

	return 0;
}

static s32 mml_wrot_comp_clk_disable(struct mml_comp *comp,
				     bool dpc)
{
	int ret;

	/* original clk enable */
	ret = mml_comp_clk_disable(comp, dpc);
	if (ret < 0)
		return ret;
	mml_mmp(clk_disable, MMPROFILE_FLAG_PULSE, comp->id, 0);

	return 0;
}

static const struct mml_comp_hw_ops wrot_hw_ops = {
	.init_frame_done_event = &wrot_init_frame_done_event,
	.clk_enable = &mml_wrot_comp_clk_enable,
	.clk_disable = &mml_wrot_comp_clk_disable,
	.qos_datasize_get = &wrot_datasize_get,
	.qos_stash_bw_get = &wrot_qos_stash_bw_get,
	.qos_format_get = &wrot_format_get,
	.qos_set = &mml_comp_qos_set,
	.qos_clear = &mml_comp_qos_clear,
	.task_done = wrot_task_done,
};

static const char *wrot_state(u32 state)
{
	switch (state) {
	case 0x0:
		return "sof";
	case 0x1:
		return "frame done";
	default:
		return "";
	}
}

static void wrot_debug_dump(struct mml_comp *comp)
{
	struct mml_comp_wrot *wrot = comp_to_wrot(comp);
	void __iomem *base = comp->base;
	u32 value[40];
	u32 debug[33];
	u32 dbg_id = 0, state, smi_req;
	u32 shadow_ctrl;
	u32 i;

	mml_err("wrot component %u dump:", comp->id);
	value[0] = readl(base + wrot->reg[VIDO_CTRL]);
	value[1] = readl(base + wrot->reg[VIDO_IN_SIZE]);
	value[2] = readl(base + wrot->reg[VIDO_TAR_SIZE]);
	value[3] = readl(base + wrot->reg[VIDO_BASE_ADDR_HIGH]);
	value[4] = readl(base + wrot->reg[VIDO_BASE_ADDR]);
	value[5] = readl(base + wrot->reg[VIDO_BASE_ADDR_HIGH_C]);
	value[6] = readl(base + wrot->reg[VIDO_BASE_ADDR_C]);
	value[7] = readl(base + wrot->reg[VIDO_BASE_ADDR_HIGH_V]);
	value[8] = readl(base + wrot->reg[VIDO_BASE_ADDR_V]);

	mml_err("shadow VIDO_CTRL %#010x VIDO_IN_SIZE %#010x VIDO_TAR_SIZE %#010x",
		value[0], value[1], value[2]);
	mml_err("shadow VIDO_BASE ADDR_HIGH   %#010x ADDR   %#010x",
		value[3], value[4]);
	mml_err("shadow VIDO_BASE ADDR_HIGH_C %#010x ADDR_C %#010x",
		value[5], value[6]);
	mml_err("shadow VIDO_BASE ADDR_HIGH_V %#010x ADDR_V %#010x",
		value[7], value[8]);

	/* Enable shadow read working */
	shadow_ctrl = readl(base + wrot->reg[VIDO_SHADOW_CTRL]);
	shadow_ctrl |= 0x4;
	writel(shadow_ctrl, base + wrot->reg[VIDO_SHADOW_CTRL]);

	value[0] = readl(base + wrot->reg[VIDO_CTRL]);
	value[1] = readl(base + wrot->reg[VIDO_DMA_PERF]);
	value[2] = readl(base + wrot->reg[VIDO_MAIN_BUF_SIZE]);
	value[3] = readl(base + wrot->reg[VIDO_SOFT_RST]);
	value[4] = readl(base + wrot->reg[VIDO_SOFT_RST_STAT]);
	value[5] = readl(base + wrot->reg[VIDO_INT]);
	value[6] = readl(base + wrot->reg[VIDO_IN_SIZE]);
	value[7] = readl(base + wrot->reg[VIDO_CROP_OFST]);
	value[8] = readl(base + wrot->reg[VIDO_TAR_SIZE]);
	value[9] = readl(base + wrot->reg[VIDO_FRAME_SIZE]);
	value[10] = readl(base + wrot->reg[VIDO_OFST_ADDR_HIGH]);
	value[11] = readl(base + wrot->reg[VIDO_OFST_ADDR]);
	value[12] = readl(base + wrot->reg[VIDO_OFST_ADDR_HIGH_C]);
	value[13] = readl(base + wrot->reg[VIDO_OFST_ADDR_C]);
	value[14] = readl(base + wrot->reg[VIDO_OFST_ADDR_HIGH_V]);
	value[15] = readl(base + wrot->reg[VIDO_OFST_ADDR_V]);
	value[16] = readl(base + wrot->reg[VIDO_STRIDE]);
	value[17] = readl(base + wrot->reg[VIDO_STRIDE_C]);
	value[18] = readl(base + wrot->reg[VIDO_STRIDE_V]);
	value[19] = readl(base + wrot->reg[VIDO_CTRL_2]);
	value[20] = readl(base + wrot->reg[VIDO_IN_LINE_ROT]);
	value[21] = readl(base + wrot->reg[VIDO_RSV_1]);
	value[22] = readl(base + wrot->reg[VIDO_ROT_EN]);
	value[23] = readl(base + wrot->reg[VIDO_SHADOW_CTRL]);
	value[24] = readl(base + wrot->reg[VIDO_PVRIC]);
	value[25] = readl(base + wrot->reg[VIDO_SCAN_10BIT]);
	value[26] = readl(base + wrot->reg[VIDO_PENDING_ZERO]);
	value[27] = readl(base + wrot->reg[VIDO_BASE_ADDR_HIGH]);
	value[28] = readl(base + wrot->reg[VIDO_BASE_ADDR]);
	value[29] = readl(base + wrot->reg[VIDO_BASE_ADDR_HIGH_C]);
	value[30] = readl(base + wrot->reg[VIDO_BASE_ADDR_C]);
	value[31] = readl(base + wrot->reg[VIDO_BASE_ADDR_HIGH_V]);
	value[32] = readl(base + wrot->reg[VIDO_BASE_ADDR_V]);
	value[33] = readl(base + wrot->reg[VIDO_CRC_CTRL]);
	value[34] = readl(base + wrot->reg[VIDO_CRC_VALUE]);
	value[35] = readl(base + wrot->reg[VIDO_MAT_CTRL]);
	value[36] = readl(base + wrot->reg[VIDO_DITHER_CON]);
	value[37] = readl(base + wrot->reg[VIDO_DITHER]);
	value[38] = readl(base + wrot->reg[VIDO_AFBC_YUVTRANS]);
	value[39] = readl(base + wrot->reg[VIDO_BKGD]);

	/* debug id from 0x0100 ~ 0x2100, count 33 which is debug array size */
	for (i = 0; i < ARRAY_SIZE(debug); i++) {
		dbg_id += 0x100;
		writel(dbg_id, base + wrot->reg[VIDO_INT_EN]);
		debug[i] = readl(base + wrot->reg[VIDO_DEBUG]);
	}

	mml_err("VIDO_CTRL %#010x VIDO_DMA_PERF %#010x VIDO_MAIN_BUF_SIZE %#010x",
		value[0], value[1], value[2]);
	mml_err("VIDO_SOFT_RST %#010x VIDO_SOFT_RST_STAT %#010x VIDO_INT %#010x",
		value[3], value[4], value[5]);
	mml_err("VIDO_IN_SIZE %#010x VIDO_CROP_OFST %#010x VIDO_TAR_SIZE %#010x",
		value[6], value[7], value[8]);
	mml_err("VIDO_FRAME_SIZE %#010x VIDO_AFBC_YUVTRANS %#010x VIDO_BKGD %#010x",
		value[9], value[38], value[39]);
	mml_err("VIDO_MAT_CTRL %#010x VIDO_DITHER_CON %#010x VIDO_DITHER %#010x",
		value[35], value[36], value[37]);
	if (value[33] || value[34])
		mml_err("VIDO_CRC_CTRL %#010x VIDO_CRC_VALUE %#010x", value[33], value[34]);
	mml_err("VIDO_OFST ADDR_HIGH   %#010x ADDR   %#010x",
		value[10], value[11]);
	mml_err("VIDO_OFST ADDR_HIGH_C %#010x ADDR_C %#010x",
		value[12], value[13]);
	mml_err("VIDO_OFST ADDR_HIGH_V %#010x ADDR_V %#010x",
		value[14], value[15]);
	mml_err("VIDO_STRIDE %#010x C %#010x V %#010x",
		value[16], value[17], value[18]);
	mml_err("VIDO_CTRL_2 %#010x VIDO_IN_LINE_ROT %#010x VIDO_RSV_1 %#010x",
		value[19], value[20], value[21]);
	mml_err("VIDO_ROT_EN %#010x VIDO_SHADOW_CTRL %#010x",
		value[22], value[23]);
	mml_err("VIDO_PVRIC %#010x VIDO_SCAN_10BIT %#010x VIDO_PENDING_ZERO %#010x",
		value[24], value[25], value[26]);
	mml_err("VIDO_BASE ADDR_HIGH   %#010x ADDR   %#010x",
		value[27], value[28]);
	mml_err("VIDO_BASE ADDR_HIGH_C %#010x ADDR_C %#010x",
		value[29], value[30]);
	mml_err("VIDO_BASE ADDR_HIGH_V %#010x ADDR_V %#010x",
		value[31], value[32]);

	for (i = 0; i < ARRAY_SIZE(debug) / 3; i++) {
		mml_err("VIDO_DEBUG %02X %#010x VIDO_DEBUG %02X %#010x VIDO_DEBUG %02X %#010x",
			i * 3 + 1, debug[i * 3],
			i * 3 + 2, debug[i * 3 + 1],
			i * 3 + 3, debug[i * 3 + 2]);
	}

	/* parse state */
	mml_err("WROT crop_busy:%u req:%u valid:%u",
		(debug[2] >> 1) & 0x1, (debug[2] >> 2) & 0x1,
		(debug[2] >> 3) & 0x1);
	state = debug[2] & 0x1;
	smi_req = (debug[24] >> 30) & 0x1;
	mml_err("WROT state: %#x (%s)", state, wrot_state(state));
	mml_err("WROT x_cnt %u y_cnt %u",
		debug[9] & 0x7f, (debug[9] >> 12) & 0x3ff);
	mml_err("WROT smi_req:%u => suggest to ask SMI help:%u", smi_req, smi_req);
}

static void wrot_reset(struct mml_comp *comp, struct mml_frame_config *cfg, u32 pipe)
{
	const struct mml_topology_path *path = cfg->path[pipe];
	struct mml_comp_wrot *wrot = comp_to_wrot(comp);

	if (cfg->info.mode == MML_MODE_RACING) {
		cmdq_clear_event(path->clt->chan, wrot->event_bufa);
		cmdq_clear_event(path->clt->chan, wrot->event_bufb);
		cmdq_clear_event(path->clt->chan, wrot->event_buf_next);
	}
}

static const struct mml_comp_debug_ops wrot_debug_ops = {
	.dump = &wrot_debug_dump,
	.reset = &wrot_reset,
};

static int mml_bind(struct device *dev, struct device *master, void *data)
{
	struct mml_comp_wrot *wrot = dev_get_drvdata(dev);
	struct drm_device *drm_dev = data;
	s32 ret;

	if (!drm_dev) {
		ret = mml_register_comp(master, &wrot->comp);
		if (ret)
			dev_err(dev, "Failed to register mml component %s: %d\n",
				dev->of_node->full_name, ret);
	} else {
		ret = mml_ddp_comp_register(drm_dev, &wrot->ddp_comp);
		if (ret)
			dev_err(dev, "Failed to register ddp component %s: %d\n",
				dev->of_node->full_name, ret);
		else
			wrot->ddp_bound = true;
	}
	return ret;
}

static void mml_unbind(struct device *dev, struct device *master, void *data)
{
	struct mml_comp_wrot *wrot = dev_get_drvdata(dev);
	struct drm_device *drm_dev = data;

	if (!drm_dev) {
		mml_unregister_comp(master, &wrot->comp);
	} else {
		mml_ddp_comp_unregister(drm_dev, &wrot->ddp_comp);
		wrot->ddp_bound = false;
	}
}

static const struct component_ops mml_comp_ops = {
	.bind	= mml_bind,
	.unbind = mml_unbind,
};

static struct mml_comp_wrot *dbg_probed_components[4];
static int dbg_probed_count;

static bool wrot_reg_read(struct mml_comp *comp, u32 addr, u32 value, u32 mask)
{
	bool return_value = false;
	u32 reg_value = 0;
	void __iomem *base = comp->base;

	reg_value = readl(base + addr);

	if ((reg_value & mask) == value)
		return_value = true;

	return return_value;
}

static void wrot_callback_work(struct work_struct *work_item)
{
#if !IS_ENABLED(CONFIG_MTK_MML_LEGACY)
	struct mml_comp_wrot *wrot = NULL;

	wrot = container_of(work_item, struct mml_comp_wrot, wrot_ai_callback_task);
	mml_pq_ir_wrot_callback(wrot->pq_task, wrot->frame_data,
		wrot->jobid, wrot->dual);
#endif
}

static irqreturn_t mml_wrot_irq_handler(int irq, void *dev_id)
{
	struct mml_comp_wrot *priv = dev_id;
	struct mml_comp *comp = &priv->comp;
	struct mml_comp_wrot *wrot = comp_to_wrot(comp);
	uint8_t dest_cnt = wrot->dest_cnt;
	irqreturn_t ret = IRQ_NONE;
	u8 out_idx = wrot->out_idx;

	if (wrot_reg_read(comp, wrot->reg[VIDO_INT], 0, (0x1))) {
		writel(1, comp->base + wrot->reg[VIDO_INT]);
		if (dest_cnt == MML_MAX_OUTPUTS && out_idx == MML_MAX_OUTPUTS - 1) {
			queue_work(priv->wrot_ai_callback_wq, &priv->wrot_ai_callback_task);
			return IRQ_HANDLED;
		}
	}
	return ret;
}

static int probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mml_comp_wrot *priv;
	s32 ret;
	int irq = -1;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	platform_set_drvdata(pdev, priv);
	priv->data = of_device_get_match_data(dev);
	priv->reg = priv->data->reg;

	if (smmu_v3_enabled()) {
		/* shared smmu device, setup 34bit in dts */
		priv->mmu_dev = mml_smmu_get_shared_device(dev, "mtk,smmu-shared");
		priv->mmu_dev_sec = mml_smmu_get_shared_device(dev, "mtk,smmu-shared-sec");
	} else {
		priv->mmu_dev = dev;
		priv->mmu_dev_sec = dev;
		ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(34));
		if (ret)
			mml_err("fail to config wrot dma mask %d", ret);
	}

	ret = mml_comp_init(pdev, &priv->comp);
	if (ret) {
		dev_err(dev, "Failed to init mml component: %d\n", ret);
		return ret;
	}

	/* init larb for smi and mtcmos */
	ret = mml_comp_init_larb(&priv->comp, dev);
	if (ret) {
		if (ret == -EPROBE_DEFER)
			return ret;
		dev_err(dev, "fail to init component %u larb ret %d\n",
			priv->comp.id, ret);
	}

	/* store smi larb con register for later use */
	priv->smi_larb_con = priv->comp.larb_base +
		SMI_LARB_NON_SEC_CON + priv->comp.larb_port * 4;
	mutex_init(&priv->sram_mutex);

	of_property_read_u16(dev->of_node, "event-frame-done",
			     &priv->event_eof);
	of_property_read_u16(dev->of_node, "event-bufa",
			     &priv->event_bufa);
	of_property_read_u16(dev->of_node, "event-bufb",
			     &priv->event_bufb);
	of_property_read_u16(dev->of_node, "event-buf-next",
			     &priv->event_buf_next);

	/* get index of wrot by alias */
	priv->idx = of_alias_get_id(dev->of_node, "mml-wrot");

	/* parse inline rot node for racing mode */
	priv->irot_base[0] = mml_get_node_base_pa(pdev, "inlinerot", 0, &priv->irot_va[0]);
	priv->irot_base[1] = mml_get_node_base_pa(pdev, "inlinerot", 1, &priv->irot_va[1]);

	/* assign ops */
	priv->comp.tile_ops = &wrot_tile_ops;
	priv->comp.config_ops = &wrot_cfg_ops;
	priv->comp.hw_ops = &wrot_hw_ops;
	priv->comp.debug_ops = &wrot_debug_ops;

	if (priv->data->read_mode == MML_PQ_EOF_MODE) {
		priv->wrot_ai_callback_wq = create_workqueue("wrot_ai_callback");
		INIT_WORK(&priv->wrot_ai_callback_task, wrot_callback_work);

		irq = platform_get_irq(pdev, 0);
		if (irq < 0)
			dev_info(dev, "Failed to get wrot irq for AI Callback: %d\n", irq);
		else {
			priv->irq = irq;
			ret = devm_request_irq(dev, irq, mml_wrot_irq_handler,
				IRQF_TRIGGER_NONE | IRQF_SHARED, dev_name(dev), priv);
			if (ret)
				dev_info(dev, "register irq fail: %d irg:%d\n", ret, irq);
			else
				dev_info(dev, "register irq success: %d irg:%d\n", ret, irq);
		}
	}

	dbg_probed_components[dbg_probed_count++] = priv;

	ret = mml_comp_add(priv->comp.id, dev, &mml_comp_ops);

	mml_log("wrot%d (%u) smi larb con %pa event eof %hu sync %hu/%hu/%hu",
		priv->idx, priv->comp.id, &priv->smi_larb_con,
		priv->event_eof,
		priv->event_bufa,
		priv->event_bufb,
		priv->event_buf_next);


	return ret;
}

static int remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &mml_comp_ops);
	component_del(&pdev->dev, &mml_comp_ops);
	return 0;
}

const struct of_device_id mml_wrot_driver_dt_match[] = {
	{
		.compatible = "mediatek,mt6983-mml_wrot",
		.data = &mt6983_wrot_data,
	},
	{
		.compatible = "mediatek,mt6893-mml_wrot",
		.data = &mt6983_wrot_data
	},
	{
		.compatible = "mediatek,mt6879-mml_wrot",
		.data = &mt6983_wrot_data
	},
	{
		.compatible = "mediatek,mt6895-mml_wrot",
		.data = &mt6983_wrot_data
	},
	{
		.compatible = "mediatek,mt6985-mml_wrot",
		.data = &mt6985_wrot_data,
	},
	{
		.compatible = "mediatek,mt6886-mml_wrot",
		.data = &mt6983_wrot_data
	},
	{
		.compatible = "mediatek,mt6897-mml_wrot",
		.data = &mt6985_wrot_data,
	},
	{
		.compatible = "mediatek,mt6899-mml0_wrot",
		.data = &mt6899_wrot_data,
	},
	{
		.compatible = "mediatek,mt6899-mml1_wrot",
		.data = &mt6899_wrot_data,
	},
	{
		.compatible = "mediatek,mt6989-mml_wrot",
		.data = &mt6989_wrot_data,
	},
	{
		.compatible = "mediatek,mt6878-mml_wrot",
		.data = &mt6878_wrot_data
	},
	{
		.compatible = "mediatek,mt6991-mml0_wrot",
		.data = &mt6991_wrot_data,
	},
	{
		.compatible = "mediatek,mt6991-mml1_wrot",
		.data = &mt6991_wrot_data,
	},
	{},
};
MODULE_DEVICE_TABLE(of, mml_wrot_driver_dt_match);

struct platform_driver mml_wrot_driver = {
	.probe = probe,
	.remove = remove,
	.driver = {
		.name = "mediatek-mml-wrot",
		.owner = THIS_MODULE,
		.of_match_table = mml_wrot_driver_dt_match,
	},
};

//module_platform_driver(mml_wrot_driver);

static s32 dbg_case;
static s32 dbg_set(const char *val, const struct kernel_param *kp)
{
	s32 result;

	result = kstrtos32(val, 0, &dbg_case);
	mml_log("%s: debug_case=%d", __func__, dbg_case);

	switch (dbg_case) {
	case 0:
		mml_log("use read to dump component status");
		break;
	default:
		mml_err("invalid debug_case: %d", dbg_case);
		break;
	}
	return result;
}

static s32 dbg_get(char *buf, const struct kernel_param *kp)
{
	s32 length = 0;
	u32 i;

	switch (dbg_case) {
	case 0:
		length += snprintf(buf + length, PAGE_SIZE - length,
			"[%d] probed count: %d\n", dbg_case, dbg_probed_count);
		for (i = 0; i < dbg_probed_count; i++) {
			struct mml_comp *comp = &dbg_probed_components[i]->comp;

			length += snprintf(buf + length, PAGE_SIZE - length,
				"  - [%d] mml comp_id: %d.%d @%pa name: %s bound: %d\n", i,
				comp->id, comp->sub_idx, &comp->base_pa,
				comp->name ? comp->name : "(null)", comp->bound);
			length += snprintf(buf + length, PAGE_SIZE - length,
				"  -         larb_port: %d @%pa pw: %d clk: %d\n",
				comp->larb_port, &comp->larb_base,
				comp->pw_cnt, comp->clk_cnt);
			length += snprintf(buf + length, PAGE_SIZE - length,
				"  -     ddp comp_id: %d bound: %d\n",
				dbg_probed_components[i]->ddp_comp.id,
				dbg_probed_components[i]->ddp_bound);
		}
		break;
	default:
		mml_err("not support read for debug_case: %d", dbg_case);
		break;
	}
	buf[length] = '\0';

	return length;
}

static const struct kernel_param_ops dbg_param_ops = {
	.set = dbg_set,
	.get = dbg_get,
};
module_param_cb(wrot_debug, &dbg_param_ops, NULL, 0644);
MODULE_PARM_DESC(wrot_debug, "mml wrot debug case");

MODULE_AUTHOR("Dennis-YC Hsieh <dennis-yc.hsieh@mediatek.com>");
MODULE_DESCRIPTION("MediaTek SoC display MML WROT driver");
MODULE_LICENSE("GPL v2");
