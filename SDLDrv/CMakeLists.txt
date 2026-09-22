ut_module(SDLDrv SHARED SOURCES SDLClient.cpp SDLDrv.cpp SDLViewport.cpp DEPS Engine SDL3::SDL3)
# GamepadMotion.hpp uses std::clamp.
set_target_properties(SDLDrv PROPERTIES CXX_STANDARD 17)
