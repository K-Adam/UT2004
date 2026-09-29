// Client and uplink for the JSON web master, a separate UT2004-web-master project.

#ifdef WITH_WEBRTC
// Standard headers go before the engine's, whose macros (check, min, max) break them.
#define NOMINMAX
#pragma pack(push, 8)
#include <windows.h>
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#pragma pack(pop)
#endif

#include "UnIpDrv.h"

#ifdef WITH_WEBRTC

static std::string ToUtf8( const FString& S )
{
	INT Len = WideCharToMultiByte( CP_UTF8, 0, *S, -1, NULL, 0, NULL, NULL );
	std::string Out( Len > 0 ? Len - 1 : 0, '\0' );
	if( Len > 1 )
		WideCharToMultiByte( CP_UTF8, 0, *S, -1, &Out[0], Len, NULL, NULL );
	return Out;
}

static FString FromUtf8( const std::string& S )
{
	INT Len = MultiByteToWideChar( CP_UTF8, 0, S.c_str(), -1, NULL, 0 );
	std::wstring Out( Len > 0 ? Len - 1 : 0, L'\0' );
	if( Len > 1 )
		MultiByteToWideChar( CP_UTF8, 0, S.c_str(), -1, &Out[0], Len );
	return FString( Out.c_str() );
}

static size_t AppendResponse( char* Data, size_t Size, size_t Count, void* Response )
{
	((std::string*)Response)->append( Data, Size * Count );
	return Size * Count;
}

// Runs on a worker thread, so it only touches standard types.
static bool HttpRequest( const std::string& Url, const std::string& Body, std::string& Response )
{
	CURL* Curl = curl_easy_init();
	if( !Curl )
		return false;
	curl_slist* Headers = curl_slist_append( NULL, "Content-Type: application/json" );
	curl_easy_setopt( Curl, CURLOPT_URL, Url.c_str() );
	curl_easy_setopt( Curl, CURLOPT_HTTPHEADER, Headers );
	// The master lists the address a post comes from, and the game is reachable over IPv4 only.
	curl_easy_setopt( Curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4 );
	curl_easy_setopt( Curl, CURLOPT_TIMEOUT, 5L );
	// Schannel otherwise fails when the CA's revocation server is unreachable, WinHTTP never checked it.
	curl_easy_setopt( Curl, CURLOPT_SSL_OPTIONS, (long)CURLSSLOPT_REVOKE_BEST_EFFORT );
	curl_easy_setopt( Curl, CURLOPT_WRITEFUNCTION, AppendResponse );
	curl_easy_setopt( Curl, CURLOPT_WRITEDATA, &Response );
	if( !Body.empty() )
	{
		curl_easy_setopt( Curl, CURLOPT_POSTFIELDS, Body.data() );
		curl_easy_setopt( Curl, CURLOPT_POSTFIELDSIZE, (long)Body.size() );
	}
	long Status = 0;
	bool Ok = curl_easy_perform( Curl ) == CURLE_OK
		&& curl_easy_getinfo( Curl, CURLINFO_RESPONSE_CODE, &Status ) == CURLE_OK
		&& Status >= 200 && Status < 300;
	curl_slist_free_all( Headers );
	curl_easy_cleanup( Curl );
	return Ok;
}

static FString WebMasterURL()
{
	FString Url;
	GConfig->GetString( TEXT("IpDrv.MasterServerLink"), TEXT("WebMasterServer"), Url );
	while( Url.Right(1) == TEXT("/") )
		Url = Url.LeftChop(1);
	return Url;
}

void WebMasterAdvertise( const FServerResponseLine& Server )
{
	FString Base = WebMasterURL();
	if( !Base.Len() )
		return;

	nlohmann::json Info = nlohmann::json::array();
	for( INT i=0; i<Server.ServerInfo.Num(); i++ )
		Info.push_back( { { "key", ToUtf8(Server.ServerInfo(i).Key) }, { "value", ToUtf8(Server.ServerInfo(i).Value) } } );
	nlohmann::json Body =
	{
		{ "port", Server.Port },
		{ "name", ToUtf8(Server.ServerName) },
		{ "map", ToUtf8(Server.MapName) },
		{ "gameType", ToUtf8(Server.GameType) },
		{ "players", Server.CurrentPlayers },
		{ "maxPlayers", Server.MaxPlayers },
		{ "flags", Server.Flags },
		{ "skill", ToUtf8(Server.SkillLevel) },
		{ "info", Info },
	};

	std::string Url = ToUtf8( Base + TEXT("/advertise") );
	std::thread( [Url, Payload = Body.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace)]()
	{
		std::string Response;
		HttpRequest( Url, Payload, Response );
	} ).detach();
}

#pragma pack(push, 8)
struct FWebMasterList
{
	std::mutex Lock;
	bool Done = false;
	bool Ok = false;
	std::string Response;
};
#pragma pack(pop)

// The worker thread holds its own reference, so freeing a list that is still loading is safe.
struct FWebMasterListHandle
{
	std::shared_ptr<FWebMasterList> List;
};

FWebMasterListHandle* WebMasterList()
{
	FString Base = WebMasterURL();
	FWebMasterListHandle* Handle = new FWebMasterListHandle;
	Handle->List = std::make_shared<FWebMasterList>();
	if( !Base.Len() )
	{
		debugf( TEXT("WebMasterServer is not set in [IpDrv.MasterServerLink]") );
		Handle->List->Done = true;
		return Handle;
	}
	std::string Url = ToUtf8( Base + TEXT("/") );
	std::thread( [Url, List = Handle->List]()
	{
		std::string Response;
		bool Ok = HttpRequest( Url, std::string(), Response );
		std::lock_guard<std::mutex> Guard( List->Lock );
		List->Response.swap( Response );
		List->Ok = Ok;
		List->Done = true;
	} ).detach();
	return Handle;
}

UBOOL WebMasterListDone( FWebMasterListHandle* Handle, UBOOL& Ok, TArray<FServerResponseLine>& Servers, FString& News )
{
	std::string Response;
	{
		std::lock_guard<std::mutex> Guard( Handle->List->Lock );
		if( !Handle->List->Done )
			return 0;
		Ok = Handle->List->Ok;
		Response.swap( Handle->List->Response );
	}

	nlohmann::json Root = nlohmann::json::parse( Response, nullptr, false );
	if( !Ok || !Root.is_object() )
	{
		Ok = 0;
		return 1;
	}

	auto Str = []( const nlohmann::json& J, const char* Key ) { return J.contains(Key) && J[Key].is_string() ? FromUtf8( J[Key].get<std::string>() ) : FString(); };
	auto Int = []( const nlohmann::json& J, const char* Key ) { return J.contains(Key) && J[Key].is_number_integer() ? J[Key].get<INT>() : 0; };
	if( Root.contains("servers") && Root["servers"].is_array() )
		for( const nlohmann::json& S : Root["servers"] )
		{
			if( !S.is_object() )
				continue;
			FServerResponseLine* Server = &Servers(Servers.AddZeroed());
			Server->IP = Str( S, "ip" );
			Server->Port = Int( S, "port" );
			Server->QueryPort = Server->Port + 1;
			Server->ServerName = Str( S, "name" );
			Server->MapName = Str( S, "map" );
			Server->GameType = Str( S, "gameType" );
			Server->CurrentPlayers = Int( S, "players" );
			Server->MaxPlayers = Int( S, "maxPlayers" );
			Server->Flags = Int( S, "flags" );
			Server->SkillLevel = Str( S, "skill" );
			if( S.contains("info") && S["info"].is_array() )
				for( const nlohmann::json& P : S["info"] )
					if( P.is_object() )
					{
						FKeyValuePair& Pair = Server->ServerInfo(Server->ServerInfo.AddZeroed());
						Pair.Key = Str( P, "key" );
						Pair.Value = Str( P, "value" );
					}
		}
	if( Root.contains("news") && Root["news"].is_array() )
		for( const nlohmann::json& N : Root["news"] )
			if( N.is_object() )
				News += Str( N, "title" ) + TEXT("\r") + Str( N, "body" ) + TEXT("\r\r");
	return 1;
}

void WebMasterListFree( FWebMasterListHandle* Handle )
{
	delete Handle;
}

#endif
