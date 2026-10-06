SUMMARY = "Read-only-rootfs node configuration: persistent data partition, volatile logs"
DESCRIPTION = "The pieces a read-only rootfs needs on this board: a mount unit \
for the persistent data partition, journald kept in RAM, and dropbear host keys \
moved off /etc so key generation does not fail at first boot."

LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

inherit systemd features_check

REQUIRED_DISTRO_FEATURES = "systemd"

SRC_URI = " \
    file://data.mount \
    file://imx585-data-partition.service \
    file://imx585-data-partition-setup \
    file://10-volatile-journal.conf \
    file://10-hostkey-dir.conf \
    file://10-hostkey-condition.conf \
"

S = "${UNPACKDIR}"

# Where the persistent partition is mounted, and the GPT partition label the
# mount unit looks for. systemd derives a .mount unit's name from its path, so
# DATA_MOUNT and the generated unit name have to agree -- hence the mangling
# below rather than a fixed filename.
IMX585_DATA_MOUNT ??= "/data"
DATA_MOUNT ?= "${IMX585_DATA_MOUNT}"
DATA_PARTLABEL ?= "imx585-data"
DATA_MOUNT_UNIT = "${@d.getVar('DATA_MOUNT').strip('/').replace('/', '-')}.mount"

# The disk to partition, derived from the machine's boot device by dropping the
# partition suffix: nvme0n1p1 -> nvme0n1, mmcblk0p1 -> mmcblk0. Done without the
# re module, which bitbake does not expose to inline python. Override if the data
# partition belongs on a different disk from the rootfs.
DATA_DISK ?= "/dev/${@d.getVar('TNSPEC_BOOTDEV').rstrip('0123456789').rstrip('p')}"

# Refuse to create anything smaller than this. A tiny partition would be worse
# than none: podman would start and then fill it.
DATA_MIN_BYTES ?= "8589934592"

do_install() {
    install -d ${D}${DATA_MOUNT}

    # First-boot partition creation. Keyed on the GPT label, so later boots exit
    # immediately; see the script for the imx585.no_data_partition escape hatch.
    install -d ${D}${libexecdir}
    sed -e 's|@DATA_DISK@|${DATA_DISK}|g' \
        -e 's|@DATA_PARTLABEL@|${DATA_PARTLABEL}|g' \
        -e 's|@DATA_MIN_BYTES@|${DATA_MIN_BYTES}|g' \
        ${UNPACKDIR}/imx585-data-partition-setup > ${D}${libexecdir}/imx585-data-partition-setup
    chmod 0755 ${D}${libexecdir}/imx585-data-partition-setup

    install -d ${D}${systemd_system_unitdir}
    sed -e 's|@DATA_MOUNT@|${DATA_MOUNT}|g' -e 's|@DATA_PARTLABEL@|${DATA_PARTLABEL}|g' \
        ${UNPACKDIR}/data.mount > ${D}${systemd_system_unitdir}/${DATA_MOUNT_UNIT}

    sed -e 's|@DATA_MOUNT_UNIT@|${DATA_MOUNT_UNIT}|g' -e 's|@LIBEXECDIR@|${libexecdir}|g' \
        ${UNPACKDIR}/imx585-data-partition.service \
        > ${D}${systemd_system_unitdir}/imx585-data-partition.service

    # journald: everything of interest is shipped to the collector, so nothing
    # is written locally. On a read-only rootfs Storage=auto would fall back to
    # volatile anyway because /var/log/journal is absent, but that is an accident
    # of the filesystem rather than a decision. RuntimeMaxUse needs setting
    # regardless: the default is 10% of RAM, i.e. 800 MB on an 8 GB Orin Nano.
    install -d ${D}${systemd_unitdir}/journald.conf.d
    install -m 0644 ${UNPACKDIR}/10-volatile-journal.conf ${D}${systemd_unitdir}/journald.conf.d/

    # dropbearkey.service writes to /etc/dropbear, which does not exist and
    # cannot be created on a read-only rootfs. Point both units at the data
    # partition so the host key survives reboots -- a key regenerated into /run
    # on every boot would make every reconnection look like a MITM.
    #
    # Only dropbearkey gets the condition override. dropbear@.service has no
    # ConditionPathExists of its own, and giving it one that tests for the
    # absence of the key would stop the server starting as soon as the key
    # existed.
    install -d ${D}${systemd_system_unitdir}/dropbearkey.service.d
    install -d ${D}${systemd_system_unitdir}/dropbear@.service.d
    for f in 10-hostkey-dir.conf 10-hostkey-condition.conf; do
        sed -e 's|@DATA_MOUNT@|${DATA_MOUNT}|g' ${UNPACKDIR}/$f \
            > ${D}${systemd_system_unitdir}/dropbearkey.service.d/$f
    done
    sed -e 's|@DATA_MOUNT@|${DATA_MOUNT}|g' ${UNPACKDIR}/10-hostkey-dir.conf \
        > ${D}${systemd_system_unitdir}/dropbear@.service.d/10-hostkey-dir.conf
}

SYSTEMD_SERVICE:${PN} = "${DATA_MOUNT_UNIT} imx585-data-partition.service"

FILES:${PN} += " \
    ${systemd_system_unitdir} \
    ${systemd_unitdir}/journald.conf.d \
    ${libexecdir}/imx585-data-partition-setup \
    ${DATA_MOUNT} \
"

# sgdisk writes the GPT, partx makes the kernel see the new partition without a
# full table re-read (impossible with the rootfs mounted), mkfs.ext4 formats it.
RDEPENDS:${PN} = "gptfdisk util-linux-partx util-linux-blkid e2fsprogs-mke2fs"

PACKAGE_ARCH = "${MACHINE_ARCH}"
