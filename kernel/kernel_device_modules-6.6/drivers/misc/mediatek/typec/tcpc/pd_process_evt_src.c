// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 MediaTek Inc.
 */

#if IS_ENABLED(CONFIG_USB_POWER_DELIVERY)
#include "inc/pd_core.h"
#include "inc/tcpci_event.h"
#include "inc/pd_process_evt.h"
#include "inc/pd_dpm_core.h"


/* PD Data MSG reactions */

DECL_PE_STATE_TRANSITION(PD_DATA_MSG_REQUEST) = {
	{ PE_SRC_SEND_CAPABILITIES, PE_SRC_NEGOTIATE_CAPABILITIES },
	{ PE_SRC_READY, PE_SRC_NEGOTIATE_CAPABILITIES },
};
DECL_PE_STATE_REACTION(PD_DATA_MSG_REQUEST);

/* DPM Event reactions */

DECL_PE_STATE_TRANSITION(PD_DPM_MSG_ACK) = {
	{ PE_SRC_NEGOTIATE_CAPABILITIES, PE_SRC_TRANSITION_SUPPLY },
};
DECL_PE_STATE_REACTION(PD_DPM_MSG_ACK);

/* Timer Event reactions */

DECL_PE_STATE_TRANSITION(PD_TIMER_PS_HARD_RESET) = {
	{ PE_SRC_HARD_RESET, PE_SRC_TRANSITION_TO_DEFAULT },
	{ PE_SRC_HARD_RESET_RECEIVED, PE_SRC_TRANSITION_TO_DEFAULT },
};
DECL_PE_STATE_REACTION(PD_TIMER_PS_HARD_RESET);

/*
 * [BLOCK] Process Ctrl MSG
 */

static inline bool pd_process_ctrl_msg_good_crc(
	struct pd_port *pd_port, struct pd_event *pd_event)

{
	switch (pd_port->pe_state_curr) {
	case PE_SRC_SEND_CAPABILITIES:
		pd_port->pe_data.cap_counter = 0;
		pd_handle_hard_reset_recovery(pd_port);
		return false;

	case PE_SRC_TRANSITION_SUPPLY:
		pd_enable_pe_state_timer(pd_port, PD_TIMER_SOURCE_TRANSITION);
		return false;

	case PE_SRC_CAPABILITY_RESPONSE:
		if (!pd_port->pe_data.explicit_contract)
			PE_TRANSIT_STATE(pd_port, PE_SRC_WAIT_NEW_CAPABILITIES);
		else if (pd_port->pe_data.invalid_contract)
			PE_TRANSIT_STATE(pd_port, PE_SRC_HARD_RESET);
		else
			PE_TRANSIT_STATE(pd_port, PE_SRC_READY);
		return true;

	case PE_SRC_SOFT_RESET:
		PE_TRANSIT_STATE(pd_port, PE_SRC_SEND_CAPABILITIES);
		return true;

	default:
		return false;
	}
}

static bool pd_process_ctrl_msg_get_sink_cap(
		struct pd_port *pd_port, uint8_t next)
{
	if (pd_port->pe_state_curr != PE_SRC_READY)
		return false;

#if CONFIG_USB_PD_PR_SWAP
	if (pd_port->dpm_caps & DPM_CAP_LOCAL_DR_POWER) {
		PE_TRANSIT_STATE(pd_port, next);
		return true;
	}
#endif	/* CONFIG_USB_PD_PR_SWAP */

	pd_port->curr_unsupported_msg = true;

	return false;
}

static inline bool pd_process_ctrl_msg(
	struct pd_port *pd_port, struct pd_event *pd_event)

{
#if CONFIG_USB_PD_PARTNER_CTRL_MSG_FIRST
	struct tcpc_device __maybe_unused *tcpc = pd_port->tcpc;

	switch (pd_port->pe_state_curr) {
	case PE_SRC_GET_SINK_CAP:

#if CONFIG_USB_PD_PR_SWAP
	case PE_DR_SRC_GET_SOURCE_CAP:
#endif	/* CONFIG_USB_PD_PR_SWAP */
		if (pd_event->msg >= PD_CTRL_GET_SOURCE_CAP &&
			pd_event->msg <= PD_CTRL_VCONN_SWAP) {
			PE_DBG("Port Partner Request First\n");
			pd_port->pe_state_curr = PE_SRC_READY;
			pd_disable_timer(
				pd_port, PD_TIMER_SENDER_RESPONSE);
		}
		break;
	}
#endif	/* CONFIG_USB_PD_PARTNER_CTRL_MSG_FIRST */

	switch (pd_event->msg) {
	case PD_CTRL_GOOD_CRC:
		return pd_process_ctrl_msg_good_crc(pd_port, pd_event);

	case PD_CTRL_ACCEPT:
		if (PE_MAKE_STATE_TRANSIT_SINGLE(
			PE_SRC_SEND_SOFT_RESET, PE_SRC_SEND_CAPABILITIES))
			return true;
		break;

	case PD_CTRL_GET_SOURCE_CAP:
		if (PE_MAKE_STATE_TRANSIT_SINGLE(
			PE_SRC_READY, PE_SRC_SEND_CAPABILITIES))
			return true;
		break;

	case PD_CTRL_GET_SINK_CAP:
		if (pd_process_ctrl_msg_get_sink_cap(
			pd_port, PE_DR_SRC_GIVE_SINK_CAP))
			return true;
		break;

#if CONFIG_USB_PD_REV30
	case PD_CTRL_NOT_SUPPORTED:
		if (PE_MAKE_STATE_TRANSIT_SINGLE(
			PE_SRC_READY, PE_SRC_NOT_SUPPORTED_RECEIVED))
			return true;
		break;

#if CONFIG_USB_PD_REV30_SRC_CAP_EXT_LOCAL
	case PD_CTRL_GET_SOURCE_CAP_EXT:
		if (PE_MAKE_STATE_TRANSIT_SINGLE(
			PE_SRC_READY, PE_SRC_GIVE_SOURCE_CAP_EXT))
			return true;
		break;
#endif	/* CONFIG_USB_PD_REV30_SRC_CAP_EXT_LOCAL */

#if CONFIG_USB_PD_REV30_STATUS_LOCAL
	case PD_CTRL_GET_STATUS:
		if (PE_MAKE_STATE_TRANSIT_SINGLE(
			PE_SRC_READY, PE_SRC_GIVE_SOURCE_STATUS))
			return true;
		break;
#endif	/* CONFIG_USB_PD_REV30_STATUS_LOCAL */

#if CONFIG_USB_PD_REV30_PPS_SOURCE
	case PD_CTRL_GET_PPS_STATUS:
		if (PE_MAKE_STATE_TRANSIT_SINGLE(
			PE_SRC_READY, PE_SRC_GIVE_PPS_STATUS))
			return true;
		break;
#endif	/* CONFIG_USB_PD_REV30_PPS_SOURCE */

	case PD_CTRL_GET_SINK_CAP_EXT:
		if (pd_process_ctrl_msg_get_sink_cap(
			pd_port, PE_DR_SRC_GIVE_SINK_CAP_EXT))
			return true;
		break;
#endif	/* CONFIG_USB_PD_REV30 */

	default:
		pd_port->curr_unsupported_msg = true;
		break;
	}

	return pd_process_protocol_error(pd_port, pd_event);
}

/*
 * [BLOCK] Process Data MSG
 */

static inline bool pd_process_data_msg(
	struct pd_port *pd_port, struct pd_event *pd_event)

{
	switch (pd_event->msg) {
#if CONFIG_USB_PD_PR_SWAP
	case PD_DATA_SOURCE_CAP:
		if (PE_MAKE_STATE_TRANSIT_SINGLE(
			PE_DR_SRC_GET_SOURCE_CAP, PE_SRC_READY))
			return true;
		break;
#endif	/* CONFIG_USB_PD_PR_SWAP */

	case PD_DATA_SINK_CAP:
		if (PE_MAKE_STATE_TRANSIT_SINGLE(
			PE_SRC_GET_SINK_CAP, PE_SRC_READY))
			return true;
		break;

	case PD_DATA_REQUEST:
		if (PE_MAKE_STATE_TRANSIT(PD_DATA_MSG_REQUEST))
			return true;
		break;

#if CONFIG_USB_PD_REV30
#if CONFIG_USB_PD_REV30_ALERT_REMOTE
	case PD_DATA_ALERT:
		if (PE_MAKE_STATE_TRANSIT_SINGLE(
			PE_SRC_READY, PE_SRC_SINK_ALERT_RECEIVED))
			return true;
		break;
#endif	/* CONFIG_USB_PD_REV30_ALERT_REMOTE */
#endif	/* CONFIG_USB_PD_REV30 */

	default:
		pd_port->curr_unsupported_msg = true;
		break;
	}

	return pd_process_protocol_error(pd_port, pd_event);
}

/*
 * [BLOCK] Process Extend MSG
 */
#if CONFIG_USB_PD_REV30

static inline bool pd_process_ext_msg(
		struct pd_port *pd_port, struct pd_event *pd_event)
{
	switch (pd_event->msg) {

#if CONFIG_USB_PD_REV30_SRC_CAP_EXT_REMOTE
	case PD_EXT_SOURCE_CAP_EXT:
		if (PE_MAKE_STATE_TRANSIT_SINGLE(
			PE_DR_SRC_GET_SOURCE_CAP_EXT, PE_SRC_READY))
			return true;
		break;
#endif	/* CONFIG_USB_PD_REV30_SRC_CAP_EXT_REMOTE */

#if CONFIG_USB_PD_REV30_STATUS_REMOTE
	case PD_EXT_STATUS:
		if (PE_MAKE_STATE_TRANSIT_SINGLE(
			PE_SRC_GET_SINK_STATUS, PE_SRC_READY))
			return true;
		break;
#endif	/* CONFIG_USB_PD_REV30_STATUS_REMOTE */

	case PD_EXT_SINK_CAP_EXT:
		if (PE_MAKE_STATE_TRANSIT_SINGLE(
			PE_SRC_GET_SINK_CAP_EXT, PE_SRC_READY))
			return true;
		break;

	default:
		pd_port->curr_unsupported_msg = true;
		break;
	}

	return pd_process_protocol_error(pd_port, pd_event);
}

#endif	/* CONFIG_USB_PD_REV30 */

/*
 * [BLOCK] Process DPM MSG
 */

static inline bool pd_process_dpm_msg(
	struct pd_port *pd_port, struct pd_event *pd_event)
{
	switch (pd_event->msg) {
	case PD_DPM_ACK:
		return PE_MAKE_STATE_TRANSIT(PD_DPM_MSG_ACK);

	case PD_DPM_NAK:
		return PE_MAKE_STATE_TRANSIT_SINGLE(
			PE_SRC_NEGOTIATE_CAPABILITIES,
			PE_SRC_CAPABILITY_RESPONSE);

	default:
		return false;
	}
}

/*
 * [BLOCK] Process HW MSG
 */

static inline bool pd_process_hw_msg_vbus_present(
	struct pd_port *pd_port, struct pd_event *pd_event)
{
	switch (pd_port->pe_state_curr) {
	case PE_SRC_TRANSITION_TO_DEFAULT:
		pd_put_pe_event(pd_port, PD_PE_POWER_ROLE_AT_DEFAULT);
		break;
	}

	return false;
}

static inline bool pd_process_hw_msg_tx_failed(
	struct pd_port *pd_port, struct pd_event *pd_event)
{
	struct pe_data *pe_data = &pd_port->pe_data;
	struct tcpc_device __maybe_unused *tcpc = pd_port->tcpc;

	if (pd_port->pe_state_curr == PE_SRC_SEND_CAPABILITIES) {
		if (!pe_data->pd_connected || !pe_data->explicit_contract) {
			PE_TRANSIT_STATE(pd_port, PE_SRC_DISCOVERY);
			return true;
		}
	}

	if (pd_port->pe_state_curr == PE_SRC_CBL_SEND_SOFT_RESET) {
		PE_TRANSIT_STATE(pd_port, PE_SRC_SEND_CAPABILITIES);
		return true;
	}

	return pd_process_tx_failed_discard(pd_port, pd_event->msg);
}

static inline bool pd_process_hw_msg(
	struct pd_port *pd_port, struct pd_event *pd_event)
{
	switch (pd_event->msg) {
	case PD_HW_VBUS_PRESENT:
		return pd_process_hw_msg_vbus_present(pd_port, pd_event);

	case PD_HW_VBUS_SAFE0V:
		pd_enable_timer(pd_port, PD_TIMER_SRC_RECOVER);
		return false;

	case PD_HW_VBUS_STABLE:
		return PE_MAKE_STATE_TRANSIT_SINGLE(
			PE_SRC_TRANSITION_SUPPLY, PE_SRC_TRANSITION_SUPPLY2);

	case PD_HW_TX_FAILED:
		fallthrough;
	case PD_HW_TX_DISCARD:
		return pd_process_hw_msg_tx_failed(pd_port, pd_event);

	default:
		return false;
	};
}

/*
 * [BLOCK] Process PE MSG
 */

static inline bool pd_process_pe_msg(
	struct pd_port *pd_port, struct pd_event *pd_event)
{
	switch (pd_event->msg) {
	case PD_PE_RESET_PRL_COMPLETED:
		return  PE_MAKE_STATE_TRANSIT_SINGLE(
			PE_SRC_STARTUP, PE_SRC_SEND_CAPABILITIES);

	case PD_PE_POWER_ROLE_AT_DEFAULT:
		return  PE_MAKE_STATE_TRANSIT_SINGLE(
			PE_SRC_TRANSITION_TO_DEFAULT, PE_SRC_STARTUP);

	default:
		return false;
	}
}

/*
 * [BLOCK] Process Timer MSG
 */
static inline bool pd_process_timer_msg_source_start(
	struct pd_port *pd_port, struct pd_event *pd_event)
{
	if (pd_is_discover_cable(pd_port) &&
	    !pd_port->pe_data.cable_discovered_state) {
		if (pd_is_reset_cable(pd_port)) {
			PE_TRANSIT_STATE(pd_port, PE_SRC_CBL_SEND_SOFT_RESET);
			return true;
		}

		if (vdm_put_dpm_discover_cable_id_event(pd_port))
			return false;
	}

	switch (pd_port->pe_state_curr) {
	case PE_SRC_STARTUP:
	case PE_SRC_CBL_SEND_SOFT_RESET:
		PE_TRANSIT_STATE(pd_port, PE_SRC_SEND_CAPABILITIES);
		return true;
	}

	return false;
};

static inline bool pd_process_timer_msg_source_cap(
	struct pd_port *pd_port, struct pd_event *pd_event)
{
	if (pd_port->pe_state_curr != PE_SRC_DISCOVERY)
		return false;

	if (pd_port->pe_data.cap_counter <= PD_CAPS_COUNT)
		PE_TRANSIT_STATE(pd_port, PE_SRC_SEND_CAPABILITIES);
	else	/* in this state, PD always not connected */
		PE_TRANSIT_STATE(pd_port, PE_SRC_DISABLED);

	return true;
}

static inline bool pd_process_timer_msg_no_response(
	struct pd_port *pd_port, struct pd_event *pd_event)
{
	if (pd_port->pe_data.hard_reset_counter <= PD_HARD_RESET_COUNT)
		PE_TRANSIT_STATE(pd_port, PE_SRC_HARD_RESET);
	else if (pd_port->pe_data.pd_prev_connected)
		PE_TRANSIT_STATE(pd_port, PE_ERROR_RECOVERY);
	else
		PE_TRANSIT_STATE(pd_port, PE_SRC_DISABLED);

	return true;
}

static inline bool pd_process_timer_msg(
	struct pd_port *pd_port, struct pd_event *pd_event)
{
	switch (pd_event->msg) {
	case PD_TIMER_SOURCE_CAPABILITY:
		return pd_process_timer_msg_source_cap(pd_port, pd_event);

	case PD_TIMER_SENDER_RESPONSE:
		return PE_MAKE_STATE_TRANSIT_SINGLE(
			PE_SRC_CBL_SEND_SOFT_RESET, PE_SRC_SEND_CAPABILITIES);

	case PD_TIMER_PS_HARD_RESET:
		return PE_MAKE_STATE_TRANSIT(PD_TIMER_PS_HARD_RESET);

	case PD_TIMER_SOURCE_START:
		return pd_process_timer_msg_source_start(pd_port, pd_event);

	case PD_TIMER_NO_RESPONSE:
		return pd_process_timer_msg_no_response(pd_port, pd_event);

	case PD_TIMER_SOURCE_TRANSITION:
		if (pd_port->state_machine != PE_STATE_MACHINE_PR_SWAP)
			pd_dpm_src_transition_power(pd_port);
		break;

	case PD_TIMER_DISCOVER_ID:
		vdm_put_dpm_discover_cable_id_event(pd_port);
		break;

	case PD_TIMER_SRC_RECOVER:
		pd_dpm_source_vbus(pd_port, true);
		pd_enable_vbus_valid_detection(pd_port, true);
		break;

#if CONFIG_USB_PD_REV30
	case PD_TIMER_SINK_TX:
#if CONFIG_USB_PD_REV30_SRC_FLOW_DELAY_STARTUP
		if (pd_port->pe_data.pd_traffic_control == PD_SOURCE_TX_START)
			pd_port->pe_data.pd_traffic_control = PD_SINK_TX_OK;
#endif	/* CONFIG_USB_PD_REV30_SRC_FLOW_DELAY_STARTUP */
		if (pd_port->pe_data.pd_traffic_control == PD_SINK_TX_NG)
			pd_port->pe_data.pd_traffic_control = PD_SOURCE_TX_OK;
		dpm_reaction_set_ready_once(pd_port);
		break;

	case PD_TIMER_CK_NOT_SUPPORTED:
		return PE_MAKE_STATE_TRANSIT_SINGLE(
			PE_SRC_CHUNK_RECEIVED, PE_SRC_SEND_NOT_SUPPORTED);
#endif	/* CONFIG_USB_PD_REV30 */
	}

	return false;
}

/*
 * [BLOCK] Process Policy Engine's SRC Message
 */

bool pd_process_event_src(struct pd_port *pd_port, struct pd_event *pd_event)
{
	switch (pd_event->event_type) {
	case PD_EVT_CTRL_MSG:
		return pd_process_ctrl_msg(pd_port, pd_event);

	case PD_EVT_DATA_MSG:
		return pd_process_data_msg(pd_port, pd_event);

#if CONFIG_USB_PD_REV30
	case PD_EVT_EXT_MSG:
		return pd_process_ext_msg(pd_port, pd_event);
#endif	/* CONFIG_USB_PD_REV30 */

	case PD_EVT_DPM_MSG:
		return pd_process_dpm_msg(pd_port, pd_event);

	case PD_EVT_HW_MSG:
		return pd_process_hw_msg(pd_port, pd_event);

	case PD_EVT_PE_MSG:
		return pd_process_pe_msg(pd_port, pd_event);

	case PD_EVT_TIMER_MSG:
		return pd_process_timer_msg(pd_port, pd_event);

	default:
		return false;
	}
}
#endif /* CONFIG_USB_POWER_DELIVERY */
