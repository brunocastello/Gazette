/*
 * Gazette — non-blocking HTTP GET with redirects
 * Copyright (c) 2026 brunocastello
 *
 * One fetch, driven a slice at a time from the WaitNextEvent idle branch
 * (constraint 7). Nothing here blocks and nothing here allocates during a
 * transfer: the whole fetch lives in one NewPtrClear block taken at the start,
 * so a refresh cannot fail halfway for want of memory.
 *
 * The body is streamed to a sink rather than accumulated (constraint 3). A
 * front-page feed is 100-200 KB and an article page can be more; holding one
 * in memory to hand over at the end would be the largest allocation in the
 * application, for no reason. The sink sees the bytes as they arrive, already
 * de-chunked.
 *
 * The interface is plain C with no Toolbox types, so the core seam and the
 * feed engine can drive a fetch without including Open Transport.
 */
#ifndef GAZETTE_FETCH_H
#define GAZETTE_FETCH_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    kGazetteFetchIdle = 0,
    kGazetteFetchConnecting,
    kGazetteFetchSending,
    kGazetteFetchHeaders,
    kGazetteFetchBody,
    kGazetteFetchDone,
    kGazetteFetchFailed
} GazetteFetchState;

/*
 * Called with each run of body bytes as it arrives, in order, already
 * de-chunked. Return 0 to abandon the fetch — which is what a parser does
 * when it has seen enough, and what the store does when the disk is full.
 */
typedef int (*GazetteFetchSink)(const char *data, size_t len, void *context);

typedef struct GazetteFetch GazetteFetch;

/*
 * Begin fetching an absolute http(s) URL. Returns NULL when the URL is not
 * one, when networking is down, or when the block cannot be allocated.
 * Nothing has gone over the wire yet: the connection is opened under Pump.
 */
GazetteFetch *GazetteFetchStart(const char *url,
                                GazetteFetchSink sink, void *context);

/* One slice. Call from the event loop's idle branch until the state is Done
   or Failed. */
GazetteFetchState GazetteFetchPump(GazetteFetch *f);

GazetteFetchState GazetteFetchGetState(const GazetteFetch *f);

/* The final response's status code, or 0 before the head has been read. */
int  GazetteFetchStatus(const GazetteFetch *f);

/* Body bytes handed to the sink so far. */
long GazetteFetchBytesRead(const GazetteFetch *f);

/* The URL the body actually came from, which is not the one passed to Start
   if any redirect was followed. */
const char *GazetteFetchFinalURL(const GazetteFetch *f);

/* A line of failure text, or "" while nothing has gone wrong. */
const char *GazetteFetchErrorText(const GazetteFetch *f);

/* Closes the connection if it is still open, and frees everything. */
void GazetteFetchDestroy(GazetteFetch *f);

#ifdef __cplusplus
}
#endif

#endif /* GAZETTE_FETCH_H */
