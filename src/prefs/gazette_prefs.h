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
 *     full-text       = 0
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

typedef struct {
    char name[kGazetteGroupLen];
    int  collapsed;             /* the sidebar's disclosure triangle */
} GazetteGroupPref;

typedef struct {
    char url[kGazetteURLLen];
    char title[kGazetteTitleLen];
    int  enabled;
    int  group;                 /* index into groups, or -1 for the top level */
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
    int  fullText;          /* 1 = fetch and strip the article page too */
    char country[kGazetteCountryLen];   /* for Google News URLs */
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

#ifdef __cplusplus
}
#endif

#endif /* GAZETTE_PREFS_H */
