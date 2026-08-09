ut_module(OpenGLDrv SHARED
  SOURCES OpenGLDrv.cpp OpenGLMaterialState.cpp OpenGLRenderDevice.cpp
          OpenGLRenderInterface.cpp OpenGLResource.cpp
  PCH OpenGLDrv.h OpenGLDrv.cpp
  DEPS Engine)
target_link_libraries(OpenGLDrv PRIVATE SDL::SDL)
if(WIN32)
  # WinDrv viewports use WGL and never load SDL.dll, see UOpenGLRenderDevice::Init.
  target_link_libraries(OpenGLDrv PRIVATE delayimp)
  target_link_options(OpenGLDrv PRIVATE /DELAYLOAD:SDL.dll)
else()
  target_include_directories(OpenGLDrv PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Inc/linuxGL)
endif()
