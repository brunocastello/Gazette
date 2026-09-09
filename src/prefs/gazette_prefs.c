/*
 * Gazette — preferences and feed list
 * Copyright (c) 2026 brunocastello
 *
 * PORTABLE: no Mac system headers. See gazette_prefs.h.
 */

#include "gazette_prefs.h"

#include "portable/gazette_portable.h"

#include <string.h>

/* Google News Top Stories for the US, the same endpoint shape NewsProxy
   builds: /rss with hl (interface language), gl (country) and ceid
   (country:language). Phase 2 generates these from the country and topic
   maps; until then it is the one feed a fresh install starts with. */
static const char kDefaultFeedURL[] =
    "https://news.google.com/rss?hl=en-US&gl=US&ceid=US:en";
static const char kDefaultFeedTitle[] = "Google News - Top Stories";

enum {
    kDefaultRefreshMinutes = 30,
    kDefaultMaxArticles    = 100
};

/* ------------------------------------------------------------------ */
/* Defaults                                                            */
/* ------------------------------------------------------------------ */

void GazettePrefsSetDefaults(GazettePrefs *p)
{
    if (p == NULL) {
        return;
    }

    memset(p, 0, sizeof *p);

    p->refreshMinutes = kDefaultRefreshMinutes;
    p->maxArticles    = kDefaultMaxArticles;
    p->fullText       = 0;

    GazettePrefsAddFeed(p, kDefaultFeedURL, kDefaultFeedTitle);
}

/* ------------------------------------------------------------------ */
/* Feed list                                                           */
/* ------------------------------------------------------------------ */

int GazettePrefsFindFeed(const GazettePrefs *p, const char *url)
{
    int i;

    if (p == NULL || url == NULL || url[0] == '\0') {
        return -1;
    }
    for (i = 0; i < p->feedCount; i++) {
        if (gz_stricmp(p->feeds[i].url, url) == 0) {
            return i;
        }
    }
    return -1;
}

int GazettePrefsAddFeed(GazettePrefs *p, const char *url, const char *title)
{
    GazetteFeedPref *entry;

    if (p == NULL || url == NULL || url[0] == '\0') {
        return 0;
    }
    if (p->feedCount >= kGazetteMaxFeeds) {
        return 0;
    }
    if (GazettePrefsFindFeed(p, url) >= 0) {
        return 0;
    }

    entry = &p->feeds[p->feedCount];
    memset(entry, 0, sizeof *entry);

    gz_copy_n(entry->url, sizeof entry->url, url, strlen(url));
    if (title != NULL && title[0] != '\0') {
        gz_copy_n(entry->title, sizeof entry->title, title, strlen(title));
    } else {
        gz_copy_n(entry->title, sizeof entry->title, url, strlen(url));
    }
    entry->enabled = 1;

    p->feedCount++;
    return 1;
}

int GazettePrefsRemoveFeed(GazettePrefs *p, const char *url)
{
    int index = GazettePrefsFindFeed(p, url);
    int i;

    if (index < 0) {
        return 0;
    }
    for (i = index; i < p->feedCount - 1; i++) {
        p->feeds[i] = p->feeds[i + 1];
    }
    memset(&p->feeds[p->feedCount - 1], 0, sizeof p->feeds[0]);
    p->feedCount--;
    return 1;
}

/* ------------------------------------------------------------------ */
/* Parsing                                                             */
/* ------------------------------------------------------------------ */

/* Split "<url> | <title>" into its two trimmed halves. With no pipe the
   whole value is the URL and the title comes out empty, which AddFeed then
   fills in with the URL. */
static void SplitFeedValue(const char *value,
                           char *url, size_t urlCap,
                           char *title, size_t titleCap)
{
    const char *pipe = strchr(value, '|');
    const char *part;
    size_t      partLen;

    if (pipe == NULL) {
        part = gz_trim(value, strlen(value), &partLen);
        gz_copy_n(url, urlCap, part, partLen);
        if (titleCap > 0) {
            title[0] = '\0';
        }
        return;
    }

    part = gz_trim(value, (size_t)(pipe - value), &partLen);
    gz_copy_n(url, urlCap, part, partLen);

    part = gz_trim(pipe + 1, strlen(pipe + 1), &partLen);
    gz_copy_n(title, titleCap, part, partLen);
}

/* Read every occurrence of key as a feed line, marking each entry enabled
   or not. Returns how many were added. */
static int ReadFeedKey(const char *text, size_t len, const char *key,
                       int enabled, GazettePrefs *p)
{
    char value[kGazetteURLLen + kGazetteTitleLen + 8];
    char url[kGazetteURLLen];
    char title[kGazetteTitleLen];
    int  n     = 0;
    int  added = 0;

    while (gz_prefs_get_nth(text, len, key, n++, value, sizeof value)) {
        SplitFeedValue(value, url, sizeof url, title, sizeof title);
        if (GazettePrefsAddFeed(p, url, title)) {
            p->feeds[p->feedCount - 1].enabled = enabled;
            added++;
        }
        if (p->feedCount >= kGazetteMaxFeeds) {
            break;
        }
    }
    return added;
}

int GazettePrefsParse(const char *text, size_t len, GazettePrefs *p)
{
    if (p == NULL) {
        return 0;
    }

    GazettePrefsSetDefaults(p);

    if (text == NULL || len == 0) {
        return p->feedCount;
    }

    p->refreshMinutes = gz_prefs_get_num(text, len, "refresh-minutes",
                                         p->refreshMinutes);
    p->maxArticles    = gz_prefs_get_num(text, len, "max-articles",
                                         p->maxArticles);
    p->fullText       = gz_prefs_get_num(text, len, "full-text",
                                         p->fullText) ? 1 : 0;

    if (p->refreshMinutes < 0) {
        p->refreshMinutes = 0;
    }
    if (p->maxArticles < 1) {
        p->maxArticles = 1;
    }

    /* A file that names any feed at all replaces the default list outright —
       otherwise a user who deliberately removed Google News would find it
       back on the next launch. */
    if (gz_prefs_get_nth(text, len, "feed", 0, NULL, 0) ||
        gz_prefs_get_nth(text, len, "feed-off", 0, NULL, 0)) {
        p->feedCount = 0;
        memset(p->feeds, 0, sizeof p->feeds);

        ReadFeedKey(text, len, "feed", 1, p);
        ReadFeedKey(text, len, "feed-off", 0, p);
    }

    return p->feedCount;
}

/* ------------------------------------------------------------------ */
/* Serialising                                                         */
/* ------------------------------------------------------------------ */

/* Append src to out, tracking the running length. Once the buffer is full
   the length keeps growing past cap so the caller can tell it overflowed. */
static void Append(char *out, size_t cap, size_t *len, const char *src)
{
    size_t n = strlen(src);

    if (*len + n < cap) {
        memcpy(out + *len, src, n);
    }
    *len += n;
}

static void AppendNum(char *out, size_t cap, size_t *len, long value)
{
    char  buf[16];
    char *q = buf + sizeof buf;
    long  v = value;
    int   negative = (v < 0);

    *--q = '\0';
    if (v == 0) {
        *--q = '0';
    }
    while (v != 0) {
        long digit = v % 10;
        if (digit < 0) {
            digit = -digit;
        }
        *--q = (char)('0' + digit);
        v /= 10;
    }
    if (negative) {
        *--q = '-';
    }
    Append(out, cap, len, q);
}

size_t GazettePrefsSerialize(const GazettePrefs *p, char *out, size_t cap)
{
    size_t len = 0;
    int    i;

    if (out == NULL || cap == 0) {
        return 0;
    }
    out[0] = '\0';
    if (p == NULL) {
        return 0;
    }

    /* Classic Mac text files are CR-terminated, and SimpleText and
       BBEdit both expect that on OS 9. gz_prefs_get_nth trims \r and \n
       alike, so reading back stays indifferent to which a hand edit left. */
    Append(out, cap, &len, "# Gazette Preferences\r");
    Append(out, cap, &len, "# Edited by hand or by Gazette; either is fine.\r");
    Append(out, cap, &len, "\r");

    Append(out, cap, &len, "refresh-minutes = ");
    AppendNum(out, cap, &len, p->refreshMinutes);
    Append(out, cap, &len, "\r");

    Append(out, cap, &len, "max-articles    = ");
    AppendNum(out, cap, &len, p->maxArticles);
    Append(out, cap, &len, "\r");

    Append(out, cap, &len, "full-text       = ");
    AppendNum(out, cap, &len, p->fullText ? 1 : 0);
    Append(out, cap, &len, "\r");

    Append(out, cap, &len, "\r");
    Append(out, cap, &len, "# feed = <url> | <title>   (feed-off = the same, disabled)\r");

    for (i = 0; i < p->feedCount; i++) {
        Append(out, cap, &len, p->feeds[i].enabled ? "feed     = " : "feed-off = ");
        Append(out, cap, &len, p->feeds[i].url);
        if (p->feeds[i].title[0] != '\0') {
            Append(out, cap, &len, " | ");
            Append(out, cap, &len, p->feeds[i].title);
        }
        Append(out, cap, &len, "\r");
    }

    if (len >= cap) {
        out[0] = '\0';
        return 0;
    }
    out[len] = '\0';
    return len;
}
