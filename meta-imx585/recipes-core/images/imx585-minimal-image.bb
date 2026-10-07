SUMMARY = "Minimal headless IMX585 + CEF168 image with CUDA-capable containers"
DESCRIPTION = "A core-image-minimal-sized rootfs that boots the Orin Nano \
headless, binds the IMX585 sensor and the CEF168 lens controller, and runs one \
OCI container engine with GPU/CUDA and USB audio passthrough. Nothing else."
LICENSE = "MIT"

inherit core-image

require imx585-ssh-user.inc
require imx585-containers.inc

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
    ${IMX585_USB_AUDIO} \
    ${CORE_IMAGE_EXTRA_INSTALL} \
"

# The container engine and nvidia-container-toolkit come from
# imx585-containers.inc, appended to IMAGE_INSTALL.

# USB Audio Class device, to be passed into a container with
# `--device /dev/snd`. snd-usb-audio autoloads from the USB modalias once udev
# is up, so it is not in KERNEL_MODULE_AUTOLOAD; `cat /proc/asound/cards`
# confirms enumeration without any userspace ALSA packages installed.
IMX585_USB_AUDIO ?= "kernel-module-snd-usb-audio"

# Periodic TRIM, for the container store on the data partition more than for the
# rootfs, which barely writes. The package exists in oe-core with its systemd
# timer disabled; meta-imx585's util-linux bbappend enables it.
IMAGE_INSTALL:append = " util-linux-fstrim"

# Read-only rootfs plumbing: the data.mount unit for the persistent partition,
# journald held in RAM, dropbear host keys moved off /etc.
IMAGE_INSTALL:append = " imx585-node-config"

# Firmware TPM 2.0. OPTEE_ENABLE_FTPM in imx585.conf is what builds the fTPM and
# its helper into OP-TEE as early TAs; these are the normal-world halves.
# optee-client brings tee-supplicant and the tee-ftpm-modprobe unit,
# kernel-module-tpm-ftpm-tee is the /dev/tpm0 driver, and imx585-ftpm-config puts
# OP-TEE secure storage on the data partition so the TPM keeps its keys.
#
# The umbrella optee-nvsamples package is deliberately not used: it would also
# bring luks-srv, hwkey-agent and pkcs11-sample host apps that nothing here calls.
IMX585_FTPM ?= " \
    optee-client \
    optee-ftpm \
    optee-nvsamples-ftpm-helper \
    kernel-module-tpm-ftpm-tee \
    imx585-ftpm-config \
    tpm2-tools \
    openssl-bin \
    tpm2-openssl \
"
# openssl-bin + tpm2-openssl are here for one job: enrolling this node's client
# identity. The key is created inside the fTPM and cannot leave it, so the CSR
# has to be signed on the node, which means an OpenSSL that can drive the TPM:
#   openssl genpkey -provider tpm2 ... -out /data/pki/node.tss2.key
# See "Using it as a client identity" in kas/README.md. The resulting TSS2 PEM is
# a wrapped blob, not a key, so it is safe on /data and safe to bind-mount into a
# read-only container. tpm2-abrmd is deliberately absent -- the in-kernel
# resource manager at /dev/tpmrm0 supersedes it.
IMAGE_INSTALL:append = " ${IMX585_FTPM}"

# dropbear, not openssh: ~0.5 MB against ~4 MB, and it reads the same
# ~/.ssh/authorized_keys that imx585-ssh-user.inc writes.
# No package-management: that would put the rpm binary and its database in the
# rootfs for no benefit on an image that is reflashed rather than updated, and it
# is meaningless against a read-only rootfs anyway.
IMAGE_FEATURES = "ssh-server-dropbear read-only-rootfs"

# read-only-rootfs makes oe-core's read_only_rootfs_hook rewrite the /dev/root
# line in /etc/fstab from "defaults" to "ro", which is what actually enforces it
# per image. The hook also appends "ro" to APPEND, which does nothing here: on
# Tegra the kernel command line comes from UBOOT_EXTLINUX_KERNEL_ARGS in the
# separate l4t-launcher-extlinux recipe. imx585.conf handles that end.
#
# If this build fails on a package whose postinst must run on the target, the
# lever is IMAGE_FEATURES += "read-only-rootfs-delayed-postinsts" -- but read the
# postinst first, because a deferred one has nowhere to record that it ran.

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
