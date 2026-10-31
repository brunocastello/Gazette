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
 * Mac OS X 10.4 and later over BSD sockets (Gazette's osx-legacy build).
 * TickCount does exist on OS X, in Carbon's Events.h, but certainly.c does
 * not include that and the core should not need a framework for a clock; this
 * one reads the same sixtieths of a second from gettimeofday, which 10.4 has.
 */
#include <stdlib.h>
#include <stdint.h>
#include <sys/time.h>

typedef char *Ptr;

#define NewPtrClear(n) ((void *)calloc(1, (size_t)(n)))
#define DisposePtr(p)  free((void *)(p))

static uint32_t TickCount(void)
{
    struct timeval tv;

    gettimeofday(&tv, NULL);
    return (uint32_t)(((uint64_t)tv.tv_sec * 60) + ((uint64_t)tv.tv_usec * 60 / 1000000));
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
