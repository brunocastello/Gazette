/*
 * certainly_compat.h - the three Toolbox calls the TLS core makes.
 *
 * certainly.c allocates with NewPtrClear, frees with DisposePtr and reads the
 * clock with TickCount. All three are Mac OS calls, and all three have exact
 * equivalents elsewhere, so rather than rewrite the core they are supplied
 * here for platforms that lack them. docs/porting.md counted them before the
 * port began and this is the whole list.
 */
#ifndef CERTAINLY_COMPAT_H
#define CERTAINLY_COMPAT_H

#ifdef CERTAINLY_OPEN_TRANSPORT

#include <MacMemory.h>  /* NewPtrClear, DisposePtr */
#include <Events.h>     /* TickCount - Gateway patch, see PATCHES.md */

#elif defined(CERTAINLY_POSIX)

/*
 * BSD sockets: Mac OS X 10.4 and up, Linux, and any host cc the tests use.
 * transport_posix.c and entropy_posix.c are the other half of this branch.
 *
 * Nothing here includes Carbon. On the OS X application MacTypes.h may well
 * be visible in another translation unit, so Ptr is guarded rather than
 * assumed, and TickCount is static inline (private to each file) so it can
 * never meet the Toolbox's own at link time.
 */
#include <stdlib.h>
#include <stdint.h>
#include <time.h>

#ifdef __APPLE__
#include <mach/mach_time.h>
#endif

#ifndef __MACTYPES__
typedef char *Ptr;
#endif

#define NewPtrClear(n) ((void *)calloc(1, (size_t)(n)))
#define DisposePtr(p)  free((void *)(p))

/*
 * Sixtieths of a second from a monotonic clock, for the same reason as the
 * Windows branch: every timeout in the core is written in ticks.
 *
 * On Darwin the clock is mach_absolute_time, whose unit is whatever the
 * timebase says -- 30 ns on a G4, 1 ns on Intel. Scaling ticks by
 * numer/denom naively overflows 64 bits within minutes on the PowerPC
 * timebase, so the division is split into quotient and remainder, which
 * keeps every product below 2^63 for any realistic uptime. Only the main
 * thread reads the clock, so the cached timebase needs no lock.
 */
static inline uint32_t TickCount(void)
{
#ifdef __APPLE__
    static mach_timebase_info_data_t tb;
    uint64_t abs, q, r, ns;

    if (tb.denom == 0)
        mach_timebase_info(&tb);
    abs = mach_absolute_time();
    q   = abs / tb.denom;
    r   = abs % tb.denom;
    ns  = q * tb.numer + r * tb.numer / tb.denom;
    return (uint32_t)((ns / 1000000u) * 60u / 1000u);
#else
    struct timespec ts;
    uint64_t ms;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    ms = (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
    return (uint32_t)(ms * 60u / 1000u);
#endif
}

#else

#include <stdlib.h>
#include <stdint.h>
#include <windows.h>

/* DisposePtr((Ptr)x) is the Toolbox idiom and appears at the call sites. */
typedef char *Ptr;

#define NewPtrClear(n) ((void *)calloc(1, (size_t)(n)))
#define DisposePtr(p)  free((void *)(p))

/*
 * Sixtieths of a second, which is what TickCount counts and what every
 * timeout in the TLS core was written against. The 64-bit intermediate
 * matters: GetTickCount passes a milliard milliseconds in under a fortnight of
 * uptime, and multiplying that by 60 in 32 bits wraps long before it does.
 */
static uint32_t TickCount(void)
{
    return (uint32_t)((ULONGLONG)GetTickCount() * 60 / 1000);
}

#endif /* CERTAINLY_OPEN_TRANSPORT */

#endif /* CERTAINLY_COMPAT_H */
