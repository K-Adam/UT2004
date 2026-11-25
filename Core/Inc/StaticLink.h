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
