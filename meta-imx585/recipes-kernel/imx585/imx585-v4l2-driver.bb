SUMMARY = "IMX585 V4L2 sensor driver"
DESCRIPTION = "Out-of-tree V4L2 kernel module for the Sony IMX585 image sensor."
HOMEPAGE = "https://github.com/NathanHowell/imx585-v4l2-driver"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=15dc9b4bc755528a250e2bba565d30c6"

FILESEXTRAPATHS:prepend := "${THISDIR}/${PN}:"

require imx585-source.inc

SRC_URI += " \
    file://0001-imx585-Provide-driver-mutex-for-state-locking.patch \
    file://0001-imx585-add-extensive-debug-instrumentation.patch \
"

inherit module

DEPENDS += "virtual/kernel"

COMPATIBLE_MACHINE = "(tegra)"

# Align package name with kernel-module-imx585 (no versioned suffix)
KERNEL_MODULE_PACKAGE_SUFFIX = ""

PACKAGES += "kernel-module-imx585"
FILES:kernel-module-imx585 = "${base_libdir}/modules/${KERNEL_VERSION}/kernel/drivers/media/i2c/imx585.ko"
FILES:${PN} = ""
INSANE_SKIP:${PN} += "installed-vs-shipped"
INSANE_SKIP:${PN}-dbg += "usrmerge buildpaths"
