/*
 * Gazette — Google News country and topic maps, and the URLs they build
 * Copyright (c) 2026 brunocastello
 *
 * PORTABLE: no system headers, host-tested. Adapted from NewsProxy, which
 * reverse-engineered this set against the original Newsstand 1.1 client and
 * live Google News.
 *
 * Google News has no documented API. What it does have is an RSS endpoint
 * that takes three parameters — hl (interface language), gl (country) and
 * ceid ("country:language") — in four shapes:
 *
 *   /rss?...                                 top stories
 *   /rss/headlines/section/topic/WORLD?...   one of Google's own sections
 *   /rss/headlines/section/geo/US?...        that country's national news
 *   /rss/search?q=...&...                    a saved search
 *
 * The curated topics Newsstand offered are a mixture of the last three, which
 * is why a topic carries a kind alongside its value.
 */
#ifndef GAZETTE_GOOGLENEWS_H
#define GAZETTE_GOOGLENEWS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Countries                                                           */
/* ------------------------------------------------------------------ */

typedef struct {
    const char *code;       /* "US", "GB", ... — what prefs stores       */
    const char *name;       /* "United States" — what the sidebar shows  */
    const char *hl;         /* interface language, "en-US"               */
    const char *gl;         /* country, "US"                             */
    const char *ceid;       /* "US:en"                                   */
} GazetteCountry;

extern const GazetteCountry kGazetteCountries[];
extern const int            kGazetteCountryCount;

/* Look a country up by code, case-insensitively. Returns the United States
   entry when the code is unknown or NULL, so a caller always has something
   to build a URL from — which is what NewsProxy's DEFAULT_COUNTRY does. */
const GazetteCountry *GazetteCountryFind(const char *code);

/* ------------------------------------------------------------------ */
/* Topics                                                              */
/* ------------------------------------------------------------------ */

typedef enum {
    kGazetteTopicSection = 0,   /* one of Google's own section names */
    kGazetteTopicSearch,        /* a saved search query              */
    kGazetteTopicNation,        /* that country's national news      */
    kGazetteTopicTop            /* top stories                       */
} GazetteTopicKind;

typedef struct {
    int              group;     /* index into kGazetteTopicGroups */
    const char      *name;      /* display name                   */
    GazetteTopicKind kind;
    const char      *value;     /* section name or search query   */
} GazetteTopic;

extern const char *const kGazetteTopicGroups[];
extern const int         kGazetteTopicGroupCount;
extern const GazetteTopic kGazetteTopics[];
extern const int          kGazetteTopicCount;

/* ------------------------------------------------------------------ */
/* URL building                                                        */
/* ------------------------------------------------------------------ */

/*
 * Build the RSS URL for a topic in a country. kind selects the shape; value
 * is the section name or the search query, and is ignored for kGazetteTopicTop
 * and kGazetteTopicNation. Returns the length written, or 0 if it would not
 * fit in cap.
 */
size_t GazetteGoogleNewsURL(const GazetteCountry *country,
                            GazetteTopicKind kind, const char *value,
                            char *out, size_t cap);

/*
 * Percent-encode a query-string value. Everything outside the RFC 3986
 * unreserved set is escaped, spaces included — Google News accepts %20 and
 * this avoids the '+'-means-space ambiguity entirely. Returns the length
 * written, or 0 if it would not fit.
 */
size_t GazetteURLEncode(const char *src, char *out, size_t cap);

/* ------------------------------------------------------------------ */
/* Article links                                                       */
/*                                                                     */
/* An item in a Google News feed does not link to the story. It links  */
/* to news.google.com/rss/articles/<token>, a page that is a script    */
/* which redirects a browser, and to Gazette is a page of headlines.   */
/* The story's own address is had the way NewsProxy (and 68k-news      */
/* before it) has it: read two attributes off that page, POST them     */
/* with the token to Google's batchexecute endpoint, and read the      */
/* address out of the answer. Three round trips, all on Google, before */
/* the article's own fetch — which is why it is done only for an       */
/* article somebody opened.                                            */
/* ------------------------------------------------------------------ */

enum {
    kGazetteGNewsTokenMax = 1024,
    kGazetteGNewsSigMax   = 256,
    kGazetteGNewsTsMax    = 32,
    kGazetteGNewsBodyMax  = 3072
};

/* Whether this is such a link; when it is, the token out of it. */
int GazetteGNewsArticleToken(const char *url, char *token, size_t cap);

/* A streaming scan of the page for data-n-a-sg and data-n-a-ts. Feed it
   the body as it arrives; Feed returns 0 once both are in hand, which is
   the fetch's signal to stop reading. */
typedef struct {
    char   sig[kGazetteGNewsSigMax];
    char   ts[kGazetteGNewsTsMax];
    size_t sigLen, tsLen;
    int    haveSig, haveTs;
    size_t matchSig, matchTs;   /* how much of each attribute name matched */
    int    inSig, inTs;         /* copying the value */
} GazetteGNewsScan;

void GazetteGNewsScanInit(GazetteGNewsScan *s);
int  GazetteGNewsScanFeed(GazetteGNewsScan *s, const char *data, size_t len);
int  GazetteGNewsScanDone(const GazetteGNewsScan *s);

/* The form body batchexecute wants, from the token and the two attributes.
   Returns the length, or 0 if it would not fit. */
size_t GazetteGNewsBuildBody(const char *token, const char *ts,
                             const char *sig, char *out, size_t cap);

/* The story's address out of batchexecute's answer, or 0 when it is not
   there. resp need not be the whole answer: the address is near its top. */
int GazetteGNewsParseAnswer(const char *resp, size_t len,
                            char *url, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* GAZETTE_GOOGLENEWS_H */
