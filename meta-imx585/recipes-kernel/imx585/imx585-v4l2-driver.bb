SUMMARY = "IMX585 tegracam sensor driver"
DESCRIPTION = "Out-of-tree tegracam/camera_common kernel module for the Sony \
IMX585 image sensor on Tegra. Converted from will127534's upstream-style V4L2 \
driver; see PORTING.md for what the conversion kept, dropped and got wrong."
HOMEPAGE = "https://github.com/NathanHowell/imx585-v4l2-driver"

# The repo the source came from is MIT, but imx585.c itself carries
# SPDX-License-Identifier: GPL-2.0 and MODULE_LICENSE("GPL") -- which is what it
# has to be, since it links against GPL-only kernel and tegracam symbols.
LICENSE = "GPL-2.0-only"
LIC_FILES_CHKSUM = "file://imx585.c;beginline=1;endline=1;md5=50d2ba0afecd20f74c12a4bdbcfcfe61"

# The source is vendored, not fetched: the only copy of the tegracam conversion is
# a git bundle in oe4t-config/archive/, which is the single-disk failure mode this
# repo exists to avoid.
#
# Provenance: 13947db8c5bbd5d18694f5a2de93a625e957c928 ("imx585: seed
# sensor_mode_properties for tegracam"), devtool branch tip of
# imx585-v4l2-driver-devtool.bundle.
SRC_URI = " \
    file://imx585.c \
    file://Makefile \
"

# Our own version, not a revision of anything. A +git<sha> suffix would claim the
# source *is* that commit, and there is no upstream Jetson/tegracam IMX585 driver
# to track -- see PORTING.md, "Maintenance model: this is the upstream".
# Provenance belongs in the SRC_URI comment above. Bump this when the driver
# changes meaningfully.
PV = "1.0"

S = "${UNPACKDIR}"

inherit module

# kernel-module-nvidia-kernel-oot, NOT nvidia-kernel-oot. The name matters: it is
# what makes module.bbclass populate KBUILD_EXTRA_SYMBOLS for us (see below).
# nvidia-kernel-oot.inc carries PROVIDES += "kernel-module-${BPN}", so this
# resolves to the same recipe.
DEPENDS += "virtual/kernel kernel-module-nvidia-kernel-oot"

COMPATIBLE_MACHINE = "(tegra)"

# Where nvidia-kernel-oot stages the tegracam headers and the symbol table for
# the modules that export tegracam_*. Both VERIFIED against meta-tegra wrynose
# (322bc23), recipes-kernel/nvidia-kernel-oot/nvidia-kernel-oot.inc do_install:
#
#   install -d ${D}${includedir}/${BPN}
#   find ${B} -name Module.symvers -type f | xargs sed ... >${D}${includedir}/${BPN}/Module.symvers
#   cp -R ${S}/nvidia-oot/include/* ${D}/${includedir}/${BPN}
#
# BPN is nvidia-kernel-oot, so <media/camera_common.h> resolves under
# ${STAGING_INCDIR}/nvidia-kernel-oot/media/. They land in FILES:${PN}-dev, which
# DEPENDS pulls into the recipe sysroot. Kept as variables anyway, so there is
# one place to change if R39.3 moves them.
NVIDIA_OOT_INCDIR ?= "${STAGING_INCDIR}/nvidia-kernel-oot"
# Checked by do_configure below. This is the same file module.bbclass passes, via
# the kernel-module-nvidia-kernel-oot symlink beside it.
NVIDIA_OOT_SYMVERS ?= "${NVIDIA_OOT_INCDIR}/Module.symvers"

EXTRA_OEMAKE += "NVIDIA_OOT_INCDIR=${NVIDIA_OOT_INCDIR}"

# KBUILD_EXTRA_SYMBOLS is deliberately NOT set here. Setting it has no effect:
# module.bbclass has
#
#     python __anonymous () {
#         depends = d.getVar('DEPENDS')
#         extra_symbols = []
#         for dep in depends.split():
#             if dep.startswith("kernel-module-"):
#                 extra_symbols.append("${STAGING_INCDIR}/" + dep + "/Module.symvers")
#         d.setVar('KBUILD_EXTRA_SYMBOLS', " ".join(extra_symbols))
#     }
#
# That setVar runs at parse finalisation and overwrites any value the recipe
# assigned, so the only way to get entries into it is to name a DEPENDS that
# starts with "kernel-module-". Setting it directly, or via EXTRA_OEMAKE, builds a
# module with every tegracam_* symbol undefined and no visible cause.
#
# Hence DEPENDS naming kernel-module-nvidia-kernel-oot, which the class turns into
#     ${STAGING_INCDIR}/kernel-module-nvidia-kernel-oot/Module.symvers
# and which resolves because nvidia-kernel-oot.inc's do_install creates
#     ln -s ${BPN} ${D}${includedir}/kernel-module-${BPN}
# precisely so this convention works. VERIFIED: that symlink is in the sysroot and
# the Module.symvers behind it carries the 16 tegracam_* exports.

do_configure:prepend() {
    if [ ! -d "${NVIDIA_OOT_INCDIR}/media" ]; then
        bbfatal "No media/ under NVIDIA_OOT_INCDIR (${NVIDIA_OOT_INCDIR}).\n\
This is the staging path for nvidia-kernel-oot's tegracam headers and it is a\n\
guess carried over from R36.4. Find where wrynose's nvidia-kernel-oot installs\n\
camera_common.h and tegracam_core.h, then set NVIDIA_OOT_INCDIR to it:\n\
  bitbake -e nvidia-kernel-oot | grep -i '^S=\|sysroot'\n\
  find \${STAGING_DIR_TARGET} -name tegracam_core.h"
    fi
    if [ ! -f "${NVIDIA_OOT_SYMVERS}" ]; then
        bbwarn "No Module.symvers at ${NVIDIA_OOT_SYMVERS}. The module will\
 build, but every tegracam_* symbol will be undefined and it will not load.\
 Locate nvidia-kernel-oot's Module.symvers and set NVIDIA_OOT_SYMVERS."
    fi
}

KERNEL_MODULE_PACKAGE_SUFFIX = ""

PACKAGES += "kernel-module-imx585"
FILES:kernel-module-imx585 = "${base_libdir}/modules/${KERNEL_VERSION}/kernel/drivers/media/i2c/imx585.ko"
FILES:${PN} = ""
INSANE_SKIP:${PN} += "installed-vs-shipped"
INSANE_SKIP:${PN}-dbg += "usrmerge buildpaths"
