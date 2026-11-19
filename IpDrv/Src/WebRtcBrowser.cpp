// WebRTC peers for UTcpNetDriver on the browser's own RTCPeerConnection
// (Emscripten/webrtc.js). A browser hosts through the web master's signaling,
// it can't take unknown peers like WebRtc.cpp does.

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "UnIpDrv.h"
#include "UnTcpNetDriver.h"

#include "WebRtc.h"

extern "C"
{
	// 0 in a browser that has WebRTC turned off.
	int ut_webrtc_supported();
	// Both joins take their STUN and relay servers from the web master at Url.
	int ut_webrtc_connect( const char* Url, const char* Answer, const char* Ufrag, int MaxInbox );
	int ut_webrtc_connect_signal( const char* Url, const char* HostId, int MaxInbox );
	// "<host>:<port>" to gather with when the web master names no STUN server, empty for none.
	void ut_webrtc_fallback_stun( const char* Server );
	void ut_webrtc_host( const char* Url, int MaxInbox );
	void ut_webrtc_host_stop();
	int ut_webrtc_host_id( char* Id, int Max );
	// The next answered peer and the client IP the web master saw, 0 when none is waiting.
	int ut_webrtc_accept( char* Ip, int Max );
	// 0 while the channel is not open.
	int ut_webrtc_send( int Id, const void* Data, int Count );
	// Bytes of the next message, 0 when none is waiting, -1 once the peer closed.
	int ut_webrtc_recv( int Id, void* Data, int Max );
	void ut_webrtc_close( int Id );
}

struct FWebRtcPeer
{
	int Id;
	sockaddr_in Addr;
	UWebRtcConnection* Connection;
	DOUBLE CreatedTime;
	INT Queries;
};

struct FWebRtcHost
{
	std::vector<std::shared_ptr<FWebRtcPeer>> Peers;
};

void WebRtcRemovePeer( FWebRtcHost* Host, FWebRtcPeer* Peer )
{
	auto It = std::find_if( Host->Peers.begin(), Host->Peers.end(), [Peer]( const std::shared_ptr<FWebRtcPeer>& P ) { return P.get()==Peer; } );
	if( It!=Host->Peers.end() )
	{
		ut_webrtc_close( Peer->Id );
		Host->Peers.erase( It );
	}
}

UBOOL WebRtcPeerSend( FWebRtcPeer* Peer, const void* Data, INT Count )
{
	return ut_webrtc_send( Peer->Id, Data, Count );
}

// The engine limits, bans and throttles by IPv4 address. An IPv6 client gets one made from its /64, which is what one
// subscriber holds, in 240.0.0.0/4 where no real client is. Text that is no address shares 240.0.0.0.
static DWORD WebRtcPeerIp( const char* Text )
{
	DWORD Ip = inet_addr( Text );
	if( Ip!=INADDR_NONE )
		return Ip;
	BYTE V6[16];
	if( inet_pton( AF_INET6, Text, V6 )!=1 )
		return htonl( 0xF0000000 );
	return htonl( 0xF0000000 | (appMemCrc( V6, 8 ) % 0x0FFFFFFE + 1) );
}

// The script's peer for the WebRTC details in the URL, 0 with Error set when they are unusable
static INT ConnectPeer( FURL& ConnectURL, FString& Error, sockaddr_in& Addr )
{
	ut_webrtc_fallback_stun( TCHAR_TO_ANSI(*WebMasterStun()) );
	FString SignalId;
	if( WebRtcSignalSetup( ConnectURL, SignalId, Addr ) )
		return ut_webrtc_connect_signal( TCHAR_TO_ANSI(*WebMasterURL()), TCHAR_TO_ANSI(*SignalId), WEBRTC_MAX_INBOX );
	std::string Ufrag, Answer;
	if( !WebRtcClientSetup( ConnectURL, Error, Addr, Ufrag, Answer ) )
		return 0;
	return ut_webrtc_connect( TCHAR_TO_ANSI(*WebMasterURL()), Answer.c_str(), Ufrag.c_str(), WEBRTC_MAX_INBOX );
}

void UTcpNetDriver::WebRtcListen()
{
	guard(UTcpNetDriver::WebRtcListen);
	FString Url = WebMasterURL();
	if( !Url.Len() )
	{
		debugf( TEXT("WebRTC: a browser hosts through the web master, set WebMasterServer in [IpDrv.MasterServerLink]") );
		return;
	}
	checkSlow(WebRtcPeerIp("2001:db8:1:2::1")==WebRtcPeerIp("2001:db8:1:2:ffff::9"));
	checkSlow(WebRtcPeerIp("2001:db8:1:2::1")!=WebRtcPeerIp("2001:db8:1:3::1"));
	checkSlow((ntohl(WebRtcPeerIp("2001:db8:1:2::1"))>>28)==15 && WebRtcPeerIp("")==htonl(0xF0000000) && WebRtcPeerIp("1.2.3.4")==inet_addr("1.2.3.4"));
	ut_webrtc_fallback_stun( TCHAR_TO_ANSI(*WebMasterStun()) );
	ut_webrtc_host( TCHAR_TO_ANSI(*Url), WEBRTC_MAX_INBOX );
	WebRtc = new FWebRtcHost;
	unguard;
}

UBOOL UTcpNetDriver::WebRtcConnect( FURL& ConnectURL, FString& Error )
{
	guard(UTcpNetDriver::WebRtcConnect);
	if( !ut_webrtc_supported() )
	{
		// The dialog cuts a longer line off at a small window.
		Error = TEXT("Joining needs WebRTC, which this browser has turned off");
		return 0;
	}
	auto Peer = std::make_shared<FWebRtcPeer>();
	Peer->Id = ConnectPeer( ConnectURL, Error, Peer->Addr );
	if( !Peer->Id )
		return 0;

	FWebRtcHost* Host = new FWebRtcHost;
	Host->Peers.push_back( Peer );
	WebRtc = Host;
	WebRtcServerConnection( this, Peer.get(), Peer->Addr, Peer->Connection, ConnectURL );
	return 1;
	unguard;
}

void UTcpNetDriver::WebRtcTick()
{
	guard(UTcpNetDriver::WebRtcTick);
	FWebRtcHost* Host = WebRtc;
	if( !Host )
		return;

	if( !ServerConnection )
	{
		char Text[64];
		WebRtcDetails = ut_webrtc_host_id( Text, sizeof(Text) ) > 0 ? FString(TEXT("signal:")) + FString(Text) : FString();

		INT Id;
		while( (Id = ut_webrtc_accept( Text, sizeof(Text) )) > 0 )
		{
			auto Peer = std::make_shared<FWebRtcPeer>();
			Peer->Id = Id;
			Peer->CreatedTime = GCurrentTime;
			Peer->Addr.sin_family = AF_INET;
			Peer->Addr.sin_addr.s_addr = WebRtcPeerIp( Text );

			INT Pending = 0, SameIP = 0;
			FWebRtcPeer* Oldest = NULL;
			for( auto& Other : Host->Peers )
				if( !Other->Connection )
				{
					if( !Oldest )
						Oldest = Other.get();
					Pending++;
					SameIP += IpAddressMatches( Other->Addr, Peer->Addr );
				}
			// Signaling is one-shot, so during a level change the peer waits for the accept on its first packet
			if( WebRtcIpLimited( this, Peer->Addr, SameIP ) || Notify->NotifyAcceptingConnection()==ACCEPTC_Reject )
			{
				ut_webrtc_close( Id );
				continue;
			}
			// Peers are in creation order. The oldest idle one makes room, refusing the new one would let idle peers lock everyone out.
			if( Pending>=WEBRTC_MAX_PENDING )
				WebRtcRemovePeer( Host, Oldest );
			Host->Peers.push_back( Peer );
		}
	}

	// Backwards, because closing removes peers. A packet can end the level and with it this host.
	BYTE Data[WEBRTC_MAX_PACKET];
	for( INT i=(INT)Host->Peers.size()-1; i>=0 && WebRtc==Host; i-- )
	{
		std::shared_ptr<FWebRtcPeer> Peer = Host->Peers[i];
		INT Count;
		while( WebRtc==Host && (!Peer->Connection || Peer->Connection->State!=USOCK_Closed) && (Count = ut_webrtc_recv( Peer->Id, Data, sizeof(Data) )) != 0 )
		{
			if( Count < 0 )
			{
				if( Peer->Connection )
					Peer->Connection->State = USOCK_Closed;
				else
					WebRtcRemovePeer( Host, Peer.get() );
				break;
			}
			if( !Peer->Connection && !ServerConnection && WebRtcAnswerQuery( Peer.get(), Peer->Queries, Data, Count ) )
				continue;
			if( !Peer->Connection && !ServerConnection && WebRtcAccept( this, Peer.get(), Peer->Addr, Peer->Connection )==ACCEPTC_Reject )
			{
				WebRtcRemovePeer( Host, Peer.get() );
				break;
			}
			if( Peer->Connection )
				Peer->Connection->ReceivedRawPacket( Data, Count );
		}
		if( WebRtc==Host && !Peer->Connection && WebRtcPendingExpired( Peer->CreatedTime, Peer->Queries ) )
			WebRtcRemovePeer( Host, Peer.get() );
	}
	unguard;
}

void UTcpNetDriver::WebRtcDestroy()
{
	guard(UTcpNetDriver::WebRtcDestroy);
	FWebRtcHost* Host = WebRtc;
	if( !Host )
		return;
	WebRtc = NULL;
	if( !ServerConnection )
		ut_webrtc_host_stop();
	for( auto& Peer : Host->Peers )
	{
		if( Peer->Connection )
			Peer->Connection->Peer = NULL;
		ut_webrtc_close( Peer->Id );
	}
	delete Host;
	unguard;
}

// Server browser queries, the id is the script's peer.

INT WebRtcQueryOpen( const FString& Host, const FString& Details )
{
	FURL URL = WebRtcQueryURL( Host, Details );
	FString Error;
	sockaddr_in Addr;
	return ConnectPeer( URL, Error, Addr );
}

UBOOL WebRtcQuerySend( INT Id, const BYTE* Data, INT Count )
{
	return ut_webrtc_send( Id, Data, Count );
}

INT WebRtcQueryRecv( INT Id, BYTE* Data, INT Max )
{
	return ut_webrtc_recv( Id, Data, Max );
}

void WebRtcQueryClose( INT Id )
{
	ut_webrtc_close( Id );
}

