/*=============================================================================
	UnScript.h: UnrealScript execution engine.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Tim Sweeney
=============================================================================*/

/*-----------------------------------------------------------------------------
	Native functions.
-----------------------------------------------------------------------------*/

//
// Native function table.
//
typedef void (UObject::*Native)( FFrame& TheStack, RESULT_DECL );
extern CORE_API Native GNatives[];
// Called for every script function as it binds, so a module can wrap one natively without changing its package.
extern CORE_API void (*GBindScriptFunction)( UFunction* Function );
BYTE CORE_API GRegisterNative( INT iNative, const Native& Func, UClass* (*Class)() );
// The C++ class a native was written for, so script can only bind and call it on objects of that class.
CORE_API UClass* GNativeClass( INT iNative );
CORE_API UBOOL GIsNativeOf( const Native& Func, UClass* Class );
CORE_API Native GUncheckedNative( INT iNative );
// Path of script the loader refused, the engine leaves the level that brought it.
extern CORE_API FString GRefusedScript;

// Size of the largest struct, class or function frame linked so far, which bounds any value script can produce.
extern CORE_API INT GMaxPropertySize;
// Natives that poll a latent action, the only ones a state frame's LatentAction may name.
extern CORE_API BYTE GLatentNatives[];
// Whether two functions take and return the same parameter block, so one can be called in place of the other.
// With Override A is called in place of B by name: it may drop out, and object parameters are of the same class.
CORE_API UBOOL GSameParms( UFunction* A, UFunction* B, UBOOL Override=0 );
// Iterators and skipping natives read bytecode a call by name does not have.
UBOOL GNeedsFixedCall( UFunction* Function );
// The script function that declares a numbered native.
UFunction* GNativeFunction( INT iNative );
// Turns down the package of an object whose layout the loader had to repair.
CORE_API void GRefuseScript( UObject* Culprit, const TCHAR* Rule );
// Logs an asset the loader emptied, which draws or plays as nothing while its package's script still runs.
CORE_API void GRepairedAsset( UObject* Culprit, const TCHAR* Rule );
UBOOL GIsRefused( UObject* Object );
CORE_API void GDisableRefusedScript( TArray<UObject*>& Loaded );
CORE_API const TCHAR* GScriptStructureError( UStruct* Node );
// Marks where each statement that can run starts, by the grammar alone, for checks made before the batch has loaded.
UBOOL GScriptStatements( UStruct* Node, TArray<BYTE>& Starts );
// The ScriptFields key of the element an array opcode reads as a context's member, apart from the opcode's own.
inline INT GContextElementKey( INT Offset ) { return -1-Offset; }

extern CORE_API Native GCasts[];
BYTE CORE_API GRegisterCast( INT CastCode, const Native& Func );


//
// Registering a native function.
//
#define IMPLEMENT_FUNCTION(cls,num,func) \
	extern "C" DLL_EXPORT Native int##cls##func = (Native)&cls::func; \
	static BYTE cls##func##Temp = GRegisterNative( num, int##cls##func, &cls::StaticClass );

#define IMPLEMENT_LATENT_FUNCTION(cls,num,func) \
	IMPLEMENT_FUNCTION(cls,num,func) \
	static BYTE cls##func##Latent = GLatentNatives[num] = 1;

/*-----------------------------------------------------------------------------
	Macros.
-----------------------------------------------------------------------------*/

//
// Macros for grabbing parameters for native functions.
//
// Script only checks an object's class when it is used as a context, so natives check their object parameters.
#define P_CHECK_OBJECT(cls,var)       if( var && !((UObject*)var)->IsA(cls::StaticClass()) ) var=NULL;
#define P_GET_UBOOL(var)              DWORD var=0;                         Stack.Step( Stack.Object, &var    );
#define P_GET_UBOOL_OPTX(var,def)     DWORD var=def;                       Stack.Step( Stack.Object, &var    );
#define P_GET_STRUCT(typ,var)         typ   var;                           Stack.Step( Stack.Object, &var    );
#define P_GET_STRUCT_OPTX(typ,var,def)typ   var=def;                       Stack.Step( Stack.Object, &var    );
#define P_GET_STRUCT_REF(typ,var)     typ   var##T; GPropAddr=0;           Stack.Step( Stack.Object, &var##T ); if( GPropObject )GPropObject->NetDirty(GProperty); typ*     var = GPropAddr ? (typ    *)GPropAddr:&var##T;
#define P_GET_INT(var)                INT   var=0;                         Stack.Step( Stack.Object, &var    );
#define P_GET_INT_OPTX(var,def)       INT   var=def;                       Stack.Step( Stack.Object, &var    );
#define P_GET_INT_REF(var)            INT   var##T=0; GPropAddr=0;         Stack.Step( Stack.Object, &var##T ); if( GPropObject )GPropObject->NetDirty(GProperty); INT*     var = GPropAddr ? (INT    *)GPropAddr:&var##T;
#define P_GET_FLOAT(var)              FLOAT var=0.f;                       Stack.Step( Stack.Object, &var    );
#define P_GET_FLOAT_OPTX(var,def)     FLOAT var=def;                       Stack.Step( Stack.Object, &var    );
#define P_GET_FLOAT_REF(var)          FLOAT var##T=0.f; GPropAddr=0;       Stack.Step( Stack.Object, &var##T ); if( GPropObject )GPropObject->NetDirty(GProperty); FLOAT*   var = GPropAddr ? (FLOAT  *)GPropAddr:&var##T;
#define P_GET_BYTE(var)               BYTE  var=0;                         Stack.Step( Stack.Object, &var    );
#define P_GET_BYTE_OPTX(var,def)      BYTE  var=def;                       Stack.Step( Stack.Object, &var    );
#define P_GET_BYTE_REF(var)           BYTE  var##T=0; GPropAddr=0;         Stack.Step( Stack.Object, &var##T ); if( GPropObject )GPropObject->NetDirty(GProperty); BYTE*    var = GPropAddr ? (BYTE   *)GPropAddr:&var##T;
#define P_GET_NAME(var)               FName var=NAME_None;                 Stack.Step( Stack.Object, &var    );
#define P_GET_NAME_OPTX(var,def)      FName var=def;                       Stack.Step( Stack.Object, &var    );
#define P_GET_NAME_REF(var)           FName var##T=NAME_None; GPropAddr=0; Stack.Step( Stack.Object, &var##T ); if( GPropObject )GPropObject->NetDirty(GProperty); FName*   var = GPropAddr ? (FName  *)GPropAddr:&var##T;
#define P_GET_STR(var)                FString var;                         Stack.Step( Stack.Object, &var    );
#define P_GET_STR_OPTX(var,def)       FString var(def);                    Stack.Step( Stack.Object, &var    );
#define P_GET_STR_REF(var)            FString var##T; GPropAddr=0;         Stack.Step( Stack.Object, &var##T ); if( GPropObject )GPropObject->NetDirty(GProperty); FString* var = GPropAddr ? (FString*)GPropAddr:&var##T;
#define P_GET_OBJECT(cls,var)         cls*  var=NULL;                      Stack.Step( Stack.Object, &var    ); P_CHECK_OBJECT(cls,var);
#define P_GET_OBJECT_OPTX(cls,var,def)cls*  var=def;                       Stack.Step( Stack.Object, &var    ); P_CHECK_OBJECT(cls,var);
#define P_GET_OBJECT_REF(cls,var)     cls*  var##T=NULL; GPropAddr=0;      Stack.Step( Stack.Object, &var##T ); if( GPropObject )GPropObject->NetDirty(GProperty); cls**    var = GPropAddr ? (cls   **)GPropAddr:&var##T; P_CHECK_OBJECT(cls,*var);
#define P_GET_ARRAY_REF(typ,var)      typ   var##T[256]; GPropAddr=0;      Stack.Step( Stack.Object,  var##T ); if( GPropObject )GPropObject->NetDirty(GProperty); typ*     var = GPropAddr ? (typ    *)GPropAddr: var##T;
// rjp --
#define P_GET_TARRAY(var,typ)         TArray<typ> var;                     Stack.Step( Stack.Object, &var    );
// -- rjp
// gam ---
#define P_GET_TARRAY_REF(var,type)    TArray<type> var##T; GPropAddr=0;    Stack.Step( Stack.Object, &var##T ); if( GPropObject )GPropObject->NetDirty(GProperty); TArray<type>* var = GPropAddr ? (TArray<type>*)GPropAddr:&var##T;
// --- gam
#define P_GET_SKIP_OFFSET(var)        _WORD var; {checkSlow(*Stack.Code==EX_Skip); Stack.Code++; var=*(_WORD*)Stack.Code; Stack.Code+=2; }
//DEBUGGER
#define P_FINISH                      Stack.Code++; if ( *Stack.Code == EX_DebugInfo ) Stack.Step( Stack.Object, NULL );

//
// Convenience macros.
//
#define P_GET_VECTOR(var)           P_GET_STRUCT(FVector,var)
#define P_GET_VECTOR_OPTX(var,def)  P_GET_STRUCT_OPTX(FVector,var,def)
#define P_GET_VECTOR_REF(var)       P_GET_STRUCT_REF(FVector,var)
#define P_GET_ROTATOR(var)          P_GET_STRUCT(FRotator,var)
#define P_GET_ROTATOR_OPTX(var,def) P_GET_STRUCT_OPTX(FRotator,var,def)
#define P_GET_ROTATOR_REF(var)      P_GET_STRUCT_REF(FRotator,var)
#define P_GET_ACTOR(var)            P_GET_OBJECT(AActor,var)
#define P_GET_ACTOR_OPTX(var,def)   P_GET_OBJECT_OPTX(AActor,var,def)
#define P_GET_ACTOR_REF(var)        P_GET_OBJECT_REF(AActor,var)

//
// Iterator macros.
//
extern CORE_API FFrame* GIteratorFrame;
#define PRE_ITERATOR \
	INT wEndOffset = Stack.ReadWord(); \
	BYTE B=0, Buffer[MAX_STRING_CONST_SIZE]; \
	BYTE *StartCode = Stack.Code; \
	do {
// Each statement's value lands on what the one before left, the load check keeps it to what an empty string covers.
#define POST_ITERATOR \
		while( (B=*Stack.Code)!=EX_IteratorPop && B!=EX_IteratorNext ) \
		{ \
			appMemzero( Buffer, sizeof(FString) ); \
			Stack.Step( Stack.Object, Buffer ); \
		} \
		if( *Stack.Code++==EX_IteratorNext ) \
			Stack.Code = StartCode; \
	} while( B != EX_IteratorPop ); \
	GIteratorFrame = &Stack;

/*-----------------------------------------------------------------------------
	FFrame implementation.
-----------------------------------------------------------------------------*/

inline FFrame::FFrame( UObject* InObject )
:	Node		(InObject ? InObject->GetClass() : NULL)
,	Object		(InObject)
,	Code		(NULL)
,	Locals		(NULL)
{}
inline FFrame::FFrame( UObject* InObject, UStruct* InNode, INT CodeOffset, void* InLocals )
:	Node		(InNode)
,	Object		(InObject)
,	Code		(&InNode->Script(CodeOffset))
,	Locals		((BYTE*)InLocals)
{}
FORCEINLINE void FFrame::Step( UObject* Context, RESULT_DECL )
{
	guardSlow(FFrame::Step);
	INT B = *Code++;
	(Context->*GNatives[B])( *this, Result );
	unguardfSlow(( TEXT("(%s @ %s : %04X)"), Object->GetFullName(), Node->GetFullName(), Code - Node->Script.GetTypedData() ));
}
inline INT FFrame::ReadInt()
{
	INT Result;
	Result = *(INT*)Code;
	Code += sizeof(INT);
	return Result;
}
inline UObject* FFrame::ReadObject()
{
	UObject* Result;

#if SERIAL_POINTER_INDEX
	INT idx = *(INT*)Code;
	check(idx >= 0 && idx < GTotalSerializedPointers);
	Result = (UObject*) GSerializedPointers[idx];
#else
	Result = *(UObject**)Code;
#endif

	Code += sizeof(INT);
	return Result;
}
inline FLOAT FFrame::ReadFloat()
{
	FLOAT Result;
	Result = *(FLOAT*)Code;
	Code += sizeof(FLOAT);
	return Result;
}
inline INT FFrame::ReadWord()
{
	INT Result;
	Result = *(_WORD*)Code;
	Code += sizeof(_WORD);
	return Result;
}
inline FName FFrame::ReadName()
{
	FName Result;
	Result = *(FName*)Code;
	Code += sizeof(FName);
	return Result;
}
CORE_API void GInitRunaway();

/*-----------------------------------------------------------------------------
	FStateFrame implementation.
-----------------------------------------------------------------------------*/

inline FStateFrame::FStateFrame( UObject* InObject )
:	FFrame		( InObject )
,	CurrentFrame( NULL )
,	StateNode	( InObject->GetClass() )
,	ProbeMask	( ~(QWORD)0 )
{}
inline const TCHAR* FStateFrame::Describe()
{
	return Node ? Node->GetFullName() : TEXT("None");
}

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/

