# Agent Notes for Tegra Demo Distro

## Repository Layout
- Yocto build directory lives in `build/`; use `source setup-env build` before bitbake commands.
- Custom layer is `layers/meta-tegrademo`; overlay and driver recipes live under `recipes-bsp` and `recipes-kernel` there.
- Dynamic-debug kernel fragment is in `layers/meta-tegrademo/recipes-kernel/linux/linux-yocto/imx585.cfg`.
- Workspaces created via `devtool modify` live under `build/workspace/sources/` (currently `imx585-v4l2-driver` and `imx585-overlay` are active).

## Boot Media & Overlay Handling
- `imx585-overlay` installs two copies of `imx585-overlay.dtbo`: one in `/boot` (rootfs) for on-device use and one at the image root so `tegraflash` can bundle it.
- `l4t-launcher-extlinux` expects overlays and DTBs at the top of `${DEPLOY_DIR_IMAGE}`; we added a dependency to ensure overlays are present before EXT Linux assets copy.
- EXT Linux autoloads the overlay via `UBOOT_EXTLINUX_FDTOVERLAYS` in `layers/meta-tegrademo/conf/distro/tegrademo.conf`; primary DTB comes from `OVERLAY_DTB_FILE` pointing to the updated `tegra234-p3768-0000+p3767-0005-nv-super.dtb` variant.
- ESP and rootfs images must both be refreshed after overlay changes: `bitbake -c clean imx585-overlay l4t-launcher-extlinux tegra-espimage` then rebuild the image (e.g., `bitbake orin-console-image`).

## Flashing Guidance
- To run the new console image on hardware, flash using `orin-console-image-jetson-orin-nano-devkit-nvme.rootfs.tegraflash.tar.zst`; earlier logs showing `demo-image-base.ext4` mean the wrong bundle was used.
- Confirm post-flash that `/lib/modules/$(uname -r)/kernel/drivers/media/i2c/imx585.ko` matches the staged RPM (compare `sha256sum`).

## IMX585 Driver Debugging
- Driver now enforces a proper mutex for `sd.state_lock`; ensure the device is running the rebuilt module (`strings imx585.ko | grep state_lock`).
- Current crash is a NULL dereference in `__v4l2_subdev_state_get_format()`, triggered because Tegra VI requests a `get_fmt` before the sensor’s `sd->active_state` is ready. Investigate the sensor probe path around `v4l2_subdev_init_finalize()` to guarantee a valid active state.
- Dynamic debug is enabled in the kernel config (`CONFIG_DYNAMIC_DEBUG{,_CORE}=y`); mount debugfs (`mount -t debugfs none /sys/kernel/debug`) and use `/sys/kernel/debug/dynamic_debug/control` to toggle `dev_dbg()` once we add them.

## On-Target Diagnostics
- Useful commands: `media-ctl -p`, `v4l2-ctl --list-devices`, `dmesg | grep -i imx585`, `lsmod | grep imx585`, `fdtget` (install via package if missing), `strace -eopenat,ioctl v4l2-ctl --list-devices` for ENAMETOOLONG errors.
- Expect benign “Fixed dependency cycle” messages from Tegra ACONNECT; focus on sensor logs and VI errors.

## Devtool Workflow (default for drivers & overlays)
- Always keep an active devtool workspace for in-flight components. For the IMX585 bring-up, ensure `devtool modify imx585-v4l2-driver` stays active until the driver is fully functional; if the workspace rolls to `workspace/attic`, immediately rerun `devtool modify` to rehydrate it before continuing.
- Use `devtool modify <recipe>` as the first step for any sensor driver or overlay work (currently `imx585-v4l2-driver` and `imx585-overlay`). This keeps patches out of the layer until we deliberately finish.
- Edit live sources under `build/workspace/sources/<recipe>` and rebuild quickly with `bitbake -c compile <recipe> -f` (or include additional targets as needed).
- Only after the feature is verified end-to-end should we run `devtool finish <recipe> layers/meta-tegrademo` to export the diff as proper patches, followed by `devtool reset <recipe>` after the changes are merged. Nathan owns the upstream git repos and will push final commits once the Yocto patches are generated.
- Follow the same pattern for upcoming work (e.g., CEF168 driver + overlay) to avoid churn from repeatedly refreshing patches during iteration.

## Pending Debug Tasks
- Add targeted `dev_dbg()` instrumentation (requires rebuilding module) to trace format negotiation before Tegra VI probes.
- Ensure `CONFIG_VIDEO_V4L2_SUBDEV_API` remains enabled so subdev states are allocated.
- After fixing the active-state crash, run camera smoke tests (`media-ctl`, `v4l2-ctl`, actual capture) to verify overlays and module autoload at boot.
