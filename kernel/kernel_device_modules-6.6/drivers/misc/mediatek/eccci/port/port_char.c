// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2016 MediaTek Inc.
 */

#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/wait.h>
#include <linux/module.h>
#include <linux/poll.h>
#ifdef CONFIG_COMPAT
#include <linux/compat.h>
#endif
#include "ccci_config.h"
#include "ccci_common_config.h"
#include "ccci_bm.h"
#include "port_proxy.h"
#include "port_char.h"
#include "port_smem.h"
#include "ccci_fsm.h"
#ifdef CONFIG_MTK_SRIL_SUPPORT
#include "modem_prj.h"
#endif

#define MAX_QUEUE_LENGTH 32

unsigned int port_char_dev_poll(struct file *fp,
	struct poll_table_struct *poll)
{
	struct port_t *port = fp->private_data;
	unsigned int mask = 0;
	int md_state = ccci_fsm_get_md_state();

	CCCI_DEBUG_LOG(0, CHAR, "poll on %s\n", port->name);
	poll_wait(fp, &port->rx_wq, poll);
	/* TODO: lack of poll wait for Tx */
	if (!skb_queue_empty(&port->rx_skb_list))
		mask |= POLLIN | POLLRDNORM;
	if (port_write_room_to_md(port) > 0)
		mask |= POLLOUT | POLLWRNORM;
	if (port->rx_ch == CCCI_UART1_RX &&
	    md_state != READY &&
		md_state != EXCEPTION) {
		/* notify MD logger to save its log
		 * before md_init kills it
		 */
		mask |= POLLERR;
		CCCI_NORMAL_LOG(0, CHAR,
			"poll error for MD logger at state %d,mask=%d\n",
			md_state, mask);
	}

	return mask;
}

static const struct file_operations char_dev_fops = {
	.owner = THIS_MODULE,
	.open = &port_dev_open, /*use default API*/
	.read = &port_dev_read, /*use default API*/
	.write = &port_dev_write, /*use default API*/
	.release = &port_dev_close,/*use default API*/
	.unlocked_ioctl = &port_dev_ioctl,/*use default API*/
#ifdef CONFIG_COMPAT
	.compat_ioctl = &port_dev_compat_ioctl,/*use default API*/
#endif
	.poll = &port_char_dev_poll,/*use port char self API*/
	.mmap = &port_dev_mmap,
};

#ifdef CONFIG_MTK_SRIL_SUPPORT
const struct file_operations *get_port_char_ops(void)
{
	return &char_dev_fops;
}
EXPORT_SYMBOL(get_port_char_ops);
#endif

static void port_char_md_state_notify(struct port_t *port, unsigned int state)
{
	struct sk_buff *skb = NULL;
	unsigned long flags;

	switch (state) {
	case GATED:
		/* when MD is stopped, the skb list of ccci_fs should be clean */
		if (port->rx_ch == CCCI_FS_RX)
			CCCI_NORMAL_LOG(0, FSM, "[%s]cccifs + GATED\n", __func__);

		if (port->flags & PORT_F_CLEAN) {
			spin_lock_irqsave(&port->rx_skb_list.lock, flags);
			while ((skb = __skb_dequeue(&port->rx_skb_list)) != NULL)
				ccci_free_skb(skb);
			spin_unlock_irqrestore(&port->rx_skb_list.lock, flags);
		}

		break;
	default:
		break;
	};

#ifdef CONFIG_MTK_SRIL_SUPPORT
	if ((port->rx_ch == CCCI_RIL_IPC0_RX || port->rx_ch == CCCI_RIL_IPC1_RX)
		&& (state == GATED || state == BOOT_WAITING_FOR_HS2
			|| state == READY || state == RESET
			|| state == EXCEPTION)) {
		wake_up_all(&port->rx_wq);
		port->md_state_changed = 1;
		pr_err("mif: %s port: %d state: %d\n",
			__func__, port->rx_ch, state);
	}
#endif
}

static int port_char_init(struct port_t *port)
{
	struct cdev *dev = NULL;
	int ret = 0;

	CCCI_DEBUG_LOG(0, CHAR,
		"char port %s is initializing\n", port->name);
	port->rx_length_th = MAX_QUEUE_LENGTH;
	port->skb_from_pool = 1;
	port->interception = 0;
	if (port->flags & PORT_F_WITH_CHAR_NODE) {
		dev = kmalloc(sizeof(struct cdev), GFP_KERNEL);
		if (unlikely(!dev)) {
			CCCI_ERROR_LOG(0, CHAR,
				"alloc char dev fail!!\n");
			return -1;
		}
		cdev_init(dev, &char_dev_fops);
		dev->owner = THIS_MODULE;
		ret = cdev_add(dev, MKDEV(port->major,
				port->minor_base + port->minor), 1);
		ret = ccci_register_dev_node(port->name, port->major,
				port->minor_base + port->minor);
		port->flags |= PORT_F_ADJUST_HEADER;
	}

	return ret;
}

#if IS_ENABLED(CONFIG_MTK_ECCCI_C2K_USB)
static usb_upstream_buffer_cb_t usb_upstream_cb;
void ccci_c2k_set_usb_callback(usb_upstream_buffer_cb_t callback)
{
	usb_upstream_cb = callback;
}
EXPORT_SYMBOL(ccci_c2k_set_usb_callback);

static int c2k_req_push_to_usb(struct port_t *port, struct sk_buff *skb)
{
	int read_len, read_count, ret = 0;
	int c2k_ch_id;
	int ppp_rx_ch = CCCI_C2K_PPP_RX;

	if (port->rx_ch == ppp_rx_ch)
		c2k_ch_id = DATA_PPP_CH_C2K-1;
	else if (port->rx_ch == CCCI_MD_LOG_RX)
		c2k_ch_id = MDLOG_CH_C2K-2;
	else {
		ret = -ENODEV;
		CCCI_ERROR_LOG(0, CHAR,
			"Err: wrong ch_id(%d) from usb bypass\n", port->rx_ch);
		return ret;
	}

	/* calculate available data */
	read_len = skb->len - sizeof(struct ccci_header);
	/* remove CCCI header */
	skb_pull(skb, sizeof(struct ccci_header));

retry_push:
	/* push to usb: USB_RAWBULK_READY: requires C2K USB driver */
	if (!usb_upstream_cb) {
		CCCI_ERROR_LOG(0, CHAR,
				"usb upstream callback is not set\n");
		return -EINVAL;
	}
	read_count = usb_upstream_cb(c2k_ch_id, skb->data, read_len);

	CCCI_DEBUG_LOG(0, CHAR,
		"data push to usb bypass (ch%d)(%d)\n",
		port->rx_ch, read_count);

	if (read_count > 0) {
		skb_pull(skb, read_count);
		read_len -= read_count;
		if (read_len > 0)
			goto retry_push;
		else if (read_len == 0)
			ccci_free_skb(skb);
		else if (read_len < 0)
			CCCI_ERROR_LOG(0, CHAR,
				"read_len error, check why come here\n");
	} else {
		CCCI_NORMAL_LOG(0, CHAR, "usb buf full\n");
		msleep(20);
		goto retry_push;
	}

	return read_len;

}
#endif

static int port_char_recv_skb(struct port_t *port, struct sk_buff *skb)
{
	if (!atomic_read(&port->usage_cnt) &&
		(port->flags&PORT_F_CLOSE_NO_DROP_PKT) == 0)
		return -CCCI_ERR_DROP_PACKET;

#if IS_ENABLED(CONFIG_MTK_ECCCI_C2K_USB)
	if (port->interception) {
		c2k_req_push_to_usb(port, skb);
		return 0;
	}
#endif
	CCCI_DEBUG_LOG(0, CHAR, "recv on %s, len=%d\n",
		port->name, port->rx_skb_list.qlen);
	return port_recv_skb(port, skb);
}

void port_char_dump_info(struct port_t *port, unsigned int flag)
{
	if (port == NULL) {
		CCCI_ERROR_LOG(0, CHAR, "%s: port==NULL\n", __func__);
		return;
	}
	if (atomic_read(&port->usage_cnt) == 0)
		return;
	if (port->flags & PORT_F_CH_TRAFFIC)
		CCCI_REPEAT_LOG(0, CHAR,
			"CHR:(%d):%dR(%d,%d,%d):%dT(%d)\n",
			port->flags, port->rx_ch,
			port->rx_skb_list.qlen,
			atomic_read(&port->rx_pkg_cnt), port->rx_drop_cnt,
			port->tx_ch, port->tx_pkg_cnt);
}
struct port_ops char_port_ops = {
	.init = &port_char_init,
	.recv_skb = &port_char_recv_skb,
	.dump_info = &port_char_dump_info,
	.md_state_notify = port_char_md_state_notify,
};

