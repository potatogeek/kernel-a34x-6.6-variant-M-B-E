#
# Copyright (C) 2015 MediaTek Inc.
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

#
# Makefile for misc devices that really don't fit anywhere else.
#

MTK_CUSTOM_PATH  := $(DEVICE_MODULES_PATH)/drivers/misc/mediatek


subdir-ccflags-y += -I$(MTK_CUSTOM_PATH)/lens/vcm/proprietary/main/inc
subdir-ccflags-y += -I$(MTK_CUSTOM_PATH)/lens/vcm/proprietary/main/common/lc898122af
subdir-ccflags-y += -I$(MTK_CUSTOM_PATH)/lens/vcm/proprietary/main/common/lc898212xdaf/inc
ifeq ($(CONFIG_MTK_LENS_BU63165AF_SUPPORT), y)
  subdir-ccflags-y += -I$(MTK_CUSTOM_PATH)/lens/vcm/proprietary/main/common/bu63165af/inc
else
  subdir-ccflags-y += -I$(MTK_CUSTOM_PATH)/lens/vcm/proprietary/main/common/bu63169af/inc
endif
subdir-ccflags-y += -I$(MTK_CUSTOM_PATH)/imgsensor/src/common/sysfs
subdir-ccflags-y += -I$(MTK_CUSTOM_PATH)/imgsensor/inc

ifdef CONFIG_CAMERA_SAMSUNG_MODEL_NAME
CAMERA_OIS_PROJECT_FOLDER := camera_project/$(subst ",,$(CONFIG_CAMERA_SAMSUNG_MODEL_NAME))
else
CAMERA_OIS_PROJECT_FOLDER := camera_project/default
endif

$(info CAMERA_OIS_PROJECT_FOLDER = $(CAMERA_OIS_PROJECT_FOLDER))
$(info CONFIG_CAMERA_OIS = $(CONFIG_CAMERA_OIS))
subdir-ccflags-$(CONFIG_CAMERA_OIS) += -I$(MTK_CUSTOM_PATH)/lens/cam_ois/src/$(CAMERA_OIS_PROJECT_FOLDER)
subdir-ccflags-$(CONFIG_CAMERA_OIS) += -I$(MTK_CUSTOM_PATH)/lens/cam_ois/inc
subdir-ccflags-$(CONFIG_CAMERA_OIS) += -I$(MTK_CUSTOM_PATH)/lens/cam_ois/src

obj-$(CONFIG_MTK_LENS)    += mainaf.o

mainaf-y  += main_lens.o
mainaf-y  += common/fp5510e2af/FP5510E2AF.o
mainaf-y  += common/bu6424af/BU6424AF.o
mainaf-y  += common/bu6429af/BU6429AF.o
mainaf-y  += common/bu64748af/bu64748_function.o
mainaf-y  += common/bu64748af/bu64748af.o
mainaf-y  += common/dw9714af/DW9714AF.o
mainaf-y  += common/dw9800waf/DW9800WAF.o
mainaf-y  += common/dw9814af/DW9814AF.o
mainaf-y  += common/dw9818af/DW9818AF.o
mainaf-y  += common/dw9718af/DW9718AF.o
mainaf-y  += common/dw9718gaf/DW9718AF.o
mainaf-y  += common/dw9718saf/DW9718SAF.o
mainaf-y  += common/dw9718taf/DW9718TAF.o
mainaf-y  += common/dw9719taf/DW9719TAF.o
mainaf-y  += common/dw9763af/DW9763AF.o
mainaf-y  += common/dw9825af_ois_mcu/DW9825AF_OIS_MCU.o
mainaf-y  += common/dw9839af/DW9839AF.o
mainaf-y  += common/gt9764af/GT9764AF.o
mainaf-y  += common/lc898212af/LC898212AF.o
mainaf-y  += common/lc898212xdaf/LC898212XDAF.o
mainaf-y  += common/lc898212xdaf/AfInter.o
mainaf-y  += common/lc898212xdaf/AfInit.o
mainaf-y  += common/lc898212xdaf/AfSTMV.o
mainaf-y  += common/lc898214af/LC898214AF.o
mainaf-y  += common/lc898217af/LC898217AF.o
mainaf-y  += common/lc898217afa/LC898217AFA.o
mainaf-y  += common/lc898217afb/LC898217AFB.o
mainaf-y  += common/lc898217afc/LC898217AFC.o
mainaf-y  += common/lc898229af/LC898229AF.o
mainaf-y  += common/lc898122af/LC898122AF.o
mainaf-y  += common/lc898122af/OisCmd.o
mainaf-y  += common/lc898122af/OisIni.o
mainaf-y  += common/wv511aaf/WV511AAF.o
mainaf-y  += common/ak7371af/AK7371AF.o
mainaf-y  += common/bu63169af/BU63169AF.o
mainaf-y  += common/bu63169af/OIS_func.o
mainaf-y  += common/bu63169af/OIS_main.o
mainaf-y  += common/bu63169af/OIS_user.o
mainaf-y  += common/bu64253gwzaf/BU64253GWZAF.o
mainaf-y  += common/gt9768af/GT9768AF.o
mainaf-y  += common/gt9772af/GT9772AF.o

CAMERA_OIS_SRC_PATH := ../../../cam_ois/src
mainaf-$(CONFIG_CAMERA_OIS)  += $(CAMERA_OIS_SRC_PATH)/$(CAMERA_OIS_PROJECT_FOLDER)/cam_ois_mcu_fw.o
mainaf-$(CONFIG_CAMERA_OIS)  += $(CAMERA_OIS_SRC_PATH)/$(CAMERA_OIS_PROJECT_FOLDER)/cam_ois_power.o
mainaf-$(CONFIG_CAMERA_OIS)  += $(CAMERA_OIS_SRC_PATH)/cam_ois_aois_if.o
mainaf-$(CONFIG_CAMERA_OIS)  += $(CAMERA_OIS_SRC_PATH)/cam_ois_sysfs.o
mainaf-$(CONFIG_CAMERA_OIS)  += $(CAMERA_OIS_SRC_PATH)/cam_ois.o
mainaf-$(CONFIG_CAMERA_OIS)  += $(CAMERA_OIS_SRC_PATH)/cam_ois_drv.o
mainaf-$(CONFIG_CAMERA_OIS)  += $(CAMERA_OIS_SRC_PATH)/cam_ois_i2c.o

