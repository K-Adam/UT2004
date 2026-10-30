/*=============================================================================
	ALAudioStream.cpp: Unreal OpenAL Audio interface object.
	Copyright 1999-2001 Epic Games, Inc. All Rights Reserved.

Revision history:
	* Created by Daniel Vogel.
	* Ported to Linux by Ryan C. Gordon.
=============================================================================*/

#include "ALAudioPrivate.h"

#pragma pack (push,8)
#include "vorbis/codec.h"
#include "vorbis/vorbisfile.h"
#pragma pack (pop)

#if __UNIX__
#undef clock
#include <pthread.h>
#define Sleep(x) usleep(x * 1000)
#define ExitThread(x)
static void *FileStreamingThread( void* Arg );
#include "FFileManagerUnix.h"
#else
static DWORD STDCALL FileStreamingThread( void* Arg );
#endif

static void FreeStreams( FALAudioStreamManager* StreamManager )
{
	FALAudioStream* Stream = StreamManager->First;
	while( Stream )
	{
		FALAudioStream* Next = Stream->GetNext();
		delete Stream;
		Stream = Next;
	}
}

#define VORBIS_BYTE_ORDER 0

/*------------------------------------------------------------------------------------
	FALAudioStream.
------------------------------------------------------------------------------------*/

//
//	FALAudioStream::FALAudioStream
//
FALAudioStream::FALAudioStream( FALAudioStream* InPrevious, FCriticalSection* InCriticalSection )
{
	if( InPrevious )
		InPrevious->Next = this;
	Previous		= InPrevious;
	Next			= NULL;

	FileHandle		= NULL;
	FileSeek		= 0;
	ChunksRequested	= 0;
	LoopCount		= 0;
	EndOfFile		= 0;

	CriticalSection	= InCriticalSection;
}

//
//	FALAudioStream::~FALAudioStream
//
FALAudioStream::~FALAudioStream()
{
	if( Previous )
		Previous->Next = Next;
	if( Next )
		Next->Previous = Previous;

	Previous	= NULL;
	Next		= NULL;

	if( FileHandle )
		fclose( FileHandle );
	FileHandle	= NULL;
}

//
//	FALAudioStream::Init
//
UBOOL FALAudioStream::Init( const TCHAR* Filename, void* InSharedData, EAudioStreamType InType )
{
	SharedData	= InSharedData;
	Type		= InType;

#if __UNIX__
	FileHandle	= ((FFileManagerUnix *) GFileManager)->fopenReadRespectHomeDir(Filename);
#else
	FileHandle	= TCHAR_CALL_OS( _wfopen(Filename,TEXT("rb")), fopen(TCHAR_TO_ANSI(Filename),"rb") );
#endif

	if( FileHandle == NULL )
		return 0;

	return 1;
}

//
//	FALAudioStream::RequestChunks
//
void FALAudioStream::RequestChunks( INT InChunksRequested, void* InSharedData )
{
	FScopeCriticalSection Lock( CriticalSection );

	ChunksRequested	= InChunksRequested;
	SharedData		= InSharedData;
}

//
//	FALAudioStream::QueryStream
//
UBOOL FALAudioStream::QueryStream( INT& ChunksQueued )
{
	FScopeCriticalSection Lock( CriticalSection );

	ChunksQueued = ChunksRequested;
	return !EndOfFile;
}


/*------------------------------------------------------------------------------------
	FALAudioStreamOgg.
------------------------------------------------------------------------------------*/

//
//	FALAudioStreamOgg::FALAudioStreamOgg
//
FALAudioStreamOgg::FALAudioStreamOgg( FALAudioStream* InPrevious, FCriticalSection* InCriticalSection )
: FALAudioStream( InPrevious, InCriticalSection )
{
	VorbisFile = NULL;
}

//
//	FALAudioStreamOgg::~FALAudioStreamOgg
//
FALAudioStreamOgg::~FALAudioStreamOgg()
{
	if( VorbisFile )
	{
		ov_clear( VorbisFile );
		// ov_clear() closes FileHandle, so don't let parent class destructor!
		FileHandle = NULL;
		delete VorbisFile;
		VorbisFile = NULL;
	}
}


// ov_open() on Win64 breaks without a custom fseek() callback, probably a
// C runtime conflict with ogg.
#ifdef _WIN64
static int _fseek64_wrapper(void *h, ogg_int64_t offset, int whence)
{
	return(fseek((FILE *) h, (long) offset, whence));
}
#endif


//
//	FALAudioStreamOgg::Init
//
UBOOL FALAudioStreamOgg::Init( const TCHAR* Filename, void* InSharedData, EAudioStreamType InType )
{
	if( !this->FALAudioStream::Init( Filename, InSharedData, InType ) )
		return 0;

	VorbisFile = new OggVorbis_File;

#ifdef _WIN64
	static ov_callbacks callbacks = {
		(size_t (*)(void *, size_t, size_t, void *))  fread,
		(int (*)(void *, ogg_int64_t, int))           _fseek64_wrapper,
		(int (*)(void *))                             fclose,
		(long (*)(void *))                            ftell
	};
                                  
	if( ov_open_callbacks((FILE*) FileHandle, VorbisFile, NULL, 0, callbacks ) < 0 )
		return 0;
#else
	if( ov_open( (FILE*) FileHandle, VorbisFile, NULL, 0 ) < 0 )
		return 0;
#endif

	vorbis_info *VorbisInfo = ov_info(VorbisFile, -1);
	Duration	= ov_time_total(VorbisFile, -1);
	Rate		= VorbisInfo->rate;
	ChunkSize	= MAX_STREAM_CHUNKSIZE;

	switch( VorbisInfo->channels )
	{
	case 1:
		Format = AL_FORMAT_MONO16;
		break;
	case 2:
		Format = AL_FORMAT_STEREO16;				
		break;
	default:
		Format = 0;	
		return 0;
		break;
	}

	return 1;
}

//
//	FALAudioStreamOgg::ReadChunks
//
UBOOL FALAudioStreamOgg::ReadChunks( INT NumChunks )
{
	if( !ChunksRequested )
		return 0;

	long	Bytes	= NumChunks * ChunkSize,
			Count	= 0;
	char*	Data	= (char*)SharedData;

	ChunksRequested = Max( 0, ChunksRequested - NumChunks );
	SharedData		= (char*)SharedData + NumChunks * ChunkSize;

	if( !Data || !Bytes )
		return 0;

	while( Count < Bytes )
	{
		long Read = ov_read( 
			VorbisFile, 
			Data + Count, 
			Bytes - Count, 
			VORBIS_BYTE_ORDER,
			2,
			1, 
			&FileSeek
		);

		if ( Read == 0 )
		{
			// Rewind stream if looping...
			if( Type == ST_OggLooping )
			{
				ov_time_seek( VorbisFile, 0.0 );
				LoopCount++;
			}
			// ... or fill with silence and mark EOF. (memset is used for reentrance reasons)
			else
			{
				memset( Data + Count, 0 , Bytes - Count );
				EndOfFile = 1;
				return 0;
			}
		}
		else
		if ( Read < 0 )
		{
			memset( Data + Count, 0 , Bytes - Count );
			return 0;
		}
		else
			Count += Read;
	}

	return 1;
}

//
//	FALAudioStreamOgg::Seek
//
INT FALAudioStreamOgg::Seek( FLOAT Seconds )
{
	FScopeCriticalSection Lock( CriticalSection );

	return ov_time_seek( VorbisFile, Seconds ) == 0;
}


/*------------------------------------------------------------------------------------
	FALAudioStreamManager.
------------------------------------------------------------------------------------*/

//
//	FALAudioStreamManager::CreateStream
//
FALAudioStream* FALAudioStreamManager::CreateStream( const TCHAR* Filename, INT InitialChunks, void* SharedData, EAudioStreamType Type, FLOAT InitialSeekTime )
{
	guard(FALAudioStreamManager::CreateStream);

	FScopeCriticalSection Lock( &CriticalSection );

	FALAudioStream* Stream = NULL;
		
	switch( Type )
	{
	case ST_Ogg:
	case ST_OggLooping:
		Stream = new FALAudioStreamOgg( Last, &CriticalSection );
		break;

	default:
		break;
	}

	if( !Stream )
		return NULL;
	
	if( Stream->Init( Filename, SharedData, Type ) )
	{
		if( !First )
			First = Stream;
		Last = Stream;

		if( InitialSeekTime > 0.01f )
			Stream->Seek( InitialSeekTime );

		Stream->RequestChunks( InitialChunks, SharedData );
		Stream->ReadChunks( InitialChunks );
		return Stream;
	}
	else
	{
		delete Stream;
		return NULL;
	}

	unguard;
}

//
//	FALAudioStreamManager::DestroyStream
//
void FALAudioStreamManager::DestroyStream( FALAudioStream* Stream, UBOOL ReadQueuedChunks )
{
	guard(FALAudioStreamManager::DestroyStream);

	FScopeCriticalSection Lock( &CriticalSection );

	if( !Stream )
		return;

	if( Stream == First )
		First = Stream->GetNext();
	if( Stream == Last )
		Last = Stream->GetPrevious();

	INT ChunksRequested;
	Stream->QueryStream( ChunksRequested );

	if( ReadQueuedChunks )
		Stream->ReadChunks( ChunksRequested );

	delete Stream;

	unguard;
}

//
//	FALAudioStreamManager::FALAudioStreamManager
//
FALAudioStreamManager::FALAudioStreamManager()
{
	guard(FALAudioStreamManager::FALAudioStreamManager);
	First		= NULL;
	Last		= NULL;
	Destroyed	= 0;
	unguard;
}

//
//	FALAudioStreamManager::~FALAudioStreamManager
//
FALAudioStreamManager::~FALAudioStreamManager()
{
	guard(FALAudioStreamManager::~FALAudioStreamManager);
	check(Destroyed);
	unguard;
}



//
//	FALAudioStreamManager::Init
//
void FALAudioStreamManager::Init()
{
	guard(FALAudioStreamManager::Init);

#if __EMSCRIPTEN__
	// No threads without SharedArrayBuffer, UALAudioSubsystem::Update reads the chunks instead.
#elif __UNIX__
	pthread_t FileStreamingThreadId = 0;
	int rc = pthread_create(&FileStreamingThreadId, NULL,
	                        FileStreamingThread, this);
	check(rc == 0);
	if (rc == 0)
		pthread_detach(FileStreamingThreadId);
#else
	HANDLE hThread = CreateThread( NULL, 0, FileStreamingThread, this, 0, &FileStreamingThreadId );
	check(hThread);
	CloseHandle( hThread );
#endif

	unguard;
}

//
//	FALAudioStreamManager::Destroy
//
void FALAudioStreamManager::Destroy()
{
	guard(FALAudioStreamManager::Destroy);
	if( Destroyed == 0 )
	{
#if __EMSCRIPTEN__
		FreeStreams( this );
		Destroyed = 2;
#else
		Destroyed = 1;
		debugf(TEXT("Waiting for file streaming thread to finish..."));
		while( Destroyed == 1 )
			appSleep(0.1f);
#endif
	}
	unguard;
}

void FALAudioStreamManager::ReadChunks()
{
	FScopeCriticalSection Lock( &CriticalSection );
	for( FALAudioStream* Stream = First; Stream; Stream = Stream->GetNext() )
		Stream->ReadChunks( 1 );
}

/*------------------------------------------------------------------------------------
	Streaming Thread.
------------------------------------------------------------------------------------*/

#if __UNIX__
static void *FileStreamingThread( void* Arg )
#else
static DWORD STDCALL FileStreamingThread( void* Arg )
#endif
{
	FALAudioStreamManager* StreamManager = (FALAudioStreamManager*) Arg;

	while( StreamManager->Destroyed == 0)
	{
		StreamManager->ReadChunks();

		// Don't eat up too much CPU time.
		Sleep(50);
	}

	// No longer have to worry about syncronisation at this point.
	FreeStreams( StreamManager );

	StreamManager->Destroyed = 2;
	
	ExitThread( 0 );
	return 0;
}

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/

