// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 MediaTek Inc.
 */

#if IS_ENABLED(CONFIG_USB_POWER_DELIVERY)
#include "inc/pd_core.h"
#include "inc/pd_dpm_core.h"
#include "inc/tcpci.h"
#include "inc/pd_policy_engine.h"

static inline void *pd_get_tcp_event_data_object(struct pd_port *pd_port)
{
	return pd_port->tcp_event.tcp_dpm_data.data_object;
}

/*
 * Policy Engine General State Activity
 */

static void pe_idle_reset_data(struct pd_port *pd_port)
{
	struct pe_data *pe_data = &pd_port->pe_data;

	pd_reset_pe_timer(pd_port);
	pd_reset_svid_data(pd_port);

	pe_data->pd_prev_connected = false;
	pe_data->pd_connected = false;
	pe_data->explicit_contract = false;
	pe_data->pe_ready = false;

	pd_notify_pe_bist_mode(pd_port, PD_BIST_MODE_DISABLE);

	pd_port->state_machine = PE_STATE_MACHINE_IDLE;
	pd_port->last_src_pdo = 0;
	pd_port->dp_v21 = false;

	pd_enable_timer(pd_port, PD_TIMER_PE_IDLE_TOUT);
}

void pe_idle1_entry(struct pd_port *pd_port)
{
#if CONFIG_USB_PD_ERROR_RECOVERY_ONCE
	pd_port->error_recovery_once = 0;
#endif	/* CONFIG_USB_PD_ERROR_RECOVERY_ONCE */

	pe_idle_reset_data(pd_port);
	pd_try_put_pe_idle_event(pd_port);
}

void pe_idle2_entry(struct pd_port *pd_port)
{
	pd_free_unexpected_event(pd_port);
	memset(&pd_port->pe_data, 0, sizeof(struct pe_data));
	pe_data_init(&pd_port->pe_data);
	pd_set_rx_enable(pd_port, PD_RX_CAP_PE_IDLE);
	pd_disable_timer(pd_port, PD_TIMER_PE_IDLE_TOUT);
	pd_notify_pe_idle(pd_port);
}

void pe_reject_entry(struct pd_port *pd_port)
{
	PE_STATE_WAIT_TX_SUCCESS(pd_port);

	pd_send_sop_ctrl_msg(pd_port, PD_CTRL_REJECT);
}

void pe_error_recovery_entry(struct pd_port *pd_port)
{
#if CONFIG_USB_PD_ERROR_RECOVERY_ONCE
	pd_port->error_recovery_once++;
#endif	/* CONFIG_USB_PD_ERROR_RECOVERY_ONCE */

	pe_idle_reset_data(pd_port);
	pd_notify_pe_error_recovery(pd_port);
	pd_try_put_pe_idle_event(pd_port);
}

#if CONFIG_USB_PD_ERROR_RECOVERY_ONCE
void pe_error_recovery_once_entry(struct pd_port *pd_port)
{
	uint8_t state = PD_CONNECT_TYPEC_ONLY_SNK_DFT;

	pd_notify_pe_hard_reset_completed(pd_port);

	if (pd_port->power_role == PD_ROLE_SOURCE) {
		state = PD_CONNECT_TYPEC_ONLY_SRC;
		pd_dpm_dynamic_disable_vconn(pd_port);
	} else
		pd_dpm_sink_vbus(pd_port, true);

	pd_update_connect_state(pd_port, state);
}
#endif	/* CONFIG_USB_PD_ERROR_RECOVERY_ONCE */

void pe_bist_test_data_entry(struct pd_port *pd_port)
{
	PE_STATE_IGNORE_UNKNOWN_EVENT(pd_port);

	pd_notify_pe_bist_mode(pd_port, PD_BIST_MODE_TEST_DATA);
}

void pe_bist_test_data_exit(struct pd_port *pd_port)
{
	pd_notify_pe_bist_mode(pd_port, PD_BIST_MODE_DISABLE);
}

void pe_bist_carrier_mode_2_entry(struct pd_port *pd_port)
{
	pd_notify_pe_bist_mode(pd_port, PD_BIST_MODE_CARRIER_2);
	pd_enable_pe_state_timer(pd_port, PD_TIMER_BIST_CONT_MODE);
}

void pe_bist_carrier_mode_2_exit(struct pd_port *pd_port)
{
	pd_notify_pe_bist_mode(pd_port, PD_BIST_MODE_DISABLE);
}

#if CONFIG_USB_PD_DISCARD_AND_UNEXPECT_MSG
void pe_unexpected_tx_wait_entry(struct pd_port *pd_port)
{
	struct tcpc_device __maybe_unused *tcpc = pd_port->tcpc;

	PE_INFO("##$$123\n");
	PE_STATE_DISCARD_AND_UNEXPECTED(pd_port);
	pd_enable_timer(pd_port, PD_TIMER_SENDER_RESPONSE);
}

void pe_send_soft_reset_tx_wait_entry(struct pd_port *pd_port)
{
	struct tcpc_device __maybe_unused *tcpc = pd_port->tcpc;

	PE_INFO("##$$124\n");
	PE_STATE_DISCARD_AND_UNEXPECTED(pd_port);
	pd_enable_timer(pd_port, PD_TIMER_SENDER_RESPONSE);
}

void pe_recv_soft_reset_tx_wait_entry(struct pd_port *pd_port)
{
	struct tcpc_device __maybe_unused *tcpc = pd_port->tcpc;

	PE_INFO("##$$125\n");
	PE_STATE_DISCARD_AND_UNEXPECTED(pd_port);
	pd_enable_timer(pd_port, PD_TIMER_SENDER_RESPONSE);
}

void pe_send_soft_reset_standby_entry(struct pd_port *pd_port)
{
	struct tcpc_device __maybe_unused *tcpc = pd_port->tcpc;

	PE_INFO("##$$126\n");
	PE_STATE_DISCARD_AND_UNEXPECTED(pd_port);
	pd_put_dpm_ack_event(pd_port);
}
#endif	/* CONFIG_USB_PD_DISCARD_AND_UNEXPECT_MSG */

/*
 * Policy Engine Share State Activity
 */

void pe_power_ready_entry(struct pd_port *pd_port)
{
	struct pe_data *pe_data = &pd_port->pe_data;

	pd_port->state_machine = PE_STATE_MACHINE_NORMAL;

	pe_data->explicit_contract = true;
	pe_data->during_swap = false;

#if CONFIG_USB_PD_RENEGOTIATION_COUNTER
	pe_data->renegotiation_count = 0;
#endif	/* CONFIG_USB_PD_RENEGOTIATION_COUNTER */

#if CONFIG_USB_PD_DISCARD_AND_UNEXPECT_MSG
	pe_data->pd_sent_ams_init_cmd = true;
#endif /* CONFIG_USB_PD_DISCARD_AND_UNEXPECT_MSG */

#if CONFIG_USB_PD_REV30
	if (pd_check_rev30(pd_port))
		dpm_reaction_clear(pd_port, DPM_REACTION_DFP_FLOW_DELAY |
					    DPM_REACTION_UFP_FLOW_DELAY);
#endif	/* CONFIG_USB_PD_REV30 */

	pd_set_rx_enable(pd_port, PD_RX_CAP_PE_READY);

	dpm_reaction_set(pd_port, DPM_REACTION_CAP_READY_ONCE);
	pd_notify_tcp_event_2nd_result(pd_port, TCP_DPM_RET_SUCCESS);
#if IS_ENABLED(CONFIG_USB_HOST_NOTIFY)
	send_otg_notify(get_otg_notify(), NOTIFY_EVENT_PD_CONTRACT, 1);
#endif /* CONFIG_USB_HOST_NOTIFY */
}

#if CONFIG_USB_PD_REV30

/*
 * [PD3.0] Figure 8-85 Get Battery Capabilities State Diagram
 */

#if CONFIG_USB_PD_REV30_BAT_CAP_REMOTE
void pe_get_battery_cap_entry(struct pd_port *pd_port)
{
	struct pd_get_battery_capabilities *gbcdb =
		pd_get_tcp_event_data_object(pd_port);

	PE_STATE_WAIT_MSG(pd_port);
	pd_send_sop_ext_msg(pd_port,
		PD_EXT_GET_BAT_CAP, PD_GBCDB_SIZE, gbcdb);
}

void pe_get_battery_cap_exit(struct pd_port *pd_port)
{
	pd_dpm_inform_battery_cap(pd_port);
}
#endif	/* CONFIG_USB_PD_REV30_BAT_CAP_REMOTE */

/*
 * [PD3.0] Figure 8-86 Give Battery Capabilities State Diagram
 */

#if CONFIG_USB_PD_REV30_BAT_CAP_LOCAL
void pe_give_battery_cap_entry(struct pd_port *pd_port)
{
	PE_STATE_WAIT_TX_SUCCESS(pd_port);

	pd_dpm_send_battery_cap(pd_port);
}
#endif	/* CONFIG_USB_PD_REV30_BAT_CAP_LOCAL */

/*
 * [PD3.0] Figure 8-87 Get Battery Status State Diagram
 */

#if CONFIG_USB_PD_REV30_BAT_STATUS_REMOTE
void pe_get_battery_status_entry(struct pd_port *pd_port)
{
	struct pd_get_battery_status *gbsdb =
		pd_get_tcp_event_data_object(pd_port);

	PE_STATE_WAIT_MSG(pd_port);
	pd_send_sop_ext_msg(pd_port,
		PD_EXT_GET_BAT_STATUS, PD_GBSDB_SIZE, gbsdb);
}

void pe_get_battery_status_exit(struct pd_port *pd_port)
{
	pd_dpm_inform_battery_status(pd_port);
}
#endif	/* CONFIG_USB_PD_REV30_BAT_STATUS_REMOTE */

/*
 * [PD3.0] Figure 8-88 Give Battery Status State Diagram
 */

#if CONFIG_USB_PD_REV30_BAT_STATUS_LOCAL
void pe_give_battery_status_entry(struct pd_port *pd_port)
{
	PE_STATE_WAIT_TX_SUCCESS(pd_port);

	pd_dpm_send_battery_status(pd_port);
}
#endif	/* CONFIG_USB_PD_REV30_BAT_STATUS_LOCAL */

/*
 * [PD3.0] Figure 8-89 Get Manufacturer Information State Diagram
 */

#if CONFIG_USB_PD_REV30_MFRS_INFO_REMOTE

void pe_get_manufacturer_info_entry(struct pd_port *pd_port)
{
	struct pd_get_manufacturer_info *gmidb =
		pd_get_tcp_event_data_object(pd_port);

	PE_STATE_WAIT_MSG(pd_port);
	pd_send_sop_ext_msg(pd_port,
		PD_EXT_GET_MFR_INFO, PD_GMIDB_SIZE, gmidb);
}

void pe_get_manufacturer_info_exit(struct pd_port *pd_port)
{
	pd_dpm_inform_mfrs_info(pd_port);
}

#endif	/* CONFIG_USB_PD_REV30_MFRS_INFO_REMOTE */

/*
 * [PD3.0] Figure 8-90 Give Manufacturer Information State Diagram
 */

#if CONFIG_USB_PD_REV30_MFRS_INFO_LOCAL

void pe_give_manufacturer_info_entry(struct pd_port *pd_port)
{
	PE_STATE_WAIT_TX_SUCCESS(pd_port);

	pd_dpm_send_mfrs_info(pd_port);
}

#endif	/* CONFIG_USB_PD_REV30_MFRS_INFO_LOCAL */

/*
 * [PD3.0] Figure 8-91 Get Country Codes State Diagram
 */

#if CONFIG_USB_PD_REV30_COUNTRY_CODE_REMOTE
void pe_get_country_codes_entry(struct pd_port *pd_port)
{
	PE_STATE_WAIT_MSG_OR_TX_FAILED(pd_port);

	pd_send_sop_ctrl_msg(pd_port, PD_CTRL_GET_COUNTRY_CODE);
}

void pe_get_country_codes_exit(struct pd_port *pd_port)
{
	pd_dpm_inform_country_codes(pd_port);
}
#endif	/* CONFIG_USB_PD_REV30_COUNTRY_CODE_REMOTE */

/*
 * [PD3.0] Figure 8-92 Give Country Codes State Diagram
 */

#if CONFIG_USB_PD_REV30_COUNTRY_CODE_LOCAL
void pe_give_country_codes_entry(struct pd_port *pd_port)
{
	PE_STATE_WAIT_TX_SUCCESS_OR_FAILED(pd_port);

	pd_dpm_send_country_codes(pd_port);
}
#endif	/* CONFIG_USB_PD_REV30_COUNTRY_CODE_LOCAL */

/*
 * [PD3.0] Figure 8-93 Get Country Information State Diagram
 */

#if CONFIG_USB_PD_REV30_COUNTRY_INFO_REMOTE
void pe_get_country_info_entry(struct pd_port *pd_port)
{
	uint32_t *ccdo =
		pd_get_tcp_event_data_object(pd_port);

	PE_STATE_WAIT_MSG_OR_TX_FAILED(pd_port);

	pd_send_sop_data_msg(pd_port,
		PD_DATA_GET_COUNTRY_INFO, PD_CCDO_SIZE, ccdo);
}

void pe_get_country_info_exit(struct pd_port *pd_port)
{
	pd_dpm_inform_country_info(pd_port);
}
#endif	/* CONFIG_USB_PD_REV30_COUNTRY_INFO_REMOTE */

/*
 * [PD3.0] Figure 8-94 Give Country Information State Diagram
 */

#if CONFIG_USB_PD_REV30_COUNTRY_INFO_LOCAL
void pe_give_country_info_entry(struct pd_port *pd_port)
{
	PE_STATE_WAIT_TX_SUCCESS_OR_FAILED(pd_port);

	pd_dpm_send_country_info(pd_port);
}
#endif	/* CONFIG_USB_PD_REV30_COUNTRY_INFO_LOCAL */

/*
 * [PD3.0] Unsupported, Unrecognized UVDM and Unsupported SVDM.
 */

void pe_vdm_not_supported_entry(struct pd_port *pd_port)
{
	struct pd_event *pd_event = pd_get_curr_pd_event(pd_port);
	bool cable = pd_event->msg == PD_DPM_CABLE_NOT_SUPPORT;

	PE_STATE_WAIT_TX_SUCCESS(pd_port);

	if (cable)
		pd_set_rx_enable(pd_port, PD_RX_CAP_PE_READY_CABLE);

	(cable ? pd_send_sop_prime_ctrl_msg :
		 pd_send_sop_ctrl_msg)(pd_port, PD_CTRL_NOT_SUPPORTED);
}

void pe_get_revision_entry(struct pd_port *pd_port)
{
	PE_STATE_WAIT_MSG_OR_TX_FAILED(pd_port);

	pd_send_sop_ctrl_msg(pd_port, PD_CTRL_GET_REVISION);
}

void pe_get_revision_exit(struct pd_port *pd_port)
{
	pd_dpm_inform_revision(pd_port);
}

void pe_give_revision_entry(struct pd_port *pd_port)
{
	PE_STATE_WAIT_TX_SUCCESS_OR_FAILED(pd_port);

	pd_dpm_send_revision(pd_port);
}
#endif	/* CONFIG_USB_PD_REV30 */
#endif /* CONFIG_USB_POWER_DELIVERY */
