/*
 * Gazette — absolute-URI splitting and redirect resolution
 * Copyright (c) 2026 brunocastello
 *
 * PORTABLE: no system headers. Adapted from Gateway's gw_url, which has had
 * the awkward cases beaten out of it against real sites; the query-string
 * helpers it also carries are left behind until Phase 2's Google News work
 * actually needs them.
 */
#ifndef GAZETTE_URL_H
#define GAZETTE_URL_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    kGazetteMaxHost = 256,
    /*
     * Feed URLs are ordinary enough, but the redirects they go through are
     * not: a Google News item link carries its target encoded in the query
     * string and runs well past a kilobyte. Gateway found the same thing with
     * signed media URLs and settled on 4096 for the same reason.
     */
    kGazetteMaxPath = 4096
};

typedef struct {
    char           host[kGazetteMaxHost];
    unsigned short port;
    int            tls;                     /* 1 when the scheme was https */
    char           path[kGazetteMaxPath];   /* origin-form, always starts '/' */
} GazetteURL;

/*
 * Split an absolute URI ("http://host[:port]/path?query").
 * Returns 1 on success, 0 when the string is not an absolute http(s) URI.
 */
int GazetteURLSplit(const char *url, size_t len, GazetteURL *out);

/*
 * Split an authority ("host" or "host:port"). defport is used when no port is
 * present. Returns 1 on success.
 */
int GazetteURLSplitAuthority(const char *s, size_t len, unsigned short defport,
                             char *host, size_t hostCap, unsigned short *port);

/*
 * Resolve a Location header against the request it answered. Handles absolute
 * URIs, absolute paths ("/x") and relative references ("x"). Returns 1 on
 * success. base and out may not be the same object.
 */
int GazetteURLResolve(const GazetteURL *base, const char *loc, size_t locLen,
                      GazetteURL *out);

/*
 * Reassemble a URL into its printed form, for logging and for the "fetching…"
 * status line. Returns the length written, or 0 if it would not fit.
 */
size_t GazetteURLFormat(const GazetteURL *u, char *out, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* GAZETTE_URL_H */
