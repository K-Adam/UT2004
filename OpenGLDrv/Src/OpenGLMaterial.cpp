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

const ANSICHAR* OpenGLVertexShader =
	"#version 300 es\n"
	OGL_SCENE_BLOCK
	"uniform mat4 LocalToWorld;\n"
	"uniform mat3 NormalToWorld;\n"
	"in vec4 aPosition;\n"
	"in vec3 aNormal;\n"
	"in vec4 aDiffuse;\n"
	"in vec4 aSpecular;\n"
	"in vec4 aTexCoord0, aTexCoord1, aTexCoord2, aTexCoord3, aTexCoord4, aTexCoord5, aTexCoord6, aTexCoord7;\n"
	"out vec3 vWorldPos;\n"
	"out vec3 vWorldNormal;\n"
	"out vec3 vCameraPos;\n"
	"out vec4 vDiffuse;\n"
	"out vec4 vSpecular;\n"
	"out vec4 vTexCoord0, vTexCoord1, vTexCoord2, vTexCoord3, vTexCoord4, vTexCoord5, vTexCoord6, vTexCoord7;\n"
	"void main()\n"
	"{\n"
	"	vec4 WorldPos = LocalToWorld * vec4(aPosition.xyz, 1.0);\n"
	"	vec4 CameraPos = WorldToCamera * WorldPos;\n"
	"	vec4 P = CameraToScreen * CameraPos;\n"
	// D3D depth runs from 0 to 1, D3D pixel centres sit on integer coordinates and
	// render targets are flipped so they are stored top down like in D3D.
	"	P.y *= ClipAdjust.x;\n"
	"	P.xy += ClipAdjust.zw * P.w;\n"
	"	P.z = 2.0 * P.z - P.w;\n"
	"	gl_Position = P;\n"
	"	gl_PointSize = 1.0;\n"
	"	vWorldPos = WorldPos.xyz;\n"
	"	vWorldNormal = NormalToWorld * aNormal;\n"
	"	vCameraPos = CameraPos.xyz;\n"
	"	vDiffuse = aDiffuse.bgra;\n"
	"	vSpecular = aSpecular.bgra;\n"
	"	vTexCoord0 = aTexCoord0; vTexCoord1 = aTexCoord1; vTexCoord2 = aTexCoord2; vTexCoord3 = aTexCoord3;\n"
	"	vTexCoord4 = aTexCoord4; vTexCoord5 = aTexCoord5; vTexCoord6 = aTexCoord6; vTexCoord7 = aTexCoord7;\n"
	"}\n";

const ANSICHAR* OpenGLFragmentPrologue =
	"#version 300 es\n"
	"precision highp float;\n"
	OGL_SCENE_BLOCK
	"in vec3 vWorldPos;\n"
	"in vec3 vWorldNormal;\n"
	"in vec3 vCameraPos;\n"
	"in vec4 vDiffuse;\n"
	"in vec4 vSpecular;\n"
	"in vec4 vTexCoord0, vTexCoord1, vTexCoord2, vTexCoord3, vTexCoord4, vTexCoord5, vTexCoord6, vTexCoord7;\n"
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
			debugf(NAME_Warning,TEXT("OpenGLDrv: %s is not renderable"),Material->GetFullName());
			CompileMaterial(DefaultMaterial,NULL,NULL);
		}
	}

	if( NumPasses )
		*NumPasses = 1;

	unguard;
}
