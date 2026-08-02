# OpenAL itself is loaded at runtime (ALFuncs.h), only its headers are needed.
ut_module(ALAudio SHARED
  SOURCES ALAudio.cpp ALAudioCapture.cpp ALAudioStream.cpp ALAudioSubsystem.cpp ALAudioVoice.cpp
  PCH ALAudioPrivate.h ALAudio.cpp
  DEPS Engine speex)
target_include_directories(ALAudio PRIVATE ${CMAKE_SOURCE_DIR}/OpenAL/Inc)
target_link_libraries(ALAudio PRIVATE dsound dxguid)
