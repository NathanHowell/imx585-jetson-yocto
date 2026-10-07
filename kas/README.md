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
sudo apt install kas      # Debian 13 ships 4.8.1; no pipx needed

kas/build.sh build kas/imx585.yml                                   # console image, podman
kas/build.sh shell kas/imx585.yml                                   # bitbake prompt
kas/build.sh build kas/imx585.yml:kas/include/minimal.yml            # minimal image, podman
kas/build.sh build kas/imx585.yml:kas/include/docker.yml             # docker instead of podman
kas/build.sh build kas/imx585.yml:kas/include/kernel-linux-yocto.yml # mainline 6.18
```

The `justfile` at the repo root wraps the common cases: `just build` (4 threads,
via `kas/lowmem.conf`), `just extract`, and the `just flash*` recipes around
`initrd-flash`. `just` lists them.

**Use `kas/build.sh`, not `kas` directly.** `KAS_WORK_DIR` (where the layers are
cloned) and `KAS_BUILD_DIR` cannot be expressed in `local.conf`, and both default
to the current directory — so a bare `kas build` in this tree clones several GB of
openembedded-core and meta-tegra into the working copy, on a `/home` with ~22G
free. The wrapper pins them and refuses to run if `/build` is not mounted.

### Storage

Everything heavy is on `/build` (`vg0/tiler-scratch`, xfs, 315G), and all of it
is declared rather than defaulted — the old tree reached 266G because `DL_DIR`,
`SSTATE_DIR` and `TMPDIR` all defaulted into it.

| | | set in |
|---|---|---|
| `DL_DIR` | `/build/yocto/downloads` | `imx585.yml` |
| `SSTATE_DIR` | `/build/yocto/sstate-cache` | `imx585.yml` |
| `TMPDIR` | `/build/yocto/tmp` | `imx585.yml` |
| `KAS_WORK_DIR` | `/build/yocto/layers` | `build.sh` |
| `KAS_BUILD_DIR` | `/build/yocto/build` | `build.sh` |

315G does not comfortably hold a full JetPack build with CUDA *plus* downloads
and sstate, so `imx585.yml` sets `INHERIT += "rm_work"` with an `RM_WORK_EXCLUDE`
covering `imx585-v4l2-driver`, `cef168-v4l2-driver`, `nvidia-kernel-oot` and
`linux-noble-nvidia-tegra`. Those four are the ones being debugged; reaping their
work directories would make `devtool modify` and post-mortem inspection
impossible, which is the entire current activity.

### Cheap failures first

A cold build is a multi-hour, ~200G affair. Climb this ladder instead of going
straight to `build`:

```sh
kas/build.sh dump kas/imx585.yml            # clones layers, resolves config
kas/build.sh shell kas/imx585.yml -c "bitbake -p"          # parse every recipe
kas/build.sh shell kas/imx585.yml -c "bitbake --runall=fetch imx585-console-image"
```

The fetch pass matters: it is not established whether every JetPack 7.2.1 BSP
component is directly fetchable, or whether some are SDK-Manager-gated and need
manual placement in `DL_DIR`. Twenty minutes to find out beats four hours.

## Flashing

R39.2 is `initrd-flash` only: the host RCM-boots a purpose-built Linux image over
USB, and *that* image reads partition content from the host and writes it to the
target's storage. All of the below is from `docs/Flashing.md` and
`docs/Flashing-without-sudo.md` in meta-tegra `322bc23`.

The artifact is `IMAGE_FSTYPES += "tegraflash-tar.zst"` (`tegra-common.inc`), so:

```sh
mkdir -p /build/flashing && cd /build/flashing
tar xf /build/yocto/tmp/deploy/images/jetson-orin-nano-devkit-nvme/\
imx585-console-image-jetson-orin-nano-devkit-nvme.tegraflash-tar.zst
lsusb -d 0955:7523      # Orin Nano in recovery mode
./initrd-flash
```

### Host requirements

| | |
|---|---|
| `dtc`, `cpp`, `bash`, Python 3, `tar`, `zstd` | present |
| `udisksctl` (`udisks2`) | present |
| `sgdisk` (`gdisk`) | present (in `/sbin`, so not on a plain user `PATH` — check with `command -v sgdisk \|\| ls /sbin/sgdisk`) |
| `bmaptool` (`bmap-tools`) | present. Not strictly required, but without it flashing a moderately large rootfs "will take an extremely long time" |
| x86-64, bare metal | yes. NVIDIA's low-level tools are binary-only x86-64, and virtualization interferes with the USB link |
| direct USB port | **not a hub.** "Random failures may occur if you connect via an external hub" |
| `tlp` absent | yes, not installed — it interferes with flashing |
| automount disabled | not applicable, this host is headless with no GNOME |

### sudo is not required

`initrd-flash` invokes `sudo` itself for the few steps that need it (`bmaptool`,
and the NVIDIA unified script on Thor). Running the whole thing under `sudo` is a
troubleshooting fallback, not the normal path. This account is already in `disk`
and `plugdev`; the remaining piece is a udev rule so the recovery-mode device is
accessible without root:

```
# /etc/udev/rules.d/70-jetson-orin-nano.rules
SUBSYSTEMS=="usb", ATTRS{idVendor}=="0955", ATTRS{idProduct}=="7523", GROUP="plugdev", TAG+="uaccess"
```

### What to flash, and when

`initrd-flash` with no arguments writes **both** the QSPI boot firmware and the
external rootfs. That is what the first flash has to do, for two reasons: the
devkit's shipped QSPI predates R39, and `OPTEE_ENABLE_FTPM = "1"` changes the
OP-TEE binary in the TOS partition, which is firmware rather than rootfs.

Afterwards:

| | |
|---|---|
| `--external-only` | skip the firmware. The normal flag once the QSPI is current. |
| `--qspi-only` | firmware only. |
| `./doexternal.sh /dev/sdX` | write the NVMe **directly from the host**, with the drive attached here rather than to the Jetson. Worth knowing for the driver debug loop — no RCM dance per iteration. |

`USE_REDUNDANT_FLASH_LAYOUT_DEFAULT = "1"` means A/B, so both rootfs slots get
written.

### Serial console

Not required, but the troubleshooting advice assumes it. On the Orin Nano devkit
the debug UART is on the **button header as 3.3 V TTL** — it is not exported over
USB the way the AGX Orin and AGX Thor kits do it, so it needs a USB-serial
adapter. `initrd-flash` itself prints only high-level status and writes
`log.initrd-flash.<timestamp>`; the interesting failures are visible on the
target.

## Files

| | |
|---|---|
| `build.sh` | wrapper pinning `KAS_WORK_DIR`/`KAS_BUILD_DIR` to `/build`. Use this. |
| `imx585.yml` | the build: distro, machine, target, `local.conf` |
| `include/base.yml` | layer pins shared by all configs |
| `include/kernel-linux-yocto.yml` | opt in to mainline linux-yocto 6.18 |
| `include/podman.yml` | meta-virtualization + podman; included by `imx585.yml` |
| `include/minimal.yml` | build `imx585-minimal-image` instead of the console image |
| `include/docker.yml` | docker instead of podman |

## Images

| | |
|---|---|
| `imx585-console-image` | bring-up: openssh, rpm on target, `kernel-modules`, strace/trace-cmd/bpftrace, plus the same podman + CUDA stack as the minimal image |
| `imx585-minimal-image` | headless, read-only rootfs: `packagegroup-core-boot` + the camera + one container engine with CUDA and USB audio, container store on a separate data partition |

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

Both images install it, through `recipes-core/images/imx585-containers.inc`.
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
| `libtegrav4l2.so`, `libv4l/plugins/libv4l2_nvvideocodec.so`, for `nvv4l2decoder` | `tegra-libraries-multimedia-v4l` |
| `libnvmm*`, `libnvbufsurface`, `libnvbufsurftransform` | `tegra-libraries-multimedia`, `tegra-libraries-multimedia-utils` |

Both `tegra-libraries-camera` and `tegra-libraries-multimedia-v4l` are
`REQUIRED_DISTRO_FEATURES = "opengl"`, which is the concrete reason `opengl`
stayed in `DISTRO_FEATURES` on a headless target — see **Headless** below.

Decode only, on this board. `devices.csv` lists `/dev/v4l2-nvenc` because it is
written for every Tegra, but the Orin Nano module — p3767-0003/0005, which is what
`TEGRA_BOARDSKU = "0005"` in `orin-nano.inc` selects — has **no NVENC engine**.
NVIDIA's module datasheet gives its video encode as "1080p30 supported by 1-2 CPU
cores", i.e. software. NVDEC and NVJPEG are present. So `nvv4l2h264enc` and
friends are unavailable no matter what is installed, and that CSV line will never
resolve. Orin NX has NVENC; Orin Nano does not.

I did not add any of these recipes: CUDA was the stated requirement and this is a
different one. Separately, no `nvidia-kernel-oot` package in wrynose carries an
nvdec module, so on 6.8 `/dev/v4l2-nvdec` appears to come from in-tree code now,
the way the Tegra ASoC drivers did. Unverified.

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

## Root filesystem

ext4, and on Tegra that is a constraint rather than a preference.

`extlinux.conf-support.md` in meta-tegra is explicit:

> The ext4 implementation in L4TLauncher may have bugs or limitations that prevent
> it from reading `extlinux.conf` or other rootfs files when newer ext4 features
> are in use. **Non-ext4 root filesystems are unlikely to work.**

L4TLauncher is NVIDIA's UEFI application, and it reads `/boot/extlinux/extlinux.conf`,
the `FDT` device tree and the `OVERLAYS` list **out of the rootfs partition**,
before Linux exists. Its only filesystem reader is ext4. This project depends on
that path directly: `UBOOT_EXTLINUX_FDTOVERLAYS = "imx585-overlay.dtbo"` in
`imx585.conf` is applied by L4TLauncher from the rootfs. A btrfs or f2fs root
would leave it unable to find any of it, and it would silently fall back to
partition-based boot — booting the `kernel-dtb` partition's device tree with no
IMX585 overlay.

The flashing path agrees. `image_types_tegra.bbclass` sets
`IMAGE_TEGRAFLASH_FS_TYPE ??= "ext4.simg"`, and NVIDIA's flash scripts stage
`resize2fs`, `e2fsck` and `dumpe2fs` to fit the rootfs image to the partition.

So the alternatives, briefly, for the record:

| | |
|---|---|
| **btrfs** | The real temptation — transparent zstd compression would pay for itself on the L4T libraries, and podman has a native btrfs storage driver using subvolumes instead of overlayfs. Dead on arrival: L4TLauncher cannot read it. Would need a separate ext4 `/boot` partition and a custom flash layout. |
| **f2fs** | Built for raw NAND and for eMMC/SD behind a dumb FTL. An NVMe SSD has its own controller, DRAM and SLC cache doing the same job, so there is little left to win, and the same bootloader problem applies. |
| **xfs** | No compression, no snapshots, nothing ext4 lacks at this scale. Same bootloader problem. |
| **erofs** | Read-only. Interesting for a locked-down variant with a writable overlay for `/var`, but it does not fit a system whose whole point is a mutable container store. |

### What is worth doing instead

`util-linux-fstrim`, with `fstrim.timer` enabled by a bbappend. oe-core packages
the timer but ships it `disable`d. Without periodic TRIM the drive's FTL never
learns which blocks the filesystem freed, so sustained write performance decays
and wear levelling has less spare area — and a container store that pulls and
deletes images churns a lot of blocks. Weekly batched trim, not `discard` at
mount time: continuous discard issues a DEALLOCATE on every delete and stalls the
submission queue.

Two notes on the ext4 itself:

- meta-tegra's warning about "newer ext4 features" is already cleared
  empirically. The previous build in `oe4t-config/` flashed and booted with
  L4TLauncher reading `extlinux.conf` and the overlay from an OE-generated ext4
  rootfs, and its poky (`702c515`, 5.3_M2-741) carried **e2fsprogs 1.47.3** —
  which already defaults to `orphan_file` and `metadata_csum_seed`. wrynose has
  1.47.4. No precautionary feature disabling is needed, and `TEGRA_EXT4_OPTIONS`
  is the lever (`-O ^metadata_csum_seed`) if that ever changes.
- `EXTRA_IMAGECMD:ext4` is `-i 4096 -b 4096` from `tegra-common.inc`: one inode
  per 4 KB rather than per 16 KB. Four times the inodes of an oe-core default, at
  a few percent of the partition — which a container store full of small files
  wants.

Worth more attention than the filesystem type: `ROOTFSPART_SIZE_DEFAULT` for this
machine is 28 GiB, and `imx585.conf` sets
`USE_REDUNDANT_FLASH_LAYOUT_DEFAULT = "1"`, so A/B halves that to roughly 14 GiB
per slot — and `/var/lib/containers` lives inside it. CUDA plus a couple of
`l4t-jetpack`-based images will make that tight. If it bites, the fix is a
separate data partition for the container store outside the A/B pair, not a
different filesystem.

## Read-only rootfs

`imx585-minimal-image` sets `IMAGE_FEATURES += "read-only-rootfs"`. The console
image stays writable for bring-up.

One thing does not work out of the box. oe-core's hook appends `ro` to `APPEND`,
but on Tegra the kernel command line comes from `UBOOT_EXTLINUX_KERNEL_ARGS` in
the separate `l4t-launcher-extlinux` recipe, so an image variable never reaches
`extlinux.conf`. `imx585.conf` sets it instead — distro-wide, since there is one
`extlinux.conf` per build. Harmless for the console image: its `/etc/fstab` still
says `defaults` and `systemd-remount-fs` puts `/` back read-write.

What the rest of the system needs, in `imx585-node-config`:

| | |
|---|---|
| journald | `Storage=volatile`, `RuntimeMaxUse=64M`. `Storage=auto` would fall back to volatile anyway with `/var/log/journal` absent, but by accident rather than decision — and the default cap is 10% of RAM, 800 MB here. |
| dropbear | `dropbearkey.service` writes to `/etc/dropbear`, which cannot exist. Host keys move to the data partition; keys in `/run` would regenerate every boot and make every reconnection look like a MITM. Only `dropbearkey` gets the condition override — giving `dropbear@.service` a condition testing for the key's *absence* would stop the server once the key existed. |
| CDI spec | `nvidia-ctk cdi generate --output=/etc/cdi/...` cannot write to a read-only `/etc`. `nvidia-cdi-generate.service` regenerates it into `/run/cdi` each boot, after `nvidia-container-setup.service`, which is what puts the csv `alt-roots` setting in place. |

Node identity is the module's EEPROM serial from
`/proc/device-tree/serial-number`, not `/etc/machine-id`. With `/etc` read-only
systemd generates a transient machine-id into a tmpfs on every boot, so it is
useless for correlating telemetry; the serial needs no mechanism in the image,
survives reflashing, and is printed on the hardware.

If the build fails on a package whose postinst must run on the target, the lever
is `IMAGE_FEATURES += "read-only-rootfs-delayed-postinsts"` — but read the
postinst first, because a deferred one has nowhere to record that it ran.

## Data partition

The container store cannot live in `/var/lib`. On a read-only rootfs oe-core's
`volatile-binds` bind-mounts a tmpfs over `/var/lib` — its service carries
`ConditionPathIsReadWrite=!/var/lib`, so it activates exactly when the rootfs is
read-only — and an image store there would be in RAM, lost on every reboot and
fatal to an 8 GB module well before that.

So `graphroot` moves to `/data/containers/storage`, on its own ext4 partition.
`runroot` stays under `/run`, which is where it belongs.

The NVIDIA flash layout leaves the NVMe's tail unallocated: `ROOTFSPART_SIZE` is
28 GiB, halved to ~14 GiB per slot by A/B, and the rest of the device is free.
`imx585-data-partition.service` claims it on first boot:

```
nvme0n1
├─ p1  APP       14 GiB   rootfs slot A, ro
├─ p2  APP_b     14 GiB   rootfs slot B, ro
├─ …   small L4T partitions
└─ free  ──sgdisk ──▶  imx585-data   →  /data   (ext4, noatime,nodev,nosuid)
```

Properties worth knowing:

- **Idempotent on two conditions**, the GPT partition label *and* the presence of
  a filesystem. The second matters: the kernel will not re-read a partition table
  while partitions on the disk are mounted, so `partx -a` adds the new partition
  through BLKPG. If even that fails the script succeeds with a message, and the
  next boot finds the partition, sees no filesystem, and formats it.
- **Escape hatch.** `imx585.no_data_partition` on the kernel command line skips
  it entirely without touching the GPT.
- **Refuses rather than guesses.** No readable GPT, or a largest free block under
  `DATA_MIN_BYTES` (8 GiB), and it fails having changed nothing. A partition too
  small to be useful is worse than none, because podman would start and fill it.
- **The disk is decided at build time**, not discovered: `DATA_DISK` drops the
  partition suffix from `TNSPEC_BOOTDEV` (`nvme0n1p1` → `nvme0n1`).
- `data.mount` is conditional on the device existing, so an unprovisioned board
  still boots and podman fails on its missing graphroot, which is the error worth
  seeing.
- `mkfs.ext4 -i 8192 -m 1`: twice the default inode count, because an overlay
  store is mostly small files, and 1% reserved instead of 5%.

With the store out of the rootfs, A/B redundancy stops being a space problem —
~14 GiB per slot against a rootfs of a couple of GB — and starts being the point:
atomic rootfs updates with the data left alone.

## Firmware TPM

`OPTEE_ENABLE_FTPM = "1"` in `imx585.conf`. No hardware: NVIDIA ships the
Microsoft `ms-tpm-20-ref` implementation as an OP-TEE trusted application
(`optee-ftpm`, UUID `bc50d971-d4c9-42c4-82cb-343fb7f37896`), built
`CFG_TA_MEASURED_BOOT=y CFG_USE_PLATFORM_EPS=y`, so the endorsement seed is
derived from this module's own fuses. The result is `/dev/tpm0` and `/dev/tpmrm0`.

meta-tegra defaults it on for tegra264 (Thor) and off for tegra234, but Orin
supports it — `optee-ftpm` is `COMPATIBLE_MACHINE = "(tegra)"`.

**This changes the OP-TEE binary.** The fTPM and its helper go in as *early* TAs
through `EARLY_TA_PATHS` in `optee-os`, so the TOS partition has to be rewritten:
tegraflash, or a capsule/BUP update. Not a rootfs-only change.

### The trap

`optee-client` defaults `CFG_TEE_FS_PARENT_PATH` to `${localstatedir}/lib/tee`,
and on a read-only rootfs `/var/lib` is a tmpfs courtesy of `volatile-binds`. The
fTPM keeps its NV indexes, persistent handles and sealed blobs in OP-TEE secure
storage under that path — so the default would give you a TPM that **silently
forgets every key on reboot**, looking for all the world like a working but
freshly-provisioned TPM.

`OPTEE_FS_PARENT_PATH = "${IMX585_DATA_MOUNT}/tee"` moves it to the data
partition. `imx585-ftpm-config` creates the directory through `tmpfiles.d` and
adds a `RequiresMountsFor` drop-in so `tee-supplicant` cannot start before the
partition is mounted — same failure otherwise. The recipe refuses to build if the
two paths drift apart.

### What is installed

| | |
|---|---|
| `optee-client` | `tee-supplicant`, plus `tee-ftpm-modprobe.service` (built only when `OPTEE_ENABLE_FTPM` is set) |
| `kernel-module-tpm-ftpm-tee` | the `/dev/tpm0` driver. `CONFIG_TCG_FTPM_TEE=m` in `tpm.cfg` — it must be a module, because `optee-nvsamples` RDEPENDS this package name and the rootfs will not assemble without it |
| `optee-ftpm`, `optee-nvsamples-ftpm-helper` | the TA and its helper |
| `tpm2-tools` | from `meta-security/meta-tpm`, the only reason that layer is in `base.yml` |

The umbrella `optee-nvsamples` package is deliberately not installed: it would
also drag in the `luks-srv`, `hwkey-agent` and `pkcs11-sample` host apps, none of
which anything here calls.

To check it came up: `tpm2_getcap properties-fixed` with
`TPM2TOOLS_TCTI=device:/dev/tpmrm0`.

### What it is and is not

Keys are sealed to a seed derived from the SoC fuses, so they are device-unique
and non-portable, and that is a genuine improvement on
`/proc/device-tree/serial-number` for identifying this node to the collector —
a serial number is readable by anything and forgeable by anything.

But the keys live in OP-TEE secure-world memory, which is software isolation, not
a tamper-resistant die. Its trustworthiness rests on fused secure boot: without
that, anyone who can replace the bootloader replaces OP-TEE, and the fTPM attests
to whatever they like. There is no CC or FIPS certification. A discrete TPM (the
Infineon SLB9670 boards plug onto pins 17–26 of the 40-pin header as-is, see the
notes) is the answer if either of those matters.

### What the TPM is for here, and what it is not

**Not the sshd host key.** That stays an ordinary `dropbearkey` file on
`/data/ssh`, as described under **Read-only rootfs**. A TPM-*held* host key would
mean replacing dropbear with OpenSSH plus `tpm2-pkcs11`, an `ssh-agent` unit and a
sqlite token store, because dropbear has no PKCS#11 or `HostKeyAgent` support at
all. The host key only authenticates this node to an administrator over ssh, which
is not where the fleet's trust actually sits.

**The collector identity is.** A TPM-resident key that the telemetry agent uses as
a TLS client credential is worth far more: it is what lets the collector
distinguish this node from something replaying its data, and unlike
`/proc/device-tree/serial-number` it cannot be copied to another box. `tpm2-tools`
is in the image for provisioning and inspection; the key itself is created with
`tpm2_createprimary` / `tpm2_create` and made persistent with `tpm2_evictcontrol`,
into NV storage that now survives reboots because of `OPTEE_FS_PARENT_PATH` above.

### Using it as a client identity

The fTPM does not give you an authentication protocol. It gives you exactly one
thing: **an asymmetric key that cannot be copied off this board.** Everything
else is ordinary PKI. So the whole design is one key, one certificate, and then
per-consumer plumbing to reach them.

The key is ECC P-256, not RSA — NVIDIA changed RSA EK generation incompatibly
between L4T releases and recommends ECC for exactly that reason, and an fTPM
RSA-2048 keygen is slow enough to notice.

The artifact to produce is a **TSS2 PEM** file (`-----BEGIN TSS2 PRIVATE KEY-----`).
That is not a private key: it is the TPM's wrapped key blob plus a parent handle,
useless on any other machine. It can sit on `/data` in the clear, and be bind-mounted
read-only into a container, which is what makes the read-only-container rule hold
without exception.

```
# shape only -- nothing here has been run on hardware yet
openssl genpkey -provider tpm2 -provider default -algorithm EC \
        -pkeyopt group:P-256 -out /data/pki/node.tss2.key
openssl req     -provider tpm2 -provider default -new \
        -key /data/pki/node.tss2.key -subj "/CN=$(tegra-serial)" -out node.csr
```

`tpm2-openssl` registers a decoder for TSS2 PEM, so once the provider is loaded
every OpenSSL-based client — `curl`, `stunnel`, anything linking libssl — takes
that file as `--key` with no further ceremony. `tpm2_encodeobject` produces the
same format from a `tpm2_create`d key if the handle is built with tpm2-tools instead.

For a single node, a CA is optional: self-sign, and pin the resulting certificate
on the collector as its client CA. The point of the TPM is not a chain of trust,
it is that the credential cannot be lifted off the box.

#### The OTel collector can use it directly

`configtls` grew native TPM support in collector v1.32.0/v0.126.0. It opens
`/dev/tpmrm0` itself and loads a TSS2-format `key_file` — no PKCS#11, no OpenSSL
provider, nothing extra in the container image:

```yaml
exporters:
  otlp:
    endpoint: collector.lan:4317
    tls:
      ca_file:   /etc/pki/collector-ca.crt
      cert_file: /etc/pki/node.crt
      key_file:  /etc/pki/node.tss2.key
      tpm:
        enabled: true
        path: /dev/tpmrm0
```

```
podman run --read-only --device /dev/tpmrm0 -v /data/pki:/etc/pki:ro ...
```

`reload_interval` covers certificate rotation; the key never rotates, because it
cannot leave the TPM.

#### Podman cannot, and needs a credential helper

Registry mTLS in podman means dropping `client.cert`/`client.key` into
`/etc/containers/certs.d/<registry>/`, and those are read by Go's `crypto/tls`
through `containers/image`. Go has no PKCS#11 and no provider mechanism, and
nothing there understands TSS2. **A TPM-held key cannot be used for registry
mTLS.** No configuration fixes this.

The supported hook is `credHelpers` in `containers-auth.json(5)`: a map of
registry to helper suffix, invoking `docker-credential-<suffix>` over the
docker-credential-helpers stdio protocol. So the TPM authenticates *once*, to a
token endpoint, and podman gets a short-lived bearer token:

```json
{ "credHelpers": { "registry.lan": "tpm" } }
```

`/usr/bin/docker-credential-tpm` reads a server name on stdin and writes
`{"ServerURL":…,"Username":…,"Secret":…}`; in between it can be a dozen lines of
shell around `curl --cert /data/pki/node.crt --key /data/pki/node.tss2.key`. The
alternative — terminating mTLS in a local `stunnel` and pointing podman at
`127.0.0.1` — works too, and is worse: it hides the registry name from podman's
own trust policy.

Worth saying plainly: for a single node on a private network, a long-lived
registry token in a `0600` file on `/data` is a defensible choice, and the helper
is only worth writing if the registry already has a token endpoint.

#### Things that will bite

- **Pass `/dev/tpmrm0`, never `/dev/tpm0`.** `tpmrm0` is the in-kernel resource
  manager and multiplexes transient object slots; `tpm0` is one consumer at a
  time with manual context juggling. `tpm2-abrmd` is not needed and is not
  installed — the kernel resource manager supersedes it.
- **Pass `-provider tpm2` on the command line.** The usual way to load a provider
  is an `openssl.cnf` edit, and `/etc` is read-only here. Either keep it explicit,
  as above, or ship the `providers` stanza from a recipe — do not expect to drop
  it in on the node.
- **It is still one TPM.** Two containers both creating persistent handles will
  collide. Keep one consumer, or partition the handle range on purpose.
- **There is no EK certificate unless we make one.** NVIDIA's flow has the vendor
  pre-generate RSA and EC EK certs and encode them into the EKB at manufacturing
  time; we are the vendor, and have not. So attestation-style enrollment ("prove
  you are a genuine TPM") is unavailable, and enrolment is trust-on-first-use:
  register the public key out of band, once. For one node that is not a
  compromise, it is the correct amount of machinery.
- **Wiping `/data/tee` destroys the key.** The EPS is derived — NVIDIA documents
  `EPS = KDF(key=fTPM_Root_Seed, info=Device_SN, salt=EPS_Seed)`, rooted in the
  bootloader seed and the fuse serial number — so the *EK* comes back after a
  wipe. A child key created with `tpm2_create` does not: its sensitive area is
  random and lives in that secure storage. Either keep the re-enrollment path
  working, or make the identity key a primary key with fixed template and
  `unique` data, which is re-derivable from the board alone.
- **The TPM stops the key being copied, not used.** Root on this node can sign
  with it at will; what it cannot do is take it elsewhere. That is the threat
  this is worth spending on, and it is why fused secure boot is a separate
  question — see **Rootfs integrity** below.
- **PCR0 is live.** MB2 packages its boot event log into a buffer the fTPM TA
  parses and extends into PCR0 (the TA is built `CFG_TA_MEASURED_BOOT=y`), so
  sealing a secret to boot state is possible. It also makes every firmware
  update a re-sealing event. Not worth it here.

## Rootfs integrity: what it would take

Not built. Recorded because the research is easy to lose and the conclusions are
not obvious.

### meta-tegra's disk-encryption doc is not the thing

`docs/Disk-Encryption-for-Jetson-Devices.md` is 52 lines of community notes
transcribed from a Matrix thread, and it is about **LUKS encryption, not verity
integrity**. Those solve opposite problems: encryption stops a thief reading the
disk, verity stops anyone modifying it. A read-only rootfs built from a public
Yocto image holds no secrets, so encrypting it buys close to nothing — what it
needs is the guarantee that what boots is what was built.

Two things in that doc are also out of date:

- It says to run `gen_ekb.py` by hand. meta-tegra automates this:
  `tegra-eks-image` runs `gen_ekb.py` from `optee-nvsamples-native` with
  `TEGRA_GEN_EKB_ARGS`, and flashes the result as the `eks` partition.
  `TEGRA_EKB_SYM2` is documented in that recipe as the disk-encryption key that
  `hwkey-agent` and `luks-srv` consume.
- Its manual privileged post-build script exists because LUKS-formatting an image
  needs device-mapper. That reasoning does not apply to our data partition, which
  is created on the running target by `imx585-data-partition.service` — where
  device-mapper is available and `nvluks-srv-app` is already installed. If we want
  the container store encrypted, that service is the place, not a sudo script on
  the build host.

### The verity pieces that do exist

In **meta-security** (the root layer, not the `meta-tpm` sublayer we currently
enable):

| | |
|---|---|
| `classes/dm-verity-img.bbclass` | adds an `ext4.verity` image type: the ext4 with a hash tree appended, plus a `.verity.env` carrying `ROOT_HASH`, `DATA_SIZE` and `DATA_BLOCK_SIZE` |
| `recipes-core/images/dm-verity-image-initramfs.bb` | an initramfs-framework initramfs that reads that env and `veritysetup create`s the root |
| `recipes-core/initrdscripts/initramfs-framework-dm/dmverity` | the hook that does it |

### Tegra integration is easier than it looks

`tegra-minimal-initramfs` uses `tegra-minimal-init`, not initramfs-framework, so
meta-security's `initramfs-module-dmverity` will not drop in. But
`tegra-minimal-init`'s `init-boot.sh` sources **`/etc/platform-preboot`** before it
resolves the root device, and that file can set `rootdev`, `opt` and `fstype`. So
the integration is a small recipe that installs a `platform-preboot` running
`veritysetup create rootfs …` and setting `rootdev=/dev/mapper/rootfs`, added
through `TEGRA_INITRD_INSTALL` along with `cryptsetup` and the dm-verity module.
No replacing the init, no adopting initramfs-framework.

### Two real risks

1. **The flash path may resize the rootfs.** A verity image cannot be resized —
   that moves or invalidates the appended hash tree — and
   `image_types_tegra.bbclass` stages `resize2fs`, `e2fsck` and `dumpe2fs` for
   NVIDIA's flash scripts. Whether the resize runs for a plain `ext4.simg` write
   needs checking before anything else, because if it does, this does not work
   without disabling it. `ROOTFSPART_SIZE` must also cover image plus hash tree.
2. **`IMAGE_TEGRAFLASH_FS_TYPE` would become a chained conversion**
   (`ext4.verity.simg`). `verity` and `simg` are both in `CONVERSIONTYPES`, and
   sparse encoding is content-agnostic, so it ought to work. Unverified.

A/B is fine: the same image goes to both slots, so the same root hash, and each
slot carries its own matching `/boot/initrd` — which is what has to be updated in
lockstep with the rootfs. L4TLauncher can still read `/boot` from a verity image,
because the ext4 is intact at the front and the hash tree is appended after it.

### The trust anchor is the whole question

The root hash sits in **plaintext** in the initrd. Verity is only as good as
whatever vouches for that file. On Tegra the answer already exists:
`l4t-launcher-extlinux`'s `do_sign_files` signs `extlinux.conf`, the DTB, the
overlays **and `initrd`**, through `tegra-uefi-signing`.

So rootfs integrity here means *fused secure boot*, not dm-verity by itself.
Without fused keys, verity detects accidental corruption and nothing else: an
attacker who can rewrite the rootfs can rewrite the initrd beside it and put their
own root hash in. The same caveat applies to the fTPM, for the same reason.

Whether NVIDIA's measured boot extends a PCR the fTPM could seal against — the
fTPM TA is built `CFG_TA_MEASURED_BOOT=y`, so something is being measured — has
not been checked, and which PCRs carry what is NVIDIA-specific.

## Read-only containers

`containers.conf.d/20-imx585.conf` sets `read_only = true`, so a container that
wants scratch space has to ask with `--tmpfs` or a volume. That is the intent: a
container writing into its own image layer is storing state nobody will collect.
`log_size_max` is capped at 10 MB, because container logs land in a journal that
lives in RAM.

## Storage driver

overlay, using the kernel's overlayfs, rootful.

meta-virtualization ships `storage.conf` with `driver = "vfs"`, which copies every
layer of every image instead of stacking them — a safe lowest-common-denominator
default and a poor one on ext4. `meta-imx585`'s
`container-host-config_%.bbappend` seds it to `overlay`, and
`recipes-kernel/linux/files/containers.cfg` sets `CONFIG_OVERLAY_FS=y`. A sed
rather than a replacement `storage.conf`, so upstream changes to the rest of the
file still land. The bbappend lives under
`meta-imx585/dynamic-layers/virtualization-layer/` and is reached through
`BBFILES_DYNAMIC`, because a plain bbappend would be a dangling append — an error,
not a warning — on a build without an engine include.

Built in rather than `=m` on purpose: this image installs named
`kernel-module-*` packages instead of `kernel-modules`, so a module would be one
more thing to track by hand and one more way for the storage driver to fail at the
first `podman pull`.

Not `fuse-overlayfs`. That exists for rootless podman on kernels without
unprivileged overlayfs, and it is slower on metadata-heavy work. The podman
recipe's `rootless` PACKAGECONFIG — which is what pulls in `fuse-overlayfs` and
`slirp4netns` — is off, and containers here run as root. (On 6.8 even rootless
could use native overlay; unprivileged overlayfs landed in 5.11.)

Not `btrfs` or `zfs`: both would mean changing the rootfs filesystem for no gain
at this scale. `vfs` only as a fallback if overlay ever misbehaves.

Two things native overlay needs, both already true here: xattr support on the
upper filesystem (`xattr` is in `DISTRO_FEATURES_DEFAULTS`, the rootfs is ext4),
and `/var/lib/containers` on a real filesystem rather than tmpfs or a nested
overlay — it is on the NVMe rootfs.

`[storage.options.overlay] mountopt = "nodev,metacopy=on"` is worth trying if
image pulls feel slow; it avoids copying file data up on metadata-only changes.
Left alone for now because it is a tuning knob, not a correctness one.

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
