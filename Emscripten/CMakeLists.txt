# Packs the System and script files next to UT2004.js in build/web, the folder GitHub Pages serves.
find_package(Python3 3.8 REQUIRED COMPONENTS Interpreter)

add_custom_target(ut_web ALL
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/index.html ${CMAKE_CURRENT_SOURCE_DIR}/ut2004.css ${CMAKE_CURRENT_SOURCE_DIR}/robots.txt ${CMAKE_CURRENT_SOURCE_DIR}/sitemap.xml ${CMAKE_BINARY_DIR}
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/SDLLaunch/Src/Unreal.ico ${CMAKE_BINARY_DIR}/favicon.ico
  COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/pack_system.py --out ${CMAKE_BINARY_DIR}
          --source ${CMAKE_SOURCE_DIR} --scripts ${UT_SCRIPTS_DIR}
          --packager ${EMSCRIPTEN_ROOT_PATH}/tools/file_packager.py
  # build/web also holds CMake's own files and the assets link, site has only what a host needs.
  COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/site
  COMMAND ${CMAKE_COMMAND} -E copy_if_different index.html ut2004.css robots.txt sitemap.xml favicon.ico UT2004.js UT2004.wasm system.data system.js ${CMAKE_BINARY_DIR}/site
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  DEPENDS SDLLaunch
  USES_TERMINAL VERBATIM)
