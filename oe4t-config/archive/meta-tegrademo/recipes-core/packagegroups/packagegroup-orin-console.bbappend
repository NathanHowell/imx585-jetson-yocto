FILESEXTRAPATHS:prepend := "${THISDIR}/files:"
SRC_URI += "file://fdt.conf"

ROOTFS_POSTPROCESS_COMMAND += "set_primary_fdt_entry;"

set_primary_fdt_entry() {
    CONF="${IMAGE_ROOTFS}/boot/extlinux/extlinux.conf"
    [ -f "$CONF" ] || {
        bbwarn "extlinux.conf not found; skipping FDT injection"
        return
    }

    if ! grep -q "^FDT" "$CONF"; then
        bbnote "Inserting FDT entry into extlinux.conf"
        # Append FDT line inside primary stanza after INITRD
        awk 'BEGIN {inserted=0} {
            print
            if (!inserted && /^LABEL primary/) label=1
            if (label && /^INITRD/) {
                print "        FDT /boot/dtb/kernel_tegra234-p3767-0000-p3768-0000.dtb"
                inserted=1
                label=0
            }
        } END { if (!inserted) bbwarn("FDT line not inserted; primary stanza missing INITRD") }' "$CONF" > "$CONF.tmp"
        mv "$CONF.tmp" "$CONF"
    fi
}
