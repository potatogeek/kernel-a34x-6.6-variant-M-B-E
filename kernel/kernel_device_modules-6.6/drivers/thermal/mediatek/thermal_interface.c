// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 MediaTek Inc.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/types.h>
#include <linux/proc_fs.h>
#include <linux/cpumask.h>
#include <linux/hrtimer.h>
#include <linux/debugfs.h>
#include <linux/uaccess.h>
#include <linux/slab.h>

#include <linux/device.h>
#include <linux/err.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/timer.h>
#include <linux/types.h>
#include <linux/debugfs.h>
#include <linux/thermal.h>
#include <soc/mediatek/dramc.h>

#include <linux/io.h>
#if IS_ENABLED(CONFIG_MTK_GPUFREQ_V2)
#include <mtk_gpufreq.h>
#endif
#include "vtskin_temp.h"
#include "thermal_interface.h"
#if IS_ENABLED(CONFIG_LEDS_MTK_MODULE)
#define CONFIG_LEDS_BRIGHTNESS_CHANGED
#include <net/genetlink.h>
#include <linux/netlink.h>
#include <linux/socket.h>
#include <leds-mtk.h>
#endif

#define MAX_HEADROOM		(100)
#define CSRAM_INIT_VAL		(0x27bc86aa)
#define is_opp_limited(opp)	(opp > 0 && opp != CSRAM_INIT_VAL)

#ifdef CONFIG_LEDS_BRIGHTNESS_CHANGED
#define NLA_DATA(na) ((void *)((char *)(na) + NLA_HDRLEN))
#define SCRN_PROC_NAME_LEN 16
static char scrn_netlink_check[SCRN_PROC_NAME_LEN] = "screen_status";

struct _SCRN_THRM_PACKAGE {
	__s32 main_scrn_status;				// 0: scrn off; 1: scrn on
	__s32 sub_scrn_status;				// 0: scrn off; 1: scrn on
	char proc_name[SCRN_PROC_NAME_LEN];	// check to avoid hacking
};

static DEFINE_MUTEX(scrn_nl_enable_lock);
static DEFINE_MUTEX(scrn_changed_lock);
static struct _SCRN_THRM_PACKAGE SCRN_Status;
static int scrn_status_changed;
static pid_t scrn_nl_pid;

/* attribute type */
enum {
	SCRN_A_UNSPEC,		/* reserved */
	SCRN_A_VAL,
	SCRN_A_MSG,
	__SCRN_A_MAX,
};
#define SCRN_A_MAX (__SCRN_A_MAX - 1)

/* commands */
enum {
	SCRN_C_UNSPEC,		/* reserved */
	SCRN_C_ECHO,
	__SCRN_C_MAX,
};
#define SCRN_C_MAX (__SCRN_C_MAX - 1)

/* attribute policy */
static struct nla_policy scrn_genl_policy[SCRN_A_MAX + 1] = {
	[SCRN_A_VAL] = { .type = NLA_NUL_STRING },
	[SCRN_A_MSG] = { .type = NLA_NUL_STRING },
};

static struct genl_family scrn_gnl_family = {
	.id			= 0,
	.hdrsize	= 0,
	.name		= "thermfamily",
	.version	= 1,
	.maxattr	= SCRN_A_MAX,
};
#endif

#define CPU_SENSOR_NUM 10
static int eas_previous_temp[CPU_SENSOR_NUM] = {25000};
static int pre_temp = 25000;

struct therm_intf_info {
	int sw_ready;
	unsigned int cpu_cluster_num;
	int is_cputcm;
	struct device *dev;
	struct mutex lock;
	struct dentry *debug_dir;
	struct ttj_info tj_info;
};

static struct therm_intf_info tm_data;

#define USER_VSENSOR_NUM 6
static struct user_vsensor_info u_vsensor[USER_VSENSOR_NUM];

void __iomem *thermal_csram_base;
EXPORT_SYMBOL(thermal_csram_base);
void __iomem *thermal_cputcm_base;
EXPORT_SYMBOL(thermal_cputcm_base);
void __iomem *thermal_apu_mbox_base;
EXPORT_SYMBOL(thermal_apu_mbox_base);
struct frs_info frs_data;
EXPORT_SYMBOL(frs_data);
struct vtskin_data *plat_vtskin_info;
EXPORT_SYMBOL(plat_vtskin_info);

static struct md_info md_info_data;
static struct pid_info pid_info_data;
static u32 bat_type;

static DEFINE_MUTEX(pid_info_lock);

#ifdef CONFIG_LEDS_BRIGHTNESS_CHANGED
static inline int genl_msg_prepare_usr_msg(u8 cmd, size_t size, pid_t pid, struct sk_buff **skbp)
{
	struct sk_buff *skb;
	void *reply;

	/* create a new netlink msg */
	skb = genlmsg_new(size, GFP_KERNEL);
	if (skb == NULL)
		return -ENOMEM;

	/* Add a new netlink message to an skb */
	reply = genlmsg_put(skb, pid, 0, &scrn_gnl_family, 0, cmd);
	if (!reply) {
		nlmsg_free(skb);
		return -ENOMEM;
	}
	*skbp = skb;

	return 0;
}

int scrn_genl_send_to_user(void *data, int len)
{
	struct sk_buff *skb;
	size_t size;
	int ret;

	mutex_lock(&scrn_nl_enable_lock);
	if (scrn_nl_pid <= 0) {
		mutex_unlock(&scrn_nl_enable_lock);
		return -1;
	}
	mutex_unlock(&scrn_nl_enable_lock);

	size = nla_total_size(len); /* total length of attribute including padding */
	pr_info("SCRN Netlink_unicast size = %zu\n", size);

	ret = genl_msg_prepare_usr_msg(SCRN_C_ECHO, size, scrn_nl_pid, &skb);
	if (ret) {
		pr_info("SCRN prepare Generic Netlink message failed\n");
		return ret;
	}

	ret = nla_put_s32(skb, SCRN_A_VAL, ((struct _SCRN_THRM_PACKAGE *)data)->main_scrn_status);
	if (ret) {
		pr_info("SCRN Add 32bit signed integer attribute failed; err code: %d\n", ret);
		nlmsg_free(skb);
		return ret;
	}
	ret = nla_put_s32(skb, SCRN_A_VAL, ((struct _SCRN_THRM_PACKAGE *)data)->sub_scrn_status);
	if (ret) {
		pr_info("SCRN Add 32bit signed integer attribute failed; err code: %d\n", ret);
		nlmsg_free(skb);
		return ret;
	}
	ret = nla_put_string(skb, SCRN_A_MSG, ((struct _SCRN_THRM_PACKAGE *)data)->proc_name);
	if (ret) {
		pr_info("SCRN Add string  attribute failed; err code: %d\n", ret);
		nlmsg_free(skb);
		return ret;
	}

	ret = genlmsg_unicast(&init_net, skb, scrn_nl_pid);
	if (ret < 0) {
		pr_info("SCRN Send to pid %d failed %d\n", scrn_nl_pid, ret);
		return ret;
	}

	pr_info("SCRN Netlink_unicast ret = %d\n", ret);

	return 0;
}

int scrn_genl_data_handler(struct sk_buff *skb, struct genl_info *info)
{
	struct nlmsghdr *nlhdr;
	struct genlmsghdr *genlhdr;
	struct nlattr *nla;
	char *str;
	int ret = 0;

	/* receive data from userspace */
	nlhdr = nlmsg_hdr(skb);
	genlhdr = nlmsg_data(nlhdr);
	nla = genlmsg_data(genlhdr);

	str = (char *)NLA_DATA(nla);
	if (strncmp(str, scrn_netlink_check, 13))
		return 0;

	/* get tid of thermal_core for the userspace to kernelspace */
	mutex_lock(&scrn_nl_enable_lock);
	scrn_nl_pid = nlhdr->nlmsg_pid;
	mutex_unlock(&scrn_nl_enable_lock);

	mutex_lock(&scrn_changed_lock);
	if (!scrn_status_changed) {
		mutex_unlock(&scrn_changed_lock);
		return 0;
	}

	/* send data to userspace */
	ret = scrn_genl_send_to_user((void *)&SCRN_Status, sizeof(struct _SCRN_THRM_PACKAGE));
	if (ret) {
		pr_info("genl_msg_send_to_user error!");
		mutex_unlock(&scrn_changed_lock);
		return 1;
	}

	scrn_status_changed = 0;
	mutex_unlock(&scrn_changed_lock);
	return 0;
}

/* operation definition */
static struct genl_ops scrn_gnl_ops_echo[] = {
	{
		.cmd	= SCRN_C_ECHO,
		.flags	= 0,
		.policy	= scrn_genl_policy,
		.doit	= scrn_genl_data_handler,
		.dumpit	= NULL,
	},
};

int scrn_generic_netlink_init(void)
{
	int state = 0;

	scrn_gnl_family.ops = scrn_gnl_ops_echo;
	scrn_gnl_family.n_ops = ARRAY_SIZE(scrn_gnl_ops_echo);

	state = genl_register_family(&scrn_gnl_family);
	if (state) {
		pr_info("genl_register_family error!!!\n");
		return -1;
	}

	pr_info("generic netlink register success!!!\n");
	memcpy(SCRN_Status.proc_name, scrn_netlink_check, strlen(scrn_netlink_check)+1);
	return 0;
}

void genetlink_exit(void)
{
	genl_unregister_family(&scrn_gnl_family);
	pr_info("generic netlink unregister.....\n");
}

int _backlight_changed_event(struct notifier_block *nb, unsigned long event,
	void *v)
{
#if IS_ENABLED(CONFIG_DEVICE_MODULES_DRM_MEDIATEK)
	struct led_conf_info *led_conf;
	int ret = 0;

	led_conf = (struct led_conf_info *)v;

	switch (event) {
	case LED_BRIGHTNESS_CHANGED:
		if (led_conf->connector_id == mtk_drm_get_conn_obj_id_from_idx(0, 1)) {
			if (led_conf->cdev.brightness > 0) {
				if (SCRN_Status.main_scrn_status == 0) {
					pr_info("Receive notification: main-screen on\n");
					SCRN_Status.main_scrn_status = 1;
					scrn_status_changed = 1;
				}
			} else {
				if (SCRN_Status.main_scrn_status == 1) {
					pr_info("Receive notification: main-screen off\n");
					SCRN_Status.main_scrn_status = 0;
					scrn_status_changed = 1;
				}
			}
		} else if (led_conf->connector_id == mtk_drm_get_conn_obj_id_from_idx(1, 1)) {
			if (led_conf->cdev.brightness > 0) {
				if (SCRN_Status.sub_scrn_status == 0) {
					pr_info("Receive notification: sub-screen on\n");
					SCRN_Status.sub_scrn_status = 1;
					scrn_status_changed = 1;
				}
			} else {
				if (SCRN_Status.sub_scrn_status == 1) {
					pr_info("Receive notification: sub-screen off\n");
					SCRN_Status.sub_scrn_status = 0;
					scrn_status_changed = 1;
				}
			}
		}
		break;

	default:
		break;
	}
#endif

	mutex_lock(&scrn_changed_lock);
	if (!scrn_status_changed) {
		mutex_unlock(&scrn_changed_lock);
		return NOTIFY_DONE;
	}

	ret = scrn_genl_send_to_user((void *)&SCRN_Status, sizeof(struct _SCRN_THRM_PACKAGE));
	if (ret)
		pr_info("Failed to send screen status");
	else
		scrn_status_changed = 0;
	mutex_unlock(&scrn_changed_lock);

	return NOTIFY_DONE;
}

static struct notifier_block leds_init_notifier = {
	.notifier_call = _backlight_changed_event,
};
#endif

static int therm_intf_read_csram_s32(int offset)
{
	void __iomem *addr = thermal_csram_base + offset;

	return sign_extend32(readl(addr), 31);
}

static int therm_intf_read_csram(int offset)
{
	void __iomem *addr = thermal_csram_base + offset;

	return readl(addr);
}

static void therm_intf_write_csram(unsigned int val, int offset)
{
	writel(val, (void __iomem *)(thermal_csram_base + offset));
}

static int therm_intf_read_cputcm_s32(int offset)
{
	void __iomem *addr_cputcm = thermal_cputcm_base + offset;

	return sign_extend32(readl(addr_cputcm), 31);
}

static int therm_intf_read_cputcm(int offset)
{
	void __iomem *addr_cputcm = thermal_cputcm_base + offset;

	return readl(addr_cputcm);
}

static void therm_intf_write_cputcm(unsigned int val, int offset)
{
	if (thermal_cputcm_base)
		writel(val, (void __iomem *)(thermal_cputcm_base + offset));
}


static int therm_intf_read_apu_mbox_s32(int offset)
{
	void __iomem *addr = thermal_apu_mbox_base + offset;

	return (!(thermal_apu_mbox_base) ? -1 : sign_extend32(readl(addr), 31));
}

static void therm_intf_write_apu_mbox(unsigned int val, int offset)
{
	if (thermal_apu_mbox_base)
		writel(val, (void __iomem *)(thermal_apu_mbox_base + offset));
}

int get_thermal_headroom(enum headroom_id id)
{
	int headroom = 0;

	if (!tm_data.sw_ready)
		return MAX_HEADROOM;

	if (id >= SOC_CPU0 && id < SOC_CPU0 + num_possible_cpus()) {
		if (tm_data.is_cputcm)
			headroom = therm_intf_read_cputcm_s32(CPU_HEADROOM_TCM_OFFSET + 4 * id);
		else
			headroom = therm_intf_read_csram_s32(CPU_HEADROOM_OFFSET + 4 * id);
	} else if (id == PCB_AP) {
		mutex_lock(&tm_data.lock);
		if (tm_data.is_cputcm)
			headroom = therm_intf_read_cputcm_s32(AP_NTC_HEADROOM_TCM_OFFSET);
		else
			headroom = therm_intf_read_csram_s32(AP_NTC_HEADROOM_OFFSET);

		mutex_unlock(&tm_data.lock);
	}

	return headroom;
}
EXPORT_SYMBOL(get_thermal_headroom);

int set_cpu_min_opp(int gear, int opp)
{
	if (!tm_data.sw_ready)
		return -ENODEV;

	if (gear >= tm_data.cpu_cluster_num)
		return -EINVAL;

	if (tm_data.is_cputcm)
		therm_intf_write_cputcm(opp, CPU_MIN_OPP_HINT_TCM_OFFSET + 4 * gear);
	else
		therm_intf_write_csram(opp, CPU_MIN_OPP_HINT_OFFSET + 4 * gear);

	return 0;
}
EXPORT_SYMBOL(set_cpu_min_opp);

int set_cpu_active_bitmask(int mask)
{
	if (!tm_data.sw_ready)
		return -ENODEV;

	if (tm_data.is_cputcm)
		therm_intf_write_cputcm(mask, CPU_ACTIVE_BITMASK_TCM_OFFSET);
	else
		therm_intf_write_csram(mask, CPU_ACTIVE_BITMASK_OFFSET);

	return 0;
}
EXPORT_SYMBOL(set_cpu_active_bitmask);

int get_cpu_temp(int cpu_id)
{
	int temp = 25000;

	if (!tm_data.sw_ready || cpu_id >= num_possible_cpus() ||
		cpu_id >= CPU_SENSOR_NUM)
		return temp;

	if (tm_data.is_cputcm)
		temp = therm_intf_read_cputcm_s32(CPU_TEMP_TCM_OFFSET + 4 * cpu_id);
	else
		temp = therm_intf_read_csram_s32(CPU_TEMP_OFFSET + 4 * cpu_id);

	if (temp != THERMAL_TEMP_INVALID)
		eas_previous_temp[cpu_id] = temp;
	else
		temp = eas_previous_temp[cpu_id];

	return temp;
}
EXPORT_SYMBOL(get_cpu_temp);

int get_dsu_temp(void)
{
	int temp = 25000;

	if (!tm_data.sw_ready)
		return temp;

	if (tm_data.is_cputcm)
		temp = therm_intf_read_cputcm_s32(DSU_AVG_TEMP_BASE_ADDR_TCM_OFFSET);
	else
		temp = therm_intf_read_csram_s32(DSU_AVG_TEMP_BASE_ADDR_OFFSET);

	if (temp != THERMAL_TEMP_INVALID)
		pre_temp = temp;
	else
		temp = pre_temp ;

	return temp;
}
EXPORT_SYMBOL(get_dsu_temp);


int get_dsu_ceiling_freq(void)
{
	int freq = -1; //KHz

	if (!tm_data.sw_ready)
		return freq;

	if (tm_data.is_cputcm)
		freq= therm_intf_read_cputcm_s32(DSU_CEILING_FREQ_TCM_OFFSET);
	else
		freq = therm_intf_read_csram_s32(DSU_CEILING_FREQ_OFFSET);

	return freq;
}
EXPORT_SYMBOL(get_dsu_ceiling_freq);

int get_cpu_ceiling_freq (int cluster_id)
{
	int freq = -1; //KHz

	if (!tm_data.sw_ready)
		return freq;

	if (tm_data.is_cputcm)
		freq = therm_intf_read_cputcm(CPU_LIMIT_FREQ_TCM_OFFSET +
				cluster_id * 4);
	else
		freq = therm_intf_read_csram_s32(CPU_LIMIT_FREQ_OFFSET +
				cluster_id * 4);
	return freq;
}
EXPORT_SYMBOL(get_cpu_ceiling_freq);

int set_reboot_temperature(int temp)
{
	if (!tm_data.sw_ready)
		return -ENODEV;

	therm_intf_write_csram(temp, REBOOT_TEMPERATURE_ADDR_OFFSET);

	return 0;
}
EXPORT_SYMBOL(set_reboot_temperature);

int set_cold_interrupt_enable_addr(int val)
{
	if (!tm_data.sw_ready)
		return -ENODEV;

	therm_intf_write_csram(val, COLD_INTERRUPT_ENABLE_OFFSET);

	return 0;
}
EXPORT_SYMBOL(set_cold_interrupt_enable_addr);

int set_gpu_pre_throttle(int temp, int index)
{
	int gpt_ctrl = 0;

	if (!tm_data.sw_ready)
		return -ENODEV;

	gpt_ctrl = therm_intf_read_csram_s32(GPU_PRE_THROTTLE_OFFSET);
	if (gpt_ctrl == 0)
		return 0;

	if (index == 1)
		therm_intf_write_csram(temp, GPU_PRE_THROTTLE_TEMP_OFFSET_1);
	else if (index == 2)
		therm_intf_write_csram(temp, GPU_PRE_THROTTLE_TEMP_OFFSET_2);

	return 0;
}
EXPORT_SYMBOL(set_gpu_pre_throttle);

int get_gpu_pre_throttle_temp(int index)
{
	int temp = 0xFFFF;

	if (!tm_data.sw_ready)
		return temp;
	if (index == 1)
		temp = therm_intf_read_csram_s32(GPU_PRE_THROTTLE_TEMP_OFFSET_1);
	else if (index == 2)
		temp = therm_intf_read_csram_s32(GPU_PRE_THROTTLE_TEMP_OFFSET_2);

	return temp;
}
EXPORT_SYMBOL(get_gpu_pre_throttle_temp);

int set_gpu_pre_throttle_opp(int opp, int index)
{
	int gpt_ctrl = 0;

	if (!tm_data.sw_ready)
		return -ENODEV;

	gpt_ctrl = therm_intf_read_csram_s32(GPU_PRE_THROTTLE_OFFSET);
	if (gpt_ctrl == 0)
		return 0;

	if (index == 1)
		therm_intf_write_csram(opp, GPU_PRE_THROTTLE_OPP_OFFSET_1);
	else if (index == 2)
		therm_intf_write_csram(opp, GPU_PRE_THROTTLE_OPP_OFFSET_2);

	return 0;
}
EXPORT_SYMBOL(set_gpu_pre_throttle_opp);

int get_gpu_pre_throttle_opp(int index)
{
	int opp = 0;

	if (!tm_data.sw_ready)
		return opp;
	if (index == 1)
		opp = therm_intf_read_csram_s32(GPU_PRE_THROTTLE_OPP_OFFSET_1);
	else if (index == 2)
		opp = therm_intf_read_csram_s32(GPU_PRE_THROTTLE_OPP_OFFSET_2);

	return opp;
}
EXPORT_SYMBOL(get_gpu_pre_throttle_opp);

static ssize_t headroom_info_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	int i;
	int len = 0;

	for (i = 0; i < NR_HEADROOM_ID; i++) {
		if (i == NR_HEADROOM_ID - 1)
			len += snprintf(buf + len, PAGE_SIZE - len, "%d\n",
				get_thermal_headroom((enum headroom_id)i));
		else
			len += snprintf(buf + len, PAGE_SIZE - len, "%d,",
				get_thermal_headroom((enum headroom_id)i));
	}

	return len;
}

static void write_ttj(int user, unsigned int cpu_ttj, unsigned int gpu_ttj,
	unsigned int apu_ttj)
{

	pr_info("%s %d %d\n", __func__, user, cpu_ttj);
	mutex_lock(&tm_data.lock);

	if (user == JATM_ON)
		tm_data.tj_info.jatm_on = 1;
	else if (user == JATM_OFF)
		tm_data.tj_info.jatm_on = 0;
	else if (user == CATM) {
		tm_data.tj_info.catm_cpu_ttj = cpu_ttj;
		tm_data.tj_info.catm_gpu_ttj = gpu_ttj;
		tm_data.tj_info.catm_apu_ttj = apu_ttj;
	}

	if (tm_data.tj_info.jatm_on == 1) {
		if (tm_data.is_cputcm)
			therm_intf_write_cputcm(cpu_ttj, TTJ_TCM_OFFSET);

		therm_intf_write_csram(cpu_ttj, TTJ_OFFSET);
		therm_intf_write_csram(gpu_ttj, TTJ_OFFSET + 4);
		therm_intf_write_csram(apu_ttj, TTJ_OFFSET + 8);
		therm_intf_write_apu_mbox(apu_ttj, APU_MBOX_TTJ_OFFSET);
	} else {
		if (tm_data.is_cputcm)
			therm_intf_write_cputcm(tm_data.tj_info.catm_cpu_ttj,
				TTJ_TCM_OFFSET);

		therm_intf_write_csram(tm_data.tj_info.catm_cpu_ttj,
			TTJ_OFFSET);
		therm_intf_write_csram(tm_data.tj_info.catm_gpu_ttj,
			TTJ_OFFSET + 4);
		therm_intf_write_csram(tm_data.tj_info.catm_apu_ttj,
			TTJ_OFFSET + 8);
		therm_intf_write_apu_mbox(tm_data.tj_info.catm_apu_ttj,
			APU_MBOX_TTJ_OFFSET);
	}
	mutex_unlock(&tm_data.lock);
}

static void write_max_ttj(unsigned int cpu_max_ttj,
	unsigned int gpu_max_ttj, unsigned int apu_max_ttj)
{
	tm_data.tj_info.cpu_max_ttj = cpu_max_ttj;
	tm_data.tj_info.gpu_max_ttj = gpu_max_ttj;
	tm_data.tj_info.apu_max_ttj = apu_max_ttj;
}

static void write_min_ttj(unsigned int min_ttj)
{
	tm_data.tj_info.min_ttj = min_ttj;
}

int get_catm_min_ttj(void)
{
	return tm_data.tj_info.min_ttj;
}
EXPORT_SYMBOL(get_catm_min_ttj);

void set_ttj(int user)
{
	write_ttj(user, tm_data.tj_info.cpu_max_ttj,
		tm_data.tj_info.gpu_max_ttj, tm_data.tj_info.apu_max_ttj);
}
EXPORT_SYMBOL(set_ttj);

void write_jatm_suspend(int jatm_suspend)
{
	if (tm_data.is_cputcm)
		therm_intf_write_cputcm(jatm_suspend, CPU_JATM_SUSPEND_TCM_OFFSET);
	else
		therm_intf_write_csram(jatm_suspend, CPU_JATM_SUSPEND_OFFSET);

	therm_intf_write_csram(jatm_suspend, GPU_JATM_SUSPEND_OFFSET);
}
EXPORT_SYMBOL(write_jatm_suspend);

int get_jatm_suspend(void)
{
	int cpu_jatm_suspend;
	int gpu_jatm_suspend;

	if (tm_data.is_cputcm)
		cpu_jatm_suspend = therm_intf_read_cputcm_s32(CPU_JATM_SUSPEND_TCM_OFFSET);
	else
		cpu_jatm_suspend = therm_intf_read_csram_s32(CPU_JATM_SUSPEND_OFFSET);

	gpu_jatm_suspend = therm_intf_read_csram_s32(GPU_JATM_SUSPEND_OFFSET);

	return (cpu_jatm_suspend || gpu_jatm_suspend);
}
EXPORT_SYMBOL(get_jatm_suspend);

int get_catm_ttj(void)
{
	int min_ttj = tm_data.tj_info.catm_cpu_ttj;

	if (min_ttj > tm_data.tj_info.catm_gpu_ttj)
		min_ttj = tm_data.tj_info.catm_gpu_ttj;
	if (min_ttj > tm_data.tj_info.catm_apu_ttj)
		min_ttj = tm_data.tj_info.catm_apu_ttj;

	return min_ttj;
}
EXPORT_SYMBOL(get_catm_ttj);

void (*cm_thermal_hint)(int) = NULL;

void cm_thermal_hint_register(ThermalHintFuncPtr func)
{
	cm_thermal_hint = func;
}
EXPORT_SYMBOL(cm_thermal_hint_register);

void cm_thermal_hint_unregister(void)
{
	cm_thermal_hint = NULL;
}
EXPORT_SYMBOL(cm_thermal_hint_unregister);


static ssize_t ttj_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	int len = 0;

	if (tm_data.is_cputcm) {
		len += snprintf(buf + len, PAGE_SIZE - len, "%u, %u, %u\n",
			therm_intf_read_cputcm_s32(TTJ_TCM_OFFSET),
			therm_intf_read_csram_s32(TTJ_OFFSET + 4),
			therm_intf_read_csram_s32(TTJ_OFFSET + 8));
	} else {
		len += snprintf(buf + len, PAGE_SIZE - len, "%u, %u, %u\n",
			therm_intf_read_csram_s32(TTJ_OFFSET),
			therm_intf_read_csram_s32(TTJ_OFFSET + 4),
			therm_intf_read_csram_s32(TTJ_OFFSET + 8));
	}
	return len;
}

static ssize_t ttj_store(struct kobject *kobj,
	struct kobj_attribute *attr, const char *buf, size_t count)
{
	char cmd[10];
	unsigned int cpu_ttj, gpu_ttj, apu_ttj;

	if (sscanf(buf, "%4s %u %u %u", cmd, &cpu_ttj, &gpu_ttj, &apu_ttj)
		== 4) {
		if (strncmp(cmd, "TTJ", 3) == 0) {
			write_ttj(CATM, cpu_ttj, gpu_ttj, apu_ttj);

			return count;
		}
	}

	pr_info("[thermal_ttj] invalid input\n");

	return -EINVAL;
}

static ssize_t max_ttj_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	int len = 0;

	len += snprintf(buf + len, PAGE_SIZE - len, "%u, %u, %u\n",
		tm_data.tj_info.cpu_max_ttj,
		tm_data.tj_info.gpu_max_ttj,
		tm_data.tj_info.apu_max_ttj);

	return len;
}

static ssize_t max_ttj_store(struct kobject *kobj,
	struct kobj_attribute *attr, const char *buf, size_t count)
{
	char cmd[10];
	unsigned int cpu_max_ttj, gpu_max_ttj, apu_max_ttj;

	if (sscanf(buf, "%8s %u %u %u", cmd, &cpu_max_ttj, &gpu_max_ttj, &apu_max_ttj)
		== 4) {
		if (strncmp(cmd, "MAX_TTJ", 7) == 0) {
			write_max_ttj(cpu_max_ttj, gpu_max_ttj, apu_max_ttj);

			return count;
		}
	}

	pr_info("[thermal_ttj] invalid input\n");

	return -EINVAL;
}

static ssize_t min_ttj_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	int len = 0;

	len += snprintf(buf + len, PAGE_SIZE - len, "%u\n",
		tm_data.tj_info.min_ttj);

	return len;
}

static ssize_t min_ttj_store(struct kobject *kobj,
	struct kobj_attribute *attr, const char *buf, size_t count)
{
	char cmd[10];
	unsigned int min_ttj;

	if (sscanf(buf, "%8s %u", cmd, &min_ttj)
		== 2) {
		if (strncmp(cmd, "MIN_TTJ", 7) == 0) {
			write_min_ttj(min_ttj);

			return count;
		}
	}

	pr_info("[thermal_ttj] invalid input\n");

	return -EINVAL;
}

static ssize_t min_throttle_freq_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	int len = 0;


	if (tm_data.is_cputcm) {
		len += snprintf(buf + len, PAGE_SIZE - len, "%d, %d, %d %d\n",
			therm_intf_read_cputcm_s32(MIN_THROTTLE_FREQ_TCM_OFFSET),
			therm_intf_read_cputcm_s32(MIN_THROTTLE_FREQ_TCM_OFFSET + 4),
			therm_intf_read_cputcm_s32(MIN_THROTTLE_FREQ_TCM_OFFSET + 8),
			therm_intf_read_csram_s32(MIN_THROTTLE_FREQ_OFFSET + 12));
	} else {
		len += snprintf(buf + len, PAGE_SIZE - len, "%d, %d, %d %d\n",
			therm_intf_read_csram_s32(MIN_THROTTLE_FREQ_OFFSET),
			therm_intf_read_csram_s32(MIN_THROTTLE_FREQ_OFFSET + 4),
			therm_intf_read_csram_s32(MIN_THROTTLE_FREQ_OFFSET + 8),
			therm_intf_read_csram_s32(MIN_THROTTLE_FREQ_OFFSET + 12));
	}

	return len;
}

static ssize_t min_throttle_freq_store(struct kobject *kobj,
	struct kobj_attribute *attr, const char *buf, size_t count)
{
	char cmd[10];
	int cluster0_min_freq;
	int cluster1_min_freq;
	int cluster2_min_freq;
	int gpu_min_freq;

	if (sscanf(buf, "%9s %d %d %d %d", cmd,
		&cluster0_min_freq,
		&cluster1_min_freq,
		&cluster2_min_freq,
		&gpu_min_freq)
		== 5) {

		if (strncmp(cmd, "MIN_FREQ", 8) == 0) {
			if (tm_data.is_cputcm) {
				therm_intf_write_cputcm(cluster0_min_freq,
					MIN_THROTTLE_FREQ_TCM_OFFSET);
				therm_intf_write_cputcm(cluster1_min_freq,
					MIN_THROTTLE_FREQ_TCM_OFFSET + 4);
				therm_intf_write_cputcm(cluster2_min_freq,
					MIN_THROTTLE_FREQ_TCM_OFFSET + 8);
			} else {
				therm_intf_write_csram(cluster0_min_freq,
					MIN_THROTTLE_FREQ_OFFSET);
				therm_intf_write_csram(cluster1_min_freq,
					MIN_THROTTLE_FREQ_OFFSET + 4);
				therm_intf_write_csram(cluster2_min_freq,
					MIN_THROTTLE_FREQ_OFFSET + 8);
			}
			therm_intf_write_csram(gpu_min_freq,
				MIN_THROTTLE_FREQ_OFFSET + 12);

			return count;
		}
	}

	pr_info("[min_throttle_freq] invalid input\n");

	return -EINVAL;
}

static void write_power_budget(unsigned int cpu_pb, unsigned int gpu_pb,
	unsigned int apu_pb)
{

	if (tm_data.is_cputcm)
		therm_intf_write_csram(cpu_pb, POWER_BUDGET_TCM_OFFSET);

	therm_intf_write_csram(cpu_pb, POWER_BUDGET_OFFSET);
	therm_intf_write_csram(gpu_pb, POWER_BUDGET_OFFSET + 4);
	therm_intf_write_csram(apu_pb, POWER_BUDGET_OFFSET + 8);

	therm_intf_write_apu_mbox(apu_pb, APU_MBOX_PB_OFFSET);
}

static ssize_t power_budget_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	int len = 0;

	if (tm_data.is_cputcm)
		len += snprintf(buf + len, PAGE_SIZE - len, "%u, %u, %u\n",
			therm_intf_read_cputcm_s32(POWER_BUDGET_TCM_OFFSET),
			therm_intf_read_csram_s32(POWER_BUDGET_OFFSET + 4),
			therm_intf_read_csram_s32(POWER_BUDGET_OFFSET + 8));
	else
		len += snprintf(buf + len, PAGE_SIZE - len, "%u, %u, %u\n",
			therm_intf_read_csram_s32(POWER_BUDGET_OFFSET),
			therm_intf_read_csram_s32(POWER_BUDGET_OFFSET + 4),
			therm_intf_read_csram_s32(POWER_BUDGET_OFFSET + 8));

	return len;
}

static ssize_t power_budget_store(struct kobject *kobj,
	struct kobj_attribute *attr, const char *buf, size_t count)
{
	char cmd[10];
	unsigned int cpu_pb, gpu_pb, apu_pb;

	if (sscanf(buf, "%3s %u %u %u", cmd, &cpu_pb, &gpu_pb, &apu_pb) == 4) {
		if (strncmp(cmd, "pb", 2) == 0) {
			write_power_budget(cpu_pb, gpu_pb, apu_pb);
			return count;
		}
	}

	pr_info("[thermal_power_budget] invalid input\n");

	return -EINVAL;
}

static ssize_t cpu_info_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	int len = 0;


	if (tm_data.is_cputcm)
		len += snprintf(buf + len, PAGE_SIZE - len, "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n",
			therm_intf_read_cputcm_s32(CPU_MIN_OPP_HINT_TCM_OFFSET),
			therm_intf_read_cputcm_s32(CPU_MIN_OPP_HINT_TCM_OFFSET + 4),
			therm_intf_read_cputcm_s32(CPU_MIN_OPP_HINT_TCM_OFFSET + 8),
			therm_intf_read_cputcm(CPU_LIMIT_FREQ_TCM_OFFSET),
			therm_intf_read_cputcm(CPU_LIMIT_FREQ_TCM_OFFSET + 4),
			therm_intf_read_cputcm(CPU_LIMIT_FREQ_TCM_OFFSET + 8),
			therm_intf_read_cputcm(CPU_CUR_FREQ_TCM_OFFSET),
			therm_intf_read_cputcm(CPU_CUR_FREQ_TCM_OFFSET + 4),
			therm_intf_read_cputcm(CPU_CUR_FREQ_TCM_OFFSET + 8),
			therm_intf_read_cputcm_s32(CPU_MAX_TEMP_TCM_OFFSET),
			therm_intf_read_cputcm_s32(CPU_MAX_TEMP_TCM_OFFSET + 4),
			therm_intf_read_cputcm_s32(CPU_MAX_TEMP_TCM_OFFSET + 8),
			therm_intf_read_cputcm(CPU_LIMIT_ARBITER_TCM_OFFSET),
			therm_intf_read_cputcm(CPU_LIMIT_ARBITER_TCM_OFFSET + 4),
			therm_intf_read_cputcm(CPU_LIMIT_ARBITER_TCM_OFFSET + 8));
	else
		len += snprintf(buf + len, PAGE_SIZE - len, "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n",
			therm_intf_read_csram_s32(CPU_MIN_OPP_HINT_OFFSET),
			therm_intf_read_csram_s32(CPU_MIN_OPP_HINT_OFFSET + 4),
			therm_intf_read_csram_s32(CPU_MIN_OPP_HINT_OFFSET + 8),
			therm_intf_read_csram(CPU_LIMIT_FREQ_OFFSET),
			therm_intf_read_csram(CPU_LIMIT_FREQ_OFFSET + 4),
			therm_intf_read_csram(CPU_LIMIT_FREQ_OFFSET + 8),
			therm_intf_read_csram(CPU_CUR_FREQ_OFFSET),
			therm_intf_read_csram(CPU_CUR_FREQ_OFFSET + 4),
			therm_intf_read_csram(CPU_CUR_FREQ_OFFSET + 8),
			therm_intf_read_csram_s32(CPU_MAX_TEMP_OFFSET),
			therm_intf_read_csram_s32(CPU_MAX_TEMP_OFFSET + 4),
			therm_intf_read_csram_s32(CPU_MAX_TEMP_OFFSET + 8));

	return len;
}

static ssize_t cpu_temp_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	int len = 0;
	int cpu_id = 0;

	for (cpu_id = 0; cpu_id < num_possible_cpus(); cpu_id++) {
		if (cpu_id == num_possible_cpus() - 1)
			len += snprintf(buf + len, PAGE_SIZE - len, "%d\n",
				get_cpu_temp(cpu_id));
		else
			len += snprintf(buf + len, PAGE_SIZE - len, "%d,",
				get_cpu_temp(cpu_id));
	}

	return len;
}

static ssize_t gpu_info_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	int len = 0;

	len += snprintf(buf + len, PAGE_SIZE - len, "%d,%d,%d\n",
		therm_intf_read_csram_s32(GPU_TEMP_OFFSET),
		therm_intf_read_csram(GPU_TEMP_OFFSET + 4),
		therm_intf_read_csram(GPU_TEMP_OFFSET + 8));

	return len;
}

static ssize_t gpu_freq_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	int len = 0;

	len += snprintf(buf + len, PAGE_SIZE - len, "%d,%d,%d,%d,%d,%d\n",
		therm_intf_read_csram_s32(GPU_ST0_FREQ_OFFSET),
		therm_intf_read_csram_s32(GPU_ST0_FREQ_OFFSET + 4),
		therm_intf_read_csram_s32(GPU_ST0_FREQ_OFFSET + 8),
		therm_intf_read_csram_s32(GPU_ST0_FREQ_OFFSET + 12),
		therm_intf_read_csram_s32(GPU_ST0_FREQ_OFFSET + 16),
		therm_intf_read_csram_s32(GPU_ST0_FREQ_OFFSET + 20));

	return len;
}

static ssize_t apu_info_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	int len = 0;

	if (thermal_apu_mbox_base) {
		len += snprintf(buf + len, PAGE_SIZE - len, "%d,%d,%d\n",
			therm_intf_read_apu_mbox_s32(APU_MBOX_TEMP_OFFSET),
			therm_intf_read_apu_mbox_s32(APU_MBOX_LIMIT_OPP_OFFSET),
			therm_intf_read_apu_mbox_s32(APU_MBOX_CUR_OPP_OFFSET));
	} else {
		len += snprintf(buf + len, PAGE_SIZE - len, "%d,%d,%d\n",
			therm_intf_read_csram_s32(APU_TEMP_OFFSET),
			therm_intf_read_csram_s32(APU_TEMP_OFFSET + 4),
			therm_intf_read_csram_s32(APU_TEMP_OFFSET + 8));
	}

	return len;
}

static ssize_t is_cpu_limit_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	int len = 0, i, is_limit = 0, limit_opp;

	for (i = 0; i < tm_data.cpu_cluster_num; i++) {
		if (tm_data.is_cputcm)
			limit_opp =
				therm_intf_read_cputcm_s32(CPU_LIMIT_OPP_TCM_OFFSET + 4 * i);
		else
			limit_opp =
				therm_intf_read_csram_s32(CPU_LIMIT_OPP_OFFSET + 4 * i);

		if (is_opp_limited(limit_opp)) {
			is_limit = 1;
			break;
		}
	}

	len += snprintf(buf + len, PAGE_SIZE - len, "%d\n", is_limit);

	return len;
}

static ssize_t is_gpu_limit_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	int len = 0;
#if IS_ENABLED(CONFIG_MTK_GPUFREQ_V2)
	int limit_freq, max_freq;

	limit_freq = therm_intf_read_csram(GPU_TEMP_OFFSET + 4);
	max_freq = gpufreq_get_freq_by_idx(TARGET_DEFAULT, 0);
	len += snprintf(buf + len, PAGE_SIZE - len, "%d\n", (limit_freq < max_freq) ? 1 : 0);
#else
	len += snprintf(buf + len, PAGE_SIZE - len, "%d\n", 0);
#endif

	return len;
}

static ssize_t is_apu_limit_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	int len = 0, limit_opp;

	if (thermal_apu_mbox_base)
		limit_opp = therm_intf_read_apu_mbox_s32(APU_MBOX_LIMIT_OPP_OFFSET);
	else
		limit_opp = therm_intf_read_csram_s32(APU_TEMP_OFFSET + 4);

	len += snprintf(buf + len, PAGE_SIZE - len, "%d\n", (is_opp_limited(limit_opp)) ? 1 : 0);

	return len;
}

static ssize_t frs_info_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	int len = 0;

	len += snprintf(buf + len, PAGE_SIZE - len, "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n",
		frs_data.enable,
		frs_data.activated, frs_data.pid,
		frs_data.target_fps, frs_data.diff,
		frs_data.tpcb, frs_data.tpcb_slope,
		frs_data.ap_headroom, frs_data.n_sec_to_ttpcb,
		frs_data.frs_target_fps, frs_data.real_fps,
		frs_data.target_tpcb, frs_data.ptime);

	return len;
}

static ssize_t frs_info_store(struct kobject *kobj,
	struct kobj_attribute *attr, const char *buf, size_t count)
{
	int enable, act, target_fps, tpcb, tpcb_slope;
	int ap_headroom, n_sec_to_ttpcb;
	int pid, diff;
	int frs_target_fps, real_fps, target_tpcb, ptime;

	if (sscanf(buf, "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d", &enable, &act, &pid,
		&target_fps, &diff, &tpcb, &tpcb_slope, &ap_headroom, &n_sec_to_ttpcb,
		&frs_target_fps, &real_fps, &target_tpcb, &ptime) == 13) {
		if ((ap_headroom >= -1000) && (ap_headroom <= 1000)) {
			if (tm_data.is_cputcm)
				therm_intf_write_cputcm(ap_headroom, AP_NTC_HEADROOM_TCM_OFFSET);

			therm_intf_write_csram(ap_headroom, AP_NTC_HEADROOM_OFFSET);
			frs_data.ap_headroom = ap_headroom;
		} else {
			pr_info("[%s] invalid ap head room input\n", __func__);
			return -EINVAL;
		}

		if (tm_data.is_cputcm)
			therm_intf_write_cputcm(tpcb, TPCB_TCM_OFFSET);

		therm_intf_write_csram(tpcb, TPCB_OFFSET);

		frs_data.enable = enable;
		frs_data.activated = act;
		frs_data.tpcb = tpcb;
		frs_data.pid = pid;
		frs_data.target_fps = target_fps;
		frs_data.diff = diff;
		frs_data.tpcb_slope = tpcb_slope;
		frs_data.n_sec_to_ttpcb = n_sec_to_ttpcb;
		frs_data.frs_target_fps = frs_target_fps;
		frs_data.real_fps = real_fps;
		frs_data.target_tpcb = target_tpcb;
		frs_data.ptime = ptime;
	} else {
		pr_info("[%s] invalid input\n", __func__);
		return -EINVAL;
	}

	return count;
}

static ssize_t cpu_atc_show(struct kobject *kobj, struct kobj_attribute *attr,
	char *buf)
{
	int i, len = 0, val;

	for (i = 0; i < CPU_ATC_NUM - 1; i++) {
		val = therm_intf_read_csram_s32(CPU_ATC_OFFSET + i * 0x4);
		len += snprintf(buf + len, PAGE_SIZE - len, "%d,", val);
	}

	val = therm_intf_read_csram_s32(CPU_ATC_OFFSET + i * 0x4);
	len += snprintf(buf + len, PAGE_SIZE - len, "%d\n", val);

	return len;
}

static ssize_t gpu_atc_show(struct kobject *kobj, struct kobj_attribute *attr,
	char *buf)
{
	int i, len = 0, val;

	for (i = 0; i < GPU_ATC_NUM - 1; i++) {
		val = therm_intf_read_csram_s32(GPU_ATC_OFFSET + i * 0x4);
		len += snprintf(buf + len, PAGE_SIZE - len, "%d,", val);
	}

	val = therm_intf_read_csram_s32(GPU_ATC_OFFSET + i * 0x4);
	len += snprintf(buf + len, PAGE_SIZE - len, "%d\n", val);

	return len;
}

static ssize_t apu_atc_show(struct kobject *kobj, struct kobj_attribute *attr,
	char *buf)
{
	int len = 0, val, i;

	if (thermal_apu_mbox_base) {
		val =  therm_intf_read_apu_mbox_s32(APU_MBOX_ATC_MAX_TTJ_ADDR);
		len += snprintf(buf + len, PAGE_SIZE - len, "%d\n", val);
	} else {
		for (i = 0; i < APU_ATC_NUM - 1; i++) {
			val = therm_intf_read_csram_s32(APU_ATC_OFFSET + i * 0x4);
			len += snprintf(buf + len, PAGE_SIZE - len, "%d,", val);
		}
		val = therm_intf_read_csram_s32(APU_ATC_OFFSET + i * 0x4);
		len += snprintf(buf + len, PAGE_SIZE - len, "%d\n", val);
	}

	return len;
}



static ssize_t target_tpcb_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	int len = 0;
	int target_tpcb = 0;

	if (tm_data.is_cputcm)
		target_tpcb = therm_intf_read_cputcm_s32(TARGET_TPCB_TCM_OFFSET);
	else
		target_tpcb = therm_intf_read_csram_s32(TARGET_TPCB_OFFSET);

	len += snprintf(buf + len, PAGE_SIZE - len, "%d\n", target_tpcb);

	return len;
}

static ssize_t target_tpcb_store(struct kobject *kobj,
	struct kobj_attribute *attr, const char *buf, size_t count)
{
	int target_tpcb = 0;

	if (kstrtoint(buf, 10, &target_tpcb) == 0) {
		if (tm_data.is_cputcm)
			therm_intf_write_cputcm(target_tpcb, TARGET_TPCB_TCM_OFFSET);

		therm_intf_write_csram(target_tpcb, TARGET_TPCB_OFFSET);
	}
	else {
		pr_info("[%s] invalid input\n", __func__);
		return -EINVAL;
	}

	return count;
}

static ssize_t md_sensor_info_store(struct kobject *kobj,
	struct kobj_attribute *attr, const char *buf, size_t count)
{
	char info_type_s[MAX_MD_NAME_LENGTH +1];
	int len = 0, num = 0, val = 0, i = 0;
	struct md_thermal_sensor_t *ts_info;

	if (sscanf(buf, "%20s %d%n", info_type_s, &num, &len) != 2) {
		pr_info("%s: wrong scan info_type and num %s\n", __func__, buf);
		return -EINVAL;
	}

	if (strncmp(info_type_s, "s_tmp", 5) != 0) {
		pr_info("%s: wrong info type=%s\n", __func__, info_type_s);
		return -EINVAL;
	}

	if (num <= 0) {
		pr_info("%s: wrong input num=%d\n", __func__, num);
		return -EINVAL;
	}

	buf += len;

	if (!md_info_data.sensor_info) {
		ts_info = devm_kcalloc(tm_data.dev, num,
			sizeof(struct md_thermal_sensor_t), GFP_KERNEL);
		if (!ts_info)
			return -ENOMEM;

		md_info_data.sensor_info = ts_info;
		md_info_data.sensor_num = num;
	} else if (md_info_data.sensor_num != num) {
		pr_info("%s: wrong sensor num=%d %d\n", __func__, md_info_data.sensor_num, num);
		return -EINVAL;
	}

	ts_info = md_info_data.sensor_info;
	for (i = 0; i < md_info_data.sensor_num; i++) {
		if (sscanf(buf, " %d%n", &val, &len) == 1) {
			buf += len;
			ts_info[i].cur_temp = val;
		}
	}

	return count;
}

static ssize_t md_sensor_info_show(struct kobject *kobj, struct kobj_attribute *attr,
	char *buf)
{
	int len = 0, i;
	struct md_thermal_sensor_t *ts_info;

	len += snprintf(buf + len, PAGE_SIZE - len, "%d", md_info_data.sensor_num);

	if (!md_info_data.sensor_info) {
		len += snprintf(buf + len, PAGE_SIZE - len, "\n");
		return len;
	}

	ts_info = md_info_data.sensor_info;
	for (i = 0; i < md_info_data.sensor_num; i++)
		len += snprintf(buf + len, PAGE_SIZE - len, ",%d", ts_info[i].cur_temp);

	len += snprintf(buf + len, PAGE_SIZE - len, "\n");

	return len;
}

static ssize_t md_actuator_info_store(struct kobject *kobj,
	struct kobj_attribute *attr, const char *buf, size_t count)
{
	char info_type_s[MAX_MD_NAME_LENGTH + 1];
	int len = 0, num = 0, val = 0, i = 0;
	struct md_thermal_actuator_t *ta_info;

	if (sscanf(buf, "%20s %d%n", info_type_s, &num, &len) != 2) {
		pr_info("%s: wrong scan info_type and num %s\n", __func__, buf);
		return -EINVAL;
	}

	if (strncmp(info_type_s, "a_ctl", 5) == 0) {
		md_info_data.md_autonomous_ctrl = num;
		return count;
	}

	if (strncmp(info_type_s, "a_cst", 5) != 0 &&
		strncmp(info_type_s, "a_mst", 5) != 0) {
		pr_info("%s: wrong info type=%s\n", __func__, info_type_s);
		return -EINVAL;
	}

	if (num <= 0) {
		pr_info("%s: wrong input num=%d\n", __func__, num);
		return -EINVAL;
	}

	buf += len;

	if (!md_info_data.actuator_info) {
		ta_info = devm_kcalloc(tm_data.dev, num,
			sizeof(struct md_thermal_actuator_t), GFP_KERNEL);
		if (!ta_info)
			return -ENOMEM;

		md_info_data.actuator_info = ta_info;
		md_info_data.actuator_num = num;
	} else if (md_info_data.actuator_num != num) {
		pr_info("%s: wrong actuator num=%d %d\n", __func__,
			md_info_data.actuator_num, num);
		return -EINVAL;
	}

	ta_info = md_info_data.actuator_info;
	if (strncmp(info_type_s, "a_cst", 5) == 0) {
		for (i = 0; i < md_info_data.actuator_num; i++) {
			if (sscanf(buf, " %d%n", &val, &len) == 1) {
				buf += len;
				ta_info[i].cur_status = val;
			}
		}
	} else {
		for (i = 0; i < md_info_data.actuator_num; i++) {
			if (sscanf(buf, " %d%n", &val, &len) == 1) {
				buf += len;
				ta_info[i].max_status = val;
			}
		}
	}

	return count;
}

static ssize_t md_actuator_info_show(struct kobject *kobj, struct kobj_attribute *attr,
	char *buf)
{
	int len = 0, i;
	struct md_thermal_actuator_t *ta_info;

	len += snprintf(buf + len, PAGE_SIZE - len, "%d", md_info_data.md_autonomous_ctrl);
	len += snprintf(buf + len, PAGE_SIZE - len, ",%d", md_info_data.actuator_num);

	if (!md_info_data.actuator_info) {
		len += snprintf(buf + len, PAGE_SIZE - len, "\n");
		return len;
	}

	ta_info = md_info_data.actuator_info;
	for (i = 0; i < md_info_data.actuator_num; i++)
		len += snprintf(buf + len, PAGE_SIZE - len, ",%d,%d",
			ta_info[i].cur_status, ta_info[i].max_status);

	len += snprintf(buf + len, PAGE_SIZE - len, "\n");

	return len;
}

static ssize_t info_b_show(struct kobject *kobj, struct kobj_attribute *attr,
	char *buf)
{
	int len = 0, val;

	val = therm_intf_read_csram_s32(INFOB_OFFSET);
	len += snprintf(buf + len, PAGE_SIZE - len, "%d\n", val);

	return len;
}

static ssize_t utc_count_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	int len = 0;

	if (tm_data.is_cputcm)
		len += snprintf(buf + len, PAGE_SIZE - len, "%d\n",
			therm_intf_read_cputcm_s32(UTC_COUNT_TCM_OFFSET));
	else
		len += snprintf(buf + len, PAGE_SIZE - len, "%d\n",
			therm_intf_read_csram_s32(UTC_COUNT_OFFSET));

	return len;
}

static ssize_t dsu_ceiling_freq_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	int len = 0;

	if (tm_data.is_cputcm)
		len += snprintf(buf + len, PAGE_SIZE - len, "%d\n",
			therm_intf_read_cputcm_s32(DSU_CEILING_FREQ_TCM_OFFSET));
	else
		len += snprintf(buf + len, PAGE_SIZE - len, "%d\n",
			therm_intf_read_csram_s32(DSU_CEILING_FREQ_OFFSET));

	return len;
}

static ssize_t sports_mode_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	int len = 0;
	int enable = therm_intf_read_csram_s32(SPORTS_MODE_ENABLE);

	len += snprintf(buf + len, PAGE_SIZE - len, "%d\n", enable);

	return len;
}

static ssize_t sports_mode_store(struct kobject *kobj,
	struct kobj_attribute *attr, const char *buf, size_t count)
{
	int enable = 0;

	if (!kstrtoint(buf, 10, &enable)) {
		therm_intf_write_csram(enable, SPORTS_MODE_ENABLE);
		if (tm_data.is_cputcm) {
			therm_intf_write_cputcm(enable, SPORTS_MODE_TCM_ENABLE);
			pr_info("%s: set tcm sport mode\n", __func__);
		}
	}
	else {
		pr_info("%s: invalid input\n", __func__);
		return -EINVAL;
	}

	return count;
}

static ssize_t cg_policy_mode_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	int len = 0;
	int enable = therm_intf_read_csram_s32(CG_POLICY_MODE_OFFSET);

	len += snprintf(buf + len, PAGE_SIZE - len, "%d\n", enable);

	return len;
}

static ssize_t cg_policy_mode_store(struct kobject *kobj,
	struct kobj_attribute *attr, const char *buf, size_t count)
{
	int enable = 0;

	if (!kstrtoint(buf, 10, &enable)) {
		therm_intf_write_csram(enable, CG_POLICY_MODE_OFFSET);
		if (tm_data.is_cputcm) {
			therm_intf_write_cputcm(enable, CG_POLICY_MODE_TCM_OFFSET);
			pr_info("%s: set cg policy mode\n", __func__);
		}
	} else {
		pr_info("%s: invalid input\n", __func__);
		return -EINVAL;
	}

	return count;
}


static ssize_t vtskin_info_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	int len = 0, i, j;
	struct vtskin_tz_param *param;

	if (!plat_vtskin_info) {
		len += snprintf(buf + len, PAGE_SIZE - len, "vtskin info NULL\n");
		return len;
	}

	len += snprintf(buf + len, PAGE_SIZE - len, "vtskin sensor total num=%d\n",
			plat_vtskin_info->num_sensor);

	for (i = 0; i < plat_vtskin_info->num_sensor; i++) {
		param = &plat_vtskin_info->params[i];
		len += snprintf(buf + len, PAGE_SIZE - len,
			"   vtskin id=%d, name=%s, ref_num=%d op=%d\n",
			i, param->tz_name, param->ref_num, param->operation);
		for (j = 0; j < param->ref_num; j++) {
			len += snprintf(buf + len, PAGE_SIZE - len,
				"      %20s, %10lld\n", param->vtskin_ref[j].sensor_name,
				param->vtskin_ref[j].sensor_coef);
		}
		len += snprintf(buf + len, PAGE_SIZE - len, "\n");
	}

	return len;
}

static ssize_t vtskin_info_store(struct kobject *kobj,
	struct kobj_attribute *attr, const char *buf, size_t count)
{
	int len = 0, ref_num = 0, op;
	char skin_name[THERMAL_NAME_LENGTH + 1], ref_name[THERMAL_NAME_LENGTH + 1];
	long long ref_coef;
	unsigned int skin_id, i;
	struct thermal_zone_device *tzd[MAX_VTSKIN_REF_NUM];
	struct vtskin_tz_param *param;
	struct vtskin_coef coef[MAX_VTSKIN_REF_NUM];

	if (!plat_vtskin_info) {
		pr_info("plat_vtskin_info is NULL\n");
		return -ENODATA;
	}

	if (sscanf(buf, "%20s %d %d%n", skin_name, &op, &ref_num, &len) != 3) {
		pr_info("wrong skin_name, op, or ref_num %s\n", buf);
		return -EINVAL;
	}
	buf += len;

	for (skin_id = 0; skin_id < plat_vtskin_info->num_sensor; skin_id++) {
		if (!strcmp(skin_name, plat_vtskin_info->params[skin_id].tz_name))
			break;
	}

	if (skin_id >= plat_vtskin_info->num_sensor) {
		pr_info("skin name:%s not match any actual vtskin name\n", skin_name);
		return -EINVAL;
	}

	if (op < 0 || op >= OP_NUM) {
		pr_info("wrong operation %d\n", op);
		return -EINVAL;
	}

	if (ref_num < 0 || ref_num > MAX_VTSKIN_REF_NUM) {
		pr_info("wrong ref number %d\n", ref_num);
		return -EINVAL;
	}

	memset(&coef[0], 0, sizeof(struct vtskin_coef) * MAX_VTSKIN_REF_NUM);
	memset(tzd, 0, sizeof(tzd));

	for (i = 0; i < ref_num; i++) {
		if (sscanf(buf, "%20s %lld%n", ref_name, &ref_coef, &len) != 2) {
			pr_info("wrong scan vtskin ref sensor:%s\n", buf);
			return -EINVAL;
		}
		buf += len;

		tzd[i] = thermal_zone_get_zone_by_name(ref_name);
		if (IS_ERR_OR_NULL(tzd[i])) {
			pr_info("get %s for thermal zone fail\n", ref_name);
			return -EINVAL;
		}

		strncpy(coef[i].sensor_name, ref_name, strlen(ref_name));
		coef[i].sensor_coef = ref_coef;
		coef[i].record_temp = THERMAL_TEMP_INVALID;
		coef[i].record_time = 0;
	}

	param = &plat_vtskin_info->params[skin_id];
	param->operation = op;
	param->ref_num = (unsigned int)ref_num;
	memcpy(&param->vtskin_ref[0], &coef[0], sizeof(struct vtskin_coef) * MAX_VTSKIN_REF_NUM);
	for (i = 0; i < MAX_VTSKIN_REF_NUM; i++)
		param->tzd[i] = tzd[i];

	return count;
}

static ssize_t vtskin_temp_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	int len = 0;

	len = snprintf(buf, PAGE_SIZE, "%d\n", therm_intf_read_csram_s32(VTSKIN));
	if (len < 0 || len >= sizeof(buf))
		pr_info("%s: snprintf return negative and buf %s\n", __func__, buf);

	return len;
}

static ssize_t vtskin_temp_store(struct kobject *kobj,
	struct kobj_attribute *attr, const char *buf, size_t count)
{
	char cmd[10];
	int temp;

	if (sscanf(buf, "%7s %d", cmd, &temp) == 2) {
		if (strncmp(cmd, "VTSKIN", 6) == 0) {
			therm_intf_write_csram(temp, VTSKIN);

			return count;
		}
	}

	pr_info("[thermal_vtskin_temp] invalid input\n");

	return -EINVAL;
}

static int catm_p[3];

static ssize_t catm_p_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	int len = 0;

	len += snprintf(buf + len, PAGE_SIZE - len, "%d, %d, %d\n",
		catm_p[0],
		catm_p[1],
		catm_p[2]);

	return len;
}

static ssize_t catm_p_store(struct kobject *kobj,
	struct kobj_attribute *attr, const char *buf, size_t count)
{
	char cmd[10];
	int p0, p1, p2;

	if (sscanf(buf, "%5s %d %d %d", cmd, &p0, &p1, &p2)
		== 4) {
		if (strncmp(cmd, "CATM", 4) == 0) {
			catm_p[0] = p0;
			catm_p[1] = p1;
			catm_p[2] = p2;

			return count;
		}
	}

	pr_info("[thermal_ttj] invalid input\n");

	return -EINVAL;
}

static ssize_t dram_data_rate_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	int len = 0;
#if IS_ENABLED(CONFIG_MTK_DRAMC)
	len = snprintf(buf, PAGE_SIZE, "%d\n",
		mtk_dramc_get_data_rate());
	if (len < 0 || len >= sizeof(buf))
		pr_info("%s: snprintf return negative and buf %s\n", __func__, buf);
#endif
	return len;
}

static ssize_t pid_info_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	int len = 0, i;
	struct pid_term_info *pid_data;

	mutex_lock(&pid_info_lock);

	if (pid_info_data.pid_num <= 0) {
		len += snprintf(buf + len, PAGE_SIZE - len, "\n");
		mutex_unlock(&pid_info_lock);
		return len;
	}

	pid_data = pid_info_data.pid_term_data;
	for (i = 0; i < pid_info_data.pid_num; i++) {
		if (i > 0)
			len += snprintf(buf + len, PAGE_SIZE - len, ",");

		len += snprintf(buf + len, PAGE_SIZE - len, "%d,%d,%d,%d",
			pid_data[i].limit_state,
			pid_data[i].p,
			pid_data[i].i,
			pid_data[i].d);
	}

	len += snprintf(buf + len, PAGE_SIZE - len, "\n");

	mutex_unlock(&pid_info_lock);
	return len;
}

static ssize_t pid_info_store(struct kobject *kobj,
	struct kobj_attribute *attr, const char *buf, size_t count)
{
	char cmd[4];
	int num = 0, len = 0, i, state, p_term, i_term, d_term;
	struct pid_term_info *pid_data;

	if (sscanf(buf, "%d %3s%n", &num, cmd, &len) != 2) {
		pr_info("%s: wrong scan info type and num %s\n", __func__, buf);
		return -EINVAL;
	}

	if (strncmp(cmd, "PID", 3) != 0) {
		pr_info("%s: wrong info type=%s\n", __func__, cmd);
		return -EINVAL;
	}

	mutex_lock(&pid_info_lock);

	pid_data = pid_info_data.pid_term_data;
	if (pid_info_data.pid_num != num && pid_data != NULL) {
		devm_kfree(tm_data.dev, pid_data);
		pid_data = NULL;
	}

	if (!pid_data) {
		pid_data = devm_kcalloc(tm_data.dev, num,
			sizeof(struct pid_term_info), GFP_KERNEL);
		if (!pid_data) {
			mutex_unlock(&pid_info_lock);
			return -ENOMEM;
		}

		pid_info_data.pid_term_data = pid_data;
		pid_info_data.pid_num = num;
	}

	buf += len;

	pid_data = pid_info_data.pid_term_data;
	for (i = 0; i < pid_info_data.pid_num; i++) {
		if (sscanf(buf, " %d %d %d %d%n", &state, &p_term, &i_term, &d_term, &len) == 4) {
			buf += len;
			pid_data[i].limit_state = state;
			pid_data[i].p = p_term;
			pid_data[i].i = i_term;
			pid_data[i].d = d_term;
		} else {
			pr_info("%s: wrong scan info type and num %s\n", __func__, buf);
			mutex_unlock(&pid_info_lock);
			return -EINVAL;
		}
	}

	mutex_unlock(&pid_info_lock);
	return count;
}

static ssize_t bat_type_show(struct kobject *kobj, struct kobj_attribute *attr,
	char *buf)
{
	int len = 0;

	len += snprintf(buf + len, PAGE_SIZE - len, "%u\n", bat_type);

	return len;
}

static ssize_t user_vsensor_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	int i, len = 0;

	for (i = 0; i < USER_VSENSOR_NUM; i++) {
		if (i == USER_VSENSOR_NUM - 1) {
			len += snprintf(buf + len, PAGE_SIZE - len, "%d, %s",
				u_vsensor[i].temp,
				u_vsensor[i].user_vsensor_name);
		} else {
			len += snprintf(buf + len, PAGE_SIZE - len, "%d, %s, ",
				u_vsensor[i].temp,
				u_vsensor[i].user_vsensor_name);
		}
	}

	return len;
}

static ssize_t user_vsensor_store(struct kobject *kobj,
	struct kobj_attribute *attr, const char *buf, size_t count)
{
	char cmd[10];
	int temp = THERMAL_TEMP_INVALID;
	char name[32];
	int id, len;

	if (sscanf(buf, "%9s %d %d %s", cmd, &id, &temp, name)
		== 4) {
		if (strncmp(cmd, "U_VSENSOR", 9) == 0) {
			if (id >= 0 && id < USER_VSENSOR_NUM) {
				u_vsensor[id].temp = temp;
				len = snprintf(u_vsensor[id].user_vsensor_name, USER_VSENSOR_NAME, "%s", name);
				if (len < 0 || len >= USER_VSENSOR_NAME)
					pr_info("user_vsensor_name write fail, %d\n", len);
				return count;
			}
		}
	}

	pr_info("%s: invalid input\n", __func__);

	return -EINVAL;
}

static ssize_t abnormal_temp_show(struct kobject *kobj, struct kobj_attribute *attr,
	char *buf)
{
	int len = 0;
	int abnormal_flag = 0;

	if (tm_data.is_cputcm) {
		abnormal_flag = therm_intf_read_cputcm_s32(ABNORMAL_TEMP_TCM_OFFSET);
		len += snprintf(buf + len, PAGE_SIZE - len, "%u\n", abnormal_flag);
	} else {
		len += snprintf(buf + len, PAGE_SIZE - len, "%u\n", 0);
	}
	return len;
}

static ssize_t abnormal_temp_store(struct kobject *kobj,
	struct kobj_attribute *attr, const char *buf, size_t count)
{
	char cmd[20];
	unsigned int abnormal_temp_flag;

	if (sscanf(buf, "%14s %u", cmd, &abnormal_temp_flag)
		== 2) {
		if (strncmp(cmd, "ABNORMAL_TEMP", 13) == 0) {
			therm_intf_write_cputcm(abnormal_temp_flag, ABNORMAL_TEMP_TCM_OFFSET);
			return count;
		}
	}

	pr_info("[abnormal_temp] invalid input\n");

	return -EINVAL;
}

static ssize_t lvts_info1_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	int len = 0;

	len += snprintf(buf + len, PAGE_SIZE - len, "%x,%x,%x,%x,%x,%x,%x,%x,%x,%x\n",
		therm_intf_read_cputcm_s32(MBRAIN_LOG_OFFSET),
		therm_intf_read_cputcm_s32(MBRAIN_LOG_OFFSET + 4),
		therm_intf_read_cputcm_s32(MBRAIN_LOG_OFFSET + 8),
		therm_intf_read_cputcm_s32(MBRAIN_LOG_OFFSET + 12),
		therm_intf_read_cputcm_s32(MBRAIN_LOG_OFFSET + 16),
		therm_intf_read_cputcm_s32(MBRAIN_LOG_OFFSET + 20),
		therm_intf_read_cputcm_s32(MBRAIN_LOG_OFFSET + 24),
		therm_intf_read_cputcm_s32(MBRAIN_LOG_OFFSET + 28),
		therm_intf_read_cputcm_s32(MBRAIN_LOG_OFFSET + 32),
		therm_intf_read_cputcm_s32(MBRAIN_LOG_OFFSET + 36));

		return len;
}

static ssize_t lvts_info2_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	int len = 0;

	len += snprintf(buf + len, PAGE_SIZE - len, "%x,%x,%x,%x,%x,%x,%x,%x,%x,%x\n",
		therm_intf_read_cputcm_s32(MBRAIN_LOG_OFFSET + 40),
		therm_intf_read_cputcm_s32(MBRAIN_LOG_OFFSET + 44),
		therm_intf_read_cputcm_s32(MBRAIN_LOG_OFFSET + 48),
		therm_intf_read_cputcm_s32(MBRAIN_LOG_OFFSET + 52),
		therm_intf_read_cputcm_s32(MBRAIN_LOG_OFFSET + 56),
		therm_intf_read_cputcm_s32(MBRAIN_LOG_OFFSET + 60),
		therm_intf_read_cputcm_s32(MBRAIN_LOG_OFFSET + 64),
		therm_intf_read_cputcm_s32(MBRAIN_LOG_OFFSET + 68),
		therm_intf_read_cputcm_s32(MBRAIN_LOG_OFFSET + 72),
		therm_intf_read_cputcm_s32(MBRAIN_LOG_OFFSET + 76));

		return len;
}

static ssize_t lvts_info3_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	int len = 0;

	len += snprintf(buf + len, PAGE_SIZE - len, "%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x\n",
		therm_intf_read_cputcm_s32(MBRAIN_LOG_OFFSET + 80),
		therm_intf_read_cputcm_s32(MBRAIN_LOG_OFFSET + 84),
		therm_intf_read_cputcm_s32(MBRAIN_LOG_OFFSET + 88),
		therm_intf_read_cputcm_s32(MBRAIN_LOG_OFFSET + 92),
		therm_intf_read_cputcm_s32(MBRAIN_LOG_OFFSET + 96),
		therm_intf_read_cputcm_s32(MBRAIN_LOG_OFFSET + 100),
		therm_intf_read_cputcm_s32(MBRAIN_LOG_OFFSET + 104),
		therm_intf_read_cputcm_s32(MBRAIN_LOG_OFFSET + 108),
		therm_intf_read_cputcm_s32(MBRAIN_LOG_OFFSET + 112),
		therm_intf_read_cputcm_s32(MBRAIN_LOG_OFFSET + 116),
		therm_intf_read_cputcm_s32(MBRAIN_LOG_OFFSET + 120),
		therm_intf_read_cputcm_s32(MBRAIN_LOG_OFFSET + 124));

		return len;
}

unsigned int thermal_hint_enable;

static ssize_t thermal_hint_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	int len = 0;

	if (cm_thermal_hint) {
		len += snprintf(buf + len, PAGE_SIZE - len, "%u\n",
			thermal_hint_enable);
	}
	return len;
}

static ssize_t thermal_hint_store(struct kobject *kobj,
	struct kobj_attribute *attr, const char *buf, size_t count)
{
	char cmd[20];
	static int enable;

	if (sscanf(buf, "%12s %u", cmd, &thermal_hint_enable)
		== 2) {
		if (strncmp(cmd, "thermal_hint", 12) == 0) {
			if ((cm_thermal_hint) && enable != thermal_hint_enable) {
				cm_thermal_hint(thermal_hint_enable);
				enable = thermal_hint_enable;
			}
			return count;
		}
	}

	pr_info("[thermal_hint] invalid input\n");

	return -EINVAL;
}

static ssize_t gpt_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	int len = 0;

	len += snprintf(buf + len, PAGE_SIZE - len, "%d,%d,%d,%d,%d\n",
		therm_intf_read_csram_s32(GPU_PRE_THROTTLE_TEMP_OFFSET_1),
		therm_intf_read_csram_s32(GPU_PRE_THROTTLE_OPP_OFFSET_1),
		therm_intf_read_csram_s32(GPU_PRE_THROTTLE_TEMP_OFFSET_2),
		therm_intf_read_csram_s32(GPU_PRE_THROTTLE_OPP_OFFSET_2),
		therm_intf_read_csram_s32(GPU_PRE_THROTTLE_OFFSET));

	return len;
}

static ssize_t gpt_store(struct kobject *kobj,
	struct kobj_attribute *attr, const char *buf, size_t count)
{
	char cmd[20];
	int index,temp,opp;

	if (sscanf(buf, "%3s %d %d %d ", cmd, &index, &temp, &opp)
		== 4) {
		if (strncmp(cmd, "gpt", 3) == 0) {
			set_gpu_pre_throttle(temp, index);
			set_gpu_pre_throttle_opp(opp, index);

			return count;
		}
	}

	if (sscanf(buf, "%7s", cmd) == 1) {
		if (strncmp(cmd, "disable", 7) == 0) {
			set_gpu_pre_throttle(120000, 1);
			set_gpu_pre_throttle(120000, 2);
			therm_intf_write_csram(0, GPU_PRE_THROTTLE_OFFSET);
			return count;
		}
	}

	if (sscanf(buf, "%6s", cmd) == 1) {
		if (strncmp(cmd, "enable", 6) == 0) {
			therm_intf_write_csram(1, GPU_PRE_THROTTLE_OFFSET);
			set_gpu_pre_throttle(0xFFFF, 1);
			set_gpu_pre_throttle(0xFFFF, 2);

			return count;
		}
	}

	pr_info("[gpt] invalid input\n");

	return -EINVAL;
}

static struct kobj_attribute ttj_attr = __ATTR_RW(ttj);
static struct kobj_attribute power_budget_attr = __ATTR_RW(power_budget);
static struct kobj_attribute cpu_info_attr = __ATTR_RO(cpu_info);
static struct kobj_attribute gpu_info_attr = __ATTR_RO(gpu_info);
static struct kobj_attribute apu_info_attr = __ATTR_RO(apu_info);
static struct kobj_attribute gpu_freq_attr = __ATTR_RO(gpu_freq);
static struct kobj_attribute is_cpu_limit_attr = __ATTR_RO(is_cpu_limit);
static struct kobj_attribute is_gpu_limit_attr = __ATTR_RO(is_gpu_limit);
static struct kobj_attribute is_apu_limit_attr = __ATTR_RO(is_apu_limit);
static struct kobj_attribute frs_info_attr = __ATTR_RW(frs_info);
static struct kobj_attribute cpu_temp_attr = __ATTR_RO(cpu_temp);
static struct kobj_attribute headroom_info_attr = __ATTR_RO(headroom_info);
static struct kobj_attribute cpu_atc_attr = __ATTR_RO(cpu_atc);
static struct kobj_attribute gpu_atc_attr = __ATTR_RO(gpu_atc);
static struct kobj_attribute apu_atc_attr = __ATTR_RO(apu_atc);
static struct kobj_attribute target_tpcb_attr = __ATTR_RW(target_tpcb);
static struct kobj_attribute md_sensor_info_attr = __ATTR_RW(md_sensor_info);
static struct kobj_attribute md_actuator_info_attr = __ATTR_RW(md_actuator_info);
static struct kobj_attribute info_b_attr = __ATTR_RO(info_b);
static struct kobj_attribute utc_count_attr = __ATTR_RO(utc_count);
static struct kobj_attribute dsu_ceiling_freq_attr = __ATTR_RO(dsu_ceiling_freq);
static struct kobj_attribute max_ttj_attr = __ATTR_RW(max_ttj);
static struct kobj_attribute min_ttj_attr = __ATTR_RW(min_ttj);
static struct kobj_attribute min_throttle_freq_attr =
	__ATTR_RW(min_throttle_freq);
static struct kobj_attribute sports_mode_attr = __ATTR_RW(sports_mode);
static struct kobj_attribute cg_policy_mode_attr = __ATTR_RW(cg_policy_mode);
static struct kobj_attribute vtskin_info_attr = __ATTR_RW(vtskin_info);
static struct kobj_attribute vtskin_temp_attr = __ATTR_RW(vtskin_temp);
static struct kobj_attribute catm_p_attr = __ATTR_RW(catm_p);
static struct kobj_attribute dram_data_rate_attr = __ATTR_RO(dram_data_rate);
static struct kobj_attribute pid_info_attr = __ATTR_RW(pid_info);
static struct kobj_attribute bat_type_attr = __ATTR_RO(bat_type);
static struct kobj_attribute user_vsensor_attr = __ATTR_RW(user_vsensor);
static struct kobj_attribute abnormal_temp_attr = __ATTR_RW(abnormal_temp);
static struct kobj_attribute lvts_info1_attr = __ATTR_RO(lvts_info1);
static struct kobj_attribute lvts_info2_attr = __ATTR_RO(lvts_info2);
static struct kobj_attribute lvts_info3_attr = __ATTR_RO(lvts_info3);
static struct kobj_attribute thermal_hint_attr = __ATTR_RW(thermal_hint);
static struct kobj_attribute gpt_attr = __ATTR_RW(gpt);



static struct attribute *thermal_attrs[] = {
	&ttj_attr.attr,
	&power_budget_attr.attr,
	&cpu_info_attr.attr,
	&gpu_info_attr.attr,
	&gpu_freq_attr.attr,
	&apu_info_attr.attr,
	&is_cpu_limit_attr.attr,
	&is_gpu_limit_attr.attr,
	&is_apu_limit_attr.attr,
	&frs_info_attr.attr,
	&cpu_temp_attr.attr,
	&headroom_info_attr.attr,
	&cpu_atc_attr.attr,
	&gpu_atc_attr.attr,
	&apu_atc_attr.attr,
	&target_tpcb_attr.attr,
	&md_sensor_info_attr.attr,
	&md_actuator_info_attr.attr,
	&info_b_attr.attr,
	&max_ttj_attr.attr,
	&min_ttj_attr.attr,
	&utc_count_attr.attr,
	&dsu_ceiling_freq_attr.attr,
	&min_throttle_freq_attr.attr,
	&sports_mode_attr.attr,
	&cg_policy_mode_attr.attr,
	&vtskin_info_attr.attr,
	&vtskin_temp_attr.attr,
	&catm_p_attr.attr,
	&dram_data_rate_attr.attr,
	&pid_info_attr.attr,
	&bat_type_attr.attr,
	&user_vsensor_attr.attr,
	&abnormal_temp_attr.attr,
	&lvts_info1_attr.attr,
	&lvts_info2_attr.attr,
	&lvts_info3_attr.attr,
	&thermal_hint_attr.attr,
	&gpt_attr.attr,
	NULL
};
static struct attribute_group thermal_attr_group = {
	.name	= "thermal",
	.attrs	= thermal_attrs,
};

#if IS_ENABLED(CONFIG_DEBUG_FS)
static int emul_temp_show(struct seq_file *m, void *unused)
{

	if (tm_data.is_cputcm)
		seq_printf(m, "%d,%d,%d,%d\n",
			therm_intf_read_cputcm_s32(EMUL_TEMP_TCM_OFFSET),
			therm_intf_read_csram_s32(EMUL_TEMP_OFFSET + 4),
			therm_intf_read_csram_s32(EMUL_TEMP_OFFSET + 8),
			therm_intf_read_csram_s32(EMUL_TEMP_OFFSET + 12));
	else
		seq_printf(m, "%d,%d,%d,%d\n",
			therm_intf_read_csram_s32(EMUL_TEMP_OFFSET),
			therm_intf_read_csram_s32(EMUL_TEMP_OFFSET + 4),
			therm_intf_read_csram_s32(EMUL_TEMP_OFFSET + 8),
			therm_intf_read_csram_s32(EMUL_TEMP_OFFSET + 12));

	return 0;
}

static ssize_t emul_temp_write(struct file *flip,
			const char *ubuf, size_t cnt, loff_t *data)
{
	int ret, temp;
	char *buf;
	char target[11];

	buf = kzalloc(cnt + 1, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	if (copy_from_user(buf, ubuf, cnt)) {
		ret = -EFAULT;
		goto err;
	}
	buf[cnt] = '\0';

	if (sscanf(buf, "%10s %d", target, &temp) != 2) {
		dev_info(tm_data.dev, "invalid input for emul temp\n");
		ret = -EINVAL;
		goto err;
	}

	if (strncmp(target, "cpu", 3) == 0) {
		if (tm_data.is_cputcm)
			therm_intf_write_cputcm(temp, EMUL_TEMP_TCM_OFFSET);
		else
			therm_intf_write_csram(temp, EMUL_TEMP_OFFSET);

	} else if (strncmp(target, "gpu", 3) == 0) {
		therm_intf_write_csram(temp, EMUL_TEMP_OFFSET + 4);
	} else if (strncmp(target, "apu", 3) == 0) {
		therm_intf_write_csram(temp, EMUL_TEMP_OFFSET + 8);
		therm_intf_write_apu_mbox(temp, APU_MBOX_EMUL_TEMP_OFFSET);
	} else if (strncmp(target, "vcore", 5) == 0) {
		therm_intf_write_csram(temp, EMUL_TEMP_OFFSET + 12);
	}

	ret = cnt;

err:
	kfree(buf);

	return ret;
}

static int emul_temp_open(struct inode *i, struct file *file)
{
	return single_open(file, emul_temp_show, i->i_private);
}

static const struct file_operations emul_temp_fops = {
	.owner = THIS_MODULE,
	.open = emul_temp_open,
	.read = seq_read,
	.write = emul_temp_write,
	.llseek = seq_lseek,
	.release = single_release,
};


static int cpu_cooler_show(struct seq_file *m, void *unused)
{

	if (tm_data.is_cputcm)
		seq_printf(m, "%d, %d, %d, %d, %d ,%d, %d, %d, %d, %d, %d, %d\n",
			therm_intf_read_cputcm(CPU_COOLER_TCM_BASE),
			therm_intf_read_cputcm(CPU_COOLER_TCM_BASE + 4),
			therm_intf_read_cputcm(CPU_COOLER_TCM_BASE + 8),
			therm_intf_read_cputcm(CPU_COOLER_TCM_BASE + 12),
			therm_intf_read_cputcm(CPU_COOLER_TCM_BASE + 16),
			therm_intf_read_cputcm(CPU_COOLER_TCM_BASE + 20),
			therm_intf_read_cputcm(CPU_COOLER_TCM_BASE + 24),
			therm_intf_read_cputcm(CPU_COOLER_TCM_BASE + 28),
			therm_intf_read_cputcm(CPU_COOLER_TCM_BASE + 32),
			therm_intf_read_cputcm(CPU_COOLER_TCM_BASE + 36),
			therm_intf_read_cputcm(CPU_COOLER_TCM_BASE + 40),
			therm_intf_read_cputcm(CPU_COOLER_TCM_BASE + 44));
	else
		seq_printf(m, "%d, %d, %d, %d, %d ,%d, %d, %d, %d, %d, %d, %d\n",
			therm_intf_read_csram(CPU_COOLER_BASE),
			therm_intf_read_csram(CPU_COOLER_BASE + 4),
			therm_intf_read_csram(CPU_COOLER_BASE + 8),
			therm_intf_read_csram(CPU_COOLER_BASE + 12),
			therm_intf_read_csram(CPU_COOLER_BASE + 16),
			therm_intf_read_csram(CPU_COOLER_BASE + 20),
			therm_intf_read_csram(CPU_COOLER_BASE + 24),
			therm_intf_read_csram(CPU_COOLER_BASE + 28),
			therm_intf_read_csram(CPU_COOLER_BASE + 32),
			therm_intf_read_csram(CPU_COOLER_BASE + 36),
			therm_intf_read_csram(CPU_COOLER_BASE + 40),
			therm_intf_read_csram(CPU_COOLER_BASE + 44));

	return 0;
}

#define CPU_COOLER_THERMAL_SRAM_LEN (12)

static ssize_t cpu_cooler_write(struct file *flip,
			const char *ubuf, size_t cnt, loff_t *data)
{
	int ret, len, i;
	char *buf;
	int values[CPU_COOLER_THERMAL_SRAM_LEN] = { 0 };

	buf = kzalloc(cnt + 1, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	if (copy_from_user(buf, ubuf, cnt)) {
		ret = -EFAULT;
		goto err;
	}
	buf[cnt] = '\0';

	len = sscanf(buf, "%d %d %d %d %d %d %d %d %d %d %d %d",
		&values[0], &values[1], &values[2], &values[3],
		&values[4], &values[5], &values[6], &values[7],
		&values[8], &values[9], &values[10], &values[11]);

	for (i = 0; i < len; i++) {
		if (tm_data.is_cputcm)
			therm_intf_write_cputcm(values[i], CPU_COOLER_TCM_BASE + (i * 4));
		else
			therm_intf_write_csram(values[i], CPU_COOLER_BASE + (i * 4));
	}

	ret = cnt;

err:
	kfree(buf);

	return ret;
}

static int cpu_cooler_open(struct inode *i, struct file *file)
{
	return single_open(file, cpu_cooler_show, i->i_private);
}

static const struct file_operations cpu_cooler_fops = {
	.owner = THIS_MODULE,
	.open = cpu_cooler_open,
	.read = seq_read,
	.write = cpu_cooler_write,
	.llseek = seq_lseek,
	.release = single_release,
};


static int gpu_cooler_show(struct seq_file *m, void *unused)
{
	/* output: tt, tp, polling_delay, leakage info */
	seq_printf(m, "%d,%d,%d,%d, ctt = %d\n",
		therm_intf_read_csram(GPU_COOLER_BASE+4),
		therm_intf_read_csram(GPU_COOLER_BASE),
		therm_intf_read_csram(GPU_COOLER_BASE+8),
		therm_intf_read_csram(GPU_COOLER_BASE+20),
		therm_intf_read_csram(GPU_COOLER_BASE+24));

	return 0;
}

static ssize_t gpu_cooler_write(struct file *flip,
			const char *ubuf, size_t cnt, loff_t *data)
{
	int ret, value;
	char *buf;
	char target[30];

	cnt = min_t(size_t, cnt, 255);

	buf = kzalloc(cnt + 1, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	if (copy_from_user(buf, ubuf, cnt)) {
		ret = -EFAULT;
		goto err;
	}
	buf[cnt] = '\0';

	if (sscanf(buf, "%15s %d", target, &value) != 2) {
		dev_info(tm_data.dev, "invalid input for gpu cooler dbg intf\n");
		ret = -EINVAL;
		goto err;
	}
	if (strncmp(target, "tp", 3) == 0)
		therm_intf_write_csram(value, GPU_COOLER_BASE);
	else if (strncmp(target, "tt", 3) == 0)
		therm_intf_write_csram(value, GPU_COOLER_BASE + 4);
	else if (strncmp(target, "polling_delay", 3) == 0)
		therm_intf_write_csram(value, GPU_COOLER_BASE + 8);
	else if (strncmp(target, "ctt", 3) == 0)
		therm_intf_write_csram(value, GPU_COOLER_BASE + 24);

	ret = cnt;

err:
	kfree(buf);

	return ret;
}

static int gpu_cooler_open(struct inode *i, struct file *file)
{
	return single_open(file, gpu_cooler_show, i->i_private);
}

static const struct file_operations gpu_cooler_fops = {
	.owner = THIS_MODULE,
	.open = gpu_cooler_open,
	.read = seq_read,
	.write = gpu_cooler_write,
	.llseek = seq_lseek,
	.release = single_release,
};


static int gpu_temp_debug_show(struct seq_file *m, void *unused)
{
	seq_printf(m, "statistics_control=%d max_temp=%d\n",
			   therm_intf_read_csram(GPU_COOLER_BASE + 16),
			   therm_intf_read_csram(GPU_COOLER_BASE + 12));

	return 0;
}

static ssize_t gpu_temp_debug_write(struct file *flip,
					const char *ubuf, size_t cnt, loff_t *data)
{
	int ret, value;
	char *buf;

	cnt = min_t(size_t, cnt, 255);

	buf = kzalloc(cnt + 1, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	if (copy_from_user(buf, ubuf, cnt)) {
		ret = -EFAULT;
		goto err;
	}
	buf[cnt] = '\0';

	if (kstrtoint(buf, 10, &value) != 0) {
		dev_info(tm_data.dev, "invalid input for gpu temp check dbg intf\n");
		ret = -EINVAL;
		goto err;
	}

	if (value == 0) {
		therm_intf_write_csram(0, GPU_COOLER_BASE + 16);
		therm_intf_write_csram(-274000, GPU_COOLER_BASE + 12);
	}
	if (value < 0)
		therm_intf_write_csram(-1, GPU_COOLER_BASE + 16);


	ret = cnt;

err:
	kfree(buf);

	return ret;
}

static int gpu_temp_debug_open(struct inode *i, struct file *file)
{
	return single_open(file, gpu_temp_debug_show, i->i_private);
}

static const struct file_operations gpu_temp_debug_fops = {
	.owner = THIS_MODULE,
	.open = gpu_temp_debug_open,
	.read = seq_read,
	.write = gpu_temp_debug_write,
	.llseek = seq_lseek,
	.release = single_release,
};


static void therm_intf_debugfs_init(void)
{
	tm_data.debug_dir = debugfs_create_dir("thermal", NULL);
	if (!tm_data.debug_dir) {
		dev_info(tm_data.dev, "failed to create thermal debugfs!\n");
		return;
	}

	debugfs_create_file("emul_temp", 0640, tm_data.debug_dir, NULL, &emul_temp_fops);
	debugfs_create_file("gpu_cooler_debug", 0640, tm_data.debug_dir, NULL, &gpu_cooler_fops);
	debugfs_create_file("gpu_temp_check", 0640, tm_data.debug_dir, NULL, &gpu_temp_debug_fops);
	debugfs_create_file("cpu_cooler_debug", 0640, tm_data.debug_dir, NULL, &cpu_cooler_fops);

	if (tm_data.is_cputcm)
		therm_intf_write_cputcm(THERMAL_TEMP_INVALID, EMUL_TEMP_TCM_OFFSET);
	else
		therm_intf_write_csram(THERMAL_TEMP_INVALID, EMUL_TEMP_OFFSET);

	therm_intf_write_csram(THERMAL_TEMP_INVALID, EMUL_TEMP_OFFSET + 4);
	therm_intf_write_csram(THERMAL_TEMP_INVALID, EMUL_TEMP_OFFSET + 8);
	therm_intf_write_csram(THERMAL_TEMP_INVALID, EMUL_TEMP_OFFSET + 12);

	therm_intf_write_apu_mbox(THERMAL_TEMP_INVALID, APU_MBOX_EMUL_TEMP_OFFSET);
}

static void therm_intf_debugfs_exit(void)
{
	debugfs_remove_recursive(tm_data.debug_dir);
}
#else
static void therm_intf_debugfs_init(void) {}
static void therm_intf_debugfs_exit(void) {}
#endif


static const struct of_device_id therm_intf_of_match[] = {
	{ .compatible = "mediatek,therm_intf", },
	{},
};
MODULE_DEVICE_TABLE(of, therm_intf_of_match);

static int therm_intf_probe(struct platform_device *pdev)
{
	struct resource *res;
	void __iomem *addr;
	struct device_node *cpu_np, *gauge_np;
	struct of_phandle_args args;
	unsigned int cpu, max_perf_domain = 0;
	int ret;

	if (!pdev->dev.of_node) {
		dev_info(&pdev->dev, "Only DT based supported\n");
		return -ENODEV;
	}

	tm_data.dev = &pdev->dev;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "therm_sram");
	addr = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(addr))
		return PTR_ERR(addr);

	thermal_csram_base = addr;

	/* Some projects don't support CPU TCM */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "therm_cputcm");
	if (res) {
		addr = devm_ioremap_resource(&pdev->dev, res);
		if (IS_ERR(addr))
			return PTR_ERR(addr);

		thermal_cputcm_base = addr;
		tm_data.is_cputcm = 1;

		therm_intf_write_csram(1, TCM_BUF_OFFSET);

		dev_info(&pdev->dev, "cpu tcm resource ready\n");
	} else {
		dev_info(&pdev->dev, "Failed to get cpu tcm resource\n");
		tm_data.is_cputcm = 0;
		therm_intf_write_csram(0, TCM_BUF_OFFSET);
	}


	/* Some projects don't support APU */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "apu_mbox");
	if (!res) {
		dev_info(&pdev->dev, "Failed to get apu_mbox resource\n");
	} else {
		addr = ioremap(res->start, res->end - res->start + 1);
		if (IS_ERR_OR_NULL(addr))
			dev_info(&pdev->dev, "Failed to remap apu_mbox addr\n");
		else
			thermal_apu_mbox_base = addr;
	}

	/* get CPU cluster num */
	for_each_possible_cpu(cpu) {
		cpu_np = of_cpu_device_node_get(cpu);
		if (!cpu_np) {
			dev_info(&pdev->dev, "Failed to get cpu %d device\n", cpu);
			return -ENODEV;
		}

		ret = of_parse_phandle_with_args(cpu_np, "performance-domains",
						 "#performance-domain-cells", 0,
						 &args);

		if (ret < 0)
			return ret;

		max_perf_domain = max(max_perf_domain, args.args[0]);
	}

	tm_data.cpu_cluster_num = max_perf_domain + 1;
	dev_info(&pdev->dev, "cpu_cluster_num = %d\n", tm_data.cpu_cluster_num);

	ret = sysfs_create_group(kernel_kobj, &thermal_attr_group);
	if (ret) {
		dev_err(&pdev->dev, "failed to create thermal sysfs, ret=%d!\n", ret);
		return ret;
	}

	gauge_np = of_find_node_by_name(NULL, "mtk-gauge");
	if (!gauge_np)
		dev_info(&pdev->dev, "Failed to get mtk-gauge\n");
	else {
		ret = of_property_read_u32(gauge_np, "bat_type", &bat_type);
		if (ret)
			dev_notice(&pdev->dev, "bat_type fail, ret=%d\n", ret);
	}

	therm_intf_debugfs_init();

	mutex_init(&tm_data.lock);

	tm_data.sw_ready = 1;
	tm_data.tj_info.catm_cpu_ttj = 95000;
	tm_data.tj_info.catm_gpu_ttj = 95000;
	tm_data.tj_info.catm_apu_ttj = 95000;
	tm_data.tj_info.cpu_max_ttj = 95000;
	tm_data.tj_info.gpu_max_ttj = 95000;
	tm_data.tj_info.apu_max_ttj = 95000;
	tm_data.tj_info.min_ttj = 63000;

#ifdef CONFIG_LEDS_BRIGHTNESS_CHANGED
	ret = scrn_generic_netlink_init();
	if (ret)
		dev_info(&pdev->dev, "Failed to initialize generic netlink\n");
	else
		mtk_leds_register_notifier(&leds_init_notifier);
#endif

	return 0;
}

static int therm_intf_remove(struct platform_device *pdev)
{
	therm_intf_debugfs_exit();
	sysfs_remove_group(kernel_kobj, &thermal_attr_group);

#ifdef CONFIG_LEDS_BRIGHTNESS_CHANGED
		mtk_leds_unregister_notifier(&leds_init_notifier);
#endif

	return 0;
}

static struct platform_driver therm_intf_driver = {
	.probe = therm_intf_probe,
	.remove = therm_intf_remove,
	.driver = {
		.name = "mtk-thermal-interface",
		.of_match_table = therm_intf_of_match,
	},
};

module_platform_driver(therm_intf_driver);

MODULE_AUTHOR("Henry Huang <henry.huang@mediatek.com>");
MODULE_DESCRIPTION("Mediatek thermal interface driver");
MODULE_LICENSE("GPL v2");

