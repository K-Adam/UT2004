#ifndef _INCL_FFILEMANAGERSTD_H_
#define _INCL_FFILEMANAGERSTD_H_

// Relative paths also resolve under the player's own folder when the game folder is not theirs to write:
// their files shadow the installed ones and everything written goes there.
class CORE_API FFileManagerStd : public FFileManager
{
public:
	void Init( UBOOL Startup );
	FArchive* CreateFileReader( const TCHAR* Filename, DWORD Flags=0, FOutputDevice* Error=GNull );
	FArchive* CreateFileWriter( const TCHAR* Filename, DWORD Flags=0, FOutputDevice* Error=GNull );
	INT FileSize( const TCHAR* Filename );
	UBOOL Delete( const TCHAR* Filename, UBOOL RequireExists=0, UBOOL EvenReadOnly=0 );
	UBOOL IsReadOnly( const TCHAR* Filename );
	DWORD Copy( const TCHAR* Dest, const TCHAR* Src, UBOOL Replace=1, UBOOL EvenIfReadOnly=0, UBOOL Attributes=0, DWORD Compress=FILECOPY_Normal, FCopyProgress* Progress=NULL );
	UBOOL Move( const TCHAR* Dest, const TCHAR* Src, UBOOL Replace=1, UBOOL EvenIfReadOnly=0, UBOOL Attributes=0 );
	SQWORD GetGlobalTime( const TCHAR* Filename ) { return 0; }
	UBOOL SetGlobalTime( const TCHAR* Filename ) { return 0; }
	UBOOL MakeDirectory( const TCHAR* Path, UBOOL Tree=0 );
	UBOOL DeleteDirectory( const TCHAR* Path, UBOOL RequireExists=0, UBOOL Tree=0 );
	TArray<FString> FindFiles( const TCHAR* Filename, UBOOL Files, UBOOL Directories );
	UBOOL SetDefaultDirectory( const TCHAR* Filename );
	FString GetDefaultDirectory();
	FString ExpandPath( const TCHAR* Path );
	INT CompareFileTimes( const TCHAR* FileA, const TCHAR* FileB );
	const TCHAR* CalcHomeDir();
#ifdef _WIN32
	UBOOL GetDriveLetters( FString& Drives ) const;
	UBOOL IsCaseSensitive() const { return 0; }
#endif

private:
	FString Home;
	UBOOL Overlaid = 0;
	UBOOL Ready = 0;

	FString InHome( const TCHAR* Path );
	FString Existing( const TCHAR* Path );
	FString Written( const TCHAR* Path );
};

#endif
