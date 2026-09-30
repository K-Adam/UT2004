/*=============================================================================
	WebRtcBrowser.cpp: The WebRTC client for UTcpNetDriver on the browser's own
	RTCPeerConnection (Emscripten/webrtc.js). Browsers can only join.
=============================================================================*/

#include <random>
#include <string>

#include "UnIpDrv.h"
#include "UnTcpNetDriver.h"
#include "WebRtc.h"

extern "C"
{
	int ut_webrtc_connect( const char* Answer, const char* Ufrag, int MaxInbox );
	void ut_webrtc_send( int Id, const void* Data, int Count );
	// Bytes of the next message, 0 when none is waiting, -1 once the peer closed.
	int ut_webrtc_recv( int Id, void* Data, int Max );
	void ut_webrtc_close( int Id );
}

struct FWebRtcPeer
{
	int Id;
	UWebRtcConnection* Connection;
};

// The driver's WebRtc field, a browser only has the one peer it joined through.
struct FWebRtcHost : FWebRtcPeer {};

IMPLEMENT_CLASS(UWebRtcConnection);

void UWebRtcConnection::LowLevelSend( void* Data, INT Count )
{
	guard(UWebRtcConnection::LowLevelSend);
	// The script drops it while the channel opens, the engine resends what needs to arrive
	if( Peer )
	{
		clock(Driver->SendCycles);
		ut_webrtc_send( Peer->Id, Data, Count );
		unclock(Driver->SendCycles);
	}
	unguard;
}

void UWebRtcConnection::Destroy()
{
	guard(UWebRtcConnection::Destroy);
	// Super flushes the last packets through the peer, so the peer goes after it
	UBOOL HadPeer = Peer != NULL;
	Super::Destroy();
	if( HadPeer )
		((UTcpNetDriver*)Driver)->WebRtcDestroy();
	unguard;
}

void UTcpNetDriver::WebRtcListen()
{
	debugf( TEXT("WebRTC: a browser can't host, UDP only") );
}

UBOOL UTcpNetDriver::WebRtcConnect( FURL& ConnectURL, FString& Error )
{
	guard(UTcpNetDriver::WebRtcConnect);
	sockaddr_in Addr;
	std::string Ufrag, Answer;
	if( !WebRtcClientSetup( ConnectURL, Error, Addr, Ufrag, Answer ) )
		return 0;

	FWebRtcHost* Host = new FWebRtcHost;
	Host->Id = ut_webrtc_connect( Answer.c_str(), Ufrag.c_str(), WEBRTC_MAX_INBOX );
	Host->Connection = new UWebRtcConnection( this, Host, Addr, USOCK_Pending, ConnectURL );
	WebRtc = Host;
	ServerConnection = Host->Connection;
	debugf( NAME_DevNet, TEXT("Game client over WebRTC to %s"), *IpString(Addr.sin_addr,ntohs(Addr.sin_port)) );
	ServerConnection->CreateChannel( CHTYPE_Control, 1, 0 );
	return 1;
	unguard;
}

void UTcpNetDriver::WebRtcTick()
{
	guard(UTcpNetDriver::WebRtcTick);
	BYTE Data[WEBRTC_MAX_PACKET];
	// A packet can end the connection and with it this peer
	while( WebRtc && WebRtc->Connection->State != USOCK_Closed )
	{
		INT Count = ut_webrtc_recv( WebRtc->Id, Data, sizeof(Data) );
		if( Count < 0 )
			WebRtc->Connection->State = USOCK_Closed;
		else if( Count == 0 )
			break;
		else
			WebRtc->Connection->ReceivedRawPacket( Data, Count );
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
	Host->Connection->Peer = NULL;
	ut_webrtc_close( Host->Id );
	delete Host;
	unguard;
}
