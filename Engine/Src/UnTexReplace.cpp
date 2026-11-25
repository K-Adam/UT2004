/*=============================================================================
	UnTexReplace.cpp: Sharper textures drawn in place of retail ones.
=============================================================================*/

#include "EnginePrivate.h"
#include "UnFoliagePrivate.h"

#define HDTEXTURES_DIR	TEXT("../HDTextures/")
#define HDTEXTURES_INI	TEXT("../HDTextures/hdtextures.ini")

struct FTextureReplacement
{
	FString				Path,
						File;
	FFoliageTexture*	Texture;
	UBOOL				Tried;
};

FTexture* FindTextureReplacement(UTexture* Texture)
{
	guard(FindTextureReplacement);

	static TMap<FName,FTextureReplacement>* Replacements = NULL;
	if( !Replacements )
	{
		Replacements = new TMap<FName,FTextureReplacement>;
		if( TMultiMap<FString,FString>* Section = GConfig->GetSectionPrivate(TEXT("Textures"),0,1,HDTEXTURES_INI) )
			for( TMultiMap<FString,FString>::TIterator It(*Section); It; ++It )
			{
				// A name hash rejects nearly every texture before GetPathName is built.
				INT Dot = It.Key().InStr(TEXT("."),1);
				FTextureReplacement Entry;
				Entry.Path		= It.Key();
				Entry.File		= FString(HDTEXTURES_DIR) + It.Value();
				Entry.Texture	= NULL;
				Entry.Tried		= 0;
				Replacements->Set(FName(*It.Key().Mid(Dot + 1)),Entry);
			}
	}

	FTextureReplacement* Entry = Replacements->Find(Texture->GetFName());
	// The path check keeps a texture of the same name in another package retail.
	if( !Entry || Entry->Path != Texture->GetPathName() )
		return NULL;
	if( !Entry->Tried )
	{
		Entry->Tried = 1;
		FFoliageTexture* Loaded = new FFoliageTexture;
		if( Loaded->Load(*Entry->File) )
		{
			Entry->Texture = Loaded;
			debugf(TEXT("HDTextures: %s draws %s"),*Entry->Path,*Entry->File);
		}
		else
		{
			debugf(NAME_Warning,TEXT("HDTextures: %s is missing or not a DXT DDS file"),*Entry->File);
			delete Loaded;
		}
	}
	return Entry->Texture;

	unguard;
}
