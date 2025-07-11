// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 MediaTek Inc.
 */

#include <linux/module.h>
#if IS_ENABLED(CONFIG_TCPC_CLASS)
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/of_gpio.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/semaphore.h>
#include <linux/workqueue.h>
#include <linux/version.h>
#include <linux/of_irq.h>
#include <linux/sched/clock.h>
#include <linux/suspend.h>

#if IS_ENABLED(CONFIG_VBUS_NOTIFIER)
#include <linux/vbus_notifier.h>
#endif /* CONFIG_VBUS_NOTIFIER */
#if IS_ENABLED(CONFIG_PDIC_NOTIFIER)
#include <linux/usb/typec/common/pdic_core.h>
#include <linux/usb/typec/common/pdic_sysfs.h>
#include <linux/usb/typec/common/pdic_notifier.h>
#endif
#if defined(CONFIG_PDIC_NOTIFIER)
#include <linux/usb/typec/common/pdic_notifier.h>
#if IS_ENABLED(CONFIG_BATTERY_NOTIFIER)
#include <linux/battery/battery_notifier.h>
#else
#include <linux/battery/sec_pd.h>
#endif

extern struct pdic_notifier_struct pd_noti;
#endif

#include "inc/tcpci.h"
#include "inc/mt6360.h"
#include "inc/tcpci_typec.h"

#include <linux/muic/common/muic.h>
#include <linux/muic/common/vt_muic/vt_muic.h>

#if IS_ENABLED(CONFIG_RT_REGMAP)
#include "inc/rt-regmap.h"
#endif /* CONFIG_RT_REGMAP */

#if CONFIG_WATER_DETECTION
#include <charger_class.h>
#if IS_ENABLED(CONFIG_HICCUP_CHARGER)
#include <linux/muic/afc_gpio/gpio_afc_charger.h>
#endif
#endif /* CONFIG_WATER_DETECTION */

#define MT6360_DRV_VERSION	"2.0.12_MTK"

#define MEDIATEK_6360_VID	0x29cf
#define MEDIATEK_6360_PID	0x6360
#define MEDIATEK_6360_DID_V2	0x3492
#define MEDIATEK_6360_DID_V3	0x3493


struct mt6360_chip {
	struct i2c_client *client;
	struct device *dev;
#if IS_ENABLED(CONFIG_RT_REGMAP)
	struct rt_regmap_device *m_dev;
#endif /* CONFIG_RT_REGMAP */
	struct semaphore io_lock;
	struct tcpc_desc *tcpc_desc;
	struct tcpc_device *tcpc;

	int irq_gpio;
	int irq;
	int chip_id;

#if CONFIG_WATER_DETECTION
	atomic_t wd_protect_rty;
	bool wd_polling;
	struct delayed_work usbid_evt_dwork;
	struct delayed_work wd_dwork;
	struct delayed_work cc_hi_check_work;
	struct charger_device *chgdev;
#endif /* CONFIG_WATER_DETECTION */
};

#define MT6360_BOOT_WD_INTERVAL_IN_MS	5000
#define MT6360_BOOT_WD_BOOTTIME_IN_S	30

#if IS_ENABLED(CONFIG_RT_REGMAP)
RT_REG_DECL(TCPC_V10_REG_VID, 2, RT_NORMAL, {});
RT_REG_DECL(TCPC_V10_REG_PID, 2, RT_NORMAL, {});
RT_REG_DECL(TCPC_V10_REG_DID, 2, RT_NORMAL, {});
RT_REG_DECL(TCPC_V10_REG_TYPEC_REV, 2, RT_NORMAL, {});
RT_REG_DECL(TCPC_V10_REG_PD_REV, 2, RT_NORMAL, {});
RT_REG_DECL(TCPC_V10_REG_PDIF_REV, 2, RT_NORMAL, {});
RT_REG_DECL(TCPC_V10_REG_ALERT, 2, RT_VOLATILE, {});
RT_REG_DECL(TCPC_V10_REG_ALERT_MASK, 5, RT_NORMAL, {});
RT_REG_DECL(TCPC_V10_REG_TCPC_CTRL, 1, RT_NORMAL, {});
RT_REG_DECL(TCPC_V10_REG_ROLE_CTRL, 1, RT_NORMAL, {});
RT_REG_DECL(TCPC_V10_REG_FAULT_CTRL, 1, RT_NORMAL, {});
RT_REG_DECL(TCPC_V10_REG_POWER_CTRL, 1, RT_VOLATILE, {});
RT_REG_DECL(TCPC_V10_REG_CC_STATUS, 1, RT_VOLATILE, {});
RT_REG_DECL(TCPC_V10_REG_POWER_STATUS, 1, RT_VOLATILE, {});
RT_REG_DECL(TCPC_V10_REG_FAULT_STATUS, 1, RT_VOLATILE, {});
RT_REG_DECL(TCPC_V10_REG_EXT_STATUS, 1, RT_VOLATILE, {});
RT_REG_DECL(TCPC_V10_REG_COMMAND, 1, RT_VOLATILE, {});
RT_REG_DECL(TCPC_V10_REG_MSG_HDR_INFO, 1, RT_NORMAL, {});
RT_REG_DECL(TCPC_V10_REG_RX_DETECT, 1, RT_NORMAL, {});
RT_REG_DECL(TCPC_V10_REG_RX_BYTE_CNT, 32, RT_VOLATILE, {});
RT_REG_DECL(TCPC_V10_REG_TRANSMIT, 1, RT_VOLATILE, {});
RT_REG_DECL(TCPC_V10_REG_TX_BYTE_CNT, 31, RT_NORMAL, {});
RT_REG_DECL(MT6360_REG_PHY_CTRL1, 1, RT_NORMAL, {});
RT_REG_DECL(MT6360_REG_PHY_CTRL2, 1, RT_NORMAL, {});
RT_REG_DECL(MT6360_REG_PHY_CTRL3, 1, RT_NORMAL, {});
RT_REG_DECL(MT6360_REG_PHY_CTRL4, 1, RT_NORMAL, {});
RT_REG_DECL(MT6360_REG_PHY_CTRL5, 1, RT_NORMAL, {});
RT_REG_DECL(MT6360_REG_PHY_CTRL6, 1, RT_VOLATILE, {});
RT_REG_DECL(MT6360_REG_PHY_CTRL7, 1, RT_NORMAL, {});
RT_REG_DECL(MT6360_REG_CLK_CTRL1, 2, RT_NORMAL, {});
RT_REG_DECL(MT6360_REG_PHY_CTRL8, 1, RT_VOLATILE, {});
RT_REG_DECL(MT6360_REG_CC1_CTRL1, 1, RT_NORMAL, {});
RT_REG_DECL(MT6360_REG_VCONN_CTRL1, 1, RT_NORMAL, {});
RT_REG_DECL(MT6360_REG_MODE_CTRL1, 1, RT_NORMAL, {});
RT_REG_DECL(MT6360_REG_MODE_CTRL2, 1, RT_NORMAL, {});
RT_REG_DECL(MT6360_REG_MODE_CTRL3, 1, RT_VOLATILE, {});
RT_REG_DECL(MT6360_REG_MT_MASK1, 1, RT_NORMAL, {});
RT_REG_DECL(MT6360_REG_MT_MASK2, 1, RT_NORMAL, {});
RT_REG_DECL(MT6360_REG_MT_MASK3, 1, RT_NORMAL, {});
RT_REG_DECL(MT6360_REG_MT_MASK4, 1, RT_NORMAL, {});
RT_REG_DECL(MT6360_REG_MT_MASK5, 1, RT_NORMAL, {});
RT_REG_DECL(MT6360_REG_MT_INT1, 1, RT_VOLATILE, {});
RT_REG_DECL(MT6360_REG_MT_INT2, 1, RT_VOLATILE, {});
RT_REG_DECL(MT6360_REG_MT_INT3, 1, RT_VOLATILE, {});
RT_REG_DECL(MT6360_REG_MT_INT4, 1, RT_VOLATILE, {});
RT_REG_DECL(MT6360_REG_MT_INT5, 1, RT_VOLATILE, {});
RT_REG_DECL(MT6360_REG_MT_ST1, 1, RT_VOLATILE, {});
RT_REG_DECL(MT6360_REG_MT_ST2, 1, RT_VOLATILE, {});
RT_REG_DECL(MT6360_REG_MT_ST3, 1, RT_VOLATILE, {});
RT_REG_DECL(MT6360_REG_MT_ST4, 1, RT_VOLATILE, {});
RT_REG_DECL(MT6360_REG_MT_ST5, 1, RT_VOLATILE, {});
RT_REG_DECL(MT6360_REG_SWRESET, 1, RT_VOLATILE, {});
RT_REG_DECL(MT6360_REG_DEBOUNCE_CTRL1, 1, RT_NORMAL, {});
RT_REG_DECL(MT6360_REG_DRP_CTRL1, 1, RT_NORMAL, {});
RT_REG_DECL(MT6360_REG_DRP_CTRL2, 2, RT_NORMAL, {});
RT_REG_DECL(MT6360_REG_PD3_CTRL, 1, RT_NORMAL, {});
RT_REG_DECL(MT6360_REG_VBUS_DISC_CTRL, 1, RT_NORMAL, {});
RT_REG_DECL(MT6360_REG_CTD_CTRL1, 1, RT_NORMAL, {});
RT_REG_DECL(MT6360_REG_I2CRST_CTRL, 1, RT_NORMAL, {});
RT_REG_DECL(MT6360_REG_WD_DET_CTRL1, 1, RT_VOLATILE, {});
RT_REG_DECL(MT6360_REG_WD_DET_CTRL2, 1, RT_NORMAL, {});
RT_REG_DECL(MT6360_REG_WD_DET_CTRL3, 1, RT_NORMAL, {});
RT_REG_DECL(MT6360_REG_WD_DET_CTRL4, 1, RT_NORMAL, {});
RT_REG_DECL(MT6360_REG_WD_DET_CTRL5, 1, RT_NORMAL, {});
RT_REG_DECL(MT6360_REG_WD_DET_CTRL6, 1, RT_NORMAL, {});
RT_REG_DECL(MT6360_REG_WD_DET_CTRL7, 1, RT_NORMAL, {});
RT_REG_DECL(MT6360_REG_WD_DET_CTRL8, 1, RT_NORMAL, {});
RT_REG_DECL(MT6360_REG_PHY_CTRL9, 1, RT_NORMAL, {});
RT_REG_DECL(MT6360_REG_PHY_CTRL10, 1, RT_NORMAL, {});
RT_REG_DECL(MT6360_REG_PHY_CTRL11, 1, RT_NORMAL, {});
RT_REG_DECL(MT6360_REG_PHY_CTRL12, 1, RT_NORMAL, {});
RT_REG_DECL(MT6360_REG_PHY_CTRL13, 1, RT_NORMAL, {});
RT_REG_DECL(MT6360_REG_PHY_CTRL14, 1, RT_NORMAL, {});
RT_REG_DECL(MT6360_REG_RX_CTRL1, 1, RT_NORMAL, {});
RT_REG_DECL(MT6360_REG_RX_CTRL2, 1, RT_NORMAL, {});
RT_REG_DECL(MT6360_REG_VBUS_CTRL2, 1, RT_NORMAL, {});
RT_REG_DECL(MT6360_REG_HILO_CTRL5, 1, RT_NORMAL, {});
RT_REG_DECL(MT6360_REG_VCONN_CTRL2, 1, RT_VOLATILE, {});
RT_REG_DECL(MT6360_REG_VCONN_CTRL3, 1, RT_VOLATILE, {});
RT_REG_DECL(MT6360_REG_DEBOUNCE_CTRL4, 1, RT_NORMAL, {});
RT_REG_DECL(MT6360_REG_CTD_CTRL2, 1, RT_VOLATILE, {});
#if IS_ENABLED(CONFIG_BATTERY_SAMSUNG)
RT_REG_DECL(MT6360_REG_CC_CTRL5, 1, RT_VOLATILE, {});
#endif

static const rt_register_map_t mt6360_chip_regmap[] = {
	RT_REG(TCPC_V10_REG_VID),
	RT_REG(TCPC_V10_REG_PID),
	RT_REG(TCPC_V10_REG_DID),
	RT_REG(TCPC_V10_REG_TYPEC_REV),
	RT_REG(TCPC_V10_REG_PD_REV),
	RT_REG(TCPC_V10_REG_PDIF_REV),
	RT_REG(TCPC_V10_REG_ALERT),
	RT_REG(TCPC_V10_REG_ALERT_MASK),
	RT_REG(TCPC_V10_REG_TCPC_CTRL),
	RT_REG(TCPC_V10_REG_ROLE_CTRL),
	RT_REG(TCPC_V10_REG_FAULT_CTRL),
	RT_REG(TCPC_V10_REG_POWER_CTRL),
	RT_REG(TCPC_V10_REG_CC_STATUS),
	RT_REG(TCPC_V10_REG_POWER_STATUS),
	RT_REG(TCPC_V10_REG_FAULT_STATUS),
	RT_REG(TCPC_V10_REG_EXT_STATUS),
	RT_REG(TCPC_V10_REG_COMMAND),
	RT_REG(TCPC_V10_REG_MSG_HDR_INFO),
	RT_REG(TCPC_V10_REG_RX_DETECT),
	RT_REG(TCPC_V10_REG_RX_BYTE_CNT),
	RT_REG(TCPC_V10_REG_TRANSMIT),
	RT_REG(TCPC_V10_REG_TX_BYTE_CNT),
	RT_REG(MT6360_REG_PHY_CTRL1),
	RT_REG(MT6360_REG_PHY_CTRL2),
	RT_REG(MT6360_REG_PHY_CTRL3),
	RT_REG(MT6360_REG_PHY_CTRL4),
	RT_REG(MT6360_REG_PHY_CTRL5),
	RT_REG(MT6360_REG_PHY_CTRL6),
	RT_REG(MT6360_REG_PHY_CTRL7),
	RT_REG(MT6360_REG_CLK_CTRL1),
	RT_REG(MT6360_REG_PHY_CTRL8),
	RT_REG(MT6360_REG_CC1_CTRL1),
	RT_REG(MT6360_REG_VCONN_CTRL1),
	RT_REG(MT6360_REG_MODE_CTRL1),
	RT_REG(MT6360_REG_MODE_CTRL2),
	RT_REG(MT6360_REG_MODE_CTRL3),
	RT_REG(MT6360_REG_MT_MASK1),
	RT_REG(MT6360_REG_MT_MASK2),
	RT_REG(MT6360_REG_MT_MASK3),
	RT_REG(MT6360_REG_MT_MASK4),
	RT_REG(MT6360_REG_MT_MASK5),
	RT_REG(MT6360_REG_MT_INT1),
	RT_REG(MT6360_REG_MT_INT2),
	RT_REG(MT6360_REG_MT_INT3),
	RT_REG(MT6360_REG_MT_INT4),
	RT_REG(MT6360_REG_MT_INT5),
	RT_REG(MT6360_REG_MT_ST1),
	RT_REG(MT6360_REG_MT_ST2),
	RT_REG(MT6360_REG_MT_ST3),
	RT_REG(MT6360_REG_MT_ST4),
	RT_REG(MT6360_REG_MT_ST5),
	RT_REG(MT6360_REG_SWRESET),
	RT_REG(MT6360_REG_DEBOUNCE_CTRL1),
	RT_REG(MT6360_REG_DRP_CTRL1),
	RT_REG(MT6360_REG_DRP_CTRL2),
	RT_REG(MT6360_REG_PD3_CTRL),
	RT_REG(MT6360_REG_VBUS_DISC_CTRL),
	RT_REG(MT6360_REG_CTD_CTRL1),
	RT_REG(MT6360_REG_I2CRST_CTRL),
	RT_REG(MT6360_REG_WD_DET_CTRL1),
	RT_REG(MT6360_REG_WD_DET_CTRL2),
	RT_REG(MT6360_REG_WD_DET_CTRL3),
	RT_REG(MT6360_REG_WD_DET_CTRL4),
	RT_REG(MT6360_REG_WD_DET_CTRL5),
	RT_REG(MT6360_REG_WD_DET_CTRL6),
	RT_REG(MT6360_REG_WD_DET_CTRL7),
	RT_REG(MT6360_REG_WD_DET_CTRL8),
	RT_REG(MT6360_REG_PHY_CTRL9),
	RT_REG(MT6360_REG_PHY_CTRL10),
	RT_REG(MT6360_REG_PHY_CTRL11),
	RT_REG(MT6360_REG_PHY_CTRL12),
	RT_REG(MT6360_REG_PHY_CTRL13),
	RT_REG(MT6360_REG_PHY_CTRL14),
	RT_REG(MT6360_REG_RX_CTRL1),
	RT_REG(MT6360_REG_RX_CTRL2),
	RT_REG(MT6360_REG_VBUS_CTRL2),
	RT_REG(MT6360_REG_HILO_CTRL5),
	RT_REG(MT6360_REG_VCONN_CTRL2),
	RT_REG(MT6360_REG_VCONN_CTRL3),
	RT_REG(MT6360_REG_DEBOUNCE_CTRL4),
	RT_REG(MT6360_REG_CTD_CTRL2),
#if IS_ENABLED(CONFIG_BATTERY_SAMSUNG)
	RT_REG(MT6360_REG_CC_CTRL5),
#endif
};
#define MT6360_CHIP_REGMAP_SIZE ARRAY_SIZE(mt6360_chip_regmap)
#endif /* CONFIG_RT_REGMAP */

#define MT6360_I2C_RETRY_CNT	5
static int mt6360_read_device(void *client, u32 reg, int len, void *dst)
{
	struct i2c_client *i2c = client;
	struct mt6360_chip *chip = i2c_get_clientdata(i2c);
	struct tcpc_device *tcpc = chip->tcpc;
	int ret = 0, count = MT6360_I2C_RETRY_CNT;

	atomic_inc(&tcpc->suspend_pending);
	wait_event_timeout(tcpc->resume_wait_que,
		   !chip->dev->parent->power.is_suspended,
				 msecs_to_jiffies(1000));
	while (1) {
		ret = i2c_smbus_read_i2c_block_data(i2c, reg, len, dst);
		if (ret < 0 && count > 1)
			count--;
		else
			break;
		udelay(100);
	}
	atomic_dec_if_positive(&tcpc->suspend_pending);
	return ret;
}

static int mt6360_write_device(void *client, u32 reg, int len, const void *src)
{
	struct i2c_client *i2c = client;
	struct mt6360_chip *chip = i2c_get_clientdata(i2c);
	struct tcpc_device *tcpc = chip->tcpc;
	int ret = 0, count = MT6360_I2C_RETRY_CNT;

	atomic_inc(&tcpc->suspend_pending);
	wait_event_timeout(tcpc->resume_wait_que,
		   !chip->dev->parent->power.is_suspended,
				 msecs_to_jiffies(1000));
	while (1) {
		ret = i2c_smbus_write_i2c_block_data(i2c, reg, len, src);
		if (ret < 0 && count > 1)
			count--;
		else
			break;
		udelay(100);
	}
	atomic_dec_if_positive(&tcpc->suspend_pending);
	return ret;
}

static int mt6360_reg_read(struct i2c_client *i2c, u8 reg, u8 *data)
{
	int ret;
	struct mt6360_chip *chip = i2c_get_clientdata(i2c);

#if IS_ENABLED(CONFIG_RT_REGMAP)
	ret = rt_regmap_block_read(chip->m_dev, reg, 1, data);
#else
	ret = mt6360_read_device(chip->client, reg, 1, data);
#endif /* CONFIG_RT_REGMAP */
	if (ret < 0) {
		dev_err(chip->dev, "%s fail(%d)\n", __func__, ret);
		return ret;
	}
	return 0;
}

static int mt6360_reg_write(struct i2c_client *i2c, u8 reg, const u8 data)
{
	int ret;
	struct mt6360_chip *chip = i2c_get_clientdata(i2c);

#if IS_ENABLED(CONFIG_RT_REGMAP)
	ret = rt_regmap_block_write(chip->m_dev, reg, 1, &data);
#else
	ret = mt6360_write_device(chip->client, reg, 1, &data);
#endif /* CONFIG_RT_REGMAP */
	if (ret < 0)
		dev_err(chip->dev, "%s fail(%d)\n", __func__, ret);
	return 0;
}

static int mt6360_block_read(struct i2c_client *i2c, u8 reg, int len, void *dst)
{
	int ret;
	struct mt6360_chip *chip = i2c_get_clientdata(i2c);

#if IS_ENABLED(CONFIG_RT_REGMAP)
	ret = rt_regmap_block_read(chip->m_dev, reg, len, dst);
#else
	ret = mt6360_read_device(chip->client, reg, len, dst);
#endif /* CONFIG_RT_REGMAP */
	if (ret < 0)
		dev_err(chip->dev, "%s fail(%d)\n", __func__, ret);
	return 0;
}

static int mt6360_block_write(struct i2c_client *i2c, u8 reg, int len,
			      const void *src)
{
	int ret;
	struct mt6360_chip *chip = i2c_get_clientdata(i2c);

#if IS_ENABLED(CONFIG_RT_REGMAP)
	ret = rt_regmap_block_write(chip->m_dev, reg, len, src);
#else
	ret = mt6360_write_device(chip->client, reg, len, src);
#endif /* CONFIG_RT_REGMAP */
	if (ret < 0)
		dev_err(chip->dev, "%s fail(%d)\n", __func__, ret);
	return 0;
}

static int mt6360_write_word(struct i2c_client *client, u8 reg_addr, u16 data)
{
	data = cpu_to_le16(data);
	return mt6360_block_write(client, reg_addr, 2, &data);
}

static int mt6360_read_word(struct i2c_client *client, u8 reg_addr, u16 *data)
{
	int ret;

	ret = mt6360_block_read(client, reg_addr, 2, data);
	if (ret < 0)
		return ret;
	*data = le16_to_cpu(*data);
	return 0;
}

static inline int mt6360_i2c_write8(struct tcpc_device *tcpc, u8 reg,
				    const u8 data)
{
	int ret;
	struct mt6360_chip *chip = tcpc_get_dev_data(tcpc);

	down(&chip->io_lock);
	ret = mt6360_reg_write(chip->client, reg, data);
	up(&chip->io_lock);
	return ret;
}

static inline int mt6360_i2c_write16(struct tcpc_device *tcpc, u8 reg,
				     const u16 data)
{
	int ret;
	struct mt6360_chip *chip = tcpc_get_dev_data(tcpc);

	down(&chip->io_lock);
	ret = mt6360_write_word(chip->client, reg, data);
	up(&chip->io_lock);
	return ret;
}

static inline int mt6360_i2c_read8(struct tcpc_device *tcpc, u8 reg, u8 *data)
{
	int ret;
	struct mt6360_chip *chip = tcpc_get_dev_data(tcpc);

	down(&chip->io_lock);
	ret = mt6360_reg_read(chip->client, reg, data);
	up(&chip->io_lock);
	return ret;
}

static inline int mt6360_i2c_read16(struct tcpc_device *tcpc, u8 reg, u16 *data)
{
	int ret;
	struct mt6360_chip *chip = tcpc_get_dev_data(tcpc);

	down(&chip->io_lock);
	ret = mt6360_read_word(chip->client, reg, data);
	up(&chip->io_lock);
	return ret;
}

static inline int mt6360_i2c_block_read(struct tcpc_device *tcpc, u8 reg,
					int len, void *dst)
{
	int ret;
	struct mt6360_chip *chip = tcpc_get_dev_data(tcpc);

	down(&chip->io_lock);
	ret = mt6360_block_read(chip->client, reg, len, dst);
	up(&chip->io_lock);
	return ret;
}

static inline int mt6360_i2c_block_write(struct tcpc_device *tcpc, u8 reg,
					 int len, const void *src)
{
	int ret;
	struct mt6360_chip *chip = tcpc_get_dev_data(tcpc);

	down(&chip->io_lock);
	ret = mt6360_block_write(chip->client, reg, len, src);
	up(&chip->io_lock);
	return ret;
}

static inline int mt6360_i2c_update_bits(struct tcpc_device *tcpc, u8 reg,
					 u8 val, u8 mask)
{
	int ret;
	u8 data;
	struct mt6360_chip *chip = tcpc_get_dev_data(tcpc);

	down(&chip->io_lock);
	ret = mt6360_reg_read(chip->client, reg, &data);
	if (ret < 0) {
		up(&chip->io_lock);
		return ret;
	}

	data &= ~mask;
	data |= (val & mask);

	ret = mt6360_reg_write(chip->client, reg, data);
	up(&chip->io_lock);
	return ret;
}

static inline int mt6360_i2c_set_bit(struct tcpc_device *tcpc, u8 reg, u8 mask)
{
	return mt6360_i2c_update_bits(tcpc, reg, mask, mask);
}

static inline int mt6360_i2c_clr_bit(struct tcpc_device *tcpc, u8 reg, u8 mask)
{
	return mt6360_i2c_update_bits(tcpc, reg, 0x00, mask);
}

#if IS_ENABLED(CONFIG_RT_REGMAP)
static struct rt_regmap_fops mt6360_regmap_fops = {
	.read_device = mt6360_read_device,
	.write_device = mt6360_write_device,
};

static int mt6360_regmap_init(struct mt6360_chip *chip)
{
	struct rt_regmap_properties *props;
	char name[32];
	int len;

	props = devm_kzalloc(chip->dev, sizeof(*props), GFP_KERNEL);
	if (!props)
		return -ENOMEM;

	props->register_num = MT6360_CHIP_REGMAP_SIZE;
	props->rm = mt6360_chip_regmap;
	props->rt_regmap_mode = RT_MULTI_BYTE |
				RT_IO_PASS_THROUGH | RT_DBG_SPECIAL;

	len = snprintf(name, sizeof(name), "mt6360-%02x", chip->client->addr);
	if (len < 0 || len > 32)
		return -EINVAL;
	len = strlen(name);
	props->name = devm_kzalloc(chip->dev, len + 1, GFP_KERNEL);
	props->aliases = devm_kzalloc(chip->dev, len + 1, GFP_KERNEL);
	if (!props->name || !props->aliases)
		return -ENOMEM;
	strlcpy((char *)props->name, name, len + 1);
	strlcpy((char *)props->aliases, name, len + 1);
	props->io_log_en = 0;
	chip->m_dev = rt_regmap_device_register(props, &mt6360_regmap_fops,
						chip->dev, chip->client, chip);
	if (!chip->m_dev) {
		dev_err(chip->dev, "%s register fail\n", __func__);
		return -EINVAL;
	}
	return 0;
}

static void mt6360_regmap_deinit(struct mt6360_chip *chip)
{
	rt_regmap_device_unregister(chip->m_dev);
}
#endif /* CONFIG_RT_REGMAP */

static inline int mt6360_software_reset(struct tcpc_device *tcpc)
{
	int ret;
#if IS_ENABLED(CONFIG_RT_REGMAP)
	struct mt6360_chip *chip = tcpc_get_dev_data(tcpc);
#endif /* CONFIG_RT_REGMAP */

	ret = mt6360_i2c_write8(tcpc, MT6360_REG_SWRESET, 1);
	if (ret < 0)
		return ret;
#if IS_ENABLED(CONFIG_RT_REGMAP)
	rt_regmap_cache_reload(chip->m_dev);
#endif /* CONFIG_RT_REGMAP */
	usleep_range(1000, 2000);
	return 0;
}

static inline int mt6360_command(struct tcpc_device *tcpc, uint8_t cmd)
{
	return mt6360_i2c_write8(tcpc, TCPC_V10_REG_COMMAND, cmd);
}

static inline int mt6360_init_vend_mask(struct tcpc_device *tcpc)
{
	u8 mask[MT6360_VEND_INT_MAX] = {0};

	mask[MT6360_VEND_INT1] |= MT6360_M_WAKEUP |
				  MT6360_M_VBUS_SAFE0V |
				  MT6360_M_VCONN_SHT_GND |
				  MT6360_M_VBUS_VALID;
	mask[MT6360_VEND_INT2] |= MT6360_M_VCONN_OV_CC1 |
				  MT6360_M_VCONN_OV_CC2 |
				  MT6360_M_VCONN_OCR |
				  MT6360_M_VCONN_INVALID;

#if CONFIG_CABLE_TYPE_DETECTION
	if (tcpc->tcpc_flags & TCPC_FLAGS_CABLE_TYPE_DETECTION)
		mask[MT6360_VEND_INT3] |= MT6360_M_CTD;
#endif /* CONFIG_CABLE_TYPE_DETECTION */

	return mt6360_i2c_block_write(tcpc, MT6360_REG_MT_MASK1,
				      MT6360_VEND_INT_MAX, mask);
}

static inline int __mt6360_init_alert_mask(struct tcpc_device *tcpc)
{
	u16 mask = TCPC_V10_REG_ALERT_CC_STATUS |
		   TCPC_V10_REG_ALERT_FAULT |
		   TCPC_V10_REG_ALERT_VBUS_SINK_DISCONNECT |
		   TCPC_V10_REG_ALERT_VENDOR_DEFINED;
	u8 masks[5] = {0x00, 0x00, 0x00,
		       TCPC_V10_REG_FAULT_STATUS_VCONN_OC, 0x00};

#if IS_ENABLED(CONFIG_USB_POWER_DELIVERY)
	mask |= TCPC_V10_REG_ALERT_TX_SUCCESS |
		TCPC_V10_REG_ALERT_TX_DISCARDED |
		TCPC_V10_REG_ALERT_TX_FAILED |
		TCPC_V10_REG_ALERT_RX_HARD_RST |
		TCPC_V10_REG_ALERT_RX_STATUS |
		TCPC_V10_REG_ALERT_RX_OVERFLOW;
#endif /* CONFIG_USB_POWER_DELIVERY */
	*(u16 *)masks = cpu_to_le16(mask);
	return mt6360_i2c_block_write(tcpc, TCPC_V10_REG_ALERT_MASK,
				      sizeof(masks), masks);
}

static int mt6360_init_alert_mask(struct tcpc_device *tcpc)
{
	struct mt6360_chip *chip = tcpc_get_dev_data(tcpc);

	dev_info(chip->dev, "%s\n", __func__);

	mt6360_init_vend_mask(tcpc);
	__mt6360_init_alert_mask(tcpc);

	return 0;
}

static irqreturn_t mt6360_intr_handler(int irq, void *data)
{
	struct mt6360_chip *chip = data;
	int ret = 0;

	pm_stay_awake(chip->dev);
	tcpci_lock_typec(chip->tcpc);
	do {
		ret = tcpci_alert(chip->tcpc, false);
	} while (ret != -ENODATA);
	tcpci_unlock_typec(chip->tcpc);
	pm_relax(chip->dev);

	return IRQ_HANDLED;
}

static inline int mt6360_mask_clear_alert(struct tcpc_device *tcpc)
{
	/* Mask all alerts & clear them */
	mt6360_i2c_write16(tcpc, TCPC_V10_REG_ALERT_MASK, 0);
	mt6360_i2c_write16(tcpc, TCPC_V10_REG_ALERT, 0xffff);
	return 0;
}

static inline int mt6360_enable_auto_rpconnect(struct tcpc_device *tcpc,
					       bool en)
{
	return (en ? mt6360_i2c_clr_bit : mt6360_i2c_set_bit)
		(tcpc, MT6360_REG_CTD_CTRL2, MT6360_DIS_RPDET);
}

#if CONFIG_WATER_DETECTION
static int mt6360_is_water_detected(struct tcpc_device *tcpc);
static void mt6360_enable_irq(struct mt6360_chip *chip, const char *name,
			      bool en);

static int mt6360_enable_usbid_polling(struct mt6360_chip *chip, bool en)
{
	int ret;

	if (en == chip->wd_polling)
		return 0;
	if (en) {
		ret = charger_dev_set_usbid_src_ton(chip->chgdev, 100000);
		if (ret < 0) {
			dev_err(chip->dev, "%s usbid src on 100ms fail\n",
					__func__);
			return ret;
		}

		ret = charger_dev_set_usbid_rup(chip->chgdev, 75000);
		if (ret < 0) {
			dev_err(chip->dev, "%s usbid rup75k fail\n", __func__);
			return ret;
		}
	}

	ret = charger_dev_enable_usbid(chip->chgdev, en);
	if (ret < 0)
		return ret;
	chip->wd_polling = en;
	mt6360_enable_irq(chip, "usbid_evt", en);

	if (en && ktime_get_boottime_seconds() <= MT6360_BOOT_WD_BOOTTIME_IN_S)
		schedule_delayed_work(&chip->usbid_evt_dwork,
			msecs_to_jiffies(MT6360_BOOT_WD_INTERVAL_IN_MS));
	return 0;
}

static void mt6360_pmu_usbid_evt_dwork_handler(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct mt6360_chip *chip = container_of(dwork,
						struct mt6360_chip,
						usbid_evt_dwork);
	struct tcpc_device *tcpcs[] = {chip->tcpc};
	int ret = 0;

	pm_system_wakeup();

	tcpci_lock_typec(chip->tcpc);
	MT6360_INFO("wd_polling = %d\n", chip->wd_polling);
	if (!chip->wd_polling)
		goto out;
	mt6360_enable_usbid_polling(chip, false);
#if !CONFIG_WD_DURING_PLUGGED_IN
	if (tcpci_is_plugged_in(chip->tcpc))
		goto out;
#endif	/* !CONFIG_WD_DURING_PLUGGED_IN */
	ret = mt6360_is_water_detected(chip->tcpc);
	if (ret <= 0 ||
	    tcpc_typec_handle_wd(tcpcs, ARRAY_SIZE(tcpcs), true) == -EAGAIN)
		mt6360_enable_usbid_polling(chip, true);
out:
	tcpci_unlock_typec(chip->tcpc);
}

static irqreturn_t mt6360_pmu_usbid_evt_handler(int irq, void *data)
{
	struct mt6360_chip *chip = data;

	mod_delayed_work(system_wq, &chip->usbid_evt_dwork,
		msecs_to_jiffies(4000));
	return IRQ_HANDLED;
}
#endif /* CONFIG_WATER_DETECTION */

struct mt6360_pmu_irq_desc {
	const char *name;
	irq_handler_t irq_handler;
	int irq;
};

#define MT6360_PMU_IRQDESC(name) {#name, mt6360_pmu_##name##_handler, -1}

static struct mt6360_pmu_irq_desc mt6360_pmu_tcpc_irq_desc[] = {
#if CONFIG_WATER_DETECTION
	MT6360_PMU_IRQDESC(usbid_evt),
#endif /* CONFIG_WATER_DETECTION */
};

#if CONFIG_WATER_DETECTION
static void mt6360_enable_irq(struct mt6360_chip *chip, const char *name,
			      bool en)
{
	struct mt6360_pmu_irq_desc *irq_desc;
	int i;

	for (i = 0; i < ARRAY_SIZE(mt6360_pmu_tcpc_irq_desc); i++) {
		irq_desc = &mt6360_pmu_tcpc_irq_desc[i];
		if (!strcmp(irq_desc->name, name)) {
			(en ? enable_irq : disable_irq_nosync)(irq_desc->irq);
			break;
		}
	}
}
#endif /* CONFIG_WATER_DETECTION */

static struct resource *mt6360_tcpc_get_irq_byname(struct device *dev,
						   unsigned int type,
						   const char *name)
{
	int i;
	struct mt6360_tcpc_platform_data *pdata = dev_get_platdata(dev);

	for (i = 0; i < pdata->irq_res_cnt; i++) {
		struct resource *r = pdata->irq_res + i;

		if (unlikely(!r->name))
			continue;

		if (type == resource_type(r) && !strcmp(r->name, name))
			return r;
	}
	return NULL;
}

static int mt6360_pmu_tcpc_irq_register(struct tcpc_device *tcpc)
{
	struct mt6360_chip *chip = tcpc_get_dev_data(tcpc);
	struct mt6360_pmu_irq_desc *irq_desc;
	struct resource *r;
	int i, ret = 0;

	for (i = 0; i < ARRAY_SIZE(mt6360_pmu_tcpc_irq_desc); i++) {
		irq_desc = &mt6360_pmu_tcpc_irq_desc[i];
		if (unlikely(!irq_desc->name))
			continue;
		r = mt6360_tcpc_get_irq_byname(chip->dev, IORESOURCE_IRQ,
					       irq_desc->name);
		if (!r)
			continue;
		irq_desc->irq = r->start;
		ret = devm_request_threaded_irq(chip->dev, irq_desc->irq, NULL,
						irq_desc->irq_handler,
						IRQF_TRIGGER_FALLING |
						IRQF_ONESHOT,
						irq_desc->name,
						chip);
		if (ret < 0)
			dev_err(chip->dev, "%s request %s irq fail\n", __func__,
				irq_desc->name);
		else
			disable_irq_nosync(irq_desc->irq);
	}
	return ret;
}

static int mt6360_init_alert(struct tcpc_device *tcpc)
{
	struct mt6360_chip *chip = tcpc_get_dev_data(tcpc);
	int ret = 0;
	char *name = NULL;

	mt6360_mask_clear_alert(tcpc);

	ret = mt6360_pmu_tcpc_irq_register(tcpc);
	if (ret < 0)
		return ret;

	name = devm_kasprintf(chip->dev, GFP_KERNEL, "%s-IRQ",
			      chip->tcpc_desc->name);
	if (!name)
		return -ENOMEM;

	dev_info(chip->dev, "%s name = %s, gpio = %d\n",
			    __func__, chip->tcpc_desc->name, chip->irq_gpio);

	ret = devm_gpio_request(chip->dev, chip->irq_gpio, name);
	if (ret < 0) {
		dev_notice(chip->dev, "%s request GPIO fail(%d)\n",
				      __func__, ret);
		return ret;
	}

	ret = gpio_direction_input(chip->irq_gpio);
	if (ret < 0) {
		dev_notice(chip->dev, "%s set GPIO fail(%d)\n", __func__, ret);
		return ret;
	}

	ret = gpio_to_irq(chip->irq_gpio);
	if (ret < 0) {
		dev_notice(chip->dev, "%s gpio to irq fail(%d)",
				      __func__, ret);
		return ret;
	}
	chip->irq = ret;

	dev_info(chip->dev, "%s IRQ number = %d\n", __func__, chip->irq);

	device_init_wakeup(chip->dev, true);
	ret = devm_request_threaded_irq(chip->dev, chip->irq, NULL,
					mt6360_intr_handler,
					IRQF_TRIGGER_LOW | IRQF_ONESHOT,
					name, chip);
	if (ret < 0) {
		dev_notice(chip->dev, "%s request irq fail(%d)\n",
				      __func__, ret);
		return ret;
	}
	enable_irq_wake(chip->irq);

	return 0;
}

static inline int mt6360_vend_alert_status_clear(struct tcpc_device *tcpc,
						 const u8 *mask)
{
	return mt6360_i2c_block_write(tcpc, MT6360_REG_MT_INT1,
			       MT6360_VEND_INT_MAX, mask);
}

static int mt6360_alert_status_clear(struct tcpc_device *tcpc, u32 mask)
{
	u16 std_mask = mask & 0xffff;

	if (std_mask)
		return mt6360_i2c_write16(tcpc, TCPC_V10_REG_ALERT, std_mask);
	return 0;
}

static int mt6360_set_clock_gating(struct tcpc_device *tcpc, bool en)
{
	int ret = 0;
#if CONFIG_TCPC_CLOCK_GATING
	int i = 0;
	u8 clks[2] = {MT6360_CLK_DIV_600K_EN | MT6360_CLK_DIV_300K_EN,
		      MT6360_CLK_DIV_2P4M_EN};

	if (en) {
		for (i = 0; i < 2; i++)
			ret = mt6360_alert_status_clear(tcpc,
				TCPC_V10_REG_ALERT_RX_ALL_MASK);
	} else {
		clks[0] |= MT6360_CLK_BCLK2_EN | MT6360_CLK_BCLK_EN;
		clks[1] |= MT6360_CLK_CK_24M_EN | MT6360_CLK_PCLK_EN;
	}

	if (ret == 0)
		ret = mt6360_i2c_block_write(tcpc, MT6360_REG_CLK_CTRL1,
					     sizeof(clks), clks);
#endif	/* CONFIG_TCPC_CLOCK_GATING */

	return ret;
}

static inline int mt6360_init_drp_duty(struct tcpc_device *tcpc)
{
	/*
	 * DRP Toggle Cycle : 51.2 + 6.4*val ms
	 * DRP Duty Ctrl : (dcSRC + 1) / 1024
	 */
	mt6360_i2c_write8(tcpc, MT6360_REG_DRP_CTRL1, 0);
	mt6360_i2c_write16(tcpc, MT6360_REG_DRP_CTRL2, TCPC_NORMAL_RP_DUTY);
	return 0;
}

static inline int mt6360_init_phy_ctrl(struct tcpc_device *tcpc)
{
	/* Disable TX Discard and auto-retry method */
	mt6360_i2c_write8(tcpc, MT6360_REG_PHY_CTRL1,
			  MT6360_REG_PHY_CTRL1_SET(0, 7, 0, 0));
	/* PHY CDR threshold */
	mt6360_i2c_write8(tcpc, MT6360_REG_PHY_CTRL2, 0x3A);
	/* Transition window count */
	mt6360_i2c_write8(tcpc, MT6360_REG_PHY_CTRL3, 0x82);
	/* BMC decoder idle time = 17.982us */
	mt6360_i2c_write8(tcpc, MT6360_REG_PHY_CTRL7, 0x36);
	mt6360_i2c_write8(tcpc, MT6360_REG_PHY_CTRL11, 0x60);
	/* Retry period setting, 416ns per step */
	mt6360_i2c_write8(tcpc, MT6360_REG_PHY_CTRL12, 0x3C);
	mt6360_i2c_write8(tcpc, MT6360_REG_RX_CTRL1, 0xE8);
	return 0;
}

static inline int mt6360_vconn_oc_handler(struct tcpc_device *tcpc)
{
	int ret;
	u8 reg;

	ret = mt6360_i2c_read8(tcpc, MT6360_REG_VCONN_CTRL1, &reg);
	if (ret < 0)
		return ret;
	/* If current limit is enabled, there's no need to turn off vconn */
	if (reg & MT6360_VCONN_CLIMIT_EN)
		return 0;
	return tcpci_set_vconn(tcpc, false);
}

static int mt6360_fault_status_clear(struct tcpc_device *tcpc, u8 status)
{
	if (status & TCPC_V10_REG_FAULT_STATUS_VCONN_OC)
		mt6360_vconn_oc_handler(tcpc);

	return mt6360_i2c_write8(tcpc, TCPC_V10_REG_FAULT_STATUS, status);
}

static int mt6360_set_alert_mask(struct tcpc_device *tcpc, u32 mask)
{
	return mt6360_i2c_write16(tcpc, TCPC_V10_REG_ALERT_MASK, mask);
}

static int mt6360_get_alert_mask(struct tcpc_device *tcpc, u32 *mask)
{
	int ret;
	u16 data = 0;

	ret = mt6360_i2c_read16(tcpc, TCPC_V10_REG_ALERT_MASK, &data);
	if (ret < 0)
		return ret;
	*mask = data;

	return 0;
}

static int mt6360_get_alert_status_and_mask(struct tcpc_device *tcpc,
					    u32 *alert, u32 *mask)
{
	int ret;
	u8 buf[4] = {0};

	ret = mt6360_i2c_block_read(tcpc, TCPC_V10_REG_ALERT, 4, buf);
	if (ret < 0)
		return ret;
	*alert = le16_to_cpu(*(u16 *)&buf[0]);
	*mask = le16_to_cpu(*(u16 *)&buf[2]);

	return 0;
}

static int mt6360_vbus_change_helper(struct tcpc_device *tcpc)
{
	int ret;
	u8 data;

	ret = mt6360_i2c_read8(tcpc, MT6360_REG_MT_ST1, &data);
	if (ret < 0)
		return ret;
	tcpc->vbus_present = !!(data & MT6360_ST_VBUS_VALID);
	/*
	 * Vsafe0v only triggers when vbus falls under 0.8V,
	 * also update parameter if vbus present triggers
	 */
	tcpc->vbus_safe0v = !!(data & MT6360_ST_VBUS_SAFE0V);
	return 0;
}

static int mt6360_get_power_status(struct tcpc_device *tcpc)
{
	return mt6360_vbus_change_helper(tcpc);
}

static inline int mt6360_get_fault_status(struct tcpc_device *tcpc, u8 *status)
{
	return mt6360_i2c_read8(tcpc, TCPC_V10_REG_FAULT_STATUS, status);
}

static int mt6360_get_cc(struct tcpc_device *tcpc, int *cc1, int *cc2)
{
	bool act_as_sink = false;
	u8 status = 0, role_ctrl = 0, cc_role = 0;
	int ret = 0;

	ret = mt6360_i2c_read8(tcpc, TCPC_V10_REG_CC_STATUS, &status);
	if (ret < 0)
		return ret;

	ret = mt6360_i2c_read8(tcpc, TCPC_V10_REG_ROLE_CTRL, &role_ctrl);
	if (ret < 0)
		return ret;

	if (status & TCPC_V10_REG_CC_STATUS_DRP_TOGGLING) {
		if (role_ctrl & TCPC_V10_REG_ROLE_CTRL_DRP) {
		*cc1 = TYPEC_CC_DRP_TOGGLING;
		*cc2 = TYPEC_CC_DRP_TOGGLING;
		return 0;
	}
		/* Toggle reg0x1A[6] DRP = 1 and = 0 */
		mt6360_i2c_write8(tcpc, TCPC_V10_REG_ROLE_CTRL,
				  role_ctrl | TCPC_V10_REG_ROLE_CTRL_DRP);
		mt6360_i2c_write8(tcpc, TCPC_V10_REG_ROLE_CTRL, role_ctrl);
		return -EAGAIN;
	}
	*cc1 = TCPC_V10_REG_CC_STATUS_CC1(status);
	*cc2 = TCPC_V10_REG_CC_STATUS_CC2(status);

	if (role_ctrl & TCPC_V10_REG_ROLE_CTRL_DRP)
		act_as_sink = TCPC_V10_REG_CC_STATUS_DRP_RESULT(status);
	else {
		if (tcpc->typec_polarity)
			cc_role = TCPC_V10_REG_CC_STATUS_CC2(role_ctrl);
		else
			cc_role = TCPC_V10_REG_CC_STATUS_CC1(role_ctrl);
		act_as_sink = (cc_role != TYPEC_CC_RP);
	}

	/*
	 * If status is not open, then OR in termination to convert to
	 * enum tcpc_cc_voltage_status.
	 */
	if (*cc1 != TYPEC_CC_VOLT_OPEN)
		*cc1 |= (act_as_sink << 2);
	if (*cc2 != TYPEC_CC_VOLT_OPEN)
		*cc2 |= (act_as_sink << 2);
	return 0;
}

static int mt6360_enable_vsafe0v_detect(struct tcpc_device *tcpc, bool en)
{
	return (en ? mt6360_i2c_set_bit : mt6360_i2c_clr_bit)
		(tcpc, MT6360_REG_MT_MASK1, MT6360_M_VBUS_SAFE0V);
}

static int mt6360_set_cc(struct tcpc_device *tcpc, int pull)
{
	int ret = 0;
	u8 data = 0;
	int rp_lvl = TYPEC_CC_PULL_GET_RP_LVL(pull), pull1, pull2;

	MT6360_INFO("%d\n", pull);
	pull = TYPEC_CC_PULL_GET_RES(pull);

#if IS_ENABLED(CONFIG_BATTERY_SAMSUNG)
	if (tcpc->ss_factory) {
		if (pull != TYPEC_CC_RD)
			return 0;
	}
#endif

	if (pull == TYPEC_CC_DRP) {
		data = TCPC_V10_REG_ROLE_CTRL_RES_SET(1, rp_lvl, TYPEC_CC_RD,
						      TYPEC_CC_RD);
		ret = mt6360_i2c_write8(tcpc, TCPC_V10_REG_ROLE_CTRL, data);
		if (ret < 0)
			return ret;
		ret = mt6360_command(tcpc, TCPM_CMD_LOOK_CONNECTION);
	} else {
		pull1 = pull2 = pull;

		if (pull == TYPEC_CC_RP &&
		    tcpc->typec_state == typec_attached_src) {
			if (tcpc->typec_polarity)
				pull1 = TYPEC_CC_RD;
			else
				pull2 = TYPEC_CC_RD;
		}
		data = TCPC_V10_REG_ROLE_CTRL_RES_SET(0, rp_lvl, pull1, pull2);
		ret = mt6360_i2c_write8(tcpc, TCPC_V10_REG_ROLE_CTRL, data);
	}
	return ret;
}

static int mt6360_set_polarity(struct tcpc_device *tcpc, int polarity)
{
	return (polarity ? mt6360_i2c_set_bit : mt6360_i2c_clr_bit)
		(tcpc, TCPC_V10_REG_TCPC_CTRL,
		 TCPC_V10_REG_TCPC_CTRL_PLUG_ORIENT);
}

static int mt6360_is_vconn_fault(struct tcpc_device *tcpc, bool *fault)
{
	int ret;
	u8 status;

	ret = mt6360_i2c_read8(tcpc, MT6360_REG_MT_ST1, &status);
	if (ret < 0)
		return ret;
	if (status & MT6360_ST_VCONN_SHT_GND) {
		*fault = true;
		return 0;
	}

	ret = mt6360_i2c_read8(tcpc, MT6360_REG_MT_ST2, &status);
	if (ret < 0)
		return ret;
	*fault = (status & MT6360_ST_VCONN_FAULT) ? true : false;
	return 0;
}

static int mt6360_set_vconn(struct tcpc_device *tcpc, int en)
{
	int ret;
	bool fault = false;

	MT6360_INFO("%d\n", en);
	/*
	 * Set Vconn OVP RVP
	 * Otherwise vconn present fail will be triggered
	 */
	if (en) {
		mt6360_i2c_set_bit(tcpc, MT6360_REG_VCONN_CTRL2,
				   MT6360_VCONN_OVP_CC_EN);
		mt6360_i2c_set_bit(tcpc, MT6360_REG_VCONN_CTRL3,
				   MT6360_VCONN_RVP_EN);
		usleep_range(20, 50);
		ret = mt6360_is_vconn_fault(tcpc, &fault);
		if (ret >= 0 && fault) {
			MT6360_INFO("Vconn fault\n");
			return -EINVAL;
		}
	}
	ret = (en ? mt6360_i2c_set_bit : mt6360_i2c_clr_bit)
		(tcpc, TCPC_V10_REG_POWER_CTRL, TCPC_V10_REG_POWER_CTRL_VCONN);
	if (!en) {
		mt6360_i2c_clr_bit(tcpc, MT6360_REG_VCONN_CTRL2,
				   MT6360_VCONN_OVP_CC_EN);
		mt6360_i2c_clr_bit(tcpc, MT6360_REG_VCONN_CTRL3,
				   MT6360_VCONN_RVP_EN);
	}
	return ret;
}

static int mt6360_set_low_power_mode(struct tcpc_device *tcpc, bool en,
				     int pull)
{
	int ret = 0;
	u8 data = 0;
#if CONFIG_WATER_DETECTION
	struct mt6360_chip *chip = tcpc_get_dev_data(tcpc);

	if (tcpc->tcpc_flags & TCPC_FLAGS_WATER_DETECTION) {
#if CONFIG_WD_DURING_PLUGGED_IN
		if (en)
			ret = mt6360_enable_usbid_polling(chip, en);
#else
		ret = mt6360_enable_usbid_polling(chip, en);
#endif	/* CONFIG_WD_DURING_PLUGGED_IN */
		if (ret < 0)
			return ret;
	}
#endif /* CONFIG_WATER_DETECTION */
	ret = (en ? mt6360_i2c_clr_bit : mt6360_i2c_set_bit)
		(tcpc, MT6360_REG_MODE_CTRL2, MT6360_AUTOIDLE_EN);
	if (ret < 0)
		return ret;
	ret = mt6360_enable_vsafe0v_detect(tcpc, !en);
	if (ret < 0)
		return ret;
	if (en) {
		data = MT6360_LPWR_EN | MT6360_LPWR_LDO_EN;

#if CONFIG_TYPEC_CAP_NORP_SRC
		data |= MT6360_VBUS_DET_EN | MT6360_PD_BG_EN |
			MT6360_PD_IREF_EN;
#endif	/* CONFIG_TYPEC_CAP_NORP_SRC */
	} else {
		data = MT6360_VBUS_DET_EN | MT6360_PD_BG_EN |
			MT6360_PD_IREF_EN | MT6360_BMCIO_OSC_EN;
	}

#if IS_ENABLED(CONFIG_BATTERY_SAMSUNG)
	if (!tcpc->ss_factory) {
		ret = mt6360_i2c_write8(tcpc, MT6360_REG_MODE_CTRL3, data);
		/* Let CC pins re-toggle */
		if (en && ret >= 0 &&
		    (tcpc->typec_local_cc & TYPEC_CC_DRP))
			ret = mt6360_command(tcpc, TCPM_CMD_LOOK_CONNECTION);
		return ret;
	}

	return 0;
#else
	MT6360_INFO("%s data=%d\n", __func__, data);

	return 0;
#endif
}

static int mt6360_tcpc_deinit(struct tcpc_device *tcpc)
{
	int cc1 = TYPEC_CC_VOLT_OPEN, cc2 = TYPEC_CC_VOLT_OPEN;

	mt6360_get_cc(tcpc, &cc1, &cc2);
	if (cc1 != TYPEC_CC_DRP_TOGGLING &&
	    (cc1 != TYPEC_CC_VOLT_OPEN || cc2 != TYPEC_CC_VOLT_OPEN)) {
	mt6360_set_cc(tcpc, TYPEC_CC_OPEN);
		usleep_range(20000, 30000);
	}

	return 0;
}

static int mt6360_wakeup_irq_handler(struct tcpc_device *tcpc)
{
	return tcpci_alert_wakeup(tcpc);
}

#if CONFIG_WATER_DETECTION
static int mt6360_enable_water_protection(struct mt6360_chip *chip, bool en)
{
	if (en)
		mod_delayed_work(system_freezable_wq, &chip->wd_dwork,
				 msecs_to_jiffies(4000));
	else
		cancel_delayed_work(&chip->wd_dwork);

	return 0;
}

static void mt6360_wd_dwork_handler(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct mt6360_chip *chip = container_of(dwork,
						struct mt6360_chip,
						wd_dwork);
	struct tcpc_device *tcpcs[] = {chip->tcpc};
	int ret = 0;

	MT6360_INFO("\n");

	ret = mt6360_is_water_detected(chip->tcpc);
	if (ret) {
		atomic_set(&chip->wd_protect_rty,
			CONFIG_WD_PROTECT_RETRY_COUNT);
		goto retry;
	}

	if (atomic_dec_and_test(&chip->wd_protect_rty)) {
		tcpc_typec_handle_wd(tcpcs, ARRAY_SIZE(tcpcs), false);
		atomic_set(&chip->wd_protect_rty,
			   CONFIG_WD_PROTECT_RETRY_COUNT);
		return ;
	}
	MT6360_INFO("rty %d\n", atomic_read(&chip->wd_protect_rty));
retry:
	mt6360_enable_water_protection(chip, true);
}
#endif /* CONFIG_WATER_DETECTION */

#if CONFIG_CABLE_TYPE_DETECTION
static inline int mt6360_get_cable_type(struct tcpc_device *tcpc,
					enum tcpc_cable_type *type)
{
	int ret;
	u8 status;

	ret = mt6360_i2c_read8(tcpc, MT6360_REG_MT_ST3, &status);
	if (ret < 0)
		return ret;
	*type = (status & MT6360_ST_CABLE_TYPE) ?
		TCPC_CABLE_TYPE_A2C : TCPC_CABLE_TYPE_C2C;
	return 0;
}

static int mt6360_ctd_irq_handler(struct tcpc_device *tcpc)
{
	int ret = 0;
	enum tcpc_cable_type cable_type = TCPC_CABLE_TYPE_NONE;

	ret = mt6360_get_cable_type(tcpc, &cable_type);
	if (ret < 0)
		return ret;
	if (cable_type == TCPC_CABLE_TYPE_C2C &&
			tcpci_check_vbus_valid(tcpc))
		cable_type = TCPC_CABLE_TYPE_A2C;
	return tcpc_typec_handle_ctd(tcpc, cable_type);
}
#endif /* CONFIG_CABLE_TYPE_DETECTION */

static int mt6360_get_cc_hi(struct tcpc_device *tcpc)
{
	int ret = 0;
	u8 data = 0;

	ret = mt6360_i2c_read8(tcpc, MT6360_REG_MT_ST5, &data);
	if (ret < 0)
		return ret;
	return ((data ^ MT6360_ST_HIDET_CC) & MT6360_ST_HIDET_CC)
		>> (ffs(MT6360_ST_HIDET_CC1) - 1);
}

static int mt6360_set_cc_hi_state(struct tcpc_device *tcpc, int val)
{
	struct mt6360_chip *chip = tcpc_get_dev_data(tcpc);

	if(tcpc->cc_hi_state == val)
		return 0;

	tcpc->cc_hi_state = val;
	tcpc->hiccup_mode = muic_get_hiccup_mode();

	MT6360_INFO("%s[cc_hi:%d/hiccup:%d]\n", __func__,
		tcpc->cc_hi_state, tcpc->hiccup_mode);

	if (tcpc->cc_hi_state && !tcpc->hiccup_mode)
		schedule_delayed_work(&chip->cc_hi_check_work,
			msecs_to_jiffies(100));

	return 0;
}

static void mt6360_cc_hi_check_work_func(struct work_struct *work)
{
	struct mt6360_chip *chip = container_of(work,
		struct mt6360_chip, cc_hi_check_work.work);
	struct tcpc_device *tcpc = chip->tcpc;

	tcpc->hiccup_mode = muic_get_hiccup_mode();

	MT6360_INFO("%s[cc_hi:%d/hiccup:%d]\n", __func__,
		tcpc->cc_hi_state, tcpc->hiccup_mode);

	if (!tcpc->water_state)
		return;

	if (tcpc->cc_hi_state && !tcpc->hiccup_mode)
		muic_set_hiccup_mode(true);
}

static int mt6360_hidet_cc_evt_process(struct tcpc_device *tcpc)
{
	int ret = 0;

	ret = mt6360_get_cc_hi(tcpc);

	MT6360_INFO("%s = %d\n", __func__, ret);

	vt_muic_set_water_state(CC_HIGH_STATE, !!ret);

	if (ret < 0)
		return ret;
	mt6360_set_cc_hi_state(tcpc, ret);
	return tcpci_notify_cc_hi(tcpc, ret);
}

static int mt6360_hidet_cc_irq_handler(struct tcpc_device *tcpc)
{
	return mt6360_hidet_cc_evt_process(tcpc);
}

static inline int mt6360_vconn_fault_evt_process(struct tcpc_device *tcpc)
{
	int ret;
	bool fault = false;

	ret = mt6360_is_vconn_fault(tcpc, &fault);
	if (ret >= 0 && fault) {
		MT6360_INFO("\n");
		mt6360_i2c_clr_bit(tcpc, MT6360_REG_VCONN_CTRL2,
				   MT6360_VCONN_OVP_CC_EN);
		mt6360_i2c_clr_bit(tcpc, MT6360_REG_VCONN_CTRL3,
				   MT6360_VCONN_RVP_EN);
	}
	return 0;
}

static int mt6360_vconn_irq_handler(struct tcpc_device *tcpc)
{
	return mt6360_vconn_fault_evt_process(tcpc);
}

static int mt6360_vbus_irq_handler(struct tcpc_device *tcpc)
{
	int ret = mt6360_vbus_change_helper(tcpc);

	if (ret < 0)
		return ret;

	MT6360_INFO("%s = %d\n", __func__, !!tcpc->vbus_present);

#if IS_ENABLED(CONFIG_VBUS_NOTIFIER)
#ifdef CONFIG_WATER_DETECTION
#if IS_ENABLED(CONFIG_HICCUP_CHARGER)
	MT6360_INFO("%s water %d\n", __func__, !!tcpc->water_state);
	if (tcpc->water_state && !!tcpc->vbus_present)
		muic_set_hiccup_mode(true);
#endif /* CONFIG_HICCUP_CHARGER */
#endif

	vbus_notifier_handle(tcpc->vbus_present ? STATUS_VBUS_HIGH : STATUS_VBUS_LOW);
#endif

	return ret;
}

struct irq_mapping_tbl {
	u8 num;
	s8 grp;
	int (*hdlr)(struct tcpc_device *tcpc);
};

#define MT6360_IRQ_MAPPING(_num, _grp, _name) \
	{ .num = _num, .grp = _grp, .hdlr = mt6360_##_name##_irq_handler }

static struct irq_mapping_tbl mt6360_vend_irq_mapping_tbl[] = {
	MT6360_IRQ_MAPPING(0, -1, wakeup),
#if CONFIG_CABLE_TYPE_DETECTION
	MT6360_IRQ_MAPPING(20, -1, ctd),
#endif /* CONFIG_CABLE_TYPE_DETECTION */
	MT6360_IRQ_MAPPING(36, 0, hidet_cc),	/* hidet_cc1 */
	MT6360_IRQ_MAPPING(37, 0, hidet_cc),	/* hidet_cc2 */
	MT6360_IRQ_MAPPING(3, 1, vconn),	/* vconn_shtgnd */
	MT6360_IRQ_MAPPING(8, 1, vconn),	/* vconn_ov_cc1 */
	MT6360_IRQ_MAPPING(9, 1, vconn),	/* vconn_ov_cc2 */
	MT6360_IRQ_MAPPING(10, 1, vconn),	/* vconn_ocr */
	MT6360_IRQ_MAPPING(12, 1, vconn),	/* vconn_invalid */

	MT6360_IRQ_MAPPING(1, 2, vbus),		/* vsafe0V */
	MT6360_IRQ_MAPPING(5, 2, vbus),		/* vbus_valid */
};

static int mt6360_alert_vendor_defined_handler(struct tcpc_device *tcpc)
{
	u8 irqnum = 0, irqbit = 0;
	u8 buf[MT6360_VEND_INT_MAX * 2];
	u8 *mask = &buf[0];
	u8 *alert = &buf[MT6360_VEND_INT_MAX];
	s8 grp = 0;
	unsigned long handled_bitmap = 0;
	int ret = 0, i = 0;

	ret = mt6360_i2c_block_read(tcpc, MT6360_REG_MT_MASK1,
				    sizeof(buf), buf);
	if (ret < 0)
		return ret;

	for (i = 0; i < MT6360_VEND_INT_MAX; i++) {
		if (!(alert[i] & mask[i]))
			continue;
		MT6360_INFO("vend_alert[%d]=alert,mask(0x%02X,0x%02X)\n",
			    i + 1, alert[i], mask[i]);
		alert[i] &= mask[i];
	}

	mt6360_vend_alert_status_clear(tcpc, alert);

	for (i = 0; i < ARRAY_SIZE(mt6360_vend_irq_mapping_tbl); i++) {
		irqnum = mt6360_vend_irq_mapping_tbl[i].num / 8;
		if (irqnum >= MT6360_VEND_INT_MAX)
			continue;
		irqbit = mt6360_vend_irq_mapping_tbl[i].num % 8;
		if (alert[irqnum] & BIT(irqbit)) {
			grp = mt6360_vend_irq_mapping_tbl[i].grp;
			if (grp >= 0) {
				ret = test_and_set_bit(grp, &handled_bitmap);
				if (ret)
					continue;
			}
			mt6360_vend_irq_mapping_tbl[i].hdlr(tcpc);
		}
	}
	return 0;
}

static int mt6360_set_auto_dischg_discnt(struct tcpc_device *tcpc, bool en)
{
	int ret = 0;
	u8 data = 0;

	MT6360_INFO("en = %d\n", en);
	if (en) {
		ret = mt6360_i2c_read8(tcpc, TCPC_V10_REG_POWER_CTRL, &data);
		if (ret < 0)
			return ret;
		data &= ~TCPC_V10_REG_VBUS_MONITOR;
		ret = mt6360_i2c_write8(tcpc, TCPC_V10_REG_POWER_CTRL, data);
		if (ret < 0)
			return ret;
		data |= TCPC_V10_REG_AUTO_DISCHG_DISCNT;
		return mt6360_i2c_write8(tcpc, TCPC_V10_REG_POWER_CTRL, data);
	}
	return mt6360_i2c_update_bits(tcpc, TCPC_V10_REG_POWER_CTRL,
				      TCPC_V10_REG_VBUS_MONITOR |
				      TCPC_V10_REG_AUTO_DISCHG_DISCNT,
				      TCPC_V10_REG_VBUS_MONITOR);
}

static int mt6360_set_cc_hidet(struct tcpc_device *tcpc, bool en)
{
	int ret;

	if (en)
		mt6360_enable_auto_rpconnect(tcpc, false);

	ret = (en ? mt6360_i2c_set_bit : mt6360_i2c_clr_bit)
		(tcpc, MT6360_REG_HILO_CTRL5, MT6360_CMPEN_HIDET_CC);
	if (ret < 0)
		return ret;
	ret = (en ? mt6360_i2c_set_bit : mt6360_i2c_clr_bit)
		(tcpc, MT6360_REG_MT_MASK5, MT6360_M_HIDET_CC);
	if (ret < 0)
		return ret;
	if (!en)
		mt6360_enable_auto_rpconnect(tcpc, true);
	return ret;
}

#if CONFIG_WATER_DETECTION
static inline int mt6360_get_usbid_adc(struct tcpc_device *tcpc, int *usbid)
{
	int ret;
	struct mt6360_chip *chip = tcpc_get_dev_data(tcpc);

	ret = charger_dev_get_adc(chip->chgdev, ADC_CHANNEL_USBID,
				  usbid, usbid);
	if (ret < 0) {
		dev_err(chip->dev, "%s fail(%d)\n", __func__, ret);
		return ret;
	}
	/* To mV */
	*usbid /= 1000;
	MT6360_INFO("%dmV\n", *usbid);
	return 0;
}

static inline bool mt6360_is_audio_device(struct tcpc_device *tcpc, int usbid)
{
	int ret;
	struct mt6360_chip *chip = tcpc_get_dev_data(tcpc);

	if (usbid >= CONFIG_WD_SBU_PH_AUDDEV)
		return false;

	/* Pull high with 1K resistor */
	ret = charger_dev_set_usbid_rup(chip->chgdev, 1000);
	if (ret < 0) {
		dev_err(chip->dev, "%s usbid rup1k fail\n", __func__);
		goto not_auddev;
	}

	msleep(100);

	ret = mt6360_get_usbid_adc(tcpc, &usbid);
	if (ret < 0) {
		dev_err(chip->dev, "%s get usbid adc fail\n", __func__);
		goto not_auddev;
	}

	if (usbid >= CONFIG_WD_SBU_AUD_UBOUND)
		goto not_auddev;

	return true;
not_auddev:
	charger_dev_set_usbid_rup(chip->chgdev, 500000);
	return false;
}

static inline int mt6360_get_usbid_adc_wa(struct tcpc_device *tcpc, int *usbid)
{
	int i = 0, ret = 0;

	for (i = 0; i < 3; i++) {
		mt6360_get_usbid_adc(tcpc, usbid);
		msleep(20);
	}

	ret = mt6360_get_usbid_adc(tcpc, usbid);

	return ret;
}

static int mt6360_is_water_detected(struct tcpc_device *tcpc)
{
	int ret, usbid, i;
	u32 lb = CONFIG_WD_SBU_PH_LBOUND;
	u32 ub = CONFIG_WD_SBU_CALIB_INIT * 110 / 100;
	struct mt6360_chip *chip = tcpc_get_dev_data(tcpc);
#if CONFIG_CABLE_TYPE_DETECTION
	u8 ctd_evt;
	enum tcpc_cable_type cable_type;
#endif /* CONFIG_CABLE_TYPE_DETECTION */

	ret = charger_dev_enable_usbid(chip->chgdev, false);
	if (ret < 0) {
		dev_err(chip->dev, "%s pull low usbid fail\n", __func__);
		goto err;
	}

	ret = charger_dev_enable_usbid_floating(chip->chgdev, false);
	if (ret < 0) {
		dev_notice(chip->dev, "%s disable usbid float fail\n",
				      __func__);
	}

	for (i = 0; i < CONFIG_WD_SBU_PL_RETRY; i++) {
		msleep(50);
		ret = mt6360_get_usbid_adc_wa(tcpc, &usbid);
		if (ret < 0) {
			dev_notice(chip->dev, "%s get usbid adc fail\n",
				   __func__);
			goto err;
		}
		MT6360_INFO("pl usbid %dmV\n", usbid);
		if (usbid <= CONFIG_WD_SBU_PL_BOUND)
			break;
	}
	if (i >= CONFIG_WD_SBU_PL_RETRY) {
		ret = 1;
		goto out;
	}

	ret = charger_dev_enable_usbid_floating(chip->chgdev, true);
	if (ret < 0) {
		dev_notice(chip->dev, "%s enable usbid float fail\n",
				      __func__);
		goto err;
	}

	/* Pull high usb idpin */
	ret = charger_dev_set_usbid_src_ton(chip->chgdev, 0);
	if (ret < 0) {
		dev_err(chip->dev, "%s usbid always src on fail\n", __func__);
		goto err;
	}

	ret = charger_dev_set_usbid_rup(chip->chgdev, 500000);
	if (ret < 0) {
		dev_err(chip->dev, "%s usbid rup500k fail\n", __func__);
		goto err;
	}

	ret = charger_dev_enable_usbid(chip->chgdev, true);
	if (ret < 0) {
		dev_err(chip->dev, "%s usbid pulled high fail\n", __func__);
		goto err;
	}

	for (i = 0; i < CONFIG_WD_SBU_PH_RETRY; i++) {
		msleep(20);
		ret = mt6360_get_usbid_adc_wa(tcpc, &usbid);
		if (ret < 0) {
			dev_notice(chip->dev, "%s get usbid adc fail\n", __func__);
			goto err;
		}
		MT6360_INFO("lb %d, ub %d, ph usbid %dmV\n", lb, ub, usbid);
		if (usbid >= lb && usbid <= ub) {
			ret = 0;
			goto out;
		}
	}

#if CONFIG_CABLE_TYPE_DETECTION
	cable_type = tcpc->typec_cable_type;
	if (cable_type == TCPC_CABLE_TYPE_NONE) {
		ret = mt6360_i2c_read8(chip->tcpc, MT6360_REG_MT_INT3,
				       &ctd_evt);
		if (ret >= 0 && (ctd_evt & MT6360_M_CTD))
			mt6360_get_cable_type(tcpc, &cable_type);
	}
	if (cable_type == TCPC_CABLE_TYPE_C2C) {
		if (((usbid >= CONFIG_WD_SBU_PH_LBOUND1_C2C) &&
		    (usbid <= CONFIG_WD_SBU_PH_UBOUND1_C2C)) ||
		    (usbid > CONFIG_WD_SBU_PH_UBOUND2_C2C)) {
			MT6360_INFO("ignore for C2C\n");
			ret = 0;
			goto out;
		}
	}
#endif /* CONFIG_CABLE_TYPE_DETECTION */

	if (mt6360_is_audio_device(tcpc, usbid)) {
		ret = 0;
		MT6360_INFO("%s audio dev but not water\n", __func__);
		goto out;
	}
	ret = 1;
out:
	MT6360_INFO("%s %s water\n", __func__, ret ? "with" : "without");

#if IS_ENABLED(CONFIG_HICCUP_CHARGER)
	mt6360_set_cc_hi_state(tcpc, mt6360_get_cc_hi(tcpc));
#endif /* CONFIG_HICCUP_CHARGER */
err:
	charger_dev_enable_usbid_floating(chip->chgdev, true);
	charger_dev_enable_usbid(chip->chgdev, false);
	return ret;
}

static int mt6360_set_water_protection(struct tcpc_device *tcpc, bool en)
{
	int ret = 0;
	struct mt6360_chip *chip = tcpc_get_dev_data(tcpc);

	ret = mt6360_enable_water_protection(chip, en);
#if CONFIG_WD_DURING_PLUGGED_IN
	if (!en && ret >= 0)
		ret = mt6360_enable_usbid_polling(chip, true);
#endif	/* CONFIG_WD_DURING_PLUGGED_IN */
	return ret;
}
#endif /* CONFIG_WATER_DETECTION */

#if CONFIG_TYPEC_CAP_FORCE_DISCHARGE
static int mt6360_set_force_discharge(struct tcpc_device *tcpc, bool en, int mv)
{
	return (en ? mt6360_i2c_set_bit : mt6360_i2c_clr_bit)
		(tcpc, TCPC_V10_REG_POWER_CTRL, TCPC_V10_REG_FORCE_DISC_EN);
}
#endif	/* CONFIG_TYPEC_CAP_FORCE_DISCHARGE */

static int mt6360_tcpc_init(struct tcpc_device *tcpc, bool sw_reset)
{
	int ret;
#if CONFIG_WATER_DETECTION
	struct mt6360_chip *chip = tcpc_get_dev_data(tcpc);
#endif /* CONFIG_WATER_DETECTION */

	MT6360_INFO("\n");

	if (sw_reset) {
		ret = mt6360_software_reset(tcpc);
		if (ret < 0)
			return ret;
	}

#if CONFIG_TCPC_I2CRST_EN
	mt6360_i2c_write8(tcpc, MT6360_REG_I2CRST_CTRL,
			  MT6360_REG_I2CRST_SET(true, 0x0f));
#endif	/* CONFIG_TCPC_I2CRST_EN */

	/* UFP Both RD setting */
	/* DRP = 0, RpVal = 0 (Default), Rd, Rd */
	mt6360_i2c_write8(tcpc, TCPC_V10_REG_ROLE_CTRL,
			  TCPC_V10_REG_ROLE_CTRL_RES_SET(0, 0, CC_RD, CC_RD));

	/*
	 * CC Detect Debounce : 25*val us
	 * Transition window count : spec 12~20us, based on 2.4MHz
	 */
	mt6360_i2c_write8(tcpc, MT6360_REG_DEBOUNCE_CTRL1, 20);
	mt6360_init_drp_duty(tcpc);

	/* Disable BLEED_DISC and Enable AUTO_DISC_DISCNCT */
	mt6360_i2c_write8(tcpc, TCPC_V10_REG_POWER_CTRL, 0x70);

	/* RX/TX Clock Gating (Auto Mode) */
	if (!sw_reset)
		mt6360_set_clock_gating(tcpc, true);

	mt6360_init_phy_ctrl(tcpc);

	/* Vconn OC */
	mt6360_i2c_write8(tcpc, MT6360_REG_VCONN_CTRL1, 0x41);

	/* VBUS_VALID debounce time: 375us */
	mt6360_i2c_write8(tcpc, MT6360_REG_DEBOUNCE_CTRL4, 0x0F);

	/* Set HILOCCFILTER 250us */
	mt6360_i2c_write8(tcpc, MT6360_REG_VBUS_CTRL2, 0xAA);

	/* Set cc open when PMIC sends Vsys UV signal */
	mt6360_i2c_set_bit(tcpc, MT6360_REG_RX_CTRL2, MT6360_OPEN400MS_EN);

	/* Enable LOOK4CONNECTION alert */
	mt6360_i2c_set_bit(tcpc, TCPC_V10_REG_TCPC_CTRL,
			   TCPC_V10_REG_TCPC_CTRL_EN_LOOK4CONNECTION_ALERT);

	mt6360_init_alert_mask(tcpc);

	/* SHIPPING off, AUTOIDLE enable, TIMEOUT = 6.4ms */
	mt6360_i2c_write8(tcpc, MT6360_REG_MODE_CTRL2,
			  MT6360_REG_MODE_CTRL2_SET(1, 1, 0));
	mdelay(1);

#if CONFIG_WATER_DETECTION
	if (tcpc->tcpc_flags & TCPC_FLAGS_WATER_DETECTION)
		mt6360_enable_usbid_polling(chip, true);
#endif /* CONFIG_WATER_DETECTION */

	return 0;
}

#if IS_ENABLED(CONFIG_USB_POWER_DELIVERY)
static int mt6360_set_msg_header(
	struct tcpc_device *tcpc, u8 power_role, u8 data_role)
{
	u8 msg_hdr = TCPC_V10_REG_MSG_HDR_INFO_SET(data_role, power_role);

	return mt6360_i2c_write8(tcpc, TCPC_V10_REG_MSG_HDR_INFO, msg_hdr);
}

static int mt6360_protocol_reset(struct tcpc_device *tcpc)
{
	int ret = 0;
	u8 phy_ctrl8 = 0;

	ret = mt6360_i2c_read8(tcpc, MT6360_REG_PHY_CTRL8, &phy_ctrl8);
	if (ret < 0)
		return ret;
	ret = mt6360_i2c_write8(tcpc, MT6360_REG_PHY_CTRL8,
				phy_ctrl8 & ~MT6360_PRL_FSM_RSTB);
	if (ret < 0)
		return ret;
	udelay(20);
	return mt6360_i2c_write8(tcpc, MT6360_REG_PHY_CTRL8,
				 phy_ctrl8 | MT6360_PRL_FSM_RSTB);
}

static int mt6360_set_rx_enable(struct tcpc_device *tcpc, u8 en)
{
	int ret = 0;

	if (en)
		ret = mt6360_set_clock_gating(tcpc, false);

	if (ret == 0)
		ret = mt6360_i2c_write8(tcpc, TCPC_V10_REG_RX_DETECT, en);

	if ((ret == 0) && !en) {
		mt6360_protocol_reset(tcpc);
		ret = mt6360_set_clock_gating(tcpc, true);
	}

	return ret;
}

static int mt6360_get_message(struct tcpc_device *tcpc, u32 *payload,
			      u16 *msg_head,
			      enum tcpm_transmit_type *frame_type)
{
	int ret = 0;
	u8 cnt = 0, buf[32];

	ret = mt6360_i2c_block_read(tcpc, TCPC_V10_REG_RX_BYTE_CNT,
				    4, buf);
	if (ret < 0)
		return ret;

	cnt = buf[0];
	*frame_type = buf[1];
	*msg_head = le16_to_cpu(*(u16 *)&buf[2]);

	if (cnt <= 3)
		return ret;

	cnt -= 3; /* FRAME_TYPE + HEADER */
	if (cnt > sizeof(buf) - 4)
		cnt = sizeof(buf) - 4;
	ret = mt6360_i2c_block_read(tcpc, TCPC_V10_REG_RX_DATA,
				    cnt, buf + 4);
	if (ret < 0)
		return ret;
	memcpy(payload, buf + 4, cnt);

	return ret;
}

/* message header (2byte) + data object (7*4) */
#define MT6360_TRANSMIT_MAX_SIZE \
	(sizeof(u16) + sizeof(u32) * 7)

#if CONFIG_USB_PD_RETRY_CRC_DISCARD
static int mt6360_retransmit(struct tcpc_device *tcpc)
{
	return mt6360_i2c_write8(tcpc, TCPC_V10_REG_TRANSMIT,
				 TCPC_V10_REG_TRANSMIT_SET(tcpc->pd_retry_count,
				 TCPC_TX_SOP));
}
#endif /* CONFIG_USB_PD_RETRY_CRC_DISCARD */

static int mt6360_transmit(struct tcpc_device *tcpc,
			   enum tcpm_transmit_type type, u16 header,
			   const u32 *data)
{
	int ret, data_cnt, packet_cnt;
	u8 temp[MT6360_TRANSMIT_MAX_SIZE + 1];

	if (type < TCPC_TX_HARD_RESET) {
		data_cnt = sizeof(u32) * PD_HEADER_CNT(header);
		packet_cnt = data_cnt + sizeof(u16);

		temp[0] = packet_cnt;
		memcpy(temp + 1, &header, 2);
		if (data_cnt > 0)
			memcpy(temp + 3, data, data_cnt);

		ret = mt6360_i2c_block_write(tcpc, TCPC_V10_REG_TX_BYTE_CNT,
					     packet_cnt + 1, temp);
		if (ret < 0)
			return ret;
	}

	return mt6360_i2c_write8(tcpc, TCPC_V10_REG_TRANSMIT,
				TCPC_V10_REG_TRANSMIT_SET(tcpc->pd_retry_count,
				type));
}

static int mt6360_set_bist_test_mode(struct tcpc_device *tcpc, bool en)
{
	return (en ? mt6360_i2c_set_bit : mt6360_i2c_clr_bit)
		(tcpc, TCPC_V10_REG_TCPC_CTRL,
		 TCPC_V10_REG_TCPC_CTRL_BIST_TEST_MODE);
}
#endif /* CONFIG_USB_POWER_DELIVERY */

#if IS_ENABLED(CONFIG_BATTERY_SAMSUNG)
static int mt6360_ss_factory(struct tcpc_device *tcpc)
{
	int ret;

	MT6360_INFO("ss_factory = %d\n", tcpc->ss_factory);

	if (tcpc->ss_factory) {
		ret = mt6360_i2c_set_bit(tcpc, MT6360_REG_MODE_CTRL3,
							MT6360_LPWR_EN);
		ret |= mt6360_set_cc(tcpc, TYPEC_CC_RD);
		ret |= mt6360_i2c_clr_bit(tcpc, MT6360_REG_WD_DET_CTRL7,
							MT6360_DRP_AUTO_EN);
		ret |= mt6360_i2c_update_bits(tcpc, MT6360_REG_CC_CTRL5,
					   0x50, MT6360_MASK_LPWR_RPRD_CC2_CC1);
	} else {
		ret = mt6360_i2c_clr_bit(tcpc, MT6360_REG_MODE_CTRL3,
							MT6360_LPWR_EN);
		ret |= mt6360_i2c_set_bit(tcpc, MT6360_REG_WD_DET_CTRL7,
							MT6360_DRP_AUTO_EN);
		ret |= mt6360_i2c_update_bits(tcpc, MT6360_REG_CC_CTRL5,
					   0x50, MT6360_MASK_LPWR_RPRD_CC2_CC1);
		ret |= tcpm_typec_error_recovery(tcpc);
	}

	return ret;
}
#endif

static struct tcpc_ops mt6360_tcpc_ops = {
	.init = mt6360_tcpc_init,
	.init_alert_mask = mt6360_init_alert_mask,
	.alert_status_clear = mt6360_alert_status_clear,
	.fault_status_clear = mt6360_fault_status_clear,
	.set_alert_mask = mt6360_set_alert_mask,
	.get_alert_mask = mt6360_get_alert_mask,
	.get_alert_status_and_mask = mt6360_get_alert_status_and_mask,
	.get_power_status = mt6360_get_power_status,
	.get_fault_status = mt6360_get_fault_status,
	.get_cc = mt6360_get_cc,
	.set_cc = mt6360_set_cc,
	.set_polarity = mt6360_set_polarity,
	.set_vconn = mt6360_set_vconn,
	.deinit = mt6360_tcpc_deinit,
	.alert_vendor_defined_handler = mt6360_alert_vendor_defined_handler,
	.set_auto_dischg_discnt = mt6360_set_auto_dischg_discnt,

	.set_low_power_mode = mt6360_set_low_power_mode,

#if IS_ENABLED(CONFIG_USB_POWER_DELIVERY)
	.set_msg_header = mt6360_set_msg_header,
	.set_rx_enable = mt6360_set_rx_enable,
	.protocol_reset = mt6360_protocol_reset,
	.get_message = mt6360_get_message,
	.transmit = mt6360_transmit,
	.set_bist_test_mode = mt6360_set_bist_test_mode,
#endif	/* CONFIG_USB_POWER_DELIVERY */

#if CONFIG_USB_PD_RETRY_CRC_DISCARD
	.retransmit = mt6360_retransmit,
#endif	/* CONFIG_USB_PD_RETRY_CRC_DISCARD */

	.set_cc_hidet = mt6360_set_cc_hidet,
	.get_cc_hi = mt6360_get_cc_hi,

#if CONFIG_WATER_DETECTION
	.set_water_protection = mt6360_set_water_protection,
#endif /* CONFIG_WATER_DETECTION */

#if CONFIG_TYPEC_CAP_FORCE_DISCHARGE
	.set_force_discharge = mt6360_set_force_discharge,
#endif	/* CONFIG_TYPEC_CAP_FORCE_DISCHARGE */
#if IS_ENABLED(CONFIG_BATTERY_SAMSUNG)
	.ss_factory = mt6360_ss_factory,
#endif
};

static int mt6360_parse_dt(struct mt6360_chip *chip, struct device *dev,
			   struct mt6360_tcpc_platform_data *pdata)
{
	struct device_node *np = dev->of_node;
	struct resource *res;
	int res_cnt = 0, ret;
	struct of_phandle_args irq;

	pr_info("%s\n", __func__);
#if !IS_ENABLED(CONFIG_MTK_GPIO) || IS_ENABLED(CONFIG_MTK_GPIOLIB_STAND)
	ret = of_get_named_gpio(np, "mt6360pd,intr-gpio", 0);
	if (ret < 0)
		ret = of_get_named_gpio(np, "mt6360pd,intr_gpio", 0);

	if (ret < 0) {
		dev_err(dev, "%s no intr_gpio info(gpiolib)\n", __func__);
		return ret;
	}
	chip->irq_gpio = ret;
#else
	ret = of_property_read_u32(np, "mt6360pd,intr-gpio-num", &chip->irq_gpio) ?
	      of_property_read_u32(np, "mt6360pd,intr_gpio_num", &chip->irq_gpio) : 0;
	if (ret < 0) {
		dev_err(dev, "%s no intr_gpio info\n", __func__);
		return ret;
	}
#endif /* !CONFIG_MTK_GPIO || CONFIG_MTK_GPIOLIB_STAND */

	while (of_irq_parse_one(np, res_cnt, &irq) == 0)
		res_cnt++;
	if (!res_cnt) {
		dev_info(dev, "%s no irqs specified\n", __func__);
		return 0;
	}
	res = devm_kzalloc(dev,  res_cnt * sizeof(*res), GFP_KERNEL);
	if (!res)
		return -ENOMEM;
	ret = of_irq_to_resource_table(np, res, res_cnt);
	pdata->irq_res = res;
	pdata->irq_res_cnt = ret;
	return 0;
}

static int mt6360_tcpcdev_init(struct mt6360_chip *chip, struct device *dev)
{
	struct tcpc_desc *desc;
	struct tcpc_device *tcpc = NULL;
	struct device_node *np = dev->of_node;
	u32 val, len;
	const char *name = "default";

	desc = devm_kzalloc(dev, sizeof(*desc), GFP_KERNEL);
	if (!desc)
		return -ENOMEM;
	if (of_property_read_u32(np, "mt-tcpc,role-def", &val) >= 0 ||
	    of_property_read_u32(np, "mt-tcpc,role_def", &val) >= 0) {
		if (val >= TYPEC_ROLE_NR)
			desc->role_def = TYPEC_ROLE_DRP;
		else
			desc->role_def = val;
	} else {
		dev_info(dev, "%s use default Role DRP\n", __func__);
		desc->role_def = TYPEC_ROLE_DRP;
	}

	if (of_property_read_u32(np, "mt-tcpc,rp-level", &val) >= 0 ||
	    of_property_read_u32(np, "mt-tcpc,rp_level", &val) >= 0) {
		switch (val) {
		case TYPEC_RP_DFT:
		case TYPEC_RP_1_5:
		case TYPEC_RP_3_0:
			desc->rp_lvl = val;
			break;
		default:
			desc->rp_lvl = TYPEC_RP_DFT;
			break;
		}
	}

	if (of_property_read_u32(np, "mt-tcpc,vconn-supply", &val) >= 0 ||
	    of_property_read_u32(np, "mt-tcpc,vconn_supply", &val) >= 0) {
		if (val >= TCPC_VCONN_SUPPLY_NR)
			desc->vconn_supply = TCPC_VCONN_SUPPLY_ALWAYS;
		else
			desc->vconn_supply = val;
	} else {
		dev_info(dev, "%s use default VconnSupply\n", __func__);
		desc->vconn_supply = TCPC_VCONN_SUPPLY_ALWAYS;
	}

	if (of_property_read_string(np, "mt-tcpc,name", &name) < 0)
		dev_info(dev, "use default name\n");
	len = strlen(name);
	desc->name = devm_kzalloc(dev, len + 1, GFP_KERNEL);
	if (!desc->name)
		return -ENOMEM;
	strlcpy((char *)desc->name, name, len + 1);

	chip->tcpc_desc = desc;
	tcpc = tcpc_device_register(dev, desc, &mt6360_tcpc_ops, chip);
	if (IS_ERR_OR_NULL(tcpc))
		return -EINVAL;
	chip->tcpc = tcpc;

#if CONFIG_USB_PD_DISABLE_PE
	tcpc->disable_pe = of_property_read_bool(np, "mt-tcpc,disable-pe") ||
				 of_property_read_bool(np, "mt-tcpc,disable_pe");
#endif	/* CONFIG_USB_PD_DISABLE_PE */

	/* Init tcpc_flags */
#if CONFIG_USB_PD_RETRY_CRC_DISCARD
	tcpc->tcpc_flags |= TCPC_FLAGS_RETRY_CRC_DISCARD;
#endif	/* CONFIG_USB_PD_RETRY_CRC_DISCARD */
#if CONFIG_USB_PD_REV30
	tcpc->tcpc_flags |= TCPC_FLAGS_PD_REV30;
#endif	/* CONFIG_USB_PD_REV30 */

	if (tcpc->tcpc_flags & TCPC_FLAGS_PD_REV30)
		dev_info(dev, "%s PD REV30\n", __func__);
	else
		dev_info(dev, "%s PD REV20\n", __func__);

#if CONFIG_WATER_DETECTION
	tcpc->tcpc_flags |= TCPC_FLAGS_WATER_DETECTION;
#endif /* CONFIG_WATER_DETECTION */
	tcpc->tcpc_flags |= TCPC_FLAGS_CABLE_TYPE_DETECTION;

	return 0;
}

static inline int mt6360_check_revision(struct i2c_client *client)
{
	u16 data = 0;
	int ret = 0;

	ret = i2c_smbus_read_i2c_block_data(client, TCPC_V10_REG_VID, 2,
					    (u8 *)&data);
	if (ret < 0) {
		dev_notice(&client->dev, "read Vendor ID fail(%d)\n", ret);
		return ret;
	}
	data = le16_to_cpu(data);
	if (data != MEDIATEK_6360_VID) {
		dev_info(&client->dev, "%s failed, VID=0x%04x\n",
				       __func__, data);
		return -ENODEV;
	}

	ret = i2c_smbus_read_i2c_block_data(client, TCPC_V10_REG_PID, 2,
					    (u8 *)&data);
	if (ret < 0) {
		dev_notice(&client->dev, "read Product ID fail(%d)\n", ret);
		return ret;
	}
	data = le16_to_cpu(data);
	if (data != MEDIATEK_6360_PID) {
		dev_info(&client->dev, "%s failed, PID=0x%04x\n",
				       __func__, data);
		return -ENODEV;
	}

	ret = i2c_smbus_read_i2c_block_data(client, TCPC_V10_REG_DID, 2,
					    (u8 *)&data);
	if (ret < 0) {
		dev_notice(&client->dev, "read Device ID fail(%d)\n", ret);
		return ret;
	}
	return le16_to_cpu(data);
}

#if IS_ENABLED(CONFIG_PDIC_NOTIFIER)
static int test_init;
#define SBU_UCT300_LOW	1690 /* mV */
#define SBU_UCT300_PASS	1400 /* mV */
int mt6360_usbid_check(void)
{
	struct charger_device *chgdev = get_charger_by_name("primary_chg");
	int usbid = 0, ret = 0, result = 1;

	ret = charger_dev_enable_usbid_floating(chgdev, true);
	if (ret < 0)
		pr_err("%s enable usbid float fail\n", __func__);

	/* Pull high usb idpin */
	ret = charger_dev_set_usbid_src_ton(chgdev, 0);
	if (ret < 0)
		pr_err("%s usbid always src on fail\n", __func__);

	ret = charger_dev_set_usbid_rup(chgdev, 75000);
	if (ret < 0)
		pr_err("%s usbid rup500k fail\n", __func__);

	ret = charger_dev_enable_usbid(chgdev, true);
	if (ret < 0)
		pr_err("%s usbid pulled high fail\n", __func__);

	ret = charger_dev_get_adc(chgdev, ADC_CHANNEL_USBID,
			&usbid, &usbid);
	if (ret < 0)
		pr_err("%s failed to get adc(%d)\n", __func__, ret);
	/* To mV */
	usbid /= 1000;

	pr_info("%s usbid : %dmV\n", __func__, usbid);

	/* 0x0 : pass, 0x1 : fail */
	if (usbid < SBU_UCT300_PASS)
		result = 0;
	else
		result = 1;

	return test_init ? result : usbid;
}

void mt6360_control_option_command(struct mt6360_chip *chip, int cmd)
{
	int pd_cmd = cmd & 0x0f;
	int usbid = 0;

/* 0x1 : Vconn control option command ON
 * 0x2 : Vconn control option command OFF
 * 0x3 : Water Detect option command ON
 * 0x4 : Water Detect option command OFF
 */
 	pr_info("%s :%d", __func__, cmd);

	switch (pd_cmd) {
	case 1:
	case 2:
		pr_info("%s not supported(%d)", __func__, cmd);
		break;
	case 3:
#ifdef CONFIG_WD_SBU_POLLING
		mt6360_enable_usbid_irq(chip, false);
#endif
		if (!test_init) {
			usbid = mt6360_usbid_check();
			pr_info("%s usbid :%d", __func__, usbid);
			if (usbid > SBU_UCT300_LOW)
				test_init = 1;
		}
		break;
	case 4:
#ifdef CONFIG_WD_SBU_POLLING
		mt6360_enable_usbid_irq(chip, true);
#endif
		break;
	default:
		break;
	}
}

static enum pdic_sysfs_property mt6360_sysfs_properties[] = {
	PDIC_SYSFS_PROP_CHIP_NAME,
	PDIC_SYSFS_PROP_CTRL_OPTION,
#if defined(CONFIG_WATER_DETECTION)
	PDIC_SYSFS_PROP_FW_WATER,
#endif
	PDIC_SYSFS_PROP_CC_PIN_STATUS,
#if IS_ENABLED(CONFIG_SEC_FACTORY)
	PDIC_SYSFS_PROP_15MODE_WATERTEST_TYPE,
#endif
	PDIC_SYSFS_PROP_NOVBUS_RP22K,
	PDIC_SYSFS_PROP_MAX_COUNT,
};

static int mt6360_sysfs_get_local_prop(struct _pdic_data_t *_data,
	enum pdic_sysfs_property prop, char *buf)
{
	struct tcpc_device *tcpc = (struct tcpc_device *)_data->drv_data;
	int cc_pin = PDIC_NOTIFY_PIN_STATUS_NO_DETERMINATION;
	int retval = -ENODEV;
	int cc1, cc2;
	bool rp22 = false;

	if (!tcpc) {
		pr_info("usbpd_data is null : request prop = %d", prop);
		return retval;
	}

	switch (prop) {
#if defined(CONFIG_WATER_DETECTION)
	case PDIC_SYSFS_PROP_FW_WATER:
		pr_info("%s is_water_detect=%d\n", __func__,
			(int)tcpc->water_state);
		retval = sprintf(buf, "%d\n", (int)tcpc->water_state);
		break;
#endif
	case PDIC_SYSFS_PROP_CC_PIN_STATUS:
		if (tcpc->typec_remote_cc[0] != TYPEC_CC_DRP_TOGGLING
			&& tcpc->typec_remote_cc[0] != TYPEC_CC_VOLT_OPEN)
			cc_pin = PDIC_NOTIFY_PIN_STATUS_CC1_ACTIVE;
		else if (tcpc->typec_remote_cc[1] != TYPEC_CC_DRP_TOGGLING
			&& tcpc->typec_remote_cc[1] != TYPEC_CC_VOLT_OPEN)
			cc_pin = PDIC_NOTIFY_PIN_STATUS_CC2_ACTIVE;

		retval = sprintf(buf, "%d\n", cc_pin);
		pr_info("usb: PDIC_SYSFS_PROP_PIN_STATUS : %d[%d,%d]",
			cc_pin, tcpc->typec_remote_cc[0], tcpc->typec_remote_cc[1]);
		break;
#if IS_ENABLED(CONFIG_SEC_FACTORY)
	case PDIC_SYSFS_PROP_15MODE_WATERTEST_TYPE:
#if IS_ENABLED(CONFIG_MTK_TYPEC_WATER_DETECT)
		retval = sprintf(buf, "sysfs\n");
#else
		retval = sprintf(buf, "unsupport\n");
#endif
		pr_info("%s : PDIC_SYSFS_PROP_15MODE_WATERTEST_TYPE : %s", __func__, buf);
		break;
#endif /* CONFIG_SEC_FACTORY */
	case PDIC_SYSFS_PROP_NOVBUS_RP22K:
		if (tcpc->ops->get_cc(tcpc, &cc1, &cc2) == 0) {
			pr_info("%s : cc1=%d, cc2=%d vbus=%d\n", __func__,
					cc1, cc2, tcpc->vbus_level);

			if (cc1 == TYPEC_CC_VOLT_SNK_1_5 &&
					cc2 == TYPEC_CC_VOLT_OPEN)
				rp22 = true;
			else if (cc2 == TYPEC_CC_VOLT_SNK_1_5 &&
					cc1 == TYPEC_CC_VOLT_OPEN)
				rp22 = true;

			if (rp22 && tcpc->vbus_level == TCPC_VBUS_SAFE0V)
				retval = sprintf(buf, "1\n");
			else
				retval = sprintf(buf, "0\n");
		}
		pr_info("%s : PDIC_SYSFS_PROP_NOVBUS_RP22K : %s\n", __func__,
				buf);
		break;
	default:
		pr_info("prop read not supported prop (%d)", prop);
		retval = -ENODATA;
		break;
	}

	return retval;
}

static ssize_t mt6360_sysfs_set_prop(struct _pdic_data_t *_data,
	enum pdic_sysfs_property prop, const char *buf, size_t size)
{
	struct tcpc_device *tcpc = (struct tcpc_device *)_data->drv_data;
	struct mt6360_chip *chip = NULL;
	int retval = -ENODEV, cmd = 0;

	if (!tcpc) {
		pr_info("usbpd_data is null : request prop = %d", prop);
		return retval;
	}

	chip = tcpc->drv_data;
	if (!chip) {
		pr_info("chip is null : request prop = %d", prop);
		return retval;
	}

	switch (prop) {
	case PDIC_SYSFS_PROP_CTRL_OPTION:
		sscanf(buf, "%d", &cmd);
		pr_info("usb: %s mode=%d\n", __func__, cmd);
		mt6360_control_option_command(chip, cmd);
		break;

	default:
		pr_info("%s prop write not supported prop (%d)\n", __func__, prop);
		retval = -ENODATA;
		return retval;
	}
	return size;
}

static int mt6360_sysfs_is_writeable(struct _pdic_data_t *_data,
	enum pdic_sysfs_property prop)
{
	switch (prop) {
	case PDIC_SYSFS_PROP_CTRL_OPTION:
		return 1;
	default:
		return 0;
	}
}

static int mt6360_sysfs_is_writeonly(struct _pdic_data_t *_data,
	enum pdic_sysfs_property prop)
{
	switch (prop) {
	case PDIC_SYSFS_PROP_CTRL_OPTION:
		return 1;
	default:
		return 0;
	}
}
#endif /* CONFIG_PDIC_NOTIFIER */

static int mt6360_tcpc_probe(struct i2c_client *client)
{
	struct mt6360_tcpc_platform_data *pdata =
		dev_get_platdata(&client->dev);
	bool use_dt = client->dev.of_node;
	struct mt6360_chip *chip;
	int ret, chip_id;
#if IS_ENABLED(CONFIG_PDIC_NOTIFIER)
	ppdic_data_t ppdic_data;
	ppdic_sysfs_property_t pd_prop;
#endif

	pr_info("%s\n", __func__);
	ret = i2c_check_functionality(client->adapter,
				      I2C_FUNC_SMBUS_I2C_BLOCK |
				      I2C_FUNC_SMBUS_BYTE_DATA);
	pr_info("%s I2C functionality : %s\n", __func__, ret ? "ok" : "fail");

	chip_id = mt6360_check_revision(client);
	if (chip_id < 0)
		return chip_id;

	chip = devm_kzalloc(&client->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	if (use_dt) {
		pdata = devm_kzalloc(&client->dev, sizeof(*pdata), GFP_KERNEL);
		if (!pdata)
			return -ENOMEM;
		ret = mt6360_parse_dt(chip, &client->dev, pdata);
		if (ret < 0)
			return -EINVAL;
		client->dev.platform_data = pdata;
	} else {
		dev_err(&client->dev, "%s no dts node\n", __func__);
		return -ENODEV;
	}
	chip->chip_id = chip_id;
	chip->dev = &client->dev;
	chip->client = client;
	sema_init(&chip->io_lock, 1);
	i2c_set_clientdata(client, chip);

#if CONFIG_WATER_DETECTION
	atomic_set(&chip->wd_protect_rty, CONFIG_WD_PROTECT_RETRY_COUNT);
	INIT_DELAYED_WORK(&chip->usbid_evt_dwork,
		mt6360_pmu_usbid_evt_dwork_handler);
	INIT_DELAYED_WORK(&chip->cc_hi_check_work,
		mt6360_cc_hi_check_work_func);
	INIT_DELAYED_WORK(&chip->wd_dwork, mt6360_wd_dwork_handler);
#endif /* CONFIG_WATER_DETECTION */

	dev_info(chip->dev, "%s chipID = 0x%0X\n", __func__, chip->chip_id);

#if IS_ENABLED(CONFIG_RT_REGMAP)
	ret = mt6360_regmap_init(chip);
	if (ret < 0) {
		dev_err(chip->dev, "%s regmap init fail(%d)\n", __func__, ret);
		goto err_regmap_init;
	}
#endif /* CONFIG_RT_REGMAP */

#if CONFIG_WATER_DETECTION
#if IS_ENABLED(CONFIG_MTK_CHARGER)
	chip->chgdev = get_charger_by_name("primary_chg");
	if (!chip->chgdev) {
		dev_err(chip->dev, "%s get charger device fail\n", __func__);
		ret = -EPROBE_DEFER;
		goto err_get_chg;
	}
#endif /* CONFIG_MTK_CHARGER */
#endif /* CONFIG_WATER_DETECTION */

	ret = mt6360_tcpcdev_init(chip, chip->dev);
	if (ret < 0) {
		dev_err(chip->dev, "%s tcpc dev init fail\n", __func__);
		goto err_tcpc_reg;
	}

	/* Set not to disconnect when vbus drop  */
	tcpm_set_exit_attached_snk_via_cc(chip->tcpc, true);
	dev_info(chip->dev, "%s pd_exit_attached_snk_via_cc : %d\n", __func__, chip->tcpc->pd_exit_attached_snk_via_cc);

#if IS_ENABLED(CONFIG_PDIC_NOTIFIER)
	ppdic_data = devm_kzalloc(&chip->tcpc->dev, sizeof(*ppdic_data), GFP_KERNEL);
	if (!ppdic_data)
		goto err_tcpc_ctd_reg;

	pd_prop = devm_kzalloc(&chip->tcpc->dev, sizeof(*pd_prop), GFP_KERNEL);
	if (!pd_prop)
		goto err_tcpc_ctd_reg;

	ppdic_data->drv_data = chip->tcpc;
	ppdic_data->name = "mt6360";
	ppdic_data->pdic_sysfs_prop = pd_prop;
	pd_prop->get_property = mt6360_sysfs_get_local_prop;
	pd_prop->set_property = mt6360_sysfs_set_prop;
	pd_prop->property_is_writeable = mt6360_sysfs_is_writeable;
	pd_prop->property_is_writeonly = mt6360_sysfs_is_writeonly;
	pd_prop->properties = mt6360_sysfs_properties;
	pd_prop->num_properties = ARRAY_SIZE(mt6360_sysfs_properties);
	chip->tcpc->ppdic_data = ppdic_data;
	pdic_register_switch_device(1);
	pdic_misc_init(ppdic_data);

	ret = pdic_core_register_chip(ppdic_data);

	if (ret)
		goto err_pdic_core_register;
#endif	/* CONFIG_PDIC_NOTIFIER */

	ret = mt6360_software_reset(chip->tcpc);
	if (ret < 0) {
		dev_err(chip->dev, "%s sw reset fail\n", __func__);
		goto err_sw_reset;
	}

	ret = mt6360_init_alert(chip->tcpc);
	if (ret < 0) {
		dev_err(chip->dev, "%s init alert fail\n", __func__);
		goto err_init_alert;
	}

	dev_info(chip->dev, "%s successfully!\n", __func__);
	return 0;

err_init_alert:
err_sw_reset:
#if IS_ENABLED(CONFIG_PDIC_NOTIFIER)
	pdic_core_unregister_chip();
err_pdic_core_register:
	pdic_misc_exit();
	pdic_register_switch_device(0);
err_tcpc_ctd_reg:
#endif
	tcpc_device_unregister(chip->dev, chip->tcpc);
err_tcpc_reg:
#if CONFIG_WATER_DETECTION
#if IS_ENABLED(CONFIG_MTK_CHARGER)
err_get_chg:
#endif /* CONFIG_MTK_CHARGER */
#endif /* CONFIG_WATER_DETECTION */
#if IS_ENABLED(CONFIG_RT_REGMAP)
	mt6360_regmap_deinit(chip);
err_regmap_init:
#endif /* CONFIG_RT_REGMAP */
	return ret;
}

static void mt6360_tcpc_remove(struct i2c_client *client)
{
	struct mt6360_chip *chip = i2c_get_clientdata(client);

	disable_irq(chip->irq);
#if CONFIG_WATER_DETECTION
	cancel_delayed_work_sync(&chip->usbid_evt_dwork);
	cancel_delayed_work_sync(&chip->wd_dwork);
	cancel_delayed_work_sync(&chip->cc_hi_check_work);
#endif /* CONFIG_WATER_DETECTION */

#if IS_ENABLED(CONFIG_PDIC_NOTIFIER)
	pdic_misc_exit();
	pdic_register_switch_device(0);
#endif
	tcpc_device_unregister(chip->dev, chip->tcpc);
#if IS_ENABLED(CONFIG_RT_REGMAP)
		mt6360_regmap_deinit(chip);
#endif /* CONFIG_RT_REGMAP */
}

static void mt6360_tcpc_shutdown(struct i2c_client *client)
{
	struct mt6360_chip *chip = i2c_get_clientdata(client);

	disable_irq(chip->irq);
#if CONFIG_WATER_DETECTION
	cancel_delayed_work_sync(&chip->usbid_evt_dwork);
	cancel_delayed_work_sync(&chip->wd_dwork);
	cancel_delayed_work_sync(&chip->cc_hi_check_work);
#endif /* CONFIG_WATER_DETECTION */
	tcpm_shutdown(chip->tcpc);
}

static int __maybe_unused mt6360_tcpc_suspend(struct device *dev)
{
	struct mt6360_chip *chip = dev_get_drvdata(dev);
	int ret = 0;

	dev_info(dev, "%s irq_gpio = %d\n",
		      __func__, gpio_get_value(chip->irq_gpio));

	ret = tcpm_suspend(chip->tcpc);
	if (ret)
		return ret;
	disable_irq(chip->irq);
	return 0;
}

static int __maybe_unused mt6360_tcpc_check_suspend_pending(struct device *dev)
{
	struct mt6360_chip *chip = dev_get_drvdata(dev);

	dev_info(dev, "%s irq_gpio = %d\n",
		      __func__, gpio_get_value(chip->irq_gpio));

	return tcpm_check_suspend_pending(chip->tcpc);
}

static int __maybe_unused mt6360_tcpc_resume(struct device *dev)
{
	struct mt6360_chip *chip = dev_get_drvdata(dev);

	dev_info(dev, "%s irq_gpio = %d\n",
		      __func__, gpio_get_value(chip->irq_gpio));

	enable_irq(chip->irq);
	tcpm_resume(chip->tcpc);

	return 0;
}

static const struct dev_pm_ops mt6360_tcpc_pm_ops = {
#if IS_ENABLED(CONFIG_PM_SLEEP)
	.prepare = mt6360_tcpc_check_suspend_pending,
#endif	/* CONFIG_PM_SLEEP */
	SET_SYSTEM_SLEEP_PM_OPS(mt6360_tcpc_suspend, mt6360_tcpc_resume)
	SET_LATE_SYSTEM_SLEEP_PM_OPS(mt6360_tcpc_check_suspend_pending, NULL)
	SET_NOIRQ_SYSTEM_SLEEP_PM_OPS(mt6360_tcpc_check_suspend_pending, NULL)
};

static const struct of_device_id mt6360_tcpc_of_match[] = {
	{.compatible = "mediatek,mt6360_typec",},
	{.compatible = "mediatek,usb_type_c",},
	{},
};
MODULE_DEVICE_TABLE(of, mt6360_tcpc_of_match);

static struct i2c_driver mt6360_tcpc_driver = {
	.probe = mt6360_tcpc_probe,
	.remove = mt6360_tcpc_remove,
	.shutdown = mt6360_tcpc_shutdown,
	.driver = {
		.name = "mt6360_typec",
		.owner = THIS_MODULE,
		.of_match_table = mt6360_tcpc_of_match,
		.pm = &mt6360_tcpc_pm_ops,
	},
};

static int __init mt6360_init(void)
{
	struct device_node *np;

	pr_info("%s (%s)\n", __func__, MT6360_DRV_VERSION);
	np = of_find_node_by_name(NULL, "tcpc");
	pr_info("%s tcpc node %s\n", __func__,
		np == NULL ? "not found" : "found");

	return i2c_add_driver(&mt6360_tcpc_driver);
}
subsys_initcall(mt6360_init);

static void __exit mt6360_exit(void)
{
	i2c_del_driver(&mt6360_tcpc_driver);
}
module_exit(mt6360_exit);

MODULE_DESCRIPTION("MT6360 TCPC Driver");
MODULE_VERSION(MT6360_DRV_VERSION);
#endif	/* CONFIG_TCPC_CLASS */
MODULE_LICENSE("GPL");

/**** Release Note ****
 * 2.0.12_MTK
 *	(1) Do I2C/IO transactions when system resumed
 *	(2) Reduce log printing
 *	(3) Control CC Open in the deinit ops
 *	(4) Decrease BMC Decoder idle time to 17.982us
 *
 * 2.0.11_MTK
 *	(1) Decrease the I2C/IO transactions
 *	(2) Remove the old way of get_power_status()
 *	(3) Add CONFIG_TYPEC_SNK_ONLY_WHEN_SUSPEND
 *
 * 2.0.10_MTK
 *	(1) Add cc_hi ops
 *	(2) Revise WD and CTD flows
 *
 * 2.0.9_MTK
 *	(1) Revise suspend/resume flow for IRQ
 *
 * 2.0.8_MTK
 *	(1) Revise IRQ handling
 *
 * 2.0.7_MTK
 *	(1) mdelay(1) after SHIPPING_OFF = 1
 *
 * 2.0.6_MTK
 *	(1) Utilize rt-regmap to reduce I2C accesses
 *	(2) Disable BLEED_DISC and Enable AUTO_DISC_DISCNCT
 *
 * 2.0.5_MTK
 *	(1) Mask vSafe0V IRQ before entering low power mode
 *	(2) AUTOIDLE enable
 *	(3) Reset Protocol FSM and clear RX alerts twice before clock gating
 *
 * 2.0.3_MTK
 *	(1) Single Rp as Attatched.SRC for Ellisys TD.4.9.4
 *
 * 2.0.2_MTK
 *	(1) Add vendor defined irq handler
 *	(2) Remove init_cc_param
 *	(3) Add shield protection WD
 *	(4) Add update/set/clr bit
 *
 * 2.0.1_MTK
 *	First released PD3.0 Driver on MTK platform
 */
