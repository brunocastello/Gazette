/*
 * Gazette — non-blocking HTTP GET with redirects
 * Copyright (c) 2026 brunocastello
 *
 * See gazette_fetch.h. The state machine is deliberately flat: one switch,
 * one transition per pass at most, and every wait expressed as "not yet,
 * come back" rather than a loop. That is what keeps a slow feed from holding
 * the event loop, and it is the shape constraint 7 asks for.
 */

#include "gazette_fetch.h"

#include "net/gazette_net.h"
#include "portable/gazette_http.h"
#include "portable/gazette_portable.h"
#include "portable/gazette_url.h"

#include <MacMemory.h>          /* NewPtrClear, DisposePtr */

#include <stdio.h>
#include <string.h>

enum {
    /*
     * Response heads are normally under 2 KB. Google News sends a long
     * Set-Cookie block and CDNs add a dozen tracing headers, so 8 KB is the
     * comfortable size rather than the tight one; a head that does not fit is
     * a server doing something Gazette has no business accommodating.
     */
    kHeadMax     = 8192,

    /* One socket read. Bigger buys nothing: Open Transport hands over what it
       has, and the sink is called once per read either way. */
    kIOBufSize   = 4096,

    /* Request line + Host + the fixed headers, with the path being the only
       part that can be long. */
    kRequestMax  = kGazetteMaxPath + 512,

    /*
     * Redirect chains: Google News alone spends three hops getting from a
     * topic URL to the feed. Five leaves room without letting a redirect loop
     * run forever.
     */
    kMaxRedirects = 5,

    /*
     * Ticks without progress before the fetch is abandoned. The connect and
     * handshake have their own timeouts inside Certainly; this one covers a
     * connection that opens and then stops saying anything, which no layer
     * below notices.
     */
    kIdleTimeout = 45 * 60
};

struct GazetteFetch {
    GazetteFetchState state;

    GazetteURL        url;              /* what is being fetched right now */
    GazetteStream     stream;
    int               redirects;

    GazetteFetchSink  sink;
    void             *context;

    char              request[kRequestMax];
    size_t            requestLen;
    size_t            requestSent;

    char              head[kHeadMax];
    size_t            headHave;         /* bytes accumulated in head[]     */

    GazetteHTTPResponse res;

    /* Body framing: exactly one of these three applies. */
    int               bodyChunked;
    int               bodyByEOF;
    long              bodyRemaining;    /* when Content-Length was given   */
    GazetteChunked    chunked;

    long              bytesRead;

    char              io[kIOBufSize];
    char              decoded[kIOBufSize];

    unsigned long     lastProgress;
    char              finalURL[kGazetteMaxPath + kGazetteMaxHost + 16];
    char              errorText[160];
};

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static void Fail(GazetteFetch *f, const char *what)
{
    char detail[96];

    /* The stream's own description says how far the connection got, which is
       the difference between a name that will not resolve and an address that
       will not answer. Both look like "could not fetch" without it.

       It is only worth appending once there is a connection to describe: a
       URL rejected before any of this would otherwise be reported as
       "... (not connected)", which reads like a second, unrelated fault. */
    detail[0] = '\0';
    if (f->stream.plain != NULL || f->stream.sec != NULL) {
        GazetteStreamDescribe(&f->stream, detail, sizeof detail);
    }
    if (detail[0] != '\0') {
        snprintf(f->errorText, sizeof f->errorText, "%s (%s)", what, detail);
    } else {
        snprintf(f->errorText, sizeof f->errorText, "%s", what);
    }
    f->state = kGazetteFetchFailed;
}

static void NoteProgress(GazetteFetch *f)
{
    f->lastProgress = GazetteNetTicks();
}

/* Open the connection for f->url and prepare the request. */
static int BeginRequest(GazetteFetch *f)
{
    GazetteURLFormat(&f->url, f->finalURL, sizeof f->finalURL);

    f->requestLen = GazetteHTTPBuildGet(&f->url, f->request, sizeof f->request);
    if (f->requestLen == 0) {
        Fail(f, "URL is too long to request");
        return 0;
    }
    f->requestSent = 0;
    f->headHave    = 0;
    f->bytesRead   = 0;

    if (!GazetteStreamConnect(&f->stream, f->url.host, f->url.port,
                              f->url.tls)) {
        Fail(f, "could not open a connection");
        return 0;
    }

    NoteProgress(f);
    f->state = kGazetteFetchConnecting;
    return 1;
}

/*
 * Hand len bytes of body to the sink, de-chunking on the way if the response
 * is chunked. Returns 0 when the fetch should stop — either the sink asked to
 * or the chunked stream is malformed.
 */
static int DeliverBody(GazetteFetch *f, const char *data, size_t len)
{
    if (len == 0) {
        return 1;
    }

    if (!f->bodyChunked) {
        f->bytesRead += (long)len;
        return (f->sink == NULL) ? 1 : f->sink(data, len, f->context);
    }

    while (len > 0) {
        size_t produced = 0;
        long   consumed = GazetteChunkedFeed(&f->chunked, data, len,
                                             f->decoded, sizeof f->decoded,
                                             &produced);

        if (consumed < 0) {
            Fail(f, "malformed chunked response");
            return 0;
        }
        if (produced > 0) {
            f->bytesRead += (long)produced;
            if (f->sink != NULL &&
                !f->sink(f->decoded, produced, f->context)) {
                return 0;
            }
        }
        if (consumed == 0 && produced == 0) {
            break;                      /* needs more input than we have */
        }
        data += consumed;
        len  -= (size_t)consumed;

        if (GazetteChunkedDone(&f->chunked)) {
            break;                      /* trailing bytes after the 0 chunk */
        }
    }
    return 1;
}

/* True once the body is complete by whichever framing applies. */
static int BodyComplete(const GazetteFetch *f)
{
    if (f->bodyChunked) {
        return GazetteChunkedDone(&f->chunked);
    }
    if (f->bodyByEOF) {
        return 0;                       /* only the close ends it */
    }
    return f->bodyRemaining <= 0;
}

/*
 * A 3xx worth following. Resolves the Location against the URL that produced
 * it, so a relative redirect works, and returns 0 when the chain should stop
 * (too many hops, or a Location that will not parse) — in which case the
 * response is treated as the final one, which is what a browser shows.
 */
static int FollowRedirect(GazetteFetch *f)
{
    GazetteURL next;

    if (f->redirects >= kMaxRedirects) {
        Fail(f, "too many redirects");
        return 0;
    }
    if (!GazetteURLResolve(&f->url, f->res.location,
                           strlen(f->res.location), &next)) {
        Fail(f, "redirect to an address Gazette cannot read");
        return 0;
    }

    f->redirects++;
    GazetteStreamDestroy(&f->stream);
    f->url = next;

    return BeginRequest(f);
}

/* Decide how the body is framed, and set the state that reads it. */
static void StartBody(GazetteFetch *f)
{
    f->bodyChunked   = f->res.chunked;
    f->bodyRemaining = f->res.hasContentLength ? f->res.contentLength : 0;
    f->bodyByEOF     = (!f->res.chunked && !f->res.hasContentLength);

    if (f->bodyChunked) {
        GazetteChunkedInit(&f->chunked);
    }

    /*
     * A status that can never carry a body ends here whatever the headers
     * said. Without this a 204 or a 304 would be read until the connection
     * closed, which for a keep-alive origin is never.
     */
    if (GazetteHTTPStatusHasNoBody(f->res.status)) {
        f->state = kGazetteFetchDone;
        return;
    }

    f->state = kGazetteFetchBody;
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

GazetteFetch *GazetteFetchStart(const char *url,
                                GazetteFetchSink sink, void *context)
{
    GazetteFetch *f;

    if (url == NULL || url[0] == '\0' || !GazetteNetIsUp()) {
        return NULL;
    }

    f = (GazetteFetch *)NewPtrClear((Size)sizeof(GazetteFetch));
    if (f == NULL) {
        return NULL;
    }

    if (!GazetteURLSplit(url, strlen(url), &f->url)) {
        DisposePtr((Ptr)f);
        return NULL;
    }

    f->sink    = sink;
    f->context = context;
    GazetteStreamInit(&f->stream);

    if (!BeginRequest(f)) {
        /* BeginRequest already recorded why; the caller reads it and then
           destroys the fetch, so the block stays alive to be read. */
        return f;
    }
    return f;
}

void GazetteFetchDestroy(GazetteFetch *f)
{
    if (f == NULL) {
        return;
    }
    GazetteStreamDestroy(&f->stream);
    DisposePtr((Ptr)f);
}

/* ------------------------------------------------------------------ */
/* The pump                                                            */
/* ------------------------------------------------------------------ */

GazetteFetchState GazetteFetchPump(GazetteFetch *f)
{
    GazetteStreamState net;

    if (f == NULL) {
        return kGazetteFetchFailed;
    }
    if (f->state == kGazetteFetchDone || f->state == kGazetteFetchFailed) {
        return f->state;
    }

    if (GazetteNetTicks() - f->lastProgress > kIdleTimeout) {
        Fail(f, "the server stopped responding");
        return f->state;
    }

    net = GazetteStreamPump(&f->stream);
    if (net == kGazetteStreamError) {
        Fail(f, "the connection failed");
        return f->state;
    }

    switch (f->state) {
        case kGazetteFetchConnecting:
            if (net == kGazetteStreamReady) {
                NoteProgress(f);
                f->state = kGazetteFetchSending;
            } else if (net == kGazetteStreamClosed) {
                Fail(f, "the server closed the connection");
            }
            break;

        case kGazetteFetchSending: {
            long n = GazetteStreamWrite(&f->stream,
                                        f->request + f->requestSent,
                                        f->requestLen - f->requestSent);
            if (n < 0) {
                Fail(f, "could not send the request");
                break;
            }
            if (n > 0) {
                /* A partial write is ordinary, not an error: the rest goes
                   next pass, from where this one stopped. */
                f->requestSent += (size_t)n;
                NoteProgress(f);
            }
            if (f->requestSent >= f->requestLen) {
                f->state = kGazetteFetchHeaders;
            }
            break;
        }

        case kGazetteFetchHeaders: {
            long n;

            if (f->headHave >= sizeof f->head) {
                Fail(f, "the response headers are too large");
                break;
            }

            n = GazetteStreamRead(&f->stream, f->head + f->headHave,
                                  sizeof f->head - f->headHave);
            if (n == 0) {
                break;                              /* nothing yet */
            }
            if (n == -1) {
                Fail(f, "the connection failed while reading headers");
                break;
            }
            if (n == -2) {
                Fail(f, "the server closed before sending a response");
                break;
            }

            f->headHave += (size_t)n;
            NoteProgress(f);

            {
                int parsed = GazetteHTTPParseResponse(f->head, f->headHave,
                                                      &f->res);
                if (parsed < 0) {
                    Fail(f, "the server sent something that is not HTTP");
                    break;
                }
                if (parsed == 0) {
                    break;                          /* head still incomplete */
                }
            }

            if (GazetteHTTPIsRedirect(f->res.status) && f->res.hasLocation) {
                (void)FollowRedirect(f);
                break;
            }

            StartBody(f);

            /*
             * Whatever came in after the blank line is already body. It has to
             * go through the same path as everything read later, or the first
             * few kilobytes of every response would be lost.
             */
            if (f->state == kGazetteFetchBody &&
                f->headHave > f->res.headLen) {
                size_t extra = f->headHave - f->res.headLen;

                if (!f->bodyChunked && !f->bodyByEOF) {
                    if ((long)extra > f->bodyRemaining) {
                        extra = (size_t)f->bodyRemaining;
                    }
                    f->bodyRemaining -= (long)extra;
                }
                if (!DeliverBody(f, f->head + f->res.headLen, extra)) {
                    if (f->state != kGazetteFetchFailed) {
                        f->state = kGazetteFetchDone;   /* the sink stopped us */
                    }
                    break;
                }
                if (BodyComplete(f)) {
                    f->state = kGazetteFetchDone;
                }
            }
            break;
        }

        case kGazetteFetchBody: {
            long   n;
            size_t want = sizeof f->io;

            if (!f->bodyChunked && !f->bodyByEOF &&
                (long)want > f->bodyRemaining) {
                want = (size_t)f->bodyRemaining;
            }
            if (want == 0) {
                f->state = kGazetteFetchDone;
                break;
            }

            n = GazetteStreamRead(&f->stream, f->io, want);
            if (n == 0) {
                break;
            }
            if (n == -1) {
                Fail(f, "the connection failed while reading the page");
                break;
            }
            if (n == -2) {
                /*
                 * The peer closed. For an EOF-delimited body that is the
                 * end and the fetch succeeded; for anything else the body
                 * was cut short, and saying so is better than handing the
                 * feed parser half a document.
                 */
                if (f->bodyByEOF || BodyComplete(f)) {
                    f->state = kGazetteFetchDone;
                } else {
                    Fail(f, "the server closed before sending the whole page");
                }
                break;
            }

            NoteProgress(f);
            if (!f->bodyChunked && !f->bodyByEOF) {
                f->bodyRemaining -= n;
            }
            if (!DeliverBody(f, f->io, (size_t)n)) {
                if (f->state != kGazetteFetchFailed) {
                    f->state = kGazetteFetchDone;
                }
                break;
            }
            if (BodyComplete(f)) {
                f->state = kGazetteFetchDone;
            }
            break;
        }

        default:
            break;
    }

    return f->state;
}

/* ------------------------------------------------------------------ */
/* Accessors                                                           */
/* ------------------------------------------------------------------ */

GazetteFetchState GazetteFetchGetState(const GazetteFetch *f)
{
    return (f == NULL) ? kGazetteFetchFailed : f->state;
}

int GazetteFetchStatus(const GazetteFetch *f)
{
    return (f == NULL) ? 0 : f->res.status;
}

long GazetteFetchBytesRead(const GazetteFetch *f)
{
    return (f == NULL) ? 0 : f->bytesRead;
}

const char *GazetteFetchFinalURL(const GazetteFetch *f)
{
    return (f == NULL) ? "" : f->finalURL;
}

const char *GazetteFetchErrorText(const GazetteFetch *f)
{
    return (f == NULL) ? "" : f->errorText;
}
