#include "TestEngine.h"

#if _WIN32
	extern "C" {HINSTANCE hInstance;}
#endif
extern "C" {TCHAR GPackage[64]=TEXT("ut_tests");}

UGameEngine* GTestEngine;

#include "FOutputDeviceFile.h"
static FOutputDeviceFile Log;

// The stock error device shuts the engine down and throws, which would leave the remaining tests on a dead engine.
class FOutputDeviceTestError : public FOutputDeviceError
{
public:
	void Serialize( const TCHAR* Msg, enum EName Event )
	{
		printf( "appError: %s\n", TCHAR_TO_ANSI(Msg) );
		fflush( stdout );
		std::_Exit( 1 );
	}
	void HandleError() {}
};
static FOutputDeviceTestError Error;

#include "FFeedbackContextAnsi.h"
static FFeedbackContextAnsi Warn;

// gtest and the linked libraries allocate in their static constructors, and the engine's global operator new routes
// that through GMalloc. This file's globals initialize before theirs, EarlyMalloc sets GMalloc there.
#ifdef _MSC_VER
#pragma init_seg(lib)
#define TEST_INIT_FIRST
#else
#define TEST_INIT_FIRST __attribute__((init_priority(101)))
#endif
#if _WIN32
	#include "FFileManagerWindows.h"
	static FFileManagerWindowsHome FileManager;
#else
	#include "FFileManagerUnix.h"
	static FFileManagerUnix FileManager;
#endif
#include "FMallocAnsi.h"
static FMallocAnsi Malloc;
struct FTestEarlyMalloc { FTestEarlyMalloc() { GMalloc = &Malloc; } };
static FTestEarlyMalloc EarlyMalloc TEST_INIT_FIRST;

#include "FConfigCacheIni.h"

int main( int argc, char* argv[] )
{
	testing::InitGoogleTest( &argc, argv );

	TCHAR CmdLine[1024] = TEXT("");
	#if _WIN32
		// appInit drops the first token there.
		appStrcpy( CmdLine, TEXT("ut_tests") );
	#else
		strcpy( GModule, "ut_tests" );
	#endif
	for( INT i=1; i<argc; i++ )
	{
		appStrcat( CmdLine, TEXT(" ") );
		appStrcat( CmdLine, ANSI_TO_TCHAR(argv[i]) );
	}

	GIsStarted = 1;
	appInit( TEXT("UT2004"), CmdLine, &Malloc, &Log, &Error, &Warn, &FileManager, FConfigCacheIni::Factory, 1 );
#if __STATIC_LINK
	INT Lookup = 0;
	RegisterEngineNatives(Lookup);
	AUTO_INITIALIZE_REGISTRANTS_FIRE;
	RegisterGameNatives(Lookup);
#endif

	// What UCC sets up for its server commandlet.
	TCHAR Temp[256];
	if( GConfig->GetString( TEXT("Engine.Engine"), TEXT("Language"), Temp, ARRAY_COUNT(Temp) ) )
		UObject::SetLanguage( Temp );
	GIsServer = GIsScriptable = GIsUCC = GLazyLoad = 1;
	GIsClient = GIsEditor = 0;
	Warn.AuxOut = GLog;
	GLog = &Warn;

	UClass* EngineClass = UObject::StaticLoadClass( UEngine::StaticClass(), NULL, TEXT("ini:Engine.Engine.GameEngine"), NULL, LOAD_NoFail, NULL );
	GTestEngine = CastChecked<UGameEngine>( ConstructObject<UEngine>( EngineClass ) );
	GTestEngine->Init();
	// Init seeds from the clock.
	appRandInit( 0 );

	GIsRunning = 1;
	INT Result = RUN_ALL_TESTS();
	GIsRunning = 0;

	GLog = &Log;
	appPreExit();
	appExit();
	return Result;
}
