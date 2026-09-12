/*
 * Gazette — preferences, groups and feed list
 * Copyright (c) 2026 brunocastello
 *
 * PORTABLE: no Mac system headers. See gazette_prefs.h.
 */

#include "gazette_prefs.h"

#include "portable/gazette_portable.h"

#include <string.h>

/*
 * The one feed a fresh install starts with, so a first run has something in
 * the window rather than an empty sidebar and no way to guess what goes in it.
 * It is a starting point and nothing more: Gazette is a general RSS and Atom
 * reader, this feed is subscribed to exactly like any other, and removing it
 * is a normal thing to do.
 */
static const char kStarterFeedURL[] =
    "https://news.google.com/rss?hl=en-US&gl=US&ceid=US:en";
static const char kStarterFeedTitle[] = "Google News - Top Stories";

enum {
    kDefaultRefreshMinutes = 30,
    kDefaultMaxArticles    = 100
};

/*
 * kGazetteMaxFeeds, kGazetteMaxGroups and kGazettePrefsTextMax are coupled,
 * and silently: if the lists can hold more than the text buffer can
 * serialise, GazettePrefsSerialize returns 0, saving gives up, and nothing
 * crashes or complains — the user's edits just stop surviving a quit. That
 * would present months later as "my feeds keep disappearing".
 *
 * Worst case is every entry at full length: a feed line is "feed     = " (11)
 * + a 511-byte URL + " | " (3) + a 127-byte title + CR, and a group line is
 * "group-closed = " (15) + a 63-byte name + CR. Plus the settings block and
 * comments, for which 768 is generous — the block is eight settings and
 * three comment lines, and comes to under four hundred bytes.
 *
 * The array below has a negative size if that ever stops holding, which turns
 * the bug into a compile error on the line that raised the limit.
 */
typedef char gazette_prefs_text_buffer_is_large_enough[
    (kGazettePrefsTextMax >
     kGazetteMaxFeeds * (11 + kGazetteURLLen + 3 + kGazetteTitleLen + 1) +
     kGazetteMaxGroups * (15 + kGazetteGroupLen + 1) + 768)
    ? 1 : -1];

/* ------------------------------------------------------------------ */
/* Order                                                              */
/* ------------------------------------------------------------------ */

/*
 * The feeds array is kept in sidebar order: the top-level feeds first, then
 * each group's feeds in group order, relative order preserved throughout.
 *
 * Keeping it that way rather than sorting on demand means the sidebar, the
 * serialiser and a drag all read the same sequence, and none of them has to
 * re-derive it. Every mutation below restores the invariant before returning.
 */
static void RebuildOrder(GazettePrefs *p)
{
    int order[kGazetteMaxFeeds];
    int where[kGazetteMaxFeeds];
    int n = 0;
    int g;
    int i;

    /* Collect indices group by group, which is the order we want them in.
       A feed naming a group that no longer exists is a top-level feed: it
       would otherwise vanish, and it has to be adopted here rather than
       appended afterwards, because the top level being a prefix of the array
       is what the sidebar's row arithmetic reads. */
    for (i = 0; i < p->feedCount; i++) {
        if (p->feeds[i].group >= p->groupCount) {
            p->feeds[i].group = -1;
        }
        if (p->feeds[i].group < 0) {
            order[n++] = i;
        }
    }
    for (g = 0; g < p->groupCount; g++) {
        for (i = 0; i < p->feedCount; i++) {
            if (p->feeds[i].group == g) {
                order[n++] = i;
            }
        }
    }
    if (n != p->feedCount) {
        return;                     /* cannot happen; refuse to scramble */
    }

    /*
     * Apply the permutation in place, following each cycle, so this costs one
     * feed of scratch rather than a copy of the whole 83 KB array.
     */
    for (i = 0; i < n; i++) {
        where[order[i]] = i;
    }
    for (i = 0; i < n; i++) {
        while (where[i] != i) {
            GazetteFeedPref tmp;
            int             j = where[i];

            tmp          = p->feeds[i];
            p->feeds[i]  = p->feeds[j];
            p->feeds[j]  = tmp;

            where[i]     = where[j];
            where[j]     = j;
        }
    }
}

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
    /* An article is read in full or it is read in summary, and the summary
       was never the thing anyone wanted. It is no longer a choice. */
    p->fullText       = 1;
    gz_copy_n(p->country, sizeof p->country, "US", 2);

    /* Newest on top, everything shown, the sidebar out: what the window looks
       like the first time it opens. memset has already said so; these are
       here because a default worth knowing is worth writing down. */
    p->oldestFirst      = 0;
    p->hideReadArticles = 0;
    p->hideReadFeeds    = 0;
    p->hideSidebar      = 0;

    GazettePrefsAddFeed(p, kStarterFeedURL, kStarterFeedTitle, -1);
}

/* ------------------------------------------------------------------ */
/* Groups                                                             */
/* ------------------------------------------------------------------ */

int GazettePrefsAddGroup(GazettePrefs *p, const char *name)
{
    GazetteGroupPref *g;

    if (p == NULL || name == NULL || name[0] == '\0') {
        return -1;
    }
    if (p->groupCount >= kGazetteMaxGroups) {
        return -1;
    }

    g = &p->groups[p->groupCount];
    memset(g, 0, sizeof *g);
    gz_copy_n(g->name, sizeof g->name, name, strlen(name));

    return p->groupCount++;
}

int GazettePrefsRemoveGroup(GazettePrefs *p, int index)
{
    int i;

    if (p == NULL || index < 0 || index >= p->groupCount) {
        return 0;
    }

    /* The feeds are not the group's to take with it: removing a folder should
       never silently unsubscribe anything, so they move to the top level. */
    for (i = 0; i < p->feedCount; i++) {
        if (p->feeds[i].group == index) {
            p->feeds[i].group = -1;
        } else if (p->feeds[i].group > index) {
            p->feeds[i].group--;
        }
    }

    for (i = index; i < p->groupCount - 1; i++) {
        p->groups[i] = p->groups[i + 1];
    }
    memset(&p->groups[p->groupCount - 1], 0, sizeof p->groups[0]);
    p->groupCount--;

    RebuildOrder(p);
    return 1;
}

int GazettePrefsRenameGroup(GazettePrefs *p, int index, const char *name)
{
    if (p == NULL || index < 0 || index >= p->groupCount ||
        name == NULL || name[0] == '\0') {
        return 0;
    }
    gz_copy_n(p->groups[index].name, sizeof p->groups[index].name,
              name, strlen(name));
    return 1;
}

int GazettePrefsMoveGroup(GazettePrefs *p, int from, int to)
{
    GazetteGroupPref moved;
    int              i;

    if (p == NULL || from < 0 || from >= p->groupCount) {
        return -1;
    }
    if (to < 0) {
        to = 0;
    }
    if (to >= p->groupCount) {
        to = p->groupCount - 1;
    }
    if (to == from) {
        return from;
    }

    moved = p->groups[from];

    /* Renumber the feeds to follow their group to its new index. */
    for (i = 0; i < p->feedCount; i++) {
        int g = p->feeds[i].group;

        if (g < 0) {
            continue;
        }
        if (g == from) {
            p->feeds[i].group = to;
        } else if (from < to && g > from && g <= to) {
            p->feeds[i].group = g - 1;
        } else if (from > to && g >= to && g < from) {
            p->feeds[i].group = g + 1;
        }
    }

    if (from < to) {
        for (i = from; i < to; i++) {
            p->groups[i] = p->groups[i + 1];
        }
    } else {
        for (i = from; i > to; i--) {
            p->groups[i] = p->groups[i - 1];
        }
    }
    p->groups[to] = moved;

    RebuildOrder(p);
    return to;
}

int GazettePrefsFirstFeedInGroup(const GazettePrefs *p, int group)
{
    int i;

    if (p == NULL) {
        return -1;
    }
    for (i = 0; i < p->feedCount; i++) {
        if (p->feeds[i].group == group) {
            return i;
        }
    }
    return -1;
}

int GazettePrefsGroupFeedCount(const GazettePrefs *p, int group)
{
    int i;
    int n = 0;

    if (p == NULL) {
        return 0;
    }
    for (i = 0; i < p->feedCount; i++) {
        if (p->feeds[i].group == group) {
            n++;
        }
    }
    return n;
}

/* ------------------------------------------------------------------ */
/* The sidebar's rows                                                  */
/*                                                                     */
/* All of this is arithmetic over the order RebuildOrder maintains: the */
/* top-level feeds are a prefix of the array, and each group's feeds    */
/* are contiguous after them, in group order. Nothing here searches.    */
/* ------------------------------------------------------------------ */

/*
 * Every one of these walks the feed list in drawing order rather than doing
 * arithmetic over it. The order is still what RebuildOrder maintains — the
 * top-level feeds first, then each group's, contiguous — but a hidden feed
 * takes no row, and once rows and feeds no longer march in step there is no
 * arithmetic to do. 128 feeds is a walk nobody will ever measure.
 */
static int FeedShown(const GazettePrefs *p, int i)
{
    return !p->feeds[i].hidden;
}

static int GroupShown(const GazettePrefs *p, int g)
{
    return !p->groups[g].hidden;
}

int GazettePrefsRowForSmart(int which)
{
    return (which >= 0 && which < kGazetteSmartCount) ? which : -1;
}

const char *GazettePrefsSmartName(int which)
{
    switch (which) {
        case kGazetteSmartToday:   return "Today";
        case kGazetteSmartUnread:  return "All Unread";
        case kGazetteSmartStarred: return "Starred";
        default:                   return "";
    }
}

int GazettePrefsRowCount(const GazettePrefs *p)
{
    int n = kGazetteSmartCount;     /* the three standing views come first */
    int i;
    int g;

    if (p == NULL) {
        return 0;
    }
    for (i = 0; i < p->feedCount && p->feeds[i].group < 0; i++) {
        if (FeedShown(p, i)) {
            n++;
        }
    }
    for (g = 0; g < p->groupCount; g++) {
        if (!GroupShown(p, g)) {
            continue;
        }
        n++;
        if (p->groups[g].collapsed) {
            continue;
        }
        for (i = 0; i < p->feedCount; i++) {
            if (p->feeds[i].group == g && FeedShown(p, i)) {
                n++;
            }
        }
    }
    return n;
}

int GazettePrefsRowAt(const GazettePrefs *p, int row, GazetteSidebarRow *out)
{
    int i;
    int g;

    if (p == NULL || out == NULL || row < 0) {
        return 0;
    }

    if (row < kGazetteSmartCount) {
        out->kind  = kGazetteRowSmart;
        out->index = row;
        return 1;
    }
    row -= kGazetteSmartCount;

    for (i = 0; i < p->feedCount && p->feeds[i].group < 0; i++) {
        if (!FeedShown(p, i)) {
            continue;
        }
        if (row-- == 0) {
            out->kind  = kGazetteRowFeed;
            out->index = i;
            return 1;
        }
    }

    for (g = 0; g < p->groupCount; g++) {
        if (!GroupShown(p, g)) {
            continue;
        }
        if (row-- == 0) {
            out->kind  = kGazetteRowGroup;
            out->index = g;
            return 1;
        }
        if (p->groups[g].collapsed) {
            continue;
        }
        for (i = 0; i < p->feedCount; i++) {
            if (p->feeds[i].group != g || !FeedShown(p, i)) {
                continue;
            }
            if (row-- == 0) {
                out->kind  = kGazetteRowFeed;
                out->index = i;
                return 1;
            }
        }
    }
    return 0;
}

/* Both of these are the walk above, stopped when it reaches what was asked
   for. Written out twice rather than through a callback: two short loops read
   better here than one indirect one. */
int GazettePrefsRowForGroup(const GazettePrefs *p, int group)
{
    int row = kGazetteSmartCount;
    int i;
    int g;

    if (p == NULL || group < 0 || group >= p->groupCount ||
        !GroupShown(p, group)) {
        return -1;
    }
    for (i = 0; i < p->feedCount && p->feeds[i].group < 0; i++) {
        if (FeedShown(p, i)) {
            row++;
        }
    }
    for (g = 0; g < p->groupCount; g++) {
        if (!GroupShown(p, g)) {
            continue;
        }
        if (g == group) {
            return row;
        }
        row++;
        if (p->groups[g].collapsed) {
            continue;
        }
        for (i = 0; i < p->feedCount; i++) {
            if (p->feeds[i].group == g && FeedShown(p, i)) {
                row++;
            }
        }
    }
    return -1;
}

int GazettePrefsRowForFeed(const GazettePrefs *p, int feed)
{
    int group;
    int row;
    int i;

    if (p == NULL || feed < 0 || feed >= p->feedCount ||
        !FeedShown(p, feed)) {
        return -1;
    }

    group = p->feeds[feed].group;
    if (group < 0) {
        row = kGazetteSmartCount;
        for (i = 0; i < feed; i++) {
            if (FeedShown(p, i)) {
                row++;
            }
        }
        return row;
    }

    if (group >= p->groupCount || p->groups[group].collapsed ||
        !GroupShown(p, group)) {
        return -1;              /* drawn nowhere: its group is shut or gone */
    }

    row = GazettePrefsRowForGroup(p, group) + 1;
    for (i = 0; i < feed; i++) {
        if (p->feeds[i].group == group && FeedShown(p, i)) {
            row++;
        }
    }
    return row;
}

void GazettePrefsShowAll(GazettePrefs *p)
{
    int i;

    if (p == NULL) {
        return;
    }
    for (i = 0; i < p->feedCount; i++) {
        p->feeds[i].hidden = 0;
    }
    for (i = 0; i < p->groupCount; i++) {
        p->groups[i].hidden = 0;
    }
}

/* ------------------------------------------------------------------ */
/* Feeds                                                              */
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

int GazettePrefsAddFeed(GazettePrefs *p, const char *url, const char *title,
                        int group)
{
    GazetteFeedPref *entry;

    if (p == NULL || url == NULL || url[0] == '\0') {
        return -1;
    }
    if (p->feedCount >= kGazetteMaxFeeds) {
        return -1;
    }
    if (GazettePrefsFindFeed(p, url) >= 0) {
        return -1;
    }
    if (group < -1 || group >= p->groupCount) {
        group = -1;
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
    entry->group   = group;

    p->feedCount++;
    RebuildOrder(p);

    return GazettePrefsFindFeed(p, url);
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

int GazettePrefsRenameFeed(GazettePrefs *p, int index, const char *title)
{
    if (p == NULL || index < 0 || index >= p->feedCount ||
        title == NULL || title[0] == '\0') {
        return 0;
    }
    gz_copy_n(p->feeds[index].title, sizeof p->feeds[index].title,
              title, strlen(title));
    return 1;
}

int GazettePrefsSetFeedEnabled(GazettePrefs *p, int index, int enabled)
{
    if (p == NULL || index < 0 || index >= p->feedCount) {
        return 0;
    }
    p->feeds[index].enabled = enabled ? 1 : 0;
    return 1;
}

int GazettePrefsSetFeedURL(GazettePrefs *p, int index, const char *url)
{
    int existing;

    if (p == NULL || index < 0 || index >= p->feedCount ||
        url == NULL || url[0] == '\0') {
        return 0;
    }

    /* Two entries for one address would refresh into one cache file and read
       back as each other. Re-typing the address a feed already has is not a
       clash, though -- it is a no-op the user is entitled to. */
    existing = GazettePrefsFindFeed(p, url);
    if (existing >= 0 && existing != index) {
        return 0;
    }

    gz_copy_n(p->feeds[index].url, sizeof p->feeds[index].url,
              url, strlen(url));
    return 1;
}

int GazettePrefsMoveFeed(GazettePrefs *p, int from, int to, int group)
{
    GazetteFeedPref moved;
    int             i;

    if (p == NULL || from < 0 || from >= p->feedCount) {
        return -1;
    }
    if (group < -1 || group >= p->groupCount) {
        group = -1;
    }
    if (to < 0) {
        to = 0;
    }
    if (to > p->feedCount - 1) {
        to = p->feedCount - 1;
    }

    moved       = p->feeds[from];
    moved.group = group;

    if (from < to) {
        for (i = from; i < to; i++) {
            p->feeds[i] = p->feeds[i + 1];
        }
    } else {
        for (i = from; i > to; i--) {
            p->feeds[i] = p->feeds[i - 1];
        }
    }
    p->feeds[to] = moved;

    /* The drop position and the target group can disagree — dropping onto a
       collapsed group, say. The group wins, and RebuildOrder puts the feed
       where that decision implies. */
    RebuildOrder(p);
    return GazettePrefsFindFeed(p, moved.url);
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

int GazettePrefsParse(const char *text, size_t len, GazettePrefs *p)
{
    char   key[64];
    char   value[kGazetteURLLen + kGazetteTitleLen + 8];
    char   url[kGazetteURLLen];
    char   title[kGazetteTitleLen];
    size_t off        = 0;
    int    group      = -1;
    int    sawAnyFeed = 0;

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
    (void)gz_prefs_get(text, len, "country", p->country, sizeof p->country);

    p->oldestFirst      = gz_prefs_get_num(text, len, "sort-oldest-first",
                                           p->oldestFirst) ? 1 : 0;
    p->hideReadArticles = gz_prefs_get_num(text, len, "hide-read-articles",
                                           p->hideReadArticles) ? 1 : 0;
    p->hideReadFeeds    = gz_prefs_get_num(text, len, "hide-read-feeds",
                                           p->hideReadFeeds) ? 1 : 0;
    p->hideSidebar      = gz_prefs_get_num(text, len, "hide-sidebar",
                                           p->hideSidebar) ? 1 : 0;

    if (p->refreshMinutes < 0) {
        p->refreshMinutes = 0;
    }
    if (p->maxArticles < 1) {
        p->maxArticles = 1;
    }
    if (p->country[0] == '\0') {
        gz_copy_n(p->country, sizeof p->country, "US", 2);
    }

    /*
     * A file that names any feed at all replaces the starter list outright —
     * otherwise a user who deliberately removed a feed would find it back on
     * the next launch. The walk is sequential because the tree is carried by
     * the order of the lines and by nothing else.
     */
    while (gz_prefs_next(text, len, &off, key, sizeof key,
                         value, sizeof value)) {
        int enabled;

        if (gz_stricmp(key, "group") == 0 ||
            gz_stricmp(key, "group-closed") == 0) {
            group = GazettePrefsAddGroup(p, value);
            if (group >= 0 && gz_stricmp(key, "group-closed") == 0) {
                p->groups[group].collapsed = 1;
            }
            continue;
        }

        if (gz_stricmp(key, "feed") == 0) {
            enabled = 1;
        } else if (gz_stricmp(key, "feed-off") == 0) {
            enabled = 0;
        } else {
            continue;
        }

        if (!sawAnyFeed) {
            /* The first feed line in the file discards the starter list. Done
               here rather than up front so the groups seen before it survive. */
            int i;

            for (i = 0; i < p->feedCount; i++) {
                memset(&p->feeds[i], 0, sizeof p->feeds[i]);
            }
            p->feedCount = 0;
            sawAnyFeed   = 1;
        }

        SplitFeedValue(value, url, sizeof url, title, sizeof title);
        {
            int index = GazettePrefsAddFeed(p, url, title, group);

            if (index >= 0) {
                p->feeds[index].enabled = enabled;
            }
        }
    }

    /* Groups declared before the first feed line still count as declared, so
       a file that lists only groups keeps them and the starter feed both. */
    RebuildOrder(p);
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

static void AppendFeed(char *out, size_t cap, size_t *len,
                       const GazetteFeedPref *f)
{
    Append(out, cap, len, f->enabled ? "feed     = " : "feed-off = ");
    Append(out, cap, len, f->url);
    if (f->title[0] != '\0') {
        Append(out, cap, len, " | ");
        Append(out, cap, len, f->title);
    }
    Append(out, cap, len, "\r");
}

size_t GazettePrefsSerialize(const GazettePrefs *p, char *out, size_t cap)
{
    size_t len = 0;
    int    g;
    int    i;

    if (out == NULL || cap == 0) {
        return 0;
    }
    out[0] = '\0';
    if (p == NULL) {
        return 0;
    }

    /* Classic Mac text files are CR-terminated, and SimpleText and BBEdit
       both expect that on OS 9. The reader trims \r and \n alike, so a hand
       edit from another machine still loads. */
    Append(out, cap, &len, "# Gazette Preferences\r");
    Append(out, cap, &len, "# Edited by hand or by Gazette; either is fine.\r");
    Append(out, cap, &len, "# The order of the lines is the order of the sidebar.\r");
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

    Append(out, cap, &len, "country         = ");
    Append(out, cap, &len, p->country);
    Append(out, cap, &len, "\r\r");

    /* What the View menu is holding. Written out so the window comes back the
       way it was left. */
    Append(out, cap, &len, "sort-oldest-first  = ");
    AppendNum(out, cap, &len, p->oldestFirst ? 1 : 0);
    Append(out, cap, &len, "\r");

    Append(out, cap, &len, "hide-read-articles = ");
    AppendNum(out, cap, &len, p->hideReadArticles ? 1 : 0);
    Append(out, cap, &len, "\r");

    Append(out, cap, &len, "hide-read-feeds    = ");
    AppendNum(out, cap, &len, p->hideReadFeeds ? 1 : 0);
    Append(out, cap, &len, "\r");

    Append(out, cap, &len, "hide-sidebar       = ");
    AppendNum(out, cap, &len, p->hideSidebar ? 1 : 0);
    Append(out, cap, &len, "\r\r");

    Append(out, cap, &len,
           "# feed = <url> | <title>   (feed-off = the same, disabled)\r");
    Append(out, cap, &len,
           "# Feeds after a group line belong to it; feeds before any belong "
           "to none.\r\r");

    for (i = 0; i < p->feedCount; i++) {
        if (p->feeds[i].group < 0) {
            AppendFeed(out, cap, &len, &p->feeds[i]);
        }
    }

    for (g = 0; g < p->groupCount; g++) {
        Append(out, cap, &len, "\r");
        Append(out, cap, &len,
               p->groups[g].collapsed ? "group-closed = " : "group        = ");
        Append(out, cap, &len, p->groups[g].name);
        Append(out, cap, &len, "\r");

        for (i = 0; i < p->feedCount; i++) {
            if (p->feeds[i].group == g) {
                AppendFeed(out, cap, &len, &p->feeds[i]);
            }
        }
    }

    if (len >= cap) {
        out[0] = '\0';
        return 0;
    }
    out[len] = '\0';
    return len;
}
