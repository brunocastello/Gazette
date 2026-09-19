/*
 * Gazette — the article store and the refresh state machine
 * Copyright (c) 2026 brunocastello
 *
 * See gazette_feeds.h.
 */

#include "feeds/gazette_feeds.h"

#include "extract/gazette_extract.h"
#include "core/gazette_core.h"
#include "feeds/gazette_googlenews.h"
#include "feeds/gazette_index.h"
#include "feeds/gazette_photos.h"
#include "net/gazette_fetch.h"
#include "portable/gazette_portable.h"
#include "store/gazette_store.h"

#include <DateTimeUtils.h>      /* GetDateTime */
#include <MacMemory.h>          /* NewPtrClear, DisposePtr */
#include <OSUtils.h>            /* MachineLocation */
#include <Script.h>             /* ReadLocation */

#include <stdio.h>
#include <string.h>

/*
 * The store and the parser are both large and both singletons, and neither is
 * wanted on the stack. The store is file-scope because it outlives every
 * refresh; the parser is allocated for the duration of one, since between
 * refreshes it is 5 KB doing nothing.
 */
static GazetteArticle     gArticles[kGazetteMaxArticles];
static int                gArticleCount;

/*
 * The view. gViewMap holds the store indices the window is showing, in the
 * order it shows them, and every index crossing this file's boundary is an
 * index into it — so the window draws the view, clicks arrive as the view,
 * and nothing outside has to know what is behind it.
 *
 * Three things shape it: the search text, "Hide Read Articles", and which way
 * up the sort is. It used to be the search alone and the map was built only
 * when a search was on, which meant two index regimes to keep straight; one
 * map, always built, is a walk of at most a hundred and fifty entries and it
 * costs nothing that anybody can measure.
 */
static char               gFilter[64];
static short              gViewMap[kGazetteMaxArticles];
static int                gViewCount;
static int                gHideRead;      /* Hide Read Articles */
static int                gOldestFirst;   /* Sort Articles By: Oldest on Top */
static char               gFeedTitle[kGazetteFeedTitleLen];
static int                gCurrentFeed = -1;
static int                gPendingFeed = -1;
static char               gRefreshHome[kGazetteHomeLen];  /* the site, per the last refresh */
static int                gCurrentGroup = -1;
static int                gCurrentSmart = -1;

static GazetteFeedParser *gParser;

/*
 * A second scanner, in discovery mode, fed the same bytes as the first. That
 * is cheaper than fetching the page twice, and the two answers arrive
 * together: either the document parsed as a feed, or it did not and this says
 * where the feed actually is.
 */
static GazetteFeedParser *gDiscover;
static char               gDiscovered[kGazetteArticleLinkLen];
static GazetteFetch      *gFetch;
static GazetteRefreshState gState = kGazetteRefreshIdle;
static long               gMaxArticles;
static int                gCleared;       /* store emptied for this refresh */
static int                gReceived;      /* articles this refresh has given */
static char               gError[192];
static long               gFetchedAt;


/* The URL the running refresh is for, kept so the cache can be written under
   the same name it will later be read under. */
static char               gCurrentURL[1024];

/*
 * The full-text job, kept entirely separate from the refresh above. They
 * share the one connection by refusing to overlap rather than by sharing any
 * state, which is the difference between two small state machines and one
 * that has to remember what it is in the middle of.
 *
 * gFullText is 8 KB that stays allocated; the extractor is 16 KB that does
 * not, and is taken for the length of one fetch the way the parser is.
 */
static GazetteExtract     *gExtract;
static GazetteFetch       *gFullFetch;
static GazetteRefreshState gFullState = kGazetteRefreshIdle;
static int                 gFullArticle = -1;      /* what gFullText is for */
static int                 gPendingFullArticle = -1;

/*
 * The page that still has to be got, and where from. Set the moment one is
 * asked for and cleared only when it arrives or is given up on — so it stands
 * through a fetch running, through a request held back because the refresh
 * had the connection, and through a refresh taking the connection away from a
 * fetch that had already started.
 *
 * That is what makes one question answer all three: GazetteFeedsFullTextComing
 * is this and nothing else. Reading the full article is not optional, so
 * "busy" never means "settle for the summary" — it means "in a moment", and
 * GazetteFeedsFullTextResume is what comes back for it.
 */
static int                 gWantArticle = -1;
static char                gWantURL[kGazetteArticleLinkLen];
static char                gFullText[kGazetteExtractMax];
static char                gFullError[192];

/*
 * A Google News item links to Google, not to the story, and the story's
 * address has to be asked for first — see the article-link section of
 * gazette_googlenews.h. That is two fetches before the article's own, and
 * this is what they need: the token, the scan of the first page, the body
 * of the second request and its answer. Taken for the length of the
 * resolution and let go before the article is fetched.
 */
typedef struct {
    char             token[kGazetteGNewsTokenMax];
    GazetteGNewsScan scan;
    char             body[kGazetteGNewsBodyMax];
    char             answer[4096];
    size_t           answerLen;
    char             url[kGazetteArticleLinkLen];
} GNewsResolve;

enum {
    kFullStageArticle = 0,      /* the story's own page, into the extractor */
    kFullStagePage,             /* Google's page, for the two attributes */
    kFullStageAnswer            /* Google's decoder, for the address */
};

static GNewsResolve       *gResolve;
static int                 gFullStage = kFullStageArticle;

/* The pictures the page named, and the page's own address once the
   redirects were followed — what a relative picture resolves against.
   Copied out of the extractor at the finish, since the extractor goes. */
static GazettePhotoRef     gFullPhotos[kGazetteMaxPhotos];
static int                 gFullPhotoCount;
static char                gFullFinalURL[kGazetteArticleLinkLen];

/*
 * The pages read lately, so that going back to an article does not fetch
 * it again. Keyed by the article's link as the feed gave it — the one the
 * reader pane asks by — which is not the address the page came from when
 * Google News stood between. A ring of a few: each is the text, the
 * pictures' addresses and the page's own, about 20 KB, taken one at a time
 * as pages are read and never all at once, so a partition with no room
 * for another simply reads that page again next time. A refresh empties
 * it: what was read then is what the user asked to read again.
 */
typedef struct {
    char            key[kGazetteArticleLinkLen];
    char            text[kGazetteExtractMax];
    GazettePhotoRef photos[kGazetteMaxPhotos];
    int             photoCount;
    char            finalURL[kGazetteArticleLinkLen];
} ReadPage;

enum { kReadPages = 6 };

static ReadPage *gReadPages[kReadPages];
static int       gReadNext;                 /* the slot the next page takes */
static char      gWantKey[kGazetteArticleLinkLen];  /* the link being read */

static ReadPage *FindReadPage(const char *key)
{
    int i;

    for (i = 0; i < kReadPages; i++) {
        if (gReadPages[i] != NULL && strcmp(gReadPages[i]->key, key) == 0) {
            return gReadPages[i];
        }
    }
    return NULL;
}

static void ForgetReadPages(void)
{
    int i;

    for (i = 0; i < kReadPages; i++) {
        if (gReadPages[i] != NULL) {
            gReadPages[i]->key[0] = '\0';
        }
    }
}

/* Keep what has just been read, under the link it was asked by. */
static void RememberReadPage(void)
{
    ReadPage *page;
    int       i;

    if (gWantKey[0] == '\0') {
        return;
    }
    page = FindReadPage(gWantKey);
    if (page == NULL) {
        if (gReadPages[gReadNext] == NULL) {
            gReadPages[gReadNext] =
                (ReadPage *)NewPtrClear((Size)sizeof(ReadPage));
            if (gReadPages[gReadNext] == NULL) {
                return;             /* no room: read it again next time */
            }
        }
        page      = gReadPages[gReadNext];
        gReadNext = (gReadNext + 1) % kReadPages;
    }
    gz_copy_n(page->key, sizeof page->key, gWantKey, strlen(gWantKey));
    gz_copy_n(page->text, sizeof page->text, gFullText, strlen(gFullText));
    page->photoCount = gFullPhotoCount;
    for (i = 0; i < gFullPhotoCount; i++) {
        page->photos[i] = gFullPhotos[i];
    }
    gz_copy_n(page->finalURL, sizeof page->finalURL, gFullFinalURL,
              strlen(gFullFinalURL));
}

/* Put a remembered page where a fetched one would go. */
static void RecallReadPage(const ReadPage *page, int articleIndex)
{
    int i;

    GazetteFeedsFullTextCancel();
    gz_copy_n(gFullText, sizeof gFullText, page->text, strlen(page->text));
    gFullPhotoCount = page->photoCount;
    for (i = 0; i < gFullPhotoCount; i++) {
        gFullPhotos[i] = page->photos[i];
    }
    gz_copy_n(gFullFinalURL, sizeof gFullFinalURL, page->finalURL,
              strlen(page->finalURL));
    gFullArticle = articleIndex;
    gWantArticle = -1;              /* settled: it is here */
    gFullState   = kGazetteRefreshDone;
}

/*
 * Seconds between the Macintosh epoch (1904) and the Unix one (1970).
 * GetDateTime counts from the former; every date in the store counts from the
 * latter, because that is what feeds date their articles in.
 */
enum { kMacToUnixEpoch = 2082844800L };

/*
 * The first line of a cache file. The number is not only the field layout: a
 * cache holds text that has already been through the whole decode / strip /
 * transliterate pipeline, so it is also the version of that pipeline. Change
 * what the pipeline produces and old files hold the old text -- they parse
 * perfectly and are simply wrong, which is worse than being unreadable,
 * because nothing announces it.
 *
 * 2: entities are decoded on both sides of the markup strip. Version 1 files
 * carry the "&nbsp;" that fix removed, and refusing them is what makes the
 * fix reach a feed the user has already read without them having to know to
 * refresh it by hand.
 *
 * 3: a body keeps its paragraphs. That changes the file's shape as well as
 * its text -- a body is written as one 'B' line per paragraph now, because a
 * line-oriented format cannot hold a newline inside a field.
 *
 * 4: an article carried whether it had been read.
 *
 * 5: and no longer does. Read state moved to "Gazette Index", because it is a
 * property of an article rather than of the file an article happens to be
 * cached in -- an article opened in a group view belongs to a feed whose
 * cache is not the one being written. See gazette_index.h.
 *
 * 6: the pipeline changed twice on 2026-09-19 and both changes are in the
 * file. A body is the feed's rich text now, where it offers one, without the
 * link-only trailer it ends with; and a linked-list entry's link is the post
 * on the feed's site, not the page it is about -- which is the key the index
 * keeps read state under, so an old file would show the old links beside
 * the new ones.
 */
static const char kCacheMagic[] = "GAZETTE-CACHE 6";

static void SaveCache(const char *url, long fetchedAt);

/* The full-text job's three halves: begin the fetch for whatever is in the
   held request, and give the connection up without giving the request up. */
static int  BeginFullText(void);
static void PauseFullText(void);

static long UnixNow(void)
{
    unsigned long macNow = 0;

    GetDateTime(&macNow);
    return (long)macNow - kMacToUnixEpoch;
}

/*
 * Seconds east of GMT, as the Date & Time control panel has it. A feed
 * timestamps its articles in UTC and the Macintosh clock keeps local time, so
 * this is what stands between the two — and it lives here, beside UnixNow,
 * because this is the file that already owns Gazette's idea of the clock.
 *
 * Which way it goes matters and is easy to get backwards: UnixNow is already
 * local, so it is an article's date that this is added to, never the current
 * time. Adding it to both counts it twice.
 *
 * gmtDelta shares a long with the daylight saving flag and is only three
 * bytes wide, which is why it is masked and sign-extended by hand rather than
 * read straight out. A machine that has never been told where it is answers
 * zero, which is the right answer for a machine keeping UTC.
 */
long GazetteFeedsGMTDelta(void)
{
    MachineLocation loc;
    long            delta;

    ReadLocation(&loc);
    delta = loc.u.gmtDelta & 0x00FFFFFFL;
    if (delta >= 0x00800000L) {
        delta -= 0x01000000L;       /* the three-byte field's sign */
    }
    return delta;
}

/* An article's timestamp read on the reader's own clock. Zero means "no date"
   everywhere else in the application, so it stays zero here. */
long GazetteFeedsLocalTime(long seconds)
{
    return (seconds == 0) ? 0 : seconds + GazetteFeedsGMTDelta();
}

/* ------------------------------------------------------------------ */
/* The store                                                           */
/* ------------------------------------------------------------------ */

/* A store index from a public one. Always through the map now, so there is
   one answer rather than one per combination of what is switched on. */
static int StoreIndex(int index)
{
    if (index < 0 || index >= gViewCount) {
        return -1;
    }
    return gViewMap[index];
}

static int Matches(const GazetteArticle *a)
{
    return gz_contains_ci(a->title, strlen(a->title), gFilter) ||
           gz_contains_ci(a->source, strlen(a->source), gFilter) ||
           gz_contains_ci(a->body, strlen(a->body), gFilter);
}

/*
 * Build the view. The store is held newest first — that is the order every
 * feed worth reading sends its items in, and the order the cache keeps — so
 * "Oldest on Top" is the same walk backwards rather than a sort.
 *
 * An article the reader has open is not dropped by "Hide Read Articles"
 * simply because opening it marked it read: this runs when the view changes,
 * not when a read flag does, so the article stays on screen until the reader
 * moves off it. That is deliberate, and it is why marking read does not call
 * this.
 */
static void Refilter(void)
{
    int n;

    gViewCount = 0;
    for (n = 0; n < gArticleCount; n++) {
        int i = gOldestFirst ? (gArticleCount - 1 - n) : n;
        const GazetteArticle *a = &gArticles[i];

        if (gFilter[0] != '\0' && !Matches(a)) {
            continue;
        }
        if (gHideRead && a->read && !a->starred) {
            continue;       /* a starred article is kept whatever its state */
        }
        gViewMap[gViewCount++] = (short)i;
    }
}

void GazetteFeedsRebuildView(void)
{
    Refilter();
}

void GazetteFeedsSetHideRead(int hide)
{
    gHideRead = hide ? 1 : 0;
    Refilter();
    GazetteFeedsFullTextCancel();
}

int GazetteFeedsHideRead(void)
{
    return gHideRead;
}

void GazetteFeedsSetOldestFirst(int oldest)
{
    gOldestFirst = oldest ? 1 : 0;
    Refilter();
    GazetteFeedsFullTextCancel();
}

int GazetteFeedsOldestFirst(void)
{
    return gOldestFirst;
}

void GazetteFeedsSetFilter(const char *text)
{
    gz_copy_n(gFilter, sizeof gFilter, (text != NULL) ? text : "",
              (text != NULL) ? strlen(text) : 0);
    Refilter();

    /* The held full text is remembered by article index, and the indices have
       just been renumbered under it. */
    GazetteFeedsFullTextCancel();
}

const char *GazetteFeedsFilter(void)
{
    return gFilter;
}

int GazetteFeedsTotalCount(void)
{
    return gArticleCount;
}

int GazetteFeedsArticleCount(void)
{
    return gViewCount;
}

const GazetteArticle *GazetteFeedsArticleAt(int index)
{
    int at = StoreIndex(index);

    return (at >= 0) ? &gArticles[at] : NULL;
}

const char *GazetteFeedsTitle(void)
{
    return gFeedTitle;
}

int GazetteFeedsRefreshFeedIndex(void)
{
    return gPendingFeed;
}

const char *GazetteFeedsRefreshHome(void)
{
    return gRefreshHome;
}

int GazetteFeedsCurrentFeed(void)
{
    return gCurrentFeed;
}

int GazetteFeedsCurrentGroup(void)
{
    return gCurrentGroup;
}

void GazetteFeedsClear(void)
{
    gArticleCount = 0;
    gViewCount    = 0;
    gFeedTitle[0] = '\0';
    gCurrentFeed  = -1;
    gCurrentGroup = -1;
    gCurrentSmart = -1;
    gFetchedAt    = 0;

    /* The held text is indexed by a position in the store that is about to
       mean a different article, or none. */
    GazetteFeedsFullTextCancel();
}

long GazetteFeedsFetchedAt(void)
{
    return gFetchedAt;
}

/* ------------------------------------------------------------------ */
/* Read and unread                                                     */
/* ------------------------------------------------------------------ */

/* Over everything held, not over the matches: how much of a feed is unread is
   a fact about the feed and not about what is being searched for. */
int GazetteFeedsUnreadCount(void)
{
    int n = 0;
    int i;

    for (i = 0; i < gArticleCount; i++) {
        if (!gArticles[i].read) {
            n++;
        }
    }
    return n;
}

/* Tell the index how much of this feed is left to read, so the sidebar can
   say so without the articles being in hand. */
static void PublishCounts(void)
{
    if (gCurrentURL[0] != '\0') {
        GazetteIndexSetFeedCounts(gCurrentURL, gArticleCount,
                                  GazetteFeedsUnreadCount());
    }
}

void GazetteFeedsMarkRead(int index, int read)
{
    int at = StoreIndex(index);

    if (at < 0) {
        return;
    }
    if (gArticles[at].read == (read ? 1 : 0)) {
        return;
    }
    gArticles[at].read = read ? 1 : 0;
    GazetteIndexSetRead(gArticles[at].link, read);
    PublishCounts();
}

void GazetteFeedsMarkAllRead(void)
{
    int i;

    for (i = 0; i < gArticleCount; i++) {
        if (!gArticles[i].read) {
            gArticles[i].read = 1;
            GazetteIndexSetRead(gArticles[i].link, 1);
        }
    }
    PublishCounts();
}

/*
 * The other way. Not an undo — it does not remember which were read before —
 * but the same command turned round, which is what a menu item that says
 * "Mark All as Unread" when there is nothing left to read has to do.
 */
void GazetteFeedsMarkAllUnread(void)
{
    int i;

    for (i = 0; i < gArticleCount; i++) {
        if (gArticles[i].read) {
            gArticles[i].read = 0;
            GazetteIndexSetRead(gArticles[i].link, 0);
        }
    }
    PublishCounts();
}

/*
 * Everything above the article in the view, or everything below it, marked
 * read in one go. "Above" and "below" are the list's, not the store's: with
 * the sort turned over they mean the opposite ends of the store, and what the
 * reader means by them is what they can see.
 */
void GazetteFeedsMarkRange(int index, int below)
{
    int i;

    if (index < 0 || index >= gViewCount) {
        return;
    }
    for (i = below ? (index + 1) : 0;
         below ? (i < gViewCount) : (i < index);
         i++) {
        int at = gViewMap[i];

        if (!gArticles[at].read) {
            gArticles[at].read = 1;
            GazetteIndexSetRead(gArticles[at].link, 1);
        }
    }
    PublishCounts();
}

/* ------------------------------------------------------------------ */
/* Starred                                                             */
/* ------------------------------------------------------------------ */

void GazetteFeedsMarkStarred(int index, int starred)
{
    int at = StoreIndex(index);

    if (at < 0 || gArticles[at].starred == (starred ? 1 : 0)) {
        return;
    }
    gArticles[at].starred = starred ? 1 : 0;
    GazetteIndexSetStarred(gArticles[at].link, starred);
}

/* ------------------------------------------------------------------ */
/* Parsing                                                             */
/* ------------------------------------------------------------------ */

/*
 * One article, straight from the parser. Returns 0 once the store is full,
 * which stops the parser, which stops the fetch — so a feed with two thousand
 * items costs one buffer's worth of parsing rather than all of it.
 */
static int ArticleSink(const GazetteArticle *article, void *context)
{
    (void)context;

    /* Held until the first article arrives: a refresh that fails at the
       handshake should leave the previous feed on screen, not an empty
       window. */
    if (!gCleared) {
        GazetteFeedsClear();
        gCurrentFeed = gPendingFeed;
        gCleared     = 1;
        /* The title goes with the articles it named. If this feed's parse
           yields none, the cache is written with an empty one and the
           header falls back to the sidebar's name — not the last feed's. */
        gFeedTitle[0] = '\0';
    }
    gReceived++;

    if (gArticleCount >= kGazetteMaxArticles ||
        (gMaxArticles > 0 && gArticleCount >= gMaxArticles)) {
        return 0;
    }

    /* A feed that carries the same item twice — an update re-listed, a
       template's slip — is one article, not two. */
    if (article->link[0] != '\0') {
        int i;

        for (i = 0; i < gArticleCount; i++) {
            if (strcmp(gArticles[i].link, article->link) == 0) {
                return 1;
            }
        }
    }

    gArticles[gArticleCount] = *article;
    gArticles[gArticleCount].feed = gPendingFeed;
    /* The index knows, and knows across a refresh: an article that was read
       before this fetch replaced the store is still read. */
    gArticles[gArticleCount].read    = GazetteIndexIsRead(article->link);
    gArticles[gArticleCount].starred = GazetteIndexIsStarred(article->link);
    gArticleCount++;
    return 1;
}

/* The fetch's body sink: everything read goes straight into the parser, and
   into the discovery scanner alongside it when one is running. */
static int BodySink(const char *data, size_t len, void *context)
{
    (void)context;

    if (gParser == NULL) {
        return 0;
    }
    if (gDiscover != NULL) {
        (void)GazetteFeedParserFeed(gDiscover, data, len);
    }
    return GazetteFeedParserFeed(gParser, data, len);
}

/* ------------------------------------------------------------------ */
/* Refreshing                                                          */
/* ------------------------------------------------------------------ */

static void ReleaseRefresh(void)
{
    if (gFetch != NULL) {
        GazetteFetchDestroy(gFetch);
        gFetch = NULL;
    }
    if (gParser != NULL) {
        DisposePtr((Ptr)gParser);
        gParser = NULL;
    }
    if (gDiscover != NULL) {
        DisposePtr((Ptr)gDiscover);
        gDiscover = NULL;
    }
}

const char *GazetteFeedsDiscoveredURL(void)
{
    return gDiscovered;
}

int GazetteFeedsRefreshStart(int feedIndex, const char *url, long maxArticles,
                             int allowDiscovery)
{
    if (gState == kGazetteRefreshRunning) {
        return 0;
    }

    /* There is one connection and headlines outrank an article's page — but
       the page is postponed, not abandoned. See PauseFullText. The pictures
       are postponed the same way. */
    PauseFullText();
    GazettePhotosPause();
    ForgetReadPages();

    ReleaseRefresh();
    gError[0] = '\0';
    gCleared  = 0;
    gReceived = 0;
    gMaxArticles = maxArticles;
    gPendingFeed = feedIndex;
    gRefreshHome[0] = '\0';
    gz_copy_n(gCurrentURL, sizeof gCurrentURL, url ? url : "",
              url ? strlen(url) : 0);

    gParser = (GazetteFeedParser *)NewPtrClear((Size)sizeof(GazetteFeedParser));
    if (gParser == NULL) {
        snprintf(gError, sizeof gError, "Not enough memory to read the feed.");
        gState = kGazetteRefreshFailed;
        return 0;
    }
    GazetteFeedParserInit(gParser, ArticleSink, NULL);

    gDiscovered[0] = '\0';
    if (allowDiscovery) {
        /* Failing to allocate this is not a reason to fail the refresh: the
           feed may well parse, and then discovery was never needed. */
        gDiscover = (GazetteFeedParser *)
                        NewPtrClear((Size)sizeof(GazetteFeedParser));
        if (gDiscover != NULL) {
            GazetteFeedParserInitDiscovery(gDiscover);
        }
    }

    gFetch = GazetteFetchStart(url, BodySink, NULL);
    if (gFetch == NULL) {
        snprintf(gError, sizeof gError, "Could not start fetching %s", url);
        ReleaseRefresh();
        gState = kGazetteRefreshFailed;
        return 0;
    }

    gState = kGazetteRefreshRunning;
    return 1;
}

GazetteRefreshState GazetteFeedsRefreshPump(void)
{
    GazetteFetchState fetchState;

    if (gState != kGazetteRefreshRunning || gFetch == NULL) {
        return gState;
    }

    fetchState = GazetteFetchPump(gFetch);

    if (fetchState == kGazetteFetchFailed) {
        snprintf(gError, sizeof gError, "%s", GazetteFetchErrorText(gFetch));
        ReleaseRefresh();
        gState = kGazetteRefreshFailed;
        return gState;
    }

    if (fetchState != kGazetteFetchDone) {
        return gState;
    }

    /* The body is in. Flush whatever the parser was holding, then judge the
       result by what came out of it rather than by the HTTP status: a 200
       carrying an HTML error page is a failed refresh. */
    GazetteFeedParserFinish(gParser);

    if (gDiscover != NULL) {
        GazetteFeedParserFinish(gDiscover);
        gz_copy_n(gDiscovered, sizeof gDiscovered,
                  GazetteFeedParserDiscovered(gDiscover),
                  strlen(GazetteFeedParserDiscovered(gDiscover)));
    }

    if (GazetteFeedParserTitle(gParser)[0] != '\0') {
        gz_copy_n(gFeedTitle, sizeof gFeedTitle,
                  GazetteFeedParserTitle(gParser),
                  strlen(GazetteFeedParserTitle(gParser)));
    }
    gz_copy_n(gRefreshHome, sizeof gRefreshHome,
              GazetteFeedParserLink(gParser),
              strlen(GazetteFeedParserLink(gParser)));

    /*
     * Judged by what *this* fetch gave, not by what the store holds: the
     * store is not cleared until the first new article arrives, so after a
     * parse that produced nothing it is still full of the previous feed's
     * articles — and gArticleCount == 0 would have said all was well and
     * written them into this feed's cache under this feed's address.
     */
    if (gReceived == 0) {
        int status = GazetteFetchStatus(gFetch);

        if (status != 200) {
            snprintf(gError, sizeof gError,
                     "The server answered %d.", status);
        } else {
            snprintf(gError, sizeof gError,
                     "Nothing that looks like a feed came back.");
        }
        ReleaseRefresh();
        gState = kGazetteRefreshFailed;
        return gState;
    }

    ReleaseRefresh();

    /*
     * Write the cache only on a refresh that produced articles, and only
     * after the store has been judged good. A cache written from a failed
     * parse would be read back on the next launch as though it were real.
     */
    gFetchedAt = UnixNow();
    SaveCache(gCurrentURL, gFetchedAt);
    PublishCounts();
    Refilter();

    gState = kGazetteRefreshDone;
    return gState;
}

GazetteRefreshState GazetteFeedsRefreshGetState(void)
{
    return gState;
}

int GazetteFeedsRefreshProgress(void)
{
    return gArticleCount;
}

const char *GazetteFeedsRefreshErrorText(void)
{
    return gError;
}


/* ------------------------------------------------------------------ */
/* Full article text                                                   */
/* ------------------------------------------------------------------ */

static void ReleaseFullText(void)
{
    if (gFullFetch != NULL) {
        GazetteFetchDestroy(gFullFetch);
        gFullFetch = NULL;
    }
    if (gExtract != NULL) {
        DisposePtr((Ptr)gExtract);
        gExtract = NULL;
    }
    if (gResolve != NULL) {
        DisposePtr((Ptr)gResolve);
        gResolve = NULL;
    }
    gFullStage = kFullStageArticle;
}

void GazetteFeedsFullTextCancel(void)
{
    ReleaseFullText();
    GazettePhotosCancel();          /* they were the page's; the page goes */
    gFullText[0]        = '\0';
    gFullError[0]       = '\0';
    gFullPhotoCount     = 0;
    gFullFinalURL[0]    = '\0';
    gFullArticle        = -1;
    gPendingFullArticle = -1;
    gWantArticle        = -1;
    gWantURL[0]         = '\0';
    gFullState          = kGazetteRefreshIdle;
}

/*
 * Start the page that was asked for while the line was busy. Called from the
 * idle loop, so it costs a comparison a pass and begins the moment whatever
 * was holding the connection lets go. Returns 1 if a fetch started.
 */
int GazetteFeedsFullTextResume(void)
{
    if (gWantArticle < 0 || gState == kGazetteRefreshRunning ||
        gFullState == kGazetteRefreshRunning) {
        return 0;
    }
    return BeginFullText();
}

int GazetteFeedsFullTextComing(int articleIndex)
{
    return (articleIndex >= 0 && gWantArticle == articleIndex);
}

/*
 * Give the connection up without giving the article up. A refresh outranks an
 * article's page — headlines are what the window is for — but abandoning the
 * page would leave the pane saying it was still reading with nothing on its
 * way. The request stands and Resume comes back for it.
 */
static void PauseFullText(void)
{
    ReleaseFullText();
    gPendingFullArticle = -1;
    gFullState          = kGazetteRefreshIdle;
}

int GazetteFeedsFullTextArticle(void)
{
    return gFullArticle;
}

const char *GazetteFeedsFullText(void)
{
    return gFullText;
}

const char *GazetteFeedsFullTextErrorText(void)
{
    return gFullError;
}

int GazetteFeedsFullTextPhotos(const GazettePhotoRef **refs)
{
    if (refs != NULL) {
        *refs = gFullPhotos;
    }
    return gFullArticle >= 0 ? gFullPhotoCount : 0;
}

const char *GazetteFeedsFullTextFinalURL(void)
{
    return gFullFinalURL;
}

GazetteRefreshState GazetteFeedsFullTextGetState(void)
{
    return gFullState;
}

/* The fetch's body sink: the page goes straight into the extractor, which
   stops the fetch itself once it has as much text as it keeps. */
static int FullTextSink(const char *data, size_t len, void *context)
{
    (void)context;

    if (gExtract == NULL) {
        return 0;
    }
    return GazetteExtractFeed(gExtract, data, len);
}

/* The resolver's two sinks: the scan of Google's page, and the answer. */
static int ResolvePageSink(const char *data, size_t len, void *context)
{
    (void)context;
    if (gResolve == NULL) {
        return 0;
    }
    return GazetteGNewsScanFeed(&gResolve->scan, data, len);
}

static int ResolveAnswerSink(const char *data, size_t len, void *context)
{
    size_t room;

    (void)context;
    if (gResolve == NULL) {
        return 0;
    }
    room = sizeof gResolve->answer - 1 - gResolve->answerLen;
    if (len > room) {
        len = room;
    }
    memcpy(gResolve->answer + gResolve->answerLen, data, len);
    gResolve->answerLen += len;
    return room > len;              /* full is enough: the address is near the top */
}

static void FailFullText(const char *why)
{
    snprintf(gFullError, sizeof gFullError, "%s", why);
    ReleaseFullText();
    gWantArticle = -1;              /* settled: it is not coming */
    gFullState   = kGazetteRefreshFailed;
}

/* The story's own page, into the extractor. */
static int BeginArticleStage(void)
{
    gExtract = (GazetteExtract *)NewPtrClear((Size)sizeof(GazetteExtract));
    if (gExtract == NULL) {
        FailFullText("Not enough memory to read the article.");
        return 0;
    }
    GazetteExtractInit(gExtract);

    gFullStage = kFullStageArticle;
    gFullFetch = GazetteFetchStart(gWantURL, FullTextSink, NULL);
    if (gFullFetch == NULL) {
        FailFullText("Could not open the article's page.");
        return 0;
    }
    return 1;
}

/*
 * Open the connection for the page already named in gWantArticle / gWantURL.
 * Both entry points below go through here, so starting and resuming are the
 * same act and cannot drift apart. A Google News link goes to Google first,
 * for the story's address; see GNewsResolve.
 */
static int BeginFullText(void)
{
    gPendingFullArticle = gWantArticle;
    gFullState          = kGazetteRefreshRunning;

    if (gz_contains_ci(gWantURL, strlen(gWantURL), "news.google.com/")) {
        gResolve = (GNewsResolve *)NewPtrClear((Size)sizeof(GNewsResolve));
        if (gResolve == NULL) {
            FailFullText("Not enough memory to read the article.");
            return 0;
        }
        if (!GazetteGNewsArticleToken(gWantURL, gResolve->token,
                                      sizeof gResolve->token)) {
            /* On Google, but not a story link: read it as it is. */
            DisposePtr((Ptr)gResolve);
            gResolve = NULL;
            return BeginArticleStage();
        }
        GazetteGNewsScanInit(&gResolve->scan);

        /* url[] is scratch until the answer fills it. */
        snprintf(gResolve->url, sizeof gResolve->url,
                 "https://news.google.com/rss/articles/%s", gResolve->token);
        gFullStage = kFullStagePage;
        gFullFetch = GazetteFetchStart(gResolve->url, ResolvePageSink, NULL);
        if (gFullFetch == NULL) {
            FailFullText("Could not ask Google News where the story is.");
            return 0;
        }
        return 1;
    }

    return BeginArticleStage();
}

int GazetteFeedsFullTextRecall(int articleIndex, const char *url)
{
    const ReadPage *page;

    if (url == NULL || url[0] == '\0' ||
        articleIndex < 0 || articleIndex >= GazetteFeedsArticleCount()) {
        return 0;
    }
    page = FindReadPage(url);
    if (page == NULL) {
        return 0;
    }
    RecallReadPage(page, articleIndex);
    return 1;
}

int GazetteFeedsFullTextStart(int articleIndex, const char *url)
{
    if (url == NULL || url[0] == '\0') {
        return 0;
    }
    if (articleIndex < 0 || articleIndex >= GazetteFeedsArticleCount()) {
        return 0;
    }

    /* Whatever was held was for a different article, and a fetch still in
       flight is for one the user has already moved on from. */
    GazetteFeedsFullTextCancel();

    gWantArticle = articleIndex;
    gz_copy_n(gWantURL, sizeof gWantURL, url, strlen(url));
    gz_copy_n(gWantKey, sizeof gWantKey, url, strlen(url));

    /*
     * The refresh has the connection. Answered 1 all the same, because the
     * page *is* coming — the reader pane asks this before it decides whether
     * to lay out the summary, and "in a moment" is not "no".
     */
    if (gState == kGazetteRefreshRunning) {
        return 1;
    }
    return BeginFullText();
}

GazetteRefreshState GazetteFeedsFullTextPump(void)
{
    GazetteFetchState fetchState;
    size_t            len;

    if (gFullState != kGazetteRefreshRunning || gFullFetch == NULL) {
        return gFullState;
    }

    fetchState = GazetteFetchPump(gFullFetch);

    if (fetchState == kGazetteFetchFailed) {
        FailFullText(GazetteFetchErrorText(gFullFetch));
        return gFullState;
    }

    if (fetchState != kGazetteFetchDone) {
        return gFullState;
    }

    /*
     * Google's page is in, or as much of it as the scan needed. If Google
     * answered with a redirect to the story itself — the older kind of
     * link — the address is simply where the fetch ended up; otherwise the
     * two attributes go to the decoder.
     */
    if (gFullStage == kFullStagePage) {
        const char *where = GazetteFetchFinalURL(gFullFetch);

        if (!gz_contains_ci(where, strlen(where), "news.google.com")) {
            gz_copy_n(gWantURL, sizeof gWantURL, where, strlen(where));
            GazetteFetchDestroy(gFullFetch);
            gFullFetch = NULL;
            (void)BeginArticleStage();
            return gFullState;
        }
        if (!GazetteGNewsScanDone(&gResolve->scan) ||
            GazetteGNewsBuildBody(gResolve->token, gResolve->scan.ts,
                                  gResolve->scan.sig, gResolve->body,
                                  sizeof gResolve->body) == 0) {
            FailFullText("Google News did not say where the story is.");
            return gFullState;
        }
        GazetteFetchDestroy(gFullFetch);
        gFullStage          = kFullStageAnswer;
        gResolve->answerLen = 0;
        gFullFetch = GazetteFetchStartPost(
            "https://news.google.com/_/DotsSplashUi/data/batchexecute",
            "application/x-www-form-urlencoded;charset=UTF-8",
            gResolve->body, strlen(gResolve->body), ResolveAnswerSink, NULL);
        if (gFullFetch == NULL) {
            FailFullText("Could not ask Google News where the story is.");
        }
        return gFullState;
    }

    if (gFullStage == kFullStageAnswer) {
        if (!GazetteGNewsParseAnswer(gResolve->answer, gResolve->answerLen,
                                     gResolve->url, sizeof gResolve->url)) {
            FailFullText("Google News did not say where the story is.");
            return gFullState;
        }
        gz_copy_n(gWantURL, sizeof gWantURL, gResolve->url,
                  strlen(gResolve->url));
        GazetteFetchDestroy(gFullFetch);
        gFullFetch = NULL;
        DisposePtr((Ptr)gResolve);
        gResolve = NULL;
        (void)BeginArticleStage();
        return gFullState;
    }

    /*
     * The site said no. A 403 from a bot wall, a 405 from a WAF, a 404, a
     * 500: what came with it is a notice, not the story, and reading it
     * as the story is how "verify that you're not a robot" ends up in the
     * reader pane. The feed's own summary is what NewsProxy shows for these
     * and it is what Gazette shows too; the status line says why.
     */
    if (GazetteFetchStatus(gFullFetch) >= 400) {
        char why[64];

        snprintf(why, sizeof why, "The site would not serve the page (%d).",
                 GazetteFetchStatus(gFullFetch));
        FailFullText(why);
        return gFullState;
    }

    /*
     * Not a page at all. A feed's link may go to a PDF — a press release,
     * a paper — or to a picture, and read as HTML a PDF comes out as
     * "%PDF-1.7 %???? 14 0 obj" in the reader pane. The type is the
     * server's word; a server that says nothing is read as a page.
     */
    {
        const char *type = GazetteFetchContentType(gFullFetch);

        if (type[0] != '\0' && !gz_starts_ci(type, strlen(type), "text/") &&
            !gz_contains_ci(type, strlen(type), "html") &&
            !gz_contains_ci(type, strlen(type), "xml")) {
            char why[96];

            snprintf(why, sizeof why, "That link is a %s, not a page.",
                     type);
            FailFullText(why);
            return gFullState;
        }
    }

    /* Done also means the extractor filled up and stopped the fetch, which is
       a success: what it has is as much as it keeps. */
    (void)GazetteExtractFinish(gExtract);

    if (!GazetteExtractUsable(gExtract)) {
        /*
         * A paywall stub, a cookie wall, a consent page, or a redirector that
         * only works with JavaScript — and no description in its head to
         * stand in. The feed's own summary is better than any of those, so
         * the failure keeps it on screen.
         */
        FailFullText("That page had no article text in it.");
        return gFullState;
    }
    len = strlen(GazetteExtractText(gExtract));

    gz_copy_n(gFullText, sizeof gFullText, GazetteExtractText(gExtract), len);
    gFullArticle = gPendingFullArticle;

    /* The pictures and the address to resolve them against, before the
       extractor and the fetch go. */
    {
        int i;

        gFullPhotoCount = GazetteExtractPhotoCount(gExtract);
        for (i = 0; i < gFullPhotoCount; i++) {
            gFullPhotos[i] = *GazetteExtractPhoto(gExtract, i);
        }
        gz_copy_n(gFullFinalURL, sizeof gFullFinalURL,
                  GazetteFetchFinalURL(gFullFetch),
                  strlen(GazetteFetchFinalURL(gFullFetch)));
    }

    ReleaseFullText();
    gWantArticle = -1;              /* settled: it is here */
    gFullState   = kGazetteRefreshDone;
    RememberReadPage();
    return gFullState;
}

/* ------------------------------------------------------------------ */
/* The cache                                                           */
/*                                                                     */
/* A line-oriented text file, one field per line, tagged by a single    */
/* letter. That format is only possible because every string in the     */
/* store has already been through gz_flatten_ws: there are no embedded  */
/* newlines to escape and nothing to quote, so writing is a print and   */
/* reading is a switch. It also means the file opens readably in        */
/* SimpleText, which is worth something when the question is "why does  */
/* this feed show the wrong articles".                                  */
/* ------------------------------------------------------------------ */

static void WriteLongLine(GazetteStoreFile *f, char tag, long value)
{
    char line[24];

    snprintf(line, sizeof line, "%c %ld", tag, value);
    GazetteStoreWriteLine(f, line);
}

static void WriteTextLine(GazetteStoreFile *f, char tag, const char *text)
{
    char line[8];

    line[0] = tag;
    line[1] = ' ';
    line[2] = '\0';
    GazetteStoreWrite(f, line, 2);
    GazetteStoreWrite(f, text, (long)strlen(text));
    GazetteStoreWrite(f, "\r", 1);
}

/*
 * The body, one 'B' line per paragraph. The format is line-oriented and a
 * field cannot hold a newline, so a body that has paragraphs is written as
 * several lines and read back joined by the newline they stand for. An empty
 * body writes nothing, which reads back as an empty body.
 */
static void WriteBodyLines(GazetteStoreFile *f, const char *body)
{
    const char *at = body;

    while (*at != '\0') {
        const char *end = strchr(at, '\n');
        size_t      len = (end != NULL) ? (size_t)(end - at) : strlen(at);

        GazetteStoreWrite(f, "B ", 2);
        GazetteStoreWrite(f, at, (long)len);
        GazetteStoreWrite(f, "\r", 1);

        if (end == NULL) {
            break;
        }
        at = end + 1;
    }
}

static void SaveCache(const char *url, long fetchedAt)
{
    GazetteStoreFile *f = GazetteStoreCacheCreate(url);
    int               i;

    if (f == NULL) {
        return;             /* a cache that cannot be written is not an error */
    }

    GazetteStoreWriteLine(f, kCacheMagic);
    WriteTextLine(f, 'U', url);
    WriteTextLine(f, 'F', gFeedTitle);
    WriteLongLine(f, 'W', fetchedAt);

    for (i = 0; i < gArticleCount; i++) {
        const GazetteArticle *a = &gArticles[i];

        GazetteStoreWriteLine(f, "-");
        WriteTextLine(f, 'T', a->title);
        WriteTextLine(f, 'L', a->link);
        WriteTextLine(f, 'S', a->source);
        WriteLongLine(f, 'D', a->date);
        WriteBodyLines(f, a->body);
    }

    GazetteStoreClose(f);
}

void GazetteFeedsFlush(void)
{
    /* The cache holds articles and the index holds what has been read, so
       there is nothing here to write but the index -- and it writes only when
       something in it actually moved. */
    GazetteIndexSave();
}

/* The other half of WriteBodyLines: each 'B' line is a paragraph, joined back
   by the newline the split stood for. */
static void AppendBodyLine(GazetteArticle *article, const char *text)
{
    size_t used = strlen(article->body);
    size_t room = sizeof article->body - used;

    if (room <= 1) {
        return;                     /* full; the rest of the body is dropped */
    }
    if (used > 0) {
        article->body[used++] = '\n';
        room--;
    }
    gz_copy_n(article->body + used, room, text, strlen(text));
}

/*
 * Walk a feed's cache file, handing each complete article to emit. The two
 * callers want the same parsing and different commit policies -- one feed
 * appends in file order, a group merges by date -- so the policy is the
 * callback and the parsing is here once.
 *
 * An article is only handed over when the next separator or the end of the
 * file says its record is complete, so a cache truncated by a crash costs the
 * article it was in the middle of and nothing else.
 */
static int ScanCache(const char *url, int feedIndex,
                     void (*emit)(const GazetteArticle *a, void *ctx),
                     void *ctx, char *titleOut, size_t titleCap,
                     long *fetchedAtOut)
{
    GazetteStoreFile *f;
    char              line[kGazetteArticleBodyLen + 8];
    GazetteArticle    article;
    int               count     = 0;
    int               inArticle = 0;
    long              n;

    if (url == NULL || url[0] == '\0') {
        return 0;
    }
    f = GazetteStoreCacheOpen(url);
    if (f == NULL) {
        return 0;
    }

    n = GazetteStoreReadLine(f, line, (long)sizeof line);
    if (n < 0 || strcmp(line, kCacheMagic) != 0) {
        /* A file from another version, or not ours at all. Refusing it is
           better than reading it as though the fields still mean what they
           did; the next refresh overwrites it. */
        GazetteStoreClose(f);
        return 0;
    }

    memset(&article, 0, sizeof article);
    article.feed = feedIndex;

    while ((n = GazetteStoreReadLine(f, line, (long)sizeof line)) >= 0) {
        char        tag  = (n > 0) ? line[0] : '\0';
        const char *rest = (n > 2) ? line + 2 : "";

        if (tag == '-') {
            if (inArticle) {
                emit(&article, ctx);
                count++;
            }
            memset(&article, 0, sizeof article);
            article.feed = feedIndex;
            inArticle    = 1;
            continue;
        }

        switch (tag) {
            case 'U': break;                /* the URL, for the reader's eye */
            case 'F': if (titleOut != NULL) {
                          gz_copy_n(titleOut, titleCap, rest, strlen(rest));
                      }
                      break;
            case 'W': if (fetchedAtOut != NULL) {
                          *fetchedAtOut = gz_parse_dec(rest, strlen(rest), 0);
                      }
                      break;
            case 'T': gz_copy_n(article.title, sizeof article.title,
                                rest, strlen(rest)); break;
            case 'L': gz_copy_n(article.link, sizeof article.link,
                                rest, strlen(rest)); break;
            case 'S': gz_copy_n(article.source, sizeof article.source,
                                rest, strlen(rest)); break;
            case 'B': AppendBodyLine(&article, rest); break;
            case 'D': article.date = gz_parse_dec(rest, strlen(rest), 0); break;
            default:  break;                /* an unknown tag is skipped */
        }
    }

    if (inArticle) {
        emit(&article, ctx);
        count++;
    }

    GazetteStoreClose(f);
    return count;
}

/* One feed: the store is emptied when the first complete article arrives, so
   a cache that turns out to hold nothing leaves the window alone, and the
   feed's own order is kept. */
typedef struct {
    int  count;
    long max;
} LoadOneCtx;

static void EmitAppend(const GazetteArticle *a, void *ctx)
{
    LoadOneCtx *c = (LoadOneCtx *)ctx;

    if (c->count == 0) {
        GazetteFeedsClear();
    }
    if (c->count >= kGazetteMaxArticles ||
        (c->max > 0 && c->count >= c->max)) {
        c->count++;                 /* counted, not kept */
        return;
    }
    gArticles[c->count++] = *a;
}

int GazetteFeedsLoadCache(int feedIndex, const char *url, long maxArticles)
{
    LoadOneCtx ctx;
    char       title[kGazetteFeedTitleLen];
    long       fetchedAt = 0;
    int        i;

    ctx.count = 0;
    ctx.max   = maxArticles;
    title[0]  = '\0';

    ScanCache(url, feedIndex, EmitAppend, &ctx, title, sizeof title,
              &fetchedAt);

    if (ctx.count == 0) {
        return 0;
    }
    if (ctx.count > kGazetteMaxArticles) {
        ctx.count = kGazetteMaxArticles;
    }
    if (maxArticles > 0 && ctx.count > (int)maxArticles) {
        ctx.count = (int)maxArticles;
    }

    gArticleCount = ctx.count;
    gCurrentFeed  = feedIndex;
    gCurrentGroup = -1;
    gCurrentSmart = -1;
    gFetchedAt    = fetchedAt;

    /* Where a later Flush writes the read state back to. A refresh sets this
       too; loading has to as well, or opening an article in a cached feed
       would have nowhere to record it. */
    gz_copy_n(gCurrentURL, sizeof gCurrentURL, url, strlen(url));
    gz_copy_n(gFeedTitle, sizeof gFeedTitle, title, strlen(title));

    /* The cache says what the articles are; the index says which of them have
       been read. Applied here rather than stored in the cache so a group view
       and a feed view agree about the same article. */
    for (i = 0; i < gArticleCount; i++) {
        gArticles[i].read    = GazetteIndexIsRead(gArticles[i].link);
        gArticles[i].starred = GazetteIndexIsStarred(gArticles[i].link);
    }
    PublishCounts();
    Refilter();
    return 1;
}

/*
 * A group: every enabled feed in it, merged newest first. Inserted in date
 * order as they arrive rather than gathered and sorted, because gathering
 * ten feeds' articles first would need ten times the store to hold what only
 * kGazetteMaxArticles of will be kept.
 */
static void EmitMerge(const GazetteArticle *a, void *ctx)
{
    int at;

    (void)ctx;

    /*
     * One article once. Two feeds can carry the same story — two Google
     * News topics that overlap, a site's main feed and a section's — and a
     * merged view read them both, so Starred showed a starred article
     * twice. The link is what the index knows an article by, so it is what
     * "the same" means here; the first one in stays.
     */
    if (a->link[0] != '\0') {
        for (at = 0; at < gArticleCount; at++) {
            if (strcmp(gArticles[at].link, a->link) == 0) {
                return;
            }
        }
    }

    for (at = 0; at < gArticleCount; at++) {
        if (gArticles[at].date < a->date) {
            break;
        }
    }

    if (gArticleCount >= kGazetteMaxArticles) {
        if (at >= kGazetteMaxArticles) {
            return;                 /* older than everything already held */
        }
        gArticleCount = kGazetteMaxArticles - 1;    /* the oldest drops out */
    }

    memmove(&gArticles[at + 1], &gArticles[at],
            (size_t)(gArticleCount - at) * sizeof gArticles[0]);
    gArticles[at] = *a;
    gArticleCount++;
}

/*
 * Which standing view is being gathered, for EmitSmart to consult. A
 * file-scope flag rather than the emitter's context pointer because ScanCache
 * hands that straight through and EmitMerge already ignores it; one of the
 * two would have to grow a structure to carry both.
 */
static int  gSmartWhich = -1;
static long gSmartToday;        /* the local day "Today" means */

/*
 * One article, on its way into a standing view. The question each view asks
 * is asked here rather than after the merge, because the merge keeps only the
 * newest kGazetteMaxArticles and a starred article from last month would be
 * thrown away before anything looked at it.
 *
 * The read and starred flags come from the index rather than from the article
 * off the cache, for the same reason they do everywhere else: the cache holds
 * what the feed said and the index holds what the reader has done.
 */
static void EmitSmart(const GazetteArticle *a, void *ctx)
{
    switch (gSmartWhich) {
        case kGazetteSmartToday:
            if (a->date == 0 ||
                GazetteDayNumber(GazetteFeedsLocalTime(a->date)) !=
                    gSmartToday) {
                return;
            }
            break;
        case kGazetteSmartUnread:
            if (GazetteIndexIsRead(a->link)) {
                return;
            }
            break;
        case kGazetteSmartStarred:
            if (!GazetteIndexIsStarred(a->link)) {
                return;
            }
            break;
        default:
            return;
    }
    EmitMerge(a, ctx);
}

int GazetteFeedsLoadSmart(int which, long maxArticles)
{
    int cap = kGazetteMaxArticles;
    int i;

    if (which < 0 || which >= kGazetteSmartCount) {
        return 0;
    }
    if (maxArticles > 0 && maxArticles < cap) {
        cap = (int)maxArticles;
    }

    GazetteFeedsClear();

    gSmartWhich = which;

    /*
     * UnixNow is already local — GetDateTime reads the Macintosh clock, and
     * the Macintosh clock keeps local time — so it is the *article* that has
     * to be converted and not the other way round. Putting the offset on both
     * sides counts it twice, which on this side of the Atlantic quietly moves
     * "Today" by an hour and at the ends of the day by a whole one.
     */
    gSmartToday = GazetteDayNumber(UnixNow());

    for (i = 0; i < GazetteCoreFeedCount(); i++) {
        if (!GazetteCoreFeedEnabled(i)) {
            continue;
        }
        ScanCache(GazetteCoreFeedURL(i), i, EmitSmart, NULL, NULL, 0, NULL);

        /* Trim as we go, so a hundred feeds cost one store rather than a
           hundred. */
        if (gArticleCount > cap) {
            gArticleCount = cap;
        }
    }
    gSmartWhich = -1;

    gCurrentFeed  = -1;
    gCurrentGroup = -1;
    gCurrentSmart = which;
    gFetchedAt    = 0;

    /* No single feed owns this view, so there is nothing for Flush to write
       back against. Marking an article read still works: the index is keyed
       by the article, not by the feed. */
    gCurrentURL[0] = '\0';
    gz_copy_n(gFeedTitle, sizeof gFeedTitle, GazettePrefsSmartName(which),
              strlen(GazettePrefsSmartName(which)));

    for (i = 0; i < gArticleCount; i++) {
        gArticles[i].read    = GazetteIndexIsRead(gArticles[i].link);
        gArticles[i].starred = GazetteIndexIsStarred(gArticles[i].link);
    }
    Refilter();
    return gArticleCount;
}

int GazetteFeedsCurrentSmart(void)
{
    return gCurrentSmart;
}

/* Counting, for the number beside "Today" in the sidebar. Nothing is kept. */
static void EmitCountToday(const GazetteArticle *a, void *ctx)
{
    if (a->date != 0 &&
        GazetteDayNumber(GazetteFeedsLocalTime(a->date)) == gSmartToday) {
        (*(int *)ctx)++;
    }
}

/*
 * How many articles are dated today, across every enabled feed.
 *
 * This one has to be counted, and counting it means reading every cache: a
 * feed's file says what is in it, and what is in it changes meaning at
 * midnight, so there is nothing that could be written down and read back.
 * The other two views' numbers are free — the index already keeps an unread
 * count per feed and a starred set — which is why only this one is a
 * function and why it is asked once when the sidebar's rows are rebuilt
 * rather than once per row drawn.
 */
int GazetteFeedsCountToday(void)
{
    int total = 0;
    int i;

    gSmartToday = GazetteDayNumber(UnixNow());

    for (i = 0; i < GazetteCoreFeedCount(); i++) {
        if (GazetteCoreFeedEnabled(i)) {
            ScanCache(GazetteCoreFeedURL(i), i, EmitCountToday, &total,
                      NULL, 0, NULL);
        }
    }
    return total;
}

int GazetteFeedsLoadGroup(int group, long maxArticles)
{
    int cap = kGazetteMaxArticles;
    int i;

    if (maxArticles > 0 && maxArticles < cap) {
        cap = (int)maxArticles;
    }

    GazetteFeedsClear();

    for (i = 0; i < GazetteCoreFeedCount(); i++) {
        if (GazetteCoreFeedGroup(i) != group || !GazetteCoreFeedEnabled(i)) {
            continue;
        }
        ScanCache(GazetteCoreFeedURL(i), i, EmitMerge, NULL, NULL, 0, NULL);

        /* Trim as we go, so ten feeds cost one store rather than ten. */
        if (gArticleCount > cap) {
            gArticleCount = cap;
        }
    }

    gCurrentFeed  = -1;
    gCurrentGroup = group;
    gCurrentSmart = -1;
    gFetchedAt    = 0;

    /*
     * No single feed owns this view, so there is nothing for Flush to write
     * back against and nothing to refresh into. Marking an article read still
     * works: the index is keyed by the article, not by the feed.
     */
    gCurrentURL[0] = '\0';
    gz_copy_n(gFeedTitle, sizeof gFeedTitle, GazetteCoreGroupName(group),
              strlen(GazetteCoreGroupName(group)));

    for (i = 0; i < gArticleCount; i++) {
        gArticles[i].read    = GazetteIndexIsRead(gArticles[i].link);
        gArticles[i].starred = GazetteIndexIsStarred(gArticles[i].link);
    }
    Refilter();
    return gArticleCount;
}

void GazetteFeedsForgetCache(const char *url)
{
    GazetteStoreCacheDelete(url);
}

void GazetteFeedsRefreshCancel(void)
{
    ReleaseRefresh();
    if (gState == kGazetteRefreshRunning) {
        gState = kGazetteRefreshIdle;
    }
}
