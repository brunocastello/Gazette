/*
 * Gazette — the network's Mac OS 9 half: Open Transport, TickCount, NewPtr
 * Copyright (c) 2026 brunocastello
 *
 * See gazette_net.h. Everything in src/net/ that is not here is shared with
 * Windows, where gazette_net_win32.c stands in for this file.
 */

/* certainly.h first, for the reason gazette_net.c gives: it brings in
   <OpenTransport.h> ahead of <stdbool.h>, which MacTypes.h needs. */
#include <certainly.h>

#include <OpenTransport.h>      /* InitOpenTransport / CloseOpenTransport */
#include <Events.h>             /* TickCount */
#include <MacMemory.h>          /* NewPtrClear, DisposePtr */

#include "gazette_net.h"

static int gNetUp = 0;

int GazetteNetInit(void)
{
    OSStatus err;

    if (gNetUp) {
        return 1;
    }

    /*
     * Under Carbon this is a macro for
     * InitOpenTransportInContext(kInitOTForApplicationMask, NULL) — the plain
     * entry point is "CarbonLib: not available". See PATCHES.md §22 for why
     * that redirection needs OTCARBONAPPLICATION=1 from the build.
     *
     * Certainly never calls this itself; it assumes the application has. On
     * a machine with no TCP/IP configured it fails here rather than at
     * connect time, which is the useful place for it to fail.
     */
    err = InitOpenTransport();
    if (err != noErr) {
        return 0;
    }

    if (MacTLS_Init() != kMacTLS_OK) {
        CloseOpenTransport();
        return 0;
    }

    gNetUp = 1;
    return 1;
}

void GazetteNetShutdown(void)
{
    if (!gNetUp) {
        return;
    }
    MacTLS_Shutdown();
    CloseOpenTransport();
    gNetUp = 0;
}

int GazetteNetIsUp(void)
{
    return gNetUp;
}

unsigned long GazetteNetTicks(void)
{
    return (unsigned long)TickCount();
}

void *GazetteNetAlloc(size_t size)
{
    return NewPtrClear((Size)size);
}

void GazetteNetFree(void *p)
{
    if (p != NULL) {
        DisposePtr((Ptr)p);
    }
}
