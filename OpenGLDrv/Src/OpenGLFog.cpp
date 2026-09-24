// The lights the volumetric fog scatters and their shadows. The maps bake lighting only on surfaces,
// so the shadows in the air are traced against the collision the lighting was built from, with the
// lighting builder's flags. They skip the skybox and follow bShadowCast like the baked shadows.

#include "OpenGLDrv.h"

#define SHADOW_TRACE (TRACE_ShadowCast | TRACE_Level | TRACE_Actors)
#define SUN_TEXEL 128.f

static const INT	NumTiles = FOG_ATLAS_TILES * FOG_ATLAS_TILES,
					NoCell = MAXINT;
static const FLOAT	Unlit = 65000.f;

// The direction an octahedral map texel stands for, the shader's Oct() inverted.
static FVector OctDirection(INT X,INT Y)
{
	FLOAT	U = (X + 0.5f) / FOG_TILE * 2.f - 1.f,
			V = (Y + 0.5f) / FOG_TILE * 2.f - 1.f;
	FVector	D(U,V,1.f - Abs(U) - Abs(V));
	if( D.Z < 0.f )
		D = FVector((1.f - Abs(V)) * (U >= 0.f ? 1.f : -1.f),(1.f - Abs(U)) * (V >= 0.f ? 1.f : -1.f),D.Z);
	return D.SafeNormal();
}

static void SunBasis(const FVector& Axis,FVector& U,FVector& V)
{
	U = (Axis ^ (Abs(Axis.Z) < 0.9f ? FVector(0,0,1) : FVector(1,0,0))).SafeNormal();
	V = Axis ^ U;
}

void UOpenGLRenderDevice::FlushShadows()
{
	appMemzero(ShadowTiles,sizeof(ShadowTiles));
	for( INT i=0; i<FOG_SUN_SIZE * FOG_SUN_SIZE; i++ )
	{
		SunCells[i][0] = SunCells[i][1] = NoCell;
		SunPending[i] = 0;
		SunTexels[i] = Unlit;
	}
	ShadowLevel = NULL;
	SunDirection = FVector(0,0,0);
	ScatterHistoryValid = IndirectHistoryValid = 0;
}

// Where the sun's shadow begins on the map line of cell I,J: points further toward the sun are lit.
// The line comes down from the sun's side until it leaves the solid around the sky, then the trace
// on toward the ground finds the first surface the sunlight meets. Starting below the terrain would
// see nothing, it does not block traces from beneath.
FLOAT FOpenGLRenderInterface::TraceSunCell(ULevel* Level,INT I,INT J,FLOAT CameraS)
{
	FVector	Axis = RenDev->SunDirection, U, V;
	SunBasis(Axis,U,V);
	FVector	Base = U * ((I + 0.5f) * SUN_TEXEL) + V * ((J + 0.5f) * SUN_TEXEL);
	for( FLOAT S=CameraS + 16384.f; S>CameraS - 16384.f; S-=1024.f )
	{
		FVector			P = Base + Axis * S;
		FCheckResult	Hit(1.f);
		if( !Level->Model->PointCheck(Hit,NULL,P,FVector(0,0,0),0) )
			continue;
		if( Level->SingleLineCheck(Hit,NULL,P - Axis * 32768.f,P,SHADOW_TRACE) )
			return -Unlit;
		return S - Hit.Time * 32768.f;
	}
	return Unlit;
}

// The selected lights' tiles get the first half of the budget, the sun cells nearest the camera the
// rest, so neither starves the other.
void FOpenGLRenderInterface::TraceShadows(ULevel* Level,const FVector& Camera)
{
	DOUBLE	Start = appSeconds(),
			Deadline = Start + 0.00075;
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D,RenDev->ShadowAtlas);
	RenDev->GL.Textures[0] = (GLuint)-1;
	FLOAT Row[FOG_TILE];
	for( INT i=0; i<FogLights.NumLights && appSeconds() < Deadline; i++ )
	{
		INT Tile = appRound(FogLights.Color[i].W);
		if( Tile < 0 )
			continue;
		UOpenGLRenderDevice::FShadowTile& T = RenDev->ShadowTiles[Tile];
		for( ; T.Rows < FOG_TILE && appSeconds() < Deadline; T.Rows++ )
		{
			for( INT X=0; X<FOG_TILE; X++ )
			{
				FVector			Dir = OctDirection(X,T.Rows);
				FCheckResult	Hit(1.f);
				Level->SingleLineCheck(Hit,NULL,T.Position + Dir * T.Radius,T.Position + Dir * 4.f,SHADOW_TRACE);
				Row[X] = (4.f + Hit.Time * (T.Radius - 4.f)) / T.Radius;
			}
			glTexSubImage2D(GL_TEXTURE_2D,0,Tile % FOG_ATLAS_TILES * FOG_TILE,Tile / FOG_ATLAS_TILES * FOG_TILE + T.Rows,FOG_TILE,1,GL_RED,GL_FLOAT,Row);
		}
	}

	if( FogLights.Sun.W == 0.f )
		return;
	Deadline = Start + 0.0015;
	FVector	U, V;
	SunBasis(RenDev->SunDirection,U,V);
	INT		CI = appFloor((Camera | U) / SUN_TEXEL),
			CJ = appFloor((Camera | V) / SUN_TEXEL);
	FLOAT	CameraS = Camera | RenDev->SunDirection;
	UBOOL	Changed = 0;
	// Rings outward from the camera, a cell that scrolled in is unlit until traced.
	for( INT R=0; R<=FOG_SUN_SIZE / 2; R++ )
		for( INT DJ=-R; DJ<=R; DJ++ )
			for( INT DI=-R; DI<=R; DI += (Abs(DJ) == R || R == 0) ? 1 : 2 * R )
			{
				if( DI == FOG_SUN_SIZE / 2 || DJ == FOG_SUN_SIZE / 2 )
					continue;
				INT	I = CI + DI, J = CJ + DJ,
					Slot = (J & (FOG_SUN_SIZE - 1)) * FOG_SUN_SIZE + (I & (FOG_SUN_SIZE - 1));
				if( RenDev->SunCells[Slot][0] != I || RenDev->SunCells[Slot][1] != J )
				{
					RenDev->SunCells[Slot][0] = I;
					RenDev->SunCells[Slot][1] = J;
					RenDev->SunTexels[Slot] = Unlit;
					RenDev->SunPending[Slot] = 1;
					Changed = 1;
				}
				if( RenDev->SunPending[Slot] && appSeconds() < Deadline )
				{
					RenDev->SunTexels[Slot] = TraceSunCell(Level,I,J,CameraS);
					RenDev->SunPending[Slot] = 0;
					Changed = 1;
				}
			}
	if( Changed )
	{
		glBindTexture(GL_TEXTURE_2D,RenDev->SunShadow);
		glTexSubImage2D(GL_TEXTURE_2D,0,0,0,FOG_SUN_SIZE,FOG_SUN_SIZE,GL_RED,GL_FLOAT,RenDev->SunTexels);
	}
}

// The sun and the FOG_LIGHTS lights that matter most to the view, among those in zones the camera can see.
// A light fades in once its shadows are traced and while it stays among them.
const FOpenGLFogLights* FOpenGLRenderInterface::PickFogLights()
{
	DOUBLE	Now = appSeconds();
	ULevel*	Level = Viewport->Actor ? Viewport->Actor->XLevel : NULL;
	FOpenGLFogLights& Out = FogLights;
	appMemzero(&Out,sizeof(Out));
	if( !Level )
		return &Out;

	if( !RenDev->ShadowAtlas )
	{
		GLuint Textures[2];
		glGenTextures(2,Textures);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D,Textures[0]);
		glTexStorage2D(GL_TEXTURE_2D,1,GL_R16F,FOG_ATLAS_TILES * FOG_TILE,FOG_ATLAS_TILES * FOG_TILE);
		// Unfinished tiles are sampled with no weight, they must not hold NaNs.
		TArray<FLOAT> Clear;
		Clear.Add(Square(FOG_ATLAS_TILES * FOG_TILE));
		for( INT i=0; i<Clear.Num(); i++ )
			Clear(i) = 1.f;
		glTexSubImage2D(GL_TEXTURE_2D,0,0,0,FOG_ATLAS_TILES * FOG_TILE,FOG_ATLAS_TILES * FOG_TILE,GL_RED,GL_FLOAT,&Clear(0));
		glBindTexture(GL_TEXTURE_2D,Textures[1]);
		// Positions along the sun's axis need full precision, the shader fetches texels unfiltered.
		glTexStorage2D(GL_TEXTURE_2D,1,GL_R32F,FOG_SUN_SIZE,FOG_SUN_SIZE);
		glTexSubImage2D(GL_TEXTURE_2D,0,0,0,FOG_SUN_SIZE,FOG_SUN_SIZE,GL_RED,GL_FLOAT,RenDev->SunTexels);
		RenDev->GL.Textures[0] = (GLuint)-1;
		RenDev->ShadowAtlas = Textures[0];
		RenDev->SunShadow = Textures[1];
	}
	if( RenDev->ShadowLevel != Level )
	{
		RenDev->FlushShadows();
		RenDev->ShadowLevel = Level;
	}

	FMatrix	CameraToWorld = ViewWorldToCamera.Inverse();
	FVector	Camera = CameraToWorld.TransformFVector(FVector(0,0,0));
	FLOAT	TanX = 1.f / ViewCameraToScreen.M[0][0],
			TanY = 1.f / ViewCameraToScreen.M[1][1];
	QWORD	Visible = 0;
	if( Level->Model && Level->Model->Leaves.Num() )
	{
		FPointRegion Region = Level->Model->PointRegion(Viewport->Actor->Level,Camera);
		if( Region.iLeaf != INDEX_NONE )
			Visible = Level->Model->Leaves(Region.iLeaf).VisibleZones;
	}

	FDynamicLight	*Best[FOG_LIGHTS], *Sun = NULL;
	FLOAT			Scores[FOG_LIGHTS], SunBrightness = 0.f;
	INT				Count = 0;
	for( INT i=0; i<Level->Actors.Num(); i++ )
	{
		AActor* Actor = Level->Actors(i);
		if( !Actor || Actor->bDeleteMe || Actor->LightType == LT_None || Actor->LightEffect == LE_Negative )
			continue;
		FDynamicLight*	Light = Actor->GetLightRenderData();
		FLOAT			Brightness = (Light->Color.X * 0.299f + Light->Color.Y * 0.587f + Light->Color.Z * 0.114f) * Light->Alpha;
		if( Brightness <= 0.f )
			continue;
		if( Actor->LightEffect == LE_Sunlight )
		{
			if( Brightness > SunBrightness )
			{
				SunBrightness = Brightness;
				Sun = Light;
			}
			continue;
		}
		INT Zone = Actor->Region.ZoneNumber;
		if( Visible && !(Visible & ((QWORD)1 << Zone)) )
			continue;
		FVector	P = ViewWorldToCamera.TransformFVector(Light->Position);
		FLOAT	R = Light->Radius;
		if( P.Z < -R || Abs(P.X) - P.Z * TanX > R * appSqrt(1.f + TanX * TanX) || Abs(P.Y) - P.Z * TanY > R * appSqrt(1.f + TanY * TanY) )
			continue;
		FLOAT Score = Brightness * R * R / (P.SizeSquared() + Square(0.25f * R));
		if( Count == FOG_LIGHTS && Score <= Scores[FOG_LIGHTS - 1] )
			continue;
		INT Slot = Count < FOG_LIGHTS ? Count++ : FOG_LIGHTS - 1;
		for( ; Slot > 0 && Scores[Slot - 1] < Score; Slot-- )
		{
			Best[Slot] = Best[Slot - 1];
			Scores[Slot] = Scores[Slot - 1];
		}
		Best[Slot] = Light;
		Scores[Slot] = Score;
	}

	if( Sun )
	{
		Out.Sun = FPlane(ViewWorldToCamera.TransformNormal(-Sun->Direction).SafeNormal(),1.f);
		Out.SunColor = FPlane(Sun->Color * Sun->Alpha);
		if( RenDev->SunDirection != -Sun->Direction.SafeNormal() )
		{
			RenDev->SunDirection = -Sun->Direction.SafeNormal();
			for( INT j=0; j<FOG_SUN_SIZE * FOG_SUN_SIZE; j++ )
				RenDev->SunCells[j][0] = NoCell;
		}
	}

	// Static lights get a tile, the one they had or the least recently used.
	FLOAT				Ease = FogTime > 0.0 ? 1.f - appExp((FogTime - Now) / 0.2) : 1.f;
	TMap<AActor*,FLOAT>	Fades;
	FogTime = Now;
	for( INT i=0; i<Count; i++ )
	{
		FDynamicLight*	Light = Best[i];
		INT				Tile = -1;
		UBOOL			Ready = 1;
		if( Light->Actor->bStatic )
		{
			INT Oldest = 0;
			for( Tile=0; Tile<NumTiles; Tile++ )
			{
				UOpenGLRenderDevice::FShadowTile& T = RenDev->ShadowTiles[Tile];
				if( T.Light == Light->Actor && T.Position == Light->Position && T.Radius == Light->Radius )
					break;
				if( T.Used < RenDev->ShadowTiles[Oldest].Used )
					Oldest = Tile;
			}
			if( Tile == NumTiles )
			{
				Tile = Oldest;
				UOpenGLRenderDevice::FShadowTile& T = RenDev->ShadowTiles[Tile];
				T.Light		= Light->Actor;
				T.Position	= Light->Position;
				T.Radius	= Light->Radius;
				T.Rows		= 0;
			}
			RenDev->ShadowTiles[Tile].Used = Now;
			Ready = RenDev->ShadowTiles[Tile].Rows == FOG_TILE;
		}
		FLOAT* Last = FogFades.Find(Light->Actor);
		FLOAT Fade = (Last ? *Last : 0.f) + ((Ready ? 1.f : 0.f) - (Last ? *Last : 0.f)) * Ease;
		Fades.Set(Light->Actor,Fade);

		BYTE Effect = Light->Actor->LightEffect;
		INT n = Out.NumLights++;
		Out.Position[n]	= FPlane(ViewWorldToCamera.TransformFVector(Light->Position),Light->Radius);
		Out.Color[n]	= FPlane(Light->Color * (Light->Alpha * Fade),(FLOAT)Tile);
		Out.Spot[n]		= FPlane(ViewWorldToCamera.TransformNormal(Light->Direction).SafeNormal(),
			Effect == LE_Spotlight || Effect == LE_StaticSpot ? 1.f - Light->Actor->LightCone / 256.f : -1.f);
	}
	FogFades = Fades;

	// A thin haze everywhere, denser in zones with fog.
	Out.Fog = FPlane(ViewFog.X,ViewFog.Y,ViewFog.Z,1.f / 40000.f + (ViewFog.Z > 0.f ? 1.f / Max(ViewFog.Y,1.f) : 0.f));
	if( Out.Sun.W > 0.f )
	{
		FVector U, V;
		SunBasis(RenDev->SunDirection,U,V);
		Out.SunU	= FPlane(ViewWorldToCamera.TransformNormal(U) / SUN_TEXEL,(Camera | U) / SUN_TEXEL);
		Out.SunV	= FPlane(ViewWorldToCamera.TransformNormal(V) / SUN_TEXEL,(Camera | V) / SUN_TEXEL);
		Out.SunAxis	= FPlane(ViewWorldToCamera.TransformNormal(RenDev->SunDirection),Camera | RenDev->SunDirection);
	}

	TraceShadows(Level,Camera);
	return &Out;
}
