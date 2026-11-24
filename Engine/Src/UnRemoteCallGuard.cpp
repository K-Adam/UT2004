#include "EnginePrivate.h"
#include "UnNet.h"

// Both reset the speed check, which only a newly possessed pawn or the first timeout may do. AcknowledgedPawn can't
// tell, a client sets it to anything first.
void ProcessClientCall( APawn*& MarginPawn, AActor* Actor, UFunction* Function, BYTE* Parms )
{
	guard(ProcessClientCall);

	static FName NAME_ServerAcknowledgePossession( TEXT("ServerAcknowledgePossession") );
	static FName NAME_ServerShortTimeout( TEXT("ServerShortTimeout") );

	APlayerController* PC = Cast<APlayerController>( Actor );
	UBOOL KeepMargin = 0;
	if( PC && Function->GetFName()==NAME_ServerAcknowledgePossession )
	{
		APawn* P = *(APawn**)Parms;
		KeepMargin = !P || P!=PC->Pawn || P==MarginPawn;
		if( !KeepMargin )
			MarginPawn = P;
	}
	else if( PC && Function->GetFName()==NAME_ServerShortTimeout )
		KeepMargin = PC->bShortConnectTimeOut;

	FLOAT TimeMargin = PC ? PC->TimeMargin : 0.f, MaxTimeMargin = PC ? PC->MaxTimeMargin : 0.f;
	Actor->ProcessEvent( Function, Parms );
	if( KeepMargin )
	{
		PC->TimeMargin = TimeMargin;
		PC->MaxTimeMargin = MaxTimeMargin;
	}

	unguard;
}

// The address is for admins, the chat manager only needs the hash.
void FilterRemoteCall( APlayerController* Receiver, UFunction* Function, BYTE* Parms )
{
	guard(FilterRemoteCall);

	static FName NAME_ClientReceiveBan( TEXT("ClientReceiveBan") );

	if( Function->GetFName()!=NAME_ClientReceiveBan )
		return;
	TFieldIterator<UProperty> First( Function );
	if( !First || !Cast<UStrProperty>(*First) || First->Offset!=0 )
		return;
	if( Receiver && Receiver->PlayerReplicationInfo && Receiver->PlayerReplicationInfo->bAdmin )
		return;

	FString& Info = *(FString*)Parms;
	const TCHAR Separator[] = { 27, 0 };
	INT Last = Info.InStr( Separator, 1 );
	if( Last!=INDEX_NONE && Last!=Info.InStr( Separator ) )
		Info = Info.Left( Last+1 ) + TEXT("0");

	unguard;
}
