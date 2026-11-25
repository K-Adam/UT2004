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
		BYTE Code[6] = { EX_FloatConst };
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
