SUMMARY = "Firmware TPM configuration for a read-only rootfs"
DESCRIPTION = "Puts OP-TEE secure storage -- which is where the fTPM keeps its NV \
indexes, persistent handles and sealed blobs -- on the persistent data partition, \
and orders tee-supplicant after that partition is mounted."

LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

inherit systemd features_check

REQUIRED_DISTRO_FEATURES = "systemd"

SRC_URI = " \
    file://10-data-partition.conf \
    file://imx585-tee-storage.conf \
"

S = "${UNPACKDIR}"

IMX585_DATA_MOUNT ??= "/data"
DATA_MOUNT ?= "${IMX585_DATA_MOUNT}"
# Must match OPTEE_FS_PARENT_PATH in the distro conf; optee-client is built with
# it as CFG_TEE_FS_PARENT_PATH, so the two cannot be allowed to drift.
TEE_FS_PARENT_PATH ?= "${OPTEE_FS_PARENT_PATH}"

python __anonymous() {
    if not d.getVar('TEE_FS_PARENT_PATH'):
        bb.fatal("OPTEE_FS_PARENT_PATH is unset; set it in the distro conf")
    if not d.getVar('TEE_FS_PARENT_PATH').startswith(d.getVar('DATA_MOUNT')):
        bb.fatal("OPTEE_FS_PARENT_PATH (%s) is not under DATA_MOUNT (%s): the fTPM "
                 "would lose its state on every reboot"
                 % (d.getVar('TEE_FS_PARENT_PATH'), d.getVar('DATA_MOUNT')))
}

do_install() {
    # tee-supplicant creates the subdirectories itself, but not the root of the
    # tree, and it starts well after the data partition is mounted.
    install -d ${D}${nonarch_libdir}/tmpfiles.d
    sed -e 's|@TEE_FS_PARENT_PATH@|${TEE_FS_PARENT_PATH}|g' \
        ${UNPACKDIR}/imx585-tee-storage.conf \
        > ${D}${nonarch_libdir}/tmpfiles.d/imx585-tee-storage.conf

    install -d ${D}${systemd_system_unitdir}/tee-supplicant.service.d
    sed -e 's|@DATA_MOUNT@|${DATA_MOUNT}|g' ${UNPACKDIR}/10-data-partition.conf \
        > ${D}${systemd_system_unitdir}/tee-supplicant.service.d/10-data-partition.conf
}

FILES:${PN} += "${nonarch_libdir}/tmpfiles.d ${systemd_system_unitdir}"

RDEPENDS:${PN} = "optee-client imx585-node-config"

PACKAGE_ARCH = "${MACHINE_ARCH}"
