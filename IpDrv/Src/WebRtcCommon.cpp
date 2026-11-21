// What the libdatachannel and the browser WebRTC peers share.

// The standard headers go before the engine's, whose macros (check, clock, min, max) break them.
// The engine builds with /Zp4, std::string crosses to WebRtc.cpp and needs the same packing in both.
#define NOMINMAX
#pragma pack(push, 8)
#if _WIN32
// inet_pton, the engine's winsock.h has none
#include <winsock2.h>
#include <ws2tcpip.h>
#endif
#include <cstring>
#include <random>
#include <string>
#pragma pack(pop)

#include "UnIpDrv.h"
#include "UnTcpNetDriver.h"

#include "WebRtc.h"

IMPLEMENT_CLASS(UWebRtcConnection);

const char* WebRtcSdpEnd = "a=sctp-port:5000\r\na=max-message-size:16384\r\n";

std::string WebRtcCandidate( const std::string& Ip, INT Port )
{
	return "a=candidate:1467250027 1 UDP 1467250027 " + Ip + " " + std::to_string(Port) + " typ host\r\na=end-of-candidates\r\n";
}

// The fingerprint goes into an SDP line, so anything but hex pairs and colons is refused
static bool ValidFingerprint( const FString& Fp )
{
	if( Fp.Len()!=95 )
		return false;
	for( INT i=0; i<Fp.Len(); i++ )
		if( i%3==2 ? (*Fp)[i]!=':' : !appStrchr(TEXT("0123456789ABCDEFabcdef"), (*Fp)[i]) )
			return false;
	return true;
}

// The id goes into a URL option and the web master's JSON. Standard types only, a host checks it on a library thread.
bool WebRtcHostIdValid( const char* Id )
{
	return strlen( Id )==32 && strspn( Id, "0123456789abcdef" )==32;
}

// The engine limits, bans and throttles by IPv4 address. An IPv6 client gets one made from its /64, which is what one
// subscriber holds, in 240.0.0.0/4 where no real client is. Text that is no address shares 240.0.0.0.
DWORD WebRtcPeerIp( const char* Text )
{
	DWORD Ip;
	if( inet_pton( AF_INET, Text, &Ip )==1 )
		return Ip;
	BYTE V6[16];
	if( inet_pton( AF_INET6, Text, V6 )!=1 )
		return htonl( 0xF0000000 );
	return htonl( 0xF0000000 | (appMemCrc( V6, 8 ) % 0x0FFFFFFE + 1) );
}

// The details of a host on the web master's socket are "signal:<id>". The web master knows it by the id, its address is for the UDP port a native one may also have open.
UBOOL WebRtcSignalSetup( FURL& ConnectURL, FString& Id, sockaddr_in& Addr )
{
	FString Details = ConnectURL.GetOption( TEXT("WebRTC="), TEXT("") );
	if( appStrnicmp( *Details, TEXT("signal:"), 7 ) )
		return 0;
	Id = Details.Mid( 7 );
	if( !WebRtcHostIdValid( TCHAR_TO_ANSI(*Id) ) )
		return 0;
	appMemzero( &Addr, sizeof(Addr) );
	Addr.sin_family = AF_INET;
	Addr.sin_port = htons( ConnectURL.Port );
	Addr.sin_addr.s_addr = inet_addr( TCHAR_TO_ANSI(*ConnectURL.Host) );
	return 1;
}

// A host on the web master's socket answers an offer only while its level takes players. One sent while the level changes
// gets no answer, or a peer that goes with the old level. So a join that has heard nothing from the server offers again.
UBOOL WebRtcOfferAgain( UTcpNetDriver* Driver, UWebRtcConnection* Connection, DOUBLE CreatedTime )
{
	return Connection && Connection==Driver->ServerConnection && Connection->State==USOCK_Pending && Connection->InPacketId==-1
		&& appSeconds()-CreatedTime < WEBRTC_OFFER_RETRY_TIME
		&& !appStrnicmp( Connection->URL.GetOption(TEXT("WebRTC="), TEXT("")), TEXT("signal:"), 7 );
}

// The per IP limit of new UDP connections, Connecting counts the address's peers that have not sent yet
UBOOL WebRtcIpLimited( UTcpNetDriver* Driver, sockaddr_in& Addr, INT Connecting )
{
	for( INT i=0; i<Driver->ClientConnections.Num(); i++ )
		if( IpAddressMatches( ((UTcpipConnection*)Driver->ClientConnections(i))->RemoteAddr, Addr ) && ((UTcpipConnection*)Driver->ClientConnections(i))->OpenedTime > GCurrentTime-60.0 )
			Connecting++;
	return Connecting >= (Driver->MaxConnPerIPPerMinute ? Driver->MaxConnPerIPPerMinute : 5);
}

// Accepts on the first packet like the UDP path. While the level changes it ignores, so later packets retry.
EAcceptConnection WebRtcAccept( UTcpNetDriver* Driver, FWebRtcPeer* Peer, sockaddr_in& Addr, UWebRtcConnection*& Connection )
{
	EAcceptConnection Accept = Driver->Notify->NotifyAcceptingConnection();
	if( Accept==ACCEPTC_Accept )
	{
		Connection = new UWebRtcConnection( Driver, Peer, Addr, USOCK_Open, FURL() );
		Connection->URL.Host = IpString( Addr.sin_addr );
		Driver->Notify->NotifyAcceptedConnection( Connection );
		Driver->ClientConnections.AddItem( Connection );
		debugf( TEXT("WebRTC connection from %s"), *Connection->LowLevelGetRemoteAddress() );
	}
	return Accept;
}

// A server browser row as the URL the setups below read
FURL WebRtcQueryURL( const FString& Host, const FString& Details )
{
	FURL URL;
	URL.Host = Host;
	URL.AddOption( *(FString(TEXT("WebRTC=")) + Details) );
	return URL;
}

// The query replies as the UDP query port sends them, one message per packet
struct FArchiveWebRtcWriter : public FArchiveUdpWriter
{
	FWebRtcPeer* Peer;

	FArchiveWebRtcWriter( FWebRtcPeer* InPeer )
	:	FArchiveUdpWriter( NULL, FIpAddr() )
	,	Peer( InPeer )
	{}
	void Flush()
	{
		WebRtcPeerSend( Peer, &SendData(0), SendData.Num() );
		// The version header stays for the next packet
		SendData.Remove( sizeof(INT), SendData.Num()-sizeof(INT) );
	}
};

// A game packet has at least two bytes, so a single byte from a peer without a connection is a server browser query.
// Queries counts them per peer, they come from anyone and each costs a reply of several packets.
UBOOL WebRtcAnswerQuery( FWebRtcPeer* Peer, INT& Queries, const BYTE* Data, INT Count )
{
	if( Count!=1 )
		return 0;
	if( Queries++ < WEBRTC_MAX_QUERIES )
	{
		FArchiveWebRtcWriter ArSend( Peer );
		ServerQueryAnswer( Data[0], ArSend );
	}
	return 1;
}

// Reads the server's details from the URL and makes up the client's ufrag and the server's answer.
UBOOL WebRtcClientSetup( FURL& ConnectURL, FString& Error, sockaddr_in& Addr, std::string& Ufrag, std::string& Answer )
{
	// The server browser adds the details the server advertises, a typed address needs ?WebRTC=<port>,<fingerprint>
	FString Details = ConnectURL.GetOption( TEXT("WebRTC="), TEXT("") );
	FString PortText, Fingerprint;
	if( !Details.Split( TEXT(","), &PortText, &Fingerprint ) || appAtoi(*PortText)<=0 || appAtoi(*PortText)>65535 || !ValidFingerprint(Fingerprint) )
	{
		Error = TEXT("This server has no WebRTC details. Join it from the server browser or add ?WebRTC=<port>,<fingerprint>");
		debugf( TEXT("WebRTC: %s"), *Error );
		return 0;
	}
	INT Port = appAtoi( *PortText );

	appMemzero( &Addr, sizeof(Addr) );
	Addr.sin_family = AF_INET;
	Addr.sin_port = htons(Port);
	Addr.sin_addr.s_addr = inet_addr( TCHAR_TO_ANSI(*ConnectURL.Host) );
	if( Addr.sin_addr.s_addr == INADDR_NONE )
	{
#if __EMSCRIPTEN__
		// Emscripten's resolver makes up addresses for its WebSocket emulation.
		HOSTENT* HostEnt = NULL;
#else
		HOSTENT* HostEnt = gethostbyname( TCHAR_TO_ANSI(*ConnectURL.Host) );
#endif
		if( !HostEnt || HostEnt->h_addrtype!=AF_INET )
		{
			Error = FString::Printf( TEXT("Could not resolve %s"), *ConnectURL.Host );
			return 0;
		}
		Addr.sin_addr = *(in_addr*)HostEnt->h_addr_list[0];
	}
	std::string Ip = TCHAR_TO_ANSI( *IpString(Addr.sin_addr) );
	std::string Fp = TCHAR_TO_ANSI( *Fingerprint );

	static const char Chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
	std::random_device Random;
	Ufrag.clear();
	for( INT i=0; i<24; i++ )
		Ufrag += Chars[Random() % (sizeof(Chars)-1)];

	Answer =
		"v=0\r\no=- 0 0 IN IP4 " + Ip + "\r\ns=-\r\nt=0 0\r\na=ice-lite\r\n"
		"m=application " + std::to_string(Port) + " UDP/DTLS/SCTP webrtc-datachannel\r\n"
		"c=IN IP4 " + Ip + "\r\na=mid:0\r\na=ice-options:ice2\r\n"
		"a=ice-ufrag:" + Ufrag + "\r\na=ice-pwd:" + Ufrag + "\r\n"
		"a=fingerprint:sha-256 " + Fp + "\r\na=setup:passive\r\n"
		+ WebRtcSdpEnd + WebRtcCandidate( Ip, Port );
	return 1;
}

void WebRtcServerConnection( UTcpNetDriver* Driver, FWebRtcPeer* Peer, sockaddr_in& Addr, UWebRtcConnection*& Connection, const FURL& ConnectURL )
{
	Driver->ServerConnection = Connection = new UWebRtcConnection( Driver, Peer, Addr, USOCK_Pending, ConnectURL );
	debugf( NAME_DevNet, TEXT("Game client over WebRTC to %s"), *IpString(Addr.sin_addr,ntohs(Addr.sin_port)) );
	Driver->ServerConnection->CreateChannel( CHTYPE_Control, 1, 0 );
}

// A peer that finishes the handshake but never sends would hold its slot, the UDP path has no such state
UBOOL WebRtcPendingExpired( DOUBLE CreatedTime, INT Queries )
{
	return GCurrentTime - CreatedTime > (Queries ? WEBRTC_QUERY_TIMEOUT : WEBRTC_PENDING_TIMEOUT);
}

void UWebRtcConnection::LowLevelSend( void* Data, INT Count )
{
	guard(UWebRtcConnection::LowLevelSend);
	// Dropped while the channel opens, the engine resends what needs to arrive
	if( Peer )
	{
		clock(Driver->SendCycles);
		WebRtcPeerSend( Peer, Data, Count );
		unclock(Driver->SendCycles);
	}
	unguard;
}

void UWebRtcConnection::Destroy()
{
	guard(UWebRtcConnection::Destroy);
	// Super flushes the last packets through the peer, so the peer goes after it
	FWebRtcPeer* Closing = Peer;
	Super::Destroy();
	FWebRtcHost* Host = ((UTcpNetDriver*)Driver)->WebRtc;
	if( Host && Closing )
		WebRtcRemovePeer( Host, Closing );
	unguard;
}
