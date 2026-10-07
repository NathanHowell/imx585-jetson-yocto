# Shared between imx585-console-image and imx585-minimal-image: one OCI container
# engine with CUDA passthrough, its image store on the persistent data partition.
#
# meta-virtualization has to be in bblayers for the engine to exist as a recipe
# at all. kas/include/podman.yml adds it and is part of the default build;
# kas/include/docker.yml swaps the engine.

# Exactly one engine. podman (daemonless: podman + crun + conmon) is roughly
# 80-120 MB smaller installed than docker (dockerd + containerd + runc +
# docker-cli + bridge-utils + full util-linux).
#
# These cannot both be installed: meta-virtualization's podman recipe has
# PODMAN_FEATURES = "docker", which installs a ${bindir}/docker wrapper. Its
# RCONFLICTS is keyed off PACKAGECONFIG rather than PODMAN_FEATURES, so the
# conflict is not declared -- it surfaces as a file collision on /usr/bin/docker
# at rootfs time instead. Set PODMAN_FEATURES = "" if you ever need both.
IMX585_CONTAINER_ENGINE ?= "podman ca-certificates"

# CUDA inside containers. This is the expensive part of either image.
#
# nvidia-container-toolkit RDEPENDS libnvidia-container-tools, which RDEPENDS
# tegra-libraries-cuda (libcuda, libnvidia-nvvm, libnvidia-ptxjitcompiler, which
# in turn pull tegra-libraries-core), plus tegra-libraries-nvml and
# tegra-container-passthrough. The weight is in tegra-libraries-cuda: four named
# libraries out of the 137 MB nvidia-l4t-3d-core deb, plus libcuda.so.1.1 from
# the 22 MB nvidia-l4t-cuda-nvgpu deb.
#
# tegra-container-passthrough is NOT the expensive one -- see kas/README.md. It
# stages the L4T wayland, weston and gstreamer shared libraries under
# ${datadir}/nvidia-container-passthrough to be bind-mounted into containers;
# those three debs are 55 KB, 1.6 MB and 2.5 MB compressed.
#
# nv-kernel-module-nvgpu is the Orin GPU driver and the one thing CUDA cannot
# work without. tegra-libraries-cuda only RRECOMMENDS it, so name the package
# that hard-depends on it. nvidia-kernel-oot-compute (as opposed to
# -compute-nvgpu) is deliberately absent: that is nvidia-uvm, which belongs to
# the open-RM/tegra264 path and would drag the display modules in behind it.
IMX585_CONTAINER_GPU ?= "nvidia-container-toolkit nvidia-kernel-oot-compute-nvgpu"

# imx585-container-config: containers read-only by default, the CDI spec
# regenerated into /run at boot, and (through the container-host-config
# bbappend it depends on) podman's graphroot on IMX585_DATA_MOUNT.
IMAGE_INSTALL:append = " ${IMX585_CONTAINER_ENGINE} ${IMX585_CONTAINER_GPU} imx585-container-config"
