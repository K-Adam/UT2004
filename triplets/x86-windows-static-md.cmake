set(VCPKG_TARGET_ARCHITECTURE x86)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
# Every configuration links the release CRT, see CMAKE_MSVC_RUNTIME_LIBRARY.
set(VCPKG_BUILD_TYPE release)
# Several modules share one SDL instance, and ALAudio loads OpenAL at runtime.
if(PORT MATCHES "^(sdl1|openal-soft)$")
  set(VCPKG_LIBRARY_LINKAGE dynamic)
endif()
