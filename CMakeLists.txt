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

# Flags that reproduce the VS2003 semantics the code relies on.
add_library(ut_common INTERFACE)
target_compile_options(ut_common INTERFACE
  $<$<COMPILE_LANGUAGE:CXX>:/Zc:wchar_t- /Zc:forScope /permissive /Zc:strictStrings- /EHa>
  /Zp4 /W3 /MP
  /wd4005 /wd4244 /wd4267 /wd4305 /wd4996 /wd4595 /wd4291 /wd4838 /wd4311 /wd4312 /wd4302)
target_compile_definitions(ut_common INTERFACE
  WIN32 _WINDOWS UNICODE _UNICODE _WIN32_IE=0x0200 _WIN32_WINNT=0x0601
  DO_GUARD=1 DO_CHECK=1 DO_STAT=1 DO_CLOCK=1
  DO_GUARD_SLOW=0 DO_STAT_SLOW=0 DO_CLOCK_SLOW=0
  $<IF:$<CONFIG:Debug>,_DEBUG$<SEMICOLON>DEBUG$<SEMICOLON>DO_CHECK_SLOW=1,NDEBUG$<SEMICOLON>DO_CHECK_SLOW=0>
  _CRT_SECURE_NO_WARNINGS _CRT_NON_CONFORMING_SWPRINTFS _WINSOCK_DEPRECATED_NO_WARNINGS
  WINDOWS_IGNORE_PACKING_MISMATCH)  # original code includes windows.h under /Zp4 too
target_include_directories(ut_common INTERFACE ${CMAKE_SOURCE_DIR}/metoolkit/include)
target_link_libraries(ut_common INTERFACE legacy_stdio_definitions)
# Old prebuilt libs request VC6/7 CRT variants by name.
target_link_options(ut_common INTERFACE /SAFESEH:NO
  /NODEFAULTLIB:libc.lib /NODEFAULTLIB:libcp.lib /NODEFAULTLIB:libcmt.lib
  /NODEFAULTLIB:libci.lib /NODEFAULTLIB:libcd.lib /NODEFAULTLIB:libcmtd.lib /NODEFAULTLIB:msvcrtd.lib)

# ut_module(<name> SHARED|EXECUTABLE|WIN32 SOURCES ... [PCH hdr create.cpp] [NOPCH files...] [DEPS ...])
# Sources are relative to <Mod>/Src unless absolute.
function(ut_module name type)
  cmake_parse_arguments(A "" "OUTPUT" "SOURCES;PCH;NOPCH;DEPS" ${ARGN})
  list(TRANSFORM A_SOURCES PREPEND ${CMAKE_CURRENT_SOURCE_DIR}/Src/ REGEX "^[^/]")
  if(type STREQUAL "SHARED")
    add_library(${name} SHARED ${A_SOURCES})
    string(TOUPPER ${name} up)
    target_compile_definitions(${name} PRIVATE ${up}_API=__declspec\(dllexport\))
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
  target_link_libraries(${name} PRIVATE ut_common PUBLIC ${A_DEPS})
  # Explicit /Yc and /Yu because target_precompile_headers would force-include the header everywhere.
  if(A_PCH)
    list(GET A_PCH 0 hdr)
    list(GET A_PCH 1 create)
    foreach(src ${A_SOURCES})
      if(src MATCHES "\.cpp$")
        set_source_files_properties(${src} TARGET_DIRECTORY ${name} PROPERTIES COMPILE_OPTIONS "/Yu${hdr}")
      endif()
    endforeach()
    set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/Src/${create} TARGET_DIRECTORY ${name} PROPERTIES COMPILE_OPTIONS "/Yc${hdr}")
    foreach(src ${A_NOPCH})
      set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/Src/${src} TARGET_DIRECTORY ${name} PROPERTIES COMPILE_OPTIONS "")
    endforeach()
  endif()
endfunction()

set(UT_DX8 ${CMAKE_SOURCE_DIR}/DirectX8)
set(UT_DX9 ${CMAKE_SOURCE_DIR}/DirectX9)
# Karma physics only exists as prebuilt VC7.1 libs, see Core/Src/UnLegacyCrt.cpp.
set(UT_KARMA Mdt MdtKea MdtBcl Mst McdFrame McdCommon McdPrimitives McdConvex
    McdConvexCreateHull MeGlobals MeAssetFactory MeAssetDB MeAssetDBXMLIO MeXML)
list(TRANSFORM UT_KARMA PREPEND ${CMAKE_SOURCE_DIR}/metoolkit/lib.rel/win32_single_msvcrt/)
list(TRANSFORM UT_KARMA APPEND .lib)

add_subdirectory(zlib)
add_subdirectory(Vorbis)
add_subdirectory(Speex)

add_subdirectory(Core)
add_subdirectory(Engine)
foreach(m Fire IpDrv UWeb Window Editor WinDrv D3DDrv D3D9Drv OpenGLDrv ALAudio
          XGame XInterface Onslaught UTV2004 GUIDesigner
          Launch UCC UnrealEd Setup DebuggerLaunch BugReport Benchmark UccDepend)
  add_subdirectory(${m})
endforeach()

# -NOH keeps the checked-in *Classes.h headers.
add_custom_target(ucc_make
  COMMAND $<TARGET_FILE:UCC> make -NOH
  WORKING_DIRECTORY ${UT_SYSTEM_DIR}
  DEPENDS UCC Core Engine Editor IpDrv UWeb Fire XGame XInterface Onslaught UTV2004
  USES_TERMINAL VERBATIM)
