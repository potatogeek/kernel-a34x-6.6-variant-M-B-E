// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 MediaTek Inc.
 *
 */

#include <linux/ctype.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/sched.h>
#include <linux/sched/clock.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <leds-mtk.h>


/****************************************************************************
 * variables
 ***************************************************************************/
#undef pr_fmt
#define pr_fmt(fmt) KBUILD_MODNAME " %s(%d) :" fmt, __func__, __LINE__

static int mtk_set_brightness(struct led_classdev *led_cdev,
					 enum led_brightness brightness);

struct mt_leds_desp_info {
	int lens;
	struct led_desp *leds[];
};

static DEFINE_MUTEX(leds_mutex);
static BLOCKING_NOTIFIER_HEAD(mtk_leds_chain_head);

struct mt_leds_desp_info *leds_info;

int mtk_leds_register_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&mtk_leds_chain_head, nb);
}
EXPORT_SYMBOL_GPL(mtk_leds_register_notifier);

int mtk_leds_unregister_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&mtk_leds_chain_head, nb);
}
EXPORT_SYMBOL_GPL(mtk_leds_unregister_notifier);

int mt_leds_call_notifier(unsigned long action, void *data)
{
	return blocking_notifier_call_chain(&mtk_leds_chain_head, action, data);
}
EXPORT_SYMBOL_GPL(mt_leds_call_notifier);

static int get_desp_index(int id)
{
	int i = 0;
	if (IS_ERR_OR_NULL(leds_info)) {
		pr_info("Get a null pointer exception of leds_info!!!");
		return -1;
	}
	while (i < leds_info->lens) {
		pr_debug("leds %d connector id is %d", i, leds_info->leds[i]->connector_id);
		if (IS_ERR_OR_NULL(leds_info->leds[i])) {
			pr_info("Get a null pointer exception of leds_info->leds[%d]!!!", i);
			return -1;
		}
		if (leds_info->leds[i]->connector_id < 0) {
			struct mt_led_data *led_dat = container_of(leds_info->leds[i],
				struct mt_led_data, desp);
			led_dat->mtk_conn_id_get(led_dat, led_dat->desp.index);
			leds_info->leds[i]->connector_id = led_dat->conf.connector_id;
		}

		if (id == leds_info->leds[i]->connector_id)
			return i;
		i++;
	}
	return -1;
}

static int  __maybe_unused call_notifier(int event, struct led_conf_info *led_conf)
{
	int err = 0;

	if (led_conf->flags & LED_MT_BRIGHTNESS_CHANGED) {
		err = mt_leds_call_notifier(event, led_conf);
		if (err)
			pr_info("Error notifier_call_chain error\n");
	}
	return err;
}

static ssize_t logic_max_brightness_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct led_conf_info *led_conf =
		container_of(led_cdev, struct led_conf_info, cdev);
	return sprintf(buf, "%u\n", led_conf->logic_max_brightness);
}

static ssize_t logic_max_brightness_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	unsigned long state;
	ssize_t ret;
	struct led_conf_info *led_conf =
			container_of(led_cdev, struct led_conf_info, cdev);
	mutex_lock(&led_cdev->led_access);
	if (led_sysfs_is_disabled(led_cdev)) {
		ret = -EBUSY;
		goto unlock;
	}
	ret = kstrtoul(buf, 10, &state);
	if (ret)
		goto unlock;
	led_conf->logic_max_brightness = state;
	led_conf->cdev.max_brightness = state;
	ret = size;
unlock:
	mutex_unlock(&led_cdev->led_access);
	return ret;
}
static DEVICE_ATTR_RW(logic_max_brightness);

static ssize_t min_brightness_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct led_conf_info *led_conf =
		container_of(led_cdev, struct led_conf_info, cdev);

	return sprintf(buf, "%u\n", led_conf->min_brightness);
}
static DEVICE_ATTR_RO(min_brightness);

static ssize_t max_hw_brightness_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct led_conf_info *led_conf =
		container_of(led_cdev, struct led_conf_info, cdev);

	return sprintf(buf, "%u\n", led_conf->max_hw_brightness);
}
static DEVICE_ATTR_RO(max_hw_brightness);

static ssize_t min_hw_brightness_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct led_conf_info *led_conf =
		container_of(led_cdev, struct led_conf_info, cdev);

	return sprintf(buf, "%u\n", led_conf->min_hw_brightness);
}
static DEVICE_ATTR_RO(min_hw_brightness);

static ssize_t led_mode_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct led_conf_info *led_conf =
		container_of(led_cdev, struct led_conf_info, cdev);

	return sprintf(buf, "%u\n", led_conf->mode);
}
static DEVICE_ATTR_RO(led_mode);
static ssize_t connector_id_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct led_conf_info *led_conf =
		container_of(led_cdev, struct led_conf_info, cdev);


	if (led_conf->connector_id <= 0) {
		struct mt_led_data *led_dat =
			container_of(led_conf, struct mt_led_data, conf);
		led_dat->mtk_conn_id_get(led_dat, led_dat->desp.index);
	}

	return sprintf(buf, "%u\n", led_conf->connector_id);
}
static DEVICE_ATTR_RO(connector_id);

#ifdef CONFIG_LEDS_MT_BRIGHTNESS_HW_CHANGED
static ssize_t mt_brightness_hw_changed_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct led_conf_info *led_conf =
		container_of(led_cdev, struct led_conf_info, cdev);

	if (led_conf->brightness_hw_changed == -1)
		return -ENODATA;

	return sprintf(buf, "%u\n", led_conf->brightness_hw_changed);
}

static DEVICE_ATTR_RO(mt_brightness_hw_changed);

static int mt_leds_add_brightness_hw_changed(struct led_conf_info *led_conf)
{
	struct device *dev = led_conf->cdev.dev;
	int ret = 0;

	if (strstr(led_conf->cdev.name, "test-backlight"))
		return ret;

	ret = device_create_file(dev, &dev_attr_mt_brightness_hw_changed);
	if (ret) {
		pr_info("Error creating mt_brightness_hw_changed\n");
		return ret;
	}

	led_conf->brightness_hw_changed_kn =
		sysfs_get_dirent(dev->kobj.sd, "mt_brightness_hw_changed");
	if (!led_conf->brightness_hw_changed_kn) {
		pr_info("Error getting mt_brightness_hw_changed kn\n");
		device_remove_file(dev, &dev_attr_mt_brightness_hw_changed);
		return -ENXIO;
	}

	return ret;
}

static void mt_leds_remove_brightness_hw_changed(struct led_conf_info *led_conf)
{
	if (led_conf->brightness_hw_changed_kn)
		sysfs_put(led_conf->brightness_hw_changed_kn);

	device_remove_file(led_conf->cdev.dev, &dev_attr_mt_brightness_hw_changed);
}

void mt_leds_notify_brightness_hw_changed(int conn_id,
					       enum led_brightness brightness)
{
	struct mt_led_data *led_dat;
	int index;

	index = get_desp_index(conn_id);
	if (index < 0) {
		pr_notice("can not find leds by led_desp connector id %d", conn_id);
		return;
	}
	led_dat = container_of(leds_info->leds[index],
		struct mt_led_data, desp);

	if (WARN_ON(!led_dat->conf.brightness_hw_changed_kn))
		return;

	led_dat->conf.brightness_hw_changed = brightness;
	sysfs_notify_dirent(led_dat->conf.brightness_hw_changed_kn);
}
EXPORT_SYMBOL_GPL(mt_leds_notify_brightness_hw_changed);
#else
static int mt_leds_add_brightness_hw_changed(struct led_conf_info *led_conf)
{
	return 0;
}
static void mt_leds_remove_brightness_hw_changed(struct led_conf_info *led_conf)
{
}
#endif

static ssize_t led_type_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct led_conf_info *led_conf =
		container_of(led_cdev, struct led_conf_info, cdev);

	return sprintf(buf, "%u\n", led_conf->led_type);
}

static ssize_t led_type_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct led_conf_info *led_conf =
		container_of(led_cdev, struct led_conf_info, cdev);
	unsigned long state;
	ssize_t ret;

	mutex_lock(&led_cdev->led_access);

	if (led_sysfs_is_disabled(led_cdev)) {
		ret = -EBUSY;
		goto unlock;
	}

	ret = kstrtoul(buf, 10, &state);
	if (ret)
		goto unlock;

	led_conf->led_type = min_t(unsigned long, state, LED_TYPE_MAX);
	call_notifier(LED_TYPE_CHANGED, led_conf);

	ret = size;
unlock:
	mutex_unlock(&led_cdev->led_access);
	return ret;
}
static DEVICE_ATTR_RW(led_type);


/****************************************************************************
 * DEBUG MACROS
 ***************************************************************************/

static void led_debug_log(struct mt_led_data *s_led,
		int level, int mappingLevel)
{
	unsigned long cur_time_mod = 0;
	unsigned long long cur_time_display = 0;
	int ret = 0;

	s_led->debug.current_t = sched_clock();
	cur_time_display = s_led->debug.current_t;
	do_div(cur_time_display, 1000000);
	cur_time_mod = do_div(cur_time_display, 1000);

	ret = snprintf(s_led->debug.buffer + strlen(s_led->debug.buffer),
		4095 - strlen(s_led->debug.buffer),
		"T:%lld.%ld,L:%d L:%d map:%d    ",
		cur_time_display, cur_time_mod,
		s_led->conf.cdev.brightness, level, mappingLevel);

	s_led->debug.count++;

	if (ret < 0 || ret >= (4096 - strlen(s_led->debug.buffer))) {
		pr_info("print log error!");
		s_led->debug.count = 5;
	}

	if (level == 0 || s_led->debug.count >= 5 ||
		(s_led->debug.current_t - s_led->debug.last_t) > 1000000000) {
		pr_info("%s", s_led->debug.buffer);
		s_led->debug.count = 0;
		s_led->debug.buffer[strlen("[Light] Set directly ") +
			strlen(s_led->conf.cdev.name)] = '\0';
	}

	s_led->debug.last_t = sched_clock();
}

/****************************************************************************
 * driver functions
 ***************************************************************************/

static int brightness_maptolevel(struct led_conf_info *led_conf, int brightness)
{
	return (((led_conf->max_hw_brightness) * brightness
				+ ((led_conf->cdev.max_brightness) / 2))
				/ (led_conf->cdev.max_brightness));
}

static int mtk_set_hw_brightness(struct mt_led_data *led_dat, int brightness,
		unsigned int params, unsigned int params_flag)
{

	int ret = 0;

	if (brightness != 0) {
		brightness = min(brightness, led_dat->conf.limit_hw_brightness);
		brightness = max(brightness, led_dat->conf.min_hw_brightness);
	}

	if (params_flag == (1 << SET_BACKLIGHT_LEVEL) &&
		(led_dat->conf.led_type == LED_TYPE_ATOMIC ||
		brightness == led_dat->hw_brightness))
		return ret;
	pr_debug("set hw brightness(%s): %d -> %d",
		led_dat->conf.cdev.name, led_dat->hw_brightness, brightness);

	if (led_dat->conf.connector_id <= 0)
		led_dat->mtk_conn_id_get(led_dat, led_dat->desp.index);
	ret = led_dat->mtk_hw_brightness_set(led_dat, brightness, params, params_flag);
	if (ret >= 0) {
		if (brightness != led_dat->hw_brightness &&
		   (params_flag & (1 << SET_BACKLIGHT_LEVEL))) {
			led_dat->hw_brightness = brightness;
#ifdef CONFIG_LEDS_MT_BRIGHTNESS_HW_CHANGED
			mt_leds_notify_brightness_hw_changed(led_dat->conf.connector_id, brightness);
#endif
		}
	} else {
		pr_debug("set hw brightness: %d -> %d, params: %d, params_flag: %d failed!",
			led_dat->hw_brightness, brightness, params, params_flag);
	}

	return 0;
}

int mtk_leds_brightness_set(int connector_id, int level,
		unsigned int params, unsigned int params_flag)
{
	struct mt_led_data *led_dat;
	int index;

	if (level < 0) {
		pr_info("connector_id %d set invalid brightness %d", connector_id, level);
		return -EINVAL;
	}
	index = get_desp_index(connector_id);
	if (index < 0) {
		pr_info("can not find leds by led_desp connector id %d", connector_id);
		return -1;
	}
	led_dat = container_of(leds_info->leds[index],
		struct mt_led_data, desp);

	mutex_lock(&led_dat->led_access);
	if (!led_dat->conf.aal_enable) {
		//pr_debug("aal not enable, set %s %d return", name, level);
	} else {
		mtk_set_hw_brightness(led_dat, level, params, params_flag);
		led_dat->last_hw_brightness = level;
	}
	mutex_unlock(&led_dat->led_access);

	return 0;
}
EXPORT_SYMBOL(mtk_leds_brightness_set);

static int mtk_set_brightness(struct led_classdev *led_cdev,
					  enum led_brightness brightness)
{
	int trans_level = 0;

	struct led_conf_info *led_conf =
		container_of(led_cdev, struct led_conf_info, cdev);
	struct mt_led_data *led_dat =
		container_of(led_conf, struct mt_led_data, conf);

	if (led_conf->connector_id <= 0) {
		struct mt_led_data *led_dat =
			container_of(led_conf, struct mt_led_data, conf);
		led_dat->mtk_conn_id_get(led_dat, led_dat->desp.index);
	}

	if (led_dat->last_brightness == brightness)
		return 0;

	led_dat->last_brightness = brightness;

	trans_level = brightness_maptolevel(led_conf, brightness);

	led_debug_log(led_dat, brightness, trans_level);

	call_notifier(LED_BRIGHTNESS_CHANGED, led_conf);
	mutex_lock(&led_dat->led_access);
	if (!led_conf->aal_enable) {
		mtk_set_hw_brightness(led_dat, trans_level, 0, 1 << SET_BACKLIGHT_LEVEL);
		led_dat->last_hw_brightness = trans_level;
	}
	mutex_unlock(&led_dat->led_access);

	return 0;

}


/****************************************************************************
 * add API for temperature control
 ***************************************************************************/
int set_max_brightness(struct mt_led_data *led_dat, int percent, bool enable)
{
	int max_l = 0, limit_l = 0, cur_l = 0;

	if (led_dat == NULL) {
		pr_info("invalid parameter\n");
		return -1;
	}

	max_l = led_dat->conf.max_hw_brightness;
	limit_l = (percent * max_l) / 100;
	pr_info("before: name: %s, percent : %d, limit_l : %d, enable: %d",
		led_dat->conf.cdev.name, percent, limit_l, enable);
	if (enable) {
		led_dat->conf.limit_hw_brightness = limit_l;
		cur_l = min(led_dat->last_hw_brightness, limit_l);
	} else {
		led_dat->conf.limit_hw_brightness = max_l;
		cur_l = led_dat->last_hw_brightness;
	}

	if (led_dat->conf.cdev.brightness != 0)
		mtk_set_hw_brightness(led_dat, cur_l, 0, 1 << SET_BACKLIGHT_LEVEL);

	pr_info("after: name: %s, cur_l : %d, max_brightness : %d",
		led_dat->conf.cdev.name, cur_l, led_dat->conf.limit_hw_brightness);

	return 0;
}

int setMaxBrightness(int connector_id, int percent, bool enable)
{
	struct mt_led_data *led_dat;
	int i = 0, index = -1;
	bool bSetall = false;

	if (percent <= 0) {
		pr_info("connector_id %d set invalid percent %d", connector_id, percent);
		return -EINVAL;
	}

	if (-1 == connector_id)
		bSetall = true;
	else {
		index = get_desp_index(connector_id);
		if (index < 0) {
			pr_info("can not find leds by led_desp connector id %d", connector_id);
			return -1;
		}
	}

	if (!bSetall) {
		led_dat = container_of(leds_info->leds[index],
			struct mt_led_data, desp);
		set_max_brightness(led_dat, percent, enable);
	} else {
		while (i < leds_info->lens) {
			pr_info("led %d name %s\n", i, leds_info->leds[i]->name);
			if (strstr(leds_info->leds[i]->name, "lcd-backlight")) {
				led_dat = container_of(leds_info->leds[i],
					struct mt_led_data, desp);
				set_max_brightness(led_dat, percent, enable);
			}
			i++;
		}
	}

	return 0;
}
EXPORT_SYMBOL(setMaxBrightness);

int mt_leds_parse_dt(struct mt_led_data *mdev, struct fwnode_handle *fwnode)
{
	int ret = 0;
	const char *state;
	struct mt_leds_desp_info *nleds_info;

	ret = fwnode_property_read_string(fwnode, "label", &(mdev->conf.cdev.name));
	if (ret) {
		pr_info("Fail to read label property");
		mdev->conf.cdev.name = "test-backlight";
	}
	ret = fwnode_property_read_string(fwnode, "default-trigger",
		&(mdev->conf.cdev.default_trigger));
	if (ret) {
		pr_info("Fail to read default-trigger property");
		mdev->conf.cdev.default_trigger = NULL;
	}

	ret = fwnode_property_read_u32(fwnode,
		"max-hw-brightness", &(mdev->conf.max_hw_brightness));
	if (ret) {
		pr_info("No max-hw-brightness, use default value 2047");
		mdev->conf.max_hw_brightness = 2047;
	}

	ret = fwnode_property_read_u32(fwnode,
		"max-brightness", &(mdev->conf.cdev.max_brightness));
	if (ret) {
		pr_info("No max-brightness, use max_hw_brightness");
		mdev->conf.cdev.max_brightness = mdev->conf.max_hw_brightness;
	}
	mdev->conf.logic_max_brightness = mdev->conf.cdev.max_brightness;

	ret = fwnode_property_read_u32(fwnode,
		"min-hw-brightness", &(mdev->conf.min_hw_brightness));
	if (ret) {
		pr_info("No min-hw-brightness, use default value 1");
		mdev->conf.min_hw_brightness = 1;
	}
	ret = fwnode_property_read_u32(fwnode,
		"min-brightness", &(mdev->conf.min_brightness));
	if (ret) {
		pr_info("No min-brightness, use min_hw_brightness");
		mdev->conf.min_brightness = mdev->conf.min_hw_brightness;
	}
	ret = fwnode_property_read_u32(fwnode,
		"led-mode", &(mdev->conf.mode));
	if (ret) {
		pr_info("No min-brightness, use default value 1");
		mdev->conf.mode = MT_LED_MODE_CUST_BLS_I2C;
	}
	mdev->conf.limit_hw_brightness = mdev->conf.max_hw_brightness;
	ret = fwnode_property_read_string(fwnode, "default-state", &state);
	if (!ret) {
		if (!strncmp(state, "half", strlen("half")))
			mdev->conf.cdev.brightness = mdev->conf.cdev.max_brightness / 2;
		else if (!strncmp(state, "on", strlen("on")))
			mdev->conf.cdev.brightness = mdev->conf.cdev.max_brightness;
		else
			mdev->conf.cdev.brightness = 0;
	} else {
		mdev->conf.cdev.brightness = mdev->conf.cdev.max_brightness * 40 / 100;
	}

	strscpy(mdev->desp.name, mdev->conf.cdev.name,
		sizeof(mdev->desp.name));
	mdev->conf.led_type = LED_TYPE_FILE;
	mdev->desp.index = leds_info->lens;

	mdev->conf.connector_id = -1;

	nleds_info = krealloc(leds_info, sizeof(struct mt_leds_desp_info) +
		sizeof(struct led_desp *) * (leds_info->lens + 1),
		GFP_KERNEL);

	if (!nleds_info)
		return -ENOMEM;
	leds_info = nleds_info;
	leds_info->leds[leds_info->lens] = &mdev->desp;
	leds_info->lens++;
	mdev->conf.aal_enable = 0;
	mutex_init(&mdev->led_access);

	pr_info("parse led: %s, num: %d, connector id: %d, max: %d, min: %d, max_hw: %d, min_hw: %d, brightness: %d",
		mdev->conf.cdev.name,
		leds_info->lens,
		mdev->conf.connector_id,
		mdev->conf.cdev.max_brightness,
		mdev->conf.min_brightness,
		mdev->conf.max_hw_brightness,
		mdev->conf.min_hw_brightness,
		mdev->conf.cdev.brightness);

	return 0;
}
EXPORT_SYMBOL_GPL(mt_leds_parse_dt);


static struct attribute *led_class_attrs[] = {
	&dev_attr_min_brightness.attr,
	&dev_attr_max_hw_brightness.attr,
	&dev_attr_min_hw_brightness.attr,
	&dev_attr_led_mode.attr,
	&dev_attr_connector_id.attr,
	&dev_attr_logic_max_brightness.attr,
	&dev_attr_led_type.attr,
	NULL,
};

static const struct attribute_group mt_led_group = {
	.attrs = led_class_attrs,
};

static const struct attribute_group *mt_led_groups[] = {
	&mt_led_group,
	NULL,
};

int mt_leds_classdev_register(struct device *parent,
				     struct mt_led_data *led_dat)
{
	int ret = 0;

	led_dat->conf.cdev.flags = LED_CORE_SUSPENDRESUME;
	led_dat->conf.flags = LED_MT_BRIGHTNESS_CHANGED;
	led_dat->conf.cdev.brightness_set_blocking = mtk_set_brightness;
#ifdef CONFIG_LEDS_MT_BRIGHTNESS_HW_CHANGED
	led_dat->conf.brightness_hw_changed = -1;
	led_dat->conf.flags |= LED_MT_BRIGHTNESS_HW_CHANGED;
#endif
	led_dat->conf.cdev.groups = mt_led_groups;

	ret = devm_led_classdev_register(parent, &(led_dat->conf.cdev));
	if (ret < 0) {
		pr_notice("led class register fail!");
		return ret;
	}
	pr_info("%s devm_led_classdev_register ok! ", led_dat->conf.cdev.name);
	if (led_dat->conf.flags & LED_MT_BRIGHTNESS_HW_CHANGED) {
		ret = mt_leds_add_brightness_hw_changed(&led_dat->conf);
		if (ret) {
			pr_info("%s add_brightness_hw_changed failed! ", led_dat->conf.cdev.name);
			return ret;
		}
	}
	pr_info("%s mt_leds_add_brightness_hw_changed ok! ", led_dat->conf.cdev.name);

	ret = snprintf(led_dat->debug.buffer + strlen(led_dat->debug.buffer),
		4095 - strlen(led_dat->debug.buffer),
		"[Light] Set %s directly ", led_dat->conf.cdev.name);

	if (ret < 0 || ret >= (4096 - strlen(led_dat->debug.buffer)))
		pr_info("print log init error!");

	led_dat->last_brightness = 0;

	if(led_dat->conf.mode != MT_LED_MODE_CUST_LCM){
		mtk_set_hw_brightness(led_dat,
			brightness_maptolevel(&led_dat->conf, led_dat->conf.cdev.brightness),
			0, 1 << SET_BACKLIGHT_LEVEL);
	}

	pr_info("%s devm_led_classdev_register end! ", led_dat->conf.cdev.name);

	return ret;
}
EXPORT_SYMBOL_GPL(mt_leds_classdev_register);

void mt_leds_classdev_unregister(struct device *parent,
				     struct mt_led_data *led_dat)
{
	devm_led_classdev_unregister(parent, &(led_dat->conf.cdev));
	if (led_dat->conf.flags & LED_MT_BRIGHTNESS_HW_CHANGED)
		mt_leds_remove_brightness_hw_changed(&(led_dat->conf));

	pr_info("%s devm_led_classdev_unregister ok! ", led_dat->conf.cdev.name);

}
EXPORT_SYMBOL_GPL(mt_leds_classdev_unregister);

static int __init mtk_leds_init(void)
{
	int ret = 0;

	pr_info("init begain +++");

	leds_info = kzalloc(sizeof(struct mt_leds_desp_info), GFP_KERNEL);

	if (!leds_info) {
		ret = -ENOMEM;
		goto err;
	}

	pr_info("init end ---");
	return 0;
 err:
	pr_notice("Failed to probe!\n");
	ret = -ENOMEM;
	return ret;

}

static void __exit mtk_leds_exit(void)
{
	kfree(leds_info);
	leds_info = NULL;
}

/* delay leds init, for (1)display has delayed to use clock upstream.
 * (2)to fix repeat switch battary and power supply caused BL KE issue,
 * battary calling bl .shutdown whitch need to call display
 * function and they not yet probe.
 */
module_init(mtk_leds_init);
module_exit(mtk_leds_exit);

MODULE_AUTHOR("Mediatek Corporation");
MODULE_DESCRIPTION("MTK Disp Backlight Driver");
MODULE_LICENSE("GPL");


