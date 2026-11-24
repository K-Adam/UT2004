ut_module(ALAudio SHARED
  SOURCES ALAudio.cpp ALAudioCapture.cpp ALAudioStream.cpp ALAudioSubsystem.cpp ALAudioVoice.cpp
  PCH ALAudioPrivate.h ALAudio.cpp
  DEPS Engine speex)
target_include_directories(ALAudio PRIVATE ${OPENAL_INCLUDE_DIR})
if(WIN32)
  # Nothing links OpenAL, so it is not copied next to the executables on its own.
  add_custom_command(TARGET ALAudio POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:OpenAL::OpenAL> ${UT_SYSTEM_DIR})
endif()
