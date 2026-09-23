/*
 * Gazette — the network's Windows half: Winsock 1.1, the tick clock, calloc
 * Copyright (c) 2026 brunocastello
 *
 * See gazette_net.h. The streams and the HTTP fetch above this are the Mac's
 * own code, unchanged: they speak only Certainly, and Certainly's
 * transport_win32.c is its Open Transport client rewritten over non-blocking
 * Winsock sockets. What is left for this file is what gazette_net_ot.c does
 * on the Mac — bring the network up, count ticks, allocate.
 *
 * Winsock 1.1, as Certainly's transport uses, because Winsock 2 was a
 * separate download for Windows 95 and nothing here needs it.
 */

#include <certainly.h>

#include <winsock.h>
#include <windows.h>

#include <stdlib.h>

#include "gazette_net.h"

static int gNetUp = 0;

/*
 * WSAStartup here as well as in the transport, which starts Winsock itself on
 * its first connection. Asking at launch is what lets a machine with no
 * TCP/IP installed — Windows 95 could be set up without it — say so in the
 * status line at once, as Mac OS 9 does when Open Transport will not open,
 * rather than on the first refresh. Winsock counts the calls, so the
 * transport's own start and this one each have their cleanup.
 */
int GazetteNetInit(void)
{
    WSADATA wsa;

    if (gNetUp) {
        return 1;
    }
    if (WSAStartup(MAKEWORD(1, 1), &wsa) != 0) {
        return 0;
    }
    if (MacTLS_Init() != kMacTLS_OK) {
        WSACleanup();
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
    WSACleanup();
    gNetUp = 0;
}

int GazetteNetIsUp(void)
{
    return gNetUp;
}

/*
 * Sixtieths of a second, which is what every timeout above this was written
 * in. GetTickCount counts milliseconds and wraps after 49.7 days of uptime —
 * a real figure on an NT server — so the clock is kept as a running total of
 * the differences between readings, which unsigned arithmetic gets right
 * across the wrap, and converted from that. The Mac's TickCount never needed
 * this: it is sixtieths already, and wraps after two years.
 */
unsigned long GazetteNetTicks(void)
{
    static DWORD     last;
    static ULONGLONG totalMs;
    static int       started;
    DWORD            now = GetTickCount();

    if (!started) {
        last    = now;
        started = 1;
    }
    totalMs += (DWORD)(now - last);
    last     = now;
    return (unsigned long)(totalMs * 60 / 1000);
}

void *GazetteNetAlloc(size_t size)
{
    return calloc(1, size);
}

void GazetteNetFree(void *p)
{
    free(p);
}
