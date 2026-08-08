/*=============================================================================
	UnComponents.h: Forward declarations of object components of actors
	Copyright 2001 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Steven Polge
=============================================================================*/

#ifndef _INC_UNCOMPONENTS
#define _INC_UNCOMPONENTS

class ENGINE_API APlayerController;

#pragma pack(push,4)

enum ETestMoveResult
{
	TESTMOVE_Stopped = 0,
	TESTMOVE_Moved = 1,
	TESTMOVE_Fell = 2,
	TESTMOVE_HitGoal = 5,
};

struct FMoverPosition
{
	BYTE PositionNumber;
	FVector StoppedLocation;
	FRotator StoppedRotation;
};

struct FCachedSound
{
	FName CacheName;
	class USound* CacheSound;
};

struct FFireProperties
{
	class UClass*  AmmoClass;
	class UClass*  ProjectileClass;
	FLOAT WarnTargetPct;
	FLOAT MaxRange;
	BITFIELD bTossed:1 GCC_PACK(4);
	BITFIELD bTrySplash:1;
	BITFIELD bLeadTarget:1;
	BITFIELD bInstantHit:1;
	BITFIELD bInitialized:1;
};
 
#pragma pack(pop)

#endif

