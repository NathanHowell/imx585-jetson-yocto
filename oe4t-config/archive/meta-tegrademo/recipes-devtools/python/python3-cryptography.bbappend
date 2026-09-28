# Ensure native build of cryptography links against Yocto's OpenSSL
DEPENDS:append:class-native = " openssl-native"

# Keep runpath pointing to the native sysroot so the loader finds our libcrypto
RUSTFLAGS:append:class-native = " -C link-arg=-Wl,-rpath,${STAGING_LIBDIR_NATIVE}"

# Provide a safety net for helper scripts executed during native tasks
python3_cryptography_native_ldpath="${STAGING_LIBDIR_NATIVE}:${STAGING_LIBDIR_NATIVE}/openssl"

do_compile:prepend:class-native() {
    export LD_LIBRARY_PATH="${python3_cryptography_native_ldpath}${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
}
