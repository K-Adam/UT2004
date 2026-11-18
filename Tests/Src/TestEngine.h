// gtest is built with the default packing and a native wchar_t, so it comes before the engine headers
// and no TCHAR string is handed to it.
#pragma pack(push,8)
#include <gtest/gtest.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#if WIN32
#include <windows.h>
#endif
#pragma pack(pop)

#include "Engine.h"
#if __STATIC_LINK
#if __UNIX__
#undef clock
#endif
#include "StaticLink.h"
#endif

// The dedicated server every test runs against, booted once because the engine cannot start twice in a process.
extern UGameEngine* GTestEngine;
