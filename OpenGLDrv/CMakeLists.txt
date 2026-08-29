ut_module(OpenGLDrv SHARED
  SOURCES OpenGLDrv.cpp OpenGLMaterial.cpp OpenGLRenderDevice.cpp OpenGLRenderInterface.cpp OpenGLResource.cpp
  PCH OpenGLDrv.h OpenGLDrv.cpp
  DEPS Engine)
target_link_libraries(OpenGLDrv PRIVATE SDL3::SDL3 ${GLES_TARGET})
if(WIN32)
  # The engine loads OpenGLDrv at runtime and SDL loads libEGL by name, so linking copies neither DLL.
  add_custom_command(TARGET OpenGLDrv POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:OpenGLDrv> $<TARGET_FILE:unofficial::angle::libEGL> ${UT_SYSTEM_DIR}
    COMMAND_EXPAND_LISTS)
endif()
