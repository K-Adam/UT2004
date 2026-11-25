/*=============================================================================
	UnFoliage.cpp: Replacement plants drawn in place of retail foliage meshes.
=============================================================================*/

#include "EnginePrivate.h"
#include "UnFoliage.h"
#include "UnFoliagePrivate.h"

#define FOLIAGE_LODS	3

/*-----------------------------------------------------------------------------
	Resources.
-----------------------------------------------------------------------------*/

// The light baked onto a terrain's heightmap samples. Filtered, it is what the terrain shows between them.
class FFoliageLightTexture : public FTexture
{
public:
	TArray<FColor>	Colors;
	INT				Width,
					Height;
	QWORD			CacheId;

	FFoliageLightTexture(ATerrainInfo* Terrain) : Width(Terrain->HeightmapX), Height(Terrain->HeightmapY), CacheId(MakeCacheID(CID_RenderTexture))
	{
		Colors.Add(Width * Height);
		appMemcpy(Colors.GetTypedData(),Terrain->VertexColors.GetTypedData(),Colors.Num() * sizeof(FColor));
	}

	virtual QWORD GetCacheId() { return CacheId; }
	virtual INT GetRevision() { return 1; }
	virtual INT GetWidth() { return Width; }
	virtual INT GetHeight() { return Height; }
	virtual INT GetFirstMip() { return 0; }
	virtual INT GetNumMips() { return 1; }
	virtual ETextureFormat GetFormat() { return TEXF_RGBA8; }
	virtual ETexClampMode GetUClamp() { return TC_Clamp; }
	virtual ETexClampMode GetVClamp() { return TC_Clamp; }
	virtual void* GetRawTextureData(INT MipIndex) { return Colors.GetTypedData(); }
	virtual void UnloadRawTextureData(INT MipIndex) {}
	virtual void GetTextureData(INT MipIndex,void* Dest,INT DestStride,ETextureFormat DestFormat,UBOOL ColoredMips)
	{
		for( INT Y=0; DestFormat == TEXF_RGBA8 && Y<Height; Y++ )
			appMemcpy((BYTE*)Dest + Y * DestStride,&Colors(Y * Width),Width * sizeof(FColor));
	}
	virtual UTexture* GetUTexture() { return NULL; }
};

struct FFoliageMaterial
{
	FFoliageTexture			Texture;
	INT						AlphaRef;
	UProxyBitmapMaterial*	Bitmap;
	UFinalBlend*			Blend;

	FFoliageMaterial() : AlphaRef(127), Bitmap(NULL), Blend(NULL) {}

	// Objects of no package. The engine clears its scratch objects at every garbage collection, so they are made again on demand.
	UMaterial* Get()
	{
		if( !Bitmap || !Blend )
		{
			Bitmap = ConstructObject<UProxyBitmapMaterial>(UProxyBitmapMaterial::StaticClass());
			GGlobalTempObjects->AddGlobalObject((UObject**)&Bitmap);
			Bitmap->SetTextureInterface(&Texture);
			Blend = ConstructObject<UFinalBlend>(UFinalBlend::StaticClass());
			GGlobalTempObjects->AddGlobalObject((UObject**)&Blend);
			Blend->Material				= Bitmap;
			// Blended without depth like the retail leaves, so the device can soften the cut outline and
			// write the depth of the solid part itself.
			Blend->FrameBufferBlending	= FB_AlphaBlend;
			Blend->ZWrite				= 0;
			Blend->ZTest				= 1;
			Blend->AlphaTest			= 1;
			Blend->TwoSided				= 1;
			Blend->AlphaRef				= AlphaRef;
		}
		return Blend;
	}
};

struct FFoliageMesh
{
	FStaticMeshVertexStream	Vertices;
	FStaticMeshUVStream		UVs;
	FRawColorStream			Colors;		// How open to the sky each vertex is, dark at the root and inside the plant.
	FRawIndexBuffer			Indices;
	FLOAT					Radius,		// Of the footprint on the ground.
							Height;

	UBOOL Load(const TCHAR* Filename)
	{
		// header: magic, version, vertex count, index count, bounds. vertex: position, normal, uv, colour. index: 16 bit.
		enum { HEADER = 40, VERTEX = 36 };
		TArray<BYTE> File;
		if( !appLoadFileToArray(File,Filename) || File.Num() < HEADER || appMemcmp(File.GetTypedData(),"PHFM",4) != 0 || *(INT*)&File(4) != 2 )
			return 0;
		INT NumVertices = *(INT*)&File(8), NumIndices = *(INT*)&File(12);
		if( NumVertices <= 0 || NumVertices > 65535 || NumIndices <= 0 || File.Num() != HEADER + NumVertices * VERTEX + NumIndices * 2 )
			return 0;
		FLOAT* Bounds = (FLOAT*)&File(16);
		Radius = Max(Max(-Bounds[0],Bounds[3]),Max(-Bounds[1],Bounds[4]));
		Height = Max(Bounds[5],1.f);
		Vertices.Vertices.Add(NumVertices);
		UVs.UVs.Add(NumVertices);
		UVs.CoordinateIndex = 0;
		Colors.Colors.Add(NumVertices);
		for( INT i=0; i<NumVertices; i++ )
		{
			FLOAT* Source = (FLOAT*)&File(HEADER + i * VERTEX);
			Vertices.Vertices(i).Position	= FVector(Source[0],Source[1],Source[2]);
			Vertices.Vertices(i).Normal		= FVector(Source[3],Source[4],Source[5]);
			UVs.UVs(i).U					= Source[6];
			UVs.UVs(i).V					= Source[7];
			Colors.Colors(i)				= *(FColor*)&Source[8];
		}
		Indices.Indices.Add(NumIndices);
		appMemcpy(Indices.Indices.GetTypedData(),&File(HEADER + NumVertices * VERTEX),NumIndices * sizeof(_WORD));
		for( INT i=0; i<NumIndices; i++ )
			if( Indices.Indices(i) >= NumVertices )
				return 0;
		Vertices.Revision = UVs.Revision = Colors.Revision = Indices.Revision = 1;
		return 1;
	}
};

struct FFoliageVariant
{
	FFoliageMesh*			LODs[FOLIAGE_LODS];
	INT						NumLODs;
	FLOAT					Weight;
	UBOOL					Narrow;		// Small enough for ground too broken to carry the wide ones.
	FFoliageInstanceStream	Placed;		// This frame's hand placed plants.
	// This frame's decorations of the terrain being drawn, by detail level and by whether its light texture lights them.
	FFoliageInstanceStream	Merged[FOLIAGE_LODS][2];
};

struct FFoliageSet
{
	TArray<FFoliageVariant*>	Variants;
	FFoliageMaterial*			Material;
	FVector						Tint;			// Brings a photographed plant to the colours of the map's own.
	FLOAT						LODDistance[FOLIAGE_LODS - 1],
								FitWidth,		// The retail footprint or height the meshes were sized for, a mapped
								FitHeight,		// mesh of another size scales them by the ratio. 0 leaves that axis out.
								Scale,
								ScaleJitter,
								TintJitter,
								TerrainAlign,	// How far a plant leans with the ground, 1 follows it fully.
								Sink,			// How deep the base goes under the ground.
								MaxRelief,		// Ground rougher than this under a wide plant gets a narrow one.
								Bend,			// How far the wind leans the top of the plant, in radians.
								FadeScale,		// A plant bigger than the retail one is missed from farther away.
								Tallest;
};

// What one decoration layer scatters over one terrain sector.
struct FFoliageGroup
{
	FVector									Origin;
	FLOAT									Radius;
	TArray<FFoliageInstanceStream*>			Streams;	// One per variant of the set, NULL without instances.

	~FFoliageGroup()
	{
		for( INT i=0; i<Streams.Num(); i++ )
			delete Streams(i);
	}
};

struct FFoliageTerrain
{
	TArray<FFoliageGroup*>	Groups;		// Layer * NumSectors + Sector.
	INT						NumSectors;
	FFoliageLightTexture*	Light;
	FMatrix					WorldToLight;
};

// A layer whose plants show the terrain's own light takes it from the light texture, point by point.
static UBOOL UsesTerrainLight(const FDecorationLayer& Layer)
{
	return !Layer.DisregardTerrainLighting && !Layer.LitDirectional && !Layer.ColorMap;
}

struct FFoliageMeshEntry
{
	FName	Name,
			Outer;
	INT		Set;	// INDEX_NONE for a mesh no plant stands in for.
	FVector	Pivot;	// Bottom centre of the retail mesh, where the plant's own origin goes.
	FLOAT	Scale;	// Brings the set's plants to the size of this retail mesh.
};

struct FFoliageLibrary
{
	TArray<FFoliageSet*>					Sets;
	TMap<FString,INT>						Replace;	// Retail mesh path to set.
	TMap<QWORD,FFoliageMeshEntry>			Meshes;		// By mesh address, checked against the names since addresses come back.
	TMap<QWORD,FFoliageTerrain*>			Terrains;
	FLOAT									WindYaw;		// In degrees, for a level that shows no wind of its own.
	ULevel*									WindLevel;		// The level WindDirection was found for, checked against the name like Meshes.
	FName									WindLevelName;
	FVector									WindDirection;
	FLOAT									WindStrength;
	UBOOL									TreesSway;		// The level moves the shadows of its trees, so its mapper meant them to stand in wind.
};

/*-----------------------------------------------------------------------------
	Loading.
-----------------------------------------------------------------------------*/

static FFoliageLibrary* LoadLibrary()
{
	guard(FFoliage::LoadLibrary);

	TArray<FString> Sections;
	if( !GConfig->GetSectionNames(Sections,FOLIAGE_INI) || !Sections.Num() )
	{
		debugf(TEXT("Foliage: %s not found, plants stay retail"),FOLIAGE_INI);
		return NULL;
	}

	FFoliageLibrary*				Library = new FFoliageLibrary;
	TMap<FString,FFoliageMaterial*>	Materials;
	TMap<FString,FFoliageVariant*>	Variants;
	TMap<FString,INT>				SetNames;
	Library->WindYaw	= 0.f;
	Library->WindLevel	= NULL;
	Library->WindStrength = 1.f;
	GConfig->GetFloat(TEXT("Wind"),TEXT("Direction"),Library->WindYaw,FOLIAGE_INI);
	GConfig->GetFloat(TEXT("Wind"),TEXT("Strength"),Library->WindStrength,FOLIAGE_INI);

	for( INT i=0; i<Sections.Num(); i++ )
	{
		const TCHAR* Section = *Sections(i);
		if( Sections(i).Left(9) == TEXT("Material.") )
		{
			FFoliageMaterial* Material = new FFoliageMaterial;
			if( !Material->Texture.Load(*(FString(FOLIAGE_DIR) + GConfig->GetStr(Section,TEXT("Albedo"),FOLIAGE_INI))) )
			{
				debugf(NAME_Warning,TEXT("Foliage: %s has no usable texture"),Section);
				delete Material;
				continue;
			}
			GConfig->GetInt(Section,TEXT("AlphaRef"),Material->AlphaRef,FOLIAGE_INI);
			Materials.Set(*Sections(i).Mid(9),Material);
		}
		else if( Sections(i).Left(5) == TEXT("Mesh.") )
		{
			FFoliageVariant* Variant = new FFoliageVariant;
			Variant->NumLODs = 0;
			for( INT LOD=0; LOD<FOLIAGE_LODS; LOD++ )
			{
				FString File;
				if( !GConfig->GetString(Section,*FString::Printf(TEXT("LOD%i"),LOD),File,FOLIAGE_INI) )
					break;
				FFoliageMesh* Mesh = new FFoliageMesh;
				if( !Mesh->Load(*(FString(FOLIAGE_DIR) + File)) )
				{
					debugf(NAME_Warning,TEXT("Foliage: %s is not a usable mesh"),*File);
					delete Mesh;
					break;
				}
				Variant->LODs[Variant->NumLODs++] = Mesh;
			}
			Variant->Placed.Dynamic = 1;
			if( !Variant->NumLODs )
			{
				delete Variant;
				continue;
			}
			Variants.Set(*Sections(i).Mid(5),Variant);
		}
	}

	for( INT i=0; i<Sections.Num(); i++ )
	{
		const TCHAR* Section = *Sections(i);
		if( Sections(i).Left(4) != TEXT("Set.") )
			continue;
		FFoliageSet* Set = new FFoliageSet;
		Set->LODDistance[0]	= 1500.f;
		Set->LODDistance[1]	= 3500.f;
		Set->ScaleJitter	= Set->TintJitter = Set->Sink = 0.f;
		Set->Scale			= 1.f;
		Set->FitWidth		= Set->FitHeight = 0.f;
		GConfig->GetFloat(Section,TEXT("FitWidth"),Set->FitWidth,FOLIAGE_INI);
		GConfig->GetFloat(Section,TEXT("FitHeight"),Set->FitHeight,FOLIAGE_INI);
		Set->Tint			= FVector(1,1,1);
		GConfig->GetFloat(Section,TEXT("Scale"),Set->Scale,FOLIAGE_INI);
		GConfig->GetFloat(Section,TEXT("TintR"),Set->Tint.X,FOLIAGE_INI);
		GConfig->GetFloat(Section,TEXT("TintG"),Set->Tint.Y,FOLIAGE_INI);
		GConfig->GetFloat(Section,TEXT("TintB"),Set->Tint.Z,FOLIAGE_INI);
		Set->TerrainAlign	= 1.f;
		Set->MaxRelief		= 1e9f;
		GConfig->GetFloat(Section,TEXT("LODDistance0"),Set->LODDistance[0],FOLIAGE_INI);
		GConfig->GetFloat(Section,TEXT("LODDistance1"),Set->LODDistance[1],FOLIAGE_INI);
		GConfig->GetFloat(Section,TEXT("ScaleJitter"),Set->ScaleJitter,FOLIAGE_INI);
		GConfig->GetFloat(Section,TEXT("TintJitter"),Set->TintJitter,FOLIAGE_INI);
		GConfig->GetFloat(Section,TEXT("TerrainAlign"),Set->TerrainAlign,FOLIAGE_INI);
		GConfig->GetFloat(Section,TEXT("Sink"),Set->Sink,FOLIAGE_INI);
		GConfig->GetFloat(Section,TEXT("MaxRelief"),Set->MaxRelief,FOLIAGE_INI);
		Set->Bend			= 0.3f;
		GConfig->GetFloat(Section,TEXT("Bend"),Set->Bend,FOLIAGE_INI);
		Set->Bend			*= Library->WindStrength;
		Set->FadeScale		= 1.f;
		GConfig->GetFloat(Section,TEXT("FadeScale"),Set->FadeScale,FOLIAGE_INI);
		Set->Tallest		= 0.f;

		// Variant=<mesh>:<weight>
		TArray<FString>	Entries;
		FLOAT			Widest = 0.f;
		GConfig->GetSectionPrivate(Section,0,1,FOLIAGE_INI)->MultiFind(TEXT("Variant"),Entries);
		for( INT e=Entries.Num()-1; e>=0; e-- )
		{
			INT					Colon = Entries(e).InStr(TEXT(":"));
			FString				Name = Colon >= 0 ? Entries(e).Left(Colon) : Entries(e);
			FFoliageVariant*	Variant = Variants.FindRef(Name);
			if( !Variant )
			{
				debugf(NAME_Warning,TEXT("Foliage: %s names the unknown or already used mesh %s"),Section,*Name);
				continue;
			}
			// A variant belongs to one set, which draws its hand placed plants with the set's material.
			Variants.Remove(*Name);
			Variant->Weight = Colon >= 0 ? Max(appAtof(*Entries(e).Mid(Colon + 1)),0.01f) : 1.f;
			Set->Variants.AddItem(Variant);
			Widest = Max(Widest,Variant->LODs[0]->Radius);
			Set->Tallest = Max(Set->Tallest,Variant->LODs[0]->Height);
		}
		for( INT v=0; v<Set->Variants.Num(); v++ )
			Set->Variants(v)->Narrow = Set->Variants(v)->LODs[0]->Radius < 0.7f * Widest;
		FFoliageMaterial** Material = Materials.Find(GConfig->GetStr(Section,TEXT("Material"),FOLIAGE_INI));
		if( !Set->Variants.Num() || !Material )
		{
			debugf(NAME_Warning,TEXT("Foliage: %s has no usable variant or material"),Section);
			delete Set;
			continue;
		}
		Set->Material = *Material;
		SetNames.Set(*Sections(i).Mid(4),Library->Sets.AddItem(Set));
	}

	TMultiMap<FString,FString>* Replace = GConfig->GetSectionPrivate(TEXT("Replace"),0,1,FOLIAGE_INI);
	if( Replace )
	{
		for( TMultiMap<FString,FString>::TIterator It(*Replace); It; ++It )
		{
			INT* Set = SetNames.Find(It.Value());
			if( Set )
				Library->Replace.Set(*It.Key(),*Set);
			else
				debugf(NAME_Warning,TEXT("Foliage: %s names the unknown set %s"),*It.Key(),*It.Value());
		}
	}
	GConfig->UnloadFile(FOLIAGE_INI);
	debugf(TEXT("Foliage: %i sets stand in for %i meshes"),Library->Sets.Num(),Library->Replace.Num());
	return Library->Replace.Num() ? Library : NULL;

	unguard;
}

// Loaded by the first frame that draws with the option on.
static FFoliageLibrary* GLibrary = NULL;

static FFoliageLibrary* GetEnabledLibrary(FLevelSceneNode* SceneNode)
{
	if( !FoliageEnabled(SceneNode) )
		return NULL;
	static UBOOL Tried = 0;
	if( !Tried )
	{
		Tried		= 1;
		GLibrary	= LoadLibrary();
	}
	return GLibrary;
}

static FFoliageMeshEntry& FindMesh(FFoliageLibrary* Library,UStaticMesh* StaticMesh)
{
	FFoliageMeshEntry* Entry = Library->Meshes.Find((QWORD)(PTRINT)StaticMesh);
	if( Entry && Entry->Name == StaticMesh->GetFName() && Entry->Outer == StaticMesh->GetOuter()->GetFName() )
		return *Entry;
	FFoliageMeshEntry New;
	INT* Set	= Library->Replace.Find(StaticMesh->GetPathName());
	New.Name	= StaticMesh->GetFName();
	New.Outer	= StaticMesh->GetOuter()->GetFName();
	New.Set		= Set ? *Set : INDEX_NONE;
	FVector Size = StaticMesh->BoundingBox.Max - StaticMesh->BoundingBox.Min;
	New.Pivot	= FVector((StaticMesh->BoundingBox.Min.X + StaticMesh->BoundingBox.Max.X) * 0.5f,(StaticMesh->BoundingBox.Min.Y + StaticMesh->BoundingBox.Max.Y) * 0.5f,StaticMesh->BoundingBox.Min.Z);
	New.Scale	= 1.f;
	if( Set && Library->Sets(*Set)->FitWidth > 0.f )
		New.Scale = Max(Size.X,Size.Y) / Library->Sets(*Set)->FitWidth;
	else if( Set && Library->Sets(*Set)->FitHeight > 0.f )
		New.Scale = Size.Z / Library->Sets(*Set)->FitHeight;
	return Library->Meshes.Set((QWORD)(PTRINT)StaticMesh,New);
}

/*-----------------------------------------------------------------------------
	Variety and placement.
-----------------------------------------------------------------------------*/

static INT PickVariant(FFoliageSet* Set,FLOAT Random,UBOOL NarrowOnly)
{
	FLOAT Total = 0.f;
	for( INT i=0; i<Set->Variants.Num(); i++ )
		if( !NarrowOnly || Set->Variants(i)->Narrow )
			Total += Set->Variants(i)->Weight;
	if( Total <= 0.f )
		return NarrowOnly ? PickVariant(Set,Random,0) : 0;
	Random *= Total;
	INT Last = 0;
	for( INT i=0; i<Set->Variants.Num(); i++ )
	{
		if( NarrowOnly && !Set->Variants(i)->Narrow )
			continue;
		Last = i;
		Random -= Set->Variants(i)->Weight;
		if( Random < 0.f )
			break;
	}
	return Last;
}

static FColor Tint(FColor Color,const FVector& Factor)
{
	return FColor(Clamp(appRound(Color.R * Factor.X),0,255),Clamp(appRound(Color.G * Factor.Y),0,255),Clamp(appRound(Color.B * Factor.Z),0,255),255);
}

// A turn around the plant's own up axis, then the tilt that brings that axis onto Up.
static FQuat UprightRotation(const FVector& Up,FLOAT Yaw)
{
	FLOAT Length = appSqrt(2.f + 2.f * Up.Z);
	return FQuat(-Up.Y / Length,Up.X / Length,0.f,(1.f + Up.Z) / Length) * FQuat(0.f,0.f,appSin(Yaw * 0.5f),appCos(Yaw * 0.5f));
}

// From the three unit axes a matrix turns X, Y and Z into.
static void RotationFromAxes(const FVector& X,const FVector& Y,const FVector& Z,FLOAT* Out)
{
	FLOAT Trace = X.X + Y.Y + Z.Z;
	if( Trace > 0.f )
	{
		FLOAT S = appSqrt(Trace + 1.f) * 2.f;
		Out[0] = (Y.Z - Z.Y) / S; Out[1] = (Z.X - X.Z) / S; Out[2] = (X.Y - Y.X) / S; Out[3] = S * 0.25f;
	}
	else if( X.X > Y.Y && X.X > Z.Z )
	{
		FLOAT S = appSqrt(1.f + X.X - Y.Y - Z.Z) * 2.f;
		Out[0] = S * 0.25f; Out[1] = (Y.X + X.Y) / S; Out[2] = (Z.X + X.Z) / S; Out[3] = (Y.Z - Z.Y) / S;
	}
	else if( Y.Y > Z.Z )
	{
		FLOAT S = appSqrt(1.f + Y.Y - X.X - Z.Z) * 2.f;
		Out[0] = (Y.X + X.Y) / S; Out[1] = S * 0.25f; Out[2] = (Z.Y + Y.Z) / S; Out[3] = (Z.X - X.Z) / S;
	}
	else
	{
		FLOAT S = appSqrt(1.f + Z.Z - X.X - Y.Y) * 2.f;
		Out[0] = (Z.X + X.Z) / S; Out[1] = (Z.Y + Y.Z) / S; Out[2] = S * 0.25f; Out[3] = (X.Y - Y.X) / S;
	}
}

// Where a plant of a footprint stands on the drawn terrain: its base, its up axis and how uneven the ground is under it.
static UBOOL FitToTerrain(ATerrainInfo* Terrain,UBOOL Smooth,const FVector& Centre,FLOAT Radius,FLOAT Align,FVector& Base,FVector& Up,FLOAT& Relief)
{
	if( !Terrain->SurfacePoint(Centre,Smooth,Base) )
		return 0;
	static const FLOAT Rim[4][2] = { {1,0}, {-1,0}, {0,1}, {0,-1} };
	FVector	Points[4];
	UBOOL	Hit[4];
	for( INT i=0; i<4; i++ )
		Hit[i] = Terrain->SurfacePoint(Centre + FVector(Rim[i][0],Rim[i][1],0.f) * Radius,Smooth,Points[i]);

	FVector Normal(0,0,1);
	if( Hit[0] && Hit[1] && Hit[2] && Hit[3] )
	{
		Normal = ((Points[0] - Points[1]) ^ (Points[2] - Points[3])).SafeNormal();
		if( Normal.Z < 0.f )
			Normal *= -1.f;
		if( Normal.Z < 0.2f )
			Normal = FVector(0,0,1);
	}
	Up = (FVector(0,0,1) + (Normal - FVector(0,0,1)) * Align).SafeNormal();

	// The lowest of the samples along the up axis, so no side of the base hangs in the air.
	FLOAT Lowest = 0.f;
	Relief = 0.f;
	for( INT i=0; i<4; i++ )
	{
		if( !Hit[i] )
			continue;
		Lowest = Min(Lowest,(Points[i] - Base) | Up);
		Relief = Max(Relief,Abs((Points[i] - Base) | Normal));
	}
	Base += Up * Lowest;
	return 1;
}

/*-----------------------------------------------------------------------------
	Wind.
-----------------------------------------------------------------------------*/

// Keeps the fastest drift a material's panners give a surface whose position changes by PerU and PerV over one
// texture repeat. A panner adds to the coordinates, so the picture moves against them.
static void TakeFlow(UMaterial* Material,const FVector& PerU,const FVector& PerV,FVector& Fastest,INT Depth = 0)
{
	if( !Material || Depth > 8 )
		return;
	UTexPanner*	Panner = Cast<UTexPanner>(Material);
	UShader*	Shader = Cast<UShader>(Material);
	UCombiner*	Combiner = Cast<UCombiner>(Material);
	UModifier*	Modifier = Cast<UModifier>(Material);
	// A triggered panner stands still until something in the game starts it.
	if( Panner && !Panner->IsA(UTexPannerTriggered::StaticClass()) )
	{
		FVector	Rate = Panner->PanDirection.Vector() * Panner->PanRate,
				Flow = -(PerU * Rate.X + PerV * Rate.Y);
		if( Flow.SizeSquared2D() > Fastest.SizeSquared2D() )
			Fastest = Flow;
	}
	if( Shader )
	{
		TakeFlow(Shader->Diffuse,PerU,PerV,Fastest,Depth + 1);
		TakeFlow(Shader->Opacity,PerU,PerV,Fastest,Depth + 1);
		TakeFlow(Shader->SelfIllumination,PerU,PerV,Fastest,Depth + 1);
	}
	if( Combiner )
	{
		TakeFlow(Combiner->Material1,PerU,PerV,Fastest,Depth + 1);
		TakeFlow(Combiner->Material2,PerU,PerV,Fastest,Depth + 1);
		TakeFlow(Combiner->Mask,PerU,PerV,Fastest,Depth + 1);
	}
	if( Modifier )
		TakeFlow(Modifier->Material,PerU,PerV,Fastest,Depth + 1);
}

static UBOOL Oscillates(UMaterial* Material,INT Depth = 0)
{
	if( !Material || Depth > 8 )
		return 0;
	UShader*	Shader = Cast<UShader>(Material);
	UCombiner*	Combiner = Cast<UCombiner>(Material);
	UModifier*	Modifier = Cast<UModifier>(Material);
	return Material->IsA(UTexOscillator::StaticClass())
		|| (Shader && (Oscillates(Shader->Diffuse,Depth + 1) || Oscillates(Shader->Opacity,Depth + 1) || Oscillates(Shader->SelfIllumination,Depth + 1)))
		|| (Combiner && (Oscillates(Combiner->Material1,Depth + 1) || Oscillates(Combiner->Material2,Depth + 1) || Oscillates(Combiner->Mask,Depth + 1)))
		|| (Modifier && Oscillates(Modifier->Material,Depth + 1));
}

// Where the clouds of a sky drift, in the sky's own space. Zero for a sky that stands still.
static FVector SkyFlow(ULevel* Level,ASkyZoneInfo* Sky)
{
	FVector	Fastest(0,0,0);
	UModel*	Model = Level->Model;
	for( INT n=0; n<Model->Nodes.Num(); n++ )
	{
		const FBspNode& Node = Model->Nodes(n);
		if( Node.iZone[1] != Sky->Region.ZoneNumber || !Model->Surfs.IsValidIndex(Node.iSurf) )
			continue;
		const FBspSurf& Surf = Model->Surfs(Node.iSurf);
		if( !Surf.Material || !Surf.Material->MaterialUSize() || !Surf.Material->MaterialVSize() )
			continue;
		FVector	U = Model->Vectors(Surf.vTextureU),
				V = Model->Vectors(Surf.vTextureV);
		if( U.SizeSquared() > 0.f && V.SizeSquared() > 0.f )
			TakeFlow(Surf.Material,U * (Surf.Material->MaterialUSize() / U.SizeSquared()),V * (Surf.Material->MaterialVSize() / V.SizeSquared()),Fastest);
	}

	for( INT a=0; a<Level->Actors.Num(); a++ )
	{
		AActor*			Actor = Level->Actors(a);
		UStaticMesh*	Mesh = Actor && Actor->DrawType == DT_StaticMesh && !Actor->bHidden && Actor->Region.Zone == Sky ? Actor->StaticMesh : NULL;
		if( !Mesh || !Mesh->UVStreams.Num() || Mesh->UVStreams(0).UVs.Num() != Mesh->VertexStream.Vertices.Num() )
			continue;
		FMatrix LocalToWorld = Actor->LocalToWorld();
		for( INT s=0; s<Mesh->Sections.Num(); s++ )
		{
			const FStaticMeshSection&	Section = Mesh->Sections(s);
			UMaterial*					Material = Mesh->GetSkin(Actor,s);
			// The mean over the triangles, which is the one direction of a flat mapping and next to nothing for
			// a mapping that wraps around, where the picture has no single way to go.
			FVector	PerU(0,0,0),
					PerV(0,0,0);
			INT		Count = 0,
					Step = Section.IsStrip ? 1 : 3;
			for( INT t=0; t<Section.NumPrimitives; t++ )
			{
				INT First = Section.FirstIndex + t * Step;
				if( First + 2 >= Mesh->IndexBuffer.Indices.Num() )
					break;
				INT		I[3] = { Mesh->IndexBuffer.Indices(First), Mesh->IndexBuffer.Indices(First + 1), Mesh->IndexBuffer.Indices(First + 2) };
				if( !Mesh->VertexStream.Vertices.IsValidIndex(Max(I[0],Max(I[1],I[2]))) )
					continue;
				FVector	E1 = Mesh->VertexStream.Vertices(I[1]).Position - Mesh->VertexStream.Vertices(I[0]).Position,
						E2 = Mesh->VertexStream.Vertices(I[2]).Position - Mesh->VertexStream.Vertices(I[0]).Position;
				FLOAT	U1 = Mesh->UVStreams(0).UVs(I[1]).U - Mesh->UVStreams(0).UVs(I[0]).U,
						V1 = Mesh->UVStreams(0).UVs(I[1]).V - Mesh->UVStreams(0).UVs(I[0]).V,
						U2 = Mesh->UVStreams(0).UVs(I[2]).U - Mesh->UVStreams(0).UVs(I[0]).U,
						V2 = Mesh->UVStreams(0).UVs(I[2]).V - Mesh->UVStreams(0).UVs(I[0]).V,
						Det = U1 * V2 - U2 * V1;
				if( Abs(Det) < 1e-9f )
					continue;
				PerU += (E1 * V2 - E2 * V1) / Det;
				PerV += (E2 * U1 - E1 * U2) / Det;
				Count++;
			}
			if( Count )
				TakeFlow(Material,LocalToWorld.TransformNormal(PerU / Count),LocalToWorld.TransformNormal(PerV / Count),Fastest);
		}
	}
	return Fastest;
}

// The way the level's wind blows as a flat unit vector: the drift of its clouds, or else of its smoke where
// that agrees with itself, or else the direction of the ini.
static FVector FindWind(FFoliageLibrary* Library,ULevel* Level)
{
	guard(FFoliage::FindWind);

	FVector Clouds(0,0,0);
	for( INT a=0; a<Level->Actors.Num(); a++ )
	{
		AZoneInfo* Zone = Cast<AZoneInfo>(Level->Actors(a));
		if( !Zone || !Zone->SkyZone )
			continue;
		// A sky is drawn turned against its zone actor's rotation.
		FVector Flow = FInverseRotationMatrix(Zone->SkyZone->Rotation).TransformNormal(SkyFlow(Level,Zone->SkyZone));
		// Mostly up or down is a picture sliding along a wall of the sky, not a drift over the land.
		if( Flow.SizeSquared2D() > Clouds.SizeSquared2D() && Flow.SizeSquared2D() > Square(Flow.Z) )
			Clouds = Flow;
	}
	if( Clouds.SizeSquared2D() > 0.f )
	{
		Clouds = FVector(Clouds.X,Clouds.Y,0.f).SafeNormal();
		debugf(TEXT("Foliage: wind follows the sky, %i degrees"),appRound(appAtan2(Clouds.Y,Clouds.X) * 180.f / PI));
		return Clouds;
	}

	FVector	Smoke(0,0,0);
	INT		Count = 0;
	for( INT a=0; a<Level->Actors.Num(); a++ )
	{
		AEmitter* Emitter = Cast<AEmitter>(Level->Actors(a));
		for( INT e=0; Emitter && e<Emitter->Emitters.Num(); e++ )
		{
			UParticleEmitter* Particles = Emitter->Emitters(e);
			if( !Particles || Particles->Disabled )
				continue;
			FVector Drift = Particles->Acceleration;
			if( Drift.SizeSquared2D() < 1.f )
			{
				Drift = Particles->StartVelocityRange.GetCenter();
				if( Particles->UseRotationFrom == PTRS_Actor )
					Drift = FRotationMatrix(Emitter->Rotation).TransformNormal(Drift);
			}
			if( Drift.SizeSquared2D() < 1.f )
				continue;
			Smoke += FVector(Drift.X,Drift.Y,0.f).SafeNormal();
			Count++;
		}
	}
	if( Count >= 3 && Smoke.Size2D() > 0.7f * Count )
	{
		Smoke = Smoke.SafeNormal();
		debugf(TEXT("Foliage: wind follows %i particle emitters, %i degrees"),Count,appRound(appAtan2(Smoke.Y,Smoke.X) * 180.f / PI));
		return Smoke;
	}

	debugf(TEXT("Foliage: the level shows no wind, %i degrees from the ini"),appRound(Library->WindYaw));
	return FVector(appCos(Library->WindYaw * PI / 180.f),appSin(Library->WindYaw * PI / 180.f),0.f);

	unguard;
}

static const FVector& LevelWind(FFoliageLibrary* Library,ULevel* Level)
{
	if( Library->WindLevel != Level || Library->WindLevelName != Level->GetOuter()->GetFName() )
	{
		Library->WindLevel		= Level;
		Library->WindLevelName	= Level->GetOuter()->GetFName();
		Library->WindDirection	= FindWind(Library,Level);
		Library->TreesSway		= 0;
		for( INT a=0; !Library->TreesSway && a<Level->Actors.Num(); a++ )
		{
			AProjector* Projector = Cast<AProjector>(Level->Actors(a));
			Library->TreesSway = Projector && Oscillates(Projector->ProjTexture);
		}
		if( Library->TreesSway )
			debugf(TEXT("Foliage: the level sways the shadows of its trees, so the trees sway"));
	}
	return Library->WindDirection;
}

FVector FoliageTreeWind(FLevelSceneNode* SceneNode)
{
	FFoliageLibrary* Library = GetEnabledLibrary(SceneNode);
	if( !Library )
		return FVector(0,0,0);
	FVector Wind = LevelWind(Library,SceneNode->Level);
	return Library->TreesSway ? Wind * Library->WindStrength : FVector(0,0,0);
}

/*-----------------------------------------------------------------------------
	Drawing.
-----------------------------------------------------------------------------*/

// X start, Y end, Z and W the bands the level dissolves out and in over.
static FPlane LODRange(FFoliageSet* Set,FFoliageVariant* Variant,INT LOD,FLevelSceneNode* SceneNode)
{
	// A narrow view magnifies, so detail has to last longer.
	FLOAT	FOVBias = Max(SceneNode->Viewport->Actor->FOVBias,0.1f);
	UBOOL	Last = LOD == Variant->NumLODs - 1;
	// A handover dissolves over a band that both of its levels take from the distance they share.
	FLOAT	Start = LOD ? Set->LODDistance[LOD - 1] / FOVBias : 0.f,
			End = Last ? 1e30f : Set->LODDistance[LOD] / FOVBias;
	return FPlane(Start,End,End * 0.2f + 1.f,Start * 0.2f + 1.f);
}

static UBOOL LODReaches(const FPlane& Range,FLOAT Near,FLOAT Far)
{
	return Far >= Range.X - Range.W && Near < Range.Y;
}

// The shader leaves out the instances outside the level's range.
static UBOOL DrawLOD(FRenderInterface* RI,FLevelSceneNode* SceneNode,FFoliageSet* Set,FFoliageVariant* Variant,INT LOD,const FPlane& Range,FFoliageInstanceStream* Instances,const FVector& WindDirection,const FMatrix* WorldToLight)
{
	// The first level's height for all of them, so a plant bends the same on both sides of a handover.
	FPlane			Wind(WindDirection.X * Set->Bend,WindDirection.Y * Set->Bend,SceneNode->Viewport->Actor->Level->TimeSeconds,1.f / Variant->LODs[0]->Height);
	FFoliageMesh*	Mesh = Variant->LODs[LOD];
	FVertexStream*	Streams[3] = { &Mesh->Vertices, &Mesh->Colors, &Mesh->UVs };
	RI->SetVertexStreams(VS_FixedFunction,Streams,3);
	RI->SetIndexBuffer(&Mesh->Indices,0);
	// The instance stream comes before the material, a device builds the material for it.
	if( !RI->SetInstanceStream(Instances,WorldToLight,&Range,&Wind) )
		return 0;
	RI->SetMaterial(Set->Material->Get());
	RI->DrawInstanced(PT_TriangleList,0,Mesh->Indices.Indices.Num() / 3,Instances->Instances.Num());
	return 1;
}

// Draws a variant's instances that lie Near to Far from the viewer, each with the detail level of its own distance.
// The levels dissolve into each other over the last part of their range.
static void DrawInstances(FRenderInterface* RI,FLevelSceneNode* SceneNode,FFoliageSet* Set,FFoliageVariant* Variant,FFoliageInstanceStream* Instances,FLOAT Near,FLOAT Far,const FVector& WindDirection,const FMatrix* WorldToLight = NULL)
{
	for( INT LOD=0; LOD<Variant->NumLODs; LOD++ )
	{
		FPlane Range = LODRange(Set,Variant,LOD,SceneNode);
		if( LODReaches(Range,Near,Far) && !DrawLOD(RI,SceneNode,Set,Variant,LOD,Range,Instances,WindDirection,WorldToLight) )
			return;
	}
	RI->SetInstanceStream(NULL);
}

static FFoliageGroup* BuildGroup(FFoliageSet* Set,const FFoliageMeshEntry& Entry,ATerrainInfo* Terrain,INT LayerIndex,INT SectorIndex)
{
	guard(FFoliage::BuildGroup);

	const FDecorationLayer&	Layer = Terrain->DecoLayers(LayerIndex);
	FDecoSectorInfo&		Sector = Terrain->DecoLayerData(LayerIndex).Sectors(SectorIndex);
	UBOOL					Smooth = Terrain->Sectors(SectorIndex)->Subdivision > 1;
	FFoliageGroup*			Group = new FFoliageGroup;
	Group->Origin	= Sector.Location;
	Group->Radius	= Sector.Radius;
	Group->Streams.AddZeroed(Set->Variants.Num());

	for( INT i=0; i<Sector.DecoInfo.Num(); i++ )
	{
		const FDecoInfo&	Info = Sector.DecoInfo(i);
		// The index a plant was scattered with, the array itself gets sorted by distance while drawing.
		FFoliageRandom		Random(Layer.Seed + LayerIndex,SectorIndex,Info.Distance & 0xFFFF);
		INT					VariantIndex = PickVariant(Set,Random.Next(),0);
		FLOAT				Yaw = Random.Next() * 2.f * PI,
							Jitter = Set->Scale * Entry.Scale * (1.f + Set->ScaleJitter * (Random.Next() * 2.f - 1.f)),
							Brightness = 1.f + Set->TintJitter * (Random.Next() * 2.f - 1.f),
							NarrowPick = Random.Next();
		FVector				Scale = Info.Scale * Jitter,
							Centre = (FScaleMatrix(Info.Scale) * FRotationMatrix(Info.Rotation) * FTranslationMatrix(Info.Location)).TransformFVector(Entry.Pivot),
							Base,
							Up;
		FLOAT				Relief,
							Spread = Max(Abs(Scale.X),Abs(Scale.Y));
		if( !FitToTerrain(Terrain,Smooth,Centre,Set->Variants(VariantIndex)->LODs[0]->Radius * Spread,Set->TerrainAlign,Base,Up,Relief) )
			continue;
		if( Relief > Set->MaxRelief && !Set->Variants(VariantIndex)->Narrow )
		{
			VariantIndex = PickVariant(Set,NarrowPick,1);
			FitToTerrain(Terrain,Smooth,Centre,Set->Variants(VariantIndex)->LODs[0]->Radius * Spread,Set->TerrainAlign,Base,Up,Relief);
		}

		if( !Group->Streams(VariantIndex) )
			Group->Streams(VariantIndex) = new FFoliageInstanceStream;
		FFoliageInstance& Instance = Group->Streams(VariantIndex)->Instances(Group->Streams(VariantIndex)->Instances.Add());
		Instance.Position	= Base - Up * Set->Sink;
		Instance.FadeEnd	= Layer.FadeoutRadius.Max * Set->FadeScale;
		// Retail thins the alpha over the whole range, often from the viewer's feet. Dissolving does that with holes
		// in a noise, which shows up close and in the contact shadows, so it waits for the last tenth.
		Instance.FadeStart	= Max(Layer.FadeoutRadius.Min,Instance.FadeEnd * 0.9f);
		Instance.Scale		= Scale;
		// Half is full brightness, as for the retail colours.
		Instance.Color		= Tint(UsesTerrainLight(Layer) ? FColor(127,127,127,255) : Info.Color,Set->Tint * Brightness);
		Instance.Rotation	= UprightRotation(Up,Yaw);
	}
	return Group;

	unguard;
}

// The set that replaces a decoration layer, NULL when the retail mesh draws.
static FFoliageSet* LayerSet(FFoliageLibrary* Library,ATerrainInfo* Terrain,INT LayerIndex,FFoliageMeshEntry& Entry)
{
	// Plants hanging from an inverted terrain or standing over a hole in it keep the retail placement.
	if( !Library || Terrain->Inverted || Terrain->DecoLayers(LayerIndex).ShowOnInvisibleTerrain )
		return NULL;
	Entry = FindMesh(Library,Terrain->DecoLayers(LayerIndex).StaticMesh);
	return Entry.Set == INDEX_NONE ? NULL : Library->Sets(Entry.Set);
}

FLOAT FFoliage::FadeScale(ATerrainInfo* Terrain,INT LayerIndex,FLevelSceneNode* SceneNode)
{
	FFoliageMeshEntry Entry;
	FFoliageSet* Set = LayerSet(GetEnabledLibrary(SceneNode),Terrain,LayerIndex,Entry);
	return Set ? Set->FadeScale : 1.f;
}

UBOOL FFoliage::RenderDecorations(ATerrainInfo* Terrain,INT LayerIndex,INT SectorIndex,FLevelSceneNode* SceneNode,FRenderInterface* RI)
{
	guard(FFoliage::RenderDecorations);

	FFoliageLibrary*	Library = GetEnabledLibrary(SceneNode);
	FFoliageMeshEntry	Entry;
	FFoliageSet*		Set = LayerSet(Library,Terrain,LayerIndex,Entry);
	if( !Set )
		return 0;

	FFoliageTerrain** Found = Library->Terrains.Find((QWORD)(PTRINT)Terrain);
	FFoliageTerrain* Built = Found ? *Found : NULL;
	if( !Built )
	{
		Built = Library->Terrains.Set((QWORD)(PTRINT)Terrain,new FFoliageTerrain);
		Built->NumSectors = Terrain->Sectors.Num();
		Built->Groups.AddZeroed(Terrain->DecoLayers.Num() * Built->NumSectors);
		Built->Light = NULL;
	}
	if( !Built->Light && Terrain->VertexColors.Num() >= Terrain->HeightmapX * Terrain->HeightmapY )
	{
		// Texel centres sit on the heightmap samples.
		FVector O = Terrain->WorldToHeightmap(FVector(0,0,0)), Scale(1.f / Terrain->HeightmapX,1.f / Terrain->HeightmapY,0.f);
		FVector Axes[3] = { Terrain->WorldToHeightmap(FVector(1,0,0)) - O, Terrain->WorldToHeightmap(FVector(0,1,0)) - O, Terrain->WorldToHeightmap(FVector(0,0,1)) - O };
		Built->Light = new FFoliageLightTexture(Terrain);
		appMemzero(&Built->WorldToLight,sizeof(FMatrix));
		for( INT i=0; i<3; i++ )
		{
			Built->WorldToLight.M[i][0] = Axes[i].X * Scale.X;
			Built->WorldToLight.M[i][1] = Axes[i].Y * Scale.Y;
		}
		Built->WorldToLight.M[3][0] = (O.X + 0.5f) * Scale.X;
		Built->WorldToLight.M[3][1] = (O.Y + 0.5f) * Scale.Y;
		Built->WorldToLight.M[3][3] = 1.f;
	}
	FFoliageGroup*& Group = Built->Groups(LayerIndex * Built->NumSectors + SectorIndex);
	if( !Group )
		Group = BuildGroup(Set,Entry,Terrain,LayerIndex,SectorIndex);

	FLOAT Distance = FDist(SceneNode->ViewOrigin,Group->Origin), Near = Distance - Group->Radius, Far = Distance + Group->Radius;
	const FDecorationLayer& Layer = Terrain->DecoLayers(LayerIndex);
	UBOOL Lightmapped = Built->Light && UsesTerrainLight(Layer);
	if( Layer.LitDirectional )
	{
		// The caller has set this sector's own sunlights, so it cannot wait for the others.
		RI->EnableLighting(1,0,1,NULL,SceneNode->Viewport->Actor->RendMap == REN_LightingOnly);
		RI->SetTransform(TT_LocalToWorld,FMatrix::Identity);
		for( INT i=0; i<Group->Streams.Num(); i++ )
			if( Group->Streams(i) )
				DrawInstances(RI,SceneNode,Set,Set->Variants(i),Group->Streams(i),Near,Far,LevelWind(Library,SceneNode->Level));
		return 1;
	}
	// A draw per sector costs far more than copying its plants, so the sectors of a frame go out together.
	for( INT i=0; i<Group->Streams.Num(); i++ )
	{
		if( !Group->Streams(i) )
			continue;
		const TArray<FFoliageInstance>& Source = Group->Streams(i)->Instances;
		for( INT LOD=0; LOD<Set->Variants(i)->NumLODs; LOD++ )
		{
			if( !LODReaches(LODRange(Set,Set->Variants(i),LOD,SceneNode),Near,Far) )
				continue;
			TArray<FFoliageInstance>& Merged = Set->Variants(i)->Merged[LOD][Lightmapped].Instances;
			INT First = Merged.Add(Source.Num());
			appMemcpy(Merged.GetTypedData()+First,Source.GetTypedData(),Source.Num() * sizeof(FFoliageInstance));
		}
	}
	return 1;

	unguard;
}

void FFoliage::FlushDecorations(ATerrainInfo* Terrain,FLevelSceneNode* SceneNode,FRenderInterface* RI)
{
	guard(FFoliage::FlushDecorations);

	FFoliageLibrary*	Library = GetEnabledLibrary(SceneNode);
	FFoliageTerrain**	Built = Library ? Library->Terrains.Find((QWORD)(PTRINT)Terrain) : NULL;
	if( !Built )
		return;

	RI->SetTransform(TT_LocalToWorld,FMatrix::Identity);
	for( INT Lightmapped=0; Lightmapped<2; Lightmapped++ )
	{
		// Static lighting is what brings the instance colour in.
		RI->EnableLighting(0,1,1,Lightmapped ? (*Built)->Light : NULL,SceneNode->Viewport->Actor->RendMap == REN_LightingOnly);
		for( INT s=0; s<Library->Sets.Num(); s++ )
		{
			FFoliageSet* Set = Library->Sets(s);
			for( INT v=0; v<Set->Variants.Num(); v++ )
			{
				for( INT LOD=0; LOD<Set->Variants(v)->NumLODs; LOD++ )
				{
					FFoliageInstanceStream& Merged = Set->Variants(v)->Merged[LOD][Lightmapped];
					if( !Merged.Instances.Num() )
						continue;
					Merged.Revision++;
					DrawLOD(RI,SceneNode,Set,Set->Variants(v),LOD,LODRange(Set,Set->Variants(v),LOD,SceneNode),&Merged,LevelWind(Library,SceneNode->Level),Lightmapped ? &(*Built)->WorldToLight : NULL);
					Merged.Instances.Empty(Merged.Instances.Num());
				}
			}
		}
	}
	RI->SetInstanceStream(NULL);

	unguard;
}

FLOAT FFoliage::WindReach(FLevelSceneNode* SceneNode,UStaticMesh* StaticMesh)
{
	FFoliageLibrary* Library = GetEnabledLibrary(SceneNode);
	if( !Library )
		return 0.f;
	FFoliageMeshEntry Entry = FindMesh(Library,StaticMesh);
	if( Entry.Set == INDEX_NONE )
		return 0.f;
	FFoliageSet* Set = Library->Sets(Entry.Set);
	// Twice the bend covers the strongest gust and the taller far detail levels.
	return Set->Tallest * Set->Scale * Entry.Scale * (1.f + Set->ScaleJitter) * Min(2.f * Abs(Set->Bend),1.f);
}

void FFoliage::ForgetTerrain(ATerrainInfo* Terrain,INT SectorIndex)
{
	guard(FFoliage::ForgetTerrain);

	FFoliageLibrary* Library = GLibrary;
	FFoliageTerrain** Found = Library ? Library->Terrains.Find((QWORD)(PTRINT)Terrain) : NULL;
	if( !Found )
		return;
	FFoliageTerrain*	Built = *Found;
	URenderDevice*		RenDev = Terrain->GetLevel() && Terrain->GetLevel()->Engine ? Terrain->GetLevel()->Engine->GRenDev : NULL;
	for( INT i=0; i<Built->Groups.Num(); i++ )
	{
		if( !Built->Groups(i) || (SectorIndex != INDEX_NONE && i % Built->NumSectors != SectorIndex) )
			continue;
		for( INT s=0; RenDev && s<Built->Groups(i)->Streams.Num(); s++ )
			if( Built->Groups(i)->Streams(s) )
				RenDev->FlushResource(Built->Groups(i)->Streams(s)->CacheId);
		delete Built->Groups(i);
		Built->Groups(i) = NULL;
	}
	// Whatever changed the sector may have relit it.
	if( Built->Light )
	{
		if( RenDev )
			RenDev->FlushResource(Built->Light->CacheId);
		delete Built->Light;
		Built->Light = NULL;
	}
	if( SectorIndex == INDEX_NONE )
	{
		delete Built;
		Library->Terrains.Remove((QWORD)(PTRINT)Terrain);
	}

	unguard;
}

UBOOL FFoliage::ReplacesActor(FLevelSceneNode* SceneNode,AActor* Actor)
{
	// A skin or a projector means the map wants this very mesh, a mirrored one has no rotation to pass on.
	if( Actor->DrawType != DT_StaticMesh || !Actor->StaticMesh || !Actor->bStatic || Actor->bDeferRendering || Actor->UV2Texture || Actor->Projectors.Num() || Actor->Skins.Num()
	||	Actor->DrawScale <= 0.f || Actor->DrawScale3D.X <= 0.f || Actor->DrawScale3D.Y <= 0.f || Actor->DrawScale3D.Z <= 0.f )
		return 0;
	FFoliageLibrary* Library = GetEnabledLibrary(SceneNode);
	return Library && FindMesh(Library,Actor->StaticMesh).Set != INDEX_NONE;
}

void FFoliage::RenderActors(TArray<FDynamicActor*>& Actors,FLevelSceneNode* SceneNode,FRenderInterface* RI)
{
	guard(FFoliage::RenderActors);

	FFoliageLibrary* Library = Actors.Num() ? GetEnabledLibrary(SceneNode) : NULL;
	if( !Library )
	{
		Actors.Empty();
		return;
	}

	for( INT i=0; i<Actors.Num(); i++ )
	{
		FDynamicActor*		DynamicActor = Actors(i);
		AActor*				Actor = DynamicActor->Actor;
		FFoliageMeshEntry	Entry = FindMesh(Library,Actor->StaticMesh);
		FFoliageSet*		Set = Library->Sets(Entry.Set);
		const FMatrix&		M = DynamicActor->LocalToWorld;
		FVector				X(M.M[0][0],M.M[0][1],M.M[0][2]),
							Y(M.M[1][0],M.M[1][1],M.M[1][2]),
							Z(M.M[2][0],M.M[2][1],M.M[2][2]),
							Position = M.TransformFVector(Entry.Pivot);
		FVector				Axes(X.Size(),Y.Size(),Z.Size()),
							Scale = Axes * Set->Scale * Entry.Scale;
		// The same plant wherever it is seen from, the mapper's own turn and size stand.
		FFoliageRandom		Random(appRound(Actor->Location.X),appRound(Actor->Location.Y),appRound(Actor->Location.Z));
		FFoliageVariant*	Variant = Set->Variants(PickVariant(Set,Random.Next(),0));
		FLOAT				Brightness = 1.f + Set->TintJitter * (Random.Next() * 2.f - 1.f);

		// One colour for the plant from the light baked on the retail vertices, whose count the plant does not share.
		INT Sum[3] = { 127, 127, 127 };
		if( Actor->StaticMeshInstance && Actor->StaticMeshInstance->ColorStream.Colors.Num() )
		{
			TArray<FColor>& Colors = Actor->StaticMeshInstance->ColorStream.Colors;
			Sum[0] = Sum[1] = Sum[2] = 0;
			for( INT c=0; c<Colors.Num(); c++ )
			{
				Sum[0] += Colors(c).R;
				Sum[1] += Colors(c).G;
				Sum[2] += Colors(c).B;
			}
			for( INT c=0; c<3; c++ )
				Sum[c] /= Colors.Num();
		}
		FColor Light(Min(Sum[0] + DynamicActor->AmbientColor.R,255),Min(Sum[1] + DynamicActor->AmbientColor.G,255),Min(Sum[2] + DynamicActor->AmbientColor.B,255),255);

		FFoliageInstance& Instance = Variant->Placed.Instances(Variant->Placed.Instances.Add());
		Instance.Position	= Position;
		Instance.FadeEnd	= 2e9f;
		Instance.FadeStart	= 1e9f;
		Instance.Scale		= Scale;
		Instance.Color		= Tint(Light,Set->Tint * Brightness);
		RotationFromAxes(X / Axes.X,Y / Axes.Y,Z / Axes.Z,&Instance.Rotation.X);
	}
	Actors.Empty();

	RI->PushState();
	RI->SetTransform(TT_LocalToWorld,FMatrix::Identity);
	RI->EnableLighting(0,1,1,NULL,SceneNode->Viewport->Actor->RendMap == REN_LightingOnly);
	for( INT s=0; s<Library->Sets.Num(); s++ )
	{
		FFoliageSet* Set = Library->Sets(s);
		for( INT v=0; v<Set->Variants.Num(); v++ )
		{
			FFoliageInstanceStream& Placed = Set->Variants(v)->Placed;
			if( !Placed.Instances.Num() )
				continue;
			Placed.Revision++;
			DrawInstances(RI,SceneNode,Set,Set->Variants(v),&Placed,0.f,1e30f,LevelWind(Library,SceneNode->Level));
			Placed.Instances.Empty(Placed.Instances.Num());
		}
	}
	RI->PopState();

	unguard;
}
