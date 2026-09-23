/*=============================================================================
	SDLDrv.h: Simple Directmedia Layer cross-platform driver.
	Copyright 2002 Epic Games, Inc. All Rights Reserved.

        SDL website: http://www.libsdl.org/

Revision history:
	* Created by Ryan C. Gordon, based on WinDrv.
      This is an updated rewrite of the original SDLDrv.
=============================================================================*/

#ifndef _INC_SDLDRV
#define _INC_SDLDRV

/*----------------------------------------------------------------------------
	API.
----------------------------------------------------------------------------*/

#ifndef SDLDRV_API
	#define SDLDRV_API DLL_IMPORT
#endif

/*----------------------------------------------------------------------------
	Dependencies.
----------------------------------------------------------------------------*/

// SDL3.dll uses default packing, the engine builds with /Zp4.
#pragma pack(push,8)
#include <SDL3/SDL.h>
#pragma pack(pop)

// Unreal includes.
#include "Engine.h"
#include "UnRender.h"

/*-----------------------------------------------------------------------------
	Declarations.
-----------------------------------------------------------------------------*/

// Classes.
class USDLViewport;
class USDLClient;

/*-----------------------------------------------------------------------------
	USDLClient.
-----------------------------------------------------------------------------*/

//
// SDL implementation of the client.
//
class USDLClient : public UClient, public FNotifyHook
{
	DECLARE_CLASS(USDLClient,UClient,CLASS_Transient|CLASS_Config,SDLDrv)

	// Configuration.
	BITFIELD			UseJoystick;
	BITFIELD			StartupFullscreen;
	INT                 JoystickNumber;
	UBOOL               IgnoreUngrabbedMouse;
	FStringNoInit       TextToSpeechFile;
	FLOAT               PadDeadZone;
	FLOAT               PadLookExponent;
	FLOAT               PadTurnBoost;
	UBOOL               PadGyro;
	FLOAT               PadGyroSensitivity;

	// Variables.
	UBOOL				InMenuLoop;
	INT					NormalMouseInfo[3];
	INT					CaptureMouseInfo[3];

	// One window and GL context, they survive resolution and fullscreen changes.
	SDL_Window*			Window;
	SDL_GLContext		GLContext;

    static SDL_Gamepad *Gamepad;
	// The attached SDL virtual joystick behind the on-screen controls.
	static SDL_JoystickID VirtualPad;

	// Constructors.
	USDLClient();
	void StaticConstructor();

	// FNotifyHook interface.
	void NotifyDestroy( void* Src );

	// UObject interface.
	void Destroy();
	void PostEditChange();
	void ShutdownAfterError();

	// UClient interface.
	void Init( UEngine* InEngine );
	void ShowViewportWindows( DWORD ShowFlags, INT DoShow );
	void EnableViewportWindows( DWORD ShowFlags, INT DoEnable );
	UBOOL Exec( const TCHAR* Cmd, FOutputDevice& Ar=*GLog );
	void Tick();
	void MakeCurrent( UViewport* InViewport );
	UViewport* GetLastCurrent();
	class UViewport* NewViewport( const FName Name );
    void TeardownSR();

	void OpenGamepad();
	void CloseGamepad();
	void EnableGyro();
	void SetVirtualJoystick( UBOOL On );
};

/*-----------------------------------------------------------------------------
	USDLViewport.
-----------------------------------------------------------------------------*/

enum { VJ_Move, VJ_Look, VJ_Fire, VJ_AltFire, VJ_Jump, VJ_Count };

//
// An SDL viewport.
//
class USDLViewport : public UViewport
{
	DECLARE_CLASS(USDLViewport,UViewport,CLASS_Transient,SDLDrv)
	DECLARE_WITHIN(USDLClient)

	// Variables.
	DWORD				BlitFlags;

	// One bit per SDL button, then LT, RT and the four left stick directions.
	DWORD				PadHeld;
	// How long the look stick has been at full deflection, for PadTurnBoost.
	FLOAT				PadEdgeTime;

	// The touch and finger holding each on-screen control, a zero finger is free. The mouse is touch 0, finger 1.
	SDL_TouchID			VJTouch[VJ_Count];
	SDL_FingerID		VJFinger[VJ_Count];
	UBOOL				VJShown;

	UBOOL				WantGrab;
	FLOAT				WheelRemainder;
	UBOOL				MouseIsGrabbed;
	UBOOL				WindowsMouseShown;
	INT					TextToSpeechObject;  // file descriptor for named pipe.

	// Constructor.
	USDLViewport();

	// UObject interface.
	void Destroy();
	void ShutdownAfterError();

	// UViewport interface.
	UBOOL Lock( BYTE* HitData=NULL, INT* HitSize=0 );
	UBOOL Exec( const TCHAR* Cmd, FOutputDevice& Ar );
	UBOOL ResizeViewport( DWORD BlitFlags, INT NewX=INDEX_NONE, INT NewY=INDEX_NONE, UBOOL bSaveSize=true );
	UBOOL IsFullscreen();
	void Unlock();
	void Repaint( UBOOL Blit );
	void SetModeCursor();
	void UpdateWindowFrame();
	void OpenWindow( PTRINT ParentWindow, UBOOL Temporary, INT NewX, INT NewY, INT OpenX, INT OpenY );
	void CloseWindow();
	void UpdateInput( UBOOL Reset, FLOAT DeltaSeconds );
	void* GetWindow();
	void SetMouseCapture( UBOOL Capture, UBOOL Clip, UBOOL FocusOnly );
	void UpdateMousePosition();
	void RenderOverlay();

	// USDLViewport interface.
	void UpdateMouseGrabState();
	void ToggleFullscreen();
	void SetResolution( const TCHAR* Cmd, UBOOL bSaveSize );
	void EndFullscreen();
	void SetTopness();
	DWORD GetViewportButtonFlags( DWORD wParam );

	UBOOL CauseInputEvent( INT iKey, EInputAction Action, FLOAT Delta=0.0 );

	void VJLayout( INT i, FLOAT& X, FLOAT& Y, FLOAT& R );
	UBOOL VJPointer( SDL_TouchID Touch, SDL_FingerID Finger, Uint32 Phase, FLOAT X, FLOAT Y );
	void VJWrite( INT i, UBOOL Held, FLOAT X, FLOAT Y );
	void VJRelease();

	virtual UBOOL TryRenderDevice( const TCHAR* ClassName, INT NewX, INT NewY, UBOOL Fullscreen );
    void SetTitleBar();

	TCHAR* GetLocalizedKeyName( EInputKey Key );
	void TextToSpeech( const FString& Text, FLOAT Volume );
};


#define AUTO_INITIALIZE_REGISTRANTS_SDLDRV \
	USDLViewport::StaticClass(); \
	USDLClient::StaticClass();

#endif //_INC_SDLDRV
/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/

