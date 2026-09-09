/*
 * Gazette — network stream: one interface over plain TCP and TLS
 * Copyright (c) 2026 brunocastello
 *
 * See gazette_net.h for why the plain path is Certainly's own transport
 * rather than a second Open Transport layer of Gazette's.
 */

/* certainly.h reaches certainly_transport.h, which includes <OpenTransport.h>
   before <stdbool.h> on purpose — MacTypes.h declares true and false as
   enumerators, and a stdbool that got in first turns that into
   "enum { 0 = 0, 1 = 1 }" and the Universal Interfaces stop compiling. Keep it
   first here for the same reason. */
#include <certainly.h>
#include <certainly_transport.h>

#include <OpenTransport.h>      /* InitOpenTransport / CloseOpenTransport */
#include <Events.h>             /* TickCount */

#include <stdio.h>
#include <string.h>

#include "gazette_net.h"

static int gNetUp = 0;

/* ------------------------------------------------------------------ */
/* Library lifecycle                                                   */
/* ------------------------------------------------------------------ */

int GazetteNetInit(void)
{
    OSStatus err;

    if (gNetUp) {
        return 1;
    }

    /*
     * Under Carbon this is a macro for
     * InitOpenTransportInContext(kInitOTForApplicationMask, NULL) — the plain
     * entry point is "CarbonLib: not available". See PATCHES.md §22 for why
     * that redirection needs OTCARBONAPPLICATION=1 from the build.
     *
     * Certainly never calls this itself; it assumes the application has. On
     * a machine with no TCP/IP configured it fails here rather than at
     * connect time, which is the useful place for it to fail.
     */
    err = InitOpenTransport();
    if (err != noErr) {
        return 0;
    }

    if (MacTLS_Init() != kMacTLS_OK) {
        CloseOpenTransport();
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
    CloseOpenTransport();
    gNetUp = 0;
}

int GazetteNetIsUp(void)
{
    return gNetUp;
}

unsigned long GazetteNetTicks(void)
{
    return (unsigned long)TickCount();
}

/* ------------------------------------------------------------------ */
/* Streams                                                            */
/* ------------------------------------------------------------------ */

void GazetteStreamInit(GazetteStream *s)
{
    if (s == NULL) {
        return;
    }
    memset(s, 0, sizeof *s);
    s->state = kGazetteStreamIdle;
}

int GazetteStreamConnect(GazetteStream *s, const char *host,
                         unsigned short port, int useTLS)
{
    if (s == NULL || host == NULL || host[0] == '\0') {
        return 0;
    }

    GazetteStreamInit(s);
    s->startTicks = GazetteNetTicks();

    if (!gNetUp) {
        s->state = kGazetteStreamError;
        return 0;
    }

    if (useTLS) {
        s->tls = 1;
        s->sec = MacTLS_Create(host, (uint16_t)port);
        /* MacTLS_Create returns NULL only on allocation failure; every other
           failure comes back as a live context already in the error state, so
           checking for NULL alone would miss most of them. */
        if (s->sec == NULL || MacTLS_GetState(s->sec) == kMacTLS_Error) {
            s->state = kGazetteStreamError;
            return 0;
        }
    } else {
        s->plain = ct_transport_create(host, (uint16_t)port);
        if (s->plain == NULL) {
            s->state = kGazetteStreamError;
            return 0;
        }
    }

    s->state = kGazetteStreamConnecting;
    return 1;
}

GazetteStreamState GazetteStreamPump(GazetteStream *s)
{
    if (s == NULL) {
        return kGazetteStreamError;
    }

    if (s->tls) {
        if (s->sec == NULL) {
            s->state = kGazetteStreamError;
            return s->state;
        }
        switch (MacTLS_Pump(s->sec)) {
            case kMacTLS_Connected:
                s->state = kGazetteStreamReady;
                break;
            case kMacTLS_Closed:
                s->state = kGazetteStreamClosed;
                break;
            case kMacTLS_Error:
                s->state = kGazetteStreamError;
                break;
            default:
                /* Idle, Connecting, Handshaking, Closing. A stream that has
                   already gone ready stays ready while it drains. */
                if (s->state != kGazetteStreamReady) {
                    s->state = kGazetteStreamConnecting;
                }
                break;
        }
        return s->state;
    }

    if (s->plain == NULL) {
        s->state = kGazetteStreamError;
        return s->state;
    }

    switch (ct_transport_pump(s->plain)) {
        case kCTransport_Connected:
            s->state = kGazetteStreamReady;
            break;
        case kCTransport_Closed:
            s->state = kGazetteStreamClosed;
            break;
        case kCTransport_Error:
            s->state = kGazetteStreamError;
            break;
        case kCTransport_Closing:
            /* Half-closed: the peer is done sending, we may still be. */
            break;
        default:
            s->state = kGazetteStreamConnecting;
            break;
    }
    return s->state;
}

long GazetteStreamWrite(GazetteStream *s, const void *buf, size_t len)
{
    if (s == NULL) {
        return -1;
    }
    if (len == 0) {
        return 0;
    }

    if (s->tls) {
        int n;
        if (s->sec == NULL) {
            return -1;
        }
        n = MacTLS_Write(s->sec, buf, len);
        return (n < 0) ? -1 : (long)n;
    }

    if (s->plain == NULL) {
        return -1;
    }
    return (long)ct_transport_send(s->plain, buf, len);
}

long GazetteStreamRead(GazetteStream *s, void *buf, size_t len)
{
    if (s == NULL) {
        return -1;
    }
    if (len == 0) {
        return 0;
    }

    if (s->tls) {
        int n;

        if (s->sec == NULL) {
            return -1;
        }
        n = MacTLS_Read(s->sec, buf, len);
        if (n > 0) {
            return n;
        }
        if (n < 0) {
            return -1;
        }
        /* Nothing buffered. A closed session with an empty buffer is EOF. */
        if (MacTLS_GetState(s->sec) == kMacTLS_Closed ||
            MacTLS_GetState(s->sec) == kMacTLS_Closing) {
            s->eof = 1;
            return -2;
        }
        return 0;
    }

    if (s->plain == NULL) {
        return -1;
    }

    {
        int n = ct_transport_recv(s->plain, buf, len);

        if (n > 0) {
            return n;
        }
        if (n == 0) {
            return 0;
        }

        /*
         * ct_transport_recv() returns -1 both for a broken connection and for
         * "the peer sent FIN and there is nothing left", which is a
         * distinction Certainly does not need and Gazette does: an HTTP/1.0
         * response with no Content-Length is delimited by exactly that close,
         * and treating it as an error would throw the body away. Asking the
         * transport whether the peer closed separates the two.
         */
        if (ct_transport_peer_closed(s->plain)) {
            s->eof = 1;
            return -2;
        }
        return -1;
    }
}

void GazetteStreamClose(GazetteStream *s)
{
    if (s == NULL) {
        return;
    }
    if (s->tls) {
        if (s->sec != NULL) {
            MacTLS_Close(s->sec);       /* frees the context as well */
            s->sec = NULL;
        }
    } else if (s->plain != NULL) {
        ct_transport_close(s->plain);
    }
    s->state = kGazetteStreamClosed;
}

void GazetteStreamDestroy(GazetteStream *s)
{
    if (s == NULL) {
        return;
    }
    if (s->sec != NULL) {
        MacTLS_Close(s->sec);
        s->sec = NULL;
    }
    if (s->plain != NULL) {
        ct_transport_destroy(s->plain);
        s->plain = NULL;
    }
    s->state = kGazetteStreamClosed;
}

int GazetteStreamTLSVersion(const GazetteStream *s)
{
    if (s == NULL || !s->tls || s->sec == NULL) {
        return 0;
    }
    switch (MacTLS_GetVersion(s->sec)) {
        case kMacTLS_Version12: return 12;
        case kMacTLS_Version13: return 13;
        default:                return 0;
    }
}

/* ------------------------------------------------------------------ */
/* Diagnostics                                                         */
/* ------------------------------------------------------------------ */

static const char *TLSPhaseText(MacTLS_Phase phase)
{
    switch (phase) {
        case kMacTLS_PhaseIdle:       return "idle";
        case kMacTLS_PhaseResolving:  return "resolving DNS";
        case kMacTLS_PhaseConnecting: return "connecting TCP";
        case kMacTLS_PhaseConnected:  return "connected";
        case kMacTLS_PhaseClosing:    return "closing";
        case kMacTLS_PhaseClosed:     return "closed";
        default:                      return "failed";
    }
}

static const char *PlainStateText(CTransportState state)
{
    switch (state) {
        case kCTransport_Idle:         return "idle";
        case kCTransport_ResolvingDNS: return "resolving DNS";
        case kCTransport_Connecting:   return "connecting TCP";
        case kCTransport_Connected:    return "connected";
        case kCTransport_Closing:      return "closing";
        case kCTransport_Closed:       return "closed";
        default:                       return "failed";
    }
}

const char *GazetteStreamDescribe(const GazetteStream *s, char *out, size_t cap)
{
    if (out == NULL || cap == 0) {
        return "";
    }
    out[0] = '\0';
    if (s == NULL) {
        return out;
    }

    if (s->tls) {
        if (s->sec == NULL) {
            snprintf(out, cap, "TLS not started");
            return out;
        }
        snprintf(out, cap, "TLS %s, OT %ld, alert %u",
                 TLSPhaseText(MacTLS_GetPhase(s->sec)),
                 MacTLS_GetTransportError(s->sec),
                 MacTLS_GetAlert(s->sec));
        return out;
    }

    if (s->plain == NULL) {
        snprintf(out, cap, "not connected");
        return out;
    }
    snprintf(out, cap, "%s, OT %ld",
             PlainStateText(ct_transport_state(s->plain)),
             ct_transport_last_error(s->plain));
    return out;
}
