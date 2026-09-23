/*
 * Gazette — the whole engine on Windows, proved on CI
 * Copyright (c) 2026 brunocastello
 *
 * Nothing here is shipped. What the window will do, from a console under
 * Wine: bring the core up (which, on a first run, writes the preferences with
 * Google News Top Stories as the one feed, exactly as the Mac does), refresh
 * that feed through the store's own refresh pump -- fetch, parse, cache --
 * then drop what is in memory and read the headlines back off the disk, as a
 * restart would. Exits non-zero if any of it does not come out.
 */

#include <windows.h>

#include <stdio.h>
#include <string.h>

#include "core/gazette_core.h"
#include "core/gazette_sys.h"
#include "feeds/gazette_feed_parse.h"
#include "feeds/gazette_feeds.h"
#include "feeds/gazette_index.h"
#include "net/gazette_net.h"

enum { kGiveUpMs = 120 * 1000 };

static int gFailures;

static void Check(const char *what, int ok)
{
    printf("   %s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) {
        gFailures++;
    }
}

static long MaxArticles(void)
{
    const GazettePrefs *prefs = GazetteCoreGetPrefs();

    return (prefs != NULL) ? prefs->maxArticles : 0;
}

int main(void)
{
    GazetteRefreshState state = kGazetteRefreshIdle;
    DWORD               started;
    int                 i, fetched;

    printf("== the core\n");
    Check("network up", GazetteNetInit());
    Check("core up, preferences read or written", GazetteCoreInit());
    Check("at least one feed", GazetteCoreFeedCount() > 0);
    if (GazetteCoreFeedCount() <= 0) {
        return 1;
    }
    printf("   feed 0: \"%s\" %s\n", GazetteCoreFeedTitle(0),
           GazetteCoreFeedURL(0));

    printf("== a refresh, as the window's idle loop drives it\n");
    Check("started", GazetteFeedsRefreshStart(0, GazetteCoreFeedURL(0),
                                              MaxArticles(), 0));
    started = GetTickCount();
    while (GetTickCount() - started < kGiveUpMs) {
        state = GazetteFeedsRefreshPump();
        if (state != kGazetteRefreshRunning) {
            break;
        }
        Sleep(10);
    }
    if (state == kGazetteRefreshFailed) {
        printf("   (refresh: %s)\n", GazetteFeedsRefreshErrorText());
    }
    Check("finished", state == kGazetteRefreshDone);
    fetched = GazetteFeedsArticleCount();
    Check("articles in the store", fetched > 0);
    for (i = 0; i < fetched && i < 3; i++) {
        const GazetteArticle *a = GazetteFeedsArticleAt(i);
        char when[32];

        (void)GazetteFormatDate(GazetteFeedsLocalTime(a->date),
                                GazetteSysLocalNow(), when, sizeof when);
        printf("   - %s (%s)\n", a->title, when);
    }

    printf("== the cache, read back as on a restart\n");
    GazetteFeedsClear();
    Check("store emptied", GazetteFeedsArticleCount() == 0);
    Check("cache loaded", GazetteFeedsLoadCache(0, GazetteCoreFeedURL(0),
                                                MaxArticles()));
    Check("the same number of headlines", GazetteFeedsArticleCount() == fetched);

    printf("== read state\n");
    if (GazetteFeedsArticleCount() > 0) {
        GazetteFeedsMarkRead(0, 1);
        GazetteFeedsFlush();
        Check("the first headline marked read and kept",
              GazetteIndexIsRead(GazetteFeedsArticleAt(0)->link));
    }

    GazetteCoreShutdown();
    GazetteNetShutdown();
    printf("%d failure%s\n", gFailures, gFailures == 1 ? "" : "s");
    return gFailures == 0 ? 0 : 1;
}
