// What WebRtc.cpp shares with the rest of the module.
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
// ends gather candidates over STUN. A native server whose WebRTC port is
// closed is listed on the web master the same way.

// IP, UDP, DTLS and SCTP headers per data channel message
#define WEBRTC_PACKET_OVERHEAD 90
#define WEBRTC_MAX_PACKET 512
// The server browser reads a query reply into this many bytes.
#define WEBRTC_MAX_QUERY_REPLY 4096
// The max-message-size of WebRtcSdpEnd, larger messages are refused instead of buffered
#define WEBRTC_MAX_MESSAGE 16384
#define WEBRTC_MAX_INBOX 256
#define WEBRTC_MAX_PENDING 64
#define WEBRTC_MIN_PENDING_TIME 2.0
#define WEBRTC_PENDING_TIMEOUT 15.0
// A server browser's peer asks a few times within seconds of connecting and is done
#define WEBRTC_MAX_QUERIES 8
#define WEBRTC_QUERY_TIMEOUT 8.0
// How long the relay gets to reach the server's own port. ICE resends its request five times in that time
#define WEBRTC_PROBE_TIME 10.0
// Windows asks about the firewall at the first start, so the test is repeated for about as long as answering that takes
#if _WIN32
#define WEBRTC_PROBE_TRIES 6
#else
#define WEBRTC_PROBE_TRIES 1
#endif
// Sending what was gathered so far beats waiting out slow interfaces, the host has to answer within 8 seconds
#define WEBRTC_GATHER_TIME 3.0
// The web master's limit for an offer and for an answer
#define WEBRTC_MAX_SDP 8192
// Room for an offer with its JSON escapes, and for the state of a server with many rules
#define WEBRTC_HOST_SOCKET_MAX_MESSAGE 32768
#define WEBRTC_HOST_SOCKET_RETRY 10.0
// The web master waits 8 seconds for an answer. An offer that has waited this many for its level would miss that with its candidates.
#define WEBRTC_OFFER_TIME 6
// A join through the web master that has heard nothing offers again this often, for this long
#define WEBRTC_OFFER_RETRY 2.0
#define WEBRTC_OFFER_RETRY_TIME 30.0
// The web master's list is cached this long at the edge
#define WEBRTC_LIST_CACHE_TIME 11.0
// What the ICE library takes
#define WEBRTC_MAX_CANDIDATES 30

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
bool WebRtcHostIdValid( const char* Id );
DWORD WebRtcPeerIp( const char* Text );
UBOOL WebRtcOfferAgain( UTcpNetDriver* Driver, UWebRtcConnection* Connection, DOUBLE CreatedTime );
UBOOL WebRtcSignalSetup( FURL& ConnectURL, FString& Id, sockaddr_in& Addr );
UBOOL WebRtcClientSetup( FURL& ConnectURL, FString& Error, sockaddr_in& Addr, std::string& Ufrag, std::string& Answer );
void WebRtcServerConnection( UTcpNetDriver* Driver, FWebRtcPeer* Peer, sockaddr_in& Addr, UWebRtcConnection*& Connection, const FURL& ConnectURL );
UBOOL WebRtcIpLimited( UTcpNetDriver* Driver, sockaddr_in& Addr, INT Connecting );
EAcceptConnection WebRtcAccept( UTcpNetDriver* Driver, FWebRtcPeer* Peer, sockaddr_in& Addr, UWebRtcConnection*& Connection );
FURL WebRtcQueryURL( const FString& Host, const FString& Details );
UBOOL WebRtcAnswerQuery( FWebRtcPeer* Peer, INT& Queries, const BYTE* Data, INT Count );
UBOOL WebRtcPendingExpired( DOUBLE CreatedTime, INT Queries );

// WebMaster.cpp
void WebMasterLinkReceived( const std::string& Json );
#if __ANDROID__
const std::string& AndroidCaCerts();
#endif

// WebRtc.cpp

// False while the peer's channel is not open
UBOOL WebRtcPeerSend( FWebRtcPeer* Peer, const void* Data, INT Count );
// Closes the peer and takes it off the host's list
void WebRtcRemovePeer( FWebRtcHost* Host, FWebRtcPeer* Peer );
