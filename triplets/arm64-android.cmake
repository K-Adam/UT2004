set(VCPKG_TARGET_ARCHITECTURE arm64)
# libc++_shared, since libmain.so and libopenal.so share one process.
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME Android)
set(VCPKG_CMAKE_SYSTEM_VERSION 28)
set(VCPKG_MAKE_BUILD_TRIPLET "--host=aarch64-linux-android")
set(VCPKG_CMAKE_CONFIGURE_OPTIONS -DANDROID_ABI=arm64-v8a)
set(VCPKG_BUILD_TYPE release)
# ALAudio loads OpenAL at runtime.
if(PORT STREQUAL "openal-soft")
  set(VCPKG_LIBRARY_LINKAGE dynamic)
endif()
