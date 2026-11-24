/*=============================================================================
	UnFile.cpp: ANSI C core.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Tim Sweeney
=============================================================================*/

// The standard headers go before the engine's, whose macros (check, clock, min, max) break them
#pragma pack(push,8)
#include <chrono>
#include <ctime>
#include <mutex>
#include <thread>
#pragma pack(pop)

// Core includes.
#include "CorePrivate.h"

// To help ANSI out.
#undef clock
#undef unclock

// ANSI C++ includes.
#include <math.h>
#include <float.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>

#    include <malloc.h>
#include <sys/types.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <io.h>
#endif

/*-----------------------------------------------------------------------------
	Time.
-----------------------------------------------------------------------------*/

CORE_API DOUBLE appSeconds()
{
	static const std::chrono::steady_clock::time_point Start = std::chrono::steady_clock::now();
	return std::chrono::duration<DOUBLE>( std::chrono::steady_clock::now() - Start ).count();
}

// 100 ns ticks. Nanoseconds would wrap the DWORD every 4 seconds, too soon for SavePackage and the script StopWatch.
CORE_API DWORD appCycles()
{
	typedef std::chrono::duration<QWORD,std::ratio<1,10000000>> FTicks;
	return (DWORD)std::chrono::duration_cast<FTicks>( std::chrono::steady_clock::now().time_since_epoch() ).count();
}

// 0 gives up the time slice, as Sleep(0) does
CORE_API void appSleep( FLOAT Seconds )
{
	if( Seconds > 0.f )
		std::this_thread::sleep_for( std::chrono::duration<DOUBLE>( Seconds ) );
	else
		std::this_thread::yield();
}

// localtime is not thread safe, and C++17 has no portable thread safe one
static void LocalTime( time_t Time, tm& Out )
{
#if _WIN32
	localtime_s( &Out, &Time );
#else
	localtime_r( &Time, &Out );
#endif
}

CORE_API void appSystemTime( INT& Year, INT& Month, INT& DayOfWeek, INT& Day, INT& Hour, INT& Min, INT& Sec, INT& MSec )
{
	std::chrono::system_clock::time_point Now = std::chrono::system_clock::now();
	tm Local;
	LocalTime( std::chrono::system_clock::to_time_t( Now ), Local );
	Year		= Local.tm_year + 1900;
	Month		= Local.tm_mon + 1;
	DayOfWeek	= Local.tm_wday;
	Day			= Local.tm_mday;
	Hour		= Local.tm_hour;
	Min			= Local.tm_min;
	Sec			= Local.tm_sec;
	MSec		= (INT)(std::chrono::duration_cast<std::chrono::milliseconds>( Now.time_since_epoch() ).count() % 1000);
}

// The format Windows always wrote, demo files carry it
CORE_API const TCHAR* appTimestamp()
{
	tm Local;
	LocalTime( time( NULL ), Local );
	ANSICHAR Text[32];
	strftime( Text, sizeof(Text), "%m/%d/%y %H:%M:%S", &Local );
	TCHAR* Result = appStaticString1024();
	appStrcpy( Result, appFromAnsi( Text ) );
	return Result;
}

/*-----------------------------------------------------------------------------
	File cache.
-----------------------------------------------------------------------------*/

// Days since the file was written, 0 when it can't be read
static INT GetFileAgeDays( const TCHAR* Filename )
{
#if _WIN32
	struct _stat Buf;
	if( _wstat( Filename, &Buf ) )
		return 0;
#else
	struct stat Buf;
	if( stat( appUnixPath( Filename ), &Buf ) )
		return 0;
#endif
	return (INT)(difftime( time( NULL ), Buf.st_mtime ) / 86400.0);
}

CORE_API void appCleanFileCache()
{
	guard(appCleanFileCache);
	FString Temp = FString::Printf( TEXT("%s") PATH_SEPARATOR TEXT("*.tmp"), *GSys->CachePath );
	TArray<FString> Found = GFileManager->FindFiles( *Temp, 1, 0 );
	for( INT i=0; i<Found.Num(); i++ )
	{
		Temp = FString::Printf( TEXT("%s") PATH_SEPARATOR TEXT("%s"), *GSys->CachePath, *Found(i) );
		debugf( TEXT("Deleting temporary file: %s"), *Temp );
		GFileManager->Delete( *Temp );
	}

	if( GSys->PurgeCacheDays )
	{
		Found = GFileManager->FindFiles( *(GSys->CachePath * TEXT("*") + GSys->CacheExt), 1, 0 );
		for( INT i=0; i<Found.Num(); i++ )
		{
			Temp = FString::Printf( TEXT("%s") PATH_SEPARATOR TEXT("%s"), *GSys->CachePath, *Found(i) );
			// The cache path is relative to the file manager's folders, not to the working directory
			INT DiffDays = GetFileAgeDays( *GFileManager->ExpandPath(*Temp) );
			if( DiffDays > GSys->PurgeCacheDays )
			{
				debugf( TEXT("Purging outdated file from cache: %s (%i days old)"), *Temp, DiffDays );
				GFileManager->Delete( *Temp );
			}
		}
	}
	unguard;
}

/*-----------------------------------------------------------------------------
	Locking.
-----------------------------------------------------------------------------*/

// Recursive, the audio stream manager locks again in calls it makes while it holds the lock
FCriticalSection::FCriticalSection()
:	Handle( new std::recursive_mutex )
{}

FCriticalSection::~FCriticalSection()
{
	delete (std::recursive_mutex*)Handle;
}

void FCriticalSection::Lock()
{
	((std::recursive_mutex*)Handle)->lock();
}

void FCriticalSection::Unlock()
{
	((std::recursive_mutex*)Handle)->unlock();
}

FScopeCriticalSection::FScopeCriticalSection( FCriticalSection* InCriticalSection )
:	CriticalSection( InCriticalSection )
{
	CriticalSection->Lock();
}

FScopeCriticalSection::~FScopeCriticalSection()
{
	CriticalSection->Unlock();
}

//
// Get a GMT Ref
//
CORE_API FString appGetGMTRef()
{
	guard(appGetGMTRef);
	struct tm *newtime;
	TCHAR* GMTRef = appStaticString1024();
	time_t ltime, gtime;
	FLOAT diff;
	time( &ltime );
	newtime = gmtime( &ltime );
	gtime = mktime(newtime);
	diff = (ltime - gtime) / 3600;
	appSprintf( GMTRef, (diff>0)?TEXT("+%1.1f"):TEXT("%1.1f"), diff );
	return GMTRef;
	unguard;
}

/*-----------------------------------------------------------------------------
	Math functions.
-----------------------------------------------------------------------------*/

static INT		SRandSeed = 0;
static FLOAT	SRandTemp = 1.f;

CORE_API void appSRandInit( INT Seed )
{
	SRandSeed = Seed;
}
CORE_API FLOAT appSRand() 
{ 
	SRandSeed = (SRandSeed * 196314165) + 907633515; 
	FLOAT Result;
	*(INT*)&Result = (*(INT*)&SRandTemp & 0xff800000) | (SRandSeed & 0x007fffff);
	return appFractional(Result); 
} 

/*-----------------------------------------------------------------------------
	Memory functions.
-----------------------------------------------------------------------------*/

CORE_API INT appMemcmp( const void* Buf1, const void* Buf2, INT Count )
{
	return memcmp( Buf1, Buf2, Count );
}

CORE_API UBOOL appMemIsZero( const void* V, int Count )
{
	guardSlow(appMemIsZero);
	BYTE* B = (BYTE*)V;
	while( Count-- > 0 )
		if( *B++ != 0 )
			return 0;
	return 1;
	unguardSlow;
}

CORE_API void* appMemmove( void* Dest, const void* Src, INT Count )
{
	return memmove( Dest, Src, Count );
}

CORE_API void appMemset( void* Dest, INT C, INT Count )
{
	memset( Dest, C, Count );
}

#ifndef DEFINED_appMemzero
CORE_API void appMemzero( void* Dest, INT Count )
{
	memset( Dest, 0, Count );
}
#endif

#ifndef DEFINED_appMemcpy
CORE_API void appMemcpy( void* Dest, const void* Src, INT Count )
{
	memcpy( Dest, Src, Count );
}
#endif

/*-----------------------------------------------------------------------------
	String functions.
-----------------------------------------------------------------------------*/

//
// Copy a string with length checking.
//warning: Behavior differs from strncpy; last character is zeroed.
//
TCHAR* appStrncpy( TCHAR* Dest, const TCHAR* Src, INT MaxLen )
{
	guard(appStrncpy);

#if UNICODE
	wcsncpy( Dest, Src, MaxLen );
#else
	strncpy( Dest, Src, MaxLen );
#endif
	Dest[MaxLen-1]=0;
	return Dest;

	unguard;
}

//
// Concatenate a string with length checking
//
TCHAR* appStrncat( TCHAR* Dest, const TCHAR* Src, INT MaxLen )
{
	guard(appStrncat);
	INT Len = appStrlen(Dest);
	TCHAR* NewDest = Dest + Len;
	if( (MaxLen-=Len) > 0 )
	{
		appStrncpy( NewDest, Src, MaxLen );
		NewDest[MaxLen-1] = 0;
	}
	return Dest;
	unguard;
}

//
// Standard string functions.
//
CORE_API INT appSprintf( TCHAR* Dest, const TCHAR* Fmt, ... )
{
	int Result;
	GET_VARARGS_RESULT(Dest,1024,Fmt,Fmt,Result);
	return Result;
}

CORE_API INT appSnprintf( TCHAR* Dest, INT Count, const TCHAR* Fmt, ... )
{
	INT Result;
	GET_VARARGS_RESULT(Dest,Count,Fmt,Fmt,Result);
	Dest[Count-1] = 0;
	return Result;
}

CORE_API INT appStrlen( const TCHAR* String )
{
#if UNICODE
	return (INT) wcslen( String );
#else
	return (INT) strlen( String );
#endif
}

CORE_API TCHAR* appStrstr( const TCHAR* String, const TCHAR* Find )
{
#if UNICODE
	return (TCHAR *) wcsstr( String, Find );
#else
	return (TCHAR *) strstr( String, Find );
#endif
}

CORE_API TCHAR* appStrchr( const TCHAR* String, int c )
{
#if UNICODE
	return (TCHAR *) wcschr( String, c );
#else
	return (TCHAR *) strchr( String, c );
#endif
}

CORE_API TCHAR* appStrcat( TCHAR* Dest, const TCHAR* Src )
{
#if UNICODE
	return wcscat( Dest, Src );
#else
	return strcat( Dest, Src );
#endif
}

CORE_API INT appStrcmp( const TCHAR* String1, const TCHAR* String2 )
{
#if UNICODE
	return wcscmp( String1, String2 );
#else
	return strcmp( String1, String2 );
#endif
}

CORE_API INT appStricmp( const TCHAR* String1, const TCHAR* String2 )
{
#if UNICODE
	return _wcsicmp( String1, String2 );
#else
	return stricmp( String1, String2 );
#endif
}

CORE_API TCHAR* appStrcpy( TCHAR* Dest, const TCHAR* Src )
{
#if UNICODE
	return wcscpy( Dest, Src );
#else
	return strcpy( Dest, Src );
#endif
}

CORE_API TCHAR* appStrupr( TCHAR* String )
{
#if UNICODE
	return _wcsupr( String );
#else
	return strupr( String );
#endif
}

CORE_API INT appAtoi( const TCHAR* Str )
{
#if UNICODE
	return _wtoi( Str );
#else
	return atoi( Str );
#endif
}

CORE_API TCHAR* appItoa( const INT Num )
{
#if UNICODE
	static TCHAR Buffer[20];
	appMemzero( Buffer, 20*sizeof(TCHAR) );
	return _itow( Num, Buffer, 10 );
#else
	static char Buffer[20];
	appMemzero( Buffer, 20*sizeof(char) );
  #if __UNIX__
    snprintf(Buffer, sizeof (Buffer), "%d", Num);
    return(Buffer);
  #else
	return _itoa( Num, Buffer, 10 );
  #endif
#endif
}

CORE_API FLOAT appAtof( const TCHAR* Str )
{
#if UNICODE
	return _wtof( Str );
#else
	return atof( Str );
#endif
}

CORE_API INT appStrtoi( const TCHAR* Start, TCHAR** End, INT Base )
{
#if UNICODE
	return wcstoul( Start, End, Base );
#else
	return strtoul( Start, End, Base );
#endif
}

CORE_API INT appStrncmp( const TCHAR* A, const TCHAR* B, INT Count )
{
#if UNICODE
	return wcsncmp( A, B, Count );
#else
	return strncmp( A, B, Count );
#endif
}

CORE_API INT appStrnicmp( const TCHAR* A, const TCHAR* B, INT Count )
{
#if UNICODE
	return _wcsnicmp( A, B, Count );
#else
	return strnicmp( A, B, Count );
#endif
}

CORE_API QWORD appStrtoq( const TCHAR* Start, TCHAR** End, INT Base )
{
	return _wcstoui64( Start, End, Base );
}

CORE_API TCHAR* appQtoa( QWORD Num, INT Base )
{
	static TCHAR Buffer[30];
	appMemzero( Buffer, sizeof(Buffer) );
	return _ui64tow( Num, Buffer, Base );
}

/*-----------------------------------------------------------------------------
	Sorting.
-----------------------------------------------------------------------------*/

CORE_API void appQsort( void* Base, INT Num, INT Width, int(CDECL *Compare)(const void* A, const void* B ) )
{
	qsort( Base, Num, Width, Compare );
}

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/

