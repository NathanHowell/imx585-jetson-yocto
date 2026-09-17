# IMX585 on Jetson / Yocto: Driver Research Notes

Working notes for bringing up the Sony IMX585 (Kurokesu module) on a current Yocto image for Jetson Orin.
Collected 2026-09-17. Sections marked **VERIFY** are inferences or have not been tested on hardware.

---

## 0. TL;DR

- **Two driver families exist and are not interchangeable.**
  - `imx585-v4l2-driver`: upstream-style V4L2 subdev, written for Raspberry Pi.
  - `imx585-jetson-driver`: Kurokesu, built on NVIDIA's `tegracam` / `camera_common` framework.
  - Jetson's non-media-controller capture path (VI) and Argus need the NVIDIA framework.
- **My old `jetson-overlay-6.12.y` branch failed because it combined the two.** It paired the upstream-style driver with an overlay written for the NVIDIA framework, so neither half understood the other. See §3.
- **Yocto on Jetson is official as of JetPack 7.2.** NVIDIA supports it through OE4T `meta-tegra`.
  - The `wrynose` branch (Yocto 6.0 LTS) and `master` both target **JetPack 7.2.1 / L4T R39.2.1**.
  - Default kernel is **`linux-noble-nvidia-tegra` 6.8.12**. **`linux-yocto` 6.18** is an option.
- **The IMX585 has no phase-detect (PDAF) pixels.** Autofocus has to be contrast-detect, or use an external distance sensor.
- **Linear raw options:**
  - RAW12 at up to **60 fps** full frame.
  - RAW10 at up to **90 fps**.
  - Clear HDR **RAW16** (linear merge of two conversion gains in one exposure) at about **30 fps**.
  - Clear HDR RAW12 is **gradation compressed**, so it is *not* linear.
- **Upstream:** no `imx585.c` as of Linux 7.3-rc3. `imx678.c` covers a sibling STARVIS 2 sensor, and an Aug 2026 RFC turns it into a shared STARVIS 2 driver with the IMX585 listed as future work.

---

## 1. Local inventory (`/home/nathan/imx585`)

| Path | What it is |
|---|---|
| `imx585-v4l2-driver/` | will127534's driver (Raspberry Pi, upstream-style V4L2 subdev). Remote branches `6.1.y`, `6.6.y`, `6.12.y`, `main`, `upstream_dev_stripdown`, `jetson-overlay-6.12.y`. |
| `imx585-v4l2-driver` @ `jetson-overlay-6.12.y` | **My non-working Jetson attempt.** Diff vs `main`: new Tegra overlay (`imx585-overlay.dts`), Makefile tweaks for Yocto (`KERNEL_SRC`, `modules_install`), subdev name forced to `imx585`, `debug/baseline.txt` (boot dmesg without the driver). |
| `imx585-jetson-driver/` | Kurokesu Jetson driver v0.2.0: `nv_imx585.c`, `imx585_mode_tbls.h`, overlays `tegra234-p3767-camera-p3768-imx585-{A,C}.dts`, DKMS/setup scripts. Targets JetPack 6.2.1/6.2.2 (L4T 36.4.4/36.5, kernel 5.15). |
| `imx585-datasheet.pdf` | Sony IMX585-AAQJ1-C datasheet, rev E21Y12D37 (2023-07-06), 57 pages. From Khadas. |

Old test environment, from `debug/baseline.txt`:
- `Linux 6.12.48-yocto-standard`
- OE4Tegra Demonstration Distro 5.2+snapshot
- `jetson-orin-nano-devkit-nvme`

That is the older meta-tegra layout: JetPack 6 BSP, with `linux-yocto` 6.12 as an alternative kernel.

---

## 2. Sensor facts (from the datasheet)

### 2.1 Geometry

| Item | Value |
|---|---|
| Optical format | Type 1/1.2, 12.84 mm diagonal |
| Pixel | 2.9 × 2.9 µm |
| Total pixels | 3856 × 2220 |
| Effective | 3856 × 2180 (includes 8-pixel color-processing margins) |
| Active | 3856 × 2176 |
| Recommended recording | 3840 × 2160 |
| Optical black | 20 lines at the front (vertical); 10 "effective OB" lines are output |
| Physical active area | 3856 × 2.9 µm = **11.18 mm**, 2180 × 2.9 µm = **6.32 mm** |
| CFA | RGGB (both drivers use RGGB) |

In the frame output (p.33 and p.36), each frame contains, in order:
1. FS
2. 1 embedded-data line
3. 10 ignored OB lines
4. **10 vertical effective OB lines** (CSI-2 data type `0x37`)
5. 4 ignored lines
6. 8 margin lines
7. 2160 recording lines
8. 8 margin lines
9. 1 embedded-data line
10. FE

### 2.2 Readout, rates, formats

| Mode | Max frame rate |
|---|---|
| All-pixel, 12-bit ADC | **60 fps** |
| All-pixel, 10-bit ADC | **90 fps** |
| 2×2 binning (1920×1080) | **90 fps** (not double the all-pixel rate) |
| Clear HDR | *not given in the datasheet* (see §5) |
| DOL-HDR (2 / 3 frame) | *not given; in a separate application note* |

- Outputs: **RAW10 / RAW12 / RAW16**. RAW16 is Clear HDR only; RAW14 was removed in rev E21Y12.
- CSI-2 D-PHY with 2, 4, 8, or 2×4 lanes. Orin Nano ports have at most 4 lanes.
- INCK: 24 / 27 / 37.125 / 72 / 74.25 MHz. The Kurokesu module has an **onboard 24 MHz oscillator**.
- I2C address pins: SLAMODE0/1. The module uses `0x1a`. Fast-mode Plus (1 MHz) is allowed when INCK ≥ 16 MHz.
- Features listed: window cropping, H/V flip, conversion gain switching (HCG/LCG), Clear HDR, Digital Overlap HDR (2/3 frame), sensor sync (XVS/XHS leader and follower).

### 2.3 Gain and sensitivity (p.1 and p.23)

- **Analog gain is 0–30 dB** in 0.3 dB steps (register 0–100).
- **30.3–72 dB is 30 dB analog plus digital gain** (register 101–240). Digital gain adds no signal-to-noise and costs raw range. **For raw work, cap at register 100.**

| Test condition: 12-bit, 30 fps, 0 dB, Tj 60 °C | HCG | LCG |
|---|---|---|
| G sensitivity (digits/lx·s) | 19556 | 3368 |
| Saturation (12-bit digits) | **1204** | **3895** |
| Conversion gain ratio HCG/LCG | **5.8** (min 5.6, max 6.0) | |

HCG improves low light but clips at about ⅓ of full scale. Clear HDR combines both reads (§5).

### 2.4 Power, reset, and start-up timing (p.41–45)

**Power-up:**
- Supply order: DVDD 1.1 V → OVDD 1.8 V → AVDD 3.3 V, all rising within 200 ms. Slew rate ≤ 25 mV/µs.
- XCLR held low ≥ 500 ns after the rails are up.
- INCK starts ≥ 1 µs after XCLR goes high.
- **First I2C ≥ 20 µs after XCLR goes high.**

**Start streaming (master mode):**
1. Write registers.
2. `STANDBY=0`.
3. **Wait 24 ms** (internal regulator).
4. `XMSTA=0`.
5. **Discard the first 8 frames** while the image stabilizes.

**Stop streaming:** `XMSTA=1`, then `STANDBY=1`.

**Register writes:**
- Registers marked "V" in Sony's register sheet take effect at the next frame boundary.
- A 2-line window around the vertical sync is off-limits for I2C writes.
- Sony recommends **REGHOLD** (`0x3001`) around multi-register updates.

- Performance is guaranteed only at junction temperature **−10 to 60 °C**.
- The register map, operating modes, Clear HDR, and DOL details are in separate Sony documents: *IMX585_Standard_Register_Setting* (Excel), the *Software Reference Manual*, and application notes. **None of these are in the PDF. Ask Kurokesu or Khadas for them.**

### 2.5 Embedded data line (p.34–35)

Present as data type `0x12` when enabled. Register `0x303A` controls it; both drivers currently disable it.

It contains only register mirrors:
- WINMODE, HREVERSE, VREVERSE
- ADBIT, ADDMODE, LANEMODE, MDBIT
- **SHR0** (`0x3050–0x3052`)
- **BLKLEVEL** (`0x30DC–0x30DD`)

**No phase or focus data.** It is still useful because it tells you exactly which exposure and black level applied to each frame.

---

## 3. Driver comparison

### 3.1 At a glance

| | will127534 `imx585.c` (RPi) | My branch `jetson-overlay-6.12.y` | Kurokesu `nv_imx585.c` | Upstream `imx678.c` (7.3-rc3) |
|---|---|---|---|---|
| Framework | V4L2 subdev, CCI regmap, runtime PM, subdev state and streams API | Same driver, plus a Tegra overlay | NVIDIA `tegracam` / `camera_common` | V4L2 subdev, CCI, runtime PM |
| Size | ~1770 lines | +2-line change | ~900 lines plus a 418-line mode table | ~1450 lines |
| Modes | 4K and 1080p binned, 12-bit; ClearHDR 12-bit compressed and 16-bit linear; mono | same | 3856×2180 and 1928×1090, RAW12 only | full or cropped window, RAW12, color and mono |
| Link rates | 594–2376 Mbps (DT `link-frequencies`) | same, but the overlay omits `link-frequencies` | fixed 720 Mbps | 594–2376 Mbps |
| Controls | exposure, analog gain, VBLANK, HBLANK, flips, black level, HDR mode, HDR blend/threshold/compression/gain, HCG, sync mode | same | TEGRA gain, exposure, frame rate, sensor mode, group hold | exposure, gain (≤ 30 dB), VBLANK, HBLANK, flips, test pattern |
| Clock | `devm_clk_get()` **required** | none in overlay → **probe fails** | none; oscillator assumed | `devm_v4l2_sensor_clk_get()` (needs `clocks` on DT) |
| Presence check | reads BLKLEVEL | same | reads STANDBY, expects `0x01` | reads module ID `0x4D1C == 0x02A6`, mono bit `0x4D18` |
| Build | kbuild against the running kernel | same plus Yocto vars | L4T headers plus `nvidia-oot` `Module.symvers` plus generated `conftest.h` | in-tree |

### 3.2 Why my branch could not work

1. **The overlay was written for NVIDIA's framework; the driver wasn't.**
   - `camera_common` is what reads `modeX` properties (`pix_clk_hz`, `line_length`, `num_lanes`, `gain_factor`, …). `imx585.c` never calls it, so those values were ignored.
   - VI and Argus need TEGRA controls plus the `set_mode` / `start_streaming` callbacks, and the RPi driver provides neither.
2. **Hard probe failures, in the order the driver checks them:**
   - `imx585.c:1555`: endpoint has no `link-frequencies`, so probe fails with "link-frequency property missing".
   - `imx585.c:1649`: no `clocks`, so `devm_clk_get()` fails with "xclk missing".
3. **Overlay errors, independent of the framework mismatch:**
   - **4 lanes on cam0 (`serial_b`).** cam0 on the Orin Nano devkit's 22-pin connector is **2-lane**. Only cam1 (`serial_c`) has 4.
   - `__overrides__` (`cam0`, `cam1`, `debug_safe`) is a **Raspberry Pi firmware** feature. UEFI/extlinux and Jetson-IO ignore it.
   - Placeholder mode values:
     - `csi_pixel_bit_depth=10` (the sensor outputs 12)
     - `pix_clk_hz=600000000` and `line_length=11200`
     - gain scale 16–506, not the real 0.3 dB steps
     - `max_framerate=60` at 720 Mbps
     - `readout_orientation=90`
     - `embedded_metadata_height=2` (the driver disables embedded data)
     - `physical_w/h` 11.13/6.26 (should be about 11.18/6.32)
   - `tegra-camera-platform` lane and speed limits were set by hand; NVIDIA's defaults are fine.
4. **What was right:** the routing through the camera I2C mux (`cam_i2cmux` → `i2c@0` / `i2c@1`), NVCSI and VI, the port indices, and the PWDN gpio-hog all match Kurokesu's working overlays.

### 3.3 Kurokesu Jetson driver details (`imx585-jetson-driver`)

- **Two overlays, picked with Jetson-IO:**
  - **A**: cam0, `serial_b`, 2 lanes, `lane_polarity=6`, `pix_clk_hz=118800000`, max 12.5 fps.
  - **C**: cam1, `serial_c`, 4 lanes, `lane_polarity=0`, `pix_clk_hz=237600000`, max 25 fps.
- **Mode properties** (both overlays):
  - `line_length=4224`, `use_decibel_gain=true`, `gain_factor=10`, gain 0–720 in steps of 3
  - `csi_pixel_bit_depth=12`, `embedded_metadata_height=0`
  - two modes: full resolution and 2×2 binned
- **Module oscillator:** the driver skips mclk handling (`nv_imx585.c:754`).
- **Power:** XCLR released, then a 500 ms delay (`IMX585_XCLR_MIN_DELAY_US`). The datasheet minimum is 20 µs; the long wait may be for the module's regulators.
- **Frame rate:** `set_frame_rate` computes VMAX = pix_clk × fr_factor / line_length / fps, clamped to 2250…0xFFFFF and forced even.
- **Exposure:** `set_exposure` computes SHR = VMAX − lines, forced even, minimum 8.
- **Lanes:** `set_mode` writes the common table, the mode table, LANEMODE, and HMAX = 1320 × 4 / lanes.
- **Test pattern:** a `test_mode` module parameter.
- **Build:** the Makefile hardcodes `-tegra-ubuntu22.04_aarch64` header paths, `/etc/nv_tegra_release`, and `/usr/src/nvidia/nvidia-oot`. It will not work unchanged in Yocto (§8).
- **Planned by Kurokesu:** higher link rates, ClearHDR, mono.

### 3.4 Fixes to make when porting the Kurokesu driver

| # | Issue | Where | Fix |
|---|---|---|---|
| F1 | Gain range reaches 72 dB, mostly digital | `nv_imx585.c:45` (`IMX585_ANALOG_GAIN_MAX 240`), DT `max_gain_val=720` | Cap at 100 (30 dB) and `max_gain_val="300"`. Apply extra gain in software. |
| F2 | Stream start order is reversed vs the datasheet | `nv_imx585.c:705-716` writes `XMSTA=0` before `STANDBY=0` | `STANDBY=0` → sleep 24–25 ms → `XMSTA=0`, as in `imx678.c:961-965` |
| F3 | Stop doesn't clear XMSTA | `imx585_stop_streaming` | `XMSTA=1` before `STANDBY=1` |
| F4 | Weak presence check | `imx585_board_setup` | Try reading `0x4D1C` / `0x4D18` 80 ms after `STANDBY=0`, as IMX678 does (**VERIFY on IMX585**) |
| F5 | Fixed 720 Mbps | `imx585_mode_tbls.h` / `set_mode` | Add a DATARATE_SEL (`0x3015`) mode; 1782 Mbps allows 60 fps (§4) |
| F6 | No RAW10, no Clear HDR | mode tables and DT | Add modes (§5) |
| F7 | Test pattern is a module parameter | `test_mode` | Optional: expose as a control |
| F8 | 500 ms XCLR delay | `IMX585_XCLR_MIN_DELAY_US` | Keep it until the module's power rails are characterized; the datasheet only needs 20 µs |
| F9 | Makefile is tied to the Ubuntu layout | `Makefile` | Replace with a Yocto recipe (§8) |

---

## 4. Timing and frame-rate math

The sensor's internal timing clock is 74.25 MHz. Upstream `imx678.c` treats the pixel rate as 74.25 MHz × 8 = 594 Mpix/s, with HMAX counted in 8-pixel units.

```
fps          = 74,250,000 / (HMAX × VMAX)
line time    = HMAX / 74.25 MHz
exposure     = (VMAX − SHR) lines           (SHR even, SHR ≥ 8 normal / ≥ 10 HDR)
VMAX default = 2250 (normal), 4500 (Clear HDR, per the will127534 driver)
```

The two drivers' constants agree. Kurokesu uses `pix_clk_hz 237600000 / line_length 4224 = 56,250 lines/s`. Upstream gives 74.25 MHz / HMAX 1320 = 56,250 lines/s, the same value.

### 4.1 Minimum HMAX vs link rate (4 lanes; ×2 for 2 lanes)

| DATARATE_SEL | Mbps/lane | will127534 min HMAX | **upstream imx678 min HMAX** | Normal fps (VMAX 2250) | Clear HDR fps (VMAX 4500) |
|---|---|---|---|---|---|
| 0x07 | 594 | 1584 | 1584 | 20.8 | 10.4 |
| 0x06 | 720 | 1320 | 1320 | 25.0 | 12.5 |
| 0x05 | 891 | 1100 | 1100 | 30.0 | 15.0 |
| 0x04 | 1188 | 792 | 792 | 41.7 | 20.8 |
| 0x03 | 1440 | 660 | 660 | 50.0 | 25.0 |
| 0x02 | **1782** | 550 | 550 | **60.0** | **30.0** |
| 0x01 | 2079 | 440 | **550** | ~~75~~ → 60 | ~~37.5~~ → 30 |
| 0x00 | 2376 | 396 | **550** | ~~83.3~~ → 60 | ~~41.7~~ → 30 |

- **Correction:** upstream holds HMAX at 550 for the three fastest link rates, and the datasheet gives 60 fps at 12-bit. The limit is sensor readout, not link capacity. The will127534 README values above 60 fps (and my earlier HDR estimates of 37.5 / 41.7 fps) were almost certainly unreachable.
- The will127534 README also says binning doubles the frame rate. The datasheet gives **90 fps max** for both all-pixel (10-bit) and binned.
- **10-bit readout is faster (90 fps).** The HMAX tables above are for 12-bit; RAW10 needs its own timing values from Sony's register sheet.
- **Link capacity check:** 4K RAW12 at 60 fps is about 6.25 Gbps of active pixels, about 1.56 Gbps per lane on 4 lanes, so 1782 Mbps fits.
  - Orin's CSI receiver handles up to 2.5 Gbps/lane.
  - Kurokesu ships at 720 Mbps to be safe on ribbon cables. **VERIFY signal integrity at 1782 Mbps.**
- On a 2-lane port (cam0, overlay A), halve every figure.

---

## 5. Linear raw, HDR, and bit depth

| Mode | Output | Linear? | Full-frame fps | Driver status |
|---|---|---|---|---|
| Normal RAW12 | one exposure, one conversion gain | **Yes (true raw)** | 60 | Kurokesu at 25 fps; will127534 at 60 |
| Normal RAW10 | one exposure, 10-bit | **Yes** | 90 | none |
| Clear HDR RAW16 (compression off, `CCMP_EN=0`, `MDBIT=3`) | HCG and LCG reads of the **same exposure**, merged on the sensor | **Linear merge** | ~30 (VMAX doubled) | will127534 only |
| Clear HDR RAW12 (gradation compression on) | same merge, compressed | **No** | ~30 | will127534 only |
| DOL-HDR 2/3 frame | separate exposures, each raw | Yes per exposure; merge yourself | lower (application note) | none |

**Clear HDR notes:**
- **Extra range:** about log2(5.8) ≈ **2.5 bits** beyond a single 12-bit LCG read, plus whatever the HDR gain adder (`EXP_GAIN`, `0x3081`: +0/6/12/18/24/29.1 dB) contributes.
- **Keeping it linear:** fix the HDR gain, the data-select threshold and blend (`EXP_TH_H/L`, `EXP_BK`), and the gradation-compression settings.
- **Calibration:** measure each unit's HCG/LCG ratio, since the tolerance is 5.6–6.0.
- **Motion:** no ghosting, because it is a single exposure (unlike DOL).

**Clear HDR register sequence:** port it from `imx585-v4l2-driver/imx585.c:383` (`common_clearHDR_mode`) and the format switch at `imx585.c:1308-1333`.
- `WDMODE 0x301A=0x10`, `COMBI_EN 0x3024=0x02`
- `0x3069=0x02`, `0x3074=0x63`
- DUR `0x3930/31`, `0x3A4C/4D/50/51`, ADTHEN `0x3E10=0x17`
- `0x493C/0x4940=0x41`, `EXP_GAIN 0x3081=0x02`
- For RAW16: `CCMP_EN=0`, `MDBIT 0x3023=0x03`

The will127534 git history removed ClearHDR once and later re-added it (`7b7265f` and later). Upstream v3 of the IMX585 patches also dropped it. **No mainline driver or RFC implements ClearHDR or RAW16.**

**On Jetson:**
- Argus and NVIDIA's ISP expect Bayer RAW10/12. RAW16 Clear HDR will probably need raw V4L2 capture (`v4l2-ctl` / `nvv4l2camerasrc`) and your own processing. **VERIFY** that VI accepts `csi_pixel_bit_depth=16`.
- The CSI-2 OB lines (data type `0x37`) give a per-frame black reference. **VERIFY** whether Tegra VI passes them or drops them.

---

## 6. Autofocus

**Confirmed: no PDAF.**
- The pixel layout (p.8) and color coding (p.24) show a plain Bayer array.
- The embedded data holds only register mirrors (§2.5).
- Sony's feature list has no phase detection.

**Plan: contrast-detect AF on raw green pixels.**
1. Choose an ROI, e.g. the center 512×512. Average Gr and Gb, or use only one of them.
2. Focus measure: Laplacian variance or Tenengrad (Sobel energy). Normalize by mean brightness so exposure changes don't bias it.
3. Search: coarse sweep, then a fine hill-climb or parabolic fit around the peak.
4. **Timing rules from the datasheet:**
   - After stream start, drop 8 frames.
   - After moving the lens, wait for it to settle, then **skip at least 1 frame**. A rolling-shutter frame that straddles the move is invalid.
   - Keep exposure and gain fixed during a sweep. Update them with REGHOLD / group hold, which apply at frame boundaries.
   - If embedded data is enabled, check SHR and BLKLEVEL on each frame so you never score a frame taken with different settings.
5. **Speed options:**
   - RAW10 at 90 fps.
   - Later, a sensor-cropped window at higher fps. Registers: `WINMODE 0x3018=0x04`, `PIX_HST/HWIDTH 0x303C/0x303E`, `PIX_VST/VWIDTH 0x3044/0x3046`; alignment 16 for width, 4 for the others (from `imx678.c`, **VERIFY** for IMX585). This needs a mode switch.

**Alternatives:**
- External ToF or laser rangefinder feeding a distance-to-focus lookup table.
- A different sensor with PDAF (IMX519/IMX708), with much smaller pixels.

---

## 7. Platform: Yocto and meta-tegra status (Sept 2026)

### 7.1 Yocto Project

| Release | Version | Status | `linux-yocto` |
|---|---|---|---|
| Blacksail | 6.1 | in development (master) | 6.18, **7.2** |
| **Wrynose** | **6.0** | **LTS until Apr 2030**; 6.0.3 released Aug 2026 | **6.18** |
| Scarthgap | 5.0 | LTS until Apr 2028 | (older) |

### 7.2 NVIDIA / OE4T

- **Official Yocto support started with JetPack 7.2.** The official path is OE4T `meta-tegra` (maintained by Matt Madison, with NVIDIA contributing).
  - Layer stack: Poky/OE-Core → meta-openembedded → **meta-tegra** → optional meta-tegra-community → product layer.
- **meta-tegra branches:**
  - `wrynose`: JetPack 7.2.1 / L4T R39.2.1, `LAYERSERIES_COMPAT: wrynose`.
  - `master`: the same BSP, `LAYERSERIES_COMPAT: blacksail`.
  - Also `whinlatter`, `walnascar`, `scarthgap`, `master-l4t-r38.x`, …
- **JetPack 7.2 on Orin** (Orin Nano, NX, AGX) moves from the 5.15 Jammy kernel to **`linux-noble-nvidia-tegra` 6.8.12**. JetPack 7.0 and 7.1 were Thor-only.
  - Other changes: **Jetson-IO** and SIPL/UDDF camera support added; only `initrd-flash` is supported; CUDA 13.2, TensorRT 10.16.2.
- **Kernel recipes on `wrynose` / `master`** (`recipes-kernel/linux/`):
  - `linux-noble-nvidia-tegra_6.8.bb` (plus `-rt`): **default** (`PREFERRED_PROVIDER_virtual/kernel ?= "linux-noble-nvidia-tegra"`).
  - `linux-yocto_6.18.bbappend` (plus `-rt`): uses `tegra-kernel-cache` branch `yocto-6.18`.
  - `whinlatter` instead has `linux-jammy-nvidia-tegra_5.15` and `linux-yocto_6.12` (the old test setup).
- **`nvidia-kernel-oot`** builds NVIDIA's out-of-tree modules, including the camera stack:
  - `tegra-camera`, `tegra-camera-platform`, `tegra-camera-rtcpu`, `capture-ivc`, `nvhost-capture`
  - sample sensors `nv-imx219` and `nv-imx477`, in package `nvidia-kernel-oot-cameras`
  - It has special skip flags for `linux-yocto`, so OOT modules are expected to build against the 6.18 kernel as well.
- **What `nvidia-kernel-oot` installs:**
  - `${includedir}/nvidia-kernel-oot/Module.symvers` (with `kernel-module-nvidia-kernel-oot` as a symlink)
  - `nvidia-oot/include/*` into `${includedir}/nvidia-kernel-oot/`
  - DT sources into `/usr/src/device-tree`
  - The generated `conftest.h` does **not** appear to be installed. **VERIFY.**

### 7.3 Device tree overlays in meta-tegra (`docs/Using-device-tree-overlays.md`)

- **Custom overlays:** a recipe that `inherit tegra-devicetree` with `PROVIDES = "virtual/dtbo"`, then `PREFERRED_PROVIDER_virtual/dtbo = "<recipe>"`. Built `.dtbo` files go to `/boot/devicetree/`.
- **Applying an overlay at boot, two ways:**
  - Stored in SPI flash, applied by UEFI: `TEGRA_PLUGIN_MANAGER_OVERLAYS:append:<machine> = " foo.dtbo"`. Append, don't overwrite.
  - Stored in rootfs: extlinux `OVERLAYS` via `UBOOT_EXTLINUX_FDTOVERLAYS` (see `docs/extlinux.conf-support.md`).
- **Or skip overlays entirely:** build a custom `virtual/dtb` from the `tegrademo-devicetree` example, with base `tegra234-p3768-0000+p3767-0005-oe4t.dts` for the Orin Nano devkit.
- **Connector name:** Kurokesu's overlays set `jetson-header-name` (22pin on L4T ≥ 36.5, 24pin earlier) for Jetson-IO. That doesn't matter for build-time application, but check what Jetson-IO on R39 expects if you use it.

---

## 8. Adaptation plan for a current Yocto image

### Choose a path

| | **Path A: NVIDIA stack (recommended for Argus/ISP)** | **Path B: mainline-style** |
|---|---|---|
| Kernel | `linux-noble-nvidia-tegra` 6.8 (default) | `linux-yocto` 6.18 (or newer) |
| Driver | port `nv_imx585.c` (`tegracam`) | IMX585 variant of the shared STARVIS 2 driver (`imx678.c` + RFC) |
| Capture | VI (non-media-controller), Argus, `nvarguscamerasrc`, `nvv4l2camerasrc` | media-controller / libcamera. **VERIFY** that Tegra VI in media-controller mode works with meta-tegra on 6.18 |
| HDR / RAW16 | raw V4L2 path likely needed | add ClearHDR yourself |
| Effort | low to medium | high |

### Path A steps

1. **Layers:** `poky` (wrynose) + `meta-openembedded` + `meta-tegra` (wrynose). Optionally start from `tegra-demo-distro`.
   - `MACHINE = "jetson-orin-nano-devkit-nvme"` (or your carrier's machine).
2. **Kernel module recipe.** Choose one:
   - **A1 (simplest):** add `nv_imx585.c` and `imx585_mode_tbls.h` to `nvidia-kernel-oot` as a patch via `EXTRA_PATCHES`, next to `nv_imx477.c`.
     - Add the object to the matching kbuild Makefile.
     - It then inherits NVIDIA's `conftest.h` and gets packaged alongside the other camera drivers.
     - **VERIFY** the path inside `nvidia-oot`, e.g. `drivers/media/i2c/`.
   - **A2 (separate recipe):**
     ```bitbake
     SUMMARY = "Sony IMX585 tegracam sensor driver"
     LICENSE = "GPL-2.0-only"
     inherit module
     DEPENDS += "nvidia-kernel-oot"
     COMPATIBLE_MACHINE = "(tegra)"
     SRC_URI = "file://nv_imx585.c file://imx585_mode_tbls.h file://Makefile file://conftest.h"
     S = "${UNPACKDIR}"
     EXTRA_OEMAKE += "KBUILD_EXTRA_SYMBOLS=${STAGING_INCDIR}/nvidia-kernel-oot/Module.symvers"
     # Kbuild: obj-m += nv_imx585.o ; ccflags-y += -I${STAGING_INCDIR}/nvidia-kernel-oot -I$(src)
     RPROVIDES:${PN} += "kernel-module-nv-imx585"
     KERNEL_MODULE_AUTOLOAD += "nv_imx585"
     ```
     - `conftest.h`: generate it once with Kurokesu's `scripts/conftest.sh` against the target kernel, or delete the `#if defined(NV_...)` guards for the one kernel you build. On 6.8, probe takes one argument and remove returns `void`.
     - **VERIFY** the include layout matches `<media/tegracam_core.h>` under `${STAGING_INCDIR}/nvidia-kernel-oot`.
3. **Device tree:**
   - Take `tegra234-p3767-camera-p3768-imx585-C.dts` (4-lane cam1) or `-A.dts` (2-lane cam0).
   - Build it through a `virtual/dtbo` recipe (§7.3), or merge the fragment into a custom `virtual/dtb`.
   - Apply with `TEGRA_PLUGIN_MANAGER_OVERLAYS:append:<machine>` or `UBOOT_EXTLINUX_FDTOVERLAYS`.
   - The `#include <dt-bindings/tegra234-p3767-0000-common.h>` header comes from NVIDIA's DT sources, which `nvidia-kernel-oot` installs to `/usr/src/device-tree`. `tegra-devicetree.bbclass` should handle the include paths. **VERIFY.**
   - While editing, apply the §3.4 DT fixes: `max_gain_val="300"`, and new modes for higher link rates, RAW10, and HDR.
4. **Image contents:**
   - `nvidia-kernel-oot-cameras`, your module
   - `tegra-libraries-camera` (Argus)
   - `gstreamer1.0-plugins-nvarguscamerasrc` and/or `gstreamer1.0-plugins-nvv4l2camerasrc`
   - `v4l-utils`, `i2c-tools`
5. **Bring-up checks:**
   - `dmesg | grep -i imx585`: probe passed and subdev registered.
   - `i2cdetect` on the camera mux bus: device at `0x1a` (cam1 is behind `cam_i2cmux/i2c@1`).
   - `i2cget` of `0x3000` → `0x01` in standby; of `0x4D1C/0x4D1D` after `STANDBY=0` + 80 ms (F4 test).
   - `v4l2-ctl -d /dev/video0 --list-formats-ext`, then `--stream-mmap --stream-count=30 --stream-to=f.raw`.
   - Test pattern first (`test_mode=5`, horizontal color bars), then real frames.
   - Argus: `nvarguscamerasrc sensor-id=0 ! ... ` at the default 25 fps, then the higher-rate modes.
6. **Driver improvements, in order:** F2/F3 (stream order), F1 (gain cap), F5 (1782 Mbps, 60 fps mode), RAW10 at 90 fps, embedded data (`0x303A` + `embedded_metadata_height="1"`, **VERIFY** on VI), Clear HDR RAW16.

### Path B steps (outline)

1. Kernel: `PREFERRED_PROVIDER_virtual/kernel = "linux-yocto"` (6.18 on wrynose). Check that `tegra-kernel-cache` enables the Tegra camera stack.
2. Driver:
   - Backport `drivers/media/i2c/imx678.c` from 7.3 together with Dave Stevenson's STARVIS 2 RFC (variant struct).
   - Add an `imx585` variant: native and active areas, min HMAX table, IMX585 common registers from will127534 or Sony's sheet, gain register `0x306C`, ID value if F4 checks out.
   - It may need helpers newer than 6.18, such as `devm_v4l2_sensor_clk_get` and `v4l2_link_freq_to_bitmap`. **VERIFY** each against 6.18.
3. DT, following the `sony,imx678.yaml` style:
   ```dts
   camera@1a {
       compatible = "sony,imx585-aaqj1", "sony,imx585";
       reg = <0x1a>;
       clocks = <&imx585_osc>;          /* fixed-clock node, 24 MHz module oscillator */
       avdd-supply = <...>; ovdd-supply = <...>; dvdd-supply = <...>;
       reset-gpios = <&gpio CAM1_PWDN GPIO_ACTIVE_LOW>;
       port { endpoint { data-lanes = <1 2 3 4>; link-frequencies = /bits/ 64 <891000000>; remote-endpoint = <...>; }; };
   };
   imx585_osc: clock-imx585 { compatible = "fixed-clock"; #clock-cells = <0>; clock-frequency = <24000000>; };
   ```
   - Note: `devm_v4l2_sensor_clk_get()` only creates a fixed clock from `clock-frequency` on non-DT or legacy platforms. On DT it returns `-ENOENT` without `clocks` (`v4l2-common.c`, 7.3).
   - GPIO polarity: upstream treats XCLR as active-low reset (`GPIOD_OUT_HIGH` = held in reset). Kurokesu's overlay uses `GPIO_ACTIVE_HIGH` with the logic inverted in the driver. Pick one convention and stick to it.
4. Userspace: libcamera needs a sensor helper and tuning files, plus a Tegra pipeline handler. **VERIFY** one exists; this may block Path B.
5. Watch upstream:
   - STARVIS 2 common driver (IMX585 listed as future)
   - raw sensor model and metadata series (embedded data, crop, binning)

---

## 9. What to reuse from upstream `imx678.c` (7.3-rc3)

Register overlap with the IMX585 (confirmed against the will127534 and Kurokesu drivers unless marked):

| Function | Register |
|---|---|
| STANDBY | `0x3000` |
| REGHOLD | `0x3001` |
| XMSTA | `0x3002` |
| INCK_SEL | `0x3014` (upstream table adds 36/18/13.5 MHz) |
| DATARATE_SEL | `0x3015` |
| WINMODE | `0x3018` |
| WDMODE | `0x301A` |
| ADDMODE | `0x301B` |
| WINMODEH / V | `0x3020` / `0x3021` |
| ADBIT / MDBIT | `0x3022` / `0x3023` |
| VMAX | `0x3028` (24-bit LE) |
| HMAX | `0x302C` (16-bit LE) |
| PIX_HST / HWIDTH | `0x303C` / `0x303E` |
| LANEMODE | `0x3040` |
| PIX_VST / VWIDTH | `0x3044` / `0x3046` |
| SHR | `0x3050` (24-bit LE) |
| XXS_OUTSEL / DRV | `0x30A4` / `0x30A6` |
| TPG_EN / PATSEL / COLORWIDTH | `0x30E0` / `0x30E2` / `0x30E4` |

**Differences:**
- **Gain:** IMX678 uses `0x3070`; IMX585 uses **`0x306C`**.
- **IMX678-only registers:** `VCMODE 0x301E`, `INTERFACE_SEL 0x4E3C`, `GAIN_PGC_FIDMD 0x3400`, and its ~370-entry `common_regs`. **Do not copy these tables.**

**Worth adopting:**
- min HMAX held at 550 above 891 MHz (§4)
- analog gain capped at 100
- datasheet-correct start/stop ordering
- ID and mono auto-detection
- window cropping with alignment rules
- test pattern menu with 12 patterns
- variant compatibles with a generic fallback
- supply names `avdd` / `ovdd` / `dvdd`
- `link-frequencies` required in the binding

**Upstream timeline:**
- **Aug 2025:** Will Whang's standalone IMX585 series v3 (dropped ClearHDR and HCG). Not merged.
- **Jul 2026:** `imx678.c` merged (Ideas on Board, Jai Luthra), based on Will Whang and Soho Enterprise's driver. Commit message lists what's missing: RAW10, pixel-perfect crop with flips, and (pending the raw sensor model) embedded data, free cropping, faster cropped frame rates, binning.
- **Aug 2026:** Dave Stevenson's RFC "Update imx678 to act as a common STARVIS 2 driver": always use window mode, `imx678_variant` parameterization, IMX662 and IMX675 variants. **IMX585 and IMX676 listed as future work.**
- **2026:** "common raw sensor model" RFC and metadata series. IMX678 gains an internal image pad, streams, an embedded-data line, analog crop, and binning.

---

## 10. Open questions and VERIFY list

1. Does IMX585 expose a module ID at `0x4D1C` and a mono flag at `0x4D18`, as IMX678 does?
2. Clear HDR frame-rate limit and exact timing (VMAX ×2?). Needs Sony's Clear HDR application note.
3. RAW10 minimum HMAX per link rate. Needs Sony's register sheet.
4. Signal integrity of the Kurokesu module and cable at 1782 Mbps on Orin Nano cam1.
5. Does Tegra VI accept RAW16 (`csi_pixel_bit_depth=16`)? Does it pass or drop OB (`0x37`) and embedded data (`0x12`) lines? Does `embedded_metadata_height` work with IMX585 embedded data?
6. The Kurokesu 500 ms XCLR delay: required by the module's power design, or just conservative?
7. meta-tegra wrynose:
   - Is `conftest.h` exported by `nvidia-kernel-oot`?
   - What are the exact include paths for `tegracam_core.h`?
   - Does `tegra-devicetree.bbclass` resolve `dt-bindings/tegra234-p3767-0000-common.h`?
8. Jetson-IO on R39: header name string for the CSI connector (22pin vs 24pin) if using runtime overlays.
9. Path B: is there a libcamera pipeline handler for Tegra VI? How complete is the camera config in `tegra-kernel-cache` on linux-yocto 6.18?
10. Obtain from Kurokesu, Khadas, or Sony: *IMX585_Standard_Register_Setting.xlsx*, *Software Reference Manual*, *Clear HDR* and *DOL-HDR* application notes.

---

## 11. Sources

**Local**
- `imx585-datasheet.pdf` (Sony IMX585-AAQJ1-C, E21Y12D37). Downloaded from https://dl.khadas.com/products/add-ons/cameras/imx585/datasheet/imx585-datasheet.pdf
- `imx585-v4l2-driver/imx585.c`, `README.md`, branch `jetson-overlay-6.12.y`
- `imx585-jetson-driver/nv_imx585.c`, `imx585_mode_tbls.h`, `tegra234-p3767-camera-p3768-imx585-{A,C}.dts`, `Makefile`, `scripts/conftest.sh`

**Sony / vendors**
- IMX585-AAQJ1 flyer: https://www.sony-semicon.com/files/62/flyer_security/IMX585-AAQJ1_Flyer.pdf
- FRAMOS IMX585: https://framos.com/products/sensors/area-sensors/sony-starvis-2-imx585aaqj1-c-25437/
- Kurokesu Jetson driver: https://github.com/Kurokesu/imx585-jetson-driver
- will127534 driver: https://github.com/will127534/imx585-v4l2-driver

**Yocto / NVIDIA / OE4T**
- Yocto releases: https://www.yoctoproject.org/development/releases/
- OE-Core kernel recipes (wrynose): https://git.openembedded.org/openembedded-core/tree/meta/recipes-kernel/linux?h=wrynose
- OE-Core kernel recipes (master): https://git.openembedded.org/openembedded-core/tree/meta/recipes-kernel/linux?h=master
- meta-tegra: https://github.com/OE4T/meta-tegra (branches `wrynose`, `master`)
- meta-tegra JetPack 7.2 notes: https://github.com/OE4T/meta-tegra/blob/master/docs/release-notes/JetPack-7.2-L4T-R39.2.0-Notes.md
- meta-tegra DT overlays doc: https://github.com/OE4T/meta-tegra/blob/wrynose/docs/Using-device-tree-overlays.md
- NVIDIA Jetson Linux r39.2, Yocto on Jetson: https://docs.nvidia.com/jetson/archives/r39.2/DeveloperGuide/AR/YoctoOnJetson.html
- NVIDIA announcement: https://x.com/NVIDIARobotics/status/2064852593436983404
- Antmicro on the Yocto on Jetson launch: https://antmicro.com/blog/2026/06/antmicro-support-for-yocto-on-nvidia-jetson

**Upstream Linux**
- imx678 driver (7.3-rc3): https://github.com/torvalds/linux/blob/238650ef6c7c7cca08e032527329424c9fbd70e5/drivers/media/i2c/imx678.c
- sony,imx678.yaml: https://github.com/torvalds/linux/blob/238650ef6c7c7cca08e032527329424c9fbd70e5/Documentation/devicetree/bindings/media/i2c/sony%2Cimx678.yaml
- STARVIS 2 common driver RFC: https://ratatoskr.run/linux-media/2026/08/17372052/t
- Common raw sensor model RFC: https://ratatoskr.run/linux-media/2026/07/17209940/t
- Metadata series preparation v7: https://ratatoskr.run/linux-media/2026/08/17381668/t
- IMX585 v3 series (Aug 2025): https://lkml.org/lkml/2025/8/16/128 and https://lwn.net/Articles/1028194/
