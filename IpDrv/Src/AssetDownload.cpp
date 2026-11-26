// Download step for the stock packages a reduced content set lacks, from the port's asset host.

// Standard headers go before the engine's, whose macros (check, min, max) break them.
#include "HttpTransfer.h"

#include "UnIpDrv.h"

// A host that is down would cost every package of a join its timeout, so it is left alone for a while.
static DOUBLE GHostRetryTime;
enum { HOST_RETRY_SECONDS = 300 };

static const FString& HostUrl()
{
	return ((UDownload*)UAssetDownload::StaticClass()->GetDefaultObject())->DownloadParams;
}

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
	debugf( NAME_DevNet, TEXT("Asset host: %s"), *StockPath );

	FHttpRequest Request;
	Request.Url = appToUtf8( *(FString(Params) + StockPath) ).GetTypedData();
	// A package can take minutes, so only a transfer that stalls is given up.
	Request.StallTime = 20;
	Request.Receive = [this]( const char* Data, size_t Count )
	{
		ReceiveData( (BYTE*)Data, (INT)Count );
		return !*Error;
	};
	Transfer = new FHttpTransfer( Request );

	// Show progress while connecting.
	ReceiveData( NULL, 0 );
	unguard;
}

void UAssetDownload::Tick()
{
	guard(UAssetDownload::Tick);
	if( Transfer && !SkippedFile && !*Error )
	{
		// The web page holds the body in memory until it is complete, so one that outgrows the package is dropped here.
		UBOOL Done = Transfer->Poll();
		if( Transfer->Received>ExpectedSize )
			DownloadError( LocalizeError(TEXT("NetSize"),TEXT("Engine")) );
		else if( !Done )
			return;
		long Status = Transfer->Complete ? Transfer->Status : 0;
		if( !Transfer->Ok() && !*Error )
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
		delete Transfer;
		Transfer = NULL;
	}
	Super::Destroy();
	unguard;
}
IMPLEMENT_CLASS(UAssetDownload)
