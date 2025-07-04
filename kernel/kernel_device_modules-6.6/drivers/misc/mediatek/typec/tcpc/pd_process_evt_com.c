// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 MediaTek Inc.
 */

#if IS_ENABLED(CONFIG_USB_POWER_DELIVERY)
#include "inc/pd_core.h"
#include "inc/tcpci_event.h"
#include "inc/pd_process_evt.h"
#include "inc/pd_dpm_core.h"

/*
 * [BLOCK] DRP (dr_swap, pr_swap, vconn_swap)
 */

#if CONFIG_USB_PD_DR_SWAP
static inline bool pd_evaluate_reject_dr_swap(struct pd_port *pd_port)
{
	if (pd_port->dpm_caps & DPM_CAP_LOCAL_DR_DATA) {
		if (pd_port->data_role == PD_ROLE_DFP)
			return pd_port->dpm_caps &
				DPM_CAP_DR_SWAP_REJECT_AS_UFP;
		return pd_port->dpm_caps & DPM_CAP_DR_SWAP_REJECT_AS_DFP;
	}

	return true;
}
#endif	/* CONFIG_USB_PD_DR_SWAP */

#if CONFIG_USB_PD_PR_SWAP
static inline bool pd_evaluate_reject_pr_swap(struct pd_port *pd_port)
{
	struct tcpc_device __maybe_unused *tcpc = pd_port->tcpc;

	if (tcpc->bootmode == 8 || tcpc->bootmode == 9)
		return true;

	if (pd_port->dpm_caps & DPM_CAP_LOCAL_DR_POWER) {
		if (pd_port->power_role == PD_ROLE_SOURCE)
			return pd_port->dpm_caps &
				DPM_CAP_PR_SWAP_REJECT_AS_SNK;
		return pd_port->dpm_caps & DPM_CAP_PR_SWAP_REJECT_AS_SRC;
	}

	return true;
}
#endif	/* CONFIG_USB_PD_PR_SWAP */

static inline bool pd_process_ctrl_msg_dr_swap(
		struct pd_port *pd_port, struct pd_event *pd_event)
{
	if (pd_port->pe_data.modal_operation) {
		pe_transit_hard_reset_state(pd_port);
		return true;
	}

#if CONFIG_USB_PD_DR_SWAP
	if (!pd_check_pe_state_ready(pd_port))
		return false;

	if (!pd_evaluate_reject_dr_swap(pd_port)) {
		pd_port->pe_data.during_swap = false;
		pd_port->state_machine = PE_STATE_MACHINE_DR_SWAP;

		PE_TRANSIT_DATA_STATE(pd_port,
			PE_DRS_UFP_DFP_EVALUATE_DR_SWAP,
			PE_DRS_DFP_UFP_EVALUATE_DR_SWAP);
		return true;
	}
#endif	/* CONFIG_USB_PD_DR_SWAP */

	PE_TRANSIT_STATE(pd_port, PE_REJECT);
	return true;
}

static inline bool pd_process_ctrl_msg_pr_swap(
		struct pd_port *pd_port, struct pd_event *pd_event)
{
#if CONFIG_USB_PD_PR_SWAP
	if (!pd_check_pe_state_ready(pd_port))
		return false;

	if (!pd_evaluate_reject_pr_swap(pd_port)) {
		pd_port->pe_data.during_swap = false;
		pd_port->state_machine = PE_STATE_MACHINE_PR_SWAP;
		pe_transit_evaluate_pr_swap_state(pd_port);
		return true;
	}
#endif	/* CONFIG_USB_PD_PR_SWAP */

	PE_TRANSIT_STATE(pd_port, PE_REJECT);
	return true;
}

static inline bool pd_process_ctrl_msg_vconn_swap(
	struct pd_port *pd_port, struct pd_event *pd_event)
{
#if CONFIG_USB_PD_VCONN_SWAP
	if (!pd_check_pe_state_ready(pd_port))
		return false;

	pd_port->state_machine = PE_STATE_MACHINE_VCONN_SWAP;
	PE_TRANSIT_STATE(pd_port, PE_VCS_EVALUATE_SWAP);
	return true;
#else
	if (!pd_check_rev30(pd_port)) {
		PE_TRANSIT_STATE(pd_port, PE_REJECT);
		return true;
	}

	return false;
#endif	/* CONFIG_USB_PD_VCONN_SWAP */
}

/*
 * [BLOCK] BIST
 */

static inline bool pd_process_data_msg_bist(
	struct pd_port *pd_port, struct pd_event *pd_event)
{
	struct tcpc_device __maybe_unused *tcpc = pd_port->tcpc;

	if (pd_port->request_v > 5000) {
		PE_INFO("bist_not_vsafe5v\n");
		return false;
	}

	switch (BDO_MODE(pd_event->pd_msg->payload[0])) {
	case BDO_MODE_TEST_DATA:
		PE_DBG("bist_test\n");
		PE_TRANSIT_STATE(pd_port, PE_BIST_TEST_DATA);
		return true;

	case BDO_MODE_CARRIER2:
		PE_DBG("bist_cm2\n");
		PE_TRANSIT_STATE(pd_port, PE_BIST_CARRIER_MODE_2);
		return true;

	default:
		PE_DBG("Unsupport BIST\n");
		return false;
	}

	return false;
}

/*
 * [BLOCK] Process Ctrl MSG
 */

static void pd_cancel_dpm_reaction(struct pd_port *pd_port)
{
	if (pd_port->pe_data.dpm_reaction_id < DPM_REACTION_REJECT_CANCEL)
		dpm_reaction_clear(pd_port, pd_port->pe_data.dpm_reaction_id);
}

static bool pd_process_ctrl_msg_wait_reject(struct pd_port *pd_port)
{
	pd_cancel_dpm_reaction(pd_port);

	if (pd_port->state_machine == PE_STATE_MACHINE_PR_SWAP)
		pd_notify_pe_cancel_pr_swap(pd_port);

	pe_transit_ready_state(pd_port);
	return true;
}

static bool pd_process_tx_msg(struct pd_port *pd_port, uint8_t msg)
{
#if CONFIG_USB_PD_DISCARD_AND_UNEXPECT_MSG
	if (msg != PD_HW_TX_DISCARD)
		pd_port->pe_data.pd_sent_ams_init_cmd = true;

	if (pd_port->pe_state_curr == PE_SEND_SOFT_RESET_TX_WAIT) {
		pe_transit_soft_reset_state(pd_port);
		return true;
	} else if (pd_port->pe_state_curr == PE_RECV_SOFT_RESET_TX_WAIT) {
		pe_transit_soft_reset_recv_state(pd_port);
		return true;
	} else if (pd_port->pe_state_curr == PE_UNEXPECTED_TX_WAIT) {
		if (msg == PD_HW_TX_DISCARD)
			pe_transit_ready_state(pd_port);
		else
			pe_transit_soft_reset_state(pd_port);
		return true;
	}
#endif	/* CONFIG_USB_PD_DISCARD_AND_UNEXPECT_MSG */

	return false;
}

static inline bool pd_process_ctrl_msg_good_crc(
	struct pd_port *pd_port, struct pd_event *pd_event)
{
	if (pd_process_tx_msg(pd_port, PD_CTRL_GOOD_CRC))
		return true;

	if (pd_port->pe_data.pe_state_flags &
		PE_STATE_FLAG_BACK_READY_IF_RECV_GOOD_CRC) {
		pe_transit_ready_state(pd_port);
		return true;
	}

	if (pd_port->pe_data.pe_state_flags &
		PE_STATE_FLAG_ENABLE_SENDER_RESPONSE_TIMER)
		pd_enable_timer(pd_port, PD_TIMER_SENDER_RESPONSE);

	return false;
}

static inline bool pd_process_ctrl_msg(
	struct pd_port *pd_port, struct pd_event *pd_event)
{
	bool ret = false;
#if CONFIG_USB_PD_REV30
	uint8_t ready_state = pe_get_curr_ready_state(pd_port);

	if (!pd_check_rev30(pd_port) &&
		pd_event->msg >= PD_CTRL_PD30_START) {
		pd_event->msg = PD_CTRL_MSG_NR;
		return false;
	}
#endif	/* CONFIG_USB_PD_REV30 */

	switch (pd_event->msg) {
	case PD_CTRL_GOOD_CRC:
		ret = pd_process_ctrl_msg_good_crc(pd_port, pd_event);
		break;

	case PD_CTRL_REJECT:
		pd_notify_tcp_event_2nd_result(pd_port, TCP_DPM_RET_REJECT);

		if (pd_port->pe_data.pe_state_flags &
			PE_STATE_FLAG_BACK_READY_IF_RECV_REJECT) {
			return pd_process_ctrl_msg_wait_reject(pd_port);
		}
		break;
	case PD_CTRL_WAIT:
		pd_notify_tcp_event_2nd_result(pd_port, TCP_DPM_RET_WAIT);

		if (pd_port->pe_data.pe_state_flags &
			PE_STATE_FLAG_BACK_READY_IF_RECV_WAIT) {
			return pd_process_ctrl_msg_wait_reject(pd_port);
		}
		break;

	case PD_CTRL_SOFT_RESET:
		if (!pd_port->pe_data.during_swap &&
			!pd_check_pe_during_hard_reset(pd_port)) {

#if CONFIG_USB_PD_DISCARD_AND_UNEXPECT_MSG
			if (pd_is_pe_wait_pd_transmit_done(pd_port)) {
				PE_TRANSIT_STATE(pd_port,
						 PE_RECV_SOFT_RESET_TX_WAIT);
				return true;
			}
#endif	/* CONFIG_USB_PD_DISCARD_AND_UNEXPECT_MSG */

			pe_transit_soft_reset_recv_state(pd_port);
			return true;
		}
		break;

	/* Swap */
	case PD_CTRL_DR_SWAP:
		ret = pd_process_ctrl_msg_dr_swap(pd_port, pd_event);
		break;

	case PD_CTRL_PR_SWAP:
		ret = pd_process_ctrl_msg_pr_swap(pd_port, pd_event);
		break;

	case PD_CTRL_VCONN_SWAP:
		ret = pd_process_ctrl_msg_vconn_swap(pd_port, pd_event);
		break;

#if CONFIG_USB_PD_REV30

#if CONFIG_USB_PD_REV30_COUNTRY_CODE_LOCAL
	case PD_CTRL_GET_COUNTRY_CODE:
		if (pd_port->country_nr) {
			ret = PE_MAKE_STATE_TRANSIT_SINGLE(
				ready_state, PE_GIVE_COUNTRY_CODES);
		}
		break;
#endif	/* CONFIG_USB_PD_REV30_COUNTRY_CODE_LOCAL */

	case PD_CTRL_NOT_SUPPORTED:
		pd_cancel_dpm_reaction(pd_port);
		pd_notify_pe_reset_protocol(pd_port);
		pd_notify_tcp_event_2nd_result(
				pd_port, TCP_DPM_RET_NOT_SUPPORT);

		if (pd_port->pe_data.pe_state_flags &
			PE_STATE_FLAG_BACK_READY_IF_SR_TIMER_TOUT) {
			pe_transit_ready_state(pd_port);
			return true;
		} else if (pd_port->pe_data.vdm_state_timer < PD_TIMER_NR) {
			vdm_put_pe_event(pd_port->tcpc, PD_PE_VDM_NOT_SUPPORT);
		}
		break;

	case PD_CTRL_GET_REVISION:
		ret = PE_MAKE_STATE_TRANSIT_SINGLE(
			ready_state, PE_GIVE_REVISION);
		break;
#endif	/* CONFIG_USB_PD_REV30 */
	}

	return ret;
}

/*
 * [BLOCK] Process Data MSG
 */

static inline bool pd_process_data_msg(
	struct pd_port *pd_port, struct pd_event *pd_event)
{
	bool ret = false;
	uint8_t ready_state = pe_get_curr_ready_state(pd_port);

#if CONFIG_USB_PD_REV30
	if (!pd_check_rev30(pd_port) &&
		pd_event->msg >= PD_DATA_PD30_START) {
		pd_event->msg = PD_DATA_MSG_NR;
		return false;
	}
#endif	/* CONFIG_USB_PD_REV30 */

	switch (pd_event->msg) {
	case PD_DATA_BIST:
		if (pd_port->pe_state_curr == ready_state)
			ret = pd_process_data_msg_bist(pd_port, pd_event);
		break;

#if CONFIG_USB_PD_REV30
#if CONFIG_USB_PD_REV30_BAT_STATUS_REMOTE
	case PD_DATA_BAT_STATUS:
		ret = PE_MAKE_STATE_TRANSIT_SINGLE(
			PE_GET_BATTERY_STATUS, ready_state);
		break;
#endif	/* CONFIG_USB_PD_REV30_BAT_STATUS_REMOTE */

#if CONFIG_USB_PD_REV30_COUNTRY_INFO_LOCAL
	case PD_DATA_GET_COUNTRY_INFO:
		if (pd_port->country_nr) {
			ret = PE_MAKE_STATE_TRANSIT_SINGLE(
				ready_state, PE_GIVE_COUNTRY_INFO);
		}
		break;
#endif	/* CONFIG_USB_PD_REV30_COUNTRY_INFO_LOCAL */

	case PD_DATA_REVISION:
		ret = PE_MAKE_STATE_TRANSIT_SINGLE(
			PE_GET_REVISION, ready_state);
		break;
#endif	/* CONFIG_USB_PD_REV30 */
	}

	return ret;
}

/*
 * [BLOCK] Process Extend MSG
 */

#if CONFIG_USB_PD_REV30
static inline bool pd_process_ext_msg(
		struct pd_port *pd_port, struct pd_event *pd_event)
{
	bool ret = false;
	uint8_t __maybe_unused ready_state = pe_get_curr_ready_state(pd_port);

	if (!pd_check_rev30(pd_port)) {
		pd_event->msg = PD_DATA_MSG_NR;
		return false;
	}

#if !CONFIG_USB_PD_REV30_CHUNKING_BY_PE
	if (pd_port->pe_state_curr == ready_state &&
		pd_is_multi_chunk_msg(pd_port)) {
		pd_port->curr_unsupported_msg = true;
		return pd_process_protocol_error(pd_port, pd_event);
	}
#endif	/* CONFIG_USB_PD_REV30_CHUNKING_BY_PE */

	switch (pd_event->msg) {

#if CONFIG_USB_PD_REV30_BAT_CAP_LOCAL
	case PD_EXT_GET_BAT_CAP:
		if (pd_port->bat_nr) {
			ret = PE_MAKE_STATE_TRANSIT_SINGLE(
				ready_state, PE_GIVE_BATTERY_CAP);
		}
		break;
#endif	/* CONFIG_USB_PD_REV30_BAT_CAP_LOCAL */

#if CONFIG_USB_PD_REV30_BAT_STATUS_LOCAL
	case PD_EXT_GET_BAT_STATUS:
		if (pd_port->bat_nr) {
			ret = PE_MAKE_STATE_TRANSIT_SINGLE(
				ready_state, PE_GIVE_BATTERY_STATUS);
		}
		break;
#endif	/* CONFIG_USB_PD_REV30_BAT_STATUS_LOCAL */

#if CONFIG_USB_PD_REV30_BAT_CAP_REMOTE
	case PD_EXT_BAT_CAP:
		ret = PE_MAKE_STATE_TRANSIT_SINGLE(
			PE_GET_BATTERY_CAP, ready_state);
		break;
#endif	/* CONFIG_USB_PD_REV30_BAT_CAP_REMOTE */

#if CONFIG_USB_PD_REV30_MFRS_INFO_LOCAL
	case PD_EXT_GET_MFR_INFO:
		ret = PE_MAKE_STATE_TRANSIT_SINGLE(
			ready_state, PE_GIVE_MANUFACTURER_INFO);
		break;
#endif	/* CONFIG_USB_PD_REV30_MFRS_INFO_LOCAL */

#if CONFIG_USB_PD_REV30_MFRS_INFO_REMOTE
	case PD_EXT_MFR_INFO:
		ret = PE_MAKE_STATE_TRANSIT_SINGLE(
			PE_GET_MANUFACTURER_INFO, ready_state);
		break;
#endif	/* CONFIG_USB_PD_REV30_MFRS_INFO_REMOTE */

#if CONFIG_USB_PD_REV30_COUNTRY_INFO_REMOTE
	case PD_EXT_COUNTRY_INFO:
		ret = PE_MAKE_STATE_TRANSIT_SINGLE(
			PE_GET_COUNTRY_INFO, ready_state);
		break;
#endif	/* CONFIG_USB_PD_REV30_COUNTRY_INFO_REMOTE */

#if CONFIG_USB_PD_REV30_COUNTRY_CODE_REMOTE
	case PD_EXT_COUNTRY_CODES:
		ret = PE_MAKE_STATE_TRANSIT_SINGLE(
			PE_GET_COUNTRY_CODES, ready_state);
		break;
#endif	/* CONFIG_USB_PD_REV30_COUNTRY_CODE_REMOTE */
	}

	return ret;
}
#endif	/* CONFIG_USB_PD_REV30 */

/*
 * [BLOCK] Process DPM MSG
 */

static inline bool pd_process_dpm_msg(
	struct pd_port *pd_port, struct pd_event *pd_event)
{
	bool ret = false;

	switch (pd_event->msg) {
	case PD_DPM_ACK:
#if CONFIG_USB_PD_DISCARD_AND_UNEXPECT_MSG
		if (pd_port->pe_state_curr == PE_SEND_SOFT_RESET_STANDBY) {
			pe_transit_soft_reset_state(pd_port);
			return true;
		}
#endif	/* CONFIG_USB_PD_DISCARD_AND_UNEXPECT_MSG */

		if (pd_port->pe_data.pe_state_flags &
			PE_STATE_FLAG_BACK_READY_IF_DPM_ACK) {
			pe_transit_ready_state(pd_port);
			return true;
		}
		break;

#if CONFIG_USB_PD_REV30
	case PD_DPM_NOT_SUPPORT:
		if (pd_check_rev30(pd_port)) {
			PE_TRANSIT_STATE(pd_port, PE_VDM_NOT_SUPPORTED);
			return true;
		}
		break;
	case PD_DPM_CABLE_NOT_SUPPORT:
		if (pd_get_rev(pd_port, TCPC_TX_SOP_PRIME) >= PD_REV30) {
			PE_TRANSIT_STATE(pd_port, PE_VDM_NOT_SUPPORTED);
			return true;
		}
		break;
#endif	/* CONFIG_USB_PD_REV30 */
	}

	return ret;
}

/*
 * [BLOCK] Process HW MSG
 */

static inline bool pd_process_recv_hard_reset(
		struct pd_port *pd_port, struct pd_event *pd_event)
{
#if CONFIG_USB_PD_RENEGOTIATION_COUNTER
	if (pd_check_pe_during_hard_reset(pd_port))
		pd_port->pe_data.renegotiation_count++;
#endif	/* CONFIG_USB_PD_RENEGOTIATION_COUNTER */

	pe_transit_hard_reset_recv_state(pd_port);
	return true;
}

static bool pd_process_hw_msg_tx_failed_discard(
	struct pd_port *pd_port, struct pd_event *pd_event)
{
#if CONFIG_USB_PD_RENEGOTIATION_COUNTER
	struct tcpc_device __maybe_unused *tcpc = pd_port->tcpc;

	if (pd_port->pe_data.renegotiation_count > PD_HARD_RESET_COUNT) {
		PE_INFO("renegotiation failed\n");
		PE_TRANSIT_STATE(pd_port, PE_ERROR_RECOVERY);
		return true;
	}
#endif	/* CONFIG_USB_PD_RENEGOTIATION_COUNTER */

	if (pd_process_tx_msg(pd_port, pd_event->msg))
		return true;
	else if (pd_port->pe_data.pe_state_flags &
		PE_STATE_FLAG_BACK_READY_IF_TX_FAILED) {
		pd_notify_tcp_event_2nd_result(
			pd_port, TCP_DPM_RET_NO_RESPONSE);
		pe_transit_ready_state(pd_port);
		return true;
	} else if (pd_port->pe_data.pe_state_flags &
		PE_STATE_FLAG_HRESET_IF_TX_FAILED) {
		pe_transit_hard_reset_state(pd_port);
		return true;
	} else if (pd_port->pe_data.pe_state_flags &
		PE_STATE_FLAG_ER_IF_TX_FAILED) {
		PE_TRANSIT_STATE(pd_port, PE_ERROR_RECOVERY);
		return true;
	}

	return false;
}

static inline bool pd_process_hw_msg(
	struct pd_port *pd_port, struct pd_event *pd_event)
{
	switch (pd_event->msg) {
	case PD_HW_RECV_HARD_RESET:
		return pd_process_recv_hard_reset(pd_port, pd_event);

	case PD_HW_TX_FAILED:
	case PD_HW_TX_DISCARD:
		return pd_process_hw_msg_tx_failed_discard(pd_port, pd_event);
#if CONFIG_USB_PD_RETRY_CRC_DISCARD
	case PD_HW_TX_RETRANSMIT:
		tcpci_retransmit(pd_port->tcpc);
		return true;
#endif	/* CONFIG_USB_PD_RETRY_CRC_DISCARD */

	default:
		return false;
	};
}

/*
 * [BLOCK] Process Timer MSG
 */

#if CONFIG_USB_PD_CHECK_RX_PENDING_IF_SRTOUT
static inline bool pd_check_rx_pending(struct pd_port *pd_port)
{
	bool pending = false;
	struct tcpc_device __maybe_unused *tcpc = pd_port->tcpc;
	uint32_t alert_status = 0, alert_mask = 0;

	if (mutex_is_locked(&tcpc->rxbuf_lock)) {
		PE_INFO("rx_pending\n");
		pending = true;
	} else if (tcpci_get_alert_status_and_mask(tcpc, &alert_status,
						   &alert_mask) >= 0) {
		alert_status &= alert_mask;
		if (alert_status & TCPC_V10_REG_ALERT_RX_STATUS) {
			PE_INFO("rx_pending2\n");
			pending = true;
		}
	}

	tcpc->pd_rx_pending = pending;
	if (pending)
		pd_enable_timer(pd_port, PD_TIMER_SENDER_RESPONSE);

	return pending;
}
#endif	/* CONFIG_USB_PD_CHECK_RX_PENDING_IF_SRTOUT */

static inline bool pd_process_timer_msg(
	struct pd_port *pd_port, struct pd_event *pd_event)
{
	uint8_t ready_state = pe_get_curr_ready_state(pd_port);

	switch (pd_event->msg) {
	case PD_TIMER_SENDER_RESPONSE:
#if CONFIG_USB_PD_CHECK_RX_PENDING_IF_SRTOUT
		if (pd_check_rx_pending(pd_port))
			return false;
#endif	/* CONFIG_USB_PD_CHECK_RX_PENDING_IF_SRTOUT */
		pd_cancel_dpm_reaction(pd_port);
		pd_notify_pe_cancel_pr_swap(pd_port);
		pd_notify_tcp_event_2nd_result(pd_port, TCP_DPM_RET_TIMEOUT);
		if (pd_port->pe_data.pe_state_flags &
			PE_STATE_FLAG_BACK_READY_IF_SR_TIMER_TOUT) {
			PE_TRANSIT_STATE(pd_port, ready_state);
			return true;
		}

		if (pd_port->pe_data.pe_state_flags &
			PE_STATE_FLAG_HRESET_IF_SR_TIMEOUT) {
			pe_transit_hard_reset_state(pd_port);
			return true;
		}
		break;
	case PD_TIMER_BIST_CONT_MODE:
		if (PE_MAKE_STATE_TRANSIT_SINGLE(
			PE_BIST_CARRIER_MODE_2, ready_state))
			return true;
		break;

#if CONFIG_USB_PD_DFP_FLOW_DELAY
	case PD_TIMER_DFP_FLOW_DELAY:
		if (pd_port->pe_state_curr == ready_state &&
			pd_port->data_role == PD_ROLE_DFP) {
			dpm_reaction_set_clear(pd_port,
				DPM_REACTION_CAP_READY_ONCE,
				DPM_REACTION_DFP_FLOW_DELAY);
		}
		break;
#endif	/* CONFIG_USB_PD_DFP_FLOW_DELAY */

#if CONFIG_USB_PD_UFP_FLOW_DELAY
	case PD_TIMER_UFP_FLOW_DELAY:
		if (pd_port->pe_state_curr == ready_state &&
			pd_port->data_role == PD_ROLE_UFP) {
			dpm_reaction_set_clear(pd_port,
				DPM_REACTION_CAP_READY_ONCE,
				DPM_REACTION_UFP_FLOW_DELAY);
		}
		break;
#endif	/* CONFIG_USB_PD_UFP_FLOW_DELAY */

#if CONFIG_USB_PD_VCONN_STABLE_DELAY
	case PD_TIMER_VCONN_STABLE:
		if (pd_port->vconn_role == PD_ROLE_VCONN_DYNAMIC_ON) {
			pd_set_vconn(pd_port, PD_ROLE_VCONN_ON);
			dpm_reaction_set_clear(pd_port,
				DPM_REACTION_CAP_READY_ONCE,
				DPM_REACTION_VCONN_STABLE_DELAY);
		}
		break;
#endif	/* CONFIG_USB_PD_VCONN_STABLE_DELAY */

#if CONFIG_USB_PD_REV30
	case PD_TIMER_DEFERRED_EVT:
		pd_notify_tcp_event_buf_reset(
			pd_port, TCP_DPM_RET_DROP_PE_BUSY);
		break;
#endif	/* CONFIG_USB_PD_REV30 */
	default:
		break;
	}

	return false;
}

bool pd_process_event_com(
	struct pd_port *pd_port, struct pd_event *pd_event)
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

	case PD_EVT_TIMER_MSG:
		return pd_process_timer_msg(pd_port, pd_event);

	default:
		return false;
	}
}
#endif /* CONFIG_USB_POWER_DELIVERY */
