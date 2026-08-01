# OpenAL itself is loaded at runtime (ALFuncs.h), only its headers are needed.
ut_module(ALAudio SHARED
  SOURCES ALAudio.cpp ALAudioCapture.cpp ALAudioStream.cpp ALAudioSubsystem.cpp ALAudioVoice.cpp
  PCH ALAudioPrivate.h ALAudio.cpp
  DEPS Engine speex vorbisfile)
target_include_directories(ALAudio PRIVATE ${CMAKE_SOURCE_DIR}/OpenAL/Inc ${UT_DX8}/Inc)
target_link_libraries(ALAudio PRIVATE ${UT_DX8}/Lib/dsound.lib dxguid)
