if(WIN32 OR EMSCRIPTEN)
  set(SDLLAUNCH_OUTPUT UT2004)
else()
  set(SDLLAUNCH_OUTPUT ut2004-bin)
endif()
ut_module(SDLLaunch WIN32 OUTPUT ${SDLLAUNCH_OUTPUT} SOURCES SDLLaunch.cpp DEPS Engine SDL3::SDL3 ZLIB::ZLIB)
if(UT_STATIC_LINK)
  target_link_libraries(SDLLaunch PRIVATE SDLDrv OpenGLDrv ALAudio Fire IpDrv UWeb XGame XInterface Onslaught Editor ${GLES_TARGET})
  # UnNullRenderDevice.h includes EnginePrivate.h.
  target_include_directories(SDLLaunch PRIVATE ${CMAKE_SOURCE_DIR}/Engine/Src ${CMAKE_SOURCE_DIR}/OpenGLDrv/Src)
endif()
if(WIN32)
  target_sources(SDLLaunch PRIVATE Src/SDLLaunch.rc)
  # A GUI subsystem exe that still enters through main.
  target_link_options(SDLLaunch PRIVATE /ENTRY:mainCRTStartup)
endif()
if(ANDROID)
  # System and script files ship xz compressed inside the APK, the content comes as a .tar.xz on first run.
  target_link_libraries(SDLLaunch PRIVATE LibLZMA::LibLZMA)
  # Read on every configure unless set with -D, so test builds can point at localhost.
  set(UT_ASSETS_ENV ${CMAKE_SOURCE_DIR}/Android/assets.env)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${UT_ASSETS_ENV})
  if(NOT UT_ASSETS_URL)
    file(STRINGS ${UT_ASSETS_ENV} UT_ASSETS_URL REGEX "^UT_ASSETS_URL=")
    string(REGEX REPLACE "^UT_ASSETS_URL=" "" UT_ASSETS_URL "${UT_ASSETS_URL}")
  endif()
  target_compile_definitions(SDLLaunch PRIVATE UT_ASSETS_URL="${UT_ASSETS_URL}")
endif()
if(EMSCRIPTEN)
  # appGetDllHandle can't dlsym in a browser, so it looks the linked packages up in this table.
  get_property(pkgs GLOBAL PROPERTY UT_LINKED_PACKAGES)
  set(decls "")
  set(checks "")
  foreach(pkg ${pkgs})
    string(APPEND decls "extern unsigned char GLoaded${pkg};\n")
    string(APPEND checks "\tif (!strcmp(Symbol, \"GLoaded${pkg}\")) return &GLoaded${pkg};\n")
  endforeach()
  file(CONFIGURE OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/LinkedPackages.cpp CONTENT
    "#include <string.h>\n${decls}extern \"C\" void* appLinkedPackage(const char* Symbol)\n{\n${checks}\treturn 0;\n}\n")
  target_sources(SDLLaunch PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/LinkedPackages.cpp)

  set(webrtc ${CMAKE_SOURCE_DIR}/Emscripten/webrtc.js)
  # Emscripten/index.html loads UT2004.js after the package loaders.
  set_target_properties(SDLLaunch PROPERTIES SUFFIX .js RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR} LINK_DEPENDS ${webrtc})
  target_link_options(SDLLaunch PRIVATE
    # Garbage collection and script recursion need far more than the 64 KB default.
    -sSTACK_SIZE=8MB
    -sALLOW_MEMORY_GROWTH -sINITIAL_MEMORY=512MB -sMAXIMUM_MEMORY=16GB
    # Without FULL_ES3 only what WebGL 2 offers natively links.
    -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2 -sGL_ENABLE_GET_PROC_ADDRESS
    -lopenal -lidbfs.js -sFETCH -sFORCE_FILESYSTEM -sEXIT_RUNTIME=0 -sENVIRONMENT=web
    -sEXPORTED_RUNTIME_METHODS=FS,IDBFS,addRunDependency,removeRunDependency
    "SHELL:--js-library ${webrtc}")
endif()
