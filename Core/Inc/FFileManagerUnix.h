/*=============================================================================
	FFileManagerUnix.h: Unreal Unix based file manager.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Brandon Reinhart
		* Major changes by Daniel Vogel
=============================================================================*/

#ifndef _INCL_FFILEMANAGERUNIX_H_
#define _INCL_FFILEMANAGERUNIX_H_

#include <dirent.h>
#include <fnmatch.h>
#include <unistd.h>
#include "FFileManagerGeneric.h"
#include <pwd.h>
#include <stdlib.h>
#include <ctype.h>

#include "UnGnuG.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>

#if __EMSCRIPTEN__
// Emscripten/content.js reads the content files from IndexedDB, suspending the engine while it waits.
extern "C" int ut_content_read( int File, void* Data, int Count );
extern "C" void ut_content_open( const char* Path );
static inline ssize_t ReadFileData( int File, void* Data, INT Count ) { return ut_content_read( File, Data, Count ); }
#else
static inline ssize_t ReadFileData( int File, void* Data, INT Count ) { return read( File, Data, Count ); }
#endif

/*-----------------------------------------------------------------------------
	File Manager.
-----------------------------------------------------------------------------*/

class FArchiveFileReader : public FArchive
{
public:
	FArchiveFileReader( int InFile, FOutputDevice* InError, INT InSize )
	:	File			( InFile )
	,	Error			( InError )
	,	Size			( InSize )
	,	Pos				( 0 )
	,	BufferBase		( 0 )
	,	BufferCount		( 0 )
	{
		guard(FArchiveFileReader::FArchiveFileReader);
		ArIsLoading = ArIsPersistent = 1;
		unguard;
	}
	~FArchiveFileReader()
	{
		guard(FArchiveFileReader::~FArchiveFileReader);
		Close();
		unguard;
	}
	void Precache( INT HintCount )
	{
		guardSlow(FArchiveFileReader::Precache);
		checkSlow(Pos==BufferBase+BufferCount);
		BufferBase = Pos;
		BufferCount = Min( Min( Max( HintCount, 0 ), (INT)(ARRAY_COUNT(Buffer) - (Pos&(ARRAY_COUNT(Buffer)-1))) ), Size-Pos );
		DOUBLE	StartTime	= appSeconds();
		if( ReadFileData( File, Buffer, BufferCount ) != BufferCount )
		{
			ArIsError = 1;
			Error->Logf( TEXT("read failed: BufferCount=%i errno=%d"), BufferCount, (int) errno );
			return;
		}
		FLOAT	DeltaTime	= (FLOAT) (appSeconds() - StartTime);
		if( DeltaTime > 100.f ) DeltaTime = 0.0f; //!!
		GFileManagerBytesRead+=BufferCount; GFileManagerLoadingTime+=DeltaTime;
		unguardSlow;
	}
	void Seek( INT InPos )
	{
		guard(FArchiveFileReader::Seek);
		check(InPos>=0);
		check(InPos<=Size);
		if( lseek(File,InPos,SEEK_SET) == -1 )
		{
			ArIsError = 1;
			Error->Logf( TEXT("seek Failed %i/%i: %i %i"), InPos, Size, Pos, errno );
		}
		Pos         = InPos;
		BufferBase  = Pos;
		BufferCount = 0;
		unguard;
	}
	INT Tell()
	{
		return Pos;
	}
	INT TotalSize()
	{
		return Size;
	}
	UBOOL Close()
	{
		guardSlow(FArchiveFileReader::Close);
		if( File >= 0 )
			close( File );
		File = -1;
		return !ArIsError;
		unguardSlow;
	}
	void Serialize( void* V, INT Length )
	{
		guardSlow(FArchiveFileReader::Serialize);
		while( Length>0 )
		{
			INT Copy = Min( Length, BufferBase+BufferCount-Pos );
			if( Copy==0 )
			{
				if( Length >= ARRAY_COUNT(Buffer) )
				{
					if( ReadFileData( File, V, Length )!=Length )
					{
						ArIsError = 1;
						Error->Logf( TEXT("read failed: Length=%i errno=%i"), Length, (int) errno );
					}
					Pos += Length;
					BufferBase += Length;
					return;
				}
				Precache( MAXINT );
				Copy = Min( Length, BufferBase+BufferCount-Pos );
				if( Copy<=0 )
				{
					ArIsError = 1;
					Error->Logf( TEXT("ReadFile beyond EOF %i+%i/%i"), Pos, Length, Size );
				}
				if( ArIsError )
					return;
			}
			appMemcpy( V, Buffer+Pos-BufferBase, Copy );
			Pos       += Copy;
			Length    -= Copy;
			V          = (BYTE*)V + Copy;
		}
		unguardSlow;
	}
protected:
	int				File;
	FOutputDevice*	Error;
	INT				Size;
	INT				Pos;
	INT				BufferBase;
	INT				BufferCount;
	BYTE			Buffer[1024];
};




class FArchiveFileWriter : public FArchive
{
public:
	FArchiveFileWriter( int InFile, FOutputDevice* InError )
	:	File		( InFile )
	,	Error		( InError )
	,	Pos		(0)
	,	BufferCount	(0)
	{
		ArIsSaving = ArIsPersistent = 1;
	}
	~FArchiveFileWriter()
	{
		guard(FArchiveFileWriter::~FArchiveFileWriter);
		Close();
		unguard;
	}
	void Seek( INT InPos )
	{
		Flush();
		if( lseek(File,InPos,SEEK_SET) == -1 )
		{
			ArIsError = 1;
			Error->Logf( LocalizeError("SeekFailed",TEXT("Core")) );
		}
		Pos = InPos;
	}
	INT Tell()
	{
		return Pos;
	}
	UBOOL Close()
	{
		guardSlow(FArchiveFileWriter::Close);
		Flush();
		if( ( File >= 0 ) && ( close( File ) == -1 ) )
		{
			ArIsError = 1;
			Error->Logf( LocalizeError("WriteFailed",TEXT("Core")) );
		}
		File = -1;
		return !ArIsError;
		unguardSlow;
	}
	void Serialize( void* V, INT Length )
	{
		Pos += Length;
		INT Copy;
		while( Length > (Copy=ARRAY_COUNT(Buffer)-BufferCount) )
		{
			appMemcpy( Buffer+BufferCount, V, Copy );
			BufferCount += Copy;
			Length      -= Copy;
			V            = (BYTE*)V + Copy;
			Flush();
		}
		if( Length )
		{
			appMemcpy( Buffer+BufferCount, V, Length );
			BufferCount += Length;
		}
	}
	void Flush()
	{
		if( BufferCount && write( File, Buffer, BufferCount )!=BufferCount )
		{
			ArIsError = 1;
			Error->Logf( LocalizeError("WriteFailed",TEXT("Core")) );
		}
		BufferCount=0;
	}
protected:
	int				File;
	FOutputDevice*	Error;
	INT				Pos;
	INT				BufferCount;
	BYTE			Buffer[4096];
};

class FFileManagerUnix : public FFileManagerGeneric
{
public:
    FFileManagerUnix(void)
    {
        BaseDir[0] = 0;
    }


    static bool isdir(const char *fname)
    {
        struct stat statbuf;
        if (stat(fname, &statbuf) != -1)
            return( (S_ISDIR(statbuf.st_mode)) ? true : false );
        return(false);
    }


    // Used by the Ogg file stream.
    FILE *fopenReadRespectHomeDir(const TCHAR *FakeFilename)
    {
        if ( (FakeFilename == NULL) ||
             (appStrlen(FakeFilename) + appStrlen(BaseDir) >= PATH_MAX) )
            return NULL;

        char ansipath[PATH_MAX];
        TCHAR tmp[PATH_MAX];
		FILE* File = NULL;

		if ( BaseDir[0] )   // Use home dir?
        {
    		appStrcpy( tmp, BaseDir );
            appStrcat( tmp, FakeFilename );
            unixToANSI( ansipath, tmp );
    		char *Filename = appUnixPath(ansipath);
            if (!isdir(Filename))
                File = fopen(Filename, "rb");
        }

        if ( File == NULL )
        {
            unixToANSI( ansipath, FakeFilename );
            char *Filename = appUnixPath(ansipath);
            if (!isdir(Filename))
            {
#if __EMSCRIPTEN__
                ut_content_open(Filename);
#endif
                File = fopen(Filename, "rb");
            }
        }

        return(File);
    }

	UBOOL GetFileLastModTime( const TCHAR* FakeFilename, DWORD &Hours, DWORD &Minutes, DWORD &Secs, DWORD &Day, DWORD &Month, DWORD &Year )
	{
        FILE* File = fopenReadRespectHomeDir(FakeFilename);
		if ( !File )
			return 0;

		struct stat statbuf;
        int rc = fstat(fileno(File), &statbuf);
		fclose(File);
        if (rc == -1)
            return 0;

        struct tm tm;
        if (localtime_r(&statbuf.st_mtime, &tm) == NULL)
            return 0;

        Hours = (DWORD) (tm.tm_hour);  // 0 - 23
        Minutes = (DWORD) (tm.tm_min);  // 0 - 59
        Secs = (DWORD) (tm.tm_sec);  // 0 - 59, may be 60 in rare occasions.
        Day = (DWORD) (tm.tm_mday);  // 1 - 31
        Month = (DWORD) (tm.tm_mon + 1);   // 1 - 12
        Year = (DWORD) (tm.tm_year + 1900); // actual year, not offset.

        if (Secs > 59) Secs = 59;  // Pesky leap seconds!

        return 1;
	}

	FArchive* CreateFileReader( const TCHAR* FakeFilename, DWORD Flags, FOutputDevice* Error )
	{
		guard(FFileManagerUnix::CreateFileReader);		

        if ( (FakeFilename == NULL) ||
             (appStrlen(FakeFilename) + appStrlen(BaseDir) >= PATH_MAX) )
            return NULL;

        char ansipath[PATH_MAX];
        TCHAR tmp[PATH_MAX];
		int File = -1;
        struct stat statbuf;

		if ( BaseDir[0] )   // Use home dir?
        {
    		appStrcpy( tmp, BaseDir );
            appStrcat( tmp, FakeFilename );
            unixToANSI( ansipath, tmp );
    		char* Filename = appUnixPath(ansipath);
            if (!isdir(Filename))
    		    File = open(Filename, O_RDONLY, S_IRUSR | S_IWUSR);
        }

        if ( File < 0 )  // not in homedir? try game dir.
        {
            unixToANSI( ansipath, FakeFilename );
            char *Filename = appUnixPath(ansipath);
            if (!isdir(Filename))
		        File = open(Filename, O_RDONLY, S_IRUSR | S_IWUSR);
        }

		if ( ( File < 0 ) || ( fstat ( File, &statbuf ) < 0 ) )
		{
			if( Flags & FILEREAD_NoFail )
				appErrorf(TEXT("Failed to read file: %s"),FakeFilename);
			return NULL;
		}

		return new(TEXT("UnixFileReader"))FArchiveFileReader(File,Error,statbuf.st_size);
		unguard;
	}
	FArchive* CreateFileWriter( const TCHAR* FakeFilename, DWORD Flags, FOutputDevice* Error )
	{	
		guard(FFileManagerUnix::CreateFileWriter);

        if ( (FakeFilename == NULL) ||
             (appStrlen(FakeFilename) + appStrlen(BaseDir) >= PATH_MAX) )
            return NULL;

        char ansipath[PATH_MAX];
        TCHAR tmp[PATH_MAX];

		appStrcpy( tmp, FakeFilename[0]=='/' ? TEXT("") : BaseDir );
        appStrcat( tmp, FakeFilename );
        unixToANSI( ansipath, tmp );
		char* Filename = appUnixPath(ansipath);

        if (BaseDir[0])  // might need to build dirs in $HOME...
        {
            char ansitmp[PATH_MAX];
            strcpy(ansitmp, Filename);
            char *ptr = strrchr(ansitmp, '/');
            if (ptr != NULL)
            {
                for (ptr = ansitmp; *ptr; ptr++)  // build each chunk.
                {
                    if (*ptr == '/')
                    {
                        *ptr = '\0';
                        // don't care if it fails, since fopen() will tell us.
                        mkdir(ansitmp, S_IREAD | S_IWRITE | S_IEXEC);
                        *ptr = '/';
                    }
                }
            }
        }

	    if( Flags & FILEWRITE_EvenIfReadOnly )
			chmod(Filename, S_IREAD | S_IWRITE);
		if( (Flags & FILEWRITE_NoReplaceExisting) && (access(Filename, F_OK) != -1))
			return NULL;

        int Mode = O_WRONLY | O_CREAT | ((Flags & FILEWRITE_Append) ? O_APPEND : O_TRUNC);
		int File = open(Filename, Mode, S_IRUSR | S_IWUSR);
		if( File < 0 )
		{
			if( Flags & FILEWRITE_NoFail )
				appErrorf( TEXT("Failed to write: %s"), &Filename );
			return NULL;
		}
		return new(TEXT("UnixFileWriter"))FArchiveFileWriter(File,Error);

		unguard;
	}
	UBOOL Delete( const TCHAR* FakeFilename, UBOOL RequireExists=0, UBOOL EvenReadOnly=0 )
	{	       
		guard(FFileManagerUnix::Delete);

        if ( (FakeFilename == NULL) ||
             (appStrlen(FakeFilename) + appStrlen(BaseDir) >= PATH_MAX) )
            return 0;

        char ansipath[PATH_MAX];
        TCHAR tmp[PATH_MAX];

		appStrcpy( tmp, FakeFilename[0]=='/' ? TEXT("") : BaseDir );
        appStrcat( tmp, FakeFilename );
        unixToANSI( ansipath, tmp );
		char* Filename = appUnixPath(ansipath);
		if( EvenReadOnly )
			chmod(Filename, S_IREAD | S_IWRITE);
		return(unlink(Filename)==0 || (errno==ENOENT && !RequireExists));
		unguard;
	}
    // gam ---
    UBOOL IsReadOnly( const TCHAR* Filename )
    {
		guard(FFileManagerUnix::IsReadOnly);

        if ( (Filename == NULL) ||
             (appStrlen(Filename) >= PATH_MAX) )
            return 0;

        if( FileSize( Filename ) < 0 )
            return( 0 );

        char ansipath[PATH_MAX];
        unixToANSI(ansipath, Filename);
		return (access(appUnixPath(ansipath), W_OK) != 0);
		unguard;
    }
    // --- gam
	SQWORD GetGlobalTime( const TCHAR* Filename )
	{
		guard(FFileManagerUnix::GetGlobalTime);
		return 0;
		unguard;
	}
	UBOOL SetGlobalTime( const TCHAR* Filename )
	{
		guard(FFileManagerUnix::SetGlobalTime);
		return 0;	
		unguard;
	}
	UBOOL MakeDirectory( const TCHAR* FakePath, UBOOL Tree=0 )
	{
		guard(FFileManagerUnix::MakeDirectory);

        if ( (FakePath == NULL) ||
             (appStrlen(FakePath) + appStrlen(BaseDir) >= PATH_MAX) )
            return 0;

        char ansipath[PATH_MAX];
        TCHAR tmp[PATH_MAX];

		appStrcpy( tmp, FakePath[0]=='/' ? TEXT("") : BaseDir );
		TCHAR* Path = appStrcat( tmp, FakePath );
		if( Tree )
			return FFileManagerGeneric::MakeDirectory( Path, Tree );

        unixToANSI(ansipath, tmp);
		return mkdir(appUnixPath(ansipath), S_IREAD | S_IWRITE | S_IEXEC)==0 || errno==EEXIST;
		unguard;
	}
	UBOOL DeleteDirectory( const TCHAR* FakePath, UBOOL RequireExists=0, UBOOL Tree=0 )
	{
		guard(FFileManagerUnix::DeleteDirectory);

        if ( (FakePath == NULL) ||
             (appStrlen(FakePath) + appStrlen(BaseDir) >= PATH_MAX) )
            return 0;

        char ansipath[PATH_MAX];
        TCHAR tmp[PATH_MAX];

		appStrcpy( tmp, FakePath[0]=='/' ? TEXT("") : BaseDir );	
		TCHAR* Path = appStrcat( tmp, FakePath );	
		if( Tree )
			return FFileManagerGeneric::DeleteDirectory( Path, RequireExists, Tree );

        unixToANSI(ansipath, tmp);

		return rmdir(appUnixPath(ansipath))==0 || (errno==ENOENT && !RequireExists);
		unguard;
	}
	TArray<FString> FindFiles( const TCHAR* FakeFilename, UBOOL Files, UBOOL Directories )
	{
		guard(FFileManagerUnix::FindFiles);
		TArray<FString> Result;

		if ( BaseDir[0] )
        {
    		TCHAR* tmp = appStaticString1024();
    		appStrcpy( tmp, BaseDir );
		    TCHAR* Filename = appStrcat( tmp, FakeFilename );
			SearchDirectory( Result, Filename, Files, Directories );
        }
		SearchDirectory( Result, FakeFilename, Files, Directories );

		return Result;
		unguard;
	}
	UBOOL SetDefaultDirectory( const TCHAR* Filename )
	{
		guard(FFileManagerUnix::SetDefaultDirectory);

        if ( (Filename == NULL) ||
             (appStrlen(Filename) >= PATH_MAX) )
            return 0;

        char ansipath[PATH_MAX];
        unixToANSI(ansipath, Filename);

		return chdir(appUnixPath(ansipath))==0;
		unguard;
	}
	FString GetDefaultDirectory()
	{
		guard(FFileManagerUnix::GetDefaultDirectory);
		{
			ANSICHAR Buffer[PATH_MAX]="";
			getcwd( Buffer, ARRAY_COUNT(Buffer) );
			return appFromAnsi( Buffer );
		}
		unguard;
	}
	// gam ---
	INT CompareFileTimes( const TCHAR* FileA, const TCHAR* FileB )
	{
		guard(FFileManagerUnix::CompareFileTimes);
	
		struct stat StatA, StatB;

		if( stat( appToAnsi( FileA ), &StatA ) != 0 )
		    return 0;

		if( stat( appToAnsi( FileB ), &StatB ) != 0 )
		    return 0;
		    
        return( INT(StatB.st_mtime - StatA.st_mtime) );

        unguard;
	}
	// --- gam
	void Init(UBOOL Startup) 
	{
        if ((ParseParam(appCmdLine(),TEXT("nohomedir"))))
            BaseDir[0] = 0;
        else
            appStrcpy(BaseDir, CalcHomeDir());
	}

    const TCHAR *CalcHomeDir(void)
    {
        if (ParseParam(appCmdLine(),TEXT("nohomedir")))
            return(appBaseDir());

	    char *Home = getenv("HOME");
    	if ( Home == NULL )
    	{
    	    uid_t id = getuid();
    		struct passwd *pwd;
    
    		setpwent();
    		while( (pwd = getpwent()) != NULL )
    		{
    		    if( pwd->pw_uid == id )
    			{
    			    Home = pwd->pw_dir;
    				break;
    			}
    		}
    		endpwent();
    	}

    	TCHAR* tmp = appStaticString1024();
    	appStrcpy( tmp, appFromAnsi(Home) );

		appStrcat( tmp, TEXT("/.ut2004") );

        mkdir(appToAnsi(tmp), S_IRWXU);
        appStrcat( tmp, TEXT("/System") );
        mkdir(appToAnsi(tmp), S_IRWXU);

        // If STILL not accessible don't use it.
        if ( access( appToAnsi(tmp), F_OK ) == -1 )
        {
        }
        else
        {
            appStrcat( tmp, TEXT("/") );
        }

        return(tmp);
    }


protected:
	// Matches like Windows: case-insensitive, without "." and "..", and "*.*" takes names without a dot too.
	// The home and game directories can hold the same name, it is listed once.
	void SearchDirectory( TArray<FString> &Result, const TCHAR* Filename, UBOOL Files, UBOOL Directories )
	{
		guard(FFileManagerUnix::SearchDirectory);
		char Path[PATH_MAX], Pattern[PATH_MAX];
		unixToANSI( Pattern, Filename );
		strcpy( Path, appUnixPath(Pattern) );

		char* Slash = strrchr( Path, '/' );
		strcpy( Pattern, Slash ? Slash + 1 : Path );
		if( Slash )
			Slash[1] = '\0';
		else
			strcpy( Path, "./" );
		if( strcmp( Pattern, "*.*" ) == 0 )
			strcpy( Pattern, "*" );

		DIR* Dir = opendir( Path );
		if( !Dir )
			return;
		while( struct dirent* Entry = readdir( Dir ) )
		{
			if( !strcmp( Entry->d_name, "." ) || !strcmp( Entry->d_name, ".." ) || fnmatch( Pattern, Entry->d_name, FNM_CASEFOLD ) != 0 )
				continue;
			char Full[PATH_MAX * 2];
			snprintf( Full, sizeof(Full), "%s%s", Path, Entry->d_name );
			struct stat Info;
			if( stat( Full, &Info ) != 0 || !(S_ISDIR( Info.st_mode ) ? Directories : Files) )
				continue;
			FString Name( appFromAnsi( Entry->d_name ) );
			if( Result.FindItemIndex( Name ) == INDEX_NONE )
				new(Result)FString( Name );
		}
		closedir( Dir );
		unguard;
	}


	TCHAR BaseDir[PATH_MAX];
};

#endif  // include-once blocker

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/

