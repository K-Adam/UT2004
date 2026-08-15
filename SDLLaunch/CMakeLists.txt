ut_module(SDLLaunch EXECUTABLE OUTPUT ut2004-bin SOURCES SDLLaunch.cpp DEPS Engine SDL3::SDL3 ZLIB::ZLIB)
if(WIN32)
  target_sources(SDLLaunch PRIVATE Src/SDLLaunch.rc)
endif()
