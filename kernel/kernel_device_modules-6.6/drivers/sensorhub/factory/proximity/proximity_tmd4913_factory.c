/*
 *  Copyright (C) 2020, Samsung Electronics Co. Ltd. All Rights Reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 */

#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/of_gpio.h>
#include <linux/uaccess.h>

#include "../../sensor/proximity.h"
#include "../../sensormanager/shub_sensor.h"
#include "../../sensormanager/shub_sensor_manager.h"
#include "../../comm/shub_comm.h"
#include "../../sensorhub/shub_device.h"
#include "../../utility/shub_utility.h"
#include "proximity_factory.h"
#include "../../others/shub_panel.h"

static ssize_t proximity_tmd4913_prox_led_test_show(char *buf)
{
	int ret = 0;
	char *buffer = NULL;
	int buffer_length = 0;
	struct prox_led_test {
		u8 ret;
		int adc[4];

	} __attribute__((__packed__)) result;

	ret = shub_send_command_wait(CMD_GETVALUE, SENSOR_TYPE_PROXIMITY, PROXIMITY_LED_TEST, 1300, NULL, 0, &buffer,
				     &buffer_length, true);
	if (ret < 0) {
		shub_errf("shub_send_command_wait Fail %d", ret);
		return ret;
	}

	if (buffer_length != sizeof(result)) {
		shub_errf("buffer length error(%d)", buffer_length);
		kfree(buffer);
		return -EINVAL;
	}

	memcpy(&result, buffer, buffer_length);

	ret = snprintf(buf, PAGE_SIZE, "%d,%d,%d,%d,%d\n", result.ret, result.adc[0], result.adc[1], result.adc[2],
		       result.adc[3]);

	kfree(buffer);

	return ret;
}

static ssize_t proximity_tmd4913_prox_trim_show(char *buf, int type)
{
	struct proximity_data *data = (struct proximity_data *)get_sensor(type)->data;

	shub_infof("setting_mode : %d", data->setting_mode);

	return snprintf(buf, PAGE_SIZE, "%d\n", data->setting_mode);
}

static int proximity_get_calibration_data(int type)
{
	int ret = 0;
	char *buffer = NULL;
	int buffer_length = 0;
	struct proximity_data *data = (struct proximity_data *)get_sensor(type)->data;

	ret = shub_send_command_wait(CMD_GETVALUE, type, CAL_DATA, 1000, NULL, 0, &buffer,
					&buffer_length, true);
	if (ret < 0) {
		shub_errf("shub_send_command_wait fail %d", ret);
		return ret;
	}

	if (buffer_length != data->cal_data_len) {
		shub_errf("buffer length error(%d)", buffer_length);
		kfree(buffer);
		return -EINVAL;
	}

	memcpy(data->cal_data, buffer, data->cal_data_len);

	save_proximity_calibration(type);

	kfree(buffer);

	return 0;
}

static int proximity_get_setting_mode(int type)
{
	int ret = 0;
	char *buffer = NULL;
	int buffer_length = 0;
	struct proximity_data *data = (struct proximity_data *)get_sensor(type)->data;

	ret = shub_send_command_wait(CMD_GETVALUE, type, PROXIMITY_SETTING_MODE, 1000, NULL,
					0, &buffer, &buffer_length, true);
	if (ret < 0) {
		shub_errf("shub_send_command_wait fail %d", ret);
		return ret;
	}

	if (buffer_length != sizeof(data->setting_mode)) {
		shub_errf("buffer length error(%d)", buffer_length);
		kfree(buffer);
		return -EINVAL;
	}

	memcpy(&data->setting_mode, buffer, sizeof(data->setting_mode));
	save_proximity_setting_mode(type);

	kfree(buffer);

	return 0;
}

static ssize_t proximity_tmd4913_prox_cal_store(const char *buf, size_t size, int type)
{
	int ret = 0;
	bool init, update = false;
	struct proximity_data *data = (struct proximity_data *)get_sensor(type)->data;

	if (!get_sensor_probe_state(type))
		return -ENOENT;
	if (!buf)
		return -EINVAL;

	init = sysfs_streq(buf, "0");
	update = sysfs_streq(buf, "1");

	if (init) {
		ret = shub_send_command(CMD_SETVALUE, type, PROX_SUBCMD_CALIBRATION_START, NULL, 0);
		if (ret < 0) {
			shub_errf("CMD fail %d", ret);
			return ret;
		}
		memset(&data->setting_mode, 0, sizeof(data->setting_mode));
		memset(data->cal_data, 0, data->cal_data_len);

	} else if (update) {
		ret = proximity_get_setting_mode(type);
		if (ret < 0) {
			shub_errf("proximity_get_setting_mode fail %d", ret);
			return ret;
		}
		msleep(500);
		ret = proximity_get_calibration_data(type);
		if (ret < 0) {
			shub_errf("proximity_get_calibration_data fail %d", ret);
			return ret;
		}
		msleep(500);
		ret = save_panel_ubid();
		if (ret < 0) {
			shub_errf("save_panel_ubid %d", ret);
			return ret;
		}
		shub_infof("ADC : %u, mode : %u", *((u16 *)(data->cal_data)), data->setting_mode);
	} else {
		shub_errf("buf data is wrong %s", buf);
	}
	return size;
}

struct proximity_factory_chipset_funcs proximity_tmd4913_ops = {
	.prox_cal_store = proximity_tmd4913_prox_cal_store,
	.prox_trim_show = proximity_tmd4913_prox_trim_show,
	.prox_led_test_show = proximity_tmd4913_prox_led_test_show,
};

struct proximity_factory_chipset_funcs *get_proximity_tmd4913_chipset_func(char *name)
{
	if (strcmp(name, "TMD4913") != 0)
		return NULL;

	return &proximity_tmd4913_ops;
}
