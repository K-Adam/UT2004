if(EMSCRIPTEN)
  # Browsers have no UDP, they join and host through their own WebRTC.
  set(IPDRV_WEBRTC WebRtcBrowser.cpp)
else()
  set(IPDRV_WEBRTC WebRtc.cpp)
endif()
ut_module(IpDrv SHARED
  SOURCES HTTPDownload.cpp InternetLink.cpp IpDrv.cpp MasterServerClient.cpp
          MasterServerUplink.cpp TcpLink.cpp TcpNetDriver.cpp UCompressCommandlet.cpp
          UdpLink.cpp UnIpDrvNative.cpp UnSocket.cpp WebMaster.cpp ${IPDRV_WEBRTC}
  PCH UnIpDrv.h IpDrv.cpp
  NOPCH WebMaster.cpp ${IPDRV_WEBRTC}
  DEPS Engine $<$<PLATFORM_ID:Windows>:wsock32>)
find_package(nlohmann_json CONFIG REQUIRED)
target_link_libraries(IpDrv PRIVATE nlohmann_json::nlohmann_json)
set_target_properties(IpDrv PROPERTIES CXX_STANDARD 17)
if(NOT EMSCRIPTEN)
  find_package(OpenSSL REQUIRED)
  find_package(CURL REQUIRED)
  if(WIN32 OR ANDROID)
    find_package(LibDataChannel CONFIG REQUIRED)
    set(IPDRV_DATACHANNEL LibDataChannel::LibDataChannel)
  else()
    # No distribution packages libdatachannel, so the release vcpkg uses is built static with the dependencies its submodules pin.
    include(FetchContent)
    set(BUILD_SHARED_LIBS OFF)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
    set(NO_MEDIA ON)
    set(NO_WEBSOCKET ON)
    set(NO_EXAMPLES ON)
    set(NO_TESTS ON)
    FetchContent_Declare(libdatachannel
      GIT_REPOSITORY https://github.com/paullouisageneau/libdatachannel.git
      GIT_TAG v0.24.6
      GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(libdatachannel)
    set(IPDRV_DATACHANNEL datachannel)
  endif()
  target_link_libraries(IpDrv PRIVATE ${IPDRV_DATACHANNEL} OpenSSL::Crypto CURL::libcurl)
endif()
