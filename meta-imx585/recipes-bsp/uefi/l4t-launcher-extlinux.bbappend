# l4t-launcher-extlinux bundles the DTB and overlays named by UBOOT_EXTLINUX_FDT /
# UBOOT_EXTLINUX_FDTOVERLAYS out of DEPLOY_DIR_IMAGE, but nothing in the task graph
# tells it to wait for the recipes that deploy them, so order them explicitly.
#
# Only imx585-overlay needs ordering: the base DTB is NVIDIA's stock
# -nv-super.dtb, which the kernel recipe deploys. See conf/distro/imx585.conf.
do_copy_dtb_overlays[depends] += " imx585-overlay:do_deploy"
