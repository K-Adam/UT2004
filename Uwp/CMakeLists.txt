# Lays the app out in build/uwp/layout, which Windows can register as it is and makeappx packs for the console.
# The folders sit like in a build tree, Scripts beside the game folder, so Default.ini works unchanged.
set(UT_UWP_LAYOUT ${CMAKE_BINARY_DIR}/layout)
set(UT_UWP_GAME ${UT_UWP_LAYOUT}/Game)
if(UT_ASSETS_DIR)
  # Copied, because a packaged app cannot read through a link that leaves its folder.
  set(UT_UWP_CONTENT COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different ${UT_ASSETS_DIR}/src ${UT_UWP_GAME})
endif()
add_custom_target(ut_uwp ALL
  COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different ${CMAKE_SOURCE_DIR}/System ${UT_UWP_GAME}/System
  COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different ${UT_SCRIPTS_DIR} ${UT_UWP_LAYOUT}/Scripts
  COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:SDLLaunch> $<TARGET_FILE:SDL3::SDL3> $<TARGET_FILE:OpenAL::OpenAL>
          $<TARGET_FILE:unofficial::angle::libGLESv2> $<TARGET_FILE:unofficial::angle::libEGL> ${UT_UWP_GAME}/System
  COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different ${CMAKE_SOURCE_DIR}/Help ${UT_UWP_GAME}/Help
  COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different ${CMAKE_SOURCE_DIR}/KarmaData ${UT_UWP_GAME}/KarmaData
  COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different ${CMAKE_SOURCE_DIR}/Web ${UT_UWP_GAME}/Web
  ${UT_UWP_CONTENT}
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/AppxManifest.xml ${UT_UWP_LAYOUT}
  # The 48 pixel Android icon serves every tile and the splash.
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/Android/app/src/main/res/mipmap-mdpi/ic_launcher.png ${UT_UWP_LAYOUT}/Logo.png
  DEPENDS SDLLaunch
  USES_TERMINAL VERBATIM)

# The console only installs signed packages. UT_UWP_PFX is a certificate whose subject is the manifest's Publisher.
set(UT_UWP_PFX "" CACHE FILEPATH "Certificate that signs build/uwp/UT2004.appx")
if(UT_UWP_PFX)
  set(UT_UWP_KIT "${CMAKE_WINDOWS_KITS_10_DIR}/bin/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/x64")
  find_program(MAKEAPPX_EXECUTABLE makeappx HINTS ${UT_UWP_KIT} REQUIRED)
  find_program(SIGNTOOL_EXECUTABLE signtool HINTS ${UT_UWP_KIT} REQUIRED)
  add_custom_target(ut_uwp_package
    COMMAND ${MAKEAPPX_EXECUTABLE} pack /o /d ${UT_UWP_LAYOUT} /p ${CMAKE_BINARY_DIR}/UT2004.appx
    COMMAND ${SIGNTOOL_EXECUTABLE} sign /fd SHA256 /f ${UT_UWP_PFX} ${CMAKE_BINARY_DIR}/UT2004.appx
    DEPENDS ut_uwp
    USES_TERMINAL VERBATIM)
endif()
