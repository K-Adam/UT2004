/*=============================================================================
	ALAudio.h: Unreal OpenAL Audio public header file.
	Copyright 1997-2003 Epic Games, Inc. All Rights Reserved.

Revision history:
	* Created by Daniel Vogel.
	* Ported to Linux by Ryan C. Gordon.
=============================================================================*/

#ifndef _INC_ALAUDIO
#define _INC_ALAUDIO

/*----------------------------------------------------------------------------
	API.
----------------------------------------------------------------------------*/

#ifndef ALAUDIO_API
	#define ALAUDIO_API DLL_IMPORT
#endif

/*-----------------------------------------------------------------------------
	Dependencies.
-----------------------------------------------------------------------------*/

#ifdef WIN32
#pragma pack(push,8)
#include <windows.h>
#pragma pack(pop)
#endif

// Before Core.h, whose clock macro breaks the standard headers.
#include <atomic>
#include "Core.h"
#include "Engine.h"
#include "UnRender.h"

#define WITH_SPEEX 1

/*-----------------------------------------------------------------------------
	Audio public includes.
-----------------------------------------------------------------------------*/

#include "ALAudioStream.h"
#include "ALAudioCapture.h"

#if WITH_SPEEX
#include "ALAudioVoice.h"
#endif

#include "ALAudioSubsystem.h"
#include "UnNet.h"

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/

#endif

