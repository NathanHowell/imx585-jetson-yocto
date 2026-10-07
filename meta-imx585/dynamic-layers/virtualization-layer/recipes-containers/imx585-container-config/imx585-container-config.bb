SUMMARY = "Container runtime configuration for the IMX585 images"
DESCRIPTION = "Points podman's image store at the persistent data partition, \
makes containers read-only by default, and regenerates the NVIDIA CDI spec into \
/run at boot so it works whether or not /etc is writable."

LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

inherit systemd features_check

REQUIRED_DISTRO_FEATURES = "virtualization systemd"

SRC_URI = " \
    file://20-imx585.conf \
    file://nvidia-cdi-generate.service \
"

S = "${UNPACKDIR}"

IMX585_DATA_MOUNT ??= "/data"
DATA_MOUNT ?= "${IMX585_DATA_MOUNT}"
CONTAINER_GRAPHROOT ?= "${DATA_MOUNT}/containers/storage"

do_install() {
    install -d ${D}${sysconfdir}/containers/containers.conf.d
    sed -e 's|@CDI_DIR@|/run/cdi|g' ${UNPACKDIR}/20-imx585.conf \
        > ${D}${sysconfdir}/containers/containers.conf.d/20-imx585.conf

    install -d ${D}${systemd_system_unitdir}
    install -m 0644 ${UNPACKDIR}/nvidia-cdi-generate.service ${D}${systemd_system_unitdir}/
}

SYSTEMD_SERVICE:${PN} = "nvidia-cdi-generate.service"

FILES:${PN} += "${sysconfdir}/containers ${systemd_system_unitdir}"

# nvidia-ctk comes from nvidia-container-toolkit; podman owns
# /etc/containers/storage.conf, which the container-host-config bbappend edits.
RDEPENDS:${PN} = "nvidia-container-toolkit imx585-node-config"

PACKAGE_ARCH = "${MACHINE_ARCH}"
