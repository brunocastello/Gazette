/*
 * Gazette — what is remembered between launches that belongs to no one feed
 * Copyright (c) 2026 brunocastello
 *
 * Two things live in one small file, "Gazette Index", beside the per-feed
 * caches.
 *
 * Which articles have been read. That used to be a flag inside each feed's
 * cache, which was fine while the window showed one feed at a time and stops
 * being fine the moment it shows a group: an article opened in a group view
 * belongs to a feed whose cache is not the one being written. Read state is
 * really a property of an article, not of the file an article happens to be
 * cached in, so it is kept by the article's link and nowhere else.
 *
 * And how many of each feed are unread. The sidebar wants that for every
 * feed, and the store holds one feed's articles at a time — counting the rest
 * would mean opening a hundred-odd cache files at launch, which on the
 * machines this targets is not a thing to do. So the count is written down
 * when a feed's articles are in hand and read back from here when they are
 * not.
 *
 * Links are held as 32-bit hashes rather than as text: a Google News link
 * runs to several hundred characters, and thousands of those would be a
 * larger file and a larger allocation than the articles themselves. A
 * collision marks one unread article read, which is the harmless direction
 * for this particular mistake to go.
 */
#ifndef GAZETTE_INDEX_H
#define GAZETTE_INDEX_H

#ifdef __cplusplus
extern "C" {
#endif

enum {
    /*
     * Read articles remembered. 128 feeds at the default hundred articles is
     * 12,800, but articles roll off a feed long before that many accumulate
     * as *read* — and at 4 bytes each this is 32 KB, which is the number
     * being chosen here. When it fills, the oldest are dropped: an article
     * read long enough ago to be evicted has almost certainly rolled off its
     * feed too, and if it has not, it comes back unread once.
     */
    kGazetteMaxReadArticles = 8192
};

/* Read the index, or start an empty one when there is none. Call once. */
void GazetteIndexLoad(void);

/* Write it back if anything changed. Cheap to call when nothing has. */
void GazetteIndexSave(void);

/* ------------------------------------------------------------------ */
/* Read articles                                                       */
/* ------------------------------------------------------------------ */

int  GazetteIndexIsRead(const char *link);
void GazetteIndexSetRead(const char *link, int read);

/* ------------------------------------------------------------------ */
/* Per-feed counts                                                     */
/* ------------------------------------------------------------------ */

/* Record what a feed holds, for the sidebar to read back later. */
void GazetteIndexSetFeedCounts(const char *feedURL, int total, int unread);

/* What was recorded, or 0 for a feed that has never been fetched. */
int  GazetteIndexFeedUnread(const char *feedURL);
int  GazetteIndexFeedTotal(const char *feedURL);

/* Forget a feed entirely, for when the feed is removed. */
void GazetteIndexForgetFeed(const char *feedURL);

#ifdef __cplusplus
}
#endif

#endif /* GAZETTE_INDEX_H */
