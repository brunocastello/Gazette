/*
 * Gazette — host tests for the portable code.
 * Copyright (c) 2026 brunocastello
 *
 * Build and run with:  make -C tests/host
 *
 * Everything exercised here is compiled by a plain host cc. If a test in this
 * file needs a Toolbox header to build, the code under test is in the wrong
 * directory.
 */

#include "portable/gazette_http.h"
#include "portable/gazette_portable.h"
#include "portable/gazette_url.h"
#include "prefs/gazette_prefs.h"

#include <stdio.h>
#include <string.h>

static int gChecks;
static int gFailures;

static void CheckStr(const char *what, const char *got, const char *want)
{
    gChecks++;
    if (strcmp(got, want) != 0) {
        gFailures++;
        printf("FAIL  %s\n        got  \"%s\"\n        want \"%s\"\n",
               what, got, want);
    }
}

static void CheckLong(const char *what, long got, long want)
{
    gChecks++;
    if (got != want) {
        gFailures++;
        printf("FAIL  %s\n        got  %ld\n        want %ld\n", what, got, want);
    }
}

static void CheckTrue(const char *what, int cond)
{
    gChecks++;
    if (!cond) {
        gFailures++;
        printf("FAIL  %s\n", what);
    }
}

/* ------------------------------------------------------------------ */
/* String helpers                                                      */
/* ------------------------------------------------------------------ */

static void TestStringHelpers(void)
{
    char buf[8];

    CheckLong("stricmp equal ignoring case", gz_stricmp("Feed", "fEEd"), 0);
    CheckTrue("stricmp orders by lowercased value", gz_stricmp("a", "b") < 0);
    CheckTrue("stricmp compares length too", gz_stricmp("ab", "a") > 0);
    CheckLong("strnicmp stops at n", gz_strnicmp("feedXX", "FEEDyy", 4), 0);

    CheckLong("copy_n truncates to cap - 1",
              (long)gz_copy_n(buf, sizeof buf, "abcdefghij", 10), 7);
    CheckStr("copy_n NUL-terminates", buf, "abcdefg");

    {
        size_t      len;
        const char *p = gz_trim("  \t hello \r\n", 12, &len);
        CheckLong("trim length", (long)len, 5);
        CheckLong("trim content", (long)strncmp(p, "hello", 5), 0);
    }

    CheckLong("parse_dec plain", gz_parse_dec("42", 2, -1), 42);
    CheckLong("parse_dec negative", gz_parse_dec("-7", 2, -1), -7);
    CheckLong("parse_dec stops at non-digit", gz_parse_dec("30m", 3, -1), 30);
    CheckLong("parse_dec falls back", gz_parse_dec("abc", 3, 99), 99);
    CheckLong("parse_dec empty falls back", gz_parse_dec("", 0, 5), 5);
}

/* ------------------------------------------------------------------ */
/* Preference text grammar                                             */
/* ------------------------------------------------------------------ */

static void TestPrefsText(void)
{
    static const char text[] =
        "# a comment = not a setting\n"
        "; nor is this one\n"
        "refresh-minutes = 15\n"
        "Full-Text: 1\n"
        "empty =\n"
        "no separator here\n"
        "feed = one\n"
        "feed = two\n"
        "feed = three\n";
    size_t len = sizeof text - 1;
    char   buf[64];

    CheckTrue("get finds a setting",
              gz_prefs_get(text, len, "refresh-minutes", buf, sizeof buf));
    CheckStr("get returns the trimmed value", buf, "15");

    CheckTrue("keys are case-insensitive",
              gz_prefs_get(text, len, "full-text", buf, sizeof buf));
    CheckStr("':' works as a separator too", buf, "1");

    CheckLong("comments are not settings",
              gz_prefs_get(text, len, "a comment", buf, sizeof buf), 0);
    CheckLong("an empty value counts as absent",
              gz_prefs_get(text, len, "empty", buf, sizeof buf), 0);
    CheckLong("a line with no separator is skipped",
              gz_prefs_get(text, len, "no separator here", buf, sizeof buf), 0);
    CheckStr("a miss clears the output", buf, "");

    CheckTrue("get_nth reads the second occurrence",
              gz_prefs_get_nth(text, len, "feed", 1, buf, sizeof buf));
    CheckStr("second feed", buf, "two");
    CheckTrue("get_nth reads the third occurrence",
              gz_prefs_get_nth(text, len, "feed", 2, buf, sizeof buf));
    CheckStr("third feed", buf, "three");
    CheckLong("get_nth past the end fails",
              gz_prefs_get_nth(text, len, "feed", 3, buf, sizeof buf), 0);

    CheckLong("get_num parses", gz_prefs_get_num(text, len, "refresh-minutes", -1), 15);
    CheckLong("get_num falls back", gz_prefs_get_num(text, len, "absent", 30), 30);

    /* Gazette writes CR-terminated lines the way OS 9 text files do, so a
       file it wrote itself must read back identically. */
    {
        static const char cr_text[] = "refresh-minutes = 20\rfeed = only\r";
        CheckLong("CR line endings parse",
                  gz_prefs_get_num(cr_text, sizeof cr_text - 1,
                                   "refresh-minutes", -1), 20);
        CheckTrue("CR feed line parses",
                  gz_prefs_get_nth(cr_text, sizeof cr_text - 1, "feed", 0,
                                   buf, sizeof buf));
        CheckStr("CR feed value", buf, "only");
    }
}

/* ------------------------------------------------------------------ */
/* Transliteration and whitespace                                      */
/* ------------------------------------------------------------------ */

static void TestAsciiText(void)
{
    char out[128];

    gz_utf8_to_ascii("plain ascii", 11, out, sizeof out);
    CheckStr("ascii passes through", out, "plain ascii");

    /* Smart quotes, an em dash and an ellipsis: exactly what a news headline
       arrives with, and exactly what Geneva cannot draw. */
    {
        const char *in = "\xE2\x80\x9CSo\xE2\x80\x9D \xE2\x80\x94 it\xE2\x80\x99s\xE2\x80\xA6";
        gz_utf8_to_ascii(in, strlen(in), out, sizeof out);
        CheckStr("typographic punctuation", out, "\"So\" -- it's...");
    }

    {
        const char *in = "S\xC3\xA3o Paulo caf\xC3\xA9 na\xC3\xAFve stra\xC3\x9F""e";
        gz_utf8_to_ascii(in, strlen(in), out, sizeof out);
        CheckStr("diacritics lose their marks", out, "Sao Paulo cafe naive strasse");
    }

    {
        const char *in = "50\xC2\xB0 \xC2\xA9 2026 \xE2\x84\xA2";
        gz_utf8_to_ascii(in, strlen(in), out, sizeof out);
        CheckStr("symbols get ASCII spellings", out, "50 deg (C) 2026 (TM)");
    }

    {
        /* U+4E2D has no ASCII spelling; '?' is deliberately visible. */
        const char *in = "a\xE4\xB8\xAD""b";
        gz_utf8_to_ascii(in, strlen(in), out, sizeof out);
        CheckStr("unmapped code points become '?'", out, "a?b");
    }

    {
        /* A truncated sequence must consume a byte and keep going rather
           than reading past the end of the buffer. */
        const char *in = "ab\xC3";
        gz_utf8_to_ascii(in, 3, out, sizeof out);
        CheckStr("a truncated sequence is one '?'", out, "ab?");
    }

    {
        /* A multi-byte replacement is all-or-nothing at the buffer's edge:
           "(TM" would be worse than stopping. */
        char small[6];
        gz_utf8_to_ascii("ab\xE2\x84\xA2", 5, small, sizeof small);
        CheckStr("a replacement that will not fit is dropped whole", small, "ab");
    }

    {
        char s[] = "  lots\t\tof\r\n  space  ";
        size_t n = gz_flatten_ws(s, sizeof s - 1);
        CheckStr("whitespace flattens", s, "lots of space");
        CheckLong("flatten returns the new length", (long)n, 13);
    }

    {
        char s[] = "   \t \r\n ";
        CheckLong("all-whitespace flattens to nothing",
                  (long)gz_flatten_ws(s, sizeof s - 1), 0);
        CheckStr("and NUL-terminates", s, "");
    }
}

/* ------------------------------------------------------------------ */
/* Preferences model                                                   */
/* ------------------------------------------------------------------ */

static void TestPrefsModel(void)
{
    GazettePrefs p;

    GazettePrefsSetDefaults(&p);
    CheckLong("defaults ship one feed", p.feedCount, 1);
    CheckLong("default refresh", p.refreshMinutes, 30);
    CheckLong("default max articles", p.maxArticles, 100);
    CheckLong("full text is off by default", p.fullText, 0);
    CheckTrue("the default feed is Google News",
              strstr(p.feeds[0].url, "news.google.com") != NULL);
    CheckTrue("the default feed is enabled", p.feeds[0].enabled);

    CheckTrue("a feed can be added",
              GazettePrefsAddFeed(&p, "https://example.com/rss", "Example"));
    CheckLong("feed count grows", p.feedCount, 2);
    CheckLong("a duplicate URL is rejected",
              GazettePrefsAddFeed(&p, "https://EXAMPLE.com/rss", "Again"), 0);
    CheckLong("find is case-insensitive",
              GazettePrefsFindFeed(&p, "HTTPS://example.com/RSS"), 1);

    CheckStr("a feed with no title falls back to its URL",
             (GazettePrefsAddFeed(&p, "https://plain.example/feed", NULL),
              p.feeds[2].title),
             "https://plain.example/feed");

    CheckTrue("a feed can be removed",
              GazettePrefsRemoveFeed(&p, "https://example.com/rss"));
    CheckLong("feed count shrinks", p.feedCount, 2);
    CheckLong("removal closes the gap",
              GazettePrefsFindFeed(&p, "https://plain.example/feed"), 1);
    CheckLong("removing an absent feed fails",
              GazettePrefsRemoveFeed(&p, "https://nowhere.example/"), 0);

    /* The list is bounded on purpose; filling it must not corrupt anything. */
    {
        GazettePrefs full;
        char         url[64];
        int          i;
        int          added = 0;

        GazettePrefsSetDefaults(&full);
        for (i = 0; i < kGazetteMaxFeeds + 10; i++) {
            snprintf(url, sizeof url, "https://feed%d.example/rss", i);
            added += GazettePrefsAddFeed(&full, url, NULL);
        }
        CheckLong("the feed list stops at its capacity",
                  full.feedCount, kGazetteMaxFeeds);
        CheckLong("adds beyond capacity are refused",
                  added, kGazetteMaxFeeds - 1);
    }
}

static void TestPrefsParse(void)
{
    static const char text[] =
        "refresh-minutes = 5\r"
        "max-articles = 25\r"
        "full-text = 1\r"
        "feed = https://a.example/rss | Feed A\r"
        "feed-off = https://b.example/rss | Feed B\r"
        "feed = https://c.example/rss\r";
    GazettePrefs p;

    CheckLong("parse reads every feed",
              GazettePrefsParse(text, sizeof text - 1, &p), 3);
    CheckLong("parse reads refresh-minutes", p.refreshMinutes, 5);
    CheckLong("parse reads max-articles", p.maxArticles, 25);
    CheckLong("parse reads full-text", p.fullText, 1);

    /* Enabled feeds come first, then the disabled ones. */
    CheckStr("first feed url", p.feeds[0].url, "https://a.example/rss");
    CheckStr("first feed title", p.feeds[0].title, "Feed A");
    CheckTrue("first feed is enabled", p.feeds[0].enabled);
    CheckStr("second feed url", p.feeds[1].url, "https://c.example/rss");
    CheckStr("a feed with no title uses its URL",
             p.feeds[1].title, "https://c.example/rss");
    CheckStr("the disabled feed comes last", p.feeds[2].url, "https://b.example/rss");
    CheckLong("and is not enabled", p.feeds[2].enabled, 0);

    /* A file that lists feeds replaces the defaults outright — otherwise
       deleting Google News would not stick across a launch. */
    CheckLong("the default feed is gone",
              GazettePrefsFindFeed(&p, "https://news.google.com/rss?hl=en-US&gl=US&ceid=US:en"),
              -1);

    /* A settings-only file keeps the default feed list. */
    {
        static const char settings_only[] = "refresh-minutes = 60\r";
        GazettePrefs q;

        CheckLong("a file with no feeds keeps the defaults",
                  GazettePrefsParse(settings_only, sizeof settings_only - 1, &q), 1);
        CheckLong("and still reads its settings", q.refreshMinutes, 60);
    }

    /* Empty input is the first run: defaults, not an empty configuration. */
    {
        GazettePrefs q;
        CheckLong("empty input yields the defaults",
                  GazettePrefsParse(NULL, 0, &q), 1);
        CheckLong("with default refresh", q.refreshMinutes, 30);
    }

    /* Nonsense values must not produce a configuration that cannot refresh. */
    {
        static const char bad[] = "refresh-minutes = -5\rmax-articles = 0\r";
        GazettePrefs q;

        GazettePrefsParse(bad, sizeof bad - 1, &q);
        CheckLong("a negative refresh clamps to manual-only", q.refreshMinutes, 0);
        CheckLong("max-articles clamps to at least one", q.maxArticles, 1);
    }
}

static void TestPrefsRoundTrip(void)
{
    GazettePrefs before, after;
    char         text[kGazettePrefsTextMax];
    size_t       len;
    int          i;

    GazettePrefsSetDefaults(&before);
    GazettePrefsAddFeed(&before, "https://a.example/rss", "Feed A");
    GazettePrefsAddFeed(&before, "https://b.example/rss", "Feed B");
    before.feeds[2].enabled = 0;
    before.refreshMinutes = 45;
    before.maxArticles    = 12;
    before.fullText       = 1;

    len = GazettePrefsSerialize(&before, text, sizeof text);
    CheckTrue("serialize writes something", len > 0);
    CheckLong("serialize NUL-terminates at the length it reports",
              (long)strlen(text), (long)len);

    GazettePrefsParse(text, len, &after);

    CheckLong("round-trip keeps the feed count", after.feedCount, before.feedCount);
    CheckLong("round-trip keeps refresh-minutes",
              after.refreshMinutes, before.refreshMinutes);
    CheckLong("round-trip keeps max-articles", after.maxArticles, before.maxArticles);
    CheckLong("round-trip keeps full-text", after.fullText, before.fullText);

    /* Serialising writes the enabled feeds and the disabled ones in file
       order, and parsing re-groups them the same way, so the two lists match
       entry for entry. */
    for (i = 0; i < after.feedCount; i++) {
        CheckStr("round-trip keeps each URL", after.feeds[i].url, before.feeds[i].url);
        CheckStr("round-trip keeps each title", after.feeds[i].title, before.feeds[i].title);
        CheckLong("round-trip keeps each enabled flag",
                  after.feeds[i].enabled, before.feeds[i].enabled);
    }

    /* A buffer too small must report failure rather than write a half file
       that would then be saved over the user's real preferences. */
    {
        char small[16];
        CheckLong("serialize refuses to truncate",
                  (long)GazettePrefsSerialize(&before, small, sizeof small), 0);
        CheckStr("and leaves the buffer empty", small, "");
    }
}


/* ------------------------------------------------------------------ */
/* Header blocks                                                       */
/* ------------------------------------------------------------------ */

static void TestHeaderBlocks(void)
{
    static const char head[] =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/xml; charset=utf-8\r\n"
        "Content-Length:   1234   \r\n"
        "X-Empty:\r\n"
        "\r\n"
        "body starts here";
    size_t len = sizeof head - 1;
    size_t headLen = 0;
    size_t vLen = 0;
    const char *v;

    CheckTrue("head end is found", gz_find_head_end(head, len, &headLen));
    CheckLong("head length includes the blank line",
              (long)headLen, (long)(strstr(head, "body") - head));

    v = gz_header_find(head, headLen, "Content-Length", &vLen);
    CheckTrue("a header is found", v != NULL);
    CheckLong("its value is trimmed", (long)vLen, 4);
    CheckLong("and correct", gz_parse_dec(v, vLen, -1), 1234);

    v = gz_header_find(head, headLen, "content-type", &vLen);
    CheckTrue("header names are case-insensitive", v != NULL);
    CheckLong("value keeps its parameters", (long)vLen, 23);

    v = gz_header_find(head, headLen, "X-Empty", &vLen);
    CheckTrue("an empty header is still present", v != NULL);
    CheckLong("with a zero-length value", (long)vLen, 0);

    CheckTrue("an absent header is NULL",
              gz_header_find(head, headLen, "Location", &vLen) == NULL);

    /* The start line must never be mistaken for a field. A response whose
       status text contained a colon would otherwise match. */
    CheckTrue("the start line is skipped",
              gz_header_find(head, headLen, "HTTP/1.1 200 OK", &vLen) == NULL);

    /* An incomplete block is not a parse failure, it is "read more". */
    CheckLong("a partial head is not yet an end",
              gz_find_head_end(head, 20, &headLen), 0);

    {
        /* Bare LF is not legal HTTP and turns up anyway. */
        static const char lf[] = "HTTP/1.0 200 OK\nA: b\n\nbody";
        CheckTrue("bare LF terminates a block too",
                  gz_find_head_end(lf, sizeof lf - 1, &headLen));
        CheckLong("at the right place", (long)headLen, 22);
    }
}

/* ------------------------------------------------------------------ */
/* URLs                                                                */
/* ------------------------------------------------------------------ */

static void CheckSplit(const char *url, int wantOk, const char *wantHost,
                       int wantPort, int wantTLS, const char *wantPath)
{
    GazetteURL u;
    int ok = GazetteURLSplit(url, strlen(url), &u);

    gChecks++;
    if (ok != wantOk) {
        gFailures++;
        printf("FAIL  split \"%s\"\n        got  ok=%d\n        want ok=%d\n",
               url, ok, wantOk);
        return;
    }
    if (!wantOk) {
        return;
    }
    if (strcmp(u.host, wantHost) != 0 || u.port != wantPort ||
        u.tls != wantTLS || strcmp(u.path, wantPath) != 0) {
        gFailures++;
        printf("FAIL  split \"%s\"\n        got  %s:%d tls=%d %s\n"
               "        want %s:%d tls=%d %s\n",
               url, u.host, u.port, u.tls, u.path,
               wantHost, wantPort, wantTLS, wantPath);
    }
}

static void TestURLSplit(void)
{
    CheckSplit("http://example.com/feed.xml", 1, "example.com", 80, 0, "/feed.xml");
    CheckSplit("https://example.com/feed.xml", 1, "example.com", 443, 1, "/feed.xml");
    CheckSplit("http://example.com", 1, "example.com", 80, 0, "/");
    CheckSplit("https://example.com:8443/x", 1, "example.com", 8443, 1, "/x");
    CheckSplit("HTTPS://Example.COM/x", 1, "Example.COM", 443, 1, "/x");

    /* The query is part of the origin-form target and must survive intact —
       a Google News feed URL is nothing but query string. */
    CheckSplit("https://news.google.com/rss?hl=en-US&gl=US&ceid=US:en", 1,
               "news.google.com", 443, 1, "/rss?hl=en-US&gl=US&ceid=US:en");

    CheckSplit("ftp://example.com/x", 0, NULL, 0, 0, NULL);
    CheckSplit("example.com/x", 0, NULL, 0, 0, NULL);
    CheckSplit("http:///nohost", 0, NULL, 0, 0, NULL);
    CheckSplit("http://example.com:0/x", 0, NULL, 0, 0, NULL);
    CheckSplit("http://example.com:99999/x", 0, NULL, 0, 0, NULL);
}

static void CheckResolve(const char *base, const char *loc, const char *want)
{
    GazetteURL b, out;
    char       formatted[1024];

    gChecks++;
    if (!GazetteURLSplit(base, strlen(base), &b)) {
        gFailures++;
        printf("FAIL  resolve: base \"%s\" does not parse\n", base);
        return;
    }
    if (!GazetteURLResolve(&b, loc, strlen(loc), &out)) {
        gFailures++;
        printf("FAIL  resolve \"%s\" against \"%s\": rejected\n", loc, base);
        return;
    }
    GazetteURLFormat(&out, formatted, sizeof formatted);
    if (strcmp(formatted, want) != 0) {
        gFailures++;
        printf("FAIL  resolve \"%s\" against \"%s\"\n"
               "        got  %s\n        want %s\n", loc, base, formatted, want);
    }
}

static void TestURLResolve(void)
{
    /* Absolute: the base is irrelevant. This is the http -> https upgrade
       every feed host does, and the one redirect that must work. */
    CheckResolve("http://example.com/feed.xml", "https://example.com/feed.xml",
                 "https://example.com/feed.xml");

    CheckResolve("https://example.com/a/b/c.xml", "/other.xml",
                 "https://example.com/other.xml");
    CheckResolve("https://example.com/a/b/c.xml", "d.xml",
                 "https://example.com/a/b/d.xml");
    CheckResolve("https://example.com/a", "b",
                 "https://example.com/b");

    /* A Location taken straight out of the header block still has its line
       ending attached. */
    CheckResolve("https://example.com/x", "/y\r\n", "https://example.com/y");
    CheckResolve("https://example.com/x", "  /y  ", "https://example.com/y");

    /* A non-default port is carried across a same-origin redirect, and shows
       up again when the URL is printed. */
    CheckResolve("http://example.com:8080/a/b", "c",
                 "http://example.com:8080/a/c");

    {
        GazetteURL b, out;
        GazetteURLSplit("https://example.com/x", 21, &b);
        CheckLong("an empty Location is rejected",
                  GazetteURLResolve(&b, "", 0, &out), 0);
        CheckLong("a whitespace-only Location is rejected",
                  GazetteURLResolve(&b, " \r\n", 3, &out), 0);
    }
}

static void TestURLFormat(void)
{
    GazetteURL u;
    char       out[1024];

    GazetteURLSplit("https://example.com/x", 21, &u);
    GazetteURLFormat(&u, out, sizeof out);
    CheckStr("the default port is not printed", out, "https://example.com/x");

    GazetteURLSplit("http://example.com:8080/x", 25, &u);
    GazetteURLFormat(&u, out, sizeof out);
    CheckStr("a non-default port is printed", out, "http://example.com:8080/x");

    GazetteURLSplit("http://example.com:443/x", 24, &u);
    GazetteURLFormat(&u, out, sizeof out);
    CheckStr("443 on http is not the default", out, "http://example.com:443/x");

    {
        char small[8];
        CheckLong("format refuses to truncate",
                  (long)GazetteURLFormat(&u, small, sizeof small), 0);
        CheckStr("and leaves the buffer empty", small, "");
    }
}

/* ------------------------------------------------------------------ */
/* HTTP requests and responses                                         */
/* ------------------------------------------------------------------ */

static void TestHTTPRequest(void)
{
    GazetteURL u;
    char       req[8192];

    GazetteURLSplit("https://news.google.com/rss?hl=en-US", 36, &u);
    CheckTrue("a request is built",
              GazetteHTTPBuildGet(&u, req, sizeof req) > 0);
    CheckTrue("it is a GET with the origin-form target",
              strncmp(req, "GET /rss?hl=en-US HTTP/1.1\r\n", 28) == 0);
    CheckTrue("Host names the server",
              strstr(req, "\r\nHost: news.google.com\r\n") != NULL);
    CheckTrue("the default port is left out of Host",
              strstr(req, "news.google.com:443") == NULL);
    /* Gazette cannot inflate anything; a server allowed to choose sends gzip. */
    CheckTrue("identity encoding is demanded",
              strstr(req, "\r\nAccept-Encoding: identity\r\n") != NULL);
    CheckTrue("the connection is not held open",
              strstr(req, "\r\nConnection: close\r\n") != NULL);
    CheckTrue("it names itself",
              strstr(req, "\r\nUser-Agent: Gazette/") != NULL);
    CheckTrue("and ends with a blank line",
              strcmp(req + strlen(req) - 4, "\r\n\r\n") == 0);

    GazetteURLSplit("http://example.com:8080/x", 25, &u);
    GazetteHTTPBuildGet(&u, req, sizeof req);
    CheckTrue("a non-default port goes into Host",
              strstr(req, "\r\nHost: example.com:8080\r\n") != NULL);

    {
        char small[16];
        CheckLong("a request that will not fit is refused",
                  (long)GazetteHTTPBuildGet(&u, small, sizeof small), 0);
        CheckStr("and leaves the buffer empty", small, "");
    }
}

static void TestHTTPResponse(void)
{
    GazetteHTTPResponse r;

    {
        static const char ok[] =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/rss+xml\r\n"
            "Content-Length: 4096\r\n"
            "\r\n";
        CheckLong("a complete head parses",
                  GazetteHTTPParseResponse(ok, sizeof ok - 1, &r), 1);
        CheckLong("status", r.status, 200);
        CheckLong("minor version", r.httpMinor, 1);
        CheckLong("content length", r.contentLength, 4096);
        CheckLong("has content length", r.hasContentLength, 1);
        CheckLong("not chunked", r.chunked, 0);
        CheckLong("1.1 holds the connection open by default",
                  r.connectionClose, 0);
        CheckLong("head length", (long)r.headLen, (long)(sizeof ok - 1));
    }

    {
        static const char partial[] = "HTTP/1.1 200 OK\r\nContent-Len";
        CheckLong("an incomplete head asks for more",
                  GazetteHTTPParseResponse(partial, sizeof partial - 1, &r), 0);
    }

    {
        static const char junk[] = "NOT HTTP AT ALL\r\n\r\n";
        CheckLong("a non-HTTP response is rejected",
                  GazetteHTTPParseResponse(junk, sizeof junk - 1, &r), -1);
    }

    {
        static const char badstatus[] = "HTTP/1.1 999 Nope\r\n\r\n";
        CheckLong("an out-of-range status is rejected",
                  GazetteHTTPParseResponse(badstatus, sizeof badstatus - 1, &r), -1);
    }

    {
        static const char h10[] = "HTTP/1.0 200 OK\r\n\r\n";
        GazetteHTTPParseResponse(h10, sizeof h10 - 1, &r);
        CheckLong("HTTP/1.0 closes unless told otherwise", r.connectionClose, 1);
        CheckLong("and has no length, so the body runs to the close",
                  r.hasContentLength, 0);
    }

    {
        static const char redir[] =
            "HTTP/1.1 301 Moved Permanently\r\n"
            "Location: https://example.com/new\r\n"
            "\r\n";
        GazetteHTTPParseResponse(redir, sizeof redir - 1, &r);
        CheckLong("a redirect is recognised",
                  GazetteHTTPIsRedirect(r.status), 1);
        CheckLong("with a Location", r.hasLocation, 1);
        CheckStr("that is trimmed", r.location, "https://example.com/new");
    }

    CheckLong("200 is not a redirect", GazetteHTTPIsRedirect(200), 0);
    CheckLong("304 is not one Gazette follows", GazetteHTTPIsRedirect(304), 0);
    CheckLong("303 is", GazetteHTTPIsRedirect(303), 1);
    CheckLong("308 is", GazetteHTTPIsRedirect(308), 1);

    CheckLong("204 can carry no body", GazetteHTTPStatusHasNoBody(204), 1);
    CheckLong("304 can carry no body", GazetteHTTPStatusHasNoBody(304), 1);
    CheckLong("100 can carry no body", GazetteHTTPStatusHasNoBody(100), 1);
    CheckLong("200 can", GazetteHTTPStatusHasNoBody(200), 0);

    {
        /* Both framings at once is a broken server or a smuggling attempt.
           Either way the chunked framing is the one that governs. */
        static const char both[] =
            "HTTP/1.1 200 OK\r\n"
            "Content-Length: 10\r\n"
            "Transfer-Encoding: chunked\r\n"
            "\r\n";
        GazetteHTTPParseResponse(both, sizeof both - 1, &r);
        CheckLong("chunked wins over Content-Length", r.chunked, 1);
        CheckLong("and the length is discarded", r.hasContentLength, 0);
    }
}

/* ------------------------------------------------------------------ */
/* Chunked decoding                                                    */
/* ------------------------------------------------------------------ */

/* Feed the whole input in one go and collect everything produced. */
static size_t ChunkAll(const char *in, size_t inLen, char *out, size_t outCap,
                       int *done, long *consumedTotal)
{
    GazetteChunked c;
    size_t total = 0;
    size_t off   = 0;

    GazetteChunkedInit(&c);
    for (;;) {
        size_t produced = 0;
        long   n = GazetteChunkedFeed(&c, in + off, inLen - off,
                                      out + total, outCap - total - 1,
                                      &produced);
        if (n < 0) {
            *done = -1;
            *consumedTotal = -1;
            return total;
        }
        total += produced;
        off   += (size_t)n;
        if (n == 0 && produced == 0) break;
        if (off >= inLen) break;
        if (GazetteChunkedDone(&c)) break;
    }
    out[total] = '\0';
    *done = GazetteChunkedDone(&c);
    *consumedTotal = (long)off;
    return total;
}

static void TestChunked(void)
{
    char out[256];
    int  done;
    long consumed;

    {
        static const char body[] = "5\r\nhello\r\n6\r\n world\r\n0\r\n\r\n";
        size_t n = ChunkAll(body, sizeof body - 1, out, sizeof out,
                            &done, &consumed);
        CheckLong("chunked decodes", (long)n, 11);
        CheckStr("to the right bytes", out, "hello world");
        CheckTrue("and reaches the end", done == 1);
    }

    {
        /* Chunk extensions are legal and carry hex characters of their own,
           which must not be read as part of the size. */
        static const char body[] = "5;name=value\r\nhello\r\n0\r\n\r\n";
        size_t n = ChunkAll(body, sizeof body - 1, out, sizeof out,
                            &done, &consumed);
        CheckLong("a chunk extension is skipped", (long)n, 5);
        CheckStr("and the data is intact", out, "hello");
    }

    {
        static const char body[] = "4\r\nabcd\r\n0\r\nX-Trailer: y\r\n\r\n";
        size_t n = ChunkAll(body, sizeof body - 1, out, sizeof out,
                            &done, &consumed);
        CheckLong("trailers are consumed", (long)n, 4);
        CheckStr("body unaffected", out, "abcd");
        CheckTrue("and the stream ends", done == 1);
    }

    {
        /* Uppercase hex, which some servers send. */
        static const char body[] = "A\r\n0123456789\r\n0\r\n\r\n";
        size_t n = ChunkAll(body, sizeof body - 1, out, sizeof out,
                            &done, &consumed);
        CheckLong("uppercase hex sizes work", (long)n, 10);
        CheckStr("data", out, "0123456789");
    }

    {
        static const char body[] = "zz\r\nnope\r\n";
        size_t n = ChunkAll(body, sizeof body - 1, out, sizeof out,
                            &done, &consumed);
        (void)n;
        CheckLong("a malformed size is an error", (long)done, -1);
    }

    {
        /* Arriving a byte at a time is the normal case on a slow link: the
           decoder must hold its state across every split. */
        static const char body[] = "5\r\nhello\r\n6\r\n world\r\n0\r\n\r\n";
        GazetteChunked c;
        size_t total = 0;
        size_t i;
        char   acc[64];

        GazetteChunkedInit(&c);
        for (i = 0; i < sizeof body - 1; i++) {
            size_t produced = 0;
            long   n = GazetteChunkedFeed(&c, body + i, 1,
                                          acc + total, sizeof acc - total,
                                          &produced);
            CheckTrue("a one-byte feed never errors", n >= 0);
            total += produced;
        }
        acc[total] = '\0';
        CheckStr("byte-at-a-time decodes identically", acc, "hello world");
        CheckTrue("and still finishes", GazetteChunkedDone(&c));
    }

    {
        /* A full output buffer must stop cleanly and report how much input it
           actually took, so the caller can hand the rest back. */
        static const char body[] = "10\r\n0123456789abcdef\r\n0\r\n\r\n";
        GazetteChunked c;
        char   small[8];
        size_t produced = 0;
        long   n;

        GazetteChunkedInit(&c);
        n = GazetteChunkedFeed(&c, body, sizeof body - 1,
                               small, sizeof small, &produced);
        CheckTrue("a short feed consumes what it can", n > 0);
        CheckLong("and fills the buffer exactly", (long)produced, 8);
        CheckLong("without finishing", GazetteChunkedDone(&c), 0);
    }
}

/* ------------------------------------------------------------------ */

int main(void)
{
    TestStringHelpers();
    TestPrefsText();
    TestAsciiText();
    TestPrefsModel();
    TestPrefsParse();
    TestPrefsRoundTrip();
    TestHeaderBlocks();
    TestURLSplit();
    TestURLResolve();
    TestURLFormat();
    TestHTTPRequest();
    TestHTTPResponse();
    TestChunked();

    printf("Gazette host tests: %d checks, %d failure%s\n",
           gChecks, gFailures, gFailures == 1 ? "" : "s");

    return gFailures == 0 ? 0 : 1;
}
