#include "TestEngine.h"
#include "FConfigCacheIni.h"

TEST( Config, AddMissingKeepsValuesAndAddsWholeLists )
{
	FConfigFile Defaults;
	FConfigSection& Link = Defaults.Set( TEXT("IpDrv.MasterServerLink"), FConfigSection() );
	Link.Add( TEXT("StunServer"), TEXT("stun.example:3478") );
	Link.Add( TEXT("WebMasterServer"), TEXT("https://master.example") );
	Link.Add( TEXT("MasterServerList"), TEXT("A") );
	Link.Add( TEXT("MasterServerList"), TEXT("B") );
	Defaults.Set( TEXT("IpDrv.AssetDownload"), FConfigSection() ).Add( TEXT("URL"), TEXT("https://assets.example/") );
	Defaults.Set( TEXT("DefaultDM MaplistRecord"), FConfigSection() ).Add( TEXT("DefaultMaps"), TEXT("DM-Example") );

	FConfigFile Ini;
	FConfigSection& Old = Ini.Set( TEXT("ipdrv.masterserverlink"), FConfigSection() );
	Old.Add( TEXT("WebMasterServer"), TEXT("") );
	Old.Add( TEXT("MasterServerList"), TEXT("Mine") );
	Ini.AddMissing( Defaults );

	FConfigSection* Section = Ini.Find( TEXT("IpDrv.MasterServerLink") );
	ASSERT_TRUE( Section != NULL );
	// An emptied value is a choice the player made
	EXPECT_TRUE( *Section->Find( TEXT("WebMasterServer") ) == TEXT("") );
	EXPECT_TRUE( *Section->Find( TEXT("StunServer") ) == TEXT("stun.example:3478") );
	TArray<FString> Masters;
	Section->MultiFind( TEXT("MasterServerList"), Masters );
	ASSERT_EQ( 1, Masters.Num() );
	EXPECT_TRUE( Masters(0) == TEXT("Mine") );

	FConfigSection* Added = Ini.Find( TEXT("IpDrv.AssetDownload") );
	ASSERT_TRUE( Added != NULL );
	EXPECT_TRUE( *Added->Find( TEXT("URL") ) == TEXT("https://assets.example/") );
	// The player deleted this map list
	EXPECT_TRUE( Ini.Find( TEXT("DefaultDM MaplistRecord") ) == NULL );

	FConfigFile Empty;
	Empty.AddMissing( Defaults );
	Masters.Empty();
	Empty.Find( TEXT("IpDrv.MasterServerLink") )->MultiFind( TEXT("MasterServerList"), Masters );
	EXPECT_EQ( 2, Masters.Num() );
}

TEST( Config, GameIniReadsKeysItLacksFromDefaults )
{
	FConfigFile Defaults;
	Defaults.Read( TEXT("Default.ini") );
	const FConfigSection* Link = Defaults.Find( TEXT("IpDrv.MasterServerLink") );
	ASSERT_TRUE( Link != NULL );
	const FString* Stun = Link->Find( TEXT("StunServer") );
	ASSERT_TRUE( Stun != NULL && Stun->Len() );

	appSaveStringToFile( FString(TEXT("[IpDrv.MasterServerLink]\r\nWebMasterServer=\r\n")), TEXT("ConfigTest.ini") );
	{
		FConfigCacheIni Cache;
		Cache.Init( TEXT("ConfigTest.ini"), TEXT("ConfigTestUser.ini"), 1 );
		FString Value;
		EXPECT_TRUE( Cache.GetString( TEXT("IpDrv.MasterServerLink"), TEXT("StunServer"), Value, NULL ) );
		EXPECT_TRUE( Value == *Stun );
		EXPECT_TRUE( Cache.GetString( TEXT("IpDrv.MasterServerLink"), TEXT("WebMasterServer"), Value, NULL ) );
		EXPECT_TRUE( Value == TEXT("") );
	}
	GFileManager->Delete( TEXT("ConfigTest.ini") );
}
