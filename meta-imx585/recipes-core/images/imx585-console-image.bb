DESCRIPTION = "Console image for Jetson Orin Nano IMX585 + CEF168 bring-up"
LICENSE = "MIT"

inherit core-image

require imx585-ssh-user.inc

IMAGE_FEATURES += "ssh-server-openssh package-management"

CORE_IMAGE_BASE_INSTALL += "packagegroup-imx585 bash-completion"

# dtc/fdtget for inspecting the live device tree on target; rtcpu-debug exposes
# the camera RTCPU trace log, which is where VI/NVCSI capture errors surface.
IMAGE_INSTALL:append = " tegra-tools-tegrastats dtc nv-kernel-module-rtcpu-debug"

KERNEL_MODULE_AUTOLOAD:append = " imx585 cef168"

IMAGE_LINGUAS = "en-us"
