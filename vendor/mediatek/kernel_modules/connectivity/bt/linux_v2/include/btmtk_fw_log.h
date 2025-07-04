/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (c) 2021 MediaTek Inc.
 */

#ifndef __BTMTK_FW_LOG_H__
#define __BTMTK_FW_LOG_H__

#include "btmtk_main.h"
#include "btmtk_chip_reset.h"

#define BT_FWLOG_IOC_MAGIC          (0xfc)
#define BT_FWLOG_IOC_ON_OFF      _IOW(BT_FWLOG_IOC_MAGIC, 0, int)
#define BT_FWLOG_IOC_SET_LEVEL   _IOW(BT_FWLOG_IOC_MAGIC, 1, int)
#define BT_FWLOG_IOC_GET_LEVEL   _IOW(BT_FWLOG_IOC_MAGIC, 2, int)
#define BT_FWLOG_OFF    0x00
#define BT_FWLOG_ON     0xFF

#define DRV_RETURN_SPECIFIC_HCE_ONLY	1	/* Currently disallow 0xFC26 */
#define KPI_WITHOUT_TYPE		0	/* bluetooth kpi */

#ifdef STATIC_REGISTER_FWLOG_NODE
#define FIXED_STPBT_MAJOR_DEV_ID	111
#endif

/* Device node */
#if (USE_DEVICE_NODE == 0)
#if CFG_SUPPORT_MULTI_DEV_NODE
	#define BT_FWLOG_DEV_NODE	"stpbt_multi_fwlog"
#else
	#define BT_FWLOG_DEV_NODE	"stpbtfwlog"
#endif
#else
#if CFG_SUPPORT_MULTI_DEV_NODE
	#define BT_FWLOG_DEV_NODE	"multi_fw_log_bt"
#else
	#define BT_FWLOG_DEV_NODE	"fw_log_bt"
#endif
#endif

#define PROC_ROOT_DIR "stpbt"
#define PROC_BT_FW_VERSION "bt_fw_version"
#define PROC_BT_UART_LAUNCHER_NOTIFY "bt_uart_launcher_notify"
#define PROC_BT_CHIP_RESET_COUNT "bt_chip_reset_count"
#if (USE_DEVICE_NODE == 1)
#define PROC_BT_UART_LAUNCHER_NOTIFY "bt_uart_launcher_notify"
#endif

struct btmtk_fops_fwlog {
	dev_t g_devIDfwlog;
	struct cdev BT_cdevfwlog;
	wait_queue_head_t fw_log_inq;
	struct sk_buff_head fwlog_queue;
	struct sk_buff_head dumplog_queue_first;
	struct sk_buff_head dumplog_queue_latest;
	struct class *pBTClass;
	struct device *pBTDevfwlog;
	spinlock_t fwlog_lock;
	u8 btmtk_bluetooth_kpi;
	struct sk_buff_head usr_opcode_queue;
};

void btmtk_init_node(void);
void btmtk_deinit_node(void);
void fw_log_bt_event_cb(void);
void fw_log_bt_state_cb(uint8_t state);
/** file_operations: stpbtfwlog */
int btmtk_fops_openfwlog(struct inode *inode, struct file *file);
int btmtk_fops_closefwlog(struct inode *inode, struct file *file);
ssize_t btmtk_fops_readfwlog(struct file *filp, char __user *buf, size_t count, loff_t *f_pos);
ssize_t btmtk_fops_writefwlog(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos);
unsigned int btmtk_fops_pollfwlog(struct file *filp, poll_table *wait);
long btmtk_fops_unlocked_ioctlfwlog(struct file *filp, unsigned int cmd, unsigned long arg);
long btmtk_fops_compat_ioctlfwlog(struct file *filp, unsigned int cmd, unsigned long arg);
int btmtk_dispatch_fwlog(struct btmtk_dev *bdev, struct sk_buff *skb);
int btmtk_dispatch_fwlog_bluetooth_kpi(struct btmtk_dev *bdev, u8 *buf, int len, u8 type);
int btmtk_update_bt_status(u8 onoff_flag);

#endif /* __BTMTK_FW_LOG_H__ */
