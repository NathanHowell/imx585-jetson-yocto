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

## Usage

```sh
pipx install kas          # not currently installed on this machine

kas build kas/imx585.yml                                   # default
kas shell kas/imx585.yml                                   # bitbake prompt
kas build kas/imx585.yml:kas/include/containers.yml        # + docker
kas build kas/imx585.yml:kas/include/kernel-linux-yocto.yml  # mainline 6.18
```

Set `DL_DIR` and `SSTATE_DIR` in `imx585.yml` before the first build. The old
tree reached 266G because they defaulted into it.

## Files

| | |
|---|---|
| `imx585.yml` | the build: distro, machine, target, `local.conf` |
| `include/base.yml` | layer pins shared by all configs |
| `include/kernel-linux-yocto.yml` | opt in to mainline linux-yocto 6.18 |
| `include/containers.yml` | opt in to docker + NVIDIA container toolkit |

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
