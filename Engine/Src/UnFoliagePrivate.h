/*=============================================================================
	UnFoliagePrivate.h: What the sources that draw from the Foliage folder share.
=============================================================================*/

#define FOLIAGE_DIR		TEXT("../Foliage/")
#define FOLIAGE_INI		TEXT("../Foliage/foliage.ini")

// The editor, hit testing and wireframe views show the retail meshes.
inline UBOOL FoliageEnabled(FLevelSceneNode* SceneNode)
{
	UViewport* Viewport = SceneNode->Viewport;
	return !GIsEditor && !Viewport->HitTesting && !Viewport->IsWire() && Viewport->RenDev->GetRenderCaps()->ImprovedFoliage;
}

// A DDS file's DXT mip chain.
class FFoliageTexture : public FTexture
{
public:
	TArray<BYTE>	File;
	TArray<INT>		MipOffsets,
					MipSizes;
	INT				Width,
					Height;
	ETextureFormat	Format;
	QWORD			CacheId;

	FFoliageTexture() : Width(0), Height(0), Format(TEXF_DXT5), CacheId(MakeCacheID(CID_RenderTexture)) {}

	UBOOL Load(const TCHAR* Filename)
	{
		if( !appLoadFileToArray(File,Filename) || File.Num() < 128 || appMemcmp(&File(0),"DDS ",4) != 0 )
			return 0;
		Height	= *(INT*)&File(12);
		Width	= *(INT*)&File(16);
		INT NumMips = Max(*(INT*)&File(28),1);
		if( Width < 1 || Width > 8192 || Height < 1 || Height > 8192 || NumMips > 14 )
			return 0;
		if( appMemcmp(&File(84),"DXT1",4) == 0 )
			Format = TEXF_DXT1;
		else if( appMemcmp(&File(84),"DXT5",4) != 0 )
			return 0;
		INT Offset = 128;
		for( INT i=0; i<NumMips; i++ )
		{
			INT Size = Max((Width >> i) + 3,4) / 4 * (Max((Height >> i) + 3,4) / 4) * (Format == TEXF_DXT1 ? 8 : 16);
			if( Offset + Size > File.Num() )
				return 0;
			MipOffsets.AddItem(Offset);
			MipSizes.AddItem(Size);
			Offset += Size;
		}
		return 1;
	}

	virtual QWORD GetCacheId() { return CacheId; }
	virtual INT GetRevision() { return 1; }
	virtual INT GetWidth() { return Width; }
	virtual INT GetHeight() { return Height; }
	virtual INT GetFirstMip() { return 0; }
	virtual INT GetNumMips() { return MipOffsets.Num(); }
	virtual ETextureFormat GetFormat() { return Format; }
	virtual ETexClampMode GetUClamp() { return TC_Clamp; }
	virtual ETexClampMode GetVClamp() { return TC_Clamp; }
	virtual void* GetRawTextureData(INT MipIndex) { return &File(MipOffsets(MipIndex)); }
	virtual void UnloadRawTextureData(INT MipIndex) {}
	virtual void GetTextureData(INT MipIndex,void* Dest,INT DestStride,ETextureFormat DestFormat,UBOOL ColoredMips)
	{
		if( DestFormat == Format )
			appMemcpy(Dest,&File(MipOffsets(MipIndex)),MipSizes(MipIndex));
	}
	virtual UTexture* GetUTexture() { return NULL; }
};

// The engine's seeded random stream belongs to the retail scatter, taking numbers from it would move every plant.
struct FFoliageRandom
{
	DWORD State;

	FFoliageRandom(DWORD A,DWORD B,DWORD C) : State(A * 0x9E3779B1 ^ B * 0x85EBCA77 ^ C * 0xC2B2AE3D) {}
	FLOAT Next()
	{
		State ^= State >> 15;
		State *= 0x2C1B3C6D;
		State ^= State >> 12;
		State *= 0x297A2D39;
		State ^= State >> 15;
		return (State >> 8) * (1.f / 16777216.f);
	}
};

// One record of the instance stream, see FRenderInterface::SetInstanceStream.
struct FFoliageInstance
{
	FVector	Position;
	FLOAT	FadeEnd;
	FQuat	Rotation;
	FVector	Scale;
	FLOAT	FadeStart;
	FColor	Color;
};

class FFoliageInstanceStream : public FVertexStream
{
public:
	TArray<FFoliageInstance>	Instances;
	QWORD						CacheId;
	INT							Revision;
	UBOOL						Dynamic;

	// Revision 0 is what a render device takes for already cached.
	FFoliageInstanceStream() : CacheId(MakeCacheID(CID_RenderVertices)), Revision(1), Dynamic(0) {}

	virtual QWORD GetCacheId() { return CacheId; }
	virtual INT GetRevision() { return Revision; }
	virtual INT GetStride() { return sizeof(FFoliageInstance); }
	virtual INT GetSize() { return Instances.Num() * sizeof(FFoliageInstance); }
	virtual UBOOL HintDynamic() { return Dynamic; }
	virtual INT GetComponents(FVertexComponent* Components)
	{
		Components[0] = FVertexComponent(CT_Float4,FVF_Instance0);
		Components[1] = FVertexComponent(CT_Float4,FVF_Instance1);
		Components[2] = FVertexComponent(CT_Float4,FVF_Instance2);
		Components[3] = FVertexComponent(CT_Color,FVF_Instance3);
		return 4;
	}
	virtual void GetStreamData(void* Dest) { appMemcpy(Dest,&Instances(0),GetSize()); }
	virtual void GetRawStreamData(void** Dest,INT FirstVertex) { *Dest = &Instances(FirstVertex); }
};

// The wind on the trees of the level a scene node shows, its length the strength of the ini. Zero where the
// level shows no sign that its trees stand in wind.
FVector FoliageTreeWind(FLevelSceneNode* SceneNode);
