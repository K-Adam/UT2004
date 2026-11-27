// Input a server, a client or downloaded script controls must not overrun buffers or forge pointers.

// The system headers go before the engine's, whose macros (check, clock, min, max) break them.
#pragma pack(push,8)
#include "UnSocketApi.h"
#include <cstring>
#include <string>
#pragma pack(pop)

#include "TestEngine.h"
#include "UnNet.h"
#include "UnLinker.h"
#include "UnFractal.h"

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

	UClass* Named = new( UObject::CreatePackage( NULL, TEXT("SecurityNamed") ), TEXT("Object") )UClass( UObject::StaticClass() );
	UFunction* Opcode = new( Named, TEXT("LocalVariable") )UFunction( NULL );
	Opcode->FunctionFlags = FUNC_Native;
	Opcode->Bind();
	EXPECT_TRUE( Opcode->Func==(Native)&UObject::execUndefined );
	EXPECT_TRUE( GRefusedScript.Len()>0 );
	GRefusedScript = TEXT("");
}

// A script function made by hand, as a downloaded package can hold any bytecode.
struct FScript
{
	UFunction* Function;
	TArray<UProperty*> Props;
	TArray<BYTE>& Code;

	FScript( const TCHAR* Name, UObject* Outer=AActor::StaticClass() )
	:	Function( new( Outer, Name )UFunction( NULL ) )
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

// An out parameter is copied back only to a variable that holds its type.
TEST( Security, OutParameterNeedsAVariableOfItsType )
{
	FScript Callee( TEXT("SecurityOutCallee") );
	UProperty* Out = Callee.Int( TEXT("Out"), CPF_Parm|CPF_OutParm );
	Callee.Link();
	Callee.Byte( EX_Let ).Local( Out ).Byte( EX_IntConst ).Int32( 5 );
	Callee.Return();

	FScript S( TEXT("SecurityOutCaller") );
	UProperty* Same = S.Int( TEXT("Same"), CPF_Parm );
	UProperty* Other = S.Add( new( S.Function, TEXT("Other") )UFloatProperty( EC_CppProperty, 0, TEXT(""), CPF_Parm ) );
	S.Link();
	S.Byte( EX_FinalFunction ).Object( Callee.Function ).Local( Same ).Byte( EX_EndFunctionParms );
	S.Byte( EX_FinalFunction ).Object( Callee.Function ).Local( Other ).Byte( EX_EndFunctionParms );
	S.Return();
	BYTE Parms[64] = {};
	S.Run( Parms );
	EXPECT_EQ( 5, S.Value<INT>( Parms, Same ) );
	EXPECT_EQ( 0, S.Value<INT>( Parms, Other ) );
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

	// C++ reads a return value as the function declares it.
	FScript Giver( TEXT("SecurityReturnForeign") );
	UProperty* Returned = Giver.Ref( TEXT("ReturnValue"), CPF_Parm|CPF_OutParm|CPF_ReturnParm, Info );
	Giver.Link();
	Giver.Byte( EX_Return ).Byte( EX_ObjectConst ).Object( Foreign ).Byte( EX_EndOfScript );
	BYTE GiverParms[64] = {};
	Giver.Run( GiverParms );
	EXPECT_FALSE( Giver.Value<UObject*>( GiverParms, Returned ) ) << "a class was returned as a LevelInfo";

	// foreach AllActors(class'Actor', Each) with a LevelInfo variable visits only the LevelInfo.
	FScript Loop( TEXT("SecurityNarrowOut") );
	UProperty* Count = Loop.Int( TEXT("Count"), CPF_Parm );
	UProperty* Each = Loop.Ref( TEXT("Each"), 0, Info );
	Loop.Link();
	Loop.Byte( EX_Iterator ).AllActors( Each );
	INT End = Loop.Code.Num();
	Loop.Word( 0 );
	// Count = Count + 1, native 146 is the int addition.
	Loop.Byte( EX_Let ).Local( Count ).Byte( 146 ).Local( Count ).Byte( EX_IntOne ).Byte( EX_EndFunctionParms );
	Loop.Byte( EX_IteratorNext );
	Loop.Code(End) = Loop.Code.Num() & 0xFF;
	Loop.Code(End+1) = Loop.Code.Num() >> 8;
	Loop.Byte( EX_IteratorPop );
	Loop.Return();
	BYTE LoopParms[64] = {};
	Loop.Run( LoopParms );
	EXPECT_EQ( 1, Loop.Value<INT>( LoopParms, Count ) ) << "an actor of another class went into a LevelInfo variable";
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
	Frame->Code = S.Code.GetTypedData();
	Frame->LatentAction = 278;
	Info->ProcessState( 0.1f );
	EXPECT_EQ( 0, Frame->LatentAction );
	EXPECT_FALSE( Frame->Code );
	Frame->Node = Node;
	Frame->Code = Code;
}

// The loader repairs a layout it cannot trust and refuses the package.
static UBOOL TakeRefusal( UPackage* Package )
{
	UBOOL Refused = Package->Refused && GRefusedScript.Len();
	Package->Refused = 0;
	GRefusedScript = TEXT("");
	return Refused;
}

// A script class as a package would hold it. The constructor copies the super's defaults byte by byte.
static UClass* ForgedClass( UPackage* Package, const TCHAR* Name, UClass* Super )
{
	UClass* Class = new( Package, Name, RF_Public|RF_Standalone )UClass( Super );
	Class->Defaults.Empty();
	Class->Defaults.Add( Class->GetPropertiesSize() );
	Class->GetDefaultObject()->InitClassDefaultObject( Class );
	Class->LabelTableOffset = MAXWORD;
	FArchive Ar;
	Class->Link( Ar, 1 );
	return Class;
}

static UFunction* Lookup( UState* Scope, const TCHAR* Name )
{
	FName Find( Name );
	for( UField* Node=Scope->VfHash[Find.GetIndex() & (UField::HASH_COUNT-1)]; Node; Node=Node->HashNext )
		if( Node->GetFName()==Find )
			return Cast<UFunction>( Node );
	return NULL;
}

TEST( Security, FieldChainStaysInItsStruct )
{
	UPackage* Package = UObject::CreatePackage( NULL, TEXT("SecurityChain") );
	UStruct* Other = new( Package, TEXT("Other") )UStruct( NULL );
	UIntProperty* Foreign = new( Other, TEXT("Foreign") )UIntProperty( EC_CppProperty, 0, TEXT(""), 0 );
	Foreign->Offset = 77;
	UStruct* Struct = new( Package, TEXT("Chained") )UStruct( NULL );
	UIntProperty* Kept = new( Struct, TEXT("Kept") )UIntProperty( EC_CppProperty, 0, TEXT(""), 0 );
	Kept->Next = Foreign;
	FArchive Ar;
	Struct->Link( Ar, 1 );
	EXPECT_FALSE( Kept->Next );
	EXPECT_EQ( 77, Foreign->Offset );
	EXPECT_TRUE( TakeRefusal( Package ) );
}

TEST( Security, PropertyWithoutUsableTypeIsDropped )
{
	UPackage* Package = UObject::CreatePackage( NULL, TEXT("SecurityTypes") );
	UStruct* Other = new( Package, TEXT("Other") )UStruct( NULL );
	UIntProperty* Element = new( Other, TEXT("Element") )UIntProperty( EC_CppProperty, 0, TEXT(""), 0 );
	Element->Offset = 77;

	UStruct* NoStruct = new( Package, TEXT("NoStruct") )UStruct( NULL );
	new( NoStruct, TEXT("Member") )UStructProperty( EC_CppProperty, 0, TEXT(""), 0, NULL );
	FArchive Ar;
	NoStruct->Link( Ar, 1 );
	EXPECT_FALSE( NoStruct->Children );
	EXPECT_TRUE( TakeRefusal( Package ) );

	// Linking the array would move the element another struct owns.
	UStruct* Borrowed = new( Package, TEXT("Borrowed") )UStruct( NULL );
	UArrayProperty* Array = new( Borrowed, TEXT("Array") )UArrayProperty( EC_CppProperty, 0, TEXT(""), 0 );
	Array->Inner = Element;
	Borrowed->Link( Ar, 1 );
	EXPECT_FALSE( Borrowed->Children );
	EXPECT_EQ( 77, Element->Offset );
	EXPECT_TRUE( TakeRefusal( Package ) );
}

TEST( Security, StructHoldingItselfHasASize )
{
	UPackage* Package = UObject::CreatePackage( NULL, TEXT("SecurityOwn") );
	FArchive Ar;

	// As a member it would copy without end, so it stands for the whole struct instead.
	UStruct* List = new( Package, TEXT("List") )UStruct( NULL );
	UStructProperty* Next = new( List, TEXT("Next") )UStructProperty( EC_CppProperty, 0, TEXT(""), 0, List );
	Next->ArrayDim = 4;
	new( List, TEXT("Value") )UIntProperty( EC_CppProperty, 0, TEXT(""), 0 );
	List->Link( Ar, 1 );
	EXPECT_EQ( 4, List->PropertiesSize );
	EXPECT_EQ( 4, Next->GetSize() );
	EXPECT_FALSE( FindField<UProperty>( List, TEXT("Next") ) );

	UStruct* Tree = new( Package, TEXT("Tree") )UStruct( NULL );
	UArrayProperty* Branches = new( Tree, TEXT("Branches") )UArrayProperty( EC_CppProperty, 0, TEXT(""), 0 );
	new( Branches, TEXT("Branches") )UStructProperty( EC_CppProperty, 0, TEXT(""), 0, Tree );
	new( Tree, TEXT("Leaf") )UIntProperty( EC_CppProperty, 0, TEXT(""), 0 );
	Tree->Link( Ar, 1 );
	EXPECT_EQ( Tree->PropertiesSize, Branches->Inner->ElementSize );
	EXPECT_FALSE( TakeRefusal( Package ) );
}

// Script and C++ find a function by name and pass what the one they know takes.
TEST( Security, FunctionMustFitWhatItHides )
{
	UPackage* Package = UObject::CreatePackage( NULL, TEXT("SecuritySignature") );

	// C++ calls Touch with one actor, a version with more would be copied past that.
	FScript Wide( TEXT("Touch"), ForgedClass( Package, TEXT("Wide"), AActor::StaticClass() ) );
	Wide.Ref( TEXT("Other"), CPF_Parm, AActor::StaticClass() );
	Wide.Int( TEXT("More"), CPF_Parm, 64 );
	Wide.Link();
	EXPECT_TRUE( Wide.Function->SignatureError() );
	Wide.Function->FunctionFlags |= FUNC_Operator;
	EXPECT_TRUE( Wide.Function->SignatureError() );

	// Another object class is handed to code that takes it for its own.
	FScript Typed( TEXT("Touch"), ForgedClass( Package, TEXT("Typed"), AActor::StaticClass() ) );
	Typed.Ref( TEXT("Other"), CPF_Parm, APawn::StaticClass() );
	Typed.Link();
	EXPECT_TRUE( Typed.Function->SignatureError() );

	FScript Same( TEXT("Touch"), ForgedClass( Package, TEXT("Same"), AActor::StaticClass() ) );
	Same.Ref( TEXT("Renamed"), CPF_Parm, AActor::StaticClass() );
	Same.Int( TEXT("Local"), 0 );
	Same.Link();
	EXPECT_FALSE( Same.Function->SignatureError() );

	// No C++ knows a downloaded package's own functions, script calls to them are held at run time.
	UClass* Base = ForgedClass( Package, TEXT("Base"), AActor::StaticClass() );
	FScript Old( TEXT("SecurityUpdate"), Base );
	Old.Int( TEXT("Value"), CPF_Parm );
	Old.Link();
	Base->Children = Old.Function;
	FArchive Ar;
	Base->Link( Ar, 1 );
	FScript Newer( TEXT("SecurityUpdate"), ForgedClass( Package, TEXT("Newer"), Base ) );
	Newer.Link();
	EXPECT_FALSE( Newer.Function->SignatureError() );

	// C++ calls a script class of its own packages by name, such as GUIComboBox.SetIndex.
	Base->SetFlags( RF_Native );
	EXPECT_TRUE( Newer.Function->SignatureError() );
	Base->ClearFlags( RF_Native );
}

// An override may drop out. One that adds it writes back to a caller who passed a value.
TEST( Security, OverrideMayOnlyDropOut )
{
	UClass* Class = ForgedClass( UObject::CreatePackage( NULL, TEXT("SecurityOut") ), TEXT("Forged"), AActor::StaticClass() );
	FScript Out( TEXT("SecurityOut"), Class ), Plain( TEXT("SecurityPlain"), Class );
	Out.Int( TEXT("Value"), CPF_Parm|CPF_OutParm );
	Plain.Int( TEXT("Value"), CPF_Parm );
	Out.Link();
	Plain.Link();
	EXPECT_TRUE( GSameParms( Plain.Function, Out.Function, 1 ) );
	EXPECT_FALSE( GSameParms( Out.Function, Plain.Function, 1 ) );
	EXPECT_FALSE( GSameParms( Plain.Function, Out.Function ) );
}

// Callers take the leading properties of a function for its parameters.
TEST( Security, FunctionParameterListIsChecked )
{
	UClass* Class = ForgedClass( UObject::CreatePackage( NULL, TEXT("SecurityParms") ), TEXT("Forged"), AActor::StaticClass() );

	FScript Late( TEXT("SecurityLateParm"), Class );
	Late.Int( TEXT("Local"), 0 );
	Late.Int( TEXT("Parm"), CPF_Parm );
	Late.Link();
	EXPECT_TRUE( Late.Function->SignatureError() );

	FScript Many( TEXT("SecurityManyParms"), Class );
	for( INT i=0; i<=MAX_FUNC_PARMS; i++ )
		Many.Int( *FString::Printf(TEXT("Parm%i"),i), CPF_Parm );
	Many.Link();
	EXPECT_TRUE( Many.Function->SignatureError() );

	FScript Mixed( TEXT("SecurityMixed"), Class );
	Mixed.Link();
	Mixed.Function->Children = new( Mixed.Function, TEXT("NoVariable") )UConst( NULL, TEXT("1") );
	EXPECT_TRUE( Mixed.Function->SignatureError() );

	// A replicated function's condition is run from its class's script.
	FScript Net( TEXT("SecurityNet"), Class );
	Net.Function->FunctionFlags = FUNC_Net;
	Net.Link();
	EXPECT_TRUE( Net.Function->SignatureError() );
}

// A native reads its arguments as its C++ declares them, whatever class redeclares it.
TEST( Security, RedeclaredNativeKeepsItsParameters )
{
	UClass* Class = ForgedClass( UObject::CreatePackage( NULL, TEXT("SecurityNative") ), TEXT("Forged"), AActor::StaticClass() );
	UFunction* Native = NULL;
	for( TFieldIterator<UFunction> It(AActor::StaticClass()); It && !Native; ++It )
		if( It->iNative>=256 && It->NumParms>0 && It->GetOwnerClass()==AActor::StaticClass() )
			Native = *It;
	ASSERT_TRUE( Native );
	FScript Decoy( TEXT("SecurityDecoy"), Class );
	Decoy.Function->FunctionFlags = FUNC_Native;
	Decoy.Function->iNative = Native->iNative;
	Decoy.Link();
	EXPECT_TRUE( Decoy.Function->SignatureError() );
}

TEST( Security, RefusedPackageRunsNoScript )
{
	UPackage* Package = UObject::CreatePackage( NULL, TEXT("SecurityRefused") );
	UClass* Class = ForgedClass( Package, TEXT("Forged"), AActor::StaticClass() );
	UFunction* Stock = Lookup( Class, TEXT("Touch") );
	FScript S( TEXT("Touch"), Class );
	S.Ref( TEXT("Other"), CPF_Parm, AActor::StaticClass() );
	S.Link();
	S.Return();
	Class->Children = S.Function;
	Class->Script.AddItem( EX_True );
	FArchive Ar;
	Class->Link( Ar, 1 );
	UClass* Child = ForgedClass( Package, TEXT("Child"), Class );
	ASSERT_TRUE( Lookup( Class, TEXT("Touch") )==S.Function );
	ASSERT_TRUE( Lookup( Child, TEXT("Touch") )==S.Function );

	GRefuseScript( S.Function, TEXT("is refused by a test") );
	TArray<UObject*> Loaded;
	GDisableRefusedScript( Loaded );
	EXPECT_TRUE( Lookup( Class, TEXT("Touch") )==Stock );
	EXPECT_TRUE( Lookup( Child, TEXT("Touch") )==Stock );
	EXPECT_TRUE( S.Function->Func==(Native)&UObject::execNothing );
	EXPECT_EQ( (INT)EX_False, (INT)Class->Script(0) );
	S.Function->Bind();
	EXPECT_TRUE( S.Function->Func==(Native)&UObject::execNothing );
	EXPECT_TRUE( TakeRefusal( Package ) );
}

// What only shows when a file is read: a reference of the wrong class, a native flag on a script variable,
// a within class that does not fit, a label table past the script and a state of another class.
TEST( Security, ForgedPackageIsRepairedOnLoad )
{
	UPackage* Package = UObject::CreatePackage( NULL, TEXT("SecurityForged") );
	UPackage* NotAStruct = UObject::CreatePackage( Package, TEXT("NotAStruct") );
	UStruct* Struct = new( Package, TEXT("Forged"), RF_Public|RF_Standalone )UStruct( NULL );
	new( Struct, TEXT("Kept") )UIntProperty( EC_CppProperty, 0, TEXT(""), 0 );
	new( Struct, TEXT("Text") )UStrProperty( EC_CppProperty, 0, TEXT(""), CPF_Native );
	new( Struct, TEXT("Wrong") )UStructProperty( EC_CppProperty, 0, TEXT(""), 0, (UStruct*)NotAStruct );

	UState* State = new( Package, TEXT("Labels"), RF_Public|RF_Standalone )UState( NULL );
	State->Script.AddItem( EX_Stop );
	State->LabelTableOffset = 500;

	UClass* Cheat = UObject::StaticLoadClass( UObject::StaticClass(), NULL, TEXT("Engine.CheatManager"), NULL, LOAD_NoWarn, NULL );
	ASSERT_TRUE( Cheat && Cheat->ClassWithin!=UObject::StaticClass() );
	UClass* Class = ForgedClass( Package, TEXT("Within"), Cheat );
	Class->ClassWithin = UObject::StaticClass();

	UClass* Plain = UObject::StaticLoadClass( UObject::StaticClass(), NULL, TEXT("Engine.LevelSummary"), NULL, LOAD_NoWarn, NULL );
	ASSERT_TRUE( Plain );
	UState* Elsewhere = NULL;
	for( TObjectIterator<UState> It; It && !Elsewhere; ++It )
		if( It->GetClass()==UState::StaticClass() && It->GetOuter()->IsA(UClass::StaticClass()) && !Plain->IsChildOf((UClass*)It->GetOuter()) )
			Elsewhere = *It;
	ASSERT_TRUE( Elsewhere );
	UObject* Object = UObject::StaticConstructObject( Plain, Package, TEXT("Stateful"), RF_Public|RF_Standalone );
	Object->InitExecution();
	Object->GetStateFrame()->Node = Object->GetStateFrame()->StateNode = Elsewhere;

	ASSERT_TRUE( UObject::SavePackage( Package, NULL, RF_Standalone, TEXT("SecurityForged.u"), GWarn ) );
	Object->GetStateFrame()->Node = Object->GetStateFrame()->StateNode = Plain;
	ASSERT_TRUE( UObject::LoadPackage( NULL, TEXT("SecurityForged.u"), LOAD_NoWarn ) );
	UObject::ResetLoaders( Package, 0, 1 );
	GFileManager->Delete( TEXT("SecurityForged.u") );

	Struct = FindObject<UStruct>( Package, TEXT("Forged") );
	ASSERT_TRUE( Struct );
	UProperty* Text = FindField<UProperty>( Struct, TEXT("Text") );
	ASSERT_TRUE( Text );
	EXPECT_FALSE( Text->PropertyFlags & CPF_Native );
	EXPECT_TRUE( Text->PropertyFlags & CPF_NeedCtorLink );
	EXPECT_FALSE( FindField<UProperty>( Struct, TEXT("Wrong") ) );
	EXPECT_TRUE( FindField<UProperty>( Struct, TEXT("Kept") ) );
	State = FindObject<UState>( Package, TEXT("Labels") );
	ASSERT_TRUE( State );
	EXPECT_EQ( (INT)MAXWORD, (INT)State->LabelTableOffset );
	Class = FindObject<UClass>( Package, TEXT("Within") );
	ASSERT_TRUE( Class );
	EXPECT_TRUE( Class->ClassWithin==Cheat->ClassWithin );
	Object = FindObject<UObject>( Package, TEXT("Stateful") );
	ASSERT_TRUE( Object && Object->GetStateFrame() );
	EXPECT_TRUE( Object->GetStateFrame()->Node==Plain );
	EXPECT_TRUE( Object->GetStateFrame()->StateNode==Plain );
	EXPECT_TRUE( TakeRefusal( Package ) );
}

// The export table and the struct body name the super apart, the linker checks only the table's.
TEST( Security, StructNamingTwoSupersIsRefused )
{
	UPackage* Package = UObject::CreatePackage( NULL, TEXT("SecuritySupers") );
	UStruct* First = new( Package, TEXT("First"), RF_Public|RF_Standalone )UStruct( NULL );
	new( Package, TEXT("Second"), RF_Public|RF_Standalone )UStruct( NULL );
	new( Package, TEXT("Child"), RF_Public|RF_Standalone )UStruct( First );
	ASSERT_TRUE( UObject::SavePackage( Package, NULL, RF_Standalone, TEXT("SecuritySupers.u"), GWarn ) );
	UObject::ResetLoaders( Package, 0, 1 );

	// Point the child's one byte SuperIndex at the struct that is neither its super nor itself.
	FArchive* Reader = GFileManager->CreateFileReader( TEXT("SecuritySupers.u") );
	ASSERT_TRUE( Reader );
	FPackageFileSummary Summary;
	*Reader << Summary;
	Reader->Seek( Summary.ExportOffset );
	INT At = 0, Super = 0, Child = 0;
	for( INT i=1; i<=Summary.ExportCount; i++ )
	{
		FObjectExport E;
		INT Name, Offset = 0;
		*Reader << AR_INDEX(E.ClassIndex);
		INT SuperAt = Reader->Tell();
		*Reader << AR_INDEX(E.SuperIndex) << E.PackageIndex << AR_INDEX(Name) << E.ObjectFlags << AR_INDEX(E.SerialSize);
		if( E.SerialSize )
			*Reader << AR_INDEX(Offset);
		if( E.SuperIndex )
			At = SuperAt, Super = E.SuperIndex, Child = i;
	}
	delete Reader;
	ASSERT_TRUE( At && Summary.ExportCount==3 );
	TArray<BYTE> Bytes;
	ASSERT_TRUE( appLoadFileToArray( Bytes, TEXT("SecuritySupers.u") ) );
	Bytes(At) = 6 - Super - Child;
	ASSERT_TRUE( appSaveArrayToFile( Bytes, TEXT("SecuritySupers.u") ) );
	EXPECT_TRUE( UObject::LoadPackage( NULL, TEXT("SecuritySupers.u"), LOAD_NoWarn ) );
	UObject::ResetLoaders( Package, 0, 1 );
	GFileManager->Delete( TEXT("SecuritySupers.u") );
	EXPECT_TRUE( TakeRefusal( Package ) );
}

static UStruct* CoreStruct( const TCHAR* Name )
{
	return FindObjectChecked<UStruct>( UObject::StaticClass(), Name );
}

static std::string ScriptRule( UStruct* Node )
{
	const TCHAR* Rule = GScriptStructureError( Node );
	return Rule ? TCHAR_TO_ANSI( Rule ) : "";
}

// A function with a local of each kind the rules look at, and a callee with an in and an out parameter.
struct FWalked : FScript
{
	FScript Callee;
	UProperty* In;
	UProperty* I;
	UProperty* Each;
	UDelegateProperty* Delegate;
	UProperty* B;
	UProperty* V;
	UProperty* P;
	UProperty* Strings;

	FWalked( const TCHAR* Name )
	:	FScript( Name )
	,	Callee( *(FString(Name)+TEXT("Callee")) )
	{
		In = Callee.Int( TEXT("In"), CPF_Parm );
		Callee.Int( TEXT("Out"), CPF_Parm|CPF_OutParm );
		Callee.Link();
		Callee.Return();
		I = Int( TEXT("I"), 0 );
		Each = Ref( TEXT("Each"), 0, AActor::StaticClass() );
		Delegate = Add( new( Function, TEXT("Delegate") )UDelegateProperty( EC_CppProperty, 0, TEXT(""), 0 ) );
		Delegate->Function = Callee.Function;
		B = Add( new( Function, TEXT("B") )UByteProperty( EC_CppProperty, 0, TEXT(""), 0 ) );
		V = Add( new( Function, TEXT("V") )UStructProperty( EC_CppProperty, 0, TEXT(""), 0, CoreStruct(TEXT("Vector")) ) );
		P = Add( new( Function, TEXT("P") )UStructProperty( EC_CppProperty, 0, TEXT(""), 0, CoreStruct(TEXT("Plane")) ) );
		Strings = Add( new( Function, TEXT("Strings") )UStrProperty( EC_CppProperty, 0, TEXT(""), 0 ) );
		Strings->ArrayDim = 2;
		Link();
	}
	void Patch( INT At, INT Offset )
	{
		Code(At) = Offset & 0xFF;
		Code(At+1) = Offset >> 8;
	}
	// if( Condition ) jumps over an int stored into a byte, which only matters where the jump is taken.
	void Guarded( void (*Condition)( FScript& ) )
	{
		Byte( EX_JumpIfNot );
		INT Skip = Code.Num();
		Word( 0 );
		Condition( *this );
		Byte( EX_Jump );
		INT End = Code.Num();
		Word( 0 );
		Patch( Skip, Code.Num() );
		Byte( EX_Let ).Local( B ).Byte( EX_IntOne );
		Patch( End, Code.Num() );
	}
	FScript& LetI()
	{
		return Byte( EX_Let ).Local( I );
	}
	FScript& Call()
	{
		return Byte( EX_FinalFunction ).Object( Callee.Function );
	}
	FScript& And( INT SkipSize )
	{
		return LetI().Byte( 130 ).Byte( EX_True ).Byte( EX_Skip ).Word( SkipSize ).Byte( EX_True ).Byte( EX_EndFunctionParms );
	}
	FScript& Foreach( INT EndAfter )
	{
		Byte( EX_Iterator ).AllActors( Each );
		INT End = Code.Num();
		return Word( End+EndAfter ).Byte( EX_IteratorNext ).Byte( EX_IteratorPop );
	}
};

// Each row is a script with one defect and the rule that names it. The passing rows are shapes retail mods use.
TEST( Security, BytecodeStructureIsChecked )
{
	struct FRow { const TCHAR* Rule; void (*Emit)( FWalked& ); } Rows[] =
	{
		{ TEXT(""), []( FWalked& S )
		{
			// foreach Each.Self.AllActors(), where None at either context skips the loop.
			S.Byte( EX_Iterator ).Byte( EX_Context ).Byte( EX_Context ).Local( S.Each );
			INT Inner = S.Skip( 0 );
			S.Byte( EX_Self );
			INT Outer = S.Skip( 0 );
			S.AllActors( S.Each );
			INT End = S.Code.Num();
			S.Word( End+3 ).Byte( EX_IteratorNext ).Byte( EX_IteratorPop );
			S.EndSkip( Inner );
			S.EndSkip( Outer );
		} },
		{ TEXT("uses a local of another function"), []( FWalked& S ){ S.Byte( EX_Let ).Local( S.In ).Byte( EX_IntOne ); } },
		{ TEXT("jumps into the middle of a statement"), []( FWalked& S ){ S.Byte( EX_Jump ).Word( 4 ); S.LetI().Byte( EX_IntOne ); } },
		{ TEXT("has a skip that misses the end of its call"), []( FWalked& S ){ S.And( 3 ); } },
		{ TEXT("has a skip for an argument that is not skipped"), []( FWalked& S ){ S.Call().Byte( EX_Skip ).Word( 2 ).Byte( EX_IntOne ).Byte( EX_EndFunctionParms ); } },
		{ TEXT("has a context skip that misses its expression"), []( FWalked& S ){ S.LetI().Byte( EX_Context ).Local( S.Each ).Word( 2 ).Byte( 0 ).Byte( EX_Self ); } },
		{ TEXT("calls an iterator outside foreach"), []( FWalked& S ){ S.AllActors( S.Each ); } },
		{ TEXT("has a foreach whose end is no iterator pop"), []( FWalked& S ){ S.Foreach( 2 ); } },
		{ TEXT("has a bool variable that names no bool"), []( FWalked& S ){ S.LetI().Byte( EX_BoolVariable ).Local( S.I ); } },
		{ TEXT(""), []( FWalked& S ){ S.LetI().Byte( EX_DynArrayLength ).Byte( EX_IntOne ); } },
		{ TEXT("assigns to a value that is no variable"), []( FWalked& S ){ S.Byte( EX_Let ).Byte( EX_ArrayElement ).Byte( EX_IntZero ).Byte( EX_IntOne ).Byte( EX_IntOne ); } },
		{ TEXT("resizes a value that is no variable"), []( FWalked& S ){ S.Byte( EX_DynArrayRemove ).Byte( EX_IntOne ).Byte( EX_IntZero ).Byte( EX_IntOne ); } },
		{ TEXT("has a native token that names a token"), []( FWalked& S ){ S.Byte( EX_ExtendedNative ).Byte( EX_Jump ).Byte( EX_Nothing ).Byte( EX_Nothing ).Byte( EX_EndFunctionParms ); } },
		{ TEXT("calls a delegate by another name"), []( FWalked& S ){ S.Byte( EX_DelegateFunction ).Object( S.Delegate ).Name( NAME_None ).Byte( EX_IntOne ).Local( S.I ).Byte( EX_EndFunctionParms ); } },
		{ TEXT("has debug info of an unknown version"), []( FWalked& S ){ S.Byte( EX_DebugInfo ).Int32( 99 ).Int32( 0 ).Int32( 0 ).Byte( 0 ); } },
		{ TEXT("passes more arguments than its function takes"), []( FWalked& S ){ S.Call().Byte( EX_IntOne ).Local( S.I ).Byte( EX_IntOne ).Byte( EX_EndFunctionParms ); } },
		{ TEXT("passes a value that is no variable as an out argument"), []( FWalked& S ){ S.Call().Byte( EX_IntOne ).Byte( EX_IntOne ).Byte( EX_EndFunctionParms ); } },
		{ TEXT("has an operand of the wrong kind"), []( FWalked& S ){ S.Byte( EX_Let ).Byte( EX_LocalVariable ).Object( S.Function ).Byte( EX_IntOne ); } },
		{ TEXT("has a struct member outside a struct"), []( FWalked& S ){ S.LetI().Byte( EX_StructMember ).Object( S.I ).Local( S.I ); } },
		{ TEXT("nests too deep"), []( FWalked& S ){ S.LetI(); for( INT i=0; i<1100; i++ ) S.Byte( EX_PrimitiveCast ).Byte( 0 ); S.Byte( EX_IntOne ); } },
		{ TEXT("assigns a value of another type"), []( FWalked& S ){ S.Byte( EX_Let ).Local( S.B ).Byte( EX_IntOne ); } },
		{ TEXT(""), []( FWalked& S ){ S.Guarded( []( FScript& C ){ C.Byte( EX_True ); } ); } },
		{ TEXT("assigns a value of another type"), []( FWalked& S ){ S.Guarded( []( FScript& C ){ C.Byte( EX_PrimitiveCast ).Byte( CST_BoolToInt ).Byte( EX_IntConst ).Int32( 2 ); } ); } },
		{ TEXT("assigns a value of another type"), []( FWalked& S ){ S.Byte( EX_Let ).Local( S.V ).Local( S.P ); } },
		{ TEXT("has a struct member on a foreign struct"), []( FWalked& S )
		{
			S.LetI().Byte( EX_StructMember ).Object( FindField<UProperty>( CoreStruct(TEXT("Plane")), TEXT("W") ) ).Local( S.V );
		} },
		{ TEXT("has a context fill larger than its value"), []( FWalked& S )
		{
			S.LetI().Byte( EX_Context ).Local( S.Each );
			INT Skip = S.Skip( 255 );
			S.Byte( EX_IntZero );
			S.EndSkip( Skip );
		} },
		{ TEXT("passes more arguments than its function takes"), []( FWalked& S )
		{
			S.Byte( EX_Context ).Local( S.Each );
			INT Skip = S.Skip( 0 );
			S.Byte( EX_VirtualFunction ).Name( FName(TEXT("Destroyed")) ).Byte( EX_IntOne ).Byte( EX_EndFunctionParms );
			S.EndSkip( Skip );
		} },
		{ TEXT("passes more arguments than its function takes"), []( FWalked& S )
		{
			S.Byte( EX_DelegateFunction ).Object( S.Delegate ).Name( S.Callee.Function->GetFName() ).Byte( EX_IntOne ).Local( S.I ).Byte( EX_IntOne ).Byte( EX_EndFunctionParms );
		} },
		{ TEXT(""), []( FWalked& S )
		{
			// A call through a context that passes an argument of another type is skipped when it runs.
			S.Byte( EX_Context ).Local( S.Each );
			INT Skip = S.Skip( 0 );
			S.Byte( EX_VirtualFunction ).Name( FName(TEXT("Touch")) ).Byte( EX_IntOne ).Byte( EX_EndFunctionParms );
			S.EndSkip( Skip );
		} },
		{ TEXT("calls a function its class lacks"), []( FWalked& S ){ S.Byte( EX_VirtualFunction ).Name( FName(TEXT("SecurityNoSuchFunction")) ).Byte( EX_EndFunctionParms ); } },
		{ TEXT("has a value that does not fit its buffer"), []( FWalked& S ){ S.Local( S.Strings ); } },
#if SERIAL_POINTER_INDEX
		{ TEXT("names an object that does not exist"), []( FWalked& S ){ S.Byte( EX_Let ).Byte( EX_LocalVariable ).Int32( MAXINT ).Byte( EX_IntOne ); } },
#endif
	};
	for( INT i=0; i<ARRAY_COUNT(Rows); i++ )
	{
		FWalked S( *FString::Printf( TEXT("SecurityWalk%i"), i ) );
		Rows[i].Emit( S );
		S.Return();
		EXPECT_EQ( TCHAR_TO_ANSI(Rows[i].Rule), ScriptRule( S.Function ) ) << i;
	}

	// Without its return the function runs on past its script.
	FWalked Open( TEXT("SecurityWalkOpen") );
	Open.LetI().Byte( EX_IntOne );
	EXPECT_EQ( "has code that runs past its end", ScriptRule( Open.Function ) );

	UPackage* Package = UObject::CreatePackage( NULL, TEXT("SecurityWalkNodes") );
	UState* State = new( Package, TEXT("Labels") )UState( NULL );
	State->Script.AddItem( EX_Stop );
	State->LabelTableOffset = 1;
	EXPECT_EQ( "has a label table that is not one", ScriptRule( State ) );

	UClass* Class = ForgedClass( Package, TEXT("Replicated"), AActor::StaticClass() );
	Class->Script.AddItem( EX_IntConst );
	Class->Script.AddZeroed( sizeof(INT) );
	UIntProperty* Rep = new( Class, TEXT("Rep") )UIntProperty( EC_CppProperty, 0, TEXT(""), CPF_Net );
	// Linking decodes each condition before the load check can run.
	Rep->RepOffset = 1;
	FArchive Ar;
	Class->Link( Ar, 1 );
	EXPECT_TRUE( TakeRefusal( Package ) );
}

// Loaded script passes and the walk leaves it as it was, the function digests are taken from it.
TEST( Security, LoadedBytecodePassesUnchanged )
{
	INT Walked = 0;
	for( TObjectIterator<UStruct> It; It; ++It )
	{
		if( !It->GetLinker() || !It->Script.Num() )
			continue;
		TArray<BYTE> Script = It->Script;
		EXPECT_EQ( "", ScriptRule( *It ) ) << TCHAR_TO_ANSI( It->GetFullName() );
		EXPECT_EQ( 0, appMemcmp( Script.GetTypedData(), It->Script.GetTypedData(), Script.Num() ) );
		Walked++;
	}
	EXPECT_GT( Walked, 1000 );
}

// A call by name was checked against the function its object's class declares. A subclass function of that name
// that takes other parameters is skipped, and a skipped object result is cleared as a whole.
TEST( Security, CallByNameKeepsItsParameters )
{
	UPackage* Package = UObject::CreatePackage( NULL, TEXT("SecurityByName") );
	UClass* Base = ForgedClass( Package, TEXT("Base"), UObject::StaticClass() );
	UClass* Other = ForgedClass( Package, TEXT("Other"), Base );
	FScript Declared( TEXT("SecurityFind"), Base ), Hiding( TEXT("SecurityFind"), Other );
	Declared.Int( TEXT("X"), CPF_Parm );
	Declared.Ref( TEXT("ReturnValue"), CPF_Parm|CPF_OutParm|CPF_ReturnParm, UObject::StaticClass() );
	Declared.Link();
	Declared.Return();
	Hiding.Int( TEXT("X"), CPF_Parm );
	Hiding.Int( TEXT("ReturnValue"), CPF_Parm|CPF_OutParm|CPF_ReturnParm );
	Hiding.Link();
	Hiding.Byte( EX_Return ).Byte( EX_IntConst ).Int32( 0x12345678 ).Byte( EX_Return ).Byte( EX_Nothing ).Byte( EX_EndOfScript );
	FArchive Ar;
	Base->Children = Declared.Function;
	Base->Link( Ar, 1 );
	Other->Children = Hiding.Function;
	Other->Link( Ar, 1 );

	FScript S( TEXT("SecurityCaller"), Base );
	UProperty* On = S.Ref( TEXT("On"), CPF_Parm, Base );
	UProperty* Through = S.Ref( TEXT("Through"), CPF_Parm, UObject::StaticClass() );
	UProperty* Direct = S.Ref( TEXT("Direct"), CPF_Parm, UObject::StaticClass() );
	S.Link();
	S.Byte( EX_Let ).Local( Through ).Byte( EX_Context ).Local( On );
	INT Skip = S.Skip( sizeof(INT) );
	S.Byte( EX_VirtualFunction ).Name( FName(TEXT("SecurityFind")) ).Byte( EX_IntOne ).Byte( EX_EndFunctionParms );
	S.EndSkip( Skip );
	S.Byte( EX_Let ).Local( Direct ).Byte( EX_VirtualFunction ).Name( FName(TEXT("SecurityFind")) ).Byte( EX_IntOne ).Byte( EX_EndFunctionParms );
	S.Return();
	ASSERT_EQ( "", ScriptRule( S.Function ) );

	UObject* Object = UObject::StaticConstructObject( Other, Package );
	ASSERT_EQ( Hiding.Function, Object->FindFunction( FName(TEXT("SecurityFind")) ) );
	BYTE Parms[64] = {};
	S.Value<UObject*>( Parms, On ) = Object;
	S.Value<UObject*>( Parms, Through ) = Object;
	S.Value<UObject*>( Parms, Direct ) = Object;
	S.Run( Parms, Object );
	EXPECT_FALSE( S.Value<UObject*>( Parms, Through ) );
	EXPECT_EQ( Object, S.Value<UObject*>( Parms, Direct ) );
}

// An element of a returned array is read from the returned value, the function's own array is gone by then.
TEST( Security, ElementOfAValueIsReadFromIt )
{
	FScript Make( TEXT("SecurityMakeArray") );
	UArrayProperty* Made = Make.IntArray( TEXT("ReturnValue") );
	Made->PropertyFlags |= CPF_Parm|CPF_OutParm|CPF_ReturnParm;
	UArrayProperty* Local = Make.IntArray( TEXT("Local") );
	Make.Link();
	Make.Byte( EX_Let ).Byte( EX_DynArrayLength ).Local( Local ).Byte( EX_IntConst ).Int32( 2 );
	Make.Byte( EX_Let ).Byte( EX_DynArrayElement ).Byte( EX_IntOne ).Local( Local ).Byte( EX_IntConst ).Int32( 7 );
	Make.Byte( EX_Return ).Local( Local ).Byte( EX_Return ).Byte( EX_Nothing ).Byte( EX_EndOfScript );

	FScript S( TEXT("SecurityReadElement") );
	UProperty* Read = S.Int( TEXT("Read"), CPF_Parm );
	S.Link();
	// An eaten dynamic array is freed as the array it is.
	S.Byte( EX_EatString ).Byte( EX_FinalFunction ).Object( Make.Function ).Byte( EX_EndFunctionParms );
	S.Byte( EX_Let ).Local( Read ).Byte( EX_DynArrayElement ).Byte( EX_IntOne ).Byte( EX_FinalFunction ).Object( Make.Function ).Byte( EX_EndFunctionParms );
	S.Return();
	ASSERT_EQ( "", ScriptRule( S.Function ) );
	EXPECT_TRUE( S.Function->ScriptFields.Find( GStatementKey(0) ) );
	BYTE Parms[64] = {};
	S.Run( Parms );
	EXPECT_EQ( 7, S.Value<INT>( Parms, Read ) );
}

// The stored fill of an element through a None context follows the 32-bit layout, the element is cleared as a whole.
TEST( Security, NoneContextClearsElementResult )
{
	FScript S( TEXT("SecurityNoneElement") );
	UProperty* Result = S.Ref( TEXT("Result"), CPF_Parm, AActor::StaticClass() );
	UProperty* None = S.Ref( TEXT("NoActor"), 0, AActor::StaticClass() );
	S.Link();
	S.Byte( EX_Let ).Local( Result ).Byte( EX_Context ).Local( None );
	INT Skip = S.Skip( 4 );
	S.Byte( EX_DynArrayElement ).Byte( EX_IntZero ).Byte( EX_InstanceVariable ).Object( FindField<UProperty>( AActor::StaticClass(), TEXT("Touching") ) );
	S.EndSkip( Skip );
	S.Return();
	ASSERT_EQ( "", ScriptRule( S.Function ) );
	BYTE Parms[64] = {};
	S.Value<UObject*>( Parms, Result ) = GTestEngine->GLevel->GetLevelInfo();
	S.Run( Parms );
	EXPECT_FALSE( S.Value<UObject*>( Parms, Result ) );
}

static void SaveAndLoad( UPackage* Package, const TCHAR* File )
{
	ASSERT_TRUE( UObject::SavePackage( Package, NULL, RF_Standalone, File, GWarn ) );
	ASSERT_TRUE( UObject::LoadPackage( NULL, File, LOAD_NoWarn ) );
	UObject::ResetLoaders( Package, 0, 1 );
	GFileManager->Delete( File );
}

// A defect in a saved file is refused when it loads, in a function and in an object's saved state code.
TEST( Security, ForgedBytecodeIsRefusedOnLoad )
{
	UClass* Plain = UObject::StaticLoadClass( UObject::StaticClass(), NULL, TEXT("Engine.LevelSummary"), NULL, LOAD_NoWarn, NULL );
	ASSERT_TRUE( Plain );

	UPackage* Package = UObject::CreatePackage( NULL, TEXT("SecurityBytecode") );
	UClass* Class = ForgedClass( Package, TEXT("Forged"), Plain );
	FScript Other( TEXT("Other"), Class );
	UProperty* Foreign = Other.Int( TEXT("Foreign"), 0 );
	Other.Link();
	Other.Return();
	FScript Thief( TEXT("Thief"), Class );
	Thief.Link();
	Thief.Byte( EX_Let ).Local( Foreign ).Byte( EX_IntOne );
	Thief.Return();
	Thief.Function->SetFlags( RF_Public|RF_Standalone );
	// A package that imports the refused one names its fields too.
	UPackage* User = UObject::CreatePackage( NULL, TEXT("SecurityUser") );
	ForgedClass( User, TEXT("User"), Class );
	ASSERT_TRUE( UObject::SavePackage( Package, NULL, RF_Standalone, TEXT("SecurityBytecode.u"), GWarn ) );
	ASSERT_TRUE( UObject::SavePackage( User, NULL, RF_Standalone, TEXT("SecurityUser.u"), GWarn ) );
	UObject::ResetLoaders( Package, 0, 1 );
	UObject::ResetLoaders( User, 0, 1 );
	EXPECT_TRUE( UObject::LoadPackage( NULL, TEXT("SecurityBytecode.u"), LOAD_NoWarn ) );
	EXPECT_TRUE( UObject::LoadPackage( NULL, TEXT("SecurityUser.u"), LOAD_NoWarn ) );
	UObject::ResetLoaders( User, 0, 1 );
	UObject::ResetLoaders( Package, 0, 1 );
	GFileManager->Delete( TEXT("SecurityUser.u") );
	GFileManager->Delete( TEXT("SecurityBytecode.u") );
	EXPECT_TRUE( TakeRefusal( Package ) );
	EXPECT_TRUE( User->Refused );
	User->Refused = 0;

	// An int evaluated into a byte writes past it.
	Package = UObject::CreatePackage( NULL, TEXT("SecurityNarrow") );
	Class = ForgedClass( Package, TEXT("Forged"), Plain );
	FScript Narrow( TEXT("Narrow"), Class );
	UProperty* Small = Narrow.Add( new( Narrow.Function, TEXT("Small") )UByteProperty( EC_CppProperty, 0, TEXT(""), 0 ) );
	Narrow.Link();
	Narrow.Byte( EX_Let ).Local( Small ).Byte( EX_IntConst ).Int32( 0x7FFFFFFF );
	Narrow.Return();
	Narrow.Function->SetFlags( RF_Public|RF_Standalone );
	SaveAndLoad( Package, TEXT("SecurityNarrow.u") );
	EXPECT_TRUE( TakeRefusal( Package ) );

	// Jump 3, Stop: offset 1 is inside the jump.
	Package = UObject::CreatePackage( NULL, TEXT("SecurityFrame") );
	Class = ForgedClass( Package, TEXT("Forged"), Plain );
	UState* State = new( Class, TEXT("Running") )UState( NULL );
	State->Script.AddItem( EX_Jump );
	State->Script.AddItem( 3 );
	State->Script.AddItem( 0 );
	State->Script.AddItem( EX_Stop );
	State->LabelTableOffset = MAXWORD;
	UObject* Object = UObject::StaticConstructObject( Class, Package, TEXT("Stateful"), RF_Public|RF_Standalone );
	Object->InitExecution();
	FStateFrame* Frame = Object->GetStateFrame();
	Frame->Node = Frame->StateNode = State;
	Frame->Code = &State->Script(1);
	SaveAndLoad( Package, TEXT("SecurityFrame.u") );

	Object = FindObject<UObject>( Package, TEXT("Stateful") );
	ASSERT_TRUE( Object && Object->GetStateFrame() );
	EXPECT_FALSE( Object->GetStateFrame()->Code );
	EXPECT_TRUE( TakeRefusal( Package ) );

	// A saved state naming another kind of object is left behind by editing tools, so it is only reset.
	Package = UObject::CreatePackage( NULL, TEXT("SecurityStale") );
	UObject* Stale = UObject::StaticConstructObject( UTextBuffer::StaticClass(), Package, TEXT("Stale"), RF_Public|RF_Standalone );
	Object = UObject::StaticConstructObject( UTextBuffer::StaticClass(), Package, TEXT("Stateful"), RF_Public|RF_Standalone );
	Object->InitExecution();
	Object->GetStateFrame()->StateNode = (UState*)Stale;
	SaveAndLoad( Package, TEXT("SecurityStale.u") );

	Object = FindObject<UObject>( Package, TEXT("Stateful") );
	ASSERT_TRUE( Object && Object->GetStateFrame() );
	EXPECT_EQ( Object->GetStateFrame()->StateNode, Object->GetClass() );
	EXPECT_FALSE( Object->GetStateFrame()->Code );
	EXPECT_FALSE( TakeRefusal( Package ) );
}

static UClass* LoadActorClass( const TCHAR* Name )
{
	return UObject::StaticLoadClass( AActor::StaticClass(), NULL, Name, NULL, LOAD_NoWarn, NULL );
}

// Spawning one would log it into the running game.
static APlayerController* NewRemotePlayer( APawn* Pawn )
{
	APlayerController* PC = ConstructObject<APlayerController>( APlayerController::StaticClass() );
	PC->PlayerReplicationInfo = ConstructObject<APlayerReplicationInfo>( APlayerReplicationInfo::StaticClass() );
	PC->XLevel = GTestEngine->GLevel;
	PC->Level = GTestEngine->GLevel->GetLevelInfo();
	if( Pawn )
	{
		PC->Pawn = Pawn;
		Pawn->Controller = PC;
	}
	return PC;
}

static APawn* SpawnPawn()
{
	ULevel* Level = GTestEngine->GLevel;
	FVector Start( 0, 0, 0 );
	for( INT i=0; i<Level->Actors.Num(); i++ )
		if( Level->Actors(i) && Level->Actors(i)->IsA( APlayerStart::StaticClass() ) )
		{
			Start = Level->Actors(i)->Location;
			break;
		}
	return (APawn*)Level->SpawnActor( LoadActorClass( TEXT("XGame.xPawn") ), NAME_None, Start, FRotator(0,0,0), NULL, 1 );
}

TEST( Security, RemoteCallsThatResetTheSpeedCheck )
{
	APawn* Pawn = SpawnPawn();
	ASSERT_TRUE( Pawn );
	APlayerController* PC = NewRemotePlayer( Pawn );

	// Running ahead of the server, as a speed hack does.
	const FLOAT Ahead = 0.5f;
	UFunction* Ack = PC->FindFunctionChecked( FName(TEXT("ServerAcknowledgePossession")) );
	BYTE Parms[64] = {};
	ASSERT_LE( (INT)Ack->ParmsSize, (INT)sizeof(Parms) );
	APawn* MarginPawn = NULL;
	auto Acknowledge = [&]( APawn* P )
	{
		PC->TimeMargin = Ahead;
		*(APawn**)Parms = P;
		ProcessClientCall( MarginPawn, PC, Ack, Parms );
	};
	Acknowledge( Pawn );
	EXPECT_LT( PC->TimeMargin, Ahead ) << "a new pawn must reset the speed check";
	Acknowledge( Pawn );
	EXPECT_EQ( Ahead, PC->TimeMargin ) << "a repeated acknowledgement reset the speed check";
	Acknowledge( NULL );
	EXPECT_EQ( NULL, PC->AcknowledgedPawn ) << "the call must still run";
	Acknowledge( Pawn );
	EXPECT_EQ( Ahead, PC->TimeMargin ) << "acknowledging None in between reset the speed check";

	UFunction* Short = PC->FindFunctionChecked( FName(TEXT("ServerShortTimeout")) );
	PC->bShortConnectTimeOut = 1;
	PC->TimeMargin = Ahead;
	ProcessClientCall( MarginPawn, PC, Short, NULL );
	EXPECT_EQ( Ahead, PC->TimeMargin ) << "a repeated short timeout reset the speed check";

	Pawn->Controller = NULL;
	GTestEngine->GLevel->DestroyActor( Pawn );
}

TEST( Security, BanInfoHidesAddressFromPlayers )
{
	UClass* Class = LoadActorClass( TEXT("XGame.xPlayer") );
	ASSERT_TRUE( Class );
	UFunction* Function = FindField<UFunction>( Class, TEXT("ClientReceiveBan") );
	ASSERT_TRUE( Function );
	const TCHAR Sep[] = { 27, 0 };
	FString Hash = FString(TEXT("3")) + Sep + TEXT("0123456789abcdef0123456789abcdef") + Sep;
	FString Parms = Hash + TEXT("192.0.2.7:7777");
	FilterRemoteCall( NULL, Function, (BYTE*)&Parms );
	EXPECT_STREQ( TCHAR_TO_ANSI( *(Hash + TEXT("0")) ), TCHAR_TO_ANSI( *Parms ) );
}

TEST( Security, ServerCapsAutonomousPhysics )
{
	APawn* Pawn = SpawnPawn();
	ASSERT_TRUE( Pawn );
	APlayerController* PC = NewRemotePlayer( Pawn );
	Pawn->SetCollision( 0, 0 );
	Pawn->bCollideWorld = 0;
	Pawn->setPhysics( PHYS_Flying );
	FVector Start = Pawn->Location;
	UFunction* Function = Pawn->FindFunctionChecked( FName(TEXT("AutonomousPhysics")) );
	auto Move = [&]( FLOAT Seconds )
	{
		GTestEngine->GLevel->FarMoveActor( Pawn, Start, 0, 1 );
		Pawn->Velocity = FVector( 0, 0, 200 );
		Pawn->Acceleration = FVector( 0, 0, 0 );
		// ProcessEvent skips numbered natives.
		// P_FINISH reads the byte after EX_EndFunctionParms.
		BYTE Code[7] = { EX_FloatConst };
		appMemcpy( Code+1, &Seconds, sizeof(FLOAT) );
		Code[5] = EX_EndFunctionParms;
		FFrame Stack( Pawn );
		Stack.Code = Code;
		(Pawn->*Function->Func)( Stack, NULL );
		return Pawn->Location;
	};
	FVector Capped = Move( PC->MaxResponseTime );
	FVector Forged = Move( 5.f );
	EXPECT_GT( (Capped - Start).Size(), 1.f );
	EXPECT_LT( (Forged - Capped).Size(), 0.01f ) << "a forged time stamp gap ran unbounded physics";

	Pawn->Controller = NULL;
	GTestEngine->GLevel->DestroyActor( Pawn );
}

TEST( Security, FileHashOnlyForLoadedPackages )
{
	ASecurity* Security = (ASecurity*)GTestEngine->GLevel->SpawnActor( ASecurity::StaticClass() );
	ASSERT_TRUE( Security );
	auto Hash = [&]( const FString& Path )
	{
		struct { INT SecType; FString Param1; FString Param2; TArray<FString> Results; } Parms;
		Parms.SecType = 2;
		Parms.Param1 = Path;
		Security->ProcessEvent( Security->FindFunctionChecked( FName(TEXT("LocalPerform")) ), &Parms );
		return Parms.Results.Num();
	};
	// Only a client is restricted.
	GIsClient = 1;
	EXPECT_EQ( 0, Hash( TEXT("Default.ini") ) );
	EXPECT_EQ( 1, Hash( TEXT("Elsewhere/Engine.u") ) );
	GIsClient = 0;
	GTestEngine->GLevel->DestroyActor( Security );
}

// All keyless clients share one ID.
TEST( Security, KeylessIdIsNeverBanned )
{
	// AccessControl has no C++ class.
	UObject* Access = *(UObject**)&GTestEngine->GLevel->GetLevelInfo()->Game->AccessControl;
	ASSERT_TRUE( Access && !appStricmp( Access->GetClass()->GetName(), TEXT("FixAccessControl") ) );
	UArrayProperty* Banned = FindField<UArrayProperty>( Access->GetClass(), TEXT("BannedIDs") );
	ASSERT_TRUE( Banned );
	TArray<FString>& IDs = *(TArray<FString>*)((BYTE*)Access + Banned->Offset);
	TArray<FString> Old = IDs;
	IDs.Empty();
	new(IDs)FString( TEXT("0aa1700a23b72d6d8cf32fc70dda27ed Keyless") );
	new(IDs)FString( TEXT("0123456789abcdef0123456789abcdef Cheater") );
	auto Check = [&]( const TCHAR* ID )
	{
		struct { FString CDHash; INT Result; } Parms;
		Parms.CDHash = ID;
		Parms.Result = -1;
		Access->ProcessEvent( Access->FindFunctionChecked( FName(TEXT("CheckID")) ), &Parms );
		return Parms.Result;
	};
	EXPECT_EQ( 0, Check( TEXT("0aa1700a23b72d6d8cf32fc70dda27ed") ) );
	EXPECT_EQ( 2, Check( TEXT("0123456789abcdef0123456789abcdef") ) );
	IDs = Old;
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
	SetNonBlocking( Socket );
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

// A triangle with one leaf, a two-plane collision hull and a light map, as the BSP build leaves them.
static UModel* ValidModel( UObject* Outer, FName Name=NAME_None )
{
	UModel* Model = ConstructObject<UModel>( UModel::StaticClass(), Outer, Name, RF_Public|RF_Standalone );
	for( INT i=0; i<3; i++ )
	{
		Model->Points.AddItem( FVector( i, i*i, 0 ) );
		Model->Vectors.AddItem( FVector( i==0, i==1, i==2 ) );
		Model->Verts(Model->Verts.AddZeroed()).pVertex = i;
	}
	FBspSurf& Surf = Model->Surfs(Model->Surfs.AddZeroed());
	Surf.vTextureU = 1;
	Surf.vTextureV = 2;
	FBspNode& Node = Model->Nodes(Model->Nodes.AddZeroed());
	Node.NumVertices = 3;
	Node.iBack = Node.iFront = Node.iPlane = Node.iRenderBound = Node.iLeaf[1] = INDEX_NONE;
	Node.iCollisionBound = 0;
	Node.iLightMap = 0;
	for( INT i=0; i<8; i++ )
		Model->LeafHulls.AddItem( i==0 ? 0x40000000 : i==1 ? INDEX_NONE : 0 );
	Model->Leaves.AddItem( FLeaf( 0, INDEX_NONE, INDEX_NONE, 1 ) );
	FLightMap* LightMap = new(Model->LightMaps) FLightMap( NULL, 0, 0 );
	LightMap->iTexture = LightMap->OffsetX = LightMap->OffsetY = 0;
	LightMap->SizeX = LightMap->SizeY = 16;
	new(LightMap->Bitmaps) FLightBitmap( NULL, 8, 0, 15, 15 );
	FLightMapTexture* Texture = new(Model->LightMapTextures) FLightMapTexture( NULL );
	Texture->LightMaps.AddItem( 0 );
	Texture->Revision = 1;
	return Model;
}

TEST( Security, LoadedModelIsChecked )
{
	UPackage* Package = UObject::CreatePackage( NULL, TEXT("SecurityModel") );
	EXPECT_FALSE( ValidModel( Package )->CheckLoaded() );
	void (*Edits[])( UModel* ) =
	{
		[]( UModel* M ){ M->Nodes(0).iSurf = 1; },
		[]( UModel* M ){ M->Nodes(0).NumVertices = 2; },
		[]( UModel* M ){ FBspNode Copy = M->Nodes(0); M->Nodes(M->Nodes.AddZeroed()) = Copy; M->Nodes(0).iBack = M->Nodes(0).iFront = 1; },
		[]( UModel* M ){ M->LeafHulls.Remove( 7 ); },
		[]( UModel* M ){ FBspNode Copy = M->Nodes(0); for( INT i=0; i<=FBspNode::MAX_DEPTH; i++ ){ INT Child = M->Nodes.AddItem( Copy ); M->Nodes(i).iFront = Child; } },
		[]( UModel* M ){ FBspNode Copy = M->Nodes(0); INT A = M->Nodes.AddItem( Copy ), B = M->Nodes.AddItem( Copy ); M->Nodes(A).iPlane = B; M->Nodes(B).iPlane = A; },
		[]( UModel* M ){ M->Lights.AddItem( GTestEngine->GLevel->GetLevelInfo() ); M->Leaves(0).iPermeating = 0; },
		[]( UModel* M ){ M->LightMaps(0).OffsetX = LIGHTMAP_TEXTURE_WIDTH-8; },
		[]( UModel* M ){ M->LightMaps(0).Bitmaps(0).Bits.Remove( 0 ); },
	};
	for( INT i=0; i<ARRAY_COUNT(Edits); i++ )
	{
		UModel* Model = ValidModel( Package );
		Edits[i]( Model );
		EXPECT_TRUE( Model->CheckLoaded() ) << "edit " << i;
	}
}

// The broken model is emptied, which the traces handle, and the package is refused.
TEST( Security, BrokenModelIsEmptiedOnLoad )
{
	UPackage* Package = UObject::CreatePackage( NULL, TEXT("SecurityModelLoad") );
	ValidModel( Package, TEXT("Model") );
	SaveAndLoad( Package, TEXT("SecurityModelLoad.u") );
	UModel* Model = FindObject<UModel>( Package, TEXT("Model") );
	ASSERT_TRUE( Model );
	EXPECT_EQ( 1, Model->Nodes.Num() );
	EXPECT_FALSE( TakeRefusal( Package ) );

	Model->Nodes(0).iSurf = 5;
	SaveAndLoad( Package, TEXT("SecurityModelLoad.u") );
	EXPECT_EQ( 0, Model->Nodes.Num() );
	EXPECT_EQ( 0, Model->NumZones );
	EXPECT_TRUE( TakeRefusal( Package ) );
	FCheckResult Hit;
	EXPECT_TRUE( Model->LineCheck( Hit, NULL, FVector(100,0,0), FVector(-100,0,0), FVector(0,0,0), 0, 0 ) );
	EXPECT_EQ( 0, Model->PointRegion( GTestEngine->GLevel->GetLevelInfo(), FVector(0,0,0) ).ZoneNumber );
}

// Vertices past the polygon's array are read and dropped, so the next polygon still loads.
TEST( Security, PolygonVerticesPastItsArray )
{
	UPackage* Package = UObject::CreatePackage( NULL, TEXT("SecurityPolys") );
	UPolys* Polys = ConstructObject<UPolys>( UPolys::StaticClass(), Package, TEXT("Polys"), RF_Public|RF_Standalone );
	for( INT i=0; i<2; i++ )
	{
		FPoly& Poly = Polys->Element(Polys->Element.AddZeroed());
		Poly.Init();
		Poly.NumVertices = i ? 3 : FPoly::MAX_VERTICES+4;
		Poly.ItemName = i ? FName(TEXT("Second")) : NAME_None;
	}
	SaveAndLoad( Package, TEXT("SecurityPolys.u") );
	ASSERT_EQ( 2, Polys->Element.Num() );
	EXPECT_EQ( 0, Polys->Element(0).NumVertices );
	EXPECT_EQ( 3, Polys->Element(1).NumVertices );
	EXPECT_EQ( FName(TEXT("Second")), Polys->Element(1).ItemName );
	EXPECT_TRUE( TakeRefusal( Package ) );
}

// One triangle in one section with its collision tree, as the static mesh build leaves it.
static UStaticMesh* ValidStaticMesh( UObject* Outer, FName Name=NAME_None )
{
	UStaticMesh* Mesh = ConstructObject<UStaticMesh>( UStaticMesh::StaticClass(), Outer, Name, RF_Public|RF_Standalone );
	Mesh->InternalVersion = 11;
	new(Mesh->Materials) FStaticMeshMaterial( NULL );
	FStaticMeshSection* Section = new(Mesh->Sections) FStaticMeshSection();
	Section->NumTriangles = Section->NumPrimitives = 1;
	Section->MinVertexIndex = 0;
	Section->MaxVertexIndex = 2;
	for( INT i=0; i<3; i++ )
	{
		FStaticMeshVertex Vertex;
		Vertex.Position = FVector( i*64, i==1 ? 64 : 0, 0 );
		Vertex.Normal = FVector( 0, 0, 1 );
		Mesh->VertexStream.Vertices.AddItem( Vertex );
		Mesh->IndexBuffer.Indices.AddItem( i );
	}
	FkDOPNode* Node = new(Mesh->kDOPTree.Nodes) FkDOPNode();
	Node->bIsLeaf = 1;
	Node->t.NumTriangles = 1;
	Node->t.StartIndex = 0;
	new(Mesh->kDOPTree.Triangles) FkDOPCollisionTriangle( 0, 1, 2 );
	return Mesh;
}

// One bone and one skinned vertex.
static USkeletalMesh* ValidSkeletalMesh( UObject* Outer )
{
	USkeletalMesh* Mesh = ConstructObject<USkeletalMesh>( USkeletalMesh::StaticClass(), Outer, NAME_None, RF_Public|RF_Standalone );
	Mesh->InternalVersion = 2;
	Mesh->RefSkeleton.AddZeroed();
	FStaticLODModel* LOD = new(Mesh->LODModels) FStaticLODModel();
	LOD->SmoothVerts.AddZeroed();
	LOD->SmoothStreamWedges = 1;
	DWORD Stream[] = { 0, 0, 0, 0xFFFFFFFF };
	for( INT i=0; i<ARRAY_COUNT(Stream); i++ )
		LOD->SkinningStream.AddItem( Stream[i] );
	return Mesh;
}

TEST( Security, LoadedMeshesAreChecked )
{
	UPackage* Package = UObject::CreatePackage( NULL, TEXT("SecurityMeshes") );
	EXPECT_FALSE( ValidStaticMesh( Package )->CheckLoaded() );
	void (*StaticEdits[])( UStaticMesh* ) =
	{
		[]( UStaticMesh* M ){ M->IndexBuffer.Indices(1) = 3; },
		[]( UStaticMesh* M ){ M->kDOPTree.Nodes(0).bIsLeaf = 0; },
	};
	for( INT i=0; i<ARRAY_COUNT(StaticEdits); i++ )
	{
		UStaticMesh* Mesh = ValidStaticMesh( Package );
		StaticEdits[i]( Mesh );
		EXPECT_TRUE( Mesh->CheckLoaded() ) << "static mesh edit " << i;
	}

	EXPECT_FALSE( ValidSkeletalMesh( Package )->CheckLoaded() );
	// A parent after its child is drawn wrong, as some converted meshes on live servers have it.
	USkeletalMesh* Late = ValidSkeletalMesh( Package );
	Late->RefSkeleton.AddZeroed( 2 );
	Late->RefSkeleton(1).ParentIndex = 2;
	EXPECT_FALSE( Late->CheckLoaded() );
	void (*SkeletalEdits[])( USkeletalMesh* ) =
	{
		[]( USkeletalMesh* M ){ M->LODModels(0).SkinningStream.Pop(); },
		[]( USkeletalMesh* M ){ M->LODModels(0).SkinningStream(0) = 6; },
		[]( USkeletalMesh* M ){ M->RefSkeleton(0).ParentIndex = 1; },
		[]( USkeletalMesh* M ){ M->RefSkeleton.AddZeroed( 2 ); M->RefSkeleton(1).ParentIndex = 2; M->RefSkeleton(2).ParentIndex = 1; },
	};
	for( INT i=0; i<ARRAY_COUNT(SkeletalEdits); i++ )
	{
		USkeletalMesh* Mesh = ValidSkeletalMesh( Package );
		SkeletalEdits[i]( Mesh );
		EXPECT_TRUE( Mesh->CheckLoaded() ) << "skeletal mesh edit " << i;
	}
}

TEST( Security, LoadedTexturesAndEmittersAreChecked )
{
	UPackage* Package = UObject::CreatePackage( NULL, TEXT("SecurityTextures") );
	UTexture* Texture = ConstructObject<UTexture>( UTexture::StaticClass(), Package );
	Texture->Init( 64, 64 );
	EXPECT_FALSE( Texture->CheckLoaded() );
	Texture->Mips(0).USize = 1<<20;
	EXPECT_TRUE( Texture->CheckLoaded() );

	UBeamEmitter* Beam = ConstructObject<UBeamEmitter>( UBeamEmitter::StaticClass(), Package );
	EXPECT_FALSE( Beam->CheckLoaded() );
	Beam->MaxParticles = MAXINT/256;
	EXPECT_TRUE( Beam->CheckLoaded() );

	UFireTexture* Fire = ConstructObject<UFireTexture>( UFireTexture::StaticClass(), Package );
	Fire->Init( 64, 64 );
	// Init leaves the sparks unset, only loaded ones are checked.
	Fire->Sparks.Empty();
	Fire->Sparks.AddZeroed();
	EXPECT_FALSE( Fire->CheckLoaded() );
	Fire->Sparks(0).X = 64;
	EXPECT_TRUE( Fire->CheckLoaded() );
	Fire->Sparks(0).X = 0;
	Fire->USize = 63;
	EXPECT_TRUE( Fire->UFractalTexture::CheckLoaded() );
	UWaveTexture* Water = ConstructObject<UWaveTexture>( UWaveTexture::StaticClass(), Package );
	Water->Init( 64, 64 );
	EXPECT_FALSE( Water->CheckLoaded() );
	Water->NumDrops = MaxDrops+1;
	EXPECT_TRUE( Water->CheckLoaded() );
}

// UV sets past the triangle's array are read and dropped when the lazy triangles load, the package still runs.
TEST( Security, StaticMeshTriangleUVsPastItsArray )
{
	UPackage* Package = UObject::CreatePackage( NULL, TEXT("SecurityTriangles") );
	UStaticMesh* Mesh = ValidStaticMesh( Package, TEXT("Mesh") );
	FStaticMeshTriangle* Triangle = new(Mesh->RawTriangles) FStaticMeshTriangle;
	appMemzero( Triangle, sizeof(*Triangle) );
	Triangle->NumUVs = ARRAY_COUNT(Triangle->UVs[0]) + 4;
	ASSERT_TRUE( UObject::SavePackage( Package, NULL, RF_Standalone, TEXT("SecurityTriangles.u"), GWarn ) );
	ASSERT_TRUE( UObject::LoadPackage( NULL, TEXT("SecurityTriangles.u"), LOAD_NoWarn ) );
	EXPECT_FALSE( TakeRefusal( Package ) );
	Mesh->RawTriangles.Load();
	ASSERT_EQ( 1, Mesh->RawTriangles.Num() );
	EXPECT_EQ( 0, Mesh->RawTriangles(0).NumUVs );
	EXPECT_FALSE( TakeRefusal( Package ) );
	UObject::ResetLoaders( Package, 0, 1 );
	GFileManager->Delete( TEXT("SecurityTriangles.u") );
}

TEST( Security, ArrayIndicesAreCheckedInRelease )
{
	struct FThrowingError : public FOutputDeviceError
	{
		void Serialize( const TCHAR* Msg, enum EName Event ) { throw 1; }
		void HandleError() {}
	} Thrower;
	FOutputDeviceError* OldError = GError;
	GError = &Thrower;

	TArray<INT> A;
	EXPECT_ANY_THROW( A.Last() );
	A.AddZeroed( 4 );
	EXPECT_ANY_THROW( A(-1) );
	EXPECT_ANY_THROW( A(4) );
	EXPECT_ANY_THROW( A.Insert( 5 ) );
	EXPECT_ANY_THROW( A.Remove( 0, -1 ) );
	EXPECT_ANY_THROW( A.Remove( 2, 3 ) );
	EXPECT_NO_THROW( A.Insert( 4 ) );
	EXPECT_NO_THROW( A.Remove( 5, 0 ) );
	EXPECT_EQ( A.Num(), 5 );

	FString S( TEXT("ab") );
	EXPECT_NO_THROW( S[S.Len()] );
	EXPECT_ANY_THROW( S[S.Len()+1] );

	GError = OldError;
}
