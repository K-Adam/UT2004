// Post processing of the world: screen space reflections, ambient occlusion and contact shadows from
// depth, global illumination, volumetric fog, depth of field, gamma and film grain. The HUD draws over
// the result unchanged.

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

static const ANSICHAR* OverlayFragmentShader =
	"#version 300 es\n"
	"precision mediump float;\n"
	"uniform sampler2D Frame;\n"
	"in vec2 UV;\n"
	"out vec4 FragColor;\n"
	"void main()\n"
	"{\n"
	"	FragColor = texture(Frame, UV);\n"
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
	"uniform float FrameIndex;\n"
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
	"	float Noise = fract(Jitter() + FrameIndex * 0.618034);\n"
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
	// Near field detail only. Far away, above all zoomed into foliage, the short march hits itself.
	"			Shadow = 1.0 - Strength.y * Hit * min(Lit * 2.0, 1.0) * (1.0 - smoothstep(2048.0, 4096.0, C.z));\n"
	"		}\n"
	"	}\n"
	"	FragColor = vec4(AO, Shadow, 0.0, 1.0);\n"
	"}\n";

// Light bounced once off the surfaces around each point, at half the world size. The lit frame already
// holds the light the baked lighting sends off every visible surface, so it is gathered from there with
// the haze taken out. Nothing tests what lies between a tap and the point, so light leaks through thin
// walls within the radius.
static const ANSICHAR* IndirectFragmentShader =
	POST_COMMON
	OGL_FOG_AMOUNT
	"uniform sampler2D Frame;\n"
	"uniform vec3 Up, FogColor;\n"
	"uniform vec4 Fog;\n"
	"uniform float FrameIndex;\n"
	"void main()\n"
	"{\n"
	"	ivec2 P = ivec2(gl_FragCoord.xy) * 2;\n"
	"	float D = DepthAt(P);\n"
	"	if (D < Split || D >= 1.0)\n"
	"	{\n"
	"		FragColor = vec4(0.0);\n"
	"		return;\n"
	"	}\n"
	"	vec3 C = PosAt(P);\n"
	"	vec3 N = NormalAt(P, C);\n"
	// A new offset each frame turns the sampling pattern into grain the history averages out.
	"	float Noise = fract(Jitter() + FrameIndex * 0.618034);\n"
	"	float Height = float(textureSize(Depth, 0).y);\n"
	// The disc taps undercount the hemisphere, the strength makes up for it.
	"	const float Radius = 320.0, Strength = 6.0;\n"
	"	float Pixels = min(Radius * 0.5 * Height / (Proj.y * C.z), Height * 0.25);\n"
	"	vec3 Sum = vec3(0.0);\n"
	"	for (int i = 0; i < 16; i++)\n"
	"	{\n"
	"		float A = (float(i) + Noise) / 16.0;\n"
	"		float Angle = 6.2831853 * (float(i) * 0.618034 + Noise);\n"
	"		ivec2 Q = ClampPixel(P + ivec2(vec2(cos(Angle), sin(Angle)) * A * Pixels));\n"
	"		float QD = DepthAt(Q);\n"
	"		if (QD < Split || QD >= 1.0)\n"
	"			continue;\n"
	"		vec3 V = PosAt(Q) - C;\n"
	"		float VV = dot(V, V);\n"
	"		float W = max(dot(V, N) * inversesqrt(VV + 0.01) - 0.1, 0.0) * max(1.0 - VV / (Radius * Radius), 0.0);\n"
	"		vec3 L = texelFetch(Frame, Q, 0).rgb;\n"
	"		Sum += (L - min(FogColor * FogAt(C + V, Up, Fog), L)) * W;\n"
	"	}\n"
	"	FragColor = vec4(Sum * Strength / 16.0, 1.0);\n"
	"}\n";

// Params holds the inverse gamma, the occlusion switch, the global illumination switch and in W the
// reflection switch. Focus holds the circle of confusion in world pixels of a sharp point at infinity,
// the focus distance and the largest circle, zero X turns depth of field off. The film grain is
// strongest in the midtones like on film, it also breaks up banding.
#define POST_FINISH \
	"uniform vec4 Params;\n" \
	"uniform vec3 Focus;\n" \
	"uniform uint Seed;\n" \
	"float Hash(uvec3 V)\n" \
	"{\n" \
	"	uint H = V.x * 1973u + V.y * 9277u + V.z * 26699u;\n" \
	"	H = (H ^ (H >> 16)) * 0x7feb352du;\n" \
	"	H = (H ^ (H >> 15)) * 0x846ca68bu;\n" \
	"	return float(H ^ (H >> 16)) / 4294967295.0;\n" \
	"}\n" \
	"vec3 Finish(vec3 Color)\n" \
	"{\n" \
	"	Color = pow(clamp(Color, 0.0, 1.0), vec3(Params.x));\n" \
	"	float Luma = dot(Color, vec3(0.299, 0.587, 0.114));\n" \
	"	return Color + (Hash(uvec3(uvec2(gl_FragCoord.xy), Seed)) - 0.5) * 0.024 * (0.3 + 2.8 * Luma * (1.0 - Luma));\n" \
	"}\n"

// Where the ray finds the reflected surface on screen it replaces the env map, fading out toward the
// screen edges and the ray's end so misses blend back into it. Decals such as puddles leave the
// depth of the ground beneath them, so nearly level normals are taken as level, Up is world up in
// camera space. The hit is spread by how far the reflection travelled, which softens distant
// reflections and dithers their stair steps. The half size occlusion, bounced light and fog
// scattering are gathered 4x4 with depth weights, which also averages out their noise. The sky gathers
// only sky, the fog in front of a branch is thin and would darken the sky around it. With depth
// of field the lit world goes to Focus with the signed circle of confusion of a thin lens, which
// grows with depth. The frame's alpha holds how hot each pixel looks. Heat rises, so the air shimmers
// at and above it, less far away where the waves would be larger than what they bend. Haze holds the
// strength and the time. Blasts holds the camera space centres of explosions with their age from 0 to
// 1 in W, then their final radius in X. Each bends the view along the rim of its growing sphere,
// less far away where a ring that only shrinks with distance would still stand out against its small explosion.
// Motion is the part of the frame the shutter stays open, zero turns the blur off. Each pixel is
// smeared along the way it moved on screen since last frame, which follows the camera only. The
// length is limited so a teleport does not streak the whole view.
static const ANSICHAR* CompositeFragmentShader =
	POST_COMMON
	POST_FINISH
	OGL_FOG_AMOUNT
	"uniform sampler2D Frame, Occlusion, Reflection, Scattering, Indirect, Albedo;\n"
	"uniform float Volumetric;\n"
	"uniform vec3 Up, FogColor;\n"
	"uniform vec4 Fog;\n"
	"uniform vec2 Haze;\n"
	"uniform vec4 Blasts[2 * " STRINGIFY(HEAT_BLASTS) "];\n"
	"uniform mat4 Reproject;\n"
	"uniform vec4 PrevProj;\n"
	"uniform float Motion;\n"
	"void main()\n"
	"{\n"
	"	ivec2 P = ivec2(UV * vec2(textureSize(Depth, 0)));\n"
	"	float D = DepthAt(P);\n"
	"	vec2 Bent = UV;\n"
	// The weapon stays sharp, its own flash would smear it.
	"	if (Haze.x > 0.0 && D >= Split)\n"
	"	{\n"
	"		vec2 Size = vec2(textureSize(Depth, 0));\n"
	"		float Heat = 0.0;\n"
	"		for (int i = 0; i < 4; i++)\n"
	"			Heat = max(Heat, texture(Frame, UV - vec2(0.0, 0.012 * float(i))).a * (1.0 - 0.25 * float(i)));\n"
	"		vec2 Q = UV * Size / Size.y;\n"
	"		vec2 Wave = vec2(sin(Q.y * 90.0 - Haze.y * 9.0 + 2.0 * sin(Q.x * 40.0 + Haze.y * 3.0)),\n"
	"			sin(Q.x * 70.0 + Haze.y * 7.0 + 2.0 * sin(Q.y * 50.0 - Haze.y * 4.0)));\n"
	// Slanted waves at unrelated rates keep the first pair from showing as a grid.
	"		Wave += 0.6 * vec2(sin(dot(Q, vec2(57.0, 131.0)) + Haze.y * 13.0), sin(dot(Q, vec2(-143.0, 61.0)) - Haze.y * 11.0));\n"
	"		float Z = D < 1.0 ? LinearZ(D) : 65536.0;\n"
	"		Bent += Wave * vec2(Size.y / Size.x, 1.0) * Heat * Haze.x * 0.0025 * clamp(800.0 / Z, 0.3, 1.0);\n"
	"		vec3 Ray = vec3((UV * 2.0 - 1.0) * Proj.xy, 1.0);\n"
	"		float Reach = Z * length(Ray);\n"
	"		Ray = normalize(Ray);\n"
	"		for (int i = 0; i < " STRINGIFY(HEAT_BLASTS) "; i++)\n"
	"		{\n"
	"			vec4 B = Blasts[i];\n"
	"			float Along = dot(B.xyz, Ray), R = Blasts[i + " STRINGIFY(HEAT_BLASTS) "].x * B.w;\n"
	// Not behind the camera and not behind what the pixel shows.
	"			if (B.w <= 0.0 || B.z < 1.0 || Along < 1.0 || Along - R > Reach)\n"
	"				continue;\n"
	"			float Rim = max(1.0 - abs(length(B.xyz - Ray * Along) - R) / (0.15 * R + 16.0), 0.0);\n"
	"			Bent -= normalize(UV - ToUV(B.xyz) + 1e-6) * Rim * Rim * (1.0 - B.w) * min(0.027 * Blasts[i + " STRINGIFY(HEAT_BLASTS) "].x / (Along * Proj.y), 0.03) * min(1.0, 1000.0 / Along) * Haze.x;\n"
	"		}\n"
	"	}\n"
	"	vec3 Color = texture(Frame, Bent).rgb;\n"
	"	if (Motion > 0.0 && D >= Split)\n"
	"	{\n"
	"		vec2 Size = vec2(textureSize(Depth, 0)), NDC = UV * 2.0 - 1.0;\n"
	"		float Z = LinearZ(D);\n"
	// The sky has no distance, only a direction.
	"		vec4 Prev = Reproject * (D < 1.0 ? vec4(NDC * Proj.xy * Z, Z, 1.0) : vec4(NDC * Proj.xy, 1.0, 0.0));\n"
	"		if (Prev.z > 0.0)\n"
	"		{\n"
	"			vec2 Move = (UV - (Prev.xy / (Prev.z * PrevProj.xy) * 0.5 + 0.5)) * Motion;\n"
	"			Move *= min(1.0, 0.015 / max(length(Move * vec2(Size.x / Size.y, 1.0)), 1e-6));\n"
	"			float Sum = 1.0;\n"
	"			for (int i = 0; i < 6; i++)\n"
	"			{\n"
	"				vec2 T = Bent + Move * ((float(i) + Jitter()) / 6.0 - 0.5);\n"
	// The weapon does not move with the world and would smear into it.
	"				if (DepthAt(ivec2(T * Size)) >= Split)\n"
	"				{\n"
	"					Color += texture(Frame, T).rgb;\n"
	"					Sum += 1.0;\n"
	"				}\n"
	"			}\n"
	"			Color /= Sum;\n"
	"		}\n"
	"	}\n"
	"	vec4 Refl = texelFetch(Reflection, P, 0);\n"
	"	if (Params.w > 0.0 && D >= Split && D < 1.0 && Refl.a > 0.004)\n"
	"	{\n"
	"		vec2 Size = vec2(textureSize(Depth, 0));\n"
	"		vec3 C = PosAt(P);\n"
	"		vec3 N = NormalAt(P, C);\n"
	"		N = normalize(mix(N, Up, smoothstep(0.8, 0.95, dot(N, Up))));\n"
	"		vec3 R = reflect(normalize(C), N);\n"
	"		float Step = max(8.0, 0.02 * C.z);\n"
	"		vec3 S = C + N * (0.5 + 0.002 * C.z) + R * Step * Jitter();\n"
	"		float W = 0.0, Spread = 0.0;\n"
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
	"				Spread = min(2.0 * length(B - C) / B.z, 3.0);\n"
	"				vec2 Edge = smoothstep(0.0, 0.1, min(HitUV, 1.0 - HitUV));\n"
	"				W = Edge.x * Edge.y * (1.0 - float(i) / 24.0);\n"
	"				break;\n"
	"			}\n"
	"			Step *= 1.25;\n"
	"		}\n"
	"		if (W > 0.0)\n"
	"		{\n"
	"			float A = 6.283 * Jitter();\n"
	"			vec2 O = vec2(cos(A), sin(A)) * Spread / Size, O2 = vec2(-O.y, O.x);\n"
	"			vec3 Hit = (texture(Frame, HitUV + O).rgb + texture(Frame, HitUV - O).rgb\n"
	"				+ texture(Frame, HitUV + O2).rgb + texture(Frame, HitUV - O2).rgb) * 0.25;\n"
	"			Color = max(Color + W * (Hit * Refl.a - Refl.rgb), 0.0);\n"
	"		}\n"
	"	}\n"
	"	if (Params.y > 0.0 || Params.z > 0.0 || Volumetric > 0.0)\n"
	"	{\n"
	"		bool Weapon = D < Split, Sky = D >= 1.0;\n"
	"		float Z = LinearZ(D);\n"
	"		ivec2 Size = (textureSize(Depth, 0) + 1) / 2;\n"
	"		vec2 Sum = vec2(0.0);\n"
	"		vec3 Scatter = vec3(0.0), Bounce = vec3(0.0);\n"
	"		float Weights = 0.0;\n"
	"		for (int Y = -1; Y <= 2; Y++)\n"
	"			for (int X = -1; X <= 2; X++)\n"
	"			{\n"
	"				ivec2 Q = clamp(P / 2 + ivec2(X, Y), ivec2(0), Size - 1);\n"
	"				float QD = DepthAt(Q * 2);\n"
	"				if ((QD < Split) != Weapon || (QD >= 1.0) != Sky)\n"
	"					continue;\n"
	"				float W = Sky ? 1.0 : max(1.0 - abs(LinearZ(QD) - Z) / (0.05 * Z + 1.0), 0.0) + 0.001;\n"
	"				Sum += texelFetch(Occlusion, Q, 0).rg * W;\n"
	"				Scatter += texelFetch(Scattering, Q, 0).rgb * W;\n"
	"				Bounce += texelFetch(Indirect, Q, 0).rgb * W;\n"
	"				Weights += W;\n"
	"			}\n"
	"		if (!Sky && (Params.y > 0.0 || Params.z > 0.0))\n"
	"		{\n"
	"			vec2 O = Params.y > 0.0 && Weights > 0.0 ? Sum / Weights : vec2(1.0);\n"
	// Occlusion darkens and bounced light brightens the surface under the fog, not the fog in front of it.
	"			vec3 Haze = min(FogColor * FogAt(PosAt(P), Up, Fog), Color);\n"
	"			vec3 Surface = Color - Haze;\n"
	// Bright surfaces lose less light to occlusion, they bounce more of it back.
	"			vec3 A = 2.0404 * Surface - 0.3324, B = -4.7951 * Surface + 0.6417, K = 2.7552 * Surface + 0.6903;\n"
	"			Surface *= max(vec3(O.x), ((O.x * A + B) * O.x + K) * O.x) * O.y;\n"
	// The albedo is already faded by the fog like the surface.
	"			if (Params.z > 0.0 && Weights > 0.0)\n"
	"				Surface += texelFetch(Albedo, P, 0).rgb * Bounce / Weights * O.x;\n"
	"			Color = Haze + Surface;\n"
	"		}\n"
	"		if (Weights > 0.0)\n"
	"			Color += Scatter / Weights * Volumetric;\n"
	"		else if (Sky)\n"
	"			Color += texture(Scattering, UV).rgb * Volumetric;\n"
	"	}\n"
	"	if (Focus.x > 0.0)\n"
	"	{\n"
	"		float CoC = D < Split ? 0.0 : D >= 1.0 ? Focus.x : Focus.x * (1.0 - Focus.y / LinearZ(D));\n"
	"		FragColor = vec4(Color, clamp(CoC / Focus.z, -1.0, 1.0) * 0.5 + 0.5);\n"
	"	}\n"
	"	else\n"
	"		FragColor = vec4(Finish(Color), 1.0);\n"
	"}\n";

// Single pass scatter as gather on a golden angle spiral. Each tap counts where its own circle reaches
// the centre. Taps behind the centre are limited to twice its circle, so blurred backgrounds stay out
// of sharp foregrounds while blurred foregrounds spread over sharp backgrounds. The weapon is
// nearest of all and stays sharp.
static const ANSICHAR* DepthOfFieldFragmentShader =
	POST_COMMON
	POST_FINISH
	"uniform sampler2D Frame;\n"
	"void main()\n"
	"{\n"
	"	vec2 Texel = 1.0 / vec2(textureSize(Frame, 0));\n"
	"	vec4 Centre = texture(Frame, UV);\n"
	"	if (DepthAt(ivec2(UV * vec2(textureSize(Depth, 0)))) < Split)\n"
	"	{\n"
	"		FragColor = vec4(Finish(Centre.rgb), 1.0);\n"
	"		return;\n"
	"	}\n"
	"	float CentreCoC = Centre.a * 2.0 - 1.0;\n"
	"	vec3 Sum = Centre.rgb;\n"
	"	float Count = 1.0, Radius = 1.0;\n"
	"	for (float Angle = 0.0; Radius < Focus.z; Angle += 2.3999632)\n"
	"	{\n"
	"		vec4 Tap = textureLod(Frame, UV + vec2(cos(Angle), sin(Angle)) * Texel * Radius, 0.0);\n"
	"		float CoC = Tap.a * 2.0 - 1.0;\n"
	"		float Size = abs(CoC) * Focus.z;\n"
	"		if (CoC > CentreCoC)\n"
	"			Size = min(Size, abs(CentreCoC) * Focus.z * 2.0);\n"
	"		Sum += mix(Sum / Count, Tap.rgb, smoothstep(Radius - 0.5, Radius + 0.5, Size));\n"
	"		Count += 1.0;\n"
	"		Radius += 1.0 / Radius;\n"
	"	}\n"
	"	FragColor = vec4(Finish(Sum / Count), 1.0);\n"
	"}\n";

// The light the fog scatters toward the camera, at half the world size. A light is sampled where the
// view ray passes through its radius and the sun over the first stretch, both in their traced shadows
// and dimmed by the distance fog in front. Forward scattering makes the haze brightest toward the sun,
// which also tints the distance fog the way the sky glows around it.
static const ANSICHAR* VolumetricFragmentShader =
	POST_COMMON
	"uniform sampler2D Atlas, SunMap;\n"
	"uniform int NumLights;\n"
	"uniform float FrameIndex;\n"
	"uniform mat4 View;\n"
	"uniform vec4 LightPosition[" STRINGIFY(FOG_LIGHTS) "], LightColor[" STRINGIFY(FOG_LIGHTS) "], LightSpot[" STRINGIFY(FOG_LIGHTS) "];\n"
	"uniform vec4 Sun, SunColor, SunU, SunV, SunAxis, Fog;\n"
	"const float Tile = float(" STRINGIFY(FOG_TILE) "), Tiles = float(" STRINGIFY(FOG_ATLAS_TILES) ");\n"
	"const int SunMask = " STRINGIFY(FOG_SUN_SIZE) " - 1;\n"
	OGL_FOG_AMOUNT
	"float Fogged(float Dist)\n"
	"{\n"
	"	return Fog.z < 0.5 ? 0.0 : FogAmount(clamp((Dist - Fog.x) / (Fog.y - Fog.x), 0.0, 1.0), 1.0);\n"
	"}\n"
	"vec2 Oct(vec3 D)\n"
	"{\n"
	"	D /= abs(D.x) + abs(D.y) + abs(D.z);\n"
	"	vec2 S = vec2(D.x >= 0.0 ? 1.0 : -1.0, D.y >= 0.0 ? 1.0 : -1.0);\n"
	"	return (D.z >= 0.0 ? D.xy : (1.0 - abs(D.yx)) * S) * 0.5 + 0.5;\n"
	"}\n"
	// The map holds where the shadow begins along the sun's axis. Over a smooth surface that changes
	// evenly and is interpolated, at the edge of an occluder each texel is compared first, blending
	// far apart boundaries would put the edge anywhere between them.
	"float SunLit(vec3 P)\n"
	"{\n"
	"	vec2 T = vec2(dot(P, SunU.xyz) + SunU.w, dot(P, SunV.xyz) + SunV.w) - 0.5;\n"
	"	float S = dot(P, SunAxis.xyz) + SunAxis.w;\n"
	"	ivec2 I = ivec2(floor(T));\n"
	"	vec2 F = T - vec2(I);\n"
	"	vec4 B = vec4(texelFetch(SunMap, I & SunMask, 0).r, texelFetch(SunMap, (I + ivec2(1, 0)) & SunMask, 0).r,\n"
	"		texelFetch(SunMap, (I + ivec2(0, 1)) & SunMask, 0).r, texelFetch(SunMap, (I + 1) & SunMask, 0).r);\n"
	"	float Lo = min(min(B.x, B.y), min(B.z, B.w)), Hi = max(max(B.x, B.y), max(B.z, B.w));\n"
	"	if (Hi - Lo < 1024.0)\n"
	"		return clamp((S - mix(mix(B.x, B.y, F.x), mix(B.z, B.w, F.x), F.y) + 32.0) / 32.0, 0.0, 1.0);\n"
	"	vec4 L = clamp((S - B + 32.0) / 32.0, 0.0, 1.0);\n"
	"	return mix(mix(L.x, L.y, F.x), mix(L.z, L.w, F.x), F.y);\n"
	"}\n"
	"void main()\n"
	"{\n"
	"	ivec2 P = ivec2(gl_FragCoord.xy) * 2;\n"
	"	float D = DepthAt(P);\n"
	"	if (D < Split)\n"
	"	{\n"
	"		FragColor = vec4(0.0);\n"
	"		return;\n"
	"	}\n"
	"	vec2 NDC = (vec2(P) + 0.5) / vec2(textureSize(Depth, 0)) * 2.0 - 1.0;\n"
	"	vec3 V = normalize(vec3(NDC * Proj.xy, 1.0));\n"
	"	float T = D < 1.0 ? length(PosAt(P)) : Fog.z > 0.5 ? Fog.y : 16384.0;\n"
	// A new offset each frame turns the sampling pattern into grain.
	"	float Noise = fract(Jitter() + FrameIndex * 0.618034);\n"
	"	vec3 Sum = vec3(0.0);\n"
	"	for (int i = 0; i < NumLights; i++)\n"
	"	{\n"
	"		vec3 L = LightPosition[i].xyz;\n"
	"		float R = LightPosition[i].w, B = dot(L, V), H2 = dot(L, L) - B * B;\n"
	"		if (H2 >= R * R || dot(LightColor[i].rgb, LightColor[i].rgb) == 0.0)\n"
	"			continue;\n"
	"		float C = sqrt(R * R - H2), T0 = max(B - C, 0.0), T1 = min(B + C, T);\n"
	"		if (T1 <= T0)\n"
	"			continue;\n"
	"		float Step = (T1 - T0) / 6.0, Light = 0.0;\n"
	"		for (int j = 0; j < 6; j++)\n"
	"		{\n"
	"			float Dist = T0 + (float(j) + Noise) * Step;\n"
	"			vec3 Ray = V * Dist - L;\n"
	"			float Range = length(Ray), A = Range / R;\n"
	"			vec3 Dir = Ray / max(Range, 0.001);\n"
	// UT's falloff, gathered toward the source like the glow around a lamp.
	"			float W = (2.0 * A * A * A - 3.0 * A * A + 1.0) / (1.0 + 4.0 * A);\n"
	"			if (LightSpot[i].w >= 0.0)\n"
	"			{\n"
	"				float Cone = (dot(Dir, LightSpot[i].xyz) - LightSpot[i].w) / max(1.0 - LightSpot[i].w, 0.0001);\n"
	"				W *= Cone > 0.0 ? Cone * Cone : 0.0;\n"
	"			}\n"
	"			if (LightColor[i].w >= 0.0)\n"
	"			{\n"
	"				vec2 Corner = vec2(mod(LightColor[i].w, Tiles), floor(LightColor[i].w / Tiles)) * Tile;\n"
	"				vec2 UV = (Corner + clamp(Oct(Dir * mat3(View)) * Tile, 0.5, Tile - 0.5)) / (Tile * Tiles);\n"
	"				W *= clamp((textureLod(Atlas, UV, 0.0).r - A) * R / 16.0 + 0.5, 0.0, 1.0);\n"
	"			}\n"
	"			Light += W * (1.0 - Fogged(Dist));\n"
	"		}\n"
	"		Sum += LightColor[i].rgb * Light * Step * 2.0;\n"
	"	}\n"
	"	vec3 Tint = vec3(0.0);\n"
	"	if (Sun.w > 0.0)\n"
	"	{\n"
	// The march stays clear of the sun map's wrapped far edge and its filter neighbour.
	"		float Length = min(T, 3800.0), Step = Length / 24.0, Lit = 0.0;\n"
	"		for (int j = 0; j < 24; j++)\n"
	"		{\n"
	"			float Dist = (float(j) + Noise) * Step;\n"
	"			Lit += SunLit(V * Dist) * (1.0 - Fogged(Dist));\n"
	"		}\n"
	// Henyey-Greenstein, one where scattering is even, partly even so the shafts also show from the side.
	"		float G = 0.6, Cos = dot(V, Sun.xyz);\n"
	"		float Phase = 0.25 + 0.75 * (1.0 - G * G) / pow(1.0 + G * G - 2.0 * G * Cos, 1.5);\n"
	"		Sum += SunColor.rgb * Lit * Step * 0.6 * Phase;\n"
	"		if (Fog.z > 0.5)\n"
	"			Tint = SunColor.rgb / max(max(SunColor.r, max(SunColor.g, SunColor.b)), 1.0) * pow(max(Cos, 0.0), 8.0) * 0.25 * (D < 1.0 ? Fogged(T) : 1.0);\n"
	"	}\n"
	"	FragColor = vec4(Sum * Fog.w + Tint, 1.0);\n"
	"}\n";

// Averages the scattering over frames, its sampling changes every frame. The history is found where
// the point was last frame and limited to the range around it now, so nothing trails behind motion.
// The range comes from neighbours at the point's own depth, or what a swaying twig left behind would
// still fit the range of the sky beside it and outline the twig.
static const ANSICHAR* ResolveFragmentShader =
	POST_COMMON
	"uniform sampler2D Frame, History;\n"
	"uniform mat4 Reproject;\n"
	"uniform vec4 PrevProj;\n"
	"uniform float HistoryWeight;\n"
	"void main()\n"
	"{\n"
	"	ivec2 Q = ivec2(gl_FragCoord.xy), Size = textureSize(Frame, 0);\n"
	"	vec3 C = texelFetch(Frame, Q, 0).rgb, Lo = C, Hi = C;\n"
	"	ivec2 P = Q * 2;\n"
	"	float D = DepthAt(P), Z = LinearZ(D);\n"
	"	bool Sky = D >= 1.0;\n"
	"	for (int Y = -1; Y <= 1; Y++)\n"
	"		for (int X = -1; X <= 1; X++)\n"
	"		{\n"
	"			ivec2 N = clamp(Q + ivec2(X, Y), ivec2(0), Size - 1);\n"
	"			float ND = DepthAt(N * 2);\n"
	"			if ((ND >= 1.0) != Sky || (!Sky && abs(LinearZ(ND) - Z) > 0.05 * Z + 1.0))\n"
	"				continue;\n"
	"			vec3 S = texelFetch(Frame, N, 0).rgb;\n"
	"			Lo = min(Lo, S);\n"
	"			Hi = max(Hi, S);\n"
	"		}\n"
	"	vec2 NDC = (vec2(P) + 0.5) / vec2(textureSize(Depth, 0)) * 2.0 - 1.0;\n"
	"	vec4 Prev = Reproject * (Sky ? vec4(NDC * Proj.xy, 1.0, 0.0) : vec4(PosAt(P), 1.0));\n"
	"	vec2 UV0 = Prev.xy / (Prev.z * PrevProj.xy) * 0.5 + 0.5;\n"
	"	float W = Prev.z > 0.0 && all(greaterThan(UV0, vec2(0.0))) && all(lessThan(UV0, vec2(1.0))) ? HistoryWeight : 0.0;\n"
	"	FragColor = vec4(mix(C, clamp(texture(History, UV0).rgb, Lo, Hi), W), 1.0);\n"
	"}\n";

// The occlusion history is kept wherever the depth stored beside it in B still matches. Limiting it to the current neighbourhood would discard a thin shadow that only
// some of the frames hit. The weapon stays in place on the screen. A half size texel is centred half
// a pixel past the pixel it was traced for.
static const ANSICHAR* OcclusionResolveFragmentShader =
	POST_COMMON
	"uniform sampler2D Frame, History;\n"
	"uniform mat4 Reproject;\n"
	"uniform vec4 PrevProj;\n"
	"uniform float HistoryWeight;\n"
	"float Encode(float Z)\n"
	"{\n"
	"	return clamp(log2(max(Z, 1.0)) / 16.0, 0.0, 1.0);\n"
	"}\n"
	"void main()\n"
	"{\n"
	"	ivec2 Q = ivec2(gl_FragCoord.xy), P = Q * 2;\n"
	"	vec2 C = texelFetch(Frame, Q, 0).rg;\n"
	"	float D = DepthAt(P);\n"
	"	if (D >= 1.0)\n"
	"	{\n"
	"		FragColor = vec4(C, 1.0, 1.0);\n"
	"		return;\n"
	"	}\n"
	"	vec3 Pos = PosAt(P);\n"
	"	vec4 Prev = vec4(Pos, 1.0);\n"
	"	vec2 UV0 = ToUV(Pos);\n"
	"	if (D >= Split)\n"
	"	{\n"
	"		Prev = Reproject * Prev;\n"
	"		UV0 = Prev.xy / (Prev.z * PrevProj.xy) * 0.5 + 0.5;\n"
	"	}\n"
	"	UV0 = (UV0 * vec2(textureSize(Depth, 0)) + 0.5) / (2.0 * vec2(textureSize(History, 0)));\n"
	"	vec3 H = texture(History, UV0).rgb;\n"
	"	bool Inside = Prev.z > 0.0 && all(greaterThan(UV0, vec2(0.0))) && all(lessThan(UV0, vec2(1.0)));\n"
	"	float W = Inside ? HistoryWeight * (1.0 - smoothstep(0.008, 0.016, abs(H.b - Encode(Prev.z)))) : 0.0;\n"
	"	FragColor = vec4(mix(C, H.rg, W), Encode(Pos.z), 1.0);\n"
	"}\n";

UBOOL UOpenGLRenderDevice::InitPostProcess(GLuint VertexShader)
{
	CopyProgram			= CompileProgram(VertexShader,CopyFragmentShader);
	OcclusionProgram	= CompileProgram(VertexShader,OcclusionFragmentShader);
	CompositeProgram	= CompileProgram(VertexShader,CompositeFragmentShader);
	DepthOfFieldProgram	= CompileProgram(VertexShader,DepthOfFieldFragmentShader);
	VolumetricProgram	= CompileProgram(VertexShader,VolumetricFragmentShader);
	ResolveProgram		= CompileProgram(VertexShader,ResolveFragmentShader);
	OverlayProgram		= CompileProgram(VertexShader,OverlayFragmentShader);
	IndirectProgram		= CompileProgram(VertexShader,IndirectFragmentShader);
	OcclusionResolveProgram	= CompileProgram(VertexShader,OcclusionResolveFragmentShader);

	GLuint Programs[] = { CopyProgram, OcclusionProgram, OcclusionResolveProgram, CompositeProgram, DepthOfFieldProgram, VolumetricProgram, ResolveProgram, OverlayProgram, IndirectProgram };
	for( INT i=0; i<ARRAY_COUNT(Programs); i++ )
	{
		if( !Programs[i] )
			return 0;
		// The units DrawFullscreen binds, samplers a program lacks have no location and are skipped.
		glUseProgram(Programs[i]);
		glUniform1i(glGetUniformLocation(Programs[i],"Frame"),0);
		glUniform1i(glGetUniformLocation(Programs[i],"Occlusion"),1);
		glUniform1i(glGetUniformLocation(Programs[i],"Depth"),2);
		glUniform1i(glGetUniformLocation(Programs[i],"Reflection"),3);
		glUniform1i(glGetUniformLocation(Programs[i],"Scattering"),4);
		glUniform1i(glGetUniformLocation(Programs[i],"Indirect"),5);
		glUniform1i(glGetUniformLocation(Programs[i],"Albedo"),6);
		glUniform1i(glGetUniformLocation(Programs[i],"SunMap"),1);
		glUniform1i(glGetUniformLocation(Programs[i],"History"),1);
	}
	return 1;
}

void UOpenGLRenderDevice::PostProcess(UBOOL ViewValid,const FMatrix& CameraToScreen,const FPlane& Light,FLOAT Gamma,FLOAT FocusDistance,const FOpenGLFogLights* Fog,const FPlane* Blasts)
{
	FPlane	Proj(1.f / CameraToScreen.M[0][0],1.f / CameraToScreen.M[1][1],CameraToScreen.M[2][2],CameraToScreen.M[3][2]);
	UBOOL	DepthPasses = ViewValid && (AmbientOcclusion || ContactShadows),
			Reflections = ViewValid && ScreenSpaceReflections,
			Bounce = ViewValid && GlobalIllumination;
	FMatrix	WorldToCamera = RenderInterface.ViewWorldToCamera,
			Reproject = WorldToCamera.Inverse() * PrevWorldToCamera;
	// Ten bits would clip bright glows, and the history would leave dim haze stuck a few steps off.
	GLenum	HalfFormat = SupportsFloatTargets ? GL_R11F_G11F_B10F : GL_RGB10_A2;
	FVector	Up = WorldToCamera.TransformNormal(FVector(0,0,1)).SafeNormal();
	FPlane	ViewFog = RenderInterface.ViewFog;
	ViewFog.W = VolumetricFog ? 1.f : 0.f;

	GLuint OcclusionTexture = World.Texture;
	if( DepthPasses )
	{
		CreateFramebuffer(Occlusion,(World.Width + 1) / 2,(World.Height + 1) / 2,0,GL_RG8,0);
		UseProgram(OcclusionProgram);
		glUniform4fv(glGetUniformLocation(OcclusionProgram,"Proj"),1,&Proj.X);
		glUniform4fv(glGetUniformLocation(OcclusionProgram,"Light"),1,&Light.X);
		glUniform2f(glGetUniformLocation(OcclusionProgram,"Strength"),AmbientOcclusion ? 1.f : 0.f,ContactShadows ? 0.6f : 0.f);
		glUniform1f(glGetUniformLocation(OcclusionProgram,"FrameIndex"),(FLOAT)(FrameCount & 1023));
		DrawFullscreen(OcclusionProgram,Occlusion.Framebuffer,Occlusion.Width,Occlusion.Height,0,0,World.DepthTexture);
		OcclusionTexture = Accumulate(OcclusionResolveProgram,Occlusion,OcclusionHistory,OcclusionHistoryValid,GL_RGBA8,Proj,Reproject);
	}
	else
		OcclusionHistoryValid = 0;

	GLuint IndirectTexture = World.Texture;
	if( Bounce )
	{
		CreateFramebuffer(Indirect,(World.Width + 1) / 2,(World.Height + 1) / 2,0,HalfFormat,0);
		UseProgram(IndirectProgram);
		glUniform4fv(glGetUniformLocation(IndirectProgram,"Proj"),1,&Proj.X);
		glUniform1f(glGetUniformLocation(IndirectProgram,"FrameIndex"),(FLOAT)(FrameCount & 1023));
		glUniform3fv(glGetUniformLocation(IndirectProgram,"Up"),1,&Up.X);
		glUniform4fv(glGetUniformLocation(IndirectProgram,"Fog"),1,&ViewFog.X);
		glUniform3fv(glGetUniformLocation(IndirectProgram,"FogColor"),1,&RenderInterface.ViewFogColor.X);
		DrawFullscreen(IndirectProgram,Indirect.Framebuffer,Indirect.Width,Indirect.Height,World.Texture,0,World.DepthTexture);
		IndirectTexture = Accumulate(ResolveProgram,Indirect,IndirectHistory,IndirectHistoryValid,HalfFormat,Proj,Reproject);
	}
	else
		IndirectHistoryValid = 0;

	if( Fog )
	{
		static const ANSICHAR* Names[] = { "Sun", "SunColor", "SunU", "SunV", "SunAxis", "Fog" };
		CreateFramebuffer(Scattering,(World.Width + 1) / 2,(World.Height + 1) / 2,0,HalfFormat,0);
		UseProgram(VolumetricProgram);
		glUniform4fv(glGetUniformLocation(VolumetricProgram,"Proj"),1,&Proj.X);
		glUniform1i(glGetUniformLocation(VolumetricProgram,"NumLights"),Fog->NumLights);
		glUniform1f(glGetUniformLocation(VolumetricProgram,"FrameIndex"),(FLOAT)(FrameCount & 1023));
		glUniformMatrix4fv(glGetUniformLocation(VolumetricProgram,"View"),1,GL_FALSE,&WorldToCamera.M[0][0]);
		glUniform4fv(glGetUniformLocation(VolumetricProgram,"LightPosition"),FOG_LIGHTS,&Fog->Position[0].X);
		glUniform4fv(glGetUniformLocation(VolumetricProgram,"LightColor"),FOG_LIGHTS,&Fog->Color[0].X);
		glUniform4fv(glGetUniformLocation(VolumetricProgram,"LightSpot"),FOG_LIGHTS,&Fog->Spot[0].X);
		for( INT i=0; i<ARRAY_COUNT(Names); i++ )
			glUniform4fv(glGetUniformLocation(VolumetricProgram,Names[i]),1,&(&Fog->Sun)[i].X);
		DrawFullscreen(VolumetricProgram,Scattering.Framebuffer,Scattering.Width,Scattering.Height,ShadowAtlas,SunShadow,World.DepthTexture);
	}
	else
		ScatterHistoryValid = 0;
	GLuint ScatterTexture = Fog ? Accumulate(ResolveProgram,Scattering,ScatterHistory,ScatterHistoryValid,HalfFormat,Proj,Reproject) : World.Texture;
	FrameViewValid = ViewValid;

	// A thin lens in front of a 24 mm high frame at f/1.4, which at this wide view blurs only gently.
	// The circle of confusion is limited to 1% of the height. A unit is 1.905 cm. Focusing closer than
	// twice the focal length would leave no image.
	UBOOL	Defocus = FocusDistance > 0.f;
	FLOAT	FocalLength = 12.f / Proj.Y,
			FocusMM = Max(FocusDistance * 19.05f,FocalLength * 2.f);
	FVector	Lens(Defocus ? Square(FocalLength) / 1.4f / (FocusMM - FocalLength) / 24.f * World.Height : 0.f,FocusDistance,0.01f * World.Height);
	FPlane	Params(1.f / Max(Gamma,0.1f),DepthPasses ? 1.f : 0.f,Bounce ? 1.f : 0.f,Reflections ? 1.f : 0.f);
	DWORD	Seed = FrameCount++;

	// Disabled inputs still get a valid texture, their weight is zero.
	UseProgram(CompositeProgram);
	glUniform4fv(glGetUniformLocation(CompositeProgram,"Proj"),1,&Proj.X);
	glUniform4fv(glGetUniformLocation(CompositeProgram,"Params"),1,&Params.X);
	glUniform3fv(glGetUniformLocation(CompositeProgram,"Focus"),1,&Lens.X);
	glUniform1ui(glGetUniformLocation(CompositeProgram,"Seed"),Seed);
	glUniform1f(glGetUniformLocation(CompositeProgram,"Volumetric"),Fog ? 1.f : 0.f);
	glUniform3fv(glGetUniformLocation(CompositeProgram,"Up"),1,&Up.X);
	glUniform4fv(glGetUniformLocation(CompositeProgram,"Fog"),1,&ViewFog.X);
	glUniform3fv(glGetUniformLocation(CompositeProgram,"FogColor"),1,&RenderInterface.ViewFogColor.X);
	// The time wraps so the waves keep their precision, a jump every quarter hour goes unseen.
	glUniform2f(glGetUniformLocation(CompositeProgram,"Haze"),ViewValid && HeatHaze ? 1.f : 0.f,(FLOAT)fmod(appSeconds(),1024.0));
	glUniform4fv(glGetUniformLocation(CompositeProgram,"Blasts"),2 * HEAT_BLASTS,&Blasts[0].X);
	glUniformMatrix4fv(glGetUniformLocation(CompositeProgram,"Reproject"),1,GL_FALSE,&Reproject.M[0][0]);
	glUniform4fv(glGetUniformLocation(CompositeProgram,"PrevProj"),1,&PrevProj.X);
	glUniform1f(glGetUniformLocation(CompositeProgram,"Motion"),MotionBlur && ViewValid && PrevViewValid ? 0.5f : 0.f);
	if( Defocus )
		CreateFramebuffer(Focus,World.Width,World.Height,0,GL_RGBA8,0);
	FOpenGLFramebuffer& Target = Defocus ? Focus : Scene;
	DrawFullscreen(CompositeProgram,Target.DrawFramebuffer(),Target.Width,Target.Height,World.Texture,
		OcclusionTexture,World.DepthTexture,0,World.ExtraTextures[EXTRA_Reflection],ScatterTexture,
		IndirectTexture,World.ExtraTextures[EXTRA_Albedo]);
	if( ViewValid )
	{
		PrevWorldToCamera	= WorldToCamera;
		PrevProj			= Proj;
	}

	if( Defocus )
	{
		UseProgram(DepthOfFieldProgram);
		glUniform4fv(glGetUniformLocation(DepthOfFieldProgram,"Params"),1,&Params.X);
		glUniform3fv(glGetUniformLocation(DepthOfFieldProgram,"Focus"),1,&Lens.X);
		glUniform1ui(glGetUniformLocation(DepthOfFieldProgram,"Seed"),Seed);
		DrawFullscreen(DepthOfFieldProgram,Scene.DrawFramebuffer(),Scene.Width,Scene.Height,Focus.Texture,0,World.DepthTexture);
	}
}

GLuint UOpenGLRenderDevice::Accumulate(GLuint Program,FOpenGLFramebuffer& Input,FOpenGLFramebuffer History[2],UBOOL& Valid,GLenum Format,const FPlane& Proj,const FMatrix& Reproject)
{
	FOpenGLFramebuffer &Resolved = History[FrameCount & 1], &Previous = History[~FrameCount & 1];
	for( INT i=0; i<2; i++ )
		if( History[i].Width != Input.Width || History[i].Height != Input.Height )
		{
			CreateFramebuffer(History[i],Input.Width,Input.Height,0,Format,0);
			Valid = 0;
		}
	UseProgram(Program);
	glUniform4fv(glGetUniformLocation(Program,"Proj"),1,&Proj.X);
	glUniform4fv(glGetUniformLocation(Program,"PrevProj"),1,&PrevProj.X);
	glUniformMatrix4fv(glGetUniformLocation(Program,"Reproject"),1,GL_FALSE,&Reproject.M[0][0]);
	glUniform1f(glGetUniformLocation(Program,"HistoryWeight"),Valid ? 0.9f : 0.f);
	DrawFullscreen(Program,Resolved.Framebuffer,Resolved.Width,Resolved.Height,Input.Texture,Previous.Texture,World.DepthTexture);
	Valid = 1;
	return Resolved.Texture;
}

// Minigun shells and the like only pop, and gibs and debris carry the word without any blast.
// Of the shock rifle only the combo blasts, its beam and ball hits are named explosions too.
static FLOAT BlastRadius(UClass* ActorClass)
{
	UBOOL Blast = 0;
	for( UClass* Class=ActorClass; Class && !Blast; Class=Class->GetSuperClass() )
		Blast = appStrstr(Class->GetName(),TEXT("Explosion")) || appStrstr(Class->GetName(),TEXT("VehDeath")) || appStrstr(Class->GetName(),TEXT("ShockCombo")) || appStrstr(Class->GetName(),TEXT("TankHitRock")) || appStrstr(Class->GetName(),TEXT("PRVCombo"));
	const TCHAR* Name = ActorClass->GetName();
	if( !Blast || appStrstr(Name,TEXT("Blood")) || appStrstr(Name,TEXT("Crap")) || (appStrstr(Name,TEXT("Shock")) && !appStrstr(Name,TEXT("ShockCombo"))) )
		return 0.f;
	if( appStrstr(Name,TEXT("Small")) )
		return 50.f;
	if( appStrstr(Name,TEXT("TankHitRock")) )
		return 1000.f;
	if( appStrstr(Name,TEXT("Redeemer")) )
		return 2500.f;
	return 300.f;
}

// Explosions are told by their class names, several effects of one explosion make one wave.
void FOpenGLRenderInterface::PickBlasts(FPlane* Out)
{
	const FLOAT	Duration = 0.45f;
	ULevel*		Level = Viewport->Actor ? Viewport->Actor->XLevel : NULL;
	if( !Level )
		return;
	FLOAT Now = Viewport->Actor->Level->TimeSeconds;
	UBOOL Seen[ARRAY_COUNT(Blasts)];
	appMemzero(Seen,sizeof(Seen));
	// Another level counts its time from zero again.
	for( INT j=0; j<ARRAY_COUNT(Blasts); j++ )
		if( Blasts[j].Start > Now )
			appMemzero(&Blasts[j],sizeof(FBlast));

	for( INT i=Level->iFirstDynamicActor; i<Level->Actors.Num(); i++ )
	{
		AActor* Actor = Level->Actors(i);
		if( !Actor || Actor->bDeleteMe || Actor->bNoDelete )
			continue;
		FName	ClassName = Actor->GetClass()->GetFName();
		FLOAT*	Known = RenDev->BlastRadii.Find(ClassName);
		FLOAT	Radius = Known ? *Known : RenDev->BlastRadii.Set(ClassName,BlastRadius(Actor->GetClass()));
		if( !Radius )
			continue;

		INT Slot = INDEX_NONE, Oldest = 0;
		for( INT j=0; j<ARRAY_COUNT(Blasts); j++ )
		{
			FBlast& B = Blasts[j];
			if( B.Actor == Actor )
				Slot = j;
			// A slot whose actor lives must stay, or the actor would start another wave.
			if( (B.Actor ? B.Start : B.Start - 1e6f) < (Blasts[Oldest].Actor ? Blasts[Oldest].Start : Blasts[Oldest].Start - 1e6f) )
				Oldest = j;
			if( B.Radius && Now - B.Start < 0.1f && (B.Location - Actor->Location).SizeSquared() < Square(128.f) )
				Radius = 0.f;
		}
		if( Slot == INDEX_NONE )
		{
			Slot = Oldest;
			Blasts[Slot].Actor		= Actor;
			Blasts[Slot].Location	= Actor->Location;
			Blasts[Slot].Radius		= Radius;
			Blasts[Slot].Start		= Now;
		}
		Seen[Slot] = 1;
	}

	INT Count = 0;
	for( INT j=0; j<ARRAY_COUNT(Blasts); j++ )
	{
		FBlast& B = Blasts[j];
		if( !Seen[j] )
			B.Actor = NULL;
		if( B.Radius && Now - B.Start < Duration && Count < HEAT_BLASTS )
		{
			Out[Count]					= FPlane(ViewWorldToCamera.TransformFVector(B.Location),Max((Now - B.Start) / Duration,0.001f));
			Out[HEAT_BLASTS + Count++]	= FPlane(B.Radius,0,0,0);
		}
	}
}

// The focus follows what the crosshair rests on, eased in diopters like an eye or a lens refocusing.
FLOAT FOpenGLRenderInterface::PickFocus()
{
	const FLOAT			Range = 16384.f;
	FMatrix				CameraToWorld = ViewWorldToCamera.Inverse();
	FVector				Camera = CameraToWorld.TransformFVector(FVector(0,0,0));
	APlayerController*	Player = Viewport->Actor;
	FCheckResult		Hit;
	FLOAT				Distance = Range;
	if( Player && Player->XLevel && !Player->XLevel->SingleLineCheck(Hit,Player->ViewTarget,CameraToWorld.TransformFVector(FVector(0,0,Range)),Camera,TRACE_World | TRACE_Pawns) )
		Distance = Max(Hit.Time * Range,16.f);
	DOUBLE	Now = appSeconds();
	FLOAT	Target = 1.f / Distance;
	FocusDiopters = Now - FocusTime > 1.0 ? Target : FocusDiopters + (Target - FocusDiopters) * (1.f - appExp((FocusTime - Now) / 0.15));
	FocusTime = Now;
	return 1.f / FocusDiopters;
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
