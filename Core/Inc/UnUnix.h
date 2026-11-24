/*============================================================================
	UnUnix.h: Unix system-specific declarations.

	Revision history:
		* Created by Mike Danylchuk
		* Modified for Unreal Engine standards compliance by Brandon Reinhart
============================================================================*/

// Pathnames.
// UNICHAR, the TCHAR it is comes later in Core.h
CORE_API char* appUnixPath( const UNICHAR* Path );

// String functions.
int stricmp( const char* s, const char* t );
int strnicmp( const char* s, const char* t, int n );
extern "C" char* strupr( char* s );

/*----------------------------------------------------------------------------
	The End.
----------------------------------------------------------------------------*/


