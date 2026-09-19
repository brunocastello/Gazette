/*
 * Gazette — HTTP/1.1 client wire format
 * Copyright (c) 2026 brunocastello
 *
 * PORTABLE: no system headers. Adapted from Gateway's gw_http and gw_chunked,
 * cut down to the half a client needs: build a GET, parse a response head,
 * decode a chunked body. Gateway's request parsing, header rewriting and
 * hop-by-hop filtering are all proxy work and have no counterpart here.
 */
#ifndef GAZETTE_HTTP_H
#define GAZETTE_HTTP_H

#include <stddef.h>

#include "portable/gazette_url.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The User-Agent Gazette sends. Some feed hosts serve a different document,
 * or nothing at all, to a client that does not name itself, and one that
 * names itself honestly is easier for an operator to interpret in a log than
 * one pretending to be Netscape.
 */
#define kGazetteUserAgent "Gazette/0.1.0 (Macintosh; Mac OS 9; PowerPC)"

/* ------------------------------------------------------------------ */
/* Requests                                                            */
/* ------------------------------------------------------------------ */

/*
 * Build the GET for this URL. Returns the number of bytes written, or 0 if it
 * would not fit in cap.
 *
 * Two of the headers are load-bearing rather than polite:
 *
 *   Accept-Encoding: identity  — Gazette cannot inflate anything, and a
 *     server that is allowed to choose will send gzip. Asking for identity is
 *     the difference between a feed and a buffer of binary.
 *   Connection: close          — the body may then be delimited by the close,
 *     which is the only framing an HTTP/1.0 origin offers. Phase 4 can look at
 *     holding the connection open, which is worth real time on this hardware
 *     when several feeds share a host, but it obliges exact framing first.
 */
size_t GazetteHTTPBuildGet(const GazetteURL *url, char *out, size_t cap);

/* The same head with a body after it: POST, with the Content-Type given
   (form-encoded when NULL) and the Content-Length that bodyLen says. Returns
   0 when head and body together would not fit. */
size_t GazetteHTTPBuildPost(const GazetteURL *url, const char *contentType,
                            const char *body, size_t bodyLen,
                            char *out, size_t cap);

/* ------------------------------------------------------------------ */
/* Responses                                                           */
/* ------------------------------------------------------------------ */

typedef struct {
    int    status;
    int    httpMinor;
    size_t headLen;             /* bytes through the terminating blank line */
    int    chunked;
    int    hasContentLength;
    long   contentLength;       /* -1 when absent */
    int    connectionClose;
    int    hasLocation;
    char   location[kGazetteMaxPath];

    /* The media type alone — "text/html" out of "text/html; charset=utf-8",
       lowercased — or "" when the server did not say. */
    char   contentType[48];
} GazetteHTTPResponse;

/*
 * Parse a response head out of the bytes read so far.
 * Returns  1 when a complete head was parsed,
 *          0 when more bytes are needed,
 *         -1 when the response is malformed.
 */
int GazetteHTTPParseResponse(const char *buf, size_t len,
                             GazetteHTTPResponse *res);

/* 1 for a 3xx that carries a Location worth following. */
int GazetteHTTPIsRedirect(int status);

/*
 * 1 when the status can never carry a body, whatever the headers say
 * (RFC 9110 6.4.1). Without this a 204 or a 304 with no Content-Length would
 * be read until the connection closed, which for a keep-alive origin is
 * never.
 */
int GazetteHTTPStatusHasNoBody(int status);

/* ------------------------------------------------------------------ */
/* Chunked transfer-coding                                             */
/* ------------------------------------------------------------------ */

typedef enum {
    kGazetteChunkSize = 0,      /* reading the hex size line          */
    kGazetteChunkData,          /* copying chunk-data                 */
    kGazetteChunkDataCRLF,      /* eating the CRLF after chunk-data   */
    kGazetteChunkTrailer,       /* reading trailers after the 0 chunk */
    kGazetteChunkDone,
    kGazetteChunkError
} GazetteChunkState;

typedef struct {
    GazetteChunkState state;
    long              remaining;        /* bytes left in the current chunk */
    int               seenDigit;
    int               cr;               /* saw CR while scanning a line    */
    int               trailerBlank;     /* consecutive empty trailer lines */
} GazetteChunked;

void GazetteChunkedInit(GazetteChunked *c);

/*
 * Feed inLen bytes. Decoded body bytes are appended to out (capacity outCap);
 * *outLen receives how many were produced. Returns the number of input bytes
 * consumed, or -1 on a malformed stream. A short return means the output
 * buffer filled — call again with the input that is left.
 */
long GazetteChunkedFeed(GazetteChunked *c, const char *in, size_t inLen,
                        char *out, size_t outCap, size_t *outLen);

int GazetteChunkedDone(const GazetteChunked *c);

#ifdef __cplusplus
}
#endif

#endif /* GAZETTE_HTTP_H */
