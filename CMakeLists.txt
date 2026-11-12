cmake_minimum_required(VERSION 3.26)
project(UT2004 LANGUAGES C CXX)
if(EMSCRIPTEN AND EMSCRIPTEN_VERSION VERSION_LESS 7.0.0)
  message(FATAL_ERROR "The web build needs emsdk 7.0.0 or newer, this one is ${EMSCRIPTEN_VERSION}")
endif()

if(WIN32)
  enable_language(RC)
elseif(ANDROID)
  if(NOT UT_STATIC_LINK)
    message(FATAL_ERROR "Android needs UT_STATIC_LINK, the app is a single libmain.so: cmake --preset android-arm64")
  endif()
  # The static modules end up in that shared library.
  set(CMAKE_POSITION_INDEPENDENT_CODE ON)
elseif(EMSCRIPTEN)
  if(NOT UT_STATIC_LINK OR NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "The web build is a single wasm32 module: cmake --preset web")
  endif()
elseif(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR NOT CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|i.86|aarch64")
  message(FATAL_ERROR "Only Windows, x86, x64 or arm64 Linux, arm64 Android and the web are supported: cmake --preset win32|win64|linux64|linux32|android-arm64|web")
endif()

# Each build runs from its own game folder, so architectures don't overwrite each other and runtime
# files stay out of the sources. The engine loads modules, inis and localization from System/ and
# finds script packages in the shared Scripts folder through the Paths in Default.ini.
set(UT_SYSTEM_DIR ${CMAKE_BINARY_DIR}/System)
cmake_path(SET UT_SCRIPTS_DIR NORMALIZE ${CMAKE_BINARY_DIR}/../Scripts)
foreach(cfg "" _DEBUG _RELEASE _RELWITHDEBINFO _MINSIZEREL)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY${cfg} ${UT_SYSTEM_DIR})
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY${cfg} ${UT_SYSTEM_DIR})
endforeach()
# The cache manager updates CacheRecords.ucl at runtime, so it is only seeded.
if(NOT EXISTS ${UT_SCRIPTS_DIR}/CacheRecords.ucl)
  file(COPY ${CMAKE_SOURCE_DIR}/System/CacheRecords.ucl DESTINATION ${UT_SCRIPTS_DIR})
endif()
# The game rewrites the ini it runs on, so development runs get their own copies of the tracked templates.
if(NOT EMSCRIPTEN AND NOT ANDROID)
  foreach(ini Dev.ini DevUser.ini)
    if(NOT EXISTS ${UT_SYSTEM_DIR}/${ini})
      file(COPY ${CMAKE_SOURCE_DIR}/DevIni/${ini} DESTINATION ${UT_SYSTEM_DIR})
    endif()
  endforeach()
endif()
function(ut_link name target)
  if(NOT EXISTS ${CMAKE_BINARY_DIR}/${name})
    if(CMAKE_HOST_WIN32)
      # A junction, because symbolic links need administrator rights on Windows.
      file(TO_NATIVE_PATH ${CMAKE_BINARY_DIR}/${name} link)
      file(TO_NATIVE_PATH ${target} target)
      # Drops a junction left dangling by a moved checkout.
      execute_process(COMMAND cmd /c rmdir ${link} OUTPUT_QUIET ERROR_QUIET)
      execute_process(COMMAND cmd /c mklink /J ${link} ${target} OUTPUT_QUIET COMMAND_ERROR_IS_FATAL ANY)
    else()
      file(CREATE_LINK ${target} ${CMAKE_BINARY_DIR}/${name} SYMBOLIC)
    endif()
  endif()
endfunction()
# Read-only data the engine reads from next to System/.
foreach(dir Help Web KarmaData)
  ut_link(${dir} ${CMAKE_SOURCE_DIR}/${dir})
endforeach()
# Dev.ini and the web page served from build/web read the content through this link.
set(UT_ASSETS_DIR "$ENV{UT_ASSETS_DIR}" CACHE PATH "Content set for development runs, with the packages in src and the packed web files in build/web")
if(UT_ASSETS_DIR AND NOT ANDROID)
  ut_link(assets ${UT_ASSETS_DIR})
endif()
add_custom_target(ut_stage ALL
  COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different ${CMAKE_SOURCE_DIR}/System ${UT_SYSTEM_DIR}
  VERBATIM)

option(UT_STATIC_LINK "Link the engine modules into the executables instead of loading them at runtime" OFF)

add_library(ut_common INTERFACE)
target_compile_definitions(ut_common INTERFACE
  UNICODE _UNICODE __STATIC_LINK=$<BOOL:${UT_STATIC_LINK}>
  DO_GUARD=1 DO_CHECK=1 DO_STAT=1 DO_CLOCK=1
  DO_GUARD_SLOW=0 DO_STAT_SLOW=0 DO_CLOCK_SLOW=0
  $<IF:$<CONFIG:Debug>,_DEBUG$<SEMICOLON>DEBUG$<SEMICOLON>DO_CHECK_SLOW=1,DO_CHECK_SLOW=0>)
target_include_directories(ut_common INTERFACE ${CMAKE_SOURCE_DIR}/metoolkit-karma/include)

if(MSVC)
  # The prebuilt static libs expect the release DLL CRT.
  set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
  set(CMAKE_MAP_IMPORTED_CONFIG_DEBUG Release)

  # Flags that reproduce the VS2003 semantics the code relies on.
  target_compile_options(ut_common INTERFACE
    $<$<COMPILE_LANGUAGE:CXX>:/Zc:wchar_t- /Zc:forScope /permissive /Zc:strictStrings- /EHa>
    /Zp4 /W3 /MP /guard:cf
    /wd4005 /wd4244 /wd4267 /wd4305 /wd4996 /wd4595 /wd4291 /wd4838 /wd4311 /wd4312 /wd4302)
  target_compile_definitions(ut_common INTERFACE
    _WIN32_IE=0x0200 _WIN32_WINNT=0x0601
    _CRT_SECURE_NO_WARNINGS _CRT_NON_CONFORMING_SWPRINTFS _WINSOCK_DEPRECATED_NO_WARNINGS
    WINDOWS_IGNORE_PACKING_MISMATCH)  # original code includes windows.h under /Zp4 too
  target_link_libraries(ut_common INTERFACE legacy_stdio_definitions)
  # Old prebuilt libs request VC6/7 CRT variants by name.
  target_link_options(ut_common INTERFACE /guard:cf $<$<EQUAL:${CMAKE_SIZEOF_VOID_P},4>:/SAFESEH:NO>
    /NODEFAULTLIB:libc.lib /NODEFAULTLIB:libcp.lib /NODEFAULTLIB:libcmt.lib
    /NODEFAULTLIB:libci.lib /NODEFAULTLIB:libcd.lib /NODEFAULTLIB:libcmtd.lib /NODEFAULTLIB:msvcrtd.lib)
else()
  # Modules are dlopen()ed by bare name, so they and the executables look next to themselves.
  set(CMAKE_BUILD_RPATH "$ORIGIN")
  # Packages store UTF-16, TCHAR must be 2 bytes like on Windows. Core supplies the wcs* functions,
  # and Clang would turn their loops back into calls to the C library's 4-byte wcslen.
  target_compile_options(ut_common INTERFACE
    -fshort-wchar -fno-builtin-wcslen -fsigned-char -fno-strict-aliasing -fno-delete-null-pointer-checks -w
    $<$<CXX_COMPILER_ID:GNU>:$<$<COMPILE_LANGUAGE:CXX>:-fpermissive>>
    # Like MSVC, template bodies that use classes declared later in the header are checked at instantiation.
    $<$<CXX_COMPILER_ID:Clang>:$<$<COMPILE_LANGUAGE:CXX>:-fdelayed-template-parsing -Wno-register>>)
  # The engine calls destructors explicitly and reuses the object (FArray in UArrayProperty::DestroyValue),
  # so the stores a destructor makes must not be optimized away. Newer Clang does the same optimization and accepts the flag.
  include(CheckCXXCompilerFlag)
  check_cxx_compiler_flag(-fno-lifetime-dse UT_HAS_NO_LIFETIME_DSE)
  if(UT_HAS_NO_LIFETIME_DSE)
    target_compile_options(ut_common INTERFACE $<$<COMPILE_LANGUAGE:CXX>:-fno-lifetime-dse>)
  endif()
  # 32-bit stat fails with EOVERFLOW on large inode numbers (WSL drives, XFS) without 64-bit file offsets.
  target_compile_definitions(ut_common INTERFACE __LINUX_X86__ LINUX=1 __FILE_NOCASE=1 _FILE_OFFSET_BITS=64)
  find_package(Threads REQUIRED)
  target_link_libraries(ut_common INTERFACE dl Threads::Threads)
  if(CMAKE_SIZEOF_VOID_P EQUAL 4 AND NOT EMSCRIPTEN)
    # x87 math loads maps three times slower. MSVC uses SSE2 for Win32 too.
    target_compile_options(ut_common INTERFACE -msse2 -mfpmath=sse)
  endif()
  if(NOT ANDROID AND NOT EMSCRIPTEN)
    # Ubuntu's GCC turns these on by itself, other distributions do not. The NDK always does.
    target_compile_options(ut_common INTERFACE -fstack-protector-strong)
    target_compile_options(ut_common INTERFACE $<$<NOT:$<CONFIG:Debug>>:-U_FORTIFY_SOURCE;-D_FORTIFY_SOURCE=2>)
    target_link_options(ut_common INTERFACE -Wl,-z,relro,-z,now)
  endif()
endif()

# ut_module(<name> SHARED|EXECUTABLE|WIN32 [OUTPUT name] SOURCES ... [PCH hdr create.cpp] [NOPCH files...] [DEPS ...])
# Sources are relative to <Mod>/Src unless absolute.
function(ut_module name type)
  cmake_parse_arguments(A "" "OUTPUT" "SOURCES;PCH;NOPCH;DEPS" ${ARGN})
  list(TRANSFORM A_SOURCES PREPEND ${CMAKE_CURRENT_SOURCE_DIR}/Src/ REGEX "^[^/]")
  if(type STREQUAL "SHARED")
    string(TOUPPER ${name} up)
    if(UT_STATIC_LINK)
      # Static IMPLEMENT_PACKAGE defines G<Package>Package, so each module's GPackage stays apart.
      add_library(${name} STATIC ${A_SOURCES})
      set_property(GLOBAL APPEND PROPERTY UT_LINKED_PACKAGES ${name})
      target_compile_definitions(${name} PRIVATE ${up}_API= GPackage=G${name}Package)
    elseif(MSVC)
      add_library(${name} SHARED ${A_SOURCES})
      target_compile_definitions(${name} PRIVATE ${up}_API=__declspec\(dllexport\))
    else()
      add_library(${name} SHARED ${A_SOURCES})
      # Every module exports GPackage, ELF would bind all of them to the first one loaded.
      target_compile_definitions(${name} PRIVATE ${up}_API= GPackage=GPackage${name})
      set_target_properties(${name} PROPERTIES PREFIX "")
    endif()
  elseif(ANDROID)
    # SDLActivity loads the app as libmain.so and calls its SDL_main.
    add_library(${name} SHARED ${A_SOURCES})
    set(A_OUTPUT main)
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
  if(NOT type STREQUAL "SHARED")
    # Executables need the staged data next to them, also when built as single targets.
    add_dependencies(${name} ut_stage)
    # appGetDllHandle finds linked modules by their exported GLoaded<Package> symbol.
    set_target_properties(${name} PROPERTIES ENABLE_EXPORTS ${UT_STATIC_LINK})
  endif()
  if(MSVC AND NOT type STREQUAL "SHARED")
    # Garbage collection recurses through the object graph, x64 Debug overflows the 1 MB default.
    # 8 MB matches the Linux default.
    target_link_options(${name} PRIVATE /STACK:8388608)
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

find_package(ZLIB REQUIRED)
if(WIN32 OR ANDROID OR EMSCRIPTEN)
  find_package(Vorbis CONFIG REQUIRED)
  if(ANDROID)
    find_package(OpenAL CONFIG REQUIRED)
    find_library(GLES_TARGET GLESv3 REQUIRED)
    find_package(LibLZMA REQUIRED)
  elseif(WIN32)
    find_package(OpenAL CONFIG REQUIRED)
    find_package(unofficial-angle CONFIG REQUIRED)
    set(GLES_TARGET unofficial::angle::libGLESv2)
  else()
    # Emscripten links its own OpenAL on Web Audio (-lopenal), and WebGL sits behind the GLES3 headers.
    set(GLES_TARGET "")
  endif()
  find_package(unofficial-libsquish CONFIG REQUIRED)
  set(SQUISH_TARGET unofficial::libsquish::squish)
  # ports/speex patches in the voice formats retail clients use, see ports/speex/ut2004.patch.
  find_path(SPEEX_INCLUDE_DIR speex/speex.h REQUIRED)
  find_library(SPEEX_LIBRARY speex REQUIRED)
  find_library(SPEEXDSP_LIBRARY speexdsp REQUIRED)
  add_library(speex INTERFACE)
  target_include_directories(speex INTERFACE ${SPEEX_INCLUDE_DIR})
  target_link_libraries(speex INTERFACE ${SPEEX_LIBRARY} ${SPEEXDSP_LIBRARY})
else()
  find_path(OPENAL_INCLUDE_DIR AL/al.h REQUIRED)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(VORBISFILE REQUIRED IMPORTED_TARGET GLOBAL vorbisfile)
  add_library(Vorbis::vorbisfile ALIAS PkgConfig::VORBISFILE)
  pkg_check_modules(GLESV2 REQUIRED IMPORTED_TARGET GLOBAL glesv2)
  set(GLES_TARGET PkgConfig::GLESV2)
  # Ubuntu's libsquish.pc has an empty prefix and a wrong library name.
  find_path(SQUISH_INCLUDE_DIR squish.h REQUIRED)
  find_library(SQUISH_LIBRARY squish REQUIRED)
  add_library(squish UNKNOWN IMPORTED)
  set_target_properties(squish PROPERTIES IMPORTED_LOCATION ${SQUISH_LIBRARY} INTERFACE_INCLUDE_DIRECTORIES ${SQUISH_INCLUDE_DIR})
  set(SQUISH_TARGET squish)
  # No distribution ships the patched Speex, so it is built from the same source and patch as the vcpkg port.
  include(FetchContent)
  # A changed patch or build file gets a fresh source dir, since patching an already patched tree fails.
  file(SHA256 ${CMAKE_SOURCE_DIR}/ports/speex/ut2004.patch SPEEX_PATCH_HASH)
  file(SHA256 ${CMAKE_SOURCE_DIR}/ports/speex/CMakeLists.txt SPEEX_CMAKE_HASH)
  string(SHA256 SPEEX_HASH "${SPEEX_PATCH_HASH}${SPEEX_CMAKE_HASH}")
  string(SUBSTRING ${SPEEX_HASH} 0 12 SPEEX_HASH)
  FetchContent_Declare(speex
    SOURCE_DIR ${CMAKE_BINARY_DIR}/_deps/speex-src-${SPEEX_HASH}
    URL https://github.com/xiph/speex/archive/5dceaaf3e23ee7fd17c80cb5f02a838fd6c18e01.tar.gz
    URL_HASH SHA512=d03da906ec26ddcea2e1dc4157ac6dd056e1407381b0f37edd350552a02a7372e9108b4e39ae522f1b165be04b813ee11db0b47d17607e4dad18118b9041636b
    PATCH_COMMAND patch -p1 -i ${CMAKE_SOURCE_DIR}/ports/speex/ut2004.patch
          COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/ports/speex/CMakeLists.txt <SOURCE_DIR>)
  FetchContent_MakeAvailable(speex)
  pkg_check_modules(SPEEXDSP REQUIRED IMPORTED_TARGET GLOBAL speexdsp)
  target_link_libraries(speex PUBLIC PkgConfig::SPEEXDSP)
endif()
find_package(SDL3 3.2 CONFIG REQUIRED)
add_subdirectory(Tools/NVTriStrip)
# Static, but linked into Engine.so.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
add_subdirectory(metoolkit-karma)
if(WIN32 OR EMSCRIPTEN)
  unset(CMAKE_POSITION_INDEPENDENT_CODE)
else()
  # Kept on so the Linux executables are position independent on every distribution.
  include(CheckPIESupported)
  check_pie_supported()
endif()

set(UT_MODULES Core Engine Fire IpDrv UWeb Editor OpenGLDrv ALAudio
    XGame XInterface Onslaught SDLDrv SDLLaunch)
# Nothing built for Android or the web runs on the host, so the script packages come from a desktop build.
if(NOT ANDROID AND NOT EMSCRIPTEN)
  list(APPEND UT_MODULES UCC)
endif()
# UTV2004 has its own copies of IpDrv's net driver classes, so it only works as a loaded module.
if(NOT UT_STATIC_LINK)
  list(APPEND UT_MODULES UTV2004)
endif()
foreach(m ${UT_MODULES})
  add_subdirectory(${m})
endforeach()
if(ANDROID)
  add_subdirectory(Android)
  return()
endif()
if(EMSCRIPTEN)
  add_subdirectory(Emscripten)
  return()
endif()

# -NOH keeps the checked-in *Classes.h headers. The source path is relative because the engine's
# command line parser stops at spaces.
cmake_path(RELATIVE_PATH CMAKE_SOURCE_DIR BASE_DIRECTORY ${UT_SYSTEM_DIR} OUTPUT_VARIABLE UT_SOURCE_FROM_SYSTEM)
# Online servers reject packages that differ from retail, even by GUID, so retail copies are used
# and ucc make only compiles the packages left without one.
set(UT_RETAIL_DIR "$ENV{UT_RETAIL_DIR}" CACHE PATH "Installed UT2004 that provides the script packages")
file(GLOB UT_SCRIPT_SOURCES LIST_DIRECTORIES true RELATIVE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/*/Classes)
foreach(classes ${UT_SCRIPT_SOURCES})
  cmake_path(GET classes PARENT_PATH pkg)
  foreach(ext u ucl)
    if(EXISTS "${UT_RETAIL_DIR}/System/${pkg}.${ext}")
      list(APPEND UT_RETAIL_SCRIPTS "${UT_RETAIL_DIR}/System/${pkg}.${ext}")
    endif()
  endforeach()
endforeach()
if(UT_RETAIL_SCRIPTS)
  set(UT_COPY_RETAIL_SCRIPTS COMMAND ${CMAKE_COMMAND} -E copy_if_different ${UT_RETAIL_SCRIPTS} ${UT_SCRIPTS_DIR})
else()
  message(WARNING "No retail script packages in UT_RETAIL_DIR (${UT_RETAIL_DIR}), ucc_make compiles them all and online play only works with servers running this build")
endif()
add_custom_target(ucc_make
  ${UT_COPY_RETAIL_SCRIPTS}
  COMMAND $<TARGET_FILE:UCC> make -NOH -SOURCE=${UT_SOURCE_FROM_SYSTEM}
  WORKING_DIRECTORY ${UT_SYSTEM_DIR}
  DEPENDS ut_stage UCC Core Engine Editor IpDrv UWeb Fire XGame XInterface Onslaught $<TARGET_NAME_IF_EXISTS:UTV2004>
  USES_TERMINAL VERBATIM)

if(UT_ASSETS_DIR AND TARGET UCC)
  enable_testing()
  add_test(NAME server_smoke
    COMMAND ${CMAKE_COMMAND} -DUCC=$<TARGET_FILE:UCC> -DINI_DIR=${CMAKE_SOURCE_DIR}/DevIni -P ${CMAKE_SOURCE_DIR}/Tools/ServerSmoke.cmake
    WORKING_DIRECTORY ${UT_SYSTEM_DIR})
endif()

# cpack -G ZIP -C Release gives a folder a player can unpack and run. It is laid out like build/<preset>,
# because Default.ini reaches the script packages through ../../Scripts.
if(TARGET UCC)
  set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION UT2004/System)
  include(InstallRequiredSystemLibraries)
  install(DIRECTORY System Help KarmaData Web DESTINATION UT2004)
  install(DIRECTORY ${UT_SYSTEM_DIR}/ DESTINATION UT2004/System USE_SOURCE_PERMISSIONS
    FILES_MATCHING PATTERN "*.exe" PATTERN "*.dll" PATTERN "*.so" PATTERN "*-bin")
  install(DIRECTORY ${UT_SCRIPTS_DIR}/ DESTINATION Scripts FILES_MATCHING PATTERN "*.u" PATTERN "*.ucl")
  install(FILES README.md NOTICE.md LICENSE DESTINATION UT2004)
  set(CPACK_PACKAGE_NAME UT2004)
  set(CPACK_PACKAGE_FILE_NAME UT2004-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR})
  set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
  include(CPack)
endif()
