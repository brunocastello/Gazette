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

#include "extract/gazette_extract.h"
#include "feeds/gazette_feed_parse.h"
#include "feeds/gazette_googlenews.h"
#include "portable/gazette_http.h"
#include "portable/gazette_portable.h"
#include "portable/gazette_url.h"
#include "prefs/gazette_opml.h"
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
        "Sort-Oldest-First: 1\n"
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
              gz_prefs_get(text, len, "Sort-Oldest-First", buf, sizeof buf));
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

/* gz_flatten_lines is gz_flatten_ws with one difference, and the difference
   is the whole point: a run of whitespace that contained a line break stays a
   line break. */
/* The matcher behind Find. Case-insensitive over ASCII, and an empty needle
   matches everything — which is what lets a cleared search field mean no
   filter without the caller special-casing it. */
static void TestContainsCI(void)
{
    static const char hay[] = "The Quick Brown Fox";

    CheckTrue("a plain match", gz_contains_ci(hay, sizeof hay - 1, "quick"));
    CheckTrue("case does not matter",
              gz_contains_ci(hay, sizeof hay - 1, "QUICK"));
    CheckTrue("at the very start", gz_contains_ci(hay, sizeof hay - 1, "the"));
    CheckTrue("at the very end", gz_contains_ci(hay, sizeof hay - 1, "Fox"));
    CheckTrue("a phrase across words",
              gz_contains_ci(hay, sizeof hay - 1, "brown fox"));
    CheckLong("a miss", gz_contains_ci(hay, sizeof hay - 1, "cat"), 0);

    CheckTrue("an empty needle matches", gz_contains_ci(hay, sizeof hay - 1, ""));
    CheckTrue("and so does a NULL one",
              gz_contains_ci(hay, sizeof hay - 1, NULL));
    CheckLong("nothing matches an empty haystack",
              gz_contains_ci("", 0, "x"), 0);
    CheckLong("a needle longer than the text cannot match",
              gz_contains_ci("ab", 2, "abc"), 0);

    /* The length is honoured, not the terminator: matching past it would
       find text the caller did not offer. */
    CheckLong("the length bounds the search",
              gz_contains_ci("abcdef", 3, "def"), 0);
}

static void TestFlattenLines(void)
{
    char buf[128];

    strcpy(buf, "  one   two  ");
    CheckLong("flatten_lines trims and collapses",
              (long)gz_flatten_lines(buf, strlen(buf)), 7);
    CheckStr("like flatten_ws does", buf, "one two");

    strcpy(buf, "one\ntwo");
    gz_flatten_lines(buf, strlen(buf));
    CheckStr("a break survives", buf, "one\ntwo");

    strcpy(buf, "one \n\n \t two");
    gz_flatten_lines(buf, strlen(buf));
    CheckStr("a run around it is one break", buf, "one\ntwo");

    strcpy(buf, "\n\n  one  \n\n");
    gz_flatten_lines(buf, strlen(buf));
    CheckStr("and the ends are still trimmed", buf, "one");

    strcpy(buf, "a\r\nb");
    gz_flatten_lines(buf, strlen(buf));
    CheckStr("a CRLF is one break too", buf, "a\nb");

    strcpy(buf, "one\ntwo");
    gz_flatten_ws(buf, strlen(buf));
    CheckStr("flatten_ws still flattens the break away", buf, "one two");
}

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
        /* The euro is the one that started this: a currency symbol Geneva
           cannot draw became a question mark, and "? 5 billion" is not a
           sentence. The wire-service spelling is. */
        const char *in = "\xE2\x82\xAC""5bn and \xC2\xA3""3bn";
        gz_utf8_to_ascii(in, strlen(in), out, sizeof out);
        CheckStr("currency symbols become their codes", out,
                 "EUR5bn and GBP3bn");
    }

    {
        /* Latin Extended-A falls back to the letter underneath rather than
           to '?': a name with its diacritics stripped is still readable. */
        const char *in = "Lech Wa\xC5\x82\xC4\x99sa, Novak \xC4\x90okovi\xC4\x87";
        gz_utf8_to_ascii(in, strlen(in), out, sizeof out);
        CheckStr("Latin Extended-A keeps its base letters", out,
                 "Lech Walesa, Novak Dokovic");
    }

    {
        /* The two-letter ligatures are found before the range table, which
           could only ever give one letter. */
        const char *in = "\xC5\x92uvre";
        gz_utf8_to_ascii(in, strlen(in), out, sizeof out);
        CheckStr("the OE ligature is two letters", out, "OEuvre");
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
        /* The headings the headline list gathers a day's articles under.
           Counted in whole days, so an article from late last night is
           Yesterday this morning rather than Today. */
        const long day = 86400L;
        const long now = 1000L * day + 45000L;      /* mid-afternoon */

        GazetteRelativeDay(now - 3600L, now, out, sizeof out);
        CheckStr("an hour ago is Today", out, "Today");

        GazetteRelativeDay(now - 40000L, now, out, sizeof out);
        CheckStr("earlier the same day is Today", out, "Today");

        GazetteRelativeDay(999L * day + 80000L, now, out, sizeof out);
        CheckStr("late last night is Yesterday", out, "Yesterday");

        GazetteRelativeDay(997L * day, now, out, sizeof out);
        CheckStr("three days back counts days", out, "3 days ago");

        GazetteRelativeDay(992L * day, now, out, sizeof out);
        CheckStr("eight days back is one week", out, "1 week ago");

        GazetteRelativeDay(980L * day, now, out, sizeof out);
        CheckStr("twenty days back is two weeks", out, "2 weeks ago");

        GazetteRelativeDay(960L * day, now, out, sizeof out);
        CheckStr("forty days back is one month", out, "1 month ago");

        GazetteRelativeDay(600L * day, now, out, sizeof out);
        CheckStr("over a year back counts years", out, "1 year ago");

        GazetteRelativeDay(0, now, out, sizeof out);
        CheckStr("no date at all is Undated", out, "Undated");

        GazetteRelativeDay(now + 5L * day, now, out, sizeof out);
        CheckStr("a date in the future is Today, not a negative", out,
                 "Today");

        CheckLong("the same day gives the same day number",
                  GazetteDayNumber(now) - GazetteDayNumber(now - 3600L), 0);
        CheckLong("a day apart is one day number apart",
                  GazetteDayNumber(now) - GazetteDayNumber(now - day), 1);
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
    CheckLong("no article limit by default", p.maxArticles, 0);
    CheckLong("newest on top by default", p.oldestFirst, 0);
    CheckLong("nothing is hidden by default", p.hideReadArticles, 0);
    CheckLong("nor are read feeds", p.hideReadFeeds, 0);
    CheckLong("nor is the sidebar", p.hideSidebar, 0);
    CheckLong("nor is the toolbar", p.hideToolbar, 0);
    CheckLong("photos are shown by default", p.showPhotos, 1);
    CheckLong("no window is remembered by default", p.windowWidth, 0);
    CheckLong("nor a sidebar width", p.sidebarWidth, 0);
    CheckTrue("the default feed is Google News",
              strstr(p.feeds[0].url, "news.google.com") != NULL);
    CheckTrue("the default feed is enabled", p.feeds[0].enabled);

    CheckTrue("a feed can be added",
              GazettePrefsAddFeed(&p, "https://example.com/rss", "Example", -1) >= 0);
    CheckLong("feed count grows", p.feedCount, 2);
    CheckLong("a duplicate URL is rejected",
              GazettePrefsAddFeed(&p, "https://EXAMPLE.com/rss", "Again", -1), -1);
    CheckLong("find is case-insensitive",
              GazettePrefsFindFeed(&p, "HTTPS://example.com/RSS"), 1);

    CheckStr("a feed with no title falls back to its URL",
             (GazettePrefsAddFeed(&p, "https://plain.example/feed", NULL, -1),
              p.feeds[2].title),
             "https://plain.example/feed");
    CheckLong("a new feed sits at the top level", p.feeds[2].group, -1);

    CheckTrue("a feed can be removed",
              GazettePrefsRemoveFeed(&p, "https://example.com/rss"));
    CheckLong("feed count shrinks", p.feedCount, 2);
    CheckLong("removal closes the gap",
              GazettePrefsFindFeed(&p, "https://plain.example/feed"), 1);
    CheckLong("removing an absent feed fails",
              GazettePrefsRemoveFeed(&p, "https://nowhere.example/"), 0);

    /* Switching a feed off is a flag, not a removal: it keeps its place. */
    CheckTrue("a feed can be switched off",
              GazettePrefsSetFeedEnabled(&p, 1, 0));
    CheckLong("and is still in the list", p.feedCount, 2);
    CheckLong("in the same place",
              GazettePrefsFindFeed(&p, "https://plain.example/feed"), 1);
    CheckLong("with its flag clear", p.feeds[1].enabled, 0);
    CheckTrue("and can be switched back on",
              GazettePrefsSetFeedEnabled(&p, 1, 1));
    CheckLong("switching a feed that is not there fails",
              GazettePrefsSetFeedEnabled(&p, 9, 0), 0);

    /* Editing the address keeps everything else about the feed. */
    CheckTrue("a feed's address can be changed",
              GazettePrefsSetFeedURL(&p, 1, "https://plain.example/atom"));
    CheckStr("and it keeps its name", p.feeds[1].title,
             "https://plain.example/feed");
    CheckLong("the old address is gone",
              GazettePrefsFindFeed(&p, "https://plain.example/feed"), -1);
    CheckLong("the new one is there",
              GazettePrefsFindFeed(&p, "https://plain.example/atom"), 1);
    CheckLong("an empty address is refused",
              GazettePrefsSetFeedURL(&p, 1, ""), 0);
    CheckLong("another feed's address is refused",
              GazettePrefsSetFeedURL(&p, 1, p.feeds[0].url), 0);
    CheckTrue("but re-typing its own is fine",
              GazettePrefsSetFeedURL(&p, 1, "https://PLAIN.example/atom"));

    /* The list is bounded on purpose; filling it must not corrupt anything. */
    {
        GazettePrefs full;
        char         url[64];
        int          i;
        int          added = 0;

        GazettePrefsSetDefaults(&full);
        for (i = 0; i < kGazetteMaxFeeds + 10; i++) {
            snprintf(url, sizeof url, "https://feed%d.example/rss", i);
            if (GazettePrefsAddFeed(&full, url, NULL, -1) >= 0) {
                added++;
            }
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
        "feed = https://a.example/rss | Feed A | https://a.example/\r"
        "feed-off = https://b.example/rss | Feed B\r"
        "feed = https://c.example/rss\r"
        "feed = https://d.example/rss | Odd | Title\r";
    GazettePrefs p;

    CheckLong("parse reads every feed",
              GazettePrefsParse(text, sizeof text - 1, &p), 4);
    CheckLong("parse reads refresh-minutes", p.refreshMinutes, 5);
    CheckLong("parse reads max-articles", p.maxArticles, 25);

    /* File order, exactly — including the disabled one, which keeps its place
       rather than being sorted to the end. The order of the lines is the order
       of the sidebar, and a feed switched off is still where the user put it. */
    CheckStr("first feed url", p.feeds[0].url, "https://a.example/rss");
    CheckStr("first feed title", p.feeds[0].title, "Feed A");
    CheckTrue("first feed is enabled", p.feeds[0].enabled);
    CheckStr("a disabled feed keeps its place",
             p.feeds[1].url, "https://b.example/rss");
    CheckLong("and is not enabled", p.feeds[1].enabled, 0);
    CheckStr("third feed url", p.feeds[2].url, "https://c.example/rss");
    CheckStr("a feed with no title uses its URL",
             p.feeds[2].title, "https://c.example/rss");

    /* The third field is the site the feed is for. A line from before there
       was one has no site; and a pipe in a title is a pipe in a title unless
       what follows it is an address. */
    CheckStr("the third field is the home page",
             p.feeds[0].home, "https://a.example/");
    CheckStr("a line with two fields has no home page", p.feeds[1].home, "");
    CheckStr("a pipe in a title stays in the title",
             p.feeds[3].title, "Odd | Title");
    CheckStr("and is not taken for a home page", p.feeds[3].home, "");

    /* A file that lists feeds replaces the defaults outright — otherwise
       deleting Google News would not stick across a launch. */
    CheckLong("the default feed is gone",
              GazettePrefsFindFeed(&p, "https://news.google.com/rss?hl=en-US&gl=US&ceid=US:en"),
              -1);

    /* Where the window was: four numbers, or the line says nothing. A
       negative left is a second monitor to the left of the first. */
    {
        static const char window_ok[] =
            "window = -200 60 700 480\rcolumns = 150 320\r";
        static const char window_short[] = "window = 40 48 620\r";
        static const char window_flat[]  =
            "window = 40 48 0 420\rcolumns = 150 0\r";
        GazettePrefs q;

        GazettePrefsParse(window_ok, sizeof window_ok - 1, &q);
        CheckLong("window's left", q.windowLeft, -200);
        CheckLong("window's top", q.windowTop, 60);
        CheckLong("window's width", q.windowWidth, 700);
        CheckLong("window's height", q.windowHeight, 480);
        CheckLong("sidebar width", q.sidebarWidth, 150);
        CheckLong("headline column width", q.listWidth, 320);

        GazettePrefsParse(window_short, sizeof window_short - 1, &q);
        CheckLong("three numbers place no window", q.windowWidth, 0);

        GazettePrefsParse(window_flat, sizeof window_flat - 1, &q);
        CheckLong("a window with no width is no window", q.windowWidth, 0);
        CheckLong("a column with no width has none", q.listWidth, 0);
    }

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
        static const char bad[] = "refresh-minutes = -5\rmax-articles = -3\r";
        GazettePrefs q;

        GazettePrefsParse(bad, sizeof bad - 1, &q);
        CheckLong("a negative refresh clamps to manual-only", q.refreshMinutes, 0);
        CheckLong("a negative max-articles clamps to all", q.maxArticles, 0);
    }
}

static void TestPrefsRoundTrip(void)
{
    GazettePrefs before, after;
    char         text[kGazettePrefsTextMax];
    size_t       len;
    int          i;

    GazettePrefsSetDefaults(&before);
    GazettePrefsAddFeed(&before, "https://a.example/rss", "Feed A", -1);
    GazettePrefsAddFeed(&before, "https://b.example/rss", "Feed B", -1);
    before.feeds[2].enabled = 0;
    GazettePrefsSetFeedHome(&before, 1, "https://a.example/");
    GazettePrefsAddGroup(&before, "A Group");
    GazettePrefsAddFeed(&before, "https://c.example/rss", "Feed C", 0);
    GazettePrefsAddGroup(&before, "Closed Group");
    before.groups[1].collapsed = 1;
    GazettePrefsAddFeed(&before, "https://d.example/rss", "Feed D", 1);
    before.refreshMinutes = 45;
    before.maxArticles    = 12;
    before.oldestFirst      = 1;
    before.hideReadArticles = 1;
    before.hideReadFeeds    = 1;
    before.hideSidebar      = 1;
    before.hideToolbar      = 1;
    before.showPhotos       = 0;
    before.windowLeft       = -12;
    before.windowTop        = 48;
    before.windowWidth      = 800;
    before.windowHeight     = 560;
    before.sidebarWidth     = 165;
    before.listWidth        = 310;

    len = GazettePrefsSerialize(&before, text, sizeof text);
    CheckTrue("serialize writes something", len > 0);
    CheckLong("serialize NUL-terminates at the length it reports",
              (long)strlen(text), (long)len);

    GazettePrefsParse(text, len, &after);

    CheckLong("round-trip keeps the feed count", after.feedCount, before.feedCount);
    CheckLong("round-trip keeps the group count", after.groupCount, before.groupCount);
    CheckStr("round-trip keeps a group name", after.groups[0].name, "A Group");
    CheckLong("round-trip keeps a collapsed triangle",
              after.groups[1].collapsed, 1);
    CheckStr("round-trip keeps the country", after.country, before.country);
    CheckLong("round-trip keeps refresh-minutes",
              after.refreshMinutes, before.refreshMinutes);
    CheckLong("round-trip keeps max-articles", after.maxArticles, before.maxArticles);
    CheckLong("round-trip keeps the sort order",
              after.oldestFirst, before.oldestFirst);
    CheckLong("round-trip keeps hide-read-articles",
              after.hideReadArticles, before.hideReadArticles);
    CheckLong("round-trip keeps hide-read-feeds",
              after.hideReadFeeds, before.hideReadFeeds);
    CheckLong("round-trip keeps hide-sidebar",
              after.hideSidebar, before.hideSidebar);
    CheckLong("round-trip keeps hide-toolbar",
              after.hideToolbar, before.hideToolbar);
    CheckLong("round-trip keeps show-photos",
              after.showPhotos, before.showPhotos);
    CheckLong("round-trip keeps the window's left",
              after.windowLeft, before.windowLeft);
    CheckLong("round-trip keeps the window's top",
              after.windowTop, before.windowTop);
    CheckLong("round-trip keeps the window's width",
              after.windowWidth, before.windowWidth);
    CheckLong("round-trip keeps the window's height",
              after.windowHeight, before.windowHeight);
    CheckLong("round-trip keeps the sidebar's width",
              after.sidebarWidth, before.sidebarWidth);
    CheckLong("round-trip keeps the headline column's width",
              after.listWidth, before.listWidth);

    /* Serialising writes the enabled feeds and the disabled ones in file
       order, and parsing re-groups them the same way, so the two lists match
       entry for entry. */
    for (i = 0; i < after.feedCount; i++) {
        CheckStr("round-trip keeps each home page",
                 after.feeds[i].home, before.feeds[i].home);
        CheckStr("round-trip keeps each URL", after.feeds[i].url, before.feeds[i].url);
        CheckStr("round-trip keeps each title", after.feeds[i].title, before.feeds[i].title);
        CheckLong("round-trip keeps each enabled flag",
                  after.feeds[i].enabled, before.feeds[i].enabled);
        CheckLong("round-trip keeps each feed's group",
                  after.feeds[i].group, before.feeds[i].group);
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

    /* Scheme-relative: the base's scheme, the reference's host. The base's
       port does not come along — it belongs to the base's host. */
    CheckResolve("https://example.com:8443/a/b", "//cdn.example/p.jpg",
                 "https://cdn.example/p.jpg");
    CheckResolve("http://example.com/a", "//cdn.example/p.jpg",
                 "http://cdn.example/p.jpg");

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
/* Entities and markup                                                 */
/* ------------------------------------------------------------------ */

static void CheckDecode(const char *what, const char *in, const char *want)
{
    char buf[512];
    size_t n;

    strcpy(buf, in);
    n = GazetteDecodeEntities(buf, strlen(buf));
    gChecks++;
    if (strcmp(buf, want) != 0 || n != strlen(want)) {
        gFailures++;
        printf("FAIL  %s\n        got  \"%s\" (%lu)\n        want \"%s\" (%lu)\n",
               what, buf, (unsigned long)n, want, (unsigned long)strlen(want));
    }
}

static void TestEntities(void)
{
    CheckDecode("the five XML entities",
                "a&amp;b&lt;c&gt;d&quot;e&apos;f", "a&b<c>d\"e'f");
    CheckDecode("decimal numeric references", "&#65;&#66;", "AB");
    CheckDecode("hex numeric references", "&#x41;&#x62;", "Ab");

    /* U+2019 has to come out as UTF-8 so the transliterator can turn it into
       an apostrophe; a raw byte would be mojibake. */
    CheckDecode("a numeric reference becomes UTF-8",
                "it&#8217;s", "it\xE2\x80\x99s");
    CheckDecode("a named HTML entity becomes UTF-8",
                "it&rsquo;s", "it\xE2\x80\x99s");

    /* An unknown reference is far more likely to be prose than markup, and
       dropping it would silently eat content. */
    CheckDecode("an unknown entity is left alone", "a&foo;b", "a&foo;b");
    CheckDecode("a bare ampersand is left alone", "AT&T", "AT&T");
    CheckDecode("an unterminated entity is left alone", "a&amp", "a&amp");
    CheckDecode("an empty reference is left alone", "a&;b", "a&;b");
    CheckDecode("nothing to do", "plain text", "plain text");

    /* Feeds double-escape constantly: "&amp;lt;" decodes to "&lt;", which is
       text, not markup. One pass is deliberate. */
    CheckDecode("only one pass", "&amp;lt;b&amp;gt;", "&lt;b&gt;");

    {
        char buf[128];
        size_t n;

        /* A tag becomes a space only where one is needed: not at the very
           start, and not next to a space that is already there. Runs are
           collapsed downstream by gz_flatten_ws in any case. */
        strcpy(buf, "<b>Bold</b> and <i>italic</i>");
        n = GazetteStripMarkup(buf, strlen(buf));
        CheckStr("markup is stripped", buf, "Bold  and italic ");
        CheckLong("with the reported length", (long)n, (long)strlen(buf));

        strcpy(buf, "a<br>b");
        GazetteStripMarkup(buf, strlen(buf));
        CheckStr("a block tag ends the line instead", buf, "a\nb");

        strcpy(buf, "a<span>b</span>c");
        GazetteStripMarkup(buf, strlen(buf));
        CheckStr("an inline one is still a space", buf, "a b c");

        /* The break replaces a space rather than following it, so a paragraph
           never begins with one. */
        strcpy(buf, "one <p>two");
        GazetteStripMarkup(buf, strlen(buf));
        CheckStr("a break absorbs the space before it", buf, "one\ntwo");

        strcpy(buf, "one</p><p>two");
        GazetteStripMarkup(buf, strlen(buf));
        CheckStr("and two tags in a row are one break", buf, "one\ntwo");

        /* Nothing precedes the first word, so no break is written there. The
           trailing one is left for the flattener to trim, the same as the
           trailing space this used to leave. */
        strcpy(buf, "<p>Leading</p>");
        GazetteStripMarkup(buf, strlen(buf));
        CheckStr("no break before the first word", buf, "Leading\n");

        strcpy(buf, "5 > 3 and 2 < 4");
        GazetteStripMarkup(buf, strlen(buf));
        CheckStr("a stray '>' outside a tag survives", buf, "5 > 3 and 2 ");

        strcpy(buf, "no markup here");
        GazetteStripMarkup(buf, strlen(buf));
        CheckStr("plain text is untouched", buf, "no markup here");
    }
}

/* ------------------------------------------------------------------ */
/* Dates                                                               */
/* ------------------------------------------------------------------ */

static void CheckDate(const char *in, long want)
{
    long got = GazetteParseDate(in, strlen(in));

    gChecks++;
    if (got != want) {
        gFailures++;
        printf("FAIL  date \"%s\"\n        got  %ld\n        want %ld\n",
               in, got, want);
    }
}

static void TestDates(void)
{
    /* RSS: RFC 822, which is what every RSS 2.0 feed sends. */
    CheckDate("Mon, 07 Sep 2026 21:30:00 GMT", 1788816600L);
    CheckDate("07 Sep 2026 21:30:00 GMT",      1788816600L);   /* no day name */
    CheckDate("Mon, 7 Sep 2026 21:30:00 GMT",  1788816600L);   /* one digit   */
    CheckDate("Mon, 07 Sep 2026 21:30 GMT",    1788816600L);   /* no seconds  */
    CheckDate("Mon, 07 Sep 2026 21:30:00 +0000", 1788816600L);
    CheckDate("Mon, 07 Sep 2026 23:30:00 +0200", 1788816600L);
    CheckDate("Mon, 07 Sep 2026 17:30:00 EST",  1788820200L);

    /* Atom: ISO 8601. */
    CheckDate("2026-09-07T21:30:00Z",      1788816600L);
    CheckDate("2026-09-07T21:30:00+02:00", 1788809400L);
    CheckDate("2026-09-07T21:30:00.123Z",  1788816600L);   /* fractional secs */
    CheckDate("2026-09-07T21:30:00+0200",  1788809400L);   /* no colon        */

    CheckDate("2024-01-01T00:00:00Z", 1704067200L);
    CheckDate("1999-12-31T23:59:59Z", 946684799L);
    CheckDate("1970-01-01T00:00:01Z", 1L);
    CheckDate("2026-03-01T00:00:00Z", 1772323200L);        /* after Feb       */

    /* RFC 822 allowed two-digit years and old feeds still send them. */
    CheckDate("Mon, 07 Sep 26 21:30:00 GMT", 1788816600L);

    CheckDate("  2026-09-07T21:30:00Z  ", 1788816600L);    /* trimmed         */

    /* Unreadable is 0, not a guess: an article dated by accident sorts wrong
       forever, and a blank date column is honest. */
    CheckDate("", 0L);
    CheckDate("not a date at all", 0L);
    CheckDate("Mon, 07 Xxx 2026 21:30:00 GMT", 0L);
    CheckDate("2026-13-45T99:99:99Z", 0L);

    {
        char out[16];

        GazetteFormatDate(1788816600L, 1788816600L, out, sizeof out);
        CheckStr("a recent date shows the time", out, "Sep 07 21:30");

        /* Past about half a year the time of day stops meaning anything and
           the year starts to. */
        GazetteFormatDate(1704067200L, 1788816600L, out, sizeof out);
        CheckStr("an old date shows the year", out, "Jan 01 2024");

        GazetteFormatDate(1788816600L, 0L, out, sizeof out);
        CheckStr("with no clock, the time is shown", out, "Sep 07 21:30");

        GazetteFormatDate(0L, 1788816600L, out, sizeof out);
        CheckStr("an unknown date formats to nothing", out, "");
    }

    {
        char out[64];

        /* 2026-09-12 05:01:00 — the example the byline was written for. */
        GazetteFormatLongDate(1789189260L, out, sizeof out);
        CheckStr("the long form names the day and the month", out,
                 "Saturday, September 12, 2026 5:01 am");

        /* Noon and midnight are the two the twelve-hour clock gets wrong. */
        GazetteFormatLongDate(1789214400L, out, sizeof out);
        CheckStr("noon is 12 pm", out,
                 "Saturday, September 12, 2026 12:00 pm");

        GazetteFormatLongDate(1789171200L, out, sizeof out);
        CheckStr("midnight is 12 am", out,
                 "Saturday, September 12, 2026 12:00 am");

        /* A single-digit day of the month carries no leading zero. */
        GazetteFormatLongDate(1788816600L, out, sizeof out);
        CheckStr("a single-digit day is not padded", out,
                 "Monday, September 7, 2026 9:30 pm");

        /* Before the epoch the day number is negative, and the weekday has
           to come out of it anyway: 1969-07-20 was a Sunday. */
        GazetteFormatLongDate(-14182980L, out, sizeof out);
        CheckStr("a date before 1970 still names its weekday", out,
                 "Sunday, July 20, 1969 8:17 pm");

        GazetteFormatLongDate(0L, out, sizeof out);
        CheckStr("an unknown date formats to nothing", out, "");

        /* A buffer that cannot hold the longest form writes nothing rather
           than a date cut in half. */
        {
            char small[20];

            GazetteFormatLongDate(1789189260L, small, sizeof small);
            CheckStr("too small a buffer writes nothing", small, "");
        }
    }
}

/* ------------------------------------------------------------------ */
/* Feed parsing                                                        */
/* ------------------------------------------------------------------ */

#define kMaxCollected 32

static GazetteArticle gCollected[kMaxCollected];
static int            gCollectedCount;
static int            gCollectLimit;

static int Collect(const GazetteArticle *a, void *context)
{
    (void)context;
    if (gCollectedCount < kMaxCollected) {
        gCollected[gCollectedCount++] = *a;
    }
    if (gCollectLimit > 0 && gCollectedCount >= gCollectLimit) {
        return 0;
    }
    return 1;
}

/* Parse in chunks of `chunk` bytes, or all at once when chunk is 0. Every
   token has to survive being split, so the tests run the same documents both
   ways. */
static void ParseFeed(GazetteFeedParser *p, const char *doc, size_t chunk)
{
    size_t len = strlen(doc);
    size_t off = 0;

    gCollectedCount = 0;
    GazetteFeedParserInit(p, Collect, NULL);

    if (chunk == 0) {
        GazetteFeedParserFeed(p, doc, len);
    } else {
        while (off < len) {
            size_t n = (len - off < chunk) ? len - off : chunk;
            if (!GazetteFeedParserFeed(p, doc + off, n)) {
                break;
            }
            off += n;
        }
    }
    GazetteFeedParserFinish(p);
}

static const char kRSS[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    "<rss version=\"2.0\">\n"
    "<channel>\n"
    "  <title>Example News</title>\n"
    "  <link>https://example.com/</link>\n"
    "  <image><title>Example Logo</title><url>https://example.com/l.png</url></image>\n"
    "  <item>\n"
    "    <title>First &amp; foremost</title>\n"
    "    <link>https://example.com/1</link>\n"
    "    <pubDate>Mon, 07 Sep 2026 21:30:00 GMT</pubDate>\n"
    "    <source url=\"https://cnn.com\">CNN</source>\n"
    "  </item>\n"
    "  <item>\n"
    "    <title><![CDATA[Second <b>story</b>]]></title>\n"
    "    <link>https://example.com/2</link>\n"
    "    <pubDate>Mon, 07 Sep 2026 20:00:00 GMT</pubDate>\n"
    "  </item>\n"
    "  <!-- a comment with <item> inside it that must not become an article -->\n"
    "  <item>\n"
    "    <guid isPermaLink=\"true\">https://example.com/3</guid>\n"
    "    <title>Third</title>\n"
    "  </item>\n"
    "</channel>\n"
    "</rss>\n";

static void CheckRSSResult(const GazetteFeedParser *p, const char *how)
{
    char label[96];

    snprintf(label, sizeof label, "RSS (%s): three articles", how);
    CheckLong(label, (long)gCollectedCount, 3);
    if (gCollectedCount != 3) {
        return;
    }

    snprintf(label, sizeof label, "RSS (%s): feed title", how);
    CheckStr(label, GazetteFeedParserTitle(p), "Example News");

    snprintf(label, sizeof label, "RSS (%s): the channel's link is the site", how);
    CheckStr(label, GazetteFeedParserLink(p), "https://example.com/");

    snprintf(label, sizeof label, "RSS (%s): entities decoded in a title", how);
    CheckStr(label, gCollected[0].title, "First & foremost");

    snprintf(label, sizeof label, "RSS (%s): link", how);
    CheckStr(label, gCollected[0].link, "https://example.com/1");

    snprintf(label, sizeof label, "RSS (%s): date", how);
    CheckLong(label, gCollected[0].date, 1788816600L);

    snprintf(label, sizeof label, "RSS (%s): source", how);
    CheckStr(label, gCollected[0].source, "CNN");

    /* CDATA keeps its content verbatim, and the markup inside it is markup. */
    snprintf(label, sizeof label, "RSS (%s): CDATA with markup", how);
    CheckStr(label, gCollected[1].title, "Second story");

    /* A permalink guid stands in when there is no <link>. */
    snprintf(label, sizeof label, "RSS (%s): guid used as a link", how);
    CheckStr(label, gCollected[2].link, "https://example.com/3");

    snprintf(label, sizeof label, "RSS (%s): a missing date is 0", how);
    CheckLong(label, gCollected[2].date, 0L);
}

static const char kAtom[] =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
    "<feed xmlns=\"http://www.w3.org/2005/Atom\">\n"
    "  <title>Atom Example</title>\n"
    "  <link rel=\"self\" href=\"https://example.com/feed.atom\"/>\n"
    "  <link rel=\"alternate\" href=\"https://example.com/\"/>\n"
    "  <entry>\n"
    "    <title>Caf&#233; opens</title>\n"
    "    <link rel=\"self\" href=\"https://example.com/wrong\"/>\n"
    "    <link rel=\"alternate\" href=\"https://example.com/a\"/>\n"
    "    <published>2026-09-07T21:30:00Z</published>\n"
    "    <author><name>Jane Roe</name></author>\n"
    "  </entry>\n"
    "  <entry>\n"
    "    <title type=\"html\">Tom &amp; Jerry</title>\n"
    "    <link href=\"https://example.com/b\"/>\n"
    "    <updated>2026-09-07T20:00:00Z</updated>\n"
    "  </entry>\n"
    "</feed>\n";

static void CheckAtomResult(const GazetteFeedParser *p, const char *how)
{
    char label[96];

    snprintf(label, sizeof label, "Atom (%s): two entries", how);
    CheckLong(label, (long)gCollectedCount, 2);
    if (gCollectedCount != 2) {
        return;
    }

    snprintf(label, sizeof label, "Atom (%s): feed title", how);
    CheckStr(label, GazetteFeedParserTitle(p), "Atom Example");

    /* rel="self" is the feed itself; the site is the alternate link, and
       an entry's links are the entry's. */
    snprintf(label, sizeof label, "Atom (%s): the alternate link is the site", how);
    CheckStr(label, GazetteFeedParserLink(p), "https://example.com/");

    /* The accent is decoded to UTF-8 and then transliterated for Mac OS 9. */
    snprintf(label, sizeof label, "Atom (%s): title transliterated", how);
    CheckStr(label, gCollected[0].title, "Cafe opens");

    /* rel="self" is the feed itself; only the alternate link is the article. */
    snprintf(label, sizeof label, "Atom (%s): rel=self is skipped", how);
    CheckStr(label, gCollected[0].link, "https://example.com/a");

    snprintf(label, sizeof label, "Atom (%s): published date", how);
    CheckLong(label, gCollected[0].date, 1788816600L);

    snprintf(label, sizeof label, "Atom (%s): author name is the source", how);
    CheckStr(label, gCollected[0].source, "Jane Roe");

    snprintf(label, sizeof label, "Atom (%s): a link with no rel is the article", how);
    CheckStr(label, gCollected[1].link, "https://example.com/b");

    snprintf(label, sizeof label, "Atom (%s): updated is used when published is absent", how);
    CheckLong(label, gCollected[1].date, 1788816600L - 5400L);
}

static void TestFeedParsing(void)
{
    static GazetteFeedParser p;

    gCollectLimit = 0;

    ParseFeed(&p, kRSS, 0);
    CheckRSSResult(&p, "whole");

    /* One byte at a time splits every tag name, every entity and the CDATA
       terminator itself. If the scanner holds any state wrongly across a
       chunk boundary, this is where it shows. */
    ParseFeed(&p, kRSS, 1);
    CheckRSSResult(&p, "1-byte chunks");

    ParseFeed(&p, kRSS, 7);
    CheckRSSResult(&p, "7-byte chunks");

    ParseFeed(&p, kAtom, 0);
    CheckAtomResult(&p, "whole");
    ParseFeed(&p, kAtom, 1);
    CheckAtomResult(&p, "1-byte chunks");
    ParseFeed(&p, kAtom, 3);
    CheckAtomResult(&p, "3-byte chunks");

    /* A sink that stops early stops the parser, which is what caps a feed at
       the max-articles preference without reading the rest of it. */
    gCollectLimit = 2;
    ParseFeed(&p, kRSS, 0);
    CheckLong("a sink can stop the parse", (long)gCollectedCount, 2);
    gCollectLimit = 0;

    {
        /* A download cut mid-item: the headline was read long before the
           closing tag would have arrived, and is worth showing. */
        static const char truncated[] =
            "<rss><channel><title>T</title>"
            "<item><title>Complete</title><link>https://e/1</link></item>"
            "<item><title>Cut short</title><link>https://e/2</link>";
        GazetteFeedParser q;

        gCollectedCount = 0;
        GazetteFeedParserInit(&q, Collect, NULL);
        GazetteFeedParserFeed(&q, truncated, sizeof truncated - 1);
        CheckLong("before finishing, only the complete item is out",
                  (long)gCollectedCount, 1);
        GazetteFeedParserFinish(&q);
        CheckLong("finishing emits the interrupted one too",
                  (long)gCollectedCount, 2);
        CheckStr("with what was read of it",
                 gCollected[1].title, "Cut short");
    }

    {
        /* An item with neither title nor link is a template artefact, not an
           article. */
        static const char empty[] =
            "<rss><channel><item></item><item><title>Real</title></item></channel></rss>";
        GazetteFeedParser q;

        gCollectedCount = 0;
        GazetteFeedParserInit(&q, Collect, NULL);
        GazetteFeedParserFeed(&q, empty, sizeof empty - 1);
        GazetteFeedParserFinish(&q);
        CheckLong("an empty item is not an article", (long)gCollectedCount, 1);
        CheckStr("the real one survives", gCollected[0].title, "Real");
    }

    {
        /* Namespace prefixes are ignored rather than resolved. */
        static const char prefixed[] =
            "<atom:feed><atom:title>Prefixed</atom:title>"
            "<atom:entry><atom:title>Item</atom:title>"
            "<atom:link href=\"https://e/x\"/></atom:entry></atom:feed>";
        GazetteFeedParser q;

        gCollectedCount = 0;
        GazetteFeedParserInit(&q, Collect, NULL);
        GazetteFeedParserFeed(&q, prefixed, sizeof prefixed - 1);
        GazetteFeedParserFinish(&q);
        CheckLong("a prefixed feed parses", (long)gCollectedCount, 1);
        CheckStr("feed title", GazetteFeedParserTitle(&q), "Prefixed");
        CheckStr("item title", gCollected[0].title, "Item");
        CheckStr("item link", gCollected[0].link, "https://e/x");
    }

    {
        /* ']' inside CDATA that is not the terminator. */
        static const char brackets[] =
            "<rss><channel><item><title><![CDATA[a]b]]c]]></title>"
            "<link>https://e/1</link></item></channel></rss>";
        GazetteFeedParser q;

        gCollectedCount = 0;
        GazetteFeedParserInit(&q, Collect, NULL);
        GazetteFeedParserFeed(&q, brackets, sizeof brackets - 1);
        GazetteFeedParserFinish(&q);
        CheckLong("brackets inside CDATA parse", (long)gCollectedCount, 1);
        CheckStr("and are kept", gCollected[0].title, "a]b]]c");
    }

    {
        /*
         * A description that carries a whole escaped HTML passage, which is
         * how Google News and most publishers ship one. The escaping nests:
         * "&amp;nbsp;" is a space in the article, an "&nbsp;" after one decode
         * pass, and only a space after two. It used to reach the reader pane
         * with the reference still in it.
         */
        static const char escaped[] =
            "<rss><channel><item><title>T</title><link>https://e/1</link>"
            "<description>&lt;p&gt;Ten&amp;nbsp;degrees&lt;/p&gt;"
            "&amp;mdash;&amp;nbsp;said the mayor</description>"
            "</item></channel></rss>";
        GazetteFeedParser q;

        gCollectedCount = 0;
        GazetteFeedParserInit(&q, Collect, NULL);
        GazetteFeedParserFeed(&q, escaped, sizeof escaped - 1);
        GazetteFeedParserFinish(&q);
        CheckLong("an escaped description parses", (long)gCollectedCount, 1);
        CheckStr("nested entities are decoded and the markup taken out",
                 gCollected[0].body, "Ten degrees\n-- said the mayor");
    }

    {
        /* The same document one byte at a time: the second decode pass runs
           over the buffer the first one rewrote, so it must not depend on
           where the chunks fell. */
        static const char escaped[] =
            "<rss><channel><item><title>T</title><link>https://e/1</link>"
            "<description>&lt;b&gt;A&amp;nbsp;B&lt;/b&gt;</description>"
            "</item></channel></rss>";
        GazetteFeedParser q;

        gCollectedCount = 0;
        GazetteFeedParserInit(&q, Collect, NULL);
        ParseFeed(&q, escaped, 1);
        CheckStr("byte at a time, the same text comes out",
                 gCollected[0].body, "A B");
    }

    {
        /*
         * A description with real paragraphs in it. What comes out has to
         * keep them: the reader pane lays each one out on its own, and a
         * four-paragraph article rendered as one block is a wall of text.
         */
        static const char paras[] =
            "<rss><channel><item><title>T</title><link>https://e/1</link>"
            "<description>&lt;p&gt;First para.&lt;/p&gt;"
            "&lt;p&gt;Second para.&lt;/p&gt;"
            "&lt;ul&gt;&lt;li&gt;A point&lt;/li&gt;"
            "&lt;li&gt;Another&lt;/li&gt;&lt;/ul&gt;"
            "&lt;p&gt;Last&lt;br&gt;line.&lt;/p&gt;</description>"
            "</item></channel></rss>";
        GazetteFeedParser q;

        gCollectedCount = 0;
        GazetteFeedParserInit(&q, Collect, NULL);
        GazetteFeedParserFeed(&q, paras, sizeof paras - 1);
        GazetteFeedParserFinish(&q);
        CheckStr("paragraphs survive the pipeline", gCollected[0].body,
                 "First para.\nSecond para.\nA point\nAnother\nLast\nline.");
    }

    {
        /* Split across chunks, the breaks land in the same places. */
        static const char paras[] =
            "<rss><channel><item><title>T</title><link>https://e/1</link>"
            "<description>&lt;p&gt;One&lt;/p&gt;&lt;p&gt;Two&lt;/p&gt;"
            "</description></item></channel></rss>";
        GazetteFeedParser q;

        gCollectedCount = 0;
        GazetteFeedParserInit(&q, Collect, NULL);
        ParseFeed(&q, paras, 1);
        CheckStr("byte at a time, the same paragraphs",
                 gCollected[0].body, "One\nTwo");
    }

    {
        /* A headline is not prose and has nowhere to put a break: markup in
           one still collapses to a single line. */
        static const char titleBreak[] =
            "<rss><channel><item>"
            "<title>Storm&lt;br&gt;hits coast</title>"
            "<link>https://e/1</link></item></channel></rss>";
        GazetteFeedParser q;

        gCollectedCount = 0;
        GazetteFeedParserInit(&q, Collect, NULL);
        GazetteFeedParserFeed(&q, titleBreak, sizeof titleBreak - 1);
        GazetteFeedParserFinish(&q);
        CheckStr("a headline stays on one line",
                 gCollected[0].title, "Storm hits coast");
    }

    {
        /* An ampersand in prose is not a reference and must survive both
           passes: "AT&amp;T" is "AT&T" and stays there. */
        static const char amp[] =
            "<rss><channel><item><title>AT&amp;T and R&amp;D</title>"
            "<link>https://e/1</link></item></channel></rss>";
        GazetteFeedParser q;

        gCollectedCount = 0;
        GazetteFeedParserInit(&q, Collect, NULL);
        GazetteFeedParserFeed(&q, amp, sizeof amp - 1);
        GazetteFeedParserFinish(&q);
        CheckStr("a decoded ampersand is not decoded again",
                 gCollected[0].title, "AT&T and R&D");
    }
}

/* ------------------------------------------------------------------ */
/* Feed auto-discovery                                                 */
/* ------------------------------------------------------------------ */

static void TestDiscovery(void)
{
    static const char page[] =
        "<!DOCTYPE html>\n<html><head>\n"
        "  <title>A Site</title>\n"
        "  <link rel=\"stylesheet\" href=\"/style.css\">\n"
        "  <link rel=\"alternate\" type=\"application/rss+xml\""
        "        title=\"RSS\" href=\"/feed.xml\">\n"
        "  <link rel=\"alternate\" type=\"application/atom+xml\" href=\"/atom.xml\">\n"
        "</head><body>Hello</body></html>";
    GazetteFeedParser p;

    GazetteFeedParserInitDiscovery(&p);
    GazetteFeedParserFeed(&p, page, sizeof page - 1);
    CheckStr("the first feed link is found",
             GazetteFeedParserDiscovered(&p), "/feed.xml");

    /* Same page, one byte at a time. */
    {
        size_t i;
        GazetteFeedParserInitDiscovery(&p);
        for (i = 0; i < sizeof page - 1; i++) {
            GazetteFeedParserFeed(&p, page + i, 1);
        }
        CheckStr("and found the same way in 1-byte chunks",
                 GazetteFeedParserDiscovered(&p), "/feed.xml");
    }

    {
        static const char none[] =
            "<html><head><link rel=\"stylesheet\" href=\"/a.css\">"
            "<link rel=\"icon\" href=\"/f.ico\"></head></html>";
        GazetteFeedParser q;

        GazetteFeedParserInitDiscovery(&q);
        GazetteFeedParserFeed(&q, none, sizeof none - 1);
        CheckStr("a page with no feed discovers nothing",
                 GazetteFeedParserDiscovered(&q), "");
    }

    {
        /* Some pages omit rel="alternate"; none of the other rel values name
           a feed, so type alone is enough. */
        static const char norel[] =
            "<html><head><link type=\"application/rss+xml\" href=\"/f.rss\">"
            "</head></html>";
        GazetteFeedParser q;

        GazetteFeedParserInitDiscovery(&q);
        GazetteFeedParserFeed(&q, norel, sizeof norel - 1);
        CheckStr("a feed link with no rel is still found",
                 GazetteFeedParserDiscovered(&q), "/f.rss");
    }
}

/* ------------------------------------------------------------------ */
/* Google News                                                         */
/* ------------------------------------------------------------------ */

/* The article link decoder: NewsProxy's, as bytes. */
static void TestGoogleNewsLinks(void)
{
    char token[kGazetteGNewsTokenMax];
    char url[1024];
    static char body[kGazetteGNewsBodyMax];
    GazetteGNewsScan scan;
    size_t n;

    CheckTrue("an rss/articles link carries a token",
              GazetteGNewsArticleToken(
                  "https://news.google.com/rss/articles/CBMiabc-_123?oc=5",
                  token, sizeof token));
    CheckStr("the token, without the query", token, "CBMiabc-_123");
    CheckTrue("so does a read link",
              GazetteGNewsArticleToken("https://news.google.com/read/AU_yqLtok",
                                       token, sizeof token));
    CheckStr("its token", token, "AU_yqLtok");
    CheckLong("a story's own link is not one",
              GazetteGNewsArticleToken("https://9to5mac.com/2026/09/17/x/",
                                       token, sizeof token), 0);
    CheckLong("nor is Google's feed",
              GazetteGNewsArticleToken("https://news.google.com/rss?hl=en",
                                       token, sizeof token), 0);

    /* The two attributes off the page, however the page is chunked. */
    {
        static const char page[] =
            "<html><body><c-wiz data-p=\"x\" data-n-a-id=\"CBMi\" "
            "data-n-a-sg=\"AZ_sig-1\" data-n-a-ts=\"1758100000\" "
            "jsdata=\"y\"><p>Top stories</p></c-wiz></body></html>";
        size_t chunk, off;

        for (chunk = 1; chunk <= 7; chunk += 3) {
            int stopped = 0;

            GazetteGNewsScanInit(&scan);
            for (off = 0; off < sizeof page - 1; off += chunk) {
                size_t m = sizeof page - 1 - off;

                if (m > chunk) {
                    m = chunk;
                }
                if (!GazetteGNewsScanFeed(&scan, page + off, m)) {
                    stopped = 1;
                    break;
                }
            }
            CheckTrue("the scan stops once both are in hand", stopped);
            CheckTrue("and says so", GazetteGNewsScanDone(&scan));
            CheckStr("the signature", scan.sig, "AZ_sig-1");
            CheckStr("the timestamp", scan.ts, "1758100000");
        }
        GazetteGNewsScanInit(&scan);
        CheckTrue("a page without them is read to the end",
                  GazetteGNewsScanFeed(&scan, "<p>nothing</p>", 14));
        CheckLong("and is not done", GazetteGNewsScanDone(&scan), 0);
    }

    /* The form body: the request 68k-news worked out, form-encoded. */
    n = GazetteGNewsBuildBody("TOK", "1758100000", "SIG", body, sizeof body);
    CheckTrue("the body is built", n > 0);
    CheckTrue("it is a form field", strncmp(body, "f.req=%5B%5B%5B%22Fbv4je%22%2C%22%5B%5C%22garturlreq", 52) == 0);
    CheckTrue("it carries the token", strstr(body, "%5C%22TOK%5C%22%2C1758100000%2C%5C%22SIG%5C%22%5D%22%5D%5D%5D") != NULL);
    CheckLong("and nothing after it", (long)strlen(body), (long)n);

    /* The answer: JSON inside JSON, read as bytes. */
    {
        static const char answer[] =
            ")]}'\n\n123\n[[\"wrb.fr\",\"Fbv4je\",\"[\\\"garturlres\\\","
            "\\\"https://www.example.com/story?a\\\\u003d1\\\\u0026b\\\\u003d2\\\","
            "null,\\\"x\\\"]\",null,null,null,\"generic\"]]";

        CheckTrue("the address is found",
                  GazetteGNewsParseAnswer(answer, sizeof answer - 1, url, sizeof url));
        CheckStr("with its escapes undone", url,
                 "https://www.example.com/story?a=1&b=2");
        CheckLong("an answer without it yields nothing",
                  GazetteGNewsParseAnswer("[[\"wrb.fr\",null]]", 17, url, sizeof url), 0);
    }
}

static void TestGoogleNews(void)
{
    char url[1024];
    const GazetteCountry *us = GazetteCountryFind("US");
    const GazetteCountry *br = GazetteCountryFind("br");

    CheckStr("a country is found by code", us->code, "US");
    CheckStr("case does not matter", br->code, "BR");
    CheckStr("and it carries its parameters", br->ceid, "BR:pt");

    /* NewsProxy's DEFAULT_COUNTRY: an unknown code still produces a usable
       feed rather than a failure the user cannot interpret. */
    CheckStr("an unknown code falls back to the US",
             GazetteCountryFind("ZZ")->code, "US");
    CheckStr("and so does no code at all",
             GazetteCountryFind(NULL)->code, "US");

    GazetteGoogleNewsURL(us, kGazetteTopicTop, NULL, url, sizeof url);
    CheckStr("top stories", url,
             "https://news.google.com/rss?hl=en-US&gl=US&ceid=US:en");

    GazetteGoogleNewsURL(us, kGazetteTopicSection, "WORLD", url, sizeof url);
    CheckStr("a section", url,
             "https://news.google.com/rss/headlines/section/topic/WORLD"
             "?hl=en-US&gl=US&ceid=US:en");

    GazetteGoogleNewsURL(br, kGazetteTopicNation, NULL, url, sizeof url);
    CheckStr("national news is keyed by gl, not by a section name", url,
             "https://news.google.com/rss/headlines/section/geo/BR"
             "?hl=pt-BR&gl=BR&ceid=BR:pt");

    /* A search already opened the query string, so the parameters that follow
       it join with '&' rather than '?'. */
    GazetteGoogleNewsURL(us, kGazetteTopicSearch, "Apple Inc", url, sizeof url);
    CheckStr("a search", url,
             "https://news.google.com/rss/search?q=Apple%20Inc"
             "&hl=en-US&gl=US&ceid=US:en");

    CheckLong("a section with no name is refused",
              (long)GazetteGoogleNewsURL(us, kGazetteTopicSection, "",
                                         url, sizeof url), 0);

    {
        char small[16];
        CheckLong("a URL that will not fit is refused",
                  (long)GazetteGoogleNewsURL(us, kGazetteTopicTop, NULL,
                                             small, sizeof small), 0);
        CheckStr("and leaves the buffer empty", small, "");
    }

    {
        char enc[128];

        GazetteURLEncode("a b&c=d", enc, sizeof enc);
        CheckStr("query values are percent-encoded", enc, "a%20b%26c%3Dd");
        GazetteURLEncode("plain-Text_1.0~", enc, sizeof enc);
        CheckStr("unreserved characters are left alone", enc, "plain-Text_1.0~");
        CheckLong("an encoding that will not fit is refused",
                  (long)GazetteURLEncode("aaaaaa", enc, 4), 0);
    }

    {
        /* The generated table is data, and the thing worth checking about
           data is that it is self-consistent. */
        int i;
        int bad = 0;

        CheckTrue("the topic table is populated", kGazetteTopicCount > 100);
        CheckTrue("the group table is populated", kGazetteTopicGroupCount > 0);

        for (i = 0; i < kGazetteTopicCount; i++) {
            const GazetteTopic *t = &kGazetteTopics[i];

            if (t->group < 0 || t->group >= kGazetteTopicGroupCount ||
                t->name == NULL || t->name[0] == '\0' ||
                t->value == NULL || t->value[0] == '\0') {
                bad++;
                continue;
            }
            if (GazetteGoogleNewsURL(us, t->kind, t->value,
                                     url, sizeof url) == 0) {
                bad++;
            }
        }
        CheckLong("every topic names a group and builds a URL", (long)bad, 0);

        for (i = 0; i < kGazetteTopicGroupCount; i++) {
            if (kGazetteTopicGroups[i] == NULL ||
                kGazetteTopicGroups[i][0] == '\0') {
                bad++;
            }
        }
        CheckLong("every group has a name", (long)bad, 0);
    }
}


/* ------------------------------------------------------------------ */
/* Groups                                                              */
/* ------------------------------------------------------------------ */

/* The feed order is the sidebar order, so it is the thing to assert on: a
   comma-joined list of titles reads as the tree the user would see. */
static void CheckOrder(const char *what, const GazettePrefs *p,
                       const char *want)
{
    char got[512];
    int  i;

    got[0] = '\0';
    for (i = 0; i < p->feedCount; i++) {
        if (i > 0) {
            strcat(got, ",");
        }
        strcat(got, p->feeds[i].title);
    }
    CheckStr(what, got, want);
}

static void TestGroups(void)
{
    GazettePrefs p;

    memset(&p, 0, sizeof p);
    p.refreshMinutes = 30;
    p.maxArticles    = 100;

    /* The sidebar is one sequence, and a group stands where it was added:
       after the top-level feeds there were at the time. */
    GazettePrefsAddFeed(&p, "https://e/top", "Top", -1);
    CheckLong("a group can be added", GazettePrefsAddGroup(&p, "News"), 0);
    CheckLong("and another", GazettePrefsAddGroup(&p, "Blogs"), 1);
    CheckLong("an empty group name is refused",
              GazettePrefsAddGroup(&p, ""), -1);

    /* Duplicate names are allowed: a group name is a label, not a key. */
    CheckTrue("duplicate group names are allowed",
              GazettePrefsAddGroup(&p, "News") == 2);
    GazettePrefsRemoveGroup(&p, 2);

    GazettePrefsAddFeed(&p, "https://e/n1", "N1", 0);
    GazettePrefsAddFeed(&p, "https://e/b1", "B1", 1);
    GazettePrefsAddFeed(&p, "https://e/n2", "N2", 0);

    /* Each group's feeds together, where the group stands. The array is
       held in that order so the window, the file and a drag all read one
       sequence. */
    CheckOrder("feeds are held in sidebar order", &p, "Top,N1,N2,B1");
    CheckLong("group feed counts", GazettePrefsGroupFeedCount(&p, 0), 2);
    CheckLong("the other group", GazettePrefsGroupFeedCount(&p, 1), 1);
    CheckLong("the top level is a group too",
              GazettePrefsGroupFeedCount(&p, -1), 1);
    CheckLong("first feed in a group", GazettePrefsFirstFeedInGroup(&p, 1), 3);
    CheckLong("an empty group has none",
              GazettePrefsFirstFeedInGroup(&p, 5), -1);

    CheckTrue("a group can be renamed",
              GazettePrefsRenameGroup(&p, 1, "Weblogs"));
    CheckStr("and keeps the new name", p.groups[1].name, "Weblogs");
    CheckLong("renaming an absent group fails",
              GazettePrefsRenameGroup(&p, 9, "No"), 0);

    /* Moving a group takes its feeds with it. */
    {
        GazettePlace above = { kGazettePlaceBeforeGroup, 0 };

        CheckLong("a group can be moved", GazettePrefsMoveGroup(&p, 1, above),
                  0);
    }
    CheckOrder("its feeds move with it", &p, "Top,B1,N1,N2");
    CheckStr("and it is where it was put", p.groups[0].name, "Weblogs");
    CheckStr("the other shifted along", p.groups[1].name, "News");

    /* Removing a group must never take subscriptions with it. */
    CheckTrue("a group can be removed", GazettePrefsRemoveGroup(&p, 0));
    CheckLong("group count shrinks", p.groupCount, 1);
    CheckLong("its feeds survive", p.feedCount, 4);
    CheckOrder("moved to the top level", &p, "Top,B1,N1,N2");
    CheckLong("and are now top-level",
              p.feeds[GazettePrefsFindFeed(&p, "https://e/b1")].group, -1);
    CheckLong("the remaining group renumbered",
              p.feeds[GazettePrefsFindFeed(&p, "https://e/n1")].group, 0);

    {
        /* A drag: a feed lands at the place it was dropped on, named by what
           it is beside. */
        GazettePrefs q;
        GazettePlace place;

        memset(&q, 0, sizeof q);
        GazettePrefsAddFeed(&q, "https://e/1", "One", -1);
        GazettePrefsAddFeed(&q, "https://e/2", "Two", -1);
        GazettePrefsAddGroup(&q, "G");
        GazettePrefsAddFeed(&q, "https://e/3", "Three", 0);
        CheckOrder("before the drag", &q, "One,Two,Three");

        place.where = kGazettePlaceGroupEnd;
        place.ref   = 0;
        CheckTrue("dragging into a group works",
                  GazettePrefsMoveFeed(&q, 0, place) >= 0);
        CheckOrder("the dragged feed joins the group", &q, "Two,Three,One");
        CheckLong("and is in it",
                  q.feeds[GazettePrefsFindFeed(&q, "https://e/1")].group, 0);

        place.where = kGazettePlaceListStart;
        CheckTrue("dragging back out works",
                  GazettePrefsMoveFeed(&q, 2, place) >= 0);
        CheckOrder("and it returns to the top", &q, "One,Two,Three");
        CheckLong("at the top level",
                  q.feeds[GazettePrefsFindFeed(&q, "https://e/1")].group, -1);

        /* Reordering within the top level. */
        place.where = kGazettePlaceAfterFeed;
        place.ref   = 1;
        CheckTrue("reordering within a level works",
                  GazettePrefsMoveFeed(&q, 0, place) >= 0);
        CheckOrder("swapped", &q, "Two,One,Three");

        /* Below a group, at the top level: the sequence is not "top-level
           first" — a feed stands wherever it was put. */
        place.where = kGazettePlaceAfterGroup;
        place.ref   = 0;
        CheckTrue("a feed can go below a group",
                  GazettePrefsMoveFeed(&q, 0, place) >= 0);
        CheckOrder("and stands there", &q, "One,Three,Two");
        CheckLong("at the top level still",
                  q.feeds[GazettePrefsFindFeed(&q, "https://e/2")].group, -1);
        CheckLong("with the group standing before it", q.groups[0].after, 1);

        /* And a group can stand between two top-level feeds. */
        place.where = kGazettePlaceAfterFeed;
        place.ref   = GazettePrefsFindFeed(&q, "https://e/2");
        CheckLong("a group can go after a top-level feed",
                  GazettePrefsMoveGroup(&q, 0, place), 0);
        CheckOrder("its feed goes with it", &q, "One,Two,Three");
        CheckLong("and it stands after both feeds", q.groups[0].after, 2);

        CheckLong("moving a feed that is not there fails",
                  GazettePrefsMoveFeed(&q, 9, place), -1);
        place.where = kGazettePlaceGroupEnd;
        CheckLong("a group does not go inside a group",
                  GazettePrefsMoveGroup(&q, 0, place), -1);
    }

    {
        /* The group list is bounded too. */
        GazettePrefs q;
        char         name[32];
        int          i;
        int          added = 0;

        memset(&q, 0, sizeof q);
        for (i = 0; i < kGazetteMaxGroups + 5; i++) {
            snprintf(name, sizeof name, "Group %d", i);
            if (GazettePrefsAddGroup(&q, name) >= 0) {
                added++;
            }
        }
        CheckLong("the group list stops at its capacity",
                  q.groupCount, kGazetteMaxGroups);
        CheckLong("adds beyond capacity are refused", added, kGazetteMaxGroups);
    }
}

/* ------------------------------------------------------------------ */
/* The sidebar's rows                                                  */
/* ------------------------------------------------------------------ */

/* The rows the sidebar would draw, as a comma-joined list with group lines
   in brackets. That reads as the pane itself, which is the point: the window
   draws exactly this sequence and can only be checked by eye. */
/* The three standing views lead every sidebar, so every expectation below
   starts with them. Spelled out rather than skipped: that they are always
   there, always first and always in this order is part of what is being
   checked. */
#define SMART "<Today>,<All Unread>,<Starred>,"

static void CheckRows(const char *what, const GazettePrefs *p, const char *want)
{
    char got[512];
    int  n = GazettePrefsRowCount(p);
    int  i;

    got[0] = '\0';
    for (i = 0; i < n; i++) {
        GazetteSidebarRow row;

        if (!GazettePrefsRowAt(p, i, &row)) {
            strcat(got, i > 0 ? ",?" : "?");
            continue;
        }
        if (i > 0) {
            strcat(got, ",");
        }
        if (row.kind == kGazetteRowSmart) {
            strcat(got, "<");
            strcat(got, GazettePrefsSmartName(row.index));
            strcat(got, ">");
        } else if (row.kind == kGazetteRowGroup) {
            strcat(got, "[");
            strcat(got, p->groups[row.index].name);
            strcat(got, "]");
        } else {
            strcat(got, p->feeds[row.index].title);
        }
    }
    CheckStr(what, got, want);
}

static void TestSidebarRows(void)
{
    GazettePrefs      p;
    GazetteSidebarRow row;

    memset(&p, 0, sizeof p);
    GazettePrefsAddFeed(&p, "https://e/top", "Top", -1);
    GazettePrefsAddGroup(&p, "News");
    GazettePrefsAddGroup(&p, "Blogs");
    GazettePrefsAddFeed(&p, "https://e/n1", "N1", 0);
    GazettePrefsAddFeed(&p, "https://e/n2", "N2", 0);
    GazettePrefsAddFeed(&p, "https://e/b1", "B1", 1);

    CheckRows("the tree flattens to what is drawn", &p,
              SMART "Top,[News],N1,N2,[Blogs],B1");
    CheckLong("one row per line, after the standing views",
              GazettePrefsRowCount(&p), 6 + kGazetteSmartCount);

    CheckLong("Today is the first row",
              GazettePrefsRowForSmart(kGazetteSmartToday), 0);
    CheckLong("Starred is the third",
              GazettePrefsRowForSmart(kGazetteSmartStarred), 2);

    CheckLong("a top-level feed is its own row",
              GazettePrefsRowForFeed(&p, 0), 3);
    CheckLong("a group's line", GazettePrefsRowForGroup(&p, 0), 4);
    CheckLong("its first feed", GazettePrefsRowForFeed(&p, 1), 5);
    CheckLong("the second group's line", GazettePrefsRowForGroup(&p, 1), 7);
    CheckLong("and its feed", GazettePrefsRowForFeed(&p, 3), 8);

    /* Shutting a group hides its feeds and nothing else: the group keeps its
       own line, and everything below closes up. */
    p.groups[0].collapsed = 1;
    CheckRows("a shut group keeps its line only", &p,
              SMART "Top,[News],[Blogs],B1");
    CheckLong("the rows below it move up", GazettePrefsRowForGroup(&p, 1), 5);
    CheckLong("a feed inside it is drawn nowhere",
              GazettePrefsRowForFeed(&p, 1), -1);
    CheckLong("but its feed below still has a row",
              GazettePrefsRowForFeed(&p, 3), 6);

    p.groups[0].collapsed = 0;

    /* An empty group is a line with nothing under it, open or shut. */
    GazettePrefsAddGroup(&p, "Empty");
    CheckRows("an empty group is still a line", &p,
              SMART "Top,[News],N1,N2,[Blogs],B1,[Empty]");

    /* A top-level feed added now goes to the very end, below the groups —
       and the rows draw it there. */
    GazettePrefsAddFeed(&p, "https://e/last", "Last", -1);
    CheckRows("a top-level feed can stand below the groups", &p,
              SMART "Top,[News],N1,N2,[Blogs],B1,[Empty],Last");
    CheckLong("and has the last row", GazettePrefsRowForFeed(&p, 4),
              7 + kGazetteSmartCount);

    CheckLong("a row past the end is not a row",
              GazettePrefsRowAt(&p, 8 + kGazetteSmartCount, &row), 0);
    CheckLong("nor is a negative one", GazettePrefsRowAt(&p, -1, &row), 0);
    CheckLong("a feed that is not there has no row",
              GazettePrefsRowForFeed(&p, 99), -1);
    CheckLong("nor does a group that is not there",
              GazettePrefsRowForGroup(&p, 99), -1);

    {
        /* With no groups at all the sidebar is the feed list, unchanged from
           what Phase 3 drew. */
        GazettePrefs q;

        memset(&q, 0, sizeof q);
        GazettePrefsAddFeed(&q, "https://e/1", "One", -1);
        GazettePrefsAddFeed(&q, "https://e/2", "Two", -1);
        CheckRows("a flat list is still a flat list", &q, SMART "One,Two");
        CheckLong("row for row", GazettePrefsRowCount(&q),
                  q.feedCount + kGazetteSmartCount);
    }
}

/*
 * Hide Read Feeds sets the `hidden` flag and the rows have to close up over
 * it — which is the whole reason they are a walk rather than arithmetic over
 * the feed order.
 */
static void TestHiddenRows(void)
{
    GazettePrefs p;

    memset(&p, 0, sizeof p);
    GazettePrefsAddFeed(&p, "https://e/top", "Top", -1);
    GazettePrefsAddFeed(&p, "https://e/t2", "Top2", -1);
    GazettePrefsAddGroup(&p, "News");
    GazettePrefsAddGroup(&p, "Blogs");
    GazettePrefsAddFeed(&p, "https://e/n1", "N1", 0);
    GazettePrefsAddFeed(&p, "https://e/n2", "N2", 0);
    GazettePrefsAddFeed(&p, "https://e/b1", "B1", 1);

    CheckRows("everything shown to begin with", &p,
              SMART "Top,Top2,[News],N1,N2,[Blogs],B1");

    /* A hidden top-level feed takes no row and the ones after it move up. */
    p.feeds[0].hidden = 1;
    CheckRows("a hidden top-level feed is not a row", &p,
              SMART "Top2,[News],N1,N2,[Blogs],B1");
    CheckLong("and has no row of its own", GazettePrefsRowForFeed(&p, 0), -1);
    CheckLong("the feed after it is the first row after the views",
              GazettePrefsRowForFeed(&p, 1), 3);
    CheckLong("the first group moves up",
              GazettePrefsRowForGroup(&p, 0), 4);

    /* One inside a group, likewise, without disturbing the group's line. */
    p.feeds[2].hidden = 1;
    CheckRows("a hidden feed inside a group closes up", &p,
              SMART "Top2,[News],N2,[Blogs],B1");
    CheckLong("its sibling takes its row", GazettePrefsRowForFeed(&p, 3), 5);

    /* A hidden group takes its feeds with it, open or not. */
    p.groups[0].hidden = 1;
    CheckRows("a hidden group takes its feeds with it", &p,
              SMART "Top2,[Blogs],B1");
    CheckLong("the hidden group has no row",
              GazettePrefsRowForGroup(&p, 0), -1);
    CheckLong("nor does a feed inside it",
              GazettePrefsRowForFeed(&p, 3), -1);
    CheckLong("the group below it moves up",
              GazettePrefsRowForGroup(&p, 1), 4);

    /* Nothing hides the standing views: they are not subscriptions and there
       is no sense in which one of them has been read. */
    CheckLong("the standing views are still there",
              GazettePrefsRowForSmart(kGazetteSmartStarred), 2);

    GazettePrefsShowAll(&p);
    CheckRows("showing all puts every line back", &p,
              SMART "Top,Top2,[News],N1,N2,[Blogs],B1");
}

static void TestGroupParsing(void)
{
    static const char text[] =
        "country = BR\r"
        "feed = https://e/loose | Loose\r"
        "group = News\r"
        "feed = https://e/n1 | N1\r"
        "feed-off = https://e/n2 | N2\r"
        "group-closed = Blogs\r"
        "feed = https://e/b1 | B1\r"
        "group-end = 1\r"
        "feed = https://e/after | After\r";
    GazettePrefs p;

    CheckLong("parse reads every feed",
              GazettePrefsParse(text, sizeof text - 1, &p), 5);
    CheckLong("and every group", p.groupCount, 2);
    CheckStr("group names", p.groups[0].name, "News");
    CheckStr("and the second", p.groups[1].name, "Blogs");
    CheckLong("a closed group stays closed", p.groups[1].collapsed, 1);
    CheckLong("an open one stays open", p.groups[0].collapsed, 0);
    CheckStr("the country is read", p.country, "BR");

    /* Membership is carried by the order of the lines and nothing else. */
    CheckOrder("the tree is in sidebar order", &p, "Loose,N1,N2,B1,After");
    CheckLong("a feed before any group is top-level", p.feeds[0].group, -1);
    CheckLong("a feed after one belongs to it", p.feeds[1].group, 0);
    CheckLong("and so does the next", p.feeds[2].group, 0);
    CheckLong("until the following group", p.feeds[3].group, 1);
    CheckLong("feed-off is still disabled", p.feeds[2].enabled, 0);
    CheckLong("a feed after group-end is top-level again",
              p.feeds[4].group, -1);
    CheckLong("the groups stand after the one top-level feed before them",
              p.groups[1].after, 1);

    {
        /* And that survives being written and read back. */
        char         out[kGazettePrefsTextMax];
        size_t       n = GazettePrefsSerialize(&p, out, sizeof out);
        GazettePrefs q;

        CheckTrue("a mixed order serialises", n > 0);
        CheckTrue("with a group-end before the trailing feed",
                  strstr(out, "group-end") != NULL);
        GazettePrefsParse(out, n, &q);
        CheckOrder("and reads back in the same order", &q,
                   "Loose,N1,N2,B1,After");
        CheckLong("with the trailing feed at the top level",
                  q.feeds[4].group, -1);
    }

    {
        /* Groups declared with no feeds at all still exist, and the starter
           feed is only displaced by an actual feed line. */
        static const char groupsOnly[] = "group = Empty\r";
        GazettePrefs q;

        GazettePrefsParse(groupsOnly, sizeof groupsOnly - 1, &q);
        CheckLong("a group with no feeds survives", q.groupCount, 1);
        CheckLong("and the starter feed is untouched", q.feedCount, 1);
    }

    {
        /* A feed naming a group that never opened cannot happen through the
           file, but a hand edit can leave a group line out. */
        static const char noGroup[] = "feed = https://e/a | A\r";
        GazettePrefs q;

        GazettePrefsParse(noGroup, sizeof noGroup - 1, &q);
        CheckLong("one feed", q.feedCount, 1);
        CheckLong("at the top level", q.feeds[0].group, -1);
        CheckLong("and no groups", q.groupCount, 0);
    }
}

static void TestPrefsIterator(void)
{
    static const char text[] =
        "# a comment\r"
        "a = 1\r"
        "b: 2\r"
        "not a setting\r"
        "c =\r"
        "a = 3\r";
    size_t off = 0;
    char   key[32];
    char   value[32];

    /* Order matters here in a way it does not for the lookups: this is what
       carries the sidebar's shape. */
    CheckTrue("first setting",
              gz_prefs_next(text, sizeof text - 1, &off, key, sizeof key,
                            value, sizeof value));
    CheckStr("its key", key, "a");
    CheckStr("its value", value, "1");

    CheckTrue("second setting",
              gz_prefs_next(text, sizeof text - 1, &off, key, sizeof key,
                            value, sizeof value));
    CheckStr("':' separates too", key, "b");
    CheckStr("value", value, "2");

    /* "not a setting" has no separator and "c =" has no value; both skipped. */
    CheckTrue("third setting",
              gz_prefs_next(text, sizeof text - 1, &off, key, sizeof key,
                            value, sizeof value));
    CheckStr("a repeated key comes back again", key, "a");
    CheckStr("with its own value", value, "3");

    CheckLong("and then it ends",
              gz_prefs_next(text, sizeof text - 1, &off, key, sizeof key,
                            value, sizeof value), 0);
    CheckStr("clearing the key", key, "");
}

/* ------------------------------------------------------------------ */
/* The article-page extractor                                          */
/* ------------------------------------------------------------------ */

/* Run a page through the extractor, whole or in chunks of `chunk` bytes.
   Everything has to survive being split, including a tag name and a comment
   terminator, so the tests below run both ways. */
static const char *Extract(GazetteExtract *e, const char *page, size_t chunk)
{
    size_t len = strlen(page);
    size_t off = 0;

    GazetteExtractInit(e);
    if (chunk == 0) {
        GazetteExtractFeed(e, page, len);
    } else {
        while (off < len) {
            size_t n = (len - off < chunk) ? len - off : chunk;

            if (!GazetteExtractFeed(e, page + off, n)) {
                break;
            }
            off += n;
        }
    }
    GazetteExtractFinish(e);
    return GazetteExtractText(e);
}

static void TestExtractTags(void)
{
    char name[24];

    GazetteHtmlTagName("p", 1, name, sizeof name);
    CheckStr("a bare name", name, "p");

    GazetteHtmlTagName("/DIV", 4, name, sizeof name);
    CheckStr("a close tag, lowercased", name, "div");

    GazetteHtmlTagName("a href=\"x\"", 10, name, sizeof name);
    CheckStr("attributes are dropped", name, "a");

    GazetteHtmlTagName("br/", 3, name, sizeof name);
    CheckStr("and so is a self-closing slash", name, "br");

    GazetteHtmlTagName("!doctype html", 13, name, sizeof name);
    CheckStr("a doctype is not an element", name, "");

    {
        /* The attribute reader, which OPML leans on as hard as the extractor
           does — and OPML writes "xmlUrl" with a capital in the middle. */
        const char *value;
        size_t      valueLen;

        CheckTrue("an attribute is found",
                  GazetteHtmlAttr(" a href=\"x\"", 11, "href",
                                  &value, &valueLen));
        CheckLong("with its length", (long)valueLen, 1);

        CheckTrue("the name matches in any case",
                  GazetteHtmlAttr(" o XMLURL=\"u\"", 13, "xmlUrl",
                                  &value, &valueLen));
        CheckLong("a name must start at a word boundary",
                  GazetteHtmlAttr(" o xmlUrl=\"u\"", 13, "url",
                                  &value, &valueLen), 0);
        CheckLong("an unquoted value is not read",
                  GazetteHtmlAttr(" o href=x", 9, "href", &value, &valueLen),
                  0);
        CheckLong("an absent one is absent",
                  GazetteHtmlAttr(" o href=\"x\"", 11, "src",
                                  &value, &valueLen), 0);
    }

    CheckTrue("p ends a paragraph", GazetteHtmlIsBlockTag("p"));
    CheckTrue("so does li", GazetteHtmlIsBlockTag("li"));
    CheckLong("span does not", GazetteHtmlIsBlockTag("span"), 0);
    CheckLong("nor does an empty name", GazetteHtmlIsBlockTag(""), 0);
}

/* The pictures a page carries, and the markers that place them. */
static void TestExtractPhotos(void)
{
    static GazetteExtract e;
    const char *text;

    /* The <img>s in the article's body, each with a paragraph of its own
       in the text. The page's og:image is not wanted: it is the picture in
       the header said again, at another address. */
    text = Extract(&e,
                   "<html><head><title>T</title>"
                   "<meta property=\"og:image\" content=\"https://s.example/lead.jpg\">"
                   "</head><body><article>"
                   "<p>First.</p>"
                   "<img src=\"/pics/one.jpg\" alt=\"A caf\xc3\xa9 at dusk\">"
                   "<p>Second.</p></article></body></html>", 0);
    CheckStr("the marker takes a paragraph of its own", text,
             "First.\n\001\nSecond.");
    CheckLong("one photo, and og:image is not it", GazetteExtractPhotoCount(&e), 1);
    CheckStr("the article's photo as the page wrote it",
             GazetteExtractPhoto(&e, 0)->url, "/pics/one.jpg");
    CheckStr("its caption is transliterated", GazetteExtractPhoto(&e, 0)->alt,
             "A cafe at dusk");

    /* The hero above the first paragraph is the header's, and goes with it;
       the one after the first paragraph is the body's. */
    text = Extract(&e, "<body><article><h1>Headline</h1>"
                       "<img src=\"https://s/hero.jpg\">"
                       "<p>First.</p><img src=\"https://s/body.jpg\"><p>Second.</p>"
                       "</article></body>", 0);
    CheckLong("the hero in the header is not kept", GazetteExtractPhotoCount(&e), 1);
    CheckStr("the body's picture is", GazetteExtractPhoto(&e, 0)->url,
             "https://s/body.jpg");
    CheckStr("and its marker stands where it stood", text,
             "First.\n\001\nSecond.");

    /* The furniture: named, sized, inline, vector, or a repeat. */
    text = Extract(&e,
                   "<body><article><p>Text.</p>"
                   "<img src=\"https://s/logo.png\">"
                   "<img class=\"author-avatar\" src=\"https://s/me.jpg\">"
                   "<img src=\"https://s/px.gif\" width=\"1\" height=\"1\">"
                   "<img src=\"data:image/gif;base64,R0lGOD\">"
                   "<img src=\"https://s/chart.svg\">"
                   "<img src=\"https://s/real.jpg\">"
                   "<img src=\"https://s/real.jpg\">"
                   "<p>More.</p></article></body>", 0);
    CheckLong("only the real one is a photo, once", GazetteExtractPhotoCount(&e), 1);
    CheckStr("and only it left a marker", text, "Text.\n\001\nMore.");

    /* Lazy loading: the address is wherever the script would have found it,
       and a srcset's first candidate is the small one. */
    Extract(&e, "<body><article><p>Text.</p>"
                "<img data-src=\"https://s/lazy.jpg\" src=\"https://s/loading.gif\">"
                "<img srcset=\"https://s/a-320.jpg 320w, https://s/a-1280.jpg 1280w\">"
                "<p>More.</p></article></body>", 0);
    CheckLong("both lazy pictures are found", GazetteExtractPhotoCount(&e), 2);
    CheckStr("data-src over a loading gif", GazetteExtractPhoto(&e, 0)->url,
             "https://s/lazy.jpg");
    CheckStr("the small candidate of a srcset", GazetteExtractPhoto(&e, 1)->url,
             "https://s/a-320.jpg");

    /* The cap, and the article block starting the list over. */
    text = Extract(&e, "<body><p>Page.</p><img src=\"https://s/page.jpg\">"
                       "<article><p>One.</p>"
                       "<img src=\"https://s/1.jpg\"><img src=\"https://s/2.jpg\">"
                       "<img src=\"https://s/3.jpg\"><img src=\"https://s/4.jpg\">"
                       "<p>Two.</p></article></body>", 0);
    CheckLong("three at most", GazetteExtractPhotoCount(&e), 3);
    CheckStr("the page's picture before the article went with the page",
             GazetteExtractPhoto(&e, 0)->url, "https://s/1.jpg");
    CheckStr("three markers, no more", text, "One.\n\001\n\001\n\001\nTwo.");

    /* A rejected <img> is still an inline tag: it separates words. */
    CheckStr("a rejected image still separates words",
             Extract(&e, "<body><p>a<img src=\"https://s/logo.png\">b</p></body>", 0),
             "a b");
}

/* What a news page hangs off the end of its article, and how a table
   comes out. */
static void TestExtractTrailers(void)
{
    static GazetteExtract e;

    CheckStr("the affiliate notice and the plugs go",
             Extract(&e, "<body><article><p>The story.</p>"
                         "<p>FTC: We use income earning auto affiliate links.</p>"
                         "<p>Check out 9to5Mac on YouTube for more Apple news:</p>"
                         "<p>Follow us on Threads and Bluesky.</p>"
                         "<p>Sign up for the newsletter</p>"
                         "<p>Related: Something else.</p>"
                         "</article></body>", 0),
             "The story.");
    CheckStr("a sentence that starts like a plug but is not one stays",
             Extract(&e, "<body><p>Check out the new sensor, which ter Horst "
                         "tested against a chest strap over three runs.</p></body>", 0),
             "Check out the new sensor, which ter Horst tested against a chest "
             "strap over three runs.");
    CheckStr("the affiliate box: a heading and a list of shop links",
             Extract(&e, "<body><article><p>The story.</p>"
                         "<h4>Worth checking out on Amazon</h4><ul>"
                         "<li><a href=\"https://amzn.to/4rkQzN0\">Apple Watch</a></li>"
                         "<li><strong><a href=\"https://www.amazon.com/dp/B0F?tag=x-20\">"
                         "AirPods Pro 3</a></strong></li></ul>"
                         "</article></body>", 0),
             "The story.");
    CheckStr("but a shop link in a sentence keeps its words",
             Extract(&e, "<body><p>He tested the <a href=\"https://amzn.to/x\">"
                         "Apple Watch</a>'s step counter.</p></body>", 0),
             "He tested the Apple Watch's step counter.");
    CheckStr("so does the author box, by its class",
             Extract(&e, "<body><article><p>The story.</p>"
                         "<div class=\"author-bio\"><p>Marcus is a podcaster.</p></div>"
                         "<div class=\"post-byline\">By Marcus</div>"
                         "<aside class=\"affiliate-links\"><p>AirPods</p></aside>"
                         "</article></body>", 0),
             "The story.");
    /* A comparison table: each cell under the name of its column, and a
       break inside a cell kept on the cell's line. */
    CheckStr("a table with a header row is written out by column",
             Extract(&e, "<body><p>Compare:</p><table class=\"comparison\">"
                         "<colgroup><col></colgroup><p></p>"
                         "<tr><th>iPhone 15 Pro (2023)</th><th>&zwnj;iPhone 18 Pro&zwnj; (2026)</th></tr>"
                         "<tr><td>Titanium</td><td>Aluminum</td></tr>"
                         "<tr><td><strong>15 Pro</strong>: 6.1-inch<br /><strong>15 Pro Max</strong>: 6.7-inch</td>"
                         "<td>6.3-inch</td></tr>"
                         "</table><p>After.</p></body>", 0),
             "Compare:\niPhone 15 Pro (2023): Titanium\niPhone 18 Pro (2026): Aluminum\n"
             "iPhone 15 Pro (2023): 15 Pro: 6.1-inch / 15 Pro Max: 6.7-inch\n"
             "iPhone 18 Pro (2026): 6.3-inch\nAfter.");
    CheckStr("a table without one is rows of cells told apart",
             Extract(&e, "<body><p>Compare:</p><table>"
                         "<tr><td>6.1-inch</td><td>6.3-inch</td></tr>"
                         "<tr><th>Battery</th><td>Good</td></tr>"
                         "</table></body>", 0),
             "Compare:\n6.1-inch | 6.3-inch\nBattery | Good");
    CheckStr("MacRumors' linkback lines go",
             Extract(&e, "<body><article><p>The story.</p>"
                         "<div class=\"linkback\">Related Roundups: <a href=\"/r\">iPhone</a></div>"
                         "<div class=\"linkback\">Tag: <a href=\"/t\">Cases</a></div>"
                         "</article></body>", 0),
             "The story.");
    CheckStr("a page with no paragraphs but a body block by name",
             Extract(&e, "<body><div class=\"noticia\">"
                         "<div class=\"manchete\">Feminino: Vasco promovera encontro</div>"
                         "<div class=\"data\">Sexta-feira, 18/09/2026 - 23:34</div>"
                         "<div class=\"corpo\"><div class=\"galeria\"><table><tr><td>"
                         "<a href=\"/p.jpg\"><img src=\"/p.jpg\" alt=\"Torcida\">Torcida do Vasco</a>"
                         "</td></tr></table></div> <BR> <BR>O Vasco tera um domingo. <BR> <BR>"
                         "A acao esta prevista.</div></div></body>", 0),
             "\001\nTorcida do Vasco\nO Vasco tera um domingo.\nA acao esta prevista.");
    CheckStr("but corporate is not corpo",
             Extract(&e, "<body><p>Intro.</p><div class=\"corporate\"><p>Menu</p></div>"
                         "<p>Text.</p></body>", 0),
             "Intro.\nMenu\nText.");
    CheckStr("a space the page wrote before a quote stays",
             Extract(&e, "<body><p>the 'best' one, he said.</p></body>", 0),
             "the 'best' one, he said.");
    CheckStr("no space before the punctuation after a link",
             Extract(&e, "<body><p>Here's <a href=\"/x\">WABetaInfo</a>: "
                         "follow <a href=\"/y\">this link</a>.</p></body>", 0),
             "Here's WABetaInfo: follow this link.");
}

static void TestExtract(void)
{
    static GazetteExtract e;   /* 16 KB: too big for this stack, as in the app */

    CheckStr("a plain page", Extract(&e, "<html><body><p>Hello there.</p>"
                                         "<p>Second para.</p></body></html>", 0),
             "Hello there.\nSecond para.");

    /* The whole point: a news page is mostly not the article. */
    CheckStr("script and style go entirely",
             Extract(&e,
                     "<html><head><title>T</title>"
                     "<style>body { color: red; }</style></head><body>"
                     "<script>var a = 1; if (a < 2) { x(); }</script>"
                     "<p>The story.</p>"
                     "<script>more();</script>"
                     "<p>Continues.</p></body></html>", 0),
             "The story.\nContinues.");

    /* The bug this guards: '<' inside JavaScript is not a tag, and a scanner
       that thinks it is runs past the real </script> and eats the article. */
    CheckStr("a '<' inside a script does not swallow the page",
             Extract(&e, "<body><script>if (a < 2 && b > 1) { x(); }</script>"
                         "<p>Survives.</p></body>", 0),
             "Survives.");

    CheckStr("even inside something else being skipped",
             Extract(&e, "<html><head><script>if (a < 2) y();</script></head>"
                         "<body><p>Survives.</p></body></html>", 0),
             "Survives.");

    CheckStr("a close tag in the wrong case still ends it",
             Extract(&e, "<body><SCRIPT>a < b</SCRIPT><p>Survives.</p></body>", 0),
             "Survives.");

    CheckStr("page furniture goes with them",
             Extract(&e,
                     "<body><nav><a href=\"/\">Home</a><a href=\"/x\">News</a></nav>"
                     "<header>Masthead</header>"
                     "<p>The story.</p>"
                     "<aside>Related stories</aside>"
                     "<footer>Copyright</footer></body>", 0),
             "The story.");

    /* Nesting: the close tag of an inner one must not end the skip early. */
    CheckStr("a nested skip element unwinds properly",
             Extract(&e, "<body><nav><div><nav>x</nav></div>y</nav>"
                         "<p>Kept.</p></body>", 0),
             "Kept.");

    /* Source formatting is not paragraph structure. */
    CheckStr("newlines in the markup are just whitespace",
             Extract(&e, "<body>\n  <p>One\n     two\n  three</p>\n</body>", 0),
             "One two three");

    CheckStr("an inline tag still separates words",
             Extract(&e, "<p>a<b>b</b>c</p>", 0), "a b c");

    /* A comment may hold anything at all, '>' included, and pages park whole
       blocks of markup inside one. It joins the text either side of it rather
       than separating them, which is what a browser does with one. */
    CheckStr("a comment is skipped to its real end",
             Extract(&e, "<p>Before<!-- <p>not this</p> a > b -->After</p>", 0),
             "BeforeAfter");

    CheckStr("entities are decoded and transliterated",
             Extract(&e, "<p>Ten&nbsp;degrees&mdash;said AT&amp;T</p>", 0),
             "Ten degrees--said AT&T");

    /* Publishers sprinkle zero-width characters through prose to control line
       breaking. They are invisible where they came from and have to stay
       invisible here, rather than becoming the '?' an unmapped one gets. */
    CheckStr("zero-width characters vanish",
             Extract(&e, "<p>the &zwnj;iPhone 18 Pro&zwnj; is here</p>", 0),
             "the iPhone 18 Pro is here");

    /* The story starts at its first paragraph: the headline the page
       repeats above it, its category, its byline, all go with the header
       they are in. The reader pane already has the headline. */
    CheckStr("a headline and its body", Extract(&e,
             "<body><h1>The Headline</h1><p>The body.</p></body>", 0),
             "The body.");
    CheckStr("the header block above the first paragraph goes", Extract(&e,
             "<body><article><div><span>WhatsApp</span></div>"
             "<h1>The Headline</h1><div>Marcus Mendes | Sep 17 2026</div>"
             "<p>The body.</p><p>More.</p></article></body>", 0),
             "The body.\nMore.");
    CheckStr("a page with no paragraphs keeps everything", Extract(&e,
             "<body><div>One.</div><div>Two.</div></body>", 0),
             "One.\nTwo.");

    /*
     * The blocks a news page wraps around its article. None of them is a
     * distinct element — they are all <div> — so what says so is the class,
     * the id or the ARIA role.
     */
    CheckStr("a comment thread goes",
             Extract(&e, "<body><p>The story.</p>"
                         "<div id=\"comments\"><p>First post</p></div></body>", 0),
             "The story.");

    CheckStr("so does a more-stories rail",
             Extract(&e, "<body><div data-track=\"popular-stories\">"
                         "<p>Other thing</p></div><p>The story.</p></body>", 0),
             "The story.");

    /* Hashed class names are why the match is a substring: the readable half
       is the half that survives a CSS module's hashing. */
    CheckStr("a hashed class name still matches",
             Extract(&e, "<body><p>The story.</p>"
                         "<div class=\"comments--LTB1t961\"><p>Nope</p></div>"
                         "<div class=\"sidebar--1d3u_-lK\"><p>Nor this</p></div>"
                         "</body>", 0),
             "The story.");

    CheckStr("an ARIA landmark is taken at its word",
             Extract(&e, "<body><div role=\"complementary\"><p>Aside</p></div>"
                         "<p>The story.</p></body>", 0),
             "The story.");

    /*
     * The guard that matters most: only the values of class, id, data-track
     * and role are searched. A page whose prose is about social media, or
     * whose links point at a comments page, keeps its article.
     */
    CheckStr("prose containing a marker word is not a marker",
             Extract(&e, "<body><p>Calls on social media to share the "
                         "comment went unheeded.</p></body>", 0),
             "Calls on social media to share the comment went unheeded.");

    CheckStr("a marker in some other attribute is not one either",
             Extract(&e, "<body><div data-url=\"/2026/09/09/apple-comments/\">"
                         "<p>The story.</p></div></body>", 0),
             "The story.");

    /*
     * And the guard that keeps a mistake from being fatal: an element with no
     * close tag must never start a skip, or the scanner hunts for an </img>
     * that is never coming and the rest of the page goes with it.
     */
    CheckStr("a void element never starts a skip",
             Extract(&e, "<body><img class=\"share-icon\" src=\"x.png\">"
                         "<p>The story.</p></body>", 0),
             "The story.");

    CheckStr("an unquoted or absent value is simply not a match",
             Extract(&e, "<body><div class><p>The story.</p></div></body>", 0),
             "The story.");

    /* Furniture named by id rather than by element, a link for a screen
       reader, and text the page hides from sight. */
    CheckStr("a div called header is furniture too",
             Extract(&e, "<body><div id=\"header\"><p>Site</p></div>"
                         "<div id=\"nav\"><a href=\"/\">Home</a></div>"
                         "<p>The story.</p></body>", 0),
             "The story.");
    CheckStr("skip to main content is for nobody",
             Extract(&e, "<body><a class=\"skip-link\" href=\"#main\">"
                         "Skip to main content</a><p>The story.</p></body>", 0),
             "The story.");
    CheckStr("nor is what the page hides",
             Extract(&e, "<body><span class=\"sr-only\">Menu</span>"
                         "<p>The story.</p></body>", 0),
             "The story.");

    /*
     * A page that says where its article is: what came before the block
     * goes, and what comes after its close is never read. Said by element,
     * by ARIA role, by microdata, and by the class a CMS uses.
     */
    CheckStr("an article element is the article",
             Extract(&e, "<body><div>Menu bits</div><p>Teaser</p>"
                         "<article><p>The story.</p><p>More.</p></article>"
                         "<p>Trailing junk</p></body>", 0),
             "The story.\nMore.");
    CheckStr("so is role=main",
             Extract(&e, "<body><p>Junk</p><div role=\"main\"><p>The story.</p>"
                         "</div><p>Junk</p></body>", 0),
             "The story.");
    CheckStr("and itemprop=articleBody",
             Extract(&e, "<body><p>Junk</p><div itemprop=\"articleBody\">"
                         "<p>The story.</p></div><p>Junk</p></body>", 0),
             "The story.");
    CheckStr("and a CMS's class for it",
             Extract(&e, "<body><p>Junk</p><div class=\"entry-content\">"
                         "<p>The story.</p></div><p>Junk</p></body>", 0),
             "The story.");
    CheckStr("a nested block of the same name does not end it early",
             Extract(&e, "<body><article><p>One.</p><article><p>Two.</p>"
                         "</article><p>Three.</p></article><p>Junk</p></body>",
                     0),
             "One.\nTwo.\nThree.");
    CheckStr("furniture inside the article still goes",
             Extract(&e, "<body><article><p>The story.</p>"
                         "<div class=\"share-tools\">Share</div></article>"
                         "</body>", 0),
             "The story.");

    {
        /* Chunked the same page every way it can be split. */
        static const char page[] =
            "<html><head><style>a{b:c}</style></head><body>"
            "<!-- a comment with a > in it -->"
            "<nav>Menu</nav><h1>Title</h1>"
            "<p>First&nbsp;paragraph.</p><p>Second.</p>"
            "<div class=\"commentBlock--9f\"><p>Not this</p></div>"
            "</body></html>";
        static const char want[] = "First paragraph.\nSecond.";

        CheckStr("whole", Extract(&e, page, 0), want);
        CheckStr("one byte at a time", Extract(&e, page, 1), want);
        CheckStr("three bytes at a time", Extract(&e, page, 3), want);
        CheckStr("seven bytes at a time", Extract(&e, page, 7), want);
    }

    {
        /* A page with nothing in it must not come back as an article: the
           feed's own summary is better than an empty pane. */
        CheckLong("an empty page yields nothing",
                  (long)strlen(Extract(&e, "<html><head></head><body>"
                                           "<script>x()</script></body></html>", 0)),
                  0);
    }

    {
        /*
         * Filling the buffer stops the fetch rather than reading a 2 MB page
         * to throw most of it away. Feed says so by returning 0.
         */
        static char big[kGazetteExtractMax * 2];
        size_t i;

        for (i = 0; i < sizeof big - 1; i++) {
            big[i] = (char)('a' + (i % 26));
        }
        big[sizeof big - 1] = '\0';

        GazetteExtractInit(&e);
        CheckLong("a page bigger than the buffer stops the fetch",
                  GazetteExtractFeed(&e, big, strlen(big)), 0);
        CheckTrue("and what was read is kept",
                  GazetteExtractFinish(&e) > kGazetteExtractMin);
    }

    {
        /* An unterminated tag at the end of a truncated download must not
           leave half a tag in the text. */
        CheckStr("a download cut inside a tag",
                 Extract(&e, "<p>Kept.</p><p class=\"x", 0), "Kept.");
    }
}

/* Discovery reached the UI in Phase 4, so what it will and will not accept
   is now the difference between "paste a home page" working and not. */
static const char *DiscoverIn(GazetteFeedParser *p, const char *page)
{
    GazetteFeedParserInitDiscovery(p);
    GazetteFeedParserFeed(p, page, strlen(page));
    GazetteFeedParserFinish(p);
    return GazetteFeedParserDiscovered(p);
}

static void TestDiscoveryPaths(void)
{
    static GazetteFeedParser p;

    CheckStr("an RSS link is found", DiscoverIn(&p,
             "<html><head><link rel=\"alternate\" "
             "type=\"application/rss+xml\" href=\"/feed.xml\"></head></html>"),
             "/feed.xml");

    CheckStr("an Atom link too", DiscoverIn(&p,
             "<html><head><link type=\"application/atom+xml\" "
             "rel=\"alternate\" href=\"https://e/atom\"></head></html>"),
             "https://e/atom");

    /* Attribute order is not fixed, and rel is often left out entirely. */
    CheckStr("rel may be absent", DiscoverIn(&p,
             "<link type=\"application/rss+xml\" href=\"a.xml\">"), "a.xml");

    CheckStr("a stylesheet link is not a feed", DiscoverIn(&p,
             "<link rel=\"stylesheet\" type=\"text/css\" href=\"s.css\">"), "");

    CheckStr("nor is an icon", DiscoverIn(&p,
             "<link rel=\"icon\" href=\"/favicon.ico\">"), "");

    CheckStr("a page with no feed link says nothing", DiscoverIn(&p,
             "<html><body><p>Nothing here.</p></body></html>"), "");

    /* The first one wins: a page listing several feeds is offering its main
       one first, and asking the user to choose is a dialog nobody wants. */
    CheckStr("the first link wins", DiscoverIn(&p,
             "<link rel=\"alternate\" type=\"application/rss+xml\" href=\"1\">"
             "<link rel=\"alternate\" type=\"application/rss+xml\" href=\"2\">"),
             "1");
}

/* ------------------------------------------------------------------ */
/* OPML                                                                */
/* ------------------------------------------------------------------ */

static void TestOPML(void)
{
    static char  text[kGazetteOPMLMax];
    GazettePrefs p;
    size_t       n;

    memset(&p, 0, sizeof p);
    GazettePrefsAddFeed(&p, "https://e/loose", "Loose", -1);
    GazettePrefsAddGroup(&p, "News & Views");
    GazettePrefsAddFeed(&p, "https://e/a?x=1&y=2", "A <b>", 0);

    n = GazetteOPMLWrite(&p, text, sizeof text);
    CheckTrue("an OPML document is written", n > 0);
    CheckTrue("with the outlines in it",
              strstr(text, "xmlUrl=\"https://e/loose\"") != NULL);

    /* The one way an export fails silently is by writing a file other
       readers refuse, and an unescaped ampersand is how that happens. */
    CheckTrue("a group name is escaped",
              strstr(text, "text=\"News &amp; Views\"") != NULL);
    CheckTrue("and so is a URL",
              strstr(text, "xmlUrl=\"https://e/a?x=1&amp;y=2\"") != NULL);
    CheckTrue("and a title", strstr(text, "title=\"A &lt;b&gt;\"") != NULL);
    CheckLong("nothing raw is left", (long)(strstr(text, "& ") != NULL), 0);

    {
        /* Round trip: what was written reads back as the same tree. */
        GazettePrefs q;

        memset(&q, 0, sizeof q);
        CheckLong("both feeds come back",
                  GazetteOPMLParse(text, strlen(text), &q), 2);
        CheckLong("and the group", q.groupCount, 1);
        CheckStr("with its name unescaped", q.groups[0].name, "News & Views");
        CheckOrder("in sidebar order", &q, "Loose,A <b>");
        CheckLong("the loose one stayed loose", q.feeds[0].group, -1);
        CheckLong("and the grouped one grouped", q.feeds[1].group, 0);
        CheckStr("the URL survived escaping",
                 q.feeds[1].url, "https://e/a?x=1&y=2");

        /* Importing is additive and idempotent: the same file twice is not
           two copies of a subscription list. */
        CheckLong("a second import adds nothing",
                  GazetteOPMLParse(text, strlen(text), &q), 0);
        CheckLong("the feed count holds", q.feedCount, 2);
        CheckLong("and the group count", q.groupCount, 1);
    }

    {
        /* A file from another reader: attributes in a different order, no
           type attribute, folders written with a separate close tag. */
        static const char foreign[] =
            "<opml version=\"2.0\"><body>\n"
            "  <outline text=\"Tech\">\n"
            "    <outline xmlUrl=\"https://e/1\" text=\"One\"></outline>\n"
            "    <outline title=\"Two\" xmlUrl=\"https://e/2\"/>\n"
            "  </outline>\n"
            "  <outline text=\"Solo\" xmlUrl=\"https://e/3\"/>\n"
            "</body></opml>\n";
        GazettePrefs q;

        memset(&q, 0, sizeof q);
        CheckLong("three feeds read",
                  GazetteOPMLParse(foreign, sizeof foreign - 1, &q), 3);
        CheckLong("one folder", q.groupCount, 1);
        CheckStr("named", q.groups[0].name, "Tech");
        CheckStr("title wins over text where both are given",
                 q.feeds[GazettePrefsFindFeed(&q, "https://e/2")].title, "Two");

        /* A feed after the folder closed belongs to no folder. */
        CheckLong("the solo feed is top-level",
                  q.feeds[GazettePrefsFindFeed(&q, "https://e/3")].group, -1);
        CheckLong("the folder's two are in it",
                  q.feeds[GazettePrefsFindFeed(&q, "https://e/1")].group, 0);
    }

    {
        /* Gazette's model is one level deep. A deeper file must not lose
           subscriptions to a shape the sidebar cannot draw. */
        static const char nested[] =
            "<opml><body>"
            "<outline text=\"Outer\">"
            "<outline text=\"Inner\">"
            "<outline xmlUrl=\"https://e/deep\" text=\"Deep\"/>"
            "</outline></outline></body></opml>";
        GazettePrefs q;

        memset(&q, 0, sizeof q);
        CheckLong("the deep feed is kept",
                  GazetteOPMLParse(nested, sizeof nested - 1, &q), 1);
        CheckLong("only the outer folder is made", q.groupCount, 1);
        CheckStr("and it is the outer one", q.groups[0].name, "Outer");
        CheckLong("with the feed in it",
                  q.feeds[GazettePrefsFindFeed(&q, "https://e/deep")].group, 0);
    }

    {
        /* Rubbish in is not a crash: an unterminated tag ends the file, and
           an outline with no xmlUrl and no children is not a feed. */
        static const char broken[] = "<opml><body><outline text=\"x\"";
        GazettePrefs q;

        memset(&q, 0, sizeof q);
        CheckLong("a truncated file yields nothing",
                  GazetteOPMLParse(broken, sizeof broken - 1, &q), 0);
        CheckLong("and adds no feeds", q.feedCount, 0);
    }

    {
        /* A buffer that cannot hold the document reports failure rather than
           writing half of one. */
        char small[64];

        CheckLong("a short buffer refuses",
                  (long)GazetteOPMLWrite(&p, small, sizeof small), 0);
        CheckStr("and leaves nothing behind", small, "");
    }
}

/* ------------------------------------------------------------------ */

int main(void)
{
    TestStringHelpers();
    TestPrefsText();
    TestAsciiText();
    TestFlattenLines();
    TestContainsCI();
    TestPrefsModel();
    TestPrefsParse();
    TestPrefsRoundTrip();
    TestPrefsIterator();
    TestGroups();
    TestSidebarRows();
    TestHiddenRows();
    TestGroupParsing();
    TestOPML();
    TestHeaderBlocks();
    TestURLSplit();
    TestURLResolve();
    TestURLFormat();
    TestHTTPRequest();
    TestHTTPResponse();
    TestChunked();
    TestEntities();
    TestDates();
    TestFeedParsing();
    TestExtractTags();
    TestExtract();
    TestExtractPhotos();
    TestExtractTrailers();
    TestDiscovery();
    TestDiscoveryPaths();
    TestGoogleNews();
    TestGoogleNewsLinks();

    printf("Gazette host tests: %d checks, %d failure%s\n",
           gChecks, gFailures, gFailures == 1 ? "" : "s");

    return gFailures == 0 ? 0 : 1;
}
