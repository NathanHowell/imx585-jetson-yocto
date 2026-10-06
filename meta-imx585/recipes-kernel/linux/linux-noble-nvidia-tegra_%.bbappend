# NVIDIA vendor kernel (6.8.12, JetPack 7.2.1). linux-noble-nvidia-tegra
# requires linux-yocto.inc, so plain .cfg fragments in SRC_URI work here.
FILESEXTRAPATHS:prepend := "${THISDIR}/files:"
SRC_URI += "file://imx585.cfg file://usb-audio.cfg"
