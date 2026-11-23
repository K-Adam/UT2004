// A browser's ICE checks against the server's WebRTC port.

// The system headers go before the engine's, whose macros (check, clock, min, max) break them.
#pragma pack(push,8)
#if _WIN32
#include <winsock2.h>
typedef int socklen_t;
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>
#define closesocket close
#endif
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <zlib.h>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>
#include <vector>
#pragma pack(pop)

#include "TestEngine.h"

typedef std::vector<unsigned char> FBytes;

static void PutAttribute( FBytes& Message, unsigned Type, const FBytes& Value )
{
	for( unsigned Field : { Type, (unsigned)Value.size() } )
	{
		Message.push_back( (unsigned char)(Field>>8) );
		Message.push_back( (unsigned char)Field );
	}
	Message.insert( Message.end(), Value.begin(), Value.end() );
	Message.resize( (Message.size()+3) & ~(size_t)3 );
}

// The STUN binding request of a controlling ICE agent whose tiebreaker is zero
static FBytes BindingRequest( const std::string& Ufrag, const unsigned char* TransactionId )
{
	FBytes Message = { 0x00, 0x01, 0, 0, 0x21, 0x12, 0xA4, 0x42 };
	Message.insert( Message.end(), TransactionId, TransactionId+12 );
	// The integrity and the fingerprint each cover the message before them, with a length that already counts them
	auto SetLength = [&Message]( size_t Trailer )
	{
		size_t Length = Message.size() - 20 + Trailer;
		Message[2] = (unsigned char)(Length>>8);
		Message[3] = (unsigned char)Length;
	};

	std::string Username = Ufrag + ":" + Ufrag;
	PutAttribute( Message, 0x0006, FBytes( Username.begin(), Username.end() ) );
	PutAttribute( Message, 0x802A, FBytes( 8 ) );
	PutAttribute( Message, 0x0024, { 0x6E, 0x00, 0x1F, 0xFF } );

	SetLength( 24 );
	unsigned char Mac[EVP_MAX_MD_SIZE];
	HMAC( EVP_sha1(), Ufrag.data(), (int)Ufrag.size(), Message.data(), Message.size(), Mac, NULL );
	PutAttribute( Message, 0x0008, FBytes( Mac, Mac+20 ) );

	SetLength( 8 );
	unsigned long Crc = crc32( 0, Message.data(), (uInt)Message.size() ) ^ 0x5354554E;
	PutAttribute( Message, 0x8028, { (unsigned char)(Crc>>24), (unsigned char)(Crc>>16), (unsigned char)(Crc>>8), (unsigned char)Crc } );
	return Message;
}

// Chrome sends a zero tiebreaker on checks through a TCP or TLS relay. Answered with an error, the browser gives the server up.
TEST( WebRtc, AnswersZeroTiebreaker )
{
	sockaddr_in Server;
	memset( &Server, 0, sizeof(Server) );
	Server.sin_family = AF_INET;
	Server.sin_port = htons( (unsigned short)(GTestEngine->GLevel->URL.Port + 2) );
	Server.sin_addr.s_addr = htonl( INADDR_LOOPBACK );
	auto Socket = socket( AF_INET, SOCK_DGRAM, 0 );
#if _WIN32
	u_long NonBlocking = 1;
	ioctlsocket( Socket, FIONBIO, &NonBlocking );
#else
	fcntl( Socket, F_SETFL, O_NONBLOCK );
#endif

	INT Successes = 0, Errors = 0;
	// The first check makes the server build its peer, that peer answers the later ones
	for( unsigned char i=0; i<5; i++ )
	{
		const unsigned char TransactionId[12] = { 'u', 't', 't', 'e', 's', 't', i };
		FBytes Request = BindingRequest( "ZeroTiebreakerTestUfrag1", TransactionId );
		sendto( Socket, (const char*)Request.data(), (int)Request.size(), 0, (sockaddr*)&Server, sizeof(Server) );
		// The ICE library answers on its own threads in real time, the game thread only picks new peers up
		std::this_thread::sleep_for( std::chrono::milliseconds(200) );
		RunGame( 0.1f );

		unsigned char Reply[512];
		// The peer's own checks come with other transaction ids
		while( recvfrom( Socket, (char*)Reply, sizeof(Reply), 0, NULL, NULL ) >= 20 )
			if( !memcmp( Reply+8, TransactionId, 6 ) )
			{
				Successes += Reply[0]==0x01 && Reply[1]==0x01;
				Errors += Reply[0]==0x01 && Reply[1]==0x11;
			}
	}
	closesocket( Socket );
	EXPECT_GT( Successes, 0 );
	EXPECT_EQ( 0, Errors );
}

// A native server whose WebRTC port is closed hosts through the web master's socket. The test is that web master.

// The test program must not link IpDrv, whose statics would allocate before the engine has an allocator
#if __STATIC_LINK
extern "C" void WebRtcHostThroughWebMaster();
#else
static void WebRtcHostThroughWebMaster()
{
	((void(*)())appGetDllExport( appGetDllHandle( TEXT("IpDrv") ), TEXT("WebRtcHostThroughWebMaster") ))();
}
#endif

typedef decltype(socket(0,0,0)) FSocket;

static void SetTimeout( FSocket Socket, int Milliseconds )
{
#if _WIN32
	DWORD Time = Milliseconds;
#else
	timeval Time = { Milliseconds/1000, (Milliseconds%1000)*1000 };
#endif
	setsockopt( Socket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&Time, sizeof(Time) );
}

static bool Receive( FSocket Socket, unsigned char* Data, size_t Count )
{
	for( size_t Got=0; Got<Count; )
	{
		int Read = recv( Socket, (char*)Data+Got, (int)(Count-Got), 0 );
		if( Read<=0 )
			return false;
		Got += Read;
	}
	return true;
}

static void SendText( FSocket Socket, const std::string& Text )
{
	FBytes Frame = { 0x81, 126, (unsigned char)(Text.size()>>8), (unsigned char)Text.size() };
	Frame.insert( Frame.end(), Text.begin(), Text.end() );
	send( Socket, (const char*)Frame.data(), (int)Frame.size(), 0 );
}

// The client's next text frame, empty when none came in time. Pings are skipped.
static std::string ReceiveText( FSocket Socket )
{
	for( ;; )
	{
		unsigned char Head[2], Extended[2], Mask[4];
		if( !Receive( Socket, Head, 2 ) )
			return std::string();
		// The rest of a frame that has begun is on its way
		SetTimeout( Socket, 2000 );
		size_t Length = Head[1] & 0x7F;
		if( Length==126 )
		{
			if( !Receive( Socket, Extended, 2 ) )
				return std::string();
			Length = Extended[0]<<8 | Extended[1];
		}
		std::string Text( Length, 0 );
		if( Length==127 || !Receive( Socket, Mask, 4 ) || (Length && !Receive( Socket, (unsigned char*)&Text[0], Length )) )
			return std::string();
		for( size_t i=0; i<Length; i++ )
			Text[i] ^= Mask[i&3];
		SetTimeout( Socket, 50 );
		if( (Head[0]&0x0F)==1 )
			return Text;
	}
}

TEST( WebRtc, HostsThroughWebMaster )
{
	FSocket Listener = socket( AF_INET, SOCK_STREAM, 0 );
	sockaddr_in Address;
	memset( &Address, 0, sizeof(Address) );
	Address.sin_family = AF_INET;
	Address.sin_addr.s_addr = htonl( INADDR_LOOPBACK );
	ASSERT_EQ( 0, bind( Listener, (sockaddr*)&Address, sizeof(Address) ) );
	socklen_t Size = sizeof(Address);
	getsockname( Listener, (sockaddr*)&Address, &Size );
	listen( Listener, 4 );
#if _WIN32
	u_long NonBlocking = 1;
	ioctlsocket( Listener, FIONBIO, &NonBlocking );
#else
	fcntl( Listener, F_SETFL, O_NONBLOCK );
#endif

	GConfig->SetString( TEXT("IpDrv.MasterServerLink"), TEXT("WebMasterServer"), *FString::Printf( TEXT("http://127.0.0.1:%i"), ntohs(Address.sin_port) ) );
	// The test ini turns the uplink off so no test advertises to a real master
	for( INT i=0; i<GTestEngine->GLevel->Actors.Num(); i++ )
	{
		AActor* Actor = GTestEngine->GLevel->Actors(i);
		UBoolProperty* DoUplink = Actor ? FindField<UBoolProperty>( Actor->GetClass(), TEXT("DoUplink") ) : NULL;
		if( DoUplink )
			*(BITFIELD*)((BYTE*)Actor + DoUplink->Offset) |= DoUplink->BitMask;
	}
	const std::string Id = "0123456789abcdef0123456789abcdef";
	// A server sends the token it has, and keeps the one the web master answers with
	const FString Kept = TEXT("00112233445566778899aabbccddeeff"), Issued = TEXT("ffeeddccbbaa99887766554433221100");
	GConfig->SetString( TEXT("IpDrv.MasterServerLink"), TEXT("LinkToken"), *Kept );
	WebRtcHostThroughWebMaster();

	const std::string Offer =
		"v=0\\r\\no=- 1 1 IN IP4 127.0.0.1\\r\\ns=-\\r\\nt=0 0\\r\\na=group:BUNDLE 0\\r\\n"
		"m=application 9 UDP/DTLS/SCTP webrtc-datachannel\\r\\nc=IN IP4 0.0.0.0\\r\\na=mid:0\\r\\n"
		"a=ice-ufrag:testufrag\\r\\na=ice-pwd:testpasswordtestpassword0\\r\\n"
		"a=fingerprint:sha-256 00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00\\r\\n"
		"a=setup:actpass\\r\\na=sctp-port:5000\\r\\na=max-message-size:16384\\r\\n"
		"a=candidate:1 1 UDP 2122317823 203.0.113.7 50000 typ host\\r\\n"
		"a=candidate:2 1 UDP 2122317567 a-name-to-look-up.local 50001 typ host\\r\\n";

	std::string Request, Answer, State;
	const FSocket None = (FSocket)-1;
	FSocket Host = None;
	// The answer waits up to 3 seconds for candidates, which the loopback address the test server is bound to never gets
	FString Token = Kept;
	for( INT i=0; i<150 && (Answer.empty() || State.empty() || Token!=Issued); i++ )
	{
		RunGame( 0.1f );
		std::this_thread::sleep_for( std::chrono::milliseconds(50) );
		FSocket Client = accept( Listener, NULL, NULL );
		if( Client!=None )
		{
#if _WIN32
			// It takes after the listener there
			NonBlocking = 0;
			ioctlsocket( Client, FIONBIO, &NonBlocking );
#endif
			SetTimeout( Client, 2000 );
			std::string Head;
			unsigned char Byte;
			while( Head.find( "\r\n\r\n" )==std::string::npos && Receive( Client, &Byte, 1 ) )
				Head += (char)Byte;
			size_t Key = Head.find( "Sec-WebSocket-Key: " );
			if( Key==std::string::npos )
			{
				// The server also asks its web master for a relay
				const char NotFound[] = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
				send( Client, NotFound, sizeof(NotFound)-1, 0 );
				closesocket( Client );
				continue;
			}
			Request = Head;
			std::string Accept = Head.substr( Key+19, Head.find( "\r\n", Key )-Key-19 ) + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
			unsigned char Digest[EVP_MAX_MD_SIZE], Encoded[64];
			unsigned int DigestSize = 0;
			EVP_Digest( Accept.data(), Accept.size(), Digest, &DigestSize, EVP_sha1(), NULL );
			EVP_EncodeBlock( Encoded, Digest, (int)DigestSize );
			std::string Reply = std::string("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: ") + (char*)Encoded + "\r\n\r\n";
			send( Client, Reply.data(), (int)Reply.size(), 0 );
			Host = Client;
			SetTimeout( Host, 50 );
			SendText( Host, "{\"type\":\"id\",\"id\":\"" + Id + "\",\"iceServers\":[]}" );
			SendText( Host, "{\"type\":\"offer\",\"id\":\"offer-1\",\"ip\":\"203.0.113.7\",\"sdp\":\"" + Offer + "\"}" );
		}
		std::string Text = Host!=None ? ReceiveText( Host ) : std::string();
		if( Text.find( "\"type\":\"state\"" )!=std::string::npos )
		{
			State = Text;
			SendText( Host, std::string("{\"type\":\"link\",\"link\":\"https://example.test/#join=") + Id + "\",\"token\":\"" + TCHAR_TO_ANSI(*Issued) + "\"}" );
		}
		else if( !Text.empty() )
			Answer = Text;
		GConfig->GetString( TEXT("IpDrv.MasterServerLink"), TEXT("LinkToken"), Token );
	}

	EXPECT_NE( std::string::npos, Request.find( "GET /host " ) );
	// The web master compares this header as it is written, and takes a missing Origin for a native server
	EXPECT_NE( std::string::npos, Request.find( "Upgrade: websocket\r\n" ) );
	EXPECT_EQ( std::string::npos, Request.find( "Origin:" ) );
	// The web master lists the id
	EXPECT_NE( std::string::npos, State.find( "{\"key\":\"WebRTC\",\"value\":\"signal:" + Id + "\"}" ) ) << State;
	EXPECT_NE( std::string::npos, State.find( std::string("\"token\":\"") + TCHAR_TO_ANSI(*Kept) + "\"" ) ) << State;
	EXPECT_TRUE( Token==Issued );
	EXPECT_NE( std::string::npos, Answer.find( "\"type\":\"answer\"" ) ) << Answer;
	EXPECT_NE( std::string::npos, Answer.find( "\"id\":\"offer-1\"" ) );
	EXPECT_NE( std::string::npos, Answer.find( "a=fingerprint:sha-256 " ) );
	// The test server is bound to the loopback address, which is no candidate to hand out
	EXPECT_EQ( std::string::npos, Answer.find( "a=candidate:" ) ) << Answer;
	EXPECT_NE( std::string::npos, Answer.find( "m=application 9 " ) );
	EXPECT_NE( std::string::npos, Answer.find( "c=IN IP4 0.0.0.0\\r\\n" ) );

	if( Host!=None )
		closesocket( Host );
	closesocket( Listener );
	// The tests after this one run without a web master again
	GConfig->SetString( TEXT("IpDrv.MasterServerLink"), TEXT("WebMasterServer"), TEXT("") );
	GConfig->SetString( TEXT("IpDrv.MasterServerLink"), TEXT("LinkToken"), TEXT("") );
}
