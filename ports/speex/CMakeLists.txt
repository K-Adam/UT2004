cmake_minimum_required(VERSION 3.20)
project(speex C)

# The FFT and Vorbis psychoacoustics sources only serve the optional VORBIS_PSYCHO encoder.
file(GLOB SPEEX_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/libspeex/*.c)
list(FILTER SPEEX_SOURCES EXCLUDE REGEX "/(test[^/]*|fftwrap|kiss_fft[^/]*|smallft|vorbis_psy)\\.c$")

set(INCLUDE_STDINT "#include <stdint.h>")
set(SIZE16 int16_t)
set(USIZE16 uint16_t)
set(SIZE32 int32_t)
set(USIZE32 uint32_t)
configure_file(include/speex/speex_config_types.h.in ${CMAKE_CURRENT_BINARY_DIR}/speex_config_types.h @ONLY)

add_library(speex STATIC ${SPEEX_SOURCES})
target_include_directories(speex
  PUBLIC include ${CMAKE_CURRENT_BINARY_DIR}
  PRIVATE include/speex)
target_compile_definitions(speex PRIVATE FLOATING_POINT EXPORT= $<IF:$<C_COMPILER_ID:MSVC>,USE_ALLOCA,VAR_ARRAYS>)
set_target_properties(speex PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(NOT MSVC)
  target_link_libraries(speex PRIVATE m)
endif()

install(TARGETS speex ARCHIVE DESTINATION lib)
install(DIRECTORY include/speex DESTINATION include FILES_MATCHING PATTERN "*.h")
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/speex_config_types.h DESTINATION include/speex)
