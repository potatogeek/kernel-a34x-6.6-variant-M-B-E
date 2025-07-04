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
#include <linux/sched/clock.h>

#include "inc/tcpci.h"
#include "inc/rt1711h.h"
#include "inc/tcpci_typec.h"

#if IS_ENABLED(CONFIG_RT_REGMAP)
#include "inc/rt-regmap.h"
#endif /* CONFIG_RT_REGMAP */

#define RT1711H_DRV_VERSION	"2.0.9_MTK"

struct rt1711_chip {
	struct i2c_client *client;
	struct device *dev;
#if IS_ENABLED(CONFIG_RT_REGMAP)
	struct rt_regmap_device *m_dev;
#endif /* CONFIG_RT_REGMAP */
	struct tcpc_desc *tcpc_desc;
	struct tcpc_device *tcpc;

	int irq_gpio;
	int irq;
	int chip_id;

	bool vconn_en;
	bool lpm_en;
	bool is_deinit;
};

#if IS_ENABLED(CONFIG_RT_REGMAP)
RT_REG_DECL(TCPC_V10_REG_VID, 2, RT_NORMAL_WR_ONCE, {});
RT_REG_DECL(TCPC_V10_REG_PID, 2, RT_NORMAL_WR_ONCE, {});
RT_REG_DECL(TCPC_V10_REG_DID, 2, RT_NORMAL_WR_ONCE, {});
RT_REG_DECL(TCPC_V10_REG_TYPEC_REV, 2, RT_NORMAL_WR_ONCE, {});
RT_REG_DECL(TCPC_V10_REG_PD_REV, 2, RT_NORMAL_WR_ONCE, {});
RT_REG_DECL(TCPC_V10_REG_PDIF_REV, 2, RT_NORMAL_WR_ONCE, {});
RT_REG_DECL(TCPC_V10_REG_ALERT, 2, RT_VOLATILE, {});
RT_REG_DECL(TCPC_V10_REG_ALERT_MASK, 4, RT_NORMAL_WR_ONCE, {});
RT_REG_DECL(TCPC_V10_REG_TCPC_CTRL, 1, RT_NORMAL_WR_ONCE, {});
RT_REG_DECL(TCPC_V10_REG_ROLE_CTRL, 1, RT_NORMAL_WR_ONCE, {});
RT_REG_DECL(TCPC_V10_REG_FAULT_CTRL, 1, RT_NORMAL_WR_ONCE, {});
RT_REG_DECL(TCPC_V10_REG_POWER_CTRL, 1, RT_VOLATILE, {});
RT_REG_DECL(TCPC_V10_REG_CC_STATUS, 1, RT_VOLATILE, {});
RT_REG_DECL(TCPC_V10_REG_POWER_STATUS, 1, RT_VOLATILE, {});
RT_REG_DECL(TCPC_V10_REG_FAULT_STATUS, 1, RT_VOLATILE, {});
RT_REG_DECL(TCPC_V10_REG_COMMAND, 1, RT_VOLATILE, {});
RT_REG_DECL(TCPC_V10_REG_MSG_HDR_INFO, 1, RT_NORMAL_WR_ONCE, {});
RT_REG_DECL(TCPC_V10_REG_RX_DETECT, 1, RT_NORMAL_WR_ONCE, {});
RT_REG_DECL(TCPC_V10_REG_RX_BYTE_CNT, 32, RT_VOLATILE, {});
RT_REG_DECL(TCPC_V10_REG_TRANSMIT, 1, RT_VOLATILE, {});
RT_REG_DECL(TCPC_V10_REG_TX_BYTE_CNT, 31, RT_NORMAL_WR_ONCE, {});
RT_REG_DECL(RT1711H_REG_CONFIG_GPIO0, 1, RT_NORMAL_WR_ONCE, {});
RT_REG_DECL(RT1711H_REG_PHY_CTRL1, 1, RT_NORMAL_WR_ONCE, {});
RT_REG_DECL(RT1711H_REG_CLK_CTRL2, 2, RT_NORMAL_WR_ONCE, {});
RT_REG_DECL(RT1711H_REG_PRL_FSM_RESET, 1, RT_VOLATILE, {});
RT_REG_DECL(RT1711H_REG_BMC_CTRL, 1, RT_VOLATILE, {});
RT_REG_DECL(RT1711H_REG_BMCIO_RXDZSEL, 1, RT_NORMAL_WR_ONCE, {});
RT_REG_DECL(RT1711H_REG_RT_STATUS, 1, RT_VOLATILE, {});
RT_REG_DECL(RT1711H_REG_RT_INT, 1, RT_VOLATILE, {});
RT_REG_DECL(RT1711H_REG_RT_MASK, 1, RT_NORMAL_WR_ONCE, {});
RT_REG_DECL(RT1711H_REG_IDLE_CTRL, 1, RT_NORMAL_WR_ONCE, {});
RT_REG_DECL(RT1711H_REG_I2CRST_CTRL, 1, RT_NORMAL_WR_ONCE, {});
RT_REG_DECL(RT1711H_REG_SWRESET, 1, RT_VOLATILE, {});
RT_REG_DECL(RT1711H_REG_TTCPC_FILTER, 1, RT_NORMAL_WR_ONCE, {});
RT_REG_DECL(RT1711H_REG_DRP_TOGGLE_CYCLE, 1, RT_NORMAL_WR_ONCE, {});
RT_REG_DECL(RT1711H_REG_DRP_DUTY_CTRL, 2, RT_VOLATILE, {});
RT_REG_DECL(RT1711H_REG_BMCIO_RXDZEN, 1, RT_NORMAL_WR_ONCE, {});
RT_REG_DECL(RT1711H_REG_UNLOCK_PW_2, 2, RT_VOLATILE, {});
RT_REG_DECL(RT1711H_REG_EFUSE5, 1, RT_VOLATILE, {});

static const rt_register_map_t rt1711_chip_regmap[] = {
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
	RT_REG(TCPC_V10_REG_COMMAND),
	RT_REG(TCPC_V10_REG_MSG_HDR_INFO),
	RT_REG(TCPC_V10_REG_RX_DETECT),
	RT_REG(TCPC_V10_REG_RX_BYTE_CNT),
	RT_REG(TCPC_V10_REG_TRANSMIT),
	RT_REG(TCPC_V10_REG_TX_BYTE_CNT),
	RT_REG(RT1711H_REG_CONFIG_GPIO0),
	RT_REG(RT1711H_REG_PHY_CTRL1),
	RT_REG(RT1711H_REG_CLK_CTRL2),
	RT_REG(RT1711H_REG_PRL_FSM_RESET),
	RT_REG(RT1711H_REG_BMC_CTRL),
	RT_REG(RT1711H_REG_BMCIO_RXDZSEL),
	RT_REG(RT1711H_REG_RT_STATUS),
	RT_REG(RT1711H_REG_RT_INT),
	RT_REG(RT1711H_REG_RT_MASK),
	RT_REG(RT1711H_REG_IDLE_CTRL),
	RT_REG(RT1711H_REG_I2CRST_CTRL),
	RT_REG(RT1711H_REG_SWRESET),
	RT_REG(RT1711H_REG_TTCPC_FILTER),
	RT_REG(RT1711H_REG_DRP_TOGGLE_CYCLE),
	RT_REG(RT1711H_REG_DRP_DUTY_CTRL),
	RT_REG(RT1711H_REG_BMCIO_RXDZEN),
	RT_REG(RT1711H_REG_UNLOCK_PW_2),
	RT_REG(RT1711H_REG_EFUSE5),
};
#define RT1711_CHIP_REGMAP_SIZE ARRAY_SIZE(rt1711_chip_regmap)

#endif /* CONFIG_RT_REGMAP */

static int rt1711_read_device(void *client, u32 reg, int len, void *dst)
{
	struct i2c_client *i2c = client;
	struct rt1711_chip *chip = i2c_get_clientdata(i2c);
	struct tcpc_device *tcpc = chip->tcpc;
	int ret = 0, count = 5;
	u64 __maybe_unused t = 0;

	atomic_inc(&tcpc->suspend_pending);
	wait_event(tcpc->resume_wait_que,
		   !chip->dev->parent->power.is_suspended);
	while (1) {
		t = local_clock();
		ret = i2c_smbus_read_i2c_block_data(i2c, reg, len, dst);
		t = local_clock() - t;
		do_div(t, NSEC_PER_USEC);
		RT1711_INFO("del = %lluus, reg = 0x%02X, len = %d\n",
			    t, reg, len);
		if (ret < 0 && count > 1)
			count--;
		else
			break;
		udelay(100);
	}
	atomic_dec_if_positive(&tcpc->suspend_pending);
	return ret;
}

static int rt1711_write_device(void *client, u32 reg, int len, const void *src)
{
	struct i2c_client *i2c = client;
	struct rt1711_chip *chip = i2c_get_clientdata(i2c);
	struct tcpc_device *tcpc = chip->tcpc;
	int ret = 0, count = 5;
	u64 __maybe_unused t = 0;

	if (chip->is_deinit)
		return -EACCES;

	atomic_inc(&tcpc->suspend_pending);
	wait_event(tcpc->resume_wait_que,
		   !chip->dev->parent->power.is_suspended);
	while (1) {
		t = local_clock();
		ret = i2c_smbus_write_i2c_block_data(i2c, reg, len, src);
		t = local_clock() - t;
		do_div(t, NSEC_PER_USEC);
		RT1711_INFO("del = %lluus, reg = 0x%02X, len = %d\n",
			    t, reg, len);
		if (ret < 0 && count > 1)
			count--;
		else
			break;
		udelay(100);
	}
	atomic_dec_if_positive(&tcpc->suspend_pending);
	return ret;
}

static int rt1711_reg_read(struct i2c_client *i2c, u8 reg)
{
	struct rt1711_chip *chip = i2c_get_clientdata(i2c);
	u8 val = 0;
	int ret = 0;

#if IS_ENABLED(CONFIG_RT_REGMAP)
	ret = rt_regmap_block_read(chip->m_dev, reg, 1, &val);
#else
	ret = rt1711_read_device(chip->client, reg, 1, &val);
#endif /* CONFIG_RT_REGMAP */
	if (ret < 0) {
		dev_err(chip->dev, "rt1711 reg read fail\n");
		return ret;
	}
	return val;
}

static int rt1711_reg_write(struct i2c_client *i2c, u8 reg, const u8 data)
{
	struct rt1711_chip *chip = i2c_get_clientdata(i2c);
	int ret = 0;

#if IS_ENABLED(CONFIG_RT_REGMAP)
	ret = rt_regmap_block_write(chip->m_dev, reg, 1, &data);
#else
	ret = rt1711_write_device(chip->client, reg, 1, &data);
#endif /* CONFIG_RT_REGMAP */
	if (ret < 0)
		dev_err(chip->dev, "rt1711 reg write fail\n");
	return ret;
}

static int rt1711_block_read(struct i2c_client *i2c,
			u8 reg, int len, void *dst)
{
	struct rt1711_chip *chip = i2c_get_clientdata(i2c);
	int ret = 0;
#if IS_ENABLED(CONFIG_RT_REGMAP)
	ret = rt_regmap_block_read(chip->m_dev, reg, len, dst);
#else
	ret = rt1711_read_device(chip->client, reg, len, dst);
#endif /* CONFIG_RT_REGMAP */
	if (ret < 0)
		dev_err(chip->dev, "rt1711 block read fail\n");
	return ret;
}

static int rt1711_block_write(struct i2c_client *i2c,
			u8 reg, int len, const void *src)
{
	struct rt1711_chip *chip = i2c_get_clientdata(i2c);
	int ret = 0;
#if IS_ENABLED(CONFIG_RT_REGMAP)
	ret = rt_regmap_block_write(chip->m_dev, reg, len, src);
#else
	ret = rt1711_write_device(chip->client, reg, len, src);
#endif /* CONFIG_RT_REGMAP */
	if (ret < 0)
		dev_err(chip->dev, "rt1711 block write fail\n");
	return ret;
}

static int32_t rt1711_write_word(struct i2c_client *client,
					uint8_t reg_addr, uint16_t data)
{
	data = cpu_to_le16(data);
	return rt1711_block_write(client, reg_addr, 2, &data);
}

static int32_t rt1711_read_word(struct i2c_client *client,
					uint8_t reg_addr, uint16_t *data)
{
	int ret = rt1711_block_read(client, reg_addr, 2, data);
	*data = le16_to_cpu(*data);
	return ret;
}

static inline int rt1711_i2c_write8(
	struct tcpc_device *tcpc, u8 reg, const u8 data)
{
	struct rt1711_chip *chip = tcpc_get_dev_data(tcpc);

	return rt1711_reg_write(chip->client, reg, data);
}

static inline int rt1711_i2c_write16(
		struct tcpc_device *tcpc, u8 reg, const u16 data)
{
	struct rt1711_chip *chip = tcpc_get_dev_data(tcpc);

	return rt1711_write_word(chip->client, reg, data);
}

static inline int rt1711_i2c_read8(struct tcpc_device *tcpc, u8 reg)
{
	struct rt1711_chip *chip = tcpc_get_dev_data(tcpc);

	return rt1711_reg_read(chip->client, reg);
}

static inline int rt1711_i2c_read16(
	struct tcpc_device *tcpc, u8 reg)
{
	struct rt1711_chip *chip = tcpc_get_dev_data(tcpc);
	u16 data;
	int ret;

	ret = rt1711_read_word(chip->client, reg, &data);
	if (ret < 0)
		return ret;
	return data;
}

static int rt1711_i2c_update_bits(struct tcpc_device *tcpc, u8 reg,
				  u8 val, u8 mask)
{
	u8 data = 0;
	int ret = 0;

	ret = rt1711_i2c_read8(tcpc, reg);
	if (ret < 0)
		return ret;
	data = ret;

	data &= ~mask;
	data |= val & mask;

	return rt1711_i2c_write8(tcpc, reg, data);
}

static inline int rt1711_i2c_set_bits(struct tcpc_device *tcpc, u8 reg, u8 mask)
{
	return rt1711_i2c_update_bits(tcpc, reg, mask, mask);
}

static inline int rt1711_i2c_clr_bits(struct tcpc_device *tcpc, u8 reg, u8 mask)
{
	return rt1711_i2c_update_bits(tcpc, reg, 0x00, mask);
}

#if IS_ENABLED(CONFIG_RT_REGMAP)
static struct rt_regmap_fops rt1711_regmap_fops = {
	.read_device = rt1711_read_device,
	.write_device = rt1711_write_device,
};
#endif /* CONFIG_RT_REGMAP */

static int rt1711_regmap_init(struct rt1711_chip *chip)
{
#if IS_ENABLED(CONFIG_RT_REGMAP)
	struct rt_regmap_properties *props;
	char name[32];
	int len;

	props = devm_kzalloc(chip->dev, sizeof(*props), GFP_KERNEL);
	if (!props)
		return -ENOMEM;

	props->register_num = RT1711_CHIP_REGMAP_SIZE;
	props->rm = rt1711_chip_regmap;

	props->rt_regmap_mode = RT_MULTI_BYTE |
				RT_IO_PASS_THROUGH | RT_DBG_SPECIAL;
	snprintf(name, sizeof(name), "rt1711-%02x", chip->client->addr);

	len = strlen(name);
	props->name = devm_kzalloc(chip->dev, len+1, GFP_KERNEL);
	props->aliases = devm_kzalloc(chip->dev, len+1, GFP_KERNEL);

	if ((!props->name) || (!props->aliases))
		return -ENOMEM;

	strlcpy((char *)props->name, name, len+1);
	strlcpy((char *)props->aliases, name, len+1);
	props->io_log_en = 0;

	chip->m_dev = rt_regmap_device_register(props,
			&rt1711_regmap_fops, chip->dev, chip->client, chip);
	if (!chip->m_dev) {
		dev_err(chip->dev, "rt1711 chip rt_regmap register fail\n");
		return -EINVAL;
	}
#endif
	return 0;
}

static int rt1711_regmap_deinit(struct rt1711_chip *chip)
{
#if IS_ENABLED(CONFIG_RT_REGMAP)
	rt_regmap_device_unregister(chip->m_dev);
#endif
	return 0;
}

static inline int rt1711_software_reset(struct tcpc_device *tcpc)
{
	int ret = rt1711_i2c_write8(tcpc, RT1711H_REG_SWRESET, 1);
#if IS_ENABLED(CONFIG_RT_REGMAP)
	struct rt1711_chip *chip = tcpc_get_dev_data(tcpc);
#endif /* CONFIG_RT_REGMAP */

	if (ret < 0)
		return ret;
#if IS_ENABLED(CONFIG_RT_REGMAP)
	rt_regmap_cache_reload(chip->m_dev);
#endif /* CONFIG_RT_REGMAP */
	usleep_range(1000, 2000);
	return 0;
}

static inline int rt1711_command(struct tcpc_device *tcpc, uint8_t cmd)
{
	return rt1711_i2c_write8(tcpc, TCPC_V10_REG_COMMAND, cmd);
}

static int rt1711_init_vbus_cal(struct tcpc_device *tcpc)
{
	struct rt1711_chip *chip = tcpc_get_dev_data(tcpc);
	const u8 val_en_test_mode[] = {0x86, 0x62};
	const u8 val_dis_test_mode[] = {0x00, 0x00};
	int ret = 0;
	u8 data = 0;
	s8 cal = 0;

	ret = rt1711_block_write(chip->client, RT1711H_REG_UNLOCK_PW_2,
			ARRAY_SIZE(val_en_test_mode), val_en_test_mode);
	if (ret < 0)
		dev_notice(chip->dev, "%s en test mode fail(%d)\n",
				__func__, ret);

	ret = rt1711_reg_read(chip->client, RT1711H_REG_EFUSE5);
	if (ret < 0)
		goto out;

	data = ret;
	data = (data & RT1711H_REG_M_VBUS_CAL) >> RT1711H_REG_S_VBUS_CAL;
	cal = (data & BIT(2)) ? (data | GENMASK(7, 3)) : data;
	cal -= 2;
	if (cal < RT1711H_REG_MIN_VBUS_CAL)
		cal = RT1711H_REG_MIN_VBUS_CAL;
	data = (cal << RT1711H_REG_S_VBUS_CAL) | (ret & GENMASK(4, 0));

	ret = rt1711_reg_write(chip->client, RT1711H_REG_EFUSE5, data);
out:
	ret = rt1711_block_write(chip->client, RT1711H_REG_UNLOCK_PW_2,
			ARRAY_SIZE(val_dis_test_mode), val_dis_test_mode);
	if (ret < 0)
		dev_notice(chip->dev, "%s dis test mode fail(%d)\n",
				__func__, ret);

	return ret;
}

static inline int rt1711_init_alert_mask(struct tcpc_device *tcpc)
{
	struct rt1711_chip *chip = tcpc_get_dev_data(tcpc);
	uint16_t mask = TCPC_V10_REG_ALERT_CC_STATUS |
			TCPC_V10_REG_ALERT_POWER_STATUS |
			TCPC_V10_REG_ALERT_FAULT;
	uint8_t masks[4] = {0x00, 0x00,
			    TCPC_V10_REG_POWER_STATUS_VBUS_PRES,
			    TCPC_V10_REG_FAULT_STATUS_VCONN_OV |
			    TCPC_V10_REG_FAULT_STATUS_VCONN_OC};

#if IS_ENABLED(CONFIG_USB_POWER_DELIVERY)
	/* Need to handle RX overflow */
	mask |= TCPC_V10_REG_ALERT_TX_SUCCESS | TCPC_V10_REG_ALERT_TX_DISCARDED
			| TCPC_V10_REG_ALERT_TX_FAILED
			| TCPC_V10_REG_ALERT_RX_HARD_RST
			| TCPC_V10_REG_ALERT_RX_STATUS
			| TCPC_V10_REG_ALERT_RX_OVERFLOW;
#endif
	*(uint16_t *)masks = cpu_to_le16(mask);
	return rt1711_block_write(chip->client, TCPC_V10_REG_ALERT_MASK,
				  sizeof(masks), masks);
}

static inline int rt1711_init_rt_mask(struct tcpc_device *tcpc)
{
	uint8_t rt_mask = RT1711H_REG_M_WAKEUP | RT1711H_REG_M_VBUS_80;

	return rt1711_i2c_write8(tcpc, RT1711H_REG_RT_MASK, rt_mask);
}

static irqreturn_t rt1711_intr_handler(int irq, void *data)
{
	struct rt1711_chip *chip = data;
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

static int rt1711_init_alert(struct tcpc_device *tcpc)
{
	struct rt1711_chip *chip = tcpc_get_dev_data(tcpc);
	int ret = 0;
	char *name = NULL;

	/* Clear Alert Mask & Status */
	rt1711_write_word(chip->client, TCPC_V10_REG_ALERT_MASK, 0);
	rt1711_write_word(chip->client, TCPC_V10_REG_ALERT, 0xffff);

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
					rt1711_intr_handler,
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

static int rt1711_alert_status_clear(struct tcpc_device *tcpc, uint32_t mask)
{
	int ret;
	uint16_t mask_t1;
	uint8_t mask_t2;

	mask_t1 = mask;
	if (mask_t1) {
		ret = rt1711_i2c_write16(tcpc, TCPC_V10_REG_ALERT, mask_t1);
		if (ret < 0)
			return ret;
	}

	mask_t2 = mask >> 16;
	if (mask_t2) {
		ret = rt1711_i2c_write8(tcpc, RT1711H_REG_RT_INT, mask_t2);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int rt1711h_set_clock_gating(struct tcpc_device *tcpc, bool en)
{
	int ret = 0;
#if CONFIG_TCPC_CLOCK_GATING
	struct rt1711_chip *chip = tcpc_get_dev_data(tcpc);
	int i = 0;
	uint8_t clks[2] = {RT1711H_REG_CLK_DIV_600K_EN |
			   RT1711H_REG_CLK_DIV_300K_EN |
			   RT1711H_REG_CLK_CK_300K_EN,
			   RT1711H_REG_CLK_DIV_2P4M_EN};

	if (en) {
		for (i = 0; i < 2; i++)
			ret = rt1711_alert_status_clear(tcpc,
				TCPC_V10_REG_ALERT_RX_ALL_MASK);
	} else {
		clks[0] |= RT1711H_REG_CLK_BCLK2_EN | RT1711H_REG_CLK_BCLK_EN;
		clks[1] |= RT1711H_REG_CLK_CK_24M_EN | RT1711H_REG_CLK_PCLK_EN;
	}

	if (ret == 0)
		ret = rt1711_block_write(chip->client, RT1711H_REG_CLK_CTRL2,
					 sizeof(clks), clks);
#endif	/* CONFIG_TCPC_CLOCK_GATING */

	return ret;
}

static inline int rt1711h_init_cc_params(
			struct tcpc_device *tcpc, uint8_t cc_res)
{
	int rv = 0;

#if IS_ENABLED(CONFIG_USB_POWER_DELIVERY)
#if CONFIG_USB_PD_SNK_DFT_NO_GOOD_CRC
	uint8_t en, sel;
	struct rt1711_chip *chip = tcpc_get_dev_data(tcpc);

	if (cc_res == TYPEC_CC_VOLT_SNK_DFT) {	/* 0.55 */
		en = 0;
		sel = 0x81;
	} else if (chip->chip_id >= RT1715_DID_D) {	/* 0.35 & 0.75 */
		en = 1;
		sel = 0x81;
	} else {	/* 0.4 & 0.7 */
		en = 1;
		sel = 0x80;
	}

	rv = rt1711_i2c_write8(tcpc, RT1711H_REG_BMCIO_RXDZEN, en);
	if (rv == 0)
		rv = rt1711_i2c_write8(tcpc, RT1711H_REG_BMCIO_RXDZSEL, sel);
#endif	/* CONFIG_USB_PD_SNK_DFT_NO_GOOD_CRC */
#endif	/* CONFIG_USB_POWER_DELIVERY */

	return rv;
}

static int rt1711h_idle_ctrl(struct tcpc_device *tcpc)
{
	struct rt1711_chip *chip = tcpc_get_dev_data(tcpc);

	if (chip->vconn_en || chip->lpm_en)
		return rt1711_i2c_write8(tcpc, RT1711H_REG_IDLE_CTRL,
				RT1711H_REG_IDLE_SET(0, 1, 0, 0));
	else
		return rt1711_i2c_write8(tcpc, RT1711H_REG_IDLE_CTRL,
				RT1711H_REG_IDLE_SET(0, 1, 1, 0));
}

static int rt1711_tcpc_init(struct tcpc_device *tcpc, bool sw_reset)
{
	int ret;
	bool retry_discard_old = false;
	struct rt1711_chip *chip = tcpc_get_dev_data(tcpc);

	RT1711_INFO("\n");

	if (sw_reset) {
		ret = rt1711_software_reset(tcpc);
		if (ret < 0)
			return ret;
	}

#if CONFIG_TCPC_I2CRST_EN
	rt1711_i2c_write8(tcpc,
		RT1711H_REG_I2CRST_CTRL,
		RT1711H_REG_I2CRST_SET(true, 0x0f));
#endif	/* CONFIG_TCPC_I2CRST_EN */

	/* UFP Both RD setting */
	/* DRP = 0, RpVal = 0 (Default), Rd, Rd */
	rt1711_i2c_write8(tcpc, TCPC_V10_REG_ROLE_CTRL,
		TCPC_V10_REG_ROLE_CTRL_RES_SET(0, 0, CC_RD, CC_RD));

	if (chip->chip_id == RT1711H_DID_A) {
		rt1711_i2c_write8(tcpc, TCPC_V10_REG_FAULT_CTRL,
			TCPC_V10_REG_FAULT_CTRL_DIS_VCONN_OV);
	}

	/*
	 * CC Detect Debounce : 26.7*val us
	 * Transition window count : spec 12~20us, based on 2.4MHz
	 * DRP Toggle Cycle : 51.2 + 6.4*val ms
	 * DRP Duty Ctrl : dcSRC / 1024
	 */

	rt1711_i2c_write8(tcpc, RT1711H_REG_TTCPC_FILTER, 15);
	rt1711_i2c_write8(tcpc, RT1711H_REG_DRP_TOGGLE_CYCLE, 0);
	rt1711_i2c_write16(tcpc,
		RT1711H_REG_DRP_DUTY_CTRL, TCPC_NORMAL_RP_DUTY);

	/* RX/TX Clock Gating (Auto Mode)*/
	if (!sw_reset)
		rt1711h_set_clock_gating(tcpc, true);

	if (!(tcpc->tcpc_flags & TCPC_FLAGS_RETRY_CRC_DISCARD))
		retry_discard_old = true;

	rt1711_i2c_write8(tcpc, RT1711H_REG_CONFIG_GPIO0, 0x80);

	/* For BIST, Change Transition Toggle Counter (Noise) from 3 to 7 */
	rt1711_i2c_write8(tcpc, RT1711H_REG_PHY_CTRL1,
		RT1711H_REG_PHY_CTRL1_SET(retry_discard_old, 7, 0, 1));

	rt1711_init_vbus_cal(tcpc);
	rt1711_init_rt_mask(tcpc);
	rt1711_init_alert_mask(tcpc);

	rt1711h_idle_ctrl(tcpc);
	mdelay(1);

	return 0;
}

static inline int rt1711_fault_status_vconn_ov(struct tcpc_device *tcpc)
{
	return rt1711_i2c_clr_bits(tcpc, RT1711H_REG_BMC_CTRL,
				   RT1711H_REG_DISCHARGE_EN);
}

static int rt1711_fault_status_clear(struct tcpc_device *tcpc, uint8_t status)
{
	if (status & TCPC_V10_REG_FAULT_STATUS_VCONN_OV)
		rt1711_fault_status_vconn_ov(tcpc);

	return rt1711_i2c_write8(tcpc, TCPC_V10_REG_FAULT_STATUS, status);
}

static int rt1711_set_alert_mask(struct tcpc_device *tcpc, uint32_t mask)
{
	int ret = 0;

	ret = rt1711_i2c_write16(tcpc, TCPC_V10_REG_ALERT_MASK, mask);
	if (ret < 0)
		return ret;

	return rt1711_i2c_write8(tcpc, RT1711H_REG_RT_MASK, mask >> 16);
}

static int rt1711_get_alert_mask(struct tcpc_device *tcpc, uint32_t *mask)
{
	int ret;
	uint8_t v2;

	ret = rt1711_i2c_read16(tcpc, TCPC_V10_REG_ALERT_MASK);
	if (ret < 0)
		return ret;

	*mask = (uint16_t) ret;

	ret = rt1711_i2c_read8(tcpc, RT1711H_REG_RT_MASK);
	if (ret < 0)
		return ret;

	v2 = (uint8_t) ret;
	*mask |= v2 << 16;

	return 0;
}

static int rt1711_get_alert_status_and_mask(struct tcpc_device *tcpc,
					    uint32_t *alert, uint32_t *mask)
{
	struct rt1711_chip *chip = tcpc_get_dev_data(tcpc);
	int ret;
	uint8_t buf[4] = {0};

	ret = rt1711_block_read(chip->client, TCPC_V10_REG_ALERT, 4, buf);
	if (ret < 0)
		return ret;
	*alert = le16_to_cpu(*(uint16_t *)&buf[0]);
	*mask = le16_to_cpu(*(uint16_t *)&buf[2]);

	ret = rt1711_block_read(chip->client, RT1711H_REG_RT_INT, 2, buf);
	if (ret < 0)
		return ret;
	*alert |= buf[0] << 16;
	*mask |= buf[1] << 16;

	return 0;
}

static int rt1711_get_power_status(struct tcpc_device *tcpc)
{
	int ret;

	ret = rt1711_i2c_read8(tcpc, TCPC_V10_REG_POWER_STATUS);
	if (ret < 0)
		return ret;
	tcpc->vbus_present = !!(ret & TCPC_V10_REG_POWER_STATUS_VBUS_PRES);

	ret = rt1711_i2c_read8(tcpc, RT1711H_REG_RT_STATUS);
	if (ret < 0)
		return ret;
	tcpc->vbus_safe0v = !!(ret & RT1711H_REG_VBUS_80);
	return 0;
}

static int rt1711_get_fault_status(struct tcpc_device *tcpc, uint8_t *status)
{
	int ret;

	ret = rt1711_i2c_read8(tcpc, TCPC_V10_REG_FAULT_STATUS);
	if (ret < 0)
		return ret;
	*status = (uint8_t) ret;
	return 0;
}

static int rt1711_get_cc(struct tcpc_device *tcpc, int *cc1, int *cc2)
{
	int status, role_ctrl, cc_role;
	bool act_as_sink, act_as_drp;

	status = rt1711_i2c_read8(tcpc, TCPC_V10_REG_CC_STATUS);
	if (status < 0)
		return status;

	if (status & TCPC_V10_REG_CC_STATUS_DRP_TOGGLING) {
		*cc1 = TYPEC_CC_DRP_TOGGLING;
		*cc2 = TYPEC_CC_DRP_TOGGLING;
		return 0;
	}
	*cc1 = TCPC_V10_REG_CC_STATUS_CC1(status);
	*cc2 = TCPC_V10_REG_CC_STATUS_CC2(status);

	role_ctrl = rt1711_i2c_read8(tcpc, TCPC_V10_REG_ROLE_CTRL);
	if (role_ctrl < 0)
		return role_ctrl;

	act_as_drp = TCPC_V10_REG_ROLE_CTRL_DRP & role_ctrl;

	if (act_as_drp) {
		act_as_sink = TCPC_V10_REG_CC_STATUS_DRP_RESULT(status);
	} else {
		if (tcpc->typec_polarity)
			cc_role = TCPC_V10_REG_CC_STATUS_CC2(role_ctrl);
		else
			cc_role = TCPC_V10_REG_CC_STATUS_CC1(role_ctrl);
		if (cc_role == TYPEC_CC_RP)
			act_as_sink = false;
		else
			act_as_sink = true;
	}

	/*
	 * If status is not open, then OR in termination to convert to
	 * enum tcpc_cc_voltage_status.
	 */
	if (*cc1 != TYPEC_CC_VOLT_OPEN)
		*cc1 |= (act_as_sink << 2);

	if (*cc2 != TYPEC_CC_VOLT_OPEN)
		*cc2 |= (act_as_sink << 2);

	rt1711h_init_cc_params(tcpc,
		(uint8_t)tcpc->typec_polarity ? *cc2 : *cc1);

	return 0;
}

static int rt1711_enable_vsafe0v_detect(
	struct tcpc_device *tcpc, bool enable)
{
	return (enable ? rt1711_i2c_set_bits : rt1711_i2c_clr_bits)
		(tcpc, RT1711H_REG_RT_MASK, RT1711H_REG_M_VBUS_80);
}

static int rt1711_set_cc(struct tcpc_device *tcpc, int pull)
{
	int ret = 0;
	uint8_t data = 0;
	int rp_lvl = TYPEC_CC_PULL_GET_RP_LVL(pull), pull1, pull2;

	RT1711_INFO("%d\n", pull);
	pull = TYPEC_CC_PULL_GET_RES(pull);
	if (pull == TYPEC_CC_DRP) {
		data = TCPC_V10_REG_ROLE_CTRL_RES_SET(1, rp_lvl, TYPEC_CC_RD,
						      TYPEC_CC_RD);
		ret = rt1711_i2c_write8(tcpc, TCPC_V10_REG_ROLE_CTRL, data);
		if (ret < 0)
			return ret;
		ret = rt1711_command(tcpc, TCPM_CMD_LOOK_CONNECTION);
	} else {
#if IS_ENABLED(CONFIG_USB_POWER_DELIVERY)
		if (pull == TYPEC_CC_RD && tcpc->pd_wait_pr_swap_complete)
			rt1711h_init_cc_params(tcpc, TYPEC_CC_VOLT_SNK_DFT);
#endif	/* CONFIG_USB_POWER_DELIVERY */

		pull1 = pull2 = pull;

		if (pull == TYPEC_CC_RP &&
		    tcpc->typec_state == typec_attached_src) {
			if (tcpc->typec_polarity)
				pull1 = TYPEC_CC_OPEN;
			else
				pull2 = TYPEC_CC_OPEN;
		}
		data = TCPC_V10_REG_ROLE_CTRL_RES_SET(0, rp_lvl, pull1, pull2);
		ret = rt1711_i2c_write8(tcpc, TCPC_V10_REG_ROLE_CTRL, data);
	}
	return ret;
}

static int rt1711_set_polarity(struct tcpc_device *tcpc, int polarity)
{
	int data;

	if (polarity >= 0 && polarity < ARRAY_SIZE(tcpc->typec_remote_cc)) {
		data = rt1711h_init_cc_params(tcpc,
			tcpc->typec_remote_cc[polarity]);
		if (data)
			return data;
	}

	return (polarity ? rt1711_i2c_set_bits : rt1711_i2c_clr_bits)
		(tcpc, TCPC_V10_REG_TCPC_CTRL,
		 TCPC_V10_REG_TCPC_CTRL_PLUG_ORIENT);
}

static int rt1711_set_vconn(struct tcpc_device *tcpc, int enable)
{
	int rv;
	struct rt1711_chip *chip = tcpc_get_dev_data(tcpc);

	chip->vconn_en = !!enable;
	rv = rt1711h_idle_ctrl(tcpc);
	if (rv < 0)
		return rv;

	return (enable ? rt1711_i2c_set_bits : rt1711_i2c_clr_bits)
		(tcpc, TCPC_V10_REG_POWER_CTRL, TCPC_V10_REG_POWER_CTRL_VCONN);
}

static int rt1711_set_low_power_mode(
		struct tcpc_device *tcpc, bool en, int pull)
{
	int ret = 0;
	uint8_t data;
	struct rt1711_chip *chip = tcpc_get_dev_data(tcpc);

	chip->lpm_en = !!en;
	ret = rt1711h_idle_ctrl(tcpc);
	if (ret < 0)
		return ret;
	ret = rt1711_enable_vsafe0v_detect(tcpc, !en);
	if (ret < 0)
		return ret;
	if (en) {
		data = RT1711H_REG_BMCIO_LPEN;

		if (TYPEC_CC_PULL_GET_RES(pull) == TYPEC_CC_RP)
			data |= RT1711H_REG_BMCIO_LPRPRD;

#if CONFIG_TYPEC_CAP_NORP_SRC
		data |= RT1711H_REG_BMCIO_BG_EN | RT1711H_REG_VBUS_DET_EN;
#endif
	} else {
		data = RT1711H_REG_BMCIO_BG_EN |
			RT1711H_REG_VBUS_DET_EN | RT1711H_REG_BMCIO_OSC_EN;
	}

	return rt1711_i2c_write8(tcpc, RT1711H_REG_BMC_CTRL, data);
}

static int rt1711_tcpc_deinit(struct tcpc_device *tcpc)
{
#if IS_ENABLED(CONFIG_RT_REGMAP)
	struct rt1711_chip *chip = tcpc_get_dev_data(tcpc);
#endif /* CONFIG_RT_REGMAP */
	int cc1 = TYPEC_CC_VOLT_OPEN, cc2 = TYPEC_CC_VOLT_OPEN;

	rt1711_get_cc(tcpc, &cc1, &cc2);
	if (cc1 != TYPEC_CC_DRP_TOGGLING &&
	    (cc1 != TYPEC_CC_VOLT_OPEN || cc2 != TYPEC_CC_VOLT_OPEN)) {
	rt1711_set_cc(tcpc, TYPEC_CC_OPEN);
		usleep_range(20000, 30000);
	}
	rt1711_i2c_write8(tcpc, RT1711H_REG_SWRESET, 1);
	chip->is_deinit = true;
#if IS_ENABLED(CONFIG_RT_REGMAP)
	rt_regmap_cache_reload(chip->m_dev);
#endif /* CONFIG_RT_REGMAP */

	return 0;
}

#if IS_ENABLED(CONFIG_USB_POWER_DELIVERY)
static int rt1711_set_msg_header(
	struct tcpc_device *tcpc, uint8_t power_role, uint8_t data_role)
{
	uint8_t msg_hdr = TCPC_V10_REG_MSG_HDR_INFO_SET(
		data_role, power_role);

	return rt1711_i2c_write8(
		tcpc, TCPC_V10_REG_MSG_HDR_INFO, msg_hdr);
}

static int rt1711_protocol_reset(struct tcpc_device *tcpc)
{
	rt1711_i2c_write8(tcpc, RT1711H_REG_PRL_FSM_RESET, 0);
	udelay(20);
	rt1711_i2c_write8(tcpc, RT1711H_REG_PRL_FSM_RESET, 1);
	return 0;
}

static int rt1711_set_rx_enable(struct tcpc_device *tcpc, uint8_t enable)
{
	int ret = 0;

	if (enable)
		ret = rt1711h_set_clock_gating(tcpc, false);

	if (ret == 0)
		ret = rt1711_i2c_write8(tcpc, TCPC_V10_REG_RX_DETECT, enable);

	if ((ret == 0) && (!enable)) {
		rt1711_protocol_reset(tcpc);
		ret = rt1711h_set_clock_gating(tcpc, true);
	}

	return ret;
}

static int rt1711_get_message(struct tcpc_device *tcpc, uint32_t *payload,
			uint16_t *msg_head, enum tcpm_transmit_type *frame_type)
{
	struct rt1711_chip *chip = tcpc_get_dev_data(tcpc);
	int rv = 0;
	uint8_t cnt = 0, buf[32];

	rv = rt1711_block_read(chip->client, TCPC_V10_REG_RX_BYTE_CNT,
			       4, buf);
	if (rv < 0)
		return rv;

	cnt = buf[0];
	*frame_type = buf[1];
	*msg_head = le16_to_cpu(*(uint16_t *)&buf[2]);

	if (cnt <= 3)
		return rv;

	cnt -= 3; /* FRAME_TYPE + HEADER */
	if (cnt > sizeof(buf) - 4)
		cnt = sizeof(buf) - 4;
	rv = rt1711_block_read(chip->client, TCPC_V10_REG_RX_DATA,
			       cnt, buf + 4);
	if (rv < 0)
		return rv;
	memcpy(payload, buf + 4, cnt);

	return rv;
}

#if CONFIG_USB_PD_RETRY_CRC_DISCARD
static int rt1711_retransmit(struct tcpc_device *tcpc)
{
	return rt1711_i2c_write8(tcpc, TCPC_V10_REG_TRANSMIT,
			TCPC_V10_REG_TRANSMIT_SET(
			tcpc->pd_retry_count, TCPC_TX_SOP));
}
#endif	/* CONFIG_USB_PD_RETRY_CRC_DISCARD */

#pragma pack(push, 1)
struct tcpc_transmit_packet {
	uint8_t cnt;
	uint16_t msg_header;
	uint8_t data[sizeof(uint32_t)*7];
};
#pragma pack(pop)

static int rt1711_transmit(struct tcpc_device *tcpc,
	enum tcpm_transmit_type type, uint16_t header, const uint32_t *data)
{
	struct rt1711_chip *chip = tcpc_get_dev_data(tcpc);
	int rv;
	int data_cnt;
	struct tcpc_transmit_packet packet;

	if (type < TCPC_TX_HARD_RESET) {
		data_cnt = sizeof(uint32_t) * PD_HEADER_CNT(header);

		packet.cnt = data_cnt + sizeof(uint16_t);
		packet.msg_header = header;

		if (data_cnt > 0)
			memcpy(packet.data, (uint8_t *) data, data_cnt);

		rv = rt1711_block_write(chip->client,
				TCPC_V10_REG_TX_BYTE_CNT,
				packet.cnt+1, (uint8_t *) &packet);
		if (rv < 0)
			return rv;
	}

	rv = rt1711_i2c_write8(tcpc, TCPC_V10_REG_TRANSMIT,
			TCPC_V10_REG_TRANSMIT_SET(
			tcpc->pd_retry_count, type));
	return rv;
}

static int rt1711_set_bist_test_mode(struct tcpc_device *tcpc, bool en)
{
	return (en ? rt1711_i2c_set_bits : rt1711_i2c_clr_bits)
		(tcpc, TCPC_V10_REG_TCPC_CTRL,
		 TCPC_V10_REG_TCPC_CTRL_BIST_TEST_MODE);
}
#endif /* CONFIG_USB_POWER_DELIVERY */

static struct tcpc_ops rt1711_tcpc_ops = {
	.init = rt1711_tcpc_init,
	.alert_status_clear = rt1711_alert_status_clear,
	.fault_status_clear = rt1711_fault_status_clear,
	.set_alert_mask = rt1711_set_alert_mask,
	.get_alert_mask = rt1711_get_alert_mask,
	.get_alert_status_and_mask = rt1711_get_alert_status_and_mask,
	.get_power_status = rt1711_get_power_status,
	.get_fault_status = rt1711_get_fault_status,
	.get_cc = rt1711_get_cc,
	.set_cc = rt1711_set_cc,
	.set_polarity = rt1711_set_polarity,
	.set_vconn = rt1711_set_vconn,
	.deinit = rt1711_tcpc_deinit,

	.set_low_power_mode = rt1711_set_low_power_mode,

#if IS_ENABLED(CONFIG_USB_POWER_DELIVERY)
	.set_msg_header = rt1711_set_msg_header,
	.set_rx_enable = rt1711_set_rx_enable,
	.protocol_reset = rt1711_protocol_reset,
	.get_message = rt1711_get_message,
	.transmit = rt1711_transmit,
	.set_bist_test_mode = rt1711_set_bist_test_mode,
#endif	/* CONFIG_USB_POWER_DELIVERY */

#if CONFIG_USB_PD_RETRY_CRC_DISCARD
	.retransmit = rt1711_retransmit,
#endif	/* CONFIG_USB_PD_RETRY_CRC_DISCARD */
};

static int rt_parse_dt(struct rt1711_chip *chip, struct device *dev)
{
	struct device_node *np = dev->of_node;
	int ret = 0;

	pr_info("%s\n", __func__);

#if !IS_ENABLED(CONFIG_MTK_GPIO) || IS_ENABLED(CONFIG_MTK_GPIOLIB_STAND)
	ret = of_get_named_gpio(np, "rt1711pd,intr-gpio", 0);
	if (ret < 0)
		ret = of_get_named_gpio(np, "rt1711pd,intr_gpio", 0);

	if (ret < 0)
		pr_err("%s no intr_gpio info\n", __func__);
	else
		chip->irq_gpio = ret;
#else
	ret = of_property_read_u32(np, "rt1711pd,intr-gpio-num", &chip->irq_gpio) ?
	      of_property_read_u32(np, "rt1711pd,intr_gpio_num", &chip->irq_gpio) : 0;
	if (ret < 0)
		pr_err("%s no intr_gpio info\n", __func__);
#endif /* !CONFIG_MTK_GPIO || CONFIG_MTK_GPIOLIB_STAND */
	return ret < 0 ? ret : 0;
}

static int rt1711_tcpcdev_init(struct rt1711_chip *chip, struct device *dev)
{
	struct tcpc_desc *desc;
	struct tcpc_device *tcpc = NULL;
	struct device_node *np = dev->of_node;
	u32 val, len;
	const char *name = "default";

	dev_info(dev, "%s\n", __func__);

	desc = devm_kzalloc(dev, sizeof(*desc), GFP_KERNEL);
	if (!desc)
		return -ENOMEM;
	if (of_property_read_u32(np, "rt-tcpc,role-def", &val) >= 0 ||
	    of_property_read_u32(np, "rt-tcpc,role_def", &val) >= 0) {
		if (val >= TYPEC_ROLE_NR)
			desc->role_def = TYPEC_ROLE_DRP;
		else
			desc->role_def = val;
	} else {
		dev_info(dev, "use default Role DRP\n");
		desc->role_def = TYPEC_ROLE_DRP;
	}

	if (of_property_read_u32(np, "rt-tcpc,rp-level", &val) >= 0 ||
	    of_property_read_u32(np, "rt-tcpc,rp_level", &val) >= 0) {
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

	if (of_property_read_u32(np, "rt-tcpc,vconn-supply", &val) >= 0 ||
	    of_property_read_u32(np, "rt-tcpc,vconn_supply", &val) >= 0) {
		if (val >= TCPC_VCONN_SUPPLY_NR)
			desc->vconn_supply = TCPC_VCONN_SUPPLY_ALWAYS;
		else
			desc->vconn_supply = val;
	} else {
		dev_info(dev, "use default VconnSupply\n");
		desc->vconn_supply = TCPC_VCONN_SUPPLY_ALWAYS;
	}

	if (of_property_read_string(np, "rt-tcpc,name",
				(const char **)&name) < 0) {
		dev_info(dev, "use default name\n");
	}

	len = strlen(name);
	desc->name = kzalloc(len+1, GFP_KERNEL);
	if (!desc->name)
		return -ENOMEM;

	strlcpy((char *)desc->name, name, len+1);

	chip->tcpc_desc = desc;

	tcpc = tcpc_device_register(dev, desc, &rt1711_tcpc_ops, chip);
	if (IS_ERR_OR_NULL(tcpc))
		return -EINVAL;
	chip->tcpc = tcpc;

#if CONFIG_USB_PD_DISABLE_PE
	tcpc->disable_pe = of_property_read_bool(np, "rt-tcpc,disable-pe") ||
				 of_property_read_bool(np, "rt-tcpc,disable_pe");
#endif	/* CONFIG_USB_PD_DISABLE_PE */

	tcpc->tcpc_flags = TCPC_FLAGS_VCONN_SAFE5V_ONLY;

#if CONFIG_USB_PD_RETRY_CRC_DISCARD
	if (chip->chip_id > RT1715_DID_D)
		tcpc->tcpc_flags |= TCPC_FLAGS_RETRY_CRC_DISCARD;
#endif  /* CONFIG_USB_PD_RETRY_CRC_DISCARD */

#if CONFIG_USB_PD_REV30
	if (chip->chip_id >= RT1715_DID_D)
		tcpc->tcpc_flags |= TCPC_FLAGS_PD_REV30;

	if (tcpc->tcpc_flags & TCPC_FLAGS_PD_REV30)
		dev_info(dev, "PD_REV30\n");
	else
		dev_info(dev, "PD_REV20\n");
#endif	/* CONFIG_USB_PD_REV30 */
	tcpc->tcpc_flags |= TCPC_FLAGS_ALERT_V10;

	return 0;
}

#define RICHTEK_1711_VID	0x29cf
#define RICHTEK_1711_PID	0x1711

static inline int rt1711h_check_revision(struct i2c_client *client)
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
	if (data != RICHTEK_1711_VID) {
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
	if (data != RICHTEK_1711_PID) {
		dev_info(&client->dev, "%s failed, PID=0x%04x\n",
				       __func__, data);
		return -ENODEV;
	}

	ret = i2c_smbus_write_byte_data(client, RT1711H_REG_SWRESET, 0x01);
	if (ret < 0)
		return ret;
	usleep_range(1000, 2000);

	ret = i2c_smbus_read_i2c_block_data(client, TCPC_V10_REG_DID, 2,
					    (u8 *)&data);
	if (ret < 0) {
		dev_notice(&client->dev, "read Device ID fail(%d)\n", ret);
		return ret;
	}
	return le16_to_cpu(data);
}

static int rt1711_probe(struct i2c_client *client)
{
	struct rt1711_chip *chip;
	int ret = 0, chip_id;
	bool use_dt = client->dev.of_node;

	pr_info("%s (%s)\n", __func__, RT1711H_DRV_VERSION);
	if (i2c_check_functionality(client->adapter,
			I2C_FUNC_SMBUS_I2C_BLOCK | I2C_FUNC_SMBUS_BYTE_DATA))
		pr_info("I2C functionality : OK...\n");
	else
		pr_info("I2C functionality check : failuare...\n");

	chip_id = rt1711h_check_revision(client);
	if (chip_id < 0)
		return chip_id;

	chip = devm_kzalloc(&client->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	if (use_dt) {
		ret = rt_parse_dt(chip, &client->dev);
		if (ret < 0)
			return ret;
	} else {
		dev_err(&client->dev, "no dts node\n");
		return -ENODEV;
	}
	chip->dev = &client->dev;
	chip->client = client;
	i2c_set_clientdata(client, chip);
	chip->chip_id = chip_id;
	pr_info("rt1711h_chipID = 0x%0x\n", chip_id);
	chip->vconn_en = false;
	chip->lpm_en = false;

	ret = rt1711_regmap_init(chip);
	if (ret < 0) {
		dev_err(chip->dev, "rt1711 regmap init fail\n");
		goto err_regmap_init;
	}

	ret = rt1711_tcpcdev_init(chip, chip->dev);
	if (ret < 0) {
		dev_notice(chip->dev, "rt1711 tcpc dev init fail\n");
		goto err_tcpc_reg;
	}

	ret = rt1711_init_alert(chip->tcpc);
	if (ret < 0) {
		pr_err("rt1711 init alert fail\n");
		goto err_irq_init;
	}

	pr_info("%s probe OK!\n", __func__);
	return 0;

err_irq_init:
	tcpc_device_unregister(chip->dev, chip->tcpc);
err_tcpc_reg:
	rt1711_regmap_deinit(chip);
err_regmap_init:
	return ret;
}

static void rt1711_remove(struct i2c_client *client)
{
	struct rt1711_chip *chip = i2c_get_clientdata(client);

	disable_irq(chip->irq);
		tcpc_device_unregister(chip->dev, chip->tcpc);
		rt1711_regmap_deinit(chip);
}

static void rt1711_shutdown(struct i2c_client *client)
{
	struct rt1711_chip *chip = i2c_get_clientdata(client);

	disable_irq(chip->irq);
	tcpm_shutdown(chip->tcpc);
}

#if IS_ENABLED(CONFIG_PM_SLEEP)
static bool rt1711_check_reset_and_reinit(struct rt1711_chip *chip)
{
	struct tcpc_device *tcpc = chip->tcpc;
	bool reinited = false;
	int ret = 0;

	tcpci_lock_typec(tcpc);
	ret = rt1711_i2c_read16(tcpc, RT1711H_REG_DRP_DUTY_CTRL);
	if (ret < 0)
		goto out;
	if ((ret & 0x3FF) == TCPC_NORMAL_RP_DUTY)
		goto out;
	rt1711_tcpc_init(tcpc, true);
	tcpc_typec_error_recovery(tcpc);
	reinited = true;
out:
	tcpci_unlock_typec(tcpc);
	return reinited;
}

static int rt1711_suspend(struct device *dev)
{
	struct rt1711_chip *chip = dev_get_drvdata(dev);
	int ret = 0;

	dev_info(dev, "%s irq_gpio = %d\n",
		      __func__, gpio_get_value(chip->irq_gpio));

	if (rt1711_check_reset_and_reinit(chip))
		return -EBUSY;

	ret = tcpm_suspend(chip->tcpc);
	if (ret)
		return ret;
	disable_irq(chip->irq);
	return 0;
}

static int rt1711_check_suspend_pending(struct device *dev)
{
	struct rt1711_chip *chip = dev_get_drvdata(dev);

	dev_info(dev, "%s irq_gpio = %d\n",
		      __func__, gpio_get_value(chip->irq_gpio));

	return tcpm_check_suspend_pending(chip->tcpc);
}

static int rt1711_resume(struct device *dev)
{
	struct rt1711_chip *chip = dev_get_drvdata(dev);

	dev_info(dev, "%s irq_gpio = %d\n",
		      __func__, gpio_get_value(chip->irq_gpio));

	enable_irq(chip->irq);
	tcpm_resume(chip->tcpc);

	return 0;
}

static int rt1711_restore(struct device *dev)
{
	struct rt1711_chip *chip = dev_get_drvdata(dev);

	dev_info(dev, "%s irq_gpio = %d\n",
		      __func__, gpio_get_value(chip->irq_gpio));

	enable_irq(chip->irq);
	tcpm_resume(chip->tcpc);
	rt1711_check_reset_and_reinit(chip);

	return 0;
}
#endif	/* CONFIG_PM_SLEEP */

static const struct dev_pm_ops rt1711_pm_ops = {
#if IS_ENABLED(CONFIG_PM_SLEEP)
	.prepare = rt1711_check_suspend_pending,
	.suspend = rt1711_suspend,
	.resume = rt1711_resume,
	.freeze = rt1711_suspend,
	.thaw = rt1711_resume,
	.poweroff = rt1711_suspend,
	.restore = rt1711_restore,
#endif	/* CONFIG_PM_SLEEP */
	SET_LATE_SYSTEM_SLEEP_PM_OPS(rt1711_check_suspend_pending, NULL)
	SET_NOIRQ_SYSTEM_SLEEP_PM_OPS(rt1711_check_suspend_pending, NULL)
};

static const struct of_device_id rt1711_of_match_table[] = {
	{.compatible = "richtek,rt1711h",},
	{.compatible = "richtek,rt1715",},
	{.compatible = "richtek,rt1716",},
	{},
};
MODULE_DEVICE_TABLE(of, rt1711_of_match_table);

static struct i2c_driver rt1711_driver = {
	.probe = rt1711_probe,
	.remove = rt1711_remove,
	.shutdown = rt1711_shutdown,
	.driver = {
		.name = "rt1711h",
		.owner = THIS_MODULE,
		.of_match_table = rt1711_of_match_table,
		.pm = &rt1711_pm_ops,
	},
};

static int __init rt1711_init(void)
{
	struct device_node *np;

	pr_info("%s (%s)\n", __func__, RT1711H_DRV_VERSION);
	np = of_find_node_by_name(NULL, "rt1711h");
	pr_info("%s rt1711h node %s\n", __func__,
		np == NULL ? "not found" : "found");

	return i2c_add_driver(&rt1711_driver);
}
subsys_initcall(rt1711_init);

static void __exit rt1711_exit(void)
{
	i2c_del_driver(&rt1711_driver);
}
module_exit(rt1711_exit);

MODULE_AUTHOR("Jeff Chang <jeff_chang@richtek.com>");
MODULE_DESCRIPTION("RT1711 TCPC Driver");
MODULE_VERSION(RT1711H_DRV_VERSION);
#endif	/* CONFIG_TCPC_CLASS */
MODULE_LICENSE("GPL");

/**** Release Note ****
 * 2.0.9_MTK
 * (1) Do I2C/IO transactions when system resumed
 * (2) Reduce log printing
 * (3) Control CC Open in the deinit ops
 *
 * 2.0.8_MTK
 * (1) Decrease the I2C/IO transactions
 * (2) Remove the old way of get_power_status()
 * (3) Add CONFIG_TYPEC_SNK_ONLY_WHEN_SUSPEND
 *
 * 2.0.7_MTK
 * (1) Revise suspend/resume flow for IRQ
 * (2) Revise auto idle mode
 *
 * 2.0.6_MTK
 * (1) Revert Vconn OC to shutdown mode
 * (2) Revise IRQ handling
 *
 * 2.0.5_MTK
 * (1) Utilize rt-regmap to reduce I2C accesses
 * (2) Decrease VBUS present threshold (VBUS_CAL) by 60mV (2LSBs)
 *
 * 2.0.4_MTK
 * (1) Mask vSafe0V IRQ before entering low power mode
 * (2) Disable auto idle mode before entering low power mode
 * (3) Reset Protocol FSM and clear RX alerts twice before clock gating
 *
 * 2.0.3_MTK
 * (1) Single Rp as Attatched.SRC for Ellisys TD.4.9.4
 *
 * 2.0.2_MTK
 * (1) Replace wake_lock with wakeup_source
 * (2) Move down the shipping off
 * (3) Add support for NoRp.SRC
 * (4) Reg0x71[7] = 1'b1 to workaround unstable VDD Iq in low power mode
 * (5) Add get_alert_mask of tcpc_ops
 *
 * 2.0.1_MTK
 * First released PD3.0 Driver on MTK platform
 */
