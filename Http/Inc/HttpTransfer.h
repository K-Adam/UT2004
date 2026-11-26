// HTTP requests to the port's hosts, curl natively and the browser's fetch on the web page.
// Only standard types, the launcher downloads the content before the engine is up.

#ifndef _INCL_HTTPTRANSFER_H_
#define _INCL_HTTPTRANSFER_H_

#pragma pack(push,8)
#include <functional>
#include <string>

struct FHttpRequest
{
	std::string Url;
	std::string Body;				// Sent as a POST when not empty.
	std::string Accept;
	std::string ContentType;		// Native only, a page's request without headers needs no CORS preflight.
	std::string UserAgent;			// Native only.
	long ConnectTimeout = 10;		// Seconds.
	long Timeout = 0;				// Seconds for the whole transfer, 0 for none.
	long StallTime = 0;				// Seconds below 1 KB/s before it gives up, 0 for never. Native only.
	long long ResumeFrom = 0;		// Native only.
	bool FollowRedirects = false;	// Native only, the browser always follows.
	bool Ipv4 = false;				// Native only.
	size_t MaxSize = 0;				// Bytes of reply, 0 for no limit.
	// Takes a successful reply as it arrives, on the web page all at once at the end. False stops the transfer.
	// Without it every reply collects in Body, a refusal's too.
	std::function<bool( const char*, size_t )> Receive;
};

class FHttpTransfer
{
public:
	explicit FHttpTransfer( const FHttpRequest& InRequest );
	~FHttpTransfer();
	FHttpTransfer( const FHttpTransfer& ) = delete;
	FHttpTransfer& operator=( const FHttpTransfer& ) = delete;

	// Moves the transfer on, true once it is over.
	bool Poll();
	// Blocks until the transfer has news or Milliseconds pass. Native only.
	void Wait( int Milliseconds );
	// Polls and waits until the transfer is over. Native only.
	bool Run();
	bool Ok() const { return Complete && Status >= 200 && Status < 300; }

	bool Done = false;
	bool Complete = false;		// The whole reply arrived.
	bool RangeRefused = false;	// The server can't resume.
	long Status = 0;			// 0 when no reply came.
	long long Received = 0;
	std::string Body;
	std::string Error;

	struct FImpl;
private:
	FHttpRequest Request;
	FImpl* Impl;

	static size_t Write( char* Data, size_t Size, size_t Count, void* Transfer );
};

// The CA certificates where OpenSSL has no store of its own, NULL where it has one.
const std::string* SystemCaBundle();
#pragma pack(pop)

#endif
