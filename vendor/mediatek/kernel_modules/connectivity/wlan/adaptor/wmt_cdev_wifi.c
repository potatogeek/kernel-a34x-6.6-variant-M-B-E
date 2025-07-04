/*
* Copyright (C) 2016 MediaTek Inc.
*
* This program is free software: you can redistribute it and/or modify it under the terms of the
* GNU General Public License version 2 as published by the Free Software Foundation.
*
* This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
* without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
* See the GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License along with this program.
* If not, see <http://www.gnu.org/licenses/>.
*/

#include <linux/init.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/sched.h>
#include <asm/current.h>
#include <linux/uaccess.h>
#include <linux/fcntl.h>
#include <linux/poll.h>
#include <linux/time.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <linux/inetdevice.h>
#include <linux/string.h>
#include <linux/version.h>

#if !IS_ENABLED(CFG_SUPPORT_CONNAC1X)
#include "wifi_pwr_on.h"
#else
#include "wmt_exp.h"
#include "stp_exp.h"
#endif
MODULE_LICENSE("Dual BSD/GPL");

#define WIFI_DRIVER_NAME "mtk_wmt_wifi_chrdev"
#define WIFI_DEV_MAJOR 0

#define PFX                         "[MTK-WIFI] "
#define WIFI_LOG_DBG                  3
#define WIFI_LOG_INFO                 2
#define WIFI_LOG_WARN                 1
#define WIFI_LOG_ERR                  0

uint32_t gDbgLevel = WIFI_LOG_DBG;

#define WIFI_DBG_FUNC(fmt, arg...)	\
	do { \
		if (gDbgLevel >= WIFI_LOG_DBG) \
			pr_info(PFX "%s[D]: " fmt, __func__, ##arg); \
	} while (0)
#define WIFI_INFO_FUNC(fmt, arg...)	\
	do { \
		if (gDbgLevel >= WIFI_LOG_INFO) \
			pr_info(PFX "%s[I]: " fmt, __func__, ##arg); \
	} while (0)
#define WIFI_INFO_FUNC_LIMITED(fmt, arg...)	\
	do { \
		if (gDbgLevel >= WIFI_LOG_INFO) \
			pr_info_ratelimited(PFX "%s[L]: " fmt, __func__, ##arg); \
	} while (0)
#define WIFI_WARN_FUNC(fmt, arg...)	\
	do { \
		if (gDbgLevel >= WIFI_LOG_WARN) \
			pr_info(PFX "%s[W]: " fmt, __func__, ##arg); \
	} while (0)
#define WIFI_ERR_FUNC(fmt, arg...)	\
	do { \
		pr_info(PFX "%s[E]: " fmt, __func__, ##arg); \
	} while (0)

#define VERSION "2.0"

#define ISPRINT(a)   (((a) >= 32) && ((a) <= 126))

static int32_t WIFI_devs = 1;
static int32_t WIFI_major = WIFI_DEV_MAJOR;
static dev_t wifi_devno;
module_param(WIFI_major, uint, 0);
static struct cdev WIFI_cdev;
#if CREATE_NODE_DYNAMIC
static struct class *wmtwifi_class;
static struct device *wmtwifi_dev;
#endif

static struct mutex wr_mtx;

#define WLAN_IFACE_NAME "wlan0"

enum {
	WLAN_MODE_HALT,
	WLAN_MODE_AP,
	WLAN_MODE_STA_P2P,
	WLAN_MODE_STA_AP_P2P,
	WLAN_MODE_DUAL_P2P,
	WLAN_MODE_DUAL_AP,
	WLAN_MODE_MAX
};
static int32_t wlan_mode = WLAN_MODE_HALT;
static int32_t powered;
static int32_t isconcurrent;
static char *ifname = WLAN_IFACE_NAME;
static uint32_t driver_loaded;
static int32_t low_latency_mode;
static int32_t wifi_standalone_log_mode;
#if !IS_ENABLED(CFG_SUPPORT_CONNAC1X)
enum {
	WRITE_PROCESSING_DONE,
	WRITE_PROCESSING_START,
	WRITE_PROCESSING_ON,
	WRITE_PROCESSING_OFF,
	WRITE_PROCESSING_MAX
};

static uint8_t  driver_resetting;
static uint8_t  write_processing;
static uint8_t  pre_cal_ongoing;
static uint8_t  whole_chip_rst_ongoing;
#endif
/*******************************************************************
 */
enum ENUM_RESET_STATUS {
	RESET_FAIL,
	RESET_SUCCESS
};

/*
 *  enable = 1, mode = 0  => init P2P network
 *  enable = 1, mode = 1  => init Soft AP network
 *  enable = 0  => uninit P2P/AP network
 */
struct PARAM_CUSTOM_P2P_SET_STRUCT {
	uint32_t u4Enable;
	uint32_t u4Mode;
};
typedef int32_t(*set_p2p_mode) (struct net_device *netdev, struct PARAM_CUSTOM_P2P_SET_STRUCT p2pmode);

static set_p2p_mode pf_set_p2p_mode;
void register_set_p2p_mode_handler(set_p2p_mode handler)
{
	WIFI_INFO_FUNC("(pid %d) register set p2p mode handler %p\n", current->pid, handler);
	pf_set_p2p_mode = handler;
}
EXPORT_SYMBOL(register_set_p2p_mode_handler);

static void (*pf_set_wifi_test_mode_fwdl)(const int);
void register_set_wifi_test_mode_fwdl_handler(void (*handler)(const int))
{
	pf_set_wifi_test_mode_fwdl = handler;
}
EXPORT_SYMBOL(register_set_wifi_test_mode_fwdl_handler);

typedef uint8_t (*is_wifi_in_test_mode) (struct net_device *netdev);
static is_wifi_in_test_mode pf_is_wifi_in_test_mode;
void register_is_wifi_in_test_mode_handler(is_wifi_in_test_mode handler)
{
	pf_is_wifi_in_test_mode = handler;
}
EXPORT_SYMBOL(register_is_wifi_in_test_mode_handler);

void update_driver_loaded_status(uint8_t loaded)
{
	WIFI_INFO_FUNC("update_driver_loaded_status: %d\n", loaded);
	driver_loaded = loaded;
}
EXPORT_SYMBOL(update_driver_loaded_status);

static int atoh(const char *str, uint32_t *hval)
{
	unsigned int i;
	uint32_t val = 0;

	WIFI_INFO_FUNC("*str : %s, len = %zu\n", str,
			strlen((const char *)str));
	for (i = 0; i < strlen((const char *)str); i++) {
		if (str[i] >= 'a' && str[i] <= 'f')
			val = (val << 4) + (str[i] - 'a' + 10);
		else if (str[i] >= 'A' && str[i] <= 'F')
			val = (val << 4) + (str[i] - 'A' + 10);
		else if (*(str + i) >= '0' && *(str + i) <= '9')
			val = (val << 4) + (*(str + i) - '0');
	}

	*hval = val;

	return 0;
}

void set_low_latency_mode(const char *mode)
{
	atoh(mode, &low_latency_mode);
	WIFI_INFO_FUNC("LLM : %d\n", low_latency_mode);
}

uint32_t get_low_latency_mode(void)
{
	WIFI_INFO_FUNC("LLM : %d\n", low_latency_mode);
	return low_latency_mode;
}
EXPORT_SYMBOL(get_low_latency_mode);

void set_wifi_standalone_log_mode(const int mode)
{
	wifi_standalone_log_mode = mode;
}

uint32_t get_wifi_standalone_log_mode(void)
{
	return wifi_standalone_log_mode;
}
EXPORT_SYMBOL(get_wifi_standalone_log_mode);

#if !IS_ENABLED(CFG_SUPPORT_CONNAC1X)
void update_driver_reset_status(uint8_t fgIsResetting)
{
	WIFI_INFO_FUNC("update_driver_reset_status: %d\n", fgIsResetting);
	driver_resetting = fgIsResetting;
}
EXPORT_SYMBOL(update_driver_reset_status);
int32_t get_wifi_powered_status(void)
{
	WIFI_INFO_FUNC("wifi power status : %d\n", powered);
	return powered;
}
EXPORT_SYMBOL(get_wifi_powered_status);
int32_t get_wifi_process_status(void)
{
	WIFI_INFO_FUNC("wifi write status: %d\n", write_processing);
	return write_processing;
}
EXPORT_SYMBOL(get_wifi_process_status);
u_int8_t is_wifi_process_idle(void)
{
	return get_wifi_process_status() == WRITE_PROCESSING_DONE;
}
EXPORT_SYMBOL(is_wifi_process_idle);
u_int8_t is_wifi_process_off_ongoing(void)
{
	return get_wifi_process_status() == WRITE_PROCESSING_OFF;
}
EXPORT_SYMBOL(is_wifi_process_off_ongoing);
void update_pre_cal_status(uint8_t fgIsPreCal)
{
	WIFI_INFO_FUNC("update_pre_cal_status: %d\n", fgIsPreCal);
	pre_cal_ongoing = fgIsPreCal;
}
EXPORT_SYMBOL(update_pre_cal_status);
uint8_t get_pre_cal_status(void)
{
	WIFI_INFO_FUNC("pre cal status: %d\n", pre_cal_ongoing);
	return pre_cal_ongoing;
}
EXPORT_SYMBOL(get_pre_cal_status);
void update_whole_chip_rst_status(uint8_t fgIsWholeChipRst)
{
	WIFI_INFO_FUNC("update_whole_chip_rst_status: %d\n", fgIsWholeChipRst);
	whole_chip_rst_ongoing = fgIsWholeChipRst;
}
EXPORT_SYMBOL(update_whole_chip_rst_status);
uint8_t get_whole_chip_rst_status(void)
{
	WIFI_INFO_FUNC("whole_chip_rst status: %d\n", whole_chip_rst_ongoing);
	return whole_chip_rst_ongoing;
}
EXPORT_SYMBOL(get_whole_chip_rst_status);
#endif

int32_t update_wr_mtx_down_up_status(uint8_t ucDownUp, uint8_t ucIsBlocking)
{
	if (ucDownUp == 0) {
		WIFI_INFO_FUNC("Try to lock wr_mtx\n");
		if (ucIsBlocking == 1)
			mutex_lock(&wr_mtx);
		else if (ucIsBlocking == 0)
			return mutex_trylock(&wr_mtx);
	} else if (ucDownUp == 1) {
		mutex_unlock(&wr_mtx);
		WIFI_INFO_FUNC("Unlock wr_mtx\n");
	}

	return 0;
}
EXPORT_SYMBOL(update_wr_mtx_down_up_status);

enum ENUM_WLAN_DRV_BUF_TYPE_T {
	BUF_TYPE_NVRAM,
	BUF_TYPE_DRV_CFG,
	BUF_TYPE_FW_CFG,
	BUF_TYPE_XONV,
	BUF_TYPE_NUM
};

typedef uint8_t(*file_buf_handler)(void *ctx, const char __user *buf, uint16_t length);
static file_buf_handler buf_handler[BUF_TYPE_NUM];
static void *buf_handler_ctx[BUF_TYPE_NUM];
void register_file_buf_handler(file_buf_handler handler, void *handler_ctx, uint8_t ucType)
{
	if (ucType < BUF_TYPE_NUM) {
		buf_handler[ucType] = handler;
		buf_handler_ctx[ucType] = handler_ctx;
	}
}
EXPORT_SYMBOL(register_file_buf_handler);

/*******************************************************************
 *  WHOLE CHIP RESET PROCEDURE:
 *
 *  WMTRSTMSG_RESET_START callback
 *  -> wlanRemove
 *  -> WMTRSTMSG_RESET_END callback
 *
 *******************************************************************
*/
/*-----------------------------------------------------------------*/
/*
 *  Receiving RESET_START message
 */
/*-----------------------------------------------------------------*/
int32_t wifi_reset_start(void)
{
	struct net_device *netdev = NULL;
	struct PARAM_CUSTOM_P2P_SET_STRUCT p2pmode;

	mutex_lock(&wr_mtx);

	if (powered == 1) {
		netdev = dev_get_by_name(&init_net, ifname);
		if (netdev == NULL) {
			WIFI_ERR_FUNC("Fail to get %s net device\n", ifname);
		} else {
			p2pmode.u4Enable = 0;
			p2pmode.u4Mode = 0;

			if (pf_set_p2p_mode) {
				if (pf_set_p2p_mode(netdev, p2pmode) != 0)
					WIFI_ERR_FUNC("Turn off p2p/ap mode fail");
				else
					WIFI_INFO_FUNC("Turn off p2p/ap mode");
			}
			dev_put(netdev);
			netdev = NULL;
		}
	} else {
		/* WIFI is off before whole chip reset, do nothing */
	}

	return 0;
}
EXPORT_SYMBOL(wifi_reset_start);

/*-----------------------------------------------------------------*/
/*
 *  Receiving RESET_END/RESET_END_FAIL message
 */
/*-----------------------------------------------------------------*/
int32_t wifi_reset_end(enum ENUM_RESET_STATUS status)
{
	struct net_device *netdev = NULL;
	struct PARAM_CUSTOM_P2P_SET_STRUCT p2pmode;
	int32_t wait_cnt = 0;
	int32_t ret = -1;

	if (status == RESET_FAIL) {
		/* whole chip reset fail, donot recover WIFI */
		ret = 0;
		if (mutex_is_locked(&wr_mtx))
			mutex_unlock(&wr_mtx);
	} else if (status == RESET_SUCCESS) {
		WIFI_WARN_FUNC("WIFI state recovering...\n");

		if (powered == 1) {
			/* WIFI is on before whole chip reset, reopen it now */
#if !IS_ENABLED(CFG_SUPPORT_CONNAC1X)
			/*
			 * mtk_wland_thread_main will check this flag for current state.
			 * if this flag is TRUE, mtk_wland_thread_main will not do power on again.
			 * Set this flag to FALSE to finish the reset procedure
			 */
			g_fgIsWiFiOn = MTK_WCN_BOOL_FALSE;
			if (mtk_wcn_wlan_func_ctrl(WLAN_OPID_FUNC_ON) == MTK_WCN_BOOL_FALSE) {
#else
			if (mtk_wcn_wmt_func_on(WMTDRV_TYPE_WIFI) == MTK_WCN_BOOL_FALSE) {
#endif
				WIFI_ERR_FUNC("WMT turn on WIFI fail!\n");
				powered = 0;
				goto done;
			} else {
				WIFI_INFO_FUNC("WMT turn on WIFI success!\n");
			}

			if (pf_set_p2p_mode == NULL) {
				WIFI_ERR_FUNC("Set p2p mode handler is NULL\n");
				goto done;
			}

			netdev = dev_get_by_name(&init_net, ifname);
			wait_cnt = 0;
			while (netdev == NULL && wait_cnt < 10) {
				WIFI_ERR_FUNC("Fail to get %s net device, sleep 300ms\n", ifname);
				msleep(300);
				wait_cnt++;
				netdev = dev_get_by_name(&init_net, ifname);
			}
			if (wait_cnt >= 10) {
				WIFI_ERR_FUNC("Get %s net device timeout\n", ifname);
				goto done;
			}

			if (wlan_mode == WLAN_MODE_STA_P2P) {
				p2pmode.u4Enable = 1;
				p2pmode.u4Mode = 0;
				if (pf_set_p2p_mode(netdev, p2pmode) != 0) {
					WIFI_ERR_FUNC("Set wlan mode 0 fail\n");
				} else {
					WIFI_WARN_FUNC("Set wlan mode %d\n", WLAN_MODE_STA_P2P);
					ret = 0;
				}
			} else if (wlan_mode == WLAN_MODE_AP) {
				p2pmode.u4Enable = 1;
				p2pmode.u4Mode = 1;
				if (pf_set_p2p_mode(netdev, p2pmode) != 0) {
					WIFI_ERR_FUNC("Set wlan mode 1 fail\n");
				} else {
					WIFI_WARN_FUNC("Set wlan mode %d\n", WLAN_MODE_AP);
					ret = 0;
				}
			} else
				ret = 0;
done:
			if (netdev != NULL)
				dev_put(netdev);
		} else {
			/* WIFI is off before whole chip reset, do nothing */
			ret = 0;
		}
		if (mutex_is_locked(&wr_mtx))
			mutex_unlock(&wr_mtx);
	}

	return ret;
}
EXPORT_SYMBOL(wifi_reset_end);

static int WIFI_open(struct inode *inode, struct file *file)
{
	WIFI_INFO_FUNC("major %d minor %d (pid %d)\n", imajor(inode), iminor(inode), current->pid);

	return 0;
}

static int WIFI_close(struct inode *inode, struct file *file)
{
	WIFI_INFO_FUNC("major %d minor %d (pid %d)\n", imajor(inode), iminor(inode), current->pid);

	return 0;
}

static bool write_value_sanity_check(int8_t *local, size_t count)
{
	if ((local[0] == '0' || local[0] == '1' || local[0] == '2') &&
	    (count > 2 || (local[1] != '\0' && local[1] != '\n')))
		return false;
	return true;
}

static void WIFI_write_off(int32_t *retval, struct net_device *netdev, size_t count)
{
	struct PARAM_CUSTOM_P2P_SET_STRUCT p2pmode;

#if !IS_ENABLED(CFG_SUPPORT_CONNAC1X)
	write_processing = WRITE_PROCESSING_OFF;
#endif

	if (powered == 0) {
		WIFI_INFO_FUNC("WIFI is already power off!\n");
		*retval = count;
		wlan_mode = WLAN_MODE_HALT;
		/*goto done;*/
		return;
	}

	netdev = dev_get_by_name(&init_net, ifname);
	if (netdev == NULL)
		WIFI_ERR_FUNC("Fail to get %s net device\n", ifname);
	else {
		p2pmode.u4Enable = 0;
		p2pmode.u4Mode = 0;

		if (pf_set_p2p_mode) {
			if (pf_set_p2p_mode(netdev, p2pmode) != 0)
				WIFI_ERR_FUNC("Turn off p2p/ap mode fail");
			else {
				WIFI_INFO_FUNC("Turn off p2p/ap mode");
				wlan_mode = WLAN_MODE_HALT;
			}
		}
		dev_put(netdev);
		netdev = NULL;
	}

#if !IS_ENABLED(CFG_SUPPORT_CONNAC1X)
	if (mtk_wcn_wlan_func_ctrl(WLAN_OPID_FUNC_OFF) == MTK_WCN_BOOL_FALSE) {
#else
	if (mtk_wcn_wmt_func_off(WMTDRV_TYPE_WIFI) == MTK_WCN_BOOL_FALSE) {
#endif
		WIFI_ERR_FUNC("WMT turn off WIFI fail!\n");
	} else {
		WIFI_INFO_FUNC("WMT turn off WIFI success!\n");
		*retval = count;
		wlan_mode = WLAN_MODE_HALT;
	}
	powered = 0;
}

static void WIFI_write_on(int32_t *retval, size_t count)
{
#if !IS_ENABLED(CFG_SUPPORT_CONNAC1X)
	write_processing = WRITE_PROCESSING_ON;
#endif
	if (powered == 1) {
		WIFI_INFO_FUNC("WIFI is already power on!\n");
		*retval = count;
		return;
	}
#if !IS_ENABLED(CFG_SUPPORT_CONNAC1X)
	if (mtk_wcn_wlan_func_ctrl(WLAN_OPID_FUNC_ON) == MTK_WCN_BOOL_FALSE) {
#else
	if (mtk_wcn_wmt_func_on(WMTDRV_TYPE_WIFI) == MTK_WCN_BOOL_FALSE) {
#endif
		WIFI_ERR_FUNC("WMT turn on WIFI fail!\n");
	} else {
		powered = 1;
		*retval = count;
		WIFI_INFO_FUNC("WMT turn on WIFI success!\n");
		wlan_mode = WLAN_MODE_HALT;
	}
}

static void WIFI_write_test_mode_on(int32_t *retval, struct net_device *netdev, size_t count)
{
	struct PARAM_CUSTOM_P2P_SET_STRUCT p2pmode;

	/* wifi off -> wifi on by test mode */
#if !IS_ENABLED(CFG_SUPPORT_CONNAC1X)
	write_processing = WRITE_PROCESSING_ON;
#endif

	if (pf_set_wifi_test_mode_fwdl == NULL) {
		WIFI_INFO_FUNC("Error set_wifi_test_mode_fwdl is not register\n");
		return;
	}

	if (powered == 1) {
		/* below is same as wifi write 0, may combine to one function. */
		netdev = dev_get_by_name(&init_net, ifname);
		if (netdev == NULL)
			WIFI_ERR_FUNC("Fail to get %s net device\n", ifname);
		else {
			if (pf_is_wifi_in_test_mode &&
				pf_is_wifi_in_test_mode(netdev) == true) {
				WIFI_INFO_FUNC("Skip: already in test mode\n");
				return;
			}
			WIFI_INFO_FUNC("do wifi off for test mode on!\n");

			p2pmode.u4Enable = 0;
			p2pmode.u4Mode = 0;

			if (pf_set_p2p_mode) {
				if (pf_set_p2p_mode(netdev, p2pmode) != 0)
					WIFI_ERR_FUNC("Turn off p2p/ap mode fail");
				else {
					WIFI_INFO_FUNC("Turn off p2p/ap mode");
					wlan_mode = WLAN_MODE_HALT;
				}
			}
			dev_put(netdev);
			netdev = NULL;
		}

#if !IS_ENABLED(CFG_SUPPORT_CONNAC1X)
		if (mtk_wcn_wlan_func_ctrl(WLAN_OPID_FUNC_OFF) == MTK_WCN_BOOL_FALSE) {
#else
		if (mtk_wcn_wmt_func_off(WMTDRV_TYPE_WIFI) == MTK_WCN_BOOL_FALSE) {
#endif
			WIFI_ERR_FUNC("WMT turn off WIFI fail!\n");
		} else {
			WIFI_INFO_FUNC("WMT turn off WIFI success!\n");
			powered = 0;
			*retval = count;
			wlan_mode = WLAN_MODE_HALT;
		}

		/* wifi write 0 action done */
	} else
		WIFI_INFO_FUNC("WIFI is already power off, skip off action.\n");

	WIFI_INFO_FUNC("Test Mode WIFI On\n");
	pf_set_wifi_test_mode_fwdl(1);

	/* below is same as wifi write 1, may combine to one function. */
#if !IS_ENABLED(CFG_SUPPORT_CONNAC1X)
	if (mtk_wcn_wlan_func_ctrl(WLAN_OPID_FUNC_ON) == MTK_WCN_BOOL_FALSE) {
#else
	if (mtk_wcn_wmt_func_on(WMTDRV_TYPE_WIFI) == MTK_WCN_BOOL_FALSE) {
#endif
		WIFI_ERR_FUNC("WMT turn on WIFI fail!\n");
	} else {
		powered = 1;
		*retval = count;
		WIFI_INFO_FUNC("WMT turn on WIFI success!\n");
		wlan_mode = WLAN_MODE_HALT;
	}
	pf_set_wifi_test_mode_fwdl(0);
}

static int32_t wifi_conv_to_printable_str(uint32_t size, int8_t *src, int8_t *des)
{
	int32_t i = 0;
	uint32_t write_byte = 0;

	if (size == 0) {
		/* sanity check */
		return 0;
	}

	for (i = 0; i < size; i++) {

		if ((i == (size - 1)) && src[i] == '\0') {
			/* null terminate */
			break;
		}

		if (ISPRINT(src[i])) {
			/* Print directly */
			write_byte += sprintf((char *)(des + write_byte), "%c", src[i]);
		} else {
			/* Transfer to HEX */
			write_byte += sprintf((char *)(des + write_byte), " %02X", (uint8_t)src[i]);
		}
	}

	return write_byte;
}

ssize_t WIFI_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
	int32_t retval = -EIO;
	int8_t local[20] = { 0 };
	struct net_device *netdev = NULL;
	struct PARAM_CUSTOM_P2P_SET_STRUCT p2pmode;
	int32_t wait_cnt = 0;
	uint32_t copy_size = 0;
	int8_t print_str[(sizeof(local) - 1) * 3 + 1] = { 0 };
	uint32_t total_len = 0;

	mutex_lock(&wr_mtx);
	if (count <= 0) {
		WIFI_ERR_FUNC("WIFI_write invalid param\n");
		goto done;
	}
#if !IS_ENABLED(CFG_SUPPORT_CONNAC1X)
	write_processing = WRITE_PROCESSING_START;
	if (driver_resetting == 1 ||
	    whole_chip_rst_ongoing) {
		WIFI_ERR_FUNC("Wi-Fi is resetting\n");
		goto done;
	}
#endif
	copy_size = (sizeof(local) - 1) < (uint32_t) count ?
		(sizeof(local) - 1) : (uint32_t) count;
	if (copy_from_user(local, buf, copy_size) == 0) {
		local[copy_size] = '\0';

		total_len = wifi_conv_to_printable_str(copy_size, local, print_str);
		print_str[total_len] = '\0';

		WIFI_INFO_FUNC("WIFI_write %s, length %zu, copy_size %u\n",
			print_str, count, copy_size);

		if (!write_value_sanity_check(local, count))
			goto done;

		if (local[0] == '0') {
			WIFI_write_off(&retval, netdev, count);
		} else if (local[0] == '1') {
			WIFI_write_on(&retval, count);
		} else if (local[0] == '2') {
			WIFI_write_test_mode_on(&retval, netdev, count);
		} else if (!strncmp(local, "WR-BUF:", 7)) {
			file_buf_handler handler = NULL;
			void *ctx = NULL;

			if (!strncmp(&local[7], "NVRAM", 5)) {
				copy_size = count - 12;
				buf += 12;
				wait_cnt = 0;
				while (wait_cnt < 2000) {
					handler = buf_handler[BUF_TYPE_NVRAM];
					ctx = buf_handler_ctx[BUF_TYPE_NVRAM];
					if (handler)
						break;
					if (wait_cnt % 20 == 0)
						WIFI_ERR_FUNC("Wi-Fi driver is not ready for 2s\n");
					msleep(100);
					wait_cnt++;
				}

				if (!handler) {
					WIFI_ERR_FUNC("Wi-Fi driver is not ready for write NVRAM\n");
				} else
					WIFI_INFO_FUNC("Wi-Fi handler = %p\n", handler);
			} else if (!strncmp(&local[7], "DRVCFG", 6)) {
				copy_size = count - 13;
				buf += 13;
				handler = buf_handler[BUF_TYPE_DRV_CFG];
				ctx = buf_handler_ctx[BUF_TYPE_DRV_CFG];
			} else if (!strncmp(&local[7], "FWCFG", 5)) {
				copy_size = count - 12;
				buf += 12;
				handler = buf_handler[BUF_TYPE_FW_CFG];
				ctx = buf_handler_ctx[BUF_TYPE_FW_CFG];
			} else if (!strncmp(&local[7], "XONV", 4)) {
				copy_size = count - 11;
				buf += 11;

				wait_cnt = 0;
				while (wait_cnt < 2000) {
					handler = buf_handler[BUF_TYPE_XONV];
					ctx = buf_handler_ctx[BUF_TYPE_XONV];
					if (handler)
						break;
					if (wait_cnt % 20 == 0)
						WIFI_ERR_FUNC("Wi-Fi driver is not ready for 2s\n");
					msleep(100);
					wait_cnt++;
				}

				if (!handler)
					WIFI_ERR_FUNC("Wi-Fi driver is not ready for write XO NVRAM\n");
				else
					WIFI_INFO_FUNC("Wi-Fi XO NVRAM handler = %p\n", handler);
			}
			if (handler && !handler(ctx, buf, (uint16_t)copy_size))
				retval = count;
			else
				retval = -ENOTSUPP;
		} else if (!strncmp(local, "RM-BUF:", 7)) {
			file_buf_handler handler = NULL;
			void *ctx = NULL;

			if (!strncmp(&local[7], "DRVCFG", 6)) {
				handler = buf_handler[BUF_TYPE_DRV_CFG];
				ctx = buf_handler_ctx[BUF_TYPE_DRV_CFG];
			} else if (!strncmp(&local[7], "FWCFG", 5)) {
				handler = buf_handler[BUF_TYPE_FW_CFG];
				ctx = buf_handler_ctx[BUF_TYPE_FW_CFG];
			}

			if (handler && !handler(ctx, NULL, 0))
				retval = count;
			else
				retval = -ENOTSUPP;
		} else if (local[0] == 'S' || local[0] == 'P' || local[0] == 'A') {
			if (powered == 1 && driver_loaded == 0) {
				WIFI_INFO_FUNC("In fact wifi is already turned off! reset related states.\n");
				powered = 0;
				wlan_mode = WLAN_MODE_HALT;
			}

			if (powered == 0) {
				/* If WIFI is off, turn on WIFI first */
#if !IS_ENABLED(CFG_SUPPORT_CONNAC1X)
				write_processing = WRITE_PROCESSING_ON;
				if (mtk_wcn_wlan_func_ctrl(WLAN_OPID_FUNC_ON) == MTK_WCN_BOOL_FALSE) {
#else
				if (mtk_wcn_wmt_func_on(WMTDRV_TYPE_WIFI) == MTK_WCN_BOOL_FALSE) {
#endif
					WIFI_ERR_FUNC("WMT turn on WIFI fail!\n");
					goto done;
				} else {
					powered = 1;
					WIFI_INFO_FUNC("WMT turn on WIFI success!\n");
					wlan_mode = WLAN_MODE_HALT;
				}
			}

			if (pf_set_p2p_mode == NULL) {
				WIFI_ERR_FUNC("Set p2p mode handler is NULL\n");
				goto done;
			}

			netdev = dev_get_by_name(&init_net, ifname);
			wait_cnt = 0;
			while (netdev == NULL && wait_cnt < 10) {
				WIFI_ERR_FUNC("Fail to get %s net device, sleep 300ms\n", ifname);
				msleep(300);
				wait_cnt++;
				netdev = dev_get_by_name(&init_net, ifname);
			}
			if (wait_cnt >= 10) {
				WIFI_ERR_FUNC("Get %s net device timeout\n", ifname);
				goto done;
			}

			/* 1. Concurrent mode */
			if (isconcurrent) {
				if (wlan_mode == WLAN_MODE_STA_AP_P2P) {
					WIFI_INFO_FUNC("WIFI is already in cocurrent mode %d!\n", wlan_mode);
					retval = count;
					goto done;
				}
				p2pmode.u4Enable = 1;
				p2pmode.u4Mode = 3;
				if (pf_set_p2p_mode(netdev, p2pmode) != 0) {
					WIFI_ERR_FUNC("Set wlan mode fail\n");
					/* Goto Non-concurrent mode */
				} else {
					WIFI_INFO_FUNC("Set wlan mode %d --> %d\n", wlan_mode, WLAN_MODE_STA_AP_P2P);
					wlan_mode = WLAN_MODE_STA_AP_P2P;
					retval = count;
					goto done;
				}
			}

			/* 2. Non-concurrent mode */
			if ((wlan_mode == WLAN_MODE_STA_P2P && (local[0] == 'S' || local[0] == 'P')) ||
			    (wlan_mode == WLAN_MODE_AP && (local[0] == 'A'))) {
				WIFI_INFO_FUNC("WIFI is already in mode %d!\n", wlan_mode);
				retval = count;
				goto done;
			}

			if ((wlan_mode == WLAN_MODE_AP && (local[0] == 'S' || local[0] == 'P')) ||
			    (wlan_mode == WLAN_MODE_STA_P2P && (local[0] == 'A'))) {
				p2pmode.u4Enable = 0;
				p2pmode.u4Mode = 0;
				if (pf_set_p2p_mode(netdev, p2pmode) != 0) {
					WIFI_ERR_FUNC("Turn off p2p/ap mode fail");
					goto done;
				}
			}

			if (local[0] == 'S' || local[0] == 'P') {
				p2pmode.u4Enable = 1;
				p2pmode.u4Mode = 0;
				if (pf_set_p2p_mode(netdev, p2pmode) != 0) {
					WIFI_ERR_FUNC("Set wlan mode fail\n");
				} else {
					WIFI_INFO_FUNC("Set wlan mode %d --> %d\n", wlan_mode, WLAN_MODE_STA_P2P);
					wlan_mode = WLAN_MODE_STA_P2P;
					retval = count;
				}
			} else if (local[0] == 'A') {
				p2pmode.u4Enable = 1;
				p2pmode.u4Mode = 1;
				if (pf_set_p2p_mode(netdev, p2pmode) != 0) {
					WIFI_ERR_FUNC("Set wlan mode fail\n");
				} else {
					WIFI_INFO_FUNC("Set wlan mode %d --> %d\n", wlan_mode, WLAN_MODE_AP);
					wlan_mode = WLAN_MODE_AP;
					retval = count;
				}
			}
			dev_put(netdev);
			netdev = NULL;
		} else if (local[0] == 'C') {
			if ((isconcurrent == 0) &&
				((wlan_mode == WLAN_MODE_STA_P2P)
				|| (wlan_mode == WLAN_MODE_AP))) {
				netdev = dev_get_by_name(&init_net, ifname);
				if (netdev && pf_set_p2p_mode) {
					p2pmode.u4Enable = 0;
					p2pmode.u4Mode = 0;
					if (pf_set_p2p_mode(netdev, p2pmode) != 0)
						WIFI_ERR_FUNC("Turn off p2p/ap mode fail");
					else
						WIFI_INFO_FUNC("Turn off p2p/ap mode success");
				} else
					WIFI_ERR_FUNC("Fail to get %s netdev\n", ifname);
			}
			isconcurrent = 1;
			WIFI_INFO_FUNC("Enable concurrent mode\n");
			retval = count;
		} else if (local[0] == 'N') {
			if ((isconcurrent == 1) &&
				(wlan_mode == WLAN_MODE_STA_AP_P2P)) {
				netdev = dev_get_by_name(&init_net, ifname);
				if (netdev && pf_set_p2p_mode) {
					p2pmode.u4Enable = 0;
					p2pmode.u4Mode = 0;
					if (pf_set_p2p_mode(netdev, p2pmode) != 0)
						WIFI_ERR_FUNC("Turn off p2p/ap mode fail");
					else
						WIFI_INFO_FUNC("Turn off p2p/ap mode success");
				} else
					WIFI_ERR_FUNC("Fail to get %s netdev\n", ifname);
			}
			isconcurrent = 0;
			WIFI_INFO_FUNC("Disable concurrent mode\n");
			retval = count;
		} else if (local[0] == 'D') {
			if (wlan_mode == WLAN_MODE_DUAL_P2P) {
				WIFI_INFO_FUNC("WIFI is already in dual p2p mode %d!\n", wlan_mode);
			} else {
				netdev = dev_get_by_name(&init_net, ifname);
				if (netdev && pf_set_p2p_mode) {
					p2pmode.u4Enable = 0;
					p2pmode.u4Mode = 0;
					if (pf_set_p2p_mode(netdev, p2pmode) != 0)
						WIFI_ERR_FUNC("Turn off p2p/ap mode fail");
					else
						WIFI_INFO_FUNC("Turn off p2p/ap mode success");
				} else
					WIFI_ERR_FUNC("Fail to get %s netdev\n", ifname);

				if (netdev && pf_set_p2p_mode) {
					p2pmode.u4Enable = 1;
					p2pmode.u4Mode = 4;
					if (pf_set_p2p_mode(netdev, p2pmode) != 0) {
						WIFI_ERR_FUNC("Set wlan mode fail\n");
					} else {
						WIFI_INFO_FUNC("Set wlan mode %d --> %d\n",
							wlan_mode, WLAN_MODE_DUAL_P2P);
						wlan_mode = WLAN_MODE_DUAL_P2P;
					}
				}
			}
			retval = count;
		} else if (local[0] == 'E') {
			if (wlan_mode == WLAN_MODE_DUAL_AP) {
				WIFI_INFO_FUNC("WIFI is already in dual ap mode %d!\n", wlan_mode);
			} else {
				netdev = dev_get_by_name(&init_net, ifname);
				if (netdev && pf_set_p2p_mode) {
					p2pmode.u4Enable = 0;
					p2pmode.u4Mode = 0;
					if (pf_set_p2p_mode(netdev, p2pmode) != 0)
						WIFI_ERR_FUNC("Turn off p2p/ap mode fail");
					else
						WIFI_INFO_FUNC("Turn off p2p/ap mode success");
				} else
					WIFI_ERR_FUNC("Fail to get %s netdev\n", ifname);

				if (netdev && pf_set_p2p_mode) {
					p2pmode.u4Enable = 1;
					p2pmode.u4Mode = 2;
					if (pf_set_p2p_mode(netdev, p2pmode) != 0) {
						WIFI_ERR_FUNC("Set wlan mode fail\n");
					} else {
						WIFI_INFO_FUNC("Set wlan mode %d --> %d\n",
							wlan_mode, WLAN_MODE_DUAL_AP);
						wlan_mode = WLAN_MODE_DUAL_AP;
					}
				}
			}
			retval = count;
		} else if (!strncmp(local, "LLM", 3)) {
			WIFI_INFO_FUNC("local = %s", local);
			if (!strncmp(local + 4, "0x", 2)) {
				WIFI_INFO_FUNC("LLM val = %s", local + 6);
				set_low_latency_mode(local + 6);
				retval = count;
			} else {
				retval = -ENOTSUPP;
			}
		} else if (!strncmp(local, "wifiSLog", 8)) {
			if (!strncmp(local + 9, "1", 1)) {
				WIFI_INFO_FUNC("local = %s, wifiSLog val = %s", local, local + 9);
				set_wifi_standalone_log_mode(1);
				retval = count;
			} else if (!strncmp(local + 9, "0", 1)) {
				WIFI_INFO_FUNC("local = %s, wifiSLog val = %s", local, local + 9);
				set_wifi_standalone_log_mode(0);
				retval = count;
			} else {
				retval = -ENOTSUPP;
			}
		}
	}
done:
	if (netdev != NULL)
		dev_put(netdev);
#if !IS_ENABLED(CFG_SUPPORT_CONNAC1X)
	write_processing = WRITE_PROCESSING_DONE;
#endif
	mutex_unlock(&wr_mtx);
	return retval;
}

const struct file_operations WIFI_fops = {
	.open = WIFI_open,
	.release = WIFI_close,
	.write = WIFI_write,
};

static int WIFI_init(void)
{
	int32_t alloc_ret = 0;
	int32_t cdev_err = 0;

	low_latency_mode = 0;
	wifi_standalone_log_mode = 0;

	mutex_init(&wr_mtx);

#if !IS_ENABLED(CFG_SUPPORT_CONNAC1X)
	wifi_pwr_on_init();
#endif

	/* Allocate char device */
	if (WIFI_major) {
		wifi_devno = MKDEV(WIFI_major, 0);
		alloc_ret = register_chrdev_region(wifi_devno, WIFI_devs,
				WIFI_DRIVER_NAME);
	} else {
		alloc_ret = alloc_chrdev_region(&wifi_devno, 0, WIFI_devs,
				WIFI_DRIVER_NAME);
	}
	if (alloc_ret) {
		WIFI_ERR_FUNC("Fail to register device numbers\n");
		return alloc_ret;
	}

	cdev_init(&WIFI_cdev, &WIFI_fops);
	WIFI_cdev.owner = THIS_MODULE;

	cdev_err = cdev_add(&WIFI_cdev, wifi_devno, WIFI_devs);
	if (cdev_err)
		goto error;

#if CREATE_NODE_DYNAMIC	/* mknod replace */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0))
	wmtwifi_class = class_create("wmtWifi");
#else
	wmtwifi_class = class_create(THIS_MODULE, "wmtWifi");
#endif
	if (IS_ERR(wmtwifi_class))
		goto error;
	wmtwifi_dev = device_create(wmtwifi_class, NULL, wifi_devno, NULL,
			"wmtWifi");
	if (IS_ERR(wmtwifi_dev))
		goto error;
#endif

	WIFI_INFO_FUNC("%s driver(major %d %d) installed.\n", WIFI_DRIVER_NAME,
			WIFI_major, MAJOR(wifi_devno));

	return 0;

error:
#if CREATE_NODE_DYNAMIC
	if (wmtwifi_dev && !IS_ERR(wmtwifi_dev)) {
		device_destroy(wmtwifi_class, wifi_devno);
		wmtwifi_dev = NULL;
	}
	if (wmtwifi_class && !IS_ERR(wmtwifi_class)) {
		class_destroy(wmtwifi_class);
		wmtwifi_class = NULL;
	}
#endif
	if (cdev_err == 0)
		cdev_del(&WIFI_cdev);

	if (alloc_ret == 0)
		unregister_chrdev_region(wifi_devno, WIFI_devs);

	return -1;
}

static void WIFI_exit(void)
{
#if CREATE_NODE_DYNAMIC
	if (wmtwifi_dev && !IS_ERR(wmtwifi_dev)) {
		device_destroy(wmtwifi_class, wifi_devno);
		wmtwifi_dev = NULL;
	}
	if (wmtwifi_class && !IS_ERR(wmtwifi_class)) {
		class_destroy(wmtwifi_class);
		wmtwifi_class = NULL;
	}
#endif

	cdev_del(&WIFI_cdev);
	unregister_chrdev_region(wifi_devno, WIFI_devs);

	WIFI_INFO_FUNC("%s driver removed\n", WIFI_DRIVER_NAME);

#if !IS_ENABLED(CFG_SUPPORT_CONNAC1X)
	wifi_pwr_on_deinit();
#endif
}

#ifdef MTK_WCN_BUILT_IN_DRIVER

int mtk_wcn_wmt_wifi_init(void)
{
	return WIFI_init();
}
EXPORT_SYMBOL(mtk_wcn_wmt_wifi_init);

void mtk_wcn_wmt_wifi_exit(void)
{
	return WIFI_exit();
}
EXPORT_SYMBOL(mtk_wcn_wmt_wifi_exit);

#else

module_init(WIFI_init);
module_exit(WIFI_exit);

#endif
