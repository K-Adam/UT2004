/*=============================================================================
	HostText.h: The text that names a host without an address.
=============================================================================*/

#ifndef _INC_HOSTTEXT
#define _INC_HOSTTEXT

// A host on the web master's socket that has no address is <32 hex digits>.signal. FURL, server rows, favorites and
// scripts all cut host and port at the first colon, so the name has dots and no colon.
#define HOST_SIGNAL_ID_LEN 32

// 32 lowercase hex digits, as the web master issues ids
inline UBOOL HostTextId( const TCHAR* Id )
{
	INT i = 0;
	while( i<HOST_SIGNAL_ID_LEN && ((Id[i]>='0' && Id[i]<='9') || (Id[i]>='a' && Id[i]<='f')) )
		i++;
	return i==HOST_SIGNAL_ID_LEN && !Id[i];
}

// Such a host is never looked up, a malformed one and the fully qualified spelling with a trailing dot included
inline UBOOL HostTextSignalSuffix( const TCHAR* Host )
{
	INT Len = appStrlen( Host );
	while( Len && Host[Len-1]=='.' )
		Len--;
	return Len>=7 && !appStrnicmp( Host+Len-7, TEXT(".signal"), 7 );
}

// The id of a name in either case, in lowercase, or empty when Host is no name
inline FString HostTextSignalId( const TCHAR* Host )
{
	if( appStrlen( Host )!=HOST_SIGNAL_ID_LEN+7 || !HostTextSignalSuffix( Host ) )
		return FString();
	FString Id = FString( Host ).Left( HOST_SIGNAL_ID_LEN ).Locs();
	return HostTextId( *Id ) ? Id : FString();
}

inline UBOOL HostTextSignal( const TCHAR* Host )
{
	return HostTextSignalId( Host ).Len() > 0;
}

// A name in lowercase, any other host unchanged
inline FString HostTextCanonical( const FString& Host )
{
	FString Id = HostTextSignalId( *Host );
	return Id.Len() ? Id + TEXT(".signal") : Host;
}

#endif
