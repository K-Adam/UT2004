// Client and uplink for the JSON master in WebMaster/.

#ifdef WITH_WEBRTC
// Standard headers go before the engine's, whose macros (check, min, max) break them.
#define NOMINMAX
#pragma pack(push, 8)
#include <windows.h>
#include <winhttp.h>
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

// Runs on a worker thread, so it only touches standard types.
static bool HttpRequest( const std::wstring& Url, const wchar_t* Method, const std::string& Body, std::string& Response )
{
	URL_COMPONENTS Parts = { sizeof(Parts) };
	wchar_t Host[256], Path[1024];
	Parts.lpszHostName = Host;
	Parts.dwHostNameLength = 256;
	Parts.lpszUrlPath = Path;
	Parts.dwUrlPathLength = 1024;
	if( !WinHttpCrackUrl( Url.c_str(), 0, 0, &Parts ) )
		return false;

	bool Ok = false;
	HINTERNET Session = WinHttpOpen( L"UT2004", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0 );
	HINTERNET Connection = Session ? WinHttpConnect( Session, Host, Parts.nPort, 0 ) : NULL;
	HINTERNET Request = Connection ? WinHttpOpenRequest( Connection, Method, Path, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
		Parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0 ) : NULL;
	if( Request )
	{
		const INT Timeout = 5000;
		WinHttpSetTimeouts( Request, Timeout, Timeout, Timeout, Timeout );
		DWORD Status = 0, StatusSize = sizeof(Status);
		if( WinHttpSendRequest( Request, L"Content-Type: application/json\r\n", (DWORD)-1L, (LPVOID)Body.data(), (DWORD)Body.size(), (DWORD)Body.size(), 0 )
		&&	WinHttpReceiveResponse( Request, NULL )
		&&	WinHttpQueryHeaders( Request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &Status, &StatusSize, WINHTTP_NO_HEADER_INDEX ) )
		{
			char Buffer[4096];
			DWORD Read = 0;
			while( WinHttpReadData( Request, Buffer, sizeof(Buffer), &Read ) && Read )
				Response.append( Buffer, Read );
			Ok = Status >= 200 && Status < 300;
		}
	}
	WinHttpCloseHandle( Request );
	WinHttpCloseHandle( Connection );
	WinHttpCloseHandle( Session );
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

	std::wstring Url = *(Base + TEXT("/advertise"));
	std::thread( [Url, Payload = Body.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace)]()
	{
		std::string Response;
		HttpRequest( Url, L"POST", Payload, Response );
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
	std::wstring Url = *(Base + TEXT("/"));
	std::thread( [Url, List = Handle->List]()
	{
		std::string Response;
		bool Ok = HttpRequest( Url, L"GET", std::string(), Response );
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
