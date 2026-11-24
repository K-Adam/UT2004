ut_module(IpDrv SHARED
  SOURCES AssetDownload.cpp HTTPDownload.cpp InternetLink.cpp IpDrv.cpp MasterServerClient.cpp
          MasterServerUplink.cpp TcpLink.cpp TcpNetDriver.cpp UCompressCommandlet.cpp
          UdpLink.cpp UnIpDrvNative.cpp UnSocket.cpp WebMaster.cpp WebRtc.cpp WebRtcCommon.cpp
  PCH UnIpDrv.h IpDrv.cpp
  NOPCH AssetDownload.cpp WebMaster.cpp WebRtc.cpp WebRtcCommon.cpp
  DEPS Engine $<$<PLATFORM_ID:Windows>:ws2_32>)
find_package(nlohmann_json CONFIG REQUIRED)
target_link_libraries(IpDrv PRIVATE nlohmann_json::nlohmann_json)
set(UT_UPDATE_URL "https://github.com/K-Adam/UT2004/releases" CACHE STRING "Where the update note in the server browser sends the player")
set_property(SOURCE Src/WebMaster.cpp APPEND PROPERTY COMPILE_DEFINITIONS UT_VERSION="${UT_VERSION}" UT_UPDATE_URL="${UT_UPDATE_URL}")
if(EMSCRIPTEN)
  # Browsers have no UDP, they join and host through their own WebRTC. This gives it libdatachannel's interface.
  # By hash so a moved branch cannot change what gets built.
  include(FetchContent)
  FetchContent_Declare(datachannel-wasm
    GIT_REPOSITORY https://github.com/K-Adam/datachannel-wasm.git
    GIT_TAG df96aaa369a4bee0bbed1da1e1fe0086aa1c3bc9)
  FetchContent_MakeAvailable(datachannel-wasm)
  target_link_libraries(IpDrv PRIVATE datachannel-wasm)
else()
  find_package(OpenSSL REQUIRED)
  find_package(CURL REQUIRED)
  if(VCPKG_TOOLCHAIN)
    find_package(LibDataChannel CONFIG REQUIRED)
    set(IPDRV_DATACHANNEL LibDataChannel::LibDataChannel)
  else()
    # No distribution packages libdatachannel, so the release vcpkg uses is built static with the dependencies its submodules pin.
    set(BUILD_SHARED_LIBS OFF)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
    set(NO_MEDIA ON)
    set(NO_EXAMPLES ON)
    set(NO_TESTS ON)
    # v0.24.7 as in ports/libdatachannel, by hash so a moved tag cannot change what gets built. libjuice is a submodule here.
    ut_fetch(libdatachannel https://github.com/paullouisageneau/libdatachannel.git 800e849a1ede5667caaf8f6caa1eb21e4906dd87
      PATCHES deps/libjuice ${CMAKE_SOURCE_DIR}/ports/libjuice/zero-tiebreaker.patch
              . ${CMAKE_SOURCE_DIR}/ports/libdatachannel/url-regex.patch
              . ${CMAKE_SOURCE_DIR}/ports/libdatachannel/ws-ca-pem.patch
              . ${CMAKE_SOURCE_DIR}/ports/libdatachannel/ws-windows-verify.patch)
    set(IPDRV_DATACHANNEL datachannel)
  endif()
  target_link_libraries(IpDrv PRIVATE ${IPDRV_DATACHANNEL} OpenSSL::Crypto CURL::libcurl)
endif()
