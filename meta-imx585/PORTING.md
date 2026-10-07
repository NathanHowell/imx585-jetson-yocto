# Porting status: R36.4 / 6.12 → R39.2.1 / 6.8.12

The recipes in this layer were carried over from `meta-tegrademo` in the old
`tegra-demo-distro` tree (snapshot in `../oe4t-config/`). They are **unbuilt on
wrynose**. Everything below is a known or suspected porting item; nothing here
has been tested against JetPack 7.2.1.

## The driver: decided

**tegracam, starting from the archived conversion.** `recipes-kernel/imx585/` now
builds a vendored copy of `13947db imx585: seed sensor_mode_properties for
tegracam` — the devtool branch tip from
`../oe4t-config/archive/bundles/imx585-v4l2-driver-devtool.bundle`, checked in
verbatim as `files/imx585.c` so the fixes below land as reviewable commits.

The source is vendored rather than fetched because the bundle was the only copy
and it lived on one disk, which is the failure mode this repo exists to fix.

### Maintenance model: this is the upstream

There is **no upstream Jetson/tegracam IMX585 driver for StarlightEye.** The
tegracam conversion was never published anywhere; it exists only in
`../oe4t-config/archive/bundles/`. So `files/imx585.c` is not a fork being carried
against someone else's tree — we maintain it, until it is stable, and only then
decide whether and how to offer it back to will127534's repo.

We are, however, synthesising **two drivers that each already work**, which is a
much better position than it sounds:

| Reference | Where | Works on | Authority for |
|---|---|---|---|
| will127534's RPi driver | `oe4t-config/archive/bundles/imx585-v4l2-driver-devtool.bundle` at `682e872` | StarlightEye, Raspberry Pi | register tables and values, min-HMAX per link frequency, ClearHDR sequences, mono, the board itself (he designed it) |
| Kurokesu `nv_imx585.c` | `../imx585-jetson-driver/` | a different IMX585 board, Jetson/tegracam | what tegracam needs in order to produce frames: mode property units and shapes, gain in decibels, group hold, and two working `tegra234-p3767-camera-p3768-imx585-{A,C}.dts` |

Our driver is the intersection: will's hardware, Kurokesu's host. Notably the
archived conversion did the *harder* half — it kept will's register layer and
hand-wrote a tegracam layer on top — rather than taking Kurokesu's already-working
tegracam layer and feeding it will's tables. That is why the bugs cluster in the
tegracam glue (mode props, gain units, clock domains) and not in the register
tables, which diffed clean.

Practical rule: when a tegracam-side value is in doubt, check Kurokesu's DTS and
`nv_imx585.c` before reasoning from the datasheet; when a register value or timing
table is in doubt, check will's driver. Kurokesu's `-C.dts` is the cam1 variant,
which is our configuration.

Three consequences, because this changes how decisions get made here:

- **Do not reason about "diffs against upstream."** There is nothing to rebase
  onto. The vendored-baseline-then-fixes commit split is still right, but for
  provenance and bisection — so a future reader can tell what the archive handed
  us from what we changed and why — not to produce a patch series.
- **will127534's driver stays the reference for register values and timing**, even
  though it is a different driver for a different host. His tables, min-HMAX
  calculations and link-frequency regvals are the authority; the tegracam subdev
  layer above them is ours. That is why restored blocks keep his line layout: so
  the tables can still be diffed against his when he changes them.
- **We are free to restructure.** Nothing external constrains the shape of this
  file, so the dropped mono/RAW16 support can be brought back properly rather
  than minimally if it is wanted.

Upstreaming later is a real divergence question, not a patch submission: will's
driver is an upstream-style V4L2 subdev for Raspberry Pi, and this is a tegracam
driver for Tegra VI. Realistically that is a separate source file or a port
branch in his tree, not a merge. Not a question for now.

### What the conversion kept, dropped, and got wrong

Of the 25 archived commits, `0003`–`0015` are bpftrace debug churn and
`0016`–`0021` are attempts to make the *upstream-style* subdev survive Tegra VI,
superseded by `0022`. The real content is `0022` (`imx585.c` −1777/+2388) plus
`0023`–`0025`.

| | Upstream `682e872` (1768 lines) | Converted `13947db` (1043 lines) |
|---|---|---|
| Link freq table | 8 rates 297–1188 MHz, regvals, per-rate min HMAX | **kept intact** — the most valuable artifact, and exactly Kurokesu's F5 gap |
| ClearHDR | `clear_hdr` + RAW16 Bayer codes + format negotiation | register sequence kept, **no mode wired to it**, `min/max_hdr_ratio = 1` |
| Mono | `mono` bool, `Y12`/`Y16` codes | **gone** |
| RAW12/RAW16 bus codes | 4 Bayer orientations × 2 depths | **gone** |

Structurally it is complete: every tegracam callback is present and sanely
shaped, `tegracam_device_register()` → `tegracam_v4l2subdev_register()` in the
right order, and `imx585_populate_sensor_mode_props()` *derives* the Tegra mode
properties from link frequency and lane count instead of hardcoding them — which
is better than static DT values and a direct answer to the placeholder problems
in `../IMX585-YOCTO-NOTES.md` §3.2.

It never produced a frame. These are the reasons, in the order they block one.

### Fixes, ordered

1. **XMSTA is never released — FIXED.** `imx585_common_regs[0]` writes `0x3002 = 0x01`
   (master stop) at mode init, and nothing ever writes `0x3002 = 0x00`.
   `imx585_start_streaming()` releases STANDBY, sleeps 25 ms and returns, leaving
   the sensor in master stop. `IMX585_REG_XMSTA` is defined and otherwise unused.
   This is the single best explanation for "never reached a working stream".

   **VERIFIED** against the pre-conversion driver, which writes it from code
   rather than from a table. Both the last pure-upstream state (`682e872`, 32
   commits back, reached via `ecf5372 Merge pull request #14 from
   will127534/main`) and the conversion's immediate parent (`8d09ef8`, at its
   line 1599) carry the same call:

   ```c
   if (imx585->sync_mode != SYNC_EXTERNAL)
           cci_write(imx585->regmap, IMX585_REG_XMSTA, 0x00, NULL);

   ret = cci_write(imx585->regmap, IMX585_REG_MODE_SELECT,
                   IMX585_MODE_STREAMING, NULL);
   ```

   Note the order: XMSTA is released **before** MODE_SELECT, not after the
   post-MODE_SELECT sleep. We have no external-sync plumbing, so the guard
   collapses to an unconditional write.

2. **`imx585_common_regs` is truncated: 109 writes missing — FIXED.** The baseline's
   `common_regs[]` has 226 entries; ours has 117. Ours is an *exact prefix* --
   every entry that is present is byte-for-byte correct, and the table simply
   stops after `0x4bc0`. Missing is the contiguous run `0x4c14` through `0x5226`
   (the 0x4cxx/0x4dxx/0x4exx/0x4fxx/0x51xx/0x52xx blocks), which are Sony's
   undocumented "shall be set to this value" calibration registers. Unlike the
   XMSTA write these are not obviously stream-fatal, but they are not optional
   either. The cause is not apparent from the diff -- the cut point is mid-block
   and matches no formatting boundary -- so treat it as an unexplained truncation
   rather than a deliberate trim. **VERIFIED** by normalising both tables to
   `addr value` pairs and diffing: the only delta is entries 118-226, and the
   count is identical in `682e872` and `8d09ef8`.

   **Counting trap:** the baseline packs *two* registers per line from line 272
   onward, so `grep -c CCI_REG` reports 117 for a 226-entry table and makes the
   truncation invisible. Count `grep -o` matches, not lines.

**Fixes 1 and 2 are applied and build clean** (all 1596 tasks, no warnings;
`imx585.ko` grew 78400 → 80280 bytes, consistent with 109 extra 16-byte
`cci_reg_sequence` entries plus the new write, and `depends=` is unchanged).

Fix 1 is a single `cci_write(priv->regmap, IMX585_REG_XMSTA, 0x00, NULL)` placed
before the `MODE_SELECT` write, not after the settling delay, with the
`SYNC_EXTERNAL` guard collapsed to unconditional and that noted in the comment for
whenever XVS/XHS slave mode gets wired up. Fix 2 restores the dropped block
verbatim, keeping the baseline's two-registers-per-line layout so the block stays
directly comparable against will127534's register tables, and the restored table
was re-diffed against the baseline: 226 entries, identical.

Both verified on hardware: the sensor probes and streams. See "First light".

3. **`imx585_init_inck_sel()` requires a readable clock rate.** It calls
   `clk_get_rate(priv->xclk)` and returns `-EINVAL` on no table match, before
   `board_setup` -- so a DT with no clock means probe dies with "unsupported xclk
   frequency". StarlightEye's INCK is an on-board 24 MHz oscillator (U5), so the
   DT's job is to *state* that rate, not to drive a clock. See "The hardware is
   StarlightEye" below. Not currently a blocker -- the overlay's `fixed-clock`
   satisfies the call.

4. **10-bit on a 12-bit sensor, twice — FIXED.** `const u32 bpp = 10` makes the advertised
   `pixel_clock` 20% too high, and `pixel_format = V4L2_PIX_FMT_SRGGB10`. This is
   §3.2's `csi_pixel_bit_depth=10` placeholder migrated from the DT into C. VI
   sizes its capture buffer and paces line timing off both.

5. **`embedded_metadata_height = 2` contradicts the register table — FIXED.** The mode
   props promise VI two lines of embedded metadata while `imx585_common_regs`
   writes `0x303a = 0x03`. **VERIFIED, and the register wins:** the
   pre-conversion driver carries that same write with the comment
   `/* Disable embedded data */`, so it is deliberate inherited behaviour and not
   a conversion artifact. Embedded data
   is off, therefore `embedded_metadata_height` must be `0`. Left at `2`, VI
   short-counts every frame, which also presents as no frames.

6. **Gain is dimensionally wrong — FIXED.** `imx585_set_gain()` writes `val` raw into
   `IMX585_REG_ANALOG_GAIN` while the mode props advertise `gain_factor = 16`,
   `min 16`, `max 512`, `step 1` -- a linear Q4 multiplier. IMX585 analog gain is
   0.3 dB/step, 0-240. No conversion, no clamp; `val = 512` writes past the
   analog range into digital. The `use_decibel_gain = true`, `gain_factor = 10`,
   0-720 step 3 shape is the correct one. This is F1 plus a unit bug.

7. **Both modes advertise 60 fps unconditionally — FIXED, and the real bug was bigger** (`imx585_60fps`), while §4 puts
   60 fps at 1782 Mbps and the DT selects 720.

**DT/driver agreement on link rate and lanes is VERIFIED.** `imx585_parse_endpoint()`
takes both values from a standard v4l2 fwnode endpoint -- `num_data_lanes` must be
2 or 4, and `ep.link_frequencies[0]` must match `imx585_link_freq_table[]` exactly
or `imx585_select_link_freq()` returns `-EINVAL` and probe fails. This is a real
trap, because Tegra camera nodes often omit `link-frequencies` entirely. Ours do
not: both sensor endpoints carry `link-frequencies = /bits/ 64 <720000000>` and
`data-lanes = <1 2 3 4>`, and 720000000 is `IMX585_LINK_FREQ_720MHZ` (regval
0x03). So the 4-lane/720 MHz choice is consistent end to end, and fix 7's 60 fps
claim is the only remaining mismatch on that axis.

The other four register tables (`imx585_normal_regs`, `imx585_clearhdr_regs`,
`imx585_mode_4k_regs`, `imx585_mode_fhd_regs`) were diffed against the baseline
the same way and are **faithful** -- 13/13, 4/4 and 4/4 entries, identical
contents. The truncation is confined to `imx585_common_regs`.

### The HMAX clock domain — the bug fix 7 was hiding

Chasing the 60 fps claim turned up something larger: **every derived timing value
was computed in the wrong clock domain.**

HMAX is expressed in `IMX585_PIXEL_RATE` (74.25 MHz) ticks, not in CSI
pixel-clock ticks, so the line rate is `74250000 / HMAX`. The derivation divided
by `pixel_clock` instead, inflating every result by
`pixel_clock / 74.25 MHz` — a factor of **6.46** at our 720 MHz link. It advertised
roughly 323 fps and a 3 ms frame time for a mode that runs at 50 fps, and
`line_length` was set to raw HMAX (660) where VI wants it in its own pixel-clock
domain (4267).

**VERIFIED two independent ways.** will127534's driver derives
`pixel_rate = width * IMX585_PIXEL_RATE / min_hmax`, which is the same relation.
And Kurokesu's working Jetson DT pairs `pix_clk_hz = 237600000` with
`line_length = 4224` at a 360 MHz link, where

```
237600000 / 4224  ==  74250000 / 1320  ==  56250 lines/s
```

and 1320 is will's min HMAX at 360 MHz. Two unrelated sources, one identity.

For our configuration — 720 MHz link, 4 lanes, 12 bpp, HMAX 660, VMAX 2250 — the
numbers now come out exactly:

| | value |
|---|---|
| line rate | `74250000 / 660` = 112500 lines/s |
| frame time | `2250 / 112500` = 20000 µs |
| max framerate | `74250000 * 1e6 / (660 * 2250)` = 50000000 µfps = **50.0 fps** |
| pixel_clock | `720e6 * 2 * 4 / 12` = 480000000 |
| line_length | `480e6 * 660 / 74.25e6` = 4267 |

So 60 fps was not merely optimistic, it was a symptom. 50 fps is the honest figure
at 1440 Mbps/lane; 60 would need ~1782 Mbps. Note this is double Kurokesu's 25 fps
precisely because we run double their lane rate — a good cross-check that both
sides are now consistent.

### Driver fixes 3-7 applied

All five, plus `set_group_hold()`, and the build is clean (1596 tasks, no
warnings):

- **bpp 10 → 12** and `pixel_format` `SRGGB10` → `SRGGB12`.
- **`embedded_metadata_height` 2 → 0**, now confirmed from three directions: the
  `0x303a = 0x03` write, the pre-conversion driver's `/* Disable embedded data */`
  comment on it, and Kurokesu's working DT.
- **Gain converted to decibels** — mode props become `gain_factor = 10`, 0..720
  step 3, and `set_gain()` clamps to the advertised range then does `val / 3` with
  a hard stop at `IMX585_ANALOG_GAIN_MAX` (240). Our register access was already
  fine: `CCI_REG16_LE(0x306c)` is what the CCI layer splits into 0x306c/0x306d,
  the same two writes Kurokesu does by hand, so this was units only, not width.
- **`set_group_hold()` implemented** against `REGHOLD` (0x3001). It was a stub
  returning 0, which told tegracam a hold had been taken when none had.
- **The frmfmt table** now advertises 50 fps.

The DT mode properties were updated to match even though
`imx585_populate_sensor_mode_props()` overwrites all of them from C and they are
therefore inert — leaving `embedded_metadata_height = "2"` and a Q4 gain scale in
a file someone will read as documentation is a trap, and they become live the
moment the driver is swapped. The overlay compiles and the values were confirmed
by round-tripping the deployed `.dtbo` with `dtc -I dtb`.

Verified on hardware: the supplies and `cil_settletime = 0` work as described.
Whether ClearHDR/mono are wanted back is still open.

### The control path

Reading the driver against `camera_common.c`, `tegracam_v4l2.c` and Kurokesu's
`nv_imx585.c` turned up four more defects, each of which denies a usable frame
or a usable control on its own. All four are fixed and verified on hardware.

- **Mode table vs. DT.** The driver listed 1928x1090 at index 0 and 3856x2180 at
  index 1 while the overlay describes a single `mode0` of 3856x2180.
  `camera_common_try_fmt` picks `s_data->mode` from the frmfmt entry and tegracam
  indexes `sensor_modes[]` with it, so `populate_sensor_mode_props()` overwrote
  the DT's 4K geometry with 1928x1090 and a 4K format request indexed past the
  one-element array. Now: `imx585_modes[]` is 4K only, index for index with the
  DT, and `populate_sensor_mode_props()` fails probe if the count or any
  width/height disagrees. The binned FHD mode returns when the overlay carries a
  `mode1` for it and the min-HMAX table has a binned row.
- **Exposure units.** `set_exposure()` subtracted microseconds from VMAX in lines;
  anything over 2250 µs underflowed and clamped to `IMX585_VMAX_MAX`, which is
  larger than VMAX. Now converted at `IMX585_PIXEL_RATE / (HMAX * 1e6)`, clamped
  to `[IMX585_EXPOSURE_MIN, VMAX - SHR_MIN]`, SHR kept even as will's driver does.
- **Frame-rate floor.** `set_frame_rate()` clamped VMAX to at least
  `min_framerate`, which is 1000000 micro-fps, pinning VMAX between 1,000,000 and
  1,048,575 lines (about 0.07 fps) for every write. Now `VMAX = PIXEL_RATE * 1e6 /
  (HMAX * µfps)`, floored at the mode's default VMAX (its maximum rate), and
  exposure is re-applied afterwards because SHR is relative to VMAX.
- **Stream start.** tegracam calls `set_mode`, then the overrides, then
  `start_streaming`. Both of ours ran `imx585_mode_init()`, so every register was
  written twice and the second pass reset VMAX after any frame-rate override. And
  the overrides only run when the VI channel's `override_enable` control is on,
  which it is not in a plain V4L2 session; `tegracam_set_ctrls` also drops writes
  while the sensor is unpowered. Now `set_mode` only records the mode, the
  `set_*` callbacks cache the requested value in `priv` as well as writing it, and
  `start_streaming` applies gain, VMAX and SHR itself after the mode tables.
  Controls set with `v4l2-ctl` before streaming therefore take effect without
  `override_enable`.

Alongside: `REGCACHE_RBTREE` → `REGCACHE_NONE` (the cache went stale across every
power cycle and nothing read-modify-writes); `imx585_parse_dt()` no longer reads
`avdd-reg`/`dvdd-reg`/`iovdd-reg`/`reset-gpios` into fields nothing consumed; the
"model ID" read at 0x0016/0x0017 — IMX219 addresses, the IMX585 map starts at
0x3000 and neither reference driver identifies the part — is replaced by a
presence check that reads `MODE_SELECT` after the reset pulse and expects
STANDBY; `reset-gpios` is now `GPIO_ACTIVE_LOW` in the overlay and the driver
asserts/releases it in the conventional sense (CAM_EN is XCLR: low is reset);
`MODULE_AUTHOR` is no longer NVIDIA's boilerplate.

### First light

Verified on the devkit with StarlightEye on cam1 (J21, the 4-lane connector) and
the CEF168 on the I2C header, R39.2.1, `imx585-console-image`:

- The sensor probes at 9-001a, the TMP117 at 9-0048, and tegra-capture-vi binds
  the sensor and the CEF168 (`cef168 0-000d`, a Lens sub-device) into one media
  graph. The nvcsi "Failed to create device link (0x180)" line at boot is
  cosmetic; NVIDIA's own sensors behind the cam_i2cmux print it too.
- NVCSI configures PP 2 / port C, PHY 1, 4 lanes, D-PHY at 720 MHz, matching the
  overlay, with no CSI errors in the RTCPU trace.
- `v4l2-ctl --stream-mmap` runs at a measured 50.00 fps in the 4K mode.
- The data is linear. A lens-cap frame reads 202.8 ± 0.69 on all four Bayer
  channels, which is the sensor's default black level (50 in 10-bit units) at 12
  bits, with no per-channel offset, gain or clipping.

Two findings from those frames that every consumer has to know:

**Sample layout.** VI writes RAW12 into 16-bit words left-aligned: the 12-bit
value is in bits 15..4 and the low nibble is always zero. Shift right by 4 for the
12-bit value, or treat black as 3243 and full scale as 65520 in the 16-bit domain.

**Stride.** The VI writes memory in 64-byte atoms, and this kernel's
`TEGRA_STRIDE_ALIGNMENT` is 1, so it programs whatever `bytesperline` the format
carries. 3856 x 2 = 7712 bytes is 32 short of a multiple of 64, and with that
stride every odd line starts half an atom off and loses its last 16 pixels, which
arrive as zeros. The stride must be rounded up to a multiple of 64 by the
application: for this mode `bytesperline = 7744`, i.e. 3872 words per line of
which 3856 are image and 16 are zero padding. With v4l2-ctl that is
`--set-fmt-video=width=3856,height=2180,pixelformat=RG12,bytesperline=7744`, and
the frame is then 16,881,920 bytes. The channel driver also exposes this as the
`preferred_stride` control. Raising HMAX does nothing for it; the sensor is not
involved.

Two driver defects surfaced in the same session and are fixed:
`imx585_apply_frame_rate()` divided by `HMAX * micro-fps` with `div_u64()`, whose
divisor is 32-bit, so 50 fps requests produced VMAX 25296 (4.45 fps) instead of
2250; it and the exposure conversion now use `div64_u64()`. And tegracam creates
gain, exposure and frame_rate with a default of 0, clamps them to the mode minimum
when it narrows the ranges at stream start, and never pushes those three into the
sensor, so the controls reported the minimums while the sensor ran the DT
defaults cached in `priv`. `imx585_seed_controls()` writes the defaults into the
controls at probe so the two agree.

A third defect hid behind the stream-start fix above: `tegracam_set_ctrls`
gates every control write on `g_input_status`, which reports
`s_data->power->state`, and nothing in the framework ever sets that field. It
stayed `SWITCH_OFF`, so the `set_*()` callbacks never ran and `priv` never left
the DT defaults. `imx585_power_on()` now sets `SWITCH_ON` and `imx585_power_off()`
clears it, as NVIDIA's reference drivers do. Writes that still arrive while the
sensor is unpowered reach the v4l2 control only, so `imx585_start_streaming()`
first calls `imx585_sync_controls()`, which copies the controls' current gain,
exposure and frame rate into `priv` before applying them.

### The hardware is StarlightEye, not a Kurokesu module

The camera is
**[will127534/StarlightEye](https://github.com/will127534/StarlightEye) V2.0** —
and will127534 is also the author of the `imx585.c` this layer builds. The driver
and the board are by the same person, for each other. Kurokesu's values
(`pix_clk_hz = 237600000`, fixed 720 Mbps, `line_length = 4224`) describe
different hardware and must not be copied.

Read off the schematic (V2.0, 2025-07-26, KiCad 8.0.4) and the upstream RPi
overlay (`main:imx585-overlay.dts` in the driver repo):

| | |
|---|---|
| INCK | **U5, SX2M24.000M20F30TNN — a 24.000 MHz on-board oscillator**, `Out` → IMX585 pin `F4 INCK`. Nothing from the host clocks this sensor. |
| Lanes | 4 (`CAM_D0..D3` + `CAM_CLK`), 22-pin FPC with the CM4 IO-board pinout → **must be cam1**; cam0 on p3768 is 2-lane. |
| Rails | All generated on-board from the FPC: U10 TPSM83100 steps up to 3.6 V, then `+3.3VA` (U3 TPS7A90), `+1V8` (U2 LP5907), `+1V1`. Matching the driver's `vana`/`vddl`/`vdig`. |
| Host control | One GPIO, `CAM_EN` — the FPC's single camera GPIO, which is why nothing else can have an interrupt. |
| Sensor I2C | 1.8 V, bridged to the FPC's 3.3 V by U9 TCA9406. |
| Sync | `XVS`/`XHS`/`XMASTER` on coax J3/J4/J5, and the upstream overlay sets `sony,sync-mode = "internal-leader"`. |

### DT corrections applied

All of the following are in `imx585-overlay.dts` and verified by compiling with
`dtc -@` and decompiling the result.

| | Was | Now |
|---|---|---|
| Sensor clock | `clocks = <&bpmp TEGRA234_CLK_EXTPERIPH1>`, `assigned-clock-rates = <24000000>`, `mclk = "extperiph1"` | `imx585_inck`, a `fixed-clock` at 24 MHz; `clocks = <&imx585_inck>`, `clock-names`/`mclk` = `"inck"` |
| `link-frequencies` | 594 MHz (`IMX585_LANE_RATE 1188000000`) | **720 MHz** (`1440000000`), the designer's own value |
| `csi_pixel_bit_depth` | `"10"` (both modes) | `"12"` |
| cam0 `imx585_a` | `status = "okay"` | no node — 2-lane connector; the overlay describes cam1 only |
| cam1 `imx585_c` | `status = "disabled"` | **`status = "okay"`** — `serial_c`, 4 lanes, `lane_polarity = "0"` |

**Fix 2 is settled by the oscillator.** The DT's job is to *state* the rate, not
drive a clock, and a `fixed-clock` is safe here because the driver only ever calls
`clk_get_rate()` (→ `imx585_inck_table`, 24 MHz → `inck_sel 0x04`) and
`clk_prepare_enable()` — never `clk_set_rate()`, which a fixed-clock would reject.
The `"inck"` name is load-bearing: `imx585_parse_dt()` reads the `mclk` string into
`pdata->mclk_name` and `imx585_power_get()` does
`devm_clk_get(dev, pdata->mclk_name)`, so `mclk` and `clock-names` must agree.
IMX585-YOCTO-NOTES.md §8.5 had this right; the EXTPERIPH1 form reached the correct
`inck_sel` by configuring a Tegra clock output the board ignores.

**No `__overrides__`.** It is a Raspberry Pi firmware feature, inert under
UEFI/extlinux, so on Jetson whatever the file says is what boots. Likewise no
`gpio@6000d000` hog: that address is Tegra K1's GPIO controller and the node could
never have bound on Tegra234, and a correct hog would have claimed CAM1_PWDN
ahead of `reset-gpios`. A node kept deliberately broken is a trap, so it is gone.

### Still open on the DT

- **Supplies: settled.** All three rails are `regulator-fixed` always-on, which is
  accurate for this carrier. will127534's Raspberry Pi overlay points
  `vana-supply` at `cam1_reg` with `startup-delay-us = <300000>`, but that is a
  regulator the *RPi base DT* defines for the CAM connector's GPIO-switched 3V3,
  the delay covers the on-board LDOs rising after 3V3 is applied, and his
  `regulator-always-on` sits in a `__dormant__` (opt-in) fragment. None of it
  transfers: the p3768 camera FPC has no gate, 3V3 is present whenever the carrier
  is, and the one camera GPIO is CAM_EN, already spent on `reset-gpios`. `vana` is
  the only rail the host provides; `vdig` and `vddl` are generated on StarlightEye
  (U3, and U2 = LP5907MFX-1.8 feeding the whole 1.8 V domain including the
  TMP117/IMU/CH32V003), so they are the equivalent of will's `cam_dummy_reg` and
  take the FPC 3V3 as their input, not the carrier's `vdd_1v8_ao`, which does not
  reach the connector.
- **Mode properties** (`pix_clk_hz`, `line_length`, gain and framerate ranges,
  `embedded_metadata_height`) match what `imx585_populate_sensor_mode_props()`
  computes. The driver overwrites them anyway; they are kept accurate so the DT
  reads as documentation and stays live if the driver is swapped.
- **Single camera.** The overlay describes cam1 only: one VI channel, one NVCSI
  channel, one `tegra-camera-platform` module, `num_csi_lanes = <4>`.
- Still to fold in: the CEF168 `v4l2_lens` node — see "The CEF168 lens node"
  below. Blocked on one physical fact, not on analysis.

### The base DTB: decided — use the stock `-nv-super.dtb`

`UBOOT_EXTLINUX_FDT` was carried from tegrademo.conf pointing at
`tegra234-p3768-0000+p3767-0005-oe4t.dtb`, built by our own
`recipes-bsp/imx585-devicetree`. **That recipe is not needed, and the override
should go.** Read the DTS it builds:

```c
#include "tegra234-p3768-0000+p3767-0005-nv-super.dts"

/* adds compatible string for oe4t, for demonstration purposes */
/ {
	compatible = "oe4t,p3768-0000+p3767-0005+tegrademo", "nvidia,...-super", ...;
};
```

Six lines, and its own comment says what it is. The entire delta against NVIDIA's
stock DTB is one prepended cosmetic compatible string, inherited from
tegra-demo-distro. It carries no IMX585 content.

What it costs is real: seven `DT_INCLUDE` paths into
`nvidia-kernel-oot`'s staged `t23x/nv-public` tree that have to keep resolving
across BSP bumps, plus `DTC_PPFLAGS -DLINUX_VERSION=600`, which is stale for
6.8.12. Dropping it removes the "verify the `t23x/nv-public` include globs"
open item outright rather than answering it.

**The overlay does not need it. VERIFIED:** the overlay matches
`compatible = JETSON_COMPATIBLE_P3768`, which R39.2.1 defines in
`t23x/nv-public/include/platforms/dt-bindings/tegra234-p3767-0000-common.h` as a
ten-string list including `"nvidia,p3768-0000+p3767-0005-super"`. The stock
`-nv-super.dtb` carries that string, so matching succeeds against it directly.

So: delete the `UBOOT_EXTLINUX_FDT` override and let it fall through to
meta-tegra's default, `UBOOT_EXTLINUX_FDT ?=
"${@os.path.basename(d.getVar('KERNEL_DEVICETREE').split()[0])}"`
(`conf/machine/include/tegra-common.inc`), which for this machine resolves via
`orin-nano.inc`'s `KERNEL_DEVICETREE ?=
"tegra234-p3768-0000+p3767-0005-nv-super.dtb"`. Nothing in our layer overrides
`KERNEL_DEVICETREE`.

**Applied.** Three places referenced it and all three are now unwired:

- `conf/distro/imx585.conf` — the `UBOOT_EXTLINUX_FDT` override is gone, with the
  reasoning in a comment where the next reader will look. The
  `UBOOT_EXTLINUX_FDTOVERLAYS` / `OVERLAY_DTB_FILE` lines stay: the overlay is
  still stacked, just on the stock base.
- `recipes-bsp/uefi/l4t-launcher-extlinux.bbappend` — the
  `do_copy_dtb_overlays[depends]` ordering now names only
  `imx585-overlay:do_deploy`. The stock DTB is deployed by the kernel recipe.
- `recipes-core/packagegroups/packagegroup-imx585-camera.bb` — dropped from
  `RDEPENDS`, so it is no longer installed into the image.

The recipe and its DTS files stay in the tree, built by nothing, because the one
thing they still hold that we want is the CEF168 `v4l2_lens` node (below). Once
that is folded into the overlay, `recipes-bsp/imx585-devicetree/` can be deleted
outright.

### The CEF168 will not appear in /dev on Tegra — VERIFIED

Worth settling before any more DT work on it, because it changes what the node is
*for*.

The driver does everything an upstream lens driver should: `MEDIA_ENT_F_LENS`,
zero pads (a lens is not in the data path), `V4L2_SUBDEV_FL_HAS_DEVNODE |
V4L2_SUBDEV_FL_HAS_EVENTS`, then `v4l2_async_register_subdev()`. It still gets no
device node:

- `/dev/v4l-subdevN` is created only by `v4l2_device_register_subdev_nodes()`.
  Tegra VI *does* call it — `nvidia-oot/drivers/media/platform/tegra/camera/vi/graph.c:327`
  — but only for subdevs bound into VI's `v4l2_device`.
- VI discovers subdevs by walking **of_graph endpoints only**
  (`of_graph_get_next_endpoint` → `of_graph_get_remote_port_parent`). A lens node
  has no ports or endpoints, so it is never added to the notifier, never bound,
  and never gets a node. It waits in the async list forever.
- The i2c driver still probes and its controls still exist. There is simply no
  userspace path to them.

**`pcl_id = "v4l2_lens"` is not a kernel mechanism.** Nothing under
`nvidia-oot/drivers/` reads `pcl_id` or `sysfs-device-tree` — they are metadata
for Argus. We are going linear-raw and bypassing `nvargus-daemon`, so that
`drivernode1` entry would buy nothing.

**The V4L2 standard exists, in two parts.** Controls:
`V4L2_CID_FOCUS_ABSOLUTE`/`_RELATIVE`/`_AUTO` in the camera class, and cef168
implements the first two. Association: the `lens-focus` phandle on the *sensor*
node, present in 6.8.12 at `drivers/media/v4l2-core/v4l2-fwnode.c:1183`. A sensor
calling `v4l2_async_register_subdev_sensor()` has `lens-focus` parsed for it, the
lens bound into its notifier, and the lens then appears with a devnode as a LENS
entity beside the sensor — this is what imx335, imx412 and hi846 do.

**tegracam does not do this.** `tegracam_v4l2.c:269` is a plain
`v4l2_async_register_subdev()`, and `lens-focus` appears nowhere in nvidia-oot. So
on Tegra we get the standard controls with none of the standard plumbing.

Two ways forward:

1. **Userspace over `/dev/i2c-N`, no kernel driver** — the bring-up path. The AF
   loop is userspace regardless (contrast detect on raw frames; the IMX585 has no
   PDAF and we are not using the ISP). The protocol is a 4-byte write
   `{cmd, val_lo, val_hi, crc8}`, CRC8 polynomial 168, with `INP_SET_FOCUS 0x80`,
   `INP_SET_FOCUS_P/N 0x81/0x82`, aperture `0x7A/0x7B/0x7C`, `INP_CALIBRATE 0x22`.
   This needs **no** `cef168` DT node at all — no compatible, no `vcc-supply` —
   only the chosen i2c bus enabled so `/dev/i2c-N` exists, which simplifies the
   placement question above to "which bus to enable". Note the two approaches do
   not mix: once a kernel driver binds 0x0d, userspace needs `I2C_SLAVE_FORCE`.
2. **Wire up the standard path** — add `lens-focus = <&cef168>` to the sensor node
   and get imx585 to register via `v4l2_async_register_subdev_sensor()`. This is
   architecturally correct and gives `v4l2-ctl --set-ctrl focus_absolute`, but it
   means taking registration ownership away from tegracam and satisfying VI's
   notifier model. Later cleanup, not bring-up.

### The CEF168 lens node — resolved

**Bus: the 40-pin header (gen1_i2c).** Of the archive's three mutually exclusive
placements this is the one the hardware uses, and also the better choice:
cam_i2cmux is a GPIO-switched i2c-mux, so a lens on an FPC leg is addressable only
while that leg is selected and AF traffic shares the segment with sensor writes at
0x1a -- precisely the contention a continuous contrast-detect loop would hit while
streaming. Implemented in the overlay: i2c@3160000 enabled (it is
status = "disabled" in tegra234.dtsi and no p3768/p3767 platform .dtsi enables it),
a pinmux group on pinmux@2430000 -- *not* &pinctrl, which has no label in R39.2.1
-- and cef168@d with vcc-supply, which cef168_probe() requires rather than
prefers.

**Access: the standard V4L2 path, plumbed by hand.** lens-focus = <&cef168> on the
sensor node plus a sensor-owned sub-notifier in the driver, since tegracam
registers with a plain v4l2_async_register_subdev() and so cannot use
v4l2_async_register_subdev_sensor(). Built from the exported pieces instead. This
is what gets the lens bound into VI's v4l2_device and given a device node, and it
means `v4l2-ctl --set-ctrl focus_absolute` works rather than a bespoke I2C path.

**The coupling this creates, which is worth knowing before debugging anything
else:** once lens-focus points at an enabled node, VI's root notifier cannot
complete until that subdev binds, because v4l2_async_nf_can_complete() recurses
into sub-notifiers. A CEF168 whose *driver* never loads therefore means no
/dev/video0 at all -- a symptom that looks nothing like its cause. The driver
guards the two cases it can detect (phandle absent, target disabled) and treats
them as "no focus control" rather than an error; it cannot guard against
kernel-module-cef168 being absent from the image. Mitigating fact:
cef168_probe() performs no I2C at all -- regulator, subdev, controls, pads,
async-register -- so a disconnected or unresponsive controller still binds and the
camera still comes up, with only focus writes failing.

Not used: NVIDIA's drivernode1/pcl_id = "v4l2_lens". Nothing under
nvidia-oot/drivers reads pcl_id or sysfs-device-tree; they are Argus metadata and
this distro bypasses Argus for linear raw.

### On-board I2C peripherals — done

Added to the `i2c@1` leg of `imx585-overlay.dts`, verified to compile with `dtc -@`:

| Addr | Device | State |
|---|---|---|
| `0x1a` | IMX585 (U4) | existing |
| `0x34` | CH32V003 IR-cut filter switch (U7) + DRV8837C (U8) | **no node by design** — no kernel driver. One-byte write from userspace on the leg's `/dev/i2c-N`. Address recorded so it is not reused. |
| `0x48` | TMP117 (U6) | `ti,tmp117`, enabled. `vcc-supply` is *required* by the binding; pointed at `imx585_vddl` (+1V8). |
| `0x68` | ICM-42688-P (U11) | `invensense,icm42688`, **`status = "disabled"`** |

The IMU is disabled on purpose and cannot simply be switched on.
`inv_icm42600_core_probe()` in 6.8.12 starts with `irq_get_irq_data(irq)` and
fails with `"could not find IRQ %d"` when the I2C client has no interrupt — no
polled mode.
On StarlightEye V2.0, INT1/INT2 reach only TP10/TP13 through R21/R22, both **DNP**,
so no interrupt leaves the board, and the FPC's one camera GPIO is already `CAM_EN`.
Enabling the node as it stands yields a probe failure every boot and no IIO device.

`CONFIG_TMP117` (IIO, not hwmon), `CONFIG_INV_ICM42600{,_I2C}` and `CONFIG_IIO`
added to `recipes-kernel/linux/files/imx585.cfg`.

### Headers and symbols

`files/Makefile` takes `NVIDIA_OOT_INCDIR` and `KBUILD_EXTRA_SYMBOLS` from the
recipe. The archived tree instead vendored **71 copies of NVIDIA's
`include/media/` headers** (that is the bulk of `0022`'s 12,263 insertions),
which compiles against a frozen R36.4 ABI while loading against R39.2.1 modules
— wrong silently rather than loudly.

Both staging paths are now **VERIFIED** against meta-tegra wrynose `322bc23`.
`nvidia-kernel-oot.inc`'s `do_install` does:

```bitbake
install -d ${D}${includedir}/${BPN}
find ${B} -name Module.symvers -type f | xargs sed -e's:${B}/::g' >${D}${includedir}/${BPN}/Module.symvers
cp -R ${S}/nvidia-oot/include/* ${D}/${includedir}/${BPN}
```

`BPN` is `nvidia-kernel-oot`, so `<media/camera_common.h>` resolves under
`${STAGING_INCDIR}/nvidia-kernel-oot/media/` and the symbol table sits beside it.
Both are in `FILES:${PN}-dev`, which `DEPENDS += "nvidia-kernel-oot"` stages.

**That was necessary but not sufficient, and the first build proved it.** The
staged header set is *not self-contained*: `camera_common.h` line 10 is

```c
#include <nvidia/conftest.h>
```

and `nvidia/conftest.h` is **generated** during nvidia-kernel-oot's own build into
`${B}/out/nvidia-conftest/nvidia/`, which is not under `${S}/nvidia-oot/include`
and so is never copied by the `cp -R` above. The first `do_compile` therefore
failed with

```
camera_common.h:10:10: fatal error: nvidia/conftest.h: No such file or directory
```

after `do_configure` passed — the `media/` guard was satisfied; the gap is one
level deeper. Fixed by `recipes-kernel/nvidia-kernel-oot/nvidia-kernel-oot_%.bbappend`,
which installs the generated tree into `${includedir}/${BPN}/nvidia/` so the
sysroot is usable by any out-of-tree consumer, with a `bbfatal` if the conftest
output location ever moves.

These headers must **not** be stubbed. `conftest.sh` probes this specific kernel
for ~200 API shapes (`v4l2_subdev_pad_ops_struct_has_get_set_frame_interval`,
`i2c_driver_struct_probe_without_i2c_device_id_arg`, `media_entity_remote_pad`
and so on) and those results are what let one source file build across BSP
kernels. A hand-written stub would assert the wrong kernel API and miscompile
quietly — the same failure mode as the 71 vendored headers, reintroduced.

### KBUILD_EXTRA_SYMBOLS cannot be set by the recipe

The third and last build failure, and the one worth remembering. With the headers
fixed the module *compiled*, then modpost rejected it with all eight tegracam
symbols undefined — `tegracam_device_register`, `tegracam_v4l2subdev_register`,
`sensor_common_parse_num_modes` and so on — while the staged `Module.symvers`
plainly contained them. The run script showed why: `KBUILD_EXTRA_SYMBOLS=""`.

`module.bbclass` builds the value itself and overwrites whatever the recipe set:

```python
python __anonymous () {
    depends = d.getVar('DEPENDS')
    extra_symbols = []
    for dep in depends.split():
        if dep.startswith("kernel-module-"):
            extra_symbols.append("${STAGING_INCDIR}/" + dep + "/Module.symvers")
    d.setVar('KBUILD_EXTRA_SYMBOLS', " ".join(extra_symbols))
}
```

The `setVar` runs at parse finalisation, so **the only way to get an entry in is
to name a `DEPENDS` beginning with `kernel-module-`.** Setting it directly loses to
the `setVar`; setting it through `EXTRA_OEMAKE` loses to the class passing
`KBUILD_EXTRA_SYMBOLS` explicitly later on the make command line. Either way the
module builds cleanly and can never load.

The fix is to use the convention meta-tegra already provides for it:

```bitbake
DEPENDS += "virtual/kernel kernel-module-nvidia-kernel-oot"
```

`nvidia-kernel-oot.inc` has `PROVIDES += "kernel-module-${BPN}"` so this resolves
to the same recipe, and its `do_install` creates
`ln -s ${BPN} ${D}${includedir}/kernel-module-${BPN}` **specifically** so that the
class's generated path
`${STAGING_INCDIR}/kernel-module-nvidia-kernel-oot/Module.symvers` resolves. That
symlink is not incidental; it exists for exactly this.

**Result — the module now builds and links.** `imx585.ko`, aarch64,
`vermagic=6.8.12-l4t-r39.2.1-1021.21`, packaged as `kernel-module-imx585`, and its
own metadata is the proof that all three fixes landed:

```
depends=tegra-camera,v4l2-cci,v4l2-fwnode
alias=of:N*T*Csony,imx585
```

`tegra-camera` is the tegracam symbols resolving; `v4l2-cci` is the CCI accessors
resolving, which only happens with `V4L2_CCI_I2C` genuinely enabled; and the
`sony,imx585` alias matches the overlay's sensor nodes.

### The CCI accessors were never actually enabled

Separate from the above, and the next failure the build would have hit.
`imx585.cfg` carried

```
CONFIG_V4L2_CCI=y
CONFIG_V4L2_CCI_I2C=y
```

and **both lines were silently discarded.** In
`drivers/media/v4l2-core/Kconfig` the two symbols are promptless `tristate`s,
settable only through another driver's `select`. The evidence is in the built
config: it contains no `V4L2_CCI` line at all — not even `is not set` — while the
IIO symbols from the same fragment took effect, and nothing warned. `cci_write()`
lives in `v4l2-cci.c`, so the module would have compiled and then failed at
modpost/load with every `cci_*` undefined.

Fixed by selecting it the only way available, `CONFIG_VIDEO_IMX219=m` — imx219 is
the Jetson stock CSI camera, so it doubles as the known-good sensor for proving
the VI/NVCSI path independently of the IMX585. Note `VIDEO_IMX415`, already in
the fragment, does **not** select `V4L2_CCI_I2C` and cannot serve this role.

The general lesson for this tree: a `.cfg` line is a *request*, not a guarantee.
Verify symbols in the built
`linux-*/*/linux-*-build/.config`, and treat "absent entirely" as the signature
of a promptless symbol rather than a typo.

Worth knowing for later: `nvidia-kernel-oot.inc` also exposes `EXTRA_PATCHES` and
`TEGRA_OOT_EXTRA_CAMERA_DRIVERS`, which is the supported hook for §8.4 step 1a —
building the sensor *inside* the OOT tree next to `nv_imx477.c`. We took 1b (a
separate recipe) for `devtool` iteration speed; 1a stays available if ABI trouble
appears, and it is a first-class mechanism rather than a workaround.

## Likely to need changes

- **Device tree names: settled** — see "The base DTB: decided" above. We boot the
  stock `-nv-super.dtb`; `DTC_PPFLAGS -DLINUX_VERSION=600` went with
  `imx585-devicetree`.
- **DT include paths: settled.** The root is (`cp -R ${S}/hardware/nvidia/ ${D}/usr/src/device-tree` plus
  `SYSROOT_DIRS += "/usr/src/device-tree"`). The subtree layout is now VERIFIED
  against R39.2.1's staged tree, and it is split:

  ```
  tegra/nv-public/include/{kernel,nvidia-oot}
  t23x/nv-public/include/{nvidia-oot,platforms}
  ```

  `include/kernel` is under **tegra/**, not t23x/. `imx585-overlay.bb` asked for
  `t23x/nv-public/include/kernel` (nonexistent) and listed neither tegra/ include
  dir. This was silent, not fatal: `expand_includes()` only appends directories
  that exist, so the bogus entry vanished and
  `<dt-bindings/clock/tegra234-clock.h>` — which the overlay does include — came
  from `KERNEL_INCLUDE` instead. That file is byte-identical to NVIDIA's copy in
  6.8.12, so the overlay compiled correctly by accident. Corrected in the recipe
  so NVIDIA's headers win deliberately.
- **`imx585.cfg` / the CCI accessors: settled.** They are required, because this
  driver keeps the CCI register tables, and they cannot be set directly — both
  symbols are promptless and only a `select` turns them on. Pulled in via
  `CONFIG_VIDEO_IMX219=m`, and **VERIFIED** in the built config as
  `CONFIG_V4L2_CCI=m` / `CONFIG_V4L2_CCI_I2C=m`. See "The CCI accessors were never
  actually enabled" above.
- **CEF168 — partly verified.** `recipes-kernel/cef168/` pins pinefeat/cef168 at
  `3abcaeef`. The source has been read: it is a `v4l2_subdev` with
  `V4L2_CID_FOCUS_ABSOLUTE`/`_RELATIVE`, `of_match_table` of `"pinefeat,cef168"`,
  and a `vcc` supply — so the DT nodes' compatible and supply are right and the
  controls are the ones an AF loop needs. Not yet *compiled* against 6.8, and the
  exposure remains V4L2 control/subdev API churn. Its DT placement is a separate
  open question — see "The CEF168 lens node" above.

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
