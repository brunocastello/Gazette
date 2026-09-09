/*
 * Gazette — the article store and the refresh state machine
 * Copyright (c) 2026 brunocastello
 *
 * The one place the fetch, the parser and the UI meet. Not portable: it drives
 * gazette_fetch, which drives Open Transport. The parsing it delegates to is,
 * and that is where the tests are.
 *
 * Phase 2 keeps one feed's articles in memory at a time — refreshing a feed
 * replaces what was there. Phase 3 adds the on-disk cache that lets several
 * feeds be held at once and survive a restart.
 */
#ifndef GAZETTE_FEEDS_H
#define GAZETTE_FEEDS_H

#include <stddef.h>

#include "feeds/gazette_feed_parse.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    /*
     * Articles held for the selected feed. Google News returns about 80 items
     * for a topic and rather more for top stories; 200 covers both with room,
     * and the store is capped again at run time by the max-articles
     * preference. At roughly 1.4 KB an article this is 280 KB of the 8 MB
     * partition — a real cost, and the reason it is a fixed ceiling rather
     * than "however many the feed sent".
     */
    kGazetteMaxArticles = 200
};

typedef enum {
    kGazetteRefreshIdle = 0,
    kGazetteRefreshRunning,
    kGazetteRefreshDone,
    kGazetteRefreshFailed
} GazetteRefreshState;

/* ------------------------------------------------------------------ */
/* The store                                                           */
/* ------------------------------------------------------------------ */

/* Articles currently held, oldest index first, in the order the feed gave
   them — which for every feed worth reading is newest first. */
int                   GazetteFeedsArticleCount(void);
const GazetteArticle *GazetteFeedsArticleAt(int index);

/* The title the feed gave itself, which is usually better than the one the
   user wrote in their preferences. "" until a refresh has succeeded. */
const char *GazetteFeedsTitle(void);

/* Drop everything held. */
void GazetteFeedsClear(void);

/* ------------------------------------------------------------------ */
/* Refreshing                                                          */
/* ------------------------------------------------------------------ */

/*
 * Start fetching and parsing a feed URL into the store. The store is not
 * cleared until the first article arrives, so a failed refresh leaves what was
 * already there on screen rather than emptying the window. Returns 1 if the
 * refresh started.
 */
int GazetteFeedsRefreshStart(const char *url, long maxArticles);

/* One slice, from the event loop's idle branch. */
GazetteRefreshState GazetteFeedsRefreshPump(void);

GazetteRefreshState GazetteFeedsRefreshGetState(void);

/* Articles parsed so far in the running refresh, for a progress line. */
int         GazetteFeedsRefreshProgress(void);

/* A line of failure text, or "" while nothing has gone wrong. */
const char *GazetteFeedsRefreshErrorText(void);

/* Abandon a refresh in flight and release the connection. */
void GazetteFeedsRefreshCancel(void);

#ifdef __cplusplus
}
#endif

#endif /* GAZETTE_FEEDS_H */
