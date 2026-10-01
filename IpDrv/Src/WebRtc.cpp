/*=============================================================================
	WebRtc.cpp: WebRTC data channel peers for UTcpNetDriver, on libdatachannel.
=============================================================================*/

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

	// A client of a browser host sends its offer once gathered and waits for the answer
	FString SignalId;
	FWebMasterRequest* Offer = nullptr;

	// Filled on libdatachannel threads
	std::mutex Lock;
	std::deque<rtc::binary> Inbox;
	bool Closed = false;
};

struct FWebRtcHost
{
	std::unique_ptr<rtc::IceUdpMuxListener> Listener;
	std::optional<std::string> BindAddress;
	std::string CertPem, KeyPem;
	uint16_t Port = 0;
	std::vector<std::shared_ptr<FWebRtcPeer>> Peers;

	std::mutex Lock;
	std::vector<rtc::IceUdpMuxRequest> Requests;
};
#pragma pack(pop)

IMPLEMENT_CLASS(UWebRtcConnection);

/*-----------------------------------------------------------------------------
	Helpers.
-----------------------------------------------------------------------------*/

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

static std::shared_ptr<FWebRtcPeer> MakeClientPeer( const rtc::Configuration& Config )
{
	auto Peer = std::make_shared<FWebRtcPeer>();
	Peer->Pc = std::make_shared<rtc::PeerConnection>( Config );
	// The channel has to exist before the offer, or the offer has no application section
	Peer->Dc = Peer->Pc->createDataChannel( "", UnreliableChannel() );
	WireCallbacks( Peer );
	return Peer;
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
		Peer->Dc->resetCallbacks();
		Peer->Pc->resetCallbacks();
		Peer->Pc->close();
	}
	catch( const std::exception& E )
	{
		debugf( NAME_DevNet, TEXT("WebRTC close: %s"), *FString(E.what()) );
	}
}

/*-----------------------------------------------------------------------------
	UWebRtcConnection.
-----------------------------------------------------------------------------*/

void UWebRtcConnection::LowLevelSend( void* Data, INT Count )
{
	guard(UWebRtcConnection::LowLevelSend);
	// Dropped while the channel opens, the engine resends what needs to arrive
	if( Peer && Peer->Dc->isOpen() )
	{
		clock(Driver->SendCycles);
		try
		{
			Peer->Dc->send( (const rtc::byte*)Data, Count );
		}
		catch( const std::exception& )
		{
		}
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
	{
		auto It = std::find_if( Host->Peers.begin(), Host->Peers.end(), [Closing]( const std::shared_ptr<FWebRtcPeer>& P ) { return P.get()==Closing; } );
		if( It!=Host->Peers.end() )
		{
			ClosePeer( *It );
			Host->Peers.erase( It );
		}
	}
	unguard;
}

/*-----------------------------------------------------------------------------
	UTcpNetDriver WebRTC side.
-----------------------------------------------------------------------------*/

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
		Host->BindAddress = ToStd( IpString(LocalAddr.sin_addr) );
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

UBOOL UTcpNetDriver::WebRtcConnect( FURL& ConnectURL, FString& Error )
{
	guard(UTcpNetDriver::WebRtcConnect);
	std::shared_ptr<FWebRtcPeer> Peer;
	sockaddr_in Addr;
	FString SignalId;
	try
	{
		rtc::Configuration Config;
		Config.disableAutoNegotiation = true;
		if( WebRtcSignalSetup( ConnectURL, SignalId, Addr ) )
		{
			// The URL constructor parses with std::regex, which links only with the STL vcpkg built against
			Config.iceServers.emplace_back( WEBRTC_STUN_HOST, WEBRTC_STUN_PORT );
			Peer = MakeClientPeer( Config );
			Peer->SignalId = SignalId;
			Peer->Pc->setLocalDescription( rtc::Description::Type::Offer );
		}
		else
		{
			std::string Ufrag, Answer;
			if( !WebRtcClientSetup( ConnectURL, Error, Addr, Ufrag, Answer ) )
				return 0;
			Peer = MakeClientPeer( Config );
			Peer->Ufrag = Ufrag;
			Peer->Pc->setLocalDescription( rtc::Description::Type::Offer, rtc::LocalDescriptionInit{ Ufrag, Ufrag } );
			Peer->Pc->setRemoteDescription( rtc::Description( Answer, rtc::Description::Type::Answer ) );
		}
	}
	catch( const std::exception& E )
	{
		Error = FString::Printf( TEXT("WebRTC connect failed: %s"), *FString(E.what()) );
		debugf( TEXT("%s"), *Error );
		if( Peer )
			ClosePeer( Peer );
		return 0;
	}
	Peer->Addr = Addr;
	Peer->CreatedTime = appSeconds();

	// A UDP connection that got no answer is replaced, it must still be the ServerConnection when it goes
	if( ServerConnection )
		delete ServerConnection;
	FWebRtcHost* Host = new FWebRtcHost;
	Host->Peers.push_back( Peer );
	WebRtc = Host;

	ServerConnection = Peer->Connection = new UWebRtcConnection( this, Peer.get(), Addr, USOCK_Pending, ConnectURL );
	debugf( NAME_DevNet, TEXT("Game client over WebRTC to %s"), *IpString(Addr.sin_addr,ntohs(Addr.sin_port)) );
	ServerConnection->CreateChannel( CHTYPE_Control, 1, 0 );
	return 1;
	unguard;
}

static void WebRtcSignal( FWebRtcPeer& Peer )
{
	try
	{
		if( !Peer.Offer )
		{
			auto Local = Peer.Pc->localDescription();
			if( Local && (Peer.Pc->gatheringState()==rtc::PeerConnection::GatheringState::Complete || appSeconds()-Peer.CreatedTime > WEBRTC_GATHER_TIME) )
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
		if( Ok )
			Peer.Pc->setRemoteDescription( rtc::Description( ToStd(Answer), rtc::Description::Type::Answer ) );
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
		std::lock_guard<std::mutex> Guard( Peer.Lock );
		Peer.Closed = true;
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
		sockaddr_in Addr;
		appMemzero( &Addr, sizeof(Addr) );
		Addr.sin_family = AF_INET;
		Addr.sin_port = htons( Request.remotePort );
		Addr.sin_addr.s_addr = inet_addr( Request.remoteAddress.c_str() );
		if( Addr.sin_addr.s_addr==INADDR_NONE )
			continue;

		// The same limits as new UDP connections, each peer costs a DTLS handshake and SCTP state
		INT Pending = 0, SameIP = 0;
		bool Known = false;
		for( auto& Peer : Host->Peers )
		{
			Known |= Peer->Ufrag==Request.localUfrag;
			if( !Peer->Connection )
			{
				Pending++;
				SameIP += IpAddressMatches( Peer->Addr, Addr );
			}
		}
		if( Known || Pending>=WEBRTC_MAX_PENDING || WebRtcIpLimited( this, Addr, SameIP ) )
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
			if( Peer->Pc )
				ClosePeer( Peer );
			continue;
		}
		Peer->CreatedTime = GCurrentTime;
		Host->Peers.push_back( Peer );
	}

	// Backwards, because closing removes peers
	for( INT i=(INT)Host->Peers.size()-1; i>=0 && WebRtc; i-- )
	{
		std::shared_ptr<FWebRtcPeer> Peer = Host->Peers[i];
		if( Peer->SignalId.Len() )
			WebRtcSignal( *Peer );
		std::deque<rtc::binary> Inbox;
		bool Closed;
		{
			std::lock_guard<std::mutex> Guard( Peer->Lock );
			Inbox.swap( Peer->Inbox );
			Closed = Peer->Closed;
		}

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
		// A peer that finishes the handshake but never sends would hold its slot, the UDP path has no such state
		else if( Closed || GCurrentTime - Peer->CreatedTime > WEBRTC_PENDING_TIMEOUT )
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
	for( auto& Peer : Host->Peers )
	{
		if( Peer->Connection )
			Peer->Connection->Peer = NULL;
		ClosePeer( Peer );
	}
	delete Host;
	unguard;
}

