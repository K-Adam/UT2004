cmake_minimum_required(VERSION 3.25)
project(UT2004 LANGUAGES C CXX RC)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(FATAL_ERROR "Only Win32 (x86) is supported for now: cmake --preset win32")
endif()

# The engine loads its DLLs and inis from System/.
set(UT_SYSTEM_DIR ${CMAKE_SOURCE_DIR}/System)
foreach(cfg "" _DEBUG _RELEASE _RELWITHDEBINFO _MINSIZEREL)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY${cfg} ${UT_SYSTEM_DIR})
endforeach()

# The prebuilt static libs expect the release DLL CRT.
set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreadedDLL)

add_subdirectory(zlib)
add_subdirectory(Vorbis)
add_subdirectory(Speex)
