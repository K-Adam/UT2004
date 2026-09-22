/*=============================================================================
	SDLClient.cpp: USDLClient code.
	Copyright 2002 Epic Games, Inc. All Rights Reserved.

        SDL website: http://www.libsdl.org/

Revision history:
	* Created by Ryan C. Gordon, based on WinDrv.
      This is an updated rewrite of the original SDLDrv.
=============================================================================*/

#include "SDLDrv.h"
#include "xForceFeedback.h"

/*-----------------------------------------------------------------------------
	Class implementation.
-----------------------------------------------------------------------------*/

IMPLEMENT_CLASS(USDLClient);

/*-----------------------------------------------------------------------------
	USDLClient implementation.
-----------------------------------------------------------------------------*/

SDL_Gamepad *USDLClient::Gamepad = NULL;


//
// USDLClient constructor.
//
USDLClient::USDLClient()
{
	guard(USDLClient::USDLClient);
	unguard;
}

//
// Static init.
//
void USDLClient::StaticConstructor()
{
	guard(USDLClient::StaticConstructor);

	new(GetClass(),TEXT("UseJoystick"),          RF_Public)UBoolProperty(CPP_PROPERTY(UseJoystick),          TEXT("Display"),  CPF_Config );
	new(GetClass(),TEXT("JoystickNumber"),       RF_Public)UIntProperty(CPP_PROPERTY(JoystickNumber),        TEXT("Joystick"), CPF_Config );
	new(GetClass(),TEXT("StartupFullscreen"),    RF_Public)UBoolProperty(CPP_PROPERTY(StartupFullscreen),    TEXT("Display"),  CPF_Config );
	new(GetClass(),TEXT("IgnoreUngrabbedMouse"), RF_Public)UBoolProperty(CPP_PROPERTY(IgnoreUngrabbedMouse), TEXT("Display"),  CPF_Config );
	new(GetClass(),TEXT("TextToSpeechFile"),     RF_Public)UStrProperty(CPP_PROPERTY(TextToSpeechFile),      TEXT("Display"),  CPF_Config );
	new(GetClass(),TEXT("PadDeadZone"),          RF_Public)UFloatProperty(CPP_PROPERTY(PadDeadZone),         TEXT("Joystick"), CPF_Config );
	new(GetClass(),TEXT("PadLookExponent"),      RF_Public)UFloatProperty(CPP_PROPERTY(PadLookExponent),     TEXT("Joystick"), CPF_Config );
	new(GetClass(),TEXT("PadTurnBoost"),         RF_Public)UFloatProperty(CPP_PROPERTY(PadTurnBoost),        TEXT("Joystick"), CPF_Config );
	new(GetClass(),TEXT("PadGyro"),              RF_Public)UBoolProperty(CPP_PROPERTY(PadGyro),              TEXT("Joystick"), CPF_Config );
	new(GetClass(),TEXT("PadGyroSensitivity"),   RF_Public)UFloatProperty(CPP_PROPERTY(PadGyroSensitivity),  TEXT("Joystick"), CPF_Config );

	// This runs on the default object before the config loads, so inis without the keys get these.
	PadDeadZone = 0.1f;
	PadLookExponent = 2.f;
	PadTurnBoost = 1.5f;
	PadGyroSensitivity = 2.f;

	unguard;
}


// TCHAR is UTF-16 on every platform, SDL takes UTF-8.
static void SDLClipboardCopy( const TCHAR* Str )
{
	char* Text = SDL_iconv_string( "UTF-8", "UTF-16LE", (const char*)Str, (appStrlen(Str) + 1) * sizeof(TCHAR) );
	if( Text )
	{
		SDL_SetClipboardText( Text );
		SDL_free( Text );
	}
}

static FString SDLClipboardPaste()
{
	FString Result;
	char* Text = SDL_GetClipboardText();
	if( Text )
	{
		TCHAR* Wide = (TCHAR*)SDL_iconv_string( "UTF-16LE", "UTF-8", Text, SDL_strlen(Text) + 1 );
		if( Wide )
		{
			Result = Wide;
			SDL_free( Wide );
		}
		SDL_free( Text );
	}
	return Result;
}

static void SDLRumble( FLOAT Low, FLOAT High, INT Milliseconds )
{
	if( USDLClient::Gamepad )
		SDL_RumbleGamepad( USDLClient::Gamepad, (Uint16)(Clamp(Low, 0.f, 1.f) * 65535), (Uint16)(Clamp(High, 0.f, 1.f) * 65535), Milliseconds );
}

static void sdl_atexit_handler(void)
{
    static bool already_called = false;

    if (!already_called)
    {
        already_called = true;
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }
}



//
// Initialize the platform-specific viewport manager subsystem.
// Must be called after the Unreal object manager has been initialized.
// Must be called before any viewports are created.
//
void USDLClient::Init( UEngine* InEngine )
{
	guard(USDLClient::USDLClient);

	// Init base.
	UClient::Init( InEngine );

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD))
    {
        const TCHAR *err = appFromAnsi(SDL_GetError());
		appErrorf(TEXT("Couldn't initialize SDL: %s\n"), err);
		appExit();
	}

    atexit(sdl_atexit_handler);
	GClipboardCopy = SDLClipboardCopy;
	GClipboardPaste = SDLClipboardPaste;
	GRumble = SDLRumble;

	// Community mappings for pads SDL does not know, loaded before PostEditChange opens the pad.
	const char* BasePath = SDL_GetBasePath();
	char* DBPath = NULL;
	if( SDL_asprintf( &DBPath, "%sgamecontrollerdb.txt", BasePath ? BasePath : "" ) >= 0 )
	{
		int Mappings = SDL_AddGamepadMappingsFromFile( DBPath );
		if( Mappings < 0 )
			debugf( NAME_Init, TEXT("Couldn't load gamecontrollerdb.txt: %s"), appFromAnsi(SDL_GetError()) );
		else
			debugf( NAME_Init, TEXT("Loaded %i gamepad mappings"), Mappings );
		SDL_free( DBPath );
	}

	// Note configuration.
	PostEditChange();

	if( ParseParam(appCmdLine(),TEXT("defaultres")) )
	{
		WindowedViewportX = 640;
		WindowedViewportY = 480;
	}

	// Success.
	debugf( NAME_Init, TEXT("SDLClient initialized.") );

	unguard;
}


//
// Opens gamepad JoystickNumber, or the first one. Only devices with a gamepad mapping open, since the binds
// assume the gamepad layout and a wheel or pedal axis resting at full travel would spin the view.
//
void USDLClient::OpenGamepad()
{
	guard(USDLClient::OpenGamepad);

	int Count = 0;
	SDL_JoystickID* Gamepads = SDL_GetGamepads( &Count );
	if( Count > 0 )
	{
		SDL_JoystickID ID = Gamepads[JoystickNumber >= 0 && JoystickNumber < Count ? JoystickNumber : 0];
		Gamepad = SDL_OpenGamepad( ID );
		const char* Name = SDL_GetGamepadNameForID( ID );
		if( Gamepad )
		{
			debugf( NAME_Init, TEXT("Gamepad: %s"), appFromAnsi(Name ? Name : "?") );
			EnableGyro();
		}
		else
			debugf( NAME_Init, TEXT("Couldn't open gamepad: %s"), appFromAnsi(SDL_GetError()) );
	}
	SDL_free( Gamepads );

	unguard;
}

// Gyro aim needs the accelerometer too, for gravity and calibration.
void USDLClient::EnableGyro()
{
	guard(USDLClient::EnableGyro);
	if( !SDL_GamepadHasSensor( Gamepad, SDL_SENSOR_GYRO ) || !SDL_GamepadHasSensor( Gamepad, SDL_SENSOR_ACCEL ) )
		return;
	bool On = PadGyro != 0;
	if( On != SDL_GamepadSensorEnabled( Gamepad, SDL_SENSOR_GYRO ) )
	{
		SDL_SetGamepadSensorEnabled( Gamepad, SDL_SENSOR_GYRO, On );
		SDL_SetGamepadSensorEnabled( Gamepad, SDL_SENSOR_ACCEL, On );
		debugf( NAME_Init, TEXT("Gamepad gyro %s"), On ? TEXT("enabled") : TEXT("disabled") );
	}
	unguard;
}

void USDLClient::CloseGamepad()
{
	guard(USDLClient::CloseGamepad);
	if( Gamepad )
		SDL_CloseGamepad( Gamepad );
	Gamepad = NULL;
	unguard;
}

//
// Shut down the platform-specific viewport manager subsystem.
//
void USDLClient::Destroy()
{
	guard(USDLClient::Destroy);

	// Make sure to shut down Viewports first.
	for( INT i=0; i<Viewports.Num(); i++ )
		Viewports(i)->ConditionalDestroy();

	// Shut down GRenDev while its context is still alive.
	Engine->GRenDev->Exit(NULL);

	if( GLContext )
		SDL_GL_DestroyContext( GLContext );
	if( Window )
		SDL_DestroyWindow( Window );
	GLContext = NULL;
	Window = NULL;

	CloseGamepad();
	GClipboardCopy = NULL;
	GClipboardPaste = NULL;
	GRumble = NULL;

    SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD);

	debugf( NAME_Exit, TEXT("SDL client shut down") );
	Super::Destroy();
	unguard;
}

void USDLClient::TeardownSR()
{
	guard(USDLClient::TeardownSR);
    // no-op, currently.
    unguard;
}

//
// Failsafe routine to shut down viewport manager subsystem
// after an error has occured. Not guarded.
//
void USDLClient::ShutdownAfterError()
{
	debugf( NAME_Exit, TEXT("Executing USDLClient::ShutdownAfterError") );
    SDL_Quit();
	Window = NULL;
	GLContext = NULL;

	if (Engine && Engine->Audio)
	{
		Engine->Audio->ConditionalShutdownAfterError();
	}

	for (INT i = Viewports.Num() - 1; i >= 0; i--)
	{
		USDLViewport *Viewport = (USDLViewport *) Viewports(i);
		Viewport->ConditionalShutdownAfterError();
	}

	Super::ShutdownAfterError();
}

void USDLClient::NotifyDestroy( void* Src )
{
	guard(USDLClient::NotifyDestroy);
	unguard;
}

//
// Command line.
//
UBOOL USDLClient::Exec( const TCHAR* Cmd, FOutputDevice& Ar )
{
	guard(USDLClient::Exec);
	if( UClient::Exec( Cmd, Ar ) )
	{
		return 1;
	}
	return 0;
	unguard;
}

//
// Perform timer-tick processing on all visible viewports.  This causes
// all realtime viewports, and all non-realtime viewports which have been
// updated, to be blitted.
//
void USDLClient::Tick()
{
	guard(USDLClient::Tick);

	// Blit any viewports that need blitting.
  	for( INT i=0; i<Viewports.Num(); i++ )
	{
		USDLViewport* Viewport = CastChecked<USDLViewport>(Viewports(i));
  		if ((Viewport->IsRealtime() || (Viewport->DirtyViewport != 0)) && Viewport->SizeX && Viewport->SizeY )
			Viewport->Repaint( (Viewport->DirtyViewport == -1) ? 0 : 1 );
	}
	
	unguard;
}

//
// Create a new viewport.
//
UViewport* USDLClient::NewViewport( const FName Name )
{
	guard(USDLClient::NewViewport);
	return new( this, Name )USDLViewport();
	unguard;
}

//
// Configuration change.
//
void USDLClient::PostEditChange()
{
	guard(USDLClient::PostEditChange);
	Super::PostEditChange();
	// The menu turns UseJoystick on and off with set ini, which ends up here.
	if( !UseJoystick )
		CloseGamepad();
	else if( !Gamepad )
		OpenGamepad();
	else
		EnableGyro();
	unguard;
}

//
// Enable or disable all viewport windows that have ShowFlags set (or all if ShowFlags=0).
//
void USDLClient::EnableViewportWindows( DWORD ShowFlags, int DoEnable )
{
	guard(USDLClient::EnableViewportWindows);
    /* meaningless in terms of SDL. */
	unguard;
}

//
// Show or hide all viewport windows that have ShowFlags set (or all if ShowFlags=0).
//
void USDLClient::ShowViewportWindows( DWORD ShowFlags, int DoShow )
{
	guard(USDLClient::ShowViewportWindows); 	
    /* meaningless in terms of SDL. */
	unguard;
}

//
// Make this viewport the current one.
// If Viewport=0, makes no viewport the current one.
//
void USDLClient::MakeCurrent( UViewport* InViewport )
{
	guard(UWindowsViewport::MakeCurrent);
	for( INT i=0; i<Viewports.Num(); i++ )
	{
		UViewport* OldViewport = Viewports(i);
		if( OldViewport->Current && OldViewport!=InViewport )
		{
			OldViewport->Current = 0;
			OldViewport->UpdateWindowFrame();
		}
	}
	if( InViewport )
	{
		LastCurrent = InViewport;
		InViewport->Current = 1;
		InViewport->UpdateWindowFrame();
	}
	unguard;
}

// Returns a pointer to the viewport that was last current.
UViewport* USDLClient::GetLastCurrent()
{
	guard(UWindowsViewport::GetLastCurrent);
	return LastCurrent;
	unguard;
}

/*-----------------------------------------------------------------------------
    That's all, folks.
-----------------------------------------------------------------------------*/

