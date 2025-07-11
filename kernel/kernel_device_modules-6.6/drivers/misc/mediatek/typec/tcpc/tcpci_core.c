// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 MediaTek Inc.
 */

#include <linux/module.h>
#if IS_ENABLED(CONFIG_TCPC_CLASS)
#include <linux/init.h>
#include <linux/device.h>
#include <linux/version.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/of.h>
#include <linux/power_supply.h>
#include <linux/workqueue.h>

#include "inc/tcpci.h"
#include "inc/tcpci_typec.h"

#if IS_ENABLED(CONFIG_USB_POWER_DELIVERY)
#include "pd_dpm_prv.h"
#include "inc/tcpm.h"
#if CONFIG_RECV_BAT_ABSENT_NOTIFY
#include "mtk_battery.h"
#endif /* CONFIG_RECV_BAT_ABSENT_NOTIFY */
#endif /* CONFIG_USB_POWER_DELIVERY */
#include "inc/rt-regmap.h"

#define TCPC_CORE_VERSION		"2.0.32_MTK"

static ssize_t tcpc_show_property(struct device *dev,
				  struct device_attribute *attr, char *buf);
static ssize_t tcpc_store_property(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count);

#define TCPC_DEVICE_ATTR(_name, _mode)					\
{									\
	.attr = { .name = #_name, .mode = _mode },			\
	.show = tcpc_show_property,					\
	.store = tcpc_store_property,					\
}

struct class *tcpc_class;
EXPORT_SYMBOL_GPL(tcpc_class);

static int bootmode;

static struct device_type tcpc_dev_type;

static struct device_attribute tcpc_device_attributes[] = {
	TCPC_DEVICE_ATTR(typec_role, 0664),
	TCPC_DEVICE_ATTR(local_rp_level, 0664),
	TCPC_DEVICE_ATTR(timer, 0220),
	TCPC_DEVICE_ATTR(alert_ratelimit, 0664),
	TCPC_DEVICE_ATTR(vbus_level, 0444),
	TCPC_DEVICE_ATTR(cc_high, 0444),
#if IS_ENABLED(CONFIG_USB_POWER_DELIVERY)
	TCPC_DEVICE_ATTR(pd_test, 0664),
	TCPC_DEVICE_ATTR(caps_info, 0444),
	TCPC_DEVICE_ATTR(pe_ready, 0444),
#endif /* CONFIG_USB_POWER_DELIVERY */
#if IS_ENABLED(CONFIG_BATTERY_SAMSUNG)
	TCPC_DEVICE_ATTR(ss_factory, 0666),
#endif
};

enum {
	TCPC_DESC_TYPEC_ROLE = 0,
	TCPC_DESC_LOCAL_RP_LEVEL,
	TCPC_DESC_TIMER,
	TCPC_DESC_ALERT_RATELIMIT,
	TCPC_TCPM_VBUS_LEVEL,
	TCPC_TCPM_CC_HIGH,
#if IS_ENABLED(CONFIG_USB_POWER_DELIVERY)
	TCPC_DESC_PD_TEST,
	TCPC_DESC_CAP_INFO,
	TCPC_DESC_PE_READY,
#endif /* CONFIG_USB_POWER_DELIVERY */
#if IS_ENABLED(CONFIG_BATTERY_SAMSUNG)
	TCPC_DESC_SS_FACTORY,
#endif
};

static struct attribute *__tcpc_attrs[ARRAY_SIZE(tcpc_device_attributes) + 1];
static struct attribute_group tcpc_attr_group = {
	.attrs = __tcpc_attrs,
};

static const struct attribute_group *tcpc_attr_groups[] = {
	&tcpc_attr_group,
	NULL,
};

static const char *const local_rp_level_names[] = {
	"Default",
	"1.5A",
	"3A",
};

static ssize_t tcpc_show_property(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct tcpc_device *tcpc = to_tcpc_device(dev);
	const ptrdiff_t offset = attr - tcpc_device_attributes;
	int ret = 0;
#if IS_ENABLED(CONFIG_USB_POWER_DELIVERY)
	int i = 0;
	struct pe_data *pe_data;
	struct pd_port *pd_port;
	struct tcpm_power_cap_val cap;
#endif	/* CONFIG_USB_POWER_DELIVERY */

	switch (offset) {
	case TCPC_DESC_TYPEC_ROLE:
		ret = snprintf(buf, 256, "%s\n",
			       typec_role_name[tcpc->typec_role]);
		if (ret < 0)
			break;
		break;
	case TCPC_DESC_LOCAL_RP_LEVEL:
		ret = snprintf(buf, 256, "%s\n",
			local_rp_level_names[tcpc->typec_local_rp_level]);
		if (ret < 0)
			break;
		break;
	case TCPC_DESC_ALERT_RATELIMIT:
		ret = snprintf(buf, 256, "%d\n", tcpc->alert_rs.burst);
		if (ret < 0)
			break;
		break;
	case TCPC_TCPM_VBUS_LEVEL:
		ret = snprintf(buf, 256, "%d\n", tcpm_inquire_vbus_level(tcpc, true));
		if (ret < 0)
			return ret;
		break;
	case TCPC_TCPM_CC_HIGH:
		ret = snprintf(buf, 256, "%d\n", tcpm_inquire_cc_high(tcpc));
		if (ret < 0)
			return ret;
		break;
#if IS_ENABLED(CONFIG_USB_POWER_DELIVERY)
	case TCPC_DESC_PD_TEST:
		ret = snprintf(buf, 256, "%s\n%s\n%s\n%s\n%s\n%s\n%s\n%s\n%s\n",
				"1: pr_swap", "2: dr_swap", "3: vconn_swap",
				"4: soft reset", "5: hard reset",
				"6: get_src_cap", "7: get_sink_cap",
				"8: discover_id", "9: discover_cable_id");
		if (ret < 0)
			dev_dbg(dev, "%s: ret=%d\n", __func__, ret);
		break;
	case TCPC_DESC_CAP_INFO:
		pd_port = &tcpc->pd_port;
		pe_data = &pd_port->pe_data;
		ret = snprintf(buf+strlen(buf), 256, "selected_cap = %d\n",
				pe_data->selected_cap);
		if (ret < 0)
			break;
		ret = snprintf(buf+strlen(buf), 256, "%s\n",
				"local_src_cap(type, vmin, vmax, oper)");
		if (ret < 0)
			break;
		for (i = 0; i < pd_port->local_src_cap.nr; i++) {
			tcpm_extract_power_cap_val(
				pd_port->local_src_cap.pdos[i],
				&cap);
			ret = snprintf(buf+strlen(buf), 256, "%d %d %d %d\n",
				      cap.type, cap.min_mv, cap.max_mv, cap.ma);
			if (ret < 0)
				break;
		}
		ret = snprintf(buf+strlen(buf), 256, "%s\n",
				"local_snk_cap(type, vmin, vmax, ioper)");
		if (ret < 0)
			break;
		for (i = 0; i < pd_port->local_snk_cap.nr; i++) {
			tcpm_extract_power_cap_val(
				pd_port->local_snk_cap.pdos[i],
				&cap);
			ret = snprintf(buf+strlen(buf), 256, "%d %d %d %d\n",
				      cap.type, cap.min_mv, cap.max_mv, cap.ma);
			if (ret < 0)
				break;
		}
		ret = snprintf(buf+strlen(buf), 256, "%s\n",
				"remote_src_cap(type, vmin, vmax, ioper)");
		if (ret < 0)
			break;
		for (i = 0; i < pe_data->remote_src_cap.nr; i++) {
			tcpm_extract_power_cap_val(
				pe_data->remote_src_cap.pdos[i],
				&cap);
			ret = snprintf(buf+strlen(buf), 256, "%d %d %d %d\n",
				      cap.type, cap.min_mv, cap.max_mv, cap.ma);
			if (ret < 0)
				break;
		}
		ret = snprintf(buf+strlen(buf), 256, "%s\n",
				"remote_snk_cap(type, vmin, vmax, ioper)");
		if (ret < 0)
			break;
		for (i = 0; i < pe_data->remote_snk_cap.nr; i++) {
			tcpm_extract_power_cap_val(
				pe_data->remote_snk_cap.pdos[i],
				&cap);
			ret = snprintf(buf+strlen(buf), 256, "%d %d %d %d\n",
				      cap.type, cap.min_mv, cap.max_mv, cap.ma);
			if (ret < 0)
				break;
		}
		break;
	case TCPC_DESC_PE_READY:
		pd_port = &tcpc->pd_port;
		ret = snprintf(buf, 256, "%s\n",
			       pd_port->pe_data.pe_ready ? "yes" : "no");
		if (ret < 0)
			break;
		break;
#endif /* CONFIG_USB_POWER_DELIVERY */
#if IS_ENABLED(CONFIG_BATTERY_SAMSUNG)
	case TCPC_DESC_SS_FACTORY:
		ret = snprintf(buf, 256, "en = %d\n", tcpc->ss_factory);
		if (ret < 0)
			break;
		break;
#endif
	default:
		break;
	}
	return strlen(buf);
}

static int get_parameters(char *buf, unsigned long *param, int num_of_par)
{
	int cnt = 0;
	char *token = strsep(&buf, " ");

	for (cnt = 0; cnt < num_of_par; cnt++) {
		if (token) {
			if (kstrtoul(token, 0, &param[cnt]) != 0)
				return -EINVAL;

			token = strsep(&buf, " ");
		} else
			return -EINVAL;
	}

	return 0;
}

static ssize_t tcpc_store_property(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
#if IS_ENABLED(CONFIG_USB_POWER_DELIVERY)
	uint8_t role;
#endif	/* CONFIG_USB_POWER_DELIVERY */

	struct tcpc_device *tcpc = to_tcpc_device(dev);
	const ptrdiff_t offset = attr - tcpc_device_attributes;
	int ret;
	unsigned long val;

	switch (offset) {
	case TCPC_DESC_TYPEC_ROLE:
	case TCPC_DESC_LOCAL_RP_LEVEL:
	case TCPC_DESC_TIMER:
		ret = get_parameters((char *)buf, &val, 1);
		if (ret < 0) {
			dev_err(dev, "get parameters fail\n");
			return -EINVAL;
		}
		switch (offset) {
		case TCPC_DESC_TYPEC_ROLE:
			tcpm_typec_change_role(tcpc, val);
			break;
		case TCPC_DESC_LOCAL_RP_LEVEL:
			switch (val) {
			case TYPEC_RP_DFT:
			case TYPEC_RP_1_5:
			case TYPEC_RP_3_0:
				tcpc->typec_local_rp_level = val;
				break;
			default:
				break;
			}
			break;
		case TCPC_DESC_TIMER:
			if (val < PD_TIMER_NR)
				tcpc_enable_timer(tcpc, val);
			break;
		default:
			break;
		}
		break;
	case TCPC_DESC_ALERT_RATELIMIT:
		ret = get_parameters((char *)buf, &val, 1);
		if (ret < 0) {
			dev_notice(dev, "get parameters fail\n");
			return -EINVAL;
		}
		tcpc->alert_rs.burst = val;
		break;
#if IS_ENABLED(CONFIG_USB_POWER_DELIVERY)
	case TCPC_DESC_PD_TEST:
		ret = get_parameters((char *)buf, &val, 1);
		if (ret < 0) {
			dev_err(dev, "get parameters fail\n");
			return -EINVAL;
		}
		switch (val) {
		case 1: /* Power Role Swap */
			role = tcpm_inquire_pd_power_role(tcpc);
			if (role == PD_ROLE_SINK)
				role = PD_ROLE_SOURCE;
			else
				role = PD_ROLE_SINK;
			tcpm_dpm_pd_power_swap(tcpc, role, NULL);
			break;
		case 2: /* Data Role Swap */
			role = tcpm_inquire_pd_data_role(tcpc);
			if (role == PD_ROLE_UFP)
				role = PD_ROLE_DFP;
			else
				role = PD_ROLE_UFP;
			tcpm_dpm_pd_data_swap(tcpc, role, NULL);
			break;
		case 3: /* Vconn Swap */
			role = tcpm_inquire_pd_vconn_role(tcpc);
			if (role == PD_ROLE_VCONN_OFF)
				role = PD_ROLE_VCONN_ON;
			else
				role = PD_ROLE_VCONN_OFF;
			tcpm_dpm_pd_vconn_swap(tcpc, role, NULL);
			break;
		case 4: /* Software Reset */
			tcpm_dpm_pd_soft_reset(tcpc, NULL);
			break;
		case 5: /* Hardware Reset */
			tcpm_dpm_pd_hard_reset(tcpc, NULL);
			break;
		case 6:
			tcpm_dpm_pd_get_source_cap(tcpc, NULL);
			break;
		case 7:
			tcpm_dpm_pd_get_sink_cap(tcpc, NULL);
			break;
		case 8:
			tcpm_dpm_vdm_discover_id(tcpc, NULL);
			break;
		case 9:
			tcpm_dpm_vdm_discover_cable_id(tcpc, NULL);
			break;
		default:
			break;
		}
		break;
#endif /* CONFIG_USB_POWER_DELIVERY */
#if IS_ENABLED(CONFIG_BATTERY_SAMSUNG)
	case TCPC_DESC_SS_FACTORY:
		ret = get_parameters((char *)buf, &val, 1);
		if (ret < 0) {
			dev_err(dev, "get parameters fail\n");
			return -EINVAL;
		}
		tcpc->ss_factory = val;
		ret = tcpci_ss_factory(tcpc);
		if (ret < 0) {
			dev_err(dev, "set ss factory %d fail\n",
						tcpc->ss_factory);
			return ret;
		}
		break;
#endif
	default:
		break;
	}
	return count;
}

static int tcpc_match_device_by_name(struct device *dev, const void *data)
{
	const char *name = data;
	struct tcpc_device *tcpc = dev_get_drvdata(dev);

	return strcmp(tcpc->desc.name, name) == 0;
}

struct tcpc_device *tcpc_dev_get_by_name(const char *name)
{
	struct device *dev = class_find_device(tcpc_class,
			NULL, (const void *)name, tcpc_match_device_by_name);
	return dev ? dev_get_drvdata(dev) : NULL;
}
EXPORT_SYMBOL(tcpc_dev_get_by_name);

static void tcpc_device_release(struct device *dev)
{
	struct tcpc_device *tcpc = to_tcpc_device(dev);

	pr_info("%s : %s device release\n", __func__, dev_name(dev));
	PD_WARN_ON(tcpc == NULL);
	/* Un-init pe thread */
#if IS_ENABLED(CONFIG_USB_POWER_DELIVERY)
	tcpci_event_deinit(tcpc);
#endif /* CONFIG_USB_POWER_DELIVERY */
	/* Un-init timer thread */
	tcpci_timer_deinit(tcpc);
	/* Un-init Mutex */
	/* Do initialization */
}

static void tcpc_event_init_work(struct work_struct *work);

struct tcpc_device *tcpc_device_register(struct device *parent,
	struct tcpc_desc *tcpc_desc, struct tcpc_ops *ops, void *drv_data)
{
	struct tcpc_device *tcpc;
	int ret = 0, i = 0;

	pr_info("%s register tcpc device (%s)\n", __func__, tcpc_desc->name);
	tcpc = devm_kzalloc(parent, sizeof(*tcpc), GFP_KERNEL);
	if (!tcpc) {
		pr_err("%s : allocate tcpc memory failed\n", __func__);
		return NULL;
	}

	tcpc->evt_wq = alloc_ordered_workqueue("%s", 0, tcpc_desc->name);
	for (i = 0; i < TCP_NOTIFY_IDX_NR; i++)
		srcu_init_notifier_head(&tcpc->evt_nh[i]);

	mutex_init(&tcpc->access_lock);
	mutex_init(&tcpc->typec_lock);
	mutex_init(&tcpc->timer_lock);
	mutex_init(&tcpc->mr_lock);
	spin_lock_init(&tcpc->timer_tick_lock);
	init_waitqueue_head(&tcpc->resume_wait_que);

	tcpc->dev.class = tcpc_class;
	tcpc->dev.type = &tcpc_dev_type;
	tcpc->dev.parent = parent;
	tcpc->dev.release = tcpc_device_release;
	dev_set_drvdata(&tcpc->dev, tcpc);
	tcpc->drv_data = drv_data;
	dev_set_name(&tcpc->dev, "%s", tcpc_desc->name);
	tcpc->desc = *tcpc_desc;
	tcpc->ops = ops;
	tcpc->typec_local_rp_level = tcpc_desc->rp_lvl;
	tcpc->typec_remote_rp_level = TYPEC_CC_VOLT_SNK_DFT;
	tcpc->typec_polarity = false;
	tcpc->bootmode = bootmode;
	tcpc->cc_hi = INT_MAX;
	tcpc->tcpc_vconn_supply = tcpc_desc->vconn_supply;
	ratelimit_state_init(&tcpc->alert_rs, HZ, 500);

	device_set_of_node_from_dev(&tcpc->dev, parent);

	ret = device_register(&tcpc->dev);
	if (ret) {
		kfree(tcpc);
		return ERR_PTR(ret);
	}
	device_init_wakeup(&tcpc->dev, true);

	tcpci_timer_init(tcpc);
	INIT_DELAYED_WORK(&tcpc->event_init_work, tcpc_event_init_work);
#if IS_ENABLED(CONFIG_USB_POWER_DELIVERY)
	tcpci_event_init(tcpc);
	init_waitqueue_head(&tcpc->tx_wait_que);
	atomic_set(&tcpc->tx_pending, 0);
	mutex_init(&tcpc->rxbuf_lock);
	INIT_DELAYED_WORK(&tcpc->tx_pending_work, tcpc_tx_pending_work_func);
	pd_core_init(tcpc);
#endif /* CONFIG_USB_POWER_DELIVERY */

	return tcpc;
}
EXPORT_SYMBOL(tcpc_device_register);

int tcpc_device_irq_enable(struct tcpc_device *tcpc)
{
	int ret;

	if (!tcpc->ops->init) {
		pr_notice("%s Please implment tcpc ops init function\n",
		__func__);
		return -EINVAL;
	}

	tcpci_lock_typec(tcpc);
	ret = tcpci_init(tcpc, false);
	if (ret < 0) {
		tcpci_unlock_typec(tcpc);
		pr_err("%s tcpc init fail\n", __func__);
		return ret;
	}

	ret = tcpc_typec_init(tcpc, tcpc->desc.role_def);
	tcpci_unlock_typec(tcpc);
	if (ret < 0) {
		pr_err("%s : tcpc typec init fail\n", __func__);
		return ret;
	}

	schedule_delayed_work(&tcpc->event_init_work, 0);

	pr_info("%s : tcpc irq enable OK!\n", __func__);
	return 0;
}
EXPORT_SYMBOL(tcpc_device_irq_enable);

#if CONFIG_USB_PD_REV30
static void bat_update_work_func(struct work_struct *work)
{
	struct tcpc_device *tcpc = container_of(work,
		struct tcpc_device, bat_update_work.work);
	union power_supply_propval value;
	int ret;

	ret = power_supply_get_property(
			tcpc->bat_psy, POWER_SUPPLY_PROP_CAPACITY, &value);
	if (ret == 0) {
		TCPC_DBG("%s battery update soc = %d\n",
					__func__, value.intval);
		tcpc->bat_soc = value.intval;
	} else
		TCPC_ERR("%s get battery capacity fail\n", __func__);

	ret = power_supply_get_property(tcpc->bat_psy,
		POWER_SUPPLY_PROP_STATUS, &value);
	if (ret == 0) {
		if (value.intval == POWER_SUPPLY_STATUS_CHARGING) {
			TCPC_DBG("%s Battery Charging, soc = %d\n",
				  __func__, tcpc->bat_soc);
			tcpc->charging_status = BSDO_BAT_INFO_CHARGING;
		} else if (value.intval == POWER_SUPPLY_STATUS_DISCHARGING) {
			TCPC_DBG("%s Battery Discharging, soc = %d\n",
				  __func__, tcpc->bat_soc);
			tcpc->charging_status = BSDO_BAT_INFO_DISCHARGING;
		} else {
			TCPC_DBG("%s Battery Idle, soc = %d\n",
				  __func__, tcpc->bat_soc);
			tcpc->charging_status = BSDO_BAT_INFO_IDLE;
		}
	}
	if (ret < 0)
		TCPC_ERR("%s get battery charger now fail\n", __func__);

	tcpm_update_bat_status_soc(tcpc,
		PD_BAT_REF_FIXED0, tcpc->charging_status, tcpc->bat_soc * 10);
}

static int bat_nb_call_func(
	struct notifier_block *nb, unsigned long val, void *v)
{
	struct tcpc_device *tcpc = container_of(nb, struct tcpc_device, bat_nb);
	struct power_supply *psy = (struct power_supply *)v;

	if (!tcpc) {
		TCPC_ERR("%s tcpc is null\n", __func__);
		return NOTIFY_OK;
	}

	if (val == PSY_EVENT_PROP_CHANGED &&
#if IS_ENABLED(CONFIG_BATTERY_SAMSUNG)
		strcmp(psy->desc->name, "mtk-fg-battery") == 0)
#else
		strcmp(psy->desc->name, "battery") == 0)
#endif
		schedule_delayed_work(&tcpc->bat_update_work, 0);
	return NOTIFY_OK;
}
#endif /* CONFIG_USB_PD_REV30 */

static void tcpc_event_init_work(struct work_struct *work)
{
#if IS_ENABLED(CONFIG_USB_POWER_DELIVERY)
	struct tcpc_device *tcpc = container_of(
			work, struct tcpc_device, event_init_work.work);
#if CONFIG_USB_PD_REV30
	int retval;
#endif /* CONFIG_USB_PD_REV30 */

	tcpci_lock_typec(tcpc);
#if CONFIG_USB_PD_WAIT_BC12
	tcpc->chg_psy = devm_power_supply_get_by_phandle(
		tcpc->dev.parent, "charger");
	if (IS_ERR_OR_NULL(tcpc->chg_psy)) {
		tcpci_unlock_typec(tcpc);
		TCPC_ERR("%s get charger psy fail\n", __func__);
		return;
	}
#endif /* CONFIG_USB_PD_WAIT_BC12 */
	tcpc->pd_inited_flag = 1;
	pr_info("%s typec attach new = %d\n", __func__, tcpc->typec_attach_new);
	if (tcpc->typec_attach_new)
		pd_put_cc_attached_event(tcpc, tcpc->typec_attach_new);
	tcpci_unlock_typec(tcpc);

#if CONFIG_USB_PD_REV30
	INIT_DELAYED_WORK(&tcpc->bat_update_work, bat_update_work_func);
#if IS_ENABLED(CONFIG_BATTERY_SAMSUNG)
	tcpc->bat_psy = power_supply_get_by_name("mtk-fg-battery");
#else
	tcpc->bat_psy = power_supply_get_by_name("battery");
#endif
	if (!tcpc->bat_psy) {
		TCPC_ERR("%s get battery psy fail\n", __func__);
		return;
	}
	tcpc->charging_status = BSDO_BAT_INFO_IDLE;
	tcpc->bat_soc = 0;
	tcpc->bat_nb.notifier_call = bat_nb_call_func;
	tcpc->bat_nb.priority = 0;
	retval = power_supply_reg_notifier(&tcpc->bat_nb);
	if (retval < 0)
		pr_err("%s register power supply notifier fail\n", __func__);
#endif /* CONFIG_USB_PD_REV30 */

#endif /* CONFIG_USB_POWER_DELIVERY */
}

struct tcp_notifier_block_wrapper {
	struct notifier_block stub_nb;
	struct notifier_block *action_nb;
};

static int tcp_notifier_func_stub(struct notifier_block *nb,
	unsigned long action, void *data)
{
	struct tcp_notifier_block_wrapper *nb_wrapper =
		container_of(nb, struct tcp_notifier_block_wrapper, stub_nb);
	struct notifier_block *action_nb = nb_wrapper->action_nb;

	return action_nb->notifier_call(action_nb, action, data);
}

struct tcpc_managed_res {
	void *res;
	void *key;
	int prv_id;
	struct tcpc_managed_res *next;
};

static int __add_wrapper_to_managed_res_list(struct tcpc_device *tcpc,
	void *res, void *key, int prv_id)
{
	struct tcpc_managed_res *tail;
	struct tcpc_managed_res *mres;

	mres = devm_kzalloc(&tcpc->dev, sizeof(*mres), GFP_KERNEL);
	if (!mres)
		return -ENOMEM;
	mres->res = res;
	mres->key = key;
	mres->prv_id = prv_id;
	mutex_lock(&tcpc->mr_lock);
	tail = tcpc->mr_head;
	if (tail) {
		while (tail->next)
			tail = tail->next;
		tail->next = mres;
	} else
		tcpc->mr_head = mres;
	mutex_unlock(&tcpc->mr_lock);

	return 0;
}

static int __register_tcp_dev_notifier(struct tcpc_device *tcpc,
	struct notifier_block *nb, uint8_t idx)
{
	struct tcp_notifier_block_wrapper *nb_wrapper;
	int retval;

	nb_wrapper = devm_kzalloc(
		&tcpc->dev, sizeof(*nb_wrapper), GFP_KERNEL);
	if (!nb_wrapper)
		return -ENOMEM;
	nb_wrapper->action_nb = nb;
	nb_wrapper->stub_nb.notifier_call = tcp_notifier_func_stub;
	retval = srcu_notifier_chain_register(
		tcpc->evt_nh + idx, &nb_wrapper->stub_nb);
	if (retval < 0) {
		devm_kfree(&tcpc->dev, nb_wrapper);
		return retval;
	}
	retval = __add_wrapper_to_managed_res_list(
				tcpc, nb_wrapper, nb, idx);
	if (retval < 0)
		dev_notice(&tcpc->dev,
			   "Failed to add resource to manager(%d)\n", retval);

	return 0;
}

static bool __is_mulit_bits_set(uint32_t flags)
{
	if (flags) {
		flags &= (flags - 1);
		return flags ? true : false;
	}

	return false;
}

int register_tcp_dev_notifier(struct tcpc_device *tcpc,
			struct notifier_block *nb, uint8_t flags)
{
	int ret = 0, i = 0;

	if (__is_mulit_bits_set(flags)) {
		for (i = 0; i < TCP_NOTIFY_IDX_NR; i++) {
			if (flags & (1 << i)) {
				ret = __register_tcp_dev_notifier(
							tcpc, nb, i);
				if (ret < 0)
					return ret;
			}
		}
	} else { /* single bit */
		for (i = 0; i < TCP_NOTIFY_IDX_NR; i++) {
			if (flags & (1 << i)) {
				ret = srcu_notifier_chain_register(
				&tcpc->evt_nh[i], nb);
				break;
			}
		}
	}

	return ret;
}
EXPORT_SYMBOL(register_tcp_dev_notifier);

static void *__remove_wrapper_from_managed_res_list(
	struct tcpc_device *tcpc, void *key, int prv_id)
{
	void *retval = NULL;
	struct tcpc_managed_res *mres = tcpc->mr_head;
	struct tcpc_managed_res *prev = NULL;

	mutex_lock(&tcpc->mr_lock);
	while (mres) {
		if (mres->key == key && mres->prv_id == prv_id) {
			retval = mres->res;
			if (prev)
				prev->next = mres->next;
			else
				tcpc->mr_head = mres->next;
			devm_kfree(&tcpc->dev, mres);
			break;
		}
		prev = mres;
		mres = mres->next;
	}
	mutex_unlock(&tcpc->mr_lock);

	return retval;
}

static int __unregister_tcp_dev_notifier(struct tcpc_device *tcpc,
	struct notifier_block *nb, uint8_t idx)
{
	struct tcp_notifier_block_wrapper *nb_wrapper;
	int retval;

	nb_wrapper = __remove_wrapper_from_managed_res_list(tcpc, nb, idx);
	if (nb_wrapper) {
		retval = srcu_notifier_chain_unregister(
			tcpc->evt_nh + idx, &nb_wrapper->stub_nb);
		devm_kfree(&tcpc->dev, nb_wrapper);
		return retval;
	}

	return -ENOENT;
}

int unregister_tcp_dev_notifier(struct tcpc_device *tcpc,
				struct notifier_block *nb, uint8_t flags)
{
	int i = 0, ret = 0;

	for (i = 0; i < TCP_NOTIFY_IDX_NR; i++) {
		if (flags & (1 << i)) {
			ret = __unregister_tcp_dev_notifier(tcpc, nb, i);
			if (ret == -ENOENT)
				ret = srcu_notifier_chain_unregister(
					tcpc->evt_nh + i, nb);
			if (ret < 0)
				return ret;
		}
	}
	return ret;
}
EXPORT_SYMBOL(unregister_tcp_dev_notifier);

void tcpc_device_unregister(struct device *dev, struct tcpc_device *tcpc)
{
	if (!tcpc)
		return;

	tcpc_typec_deinit(tcpc);
	device_unregister(&tcpc->dev);

}
EXPORT_SYMBOL(tcpc_device_unregister);

void *tcpc_get_dev_data(struct tcpc_device *tcpc)
{
	return tcpc->drv_data;
}
EXPORT_SYMBOL(tcpc_get_dev_data);

void tcpci_lock_typec(struct tcpc_device *tcpc)
{
	mutex_lock(&tcpc->typec_lock);
}
EXPORT_SYMBOL(tcpci_lock_typec);

void tcpci_unlock_typec(struct tcpc_device *tcpc)
{
	mutex_unlock(&tcpc->typec_lock);
}
EXPORT_SYMBOL(tcpci_unlock_typec);

static void tcpc_init_attrs(struct device_type *dev_type)
{
	int i;

	dev_type->groups = tcpc_attr_groups;
	for (i = 0; i < ARRAY_SIZE(tcpc_device_attributes); i++)
		__tcpc_attrs[i] = &tcpc_device_attributes[i].attr;
}

static int __init tcpc_class_init(void)
{
	struct device_node *of_chosen = NULL;
	const struct {
		u32 size;
		u32 tag;
		u32 bootmode;
		u32 boottype;
	} *tag = NULL;

	pr_info("%s (%s)\n", __func__, TCPC_CORE_VERSION);

	tcpc_class = class_create("tcpc");
	if (IS_ERR(tcpc_class)) {
		pr_info("Unable to create tcpc class; errno = %ld\n",
		       PTR_ERR(tcpc_class));
		return PTR_ERR(tcpc_class);
	}
	tcpc_init_attrs(&tcpc_dev_type);

	regmap_plat_init();

	/* mediatek boot mode */
	of_chosen = of_find_node_by_path("/chosen");
	if (of_chosen == NULL)
		of_chosen = of_find_node_by_path("/chosen@0");
	if (of_chosen == NULL)
		goto out;
	tag = of_get_property(of_chosen, "atag,boot", NULL);
	if (!tag) {
		pr_notice("failed to get atag,boot\n");
		goto out;
	}
	pr_info("size:0x%x tag:0x%x mode:0x%x type:0x%x\n",
		tag->size, tag->tag, tag->bootmode, tag->boottype);
	bootmode = tag->bootmode;
out:
	pr_info("TCPC class init OK\n");
	return 0;
}

static void __exit tcpc_class_exit(void)
{
	regmap_plat_exit();

	class_destroy(tcpc_class);
	pr_info("TCPC class un-init OK\n");
}

subsys_initcall(tcpc_class_init);
module_exit(tcpc_class_exit);

void __weak sched_set_fifo(struct task_struct *p)
{
	struct sched_param sp = { .sched_priority = MAX_RT_PRIO / 2 };

	WARN_ON_ONCE(sched_setscheduler_nocheck(p, SCHED_FIFO, &sp) != 0);
}

MODULE_DESCRIPTION("Richtek TypeC Port Control Core");
MODULE_AUTHOR("Jeff Chang <jeff_chang@richtek.com>");
MODULE_VERSION(TCPC_CORE_VERSION);
#endif	/* CONFIG_TCPC_CLASS */
MODULE_LICENSE("GPL");

/* Release Version
 * 2.0.32_MTK
 * (1) Let PE go back to ready states when tx failed
 * (2) Increase CONFIG_USB_PD_VCONN_READY_TOUT from 5ms to 10ms
 * (3) Increase the priority of irq_thread
 * (4) Fix and revise I2C/IO transactions when system resumed
 *
 * 2.0.31_MTK
 * (1) Do I2C/IO transactions when system resumed
 * (2) Reduce log printing
 * (3) Revise attach/detach conditions/actions
 * (4) Disable FOD
 * (5) Add support for RT1718S
 * (6) Replace 64-bit divisions with do_div() calls
 * (7) Disable Rx SOP' when in ready states
 * (8) Revise BIST flows
 * (9) Revise discharge controls
 * (10) Control CC Open in the deinit ops
 * (11) Revise sink_vbus of standby current
 * (12) Implement alert ratelimit mechanism
 * (13) Disable CONFIG_USB_PD_DISCARD_AND_UNEXPECT_MSG
 * (14) Revise cable discovery flow
 * (15) Separate tSenderResponse for PD2 and PD3
 * (16) Enter low power mode with 5ms delay after unattached
 * (17) Fix tcpm_bk sync issues
 * (18) Fix SinkTxNG
 * (19) Call pm_system_wakeup() in delayed_work handler function
 * (20) Update pd_transmit_state when PD Hard Reset failed
 * (21) Remove the unwanted PD Hard Reset in tcpm.c
 * (22) Revise the feature of VBUS shorted to CC
 * (23) Handle typec timers first
 * (24) Revise the logics of expected_svid
 * (25) Replace BUG_ON with WARN_ON
 * (26) Start tPDDebounce always when CC Open at Attached.SNK
 * (27) Let CC pins re-toggle after entering lpm
 * (28) Add ps_changed flow
 * (29) Revise Rx flow
 * (30) Reset pd_wait_pr_swap_complete at receiving Not_Supported
 *
 * 2.0.30_MTK
 * (1) Decrease the I2C/IO transactions
 * (2) Remove the old way of get_power_status()
 * (3) Revise struct pe_data
 * (4) Add CONFIG_TYPEC_SNK_ONLY_WHEN_SUSPEND
 * (5) Spread PD_DYNAMIC_SENDER_RESPONSE to all of TCPC chips
 * (6) Spread suspend_pending to all of TCPC chips
 *
 * 2.0.29_MTK
 * (1) Revise wakeup source of pps_request
 * (2) Unlock typec_lock in tcpm_shutdown()
 * (3) Remove unnecessary preprocessor directives
 * (4) Revise struct pe_data
 * (5) Revise NoRp.SRC support again
 * (6) Not response NAK when receiving PD DP Status Update
 * (7) Revise code related to typec_state, typec_attach_*
 * (8) Revise sink vbus
 *
 * 2.0.28_MTK
 * (1) Revise rx_pending, rxbuf_lock, and discard_pending
 * (2) Revise macros
 * (3) Update modal operation supported
 * (4) Revise receiving Hard Reset after unattached
 * (5) Limit discover cable count in pd_dpm_reaction.c
 * (6) Revise custom VDM
 *
 * 2.0.27_MTK
 * (1) Do not discharge VBUS when Attached.SNK
 * (2) Bump the PD revision/version to R3.1 V1.6
 * (3) Revise WD, FOD and CTD flows
 * (4) Fix coverity issues
 * (5) Revise CC high status
 * (6) Fix PD compliance failures of Ellisys and MQP
 *
 * 2.0.26_MTK
 * (1) Fix coverity issues
 * (2) PD DP Alt Mode V2.1
 * (3) Revise WD
 * (4) Decrease tDRP to 51.2ms and dcSRC.DRP to 30%
 * (5) Remove old code
 *
 * 2.0.25_MTK
 * (1) Fix COMMON.CHECK.PD.9#1 of MQP
 * (2) Revise PR_Swap flow
 * (3) Revise WD for Titan's multi-port accessory
 *
 * 2.0.24_MTK
 * (1) Revise PR_Swap flow
 * (2) Add a dts option of attempt-discover-id-dfp;
 *
 * 2.0.23_MTK
 * (1) Revise Request flow
 * (2) Revise WD, FOD and OTP flows
 * (3) Add a tcpm function for inquiring CC high status
 *
 * 2.0.22_MTK
 * (1) Revise Vconn
 * (2) Notify in PD mode when receiving the first PD message
 * (3) VPDO support
 *
 * 2.0.21_MTK
 * (1) Add tcpm functions for manipulating local source caps
 * (2) Add tcpm functions for exiting Attached.SNK when CC opens during PD
 * (3) Enable force discharge during TYPEC_WAIT_PS_SRC_VSAFE0V
 * (4) Keep pd_traffic_idle false if tcp_event_count not zero
 * (5) Fix NoRp.SRC support
 *
 * 2.0.20_MTK
 * (1) Revise pd_dbg_info
 *
 * 2.0.19_MTK
 * (1) Revise low power mode
 * (2) Remove unused members in struct tcpc_device
 * (3) #undef CONFIG_TYPEC_CAP_RA_DETACH and CONFIG_TYPEC_CAP_POWER_OFF_CHARGE
 * (4) Use alarm timer in tcpci_timer.c
 * (5) Wait previous PD Tx done before sending next PD Tx
 * (6) Revise __remove_wrapper_from_managed_res_list()
 * (7) Revise pd_sync_sop_spec_revision()
 * (8) #undef CONFIG_USB_PD_DPM_AUTO_SEND_ALERT
 * (9) Revise water detection/protection
 * (10) CC open for 20ms in tcpc_typec_init()
 * (11) Fix ErrorRecovery when RX_OVERFLOW
 * (12) Change pps_request_task as pps_request_dwork
 * (13) Not to send unnecessary Discover Identity
 * (14) Deny requests of data role swap during modal operation
 * (15) Disable sink/ufp delay by default
 * (16) Check vSafe0V during Hard Reset
 * (17) Call tcpci_vbus_level_changed() with vbus_level changed for ALERT_V20
 * (18) Revise PD DP Alternative Mode
 * (19) Revise pd_traffic_control
 * (20) Revise *_tcpc_deinit()
 * (21) Delay IRQ processing of water detection for 100ms
 * (22) Revise SW workaround of cap miss match
 * (23) Change to Sink only when KPOC
 * (24) Response Discover Identity with less VDOs if requested repeatedly
 *
 * 2.0.18_MTK
 * (1) Fix typos
 * (2) Revise tcpci_alert.c
 * (3) Request the previous voltage/current first with new Source_Capabilities
 * (4) #undef CONFIG_USB_PD_ALT_MODE_RTDC
 * (5) Use typec_state in typec_is_cc_attach()
 * (6) Add epr_source_pdp into struct pd_source_cap_ext for new PD spec
 * (7) Revise get_status_once, pd_traffic_idle
 * (8) Revise IRQ handling
 * (9) Start PD policy engine without delay
 * (10) Revise typec_remote_rp_level
 * (11) Revise Hard Reset timer as PD Sink
 * (12) Set mismatch to false in dpm_build_default_request_info()
 * (13) Revise tcpc_timer
 * (14) Remove notifier_supply_num
 * (15) Fix COMMON.CHECK.PD.5 Check Unexpected Messages and Signals
 * (16) Fix TEST.PD.PROT.PORT3.4 Invalid Battery Capabilities Reference
 * (17) Fix TEST.PD.VDM.SRC.1 Discovery Process and Enter Mode
 * (18) Add PD_TIMER_VSAFE5V_DELAY
 * (19) Revise SOP' communication
 * (20) Revise PD request as Sink
 * (21) Disable old legacy cable workaround
 * (22) Enable PD Safe0V Timeout
 * (23) Replace calling of sched_setscheduler() with sched_set_fifo()
 *
 * 2.0.17_MTK
 * (1) Add CONFIG_TYPEC_LEGACY3_ALWAYS_LOCAL_RP
 * (2) Fix a synchronization/locking problem in pd_notify_pe_error_recovery()
 * (3) Add USB_VID_MQP
 * (4) Revise the return value checking of tcpc_device_register()
 *
 * 2.0.16_MTK
 * (1) Check the return value of wait_event_interruptible()
 * (2) Revise *_get_cc()
 * (3) Revise role_def
 * (4) Fix COMMON.CHECK.PD.10
 *
 * 2.0.15_MTK
 * (1) undef CONFIG_COMPATIBLE_APPLE_TA
 * (2) Fix TEST.PD.PROT.ALL.5 Unrecognized Message (PD2)
 * (3) Fix TEST.PD.PROT.ALL3.3 Invalid Manufacturer Info Target
 * (4) Fix TEST.PD.PROT.ALL3.4 Invalid Manufacturer Info Ref
 * (5) Fix TEST.PD.PROT.SRC.11 Unexpected Message Received in Ready State (PD2)
 * (6) Fix TEST.PD.PROT.SRC.13 PR_Swap - GoodCRC not sent in Response to PS_RDY
 * (7) Fix TEST.PD.VDM.SRC.2 Invalid Fields - Discover Identity (PD2)
 * (8) Revise the usages of PD_TIMER_NO_RESPONSE
 * (9) Retry to send Source_Capabilities after PR_Swap
 * (10) Fix tcpm_get_remote_power_cap() and __tcpm_inquire_select_source_cap()
 * (11) Increase the threshold to enter PE_ERROR_RECOVERY_ONCE from 2 to 4
 * (12) Change wait_event() back to wait_event_interruptible() for not being
 *	detected as hung tasks
 *
 * 2.0.14_MTK
 * (1) Move out typec_port registration and operation to rt_pd_manager.c
 * (2) Rename CONFIG_TYPEC_WAIT_BC12 to CONFIG_USB_PD_WAIT_BC12
 * (3) Not to set power/data/vconn role repeatedly
 * (4) Revise vconn highV protection
 * (5) Revise tcpc timer
 * (6) Reduce IBUS Iq for MT6371, MT6372 and MT6360
 * (7) Decrease VBUS present threshold (VBUS_CAL) by 60mV (2LSBs) for RT171x
 * (8) Replace \r\n with \n for resolving logs without newlines
 * (9) Remove the member time_stamp from struct pd_msg
 * (10) Remove NoResponseTimer as Sink for new PD spec
 * (11) Revise responses of Reject and Not_Supported
 * (12) Revise the usages of pd_traffic_control and typec_power_ctrl
 * (13) Revise the usages of wait_event_*()
 * (14) Add PD capability for TYPEC_ATTACHED_DBGACC_SNK
 * (15) Utilize rt-regmap to reduce I2C accesses
 *
 * 2.0.13_MTK
 * (1) Add TCPC flags for VCONN_SAFE5V_ONLY
 * (2) Add boolean property attempt_discover_svid in dts/dtsi
 * (3) Add a TCPM API for postponing Type-C role change until unattached
 * (4) Update VDOs according new PD spec
 * (5) Add an option for enabling/disabling the support of DebugAccessory.SRC
 * (6) Add the workaround for delayed ps_change related to PS_RDY
 *     during PR_SWAP
 * (7) Always Back to PE ready state in pd_dpm_dfp_inform_id() and
 *     pd_dpm_dfp_inform_svids()
 * (8) Re-fetch triggered_timer and enable_mask after lock acquisition
 * (9) Leave low power mode only when CC is detached
 * (10) Revise code related to pd_check_rev30()
 * (11) Bypass BC1.2 for PR_SWAP from Source to Sink
 * (12) Support charging icon for AudioAccessory
 * (13) Replace tcpc_dev with tcpc
 * (14) TCPCI Alert V10 and V20 co-exist
 * (15) Resolve DP Source/Sink Both Connected when acting as DFP_U
 * (16) Change CONFIG_TYPEC_SNK_CURR_DFT from 150 to 100 (mA)
 * (17) Define CONFIG_USB_PD_PR_SWAP_ERROR_RECOVERY by default
 * (18) Add an option for TCPC log with port name
 * (19) USB-C states go from ErrorRecovery to Unattached.SRC with Try.SRC role
 * (20) Revise dts/dtsi value for DisplayPort Alternative Mode
 * (21) Mask vSafe0V IRQ before entering low power mode
 * (22) Disable auto idle mode before entering low power mode
 * (23) Reset Protocol FSM and clear RX alerts twice before clock gating
 *
 * 2.0.12_MTK
 * (1) Fix voltage/current steps of RDO for APDO
 * (2) Non-blocking TCPC notification by default
 * (3) Fix synchronization/locking problems
 * (4) Fix NoRp.SRC support
 *
 * 2.0.11_MTK
 * (1) Fix PD compliance failures of Ellisys and MQP
 * (2) Wait the result of BC1.2 before starting PD policy engine
 * (3) Fix compile warnings
 * (4) Fix NoRp.SRC support
 *
 * 2.0.10_MTK
 * (1) fix battery noitifier plug out cause recursive locking detected in
 *     nh->srcu.
 *
 * 2.0.9_MTK
 * (1) fix 10k A-to-C legacy cable workaround side effect when
 *     cable plug in at worakround flow.
 *
 * 2.0.8_MTK
 * (1) fix timeout thread flow for wakeup pd event thread
 *     after disable timer first.
 *
 * 2.0.7_MTK
 * (1) add extract pd source capability pdo defined in
 *     PD30 v1.1 ECN for pe40 get apdo profile.
 *
 * 2.0.6_MTK
 * (1) register battery notifier for battery plug out
 *     avoid TA hardreset 3 times will show charing icon.
 *
 * 2.0.5_MTK
 * (1) add CONFIG_TYPEC_CAP_NORP_SRC to support
 *      A-to-C No-Rp cable.
 * (2) add handler pd eint with eint mask
 *
 * 2.0.4_MTK
 * (1) add CONFIG_TCPC_NOTIFIER_LATE_SYNC to
 *      move irq_enable to late_initcall_sync stage
 *      to prevent from notifier_supply_num setting wrong.
 *
 * 2.0.3_MTK
 * (1) use local_irq_XXX to instead raw_local_irq_XXX
 *      to fix lock prov WARNING
 * (2) Remove unnecessary charger detect flow. it does
 *      not need to switch BC1-2 path on otg_en
 *
 * 2.0.2_MTK
 * (1) Fix Coverity and check patch issue
 * (2) Fix 32-bit project build error
 *
 * 2.0.1_MTK
 *	First released PD3.0 Driver for MTK Platform
 */

