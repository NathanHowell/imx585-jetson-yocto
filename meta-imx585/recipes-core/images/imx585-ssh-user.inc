# Shared between imx585-console-image and imx585-minimal-image: a non-root user
# whose only credential is an authorized_keys entry supplied at build time.
# Works with both openssh and dropbear.

inherit extrausers

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
