ut_module(UCC EXECUTABLE OUTPUT $<IF:$<PLATFORM_ID:Windows>,UCC,ucc-bin> SOURCES UCC.cpp DEPS Engine ZLIB::ZLIB)
if(UT_STATIC_LINK)
  target_link_libraries(UCC PRIVATE Fire IpDrv UWeb XGame XInterface Onslaught Editor)
  # UnNullRenderDevice.h includes EnginePrivate.h.
  target_include_directories(UCC PRIVATE ${CMAKE_SOURCE_DIR}/Engine/Src)
endif()
