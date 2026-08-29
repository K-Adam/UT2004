set(CORE_SRC Core.cpp UExporter.cpp UFactory.cpp UnAnsi.cpp UnArc.cpp UnBits.cpp UnCache.cpp
    UnClass.cpp UnCoreNative.cpp UnCoreNet.cpp UnCorSc.cpp UnLinker.cpp UnMath.cpp
    UnMem.cpp UnMisc.cpp UnName.cpp UnObj.cpp UnProp.cpp)
if(WIN32)
  ut_module(Core SHARED
    SOURCES ${CORE_SRC} UnVcWin32.cpp
    PCH CorePrivate.h Core.cpp
    NOPCH UnAnsi.cpp UnVcWin32.cpp
    DEPS Vorbis::vorbisfile winmm shlwapi)
else()
  ut_module(Core SHARED SOURCES ${CORE_SRC} UnUnix.cpp UnGUID.cpp DEPS Vorbis::vorbisfile)
endif()
