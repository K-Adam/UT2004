//=============================================================================
// xDataObject.cpp
// Copyright 2001 Digital Extremes - All Rights Reserved.
// Confidential.
// Capps ripped out all of the Manifest checks, since we weren't using them
// to find savegames.
//=============================================================================

#include "EnginePrivate.h"

static UPackage* CheckPackageLoaded(const FString& packageName, UBOOL bCreateIfNotFound=0, UManifest* manifest=NULL);

static void RemoveFileExt(const FString& fileName, TCHAR* noExtFileName)
{
    TCHAR *End;
    appStrncpy(noExtFileName, *fileName, 256);
    End = appStrchr(noExtFileName,'.');
    if (End)
        *End++ = 0;
}

static UPackage* CheckPackageLoaded(const FString& packageName, UBOOL bCreateIfNotFound, UManifest* manifest)
{
	guard(CheckPackageLoaded);
	// Script names the package, and the name becomes a path under ..\Saves.
	if( !appIsBareFileName(*packageName) )
		return NULL;

    // remove the file extension
    TCHAR Temp[256];
    RemoveFileExt(packageName, Temp);

    UPackage* pkg = FindObject<UPackage>(ANY_PACKAGE, Temp);

    if (!pkg)
    {
        pkg = (UPackage*) UObject::LoadPackage(pkg, *packageName, LOAD_NoWarn | LOAD_Quiet);
    }

    if (!pkg && bCreateIfNotFound)
    {
		// Create package.
		pkg = UObject::CreatePackage(NULL, *packageName);
        UObject::ResetLoaders(pkg, 0, 1);

        // Add new package to the manifest
        if (pkg && manifest)
            manifest->AddEntry(packageName);
    }
	// mh -- make sure the manifest is cleaned up
	if (!pkg && !bCreateIfNotFound)
	{		
		if (manifest)
		{
			INT mi = manifest->FindEntry(packageName);
			if (mi >= 0) manifest->RemoveEntry(mi);
		}
	}

    return pkg;
	unguard;
}

// Load/Create manifest class
static UManifest* CreateManifest()
{
    UManifest* manifest = Cast<UManifest>(UObject::StaticConstructObject(UManifest::StaticClass(), UObject::GetTransientPackage(), NAME_None, RF_Public | RF_Standalone, NULL, GError));

    FString fileName;
    fileName = TEXT("..") PATH_SEPARATOR TEXT("Saves") PATH_SEPARATOR;
    fileName += TEXT("*.uvx");

    TArray<FString> saveGameFiles = GFileManager->FindFiles(*fileName, 1, 0);
    TCHAR Temp[256];
 
    for (INT i=0; i<saveGameFiles.Num(); i++)
    {
        RemoveFileExt(saveGameFiles(i), Temp);
        manifest->AddEntry(FString(Temp));
    }

    return manifest;
}

void AGameInfo::execGetSavedGames( FFrame& Stack, RESULT_DECL )
{
	guard(AGameInfo::execGetSavedGames);
	P_FINISH;

    if (!SaveGameManifest)
        SaveGameManifest = CreateManifest();

    *(UManifest**) Result = SaveGameManifest;

    unguard;
}

void AGameInfo::execCreateDataObject( FFrame& Stack, RESULT_DECL )
{
	guard(AGameInfo::execCreateDataObject);
    P_GET_OBJECT(UClass,objClass);
    P_GET_STR(objName);
    P_GET_STR(packageName);
	P_FINISH;

    checkSlow( objName.Len() );
    checkSlow( packageName.Len() );

    if (!SaveGameManifest)
        SaveGameManifest = CreateManifest();

    UPackage* pkg = CheckPackageLoaded(packageName, 1, SaveGameManifest);
    UObject* obj = pkg ? StaticConstructObject(objClass, pkg, *objName, RF_Public | RF_Standalone, NULL, GError) : NULL;

    if (obj)
    {
        obj->ScriptInit(Level);
    }

    *(UObject**) Result = obj;

    unguard;
}

void AGameInfo::execDeleteDataObject( FFrame& Stack, RESULT_DECL )
{
	guard(AGameInfo::execDeleteDataObject);
    P_GET_OBJECT(UClass,objClass);
    P_GET_STR(objName);
    P_GET_STR(packageName);
	P_FINISH;

    checkSlow( objName.Len() );
    checkSlow( packageName.Len() );

	UPackage* pkg = CheckPackageLoaded(packageName, 0, SaveGameManifest);

    if (!pkg)
    {
        *(DWORD*) Result = 0; 
        return;
    }

    UObject* obj = FindObject<UObject>(pkg, *objName);

    if (obj && obj->IsA(objClass))
    {
        delete obj;
        *(DWORD*) Result = 1;
    }
    else
    {
        *(DWORD*) Result = 0; 
    }

    unguard;
}

void AGameInfo::execLoadDataObject( FFrame& Stack, RESULT_DECL )
{
	guard(AGameInfo::execLoadDataObject);
    P_GET_OBJECT(UClass,objClass);
    P_GET_STR(objName);
    P_GET_STR(packageName);
	P_FINISH;

    checkSlow( objName.Len() );
    checkSlow( packageName.Len() );

	UPackage* pkg = CheckPackageLoaded(packageName, 0, SaveGameManifest);

    if (!pkg)
    {
        *(UObject**) Result = NULL;
        return;
    }

    UObject *obj = FindObject<UObject>(pkg, *objName);

    if (obj && obj->IsA(objClass))
    {
        obj->ScriptInit(Level);
    }
    else
    {
        obj = NULL;
    }

    *(UObject**) Result = obj;

    unguard;
}

void AGameInfo::execAllDataObjects( FFrame& Stack, RESULT_DECL )
{
	guard(AGameInfo::execAllDataObjects);
	P_GET_OBJECT(UClass,objClass);
    P_GET_OBJECT_OUT(UObject, obj, OutClass);
    P_GET_STR(packageName);
	P_FINISH;
	objClass = GOutClass( objClass, OutClass );

    checkSlow( packageName.Len() );

    UPackage* pkg = CheckPackageLoaded(packageName, 0, SaveGameManifest);

    if (!pkg)
    {
        return;
    }

    TObjectIterator<UObject> It;

	PRE_ITERATOR;
		// Fetch next object in the iteration.
		*obj = NULL;
        while (It && *obj==NULL)
        {
            if (It->IsIn(pkg) && It->IsA(objClass))
            {
                *obj = *It;
            }
            ++It;
        }
		if( *obj == NULL )
		{
			Stack.Code = &Stack.Node->Script(wEndOffset + 1);
			break;
		}
	POST_ITERATOR;

    unguard;
}

void AGameInfo::execSavePackage( FFrame& Stack, RESULT_DECL )
{
	guard(AGameInfo::execSavePackage);
    P_GET_STR(packageName);
	P_FINISH;

    *(DWORD*) Result = 0; 

    checkSlow( packageName.Len() );

    UPackage* pkg = CheckPackageLoaded(packageName, 0, SaveGameManifest);

	if (!pkg) {
        return;
	}

    // null the levelinfo var for actors
    for (TObjectIterator<AActor> It; It; ++It)
    {
        if (It->IsIn(pkg))
        {
            It->Level = NULL;
        }
    }

    GFileManager->MakeDirectory( TEXT("..") PATH_SEPARATOR TEXT("Saves") );

	FString fileName;
	fileName = TEXT("..") PATH_SEPARATOR TEXT("Saves") PATH_SEPARATOR;
	fileName += packageName + TEXT(".uvx");
 
    *(DWORD*) Result = SavePackage(pkg, NULL, RF_Standalone, *fileName, GWarn, NULL);

    // restore levelinfo
    for (TObjectIterator<AActor> It; It; ++It)
    {
        if (It->IsIn(pkg))
        {
            It->Level = XLevel->GetLevelInfo();
        }
    }

    unguard;
}

// Only allow deletion of created packages
void AGameInfo::execDeletePackage( FFrame& Stack, RESULT_DECL )
{
	guard(AGameInfo::execDeletePackage);
    P_GET_STR(packageName);
	P_FINISH;

    checkSlow( packageName.Len() );

    *(DWORD*) Result = 0;
	if( !appIsBareFileName(*packageName) )
		return;

	UBOOL success = true;

	FString fileName;

	fileName = TEXT("..") PATH_SEPARATOR TEXT("Saves") PATH_SEPARATOR;
	fileName += packageName + TEXT(".uvx");

    success = DeletePackage(*fileName);
	if (!success) {
        return;
	}

    *(DWORD*) Result = 1;

    unguard;
}

