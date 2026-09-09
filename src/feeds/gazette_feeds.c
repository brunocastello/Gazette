/*
 * Gazette — the article store and the refresh state machine
 * Copyright (c) 2026 brunocastello
 *
 * See gazette_feeds.h.
 */

#include "feeds/gazette_feeds.h"

#include "net/gazette_fetch.h"
#include "portable/gazette_portable.h"

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

static GazetteFeedParser *gParser;
static GazetteFetch      *gFetch;
static GazetteRefreshState gState = kGazetteRefreshIdle;
static long               gMaxArticles;
static int                gCleared;       /* store emptied for this refresh */
static char               gError[192];

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

void GazetteFeedsClear(void)
{
    gArticleCount = 0;
    gFeedTitle[0] = '\0';
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
        gCleared = 1;
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

int GazetteFeedsRefreshStart(const char *url, long maxArticles)
{
    if (gState == kGazetteRefreshRunning) {
        return 0;
    }

    ReleaseRefresh();
    gError[0]    = '\0';
    gCleared     = 0;
    gMaxArticles = maxArticles;

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

void GazetteFeedsRefreshCancel(void)
{
    ReleaseRefresh();
    if (gState == kGazetteRefreshRunning) {
        gState = kGazetteRefreshIdle;
    }
}
