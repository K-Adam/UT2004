/*=============================================================================
	UCC.cpp: Unreal command-line launcher.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.

Revision history:
	* Created by Tim Sweeney.
=============================================================================*/

#if _WIN32
	#pragma pack(push,8)
	#include <windows.h>
	#include <process.h>
	#pragma pack(pop)
#else
	#include <errno.h>
	#include <sys/stat.h>
#endif

#include <stdio.h>

// Core and Engine
#include "Engine.h"

#if __STATIC_LINK
#if __UNIX__
#undef clock
#endif
#include "StaticLink.h"
#endif

INT GFilesOpen, GFilesOpened;

/*-----------------------------------------------------------------------------
	Global variables.
-----------------------------------------------------------------------------*/

// General.
#if _WIN32
	extern "C" {HINSTANCE hInstance;}
#endif
extern "C" {TCHAR GPackage[64]=TEXT("UCC");}

// Log.
#include "FOutputDeviceFile.h"
FOutputDeviceFile Log;

// gam --- Aux log.
FOutputDeviceFile AuxLog;
// --- gam

// Error.
#include "FOutputDeviceAnsiError.h"
FOutputDeviceAnsiError Error;

// Feedback.
#include "FFeedbackContextAnsi.h"
FFeedbackContextAnsi Warn;

// File manager.
#include "FFileManagerStd.h"
FFileManagerStd FileManager;
#include "FMallocAnsi.h"
FMallocAnsi Malloc;

// Config.
#include "FConfigCacheIni.h"


/*-----------------------------------------------------------------------------
	Main.
-----------------------------------------------------------------------------*/

// Unreal command-line applet executor.
FString RightPad( FString In, INT Count )
{
	while( In.Len()<Count )
		In += TEXT(" ");
	return In;
}
static INT Compare( FString& A, FString& B ) // gam
{
	return appStricmp( *A, *B );
}
void ShowBanner( FOutputDevice& Warn )
{
	Warn.Logf( TEXT("=======================================") );
	Warn.Logf( TEXT("ucc.exe: UnrealOS execution environment") );
	Warn.Logf( TEXT("Copyright 2001 Epic Games Inc") );
	Warn.Logf( TEXT("=======================================") );
	Warn.Logf( TEXT("") );
}


int main( int argc, char* argv[] )
{
    #if ((__UNIX__) && (defined UT_DEBUG))
        setvbuf(stdout, NULL, _IONBF, 0);
        setvbuf(stderr, NULL, _IONBF, 0);
    #endif

	INT ErrorLevel = 0;
	GIsStarted     = 1;
#ifndef UT_DEBUG
	try
#endif
	{
		GIsGuarded = 1;

		strcpy( GModule, "ucc" );

		// The executable and the commandlet name go
		TCHAR CmdLine[4096];
		appArgsToCmdLine( argc, argv, 2, CmdLine, ARRAY_COUNT(CmdLine) );

		// Init engine core.
		appInit( TEXT("UT2004"), CmdLine, &Malloc, &Log, &Error, &Warn, &FileManager, FConfigCacheIni::Factory, 1 );

#if __STATIC_LINK
        INT Lookup = 0;
        RegisterEngineNatives(Lookup);
        AUTO_INITIALIZE_REGISTRANTS_FIRE;
        RegisterGameNatives(Lookup);
#endif



		// Get the ucc stuff going.	

	    // Language (gam)
	    TCHAR Temp[256];
	    if( GConfig->GetString( TEXT("Engine.Engine"), TEXT("Language"), Temp, ARRAY_COUNT(Temp) ) )
	    UObject::SetLanguage( Temp );

		FString Token = argc>1 ? appFromAnsi(argv[1]) : TEXT("");
		TArray<FRegistryObjectInfo> List;
		UObject::GetRegistryObjects( List, UClass::StaticClass(), UCommandlet::StaticClass(), 0 );
		GIsClient = GIsServer = GIsEditor = GIsScriptable = GIsUCC = 1;
		GLazyLoad = 0;
		UBOOL Help = 0;
		DWORD LoadFlags = LOAD_NoWarn | LOAD_Quiet;

		if( Token==TEXT("") )
		{
			ShowBanner( Warn );
			Warn.Logf( TEXT("Use \"ucc help\" for help") );
		}
		else if( Token==TEXT("HELP") )
		{
			ShowBanner( Warn );
			verify(UObject::StaticLoadClass( UCommandlet::StaticClass(), NULL, TEXT("Core.Commandlet"), NULL, LOAD_NoFail, NULL )==UCommandlet::StaticClass());
			const TCHAR* Tmp = appCmdLine();
			GIsEditor = 0; // To enable loading localized text.
			if( !ParseToken( Tmp, Token, 0 ) )
			{
				INT i;
				Warn.Logf( TEXT("Usage:") );
				Warn.Logf( TEXT("   ucc <command> <parameters>") );
				Warn.Logf( TEXT("") );
				Warn.Logf( TEXT("Commands for \"ucc\":") );
				TArray<FString> Items;
				for( i=0; i<List.Num(); i++ )
				{
					UClass* Class = UObject::StaticLoadClass( UCommandlet::StaticClass(), NULL, *List(i).Object, NULL, LoadFlags, NULL );
					if( Class )
					{
						UCommandlet* Default = (UCommandlet*)Class->GetDefaultObject();
						if( Default->HelpCmd != TEXT("") )
							new(Items)FString( FString(TEXT("   ucc ")) + RightPad(Default->HelpCmd,21) + TEXT(" ") + Default->HelpOneLiner );
					}
				}
				new(Items)FString( TEXT("   ucc help <command>        Get help on a command") );
				Sort( Items.GetTypedData(), Items.Num() );
				for( i=0; i<Items.Num(); i++ )
					Warn.Log( Items(i) );
			}
			else
			{
				Help = 1;
				goto Process;
			}
		}
		else
		{
			// Look it up.
			if( Token==TEXT("Make") )
				LoadFlags |= LOAD_DisallowFiles;
		Process:
			UClass* Class = UObject::StaticLoadClass( UCommandlet::StaticClass(), NULL, *Token, NULL, LoadFlags, NULL );
			if( !Class )
				Class = UObject::StaticLoadClass( UCommandlet::StaticClass(), NULL, *(Token+TEXT("Commandlet")), NULL, LoadFlags, NULL );
			if( !Class )
			{
				INT i;
				for( i=0; i<List.Num(); i++ )
				{
					FString Str = List(i).Object;
					while( Str.InStr(TEXT("."))>=0 )
						Str = Str.Mid(Str.InStr(TEXT("."))+1);
					if( Token==Str || Token+TEXT("Commandlet")==Str )
						break;
				}
				if( i<List.Num() )
					Class = UObject::StaticLoadClass( UCommandlet::StaticClass(), NULL, *List(i).Object, NULL, LoadFlags, NULL );
			}
			if( Class )
			{
				UCommandlet* Default = (UCommandlet*)Class->GetDefaultObject();
				if( Help )
				{
					// Get help on it.
					if( Default->HelpUsage!=TEXT("") )
					{
						Warn.Logf( TEXT("Usage:") );
						Warn.Logf( TEXT("   ucc %s"), *Default->HelpUsage );
					}
					if( Default->HelpParm[0]!=TEXT("") )
					{
						Warn.Logf( TEXT("") );
						Warn.Logf( TEXT("Parameters:") );
						for( INT i=0; i<ARRAY_COUNT(Default->HelpParm) && Default->HelpParm[i]!=TEXT(""); i++ )
							Warn.Logf( TEXT("   %s %s"), *RightPad(Default->HelpParm[i],16), *Default->HelpDesc[i] );
					}
					if( Default->HelpWebLink!=TEXT("") )
					{
						Warn.Logf( TEXT("") );
						Warn.Logf( TEXT("For more info, see") );
						Warn.Logf( TEXT("   %s"), *Default->HelpWebLink );
					}
				}
				else
				{
					// Run it.
					if( Default->LogToStdout )
					{
						Warn.AuxOut = GLog;
						GLog        = &Warn;
					}
                    else
                    {
                        // gam ---
                        appStrcpy( AuxLog.Filename, TEXT("StdOut.log") );
                        AuxLog.bLogEvent = 0;
                        Warn.AuxOut = &AuxLog;
                        // --- gam
                    }
					if( Default->ShowBanner )
					{
						ShowBanner( Warn );
					}
					debugf( TEXT("Executing %s"), Class->GetFullName() );
					GIsClient = Default->IsClient;
					GIsServer = Default->IsServer;
					GIsEditor = Default->IsEditor;
					GLazyLoad = Default->LazyLoad;
					UCommandlet* Commandlet = ConstructObject<UCommandlet>( Class );
					// Commandlets collect garbage in Main and are read after it.
					Commandlet->AddToRoot();
					Commandlet->InitExecution();
					Commandlet->ParseParms( appCmdLine() );
					Commandlet->Main( appCmdLine() );
					if( Commandlet->ShowErrorCount )
                    {
                        // gam ---
                        if ( Warn.ErrorCount == 0)
						    GWarn->Logf( TEXT("Success - %d error(s), %d warning(s)"), Warn.ErrorCount, Warn.WarningCount );
                        else
                        {
						    GWarn->Logf( TEXT("Failure - %d error(s), %d warning(s)"), Warn.ErrorCount, Warn.WarningCount );
                            ErrorLevel = 1;
                        }
                        // --- gam
                    }
					if( Default->LogToStdout )
					{
						Warn.AuxOut = NULL;
						GLog        = &Log;
					}
				}
			}
			else
			{
				ShowBanner( Warn );
				Warn.Logf( TEXT("Commandlet %s not found"), *Token );
			}
		}
		appPreExit();
		GIsGuarded = 0;
	}
#ifndef UT_DEBUG
	catch( ... )
	{
		// Crashed.
		ErrorLevel = 1;
		GIsGuarded = 0;
		Error.HandleError();
	}
#endif
	appExit();
	GIsStarted = 0;
	return ErrorLevel;
}

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/

