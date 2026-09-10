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
/* Searching                                                           */
/*                                                                     */
/* A filter over what is held rather than a search of the disk. What   */
/* is held is one feed, or — since a group is readable — every feed in */
/* a group, which is what makes searching a whole section of the       */
/* sidebar a matter of selecting it first.                             */
/*                                                                     */
/* Every index below the store's line is a filtered one while a filter */
/* is set: the article list draws them, clicks arrive as them, and the */
/* store maps them back. Nothing above this header has to know.        */
/* ------------------------------------------------------------------ */

/* Set the search text, or NULL or "" to show everything again. Matches the
   title, the source and the body, case-insensitively. */
void GazetteFeedsSetFilter(const char *text);

/* What is being searched for, or "". */
const char *GazetteFeedsFilter(void);

/* How many are held in total, filter or no filter — the denominator when
   saying "8 of 120". */
int GazetteFeedsTotalCount(void);

/* ------------------------------------------------------------------ */
/* Read and unread                                                     */
/*                                                                     */
/* Opening an article marks it read. The state belongs to the feed and  */
/* rides in its cache file, so it survives a quit — an unread count     */
/* that resets every launch would be worse than none at all.            */
/* ------------------------------------------------------------------ */

int  GazetteFeedsUnreadCount(void);
void GazetteFeedsMarkRead(int index, int read);
void GazetteFeedsMarkAllRead(void);

/*
 * Write the cache back if the read state has moved since it was loaded.
 * Called before the store is replaced and before quitting, rather than on
 * every article: the file is the whole feed, and rewriting a hundred articles
 * because one was opened is a disk write nobody asked for.
 */
void GazetteFeedsFlush(void);

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

/*
 * Load every enabled feed of a group, merged and newest first — what a group
 * shows when it is selected. Returns the number of articles gathered.
 *
 * The store's ceiling applies to the whole group rather than to each feed in
 * it, so a group of ten feeds is the newest kGazetteMaxArticles across the
 * ten and not ten times that. Feeds with no cache contribute nothing and are
 * not an error: a group is readable as soon as any one of its feeds has been
 * fetched.
 */
int GazetteFeedsLoadGroup(int group, long maxArticles);

/* Which group the store is showing, or -1 when it is showing one feed. */
int GazetteFeedsCurrentGroup(void);

/* When the loaded feed was last fetched, in seconds since 1970, or 0. Read
   from the cache; the auto-refresh timer uses it to decide what is stale. */
long GazetteFeedsFetchedAt(void);

/* Forget a feed's cache, for when the feed itself is removed. */
void GazetteFeedsForgetCache(const char *url);

/* ------------------------------------------------------------------ */
/* Full article text                                                   */
/*                                                                     */
/* The feed's own summary is what the reader pane shows. With the      */
/* full-text preference on, opening an article also fetches the page   */
/* it links to and extracts the prose from it.                         */
/*                                                                     */
/* Lazy and one article at a time on purpose. Fetching every page a    */
/* refresh brought in would be a hundred connections and several       */
/* minutes on a modem, nearly all of it for articles nobody opens.     */
/* ------------------------------------------------------------------ */

/*
 * Start fetching one article's own page. Refused while a refresh is running:
 * there is one connection, and headlines matter more than the body of an
 * article that is already readable in summary. Returns 1 if it started.
 */
int GazetteFeedsFullTextStart(int articleIndex, const char *url);

/* One slice, from the event loop's idle branch, exactly like the refresh. */
GazetteRefreshState GazetteFeedsFullTextPump(void);
GazetteRefreshState GazetteFeedsFullTextGetState(void);

/* Which article the held text belongs to, or -1 when none is held. The
   reader pane asks this before using it: the answer is no after a refresh,
   after switching feeds, and while a fetch is still running. */
int GazetteFeedsFullTextArticle(void);

/* The extracted text, or "" when there is none. */
const char *GazetteFeedsFullText(void);

/* Why the last attempt came to nothing, or "". */
const char *GazetteFeedsFullTextErrorText(void);

/* Abandon a fetch in flight and drop whatever was held. */
void GazetteFeedsFullTextCancel(void);

#ifdef __cplusplus
}
#endif

#endif /* GAZETTE_FEEDS_H */
