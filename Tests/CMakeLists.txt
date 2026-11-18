ut_module(ut_tests EXECUTABLE SOURCES TestMain.cpp EngineTest.cpp DEPS Engine ZLIB::ZLIB GTest::gtest)
if(UT_STATIC_LINK)
  target_link_libraries(ut_tests PRIVATE Fire IpDrv UWeb XGame XInterface Onslaught Editor)
  # UnNullRenderDevice.h includes EnginePrivate.h.
  target_include_directories(ut_tests PRIVATE ${CMAKE_SOURCE_DIR}/Engine/Src)
endif()
