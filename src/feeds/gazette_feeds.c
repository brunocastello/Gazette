/*
 * Gazette — the article store and the refresh state machine
 * Copyright (c) 2026 brunocastello
 *
 * See gazette_feeds.h.
 */

#include "feeds/gazette_feeds.h"

#include "net/gazette_fetch.h"
#include "portable/gazette_portable.h"
#include "store/gazette_store.h"

#include <DateTimeUtils.h>      /* GetDateTime */
#include <MacMemory.h>          /* NewPtrClear, DisposePtr */

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
static char               gFeedTitle[kGazetteFeedTitleLen];
static int                gCurrentFeed = -1;
static int                gPendingFeed = -1;

static GazetteFeedParser *gParser;
static GazetteFetch      *gFetch;
static GazetteRefreshState gState = kGazetteRefreshIdle;
static long               gMaxArticles;
static int                gCleared;       /* store emptied for this refresh */
static char               gError[192];
static long               gFetchedAt;

/* The URL the running refresh is for, kept so the cache can be written under
   the same name it will later be read under. */
static char               gCurrentURL[1024];

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
 */
static const char kCacheMagic[] = "GAZETTE-CACHE 3";

static void SaveCache(const char *url, long fetchedAt);

static long UnixNow(void)
{
    unsigned long macNow = 0;

    GetDateTime(&macNow);
    return (long)macNow - kMacToUnixEpoch;
}

/* ------------------------------------------------------------------ */
/* The store                                                           */
/* ------------------------------------------------------------------ */

int GazetteFeedsArticleCount(void)
{
    return gArticleCount;
}

const GazetteArticle *GazetteFeedsArticleAt(int index)
{
    if (index < 0 || index >= gArticleCount) {
        return NULL;
    }
    return &gArticles[index];
}

const char *GazetteFeedsTitle(void)
{
    return gFeedTitle;
}

int GazetteFeedsCurrentFeed(void)
{
    return gCurrentFeed;
}

void GazetteFeedsClear(void)
{
    gArticleCount = 0;
    gFeedTitle[0] = '\0';
    gCurrentFeed  = -1;
    gFetchedAt    = 0;
}

long GazetteFeedsFetchedAt(void)
{
    return gFetchedAt;
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
    }

    if (gArticleCount >= kGazetteMaxArticles ||
        (gMaxArticles > 0 && gArticleCount >= gMaxArticles)) {
        return 0;
    }

    gArticles[gArticleCount++] = *article;
    return 1;
}

/* The fetch's body sink: everything read goes straight into the parser. */
static int BodySink(const char *data, size_t len, void *context)
{
    (void)context;

    if (gParser == NULL) {
        return 0;
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
}

int GazetteFeedsRefreshStart(int feedIndex, const char *url, long maxArticles)
{
    if (gState == kGazetteRefreshRunning) {
        return 0;
    }

    ReleaseRefresh();
    gError[0]    = '\0';
    gCleared     = 0;
    gMaxArticles = maxArticles;
    gPendingFeed = feedIndex;
    gz_copy_n(gCurrentURL, sizeof gCurrentURL, url ? url : "",
              url ? strlen(url) : 0);

    gParser = (GazetteFeedParser *)NewPtrClear((Size)sizeof(GazetteFeedParser));
    if (gParser == NULL) {
        snprintf(gError, sizeof gError, "Not enough memory to read the feed.");
        gState = kGazetteRefreshFailed;
        return 0;
    }
    GazetteFeedParserInit(gParser, ArticleSink, NULL);

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

    if (GazetteFeedParserTitle(gParser)[0] != '\0') {
        gz_copy_n(gFeedTitle, sizeof gFeedTitle,
                  GazetteFeedParserTitle(gParser),
                  strlen(GazetteFeedParserTitle(gParser)));
    }

    if (gArticleCount == 0) {
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

int GazetteFeedsLoadCache(int feedIndex, const char *url, long maxArticles)
{
    GazetteStoreFile *f;
    char              line[kGazetteArticleBodyLen + 8];
    GazetteArticle    article;
    char              title[kGazetteFeedTitleLen];
    long              fetchedAt = 0;
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
    title[0] = '\0';

    /*
     * Articles are built into a local and only committed to the store when
     * the next separator or the end of the file says the record is complete.
     * Nothing is written to gArticles until the first complete one, so a
     * truncated cache leaves what was on screen alone.
     */
    while ((n = GazetteStoreReadLine(f, line, (long)sizeof line)) >= 0) {
        char        tag  = (n > 0) ? line[0] : '\0';
        const char *rest = (n > 2) ? line + 2 : "";

        if (tag == '-') {
            if (inArticle) {
                if (count == 0) {
                    GazetteFeedsClear();
                }
                if (count < kGazetteMaxArticles &&
                    (maxArticles <= 0 || count < maxArticles)) {
                    gArticles[count++] = article;
                }
            }
            memset(&article, 0, sizeof article);
            inArticle = 1;
            continue;
        }

        switch (tag) {
            case 'U': break;                /* the URL, for the reader's eye */
            case 'F': gz_copy_n(title, sizeof title, rest, strlen(rest)); break;
            case 'W': fetchedAt = gz_parse_dec(rest, strlen(rest), 0); break;
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
        if (count == 0) {
            GazetteFeedsClear();
        }
        if (count < kGazetteMaxArticles &&
            (maxArticles <= 0 || count < maxArticles)) {
            gArticles[count++] = article;
        }
    }

    GazetteStoreClose(f);

    if (count == 0) {
        return 0;
    }

    gArticleCount = count;
    gCurrentFeed  = feedIndex;
    gFetchedAt    = fetchedAt;
    gz_copy_n(gFeedTitle, sizeof gFeedTitle, title, strlen(title));
    return 1;
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
