/* eu2_sys.c - the few OS services the game needs beyond amiga_gfx (C only, Amiga headers here). */
#include <exec/types.h>
#include <proto/dos.h>
#include "eu2_sys.h"

void eu2_sleep_ms(int ms)
{
	LONG ticks = ms / 20;
	Delay(ticks > 0 ? ticks : 1);
}

void eu2_mkdir(const char *path)
{
	BPTR l = Lock((STRPTR)path, ACCESS_READ);
	if (l) { UnLock(l); return; }
	l = CreateDir((STRPTR)path);
	if (l) UnLock(l);
}
