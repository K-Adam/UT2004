// Post processing of the world: screen space reflections, ambient occlusion and contact shadows from
// depth, bloom, gamma and film grain. The HUD draws over the result unchanged.

#include "OpenGLDrv.h"

#define STRINGIFY2(X) #X
#define STRINGIFY(X) STRINGIFY2(X)

const ANSICHAR* FullscreenVertexShader =
	"#version 300 es\n"
	"out vec2 UV;\n"
	"void main()\n"
	"{\n"
	"	vec2 P = vec2(gl_VertexID == 1 ? 3.0 : -1.0, gl_VertexID == 2 ? 3.0 : -1.0);\n"
	"	UV = P * 0.5 + 0.5;\n"
	"	gl_Position = vec4(P, 0.0, 1.0);\n"
	"}\n";

static const ANSICHAR* CopyFragmentShader =
	"#version 300 es\n"
	"precision mediump float;\n"
	"uniform sampler2D Frame;\n"
	"in vec2 UV;\n"
	"out vec4 FragColor;\n"
	"void main()\n"
	"{\n"
	"	FragColor = vec4(texture(Frame, UV).rgb, 1.0);\n"
	"}\n";

// Proj holds 1 / M00, 1 / M11, M22 and M32 of the player's projection. The weapon shares the near and
// far planes, so its depth linearizes the same way once taken out of its range. A neighbour across an
// edge would tilt the normal, so the one nearer in depth is taken. At the border the clamped neighbour
// is the centre itself and gives no direction.
#define POST_COMMON \
	"#version 300 es\n" \
	"precision highp float;\n" \
	"precision highp int;\n" \
	"uniform highp sampler2D Depth;\n" \
	"uniform vec4 Proj;\n" \
	"in vec2 UV;\n" \
	"out vec4 FragColor;\n" \
	"const float Split = " STRINGIFY(VIEW_MODEL_DEPTH) ";\n" \
	"float LinearZ(float D)\n" \
	"{\n" \
	"	D = D < Split ? D / Split : (D - Split) / (1.0 - Split);\n" \
	"	return Proj.w / (D - Proj.z);\n" \
	"}\n" \
	"ivec2 ClampPixel(ivec2 P)\n" \
	"{\n" \
	"	return clamp(P, ivec2(0), textureSize(Depth, 0) - 1);\n" \
	"}\n" \
	"float DepthAt(ivec2 P)\n" \
	"{\n" \
	"	return texelFetch(Depth, ClampPixel(P), 0).r;\n" \
	"}\n" \
	"vec3 Nearer(vec3 A, vec3 B)\n" \
	"{\n" \
	"	return dot(B, B) == 0.0 || (dot(A, A) > 0.0 && abs(A.z) < abs(B.z)) ? A : B;\n" \
	"}\n" \
	"vec3 PosAt(ivec2 P)\n" \
	"{\n" \
	"	P = ClampPixel(P);\n" \
	"	float Z = LinearZ(texelFetch(Depth, P, 0).r);\n" \
	"	vec2 NDC = (vec2(P) + 0.5) / vec2(textureSize(Depth, 0)) * 2.0 - 1.0;\n" \
	"	return vec3(NDC * Proj.xy * Z, Z);\n" \
	"}\n" \
	"vec2 ToUV(vec3 P)\n" \
	"{\n" \
	"	return P.xy / (P.z * Proj.xy) * 0.5 + 0.5;\n" \
	"}\n" \
	"vec3 NormalAt(ivec2 P, vec3 C)\n" \
	"{\n" \
	"	vec3 L = PosAt(P - ivec2(1, 0)), R = PosAt(P + ivec2(1, 0)), B = PosAt(P - ivec2(0, 1)), T = PosAt(P + ivec2(0, 1));\n" \
	"	vec3 N = normalize(cross(Nearer(R - C, C - L), Nearer(T - C, C - B)));\n" \
	"	return dot(N, C) > 0.0 ? -N : N;\n" \
	"}\n" \
	"float Jitter()\n" \
	"{\n" \
	"	return fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));\n" \
	"}\n"

// R is the ambient occlusion and G the contact shadow, at half the world size. Light is the camera
// space position of a point light with W its radius, or the direction toward the sun with W 0.
static const ANSICHAR* OcclusionFragmentShader =
	POST_COMMON
	"uniform vec4 Light;\n"
	"uniform vec2 Strength;\n"
	"void main()\n"
	"{\n"
	"	ivec2 P = ivec2(gl_FragCoord.xy) * 2;\n"
	"	float D = DepthAt(P);\n"
	"	if (D >= 1.0)\n"
	"	{\n"
	"		FragColor = vec4(1.0);\n"
	"		return;\n"
	"	}\n"
	"	bool Weapon = D < Split;\n"
	"	vec3 C = PosAt(P);\n"
	"	vec3 N = NormalAt(P, C);\n"
	"	float Noise = Jitter();\n"
	"	float Height = float(textureSize(Depth, 0).y);\n"
	"	float AO = 1.0;\n"
	"	if (Strength.x > 0.0)\n"
	"	{\n"
	"		const float Radius = 56.0;\n"
	"		float Pixels = min(Radius * 0.5 * Height / (Proj.y * C.z), Height * 0.15);\n"
	"		float Sum = 0.0;\n"
	"		for (int i = 0; i < 12; i++)\n"
	"		{\n"
	"			float A = (float(i) + 0.5) / 12.0;\n"
	"			float Angle = 6.2831853 * (A * 7.0 + Noise);\n"
	"			ivec2 Q = P + ivec2(vec2(cos(Angle), sin(Angle)) * A * Pixels);\n"
	"			if ((DepthAt(Q) < Split) != Weapon)\n"
	"				continue;\n"
	"			vec3 V = PosAt(Q) - C;\n"
	"			float VV = dot(V, V);\n"
	"			Sum += max(dot(V, N) * inversesqrt(VV + 0.01) - 0.1, 0.0) * max(1.0 - VV / (Radius * Radius), 0.0);\n"
	"		}\n"
	"		AO = max(1.0 - Sum * Strength.x / 3.0, 0.0);\n"
	"	}\n"
	"	float Shadow = 1.0;\n"
	"	if (Strength.y > 0.0 && dot(Light.xyz, Light.xyz) > 0.0)\n"
	"	{\n"
	"		vec3 ToLight = Light.w > 0.0 ? Light.xyz - C : Light.xyz;\n"
	"		float Dist = length(ToLight);\n"
	"		ToLight /= Dist;\n"
	"		float Falloff = Light.w > 0.0 ? max(1.0 - Dist * Dist / (Light.w * Light.w), 0.0) : 1.0;\n"
	"		float Lit = dot(N, ToLight) * Falloff * Falloff;\n"
	"		if (Lit > 0.0)\n"
	"		{\n"
	"			const float Length = 24.0;\n"
	"			float Hit = 0.0;\n"
	"			for (int i = 0; i < 8; i++)\n"
	"			{\n"
	"				vec3 S = C + ToLight * (Length * (float(i) + Noise) / 8.0);\n"
	"				if (S.z < 1.0)\n"
	"					break;\n"
	"				vec2 SUV = ToUV(S);\n"
	"				if (any(lessThan(SUV, vec2(0.0))) || any(greaterThan(SUV, vec2(1.0))))\n"
	"					break;\n"
	"				float SD = DepthAt(ivec2(SUV * vec2(textureSize(Depth, 0))));\n"
	"				if ((SD < Split) != Weapon)\n"
	"					continue;\n"
	"				float Behind = S.z - LinearZ(SD);\n"
	"				if (Behind > 0.5 + 0.004 * S.z && Behind < 6.0 + 0.02 * S.z)\n"
	"				{\n"
	"					Hit = 1.0 - float(i) / 8.0;\n"
	"					break;\n"
	"				}\n"
	"			}\n"
	"			Shadow = 1.0 - Strength.y * Hit * min(Lit * 2.0, 1.0);\n"
	"		}\n"
	"	}\n"
	"	FragColor = vec4(AO, Shadow, 0.0, 1.0);\n"
	"}\n";

// Params holds the source texel size, Z is set when sampling the world. Nothing in the frame is
// brighter than white, so lamps are told apart by being far brighter than lit surfaces. Taking them
// before averaging keeps small lamps from fading into their dark surroundings.
#define BLOOM_COMMON \
	"#version 300 es\n" \
	"precision mediump float;\n" \
	"uniform sampler2D Frame;\n" \
	"uniform vec3 Params;\n" \
	"in vec2 UV;\n" \
	"out vec4 FragColor;\n" \
	"vec3 Tap(float X, float Y)\n" \
	"{\n" \
	"	vec3 C = texture(Frame, UV + vec2(X, Y) * Params.xy).rgb;\n" \
	"	return Params.z > 0.0 ? C * smoothstep(0.7, 0.9, max(C.r, max(C.g, C.b))) : C;\n" \
	"}\n"

static const ANSICHAR* BloomDownFragmentShader =
	BLOOM_COMMON
	"void main()\n"
	"{\n"
	"	vec3 A = Tap(-2.0, -2.0), B = Tap(0.0, -2.0), C = Tap(2.0, -2.0), D = Tap(-1.0, -1.0), E = Tap(1.0, -1.0);\n"
	"	vec3 F = Tap(-2.0, 0.0), G = Tap(0.0, 0.0), H = Tap(2.0, 0.0), I = Tap(-1.0, 1.0), J = Tap(1.0, 1.0);\n"
	"	vec3 K = Tap(-2.0, 2.0), L = Tap(0.0, 2.0), M = Tap(2.0, 2.0);\n"
	"	FragColor = vec4((D + E + I + J) * 0.125 + (A + C + K + M) * 0.03125 + (B + F + H + L) * 0.0625 + G * 0.125, 1.0);\n"
	"}\n";

// A 3x3 tent over the smaller level, added onto the larger one. Z weakens each wider level, so the
// glow stays close to its light like in a lens.
static const ANSICHAR* BloomUpFragmentShader =
	BLOOM_COMMON
	"void main()\n"
	"{\n"
	"	vec3 S = Tap(-1.0, -1.0) + Tap(1.0, -1.0) + Tap(-1.0, 1.0) + Tap(1.0, 1.0)\n"
	"		+ 2.0 * (Tap(0.0, -1.0) + Tap(-1.0, 0.0) + Tap(1.0, 0.0) + Tap(0.0, 1.0)) + 4.0 * Tap(0.0, 0.0);\n"
	"	FragColor = vec4(S * Params.z / 16.0, 1.0);\n"
	"}\n";

// Params holds the inverse gamma and the occlusion, bloom and reflection switches. Where the ray finds
// the reflected surface on screen it replaces the env map, fading out toward the screen edges and the
// ray's end so misses blend back into it. The half size occlusion is gathered 4x4 with depth
// weights, which also averages out its noise.
static const ANSICHAR* CompositeFragmentShader =
	POST_COMMON
	"uniform sampler2D Frame, Occlusion, Bloom, Reflection;\n"
	"uniform vec4 Params;\n"
	"uniform uint Seed;\n"
	"float Hash(uvec3 V)\n"
	"{\n"
	"	uint H = V.x * 1973u + V.y * 9277u + V.z * 26699u;\n"
	"	H = (H ^ (H >> 16)) * 0x7feb352du;\n"
	"	H = (H ^ (H >> 15)) * 0x846ca68bu;\n"
	"	return float(H ^ (H >> 16)) / 4294967295.0;\n"
	"}\n"
	"void main()\n"
	"{\n"
	"	vec3 Color = texture(Frame, UV).rgb;\n"
	"	ivec2 P = ivec2(UV * vec2(textureSize(Depth, 0)));\n"
	"	float D = DepthAt(P);\n"
	"	vec4 Refl = texelFetch(Reflection, P, 0);\n"
	"	if (Params.w > 0.0 && D >= Split && D < 1.0 && Refl.a > 0.004)\n"
	"	{\n"
	"		vec2 Size = vec2(textureSize(Depth, 0));\n"
	"		vec3 C = PosAt(P);\n"
	"		vec3 N = NormalAt(P, C);\n"
	"		vec3 R = reflect(normalize(C), N);\n"
	"		float Step = max(8.0, 0.02 * C.z);\n"
	"		vec3 S = C + N * (0.5 + 0.002 * C.z) + R * Step * Jitter();\n"
	"		float W = 0.0;\n"
	"		vec2 HitUV;\n"
	"		for (int i = 0; i < 24; i++)\n"
	"		{\n"
	"			S += R * Step;\n"
	"			if (S.z < 1.0)\n"
	"				break;\n"
	"			vec2 SUV = ToUV(S);\n"
	"			if (any(lessThan(SUV, vec2(0.0))) || any(greaterThan(SUV, vec2(1.0))))\n"
	"				break;\n"
	"			float SD = DepthAt(ivec2(SUV * Size));\n"
	"			float Behind = S.z - LinearZ(SD);\n"
	// Behind the weapon or the sky nothing is known.
	"			if (SD >= Split && SD < 1.0 && Behind > 0.0 && Behind < max(2.0 * Step, 8.0))\n"
	"			{\n"
	"				vec3 A = S - R * Step, B = S;\n"
	"				for (int j = 0; j < 5; j++)\n"
	"				{\n"
	"					vec3 M = (A + B) * 0.5;\n"
	"					if (M.z - LinearZ(DepthAt(ivec2(ToUV(M) * Size))) > 0.0)\n"
	"						B = M;\n"
	"					else\n"
	"						A = M;\n"
	"				}\n"
	"				HitUV = ToUV(B);\n"
	"				vec2 Edge = smoothstep(0.0, 0.1, min(HitUV, 1.0 - HitUV));\n"
	"				W = Edge.x * Edge.y * (1.0 - float(i) / 24.0);\n"
	"				break;\n"
	"			}\n"
	"			Step *= 1.25;\n"
	"		}\n"
	"		if (W > 0.0)\n"
	"			Color = max(Color + W * (texture(Frame, HitUV).rgb * Refl.a - Refl.rgb), 0.0);\n"
	"	}\n"
	"	if (Params.y > 0.0 && D < 1.0)\n"
	"	{\n"
	"		bool Weapon = D < Split;\n"
	"		float Z = LinearZ(D);\n"
	"		ivec2 Size = textureSize(Occlusion, 0);\n"
	"		vec2 Sum = vec2(0.0);\n"
	"		float Weights = 0.0;\n"
	"		for (int Y = -1; Y <= 2; Y++)\n"
	"			for (int X = -1; X <= 2; X++)\n"
	"			{\n"
	"				ivec2 Q = clamp(P / 2 + ivec2(X, Y), ivec2(0), Size - 1);\n"
	"				float QD = DepthAt(Q * 2);\n"
	"				if ((QD < Split) != Weapon || QD >= 1.0)\n"
	"					continue;\n"
	"				float W = max(1.0 - abs(LinearZ(QD) - Z) / (0.05 * Z + 1.0), 0.0) + 0.001;\n"
	"				Sum += texelFetch(Occlusion, Q, 0).rg * W;\n"
	"				Weights += W;\n"
	"			}\n"
	"		vec2 O = Weights > 0.0 ? Sum / Weights : vec2(1.0);\n"
	// Bright surfaces lose less light to occlusion, they bounce more of it back.
	"		vec3 A = 2.0404 * Color - 0.3324, B = -4.7951 * Color + 0.6417, K = 2.7552 * Color + 0.6903;\n"
	"		Color *= max(vec3(O.x), ((O.x * A + B) * O.x + K) * O.x) * O.y;\n"
	"	}\n"
	"	Color += texture(Bloom, UV).rgb * Params.z;\n"
	"	Color = pow(clamp(Color, 0.0, 1.0), vec3(Params.x));\n"
	// Film grain, strongest in the midtones like on film. It also breaks up banding.
	"	float Luma = dot(Color, vec3(0.299, 0.587, 0.114));\n"
	"	Color += (Hash(uvec3(uvec2(gl_FragCoord.xy), Seed)) - 0.5) * 0.024 * (0.3 + 2.8 * Luma * (1.0 - Luma));\n"
	"	FragColor = vec4(Color, 1.0);\n"
	"}\n";

UBOOL UOpenGLRenderDevice::InitPostProcess(GLuint VertexShader)
{
	CopyProgram			= CompileProgram(VertexShader,CopyFragmentShader);
	OcclusionProgram	= CompileProgram(VertexShader,OcclusionFragmentShader);
	BloomDownProgram	= CompileProgram(VertexShader,BloomDownFragmentShader);
	BloomUpProgram		= CompileProgram(VertexShader,BloomUpFragmentShader);
	CompositeProgram	= CompileProgram(VertexShader,CompositeFragmentShader);

	GLuint Programs[] = { CopyProgram, OcclusionProgram, BloomDownProgram, BloomUpProgram, CompositeProgram };
	for( INT i=0; i<ARRAY_COUNT(Programs); i++ )
	{
		if( !Programs[i] )
			return 0;
		// The units DrawFullscreen binds, samplers a program lacks have no location and are skipped.
		glUseProgram(Programs[i]);
		glUniform1i(glGetUniformLocation(Programs[i],"Frame"),0);
		glUniform1i(glGetUniformLocation(Programs[i],"Occlusion"),1);
		glUniform1i(glGetUniformLocation(Programs[i],"Bloom"),2);
		glUniform1i(glGetUniformLocation(Programs[i],"Depth"),3);
		glUniform1i(glGetUniformLocation(Programs[i],"Reflection"),4);
	}
	return 1;
}

void UOpenGLRenderDevice::PostProcess(UBOOL ViewValid,const FMatrix& CameraToScreen,const FPlane& Light,FLOAT Gamma)
{
	FPlane	Proj(1.f / CameraToScreen.M[0][0],1.f / CameraToScreen.M[1][1],CameraToScreen.M[2][2],CameraToScreen.M[3][2]);
	UBOOL	DepthPasses = ViewValid && (AmbientOcclusion || ContactShadows),
			Reflections = ViewValid && ScreenSpaceReflections;

	if( DepthPasses )
	{
		CreateFramebuffer(Occlusion,(World.Width + 1) / 2,(World.Height + 1) / 2,0,GL_RG8,0);
		UseProgram(OcclusionProgram);
		glUniform4fv(glGetUniformLocation(OcclusionProgram,"Proj"),1,&Proj.X);
		glUniform4fv(glGetUniformLocation(OcclusionProgram,"Light"),1,&Light.X);
		glUniform2f(glGetUniformLocation(OcclusionProgram,"Strength"),AmbientOcclusion ? 1.f : 0.f,ContactShadows ? 0.6f : 0.f);
		DrawFullscreen(OcclusionProgram,Occlusion.Framebuffer,Occlusion.Width,Occlusion.Height,0,0,0,World.DepthTexture);
	}

	if( Bloom )
	{
		GLuint	Source = World.Texture;
		INT		SourceWidth = World.Width, SourceHeight = World.Height;
		UseProgram(BloomDownProgram);
		for( INT i=0; i<BLOOM_LEVELS; i++ )
		{
			FOpenGLFramebuffer& Level = BloomLevels[i];
			CreateFramebuffer(Level,Max(World.Width >> (i + 1),1),Max(World.Height >> (i + 1),1),0,GL_RGB10_A2,0);
			glUniform3f(glGetUniformLocation(BloomDownProgram,"Params"),1.f / SourceWidth,1.f / SourceHeight,i == 0 ? 1.f : 0.f);
			DrawFullscreen(BloomDownProgram,Level.Framebuffer,Level.Width,Level.Height,Source);
			Source			= Level.Texture;
			SourceWidth		= Level.Width;
			SourceHeight	= Level.Height;
		}
		UseProgram(BloomUpProgram);
		for( INT i=BLOOM_LEVELS - 1; i>0; i-- )
		{
			FOpenGLFramebuffer &Small = BloomLevels[i], &Large = BloomLevels[i - 1];
			glUniform3f(glGetUniformLocation(BloomUpProgram,"Params"),1.f / Small.Width,1.f / Small.Height,0.7f);
			DrawFullscreen(BloomUpProgram,Large.Framebuffer,Large.Width,Large.Height,Small.Texture,0,0,0,1);
		}
	}

	// Disabled inputs still get a valid texture, their weight is zero.
	UseProgram(CompositeProgram);
	glUniform4fv(glGetUniformLocation(CompositeProgram,"Proj"),1,&Proj.X);
	glUniform4f(glGetUniformLocation(CompositeProgram,"Params"),1.f / Max(Gamma,0.1f),DepthPasses ? 1.f : 0.f,Bloom ? 1.f : 0.f,Reflections ? 1.f : 0.f);
	glUniform1ui(glGetUniformLocation(CompositeProgram,"Seed"),FrameCount++);
	DrawFullscreen(CompositeProgram,Scene.DrawFramebuffer(),Scene.Width,Scene.Height,World.Texture,
		DepthPasses ? Occlusion.Texture : World.Texture,Bloom ? BloomLevels[0].Texture : World.Texture,World.DepthTexture,0,World.ReflectionTexture);
}

// The light contact shadows follow: the sun if the level has one, otherwise the light strongest at
// the camera among those that reach its leaf and those that lit actors this frame.
FPlane FOpenGLRenderInterface::PickShadowLight()
{
	FVector Camera = ViewWorldToCamera.Inverse().TransformFVector(FVector(0,0,0));
	ULevel* Level = Viewport->Actor ? Viewport->Actor->XLevel : NULL;
	if( Level && Level->Model && Level->Model->Leaves.Num() )
	{
		UModel*			Model = Level->Model;
		FPointRegion	Region = Model->PointRegion(Viewport->Actor->Level,Camera);
		if( Region.iLeaf != INDEX_NONE && Model->Leaves(Region.iLeaf).iPermeating != INDEX_NONE )
			for( INT i=Model->Leaves(Region.iLeaf).iPermeating; Model->Lights(i); i++ )
				WorldLights.AddUniqueItem(Model->Lights(i)->GetLightRenderData());
	}

	FDynamicLight	*Best = NULL, *Kept = NULL;
	FLOAT			BestScore = 0.f, KeptScore = 0.f;
	for( INT i=0; i<WorldLights.Num(); i++ )
	{
		FDynamicLight* Light = WorldLights(i);
		if( !Light || !Light->Actor || Light->Actor->LightType == LT_None )
			continue;
		FLOAT Brightness = Light->Color.X * 0.299f + Light->Color.Y * 0.587f + Light->Color.Z * 0.114f,
			Score = Light->Actor->LightEffect == LE_Sunlight ? 1000.f + Brightness
				: Brightness * Square(Max(0.f,1.f - (Light->Position - Camera).SizeSquared() / Square(Light->Radius)));
		if( Light->Actor == RenDev->ShadowLight )
		{
			Kept = Light;
			KeptScore = Score;
		}
		if( Score > BestScore )
		{
			Best = Light;
			BestScore = Score;
		}
	}
	// The shadows only turn to a clearly stronger light, similar ones would make them flicker.
	if( Kept && KeptScore > 0.f && BestScore < KeptScore * 1.25f )
		Best = Kept;
	RenDev->ShadowLight = Best ? Best->Actor : NULL;
	if( !Best )
		return FPlane(0,0,0,0);
	if( Best->Actor->LightEffect == LE_Sunlight )
		return FPlane(ViewWorldToCamera.TransformNormal(-Best->Direction).SafeNormal(),0.f);
	return FPlane(ViewWorldToCamera.TransformFVector(Best->Position),Best->Radius);
}
