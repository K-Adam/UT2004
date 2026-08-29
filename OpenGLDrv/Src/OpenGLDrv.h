#ifndef HEADER_OPENGLDRV
#define HEADER_OPENGLDRV

#ifdef WIN32
#pragma pack(push,8)
#include <windows.h>
#pragma pack(pop)
#endif
// SDL3.dll uses default packing, the engine builds with /Zp4.
#pragma pack(push,8)
#include <SDL3/SDL.h>
#pragma pack(pop)
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>

#include "Engine.h"

class UOpenGLRenderDevice;

enum
{
	OGL_MAX_TEXTURES	= 16,
	OGL_MAX_MATRICES	= 8,
	OGL_MAX_CONSTANTS	= 8,
	OGL_MAX_LIGHTS		= 8,
	OGL_MAX_STREAMS		= 8,
	OGL_MAX_QUADS		= 5461,
};

enum EOpenGLAttribute
{
	GLA_Position,
	GLA_Normal,
	GLA_Diffuse,
	GLA_Specular,
	GLA_TexCoord0,
	GLA_Max = GLA_TexCoord0 + 8
};

class FOpenGLResource
{
public:
	UOpenGLRenderDevice*	RenDev;
	QWORD				CacheId;
	INT					CachedRevision;

	FOpenGLResource(UOpenGLRenderDevice* InRenDev,QWORD InCacheId);
	virtual ~FOpenGLResource();
};

class FOpenGLTexture : public FOpenGLResource
{
public:
	GLuint	Texture;
	GLenum	Target;
	GLenum	InternalFormat;
	INT		Width,
			Height,
			NumMips;
	GLuint	Framebuffer,
			DepthStencil;
	TArray<INT>	ChildRevisions;

	FOpenGLTexture(UOpenGLRenderDevice* InRenDev,QWORD InCacheId);
	virtual ~FOpenGLTexture();

	void Cache(FBaseTexture* Source);

private:
	void Release();
	void Allocate(GLenum InTarget,GLenum InFormat,INT InWidth,INT InHeight,INT InNumMips);
	void Upload(GLenum Face,INT Level,FTexture* Source,INT SourceMip,ETextureFormat Format,INT MipWidth,INT MipHeight);
};

class FOpenGLVertexStream : public FOpenGLResource
{
public:
	GLuint	Buffer;

	FOpenGLVertexStream(UOpenGLRenderDevice* InRenDev,QWORD InCacheId);
	virtual ~FOpenGLVertexStream();

	void Cache(FVertexStream* Source);
};

class FOpenGLIndexBuffer : public FOpenGLResource
{
public:
	GLuint	Buffer;

	FOpenGLIndexBuffer(UOpenGLRenderDevice* InRenDev,QWORD InCacheId);
	virtual ~FOpenGLIndexBuffer();

	void Cache(FIndexBuffer* Source);
};

// A ring buffer for per-frame data, filled with glBufferSubData and orphaned when full.
struct FOpenGLDynamicBuffer
{
	GLenum	Target;
	GLuint	Buffer;
	INT		Size,
			Used;

	FOpenGLDynamicBuffer(GLenum InTarget) : Target(InTarget), Buffer(0), Size(0), Used(0) {}
	INT Add(const void* Data,INT Bytes,INT Alignment);
	void Release();
};

struct FOpenGLProgram
{
	TArray<BYTE>	Key;
	GLuint			Program;
	GLint			LocalToWorld,
					NormalToWorld,
					AlphaRef,
					Matrices[OGL_MAX_MATRICES],
					Constants[OGL_MAX_CONSTANTS];
};

enum EOpenGLSamplerFlags
{
	GSF_ClampU		= 1,
	GSF_ClampV		= 2,
	GSF_Mipmapped	= 4,
};

struct FOpenGLMaterialState
{
	FOpenGLProgram*	Program;
	INT				NumTextures,
					NumMatrices,
					NumConstants;
	FOpenGLTexture*	Textures[OGL_MAX_TEXTURES];
	BYTE			SamplerFlags[OGL_MAX_TEXTURES];
	FMatrix			Matrices[OGL_MAX_MATRICES];
	FPlane			Constants[OGL_MAX_CONSTANTS];
	INT				GlobalColorConstant;	// The constant SetGlobalColor replaces, or INDEX_NONE.
	GLenum			SrcBlend,
					DstBlend;
	UBOOL			AlphaTest;
	FLOAT			AlphaRef;
	UBOOL			ZWrite,
					ZTest,
					TwoSided;

	void Reset();
};

struct FOpenGLLight
{
	FPlane	Position;	// W is the radius.
	FPlane	Direction;
	FPlane	Color;
	FPlane	Params;		// X is the type, Y the spot cone sine.
};

// Layout of the std140 Scene block in the shaders.
struct FOpenGLSceneBlock
{
	FMatrix	WorldToCamera,
			CameraToScreen;
	FPlane	ClipAdjust,
			Fog,
			FogColor,
			Ambient,
			Options;		// X dynamic lights, Y static lighting, Z 2x lighting, W mip bias.
	FPlane	LightPosition[OGL_MAX_LIGHTS],
			LightDirection[OGL_MAX_LIGHTS],
			LightColor[OGL_MAX_LIGHTS],
			LightParams[OGL_MAX_LIGHTS];
};

struct FOpenGLStream
{
	GLuint				Buffer;
	INT					Stride,
						NumComponents;
	FVertexComponent	Components[MAX_VERTEX_COMPONENTS];
};

class FOpenGLRenderInterface : public FRenderInterface
{
public:
	struct FSavedState
	{
		FOpenGLTexture*		RenderTarget;
		INT					ViewportX,
							ViewportY,
							ViewportWidth,
							ViewportHeight;
		INT					ZBias;

		ECompareFunction	StencilTest;
		EStencilOp			StencilFailOp,
							StencilZFailOp,
							StencilPassOp;
		DWORD				StencilRef,
							StencilMask,
							StencilWriteMask;

		FMatrix				LocalToWorld,
							WorldToCamera,
							CameraToScreen;

		FOpenGLStream			Streams[OGL_MAX_STREAMS];
		INT					NumStreams;
		GLuint				IndexBuffer;
		INT					IndexSize,
							IndexBufferBase;

		ECullMode			CullMode;

		UBOOL				UseDynamicLighting,
							UseStaticLighting,
							LightingModulate2X,
							LightingOnly;
		FOpenGLTexture*		Lightmap;
		FSphere				LitSphere;
		FColor				AmbientLightColor;
		FOpenGLLight			Lights[OGL_MAX_LIGHTS];
		UBOOL				LightEnabled[OGL_MAX_LIGHTS];

		UBOOL				FogEnabled;
		FLOAT				FogStart,
							FogEnd;
		FColor				FogColor;

		FOpenGLMaterialState	Material;
	};

	UOpenGLRenderDevice*	RenDev;
	UViewport*			Viewport;
	enum { MAX_STATE_DEPTH = 128 };
	FSavedState			SavedStates[MAX_STATE_DEPTH];
	FSavedState*		CurrentState;
	INT					SavedStateIndex;
	EPrecacheMode		PrecacheMode;

	TArray<BYTE>		DynamicVertices;
	INT					DynamicStride,
						DynamicSize;
	DWORD				DynamicComponents;

	FOpenGLRenderInterface(UOpenGLRenderDevice* InRenDev);

	void Locked(UViewport* InViewport);
	void Unlocked();

	// FRenderInterface interface.
	virtual void PushState();
	virtual void PopState();
	virtual UBOOL SetRenderTarget(FRenderTarget* RenderTarget);
	virtual void SetViewport(INT X,INT Y,INT Width,INT Height);
	virtual void Clear(UBOOL UseColor,FColor Color,UBOOL UseDepth,FLOAT Depth,UBOOL UseStencil,DWORD Stencil);
	virtual void PushHit(const BYTE* Data,INT Count) {}
	virtual void PopHit(INT Count,UBOOL Force) {}
	virtual void SetCullMode(ECullMode CullMode);
	virtual void SetAmbientLight(FColor Color);
	virtual void EnableLighting(UBOOL UseDynamic,UBOOL UseStatic,UBOOL Modulate2X,FBaseTexture* Lightmap,UBOOL LightingOnly,FSphere LitSphere);
	virtual void SetLight(INT LightIndex,FDynamicLight* Light,FLOAT Scale);
	virtual void SetNPatchTesselation(FLOAT Tesselation) {}
	virtual void SetDistanceFog(UBOOL Enable,FLOAT FogStart,FLOAT FogEnd,FColor Color);
	virtual void SetGlobalColor(FColor Color);
	virtual void SetTransform(ETransformType Type,const FMatrix& Matrix);
	virtual void SetMaterial(UMaterial* Material,FString* ErrorString,UMaterial** ErrorMaterial,INT* NumPasses);
	virtual void SetStencilOp(ECompareFunction Test,DWORD Ref,DWORD Mask,EStencilOp FailOp,EStencilOp ZFailOp,EStencilOp PassOp,DWORD WriteMask);
	virtual void SetPrecacheMode(EPrecacheMode InPrecacheMode) { PrecacheMode = InPrecacheMode; }
	virtual void SetZBias(INT ZBias);
	virtual INT SetVertexStreams(EVertexShader Shader,FVertexStream** Streams,INT NumStreams);
	virtual INT SetDynamicStream(EVertexShader Shader,FVertexStream* Stream);
	virtual INT SetIndexBuffer(FIndexBuffer* IndexBuffer,INT BaseVertexIndex);
	virtual INT SetDynamicIndexBuffer(FIndexBuffer* IndexBuffer,INT BaseVertexIndex);
	virtual void DrawPrimitive(EPrimitiveType PrimitiveType,INT FirstIndex,INT NumPrimitives,INT MinIndex,INT MaxIndex);
	virtual int LockDynBuffer(BYTE** OutBuffer,int NumVerts,int Stride,DWORD ComponentFlags);
	virtual int UnlockDynBuffer();
	virtual void DrawDynQuads(INT NumPrimitives);
	virtual void DrawQuads(INT FirstVertex,INT NumPrimitives);

	FOpenGLTexture* CacheTexture(FBaseTexture* Texture);

private:
	INT AddDynamicVertices(const void* Data,INT Size,INT Stride,const FVertexComponent* Components,INT NumComponents);
	void SetQuadIndexBuffer(INT BaseVertexIndex);
	void Commit();
	UBOOL CompileMaterial(UMaterial* Material,FString* ErrorString,UMaterial** ErrorMaterial);
};

class UOpenGLRenderDevice : public URenderDevice
{
	DECLARE_CLASS(UOpenGLRenderDevice,URenderDevice,CLASS_Config,OpenGLDrv)

	UBOOL	UseVSync,
			UseTrilinear,
			UsePrecaching;
	INT		LevelOfAnisotropy,
			MultisampleSamples;
	FLOAT	DefaultTexMipBias,
			DetailTexMipBias;

	UBOOL	SupportsDXT1,
			SupportsDXT35,
			SupportsAnisotropy;
	INT		MaxTextureSize,
			MaxAnisotropy;

	FRenderCaps				RenderCaps;
	FOpenGLRenderInterface	RenderInterface;
	UViewport*				LockedViewport;
	UBOOL					ContextReady;

	TMap<QWORD,FOpenGLResource*>			Resources;
	TMultiMap<DWORD,FOpenGLProgram*>		Programs;
	TMap<DWORD,GLuint>					Samplers;

	GLuint	VertexArray,
			EmptyVertexArray,
			SceneBuffer,
			QuadIndices,
			VertexShaderObject,
			GammaProgram;
	GLint	GammaRamp;
	FOpenGLDynamicBuffer	DynamicVertexBuffer,
						DynamicIndexBuffer;

	// The scene is drawn here and copied to the window by the gamma pass.
	GLuint	SceneFramebuffer,
			SceneTexture,
			SceneDepthStencil,
			MultisampleFramebuffer,
			MultisampleColor;
	INT		SceneWidth,
			SceneHeight,
			SceneSamples;
	UBOOL	SceneResolved;

	// Last state sent to GL, so Commit only changes what differs.
	struct FGLState
	{
		GLuint			Framebuffer,
						Program,
						Textures[OGL_MAX_TEXTURES],
						Samplers[OGL_MAX_TEXTURES],
						ElementBuffer;
		GLenum			TextureTargets[OGL_MAX_TEXTURES];
		INT				Viewport[4];
		UBOOL			Blend,
						DepthTest,
						Cull,
						Stencil,
						PolygonOffset;
		GLenum			SrcBlend,
						DstBlend,
						DepthFunc,
						CullFace;
		GLboolean		DepthMask;
		INT				ZBias;
		DWORD			StencilKey[7];
		FOpenGLStream		Streams[OGL_MAX_STREAMS];
		INT				NumStreams,
						BaseVertex;
		UBOOL			Attributes[GLA_Max],
						BlackDiffuse;
		FOpenGLSceneBlock	Scene;
		UBOOL			Valid;
	} GL;

	UOpenGLRenderDevice();
	void StaticConstructor();

	// URenderDevice interface.
	virtual UBOOL Init();
	virtual UBOOL SetRes(UViewport* Viewport,INT NewX,INT NewY,UBOOL Fullscreen,INT ColorBytes=0,UBOOL bSaveSize=true);
	virtual void Exit(UViewport* Viewport);
	virtual void Flush(UViewport* Viewport);
	virtual void FlushResource(QWORD CacheId);
	virtual UBOOL ResourceCached(QWORD CacheId);
	virtual void UpdateGamma(UViewport* Viewport) {}
	virtual void RestoreGamma() {}
	virtual UBOOL Exec(const TCHAR* Cmd,FOutputDevice& Ar);
	virtual FRenderInterface* Lock(UViewport* Viewport,BYTE* HitData,INT* HitSize);
	virtual void Unlock(FRenderInterface* RI);
	virtual void Present(UViewport* Viewport);
	virtual void ReadPixels(UViewport* Viewport,FColor* Pixels,UBOOL Flipped);
	virtual void SetEmulationMode(EHardwareEmulationMode Mode) {}
	virtual FRenderCaps* GetRenderCaps();

	FOpenGLResource* GetCachedResource(QWORD CacheId);
	GLuint GetSampler(DWORD Flags);
	GLuint CompileProgram(GLuint VertexShader,const ANSICHAR* FragmentSource);
	void ResolveScene();

private:
	UBOOL InitContext();
	void ResetStateCache();
	void CreateSceneFramebuffer(INT Width,INT Height);
	void DestroySceneFramebuffer();
	void ReleaseObjects();
};

extern const ANSICHAR* OpenGLVertexShader;

#endif
