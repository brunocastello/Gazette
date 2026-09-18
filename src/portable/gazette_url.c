/*
 * Gazette — absolute-URI splitting and redirect resolution
 * Copyright (c) 2026 brunocastello
 *
 * PORTABLE: no system headers. See gazette_url.h.
 */

#include "gazette_url.h"

#include "portable/gazette_portable.h"

#include <string.h>

int GazetteURLSplitAuthority(const char *s, size_t len, unsigned short defport,
                             char *host, size_t hostCap, unsigned short *port)
{
    size_t i;
    size_t hlen;
    long   p = defport;

    if (s == NULL || host == NULL || port == NULL || hostCap == 0) {
        return 0;
    }

    while (len > 0 && (*s == ' ' || *s == '\t')) { s++; len--; }
    while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t')) { len--; }
    if (len == 0) {
        return 0;
    }

    hlen = len;
    for (i = 0; i < len; i++) {
        if (s[i] == ':') {
            hlen = i;
            p = gz_parse_dec(s + i + 1, len - i - 1, -1);
            break;
        }
        if (s[i] == '/') {
            hlen = i;
            break;
        }
    }
    if (hlen == 0 || hlen >= hostCap) {
        return 0;
    }
    if (p < 1 || p > 65535) {
        return 0;
    }

    gz_copy_n(host, hostCap, s, hlen);
    *port = (unsigned short)p;
    return 1;
}

int GazetteURLSplit(const char *url, size_t len, GazetteURL *out)
{
    size_t         off;
    size_t         authEnd;
    unsigned short defport;

    if (url == NULL || out == NULL) {
        return 0;
    }

    memset(out, 0, sizeof *out);

    if (gz_starts_ci(url, len, "https://")) {
        out->tls = 1;
        defport  = 443;
        off      = 8;
    } else if (gz_starts_ci(url, len, "http://")) {
        out->tls = 0;
        defport  = 80;
        off      = 7;
    } else {
        return 0;
    }

    authEnd = off;
    while (authEnd < len && url[authEnd] != '/') {
        authEnd++;
    }

    if (!GazetteURLSplitAuthority(url + off, authEnd - off, defport,
                                  out->host, sizeof out->host, &out->port)) {
        return 0;
    }

    if (authEnd >= len) {
        out->path[0] = '/';
        out->path[1] = '\0';
    } else {
        if (len - authEnd >= sizeof out->path) {
            return 0;
        }
        gz_copy_n(out->path, sizeof out->path, url + authEnd, len - authEnd);
    }
    return 1;
}

int GazetteURLResolve(const GazetteURL *base, const char *loc, size_t locLen,
                      GazetteURL *out)
{
    if (base == NULL || loc == NULL || out == NULL) {
        return 0;
    }

    /* A Location value arrives with its line ending still attached when it is
       taken straight out of the header block. */
    while (locLen > 0 && (*loc == ' ' || *loc == '\t')) { loc++; locLen--; }
    while (locLen > 0 &&
           (loc[locLen - 1] == '\r' || loc[locLen - 1] == '\n' ||
            loc[locLen - 1] == ' '  || loc[locLen - 1] == '\t')) {
        locLen--;
    }
    if (locLen == 0) {
        return 0;
    }

    if (gz_starts_ci(loc, locLen, "http://") ||
        gz_starts_ci(loc, locLen, "https://")) {
        return GazetteURLSplit(loc, locLen, out);
    }

    /*
     * Scheme-relative: "//cdn.example/x.jpg" is an image source on half the
     * web, and it means the base's scheme on a different host. Split as an
     * absolute URL with that scheme put in front, and the port that comes
     * with it — the base's port is the base's host's.
     */
    if (locLen >= 2 && loc[0] == '/' && loc[1] == '/') {
        const char *auth    = loc + 2;
        size_t      authLen = 0;
        size_t      rest;

        while (authLen < locLen - 2 && auth[authLen] != '/' &&
               auth[authLen] != '?' && auth[authLen] != '#') {
            authLen++;
        }
        out->tls = base->tls;
        if (!GazetteURLSplitAuthority(auth, authLen,
                                      (unsigned short)(base->tls ? 443 : 80),
                                      out->host, sizeof out->host,
                                      &out->port)) {
            return 0;
        }
        rest = locLen - 2 - authLen;
        if (rest == 0 || auth[authLen] != '/') {
            /* No path, or a query with no path before it: origin-form
               always starts with a slash. */
            if (rest + 1 >= sizeof out->path) {
                return 0;
            }
            out->path[0] = '/';
            memcpy(out->path + 1, auth + authLen, rest);
            out->path[rest + 1] = '\0';
        } else {
            if (rest >= sizeof out->path) {
                return 0;
            }
            memcpy(out->path, auth + authLen, rest);
            out->path[rest] = '\0';
        }
        return 1;
    }

    /* Same origin; only the path changes. */
    *out = *base;

    if (loc[0] == '/') {
        if (locLen >= sizeof out->path) {
            return 0;
        }
        gz_copy_n(out->path, sizeof out->path, loc, locLen);
        return 1;
    }

    {
        /* Relative reference: replace the last segment of the base path. */
        size_t keep = strlen(base->path);

        while (keep > 0 && base->path[keep - 1] != '/') {
            keep--;
        }
        if (keep + locLen >= sizeof out->path) {
            return 0;
        }
        memcpy(out->path, base->path, keep);
        memcpy(out->path + keep, loc, locLen);
        out->path[keep + locLen] = '\0';
    }
    return 1;
}

size_t GazetteURLFormat(const GazetteURL *u, char *out, size_t cap)
{
    const char *scheme;
    size_t      need;
    size_t      len = 0;
    int         defaultPort;

    if (out == NULL || cap == 0) {
        return 0;
    }
    out[0] = '\0';
    if (u == NULL) {
        return 0;
    }

    scheme      = u->tls ? "https://" : "http://";
    defaultPort = u->tls ? (u->port == 443) : (u->port == 80);

    need = strlen(scheme) + strlen(u->host) + strlen(u->path);
    if (!defaultPort) {
        need += 6;                          /* ':' plus up to five digits */
    }
    if (need >= cap) {
        return 0;
    }

    len += gz_copy_n(out + len, cap - len, scheme, strlen(scheme));
    len += gz_copy_n(out + len, cap - len, u->host, strlen(u->host));

    if (!defaultPort) {
        char           digits[8];
        int            n = 0;
        unsigned short p = u->port;

        do {
            digits[n++] = (char)('0' + (p % 10));
            p = (unsigned short)(p / 10);
        } while (p != 0 && n < (int)sizeof digits);

        out[len++] = ':';
        while (n > 0) {
            out[len++] = digits[--n];
        }
        out[len] = '\0';
    }

    len += gz_copy_n(out + len, cap - len, u->path, strlen(u->path));
    return len;
}
