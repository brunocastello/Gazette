/*
 * Gazette — the network's BSD-sockets half: sockets, the monotonic clock,
 * calloc
 * Copyright (c) 2026 brunocastello
 *
 * See gazette_net.h. The streams and the HTTP fetch above this are the Mac's
 * own code, unchanged: they speak only Certainly, and Certainly's
 * transport_posix.c is its Open Transport client rewritten over non-blocking
 * BSD sockets. What is left for this file is what gazette_net_ot.c does on
 * the Mac and gazette_net_win32.c on Windows — bring the network up, count
 * ticks, allocate. Used by the Mac OS X 10.4+ build and by the host test in
 * tests/posix.
 */

#include <certainly.h>

#include <stdlib.h>

/* Certainly's own tick clock, so the two agree on what a tick is. It is the
   BSD branch of certainly_compat.h; nothing of Carbon comes with it. */
#ifndef CERTAINLY_POSIX
#define CERTAINLY_POSIX 1
#endif
#include <certainly_compat.h>

#include "gazette_net.h"

static int gNetUp = 0;

/*
 * There is no stack to start: BSD sockets are simply there, and the transport
 * makes its own. Asking at launch still matters for Certainly itself, whose
 * entropy pool and trust anchors are set up by MacTLS_Init.
 */
int GazetteNetInit(void)
{
    if (gNetUp) {
        return 1;
    }
    if (MacTLS_Init() != kMacTLS_OK) {
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
    gNetUp = 0;
}

int GazetteNetIsUp(void)
{
    return gNetUp;
}

/*
 * Sixtieths of a second, which is what every timeout above this was written
 * in, from a monotonic clock (mach_absolute_time on Darwin, CLOCK_MONOTONIC
 * elsewhere) so a change of the wall clock cannot fire or stall a timeout.
 */
unsigned long GazetteNetTicks(void)
{
    return (unsigned long)TickCount();
}

void *GazetteNetAlloc(size_t size)
{
    return calloc(1, size);
}

void GazetteNetFree(void *p)
{
    free(p);
}
