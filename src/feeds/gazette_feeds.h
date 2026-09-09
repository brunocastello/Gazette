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
     * Articles held for the selected feed. An article is about 2.9 KB now
     * that it carries the feed's summary for the reader pane, so 150 of them
     * is roughly 440 KB of the 8 MB partition. That is a real cost and the
     * reason this is a fixed ceiling rather than "however many the feed
     * sent": one hostile feed should not be able to decide how much of the
     * heap Gazette uses.
     *
     * It is comfortably more than any feed delivers in one fetch — Google
     * News top stories returned 38 in testing and a topic returns fewer — and
     * the store is capped again at run time by the max-articles preference,
     * which defaults to 100.
     */
    kGazetteMaxArticles = 150
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

/*
 * Which entry in the preferences' feed list the held articles belong to, or
 * -1 when nothing has been loaded. The sidebar needs this to know which row
 * to show as selected, and the refresh needs it to know whether switching
 * feeds means throwing the store away.
 */
int GazetteFeedsCurrentFeed(void);

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
int GazetteFeedsRefreshStart(int feedIndex, const char *url, long maxArticles);

/* One slice, from the event loop's idle branch. */
GazetteRefreshState GazetteFeedsRefreshPump(void);

GazetteRefreshState GazetteFeedsRefreshGetState(void);

/* Articles parsed so far in the running refresh, for a progress line. */
int         GazetteFeedsRefreshProgress(void);

/* A line of failure text, or "" while nothing has gone wrong. */
const char *GazetteFeedsRefreshErrorText(void);

/* Abandon a refresh in flight and release the connection. */
void GazetteFeedsRefreshCancel(void);

/* ------------------------------------------------------------------ */
/* The cache                                                           */
/*                                                                     */
/* One file per feed, so a restart shows what was there and switching   */
/* between feeds does not need the network. A successful refresh writes */
/* its result; selecting a feed reads it.                               */
/* ------------------------------------------------------------------ */

/*
 * Load a feed's cached articles into the store. Returns 1 when a cache
 * existed and held something, 0 otherwise — in which case the store is left
 * alone, so a feed with no cache does not blank the window.
 */
int GazetteFeedsLoadCache(int feedIndex, const char *url, long maxArticles);

/* When the loaded feed was last fetched, in seconds since 1970, or 0. Read
   from the cache; the auto-refresh timer uses it to decide what is stale. */
long GazetteFeedsFetchedAt(void);

/* Forget a feed's cache, for when the feed itself is removed. */
void GazetteFeedsForgetCache(const char *url);

#ifdef __cplusplus
}
#endif

#endif /* GAZETTE_FEEDS_H */
