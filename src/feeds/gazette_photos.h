/*
 * Gazette — the pictures in an article
 * Copyright (c) 2026 brunocastello
 *
 * The full-text job hands over the addresses the extractor found in the
 * article's page, and this fetches them one after another on the same single
 * connection, after the text has landed: an article is readable at once and
 * the photographs fill in behind it. Like the full text, it is lazy and one
 * article at a time — nothing is fetched for an article nobody opened, and
 * nothing is cached.
 *
 * What comes back is bytes. Decoding them is QuickTime's business and the
 * window's; this only says which picture is here, which is still coming and
 * which is not coming at all.
 */
#ifndef GAZETTE_PHOTOS_H
#define GAZETTE_PHOTOS_H

#include "extract/gazette_extract.h"
#include "feeds/gazette_feeds.h"        /* GazetteRefreshState */

#ifdef __cplusplus
extern "C" {
#endif

enum {
    /*
     * The bytes an article's pictures may add up to, and the most any one of
     * them may be. A news photograph is 40–150 KB at the size a site serves
     * it, so three fit under the budget; a page that serves the 2 MB
     * original is cut off and shows that picture as not coming, which on a
     * modem is the kinder outcome. The whole budget is one block, taken when
     * the job starts.
     */
    kGazettePhotoBudget = 250L * 1024,
    kGazettePhotoEach   = 120L * 1024
};

enum {
    kGazettePhotoPending = 0,   /* still to come; the pane draws a placeholder */
    kGazettePhotoLoaded,        /* bytes in hand, QuickTime's to draw */
    kGazettePhotoFailed         /* not coming: the pane closes the gap */
};

/*
 * Begin fetching an article's pictures. refs are copied; baseURL is the
 * page's own address, which the relative ones are resolved against. Any
 * earlier job is dropped whole. Returns 1 when there is something to fetch.
 *
 * Refused while a refresh or the full text has the connection — but held,
 * not forgotten: GazettePhotosResume starts it once the line is free.
 */
int  GazettePhotosStart(int articleIndex, const char *baseURL,
                        const GazettePhotoRef *refs, int count);

/* One slice, from the event loop's idle branch. Done once every picture is
   settled one way or the other. */
GazetteRefreshState GazettePhotosPump(void);
GazetteRefreshState GazettePhotosGetState(void);

/* Start a job that was held back because the line was busy. From the idle
   branch; returns 1 if a fetch actually started. */
int  GazettePhotosResume(void);

/* Give the connection up without giving the job up: a refresh wants it. */
void GazettePhotosPause(void);

/* Drop everything, including the bytes already fetched. */
void GazettePhotosCancel(void);

/* Which article the pictures are for, or -1. The pane asks this before it
   draws any of them, the way it asks the full text. */
int  GazettePhotosArticle(void);

int         GazettePhotosCount(void);
int         GazettePhotosState(int i);
const char *GazettePhotosCaption(int i);

/* The picture's bytes, once Loaded; NULL otherwise. They stay valid until
   the next Start or Cancel. */
const char *GazettePhotosData(int i, long *outLen);

#ifdef __cplusplus
}
#endif

#endif /* GAZETTE_PHOTOS_H */
