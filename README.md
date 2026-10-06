# IMX585 on Jetson Orin Nano

Bringing up a Sony IMX585 (Kurokesu module) with a Pinefeat CEF168 Canon EF
lens controller on a Jetson Orin Nano, under Yocto. The goal is **linear raw
sensor data** plus contrast-detect autofocus driven through the CEF168. (The
IMX585 has no PDAF; focus is closed-loop off image sharpness.)

## Layout

| | |
|---|---|
| `kas/` | the build. wrynose (Yocto 6.0 LTS) + meta-tegra `wrynose` = JetPack 7.2.1 / L4T R39.2.1, NVIDIA kernel 6.8.12. See `kas/README.md`. |
| `meta-imx585/` | the Yocto layer: IMX585 + CEF168 driver recipes, device tree overlay, full DTs, and two images (`imx585-console-image` for bring-up, `imx585-minimal-image` for a headless read-only camera node running CUDA containers). **Unbuilt on wrynose** — see `meta-imx585/PORTING.md`. |
| `oe4t-config/` | read-only snapshot of the previous OE4T build (R36.4 / linux-yocto 6.12) and everything that existed only on local disk. |
| `oe4t-config/archive/` | git bundles and patch series for unpushed work, incl. 25 commits of tegracam conversion. |
| `IMX585-YOCTO-NOTES.md` | sensor datasheet notes, driver comparison, frame-rate math, HDR vs linear, AF plan. |
| `imx585-v4l2-driver/` | submodule — upstream-style V4L2 driver (the one that crashed). |
| `imx585-jetson-driver/` | submodule — Kurokesu's tegracam driver, JetPack 6.2.x. |

## State

Nothing builds yet on the new platform. The previous build flashed and booted
but the camera never came up: a NULL dereference in
`__v4l2_subdev_state_get_format()`, from pairing an upstream-style V4L2 subdev
with an NVIDIA-camera-framework device tree. Choosing between the two driver
lineages is the open decision; `meta-imx585/PORTING.md` frames it.

`IMX585-YOCTO-NOTES.md` §8 frames that decision. Note §8.1 in particular: the
kernel version is *not* the axis it looks like, because NVIDIA's out-of-tree
camera stack builds against mainline `linux-yocto` too.
