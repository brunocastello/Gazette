/*
 * Gazette — gazette_sys.h on Mac OS 9
 * Copyright (c) 2026 brunocastello
 */

#include <DateTimeUtils.h>      /* GetDateTime */
#include <MacMemory.h>          /* NewPtrClear, DisposePtr */
#include <OSUtils.h>            /* MachineLocation */
#include <Script.h>             /* ReadLocation */

#include "core/gazette_sys.h"

/*
 * Seconds between the Macintosh epoch (1904) and the Unix one (1970).
 * GetDateTime counts from the former; every date in the store counts from the
 * latter, because that is what feeds date their articles in.
 */
enum { kMacToUnixEpoch = 2082844800L };

void *GazetteSysAlloc(size_t size)
{
    return NewPtrClear((Size)size);
}

void GazetteSysFree(void *p)
{
    if (p != NULL) {
        DisposePtr((Ptr)p);
    }
}

long GazetteSysLocalNow(void)
{
    unsigned long macNow = 0;

    GetDateTime(&macNow);
    return (long)macNow - kMacToUnixEpoch;
}

/*
 * Seconds east of GMT, as the Date & Time control panel has it. gmtDelta
 * shares a long with the daylight saving flag and is only three bytes wide,
 * which is why it is masked and sign-extended by hand rather than read
 * straight out. A machine that has never been told where it is answers zero,
 * which is the right answer for a machine keeping UTC.
 */
long GazetteSysGMTDelta(void)
{
    MachineLocation loc;
    long            delta;

    ReadLocation(&loc);
    delta = loc.u.gmtDelta & 0x00FFFFFFL;
    if (delta >= 0x00800000L) {
        delta -= 0x01000000L;       /* the three-byte field's sign */
    }
    return delta;
}
