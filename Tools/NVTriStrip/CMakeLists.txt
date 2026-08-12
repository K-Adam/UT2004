# Built from source because the original prebuilt library is x86 only.
add_library(nvtristrip STATIC NvTriStrip.cpp NvTriStripObjects.cpp)
target_include_directories(nvtristrip PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(nvtristrip PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
  # Engine.dll defines __stdio_common_vfprintf for Karma (Engine/Src/KarmaLegacyCrt.cpp), so printf
  # must reach it by plain name instead of importing the UCRT copy.
  target_compile_definitions(nvtristrip PRIVATE _NO_CRT_STDIO_INLINE)
endif()
