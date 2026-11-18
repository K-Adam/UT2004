// A browser's ICE checks against the server's WebRTC port.

// The system headers go before the engine's, whose macros (check, clock, min, max) break them.
#pragma pack(push,8)
#if _WIN32
#include <winsock2.h>
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
