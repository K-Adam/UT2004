# Built from source because the original prebuilt library is x86 only.
add_library(nvtristrip STATIC NvTriStrip.cpp NvTriStripObjects.cpp)
target_include_directories(nvtristrip PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(nvtristrip PROPERTIES POSITION_INDEPENDENT_CODE ON)
