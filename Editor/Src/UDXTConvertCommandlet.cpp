/*=============================================================================
	UDXTConvertCommandlet.cpp: DXT3/5 -> DXT1/RGBA conversion (KYRO I/II).
	Copyright 2000 Epic Games, Inc. All Rights Reserved.

Revision history:
	* Created by Daniel Vogel.
=============================================================================*/

#include "EditorPrivate.h"
#include <stdlib.h>

/*-----------------------------------------------------------------------------
	UDXTConvertCommandlet
-----------------------------------------------------------------------------*/

class UDXTConvertCommandlet : public UCommandlet
{
	DECLARE_CLASS(UDXTConvertCommandlet,UCommandlet,CLASS_Transient,Editor);
	void StaticConstructor()
	{
		guard(UDXTConvertCommandlet::StaticConstructor);

		LogToStdout     = 0;
		IsClient        = 1;
		IsEditor        = 1;
		IsServer        = 1;
		LazyLoad        = 1;
		ShowErrorCount  = 1;

		unguard;
	}
	INT Main( const TCHAR* Parms )
	{
		guard(UDXTConvertCommandlet::Main);
		FString WildCard;
		if( !ParseToken(Parms,WildCard,0) )
			appErrorf(TEXT("Package search mask not specified."));
		UClass* TexClass = FindObjectChecked<UClass>( ANY_PACKAGE, TEXT("Texture") );
		TArray<FString> FilesFound = GFileManager->FindFiles( *WildCard, 1, 0 );

		GLazyLoad = 0;
		for (INT i=0; i<FilesFound.Num(); i++)
		{
			FString Pkg = FilesFound(i);
			GWarn->Logf( TEXT("Package %s..."), *Pkg );
			GWarn->Logf( TEXT("  Loading") );

			UObject* Package = NULL;

			Package = LoadPackage(NULL,*Pkg,LOAD_NoWarn);

			if (Package != NULL)
			{
				GWarn->Logf( TEXT("  Converting") );
				for( TObjectIterator<UObject> It; It; ++It )
				{
					if( It->IsA(TexClass) && It->IsIn(Package) )
					{
						UTexture* Texture	= (UTexture*) *It;
						Texture->ConvertDXT();
					}
				}

				GWarn->Logf( TEXT("  Saving") );
				
				if (appStrstr(*Pkg, TEXT(".ut2")))
				{
					// Save the updated map
					GEditor->Exec( *FString::Printf(TEXT("MAP SAVE FILE=\"%s\""), *Pkg ) );
				}
				else
					SavePackage( Package, NULL, RF_Standalone, *Pkg, GError, NULL );
				UObject::CollectGarbage(RF_Native);
			}
		}

		GIsRequestingExit=1;
		return 0;
		unguard;
	}
};
IMPLEMENT_CLASS(UDXTConvertCommandlet)

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/

