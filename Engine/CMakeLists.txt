file(GLOB ENGINE_SRC RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}/Src CONFIGURE_DEPENDS Src/*.cpp)

ut_module(Engine SHARED
  SOURCES ${ENGINE_SRC}
  PCH EnginePrivate.h AFileLog.cpp
  NOPCH UnCDKey.cpp UStreamInterface.cpp UExportCacheCommandlet.cpp)
target_link_libraries(Engine PRIVATE nvtristrip karma::karma ${SQUISH_TARGET} PUBLIC Core)
