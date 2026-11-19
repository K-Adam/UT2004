set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME WindowsStore)
set(VCPKG_CMAKE_SYSTEM_VERSION 10.0)
# Every configuration links the release CRT, see CMAKE_MSVC_RUNTIME_LIBRARY.
set(VCPKG_BUILD_TYPE release)
set(VCPKG_PLATFORM_TOOLSET v143)
# ALAudio loads OpenAL at runtime and SDL loads ANGLE.
if(PORT MATCHES "^(openal-soft|angle)$")
  set(VCPKG_LIBRARY_LINKAGE dynamic)
endif()
