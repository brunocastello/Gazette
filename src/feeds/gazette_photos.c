/*
 * Gazette — the pictures in an article
 * Copyright (c) 2026 brunocastello
 *
 * See gazette_photos.h.
 */

#include "feeds/gazette_photos.h"

#include "net/gazette_fetch.h"
#include "portable/gazette_portable.h"
#include "portable/gazette_url.h"

#include <MacMemory.h>          /* NewPtrClear, DisposePtr */

#include <string.h>

typedef struct {
    char  url[kGazettePhotoURLLen + 16];    /* resolved; room for a scheme */
    char  alt[kGazettePhotoAltLen];
    int   state;
    long  offset;                           /* into the block's data */
    long  len;
} Photo;

/*
 * Everything a job needs, in one block taken when it starts and kept until
 * the article is left: the budget for the bytes, and the two URL records
 * that resolve the addresses — 4 KB each, with no business on a stack the
 * fetch pump is already several frames into. A pause gives up the
 * connection and nothing else, so a picture that had landed before a
 * refresh took the line is still here after it.
 */
typedef struct {
    GazetteURL base;
    GazetteURL resolved;
    char       data[kGazettePhotoBudget];
} Block;

static Block               *gBlock;
static GazetteFetch        *gFetch;
static Photo                gPhotos[kGazetteMaxPhotos];
static int                  gCount;
static int                  gHasLead;
static int                  gArticle   = -1;
static int                  gCurrent   = -1;        /* the one being fetched */
static long                 gUsed;                  /* of the block's data */
static GazetteRefreshState  gState     = kGazetteRefreshIdle;
static int                  gWanted;                /* a job held back */

/* ------------------------------------------------------------------ */
/* The bytes                                                           */
/* ------------------------------------------------------------------ */

/*
 * What the first bytes say the file is. Sniffed rather than trusted from a
 * Content-Type, because a CDN's Content-Type is whatever it was told and the
 * bytes are what QuickTime will be handed. Anything else — a WebP, an HTML
 * error page served as 200 — is not a picture Gazette can draw.
 */
static int LooksLikeAPicture(const unsigned char *b, long len)
{
    if (len < 4) {
        return 0;
    }
    if (b[0] == 0xFF && b[1] == 0xD8 && b[2] == 0xFF) {
        return 1;                                   /* JPEG */
    }
    if (b[0] == 0x89 && b[1] == 'P' && b[2] == 'N' && b[3] == 'G') {
        return 1;                                   /* PNG */
    }
    if (b[0] == 'G' && b[1] == 'I' && b[2] == 'F' && b[3] == '8') {
        return 1;                                   /* GIF */
    }
    return 0;
}

/* Give a picture's bytes back to the budget. */
static void Discard(Photo *p)
{
    gUsed -= p->len;
    p->len = 0;
}

static int PhotoSink(const char *data, size_t len, void *context)
{
    Photo *p = &gPhotos[gCurrent];
    long   room;

    (void)context;
    if (gBlock == NULL) {
        return 0;
    }

    /* The first bytes settle whether this is worth reading at all. */
    if (p->len < 4 && p->len + (long)len >= 4) {
        unsigned char head[4];
        long          have = p->len;

        memcpy(head, gBlock->data + p->offset, (size_t)have);
        memcpy(head + have, data, (size_t)(4 - have));
        if (!LooksLikeAPicture(head, 4)) {
            Discard(p);
            return 0;
        }
    }

    room = kGazettePhotoEach - p->len;
    if (kGazettePhotoBudget - gUsed < room) {
        room = kGazettePhotoBudget - gUsed;
    }
    if ((long)len > room) {
        /* Over the cap: the rest of it is not worth the line, and what was
           read is not a picture. */
        Discard(p);
        return 0;
    }
    memcpy(gBlock->data + p->offset + p->len, data, len);
    p->len += (long)len;
    gUsed  += (long)len;
    return 1;
}

/* ------------------------------------------------------------------ */
/* The job                                                             */
/* ------------------------------------------------------------------ */

static void DropLine(void)
{
    if (gFetch != NULL) {
        GazetteFetchDestroy(gFetch);
        gFetch = NULL;
    }
    if (gCurrent >= 0 && gCurrent < gCount &&
        gPhotos[gCurrent].state == kGazettePhotoPending) {
        Discard(&gPhotos[gCurrent]);        /* half of one is none of it */
    }
    gCurrent = -1;
}

static int LineIsBusy(void)
{
    return GazetteFeedsRefreshGetState()  == kGazetteRefreshRunning ||
           GazetteFeedsFullTextGetState() == kGazetteRefreshRunning;
}

/* Open the connection for the next picture still pending. Returns 0 when
   there is none left, which is the job finishing. */
static int FetchNext(void)
{
    int i;

    for (i = 0; i < gCount; i++) {
        Photo *p = &gPhotos[i];

        if (p->state != kGazettePhotoPending) {
            continue;
        }
        p->offset = gUsed;
        p->len    = 0;
        gFetch    = GazetteFetchStart(p->url, PhotoSink, NULL);
        if (gFetch != NULL) {
            gCurrent = i;
            return 1;
        }
        p->state = kGazettePhotoFailed;
    }
    gCurrent = -1;
    return 0;
}

static int Begin(void)
{
    gWanted = 0;
    if (!FetchNext()) {
        gState = kGazetteRefreshDone;
        return 0;
    }
    gState = kGazetteRefreshRunning;
    return 1;
}

int GazettePhotosStart(int articleIndex, const char *baseURL,
                       const GazettePhotoRef *refs, int count, int hasLead)
{
    int i, kept = 0;

    GazettePhotosCancel();

    if (refs == NULL || count <= 0 || baseURL == NULL) {
        return 0;
    }
    if (count > kGazetteMaxPhotos) {
        count = kGazetteMaxPhotos;
    }

    gBlock = (Block *)NewPtrClear((Size)sizeof(Block));
    if (gBlock == NULL ||
        !GazetteURLSplit(baseURL, strlen(baseURL), &gBlock->base)) {
        GazettePhotosCancel();
        return 0;
    }

    for (i = 0; i < count; i++) {
        Photo *p = &gPhotos[kept];

        gz_copy_n(p->alt, sizeof p->alt, refs[i].alt, strlen(refs[i].alt));
        p->offset = 0;
        p->len    = 0;

        /* An address that will not resolve keeps its slot, marked as not
           coming: the pane places every picture by its position in the
           list, and the markers in the text count the same way. */
        if (GazetteURLResolve(&gBlock->base, refs[i].url,
                              strlen(refs[i].url), &gBlock->resolved) &&
            GazetteURLFormat(&gBlock->resolved, p->url, sizeof p->url) != 0) {
            p->state = kGazettePhotoPending;
        } else {
            p->url[0] = '\0';
            p->state  = kGazettePhotoFailed;
        }
        kept++;
    }

    gCount   = kept;
    gHasLead = hasLead ? 1 : 0;
    gArticle = articleIndex;

    if (LineIsBusy()) {
        gWanted = 1;
        gState  = kGazetteRefreshIdle;
        return 1;
    }
    return Begin();
}

int GazettePhotosResume(void)
{
    if (!gWanted || gArticle < 0 || LineIsBusy()) {
        return 0;
    }
    return Begin();
}

void GazettePhotosPause(void)
{
    if (gArticle < 0 || gState != kGazetteRefreshRunning) {
        return;
    }
    DropLine();
    gWanted = 1;
    gState  = kGazetteRefreshIdle;
}

void GazettePhotosCancel(void)
{
    DropLine();
    if (gBlock != NULL) {
        DisposePtr((Ptr)gBlock);
        gBlock = NULL;
    }
    memset(gPhotos, 0, sizeof gPhotos);
    gCount   = 0;
    gHasLead = 0;
    gArticle = -1;
    gUsed    = 0;
    gWanted  = 0;
    gState   = kGazetteRefreshIdle;
}

GazetteRefreshState GazettePhotosPump(void)
{
    GazetteFetchState fetchState;
    Photo            *p;

    if (gState != kGazetteRefreshRunning || gFetch == NULL) {
        return gState;
    }

    fetchState = GazetteFetchPump(gFetch);
    if (fetchState != kGazetteFetchDone && fetchState != kGazetteFetchFailed) {
        return gState;
    }

    p = &gPhotos[gCurrent];

    /*
     * Done covers the sink stopping the fetch itself — the wrong kind of
     * file, or one over the cap — and both of those gave their bytes back
     * first. A 200 with picture bytes in it is the only success.
     */
    if (fetchState == kGazetteFetchDone && GazetteFetchStatus(gFetch) == 200 &&
        p->len >= 4 &&
        LooksLikeAPicture((const unsigned char *)gBlock->data + p->offset,
                          p->len)) {
        p->state = kGazettePhotoLoaded;
    } else {
        Discard(p);
        p->state = kGazettePhotoFailed;
    }
    GazetteFetchDestroy(gFetch);
    gFetch   = NULL;
    gCurrent = -1;

    if (!FetchNext()) {
        gState = kGazetteRefreshDone;
    }
    return gState;
}

GazetteRefreshState GazettePhotosGetState(void)
{
    return gState;
}

/* ------------------------------------------------------------------ */
/* What is here                                                        */
/* ------------------------------------------------------------------ */

int GazettePhotosArticle(void)
{
    return gArticle;
}

int GazettePhotosCount(void)
{
    return gArticle >= 0 ? gCount : 0;
}

int GazettePhotosHasLead(void)
{
    return gArticle >= 0 ? gHasLead : 0;
}

int GazettePhotosState(int i)
{
    if (gArticle < 0 || i < 0 || i >= gCount) {
        return kGazettePhotoFailed;
    }
    return gPhotos[i].state;
}

const char *GazettePhotosCaption(int i)
{
    if (gArticle < 0 || i < 0 || i >= gCount) {
        return "";
    }
    return gPhotos[i].alt;
}

const char *GazettePhotosData(int i, long *outLen)
{
    if (outLen != NULL) {
        *outLen = 0;
    }
    if (gArticle < 0 || gBlock == NULL || i < 0 || i >= gCount ||
        gPhotos[i].state != kGazettePhotoLoaded) {
        return NULL;
    }
    if (outLen != NULL) {
        *outLen = gPhotos[i].len;
    }
    return gBlock->data + gPhotos[i].offset;
}
