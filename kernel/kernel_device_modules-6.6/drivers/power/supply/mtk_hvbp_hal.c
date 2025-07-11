// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 MediaTek Inc.
 */

#include "charger_class.h"
#include "mtk_charger.h"
#include "mtk_hvbp.h"

enum mtk_chg_type {
	MTK_CHGTYP_SWCHG = 0,
	MTK_CHGTYP_DVCHG,
	MTK_CHGTYP_DVCHG_SLAVE,
	MTK_CHGTYP_BUCKBOOSTCHG,
	MTK_CHGTYP_HV_DVCHG,
	MTK_CHGTYP_HV_DVCHG_SLAVE,
	MTK_CHGTYP_MAX,
};

struct mtk_chgdev_desc {
	enum mtk_chg_type type;
	const char *name;
	bool must_exist;
};

static struct mtk_chgdev_desc mtk_chgdev_desc_tbl[MTK_CHGTYP_MAX] = {
	{
		.type = MTK_CHGTYP_SWCHG,
		.name = "primary_chg",
		.must_exist = true,
	},
	{
		.type = MTK_CHGTYP_DVCHG,
		.name = "primary_dvchg",
		.must_exist = false,
	},
	{
		.type = MTK_CHGTYP_DVCHG_SLAVE,
		.name = "secondary_dvchg",
		.must_exist = false,
	},
	{
		.type = MTK_CHGTYP_BUCKBOOSTCHG,
		.name = "buck_boost_chg",
		.must_exist = false,
	},
	{
		.type = MTK_CHGTYP_HV_DVCHG,
		.name = "hvdiv2_chg1",
		.must_exist = true,
	},
	{
		.type = MTK_CHGTYP_HV_DVCHG_SLAVE,
		.name = "hvdiv2_chg2",
		.must_exist = false,
	},
};

struct hvbp_hal {
	struct device *dev;
	struct charger_device *chgdevs[MTK_CHGTYP_MAX];
	struct adapter_device **adapters;
	struct adapter_device *adapter;
	const char **support_ta;
	int support_ta_cnt;
};

static inline int to_chgtyp(enum chg_idx idx)
{
	switch (idx) {
	case CHG1:
		return MTK_CHGTYP_SWCHG;
	case DVCHG1:
		return MTK_CHGTYP_DVCHG;
	case DVCHG2:
		return MTK_CHGTYP_DVCHG_SLAVE;
	case HVDVCHG1:
		return MTK_CHGTYP_HV_DVCHG;
	case HVDVCHG2:
		return MTK_CHGTYP_HV_DVCHG_SLAVE;
	case BUCKBSTCHG:
		return MTK_CHGTYP_BUCKBOOSTCHG;
	default:
		return -EOPNOTSUPP;
	}
}

static inline int to_chgclass_adc(enum hvbp_adc_channel chan)
{
	switch (chan) {
	case HVBP_ADCCHAN_VBUS:
		return ADC_CHANNEL_VBUS;
	case HVBP_ADCCHAN_IBUS:
		return ADC_CHANNEL_IBUS;
	case HVBP_ADCCHAN_VBAT:
		return ADC_CHANNEL_VBAT;
	case HVBP_ADCCHAN_IBAT:
		return ADC_CHANNEL_IBAT;
	case HVBP_ADCCHAN_TBAT:
		return ADC_CHANNEL_TBAT;
	case HVBP_ADCCHAN_TCHG:
		return ADC_CHANNEL_TEMP_JC;
	case HVBP_ADCCHAN_VOUT:
		return ADC_CHANNEL_VOUT;
	case HVBP_ADCCHAN_VSYS:
		return ADC_CHANNEL_VSYS;
	default:
		break;
	}
	return -EOPNOTSUPP;
}

int hvbp_hal_get_ta_output(struct chg_alg_device *alg, int *mV, int *mA)
{
	int ret;
	struct hvbp_hal *hal = chg_alg_dev_get_drv_hal_data(alg);

	ret = adapter_dev_get_output(hal->adapter, mV, mA);
	if (ret < 0)
		return ret;
	return (ret == MTK_ADAPTER_OK) ? 0 : -ret;
}

int hvbp_hal_get_ta_status(struct chg_alg_device *alg,
			    struct hvbp_ta_status *status)
{
	int ret;
	struct hvbp_hal *hal = chg_alg_dev_get_drv_hal_data(alg);
	struct adapter_status _status;

	_status.temperature = 0;
	_status.ocp = 0;
	_status.ovp = 0;
	_status.otp = 0;

	ret = adapter_dev_get_status(hal->adapter, &_status);
	if (ret < 0)
		return ret;
	if (ret != MTK_ADAPTER_OK)
		return -ret;
	/* 0 means NOT SUPPORT */
	status->temperature = (_status.temperature == 0) ? 25 :
							   _status.temperature;
	status->ocp = _status.ocp;
	status->ovp = _status.ovp;
	status->otp = _status.otp;
	return 0;
}

int hvbp_hal_set_ta_cap(struct chg_alg_device *alg, int mV, int mA)
{
	int ret;
	struct hvbp_hal *hal = chg_alg_dev_get_drv_hal_data(alg);

	ret = adapter_dev_set_cap(hal->adapter, MTK_PD_APDO, mV, mA);
	return (ret <= MTK_ADAPTER_OK) ? ret : -ret;
}

int hvbp_hal_is_ta_cc(struct chg_alg_device *alg, bool *is_cc)
{
	int ret;
	struct hvbp_hal *hal = chg_alg_dev_get_drv_hal_data(alg);

	ret = adapter_dev_is_cc(hal->adapter, is_cc);
	return (ret <= MTK_ADAPTER_OK) ? ret : -ret;
}

int hvbp_hal_set_ta_wdt(struct chg_alg_device *alg, u32 ms)
{
	int ret;
	struct hvbp_hal *hal = chg_alg_dev_get_drv_hal_data(alg);

	ret = adapter_dev_set_wdt(hal->adapter, ms);
	return (ret <= MTK_ADAPTER_OK) ? ret : -ret;
}

int hvbp_hal_enable_ta_wdt(struct chg_alg_device *alg, bool en)
{
	int ret;
	struct hvbp_hal *hal = chg_alg_dev_get_drv_hal_data(alg);

	ret = adapter_dev_enable_wdt(hal->adapter, en);
	return (ret <= MTK_ADAPTER_OK) ? ret : -ret;
}

int hvbp_hal_enable_ta_charging(struct chg_alg_device *alg, bool en,
				 int mV, int mA)
{
	int ret;
	struct hvbp_hal *hal = chg_alg_dev_get_drv_hal_data(alg);
	enum adapter_cap_type type = en ? MTK_PD_APDO_START : MTK_PD_APDO_END;

	ret = adapter_dev_set_cap(hal->adapter, type, mV, mA);
	return (ret <= MTK_ADAPTER_OK) ? ret : -ret;
}

int hvbp_hal_sync_ta_volt(struct chg_alg_device *alg, u32 mV)
{
	int ret;
	struct hvbp_hal *hal = chg_alg_dev_get_drv_hal_data(alg);

	ret = adapter_dev_sync_volt(hal->adapter, mV);
	return (ret <= MTK_ADAPTER_OK) ? ret : -ret;
}

int hvbp_hal_authenticate_ta(struct chg_alg_device *alg,
			      struct hvbp_ta_auth_data *data)
{
	int ret, i;
	struct hvbp_hal *hal = chg_alg_dev_get_drv_hal_data(alg);
	struct adapter_auth_data _data = {
		.vcap_min = data->vcap_min,
		.vcap_max = data->vcap_max,
		.icap_min = data->icap_min,
	};

	for (i = 0; i < hal->support_ta_cnt; i++) {
		if (!hal->adapters[i])
			continue;
		ret = adapter_dev_authentication(hal->adapters[i], &_data);
		if (ret < 0 || ret != MTK_ADAPTER_OK) {
			HVBP_DBG("authenticate fail(%d)\n", ret);
			continue;
		}
		hal->adapter = hal->adapters[i];
		data->vta_min = _data.vta_min;
		data->vta_max = _data.vta_max;
		data->ita_max = _data.ita_max;
		data->pwr_lmt = _data.pwr_lmt;
		data->pdp = _data.pdp;
		data->support_meas_cap = _data.support_meas_cap;
		data->support_status = _data.support_status;
		data->support_cc = _data.support_cc;
		data->vta_step = _data.vta_step;
		data->ita_step = _data.ita_step;
		data->ita_gap_per_vstep = _data.ita_gap_per_vstep;
		HVBP_INFO("lmt(%d,%dW),step(%d,%d),cc=%d,cap=%d,status=%d\n",
			  data->pwr_lmt, data->pdp, data->vta_step,
			  data->ita_step, data->support_cc,
			  data->support_meas_cap,
			  data->support_status);
		return 0;
	}
	return -EINVAL;
}

int hvbp_hal_send_ta_hardreset(struct chg_alg_device *alg)
{
	int ret;
	struct hvbp_hal *hal = chg_alg_dev_get_drv_hal_data(alg);

	ret = adapter_dev_send_hardreset(hal->adapter);
	return (ret <= MTK_ADAPTER_OK) ? ret : -ret;
}

int hvbp_hal_init_hardware(struct chg_alg_device *alg, const char **support_ta,
			    int support_ta_cnt)
{
	struct hvbp_algo_info *info = chg_alg_dev_get_drvdata(alg);
	struct hvbp_algo_data *data = info->data;
	struct hvbp_hal *hal;
	int i, ret;
	bool has_ta = false;

	if (support_ta_cnt <= 0)
		return -EINVAL;

	hal = devm_kzalloc(info->dev, sizeof(*hal), GFP_KERNEL);
	if (!hal)
		return -ENOMEM;
	hal->adapters = devm_kzalloc(info->dev,
				     sizeof(struct adapter_device *) *
				     support_ta_cnt, GFP_KERNEL);
	if (!hal->adapters)
		return -ENOMEM;

	hal->support_ta = support_ta;
	hal->support_ta_cnt = support_ta_cnt;
	/* get TA */
	for (i = 0; i < hal->support_ta_cnt; i++) {
		hal->adapters[i] = get_adapter_by_name(support_ta[i]);
		if (hal->adapters[i]) {
			has_ta = true;
			continue;
		}
		HVBP_ERR("no %s\n", hal->support_ta[i]);
	}
	if (!has_ta) {
		ret = -ENODEV;
		goto err;
	}

	/* get charger device */
	for (i = 0; i < MTK_CHGTYP_MAX; i++) {
		hal->chgdevs[i] =
			get_charger_by_name(mtk_chgdev_desc_tbl[i].name);
		if (!hal->chgdevs[i]) {
			HVBP_ERR("get %s fail\n", mtk_chgdev_desc_tbl[i].name);
			if (mtk_chgdev_desc_tbl[i].must_exist) {
				ret = -ENODEV;
				goto err;
			}
		}
	}
	data->is_dvchg_exist[HVBP_BUCK_BSTCHG] = true;
	data->is_dvchg_exist[HVBP_HVDVCHG_MASTER] = true;
	if (hal->chgdevs[MTK_CHGTYP_HV_DVCHG_SLAVE])
		data->is_dvchg_exist[HVBP_HVDVCHG_SLAVE] = true;
	chg_alg_dev_set_drv_hal_data(alg, hal);
	hal->dev = info->dev;
	HVBP_INFO("successfully\n");
	return 0;
err:
	return ret;
}

int hvbp_hal_enable_sw_vbusovp(struct chg_alg_device *alg, bool en)
{
	mtk_chg_enable_vbus_ovp(en);
	return 0;
}

int hvbp_set_operating_mode(struct chg_alg_device *alg, enum chg_idx chgidx,
			    bool en)
{
	int chgtyp = to_chgtyp(chgidx);
	struct hvbp_hal *hal = chg_alg_dev_get_drv_hal_data(alg);

	if (chgtyp < 0)
		return chgtyp;
	return charger_dev_set_operation_mode(hal->chgdevs[chgtyp], en);
}

int hvbp_hal_enable_charging(struct chg_alg_device *alg,
			      enum chg_idx chgidx, bool en)
{
	int chgtyp = to_chgtyp(chgidx);
	struct hvbp_hal *hal = chg_alg_dev_get_drv_hal_data(alg);

	if (chgtyp < 0)
		return chgtyp;
	return charger_dev_enable(hal->chgdevs[chgtyp], en);
}

int hvbp_hal_dump_registers(struct chg_alg_device *alg, enum chg_idx chgidx)
{
	int chgtyp = to_chgtyp(chgidx);
	struct hvbp_hal *hal = chg_alg_dev_get_drv_hal_data(alg);

	if (chgtyp < 0)
		return chgtyp;
	return charger_dev_dump_registers(hal->chgdevs[chgtyp]);
}

int hvbp_hal_enable_hz(struct chg_alg_device *alg,
			enum chg_idx chgidx, bool en)
{
	int chgtyp = to_chgtyp(chgidx);
	struct hvbp_hal *hal = chg_alg_dev_get_drv_hal_data(alg);
	u32 mV = en ? 24000 : 4500;

	if (chgtyp < 0)
		return chgtyp;

	return charger_dev_set_mivr(hal->chgdevs[chgtyp], milli_to_micro(mV));
}

int hvbp_hal_set_vacovp(struct chg_alg_device *alg,
			  enum chg_idx chgidx, u32 mV)
{
	int chgtyp = to_chgtyp(chgidx);
	struct hvbp_hal *hal = chg_alg_dev_get_drv_hal_data(alg);

	if (chgtyp < 0)
		return chgtyp;
	return charger_dev_set_vac_ovp(hal->chgdevs[chgtyp],
				       milli_to_micro(mV));
}

int hvbp_hal_set_vbusovp(struct chg_alg_device *alg,
			  enum chg_idx chgidx, u32 mV)
{
	int chgtyp = to_chgtyp(chgidx);
	struct hvbp_hal *hal = chg_alg_dev_get_drv_hal_data(alg);

	if (chgtyp < 0)
		return chgtyp;
	return charger_dev_set_vbusovp(hal->chgdevs[chgtyp],
				       milli_to_micro(mV));
}

int hvbp_hal_set_ibusocp(struct chg_alg_device *alg,
			  enum chg_idx chgidx, u32 mA)
{
	int chgtyp = to_chgtyp(chgidx);
	struct hvbp_hal *hal = chg_alg_dev_get_drv_hal_data(alg);

	if (chgtyp < 0)
		return chgtyp;
	return charger_dev_set_ibusocp(hal->chgdevs[chgtyp],
				       milli_to_micro(mA));
}

int hvbp_hal_set_vbatovp(struct chg_alg_device *alg,
			  enum chg_idx chgidx, u32 mV)
{
	int chgtyp = to_chgtyp(chgidx);
	struct hvbp_hal *hal = chg_alg_dev_get_drv_hal_data(alg);

	if (chgtyp < 0)
		return chgtyp;
	return charger_dev_set_vbatovp(hal->chgdevs[chgtyp],
				       milli_to_micro(mV));
}

int hvbp_hal_set_ibatocp(struct chg_alg_device *alg,
			  enum chg_idx chgidx, u32 mA)
{
	int chgtyp = to_chgtyp(chgidx);
	struct hvbp_hal *hal = chg_alg_dev_get_drv_hal_data(alg);

	if (chgtyp < 0)
		return chgtyp;
	return charger_dev_set_ibatocp(hal->chgdevs[chgtyp],
				       milli_to_micro(mA));
}

int hvbp_hal_set_vbatovp_alarm(struct chg_alg_device *alg,
				enum chg_idx chgidx, u32 mV)
{
	int chgtyp = to_chgtyp(chgidx);
	struct hvbp_hal *hal = chg_alg_dev_get_drv_hal_data(alg);

	if (chgtyp < 0)
		return chgtyp;
	return charger_dev_set_vbatovp_alarm(hal->chgdevs[chgtyp],
					     milli_to_micro(mV));
}

int hvbp_hal_reset_vbatovp_alarm(struct chg_alg_device *alg,
				  enum chg_idx chgidx)
{
	int chgtyp = to_chgtyp(chgidx);
	struct hvbp_hal *hal = chg_alg_dev_get_drv_hal_data(alg);

	if (chgtyp < 0)
		return chgtyp;
	return charger_dev_reset_vbatovp_alarm(hal->chgdevs[chgtyp]);
}

int hvbp_hal_set_vbusovp_alarm(struct chg_alg_device *alg,
				enum chg_idx chgidx, u32 mV)
{
	int chgtyp = to_chgtyp(chgidx);
	struct hvbp_hal *hal = chg_alg_dev_get_drv_hal_data(alg);

	if (chgtyp < 0)
		return chgtyp;
	return charger_dev_set_vbusovp_alarm(hal->chgdevs[chgtyp],
					     milli_to_micro(mV));
}

int hvbp_hal_reset_vbusovp_alarm(struct chg_alg_device *alg,
				  enum chg_idx chgidx)
{
	int chgtyp = to_chgtyp(chgidx);
	struct hvbp_hal *hal = chg_alg_dev_get_drv_hal_data(alg);

	if (chgtyp < 0)
		return chgtyp;
	return charger_dev_reset_vbusovp_alarm(hal->chgdevs[chgtyp]);
}

static int hvbp_get_tbat(struct hvbp_hal *hal)
{
	int ret = 0;
	int tmp_ret = 0;
	union power_supply_propval prop = {0};
	struct power_supply *bat_psy;

#if IS_ENABLED(CONFIG_BATTERY_SAMSUNG)
	bat_psy = power_supply_get_by_name("mtk-fg-battery");
#else
	bat_psy = power_supply_get_by_name("battery");
#endif
	if (IS_ERR_OR_NULL(bat_psy)) {
		HVBP_ERR("%s Couldn't get bat_psy\n", __func__);
		ret = 27;
	} else {
		tmp_ret = power_supply_get_property(bat_psy,
						 POWER_SUPPLY_PROP_TEMP, &prop);
		if (tmp_ret) {
			HVBP_ERR("%s Couldn't get bat_psy\n", __func__);
			ret = 27;
			return ret;
		}
		ret = prop.intval / 10;
	}

	ret = 25;
	HVBP_DBG("%d\n", ret);
	return ret;
}

int hvbp_hal_get_adc(struct chg_alg_device *alg, enum chg_idx chgidx,
		      enum hvbp_adc_channel chan, int *val)
{
	int ret;
	int chgtyp = to_chgtyp(chgidx);
	struct hvbp_hal *hal = chg_alg_dev_get_drv_hal_data(alg);
	int _chan = to_chgclass_adc(chan);

	if (chgtyp < 0)
		return chgtyp;
	if (_chan < 0)
		return _chan;

	if (_chan == ADC_CHANNEL_TBAT) {
		*val = hvbp_get_tbat(hal);
		return 0;
	}
	ret = charger_dev_get_adc(hal->chgdevs[chgtyp], _chan, val, val);
	if (ret < 0)
		return ret;
	if (_chan == ADC_CHANNEL_VBAT || _chan == ADC_CHANNEL_IBAT ||
	    _chan == ADC_CHANNEL_VBUS || _chan == ADC_CHANNEL_IBUS ||
	    _chan == ADC_CHANNEL_VOUT || _chan == ADC_CHANNEL_VSYS)
		*val = micro_to_milli(*val);
	return 0;
}

int hvbp_hal_get_soc(struct chg_alg_device *alg, u32 *soc)
{
	int ret = 0;
	int ret_tmp = 0;
	struct power_supply *bat_psy;
	union power_supply_propval prop = {0};
	//struct hvbp_hal *hal = chg_alg_dev_get_drv_hal_data(alg);

#if IS_ENABLED(CONFIG_BATTERY_SAMSUNG)
	bat_psy = power_supply_get_by_name("mtk-fg-battery");
#else
	bat_psy = power_supply_get_by_name("battery");
#endif
	if (IS_ERR_OR_NULL(bat_psy)) {
		HVBP_ERR("%s Couldn't get bat_psy\n", __func__);
		ret = 50;
	} else {
		ret_tmp = power_supply_get_property(bat_psy,
					     POWER_SUPPLY_PROP_CAPACITY, &prop);
		if (ret_tmp < 0){
			HVBP_ERR("%s Couldn't get battery capacity\n", __func__);
			ret = 50;
			return ret;
		}
		ret = prop.intval;
	}
	if (ret < 0)
		return ret;
	*soc = ret;
	HVBP_DBG("%d\n", *soc);
	return 0;
}

int hvbp_hal_is_adapter_ready(struct chg_alg_device *alg)
{
	struct hvbp_hal *hal;
	struct power_supply *chg_psy = NULL;
	struct mtk_charger *info = NULL;


	if (alg == NULL) {
		pr_notice("%s: alg is null\n", __func__);
		return -EINVAL;
	}

	hal = chg_alg_dev_get_drv_hal_data(alg);
	if (chg_psy == NULL || IS_ERR(chg_psy))
		pr_notice("%s Couldn't get chg_psy\n", __func__);
	else {
		info = (struct mtk_charger *)power_supply_get_drvdata(chg_psy);
		hal->adapter = info->select_adapter;
		pr_notice("%s ta_cap:%d\n", __func__, info->ta_capability);
		if (info->ta_capability == APDO_TA)
			return ALG_READY;
		else
			return ALG_TA_NOT_SUPPORT;
	}
	return ALG_TA_CHECKING;
}

int hvbp_hal_set_ichg(struct chg_alg_device *alg, enum chg_idx chgidx, u32 mA)
{
	int chgtyp = to_chgtyp(chgidx);
	struct hvbp_hal *hal = chg_alg_dev_get_drv_hal_data(alg);

	if (chgtyp < 0)
		return chgtyp;
	return charger_dev_set_charging_current(hal->chgdevs[chgtyp],
						milli_to_micro(mA));
}

int hvbp_hal_set_aicr(struct chg_alg_device *alg, enum chg_idx chgidx, u32 mA)
{
	int chgtyp = to_chgtyp(chgidx);
	struct hvbp_hal *hal = chg_alg_dev_get_drv_hal_data(alg);

	if (chgtyp < 0)
		return chgtyp;
	return charger_dev_set_input_current(hal->chgdevs[chgtyp],
					     milli_to_micro(mA));
}

int hvbp_hal_get_ichg(struct chg_alg_device *alg, enum chg_idx chgidx, u32 *mA)
{
	int chgtyp = to_chgtyp(chgidx);
	struct hvbp_hal *hal = chg_alg_dev_get_drv_hal_data(alg);
	int ret, uA = 0;

	if (chgtyp < 0)
		return chgtyp;

	ret = charger_dev_get_charging_current(hal->chgdevs[chgtyp], &uA);
	*mA = micro_to_milli(uA);

	return ret;
}

int hvbp_hal_get_aicr(struct chg_alg_device *alg, enum chg_idx chgidx, u32 *mA)
{
	int chgtyp = to_chgtyp(chgidx);
	struct hvbp_hal *hal = chg_alg_dev_get_drv_hal_data(alg);
	int ret, uA = 0;

	if (chgtyp < 0)
		return chgtyp;

	ret = charger_dev_get_input_current(hal->chgdevs[chgtyp], &uA);
	*mA = micro_to_milli(uA);

	return ret;
}

int hvbp_hal_is_vbuslowerr(struct chg_alg_device *alg,
			    enum chg_idx chgidx, bool *err)
{
	int chgtyp = to_chgtyp(chgidx);
	struct hvbp_hal *hal = chg_alg_dev_get_drv_hal_data(alg);

	if (chgtyp < 0)
		return chgtyp;
	return charger_dev_is_vbuslowerr(hal->chgdevs[chgtyp], err);
}

int hvbp_hal_get_adc_accuracy(struct chg_alg_device *alg,
			       enum chg_idx chgidx,
			       enum hvbp_adc_channel chan, int *val)
{
	int ret;
	int chgtyp = to_chgtyp(chgidx);
	struct hvbp_hal *hal = chg_alg_dev_get_drv_hal_data(alg);
	int _chan = to_chgclass_adc(chan);

	if (chgtyp < 0)
		return chgtyp;
	if (_chan < 0)
		return _chan;

	ret = charger_dev_get_adc_accuracy(hal->chgdevs[chgtyp], _chan, val,
					   val);
	if (ret < 0)
		return ret;
	if (_chan == ADC_CHANNEL_VBAT || _chan == ADC_CHANNEL_IBAT ||
	    _chan == ADC_CHANNEL_VBUS || _chan == ADC_CHANNEL_IBUS ||
	    _chan == ADC_CHANNEL_VOUT)
		*val = micro_to_milli(*val);
	return 0;
}

int hvbp_hal_init_chip(struct chg_alg_device *alg, enum chg_idx chgidx)
{
	int chgtyp = to_chgtyp(chgidx);
	struct hvbp_hal *hal = chg_alg_dev_get_drv_hal_data(alg);

	if (chgtyp < 0)
		return chgtyp;
	return charger_dev_init_chip(hal->chgdevs[chgtyp]);
}
