# Stage NVIDIA's generated conftest headers so the sysroot's camera headers are
# actually usable by an out-of-tree module.
#
# nvidia-kernel-oot.inc's do_install stages headers with
#
#     cp -R ${S}/nvidia-oot/include/* ${D}/${includedir}/${BPN}
#
# which copies media/camera_common.h, media/tegracam_core.h and friends -- but
# camera_common.h line 10 is
#
#     #include <nvidia/conftest.h>
#
# and that header is *generated* during nvidia-kernel-oot's own build, into
# ${B}/out/nvidia-conftest/nvidia/, which is outside ${S}/nvidia-oot/include and
# therefore never staged. The result is that the staged header set is not
# self-contained: anything that includes camera_common.h from the sysroot dies
# with "fatal error: nvidia/conftest.h: No such file or directory".
#
# These headers are not boilerplate and must not be stubbed. conftest.sh probes
# this specific kernel tree for ~200 API shapes (v4l2_subdev_pad_ops_struct_has_*,
# i2c_driver_struct_probe_without_i2c_device_id_arg, media_entity_remote_pad, ...)
# and the results are what let one source file build across BSP kernel versions.
# A hand-written stub would silently assert the wrong kernel API and miscompile.
# So we install the generated tree as-is.
#
# ${B} == ${S} for this recipe (nvidia-kernel-oot.inc sets B = "${S}").
do_install:append() {
    if [ ! -f ${B}/out/nvidia-conftest/nvidia/conftest.h ]; then
        bbfatal "nvidia/conftest.h not found under ${B}/out/nvidia-conftest. \
The conftest output location changed; find it with: \
find ${B} -name conftest.h -path '*nvidia*'"
    fi
    install -d ${D}${includedir}/${BPN}/nvidia
    cp -R ${B}/out/nvidia-conftest/nvidia/. ${D}${includedir}/${BPN}/nvidia/
    # Build scaffolding, not headers -- no reason to ship it in -dev.
    rm -f ${D}${includedir}/${BPN}/nvidia/BUILD.bazel \
          ${D}${includedir}/${BPN}/nvidia/Makefile \
          ${D}${includedir}/${BPN}/nvidia/conftest.sh
}
