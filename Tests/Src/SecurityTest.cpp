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

// A script function made by hand, as a downloaded package can hold any bytecode.
struct FScript
{
	UFunction* Function;
	TArray<UProperty*> Props;
	TArray<BYTE>& Code;

	FScript( const TCHAR* Name )
	:	Function( new( AActor::StaticClass(), Name )UFunction( NULL ) )
	,	Code( Function->Script )
	{}
	template<class T> T* Add( T* Property )
	{
		Props.AddItem( Property );
		return Property;
	}
	UIntProperty* Int( const TCHAR* Name, DWORD Flags, INT Dim=1 )
	{
		UIntProperty* Property = Add( new( Function, Name )UIntProperty( EC_CppProperty, 0, TEXT(""), Flags ) );
		Property->ArrayDim = Dim;
		return Property;
	}
	UObjectProperty* Ref( const TCHAR* Name, DWORD Flags, UClass* Class )
	{
		return Add( new( Function, Name )UObjectProperty( EC_CppProperty, 0, TEXT(""), Flags, Class ) );
	}
	UArrayProperty* IntArray( const TCHAR* Name )
	{
		UArrayProperty* Property = Add( new( Function, Name )UArrayProperty( EC_CppProperty, 0, TEXT(""), 0 ) );
		new( Property, Name )UIntProperty( EC_CppProperty, 0, TEXT(""), 0 );
		return Property;
	}
	// The property constructor puts each one first, the function needs them in the order they were made.
	void Link()
	{
		for( INT i=0; i<Props.Num(); i++ )
			Props(i)->Next = i+1<Props.Num() ? Props(i+1) : NULL;
		Function->Children = Props.Num() ? Props(0) : NULL;
		FArchive Ar;
		Function->Link( Ar, 1 );
		Function->ReturnValueOffset = MAXWORD;
		for( INT i=0; i<Props.Num() && (Props(i)->PropertyFlags & CPF_Parm); i++ )
		{
			Function->NumParms++;
			Function->ParmsSize = Props(i)->Offset + Props(i)->GetSize();
			if( Props(i)->PropertyFlags & CPF_ReturnParm )
				Function->ReturnValueOffset = Props(i)->Offset;
		}
		Function->Bind();
	}
	FScript& Byte( INT Value )
	{
		Code.AddItem( (BYTE)Value );
		return *this;
	}
	FScript& Word( INT Value )
	{
		return Byte( Value & 0xFF ).Byte( Value >> 8 );
	}
	FScript& Int32( INT Value )
	{
		appMemcpy( &Code(Code.Add(sizeof(INT))), &Value, sizeof(INT) );
		return *this;
	}
	FScript& Name( FName Value )
	{
		appMemcpy( &Code(Code.Add(sizeof(FName))), &Value, sizeof(FName) );
		return *this;
	}
	FScript& Object( UObject* Value )
	{
#if SERIAL_POINTER_INDEX
		return Int32( SerialPointerIndex( Value ) );
#else
		appMemcpy( &Code(Code.Add(sizeof(UObject*))), &Value, sizeof(UObject*) );
		return *this;
#endif
	}
	FScript& Local( UProperty* Property )
	{
		return Byte( EX_LocalVariable ).Object( Property );
	}
	// A context whose skip is patched by EndSkip, which returns the skip's position.
	INT Skip( INT Fill )
	{
		Word( 0 ).Byte( Fill );
		return Code.Num();
	}
	void EndSkip( INT Start )
	{
		INT Size = Code.Num() - Start;
		Code(Start-3) = Size & 0xFF;
		Code(Start-2) = Size >> 8;
	}
	FScript& AllActors( UProperty* Out )
	{
		return Byte( EX_ExtendedNative+1 ).Byte( 304-256 ).Byte( EX_ObjectConst ).Object( AActor::StaticClass() ).Local( Out ).Byte( EX_EndFunctionParms );
	}
	void Return()
	{
		Byte( EX_Return ).Byte( EX_Nothing ).Byte( EX_EndOfScript );
	}
	template<class T> T& Value( BYTE* Parms, UProperty* Property )
	{
		return *(T*)(Parms + Property->Offset);
	}
	void Run( BYTE* Parms, UObject* On=NULL )
	{
		(On ? On : GTestEngine->GLevel->GetLevelInfo())->ProcessEvent( Function, Parms );
	}
};

// foreach None.AllActors(Count+=1), with the context skip ending at the operand or past the loop.
static void ForeachOnNone( const TCHAR* Name, UBOOL SkipLoop, UFunction* ContextCall )
{
	FScript S( Name );
	UProperty* Count = S.Int( TEXT("Count"), CPF_Parm );
	UProperty* Done = S.Int( TEXT("Done"), CPF_Parm );
	UProperty* None = S.Ref( TEXT("NoActor"), 0, AActor::StaticClass() );
	UProperty* Each = S.Ref( TEXT("Each"), 0, AActor::StaticClass() );
	S.Link();
	S.Byte( EX_Iterator ).Byte( EX_Context );
	if( ContextCall )
		S.Byte( EX_FinalFunction ).Object( ContextCall ).Byte( EX_EndFunctionParms );
	else
		S.Local( None );
	INT Skip = S.Skip( 0 );
	S.AllActors( Each );
	if( !SkipLoop )
		S.EndSkip( Skip );
	INT End = S.Code.Num();
	S.Word( 0 );
	S.Byte( EX_Let ).Local( Count ).Byte( EX_IntOne );
	S.Byte( EX_IteratorNext );
	S.Code(End) = S.Code.Num() & 0xFF;
	S.Code(End+1) = S.Code.Num() >> 8;
	S.Byte( EX_IteratorPop );
	if( SkipLoop )
		S.EndSkip( Skip );
	S.Byte( EX_Let ).Local( Done ).Byte( EX_IntOne );
	S.Return();
	BYTE Parms[64] = {};
	S.Run( Parms );
	EXPECT_EQ( 0, S.Value<INT>( Parms, Count ) );
	EXPECT_EQ( 1, S.Value<INT>( Parms, Done ) );
}

TEST( Security, ForeachOnNoneSkipsTheLoop )
{
	ForeachOnNone( TEXT("SecurityForeachOperand"), 0, NULL );
	ForeachOnNone( TEXT("SecurityForeachLoop"), 1, NULL );

	// The context is a call that runs a foreach of its own and returns None.
	FScript Inner( TEXT("SecurityForeachInner") );
	Inner.Ref( TEXT("ReturnValue"), CPF_Parm|CPF_OutParm|CPF_ReturnParm, AActor::StaticClass() );
	UProperty* Each = Inner.Ref( TEXT("Each"), 0, AActor::StaticClass() );
	Inner.Link();
	Inner.Byte( EX_Iterator ).AllActors( Each );
	INT End = Inner.Code.Num();
	Inner.Word( End+3 ).Byte( EX_IteratorNext ).Byte( EX_IteratorPop );
	Inner.Byte( EX_Return ).Byte( EX_NoObject ).Byte( EX_EndOfScript );
	ForeachOnNone( TEXT("SecurityForeachNested"), 0, Inner.Function );
}

TEST( Security, DynArrayOpcodesNeedAnArray )
{
	FScript S( TEXT("SecurityDynArray") );
	UProperty* Length = S.Int( TEXT("Length"), CPF_Parm );
	UProperty* Nested = S.Int( TEXT("Nested"), CPF_Parm );
	UProperty* Array = S.IntArray( TEXT("Values") );
	S.Link();
	// Values[2] = 7
	S.Byte( EX_Let ).Byte( EX_DynArrayElement ).Byte( EX_IntConst ).Int32( 2 ).Local( Array ).Byte( EX_IntConst ).Int32( 7 );
	// Values.Remove( 1, MAXINT )
	S.Byte( EX_DynArrayRemove ).Local( Array ).Byte( EX_IntOne ).Byte( EX_IntConst ).Int32( MAXINT );
	S.Byte( EX_Let ).Local( Length ).Byte( EX_DynArrayLength ).Local( Array );
	// Nested = Values[0].Length, which would read the element as an array.
	S.Byte( EX_Let ).Local( Nested ).Byte( EX_DynArrayLength ).Byte( EX_DynArrayElement ).Byte( EX_IntZero ).Local( Array );
	S.Return();
	BYTE Parms[64] = {};
	S.Value<INT>( Parms, Nested ) = 123;
	S.Run( Parms );
	EXPECT_EQ( 1, S.Value<INT>( Parms, Length ) );
	EXPECT_EQ( 123, S.Value<INT>( Parms, Nested ) );
}

// Plane extends Vector, so a member of Vector is read from a value that is larger than Vector.
TEST( Security, StructMemberOfDerivedStruct )
{
	UStruct* Vector = FindObject<UStruct>( ANY_PACKAGE, TEXT("Vector") );
	UStruct* Plane = FindObject<UStruct>( ANY_PACKAGE, TEXT("Plane") );
	ASSERT_TRUE( Vector && Plane );
	EXPECT_GE( Vector->DerivedSize, Plane->PropertiesSize );

	FScript S( TEXT("SecurityStructMember") );
	UProperty* Value = S.Add( new( S.Function, TEXT("Value") )UStructProperty( EC_CppProperty, 0, TEXT(""), CPF_Parm, Plane ) );
	UProperty* X = S.Add( new( S.Function, TEXT("X") )UFloatProperty( EC_CppProperty, 0, TEXT(""), CPF_Parm ) );
	S.Link();
	S.Byte( EX_Let ).Local( X ).Byte( EX_StructMember ).Object( FindField<UProperty>( Vector, TEXT("X") ) ).Local( Value );
	S.Return();
	BYTE Parms[64] = {};
	S.Value<FPlane>( Parms, Value ) = FPlane( 3, 4, 5, 6 );
	S.Run( Parms );
	EXPECT_EQ( 3.f, S.Value<FLOAT>( Parms, X ) );
}

TEST( Security, ObjectOfAnotherClassIsNotStored )
{
	UObject* Foreign = AActor::StaticClass();
	UClass* Info = ALevelInfo::StaticClass();

	// Callee copies its LevelInfo parameter In to the out parameter Seen and puts the foreign object into Made.
	FScript Callee( TEXT("SecurityStoreCallee") );
	UProperty* In = Callee.Ref( TEXT("In"), CPF_Parm, Info );
	UProperty* Seen = Callee.Ref( TEXT("Seen"), CPF_Parm|CPF_OutParm, UObject::StaticClass() );
	UProperty* Made = Callee.Ref( TEXT("Made"), CPF_Parm|CPF_OutParm, UObject::StaticClass() );
	Callee.Link();
	Callee.Byte( EX_Let ).Local( Seen ).Local( In );
	Callee.Byte( EX_Let ).Local( Made ).Byte( EX_ObjectConst ).Object( Foreign );
	Callee.Return();

	FScript S( TEXT("SecurityStore") );
	UProperty* Let = S.Ref( TEXT("Let"), CPF_Parm, Info );
	UProperty* Kept = S.Ref( TEXT("Kept"), CPF_Parm, Info );
	UProperty* Arg = S.Ref( TEXT("Arg"), CPF_Parm, UObject::StaticClass() );
	UProperty* Out = S.Ref( TEXT("Out"), CPF_Parm, Info );
	S.Link();
	S.Byte( EX_Let ).Local( Let ).Byte( EX_ObjectConst ).Object( Foreign );
	S.Byte( EX_Let ).Local( Kept ).Byte( EX_Self );
	S.Byte( EX_FinalFunction ).Object( Callee.Function ).Byte( EX_ObjectConst ).Object( Foreign ).Local( Arg ).Local( Out ).Byte( EX_EndFunctionParms );
	S.Return();
	BYTE Parms[64] = {};
	S.Value<UObject*>( Parms, Arg ) = Foreign;
	S.Run( Parms );
	EXPECT_FALSE( S.Value<UObject*>( Parms, Let ) ) << "a class went into a LevelInfo variable";
	EXPECT_TRUE( S.Value<UObject*>( Parms, Kept )==GTestEngine->GLevel->GetLevelInfo() );
	EXPECT_FALSE( S.Value<UObject*>( Parms, Out ) ) << "an out parameter brought a class into a LevelInfo variable";
}

TEST( Security, MissingFunctionUnderContextIsSkipped )
{
	FScript S( TEXT("SecurityMissingCall") );
	UProperty* Result = S.Int( TEXT("Result"), CPF_Parm );
	UProperty* Done = S.Int( TEXT("Done"), CPF_Parm );
	S.Link();
	S.Byte( EX_Let ).Local( Result ).Byte( EX_Context ).Byte( EX_Self );
	INT Skip = S.Skip( sizeof(INT) );
	S.Byte( EX_VirtualFunction ).Name( FName(TEXT("SecurityNoSuchFunction")) ).Byte( EX_IntOne ).Byte( EX_EndFunctionParms );
	S.EndSkip( Skip );
	S.Byte( EX_Let ).Local( Done ).Byte( EX_IntOne );
	S.Return();
	BYTE Parms[64] = {};
	S.Value<INT>( Parms, Result ) = 5;
	S.Run( Parms );
	EXPECT_EQ( 0, S.Value<INT>( Parms, Result ) );
	EXPECT_EQ( 1, S.Value<INT>( Parms, Done ) );
}

TEST( Security, DelegateTargetMustFit )
{
	AActor* Info = GTestEngine->GLevel->GetLevelInfo();
	UFunction* Touch = Info->FindFunctionChecked( FName(TEXT("Touch")) );
	EXPECT_TRUE( GSameParms( Touch, Info->FindFunctionChecked( FName(TEXT("UnTouch")) ) ) );
	EXPECT_FALSE( GSameParms( Touch, Info->FindFunctionChecked( FName(TEXT("Tick")) ) ) );

	// A target that is missing or takes another parameter block is not called.
	struct { AActor* Other; } Parms = { NULL };
	FScriptDelegate Delegate;
	Delegate.Object = Info;
	Delegate.FunctionName = FName(TEXT("SecurityNoSuchFunction"));
	Info->ProcessDelegate( FName(TEXT("Touch")), &Delegate, &Parms );
	Delegate.FunctionName = FName(TEXT("Tick"));
	Info->ProcessDelegate( FName(TEXT("Touch")), &Delegate, &Parms );

	// Script calls OnQueryFinished( 1, 5 ) while the delegate names a function that takes a string.
	UClass* Class = UObject::StaticLoadClass( AActor::StaticClass(), NULL, TEXT("IpDrv.MasterServerClient"), NULL, LOAD_NoWarn, NULL );
	ASSERT_TRUE( Class );
	AActor* Client = GTestEngine->GLevel->SpawnActor( Class );
	ASSERT_TRUE( Client );
	UProperty* Property = FindField<UProperty>( Class, TEXT("__OnQueryFinished__Delegate") );
	ASSERT_TRUE( Property );
	FScriptDelegate* Slot = (FScriptDelegate*)((BYTE*)Client + Property->Offset);
	Slot->Object = Client;
	Slot->FunctionName = FName(TEXT("OnReceivedModMOTDData"));
	FScript S( TEXT("SecurityDelegateCall") );
	UProperty* Done = S.Int( TEXT("Done"), CPF_Parm );
	S.Link();
	S.Byte( EX_DelegateFunction ).Object( Property ).Name( FName(TEXT("OnQueryFinished")) );
	S.Byte( EX_ByteConst ).Byte( 1 ).Byte( EX_IntConst ).Int32( 5 ).Byte( EX_EndFunctionParms );
	S.Byte( EX_Let ).Local( Done ).Byte( EX_IntOne );
	S.Return();
	BYTE Called[64] = {};
	S.Run( Called, Client );
	EXPECT_EQ( 1, S.Value<INT>( Called, Done ) );
	Slot->Object = NULL;
	Slot->FunctionName = NAME_None;
	GTestEngine->GLevel->DestroyActor( Client );
}

// The stored fill size is the 32-bit one, an object result has to be cleared as a whole.
TEST( Security, NoneContextClearsObjectResult )
{
	FScript S( TEXT("SecurityNoneSpawn") );
	UProperty* Result = S.Ref( TEXT("Result"), CPF_Parm, AActor::StaticClass() );
	UProperty* None = S.Ref( TEXT("NoActor"), 0, AActor::StaticClass() );
	S.Link();
	S.Byte( EX_Let ).Local( Result ).Byte( EX_Context ).Local( None );
	INT Skip = S.Skip( 4 );
	S.Byte( EX_ExtendedNative+1 ).Byte( 278-256 ).Byte( EX_EndFunctionParms );
	S.EndSkip( Skip );
	S.Return();
	BYTE Parms[64] = {};
	S.Value<UObject*>( Parms, Result ) = GTestEngine->GLevel->GetLevelInfo();
	S.Run( Parms );
	EXPECT_FALSE( S.Value<UObject*>( Parms, Result ) );
}

TEST( Security, AssignmentThroughNoneOfAnySize )
{
	FScript S( TEXT("SecurityNoneLet") );
	UProperty* Done = S.Int( TEXT("Done"), CPF_Parm );
	UProperty* None = S.Ref( TEXT("NoActor"), 0, AActor::StaticClass() );
	UProperty* Big = S.Int( TEXT("Big"), 0, 600 );
	S.Link();
	EXPECT_GE( GMaxPropertySize, Big->GetSize() );
	S.Byte( EX_Let ).Byte( EX_Context ).Local( None );
	INT Skip = S.Skip( 0 );
	S.Byte( EX_InstanceVariable ).Object( FindField<UProperty>( AActor::StaticClass(), TEXT("Tag") ) );
	S.EndSkip( Skip );
	S.Local( Big );
	S.Byte( EX_Let ).Local( Done ).Byte( EX_IntOne );
	S.Return();
	BYTE Parms[64] = {};
	S.Run( Parms );
	EXPECT_EQ( 1, S.Value<INT>( Parms, Done ) );
}

// A saved state frame names its latent action by number.
TEST( Security, LatentActionMustBeAPollNative )
{
	AActor* Info = GTestEngine->GLevel->GetLevelInfo();
	FScript S( TEXT("SecurityStateCode") );
	S.Byte( EX_Stop );
	FStateFrame* Frame = Info->GetStateFrame();
	ASSERT_TRUE( Frame );
	UStruct* Node = Frame->Node;
	BYTE* Code = Frame->Code;
	Frame->Node = S.Function;
	Frame->Code = &S.Code(0);
	Frame->LatentAction = 278;
	Info->ProcessState( 0.1f );
	EXPECT_EQ( 0, Frame->LatentAction );
	EXPECT_FALSE( Frame->Code );
	Frame->Node = Node;
	Frame->Code = Code;
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
