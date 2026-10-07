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
  file, so the dropped mono/RAW16 support, the dead cam0 plumbing and the gain
  unit handling can be fixed properly rather than minimally. Worth doing *after*
  the sensor streams, not before — the ordered fixes below are the path to a first
  frame and shouldn't be mixed with refactoring.

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
   post-MODE_SELECT sleep. An earlier draft of this file guessed "after the
   sleep"; the baseline says otherwise. The conversion dropped only the XMSTA
   call, keeping the MODE_SELECT write that follows it. We have no external-sync
   plumbing, so the guard collapses to an unconditional write.

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

Neither has been exercised on hardware. They remove two reasons the sensor could
not stream; whether it does is still unknown.

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

**Still untested on hardware.** Seven fixes in, the remaining known-unknowns are
the supplies, the `cil_settletime = 0` assumption, and whether ClearHDR/mono are
wanted back.

Lesser: ~~`imx585_set_group_hold()` is a stub returning 0~~ — **fixed**, see
below. `imx585_board_setup()` reads the model ID and only `dev_dbg`s it, never
compares — F4 is unimplemented. `MODULE_AUTHOR("NVIDIA Corporation")` is copied
boilerplate. ~~`imx585_populate_sensor_mode_props()`'s loop body is dedented out
of its own `for`~~ — that function was rewritten, so the indentation is now sane.

### The hardware is StarlightEye, not a Kurokesu module

This correction invalidates an earlier version of this section, which said to take
Kurokesu's measured DT values. The camera is
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
| cam0 `imx585_a` | `status = "okay"` | `status = "disabled"` — 2-lane connector |
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

**`__overrides__` could not have fixed the cam0/cam1 inversion.** It is a Raspberry
Pi firmware feature, inert under UEFI/extlinux, so on Jetson whatever the file
defaults to is what boots — which is why the `status` values had to change in the
file itself.

### Still open on the DT

- **Supplies are loosely modelled.** Upstream points `vana-supply` at the
  host-gated 3.3 V with `startup-delay-us = <300000>` and `vdig`/`vddl` at a
  **dummy** regulator, because those rails are on-board (U3/U2). The three
  invented `regulator-fixed` always-on nodes here are the right shape but do not
  describe that split. They work; they are not accurate.
- **`pix_clk_hz = "600000000"` and `line_length = "11200"`** remain placeholders.
  Low priority: `imx585_populate_sensor_mode_props()` overwrites both from its own
  computation, so the DT values are inert for this driver — but see ordered fix 4,
  because that computation is itself wrong (`bpp = 10`).
- **`gain_factor`/`max_gain_val` in the DT** still describe a linear Q4 scale
  (ordered fix 6), and `embedded_metadata_height = "2"` still contradicts the
  driver's register table (ordered fix 5) — which that fix resolves in favour of
  the register: it must become `"0"`.
- **cam0 plumbing is now dead weight.** Its VI channel, NVCSI channel and
  `tegra-camera-platform` `module0` entry still exist, pointing at a disabled
  sensor and a `sysfs-device-tree` path that will not appear. Harmless for V4L2
  capture; a single-camera overlay would be cleaner, and is the eventual shape.
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

### The CEF168 lens node — blocked on how it is wired

The driver side is **VERIFIED and in good shape.** `recipes-kernel/cef168`
fetches pinefeat/cef168 at a pinned SRCREV, and the source is a real
`v4l2_subdev` exposing `V4L2_CID_FOCUS_ABSOLUTE` (0..S16_MAX) and
`V4L2_CID_FOCUS_RELATIVE` — precisely the two controls a contrast-detect AF loop
needs, since the IMX585 has no PDAF. Its `of_match_table` is
`{ .compatible = "pinefeat,cef168" }` and it requests a `vcc` supply, so the
archived nodes' `compatible` and `vcc-supply` are both right.

What is *not* settled is which bus it hangs off, and the archive cannot settle it
because it contains three mutually exclusive answers:

| Archived DTS | Placement |
|---|---|
| `…-oe4t-imx585-cef168.dts` | `cam_i2cmux/i2c@0` (cam0 FPC leg) |
| `…-oe4t-imx585-cef168-cam1.dts` | `cam_i2cmux/i2c@1` (cam1 FPC leg) |
| `…-oe4t-cef168-header-i2c.dts` | `gen1_i2c`, 40-pin header, and *disables* the FPC node |

Three variants exist because the placement was being explored, not decided. This
is a wiring fact about hardware, and inventing one is how the placeholders this
file catalogues got here, so it is not being guessed.

**The technical argument favours the 40-pin header.** `cam_i2cmux` is a
GPIO-switched i2c-mux, so a lens controller on an FPC leg is only addressable
while the mux selects that leg, and AF traffic then contends with sensor register
writes on the same segment as the sensor at 0x1a. A continuous AF loop running
during capture is exactly the case that makes that contention matter. On
`gen1_i2c` the focus loop is independent of the capture path. The header variant
is also the only one documenting physical wiring (header pins 1/3/5/6 with wire
colours), which suggests it is the one that was actually built.

**But the archived header overlay cannot be used as-is on R39.2.1 — VERIFIED:**

- It opens `&pinctrl { … }`, and **no `pinctrl:` label exists anywhere in
  R39.2.1's DT sources.** The label is `pinmux` (`pinmux@2430000` in
  `t23x/nv-public/tegra234.dtsi`). That reference cannot resolve.
- `gen1_i2c` is `status = "disabled"` in `tegra234.dtsi`, and no p3768/p3767
  platform `.dtsi` enables it, so the overlay must enable the controller itself.
  The archived one does; worth stating because it is not a no-op.
- Our overlay deliberately uses `target-path = "/"` with name-nesting rather than
  label references (`&gen1_i2c`), because label-based overlays need `__symbols__`
  in the base DTB and the base here is NVIDIA's prebuilt `-nv-super.dtb`. Folding
  this in means re-expressing it as `bus@0 { i2c@3160000 { … } }` plus
  `bus@0 { pinmux@2430000 { … } }`, not copying the `&`-form.

So when the wiring is known, folding in is: the `cef168@d` node (`reg = <0x0d>`,
`compatible = "pinefeat,cef168"`, `vcc-supply`), on the chosen bus in
name-nested form; and a `drivernode1 { pcl_id = "v4l2_lens"; sysfs-device-tree =
… }` inside `tegra-camera-platform`'s `module1` (our enabled sensor is
`imx585_c` on `i2c@1`, so it is `module1`, not `module0`), with the
`sysfs-device-tree` path matching the node's real path exactly.

### On-board I2C peripherals — done

Added to the `i2c@1` leg of `imx585-overlay.dts`, verified to compile with `dtc -@`:

| Addr | Device | State |
|---|---|---|
| `0x1a` | IMX585 (U4) | existing |
| `0x34` | CH32V003 IR-cut filter switch (U7) + DRV8837C (U8) | **no node by design** — no kernel driver. One-byte write from userspace on the leg's `/dev/i2c-N`. Address recorded so it is not reused. |
| `0x48` | TMP117 (U6) | `ti,tmp117`, enabled. `vcc-supply` is *required* by the binding; pointed at `imx585_vddl` (+1V8). |
| `0x68` | ICM-42688-P (U11) | `invensense,icm42688`, **`status = "disabled"`** |

The IMU is disabled on purpose and cannot simply be switched on.
`inv_icm42600_core_probe()` ends its IRQ lookup with
`return dev_err_probe(dev, irq, "error missing INT1 interrupt")` — no polled mode.
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
to name a `DEPENDS` beginning with `kernel-module-`.** Two earlier attempts here
both failed silently and for different reasons: an `EXTRA_OEMAKE` entry (the
class passes `KBUILD_EXTRA_SYMBOLS` explicitly later on the make command line,
winning), then a plain bitbake variable (this `setVar` wins). Each produced a
module that built cleanly and could never load.

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

- ~~**Device tree names.**~~ **RESOLVED** — see "The base DTB: decided" above.
  The `-oe4t.dtb` is ours, not NVIDIA's, and its entire content is one cosmetic
  compatible string, so the override goes and we boot the stock
  `-nv-super.dtb`. That also retires the `DTC_PPFLAGS -DLINUX_VERSION=600`
  question, which only ever applied to `imx585-devicetree_1.0.bb`.
- **DT include paths — RESOLVED, and one entry was wrong.** The root was already
  confirmed (`cp -R ${S}/hardware/nvidia/ ${D}/usr/src/device-tree` plus
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
- ~~**`imx585.cfg`.** `CONFIG_V4L2_CCI{,_I2C}` … if you land on tegracam, drop them.~~
  **Wrong twice over.** They are required (the conversion kept the CCI register
  tables), *and* the way they were being set did nothing. See "The CCI accessors
  were never actually enabled" above: both symbols are promptless and only a
  `select` turns them on. Now pulled in via `CONFIG_VIDEO_IMX219=m`, and
  **VERIFIED** in the rebuilt config as `CONFIG_V4L2_CCI=m` /
  `CONFIG_V4L2_CCI_I2C=m`.
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
