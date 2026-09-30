/*=============================================================================
	WebRtc.h: What the libdatachannel and the browser WebRTC peers share.
	Included after <string>, <random> and the engine headers.

	Signaling-free like libp2p's WebRTC Direct (v1). The server advertises its
	WebRTC port and certificate fingerprint. A client picks one random string as
	its ICE ufrag and password, puts it in its own offer and writes the server's
	answer itself. The server learns the string from the first STUN request on
	its mux port and builds the matching description pair.

	Browsers can't take unknown peers, so a browser host advertises signal:<id>
	instead. Clients send their offer to it through the web master and both
	ends gather candidates over STUN.
=============================================================================*/

// IP, UDP, DTLS and SCTP headers per data channel message
#define WEBRTC_PACKET_OVERHEAD 90
#define WEBRTC_MAX_PACKET 512
#define WEBRTC_MAX_INBOX 256
#define WEBRTC_MAX_PENDING 64
#define WEBRTC_PENDING_TIMEOUT 15.0
#define WEBRTC_STUN_HOST "stun.cloudflare.com"
#define WEBRTC_STUN_PORT 3478
// Sending what was gathered so far beats waiting out slow interfaces, the host has to answer within 8 seconds
#define WEBRTC_GATHER_TIME 3.0

struct FWebRtcPeer;

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

static const char* WebRtcSdpEnd = "a=sctp-port:5000\r\na=max-message-size:16384\r\n";

static std::string WebRtcCandidate( const std::string& Ip, INT Port )
{
	return "a=candidate:1467250027 1 UDP 1467250027 " + Ip + " " + std::to_string(Port) + " typ host\r\na=end-of-candidates\r\n";
}

static std::string ToStd( const FString& S )
{
	return std::string( TCHAR_TO_ANSI(*S) );
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

// A browser host's details are "signal:<id>". Its address is only shown, the web master knows it by the id.
static UBOOL WebRtcSignalSetup( FURL& ConnectURL, FString& Id, sockaddr_in& Addr )
{
	FString Details = ConnectURL.GetOption( TEXT("WebRTC="), TEXT("") );
	if( appStrnicmp( *Details, TEXT("signal:"), 7 ) )
		return 0;
	Id = Details.Mid( 7 );
	if( Id.Len()!=32 )
		return 0;
	for( INT i=0; i<Id.Len(); i++ )
		if( !appStrchr( TEXT("0123456789abcdef"), (*Id)[i] ) )
			return 0;
	appMemzero( &Addr, sizeof(Addr) );
	Addr.sin_family = AF_INET;
	Addr.sin_port = htons( ConnectURL.Port );
	Addr.sin_addr.s_addr = inet_addr( TCHAR_TO_ANSI(*ConnectURL.Host) );
	return 1;
}

// The per IP limit of new UDP connections, Connecting counts the address's peers that have not sent yet
static UBOOL WebRtcIpLimited( UTcpNetDriver* Driver, sockaddr_in& Addr, INT Connecting )
{
	for( INT i=0; i<Driver->ClientConnections.Num(); i++ )
		if( IpAddressMatches( ((UTcpipConnection*)Driver->ClientConnections(i))->RemoteAddr, Addr ) && ((UTcpipConnection*)Driver->ClientConnections(i))->OpenedTime > GCurrentTime-60.0 )
			Connecting++;
	return Connecting >= (Driver->MaxConnPerIPPerMinute ? Driver->MaxConnPerIPPerMinute : 5);
}

// Accepts on the first packet like the UDP path. While the level changes it ignores, so later packets retry.
static EAcceptConnection WebRtcAccept( UTcpNetDriver* Driver, FWebRtcPeer* Peer, sockaddr_in& Addr, UWebRtcConnection*& Connection )
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

// Reads the server's details from the URL and makes up the client's ufrag and the server's answer.
static UBOOL WebRtcClientSetup( FURL& ConnectURL, FString& Error, sockaddr_in& Addr, std::string& Ufrag, std::string& Answer )
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
	std::string Ip = ToStd( IpString(Addr.sin_addr) );

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
		"a=fingerprint:sha-256 " + ToStd(Fingerprint) + "\r\na=setup:passive\r\n"
		+ WebRtcSdpEnd + WebRtcCandidate( Ip, Port );
	return 1;
}
