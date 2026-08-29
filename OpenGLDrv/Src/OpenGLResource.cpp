#include "OpenGLDrv.h"

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

	if( InternalFormat != GL_RGBA8 )
	{
		INT Pitch = ((MipWidth + 3) / 4) * (Format == TEXF_DXT1 ? 8 : 16);
		INT Size = Pitch * ((MipHeight + 3) / 4);
		TArray<BYTE> Data(Size);
		Source->GetTextureData(SourceMip,&Data(0),Pitch,Format);
		glCompressedTexSubImage2D(Face,Level,0,0,MipWidth,MipHeight,InternalFormat,Size,&Data(0));
		return;
	}

	TArray<BYTE> Data(MipWidth * MipHeight * 4);
	appMemzero(&Data(0),Data.Num());
	if( IsDXTC(Format) )
	{
		BYTE* Decompressed = NULL;
		UTexture* Texture = Source->GetUTexture();
		if( Texture )
			Texture->ConvertDXT(SourceMip,1,0,&Decompressed);
		if( Decompressed )
		{
			appMemcpy(&Data(0),Decompressed,Data.Num());
			delete [] Decompressed;
		}
	}
	else if( Format == TEXF_L8 || Format == TEXF_RGB8 )
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
		Source->GetTextureData(SourceMip,&Data(0),MipWidth * 4,TEXF_RGBA8);
	}

	SwapRedBlue(&Data(0),MipWidth * MipHeight);
	glTexSubImage2D(Face,Level,0,0,MipWidth,MipHeight,GL_RGBA,GL_UNSIGNED_BYTE,&Data(0));
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
			Child->GetTextureData(0,&Data(0),ChildWidth * 4,TEXF_RGBA8);
			SwapRedBlue(&Data(0),ChildWidth * ChildHeight);
			glTexSubImage2D(GL_TEXTURE_2D,0,X,Y,ChildWidth,ChildHeight,GL_RGBA,GL_UNSIGNED_BYTE,&Data(0));
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
:	FOpenGLResource(InRenDev,InCacheId), Buffer(0)
{}

FOpenGLVertexStream::~FOpenGLVertexStream()
{
	// The bound VAO drops its references to a deleted buffer.
	RenDev->GL.NumStreams = INDEX_NONE;
	glDeleteBuffers(1,&Buffer);
}

void FOpenGLVertexStream::Cache(FVertexStream* Source)
{
	INT Size = Source->GetSize();
	TArray<BYTE> Data(Size);
	if( Size )
		Source->GetStreamData(&Data(0));
	if( !Buffer )
		glGenBuffers(1,&Buffer);
	glBindBuffer(GL_ARRAY_BUFFER,Buffer);
	glBufferData(GL_ARRAY_BUFFER,Size,Size ? &Data(0) : NULL,Source->HintDynamic() ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW);
	CachedRevision = Source->GetRevision();
}

FOpenGLIndexBuffer::FOpenGLIndexBuffer(UOpenGLRenderDevice* InRenDev,QWORD InCacheId)
:	FOpenGLResource(InRenDev,InCacheId), Buffer(0)
{}

FOpenGLIndexBuffer::~FOpenGLIndexBuffer()
{
	if( RenDev->GL.ElementBuffer == Buffer )
		RenDev->GL.ElementBuffer = (GLuint)-1;
	glDeleteBuffers(1,&Buffer);
}

void FOpenGLIndexBuffer::Cache(FIndexBuffer* Source)
{
	INT Size = Source->GetSize();
	TArray<BYTE> Data(Size);
	if( Size )
		Source->GetContents(&Data(0));
	if( !Buffer )
		glGenBuffers(1,&Buffer);
	// The element binding belongs to the VAO, which is always bound.
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,Buffer);
	RenDev->GL.ElementBuffer = Buffer;
	glBufferData(GL_ELEMENT_ARRAY_BUFFER,Size,Size ? &Data(0) : NULL,GL_STATIC_DRAW);
	CachedRevision = Source->GetRevision();
}

INT FOpenGLDynamicBuffer::Add(const void* Data,INT Bytes,INT Alignment)
{
	INT Offset = ((Used + Alignment - 1) / Alignment) * Alignment;
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
	glBufferSubData(Target,Offset,Bytes,Data);
	Used = Offset + Bytes;
	return Offset;
}

void FOpenGLDynamicBuffer::Release()
{
	if( Buffer )
		glDeleteBuffers(1,&Buffer);
	Buffer = 0;
	Size = Used = 0;
}
