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
		// As the stock HandleError does. It stops the static classes destroyed at DLL detach from logging through the freed name table.
		GIsCriticalError = 1;
		std::_Exit( 1 );
	}
	void HandleError() {}
};
static FOutputDeviceTestError Error;

#include "FFeedbackContextAnsi.h"
static FFeedbackContextAnsi Warn;

#include "FFileManagerStd.h"
static FFileManagerStd FileManager;
#include "FMallocAnsi.h"
static FMallocAnsi Malloc;

#include "FConfigCacheIni.h"

int main( int argc, char* argv[] )
{
	testing::InitGoogleTest( &argc, argv );

	// From argv, which gtest has taken its own flags out of
	strcpy( GModule, "ut_tests" );
	TCHAR CmdLine[4096] = TEXT("");
	for( INT i=1; i<argc; i++ )
	{
		if( i>1 )
			appStrncat( CmdLine, TEXT(" "), ARRAY_COUNT(CmdLine) );
		appStrncat( CmdLine, ANSI_TO_TCHAR(argv[i]), ARRAY_COUNT(CmdLine) );
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
