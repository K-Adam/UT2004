// Input a server, a client or downloaded script controls must not overrun buffers or forge pointers.

// The system headers go before the engine's, whose macros (check, clock, min, max) break them.
#pragma pack(push,8)
#if _WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>
#define closesocket close
#endif
#include <cstring>
#include <string>
#pragma pack(pop)

#include "TestEngine.h"

static FString Repeat( const TCHAR* Text, INT Count )
{
	FString Result;
	while( Count-- > 0 )
		Result += Text;
	return Result;
}

// Script natives take their parameters, then the return value, as one block.
static FString CallString( UObject* Object, const TCHAR* Function, const FString& Arg )
{
	struct { FString Arg; FString Result; } Parms;
	Parms.Arg = Arg;
	Object->ProcessEvent( Object->FindFunctionChecked( FName(Function) ), &Parms );
	return Parms.Result;
}

template<class T> static T* FirstProperty()
{
	for( TObjectIterator<T> It; It; ++It )
		if( It->GetClass()==T::StaticClass() )
			return *It;
	return NULL;
}

TEST( Security, LongPackagePath )
{
	TCHAR Out[256];
	new(GSys->Paths)FString( Repeat( TEXT("p"), 200 ) + TEXT("/*.u") );
	EXPECT_FALSE( appFindPackageFile( *Repeat( TEXT("n"), 199 ), NULL, Out, 0 ) );
	GSys->Paths.Remove( GSys->Paths.Num()-1 );
}

TEST( Security, LongIniName )
{
	FString Name = Repeat( TEXT("i"), 255 );
	TArray<FString> Sections;
	GConfig->GetSectionNames( Sections, *Name );
	FString Value;
	EXPECT_FALSE( GConfig->GetString( TEXT("Section"), TEXT("Key"), Value, *Name ) );
	Localize( TEXT("Section"), TEXT("Key"), *Name, NULL, 1 );
}

TEST( Security, LongPropertyText )
{
	ALevelInfo* Info = GTestEngine->GLevel->GetLevelInfo();
	FString Old = Info->Title;
	Info->Title = Repeat( TEXT("t"), 5000 );
	EXPECT_EQ( 5000, CallString( Info, TEXT("GetPropertyText"), TEXT("Title") ).Len() );
	Info->Title = Old;
}

TEST( Security, LongFileLogLine )
{
	UClass* Class = UObject::StaticLoadClass( AActor::StaticClass(), NULL, TEXT("Engine.FileLog"), NULL, LOAD_NoWarn, NULL );
	ASSERT_TRUE( Class );
	AActor* Log = GTestEngine->GLevel->SpawnActor( Class );
	ASSERT_TRUE( Log );
	struct { FString Name; FString Ext; BITFIELD bOverwrite; } Open;
	Open.Name = TEXT("SecurityTest");
	Open.bOverwrite = 1;
	Log->ProcessEvent( Log->FindFunctionChecked( FName(TEXT("OpenLog")) ), &Open );
	struct { FString Line; } Line;
	Line.Line = Repeat( TEXT("l"), 5000 );
	Log->ProcessEvent( Log->FindFunctionChecked( FName(TEXT("Logf")) ), &Line );
	Log->ProcessEvent( Log->FindFunctionChecked( FName(TEXT("CloseLog")) ), NULL );
	GTestEngine->GLevel->DestroyActor( Log );
	FString File = FString(TEXT("..")) + PATH_SEPARATOR + TEXT("UserLogs") + PATH_SEPARATOR + TEXT("SecurityTest.log");
	EXPECT_EQ( 5002, GFileManager->FileSize( *File ) );
	GFileManager->Delete( *File );
}

TEST( Security, ImportTextKeepsTypes )
{
	UPointerProperty* Pointer = FirstProperty<UPointerProperty>();
	ASSERT_TRUE( Pointer );
	PTRINT Address = 7;
	const TCHAR* Digits = TEXT("12345");
	EXPECT_EQ( Digits+5, Pointer->ImportText( Digits, (BYTE*)&Address, 0 ) );
	EXPECT_EQ( 7, Address ) << "text set a native pointer";

	UObjectProperty* Level = FindField<UObjectProperty>( AActor::StaticClass(), TEXT("Level") );
	ASSERT_TRUE( Level );
	UObject* Object = NULL;
	Level->ImportText( TEXT("Class'Core.Object'"), (BYTE*)&Object, 0 );
	EXPECT_FALSE( Object ) << "a class went into a LevelInfo reference";

	UClassProperty* Meta = NULL;
	for( TFieldIterator<UProperty> It(AGameInfo::StaticClass()); It && !Meta; ++It )
		if( Cast<UClassProperty>(*It) && Cast<UClassProperty>(*It)->MetaClass!=UObject::StaticClass() )
			Meta = Cast<UClassProperty>(*It);
	ASSERT_TRUE( Meta );
	UClass* Class = AActor::StaticClass();
	Meta->ImportText( TEXT("None"), (BYTE*)&Class, 0 );
	EXPECT_FALSE( Class );
	Meta->ImportText( TEXT("Class'Core.Object'"), (BYTE*)&Class, 0 );
	EXPECT_FALSE( Class ) << "a class outside the metaclass was accepted";

	UNameProperty* Name = FirstProperty<UNameProperty>();
	ASSERT_TRUE( Name );
	FName Value = NAME_None;
	const TCHAR* Words = TEXT("Foo Bar");
	const TCHAR* End = Name->ImportText( Words, (BYTE*)&Value, 0 );
	EXPECT_TRUE( End>Words && End<=Words+7 ) << "the returned position is outside the input";
	EXPECT_STREQ( "Foo", TCHAR_TO_ANSI( *Value ) );

	UDelegateProperty* Delegate = FirstProperty<UDelegateProperty>();
	ASSERT_TRUE( Delegate );
	BYTE Storage[64] = {};
	ASSERT_LE( Delegate->ElementSize, (INT)sizeof(Storage) );
	Delegate->ImportText( *(Repeat( TEXT("o"), 300 ) + TEXT(".") + Repeat( TEXT("f"), 300 )), Storage, 0 );
}

TEST( Security, ConfigValuesStayOnOneLine )
{
	const TCHAR* File = TEXT("SecurityTest.ini");
	GConfig->SetString( TEXT("Section\r\n[Forged]"), TEXT("Key\r\nForgedKey"), TEXT("Value\r\n[Forged]\r\nForgedKey=1"), File );
	GConfig->Flush( 0, File );
	FString Text;
	ASSERT_TRUE( appLoadFileToString( Text, File ) );
	EXPECT_EQ( -1, Text.InStr( TEXT("\n[Forged]") ) );
	EXPECT_EQ( -1, Text.InStr( TEXT("\nForgedKey") ) );
	GConfig->UnloadFile( File );
	GFileManager->Delete( File );
}

TEST( Security, ScriptCannotSetSystemPaths )
{
	FString Old = GSys->SavePath;
	UObject::StaticExec( TEXT("SET System SavePath Forged") );
	UObject::StaticExec( *FString::Printf( TEXT("POKE System %s SavePath Forged"), GSys->GetPathName() ) );
	EXPECT_STREQ( TCHAR_TO_ANSI(*Old), TCHAR_TO_ANSI(*GSys->SavePath) );
}

// A package picks its natives by number or name, which must not reach code written for another class.
TEST( Security, NativeOfAnotherClassIsRefused )
{
	UFunction* ActorNative = NULL;
	for( TFieldIterator<UFunction> It(AActor::StaticClass()); It && !ActorNative; ++It )
		if( It->iNative>=256 && It->GetOwnerClass()==AActor::StaticClass() )
			ActorNative = *It;
	ASSERT_TRUE( ActorNative );
	UFunction* Forged = new( UCommandlet::StaticClass(), TEXT("SecurityForged") )UFunction( NULL );
	Forged->FunctionFlags = FUNC_Native;
	Forged->iNative = ActorNative->iNative;
	Forged->Bind();
	EXPECT_TRUE( Forged->Func==(Native)&UObject::execUndefined );
	EXPECT_TRUE( GRefusedScript.Len()>0 );
	GRefusedScript = TEXT("");
}

// The query script answers each request in a datagram, the engine caps what one datagram can set off.
TEST( Security, GamespyQueryReplyLimit )
{
	UClass* Class = UObject::StaticLoadClass( AActor::StaticClass(), NULL, TEXT("IpDrv.UdpGamespyQuery"), NULL, LOAD_NoWarn, NULL );
	ASSERT_TRUE( Class );
	AActor* Query = GTestEngine->GLevel->SpawnActor( Class );
	ASSERT_TRUE( Query );
	UIntProperty* Port = FindField<UIntProperty>( Class, TEXT("Port") );
	ASSERT_TRUE( Port );

	sockaddr_in Server;
	memset( &Server, 0, sizeof(Server) );
	Server.sin_family = AF_INET;
	Server.sin_port = htons( (unsigned short)*(INT*)((BYTE*)Query + Port->Offset) );
	Server.sin_addr.s_addr = htonl( INADDR_LOOPBACK );
	auto Socket = socket( AF_INET, SOCK_DGRAM, 0 );
#if _WIN32
	u_long NonBlocking = 1;
	ioctlsocket( Socket, FIONBIO, &NonBlocking );
#else
	fcntl( Socket, F_SETFL, O_NONBLOCK );
#endif
	auto Replies = [&]( const std::string& Request )
	{
		sendto( Socket, Request.data(), (int)Request.size(), 0, (sockaddr*)&Server, sizeof(Server) );
		RunGame( 0.5f );
		INT Count = 0;
		char Reply[2048];
		while( recvfrom( Socket, Reply, sizeof(Reply), 0, NULL, NULL ) > 0 )
			Count++;
		return Count;
	};

	EXPECT_GT( Replies( "\\status\\" ), 0 );
	std::string Flood;
	for( INT i=0; i<100; i++ )
		Flood += "\\status\\";
	INT Count = Replies( Flood );
	EXPECT_GT( Count, 0 );
	// GAMESPY_REPLIES_PER_DATAGRAM in UdpLink.cpp
	EXPECT_LE( Count, 16 );

	closesocket( Socket );
	GTestEngine->GLevel->DestroyActor( Query );
}
