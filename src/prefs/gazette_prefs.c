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
    kDefaultMaxArticles    = 0      /* as many as the feed offers */
};

/*
 * kGazetteMaxFeeds, kGazetteMaxGroups and kGazettePrefsTextMax are coupled,
 * and silently: if the lists can hold more than the text buffer can
 * serialise, GazettePrefsSerialize returns 0, saving gives up, and nothing
 * crashes or complains — the user's edits just stop surviving a quit. That
 * would present months later as "my feeds keep disappearing".
 *
 * Worst case is every entry at full length: a feed line is "feed     = " (11)
 * + a 511-byte URL + " | " (3) + a 127-byte title + " | " (3) + a 255-byte
 * home page + CR, and a group line is "group-closed = " (15) + a 63-byte name
 * + CR. Plus the settings block and
 * comments, for which 768 is generous — the block is eight settings and
 * three comment lines, and comes to under four hundred bytes.
 *
 * The array below has a negative size if that ever stops holding, which turns
 * the bug into a compile error on the line that raised the limit.
 */
typedef char gazette_prefs_text_buffer_is_large_enough[
    (kGazettePrefsTextMax >
     kGazetteMaxFeeds * (11 + kGazetteURLLen + 3 + kGazetteTitleLen +
                         3 + kGazetteHomeLen + 1) +
     kGazetteMaxGroups * (15 + kGazetteGroupLen + 1) + 768)
    ? 1 : -1];

/* ------------------------------------------------------------------ */
/* Order                                                              */
/* ------------------------------------------------------------------ */

/*
 * The sidebar is one sequence: top-level feeds and groups in the user's
 * order, each group's feeds standing where the group does. Two things carry
 * it. The feeds array is held in drawing order, top to bottom, with each
 * group's feeds contiguous. And each group records how many top-level feeds
 * come before its row — `after` — which is what lets a group stand between
 * two top-level feeds rather than after all of them, and what survives the
 * feeds around it being reordered.
 *
 * Keeping the array in drawing order rather than sorting on demand means the
 * sidebar, the serialiser and a drag all read the same sequence, and none of
 * them has to re-derive it. Every mutation below restores the invariant
 * before returning, through RebuildOrder.
 */

/* How many feeds are at the top level. */
static int TopCount(const GazettePrefs *p)
{
    int n = 0;
    int i;

    for (i = 0; i < p->feedCount; i++) {
        if (p->feeds[i].group < 0) {
            n++;
        }
    }
    return n;
}

/* A top-level feed's ordinal among the top-level feeds, or -1. */
static int TopOrdinal(const GazettePrefs *p, int feed)
{
    int t = 0;
    int i;

    if (feed < 0 || feed >= p->feedCount || p->feeds[feed].group >= 0) {
        return -1;
    }
    for (i = 0; i < feed; i++) {
        if (p->feeds[i].group < 0) {
            t++;
        }
    }
    return t;
}

/* The index of the top-level feed with this ordinal, or -1 when there are
   not that many: the place one past the last. */
static int TopFeedAt(const GazettePrefs *p, int ordinal)
{
    int t = 0;
    int i;

    for (i = 0; i < p->feedCount; i++) {
        if (p->feeds[i].group < 0) {
            if (t == ordinal) {
                return i;
            }
            t++;
        }
    }
    return -1;
}

/*
 * The sequence, from the array and the groups' positions: before the
 * top-level feed with ordinal t come the groups whose `after` is t, in group
 * order, each followed by its feeds in array order; after the last top-level
 * feed come the rest. A feed naming a group that no longer exists is a
 * top-level feed — it would otherwise vanish.
 */
int GazettePrefsSequence(const GazettePrefs *p, GazetteSidebarRow *out)
{
    int n = 0;
    int t = 0;
    int g = 0;
    int i;
    int j;

    if (p == NULL || out == NULL) {
        return 0;
    }

    for (i = 0; i <= p->feedCount; i++) {
        int top = (i < p->feedCount &&
                   (p->feeds[i].group < 0 ||
                    p->feeds[i].group >= p->groupCount));

        if (i < p->feedCount && !top) {
            continue;               /* a group's feed: emitted with the group */
        }

        /* Every group standing before this top-level feed — or, past the
           last feed, every group left. */
        while (g < p->groupCount &&
               (i == p->feedCount || p->groups[g].after <= t)) {
            out[n].kind  = kGazetteRowGroup;
            out[n].index = g;
            n++;
            for (j = 0; j < p->feedCount; j++) {
                if (p->feeds[j].group == g) {
                    out[n].kind  = kGazetteRowFeed;
                    out[n].index = j;
                    n++;
                }
            }
            g++;
        }
        if (i < p->feedCount) {
            out[n].kind  = kGazetteRowFeed;
            out[n].index = i;
            n++;
            t++;
        }
    }
    return n;
}

/*
 * Put the array back into drawing order: the order the sequence reads it in.
 * Applied as a permutation in place, following each cycle, so this costs one
 * feed of scratch rather than a copy of the whole array. Along the way the
 * groups' positions are clamped to the top-level feeds there are, and any
 * feed naming a group that is gone is adopted by the top level.
 */
static void RebuildOrder(GazettePrefs *p)
{
    static GazetteSidebarRow seq[kGazetteMaxFeeds + kGazetteMaxGroups];
    int order[kGazetteMaxFeeds];
    int where[kGazetteMaxFeeds];
    int n = 0;
    int count;
    int top;
    int i;

    for (i = 0; i < p->feedCount; i++) {
        if (p->feeds[i].group >= p->groupCount) {
            p->feeds[i].group = -1;
        }
    }
    top = TopCount(p);
    for (i = 0; i < p->groupCount; i++) {
        if (p->groups[i].after < 0) {
            p->groups[i].after = 0;
        }
        if (p->groups[i].after > top) {
            p->groups[i].after = top;
        }
    }

    count = GazettePrefsSequence(p, seq);
    for (i = 0; i < count; i++) {
        if (seq[i].kind == kGazetteRowFeed) {
            order[n++] = seq[i].index;
        }
    }
    if (n != p->feedCount) {
        return;                     /* cannot happen; refuse to scramble */
    }

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
    gz_copy_n(p->country, sizeof p->country, "US", 2);

    /* Newest on top, everything shown, the sidebar out: what the window looks
       like the first time it opens. memset has already said so; these are
       here because a default worth knowing is worth writing down. */
    p->oldestFirst      = 0;
    p->hideReadArticles = 0;
    p->hideReadFeeds    = 0;
    p->hideSidebar      = 0;
    p->hideToolbar      = 0;
    p->showPhotos       = 1;

    /* No window remembered, no column widths: the window's own numbers. */
    p->windowLeft   = 0;
    p->windowTop    = 0;
    p->windowWidth  = 0;
    p->windowHeight = 0;
    p->sidebarWidth = 0;
    p->listWidth    = 0;

    GazettePrefsAddFeed(p, kStarterFeedURL, kStarterFeedTitle, -1);
}

/*
 * Up to n decimal numbers off one value, separated by spaces: "40 48 620 420"
 * is a rectangle a person can read, where four keys would be four lines to
 * keep in step. Returns how many were found; out beyond that is untouched.
 */
static int ParseNums(const char *value, long *out, int n)
{
    const char *s = value;
    int         got = 0;

    while (got < n) {
        const char *start;

        while (*s == ' ' || *s == '\t') {
            s++;
        }
        if (*s == '\0') {
            break;
        }
        start = s;
        while (*s != '\0' && *s != ' ' && *s != '\t') {
            s++;
        }
        out[got++] = gz_parse_dec(start, (size_t)(s - start), 0);
    }
    return got;
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
    g->after = TopCount(p);         /* a new group goes at the end */

    return p->groupCount++;
}

int GazettePrefsRemoveGroup(GazettePrefs *p, int index)
{
    int i;

    if (p == NULL || index < 0 || index >= p->groupCount) {
        return 0;
    }

    /*
     * The feeds are not the group's to take with it: removing a folder should
     * never silently unsubscribe anything, so they move to the top level —
     * in place, where the group stood, which the groups below have to make
     * room for. The array is in drawing order, so they are already in the
     * right place in it; only the groups' positions move.
     */
    {
        int members = GazettePrefsGroupFeedCount(p, index);
        int at      = p->groups[index].after;

        for (i = 0; i < p->groupCount; i++) {
            if (i != index && (p->groups[i].after > at ||
                               (p->groups[i].after == at && i > index))) {
                p->groups[i].after += members;
            }
        }
    }
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

int GazettePrefsMoveGroup(GazettePrefs *p, int group, GazettePlace place)
{
    GazetteGroupPref moved;
    int              newTo[kGazetteMaxGroups];
    int              after;
    int              at;            /* its index once the others close up */
    int              i;
    int              k;

    if (p == NULL || group < 0 || group >= p->groupCount) {
        return -1;
    }

    /* Where it goes: a position among the top-level feeds, and an index
       among the groups once it has been taken out. */
    switch (place.where) {
        case kGazettePlaceBeforeGroup:
        case kGazettePlaceAfterGroup:
            if (place.ref < 0 || place.ref >= p->groupCount ||
                place.ref == group) {
                return -1;
            }
            after = p->groups[place.ref].after;
            at    = place.ref - (place.ref > group ? 1 : 0);
            if (place.where == kGazettePlaceAfterGroup) {
                at++;
            }
            break;

        case kGazettePlaceAfterFeed:
        case kGazettePlaceBeforeFeed: {
            int t = TopOrdinal(p, place.ref);

            if (t < 0) {
                return -1;          /* only a top-level feed is a neighbour */
            }
            after = (place.where == kGazettePlaceAfterFeed) ? t + 1 : t;
            /* Directly after (or before) the feed: ahead of the groups that
               already stand at that position. */
            at = 0;
            for (i = 0; i < p->groupCount; i++) {
                if (i != group && p->groups[i].after < after) {
                    at++;
                }
            }
            break;
        }

        case kGazettePlaceListStart:
            after = 0;
            at    = 0;
            break;

        case kGazettePlaceListEnd:
            after = TopCount(p);
            at    = p->groupCount - 1;
            break;

        default:
            return -1;              /* a group does not go inside a group */
    }

    if (at < 0) {
        at = 0;
    }
    if (at > p->groupCount - 1) {
        at = p->groupCount - 1;
    }

    /* Take it out, close up, put it back at `at`, and tell the feeds. */
    moved = p->groups[group];
    for (i = group; i < p->groupCount - 1; i++) {
        p->groups[i] = p->groups[i + 1];
    }
    for (i = p->groupCount - 1; i > at; i--) {
        p->groups[i] = p->groups[i - 1];
    }
    p->groups[at]       = moved;
    p->groups[at].after = after;

    for (i = 0, k = 0; i < p->groupCount; i++) {
        if (i == group) {
            newTo[i] = at;
            continue;
        }
        if (k == at) {
            k++;
        }
        newTo[i] = k++;
    }
    for (i = 0; i < p->feedCount; i++) {
        if (p->feeds[i].group >= 0 && p->feeds[i].group < p->groupCount) {
            p->feeds[i].group = newTo[p->feeds[i].group];
        }
    }

    RebuildOrder(p);
    return at;
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
/* The sequence above, with the three standing views in front of it    */
/* and the hidden and the shut left out: a hidden feed or group takes  */
/* no row, and a shut group's feeds take none. Every question about a  */
/* row is answered by building the rows and looking — 128 feeds is a   */
/* walk nobody will ever measure.                                      */
/* ------------------------------------------------------------------ */

static int FeedShown(const GazettePrefs *p, int i)
{
    return !p->feeds[i].hidden;
}

static int GroupShown(const GazettePrefs *p, int g)
{
    return !p->groups[g].hidden;
}

static int BuildRows(const GazettePrefs *p, GazetteSidebarRow *rows)
{
    static GazetteSidebarRow seq[kGazetteMaxFeeds + kGazetteMaxGroups];
    int count;
    int n = 0;
    int i;
    int shownGroup = -1;            /* the open, shown group whose feeds follow */

    for (i = 0; i < kGazetteSmartCount; i++) {
        rows[n].kind  = kGazetteRowSmart;
        rows[n].index = i;
        n++;
    }

    count = GazettePrefsSequence(p, seq);
    for (i = 0; i < count; i++) {
        if (seq[i].kind == kGazetteRowGroup) {
            int g = seq[i].index;

            shownGroup = (GroupShown(p, g) && !p->groups[g].collapsed) ? g
                                                                        : -1;
            if (GroupShown(p, g)) {
                rows[n++] = seq[i];
            }
        } else {
            int f     = seq[i].index;
            int group = p->feeds[f].group;

            if (!FeedShown(p, f)) {
                continue;
            }
            if (group < 0 || group == shownGroup) {
                rows[n++] = seq[i];
            }
        }
    }
    return n;
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
    static GazetteSidebarRow rows[kGazetteSmartCount + kGazetteMaxFeeds +
                                  kGazetteMaxGroups];

    if (p == NULL) {
        return 0;
    }
    return BuildRows(p, rows);
}

int GazettePrefsRowAt(const GazettePrefs *p, int row, GazetteSidebarRow *out)
{
    static GazetteSidebarRow rows[kGazetteSmartCount + kGazetteMaxFeeds +
                                  kGazetteMaxGroups];
    int n;

    if (p == NULL || out == NULL || row < 0) {
        return 0;
    }
    n = BuildRows(p, rows);
    if (row >= n) {
        return 0;
    }
    *out = rows[row];
    return 1;
}

int GazettePrefsRowForGroup(const GazettePrefs *p, int group)
{
    static GazetteSidebarRow rows[kGazetteSmartCount + kGazetteMaxFeeds +
                                  kGazetteMaxGroups];
    int n;
    int i;

    if (p == NULL || group < 0 || group >= p->groupCount) {
        return -1;
    }
    n = BuildRows(p, rows);
    for (i = 0; i < n; i++) {
        if (rows[i].kind == kGazetteRowGroup && rows[i].index == group) {
            return i;
        }
    }
    return -1;
}

int GazettePrefsRowForFeed(const GazettePrefs *p, int feed)
{
    static GazetteSidebarRow rows[kGazetteSmartCount + kGazetteMaxFeeds +
                                  kGazetteMaxGroups];
    int n;
    int i;

    if (p == NULL || feed < 0 || feed >= p->feedCount) {
        return -1;
    }
    n = BuildRows(p, rows);
    for (i = 0; i < n; i++) {
        if (rows[i].kind == kGazetteRowFeed && rows[i].index == feed) {
            return i;
        }
    }
    return -1;                      /* drawn nowhere: hidden, or its group shut */
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

/* Take a feed out of the array, closing up behind it — and, for a top-level
   feed, moving up the groups that stood below it. */
static void RemoveFeedAt(GazettePrefs *p, int index)
{
    int t = TopOrdinal(p, index);
    int i;

    if (t >= 0) {
        for (i = 0; i < p->groupCount; i++) {
            if (p->groups[i].after > t) {
                p->groups[i].after--;
            }
        }
    }
    for (i = index; i < p->feedCount - 1; i++) {
        p->feeds[i] = p->feeds[i + 1];
    }
    memset(&p->feeds[p->feedCount - 1], 0, sizeof p->feeds[0]);
    p->feedCount--;
}

/* Put a feed into the array at an index, making room. */
static void InsertFeedAt(GazettePrefs *p, int index, const GazetteFeedPref *f)
{
    int i;

    if (index < 0) {
        index = 0;
    }
    if (index > p->feedCount) {
        index = p->feedCount;
    }
    for (i = p->feedCount; i > index; i--) {
        p->feeds[i] = p->feeds[i - 1];
    }
    p->feeds[index] = *f;
    p->feedCount++;
}

int GazettePrefsRemoveFeed(GazettePrefs *p, const char *url)
{
    int index = GazettePrefsFindFeed(p, url);

    if (index < 0) {
        return 0;
    }
    RemoveFeedAt(p, index);
    RebuildOrder(p);
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

int GazettePrefsSetFeedHome(GazettePrefs *p, int index, const char *home)
{
    if (p == NULL || index < 0 || index >= p->feedCount) {
        return 0;
    }
    if (home == NULL) {
        home = "";
    }
    if (strcmp(p->feeds[index].home, home) == 0) {
        return 0;
    }
    gz_copy_n(p->feeds[index].home, sizeof p->feeds[index].home,
              home, strlen(home));
    return 1;
}

int GazettePrefsMoveFeed(GazettePrefs *p, int feed, GazettePlace place)
{
    GazetteFeedPref moved;
    int             ref = place.ref;
    int             at;             /* the index it goes in at */
    int             i;

    if (p == NULL || feed < 0 || feed >= p->feedCount) {
        return -1;
    }
    if ((place.where == kGazettePlaceAfterFeed ||
         place.where == kGazettePlaceBeforeFeed) &&
        (ref < 0 || ref >= p->feedCount || ref == feed)) {
        return -1;
    }
    if ((place.where == kGazettePlaceGroupStart ||
         place.where == kGazettePlaceGroupEnd ||
         place.where == kGazettePlaceBeforeGroup ||
         place.where == kGazettePlaceAfterGroup) &&
        (ref < 0 || ref >= p->groupCount)) {
        return -1;
    }

    /* Out first, so that everything below is measured in a list it is not
       in — and a feed it was measured against may have moved up one. */
    moved = p->feeds[feed];
    RemoveFeedAt(p, feed);
    if ((place.where == kGazettePlaceAfterFeed ||
         place.where == kGazettePlaceBeforeFeed) && ref > feed) {
        ref--;
    }

    switch (place.where) {
        case kGazettePlaceAfterFeed:
        case kGazettePlaceBeforeFeed: {
            int t = TopOrdinal(p, ref);

            moved.group = p->feeds[ref].group;
            at = (place.where == kGazettePlaceAfterFeed) ? ref + 1 : ref;
            if (t >= 0) {
                /* Beside a top-level feed: the groups standing below the
                   pair move down one. Before it, the new feed takes its
                   ordinal and the groups that stood before it still do. */
                int keep = (place.where == kGazettePlaceAfterFeed) ? t : t - 1;

                for (i = 0; i < p->groupCount; i++) {
                    if (p->groups[i].after > keep) {
                        p->groups[i].after++;
                    }
                }
            }
            break;
        }

        case kGazettePlaceGroupStart:
        case kGazettePlaceGroupEnd: {
            int first = GazettePrefsFirstFeedInGroup(p, ref);
            int last  = -1;

            for (i = 0; i < p->feedCount; i++) {
                if (p->feeds[i].group == ref) {
                    last = i;
                }
            }
            moved.group = ref;
            if (first < 0) {
                at = p->feedCount;  /* an empty group: RebuildOrder places it */
            } else {
                at = (place.where == kGazettePlaceGroupStart) ? first
                                                              : last + 1;
            }
            break;
        }

        case kGazettePlaceBeforeGroup:
        case kGazettePlaceAfterGroup: {
            /* At the top level, with the ordinal the group stands at: the
               group itself, and those beside it in order, move down one
               when the feed lands above them. */
            int t = p->groups[ref].after;

            moved.group = -1;
            for (i = 0; i < p->groupCount; i++) {
                if (p->groups[i].after > t ||
                    (p->groups[i].after == t &&
                     (place.where == kGazettePlaceBeforeGroup ? i >= ref
                                                              : i > ref))) {
                    p->groups[i].after++;
                }
            }
            at = TopFeedAt(p, t);
            if (at < 0) {
                at = p->feedCount;
            }
            break;
        }

        case kGazettePlaceListStart:
            moved.group = -1;
            for (i = 0; i < p->groupCount; i++) {
                p->groups[i].after++;
            }
            at = 0;
            break;

        case kGazettePlaceListEnd:
            moved.group = -1;
            at = p->feedCount;
            break;

        default:
            /* Nowhere it can go: back where it was, group and all. */
            InsertFeedAt(p, feed, &moved);
            RebuildOrder(p);
            return -1;
    }

    InsertFeedAt(p, at, &moved);
    RebuildOrder(p);
    return GazettePrefsFindFeed(p, moved.url);
}

/* ------------------------------------------------------------------ */
/* Parsing                                                             */
/* ------------------------------------------------------------------ */

/*
 * Split "<url> | <title> | <home>" into its trimmed parts. With no pipe the
 * whole value is the URL and the title comes out empty, which AddFeed then
 * fills in with the URL. The home page is the part after the *last* pipe,
 * and only when it looks like an address: a title with a pipe in it is
 * rarer than a feed with no home page, but it is not impossible, and a file
 * written before there was a third field has none.
 */
static void SplitFeedValue(const char *value,
                           char *url, size_t urlCap,
                           char *title, size_t titleCap,
                           char *home, size_t homeCap)
{
    const char *pipe = strchr(value, '|');
    const char *last;
    const char *part;
    size_t      partLen;
    size_t      restLen;

    if (homeCap > 0) {
        home[0] = '\0';
    }
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

    restLen = strlen(pipe + 1);
    last    = strrchr(pipe + 1, '|');
    if (last != NULL && strstr(last + 1, "://") != NULL) {
        part = gz_trim(last + 1, strlen(last + 1), &partLen);
        gz_copy_n(home, homeCap, part, partLen);
        restLen = (size_t)(last - (pipe + 1));
    }

    part = gz_trim(pipe + 1, restLen, &partLen);
    gz_copy_n(title, titleCap, part, partLen);
}

int GazettePrefsParse(const char *text, size_t len, GazettePrefs *p)
{
    char   key[64];
    char   value[kGazetteURLLen + kGazetteTitleLen + kGazetteHomeLen + 8];
    char   url[kGazetteURLLen];
    char   title[kGazetteTitleLen];
    char   home[kGazetteHomeLen];
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
    (void)gz_prefs_get(text, len, "country", p->country, sizeof p->country);

    p->oldestFirst      = gz_prefs_get_num(text, len, "sort-oldest-first",
                                           p->oldestFirst) ? 1 : 0;
    p->hideReadArticles = gz_prefs_get_num(text, len, "hide-read-articles",
                                           p->hideReadArticles) ? 1 : 0;
    p->hideReadFeeds    = gz_prefs_get_num(text, len, "hide-read-feeds",
                                           p->hideReadFeeds) ? 1 : 0;
    p->hideSidebar      = gz_prefs_get_num(text, len, "hide-sidebar",
                                           p->hideSidebar) ? 1 : 0;
    p->hideToolbar      = gz_prefs_get_num(text, len, "hide-toolbar",
                                           p->hideToolbar) ? 1 : 0;
    p->showPhotos       = gz_prefs_get_num(text, len, "show-photos",
                                           p->showPhotos) ? 1 : 0;

    /* The window's rectangle is all four numbers or nothing: three of them
       would place a window nobody described. */
    {
        char value[64];
        long nums[4];

        if (gz_prefs_get(text, len, "window", value, sizeof value) &&
            ParseNums(value, nums, 4) == 4 && nums[2] > 0 && nums[3] > 0) {
            p->windowLeft   = nums[0];
            p->windowTop    = nums[1];
            p->windowWidth  = nums[2];
            p->windowHeight = nums[3];
        }
        if (gz_prefs_get(text, len, "columns", value, sizeof value) &&
            ParseNums(value, nums, 2) == 2) {
            p->sidebarWidth = nums[0] > 0 ? nums[0] : 0;
            p->listWidth    = nums[1] > 0 ? nums[1] : 0;
        }
    }

    if (p->refreshMinutes < 0) {
        p->refreshMinutes = 0;
    }
    if (p->maxArticles < 0) {
        p->maxArticles = 0;         /* 0: as many as the feed offers */
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
            /* A group stands where its line does: after however many
               top-level feeds the file has named so far, which AddGroup
               counts. */
            group = GazettePrefsAddGroup(p, value);
            if (group >= 0 && gz_stricmp(key, "group-closed") == 0) {
                p->groups[group].collapsed = 1;
            }
            continue;
        }

        /* Back to the top level: the feeds after this are nobody's. A file
           from before there was such a line has none, and reads as it
           always did. */
        if (gz_stricmp(key, "group-end") == 0) {
            group = -1;
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
            /* And the groups seen so far stood after the starter, which
               is gone: they stand at the top now. */
            for (i = 0; i < p->groupCount; i++) {
                p->groups[i].after = 0;
            }
            sawAnyFeed = 1;
        }

        SplitFeedValue(value, url, sizeof url, title, sizeof title,
                       home, sizeof home);
        {
            int index = GazettePrefsAddFeed(p, url, title, group);

            if (index >= 0) {
                p->feeds[index].enabled = enabled;
                gz_copy_n(p->feeds[index].home, sizeof p->feeds[index].home,
                          home, strlen(home));
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
    if (f->title[0] != '\0' || f->home[0] != '\0') {
        Append(out, cap, len, " | ");
        Append(out, cap, len, f->title);
    }
    if (f->home[0] != '\0') {
        Append(out, cap, len, " | ");
        Append(out, cap, len, f->home);
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
    Append(out, cap, &len, "\r");

    Append(out, cap, &len, "hide-toolbar       = ");
    AppendNum(out, cap, &len, p->hideToolbar ? 1 : 0);
    Append(out, cap, &len, "\r");

    Append(out, cap, &len, "show-photos        = ");
    AppendNum(out, cap, &len, p->showPhotos ? 1 : 0);
    Append(out, cap, &len, "\r\r");

    /* Where the window was. Written only once there is one to write: a file
       saved before the window ever moved says nothing about it, and the
       window opens where it always has. */
    {
        int haveWindow  = (p->windowWidth > 0 && p->windowHeight > 0);
        int haveColumns = (p->sidebarWidth > 0 && p->listWidth > 0);

        if (haveWindow || haveColumns) {
            Append(out, cap, &len,
                   "# window = <left> <top> <width> <height>   "
                   "columns = <sidebar> <headlines>\r");
        }
        if (haveWindow) {
            Append(out, cap, &len, "window  = ");
            AppendNum(out, cap, &len, p->windowLeft);
            Append(out, cap, &len, " ");
            AppendNum(out, cap, &len, p->windowTop);
            Append(out, cap, &len, " ");
            AppendNum(out, cap, &len, p->windowWidth);
            Append(out, cap, &len, " ");
            AppendNum(out, cap, &len, p->windowHeight);
            Append(out, cap, &len, "\r");
        }
        if (haveColumns) {
            Append(out, cap, &len, "columns = ");
            AppendNum(out, cap, &len, p->sidebarWidth);
            Append(out, cap, &len, " ");
            AppendNum(out, cap, &len, p->listWidth);
            Append(out, cap, &len, "\r");
        }
        if (haveWindow || haveColumns) {
            Append(out, cap, &len, "\r");
        }
    }

    Append(out, cap, &len,
           "# feed = <url> | <title> | <home page>   "
           "(feed-off = the same, disabled)\r");
    Append(out, cap, &len,
           "# Feeds after a group line belong to it, up to a group-end line; "
           "feeds before any belong to none.\r\r");

    /* In the sidebar's own order, top to bottom. A top-level feed that
       follows a group's feeds is announced by a group-end line, without
       which it would read as the group's. */
    {
        static GazetteSidebarRow seq[kGazetteMaxFeeds + kGazetteMaxGroups];
        int count   = GazettePrefsSequence(p, seq);
        int inGroup = 0;

        for (i = 0; i < count; i++) {
            if (seq[i].kind == kGazetteRowGroup) {
                g = seq[i].index;
                Append(out, cap, &len, "\r");
                Append(out, cap, &len,
                       p->groups[g].collapsed ? "group-closed = "
                                              : "group        = ");
                Append(out, cap, &len, p->groups[g].name);
                Append(out, cap, &len, "\r");
                inGroup = 1;
            } else if (p->feeds[seq[i].index].group < 0) {
                if (inGroup) {
                    Append(out, cap, &len, "group-end    = 1\r\r");
                    inGroup = 0;
                }
                AppendFeed(out, cap, &len, &p->feeds[seq[i].index]);
            } else {
                AppendFeed(out, cap, &len, &p->feeds[seq[i].index]);
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
