# l4t-launcher-extlinux bundles the DTB and overlays named by UBOOT_EXTLINUX_FDT /
# UBOOT_EXTLINUX_FDTOVERLAYS out of DEPLOY_DIR_IMAGE, but nothing in the task graph
# tells it to wait for the recipes that deploy them, so order them explicitly.
#
# Only imx585-overlay is needed. imx585-devicetree used to be listed here because
# UBOOT_EXTLINUX_FDT named its tegra234-p3768-0000+p3767-0005-oe4t.dtb; we now boot
# NVIDIA's stock -nv-super.dtb, which the kernel recipe deploys. See
# conf/distro/imx585.conf and PORTING.md, "The base DTB: decided".
do_copy_dtb_overlays[depends] += " imx585-overlay:do_deploy"
