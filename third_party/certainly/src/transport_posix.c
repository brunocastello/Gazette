/*
 * transport_posix.c — the BSD-sockets implementation of certainly_transport.h.
 *
 * For Mac OS X 10.4 and later (Mach-O, both PowerPC and Intel) and for any
 * host with a POSIX socket layer, which is what lets CI prove it against live
 * servers on Linux and on a current macOS before it ever runs on Tiger.
 * Nothing here is newer than Mac OS X 10.4: socket, connect, select, fcntl,
 * getaddrinfo and pthreads are all in its libSystem.
 *
 * Like transport_win32.c this is far shorter than transport_ot.c, for the
 * same reason: a non-blocking socket returns EWOULDBLOCK where Open Transport
 * wants a notifier. The state machine is the same shape.
 */

#if !defined(_GNU_SOURCE)
#define _GNU_SOURCE 1           /* Linux: getaddrinfo, MSG_NOSIGNAL under -std=c99 */
#endif
#if !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE 1      /* Darwin: the BSD names -std=c99 would hide */
#endif
#ifndef CERTAINLY_POSIX
#define CERTAINLY_POSIX 1       /* certainly_compat.h's TickCount */
#endif

#include "certainly_transport.h"
#include "certainly_compat.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <unistd.h>

#include <stdlib.h>
#include <string.h>

#define CT_INVALID_FD (-1)

/*
 * A write to a socket the peer has closed raises SIGPIPE, which kills a GUI
 * application that never asked for it. Darwin has a per-socket option
 * (SO_NOSIGPIPE, set below), Linux a per-call flag; use whichever exists.
 */
#ifdef MSG_NOSIGNAL
#define CT_SEND_FLAGS MSG_NOSIGNAL
#else
#define CT_SEND_FLAGS 0
#endif

struct CTransport {
    int             sock;
    CTransportState state;
    uint16_t        port;
    char            host[256];

    long            lastError;      /* errno at the failure */
    uint32_t        addr;           /* resolved peer, host byte order */
    int             peerClosed;     /* FIN or reset seen */
    uint32_t        startedAt;      /* TickCount() when connecting began */

    /*
     * Name resolution runs on its own thread, as on Windows: getaddrinfo
     * blocks, and one slow lookup must not stall the event loop.
     *
     * The result fields are shared with that thread and guarded by a mutex,
     * not by volatile. The Windows build gets away with ordering the stores
     * because x86 never reorders them; a PowerPC does, so a pump on another
     * core could see dnsDone set before dnsAddr was written. A mutex is the
     * portable barrier, and the pump takes it for a few instructions once per
     * pass.
     */
    pthread_t       dnsThread;
    int             dnsStarted;
    pthread_mutex_t dnsLock;
    int             dnsLockInit;
    int             dnsDone;        /* 0 pending, 1 resolved, -1 failed */
    uint32_t        dnsAddr;        /* network byte order */
    long            dnsError;
};

/* ------------------------------------------------------------------ */
/* Name resolution                                                     */
/* ------------------------------------------------------------------ */

/*
 * One lookup at a time. Before 10.5, Darwin's getaddrinfo was old BSD
 * resolver code with shared state and is not safe on two threads at once;
 * Leopard moved it behind mDNSResponder. Gazette can have three fetches
 * resolving together -- a refresh, a full-text page and its photos -- so
 * on Tiger the lookups are serialised here (CPython does the same for the
 * same reason, bpo-25924). It costs nothing elsewhere: the threads wait on
 * each other, never the event loop.
 */
static pthread_mutex_t sResolverLock = PTHREAD_MUTEX_INITIALIZER;

static void *resolve_thread(void *param)
{
    CTransport      *t = (CTransport *)param;
    struct addrinfo  hints, *res = NULL;
    uint32_t         a = 0;
    int              rc, ok = 0;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    pthread_mutex_lock(&sResolverLock);
    rc = getaddrinfo(t->host, NULL, &hints, &res);
    if (rc == 0 && res != NULL && res->ai_family == AF_INET &&
        res->ai_addr != NULL) {
        a = ((struct sockaddr_in *)res->ai_addr)->sin_addr.s_addr;
        ok = 1;
    }
    if (res != NULL) freeaddrinfo(res);
    pthread_mutex_unlock(&sResolverLock);

    pthread_mutex_lock(&t->dnsLock);
    if (ok) {
        t->dnsAddr = a;
        t->dnsDone = 1;
    } else {
        t->dnsError = (rc != 0) ? (long)rc : (long)EINVAL;
        t->dnsDone  = -1;
    }
    pthread_mutex_unlock(&t->dnsLock);
    return NULL;
}

static int start_resolve(CTransport *t)
{
    in_addr_t literal;

    /* A dotted quad needs no thread and no resolver. */
    literal = inet_addr(t->host);
    if (literal != (in_addr_t)INADDR_NONE) {
        t->dnsAddr = (uint32_t)literal;
        t->dnsDone = 1;
        return 1;
    }

    if (pthread_mutex_init(&t->dnsLock, NULL) != 0) return 0;
    t->dnsLockInit = 1;
    if (pthread_create(&t->dnsThread, NULL, resolve_thread, t) != 0) return 0;
    t->dnsStarted = 1;
    return 1;
}

/* One look at the resolver's answer: 0 pending, 1 resolved, -1 failed. */
static int resolve_status(CTransport *t)
{
    int r;

    if (!t->dnsLockInit) return t->dnsDone;     /* the literal, no thread */

    pthread_mutex_lock(&t->dnsLock);
    r = t->dnsDone;
    if (r < 0) t->lastError = t->dnsError;
    pthread_mutex_unlock(&t->dnsLock);
    return r;
}

/* ------------------------------------------------------------------ */
/* Connecting                                                          */
/* ------------------------------------------------------------------ */

static int set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);

    if (flags < 0) return 0;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

/* A write to a reset connection must not kill the application. */
#if defined(__APPLE__) && !defined(SO_NOSIGPIPE)
#error "SO_NOSIGPIPE missing: compile with -std=gnu99, not -std=c99 (it hides behind _POSIX_C_SOURCE)"
#endif

static void no_sigpipe(int fd)
{
#ifdef SO_NOSIGPIPE
    int one = 1;

    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#else
    (void)fd;
#endif
}

static int begin_connect(CTransport *t)
{
    struct sockaddr_in sa;

    t->sock = socket(AF_INET, SOCK_STREAM, 0);
    if (t->sock == CT_INVALID_FD) {
        t->lastError = errno;
        return 0;
    }
    no_sigpipe(t->sock);
    if (!set_nonblocking(t->sock)) {
        t->lastError = errno;
        return 0;
    }

    memset(&sa, 0, sizeof(sa));
    sa.sin_family      = AF_INET;
    sa.sin_port        = htons(t->port);
    sa.sin_addr.s_addr = t->dnsAddr;

    while (connect(t->sock, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        int err = errno;

        /*
         * The expected answer from a non-blocking connect. EINTR belongs
         * here too: the connect carries on regardless, and calling it again
         * would only earn EALREADY.
         */
        if (err == EINPROGRESS || err == EWOULDBLOCK || err == EAGAIN ||
            err == EINTR) break;
        t->lastError = err;
        return 0;
    }

    t->addr      = ntohl(t->dnsAddr);
    t->startedAt = TickCount();
    return 1;
}

/* Has the connect finished, and did it work? 0 pending, 1 up, -1 failed. */
static int connect_status(CTransport *t)
{
    struct timeval tv;
    fd_set         writable;
    int            n, err = 0;
    socklen_t      len = sizeof(err);

    /* select() cannot represent a descriptor past FD_SETSIZE. */
    if (t->sock < 0 || t->sock >= FD_SETSIZE) {
        t->lastError = EBADF;
        return -1;
    }

    FD_ZERO(&writable);
    FD_SET(t->sock, &writable);
    tv.tv_sec = 0;
    tv.tv_usec = 0;

    n = select(t->sock + 1, NULL, &writable, NULL, &tv);
    if (n < 0) {
        if (errno == EINTR) return 0;
        t->lastError = errno;
        return -1;
    }
    if (n == 0) {
        if (TickCount() - t->startedAt > CT_CONNECT_TIMEOUT_TICKS) {
            t->lastError = ETIMEDOUT;
            return -1;
        }
        return 0;
    }

    /*
     * Writable means the connect finished, but not that it succeeded: a
     * refusal also makes the socket writable. SO_ERROR is the one answer that
     * distinguishes them.
     */
    if (getsockopt(t->sock, SOL_SOCKET, SO_ERROR, &err, &len) < 0) {
        t->lastError = errno;
        return -1;
    }
    if (err != 0) {
        t->lastError = err;
        return -1;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* Interface                                                           */
/* ------------------------------------------------------------------ */

CTransport *ct_transport_create(const char *host, uint16_t port)
{
    CTransport *t;

    if (host == NULL || host[0] == '\0') return NULL;

    t = (CTransport *)calloc(1, sizeof(*t));
    if (t == NULL) return NULL;

    t->sock = CT_INVALID_FD;
    t->port = port;
    if (strlen(host) >= sizeof(t->host)) {
        free(t);
        return NULL;
    }
    strcpy(t->host, host);

    if (!start_resolve(t)) {
        if (t->dnsLockInit) pthread_mutex_destroy(&t->dnsLock);
        t->dnsLockInit = 0;
        t->state = kCTransport_Error;
        return t;
    }
    t->state     = kCTransport_ResolvingDNS;
    t->startedAt = TickCount();
    return t;
}

CTransport *ct_transport_adopt(CTSocket sock)
{
    CTransport *t;

    if (sock == CT_SOCKET_NONE) return NULL;

    t = (CTransport *)calloc(1, sizeof(*t));
    if (t == NULL) return NULL;

    t->sock  = (int)sock;
    t->state = kCTransport_Connected;
    no_sigpipe(t->sock);
    set_nonblocking(t->sock);
    return t;
}

CTransportState ct_transport_pump(CTransport *t)
{
    if (t == NULL) return kCTransport_Error;

    switch (t->state) {
    case kCTransport_ResolvingDNS: {
        int r = resolve_status(t);

        if (r == 0) {
            if (TickCount() - t->startedAt > CT_CONNECT_TIMEOUT_TICKS) {
                t->lastError = ETIMEDOUT;
                t->state = kCTransport_Error;
            }
            break;
        }
        if (r < 0) {
            t->state = kCTransport_Error;
            break;
        }
        if (!begin_connect(t)) {
            t->state = kCTransport_Error;
            break;
        }
        t->state = kCTransport_Connecting;
        break;
    }

    case kCTransport_Connecting: {
        int r = connect_status(t);

        if (r > 0)      t->state = kCTransport_Connected;
        else if (r < 0) t->state = kCTransport_Error;
        break;
    }

    default:
        break;
    }
    return t->state;
}

int ct_transport_send(CTransport *t, const void *buf, size_t len)
{
    ssize_t n;

    if (t == NULL || t->sock == CT_INVALID_FD) return -1;
    if (len == 0) return 0;

    do {
        n = send(t->sock, buf, len, CT_SEND_FLAGS);
    } while (n < 0 && errno == EINTR);

    if (n < 0) {
        int err = errno;

        /* Not an error: the send buffer is full, try the next slice. */
        if (err == EAGAIN || err == EWOULDBLOCK) return 0;
        t->lastError = err;
        return -1;
    }
    return (int)n;
}

int ct_transport_recv(CTransport *t, void *buf, size_t len)
{
    ssize_t n;

    if (t == NULL || t->sock == CT_INVALID_FD) return -1;
    if (len == 0) return 0;

    do {
        n = recv(t->sock, buf, len, 0);
    } while (n < 0 && errno == EINTR);

    if (n == 0) {
        /*
         * The peer closed its side. The interface reports that through
         * ct_transport_peer_closed(), not through this return value, so the
         * TLS core sees "nothing available" here and asks separately whether
         * anything more is coming.
         */
        t->peerClosed = 1;
        return 0;
    }
    if (n < 0) {
        int err = errno;

        if (err == EAGAIN || err == EWOULDBLOCK) return 0;
        if (err == ECONNRESET || err == ECONNABORTED || err == EPIPE) {
            t->peerClosed = 1;
            t->lastError = err;
            return 0;
        }
        t->lastError = err;
        return -1;
    }
    return (int)n;
}

void ct_transport_close(CTransport *t)
{
    if (t == NULL || t->sock == CT_INVALID_FD) return;

    shutdown(t->sock, SHUT_WR);
    t->state = kCTransport_Closing;
}

void ct_transport_destroy(CTransport *t)
{
    if (t == NULL) return;

    /*
     * A resolver thread still running holds a pointer to this struct, so it
     * has to finish before the memory goes back. getaddrinfo always returns
     * eventually, and a connection being torn down mid-lookup is rare enough
     * that waiting is cheaper than the machinery to avoid it.
     */
    if (t->dnsStarted) pthread_join(t->dnsThread, NULL);
    if (t->dnsLockInit) pthread_mutex_destroy(&t->dnsLock);

    if (t->sock != CT_INVALID_FD) close(t->sock);
    free(t);
}

void ct_socket_close(CTSocket sock)
{
    if (sock != CT_SOCKET_NONE) close((int)sock);
}

/* ------------------------------------------------------------------ */
/* Accessors                                                           */
/* ------------------------------------------------------------------ */

CTransportState ct_transport_state(const CTransport *t)
{
    return (t == NULL) ? kCTransport_Error : t->state;
}

int ct_transport_peer_closed(const CTransport *t)
{
    return (t == NULL) ? 1 : t->peerClosed;
}

uint16_t ct_transport_port(const CTransport *t)
{
    return (t == NULL) ? 0 : t->port;
}

long ct_transport_last_error(const CTransport *t)
{
    return (t == NULL) ? 0 : t->lastError;
}

uint32_t ct_transport_peer_ipv4(const CTransport *t)
{
    return (t == NULL) ? 0 : t->addr;
}
