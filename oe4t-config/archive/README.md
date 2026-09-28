# Archive — everything that existed only in the local Yocto trees

Created 2026-09-28, so `/home/nathan/tegra-demo-distro` (266G) can be deleted.

Everything here was verified to exist **nowhere else**: not on `origin`, not on
the `finchies` fork, not in any stash. The layer snapshots duplicate content
that is on the fork; the bundles and patches do not.

## bundles/ — git history that was never pushed

Self-contained (`git bundle verify` reports no prerequisites), so each one
restores with `git clone <file>.bundle <dir>` and no network.

| Bundle | Contains |
|---|---|
| `imx585-v4l2-driver-devtool.bundle` | all refs of the devtool clone of `NathanHowell/imx585-v4l2-driver`. Its `devtool` branch is **25 commits** past `origin/jetson-overlay-6.12.y`, ending at `13947db imx585: seed sensor_mode_properties for tegracam`. This is the tegracam-conversion work and the most valuable thing in this directory. |
| `imx585-overlay-devtool.bundle` | the second devtool clone of the same repo; `devtool` is 1 commit ahead (`2002776 imx585-overlay: Describe clocks and supplies`). The `bad-bad-bad-build` tree held the same patch as `d7b2c2c`; content verified identical, so only one copy is kept. |
| `tegra-demo-distro.bundle` | branches `rework` (7 commits past `finchies/rework`), `fixes` (2 ahead), `l4tlauncher-hacks` (local only), `master`. |

`*.log` files alongside list the unpushed commits.

Restore check that was run:

```
git clone --bare imx585-v4l2-driver-devtool.bundle /tmp/restore
git -C /tmp/restore log --oneline -1 devtool   # 13947db
git -C /tmp/restore rev-list --count devtool   # 113
```

## patches/ — the same commits as readable text

`git format-patch` output, which is the more useful form given the work has to
be reapplied onto a 6.8 tegracam base rather than restored verbatim.

- `imx585-v4l2-driver/` — 25 patches. `0001`–`0002` are the two already
  captured in `../meta-tegrademo/recipes-kernel/imx585/`; `0003`–`0015` are
  bpftrace/debug scaffolding churn; `0016`–`0025` are the substantive tegracam
  work, of which `0022 tegracam: wire imx585 driver into tegra stack` is the core.
- `imx585-v4l2-driver-squashed.diff` — all 25 as one diff, for reading.
- `imx585-overlay/` — 1 patch.
- `tegra-demo-distro/` — 7 patches against `finchies/rework`.

## uncommitted/

- `imx585-legacy.c` — untracked in the devtool workspace, so in no commit and
  no bundle.
- `tegra-demo-distro-worktree.diff` — the one modified tracked file
  (`orin-console-image.bb`).

## meta-tegrademo/ and meta-tegra-support/

Full copies of both layers (`.git` removed). `../meta-tegrademo/` higher up in
this snapshot holds only the IMX585/CEF168 subset; these are everything,
including the swupdate dynamic layer, the demo images and the CI recipes.
`meta-tegra-support` is here because `tegrademo.conf` does
`INHERIT += "tegra-support-sanity"`, which lives in it — the new
`meta-imx585/conf/distro/imx585.conf` drops that dependency.

## Deliberately not archived

`build/downloads`, `build/sstate-cache`, `build/tmp`, the
`build/bad-bad-bad-build` tree, and `repos/` (five upstream submodules, clean
at the SHAs recorded in `../README.md`). All regenerable or upstream.
