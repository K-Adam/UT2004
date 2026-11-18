#include "OpenGLDrv.h"

struct FOpenGLModifierInfo
{
	UBOOL	ModifyTextureTransforms,
			ModifyFramebufferBlending,
			ModifyColor,
			ModifyOpacity;

	FMatrix	Matrix;
	BYTE	TexCoordSource,
			TexCoordCount;
	UBOOL	TexCoordProjected;

	BYTE	FrameBufferBlending;
	UBOOL	ZWrite,
			ZTest,
			AlphaTest,
			TwoSided;
	BYTE	AlphaRef;

	FColor	Color;
	UBOOL	AlphaBlend;

	UMaterial*	Opacity;
	UBOOL		OpacityOverrideTexModifier;

	FOpenGLModifierInfo()
	:	ModifyTextureTransforms(0), ModifyFramebufferBlending(0), ModifyColor(0), ModifyOpacity(0)
	,	Matrix(FMatrix::Identity), TexCoordSource(TCS_Stream0), TexCoordCount(TCN_2DCoords), TexCoordProjected(0)
	,	FrameBufferBlending(FB_Overwrite), ZWrite(1), ZTest(1), AlphaTest(0), TwoSided(0), AlphaRef(0)
	,	Color(0,0,0,0), AlphaBlend(0), Opacity(NULL), OpacityOverrideTexModifier(0)
	{}

	void SetSource(BYTE Source,const FMatrix& InMatrix,BYTE Count=TCN_2DCoords,UBOOL Projected=0)
	{
		ModifyTextureTransforms	= 1;
		TexCoordSource			= Source;
		Matrix					= InMatrix;
		TexCoordCount			= Count;
		TexCoordProjected		= Projected;
	}
};

// Stock materials that fake light shafts with translucent meshes. Nothing else marks them.
static const TCHAR* LightBeams[][2] =
{
	{ TEXT("Litespray_shade"),			TEXT("AWGlobal.Shaders.Litespray_shade") },
	{ TEXT("TowerLightConePanner"),		TEXT("TowerTerrain.General.TowerLightConePanner") },
	{ TEXT("TowerLightConeGradient"),	TEXT("TowerTerrain.General.TowerLightConeGradient") },
	{ TEXT("lightbeamendshader1"),		TEXT("HourAdaraTexor.lightbeamendshader1") },
	{ TEXT("SC_SpotLightGrad_S"),		TEXT("SC_Intro.SpotLights.SC_SpotLightGrad_S") },
	{ TEXT("streaker"),					TEXT("Egypt_tech_Epic.Shaders.streaker") },
	{ TEXT("LightBoxFB"),				TEXT("PC_UrbanTex.pauls_stuff.LightBoxFB") },
	{ TEXT("LightBox"),					TEXT("Bastien.Effects.LightBox") },
};

// Runs for every draw, so the path is only built when the name matches.
static UBOOL IsLightBeam(UMaterial* Material)
{
	for( INT i=0; i<ARRAY_COUNT(LightBeams); i++ )
		if( appStricmp(Material->GetName(),LightBeams[i][0]) == 0 && appStricmp(Material->GetPathName(),LightBeams[i][1]) == 0 )
			return 1;
	return 0;
}

// Uses the material type bits where set, because Cast<> is slow.
template<class T,DWORD Type> static T* MaterialCast(UMaterial* Material)
{
	if( !Material )
		return NULL;
	return (Material->MaterialType & Type) ? (T*)Material : Material->MaterialType == 0 ? Cast<T>(Material) : NULL;
}

// Fire, muzzle flashes, explosions and lava can only be told by what their art is called. A cold word
// in the name rules a material out, which keeps smoke, coronas and energy weapons still.
static const TCHAR* ColdNames[]	= { TEXT("SMOKE"), TEXT("STEAM"), TEXT("COOL"), TEXT("SHOCK"), TEXT("LINK"), TEXT("BIO"), TEXT("PLASMA"), TEXT("FLARE"), TEXT("CORONA") };
static const TCHAR* LavaNames[]	= { TEXT("LAVA"), TEXT("MAGMA"), TEXT("MOLTEN") };
static const TCHAR* HotNames[]	= { TEXT("FLAME"), TEXT("FIRE"), TEXT("BURN"), TEXT("NAPALM"), TEXT("EXPLO"), TEXT("EXP1_"), TEXT("EXP2_"), TEXT("EXP7_"), TEXT("WE1_"), TEXT("BLAST"), TEXT("MUZZLE"), TEXT("FLASH"), TEXT("EXHAUST") };

static UBOOL HasName(const FString& Name,const TCHAR** Names,INT Count)
{
	for( INT i=0; i<Count; i++ )
		if( appStrstr(*Name,Names[i]) )
			return 1;
	return 0;
}

static INT HeatKind(UOpenGLRenderDevice* RenDev,UMaterial* Material)
{
	// Particles and xEmitters pass one wrapper for every texture, so only what it wraps can be remembered.
	for( INT i=0; i<8 && Material; i++ )
	{
		if( UModifier* Modifier = MaterialCast<UModifier,MT_Modifier>(Material) )
			Material = Modifier->Material;
		else if( UParticleMaterial* Particle = MaterialCast<UParticleMaterial,MT_ParticleMaterial>(Material) )
			Material = Particle->BitmapMaterial;
		else
			break;
	}
	if( !Material )
		return HEAT_None;
	if( BYTE* Known = RenDev->HeatKinds.Find(Material->GetFName()) )
		return *Known;
	// Only its own name, a group called Lava also holds the rock around it.
	FString	Name = FString(Material->GetName()).Caps();
	BYTE	Kind = HasName(Name,ColdNames,ARRAY_COUNT(ColdNames)) ? HEAT_None
				: HasName(Name,LavaNames,ARRAY_COUNT(LavaNames)) ? HEAT_Lava
				: HasName(Name,HotNames,ARRAY_COUNT(HotNames)) ? HEAT_Hot : HEAT_None;
	RenDev->HeatKinds.Set(Material->GetFName(),Kind);
	return Kind;
}

// Strips the modifiers off a chain ending in a T and folds them into Info.
template<class T,DWORD Type> static T* CheckMaterial(FLOAT Time,UMaterial* InMaterial,FOpenGLModifierInfo* Info=NULL)
{
	T* Material = MaterialCast<T,Type>(InMaterial);
	if( Material )
		return Material;

	UModifier* Modifier = MaterialCast<UModifier,MT_Modifier>(InMaterial);
	while( Modifier )
	{
		Material = MaterialCast<T,Type>(Modifier->Material);
		Modifier = MaterialCast<UModifier,MT_Modifier>(Modifier->Material);
	}

	if( Material && Info )
	{
		UBOOL NeedSource = 1;
		for( Modifier = MaterialCast<UModifier,MT_Modifier>(InMaterial); Modifier; Modifier = MaterialCast<UModifier,MT_Modifier>(Modifier->Material) )
		{
			if( UTexModifier* TexModifier = MaterialCast<UTexModifier,MT_TexModifier>(Modifier) )
			{
				// GetMatrix comes first, UTexEnvMap sets its source in it.
				if( FMatrix* TexMatrix = TexModifier->GetMatrix(Time) )
				{
					Info->ModifyTextureTransforms = 1;
					Info->Matrix *= *TexMatrix;
				}
				if( NeedSource && TexModifier->TexCoordSource != TCS_NoChange )
				{
					Info->ModifyTextureTransforms	= 1;
					Info->TexCoordSource			= TexModifier->TexCoordSource;
					Info->TexCoordCount				= TexModifier->TexCoordCount;
					Info->TexCoordProjected			= TexModifier->TexCoordProjected;
					NeedSource = 0;
				}
			}
			if( UFinalBlend* FinalBlend = MaterialCast<UFinalBlend,MT_FinalBlend>(Modifier) )
			{
				Info->ModifyFramebufferBlending	= 1;
				Info->FrameBufferBlending		= FinalBlend->FrameBufferBlending;
				Info->ZWrite					= FinalBlend->ZWrite;
				Info->ZTest						= FinalBlend->ZTest;
				Info->AlphaTest					= FinalBlend->AlphaTest;
				Info->TwoSided					= FinalBlend->TwoSided;
				Info->AlphaRef					= FinalBlend->AlphaRef;
			}
			if( UColorModifier* ColorModifier = MaterialCast<UColorModifier,MT_ColorModifier>(Modifier) )
			{
				Info->ModifyColor	= 1;
				Info->AlphaBlend	|= ColorModifier->AlphaBlend;
				Info->TwoSided		|= ColorModifier->RenderTwoSided;
				Info->Color			= ColorModifier->Color;
			}
			if( UOpacityModifier* OpacityModifier = MaterialCast<UOpacityModifier,MT_OpacityModifier>(Modifier) )
			{
				Info->ModifyOpacity					= 1;
				Info->Opacity						= OpacityModifier->Opacity;
				Info->OpacityOverrideTexModifier	= OpacityModifier->bOverrideTexModifier;
			}
		}
	}
	return Material;
}

// Walks a material once per SetMaterial to fill the material state and build the
// program key. The key holds the address of every code fragment emitted, plus the
// few choices that change code outside them, so the GLSL text itself is only
// formatted when a program has to be built.
class FOpenGLCompiler
{
public:
	FOpenGLRenderInterface*				RI;
	FOpenGLRenderInterface::FSavedState*	S;
	FOpenGLMaterialState&					State;
	TArray<BYTE>&						Key;
	FString*							Code;
	FLOAT								Time;
	INT									NumTemps;
	UBOOL								EnvSampled;
	INT									ReflE,
										ReflMask,
										Lit,		// The dynamic and static lighting, once applied.
										Light;		// All lighting multiplied into the colour.
	UBOOL								Beam;
	INT									Heat;
	UBOOL								Cube[OGL_MAX_TEXTURES];
	FString*							ErrorString;
	UMaterial**							ErrorMaterial;

	// Fog colour of the pass. D3D9Drv overrides it so blended passes fade to their neutral colour.
	enum EFog { FOG_Normal, FOG_Black, FOG_Grey, FOG_White };
	INT		FogMode;

	FOpenGLCompiler(FOpenGLRenderInterface* InRI,FOpenGLMaterialState& InState,TArray<BYTE>& InKey,FString* InCode,FString* InErrorString,UMaterial** InErrorMaterial)
	:	RI(InRI), S(InRI->CurrentState), State(InState), Key(InKey), Code(InCode), NumTemps(0)
	,	EnvSampled(0), ReflE(INDEX_NONE), ReflMask(INDEX_NONE), Lit(INDEX_NONE), Light(INDEX_NONE), Beam(0), Heat(HEAT_None)
	,	ErrorString(InErrorString), ErrorMaterial(InErrorMaterial), FogMode(FOG_Normal)
	{
		Time = RI->Viewport->Actor ? RI->Viewport->Actor->Level->TimeSeconds : 0.f;
		appMemzero(Cube,sizeof(Cube));
	}

	void K(INT Value)
	{
		Key.AddItem((BYTE)Value);
	}

	void Emit(const TCHAR* Fmt,...)
	{
		PTRINT Site = (PTRINT)Fmt;
		appMemcpy(&Key(Key.Add(sizeof(Site))),&Site,sizeof(Site));
		if( Code )
		{
			TCHAR Line[1024];
			GET_VARARGS(Line,ARRAY_COUNT(Line),Fmt,Fmt);
			*Code += Line;
		}
	}

	INT Fail(const TCHAR* Message,UMaterial* Material)
	{
		if( ErrorString )
			*ErrorString = Message;
		if( ErrorMaterial )
			*ErrorMaterial = Material;
		return INDEX_NONE;
	}

	INT NewTemp()
	{
		return NumTemps++;
	}

	INT AddMatrix(const FMatrix& Matrix)
	{
		if( State.NumMatrices >= OGL_MAX_MATRICES )
			return Fail(TEXT("Too many texture transforms"),NULL);
		State.Matrices[State.NumMatrices] = Matrix;
		return State.NumMatrices++;
	}

	INT Constant(FPlane Value,UBOOL GlobalColor=0)
	{
		if( State.NumConstants >= OGL_MAX_CONSTANTS )
			return Fail(TEXT("Too many constant colours"),NULL);
		INT Slot = State.NumConstants++;
		State.Constants[Slot] = Value;
		if( GlobalColor )
			State.GlobalColorConstant = Slot;
		INT T = NewTemp();
		Emit(TEXT("\tvec4 r%i = c%i;\n"),T,Slot);
		return T;
	}

	// Texture coordinates as D3D's fixed function texture transforms produce them.
	INT TexCoord(const FOpenGLModifierInfo& Mod)
	{
		BYTE Source = Mod.ModifyTextureTransforms ? Mod.TexCoordSource : TCS_Stream0;
		UBOOL UseMatrix = Mod.ModifyTextureTransforms && (Mod.Matrix != FMatrix::Identity || Mod.TexCoordProjected);
		FMatrix Matrix = Mod.Matrix;
		INT T = NewTemp();
		K(Source);

		if( Source <= TCS_Stream7 )
		{
			// Two coordinates are transformed as (u,v,1,0), so the translation sits in the third row.
			if( UseMatrix && Mod.TexCoordCount == TCN_2DCoords )
				Emit(TEXT("\tvec4 r%i = vec4(vTexCoord%i.xy, 1.0, 0.0);\n"),T,Source);
			else
				Emit(TEXT("\tvec4 r%i = vTexCoord%i;\n"),T,Source);
		}
		else if( Source == TCS_WorldCoords || Source == TCS_ProjectorCoords )
		{
			Emit(TEXT("\tvec4 r%i = vec4(vRestPos, 1.0);\n"),T);
			UseMatrix = Mod.Matrix != FMatrix::Identity || Mod.TexCoordProjected;
		}
		else if( Source == TCS_CameraCoords )
			Emit(TEXT("\tvec4 r%i = vec4(vCameraPos, 1.0);\n"),T);
		else
		{
			EnvSampled = 1;
			if( Source != TCS_WorldEnvMapCoords )
				Emit(TEXT("\tvec4 r%i = vec4(CameraReflection(), 1.0);\n"),T);
			else
			{
				Emit(TEXT("\tvec4 r%i = vec4(ProbeReflection(), 1.0);\n"),T);
				// Same transform as D3D9Drv, which applies the transposed world to texture matrix.
				Matrix = (S->WorldToCamera * Mod.Matrix).Transpose();
				UseMatrix = 1;
			}
		}

		if( UseMatrix )
		{
			INT Slot = AddMatrix(Matrix);
			if( Slot == INDEX_NONE )
				return INDEX_NONE;
			Emit(TEXT("\tr%i = m%i * r%i;\n"),T,Slot,T);
			if( Mod.TexCoordProjected && Mod.TexCoordCount == TCN_3DCoords )
				Emit(TEXT("\tr%i.xy /= r%i.z;\n"),T,T);
			else if( Mod.TexCoordProjected && Mod.TexCoordCount == TCN_4DCoords )
				Emit(TEXT("\tr%i.xyz /= r%i.w;\n"),T,T);
		}
		return T;
	}

	INT SampleTexture(FOpenGLTexture* Texture,BYTE UClamp,BYTE VClamp,const FOpenGLModifierInfo& Mod,UBOOL Detail=0)
	{
		if( State.NumTextures >= OGL_MAX_TEXTURES )
			return Fail(TEXT("Too many textures"),NULL);
		INT Unit = State.NumTextures++;
		State.Textures[Unit] = Texture;
		Cube[Unit] = Texture && Texture->Target == GL_TEXTURE_CUBE_MAP;
		State.SamplerFlags[Unit] = Cube[Unit] ? (GSF_ClampU | GSF_ClampV) : ((UClamp == TC_Clamp ? GSF_ClampU : 0) | (VClamp == TC_Clamp ? GSF_ClampV : 0));
		K(Cube[Unit]);

		INT TC = TexCoord(Mod);
		if( TC == INDEX_NONE )
			return INDEX_NONE;
		INT T = NewTemp();
		// The -0.5 mip bias sharpens like D3D9Drv's default, detail textures stay unbiased.
		if( Cube[Unit] )
			Emit(TEXT("\tvec4 r%i = texture(s%i, r%i.xyz, -0.5);\n"),T,Unit,TC);
		else if( Detail )
			Emit(TEXT("\tvec4 r%i = texture(s%i, r%i.xy);\n"),T,Unit,TC);
		else
			Emit(TEXT("\tvec4 r%i = texture(s%i, r%i.xy, -0.5);\n"),T,Unit,TC);
		return T;
	}

	// Three samples along the world axes, weighted by how squarely the surface faces each.
	INT SampleTriplanar(UTriplanarBitmapMaterial* Bitmap)
	{
		if( State.NumTextures >= OGL_MAX_TEXTURES )
			return Fail(TEXT("Too many textures"),NULL);
		INT Unit = State.NumTextures++;
		State.Textures[Unit] = RI->CacheTexture(Bitmap->GetRenderInterface());
		Cube[Unit] = 0;
		State.SamplerFlags[Unit] = 0;
		K(Cube[Unit]);

		INT Scale = Constant(FPlane(Bitmap->WorldScale,0.f,0.f,0.f));
		INT Mean = Constant(Bitmap->Mean);
		if( Scale == INDEX_NONE || Mean == INDEX_NONE )
			return INDEX_NONE;
		INT P = NewTemp(), W = NewTemp(), T = NewTemp();
		Emit(TEXT("\tvec3 r%i = vRestPos * r%i.x;\n"),P,Scale);
		Emit(TEXT("\tvec3 r%i = abs(normalize(vWorldNormal));\n"),W);
		Emit(TEXT("\tr%i *= r%i; r%i *= r%i; r%i *= r%i;\n"),W,W,W,W,W,W);
		Emit(TEXT("\tr%i /= r%i.x + r%i.y + r%i.z;\n"),W,W,W,W);
		Emit(TEXT("\tvec4 r%i = texture(s%i, r%i.yz, -0.5) * r%i.x;\n"),T,Unit,P,W);
		Emit(TEXT("\tr%i += texture(s%i, r%i.xz, -0.5) * r%i.y;\n"),T,Unit,P,W);
		Emit(TEXT("\tr%i += texture(s%i, r%i.xy, -0.5) * r%i.z;\n"),T,Unit,P,W);
		Emit(TEXT("\tr%i.rgb = r%i.rgb + (r%i.rgb - r%i.rgb) * inversesqrt(dot(r%i, r%i));\n"),T,Mean,T,Mean,W,W);
		return T;
	}

	INT Sample(UBitmapMaterial* Bitmap,const FOpenGLModifierInfo& Mod,UBOOL Detail=0)
	{
		if( UTriplanarBitmapMaterial* Triplanar = Cast<UTriplanarBitmapMaterial>(Bitmap) )
			return SampleTriplanar(Triplanar);
		UBitmapMaterial* Current = Bitmap->Get(RI->Viewport->CurrentTime,RI->Viewport);
		FBaseTexture* Base = Current ? Current->GetRenderInterface() : NULL;
		return SampleTexture(Base ? RI->CacheTexture(Base) : NULL,Bitmap->UClampMode,Bitmap->VClampMode,Mod,Detail);
	}

	INT Combined(UMaterial* InMaterial,FOpenGLModifierInfo Mod)
	{
		UVertexColor*		VertexColor;
		UConstantMaterial*	ConstantMaterial;
		UBitmapMaterial*	Bitmap;
		UCombiner*			Combiner;

		if( !InMaterial )
		{
			INT T = NewTemp();
			Emit(TEXT("\tvec4 r%i = vec4(1.0);\n"),T);
			return T;
		}
		if( (VertexColor = CheckMaterial<UVertexColor,MT_VertexColor>(Time,InMaterial,&Mod)) != NULL )
		{
			INT T = NewTemp();
			Emit(TEXT("\tvec4 r%i = vDiffuse;\n"),T);
			return T;
		}
		if( (ConstantMaterial = CheckMaterial<UConstantMaterial,MT_ConstantMaterial>(Time,InMaterial,&Mod)) != NULL )
			return Constant(ConstantMaterial->GetColor(Time).Plane());
		if( (Bitmap = CheckMaterial<UBitmapMaterial,MT_BitmapMaterial>(Time,InMaterial,&Mod)) != NULL )
			return Sample(Bitmap,Mod);
		if( (Combiner = CheckMaterial<UCombiner,MT_Combiner>(Time,InMaterial,&Mod)) != NULL )
			return CombinerMaterial(Combiner,Mod);
		return Fail(TEXT("Only bitmaps, constants, vertex colours and combiners can be combined"),InMaterial);
	}

	INT CombinerMaterial(UCombiner* Combiner,const FOpenGLModifierInfo& Mod)
	{
		if( !Combiner->Material1 )
			return Fail(TEXT("Combiner must specify Material1"),Combiner);

		INT A = Combined(Combiner->Material1,Mod);
		INT B = Combined(Combiner->Material2,Mod);
		if( A == INDEX_NONE || B == INDEX_NONE )
			return INDEX_NONE;

		// Without a mask D3D9Drv blends by the alpha of the operand it loads first, the non bitmap one.
		UBOOL Swapped = !CheckMaterial<UBitmapMaterial,MT_BitmapMaterial>(Time,Combiner->Material2)
			|| (Combiner->InvertMask && CheckMaterial<UBitmapMaterial,MT_BitmapMaterial>(Time,Combiner->Material1) && Combiner->Mask == Combiner->Material2);

		INT M, MaskSource;
		if( !Combiner->Mask )
			M = (MaskSource = Swapped) ? B : A;
		else if( Combiner->Mask == Combiner->Material1 )
			M = A, MaskSource = 0;
		else if( Combiner->Mask == Combiner->Material2 )
			M = B, MaskSource = 1;
		else if( (M = Combined(Combiner->Mask,Mod)) == INDEX_NONE )
			return INDEX_NONE;
		else
			MaskSource = 2;
		K(MaskSource);

		INT Mask = NewTemp();
		if( Combiner->Mask && Combiner->InvertMask )
			Emit(TEXT("\tfloat r%i = 1.0 - r%i.a;\n"),Mask,M);
		else
			Emit(TEXT("\tfloat r%i = r%i.a;\n"),Mask,M);

		INT T = NewTemp();
		switch( Combiner->CombineOperation )
		{
		case CO_Use_Color_From_Material1:
			Emit(TEXT("\tvec4 r%i = vec4(r%i.rgb, 0.0);\n"),T,A);
			break;
		case CO_Use_Color_From_Material2:
			Emit(TEXT("\tvec4 r%i = vec4(r%i.rgb, 0.0);\n"),T,B);
			break;
		case CO_Multiply:
			if( Combiner->Modulate4X )
				Emit(TEXT("\tvec4 r%i = vec4(clamp(r%i.rgb * r%i.rgb * 4.0, 0.0, 1.0), 0.0);\n"),T,A,B);
			else if( Combiner->Modulate2X )
				Emit(TEXT("\tvec4 r%i = vec4(clamp(r%i.rgb * r%i.rgb * 2.0, 0.0, 1.0), 0.0);\n"),T,A,B);
			else
				Emit(TEXT("\tvec4 r%i = vec4(r%i.rgb * r%i.rgb, 0.0);\n"),T,A,B);
			break;
		case CO_Add:
			Emit(TEXT("\tvec4 r%i = vec4(clamp(r%i.rgb + r%i.rgb, 0.0, 1.0), 0.0);\n"),T,A,B);
			break;
		case CO_Subtract:
			// D3D9Drv computes Material2 minus Material1, and content is tuned to it.
			Emit(TEXT("\tvec4 r%i = vec4(clamp(r%i.rgb - r%i.rgb, 0.0, 1.0), 0.0);\n"),T,B,A);
			break;
		case CO_AlphaBlend_With_Mask:
			Emit(TEXT("\tvec4 r%i = vec4(mix(r%i.rgb, r%i.rgb, r%i), 0.0);\n"),T,A,B,Mask);
			break;
		case CO_Add_With_Mask_Modulation:
			if( Combiner->Mask )
				Emit(TEXT("\tvec4 r%i = vec4(clamp(r%i.rgb + r%i.rgb * r%i, 0.0, 1.0), 0.0);\n"),T,B,A,Mask);
			else
				Emit(TEXT("\tvec4 r%i = vec4(clamp(r%i.rgb + r%i.rgb * r%i.a, 0.0, 1.0), 0.0);\n"),T,B,A,B);
			break;
		case CO_Use_Color_From_Mask:
		default:
			Emit(TEXT("\tvec4 r%i = vec4(r%i.rgb, 0.0);\n"),T,M);
			break;
		}

		switch( Combiner->AlphaOperation )
		{
		case AO_Multiply:
			Emit(TEXT("\tr%i.a = r%i.a * r%i.a;\n"),T,A,B);
			break;
		case AO_Add:
			Emit(TEXT("\tr%i.a = clamp(r%i.a + r%i.a, 0.0, 1.0);\n"),T,A,B);
			break;
		case AO_Use_Alpha_From_Material1:
			Emit(TEXT("\tr%i.a = r%i.a;\n"),T,A);
			break;
		case AO_Use_Alpha_From_Material2:
			Emit(TEXT("\tr%i.a = r%i.a;\n"),T,B);
			break;
		case AO_Use_Mask:
		default:
			Emit(TEXT("\tr%i.a = r%i;\n"),T,Mask);
			break;
		}
		return T;
	}

	// Static and dynamic lighting, Options.z doubles it like D3D9Drv's MODULATE2X stages.
	INT ApplyLighting(INT C,UBOOL Diffuse,UBOOL Lightmap)
	{
		if( Diffuse )
		{
			Lit = NewTemp();
			Emit(TEXT("\tvec4 r%i = Lighting();\n"),Lit);
			Light = NewTemp();
			Emit(TEXT("\tvec3 r%i = r%i.rgb * (1.0 + Options.z);\n"),Light,Lit);
			Emit(TEXT("\tr%i = vec4(clamp(r%i.rgb * r%i, 0.0, 1.0), r%i.a * r%i.a);\n"),C,C,Light,C,Lit);
		}
		if( Lightmap )
		{
			FOpenGLModifierInfo Mod;
			Mod.SetSource(TCS_Stream1,FMatrix::Identity);
			INT LM = SampleTexture(S->Lightmap,TC_Clamp,TC_Clamp,Mod);
			if( LM == INDEX_NONE )
				return INDEX_NONE;
			Emit(TEXT("\tr%i.rgb = clamp(r%i.rgb * r%i.rgb * (1.0 + Options.z), 0.0, 1.0);\n"),C,C,LM);
			if( Light == INDEX_NONE )
			{
				Light = NewTemp();
				Emit(TEXT("\tvec3 r%i = vec3(1.0);\n"),Light);
			}
			Emit(TEXT("\tr%i *= r%i.rgb * (1.0 + Options.z);\n"),Light,LM);
		}
		return C;
	}

	INT ApplyDetail(INT C,UMaterial* DetailMaterial,FLOAT Scale,FOpenGLModifierInfo Mod)
	{
		if( !DetailMaterial )
			return C;
		Mod.ModifyTextureTransforms = 1;
		Mod.Matrix *= FMatrix(FPlane(Scale,0,0,0),FPlane(0,Scale,0,0),FPlane(0,0,1,0),FPlane(0,0,0,1));
		UBitmapMaterial* Bitmap = CheckMaterial<UBitmapMaterial,MT_BitmapMaterial>(Time,DetailMaterial,&Mod);
		if( !Bitmap )
			return C;
		INT D = Sample(Bitmap,Mod,1);
		if( D == INDEX_NONE )
			return INDEX_NONE;
		Emit(TEXT("\tr%i.rgb = clamp(r%i.rgb * r%i.rgb * 2.0, 0.0, 1.0);\n"),C,C,D);
		return C;
	}

	void SetBlend(GLenum Src,GLenum Dst,INT Fog)
	{
		State.SrcBlend	= Src;
		State.DstBlend	= Dst;
		FogMode			= Fog;
	}

	void ApplyFinalBlend(const FOpenGLModifierInfo& Mod)
	{
		switch( Mod.FrameBufferBlending )
		{
		case FB_Overwrite:								SetBlend(GL_ONE,GL_ZERO,FOG_Normal); break;
		case FB_AlphaBlend:								SetBlend(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,FOG_Normal); break;
		case FB_AlphaModulate_MightNotFogCorrectly:		SetBlend(GL_ONE,GL_ONE_MINUS_SRC_ALPHA,FOG_Black); break;
		case FB_Modulate:								SetBlend(GL_DST_COLOR,GL_SRC_COLOR,FOG_Grey); break;
		case FB_Translucent:							SetBlend(GL_ONE,GL_ONE_MINUS_SRC_COLOR,FOG_Black); break;
		case FB_Darken:									SetBlend(GL_ZERO,GL_ONE_MINUS_SRC_COLOR,FOG_Black); break;
		case FB_Brighten:								SetBlend(GL_SRC_ALPHA,GL_ONE,FOG_Black); break;
		case FB_Invisible:								SetBlend(GL_ZERO,GL_ONE,FOG_Black); break;
		}
		State.ZWrite	= Mod.ZWrite;
		State.ZTest		= Mod.ZTest;
		State.TwoSided	= Mod.TwoSided;
		State.AlphaTest	= Mod.AlphaTest;
		State.AlphaRef	= Mod.AlphaRef / 255.f;
	}

	// The ColorModifier tint. D3D9Drv doubles it under 2x lighting and content is tuned to that.
	INT ApplyColorModifier(INT C,const FOpenGLModifierInfo& Mod)
	{
		if( !Mod.ModifyColor )
			return C;
		if( Mod.AlphaBlend && State.SrcBlend == GL_ONE && State.DstBlend == GL_ZERO )
			SetBlend(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,FogMode);
		State.TwoSided |= Mod.TwoSided;
		INT Tint = Constant(Mod.Color.Plane(),1);
		if( Tint == INDEX_NONE )
			return INDEX_NONE;
		Emit(TEXT("\tr%i = vec4(clamp(r%i.rgb * r%i.rgb * (1.0 + Options.z), 0.0, 1.0), r%i.a * r%i.a);\n"),C,C,Tint,C,Tint);
		if( ReflE != INDEX_NONE )
			Emit(TEXT("\tr%i *= r%i.rgb * (1.0 + Options.z);\n"),ReflE,Tint);
		return C;
	}

	const TCHAR* FogTarget()
	{
		static const TCHAR* Targets[] = { TEXT("FogColor.rgb"), TEXT("vec3(0.0)"), TEXT("vec3(0.498)"), TEXT("vec3(1.0)") };
		return Targets[FogMode];
	}

	void Output(INT C)
	{
		K(Beam);
		if( Beam )
		{
			if( State.SrcBlend == GL_SRC_ALPHA )
				Emit(TEXT("\tr%i.rgb *= r%i.a;\n"),C,C);
			// Fading toward the silhouette makes the flat cone read as a volume, fading near the camera
			// keeps it from popping when driving through it, and far away the boost would glare.
			Emit(TEXT("\tr%i.rgb *= 1.2 * pow(abs(dot(normalize(mat3(WorldToCamera) * vWorldNormal), normalize(vCameraPos))), 1.5) * smoothstep(32.0, 160.0, length(vCameraPos)) * mix(1.0, 0.5, smoothstep(1500.0, 5000.0, length(vCameraPos)));\n"),C);
			SetBlend(GL_ONE,GL_ONE,FOG_Black);
			State.ZWrite	= 0;
			State.TwoSided	= 1;
			State.AlphaTest	= 0;
			State.Beam		= 1;
		}
		K(FogMode);
		K(State.AlphaTest);
		if( State.AlphaTest )
		{
			// Multisampling smooths triangle edges, not the outline a plant's texture cuts. Its alpha becomes how
			// much of the pixel the outline covers. A tree's leaves that only drop their clear texels blend
			// softly from zero and have no outline to sharpen.
			if( State.AlphaRef > 0.f && S->Instanced && State.SrcBlend == GL_SRC_ALPHA && State.DstBlend == GL_ONE_MINUS_SRC_ALPHA )
			{
				K(appRound(State.AlphaRef * 255.f));
				Emit(TEXT("\tr%i.a = clamp((r%i.a - %f) / max(fwidth(r%i.a), 0.0001) + 0.5, 0.0, 1.0);\n"),C,C,State.AlphaRef,C);
				State.AlphaRef = 0.f;
			}
			Emit(TEXT("\tif (r%i.a <= AlphaRef) discard;\n"),C);
		}
		Emit(TEXT("\tFragColor = vec4(ApplyFog(r%i.rgb, %s), r%i.a);\n"),C,FogTarget(),C);
		// Blended draws leave the reflection of the surface below. Modulating and darkening scale it like
		// the colour, other blends keep it whole, as attachments share the blend and the alpha cannot
		// both weight and hold the reflectivity.
		UBOOL	Opaque = State.SrcBlend == GL_ONE && State.DstBlend == GL_ZERO && State.ZWrite,
				Scales = State.DstBlend == GL_SRC_COLOR || (State.SrcBlend == GL_ZERO && State.DstBlend == GL_ONE_MINUS_SRC_COLOR);
		if( Opaque && ReflE != INDEX_NONE )
			Emit(TEXT("\tReflection = vec4(ApplyFog(r%i, vec3(0.0)), ApplyFog(vec3(r%i), vec3(0.0)).x);\n"),ReflE,ReflMask);
		else if( Scales )
			Emit(TEXT("\tReflection = vec4(FragColor.rgb, dot(FragColor.rgb, vec3(0.299, 0.587, 0.114)));\n"));
		else
			Emit(TEXT("\tReflection = vec4(0.0);\n"));
		// Dividing out the lighting in float keeps the albedo exact in the dark, where the bounce matters
		// most, and keeps detail textures and tints. Lit layers blended over each other blend it alike.
		UBOOL Layered = (State.SrcBlend == GL_SRC_ALPHA && State.DstBlend == GL_ONE_MINUS_SRC_ALPHA) || (State.SrcBlend == GL_ONE && State.DstBlend == GL_ONE);
		K(Opaque | Scales << 1 | Layered << 2);
		if( Light != INDEX_NONE && (Opaque || Layered) )
			Emit(TEXT("\tAlbedo = vec4(ApplyFog(min(r%i.rgb / max(r%i, vec3(0.004)), 1.0), vec3(0.0)), r%i.a);\n"),C,Light,C);
		else if( Scales )
			Emit(TEXT("\tAlbedo = FragColor;\n"));
		else
			Emit(TEXT("\tAlbedo = vec4(0.0);\n"));
		// The heat haze reads how hot the world looks from its alpha, which is how much it glows. Hot
		// names on solid surfaces are mostly scenery, only lava glows without blending, by how red it
		// is, and half so a whole floor of it stays calm. Its translucent layers are crust and flow
		// drawn over rock as much as over lava, their coverage says nothing about heat. Where the alpha
		// weights the blend of the other hot draws it stays, it is the coverage already.
		UBOOL	Blended = !(State.SrcBlend == GL_ONE && State.DstBlend == GL_ZERO);
		INT		Hot = Beam || Scales ? HEAT_None : Heat == HEAT_Hot ? (Blended || State.AlphaTest ? HEAT_Hot : HEAT_None)
					: Heat == HEAT_Lava && !Blended ? HEAT_Lava : HEAT_None;
		K(Hot);
		State.Heat = Hot != HEAT_None;
		if( Hot && State.SrcBlend != GL_SRC_ALPHA && State.DstBlend != GL_ONE_MINUS_SRC_ALPHA )
			Emit(Hot == HEAT_Lava ? TEXT("\tFragColor.a = mix(FragColor.a, 0.5 * smoothstep(0.4, 0.8, FragColor.r - FragColor.b), FogUp.w);\n") : TEXT("\tFragColor.a = mix(FragColor.a, max(FragColor.r, max(FragColor.g, FragColor.b)), FogUp.w);\n"));
		// Whatever solid covers a hot surface hides its heat too.
		else if( !Hot && !Blended )
			Emit(TEXT("\tFragColor.a *= 1.0 - FogUp.w;\n"));
	}

	UBOOL Shader(UShader* Shader,FOpenGLModifierInfo Mod)
	{
		UBOOL	Unlit		= Shader->SelfIllumination && !Shader->SelfIlluminationMask;
		UMaterial*	Diffuse	= Unlit ? Shader->SelfIllumination : Shader->Diffuse;
		UMaterial*	Opacity	= Mod.ModifyOpacity ? Mod.Opacity : Shader->Opacity;
		UBOOL	OpacityUsesBase	= !(Mod.ModifyOpacity && Mod.OpacityOverrideTexModifier);
		UBOOL	SelfIllumination = !Unlit && Shader->SelfIllumination;
		K(Unlit);
		K(Opacity != NULL);
		K(SelfIllumination);

		// Blending first, the opacity composition depends on it.
		if( Mod.ModifyFramebufferBlending )
			ApplyFinalBlend(Mod);
		else
		{
			switch( Shader->OutputBlending )
			{
			case OB_Normal:
				if( Opacity )
				{
					SetBlend(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,FOG_Normal);
					State.AlphaTest	= 1;
					State.ZWrite	= 0;
				}
				break;
			case OB_Masked:
				State.AlphaTest	= 1;
				State.AlphaRef	= 127 / 255.f;
				break;
			case OB_Modulate:		SetBlend(GL_DST_COLOR,GL_SRC_COLOR,FOG_Grey); State.ZWrite = 0; break;
			case OB_Translucent:	SetBlend(GL_ONE,GL_ONE,FOG_Black); State.ZWrite = 0; break;
			case OB_Brighten:		SetBlend(GL_ONE,GL_ONE_MINUS_SRC_COLOR,FOG_Black); State.ZWrite = 0; break;
			case OB_Darken:			SetBlend(GL_ZERO,GL_ONE_MINUS_SRC_COLOR,FOG_Black); State.ZWrite = 0; break;
			case OB_Invisible:		SetBlend(GL_ZERO,GL_ONE,FOG_Normal); State.ZWrite = 0; break;
			}
			State.TwoSided = Shader->TwoSided;
		}

		INT C = Combined(Diffuse,Mod);
		if( C == INDEX_NONE )
			return 0;
		if( !Unlit && ApplyLighting(C,S->UseStaticLighting || S->UseDynamicLighting,S->Lightmap != NULL) == INDEX_NONE )
			return 0;

		INT SI = INDEX_NONE, SIMask = INDEX_NONE;
		if( SelfIllumination )
		{
			if( (SI = Combined(Shader->SelfIllumination,Mod)) == INDEX_NONE )
				return 0;
			SIMask = Shader->SelfIlluminationMask == Shader->SelfIllumination ? SI : Combined(Shader->SelfIlluminationMask,Mod);
			if( SIMask == INDEX_NONE )
				return 0;
		}

		INT		Spec = INDEX_NONE, SpecMask = INDEX_NONE;
		UBOOL	EnvSpecular = 0;
		if( Shader->Specular )
		{
			EnvSampled = 0;
			if( (Spec = Combined(Shader->Specular,Mod)) == INDEX_NONE )
				return 0;
			EnvSpecular = EnvSampled && !Shader->ModulateSpecular2X;
			if( !Shader->SpecularityMask )
				Emit(TEXT("\tfloat r%i = 1.0;\n"),SpecMask = NewTemp());
			else if( Shader->SpecularityMask == Shader->Diffuse )
				Emit(TEXT("\tfloat r%i = r%i.a;\n"),SpecMask = NewTemp(),C);
			else
			{
				INT M = Combined(Shader->SpecularityMask,Mod);
				if( M == INDEX_NONE )
					return 0;
				Emit(TEXT("\tfloat r%i = r%i.a;\n"),SpecMask = NewTemp(),M);
			}
			// Unlike D3D9Drv, highlights and reflections fade where the mesh is in shade.
			if( Lit != INDEX_NONE )
				Emit(TEXT("\tr%i *= smoothstep(0.15, 0.6, dot(r%i.rgb, vec3(0.299, 0.587, 0.114)) * (1.0 + Options.z));\n"),SpecMask,Lit);
		}

		if( !Opacity )
		{
			if( SelfIllumination )
				Emit(TEXT("\tr%i.rgb = mix(r%i.rgb, r%i.rgb, r%i.a);\n"),C,C,SI,SIMask);
			UMaterial*	DetailMaterial	= Shader->Detail;
			FLOAT		DetailScale		= Shader->DetailScale;
			if( !DetailMaterial )
			{
				UTexture* DiffuseTexture = CheckMaterial<UTexture,MT_Texture>(Time,Diffuse);
				DetailMaterial	= DiffuseTexture ? DiffuseTexture->Detail : NULL;
				DetailScale		= DiffuseTexture ? DiffuseTexture->DetailScale : 8.f;
			}
			if( ApplyDetail(C,DetailMaterial,DetailScale,Mod) == INDEX_NONE )
				return 0;
			if( Spec != INDEX_NONE )
			{
				if( Shader->ModulateSpecular2X )
					Emit(TEXT("\tr%i.rgb = clamp(r%i.rgb * r%i.rgb * 2.0, 0.0, 1.0);\n"),C,C,Spec);
				else
				{
					// What the clamp let through is the env map's real share of the colour.
					if( EnvSpecular )
					{
						ReflE = NewTemp();
						ReflMask = SpecMask;
						Emit(TEXT("\tvec3 r%i = min(r%i.rgb, 1.0);\n"),ReflE,C);
					}
					Emit(TEXT("\tr%i.rgb = clamp(r%i.rgb + r%i.rgb * r%i, 0.0, 1.0);\n"),C,C,Spec,SpecMask);
					if( EnvSpecular )
						Emit(TEXT("\tr%i = r%i.rgb - r%i;\n"),ReflE,C,ReflE);
				}
			}
			if( ApplyColorModifier(C,Mod) == INDEX_NONE )
				return 0;
			Output(C);
			return 1;
		}

		if( Opacity != Diffuse )
		{
			INT O = Combined(Opacity,OpacityUsesBase ? Mod : FOpenGLModifierInfo());
			if( O == INDEX_NONE )
				return 0;
			Emit(TEXT("\tr%i.a = r%i.a;\n"),C,O);
		}
		if( ApplyColorModifier(C,Mod) == INDEX_NONE )
			return 0;

		UBOOL Premultiply = !Beam && (SelfIllumination || Spec != INDEX_NONE) && State.SrcBlend == GL_SRC_ALPHA && State.DstBlend == GL_ONE_MINUS_SRC_ALPHA;
		K(Premultiply);
		if( !Premultiply )
		{
			if( SelfIllumination )
				Emit(TEXT("\tr%i.rgb = mix(r%i.rgb, r%i.rgb, r%i.a * r%i.a);\n"),C,C,SI,SIMask,C);
			if( Spec != INDEX_NONE )
			{
				if( Shader->ModulateSpecular2X )
					Emit(TEXT("\tr%i.rgb = clamp(r%i.rgb * r%i.rgb * 2.0, 0.0, 1.0);\n"),C,C,Spec);
				else
					Emit(TEXT("\tr%i.rgb = clamp(r%i.rgb + r%i.rgb * r%i, 0.0, 1.0);\n"),C,C,Spec,SpecMask);
			}
			Output(C);
			return 1;
		}

		// D3D9Drv draws self illumination and specular as extra blended passes over
		// the alpha blended base. With premultiplied alpha one pass gives the same result.
		SetBlend(GL_ONE,GL_ONE_MINUS_SRC_ALPHA,FogMode);
		K(State.AlphaTest);
		if( State.AlphaTest )
			Emit(TEXT("\tif (r%i.a <= AlphaRef) discard;\n"),C);
		INT Glow = NewTemp();
		if( SelfIllumination )
			Emit(TEXT("\tfloat r%i = r%i.a * r%i.a;\n"),Glow,SIMask,C);
		else
			Emit(TEXT("\tfloat r%i = 0.0;\n"),Glow);
		Emit(TEXT("\tvec3 r%i = ApplyFog(r%i.rgb, FogColor.rgb) * r%i.a * (1.0 - r%i);\n"),NewTemp(),C,C,Glow);
		INT Base = NumTemps - 1;
		if( SelfIllumination )
			Emit(TEXT("\tr%i += ApplyFog(r%i.rgb, FogColor.rgb) * r%i;\n"),Base,SI,Glow);
		if( Spec != INDEX_NONE )
			Emit(TEXT("\tr%i += ApplyFog(r%i.rgb * r%i, vec3(0.0));\n"),Base,Spec,SpecMask);
		Emit(TEXT("\tFragColor = vec4(r%i, r%i.a + r%i - r%i.a * r%i);\n"),Base,C,Glow,C,Glow);
		// Puddle decals mask colour and reflectivity with one texture, so the opacity is the reflectivity.
		// The fog hides it like the rest of the surface.
		State.Reflectivity = EnvSpecular;
		if( EnvSpecular )
			Emit(TEXT("\tReflection = vec4(ApplyFog(r%i.rgb * r%i, vec3(0.0)), FragColor.a * ApplyFog(vec3(1.0), vec3(0.0)).x);\n"),Spec,SpecMask);
		else
			Emit(TEXT("\tReflection = vec4(0.0);\n"));
		Emit(TEXT("\tAlbedo = vec4(0.0);\n"));
		return 1;
	}

	UBOOL Simple(UMaterial* Material,FOpenGLModifierInfo Mod)
	{
		UMaterial*	DetailMaterial = NULL;
		FLOAT		DetailScale = 8.f;
		UTexture*	Texture = Cast<UTexture>(Material);
		if( Texture )
		{
			if( !Mod.ModifyFramebufferBlending && Texture->bMasked )
			{
				Mod.ModifyFramebufferBlending	= 1;
				Mod.FrameBufferBlending			= (Mod.ModifyColor && Mod.AlphaBlend) ? FB_AlphaBlend : FB_Overwrite;
				Mod.AlphaTest					= 1;
				Mod.AlphaRef					= 127;
			}
			else if( !Mod.ModifyFramebufferBlending && Texture->bAlphaTexture )
			{
				Mod.ModifyFramebufferBlending	= 1;
				Mod.FrameBufferBlending			= FB_AlphaBlend;
				Mod.AlphaTest					= 1;
				Mod.AlphaRef					= 0;
			}
			if( !Mod.ModifyColor )
				Mod.TwoSided = Texture->bTwoSided;
			DetailMaterial	= Texture->Detail;
			DetailScale		= Texture->DetailScale;
		}

		INT C = Combined(Material,Mod);
		if( C == INDEX_NONE || ApplyLighting(C,S->UseStaticLighting || S->UseDynamicLighting,S->Lightmap != NULL) == INDEX_NONE )
			return 0;
		if( Mod.ModifyFramebufferBlending )
			ApplyFinalBlend(Mod);
		else
			State.TwoSided = Mod.TwoSided;
		if( ApplyColorModifier(C,Mod) == INDEX_NONE || ApplyDetail(C,DetailMaterial,DetailScale,Mod) == INDEX_NONE )
			return 0;
		Output(C);
		return 1;
	}

	UBOOL Particle(UParticleMaterial* Particle)
	{
		switch( Particle->ParticleBlending )
		{
		case PTDS_Regular:								SetBlend(GL_ONE,GL_ZERO,FOG_Normal); break;
		case PTDS_AlphaBlend:							SetBlend(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,FOG_Normal); break;
		case PTDS_AlphaModulate_MightNotFogCorrectly:	SetBlend(GL_ONE,GL_ONE_MINUS_SRC_ALPHA,FOG_Black); break;
		case PTDS_Modulated:							SetBlend(GL_DST_COLOR,GL_SRC_COLOR,FOG_Grey); break;
		case PTDS_Translucent:							SetBlend(GL_ONE,GL_ONE,FOG_Black); break;
		case PTDS_Darken:								SetBlend(GL_ZERO,GL_ONE_MINUS_SRC_COLOR,FOG_Black); break;
		case PTDS_Brighten:								SetBlend(GL_ONE,GL_ONE_MINUS_SRC_COLOR,FOG_Black); break;
		}
		State.ZWrite	= Particle->ZWrite;
		State.ZTest		= Particle->ZTest;
		State.TwoSided	= Particle->RenderTwoSided;
		State.AlphaTest	= Particle->AlphaTest;
		State.AlphaRef	= Particle->AlphaRef / 255.f;

		UBitmapMaterial* Bitmap = Particle->BitmapMaterial;
		INT C;
		if( !Bitmap )
			Emit(TEXT("\tvec4 r%i = vec4(1.0);\n"),C = NewTemp());
		else if( Particle->BlendBetweenSubdivisions )
		{
			FOpenGLModifierInfo Second;
			Second.SetSource(TCS_Stream1,FMatrix::Identity);
			INT T0 = Sample(Bitmap,FOpenGLModifierInfo());
			INT T1 = Sample(Bitmap,Second);
			if( T0 == INDEX_NONE || T1 == INDEX_NONE )
				return 0;
			Emit(TEXT("\tvec4 r%i = mix(r%i, r%i, vDiffuse.a);\n"),C = NewTemp(),T0,T1);
			Emit(TEXT("\tr%i.rgb *= vDiffuse.rgb;\n"),C);
		}
		else
		{
			INT T = Sample(Bitmap,FOpenGLModifierInfo());
			if( T == INDEX_NONE )
				return 0;
			// SetGlobalColor supplies the tint of mesh particles.
			INT Tint;
			if( Particle->UseTFactor )
				Tint = Constant(FPlane(0,0,0,0),1);
			else
				Emit(TEXT("\tvec4 r%i = vDiffuse;\n"),Tint = NewTemp());
			if( Tint == INDEX_NONE )
				return 0;
			if( Particle->ParticleBlending == PTDS_Modulated )
				Emit(TEXT("\tvec4 r%i = mix(r%i, r%i, vDiffuse.a);\n"),C = NewTemp(),Tint,T);
			else
				Emit(TEXT("\tvec4 r%i = r%i * r%i;\n"),C = NewTemp(),T,Tint);
		}

		INT NumProjectors = Particle->AcceptsProjectors ? Clamp(Particle->NumProjectors,0,8) : 0;
		for( INT i=0; i<NumProjectors; i++ )
		{
			FParticleProjectorInfo& Projector = Particle->Projectors[i];
			if( !Projector.BitmapMaterial )
				continue;
			FOpenGLModifierInfo Mod;
			Mod.SetSource(TCS_WorldCoords,Projector.Matrix,TCN_3DCoords,Projector.Projected);
			INT P = Sample(Projector.BitmapMaterial,Mod);
			if( P == INDEX_NONE )
				return 0;
			if( Projector.BlendMode == PB_AlphaBlend )
				Emit(TEXT("\tr%i.rgb = mix(r%i.rgb, r%i.rgb, r%i.a);\n"),C,C,P,P);
			else if( Projector.BlendMode == PB_Add )
				Emit(TEXT("\tr%i.rgb = clamp(r%i.rgb * r%i.rgb * 2.0, 0.0, 1.0);\n"),C,C,P);
			else
				Emit(TEXT("\tr%i.rgb *= r%i.rgb;\n"),C,P);
		}
		Output(C);
		return 1;
	}

	UBOOL Terrain(UTerrainMaterial* Terrain)
	{
		UBOOL Lit = S->UseStaticLighting || S->UseDynamicLighting;
		K(Terrain->RenderMethod);
		K(Lit);

		if( Terrain->RenderMethod == RM_AlphaMap )
		{
			FTerrainMaterialLayer& Layer = Terrain->Layers(0);
			FOpenGLModifierInfo Mod;
			Mod.SetSource(TCS_WorldCoords,Layer.TextureMatrix);

			UBitmapMaterial* Bitmap = CheckMaterial<UBitmapMaterial,MT_BitmapMaterial>(Time,Layer.Texture,&Mod);
			if( !Bitmap )
			{
				// A shader layer, faded in by the alpha map on all but the first pass.
				UShader* LayerShader = CheckMaterial<UShader,MT_Shader>(Time,Layer.Texture,&Mod);
				Mod.SetSource(TCS_WorldCoords,Mod.Matrix);
				if( !LayerShader )
				{
					Fail(TEXT("Terrain layers must be Shaders or BitmapMaterials"),Layer.Texture);
					return 0;
				}
				if( !Terrain->FirstPass )
				{
					Mod.ModifyOpacity				= 1;
					Mod.Opacity						= Layer.AlphaWeight;
					Mod.OpacityOverrideTexModifier	= 1;
				}
				return Shader(LayerShader,Mod);
			}

			if( !Terrain->FirstPass )
			{
				SetBlend(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,FOG_Normal);
				State.AlphaTest = 1;
			}
			INT C = Sample(Bitmap,Mod);
			INT W = Layer.AlphaWeight ? Sample(Layer.AlphaWeight,FOpenGLModifierInfo()) : Constant(FPlane(1,1,1,1));
			if( C == INDEX_NONE || W == INDEX_NONE )
				return 0;
			if( Lit )
			{
				Light = NewTemp();
				Emit(TEXT("\tvec3 r%i = Lighting().rgb * 2.0;\n"),Light);
				Emit(TEXT("\tr%i.rgb = clamp(r%i.rgb * r%i, 0.0, 1.0);\n"),C,C,Light);
			}
			K(Bitmap->IsTransparent());
			if( Bitmap->IsTransparent() )
				Emit(TEXT("\tr%i.a *= r%i.a;\n"),C,W);
			else
				Emit(TEXT("\tr%i.a = r%i.a;\n"),C,W);
			Output(C);
			return 1;
		}

		if( !Terrain->FirstPass )
		{
			SetBlend(GL_ONE,GL_ONE,FOG_Black);
			State.ZWrite = 0;
		}
		INT C = NewTemp();
		Emit(TEXT("\tvec4 r%i = vec4(0.0, 0.0, 0.0, 1.0);\n"),C);
		INT L = Light = NewTemp();
		Emit(TEXT("\tvec3 r%i = Lighting().rgb * 2.0;\n"),L);

		INT Combined = INDEX_NONE;
		if( Terrain->RenderMethod == RM_CombinedWeightMap && (Combined = Sample(Terrain->Layers(0).AlphaWeight,FOpenGLModifierInfo())) == INDEX_NONE )
			return 0;
		K(Terrain->Layers.Num());
		for( INT i=0; i<Terrain->Layers.Num() && i<4; i++ )
		{
			FTerrainMaterialLayer& Layer = Terrain->Layers(i);
			FOpenGLModifierInfo Mod;
			Mod.SetSource(TCS_WorldCoords,Layer.TextureMatrix);
			UBitmapMaterial* Bitmap = CheckMaterial<UBitmapMaterial,MT_BitmapMaterial>(Time,Layer.Texture,&Mod);
			if( !Bitmap )
			{
				Fail(TEXT("Weight map terrain layers must be BitmapMaterials"),Layer.Texture);
				return 0;
			}
			INT T = Sample(Bitmap,Mod);
			if( T == INDEX_NONE )
				return 0;
			if( Combined != INDEX_NONE )
			{
				// The combined weight map holds the layer weights in its R, G, B and A channels.
				static const TCHAR* Channels[] = { TEXT("r"), TEXT("g"), TEXT("b"), TEXT("a") };
				Emit(TEXT("\tr%i.rgb += r%i.rgb * r%i.%s;\n"),C,T,Combined,Channels[i]);
			}
			else
			{
				INT W = Sample(Layer.AlphaWeight,FOpenGLModifierInfo());
				if( W == INDEX_NONE )
					return 0;
				Emit(TEXT("\tr%i.rgb += clamp(r%i.rgb * r%i, 0.0, 1.0) * r%i.rgb;\n"),C,T,L,W);
			}
		}
		if( Combined != INDEX_NONE )
			Emit(TEXT("\tr%i.rgb = clamp(r%i.rgb * r%i, 0.0, 1.0);\n"),C,C,L);
		else
			Emit(TEXT("\tr%i.rgb = clamp(r%i.rgb, 0.0, 1.0);\n"),C,C);
		Output(C);
		return 1;
	}

	// How projectors fade with a factor, which depends on the framebuffer blend.
	void Attenuate(INT C,BYTE Blending,INT Factor,const TCHAR* Channel)
	{
		if( Blending == PB_Add )
			Emit(TEXT("\tr%i.rgb *= r%i.%s;\n"),C,Factor,Channel);
		else if( Blending == PB_AlphaBlend )
			Emit(TEXT("\tr%i.a *= r%i.%s;\n"),C,Factor,Channel);
		else
			Emit(TEXT("\tr%i.rgb = mix(vec3(0.498), r%i.rgb, r%i.%s);\n"),C,C,Factor,Channel);
	}

	UBOOL Projector(UProjectorMaterial* Projector,const FOpenGLModifierInfo& InMod)
	{
		BYTE		Blending	= Projector->FrameBufferBlending;
		UShader*	BaseShader	= Cast<UShader>(Projector->BaseMaterial);
		UTexture*	BaseTexture	= Cast<UTexture>(Projector->BaseMaterial);
		UMaterial*	BaseDiffuse	= Projector->BaseMaterial;
		UMaterial*	BaseOpacity	= NULL;
		if( BaseTexture && (BaseTexture->bMasked || BaseTexture->bAlphaTexture) )
			BaseOpacity = BaseTexture;
		else if( BaseShader )
		{
			BaseDiffuse = BaseShader->Diffuse;
			BaseOpacity = BaseShader->Opacity;
		}
		UBOOL UseBase = Projector->BaseMaterialBlending != PB_None && BaseDiffuse;
		K(Blending);
		K(Projector->bStaticProjector);

		// Static projectors carry projected coordinates in stream 0 and the base UVs in stream 1.
		FOpenGLModifierInfo ProjectedMod, BaseMod = InMod;
		UBitmapMaterial* ProjectedBitmap = CheckMaterial<UBitmapMaterial,MT_BitmapMaterial>(Time,Projector->Projected,&ProjectedMod);
		if( Projector->bStaticProjector )
		{
			ProjectedMod.SetSource(TCS_Stream0,ProjectedMod.Matrix,TCN_3DCoords,1);
			BaseMod.ModifyTextureTransforms = 1;
			BaseMod.TexCoordSource = TCS_Stream1;
		}
		else
			ProjectedMod.SetSource(TCS_WorldCoords,Projector->Matrix * ProjectedMod.Matrix,TCN_3DCoords,Projector->bProjected);

		INT C = ProjectedBitmap ? Sample(ProjectedBitmap,ProjectedMod) : Combined(Projector->Projected,ProjectedMod);
		if( C == INDEX_NONE )
			return 0;

		K(UseBase);
		if( UseBase )
		{
			INT B = Combined(BaseDiffuse,BaseMod);
			if( B == INDEX_NONE )
				return 0;
			if( Projector->BaseMaterialBlending == PB_AlphaBlend )
				Emit(TEXT("\tr%i.rgb = mix(r%i.rgb, r%i.rgb, r%i.a);\n"),C,B,C,C);
			else
				Emit(TEXT("\tr%i.rgb = clamp(r%i.rgb * r%i.rgb * 2.0, 0.0, 1.0);\n"),C,C,B);
		}

		INT Vertex = NewTemp();
		Emit(TEXT("\tvec4 r%i = vDiffuse;\n"),Vertex);

		if( Projector->bStaticProjector )
		{
			UBOOL InStage = !UseBase && ProjectedBitmap;
			K(InStage);
			K(Projector->bGradient || !Projector->bProjectOnBackfaces);
			if( InStage )
				Attenuate(C,Blending,Vertex,TEXT("a"));
			if( BaseOpacity && !OpacityAttenuation(C,Blending,BaseOpacity,BaseMod) )
				return 0;
			if( !InStage && (Projector->bGradient || !Projector->bProjectOnBackfaces) )
				Attenuate(C,Blending,Vertex,TEXT("a"));
		}
		else
		{
			K(Projector->bGradient);
			if( Projector->bGradient && Projector->Gradient )
			{
				FOpenGLModifierInfo GradientMod;
				GradientMod.SetSource(TCS_WorldCoords,Projector->GradientMatrix,TCN_3DCoords);
				INT G = Sample(Projector->Gradient,GradientMod);
				if( G == INDEX_NONE )
					return 0;
				Attenuate(C,Blending,G,TEXT("a"));
			}
			if( Projector->bProjectOnAlpha && BaseOpacity && !OpacityAttenuation(C,Blending,BaseOpacity,InMod) )
				return 0;
			K(Projector->bProjectOnBackfaces);
			if( !Projector->bProjectOnBackfaces )
			{
				// Fades surfaces facing away from the projector, which D3D9Drv looks up in a generated cube map.
				FCoords Axes = Projector->Matrix.Coords();
				FMatrix NormalMatrix = FMatrix::Identity;
				FVector Z = -Axes.ZAxis.SafeNormal();
				NormalMatrix.M[0][0] = Z.X;
				NormalMatrix.M[0][1] = Z.Y;
				NormalMatrix.M[0][2] = Z.Z;
				INT Slot = AddMatrix(NormalMatrix);
				if( Slot == INDEX_NONE )
					return 0;
				INT F = NewTemp();
				Emit(TEXT("\tvec4 r%i = vec4(max(dot(normalize(vWorldNormal), m%i[0].xyz), 0.0));\n"),F,Slot);
				Attenuate(C,Blending,F,TEXT("a"));
			}
		}

		State.TwoSided	= Projector->bTwoSided;
		State.ZWrite	= 0;
		if( Blending == PB_Add )
			SetBlend(GL_ONE,GL_ONE,FOG_Black);
		else if( Blending == PB_AlphaBlend )
			SetBlend(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,FOG_Normal);
		else
			SetBlend(GL_DST_COLOR,GL_SRC_COLOR,FOG_Grey);
		Output(C);
		return 1;
	}

	UBOOL OpacityAttenuation(INT C,BYTE Blending,UMaterial* BaseOpacity,const FOpenGLModifierInfo& Mod)
	{
		INT O = Combined(BaseOpacity,Mod);
		if( O == INDEX_NONE )
			return 0;
		Attenuate(C,Blending,O,TEXT("a"));
		return 1;
	}

	UBOOL LightingOnly()
	{
		INT C = NewTemp();
		Emit(TEXT("\tvec4 r%i = vec4(1.0);\n"),C);
		if( ApplyLighting(C,1,S->Lightmap != NULL) == INDEX_NONE )
			return 0;
		Output(C);
		return 1;
	}

	UBOOL Compile(UMaterial* Material)
	{
		FOpenGLModifierInfo	Mod;
		UShader*			Shader;
		UTerrainMaterial*	Terrain;
		UParticleMaterial*	Particle;
		UProjectorMaterial*	Projector;

		if( S->LightingOnly )
			return LightingOnly();
		Beam = IsLightBeam(Material);
		Heat = RI->RenDev->HeatHaze ? HeatKind(RI->RenDev,Material) : HEAT_None;
		UMaterial* Base = NULL;
		if( (Shader = CheckMaterial<UShader,MT_Shader>(Time,Material,&Mod)) != NULL
		||	(Base = CheckMaterial<UCombiner,MT_Combiner>(Time,Material,&Mod)) != NULL
		||	(Base = CheckMaterial<UConstantMaterial,MT_ConstantMaterial>(Time,Material,&Mod)) != NULL
		||	(Base = CheckMaterial<UBitmapMaterial,MT_BitmapMaterial>(Time,Material,&Mod)) != NULL )
		{
			if( !(Shader ? this->Shader(Shader,Mod) : Simple(Base,Mod)) )
				return 0;
			// Leaves and other cutouts, not particles, projectors or terrain layers. Reflective films such as
			// puddles lie on the floor, their own depth would be a step that ambient occlusion and contact shadows outline.
			State.DepthPass = !State.Beam && !State.Reflectivity && State.AlphaTest && !State.ZWrite && State.ZTest && State.DstBlend == GL_ONE_MINUS_SRC_ALPHA;
			return 1;
		}
		if( (Terrain = CheckMaterial<UTerrainMaterial,MT_TerrainMaterial>(Time,Material,&Mod)) != NULL )
			return this->Terrain(Terrain);
		if( (Particle = CheckMaterial<UParticleMaterial,MT_ParticleMaterial>(Time,Material,&Mod)) != NULL )
			return this->Particle(Particle);
		if( (Projector = CheckMaterial<UProjectorMaterial,MT_ProjectorMaterial>(Time,Material,&Mod)) != NULL )
			return this->Projector(Projector,Mod);
		Fail(TEXT("Unsupported material"),Material);
		return 0;
	}

	// Uniform declarations depend on what the walk used, so they go in front of the body.
	FString Declarations()
	{
		FString Result;
		for( INT i=0; i<State.NumTextures; i++ )
			Result += FString::Printf(Cube[i] ? TEXT("uniform samplerCube s%i;\n") : TEXT("uniform sampler2D s%i;\n"),i);
		for( INT i=0; i<State.NumMatrices; i++ )
			Result += FString::Printf(TEXT("uniform mat4 m%i;\n"),i);
		for( INT i=0; i<State.NumConstants; i++ )
			Result += FString::Printf(TEXT("uniform vec4 c%i;\n"),i);
		return Result;
	}
};

static TArray<ANSICHAR> ToAnsi(const FString& Source)
{
	TArray<ANSICHAR> Result;
	Result.Add(Source.Len() + 1);
	for( INT i=0; i<Source.Len(); i++ )
		Result(i) = (ANSICHAR)(*Source)[i];
	Result(Source.Len()) = 0;
	return Result;
}

UBOOL FOpenGLRenderInterface::CompileMaterial(UMaterial* Material,FString* ErrorString,UMaterial** ErrorMaterial)
{
	guard(FOpenGLRenderInterface::CompileMaterial);

	FOpenGLMaterialState& State = CurrentState->Material;
	// Kept across calls, this runs for every draw.
	static TArray<BYTE> Key;
	static TArray<FOpenGLProgram*> Candidates;
	Key.Empty(Key.Num());
	Candidates.Empty(Candidates.Num());
	State.Reset();
	FOpenGLCompiler Compiler(this,State,Key,NULL,ErrorString,ErrorMaterial);
	if( !Compiler.Compile(Material) )
		return 0;
	Compiler.K(State.NumTextures);
	Compiler.K(State.NumMatrices);
	Compiler.K(State.NumConstants);
	Compiler.K(CurrentState->Instanced);

	DWORD Hash = appMemCrc(&Key(0),Key.Num());
	RenDev->Programs.MultiFind(Hash,Candidates);
	for( INT i=0; i<Candidates.Num(); i++ )
	{
		if( Candidates(i)->Key.Num() == Key.Num() && appMemcmp(&Candidates(i)->Key(0),&Key(0),Key.Num()) == 0 )
		{
			State.Program = Candidates(i);
			return 1;
		}
	}

	// A new structure, walk again and write the code this time.
	FString Body;
	TArray<BYTE> Unused;
	State.Reset();
	FOpenGLCompiler Writer(this,State,Unused,&Body,ErrorString,ErrorMaterial);
	Writer.Compile(Material);

	// Two detail levels that hand over keep exactly one of them per sample. Each multisample gets its own
	// threshold, which the resolve averages into a smooth fade. With a single sample the noise is new
	// every frame instead, so the eye does the averaging. A hash, as gradient noise shifted per frame keeps a visible pattern.
	const TCHAR* Fade = CurrentState->Instanced ? TEXT("in vec2 vFade;\n") : TEXT("");
	const TCHAR* Dissolve = CurrentState->Instanced
		? TEXT("\tfloat Samples = 1.0;\n")
		  TEXT("#ifdef GL_OES_sample_variables\n")
		  TEXT("\tSamples = float(gl_NumSamples);\n")
		  TEXT("#endif\n")
		  TEXT("\tuvec3 At = uvec3(gl_FragCoord.xy, Samples > 1.0 ? 0.0 : Options.w);\n")
		  TEXT("\tuint H = At.x * 1973u + At.y * 9277u + At.z * 26699u;\n")
		  TEXT("\tH = (H ^ (H >> 16)) * 0x7feb352du;\n")
		  TEXT("\tH = (H ^ (H >> 15)) * 0x846ca68bu;\n")
		  TEXT("\tfloat Dither = float(H ^ (H >> 16)) / 4294967296.0;\n")
		  TEXT("\tint Mask = 0;\n")
		  TEXT("\tfor (int i = 0; i < int(Samples); i++)\n")
		  TEXT("\t{\n")
		  TEXT("\t\tfloat T = fract(Dither + float(i) / Samples);\n")
		  TEXT("\t\tif (vFade.x > T && vFade.y >= 1.0 - T) Mask |= 1 << i;\n")
		  TEXT("\t}\n")
		  TEXT("\tif (Mask == 0) discard;\n")
		  TEXT("#ifdef GL_OES_sample_variables\n")
		  TEXT("\tgl_SampleMask[0] = Mask;\n")
		  TEXT("#endif\n")
		: TEXT("");
	FString Fragment = FString(OpenGLFragmentPrologue) + Fade + Writer.Declarations() + TEXT("void main()\n{\n") + Dissolve + Body + TEXT("}\n");
	FOpenGLProgram* Program = new FOpenGLProgram;
	Program->Key = Key;
	// No real value has this pattern, so the first draw sends everything.
	appMemset(&Program->SentLocalToWorld,0xff,sizeof(Program->SentLocalToWorld));
	appMemset(Program->SentMatrices,0xff,sizeof(Program->SentMatrices));
	appMemset(Program->SentConstants,0xff,sizeof(Program->SentConstants));
	appMemset(&Program->SentAlphaRef,0xff,sizeof(Program->SentAlphaRef));
	Program->Program = RenDev->StartProgram(CurrentState->Instanced ? RenDev->InstancedVertexShaderObject : RenDev->VertexShaderObject,&ToAnsi(Fragment)(0),Program->FragmentShader);
	Program->Pending = 1;
	RenDev->Programs.Add(Hash,Program);
	State.Program = Program;
	return 1;

	unguard;
}
