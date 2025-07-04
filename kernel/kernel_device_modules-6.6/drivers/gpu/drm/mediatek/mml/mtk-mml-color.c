// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2021 MediaTek Inc.
 */

#include <linux/component.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <mtk_drm_ddp_comp.h>

#include "mtk-mml-core.h"
#include "mtk-mml-driver.h"
#include "mtk-mml-dle-adaptor.h"
#include "mtk-mml-pq-core.h"
#include "tile_driver.h"

#undef pr_fmt
#define pr_fmt(fmt) "[mml_pq_color]" fmt

#define COLOR_WAIT_TIMEOUT_MS (50)
#define COLOR_REG_NUM (368)

#define REG_NOT_SUPPORT 0xfff


enum mml_color_reg_index {
	COLOR_CFG_MAIN,
	COLOR_PXL_CNT_MAIN,
	COLOR_LINE_CNT_MAIN,
	COLOR_WIN_X_MAIN,
	COLOR_WIN_Y_MAIN,
	COLOR_DBG_CFG_MAIN,
	COLOR_START,
	COLOR_INTEN,
	COLOR_INTSTA,
	COLOR_OUT_SEL,
	COLOR_FRAME_DONE_DEL,
	COLOR_INTERNAL_IP_WIDTH,
	COLOR_INTERNAL_IP_HEIGHT,
	COLOR_CM1_EN,
	COLOR_CM2_EN,
	COLOR_SHADOW_CTRL,
	COLOR_REGIONAL_ENABLE_LENGHTH,
	COLOR_MISC,
	COLOR_REG_MAX_COUNT
};

static const u16 colo_reg_table_mt6983[COLOR_REG_MAX_COUNT] = {
	[COLOR_CFG_MAIN] = 0x400,
	[COLOR_PXL_CNT_MAIN] = 0x404,
	[COLOR_LINE_CNT_MAIN] = 0x408,
	[COLOR_WIN_X_MAIN] = 0x40c,
	[COLOR_WIN_Y_MAIN] = 0x410,
	[COLOR_DBG_CFG_MAIN] = 0x420,
	[COLOR_START] = 0xc00,
	[COLOR_INTEN] = 0xc04,
	[COLOR_INTSTA] = 0xc08,
	[COLOR_OUT_SEL] = 0xc0c,
	[COLOR_FRAME_DONE_DEL] = 0xc10,
	[COLOR_INTERNAL_IP_WIDTH] = 0xc50,
	[COLOR_INTERNAL_IP_HEIGHT] = 0xc54,
	[COLOR_CM1_EN] = 0xc60,
	[COLOR_CM2_EN] = 0xca0,
	[COLOR_SHADOW_CTRL] = 0xcb0,
	[COLOR_MISC] = REG_NOT_SUPPORT,
	[COLOR_REGIONAL_ENABLE_LENGHTH] = REG_NOT_SUPPORT,
};

static const u16 colo_reg_table_mt6989[COLOR_REG_MAX_COUNT] = {
	[COLOR_CFG_MAIN] = 0x400,
	[COLOR_PXL_CNT_MAIN] = 0x404,
	[COLOR_LINE_CNT_MAIN] = 0x408,
	[COLOR_WIN_X_MAIN] = 0x40c,
	[COLOR_WIN_Y_MAIN] = 0x410,
	[COLOR_DBG_CFG_MAIN] = 0x420,
	[COLOR_START] = 0xc00,
	[COLOR_INTEN] = 0xc04,
	[COLOR_INTSTA] = 0xc08,
	[COLOR_OUT_SEL] = 0xc0c,
	[COLOR_FRAME_DONE_DEL] = 0xc10,
	[COLOR_INTERNAL_IP_WIDTH] = 0xc50,
	[COLOR_INTERNAL_IP_HEIGHT] = 0xc54,
	[COLOR_CM1_EN] = 0xc60,
	[COLOR_CM2_EN] = 0xca0,
	[COLOR_SHADOW_CTRL] = 0xcb0,
	[COLOR_REGIONAL_ENABLE_LENGHTH] = 0xf08,
	[COLOR_MISC] = 0xf0c,
};


struct color_data {
	const u16 *reg_table;
	u8 px_per_tick;
};

static const struct color_data mt6983_color_data = {
	.reg_table = colo_reg_table_mt6983,
	.px_per_tick = 1,
};

static const struct color_data mt6989_color_data = {
	.reg_table = colo_reg_table_mt6989,
	.px_per_tick = 2,
};


struct mml_comp_color {
	struct mtk_ddp_comp ddp_comp;
	struct mml_comp comp;
	const struct color_data *data;
	bool ddp_bound;
};

/* meta data for each different frame config */
struct color_frame_data {
	u16 labels[COLOR_REG_NUM];
	struct mml_frame_size max_size;
	bool config_success;
};

#define color_frm_data(i)	((struct color_frame_data *)(i->data))

static inline struct mml_comp_color *comp_to_color(struct mml_comp *comp)
{
	return container_of(comp, struct mml_comp_color, comp);
}

static s32 color_prepare(struct mml_comp *comp, struct mml_task *task,
			 struct mml_comp_config *ccfg)
{
	struct color_frame_data *color_frm = NULL;

	color_frm = kzalloc(sizeof(*color_frm), GFP_KERNEL);

	ccfg->data = color_frm;
	return 0;
}

static s32 color_buf_prepare(struct mml_comp *comp, struct mml_task *task,
			     struct mml_comp_config *ccfg)
{
	struct mml_frame_config *cfg = task->config;
	struct mml_frame_dest *dest = &cfg->info.dest[ccfg->node->out_idx];
	s32 ret = 0;

	if (!dest->pq_config.en_color)
		return ret;

	mml_pq_trace_ex_begin("%s", __func__);
	ret = mml_pq_set_comp_config(task);
	mml_pq_trace_ex_end();
	return ret;
}

static s32 color_tile_prepare(struct mml_comp *comp, struct mml_task *task,
			      struct mml_comp_config *ccfg,
			      struct tile_func_block *func,
			      union mml_tile_data *data)
{
	struct mml_frame_dest *dest =
		&task->config->info.dest[ccfg->node->out_idx];

	func->enable_flag = dest->pq_config.en_color;
	return 0;
}

static const struct mml_comp_tile_ops color_tile_ops = {
	.prepare = color_tile_prepare,
};

static void color_init(struct mml_comp *comp, struct cmdq_pkt *pkt, const phys_addr_t base_pa,
	bool shadow)
{
	struct mml_comp_color *color = comp_to_color(comp);

	/* relay mode */
	cmdq_pkt_write(pkt, NULL,
		base_pa + color->data->reg_table[COLOR_START], 3, U32_MAX);
	cmdq_pkt_write(pkt, NULL,
		base_pa + color->data->reg_table[COLOR_CM1_EN], 0, 0x00000001);
	cmdq_pkt_write(pkt, NULL,
		base_pa + color->data->reg_table[COLOR_CM2_EN], 0, 0x00000001);
	/* Enable shadow */
	cmdq_pkt_write(pkt, NULL, base_pa + color->data->reg_table[COLOR_SHADOW_CTRL],
		(shadow ? 0 : 1) | 0x2, U32_MAX);
}

static u32 color_get_label_count(struct mml_comp *comp, struct mml_task *task,
			struct mml_comp_config *ccfg)
{
	struct mml_frame_config *cfg = task->config;
	const struct mml_frame_dest *dest = &cfg->info.dest[ccfg->node->out_idx];

	mml_pq_msg("%s pipe_id[%d] engine_id[%d] en_color[%d]", __func__,
		ccfg->pipe, comp->id, dest->pq_config.en_color);

	if (!dest->pq_config.en_color)
		return 0;

	return COLOR_REG_NUM;
}

static s32 color_config_init(struct mml_comp *comp, struct mml_task *task,
			     struct mml_comp_config *ccfg)
{
	color_init(comp, task->pkts[ccfg->pipe], comp->base_pa, task->config->shadow);
	return 0;
}

static struct mml_pq_comp_config_result *get_color_comp_config_result(
	struct mml_task *task)
{
	return task->pq_task->comp_config.result;
}

static void color_config_region_pq(struct mml_comp *comp, struct cmdq_pkt *pkt,
			const phys_addr_t base_pa, const struct mml_pq_config *cfg)
{
	struct mml_comp_color *color = comp_to_color(comp);

	if (color->data->reg_table[COLOR_REGIONAL_ENABLE_LENGHTH] != REG_NOT_SUPPORT) {
		if (!cfg->en_region_pq) {
			mml_pq_msg("%s:disable region pq", __func__);

			cmdq_pkt_write(pkt, NULL,
				base_pa + color->data->reg_table[COLOR_REGIONAL_ENABLE_LENGHTH],
				0, U32_MAX);
		}
	}
}

static s32 color_config_frame(struct mml_comp *comp, struct mml_task *task,
			      struct mml_comp_config *ccfg)
{
	struct mml_frame_config *cfg = task->config;
	struct color_frame_data *color_frm = color_frm_data(ccfg);
	struct cmdq_pkt *pkt = task->pkts[ccfg->pipe];
	const struct mml_frame_dest *dest = &cfg->info.dest[ccfg->node->out_idx];
	struct mml_comp_color *color = comp_to_color(comp);
	const phys_addr_t base_pa = comp->base_pa;
	struct mml_pq_comp_config_result *result = NULL;
	struct mml_task_reuse *reuse = &task->reuse[ccfg->pipe];
	struct mml_pipe_cache *cache = &cfg->cache[ccfg->pipe];
	u32 alpha = cfg->info.alpha ? 1 : 0;
	s32 ret = 0;
	s32 i;
	struct mml_pq_reg *regs = NULL;

	cmdq_pkt_write(pkt, NULL, base_pa + color->data->reg_table[COLOR_MISC], alpha, U32_MAX);

	if (!dest->pq_config.en_color) {
		cmdq_pkt_write(pkt, NULL,
			base_pa + color->data->reg_table[COLOR_START], 0x3, U32_MAX);

		return ret;
	}

	color_config_region_pq(comp, pkt, base_pa, &dest->pq_config);

	cmdq_pkt_write(pkt, NULL, base_pa + color->data->reg_table[COLOR_START], 0x1, U32_MAX);

	do {
		ret = mml_pq_get_comp_config_result(task, COLOR_WAIT_TIMEOUT_MS);
		if (ret) {
			mml_pq_comp_config_clear(task);
			color_frm->config_success = false;
			cmdq_pkt_write(pkt, NULL,
				base_pa + color->data->reg_table[COLOR_START], 3, U32_MAX);
			mml_pq_err("%s: get color param timeout: %d in %dms", __func__,
				ret, COLOR_WAIT_TIMEOUT_MS);
			ret = -ETIMEDOUT;
			goto exit;
		}

		result = get_color_comp_config_result(task);
		if (!result) {
			color_frm->config_success = false;
			cmdq_pkt_write(pkt, NULL,
				base_pa + color->data->reg_table[COLOR_START], 3, U32_MAX);
			mml_pq_err("%s: not get result from user lib", __func__);
			ret = -EBUSY;
			goto exit;
		}
	} while ((mml_pq_debug_mode & MML_PQ_SET_TEST) && result->is_set_test);

	regs = result->color_regs;

	/* TODO: use different regs */
	mml_pq_msg("%s:config color regs, count: %d", __func__,
		result->color_reg_cnt);
	color_frm->config_success = true;
	for (i = 0; i < result->color_reg_cnt; i++) {
		mml_write(comp->id, pkt, base_pa + regs[i].offset, regs[i].value,
			regs[i].mask, reuse, cache,
			&color_frm->labels[i]);
		mml_pq_msg("[color][config][%x] = %#x mask(%#x)",
			regs[i].offset, regs[i].value, regs[i].mask);
	}

exit:
	return ret;
}

static s32 color_config_tile(struct mml_comp *comp, struct mml_task *task,
			     struct mml_comp_config *ccfg, u32 idx)
{
	struct mml_frame_config *cfg = task->config;
	struct cmdq_pkt *pkt = task->pkts[ccfg->pipe];
	struct mml_comp_color *color = comp_to_color(comp);
	struct color_frame_data *color_frm = color_frm_data(ccfg);
	const phys_addr_t base_pa = comp->base_pa;

	struct mml_tile_engine *tile = config_get_tile(cfg, ccfg, idx);

	u32 width = tile->in.xe - tile->in.xs + 1;
	u32 height = tile->in.ye - tile->in.ys + 1;

	cmdq_pkt_write(pkt, NULL,
		base_pa + color->data->reg_table[COLOR_INTERNAL_IP_WIDTH], width, U32_MAX);
	cmdq_pkt_write(pkt, NULL,
		base_pa + color->data->reg_table[COLOR_INTERNAL_IP_HEIGHT], height, U32_MAX);

	/* dvfs and qos accumulate tile pixel */
	color_frm->max_size.width += width;
	color_frm->max_size.height = height;

	mml_pq_msg("[color] %s width [%d] height[%d]", __func__, width, height);
	return 0;
}

static s32 color_config_post(struct mml_comp *comp, struct mml_task *task,
			     struct mml_comp_config *ccfg)
{
	const struct mml_comp_color *color = comp_to_color(comp);
	struct mml_frame_config *cfg = task->config;
	struct mml_frame_dest *dest = &cfg->info.dest[ccfg->node->out_idx];
	struct mml_pipe_cache *cache = &cfg->cache[ccfg->pipe];
	struct color_frame_data *color_frm = color_frm_data(ccfg);
	u32 px_per_tick = color->data->px_per_tick;

	if (dest->pq_config.en_region_pq)
		px_per_tick = 1;
	dvfs_cache_sz(cache, color_frm->max_size.width / px_per_tick,
		color_frm->max_size.height, 0, 0);
	dvfs_cache_log(cache, comp, "color");

	if (dest->pq_config.en_color)
		mml_pq_put_comp_config_result(task);
	return 0;
}

static s32 color_reconfig_frame(struct mml_comp *comp, struct mml_task *task,
			      struct mml_comp_config *ccfg)
{
	struct mml_frame_config *cfg = task->config;
	struct color_frame_data *color_frm = color_frm_data(ccfg);
	const struct mml_frame_dest *dest = &cfg->info.dest[ccfg->node->out_idx];
	struct mml_pq_comp_config_result *result = NULL;
	struct mml_task_reuse *reuse = &task->reuse[ccfg->pipe];
	s32 ret = 0;
	s32 i;
	struct mml_pq_reg *regs = NULL;

	if (!dest->pq_config.en_color)
		return ret;

	do {
		ret = mml_pq_get_comp_config_result(task, COLOR_WAIT_TIMEOUT_MS);

		if (ret) {
			mml_pq_comp_config_clear(task);
			mml_pq_err("get color param timeout: %d in %dms",
				ret, COLOR_WAIT_TIMEOUT_MS);
			ret = -ETIMEDOUT;
			goto exit;
		}

		result = get_color_comp_config_result(task);
		if (!result || !color_frm->config_success || !result->color_reg_cnt) {
			mml_pq_err("%s: not get result from user lib", __func__);
			ret = -EBUSY;
			goto exit;
		}

	} while ((mml_pq_debug_mode & MML_PQ_SET_TEST) && result->is_set_test);

	regs = result->color_regs;

	/* TODO: use different regs */
	mml_pq_msg("%s:config color regs, count: %d", __func__,
		result->color_reg_cnt);
	for (i = 0; i < result->color_reg_cnt; i++) {
		mml_update(comp->id, reuse, color_frm->labels[i], regs[i].value);
		mml_pq_msg("[color][config][%x] = %#x mask(%#x)",
			regs[i].offset, regs[i].value, regs[i].mask);
	}

exit:
	return ret;
}

static const struct mml_comp_config_ops color_cfg_ops = {
	.prepare = color_prepare,
	.buf_prepare = color_buf_prepare,
	.get_label_count = color_get_label_count,
	.init = color_config_init,
	.frame = color_config_frame,
	.tile = color_config_tile,
	.post = color_config_post,
	.reframe = color_reconfig_frame,
	.repost = color_config_post,
};

static u32 read_reg_value(struct mml_comp *comp, u16 reg)
{
	void __iomem *base = comp->base;

	if (reg == REG_NOT_SUPPORT) {
		mml_err("%s color reg is not support", __func__);
		return 0xFFFFFFFF;
	}

	return readl(base + reg);
}

static void color_debug_dump(struct mml_comp *comp)
{
	struct mml_comp_color *color = comp_to_color(comp);
	void __iomem *base = comp->base;
	u32 value[14];
	u32 shadow_ctrl;

	mml_err("color component %u dump:", comp->id);

	/* Enable shadow read working */
	shadow_ctrl = read_reg_value(comp, color->data->reg_table[COLOR_SHADOW_CTRL]);
	shadow_ctrl |= 0x4;
	writel(shadow_ctrl, base + color->data->reg_table[COLOR_SHADOW_CTRL]);

	value[0] = read_reg_value(comp, color->data->reg_table[COLOR_CFG_MAIN]);
	value[1] = read_reg_value(comp, color->data->reg_table[COLOR_PXL_CNT_MAIN]);
	value[2] = read_reg_value(comp, color->data->reg_table[COLOR_LINE_CNT_MAIN]);
	value[3] = read_reg_value(comp, color->data->reg_table[COLOR_WIN_X_MAIN]);
	value[4] = read_reg_value(comp, color->data->reg_table[COLOR_WIN_Y_MAIN]);
	value[5] = read_reg_value(comp, color->data->reg_table[COLOR_DBG_CFG_MAIN]);
	value[6] = read_reg_value(comp, color->data->reg_table[COLOR_START]);
	value[7] = read_reg_value(comp, color->data->reg_table[COLOR_INTEN]);
	value[8] = read_reg_value(comp, color->data->reg_table[COLOR_INTSTA]);
	value[9] = read_reg_value(comp, color->data->reg_table[COLOR_OUT_SEL]);
	value[10] = read_reg_value(comp, color->data->reg_table[COLOR_FRAME_DONE_DEL]);
	value[11] = read_reg_value(comp, color->data->reg_table[COLOR_INTERNAL_IP_WIDTH]);
	value[12] = read_reg_value(comp, color->data->reg_table[COLOR_INTERNAL_IP_HEIGHT]);
	value[13] = read_reg_value(comp, color->data->reg_table[COLOR_MISC]);

	mml_err("COLOR_CFG_MAIN %#010x COLOR_PXL_CNT_MAIN %#010x COLOR_LINE_CNT_MAIN %#010x",
		value[0], value[1], value[2]);
	mml_err("COLOR_WIN_X_MAIN %#010x COLOR_WIN_Y_MAIN %#010x",
		value[3], value[4]);
	mml_err("COLOR_DBG_CFG_MAIN %#010x COLOR_START %#010x COLOR_INTEN %#010x",
		value[5], value[6], value[7]);
	mml_err("COLOR_INTSTA %#010x COLOR_OUT_SEL %#010x COLOR_FRAME_DONE_DEL %#010x",
		value[8], value[9], value[10]);
	mml_err("COLOR_INTERNAL_IP_WIDTH %#010x COLOR_INTERNAL_IP_HEIGHT %#010x",
		value[11], value[12]);
	mml_err("COLOR_MISC %#010x",
		value[13]);
}

static const struct mml_comp_debug_ops color_debug_ops = {
	.dump = &color_debug_dump,
};

static int mml_bind(struct device *dev, struct device *master, void *data)
{
	struct mml_comp_color *color = dev_get_drvdata(dev);
	struct drm_device *drm_dev = data;
	s32 ret;

	if (!drm_dev) {
		ret = mml_register_comp(master, &color->comp);
		if (ret)
			dev_err(dev, "Failed to register mml component %s: %d\n",
				dev->of_node->full_name, ret);
	} else {
		ret = mml_ddp_comp_register(drm_dev, &color->ddp_comp);
		if (ret)
			dev_err(dev, "Failed to register ddp component %s: %d\n",
				dev->of_node->full_name, ret);
		else
			color->ddp_bound = true;
	}
	return ret;
}

static void mml_unbind(struct device *dev, struct device *master, void *data)
{
	struct mml_comp_color *color = dev_get_drvdata(dev);
	struct drm_device *drm_dev = data;

	if (!drm_dev) {
		mml_unregister_comp(master, &color->comp);
	} else {
		mml_ddp_comp_unregister(drm_dev, &color->ddp_comp);
		color->ddp_bound = false;
	}
}

static const struct component_ops mml_comp_ops = {
	.bind	= mml_bind,
	.unbind = mml_unbind,
};

static struct mml_comp_color *dbg_probed_components[2];
static int dbg_probed_count;

static int probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mml_comp_color *priv;
	s32 ret;
	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	platform_set_drvdata(pdev, priv);
	priv->data = of_device_get_match_data(dev);

	ret = mml_comp_init(pdev, &priv->comp);
	if (ret) {
		dev_err(dev, "Failed to init mml component: %d\n", ret);
		return ret;
	}
	/* assign ops */
	priv->comp.tile_ops = &color_tile_ops;
	priv->comp.config_ops = &color_cfg_ops;
	priv->comp.debug_ops = &color_debug_ops;

	if (unlikely(dbg_probed_count < 0))
		return -EFAULT;

	dbg_probed_components[dbg_probed_count++] = priv;

	ret = mml_comp_add(priv->comp.id, dev, &mml_comp_ops);

	return ret;
}

static int remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &mml_comp_ops);
	component_del(&pdev->dev, &mml_comp_ops);
	return 0;
}

const struct of_device_id mml_color_driver_dt_match[] = {
	{
		.compatible = "mediatek,mt6983-mml_color",
		.data = &mt6983_color_data,
	},
	{
		.compatible = "mediatek,mt6893-mml_color",
		.data = &mt6983_color_data,
	},
	{
		.compatible = "mediatek,mt6879-mml_color",
		.data = &mt6983_color_data,
	},
	{
		.compatible = "mediatek,mt6895-mml_color",
		.data = &mt6983_color_data,
	},
	{
		.compatible = "mediatek,mt6985-mml_color",
		.data = &mt6983_color_data,
	},
	{
		.compatible = "mediatek,mt6886-mml_color",
		.data = &mt6983_color_data,
	},
	{
		.compatible = "mediatek,mt6897-mml_color",
		.data = &mt6983_color_data,
	},
	{
		.compatible = "mediatek,mt6899-mml_color",
		.data = &mt6989_color_data,
	},
	{
		.compatible = "mediatek,mt6989-mml_color",
		.data = &mt6989_color_data,
	},
	{
		.compatible = "mediatek,mt6878-mml_color",
		.data = &mt6983_color_data,
	},
	{
		.compatible = "mediatek,mt6991-mml_color",
		.data = &mt6989_color_data,
	},
	{},
};
MODULE_DEVICE_TABLE(of, mml_color_driver_dt_match);

struct platform_driver mml_color_driver = {
	.probe = probe,
	.remove = remove,
	.driver = {
		.name = "mediatek-mml-color",
		.owner = THIS_MODULE,
		.of_match_table = mml_color_driver_dt_match,
	},
};

//module_platform_driver(mml_color_driver);

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
module_param_cb(color_debug, &dbg_param_ops, NULL, 0644);
MODULE_PARM_DESC(color_debug, "mml color debug case");

MODULE_AUTHOR("Dennis-YC Hsieh <dennis-yc.hsieh@mediatek.com>");
MODULE_DESCRIPTION("MediaTek SoC display MML COLOR driver");
MODULE_LICENSE("GPL v2");
