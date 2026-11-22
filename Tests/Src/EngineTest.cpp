#include "TestEngine.h"

#if __STATIC_LINK
#include "Editor.h"
#endif

static const TCHAR* DeathmatchURL = TEXT("DM-Rankin?game=XGame.xDeathMatch?MinPlayers=4?QuickStart=true");
static const TCHAR* OnslaughtURL = TEXT("ONS-RedPlanet?game=Onslaught.ONSOnslaughtGame?MinPlayers=8?QuickStart=true");

// The test owns the clock, so game time passes as fast as the machine ticks.
void RunGame( FLOAT Seconds )
{
	const FLOAT Step = 1.f/30.f;
	for( FLOAT Time=0.f; Time<Seconds; Time+=Step )
	{
		GStats.Clear();
		GCurrentTime += Step;
		GDeltaTime = Step;
		GTestEngine->Tick( Step );
	}
}

static void Travel( const TCHAR* URL )
{
	ULevel* Old = GTestEngine->GLevel;
	GTestEngine->Exec( *FString::Printf( TEXT("servertravel %s"), URL ) );
	// The switch waits out the net driver's ServerTravelPause.
	RunGame( 6.f );
	ASSERT_NE( Old, GTestEngine->GLevel ) << "no map change to " << TCHAR_TO_ANSI(URL);
}

static INT CountObjects()
{
	INT Count = 0;
	for( FObjectIterator It; It; ++It )
		Count++;
	return Count;
}

static void ExpectMatchInProgress()
{
	AGameInfo* Game = GTestEngine->GLevel->GetLevelInfo()->Game;
	ASSERT_TRUE( Game && Game->GetStateFrame() && Game->GetStateFrame()->StateNode );
	EXPECT_STREQ( "MatchInProgress", TCHAR_TO_ANSI( Game->GetStateFrame()->StateNode->GetName() ) );
}

TEST( Engine, NativesBound )
{
	INT Count = 0;
	for( TObjectIterator<UFunction> It; It; ++It )
	{
		if( It->FunctionFlags & FUNC_Native )
		{
			Count++;
			EXPECT_TRUE( It->Func && It->Func!=&UObject::execUndefined ) << TCHAR_TO_ANSI( It->GetFullName() );
		}
	}
	EXPECT_GT( Count, 0 );
}

TEST( Engine, CubemapFaceMustBeATexture )
{
	UTexture* Plain = ConstructObject<UTexture>( UTexture::StaticClass() );
	UCubemap* Cube = ConstructObject<UCubemap>( UCubemap::StaticClass() );
	for( INT Face=0; Face<6; Face++ )
		Cube->Faces[Face] = Cube;
	Cube->Faces[1] = Plain;
	FCubemap* Interface = Cube->GetRenderInterface()->GetCubemapInterface();
	ASSERT_TRUE( Interface != NULL );
	EXPECT_EQ( NULL, Interface->GetFace(0) );
	EXPECT_EQ( Plain->GetRenderInterface()->GetTextureInterface(), Interface->GetFace(1) );
}

#if __STATIC_LINK
// Only the static build has every module's C++ classes in reach of one executable.
TEST( Engine, ClassSizes )
{
	#undef VERIFY_CLASS_SIZE_NODIE
	#define VERIFY_CLASS_SIZE_NODIE(ClassName) \
		EXPECT_TRUE( ClassName::StaticClass()->GetLinker()!=NULL ) << #ClassName " has no script class loaded"; \
		EXPECT_EQ( sizeof(ClassName), (size_t)ClassName::StaticClass()->GetPropertiesSize() ) << #ClassName;
	#define VERIFY_CLASS_SIZES
	#define NAMES_ONLY
	#define AUTOGENERATE_NAME(name)
	#define AUTOGENERATE_FUNCTION(cls,idx,name)
	#include "XGameClasses.h"
	#include "OnslaughtClasses.h"
	#include "XInterfaceClasses.h"
	#include "EditorClasses.h"
	#undef AUTOGENERATE_FUNCTION
	#undef AUTOGENERATE_NAME
	#undef NAMES_ONLY
	#undef VERIFY_CLASS_SIZES
}
#endif

TEST( Engine, DeathmatchRuns )
{
	RunGame( 60.f );
	ExpectMatchInProgress();

	INT Bots = 0, Alive = 0;
	ULevel* Level = GTestEngine->GLevel;
	for( INT i=0; i<Level->Actors.Num(); i++ )
	{
		AController* Controller = Cast<AController>( Level->Actors(i) );
		if( Controller && Controller->bIsPlayer )
		{
			Bots++;
			if( Controller->Pawn )
				Alive++;
		}
	}
	EXPECT_EQ( 4, Bots );
	EXPECT_GT( Alive, 0 );
}

TEST( Engine, OnslaughtRuns )
{
	Travel( OnslaughtURL );
	RunGame( 60.f );
	ExpectMatchInProgress();

	INT Vehicles = 0;
	ULevel* Level = GTestEngine->GLevel;
	for( INT i=0; i<Level->Actors.Num(); i++ )
	{
		AActor* Actor = Level->Actors(i);
		if( !Actor )
			continue;
		UBOOL IsVehicle = Actor->IsA( AVehicle::StaticClass() );
		Vehicles += IsVehicle;
		if( IsVehicle || Actor->Physics==PHYS_Karma || Actor->Physics==PHYS_KarmaRagDoll )
		{
			EXPECT_TRUE( std::isfinite(Actor->Location.X) && std::isfinite(Actor->Location.Y) && std::isfinite(Actor->Location.Z) )
				<< TCHAR_TO_ANSI( Actor->GetFullName() );
		}
	}
	EXPECT_GT( Vehicles, 0 );
}

TEST( Engine, MapChangesDoNotLeak )
{
	const INT Changes = 10;
	INT Objects[Changes], Pointers[Changes];
	for( INT i=0; i<Changes; i++ )
	{
		Travel( (i&1) ? OnslaughtURL : DeathmatchURL );
		Objects[i] = CountObjects();
		#if SERIAL_POINTER_INDEX
			Pointers[i] = GTotalSerializedPointers;
		#else
			Pointers[i] = 0;
		#endif
		printf( "map change %i: %i objects, %i serialized pointers\n", i+1, Objects[i], Pointers[i] );
	}
	// The maps alternate, so each count is compared with an earlier visit of the same map. The pointer table's high
	// mark moves by a few slots with what is alive at the switch, a leak adds hundreds per change.
	for( INT i=Changes-2; i<Changes; i++ )
	{
		EXPECT_LE( Objects[i], Objects[i-6] + Objects[i-6]/20 );
		EXPECT_LE( Pointers[i], Pointers[i-6] + 64 );
	}
}
