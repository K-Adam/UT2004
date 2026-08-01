ut_module(OpenGLDrv SHARED
  SOURCES OpenGLDrv.cpp OpenGLMaterialState.cpp OpenGLRenderDevice.cpp
          OpenGLRenderInterface.cpp OpenGLResource.cpp
  PCH OpenGLDrv.h OpenGLDrv.cpp
  DEPS Engine)
