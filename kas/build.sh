#!/bin/sh
# kas wrapper. Use this instead of calling kas directly.
#
#   kas/build.sh build kas/imx585.yml
#   kas/build.sh shell kas/imx585.yml
#   kas/build.sh dump  kas/imx585.yml
#
# Two directories cannot be expressed in local.conf and so cannot live in
# imx585.yml with DL_DIR/SSTATE_DIR/TMPDIR:
#
#   KAS_WORK_DIR   where kas clones openembedded-core, meta-tegra, bitbake, ...
#   KAS_BUILD_DIR  the bitbake build directory (conf/, cache/)
#
# Both default to the *current directory*. This repo has no .gitignore -- that is
# deliberate, since the old tree's one real mistake was a build/conf that git was
# told to ignore -- so a bare `kas build` here would clone several GB of layers
# into the working tree, on a /home with ~22G free. Hence this wrapper.
set -eu

: "${KAS_WORK_DIR:=/build/yocto/layers}"
: "${KAS_BUILD_DIR:=/build/yocto/build}"
export KAS_WORK_DIR KAS_BUILD_DIR

if [ ! -d "$KAS_WORK_DIR" ]; then
    echo "kas/build.sh: $KAS_WORK_DIR does not exist." >&2
    echo "Is /build mounted?  mount | grep /build" >&2
    exit 1
fi

exec kas "$@"
