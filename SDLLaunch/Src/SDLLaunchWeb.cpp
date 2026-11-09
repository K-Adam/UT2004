#include "SDLLaunchPrivate.h"

// The log file sits in the page's IndexedDB, the browser console is where it can be read.
static class FOutputDeviceConsole : public FOutputDevice
{
	void Serialize( const TCHAR* Data, EName Event )
	{
		// A server sends its window title every second, which would bury the log.
		if( Event == NAME_Title )
			return;
		// appToAnsi returns a shared buffer, so one call per string.
		printf( "%s: ", appToAnsi(FName::SafeString(Event)) );
		puts( appToAnsi(Data) );
	}
} Console;

FOutputDevice* GWebLogHook = &Console;
