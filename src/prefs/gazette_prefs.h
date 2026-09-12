/*
 * Gazette — preferences and feed list
 * Copyright (c) 2026 brunocastello
 *
 * PORTABLE: the parsing and serialising live here with no Mac headers, so
 * tests/host exercises the whole prefs round-trip. Reading and writing the
 * file itself is the Toolbox's job and lives in store/gazette_store.h.
 *
 * The file is a plain, hand-editable text file called "Gazette Preferences"
 * in the System Folder's Preferences folder:
 *
 *     # Gazette Preferences
 *     refresh-minutes = 30
 *     max-articles    = 100
 *     country         = US
 *
 *     feed = https://example.com/daily.xml | Something Ungrouped
 *
 *     group = Google News
 *     feed  = https://news.google.com/rss?... | Top Stories
 *     feed  = https://news.google.com/rss/... | World
 *
 *     group-closed = Blogs
 *     feed     = https://example.com/feed.xml | A Blog
 *     feed-off = https://example.net/rss      | Switched off for now
 *
 * A feed line is "<url> | <title>". The pipe is the separator because it is
 * the one printable character a URL can never carry unescaped; the title is
 * optional and defaults to the URL. "feed-off" is the same entry with its
 * enabled flag clear, which keeps a disabled feed visible and editable in the
 * file rather than commented out and forgotten.
 *
 * "group" opens a group and every feed after it belongs to that group, until
 * the next one. Feeds before any group sit at the top level, the way a
 * loose file sits beside folders. "group-closed" is the same thing with its
 * disclosure triangle shut, so the sidebar comes back the way it was left.
 *
 * The order of the lines is the order of the sidebar. That is the whole point:
 * the tree is the user's, arranged by hand in this file or by dragging in the
 * window, and nothing in Gazette is entitled to re-sort it.
 */
#ifndef GAZETTE_PREFS_H
#define GAZETTE_PREFS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Fixed capacities: on Mac OS 9 a bounded, statically sized model is worth far
 * more than an unbounded one. A GazettePrefs is about 41 KB, which the 8 MB
 * partition carries without trouble, and nothing in the prefs path can then
 * fail for want of memory -- no allocation, so no allocation failure.
 *
 * 64 is a judgement, not a limit anything imposes. It is well past what
 * Newsstand offered, it keeps the one live GazettePrefs plus the two
 * serialisation buffers under about 140 KB, and at full length it serialises
 * to roughly 42 KB against the 48 KB text buffer below. Raising it is one
 * number -- but kGazettePrefsTextMax has to move with it, or saving quietly
 * starts failing. gazette_prefs.c carries a compile-time check so that cannot
 * happen unnoticed.
 */
enum {
    kGazetteMaxFeeds  = 128,
    kGazetteMaxGroups = 24,
    kGazetteURLLen    = 512,
    kGazetteTitleLen  = 128,
    kGazetteGroupLen  = 64,
    kGazetteCountryLen = 8,

    /* Enough for kGazetteMaxFeeds full-length feed lines, every group name,
       and the settings block, so serialising can never be the thing that
       fails. gazette_prefs.c has the arithmetic as a compile-time check. */
    kGazettePrefsTextMax = 96 * 1024
};

/*
 * `hidden` on either of these is a *view* flag, not part of the model: it is
 * what "Hide Read Feeds" sets, and it says only that the sidebar is not
 * drawing that line at the moment. It is never written to the file and never
 * read back from one — a subscription the user cannot see is still a
 * subscription — and everything that walks the rows skips it.
 */
typedef struct {
    char name[kGazetteGroupLen];
    int  collapsed;             /* the sidebar's disclosure triangle */
    int  hidden;                /* runtime only; see above */
} GazetteGroupPref;

typedef struct {
    char url[kGazetteURLLen];
    char title[kGazetteTitleLen];
    int  enabled;
    int  group;                 /* index into groups, or -1 for the top level */
    int  hidden;                /* runtime only; see above */
} GazetteFeedPref;

typedef struct {
    GazetteGroupPref groups[kGazetteMaxGroups];
    int  groupCount;

    /*
     * Feeds in sidebar order: top-level ones first, then each group's in
     * turn. Kept that way rather than sorted on demand, because the order is
     * the user's and a redraw is not the place to be re-deriving it.
     */
    GazetteFeedPref feeds[kGazetteMaxFeeds];
    int  feedCount;

    long refreshMinutes;    /* 0 = manual refresh only */
    long maxArticles;       /* per feed; bounds the cache and the list */
    char country[kGazetteCountryLen];   /* for Google News URLs */

    /*
     * What the View menu is holding. These are the user's, they survive a
     * quit like every other preference, and they change nothing about what
     * is subscribed to or what has been fetched — only what is drawn.
     */
    int  oldestFirst;       /* Sort Articles By: Oldest on Top */
    int  hideReadArticles;
    int  hideReadFeeds;
    int  hideSidebar;
    int  hideToolbar;
} GazettePrefs;

/* Populate p with Gazette's out-of-the-box configuration: one Google News
   Top Stories feed, so a first run has something to show. */
void GazettePrefsSetDefaults(GazettePrefs *p);

/* Parse prefs text into p. Settings that are absent or unparseable keep the
   default from GazettePrefsSetDefaults, which this calls first. Returns the
   number of feeds read. */
int GazettePrefsParse(const char *text, size_t len, GazettePrefs *p);

/* Write p back out as prefs text. Returns the length written, or 0 if it
   would not fit in cap. Always NUL-terminates when cap > 0. */
size_t GazettePrefsSerialize(const GazettePrefs *p, char *out, size_t cap);

/*
 * Append a feed to a group (-1 for the top level). title may be NULL or
 * empty, in which case the URL stands in for it. Returns the new feed's
 * index, or -1 when the list is full, the URL is empty, or the URL is
 * already present.
 *
 * The feed lands after the last one already in that group, so adding does
 * not disturb an order the user arranged.
 */
int GazettePrefsAddFeed(GazettePrefs *p, const char *url, const char *title,
                        int group);

/* Remove the feed with this URL (case-insensitive). Returns 1 if one went. */
int GazettePrefsRemoveFeed(GazettePrefs *p, const char *url);

/* Index of the feed with this URL, or -1. */
int GazettePrefsFindFeed(const GazettePrefs *p, const char *url);

/* Move a feed to a new position in the list, and into a group. This is what a
   drag lands on. Returns the feed's new index, or -1. */
int GazettePrefsMoveFeed(GazettePrefs *p, int from, int to, int group);

/* Rename a feed. Returns 1 on success. */
int GazettePrefsRenameFeed(GazettePrefs *p, int index, const char *title);

/* Switch a feed off or on. A feed that is off keeps its place in the list and
   its line in the file -- it is skipped by a refresh, not forgotten. Returns
   1 on success. */
int GazettePrefsSetFeedEnabled(GazettePrefs *p, int index, int enabled);

/* Change a feed's address, keeping its place, its name and its group. Refused
   when the address is empty or already belongs to a different feed. Returns 1
   on success. */
int GazettePrefsSetFeedURL(GazettePrefs *p, int index, const char *url);

/* ------------------------------------------------------------------ */
/* Groups                                                             */
/* ------------------------------------------------------------------ */

/* Append a group. Returns its index, or -1 when there is no room or the name
   is empty. Duplicate names are allowed: they are labels, not keys. */
int GazettePrefsAddGroup(GazettePrefs *p, const char *name);

/*
 * Remove a group. Its feeds move to the top level rather than being deleted —
 * removing a folder should never silently take subscriptions with it.
 * Returns 1 if a group went.
 */
int GazettePrefsRemoveGroup(GazettePrefs *p, int index);

int GazettePrefsRenameGroup(GazettePrefs *p, int index, const char *name);

/* Move a group, and its feeds with it. Returns its new index, or -1. */
int GazettePrefsMoveGroup(GazettePrefs *p, int from, int to);

/* Index of the first feed in a group, or -1 when it has none. */
int GazettePrefsFirstFeedInGroup(const GazettePrefs *p, int group);

/* How many feeds a group holds. */
int GazettePrefsGroupFeedCount(const GazettePrefs *p, int group);

/* ------------------------------------------------------------------ */
/* The sidebar's rows                                                  */
/* ------------------------------------------------------------------ */

/*
 * The tree flattened into the lines the sidebar actually draws: every group,
 * the feeds of the open ones, and the top-level feeds ahead of them all. A
 * closed group contributes its own row and nothing else.
 *
 * This is a view of the model rather than part of it, but it is arithmetic
 * over the feed order and nothing more, so it lives here where a host test
 * can reach it instead of inside a Toolbox drawing routine where it could
 * only be checked by looking at the screen.
 */
enum { kGazetteRowGroup = 0, kGazetteRowFeed = 1, kGazetteRowSmart = 2 };

/*
 * The three standing views above the feed list. They are not subscriptions
 * and they are not in the preferences file — they are three questions about
 * everything that has been fetched, and they are first in the sidebar because
 * they are where a reader starts.
 *
 * They live in the row model rather than in the window because the row model
 * is the one place that knows what shape the sidebar is, and because the
 * arithmetic that puts a feed on row n has to know they are there.
 */
enum {
    kGazetteSmartToday   = 0,
    kGazetteSmartUnread  = 1,
    kGazetteSmartStarred = 2,
    kGazetteSmartCount   = 3
};

typedef struct {
    int kind;                   /* kGazetteRowGroup or kGazetteRowFeed */
    int index;                  /* into groups[] or feeds[] accordingly */
} GazetteSidebarRow;

/* How many rows the sidebar shows. Hidden feeds and hidden groups are not
   rows: they are drawn nowhere and counted nowhere. */
int GazettePrefsRowCount(const GazettePrefs *p);

/* What row n is. Returns 1 and fills out, or returns 0 and leaves it. */
int GazettePrefsRowAt(const GazettePrefs *p, int row, GazetteSidebarRow *out);

/* The row a feed is drawn on, or -1 when it is hidden, or its group is
   closed or hidden, and it is not drawn at all. */
int GazettePrefsRowForFeed(const GazettePrefs *p, int feed);

/* The row a group's own line is drawn on, or -1 when it is hidden. */
int GazettePrefsRowForGroup(const GazettePrefs *p, int group);

/* The row one of the standing views is drawn on. They are the first rows and
   nothing hides them, so this is the index itself; it exists so that nothing
   above has to know that. */
int GazettePrefsRowForSmart(int which);

/* The name a standing view is labelled with. */
const char *GazettePrefsSmartName(int which);

/* Clear every `hidden` flag — what "Hide Read Feeds" being switched off
   means, and what the sidebar starts from each time it recomputes them. */
void GazettePrefsShowAll(GazettePrefs *p);

#ifdef __cplusplus
}
#endif

#endif /* GAZETTE_PREFS_H */
