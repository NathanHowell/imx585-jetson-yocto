# Kbuild for the out-of-tree IMX585 tegracam driver.
#
# Two things make this different from an ordinary external module:
#
#  - The tegracam headers (<media/camera_common.h>, <media/tegracam_core.h>) are
#    not in the kernel tree. They come from nvidia-kernel-oot, which the recipe
#    stages and passes in as NVIDIA_OOT_INCDIR.
#  - tegracam_device_register() and friends are exported by nvidia-oot modules,
#    not by vmlinux, so kbuild needs their Module.symvers or every tegracam
#    symbol comes out as an "undefined!" warning and the module will not load.
#
# The archived driver carried 71 vendored copies of NVIDIA's include/media
# headers instead. That builds, but it builds against a frozen R36.4 ABI while
# loading against R39.2.1 modules, which goes wrong silently rather than loudly.

obj-m += imx585.o

KERNEL_SRC ?= /lib/modules/$(shell uname -r)/build
INSTALL_MOD_DIR ?= kernel/drivers/media/i2c

ccflags-y += -I$(NVIDIA_OOT_INCDIR)

.PHONY: all modules modules_install clean

all: modules

modules:
	$(MAKE) -C $(KERNEL_SRC) M=$(CURDIR) modules

modules_install:
	$(MAKE) -C $(KERNEL_SRC) M=$(CURDIR) \
		INSTALL_MOD_DIR=$(INSTALL_MOD_DIR) modules_install

clean:
	$(MAKE) -C $(KERNEL_SRC) M=$(CURDIR) clean
