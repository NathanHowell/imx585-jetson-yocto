# Only used when kas/include/kernel-linux-yocto.yml is layered in.
FILESEXTRAPATHS:prepend := "${THISDIR}/files:"
SRC_URI += "file://imx585.cfg file://usb-audio.cfg file://containers.cfg"
KERNEL_CONFIG_FRAGMENTS += "imx585.cfg usb-audio.cfg containers.cfg"
