/*============================================================================
	UnSocket.h: Common interface for WinSock and BSD sockets.

	Revision history:
		* Created by Mike Danylchuk
============================================================================*/

#define IP(sin_addr,n) ((BYTE*)&sin_addr.s_addr)[n-1]

/*-----------------------------------------------------------------------------
	Definitions.
-----------------------------------------------------------------------------*/

// An IP address (host byte order).
struct IPDRV_API FIpAddr
{
	DWORD Addr;			// Host byte order
	DWORD Port;			// Host byte order

	// tors
	FIpAddr() {}
	FIpAddr( const TCHAR* IPString, INT InPort )
	{
		Addr = ntohl(inet_addr(appToAnsi(IPString)));  // FIpAddr is in host byte order.
		Port = InPort;
	}
	FIpAddr( SOCKADDR_IN SockAddr );

	// equality
	UBOOL operator==(FIpAddr& Other)
	{
		return Addr==Other.Addr && Port==Other.Port;
	}

	// helpers
	FString GetString( UBOOL ShowPort );
	SOCKADDR_IN GetSockAddr();

	// serialization
    friend IPDRV_API FArchive& operator<<( FArchive& Ar, FIpAddr& IP )
    {
        IP.Addr = INTEL_ORDER32(IP.Addr);  // this is not really in "host" order, but intel.
        return Ar << IP.Addr << IP.Port;
        IP.Addr = INTEL_ORDER32(IP.Addr);
    }
};

// SocketData.
struct IPDRV_API FSocketData
{
	SOCKADDR_IN Addr;	// Network byte order.
	INT Port;			// Host byte order.
	SOCKET Socket;

	void UpdateFromSocket()
	{
		SOCKLEN size = sizeof(Addr);
		getsockname ( Socket, (SOCKADDR*)(&Addr), &size);
		Port = ntohs(Addr.sin_port);
	}
	FString GetString( UBOOL ShowPort );
};

/*----------------------------------------------------------------------------
	Functions.
----------------------------------------------------------------------------*/

UBOOL InitSockets( FString& Error );
UBOOL SetSocketReuseAddr( INT Socket, UBOOL ReUse=1 );
UBOOL SetSocketLinger( INT Socket );
UBOOL SetSocketRecvErr( INT Socket );
TCHAR* SocketError( INT Code=-1 );
// Addr of a host name or an address in text. Blocks while the system looks a name up.
UBOOL IpResolve( const ANSICHAR* Host, in_addr& Addr, FString& Error );
UBOOL IpMatches( sockaddr_in& A, sockaddr_in& B );
UBOOL IpAddressMatches( sockaddr_in& A, sockaddr_in& B );
UBOOL IpAddressRangeMatches( sockaddr_in& A, sockaddr_in& B );
void IpGetBytes( in_addr Addr, BYTE& Ip1, BYTE& Ip2, BYTE& Ip3, BYTE& Ip4 );
void IpSetBytes( in_addr& Addr, BYTE Ip1, BYTE Ip2, BYTE Ip3, BYTE Ip4 );
void IpGetInt( in_addr Addr, DWORD& Ip );
void IpSetInt( in_addr& Addr, DWORD Ip );
FString IpString( in_addr Addr, INT Port=0 );

/*----------------------------------------------------------------------------
	The End.
----------------------------------------------------------------------------*/

