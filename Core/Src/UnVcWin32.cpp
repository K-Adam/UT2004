/*=============================================================================
	UnVcWin32.cpp: Visual C++ Windows 32-bit core.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Tim Sweeney
=============================================================================*/
#if _WIN32

// Windows and runtime includes.
#pragma warning( disable : 4201 )

#pragma pack(push,8)
#include <windows.h>
#include <shellapi.h>
#include <objbase.h>
#include <mmsystem.h>
#pragma pack(pop)

#include <stdio.h>
#include <float.h>
#include <time.h>
#include <io.h>
#include <direct.h>
#include <errno.h>
#include <sys/stat.h>

// Core includes.
#include "CorePrivate.h"

#include <string.h>

/*-----------------------------------------------------------------------------
	Unicode helpers.
-----------------------------------------------------------------------------*/

#if UNICODE

// Windows versions ask Windows to convert UNICODE to ANSI using the current encoding.

CORE_API ANSICHAR* winToANSI( ANSICHAR* ACh, const TCHAR* InUCh, INT Count )
{
	guardSlow(winToANSI);
	WideCharToMultiByte( CP_ACP, 0, InUCh, -1, ACh, Count, NULL, NULL );
	return ACh;
	unguardSlow;
}
CORE_API ANSICHAR* winToOEM( ANSICHAR* ACh, const TCHAR* InUCh, INT Count )
{
	guardSlow(winToOEM);
	WideCharToMultiByte( CP_OEMCP, 0, InUCh, -1, ACh, Count, NULL, NULL );
	return ACh;
	unguardSlow;
}
CORE_API INT winGetSizeANSI( const TCHAR* InUCh )
{
	guardSlow(winGetSizeANSI);
	return WideCharToMultiByte( CP_ACP, 0, InUCh, -1, NULL, 0, NULL, NULL );
	unguardSlow;
}
CORE_API TCHAR* winToUNICODE( TCHAR* UCh, const ANSICHAR* InACh, INT Count )
{
	MultiByteToWideChar( CP_ACP, 0, InACh, -1, UCh, Count );
	return UCh;
}
CORE_API INT winGetSizeUNICODE( const ANSICHAR* InACh )
{
	return MultiByteToWideChar( CP_ACP, 0, InACh, -1, NULL, 0 );
}

CORE_API UNICHAR* winAnsiToTCHAR(char* str)
{
	INT iLength = winGetSizeUNICODE(str);
	UNICHAR* pBuffer = new TCHAR[iLength];
	appStrcpy(pBuffer,TEXT(""));
	return winToUNICODE(pBuffer,str,iLength);
}

#endif	// UNICODE

/*-----------------------------------------------------------------------------
	FOutputDeviceWindowsError.
-----------------------------------------------------------------------------*/

//
// Immediate exit.
//
CORE_API void appRequestExit( UBOOL Force )
{
	guard(appForceExit);
	debugf( TEXT("appRequestExit(%i)"), Force );
	if( Force )
	{
		// Force immediate exit. Dangerous because config code isn't flushed, etc.
		// The log is, or the error that led here is lost with it.
		if( GLog )
			GLog->Flush();
		ExitProcess( 1 );
	}
	else
	{
		// Tell the platform specific code we want to exit cleanly from the main loop.
		PostQuitMessage( 0 );
		GIsRequestingExit = 1;
	}
	unguard;
}

//
// Get system error.
//
CORE_API const TCHAR* appGetSystemErrorMessage( INT Error )
{
	guard(appGetSystemErrorMessage);
	static TCHAR Msg[1024];
	*Msg = 0;
	if( Error==0 )
		Error = GetLastError();
	FormatMessage( FORMAT_MESSAGE_FROM_SYSTEM, NULL, Error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), Msg, 1024, NULL );

	if( appStrchr(Msg,'\r') )
		*appStrchr(Msg,'\r')=0;
	if( appStrchr(Msg,'\n') )
		*appStrchr(Msg,'\n')=0;
	return Msg;
	unguard;
}

/*-----------------------------------------------------------------------------
	Commands.
-----------------------------------------------------------------------------*/

UBOOL appPlatformExec( const TCHAR* Cmd, FOutputDevice& Ar )
{
	if( ParseCommand(&Cmd,TEXT("MEMSTAT")) )
	{
		MEMORYSTATUS B; B.dwLength = sizeof(B);
		GlobalMemoryStatus(&B);
		Ar.Logf( TEXT("Memory available: Phys=%iK/%iK Pagef=%iK/%iK Virt=%iK/%iK"), B.dwAvailPhys/1024, B.dwTotalPhys/1024, B.dwAvailPageFile/1024, B.dwTotalPageFile/1024, B.dwAvailVirtual/1024, B.dwTotalVirtual/1024 );
		Ar.Logf( TEXT("Memory load = %i%%"), B.dwMemoryLoad );
		return 1;
	}
	else if( ParseCommand( &Cmd, TEXT("RELAUNCH") ) )
	{
		debugf( TEXT("Relaunch: %s"), Cmd );
		GConfig->Flush( 0 );
		UBOOL Params = 1;
		if ( *Cmd )
		{
            // Relative, so the file manager puts it where the server it starts reads it, also when the game folder is read-only.
            verify(appSaveStringToFile(FString(Cmd),TEXT("ServerCommand.txt"),GFileManager));
		}
		else Params = 0;

		TCHAR ThisFile[256];
		GetModuleFileName( NULL, ThisFile, ARRAY_COUNT(ThisFile) );
		debugf( TEXT("GetModuleFileName returned %s"), ThisFile );
		PTRINT Result = (PTRINT)ShellExecute( NULL, TEXT("open"), ThisFile, Params ? TEXT("COMMAND=ServerCommand.txt") : TEXT(""), appBaseDir(), SW_SHOWNORMAL );
		debugf(TEXT("ShellExecute result: %i"),(INT) Result);
		appRequestExit( 0 );
		return 1;
	}
	else return 0;
}

/*-----------------------------------------------------------------------------
	DLLs.
-----------------------------------------------------------------------------*/

//
// Load a library.
//warning: Intended only to be called by UPackage::BindPackage. Don't call directly.
//
static void* GetDllHandleHelper( const TCHAR* Filename )
{
#if __STATIC_LINK
	check(Filename);
	const TCHAR* PackageName = Filename;
	TCHAR* Cur;
	TCHAR* Test = appStaticString1024();

	// Get GLoadedPackage symbol name from full path.
 	while( (Cur = appStrchr( PackageName, '/' )) != NULL )
		PackageName = Cur + 1;
 	while( (Cur = appStrchr( PackageName, '\\' )) != NULL )
		PackageName = Cur + 1;
	appSprintf( Test, TEXT("GLoaded%s"), PackageName );
	if( (Cur = appStrchr( Test, '.' )) != NULL )
		*Cur = '\0';

    HMODULE exeHandle = GetModuleHandle(NULL);
	if (exeHandle)
    {
        void *rc = (GetProcAddress( exeHandle, TCHAR_TO_ANSI(Test) ));
        if (rc != NULL) return exeHandle;  // it's in the main binary...
    }
#endif

	return LoadLibraryW(Filename);
}

CORE_API void* appGetDllHandle( const TCHAR* Filename )
{
	guard(appGetDllHandle);
	check(Filename);

	void* Result = GetDllHandleHelper( Filename );
	if( !Result )
		Result = GetDllHandleHelper( *(FString(Filename) + DLLEXT) );
	if( !Result )
		Result = GetDllHandleHelper( *(US + TEXT("_") + Filename + DLLEXT) );

	return Result;

	unguard;
}

//
// Free a DLL.
//
CORE_API void appFreeDllHandle( void* DllHandle )
{
	guard(appFreeDllHandle);

#if __STATIC_LINK
    if (DllHandle == (void *) GetModuleHandle(NULL))
        return;
#endif

	check(DllHandle);

	FreeLibrary( (HMODULE)DllHandle );

	unguard;
}

//
// Lookup the address of a DLL function.
//
CORE_API void* appGetDllExport( void* DllHandle, const TCHAR* ProcName )
{
	guard(appGetDllExport);
	check(DllHandle);
	check(ProcName);

	return (void*)GetProcAddress( (HMODULE)DllHandle, TCHAR_TO_ANSI(ProcName) );

	unguard;
}

// Type - dicrates the type of dialog we're displaying
CORE_API const UBOOL appMsgf( INT Type, const TCHAR* Fmt, ... )
{
	guard(appMsgf);
	TCHAR TempStr[4096]=TEXT("");
	GET_VARARGS( TempStr, ARRAY_COUNT(TempStr), Fmt, Fmt );
	switch( Type )
	{
		case 1:
			return MessageBox( NULL, TempStr, TEXT("Message"), MB_YESNO ) == IDYES;
			break;
		case 2:
			return MessageBox( NULL, TempStr, TEXT("Message"), MB_OKCANCEL ) == IDOK;
			break;
		default:
			MessageBox( NULL, TempStr, TEXT("Message"), MB_OK );
			break;
	}
	return 1;
	unguard;
}

//
// Break the debugger.
//
void appDebugBreak()
{
	guard(appDebugBreak);
	::DebugBreak();
	unguard;
}

//
// appIsDebuggerPresent - are we running under a debugger?
//
UBOOL appIsDebuggerPresent()
{
	return IsDebuggerPresent();
}



/*-----------------------------------------------------------------------------
	Link functions.
-----------------------------------------------------------------------------*/

//
// Launch a uniform resource locator (i.e. http://www.epicgames.com/unreal).
// This is expected to return immediately as the URL is launched by another
// task.
//
void appLaunchURL( const TCHAR* URL, const TCHAR* Parms, FString* Error )
{
	guard(appLaunchURL);

	PTRINT Code = (PTRINT)ShellExecuteW(NULL,TEXT("open"),URL,Parms?Parms:TEXT(""),TEXT(""),SW_SHOWNORMAL);
	if( Error )
		*Error = Code<=32 ? LocalizeError(TEXT("UrlFailed"),TEXT("Core")) : TEXT("");
	unguard;
}

void *appCreateProc( const TCHAR* URL, const TCHAR* Parms )
{
	guard(appCreateProc);

	debugf( NAME_Log, TEXT("CreateProc %s %s"), URL, Parms );

	TCHAR CommandLine[1024];
	appSprintf( CommandLine, TEXT("%s %s"), URL, Parms );

	PROCESS_INFORMATION ProcInfo;
	SECURITY_ATTRIBUTES Attr;
	Attr.nLength = sizeof(SECURITY_ATTRIBUTES);
	Attr.lpSecurityDescriptor = NULL;
	Attr.bInheritHandle = TRUE;

	STARTUPINFO StartupInfo = { sizeof(STARTUPINFO), NULL, NULL, NULL,
		CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
		NULL, NULL, NULL, NULL, SW_HIDE, NULL, NULL,
		NULL, NULL, NULL };
	if( !CreateProcess( NULL, CommandLine, &Attr, &Attr, TRUE, DETACHED_PROCESS | REALTIME_PRIORITY_CLASS,
		NULL, NULL, &StartupInfo, &ProcInfo ) )
		return NULL;

	return (void*)ProcInfo.hProcess;

	unguard;
}

UBOOL appGetProcReturnCode( void* ProcHandle, INT* ReturnCode )
{
	guard(appGetProcReturnCode);
	return GetExitCodeProcess( (HANDLE)ProcHandle, (DWORD*)ReturnCode ) && *((DWORD*)ReturnCode) != STILL_ACTIVE;
	unguard;
}

/*-----------------------------------------------------------------------------
	Guids.
-----------------------------------------------------------------------------*/

//
// Create a new globally unique identifier.
//
CORE_API FGuid appCreateGuid()
{
	guard(appCreateGuid);

	FGuid Result(0,0,0,0);
	verify( CoCreateGuid( (GUID*)&Result )==S_OK ); // gam
	return Result;

	unguard;
}

/*-----------------------------------------------------------------------------
	Command line.
-----------------------------------------------------------------------------*/

// Get startup directory.
CORE_API const TCHAR* appBaseDir()
{
	guard(appBaseDir);
	static TCHAR Result[256]=TEXT("");
	if( !Result[0] )
	{
		// Get directory this executable was launched from.
		GetModuleFileName( hInstance, Result, ARRAY_COUNT(Result) );

		INT i;
		for( i=appStrlen(Result)-1; i>0; i-- )
			if( Result[i-1]==PATH_SEPARATOR[0] || Result[i-1]=='/' )
				break;
		Result[i]=0;
	}
	return Result;
	unguard;
}

// Get computer name.
CORE_API const TCHAR* appComputerName()
{
	guard(appComputerName);
	static TCHAR Result[256]=TEXT("");
	if( !Result[0] )
	{
		DWORD Size=ARRAY_COUNT(Result);
		GetComputerName( Result, &Size );

		TCHAR *c, *d;
		for( c=Result, d=Result; *c!=0; c++ )
			if( appIsAlnum(*c) )
				*d++ = *c;
		*d++ = 0;
	}
	return Result;
	unguard;
}

// Get user name.
CORE_API const TCHAR* appUserName()
{
	guard(appUserName);
	static TCHAR Result[256]=TEXT("");
	if( !Result[0] )
	{
		DWORD Size=ARRAY_COUNT(Result);
		GetUserName( Result, &Size );

		TCHAR *c, *d;
		for( c=Result, d=Result; *c!=0; c++ )
			if( appIsAlnum(*c) )
				*d++ = *c;
		*d++ = 0;
	}
	return Result;
	unguard;
}

CORE_API ANSICHAR GModule[32];

// The rest of the line as typed. Rebuilding it from argv would lose the quoting Parse reads, and argv is in the ANSI code page.
CORE_API void appArgsToCmdLine( INT argc, char** argv, INT First, TCHAR* Out, INT MaxLen )
{
	const TCHAR* Ch = GetCommandLineW();
	for( INT i=0; i<First; i++ )
	{
		while( appIsSpace(*Ch) )
			Ch++;
		for( UBOOL Quoted=0; *Ch && (Quoted || !appIsSpace(*Ch)); Ch++ )
			if( *Ch=='\"' )
				Quoted = !Quoted;
	}
	while( appIsSpace(*Ch) )
		Ch++;
	appStrncpy( Out, Ch, MaxLen );
}


// Get launch package base name.
CORE_API const TCHAR* appPackage()
{
	guard(appPackage);
	// Every launcher sets the module name
	static TCHAR Result[256];
	return appStrcpy( Result, ANSI_TO_TCHAR(GModule) );
	unguard;
}

/*-----------------------------------------------------------------------------
	App init/exit.
-----------------------------------------------------------------------------*/

//
// Platform specific initialization.
//
void appPlatformPreInit()
{
	guard(appPlatformPreInit);

	// This also has the undocumented side effect of affecting sleep granularity on current versions of Windows.
	timeBeginPeriod( 1 );

	// Every supported Windows is NT to the masters. Without a manifest GetVersionEx reports 6.2 on newer ones.
	OSVERSIONINFO Version;
	Version.dwOSVersionInfoSize = sizeof(Version);
	GetVersionEx( &Version );
	appSprintf( GMachineOS, TEXT("Windows NT %u.%u (Build: %u)"), Version.dwMajorVersion, Version.dwMinorVersion, Version.dwBuildNumber );
	debugf( NAME_Init, TEXT("Detected: Microsoft %s"), GMachineOS );
	GRunningOS = OS_WINNT;
	GUnicodeOS = 1;

	unguard;
}
BOOL WINAPI CtrlCHandler( DWORD dwCtrlType )
{
	if( !GIsRequestingExit )
	{
		PostQuitMessage( 0 );
		GIsRequestingExit = 1;
	}
	else
	{
		appErrorf( TEXT("Aborted!") );
	}
	return 1;
}
#ifndef _DEBUG
#pragma optimize( "g", off ) // Fix CPU brand string mangling in release mode
#endif
void appPlatformInit()
{
	guard(appPlatformInit);

	// System initialization.
	GSys = new USystem;
	GSys->AddToRoot();
	// The retail path, which server packages look for. The file manager keeps the files in the player's local
	// application data.
	GSys->CachePath = TEXT("..\\Cache");
	for( INT i=0; i<GSys->Suppress.Num(); i++ )
		GSys->Suppress(i).SetFlags( RF_Suppress );

	// Ctrl+C handling.
	SetConsoleCtrlHandler( CtrlCHandler, 1 );

	// Randomize.
    if( GUseFixedTimeStep )
        srand( 0 );
    else
		srand( (unsigned)time( NULL ) );

	// Identity.
	debugf( NAME_Init, TEXT("Computer: %s"), appComputerName() );
	debugf( NAME_Init, TEXT("User: %s"), appUserName() );
 
	// Get CPU info.
	guard(GetCpuInfo);
	SYSTEM_INFO SI;
	GetSystemInfo(&SI);
	GPageSize = SI.dwPageSize;
	check(!(GPageSize&(GPageSize-1)));
	GProcessorCount=SI.dwNumberOfProcessors;
	debugf( NAME_Init, TEXT("CPU Page size=%i, Processors=%i"), SI.dwPageSize, SI.dwNumberOfProcessors );
    appSprintf( GMachineCPU, TEXT("%i processors"), SI.dwNumberOfProcessors );
	unguard;

	// Get memory.
	guard(GetMemory);
	MEMORYSTATUS M;
	GlobalMemoryStatus(&M);
	GPhysicalMemory=M.dwTotalPhys;
	debugf( NAME_Init, TEXT("Memory total: Phys=%iK Pagef=%iK Virt=%iK"), M.dwTotalPhys/1024, M.dwTotalPageFile/1024, M.dwTotalVirtual/1024 );
    appStrcat( GMachineCPU, *FString::Printf(TEXT(" with %dMB RAM"), M.dwTotalPhys/(1024 * 1024) ) ); // gam
	unguard;

	// Working set.
	guard(GetWorkingSet);
	SIZE_T WsMin=0, WsMax=0;
	GetProcessWorkingSetSize( GetCurrentProcess(), &WsMin, &WsMax );
	debugf( NAME_Init, TEXT("Working set: %X / %X"), WsMin, WsMax );
	unguard;

	unguard;
}
#ifndef _DEBUG
#pragma optimize("", on)
#endif

void appPlatformPreExit()
{
	guard(appPlatformPreExit);
	unguard;
}
void appPlatformExit()
{
	guard(appPlatformExit);
	timeEndPeriod( 1 );
	unguard;
}

#endif
/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/

