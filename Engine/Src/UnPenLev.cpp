/*=============================================================================
	UnPenLev.cpp: Unreal pending level class.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Tim Sweeney
=============================================================================*/

#include "EnginePrivate.h"
#include "UnNet.h"
#include "UnLinker.h"

/*-----------------------------------------------------------------------------
	UPendingLevel implementation.
-----------------------------------------------------------------------------*/

//
// Constructor.
//
UPendingLevel::UPendingLevel( UEngine* InEngine, const FURL& InURL )
:	ULevelBase( InEngine, InURL )
{}
IMPLEMENT_CLASS(UPendingLevel);

void UPendingLevel::ReceiveNextFile( UNetConnection* Connection, INT Attempt )
{
	guard(UPendingLevel::ReceiveNextFile);
	if( !FilesTotal )
		FilesTotal = FilesNeeded;
	for( INT i=0; i<Connection->PackageMap->List.Num(); i++ )
		if( Connection->PackageMap->List(i).PackageFlags & PKG_Need )
		{
			Connection->ReceiveFile( i, Attempt );
			Connection->Download->FileNumber = FilesTotal - FilesNeeded + 1;
			Connection->Download->FileCount = FilesTotal;
			return;
		}
		if( Connection->Download )
			delete Connection->Download;
		unguard;
}

UBOOL UPendingLevel::TrySkipFile()
{
	guard(UPendingLevel::TrySkipFile);
	return NetDriver->ServerConnection->Download && NetDriver->ServerConnection->Download->TrySkipFile();
	unguard;
}
void UPendingLevel::NotifyReceivedFile( UNetConnection* Connection, INT PackageIndex, const TCHAR* InError, UBOOL Skipped, INT Attempt )
{
	guard(UPendingLevel::NotifyReceivedFile);
	check(Connection->PackageMap->List.IsValidIndex(PackageIndex));

	// Map pack to package.
	FPackageInfo& Info = Connection->PackageMap->List(PackageIndex);
	// A cut down copy that was to be replaced is still there and goes on being used. Once the client's own sources
	// have failed, the server has nothing better than a copy of the size it reports.
	TCHAR Filename[256];
	UBOOL Cached = *InError && appFindPackageFile( Info.Parent->GetName(), &Info.Guid, Filename, Info.RemoteGeneration );
	UBOOL ServerHasNoBetter = Cached && (Attempt+1) >= Connection->ClientDownloads && GFileManager->FileSize( Filename ) >= Info.FileSize;
	if( *InError && (Attempt+1) < Connection->DownloadInfo.Num() && !ServerHasNoBetter )
	{
		// Try with the next download method.
		ReceiveNextFile( Connection, Attempt+1 );
	}
	else if( *InError && !Cached )
	{
		// If transfer failed, so propagate error.
		if( Error==TEXT("") )
			Error = FString::Printf( LocalizeError(TEXT("DownloadFailed"),TEXT("Engine")), Info.Parent->GetName(), InError );
	}
	else
	{
		// Now that a file has been successfully received, mark its package as downloaded.		
		check(Info.PackageFlags&PKG_Need);
		Info.PackageFlags &= ~PKG_Need;
		FilesNeeded--;
		if( Skipped )
			Connection->PackageMap->List.Remove( PackageIndex );

		// Send next download request.
		ReceiveNextFile( Connection, 0 );
	}
	unguard;
}

UBOOL UPendingLevel::LinkerInUse( ULinkerLoad* Linker, ULevel* Level )
{
	guard(UPendingLevel::LinkerInUse);
	if( Level && Linker->LinkerRoot==Level->GetOuter() )
		return 1;
	for( INT i=0; i<Linker->ExportMap.Num(); i++ )
		if( Linker->ExportMap(i).ClassIndex==0 && Linker->ExportMap(i).ObjectName!=NAME_None )
			return 1;
	return 0;
	unguard;
}

// Lets go of a package's file so that a download can replace it.
static UBOOL ReleasePackage( UObject* Package, ULevel* Level )
{
	TArray<UObject*> Loaders = UObject::GetLoaderList();
	for( INT i=0; i<Loaders.Num(); i++ )
	{
		ULinkerLoad* Linker = (ULinkerLoad*)Loaders(i);
		if( Linker->LinkerRoot==Package )
		{
			if( UPendingLevel::LinkerInUse( Linker, Level ) )
				return 0;
			UObject::ResetLoaders( Package, 0, 1 );
			break;
		}
	}
	return 1;
}

/*-----------------------------------------------------------------------------
	UNetPendingLevel implementation.
-----------------------------------------------------------------------------*/

//
// Constructor.
//
UNetPendingLevel::UNetPendingLevel( UEngine* InEngine, const FURL& InURL )
:	UPendingLevel( InEngine, InURL )
{
	guard(UNetPendingLevel::UNetPendingLevel);

	// Init.
	Error     = TEXT("");
	NetDriver = NULL;

	// Try to create network driver.
	UClass* NetDriverClass = StaticLoadClass( UNetDriver::StaticClass(), NULL, TEXT("ini:Engine.Engine.NetworkDevice"), NULL, LOAD_NoFail, NULL );
	NetDriver = ConstructObject<UNetDriver>( NetDriverClass );
	if( NetDriver->InitConnect( this, URL, Error ) )
	{
		// Send initial message.
		NetDriver->ServerConnection->Logf( TEXT("HELLO REVISION=0 MINVER=%i VER=%i"), ENGINE_MIN_NET_VERSION, ENGINE_VERSION);
		NetDriver->ServerConnection->FlushNet();
	}
	else
	{
		delete NetDriver;
		NetDriver=NULL;
	}

	unguard;
}

//
// FNetworkNotify interface.
//
EAcceptConnection UNetPendingLevel::NotifyAcceptingConnection()
{
	guard(UNetPendingLevel::NotifyAcceptingConnection);
	return ACCEPTC_Reject;
	unguard;
}
void UNetPendingLevel::NotifyAcceptedConnection( class UNetConnection* Connection )
{
	guard(UNetPendingLevel::NotifyAcceptedConnection);
	unguard;
}
UBOOL UNetPendingLevel::NotifyAcceptingChannel( class UChannel* Channel )
{
	guard(UNetPendingLevel::NotifyAcceptingChannel);
	return 0;
	unguard;
}
ULevel* UNetPendingLevel::NotifyGetLevel()
{
	guard(UNetPendingLevel::NotifyGetLevel);
	return NULL;
	unguard;
}
void UNetPendingLevel::NotifyReceivedText( UNetConnection* Connection, const TCHAR* Text )
{
	guard(UNetPendingLevel::NotifyReceivedText);
	check(Connection==NetDriver->ServerConnection);

	UGameEngine* GameEngine = Cast<UGameEngine>( Engine );	// Needed for errors

	// This client got a response from the server.
	if( ParseCommand(&Text,TEXT("UPGRADE")) )
	{
		// Report mismatch.
		INT RemoteMinVer=0, RemoteVer=0;
		Parse( Text, TEXT("MINVER="), RemoteMinVer );
		Parse( Text, TEXT("VER="),    RemoteVer    );
		if( ENGINE_VERSION < RemoteMinVer )
		{
			// Upgrade message.
			Engine->SetProgress( *FString::Printf(TEXT("menu:%s"),*GameEngine->DisconnectMenuClass),LocalizeError(TEXT("ConnectionFailed"),TEXT("Engine")), LocalizeError(TEXT("ClientOutdated"),TEXT("Engine")) ); // gam
		}
		else
		{
			// Downgrade message.
			Engine->SetProgress( *FString::Printf(TEXT("menu:%s"),*GameEngine->DisconnectMenuClass), LocalizeError(TEXT("ConnectionFailed"),TEXT("Engine")), LocalizeError(TEXT("ServerOutdated"),TEXT("Engine")) );
		}
	}
	else if( ParseCommand(&Text,TEXT("FAILCODE")) )
	{

		GWarn->Logf(TEXT("FailCode %s"),Text);

		// Notify console.
		if( Engine->Client &&
			Engine->Client->Viewports(0) &&
			Engine->Client->Viewports(0)->Console )
		{
			FURL NewURL(URL);

	        for( INT i=NewURL.Op.Num()-1; i>=0; i-- )
	        {
		        if( !appStrPrefix( *NewURL.Op(i), TEXT("PASSWORD=") ) )
		        {
			        NewURL.Op.Remove( i );
			        break;
			    }
	        }
			CastChecked<UConsole>(Engine->Client->Viewports(0)->Console)->eventConnectFailure( Text, *NewURL.String() );
		}
	}
	else if( ParseCommand( &Text, TEXT("USES") ) )
	{
		// A running download points into the list, which growing would reallocate.
		if( Connection->Download )
			return;
		// Dependency information.
		FPackageInfo& Info = *new(Connection->PackageMap->List)FPackageInfo(NULL);
		TCHAR PackageName[NAME_SIZE]=TEXT("");
		Parse( Text, TEXT("GUID=" ), Info.Guid );
		Parse( Text, TEXT("GEN=" ),  Info.RemoteGeneration );
		Parse( Text, TEXT("SIZE="),  Info.FileSize );
		Info.DownloadSize = Info.FileSize;
		Parse( Text, TEXT("FLAGS="), Info.PackageFlags );
		Parse( Text, TEXT("PKG="), PackageName, ARRAY_COUNT(PackageName) );
		Parse( Text, TEXT("FNAME="), Info.URL );
		guard(CreateUsesPackage);
		Info.Parent = CreatePackage(NULL,PackageName);
		unguardf((TEXT("PackageName=(%s) Text=(%s)"),PackageName,Text));
	}
	else if( ParseCommand(&Text,TEXT("USERFLAG")) )
	{
		Connection->UserFlags = appAtoi(Text);
	}
	else if( ParseCommand( &Text, TEXT("CHALLENGE") ) )
	{
		// Challenged by server.
		INT RemoteStats = 0;
		INT Sec=1;
		INT Gozer;
		Parse( Text, TEXT("VER="), Connection->NegotiatedVer );
		Parse( Text, TEXT("CHALLENGE="), Connection->Challenge );
		Parse( Text, TEXT("STATS="), RemoteStats );
		Parse( Text, TEXT("SEC="),Sec);
		Parse( Text, TEXT("GZ="),Gozer);

		Connection->Gozer = Gozer;

		GameEngine->bCheatProtection = UBOOL(Sec);

		// Encode the Global MD5 for shipping.

		BYTE EncBuffer[66];
		BYTE MD5Buffer[16];

		appMemcpy(&MD5Buffer,&GMD5,16);

		for (INT i=0;i<64;i++)
			EncBuffer[i] = BYTE(appFrand()*256);

		BYTE MD5Encrypt = BYTE(appFrand()*256);
		BYTE MD5StartPos = BYTE(appFrand()*32);
		
		EncBuffer[64] = MD5Encrypt;
		EncBuffer[65] = MD5StartPos;
		
		INT Pos=MD5StartPos;
		for (INT j=0;j<16;j++)
		{
			BYTE b = MD5Buffer[j] ^ (MD5Encrypt++);
			EncBuffer[Pos] = (EncBuffer[Pos++] & 0x0F) + (b & 0xF0);
			EncBuffer[Pos] = (EncBuffer[Pos++] & 0xF0) + (b & 0x0F);
		}

		FString GlobalMD5, StatsLogin;
		for (INT i=0; i<66; i++)
			GlobalMD5 += FString::Printf(TEXT("%02x"), EncBuffer[i]);	

		// Send the hash of our CDKEY, our stats username and our encrypted stats password.
		APlayerController* P = Engine->Client->Viewports(0)->Actor;

		if( RemoteStats==1 && P->bEnableStatsTracking && P->StatsUsername.Len() > 0 && P->StatsPassword.Len() > 0 )
			NetDriver->ServerConnection->Logf( TEXT("AUTH HASH=%s GM=%s USERNAME=%s PASSWORD=%s"), *GetCDKeyHash(), *GlobalMD5, *P->StatsUsername, *EncryptWithCDKeyHash( *P->StatsPassword, TEXT("STATS") ) );
		else
			NetDriver->ServerConnection->Logf( TEXT("AUTH HASH=%s GM=%s USERNAME= PASSWORD="), *GetCDKeyHash(), *GlobalMD5 );

		FURL PartialURL(URL);
		PartialURL.Host = TEXT("");
		// The WebRTC details only tell this client how to reach the server, and the server reads the
		// login URL up to the first comma, which would cut off every option after the fingerprint.
		for( INT i=PartialURL.Op.Num()-1; i>=0; i-- )
			if( appStrnicmp(*PartialURL.Op(i),TEXT("WebRTC="),7)==0 || appStrnicmp(*PartialURL.Op(i),TEXT("WebOnly="),8)==0 )
				PartialURL.Op.Remove( i );

	    for( INT i=PartialURL.Op.Num()-1; i>=0; i-- )
	    {
		    if( !appStrPrefix( *PartialURL.Op(i), TEXT("game=") ) )
		    {
			    PartialURL.Op.Remove( i );
			    break;
			}
	    }
				
		NetDriver->ServerConnection->Logf( TEXT("NETSPEED %i"), Connection->CurrentNetSpeed );
		NetDriver->ServerConnection->Logf( TEXT("LOGIN RESPONSE=%i URL=%s"), Engine->ChallengeResponse(Connection->Challenge), *PartialURL.String() );
		NetDriver->ServerConnection->FlushNet();
	}
	else if( ParseCommand( &Text, TEXT("DLMGR") ) )
	{
		INT i = Connection->DownloadInfo.AddZeroed();
		Parse( Text, TEXT("CLASS="), Connection->DownloadInfo(i).ClassName );
		Parse( Text, TEXT("PARAMS="), Connection->DownloadInfo(i).Params );
		ParseUBOOL( Text, TEXT("COMPRESSION="), Connection->DownloadInfo(i).Compression );
		Connection->DownloadInfo(i).Class = StaticLoadClass( UDownload::StaticClass(), NULL, *Connection->DownloadInfo(i).ClassName, NULL, LOAD_NoWarn | LOAD_Quiet, NULL );
		if( !Connection->DownloadInfo(i).Class )
			Connection->DownloadInfo.Remove(i);
	}
	else if( ParseCommand( &Text, TEXT("WELCOME") ) )
	{
		// Server accepted connection.

		// Parse welcome message.
		Parse( Text, TEXT("LEVEL="), URL.Map );
		ParseUBOOL( Text, TEXT("LONE="), LonePlayer );
		Parse( Text, TEXT("CHALLENGE="), Connection->Challenge );

		FString gametype;
		Parse( Text, TEXT("GAME="), gametype);
		URL.AddOption( *FString::Printf(TEXT("GAME=%s"),*gametype) );
		// Travel on this server starts from this URL. A join over WebRTC stays on it. One over UDP drops the WebRTC details,
		// or a server that answers nothing while it changes its level would look filtered and move the player to a WebRTC port that may be closed.
		if( NetDriver->ServerConnection->URL.GetOption(TEXT("WebOnly="), NULL) )
			URL.AddOption( TEXT("WebOnly=1") );
		else
			for( INT i=URL.Op.Num()-1; i>=0; i-- )
				if( appStrnicmp(*URL.Op(i),TEXT("WebRTC="),7)==0 )
					URL.Op.Remove( i );

		// A server that names no download method still has the channel. The client's own sources of stock packages
		// leave a package to a server that claims the retail file, and get a second turn in case that fails.
		if( Connection->DownloadInfo.Num()==Connection->ClientDownloads )
		{
			FDownloadInfo& Channel = Connection->DownloadInfo( Connection->DownloadInfo.AddZeroed() );
			Channel.Class = UChannelDownload::StaticClass();
			Channel.ClassName = TEXT("Engine.ChannelDownload");
		}
		for( INT i=NetDriver->ClientRedirectURLs.Num(); i<Connection->ClientDownloads; i++ )
		{
			// AddItem would assign into raw memory, and the copy has to exist before the array grows.
			FDownloadInfo Again = Connection->DownloadInfo(i);
			new(Connection->DownloadInfo)FDownloadInfo( Again );
		}

		// Make sure all packages we need are downloadable.
		for( INT i=0; i<Connection->PackageMap->List.Num(); i++ )
		{
			TCHAR Filename[256];
			FPackageInfo& Info = Connection->PackageMap->List(i);
			UBOOL Found = appFindPackageFile( Info.Parent->GetName(), &Info.Guid, Filename, Info.RemoteGeneration );

			// A cached stock package below the retail size has data cut out, so it is fetched again where a full copy can be had.
			INT StockSize;
			FString StockMD5, StockPath;
			if( Found && NetDriver->AllowDownloads && (Info.PackageFlags & PKG_AllowDownload)
			&&	!appStrnicmp( Filename, *GSys->CachePath, GSys->CachePath.Len() )
			&&	UDownload::StockPackage( Info.Guid, StockSize, StockMD5, StockPath )
			&&	GFileManager->FileSize( Filename )<StockSize
			&&	(Connection->ClientDownloads>NetDriver->ClientRedirectURLs.Num() || Info.FileSize==StockSize)
			&&	ReleasePackage( Info.Parent, GameEngine->GLevel ) )
				Found = 0;
			if( !Found )
			{
				appSprintf( Filename, TEXT("%s%s"), Info.Parent->GetName(), DLLEXT );
				if( GFileManager->FileSize(Filename) <= 0 )
				{
					// We need to download this package.
					FilesNeeded++;
					Info.PackageFlags |= PKG_Need;

					if( !NetDriver->AllowDownloads || !(Info.PackageFlags & PKG_AllowDownload) )
					{
						Error = FString::Printf( LocalizeError(TEXT("DownloadNotAllowed"),TEXT("Engine")), Info.Parent->GetName() );
						NetDriver->ServerConnection->State = USOCK_Closed;
						return;
					}
				}
			}
		}

		// Send first download request.
		ReceiveNextFile( Connection, 0 );
		// We have successfully connected.
		Success = 1;
	}
	else
	{
		// Other command.
	}
	unguard;
}

UBOOL UNetPendingLevel::NotifySendingFile( UNetConnection* Connection, FGuid Guid )
{
	guard(UNetPendingLevel::NotifySendingFile);

	// Server has requested a file from this client.
	debugf( NAME_DevNet, LocalizeError(TEXT("RequestDenied"),TEXT("Engine")) );
	return 0;

	unguard;
}

//
// Update the pending level's status.
//
void UNetPendingLevel::Tick( FLOAT DeltaTime )
{
	guard(UNetPendingLevel::Tick);
	check(NetDriver);
	check(NetDriver->ServerConnection);

	// Handle timed out or failed connection.
	if( NetDriver->ServerConnection->State==USOCK_Closed && Error==TEXT("") )
	{
		Error = LocalizeError(TEXT("ConnectionFailed"),TEXT("Engine"));
		return;
	}

	// Update network driver.
	NetDriver->TickDispatch( DeltaTime );
	NetDriver->TickFlush();

	unguard;
}
//
// Send JOIN to other end
//
void UNetPendingLevel::SendJoin()
{
	guard(UNetPendingLevel::SendJoin);
	SentJoin = 1;
	NetDriver->ServerConnection->Logf( TEXT("JOIN") );
	NetDriver->ServerConnection->FlushNet();
	unguard;
}
IMPLEMENT_CLASS(UNetPendingLevel);

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/

