// What the launchers share when the engine modules are linked into them. Include it only under __STATIC_LINK.

#ifndef _INC_STATICLINK
#define _INC_STATICLINK

#include "Engine.h"

#include "UnRender.h"
#include "UnNullRenderDevice.h"
#include "UnNet.h"
#include "UnFractal.h"
#include "UnIpDrv.h"
#include "UnTcpNetDriver.h"
#include "UnIpDrvCommandlets.h"
#include "UnIpDrvNative.h"
#include "XInterface.h"
#include "UWeb.h"
#include "UWebNative.h"
#include "OnslaughtPrivate.h"
void autoInitializeRegistrantsEditor(INT &Lookup);

// Statically linked third-party libraries allocate in their static constructors, and the engine's global operator new routes
// that through GMalloc. A launcher defines one of these right after its allocator, so GMalloc is set before they run.
struct FEarlyMalloc { FEarlyMalloc( FMalloc* InMalloc ) { GMalloc = InMalloc; } };
#ifdef _MSC_VER
// The launcher puts its globals in the library phase with init_seg, which has to come before its allocator.
#define EARLY_INIT_PRIORITY
#else
// Clang and GCC have no init_seg, the lowest priority runs before the other static constructors.
#define EARLY_INIT_PRIORITY __attribute__((init_priority(101)))
#endif

// A launcher registers its own modules between the two calls, which keeps the lookup order each one had.
inline void RegisterEngineNatives( INT& Lookup )
{
	for( INT k=0; k<ARRAY_COUNT(GNativeLookupFuncs); k++ )
		GNativeLookupFuncs[k] = NULL;

	GNativeLookupFuncs[Lookup++] = &FindCoreUObjectNative;
	GNativeLookupFuncs[Lookup++] = &FindCoreUCommandletNative;

	AUTO_INITIALIZE_REGISTRANTS_ENGINE;
}

inline void RegisterGameNatives( INT& Lookup )
{
	AUTO_INITIALIZE_REGISTRANTS_IPDRV;
	RegisterNamesXInterface();
	AUTO_INITIALIZE_REGISTRANTS_XINTERFACE;
	AUTO_INITIALIZE_REGISTRANTS_XGAME;
	AUTO_INITIALIZE_REGISTRANTS_UWEB;
	RegisterNamesOnslaught();
	AUTO_INITIALIZE_REGISTRANTS_ONSLAUGHT;
	autoInitializeRegistrantsEditor(Lookup);

	GNativeLookupFuncs[Lookup++] = &FindUWebUWebResponseNative;
	GNativeLookupFuncs[Lookup++] = &FindUWebUWebRequestNative;
	check( Lookup < ARRAY_COUNT(GNativeLookupFuncs) );
}

#endif
