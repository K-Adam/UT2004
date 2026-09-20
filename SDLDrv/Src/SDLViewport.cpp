/*=============================================================================
	SDLViewport.cpp: USDLViewport code.
	Copyright 2002 Epic Games, Inc. All Rights Reserved.

        SDL website: http://www.libsdl.org/

Revision history:
	* Created by Ryan C. Gordon, based on WinDrv.
      This is an updated rewrite of the original SDLDrv.
=============================================================================*/

#include <stdlib.h>
#ifdef WIN32
#include <io.h>
#else
#include <unistd.h>
#endif
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "SDLDrv.h"

// Duplicated here for convenience.
enum EEditorMode
{
	EM_None 				= 0,	// Gameplay, editor disabled.
	EM_ViewportMove			= 1,	// Move viewport normally.
	EM_ViewportZoom			= 2,	// Move viewport with acceleration.
	EM_ActorRotate			= 5,	// Rotate actor.
	EM_ActorScale			= 8,	// Scale actor.
	EM_TexturePan			= 11,	// Pan textures.
	EM_TextureRotate		= 13,	// Rotate textures.
	EM_TextureScale			= 14,	// Scale textures.
	EM_ActorSnapScale		= 18,	// Actor snap-scale.
	EM_TexView				= 19,	// Viewing textures.
	EM_TexBrowser			= 20,	// Browsing textures.
	EM_StaticMeshBrowser	= 21,	// Browsing static meshes.
	EM_MeshView				= 22,	// Viewing mesh.
	EM_MeshBrowser			= 23,	// Browsing mesh.
	EM_BrushClip			= 24,	// Brush Clipping.
	EM_VertexEdit			= 25,	// Multiple Vertex Editing.
	EM_FaceDrag				= 26,	// Face Dragging.
	EM_Polygon				= 27,	// Free hand polygon drawing
	EM_TerrainEdit			= 28,	// Terrain editing.
	EM_PrefabBrowser		= 29,	// Browsing prefabs.
	EM_Matinee				= 30,	// Movie editing.
	EM_EyeDropper			= 31,	// Eyedropper
	EM_Animation			= 32,	// Viewing Animation
	EM_FindActor			= 33,	// Find Actor
	EM_MaterialEditor		= 34,	// Material editor
	EM_Geometry				= 35,	// Geometry editing mode
	EM_NewCameraMove		= 50,
};


/*-----------------------------------------------------------------------------
	Class implementation.
-----------------------------------------------------------------------------*/

IMPLEMENT_CLASS(USDLViewport);

//
// EInputKey mirrors Windows virtual keys. Printable keys follow the layout like VK codes do,
// the rest follow their physical position.
//
static EInputKey KeyFromSDL( SDL_Keycode Key, SDL_Scancode Scancode )
{
	if( Key >= SDLK_A && Key <= SDLK_Z )
		return (EInputKey)(IK_A + Key - SDLK_A);
	if( Key >= SDLK_0 && Key <= SDLK_9 )
		return (EInputKey)(IK_0 + Key - SDLK_0);
	switch( Key )
	{
		case SDLK_SPACE:		return IK_Space;
		case SDLK_GRAVE:		return IK_Tilde;
		case SDLK_APOSTROPHE:	return IK_SingleQuote;
		case SDLK_SEMICOLON:	return IK_Semicolon;
		case SDLK_COMMA:		return IK_Comma;
		case SDLK_PERIOD:		return IK_Period;
		case SDLK_SLASH:		return IK_Slash;
		case SDLK_BACKSLASH:	return IK_Backslash;
		case SDLK_LEFTBRACKET:	return IK_LeftBracket;
		case SDLK_RIGHTBRACKET:	return IK_RightBracket;
		case SDLK_MINUS:		return IK_Minus;
		case SDLK_EQUALS:		return IK_Equals;
	}
	if( Scancode >= SDL_SCANCODE_F1 && Scancode <= SDL_SCANCODE_F12 )
		return (EInputKey)(IK_F1 + Scancode - SDL_SCANCODE_F1);
	if( Scancode >= SDL_SCANCODE_F13 && Scancode <= SDL_SCANCODE_F24 )
		return (EInputKey)(IK_F13 + Scancode - SDL_SCANCODE_F13);
	if( Scancode >= SDL_SCANCODE_KP_1 && Scancode <= SDL_SCANCODE_KP_9 )
		return (EInputKey)(IK_NumPad1 + Scancode - SDL_SCANCODE_KP_1);
	switch( Scancode )
	{
		case SDL_SCANCODE_BACKSPACE:	return IK_Backspace;
		case SDL_SCANCODE_TAB:			return IK_Tab;
		case SDL_SCANCODE_RETURN:
		case SDL_SCANCODE_KP_ENTER:		return IK_Enter;
		case SDL_SCANCODE_PAUSE:		return IK_Pause;
		case SDL_SCANCODE_ESCAPE:		return IK_Escape;
		case SDL_SCANCODE_DELETE:		return IK_Delete;
		case SDL_SCANCODE_INSERT:		return IK_Insert;
		case SDL_SCANCODE_PRINTSCREEN:	return IK_PrintScrn;
		case SDL_SCANCODE_LSHIFT:
		case SDL_SCANCODE_RSHIFT:		return IK_Shift;
		case SDL_SCANCODE_LCTRL:
		case SDL_SCANCODE_RCTRL:		return IK_Ctrl;
		case SDL_SCANCODE_LALT:
		case SDL_SCANCODE_RALT:			return IK_Alt;
		case SDL_SCANCODE_LGUI:
		case SDL_SCANCODE_RGUI:			return IK_F24;
		// The console key on layouts where it does not type a backquote.
		case SDL_SCANCODE_GRAVE:		return IK_Tilde;
		// Punctuation positions on layouts where they type other characters, like VK_OEM keys.
		case SDL_SCANCODE_APOSTROPHE:	return IK_SingleQuote;
		case SDL_SCANCODE_SEMICOLON:	return IK_Semicolon;
		case SDL_SCANCODE_COMMA:		return IK_Comma;
		case SDL_SCANCODE_PERIOD:		return IK_Period;
		case SDL_SCANCODE_SLASH:		return IK_Slash;
		case SDL_SCANCODE_BACKSLASH:	return IK_Backslash;
		case SDL_SCANCODE_LEFTBRACKET:	return IK_LeftBracket;
		case SDL_SCANCODE_RIGHTBRACKET:	return IK_RightBracket;
		case SDL_SCANCODE_MINUS:		return IK_Minus;
		case SDL_SCANCODE_EQUALS:		return IK_Equals;
		case SDL_SCANCODE_NONUSBACKSLASH:	return IK_UnknownE2;
		case SDL_SCANCODE_HOME:			return IK_Home;
		case SDL_SCANCODE_END:			return IK_End;
		case SDL_SCANCODE_PAGEUP:		return IK_PageUp;
		case SDL_SCANCODE_PAGEDOWN:		return IK_PageDown;
		case SDL_SCANCODE_LEFT:			return IK_Left;
		case SDL_SCANCODE_RIGHT:		return IK_Right;
		case SDL_SCANCODE_UP:			return IK_Up;
		case SDL_SCANCODE_DOWN:			return IK_Down;
		case SDL_SCANCODE_KP_0:			return IK_NumPad0;
		case SDL_SCANCODE_KP_MULTIPLY:	return IK_GreyStar;
		case SDL_SCANCODE_KP_PLUS:		return IK_GreyPlus;
		case SDL_SCANCODE_KP_EQUALS:	return IK_Separator;
		case SDL_SCANCODE_KP_MINUS:		return IK_GreyMinus;
		case SDL_SCANCODE_KP_PERIOD:	return IK_NumPadPeriod;
		case SDL_SCANCODE_KP_DIVIDE:	return IK_GreySlash;
		case SDL_SCANCODE_NUMLOCKCLEAR:	return IK_NumLock;
		case SDL_SCANCODE_CAPSLOCK:		return IK_CapsLock;
		case SDL_SCANCODE_SCROLLLOCK:	return IK_ScrollLock;
		default:						return IK_None;
	}
}

/*-----------------------------------------------------------------------------
	USDLViewport Init/Exit.
-----------------------------------------------------------------------------*/

//
// Constructor.
//
USDLViewport::USDLViewport()
:	UViewport()
{
	guard(USDLViewport::USDLViewport);

	FullscreenOnly = 0;
	TextToSpeechObject = -1;
	ColorBytes = 4;
	LastJoyHat = IK_None;
	debugf( TEXT("Created and initialized a new SDL viewport.") );

	unguard;
}

//
// Destroy.
//
void USDLViewport::Destroy()
{
	guard(USDLViewport::Destroy);
	Super::Destroy();

	if( BlitFlags & BLIT_Temporary )
		appFree( ScreenPointer );

    if (TextToSpeechObject != -1)
    {
        close(TextToSpeechObject);
        TextToSpeechObject = -1;
    }

	unguard;
}

//
// Error shutdown.
//
void USDLViewport::ShutdownAfterError()
{
	SDL_Quit();
	Super::ShutdownAfterError();
}


//
// Fullscreen and grabbed windows read raw relative motion, unless a windowed menu shows the OS cursor.
//
void USDLViewport::UpdateMouseGrabState()
{
	SDL_Window* Window = GetOuterUSDLClient()->Window;
	SDL_SetWindowRelativeMouseMode( Window, (IsFullscreen() || WantGrab) && !WindowsMouseShown );
	MouseIsGrabbed = SDL_GetWindowRelativeMouseMode( Window );
}


/*-----------------------------------------------------------------------------
	Command line.
-----------------------------------------------------------------------------*/

//
// Command line.
//
UBOOL USDLViewport::Exec( const TCHAR* Cmd, FOutputDevice& Ar )
{
	guard(USDLViewport::Exec);
	if( UViewport::Exec( Cmd, Ar ) )
	{
		return 1;
	}
	else if( ParseCommand(&Cmd,TEXT("ENDFULLSCREEN")) )
	{
		if( BlitFlags & BLIT_Fullscreen )
			EndFullscreen();
		return 1;
	}
	else if( ParseCommand(&Cmd,TEXT("TOGGLEFULLSCREEN")) )
	{
		ToggleFullscreen();
		return 1;
	}
	else if( ParseCommand(&Cmd,TEXT("GETCURRENTRES")) )
	{
		Ar.Logf( TEXT("%ix%i"), SizeX, SizeY ); // gam
		return 1;
	}
	else if( ParseCommand(&Cmd,TEXT("GETCURRENTRENDERDEVICE")) )
	{
		Ar.Log( RenDev->GetClass()->GetPathName() );
		return 1;
	}
	else if( ParseCommand(&Cmd,TEXT("SETRES")) )
	{
		SetResolution( Cmd, 1 );
		return 1;
	}
	else if( ParseCommand(&Cmd,TEXT("TEMPSETRES")) )
	{
		SetResolution( Cmd, 0 );
		return 1;
	}

	// gam ---
	else if( ParseCommand(&Cmd,TEXT("SETMOUSE")) )
	{
	    const TCHAR* CmdTemp = Cmd;
		INT X, Y;
		
		X = appAtoi(CmdTemp);
		
		while( appIsDigit( *CmdTemp) )
		    CmdTemp++;

		while( appIsSpace( *CmdTemp) )
		    CmdTemp++;
		    
		Y = appAtoi(CmdTemp);
		
        SDL_WarpMouseInWindow( GetOuterUSDLClient()->Window, X, Y );
        return 1;
	}

	else if( ParseCommand(&Cmd,TEXT("TTS")) )
	{
		if( appStrcmp(Cmd,TEXT("")) != 0 )
			TextToSpeech( FString(Cmd), 1.f );
		return 1;
	}

        // vi style quit commands.
    else if ((ParseCommand(&Cmd,TEXT(":wq"))) || (ParseCommand(&Cmd,TEXT(":q!"))))
    {
        GIsRequestingExit = 1;
        return 1;
    }

	// --- gam
	else return 0;
	unguard;
}

/*-----------------------------------------------------------------------------
	Window openining and closing.
-----------------------------------------------------------------------------*/

//
// Open this viewport's window.
//
void USDLViewport::OpenWindow( PTRINT InParentWindow, UBOOL IsTemporary, INT NewX, INT NewY, INT OpenX, INT OpenY )
{
	guard(USDLViewport::OpenWindow);

	check(Actor);
	UBOOL DoRepaint=0, DoSetActive=0;
	USDLClient* C = GetOuterUSDLClient();

    if (TextToSpeechObject == -1)
    {
        if (C->TextToSpeechFile.Len() == 0)
            debugf(TEXT("TTS: No output filename specified."));
        else
        {
            // Please note we don't create this file...
            TextToSpeechObject = open(appToAnsi(*(C->TextToSpeechFile)), O_WRONLY);
            if (TextToSpeechObject != -1)
                debugf(TEXT("TTS: Opened file \"%s\" for text-to-speech output."), *(C->TextToSpeechFile));
            else
            {
                int err = errno;
                debugf(TEXT("TTS: Couldn't open TTS file \"%s\""), *(C->TextToSpeechFile));
                debugf(TEXT("TTS: System error is \"%s\" (%d)."), appFromAnsi(strerror(err)), err);
                if (err == ENOENT)
                {
                    debugf(TEXT("TTS: (We intentionally don't create this file if it doesn't exist!)"));
                    debugf(TEXT("TTS: Disabling Text-to-speech support..."));
                }
            }
        }
    }

	if( NewX!=INDEX_NONE )
		NewX = Align( NewX, 2 );

	// Create frame buffer.
	if( IsTemporary )
	{
		// Create in-memory data.
		BlitFlags     = BLIT_Temporary;
		ColorBytes    = 2;
		SizeX         = NewX;
		SizeY         = NewY;
		ScreenPointer = (BYTE*)appMalloc( 2 * NewX * NewY, TEXT("TemporaryViewportData") );	
		debugf( NAME_Log, TEXT("Opened temporary viewport") );
   	}

	// Create rendering device.
	if( !RenDev && !appStrcmp( GetName(), TEXT("VisibiltyViewport") ) )
		TryRenderDevice( TEXT("Editor.VisibilityRenderDevice"), NewX, NewY, 0 );
	if( !RenDev && (BlitFlags & BLIT_Temporary) )
		TryRenderDevice( TEXT("NullDrv.NullRenderDevice"), NewX, NewY, 0 );
	if( !RenDev && !GIsEditor && ParseParam(appCmdLine(),TEXT("windowed")) )
		TryRenderDevice( TEXT("ini:Engine.Engine.RenderDevice"), NewX, NewY, 0 );
	if( !RenDev )
		TryRenderDevice( TEXT("ini:Engine.Engine.RenderDevice"), NewX, NewY, GIsEditor ? 0 : C->StartupFullscreen );

	check(RenDev);
	UpdateWindowFrame();

	unguard;
}

//
// Close a viewport window.  Assumes that the viewport has been opened with
// OpenViewportWindow.  Does not affect the viewport's object, only the
// platform-specific information associated with it.
//
void USDLViewport::CloseWindow()
{
	guard(USDLViewport::CloseWindow);
    /* no-op. */
	unguard;
}

/*-----------------------------------------------------------------------------
	USDLViewport operations.
-----------------------------------------------------------------------------*/

//
// Set window position according to menu's on-top setting:
//
void USDLViewport::SetTopness()
{
	guard(USDLViewport::SetTopness);
    /* no-op. */
	unguard;
}

//
// Repaint the viewport.
//
void USDLViewport::Repaint( UBOOL Blit )
{
	guard(USDLViewport::Repaint);
	GetOuterUSDLClient()->Engine->Draw( this, Blit );
	unguard;
}

//
// Return whether fullscreen.
//
UBOOL USDLViewport::IsFullscreen()
{
	guard(USDLViewport::IsFullscreen);
	return (BlitFlags & BLIT_Fullscreen)!=0;
	unguard;
}

void USDLViewport::SetModeCursor()
{
}


void USDLViewport::SetTitleBar()
{
    TCHAR WindowName[80];
	appSnprintf( WindowName, ARRAY_COUNT(WindowName), TEXT("%s"), LocalizeGeneral("Product",appPackage()) );

	SDL_SetWindowTitle( GetOuterUSDLClient()->Window, appToAnsi(WindowName) );
}


//
// Update user viewport interface.
//
void USDLViewport::UpdateWindowFrame()
{
	guard(USDLViewport::UpdateWindowFrame);

	// If not a window, exit.
	if( !GetOuterUSDLClient()->Window || (BlitFlags&BLIT_Fullscreen) || (BlitFlags&BLIT_Temporary) || !Actor)
		return;

    SetTitleBar();

	unguard;
}

//
// The GUI reads the window cursor position when it shows the OS mouse.
//
void USDLViewport::UpdateMousePosition()
{
	float X, Y;
	SDL_GetMouseState( &X, &Y );
	WindowsMouseX = X;
	WindowsMouseY = Y;
}

//
// Return the viewport's window.
//
void* USDLViewport::GetWindow()
{
	guard(USDLViewport::GetWindow);
	SDL_Window* Window = GetOuterUSDLClient()->Window;
#ifdef WIN32
	// Force feedback needs the native window.
	return SDL_GetPointerProperty( SDL_GetWindowProperties(Window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL );
#else
	return Window;
#endif
	unguard;
}

/*-----------------------------------------------------------------------------
	Input.
-----------------------------------------------------------------------------*/

//
// Input event router.
//
UBOOL USDLViewport::CauseInputEvent( INT iKey, EInputAction Action, FLOAT Delta )
{
	guard(USDLViewport::CauseInputEvent);
	return GetOuterUSDLClient()->Engine->InputEvent( this, (EInputKey)iKey, Action, Delta );
	unguard;
}

//
// If the cursor is currently being captured, stop capturing, clipping, and 
// hiding it, and move its position back to where it was when it was initially
// captured.
//
void USDLViewport::SetMouseCapture( UBOOL Capture, UBOOL Clip, UBOOL OnlyFocus )
{
	guard(USDLViewport::SetMouseCapture);
	WantGrab = Capture;
	UpdateMouseGrabState();
	unguard;
}


//
// Update input for this viewport.
//
void USDLViewport::UpdateInput( UBOOL Reset, FLOAT DeltaSeconds )
{
	guard(USDLViewport::UpdateInput);

	// Resets come from SetRes and from inside the event loop below, so they leave the queue alone.
	if( Reset )
		return;

	USDLClient* Client = GetOuterUSDLClient();

    if( Client->UseJoystick && USDLClient::Gamepad )
    {
        // The hat's keys for the d-pad and the default binds' axes for the sticks. Axes span -0.5 to 0.5, like the raw path.
        static const EInputKey Buttons[SDL_GAMEPAD_BUTTON_MISC1 + 1] = { IK_Joy1, IK_Joy2, IK_Joy3, IK_Joy4, IK_Joy5, IK_Joy6, IK_Joy7, IK_Joy8, IK_Joy9, IK_Joy10, IK_Joy11, IK_Joy13, IK_Joy14, IK_Joy15, IK_Joy16, IK_Joy12 };
        static const EInputKey Axes[SDL_GAMEPAD_AXIS_COUNT] = { IK_JoyX, IK_JoyY, IK_JoyV, IK_JoySlider1, IK_JoyZ, IK_JoyR };
        SDL_UpdateGamepads();
        for( INT i=0; i<ARRAY_COUNT(Buttons); i++ )
        {
            UBOOL Pressed = SDL_GetGamepadButton( USDLClient::Gamepad, (SDL_GamepadButton)i );
            if( Pressed != (Input->KeyDown( Buttons[i] ) != 0) )
                CauseInputEvent( Buttons[i], Pressed ? IST_Press : IST_Release );
        }
        for( INT i=0; i<ARRAY_COUNT(Axes); i++ )
            Input->DirectAxis( Axes[i], SDL_GetGamepadAxis( USDLClient::Gamepad, (SDL_GamepadAxis)i ) / 65535.f, DeltaSeconds );
    }
    else if ((Client->UseJoystick) && (USDLClient::Joystick != NULL))
    {
        SDL_UpdateJoysticks();

        int i;
        int max = USDLClient::JoystickButtons;
        for (i = 0; i < max; i++)
        {
            UBOOL Pressed = SDL_GetJoystickButton(USDLClient::Joystick, i);
		    INT Button = IK_Joy1 + i;
			if( !Input->KeyDown( Button ) && Pressed )
				CauseInputEvent( Button, IST_Press );
			else if( Input->KeyDown( Button ) && !Pressed )
				CauseInputEvent( Button, IST_Release );
        }

        static EInputKey axes[] = { IK_JoyX, IK_JoyY, IK_JoyZ, IK_JoyR, IK_JoyU, IK_JoyV, IK_JoySlider1, IK_JoySlider2 };
        check(USDLClient::JoystickAxes <= 8);
        for (i = 0; i < USDLClient::JoystickAxes; i++)
        {
            float pos = SDL_GetJoystickAxis(USDLClient::Joystick, i) / 65535.f;
		    Input->DirectAxis(axes[i], pos, DeltaSeconds);
        }
	}

	// Windowed menus use the OS cursor, so the menu cursor is where the mouse is.
	// UGameEngine::Tick releases and restores the grab for them.
	if( bShowWindowsMouse != WindowsMouseShown )
	{
		WindowsMouseShown = bShowWindowsMouse;
		if( WindowsMouseShown )
			SDL_ShowCursor();
		else
			SDL_HideCursor();
		UpdateMouseGrabState();
	}

	// Text input only while a menu or the console reads it, so an IME does not take game keys.
	bool WantText = (GUIController && GUIController->bActive) || (Console && Console->IsA(UConsole::StaticClass()) && ((UConsole*)Console)->bTyping);
	if( WantText != SDL_TextInputActive( Client->Window ) )
	{
		if( WantText )
			SDL_StartTextInput( Client->Window );
		else
			SDL_StopTextInput( Client->Window );
	}

	// Mouse movement management.
	UBOOL MouseMoved = false;
	UBOOL JoyHatMoved = false;
	UBOOL Resized = false;
	FLOAT DX = 0, DY = 0;
	EInputKey ThisJoyHat = IK_None;
	SDL_JoystickID JoystickID = USDLClient::Joystick ? SDL_GetJoystickID( USDLClient::Joystick ) : 0;

	static const EInputKey MouseButtons[] = { IK_None, IK_LeftMouse, IK_MiddleMouse, IK_RightMouse, IK_Mouse4, IK_Mouse5, IK_Mouse6, IK_Mouse7, IK_Mouse8 };

	SDL_Event Event;
	while( SDL_PollEvent( &Event ) )
	{
		switch( Event.type )
		{
            case SDL_EVENT_QUIT:
                GIsRequestingExit = 1;
                break;

			case SDL_EVENT_KEY_DOWN:
			case SDL_EVENT_KEY_UP:
			{
				UBOOL Down = Event.type == SDL_EVENT_KEY_DOWN;
				if( Down && Event.key.key == SDLK_G && (Event.key.mod & SDL_KMOD_CTRL) )
				{
					if( !Event.key.repeat )
					{
						WantGrab = !WantGrab;
						UpdateMouseGrabState();
					}
					break;
				}
				if( Down && Event.key.key == SDLK_RETURN && (Event.key.mod & SDL_KMOD_ALT) )
				{
					if( !Event.key.repeat )
						ToggleFullscreen();
					break;
				}

				// Repeats press again, like WM_KEYDOWN.
				EInputKey Key = KeyFromSDL( Event.key.key, Event.key.scancode );
				if( Key != IK_None )
					CauseInputEvent( Key, Down ? IST_Press : IST_Release );

				// Text input leaves out the control characters WM_CHAR sends, edit boxes use them for editing and the clipboard.
				// Ctrl+Alt is AltGr, which types text.
				if( Down && (Key == IK_Backspace || Key == IK_Tab || Key == IK_Escape) )
					Client->Engine->Key( this, Key, Key );
				else if( Down && Key >= IK_A && Key <= IK_Z && (Event.key.mod & SDL_KMOD_CTRL) && !(Event.key.mod & SDL_KMOD_ALT) )
					Client->Engine->Key( this, (EInputKey)(Key - IK_A + 1), Key - IK_A + 1 );
				break;
			}

			case SDL_EVENT_TEXT_INPUT:
			{
				const char* Text = Event.text.text;
				while( Uint32 Char = SDL_StepUTF8( &Text, NULL ) )
					if( Char < 0x10000 )
						Client->Engine->Key( this, Char < 256 ? (EInputKey)Char : IK_Unicode, (TCHAR)Char );
				break;
			}

			case SDL_EVENT_MOUSE_BUTTON_DOWN:
			case SDL_EVENT_MOUSE_BUTTON_UP:
				if( Event.button.button < ARRAY_COUNT(MouseButtons) && MouseButtons[Event.button.button] != IK_None )
					CauseInputEvent( MouseButtons[Event.button.button], Event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? IST_Press : IST_Release );
				break;

			case SDL_EVENT_MOUSE_WHEEL:
			{
				// Precise wheels and touchpads send fractions of a notch.
				FLOAT Wheel = Event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -Event.wheel.y : Event.wheel.y;
				if( Wheel * WheelRemainder < 0.f )
					WheelRemainder = 0.f;
				WheelRemainder += Wheel;
				while( Abs(WheelRemainder) >= 1.f )
				{
					EInputKey Key = WheelRemainder > 0.f ? IK_MouseWheelUp : IK_MouseWheelDown;
					CauseInputEvent( Key, IST_Press );
					CauseInputEvent( Key, IST_Release );
					WheelRemainder -= WheelRemainder > 0.f ? 1.f : -1.f;
				}
				break;
			}

			case SDL_EVENT_MOUSE_MOTION:
				if ((!MouseIsGrabbed) && (Client->IgnoreUngrabbedMouse))
					break;

				MouseMoved = true;
				DX += Event.motion.xrel;
				DY += Event.motion.yrel;
				break;

			case SDL_EVENT_JOYSTICK_ADDED:
				if( Client->UseJoystick && !USDLClient::Joystick )
					Client->OpenJoystick();
				break;

			case SDL_EVENT_JOYSTICK_REMOVED:
				if( Event.jdevice.which == JoystickID )
				{
					Client->CloseJoystick();
					JoystickID = 0;
					Input->ResetInput();
				}
				break;

			case SDL_EVENT_JOYSTICK_BALL_MOTION:
				if ( Event.jball.which != JoystickID || !Client->UseJoystick )
	                break;
				MouseMoved = true;
				DX += Event.jball.xrel * Client->ScaleJBX;
				DY += Event.jball.yrel * Client->ScaleJBY;
	                break;

			case SDL_EVENT_JOYSTICK_HAT_MOTION:
				if ( (Event.jhat.which != JoystickID) || USDLClient::Gamepad ||
				     (Event.jhat.hat != Client->JoystickHatNumber) ||
				     (!Client->UseJoystick) ||
				     (Client->IgnoreHat) )
				{
					break;
				}

				JoyHatMoved = true;

				switch ( Event.jhat.value )
				{
					case SDL_HAT_UP :
						ThisJoyHat = IK_Joy13;
						break;
					case SDL_HAT_DOWN :
						ThisJoyHat = IK_Joy14;
						break;
					case SDL_HAT_LEFT :
						ThisJoyHat = IK_Joy15;
						break;
					case SDL_HAT_RIGHT :
						ThisJoyHat = IK_Joy16;
						break;
					default :
						ThisJoyHat = (EInputKey) 0;
						break;
				}
				break;

			case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
				Resized = true;
				break;

			case SDL_EVENT_WINDOW_FOCUS_LOST:
				if( GUIController )
					GUIController->ResetInput();
				if( Input )
					Input->ResetInput();
				break;

			default:;
		}
	}

	// Once after the loop, so a drag resizes once per frame. The window already has the new size.
	if( Resized && RenDev && !(BlitFlags & BLIT_Fullscreen) )
	{
		int W, H;
		SDL_GetWindowSizeInPixels( Client->Window, &W, &H );
		if( W != SizeX || H != SizeY )
			ResizeViewport( BlitFlags | BLIT_NoWindowChange, W, H );
	}

	// Deliver mouse behavior to the engine.
	if ( MouseMoved )
	{
	    WindowsMouseX = Clamp<FLOAT>( WindowsMouseX + DX, 0, SizeX );
	    WindowsMouseY = Clamp<FLOAT>( WindowsMouseY + DY, 0, SizeY );

		// Send to input subsystem.
		if( DX )
			CauseInputEvent( IK_MouseX, IST_Axis, +DX );

		if( DY )
			CauseInputEvent( IK_MouseY, IST_Axis, -DY );

		if (!Client->InMenuLoop)
			Client->Engine->MouseDelta(this, 0, appRound(DX), appRound(DY));

		if (!IsRealtime())
		{
			if( Input->KeyDown(IK_Space) )
            {
				for( INT i=0; i<Client->Viewports.Num(); i++ )
					Client->Viewports(i)->Repaint( 1 );
            }
			else
            {
				Repaint( 1 );
            }
		}
	}

	if ( (LastJoyHat != ThisJoyHat) && JoyHatMoved )
	{
		if (LastJoyHat)
			CauseInputEvent( LastJoyHat, IST_Release );
		if (ThisJoyHat)
			CauseInputEvent( ThisJoyHat, IST_Press );
		LastJoyHat = ThisJoyHat;
	}

	unguard;
}



/*-----------------------------------------------------------------------------
	Lock and Unlock.
-----------------------------------------------------------------------------*/

//
// Lock the viewport window and set the approprite Screen and RealScreen fields
// of Viewport.  Returns 1 if locked successfully, 0 if failed.  Note that a
// lock failing is not a critical error; it's a sign that a DirectDraw mode
// has ended or the user has closed a viewport window.
//
UBOOL USDLViewport::Lock( BYTE* HitData, INT* HitSize )
{
	guard(USDLViewport::Lock);

	// Success here, so pass to superclass.
	return UViewport::Lock(HitData,HitSize);

	unguard;
}

//
// Unlock the viewport window.  If Blit=1, blits the viewport's frame buffer.
//
void USDLViewport::Unlock()
{
	guard(USDLViewport::Unlock);
	UViewport::Unlock();
	unguard;
}

/*-----------------------------------------------------------------------------
	Viewport modes.
-----------------------------------------------------------------------------*/

//
// Try switching to a new rendering device.
//
UBOOL USDLViewport::TryRenderDevice( const TCHAR* ClassName, INT NewX, INT NewY, UBOOL Fullscreen )
{
	guard(USDLViewport::TryRenderDevice);

	// Shut down current rendering device.
	guard(ShutdownOldRenDev);
	if( RenDev && RenDev != GetOuterUSDLClient()->Engine->GRenDev ) 
	{
		RenDev->Exit(this);
		delete RenDev;
		RenDev = NULL;
	}
	unguard;

	// Use appropriate defaults.
	USDLClient* C = GetOuterUSDLClient();
	if( NewX==INDEX_NONE )
		NewX = C->WindowedViewportX;
	if( NewY==INDEX_NONE )
		NewY = C->WindowedViewportY;

	// Find device driver.
	UClass* RenderClass = UObject::StaticLoadClass( URenderDevice::StaticClass(), NULL, ClassName, NULL, 0, NULL );
	if( RenderClass  )
	{
		if( RenderClass == GetOuterUSDLClient()->Engine->GRenDev->GetClass() )
		{
			RenDev = GetOuterUSDLClient()->Engine->GRenDev;
		}
		else
		{
			RenDev = ConstructObject<URenderDevice>( RenderClass, this );
			RenDev->Init();
		}

        // "ColorBytes" should be zero unless a fallback is needed.
		if( !RenDev->SetRes( this, NewX, NewY, Fullscreen | FullscreenOnly, ColorBytes ) )
		{
			debugf( NAME_Log, LocalizeError(TEXT("Failed3D"),TEXT("SDLDrv")) );
			if( RenDev != GetOuterUSDLClient()->Engine->GRenDev ) 
				delete RenDev;
			RenDev = NULL;
		}
	}

  	if( !GIsEditor )
	{
		if ( GUIController )
		{
			GUIController->ResetInput();
			GUIController->ResolutionChanged(SizeX,SizeY);
		}
		if ( Input )
			Input->ResetInput();
    }

    return RenDev != NULL;
	unguard;
}


//
// If in fullscreen mode, end it and return to Windows.
//
void USDLViewport::EndFullscreen()
{
	guard(USDLViewport::EndFullscreen);
	USDLClient* Client = GetOuterUSDLClient();
	debugf(TEXT("EndFullscreen"));
	if( RenDev && (BlitFlags & (BLIT_Direct3D|BLIT_OpenGL)) && !FullscreenOnly )
	{
		RenDev->SetRes( this, Client->WindowedViewportX, Client->WindowedViewportY, 0 );
	}
	else if( RenDev && (BlitFlags & BLIT_OpenGL) )
	{
		RenDev->SetRes( this, INDEX_NONE, INDEX_NONE, FullscreenOnly );
	}
	UpdateWindowFrame();

	if ( GUIController )
	{
		GUIController->ResetInput();
		GUIController->ResolutionChanged(SizeX, SizeY);
	}

	if ( Input )
		Input->ResetInput();

	unguard;
}

//
// Toggle fullscreen.
//
void USDLViewport::ToggleFullscreen()
{
	guard(USDLViewport::ToggleFullscreen);
	if( BlitFlags & BLIT_Fullscreen )
	{
		EndFullscreen();
		SetMouseCapture(1,1,0);
	}
	else if( !(Actor->ShowFlags & SHOW_ChildWindow) )
	{
		debugf(TEXT("AttemptFullscreen"));
		TryRenderDevice( TEXT("ini:Engine.Engine.RenderDevice"), INDEX_NONE, INDEX_NONE, 1 );
	}
	USDLClient* Client = GetOuterUSDLClient();
	Client->StartupFullscreen = IsFullscreen();
	Client->SaveConfig();
	unguard;
}

//
// SETRES WxH[w|f]. Fullscreen always covers the desktop, so the size only applies to the window.
//
void USDLViewport::SetResolution( const TCHAR* Cmd, UBOOL bSaveSize )
{
	guard(USDLViewport::SetResolution);
	INT X = appAtoi(Cmd);
	const TCHAR* Rest = appStrchr(Cmd,'x') ? appStrchr(Cmd,'x')+1 : appStrchr(Cmd,'X') ? appStrchr(Cmd,'X')+1 : TEXT("");
	INT Y = appAtoi(Rest);
	UBOOL Fullscreen = IsFullscreen() || FullscreenOnly;
	if( appStrchr(Rest,'w') || appStrchr(Rest,'W') )
		Fullscreen = 0;
	else if( appStrchr(Rest,'f') || appStrchr(Rest,'F') )
		Fullscreen = 1;
	if( !X || !Y )
		return;
	if( !RenDev->SetRes( this, X, Y, Fullscreen || FullscreenOnly, 0, bSaveSize ) )
		EndFullscreen();
	if( GUIController )
		GUIController->ResolutionChanged(SizeX, SizeY);
	unguard;
}

//
// Resize the viewport.
//
UBOOL USDLViewport::ResizeViewport( DWORD NewBlitFlags, INT InNewX, INT InNewY, UBOOL bSaveSize )
{
	guard(USDLViewport::ResizeViewport);
	USDLClient* Client = GetOuterUSDLClient();

	// Andrew says: I made this keep the BLIT_Temporary flag so the temporary screen buffer doesn't get leaked
	// during light rebuilds.

	// Remember viewport.
	UViewport* SavedViewport = NULL;
	if( Client->Engine->Audio && !GIsEditor && !(GetFlags() & RF_Destroyed) )
		SavedViewport = Client->Engine->Audio->GetViewport();

	// Fullscreen takes the desktop size below.
	INT NewX = InNewX!=INDEX_NONE ? InNewX : Client->WindowedViewportX;
	INT NewY = InNewX!=INDEX_NONE ? InNewY : Client->WindowedViewportY;

	check(NewX>=0);
	check(NewY>=0);

	if( !(NewBlitFlags & BLIT_Temporary) )
	{
		ScreenPointer = NULL;
	}

	// Set new info.
	BlitFlags = NewBlitFlags & ~BLIT_ParameterFlags;

	if( !Client->Window )
	{
		Client->Window = SDL_CreateWindow( "", NewX, NewY, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN );
		if( !Client->Window )
			appErrorf( TEXT("Couldn't create window: %s"), appFromAnsi(SDL_GetError()) );

		// The menus need 640x480, a smaller window makes the GUI switch to a fixed resolution.
		if( !GIsEditor )
			SDL_SetWindowMinimumSize( Client->Window, 640, 480 );

#ifndef WIN32
		// Windows windows get the executable's icon.
		SDL_Surface* Icon = SDL_LoadBMP( "../Help/Unreal.bmp" );
		if( Icon )
		{
			SDL_SetSurfaceColorKey( Icon, true, SDL_MapSurfaceRGB( Icon, 255, 0, 255 ) );
			SDL_SetWindowIcon( Client->Window, Icon );
			SDL_DestroySurface( Icon );
		}
#endif
	}

	// A null render device may have opened the window first.
	if( (NewBlitFlags & BLIT_OpenGL) && !Client->GLContext )
	{
		Client->GLContext = SDL_GL_CreateContext( Client->Window );
		if( !Client->GLContext )
			appErrorf( TEXT("Couldn't create OpenGL context: %s"), appFromAnsi(SDL_GetError()) );
	}

	// User resizing already gave the window its size.
	SDL_Window* Window = Client->Window;
	if( !(NewBlitFlags & BLIT_NoWindowChange) )
	{
		if( NewBlitFlags & BLIT_Fullscreen )
		{
			// Borderless at the desktop size, so switching away is instant.
			SDL_SetWindowFullscreenMode( Window, NULL );
			SDL_SetWindowFullscreen( Window, true );
		}
		else
		{
			SDL_SetWindowFullscreen( Window, false );
			SDL_SetWindowSize( Window, Align(NewX,2), NewY );
			SDL_SetWindowResizable( Window, !GIsEditor );
		}
		SDL_ShowWindow( Window );
		SDL_SyncWindow( Window );

		// The OS may limit the window.
		int W, H;
		SDL_GetWindowSizeInPixels( Window, &W, &H );
		NewX = W;
		NewY = H;
	}
	SizeX = NewX;
	SizeY = NewY;

	SDL_HideCursor();
	WindowsMouseShown = 0;
	bWindowsMouseAvailable = !(NewBlitFlags & BLIT_Fullscreen);

	SetTitleBar();

	// Make this viewport current and update its title bar.
	GetOuterUClient()->MakeCurrent( this );	

	// Update audio.
	if( SavedViewport && SavedViewport!=Client->Engine->Audio->GetViewport() )
		Client->Engine->Audio->SetViewport( SavedViewport );

	// Update the window.
	UpdateWindowFrame();

	// Save info.
	if( RenDev && !GIsEditor && bSaveSize && !(NewBlitFlags & BLIT_Fullscreen) && NewX && NewY )
	{
		Client->WindowedViewportX  = NewX;
		Client->WindowedViewportY  = NewY;
		Client->SaveConfig();
	}

	if( !(NewBlitFlags & BLIT_Fullscreen) )
		WantGrab = Client->CaptureMouse && !bShowWindowsMouse;
	UpdateMouseGrabState();

	return 1;
	unguard;
}

//
// Get localized keyname.
//
TCHAR * USDLViewport::GetLocalizedKeyName( EInputKey Key )
{
	return TEXT("");
}




void USDLViewport::TextToSpeech( const FString& Text, FLOAT Volume )
{
    INT len = Text.Len();
    if (len == 0)
        return;

    if (TextToSpeechObject != -1)
    {
        char *cvt = (char *) appAlloca(len + 2);
        if (cvt)
        {
            appToAnsi(*Text, cvt);
            if (cvt[len-1] != '\n')
            {
                cvt[len++] = '\n';
                cvt[len] = '\0';
            }

            // Could be made non-blocking.
            write(TextToSpeechObject, cvt, len);
        }
    }
}


/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/

