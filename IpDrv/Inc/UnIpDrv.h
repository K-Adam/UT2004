/*=============================================================================
	IpDrvPrivate.h: Unreal TCP/IP driver.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.

Revision history:
	* Created by Tim Sweeney.
=============================================================================*/

#ifndef UNIPDRV_H
#define UNIPDRV_H

#if _MSC_VER
	#pragma warning( disable : 4201 )
#endif

// A statically linked launcher includes this after the engine headers, whose clock macro breaks <ctime>.
#pragma push_macro("clock")
#undef clock
#pragma pack(push,8)
#include <atomic>
#include <thread>
#pragma pack(pop)
#pragma pop_macro("clock")

#include "UnSocketApi.h"

#if __WINSOCK__
	#define SOCKET_API TEXT("WinSock")
#else
	#define SOCKET_API TEXT("Sockets")

	#if __EMSCRIPTEN__
	// Emscripten emulates sockets over WebSockets, every ping, link and download would turn into a ws:// connection attempt.
	#define socket( Domain, Type, Protocol ) INVALID_SOCKET
	#endif
#endif

/*----------------------------------------------------------------------------
	API.
----------------------------------------------------------------------------*/

#ifndef IPDRV_API
	#define IPDRV_API DLL_IMPORT
#endif

/*-----------------------------------------------------------------------------
	Includes..
-----------------------------------------------------------------------------*/

#include "Engine.h"
#include "UnNet.h"
#include "UnSocket.h"

/*-----------------------------------------------------------------------------
	Definitions.
-----------------------------------------------------------------------------*/

// Globals.
extern UBOOL GIpDrvInitialized;

/*-----------------------------------------------------------------------------
	More Includes.
-----------------------------------------------------------------------------*/

#include "InternetLink.h"

/*-----------------------------------------------------------------------------
	Host resolution thread.
-----------------------------------------------------------------------------*/

void ResolveThreadEntry( class FResolveInfo* Info );

//
// Class for creating a background thread to resolve a host.
//
class FResolveInfo
{
public:
	// Variables.
	in_addr		Addr;
	// Written by the resolve thread.
	std::atomic<INT>	Done;
	ANSICHAR	HostName[256];
	TCHAR		Error[256];

	// Functions.
	FResolveInfo( const TCHAR* InHostName )
	{	
		debugf( TEXT("Resolving %s..."), InHostName );

		// The name can come from a server or an HTTP redirect.
		if( appStrlen(InHostName) >= ARRAY_COUNT(HostName) )
		{
			appStrncpy( Error, TEXT("Host name too long"), ARRAY_COUNT(Error) );
			Done = 1;
			return;
		}
		appMemcpy( HostName, appToAnsi(InHostName), appStrlen(InHostName) + 1 );
		*Error = 0;
		Done = 0;

#if __EMSCRIPTEN__
		// No threads, and IpResolve takes no host names there.
		ResolveThreadEntry( this );
#else
		std::thread( ResolveThreadEntry, this ).detach();
#endif
	}
	UBOOL Resolved()
	{
		return Done != 0;
	}
	const TCHAR* GetError()
	{
		return *Error ? Error : NULL;
	}
	const in_addr GetAddr()
	{
		return Addr;
	}
	const TCHAR* GetHostName()
	{
		return appFromAnsi(HostName);
	}
};

/*-----------------------------------------------------------------------------
	Bind to next available port.
-----------------------------------------------------------------------------*/

//
// Bind to next available port.
//
inline int bindnextport( SOCKET s, struct sockaddr_in* addr, int portcount, int portinc )
{
	guard(bindnextport);

	for( int i=0; i<portcount; i++ )
	{
		if( !bind( s, (sockaddr*)addr, sizeof(sockaddr_in) ) )
		{
			if (ntohs(addr->sin_port) != 0)
				return ntohs(addr->sin_port);
			else
			{
				// 0 means allocate a port for us, so find out what that port was
				struct sockaddr_in boundaddr;
				SOCKLEN size = sizeof(boundaddr);
				getsockname ( s, (sockaddr*)(&boundaddr), &size);
				return ntohs(boundaddr.sin_port);
			}
		}
		if( addr->sin_port==0 )
			break;
		addr->sin_port = htons( ntohs(addr->sin_port) + portinc );
	}
	return 0;
	unguard;
}

inline int getlocalhostaddr( FOutputDevice& Out, in_addr &HostAddr )
{
	guard(getlocalhostaddr);
	int CanBindAll = 0;
	IpSetInt( HostAddr, INADDR_ANY );
#if __EMSCRIPTEN__
	// A page has no address of its own.
	return CanBindAll;
#endif
	TCHAR Home[256]=TEXT("");
	TCHAR HostName[256]=TEXT("");
	ANSICHAR AnsiHostName[256]="";
	if( gethostname( AnsiHostName, 256 ) )
		Out.Logf( TEXT("%s: gethostname failed (%s)"), SOCKET_API, SocketError() );
	appStrcpy( HostName, appFromAnsi(AnsiHostName) );
	if( Parse(appCmdLine(),TEXT("MULTIHOME="),Home,ARRAY_COUNT(Home)) )
	{
		TCHAR *A, *B, *C, *D;
		A=Home;
		if
		(	(A=Home)!=NULL
		&&	(B=appStrchr(A,'.'))!=NULL
		&&	(C=appStrchr(B+1,'.'))!=NULL
		&&	(D=appStrchr(C+1,'.'))!=NULL )
		{
			IpSetBytes( HostAddr, appAtoi(A), appAtoi(B+1), appAtoi(C+1), \
				appAtoi(D+1) );
		}
		else Out.Logf( TEXT("Invalid multihome IP address %s"), Home );
	}
	else
	{
		FString ResolveError;
		if( !IpResolve( AnsiHostName, HostAddr, ResolveError ) )
		{
			Out.Logf( TEXT("%s ... can't get local IP."), *ResolveError );
		}
		else
		{
			if( !ParseParam(appCmdLine(),TEXT("PRIMARYNET")) )
				CanBindAll = 1;
			static UBOOL First=0;
			if( !First )
			{
				First = 1;
				debugf( NAME_Init, TEXT("%s: I am %s (%s)"), SOCKET_API, HostName, *IpString( HostAddr ) );
			}
		}
	}
	return CanBindAll;
	unguard;
}

//
// Get local IP to bind to
//
inline in_addr getlocalbindaddr( FOutputDevice& Out )
{
	guard(getlocalbindaddr);

	in_addr BindAddr;

	// If we can bind to all addresses, return 0.0.0.0
	if( getlocalhostaddr( Out, BindAddr ) )
		IpSetInt( BindAddr, INADDR_ANY );	
	return BindAddr;

	unguard;
}

inline UBOOL HasServerDetail( const TArray<FKeyValuePair>& Info, const TCHAR* Key )
{
	for( INT i=0; i<Info.Num(); i++ )
		if( Info(i).Key==Key )
			return 1;
	return 0;
}

inline FString GetServerDetail( const TArray<FKeyValuePair>& Info, const TCHAR* Key )
{
	for( INT i=0; i<Info.Num(); i++ )
		if( Info(i).Key==Key )
			return Info(i).Value;
	return FString();
}

// Replaces a server detail, adds it when missing and removes it when Value is empty
inline void SetServerDetail( TArray<FKeyValuePair>& Info, const TCHAR* Key, const FString& Value )
{
	INT i;
	for( i=0; i<Info.Num() && Info(i).Key!=Key; i++ );
	if( !Value.Len() )
	{
		if( i<Info.Num() )
			Info.Remove( i );
		return;
	}
	if( i==Info.Num() )
		Info(Info.AddZeroed()).Key = Key;
	Info(i).Value = Value;
}

// Started with -webrtc-client-only: no UDP game socket and no main masters, it joins and hosts over WebRTC only
inline UBOOL WebRtcOnly()
{
	return ParseParam( appCmdLine(), TEXT("webrtc-client-only") );
}

FString WebMasterURL();
// The STUN server to gather with when the web master names none, "<host>[:<port>]" or empty
FString WebMasterStun();
void WebMasterAdvertise( const FServerResponseLine& Server );
// True once for each join link the web master answered a state with. Token is the one to keep, empty when it issued none.
UBOOL WebMasterLinkTake( FString& Link, FString& Token );
// A host on the web master's socket sends its state there and returns true. A page always is one, a native server
// when its WebRTC port turned out closed.
UBOOL WebRtcHostState( const char* Json );
// The WebRTC details the web master lists for a driver whose own are Own: signal:<id> for a host on its socket,
// empty while there is nothing to list yet.
FString WebRtcListedDetails( const FString& Own );
// What the server does once its test finds the WebRTC port closed. Exported for the engine tests, which have no relay for that test to fail through.
extern "C" IPDRV_API void WebRtcHostThroughWebMaster();
// The Done functions return false while the request is running
struct FWebMasterRequest* WebMasterList();
UBOOL WebMasterListDone( struct FWebMasterRequest* Request, UBOOL& Ok, TArray<FServerResponseLine>& Servers, FString& News );
// Sends a client's offer to a host on the web master's socket and gets its answer back
struct FWebMasterRequest* WebMasterOffer( const FString& HostId, const FString& Sdp );
// Gone says the web master has no socket of that id, so offering again is no use
UBOOL WebMasterOfferDone( struct FWebMasterRequest* Request, UBOOL& Ok, FString& Sdp, UBOOL& Gone );
void WebMasterRequestFree( struct FWebMasterRequest* Request );
// The web master's ICE answer: its UDP relay (Host), its TLS relay for a page (TlsHost), the caller's public address (Ip)
// and a STUN server, the ini's StunServer when it names none. Empty when not given. The strings are valid until the next call.
struct FWebMasterTurn
{
	const ANSICHAR *Host, *TlsHost, *Username, *Password, *Ip, *StunHost;
	INT Port, TlsPort, StunPort;
};
// Returns false while the web master is being asked
UBOOL WebMasterTurn( FWebMasterTurn& Out );

// Writes the server's reply packets to a server browser query, nothing while no server runs
void ServerQueryAnswer( BYTE Command, FArchive& ArSend );
// Server browser queries over a WebRTC data channel, for servers and clients that UDP can't serve.
// Open gives 0 for unusable details, Send 0 while the channel opens, Recv 0 for nothing waiting and -1 once closed.
INT WebRtcQueryOpen( const FString& Host, const FString& Details );
UBOOL WebRtcQuerySend( INT Id, const BYTE* Data, INT Count );
INT WebRtcQueryRecv( INT Id, BYTE* Data, INT Max );
void WebRtcQueryClose( INT Id );
void AddMasterServers( class AMasterServerLink* Link, FArchive& Ar );

/*-----------------------------------------------------------------------------
	Public includes.
-----------------------------------------------------------------------------*/

#include "IpDrvClasses.h"
#include "HTTPDownload.h"

#if __STATIC_LINK
#define NAMES_ONLY
#define NATIVE_DEFS_ONLY
#define AUTOGENERATE_NAME(name)
#define AUTOGENERATE_FUNCTION(cls,idx,name)
#include "IpDrvClasses.h"
#undef AUTOGENERATE_FUNCTION
#undef AUTOGENERATE_NAME
#undef NATIVE_DEFS_ONLY
#undef NAMES_ONLY
#endif

#endif // UNIPDRV_H

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/

