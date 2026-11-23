// WebRTC data channel peers for UTcpNetDriver, on libdatachannel and in a browser on datachannel-wasm,
// which gives the browser's own WebRTC the same interface.

// The standard and libdatachannel headers go before the engine's, whose macros (check, clock, min, max) break them.
// The engine builds with /Zp4, libdatachannel with the default packing its structs need here too.
#define NOMINMAX
#pragma pack(push, 8)
#include <rtc/rtc.hpp>
#if __EMSCRIPTEN__
#include <emscripten.h>
#else
#if _WIN32
// inet_pton, the engine's winsock.h has none
#include <ws2tcpip.h>
#endif
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <random>
#endif
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>
#pragma pack(pop)

#include "UnIpDrv.h"
#include "UnTcpNetDriver.h"

#include "WebRtc.h"

// The mutexes inside need their natural alignment, which /Zp4 breaks on x64
#pragma pack(push, 8)
struct FWebRtcPeer
{
	std::shared_ptr<rtc::PeerConnection> Pc;
	std::shared_ptr<rtc::DataChannel> Dc;
	std::string Ufrag;
	sockaddr_in Addr;
	UWebRtcConnection* Connection = nullptr;
	DOUBLE CreatedTime = 0;
	INT Queries = 0;

	// A client of a host on the web master's socket sends its offer once gathered and waits for the answer
	FString SignalId;
	FWebMasterRequest* Offer = nullptr;
	DOUBLE GatherTime = 0;
	DOUBLE RetryTime = 0;
	bool HostGone = false;
	// A page's join by port and fingerprint waits for the web master's ICE servers with the answer it made up
	std::string Answer;
	// The web master's offer this peer still owes its answer to
	std::string AnswerId;

	// Filled from the library's callbacks, natively on its threads
	std::mutex Lock;
	std::deque<rtc::binary> Inbox;
	bool Closed = false;
};

struct FWebRtcHost
{
	// Joins this server through the web master's relay to see whether the port is open from outside
	std::shared_ptr<FWebRtcPeer> Probe;
	std::optional<std::string> BindAddress;
	std::string CertPem, KeyPem;
	std::vector<std::shared_ptr<FWebRtcPeer>> Peers;

#if !__EMSCRIPTEN__
	uint16_t Port = 0;
	std::unique_ptr<rtc::IceUdpMuxListener> Listener;
	std::mutex Lock;
	std::vector<rtc::IceUdpMuxRequest> Requests;
#endif
};

struct FWebRtcOffer
{
	std::string Id, Sdp, Ip;
	std::chrono::steady_clock::time_point Time;
};

// A page, and a server whose WebRTC port the probe found closed, keeps a socket to the web master, which lists it as
// signal:<id> and passes each client's offer down the socket.
struct FWebRtcHostSocket
{
	std::shared_ptr<rtc::WebSocket> Socket;
	DOUBLE RetryTime = 0, OfferTime = 0;
	enum { New, Listed, Failing } Logged = New;

	// Filled from the library's callbacks, natively on its threads
	std::mutex Lock;
	std::string Id;
	std::deque<FWebRtcOffer> Offers;
};
#pragma pack(pop)

// Never destroyed, and its callbacks touch nothing else. A host and its driver go with every level, the id has to outlive them.
static FWebRtcHostSocket* HostSocket;

// Longer messages are dropped on arrival, queued they would hold memory until the game thread got to them.
static void WireCallbacks( const std::shared_ptr<FWebRtcPeer>& Peer, size_t MaxMessage )
{
	std::weak_ptr<FWebRtcPeer> Weak = Peer;
	auto MarkClosed = [Weak]()
	{
		if( auto P = Weak.lock() )
		{
			std::lock_guard<std::mutex> Guard( P->Lock );
			P->Closed = true;
		}
	};
	Peer->Dc->onMessage(
		[Weak, MaxMessage]( rtc::binary Data )
		{
			if( auto P = Weak.lock() )
			{
				std::lock_guard<std::mutex> Guard( P->Lock );
				if( Data.size() <= MaxMessage && P->Inbox.size() < WEBRTC_MAX_INBOX )
					P->Inbox.push_back( std::move(Data) );
			}
		},
		[]( std::string ) {} );
	Peer->Dc->onClosed( MarkClosed );
	Peer->Pc->onStateChange( [MarkClosed]( rtc::PeerConnection::State State )
	{
		if( State==rtc::PeerConnection::State::Failed || State==rtc::PeerConnection::State::Closed )
			MarkClosed();
	});
}

static rtc::DataChannelInit UnreliableChannel()
{
	rtc::DataChannelInit Init;
	Init.negotiated = true;
	Init.id = 0;
	Init.reliability.unordered = true;
	Init.reliability.maxRetransmits = 0;
	return Init;
}

static void MakeClientPeer( const std::shared_ptr<FWebRtcPeer>& Peer, rtc::Configuration Config, const FWebMasterTurn* Turn )
{
	Config.disableAutoNegotiation = true;
	Config.maxMessageSize = WEBRTC_MAX_MESSAGE;
	// ICE only picks the relay when no direct path works
	if( Turn && *Turn->Host )
		Config.iceServers.emplace_back( std::string(Turn->Host), (uint16_t)Turn->Port, std::string(Turn->Username), std::string(Turn->Password), rtc::IceServer::RelayType::TurnUdp );
	if( Turn && *Turn->TlsHost )
		Config.iceServers.emplace_back( std::string(Turn->TlsHost), (uint16_t)Turn->TlsPort, std::string(Turn->Username), std::string(Turn->Password), rtc::IceServer::RelayType::TurnTls );
	Peer->Pc = std::make_shared<rtc::PeerConnection>( Config );
	// The channel has to exist before the offer, or the offer has no application section
	Peer->Dc = Peer->Pc->createDataChannel( "", UnreliableChannel() );
	WireCallbacks( Peer, WEBRTC_MAX_QUERY_REPLY );
}

// A peer of a listening server, for a client that came to its port or sent an offer
static void MakeHostPeer( const std::shared_ptr<FWebRtcPeer>& Peer, FWebRtcHost* Host, rtc::Configuration Config )
{
	Config.bindAddress = Host->BindAddress;
	Config.disableAutoNegotiation = true;
	Config.maxMessageSize = WEBRTC_MAX_MESSAGE;
	Config.certificatePemFile = Host->CertPem;
	Config.keyPemFile = Host->KeyPem;
	Peer->Pc = std::make_shared<rtc::PeerConnection>( Config );
	Peer->Dc = Peer->Pc->createDataChannel( "", UnreliableChannel() );
	WireCallbacks( Peer, WEBRTC_MAX_PACKET );
}

static void ClosePeer( const std::shared_ptr<FWebRtcPeer>& Peer )
{
	if( Peer->Offer )
	{
		WebMasterRequestFree( Peer->Offer );
		Peer->Offer = nullptr;
	}
	try
	{
		if( Peer->Dc )
			Peer->Dc->resetCallbacks();
		if( Peer->Pc )
		{
			Peer->Pc->resetCallbacks();
			Peer->Pc->close();
		}
	}
	catch( const std::exception& E )
	{
		debugf( NAME_DevNet, TEXT("WebRTC close: %s"), *FString(E.what()) );
	}
}

UBOOL WebRtcPeerSend( FWebRtcPeer* Peer, const void* Data, INT Count )
{
	if( !Peer->Dc || !Peer->Dc->isOpen() )
		return 0;
	try
	{
		Peer->Dc->send( (const rtc::byte*)Data, Count );
	}
	catch( const std::exception& )
	{
	}
	return 1;
}

void WebRtcRemovePeer( FWebRtcHost* Host, FWebRtcPeer* Peer )
{
	auto It = std::find_if( Host->Peers.begin(), Host->Peers.end(), [Peer]( const std::shared_ptr<FWebRtcPeer>& P ) { return P.get()==Peer; } );
	if( It!=Host->Peers.end() )
	{
		ClosePeer( *It );
		Host->Peers.erase( It );
	}
}

// The server learns the ufrag from the first STUN request, so the offer carries the one its answer was written for
static void OfferDirect( const std::shared_ptr<FWebRtcPeer>& Peer, rtc::Configuration Config, const FWebMasterTurn* Turn )
{
	MakeClientPeer( Peer, Config, Turn );
	Peer->Pc->setLocalDescription( rtc::Description::Type::Offer, rtc::LocalDescriptionInit{ Peer->Ufrag, Peer->Ufrag } );
	Peer->Pc->setRemoteDescription( rtc::Description( Peer->Answer, rtc::Description::Type::Answer ) );
	Peer->Answer.clear();
}

// Starts a client peer for the WebRTC details in the URL, null with Error set when that fails.
// A signaled peer gets its connection in WebRtcSignal, the library takes the STUN server only when it creates one.
static std::shared_ptr<FWebRtcPeer> ConnectPeer( FURL& ConnectURL, FString& Error, const FWebMasterTurn* Relay = nullptr )
{
	auto Peer = std::make_shared<FWebRtcPeer>();
	Peer->CreatedTime = appSeconds();
	if( WebRtcSignalSetup( ConnectURL, Peer->SignalId, Peer->Addr ) )
		return Peer;
	if( !WebRtcClientSetup( ConnectURL, Error, Peer->Addr, Peer->Ufrag, Peer->Answer ) )
		return nullptr;
	// A page has no other way out of a network that only lets TLS through, so its peer waits in WebRtcSignal for the web master's relay
#if !__EMSCRIPTEN__
	try
	{
		OfferDirect( Peer, rtc::Configuration(), Relay );
	}
	catch( const std::exception& E )
	{
		Error = FString::Printf( TEXT("WebRTC connect failed: %s"), *FString(E.what()) );
		debugf( TEXT("%s"), *Error );
		ClosePeer( Peer );
		return nullptr;
	}
#endif
	return Peer;
}

UBOOL UTcpNetDriver::WebRtcConnect( FURL& ConnectURL, FString& Error )
{
	guard(UTcpNetDriver::WebRtcConnect);
#if __EMSCRIPTEN__
	if( !EM_ASM_INT({ return typeof RTCPeerConnection != 'undefined' }) )
	{
		// The dialog cuts a longer line off at a small window.
		Error = TEXT("Joining needs WebRTC, which this browser has turned off");
		return 0;
	}
#endif
	std::shared_ptr<FWebRtcPeer> Peer = ConnectPeer( ConnectURL, Error );
	if( !Peer )
		return 0;

	// A UDP connection that got no answer is replaced, it must still be the ServerConnection when it goes
	if( ServerConnection )
		delete ServerConnection;
	FWebRtcHost* Host = new FWebRtcHost;
	Host->Peers.push_back( Peer );
	WebRtc = Host;

	WebRtcServerConnection( this, Peer.get(), Peer->Addr, Peer->Connection, ConnectURL );
	return 1;
	unguard;
}

// The local description once it has its candidates, or what was gathered when the wait is over
static std::optional<rtc::Description> GatheredDescription( FWebRtcPeer& Peer )
{
	auto Local = Peer.Pc->localDescription();
	if( Local && (Peer.Pc->gatheringState()==rtc::PeerConnection::GatheringState::Complete || appSeconds()-Peer.GatherTime > WEBRTC_GATHER_TIME) )
		return Local;
	return std::nullopt;
}

static void WebRtcSignal( const std::shared_ptr<FWebRtcPeer>& Shared )
{
	FWebRtcPeer& Peer = *Shared;
	try
	{
		if( !Peer.Pc )
		{
			FWebMasterTurn Turn;
			if( appSeconds() < Peer.RetryTime || !WebMasterTurn( Turn ) )
				return;
			rtc::Configuration Config;
			if( *Turn.StunHost )
				Config.iceServers.emplace_back( std::string(Turn.StunHost), (uint16_t)Turn.StunPort );
			if( !Peer.Answer.empty() )
			{
				OfferDirect( Shared, Config, &Turn );
				return;
			}
			MakeClientPeer( Shared, Config, &Turn );
			Peer.Pc->setLocalDescription( rtc::Description::Type::Offer );
			Peer.GatherTime = appSeconds();
			return;
		}
		if( !Peer.Offer )
		{
			if( auto Local = GatheredDescription( Peer ) )
				Peer.Offer = WebMasterOffer( Peer.SignalId, FString( std::string(*Local).c_str() ) );
			return;
		}
		UBOOL Ok, Gone;
		FString Answer;
		if( !WebMasterOfferDone( Peer.Offer, Ok, Answer, Gone ) )
			return;
		Peer.HostGone = Gone;
		WebMasterRequestFree( Peer.Offer );
		Peer.Offer = nullptr;
		Peer.SignalId = TEXT("");
		// The answer is as large as the web master makes it, so it must not go through the stack
		if( Ok )
			Peer.Pc->setRemoteDescription( rtc::Description( std::string(&appToUtf8(*Answer)(0)), rtc::Description::Type::Answer ) );
		else
		{
			debugf( NAME_DevNet, TEXT("WebRTC: the host did not answer through the web master") );
			std::lock_guard<std::mutex> Guard( Peer.Lock );
			Peer.Closed = true;
		}
	}
	catch( const std::exception& E )
	{
		debugf( TEXT("WebRTC signaling failed: %s"), *FString(E.what()) );
		// Or a connection that could not be made would be tried again every tick
		Peer.SignalId = TEXT("");
		Peer.Answer.clear();
		std::lock_guard<std::mutex> Guard( Peer.Lock );
		Peer.Closed = true;
	}
}

// Whether WebRtcSignal still has a step to take for the peer
static bool Signaling( const FWebRtcPeer& Peer )
{
	return Peer.SignalId.Len() || !Peer.Answer.empty();
}

std::string WebRtcJsonString( const nlohmann::json& Object, const char* Key, size_t Max )
{
	auto It = Object.find( Key );
	return It!=Object.end() && It->is_string() && It->get_ref<const std::string&>().size()<=Max ? It->get<std::string>() : std::string();
}

// Natively on a libdatachannel thread, so it only touches standard types
static void HostSocketMessage( const std::string& Text )
{
	nlohmann::json Message = nlohmann::json::parse( Text, nullptr, false );
	if( !Message.is_object() )
		return;
	std::string Type = WebRtcJsonString(Message, "type", 16 ), Id = WebRtcJsonString(Message, "id", 64 ), Sdp = WebRtcJsonString(Message, "sdp", WEBRTC_MAX_SDP );
	if( Type=="link" )
		return WebMasterLinkReceived( Text );
	std::lock_guard<std::mutex> Guard( HostSocket->Lock );
	if( Type=="id" && WebRtcHostIdValid( Id.c_str() ) )
		HostSocket->Id = Id;
	else if( Type=="offer" && !Id.empty() && !Sdp.empty() )
	{
		// The oldest has waited longest for its level and is the first to be too late
		if( HostSocket->Offers.size()>=WEBRTC_MAX_PENDING )
			HostSocket->Offers.pop_front();
		HostSocket->Offers.push_back( { Id, Sdp, WebRtcJsonString(Message, "ip", 64 ), std::chrono::steady_clock::now() } );
	}
}

static void HostSocketOpen()
{
	// Also when no socket could be made, or that would be tried every tick
	HostSocket->RetryTime = appSeconds() + WEBRTC_HOST_SOCKET_RETRY;
	try
	{
		rtc::WebSocket::Configuration Config;
		// Without a limit a dead link is only noticed when TCP gives up
		Config.maxOutstandingPings = 2;
		Config.maxMessageSize = WEBRTC_HOST_SOCKET_MAX_MESSAGE;
#if __ANDROID__
		Config.caCertificatePemFile = AndroidCaCerts();
#endif
		HostSocket->Socket = std::make_shared<rtc::WebSocket>( Config );
		HostSocket->Socket->onMessage( []( rtc::binary ) {}, []( std::string Text ) { HostSocketMessage( Text ); } );
		// http becomes ws and https becomes wss
		HostSocket->Socket->open( "ws" + std::string( &appToUtf8( *WebMasterURL() )(0) ).substr( 4 ) + "/host" );
	}
	catch( const std::exception& E )
	{
		debugf( NAME_DevNet, TEXT("WebRTC: the web master socket failed: %s"), *FString(E.what()) );
	}
}

static void HostSocketSend( const std::string& Text )
{
	try
	{
		if( HostSocket->Socket && HostSocket->Socket->isOpen() )
			HostSocket->Socket->send( Text );
	}
	catch( const std::exception& E )
	{
		debugf( NAME_DevNet, TEXT("WebRTC: sending to the web master failed: %s"), *FString(E.what()) );
	}
}

UBOOL WebRtcHostState( const char* Json )
{
	if( !HostSocket )
		return 0;
	HostSocketSend( std::string("{\"type\":\"state\",\"server\":") + Json + "}" );
	return 1;
}

static INT ProbeTriesLeft = WEBRTC_PROBE_TRIES;

void WebRtcHostThroughWebMaster()
{
	ProbeTriesLeft = 0;
	if( !HostSocket )
		HostSocket = new FWebRtcHostSocket;
}

FString WebRtcListedDetails( const FString& Own )
{
	// A port that turns out closed would be listed for minutes with details nobody can use
	if( ProbeTriesLeft )
		return FString();
	if( !HostSocket )
		return Own;
	std::lock_guard<std::mutex> Guard( HostSocket->Lock );
	return HostSocket->Id.empty() ? FString() : FString(TEXT("signal:")) + FString( HostSocket->Id.c_str() );
}

// The same limits as new UDP connections, each peer costs a DTLS handshake and SCTP state.
// Evict is the pending peer that has to go when this one is added, -1 for none. A peer from an offer has no ufrag.
static bool AdmitPeer( UTcpNetDriver* Driver, FWebRtcHost* Host, sockaddr_in& Addr, const std::string& Ufrag, INT& Evict )
{
	INT Pending = 0, SameIP = 0;
	bool Known = false, EvictUnproven = false;
	Evict = -1;
	for( INT i=0; i<(INT)Host->Peers.size(); i++ )
	{
		FWebRtcPeer& Other = *Host->Peers[i];
		Known |= !Ufrag.empty() && Other.Ufrag==Ufrag;
		if( !Other.Connection )
		{
			Pending++;
			SameIP += IpAddressMatches( Other.Addr, Addr );
			// Peers are in creation order. The oldest whose address ICE has not proven goes first, spoofed requests never get further.
			bool Unproven = Other.Pc->state()!=rtc::PeerConnection::State::Connected;
			if( Evict<0 || (Unproven && !EvictUnproven) )
			{
				Evict = i;
				EvictUnproven = Unproven;
			}
		}
	}
	if( Known || WebRtcIpLimited( Driver, Addr, SameIP ) )
		return false;
	// A peer gets time for its handshake before it can be pushed out, which also bounds what a flood of requests costs
	if( Pending>=WEBRTC_MAX_PENDING && GCurrentTime-Host->Peers[Evict]->CreatedTime<WEBRTC_MIN_PENDING_TIME )
		return false;
	if( Pending<WEBRTC_MAX_PENDING )
		Evict = -1;
	return true;
}

// Refusing the new peer instead of evicting would let requests from made up addresses hold every slot against real clients
static void AddPeer( FWebRtcHost* Host, const std::shared_ptr<FWebRtcPeer>& Peer, INT Evict )
{
	if( Evict>=0 )
	{
		ClosePeer( Host->Peers[Evict] );
		Host->Peers.erase( Host->Peers.begin()+Evict );
	}
	Peer->CreatedTime = GCurrentTime;
	Host->Peers.push_back( Peer );
}

#if !__EMSCRIPTEN__

// v1 sets the ICE password to the ufrag, so the ufrag also has to satisfy the password rules (RFC 8839)
static bool ValidUfrag( const std::string& Ufrag )
{
	if( Ufrag.size()<22 || Ufrag.size()>256 )
		return false;
	for( char C : Ufrag )
		if( !isalnum((unsigned char)C) && C!='+' && C!='/' )
			return false;
	return true;
}

// One certificate per process, so the advertised fingerprint survives map changes
static bool MakeCertificate( std::string& CertPem, std::string& KeyPem, std::string& Fingerprint )
{
	static std::string Cert, Key, Fp;
	if( Fp.empty() )
	{
		EVP_PKEY* PKey = EVP_EC_gen("P-256");
		X509* X = X509_new();
		bool Ok = PKey && X;
		if( Ok )
		{
			std::random_device Random;
			ASN1_INTEGER_set( X509_get_serialNumber(X), Random() & 0x7fffffff );
			X509_gmtime_adj( X509_getm_notBefore(X), -3600 );
			X509_gmtime_adj( X509_getm_notAfter(X), 365L*24*3600 );
			X509_set_pubkey( X, PKey );
			X509_NAME* Name = X509_get_subject_name( X );
			X509_NAME_add_entry_by_txt( Name, "CN", MBSTRING_ASC, (const unsigned char*)"ut2004", -1, -1, 0 );
			X509_set_issuer_name( X, Name );
			Ok = X509_sign( X, PKey, EVP_sha256() ) > 0;
		}
		BIO* CertBio = BIO_new( BIO_s_mem() );
		BIO* KeyBio = BIO_new( BIO_s_mem() );
		unsigned char Md[EVP_MAX_MD_SIZE];
		unsigned int MdLen = 0;
		Ok = Ok && PEM_write_bio_X509( CertBio, X ) && PEM_write_bio_PrivateKey( KeyBio, PKey, NULL, NULL, 0, NULL, NULL )
			&& X509_digest( X, EVP_sha256(), Md, &MdLen );
		if( Ok )
		{
			char* P;
			long Len = BIO_get_mem_data( CertBio, &P );
			Cert.assign( P, Len );
			Len = BIO_get_mem_data( KeyBio, &P );
			Key.assign( P, Len );
			static const char Hex[] = "0123456789ABCDEF";
			for( unsigned int i=0; i<MdLen; i++ )
			{
				if( i )
					Fp += ':';
				Fp += Hex[Md[i]>>4];
				Fp += Hex[Md[i]&15];
			}
		}
		BIO_free( CertBio );
		BIO_free( KeyBio );
		X509_free( X );
		EVP_PKEY_free( PKey );
		if( !Ok )
			return false;
	}
	CertPem = Cert;
	KeyPem = Key;
	Fingerprint = Fp;
	return true;
}

// Leaves a description the candidates Keep accepts, as many as the ICE library takes
template<class F> static void KeepCandidates( rtc::Description& Description, F Keep )
{
	INT Left = WEBRTC_MAX_CANDIDATES;
	for( rtc::Candidate& Candidate : Description.extractCandidates() )
		if( Left>0 && Keep( Candidate ) )
		{
			Description.addCandidate( std::move(Candidate) );
			Left--;
		}
}

// LAN, link-local, carrier NAT and loopback addresses. They describe the server's network and reach nobody outside it.
static bool PrivateAddress( const std::string& Text )
{
	BYTE A[16];
	if( inet_pton( AF_INET, Text.c_str(), A )==1 )
		return A[0]==10 || A[0]==127 || (A[0]==172 && (A[1]&0xF0)==16) || (A[0]==192 && A[1]==168) || (A[0]==169 && A[1]==254) || (A[0]==100 && (A[1]&0xC0)==64);
	return inet_pton( AF_INET6, Text.c_str(), A )==1 && (A[0]==0 || (A[0]&0xFE)==0xFC || (A[0]==0xFE && (A[1]&0xC0)==0x80));
}

// The library starts a thread and a lookup for each candidate that is a name, which a browser's are for its LAN addresses
static void OfferCandidates( rtc::Description& Offer )
{
	KeepCandidates( Offer, []( rtc::Candidate& Candidate ) { return Candidate.resolve( rtc::Candidate::ResolveMode::Simple ); } );
}

// The answer goes to anyone who asks, so it does not name the server's LAN addresses
static void AnswerCandidates( rtc::Description& Answer )
{
	KeepCandidates( Answer, []( rtc::Candidate& Candidate )
	{
		return Candidate.type()!=rtc::Candidate::Type::Host || (Candidate.address() && !PrivateAddress( *Candidate.address() ));
	});
}

void UTcpNetDriver::WebRtcListen()
{
	guard(UTcpNetDriver::WebRtcListen);
	FWebRtcHost* Host = new FWebRtcHost;
	std::string Fingerprint;
	if( !MakeCertificate( Host->CertPem, Host->KeyPem, Fingerprint ) )
	{
		debugf( TEXT("WebRTC: certificate generation failed, UDP only") );
		delete Host;
		return;
	}
	Host->Port = ntohs(LocalAddr.sin_port) + 2;
	if( LocalAddr.sin_addr.s_addr != INADDR_ANY )
		Host->BindAddress = std::string( TCHAR_TO_ANSI(*IpString(LocalAddr.sin_addr)) );
	try
	{
		Host->Listener = std::make_unique<rtc::IceUdpMuxListener>( Host->Port, Host->BindAddress );
	}
	catch( const std::exception& E )
	{
		debugf( TEXT("WebRTC: listening on port %i failed (%s), UDP only"), Host->Port, *FString(E.what()) );
		delete Host;
		return;
	}
	// Runs on the mux thread, the game thread picks the requests up in WebRtcTick
	Host->Listener->OnUnhandledStunRequest( [Host]( rtc::IceUdpMuxRequest Request )
	{
		if( Request.localUfrag==Request.remoteUfrag && ValidUfrag(Request.localUfrag) )
		{
			std::lock_guard<std::mutex> Guard( Host->Lock );
			if( Host->Requests.size() < WEBRTC_MAX_PENDING )
				Host->Requests.push_back( std::move(Request) );
		}
	});
	WebRtc = Host;
	WebRtcDetails = FString::Printf( TEXT("%i,%s"), Host->Port, *FString(Fingerprint.c_str()) );
	debugf( TEXT("WebRTC on port %i, fingerprint %s"), Host->Port, *FString(Fingerprint.c_str()) );
	unguard;
}

// A try's ufrag stays for the process. A late request of a closed probe would otherwise become a player peer,
// whose answer opens the router for the relay and makes the next try read reachable.
static std::vector<std::string> ProbeUfrags;

// Only a request from one of these came through the relay, the probe's direct ones prove nothing
static std::vector<std::string> ProbeRelayAddresses( FWebRtcHost* Host )
{
	std::vector<std::string> Addresses;
	if( auto Local = Host->Probe->Pc->localDescription() )
		for( const rtc::Candidate& Candidate : Local->candidates() )
			if( auto Address = Candidate.address(); Address && Candidate.type()==rtc::Candidate::Type::Relayed )
				Addresses.push_back( *Address );
	return Addresses;
}

static void ProbeFinish( FWebRtcHost* Host, const TCHAR* Result )
{
	debugf( Result, Host->Port );
	if( Host->Probe )
		ClosePeer( Host->Probe );
	Host->Probe.reset();
	ProbeTriesLeft = 0;
}

static void WebRtcProbe( FWebRtcHost* Host, const FString& Details )
{
	const TCHAR* Unknown = TEXT("WebRTC: UDP port %i could not be tested from the internet");
	if( !Host->Probe )
	{
		// Only a server the web master lists is joined from outside. A missing key leaves the script default.
		UBOOL DoUplink = 1;
		GConfig->GetBool( TEXT("IpDrv.MasterServerUplink"), TEXT("DoUplink"), DoUplink );
		FWebMasterTurn Turn;
		if( !DoUplink || !WebMasterURL().Len() || !WebMasterTurn( Turn ) )
			return;
		FURL URL = WebRtcQueryURL( FString(Turn.Ip), Details );
		FString Error;
		if( *Turn.Host && *Turn.Ip )
			Host->Probe = ConnectPeer( URL, Error, &Turn );
		if( Host->Probe )
			ProbeUfrags.push_back( Host->Probe->Ufrag );
		else
			ProbeFinish( Host, Unknown );
	}
	else if( appSeconds() - Host->Probe->CreatedTime > WEBRTC_PROBE_TIME )
	{
		if( ProbeRelayAddresses( Host ).empty() )
			ProbeFinish( Host, Unknown );
		else if( --ProbeTriesLeft == 0 )
		{
			ProbeFinish( Host, TEXT("WebRTC: UDP port %i is not reachable from the internet, hosting through the web master instead. Forward it and restart for direct connections, the game port is not tested.") );
			WebRtcHostThroughWebMaster();
		}
		else
		{
			ClosePeer( Host->Probe );
			Host->Probe.reset();
		}
	}
}

static void PortTick( UTcpNetDriver* Driver, FWebRtcHost* Host )
{
	std::vector<rtc::IceUdpMuxRequest> Requests;
	{
		std::lock_guard<std::mutex> Guard( Host->Lock );
		Requests.swap( Host->Requests );
	}
	for( auto& Request : Requests )
	{
		if( std::find( ProbeUfrags.begin(), ProbeUfrags.end(), Request.localUfrag ) != ProbeUfrags.end() )
		{
			if( Host->Probe )
			{
				std::vector<std::string> Relay = ProbeRelayAddresses( Host );
				if( std::find( Relay.begin(), Relay.end(), Request.remoteAddress ) != Relay.end() )
					ProbeFinish( Host, TEXT("WebRTC: UDP port %i is reachable from the internet") );
			}
			continue;
		}
		sockaddr_in Addr;
		appMemzero( &Addr, sizeof(Addr) );
		Addr.sin_family = AF_INET;
		Addr.sin_port = htons( Request.remotePort );
		Addr.sin_addr.s_addr = inet_addr( Request.remoteAddress.c_str() );
		if( Addr.sin_addr.s_addr==INADDR_NONE )
			continue;

		INT Evict;
		if( !AdmitPeer( Driver, Host, Addr, Request.localUfrag, Evict ) )
			continue;
		// A peer made while the level switches would die with this driver, the client's ICE checks retry against the next one
		if( Driver->Notify->NotifyAcceptingConnection()!=ACCEPTC_Accept )
			continue;

		const std::string& Ufrag = Request.localUfrag;
		const std::string& Ip = Request.remoteAddress;
		std::string Offer =
			"v=0\r\no=- 0 0 IN IP4 " + Ip + "\r\ns=-\r\nc=IN IP4 " + Ip + "\r\nt=0 0\r\na=ice-options:ice2,trickle\r\n"
			"m=application " + std::to_string(Request.remotePort) + " UDP/DTLS/SCTP webrtc-datachannel\r\n"
			"a=mid:0\r\na=setup:active\r\na=ice-ufrag:" + Ufrag + "\r\na=ice-pwd:" + Ufrag + "\r\n"
			// The client's certificate is unknown and not checked, any sha-256 value passes the parser
			"a=fingerprint:sha-256 00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00\r\n"
			+ WebRtcSdpEnd + WebRtcCandidate( Ip, Request.remotePort );

		auto Peer = std::make_shared<FWebRtcPeer>();
		Peer->Addr = Addr;
		Peer->Ufrag = Ufrag;
		try
		{
			rtc::Configuration Config;
			Config.enableIceUdpMux = true;
			Config.portRangeBegin = Config.portRangeEnd = Host->Port;
			Config.disableFingerprintVerification = true;
			MakeHostPeer( Peer, Host, Config );
			Peer->Pc->setRemoteDescription( rtc::Description( Offer, rtc::Description::Type::Offer ) );
			Peer->Pc->setLocalDescription( rtc::Description::Type::Answer, rtc::LocalDescriptionInit{ Ufrag, Ufrag } );
		}
		catch( const std::exception& E )
		{
			debugf( NAME_DevNet, TEXT("WebRTC peer %s failed: %s"), *IpString(Addr.sin_addr,Request.remotePort), *FString(E.what()) );
			ClosePeer( Peer );
			continue;
		}
		AddPeer( Host, Peer, Evict );
	}
	if( Host->Listener && ProbeTriesLeft )
		WebRtcProbe( Host, Driver->WebRtcDetails );
}

#else

// A browser resolves the names in an offer itself and hides its own LAN addresses behind such names
static void OfferCandidates( rtc::Description& ) {}
static void AnswerCandidates( rtc::Description& ) {}

void UTcpNetDriver::WebRtcListen()
{
	guard(UTcpNetDriver::WebRtcListen);
	if( !WebMasterURL().Len() )
	{
		debugf( TEXT("WebRTC: a browser hosts through the web master, set WebMasterServer in [IpDrv.MasterServerLink]") );
		return;
	}
	WebRtcHostThroughWebMaster();
	WebRtc = new FWebRtcHost;
	unguard;
}

static void PortTick( UTcpNetDriver* Driver, FWebRtcHost* Host )
{
	// Empty while the socket has no id, and the uplink then lists nothing
	if( !Driver->ServerConnection )
		Driver->WebRtcDetails = WebRtcListedDetails( FString() );
}

#endif

static void HostSocketTick( UTcpNetDriver* Driver, FWebRtcHost* Host )
{
	FWebRtcHostSocket* S = HostSocket;
	if( !S->Socket || S->Socket->isClosed() )
	{
		if( S->Socket )
		{
			// The old socket's callbacks must be gone before the next one shares the state
			S->Socket->resetCallbacks();
			S->Socket.reset();
			S->RetryTime = appSeconds() + WEBRTC_HOST_SOCKET_RETRY;
			if( S->Logged!=FWebRtcHostSocket::Failing )
				debugf( TEXT("WebRTC: no connection to the web master, trying again every %i seconds"), (INT)WEBRTC_HOST_SOCKET_RETRY );
			S->Logged = FWebRtcHostSocket::Failing;
			// A new socket gets a new id
			std::lock_guard<std::mutex> Guard( S->Lock );
			S->Id.clear();
			S->Offers.clear();
		}
		else if( appSeconds() >= S->RetryTime )
			HostSocketOpen();
		return;
	}
	bool Listed, Offered;
	{
		std::lock_guard<std::mutex> Guard( S->Lock );
		Listed = !S->Id.empty();
		Offered = !S->Offers.empty();
	}
	if( Listed && S->Logged!=FWebRtcHostSocket::Listed )
	{
		debugf( TEXT("WebRTC: connected to the web master, players join through it") );
		S->Logged = FWebRtcHostSocket::Listed;
	}
	if( !Offered || appSeconds() < S->OfferTime )
		return;

	// A level that is switching takes nobody and a peer made now would die with it. The offers wait for the next level,
	// which is in time for some. Asked twice a second, because the level logs each time.
	EAcceptConnection Accept = Driver->Notify->NotifyAcceptingConnection();
	FWebMasterTurn Turn;
	if( Accept==ACCEPTC_Ignore || (Accept==ACCEPTC_Accept && !WebMasterTurn( Turn )) )
	{
		S->OfferTime = appSeconds() + 0.5;
		return;
	}
	std::deque<FWebRtcOffer> Offers;
	{
		std::lock_guard<std::mutex> Guard( S->Lock );
		Offers.swap( S->Offers );
	}
	if( Accept!=ACCEPTC_Accept )
		return;

	for( FWebRtcOffer& Offer : Offers )
	{
		if( std::chrono::steady_clock::now()-Offer.Time > std::chrono::seconds(WEBRTC_OFFER_TIME) )
			continue;
		sockaddr_in Addr;
		appMemzero( &Addr, sizeof(Addr) );
		Addr.sin_family = AF_INET;
		Addr.sin_addr.s_addr = WebRtcPeerIp( Offer.Ip.c_str() );
		INT Evict;
		if( !AdmitPeer( Driver, Host, Addr, std::string(), Evict ) )
			continue;

		auto Peer = std::make_shared<FWebRtcPeer>();
		Peer->Addr = Addr;
		Peer->AnswerId = Offer.Id;
		try
		{
			rtc::Configuration Config;
			if( *Turn.StunHost )
				Config.iceServers.emplace_back( std::string(Turn.StunHost), (uint16_t)Turn.StunPort );
			MakeHostPeer( Peer, Host, Config );
			rtc::Description Remote( Offer.Sdp, rtc::Description::Type::Offer );
			OfferCandidates( Remote );
			Peer->Pc->setRemoteDescription( Remote );
			Peer->Pc->setLocalDescription( rtc::Description::Type::Answer );
			Peer->GatherTime = appSeconds();
		}
		catch( const std::exception& E )
		{
			debugf( NAME_DevNet, TEXT("WebRTC peer %s failed: %s"), *IpString(Addr.sin_addr), *FString(E.what()) );
			ClosePeer( Peer );
			continue;
		}
		AddPeer( Host, Peer, Evict );
	}
}

void UTcpNetDriver::WebRtcTick()
{
	guard(UTcpNetDriver::WebRtcTick);
	FWebRtcHost* Host = WebRtc;
	if( !Host )
		return;

	PortTick( this, Host );
	if( HostSocket && !ServerConnection )
		HostSocketTick( this, Host );

	// Backwards, because closing removes peers
	for( INT i=(INT)Host->Peers.size()-1; i>=0 && WebRtc; i-- )
	{
		std::shared_ptr<FWebRtcPeer> Peer = Host->Peers[i];
		if( Signaling( *Peer ) )
			WebRtcSignal( Peer );
		if( !Peer->AnswerId.empty() )
			if( auto Local = GatheredDescription( *Peer ) )
			{
				AnswerCandidates( *Local );
				HostSocketSend( nlohmann::json{ { "type", "answer" }, { "id", Peer->AnswerId }, { "sdp", std::string(*Local) } }.dump( -1, ' ', false, nlohmann::json::error_handler_t::replace ) );
				Peer->AnswerId.clear();
			}
		std::deque<rtc::binary> Inbox;
		bool Closed;
		{
			std::lock_guard<std::mutex> Guard( Peer->Lock );
			Inbox.swap( Peer->Inbox );
			Closed = Peer->Closed;
		}
		if( Closed && Inbox.empty() && !Peer->HostGone && WebRtcOfferAgain( this, Peer->Connection, Peer->CreatedTime ) && WebRtcSignalSetup( Peer->Connection->URL, Peer->SignalId, Peer->Addr ) )
		{
			debugf( NAME_DevNet, TEXT("WebRTC: no connection through the web master yet, offering again") );
			ClosePeer( Peer );
			Peer->Pc.reset();
			Peer->Dc.reset();
			Peer->RetryTime = appSeconds() + WEBRTC_OFFER_RETRY;
			std::lock_guard<std::mutex> Guard( Peer->Lock );
			Peer->Closed = Closed = false;
		}

		while( !Peer->Connection && !ServerConnection && !Inbox.empty() && WebRtcAnswerQuery( Peer.get(), Peer->Queries, (const BYTE*)Inbox.front().data(), (INT)Inbox.front().size() ) )
			Inbox.pop_front();

		if( !Peer->Connection && !Inbox.empty() && !ServerConnection && WebRtcAccept( this, Peer.get(), Peer->Addr, Peer->Connection )==ACCEPTC_Reject )
			Closed = true;

		if( Peer->Connection )
		{
			// A packet can end the level and with it this host and the connection
			for( auto& Data : Inbox )
				if( WebRtc && Data.size() <= WEBRTC_MAX_PACKET && Peer->Connection->State!=USOCK_Closed )
					Peer->Connection->ReceivedRawPacket( Data.data(), (INT)Data.size() );
			if( Closed && WebRtc )
				Peer->Connection->State = USOCK_Closed;
		}
		else if( Closed || WebRtcPendingExpired( Peer->CreatedTime, Peer->Queries ) )
		{
			ClosePeer( Peer );
			Host->Peers.erase( Host->Peers.begin()+i );
		}
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
#if !__EMSCRIPTEN__
	if( Host->Listener )
		Host->Listener->stop();
#endif
	if( Host->Probe )
		ClosePeer( Host->Probe );
	for( auto& Peer : Host->Peers )
	{
		if( Peer->Connection )
			Peer->Connection->Peer = NULL;
		ClosePeer( Peer );
	}
	delete Host;
	unguard;
}

// Never destroyed, libdatachannel's own globals may be gone by the time statics are
static std::map<INT,std::shared_ptr<FWebRtcPeer>>& QueryPeers()
{
	static auto* Peers = new std::map<INT,std::shared_ptr<FWebRtcPeer>>;
	return *Peers;
}

INT WebRtcQueryOpen( const FString& Host, const FString& Details )
{
	static INT LastId = 0;
	FURL URL = WebRtcQueryURL( Host, Details );
	FString Error;
	std::shared_ptr<FWebRtcPeer> Peer = ConnectPeer( URL, Error );
	if( !Peer )
		return 0;
	QueryPeers()[++LastId] = Peer;
	return LastId;
}

UBOOL WebRtcQuerySend( INT Id, const BYTE* Data, INT Count )
{
	auto It = QueryPeers().find( Id );
	return It!=QueryPeers().end() && WebRtcPeerSend( It->second.get(), Data, Count );
}

INT WebRtcQueryRecv( INT Id, BYTE* Data, INT Max )
{
	auto It = QueryPeers().find( Id );
	if( It==QueryPeers().end() )
		return -1;
	FWebRtcPeer& Peer = *It->second;
	if( Signaling( Peer ) )
		WebRtcSignal( It->second );
	std::lock_guard<std::mutex> Guard( Peer.Lock );
	while( !Peer.Inbox.empty() )
	{
		rtc::binary Message = std::move( Peer.Inbox.front() );
		Peer.Inbox.pop_front();
		if( !Message.empty() && (INT)Message.size() <= Max )
		{
			appMemcpy( Data, Message.data(), Message.size() );
			return (INT)Message.size();
		}
	}
	return Peer.Closed ? -1 : 0;
}

void WebRtcQueryClose( INT Id )
{
	auto It = QueryPeers().find( Id );
	if( It!=QueryPeers().end() )
	{
		ClosePeer( It->second );
		QueryPeers().erase( It );
	}
}

