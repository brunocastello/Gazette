/*
 * Gazette — network stream: one interface over plain TCP and TLS
 * Copyright (c) 2026 brunocastello
 *
 * Constraint 4: the TLS is Gateway's, vendored unchanged in
 * third_party/certainly. What is different here is how little sits on top of
 * it. Gateway needs its own Open Transport layer (src/net/gw_net.c) because it
 * is a proxy: it listens, accepts, and upgrades live plaintext connections to
 * TLS in place. Gazette does none of those things — it makes outbound client
 * connections and nothing else — and Certainly already carries a complete
 * non-blocking Open Transport client in certainly_transport.h. So the plain
 * side of this stream is that same transport, driven directly, and the ~450
 * lines of endpoint plumbing Gateway keeps have no counterpart here.
 *
 * No system headers: both connection types are forward-declared, so a caller
 * can hold a GazetteStream without dragging Open Transport into its
 * translation unit. Nothing here is host-testable, though — it links against
 * Open Transport either way.
 *
 * Conventions, inherited from Gateway's gw_transport.h because every state
 * machine above depends on them:
 *
 *   - Nothing blocks. Every call returns at once, and Pump does whatever small
 *     amount of work is available this pass.
 *   - Write returning 0 means flow-controlled, try the same bytes again next
 *     slice. It is not an error and not a closed connection.
 *   - Read distinguishes four cases: >0 bytes, 0 for nothing right now, -1 for
 *     a broken connection, -2 for an orderly close by the peer. Collapsing 0
 *     and -2 breaks any body that is delimited by the close.
 */
#ifndef GAZETTE_NET_H
#define GAZETTE_NET_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Certainly's two connection objects, neither of which this header defines. */
struct CTransport;
struct MacTLS_Context;

typedef enum {
    kGazetteStreamIdle = 0,
    kGazetteStreamConnecting,
    kGazetteStreamReady,
    kGazetteStreamClosed,
    kGazetteStreamError
} GazetteStreamState;

typedef struct {
    int                    tls;
    struct CTransport     *plain;
    struct MacTLS_Context *sec;
    GazetteStreamState     state;
    int                    eof;
    unsigned long          startTicks;
} GazetteStream;

/* ------------------------------------------------------------------ */
/* Library lifecycle                                                   */
/* ------------------------------------------------------------------ */

/* Open Transport and Certainly, once each, before any stream is opened.
   Returns 1 on success. Safe to call more than once. */
int  GazetteNetInit(void);
void GazetteNetShutdown(void);

/* 1 once GazetteNetInit() has succeeded. The UI shows a different status when
   networking never came up, which on Mac OS 9 usually means TCP/IP is not
   configured rather than anything Gazette did. */
int  GazetteNetIsUp(void);

/* Ticks, for timeouts kept by the layers above this one. */
unsigned long GazetteNetTicks(void);

/* ------------------------------------------------------------------ */
/* Streams                                                            */
/* ------------------------------------------------------------------ */

void GazetteStreamInit(GazetteStream *s);

/* Start connecting. Never blocks: name resolution and the TCP handshake both
   proceed under GazetteStreamPump. useTLS selects Certainly over the raw
   transport. Returns 1 if the attempt started. */
int  GazetteStreamConnect(GazetteStream *s, const char *host,
                          unsigned short port, int useTLS);

/* One slice of the state machine. Call from the event loop's idle branch. */
GazetteStreamState GazetteStreamPump(GazetteStream *s);

/* >= 0: bytes handed to the stack (0 means flow-controlled). -1: error. */
long GazetteStreamWrite(GazetteStream *s, const void *buf, size_t len);

/* >0: bytes read. 0: nothing yet. -1: error. -2: peer closed. */
long GazetteStreamRead(GazetteStream *s, void *buf, size_t len);

void GazetteStreamClose(GazetteStream *s);
void GazetteStreamDestroy(GazetteStream *s);

/* 0 when unknown or plain, otherwise 12 or 13. */
int  GazetteStreamTLSVersion(const GazetteStream *s);

/*
 * A failure line with enough in it to act on: what went wrong, how far the
 * connection got, and the transport's own error number. "connect failed" on
 * its own cannot be acted on; knowing whether the name resolved separates a
 * DNS problem from a routing or firewall one. Writes into out and returns it.
 */
const char *GazetteStreamDescribe(const GazetteStream *s, char *out, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* GAZETTE_NET_H */
