if(EMSCRIPTEN)
  # Browsers have no UDP, the client joins through their own WebRTC.
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
if(WIN32 OR EMSCRIPTEN)
  find_package(nlohmann_json CONFIG REQUIRED)
  target_link_libraries(IpDrv PRIVATE nlohmann_json::nlohmann_json)
  target_compile_definitions(IpDrv PRIVATE WITH_WEBRTC)
  set_target_properties(IpDrv PROPERTIES CXX_STANDARD 17)
endif()
if(WIN32)
  # libdatachannel, OpenSSL and curl come from vcpkg only, Linux and Android build without WebRTC.
  find_package(LibDataChannel CONFIG REQUIRED)
  find_package(OpenSSL REQUIRED)
  find_package(CURL REQUIRED)
  target_link_libraries(IpDrv PRIVATE LibDataChannel::LibDataChannel OpenSSL::Crypto CURL::libcurl)
endif()
