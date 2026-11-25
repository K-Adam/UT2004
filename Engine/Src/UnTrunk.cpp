/*=============================================================================
	UnTrunk.cpp: Tree trunks drawn rounder than their retail sections.
=============================================================================*/

#include "EnginePrivate.h"
#include "UnRenderPrivate.h"
#include "UnFoliage.h"
#include "UnFoliagePrivate.h"

/*-----------------------------------------------------------------------------
	Resources.
-----------------------------------------------------------------------------*/

class FTrunkTexture : public FFoliageTexture
{
public:
	virtual ETexClampMode GetUClamp() { return TC_Wrap; }
	virtual ETexClampMode GetVClamp() { return TC_Wrap; }
};

// A bark of the Foliage folder with the fine grain that keeps it crisp up close, a [Bark.X] section of the ini.
struct FTrunkBark
{
	FTrunkTexture			Albedo,
							Detail;
	UBOOL					HasDetail;
	FLOAT					DetailScale,
							TileSize;	// Above 0 the bark lies along the world axes, this many units to a repeat, and takes no mapping or detail.
	UProxyBitmapMaterial*	Bitmaps[2];
	UShader*				Shader;

	FTrunkBark() : HasDetail(0), DetailScale(8.f), TileSize(0.f), Shader(NULL) { Bitmaps[0] = Bitmaps[1] = NULL; }

	// The colour of the smallest mip, which is one DXT1 block.
	FPlane Mean()
	{
		BYTE*	Block = &Albedo.File(Albedo.MipOffsets.Last());
		INT		Ends[2] = { Block[0] | (Block[1] << 8), Block[2] | (Block[3] << 8) },
				Pick = Block[4] & 3;
		FPlane	Colors[2];
		for( INT i=0; i<2; i++ )
			Colors[i] = FPlane((Ends[i] >> 11) / 31.f,((Ends[i] >> 5) & 63) / 63.f,(Ends[i] & 31) / 31.f,1.f);
		FLOAT	Share = Pick < 2 ? Pick : Ends[0] > Ends[1] ? (Pick - 1) / 3.f : 0.5f;
		return Colors[0] + (Colors[1] - Colors[0]) * Share;
	}

	// Objects of no package, made again on demand like the foliage materials.
	UMaterial* Get()
	{
		UBOOL WithDetail = HasDetail && TileSize <= 0.f;
		if( !Shader || !Bitmaps[0] || (WithDetail && !Bitmaps[1]) )
		{
			for( INT i=0; i<(WithDetail ? 2 : 1); i++ )
			{
				Bitmaps[i] = ConstructObject<UProxyBitmapMaterial>(!i && TileSize > 0.f ? UTriplanarBitmapMaterial::StaticClass() : UProxyBitmapMaterial::StaticClass());
				GGlobalTempObjects->AddGlobalObject((UObject**)&Bitmaps[i]);
				Bitmaps[i]->SetTextureInterface(i ? &Detail : &Albedo);
			}
			if( TileSize > 0.f )
			{
				((UTriplanarBitmapMaterial*)Bitmaps[0])->WorldScale	= 1.f / TileSize;
				((UTriplanarBitmapMaterial*)Bitmaps[0])->Mean		= Mean();
			}
			Shader = ConstructObject<UShader>(UShader::StaticClass());
			GGlobalTempObjects->AddGlobalObject((UObject**)&Shader);
			Shader->Diffuse		= Bitmaps[0];
			Shader->Detail		= Bitmaps[1];
			Shader->DetailScale	= DetailScale;
		}
		return Shader;
	}
};

// How the sections of one bark material are refined, a [Trunk.X] section of the ini.
struct FTrunkStyle
{
	FTrunkBark*	Bark;		// NULL keeps the retail material.
	UBOOL		Stitch;		// Moves the pieces of the retail mapping so the bark runs through along the limbs.
	FLOAT		RepeatU,	// How often the bark runs over one repeat of the retail material.
				RepeatV,
				Round,		// How far a new vertex goes toward the curve its neighbours' normals describe, 1 is all the way.
				RoundMax,	// The furthest it may go, in units of the mesh.
				Crease,		// How dark the bark gets where another limb of the tree touches it, 1 is black.
				CreaseReach,	// How far from the other limb that shade ends, in units of the mesh.
				Base,		// How dark the foot of the trunk gets.
				BaseHeight,	// Where that ends above the ground, in world units like MossHeight.
				Ground,		// How far the foot takes on the light baked on the terrain under it.
				Moss,		// How far bark that faces up near the ground goes to MossColor.
				MossHeight,
				TintJitter,	// How much one tree may be brighter or darker than the next.
				Sway;		// How far the wind leans the top of the tree in radians, where the level has wind on its trees.
};

// A section with every triangle split in four, in the space of the mesh.
struct FTrunkSection
{
	const FTrunkStyle*	Style;
	TArray<FVector>		Positions,
						Normals;
	TArray<FLOAT>		UVs;
	TArray<BYTE>		Crease;		// How near another limb of the tree is, 255 where it touches.
	TArray<_WORD>		Source;		// The two retail vertices each vertex lies between, the same one twice for a retail vertex.
	FRawIndexBuffer		Indices;
};

struct FTrunkMesh
{
	FName					Name,
							Outer;
	INT						NumVertices;
	TArray<FTrunkSection*>	Sections;	// NULL for a section that stays retail.
	DWORD					Refined;	// A bit for each section that is not NULL.
	FLOAT					Sway;		// The most any of its barks asks for. 0 for a tree whose twigs and leaves cannot follow.
};

struct FTrunkVertex
{
	FVector	Position,
			Normal;
	FColor	Light;
	FLOAT	U,
			V;
};

// A tree's section as the level batches hold theirs, with the light baked on that tree, but around the
// point the tree stands on. It is drawn as one instance there, which is the path that knows wind.
class FTrunkStream : public FVertexStream
{
public:
	TArray<FTrunkVertex>	Vertices;
	QWORD					CacheId;

	FTrunkStream() : CacheId(MakeCacheID(CID_RenderVertices)) {}

	virtual QWORD GetCacheId() { return CacheId; }
	virtual INT GetRevision() { return 1; }
	virtual INT GetStride() { return sizeof(FTrunkVertex); }
	virtual INT GetSize() { return Vertices.Num() * sizeof(FTrunkVertex); }
	virtual INT GetComponents(FVertexComponent* Components)
	{
		Components[0] = FVertexComponent(CT_Float3,FVF_Position);
		Components[1] = FVertexComponent(CT_Float3,FVF_Normal);
		Components[2] = FVertexComponent(CT_Color,FVF_Diffuse);
		Components[3] = FVertexComponent(CT_Float2,FVF_TexCoord0);
		return 4;
	}
	virtual void GetStreamData(void* Dest) { appMemcpy(Dest,Vertices.GetTypedData(),GetSize()); }
	virtual void GetRawStreamData(void** Dest,INT FirstVertex) { *Dest = &Vertices(FirstVertex); }
};

struct FTrunkActor
{
	ULevel*					Level;
	UStaticMesh*			StaticMesh;
	TArray<FTrunkStream*>	Streams;	// One per section, NULL where the mesh has none.
	FTrunkStream*			Cards;		// The retail vertices of the whole mesh, for the twigs and leaves of a tree that sways.
	FFoliageInstanceStream	Instance;
	FLOAT					Height;

	FTrunkActor() : Cards(NULL) {}
	~FTrunkActor()
	{
		for( INT i=0; i<Streams.Num(); i++ )
			delete Streams(i);
		delete Cards;
	}
	void Flush(URenderDevice* RenDev)
	{
		for( INT i=0; i<Streams.Num(); i++ )
			if( Streams(i) )
				RenDev->FlushResource(Streams(i)->CacheId);
		if( Cards )
			RenDev->FlushResource(Cards->CacheId);
		RenDev->FlushResource(Instance.CacheId);
	}
};

struct FTrunkLibrary
{
	TArray<FTrunkStyle>			Styles;
	TMap<FString,INT>			Materials;	// Retail material path to style.
	TMap<QWORD,FTrunkMesh>		Meshes;		// By mesh address, checked against the names since addresses come back.
	TMap<QWORD,FTrunkActor*>	Actors;
};

/*-----------------------------------------------------------------------------
	Loading.
-----------------------------------------------------------------------------*/

static FTrunkLibrary* LoadTrunks()
{
	guard(FFoliage::LoadTrunks);

	TMultiMap<FString,FString>* Rows = GConfig->GetSectionPrivate(TEXT("Trunks"),0,1,FOLIAGE_INI);
	if( !Rows )
		return NULL;
	FTrunkLibrary*				Library = new FTrunkLibrary;
	TMap<FString,INT>			Names;
	TMap<FString,FTrunkBark*>	Barks;
	for( TMultiMap<FString,FString>::TIterator It(*Rows); It; ++It )
	{
		INT* Style = Names.Find(It.Value());
		if( !Style )
		{
			FString Section = FString(TEXT("Trunk.")) + It.Value();
			if( !GConfig->GetSectionPrivate(*Section,0,1,FOLIAGE_INI) )
			{
				debugf(NAME_Warning,TEXT("Foliage: %s names the unknown trunk %s"),*It.Key(),*It.Value());
				continue;
			}
			FTrunkStyle New;
			New.Bark		= NULL;
			New.Stitch		= FString(GConfig->GetStr(*Section,TEXT("Mapping"),FOLIAGE_INI)) == TEXT("Stitch");
			New.RepeatU		= New.RepeatV = 1.f;
			New.Round		= 0.75f;
			New.RoundMax	= 4.f;
			GConfig->GetFloat(*Section,TEXT("RepeatU"),New.RepeatU,FOLIAGE_INI);
			GConfig->GetFloat(*Section,TEXT("RepeatV"),New.RepeatV,FOLIAGE_INI);
			New.RepeatU		= Max(New.RepeatU,1.f);
			New.RepeatV		= Max(New.RepeatV,1.f);
			GConfig->GetFloat(*Section,TEXT("Round"),New.Round,FOLIAGE_INI);
			GConfig->GetFloat(*Section,TEXT("RoundMax"),New.RoundMax,FOLIAGE_INI);
			New.Crease		= New.CreaseReach = New.Base = New.BaseHeight = New.Ground = New.Moss = New.MossHeight = New.TintJitter = 0.f;
			GConfig->GetFloat(*Section,TEXT("Crease"),New.Crease,FOLIAGE_INI);
			GConfig->GetFloat(*Section,TEXT("CreaseReach"),New.CreaseReach,FOLIAGE_INI);
			GConfig->GetFloat(*Section,TEXT("Base"),New.Base,FOLIAGE_INI);
			GConfig->GetFloat(*Section,TEXT("BaseHeight"),New.BaseHeight,FOLIAGE_INI);
			GConfig->GetFloat(*Section,TEXT("Ground"),New.Ground,FOLIAGE_INI);
			GConfig->GetFloat(*Section,TEXT("Moss"),New.Moss,FOLIAGE_INI);
			GConfig->GetFloat(*Section,TEXT("MossHeight"),New.MossHeight,FOLIAGE_INI);
			GConfig->GetFloat(*Section,TEXT("TintJitter"),New.TintJitter,FOLIAGE_INI);
			New.Sway		= 0.f;
			GConfig->GetFloat(*Section,TEXT("Sway"),New.Sway,FOLIAGE_INI);
			FString BarkName;
			if( GConfig->GetString(*Section,TEXT("Bark"),BarkName,FOLIAGE_INI) )
			{
				FTrunkBark** Loaded = Barks.Find(BarkName);
				if( !Loaded )
				{
					FString		BarkSection = FString(TEXT("Bark.")) + BarkName,
								DetailFile;
					FTrunkBark*	Bark = new FTrunkBark;
					GConfig->GetFloat(*BarkSection,TEXT("DetailScale"),Bark->DetailScale,FOLIAGE_INI);
					GConfig->GetFloat(*BarkSection,TEXT("TileSize"),Bark->TileSize,FOLIAGE_INI);
					Bark->HasDetail = GConfig->GetString(*BarkSection,TEXT("Detail"),DetailFile,FOLIAGE_INI) && Bark->Detail.Load(*(FString(FOLIAGE_DIR) + DetailFile));
					if( !Bark->Albedo.Load(*(FString(FOLIAGE_DIR) + GConfig->GetStr(*BarkSection,TEXT("Albedo"),FOLIAGE_INI))) )
					{
						debugf(NAME_Warning,TEXT("Foliage: %s has no usable texture, %s keeps its retail bark"),*BarkSection,*Section);
						delete Bark;
						Bark = NULL;
					}
					Loaded = &Barks.Set(*BarkName,Bark);
				}
				New.Bark = *Loaded;
			}
			Style = &Names.Set(*It.Value(),Library->Styles.AddItem(New));
		}
		Library->Materials.Set(*It.Key(),*Style);
	}
	GConfig->UnloadFile(FOLIAGE_INI);
	debugf(TEXT("Foliage: %i bark materials get rounder trunks"),Library->Materials.Num());
	return Library->Materials.Num() ? Library : NULL;

	unguard;
}

static FTrunkLibrary* GTrunks = NULL;

static FTrunkLibrary* GetEnabledTrunks(FLevelSceneNode* SceneNode)
{
	if( !FoliageEnabled(SceneNode) )
		return NULL;
	static UBOOL Tried = 0;
	if( !Tried )
	{
		Tried	= 1;
		GTrunks	= LoadTrunks();
	}
	return GTrunks;
}

// The style of the first listed material down a section's material, which has to be opaque all the way.
// A leaf card that shares a bark's texture stays what it is.
static const FTrunkStyle* FindStyle(FTrunkLibrary* Library,UMaterial* Material)
{
	const FTrunkStyle* Style = NULL;
	for( INT Depth=0; Material && Depth<8; Depth++ )
	{
		INT*			Listed = Library->Materials.Find(Material->GetPathName());
		UFinalBlend*	Blend = Cast<UFinalBlend>(Material);
		UShader*		Shader = Cast<UShader>(Material);
		UModifier*		Modifier = Cast<UModifier>(Material);
		UTexture*		Texture = Cast<UTexture>(Material);
		if( Listed && !Style )
			Style = &Library->Styles(*Listed);
		if( (Blend && (Blend->AlphaTest || Blend->FrameBufferBlending != FB_Overwrite))
		||	(Shader && (Shader->Opacity || Shader->OutputBlending != OB_Normal))
		||	(Texture && (Texture->bMasked || Texture->bAlphaTexture)) )
			return NULL;
		Material = Shader ? Shader->Diffuse : Modifier ? Modifier->Material : NULL;
	}
	return Style;
}

/*-----------------------------------------------------------------------------
	Mapping.
-----------------------------------------------------------------------------*/

// Two triangles that share an edge, with the corners of each at its two ends.
struct FTrunkEdge
{
	INT T1, A1, B1, T2, A2, B2;
};

static void SharedEdges(const TArray<INT>& Weld,TArray<FTrunkEdge>& Edges)
{
	TMap<QWORD,FTrunkEdge>	Open;
	TMap<QWORD,INT>			Uses;
	for( INT i=0; i<Weld.Num(); i+=3 )
		for( INT c=0; c<3; c++ )
		{
			INT		A = i + c,
					B = i + (c + 1) % 3;
			if( Weld(A) > Weld(B) )
				Exchange(A,B);
			QWORD		Key = ((QWORD)Weld(A) << 32) | (QWORD)Weld(B);
			INT*		Count = Uses.Find(Key);
			FTrunkEdge*	First = Open.Find(Key);
			if( !Count )
			{
				FTrunkEdge Edge = { i / 3, A, B, 0, 0, 0 };
				Open.Set(Key,Edge);
				Uses.Set(Key,1);
			}
			else if( ++*Count == 2 )
			{
				First->T2 = i / 3;
				First->A2 = A;
				First->B2 = B;
			}
		}
	// An edge of three or more triangles has no one neighbour to run the bark on into.
	for( TMap<QWORD,FTrunkEdge>::TIterator It(Open); It; ++It )
		if( *Uses.Find(It.Key()) == 2 )
			Edges.AddItem(It.Value());
}

static inline INT Compare(DOUBLE A,DOUBLE B)
{
	return A < B ? -1 : A > B ? 1 : 0;
}

static DOUBLE Median(TArray<DOUBLE>& Values)
{
	Sort(Values.GetTypedData(),Values.Num());
	return (Values((Values.Num() - 1) / 2) + Values(Values.Num() / 2)) * 0.5;
}

// The head of the set an entry belongs to, where each entry names another of its set or itself.
static INT Root(TArray<INT>& Parent,INT T)
{
	while( Parent(T) != T )
		T = Parent(T) = Parent(Parent(T));
	return T;
}

// Closes seams of the retail mapping by moving its pieces as wholes, so nothing that ran through can break.
// A piece that wraps a limb gets a whole number of repeats around it, with the same factor along the limb to
// keep the bark's proportions. Then each piece slides to meet its neighbour, starting from the largest.
static void StitchMapping(const TArray<FVector>& Corners,const TArray<INT>& Weld,TArray<DOUBLE>& UVs,TArray<INT>& Pieces,FLOAT RepeatU,FLOAT RepeatV)
{
	guard(FFoliage::StitchMapping);

	TArray<FTrunkEdge> Edges, Seams;
	SharedEdges(Weld,Edges);
	INT NumTriangles = Weld.Num() / 3;

	// Triangles belong to one piece where both sides of an edge agree.
	TArray<INT> Parent;
	Parent.Add(NumTriangles);
	for( INT t=0; t<NumTriangles; t++ )
		Parent(t) = t;
	for( INT e=0; e<Edges.Num(); e++ )
	{
		const FTrunkEdge& Edge = Edges(e);
		if( Abs(UVs(Edge.A1 * 2) - UVs(Edge.A2 * 2)) < 0.004 && Abs(UVs(Edge.A1 * 2 + 1) - UVs(Edge.A2 * 2 + 1)) < 0.004
		&&	Abs(UVs(Edge.B1 * 2) - UVs(Edge.B2 * 2)) < 0.004 && Abs(UVs(Edge.B1 * 2 + 1) - UVs(Edge.B2 * 2 + 1)) < 0.004 )
			Parent(Root(Parent,Edge.T1)) = Root(Parent,Edge.T2);
		else
			Seams.AddItem(Edge);
	}
	TArray<FLOAT> Areas;
	Areas.AddZeroed(NumTriangles);
	for( INT t=0; t<NumTriangles; t++ )
	{
		Pieces(t) = Root(Parent,t);
		Areas(Pieces(t)) += ((Corners(t * 3 + 1) - Corners(t * 3)) ^ (Corners(t * 3 + 2) - Corners(t * 3))).Size();
	}

	// A seam inside one piece is where it closes around the limb.
	for( INT p=0; p<NumTriangles; p++ )
	{
		if( Areas(p) <= 0.f )
			continue;
		TArray<DOUBLE> WrapU, WrapV;
		for( INT s=0; s<Seams.Num(); s++ )
		{
			const FTrunkEdge& Seam = Seams(s);
			if( Pieces(Seam.T1) != p || Pieces(Seam.T2) != p )
				continue;
			const INT Ends[2][2] = { { Seam.A1, Seam.A2 }, { Seam.B1, Seam.B2 } };
			for( INT k=0; k<2; k++ )
			{
				DOUBLE	DU = UVs(Ends[k][0] * 2) - UVs(Ends[k][1] * 2),
						DV = UVs(Ends[k][0] * 2 + 1) - UVs(Ends[k][1] * 2 + 1),
						Sign = DU > 0.0 ? 1.0 : -1.0;
				WrapU.AddItem(DU * Sign);
				WrapV.AddItem(DV * Sign);
			}
		}
		DOUBLE	Slant = 0.0,
				Factor = RepeatU;
		if( WrapU.Num() >= 2 )
		{
			DOUBLE DU = Median(WrapU), DV = Median(WrapV);
			if( DU > 0.05 )
			{
				Slant	= DV / DU;
				Factor	= Max(1,appRound(DU * RepeatU)) / DU;
			}
		}
		for( INT t=0; t<NumTriangles; t++ )
			for( INT c=0; Pieces(t) == p && c<3; c++ )
			{
				DOUBLE& U = UVs((t * 3 + c) * 2);
				DOUBLE& V = UVs((t * 3 + c) * 2 + 1);
				V = (V - Slant * U) * RepeatV * Factor / RepeatU;
				U *= Factor;
			}
	}

	// Slide the pieces together, each onto one that is already in place.
	TArray<UBOOL> Done;
	Done.AddZeroed(NumTriangles);
	for( ;; )
	{
		INT Root = INDEX_NONE;
		for( INT p=0; p<NumTriangles; p++ )
			if( !Done(p) && Areas(p) > 0.f && (Root == INDEX_NONE || Areas(p) > Areas(Root)) )
				Root = p;
		if( Root == INDEX_NONE )
			break;
		TArray<INT> Queue;
		Queue.AddItem(Root);
		Done(Root) = 1;
		for( INT q=0; q<Queue.Num(); q++ )
		{
			INT Placed = Queue(q);
			for( INT p=0; p<NumTriangles; p++ )
			{
				if( Done(p) || Areas(p) <= 0.f )
					continue;
				TArray<DOUBLE> SlideU, SlideV;
				for( INT s=0; s<Seams.Num(); s++ )
				{
					const FTrunkEdge&	Seam = Seams(s);
					UBOOL				Forward = Pieces(Seam.T1) == Placed && Pieces(Seam.T2) == p;
					if( !Forward && !(Pieces(Seam.T2) == Placed && Pieces(Seam.T1) == p) )
						continue;
					const INT Ends[2][2] = { { Seam.A1, Seam.A2 }, { Seam.B1, Seam.B2 } };
					for( INT k=0; k<2; k++ )
					{
						INT On = Ends[k][Forward ? 0 : 1], Off = Ends[k][Forward ? 1 : 0];
						SlideU.AddItem(UVs(On * 2) - UVs(Off * 2));
						SlideV.AddItem(UVs(On * 2 + 1) - UVs(Off * 2 + 1));
					}
				}
				if( !SlideU.Num() )
					continue;
				DOUBLE DU = Median(SlideU), DV = Median(SlideV);
				for( INT t=0; t<NumTriangles; t++ )
					for( INT c=0; Pieces(t) == p && c<3; c++ )
					{
						UVs((t * 3 + c) * 2)		+= DU;
						UVs((t * 3 + c) * 2 + 1)	+= DV;
					}
				Done(p) = 1;
				Queue.AddItem(p);
			}
		}
	}
	unguard;
}

/*-----------------------------------------------------------------------------
	Refinement.
-----------------------------------------------------------------------------*/

// 0 to 1 over 0 to 1 with a soft start and end.
static FLOAT Smooth(FLOAT X)
{
	X = Clamp(X,0.f,1.f);
	return X * X * (3.f - 2.f * X);
}

static FVector ClosestOnTriangle(const FVector& P,const FVector& A,const FVector& B,const FVector& C)
{
	FVector	AB = B - A, AC = C - A, AP = P - A;
	FLOAT	D1 = AB | AP, D2 = AC | AP;
	if( D1 <= 0.f && D2 <= 0.f )
		return A;
	FVector	BP = P - B;
	FLOAT	D3 = AB | BP, D4 = AC | BP;
	if( D3 >= 0.f && D4 <= D3 )
		return B;
	FLOAT	VC = D1 * D4 - D3 * D2;
	if( VC <= 0.f && D1 >= 0.f && D3 <= 0.f )
		return A + AB * (D1 / (D1 - D3));
	FVector	CP = P - C;
	FLOAT	D5 = AB | CP, D6 = AC | CP;
	if( D6 >= 0.f && D5 <= D6 )
		return C;
	FLOAT	VB = D5 * D2 - D1 * D6;
	if( VB <= 0.f && D2 >= 0.f && D6 <= 0.f )
		return A + AC * (D2 / (D2 - D6));
	FLOAT	VA = D3 * D6 - D5 * D4;
	if( VA <= 0.f && D4 - D3 >= 0.f && D5 - D6 >= 0.f )
		return B + (C - B) * ((D4 - D3) / ((D4 - D3) + (D5 - D6)));
	FLOAT	Sum = VA + VB + VC;
	return Sum > 0.f ? A + AB * (VB / Sum) + AC * (VC / Sum) : A;
}

// Splits every triangle of a section in four. A new vertex sits on the middle of an edge, pulled toward the
// planes the normals at the edge's ends describe, which is Phong tessellation. The retail vertices stay.
static FTrunkSection* RefineSection(UStaticMesh* StaticMesh,INT SectionIndex,const FTrunkStyle* Style)
{
	guard(FFoliage::RefineSection);

	const FStaticMeshSection&	Section = StaticMesh->Sections(SectionIndex);
	TArray<FStaticMeshVertex>&	Vertices = StaticMesh->VertexStream.Vertices;
	TArray<_WORD>&				Indices = StaticMesh->IndexBuffer.Indices;
	INT							NumCorners = Section.NumPrimitives * 3;
	if( Section.IsStrip || !NumCorners || Section.FirstIndex + NumCorners > Indices.Num() || !StaticMesh->UVStreams.Num() || StaticMesh->UVStreams(0).UVs.Num() != Vertices.Num() )
		return NULL;
	for( INT i=0; i<NumCorners; i++ )
		if( !Vertices.IsValidIndex(Indices(Section.FirstIndex + i)) )
			return NULL;

	// The rounding follows normals shared by all vertices at a position, so the two sides of a texture seam
	// move alike and no gap opens.
	TMap<QWORD,INT>	WeldIds;
	TArray<INT>		Weld;
	TArray<FVector>	WeldNormals;
	Weld.Add(NumCorners);
	for( INT i=0; i<NumCorners; i++ )
	{
		const FVector&	P = Vertices(Indices(Section.FirstIndex + i)).Position;
		QWORD			Key = ((QWORD)(appRound(P.X * 20.f) & 0x1FFFFF) << 42) | ((QWORD)(appRound(P.Y * 20.f) & 0x1FFFFF) << 21) | (QWORD)(appRound(P.Z * 20.f) & 0x1FFFFF);
		INT*			Id = WeldIds.Find(Key);
		Weld(i) = Id ? *Id : WeldIds.Set(Key,WeldNormals.AddZeroed());
	}
	for( INT i=0; i<NumCorners; i+=3 )
	{
		const FVector&	A = Vertices(Indices(Section.FirstIndex + i)).Position;
		FVector			Face = (Vertices(Indices(Section.FirstIndex + i + 1)).Position - A) ^ (Vertices(Indices(Section.FirstIndex + i + 2)).Position - A);
		for( INT c=0; c<3; c++ )
			WeldNormals(Weld(i + c)) += Face;
	}
	for( INT i=0; i<WeldNormals.Num(); i++ )
		WeldNormals(i) = WeldNormals(i).SafeNormal();

	// A corner's coordinates, and for each triangle the piece of the mapping that moved as one.
	TArray<DOUBLE>	CornerUVs;
	TArray<INT>		Pieces;
	CornerUVs.Add(NumCorners * 2);
	Pieces.AddZeroed(NumCorners / 3);
	for( INT i=0; i<NumCorners; i++ )
	{
		CornerUVs(i * 2)		= StaticMesh->UVStreams(0).UVs(Indices(Section.FirstIndex + i)).U;
		CornerUVs(i * 2 + 1)	= StaticMesh->UVStreams(0).UVs(Indices(Section.FirstIndex + i)).V;
	}
	if( Style->Bark && Style->Stitch )
	{
		TArray<FVector> Corners;
		Corners.Add(NumCorners);
		for( INT i=0; i<NumCorners; i++ )
			Corners(i) = Vertices(Indices(Section.FirstIndex + i)).Position;
		StitchMapping(Corners,Weld,CornerUVs,Pieces,Style->RepeatU,Style->RepeatV);
	}
	else if( Style->Bark )
		for( INT i=0; i<NumCorners; i++ )
		{
			CornerUVs(i * 2)		*= Style->RepeatU;
			CornerUVs(i * 2 + 1)	*= Style->RepeatV;
		}

	FTrunkSection*		Fine = new FTrunkSection;
	TMap<QWORD,INT>		Corners;	// Retail vertex and piece to fine vertex, stitching can give a retail vertex two places in the bark.
	TMap<QWORD,INT>		Middles;	// Fine vertex pair to the vertex between them.
	TArray<INT>			FineWeld;
	Fine->Style = Style;
	for( INT i=0; i<NumCorners; i+=3 )
	{
		INT V[6];
		for( INT c=0; c<3; c++ )
		{
			INT		Retail = Indices(Section.FirstIndex + i + c);
			QWORD	Key = ((QWORD)Pieces(i / 3) << 32) | (QWORD)Retail;
			INT*	Found = Corners.Find(Key);
			if( Found )
				V[c] = *Found;
			else
			{
				V[c] = Corners.Set(Key,Fine->Positions.AddItem(Vertices(Retail).Position));
				Fine->Normals.AddItem(Vertices(Retail).Normal);
				Fine->UVs.AddItem(CornerUVs((i + c) * 2));
				Fine->UVs.AddItem(CornerUVs((i + c) * 2 + 1));
				Fine->Source.AddItem(Retail);
				Fine->Source.AddItem(Retail);
				FineWeld.AddItem(Weld(i + c));
			}
		}
		for( INT c=0; c<3; c++ )
		{
			INT		A = V[c],
					B = V[(c + 1) % 3];
			QWORD	Key = ((QWORD)Min(A,B) << 32) | (QWORD)Max(A,B);
			INT*	Found = Middles.Find(Key);
			if( Found )
			{
				V[3 + c] = *Found;
				continue;
			}
			FVector	Middle = (Fine->Positions(A) + Fine->Positions(B)) * 0.5f,
					NA = WeldNormals(FineWeld(A)),
					NB = WeldNormals(FineWeld(B)),
					Pull = (NA * ((Middle - Fine->Positions(A)) | NA) + NB * ((Middle - Fine->Positions(B)) | NB)) * (-0.5f * Style->Round);
			FLOAT	Length = Pull.Size();
			if( Length > Style->RoundMax )
				Pull *= Style->RoundMax / Length;
			V[3 + c] = Middles.Set(Key,Fine->Positions.AddItem(Middle + Pull));
			Fine->Normals.AddItem((Fine->Normals(A) + Fine->Normals(B)).SafeNormal());
			Fine->UVs.AddItem((Fine->UVs(A * 2) + Fine->UVs(B * 2)) * 0.5f);
			Fine->UVs.AddItem((Fine->UVs(A * 2 + 1) + Fine->UVs(B * 2 + 1)) * 0.5f);
			Fine->Source.AddItem(Fine->Source(A * 2));
			Fine->Source.AddItem(Fine->Source(B * 2));
			// Normals are only read for retail vertices, a new vertex takes the entry of one end to name its shell.
			FineWeld.AddItem(FineWeld(A));
		}
		static const INT Split[12] = { 0,3,5, 3,1,4, 5,4,2, 3,4,5 };
		for( INT s=0; s<12; s++ )
			Fine->Indices.Indices.AddItem(V[Split[s]]);
	}
	if( Fine->Positions.Num() > 65535 )
	{
		delete Fine;
		return NULL;
	}
	Fine->Indices.Revision = 1;

	// A tree is modelled as limbs pushed into each other. Where another limb is near, the bark is in its
	// shade, and the dark ring hides that the bark does not run on from one limb into the next.
	Fine->Crease.AddZeroed(Fine->Positions.Num());
	if( Style->Crease > 0.f && Style->CreaseReach > 0.f )
	{
		TArray<INT> Shell;
		Shell.Add(WeldNormals.Num());
		for( INT i=0; i<Shell.Num(); i++ )
			Shell(i) = i;
		for( INT i=0; i<NumCorners; i+=3 )
			for( INT c=1; c<3; c++ )
				Shell(Root(Shell,Weld(i))) = Root(Shell,Weld(i + c));
		for( INT i=0; i<Shell.Num(); i++ )
			Shell(i) = Root(Shell,i);
		for( INT v=0; v<Fine->Positions.Num(); v++ )
		{
			FLOAT Nearest = Square(Style->CreaseReach);
			for( INT i=0; i<NumCorners; i+=3 )
				if( Shell(Weld(i)) != Shell(FineWeld(v)) )
					Nearest = Min(Nearest,(ClosestOnTriangle(Fine->Positions(v),Vertices(Indices(Section.FirstIndex + i)).Position,Vertices(Indices(Section.FirstIndex + i + 1)).Position,Vertices(Indices(Section.FirstIndex + i + 2)).Position) - Fine->Positions(v)).SizeSquared());
			Fine->Crease(v) = appRound(255.f * (1.f - Smooth(appSqrt(Nearest) / Style->CreaseReach)));
		}
	}
	return Fine;

	unguard;
}

static FTrunkMesh& FindTrunkMesh(FTrunkLibrary* Library,UStaticMesh* StaticMesh)
{
	FTrunkMesh* Found = Library->Meshes.Find((QWORD)(PTRINT)StaticMesh);
	if( Found && Found->Name == StaticMesh->GetFName() && Found->Outer == StaticMesh->GetOuter()->GetFName() && Found->NumVertices == StaticMesh->VertexStream.Vertices.Num() )
		return *Found;
	if( Found )
		for( INT i=0; i<Found->Sections.Num(); i++ )
			delete Found->Sections(i);

	FTrunkMesh New;
	New.Name		= StaticMesh->GetFName();
	New.Outer		= StaticMesh->GetOuter()->GetFName();
	New.NumVertices	= StaticMesh->VertexStream.Vertices.Num();
	New.Refined		= 0;
	New.Sway		= 0.f;
	UBOOL CardsFollow = 1;
	New.Sections.AddZeroed(StaticMesh->Sections.Num());
	// The caller hands the drawn sections back as bits of a DWORD, a mesh with more sections than that stays retail.
	for( INT i=0; StaticMesh->Sections.Num() <= 31 && i<StaticMesh->Sections.Num(); i++ )
	{
		UMaterial*			Material = i < StaticMesh->Materials.Num() ? StaticMesh->Materials(i).Material : NULL;
		const FTrunkStyle*	Style = Material && Material->RequiredUVStreams() == 1 ? FindStyle(Library,Material) : NULL;
		if( Style )
			New.Sections(i) = RefineSection(StaticMesh,i,Style);
		if( New.Sections(i) )
		{
			New.Refined |= 1u << i;
			New.Sway = Max(New.Sway,Style->Sway);
		}
		else if( Style )
			debugf(TEXT("Foliage: section %i of %s keeps its retail trunk"),i,StaticMesh->GetPathName());
		// A retail section moves with the tree when it can be drawn like the bark, from one stream in one go.
		if( !New.Sections(i) && (!Material || Material->RequiredUVStreams() != 1 || StaticMesh->Sections(i).IsStrip) )
			CardsFollow = 0;
	}
	if( New.Sway > 0.f && (!CardsFollow || !StaticMesh->UVStreams.Num() || StaticMesh->UVStreams(0).UVs.Num() != New.NumVertices) )
	{
		debugf(TEXT("Foliage: %s stands still in the wind, its twigs and leaves cannot be drawn along with the bark"),StaticMesh->GetPathName());
		New.Sway = 0.f;
	}
	return Library->Meshes.Set((QWORD)(PTRINT)StaticMesh,New);
}

// The light the level batch gives a retail vertex, here for a point between two of them.
static FColor BakedLight(FDynamicActor* Owner,UStaticMeshInstance* Instance,INT A,INT B)
{
	if( Owner->Actor->bUnlit )
		return FColor(127,127,127);
	if( !Instance )
		return Owner->AmbientColor;
	FColor CA = Instance->ColorStream.Colors(A), CB = Instance->ColorStream.Colors(B);
	FColor Light((CA.R + CB.R) / 2,(CA.G + CB.G) / 2,(CA.B + CB.B) / 2,(CA.A + CB.A) / 2);
	Light += Owner->AmbientColor;
	return Light;
}

// The light of a tree on its refined sections, each new vertex between the two retail ones it came from.
static FTrunkActor* BuildTrunkActor(const FTrunkMesh& Mesh,FDynamicActor* Owner,ULevel* Level)
{
	guard(FFoliage::BuildTrunkActor);

	AActor*					Actor = Owner->Actor;
	UStaticMesh*			StaticMesh = Actor->StaticMesh;
	UStaticMeshInstance*	Instance = Actor->StaticMeshInstance;
	// What the level batch asks before it takes the baked light, so trunk and leaves get the same.
	if( Instance && Instance->ColorStream.Colors.Num() != StaticMesh->VertexStream.Vertices.Num() )
		Instance = NULL;
	for( INT i=0; Instance && i<Instance->Lights.Num(); i++ )
		if( Instance->Lights(i).VisibilityBits.Num() != (StaticMesh->VertexStream.Vertices.Num() + 7) / 8 )
			Instance = NULL;
	if( Instance )
		CalculateStaticMeshLighting(StaticMesh,Instance,Owner);

	FTrunkActor*	Built = new FTrunkActor;
	FMatrix			NormalToWorld = Owner->LocalToWorld.TransposeAdjoint();
	FLOAT			NormalSign = Owner->Determinant < 0.f ? -1.f : 1.f;
	Built->Level		= Level;
	Built->StaticMesh	= StaticMesh;
	Built->Streams.AddZeroed(Mesh.Sections.Num());

	// Where the tree stands: the ground under its lowest bark, and the light baked on the terrain there.
	FVector	Lowest(0,0,1e30f),
			Ground;
	FColor	GroundLight(0,0,0,0);
	UBOOL	OnTerrain = 0;
	for( INT s=0; s<Mesh.Sections.Num(); s++ )
		for( INT v=0; Mesh.Sections(s) && v<Mesh.Sections(s)->Positions.Num(); v++ )
		{
			FVector World = Owner->LocalToWorld.TransformFVector(Mesh.Sections(s)->Positions(v));
			if( World.Z < Lowest.Z )
				Lowest = World;
		}
	Ground = Lowest;
	for( INT t=0; !OnTerrain && Actor->Region.Zone && t<Actor->Region.Zone->Terrains.Num(); t++ )
	{
		ATerrainInfo* Terrain = Actor->Region.Zone->Terrains(t);
		if( !Terrain || !Terrain->SurfacePoint(Lowest,0,Ground) || Terrain->VertexColors.Num() < Terrain->HeightmapX * Terrain->HeightmapY )
			continue;
		FVector Sample = Terrain->WorldToHeightmap(Lowest);
		GroundLight	= Terrain->VertexColors(Clamp(appRound(Sample.X),0,Terrain->HeightmapX - 1) + Clamp(appRound(Sample.Y),0,Terrain->HeightmapY - 1) * Terrain->HeightmapX);
		OnTerrain	= 1;
	}
	Built->Height = 1.f;
	for( INT v=0; v<StaticMesh->VertexStream.Vertices.Num(); v++ )
		Built->Height = Max(Built->Height,Owner->LocalToWorld.TransformFVector(StaticMesh->VertexStream.Vertices(v).Position).Z - Ground.Z);
	FFoliageInstance& Record = Built->Instance.Instances(Built->Instance.Instances.Add());
	Record.Position		= Ground;
	Record.FadeEnd		= 2e9f;
	Record.Rotation		= FQuat(0.f,0.f,0.f,1.f);
	Record.Scale		= FVector(1,1,1);
	Record.FadeStart	= 1e9f;
	Record.Color		= FColor(255,255,255,255);
	if( Mesh.Sway > 0.f )
	{
		Built->Cards = new FTrunkStream;
		Built->Cards->Vertices.Add(StaticMesh->VertexStream.Vertices.Num());
		for( INT v=0; v<Built->Cards->Vertices.Num(); v++ )
		{
			FTrunkVertex& Vertex = Built->Cards->Vertices(v);
			Vertex.Position	= Owner->LocalToWorld.TransformFVector(StaticMesh->VertexStream.Vertices(v).Position) - Ground;
			Vertex.Normal	= (FVector)NormalToWorld.TransformNormal(StaticMesh->VertexStream.Vertices(v).Normal).SafeNormal() * NormalSign;
			Vertex.U		= StaticMesh->UVStreams(0).UVs(v).U;
			Vertex.V		= StaticMesh->UVStreams(0).UVs(v).V;
			Vertex.Light	= BakedLight(Owner,Instance,v,v);
		}
	}

	// The same tree wherever it is seen from.
	FFoliageRandom	Random(appRound(Actor->Location.X),appRound(Actor->Location.Y),appRound(Actor->Location.Z));
	FLOAT			Jitter = Random.Next() * 2.f - 1.f;

	for( INT s=0; s<Mesh.Sections.Num(); s++ )
	{
		FTrunkSection* Section = Mesh.Sections(s);
		if( !Section )
			continue;
		const FTrunkStyle& Style = *Section->Style;
		FTrunkStream* Stream = Built->Streams(s) = new FTrunkStream;
		Stream->Vertices.Add(Section->Positions.Num());
		for( INT v=0; v<Section->Positions.Num(); v++ )
		{
			FTrunkVertex& Vertex = Stream->Vertices(v);
			Vertex.Position	= Owner->LocalToWorld.TransformFVector(Section->Positions(v)) - Ground;
			Vertex.Normal	= (FVector)NormalToWorld.TransformNormal(Section->Normals(v)).SafeNormal() * NormalSign;
			Vertex.U		= Section->UVs(v * 2);
			Vertex.V		= Section->UVs(v * 2 + 1);
			Vertex.Light	= BakedLight(Owner,Instance,Section->Source(v * 2),Section->Source(v * 2 + 1));

			FLOAT	Height = Vertex.Position.Z,
					Low = Style.BaseHeight > 0.f ? 1.f - Smooth(Height / Style.BaseHeight) : 0.f,
					Mossy = Style.MossHeight > 0.f ? Style.Moss * (1.f - Smooth(Height / Style.MossHeight)) * Clamp(Vertex.Normal.Z,0.f,1.f) : 0.f,
					Shade = (1.f - Style.Crease * Section->Crease(v) / 255.f) * (1.f - Style.Base * Low) * (1.f + Style.TintJitter * Jitter),
					Pull = OnTerrain ? Style.Ground * Low : 0.f;
			FVector	Light(Vertex.Light.R,Vertex.Light.G,Vertex.Light.B);
			Light += (FVector(GroundLight.R,GroundLight.G,GroundLight.B) - Light) * Pull;
			Light *= (FVector(1,1,1) + (FVector(0.75f,1.f,0.6f) - FVector(1,1,1)) * Mossy) * Shade;
			Vertex.Light = FColor(Clamp(appRound(Light.X),0,255),Clamp(appRound(Light.Y),0,255),Clamp(appRound(Light.Z),0,255),Vertex.Light.A);
		}
	}
	return Built;

	unguard;
}

/*-----------------------------------------------------------------------------
	Drawing.
-----------------------------------------------------------------------------*/

DWORD FFoliage::RenderTrunks(FLevelSceneNode* SceneNode,FDynamicActor* Owner,TList<FProjectorRenderInfo*>* Projectors,UBOOL LightsSet,FRenderInterface* RI)
{
	guard(FFoliage::RenderTrunks);

	AActor*			Actor = Owner->Actor;
	UStaticMesh*	StaticMesh = Actor->StaticMesh;
	// A skin means the map wants its own material here. Only trees of the level batches are taken, their light is settled.
	if( !Actor->bStatic || Actor->Skins.Num() || Actor->UV2Texture || Actor->StaticSectionBatches.Num() != StaticMesh->Sections.Num() )
		return 0;
	FTrunkLibrary* Library = GetEnabledTrunks(SceneNode);
	if( !Library )
		return 0;
	FTrunkMesh& Mesh = FindTrunkMesh(Library,StaticMesh);
	if( !Mesh.Refined )
		return 0;

	FTrunkActor** Found = Library->Actors.Find((QWORD)(PTRINT)Actor);
	FTrunkActor* Built = Found ? *Found : NULL;
	if( !Built || Built->StaticMesh != StaticMesh || Built->Level != SceneNode->Level || Built->Streams.Num() != Mesh.Sections.Num() )
	{
		if( Built )
			Built->Flush(SceneNode->Viewport->RenDev);
		delete Built;
		Built = Library->Actors.Set((QWORD)(PTRINT)Actor,BuildTrunkActor(Mesh,Owner,SceneNode->Level));
	}

	UBOOL ShowProjectors = SceneNode->Viewport->Actor->ShowFlags & SHOW_Projectors;
	RI->PushState();
	RI->SetTransform(TT_LocalToWorld,FMatrix::Identity);
	// The level batch turns the triangles of a mirrored tree around, here the culling turns instead.
	RI->SetCullMode(Owner->Determinant < 0.f ? CM_CCW : CM_CW);
	if( !LightsSet )
		RI->EnableLighting(0,SceneNode->Viewport->IsLit(),1,NULL,SceneNode->Viewport->Actor->RendMap == REN_LightingOnly);
	// Twigs and leaves stay with the level batches unless the tree sways, then they have to bend with it.
	FVector	TreeWind = Mesh.Sway > 0.f ? FoliageTreeWind(SceneNode) * Mesh.Sway : FVector(0,0,0);
	UBOOL	Sways = TreeWind.SizeSquared2D() > 0.f;
	// Negative, the flutter of leaves reads how open to the sky a plant's vertex is where a tree has its light.
	FPlane	Wind(TreeWind.X,TreeWind.Y,Actor->Level->TimeSeconds,-1.f / Built->Height);
	// Asking a decal whether it still shows gives it up once its time is over, so it leaves the list right then,
	// as in the retail loops this stands in for.
	AActor* ProjectorBase = Actor->GetProjectorBase();
	for( INT p=0; ShowProjectors && p<ProjectorBase->StaticMeshProjectors.Num(); p++ )
		if( !ProjectorBase->StaticMeshProjectors(p)->RenderInfo->Render(Actor->Level->TimeSeconds) )
		{
			delete ProjectorBase->StaticMeshProjectors(p);
			ProjectorBase->StaticMeshProjectors.Remove(p--);
		}
	// Opaque sections first, then the blended ones with their triangles sorted from the back, as the retail path draws a tree.
	for( INT Pass=0; Pass<2; Pass++ )
	for( INT s=0; s<Mesh.Sections.Num(); s++ )
	{
		FTrunkSection* Section = Mesh.Sections(s);
		if( (!Section && !Sways) || !StaticMesh->Sections(s).NumPrimitives )
			continue;
		UMaterial*		Material = Section && Section->Style->Bark ? Section->Style->Bark->Get() : StaticMesh->GetSkin(Actor,s);
		UBOOL			Sorted = !Section && Material->RequiresSorting() && (Owner->BoundingSphere - SceneNode->ViewOrigin).SizeSquared() < Square(8192.f);
		if( Sorted != Pass )
			continue;
		FVertexStream*	Streams[1] = { Section ? Built->Streams(s) : Built->Cards };
		INT				FirstIndex = Section ? 0 : StaticMesh->Sections(s).FirstIndex,
						NumTriangles = Section ? Section->Indices.Indices.Num() / 3 : StaticMesh->Sections(s).NumPrimitives;
		RI->SetVertexStreams(VS_FixedFunction,Streams,1);
		if( Sorted )
		{
			FSortedIndexBuffer SortedIndices(StaticMesh,s,Owner->WorldToLocal.TransformFVector(SceneNode->ViewOrigin),StaticMesh->Sections(s).MinVertexIndex);
			FirstIndex		= RI->SetDynamicIndexBuffer(&SortedIndices,0);
			NumTriangles	= StaticMesh->Sections(s).NumPrimitives;
		}
		else
			RI->SetIndexBuffer(Section ? &Section->Indices : &StaticMesh->IndexBuffer,0);
		// The instance stream comes before the material, a device builds the material for it.
		if( !RI->SetInstanceStream(&Built->Instance,NULL,NULL,Sways ? &Wind : NULL) )
		{
			RI->PopState();
			return 0;
		}
		RI->SetMaterial(Material);
		RI->DrawInstanced(PT_TriangleList,FirstIndex,NumTriangles,1);

		// What lands on the tree lands on the surface drawn here. A decal is cut out of the retail triangles when
		// it hits, which lie under the rounded bark and stand still in the wind, so it is projected again.
		if( !ShowProjectors || (!Projectors && !ProjectorBase->Projectors.Num() && !ProjectorBase->StaticMeshProjectors.Num()) )
			continue;
		RI->PushState();
		RI->EnableLighting(0,0);
		for( INT p=0; p<ProjectorBase->StaticMeshProjectors.Num(); p++ )
		{
			RI->SetMaterial(ProjectorBase->StaticMeshProjectors(p)->RenderInfo->GetMaterial(SceneNode,Material));
			RI->DrawInstanced(PT_TriangleList,FirstIndex,NumTriangles,1);
		}
		for( INT p=0; LightsSet && p<ProjectorBase->Projectors.Num(); p++ )
		{
			FProjectorRenderInfo* Info = ProjectorBase->Projectors(p);
			if( !Info->Render(Actor->Level->TimeSeconds) )
			{
				ProjectorBase->Projectors.Remove(p--);
				continue;
			}
			RI->SetMaterial(Info->GetMaterial(SceneNode,Material));
			RI->DrawInstanced(PT_TriangleList,FirstIndex,NumTriangles,1);
		}
		for( TList<FProjectorRenderInfo*>* List = LightsSet ? Projectors : NULL; List; List = List->Next )
		{
			FProjectorRenderInfo* Info = List->Element;
			if( !Info->Render(Actor->Level->TimeSeconds) || (Info->Projector->bNoProjectOnOwner && Info->Projector->Owner == Actor) )
				continue;
			RI->SetMaterial(Info->GetMaterial(SceneNode,Material));
			RI->DrawInstanced(PT_TriangleList,FirstIndex,NumTriangles,1);
		}
		RI->PopState();
	}
	RI->SetInstanceStream(NULL);
	RI->PopState();
	return Sways ? (1u << Mesh.Sections.Num()) - 1 : Mesh.Refined;

	unguard;
}

void FFoliage::ForgetLevel(ULevel* Level)
{
	guard(FFoliage::ForgetLevel);

	if( !GTrunks )
		return;
	URenderDevice* RenDev = Level->Engine ? Level->Engine->GRenDev : NULL;
	for( TMap<QWORD,FTrunkActor*>::TIterator It(GTrunks->Actors); It; ++It )
	{
		if( It.Value()->Level != Level )
			continue;
		if( RenDev )
			It.Value()->Flush(RenDev);
		delete It.Value();
		It.RemoveCurrent();
	}

	unguard;
}
