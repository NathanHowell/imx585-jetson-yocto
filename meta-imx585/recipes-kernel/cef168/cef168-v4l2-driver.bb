SUMMARY = "CEF168 lens controller V4L2 driver"
DESCRIPTION = "Out-of-tree V4L2 kernel module for the Pinefeat CEF168 Canon EF/EF-S lens controller."
HOMEPAGE = "https://github.com/pinefeat/cef168"
LICENSE = "GPL-2.0-only"
LIC_FILES_CHKSUM = "file://LICENSE;md5=b234ee4d69f5fce4486a80fdaf4a4263"

SRC_URI = "git://github.com/pinefeat/cef168.git;protocol=https;branch=main"
SRCREV = "3abcaeeffa118218b08c310bca4d7ecfbbc60ff0"

PV = "1.0+git${SRCPV}"

inherit module

DEPENDS += "virtual/kernel"

COMPATIBLE_MACHINE = "(tegra)"

KERNEL_MODULE_PACKAGE_SUFFIX = ""

PACKAGES += "kernel-module-cef168"
FILES:kernel-module-cef168 = "${base_libdir}/modules/${KERNEL_VERSION}/kernel/drivers/media/i2c/cef168.ko"
FILES:${PN} = ""
INSANE_SKIP:${PN} += "installed-vs-shipped"
INSANE_SKIP:${PN}-dbg += "usrmerge buildpaths"

EXTRA_OEMAKE += "KDIR=${STAGING_KERNEL_DIR} -C ${STAGING_KERNEL_DIR} M=${S} INSTALL_MOD_DIR=kernel/drivers/media/i2c"

do_configure:append() {
    if [ ! -f ${S}/Makefile ]; then
        if [ ! -f ${S}/Makefile.template ]; then
            bbfatal "Makefile.template is missing in upstream checkout"
        fi
        cp ${S}/Makefile.template ${S}/Makefile
    fi
}
