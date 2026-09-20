/*=============================================================================
	SDLLaunch.cpp: Game launcher.
	Copyright 2002 Epic Games, Inc. All Rights Reserved.

Revision history:
	* Created by Ryan C. Gordon, based on ut2003's win32 Launch.cpp.
=============================================================================*/

#include <time.h>  // dodging a macro clash with Unix C runtime headers...

#include "SDLLaunchPrivate.h"

#if __STATIC_LINK
#include "Engine.h"

#include "UnRender.h"
#include "UnNullRenderDevice.h"
#include "SDLDrv.h"
#include "OpenGLDrv.h"
#include "UnNet.h"
#include "UnFractal.h"
#include "ALAudio.h"
#include "UnIpDrv.h"
#include "UnTcpNetDriver.h"
#include "UnIpDrvCommandlets.h"
#include "UnIpDrvNative.h"
#include "XInterface.h"
#include "UWeb.h"
#include "UWebNative.h"
#include "OnslaughtPrivate.h"
void autoInitializeRegistrantsEditor(INT &Lookup);
extern "C" BYTE GLoadedSDLDrv, GLoadedOpenGLDrv, GLoadedALAudio;

#endif


/*-----------------------------------------------------------------------------
	Global variables.
-----------------------------------------------------------------------------*/

// General.
#ifdef WIN32
extern "C" {HINSTANCE hInstance;}
#endif
extern "C" {TCHAR GPackage[64]=TEXT("SDLLaunch");}

// Memory allocator.
#ifdef _DEBUG
	#include "FMallocDebug.h"
	FMallocDebug Malloc;
#else
	#include "FMallocAnsi.h"
	FMallocAnsi Malloc;
#endif

// Log file.
#include "FOutputDeviceFile.h"
FOutputDeviceFile Log;

// Error handler.
#include "FOutputDeviceAnsiError.h"
FOutputDeviceAnsiError Error;

// Feedback.
#include "FFeedbackContextAnsi.h"
FFeedbackContextAnsi Warn;

// File manager.
#ifdef WIN32
#include "FFileManagerWindows.h"
FFileManagerWindows FileManager;
#else
#include "FFileManagerUnix.h"
FFileManagerUnix FileManager;
#endif

// Config.
#include "FConfigCacheIni.h"

/*-----------------------------------------------------------------------------
	mainline
-----------------------------------------------------------------------------*/

// Not really SDL specific apart from the splash screen.
#define USE_SDL_SPLASH

#if (!defined USE_SDL_SPLASH)

static void InitSplash(const TCHAR *filename) {}
static void ExitSplash(void) {}

#else

// SDL3.dll uses default packing, the engine builds with /Zp4.
#pragma pack(push,8)
#include <SDL3/SDL.h>
#pragma pack(pop)

static SDL_Window* SplashWindow = NULL;

static void InitSplash(const TCHAR *filename)
{
    if (filename == NULL)
        return;

    // A GL backed surface would make its context current and take that away from SDLDrv's.
    SDL_SetHint(SDL_HINT_FRAMEBUFFER_ACCELERATION, "0");
    if (!SDL_Init(SDL_INIT_VIDEO))
        return;

    SDL_Surface *bmp = SDL_LoadBMP(appToAnsi(filename));
    if (bmp == NULL)
        return;

    SplashWindow = SDL_CreateWindow("", bmp->w, bmp->h, SDL_WINDOW_BORDERLESS);
    SDL_Surface *screen = SplashWindow ? SDL_GetWindowSurface(SplashWindow) : NULL;
    if (screen != NULL)
    {
        SDL_BlitSurface(bmp, NULL, screen, NULL);
        SDL_UpdateWindowSurface(SplashWindow);
    }

    SDL_DestroySurface(bmp);
}


static void ExitSplash(void)
{
    if (SplashWindow == NULL)
        return;
    SDL_DestroyWindow(SplashWindow);
    SplashWindow = NULL;
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}
#endif


//
// Unreal's main message loop.  All windows in Unreal receive messages
// somewhere below this function on the stack.
//

class CMainLoop
{
public:
	inline CMainLoop( UEngine* InEngine, UBOOL InIncludeMessaging = true );
	inline ~CMainLoop(void) noexcept(false);

	inline void	RunLoop(void);
	inline bool	Finished(void) const	{return !GIsRunning || GIsRequestingExit;}

private:
	UEngine *		Engine;
	UBOOL			IncludeMessaging;

	DOUBLE			OldTime;
	DOUBLE			SecondStartTime;
	INT				TickCount;
	DWORD			LastFrameCycles;

	// Benchmarking.
	INT				BMFrames;
	INT				BMDiscardedFrames;
	INT				BMMaxFrames;
	FLOAT			BMSeconds;
	TArray<FString> BMStrings;
	TArray<FLOAT>	BMFrameTimes;

	// Movie recording.
	UBOOL			RecordingMovie;

};

inline CMainLoop::CMainLoop( UEngine* InEngine, UBOOL InIncludeMessaging )
:	Engine(InEngine), IncludeMessaging(InIncludeMessaging)
{
	guard(MainLoopCtor);
	check(Engine);

	// Loop while running.
	GIsRunning			= 1;
	OldTime				= appSeconds();
	SecondStartTime		= OldTime;
	TickCount			= 0;

	// Benchmarking.
	BMFrames			= 0;
	BMDiscardedFrames	= 10;
	BMSeconds			= 0.0f;
	
	BMMaxFrames			= 0; // gam
	Parse(appCmdLine(),TEXT("SECONDS="),BMMaxFrames);
	BMMaxFrames			*= 30;
	
	for( INT i=0; i<(BMMaxFrames-BMDiscardedFrames+1); i++ )
		new(BMStrings) FString(TEXT(""));
	if( BMMaxFrames )
		BMFrameTimes.Add(BMMaxFrames-BMDiscardedFrames+1);

	LastFrameCycles		= appCycles();

	// Seed random number generator with constant seed for benchmarking.
	if( GIsBenchmarking )
		appRandInit( 0 );

	// Movie recording.
	RecordingMovie	= ParseParam(appCmdLine(),TEXT("RECORDMOVIE"));
		
	unguard;
}

inline void	CMainLoop::RunLoop(void)
{
	// Ever really needed ? - Erik
	if( Finished() ) 
		return;

	// Clear stats (will also update old stats).
	GStats.Clear();

    if( GIsBenchmarking )
	{
		if( BMFrames == BMDiscardedFrames )
			BMSeconds = 0.0f;
		else if( BMFrames > BMMaxFrames )
			appRequestExit(0);
	}

	// Update the world.
	DOUBLE NewTime   = appSeconds();
	FLOAT  DeltaTime = NewTime - OldTime;

	guard(UpdateWorld);
	if( GUseFixedTimeStep )
	{
		GDeltaTime = GFixedTimeStep;
		GCurrentTime += GFixedTimeStep;
		if( BMFrames < BMMaxFrames )
			BMSeconds += DeltaTime;
		BMFrames++;
	}
	else
	{
		GCurrentTime = NewTime;
		GDeltaTime = DeltaTime;
	}

	//Update.
	if( GDeltaTime < 0.f )
		GDeltaTime = 1 / 20.f;
	Engine->Tick( GDeltaTime );
	// sjs --- engine::tick may load a new map and cause the timing to be reset (this is a good thing)
	if( appSeconds() < NewTime )
           SecondStartTime = NewTime = GCurrentTime = appSeconds();
	// --- sjs
	if( GWindowManager )
		GWindowManager->Tick( GDeltaTime );

	TickCount++;
	OldTime = NewTime;

	if( OldTime > SecondStartTime + 1 )
	{
		if( GUseFixedTimeStep )
			Engine->CurrentTickRate = 1 / GFixedTimeStep;
		else
			Engine->CurrentTickRate = (FLOAT)TickCount / (OldTime - SecondStartTime);
		SecondStartTime = OldTime;
		TickCount = 0;
	}
	unguard;

	// Enforce optional maximum tick rate.
	guard(EnforceTickRate);
	if( !GUseFixedTimeStep && IncludeMessaging )
	{		
		FLOAT MaxTickRate = Engine->GetMaxTickRate();
		// Nobody sees a minimized or covered window, so it does not need more frames than the game needs ticks.
		SDL_Window* Window = SDL_GL_GetCurrentWindow();
		if( Window && (SDL_GetWindowFlags( Window ) & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_OCCLUDED)) )
			MaxTickRate = MaxTickRate > 0.f ? Min( MaxTickRate, 30.f ) : 30.f;
		if( MaxTickRate>0.0 )
		{
			// Sleep alone wakes up to a millisecond late.
			DOUBLE Delta = 1.0 / MaxTickRate - (appSeconds() - OldTime);
			if( Delta > 0.0 )
				SDL_DelayPrecise( (Uint64)(Delta * 1000000000.0) );
		}
	}
	unguard;

	if( IncludeMessaging && Engine->Client )
	{
		// Handle all incoming messages.
		guard(MessagePump);
		Engine->Client->Viewports(0)->UpdateInput(0, 0);
		unguard;
	}

	if( RecordingMovie && Engine->Client )
		Engine->Client->Viewports(0)->Exec(TEXT("shot"));

	GStats.DWORDStats( GEngineStats.STATS_Frame_TotalCycles ) = appCycles() - LastFrameCycles;
	GStats.DWORDStats( GEngineStats.STATS_Game_ScriptCycles ) = GScriptCycles;
	GScriptCycles	= 0;
	LastFrameCycles = appCycles();

	if( GIsBenchmarking )
	{
		if( BMFrames == 1 )
		{
			// Get descriptions.
			GStats.UpdateString( BMStrings(0), 1 );
			BMFrameTimes(0) = 0.f;
		}
		else
		{
			// Get frame stats. (Index already implies the +1 for descriptions)
			INT BMStringsIndex = Clamp( BMFrames - BMDiscardedFrames, 2, BMMaxFrames - BMDiscardedFrames );
			GStats.UpdateString( BMStrings(BMStringsIndex - 1), 0 );
			BMFrameTimes( BMStringsIndex - 1 ) = GStats.DWORDStats( GEngineStats.STATS_Frame_TotalCycles ) * GSecondsPerCycle * 1000.f;
		}
	}
}

inline CMainLoop::~CMainLoop(void) noexcept(false)
{
	guard(CMainLoopDtor);

	GIsRunning = 0;
	if( GIsBenchmarking )
	{
		INT Frames		= BMMaxFrames - BMDiscardedFrames; 
		UBOOL LogLowFPS = ParseParam(appCmdLine(),TEXT("ONLYLOGLOWFPS"));
		FLOAT LowFPS	= 100.f; // in ms

		// Level.
		FString LevelName = Engine->Client->Viewports(0)->Actor->GetViewTarget()->XLevel->GetPathName();
		LevelName = LevelName.LeftChop( 8 );

		// Get time & date.
		INT Year, Month, DayOfWeek, Day, Hour, Minutes, Sec, MSec;
		appSystemTime( Year, Month, DayOfWeek, Day, Hour, Minutes, Sec, MSec );
		FString DateTime = FString::Printf(TEXT("%i-%02i-%02i-%02i-%02i-%02i"),Year,Month,Day,Hour,Minutes,Sec);

		// Machine Details.
		FString MachineString = FString::Printf(TEXT("%s\r\n%s\r\n%s\r\n%s\r\n\r\n%s\r\n\r\n"),GBuildLabel,GMachineOS,GMachineCPU,GMachineVideo,appCmdLine());
		FString OutputString = MachineString;
		FString LowFPSString = TEXT("");

		// Count how many frames take more than LowFPS ms.
		if( LogLowFPS )
		{
			INT Count = 0;
			for( INT i=1; i<Frames; i++ )	
				if( BMFrameTimes(i) > LowFPS )
					Count++;
			
			LowFPSString = FString::Printf(TEXT("High frametimes: %i / %i == %f percent \r\n\r\n"), Count, Frames, 100.f * FLOAT(Count) / Frames ); 
			OutputString += LowFPSString;
		}

		// Determine min/ max framerate and score.
		FLOAT	MinFPS = 1000.f,
				MaxFPS = 0.f,
				AvgFPS = Frames / BMSeconds,
				Score  = 0.f;

		// Check for FRAMETIMECAP command line option.
		FLOAT FPSCap = 0;
		Parse(appCmdLine(),TEXT("MAXFPS="),FPSCap);
		if( FPSCap <= 0 )
			FPSCap = 80;
		FLOAT FrameTimeCap = 1000.f / FPSCap;

		for( INT i=1; i<Frames; i++ )
		{
			// Calculate min/max.
			FLOAT FPS = 1000.f / BMFrameTimes(i);
			MinFPS = Min( MinFPS, FPS );
			MaxFPS = Max( MaxFPS, FPS );

			// Calculate score.
			Score += Max( BMFrameTimes(i), FrameTimeCap ) ;
		}

		Score = Frames / Score * 1000.f;

		// Output detailed results to a file.
		INT LastRand = appRand();
		for( INT i=0; i<Frames; i++ )
		{
			if( LogLowFPS && i!=0 )
			{
				if( BMFrameTimes(i) > LowFPS )
					OutputString += BMStrings(i);
			}
			else
				OutputString += BMStrings(i);
		}
		TCHAR File[1024];
		appSprintf( File, TEXT("..\\Benchmark\\CSVs\\stats-%s.csv"), *DateTime );
		appSaveStringToFile( OutputString, File );

		// Output average framerate.
		OutputString = TEXT("");
		appLoadFileToString( OutputString, TEXT("..\\Benchmark\\benchmark.log") );
		OutputString += FString::Printf(TEXT("%f / %f / %f fps -- Score = %f        rand[%i]\r\n"), MinFPS, AvgFPS, MaxFPS, Score, LastRand );
		appSaveStringToFile( OutputString, TEXT("..\\Benchmark\\benchmark.log") );
		OutputString = MachineString;
		if( LogLowFPS )
			OutputString += LowFPSString;
		OutputString += FString::Printf(TEXT("%f / %f / %f fps         rand[%i]\r\nScore = %f\r\n"), MinFPS, AvgFPS, MaxFPS, LastRand, Score );
		appSaveStringToFile( OutputString, *FString::Printf(TEXT("..\\Benchmark\\Results\\avgfps-%s.log"), *DateTime ) );
	
		// Output low framerate stats.
		OutputString = TEXT("");
		appLoadFileToString( OutputString, TEXT("..\\Benchmark\\lowframerate.log") );
		OutputString += FString::Printf(TEXT("%s\r\n%f / %f / %f fps\r\nScore = %f\r\n%s\r\n"), *LevelName, MinFPS, AvgFPS, MaxFPS, Score, *LowFPSString );
		appSaveStringToFile( OutputString, TEXT("..\\Benchmark\\lowframerate.log") );

		GLog->Flush();
		GFileManager->Copy( *FString::Printf(TEXT("..\\Benchmark\\Logs\\ut2004-%s.log"),*DateTime), TEXT("ut2004.log") );
	}

	unguard;
}

static void MainLoop( UEngine* Engine )
{
	guard(MainLoop);
	check(Engine);

	CMainLoop* theLoop = new CMainLoop(Engine);
	while (!theLoop->Finished()) 
	{
		theLoop->RunLoop();
	}
	
	delete theLoop;

	unguard;
}


static UEngine* InitEngine()
{
	guard(InitEngine);
	DOUBLE LoadTime = appSeconds();

	// Set exec hook.
	GExec = NULL;

	// First-run menu.
	INT FirstRun=0;
	GConfig->GetInt( TEXT("FirstRun"), TEXT("FirstRun"), FirstRun );

	// Older inis and the retail menu still name drivers that no longer exist.
	FString Viewport = GConfig->GetStr(TEXT("Engine.Engine"),TEXT("ViewportManager"),TEXT("System"));
	if( Viewport!=TEXT("SDLDrv.SDLClient") )
	{
		debugf(TEXT("Forcing use of SDLDrv instead of %s."),*Viewport);
		GConfig->SetString(TEXT("Engine.Engine"), TEXT("ViewportManager"), TEXT("SDLDrv.SDLClient"));
	}
	FString Renderer = GConfig->GetStr(TEXT("Engine.Engine"),TEXT("RenderDevice"),TEXT("System"));
	if( Renderer!=TEXT("OpenGLDrv.OpenGLRenderDevice") && Renderer!=TEXT("Engine.NullRenderDevice") )
	{
		debugf(TEXT("Forcing use of OpenGLDrv instead of %s."),*Renderer);
		GConfig->SetString(TEXT("Engine.Engine"), TEXT("RenderDevice"), TEXT("OpenGLDrv.OpenGLRenderDevice"));
	}

	if( ParseParam(appCmdLine(),TEXT("FirstRun")) )
		FirstRun=0;
	if( FirstRun<220 )
	{
		// Migrate savegames.
		TArray<FString> Saves = GFileManager->FindFiles( TEXT("../Save/*.usa"), 1, 0 );
		for( TArray<FString>::TIterator It(Saves); It; ++It )
		{
			INT Pos = appAtoi(**It+4);
			FString Section = TEXT("UnrealShare.UnrealSlotMenu");
			FString Key     = FString::Printf(TEXT("SlotNames[%i]"),Pos);
			if( appStricmp(GConfig->GetStr(*Section,*Key,TEXT("user")),TEXT(""))==0 )
				GConfig->SetString(*Section,*Key,TEXT("Saved game"),TEXT("user"));
		}
	}

	// EXEC from command-line
	FString Command;
	if( Parse(appCmdLine(),TEXT("consolecommand="), Command) )
	{
        if (GExec == NULL)
        {
            debugf(TEXT("Can't use consolecommand"));
        }
        else
        {
		    debugf(TEXT("Executing console command %s"),*Command);
    		GExec->Exec( *Command, *GLog );
        }
		return NULL;
	}

	// Test render device.
	FString Device;
	if( Parse(appCmdLine(),TEXT("testrendev="),Device) )
	{
		debugf(TEXT("Detecting %s"),*Device);
		try
		{
			UClass* Cls = LoadClass<URenderDevice>( NULL, *Device, NULL, 0, NULL );
			GConfig->SetInt(*Device,TEXT("DescFlags"),RDDESCF_Incompatible);
			GConfig->Flush(0);
			if( Cls )
			{
				URenderDevice* RenDev = ConstructObject<URenderDevice>(Cls);
				if( RenDev )
				{
					if( RenDev->Init() )
					{
						debugf(TEXT("Successfully detected %s"),*Device);
					}
					else
					{
						delete RenDev;
						RenDev = NULL;
					}
				}
			}
		} catch( ... ) {}
		FArchive* Ar = GFileManager->CreateFileWriter(TEXT("Detected.ini"),0);
		if( Ar )
			delete Ar;
		return NULL;
	}

    // gam ---
    if( ParseParam(appCmdLine(),TEXT("debugging")) && (GFileManager->FileSize(TEXT("Running.ini")) >= 0) )
        GFileManager->Delete(TEXT("Running.ini"),0,0);
    // --- gam

	// Create is-running semaphore file.
	FArchive* Ar = GFileManager->CreateFileWriter(TEXT("Running.ini"),0);
	if( Ar )
		delete Ar;

	// Update first-run.
	if( FirstRun<ENGINE_VERSION )
		FirstRun = ENGINE_VERSION;
	GConfig->SetInt( TEXT("FirstRun"), TEXT("FirstRun"), FirstRun );


	// Create the global engine object.
	UClass* EngineClass;
	if( !GIsEditor )
	{
		// Create game engine.
		EngineClass = UObject::StaticLoadClass( UGameEngine::StaticClass(), NULL, TEXT("ini:Engine.Engine.GameEngine"), NULL, LOAD_NoFail, NULL );
	}
	else
	{
		// Editor.
		EngineClass = UObject::StaticLoadClass( UEngine::StaticClass(), NULL, TEXT("ini:Engine.Engine.EditorEngine"), NULL, LOAD_NoFail, NULL );
	}
	UEngine* Engine = ConstructObject<UEngine>( EngineClass );
	Engine->Init();
	debugf( TEXT("Startup time: %f seconds"), appSeconds()-LoadTime );

	return Engine;
	unguard;
}



//
// Main entry point.
// This is an example of how to initialize and launch the engine.
//
int main(int argc, char **argv)
{
	TCHAR CmdLine[1024] = TEXT("");
#ifdef WIN32
	// appInit skips the executable name on Windows, as GetCommandLine starts with it.
	appStrncpy( CmdLine, GetCommandLine(), ARRAY_COUNT(CmdLine) );
#else
    if (argc >= 2)
    {
	    appStrcpy(CmdLine, appFromAnsi(argv[1]));
	    for (INT i = 2; i < argc; i++)
    	{
       		appStrcat(CmdLine, TEXT(" "));
    		appStrcat(CmdLine, appFromAnsi(argv[i]));
    	}
    }
#endif

	// Remember instance.
	INT ErrorLevel = 0;
	GIsStarted     = 1;

#ifdef WIN32
	hInstance = GetModuleHandle( NULL );
#endif
	strcpy( GModule, appToAnsi(TEXT("UT2004")) );
	appStrcpy( GPackage, appPackage() );

	// Begin guarded code.
#ifndef _DEBUG
	try
	{
#endif

		// Init core.
		GIsClient = GIsGuarded = 1;
		appInit( GPackage, CmdLine, &Malloc, &Log, &Error, &Warn, &FileManager, FConfigCacheIni::Factory, 1 );

#if __STATIC_LINK
        // Clean lookups.
        for( INT k=0; k<ARRAY_COUNT(GNativeLookupFuncs); k++ )
            GNativeLookupFuncs[k] = NULL;

        INT Lookup = 0;
        // Core natives.
        GNativeLookupFuncs[Lookup++] = &FindCoreUObjectNative;
        GNativeLookupFuncs[Lookup++] = &FindCoreUCommandletNative;

        // auto-generated lookups and statics
        AUTO_INITIALIZE_REGISTRANTS_ENGINE;
        AUTO_INITIALIZE_REGISTRANTS_SDLDRV;
        AUTO_INITIALIZE_REGISTRANTS_FIRE;
        AUTO_INITIALIZE_REGISTRANTS_ALAUDIO;
        AUTO_INITIALIZE_REGISTRANTS_OPENGLDRV;
        AUTO_INITIALIZE_REGISTRANTS_IPDRV;
        RegisterNamesXInterface();
        AUTO_INITIALIZE_REGISTRANTS_XINTERFACE;
        AUTO_INITIALIZE_REGISTRANTS_XGAME;
        AUTO_INITIALIZE_REGISTRANTS_UWEB;
        RegisterNamesOnslaught();
        AUTO_INITIALIZE_REGISTRANTS_ONSLAUGHT;
        autoInitializeRegistrantsEditor(Lookup);

        GNativeLookupFuncs[Lookup++] = &FindUWebUWebResponseNative;
        GNativeLookupFuncs[Lookup++] = &FindUWebUWebRequestNative;
        check( Lookup < ARRAY_COUNT(GNativeLookupFuncs) );


// The linker drops these as dead code without an explicit reference.
char blah;
snprintf(&blah, 1, "%d%d%d", GLoadedSDLDrv, GLoadedOpenGLDrv, GLoadedALAudio);
#endif

		// Init mode.
		GIsServer     = 1;
		GIsClient     = !ParseParam(appCmdLine(),TEXT("SERVER"));
		GIsEditor     = 0;
		GIsScriptable = 1;
		GLazyLoad     = !GIsClient || ParseParam(appCmdLine(),TEXT("LAZY"));

		// Figure out whether to show log or splash screen.
		UBOOL ShowLog = ParseParam(CmdLine,TEXT("LOG"));

		TCHAR *LogoFName = appStaticString1024();
		if( !Parse( appCmdLine(), TEXT("-USERLOGO="), LogoFName, 1024 ) )
			appSprintf( LogoFName, TEXT("%sLogo.bmp"), GPackage );

		FString Filename = FString(TEXT("../Help")) * LogoFName;
		if ( appStrlen(GModPath)>0 )
			Filename = FString(GModPath) * FString(TEXT("Help")) * GModName + TEXT("Logo.bmp");
		if( GFileManager->FileSize(*Filename)<0 )
			Filename = FString(TEXT("../Help")) * GPackage + TEXT("Logo.bmp"); // gam
		if( (!ShowLog) &&
            (!ParseParam(CmdLine,TEXT("server"))) &&
            (!ParseParam(CmdLine,TEXT("nosplash"))) &&
            !appStrfind(CmdLine,TEXT("TestRenDev")) )
			InitSplash( *Filename );

		// Benchmarking/ movie recording.
		GIsBenchmarking		= ParseParam(appCmdLine(),TEXT("BENCHMARK"));
		GIsRecordingMovie	= ParseParam(appCmdLine(),TEXT("RECORDMOVIE"));
	
		// Fixed timestep.
		FLOAT FixedFPS		= 0.f;
		Parse(appCmdLine(),TEXT("FIXEDFPS="),FixedFPS);
	
		if( GIsRecordingMovie || GIsBenchmarking || FixedFPS > 0.f )
		{
			GUseFixedTimeStep	= 1;
			GFixedTimeStep		= FixedFPS > 0.f ? 1.f / FixedFPS : 1.f / 30.f;
		}

		if( ParseParam(appCmdLine(),TEXT("NULLRENDER")) )
			GConfig->SetString( TEXT("Engine.Engine"		), TEXT("RenderDevice"		), TEXT("Engine.NullRenderDevice"	));

		// Don't update ini files if benchmarking.
		if( GIsBenchmarking || ParseParam(appCmdLine(),TEXT("NOINI")) || ParseParam(appCmdLine(),TEXT("EXITAFTERDEMO")) )
		{
			GConfig->Detach( GIni );
			GConfig->Detach( GUserIni );
		}

		// Init engine.
		UEngine* Engine = InitEngine();

		if( Engine )
		{
			// Hide splash screen.
			ExitSplash();

			// Optionally Exec an exec file
			FString Temp;
			if( Parse(CmdLine, TEXT("EXEC="), Temp) )
			{
				Temp = FString(TEXT("exec ")) + Temp;
				if( Engine->Client && Engine->Client->Viewports.Num() && Engine->Client->Viewports(0) )
					Engine->Client->Viewports(0)->Exec( *Temp, *GLog );
			}

#ifdef BETAEXPIRE
			if ( time(NULL) > (BETAEXPIRE + 30 * 24 * 60 * 60) )
			{
				GIsRequestingExit = 1;
				appMsgf( 0, TEXT("This beta has expired.") );
				while (1) _exit(42);
			}
#endif

			// Start main engine loop, including the Windows message pump.
			if( !GIsRequestingExit )
				MainLoop( Engine );
		}

		// Clean shutdown.
		GFileManager->Delete(TEXT("Running.ini"),0,0);

		GLog->Log( NAME_Title, LocalizeGeneral(TEXT("Exit"),GPackage) );

		appPreExit();
		GIsGuarded = 0;
#ifndef _DEBUG
	}
	catch( ... )
	{
		// Crashed.
		ErrorLevel = 1;
		Error.HandleError();
		// The GUI build has no console, so the error would otherwise only be in the log.
		if( GIsClient )
		{
			// The history is one line, which the message box would size to its full width.
			FString Message = GErrorHist;
			for( INT i; (i=Message.InStr(TEXT(" <- ")))!=-1; )
				Message = Message.Left(i) + TEXT("\n") + Message.Mid(i+4);
			SDL_ShowSimpleMessageBox( SDL_MESSAGEBOX_ERROR, "UT2004", appToAnsi(*Message), NULL );
		}
	}
#endif

	// Final shut down.
	appExit();
	GIsStarted = 0;
	return((int) ErrorLevel);
}

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/

