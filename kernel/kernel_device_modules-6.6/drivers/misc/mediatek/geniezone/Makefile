# Copyright (C) 2018 MediaTek Inc.
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License version 2 as
# published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.

#
# Makefile for the GenieZone KREE driver
#
GZ_SEC_STORAGE_UT := y

ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/geniezone/include
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/geniezone/public
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/memory-ssmr
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/trusted_mem/public
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/trusted_mem/ssmr
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/dma-buf/heaps

$(info ************  drivers/trusty/mtee-kree mk ************)

#ccflags-y := -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/geniezone/public -Werror $(ccflags-y)

ifneq ($(filter y m, $(CONFIG_TEE)),)
  ccflags-y += -I$(DEVICE_MODULES_PATH)/arch/arm/mach-$(MTK_PLATFORM)/include/trustzone/utils/tlutils
  ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/include/mt-plat/$(MTK_PLATFORM)/include/trustzone/utils/tlutils
  ccflags-y += -I$(srctree)/$(src)/$(MTK_PLATFORM)
  ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/trusted_mem/public
  ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/m4u/$(MTK_PLATFORM)
  ifneq ($(filter y m, $(CONFIG_TRUSTONIC_TEE_SUPPORT)),)
    include $(DEVICE_MODULES_PATH)/drivers/tee/gud/Makefile.include
  else ifneq ($(filter y m, $(CONFIG_MICROTRUST_TEE_SUPPORT)),)
    include $(DEVICE_MODULES_PATH)/drivers/tee/teei/Makefile.include
  else ifneq ($(filter y m, $(CONFIG_TEEGRIS_TEE_SUPPORT)),)
    include $(DEVICE_MODULES_PATH)/drivers/tee/tzdev/$(CONFIG_TEEGRIS_VERSION)/include/Makefile.include
    ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/tee/tzdev/$(CONFIG_TEEGRIS_VERSION)/include/tzdev/
  #else ifeq ($(CONFIG_BLOWFISH_TEE_SUPPORT),y)
  # ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/tzdev
  else
  endif
else # Legacy
  ifneq ($(filter y m, $(CONFIG_TRUSTONIC_TEE_SUPPORT)),)
    ccflags-y += -I$(DEVICE_MODULES_PATH)/arch/arm/mach-$(MTK_PLATFORM)/include/trustzone/utils/tlutils
    ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/include/mt-plat/$(MTK_PLATFORM)/include/trustzone/utils/tlutils
    ccflags-y += -I$(srctree)/$(src)/$(MTK_PLATFORM)
    include $(DEVICE_MODULES_PATH)/drivers/tee/gud/Makefile.include
  endif
endif # !CONFIG_TEE

obj-$(CONFIG_MTK_GZ_MAIN) += gz_main_mod.o
gz_main_mod-objs += gz_main.o \
			     mtee_ut/gz_ut.o \
			     mtee_ut/gz_shmem_ut.o mtee_ut/gz_vreg_ut.o \

ifneq ($(filter y m, $(CONFIG_MTK_TRUSTED_MEMORY_SUBSYSTEM)),)
  gz_main_mod-objs += mtee_ut/gz_chmem_ut.o
endif

ifneq ($(filter y m, $(CONFIG_ARM_FFA_TRANSPORT)),)
   gz_main_mod-objs += gz_ffa.o
endif

obj-$(CONFIG_MTK_GZ_TZ_SYSTEM) += gz_tz_system.o
gz_tz_system-objs += tz_system.o mtee-kree/tz_mmap.o mtee-kree/kree_mem.o

ifndef CONFIG_GRT_HYPERVISOR
  obj-$(CONFIG_MTK_ENABLE_GENIEZONE) += gz-trusty/
endif
