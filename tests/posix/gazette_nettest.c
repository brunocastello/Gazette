/*
 * Gazette — the POSIX network path, proved on CI
 * Copyright (c) 2026 brunocastello
 *
 * Nothing here is shipped. It is the application's own fetch —
 * src/net/gazette_fetch.c over gazette_net.c over Certainly's BSD-sockets
 * transport, TLS and all — driven from a console program, so that CI can run
 * it on Linux and macOS against live servers and show in its log that the
 * Mac OS X build's network path really does fetch and parse a feed. The loop is the shell's: poll,
 * sleep a little, poll again, never block.
 *
 *   GazetteNetTest <url> [<url> ...]
 *
 * For each URL: the status, where the redirects ended, how much came, and —
 * when it parses as a feed — its title and first headlines. Exits non-zero
 * if any fetch never gets an answer, so the workflow step fails with it.
 */

#include <time.h>
#include <unistd.h>

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

/* Milliseconds from a monotonic clock, in place of GetTickCount. */
static unsigned long NowMs(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long)ts.tv_sec * 1000UL +
           (unsigned long)ts.tv_nsec / 1000000UL;
}

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
    unsigned long     started = NowMs();
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
        if (NowMs() - started > kGiveUpMs) {
            printf("   FAILED: no answer in %d s\n", kGiveUpMs / 1000);
            GazetteFetchDestroy(f);
            return 0;
        }
        usleep(kSleepMs * 1000);
    }

    GazetteFeedParserFinish(&gParser);

    /*
     * What this proves is the POSIX network path: resolve, connect,
     * TLS, HTTP, redirects, chunking. Any answer from the server proves
     * it, whatever the status -- Google answers GitHub's shared runners
     * with a 503 now and then, and a build must not fail on a server's
     * bad day. Only a fetch that never got an answer fails the test.
     */
    ok = (state == kGazetteFetchDone && GazetteFetchStatus(f) > 0);
    if (state == kGazetteFetchFailed) {
        printf("   FAILED: %s\n", GazetteFetchErrorText(f));
    } else {
        printf("   %d %s, %ld bytes in %lu ms\n   from %s\n",
               GazetteFetchStatus(f), GazetteFetchContentType(f),
               GazetteFetchBytesRead(f),
               NowMs() - started,
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
        printf("FAILED: Certainly would not start\n");
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
