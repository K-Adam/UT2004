vcpkg_check_linkage(ONLY_STATIC_LIBRARY)

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO sctplab/usrsctp
    # The revision libdatachannel 0.24.6 pins as its submodule. The last tagged release is from 2021.
    REF fec583d54493f879d2ae44a743423bf8a04371ab
    SHA512 f98ab4945e7a200b54c0139614c730e4ff4b8fd69e38f0e9905e994133a07f614651126f4dc25ca626b58d4584dfaf2eb8266864f826dfd0c8e48567d03a2315
    HEAD_REF master
    PATCHES
        fix_export.patch
        mingw.diff
)

vcpkg_cmake_configure(
    SOURCE_PATH ${SOURCE_PATH}
    OPTIONS
        -Dsctp_werror=OFF
        -Dsctp_build_programs=OFF
)

vcpkg_cmake_install()

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")

configure_file("${SOURCE_PATH}/LICENSE.md" "${CURRENT_PACKAGES_DIR}/share/usrsctp/copyright" COPYONLY)

vcpkg_copy_pdbs()

vcpkg_fixup_pkgconfig()
vcpkg_cmake_config_fixup(PACKAGE_NAME unofficial-${PORT} CONFIG_PATH share/unofficial-${PORT})
