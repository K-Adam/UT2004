file(GLOB ENGINE_SRC RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}/Src CONFIGURE_DEPENDS Src/*.cpp)
list(REMOVE_ITEM ENGINE_SRC xTexShader.cpp)   # not in the original project

ut_module(Engine SHARED
  SOURCES ${ENGINE_SRC}
  PCH EnginePrivate.h AFileLog.cpp
  NOPCH UnCDKey.cpp UStreamInterface.cpp UExportCacheCommandlet.cpp xForceFeedback.cpp)
if(WIN32)
  target_include_directories(Engine PRIVATE ${CMAKE_SOURCE_DIR}/IFC/Inc)
  set_source_files_properties(Src/xForceFeedback.cpp TARGET_DIRECTORY Engine PROPERTIES
    COMPILE_DEFINITIONS DIRECTINPUT_VERSION=0x0800)
  # x86 only prebuilts, the code skips them on Win64.
  if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    target_link_libraries(Engine PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/Lib/s3tc.lib
      ${CMAKE_SOURCE_DIR}/IFC/Release/IFC23.lib)
  endif()
endif()
target_link_libraries(Engine PRIVATE nvtristrip karma::karma PUBLIC Core)
