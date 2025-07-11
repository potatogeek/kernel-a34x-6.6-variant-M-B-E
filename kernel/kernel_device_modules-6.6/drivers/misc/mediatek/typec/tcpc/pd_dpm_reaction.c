// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 MediaTek Inc.
 */

#include "inc/tcpci.h"
#include "inc/pd_policy_engine.h"
#include "inc/pd_dpm_core.h"
#include "inc/pd_dpm_pdo_select.h"
#include "pd_dpm_prv.h"

#define DPM_REACTION_COND_ALWAYS		(1<<0)
#define DPM_REACTION_COND_UFP_ONLY	(1<<1)
#define DPM_REACTION_COND_DFP_ONLY	(1<<2)
#define DPM_REACTION_COND_PD30		(1<<3)

#define DPM_REACCOND_DFP	\
	(DPM_REACTION_COND_ALWAYS | DPM_REACTION_COND_DFP_ONLY)

#define DPM_REACCOND_UFP	\
	(DPM_REACTION_COND_ALWAYS | DPM_REACTION_COND_UFP_ONLY)

#define DPM_REACTION_COND_CHECK_ONCE			(1<<5)
#define DPM_REACTION_COND_ONE_SHOT			(1<<6)
#define DPM_REACTION_COND_LIMITED_RETRIES		(1<<7)

/*
 * DPM flow delay reactions
 */

#if CONFIG_USB_PD_UFP_FLOW_DELAY
static uint8_t dpm_reaction_ufp_flow_delay(struct pd_port *pd_port)
{
	struct tcpc_device __maybe_unused *tcpc = pd_port->tcpc;

	DPM_INFO("UFP Delay\n");
	pd_restart_timer(pd_port, PD_TIMER_UFP_FLOW_DELAY);
	return DPM_READY_REACTION_BUSY;
}
#endif	/* CONFIG_USB_PD_UFP_FLOW_DELAY */

#if CONFIG_USB_PD_DFP_FLOW_DELAY
static uint8_t dpm_reaction_dfp_flow_delay(struct pd_port *pd_port)
{
	struct tcpc_device __maybe_unused *tcpc = pd_port->tcpc;

	DPM_INFO("DFP Delay\n");
	pd_restart_timer(pd_port, PD_TIMER_DFP_FLOW_DELAY);
	return DPM_READY_REACTION_BUSY;
}
#endif	/* CONFIG_USB_PD_DFP_FLOW_DELAY */

#if CONFIG_USB_PD_VCONN_STABLE_DELAY
static uint8_t dpm_reaction_vconn_stable_delay(struct pd_port *pd_port)
{
	struct tcpc_device __maybe_unused *tcpc = pd_port->tcpc;

	if (pd_port->vconn_role == PD_ROLE_VCONN_DYNAMIC_ON) {
		DPM_INFO("VStable Delay\n");
		return DPM_READY_REACTION_BUSY;
	}

	return 0;
}
#endif	/* CONFIG_USB_PD_VCONN_STABLE_DELAY */

/*
 * DPM get cap reaction
 */

#if CONFIG_USB_PD_REV30
static uint8_t dpm_reaction_get_source_cap_ext(struct pd_port *pd_port)
{
	return TCP_DPM_EVT_GET_SOURCE_CAP_EXT;
}
#endif	/* CONFIG_USB_PD_REV30 */

static uint8_t dpm_reaction_get_sink_cap(struct pd_port *pd_port)
{
	return TCP_DPM_EVT_GET_SINK_CAP;
}

static uint8_t dpm_reaction_get_source_cap(struct pd_port *pd_port)
{
	return TCP_DPM_EVT_GET_SOURCE_CAP;
}

static uint8_t dpm_reaction_attempt_get_flag(struct pd_port *pd_port)
{
	return TCP_DPM_EVT_GET_SINK_CAP;
}

/*
 * DPM swap reaction
 */

#if CONFIG_USB_PD_PR_SWAP
static uint8_t dpm_reaction_request_pr_swap(struct pd_port *pd_port)
{
	uint32_t prefer_role =
		DPM_CAP_EXTRACT_PR_CHECK(pd_port->dpm_caps);

	if (!(pd_port->dpm_caps & DPM_CAP_LOCAL_DR_POWER))
		return 0;

	if (pd_port->power_role == PD_ROLE_SINK) {
		if (prefer_role == DPM_CAP_PR_CHECK_PREFER_SRC)
			return TCP_DPM_EVT_PR_SWAP_AS_SRC;
	} else {
#if CONFIG_USB_PD_SRC_TRY_PR_SWAP_IF_BAD_PW
		if (dpm_check_good_power(pd_port) == GOOD_PW_PARTNER)
			return TCP_DPM_EVT_PR_SWAP_AS_SNK;
#endif	/* CONFIG_USB_PD_SRC_TRY_PR_SWAP_IF_BAD_PW */

		if (prefer_role == DPM_CAP_PR_CHECK_PREFER_SNK)
			return TCP_DPM_EVT_PR_SWAP_AS_SNK;
	}

	return 0;
}
#endif	/* CONFIG_USB_PD_PR_SWAP */

#if CONFIG_USB_PD_DR_SWAP
static uint8_t dpm_reaction_request_dr_swap(struct pd_port *pd_port)
{
	uint32_t prefer_role =
		DPM_CAP_EXTRACT_DR_CHECK(pd_port->dpm_caps);

	if (!(pd_port->dpm_caps & DPM_CAP_LOCAL_DR_DATA))
		return 0;

	if (pd_port->data_role == PD_ROLE_DFP
		&& prefer_role == DPM_CAP_DR_CHECK_PREFER_UFP)
		return TCP_DPM_EVT_DR_SWAP_AS_UFP;

	if (pd_port->data_role == PD_ROLE_UFP
		&& prefer_role == DPM_CAP_DR_CHECK_PREFER_DFP)
		return TCP_DPM_EVT_DR_SWAP_AS_DFP;

	if (pd_port->data_role == PD_ROLE_UFP &&
	    dpm_reaction_check(pd_port, DPM_REACTION_DISCOVER_CABLE) &&
	    !pd_check_rev30(pd_port))
		return TCP_DPM_EVT_DR_SWAP_AS_DFP;

	return 0;
}
#endif	/* CONFIG_USB_PD_DR_SWAP */


/*
 * DPM DiscoverCable reaction
 */

static uint8_t dpm_reaction_dynamic_vconn(struct pd_port *pd_port)
{
	pd_dpm_dynamic_enable_vconn(pd_port);
	return 0;
}

#if CONFIG_USB_PD_DISCOVER_CABLE_REQUEST_VCONN
static uint8_t dpm_reaction_request_vconn_source(struct pd_port *pd_port)
{
	bool return_vconn = true;
	struct tcpc_device __maybe_unused *tcpc = pd_port->tcpc;

	if (!dpm_reaction_check(pd_port, DPM_REACTION_DISCOVER_CABLE))
		return 0;

	if (tcpm_inquire_pd_vconn_role(tcpc))
		return 0;

	switch (tcpc->tcpc_vconn_supply) {
	case TCPC_VCONN_SUPPLY_NEVER:
		return 0;
	case TCPC_VCONN_SUPPLY_STARTUP:
		return_vconn = false;
		fallthrough;
	default:
		break;
	}

	if (return_vconn)
		dpm_reaction_set(pd_port, DPM_REACTION_RETURN_VCONN_SRC);

	return TCP_DPM_EVT_VCONN_SWAP_ON;
}
#endif	/* CONFIG_USB_PD_DISCOVER_CABLE_REQUEST_VCONN */

static uint8_t pd_dpm_reaction_discover_cable(struct pd_port *pd_port)
{
	struct pe_data *pe_data = &pd_port->pe_data;

	if (!pd_is_cable_communication_available(pd_port))
		return 0;

	if (pd_is_reset_cable(pd_port))
		return TCP_DPM_EVT_CABLE_SOFTRESET;

	switch (pe_data->cable_discovered_state) {
	case CABLE_DISCOVERED_NONE:
		if (!pd_is_discover_cable(pd_port))
			return 0;
		pd_restart_timer(pd_port, PD_TIMER_DISCOVER_ID);
		return DPM_READY_REACTION_BUSY;
	case CABLE_DISCOVERED_ID:
		return pd_port->dp_v21 ? TCP_DPM_EVT_DISCOVER_CABLE_SVIDS : 0;
	case CABLE_DISCOVERED_SVIDS:
		return pd_port->cable_svid_to_discover ?
		       TCP_DPM_EVT_DISCOVER_CABLE_MODES : 0;
	case CABLE_DISCOVERED_MODES:
	default:
		return 0;
	}
}

#if CONFIG_USB_PD_DISCOVER_CABLE_RETURN_VCONN
static uint8_t dpm_reaction_return_vconn_source(struct pd_port *pd_port)
{
	struct tcpc_device __maybe_unused *tcpc = pd_port->tcpc;

	if (tcpm_inquire_pd_vconn_role(tcpc)) {
		DPM_DBG("VconnReturn\n");
		return TCP_DPM_EVT_VCONN_SWAP_OFF;
	}

	return 0;
}
#endif	/* CONFIG_USB_PD_DISCOVER_CABLE_RETURN_VCONN */

/*
 * DPM EnterMode reaction
 */

#if CONFIG_USB_PD_ATTEMPT_DISCOVER_ID
static uint8_t dpm_reaction_discover_id(struct pd_port *pd_port)
{
	return TCP_DPM_EVT_DISCOVER_ID;
}
#endif	/* CONFIG_USB_PD_ATTEMPT_DISCOVER_ID */

#if CONFIG_USB_PD_ATTEMPT_DISCOVER_SVIDS
static uint8_t dpm_reaction_discover_svids(struct pd_port *pd_port)
{
	return TCP_DPM_EVT_DISCOVER_SVIDS;
}
#endif	/* CONFIG_USB_PD_ATTEMPT_DISCOVER_SVIDS */

static uint8_t dpm_reaction_mode_operation(struct pd_port *pd_port)
{
	if (svdm_notify_pe_ready(pd_port))
		return DPM_READY_REACTION_BUSY;

	return 0;
}

/*
 * DPM Local/Remote Alert reaction
 */

#if CONFIG_USB_PD_REV30

#if CONFIG_USB_PD_DPM_AUTO_SEND_ALERT

static uint8_t dpm_reaction_send_alert(struct pd_port *pd_port)
{
	struct pe_data *pe_data = &pd_port->pe_data;

	if (pe_data->local_alert == 0)
		return 0;

	return TCP_DPM_EVT_ALERT;
}

#endif	/* CONFIG_USB_PD_DPM_AUTO_SEND_ALERT */

#if CONFIG_USB_PD_DPM_AUTO_GET_STATUS

const uint32_t c_get_status_alert_type = ADO_ALERT_OCP|
	ADO_ALERT_OTP|ADO_ALERT_OVP|ADO_ALERT_OPER_CHANGED|
	ADO_ALERT_SRC_IN_CHANGED;

static inline uint8_t dpm_reaction_alert_status_changed(struct pd_port *pd_port)
{
	pd_port->pe_data.remote_alert &=
		~ADO_ALERT_TYPE_SET(c_get_status_alert_type);

	return TCP_DPM_EVT_GET_STATUS;
}

static inline uint8_t dpm_reaction_alert_battry_changed(struct pd_port *pd_port)
{
	int i = 0;
	uint32_t bat_change_i = 255;
	uint32_t *remote_alert = &pd_port->pe_data.remote_alert;
	uint8_t bat_change_mask_f = ADO_FIXED_BAT(*remote_alert);
	uint8_t bat_change_mask_hs = ADO_HOT_SWAP_BAT(*remote_alert);
	uint8_t mask = 0;

	if (bat_change_mask_f) {
		for (i = 0; i < 4; i++) {
			mask = 1 << i;
			if (bat_change_mask_f & mask) {
				bat_change_i = i;
				bat_change_mask_f &= ~mask;
				*remote_alert &= ~ADO_FIXED_BAT_SET(mask);
				break;
			}
		}
	} else if (bat_change_mask_hs) {
		for (i = 0; i < 4; i++) {
			mask = 1 << i;
			if (bat_change_mask_hs & mask) {
				bat_change_i = i + 4;
				bat_change_mask_hs &= ~mask;
				*remote_alert &= ~ADO_HOT_SWAP_BAT_SET(mask);
				break;
			}
		}
	}

	if (bat_change_mask_f == 0 && bat_change_mask_hs == 0)
		*remote_alert &= ~ADO_ALERT_TYPE_SET(ADO_ALERT_BAT_CHANGED);

	if (bat_change_i > 7)
		return 0;

	pd_port->tcp_event.tcp_dpm_data.data_object[0] =
		cpu_to_le32(bat_change_i);
	return TCP_DPM_EVT_GET_BAT_STATUS;
}

static uint8_t dpm_reaction_handle_alert(struct pd_port *pd_port)
{
	uint32_t alert_type = ADO_ALERT_TYPE(pd_port->pe_data.remote_alert);

	if (alert_type & c_get_status_alert_type)
		return dpm_reaction_alert_status_changed(pd_port);

	if (alert_type & ADO_ALERT_BAT_CHANGED)
		return dpm_reaction_alert_battry_changed(pd_port);

	return 0;
}
#endif	/* CONFIG_USB_PD_DPM_AUTO_GET_STATUS */

#endif	/* CONFIG_USB_PD_REV30 */

/*
 * DPM Idle reaction
 */

static inline uint8_t dpm_get_pd_connect_state(struct pd_port *pd_port)
{
	if (pd_port->power_role == PD_ROLE_SOURCE) {
		if (pd_check_rev30(pd_port))
			return PD_CONNECT_PE_READY_SRC_PD30;

		return PD_CONNECT_PE_READY_SRC;
	}

	if (pd_check_rev30(pd_port)) {
		if (pd_is_source_support_apdo(pd_port))
			return PD_CONNECT_PE_READY_SNK_APDO;

		return PD_CONNECT_PE_READY_SNK_PD30;
	}

	return PD_CONNECT_PE_READY_SNK;
}

static inline void dpm_check_vconn_highv_prot(struct pd_port *pd_port)
{
#if CONFIG_USB_PD_VCONN_SAFE5V_ONLY
	struct tcpc_device *tcpc = pd_port->tcpc;
	struct pe_data *pe_data = &pd_port->pe_data;
	bool vconn_highv_prot = pd_port->request_v_new > 5000;

	if (pe_data->vconn_highv_prot && !vconn_highv_prot &&
		tcpc->tcpc_flags & TCPC_FLAGS_VCONN_SAFE5V_ONLY) {
		DPM_INFO("VC_HIGHV_PROT: %d\n", vconn_highv_prot);
		pe_data->vconn_highv_prot = vconn_highv_prot;
		pd_set_vconn(pd_port, pe_data->vconn_highv_prot_role);
	}
#endif	/* CONFIG_USB_PD_VCONN_SAFE5V_ONLY */
}

static uint8_t dpm_reaction_update_pe_ready(struct pd_port *pd_port)
{
	uint8_t state;
	struct tcpc_device __maybe_unused *tcpc = pd_port->tcpc;

	if (!pd_port->pe_data.pe_ready) {
		DPM_INFO("PE_READY\n");
		pd_port->pe_data.pe_ready = true;
	}

	state = dpm_get_pd_connect_state(pd_port);
	pd_update_connect_state(pd_port, state);

	dpm_check_vconn_highv_prot(pd_port);
	pd_dpm_dynamic_disable_vconn(pd_port);

#if CONFIG_USB_PD_REV30
	if (tcpc->tcp_event_count)
		return 0;
	pd_port->pe_data.pd_traffic_idle = true;
	if (pd_check_rev30(pd_port) &&
		(pd_port->power_role == PD_ROLE_SOURCE))
		pd_set_sink_tx(pd_port, PD30_SINK_TX_OK);
#endif	/* CONFIG_USB_PD_REV30 */

	return 0;
}

/*
 * DPM reaction declaration
 */

typedef uint8_t (*dpm_reaction_fun)(struct pd_port *pd_port);

struct dpm_ready_reaction {
	uint32_t bit_mask;
	uint8_t condition;
	dpm_reaction_fun	handler;
};

#define DECL_DPM_REACTION(xmask, xcond, xhandler)	{ \
	.bit_mask = xmask,	\
	.condition = xcond,	\
	.handler = xhandler, \
}

#define DECL_DPM_REACTION_ALWAYS(xhandler)	\
	DECL_DPM_REACTION(DPM_REACTION_CAP_ALWAYS,	\
		DPM_REACTION_COND_ALWAYS,	\
		xhandler)

#define DECL_DPM_REACTION_CHECK_ONCE(xmask, xhandler)	\
	DECL_DPM_REACTION(xmask,	\
		DPM_REACTION_COND_ALWAYS |	\
		DPM_REACTION_COND_CHECK_ONCE,	\
		xhandler)

#define DECL_DPM_REACTION_CHECK_ONCE_LIMITED_RETRIES(xmask, xhandler)	\
	DECL_DPM_REACTION(xmask,	\
		DPM_REACTION_COND_ALWAYS |	\
		DPM_REACTION_COND_CHECK_ONCE |	\
		DPM_REACTION_COND_LIMITED_RETRIES,	\
		xhandler)

#define DECL_DPM_REACTION_RUN_ONCE(xmask, xhandler)	\
	DECL_DPM_REACTION(xmask,	\
		DPM_REACTION_COND_ALWAYS |	\
		DPM_REACTION_COND_CHECK_ONCE |	\
		DPM_REACTION_COND_ONE_SHOT,	\
		xhandler)

#define DECL_DPM_REACTION_LIMITED_RETRIES(xmask, xhandler)	\
	DECL_DPM_REACTION(xmask,	\
		DPM_REACTION_COND_ALWAYS |\
		DPM_REACTION_COND_LIMITED_RETRIES,	\
		xhandler)

#define DECL_DPM_REACTION_UFP(xmask, xhandler) \
	DECL_DPM_REACTION(xmask, \
		DPM_REACTION_COND_UFP_ONLY,	\
		xhandler)

#define DECL_DPM_REACTION_DFP(xmask, xhandler) \
	DECL_DPM_REACTION(xmask, \
		DPM_REACTION_COND_DFP_ONLY,	\
		xhandler)

#define DECL_DPM_REACTION_PD30(xmask, xhandler) \
	DECL_DPM_REACTION(xmask, \
		DPM_REACTION_COND_PD30,	 \
		xhandler)

#define DECL_DPM_REACTION_PD30_ONE_SHOT(xmask, xhandler) \
	DECL_DPM_REACTION(xmask, \
		DPM_REACTION_COND_PD30 | \
		DPM_REACTION_COND_ONE_SHOT, \
		xhandler)

#define DECL_DPM_REACTION_DFP_PD30_LIMITED_RETRIES(xmask, xhandler) \
	DECL_DPM_REACTION(xmask, \
		DPM_REACTION_COND_DFP_ONLY |\
		DPM_REACTION_COND_PD30 | \
		DPM_REACTION_COND_LIMITED_RETRIES, \
		xhandler)

static const struct dpm_ready_reaction dpm_reactions[] = {

#if CONFIG_USB_PD_REV30
#if CONFIG_USB_PD_DPM_AUTO_SEND_ALERT
	DECL_DPM_REACTION_PD30(
		DPM_REACTION_CAP_ALWAYS,
		dpm_reaction_send_alert),
#endif	/* CONFIG_USB_PD_DPM_AUTO_SEND_ALERT */
#if CONFIG_USB_PD_DPM_AUTO_GET_STATUS
	DECL_DPM_REACTION_PD30(
		DPM_REACTION_CAP_ALWAYS,
		dpm_reaction_handle_alert),
#endif	/* CONFIG_USB_PD_DPM_AUTO_GET_STATUS */
#endif	/* CONFIG_USB_PD_REV30 */

#if CONFIG_USB_PD_DFP_FLOW_DELAY
	DECL_DPM_REACTION_DFP(
		DPM_REACTION_DFP_FLOW_DELAY,
		dpm_reaction_dfp_flow_delay),
#endif	/* CONFIG_USB_PD_DFP_FLOW_DELAY */

#if CONFIG_USB_PD_UFP_FLOW_DELAY
	DECL_DPM_REACTION_UFP(
		DPM_REACTION_UFP_FLOW_DELAY,
		dpm_reaction_ufp_flow_delay),
#endif	/* CONFIG_USB_PD_UFP_FLOW_DELAY */

	DECL_DPM_REACTION_LIMITED_RETRIES(
		DPM_REACTION_GET_SINK_CAP,
		dpm_reaction_get_sink_cap),

	DECL_DPM_REACTION_LIMITED_RETRIES(
		DPM_REACTION_GET_SOURCE_CAP,
		dpm_reaction_get_source_cap),

	DECL_DPM_REACTION_LIMITED_RETRIES(
		DPM_REACTION_ATTEMPT_GET_FLAG,
		dpm_reaction_attempt_get_flag),

#if CONFIG_USB_PD_REV30
	DECL_DPM_REACTION_PD30_ONE_SHOT(
		DPM_REACTION_GET_SOURCE_CAP_EXT,
		dpm_reaction_get_source_cap_ext),
#endif	/* CONFIG_USB_PD_REV30 */

#if CONFIG_USB_PD_PR_SWAP
	DECL_DPM_REACTION_CHECK_ONCE(
		DPM_REACTION_REQUEST_PR_SWAP,
		dpm_reaction_request_pr_swap),
#endif	/* CONFIG_USB_PD_PR_SWAP */

#if CONFIG_USB_PD_DR_SWAP
	DECL_DPM_REACTION_CHECK_ONCE(
		DPM_REACTION_REQUEST_DR_SWAP,
		dpm_reaction_request_dr_swap),
#endif	/* CONFIG_USB_PD_DR_SWAP */

	DECL_DPM_REACTION_CHECK_ONCE(
		DPM_REACTION_DYNAMIC_VCONN,
		dpm_reaction_dynamic_vconn),

#if CONFIG_USB_PD_DISCOVER_CABLE_REQUEST_VCONN
	DECL_DPM_REACTION_RUN_ONCE(
		DPM_REACTION_REQUEST_VCONN_SRC,
		dpm_reaction_request_vconn_source),
#endif	/* CONFIG_USB_PD_DISCOVER_CABLE_REQUEST_VCONN */

#if CONFIG_USB_PD_VCONN_STABLE_DELAY
	DECL_DPM_REACTION_CHECK_ONCE(
		DPM_REACTION_VCONN_STABLE_DELAY,
		dpm_reaction_vconn_stable_delay),
#endif	/* CONFIG_USB_PD_VCONN_STABLE_DELAY */

	DECL_DPM_REACTION_CHECK_ONCE_LIMITED_RETRIES(
		DPM_REACTION_DISCOVER_CABLE,
		pd_dpm_reaction_discover_cable),

#if CONFIG_USB_PD_DISCOVER_CABLE_RETURN_VCONN
	DECL_DPM_REACTION_RUN_ONCE(
		DPM_REACTION_RETURN_VCONN_SRC,
		dpm_reaction_return_vconn_source),
#endif	/* CONFIG_USB_PD_DISCOVER_CABLE_RETURN_VCONN */

#if CONFIG_USB_PD_ATTEMPT_DISCOVER_ID
	DECL_DPM_REACTION_DFP_PD30_LIMITED_RETRIES(
		DPM_REACTION_DISCOVER_ID,
		dpm_reaction_discover_id),
#endif	/* CONFIG_USB_PD_ATTEMPT_DISCOVER_ID */

#if CONFIG_USB_PD_ATTEMPT_DISCOVER_SVIDS
	DECL_DPM_REACTION_DFP_PD30_LIMITED_RETRIES(
		DPM_REACTION_DISCOVER_SVIDS,
		dpm_reaction_discover_svids),
#endif	/* CONFIG_USB_PD_ATTEMPT_DISCOVER_SVIDS */

	DECL_DPM_REACTION_ALWAYS(
		dpm_reaction_mode_operation),

	DECL_DPM_REACTION_ALWAYS(
		dpm_reaction_update_pe_ready),
};

/**
 * dpm_get_reaction_env
 *
 * Get current reaction's environmental conditions.
 *
 * Returns environmental conditions.
 */

static inline uint8_t dpm_get_reaction_env(struct pd_port *pd_port)
{
	uint8_t conditions;

	if (pd_port->data_role == PD_ROLE_DFP)
		conditions = DPM_REACCOND_DFP;
	else
		conditions = DPM_REACCOND_UFP;

	if (pd_check_rev30(pd_port))
		conditions |= DPM_REACTION_COND_PD30;

	return conditions;
}

/**
 * dpm_check_reaction_busy
 *
 * check this reaction is still keep busy
 *
 * @ reaction : which reaction is checked.
 *
 * Return Boolean to indicate busy or not.
 */

static inline bool dpm_check_reaction_busy(struct pd_port *pd_port,
		const struct dpm_ready_reaction *reaction)
{
	if (reaction->bit_mask & DPM_REACTION_CAP_ALWAYS)
		return false;

	return !dpm_reaction_check(
			pd_port, DPM_REACTION_CAP_READY_ONCE);
}

/**
 * dpm_check_reaction_available
 *
 * check this reaction is available.
 *
 * @ env : Environmental conditions of reaction table
 * @ reaction : which reaction is checked.
 *
 * Returns TCP_DPM_EVT_ID if available;
 * Returns Zero to indicate if not available.
 * Returns DPM_READY_REACTION_BUSY to indicate
 *	this reaction is waiting for being finished.
 */

static inline uint8_t dpm_check_reaction_available(struct pd_port *pd_port,
	uint8_t env, const struct dpm_ready_reaction *reaction)
{
	int ret;

	if (!dpm_reaction_check(pd_port, reaction->bit_mask))
		return 0;

	if (!(reaction->condition & env))
		return 0;

	if (dpm_check_reaction_busy(pd_port, reaction))
		return DPM_READY_REACTION_BUSY;

	ret = reaction->handler(pd_port);

	if (ret == 0 &&
		reaction->condition & DPM_REACTION_COND_CHECK_ONCE)
		dpm_reaction_clear(pd_port, reaction->bit_mask);

	return ret;
}

/**
 * dpm_check_reset_reaction
 *
 * Once trigger one reaction,
 * check if automatically clear this reaction flag.
 *
 * For the following reactions type:
 *	DPM_REACTION_COND_ONE_SHOT,
 *	DPM_REACTION_COND_LIMITED_RETRIES
 *
 * @ reaction : which reaction is triggered.
 *
 * Return Boolean to indicate automatically clear or not.
 */

static inline bool dpm_check_clear_reaction(struct pd_port *pd_port,
	const struct dpm_ready_reaction *reaction)
{
	struct pe_data *pe_data = &pd_port->pe_data;

	if (pe_data->dpm_reaction_id != reaction->bit_mask) {
		pe_data->dpm_reaction_id = reaction->bit_mask;
		pe_data->dpm_reaction_try = 1;
	} else
		pe_data->dpm_reaction_try++;

	if (reaction->condition & DPM_REACTION_COND_ONE_SHOT)
		return true;

	if (reaction->condition & DPM_REACTION_COND_LIMITED_RETRIES)
		return pe_data->dpm_reaction_try >= 6;

	return false;
}

uint8_t pd_dpm_get_ready_reaction(struct pd_port *pd_port)
{
	uint8_t evt;
	uint8_t env;
	uint32_t clear_reaction = DPM_REACTION_CAP_READY_ONCE;
	const struct dpm_ready_reaction *reaction = dpm_reactions;
	const struct dpm_ready_reaction *reaction_last =
			dpm_reactions + ARRAY_SIZE(dpm_reactions);
	struct tcpc_device __maybe_unused *tcpc = pd_port->tcpc;

	env = dpm_get_reaction_env(pd_port);

	do {
		evt = dpm_check_reaction_available(pd_port, env, reaction);
	} while ((evt == 0) && (++reaction < reaction_last));

	if (evt > 0 && dpm_check_clear_reaction(pd_port, reaction)) {
		clear_reaction |= reaction->bit_mask;
		DPM_DBG("clear_reaction=%d\n", evt);
	}

	dpm_reaction_clear(pd_port, clear_reaction);
	return evt;
}
