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
    file://10-volatile-journal.conf \
    file://10-hostkey-dir.conf \
    file://10-hostkey-condition.conf \
"

S = "${UNPACKDIR}"

# Where the persistent partition is mounted, and the GPT partition label the
# mount unit looks for. systemd derives a .mount unit's name from its path, so
# DATA_MOUNT and the generated unit name have to agree -- hence the mangling
# below rather than a fixed filename.
DATA_MOUNT ?= "/data"
DATA_PARTLABEL ?= "imx585-data"
DATA_MOUNT_UNIT = "${@d.getVar('DATA_MOUNT').strip('/').replace('/', '-')}.mount"

do_install() {
    install -d ${D}${DATA_MOUNT}

    install -d ${D}${systemd_system_unitdir}
    sed -e 's|@DATA_MOUNT@|${DATA_MOUNT}|g' -e 's|@DATA_PARTLABEL@|${DATA_PARTLABEL}|g' \
        ${UNPACKDIR}/data.mount > ${D}${systemd_system_unitdir}/${DATA_MOUNT_UNIT}

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

SYSTEMD_SERVICE:${PN} = "${DATA_MOUNT_UNIT}"

FILES:${PN} += " \
    ${systemd_system_unitdir} \
    ${systemd_unitdir}/journald.conf.d \
    ${DATA_MOUNT} \
"

PACKAGE_ARCH = "${MACHINE_ARCH}"
