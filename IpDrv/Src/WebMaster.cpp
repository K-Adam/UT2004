// Client and uplink for the JSON web master, a separate UT2004-web-master project.

// Standard headers go before the engine's, whose macros (check, min, max) break them.
#define NOMINMAX
#include "HttpTransfer.h"
#pragma pack(push, 8)
#include <nlohmann/json.hpp>
#include <algorithm>
#include <mutex>
#include <string>
#include "SemVer.h"
#pragma pack(pop)

#include "UnIpDrv.h"
#include "UnTcpNetDriver.h"

#include "WebRtc.h"

// The reply comes from the network, so its size and row counts are bounded.
enum
{
	MAX_RESPONSE_SIZE	= 8 * 1024 * 1024,
	MAX_WEB_SERVERS		= 2048,
	MAX_WEB_SERVER_INFO	= 64,
	MAX_WEB_NEWS		= 32,
};

// The web master's credential lasts 48 hours and an expired one drops a relayed game.
// A failure is kept for a while too, or every server browser query would ask again.
enum
{
	TURN_REFRESH_TIME	= 24 * 3600,
	TURN_RETRY_TIME		= 600,
};

// An offer waits up to 8 seconds for the host to answer, an unreachable master still fails fast.
static FHttpTransfer* WebMasterTransfer( const FString& Url, const std::string& Body )
{
	FHttpRequest Request;
	Request.Url = appToUtf8( *Url ).GetTypedData();
	Request.Body = Body;
	Request.Accept = "application/json";
	Request.ContentType = "application/json";
	Request.UserAgent = "UT2004/" UT_VERSION;
	// The master lists the address a post comes from, and the game is reachable over IPv4 only.
	Request.Ipv4 = true;
	Request.ConnectTimeout = 5;
	Request.Timeout = 10;
	Request.MaxSize = MAX_RESPONSE_SIZE;
	return new FHttpTransfer( Request );
}

FString WebMasterURL()
{
	FString Url;
	GConfig->GetString( TEXT("IpDrv.MasterServerLink"), TEXT("WebMasterServer"), Url );
	while( Url.Right(1) == TEXT("/") )
		Url = Url.LeftChop(1);
	return Url;
}

// An emptied key means no server.
FString WebMasterStun()
{
	FString Stun;
	GConfig->GetString( TEXT("IpDrv.MasterServerLink"), TEXT("StunServer"), Stun );
	return Stun;
}

#pragma pack(push, 8)
struct FWebMasterLink
{
	std::mutex Lock;
	std::string Link, Token;
};
#pragma pack(pop)

// Never destroyed, a request that is still running at exit writes to it
static FWebMasterLink& ReceivedLink()
{
	static FWebMasterLink* Link = new FWebMasterLink;
	return *Link;
}

// WebRtc.cpp, the header it would go in is read by files without the JSON library
std::string WebRtcJsonString( const nlohmann::json& Object, const char* Key, size_t Max );

// Natively on a network thread, so it only touches standard types
void WebMasterLinkReceived( const std::string& Json )
{
	nlohmann::json Reply = nlohmann::json::parse( Json, nullptr, false );
	if( !Reply.is_object() )
		return;
	// The link goes into the log as one line
	std::string Link = WebRtcJsonString( Reply, "link", 256 ), Token = WebRtcJsonString( Reply, "token", 32 );
	if( Link.empty() || std::any_of( Link.begin(), Link.end(), []( unsigned char C ) { return C<=' ' || C>'~'; } ) )
		return;
	FWebMasterLink& Out = ReceivedLink();
	std::lock_guard<std::mutex> Guard( Out.Lock );
	Out.Link = Link;
	// The token goes into the ini and back to the web master
	if( WebRtcHostIdValid( Token.c_str() ) )
		Out.Token = Token;
}

// The uplink polls it through WebMasterLinkTake every tick.
static FHttpTransfer* GAdvertise;

UBOOL WebMasterLinkTake( FString& Link, FString& Token )
{
	if( GAdvertise && GAdvertise->Poll() )
	{
		if( GAdvertise->Ok() )
			WebMasterLinkReceived( GAdvertise->Body );
		delete GAdvertise;
		GAdvertise = NULL;
	}
	FWebMasterLink& In = ReceivedLink();
	std::lock_guard<std::mutex> Guard( In.Lock );
	if( In.Link.empty() )
		return 0;
	Link = FString( In.Link.c_str() );
	Token = FString( In.Token.c_str() );
	In.Link.clear();
	In.Token.clear();
	return 1;
}

void WebMasterAdvertise( const FServerResponseLine& Server )
{
	FString Base = WebMasterURL();
	if( !Base.Len() )
		return;

	// Empty asks the web master for one
	FString Token;
	GConfig->GetString( TEXT("IpDrv.MasterServerLink"), TEXT("LinkToken"), Token );

	nlohmann::json Info = nlohmann::json::array();
	for( INT i=0; i<Server.ServerInfo.Num(); i++ )
		Info.push_back( { { "key", appToUtf8(*Server.ServerInfo(i).Key).GetTypedData() }, { "value", appToUtf8(*Server.ServerInfo(i).Value).GetTypedData() } } );
	nlohmann::json Body =
	{
		{ "port", Server.Port },
		{ "name", appToUtf8(*Server.ServerName).GetTypedData() },
		{ "map", appToUtf8(*Server.MapName).GetTypedData() },
		{ "gameType", appToUtf8(*Server.GameType).GetTypedData() },
		{ "players", Server.CurrentPlayers },
		{ "maxPlayers", Server.MaxPlayers },
		{ "flags", Server.Flags },
		{ "skill", appToUtf8(*Server.SkillLevel).GetTypedData() },
		{ "info", Info },
		{ "token", appToUtf8(*Token).GetTypedData() },
	};

	std::string Json = Body.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
	if( !WebRtcHostState( Json.c_str() ) && !GAdvertise )
		GAdvertise = WebMasterTransfer( Base + TEXT("/advertise"), Json );
}

// Without a web master it has no transfer.
struct FWebMasterRequest
{
	FHttpTransfer* Transfer = NULL;
	~FWebMasterRequest() { delete Transfer; }
};

static FWebMasterRequest* WebMasterRequest( const TCHAR* Path, const std::string& Body )
{
	FString Base = WebMasterURL();
	FWebMasterRequest* Request = new FWebMasterRequest;
	if( Base.Len() )
		Request->Transfer = WebMasterTransfer( Base + Path, Body );
	else
	{
		// A page without a web master asks for its ICE servers again and again
		static UBOOL Logged;
		if( !Logged )
			debugf( TEXT("WebMasterServer is not set in [IpDrv.MasterServerLink]") );
		Logged = 1;
	}
	return Request;
}

static UBOOL WebMasterRequestDone( FWebMasterRequest* Request, UBOOL& Ok, std::string& Body )
{
	Ok = 0;
	if( !Request->Transfer )
		return 1;
	if( !Request->Transfer->Poll() )
		return 0;
	Ok = Request->Transfer->Ok();
	Body.swap( Request->Transfer->Body );
	return 1;
}

FWebMasterRequest* WebMasterList()
{
	return WebMasterRequest( TEXT("/"), std::string() );
}

// The server browser builds the join URL from these, so nothing that could start another URL part may pass.
static UBOOL IsDottedQuad( const TCHAR* S )
{
	for( INT Part=0; Part<4; Part++ )
	{
		INT Value = 0, Digits = 0;
		for( ; *S>='0' && *S<='9' && Digits<3; S++, Digits++ )
			Value = Value*10 + (*S-'0');
		if( !Digits || Value>255 || (Digits>1 && S[-Digits]=='0') || *S++ != (Part<3 ? '.' : 0) )
			return 0;
	}
	return 1;
}

FWebMasterRequest* WebMasterOffer( const FString& HostId, const FString& Sdp )
{
	return WebMasterRequest( TEXT("/offer"), nlohmann::json{ { "host", appToUtf8(*HostId).GetTypedData() }, { "sdp", appToUtf8(*Sdp).GetTypedData() } }.dump() );
}

UBOOL WebMasterOfferDone( FWebMasterRequest* Request, UBOOL& Ok, FString& Sdp, UBOOL& Gone )
{
	std::string Body;
	if( !WebMasterRequestDone( Request, Ok, Body ) )
		return 0;
	nlohmann::json Root = nlohmann::json::parse( Body, nullptr, false );
	Ok = Ok && Root.is_object() && Root.contains("sdp") && Root["sdp"].is_string();
	Gone = !Ok && Root.is_object() && Root.contains("error") && Root["error"]=="no such host";
	if( Ok )
		Sdp = appFromUtf8( Root["sdp"].get<std::string>().c_str() );
	return 1;
}

// Standard strings, these outlive the engine's allocator
#pragma pack(push, 8)
struct FIceAnswer
{
	std::string Host, TlsHost, Username, Password, Ip, StunHost;
	INT Port = 0, TlsPort = 0, StunPort = 0;
};
#pragma pack(pop)

// The first STUN server, the first UDP relay and the first TLS relay of the list. Only a browser can use the TLS one,
// the game's ICE library has UDP alone.
static void ParseIce( const std::string& Body, FIceAnswer& Out )
{
	nlohmann::json Root = nlohmann::json::parse( Body, nullptr, false );
	if( !Root.is_object() || !Root.contains("iceServers") || !Root["iceServers"].is_array() )
		return;
	// The server connects to this address, so a name the resolver would look up must not pass
	if( Root.contains("ip") && Root["ip"].is_string() && IsDottedQuad( *FString( Root["ip"].get<std::string>().c_str() ) ) )
		Out.Ip = Root["ip"].get<std::string>();
	for( const auto& Server : Root["iceServers"] )
	{
		if( !Server.is_object() || !Server.contains("urls") || !Server["urls"].is_array() )
			continue;
		bool Credential = Server.contains("username") && Server["username"].is_string()
			&& Server.contains("credential") && Server["credential"].is_string();
		for( const auto& Item : Server["urls"] )
		{
			if( !Item.is_string() )
				continue;
			std::string Url = Item.get<std::string>();
			bool Stun = Url.compare( 0, 5, "stun:" ) == 0, Tls = Url.compare( 0, 6, "turns:" ) == 0;
			size_t Name = Tls ? 6 : 5, Colon = Url.find( ':', Name );
			if( (!Stun && !Tls && Url.compare( 0, 5, "turn:" ) != 0) || Colon == std::string::npos || Colon == Name )
				continue;
			INT UrlPort = atoi( Url.c_str() + Colon + 1 );
			if( UrlPort < 1 || UrlPort > 65535 )
				continue;
			if( Stun && Out.StunHost.empty() )
			{
				Out.StunHost = Url.substr( 5, Colon - 5 );
				Out.StunPort = UrlPort;
			}
#if __EMSCRIPTEN__
			else if( Tls && Credential && Out.TlsHost.empty() )
			{
				Out.TlsHost = Url.substr( 6, Colon - 6 );
				Out.TlsPort = UrlPort;
				Out.Username = Server["username"].get<std::string>();
				Out.Password = Server["credential"].get<std::string>();
			}
#endif
			else if( !Stun && !Tls && Credential && Out.Host.empty() && Url.find("transport=tcp") == std::string::npos )
			{
				Out.Host = Url.substr( 5, Colon - 5 );
				Out.Port = UrlPort;
				Out.Username = Server["username"].get<std::string>();
				Out.Password = Server["credential"].get<std::string>();
			}
		}
	}
}

UBOOL WebMasterTurn( FWebMasterTurn& Out )
{
	static FWebMasterRequest* Request;
	static FIceAnswer Ice;
	static DOUBLE NextTime;
	if( Request )
	{
		UBOOL Ok;
		std::string Body;
		if( !WebMasterRequestDone( Request, Ok, Body ) )
			return 0;
		WebMasterRequestFree( Request );
		Request = nullptr;
		Ice = FIceAnswer();
		if( Ok )
			ParseIce( Body, Ice );
		NextTime = appSeconds() + (Ice.Host.empty() && Ice.TlsHost.empty() ? TURN_RETRY_TIME : TURN_REFRESH_TIME);
		if( Ice.StunHost.empty() )
		{
			// Without a port the ICE library takes the standard one
			FString Stun = WebMasterStun(), Host = Stun, Port;
			Stun.Split( TEXT(":"), &Host, &Port );
			Ice.StunHost = TCHAR_TO_ANSI( *Host );
			Ice.StunPort = Clamp( appAtoi( *Port ), 0, 65535 );
		}
	}
	else if( appSeconds() >= NextTime )
	{
		Request = WebMasterRequest( TEXT("/ice"), std::string() );
		return 0;
	}
	Out.Host = Ice.Host.c_str();
	Out.TlsHost = Ice.TlsHost.c_str();
	Out.Username = Ice.Username.c_str();
	Out.Password = Ice.Password.c_str();
	Out.Ip = Ice.Ip.c_str();
	Out.StunHost = Ice.StunHost.c_str();
	Out.Port = Ice.Port;
	Out.TlsPort = Ice.TlsPort;
	Out.StunPort = Ice.StunPort;
	return 1;
}

// The engine's strings end at a NUL, so a key such as "Link\0x" would read as Link. Colour codes are other control characters and stay.
static FString JsonText( const nlohmann::json& J, const char* Key )
{
	if( !J.contains(Key) || !J[Key].is_string() )
		return FString();
	const std::string& S = J[Key].get_ref<const std::string&>();
	return S.find( '\0' )!=std::string::npos ? FString() : appFromUtf8( S.c_str() );
}

static void ParseServers( const nlohmann::json& Root, TArray<FServerResponseLine>& Servers )
{
	auto Int = []( const nlohmann::json& J, const char* Key ) { return J.contains(Key) && J[Key].is_number_integer() ? J[Key].get<INT>() : 0; };
	if( Root.contains("servers") && Root["servers"].is_array() )
		for( const nlohmann::json& S : Root["servers"] )
		{
			if( Servers.Num() >= MAX_WEB_SERVERS )
				break;
			FString IP = JsonText( S, "ip" );
			INT Port = Int( S, "port" );
			if( !S.is_object() || !IsDottedQuad(*IP) || Port<1 || Port>65535 )
				continue;
			FServerResponseLine* Server = &Servers(Servers.AddZeroed());
			Server->IP = IP;
			Server->Port = Port;
			Server->QueryPort = Server->Port + 1;
			Server->ServerName = JsonText( S, "name" );
			Server->MapName = JsonText( S, "map" );
			Server->GameType = JsonText( S, "gameType" );
			Server->CurrentPlayers = Int( S, "players" );
			Server->MaxPlayers = Int( S, "maxPlayers" );
			Server->Flags = Int( S, "flags" );
			Server->SkillLevel = JsonText( S, "skill" );
			if( S.contains("info") && S["info"].is_array() )
				for( const nlohmann::json& P : S["info"] )
					if( P.is_object() && Server->ServerInfo.Num() < MAX_WEB_SERVER_INFO )
					{
						FKeyValuePair& Pair = Server->ServerInfo(Server->ServerInfo.AddZeroed());
						Pair.Key = JsonText( P, "key" );
						Pair.Value = JsonText( P, "value" );
					}
			// Every host the web master has no address of is listed at 0.0.0.0, which names none of them
			FString Id = IP==TEXT("0.0.0.0") ? ServerNameId( Server->ServerInfo ) : FString();
			if( Id.Len() )
				Server->IP = Id + TEXT(".signal");
		}
}

void WebMasterParseServers( const ANSICHAR* Json, TArray<FServerResponseLine>& Servers )
{
	nlohmann::json Root = nlohmann::json::parse( Json, nullptr, false );
	if( Root.is_object() )
		ParseServers( Root, Servers );
}

UBOOL WebMasterLookupDone( FWebMasterRequest* Request, const FString& Id, FString& Signal )
{
	UBOOL Ok;
	std::string Body;
	if( !WebMasterRequestDone( Request, Ok, Body ) )
		return 0;
	TArray<FServerResponseLine> Servers;
	if( Ok )
		WebMasterParseServers( Body.c_str(), Servers );
	Signal = FindServerSignal( Servers, Id );
	return 1;
}

UBOOL WebMasterListDone( FWebMasterRequest* Request, UBOOL& Ok, TArray<FServerResponseLine>& Servers, FString& News )
{
	std::string Response;
	if( !WebMasterRequestDone( Request, Ok, Response ) )
		return 0;

	nlohmann::json Root = nlohmann::json::parse( Response, nullptr, false );
	if( !Ok || !Root.is_object() )
	{
		Ok = 0;
		return 1;
	}
	ParseServers( Root, Servers );
#if !__EMSCRIPTEN__
	// The web page is always the latest build.
	if( Root.contains("version") && Root["version"].is_string() && SemVerIsNewer( Root["version"].get<std::string>(), UT_VERSION ) )
		News = FString::Printf( TEXT("Update available: %s\r%s\r\r"), *JsonText( Root, "version" ), appFromAnsi(UT_UPDATE_URL) );
#endif
	INT NewsCount = 0;
	if( Root.contains("news") && Root["news"].is_array() )
		for( const nlohmann::json& N : Root["news"] )
			if( N.is_object() && NewsCount++ < MAX_WEB_NEWS )
				News += JsonText( N, "title" ) + TEXT("\r") + JsonText( N, "body" ) + TEXT("\r\r");
	return 1;
}

void WebMasterRequestFree( FWebMasterRequest* Request )
{
	delete Request;
}

