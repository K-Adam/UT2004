/*=============================================================================
	SDLLaunchPrivate.h: Unreal launcher for Linux.
	Copyright 1999-2002 Epic Games, Inc. All Rights Reserved.

Revision history:
	* Created by Daniel Vogel (based on XLaunch).
=============================================================================*/

#ifdef WIN32
#pragma pack(push,8)
#include <windows.h>
#pragma pack(pop)
#endif
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/stat.h>

#include <malloc.h>

#include <fcntl.h>
#include "Engine.h"
#include "UnRender.h"

int RunEngine( TCHAR* CmdLine );

#ifdef __ANDROID__
// SDLLaunchAndroid.cpp
void InitSplash(const TCHAR *filename);
bool AndroidFail(const char* Fmt, const char* Arg);
void CloseSetupScreen();
bool ExtractAssets(const char* Root);
bool InstallContent(const char* Root);
void StartServerService(const TCHAR* Cmd);
#elif __EMSCRIPTEN__
// SDLLaunchWeb.cpp
extern FOutputDevice* GWebLogHook;
#endif

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
