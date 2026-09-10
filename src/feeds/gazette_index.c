/*
 * Gazette — what is remembered between launches that belongs to no one feed
 * Copyright (c) 2026 brunocastello
 *
 * See gazette_index.h.
 */

#include "feeds/gazette_index.h"

#include "feeds/gazette_feeds.h"        /* kGazetteMaxArticles, for sizing */
#include "portable/gazette_portable.h"
#include "prefs/gazette_prefs.h"        /* kGazetteMaxFeeds */
#include "store/gazette_store.h"

#include <stdio.h>
#include <string.h>

static const char kIndexFileName[] = "Gazette Index";
static const char kIndexMagic[]    = "GAZETTE-INDEX 1";

typedef struct {
    unsigned long key;              /* hash of the feed's URL */
    short         total;
    short         unread;
} FeedCounts;

/*
 * Read links, in the order they were read. Insertion order is what makes
 * eviction mean "the oldest" rather than "an arbitrary one", and it is why
 * this is a plain array rather than something sorted: a lookup costs a scan,
 * but a lookup only happens when a feed's articles are loaded, so it is a few
 * hundred scans at a feed switch and one when an article is opened.
 */
static unsigned long gRead[kGazetteMaxReadArticles];
static int           gReadCount;

static FeedCounts    gFeeds[kGazetteMaxFeeds];
static int           gFeedCount;

static int           gDirty;

/* FNV-1a, the same hash the cache uses for its file names. Nothing here needs
   a good hash, only a cheap and well-mixed one. */
static unsigned long Hash(const char *s)
{
    unsigned long h = 2166136261UL;

    if (s == NULL) {
        return 0;
    }
    while (*s != '\0') {
        h ^= (unsigned long)(unsigned char)*s++;
        h *= 16777619UL;
        h &= 0xFFFFFFFFUL;
    }
    return h;
}

/* ------------------------------------------------------------------ */
/* Read articles                                                       */
/* ------------------------------------------------------------------ */

static int FindRead(unsigned long key)
{
    int i;

    for (i = 0; i < gReadCount; i++) {
        if (gRead[i] == key) {
            return i;
        }
    }
    return -1;
}

int GazetteIndexIsRead(const char *link)
{
    if (link == NULL || link[0] == '\0') {
        return 0;
    }
    return (FindRead(Hash(link)) >= 0);
}

void GazetteIndexSetRead(const char *link, int read)
{
    unsigned long key;
    int           at;

    if (link == NULL || link[0] == '\0') {
        return;
    }
    key = Hash(link);
    at  = FindRead(key);

    if (!read) {
        if (at < 0) {
            return;
        }
        memmove(&gRead[at], &gRead[at + 1],
                (size_t)(gReadCount - at - 1) * sizeof gRead[0]);
        gReadCount--;
        gDirty = 1;
        return;
    }

    if (at >= 0) {
        return;
    }
    if (gReadCount >= kGazetteMaxReadArticles) {
        /*
         * Full: drop the oldest eighth rather than one entry, so this costs
         * one shift every thousand articles instead of a shift every time.
         */
        int drop = kGazetteMaxReadArticles / 8;

        memmove(&gRead[0], &gRead[drop],
                (size_t)(gReadCount - drop) * sizeof gRead[0]);
        gReadCount -= drop;
    }
    gRead[gReadCount++] = key;
    gDirty = 1;
}

/* ------------------------------------------------------------------ */
/* Per-feed counts                                                     */
/* ------------------------------------------------------------------ */

static FeedCounts *FindFeed(unsigned long key, int create)
{
    int i;

    for (i = 0; i < gFeedCount; i++) {
        if (gFeeds[i].key == key) {
            return &gFeeds[i];
        }
    }
    if (!create || gFeedCount >= kGazetteMaxFeeds) {
        return NULL;
    }
    gFeeds[gFeedCount].key    = key;
    gFeeds[gFeedCount].total  = 0;
    gFeeds[gFeedCount].unread = 0;
    return &gFeeds[gFeedCount++];
}

void GazetteIndexSetFeedCounts(const char *feedURL, int total, int unread)
{
    FeedCounts *f;

    if (feedURL == NULL || feedURL[0] == '\0') {
        return;
    }
    f = FindFeed(Hash(feedURL), 1);
    if (f == NULL) {
        return;
    }
    if (f->total == (short)total && f->unread == (short)unread) {
        return;
    }
    f->total  = (short)total;
    f->unread = (short)unread;
    gDirty    = 1;
}

int GazetteIndexFeedUnread(const char *feedURL)
{
    FeedCounts *f;

    if (feedURL == NULL || feedURL[0] == '\0') {
        return 0;
    }
    f = FindFeed(Hash(feedURL), 0);
    return (f != NULL) ? f->unread : 0;
}

int GazetteIndexFeedTotal(const char *feedURL)
{
    FeedCounts *f;

    if (feedURL == NULL || feedURL[0] == '\0') {
        return 0;
    }
    f = FindFeed(Hash(feedURL), 0);
    return (f != NULL) ? f->total : 0;
}

void GazetteIndexForgetFeed(const char *feedURL)
{
    unsigned long key;
    int           i;

    if (feedURL == NULL || feedURL[0] == '\0') {
        return;
    }
    key = Hash(feedURL);
    for (i = 0; i < gFeedCount; i++) {
        if (gFeeds[i].key == key) {
            memmove(&gFeeds[i], &gFeeds[i + 1],
                    (size_t)(gFeedCount - i - 1) * sizeof gFeeds[0]);
            gFeedCount--;
            gDirty = 1;
            return;
        }
    }
}

/* ------------------------------------------------------------------ */
/* The file                                                            */
/*                                                                     */
/* One line per entry, tagged by a letter, the same shape as the        */
/* article cache — readable in SimpleText, and a truncated file costs   */
/* whatever was not read rather than the whole of it.                   */
/* ------------------------------------------------------------------ */

static unsigned long ParseHex(const char *s)
{
    unsigned long v = 0;

    while (*s != '\0') {
        char c = *s++;
        int  d;

        if (c >= '0' && c <= '9')      d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else break;

        v = (v << 4) | (unsigned long)d;
    }
    return v;
}

void GazetteIndexLoad(void)
{
    GazetteStoreFile *f;
    char              line[128];
    long              n;

    gReadCount = 0;
    gFeedCount = 0;
    gDirty     = 0;

    f = GazetteStoreDataOpen(kIndexFileName);
    if (f == NULL) {
        return;                     /* a first run; an empty index is right */
    }

    n = GazetteStoreReadLine(f, line, (long)sizeof line);
    if (n < 0 || strcmp(line, kIndexMagic) != 0) {
        GazetteStoreClose(f);
        return;
    }

    while ((n = GazetteStoreReadLine(f, line, (long)sizeof line)) >= 0) {
        if (n < 3) {
            continue;
        }
        if (line[0] == 'R') {
            if (gReadCount < kGazetteMaxReadArticles) {
                gRead[gReadCount++] = ParseHex(line + 2);
            }
        } else if (line[0] == 'F') {
            /* "F <hash> <total> <unread>" */
            const char *at = line + 2;
            FeedCounts *fc;

            fc = FindFeed(ParseHex(at), 1);
            if (fc == NULL) {
                continue;
            }
            while (*at != '\0' && *at != ' ') at++;
            while (*at == ' ') at++;
            fc->total = (short)gz_parse_dec(at, strlen(at), 0);

            while (*at != '\0' && *at != ' ') at++;
            while (*at == ' ') at++;
            fc->unread = (short)gz_parse_dec(at, strlen(at), 0);
        }
    }

    GazetteStoreClose(f);
}

void GazetteIndexSave(void)
{
    GazetteStoreFile *f;
    char              line[128];
    int               i;

    if (!gDirty) {
        return;
    }

    f = GazetteStoreDataCreate(kIndexFileName);
    if (f == NULL) {
        return;         /* an index that cannot be written is not an error */
    }

    GazetteStoreWriteLine(f, kIndexMagic);

    for (i = 0; i < gFeedCount; i++) {
        snprintf(line, sizeof line, "F %08lX %d %d", gFeeds[i].key,
                 (int)gFeeds[i].total, (int)gFeeds[i].unread);
        GazetteStoreWriteLine(f, line);
    }
    for (i = 0; i < gReadCount; i++) {
        snprintf(line, sizeof line, "R %08lX", gRead[i]);
        GazetteStoreWriteLine(f, line);
    }

    GazetteStoreClose(f);
    gDirty = 0;
}
