// WebRTC data channel peers for UTcpNetDriver, on libdatachannel.

// The standard and libdatachannel headers go before the engine's, whose macros (check, clock, min, max) break them.
// The engine builds with /Zp4, libdatachannel with the default packing its structs need here too.
#define NOMINMAX
#pragma pack(push, 8)
#include <rtc/rtc.hpp>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <algorithm>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <random>
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

	// A client of a browser host sends its offer once gathered and waits for the answer
	FString SignalId;
	FWebMasterRequest* Offer = nullptr;
	DOUBLE GatherTime = 0;

	// Filled on libdatachannel threads
	std::mutex Lock;
	std::deque<rtc::binary> Inbox;
	bool Closed = false;
};

struct FWebRtcHost
{
	std::unique_ptr<rtc::IceUdpMuxListener> Listener;
	// Joins this server through the web master's relay to see whether the port is open from outside
	std::shared_ptr<FWebRtcPeer> Probe;
	std::optional<std::string> BindAddress;
	std::string CertPem, KeyPem;
	uint16_t Port = 0;
	std::vector<std::shared_ptr<FWebRtcPeer>> Peers;

	std::mutex Lock;
	std::vector<rtc::IceUdpMuxRequest> Requests;
};
#pragma pack(pop)

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

static void WireCallbacks( const std::shared_ptr<FWebRtcPeer>& Peer )
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
		[Weak]( rtc::binary Data )
		{
			if( auto P = Weak.lock() )
			{
				std::lock_guard<std::mutex> Guard( P->Lock );
				if( P->Inbox.size() < WEBRTC_MAX_INBOX )
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
	// ICE only picks the relay when no direct path works.
	// The URL constructor parses with std::regex, which links only with the STL vcpkg built against
	if( Turn && *Turn->Host )
		Config.iceServers.emplace_back( std::string(Turn->Host), (uint16_t)Turn->Port, std::string(Turn->Username), std::string(Turn->Password), rtc::IceServer::RelayType::TurnUdp );
	Peer->Pc = std::make_shared<rtc::PeerConnection>( Config );
	// The channel has to exist before the offer, or the offer has no application section
	Peer->Dc = Peer->Pc->createDataChannel( "", UnreliableChannel() );
	WireCallbacks( Peer );
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

// Starts a client peer for the WebRTC details in the URL, null with Error set when that fails.
// A signaled peer gets its connection in WebRtcSignal, the library takes the STUN server only when it creates one.
static std::shared_ptr<FWebRtcPeer> ConnectPeer( FURL& ConnectURL, FString& Error, const FWebMasterTurn* Relay = nullptr )
{
	auto Peer = std::make_shared<FWebRtcPeer>();
	Peer->CreatedTime = appSeconds();
	if( WebRtcSignalSetup( ConnectURL, Peer->SignalId, Peer->Addr ) )
		return Peer;
	std::string Answer;
	if( !WebRtcClientSetup( ConnectURL, Error, Peer->Addr, Peer->Ufrag, Answer ) )
		return nullptr;
	try
	{
		MakeClientPeer( Peer, rtc::Configuration(), Relay );
		Peer->Pc->setLocalDescription( rtc::Description::Type::Offer, rtc::LocalDescriptionInit{ Peer->Ufrag, Peer->Ufrag } );
		Peer->Pc->setRemoteDescription( rtc::Description( Answer, rtc::Description::Type::Answer ) );
	}
	catch( const std::exception& E )
	{
		Error = FString::Printf( TEXT("WebRTC connect failed: %s"), *FString(E.what()) );
		debugf( TEXT("%s"), *Error );
		ClosePeer( Peer );
		return nullptr;
	}
	return Peer;
}

UBOOL UTcpNetDriver::WebRtcConnect( FURL& ConnectURL, FString& Error )
{
	guard(UTcpNetDriver::WebRtcConnect);
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

static void WebRtcSignal( const std::shared_ptr<FWebRtcPeer>& Shared )
{
	FWebRtcPeer& Peer = *Shared;
	try
	{
		if( !Peer.Pc )
		{
			FWebMasterTurn Turn;
			if( !WebMasterTurn( Turn ) )
				return;
			rtc::Configuration Config;
			if( *Turn.StunHost )
				Config.iceServers.emplace_back( std::string(Turn.StunHost), (uint16_t)Turn.StunPort );
			MakeClientPeer( Shared, Config, &Turn );
			Peer.Pc->setLocalDescription( rtc::Description::Type::Offer );
			Peer.GatherTime = appSeconds();
			return;
		}
		if( !Peer.Offer )
		{
			auto Local = Peer.Pc->localDescription();
			if( Local && (Peer.Pc->gatheringState()==rtc::PeerConnection::GatheringState::Complete || appSeconds()-Peer.GatherTime > WEBRTC_GATHER_TIME) )
				Peer.Offer = WebMasterOffer( Peer.SignalId, FString( std::string(*Local).c_str() ) );
			return;
		}
		UBOOL Ok;
		FString Answer;
		if( !WebMasterOfferDone( Peer.Offer, Ok, Answer ) )
			return;
		WebMasterRequestFree( Peer.Offer );
		Peer.Offer = nullptr;
		Peer.SignalId = TEXT("");
		// The answer is as large as the web master makes it, so it must not go through the stack
		if( Ok )
			Peer.Pc->setRemoteDescription( rtc::Description( std::string(&appToUtf8(*Answer)(0)), rtc::Description::Type::Answer ) );
		else
		{
			debugf( TEXT("WebRTC: the host did not answer through the web master") );
			std::lock_guard<std::mutex> Guard( Peer.Lock );
			Peer.Closed = true;
		}
	}
	catch( const std::exception& E )
	{
		debugf( TEXT("WebRTC signaling failed: %s"), *FString(E.what()) );
		// Or a connection that could not be made would be tried again every tick
		Peer.SignalId = TEXT("");
		std::lock_guard<std::mutex> Guard( Peer.Lock );
		Peer.Closed = true;
	}
}

// A try's ufrag stays for the process. A late request of a closed probe would otherwise become a player peer,
// whose answer opens the router for the relay and makes the next try read reachable.
static INT ProbeTriesLeft = WEBRTC_PROBE_TRIES;
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
			ProbeFinish( Host, TEXT("WebRTC: UDP port %i is not reachable from the internet. Forward it and allow it in the firewall for web players, the game port is not tested.") );
		else
		{
			ClosePeer( Host->Probe );
			Host->Probe.reset();
		}
	}
}

void UTcpNetDriver::WebRtcTick()
{
	guard(UTcpNetDriver::WebRtcTick);
	FWebRtcHost* Host = WebRtc;
	if( !Host )
		return;

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

		// The same limits as new UDP connections, each peer costs a DTLS handshake and SCTP state
		INT Pending = 0, SameIP = 0, Evict = -1;
		bool Known = false, EvictUnproven = false;
		for( INT i=0; i<(INT)Host->Peers.size(); i++ )
		{
			FWebRtcPeer& Other = *Host->Peers[i];
			Known |= Other.Ufrag==Request.localUfrag;
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
		if( Known || WebRtcIpLimited( this, Addr, SameIP ) )
			continue;
		// A peer gets time for its handshake before it can be pushed out, which also bounds what a flood of requests costs
		if( Pending>=WEBRTC_MAX_PENDING && GCurrentTime-Host->Peers[Evict]->CreatedTime<WEBRTC_MIN_PENDING_TIME )
			continue;
		// A peer made while the level switches would die with this driver, the client's ICE checks retry against the next one
		if( Notify->NotifyAcceptingConnection()!=ACCEPTC_Accept )
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
			Config.bindAddress = Host->BindAddress;
			Config.portRangeBegin = Config.portRangeEnd = Host->Port;
			Config.disableAutoNegotiation = true;
			Config.disableFingerprintVerification = true;
			Config.maxMessageSize = WEBRTC_MAX_MESSAGE;
			Config.certificatePemFile = Host->CertPem;
			Config.keyPemFile = Host->KeyPem;
			Peer->Pc = std::make_shared<rtc::PeerConnection>( Config );
			Peer->Dc = Peer->Pc->createDataChannel( "", UnreliableChannel() );
			WireCallbacks( Peer );
			Peer->Pc->setRemoteDescription( rtc::Description( Offer, rtc::Description::Type::Offer ) );
			Peer->Pc->setLocalDescription( rtc::Description::Type::Answer, rtc::LocalDescriptionInit{ Ufrag, Ufrag } );
		}
		catch( const std::exception& E )
		{
			debugf( NAME_DevNet, TEXT("WebRTC peer %s failed: %s"), *IpString(Addr.sin_addr,Request.remotePort), *FString(E.what()) );
			ClosePeer( Peer );
			continue;
		}
		// Refusing the new peer instead would let requests from made up addresses hold every slot against real clients
		if( Pending>=WEBRTC_MAX_PENDING )
		{
			ClosePeer( Host->Peers[Evict] );
			Host->Peers.erase( Host->Peers.begin()+Evict );
		}
		Peer->CreatedTime = GCurrentTime;
		Host->Peers.push_back( Peer );
	}
	if( Host->Listener && ProbeTriesLeft )
		WebRtcProbe( Host, WebRtcDetails );

	// Backwards, because closing removes peers
	for( INT i=(INT)Host->Peers.size()-1; i>=0 && WebRtc; i-- )
	{
		std::shared_ptr<FWebRtcPeer> Peer = Host->Peers[i];
		if( Peer->SignalId.Len() )
			WebRtcSignal( Peer );
		std::deque<rtc::binary> Inbox;
		bool Closed;
		{
			std::lock_guard<std::mutex> Guard( Peer->Lock );
			Inbox.swap( Peer->Inbox );
			Closed = Peer->Closed;
		}

		while( !Peer->Connection && !ServerConnection && !Inbox.empty() && WebRtcAnswerQuery( Peer.get(), Peer->Queries, (const BYTE*)Inbox.front().data(), (INT)Inbox.front().size() ) )
			Inbox.pop_front();

		if( !Peer->Connection && !Inbox.empty() && !ServerConnection && WebRtcAccept( this, Peer.get(), Peer->Addr, Peer->Connection )==ACCEPTC_Reject )
			Closed = true;

		if( Peer->Connection )
		{
			for( auto& Data : Inbox )
				if( Data.size() <= WEBRTC_MAX_PACKET && Peer->Connection->State!=USOCK_Closed )
					Peer->Connection->ReceivedRawPacket( Data.data(), (INT)Data.size() );
			if( Closed )
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
	if( Host->Listener )
		Host->Listener->stop();
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
	if( Peer.SignalId.Len() )
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

