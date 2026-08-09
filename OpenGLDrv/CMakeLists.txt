ut_module(OpenGLDrv SHARED
  SOURCES OpenGLDrv.cpp OpenGLMaterialState.cpp OpenGLRenderDevice.cpp
          OpenGLRenderInterface.cpp OpenGLResource.cpp
  PCH OpenGLDrv.h OpenGLDrv.cpp
  DEPS Engine)
if(NOT WIN32)
  target_include_directories(OpenGLDrv PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Inc/linuxGL)
  target_link_libraries(OpenGLDrv PRIVATE SDL::SDL)
endif()
