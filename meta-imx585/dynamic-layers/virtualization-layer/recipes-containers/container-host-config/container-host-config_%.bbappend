# Two edits to meta-virtualization's storage.conf.
#
# 1. driver: the default is "vfs", which copies every layer of every image
#    instead of stacking them -- correct as a lowest-common-denominator default,
#    wasteful on a real filesystem. overlay is right here: rootful podman on 6.8
#    with CONFIG_OVERLAY_FS built in (recipes-kernel/linux/files/containers.cfg)
#    uses the kernel's overlayfs directly, no fuse-overlayfs in the path.
#    fuse-overlayfs is only needed for rootless on kernels without unprivileged
#    overlayfs, and the podman recipe's `rootless` PACKAGECONFIG is off.
#
# 2. graphroot: it must leave /var/lib. On a read-only rootfs oe-core's
#    volatile-binds bind-mounts a tmpfs over /var/lib -- its service carries
#    ConditionPathIsReadWrite=!/var/lib, so it activates exactly when the rootfs
#    is read-only -- and an image store in RAM would be lost on every reboot and
#    would exhaust an 8 GB module long before that. It goes on the persistent
#    data partition instead. runroot stays under /run, which is where it belongs.
#
# Seds rather than a replacement storage.conf, so upstream changes to the rest of
# the file still land.
IMX585_DATA_MOUNT ??= "/data"
DATA_MOUNT ?= "${IMX585_DATA_MOUNT}"
CONTAINER_GRAPHROOT ?= "${DATA_MOUNT}/containers/storage"

do_install:append() {
    sed -i -e 's|^\(\s*\)driver\s*=\s*"vfs"|\1driver = "overlay"|' \
           -e 's|^\(\s*\)graphroot\s*=\s*"[^"]*"|\1graphroot = "${CONTAINER_GRAPHROOT}"|' \
        ${D}${sysconfdir}/containers/storage.conf

    grep -q 'driver = "overlay"' ${D}${sysconfdir}/containers/storage.conf || \
        bbfatal "storage.conf: could not switch the storage driver to overlay"
    grep -q 'graphroot = "${CONTAINER_GRAPHROOT}"' ${D}${sysconfdir}/containers/storage.conf || \
        bbfatal "storage.conf: could not relocate graphroot to ${CONTAINER_GRAPHROOT}"
}
