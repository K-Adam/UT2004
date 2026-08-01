file(GLOB ENGINE_SRC RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}/Src CONFIGURE_DEPENDS Src/*.cpp)
list(REMOVE_ITEM ENGINE_SRC xTexShader.cpp)   # not in the original project

set(MEK ${CMAKE_SOURCE_DIR}/metoolkit/lib.rel/win32_single_msvcrt)
set(KARMA Mdt MdtKea MdtBcl Mst McdFrame McdCommon McdPrimitives McdConvex
    McdConvexCreateHull MeGlobals MeAssetFactory MeAssetDB MeAssetDBXMLIO MeXML)
list(TRANSFORM KARMA PREPEND ${MEK}/)
list(TRANSFORM KARMA APPEND .lib)

ut_module(Engine SHARED
  SOURCES ${ENGINE_SRC}
  PCH EnginePrivate.h AFileLog.cpp
  NOPCH UnCDKey.cpp UStreamInterface.cpp UExportCacheCommandlet.cpp xForceFeedback.cpp
  DEPS Core)
target_include_directories(Engine PRIVATE ${CMAKE_SOURCE_DIR}/IFC/Inc)
set_source_files_properties(Src/xForceFeedback.cpp TARGET_DIRECTORY Engine PROPERTIES
  INCLUDE_DIRECTORIES ${UT_DX8}/Inc COMPILE_DEFINITIONS DIRECTINPUT_VERSION=0x0800)
target_link_libraries(Engine PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/Lib/NvTriStrip.lib ${CMAKE_CURRENT_SOURCE_DIR}/Lib/s3tc.lib
  ${CMAKE_SOURCE_DIR}/IFC/Release/IFC23.lib
  ${KARMA})
