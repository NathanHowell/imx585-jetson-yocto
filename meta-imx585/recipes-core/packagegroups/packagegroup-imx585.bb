SUMMARY = "IMX585 + CEF168 camera bring-up package group"
DESCRIPTION = "Kernel modules, device trees and the userspace tools needed to \
bring up the Sony IMX585 sensor and the Pinefeat CEF168 lens controller on a \
Jetson Orin Nano."

LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

PACKAGE_ARCH = "${MACHINE_ARCH}"

inherit packagegroup

# Camera stack proper.
RDEPENDS:${PN} = " \
    kernel-modules \
    kernel-module-imx585 \
    kernel-module-cef168 \
    imx585-overlay \
    imx585-devicetree \
"

# Bring-up and debug tooling. v4l-utils gives media-ctl and v4l2-ctl, which are
# how you inspect the Tegra VI/NVCSI media graph.
RDEPENDS:${PN} += " \
    v4l-utils \
    i2c-tools \
    libgpiod-tools \
    strace \
    trace-cmd \
    bpftrace \
"

RRECOMMENDS:${PN} = " \
    e2fsprogs \
    iproute2 \
    pciutils \
    usbutils \
"
