ut_module(IpDrv SHARED
  SOURCES HTTPDownload.cpp InternetLink.cpp IpDrv.cpp MasterServerClient.cpp
          MasterServerUplink.cpp TcpLink.cpp TcpNetDriver.cpp UCompressCommandlet.cpp
          UdpLink.cpp UnIpDrvNative.cpp UnSocket.cpp
  PCH UnIpDrv.h IpDrv.cpp
  DEPS Engine $<$<PLATFORM_ID:Windows>:wsock32>)
