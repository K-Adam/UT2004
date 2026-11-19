set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
# Every configuration links the release CRT, see CMAKE_MSVC_RUNTIME_LIBRARY.
set(VCPKG_BUILD_TYPE release)
# The static libraries must not need a newer STL than the Visual Studio 2022 toolset the presets build with.
set(VCPKG_PLATFORM_TOOLSET v143)
# Several modules share one SDL instance, ALAudio loads OpenAL at runtime and SDL loads ANGLE.
if(PORT MATCHES "^(sdl3|openal-soft|angle)$")
  set(VCPKG_LIBRARY_LINKAGE dynamic)
endif()
# vcpkg builds ANGLE without program binaries, which the shader cache of OpenGLDrv loads.
if(PORT STREQUAL "angle")
  set(VCPKG_C_FLAGS "")
  set(VCPKG_CXX_FLAGS "/DANGLE_HAS_BINARY_LOADING")
endif()
