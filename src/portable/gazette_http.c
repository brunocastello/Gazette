/*
 * Gazette — HTTP/1.1 client wire format
 * Copyright (c) 2026 brunocastello
 *
 * PORTABLE: no system headers. See gazette_http.h.
 */

#include "gazette_http.h"

#include "portable/gazette_portable.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Requests                                                            */
/* ------------------------------------------------------------------ */

/* Append src, tracking the running length. Once the buffer is full the length
   keeps growing past cap so the caller can tell it overflowed. */
static void Append(char *out, size_t cap, size_t *len, const char *src)
{
    size_t n = strlen(src);

    if (*len + n < cap) {
        memcpy(out + *len, src, n);
    }
    *len += n;
}

/* The request line and the headers common to both methods, up to and not
   including the blank line that ends the head. */
static size_t BuildHead(const GazetteURL *url, const char *method,
                        char *out, size_t cap)
{
    size_t len = 0;

    if (out == NULL || cap == 0) {
        return 0;
    }
    out[0] = '\0';
    if (url == NULL || url->host[0] == '\0') {
        return 0;
    }

    Append(out, cap, &len, method);
    Append(out, cap, &len, " ");
    Append(out, cap, &len, url->path[0] ? url->path : "/");
    Append(out, cap, &len, " HTTP/1.1\r\nHost: ");
    Append(out, cap, &len, url->host);

    /*
     * A non-default port belongs in Host:, and only then. Sending "Host:
     * example.com:80" is legal but is enough to miss a virtual host on servers
     * that compare the header literally.
     */
    if (!(url->tls ? (url->port == 443) : (url->port == 80))) {
        char           digits[8];
        int            n = 0;
        unsigned short p = url->port;

        do {
            digits[n++] = (char)('0' + (p % 10));
            p = (unsigned short)(p / 10);
        } while (p != 0 && n < (int)sizeof digits);

        Append(out, cap, &len, ":");
        while (n > 0) {
            char one[2];
            one[0] = digits[--n];
            one[1] = '\0';
            Append(out, cap, &len, one);
        }
    }

    Append(out, cap, &len, "\r\nUser-Agent: " kGazetteUserAgent);
    Append(out, cap, &len, "\r\nAccept: application/rss+xml, application/atom+xml, "
                           "application/xml, text/xml, */*");
    Append(out, cap, &len, "\r\nAccept-Encoding: identity");
    Append(out, cap, &len, "\r\nConnection: close");
    return len;
}

size_t GazetteHTTPBuildGet(const GazetteURL *url, char *out, size_t cap)
{
    size_t len = BuildHead(url, "GET", out, cap);

    if (len == 0) {
        return 0;
    }
    Append(out, cap, &len, "\r\n\r\n");

    if (len >= cap) {
        out[0] = '\0';
        return 0;
    }
    out[len] = '\0';
    return len;
}

size_t GazetteHTTPBuildPost(const GazetteURL *url, const char *contentType,
                            const char *body, size_t bodyLen,
                            char *out, size_t cap)
{
    size_t len = BuildHead(url, "POST", out, cap);
    char   digits[16];
    int    n = 0;
    size_t v = bodyLen;

    if (len == 0 || body == NULL) {
        return 0;
    }
    Append(out, cap, &len, "\r\nContent-Type: ");
    Append(out, cap, &len, contentType ? contentType
                                       : "application/x-www-form-urlencoded");
    Append(out, cap, &len, "\r\nContent-Length: ");
    do {
        digits[n++] = (char)('0' + (v % 10));
        v /= 10;
    } while (v != 0 && n < (int)sizeof digits);
    while (n > 0) {
        char one[2];
        one[0] = digits[--n];
        one[1] = '\0';
        Append(out, cap, &len, one);
    }
    Append(out, cap, &len, "\r\n\r\n");

    /* The body is bytes, not text: copied whole rather than appended as a
       string, and only when the lot fits. */
    if (len + bodyLen >= cap) {
        out[0] = '\0';
        return 0;
    }
    memcpy(out + len, body, bodyLen);
    len += bodyLen;
    out[len] = '\0';
    return len;
}

/* ------------------------------------------------------------------ */
/* Responses                                                           */
/* ------------------------------------------------------------------ */

int GazetteHTTPParseResponse(const char *buf, size_t len,
                             GazetteHTTPResponse *res)
{
    size_t      headLen;
    size_t      vLen;
    const char *v;

    if (buf == NULL || res == NULL) {
        return -1;
    }

    memset(res, 0, sizeof *res);
    res->contentLength = -1;

    if (!gz_find_head_end(buf, len, &headLen)) {
        return 0;
    }
    res->headLen = headLen;

    if (!gz_starts_ci(buf, headLen, "HTTP/1.")) {
        return -1;
    }
    res->httpMinor = (buf[7] == '1') ? 1 : 0;

    {
        /* buf + 8 is the space before the code; gz_parse_dec skips it. */
        long st = gz_parse_dec(buf + 8, headLen - 8, -1);
        if (st < 100 || st > 599) {
            return -1;
        }
        res->status = (int)st;
    }

    v = gz_header_find(buf, headLen, "Transfer-Encoding", &vLen);
    if (v != NULL && vLen >= 7 && gz_strnicmp(v, "chunked", 7) == 0) {
        res->chunked = 1;
    }

    v = gz_header_find(buf, headLen, "Content-Length", &vLen);
    if (v != NULL) {
        long cl = gz_parse_dec(v, vLen, -1);
        if (cl >= 0) {
            res->hasContentLength = 1;
            res->contentLength    = cl;
        }
    }

    v = gz_header_find(buf, headLen, "Connection", &vLen);
    if (v != NULL && vLen >= 5 && gz_strnicmp(v, "close", 5) == 0) {
        res->connectionClose = 1;
    }
    if (res->httpMinor == 0 && v == NULL) {
        res->connectionClose = 1;       /* HTTP/1.0 closes unless told not to */
    }

    v = gz_header_find(buf, headLen, "Location", &vLen);
    if (v != NULL && vLen > 0 && vLen < sizeof res->location) {
        gz_copy_n(res->location, sizeof res->location, v, vLen);
        res->hasLocation = 1;
    }

    /*
     * Transfer-Encoding wins over Content-Length when both are present
     * (RFC 9112 6.3). A response carrying both is either a broken server or a
     * request-smuggling attempt, and either way the framing that matters is
     * the chunked one.
     */
    if (res->chunked) {
        res->hasContentLength = 0;
        res->contentLength    = -1;
    }

    return 1;
}

int GazetteHTTPIsRedirect(int status)
{
    switch (status) {
        case 301:       /* Moved Permanently */
        case 302:       /* Found            */
        case 303:       /* See Other        */
        case 307:       /* Temporary Redirect */
        case 308:       /* Permanent Redirect */
            return 1;
        default:
            return 0;
    }
}

int GazetteHTTPStatusHasNoBody(int status)
{
    return (status >= 100 && status < 200) || status == 204 || status == 304;
}

/* ------------------------------------------------------------------ */
/* Chunked transfer-coding                                             */
/* ------------------------------------------------------------------ */

void GazetteChunkedInit(GazetteChunked *c)
{
    if (c == NULL) {
        return;
    }
    memset(c, 0, sizeof *c);
    c->state = kGazetteChunkSize;
}

static int HexVal(int ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

long GazetteChunkedFeed(GazetteChunked *c, const char *in, size_t inLen,
                        char *out, size_t outCap, size_t *outLen)
{
    size_t i        = 0;
    size_t produced = 0;

    if (c == NULL || outLen == NULL) {
        return -1;
    }
    *outLen = 0;
    if (c->state == kGazetteChunkError) {
        return -1;
    }
    if (in == NULL || out == NULL) {
        return -1;
    }

    while (i < inLen && c->state != kGazetteChunkDone) {
        unsigned char ch = (unsigned char)in[i];

        switch (c->state) {
            case kGazetteChunkSize: {
                int hv = HexVal(ch);

                if (hv >= 0 && !c->cr) {
                    if (c->remaining > 0x00FFFFFFL) {   /* 16 MiB chunk: no */
                        c->state = kGazetteChunkError;
                        return -1;
                    }
                    c->remaining = c->remaining * 16 + hv;
                    c->seenDigit = 1;
                    i++;
                    break;
                }
                if (ch == '\r') {
                    c->cr = 1;
                    i++;
                    break;
                }
                if (ch == '\n') {
                    if (!c->seenDigit) {
                        c->state = kGazetteChunkError;
                        return -1;
                    }
                    c->cr        = 0;
                    c->seenDigit = 0;
                    if (c->remaining == 0) {
                        c->state = kGazetteChunkTrailer;
                        /* A trailer section that starts with a blank line ends
                           immediately, which is the no-trailer case and so the
                           usual one. */
                        c->trailerBlank = 1;
                    } else {
                        c->state = kGazetteChunkData;
                    }
                    i++;
                    break;
                }
                /* A chunk extension (";name=value") starts here. Setting cr
                   stops the digit scanner, since extension text may itself
                   contain hex characters. */
                c->cr = 1;
                i++;
                break;
            }

            case kGazetteChunkData: {
                size_t avail = inLen - i;
                size_t room  = outCap - produced;
                size_t n     = (size_t)c->remaining;

                if (room == 0) {
                    goto done;
                }
                if (n > avail) n = avail;
                if (n > room)  n = room;
                memcpy(out + produced, in + i, n);
                produced     += n;
                i            += n;
                c->remaining -= (long)n;
                if (c->remaining == 0) {
                    c->state = kGazetteChunkDataCRLF;
                }
                break;
            }

            case kGazetteChunkDataCRLF:
                if (ch == '\n') {
                    c->state     = kGazetteChunkSize;
                    c->remaining = 0;
                }
                i++;
                break;

            case kGazetteChunkTrailer:
                if (ch == '\n') {
                    if (c->trailerBlank) {
                        c->state = kGazetteChunkDone;
                        i++;
                        break;
                    }
                    c->trailerBlank = 1;
                } else if (ch != '\r') {
                    c->trailerBlank = 0;
                }
                i++;
                break;

            default:
                goto done;
        }
    }

done:
    *outLen = produced;
    return (long)i;
}

int GazetteChunkedDone(const GazetteChunked *c)
{
    return (c != NULL && c->state == kGazetteChunkDone) ? 1 : 0;
}
