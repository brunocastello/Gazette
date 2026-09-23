/*
 * Gazette — gazette_sys.h on Windows 95 through XP
 * Copyright (c) 2026 brunocastello
 *
 * The same two clocks the Mac keeps: "now" in local time, and the distance
 * from it to UTC. Both are read from Windows' own two readings of the same
 * moment -- GetLocalTime and GetSystemTime -- so daylight saving is whatever
 * Windows says it is, as the Mac's is whatever the Date & Time control panel
 * says.
 */

#include <windows.h>

#include <stdlib.h>

#include "core/gazette_sys.h"

/* 100-nanosecond intervals between 1601 (FILETIME's epoch) and 1970. */
#define kFileTimeTo1970 116444736000000000ULL

void *GazetteSysAlloc(size_t size)
{
    return calloc(1, size);
}

void GazetteSysFree(void *p)
{
    free(p);
}

/* A SYSTEMTIME as seconds since 1970, whatever clock it was read on. */
static long Seconds(const SYSTEMTIME *when)
{
    FILETIME       file;
    ULARGE_INTEGER n;

    if (!SystemTimeToFileTime(when, &file)) {
        return 0;
    }
    n.LowPart  = file.dwLowDateTime;
    n.HighPart = file.dwHighDateTime;
    return (long)((n.QuadPart - kFileTimeTo1970) / 10000000ULL);
}

long GazetteSysLocalNow(void)
{
    SYSTEMTIME local;

    GetLocalTime(&local);
    return Seconds(&local);
}

/* Local minus UTC, read together and rounded to the minute: the two calls
   straddle a second boundary now and then, and no zone is off by less. */
long GazetteSysGMTDelta(void)
{
    SYSTEMTIME local, utc;
    long       delta;

    GetSystemTime(&utc);
    GetLocalTime(&local);
    delta = Seconds(&local) - Seconds(&utc);
    return ((delta + (delta >= 0 ? 30 : -30)) / 60) * 60;
}
