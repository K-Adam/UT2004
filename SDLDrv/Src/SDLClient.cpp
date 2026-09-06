/*=============================================================================
	SDLClient.cpp: USDLClient code.
	Copyright 2002 Epic Games, Inc. All Rights Reserved.

        SDL website: http://www.libsdl.org/

Revision history:
	* Created by Ryan C. Gordon, based on WinDrv.
      This is an updated rewrite of the original SDLDrv.
=============================================================================*/

#include "SDLDrv.h"

/*-----------------------------------------------------------------------------
	Class implementation.
-----------------------------------------------------------------------------*/

IMPLEMENT_CLASS(USDLClient);

/*-----------------------------------------------------------------------------
	USDLClient implementation.
-----------------------------------------------------------------------------*/

SDL_Joystick *USDLClient::Joystick = NULL;
SDL_Gamepad *USDLClient::Gamepad = NULL;
int USDLClient::JoystickAxes = 0;
int USDLClient::JoystickButtons = 0;
int USDLClient::JoystickHats = 0;
int USDLClient::JoystickBalls = 0;


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
	new(GetClass(),TEXT("JoystickHatNumber"),    RF_Public)UIntProperty(CPP_PROPERTY(JoystickHatNumber),     TEXT("Joystick"), CPF_Config );
	new(GetClass(),TEXT("StartupFullscreen"),    RF_Public)UBoolProperty(CPP_PROPERTY(StartupFullscreen),    TEXT("Display"),  CPF_Config );
	new(GetClass(),TEXT("ScaleJBX"),             RF_Public)UFloatProperty(CPP_PROPERTY(ScaleJBX),            TEXT("Joystick"), CPF_Config );
	new(GetClass(),TEXT("ScaleJBY"),             RF_Public)UFloatProperty(CPP_PROPERTY(ScaleJBY),            TEXT("Joystick"), CPF_Config );
	new(GetClass(),TEXT("IgnoreHat"),            RF_Public)UBoolProperty(CPP_PROPERTY(IgnoreHat),            TEXT("Joystick"), CPF_Config );
	new(GetClass(),TEXT("IgnoreUngrabbedMouse"), RF_Public)UBoolProperty(CPP_PROPERTY(IgnoreUngrabbedMouse), TEXT("Display"),  CPF_Config );
	new(GetClass(),TEXT("TextToSpeechFile"),     RF_Public)UStrProperty(CPP_PROPERTY(TextToSpeechFile),      TEXT("Display"),  CPF_Config );

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

// just in case.  :)  --ryan.
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

	// Note configuration.
	PostEditChange();

	// Default res option.
	if( ParseParam(appCmdLine(),TEXT("defaultres")) )
	{
	    // gam ---
		WindowedViewportX  = FullscreenViewportX  = MenuViewportX   = 640;
		WindowedViewportY  = FullscreenViewportY  = MenuViewportY   = 480;
		// --- gam
		AutoResolution = 0;
	}

	UpdateDesktopSize();


	// Success.
	debugf( NAME_Init, TEXT("SDLClient initialized.") );

	unguard;
}


//
// Opens joystick JoystickNumber, or the first one. Devices SDL knows a gamepad mapping for open as
// gamepads, so every pad gets the same button and axis layout.
//
void USDLClient::OpenJoystick()
{
	guard(USDLClient::OpenJoystick);

	int Count = 0;
	SDL_JoystickID* Joysticks = SDL_GetJoysticks( &Count );
	JoystickButtons = JoystickAxes = JoystickHats = JoystickBalls = 0;
	if( Count > 0 )
	{
		SDL_JoystickID ID = Joysticks[JoystickNumber >= 0 && JoystickNumber < Count ? JoystickNumber : 0];
		if( SDL_IsGamepad( ID ) )
		{
			Gamepad = SDL_OpenGamepad( ID );
			Joystick = Gamepad ? SDL_GetGamepadJoystick( Gamepad ) : NULL;
		}
		else
			Joystick = SDL_OpenJoystick( ID );

		const char* Name = SDL_GetJoystickNameForID( ID );
		if( !Name )
			Name = "?";
		if( !Joystick )
			debugf( NAME_Init, TEXT("Couldn't open joystick %s: %s"), appFromAnsi(Name), appFromAnsi(SDL_GetError()) );
		else if( Gamepad )
			debugf( NAME_Init, TEXT("Gamepad: %s"), appFromAnsi(Name) );
		else
		{
			// Joy13 to Joy16 are the hat, x + y + z + r + u + v + 2 sliders are the 8 axes.
			JoystickHats	= SDL_GetNumJoystickHats( Joystick );
			JoystickBalls	= SDL_GetNumJoystickBalls( Joystick );
			JoystickButtons	= Min( SDL_GetNumJoystickButtons( Joystick ), (JoystickHats > 0 && !IgnoreHat) ? 12 : 16 );
			JoystickAxes	= Min( SDL_GetNumJoystickAxes( Joystick ), 8 );
			if( JoystickHatNumber < 0 || JoystickHatNumber >= JoystickHats )
				JoystickHatNumber = 0;
			debugf( NAME_Init, TEXT("Joystick: %s, %i buttons, %i axes, %i hats, %i balls"), appFromAnsi(Name), JoystickButtons, JoystickAxes, JoystickHats, JoystickBalls );
		}
	}
	SDL_free( Joysticks );

	unguard;
}

void USDLClient::CloseJoystick()
{
	guard(USDLClient::CloseJoystick);
	if( Gamepad )
		SDL_CloseGamepad( Gamepad );
	else if( Joystick )
		SDL_CloseJoystick( Joystick );
	Gamepad = NULL;
	Joystick = NULL;
	unguard;
}

//
// Reads the desktop size of the display the window is on, or of the primary one before there is a window.
//
void USDLClient::UpdateDesktopSize()
{
	guard(USDLClient::UpdateDesktopSize);
	const SDL_DisplayMode* Desktop = SDL_GetDesktopDisplayMode( Window ? SDL_GetDisplayForWindow( Window ) : SDL_GetPrimaryDisplay() );
	if( !Desktop )
		return;
	DesktopX = Desktop->w;
	DesktopY = Desktop->h;
	if( AutoResolution )
	{
		FullscreenViewportX = DesktopX;
		FullscreenViewportY = DesktopY;
	}
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

	CloseJoystick();
	GClipboardCopy = NULL;
	GClipboardPaste = NULL;

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
	if( UseJoystick && !Joystick )
		OpenJoystick();
	else if( !UseJoystick )
		CloseJoystick();
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

