/*
 * Gazette — incremental RSS 2.0 / Atom parser and feed auto-discovery
 * Copyright (c) 2026 brunocastello
 *
 * PORTABLE: no system headers, host-tested (constraint 5).
 *
 * Incremental because the fetch hands over 4 KB at a time and a front-page
 * feed is 100-200 KB (constraint 3). The parser holds one article in progress
 * and emits it the moment its closing tag arrives, so the largest thing in
 * memory at any point is one article and one element's text — not the
 * document. Every token may be split across a chunk boundary, including in
 * the middle of a tag name or a CDATA terminator, and the state survives it.
 *
 * It is a scanner, not an XML parser: it does not validate, does not resolve
 * namespaces (a prefix is simply ignored, so <atom:link> and <link> are the
 * same element), and does not care about document structure beyond "am I
 * inside an item". Feeds in the wild are not well-formed often enough for
 * strictness to be a virtue here, and the failure mode of a strict parser —
 * no headlines at all — is worse than the failure mode of a lax one.
 *
 * Text is put through the whole Mac OS 9 pipeline on the way out: entities
 * decoded, markup stripped, transliterated to ASCII and whitespace flattened
 * (constraint 8). What the sink receives is drawable in Geneva as it stands.
 */
#ifndef GAZETTE_FEED_PARSE_H
#define GAZETTE_FEED_PARSE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    kGazetteArticleTitleLen  = 256,
    /*
     * Long, because a Google News item link is not a publisher's URL: it is
     * a news.google.com/rss/articles/CBMi... redirector with the target
     * base64'd into the path, and those run to several hundred characters.
     */
    kGazetteArticleLinkLen   = 1024,
    kGazetteArticleSourceLen = 128,
    kGazetteFeedTitleLen     = 256,

    /*
     * The summary the feed itself carries, which is what the reader pane
     * shows until Phase 4 adds fetching the article page. Most feeds write a
     * paragraph or two; 1536 bytes of transliterated ASCII is a few hundred
     * words, and truncating past that costs less than 150 articles times
     * whatever a generous limit would be.
     */
    kGazetteArticleBodyLen   = 1536,

    /*
     * One element's text, before it is decoded and stripped. Bigger than any
     * field it feeds because a description arrives as HTML and shrinks: a
     * Google News item's is 2-3 KB of anchor tags that becomes a couple of
     * hundred bytes of prose.
     */
    kGazetteCaptureMax       = 4096
};

typedef struct {
    char title[kGazetteArticleTitleLen];
    char link[kGazetteArticleLinkLen];
    char source[kGazetteArticleSourceLen];  /* publisher, when the feed says */
    char body[kGazetteArticleBodyLen];      /* the feed's own summary        */
    long date;                              /* seconds since 1970, 0 unknown */

    /*
     * Whether the article has been opened. Not something the parser knows or
     * ever sets — it is the store's, and it lives here because the store
     * holds whole articles and the cache round-trips them. A freshly parsed
     * article is unread, which memset already makes true.
     */
    int  read;

    /*
     * Which feed in the preferences it came from. The store used to hold one
     * feed's articles, where the answer was the same for all of them and did
     * not need saying; a group view holds several feeds' at once, and each
     * headline still has to know whose it is.
     */
    int  feed;
} GazetteArticle;

/*
 * Called once per complete item or entry, in document order. Return 0 to stop
 * parsing — which is what a caller does once it has as many articles as its
 * max-articles preference allows.
 */
typedef int (*GazetteArticleSink)(const GazetteArticle *article, void *context);

/* Everything the scanner needs to survive a chunk boundary. Opaque in
   practice; declared here so a caller can allocate one in a single block. */
typedef struct {
    int  state;
    int  mode;

    char tag[512];
    size_t tagLen;

    char capture[kGazetteCaptureMax];
    /*
     * Scratch for the transliteration pass. In the struct rather than on the
     * stack: CaptureFinish runs several frames down inside the fetch pump,
     * and a 4 KB local there is a real risk on a Mac OS 9 stack.
     */
    char scratch[kGazetteCaptureMax];
    size_t captureLen;
    int  capturing;             /* which field, or 0 */
    int  captureTruncated;

    int  cdHoldLen;             /* ']' seen inside CDATA, not yet emitted */
    int  commentDashes;

    int  inItem;
    int  inAuthor;
    int  inImage;               /* <image><title> is not the feed title */
    int  sawFeedTitle;

    GazetteArticle article;
    char feedTitle[kGazetteFeedTitleLen];

    char discovered[kGazetteArticleLinkLen];
    int  sawDiscovery;

    long articleCount;
    int  stopped;

    GazetteArticleSink sink;
    void *context;
} GazetteFeedParser;

/* Parse a feed document. sink may be NULL to read only the feed title. */
void GazetteFeedParserInit(GazetteFeedParser *p,
                           GazetteArticleSink sink, void *context);

/*
 * Parse an HTML page for a feed link instead — <link rel="alternate"
 * type="application/rss+xml" href="..."> and its Atom equivalent. This is the
 * "paste a site's home page and get its feed" path.
 */
void GazetteFeedParserInitDiscovery(GazetteFeedParser *p);

/* Feed one chunk. Returns 0 once the sink has asked to stop, 1 otherwise. */
int GazetteFeedParserFeed(GazetteFeedParser *p, const char *data, size_t len);

/*
 * Finish. An item whose closing tag never arrived — a truncated download — is
 * emitted here rather than discarded, since a headline that was fully read is
 * worth showing even if the document was not.
 */
void GazetteFeedParserFinish(GazetteFeedParser *p);

const char *GazetteFeedParserTitle(const GazetteFeedParser *p);
long        GazetteFeedParserArticleCount(const GazetteFeedParser *p);

/* The feed URL found by discovery, or "" if none. Relative to the page it was
   found on, so the caller resolves it against that URL. */
const char *GazetteFeedParserDiscovered(const GazetteFeedParser *p);

/* ------------------------------------------------------------------ */
/* Exposed for the host tests                                          */
/* ------------------------------------------------------------------ */

/*
 * Decode XML/HTML entities in place, producing UTF-8. Handles the five named
 * XML entities, the common HTML ones a feed's escaped markup carries, and
 * numeric references in both decimal and hex. Returns the new length.
 */
size_t GazetteDecodeEntities(char *s, size_t len);

/* Remove markup from s in place. Feeds routinely escape HTML into a title or
   description; once the entities are decoded it is markup again. */
size_t GazetteStripMarkup(char *s, size_t len);

/*
 * Parse a feed date into seconds since 1970, or 0 when it cannot be read.
 * Accepts RFC 822 as RSS uses it ("Sun, 07 Sep 2026 21:30:00 GMT", with or
 * without the day name, and with a numeric or named zone) and ISO 8601 as
 * Atom uses it ("2026-09-07T21:30:00Z", or with a +hh:mm offset).
 */
long GazetteParseDate(const char *s, size_t len);

/*
 * Format a date for the article list: "Sep 08 14:23", or "Sep 08 2024" once
 * it is more than about half a year old, since the time of day stops meaning
 * anything at that distance and the year starts to. Writes "" for 0.
 * nowSeconds is the current time, or 0 if the application does not know it —
 * in which case the time of day is always shown.
 */
size_t GazetteFormatDate(long seconds, long nowSeconds, char *out, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* GAZETTE_FEED_PARSE_H */
