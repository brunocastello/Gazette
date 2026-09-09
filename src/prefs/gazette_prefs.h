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
 *
 *     feed     = https://news.google.com/rss?... | Google News - Top Stories
 *     feed-off = https://example.com/feed.xml   | Something switched off
 *
 * A feed line is "<url> | <title>". The pipe is the separator because it is
 * the one printable character a URL can never carry unescaped; the title is
 * optional and defaults to the URL. "feed-off" is the same entry with its
 * enabled flag clear, which keeps a disabled feed visible and editable in the
 * file rather than commented out and forgotten.
 */
#ifndef GAZETTE_PREFS_H
#define GAZETTE_PREFS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Fixed capacities: on Mac OS 9 a bounded, statically sized model is worth
   far more than an unbounded one. A GazettePrefs is about 40 KB, which the
   8 MB partition carries without trouble, and nothing in the prefs path can
   then fail for want of memory. */
enum {
    kGazetteMaxFeeds  = 64,
    kGazetteURLLen    = 512,
    kGazetteTitleLen  = 128,
    /* Enough for kGazetteMaxFeeds full-length feed lines plus the settings
       block and comments, so serialising can never be the thing that fails. */
    kGazettePrefsTextMax = 48 * 1024
};

typedef struct {
    char url[kGazetteURLLen];
    char title[kGazetteTitleLen];
    int  enabled;
} GazetteFeedPref;

typedef struct {
    GazetteFeedPref feeds[kGazetteMaxFeeds];
    int  feedCount;

    long refreshMinutes;    /* 0 = manual refresh only */
    long maxArticles;       /* per feed; bounds the cache and the list */
    int  fullText;          /* 1 = fetch and strip the article page too */
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

/* Append a feed. title may be NULL or empty, in which case the URL stands in
   for it. Returns 1 on success, 0 when the list is full, the URL is empty or
   the URL is already present. */
int GazettePrefsAddFeed(GazettePrefs *p, const char *url, const char *title);

/* Remove the feed with this URL (case-insensitive). Returns 1 if one went. */
int GazettePrefsRemoveFeed(GazettePrefs *p, const char *url);

/* Index of the feed with this URL, or -1. */
int GazettePrefsFindFeed(const GazettePrefs *p, const char *url);

#ifdef __cplusplus
}
#endif

#endif /* GAZETTE_PREFS_H */
