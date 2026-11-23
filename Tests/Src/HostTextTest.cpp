#include "TestEngine.h"
#include "UnIpDrv.h"

static const TCHAR* Id = TEXT("0123456789abcdef0123456789abcdef");
static const TCHAR* Name = TEXT("0123456789abcdef0123456789abcdef.signal");

TEST( HostText, NameIsThirtyTwoHexDigitsAndSignal )
{
	EXPECT_TRUE( HostTextSignalId( TEXT("0123456789ABCDEF0123456789abcdef.SIGNAL") ) == Id );
	EXPECT_FALSE( HostTextSignal( TEXT("0123456789abcdef0123456789abcde.signal") ) );
	EXPECT_FALSE( HostTextSignal( TEXT("0123456789abcdef0123456789abcdeg.signal") ) );
	EXPECT_FALSE( HostTextSignal( TEXT("0123456789abcdef0123456789abcdef.signals") ) );
	EXPECT_FALSE( HostTextSignal( TEXT("0123456789abcdef0123456789abcdef.sig") ) );
	EXPECT_FALSE( HostTextSignal( TEXT("1.2.3.4") ) );
	EXPECT_FALSE( HostTextSignal( TEXT("") ) );
	// A malformed name is still never looked up
	EXPECT_TRUE( HostTextSignalSuffix( TEXT("example.Signal") ) );
	EXPECT_TRUE( HostTextSignalSuffix( TEXT("0123456789abcdef0123456789abcdef.signal..") ) );
	EXPECT_FALSE( HostTextSignal( TEXT("0123456789abcdef0123456789abcdef.signal.") ) );
	EXPECT_FALSE( HostTextSignalSuffix( TEXT("signal") ) );
	EXPECT_TRUE( HostTextCanonical( TEXT("Example.COM") ) == TEXT("Example.COM") );
}

TEST( HostText, UrlKeepsTheNameInLowercase )
{
	FURL WithPort( NULL, TEXT("0123456789ABCDEF0123456789ABCDEF.signal:7790?WebRTC=signal:0123456789abcdef0123456789abcdef"), TRAVEL_Absolute );
	EXPECT_TRUE( WithPort.Valid );
	EXPECT_EQ( 0, appStrcmp( *WithPort.Host, Name ) );
	EXPECT_EQ( 7790, WithPort.Port );
	EXPECT_EQ( 0, appStrcmp( WithPort.GetOption( TEXT("WebRTC="), TEXT("") ), TEXT("signal:0123456789abcdef0123456789abcdef") ) );

	FURL Plain( NULL, Name, TRAVEL_Absolute );
	EXPECT_TRUE( Plain.Valid );
	EXPECT_EQ( 0, appStrcmp( *Plain.Host, Name ) );
	EXPECT_EQ( FURL::DefaultPort, Plain.Port );
	EXPECT_EQ( 0, appStrcmp( *FURL( NULL, *Plain.String(), TRAVEL_Absolute ).Host, Name ) );
}

// At 0.0.0.0 every such host was the same host, so travel to another one took the first one's details along
TEST( HostText, TravelKeepsDetailsForTheSameNameOnly )
{
	FURL Base( NULL, TEXT("0123456789abcdef0123456789abcdef.signal:7777?WebRTC=signal:11111111111111111111111111111111"), TRAVEL_Absolute );
	FURL Same( &Base, TEXT("0123456789ABCDEF0123456789ABCDEF.signal:7777/DM-Rankin"), TRAVEL_Partial );
	EXPECT_EQ( 0, appStrcmp( Same.GetOption( TEXT("WebRTC="), TEXT("") ), TEXT("signal:11111111111111111111111111111111") ) );
	FURL Other( &Base, TEXT("22222222222222222222222222222222.signal:7777/DM-Rankin"), TRAVEL_Partial );
	EXPECT_EQ( 0, appStrcmp( Other.GetOption( TEXT("WebRTC="), TEXT("") ), TEXT("") ) );
}

// The test program must not link IpDrv, whose statics would allocate before the engine has an allocator
static void ParseList( const ANSICHAR* Json, TArray<FServerResponseLine>& Servers )
{
#if __STATIC_LINK
	WebMasterParseServers( Json, Servers );
#else
	typedef void (*FParse)( const ANSICHAR*, TArray<FServerResponseLine>& );
	((FParse)appGetDllExport( appGetDllHandle( TEXT("IpDrv") ), TEXT("WebMasterParseServers") ))( Json, Servers );
#endif
}

// Newest first, as the web master sends it. The first two rows share a Link id, the third is a host without one.
static const ANSICHAR* List =
	"{\"servers\":["
	"{\"ip\":\"0.0.0.0\",\"port\":7777,\"info\":[{\"key\":\"Link\",\"value\":\"0123456789abcdef0123456789abcdef\"},{\"key\":\"WebRTC\",\"value\":\"signal:11111111111111111111111111111111\"}]},"
	"{\"ip\":\"0.0.0.0\",\"port\":7778,\"info\":[{\"key\":\"Link\",\"value\":\"0123456789abcdef0123456789abcdef\"},{\"key\":\"WebRTC\",\"value\":\"signal:22222222222222222222222222222222\"}]},"
	"{\"ip\":\"0.0.0.0\",\"port\":7779,\"info\":[{\"key\":\"WebRTC\",\"value\":\"signal:33333333333333333333333333333333\"}]},"
	"{\"ip\":\"203.0.113.5\",\"port\":7777,\"info\":[{\"key\":\"Link\",\"value\":\"44444444444444444444444444444444\"},{\"key\":\"WebRTC\",\"value\":\"7779,AB\"}]},"
	"{\"ip\":\"0.0.0.0\",\"port\":7780,\"info\":[{\"key\":\"Link\\u0000x\",\"value\":\"55555555555555555555555555555555\"},{\"key\":\"WebRTC\",\"value\":\"signal:66666666666666666666666666666666\"}]},"
	"{\"ip\":\"203.0.113.6\",\"port\":7777,\"name\":\"\\u001b\\u00ff\\u0001\\u0001Red\",\"info\":[]}"
	"]}";

TEST( HostText, ListNamesHostsWithoutAnAddress )
{
	TArray<FServerResponseLine> Servers;
	ParseList( List, Servers );
	ASSERT_EQ( 6, Servers.Num() );
	EXPECT_EQ( 0, appStrcmp( *Servers(0).IP, Name ) );
	EXPECT_EQ( 0, appStrcmp( *Servers(2).IP, TEXT("33333333333333333333333333333333.signal") ) );
	EXPECT_EQ( 0, appStrcmp( *Servers(3).IP, TEXT("203.0.113.5") ) );
	// The key that would read as Link is dropped, the row is named by its signal: details
	EXPECT_EQ( 0, appStrcmp( *Servers(4).IP, TEXT("66666666666666666666666666666666.signal") ) );
	EXPECT_EQ( 0, Servers(4).ServerInfo(0).Key.Len() );
	// Colour codes are control characters too and stay
	EXPECT_EQ( 7, Servers(5).ServerName.Len() );
	EXPECT_EQ( 0x1B, (INT)(*Servers(5).ServerName)[0] );

	// The newest row of a Link id, and a name ignores the port
	EXPECT_EQ( 0, appStrcmp( *FindServerSignal( Servers, Id ), TEXT("11111111111111111111111111111111") ) );
	EXPECT_EQ( 0, appStrcmp( *FindServerSignal( Servers, TEXT("33333333333333333333333333333333") ), TEXT("33333333333333333333333333333333") ) );
	// A row with an address has no signal: details to join by
	EXPECT_EQ( 0, FindServerSignal( Servers, TEXT("44444444444444444444444444444444") ).Len() );
	EXPECT_EQ( 0, FindServerSignal( Servers, TEXT("55555555555555555555555555555555") ).Len() );
	EXPECT_EQ( 0, FindServerSignal( Servers, TEXT("77777777777777777777777777777777") ).Len() );
	TArray<FServerResponseLine> None;
	ParseList( "not json", None );
	EXPECT_EQ( 0, None.Num() );
}
