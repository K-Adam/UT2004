/*=============================================================================
	SDLClient.cpp: USDLClient code.
	Copyright 2002 Epic Games, Inc. All Rights Reserved.

        SDL website: http://www.libsdl.org/
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
SDL_JoystickID USDLClient::VirtualPad = 0;
SDL_Gamepad* USDLClient::WatchedPad = NULL;


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
	new(GetClass(),TEXT("VirtualJoystick"),      RF_Public)UIntProperty(CPP_PROPERTY(VirtualJoystick),       TEXT("Joystick"), CPF_Config );
	new(GetClass(),TEXT("VirtualJoystickScale"), RF_Public)UFloatProperty(CPP_PROPERTY(VirtualJoystickScale), TEXT("Joystick"), CPF_Config );
	new(GetClass(),TEXT("VirtualJoystickSwap"),  RF_Public)UBoolProperty(CPP_PROPERTY(VirtualJoystickSwap),  TEXT("Joystick"), CPF_Config );

	// This runs on the default object before the config loads, so inis without the keys get these.
	PadDeadZone = 0.1f;
	PadLookExponent = 2.f;
	PadTurnBoost = 1.5f;
	PadGyroSensitivity = 2.f;
	VirtualJoystickScale = 1.f;

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

static UBOOL SDLLaunchURL( const TCHAR* URL )
{
	return SDL_OpenURL( &appToUtf8(URL)(0) );
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

static SDL_Haptic* DeviceHaptic;
static bool DeviceHapticInit;

static void CloseDeviceHaptic()
{
	if( DeviceHaptic )
		SDL_CloseHaptic( DeviceHaptic );
	DeviceHaptic = NULL;
	if( DeviceHapticInit )
		SDL_QuitSubSystem( SDL_INIT_HAPTIC );
	DeviceHapticInit = false;
}

static void SDLRumble( FLOAT Low, FLOAT High, INT Milliseconds )
{
	if( USDLClient::Gamepad && SDL_RumbleGamepad( USDLClient::Gamepad, (Uint16)(Clamp(Low, 0.f, 1.f) * 65535), (Uint16)(Clamp(High, 0.f, 1.f) * 65535), Milliseconds ) )
		return;
	// The on-screen controls' virtual pad can't rumble, so the phone or tablet showing them vibrates itself.
	// SDL lists Android's own vibrator first.
	if( !USDLClient::VirtualPad )
		return;
	if( !DeviceHapticInit && (DeviceHapticInit = SDL_InitSubSystem( SDL_INIT_HAPTIC )) )
	{
		int Count = 0;
		SDL_HapticID* IDs = SDL_GetHaptics( &Count );
		if( Count > 0 && (DeviceHaptic = SDL_OpenHaptic( IDs[0] )) && !SDL_InitHapticRumble( DeviceHaptic ) )
		{
			SDL_CloseHaptic( DeviceHaptic );
			DeviceHaptic = NULL;
		}
		SDL_free( IDs );
	}
	if( DeviceHaptic )
		SDL_PlayHapticRumble( DeviceHaptic, Clamp( Max(Low, High), 0.f, 1.f ), Milliseconds );
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
	GLaunchURL = SDLLaunchURL;
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

	VJAuto = ParseParam(appCmdLine(),TEXT("VIRTUALJOYSTICK"));

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

// The on-screen controls have no motion sensors, so the phone or tablet showing them aims with its own.
static SDL_Sensor* DeviceSensors[2];
static bool DeviceSensorsInit;

static void CloseDeviceSensors()
{
	for( INT i=0; i<2; i++ )
	{
		if( DeviceSensors[i] )
			SDL_CloseSensor( DeviceSensors[i] );
		DeviceSensors[i] = NULL;
	}
	if( DeviceSensorsInit )
		SDL_QuitSubSystem( SDL_INIT_SENSOR );
	DeviceSensorsInit = false;
}

// Gyro aim needs the accelerometer too, for gravity and calibration.
void USDLClient::EnableGyro()
{
	guard(USDLClient::EnableGyro);
	if( VirtualPad && PadGyro )
	{
		if( !DeviceSensorsInit && (DeviceSensorsInit = SDL_InitSubSystem( SDL_INIT_SENSOR )) )
		{
			int Count = 0;
			SDL_SensorID* IDs = SDL_GetSensors( &Count );
			for( INT i=0; i<Count; i++ )
			{
				SDL_SensorType Type = SDL_GetSensorTypeForID( IDs[i] );
				INT Slot = Type == SDL_SENSOR_GYRO ? 0 : Type == SDL_SENSOR_ACCEL ? 1 : -1;
				if( Slot >= 0 && !DeviceSensors[Slot] )
					DeviceSensors[Slot] = SDL_OpenSensor( IDs[i] );
			}
			SDL_free( IDs );
			debugf( NAME_Init, DeviceSensors[0] && DeviceSensors[1] ? TEXT("Device gyro enabled") : TEXT("No device gyro") );
		}
		return;
	}
	CloseDeviceSensors();
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
// The on-screen controls drive an SDL virtual gamepad, so they share the pad's binds and response shaping.
// It takes the place of a physical pad while attached.
//
void USDLClient::SetVirtualJoystick( UBOOL On )
{
	guard(USDLClient::SetVirtualJoystick);
	CloseGamepad();
	if( On )
	{
		SDL_VirtualJoystickDesc Desc;
		SDL_INIT_INTERFACE( &Desc );
		Desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
		Desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
		Desc.nbuttons = 3;
		Desc.name = "UT2004 Virtual Joystick";
		VirtualPad = SDL_AttachVirtualJoystick( &Desc );
		if( VirtualPad )
			Gamepad = SDL_OpenGamepad( VirtualPad );
		if( Gamepad )
		{
			// The opened joystick starts at raw 0, which reads as half pulled triggers.
			SDL_Joystick* Joystick = SDL_GetGamepadJoystick( Gamepad );
			SDL_SetJoystickVirtualAxis( Joystick, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, SDL_JOYSTICK_AXIS_MIN );
			SDL_SetJoystickVirtualAxis( Joystick, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, SDL_JOYSTICK_AXIS_MIN );
			debugf( NAME_Init, TEXT("Virtual joystick on") );
			EnableGyro();
			WatchPads();
			return;
		}
		// SDL builds without HIDAPI have no virtual joystick driver.
		debugf( NAME_Init, TEXT("Couldn't attach virtual joystick: %s"), appFromAnsi(SDL_GetError()) );
		VJWanted = 0;
		VJFailed = 1;
	}
	if( VirtualPad )
		SDL_DetachVirtualJoystick( VirtualPad );
	VirtualPad = 0;
	WatchPads();
	CloseDeviceSensors();
	if( UseJoystick )
		OpenGamepad();
	unguard;
}

//
// Follows the VirtualJoystick setting. Enabled also brings back controls that stepped aside under Auto.
//
void USDLClient::UpdateVirtualJoystick()
{
	guard(USDLClient::UpdateVirtualJoystick);
	UBOOL Wanted = !VJFailed && (VirtualJoystick == VJM_Enabled || (VirtualJoystick == VJM_Auto && VJAuto));
	if( Wanted == VJWanted && (VirtualPad || VirtualJoystick != VJM_Enabled) )
		return;
	VJWanted = Wanted;
	if( Viewports.Num() )
		CastChecked<USDLViewport>(Viewports(0))->SwitchVirtualJoystick( Wanted );
	else
		SetVirtualJoystick( Wanted );
	unguard;
}

//
// Opens the first real pad while the on-screen controls are on and pads are enabled, otherwise closes it.
//
void USDLClient::WatchPads()
{
	guard(USDLClient::WatchPads);
	if( WatchedPad )
		SDL_CloseGamepad( WatchedPad );
	WatchedPad = NULL;
	if( !VirtualPad || !UseJoystick )
		return;
	int Count = 0;
	SDL_JoystickID* Pads = SDL_GetGamepads( &Count );
	for( INT i=0; i<Count && !WatchedPad; i++ )
		if( Pads[i] != VirtualPad )
			WatchedPad = SDL_OpenGamepad( Pads[i] );
	SDL_free( Pads );
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
	VirtualPad = 0;
	WatchPads();
	CloseDeviceSensors();
	CloseDeviceHaptic();
	GClipboardCopy = NULL;
	GClipboardPaste = NULL;
	GLaunchURL = NULL;
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
	UpdateVirtualJoystick();
	// The menu turns UseJoystick on and off with set ini, which ends up here.
	if( !UseJoystick && !VirtualPad )
		CloseGamepad();
	else if( !Gamepad )
		OpenGamepad();
	else
		EnableGyro();
	WatchPads();
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

