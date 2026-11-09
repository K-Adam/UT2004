// What the libdatachannel and the browser WebRTC peers share.
// Included after <string> and the engine headers.
//
// Signaling-free like libp2p's WebRTC Direct (v1). The server advertises its
// WebRTC port and certificate fingerprint. A client picks one random string as
// its ICE ufrag and password, puts it in its own offer and writes the server's
// answer itself. The server learns the string from the first STUN request on
// its mux port and builds the matching description pair.
//
// Browsers can't take unknown peers, so a browser host advertises signal:<id>
// instead. Clients send their offer to it through the web master and both
// ends gather candidates over STUN.

// IP, UDP, DTLS and SCTP headers per data channel message
#define WEBRTC_PACKET_OVERHEAD 90
#define WEBRTC_MAX_PACKET 512
// The max-message-size of WebRtcSdpEnd, larger messages are refused instead of buffered
#define WEBRTC_MAX_MESSAGE 16384
#define WEBRTC_MAX_INBOX 256
#define WEBRTC_MAX_PENDING 64
#define WEBRTC_MIN_PENDING_TIME 2.0
#define WEBRTC_PENDING_TIMEOUT 15.0
// A server browser's peer asks a few times within seconds of connecting and is done
#define WEBRTC_MAX_QUERIES 8
#define WEBRTC_QUERY_TIMEOUT 8.0
#define WEBRTC_STUN_HOST "stun.cloudflare.com"
#define WEBRTC_STUN_PORT 3478
// Sending what was gathered so far beats waiting out slow interfaces, the host has to answer within 8 seconds
#define WEBRTC_GATHER_TIME 3.0

struct FWebRtcPeer;
struct FWebRtcHost;

class UWebRtcConnection : public UTcpipConnection
{
	DECLARE_CLASS(UWebRtcConnection,UTcpipConnection,CLASS_Config|CLASS_Transient,IpDrv)
	NO_DEFAULT_CONSTRUCTOR(UWebRtcConnection)

	FWebRtcPeer* Peer;

	UWebRtcConnection( UNetDriver* InDriver, FWebRtcPeer* InPeer, const sockaddr_in& InAddr, EConnectionState InState, const FURL& InURL )
	:	UTcpipConnection( INVALID_SOCKET, InDriver, InAddr, InState, 0, InURL )
	,	Peer( InPeer )
	{
		MaxPacket = WEBRTC_MAX_PACKET;
		PacketOverhead = WEBRTC_PACKET_OVERHEAD;
	}

	void LowLevelSend( void* Data, INT Count );
	void Destroy();
};

// WebRtcCommon.cpp
extern const char* WebRtcSdpEnd;
std::string WebRtcCandidate( const std::string& Ip, INT Port );
UBOOL WebRtcSignalSetup( FURL& ConnectURL, FString& Id, sockaddr_in& Addr );
UBOOL WebRtcClientSetup( FURL& ConnectURL, FString& Error, sockaddr_in& Addr, std::string& Ufrag, std::string& Answer );
void WebRtcServerConnection( UTcpNetDriver* Driver, FWebRtcPeer* Peer, sockaddr_in& Addr, UWebRtcConnection*& Connection, const FURL& ConnectURL );
UBOOL WebRtcIpLimited( UTcpNetDriver* Driver, sockaddr_in& Addr, INT Connecting );
EAcceptConnection WebRtcAccept( UTcpNetDriver* Driver, FWebRtcPeer* Peer, sockaddr_in& Addr, UWebRtcConnection*& Connection );
FURL WebRtcQueryURL( const FString& Host, const FString& Details );
UBOOL WebRtcAnswerQuery( FWebRtcPeer* Peer, INT& Queries, const BYTE* Data, INT Count );
UBOOL WebRtcPendingExpired( DOUBLE CreatedTime, INT Queries );

// WebRtc.cpp or WebRtcBrowser.cpp

// False while the peer's channel is not open
UBOOL WebRtcPeerSend( FWebRtcPeer* Peer, const void* Data, INT Count );
// Closes the peer and takes it off the host's list
void WebRtcRemovePeer( FWebRtcHost* Host, FWebRtcPeer* Peer );
