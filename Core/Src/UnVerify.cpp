/*=============================================================================
	UnVerify.cpp: Load-time check of bytecode structure and types.
=============================================================================*/

#include "CorePrivate.h"

enum EExprKind
{
	EK_Value,
	EK_Variable,	// The interpreter leaves its address in GPropAddr.
	EK_Length,		// A dynamic array's length, only assignment sets it.
};

// A value by the memory it is written into. Script types that share a layout share a kind.
enum EScriptKind
{
	TK_Any,			// Not known before the batch loads, or a missing import the interpreter cannot run.
	TK_None,		// Writes nothing.
	TK_Bad,			// A property script cannot hold a value of.
	TK_Byte,
	TK_Pod4,		// Int, float and bool.
	TK_Name,
	TK_Pointer,
	TK_Object,
	TK_Delegate,
	TK_String,
	TK_Struct,
	TK_Array,
};

struct FScriptType
{
	BYTE		Kind;
	INT			Dim;
	UStruct*	Struct;
	UProperty*	Inner;
	UClass*		Class;		// Only to find calls by name.
	UClass*		Meta;
};

static FScriptType ScriptType( BYTE Kind, UStruct* Struct=NULL, UClass* Class=NULL, UClass* Meta=NULL )
{
	FScriptType T = { Kind, 1, Struct, NULL, Class, Meta };
	return T;
}

static FScriptType TypeOf( UProperty* Property )
{
	if( !Property )
		return ScriptType( TK_Any );
	FScriptType T = ScriptType( TK_Bad );
	UClass* C = Property->GetClass();
	if( C==UByteProperty::StaticClass() )
		T.Kind = TK_Byte;
	else if( C==UIntProperty::StaticClass() || C==UFloatProperty::StaticClass() || C==UBoolProperty::StaticClass() )
		T.Kind = TK_Pod4;
	else if( C==UNameProperty::StaticClass() )
		T.Kind = TK_Name;
	else if( C==UPointerProperty::StaticClass() )
		T.Kind = TK_Pointer;
	else if( C==UObjectProperty::StaticClass() || C==UClassProperty::StaticClass() )
	{
		T.Kind = TK_Object;
		T.Class = ((UObjectProperty*)Property)->PropertyClass;
		if( C==UClassProperty::StaticClass() )
			T.Meta = ((UClassProperty*)Property)->MetaClass;
	}
	else if( C==UDelegateProperty::StaticClass() )
		T.Kind = TK_Delegate;
	else if( C==UStrProperty::StaticClass() )
		T.Kind = TK_String;
	else if( C==UStructProperty::StaticClass() )
	{
		T.Kind = TK_Struct;
		T.Struct = ((UStructProperty*)Property)->Struct;
	}
	else if( C==UArrayProperty::StaticClass() )
	{
		T.Kind = TK_Array;
		T.Inner = ((UArrayProperty*)Property)->Inner;
	}
	T.Dim = Property->ArrayDim;
	return T;
}

static INT HostSize( const FScriptType& T )
{
	INT Size = 0;
	switch( T.Kind )
	{
		case TK_Byte:		Size = sizeof(BYTE); break;
		case TK_Pod4:		Size = sizeof(INT); break;
		case TK_Name:		Size = sizeof(FName); break;
		case TK_Pointer:	Size = sizeof(void*); break;
		case TK_Object:		Size = sizeof(UObject*); break;
		case TK_Delegate:	Size = sizeof(FScriptDelegate); break;
		case TK_String:		Size = sizeof(FString); break;
		case TK_Array:		Size = sizeof(FArray); break;
		case TK_Struct:		Size = T.Struct ? T.Struct->PropertiesSize : 0; break;
	}
	return Size * T.Dim;
}

// Every slot is written by evaluating the value straight into it, at the value's size and layout.
static UBOOL Fits( const FScriptType& Value, const FScriptType& Slot )
{
	if( Value.Kind==TK_Any || Slot.Kind==TK_Any || Value.Kind==TK_None )
		return 1;
	if( Value.Kind!=Slot.Kind || Value.Kind==TK_Bad || Value.Dim!=Slot.Dim )
		return 0;
	// The compiler stores a child struct where its base is expected, and the copy is as large as the child.
	if( Value.Kind==TK_Struct )
		return Value.Struct==Slot.Struct || (Value.Struct && Slot.Struct && Value.Struct->IsChildOf(Slot.Struct) && Value.Struct->PropertiesSize<=Slot.Struct->PropertiesSize);
	// The compiler passes any dynamic array for another. An element is read at the stride of either.
	if( Value.Kind==TK_Array && Value.Inner && Slot.Inner )
	{
		FScriptType A = TypeOf( Value.Inner ), B = TypeOf( Slot.Inner );
		if( A.Kind==TK_Struct && B.Kind==TK_Struct )
			return A.Struct && B.Struct && A.Struct->PropertiesSize==B.Struct->PropertiesSize && (A.Struct->IsChildOf(B.Struct) || B.Struct->IsChildOf(A.Struct));
		return Fits( A, B ) && Fits( B, A );
	}
	return 1;
}

// Statement results and switch values go into fixed buffers of which only an FString's size is zeroed.
static UBOOL FitsBuffer( const FScriptType& T )
{
	if( T.Kind==TK_Any || T.Kind==TK_None )
		return 1;
	if( T.Kind==TK_Bad || HostSize(T)>MAX_STRING_CONST_SIZE )
		return 0;
	if( T.Kind==TK_String || T.Kind==TK_Array )
		return T.Dim==1;
	return T.Kind!=TK_Struct || !T.Struct || !T.Struct->ConstructorLink;
}

static const FScriptType IntType = ScriptType( TK_Pod4 );
static const FScriptType ObjectType = ScriptType( TK_Object );

static UStruct* CoreStruct( const TCHAR* Name )
{
	return FindObjectChecked<UStruct>( UObject::StaticClass(), Name );
}

struct FScriptExpr
{
	BYTE		Token;
	BYTE		Inner;		// The token under any contexts.
	BYTE		Kind;
	UBOOL		Iterator;	// A call that may run an iterator.
	UProperty*	Property;
	FScriptType	Type;
	UBOOL		Known;		// A constant the interpreter evaluates to Value.
	INT			Value;

	FScriptExpr( BYTE InKind )
	:	Token( EX_Nothing ), Inner( EX_Nothing ), Kind( EK_Value ), Iterator( 0 ), Property( NULL ), Type( ScriptType(InKind) ), Known( 0 ), Value( 0 )
	{}
};

class FScriptWalker
{
public:
	// Before its batch ends the objects the script names are not loaded, then only the grammar is checked.
	FScriptWalker( UStruct* InNode, UBOOL InLoaded )
	:	Node( InNode )
	,	Function( Cast<UFunction>(InNode) )
	,	Class( Cast<UClass>(InNode) )
	,	StateCode( Class ? NULL : Cast<UState>(InNode) )
	,	Code( InNode->Script.GetTypedData() )
	,	Size( InNode->Script.Num() )
	,	Pos( 0 )
	,	Depth( 0 )
	,	Error( NULL )
	,	Landings( NULL )
	,	Loaded( InLoaded )
	{
		Starts.AddZeroed( Size );
	}

	const TCHAR* Run()
	{
		if( Size>=GStatementKey(0) )
			return TEXT("has more script than ScriptFields keys cover");
		BYTE Last = EX_EndOfScript;
		while( Pos<Size && !Error )
		{
			Starts(Pos) = 1;
			Last = Code[Pos];
			Current = Pos;
			if( Class )
			{
				if( Last==EX_EndOfScript )
					Pos++;
				// A replication condition is evaluated into a bool.
				else if( !Fits(Expr().Type,IntType) )
					TypeFail( TEXT("has a condition that is no bool") );
			}
			else
			{
				Order.AddItem( Pos );
				Falls.AddItem( 1 );
				FirstEdge.AddItem( Edges.Num() );
				Statement();
			}
		}
		if( Error )
			return Error;

		// The interpreter runs on from any statement, so the last one must not fall through.
		UBOOL Ends = Last==EX_EndOfScript || Last==EX_Jump || Last==EX_LabelTable || (Function ? Last==EX_Return : Last==EX_Stop);
		if( Size && !Class && !(Function && (Function->FunctionFlags & FUNC_Native)) && !Ends )
			return TEXT("has code that runs past its end");

		// execJump, execJumpIfNot, execCase and GotoLabel continue at the offset they read.
		for( INT i=0; i<Targets.Num(); i++ )
			if( !IsStatement(Targets(i)) )
				return TEXT("jumps into the middle of a statement");

		// An iterator native continues after the end it read.
		for( INT i=0; i<IteratorEnds.Num(); i++ )
			if( !IsStatement(IteratorEnds(i)) || Code[IteratorEnds(i)]!=EX_IteratorPop )
				return TEXT("has a foreach whose end is no iterator pop");

		// execSwitch reads every case on the chain without looking at the token.
		for( INT i=0; i<Switches.Num(); i++ )
		{
			INT At = Switches(i), Steps = 0;
			for( ; ; )
			{
				if( !IsStatement(At) || Code[At]!=EX_Case || Steps++>Size )
					return TEXT("has a switch whose cases do not chain");
				_WORD Next;
				appMemcpy( &Next, Code+At+1, sizeof(_WORD) );
				if( Next==MAXWORD )
					break;
				// execSwitch evaluates each case into one buffer and compares it as the switch's value. A switch of
				// unknown type still needs cases of one type.
				FScriptType* Case = CaseTypes.Find( At );
				if( SwitchTypes(i).Kind==TK_Any && Case )
					SwitchTypes(i) = *Case;
				else if( Case && !Fits(*Case,SwitchTypes(i)) )
					TypeFail( TEXT("has a case of another type than its switch"), At );
				At = Next;
			}
		}

		// GotoLabel reads the table from this offset.
		if( StateCode && StateCode->LabelTableOffset!=MAXWORD && (!IsStatement(StateCode->LabelTableOffset-1) || Code[StateCode->LabelTableOffset-1]!=EX_LabelTable) )
			return TEXT("has a label table that is not one");

		// Replication runs a condition from its offset as one expression. A function keeps the offset of its root,
		// which may be declared in a state.
		if( Class && Loaded )
			for( TFieldIterator<UField> It(Class); It && It->GetOwnerClass()==Class; ++It )
			{
				UProperty* Property = Cast<UProperty>( *It );
				if( Property && (Property->PropertyFlags & CPF_Net) && !IsStatement(Property->RepOffset) )
					return TEXT("has a replication condition in the middle of an expression");
				UState* Scope = Cast<UState>( *It );
				for( UField* Field=Scope ? Scope->Children : *It; Field; Field=Scope ? Field->Next : NULL )
				{
					UFunction* Replicated = Cast<UFunction>( Field );
					if( Replicated && (Replicated->FunctionFlags & FUNC_Net) && !Replicated->GetSuperFunction() && !IsStatement(Replicated->RepOffset) )
						return TEXT("has a replication condition in the middle of an expression");
				}
			}

		MarkReached();
		for( TMap<INT,const TCHAR*>::TIterator It(TypeErrors); It; ++It )
			if( Reached(It.Key()) )
				return It.Value();
		return NULL;
	}

	TArray<BYTE>	Reached;

private:
	TArray<BYTE>	Starts;
	UStruct*		Node;
	UFunction*		Function;
	UClass*			Class;
	UState*			StateCode;
	BYTE*			Code;
	INT				Size;
	INT				Pos;
	INT				Depth;
	const TCHAR*	Error;
	TArray<INT>		Targets;
	TArray<INT>		IteratorEnds;
	TArray<INT>		Switches;
	TArray<INT>		Order;		// Statement starts.
	TArray<BYTE>	Falls;		// Whether each statement runs on into the next.
	TArray<INT>		Edges;		// Where each statement may jump to, in statement order.
	TArray<INT>		FirstEdge;
	TArray<INT>		Entries;	// Offsets a label starts at.
	INT				Current;
	TMap<INT,const TCHAR*>	TypeErrors;
	TArray<FScriptType>	SwitchTypes;
	TMap<INT,FScriptType>	CaseTypes;
	TArray<INT>*	Landings;
	UBOOL			Loaded;

	void Fail( const TCHAR* Rule )
	{
		if( !Error )
			Error = Rule;
		Pos = Size;
	}
	UBOOL IsStatement( INT Offset )
	{
		return Offset>=0 && Offset<Size && Starts(Offset);
	}
	// A value of another type than its slot, which only matters in code that runs.
	void TypeFail( const TCHAR* Rule, INT At=INDEX_NONE )
	{
		if( At==INDEX_NONE )
			At = Current;
		if( Class )
			Fail( Rule );
		else if( !TypeErrors.Find(At) )
			TypeErrors.Set( At, Rule );
	}
	// Obfuscators put code of any type where no jump leads. Only what runs has to fit its slots.
	void MarkReached()
	{
		if( Class )
		{
			Reached = Starts;
			return;
		}
		Reached.AddZeroed( Size );
		TArray<INT> Index, Work;
		Index.Add( Size );
		for( INT i=0; i<Order.Num(); i++ )
			Index(Order(i)) = i;
		if( Order.Num() )
			Work.AddItem( 0 );
		for( INT i=0; i<Entries.Num(); i++ )
			Work.AddItem( Index(Entries(i)) );
		while( Work.Num() )
		{
			INT At = Work.Pop();
			if( Reached(Order(At)) )
				continue;
			Reached(Order(At)) = 1;
			if( Falls(At) && At+1<Order.Num() )
				Work.AddItem( At+1 );
			for( INT i=FirstEdge(At); i<(At+1<Order.Num() ? FirstEdge(At+1) : Edges.Num()); i++ )
				Work.AddItem( Index(Edges(i)) );
		}
	}
	void Ends()
	{
		Falls(Falls.Num()-1) = 0;
	}
	void Jump( INT To )
	{
		Targets.AddItem( To );
		Edges.AddItem( To );
	}
	// The class a member is looked up in: the static class of its context's object, or the script's own.
	UClass* ScopeClass( const FScriptType* Scope )
	{
		return Scope ? (Scope->Kind==TK_Object ? Scope->Class : NULL) : Node->GetOwnerClass();
	}
	UBOOL Has( INT Count )
	{
		if( !Error && Pos+Count<=Size )
			return 1;
		Fail( TEXT("has an instruction that runs past its script") );
		return 0;
	}
	BYTE Peek()
	{
		return Has(1) ? Code[Pos] : (BYTE)EX_EndOfScript;
	}
	BYTE Byte()
	{
		return Has(1) ? Code[Pos++] : 0;
	}
	_WORD Word()
	{
		_WORD Value = 0;
		if( Has(sizeof(_WORD)) )
		{
			appMemcpy( &Value, Code+Pos, sizeof(_WORD) );
			Pos += sizeof(_WORD);
		}
		return Value;
	}
	INT Int()
	{
		INT Value = 0;
		if( Has(sizeof(INT)) )
		{
			appMemcpy( &Value, Code+Pos, sizeof(INT) );
			Pos += sizeof(INT);
		}
		return Value;
	}
	void Skip( INT Count )
	{
		if( Has(Count) )
			Pos += Count;
	}
	// Four bytes on every platform, as FFrame::ReadObject reads them.
	UObject* Object()
	{
		if( !Has(sizeof(INT)) )
			return NULL;
#if SERIAL_POINTER_INDEX
		INT Index = Int();
		if( Index<0 || Index>=GTotalSerializedPointers || GSerializedPointers[Index]==FREE_SERIALIZED_POINTER )
		{
			Fail( TEXT("names an object that does not exist") );
			return NULL;
		}
		return Loaded ? (UObject*)GSerializedPointers[Index] : NULL;
#else
		UObject* Value;
		appMemcpy( &Value, Code+Pos, sizeof(Value) );
		Pos += sizeof(Value);
		return Loaded ? Value : NULL;
#endif
	}
	// A missing import loads as NULL. The interpreter only reads through it, which ends the game.
	UObject* Operand( UClass* Kind )
	{
		UObject* Value = Object();
		if( Value && !Value->IsA(Kind) )
			Fail( TEXT("has an operand of the wrong kind") );
		return Error ? NULL : Value;
	}
	// execDebugInfo rewinds over any other version and runs it again.
	void DebugInfo()
	{
		if( Int()!=100 )
			Fail( TEXT("has debug info of an unknown version") );
		Skip( 2*sizeof(INT)+1 );
	}
	enum { DEBUG_INFO_SIZE = 2+3*sizeof(INT) };
	UBOOL IsDebugInfo( INT At )
	{
		INT Version = 0;
		if( At+1+(INT)sizeof(INT)<=Size && Code[At]==EX_DebugInfo )
			appMemcpy( &Version, Code+At+1, sizeof(INT) );
		return Version==100;
	}
	// After a call or a return the interpreter only peeks, another version rewinds to where it was.
	void OptionalDebugInfo()
	{
		if( !Error && IsDebugInfo(Pos) )
		{
			Pos++;
			DebugInfo();
		}
	}
	UBOOL IsLocal( UProperty* Property )
	{
		for( UField* Field=Function ? Function->Children : NULL; Field; Field=Field->Next )
			if( Field==Property )
				return 1;
		return 0;
	}
	// The function a call by name finds where its code was compiled: the state, then the class. Behind a context
	// the compiler found it in the static class of the object.
	UFunction* FindByName( FName Name, UBOOL Global, UClass* Scope )
	{
		if( !Loaded )
			return NULL;
		UState* State = Cast<UState>( Function ? Function->GetOuter() : Node );
		if( Global || Scope || (State && State->IsA(UClass::StaticClass())) )
			State = NULL;
		UStruct* Scopes[2] = { State, Scope ? Scope : Node->GetOwnerClass() };
		for( INT i=0; i<2; i++ )
			for( TFieldIterator<UFunction> It(Scopes[i]); It; ++It )
				if( It->GetFName()==Name )
					return *It;
		return NULL;
	}
	void Need( const FScriptType& Value, const FScriptType& Slot, const TCHAR* Rule )
	{
		if( !Fits(Value,Slot) )
			TypeFail( Rule );
	}

	// The arguments of a call as CallFunction and the natives read them. Fixed is set where the function is decided
	// here, by the file or by the run time holding a call by name to the function found for it. With Misfit an
	// argument of another type only sets it.
	void Call( FScriptExpr& E, UFunction* F, UBOOL Fixed, UBOOL Iterator, UBOOL* Misfit=NULL )
	{
		// A numbered native reads its arguments as the function that registered it declares them.
		UFunction* Native = F && F->iNative ? GNativeFunction( F->iNative ) : NULL;
		UFunction* Reads = Native ? Native : F;
		E.Iterator = !Reads || (Reads->FunctionFlags & FUNC_Iterator);
		// PRE_ITERATOR reads the loop's end after the arguments.
		if( Reads && (Reads->FunctionFlags & FUNC_Iterator) && !Iterator )
			Fail( TEXT("calls an iterator outside foreach") );
		UField* Field = Reads ? Reads->Children : NULL;
		UBOOL Skipped = 0;
		FScriptType Args[2] = { ScriptType(TK_Any), ScriptType(TK_Any) };
		for( INT Count=0; !Error && Peek()!=EX_EndFunctionParms; Count++ )
		{
			UProperty* Parm = Cast<UProperty>( Field );
			if( Parm && (Parm->PropertyFlags & (CPF_Parm|CPF_ReturnParm))!=CPF_Parm )
				Parm = NULL;
			// IsParm ends the game on an argument past the parameters.
			if( Reads && Fixed && !Parm )
			{
				Fail( TEXT("passes more arguments than its function takes") );
				return;
			}
			FScriptExpr Argument( TK_None );
			if( Peek()==EX_Skip )
			{
				// The skipping native jumps past the operand and the end of the call.
				if( Parm && !(Parm->PropertyFlags & CPF_SkipParm) )
					Fail( TEXT("has a skip for an argument that is not skipped") );
				Skipped = 1;
				Pos++;
				INT Length = Word();
				INT From = Pos;
				Argument = Expr();
				if( From+Length!=Pos+1 || Peek()!=EX_EndFunctionParms )
					Fail( TEXT("has a skip that misses the end of its call") );
			}
			else if( Parm && (Parm->PropertyFlags & CPF_SkipParm) )
				Fail( TEXT("has a skipped argument without a skip") );
			else if( Peek()==EX_Nothing )
				Pos++;
			else
			{
				// An out argument is copied back to the address its evaluation left in GPropAddr.
				Argument = Expr();
				if( Parm && (Parm->PropertyFlags & CPF_OutParm) && Argument.Kind!=EK_Variable )
					Fail( TEXT("passes a value that is no variable as an out argument") );
			}
			// A native writes an out argument through as its parameter's type.
			if( Parm && Misfit && !Fits(Argument.Type,TypeOf(Parm)) )
				*Misfit = 1;
			else if( Parm )
				Need( Argument.Type, TypeOf(Parm), TEXT("passes an argument of another type") );
			if( Count<2 )
				Args[Count] = Argument.Type;
			Field = Field ? Field->Next : NULL;
		}
		Byte();
		// The skipping natives step past the end of the call and no further.
		if( !Skipped )
			OptionalDebugInfo();

		UProperty* Return = Reads ? Reads->GetReturnProperty() : NULL;
		E.Type = !Reads ? ScriptType(TK_Any) : Return ? TypeOf(Return) : ScriptType(TK_None);
		// The compiler gives these the class their class argument names, by the function's name.
		static FName CreateDataObject( TEXT("CreateDataObject") ), LoadDataObject( TEXT("LoadDataObject") );
		INT ClassArg = !Reads ? -1 : Reads->GetFName()==NAME_Spawn ? 0 : (Reads->GetFName()==CreateDataObject || Reads->GetFName()==LoadDataObject) ? 1 : -1;
		if( E.Type.Kind==TK_Object && ClassArg>=0 && Args[ClassArg].Meta )
			E.Type.Class = Args[ClassArg].Meta;
	}

	// The source and result of each cast, as its native reads and writes them.
	static FScriptType CastType( TCHAR Kind )
	{
		switch( Kind )
		{
			case 'b': return ScriptType( TK_Byte );
			case 'i': return ScriptType( TK_Pod4 );
			case 'n': return ScriptType( TK_Name );
			case 'o': return ScriptType( TK_Object );
			case 's': return ScriptType( TK_String );
			case 'v': return ScriptType( TK_Struct, CoreStruct(TEXT("Vector")) );
			case 'r': return ScriptType( TK_Struct, CoreStruct(TEXT("Rotator")) );
		}
		return ScriptType( TK_Bad );
	}
	void PrimitiveCast( FScriptExpr& E )
	{
		static const struct { BYTE Code; TCHAR From, To; } Casts[] =
		{
			{ CST_RotatorToVector, 'r', 'v' },
			{ CST_ByteToInt, 'b', 'i' }, { CST_ByteToBool, 'b', 'i' }, { CST_ByteToFloat, 'b', 'i' }, { CST_ByteToString, 'b', 's' },
			{ CST_IntToByte, 'i', 'b' }, { CST_IntToBool, 'i', 'i' }, { CST_IntToFloat, 'i', 'i' }, { CST_IntToString, 'i', 's' },
			{ CST_BoolToByte, 'i', 'b' }, { CST_BoolToInt, 'i', 'i' }, { CST_BoolToFloat, 'i', 'i' }, { CST_BoolToString, 'i', 's' },
			{ CST_FloatToByte, 'i', 'b' }, { CST_FloatToInt, 'i', 'i' }, { CST_FloatToBool, 'i', 'i' }, { CST_FloatToString, 'i', 's' },
			{ CST_ObjectToBool, 'o', 'i' }, { CST_ObjectToString, 'o', 's' },
			{ CST_NameToBool, 'n', 'i' }, { CST_NameToString, 'n', 's' },
			{ CST_StringToByte, 's', 'b' }, { CST_StringToInt, 's', 'i' }, { CST_StringToBool, 's', 'i' }, { CST_StringToFloat, 's', 'i' },
			{ CST_StringToVector, 's', 'v' }, { CST_StringToRotator, 's', 'r' },
			{ CST_VectorToBool, 'v', 'i' }, { CST_VectorToRotator, 'v', 'r' }, { CST_VectorToString, 'v', 's' },
			{ CST_RotatorToBool, 'r', 'i' }, { CST_RotatorToString, 'r', 's' },
		};
		BYTE Code = Byte();
		FScriptExpr Operand = Expr();
		for( INT i=0; i<ARRAY_COUNT(Casts); i++ )
			if( Casts[i].Code==Code )
			{
				Need( Operand.Type, CastType(Casts[i].From), TEXT("casts a value of another type") );
				E.Type = CastType( Casts[i].To );
				// Obfuscated code hides its jumps behind casts of constants. Each folds as its native computes it.
				E.Known = Operand.Known;
				switch( Code )
				{
					case CST_ByteToInt:		E.Value = Operand.Value; break;
					case CST_ByteToBool:
					case CST_IntToBool:		E.Value = Operand.Value!=0; break;
					case CST_IntToByte:		E.Value = Operand.Value & 0xFF; break;
					case CST_BoolToByte:
					case CST_BoolToInt:		E.Value = Operand.Value & 1; break;
					default:				E.Known = 0; break;
				}
				return;
			}
		TypeFail( TEXT("has a cast of an unknown kind") );
	}

	// Under foreach the contexts leading to the iterator may skip the loop, and only the last member may call it.
	// Scope is the type of the object an expression is evaluated on, Member is set for a context's member itself.
	enum EForeach { FE_None, FE_Chain, FE_Call };
	FScriptExpr Expr( INT Foreach=FE_None, const FScriptType* Scope=NULL, UBOOL Member=0 )
	{
		FScriptExpr E( TK_Any );
		if( ++Depth>1024 )
			Fail( TEXT("nests too deep") );
		E.Token = E.Inner = Byte();
		if( Error )
		{
			Depth--;
			return E;
		}
		if( E.Token>=EX_ExtendedNative )
		{
			INT iNative = E.Token>=EX_FirstNative ? E.Token : (E.Token-EX_ExtendedNative)*0x100 + Byte();
			// execHighNative runs a number below 0x100 as that token.
			if( E.Token<EX_FirstNative && iNative<0x100 )
				Fail( TEXT("has a native token that names a token") );
			Call( E, GNativeFunction(iNative), 1, Foreach==FE_Call );
			Depth--;
			return E;
		}
		switch( E.Token )
		{
			case EX_LocalVariable:
			case EX_NativeParm:
			{
				// Both read the frame's locals at the property's offset. execNativeParm sets GPropAddr only when it reads a value.
				E.Property = (UProperty*)Operand( UProperty::StaticClass() );
				if( E.Property && !IsLocal(E.Property) )
					Fail( TEXT("uses a local of another function") );
				E.Kind = E.Token==EX_LocalVariable ? EK_Variable : EK_Value;
				E.Type = TypeOf( E.Property );
				break;
			}
			case EX_InstanceVariable:
			case EX_DefaultVariable:
			{
				E.Property = (UProperty*)Operand( UProperty::StaticClass() );
				E.Kind = EK_Variable;
				E.Type = TypeOf( E.Property );
				// The compiler types an object's Outer by its class's within and its Class by its class.
				UClass* Of = ScopeClass( Scope );
				if( Of && E.Property && E.Property->GetOuter()==UObject::StaticClass() )
				{
					if( E.Property->GetFName()==NAME_Outer )
						E.Type.Class = Of->ClassWithin;
					else if( E.Property->GetFName()==NAME_Class )
						E.Type.Meta = Of;
				}
				break;
			}
			case EX_BoolVariable:
			{
				// execBoolVariable reads the property right behind the next token and applies its bit mask.
				BYTE Next = Peek();
				if( Next!=EX_LocalVariable && Next!=EX_InstanceVariable && Next!=EX_DefaultVariable && Next!=EX_StructMember )
					Fail( TEXT("has a bool variable that names no variable") );
				FScriptExpr Variable = Expr( FE_None, Scope );
				if( Variable.Property && !Variable.Property->IsA(UBoolProperty::StaticClass()) )
					Fail( TEXT("has a bool variable that names no bool") );
				E.Property = Variable.Property;
				E.Kind = Variable.Kind;
				E.Type = IntType;
				break;
			}
			case EX_StructMember:
			{
				// execStructMember sizes its buffer by the property's outer as a struct and the structs that extend it.
				E.Property = (UProperty*)Operand( UProperty::StaticClass() );
				UStruct* Outer = E.Property ? (UStruct*)E.Property->GetOuter() : NULL;
				if( Outer && Outer->GetClass()!=UStruct::StaticClass() )
					Fail( TEXT("has a struct member outside a struct") );
				FScriptExpr Base = Expr( FE_None, Scope );
				if( Outer && Base.Type.Kind!=TK_Any && !(Base.Type.Kind==TK_Struct && Base.Type.Dim==1 && Base.Type.Struct && Base.Type.Struct->IsChildOf(Outer)) )
					TypeFail( TEXT("has a struct member on a foreign struct") );
				E.Kind = Base.Kind==EK_Variable ? EK_Variable : EK_Value;
				E.Type = TypeOf( E.Property );
				break;
			}
			case EX_ArrayElement:
			case EX_DynArrayElement:
			{
				INT At = Pos-1;
				// The base is evaluated for its address alone, which only a variable leaves to write through. A second
				// static index would apply the same bound again past the first.
				Need( Expr().Type, IntType, TEXT("has an index that is no int") );
				FScriptExpr Base = Expr( FE_None, Scope );
				if( E.Token==EX_ArrayElement && Base.Inner==EX_ArrayElement )
					Fail( TEXT("indexes an array element") );
				E.Property = Base.Property;
				E.Kind = Base.Kind==EK_Variable ? EK_Variable : EK_Value;
				// The array opcodes treat any other variable like None (DropNonArray).
				if( E.Token==EX_ArrayElement )
				{
					E.Type = Base.Type;
					E.Type.Dim = 1;
				}
				else
					E.Type = Base.Type.Kind==TK_Array ? TypeOf( Base.Type.Inner ) : ScriptType( TK_None );
				// A base that is no variable leaves the address of whatever its call evaluated last. The interpreter
				// evaluates and indexes such a base at the type found here.
				if( Base.Kind!=EK_Variable && Base.Type.Kind!=TK_Any && Loaded )
				{
					UProperty* Value = E.Token==EX_ArrayElement ? (Base.Property && Base.Property->ArrayDim>1 ? Base.Property : NULL) : (Base.Type.Kind==TK_Array && Base.Type.Dim==1 ? Base.Type.Inner : NULL);
					if( Value )
						Node->ScriptFields.Set( At, Value );
					else
						TypeFail( TEXT("indexes a value that is no array") );
				}
				break;
			}
			case EX_DynArrayLength:
			case EX_DynArrayInsert:
			case EX_DynArrayRemove:
			{
				UBOOL Variable = Expr( FE_None, Scope ).Kind==EK_Variable;
				if( E.Token==EX_DynArrayLength )
				{
					E.Kind = Variable ? EK_Length : EK_Value;
					E.Type = IntType;
				}
				else
				{
					if( !Variable )
						Fail( TEXT("resizes a value that is no variable") );
					Need( Expr().Type, IntType, TEXT("has an index that is no int") );
					Need( Expr().Type, IntType, TEXT("has a count that is no int") );
					E.Type = ScriptType( TK_None );
				}
				break;
			}
			case EX_Context:
			case EX_ClassContext:
			{
				FScriptExpr Target = Expr( Foreach ? FE_Chain : FE_None, E.Token==EX_Context ? Scope : NULL );
				Need( Target.Type, ObjectType, TEXT("has a context that is no object") );
				INT Length = Word();
				INT Fill = Byte();
				INT From = Pos;
				// A class context runs on the default object of the class.
				FScriptType On = Target.Type;
				if( E.Token==EX_ClassContext )
					On = Target.Type.Kind==TK_Object ? ScriptType( TK_Object, NULL, Target.Type.Meta ) : ScriptType( TK_Any );
				FScriptExpr Member = Expr( Foreach, &On, 1 );
				// SkipNullContext clears an element at the element's size, which only this check knows.
				if( Loaded && (Member.Token==EX_ArrayElement || Member.Token==EX_DynArrayElement) && Member.Property )
				{
					UArrayProperty* Array = Cast<UArrayProperty>( Member.Property );
					UProperty* Element = Member.Token==EX_ArrayElement ? Member.Property : Array ? Array->Inner : NULL;
					if( Element )
						Node->ScriptFields.Set( GContextElementKey(From), Element );
				}
				// SkipNullContext clears this much of a result sized for the member's value, unless the member names the
				// field it takes the size from. Of a nested context it cannot tell the value's size.
				UBOOL Named = Member.Token>=EX_ExtendedNative || Member.Token==EX_InstanceVariable || Member.Token==EX_DefaultVariable
					|| Member.Token==EX_StructMember || Member.Token==EX_FinalFunction || Member.Token==EX_DelegateFunction || Member.Token==EX_VirtualFunction
					|| Member.Token==EX_GlobalFunction || Member.Token==EX_ArrayElement || Member.Token==EX_DynArrayElement;
				if( !Named && (Member.Type.Kind==TK_Any ? Fill!=0 : Fill>HostSize(Member.Type)) )
					TypeFail( TEXT("has a context fill larger than its value") );
				// SkipNullContext jumps this far when the object is None. The compiler's implicit Outer has a
				// fixed skip, and a foreach may skip the whole loop.
				UBOOL Outer = Target.Token==EX_InstanceVariable && Target.Property && Target.Property->GetFName()==NAME_Outer
					&& Target.Property->GetOuter()==UObject::StaticClass() && Length==5;
				// With -debug a latent call is followed by an empty statement and its debug info inside the skip.
				UBOOL Latent = !Error && From+Length==Pos+1+DEBUG_INFO_SIZE && Pos+1+DEBUG_INFO_SIZE<=Size && Code[Pos]==EX_Nothing && IsDebugInfo(Pos+1);
				if( From+Length!=Pos && !Outer && !Latent )
				{
					if( Foreach && Landings )
						Landings->AddItem( From+Length );
					else
						Fail( TEXT("has a context skip that misses its expression") );
				}
				E.Inner = Member.Inner;
				E.Kind = Member.Kind;
				E.Iterator = Member.Iterator;
				E.Property = Member.Property;
				E.Type = Member.Type;
				break;
			}
			case EX_VirtualFunction:
			case EX_GlobalFunction:
			{
				INT At = Pos-1;
				FName Name;
				if( Has(sizeof(FName)) )
					appMemcpy( &Name, Code+Pos, sizeof(FName) );
				Skip( sizeof(FName) );
				// Behind an object of unknown class the call is skipped.
				UClass* In = ScopeClass( Scope );
				UFunction* Found = In && !Error ? FindByName( Name, E.Token==EX_GlobalFunction, Scope ? In : NULL ) : NULL;
				// Behind a context the object may lack it, retail packages were compiled against other versions of
				// their dependencies. The interpreter then skips the call.
				if( Loaded && !Found && !Member )
					TypeFail( TEXT("calls a function its class lacks") );
				if( Found && GNeedsFixedCall(Found) )
					TypeFail( TEXT("calls by name a function that is called directly") );
				// The interpreter calls only a function with the parameters checked here. Retail packages compiled against
				// older versions pass arguments of another type behind a context, which then skips the whole call.
				UBOOL Misfit = 0;
				Call( E, Found, Found || !Member, Foreach==FE_Call, Member ? &Misfit : NULL );
				if( Loaded )
					Node->ScriptFields.Set( At, Misfit ? NULL : Found );
				break;
			}
			case EX_FinalFunction:
				// Any script function may be named, the interpreter guards the object it runs on.
				Call( E, (UFunction*)Operand(UFunction::StaticClass()), 1, Foreach==FE_Call );
				break;
			case EX_DelegateFunction:
			{
				// The target is found at run time and checked against the delegate's signature.
				UDelegateProperty* Delegate = (UDelegateProperty*)Operand( UDelegateProperty::StaticClass() );
				FName Name;
				if( Has(sizeof(FName)) )
					appMemcpy( &Name, Code+Pos, sizeof(FName) );
				Skip( sizeof(FName) );
				if( Delegate && !Delegate->Function )
					TypeFail( TEXT("calls a delegate without a signature") );
				else if( Delegate && Delegate->Function->GetFName()!=Name )
					Fail( TEXT("calls a delegate by another name") );
				Call( E, Delegate ? Delegate->Function : NULL, 1, 0 );
				break;
			}
			case EX_DynamicCast:
			case EX_MetaCast:
			{
				UClass* Class = (UClass*)Operand( UClass::StaticClass() );
				Need( Expr().Type, ObjectType, TEXT("casts a value of another type") );
				// The compiler takes Class(X) for a class of Object.
				if( E.Token==EX_MetaCast )
					E.Type = ScriptType( TK_Object, NULL, UClass::StaticClass(), Class );
				else
					E.Type = ScriptType( TK_Object, NULL, Class, Class==UClass::StaticClass() ? UObject::StaticClass() : NULL );
				break;
			}
			case EX_StructCmpEq:
			case EX_StructCmpNe:
			{
				UStruct* Struct = (UStruct*)Operand( UStruct::StaticClass() );
				if( Struct && Struct->GetClass()!=UStruct::StaticClass() )
					Fail( TEXT("compares a struct that is no struct") );
				FScriptType Compared = Struct ? ScriptType( TK_Struct, Struct ) : ScriptType( TK_Any );
				Need( Expr( FE_None, Scope ).Type, Compared, TEXT("compares a value of another struct") );
				Need( Expr( FE_None, Scope ).Type, Compared, TEXT("compares a value of another struct") );
				E.Type = IntType;
				break;
			}
			case EX_PrimitiveCast:
				PrimitiveCast( E );
				break;
			case EX_New:
			{
				// execNew reads an outer, a name, flags and a class.
				Need( Expr().Type, ObjectType, TEXT("creates an object from an argument of another type") );
				Need( Expr().Type, ScriptType(TK_String), TEXT("creates an object from an argument of another type") );
				Need( Expr().Type, IntType, TEXT("creates an object from an argument of another type") );
				FScriptExpr Class = Expr();
				Need( Class.Type, ObjectType, TEXT("creates an object from an argument of another type") );
				E.Type = ScriptType( TK_Object, NULL, Class.Type.Meta );
				break;
			}
			case EX_IntConst:
				E.Known = 1;
				E.Value = Int();
				E.Type = IntType;
				break;
			case EX_FloatConst:
				Skip( sizeof(INT) );
				E.Type = IntType;
				break;
			case EX_RotationConst:
			case EX_VectorConst:
				Skip( 3*sizeof(INT) );
				E.Type = ScriptType( TK_Struct, CoreStruct(E.Token==EX_VectorConst ? TEXT("Vector") : TEXT("Rotator")) );
				break;
			case EX_ByteConst:
			case EX_IntConstByte:
				E.Known = 1;
				E.Value = Byte();
				E.Type = ScriptType( E.Token==EX_ByteConst ? TK_Byte : TK_Pod4 );
				break;
			case EX_NameConst:
				Skip( sizeof(FName) );
				E.Type = ScriptType( TK_Name );
				break;
			case EX_DelegateProperty:
				Skip( sizeof(FName) );
				E.Type = ScriptType( TK_Delegate );
				break;
			case EX_StringConst:
				while( Byte() );
				E.Type = ScriptType( TK_String );
				break;
			case EX_UnicodeStringConst:
				while( Word() );
				E.Type = ScriptType( TK_String );
				break;
			case EX_ObjectConst:
			{
				// A missing import still writes a pointer.
				UObject* Value = Object();
				E.Type = ScriptType( TK_Object, NULL, Value ? Value->GetClass() : NULL, Cast<UClass>(Value) );
				break;
			}
			case EX_Self:
				E.Type = ScriptType( TK_Object, NULL, Node->GetOwnerClass() );
				break;
			case EX_NoObject:
				E.Type = ObjectType;
				break;
			case EX_IntZero:
			case EX_IntOne:
			case EX_True:
			case EX_False:
				E.Known = 1;
				E.Value = E.Token==EX_IntOne || E.Token==EX_True;
				E.Type = IntType;
				break;
			case EX_Nothing:
				E.Type = ScriptType( TK_None );
				break;
			default:
				Pos--;
				Fail( TEXT("has a statement where an expression belongs") );
				break;
		}
		Depth--;
		return E;
	}

	void Statement()
	{
		BYTE Token = Byte();
		switch( Token )
		{
			case EX_Return:
			{
				// execReturn evaluates into the caller's result, which is sized for the return value.
				FScriptExpr Value = Expr();
				if( Function )
				{
					UProperty* Return = Function->GetReturnProperty();
					Need( Value.Type, Return ? TypeOf(Return) : ScriptType(TK_Bad), TEXT("returns a value of another type") );
				}
				OptionalDebugInfo();
				Ends();
				break;
			}
			case EX_EatString:
			{
				// The compiler eats arrays of strings too, an array has a string's layout.
				INT Start = Pos-1;
				FScriptType Value = Expr().Type;
				if( Value.Kind==TK_Array && Value.Dim==1 )
				{
					LeftArray( Start, Value );
					Value = ScriptType( TK_String );
				}
				Need( Value, ScriptType(TK_String), TEXT("eats a value that is no string") );
				break;
			}
			case EX_GotoLabel:
				Need( Expr().Type, ScriptType(TK_Name), TEXT("goes to a label that is no name") );
				break;
			case EX_Jump:
				Jump( Word() );
				Ends();
				break;
			case EX_JumpIfNot:
			{
				INT To = Word();
				FScriptExpr Condition = Expr();
				Need( Condition.Type, IntType, TEXT("has a condition that is no bool") );
				// A constant condition always or never jumps.
				if( Condition.Known && Condition.Value )
					Targets.AddItem( To );
				else
					Jump( To );
				if( Condition.Known && !Condition.Value )
					Ends();
				break;
			}
			case EX_Assert:
				Word();
				Need( Expr().Type, IntType, TEXT("has a condition that is no bool") );
				break;
			case EX_Switch:
			{
				// execSwitch compares strings when the size is 0 and bytes otherwise.
				BYTE SwitchSize = Byte();
				FScriptExpr Value = Expr();
				if( Value.Type.Kind!=TK_Any && Value.Type.Kind!=TK_None && (SwitchSize==0)!=(Value.Type.Kind==TK_String) )
					TypeFail( TEXT("has a switch whose size does not fit its value") );
				if( !FitsBuffer(Value.Type) )
					TypeFail( TEXT("has a value that does not fit its buffer") );
				Switches.AddItem( Pos );
				SwitchTypes.AddItem( Value.Type );
				break;
			}
			case EX_Case:
			{
				INT At = Pos-1;
				_WORD Next = Word();
				if( Next!=MAXWORD )
				{
					Jump( Next );
					FScriptType Value = Expr().Type;
					if( !FitsBuffer(Value) )
						TypeFail( TEXT("has a value that does not fit its buffer") );
					CaseTypes.Set( At, Value );
				}
				break;
			}
			case EX_Iterator:
			{
				TArray<INT> Skips;
				Landings = &Skips;
				FScriptExpr Loop = Expr( FE_Call );
				Landings = NULL;
				INT End = Word();
				IteratorEnds.AddItem( End );
				Edges.AddItem( End );
				if( !Loop.Iterator )
					Fail( TEXT("has a foreach without an iterator") );
				for( INT i=0; i<Skips.Num(); i++ )
					if( Skips(i)!=End+1 )
						Fail( TEXT("has a context skip that misses its expression") );
				break;
			}
			case EX_LabelTable:
			{
				Ends();
				if( !StateCode )
					Fail( TEXT("has a label table outside state code") );
				for( FLabelEntry Entry(NAME_None,0); Has(sizeof(FLabelEntry)); )
				{
					appMemcpy( &Entry, Code+Pos, sizeof(FLabelEntry) );
					Pos += sizeof(FLabelEntry);
					if( Entry.Name==NAME_None )
						break;
					Targets.AddItem( Entry.iCode );
					Entries.AddItem( Entry.iCode );
				}
				break;
			}
			case EX_Let:
			case EX_LetBool:
			case EX_LetDelegate:
			{
				// The target is evaluated for its address alone, execLetBool also applies its bit mask.
				FScriptExpr Target = Expr();
				if( Target.Kind!=EK_Variable && !(Target.Kind==EK_Length && Token==EX_Let) )
					Fail( TEXT("assigns to a value that is no variable") );
				if( Token==EX_LetBool && Target.Inner!=EX_BoolVariable )
					Fail( TEXT("assigns a bool to a variable that is no bool") );
				// Through None execLet evaluates into scratch room, which it destroys as the property found here.
				UArrayProperty* Array = Target.Inner==EX_DynArrayElement ? Cast<UArrayProperty>( Target.Property ) : NULL;
				UProperty* Held = Array ? Array->Inner : Target.Property;
				if( Loaded && Token==EX_Let && Target.Kind==EK_Variable && Held && (Held->PropertyFlags & CPF_NeedCtorLink) && TypeOf(Held).Kind==Target.Type.Kind )
					Node->ScriptFields.Set( Current, Held );
				FScriptType Slot = Token==EX_LetBool || Target.Kind==EK_Length ? IntType : Token==EX_LetDelegate ? ScriptType(TK_Delegate) : Target.Type;
				// execLetDelegate writes a delegate through the target's address.
				if( Token==EX_LetDelegate )
					Need( Target.Type, Slot, TEXT("assigns a delegate to a variable that is no delegate") );
				Need( Expr().Type, Slot, TEXT("assigns a value of another type") );
				break;
			}
			case EX_DebugInfo:
				DebugInfo();
				break;
			case EX_Stop:
				// execStop clears the code pointer, which only state code tests for.
				if( Function )
					Fail( TEXT("stops inside a function") );
				Ends();
				break;
			case EX_EndOfScript:
				Ends();
				break;
			case EX_Nothing:
			case EX_IteratorNext:
			case EX_IteratorPop:
				break;
			default:
			{
				Pos--;
				// A native function's parameter statements are never run. The compiler wraps a string statement
				// in EX_EatString to free it. A bare string statement would leak.
				INT Start = Pos;
				FScriptType Value = Expr().Type;
				if( Function && (Function->FunctionFlags & FUNC_Native) )
					break;
				if( !FitsBuffer(Value) )
					TypeFail( TEXT("has a value that does not fit its buffer") );
				else if( Value.Kind==TK_String )
					TypeFail( TEXT("has a statement whose value is never freed") );
				// Retail compiles a bare dynamic array statement, GStepStatement frees it as the array found here.
				else if( Value.Kind==TK_Array && Value.Dim==1 )
					LeftArray( Start, Value );
				break;
			}
		}
	}

	void LeftArray( INT Start, FScriptType Value )
	{
		if( !Loaded || !Value.Inner )
			return;
		UArrayProperty* Array = Cast<UArrayProperty>( Value.Inner->GetOuter() );
		if( Array && Array->Inner==Value.Inner )
			Node->ScriptFields.Set( GStatementKey(Start), Array );
	}
};

const TCHAR* GScriptStructureError( UStruct* Node )
{
	guard(GScriptStructureError);
	Node->ScriptFields.Empty();
	return FScriptWalker( Node, 1 ).Run();
	unguard;
}

UBOOL GScriptStatements( UStruct* Node, TArray<BYTE>& Starts )
{
	guard(GScriptStatements);
	FScriptWalker Walker( Node, 0 );
	Starts.Empty();
	if( Walker.Run() )
		return 0;
	Starts = Walker.Reached;
	return 1;
	unguard;
}
