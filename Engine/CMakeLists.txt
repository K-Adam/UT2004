file(GLOB ENGINE_SRC RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}/Src CONFIGURE_DEPENDS Src/*.cpp)
list(REMOVE_ITEM ENGINE_SRC xTexShader.cpp)   # not in the original project

ut_module(Engine SHARED
  SOURCES ${ENGINE_SRC}
  PCH EnginePrivate.h AFileLog.cpp
  NOPCH UnCDKey.cpp UStreamInterface.cpp UExportCacheCommandlet.cpp xForceFeedback.cpp)
# x86 only prebuilt, the code skips it on Win64.
if(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 4)
  target_link_libraries(Engine PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Lib/s3tc.lib)
endif()
target_link_libraries(Engine PRIVATE nvtristrip karma::karma PUBLIC Core)
