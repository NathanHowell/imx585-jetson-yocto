DESCRIPTION = "Console image for Jetson Orin Nano IMX585 + CEF168 bring-up"
LICENSE = "MIT"

inherit core-image

require imx585-ssh-user.inc
require imx585-containers.inc

IMAGE_FEATURES += "ssh-server-openssh package-management"

# Bring-up image: root logs in on the serial console with no password. The
# minimal image keeps root locked and has only the key-only SSH user.
IMAGE_FEATURES += "empty-root-password allow-empty-password allow-root-login"

CORE_IMAGE_BASE_INSTALL += "packagegroup-imx585 bash-completion"

# dtc/fdtget for inspecting the live device tree on target; rtcpu-debug exposes
# the camera RTCPU trace log, which is where VI/NVCSI capture errors surface.
IMAGE_INSTALL:append = " tegra-tools-tegrastats dtc nv-kernel-module-rtcpu-debug"

# Same storage layout as the minimal image: the persistent data partition is
# created from the NVMe's free space on first boot and mounted at
# IMX585_DATA_MOUNT, which is also where OP-TEE secure storage and the podman
# image store live. The dropbear drop-ins the package carries are inert here, as
# this image uses openssh.
#
# nvgpu, which CUDA containers need, is loaded here by nv-load-display-modules
# from tegra-configs-display-driver, which packagegroup-base-extended brings in.
IMAGE_INSTALL:append = " imx585-node-config"

KERNEL_MODULE_AUTOLOAD:append = " imx585 cef168"

IMAGE_LINGUAS = "en-us"
