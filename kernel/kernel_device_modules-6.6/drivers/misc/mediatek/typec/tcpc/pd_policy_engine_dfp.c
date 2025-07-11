// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 MediaTek Inc.
 */

#include "inc/pd_core.h"
#include "inc/pd_dpm_core.h"
#include "inc/tcpci.h"
#include "inc/pd_policy_engine.h"

/*
 * [PD2.0] Figure 8-64 DFP to UFP VDM Discover Identity State Diagram
 */

void pe_dfp_ufp_vdm_identity_request_entry(struct pd_port *pd_port)
{
	pd_send_vdm_discover_id(pd_port, TCPC_TX_SOP);
}

void pe_dfp_ufp_vdm_identity_acked_entry(struct pd_port *pd_port)
{
	pd_dpm_dfp_inform_id(pd_port, true);
}

void pe_dfp_ufp_vdm_identity_naked_entry(struct pd_port *pd_port)
{
	pd_dpm_dfp_inform_id(pd_port, false);
}

/*
 * [PD2.0] Figure 8-65 DFP VDM Discover Identity State Diagram
 */

void pe_dfp_cbl_vdm_identity_request_entry(struct pd_port *pd_port)
{
	pd_set_rx_enable(pd_port, PD_RX_CAP_PE_READY_CABLE);

	pd_port->pe_data.discover_cable_id_counter++;
	pd_send_vdm_discover_id(pd_port, TCPC_TX_SOP_PRIME);
}

void pe_dfp_cbl_vdm_identity_acked_entry(struct pd_port *pd_port)
{
	pd_dpm_inform_cable_id(pd_port, true, false);
}

void pe_dfp_cbl_vdm_identity_naked_entry(struct pd_port *pd_port)
{
	pd_dpm_inform_cable_id(pd_port, false, false);
}

void pe_dfp_cbl_vdm_svids_request_entry(struct pd_port *pd_port)
{
	pd_set_rx_enable(pd_port, PD_RX_CAP_PE_READY_CABLE);

	pd_send_vdm_discover_svids(pd_port, TCPC_TX_SOP_PRIME);
}

void pe_dfp_cbl_vdm_svids_acked_entry(struct pd_port *pd_port)
{
	pd_dpm_inform_cable_svids(pd_port, true);
}

void pe_dfp_cbl_vdm_svids_naked_entry(struct pd_port *pd_port)
{
	pd_dpm_inform_cable_svids(pd_port, false);
}

void pe_dfp_cbl_vdm_modes_request_entry(struct pd_port *pd_port)
{
	pd_set_rx_enable(pd_port, PD_RX_CAP_PE_READY_CABLE);

	pd_send_vdm_discover_modes(pd_port, TCPC_TX_SOP_PRIME,
				   pd_port->cable_svid_to_discover);
}

void pe_dfp_cbl_vdm_modes_acked_entry(struct pd_port *pd_port)
{
	pd_dpm_inform_cable_modes(pd_port, true);
}

void pe_dfp_cbl_vdm_modes_naked_entry(struct pd_port *pd_port)
{
	pd_dpm_inform_cable_modes(pd_port, false);
}

/*
 * [PD2.0] Figure 8-66 DFP VDM Discover SVIDs State Diagram
 */

void pe_dfp_vdm_svids_request_entry(struct pd_port *pd_port)
{
	pd_send_vdm_discover_svids(pd_port, TCPC_TX_SOP);
}

void pe_dfp_vdm_svids_acked_entry(struct pd_port *pd_port)
{
	pd_dpm_dfp_inform_svids(pd_port, true);
}

void pe_dfp_vdm_svids_naked_entry(struct pd_port *pd_port)
{
	pd_dpm_dfp_inform_svids(pd_port, false);
}

/*
 * [PD2.0] Figure 8-67 DFP VDM Discover Modes State Diagram
 */

void pe_dfp_vdm_modes_request_entry(struct pd_port *pd_port)
{
	pd_send_vdm_discover_modes(pd_port, TCPC_TX_SOP, pd_port->mode_svid);
}

void pe_dfp_vdm_modes_acked_entry(struct pd_port *pd_port)
{
	pd_dpm_dfp_inform_modes(pd_port, true);
}

void pe_dfp_vdm_modes_naked_entry(struct pd_port *pd_port)
{
	pd_dpm_dfp_inform_modes(pd_port, false);
}

/*
 * [PD2.0] Figure 8-68 DFP VDM Mode Entry State Diagram
 */

void pe_dfp_vdm_mode_entry_request_entry(struct pd_port *pd_port)
{
	pd_send_vdm_enter_mode(pd_port, TCPC_TX_SOP,
		pd_port->mode_svid, pd_port->mode_obj_pos);
}

void pe_dfp_vdm_mode_entry_acked_entry(struct pd_port *pd_port)
{
	pd_dpm_dfp_inform_enter_mode(pd_port, true);
}

void pe_dfp_vdm_mode_entry_naked_entry(struct pd_port *pd_port)
{
	pd_dpm_dfp_inform_enter_mode(pd_port, false);
}

/*
 * [PD2.0] Figure 8-69 DFP VDM Mode Exit State Diagram
 */

void pe_dfp_vdm_mode_exit_request_entry(struct pd_port *pd_port)
{
	pd_send_vdm_exit_mode(pd_port, TCPC_TX_SOP,
		pd_port->mode_svid, pd_port->mode_obj_pos);
}

void pe_dfp_vdm_mode_exit_acked_entry(struct pd_port *pd_port)
{
	pd_dpm_dfp_inform_exit_mode(pd_port);
}

/*
 * [PD2.0] Figure 8-70 DFP VDM Attention State Diagram
 */

void pe_dfp_vdm_attention_request_entry(struct pd_port *pd_port)
{
	pd_dpm_dfp_inform_attention(pd_port);
}

/*
 * [PD2.0] Figure 8-83 DFP Cable Soft Reset or Cable Reset State Diagram
 */

void pe_dfp_cbl_send_soft_reset_entry(struct pd_port *pd_port)
{
	PE_STATE_WAIT_MSG_OR_TX_FAILED(pd_port);

	pd_set_rx_enable(pd_port, PD_RX_CAP_PE_READY_CABLE);

	pd_send_cable_soft_reset(pd_port);
}

void pe_dfp_cbl_send_cable_reset_entry(struct pd_port *pd_port)
{
	/* TODO : we don't do cable reset now */
}

/*
 * [PD2.0] Display Port
 */

void pe_dfp_vdm_dp_status_update_request_entry(struct pd_port *pd_port)
{
	pd_dpm_dfp_send_dp_status_update(pd_port);
}

void pe_dfp_vdm_dp_status_update_acked_entry(struct pd_port *pd_port)
{
	pd_dpm_dfp_inform_dp_status_update(pd_port, true);
}

void pe_dfp_vdm_dp_status_update_naked_entry(struct pd_port *pd_port)
{
	pd_dpm_dfp_inform_dp_status_update(pd_port, false);
}

void pe_dfp_vdm_dp_configuration_request_entry(struct pd_port *pd_port)
{
	pd_dpm_dfp_send_dp_configuration(pd_port);
}

void pe_dfp_vdm_dp_configuration_acked_entry(struct pd_port *pd_port)
{
	pd_dpm_dfp_inform_dp_configuration(pd_port, true);
}

void pe_dfp_vdm_dp_configuration_naked_entry(struct pd_port *pd_port)
{
	pd_dpm_dfp_inform_dp_configuration(pd_port, false);
}

/*
 * Custom VDM
 */

void pe_dfp_cvdm_send_entry(struct pd_port *pd_port)
{
	if (pd_port->cvdm_cable)
		pd_set_rx_enable(pd_port, PD_RX_CAP_PE_READY_CABLE);

	pd_dpm_dfp_send_cvdm(pd_port);
}

void pe_dfp_cvdm_acked_entry(struct pd_port *pd_port)
{
	pd_dpm_dfp_inform_cvdm(pd_port, true);
}

void pe_dfp_cvdm_naked_entry(struct pd_port *pd_port)
{
	pd_dpm_dfp_inform_cvdm(pd_port, false);
}
