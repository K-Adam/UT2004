// Before the engine headers, whose macros break the standard ones
#pragma pack(push,8)
#include <filesystem>
#pragma pack(pop)

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

TEST( Engine, CubemapFacesFitFaceZero )
{
	UCubemap* Cube = ConstructObject<UCubemap>( UCubemap::StaticClass() );
	Cube->USize = Cube->VSize = 16;
	Cube->Format = TEXF_RGBA8;
	UTexture* Faces[4];
	for( INT i=0; i<4; i++ )
	{
		Faces[i] = ConstructObject<UTexture>( UTexture::StaticClass() );
		Faces[i]->USize = Faces[i]->VSize = 16;
		Faces[i]->Format = TEXF_RGBA8;
	}
	Faces[1]->USize = 32;
	Faces[2]->Format = TEXF_DXT1;
	Faces[3]->Format = TEXF_P8;
	for( INT Face=0; Face<6; Face++ )
		Cube->Faces[Face] = Cube;
	for( INT i=0; i<4; i++ )
		Cube->Faces[i+1] = Faces[i];
	FCubemap* Interface = Cube->GetRenderInterface()->GetCubemapInterface();
	ASSERT_TRUE( Interface != NULL );
	EXPECT_EQ( NULL, Interface->GetFace(0) );
	EXPECT_EQ( Faces[0]->GetRenderInterface()->GetTextureInterface(), Interface->GetFace(1) );
	EXPECT_EQ( NULL, Interface->GetFace(2) );
	EXPECT_EQ( NULL, Interface->GetFace(3) );
	EXPECT_EQ( Faces[3]->GetRenderInterface()->GetTextureInterface(), Interface->GetFace(4) );
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

TEST( Core, Timing )
{
	DOUBLE Seconds = appSeconds();
	DWORD Cycles = appCycles();
	appSleep( 0.1f );
	DOUBLE Elapsed = appSeconds() - Seconds;
	DOUBLE CycleSeconds = (DWORD)(appCycles() - Cycles) * GSecondsPerCycle;
	EXPECT_GE( Elapsed, 0.099 );
	EXPECT_LT( Elapsed, 1.0 );
	EXPECT_NEAR( Elapsed, CycleSeconds, 0.02 );

	// The format demo files carry, 9 stands for a digit
	const TCHAR* Pattern = TEXT("99/99/99 99:99:99");
	FString Stamp = appTimestamp();
	ASSERT_EQ( appStrlen(Pattern), Stamp.Len() );
	for( INT i=0; Pattern[i]; i++ )
		EXPECT_TRUE( Pattern[i]=='9' ? appIsDigit((*Stamp)[i]) : (*Stamp)[i]==Pattern[i] ) << i;
}

TEST( Core, SharedSystemCommands )
{
	FStringOutputDevice Out;
	EXPECT_TRUE( GSys->Exec( TEXT("DIR Core.int"), Out ) );
	EXPECT_NE( -1, Out.InStr( TEXT("Core.int") ) ) << TCHAR_TO_ANSI(*Out);
	EXPECT_TRUE( GSys->Exec( TEXT("MEMSTAT"), Out ) );
	EXPECT_FALSE( GSys->Exec( TEXT("NOSUCHSYSTEMCOMMAND"), Out ) );
}

TEST( Core, CachePurgesOldFiles )
{
	// A folder of its own, the cleanup would take the player's real downloads too
	FString CachePath = GSys->CachePath;
	INT Days = GSys->PurgeCacheDays;
	GSys->CachePath = TEXT("PurgeTestCache");
	GFileManager->MakeDirectory( *GSys->CachePath );
	GSys->PurgeCacheDays = 30;
	FString Old = GSys->CachePath * (FString(TEXT("old")) + GSys->CacheExt), New = GSys->CachePath * (FString(TEXT("new")) + GSys->CacheExt);
	ASSERT_TRUE( appSaveStringToFile( TEXT("x"), *Old, GFileManager ) );
	ASSERT_TRUE( appSaveStringToFile( TEXT("x"), *New, GFileManager ) );
	std::filesystem::path Real = std::filesystem::u8path( &appToUtf8( *GFileManager->ExpandPath( *Old ) )(0) );
	std::filesystem::last_write_time( Real, std::filesystem::last_write_time( Real ) - std::chrono::hours( 24*40 ) );
	appCleanFileCache();
	EXPECT_LT( GFileManager->FileSize( *Old ), 0 );
	EXPECT_GE( GFileManager->FileSize( *New ), 0 );
	GFileManager->DeleteDirectory( *GSys->CachePath, 0, 1 );
	GSys->CachePath = CachePath;
	GSys->PurgeCacheDays = Days;
}

// The launcher passes the arguments after the executable and appInit keeps all of them. The engine test starts with the map.
TEST( Core, CommandLineStartsWithTheFirstArgument )
{
	EXPECT_EQ( 0, appStrncmp( appCmdLine(), TEXT("DM-Rankin"), 9 ) ) << TCHAR_TO_ANSI(appCmdLine());
}
