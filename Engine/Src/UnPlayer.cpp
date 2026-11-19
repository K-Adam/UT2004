/*=============================================================================
	UnActor.cpp: Actor list functions.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Tim Sweeney
=============================================================================*/

#include "EnginePrivate.h"

/*-----------------------------------------------------------------------------
	UPlayer object implementation.
-----------------------------------------------------------------------------*/

UPlayer::UPlayer()
{}
void UPlayer::Serialize( FArchive& Ar )
{
	guard(UPlayer::Serialize);
	Super::Serialize( Ar );
	unguard;
}
void UPlayer::Destroy()
{
	guard(UPlayer::Destroy);
	if( GIsRunning && Actor )
	{
		ULevel* Level = Actor->GetLevel();
		checkSlow(Level);
		Actor->Player = NULL;
		if( Level && Actor->RendMap != REN_Prefab && Actor->RendMap != REN_PrefabCompiled )		// !!hack - I don't know why this crashes when it's a REN_Prefab
			Level->DestroyActor( Actor, 1 );
		Actor = NULL;
	}
	Super::Destroy();
	unguard;
}

UBOOL UPlayer::Exec( const TCHAR* Cmd, FOutputDevice& Ar )
{
	guard(UPlayer::Exec);
	if ( !Actor )
		return 0;
	if( Actor->GetLevel()->Exec(Cmd,Ar) )
	{
		return 1;
	}
	if( Actor->Level && Actor->Level->Game && Actor->Level->Game->ScriptConsoleExec(Cmd,Ar,Actor->Pawn) )
	{
		return 1;
	}
	if( Actor->myHUD && Actor->myHUD->ScriptConsoleExec(Cmd,Ar,Actor->Pawn) )
	{
		return 1;
	}
	if( Actor->CheatManager && Actor->CheatManager->ScriptConsoleExec(Cmd,Ar,Actor->Pawn) )
	{
		return 1;
	}
	if( Actor->AdminManager && Actor->AdminManager->ScriptConsoleExec(Cmd, Ar, Actor->Pawn) )
	{
		return 1;
	}
	if( Actor->ScriptConsoleExec(Cmd,Ar,Actor->Pawn) )
	{
		return 1;
	}
	if( Actor->PlayerInput && Actor->PlayerInput->ScriptConsoleExec(Cmd,Ar,Actor->Pawn) )
	{
		return 1;
	}
	if( Actor->Pawn )
	{
		if( Actor->Pawn->ScriptConsoleExec(Cmd,Ar,Actor->Pawn) )
			return 1;
		if( Actor->Pawn->Weapon && Actor->Pawn->Weapon->ScriptConsoleExec(Cmd,Ar,Actor->Pawn) )
			return 1;
		if( Actor->Pawn->SelectedItem && Actor->Pawn->SelectedItem->ScriptConsoleExec(Cmd,Ar,Actor->Pawn) )
				return 1;
	}
	if( Actor->GetLevel()->Engine->Exec(Cmd,Ar) )
	{
		return 1;
	}

	for ( INT i = 0; i < LocalInteractions.Num(); i++ )
		if ( LocalInteractions(i) && LocalInteractions(i)->ScriptConsoleExec(Cmd,Ar,Actor->Pawn) )
			return 1;
	
	return 0;
	unguard;
}
IMPLEMENT_CLASS(UPlayer);

// Packages reach the native fields of the 32-bit retail Player through a text-assigned reference of their own
// Player subclass. Other builds have other fields there, so the reference gets a blank object of that subclass. Its
// zeroes match network play, where the engine refuses the debug render modes those fields hold.
class UPlayerStandIn : public UPlayer
{
	DECLARE_CLASS(UPlayerStandIn,UPlayer,CLASS_Transient,Engine)
	void ReadInput( FLOAT DeltaSeconds ) {}
	void Serialize( const TCHAR* V, EName Event ) {}
	using UPlayer::Serialize;
};
IMPLEMENT_CLASS(UPlayerStandIn);

static UObject* PlayerStandIn( UObject* Object, UClass* Wanted )
{
	UObject* Outer = Object->GetOuter();
	if( !Object->IsA(UPlayer::StaticClass()) || Wanted->GetSuperClass()!=UPlayer::StaticClass() || !Outer->IsA(Wanted->ClassWithin) )
		return NULL;
	FString Name = FString(Wanted->GetName()) + TEXT("StandIn");
	UObject* StandIn = UObject::StaticFindObject( Wanted, Outer, *Name );
	if( !StandIn )
	{
		// The class inherits Player's abstract flag, the stand-in brings the concrete native part.
		DWORD Flags = Wanted->ClassFlags;
		Wanted->ClassFlags &= ~CLASS_Abstract;
		StandIn = UObject::StaticAllocateObject( Wanted, Outer, FName(*Name), 0 );
		Wanted->ClassFlags = Flags;
		UPlayerStandIn::InternalConstructor( StandIn );
	}
	return StandIn;
}
static INT InstallPlayerStandIn = (GImportStandIn = PlayerStandIn, 0);

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/

