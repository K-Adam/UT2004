#ifndef UTVCOMMANDLET
#define UTVCOMMANDLET

#include "ReplicatorEngine.h"

class UUTVCommandlet : public UCommandlet
{
	DECLARE_CLASS(UUTVCommandlet,UCommandlet,CLASS_Transient,utv2004);

	void StaticConstructor();
	INT Main( const TCHAR* Parms );
public:
};

#endif
