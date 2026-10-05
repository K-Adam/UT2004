/*=============================================================================
	WebRtcBrowser.cpp: WebRTC peers for UTcpNetDriver on the browser's own
	RTCPeerConnection (Emscripten/webrtc.js). A browser hosts through the web
	master's signaling, it can't take unknown peers like WebRtc.cpp does.
=============================================================================*/

#include <algorithm>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "UnIpDrv.h"
#include "UnTcpNetDriver.h"

#include "WebRtc.h"

extern "C"
{
	int ut_webrtc_connect( const char* Answer, const char* Ufrag, int MaxInbox );
	int ut_webrtc_connect_signal( const char* Url, const char* HostId, int MaxInbox );
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

IMPLEMENT_CLASS(UWebRtcConnection);

static void RemovePeer( FWebRtcHost* Host, FWebRtcPeer* Peer )
{
	auto It = std::find_if( Host->Peers.begin(), Host->Peers.end(), [Peer]( const std::shared_ptr<FWebRtcPeer>& P ) { return P.get()==Peer; } );
	if( It!=Host->Peers.end() )
	{
		ut_webrtc_close( Peer->Id );
		Host->Peers.erase( It );
	}
}

static UBOOL WebRtcPeerSend( FWebRtcPeer* Peer, const void* Data, INT Count )
{
	return ut_webrtc_send( Peer->Id, Data, Count );
}

// The script's peer for the WebRTC details in the URL, 0 with Error set when they are unusable
static INT ConnectPeer( FURL& ConnectURL, FString& Error, sockaddr_in& Addr )
{
	FString SignalId;
	if( WebRtcSignalSetup( ConnectURL, SignalId, Addr ) )
		return ut_webrtc_connect_signal( TCHAR_TO_ANSI(*WebMasterURL()), TCHAR_TO_ANSI(*SignalId), WEBRTC_MAX_INBOX );
	std::string Ufrag, Answer;
	if( !WebRtcClientSetup( ConnectURL, Error, Addr, Ufrag, Answer ) )
		return 0;
	return ut_webrtc_connect( Answer.c_str(), Ufrag.c_str(), WEBRTC_MAX_INBOX );
}

void UWebRtcConnection::LowLevelSend( void* Data, INT Count )
{
	guard(UWebRtcConnection::LowLevelSend);
	// The script drops it while the channel opens, the engine resends what needs to arrive
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
		RemovePeer( Host, Closing );
	unguard;
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
	ut_webrtc_host( TCHAR_TO_ANSI(*Url), WEBRTC_MAX_INBOX );
	WebRtc = new FWebRtcHost;
	unguard;
}

UBOOL UTcpNetDriver::WebRtcConnect( FURL& ConnectURL, FString& Error )
{
	guard(UTcpNetDriver::WebRtcConnect);
	auto Peer = std::make_shared<FWebRtcPeer>();
	Peer->Id = ConnectPeer( ConnectURL, Error, Peer->Addr );
	if( !Peer->Id )
		return 0;

	FWebRtcHost* Host = new FWebRtcHost;
	Host->Peers.push_back( Peer );
	WebRtc = Host;
	ServerConnection = Peer->Connection = new UWebRtcConnection( this, Peer.get(), Peer->Addr, USOCK_Pending, ConnectURL );
	debugf( NAME_DevNet, TEXT("Game client over WebRTC to %s"), *IpString(Peer->Addr.sin_addr,ntohs(Peer->Addr.sin_port)) );
	ServerConnection->CreateChannel( CHTYPE_Control, 1, 0 );
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

		// IPv6 clients have no address here and skip the per IP limit
		INT Id;
		while( (Id = ut_webrtc_accept( Text, sizeof(Text) )) > 0 )
		{
			auto Peer = std::make_shared<FWebRtcPeer>();
			Peer->Id = Id;
			Peer->CreatedTime = GCurrentTime;
			Peer->Addr.sin_family = AF_INET;
			Peer->Addr.sin_addr.s_addr = inet_addr( Text );
			if( Peer->Addr.sin_addr.s_addr==INADDR_NONE )
				Peer->Addr.sin_addr.s_addr = 0;

			INT Pending = 0, SameIP = 0;
			for( auto& Other : Host->Peers )
				if( !Other->Connection )
				{
					Pending++;
					SameIP += IpAddressMatches( Other->Addr, Peer->Addr );
				}
			// Signaling is one-shot, so during a level change the peer waits for the accept on its first packet
			if( Pending>=WEBRTC_MAX_PENDING || (Peer->Addr.sin_addr.s_addr && WebRtcIpLimited( this, Peer->Addr, SameIP )) || Notify->NotifyAcceptingConnection()==ACCEPTC_Reject )
				ut_webrtc_close( Id );
			else
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
					RemovePeer( Host, Peer.get() );
				break;
			}
			if( !Peer->Connection && !ServerConnection && WebRtcAnswerQuery( Peer.get(), Peer->Queries, Data, Count ) )
				continue;
			if( !Peer->Connection && !ServerConnection && WebRtcAccept( this, Peer.get(), Peer->Addr, Peer->Connection )==ACCEPTC_Reject )
			{
				RemovePeer( Host, Peer.get() );
				break;
			}
			if( Peer->Connection )
				Peer->Connection->ReceivedRawPacket( Data, Count );
		}
		// A peer that connects but never sends would hold its slot
		if( WebRtc==Host && !Peer->Connection && GCurrentTime - Peer->CreatedTime > (Peer->Queries ? WEBRTC_QUERY_TIMEOUT : WEBRTC_PENDING_TIMEOUT) )
			RemovePeer( Host, Peer.get() );
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

/*-----------------------------------------------------------------------------
	Server browser queries, the id is the script's peer.
-----------------------------------------------------------------------------*/

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

