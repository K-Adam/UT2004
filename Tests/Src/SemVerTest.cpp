// gtest is built with the default packing.
#pragma pack(push,8)
#include <gtest/gtest.h>
#include "SemVer.h"
#pragma pack(pop)

TEST( SemVer, NewerOnlyWhenStrictlyGreater )
{
	EXPECT_TRUE( SemVerIsNewer( "1.0.0-rc.4", "1.0.0-rc.3" ) );
	EXPECT_TRUE( SemVerIsNewer( "1.0.0-rc.10", "1.0.0-rc.2" ) );
	EXPECT_TRUE( SemVerIsNewer( "1.0.0-rc.10", "1.0.0-rc.9" ) );
	EXPECT_TRUE( SemVerIsNewer( "1.0.0", "1.0.0-rc.3" ) );
	EXPECT_TRUE( SemVerIsNewer( "1.0.1-rc.1", "1.0.0" ) );
	EXPECT_TRUE( SemVerIsNewer( "1.10.0", "1.9.0" ) );
	EXPECT_TRUE( SemVerIsNewer( "1.0.0-rc.1", "1.0.0-beta.9" ) );
	EXPECT_TRUE( SemVerIsNewer( "1.0.0-rc.1.1", "1.0.0-rc.1" ) );
	EXPECT_TRUE( SemVerIsNewer( "1.0.0-rc", "1.0.0-1" ) );

	EXPECT_FALSE( SemVerIsNewer( "1.0.0-rc.3", "1.0.0-rc.3" ) );
	EXPECT_FALSE( SemVerIsNewer( "1.0.0-rc.3+build5", "1.0.0-rc.3" ) );
	EXPECT_FALSE( SemVerIsNewer( "1.0.0-rc.3", "1.0.0-rc.4" ) );
	EXPECT_FALSE( SemVerIsNewer( "1.0.0-rc.2", "1.0.0-rc.10" ) );
	EXPECT_FALSE( SemVerIsNewer( "1.0.0-rc.3", "1.0.0" ) );
}

TEST( SemVer, UnparsedIsNeverNewer )
{
	EXPECT_FALSE( SemVerIsNewer( "1.0.0-rc.3", "develop" ) );
	EXPECT_FALSE( SemVerIsNewer( "develop", "1.0.0" ) );
	EXPECT_FALSE( SemVerIsNewer( "", "1.0.0" ) );
	EXPECT_FALSE( SemVerIsNewer( "2.0", "1.0.0" ) );
	EXPECT_FALSE( SemVerIsNewer( "2.x.0", "1.0.0" ) );
	EXPECT_FALSE( SemVerIsNewer( "99999999999.0.0", "1.0.0" ) );
	EXPECT_FALSE( SemVerIsNewer( "9.0.0-" + std::string( 100, '.' ), "1.0.0" ) );
}
