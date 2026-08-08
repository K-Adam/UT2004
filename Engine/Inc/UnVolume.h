/*=============================================================================
	UnVolume.h: Volumes.
	Copyright 1997-2000 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Warren Marshall
=============================================================================*/


#pragma pack(push,4)

struct FDecorationType
{
	class UStaticMesh*	StaticMesh;
	FRange				Count;
	FRange				DrawScale;
	UBOOL				bAlign;
	UBOOL				bRandomPitch;
	UBOOL				bRandomYaw;
	UBOOL				bRandomRoll;
};

#pragma pack(pop)

/*----------------------------------------------------------------------------
	The End.
----------------------------------------------------------------------------*/

