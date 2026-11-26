#include "HttpTransfer.h"

// The engine builds with /Zp4, which would change the layout of curl's structs.
#pragma pack(push,8)
#include <string.h>
#if __EMSCRIPTEN__
#include <emscripten/fetch.h>
#else
#include <curl/curl.h>
#endif
#if __ANDROID__
#include <dirent.h>
#include <fstream>
#include <sstream>
#endif
#pragma pack(pop)

#if __ANDROID__
// The files are PEM, the text around each certificate is skipped.
const std::string* SystemCaBundle()
{
	static const std::string Certs = []()
	{
		std::string All;
		for( const char* Dir : { "/apex/com.android.conscrypt/cacerts", "/system/etc/security/cacerts" } )
			if( DIR* D = opendir( Dir ) )
			{
				while( dirent* E = readdir( D ) )
					if( E->d_name[0] != '.' )
					{
						std::ostringstream Cert;
						Cert << std::ifstream( std::string(Dir) + "/" + E->d_name ).rdbuf();
						All += Cert.str() + "\n";
					}
				closedir( D );
				if( !All.empty() )
					break;
			}
		return All;
	}();
	return &Certs;
}
#else
const std::string* SystemCaBundle()
{
	return nullptr;
}
#endif

#if __EMSCRIPTEN__

struct FHttpTransfer::FImpl
{
	emscripten_fetch_t* Fetch;
	bool Finished;
	long long Loaded;
};

static void FetchFinished( emscripten_fetch_t* Fetch )
{
	// Closing a fetch that is still running calls this again, after its transfer is gone.
	if( Fetch->userData )
		((FHttpTransfer::FImpl*)Fetch->userData)->Finished = true;
}

static void FetchProgress( emscripten_fetch_t* Fetch )
{
	if( Fetch->userData )
		((FHttpTransfer::FImpl*)Fetch->userData)->Loaded = Fetch->dataOffset + Fetch->numBytes;
}

FHttpTransfer::FHttpTransfer( const FHttpRequest& InRequest )
:	Request( InRequest )
,	Impl( new FImpl{} )
{
	emscripten_fetch_attr_t Attr;
	emscripten_fetch_attr_init( &Attr );
	strcpy( Attr.requestMethod, Request.Body.empty() ? "GET" : "POST" );
	// Replace, or the request waits for a lookup in a cache that never holds it.
	Attr.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY | EMSCRIPTEN_FETCH_REPLACE;
	Attr.timeoutMSecs = Request.Timeout * 1000;
	// The browser reads the body after this returns, so the request keeps it.
	Attr.requestData = Request.Body.data();
	Attr.requestDataSize = Request.Body.size();
	const char* Headers[] = { "Accept", Request.Accept.c_str(), nullptr };
	if( !Request.Accept.empty() )
		Attr.requestHeaders = Headers;
	Attr.userData = Impl;
	Attr.onsuccess = FetchFinished;
	Attr.onerror = FetchFinished;
	Attr.onprogress = FetchProgress;
	Impl->Fetch = emscripten_fetch( &Attr, Request.Url.c_str() );
}

FHttpTransfer::~FHttpTransfer()
{
	Impl->Fetch->userData = nullptr;
	emscripten_fetch_close( Impl->Fetch );
	delete Impl;
}

bool FHttpTransfer::Poll()
{
	if( Done )
		return true;
	emscripten_fetch_t* Fetch = Impl->Fetch;
	Received = Impl->Finished ? Fetch->numBytes : Impl->Loaded;
	// The reply is held in memory until it is complete, so one that outgrows its limit is dropped here.
	bool Large = Request.MaxSize && (size_t)Received > Request.MaxSize;
	if( !Large && !Impl->Finished )
		return false;
	Done = true;
	Status = Large ? 0 : Fetch->status;
	Complete = Status != 0;
	if( !Complete )
		Error = Large ? "The reply is too large" : Fetch->statusText;
	else if( !Request.Receive )
		Body.assign( Fetch->data, Fetch->numBytes );
	else if( Ok() && !Request.Receive( Fetch->data, Fetch->numBytes ) )
	{
		Complete = false;
		Error = "Stopped";
	}
	return true;
}

void FHttpTransfer::Wait( int )
{
}

#else

struct FHttpTransfer::FImpl
{
	CURLM* Multi;
	CURL* Easy;
	curl_slist* Headers;
	char Reason[CURL_ERROR_SIZE];
};

size_t FHttpTransfer::Write( char* Data, size_t Size, size_t Count, void* Transfer )
{
	FHttpTransfer* T = (FHttpTransfer*)Transfer;
	size_t Bytes = Size * Count;
	// A short count makes curl abort the transfer.
	if( T->Request.MaxSize && (size_t)T->Received + Bytes > T->Request.MaxSize )
		return 0;
	T->Received += Bytes;
	if( !T->Request.Receive )
	{
		T->Body.append( Data, Bytes );
		return Bytes;
	}
	// An error page must not end up in a file.
	long Status = 0;
	curl_easy_getinfo( T->Impl->Easy, CURLINFO_RESPONSE_CODE, &Status );
	if( Status < 200 || Status >= 300 )
		return Bytes;
	return T->Request.Receive( Data, Bytes ) ? Bytes : 0;
}

FHttpTransfer::FHttpTransfer( const FHttpRequest& InRequest )
:	Request( InRequest )
,	Impl( new FImpl{} )
{
	Impl->Multi = curl_multi_init();
	Impl->Easy = curl_easy_init();
	if( !Impl->Multi || !Impl->Easy )
	{
		Done = true;
		Error = "curl did not start";
		return;
	}
	CURL* Easy = Impl->Easy;
	curl_easy_setopt( Easy, CURLOPT_URL, Request.Url.c_str() );
	curl_easy_setopt( Easy, CURLOPT_ERRORBUFFER, Impl->Reason );
	// Schannel otherwise fails when the CA's revocation server is unreachable, WinHTTP never checked it.
	curl_easy_setopt( Easy, CURLOPT_SSL_OPTIONS, (long)CURLSSLOPT_REVOKE_BEST_EFFORT );
	if( const std::string* Ca = SystemCaBundle() )
	{
		curl_blob Blob = { (void*)Ca->data(), Ca->size(), CURL_BLOB_NOCOPY };
		curl_easy_setopt( Easy, CURLOPT_CAINFO_BLOB, &Blob );
	}
	curl_easy_setopt( Easy, CURLOPT_CONNECTTIMEOUT, Request.ConnectTimeout );
	curl_easy_setopt( Easy, CURLOPT_TIMEOUT, Request.Timeout );
	if( Request.StallTime )
	{
		curl_easy_setopt( Easy, CURLOPT_LOW_SPEED_LIMIT, 1024L );
		curl_easy_setopt( Easy, CURLOPT_LOW_SPEED_TIME, Request.StallTime );
	}
	curl_easy_setopt( Easy, CURLOPT_RESUME_FROM_LARGE, (curl_off_t)Request.ResumeFrom );
	curl_easy_setopt( Easy, CURLOPT_FOLLOWLOCATION, Request.FollowRedirects ? 1L : 0L );
	if( Request.Ipv4 )
		curl_easy_setopt( Easy, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4 );
	if( !Request.UserAgent.empty() )
		curl_easy_setopt( Easy, CURLOPT_USERAGENT, Request.UserAgent.c_str() );
	if( !Request.Accept.empty() )
		Impl->Headers = curl_slist_append( Impl->Headers, ("Accept: " + Request.Accept).c_str() );
	if( !Request.ContentType.empty() )
		Impl->Headers = curl_slist_append( Impl->Headers, ("Content-Type: " + Request.ContentType).c_str() );
	if( Impl->Headers )
		curl_easy_setopt( Easy, CURLOPT_HTTPHEADER, Impl->Headers );
	if( !Request.Body.empty() )
	{
		curl_easy_setopt( Easy, CURLOPT_POSTFIELDS, Request.Body.data() );
		curl_easy_setopt( Easy, CURLOPT_POSTFIELDSIZE, (long)Request.Body.size() );
	}
	curl_easy_setopt( Easy, CURLOPT_WRITEFUNCTION, Write );
	curl_easy_setopt( Easy, CURLOPT_WRITEDATA, this );
	curl_multi_add_handle( Impl->Multi, Easy );
}

FHttpTransfer::~FHttpTransfer()
{
	if( Impl->Multi && Impl->Easy )
		curl_multi_remove_handle( Impl->Multi, Impl->Easy );
	curl_easy_cleanup( Impl->Easy );
	curl_multi_cleanup( Impl->Multi );
	curl_slist_free_all( Impl->Headers );
	delete Impl;
}

bool FHttpTransfer::Poll()
{
	if( Done )
		return true;
	int Running = 0, Left = 0;
	curl_multi_perform( Impl->Multi, &Running );
	if( Running )
		return false;
	Done = true;
	CURLMsg* Message = curl_multi_info_read( Impl->Multi, &Left );
	CURLcode Code = Message && Message->msg == CURLMSG_DONE ? Message->data.result : CURLE_FAILED_INIT;
	curl_easy_getinfo( Impl->Easy, CURLINFO_RESPONSE_CODE, &Status );
	Complete = Code == CURLE_OK;
	// An error reply to a resumed request fails the range check too, it says nothing about resuming.
	RangeRefused = Code == CURLE_RANGE_ERROR && Status >= 200 && Status < 300;
	if( !Complete )
		Error = Impl->Reason[0] ? Impl->Reason : curl_easy_strerror( Code );
	else if( !Ok() )
		Error = "The server answered " + std::to_string( Status );
	return true;
}

void FHttpTransfer::Wait( int Milliseconds )
{
	if( !Done )
		curl_multi_poll( Impl->Multi, nullptr, 0, Milliseconds, nullptr );
}

#endif

bool FHttpTransfer::Run()
{
	while( !Poll() )
		Wait( 100 );
	return Ok();
}
