DESCRIPTION = "Console image for Jetson Orin Nano IMX585 + CEF168 bring-up"
LICENSE = "MIT"

inherit core-image extrausers

IMAGE_FEATURES += "ssh-server-openssh package-management"

CORE_IMAGE_BASE_INSTALL += "packagegroup-imx585 bash-completion"

# dtc/fdtget for inspecting the live device tree on target; rtcpu-debug exposes
# the camera RTCPU trace log, which is where VI/NVCSI capture errors surface.
IMAGE_INSTALL:append = " tegra-tools-tegrastats dtc nv-kernel-module-rtcpu-debug"

KERNEL_MODULE_AUTOLOAD:append = " imx585 cef168"

IMAGE_LINGUAS = "en-us"

SSH_USER ?= "orin"
SSH_USER_UID ?= "1000"
SSH_USER_AUTHORIZED_KEY ?= ""

EXTRA_USERS_PARAMS += "useradd -m -U -u ${SSH_USER_UID} -s /bin/sh -p '*' ${SSH_USER};"

install_ssh_key_for_user () {
    if [ -z "${SSH_USER_AUTHORIZED_KEY}" ]; then
        echo "NOTE: SSH_USER_AUTHORIZED_KEY not set; skipping key install" >&2
        return
    fi

    install -d -m 0700 ${IMAGE_ROOTFS}/home/${SSH_USER}/.ssh
    cat > ${IMAGE_ROOTFS}/home/${SSH_USER}/.ssh/authorized_keys <<EOF2
${SSH_USER_AUTHORIZED_KEY}
EOF2
    chmod 0600 ${IMAGE_ROOTFS}/home/${SSH_USER}/.ssh/authorized_keys
    chown ${SSH_USER_UID}:${SSH_USER_UID} ${IMAGE_ROOTFS}/home/${SSH_USER} ${IMAGE_ROOTFS}/home/${SSH_USER}/.ssh ${IMAGE_ROOTFS}/home/${SSH_USER}/.ssh/authorized_keys
}

ROOTFS_POSTPROCESS_COMMAND += "install_ssh_key_for_user; "
