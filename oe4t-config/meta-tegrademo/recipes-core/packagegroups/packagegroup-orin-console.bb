SUMMARY = "Package group for Jetson Orin Nano console image"
DESCRIPTION = "Packages required for the Orin Nano console-focused image with networking, container, and NVIDIA GPU support."

LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

PACKAGE_ARCH = "${MACHINE_ARCH}"

inherit packagegroup

RDEPENDS:${PN} = " \
    ca-certificates \
    chrony \
    dhcpcd \
    i2c-tools \
    kernel-modules \
    kernel-module-cef168 \
    kernel-module-imx585 \
    imx585-overlay \
    nvidia-container-toolkit \
    docker-moby \
    zsh \
    tegrademo-devicetree \
    v4l-utils \
    libgpiod-tools \
    strace \
    ltrace \
    bpftrace \
    trace-cmd \
"

RRECOMMENDS:${PN} = " \
    e2fsprogs \
    iproute2 \
    net-tools \
    pciutils \
    usbutils \
"
