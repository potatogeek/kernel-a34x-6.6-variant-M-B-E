/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2021 MediaTek Inc.
 */

#ifndef __MTK_DRM_TRACE__
#define __MTK_DRM_TRACE__

#include "mtk_drm_ddp_comp.h"

#define DRM_TRACE_ID 0xFFFF0000
#define DRM_TRACE_FPS_ID (DRM_TRACE_ID + 1)
#define DRM_TRACE_FENCE_ID (DRM_TRACE_ID + 2)
#define DRM_TRACE_VSYNC_ID (DRM_TRACE_ID + 3)

enum TRACE_PATTERN {
	TRACE_OFF = 0,
	TRACE_ON,
	TRACE_MARK
};

/* MTK_DRM FTRACE */
extern unsigned int g_trace_log;
#define mtk_drm_trace_begin(fmt, args...) do { \
	if (g_trace_log) { \
		mtk_drm_print_trace( \
			"B|%d|"fmt"\n", current->tgid, ##args); \
	} \
} while (0)

#define mtk_drm_trace_end(fmt, args...) do { \
	if (g_trace_log) { \
		mtk_drm_print_trace("E|%d|"fmt"\n", \
		current->tgid, ##args); \
	} \
} while (0)

#define mtk_drm_trace_async_begin(fmt, args...) do { \
	if (g_trace_log >= 2) { \
		mtk_drm_print_trace( \
			"S|%d|"fmt"\n", current->tgid, ##args); \
	} \
} while (0)

#define mtk_drm_trace_async_end(fmt, args...) do { \
	if (g_trace_log >= 2) { \
		mtk_drm_print_trace( \
			"F|%d|"fmt"\n", current->tgid, ##args); \
	} \
} while (0)

#define mtk_drm_trace_c(fmt, args...) do { \
	if (g_trace_log) { \
		mtk_drm_print_trace( \
			"C|"fmt"\n", ##args); \
	} \
} while (0)

#define mtk_drm_trace_default(fmt, args...) mtk_drm_print_trace("C|"fmt"\n", ##args)

void mtk_drm_print_trace(char *fmt, ...);
void drm_trace_tag_start(const char *tag);
void drm_trace_tag_end(const char *tag);
void drm_trace_tag_mark(const char *tag);
void drm_trace_tag_mark_bycrtc(const char *tag, int crtc_index);
void drm_trace_tag_value(const char *tag, unsigned long value);
void mtk_drm_refresh_tag_start(struct mtk_ddp_comp *ddp_comp);
void mtk_drm_refresh_tag_end(struct mtk_ddp_comp *ddp_comp);
void mtk_drm_default_tag(struct mtk_ddp_comp *ddp_comp, const char *tag, enum TRACE_PATTERN pat);
void MMPathTraceDRM(struct mtk_ddp_comp *ddp_comp);
int MMPathTraceRDMA(struct mtk_ddp_comp *ddp_comp, char *str,
	unsigned int strlen, unsigned int n);
int MMPathTraceWDMA(struct mtk_ddp_comp *ddp_comp, char *str,
	unsigned int strlen, unsigned int n);
int MMPathTraceCrtcPlanes(struct drm_crtc *crtc,
	char *str, int strlen, int n);
#endif
