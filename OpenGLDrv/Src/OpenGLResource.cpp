#include "OpenGLDrv.h"
#include <squish.h>

FOpenGLResource::FOpenGLResource(UOpenGLRenderDevice* InRenDev,QWORD InCacheId)
:	RenDev(InRenDev), CacheId(InCacheId), CachedRevision(0)
{
	RenDev->Resources.Set(CacheId,this);
}

FOpenGLResource::~FOpenGLResource()
{
	RenDev->Resources.Remove(CacheId);
}

FOpenGLTexture::FOpenGLTexture(UOpenGLRenderDevice* InRenDev,QWORD InCacheId)
:	FOpenGLResource(InRenDev,InCacheId), Texture(0), Target(GL_TEXTURE_2D), InternalFormat(0)
,	Width(0), Height(0), NumMips(0), Framebuffer(0), DepthStencil(0)
{}

FOpenGLTexture::~FOpenGLTexture()
{
	Release();
}

// Engine textures are BGRA in memory, ES has no BGRA format and texture swizzles are not used.
static void SwapRedBlue(BYTE* Data,INT NumPixels)
{
	for( INT i=0; i<NumPixels; i++, Data+=4 )
		Exchange(Data[0],Data[2]);
}

static void BindForUpload(UOpenGLRenderDevice* RenDev,GLenum Target,GLuint Texture)
{
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(Target,Texture);
	RenDev->GL.Textures[0]			= Texture;
	RenDev->GL.TextureTargets[0]	= Target;
}

void FOpenGLTexture::Release()
{
	if( Texture )
	{
		for( INT i=0; i<OGL_MAX_TEXTURES; i++ )
			if( RenDev->GL.Textures[i] == Texture )
				RenDev->GL.Textures[i] = 0;
		glDeleteTextures(1,&Texture);
	}
	if( Framebuffer )
	{
		if( RenDev->GL.Framebuffer == Framebuffer )
			RenDev->GL.Framebuffer = (GLuint)-1;
		glDeleteFramebuffers(1,&Framebuffer);
	}
	if( DepthStencil )
		glDeleteRenderbuffers(1,&DepthStencil);
	Texture = Framebuffer = DepthStencil = 0;
}

// Immutable storage, so a changed size or format needs a new texture.
void FOpenGLTexture::Allocate(GLenum InTarget,GLenum InFormat,INT InWidth,INT InHeight,INT InNumMips)
{
	Release();
	Target			= InTarget;
	InternalFormat	= InFormat;
	Width			= InWidth;
	Height			= InHeight;
	NumMips			= InNumMips;
	glGenTextures(1,&Texture);
	BindForUpload(RenDev,Target,Texture);
	glTexStorage2D(Target,NumMips,InternalFormat,Width,Height);
}

void FOpenGLTexture::Upload(GLenum Face,INT Level,FTexture* Source,INT SourceMip,ETextureFormat Format,INT MipWidth,INT MipHeight)
{
	if( !Source )
		return;

	if( IsDXTC(Format) )
	{
		INT Pitch = ((MipWidth + 3) / 4) * (Format == TEXF_DXT1 ? 8 : 16);
		INT Size = Pitch * ((MipHeight + 3) / 4);
		TArray<BYTE> Blocks(Size);
		appMemzero(Blocks.GetTypedData(),Size);
		Source->GetTextureData(SourceMip,Blocks.GetTypedData(),Pitch,Format);
		if( InternalFormat != GL_RGBA8 )
		{
			glCompressedTexSubImage2D(Face,Level,0,0,MipWidth,MipHeight,InternalFormat,Size,Blocks.GetTypedData());
			return;
		}
		// Static lightmaps have no UTexture to decompress them, so squish decodes every DXT texture. It writes RGBA, as GL wants it.
		TArray<BYTE> Data(MipWidth * MipHeight * 4);
		squish::DecompressImage(Data.GetTypedData(),MipWidth,MipHeight,Blocks.GetTypedData(),Format == TEXF_DXT1 ? squish::kDxt1 : Format == TEXF_DXT3 ? squish::kDxt3 : squish::kDxt5);
		glTexSubImage2D(Face,Level,0,0,MipWidth,MipHeight,GL_RGBA,GL_UNSIGNED_BYTE,Data.GetTypedData());
		return;
	}

	TArray<BYTE> Data(MipWidth * MipHeight * 4);
	appMemzero(Data.GetTypedData(),Data.Num());
	if( Format == TEXF_L8 || Format == TEXF_RGB8 )
	{
		BYTE* Raw = (BYTE*)Source->GetRawTextureData(SourceMip);
		for( INT i=0; Raw && i<MipWidth*MipHeight; i++ )
		{
			BYTE* Pixel = &Data(i * 4);
			if( Format == TEXF_L8 )
				Pixel[0] = Pixel[1] = Pixel[2] = Raw[i];
			else
				appMemcpy(Pixel,&Raw[i * 3],3);
			Pixel[3] = 255;
		}
		Source->UnloadRawTextureData(SourceMip);
	}
	else
	{
		// The engine only converts these to RGBA8.
		if( Format != TEXF_RGBA8 && Format != TEXF_P8 && Format != TEXF_G16 )
			debugf(NAME_Warning,TEXT("OpenGLDrv: Texture format %i is not supported"),(INT)Format);
		Source->GetTextureData(SourceMip,Data.GetTypedData(),MipWidth * 4,TEXF_RGBA8);
	}

	SwapRedBlue(Data.GetTypedData(),MipWidth * MipHeight);
	glTexSubImage2D(Face,Level,0,0,MipWidth,MipHeight,GL_RGBA,GL_UNSIGNED_BYTE,Data.GetTypedData());
}

void FOpenGLTexture::Cache(FBaseTexture* Source)
{
	guard(FOpenGLTexture::Cache);

	INT SourceWidth		= Source->GetWidth(),
		SourceHeight	= Source->GetHeight();
	CachedRevision = Source->GetRevision();

	if( Source->GetRenderTargetInterface() )
	{
		if( Texture && Width == SourceWidth && Height == SourceHeight )
			return;
		Allocate(GL_TEXTURE_2D,GL_RGBA8,SourceWidth,SourceHeight,1);
		glGenRenderbuffers(1,&DepthStencil);
		glBindRenderbuffer(GL_RENDERBUFFER,DepthStencil);
		glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH24_STENCIL8,Width,Height);
		glGenFramebuffers(1,&Framebuffer);
		glBindFramebuffer(GL_FRAMEBUFFER,Framebuffer);
		glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,Texture,0);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_STENCIL_ATTACHMENT,GL_RENDERBUFFER,DepthStencil);
		if( glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE )
			debugf(NAME_Warning,TEXT("OpenGLDrv: Incomplete render target %ix%i"),Width,Height);
		RenDev->GL.Framebuffer = Framebuffer;
		return;
	}

	if( FCompositeTexture* Composite = Source->GetCompositeTextureInterface() )
	{
		if( !Texture || Width != SourceWidth || Height != SourceHeight )
		{
			Allocate(GL_TEXTURE_2D,GL_RGBA8,SourceWidth,SourceHeight,1);
			ChildRevisions.Empty();
		}
		else
			BindForUpload(RenDev,Target,Texture);

		while( ChildRevisions.Num() < Composite->GetNumChildren() )
			ChildRevisions.AddItem(INDEX_NONE);
		for( INT i=0; i<Composite->GetNumChildren(); i++ )
		{
			INT			X, Y;
			FTexture*	Child = Composite->GetChild(i,&X,&Y);
			if( !Child || ChildRevisions(i) == Child->GetRevision() )
				continue;
			ChildRevisions(i) = Child->GetRevision();
			INT ChildWidth = Child->GetWidth(), ChildHeight = Child->GetHeight();
			TArray<BYTE> Data(ChildWidth * ChildHeight * 4);
			Child->GetTextureData(0,Data.GetTypedData(),ChildWidth * 4,TEXF_RGBA8);
			SwapRedBlue(Data.GetTypedData(),ChildWidth * ChildHeight);
			glTexSubImage2D(GL_TEXTURE_2D,0,X,Y,ChildWidth,ChildHeight,GL_RGBA,GL_UNSIGNED_BYTE,Data.GetTypedData());
		}
		return;
	}

	FCubemap*		Cubemap		= Source->GetCubemapInterface();
	FTexture*		Plain		= Source->GetTextureInterface();
	ETextureFormat	Format		= Source->GetFormat();
	INT				NumSourceMips = Source->GetNumMips(),
					FirstMip	= Source->GetFirstMip();

	while( FirstMip < NumSourceMips - 1 && Max(SourceWidth >> FirstMip,SourceHeight >> FirstMip) > RenDev->MaxTextureSize )
		FirstMip++;
	INT	TopWidth	= Max(1,SourceWidth >> FirstMip),
		TopHeight	= Max(1,SourceHeight >> FirstMip),
		Levels		= Max(1,NumSourceMips - FirstMip),
		MaxLevels	= 1;
	while( Max(TopWidth,TopHeight) >> MaxLevels )
		MaxLevels++;
	Levels = Min(Levels,MaxLevels);

	// ANGLE needs whole blocks on the top level, other sizes are decompressed.
	UBOOL Compressed = ((Format == TEXF_DXT1 && RenDev->SupportsDXT1) || ((Format == TEXF_DXT3 || Format == TEXF_DXT5) && RenDev->SupportsDXT35))
		&& TopWidth % 4 == 0 && TopHeight % 4 == 0;
	GLenum NewFormat = !Compressed ? GL_RGBA8
		: Format == TEXF_DXT1 ? GL_COMPRESSED_RGBA_S3TC_DXT1_EXT
		: Format == TEXF_DXT3 ? GL_COMPRESSED_RGBA_S3TC_DXT3_EXT
		: GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
	GLenum NewTarget = Cubemap ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D;

	if( !Texture || Target != NewTarget || InternalFormat != NewFormat || Width != TopWidth || Height != TopHeight || NumMips != Levels )
		Allocate(NewTarget,NewFormat,TopWidth,TopHeight,Levels);
	else
		BindForUpload(RenDev,Target,Texture);

	for( INT Level=0; Level<Levels; Level++ )
	{
		INT MipWidth = Max(1,TopWidth >> Level), MipHeight = Max(1,TopHeight >> Level);
		if( Cubemap )
			for( INT Face=0; Face<6; Face++ )
				Upload(GL_TEXTURE_CUBE_MAP_POSITIVE_X + Face,Level,Cubemap->GetFace(Face),FirstMip + Level,Format,MipWidth,MipHeight);
		else
			Upload(GL_TEXTURE_2D,Level,Plain,FirstMip + Level,Format,MipWidth,MipHeight);
	}

	unguard;
}

FOpenGLVertexStream::FOpenGLVertexStream(UOpenGLRenderDevice* InRenDev,QWORD InCacheId)
:	FOpenGLResource(InRenDev,InCacheId), Buffer(0), Size(-1)
{}

FOpenGLVertexStream::~FOpenGLVertexStream()
{
	// The bound VAO drops its references to a deleted buffer.
	RenDev->GL.NumStreams = INDEX_NONE;
	glDeleteBuffers(1,&Buffer);
}

#ifdef __EMSCRIPTEN__
// WebGL has no buffer mapping, the data goes through system memory and glBufferSubData.
static TArray<BYTE> UploadScratch;
#endif

// Maps the whole store with invalidation, so a buffer refilled every frame, like a skinned mesh, never waits for the GPU.
// NULL for an empty buffer.
static BYTE* MapWholeBuffer(GLenum Target,GLuint Buffer,INT& Size,INT NewSize,GLenum Usage)
{
	glBindBuffer(Target,Buffer);
	if( Size != NewSize )
	{
		glBufferData(Target,NewSize,NULL,Usage);
		Size = NewSize;
	}
#ifdef __EMSCRIPTEN__
	if( UploadScratch.Num() < Size )
		UploadScratch.Add(Size - UploadScratch.Num());
	return Size ? UploadScratch.GetTypedData() : NULL;
#else
	return Size ? (BYTE*)glMapBufferRange(Target,0,Size,GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT) : NULL;
#endif
}

static void UnmapWholeBuffer(GLenum Target,INT Size)
{
#ifdef __EMSCRIPTEN__
	glBufferSubData(Target,0,Size,UploadScratch.GetTypedData());
#else
	glUnmapBuffer(Target);
#endif
}

void FOpenGLVertexStream::Cache(FVertexStream* Source)
{
	// ANGLE keeps a buffer's contents when its size changes, and reads them back from the GPU for that, which waits
	// for the frames in flight. A new buffer has nothing to keep, and the bound streams may name the old one.
	if( Buffer && Size != Source->GetSize() )
	{
		RenDev->GL.NumStreams = INDEX_NONE;
		glDeleteBuffers(1,&Buffer);
		Buffer = 0;
	}
	if( !Buffer )
		glGenBuffers(1,&Buffer);
	if( BYTE* Dest = MapWholeBuffer(GL_ARRAY_BUFFER,Buffer,Size,Source->GetSize(),Source->HintDynamic() ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW) )
	{
		Source->GetStreamData(Dest);
		UnmapWholeBuffer(GL_ARRAY_BUFFER,Size);
	}
	CachedRevision = Source->GetRevision();
}

// A copy of the stream in which vertices at one position share the sum of their normals, kept beside
// the original under an id no engine resource has.
GLuint UOpenGLRenderDevice::SmoothStream(FVertexStream* Source)
{
	QWORD					Id = Source->GetCacheId() | ((QWORD)1 << 63);
	FOpenGLVertexStream*	Stream = (FOpenGLVertexStream*)GetCachedResource(Id);
	if( !Stream )
		Stream = new FOpenGLVertexStream(this,Id);
	if( Stream->Buffer && Stream->CachedRevision == Source->GetRevision() )
		return Stream->Buffer;

	FVertexComponent Components[MAX_VERTEX_COMPONENTS];
	INT NumComponents = Source->GetComponents(Components), Stride = Source->GetStride(), Position = INDEX_NONE, Normal = INDEX_NONE, Offset = 0;
	for( INT i=0; i<NumComponents; i++ )
	{
		if( Components[i].Function == FVF_Position )
			Position = Offset;
		if( Components[i].Function == FVF_Normal )
			Normal = Offset;
		Offset += Components[i].Type == CT_Color ? 4 : (4 - Components[i].Type) * sizeof(FLOAT);
	}

	TArray<BYTE> Data;
	Data.Add(Source->GetSize());
	if( Data.Num() )
		Source->GetStreamData(Data.GetTypedData());
	if( Position != INDEX_NONE && Normal != INDEX_NONE )
	{
		TMap<DWORD,FVector> Sums;
		for( INT Pass=0; Pass<2; Pass++ )
			for( INT i=0; i+Stride<=Data.Num(); i+=Stride )
			{
				FVector&	N = *(FVector*)&Data(i + Normal);
				DWORD		Hash = appMemCrc(&Data(i + Position),sizeof(FVector));
				FVector*	Sum = Sums.Find(Hash);
				if( Pass == 0 )
				{
					if( Sum )
						*Sum += N;
					else
						Sums.Set(Hash,N);
				}
				// Opposed normals cancel out and leave no direction.
				else if( Sum && Sum->SizeSquared() > 0.01f )
					N = Sum->SafeNormal();
			}
	}

	if( !Stream->Buffer )
		glGenBuffers(1,&Stream->Buffer);
	glBindBuffer(GL_ARRAY_BUFFER,Stream->Buffer);
	glBufferData(GL_ARRAY_BUFFER,Data.Num(),Data.Num() ? Data.GetTypedData() : NULL,GL_STATIC_DRAW);
	Stream->Size			= Data.Num();
	Stream->CachedRevision	= Source->GetRevision();
	return Stream->Buffer;
}

FOpenGLIndexBuffer::FOpenGLIndexBuffer(UOpenGLRenderDevice* InRenDev,QWORD InCacheId)
:	FOpenGLResource(InRenDev,InCacheId), Buffer(0), Size(-1)
{}

FOpenGLIndexBuffer::~FOpenGLIndexBuffer()
{
	if( RenDev->GL.ElementBuffer == Buffer )
		RenDev->GL.ElementBuffer = (GLuint)-1;
	glDeleteBuffers(1,&Buffer);
}

void FOpenGLIndexBuffer::Cache(FIndexBuffer* Source)
{
	if( !Buffer )
		glGenBuffers(1,&Buffer);
	// The element binding belongs to the VAO, which is always bound.
	if( BYTE* Dest = MapWholeBuffer(GL_ELEMENT_ARRAY_BUFFER,Buffer,Size,Source->GetSize(),GL_STATIC_DRAW) )
	{
		Source->GetContents(Dest);
		UnmapWholeBuffer(GL_ELEMENT_ARRAY_BUFFER,Size);
	}
	RenDev->GL.ElementBuffer = Buffer;
	CachedRevision = Source->GetRevision();
}

BYTE* FOpenGLDynamicBuffer::Lock(INT Bytes,INT Alignment,INT& Offset)
{
	Offset = ((Used + Alignment - 1) / Alignment) * Alignment;
	if( !Buffer )
		glGenBuffers(1,&Buffer);
	glBindBuffer(Target,Buffer);
	if( Offset + Bytes > Size )
	{
		// Orphaning gives a fresh store, the GPU may still read the old one.
		Size = Max(Size,Max(Bytes,1 << 20));
		glBufferData(Target,Size,NULL,GL_DYNAMIC_DRAW);
		Offset = 0;
	}
	Used = Offset + Bytes;
#if defined(_WIN32) || defined(__EMSCRIPTEN__)
	// Not mapped: ANGLE maps into a staging copy and then copies the whole buffer back before
	// every draw that reads it. Uploading with glBufferSubData keeps its system memory copy current.
	// WebGL has no mapping at all.
	LockedOffset	= Offset;
	LockedBytes		= Bytes;
	if( Scratch.Num() < Bytes )
		Scratch.Add(Bytes - Scratch.Num());
	return Bytes > 0 ? Scratch.GetTypedData() : NULL;
#else
	// Unsynchronized, nothing has drawn from this range since the store was orphaned.
	// Native drivers, phone ones above all, may copy or wait on glBufferSubData into a buffer the GPU still reads.
	BYTE* Result = Bytes > 0 ? (BYTE*)glMapBufferRange(Target,Offset,Bytes,GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_RANGE_BIT | GL_MAP_UNSYNCHRONIZED_BIT) : NULL;
	LockedBytes = Result ? Bytes : 0;
	return Result;
#endif
}

void FOpenGLDynamicBuffer::Unlock()
{
	// Lock left the buffer bound.
	if( LockedBytes > 0 )
#if defined(_WIN32) || defined(__EMSCRIPTEN__)
		glBufferSubData(Target,LockedOffset,LockedBytes,Scratch.GetTypedData());
#else
		glUnmapBuffer(Target);
#endif
	LockedBytes = 0;
}

void FOpenGLDynamicBuffer::Release()
{
	if( Buffer )
		glDeleteBuffers(1,&Buffer);
	Buffer = 0;
	Size = Used = LockedBytes = 0;
	Scratch.Empty();
}
