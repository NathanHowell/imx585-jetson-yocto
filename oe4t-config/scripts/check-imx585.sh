#!/bin/sh

set -e

log() {
    printf '%s\n' "$*"
}

if ! command -v media-ctl >/dev/null 2>&1; then
    log "media-ctl not found; install v4l-utils"
    exit 1
fi

log "== lsmod | grep imx585 =="
lsmod | grep imx585 || log "imx585 module not loaded"

log "== dmesg | grep -i imx585 =="
dmesg | grep -i imx585 || true

log "== media-ctl -p =="
media-ctl -p

if command -v fdtget >/dev/null 2>&1; then
    log "== fdtget /sys/firmware/fdt imx585 nodes =="
    fdtget /sys/firmware/fdt /bus@0/cam_i2cmux/i2c@0/imx585_a@1a status data-lanes || true
    fdtget /sys/firmware/fdt /bus@0/cam_i2cmux/i2c@1/imx585_c@1a status data-lanes || true
else
    log "fdtget not available"
fi

log "== v4l2-ctl --list-devices (strace) =="
strace -eopenat,ioctl v4l2-ctl --list-devices
