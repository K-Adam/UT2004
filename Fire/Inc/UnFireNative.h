/*=============================================================================
	UnFireNative.h: Native function lookup table for static libraries.
	Copyright 2000 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Brandon Reinhart
=============================================================================*/

#ifndef UNFIRENATIVE_H
#define UNFIRENATIVE_H

#if __STATIC_LINK
// No native execs.

#define AUTO_INITIALIZE_REGISTRANTS_FIRE \
	UFractalTexture::StaticClass(); \
	UFireTexture::StaticClass(); \
	UWaterTexture::StaticClass(); \
	UWaveTexture::StaticClass(); \
	UFluidTexture::StaticClass(); \
	UWetTexture::StaticClass(); \
	UIceTexture::StaticClass();
#endif

#endif

