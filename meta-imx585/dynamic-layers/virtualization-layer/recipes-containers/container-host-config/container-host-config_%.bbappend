# meta-virtualization ships storage.conf with driver = "vfs", which copies every
# layer of every image instead of stacking them -- correct as a lowest-common-
# denominator default, wasteful on a real filesystem.
#
# overlay is the right driver here: rootful podman on 6.8 with CONFIG_OVERLAY_FS
# built in (see recipes-kernel/linux/files/containers.cfg) uses the kernel's
# overlayfs directly, with no fuse-overlayfs in the path. fuse-overlayfs is only
# needed for rootless on kernels without unprivileged overlayfs, and the podman
# recipe's `rootless` PACKAGECONFIG -- which is what would pull it in -- is off.
#
# Done as a sed rather than a replacement storage.conf so that upstream changes to
# the rest of the file still land.
do_install:append() {
    sed -i -e 's|^\(\s*\)driver\s*=\s*"vfs"|\1driver = "overlay"|' \
        ${D}${sysconfdir}/containers/storage.conf
    grep -q 'driver = "overlay"' ${D}${sysconfdir}/containers/storage.conf || \
        bbfatal "storage.conf: could not switch the storage driver to overlay"
}
