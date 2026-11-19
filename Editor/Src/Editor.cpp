/*=============================================================================
	Editor.cpp: Unreal editor package.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.
=============================================================================*/

#include "EditorPrivate.h"

/*-----------------------------------------------------------------------------
	Globals.
-----------------------------------------------------------------------------*/

// Global variables.
EDITOR_API FGlobalTopicTable GTopics;

// Register things.
#define NAMES_ONLY
#define AUTOGENERATE_NAME(name) EDITOR_API FName EDITOR_##name;
#define AUTOGENERATE_FUNCTION(cls,idx,name) IMPLEMENT_FUNCTION(cls,idx,name)
#include "EditorClasses.h"
#undef AUTOGENERATE_FUNCTION
#undef AUTOGENERATE_NAME
#undef NAMES_ONLY

// sjs --- import natives
#define NATIVES_ONLY
#define NAMES_ONLY
#define AUTOGENERATE_NAME(name)
#define AUTOGENERATE_FUNCTION(cls,idx,name)
#include "EditorClasses.h"
#undef AUTOGENERATE_FUNCTION
#undef AUTOGENERATE_NAME
#undef NATIVES_ONLY
#undef NAMES_ONLY
// --- sjs

// Package implementation.
IMPLEMENT_PACKAGE(Editor);

#if __STATIC_LINK
void autoInitializeRegistrantsEditorContentCommandlets(INT &Lookup);
void autoInitializeRegistrantsEditorTransBuffer(INT &Lookup);
void autoInitializeRegistrantsEditorUnVisi(INT &Lookup);
void autoInitializeRegistrantsEditorUnSrcCom(INT &Lookup);
void autoInitializeRegistrantsEditorTexStrip(INT &Lookup);
void autoInitializeRegistrantsEditorTexLOD(INT &Lookup);
void autoInitializeRegistrantsEditorTexInfo(INT &Lookup);
void autoInitializeRegistrantsEditorSurfType(INT &Lookup);
void autoInitializeRegistrantsEditorStripSource(INT &Lookup);
void autoInitializeRegistrantsEditorMake(INT &Lookup);

void autoInitializeRegistrantsEditor(INT &Lookup)
{
    // This is a hack since we don't have predeclarations for a lot of these
    //  classes, but need to reference them all here.
    autoInitializeRegistrantsEditorContentCommandlets(Lookup);
    autoInitializeRegistrantsEditorTransBuffer(Lookup);
    autoInitializeRegistrantsEditorUnVisi(Lookup);
    autoInitializeRegistrantsEditorUnSrcCom(Lookup);
    autoInitializeRegistrantsEditorTexStrip(Lookup);
    autoInitializeRegistrantsEditorTexLOD(Lookup);
    autoInitializeRegistrantsEditorTexInfo(Lookup);
    autoInitializeRegistrantsEditorSurfType(Lookup);
    autoInitializeRegistrantsEditorStripSource(Lookup);
    autoInitializeRegistrantsEditorMake(Lookup);


	UPolysExporterOBJ::StaticClass();
	ULevelExporterOBJ::StaticClass();
	UStaticMeshExporterT3D::StaticClass();
	UStaticMeshFactory::StaticClass();
	UPrefab::StaticClass();
	UTransactor::StaticClass();
	UAnimNotifyProps::StaticClass();
	UEditorEngine::StaticClass();
	UMaterialFactory::StaticClass();
	GNativeLookupFuncs[Lookup++] = &FindEditorUMaterialFactoryNative;
	UFontFactory::StaticClass();
	UTextureFactory::StaticClass();
	UTextureExporterDDS::StaticClass();
	UTextureExporterUPT::StaticClass();
	UTextureExporterTGA::StaticClass();
	UTextureExporterBMP::StaticClass();
	UTextureExporterPCX::StaticClass();
	UPrefabFactory::StaticClass();
	USoundFactory::StaticClass();
	UModelFactory::StaticClass();
	UPolysFactory::StaticClass();
	ULevelFactory::StaticClass();
	UClassFactoryUC::StaticClass();
	UTextureFactoryNew::StaticClass();
	UClassFactoryNew::StaticClass();
	ULevelFactoryNew::StaticClass();
	UPrefabExporterT3D::StaticClass();
	ULevelExporterSTL::StaticClass();
	ULevelExporterT3D::StaticClass();
	UModelExporterT3D::StaticClass();
	UPolysExporterT3D::StaticClass();
	UClassExporterUC::StaticClass();
	UClassExporterH::StaticClass();
	USoundExporterWAV::StaticClass();
	UTextBufferExporterTXT::StaticClass();
	UBrushBuilder::StaticClass();
	GNativeLookupFuncs[Lookup++] = &FindEditorUBrushBuilderNative;

    #if WIN32 && !UT_UWP
	UTrueTypeFontFactory::StaticClass();
    #endif
}
#endif

/*-----------------------------------------------------------------------------
	The end.
-----------------------------------------------------------------------------*/

