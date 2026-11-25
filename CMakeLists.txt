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
elseif(EMSCRIPTEN)
  if(NOT UT_STATIC_LINK OR NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "The web build is a single wasm32 module: cmake --preset web")
  endif()
  add_compile_options(-fwasm-exceptions)
  add_link_options(-fwasm-exceptions)
elseif(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR NOT CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|i.86|aarch64")
  message(FATAL_ERROR "Only Windows, x86, x64 or arm64 Linux, arm64 Android and the web are supported: cmake --preset win32|win64|linux64|linux32|android-arm64|web")
endif()
# Linux loads the modules as shared libraries, and Android links the static ones into libmain.so.
if(NOT WIN32 AND NOT EMSCRIPTEN)
  set(CMAKE_POSITION_INDEPENDENT_CODE ON)
  include(CheckPIESupported)
  check_pie_supported()
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
# The game rewrites the ini it runs on, so development runs get their own, written once from the defaults.
include(Tools/IniOverride.cmake)
if(NOT CMAKE_CROSSCOMPILING)
  if(NOT EXISTS ${UT_SYSTEM_DIR}/Dev.ini)
    ut_ini_write(${UT_SYSTEM_DIR}/Dev.ini ${CMAKE_SOURCE_DIR}/System/Default.ini ${CMAKE_SOURCE_DIR}/DevIni/DevOverrides.ini)
  endif()
  if(NOT EXISTS ${UT_SYSTEM_DIR}/DevUser.ini)
    ut_ini_write(${UT_SYSTEM_DIR}/DevUser.ini ${CMAKE_SOURCE_DIR}/System/DefUser.ini ${CMAKE_SOURCE_DIR}/DevIni/DevUserOverrides.ini)
  endif()
endif()
# Dev.ini and the web page served from build/web read the content through this link.
file(TO_CMAKE_PATH "$ENV{UT_ASSETS_DIR}" UT_ASSETS_ENV_DIR)
set(UT_ASSETS_DIR "${UT_ASSETS_ENV_DIR}" CACHE PATH "Content set for development runs, with the packages in src and the packed web files in build/web")
if(UT_ASSETS_DIR AND NOT ANDROID AND NOT EXISTS ${CMAKE_BINARY_DIR}/assets)
  if(CMAKE_HOST_WIN32)
    # A junction, because symbolic links need administrator rights on Windows.
    file(TO_NATIVE_PATH ${CMAKE_BINARY_DIR}/assets link)
    file(TO_NATIVE_PATH ${UT_ASSETS_DIR} target)
    # Drops a junction left dangling by a moved checkout.
    execute_process(COMMAND cmd /c rmdir ${link} OUTPUT_QUIET ERROR_QUIET)
    execute_process(COMMAND cmd /c mklink /J ${link} ${target} OUTPUT_QUIET COMMAND_ERROR_IS_FATAL ANY)
  else()
    file(CREATE_LINK ${UT_ASSETS_DIR} ${CMAKE_BINARY_DIR}/assets SYMBOLIC)
  endif()
endif()
# Copies rather than links, as the game writes CacheRecords.ucl next to the script packages and assets stays the only link.
set(UT_STAGE_DIRS "")
foreach(dir Help Web KarmaData Foliage HDTextures)
  # Older configures linked these, removing the link leaves its target alone.
  if(IS_SYMLINK ${CMAKE_BINARY_DIR}/${dir})
    if(CMAKE_HOST_WIN32)
      file(TO_NATIVE_PATH ${CMAKE_BINARY_DIR}/${dir} link)
      execute_process(COMMAND cmd /c rmdir ${link} COMMAND_ERROR_IS_FATAL ANY)
    else()
      file(REMOVE ${CMAKE_BINARY_DIR}/${dir})
    endif()
  endif()
  list(APPEND UT_STAGE_DIRS COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different ${CMAKE_SOURCE_DIR}/${dir} ${CMAKE_BINARY_DIR}/${dir})
endforeach()
add_custom_target(ut_stage ALL
  COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different ${CMAKE_SOURCE_DIR}/System ${UT_SYSTEM_DIR}
  COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different ${CMAKE_SOURCE_DIR}/Scripts ${UT_SCRIPTS_DIR}
  ${UT_STAGE_DIRS}
  VERBATIM)

option(UT_STATIC_LINK "Link the engine modules into the executables instead of loading them at runtime" OFF)

add_library(ut_common INTERFACE)
target_compile_features(ut_common INTERFACE cxx_std_17)
target_compile_definitions(ut_common INTERFACE
  UNICODE _UNICODE __STATIC_LINK=$<BOOL:${UT_STATIC_LINK}>
  DO_GUARD=1 DO_CHECK=1 DO_STAT=1 DO_CLOCK=1
  DO_GUARD_SLOW=0 DO_STAT_SLOW=0 DO_CLOCK_SLOW=0
  $<IF:$<CONFIG:Debug>,_DEBUG$<SEMICOLON>DEBUG$<SEMICOLON>DO_CHECK_SLOW=1,DO_CHECK_SLOW=0>)
target_link_libraries(ut_common INTERFACE karma_api)

if(MSVC)
  # The prebuilt static libs expect the release DLL CRT.
  set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
  set(CMAKE_MAP_IMPORTED_CONFIG_DEBUG Release)

  # Flags that reproduce the VS2003 semantics the code relies on.
  target_compile_options(ut_common INTERFACE
    $<$<COMPILE_LANGUAGE:CXX>:/Zc:wchar_t- /Zc:forScope /permissive /Zc:strictStrings- /EHa>
    /Zp4 /W3 /MP /guard:cf
    /wd4005 /wd4244 /wd4267 /wd4305 /wd4996 /wd4595 /wd4291 /wd4838 /wd4311 /wd4312 /wd4302)
  target_link_libraries(ut_common INTERFACE legacy_stdio_definitions)
  # Old prebuilt libs request VC6/7 CRT variants by name.
  target_link_options(ut_common INTERFACE /guard:cf $<$<EQUAL:${CMAKE_SIZEOF_VOID_P},4>:/SAFESEH:NO>
    /NODEFAULTLIB:libc.lib /NODEFAULTLIB:libcp.lib /NODEFAULTLIB:libcmt.lib
    /NODEFAULTLIB:libci.lib /NODEFAULTLIB:libcd.lib /NODEFAULTLIB:libcmtd.lib /NODEFAULTLIB:msvcrtd.lib)
else()
  # Packages store UTF-16, TCHAR must be 2 bytes like on Windows. Core supplies the wcs* functions,
  # and Clang would turn their loops back into calls to the C library's 4-byte wcslen.
  target_compile_options(ut_common INTERFACE
    -fshort-wchar -fno-builtin-wcslen -fsigned-char -fno-strict-aliasing -fno-delete-null-pointer-checks -w
    $<$<CXX_COMPILER_ID:GNU>:$<$<COMPILE_LANGUAGE:CXX>:-fpermissive>>
    # Like MSVC, template bodies that use classes declared later in the header are checked at instantiation.
    $<$<CXX_COMPILER_ID:Clang>:$<$<COMPILE_LANGUAGE:CXX>:-fdelayed-template-parsing>>)
  if(NOT ANDROID)
    # Ubuntu's GCC turns this on by itself, other distributions and Emscripten do not. The NDK always does.
    target_compile_options(ut_common INTERFACE -fstack-protector-strong)
  endif()
  # The engine calls destructors explicitly and reuses the object (FArray in UArrayProperty::DestroyValue),
  # so the stores a destructor makes must not be optimized away. Newer Clang does the same optimization and accepts the flag.
  include(CheckCXXCompilerFlag)
  check_cxx_compiler_flag(-fno-lifetime-dse UT_HAS_NO_LIFETIME_DSE)
  if(UT_HAS_NO_LIFETIME_DSE)
    target_compile_options(ut_common INTERFACE $<$<COMPILE_LANGUAGE:CXX>:-fno-lifetime-dse>)
  endif()
  include(CheckLinkerFlag)
  check_linker_flag(CXX LINKER:-z,relro,-z,now UT_HAS_RELRO)
  if(UT_HAS_RELRO)
    target_link_options(ut_common INTERFACE LINKER:-z,relro,-z,now)
  endif()
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "^i.86$")
    # x87 math loads maps three times slower. MSVC uses SSE2 for Win32 too.
    target_compile_options(ut_common INTERFACE -msse2 -mfpmath=sse)
  endif()
endif()
if(WIN32)
  target_compile_definitions(ut_common INTERFACE
    _WIN32_IE=0x0200 _WIN32_WINNT=0x0601 WIN32_LEAN_AND_MEAN
    _CRT_SECURE_NO_WARNINGS _CRT_NON_CONFORMING_SWPRINTFS _WINSOCK_DEPRECATED_NO_WARNINGS
    WINDOWS_IGNORE_PACKING_MISMATCH)  # original code includes windows.h under /Zp4 too
else()
  # Modules are dlopen()ed by bare name, so they and the executables look next to themselves.
  set(CMAKE_BUILD_RPATH "$ORIGIN")
  # 32-bit stat fails with EOVERFLOW on large inode numbers (WSL drives, XFS) without 64-bit file offsets.
  target_compile_definitions(ut_common INTERFACE __FILE_NOCASE=1 _FILE_OFFSET_BITS=64)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  # The NDK and Emscripten libraries have no glibc fortify checks.
  target_compile_options(ut_common INTERFACE $<$<NOT:$<CONFIG:Debug>>:-U_FORTIFY_SOURCE;-D_FORTIFY_SOURCE=2>)
endif()
find_package(Threads REQUIRED)
target_link_libraries(ut_common INTERFACE ${CMAKE_DL_LIBS} Threads::Threads)

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

# Builds a dependency from a fixed commit with the patches its vcpkg port applies, for where vcpkg is not used.
# PATCHES takes pairs of a subdirectory and a patch file, COPY files go into the source root.
# A changed patch or patch command gets a fresh source dir, since patching an already patched tree fails.
function(ut_fetch name repo commit)
  cmake_parse_arguments(A "" "" "PATCHES;COPY" ${ARGN})
  find_package(Git REQUIRED)
  set(hash ${commit})
  set(commands "")
  while(A_PATCHES)
    list(POP_FRONT A_PATCHES dir patch)
    file(SHA256 ${patch} h)
    string(APPEND hash ${h})
    list(APPEND commands COMMAND ${GIT_EXECUTABLE} -C ${dir} apply --whitespace=nowarn ${patch})
  endwhile()
  foreach(f ${A_COPY})
    file(SHA256 ${f} h)
    string(APPEND hash ${h})
    list(APPEND commands COMMAND ${CMAKE_COMMAND} -E copy ${f} .)
  endforeach()
  # PATCH_COMMAND takes its first command without the COMMAND keyword.
  list(POP_FRONT commands)
  string(SHA256 hash "${hash}${commands}")
  string(SUBSTRING ${hash} 0 12 hash)
  include(FetchContent)
  FetchContent_Declare(${name}
    SOURCE_DIR ${CMAKE_BINARY_DIR}/_deps/${name}-src-${hash}
    GIT_REPOSITORY ${repo}
    GIT_TAG ${commit}
    # A git source updates on every configure, which would patch the tree again and fail
    UPDATE_DISCONNECTED ON
    PATCH_COMMAND ${commands})
  FetchContent_MakeAvailable(${name})
endfunction()

find_package(ZLIB REQUIRED)
# OpenAL itself is loaded at runtime (ALFuncs.h), only its headers are needed.
find_path(OPENAL_INCLUDE_DIR AL/al.h REQUIRED)
if(VCPKG_TOOLCHAIN)
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
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(VORBISFILE REQUIRED IMPORTED_TARGET GLOBAL vorbisfile)
  add_library(Vorbis::vorbisfile ALIAS PkgConfig::VORBISFILE)
  pkg_check_modules(GLESV2 REQUIRED IMPORTED_TARGET GLOBAL glesv2)
  set(GLES_TARGET PkgConfig::GLESV2)
  # Ubuntu's libsquish.pc has an empty prefix and a wrong library name.
  # Sonames differ between distributions (libsquish.so.0, libsquish.so.0.0), so the archive is preferred.
  find_path(SQUISH_INCLUDE_DIR squish.h REQUIRED)
  find_library(SQUISH_LIBRARY NAMES libsquish.a squish REQUIRED)
  add_library(squish UNKNOWN IMPORTED)
  set_target_properties(squish PROPERTIES IMPORTED_LOCATION ${SQUISH_LIBRARY} INTERFACE_INCLUDE_DIRECTORIES ${SQUISH_INCLUDE_DIR})
  if(SQUISH_LIBRARY MATCHES "\\.a$")
    # Debian's archive is built with OpenMP.
    target_link_libraries(squish INTERFACE gomp)
  endif()
  set(SQUISH_TARGET squish)
  # No distribution ships the patched Speex, so it is built from the commit and patch of the vcpkg port.
  file(STRINGS ${CMAKE_SOURCE_DIR}/ports/speex/portfile.cmake SPEEX_REF REGEX "^ *REF [0-9a-f]+")
  if(NOT SPEEX_REF MATCHES "REF ([0-9a-f]+)")
    message(FATAL_ERROR "No REF commit found in ports/speex/portfile.cmake")
  endif()
  ut_fetch(speex https://github.com/xiph/speex.git ${CMAKE_MATCH_1}
    PATCHES . ${CMAKE_SOURCE_DIR}/ports/speex/ut2004.patch
    COPY ${CMAKE_SOURCE_DIR}/ports/speex/CMakeLists.txt)
  pkg_check_modules(SPEEXDSP REQUIRED IMPORTED_TARGET GLOBAL speexdsp)
  target_link_libraries(speex PUBLIC PkgConfig::SPEEXDSP)
endif()
find_package(SDL3 3.2 CONFIG REQUIRED)
add_subdirectory(Tools/NVTriStrip)
add_subdirectory(metoolkit-karma)

set(UT_MODULES Core Engine Fire IpDrv UWeb Editor OpenGLDrv ALAudio
    XGame XInterface Onslaught SDLDrv SDLLaunch)
# UCC is a tool for the build host, a cross build would not run it.
if(NOT CMAKE_CROSSCOMPILING)
  list(APPEND UT_MODULES UCC)
endif()
# StaticLink.h registers no UTV2004 classes, so it only works as a loaded module.
if(NOT UT_STATIC_LINK)
  list(APPEND UT_MODULES UTV2004)
endif()
# Read at configure time, so the log and the crash reports of a build name the commit it was configured at.
execute_process(COMMAND git describe --always --dirty WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE UT_PORT_VERSION OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
# Only a build of a release tag has a version to compare with the web master's, every other build is develop.
# The tag CI builds is named, as a release candidate and the release can be tags on one commit.
set(UT_VERSION develop)
set(UT_TAG "$ENV{GITHUB_REF_NAME}")
if("$ENV{GITHUB_REF_TYPE}" STREQUAL "tag")
  string(REGEX REPLACE "^v" "" UT_VERSION "${UT_TAG}")
else()
  execute_process(COMMAND git describe --tags --abbrev=0 --match v[0-9]* WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE UT_TAG OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
endif()
# Windows Installer versions are three numbers, so a tag like v1.0.0-rc.2 gives 1.0.0.
set(UT_MSI_VERSION 0.0.0)
if(UT_TAG MATCHES "^v([0-9]+\\.[0-9]+\\.[0-9]+)")
  set(UT_MSI_VERSION ${CMAKE_MATCH_1})
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
# Online servers reject packages that differ from retail, even by GUID, so Scripts/ tracks the retail
# packages and ucc make only compiles this port's own ones, which no server checks.
set(UT_OWN_SCRIPTS ${UT_SCRIPTS_DIR}/GUI2K4Fix.u ${UT_SCRIPTS_DIR}/XAdminFix.u)
set(UT_RETAIL_DIR "$ENV{UT_RETAIL_DIR}" CACHE PATH "Installed UT2004 that provides the content ucc_make loads")
if(EXISTS "${UT_RETAIL_DIR}/Textures")
  # The retail packages import sounds and meshes, so they only load with the install's content in reach.
  file(READ ${CMAKE_SOURCE_DIR}/System/Default.ini UT_MAKE_INI)
  foreach(dir Maps Textures Sounds Music StaticMeshes Animations)
    string(REPLACE "Paths=../${dir}/" "Paths=${UT_RETAIL_DIR}/${dir}/" UT_MAKE_INI "${UT_MAKE_INI}")
  endforeach()
  file(WRITE ${UT_SYSTEM_DIR}/Make.ini "${UT_MAKE_INI}")
  set(UT_MAKE_INI -INI=Make.ini)
endif()
# ucc make skips packages that exist, so the own ones are removed first and the result goes back into Scripts/.
add_custom_target(ucc_make
  COMMAND ${CMAKE_COMMAND} -E rm -f ${UT_OWN_SCRIPTS}
  COMMAND $<TARGET_FILE:UCC> make -NOH -SOURCE=${UT_SOURCE_FROM_SYSTEM} ${UT_MAKE_INI}
  COMMAND ${CMAKE_COMMAND} -E copy ${UT_OWN_SCRIPTS} ${CMAKE_SOURCE_DIR}/Scripts
  WORKING_DIRECTORY ${UT_SYSTEM_DIR}
  DEPENDS ut_stage UCC Core Engine Editor IpDrv UWeb Fire XGame XInterface Onslaught $<TARGET_NAME_IF_EXISTS:UTV2004>
  USES_TERMINAL VERBATIM)

if(TARGET UCC)
  enable_testing()
  # The first run's zip reader and path filter, built apart from the engine.
  add_executable(content_test SDLLaunch/Src/ContentTest.cpp SDLLaunch/Src/SDLLaunchContent.cpp)
  target_link_libraries(content_test PRIVATE SDL3::SDL3 ZLIB::ZLIB)
  # Outside System, which is what gets packaged.
  foreach(cfg "" _DEBUG _RELEASE _RELWITHDEBINFO _MINSIZEREL)
    set_target_properties(content_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY${cfg} ${CMAKE_BINARY_DIR}/Test)
  endforeach()
  add_test(NAME content
    COMMAND ${CMAKE_COMMAND} -DTEST=$<TARGET_FILE:content_test> -DDIR=${CMAKE_BINARY_DIR}/Test/content -P ${CMAKE_SOURCE_DIR}/Tools/ContentTest.cmake)
  set_tests_properties(content PROPERTIES ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${UT_SYSTEM_DIR}")
  add_test(NAME foliage_pack
    COMMAND ${CMAKE_COMMAND} -DDIR=${CMAKE_SOURCE_DIR}/Foliage -P ${CMAKE_SOURCE_DIR}/Tools/FoliagePack.cmake)
  find_package(GTest CONFIG QUIET)
  if(GTest_FOUND)
    add_subdirectory(Tests)
  endif()
endif()
if(UT_ASSETS_DIR AND TARGET UCC)
  add_test(NAME test_ini
    COMMAND ${CMAKE_COMMAND} -DSOURCE=${CMAKE_SOURCE_DIR} -P ${CMAKE_SOURCE_DIR}/Tools/TestIni.cmake
    WORKING_DIRECTORY ${UT_SYSTEM_DIR})
  set_tests_properties(test_ini PROPERTIES FIXTURES_SETUP ini)
  add_test(NAME server_smoke
    COMMAND ${CMAKE_COMMAND} -DUCC=$<TARGET_FILE:UCC> -P ${CMAKE_SOURCE_DIR}/Tools/ServerSmoke.cmake
    WORKING_DIRECTORY ${UT_SYSTEM_DIR})
  add_test(NAME package_sweep
    COMMAND ${CMAKE_COMMAND} -DUCC=$<TARGET_FILE:UCC> "-DPACKAGES=../../Scripts/*.u ../assets/src/Maps/*.ut2" -P ${CMAKE_SOURCE_DIR}/Tools/PackageSweep.cmake
    WORKING_DIRECTORY ${UT_SYSTEM_DIR})
  set(UT_SERVER_TESTS server_smoke package_sweep)
  if(TARGET ut_tests)
    add_test(NAME engine
      COMMAND ut_tests DM-Rankin?game=XGame.xDeathMatch?MinPlayers=4?QuickStart=true -INI=Smoke.ini -USERINI=SmokeUser.ini -nohomedir -multihome=127.0.0.1 -PORT=7800
      WORKING_DIRECTORY ${UT_SYSTEM_DIR})
    list(APPEND UT_SERVER_TESTS engine)
  endif()
  # They share the inis. Each server gets ports of its own, as on Linux a killed server holds its ports until its exit
  # is done, which can be after the next test started. They listen on loopback only, which keeps the Windows firewall
  # from asking about network access.
  set_tests_properties(${UT_SERVER_TESTS} PROPERTIES FIXTURES_REQUIRED ini RESOURCE_LOCK server)
endif()

# cpack -G ZIP -C Release gives the game folder a player unpacks and runs. It has no shared Scripts
# folder beside it, so the script packages and cache records go into System and its Default.ini looks there.
if(TARGET UCC)
  set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION System)
  # The physics submodule and the fetched libraries have install rules of their own, the archive only takes this component.
  set(CMAKE_INSTALL_DEFAULT_COMPONENT_NAME game)
  include(InstallRequiredSystemLibraries)
  install(DIRECTORY System Help KarmaData Web Foliage HDTextures DESTINATION .)
  install(DIRECTORY ${UT_SYSTEM_DIR}/ DESTINATION System USE_SOURCE_PERMISSIONS
    FILES_MATCHING PATTERN "*.exe" PATTERN "*.dll" PATTERN "*.so" PATTERN "*.so.*" PATTERN "*-bin"
    PATTERN "ut_tests*" EXCLUDE)
  install(DIRECTORY Scripts/ DESTINATION System)
  install(CODE [[
    set(ini "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/System/Default.ini")
    file(READ "${ini}" text)
    string(REGEX REPLACE "Paths=\\.\\./\\.\\./Scripts/[^\n]*\n" "" text "${text}")
    string(REPLACE "../../Scripts/" "../System/" text "${text}")
    file(WRITE "${ini}" "${text}")
  ]])
  install(FILES README.md NOTICE.md LICENSE DESTINATION .)
  if(NOT WIN32)
    # SDL 3 is not in the distributions yet, so the library the build used goes along.
    if(TARGET SDL3::SDL3-shared)
      install(IMPORTED_RUNTIME_ARTIFACTS SDL3::SDL3-shared DESTINATION System)
    endif()
    install(PROGRAMS Linux/install.sh DESTINATION .)
    install(FILES Android/app/src/main/res/mipmap-mdpi/ic_launcher.png DESTINATION . RENAME ut2004.png)
  endif()
  if(WIN32)
    # The installer, from the same files the archive gets. Needs the WiX .NET tool with its UI and Util extensions.
    find_program(WIX_EXECUTABLE wix)
    set(UT_MSI_DIR ${CMAKE_BINARY_DIR}/msi)
    add_custom_target(msi
      COMMAND ${CMAKE_COMMAND} -E rm -rf ${UT_MSI_DIR}
      COMMAND ${CMAKE_COMMAND} --install ${CMAKE_BINARY_DIR} --config $<CONFIG> --component game --prefix ${UT_MSI_DIR}/stage
      COMMAND ${CMAKE_COMMAND} -DSOURCE=${CMAKE_SOURCE_DIR} -DOUT=${UT_MSI_DIR}/License.rtf -P ${CMAKE_SOURCE_DIR}/Tools/LicenseRtf.cmake
      COMMAND ${WIX_EXECUTABLE} build -acceptEula wix7 -arch $<IF:$<EQUAL:${CMAKE_SIZEOF_VOID_P},8>,x64,x86>
        -ext WixToolset.UI.wixext -ext WixToolset.Util.wixext
        -d Stage=${UT_MSI_DIR}/stage -d Source=${CMAKE_SOURCE_DIR} -d LicenseRtf=${UT_MSI_DIR}/License.rtf -d Version=${UT_MSI_VERSION}
        -o ${CMAKE_BINARY_DIR}/UT2004.msi ${CMAKE_SOURCE_DIR}/Windows/UT2004.wxs
      DEPENDS SDLLaunch UCC Core Engine Editor IpDrv UWeb Fire XGame XInterface Onslaught SDLDrv OpenGLDrv ALAudio $<TARGET_NAME_IF_EXISTS:UTV2004>
      VERBATIM)
  endif()
  set(CPACK_PACKAGE_NAME UT2004)
  set(CPACK_PACKAGE_FILE_NAME UT2004-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR})
  set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
  set(CPACK_INSTALL_CMAKE_PROJECTS "${CMAKE_BINARY_DIR};UT2004;game;/")
  include(CPack)
endif()
