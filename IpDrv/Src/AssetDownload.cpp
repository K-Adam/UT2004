// Download step for the stock packages a reduced content set lacks, from the port's asset host.

// Standard headers go before the engine's, whose macros (check, min, max) break them.
#define NOMINMAX
#pragma pack(push, 8)
#if __EMSCRIPTEN__
#include <emscripten/fetch.h>
#else
#include <curl/curl.h>
#endif
#include <string>
#pragma pack(pop)

#include "UnIpDrv.h"

// A host that is down would cost every package of a join its timeout, so it is left alone for a while.
static DOUBLE GHostRetryTime;
enum { HOST_RETRY_SECONDS = 300 };

static const FString& HostUrl()
{
	return ((UDownload*)UAssetDownload::StaticClass()->GetDefaultObject())->DownloadParams;
}

struct FAssetTransfer
{
#if __EMSCRIPTEN__
	emscripten_fetch_t* Fetch;
	UBOOL Done;
	QWORD Loaded;
#else
	CURLM* Multi;
	CURL* Easy;
#endif
};

#if __EMSCRIPTEN__
static void FetchDone( emscripten_fetch_t* Fetch )
{
	// Closing a fetch that is still running calls this again, after its download is gone.
	if( Fetch->userData )
		((FAssetTransfer*)Fetch->userData)->Done = 1;
}

static void FetchProgress( emscripten_fetch_t* Fetch )
{
	if( Fetch->userData )
		((FAssetTransfer*)Fetch->userData)->Loaded = Fetch->dataOffset + Fetch->numBytes;
}
#else
CURL* HttpEasy( const std::string& Url );

static size_t ReceiveBody( char* Data, size_t Size, size_t Count, void* Download )
{
	UAssetDownload* Asset = (UAssetDownload*)Download;
	Asset->ReceiveData( (BYTE*)Data, Size * Count );
	// A short count makes curl abort the transfer
	return *Asset->Error ? 0 : Size * Count;
}
#endif

void UAssetDownload::StaticConstructor()
{
	guard(UAssetDownload::StaticConstructor);
	// An emptied key turns the step off.
	new(GetClass(),TEXT("URL"),RF_Public)UStrProperty(CPP_PROPERTY(DownloadParams), TEXT("Settings"), CPF_Config );
	unguard;
}

void UAssetDownload::Offer( UNetConnection* Connection )
{
	guard(UAssetDownload::Offer);
	const FString& Url = HostUrl();
	if( !Url.Len() )
		return;
	// Ahead of the server's methods, as a server redirect can be far slower than the host.
	FDownloadInfo& Info = Connection->DownloadInfo( Connection->DownloadInfo.AddZeroed() );
	Info.Class = StaticClass();
	Info.ClassName = StaticClass()->GetPathName();
	Info.Params = Url;
	Connection->ClientDownloads = Connection->DownloadInfo.Num();
	unguard;
}

void UAssetDownload::ReceiveFile( UNetConnection* InConnection, INT InPackageIndex, const TCHAR* Params, UBOOL InCompression, INT InAttempt )
{
	guard(UAssetDownload::ReceiveFile);
	UDownload::ReceiveFile( InConnection, InPackageIndex, Params, InCompression, InAttempt );

	// Maps are not hosted.
	INT StockSize;
	FString StockMD5, StockPath;
	if( appSeconds()<GHostRetryTime || !StockPackage( Info->Guid, StockSize, StockMD5, StockPath ) || StockPath.Left(5)==TEXT("Maps/") )
	{
		// The caller still sets this download up after the call, so Tick reports the failure.
		DownloadError( LocalizeError(TEXT("ConnectionFailed"),TEXT("Engine")) );
		return;
	}
	ExpectedSize = Info->DownloadSize = StockSize;
	std::string Url = &appToUtf8( *(FString(Params) + StockPath) )(0);
	debugf( NAME_DevNet, TEXT("Asset host: %s"), *StockPath );

	Transfer = new FAssetTransfer();
#if __EMSCRIPTEN__
	emscripten_fetch_attr_t Attr;
	emscripten_fetch_attr_init( &Attr );
	strcpy( Attr.requestMethod, "GET" );
	Attr.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY;
	Attr.userData = Transfer;
	Attr.onsuccess = FetchDone;
	Attr.onerror = FetchDone;
	Attr.onprogress = FetchProgress;
	Transfer->Fetch = emscripten_fetch( &Attr, Url.c_str() );
#else
	Transfer->Multi = curl_multi_init();
	Transfer->Easy = HttpEasy( Url );
	curl_easy_setopt( Transfer->Easy, CURLOPT_FAILONERROR, 1L );
	curl_easy_setopt( Transfer->Easy, CURLOPT_CONNECTTIMEOUT, 10L );
	// A package can take minutes, so only a transfer that stalls is given up.
	curl_easy_setopt( Transfer->Easy, CURLOPT_LOW_SPEED_LIMIT, 1024L );
	curl_easy_setopt( Transfer->Easy, CURLOPT_LOW_SPEED_TIME, 20L );
	curl_easy_setopt( Transfer->Easy, CURLOPT_WRITEFUNCTION, ReceiveBody );
	curl_easy_setopt( Transfer->Easy, CURLOPT_WRITEDATA, this );
	curl_multi_add_handle( Transfer->Multi, Transfer->Easy );
#endif

	// Show progress while connecting.
	ReceiveData( NULL, 0 );
	unguard;
}

void UAssetDownload::Tick()
{
	guard(UAssetDownload::Tick);
	if( Transfer && !SkippedFile && !*Error )
	{
		long Status = 0;
#if __EMSCRIPTEN__
		emscripten_fetch_t* Fetch = Transfer->Fetch;
		// The body is held in memory until it is complete, so one that outgrows the package is dropped here.
		if( Transfer->Loaded>(QWORD)ExpectedSize )
			DownloadError( LocalizeError(TEXT("NetSize"),TEXT("Engine")) );
		else if( !Transfer->Done )
			return;
		Status = Fetch->status;
		if( Status==200 && !*Error )
			ReceiveData( (BYTE*)Fetch->data, Fetch->numBytes );
#else
		int Running = 0, Left = 0;
		curl_multi_perform( Transfer->Multi, &Running );
		if( Running && !*Error )
			return;
		CURLMsg* Message = curl_multi_info_read( Transfer->Multi, &Left );
		curl_easy_getinfo( Transfer->Easy, CURLINFO_RESPONSE_CODE, &Status );
		if( (!Message || Message->data.result!=CURLE_OK) && Status==200 )
			Status = 0;
#endif
		if( Status!=200 && !*Error )
		{
			DownloadError( LocalizeError(TEXT("ConnectionFailed"),TEXT("Engine")) );
			// A file the host lacks says nothing about the host, and neither does an address that a server named.
			if( (Status<400 || Status>=500) && Connection->DownloadInfo(AttemptNumber).Params==HostUrl() )
				GHostRetryTime = appSeconds() + HOST_RETRY_SECONDS;
		}
	}
	// This deletes the download.
	DownloadDone();
	unguard;
}

UBOOL UAssetDownload::TrySkipFile()
{
	guard(UAssetDownload::TrySkipFile);
	if( !Super::TrySkipFile() )
		return 0;
	// The server takes a skipped package off its list as well.
	Connection->Logf( TEXT("SKIP GUID=%s"), Info->Guid.String() );
	return 1;
	unguard;
}

void UAssetDownload::Destroy()
{
	guard(UAssetDownload::Destroy);
	if( Transfer )
	{
#if __EMSCRIPTEN__
		Transfer->Fetch->userData = NULL;
		emscripten_fetch_close( Transfer->Fetch );
#else
		curl_multi_remove_handle( Transfer->Multi, Transfer->Easy );
		curl_easy_cleanup( Transfer->Easy );
		curl_multi_cleanup( Transfer->Multi );
#endif
		delete Transfer;
		Transfer = NULL;
	}
	Super::Destroy();
	unguard;
}
IMPLEMENT_CLASS(UAssetDownload)
