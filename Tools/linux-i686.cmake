# 32-bit x86 Linux from a 64-bit host with the i386 multiarch packages. The host runs the result, so UCC and the tests stay.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR i686)
set(CMAKE_CROSSCOMPILING FALSE)
set(CMAKE_C_FLAGS_INIT -m32)
set(CMAKE_CXX_FLAGS_INIT -m32)
set(CMAKE_LIBRARY_ARCHITECTURE i386-linux-gnu)
set(ENV{PKG_CONFIG_LIBDIR} /usr/lib/i386-linux-gnu/pkgconfig:/usr/share/pkgconfig)
