# oe-core packages fstrim.timer but ships it disabled
# (SYSTEMD_AUTO_ENABLE:util-linux-fstrim = "disable"). On an NVMe rootfs that
# matters: without periodic TRIM the drive's FTL never learns which blocks the
# filesystem has freed, so sustained write performance degrades and wear
# levelling has less spare area to work with. The timer runs weekly.
#
# Periodic TRIM rather than mounting with `discard`: continuous discard issues a
# DEALLOCATE on every delete, which stalls the submission queue, and ext4's
# async discard support does not remove the cost entirely. Weekly batched trim is
# what distributions settled on.
SYSTEMD_AUTO_ENABLE:util-linux-fstrim = "enable"
