# Build and flash recipes for the IMX585 Jetson bring-up tree. `just` lists them.
#
# Paths follow kas/imx585.yml (TMPDIR = /build/yocto/tmp) and kas/build.sh.

set shell := ["bash", "-euo", "pipefail", "-c"]

kas_yml   := "kas/imx585.yml"
machine   := "jetson-orin-nano-devkit-nvme"
image     := "imx585-console-image"
deploy    := "/build/yocto/tmp/deploy/images/" + machine
bundle    := deploy + "/" + image + "-" + machine + ".rootfs.tegraflash-tar.zst"
flash_dir := "/build/yocto/flash"
console   := "/dev/ttyUSB0"

default:
    @just --list

# bitbake the image, capped at 4 threads / -j 4 (kas/lowmem.conf)
build target=image:
    kas/build.sh shell {{kas_yml}} -c "bitbake -R {{justfile_directory()}}/kas/lowmem.conf {{target}}"

# bitbake the image with the full thread count from kas/imx585.yml
build-fast target=image:
    kas/build.sh shell {{kas_yml}} -c "bitbake {{target}}"

# bitbake imx585-minimal-image (read-only rootfs, dropbear), 4 threads
build-minimal:
    kas/build.sh shell {{kas_yml}}:kas/include/minimal.yml -c "bitbake -R {{justfile_directory()}}/kas/lowmem.conf imx585-minimal-image"

# bitbake prompt in the kas environment
shell:
    kas/build.sh shell {{kas_yml}}

# unpack the newest tegraflash bundle into the flash directory
extract:
    test -e "{{bundle}}"
    mkdir -p "{{flash_dir}}"
    rm -rf "{{flash_dir}}"/* "{{flash_dir}}"/.[!.]*
    zstd -dc "$(readlink -f "{{bundle}}")" | tar -C "{{flash_dir}}" -xS
    ls -l "{{flash_dir}}/.env.initrd-flash"

# confirm a Jetson is in USB recovery mode
recovery:
    lsusb -d 0955: || { echo "no Jetson in recovery mode: short FC REC to GND and reset" >&2; exit 1; }

# flash everything: QSPI boot firmware and the NVMe
flash: recovery
    cd "{{flash_dir}}" && sudo ./initrd-flash

# flash only the NVMe (rootfs, kernel, ESP); QSPI untouched
flash-rootfs: recovery
    cd "{{flash_dir}}" && sudo ./initrd-flash --external-only

# flash only the QSPI boot firmware (UEFI, OP-TEE, MB1/MB2)
flash-qspi: recovery
    cd "{{flash_dir}}" && sudo ./initrd-flash --qspi-only

# flash everything after wiping the NVMe, dropping the data partition too
flash-erase: recovery
    cd "{{flash_dir}}" && sudo ./initrd-flash --erase-nvme

# extract the newest bundle and flash everything
reflash: extract flash

# extract the newest bundle and flash only the NVMe
reflash-rootfs: extract flash-rootfs

# serial console on the devkit's debug UART (Ctrl-t q to quit)
console:
    tio -b 115200 {{console}}

# ssh to the board over mDNS
ssh user="orin":
    ssh {{user}}@{{machine}}.local

# host udev rule so initrd-flash can talk to a recovery-mode Jetson without sudo
udev-rule:
    printf 'SUBSYSTEM=="usb", ATTR{idVendor}=="0955", MODE="0664", GROUP="plugdev"\n' | sudo tee /etc/udev/rules.d/99-nvidia-tegra-recovery.rules
    sudo udevadm control --reload && sudo udevadm trigger
