/*============================================================================
	UnSocket.cpp: Common interface for WinSock and BSD sockets.

	Revision history:
		* Created by Mike Danylchuk
============================================================================*/

#include "UnIpDrv.h"

/*----------------------------------------------------------------------------
	Resolve.
----------------------------------------------------------------------------*/

UBOOL IpResolve( const ANSICHAR* Host, in_addr& Addr, FString& Error )
{
	// The id would leak to DNS
	if( HostTextSignalSuffix( ANSI_TO_TCHAR(Host) ) )
	{
		Error = FString::Printf( TEXT("%s has no address, it is joined through the web master"), *FString(Host) );
		return 0;
	}
	// Spellings like 1.2.3 and 2130706433 that getaddrinfo refuses on some systems.
	DWORD Numeric = inet_addr( Host );
	if( Numeric != INADDR_NONE )
	{
		Addr.s_addr = Numeric;
		return 1;
	}
#if __EMSCRIPTEN__
	// Emscripten's resolver only makes up addresses for its WebSocket emulation.
	Error = TEXT("Browsers can't look up host names");
	return 0;
#else
	addrinfo Hints, *Result = NULL;
	appMemzero( &Hints, sizeof(Hints) );
	Hints.ai_family = AF_INET;
	INT Code = getaddrinfo( Host, NULL, &Hints, &Result );
	if( Code || !Result )
	{
#if __WINSOCK__
		FString Reason = SocketError( Code );
#else
		FString Reason = gai_strerror( Code );
#endif
		Error = FString::Printf( TEXT("Can't find host %s (%s)"), *FString(Host), *Reason );
		return 0;
	}
	Addr = ((sockaddr_in*)Result->ai_addr)->sin_addr;
	freeaddrinfo( Result );
	return 1;
#endif
}

// Resolution thread entrypoint.
void ResolveThreadEntry( FResolveInfo* Info )
{
	IpSetInt( Info->Addr, 0 );
	FString Error;
	if( !IpResolve( Info->HostName, Info->Addr, Error ) )
		appStrncpy( Info->Error, *Error, ARRAY_COUNT(Info->Error) );
	Info->Done = 1;
}

/*----------------------------------------------------------------------------
	Initialization.
----------------------------------------------------------------------------*/

UBOOL InitSockets( FString& Error )
{
	guard(InitSockets);

	// Init names.
	#define NAMES_ONLY
	#define AUTOGENERATE_NAME(name) extern IPDRV_API FName IPDRV_##name; IPDRV_##name=FName(TEXT(#name),FNAME_Intrinsic);
	#define AUTOGENERATE_FUNCTION(cls,idx,name)
	#include "IpDrvClasses.h"
	#undef AUTOGENERATE_FUNCTION
	#undef AUTOGENERATE_NAME
	#undef DECLARE_NAME
	#undef NAMES_ONLY

	UBOOL Mismatch = false;
	#define VERIFY_CLASS_SIZES
	#define NAMES_ONLY
	#define AUTOGENERATE_NAME(name)
	#define AUTOGENERATE_FUNCTION(cls,idx,name)
	#include "IpDrvClasses.h"
	#undef AUTOGENERATE_FUNCTION
	#undef AUTOGENERATE_NAME
	#undef NAMES_ONLY
	#undef VERIFY_CLASS_SIZES
	if( Mismatch )
		appErrorf(TEXT("IpDrv C++/UnrealScript class size mismatch"));

#if __WINSOCK__
	// Init WSA.
	static UBOOL Tried = 0;
	if( !Tried )
	{
		Tried = 1;
		WSADATA WSAData;
		INT Code = WSAStartup( 0x0202, &WSAData );
		if( Code==0 )
		{
			GIpDrvInitialized = 1;
			debugf
			(
				NAME_Init,
				TEXT("WinSock: version %i.%i (%i.%i), MaxSocks=%i, MaxUdp=%i"),
				WSAData.wVersion>>8,WSAData.wVersion&255,
				WSAData.wHighVersion>>8,WSAData.wHighVersion&255,
				WSAData.iMaxSockets,WSAData.iMaxUdpDg
			);
		} else {
			TCHAR Error256[256];
			appSprintf( Error256, TEXT("WSAStartup failed (%s)"), SocketError(Code) );
			Error = FString::Printf( TEXT("%s"), Error256 );
		}
	}
#else
	GIpDrvInitialized = 1;
#endif

	return GIpDrvInitialized;
	unguard;
}

/*----------------------------------------------------------------------------
	Errors.
----------------------------------------------------------------------------*/

//
// Convert error code to text.
//
TCHAR* SocketError( INT Code )
{
#if __WINSOCK__
	if( Code == -1 )
		Code = WSAGetLastError();
	switch( Code )
	{
		case WSAEINTR:				return TEXT("WSAEINTR");
		case WSAEBADF:				return TEXT("WSAEBADF");
		case WSAEACCES:				return TEXT("WSAEACCES");
		case WSAEFAULT:				return TEXT("WSAEFAULT");
		case WSAEINVAL:				return TEXT("WSAEINVAL");
		case WSAEMFILE:				return TEXT("WSAEMFILE");
		case WSAEWOULDBLOCK:		return TEXT("WSAEWOULDBLOCK");
		case WSAEINPROGRESS:		return TEXT("WSAEINPROGRESS");
		case WSAEALREADY:			return TEXT("WSAEALREADY");
		case WSAENOTSOCK:			return TEXT("WSAENOTSOCK");
		case WSAEDESTADDRREQ:		return TEXT("WSAEDESTADDRREQ");
		case WSAEMSGSIZE:			return TEXT("WSAEMSGSIZE");
		case WSAEPROTOTYPE:			return TEXT("WSAEPROTOTYPE");
		case WSAENOPROTOOPT:		return TEXT("WSAENOPROTOOPT");
		case WSAEPROTONOSUPPORT:	return TEXT("WSAEPROTONOSUPPORT");
		case WSAESOCKTNOSUPPORT:	return TEXT("WSAESOCKTNOSUPPORT");
		case WSAEOPNOTSUPP:			return TEXT("WSAEOPNOTSUPP");
		case WSAEPFNOSUPPORT:		return TEXT("WSAEPFNOSUPPORT");
		case WSAEAFNOSUPPORT:		return TEXT("WSAEAFNOSUPPORT");
		case WSAEADDRINUSE:			return TEXT("WSAEADDRINUSE");
		case WSAEADDRNOTAVAIL:		return TEXT("WSAEADDRNOTAVAIL");
		case WSAENETDOWN:			return TEXT("WSAENETDOWN");
		case WSAENETUNREACH:		return TEXT("WSAENETUNREACH");
		case WSAENETRESET:			return TEXT("WSAENETRESET");
		case WSAECONNABORTED:		return TEXT("WSAECONNABORTED");
		case WSAECONNRESET:			return TEXT("WSAECONNRESET");
		case WSAENOBUFS:			return TEXT("WSAENOBUFS");
		case WSAEISCONN:			return TEXT("WSAEISCONN");
		case WSAENOTCONN:			return TEXT("WSAENOTCONN");
		case WSAESHUTDOWN:			return TEXT("WSAESHUTDOWN");
		case WSAETOOMANYREFS:		return TEXT("WSAETOOMANYREFS");
		case WSAETIMEDOUT:			return TEXT("WSAETIMEDOUT");
		case WSAECONNREFUSED:		return TEXT("WSAECONNREFUSED");
		case WSAELOOP:				return TEXT("WSAELOOP");
		case WSAENAMETOOLONG:		return TEXT("WSAENAMETOOLONG");
		case WSAEHOSTDOWN:			return TEXT("WSAEHOSTDOWN");
		case WSAEHOSTUNREACH:		return TEXT("WSAEHOSTUNREACH");
		case WSAENOTEMPTY:			return TEXT("WSAENOTEMPTY");
		case WSAEPROCLIM:			return TEXT("WSAEPROCLIM");
		case WSAEUSERS:				return TEXT("WSAEUSERS");
		case WSAEDQUOT:				return TEXT("WSAEDQUOT");
		case WSAESTALE:				return TEXT("WSAESTALE");
		case WSAEREMOTE:			return TEXT("WSAEREMOTE");
		case WSAEDISCON:			return TEXT("WSAEDISCON");
		case WSASYSNOTREADY:		return TEXT("WSASYSNOTREADY");
		case WSAVERNOTSUPPORTED:	return TEXT("WSAVERNOTSUPPORTED");
		case WSANOTINITIALISED:		return TEXT("WSANOTINITIALISED");
		case WSAHOST_NOT_FOUND:		return TEXT("WSAHOST_NOT_FOUND");
		case WSATRY_AGAIN:			return TEXT("WSATRY_AGAIN");
		case WSANO_RECOVERY:		return TEXT("WSANO_RECOVERY");
		case WSANO_DATA:			return TEXT("WSANO_DATA");
		case 0:						return TEXT("WSANO_ERROR");
		default:					return TEXT("WSA_Unknown");
	}
#else
	if( Code == -1 )
		Code = errno;
	switch( Code )
	{
		case EINTR:					return TEXT("EINTR");
		case EBADF:					return TEXT("EBADF");
		case EACCES:				return TEXT("EACCES");
		case EFAULT:				return TEXT("EFAULT");
		case EINVAL:				return TEXT("EINVAL");
		case EMFILE:				return TEXT("EMFILE");
		case EWOULDBLOCK:			return TEXT("EWOULDBLOCK");
		case EINPROGRESS:			return TEXT("EINPROGRESS");
		case EALREADY:				return TEXT("EALREADY");
		case ENOTSOCK:				return TEXT("ENOTSOCK");
		case EDESTADDRREQ:			return TEXT("EDESTADDRREQ");
		case EMSGSIZE:				return TEXT("EMSGSIZE");
		case EPROTOTYPE:			return TEXT("EPROTOTYPE");
		case ENOPROTOOPT:			return TEXT("ENOPROTOOPT");
		case EPROTONOSUPPORT:		return TEXT("EPROTONOSUPPORT");
		case ESOCKTNOSUPPORT:		return TEXT("ESOCKTNOSUPPORT");
		case EOPNOTSUPP:			return TEXT("EOPNOTSUPP");
		case EPFNOSUPPORT:			return TEXT("EPFNOSUPPORT");
		case EAFNOSUPPORT:			return TEXT("EAFNOSUPPORT");
		case EADDRINUSE:			return TEXT("EADDRINUSE");
		case EADDRNOTAVAIL:			return TEXT("EADDRNOTAVAIL");
		case ENETDOWN:				return TEXT("ENETDOWN");
		case ENETUNREACH:			return TEXT("ENETUNREACH");
		case ENETRESET:				return TEXT("ENETRESET");
		case ECONNABORTED:			return TEXT("ECONNABORTED");
		case ECONNRESET:			return TEXT("ECONNRESET");
		case ENOBUFS:				return TEXT("ENOBUFS");
		case EISCONN:				return TEXT("EISCONN");
		case ENOTCONN:				return TEXT("ENOTCONN");
		case ESHUTDOWN:				return TEXT("ESHUTDOWN");
		case ETOOMANYREFS:			return TEXT("ETOOMANYREFS");
		case ETIMEDOUT:				return TEXT("ETIMEDOUT");
		case ECONNREFUSED:			return TEXT("ECONNREFUSED");
		case ELOOP:					return TEXT("ELOOP");
		case ENAMETOOLONG:			return TEXT("ENAMETOOLONG");
		case EHOSTDOWN:				return TEXT("EHOSTDOWN");
		case EHOSTUNREACH:			return TEXT("EHOSTUNREACH");
		case ENOTEMPTY:				return TEXT("ENOTEMPTY");
		case EUSERS:				return TEXT("EUSERS");
		case EDQUOT:				return TEXT("EDQUOT");
		case ESTALE:				return TEXT("ESTALE");
		case EREMOTE:				return TEXT("EREMOTE");
#if !__EMSCRIPTEN__
		// Emscripten numbers errno like WASI, where these clash with EACCES and the like. It looks nothing up anyway.
		case HOST_NOT_FOUND:		return TEXT("HOST_NOT_FOUND");
		case TRY_AGAIN:				return TEXT("TRY_AGAIN");
		case NO_RECOVERY:			return TEXT("NO_RECOVERY");
#endif
		case 0:						return TEXT("NO_ERROR");
		default:					return TEXT("Unknown");
	}
#endif
}

/*----------------------------------------------------------------------------
	Helper functions.
----------------------------------------------------------------------------*/

UBOOL IpMatches( sockaddr_in& A, sockaddr_in& B )
{
	return	A.sin_addr.s_addr      == B.sin_addr.s_addr
	&&		A.sin_port             == B.sin_port
	&&		A.sin_family           == B.sin_family;
}

UBOOL IpAddressMatches( sockaddr_in& A, sockaddr_in& B )
{
	return	A.sin_addr.s_addr	== B.sin_addr.s_addr
		&&	A.sin_family		== B.sin_family;
}

UBOOL IpAddressRangeMatches( sockaddr_in& A, sockaddr_in& B )
{
	// Same /16. The addresses are in network byte order.
	return	(ntohl(A.sin_addr.s_addr) & 0xFFFF0000) == (ntohl(B.sin_addr.s_addr) & 0xFFFF0000)
		&&	A.sin_family == B.sin_family;
}

void IpGetBytes( in_addr Addr, BYTE& Ip1, BYTE& Ip2, BYTE& Ip3, BYTE& Ip4 )
{
	Ip1 = IP(Addr,1);
	Ip2 = IP(Addr,2);
	Ip3 = IP(Addr,3);
	Ip4 = IP(Addr,4);
}

void IpSetBytes( in_addr& Addr, BYTE Ip1, BYTE Ip2, BYTE Ip3, BYTE Ip4 )
{
	IP(Addr,1) = Ip1;
	IP(Addr,2) = Ip2;
	IP(Addr,3) = Ip3;
	IP(Addr,4) = Ip4;
}

void IpGetInt( in_addr Addr, DWORD& Ip )
{
	Ip = Addr.s_addr;
}

void IpSetInt( in_addr& Addr, DWORD Ip )
{
	Addr.s_addr = Ip;
}

FString IpString( in_addr Addr, INT Port )
{
	guard(IpString);
	FString Result = FString::Printf( TEXT("%i.%i.%i.%i"), IP(Addr,1), IP(Addr,2), IP(Addr,3), IP(Addr,4) );
	if( Port )
		Result += FString::Printf( TEXT(":%i"), Port );
	return Result;
	unguard;
}

UBOOL SetSocketReuseAddr( INT Socket, UBOOL ReUse )
{
	INT optval = ReUse?1:0;
	return setsockopt( Socket, SOL_SOCKET, SO_REUSEADDR, (char *)&optval, sizeof(INT) ) == 0;
}


UBOOL SetSocketLinger( INT Socket )
{
	LINGER ling;
	ling.l_onoff  = 1;	// linger on
	ling.l_linger = 0;	// timeout in seconds
	return ( setsockopt( Socket, SOL_SOCKET, SO_LINGER, (char*)&ling, sizeof(ling) ) == 0 );
}


UBOOL SetSocketRecvErr( INT Socket )
{
#ifdef __linux__
	int on = 1;
	return setsockopt(Socket, SOL_IP, IP_RECVERR, (char*)&on, sizeof (on)) == 0;
#else
	return 1;
#endif
}


//
// FSocketData functions
//

FString FSocketData::GetString( UBOOL ShowPort )
{
	return IpString(Addr.sin_addr, ShowPort ? ntohs(Addr.sin_port) : 0 );
}

//
// FIpAddr functions
//

FIpAddr::FIpAddr( SOCKADDR_IN SockAddr )
{
	DWORD NetAddr;
	IpGetInt( SockAddr.sin_addr, NetAddr );
    Addr = ntohl(NetAddr);
	Port = ntohs(SockAddr.sin_port);
}

FString FIpAddr::GetString( UBOOL ShowPort )
{
	in_addr a;
	IpSetInt( a, htonl(Addr) );
	return IpString( a, ShowPort ? Port : 0);
}

SOCKADDR_IN FIpAddr::GetSockAddr()
{
	SOCKADDR_IN Result;
	Result.sin_family = AF_INET;
	Result.sin_port = htons(Port);
	IpSetInt( Result.sin_addr, htonl(Addr) );
	return Result;
}

/*----------------------------------------------------------------------------
	The End.
----------------------------------------------------------------------------*/

