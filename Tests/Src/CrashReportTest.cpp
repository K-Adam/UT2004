#include "TestEngine.h"
#include "CrashReport.h"

TEST( CrashReport, RedactsWhatIdentifiesThePlayer )
{
	TArray<FString> Names;
	new(Names) FString( TEXT("kovacs") );
	new(Names) FString( TEXT("Xan") );
	new(Names) FString( TEXT("ab") );
	FString Log =
		TEXT("Init: Base directory: C:\\Users\\Kovacs\\UT2004\\System\\\r\n")
		TEXT("Log: Browse: 192.168.1.20:7777/DM-Rankin?Name=Xan?Password=secret?team=255\r\n")
		TEXT("Init: Command line: -AdminPassword=hunter2 -windowed\r\n")
		TEXT("ScriptLog: New Player Xan id=98949deac6c337d804e2328b8e4e63c4\r\n")
		TEXT("Log: GL_RENDERER   : ANGLE (D3D11-32.0.15.7602)\r\n")
		TEXT("Log: Startup time: 7.609251 seconds, cab 300.1.2.3.\r\n")
		TEXT("Log: Resolved master (10.0.0.1), connecting...10.0.0.2");
	FString Out = RedactLog( Log, Names );
	EXPECT_TRUE( Out == FString(
		TEXT("Init: Base directory: C:\\Users\\<name>\\UT2004\\System\\\r\n")
		TEXT("Log: Browse: <ip>:7777/DM-Rankin?Name=<name>?Password=<password>?team=255\r\n")
		TEXT("Init: Command line: -AdminPassword=<password> -windowed\r\n")
		TEXT("ScriptLog: New Player <name> id=<id>\r\n")
		TEXT("Log: GL_RENDERER   : ANGLE (D3D11-32.0.15.7602)\r\n")
		TEXT("Log: Startup time: 7.609251 seconds, cab 300.1.2.3.\r\n")
		TEXT("Log: Resolved master (<ip>), connecting...<ip>") ) ) << TCHAR_TO_ANSI(*Out);
}

TEST( CrashReport, TailStartsAtALine )
{
	EXPECT_TRUE( LogTail( TEXT("first\nsecond\nthird"), 9 ) == TEXT("third") );
	EXPECT_TRUE( LogTail( TEXT("first\nsecond"), 100 ) == TEXT("first\nsecond") );
}
