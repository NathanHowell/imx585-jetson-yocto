SUMMARY = "IMX585 + CEF168 camera runtime"
DESCRIPTION = "The minimum set of packages needed to capture from the Sony \
IMX585 and drive the Pinefeat CEF168 lens controller: the two out-of-tree \
drivers, the device trees that describe them, and the NVIDIA VI/NVCSI module \
set they bind to. No debug or bring-up tooling."

LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

PACKAGE_ARCH = "${MACHINE_ARCH}"

inherit packagegroup

# nvidia-kernel-oot-cameras is what carries tegra-camera, tegra-camera-platform,
# nvhost-nvcsi, nvhost-vi5 and virtual-i2c-mux. It is only an
# MACHINE_EXTRA_RRECOMMENDS in tegra-common.inc, which means it arrives via
# packagegroup-base-extended -- an image built from packagegroup-core-boot alone
# does not get it, so name it here.
#
# nvidia-kernel-oot-base is already in MACHINE_ESSENTIAL_EXTRA_RDEPENDS; listed
# for clarity, not because it would otherwise be missing.
RDEPENDS:${PN} = " \
    kernel-module-imx585 \
    kernel-module-cef168 \
    imx585-overlay \
    nvidia-kernel-oot-base \
    nvidia-kernel-oot-cameras \
"

# 99-tegra-devices.rules; sets ownership on the nvhost/camera device nodes and
# creates the 'debug' group they are assigned to.
RDEPENDS:${PN} += "tegra-configs-udev"
