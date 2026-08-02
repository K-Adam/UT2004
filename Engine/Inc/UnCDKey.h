/*=============================================================================
	UnCDKey.h: CD key hashing for master server authentication
	Copyright 1997-2002 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Jack Porter
=============================================================================*/

/*-----------------------------------------------------------------------------
	Global CD Key functions
-----------------------------------------------------------------------------*/

FString ENGINE_API GetCDKeyHash();
FString ENGINE_API MangleKey(FString KeyHash, FString Key);
FString ENGINE_API UnmangleKey(FString KeyHash, FString Key);
FString ENGINE_API GetCDKeyResponse( const TCHAR* Challenge );
FString ENGINE_API EncryptWithCDKeyHash( const TCHAR* String, const TCHAR* HashAppend );
FString ENGINE_API DecryptWithCDKeyHash( const TCHAR* String, const TCHAR* HashAppend, const TCHAR* InCDKey );

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/

