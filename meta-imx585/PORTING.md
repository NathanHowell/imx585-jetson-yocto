# Porting status: R36.4 / 6.12 → R39.2.1 / 6.8.12

The recipes in this layer were carried over from `meta-tegrademo` in the old
`tegra-demo-distro` tree (snapshot in `../oe4t-config/`). They are **unbuilt on
wrynose**. Everything below is a known or suspected porting item; nothing here
has been tested against JetPack 7.2.1.

## Blocking

**The driver itself.** `recipes-kernel/imx585/imx585-source.inc` still pins
`NathanHowell/imx585-v4l2-driver` branch `jetson-overlay-6.12.y` at SRCREV
`1066048`. That is the upstream-style V4L2 driver that crashed with a NULL
dereference in `__v4l2_subdev_state_get_format()`, because Tegra VI issues
`get_fmt` before the sensor's `sd->active_state` exists — an upstream-style
subdev paired with an NVIDIA-camera-framework device tree.

Two ways forward, and this is the real decision:

1. Continue the tegracam conversion already started. 25 unpushed commits sit in
   `../oe4t-config/archive/bundles/imx585-v4l2-driver-devtool.bundle`, ending at
   `13947db imx585: seed sensor_mode_properties for tegracam`. See
   `../oe4t-config/archive/patches/imx585-v4l2-driver/`.
2. Start from Kurokesu's `nv_imx585.c` (`../imx585-jetson-driver/`), which is
   tegracam-native but validated only on JetPack 6.2.1/6.2.2 — kernel 5.15,
   Ubuntu 22.04 headers. Its Makefile hardcodes
   `3rdparty/canonical/linux-jammy/kernel-source` and patches its DTS from
   `/etc/nv_tegra_release`; none of that survives the move to Yocto anyway.

Either path is a 5.15/6.12 → 6.8.12 tegracam API jump that nobody has walked.

## Likely to need changes

- **Device tree names.** `conf/distro/imx585.conf` still references
  `tegra234-p3768-0000+p3767-0005-oe4t.dtb`. On wrynose the machine default is
  `tegra234-p3768-0000+p3767-0005-nv-super.dtb` (`conf/machine/include/orin-nano.inc`).
  The R39 source DTS names have not been re-checked.
- **DT include paths.** `imx585-overlay.bb` and `imx585-devicetree_1.0.bb` glob
  `${RECIPE_SYSROOT}/usr/src/device-tree/nvidia/t23x/nv-public/...`.
  `nvidia-kernel-oot` on wrynose still stages `/usr/src/device-tree` from
  `${S}/hardware/nvidia/`, so the root is right, but the `t23x` subtree layout
  under R39.2.1 is unverified.
- **`DTC_PPFLAGS`.** `imx585-devicetree_1.0.bb` passes
  `-DLINUX_VERSION=600`, copied from the 6.x generic-dts Makefile. Check what
  R39.2.1 expects.
- **`imx585.cfg`.** `CONFIG_V4L2_CCI{,_I2C}` are needed only by the
  upstream-style driver's CCI regmap use. If you land on tegracam, drop them.
- **CEF168.** `recipes-kernel/cef168/` pins pinefeat/cef168 at
  `3abcaeef`. Unverified against 6.8; it is a small subdev + ctrl driver, so the
  exposure is V4L2 control and subdev API churn.

## Operational changes in JetPack 7.2

- Flashing: R39.2 supports **`initrd-flash` only**. The old tegraflash-tarball
  flow changes.
- Many Tegra ASoC drivers moved in-tree, so `nvidia-kernel-oot-alsa` shrank.
  Irrelevant to the camera, but a verbatim copy of the old packagegroup would
  fail on missing packages.

## Not carried over

`meta-tegrademo`'s swupdate layer, demo images (sato/weston/egl/x11),
`data-overlay-setup`, `tegra-fitimage`, `docker-conf` and the CI bits. All of it
is in `../oe4t-config/archive/meta-tegrademo/` if it turns out to be wanted.
