# Engine free, so the launcher can download the content before the engine is up.
add_library(Http STATIC Src/HttpTransfer.cpp)
target_include_directories(Http PUBLIC Inc)
set_target_properties(Http PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(Http PRIVATE ut_common)
if(NOT EMSCRIPTEN)
  find_package(CURL REQUIRED)
  target_link_libraries(Http PRIVATE CURL::libcurl)
endif()
