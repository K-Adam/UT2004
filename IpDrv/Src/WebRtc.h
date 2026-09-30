/*=============================================================================
	WebRtc.h: What the libdatachannel and the browser WebRTC peers share.
	Included after <string>, <random> and the engine headers.

	Signaling-free like libp2p's WebRTC Direct (v1). The server advertises its
	WebRTC port and certificate fingerprint. A client picks one random string as
	its ICE ufrag and password, puts it in its own offer and writes the server's
	answer itself. The server learns the string from the first STUN request on
	its mux port and builds the matching description pair.
=============================================================================*/

// IP, UDP, DTLS and SCTP headers per data channel message
#define WEBRTC_PACKET_OVERHEAD 90
#define WEBRTC_MAX_PACKET 512
#define WEBRTC_MAX_INBOX 256

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
