set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
# Every configuration links the release CRT, see CMAKE_MSVC_RUNTIME_LIBRARY.
set(VCPKG_BUILD_TYPE release)
# Several modules share one SDL instance.
if(PORT STREQUAL "sdl1")
  set(VCPKG_LIBRARY_LINKAGE dynamic)
endif()
