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

## Images

| | |
|---|---|
| `imx585-console-image` | bring-up: openssh, rpm on target, `kernel-modules`, strace/trace-cmd/bpftrace |
| `imx585-minimal-image` | headless: `packagegroup-core-boot` + the camera + one container engine with CUDA and USB audio, nothing else |

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

## CUDA in containers

`nvidia-container-toolkit` is installed unconditionally, because containers here
need CUDA. It RDEPENDS `libnvidia-container-tools` → `tegra-libraries-cuda`
(`libcuda`, `libnvidia-nvvm`, `libnvidia-ptxjitcompiler`, pulling
`tegra-libraries-core`), plus `tegra-libraries-nvml` and
`tegra-container-passthrough`.

The weight is in `tegra-libraries-cuda`. It installs four named libraries out of
the **137 MB** `nvidia-l4t-3d-core` deb, plus `libcuda.so.1.1` and
`libcuda_instrumentation.so` from the 22 MB `nvidia-l4t-cuda-nvgpu` deb.
`tegra-libraries-core` adds a further 24 libraries from a 4.0 MB deb, and
`tegra-libraries-nvml` one library plus `nvidia-smi` from a 1.2 MB deb. Those are
compressed deb sizes, not installed sizes — the recipes install named subsets, so
the installed figure is smaller and I have not measured it.

### There is no gstreamer in this image

`tegra-container-passthrough` is where the gstreamer, wayland and weston names
come from, and it is not the expensive part. It unpacks those L4T debs and stages
their `/usr/lib/aarch64-linux-gnu` trees under
`${datadir}/nvidia-container-passthrough`, where `nvidia-container-toolkit`'s CSV
mode bind-mounts them into containers that want them. Measured against NVIDIA's
feed at `39.2.1-20260806224157`:

| deb | compressed |
|---|---|
| `nvidia-l4t-wayland` | 55 KB |
| `nvidia-l4t-weston` | 1.6 MB |
| `nvidia-l4t-gstreamer` | 2.5 MB |

That is the whole headless-irrelevant payload: about 4 MB of debs. No gstreamer
*installation* exists in the rootfs — no `gstreamer1.0` core, no plugin registry,
nothing on the loader path. These are inert files waiting to be mounted
elsewhere, so trimming them with a bbappend would buy single-digit MB and risk
breaking any container that does want NVIDIA gstreamer or Weston. Not worth it.

### How the staged tree is consumed

`tegra-configs-container-csv` installs two files into
`/etc/nvidia-container-runtime/host-files-for-container.d/`. They are the whole
interface, and they are in meta-tegra's git, not inside a deb:

| | |
|---|---|
| `devices.csv` | 43 `dev,` lines — `/dev/nvhost-*-gpu`, `/dev/nvgpu/igpu0/*`, `/dev/nvmap`, `/dev/nvsciipc`, `/dev/v4l2-nvdec`, `/dev/v4l2-nvenc`, `/dev/dri/*` |
| `drivers.csv` | ~250 `lib,` / `sym,` lines naming host files to bind-mount and symlinks to recreate inside the container |

`nvidia-ctk cdi generate --mode=csv` walks those lines and writes a CDI spec; the
engine then applies the mounts. The container image ships **no** NVIDIA
userspace — it is version-locked to the host driver, so it is injected at
start-up. That is the entire design of `nvcr.io/nvidia/l4t-*`.

The paths in `drivers.csv` fall into two families, and this is why the shadow
root exists:

- **`/usr/lib/...`** — satisfied straight out of this image's real rootfs,
  because OE installs to `${libdir}` = `/usr/lib` with no Debian multiarch
  triplet. `libcuda.so.1.1`, `libnvidia-nvvm`, `libnvidia-ptxjitcompiler`,
  `libnvrm_*`, `libnvos`, `libnvsciipc`, `libnvidia-ml.so.1`, `nvidia-smi` all
  land exactly where the CSV expects them.
- **`/usr/lib/aarch64-linux-gnu/...`** — paths that can never exist on an OE
  rootfs. These are what `tegra-container-passthrough` stages under
  `${datadir}/nvidia-container-passthrough`, and what meta-tegra's
  `0001-Add-support-for-alternate-roots-for-tegra-CSV-handli.patch` teaches the
  toolkit to look for, via the `alt-roots` setting that
  `nvidia-container-setup.service` writes at boot.

The gstreamer entries are in that second family:
`gstreamer-1.0/libgstnvarguscamerasrc.so`, `libgstnvvideo4linux2.so`,
`libgstnvvidconv.so`, `libgstnvjpeg.so`, `libgstnvcompositor.so`,
`libgstnvv4l2camerasrc.so`, the sinks, plus `libgstnvegl-1.0.so.0`,
`libgstnvexifmeta.so` and `nvidia/libgstnvcustomhelper.so*`. They mount to
`/usr/lib/aarch64-linux-gnu/gstreamer-1.0/`, which is where a Debian or Ubuntu
container's gstreamer already scans — so the intended container is **Ubuntu noble
arm64 with its own `gstreamer1.0` core from apt**, which then finds
`nvvidconv`, `nvv4l2decoder`, `nvv4l2h264enc` and friends as if they were
installed. The plugins are only plugins; nothing in the staged tree provides
`libgstreamer-1.0.so.0`.

### What will not work as built

Those gstreamer plugins will mount but fail to load. Their L4T dependencies are
`/usr/lib/...`-family entries that this image does not install, and CSV discovery
skips missing entries rather than failing, so the symptom is `gst-inspect-1.0`
reporting the plugin as broken, not a container that refuses to start:

| needed by | host recipe to add |
|---|---|
| `libnvargus.so` + the `nvargus-daemon` socket, for `nvarguscamerasrc` | `tegra-libraries-camera`, `tegra-argus-daemon` |
| `libtegrav4l2.so`, `libv4l/plugins/libv4l2_nvvideocodec.so`, for `nvv4l2decoder`/`nvv4l2*enc` | `tegra-libraries-multimedia-v4l` |
| `libnvmm*`, `libnvbufsurface`, `libnvbufsurftransform` | `tegra-libraries-multimedia`, `tegra-libraries-multimedia-utils` |

Both `tegra-libraries-camera` and `tegra-libraries-multimedia-v4l` are
`REQUIRED_DISTRO_FEATURES = "opengl"`, which is the concrete reason `opengl`
stayed in `DISTRO_FEATURES` on a headless target — see **Headless** below.

I did not add any of them: CUDA was the stated requirement and these are a
different one. `/dev/v4l2-nvdec` and `/dev/v4l2-nvenc` are a separate open
question — no `nvidia-kernel-oot` package in wrynose carries an nvdec or nvenc
module, so on 6.8 they appear to have moved in-tree like the Tegra ASoC drivers
did. Unverified.

The GPU kernel driver is `nv-kernel-module-nvgpu`, which `tegra-libraries-cuda`
only *recommends*; the image installs `nvidia-kernel-oot-compute-nvgpu`, which
depends on it. `nvidia-kernel-oot-compute` is a different thing —
`nvidia-uvm`, the open-RM/tegra264 path — and would drag the display modules in
behind it.

`nvgpu` is normally loaded by `nv-load-display-modules` from
`tegra-configs-display-driver`, a script that also unconditionally modprobes
`nvidia_drm`. With no display modules installed that fails and takes
`systemd-modules-load.service` with it, so the image puts `nvgpu` in
`KERNEL_MODULE_AUTOLOAD` instead. The one unknown is whether `nvgpu` needs
options from the L4T `/etc/modprobe.d/nvgpu.conf`, which ships in that same
package and cannot be read without unpacking the deb.

## Headless

`x11`, `wayland` and `vulkan` are in `DISTRO_FEATURES_OPTED_OUT`.

`opengl` is not, and that is deliberate. It is not a display feature in this
tree — it is the `REQUIRED_DISTRO_FEATURES` gate on `tegra-mmapi`, which is
Argus, and on the NVIDIA gstreamer plugins. Dropping it would make the
tegracam/Argus path of `IMX585-YOCTO-NOTES.md` §8.2 unbuildable rather than
merely uninstalled. Nothing in the minimal image's install list pulls it in, so
keeping it costs rootfs nothing.

## USB audio

`kernel-module-snd-usb-audio`, plus a `usb-audio.cfg` kernel fragment that pins
`CONFIG_SND_USB_AUDIO=m` rather than trusting NVIDIA's `defconfig` to carry it.
No userspace ALSA packages: `/proc/asound/cards` confirms enumeration, and the
container gets the device with `--device /dev/snd`. The module autoloads from the
USB modalias once udev is up, so it is not in `KERNEL_MODULE_AUTOLOAD`.

None of the Tegra onboard-audio stack is installed — `nvidia-kernel-oot-alsa`
and the fifteen `kernel-module-snd-soc-tegra*` packages are in the
`MACHINE_EXTRA_RRECOMMENDS` that this image skips. USB audio does not need them.

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
