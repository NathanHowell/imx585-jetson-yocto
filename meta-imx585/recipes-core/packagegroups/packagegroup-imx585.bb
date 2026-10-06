SUMMARY = "IMX585 + CEF168 camera bring-up package group"
DESCRIPTION = "The camera runtime (packagegroup-imx585-camera) plus the \
userspace tooling used to bring it up and debug it on a Jetson Orin Nano."

LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

PACKAGE_ARCH = "${MACHINE_ARCH}"

inherit packagegroup

# Camera stack proper. kernel-modules installs *every* module the kernel built;
# that is deliberate here (bring-up image) and deliberately absent from
# packagegroup-imx585-camera.
RDEPENDS:${PN} = " \
    packagegroup-imx585-camera \
    kernel-modules \
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
