ut_module(IpDrv SHARED
  SOURCES HTTPDownload.cpp InternetLink.cpp IpDrv.cpp MasterServerClient.cpp
          MasterServerUplink.cpp TcpLink.cpp TcpNetDriver.cpp UCompressCommandlet.cpp
          UdpLink.cpp UnIpDrvNative.cpp UnSocket.cpp WebRtc.cpp
  PCH UnIpDrv.h IpDrv.cpp
  NOPCH WebRtc.cpp
  DEPS Engine $<$<PLATFORM_ID:Windows>:wsock32>)
if(WIN32)
  # libdatachannel and OpenSSL come from vcpkg only, other platforms build without WebRTC.
  find_package(LibDataChannel CONFIG REQUIRED)
  find_package(OpenSSL REQUIRED)
  target_link_libraries(IpDrv PRIVATE LibDataChannel::LibDataChannel OpenSSL::Crypto)
  target_compile_definitions(IpDrv PRIVATE WITH_WEBRTC)
  set_target_properties(IpDrv PROPERTIES CXX_STANDARD 17)
endif()
