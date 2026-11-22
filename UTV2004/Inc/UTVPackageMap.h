#ifndef _UTVPACKAGEMAP_H_
#define _UTVPACKAGEMAP_H_

#include "EpicInclude.h"

#define UTV2004C TEXT("UTV2004C")
#define UTVVERSION TEXT("1.00b21")

class UUTVPackageMap : public UPackageMap
{
	DECLARE_CLASS(UUTVPackageMap,UPackageMap,CLASS_Transient,utv2004);
	// User-provided, so new UUTVPackageMap() does not zero the class and index StaticAllocateObject set.
	UUTVPackageMap() {}
	void ShowStatus ();
	UBOOL CreateLinkers ();
	INT GetMaxObjectIndex();
	INT GetMaxObjectIndexRead();
	INT GetMaxObjectIndexWrite();
	void IndexToObjectName( INT Index );
	INT ObjectToIndex(UObject *Object);
	INT GetReplicationId ();
	void Compute ();
	UBOOL ComputeError ();
	void FixLinkers (UPackageMap* Other);
	void PostFixLinkers (UPackageMap* Other);
	void ClearDepend ();
	INT GetDepend ();
	void SetSerializeRead(bool isRead);
	UBOOL SerializeObject( FArchive& Ar, UClass* Class, UObject*& Object );
	UBOOL SerializeName(FArchive &Ar, DWORD &Index);

	INT GetReplicationSend ();
	INT GetReplicationGet ();
	INT GetReplicationMax ();
	INT GetReplicationSendTarget ();
	INT GetReplicationGetTarget ();

	INT NeededPackage ();
protected:
	INT UTVReplication;
	INT RSend, RGet, RMax;
	INT RGetT, RSendT;
	INT MissingPackage;
	UBOOL WasComputeError;

	TArray<FString> RealNames;
	INT LastDepend;
	INT ExtraObjects;
	INT ExtraNames;
	bool IsRead;
};

#endif