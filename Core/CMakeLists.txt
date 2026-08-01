ut_module(Core SHARED
  SOURCES Core.cpp UExporter.cpp UFactory.cpp UnAnsi.cpp UnArc.cpp UnBits.cpp UnCache.cpp
          UnClass.cpp UnCoreNative.cpp UnCoreNet.cpp UnCorSc.cpp UnLinker.cpp UnMath.cpp
          UnMem.cpp UnMisc.cpp UnName.cpp UnObj.cpp UnProp.cpp UnVcWin32.cpp
          UnLegacyCrt.cpp
  PCH CorePrivate.h Core.cpp
  NOPCH UnAnsi.cpp UnVcWin32.cpp UnLegacyCrt.cpp
  DEPS vorbisfile winmm)
