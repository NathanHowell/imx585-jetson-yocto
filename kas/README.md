# kas build configuration

Declarative replacement for the old `tegra-demo-distro` checkout. Every layer
pin and every `local.conf` setting lives in git here, which is the one thing the
old setup got wrong: its `build/conf` was covered by `build*/` in
`.gitignore`, so the configuration that mattered existed only on one disk.

## Platform

| | |
|---|---|
| Yocto release | wrynose (6.0 LTS) |
| meta-tegra branch | `wrynose` |
| JetPack / Jetson Linux | 7.2.1 / R39.2.1 |
| Kernel (default) | `linux-noble-nvidia-tegra` 6.8.12 |
| Machine | `jetson-orin-nano-devkit-nvme` |
| Distro | `imx585` (`meta-imx585/conf/distro/imx585.conf`) |

There is no poky repo. The poky combo repo has no `wrynose` branch — its newest
release branch is `walnascar` (5.2) — so the base is `openembedded-core`
(`wrynose`) plus `bitbake` (`2.18`, per wrynose's `BB_MIN_VERSION = "2.18.0"`).
That matches what meta-tegra declares: `LAYERDEPENDS_tegra = "core"`.

There is no meta-tegra-community either. `nvidia-container-toolkit` and
`libnvidia-container` are in **meta-tegra**, under
`external/virtualization-layer/`, which meta-tegra publishes through
`BBFILES_DYNAMIC` as soon as a layer named `virtualization-layer` is present.
meta-tegra-community only has an `nvidia-docker-tests` recipe.

## Usage

```sh
pipx install kas          # not currently installed on this machine

kas build kas/imx585.yml                                     # bring-up image
kas shell kas/imx585.yml                                     # bitbake prompt
kas build kas/imx585.yml:kas/include/podman.yml               # minimal + podman
kas build kas/imx585.yml:kas/include/docker.yml               # minimal + docker
kas build kas/imx585.yml:kas/include/kernel-linux-yocto.yml   # mainline 6.18
```

Set `DL_DIR` and `SSTATE_DIR` in `imx585.yml` before the first build. The old
tree reached 266G because they defaulted into it.

## Files

| | |
|---|---|
| `imx585.yml` | the build: distro, machine, target, `local.conf` |
| `include/base.yml` | layer pins shared by all configs |
| `include/kernel-linux-yocto.yml` | opt in to mainline linux-yocto 6.18 |
| `include/podman.yml` | minimal image + podman (recommended engine) |
| `include/docker.yml` | minimal image + docker instead |
| `include/nvidia-container-toolkit.yml` | GPU/CUDA/Argus passthrough into containers |

## Images

| | |
|---|---|
| `imx585-console-image` | bring-up: openssh, rpm on target, `kernel-modules`, strace/trace-cmd/bpftrace |
| `imx585-minimal-image` | `packagegroup-core-boot` + the camera + one container engine, nothing else |

`imx585-minimal-image` installs `packagegroup-core-boot` directly instead of
`CORE_IMAGE_BASE_INSTALL`. That one line is most of the size difference: going
through `packagegroup-base-extended` pulls `MACHINE_EXTRA_RDEPENDS` and
`MACHINE_EXTRA_RRECOMMENDS` out of meta-tegra's `tegra-common.inc`, which is
`nvidia-kernel-oot-display`, `tegra-configs-display-driver`,
`tegra-nvfancontrol`, `tegra-nvsciipc`, `nvidia-kernel-oot-alsa`,
`nvidia-kernel-oot-canbus` and fifteen Tegra ASoC module packages.

`nvidia-kernel-oot-cameras` is in that same `MACHINE_EXTRA_RRECOMMENDS` list, so
the minimal image has to name it explicitly — `packagegroup-imx585-camera` does.

## Container engine

podman, not docker. It is daemonless (one Go binary plus `crun` and `conmon`,
both C) where docker is `dockerd` + `containerd` + `runc` + `docker-cli`, four Go
binaries, and additionally RDEPENDS the full `util-linux` and `bridge-utils`.
Both are reachable: `include/docker.yml` swaps them. They are mutually exclusive
in practice — meta-virtualization's podman installs a `/usr/bin/docker` wrapper
and its `RCONFLICTS` is keyed off `PACKAGECONFIG` rather than `PODMAN_FEATURES`,
so installing both collides on that file at rootfs time instead of failing
cleanly.

`nvidia-container-toolkit` is a separate include because it is engine-agnostic
and because it is expensive: it RDEPENDS `libnvidia-container-tools` →
`tegra-libraries-cuda`, plus `tegra-libraries-nvml` and
`tegra-container-passthrough`, and that last one stages all of
`/usr/lib/aarch64-linux-gnu` from the L4T camera, wayland, weston and gstreamer
debs just to bind-mount it into containers. Raw V4L2 capture inside a container
needs none of it — `--device /dev/video0 --device /dev/i2c-*` is enough.

## On the kernel choice

`linux-noble-nvidia-tegra` is already meta-tegra's
`PREFERRED_PROVIDER_virtual/kernel` on wrynose, so the default config sets no
kernel preference at all — taking the default *is* the NVIDIA-supported kernel.

Switching to mainline does not forfeit the NVIDIA camera stack, which is what
the earlier notes assumed. `nvidia-kernel-oot` is `COMPATIBLE_MACHINE =
"(tegra)"` with no kernel restriction, and the only modules it skips against
linux-yocto are two Realtek wifi drivers
(`TEGRA_OOT_MODULE_SKIP_MAKEFLAGS_LINUX_YOCTO`). tegracam, VI, NVCSI and
`tegra-camera-platform` build either way. Verified by reading the recipe, not
by building it.
