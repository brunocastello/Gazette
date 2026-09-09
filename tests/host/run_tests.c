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

#include "portable/gazette_portable.h"
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

int main(void)
{
    TestStringHelpers();
    TestPrefsText();
    TestAsciiText();
    TestPrefsModel();
    TestPrefsParse();
    TestPrefsRoundTrip();

    printf("Gazette host tests: %d checks, %d failure%s\n",
           gChecks, gFailures, gFailures == 1 ? "" : "s");

    return gFailures == 0 ? 0 : 1;
}
