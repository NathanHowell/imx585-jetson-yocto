# OE4T Yocto build configuration — IMX585 bring-up snapshot

A read-only snapshot of the working pieces of the earlier OE4T
(`tegra-demo-distro`) build, kept here so the IMX585 work can be carried onto a
current Yocto image without needing the original build tree.

## Provenance

Copied on 2026-09-17 from `/home/nathan/tegra-demo-distro`:

| | |
|---|---|
| Repo | `github.com/OE4T/tegra-demo-distro` (fork remote `finchies` → `github.com/NathanHowell/finch-image`) |
| Branch / commit | `rework` @ `d824dd2452e510f09d14c5fddd3f4e9e4d4b0424` |
| `repos/poky` | `702c515a7568b10bb208635d1d747b529e810690` (5.3_M2-741) |
| `repos/meta-tegra` | `fb67c070792c5b1c3b5a706a42faff276738f4ab` |
| `repos/meta-openembedded` | `52e78803b0c0c2f6b523b6a6fd4505af25cc6ff2` |
| `repos/meta-tegra-community` | `dcbf9eb2c9db5faacad8562985ec97a4607b77c8` |
| `repos/meta-virtualization` | `183ff71f2b04985db079bc76b92ec941190ef111` |

`MACHINE = jetson-orin-nano-devkit-nvme`, `DISTRO = tegrademo` (5.2), kernel
`linux-yocto` 6.12 — this is the build that produced the
`Linux version 6.12.48-yocto-standard` boot log in
`../imx585-v4l2-driver/debug/baseline.txt`.

`build/` is in `.gitignore` upstream, so `build-conf/` here is the **only** copy
of that configuration. Everything under `meta-tegrademo/` is committed in the
fork, except `recipes-core/images/orin-console-image.bb`, which was copied from
the working tree with uncommitted changes.

## Contents

```
build-conf/                 the build/conf directory (not in upstream git)
  local.conf                MACHINE, DISTRO, linux-yocto 6.12 pin, KERNEL_DTC_FLAGS=-@
  bblayers.conf             layer list — absolute /home/nathan paths, rewrite when reusing
  templateconf.cfg          meta-tegrademo/conf/templates/tegrademo
  distrolayer.cfg, conf-notes.txt, conf-summary.txt

meta-tegrademo/             the IMX585/CEF168-relevant subset of the custom layer
  conf/layer.conf
  conf/distro/tegrademo.conf          UBOOT_EXTLINUX_FDT{,OVERLAYS}, OVERLAY_DTB_FILE
  conf/distro/include/tegrademo.inc   distro definition, linux-yocto 6.12 preference
  recipes-kernel/imx585/              driver recipe + source pin + 2 patches
  recipes-kernel/cef168/              Pinefeat CEF168 EF lens-controller driver recipe
  recipes-kernel/linux/               linux-yocto bbappend + imx585.cfg config fragment
  recipes-bsp/imx585-overlay/         overlay recipe (devicetree.bbclass) + DT patch
  recipes-bsp/tegrademo-devicetree/   full DTs, incl. two IMX585+CEF168 variants
  recipes-bsp/uefi/                   l4t-launcher bbappend forcing overlay deploy order
  recipes-core/images/                orin-console-image.bb
  recipes-core/packagegroups/         packagegroup-orin-console + fdt.conf

scripts/check-imx585.sh     on-target diagnostic (lsmod/dmesg/media-ctl/fdtget)
AGENTS.md                   bring-up notes from the distro repo — devtool workflow,
                            overlay/flashing quirks, and the crash this build hit
```

## How the pieces fit

- `imx585-v4l2-driver.bb` builds the upstream-style driver from
  `NathanHowell/imx585-v4l2-driver`, branch `jetson-overlay-6.12.y`, pinned at
  `SRCREV = 1066048…` — the same tree checked out at `../imx585-v4l2-driver`.
  Two patches are applied on top: a subdev state mutex, and debug
  instrumentation.
- `imx585-overlay.bb` builds `imx585-overlay.dts` out of that same git repo via
  `imx585-source.inc`, with an extra patch adding clock and link settings. It
  needs `nvidia-kernel-oot` staged for the NVIDIA DT includes.
- `tegrademo-devicetree_1.0.bb` builds full (non-overlay) DTs, including
  `…-imx585-cef168.dts` and `…-imx585-cef168-cam1.dts`, which put the sensor and
  the lens controller behind `cam_i2cmux` and register both with
  `tegra-camera-platform` as `v4l2_sensor` and `v4l2_lens`.
- `tegrademo.conf` applies the overlay at boot through
  `UBOOT_EXTLINUX_FDTOVERLAYS` and `OVERLAY_DTB_FILE`.
- The image autoloads both modules:
  `KERNEL_MODULE_AUTOLOAD:append = " imx585 cef168"`.

## Known state at the time of the snapshot

This configuration built and flashed, but the camera did not come up. Per
`AGENTS.md`, the driver crashed with a NULL dereference in
`__v4l2_subdev_state_get_format()` because Tegra VI issues `get_fmt` before the
sensor's `sd->active_state` exists. That is consistent with the root cause in
`../IMX585-YOCTO-NOTES.md` §4: an upstream-style V4L2 driver paired with an
NVIDIA-camera-framework device tree. Treat the recipes here as a working Yocto
packaging reference, not as a working driver.

The full-DT variants (`…-imx585-cef168*.dts`) are the more useful artifact going
forward, since they contain hand-written `tegra-camera-platform` and `cam_i2cmux`
nodes for both the sensor and the focus controller.
