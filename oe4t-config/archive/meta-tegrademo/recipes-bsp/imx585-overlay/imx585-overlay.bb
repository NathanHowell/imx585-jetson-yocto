SUMMARY = "IMX585 camera overlay for Jetson Orin Nano"
DESCRIPTION = "Device tree overlay describing the IMX585 camera connection for Jetson Orin Nano boards."
HOMEPAGE = "https://github.com/NathanHowell/imx585-v4l2-driver"

LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=15dc9b4bc755528a250e2bba565d30c6"

require recipes-kernel/imx585/imx585-source.inc

FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

SRC_URI += "file://0001-imx585-overlay-Add-clock-and-link-settings.patch"

inherit devicetree

# devicetree.bbclass defaults S to ${UNPACKDIR}; use the unpacked source tree.
S = "${UNPACKDIR}/${BP}"

DEPENDS += "nvidia-kernel-oot"

# Ensure the NVIDIA platform headers and overlay sources are available to dtc.
DT_INCLUDE = " \
    ${RECIPE_SYSROOT}/usr/src/device-tree/nvidia/tegra/nv-public \
    ${RECIPE_SYSROOT}/usr/src/device-tree/nvidia/t23x/nv-public/include/kernel \
    ${RECIPE_SYSROOT}/usr/src/device-tree/nvidia/t23x/nv-public/include/nvidia-oot \
    ${RECIPE_SYSROOT}/usr/src/device-tree/nvidia/t23x/nv-public/include/platforms \
    ${RECIPE_SYSROOT}/usr/src/device-tree/nvidia/t23x/nv-public \
    ${RECIPE_SYSROOT}/usr/src/device-tree/nvidia/t23x/nv-public/nv-platform \
    ${S} \
    ${KERNEL_INCLUDE} \
"

# Preserve include ordering (dtc search path) when expanding globbed directories.
def expand_includes(varname, d):
    import glob
    includes = list()
    for i in (d.getVar(varname) or "").split():
        for g in glob.glob(i):
            if os.path.isdir(g):
                includes.append(g)
    return includes

# The overlay references Jetson-specific nodes; only build on Tegra machines.
COMPATIBLE_MACHINE = "(tegra)"

DT_FILES = "imx585-overlay.dts"

# Avoid conflicting with other recipes that provide the full device tree set.
PROVIDES:remove = "virtual/dtb"

PACKAGES = "${PN}"
FILES:${PN} = "/boot/devicetree/*.dtbo"

do_deploy:append() {
    install -Dm 0644 ${B}/imx585-overlay.dtbo ${DEPLOYDIR}/imx585-overlay.dtbo
}

