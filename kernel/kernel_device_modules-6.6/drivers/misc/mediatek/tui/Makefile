#
# Makefile for the stui Module
#

obj-$(CONFIG_TEEGRIS_TUI) += tuihw.o tuihw-inf.o

ccflags-y += -DUSE_TEE_CLIENT_API
ccflags-y += -DTUI_ENABLE_DISPLAY
ccflags-y += -DDEBUG

ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/tui/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/tee/tzdev/$(CONFIG_TEEGRIS_VERSION)/include/tzdev
ccflags-y += -I$(DEVICE_MODULES_PATH)/include
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/gpu/drm/mediatek/mediatek_v2
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/tui/platforms/

# secure memory
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/trusted_mem
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/dma-buf/heaps/

tuihw-inf-y = stui_inf.o

tuihw-y += main.o
tuihw-y += stui_core.o
tuihw-y += platforms/stui_hal_display.o
tuihw-y += platforms/stui_hal_touch.o

ifdef CONFIG_TEEGRIS_TUI_LOWLEVEL
tuihw-y += iwd_agent.o
endif
