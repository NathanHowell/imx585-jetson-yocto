DESCRIPTION = "Console-focused image for the Jetson Orin Nano with NVIDIA GPU and container tooling"
LICENSE = "MIT"

inherit core-image extrausers

IMAGE_FEATURES += "ssh-server-openssh package-management"

VIRTUAL-RUNTIME_container_engine ?= "docker"
VIRTUAL-RUNTIME_container_runtime ?= "virtual-runc"

CORE_IMAGE_BASE_INSTALL += "packagegroup-orin-console"

# Ensure useful admin tools remain available on the target
CORE_IMAGE_BASE_INSTALL += "bash-completion"

# Provide dtc utilities (dtc, fdtget, fdtdump) and camrtc debug module on the target
IMAGE_INSTALL:append = " tegra-tools-tegrastats dtc nv-kernel-module-rtcpu-debug"

KERNEL_MODULE_AUTOLOAD:append = " imx585 cef168"

IMAGE_LINGUAS = "en-us"

SSH_USER ?= "orin"
SSH_USER_UID ?= "1000"
SSH_USER_AUTHORIZED_KEY ?= ""

EXTRA_USERS_PARAMS += "useradd -m -U -u ${SSH_USER_UID} -s /bin/zsh -p '*' ${SSH_USER};"

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
