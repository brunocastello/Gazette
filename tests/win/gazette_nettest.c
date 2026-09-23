/*
 * Gazette — the Windows network path, proved on CI
 * Copyright (c) 2026 brunocastello
 *
 * Nothing here is shipped. It is the application's own fetch —
 * src/net/gazette_fetch.c over gazette_net.c over Certainly's Winsock
 * transport, TLS and all — driven from a console program, so that CI can run
 * it under Wine against live servers and show in its log that the Windows
 * build really does fetch and parse a feed. The loop is the shell's: poll,
 * sleep a little, poll again, never block.
 *
 *   GazetteNetTest <url> [<url> ...]
 *
 * For each URL: the status, where the redirects ended, how much came, and —
 * when it parses as a feed — its title and first headlines. Exits non-zero
 * if any fetch fails, so the workflow step fails with it.
 */

#include <windows.h>

#include <stdio.h>
#include <string.h>

#include "feeds/gazette_feed_parse.h"
#include "net/gazette_fetch.h"
#include "net/gazette_net.h"

enum {
    kSleepMs     = 10,
    kGiveUpMs    = 90 * 1000,
    kShowHeadlines = 3
};

static GazetteFeedParser gParser;
static int               gArticles;

static int PrintArticle(const GazetteArticle *a, void *context)
{
    (void)context;
    if (gArticles < kShowHeadlines) {
        printf("    - %s\n", a->title);
    }
    gArticles++;
    return 1;
}

static int Sink(const char *data, size_t len, void *context)
{
    (void)context;
    (void)GazetteFeedParserFeed(&gParser, data, len);
    return 1;
}

static int FetchOne(const char *url)
{
    GazetteFetch     *f;
    GazetteFetchState state;
    DWORD             started = GetTickCount();
    int               ok;

    printf("== %s\n", url);
    gArticles = 0;
    GazetteFeedParserInit(&gParser, PrintArticle, NULL);

    f = GazetteFetchStart(url, Sink, NULL);
    if (f == NULL) {
        printf("   FAILED: could not start\n");
        return 0;
    }
    for (;;) {
        state = GazetteFetchPump(f);
        if (state == kGazetteFetchDone || state == kGazetteFetchFailed) {
            break;
        }
        if (GetTickCount() - started > kGiveUpMs) {
            printf("   FAILED: no answer in %d s\n", kGiveUpMs / 1000);
            GazetteFetchDestroy(f);
            return 0;
        }
        Sleep(kSleepMs);
    }

    GazetteFeedParserFinish(&gParser);
    ok = (state == kGazetteFetchDone && GazetteFetchStatus(f) == 200);
    if (state == kGazetteFetchFailed) {
        printf("   FAILED: %s\n", GazetteFetchErrorText(f));
    } else {
        printf("   %d %s, %ld bytes in %lu ms\n   from %s\n",
               GazetteFetchStatus(f), GazetteFetchContentType(f),
               GazetteFetchBytesRead(f),
               (unsigned long)(GetTickCount() - started),
               GazetteFetchFinalURL(f));
        if (gArticles > 0) {
            printf("   feed \"%s\": %d articles\n",
                   GazetteFeedParserTitle(&gParser), gArticles);
        }
    }
    GazetteFetchDestroy(f);
    return ok;
}

int main(int argc, char **argv)
{
    int i;
    int failures = 0;

    if (argc < 2) {
        fprintf(stderr, "usage: GazetteNetTest <url> [<url> ...]\n");
        return 2;
    }
    if (!GazetteNetInit()) {
        printf("FAILED: Winsock or Certainly would not start\n");
        return 1;
    }
    for (i = 1; i < argc; i++) {
        if (!FetchOne(argv[i])) {
            failures++;
        }
    }
    GazetteNetShutdown();
    printf("%d of %d fetched\n", argc - 1 - failures, argc - 1);
    return failures == 0 ? 0 : 1;
}
