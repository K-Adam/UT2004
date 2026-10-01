# Packs the System and script files next to UT2004.js in build/web, the folder GitHub Pages serves.
find_package(Python3 3.8 REQUIRED COMPONENTS Interpreter)

add_custom_target(ut_web ALL
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/index.html ${CMAKE_BINARY_DIR}
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/SDLLaunch/Src/Unreal.ico ${CMAKE_BINARY_DIR}/favicon.ico
  COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/pack_system.py --out ${CMAKE_BINARY_DIR}
          --source ${CMAKE_SOURCE_DIR} --scripts ${UT_SCRIPTS_DIR}
          --packager ${EMSCRIPTEN_ROOT_PATH}/tools/file_packager.py
  DEPENDS SDLLaunch
  USES_TERMINAL VERBATIM)
