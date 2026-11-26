// The standard headers go before the engine's, whose macros (check, clock, min, max) break them
#pragma pack(push,8)
#include <filesystem>
#include <string>
#include <system_error>
#ifdef _WIN32
#include <windows.h>
#endif
#pragma pack(pop)

#include <errno.h>
#include <stdio.h>
#include <zlib.h>
#ifdef _WIN32
#include <io.h>
#include <share.h>
#else
#include <pwd.h>
#include <unistd.h>
#include <sys/stat.h>
#endif

#include "CorePrivate.h"
#include "FFileManagerStd.h"
#include "FFileManagerArc.h"

namespace fs = std::filesystem;

#if __EMSCRIPTEN__
// Emscripten/content.js reads the content files from IndexedDB, suspending the engine while it waits.
extern "C" int ut_content_read( int File, void* Data, int Count );
#endif

static fs::path ToPath( const TCHAR* Path )
{
#ifdef _WIN32
	return fs::path( Path ).lexically_normal();
#else
	// appUnixPath turns backslashes and finds the name on disk in another case
	return fs::u8path( appUnixPath( Path ) ).lexically_normal();
#endif
}

static FString FromPath( const fs::path& Path )
{
#ifdef _WIN32
	return FString( Path.c_str() );
#else
	return appFromUtf8( Path.c_str() );
#endif
}

static UBOOL IsRelative( const TCHAR* Path )
{
	return Path[0] && Path[0]!='\\' && Path[0]!='/' && Path[1]!=':';
}

// Writers block other readers unless FILEWRITE_AllowRead is set. Readers share everything, so a log being written can be copied.
static FILE* OpenNative( const fs::path& Path, const char* Mode, UBOOL Exclusive )
{
#ifdef _WIN32
	return _wfsopen( Path.c_str(), appFromAnsi(Mode), Exclusive ? _SH_DENYRW : Mode[0]=='r' ? _SH_DENYNO : _SH_DENYWR );
#else
	return fopen( Path.c_str(), Mode );
#endif
}

static UBOOL CanWrite( const fs::path& Dir )
{
	fs::path Probe = Dir / "write.probe";
	FILE* File = OpenNative( Probe, "wb", 1 );
	if( !File )
		return 0;
	fclose( File );
	std::error_code Ec;
	fs::remove( Probe, Ec );
	return 1;
}

#ifdef _WIN32
static FString LocalAppData()
{
	const TCHAR* Local = _wgetenv( TEXT("LOCALAPPDATA") );
	return Local && appStrlen(Local)<=900 ? FString(Local) : FString();
}
#endif

// Windows keeps the player's folder in their local application data and the game folder when that can be written.
// Unix keeps it in ~/.ut2004 unless home can't be written.
static FString PlayerDir()
{
#ifdef _WIN32
	if( CanWrite( ToPath( appBaseDir() ) ) )
		return FString();
	FString Local = LocalAppData();
	return Local.Len() ? Local + TEXT("\\UT2004\\System\\") : FString();
#else
	const char* Env = getenv( "HOME" );
	if( !Env || Env[0]!='/' )
	{
		struct passwd* Pwd = getpwuid( getuid() );
		Env = Pwd ? Pwd->pw_dir : NULL;
	}
	if( !Env || Env[0]!='/' || strlen(Env)>900 )
		return FString();
	FString Dir = appFromUtf8( Env );
	// A path that is not UTF-8 decodes to "?"
	if( Dir[0]!='/' )
		return FString();
	mkdir( (std::string(Env) + "/.ut2004").c_str(), S_IRWXU );
	mkdir( (std::string(Env) + "/.ut2004/System").c_str(), S_IRWXU );
	return Dir + TEXT("/.ut2004/System/");
#endif
}

// The Windows download cache shows as ..\Cache, where retail has it, and lives in the player's local application data.
static FString InCache( const TCHAR* Path )
{
#ifdef _WIN32
	const INT Len = 8;
	if( appStrnicmp( Path, TEXT("..\\Cache"), Len )!=0 && appStrnicmp( Path, TEXT("../Cache"), Len )!=0 )
		return FString();
	if( Path[Len] && Path[Len]!='\\' && Path[Len]!='/' )
		return FString();
	FString Local = LocalAppData();
	return Local.Len() ? Local + TEXT("\\UT2004\\Cache") + (Path+Len) : FString();
#else
	return FString();
#endif
}

namespace
{

// Reads the descriptor itself, stdio would add system calls to every seek.
class FArchiveFileReader : public FArchive
{
public:
	FArchiveFileReader( FILE* InFile, FOutputDevice* InError, INT InSize )
	:	File		( InFile )
	,	Error		( InError )
	,	Size		( InSize )
	,	Pos			( 0 )
	,	BufferBase	( 0 )
	,	BufferCount	( 0 )
	{
		ArIsLoading = ArIsPersistent = 1;
	}
	~FArchiveFileReader()
	{
		Close();
	}
	void Precache( INT HintCount )
	{
		checkSlow(Pos==BufferBase+BufferCount);
		BufferBase = Pos;
		BufferCount = Min( Min( Max( HintCount, 0 ), (INT)(ARRAY_COUNT(Buffer) - (Pos&(ARRAY_COUNT(Buffer)-1))) ), Size-Pos );
		if( !Read( Buffer, BufferCount ) )
			Error->Logf( TEXT("read failed: BufferCount=%i errno=%i"), BufferCount, (INT)errno );
	}
	void Seek( INT InPos )
	{
		guard(FArchiveFileReader::Seek);
		check(InPos>=0);
		check(InPos<=Size);
		DOUBLE StartTime = appSeconds();
		if( lseek( fileno(File), InPos, SEEK_SET )<0 )
		{
			ArIsError = 1;
			Error->Logf( TEXT("seek failed %i/%i: %i %i"), InPos, Size, Pos, (INT)errno );
		}
		FLOAT DeltaTime = appSeconds() - StartTime;
		if( DeltaTime < 100.f )
			GFileManagerSeekTime += DeltaTime;
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
		if( File )
			fclose( File );
		File = NULL;
		return !ArIsError;
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
					if( !Read( V, Length ) )
						Error->Logf( TEXT("read failed: Length=%i errno=%i"), Length, (INT)errno );
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
private:
	FILE*			File;
	FOutputDevice*	Error;
	INT				Size;
	INT				Pos;
	INT				BufferBase;
	INT				BufferCount;
	BYTE			Buffer[1024];

	UBOOL Read( void* Data, INT Count )
	{
		DOUBLE StartTime = appSeconds();
#if __EMSCRIPTEN__
		INT Got = ut_content_read( fileno(File), Data, Count );
#else
		INT Got = (INT)read( fileno(File), Data, Count );
#endif
		FLOAT DeltaTime = appSeconds() - StartTime;
		if( DeltaTime < 100.f )
			GFileManagerLoadingTime += DeltaTime;
		if( Got>0 )
			GFileManagerBytesRead += Got;
		if( Got!=Count )
			ArIsError = 1;
		return Got==Count;
	}
};

// Buffers itself on an unbuffered file, so Flush reaches the system.
class FArchiveFileWriter : public FArchive
{
public:
	FArchiveFileWriter( FILE* InFile, FOutputDevice* InError, INT InPos )
	:	File		( InFile )
	,	Error		( InError )
	,	Pos			( InPos )
	,	BufferCount	( 0 )
	{
		ArIsSaving = ArIsPersistent = 1;
	}
	~FArchiveFileWriter()
	{
		Close();
	}
	void Seek( INT InPos )
	{
		Flush();
		if( fseek( File, InPos, SEEK_SET )!=0 )
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
		if( File )
		{
			Flush();
			if( fclose( File )!=0 )
			{
				ArIsError = 1;
				Error->Logf( LocalizeError("WriteFailed",TEXT("Core")) );
			}
		}
		File = NULL;
		return !ArIsError;
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
		if( BufferCount && fwrite( Buffer, 1, BufferCount, File )!=(size_t)BufferCount )
		{
			ArIsError = 1;
			Error->Logf( LocalizeError("WriteFailed",TEXT("Core")) );
		}
		BufferCount = 0;
	}
private:
	FILE*			File;
	FOutputDevice*	Error;
	INT				Pos;
	INT				BufferCount;
	BYTE			Buffer[4096];
};

}

void FFileManagerStd::Init( UBOOL Startup )
{
	// Runs again once the command line is complete
	Ready = 1;
#ifdef _WIN32
	// Every loaded package keeps its file open, more than the default 512 streams
	_setmaxstdio( 8192 );
#endif
	Overlaid = 0;
	FString Dir = ParseParam( appCmdLine(), TEXT("nohomedir") ) ? FString() : PlayerDir();
	if( !Dir.Len() )
		return;
	fs::path Path = ToPath( *Dir );
	std::error_code Ec;
	fs::create_directories( Path, Ec );
	if( !CanWrite( Path ) )
		return;
	Home = Dir;
	Overlaid = 1;
}

// The game folder is returned as the appBaseDir pointer, which callers compare.
const TCHAR* FFileManagerStd::CalcHomeDir()
{
	if( !Ready )
		Init( 0 );
	return Overlaid ? *Home : appBaseDir();
}

// Where the player's copy of a file is or would be.
FString FFileManagerStd::InHome( const TCHAR* Path )
{
	if( !Ready )
		Init( 0 );
	FString Cache = InCache( Path );
	if( Cache.Len() )
		return Cache;
	if( !Overlaid || !IsRelative(Path) )
		return Path;
	// Paths like ..\Save leave the System folder
	FString Own = FromPath( ToPath( *(Home + Path) ) );
	// The engine's own path buffers hold 256 characters, a longer path fails as an unknown file does
	return Own.Len()<256 ? Own : FString(Path);
}

FString FFileManagerStd::Existing( const TCHAR* Path )
{
	FString Own = InHome( Path );
	// Only the player's folder holds the cache, as in FindFiles. A copy found in the game folder would get an empty twin from a writer.
	if( Own==Path || InCache( Path ).Len() )
		return Own;
	std::error_code Ec;
	return fs::exists( ToPath(*Own), Ec ) ? Own : FString(Path);
}

FString FFileManagerStd::Written( const TCHAR* Path )
{
	FString Own = InHome( Path );
	std::error_code Ec;
	fs::create_directories( ToPath(*Own).parent_path(), Ec );
	return Own;
}

FArchive* FFileManagerStd::CreateFileReader( const TCHAR* Filename, DWORD Flags, FOutputDevice* Error )
{
	guard(FFileManagerStd::CreateFileReader);
	fs::path Path = ToPath( *Existing(Filename) );
	std::error_code Ec;
	// Fails for a directory too
	uintmax_t Size = fs::file_size( Path, Ec );
	FILE* File = Ec ? NULL : OpenNative( Path, "rb", 0 );
	if( !File )
	{
		if( Flags & FILEREAD_NoFail )
			appErrorf( TEXT("Failed to read file: %s"), Filename );
		return NULL;
	}
	return new(TEXT("FileReader"))FArchiveFileReader( File, Error, (INT)Size );
	unguard;
}

FArchive* FFileManagerStd::CreateFileWriter( const TCHAR* Filename, DWORD Flags, FOutputDevice* Error )
{
	guard(FFileManagerStd::CreateFileWriter);
	fs::path Path = ToPath( *Written(Filename) );
	std::error_code Ec;
	if( Flags & FILEWRITE_EvenIfReadOnly )
		fs::permissions( Path, fs::perms::owner_write, fs::perm_options::add, Ec );
	const char* Mode = (Flags & FILEWRITE_Append) ? "ab" : (Flags & FILEWRITE_NoReplaceExisting) ? "wbx" : "wb";
	FILE* File = OpenNative( Path, Mode, !(Flags & FILEWRITE_AllowRead) );
	if( !File )
	{
		if( Flags & FILEWRITE_NoFail )
			appErrorf( TEXT("Failed to write: %s"), Filename );
		return NULL;
	}
	setvbuf( File, NULL, _IONBF, 0 );
	INT Pos = (Flags & FILEWRITE_Append) ? (INT)fs::file_size( Path, Ec ) : 0;
	return new(TEXT("FileWriter"))FArchiveFileWriter( File, Error, Ec ? 0 : Pos );
	unguard;
}

INT FFileManagerStd::FileSize( const TCHAR* Filename )
{
	std::error_code Ec;
	uintmax_t Size = fs::file_size( ToPath(*Existing(Filename)), Ec );
	return Ec ? -1 : (INT)Size;
}

UBOOL FFileManagerStd::Delete( const TCHAR* Filename, UBOOL RequireExists, UBOOL EvenReadOnly )
{
	guard(FFileManagerStd::Delete);
	fs::path Path = ToPath( *InHome(Filename) );
	std::error_code Ec;
	if( EvenReadOnly )
		fs::permissions( Path, fs::perms::owner_write, fs::perm_options::add, Ec );
#ifdef _WIN32
	if( _wunlink( Path.c_str() )==0 )
#else
	if( unlink( Path.c_str() )==0 )
#endif
		return 1;
	if( errno==ENOENT && !RequireExists )
		return 1;
	debugf( NAME_Warning, TEXT("Error deleting file '%s' (%i)"), Filename, (INT)errno );
	return 0;
	unguard;
}

UBOOL FFileManagerStd::IsReadOnly( const TCHAR* Filename )
{
	fs::path Path = ToPath( *Existing(Filename) );
	std::error_code Ec;
	if( !fs::is_regular_file( Path, Ec ) )
		return 0;
#ifdef _WIN32
	return _waccess( Path.c_str(), 2 )!=0;
#else
	return access( Path.c_str(), W_OK )!=0;
#endif
}

DWORD FFileManagerStd::Copy( const TCHAR* InDestFile, const TCHAR* InSrcFile, UBOOL ReplaceExisting, UBOOL EvenIfReadOnly, UBOOL Attributes, DWORD Compress, FCopyProgress* Progress )
{
	guard(FFileManagerStd::Copy);
	const INT BlockSize = 32768;
	const INT MaxCompSize = 33096;
	if( Progress && !Progress->Poll( 0.0 ) )
		return COPY_Canceled;
	DWORD Result = COPY_OK;
	FString SrcFile = InSrcFile;
	FString DestFile = InDestFile;
	if( Compress==FILECOPY_Compress )
		DestFile = DestFile + COMPRESSED_EXTENSION;
	else if( Compress==FILECOPY_Decompress )
		SrcFile = SrcFile + COMPRESSED_EXTENSION;

	FArchive* Src = CreateFileReader( *SrcFile );
	if( !Src )
		return COPY_ReadFail;
	INT Size = Src->TotalSize();
	FArchive* Dest = CreateFileWriter( *DestFile, (ReplaceExisting?0:FILEWRITE_NoReplaceExisting) | (EvenIfReadOnly?FILEWRITE_EvenIfReadOnly:0) );
	if( !Dest )
	{
		delete Src;
		return COPY_WriteFail;
	}
	BYTE UncBuffer[BlockSize];
	BYTE ComBuffer[MaxCompSize];
	INT Percent = 0;
	auto Cancelled = [&]( INT Done )
	{
		INT NewPercent = Done * 100 / Size;
		UBOOL Stop = Progress && Percent!=NewPercent && !Progress->Poll( (FLOAT)NewPercent / 100.f );
		Percent = NewPercent;
		return Stop;
	};
	switch( Compress )
	{
	case FILECOPY_Normal:
		for( INT Total=0; Total<Size; Total+=BlockSize )
		{
			INT Count = Min( Size-Total, BlockSize );
			Src->Serialize( UncBuffer, Count );
			if( Src->IsError() )
			{
				Result = COPY_ReadFail;
				break;
			}
			Dest->Serialize( UncBuffer, Count );
			if( Dest->IsError() )
			{
				Result = COPY_WriteFail;
				break;
			}
			if( Cancelled( Total ) )
			{
				Result = COPY_Canceled;
				break;
			}
		}
		break;
	case FILECOPY_Decompress:
		while( !Src->AtEnd() )
		{
			DWORD ComSize, UncSize;
			Src->Serialize( &ComSize, sizeof(ComSize) );
			Src->Serialize( &UncSize, sizeof(UncSize) );
			ComSize = INTEL_ORDER32(ComSize);
			UncSize = INTEL_ORDER32(UncSize);
			if( ComSize > (DWORD)MaxCompSize || UncSize > (DWORD)BlockSize )
			{
				Result = COPY_DecompFail;
				break;
			}
			Src->Serialize( ComBuffer, ComSize );
			if( Src->IsError() )
			{
				Result = COPY_ReadFail;
				break;
			}
			uLongf ZlibUncSize = UncSize;
			if( uncompress( UncBuffer, &ZlibUncSize, ComBuffer, ComSize ) != Z_OK )
			{
				Result = COPY_DecompFail;
				break;
			}
			Dest->Serialize( UncBuffer, (INT)ZlibUncSize );
			if( Dest->IsError() )
			{
				Result = COPY_WriteFail;
				break;
			}
			if( Cancelled( Src->Tell() ) )
			{
				Result = COPY_Canceled;
				break;
			}
		}
		break;
	case FILECOPY_Compress:
		for( INT Total=0; Total<Size; Total+=BlockSize )
		{
			DWORD UncSize = Min( Size-Total, BlockSize );
			Src->Serialize( UncBuffer, UncSize );
			if( Src->IsError() )
			{
				Result = COPY_ReadFail;
				break;
			}
			uLongf ZlibComSize = MaxCompSize;
			if( compress( ComBuffer, &ZlibComSize, UncBuffer, UncSize ) != Z_OK )
			{
				Result = COPY_CompFail;
				break;
			}
			DWORD ComSize = (DWORD)ZlibComSize;
			DWORD OutComSize = INTEL_ORDER32(ComSize);
			DWORD OutUncSize = INTEL_ORDER32(UncSize);
			Dest->Serialize( &OutComSize, sizeof(OutComSize) );
			Dest->Serialize( &OutUncSize, sizeof(OutUncSize) );
			Dest->Serialize( ComBuffer, ComSize );
			if( Dest->IsError() )
			{
				Result = COPY_WriteFail;
				break;
			}
			if( Cancelled( Total ) )
			{
				Result = COPY_Canceled;
				break;
			}
		}
		break;
	}
	if( Result==COPY_OK && !Dest->Close() )
		Result = COPY_WriteFail;
	delete Dest;
	if( Result!=COPY_OK )
		Delete( *DestFile );
	if( Result==COPY_OK && !Src->Close() )
		Result = COPY_ReadFail;
	delete Src;
	if( Progress && Result==COPY_OK && !Progress->Poll( 1.0 ) )
		Result = COPY_Canceled;
	return Result;
	unguard;
}

UBOOL FFileManagerStd::Move( const TCHAR* Dest, const TCHAR* Src, UBOOL Replace, UBOOL EvenIfReadOnly, UBOOL Attributes )
{
	guard(FFileManagerStd::Move);
	fs::path To = ToPath( *Written(Dest) );
	fs::path From = ToPath( *Existing(Src) );
	std::error_code Ec;
	if( fs::exists( To, Ec ) )
	{
		if( !Replace )
			return 0;
		if( EvenIfReadOnly )
			fs::permissions( To, fs::perms::owner_write, fs::perm_options::add, Ec );
	}
	fs::rename( From, To, Ec );
	if( !Ec )
		return 1;
	// Across drives or file systems
	if( Copy( Dest, Src, Replace, EvenIfReadOnly, Attributes, FILECOPY_Normal, NULL )!=COPY_OK )
	{
		debugf( NAME_Warning, TEXT("Error moving file '%s' to '%s'"), Src, Dest );
		return 0;
	}
	fs::remove( From, Ec );
	return 1;
	unguard;
}

INT FFileManagerStd::CompareFileTimes( const TCHAR* FileA, const TCHAR* FileB )
{
	std::error_code EcA, EcB;
	fs::file_time_type A = fs::last_write_time( ToPath(*Existing(FileA)), EcA );
	fs::file_time_type B = fs::last_write_time( ToPath(*Existing(FileB)), EcB );
	if( EcA || EcB )
		return 0;
	return (INT)std::chrono::duration_cast<std::chrono::seconds>( B - A ).count();
}

FString FFileManagerStd::ExpandPath( const TCHAR* Path )
{
	return FFileManager::ExpandPath( *Existing(Path) );
}

UBOOL FFileManagerStd::MakeDirectory( const TCHAR* Path, UBOOL Tree )
{
	fs::path Dir = ToPath( *InHome(Path) );
	std::error_code Ec;
	fs::create_directories( Dir, Ec );
	return fs::is_directory( Dir, Ec );
}

UBOOL FFileManagerStd::DeleteDirectory( const TCHAR* Path, UBOOL RequireExists, UBOOL Tree )
{
	guard(FFileManagerStd::DeleteDirectory);
	if( !Path[0] )
		return 0;
	fs::path Dir = ToPath( *InHome(Path) );
	if( Tree )
	{
		// Removes links without following them
		std::error_code Ec;
		uintmax_t Count = fs::remove_all( Dir, Ec );
		return !Ec && (Count>0 || !RequireExists);
	}
#ifdef _WIN32
	if( _wrmdir( Dir.c_str() )==0 )
#else
	if( rmdir( Dir.c_str() )==0 )
#endif
		return 1;
	return errno==ENOENT && !RequireExists;
	unguard;
}

// Matches like Windows: case-insensitive and "*.*" takes names without a dot too.
static void SearchDirectory( TArray<FString>& Result, const TCHAR* Filename, UBOOL Files, UBOOL Directories )
{
	fs::path Spec = ToPath( Filename );
	FString Pattern = FromPath( Spec.filename() );
	if( Pattern==TEXT("*.*") )
		Pattern = TEXT("*");
	fs::path Dir = Spec.parent_path();
	if( Dir.empty() )
		Dir = ".";
	std::error_code Ec;
	for( fs::directory_iterator It( Dir, Ec ), End; !Ec && It!=End; It.increment( Ec ) )
	{
		FString Name = FromPath( It->path().filename() );
		// A name an older build wrote in Latin-1 does not decode
		if( Name==TEXT("?") && It->path().filename()!=fs::path("?") )
			continue;
		if( !WildcardMatch( *Pattern, *Name ) || Result.FindItemIndex( Name )!=INDEX_NONE )
			continue;
		// Type check after the name match, a stat per entry takes seconds on a network drive
		std::error_code TypeEc;
		UBOOL IsDirectory = It->is_directory( TypeEc );
		if( !TypeEc && (IsDirectory ? Directories : Files) )
			new(Result)FString( Name );
	}
}

// The game folder and the player's can hold the same name, it is listed once.
TArray<FString> FFileManagerStd::FindFiles( const TCHAR* Filename, UBOOL Files, UBOOL Directories )
{
	guard(FFileManagerStd::FindFiles);
	TArray<FString> Result;
	FString Own = InHome( Filename );
	if( !InCache( Filename ).Len() )
		SearchDirectory( Result, Filename, Files, Directories );
	if( Own!=Filename )
		SearchDirectory( Result, *Own, Files, Directories );
	return Result;
	unguard;
}

UBOOL FFileManagerStd::SetDefaultDirectory( const TCHAR* Filename )
{
	std::error_code Ec;
	fs::current_path( ToPath(Filename), Ec );
	return !Ec;
}

FString FFileManagerStd::GetDefaultDirectory()
{
	std::error_code Ec;
	return FromPath( fs::current_path( Ec ) );
}

#ifdef _WIN32
UBOOL FFileManagerStd::GetDriveLetters( FString& Drives ) const
{
	Drives = TEXT("");
	DWORD OSDrives = GetLogicalDrives();
	TCHAR Drive[3] = { 0, ':', 0 };
	for( INT i=0; i<26; i++ )
	{
		if( !(OSDrives & (1 << i)) )
			continue;
		Drive[0] = 'A' + i;
		UINT Type = GetDriveTypeW( Drive );
		if( Type==DRIVE_UNKNOWN || Type==DRIVE_REMOVABLE || Type==DRIVE_RAMDISK )
			continue;
		if( Drives.Len() )
			Drives += TEXT(";");
		Drives += Drive;
	}
	return Drives.Len();
}
#endif
