FILESEXTRAPATHS:prepend := "${THISDIR}/${BPN}:"
SRC_URI += "file://imx585.cfg"
KERNEL_CONFIG_FRAGMENTS += "imx585.cfg"
