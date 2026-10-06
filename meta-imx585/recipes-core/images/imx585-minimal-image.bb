SUMMARY = "Minimal IMX585 + CEF168 image with one container engine"
DESCRIPTION = "A core-image-minimal-sized rootfs that boots the Orin Nano, \
binds the IMX585 sensor and the CEF168 lens controller, and runs exactly one \
OCI container engine. Nothing else."
LICENSE = "MIT"

inherit core-image

require imx585-ssh-user.inc

# IMAGE_INSTALL is set, not appended to CORE_IMAGE_BASE_INSTALL, on purpose.
# CORE_IMAGE_BASE_INSTALL would add packagegroup-base-extended, which drags in
# MACHINE_EXTRA_RDEPENDS and MACHINE_EXTRA_RRECOMMENDS from
# meta-tegra/conf/machine/include/tegra-common.inc: nvidia-kernel-oot-display,
# tegra-configs-display-driver, tegra-nvfancontrol, tegra-nvsciipc,
# tegra-redundant-boot, nvidia-kernel-oot-alsa, nvidia-kernel-oot-canbus and the
# fifteen Tegra ASoC module packages. None of that is needed to capture frames.
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

# dropbear, not openssh: ~0.5 MB against ~4 MB, and it reads the same
# ~/.ssh/authorized_keys that imx585-ssh-user.inc writes.
# No package-management: that would put the rpm binary and its database in the
# rootfs for no benefit on an image that is reflashed rather than updated.
IMAGE_FEATURES = "ssh-server-dropbear"

# For bring-up on the serial console, when there is no key yet:
# IMAGE_FEATURES += "empty-root-password allow-root-login serial-autologin-root"

KERNEL_MODULE_AUTOLOAD:append = " imx585 cef168"

IMAGE_LINGUAS = ""

IMAGE_ROOTFS_SIZE ?= "8192"
IMAGE_ROOTFS_EXTRA_SPACE:append = "${@bb.utils.contains("DISTRO_FEATURES", "systemd", " + 4096", "", d)}"
