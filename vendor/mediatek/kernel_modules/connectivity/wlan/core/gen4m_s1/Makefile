# SPDX-License-Identifier: BSD-2-Clause
KVERSION := $(shell uname -r)

CONN_ALPS_OUT_PATH := $(wildcard $(abspath $(O)/../vendor/mediatek/kernel_modules/connectivity/))
CONNECTIVITY_OUT_PATH := $(if $(CONN_ALPS_OUT_PATH), $(CONN_ALPS_OUT_PATH), $(CONN_EAP_OUT_PATH))

$(info Default out path=$(CONN_ALPS_OUT_PATH))
$(info Connectivity out path=$(CONNECTIVITY_OUT_PATH))

ifneq ($(CONNECTIVITY_OUT_PATH),)
	SEGMENT := SP
	KERNEL_DIR=$(KERNEL_SRC)
	MODULE_PWD=$(M)/../..
	include $(KERNEL_SRC)/$(DEVICE_MODULES_REL_DIR)/Makefile.include
ifneq ($(_CONNAC_VER), 1_0)
	EXTRA_SYMBOLS += $(abspath $(CONNECTIVITY_OUT_PATH)/conninfra/Module.symvers)
	EXTRA_SYMBOLS += $(abspath $(CONNECTIVITY_OUT_PATH)/connfem/Module.symvers)
	EXTRA_SYMBOLS += $(abspath $(CONNECTIVITY_OUT_PATH)/wlan/adaptor/wlan_page_pool/Module.symvers)
endif
	EXTRA_SYMBOLS += $(foreach dep, $(_CONNAC_DENPENDENCY_SYMBOLS), $(abspath $(CONNECTIVITY_OUT_PATH)/$(dep)))
else
	KERNEL_DIR=/lib/modules/$(shell uname -r)/build
	MODULE_PWD=$(PWD)
	EXTRA_SYMBOLS := $(MODULE_PWD)/../conninfra/Module.symvers
	EXTRA_SYMBOLS += $(MODULE_PWD)/../adaptor/wlan_page_pool/Module.symvers
	EXTRA_SYMBOLS += $(MODULE_PWD)/../connfem/Module.symvers
endif
MODULE_PWD=../vendor/mediatek/kernel_modules/connectivity/wlan/core/gen4m_s1
$(info gen4m depends on following symbols:[${EXTRA_SYMBOLS}])
modules modules_install clean:
	$(MAKE) -C $(KERNEL_DIR) M=$(MODULE_PWD) $(KBUILD_OPTIONS) EXTRA_CFLAGS="$(EXTRA_CFLAGS)" KBUILD_EXTRA_SYMBOLS="$(EXTRA_SYMBOLS)" MODULE_NAME=$(_MODULE_NAME) SEGMENT=$(SEGMENT) $(@)
	mkdir -p $(O)/$(M)
	cp -f $(O)/$(MODULE_PWD)/Module.symvers $(O)/$(M)/Module.symvers
	cp -f $(O)/$(MODULE_PWD)/*.ko $(O)/$(M)
