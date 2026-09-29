ut_module(IpDrv SHARED
  SOURCES HTTPDownload.cpp InternetLink.cpp IpDrv.cpp MasterServerClient.cpp
          MasterServerUplink.cpp TcpLink.cpp TcpNetDriver.cpp UCompressCommandlet.cpp
          UdpLink.cpp UnIpDrvNative.cpp UnSocket.cpp WebMaster.cpp WebRtc.cpp
  PCH UnIpDrv.h IpDrv.cpp
  NOPCH WebMaster.cpp WebRtc.cpp
  DEPS Engine $<$<PLATFORM_ID:Windows>:wsock32>)
if(WIN32)
  # libdatachannel, OpenSSL and nlohmann-json come from vcpkg only, other platforms build without WebRTC.
  find_package(LibDataChannel CONFIG REQUIRED)
  find_package(OpenSSL REQUIRED)
  find_package(nlohmann_json CONFIG REQUIRED)
  target_link_libraries(IpDrv PRIVATE LibDataChannel::LibDataChannel OpenSSL::Crypto nlohmann_json::nlohmann_json winhttp)
  target_compile_definitions(IpDrv PRIVATE WITH_WEBRTC)
  set_target_properties(IpDrv PROPERTIES CXX_STANDARD 17)
endif()
