SUMMARY = "Minimal headless IMX585 + CEF168 image with CUDA-capable containers"
DESCRIPTION = "A core-image-minimal-sized rootfs that boots the Orin Nano \
headless, binds the IMX585 sensor and the CEF168 lens controller, and runs one \
OCI container engine with GPU/CUDA and USB audio passthrough. Nothing else."
LICENSE = "MIT"

inherit core-image

require imx585-ssh-user.inc

# IMAGE_INSTALL is set, not appended to CORE_IMAGE_BASE_INSTALL, on purpose.
# CORE_IMAGE_BASE_INSTALL would add packagegroup-base-extended, which drags in
# MACHINE_EXTRA_RDEPENDS and MACHINE_EXTRA_RRECOMMENDS from
# meta-tegra/conf/machine/include/tegra-common.inc: nvidia-kernel-oot-display,
# tegra-configs-display-driver, tegra-nvfancontrol, tegra-nvsciipc,
# tegra-redundant-boot, nvidia-kernel-oot-alsa, nvidia-kernel-oot-canbus and the
# fifteen Tegra ASoC module packages. None of that is needed here.
#
# packagegroup-core-boot still brings MACHINE_ESSENTIAL_EXTRA_RDEPENDS
# (tegra-firmware, l4t-launcher-extlinux, nvidia-kernel-oot-base), which is what
# makes the board boot, and MACHINE_ESSENTIAL_EXTRA_RRECOMMENDS, which is where
# kernel-module-nvme comes from -- do not set NO_RECOMMENDATIONS here or the
# NVMe rootfs stops being reachable.
IMAGE_INSTALL = " \
    packagegroup-core-boot \
    packagegroup-imx585-camera \
    ${IMX585_CONTAINER_ENGINE} \
    ${IMX585_CONTAINER_GPU} \
    ${IMX585_USB_AUDIO} \
    ${CORE_IMAGE_EXTRA_INSTALL} \
"

# Exactly one engine. podman (daemonless: podman + crun + conmon) is roughly
# 80-120 MB smaller installed than docker (dockerd + containerd + runc +
# docker-cli + bridge-utils + full util-linux). Override from local.conf or a kas
# include to switch -- see kas/include/podman.yml and kas/include/docker.yml.
#
# These cannot both be installed: meta-virtualization's podman recipe has
# PODMAN_FEATURES = "docker", which installs a ${bindir}/docker wrapper. Its
# RCONFLICTS is keyed off PACKAGECONFIG rather than PODMAN_FEATURES, so the
# conflict is not declared -- it surfaces as a file collision on /usr/bin/docker
# at rootfs time instead. Set PODMAN_FEATURES = "" if you ever need both.
IMX585_CONTAINER_ENGINE ?= "podman ca-certificates"

# Either way, meta-virtualization has to be in bblayers for the engine to exist
# as a recipe at all. kas/include/podman.yml and kas/include/docker.yml add it;
# building this image without one of them fails on a missing provider.

# CUDA inside containers. This is the expensive part of the image and it is not
# optional here, because that is the stated requirement.
#
# nvidia-container-toolkit RDEPENDS libnvidia-container-tools, which RDEPENDS
# tegra-libraries-cuda (libcuda, libnvidia-nvvm, libnvidia-ptxjitcompiler, which
# in turn pull tegra-libraries-core), plus tegra-libraries-nvml and
# tegra-container-passthrough. That last one stages all of
# /usr/lib/aarch64-linux-gnu from the L4T camera, wayland, weston and gstreamer
# debs under ${datadir}/nvidia-container-passthrough purely to be bind-mounted
# into containers, and it is the single biggest item in the rootfs. See
# kas/README.md for how to measure and trim it on a headless target.
#
# nv-kernel-module-nvgpu is the Orin GPU driver and the one thing CUDA cannot
# work without. tegra-libraries-cuda only RRECOMMENDS it, so name the package
# that hard-depends on it. nvidia-kernel-oot-compute (as opposed to
# -compute-nvgpu) is deliberately absent: that is nvidia-uvm, which belongs to
# the open-RM/tegra264 path and would drag the display modules in behind it.
IMX585_CONTAINER_GPU ?= "nvidia-container-toolkit nvidia-kernel-oot-compute-nvgpu"

# USB Audio Class device, to be passed into a container with
# `--device /dev/snd`. snd-usb-audio autoloads from the USB modalias once udev
# is up, so it is not in KERNEL_MODULE_AUTOLOAD; `cat /proc/asound/cards`
# confirms enumeration without any userspace ALSA packages installed.
IMX585_USB_AUDIO ?= "kernel-module-snd-usb-audio"

# dropbear, not openssh: ~0.5 MB against ~4 MB, and it reads the same
# ~/.ssh/authorized_keys that imx585-ssh-user.inc writes.
# No package-management: that would put the rpm binary and its database in the
# rootfs for no benefit on an image that is reflashed rather than updated.
IMAGE_FEATURES = "ssh-server-dropbear"

# For bring-up on the serial console, when there is no key yet:
# IMAGE_FEATURES += "empty-root-password allow-root-login serial-autologin-root"

# nvgpu is normally modprobed by nv-load-display-modules, which comes in the
# tegra-configs-display-driver package. That script also unconditionally
# modprobes nvidia_drm, so on an image with no display modules installed it
# fails and takes systemd-modules-load.service down with it. Loading nvgpu
# directly here replaces it. If nvgpu turns out to need options from the L4T
# /etc/modprobe.d/nvgpu.conf -- which ships in that same package and cannot be
# read without unpacking the deb -- install tegra-configs-display-driver and
# accept the failing unit, or add nvidia-kernel-oot-display too.
KERNEL_MODULE_AUTOLOAD:append = " imx585 cef168 nvgpu"

IMAGE_LINGUAS = ""

IMAGE_ROOTFS_SIZE ?= "8192"
IMAGE_ROOTFS_EXTRA_SPACE:append = "${@bb.utils.contains("DISTRO_FEATURES", "systemd", " + 4096", "", d)}"
