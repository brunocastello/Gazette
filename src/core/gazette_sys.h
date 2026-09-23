/*
 * Gazette — the four system services the engine needs that are not files or
 * the network: memory, the local clock and the time zone.
 * Copyright (c) 2026 brunocastello
 *
 * gazette_feeds.c and gazette_photos.c were written against NewPtrClear,
 * GetDateTime and ReadLocation. Those calls are here now, behind plain C, and
 * gazette_sys_mac.c / gazette_sys_win32.c answer them -- so the engine files
 * are one source for both systems, as src/net/ and src/store/ are.
 *
 * No system headers.
 */
#ifndef GAZETTE_SYS_H
#define GAZETTE_SYS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Zeroed memory, and its release: NewPtrClear / DisposePtr on Mac OS 9,
   calloc / free on Windows. NULL when there is none. */
void *GazetteSysAlloc(size_t size);
void  GazetteSysFree(void *p);

/*
 * Now, in seconds since 1970 on the reader's own clock -- local time, as the
 * Macintosh clock keeps it. Article dates are UTC and are brought to this
 * clock by adding GazetteSysGMTDelta; "now" never is.
 */
long GazetteSysLocalNow(void);

/* Seconds east of GMT, daylight saving included. */
long GazetteSysGMTDelta(void);

#ifdef __cplusplus
}
#endif

#endif /* GAZETTE_SYS_H */
