# WebRtcTest.cpp signs its STUN checks
find_package(OpenSSL REQUIRED)
ut_module(ut_tests EXECUTABLE SOURCES TestMain.cpp EngineTest.cpp WebRtcTest.cpp SecurityTest.cpp SemVerTest.cpp
  DEPS Engine ZLIB::ZLIB GTest::gtest OpenSSL::Crypto $<$<PLATFORM_ID:Windows>:ws2_32>)
# SemVer.h
target_include_directories(ut_tests PRIVATE ${CMAKE_SOURCE_DIR}/IpDrv/Src)
if(UT_STATIC_LINK)
  target_link_libraries(ut_tests PRIVATE Fire IpDrv UWeb XGame XInterface Onslaught Editor)
  # UnNullRenderDevice.h includes EnginePrivate.h.
  target_include_directories(ut_tests PRIVATE ${CMAKE_SOURCE_DIR}/Engine/Src)
endif()
