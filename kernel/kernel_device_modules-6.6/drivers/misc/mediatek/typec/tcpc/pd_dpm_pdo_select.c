// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 MediaTek Inc.
 */

#if IS_ENABLED(CONFIG_USB_POWER_DELIVERY)
#include "inc/pd_dpm_pdo_select.h"

struct dpm_select_info_t {
	uint8_t pos;
	int max_uw;
	int cur_mv;
	uint8_t policy;
};

static inline void dpm_extract_apdo_info(
		uint32_t pdo, struct dpm_pdo_info_t *info)
{
#if CONFIG_USB_PD_REV30_PPS_SINK
	switch (APDO_TYPE(pdo)) {
	case APDO_TYPE_PPS:
		info->apdo_type = DPM_APDO_TYPE_PPS;

		if (pdo & APDO_PPS_CURR_FOLDBACK)
			info->apdo_type |= DPM_APDO_TYPE_PPS_CF;

		info->pwr_limit = APDO_PPS_EXTRACT_PWR_LIMIT(pdo);
		info->ma = APDO_PPS_EXTRACT_CURR(pdo);
		info->vmin = APDO_PPS_EXTRACT_MIN_VOLT(pdo);
		info->vmax = APDO_PPS_EXTRACT_MAX_VOLT(pdo);
		info->uw = info->ma * info->vmax;
		return;
	}
#endif	/* CONFIG_USB_PD_REV30_PPS_SINK */

	info->type = TCPM_POWER_CAP_VAL_TYPE_UNKNOWN;
}

void dpm_extract_pdo_info(uint32_t pdo, struct dpm_pdo_info_t *info)
{
	memset(info, 0, sizeof(struct dpm_pdo_info_t));

	info->type = PDO_TYPE_VAL(pdo);

	switch (PDO_TYPE(pdo)) {
	case PDO_TYPE_FIXED:
		info->ma = PDO_FIXED_EXTRACT_CURR(pdo);
		info->vmax = info->vmin = PDO_FIXED_EXTRACT_VOLT(pdo);
		info->uw = info->ma * info->vmax;
		if (!info->usb_comm_capable)
			info->usb_comm_capable = !!(pdo & PDO_FIXED_USB_COMM);
		break;

	case PDO_TYPE_VARIABLE:
		info->ma = PDO_VAR_EXTRACT_CURR(pdo);
		info->vmin = PDO_VAR_EXTRACT_MIN_VOLT(pdo);
		info->vmax = PDO_VAR_EXTRACT_MAX_VOLT(pdo);
		info->uw = info->ma * info->vmax;
		break;

	case PDO_TYPE_BATTERY:
		info->uw = PDO_BATT_EXTRACT_OP_POWER(pdo) * 1000;
		info->vmin = PDO_BATT_EXTRACT_MIN_VOLT(pdo);
		info->vmax = PDO_BATT_EXTRACT_MAX_VOLT(pdo);
		info->ma = info->uw / info->vmin;
		break;

#if CONFIG_USB_PD_REV30_PPS_SINK
	case PDO_TYPE_APDO:
		dpm_extract_apdo_info(pdo, info);
		break;
#endif	/* CONFIG_USB_PD_REV30_PPS_SINK */
	}
}

static inline int dpm_calc_src_cap_power_uw(
	struct dpm_pdo_info_t *source, struct dpm_pdo_info_t *sink)
{
	return MIN(source->uw, sink->uw);
}

/*
 * Select PDO from VSafe5V
 */

static bool dpm_select_pdo_from_vsafe5v(
	struct dpm_select_info_t *select_info,
	struct dpm_pdo_info_t *sink, struct dpm_pdo_info_t *source)
{
	int uw;

	if ((sink->vmin != TCPC_VBUS_SINK_5V) ||
		(sink->vmax != TCPC_VBUS_SINK_5V) ||
		(source->vmin != TCPC_VBUS_SINK_5V) ||
		(source->vmax != TCPC_VBUS_SINK_5V))
		return false;

	uw = dpm_calc_src_cap_power_uw(source, sink);
	if (uw > select_info->max_uw) {
		select_info->max_uw = uw;
		select_info->cur_mv = source->vmax;
		return true;
	}

	return false;
}

/*
 * Select PDO from Max Power
 */

static inline bool dpm_is_valid_pdo_pair(struct dpm_pdo_info_t *sink,
	struct dpm_pdo_info_t *source, uint32_t policy, bool fixed_or_apdo)
{
	if (fixed_or_apdo) {
		if (sink->vmin < source->vmin)
			return false;

		if (sink->vmax > source->vmax)
			return false;
	} else {
		if (sink->vmin > source->vmin)
			return false;

		if (sink->vmax < source->vmax)
			return false;
	}

	if (policy & DPM_CHARGING_POLICY_IGNORE_MISMATCH_CURR)
		return true;

	return sink->ma <= source->ma;
}

static bool dpm_select_pdo_from_max_power(
	struct dpm_select_info_t *select_info,
	struct dpm_pdo_info_t *sink, struct dpm_pdo_info_t *source)
{
	bool overload;
	int uw;

	if (sink->type == DPM_PDO_TYPE_APDO ||
	    source->type == DPM_PDO_TYPE_APDO)
		return false;

	if (!dpm_is_valid_pdo_pair(sink, source, select_info->policy,
				   sink->type == DPM_PDO_TYPE_FIXED))
		return false;

	uw = dpm_calc_src_cap_power_uw(source, sink);

	overload = uw > select_info->max_uw;

	if ((!overload) && (uw == select_info->max_uw)) {
		if (select_info->policy &
			DPM_CHARGING_POLICY_PREFER_LOW_VOLTAGE)
			overload = (source->vmax < select_info->cur_mv);
		else if (select_info->policy &
			DPM_CHARGING_POLICY_PREFER_HIGH_VOLTAGE)
			overload = (source->vmax > select_info->cur_mv);
	}

	if (overload) {
		select_info->max_uw = uw;
		select_info->cur_mv = source->vmax;
		return true;
	}

	return false;
}

/*
 * Select PDO from PPS
 */

#if CONFIG_USB_PD_REV30_PPS_SINK
static bool dpm_select_pdo_from_pps(
		struct dpm_select_info_t *select_info,
		struct dpm_pdo_info_t *sink, struct dpm_pdo_info_t *source)
{
	bool overload;
	int uw;

	if (sink->type != DPM_PDO_TYPE_APDO ||
	    source->type != DPM_PDO_TYPE_APDO)
		return false;

	if (!(source->apdo_type & DPM_APDO_TYPE_PPS))
		return false;

	if (!dpm_is_valid_pdo_pair(sink, source, select_info->policy, true))
		return false;

	uw = sink->vmax * source->ma;

	if (uw > select_info->max_uw)
		overload = true;
	else if (uw < select_info->max_uw)
		overload = false;
	else if (source->vmax > select_info->cur_mv)
		overload = true;
	else
		overload = false;

	if (overload) {
		select_info->max_uw = uw;
		select_info->cur_mv = source->vmax;
		return true;
	}

	return false;
}
#endif	/* CONFIG_USB_PD_REV30_PPS_SINK */

/*
 * Select PDO from defined rule ...
 */

typedef bool (*dpm_select_pdo_fun)(
	struct dpm_select_info_t *select_info,
	struct dpm_pdo_info_t *sink, struct dpm_pdo_info_t *source);

bool dpm_find_match_req_info(struct pd_port *pd_port,
		struct dpm_rdo_info_t *req_info,
		struct dpm_pdo_info_t *sink, int cnt, uint32_t *src_pdos,
		int max_uw, uint32_t policy)
{
	int i;
	struct dpm_select_info_t select;
	struct dpm_pdo_info_t source;
	dpm_select_pdo_fun select_pdo_fun;
	int usb_comm_capable = 0;

	select.pos = 0;
	select.max_uw = max_uw;
	select.cur_mv = 0;
	select.policy = policy;

	switch (policy & DPM_CHARGING_POLICY_MASK) {
	case DPM_CHARGING_POLICY_MAX_POWER:
		select_pdo_fun = dpm_select_pdo_from_max_power;
		break;

	case DPM_CHARGING_POLICY_CUSTOM:
		if (pd_port->pe_data.explicit_contract) {
			select.policy = DPM_CHARGING_POLICY_MAX_POWER_LVIC;
			select_pdo_fun = dpm_select_pdo_from_max_power;
		} else
			select_pdo_fun = dpm_select_pdo_from_vsafe5v;
		break;

#if CONFIG_USB_PD_REV30_PPS_SINK
	case DPM_CHARGING_POLICY_PPS:
		select_pdo_fun = dpm_select_pdo_from_pps;
		break;
#endif	/* CONFIG_USB_PD_REV30_PPS_SINK */

	default: /* DPM_CHARGING_POLICY_VSAFE5V */
		select_pdo_fun = dpm_select_pdo_from_vsafe5v;
		break;
	}

	for (i = 0; i < cnt; i++) {
		dpm_extract_pdo_info(src_pdos[i], &source);

		if (select_pdo_fun(&select, sink, &source))
			select.pos = i+1;

		if (!usb_comm_capable)
			usb_comm_capable = source.usb_comm_capable;
	}

#if IS_ENABLED(CONFIG_PDIC_NOTIFIER)
	DPM_INFO("(%d,%d,%d)\n", cnt, source.vmax, source.ma);
	if (cnt == 1 && source.vmax == 5000 && source.ma == 500) {
		DPM_INFO("is_send_svid = true\n");
		pd_port->is_send_svid = true;
	}
#endif	/* CONFIG_PDIC_NOTIFIER */
#if IS_ENABLED(CONFIG_USE_USB_COMMUNICATIONS_CAPABLE)
	if (usb_comm_capable)
		send_otg_notify(get_otg_notify(), NOTIFY_EVENT_PD_USB_COMM_CAPABLE, USB_NOTIFY_COMM_CAPABLE);
	else
		send_otg_notify(get_otg_notify(), NOTIFY_EVENT_PD_USB_COMM_CAPABLE, USB_NOTIFY_NO_COMM_CAPABLE);
#endif /* CONFIG_USE_USB_COMMUNICATIONS_CAPABLE */

	if (select.pos > 0) {
		dpm_extract_pdo_info(src_pdos[select.pos-1], &source);

		req_info->pos = select.pos;
		req_info->type = source.type;
		req_info->vmin = source.vmin;
		req_info->vmax = source.vmax;

		if (req_info->type == DPM_PDO_TYPE_BAT) {
			req_info->mismatch = source.uw < sink->uw;
			req_info->max_uw = sink->uw;
			req_info->oper_uw = select.max_uw;
		} else {
			req_info->mismatch = source.ma < sink->ma;
			req_info->max_ma = sink->ma;
			req_info->oper_ma = MIN(sink->ma, source.ma);
		}

#if CONFIG_USB_PD_REV30_PPS_SINK
		if (req_info->type == DPM_PDO_TYPE_APDO) {
			req_info->vmin = sink->vmin;
			req_info->vmax = sink->vmax;
		}
#endif	/* CONFIG_USB_PD_REV30_PPS_SINK */

		pd_port->last_sink_pdo_info = *sink;

		return true;
	}

	return false;
}
#endif	/* CONFIG_USB_POWER_DELIVERY */
