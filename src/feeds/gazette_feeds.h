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

#include "extract/gazette_extract.h"    /* GazettePhotoRef */

#include <stddef.h>

#include "feeds/gazette_feed_parse.h"
#include "prefs/gazette_prefs.h"        /* kGazetteSmartCount and its names */

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

/* ------------------------------------------------------------------ */
/* The rest of the view                                                */
/*                                                                     */
/* Two more things shape the list the window draws, and they go        */
/* through the same map the search does, so an index is an index       */
/* whatever is switched on.                                            */
/* ------------------------------------------------------------------ */

/*
 * "Hide Read Articles". A starred article stays in the list whatever its read
 * state: starring it is the reader saying they want it where they can find
 * it, which is the opposite of what hiding it would do.
 *
 * Marking an article read does *not* re-derive the list. That is deliberate:
 * opening an article marks it read, and an article that vanished from under
 * the reader at the moment they opened it would be unusable. It goes when the
 * view is next rebuilt — which is to say, when they have moved on.
 */
void GazetteFeedsSetHideRead(int hide);
int  GazetteFeedsHideRead(void);

/* "Sort Articles By". The store is held newest first, so this is a walk
   backwards rather than a sort. */
void GazetteFeedsSetOldestFirst(int oldest);
int  GazetteFeedsOldestFirst(void);

/* Re-derive the list from the store. Called when something outside has
   changed what belongs in it — a Mark All as Read, with read articles
   hidden — rather than on every change of state. */
void GazetteFeedsRebuildView(void);

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

/* The same command turned round, for when there is nothing left unread and
   the menu item has become "Mark All as Unread". Not an undo: it does not
   remember which were read before. */
void GazetteFeedsMarkAllUnread(void);

/* Everything before this article in the list, or everything after it, marked
   read. "Before" and "after" are the list's own order, so they follow the
   sort rather than the store. */
void GazetteFeedsMarkRange(int index, int below);

/* ------------------------------------------------------------------ */
/* Starred                                                             */
/*                                                                     */
/* Kept where the read state is kept — in the index, by the article's  */
/* link — so it outlives the article rolling off its feed and coming   */
/* back on the next fetch. GazetteArticle carries it as `starred`.     */
/* ------------------------------------------------------------------ */

void GazetteFeedsMarkStarred(int index, int starred);

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
/*
 * allowDiscovery asks the refresh to also watch for a feed link, so that a
 * site's home page pasted into New Feed can be turned into its feed. It costs
 * a second scanner over the same bytes, so it is asked for only on the first
 * refresh after a feed is added and not on every refresh thereafter.
 */
/*
 * What the last refresh learned about the feed beyond its articles: which
 * feed it was, and the site it is for. Valid once the refresh is done, and
 * until the next one starts; "" when the feed did not say where it lives.
 */
int         GazetteFeedsRefreshFeedIndex(void);
const char *GazetteFeedsRefreshHome(void);

int GazetteFeedsRefreshStart(int feedIndex, const char *url, long maxArticles,
                             int allowDiscovery);

/*
 * The feed link found while refreshing, or "" — set only when discovery was
 * asked for and the document turned out to be a page rather than a feed.
 * It is whatever the page wrote, so it may be relative and the caller
 * resolves it against the URL that was fetched.
 *
 * Only meaningful on a refresh that produced no articles. A working feed that
 * happens to link to another feed must not be quietly replaced by it.
 */
const char *GazetteFeedsDiscoveredURL(void);

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

/* ------------------------------------------------------------------ */
/* The standing views                                                  */
/*                                                                     */
/* Today, All Unread and Starred: one question asked of every enabled  */
/* feed's cache rather than of one feed. Gathered exactly the way a    */
/* group is, and the question is asked as each article arrives — the   */
/* merge keeps only the newest kGazetteMaxArticles, so a starred       */
/* article from last month would be dropped before anything looked at  */
/* it if the filtering came afterwards.                                */
/* ------------------------------------------------------------------ */

/* Gather one. Returns how many articles it found. */
int GazetteFeedsLoadSmart(int which, long maxArticles);

/* Which standing view the store is showing, or -1. */
int GazetteFeedsCurrentSmart(void);

/*
 * How many articles are dated today, across every enabled feed — the number
 * the sidebar puts beside "Today".
 *
 * It reads every cache, because what counts as today changes at midnight and
 * so there is nothing that could be written down and read back. The other two
 * views' numbers cost nothing — the index keeps an unread count per feed and
 * knows how many are starred — so this is the only one that is a function,
 * and it is meant to be asked when the sidebar's rows are rebuilt rather than
 * when a row is drawn.
 */
int GazetteFeedsCountToday(void);

/* ------------------------------------------------------------------ */
/* The clock                                                           */
/*                                                                     */
/* A feed timestamps its articles in UTC and the Macintosh clock keeps */
/* local time. Everything shown to the reader goes through this, and   */
/* it lives here because this is the file that already owns Gazette's  */
/* idea of what time it is.                                            */
/* ------------------------------------------------------------------ */

/*
 * Seconds east of GMT, and an article's date read on the reader's clock.
 *
 * One direction only: the Macintosh clock already keeps local time, so it is
 * the article that is converted and never "now". Adding the offset to both
 * sides counts it twice, and the mistake shows up as dates that are right in
 * London and an hour out everywhere else.
 */
long GazetteFeedsGMTDelta(void);
long GazetteFeedsLocalTime(long seconds);

/* When the loaded feed was last fetched, in seconds since 1970, or 0. Read
   from the cache; the auto-refresh timer uses it to decide what is stale. */
long GazetteFeedsFetchedAt(void);

/* Forget a feed's cache, for when the feed itself is removed. */
void GazetteFeedsForgetCache(const char *url);

/* ------------------------------------------------------------------ */
/* Full article text                                                   */
/*                                                                     */
/* Opening an article fetches the page it links to and extracts the    */
/* prose from it. Always: the feed's own summary is what stands on     */
/* screen until that lands, and what it falls back to when the page    */
/* cannot be had, but it is never what was wanted.                     */
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

/*
 * The same article's page, if it was read lately and is still held: puts it
 * where a fetched one goes and answers 1 with the state Done, and nothing
 * goes over the wire — so it works with the machine unplugged. Asked before
 * Start. A refresh is what forgets what was read.
 */
int GazetteFeedsFullTextRecall(int articleIndex, const char *url);

/*
 * Whether this article's own page is still to come — a fetch running for it,
 * or one held back because the refresh has the connection.
 *
 * The reader pane asks before it composes anything. While the answer is yes
 * it says so rather than laying out the summary: the summary is what the
 * article falls back to, not what it starts as, and swapping one for the
 * other under the reader's eyes a second after they opened it is worse than
 * the wait it was meant to spare them.
 */
int GazetteFeedsFullTextComing(int articleIndex);

/* Start a page that was asked for while the line was busy. From the idle
   branch; returns 1 if a fetch actually started. */
int GazetteFeedsFullTextResume(void);

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

/*
 * The pictures the page named, once the text is here: the list the
 * extractor built (see kGazettePhotoMarker there). Returns how many; 0
 * while no text is held. The addresses are as the page wrote them, to be
 * resolved against the page's own address — which is FinalURL, the one the
 * text actually came from.
 */
int         GazetteFeedsFullTextPhotos(const GazettePhotoRef **refs);
const char *GazetteFeedsFullTextFinalURL(void);

/* Abandon a fetch in flight and drop whatever was held. */
void GazetteFeedsFullTextCancel(void);

#ifdef __cplusplus
}
#endif

#endif /* GAZETTE_FEEDS_H */
