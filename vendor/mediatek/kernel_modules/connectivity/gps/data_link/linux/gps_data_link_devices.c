/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2019 - 2021 MediaTek Inc.
 */

#include <linux/dma-mapping.h>
#include <linux/of_device.h>

#include <linux/device.h>
#include <linux/platform_device.h>

#include "gps_dl_config.h"
#include "gps_dl_context.h"
#include "gps_dl_hw_api.h"
#include "gps_dl_isr.h"
#include "gps_data_link_devices.h"
#include "gps_each_link.h"
#if GPS_DL_HAS_MCUDL
#include "gps_mcudl_devices.h"
#include "gps_mcudl_each_device.h"
#endif
#ifdef GPS_DL_HAS_MCUDL_HAL_STAT
#include "gps_mcudl_hal_stat.h"
#endif
#if GPS_DL_HAS_PLAT_DRV
#include "gps_dl_linux_plat_drv.h"
#include "gps_dl_linux_reserved_mem.h"
#endif
#if GPS_DL_MOCK_HAL
#include "gps_mock_hal.h"
#endif
#include "gps_dl_procfs.h"
#include "gps_dl_subsys_reset.h"

#if GPS_DL_GET_PLATFORM_CLOCK_FREQ
#include "gps_dl_linux_clock_mng.h"
#endif
#if GPS_DL_GET_INFO_FROM_NODE
#include "gps_dl_info_node.h"
#endif

#define GPS_DATA_LINK_DEV_NAME "gps_data_link_cdev"
int gps_dl_devno_major;
int gps_dl_devno_minor;

void gps_dl_dma_buf_free(struct gps_dl_dma_buf *p_dma_buf, enum gps_dl_link_id_enum link_id)
{
	struct gps_each_device *p_dev;

	p_dev = gps_dl_device_get(link_id);
	if (p_dev == NULL) {
		GDL_LOGXE_INI(link_id, "gps_dl_device_get return null");
		return;
	}

	if (p_dma_buf->vir_addr)
		dma_free_coherent(p_dev->dev,
			p_dma_buf->len, p_dma_buf->vir_addr, p_dma_buf->phy_addr);

	memset(p_dma_buf, 0, sizeof(*p_dma_buf));
}

int gps_dl_dma_buf_alloc(struct gps_dl_dma_buf *p_dma_buf, enum gps_dl_link_id_enum link_id,
	enum gps_dl_dma_dir dir, unsigned int len)
{
	struct gps_each_device *p_dev = NULL;
	struct device *p_linux_plat_dev = NULL;

	p_dev = gps_dl_device_get(link_id);
	if (p_dev == NULL) {
		GDL_LOGXE_INI(link_id, "gps_dl_device_get return null");
		return -1;
	}

	p_linux_plat_dev = (struct device *)p_dev->private_data;

	memset(p_dma_buf, 0, sizeof(*p_dma_buf));
	p_dma_buf->dev_index = link_id;
	p_dma_buf->dir = dir;
	p_dma_buf->len = len;
	p_dma_buf->is_for_mcudl = false;
	p_dma_buf->entry_l = GPS_DL_DMA_BUF_ENTRY_MAX;

	GDL_LOGI_INI("p_linux_plat_dev = 0x%p", p_linux_plat_dev);
	if (p_linux_plat_dev == NULL) {
	#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0)
		p_dma_buf->vir_addr = dma_alloc_coherent(
			p_dev->dev, len, &p_dma_buf->phy_addr, GFP_DMA | GFP_DMA32 | __GFP_ZERO);
	#else
		p_dma_buf->vir_addr = dma_zalloc_coherent(
			p_dev->dev, len, &p_dma_buf->phy_addr, GFP_DMA | GFP_DMA32);
	#endif

	} else {
	#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0)
		p_dma_buf->vir_addr = dma_alloc_coherent(
			p_linux_plat_dev, len, &p_dma_buf->phy_addr, GFP_DMA | __GFP_ZERO);/* | GFP_DMA32); */
	#else
		p_dma_buf->vir_addr = dma_zalloc_coherent(
			p_linux_plat_dev, len, &p_dma_buf->phy_addr, GFP_DMA);/* | GFP_DMA32); */
	#endif
	}

	GDL_LOGI_INI(
#if GPS_DL_ON_LINUX
		"alloc gps dl dma buf(%d,%d), addr: vir=0x%p, phy=0x%llx, len=%u",
#else
		"alloc gps dl dma buf(%d,%d), addr: vir=0x%p, phy=0x%08x, len=%u",
#endif
		p_dma_buf->dev_index, p_dma_buf->dir,
		p_dma_buf->vir_addr, p_dma_buf->phy_addr, p_dma_buf->len);

	if (NULL == p_dma_buf->vir_addr) {
		GDL_LOGXE_INI(link_id,
			"alloc gps dl dma buf(%d,%d)(len = %u) fail", link_id, dir, len);
		/* force move forward even fail */
		/* return -ENOMEM; */
	}

	return 0;
}

int gps_dl_dma_buf_alloc2(enum gps_dl_link_id_enum link_id)
{
	int retval;
	struct gps_each_device *p_dev;
	struct gps_each_link *p_link;

	p_dev = gps_dl_device_get(link_id);
	p_link = gps_dl_link_get(link_id);
	if (p_dev == NULL) {
		GDL_LOGXE_INI(link_id, "gps_dl_device_get return null");
		return -1;
	}

	#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0)
		of_dma_configure(p_dev->dev, p_dev->dev->of_node, false);
	#else
		of_dma_configure(p_dev->dev, p_dev->dev->of_node);
	#endif

	if (!p_dev->dev->coherent_dma_mask)
		p_dev->dev->coherent_dma_mask = DMA_BIT_MASK(32);

	if (!p_dev->dev->dma_mask)
		p_dev->dev->dma_mask = &p_dev->dev->coherent_dma_mask;


	retval = gps_dl_dma_buf_alloc(
		&p_link->tx_dma_buf, link_id, GDL_DMA_A2D, p_link->cfg.tx_buf_size);

	if (retval)
		return retval;

	retval = gps_dl_dma_buf_alloc(
		&p_link->rx_dma_buf, link_id, GDL_DMA_D2A, p_link->cfg.rx_buf_size);

	if (retval)
		return retval;

	return 0;
}

void gps_dl_ctx_links_deinit(void)
{
	enum gps_dl_link_id_enum link_id;

	struct gps_each_device *p_dev = NULL;
	struct gps_each_link *p_link = NULL;

	for (link_id = 0; link_id < GPS_DATA_LINK_NUM; link_id++) {
		p_dev = gps_dl_device_get(link_id);
		p_link = gps_dl_link_get(link_id);

		if (gps_dl_reserved_mem_is_ready()) {
			gps_dl_reserved_mem_dma_buf_deinit(&p_link->tx_dma_buf);
			gps_dl_reserved_mem_dma_buf_deinit(&p_link->rx_dma_buf);

		} else {
			gps_dl_dma_buf_free(&p_link->tx_dma_buf, link_id);
			gps_dl_dma_buf_free(&p_link->rx_dma_buf, link_id);
		}

		/* un-binding each device and link */
		p_link->p_device = NULL;
		p_dev->p_link = NULL;
		gps_each_link_deinit(link_id);
	}
}

int gps_dl_ctx_links_init(void)
{
	int retval;
	enum gps_dl_link_id_enum link_id;
	struct gps_each_device *p_dev = NULL;
	struct gps_each_link *p_link = NULL;
	enum gps_each_link_waitable_type j;

	for (link_id = 0; link_id < GPS_DATA_LINK_NUM; link_id++) {
		p_dev = gps_dl_device_get(link_id);
		p_link = gps_dl_link_get(link_id);

		if (gps_dl_reserved_mem_is_ready()) {
			gps_dl_reserved_mem_dma_buf_init(&p_link->tx_dma_buf,
				link_id, GDL_DMA_A2D, p_link->cfg.tx_buf_size);

			gps_dl_reserved_mem_dma_buf_init(&p_link->rx_dma_buf,
				link_id, GDL_DMA_D2A, p_link->cfg.rx_buf_size);
		} else {
			retval = gps_dl_dma_buf_alloc2(link_id);
			if (retval)
				return retval;
		}

		for (j = 0; j < GPS_DL_WAIT_NUM; j++)
			gps_dl_link_waitable_init(&p_link->waitables[j], j);

		/* binding each device and link */
		p_link->p_device = p_dev;
		p_dev->p_link = p_link;

		/* Todo: MNL read buf is 512, here is work-around */
		/* the solution should be make on gdl_dma_buf_get */
		gps_dl_set_rx_transfer_max(link_id, GPS_LIBMNL_READ_MAX);
		gps_each_link_init(link_id);
	}

	return 0;
}

static void gps_dl_devices_exit(void)
{
#if GPS_DL_GET_PLATFORM_CLOCK_FREQ
	gps_dl_clock_mng_deinit();
#endif

	gps_dl_device_context_deinit();

#if GPS_DL_HAS_PLAT_DRV
	gps_dl_linux_plat_drv_unregister();
#endif

}

void gps_dl_device_context_deinit(void)
{
	enum gps_dl_link_id_enum link_id;
	dev_t devno = MKDEV(gps_dl_devno_major, gps_dl_devno_minor);
	struct gps_each_device *p_dev = NULL;

#if GPS_DL_GET_INFO_FROM_NODE
	gps_dl_info_node_remove();
#endif

	gps_dl_procfs_remove();

	gps_dl_unregister_conninfra_reset_cb();

	gps_dl_irq_deinit();

#if GPS_DL_HAS_CTRLD
	gps_dl_ctrld_deinit();
#endif

#if GPS_DL_MOCK_HAL
	gps_dl_mock_deinit();
#endif

#if GPS_DL_HAS_MCUDL
	gps_mcudl_device_context_deinit();
#endif
#ifdef GPS_DL_HAS_MCUDL_HAL_STAT
	gps_mcudl_stat_lp_ctx_deinit();
	gps_mcudl_stat_mcu_ctx_deinit();
#endif

	gps_dl_ctx_links_deinit();
	gps_dl_reserved_mem_deinit();

#if GPS_DL_HAS_MCUDL
	gps_mcudl_devices_exit();
#endif

	for (link_id = 0; link_id < GPS_DATA_LINK_NUM; link_id++) {
		p_dev = gps_dl_device_get(link_id);
		gps_dl_cdev_cleanup(p_dev, link_id);
	}

	unregister_chrdev_region(devno, GPS_DATA_LINK_NUM);
}

int gps_dl_irq_init(void)
{
#if 0
	enum gps_dl_irq_index_enum irq_idx;

	for (irq_idx = 0; irq_idx < GPS_DL_IRQ_NUM; irq_idx++)
		;
#endif

	/*
	 * GPS_DL_IRQ_NUM == GPS_DL_IRQ_DMA+1 if GPS_DL_HAS_MCUDL
	 * GPS_DL_IRQ_NUM == GPS_DL_IRQ_HIF_ON+1 if GPS_DL_HAS_MCUDL
	 * we also uses GPS_DL_IRQ_NUM anyway,
	 * gps_dl_linux_irq_index_to_id will return 0 if the irq does not defined in dts,
	 * and than irq_register will be bypassed for it.
	 */
	gps_dl_linux_irqs_register(gps_dl_irq_get(0), GPS_DL_IRQ_NUM);

	return 0;
}

int gps_dl_irq_deinit(void)
{
	gps_dl_linux_irqs_unregister(gps_dl_irq_get(0), GPS_DL_IRQ_NUM);
	return 0;
}

static int gps_dl_devices_init(void)
{
#if GPS_DL_HAS_PLAT_DRV
	gps_dl_linux_plat_drv_register();
#else
	gps_dl_device_context_init();
#endif

#if GPS_DL_GET_PLATFORM_CLOCK_FREQ
	gps_dl_clock_mng_init();
#endif

	return 0;
}

void gps_dl_device_context_init(void)
{
	int result;
	enum gps_dl_link_id_enum link_id;
	dev_t devno = 0;
	struct gps_each_device *p_dev = NULL;

	result = alloc_chrdev_region(&devno, gps_dl_devno_minor,
		GPS_DATA_LINK_NUM, GPS_DATA_LINK_DEV_NAME);

	gps_dl_devno_major = MAJOR(devno);

	if (result < 0) {
		GDL_LOGE_INI("fail to get major %d\n", gps_dl_devno_major);
		return;
	}

	GDL_LOGD_INI("success to get major %d\n", gps_dl_devno_major);

	for (link_id = 0; link_id < GPS_DATA_LINK_NUM; link_id++) {
		devno = MKDEV(gps_dl_devno_major, gps_dl_devno_minor + link_id);
		p_dev = gps_dl_device_get(link_id);
		p_dev->devno = devno;
		result = gps_dl_cdev_setup(p_dev, link_id);
		if (result) {
			/* error happened */
			gps_dl_devices_exit();
			return;
		}
	}

#if GPS_DL_HAS_MCUDL
	result = gps_mcudl_devices_init();
	if (result) {
		gps_dl_devices_exit();
		return;
	}
#endif

	gps_dl_reserved_mem_init();
	gps_dl_ctx_links_init();
#if GPS_DL_HAS_MCUDL
	gps_mcudl_device_context_init();
#endif
#ifdef GPS_DL_HAS_MCUDL_HAL_STAT
	gps_mcudl_stat_lp_ctx_init();
	gps_mcudl_stat_mcu_ctx_init();
#endif

#if GPS_DL_MOCK_HAL
	gps_dl_mock_init();
#endif

#if GPS_DL_HAS_CTRLD
	gps_dl_ctrld_init();
#endif

#if (!(GPS_DL_NO_USE_IRQ || GPS_DL_HW_IS_MOCK))
	/* must after gps_dl_ctx_links_init */
	gps_dl_irq_init();
#endif
	gps_dl_register_conninfra_reset_cb();

	gps_dl_procfs_setup();

#if GPS_DL_GET_INFO_FROM_NODE
	gps_dl_info_node_setup();
#endif
}

void mtk_gps_data_link_devices_exit(void)
{
	GDL_LOGI_INI("mtk_gps_data_link_devices_exit");
	gps_dl_devices_exit();
}

int mtk_gps_data_link_devices_init(void)
{
	GDL_LOGI_INI("mtk_gps_data_link_devices_init");
	gps_dl_devices_init();
	/* GDL_ASSERT(false, 0, "test assert"); */
	return 0;
}

