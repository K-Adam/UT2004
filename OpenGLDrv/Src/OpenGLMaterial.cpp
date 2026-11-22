// The fixed function drivers squeeze a material into a few texture stages and fall back to extra
// passes or simpler materials when it does not fit. Here every material becomes one generated
// program, so there are no stage limits, no fallbacks and a single pass. The equations follow
// D3D9Drv, the renderer the content was made for.

#include "OpenGLDrv.h"

#define OGL_SCENE_BLOCK \
	"layout(std140) uniform Scene\n" \
	"{\n" \
	"	mat4 WorldToCamera;\n" \
	"	mat4 CameraToScreen;\n" \
	"	vec4 ClipAdjust;\n" \
	"	vec4 Fog;\n" \
	"	vec4 FogColor;\n" \
	"	vec4 Ambient;\n" \
	"	vec4 Options;\n" \
	"	vec4 FogUp;\n" \
	"	vec4 LightPosition[8];\n" \
	"	vec4 LightDirection[8];\n" \
	"	vec4 LightColor[8];\n" \
	"	vec4 LightParams[8];\n" \
	"};\n"

// Compiled behind a header with the version line, and INSTANCED for the variant DrawInstanced uses.
const ANSICHAR* OpenGLVertexShader =
	OGL_SCENE_BLOCK
	"uniform mat4 LocalToWorld;\n"
	"uniform mat3 NormalToWorld;\n"
	"in vec4 aPosition;\n"
	"in vec3 aNormal;\n"
	"in vec4 aDiffuse;\n"
	"in vec4 aSpecular;\n"
	"in vec4 aTexCoord0, aTexCoord1, aTexCoord2, aTexCoord3, aTexCoord4, aTexCoord5, aTexCoord6, aTexCoord7;\n"
	"out vec3 vWorldPos;\n"
	"out vec3 vRestPos;\n"
	"out vec3 vWorldNormal;\n"
	"out vec3 vCameraPos;\n"
	"out vec4 vDiffuse;\n"
	"out vec4 vSpecular;\n"
	"out vec4 vTexCoord0, vTexCoord2, vTexCoord3, vTexCoord4, vTexCoord5, vTexCoord6, vTexCoord7;\n"
	// The lightmap coordinate, an edge pixel must not leave the surface's atlas rectangle.
	"centroid out vec4 vTexCoord1;\n"
	"#ifdef INSTANCED\n"
	"in vec4 aInstance0, aInstance1, aInstance2, aInstance3;\n"
	"uniform mat4 WorldToLightmap;\n"
	"uniform vec4 InstanceRange;\n"
	"uniform vec4 Wind;\n"
	"out vec2 vFade;\n"
	"vec3 Rotate(vec3 V)\n"
	"{\n"
	"	return V + 2.0 * cross(aInstance1.xyz, cross(aInstance1.xyz, V) + aInstance1.w * V);\n"
	"}\n"
	"#endif\n"
	"void main()\n"
	"{\n"
	"#ifdef INSTANCED\n"
	// X is how much of the instance is left toward its far end, Y how much has arrived at the near end
	// of the range. The fragment shader dithers both, which keeps depth writes and needs no sorting.
	"	vec3 Base = (LocalToWorld * vec4(aInstance0.xyz, 1.0)).xyz;\n"
	"	float Distance = length((WorldToCamera * vec4(Base, 1.0)).xyz);\n"
	"	float Gone = clamp((aInstance0.w - Distance) / max(aInstance0.w - aInstance2.w, 1.0), 0.0, 1.0);\n"
	"	vFade.x = min(Gone, clamp((InstanceRange.y - Distance) / InstanceRange.z, 0.0, 1.0));\n"
	"	vFade.y = InstanceRange.x > 0.0 ? clamp((Distance - InstanceRange.x) / InstanceRange.w + 1.0, 0.0, 1.0) : 1.0;\n"
	"	vec3 Offset = Rotate(aPosition.xyz * aInstance2.xyz);\n"
	"	vec3 Normal = Rotate(aNormal / aInstance2.xyz);\n"
	"	vec3 Rest = Base + mat3(LocalToWorld) * Offset;\n"
	"	float Strength = length(Wind.xy);\n"
	"	if (Strength > 0.0)\n"
	"	{\n"
	"		vec3 Dir = vec3(Wind.xy / Strength, 0.0);\n"
	"		vec3 Up = Rotate(vec3(0.0, 0.0, 1.0));\n"
	"		float Along = dot(Base.xy, Dir.xy);\n"
	"		float Across = dot(Base.xy, vec2(-Dir.y, Dir.x));\n"
	"		float Plant = fract(52.9829189 * fract(dot(Base.xy, vec2(0.06711056, 0.00583715))));\n"
	// Two gust fronts that travel downwind read at the root so a plant moves as one, the plant's own sway and
	// its leaves' flutter. Every time factor is a multiple of 1/1024, the period the device wraps the time at.
	// As triangle waves with their corners smoothed, cheaper than sines and they repeat exactly every whole phase.
	"		vec4 W = abs(fract(vec4(\n"
	"			Along / 3000.0 + Across / 9000.0 - Wind.z * 0.125,\n"
	"			Along / 1100.0 - Across / 2600.0 - Wind.z * 0.3125,\n"
	"			Plant + Wind.z * 0.75,\n"
	"			Plant + dot(aPosition.xyz, vec3(0.013, 0.017, 0.011)) + Wind.z * 2.5)) * 2.0 - 1.0);\n"
	"		W = W * W * (3.0 - 2.0 * W);\n"
	"		float Gust = W.x * (0.4 + 0.6 * W.y);\n"
	"		float Tall = aPosition.z * aInstance2.z;\n"
	// The angle grows with height, which curves the plant. Leaning each vertex from the point of the stem below
	// it instead of turning the whole plant about its root keeps the blades of a wide clump at their length.
	"		float Angle = Strength * aPosition.z * abs(Wind.w) * (0.25 + Gust + 0.2 * (W.z - 0.5));\n"
	"		Offset += (Dir * sin(Angle) + Up * (cos(Angle) - 1.0)) * Tall;\n"
	"		Normal += (Dir * dot(Normal, Up) - Up * dot(Normal, Dir)) * (2.0 * Angle);\n"
	// The colour is how open to the sky a vertex is, so the outer leaves flutter and the inside holds still.
	// One direction for every vertex, along the normal the two sides of a twig would move apart and it would swell.
	"		Offset += vec3(-Dir.y, Dir.x, 0.35) * (step(0.0, Wind.w) * (W.w - 0.5) * Strength * 0.1 * Tall * (0.3 + Gust) * smoothstep(0.55, 0.9, aDiffuse.r));\n"
	"	}\n"
	"	vec4 WorldPos = vec4(Base + mat3(LocalToWorld) * Offset, 1.0);\n"
	"	vec4 Diffuse = aInstance3 * aDiffuse;\n"
	"#else\n"
	"	vec4 WorldPos = LocalToWorld * vec4(aPosition.xyz, 1.0);\n"
	"	vec3 Normal = aNormal;\n"
	"	vec4 Diffuse = aDiffuse;\n"
	"#endif\n"
	"	vec4 CameraPos = WorldToCamera * WorldPos;\n"
	"	vec4 P = CameraToScreen * CameraPos;\n"
	// D3D depth runs from 0 to 1, D3D pixel centres sit on integer coordinates and
	// render targets are flipped so they are stored top down like in D3D.
	"	P.y *= ClipAdjust.x;\n"
	"	P.xy += ClipAdjust.zw * P.w;\n"
	"	P.z = 2.0 * P.z - P.w;\n"
	"#ifdef INSTANCED\n"
	// Instances outside the range fold into one point and cost no pixels.
	"	if (min(vFade.x, vFade.y) <= 0.0)\n"
	"		P = vec4(2.0, 2.0, 2.0, 1.0);\n"
	"#endif\n"
	"	gl_Position = P;\n"
	"	gl_PointSize = 1.0;\n"
	"	vWorldPos = WorldPos.xyz;\n"
	// Where the vertex is without wind, for what has to stay on a surface that sways.
	"#ifdef INSTANCED\n"
	"	vRestPos = Rest;\n"
	"#else\n"
	"	vRestPos = WorldPos.xyz;\n"
	"#endif\n"
	"	vWorldNormal = NormalToWorld * Normal;\n"
	"	vCameraPos = CameraPos.xyz;\n"
	"	vDiffuse = Diffuse.bgra;\n"
	"	vSpecular = aSpecular.bgra;\n"
	"	vTexCoord0 = aTexCoord0; vTexCoord1 = aTexCoord1; vTexCoord2 = aTexCoord2; vTexCoord3 = aTexCoord3;\n"
	"#ifdef INSTANCED\n"
	// Read where the plant stands at rest, or the light baked on the ground would crawl over the swaying leaves.
	"	vTexCoord1 = WorldToLightmap * vec4(Rest, 1.0);\n"
	"#endif\n"
	"	vTexCoord4 = aTexCoord4; vTexCoord5 = aTexCoord5; vTexCoord6 = aTexCoord6; vTexCoord7 = aTexCoord7;\n"
	"}\n";

const ANSICHAR* OpenGLFragmentPrologue =
	"#version 300 es\n"
	// For the foliage fade's sample mask, WebGL 2 lacks it.
	"#ifdef GL_OES_sample_variables\n"
	"#extension GL_OES_sample_variables : enable\n"
	"#endif\n"
	"precision highp float;\n"
	OGL_SCENE_BLOCK
	"in vec3 vWorldPos;\n"
	"in vec3 vRestPos;\n"
	"in vec3 vWorldNormal;\n"
	"in vec3 vCameraPos;\n"
	"in vec4 vDiffuse;\n"
	"in vec4 vSpecular;\n"
	"in vec4 vTexCoord0, vTexCoord2, vTexCoord3, vTexCoord4, vTexCoord5, vTexCoord6, vTexCoord7;\n"
	"centroid in vec4 vTexCoord1;\n"
	"uniform float AlphaRef;\n"
	"layout(location = 0) out vec4 FragColor;\n"
	// What the env map added and how reflective the surface is, the screen space reflections replace it.
	"layout(location = 1) out vec4 Reflection;\n"
	// The surface colour before lighting, which the global illumination tints the bounced light with.
	"layout(location = 2) out vec4 Albedo;\n"
	// FDynamicLight::SampleIntensity per pixel. Light types: 0 sunlight, 1 cylinder,
	// 2 non incidence, 3 quadratic non incidence, 4 point, 5 spot.
	"float SampleIntensity(int i, vec3 P, vec3 N)\n"
	"{\n"
	"	float Type = LightParams[i].x;\n"
	"	if (Type < 0.5)\n"
	"	{\n"
	"		float D = dot(LightDirection[i].xyz, N);\n"
	"		return D < 0.0 ? -2.0 * D : 0.0;\n"
	"	}\n"
	"	vec3 V = LightPosition[i].xyz - P;\n"
	"	float R = LightPosition[i].w;\n"
	"	float D2 = dot(V, V);\n"
	"	float D = sqrt(D2);\n"
	"	if (Type < 1.5)\n"
	"		return D < R ? max(0.0, 1.0 - dot(V.xy, V.xy) / (R * R)) * 2.0 : 0.0;\n"
	"	float VN = dot(V, N);\n"
	// Per vertex these lights blend out between vertices. Per pixel they would end in a hard line
	// where the surface turns away and at the radius, so both ends fade.
	"	float Soft = smoothstep(0.0, 0.1, VN / max(D, 0.0001)) * (1.0 - smoothstep(0.8, 1.0, D / R));\n"
	"	if (Type < 2.5)\n"
	"		return sqrt(max(1.02 - D / R, 0.0)) * 2.0 * Soft;\n"
	"	if (Type < 3.5)\n"
	"		return max(1.02 - D2 / (R * R), 0.0) * 2.0 * Soft;\n"
	"	if (VN <= 0.0 || D > R)\n"
	"		return 0.0;\n"
	"	float A = D / R;\n"
	"	float Attenuation = (2.0 * A * A * A - 3.0 * A * A + 1.0) * VN / max(D, 0.0001) * 2.0;\n"
	"	if (Type < 4.5)\n"
	"		return Attenuation;\n"
	"	float Sine = LightParams[i].y;\n"
	"	float RSine = 1.0 / max(1.0 - Sine, 0.0001);\n"
	"	float VDotV = -dot(V, LightDirection[i].xyz);\n"
	"	if (VDotV > 0.0 && VDotV * VDotV > Sine * Sine * D2)\n"
	"	{\n"
	"		float S = VDotV * RSine / D - Sine * RSine;\n"
	"		return S * S * Attenuation;\n"
	"	}\n"
	"	return 0.0;\n"
	"}\n"
	// What D3D's lit diffuse colour would be: static vertex lighting as emissive plus
	// dynamic lights scaled by the specular stream, or the vertex colour when unlit.
	"vec4 Lighting()\n"
	"{\n"
	"	if (Options.x < 0.5)\n"
	"		return vDiffuse;\n"
	"	vec4 Material = Options.y > 0.5 ? vSpecular : vec4(1.0);\n"
	"	vec3 N = vWorldNormal;\n"
	"	float Length = dot(N, N);\n"
	"	N = Length > 0.0 ? N * inversesqrt(Length) : N;\n"
	"	vec3 Light = Ambient.rgb;\n"
	"	int Count = int(Ambient.w);\n"
	"	for (int i = 0; i < 8; i++)\n"
	"	{\n"
	"		if (i >= Count)\n"
	"			break;\n"
	"		Light += LightColor[i].rgb * SampleIntensity(i, vWorldPos, N);\n"
	"	}\n"
	"	vec3 Emissive = Options.y > 0.5 ? vDiffuse.rgb : vec3(0.0);\n"
	"	return clamp(vec4(Emissive + Light * Material.rgb, Material.a), 0.0, 1.0);\n"
	"}\n"
	"vec3 CameraReflection()\n"
	"{\n"
	"	vec3 N = normalize(mat3(WorldToCamera) * vWorldNormal);\n"
	"	vec3 E = -normalize(vCameraPos);\n"
	"	return 2.0 * dot(E, N) * N - E;\n"
	"}\n"
	// The cubemap holds the surroundings as seen from its capture point Probe[0], so the reflected ray
	// is cut at the box Probe[1] to Probe[2] around that point and the cube looked up toward the hit.
	"uniform vec3 Probe[3];\n"
	"vec3 ProbeReflection()\n"
	"{\n"
	"	vec3 R = CameraReflection() * mat3(WorldToCamera);\n"
	"	vec3 P = vWorldPos;\n"
	// The faces keep their distance from pixels near or past them, a pixel on a face would see only that
	// spot of it and the projection would smear there.
	"	vec3 Side = step(0.0, R);\n"
	"	vec3 Face = mix(min(Probe[1], P - 256.0), max(Probe[2], P + 256.0), Side);\n"
	"	vec3 T = (Face - P) / ((Side * 2.0 - 1.0) * max(abs(R), vec3(1e-5)));\n"
	"	return mat3(WorldToCamera) * (P + R * min(min(T.x, T.y), T.z) - Probe[0]);\n"
	"}\n"
	OGL_FOG_AMOUNT
	"vec3 ApplyFog(vec3 Color, vec3 Target)\n"
	"{\n"
	"	return mix(Color, Target, FogAt(vCameraPos, FogUp.xyz, Fog));\n"
	"}\n";

void FOpenGLMaterialState::Reset()
{
	Program				= NULL;
	NumTextures			= 0;
	NumMatrices			= 0;
	NumConstants		= 0;
	GlobalColorConstant	= INDEX_NONE;
	SrcBlend			= GL_ONE;
	DstBlend			= GL_ZERO;
	AlphaTest			= 0;
	AlphaRef			= 0.f;
	ZWrite				= 1;
	ZTest				= 1;
	TwoSided			= 0;
	Beam				= 0;
	Heat				= 0;
	DepthPass			= 0;
	Reflectivity		= 0;
}

UBOOL UOpenGLRenderDevice::ProgramReady(FOpenGLProgram* P,UBOOL Wait)
{
	if( !P->Pending )
		return 1;
	if( ParallelCompile && !Wait )
	{
		GLint Done = 0;
		glGetProgramiv(P->Program,GL_COMPLETION_STATUS_KHR,&Done);
		if( !Done )
			return 0;
	}
	P->Pending = 0;
	P->Program = FinishProgram(P->Program,P->FragmentShader);
	if( !P->Program )
		return 1;

	P->LocalToWorld		= glGetUniformLocation(P->Program,"LocalToWorld");
	P->NormalToWorld	= glGetUniformLocation(P->Program,"NormalToWorld");
	P->WorldToLightmap	= glGetUniformLocation(P->Program,"WorldToLightmap");
	P->InstanceRange	= glGetUniformLocation(P->Program,"InstanceRange");
	P->Wind				= glGetUniformLocation(P->Program,"Wind");
	P->AlphaRef			= glGetUniformLocation(P->Program,"AlphaRef");
	P->Probe			= glGetUniformLocation(P->Program,"Probe");
	UseProgram(P->Program);
	ANSICHAR Name[8];
	// Samplers the program lacks have location -1, which glUniform ignores.
	for( INT i=0; i<OGL_MAX_TEXTURES; i++ )
	{
		sprintf(Name,"s%i",i);
		glUniform1i(glGetUniformLocation(P->Program,Name),i);
	}
	for( INT i=0; i<OGL_MAX_MATRICES; i++ )
	{
		sprintf(Name,"m%i",i);
		P->Matrices[i] = glGetUniformLocation(P->Program,Name);
	}
	for( INT i=0; i<OGL_MAX_CONSTANTS; i++ )
	{
		sprintf(Name,"c%i",i);
		P->Constants[i] = glGetUniformLocation(P->Program,Name);
	}
	return 1;
}

void FOpenGLRenderInterface::SetMaterial(UMaterial* Material,FString* ErrorString,UMaterial** ErrorMaterial,INT* NumPasses)
{
	guard(FOpenGLRenderInterface::SetMaterial);

	UMaterial* DefaultMaterial = Cast<UMaterial>(UMaterial::StaticClass()->GetDefaultObject())->DefaultMaterial;
	if( !Material || PrecacheMode == PRECACHE_VertexBuffers )
		Material = DefaultMaterial;

	if( GIsEditor )
	{
		TArray<UMaterial*> History;
		if( !Material->CheckCircularReferences(History) )
		{
			if( ErrorMaterial && History.Num() )
				*ErrorMaterial = History.Last();
			if( ErrorString && History.Num() )
				*ErrorString = FString::Printf(TEXT("Circular material reference in %s"),History.Last()->GetName());
			Material = DefaultMaterial;
		}
	}

	Material->PreSetMaterial(Viewport->Actor ? Viewport->Actor->Level->TimeSeconds : 0.f);
	FString Reason;
	if( !ErrorString )
		ErrorString = &Reason;
	if( !CompileMaterial(Material,ErrorString,ErrorMaterial) )
	{
		// Content the fixed function drivers could not render may name a chain of fallbacks. Only the
		// editor rejects circular chains.
		UBOOL Compiled = 0;
		UMaterial* Fallback = Material->FallbackMaterial;
		for( INT i=0; i<8 && Fallback && !Compiled; i++, Fallback=Fallback->FallbackMaterial )
			Compiled = CompileMaterial(Fallback,NULL,NULL);
		if( !Compiled )
		{
			// This runs for every draw, so a material is reported once.
			static TArray<FString> Reported;
			FString Name = Material->GetPathName();
			if( Reported.FindItemIndex(Name)==INDEX_NONE )
			{
				new(Reported)FString(Name);
				debugf(NAME_Warning,TEXT("OpenGLDrv: %s is not renderable: %s"),Material->GetFullName(),**ErrorString);
			}
			CompileMaterial(DefaultMaterial,NULL,NULL);
		}
	}

	if( NumPasses )
		*NumPasses = 1;

	unguard;
}
