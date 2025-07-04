/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2021 MediaTek Inc.
 */

#ifndef __MTKFB_LOG_H
#define __MTKFB_LOG_H

#include <linux/kernel.h>
#include <linux/sched/clock.h>

#if IS_ENABLED(CONFIG_MTK_AEE_FEATURE)
#include <aee.h>
#endif

#if IS_ENABLED(CONFIG_MTK_MME_SUPPORT)
#include "mmevent_function.h"
#endif

extern unsigned long long mutex_time_start;
extern unsigned long long mutex_time_end;
extern long long mutex_time_period;
extern const char *mutex_locker;
extern unsigned int g_trace_log;

#ifndef DRM_TRACE_ID
#define DRM_TRACE_ID 0xFFFF0000
#endif
extern void mtk_drm_print_trace(char *fmt, ...);
extern int mtk_vidle_power_keep(void);
extern void mtk_vidle_power_release(void);

#define mtk_drm_trace_tag_begin(fmt, args...) do { \
	if (g_trace_log) { \
		mtk_drm_print_trace( \
			"B|%d|"fmt"\n", DRM_TRACE_ID, ##args); \
	} \
} while (0)

#define mtk_drm_trace_tag_end(fmt, args...) do { \
	if (g_trace_log) { \
		mtk_drm_print_trace( \
			"E|%d|"fmt"\n", DRM_TRACE_ID, ##args); \
	} \
} while (0)

enum DPREC_LOGGER_PR_TYPE {
	DPREC_LOGGER_ERROR,
	DPREC_LOGGER_FENCE,
	DPREC_LOGGER_DEBUG,
	DPREC_LOGGER_DUMP,
	DPREC_LOGGER_STATUS,
	DPREC_LOGGER_PR_NUM
};

int mtk_dprec_logger_pr(unsigned int type, char *fmt, ...);

#if IS_ENABLED(CONFIG_MTK_MME_SUPPORT)
#define DDP_EXTEND_MSG(fmt, arg...) \
	MME_EXTEND_INFO(MME_MODULE_DISP, MME_BUFFER_INDEX_2, fmt, ##arg)

#define DDPINFO(fmt, arg...)                                               \
	do {                                                                   \
		MME_INFO(MME_MODULE_DISP, MME_BUFFER_INDEX_2, fmt, ##arg);      \
		if (g_mobile_log)                                              \
			pr_info("[DISP]" pr_fmt(fmt), ##arg);     \
	} while (0)

#define DDPDBG(fmt, arg...)                                                    \
	do {                                                                   \
		if (!g_detail_log)                                             \
			break;                                                 \
		MME_INFO(MME_MODULE_DISP, MME_BUFFER_INDEX_2, fmt, ##arg);      \
		if (g_mobile_log)                                              \
			pr_info("[DISP]" pr_fmt(fmt), ##arg);     \
	} while (0)

#define DDPDBG_BWM(fmt, arg...)                                                    \
	do {                                                                   \
		if (!g_ovl_bwm_debug)                                             \
			break;                                                 \
		MME_INFO(MME_MODULE_DISP, MME_BUFFER_INDEX_2, fmt, ##arg);      \
		if (g_mobile_log)                                              \
			pr_info("[DISP]" pr_fmt(fmt), ##arg);     \
	} while (0)

#define DDP_PROFILE(fmt, arg...)                                               \
	do {                                                                   \
		if (!g_profile_log)                                            \
			break;                                                 \
		MME_INFO(MME_MODULE_DISP, MME_BUFFER_INDEX_2, fmt, ##arg);      \
		if (g_mobile_log)                                              \
			pr_info("[DISP]" pr_fmt(fmt), ##arg);     \
	} while (0)

#define DDPQOS(fmt, arg...)                                                    \
	do {                                                                   \
		if (!g_qos_log && !g_detail_log)                               \
			break;                                                 \
		MME_INFO(MME_MODULE_DISP, MME_BUFFER_INDEX_2, fmt, ##arg);     \
		if (g_mobile_log)                                              \
			pr_info("[DISP]" pr_fmt(fmt), ##arg);                  \
	} while (0)

#define DDPMSG(fmt, arg...)                                                    \
	do {                                                                   \
		MME_INFO(MME_MODULE_DISP, MME_BUFFER_INDEX_2, fmt, ##arg);      \
		pr_info("[DISP]" pr_fmt(fmt), ##arg);             \
	} while (0)

#define DDPDUMP(fmt, arg...)                                                   \
	do {                                                                   \
		MME_INFO(MME_MODULE_DISP, MME_BUFFER_INDEX_3, fmt, ##arg);      \
		if (g_mobile_log)                                              \
			pr_info("[DISP]" pr_fmt(fmt), ##arg);     \
	} while (0)

#define DDPFENCE(fmt, arg...)                                                  \
	do {                                                                   \
		MME_INFO(MME_MODULE_DISP, MME_BUFFER_INDEX_1, fmt, ##arg);      \
		if (g_fence_log)                                               \
			pr_info("[DISP]" pr_fmt(fmt), ##arg);     \
	} while (0)

#define DDPPR_ERR(fmt, arg...)                                                 \
	do {                                                                   \
		MME_INFO(MME_MODULE_DISP, MME_BUFFER_INDEX_0, fmt, ##arg);      \
		pr_err("[DISP][E]" pr_fmt(fmt), ##arg);              \
	} while (0)

#define DDPIRQ(fmt, arg...)                                                    \
	MME_INFO(MME_MODULE_DISP, MME_BUFFER_INDEX_4, fmt, ##arg)

#else

#define DDPINFO(fmt, arg...)                                                   \
	do {                                                                   \
		mtk_dprec_logger_pr(DPREC_LOGGER_DEBUG, fmt, ##arg);           \
		if (g_mobile_log)                                              \
			pr_info("[DISP]" pr_fmt(fmt), ##arg);     \
	} while (0)

#define DDPDBG(fmt, arg...)                                                    \
	do {                                                                   \
		if (!g_detail_log)                                             \
			break;                                                 \
		mtk_dprec_logger_pr(DPREC_LOGGER_DEBUG, fmt, ##arg);           \
		if (g_mobile_log)                                              \
			pr_info("[DISP]" pr_fmt(fmt), ##arg);     \
	} while (0)

#define DDPDBG_BWM(fmt, arg...)                                                    \
	do {                                                                   \
		if (!g_ovl_bwm_debug)                                             \
			break;                                                 \
		mtk_dprec_logger_pr(DPREC_LOGGER_DEBUG, fmt, ##arg);           \
		if (g_mobile_log)                                              \
			pr_info("[DISP]" pr_fmt(fmt), ##arg);     \
	} while (0)

#define DDP_PROFILE(fmt, arg...)                                               \
	do {                                                                   \
		if (!g_profile_log)                                            \
			break;                                                 \
		mtk_dprec_logger_pr(DPREC_LOGGER_DEBUG, fmt, ##arg);           \
		if (g_mobile_log)                                              \
			pr_info("[DISP]" pr_fmt(fmt), ##arg);     \
	} while (0)

#define DDPQOS(fmt, arg...)                                                    \
	do {                                                                   \
		if (!g_qos_log && !g_detail_log)                               \
			break;                                                 \
		mtk_dprec_logger_pr(DPREC_LOGGER_DEBUG, fmt, ##arg);           \
		if (g_mobile_log)                                              \
			pr_info("[DISP]" pr_fmt(fmt), ##arg);                  \
	} while (0)

#define DDPMSG(fmt, arg...)                                                    \
	do {                                                                   \
		mtk_dprec_logger_pr(DPREC_LOGGER_DEBUG, fmt, ##arg);           \
		pr_info("[DISP]" pr_fmt(fmt), ##arg);             \
	} while (0)

#define DDP_EXTEND_MSG(fmt, arg...) \
	do { \
		mtk_dprec_logger_pr(DPREC_LOGGER_DEBUG, fmt, ##arg);           \
		pr_info("[DISP]" pr_fmt(fmt), ##arg);             \
	} while (0)

#define DDPDUMP(fmt, arg...)                                                   \
	do {                                                                   \
		mtk_dprec_logger_pr(DPREC_LOGGER_DUMP, fmt, ##arg);            \
		if (g_mobile_log)                                              \
			pr_info("[DISP]" pr_fmt(fmt), ##arg);     \
	} while (0)

#define DDPFENCE(fmt, arg...)                                                  \
	do {                                                                   \
		mtk_dprec_logger_pr(DPREC_LOGGER_FENCE, fmt, ##arg);           \
		if (g_fence_log)                                               \
			pr_info("[DISP]" pr_fmt(fmt), ##arg);     \
	} while (0)

#define DDPPR_ERR(fmt, arg...)                                                 \
	do {                                                                   \
		mtk_dprec_logger_pr(DPREC_LOGGER_ERROR, fmt, ##arg);           \
		pr_err("[DISP][E]" pr_fmt(fmt), ##arg);              \
	} while (0)

#define DDPIRQ(fmt, arg...)                                                    \
	do {                                                                   \
		if (g_irq_log)                                                 \
			mtk_dprec_logger_pr(DPREC_LOGGER_DEBUG, fmt, ##arg);   \
	} while (0)
#endif

#define DDPFUNC(fmt, arg...)		\
	pr_info("[DISP][%s line:%d]"pr_fmt(fmt), __func__, __LINE__, ##arg)

#define DDP_COMMIT_LOCK(lock, name, line)                                       \
	do {                                                                   \
		DDPINFO("CM_LOCK:%s[%d] +\n", name, line);		   \
		DRM_MMP_EVENT_START(mutex_lock, (unsigned long)lock,	   \
				line);	   \
		mtk_drm_trace_tag_begin("CM_LOCK_%s", name);	\
		mutex_lock(lock);		   \
		mutex_time_start = sched_clock();		   \
		mutex_locker = name;		   \
	} while (0)

#define DDP_COMMIT_UNLOCK(lock, name, line)                                     \
	do {                                                                   \
		mutex_locker = NULL;		   \
		mutex_time_end = sched_clock();		   \
		mutex_time_period = mutex_time_end - mutex_time_start;   \
		if (mutex_time_period > 1000000000) {		   \
			DDPPR_ERR("CM_ULOCK:%s[%d] timeout:<%lld ns>!\n",   \
				name, line, mutex_time_period);		   \
			DRM_MMP_MARK(mutex_lock,		   \
				(unsigned long)mutex_time_period, 0);   \
			dump_stack();		   \
		}		   \
		mutex_unlock(lock);		   \
		DRM_MMP_EVENT_END(mutex_lock, (unsigned long)lock,	   \
			line);	   \
		mtk_drm_trace_tag_end("CM_ULOCK%s", name);	\
		DDPINFO("CM_ULOCK:%s[%d] -\n", name, line);		   \
	} while (0)

#define DDP_MUTEX_LOCK(lock, name, line)                                       \
	do {                                                                   \
		DDPINFO("M_LOCK:%s[%d] +\n", name, line);		   \
		DRM_MMP_EVENT_START(mutex_lock, (unsigned long)lock,	   \
				line);	   \
		mtk_drm_trace_tag_begin("M_LOCK_%s", name);	\
		mutex_lock(lock);		   \
		mtk_vidle_user_power_keep(DISP_VIDLE_USER_CRTC);		\
		mutex_time_start = sched_clock();		   \
		mutex_locker = name;		   \
	} while (0)

#define DDP_MUTEX_UNLOCK(lock, name, line)                                     \
	do {                                                                   \
		mutex_locker = NULL;		   \
		mutex_time_end = sched_clock();		   \
		mutex_time_period = mutex_time_end - mutex_time_start;   \
		if (mutex_time_period > 1000000000) {		   \
			DDPPR_ERR("M_ULOCK:%s[%d] timeout:<%lld ns>!\n",   \
				name, line, mutex_time_period);		   \
			DRM_MMP_MARK(mutex_lock,		   \
				(unsigned long)mutex_time_period, 0);   \
			dump_stack();		   \
		}		   \
		mtk_vidle_user_power_release(DISP_VIDLE_USER_CRTC);			\
		mutex_unlock(lock);		   \
		DRM_MMP_EVENT_END(mutex_lock, (unsigned long)lock,	   \
			line);	   \
		mtk_drm_trace_tag_end("M_LOCK_%s", name);	\
		DDPINFO("M_ULOCK:%s[%d] -\n", name, line);		   \
	} while (0)

#define DDP_MUTEX_LOCK_CONDITION(lock, name, line, con)                                \
	do {                                                                           \
		DDPINFO("M_LOCK:%s[%d] +\n", name, line);                              \
		DRM_MMP_EVENT_START(mutex_lock, (unsigned long)lock, line);            \
		mtk_drm_trace_tag_begin("M_LOCK_%s", name);                            \
		mutex_lock(lock);                                                      \
		if (con)                                                               \
			mtk_vidle_user_power_keep(DISP_VIDLE_USER_CRTC);               \
		mutex_time_start = sched_clock();                                      \
		mutex_locker = name;                                                   \
	} while (0)

#define DDP_MUTEX_UNLOCK_CONDITION(lock, name, line, con)                              \
	do {                                                                           \
		if (con)                                                               \
			mtk_vidle_user_power_release(DISP_VIDLE_USER_CRTC);            \
		mutex_locker = NULL;                                                   \
		mutex_time_end = sched_clock();                                        \
		mutex_time_period = mutex_time_end - mutex_time_start;                 \
		if (unlikely(mutex_time_period > 1000000000)) {                        \
			mutex_unlock(lock);                                            \
			DDPPR_ERR("M_ULOCK:%s[%d] timeout:<%lld ns>!\n",               \
				  name, line, mutex_time_period);                      \
			DRM_MMP_MARK(mutex_lock, (unsigned long)mutex_time_period, 0); \
			dump_stack();                                                  \
		} else                                                                 \
			mutex_unlock(lock);                                            \
		DRM_MMP_EVENT_END(mutex_lock, (unsigned long)lock, line);              \
		mtk_drm_trace_tag_end("M_LOCK_%s", name);                              \
		DDPINFO("M_ULOCK:%s[%d] -\n", name, line);                             \
	} while (0)

#define DDP_MUTEX_LOCK_NESTED(lock, i, name, line)                             \
	do {                                                                   \
		DDPINFO("M_LOCK_NST[%d]:%s[%d] +\n", i, name, line);   \
		mtk_drm_trace_tag_begin("M_LOCK_NST_%s", name);	\
		mutex_lock_nested(lock, i);		   \
		if (i == 0) \
			mtk_vidle_user_power_keep(DISP_VIDLE_USER_CRTC); \
	} while (0)

#define DDP_MUTEX_UNLOCK_NESTED(lock, i, name, line)                           \
	do {                                                                   \
		if (i == 0) \
			mtk_vidle_user_power_release(DISP_VIDLE_USER_CRTC); \
		mutex_unlock(lock);		   \
		mtk_drm_trace_tag_end("M_LOCK_NST_%s", name);	\
		DDPINFO("M_ULOCK_NST[%d]:%s[%d] -\n", i, name, line);	\
	} while (0)

#if IS_ENABLED(CONFIG_MTK_AEE_FEATURE)
#define DDPAEE(string, args...)							\
	do {									\
		char str[200];							\
		int r;	\
		r = snprintf(str, 199, "DDP:" string, ##args);			\
		if (r < 0) {	\
			pr_err("snprintf error\n");	\
		}	\
		aee_kernel_warning_api(__FILE__, __LINE__,			\
					DB_OPT_DEFAULT | DB_OPT_FTRACE |	\
					DB_OPT_MMPROFILE_BUFFER,		\
				       str, string, ##args);			\
		DDPPR_ERR("[DDP Error]" string, ##args);			\
	} while (0)

#define DDPAEE_FATAL(string, args...)						\
	do {									\
		char str[200];							\
		int r;	\
		r = snprintf(str, 199, "DDP:" string, ##args);			\
		if (r < 0) {	\
			pr_err("snprintf error\n");				\
		}	\
		aee_kernel_fatal_api(__FILE__, __LINE__,			\
					DB_OPT_DEFAULT | DB_OPT_FTRACE |	\
					DB_OPT_MMPROFILE_BUFFER,		\
					str, string, ##args);			\
		DDPPR_ERR("[DDP Fatal Error]" string, ##args);			\
	} while (0)
#else /* !CONFIG_MTK_AEE_FEATURE */
#define DDPAEE(string, args...)                                                \
	do {                                                                   \
		char str[200];                                                 \
		int r;	\
		r = snprintf(str, 199, "DDP:" string, ##args);                     \
		if (r < 0) {	\
			pr_err("snprintf error\n");	\
		}	\
		pr_err("[DDP Error]" string, ##args);                          \
	} while (0)

#define DDPAEE_FATAL(string, args...)                                          \
	do {										\
		char str[200];								\
		int r;	\
		r = snprintf(str, 199, "DDP:" string, ##args);				\
		if (r < 0) {	\
			pr_err("snprintf error\n"); \
		}	\
		pr_err("[DDP Fatal Error]" string, ##args);				\
	} while (0)
#endif /* CONFIG_MTK_AEE_FEATURE */

extern bool g_mobile_log;
extern bool g_msync_debug;
extern bool g_fence_log;
extern bool g_irq_log;
extern bool g_detail_log;
extern bool g_gpuc_direct_push;
extern bool g_ovl_bwm_debug;
extern bool g_vidle_apsrc_debug;
extern bool g_profile_log;
extern bool g_qos_log;
extern bool g_y2r_en;
extern unsigned long long g_pf_time;
#endif
