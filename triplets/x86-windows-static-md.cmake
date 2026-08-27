set(VCPKG_TARGET_ARCHITECTURE x86)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
# Every configuration links the release CRT, see CMAKE_MSVC_RUNTIME_LIBRARY.
set(VCPKG_BUILD_TYPE release)
# Several modules share one SDL instance, ALAudio loads OpenAL at runtime and SDL loads ANGLE.
if(PORT MATCHES "^(sdl3|openal-soft|angle)$")
  set(VCPKG_LIBRARY_LINKAGE dynamic)
endif()
