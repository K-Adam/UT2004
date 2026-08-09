cmake_minimum_required(VERSION 3.25)
project(UT2004 LANGUAGES C CXX)

if(WIN32)
  enable_language(RC)
  if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "Windows builds are Win32 (x86) only: cmake --preset win32")
  endif()
elseif(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR NOT CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|i.86")
  message(FATAL_ERROR "Only Windows and x86 or x64 Linux are supported: cmake --preset win32|linux|linux32")
endif()

# The engine loads its modules and inis from System/.
set(UT_SYSTEM_DIR ${CMAKE_SOURCE_DIR}/System)
foreach(cfg "" _DEBUG _RELEASE _RELWITHDEBINFO _MINSIZEREL)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY${cfg} ${UT_SYSTEM_DIR})
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY${cfg} ${UT_SYSTEM_DIR})
endforeach()

add_library(ut_common INTERFACE)
target_compile_definitions(ut_common INTERFACE
  UNICODE _UNICODE
  DO_GUARD=1 DO_CHECK=1 DO_STAT=1 DO_CLOCK=1
  DO_GUARD_SLOW=0 DO_STAT_SLOW=0 DO_CLOCK_SLOW=0
  $<IF:$<CONFIG:Debug>,_DEBUG$<SEMICOLON>DEBUG$<SEMICOLON>DO_CHECK_SLOW=1,DO_CHECK_SLOW=0>)
target_include_directories(ut_common INTERFACE ${CMAKE_SOURCE_DIR}/metoolkit/include)

if(MSVC)
  # The prebuilt static libs expect the release DLL CRT.
  set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
  set(CMAKE_MAP_IMPORTED_CONFIG_DEBUG Release)

  # Flags that reproduce the VS2003 semantics the code relies on.
  target_compile_options(ut_common INTERFACE
    $<$<COMPILE_LANGUAGE:CXX>:/Zc:wchar_t- /Zc:forScope /permissive /Zc:strictStrings- /EHa>
    /Zp4 /W3 /MP
    /wd4005 /wd4244 /wd4267 /wd4305 /wd4996 /wd4595 /wd4291 /wd4838 /wd4311 /wd4312 /wd4302)
  target_compile_definitions(ut_common INTERFACE
    _WIN32_IE=0x0200 _WIN32_WINNT=0x0601
    _CRT_SECURE_NO_WARNINGS _CRT_NON_CONFORMING_SWPRINTFS _WINSOCK_DEPRECATED_NO_WARNINGS
    WINDOWS_IGNORE_PACKING_MISMATCH)  # original code includes windows.h under /Zp4 too
  target_link_libraries(ut_common INTERFACE legacy_stdio_definitions)
  # Old prebuilt libs request VC6/7 CRT variants by name.
  target_link_options(ut_common INTERFACE /SAFESEH:NO
    /NODEFAULTLIB:libc.lib /NODEFAULTLIB:libcp.lib /NODEFAULTLIB:libcmt.lib
    /NODEFAULTLIB:libci.lib /NODEFAULTLIB:libcd.lib /NODEFAULTLIB:libcmtd.lib /NODEFAULTLIB:msvcrtd.lib)
else()
  # Modules are dlopen()ed by bare name, so they and the executables look next to themselves.
  set(CMAKE_BUILD_RPATH "$ORIGIN")
  # Packages store UTF-16, TCHAR must be 2 bytes like on Windows. Core supplies the wcs* functions.
  # The engine calls destructors explicitly and reuses the object (FArray in UArrayProperty::DestroyValue),
  # so the stores a destructor makes must not be optimized away.
  target_compile_options(ut_common INTERFACE
    -fshort-wchar -fsigned-char -fno-strict-aliasing -fno-delete-null-pointer-checks -w
    $<$<COMPILE_LANGUAGE:CXX>:-fpermissive -fno-lifetime-dse>)
  # 32-bit stat fails with EOVERFLOW on large inode numbers (WSL drives, XFS) without 64-bit file offsets.
  target_compile_definitions(ut_common INTERFACE __LINUX_X86__ LINUX=1 __FILE_NOCASE=1 _FILE_OFFSET_BITS=64)
  target_link_libraries(ut_common INTERFACE dl pthread)
  if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    # x87 math loads maps three times slower. MSVC uses SSE2 for Win32 too.
    target_compile_options(ut_common INTERFACE -msse2 -mfpmath=sse)
  endif()
endif()

# ut_module(<name> SHARED|EXECUTABLE|WIN32 [OUTPUT name] SOURCES ... [PCH hdr create.cpp] [NOPCH files...] [DEPS ...])
# Sources are relative to <Mod>/Src unless absolute.
function(ut_module name type)
  cmake_parse_arguments(A "" "OUTPUT" "SOURCES;PCH;NOPCH;DEPS" ${ARGN})
  list(TRANSFORM A_SOURCES PREPEND ${CMAKE_CURRENT_SOURCE_DIR}/Src/ REGEX "^[^/]")
  if(type STREQUAL "SHARED")
    add_library(${name} SHARED ${A_SOURCES})
    string(TOUPPER ${name} up)
    if(MSVC)
      target_compile_definitions(${name} PRIVATE ${up}_API=__declspec\(dllexport\))
    else()
      # Every module exports GPackage, ELF would bind all of them to the first one loaded.
      target_compile_definitions(${name} PRIVATE ${up}_API= GPackage=GPackage${name})
      set_target_properties(${name} PROPERTIES PREFIX "")
    endif()
  elseif(type STREQUAL "WIN32")
    add_executable(${name} WIN32 ${A_SOURCES})
  else()
    add_executable(${name} ${A_SOURCES})
  endif()
  if(A_OUTPUT)
    set_target_properties(${name} PROPERTIES OUTPUT_NAME ${A_OUTPUT})
  endif()
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/Inc)
    target_include_directories(${name} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/Inc)
  endif()
  target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Src)
  target_link_libraries(${name} PUBLIC ${A_DEPS} PRIVATE ut_common)
  if(type STREQUAL "EXECUTABLE" AND NOT WIN32)
    # The Linux Karma libs are not position independent, so the executables carry them
    # and export them to Engine.so.
    target_link_libraries(${name} PRIVATE ${KARMA_LIBS})
    target_link_options(${name} PRIVATE -no-pie)
    set_target_properties(${name} PROPERTIES ENABLE_EXPORTS ON)
  endif()
  # Explicit /Yc and /Yu because target_precompile_headers would force-include the header everywhere.
  if(A_PCH AND MSVC)
    list(GET A_PCH 0 hdr)
    list(GET A_PCH 1 create)
    foreach(src ${A_SOURCES})
      if(src MATCHES "\\.cpp$")
        set_source_files_properties(${src} TARGET_DIRECTORY ${name} PROPERTIES COMPILE_OPTIONS "/Yu${hdr}")
      endif()
    endforeach()
    set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/Src/${create} TARGET_DIRECTORY ${name} PROPERTIES COMPILE_OPTIONS "/Yc${hdr}")
    foreach(src ${A_NOPCH})
      set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/Src/${src} TARGET_DIRECTORY ${name} PROPERTIES COMPILE_OPTIONS "")
    endforeach()
  endif()
endfunction()

# Karma physics only exists as prebuilt libs.
set(KARMA_LIBS Mdt MdtKea MdtBcl Mst McdFrame McdCommon McdPrimitives McdConvex
    McdConvexCreateHull MeGlobals MeAssetFactory MeAssetDB MeAssetDBXMLIO MeXML)
if(WIN32)
  # VC7.1 libs, see Core/Src/UnLegacyCrt.cpp. Consumers must list them before Core.lib
  # so their stdio imports resolve to Core's exports.
  list(TRANSFORM KARMA_LIBS PREPEND ${CMAKE_SOURCE_DIR}/metoolkit/lib.rel/win32_single_msvcrt/)
  list(TRANSFORM KARMA_LIBS APPEND .lib)
  list(APPEND KARMA_LIBS legacy_stdio_definitions)
else()
  if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(KARMA_DIR linux_hx_single)
  else()
    set(KARMA_DIR linux_single_gcc3.2)
  endif()
  list(TRANSFORM KARMA_LIBS PREPEND ${CMAKE_SOURCE_DIR}/metoolkit/lib.rel/${KARMA_DIR}/lib)
  list(TRANSFORM KARMA_LIBS APPEND .a)
  # Whole archives because only Engine.so references them, and ld does not pull archive
  # members for undefined symbols of shared libraries.
  string(JOIN "," KARMA_GROUP ${KARMA_LIBS})
  set(KARMA_LIBS "$<LINK_LIBRARY:WHOLE_ARCHIVE,${KARMA_GROUP}>")
endif()

find_package(ZLIB REQUIRED)
if(WIN32)
  find_package(Vorbis CONFIG REQUIRED)
  find_package(dxsdk-d3dx CONFIG REQUIRED)
else()
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(VORBISFILE REQUIRED IMPORTED_TARGET GLOBAL vorbisfile)
  add_library(Vorbis::vorbisfile ALIAS PkgConfig::VORBISFILE)
  # Debian and Ubuntu cannot install the SDL 1.2 dev package for two architectures at once,
  # so the runtime library is accepted when the dev symlink is missing.
  find_path(SDL_INCLUDE_DIR SDL.h PATH_SUFFIXES SDL REQUIRED)
  find_library(SDL_LIBRARY NAMES SDL libSDL-1.2.so.0 REQUIRED)
  add_library(SDL::SDL INTERFACE IMPORTED GLOBAL)
  target_include_directories(SDL::SDL INTERFACE ${SDL_INCLUDE_DIR})
  target_link_libraries(SDL::SDL INTERFACE ${SDL_LIBRARY})
endif()
# Speex stays vendored for Epic's 48 kHz mode (EPIC_48K), which upstream lacks.
add_subdirectory(Speex)

set(UT_MODULES Core Engine Fire IpDrv UWeb Editor OpenGLDrv ALAudio
    XGame XInterface Onslaught UTV2004 UCC)
if(WIN32)
  list(APPEND UT_MODULES Window WinDrv D3D9Drv GUIDesigner
       Launch UnrealEd Setup DebuggerLaunch BugReport Benchmark UccDepend)
else()
  list(APPEND UT_MODULES SDLDrv SDLLaunch)
endif()
foreach(m ${UT_MODULES})
  add_subdirectory(${m})
endforeach()

# -NOH keeps the checked-in *Classes.h headers. The script compiler only works in 32-bit builds.
if(WIN32)
  add_custom_target(ucc_make
    COMMAND $<TARGET_FILE:UCC> make -NOH
    WORKING_DIRECTORY ${UT_SYSTEM_DIR}
    DEPENDS UCC Core Engine Editor IpDrv UWeb Fire XGame XInterface Onslaught UTV2004
    USES_TERMINAL VERBATIM)
endif()
