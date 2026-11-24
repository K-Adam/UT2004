/*=============================================================================
    FFileManagerWindows.h: Unreal Windows OS based file manager.
    Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.

    Revision history:
        * Created by Tim Sweeney
=============================================================================*/

#include "FFileManagerGeneric.h"

#include <sys/types.h>
#include <sys/stat.h>

#define DETAILED_STATS 0

/*-----------------------------------------------------------------------------
    File Manager.
-----------------------------------------------------------------------------*/

class FScopeMiscTime
{
public:
	FScopeMiscTime()
	{
		StartTime = appSeconds();
	}
	~FScopeMiscTime()
	{
		FLOAT DeltaTime = appSeconds() - StartTime;
		if( DeltaTime < 100.f ) //!! need to debug this - I guess processor affinity is borked in the very beginning
			GFileManagerMiscTime += DeltaTime;
	}
private:
	DOUBLE StartTime;
};

// File manager.
class FArchiveFileReader : public FArchive
{
public:
    FArchiveFileReader( HANDLE InHandle, FOutputDevice* InError, INT InSize, const TCHAR* InFilename )
    :   Handle          ( InHandle )
    ,   Error           ( InError )
    ,   Size            ( InSize )
    ,   Pos             ( 0 )
    ,   BufferBase      ( 0 )
    ,   BufferCount     ( 0 )
	,	BytesRead		( 0 )
	,	LoadingTime		( 0 )
	,	SeekTime		( 0 )
    {
        guard(FArchiveFileReader::FArchiveFileReader);
        ArIsLoading = ArIsPersistent = 1;
		appStrcpy( (TCHAR*) Filename, InFilename );
        unguard;
    }
    ~FArchiveFileReader()
    {
        guard(FArchiveFileReader::~FArchiveFileReader);
        if( Handle )
            Close();
        unguard;
    }
    void Precache( INT HintCount )
    {
        guardSlow(FArchiveFileReader::Precache);
        checkSlow(Pos==BufferBase+BufferCount);
        BufferBase			= Pos;
        BufferCount			= Min( Min( Max( HintCount, 0 ), (INT)(ARRAY_COUNT(Buffer) - (Pos&(ARRAY_COUNT(Buffer)-1))) ), Size-Pos );
        INT		Count		= 0;
		DOUBLE	StartTime	= appSeconds();
        ReadFile( Handle, Buffer, BufferCount, (DWORD*)&Count, NULL );
		FLOAT	DeltaTime	= appSeconds() - StartTime;
		if( DeltaTime > 100.f ) DeltaTime = 0; //!!
		GFileManagerBytesRead+=Count; BytesRead+=Count; LoadingTime+=DeltaTime; GFileManagerLoadingTime+=DeltaTime;
        if( Count!=BufferCount )
        {
            ArIsError = 1;
            Error->Logf( TEXT("ReadFile failed: Count=%i BufferCount=%i Error=%s"), Count, BufferCount, appGetSystemErrorMessage() );
        }
        unguardSlow;
    }
    void Seek( INT InPos )
    {
        guard(FArchiveFileReader::Seek);
        check(InPos>=0);
		if ( InPos > Size )
			appErrorf(NAME_FriendlyError, TEXT("InPos>Size in FFileManagerWindows.  This error is probably due to either your computer overheating, or a corrupted installation of your game.  If this error persists, please try re-installing the game."));

		DOUBLE StartTime=appSeconds();
        if( SetFilePointer( Handle, InPos, 0, FILE_BEGIN )==0xFFFFFFFF )
        {
            ArIsError = 1;
            Error->Logf( TEXT("SetFilePointer Failed %i/%i: %i %s"), InPos, Size, Pos, appGetSystemErrorMessage() );
        }
		DOUBLE DeltaTime = appSeconds()-StartTime;
		if( DeltaTime < 100.f )
		{
			SeekTime			   += DeltaTime;
			GFileManagerSeekTime   += DeltaTime;
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
        if( Handle )
            CloseHandle( Handle );
        Handle = NULL;
#if DETAILED_STATS
		if( GLog )
			GLog->Logf( TEXT("FFileManagerWindows: Reading %i Bytes from file %s took %f seconds (%f reading, %f seeking)"), BytesRead, Filename, LoadingTime + SeekTime, LoadingTime, SeekTime );
#endif
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
                    INT		Count		= 0;
					DOUBLE	StartTime	= appSeconds();
                    ReadFile( Handle, V, Length, (DWORD*)&Count, NULL );
					FLOAT	DeltaTime	= appSeconds()-StartTime;
					if( DeltaTime > 100.f ) DeltaTime = 0; //!!
					GFileManagerBytesRead+=Count; BytesRead+=Count; LoadingTime+=DeltaTime; GFileManagerLoadingTime+=DeltaTime;
                    if( Count!=Length )
                    {
                        ArIsError = 1;
                        Error->Logf( TEXT("ReadFile failed: Count=%i Length=%i Error=%s"), Count, Length, appGetSystemErrorMessage() );
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
    HANDLE          Handle;
    FOutputDevice*  Error;
    INT             Size;
    INT             Pos;
    INT             BufferBase;
    INT             BufferCount;
    BYTE            Buffer[1024];
	INT				BytesRead;
	FLOAT			LoadingTime;
	FLOAT			SeekTime;
	TCHAR*			Filename[MAX_PATH];
};
class FArchiveFileWriter : public FArchive
{
public:
    FArchiveFileWriter( HANDLE InHandle, FOutputDevice* InError, INT InPos )
    :   Handle      ( InHandle )
    ,   Error       ( InError )
    ,   Pos         ( InPos )
    ,   BufferCount ( 0 )
    {
        ArIsSaving = ArIsPersistent = 1;
    }
    ~FArchiveFileWriter()
    {
        guard(FArchiveFileWriter::~FArchiveFileWriter);
        if( Handle )
            Close();
        Handle = NULL;
        unguard;
    }
    void Seek( INT InPos )
    {
        Flush();
        if( SetFilePointer( Handle, InPos, 0, FILE_BEGIN )==0xFFFFFFFF )
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
        if( Handle && !CloseHandle(Handle) )
        {
            ArIsError = 1;
            Error->Logf( LocalizeError("WriteFailed",TEXT("Core")) );
        }
		Handle = NULL;
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
        if( BufferCount )
        {
            INT Result=0;
            if( !WriteFile( Handle, Buffer, BufferCount, (DWORD*)&Result, NULL ) )
            {
                ArIsError = 1;
                Error->Logf( LocalizeError("WriteFailed",TEXT("Core")) );
            }
        }
        BufferCount = 0;
    }
protected:
    HANDLE          Handle;
    FOutputDevice*  Error;
    INT             Pos;
    INT             BufferCount;
    BYTE            Buffer[4096];
};
class FFileManagerWindows : public FFileManagerGeneric
{
public:
    FArchive* CreateFileReader( const TCHAR* Filename, DWORD Flags, FOutputDevice* Error )
    {
        guard(FFileManagerWindows::CreateFileReader);
		FScopeMiscTime MiscTime;
		DWORD  Access    = GENERIC_READ;
        DWORD  WinFlags  = FILE_SHARE_READ;
        DWORD  Create    = OPEN_EXISTING;
        HANDLE Handle    = CreateFileW( Filename, Access, WinFlags, NULL, Create, FILE_ATTRIBUTE_NORMAL, NULL );
		if( Handle==INVALID_HANDLE_VALUE )
        {
            if( Flags & FILEREAD_NoFail )
                appErrorf( TEXT("Failed to read file: %s"), Filename );
            return NULL;
        }
        return new(TEXT("WindowsFileReader"))FArchiveFileReader(Handle,Error,GetFileSize(Handle,NULL),Filename);
        unguard;
    }
    FArchive* CreateFileWriter( const TCHAR* Filename, DWORD Flags, FOutputDevice* Error )
    {
        guard(FFileManagerWindows::CreateFileWriter);
		FScopeMiscTime MiscTime; //!! counts FileSize twice
	    if( (GFileManager->FileSize (Filename) >= 0) && (Flags & FILEWRITE_EvenIfReadOnly) ) // gam
            SetFileAttributesW(Filename, 0);
		DWORD  Access    = GENERIC_WRITE;
        DWORD  WinFlags  = (Flags & FILEWRITE_AllowRead) ? FILE_SHARE_READ : 0;
        DWORD  Create    = (Flags & FILEWRITE_Append) ? OPEN_ALWAYS : (Flags & FILEWRITE_NoReplaceExisting) ? CREATE_NEW : CREATE_ALWAYS;
        HANDLE Handle    = CreateFileW( Filename, Access, WinFlags, NULL, Create, FILE_ATTRIBUTE_NORMAL, NULL );
        INT    Pos       = 0;
        if( Handle==INVALID_HANDLE_VALUE )
        {
            if( Flags & FILEWRITE_NoFail )
                appErrorf( TEXT("Failed to create file: %s"), Filename );
			return NULL;
        }
        if( Flags & FILEWRITE_Append )
            Pos = SetFilePointer( Handle, 0, 0, FILE_END );
		return new(TEXT("WindowsFileWriter"))FArchiveFileWriter(Handle,Error,Pos);
        unguard;
    }
    INT FileSize( const TCHAR* Filename )
    {
        guard(FFileManagerWindows::FileSize);
		FScopeMiscTime MiscTime;
		HANDLE Handle = CreateFileW( Filename, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL );
        if( Handle==INVALID_HANDLE_VALUE )
			return -1;
        DWORD Result = GetFileSize( Handle, NULL );
        CloseHandle( Handle );
		return Result;
        unguard;
    }
    DWORD Copy( const TCHAR* DestFile, const TCHAR* SrcFile, UBOOL ReplaceExisting, UBOOL EvenIfReadOnly, UBOOL Attributes, DWORD Compress, FCopyProgress* Progress )
    {
		FScopeMiscTime MiscTime;
		if( EvenIfReadOnly )
            SetFileAttributesW(DestFile, 0);
        DWORD Result;
        if( Progress || Compress != FILECOPY_Normal )
			Result = FFileManagerGeneric::Copy( DestFile, SrcFile, ReplaceExisting, EvenIfReadOnly, Attributes, Compress, Progress );
		else
		{
			if( CopyFileW(SrcFile, DestFile, !ReplaceExisting) != 0)
				Result = COPY_OK;
			else
				Result = COPY_MiscFail;
		}
        if( Result==COPY_OK && !Attributes )
            SetFileAttributesW(DestFile, 0);
        return Result;
    }
    UBOOL Delete( const TCHAR* Filename, UBOOL RequireExists=0, UBOOL EvenReadOnly=0 )
    {
        guard(FFileManagerWindows::Delete);
		FScopeMiscTime MiscTime;
        if( EvenReadOnly )
            SetFileAttributesW(Filename,FILE_ATTRIBUTE_NORMAL);
        // gam ---
        INT Result = DeleteFile(Filename)!=0 || (!RequireExists && GetLastError()==ERROR_FILE_NOT_FOUND);
        if( !Result )
        {
            // gam ---
            DWORD error = GetLastError();
            debugf( NAME_Warning, TEXT("Error deleting file '%s' (0x%d)"), Filename, error );
            // --- gam
        }
        return Result!=0;
        // --- gam
        unguard;
    }
    // gam ---
    UBOOL IsReadOnly( const TCHAR* Filename )
    {
        guard(FFileManagerWindows::IsReadOnly);
        DWORD rc;
        if( FileSize( Filename ) < 0 )
            return( 0 );
		else
		{
			FScopeMiscTime MiscTime;
			rc = GetFileAttributesW(Filename);
			if (rc != 0xFFFFFFFF)
				return ((rc & FILE_ATTRIBUTE_READONLY) != 0);
			else
			{
				debugf( NAME_Warning, TEXT("Error reading attributes for '%s'"), Filename );
				return (0);
			}
		}
        unguard;
    }
    // --- gam
    UBOOL Move( const TCHAR* Dest, const TCHAR* Src, UBOOL Replace=1, UBOOL EvenIfReadOnly=0, UBOOL Attributes=0 )
    {
        guard(FFileManagerWindows::Move);
		//warning: MoveFileEx is broken on Windows 95 (Microsoft bug).
        Delete( Dest, 0, EvenIfReadOnly );
		INT Result;
		{
	        FScopeMiscTime MiscTime;
			Result = MoveFileW(Src,Dest);
			if( !Result )
			{
				// gam ---
				DWORD error = GetLastError();
				debugf( NAME_Warning, TEXT("Error moving file '%s' to '%s' (%d)"), Src, Dest, error );
				// --- gam
			}
		}
		return Result!=0;
        unguard;
    }
    SQWORD GetGlobalTime( const TCHAR* Filename )
    {
        //return grenwich mean time as expressed in nanoseconds since the creation of the universe.
        //time is expressed in meters, so divide by the speed of light to obtain seconds.
        //assumes the speed of light in a vacuum is constant.
        //the file specified by Filename is assumed to be in your reference frame, otherwise you
        //must transform the result by the path integral of the minkowski metric tensor in order to
        //obtain the correct result.
        return 0;
    }
    UBOOL SetGlobalTime( const TCHAR* Filename )
    {
        return 0;//!!
    }
    UBOOL MakeDirectory( const TCHAR* Path, UBOOL Tree=0 )
    {
        guard(FFileManagerWindows::MakeDirectory);
        // gam ---
		FString ExpandedPath = ExpandPath( Path );
        if( Tree )
            return FFileManagerGeneric::MakeDirectory( *ExpandedPath, Tree );
		else
		{
			FScopeMiscTime MiscTime;
			return CreateDirectoryW(*ExpandedPath,NULL)!=0 || GetLastError()==ERROR_ALREADY_EXISTS;
		}
        // --- gam
        unguard;
    }
    UBOOL DeleteDirectory( const TCHAR* Path, UBOOL RequireExists=0, UBOOL Tree=0 )
    {
        guard(FFileManagerWindows::DeleteDirectory);
		if( Tree )
            return FFileManagerGeneric::DeleteDirectory( Path, RequireExists, Tree );
		else
		{
	        FScopeMiscTime MiscTime;
			return RemoveDirectoryW(Path)!=0 || (!RequireExists && GetLastError()==ERROR_FILE_NOT_FOUND);
		}
        unguard;
    }
    TArray<FString> FindFiles( const TCHAR* Filename, UBOOL Files, UBOOL Directories )
    {
        guard(FFileManagerWindows::FindFiles);
		FScopeMiscTime MiscTime;
		TArray<FString> Result;
        WIN32_FIND_DATAW Data;
        HANDLE Handle=FindFirstFileW(Filename,&Data);
        if( Handle!=INVALID_HANDLE_VALUE )
            do
                if
                (   appStricmp(Data.cFileName,TEXT("."))
                &&  appStricmp(Data.cFileName,TEXT(".."))
                &&  ((Data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)?Directories:Files) )
                    new(Result)FString(Data.cFileName);
            while( FindNextFileW(Handle,&Data) );

        if( Handle!=INVALID_HANDLE_VALUE )
            FindClose( Handle );
        return Result;
        unguard;
    }
    UBOOL SetDefaultDirectory( const TCHAR* Filename )
    {
        guard(FFileManagerWindows::SetDefaultDirectory);
		FScopeMiscTime MiscTime;
		return SetCurrentDirectoryW(Filename)!=0;
        unguard;
    }
    FString GetDefaultDirectory()
    {
        guard(FFileManagerWindows::GetDefaultDirectory);
		FScopeMiscTime MiscTime;
        TCHAR Buffer[1024]=TEXT("");
        GetCurrentDirectoryW(ARRAY_COUNT(Buffer),Buffer);
        return FString(Buffer);

        unguard;
    }
	// gam ---
	INT CompareFileTimes( const TCHAR* FileA, const TCHAR* FileB )
	{
        guard(FFileManagerWindows::CompareFileTimes);
		FScopeMiscTime MiscTime;	
		struct _stat StatA, StatB;
		if( _wstat( FileA, &StatA ) != 0 )
		    return 0;
		if( _wstat( FileB, &StatB ) != 0 )
		    return 0;
        return( INT(StatB.st_mtime - StatA.st_mtime) );
        unguard;
	}
	// --- gam

	// rjp --
	UBOOL GetDriveLetters( FString& Drives ) const
	{
		guard(FFileManagerWindows::GetDriveLetters);

		Drives = TEXT("");
		DWORD OSDrives = GetLogicalDrives();

		TCHAR drive[3]; 
		drive[1] = ':'; drive[2] = 0;

		for ( INT i = 0; i < 27; i++ )
		{
			if ( OSDrives & (1 << i) )
			{
				drive[0] = 65 + i;

				if ( GetDriveTypeW(drive) == DRIVE_UNKNOWN ||
					 GetDriveTypeW(drive) == DRIVE_REMOVABLE ||
					 GetDriveTypeW(drive) == DRIVE_RAMDISK )
					 continue;

				if ( Drives != TEXT("") )
					Drives += TEXT(";");

				Drives += drive;
			}
		}

		return Drives.Len();
		unguard;
	}

	UBOOL IsCaseSensitive() const { return false; }
	// -- rjp
};

// A game folder under Program Files is read-only, so relative paths then also resolve under the player's
// local application data: their files shadow the installed ones and everything written goes there.
// A writable game folder keeps working as before, with the settings next to the game.
class FFileManagerWindowsHome : public FFileManagerWindows
{
public:
	const TCHAR* CalcHomeDir()
	{
		if( !Home[0] )
		{
			appStrcpy( Home, appBaseDir() );
			TCHAR Local[MAX_PATH - 64];
			DWORD Len = GetEnvironmentVariableW( TEXT("LOCALAPPDATA"), Local, ARRAY_COUNT(Local) );
			// Without a usable folder the game stays where it is and reports that it can't write.
			Overlaid = !ParseParam( appCmdLine(), TEXT("nohomedir") ) && !CanWrite( Home ) && Len>0 && Len<ARRAY_COUNT(Local);
			if( Overlaid )
			{
				appSprintf( Home, TEXT("%s\\UT2004\\System\\"), Local );
				FFileManagerGeneric::MakeDirectory( Home, 1 );
			}
		}
		return Home;
	}
	FArchive* CreateFileReader( const TCHAR* Filename, DWORD Flags, FOutputDevice* Error )
	{
		return FFileManagerWindows::CreateFileReader( *Existing(Filename), Flags, Error );
	}
	FArchive* CreateFileWriter( const TCHAR* Filename, DWORD Flags, FOutputDevice* Error )
	{
		return FFileManagerWindows::CreateFileWriter( *Written(Filename), Flags, Error );
	}
	INT FileSize( const TCHAR* Filename )
	{
		return FFileManagerWindows::FileSize( *Existing(Filename) );
	}
	DWORD Copy( const TCHAR* DestFile, const TCHAR* SrcFile, UBOOL ReplaceExisting, UBOOL EvenIfReadOnly, UBOOL Attributes, DWORD Compress, FCopyProgress* Progress )
	{
		return FFileManagerWindows::Copy( *Written(DestFile), *Existing(SrcFile), ReplaceExisting, EvenIfReadOnly, Attributes, Compress, Progress );
	}
	UBOOL Delete( const TCHAR* Filename, UBOOL RequireExists=0, UBOOL EvenReadOnly=0 )
	{
		return FFileManagerWindows::Delete( *InHome(Filename), RequireExists, EvenReadOnly );
	}
	UBOOL IsReadOnly( const TCHAR* Filename )
	{
		return FFileManagerWindows::IsReadOnly( *Existing(Filename) );
	}
	UBOOL Move( const TCHAR* Dest, const TCHAR* Src, UBOOL Replace=1, UBOOL EvenIfReadOnly=0, UBOOL Attributes=0 )
	{
		return FFileManagerWindows::Move( *Written(Dest), *Existing(Src), Replace, EvenIfReadOnly, Attributes );
	}
	UBOOL MakeDirectory( const TCHAR* Path, UBOOL Tree=0 )
	{
		return FFileManagerWindows::MakeDirectory( *Written(Path), Tree );
	}
	UBOOL DeleteDirectory( const TCHAR* Path, UBOOL RequireExists=0, UBOOL Tree=0 )
	{
		return FFileManagerWindows::DeleteDirectory( *InHome(Path), RequireExists, Tree );
	}
	TArray<FString> FindFiles( const TCHAR* Filename, UBOOL Files, UBOOL Directories )
	{
		FString Cache = InCache( Filename );
		if( Cache.Len() )
			return FFileManagerWindows::FindFiles( *Cache, Files, Directories );
		TArray<FString> Result = FFileManagerWindows::FindFiles( Filename, Files, Directories );
		if( IsOverlaid(Filename) )
		{
			TArray<FString> Own = FFileManagerWindows::FindFiles( *InHome(Filename), Files, Directories );
			for( INT i=0; i<Own.Num(); i++ )
			{
				INT j = 0;
				while( j<Result.Num() && appStricmp( *Result(j), *Own(i) )!=0 )
					j++;
				if( j==Result.Num() )
					new(Result)FString( Own(i) );
			}
		}
		return Result;
	}
	INT CompareFileTimes( const TCHAR* FileA, const TCHAR* FileB )
	{
		return FFileManagerWindows::CompareFileTimes( *Existing(FileA), *Existing(FileB) );
	}
	FString ExpandPath( const TCHAR* Path )
	{
		return FFileManagerWindows::ExpandPath( *Existing(Path) );
	}

private:
	TCHAR Home[MAX_PATH] = TEXT("");
	UBOOL Overlaid = 0;

	static UBOOL CanWrite( const TCHAR* Dir )
	{
		FString Probe = FString(Dir) + TEXT("write.probe");
		HANDLE Handle = CreateFileW( *Probe, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, NULL );
		if( Handle==INVALID_HANDLE_VALUE )
			return 0;
		CloseHandle( Handle );
		return 1;
	}
	UBOOL IsOverlaid( const TCHAR* Path )
	{
		CalcHomeDir();
		return Overlaid && Path[0] && Path[0]!='\\' && Path[0]!='/' && Path[1]!=':';
	}
	// The download cache shows as ..\Cache, where retail has it, and lives in the player's local application data.
	FString InCache( const TCHAR* Path )
	{
		const INT Len = ARRAY_COUNT(TEXT("..\\Cache")) - 1;
		TCHAR Local[MAX_PATH - 64];
		if( appStrnicmp( Path, TEXT("..\\Cache"), Len )!=0 && appStrnicmp( Path, TEXT("../Cache"), Len )!=0 )
			return FString();
		if( Path[Len] && Path[Len]!='\\' && Path[Len]!='/' )
			return FString();
		DWORD Got = GetEnvironmentVariableW( TEXT("LOCALAPPDATA"), Local, ARRAY_COUNT(Local) );
		if( Got==0 || Got>=ARRAY_COUNT(Local) )
			return FString();
		return FString(Local) + TEXT("\\UT2004\\Cache") + (Path+Len);
	}
	// Where the player's copy of a file is or would be.
	FString InHome( const TCHAR* Path )
	{
		FString Cache = InCache( Path );
		if( Cache.Len() )
			return Cache;
		if( !IsOverlaid(Path) )
			return Path;
		// Paths like ..\Save leave the System folder, and the directory functions need them without the dots.
		TCHAR Full[MAX_PATH] = TEXT("");
		DWORD Len = GetFullPathNameW( *(FString(Home) + Path), ARRAY_COUNT(Full), Full, NULL );
		// The engine's own path buffers hold 256 characters, a longer path fails as an unknown file does.
		return Len>0 && Len<256 ? FString(Full) : FString(Path);
	}
	FString Existing( const TCHAR* Path )
	{
		FString Own = InHome( Path );
		return GetFileAttributesW( *Own )!=INVALID_FILE_ATTRIBUTES ? Own : FString(Path);
	}
	FString Written( const TCHAR* Path )
	{
		FString Own = InHome( Path );
		if( IsOverlaid(Path) )
		{
			INT End = Own.Len();
			while( End>0 && Own[End-1]!='\\' )
				End--;
			FFileManagerGeneric::MakeDirectory( *Own.Left(End), 1 );
		}
		return Own;
	}
};

UBOOL GetFileLastModTime( const TCHAR* FakeFilename, DWORD &Hours, DWORD &Minutes, DWORD &Secs, DWORD &Day, DWORD &Month, DWORD &Year )
{

	WIN32_FILE_ATTRIBUTE_DATA fad;
	SYSTEMTIME systime;
	appMemzero(&fad,sizeof(fad));

	if (!GetFileAttributesEx(FakeFilename,GetFileExInfoStandard,&fad) )
		return 0;

	FileTimeToSystemTime(&fad.ftLastWriteTime,&systime);

    Hours = (DWORD) (systime.wHour);  // 0 - 23
    Minutes = (DWORD) (systime.wMinute);  // 0 - 59
    Secs = (DWORD) (systime.wSecond);  // 0 - 59, may be 60 in rare occasions.
    Day = (DWORD) (systime.wDay);  // 1 - 31
    Month = (DWORD) (systime.wMonth);   // 1 - 12
    Year = (DWORD) (systime.wYear); // actual year, not offset.

    if (Secs > 59) Secs = 59;  // Pesky leap seconds!

    return 1;
}

/*-----------------------------------------------------------------------------
    The End.
-----------------------------------------------------------------------------*/

