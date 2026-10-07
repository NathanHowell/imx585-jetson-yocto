SUMMARY = "IMX585 camera overlay for Jetson Orin Nano"
DESCRIPTION = "Device tree overlay describing the IMX585 camera connection for Jetson Orin Nano boards."
HOMEPAGE = "https://github.com/NathanHowell/imx585-v4l2-driver"

# The DTS carries SPDX-License-Identifier: GPL-2.0-only, which is what applies
# here -- the source repo's MIT LICENSE covered the driver, not this file.
LICENSE = "GPL-2.0-only"
LIC_FILES_CHKSUM = "file://imx585-overlay.dts;beginline=1;endline=1;md5=fcab174c20ea2e2bc0be64b493708266"

# Vendored, like the driver. This used to `require imx585-source.inc` and take the
# DTS out of the driver's git tree with a patch on top; that tree is no longer
# fetched, so the DTS lives here with the patch already applied.
#
# Targets will127534's StarlightEye V2.0 (4-lane, 22-pin FPC) on cam1.
#
# Corrected against the StarlightEye schematic and will127534's own Raspberry Pi
# overlay -- he wrote both the board and the driver -- and verified by compiling
# with `dtc -@`:
#
#   - cam1 (imx585_c, serial_c, 4 lanes, lane_polarity 0) is the enabled sensor;
#     cam0 is disabled, being 2-lane on the p3768 carrier. These had to change in
#     the file: __overrides__ is RPi firmware and inert under UEFI/extlinux.
#   - imx585_inck, a fixed-clock at 24 MHz, is U5 (SX2M24.000M20F30TNN) driving
#     IMX585 pin F4/INCK. The host clocks nothing; the node states a rate. Safe
#     because the driver only clk_get_rate()s and clk_prepare_enable()s it. The
#     "inck" name must match `mclk` -- parse_dt() reads that string and
#     power_get() does devm_clk_get(dev, pdata->mclk_name).
#   - link-frequencies = 720 MHz (1440 Mbps/lane), the value in the upstream RPi
#     overlay and a real entry in imx585_link_freq_table.
#   - csi_pixel_bit_depth = 12. The sensor is RAW12; "10" was a placeholder.
#   - On-board I2C peripherals on the cam1 leg: TMP117 at 0x48 enabled,
#     ICM-42688-P at 0x68 present but disabled (its driver mandates an interrupt
#     that the board does not route), CH32V003 IR-filter switch at 0x34
#     documented without a node. See PORTING.md.
#
# Still inaccurate rather than broken: the vana/vdig/vddl regulator-fixed nodes
# model all three rails as always-on, where upstream gates 3.3 V from the host
# (startup-delay-us = 300000) and uses a dummy regulator for the two on-board
# rails. Remaining items are in PORTING.md, "Still open on the DT".
SRC_URI = "file://imx585-overlay.dts"

inherit devicetree

S = "${UNPACKDIR}"

DEPENDS += "nvidia-kernel-oot"

# Ensure the NVIDIA platform headers and overlay sources are available to dtc.
#
# Paths VERIFIED against R39.2.1's staged tree. The layout is split, and not the
# way the carried-over list assumed:
#
#     tegra/nv-public/include/{kernel,nvidia-oot}
#     t23x/nv-public/include/{nvidia-oot,platforms}
#
# so `include/kernel` lives under tegra/, NOT under t23x/. The old list asked for
# t23x/nv-public/include/kernel, which does not exist, and omitted both tegra/
# include dirs. That was silent rather than fatal because expand_includes() below
# only appends directories that exist, so the bogus entry was dropped and
# <dt-bindings/clock/tegra234-clock.h> -- which this overlay includes -- was
# instead picked up from KERNEL_INCLUDE. It happens to be byte-identical to
# NVIDIA's copy today, so the overlay built correctly by luck. Listing the real
# directories ahead of KERNEL_INCLUDE makes NVIDIA's copies win on purpose, which
# is what we want if the two ever diverge.
DT_INCLUDE = " \
    ${RECIPE_SYSROOT}/usr/src/device-tree/nvidia/tegra/nv-public \
    ${RECIPE_SYSROOT}/usr/src/device-tree/nvidia/tegra/nv-public/include/kernel \
    ${RECIPE_SYSROOT}/usr/src/device-tree/nvidia/tegra/nv-public/include/nvidia-oot \
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

