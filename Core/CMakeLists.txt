set(CORE_SRC Core.cpp UExporter.cpp UFactory.cpp UnAnsi.cpp UnArc.cpp UnBits.cpp UnCache.cpp
    UnClass.cpp UnCoreNative.cpp UnCoreNet.cpp UnCorSc.cpp UnLinker.cpp UnMath.cpp
    UnMem.cpp UnMisc.cpp UnName.cpp UnObj.cpp UnProp.cpp)
if(WIN32)
  if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    list(APPEND CORE_SRC UnLegacyCrt.cpp)
  endif()
  ut_module(Core SHARED
    SOURCES ${CORE_SRC} UnVcWin32.cpp
    PCH CorePrivate.h Core.cpp
    NOPCH UnAnsi.cpp UnVcWin32.cpp UnLegacyCrt.cpp
    DEPS Vorbis::vorbisfile winmm)
  if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    target_link_options(Core PRIVATE /EXPORT:fputs=LegacyFputs /EXPORT:fwrite=LegacyFwrite
      /EXPORT:fgets=LegacyFgets /EXPORT:__stdio_common_vfprintf=LegacyStdioCommonVfprintf)
  endif()
else()
  ut_module(Core SHARED SOURCES ${CORE_SRC} UnUnix.cpp UnGUID.cpp DEPS Vorbis::vorbisfile)
endif()
