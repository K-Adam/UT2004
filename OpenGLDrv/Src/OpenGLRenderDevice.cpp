// SDLDrv creates the window and the ES context, through ANGLE on Windows. The device draws into
// its own framebuffer, which a final pass copies to the window while applying gamma, so brightness
// works without OS gamma ramps.

#include "OpenGLDrv.h"

IMPLEMENT_CLASS(UOpenGLRenderDevice);

static const ANSICHAR* GammaVertexShader =
	"#version 300 es\n"
	"out vec2 UV;\n"
	"void main()\n"
	"{\n"
	"	vec2 P = vec2(gl_VertexID == 1 ? 3.0 : -1.0, gl_VertexID == 2 ? 3.0 : -1.0);\n"
	"	UV = P * 0.5 + 0.5;\n"
	"	gl_Position = vec4(P, 0.0, 1.0);\n"
	"}\n";

static const ANSICHAR* GammaFragmentShader =
	"#version 300 es\n"
	"precision mediump float;\n"
	"uniform sampler2D Frame;\n"
	"uniform vec3 Ramp;\n"
	"in vec2 UV;\n"
	"out vec4 FragColor;\n"
	"void main()\n"
	"{\n"
	"	vec3 C = texture(Frame, UV).rgb;\n"
	"	FragColor = vec4(clamp(Ramp.x * pow(C, vec3(Ramp.y)) + Ramp.z, 0.0, 1.0), 1.0);\n"
	"}\n";

UOpenGLRenderDevice::UOpenGLRenderDevice()
:	RenderInterface(this)
,	DynamicVertexBuffer(GL_ARRAY_BUFFER)
,	DynamicIndexBuffer(GL_ELEMENT_ARRAY_BUFFER)
{
	LockedViewport	= NULL;
	ContextReady	= 0;
	VertexArray = EmptyVertexArray = SceneBuffer = QuadIndices = GammaProgram = VertexShaderObject = 0;
	SceneFramebuffer = SceneTexture = SceneDepthStencil = MultisampleFramebuffer = MultisampleColor = 0;
	SceneWidth = SceneHeight = SceneSamples = 0;
}

void UOpenGLRenderDevice::StaticConstructor()
{
	new(GetClass(),TEXT("UseVSync"),			RF_Public)UBoolProperty	(CPP_PROPERTY(UseVSync),			TEXT("Options"),CPF_Config);
	new(GetClass(),TEXT("UseTrilinear"),		RF_Public)UBoolProperty	(CPP_PROPERTY(UseTrilinear),		TEXT("Options"),CPF_Config);
	new(GetClass(),TEXT("UsePrecaching"),		RF_Public)UBoolProperty	(CPP_PROPERTY(UsePrecaching),		TEXT("Options"),CPF_Config);
	new(GetClass(),TEXT("LevelOfAnisotropy"),	RF_Public)UIntProperty	(CPP_PROPERTY(LevelOfAnisotropy),	TEXT("Options"),CPF_Config);
	new(GetClass(),TEXT("MultisampleSamples"),	RF_Public)UIntProperty	(CPP_PROPERTY(MultisampleSamples),	TEXT("Options"),CPF_Config);
	new(GetClass(),TEXT("DefaultTexMipBias"),	RF_Public)UFloatProperty(CPP_PROPERTY(DefaultTexMipBias),	TEXT("Options"),CPF_Config);
	new(GetClass(),TEXT("DetailTexMipBias"),	RF_Public)UFloatProperty(CPP_PROPERTY(DetailTexMipBias),	TEXT("Options"),CPF_Config);
}

UBOOL UOpenGLRenderDevice::Init()
{
	guard(UOpenGLRenderDevice::Init);

	// The shaders give the engine D3D conventions, see OpenGLVertexShader.
	GIsOpenGL = 0;

	SupportsCubemaps				= 1;
	SupportsZBIAS					= 1;
	SupportsRenderToTextureRGBA8888	= 1;
	SupportsRenderToTextureRGB565	= 0;
	SupportsRenderToTextureNPOT		= 1;
	return 1;

	unguard;
}

static UBOOL HasExtension(const char* Name)
{
	GLint Count = 0;
	glGetIntegerv(GL_NUM_EXTENSIONS,&Count);
	for( GLint i=0; i<Count; i++ )
		if( strcmp((const char*)glGetStringi(GL_EXTENSIONS,i),Name) == 0 )
			return 1;
	return 0;
}

static GLuint CompileShader(GLenum Type,const ANSICHAR* Source)
{
	GLuint Shader = glCreateShader(Type);
	glShaderSource(Shader,1,&Source,NULL);
	glCompileShader(Shader);
	GLint Status = 0;
	glGetShaderiv(Shader,GL_COMPILE_STATUS,&Status);
	if( !Status )
	{
		ANSICHAR Log[4096] = "";
		glGetShaderInfoLog(Shader,sizeof(Log),NULL,Log);
		debugf(NAME_Warning,TEXT("OpenGLDrv: Shader compile failed: %s"),appFromAnsi(Log));
		// Logged line by line, the log truncates long lines.
		FString Text(Source);
		for( INT Pos = Text.InStr(TEXT("\n")); Pos >= 0; Pos = Text.InStr(TEXT("\n")) )
		{
			debugf(NAME_Warning,TEXT("    %s"),*Text.Left(Pos));
			Text = Text.Mid(Pos + 1);
		}
		glDeleteShader(Shader);
		return 0;
	}
	return Shader;
}

GLuint UOpenGLRenderDevice::CompileProgram(GLuint VertexShader,const ANSICHAR* FragmentSource)
{
	GLuint FragmentShader = CompileShader(GL_FRAGMENT_SHADER,FragmentSource);
	if( !VertexShader || !FragmentShader )
		return 0;

	static const ANSICHAR* Attributes[GLA_Max] = { "aPosition", "aNormal", "aDiffuse", "aSpecular",
		"aTexCoord0", "aTexCoord1", "aTexCoord2", "aTexCoord3", "aTexCoord4", "aTexCoord5", "aTexCoord6", "aTexCoord7" };
	GLuint Program = glCreateProgram();
	glAttachShader(Program,VertexShader);
	glAttachShader(Program,FragmentShader);
	for( INT i=0; i<GLA_Max; i++ )
		glBindAttribLocation(Program,i,Attributes[i]);
	glLinkProgram(Program);
	glDetachShader(Program,VertexShader);
	glDetachShader(Program,FragmentShader);
	glDeleteShader(FragmentShader);

	GLint Status = 0;
	glGetProgramiv(Program,GL_LINK_STATUS,&Status);
	if( !Status )
	{
		ANSICHAR Log[4096] = "";
		glGetProgramInfoLog(Program,sizeof(Log),NULL,Log);
		debugf(NAME_Warning,TEXT("OpenGLDrv: Program link failed: %s"),appFromAnsi(Log));
		glDeleteProgram(Program);
		return 0;
	}
	GLuint Block = glGetUniformBlockIndex(Program,"Scene");
	if( Block != GL_INVALID_INDEX )
		glUniformBlockBinding(Program,Block,0);
	return Program;
}

GLuint UOpenGLRenderDevice::GetSampler(DWORD Flags)
{
	if( GLuint* Found = Samplers.Find(Flags) )
		return *Found;

	GLuint Sampler;
	glGenSamplers(1,&Sampler);
	glSamplerParameteri(Sampler,GL_TEXTURE_WRAP_S,(Flags & GSF_ClampU) ? GL_CLAMP_TO_EDGE : GL_REPEAT);
	glSamplerParameteri(Sampler,GL_TEXTURE_WRAP_T,(Flags & GSF_ClampV) ? GL_CLAMP_TO_EDGE : GL_REPEAT);
	glSamplerParameteri(Sampler,GL_TEXTURE_WRAP_R,GL_CLAMP_TO_EDGE);
	glSamplerParameteri(Sampler,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
	glSamplerParameteri(Sampler,GL_TEXTURE_MIN_FILTER,!(Flags & GSF_Mipmapped) ? GL_LINEAR : UseTrilinear ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR_MIPMAP_NEAREST);
	if( SupportsAnisotropy && LevelOfAnisotropy > 1 )
		glSamplerParameterf(Sampler,GL_TEXTURE_MAX_ANISOTROPY_EXT,(FLOAT)Min(LevelOfAnisotropy,MaxAnisotropy));
	Samplers.Set(Flags,Sampler);
	return Sampler;
}

// The state of a fresh context, with the object bindings forced to rebind.
void UOpenGLRenderDevice::ResetStateCache()
{
	appMemzero(&GL,sizeof(GL));
	GL.Framebuffer	= (GLuint)-1;
	GL.Program		= (GLuint)-1;
	GL.ElementBuffer= (GLuint)-1;
	GL.DepthFunc	= GL_LESS;
	GL.DepthMask	= GL_TRUE;
	GL.CullFace		= GL_BACK;
	GL.SrcBlend		= GL_ONE;
	GL.DstBlend		= GL_ZERO;
	GL.NumStreams	= INDEX_NONE;
	for( INT i=0; i<4; i++ )
		GL.Viewport[i] = -1;
	appMemset(GL.Textures,0xff,sizeof(GL.Textures));
	appMemset(GL.Samplers,0xff,sizeof(GL.Samplers));
	appMemset(GL.StencilKey,0xff,sizeof(GL.StencilKey));
}

UBOOL UOpenGLRenderDevice::InitContext()
{
	debugf(TEXT("GL_VENDOR     : %s"),appFromAnsi((const ANSICHAR*)glGetString(GL_VENDOR)));
	debugf(TEXT("GL_RENDERER   : %s"),appFromAnsi((const ANSICHAR*)glGetString(GL_RENDERER)));
	debugf(TEXT("GL_VERSION    : %s"),appFromAnsi((const ANSICHAR*)glGetString(GL_VERSION)));

	SupportsDXT1		= HasExtension("GL_EXT_texture_compression_s3tc") || HasExtension("GL_EXT_texture_compression_dxt1");
	SupportsDXT35		= HasExtension("GL_EXT_texture_compression_s3tc") || (HasExtension("GL_ANGLE_texture_compression_dxt3") && HasExtension("GL_ANGLE_texture_compression_dxt5"));
	SupportsAnisotropy	= HasExtension("GL_EXT_texture_filter_anisotropic");
	glGetIntegerv(GL_MAX_TEXTURE_SIZE,&MaxTextureSize);
	MaxAnisotropy = 1;
	if( SupportsAnisotropy )
	{
		GLfloat Max = 1.f;
		glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT,&Max);
		MaxAnisotropy = appFloor(Max);
	}
	debugf(TEXT("OpenGLDrv: DXT1 %i, DXT3/5 %i, anisotropy %i, max texture %i"),SupportsDXT1,SupportsDXT35,MaxAnisotropy,MaxTextureSize);

	VertexShaderObject = CompileShader(GL_VERTEX_SHADER,OpenGLVertexShader);
	GLuint GammaVertex = CompileShader(GL_VERTEX_SHADER,GammaVertexShader);
	GammaProgram = CompileProgram(GammaVertex,GammaFragmentShader);
	glDeleteShader(GammaVertex);
	if( !VertexShaderObject || !GammaProgram )
		return 0;
	GammaRamp = glGetUniformLocation(GammaProgram,"Ramp");

	glGenVertexArrays(1,&EmptyVertexArray);
	glGenVertexArrays(1,&VertexArray);
	glBindVertexArray(VertexArray);
	glGenBuffers(1,&SceneBuffer);
	glBindBuffer(GL_UNIFORM_BUFFER,SceneBuffer);
	glBufferData(GL_UNIFORM_BUFFER,sizeof(FOpenGLSceneBlock),NULL,GL_DYNAMIC_DRAW);
	glBindBufferBase(GL_UNIFORM_BUFFER,0,SceneBuffer);

	glPixelStorei(GL_UNPACK_ALIGNMENT,1);
	glPixelStorei(GL_PACK_ALIGNMENT,1);
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_SCISSOR_TEST);
	// Attributes a stream lacks read these, D3D treats missing vertex colours as white.
	glVertexAttrib4f(GLA_Diffuse,1.f,1.f,1.f,1.f);
	glVertexAttrib4f(GLA_Specular,1.f,1.f,1.f,1.f);

	ResetStateCache();
	ContextReady = 1;
	return 1;
}

void UOpenGLRenderDevice::CreateSceneFramebuffer(INT Width,INT Height)
{
	GLint MaxSamples = 0;
	glGetIntegerv(GL_MAX_SAMPLES,&MaxSamples);
	INT Samples = MultisampleSamples > 1 ? Min<INT>(MultisampleSamples,MaxSamples) : 0;
	if( SceneFramebuffer && Width == SceneWidth && Height == SceneHeight && Samples == SceneSamples )
		return;

	DestroySceneFramebuffer();
	SceneWidth		= Width;
	SceneHeight		= Height;
	SceneSamples	= Samples;

	glGenTextures(1,&SceneTexture);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D,SceneTexture);
	glTexStorage2D(GL_TEXTURE_2D,1,GL_RGBA8,Width,Height);
	GL.Textures[0]			= SceneTexture;
	GL.TextureTargets[0]	= GL_TEXTURE_2D;
	glGenFramebuffers(1,&SceneFramebuffer);
	glBindFramebuffer(GL_FRAMEBUFFER,SceneFramebuffer);
	glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,SceneTexture,0);

	glGenRenderbuffers(1,&SceneDepthStencil);
	glBindRenderbuffer(GL_RENDERBUFFER,SceneDepthStencil);
	if( SceneSamples )
	{
		glRenderbufferStorageMultisample(GL_RENDERBUFFER,SceneSamples,GL_DEPTH24_STENCIL8,Width,Height);
		glGenRenderbuffers(1,&MultisampleColor);
		glBindRenderbuffer(GL_RENDERBUFFER,MultisampleColor);
		glRenderbufferStorageMultisample(GL_RENDERBUFFER,SceneSamples,GL_RGBA8,Width,Height);
		glGenFramebuffers(1,&MultisampleFramebuffer);
		glBindFramebuffer(GL_FRAMEBUFFER,MultisampleFramebuffer);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_RENDERBUFFER,MultisampleColor);
	}
	else
		glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH24_STENCIL8,Width,Height);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_STENCIL_ATTACHMENT,GL_RENDERBUFFER,SceneDepthStencil);

	if( glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE )
		debugf(NAME_Warning,TEXT("OpenGLDrv: Incomplete scene framebuffer %ix%i, %i samples"),Width,Height,SceneSamples);
	GL.Framebuffer = SceneSamples ? MultisampleFramebuffer : SceneFramebuffer;
	debugf(TEXT("OpenGLDrv: Scene %ix%i, %i samples"),Width,Height,SceneSamples);
}

void UOpenGLRenderDevice::DestroySceneFramebuffer()
{
	if( SceneFramebuffer )			glDeleteFramebuffers(1,&SceneFramebuffer);
	if( MultisampleFramebuffer )	glDeleteFramebuffers(1,&MultisampleFramebuffer);
	if( SceneTexture )				glDeleteTextures(1,&SceneTexture);
	if( SceneDepthStencil )			glDeleteRenderbuffers(1,&SceneDepthStencil);
	if( MultisampleColor )			glDeleteRenderbuffers(1,&MultisampleColor);
	SceneFramebuffer = MultisampleFramebuffer = SceneTexture = SceneDepthStencil = MultisampleColor = 0;
	GL.Framebuffer = (GLuint)-1;
	for( INT i=0; i<OGL_MAX_TEXTURES; i++ )
		GL.Textures[i] = (GLuint)-1;
}

UBOOL UOpenGLRenderDevice::SetRes(UViewport* Viewport,INT NewX,INT NewY,UBOOL Fullscreen,INT ColorBytes,UBOOL bSaveSize)
{
	guard(UOpenGLRenderDevice::SetRes);

	if( LockedViewport )
		appErrorf(TEXT("Can't change resolution while render device is locked!"));
	if( appStricmp(Viewport->GetClass()->GetName(),TEXT("SDLViewport")) != 0 )
	{
		debugf(TEXT("OpenGLDrv: Needs the SDL viewport of ut2004-bin"));
		return 0;
	}

	// On Windows this makes SDL use ANGLE's EGL instead of a native ES context from the driver.
	SDL_SetHint(SDL_HINT_OPENGL_ES_DRIVER,"1");
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_ES);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,0);
	SDL_GL_SetAttribute(SDL_GL_RED_SIZE,8);
	SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE,8);
	SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE,8);
	// The scene framebuffer has the depth and stencil, the window only receives the gamma pass.
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,0);
	SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE,0);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER,1);
	SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS,0);
	SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES,0);

	if( !Viewport->ResizeViewport((Fullscreen ? BLIT_Fullscreen : 0) | BLIT_OpenGL,NewX,NewY,bSaveSize) )
		return 0;

	// SDLDrv keeps the first context it made, which is a desktop one after OpenGLDrv ran.
	const ANSICHAR* Version = (const ANSICHAR*)glGetString(GL_VERSION);
	if( !Version || strncmp(Version,"OpenGL ES 3",11) != 0 )
	{
		debugf(TEXT("OpenGLDrv: No OpenGL ES 3 context (%s)"),Version ? appFromAnsi(Version) : TEXT("none"));
		return 0;
	}
	if( !ContextReady && !InitContext() )
		return 0;

	CreateSceneFramebuffer(Viewport->SizeX,Viewport->SizeY);
	SDL_GL_SetSwapInterval(UseVSync ? 1 : 0);
	Viewport->PendingFrame = 0;
	return 1;

	unguard;
}

void UOpenGLRenderDevice::ReleaseObjects()
{
	for( TMap<DWORD,GLuint>::TIterator It(Samplers); It; ++It )
		glDeleteSamplers(1,&It.Value());
	Samplers.Empty();
	DestroySceneFramebuffer();
	DynamicVertexBuffer.Release();
	DynamicIndexBuffer.Release();
	if( QuadIndices )			glDeleteBuffers(1,&QuadIndices);
	if( SceneBuffer )			glDeleteBuffers(1,&SceneBuffer);
	if( VertexArray )			glDeleteVertexArrays(1,&VertexArray);
	if( EmptyVertexArray )		glDeleteVertexArrays(1,&EmptyVertexArray);
	if( GammaProgram )			glDeleteProgram(GammaProgram);
	if( VertexShaderObject )	glDeleteShader(VertexShaderObject);
	VertexArray = EmptyVertexArray = SceneBuffer = QuadIndices = GammaProgram = VertexShaderObject = 0;
	SceneWidth = SceneHeight = SceneSamples = 0;
}

void UOpenGLRenderDevice::Exit(UViewport* Viewport)
{
	guard(UOpenGLRenderDevice::Exit);
	if( ContextReady )
	{
		Flush(Viewport);
		ReleaseObjects();
		ContextReady = 0;
	}
	unguard;
}

void UOpenGLRenderDevice::Flush(UViewport* Viewport)
{
	guard(UOpenGLRenderDevice::Flush);

	TArray<FOpenGLResource*> All;
	for( TMap<QWORD,FOpenGLResource*>::TIterator It(Resources); It; ++It )
		All.AddItem(It.Value());
	for( INT i=0; i<All.Num(); i++ )
		delete All(i);

	for( TMultiMap<DWORD,FOpenGLProgram*>::TIterator It(Programs); It; ++It )
	{
		if( It.Value()->Program )
			glDeleteProgram(It.Value()->Program);
		delete It.Value();
	}
	Programs.Empty();
	GL.Program = (GLuint)-1;
	GL.NumStreams = INDEX_NONE;

	// The base state outlives frames, drop what it points at.
	FOpenGLRenderInterface::FSavedState& Base = RenderInterface.SavedStates[0];
	Base.Material.Reset();
	Base.RenderTarget			= NULL;
	Base.Lightmap				= NULL;
	Base.NumStreams				= 0;
	Base.IndexBuffer			= 0;

	PrecacheOnFlip = UsePrecaching;

	unguard;
}

FOpenGLResource* UOpenGLRenderDevice::GetCachedResource(QWORD CacheId)
{
	FOpenGLResource** Found = Resources.Find(CacheId);
	return Found ? *Found : NULL;
}

void UOpenGLRenderDevice::FlushResource(QWORD CacheId)
{
	delete GetCachedResource(CacheId);
}

UBOOL UOpenGLRenderDevice::ResourceCached(QWORD CacheId)
{
	return GetCachedResource(CacheId) != NULL;
}

UBOOL UOpenGLRenderDevice::Exec(const TCHAR* Cmd,FOutputDevice& Ar)
{
	if( ParseCommand(&Cmd,TEXT("SUPPORTEDRESOLUTION")) )
	{
		INT Width = 0, Height = 0, BitDepth = 0;
		// SDLDrv picks the closest display mode.
		Ar.Logf(TEXT("%u"),Parse(Cmd,TEXT("WIDTH="),Width) && Parse(Cmd,TEXT("HEIGHT="),Height) && Parse(Cmd,TEXT("BITDEPTH="),BitDepth));
		return 1;
	}
	return 0;
}

FRenderInterface* UOpenGLRenderDevice::Lock(UViewport* Viewport,BYTE* HitData,INT* HitSize)
{
	guard(UOpenGLRenderDevice::Lock);

	// User resizing reaches the viewport without SetRes.
	if( Viewport->SizeX != SceneWidth || Viewport->SizeY != SceneHeight )
		CreateSceneFramebuffer(Viewport->SizeX,Viewport->SizeY);
	LockedViewport = Viewport;
	SceneResolved = 0;
	RenderInterface.Locked(Viewport);
	return &RenderInterface;

	unguard;
}

void UOpenGLRenderDevice::Unlock(FRenderInterface* RI)
{
	RenderInterface.Unlocked();
	LockedViewport = NULL;
}

void UOpenGLRenderDevice::ResolveScene()
{
	if( SceneSamples && !SceneResolved )
	{
		glDisable(GL_SCISSOR_TEST);
		glBindFramebuffer(GL_READ_FRAMEBUFFER,MultisampleFramebuffer);
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER,SceneFramebuffer);
		glBlitFramebuffer(0,0,SceneWidth,SceneHeight,0,0,SceneWidth,SceneHeight,GL_COLOR_BUFFER_BIT,GL_NEAREST);
		glEnable(GL_SCISSOR_TEST);
		glBindFramebuffer(GL_FRAMEBUFFER,MultisampleFramebuffer);
		GLenum Attachments[] = { GL_COLOR_ATTACHMENT0, GL_DEPTH_STENCIL_ATTACHMENT };
		glInvalidateFramebuffer(GL_FRAMEBUFFER,2,Attachments);
		GL.Framebuffer = MultisampleFramebuffer;
	}
	SceneResolved = 1;
}

void UOpenGLRenderDevice::Present(UViewport* Viewport)
{
	guard(UOpenGLRenderDevice::Present);

	SDL_Window* Window = SDL_GL_GetCurrentWindow();
	if( !SceneFramebuffer || !Window )
		return;
	ResolveScene();

	int Width = SceneWidth, Height = SceneHeight;
	SDL_GetWindowSizeInPixels(Window,&Width,&Height);
	glBindFramebuffer(GL_FRAMEBUFFER,0);
	glViewport(0,0,Width,Height);
	glDisable(GL_SCISSOR_TEST);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);
	glDisable(GL_CULL_FACE);
	glDisable(GL_STENCIL_TEST);
	glUseProgram(GammaProgram);
	UClient* Client = Viewport->GetOuterUClient();
	UBOOL Neutral = ParseParam(appCmdLine(),TEXT("NOGAMMA"));
	FLOAT Gamma = Neutral ? 1.f : Client->Gamma, Brightness = Neutral ? 0.5f : Client->Brightness, Contrast = Neutral ? 0.5f : Client->Contrast;
	// D3D9Drv's gamma ramp: (Contrast + 0.5) * x ^ (1 / Gamma) + (Brightness - 0.5) / 2 - Contrast / 2 + 0.25
	glUniform3f(GammaRamp,Contrast + 0.5f,1.f / Max(Gamma,0.1f),(Brightness - 0.5f) * 0.5f - Contrast * 0.5f + 0.25f);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D,SceneTexture);
	GLuint Sampler = GetSampler(GSF_ClampU | GSF_ClampV);
	glBindSampler(0,Sampler);
	// Attribute free, so the scene's enabled arrays stay out of it.
	glBindVertexArray(EmptyVertexArray);
	glDrawArrays(GL_TRIANGLES,0,3);
	glBindVertexArray(VertexArray);
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_SCISSOR_TEST);

	GL.Framebuffer			= 0;
	GL.Program				= GammaProgram;
	GL.Textures[0]			= SceneTexture;
	GL.TextureTargets[0]	= GL_TEXTURE_2D;
	GL.Samplers[0]			= Sampler;
	GL.Blend = GL.Cull = GL.Stencil = 0;
	appMemset(GL.Viewport,0xff,sizeof(GL.Viewport));

	SDL_GL_SwapWindow(Window);

	unguard;
}

void UOpenGLRenderDevice::ReadPixels(UViewport* Viewport,FColor* Pixels,UBOOL Flipped)
{
	guard(UOpenGLRenderDevice::ReadPixels);

	if( !Viewport || !Pixels || !SceneFramebuffer )
		return;
	ResolveScene();
	glBindFramebuffer(GL_FRAMEBUFFER,SceneFramebuffer);
	GL.Framebuffer = SceneFramebuffer;

	INT Width = Min(Viewport->SizeX,SceneWidth), Height = Min(Viewport->SizeY,SceneHeight);
	glReadPixels(0,0,Width,Height,GL_RGBA,GL_UNSIGNED_BYTE,Pixels);
	for( INT i=0; i<Width*Height; i++ )
		Exchange(Pixels[i].R,Pixels[i].B);
	// GL rows run bottom up.
	if( !Flipped )
		for( INT Y=0; Y<Height/2; Y++ )
			for( INT X=0; X<Width; X++ )
				Exchange(Pixels[Y * Width + X],Pixels[(Height - 1 - Y) * Width + X]);

	unguard;
}

FRenderCaps* UOpenGLRenderDevice::GetRenderCaps()
{
	RenderCaps.HardwareTL					= 1;
	RenderCaps.PixelShaderVersion			= 14;
	RenderCaps.MaxSimultaneousTerrainLayers	= 3;
	return &RenderCaps;
}
