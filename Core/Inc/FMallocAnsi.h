/*=============================================================================
	FMallocAnsi.h: ANSI memory allocator.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Tim Sweeney
=============================================================================*/

#include "Core.h"
#if UT_MIMALLOC
#include <mimalloc.h>
#define ANSI_MALLOC mi_malloc
#define ANSI_REALLOC mi_realloc
#define ANSI_FREE mi_free
#else
#define ANSI_MALLOC malloc
#define ANSI_REALLOC realloc
#define ANSI_FREE free
#endif

//
// ANSI C memory allocator.
//
class FMallocAnsi : public FMalloc
{
public:
	// FMalloc interface.
	void* Malloc( size_t Size, const TCHAR* Tag )
	{
		guard(FMallocAnsi::Malloc);
		check(Size>=0);
		void* Ptr = ANSI_MALLOC( Size );
		check(Ptr);
		return Ptr;
		unguard;
	}
	void* Realloc( void* Ptr, size_t NewSize, const TCHAR* Tag )
	{
		guard(FMallocAnsi::Realloc);
		check(NewSize>=0);
		void* Result;
if ((PTRINT) Ptr < 0x1000) Ptr = NULL;
		if( Ptr && NewSize )
		{
			Result = ANSI_REALLOC( Ptr, NewSize );
		}
		else if( NewSize )
		{
			Result = ANSI_MALLOC( NewSize );
		}
		else
		{
			if( Ptr )
				ANSI_FREE( Ptr );
			Result = NULL;
		}
		check(Result || !NewSize);
		return Result;
		unguardf(( TEXT("%08X %i %s"), (INT) ((PTRINT)Ptr), NewSize, Tag ));
	}
	void Free( void* Ptr )
	{
		guard(FMallocAnsi::Free);
		ANSI_FREE( Ptr );
		unguard;
	}
	void DumpAllocs()
	{
		guard(FMallocAnsi::DumpAllocs);
		debugf( NAME_Exit, TEXT("Allocation checking disabled") );
		unguard;
	}
	void HeapCheck()
	{
		guard(FMallocAnsi::HeapCheck);
#if defined _MSC_VER && DO_CHECK
		INT Result = _heapchk();
		check(Result!=_HEAPBADBEGIN);
		check(Result!=_HEAPBADNODE);
		check(Result!=_HEAPBADPTR);
		check(Result!=_HEAPEMPTY);
		check(Result==_HEAPOK);
#endif
		unguard;
	}
	void Init()
	{
		guard(FMallocAnsi::Init);
		unguard;
	}
	void Exit()
	{
		guard(FMallocAnsi::Exit);
		unguard;
	}
};

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/

