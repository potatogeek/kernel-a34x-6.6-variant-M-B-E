# SPDX-License-Identifier: GPL-2.0

ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/include/mt-plat/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/gpu/mediatek/gpufreq/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/power_throttling/

ifeq ($(CONFIG_MTK_GCOV_KERNEL),y)
GCOV_PROFILE_mtk_bp_thl.o := y
endif

obj-$(CONFIG_MTK_BATTERY_OC_POWER_THROTTLING) += mtk_battery_oc_throttling.o
obj-$(CONFIG_MTK_BATTERY_PERCENT_THROTTLING) += mtk_bp_thl.o
obj-$(CONFIG_MTK_DYNAMIC_LOADING_POWER_THROTTLING) += mtk_dynamic_loading_throttling.o
obj-$(CONFIG_MTK_LOW_BATTERY_POWER_THROTTLING) += mtk_low_battery_throttling.o
obj-$(CONFIG_PMIC_LBAT_SERVICE) += pmic_lbat_service.o
obj-$(CONFIG_PMIC_LBAT_SERVICE) += pmic_lvsys_notify.o
obj-$(CONFIG_PMIC_DUAL_LBAT_SERVICE) += pmic_dual_lbat_service.o
obj-$(CONFIG_MTK_MD_POWER_THROTTLING) += mtk_md_power_throttling.o
obj-$(CONFIG_MTK_CPU_POWER_THROTTLING) += mtk_cpu_power_throttling.o
obj-$(CONFIG_MTK_GPU_POWER_THROTTLING) += mtk_gpu_power_throttling.o
