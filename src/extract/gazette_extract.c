/*
 * Gazette — HTML to readable text
 * Copyright (c) 2026 brunocastello
 *
 * See gazette_extract.h.
 */

#include "extract/gazette_extract.h"

#include "feeds/gazette_feed_parse.h"   /* GazetteDecodeEntities */
#include "portable/gazette_portable.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* What Gazette knows about tags                                       */
/* ------------------------------------------------------------------ */

/*
 * The elements that end a paragraph. Whether a page is one paragraph or forty
 * is the difference between something to read and a wall of text, so these
 * become a line break and every other tag becomes a space.
 *
 * The list does not need to be the whole of HTML. An element missing from it
 * costs a paragraph break, not correctness.
 */
static const char *const kBlockTags[] = {
    "p", "br", "div", "li", "ul", "ol", "dl", "dt", "dd",
    "tr", "table", "pre", "hr",
    "h1", "h2", "h3", "h4", "h5", "h6",
    "blockquote", "section", "article", "main", "figure", "figcaption"
};

/*
 * The elements that are never prose. Everything between the open tag and its
 * close is dropped, nesting and all — which is what turns a news page from a
 * transcript of its own navigation into the article.
 *
 * script and style are the ones that matter most: without them the reader
 * pane fills with JavaScript. The rest is page furniture, and losing a little
 * of it that was worth reading costs less than keeping all of it.
 */
static const char *const kSkipTags[] = {
    "head", "nav", "header", "footer", "aside",
    "form", "noscript", "iframe", "svg", "canvas", "video", "audio",
    "select", "button", "template", "object", "map"
};

/*
 * The elements whose content is not markup. This is not a nicety: a script
 * holding "if (a < 2)" has a '<' in it that is not a tag, and a scanner that
 * treats it as one runs on looking for a '>' — swallowing the real </script>
 * and, with it, the rest of the page. HTML says the content of these ends at
 * their own close tag and nowhere else, so that is what is looked for.
 *
 * Their text is dropped either way, so they need no entry in kSkipTags: raw
 * mode is both how they are found and how they are skipped.
 */
static const char *const kRawTextTags[] = {
    "script", "style", "textarea"
};

/*
 * The blocks a news page wraps around its article: the comment thread, the
 * "more stories" rail, the newsletter box, the share buttons. None of them is
 * a distinct element — they are all <div> — and what says so is the class or
 * the id, which is the only place the page names what a box is for.
 *
 * Matched as substrings, because a modern page's class names are hashed:
 * MacRumors ships "comments--LTB1t961" and "sidebar--1d3u_-lK", and the
 * readable half is the half that survives the hashing. Only the values of
 * class, id and data-track are searched, never the whole tag, so a URL in
 * some other attribute that happens to contain one of these words cannot
 * throw the article away.
 */
static const char *const kUnwantedMarkers[] = {
    "comment", "disqus", "sidebar", "related", "popular", "trending",
    "newsletter", "subscribe", "share", "social", "promo", "sponsor",
    "advert", "recirc", "morestories", "more-stories", "readmore",
    "read-more", "breadcrumb", "pagination", "menu", "navbar", "navigation",
    "masthead", "toolbar", "cookie", "consent", "modal", "popup", "overlay",
    "footer", "widget",
    /* A page's furniture named by id rather than by element — <div
       id="header">, <div id="nav"> — is furniture just the same; the login
       box, the rating stars and the download list are a site's, not the
       article's; and "skip to main content" is a link for a screen reader
       and text for nobody, as is anything the page hides from sight. */
    "header", "login", "nav", "rating", "fivestar", "download",
    "skip", "sr-only", "screen-reader", "visually-hidden", "visuallyhidden",
    /* The article's own furniture, inside its block: who wrote it and
       when, said again in a box at the top or the bottom; the affiliate
       list; the tag cloud; the third-party recommendation rails. */
    "byline", "author", "dateline", "timestamp", "affiliate", "disclosure",
    "tags", "taglist", "outbrain", "taboola", "recommend", "signup",
    /* MacRumors' "Tag:" and "Related Roundups:" lines; the headline said
       again inside the block, by the name a CMS gives it. */
    "linkback", "headline", "manchete"
};

/*
 * Class names that say "the article's text" only when they are the whole
 * name, because as substrings they are inside too many other words:
 * "corpo" is NETVASCO's body and also "corporate"; "texto" is Portuguese
 * for text and also "context". Compared token by token.
 */
static const char *const kContentWords[] = {
    "corpo", "texto", "conteudo", "materia", "artigo", "story", "body"
};

/*
 * Paragraphs that are not the article even when they stand inside its
 * block: the affiliate-links notice, the plug for the site's other outlets,
 * the "see also" line. Matched at the start of a paragraph, after the
 * pipeline, so the comparison is against the text a reader would see.
 */
static const char *const kTrailerStarts[] = {
    "Tag:", "Tags:", "Topics:", "Filed under", "Related Roundup",
    "Related Forum", "Buyer's Guide:",
    "Worth checking out", "Shop ", "Buy now", "Best deals",
    "FTC:", "Related:", "Related Articles", "Read more:", "See also:",
    "Popular Stories", "Top Rated Comments"
};

/*
 * And the plug: "Check out <site> on YouTube for more news:", "Follow us
 * on Threads", "Sign up for our newsletter". Not the site's name, which
 * would be a rule per site, but its shape — an imperative, and either an
 * outlet named in it or a colon it ends with, leading the reader
 * somewhere else.
 */
static const char *const kPlugVerbs[] = {
    "Check out ", "Follow ", "Subscribe ", "Sign up ", "Join ", "Add ",
    "Download ", "Get the ", "Listen to ", "Watch "
};
static const char *const kPlugOutlets[] = {
    "youtube", "twitter", "facebook", "instagram", "threads", "bluesky",
    "mastodon", "telegram", "whatsapp", "tiktok", "newsletter", "podcast",
    "app store", "google news", "preferred source", "rss", "discord"
};


/*
 * The blocks a page puts its article *in*, when it says. <article> and
 * <main> say it in HTML; role="main" and itemprop="articleBody" say it for
 * a reader that cannot see; and a class or an id from this list says it the
 * way the CMSes say it. Once one opens, everything scraped before it was
 * the page and not the article, and is dropped; when it closes, so does
 * the text. A page that names no such block is read as before, furniture
 * skipped by name.
 */
static const char *const kContentMarkers[] = {
    "article-body", "articlebody", "article__body", "article-content",
    "articlecontent", "entry-content", "entrycontent", "post-content",
    "postcontent", "post-body", "postbody", "story-body", "storybody",
    "content-body", "node-content", "game-preview"
};

/* The ARIA landmarks that say, in the page's own words, that a region is not
   the article. */
static const char *const kUnwantedRoles[] = {
    "navigation", "complementary", "banner", "contentinfo", "search",
    "dialog", "menubar", "menu", "toolbar"
};

/*
 * Only these may be skipped on the strength of a class or an id. The list is
 * a whitelist rather than a blacklist for one reason: an element with no
 * close tag must never start a skip. <img class="share-icon"> would set the
 * scanner hunting for an </img> that is never coming, and the rest of the
 * page would go with it.
 */
static const char *const kContainerTags[] = {
    "div", "section", "aside", "nav", "footer", "header", "form", "main",
    "article", "ul", "ol", "li", "dl", "table", "figure", "figcaption",
    "blockquote", "p", "span", "h1", "h2", "h3", "h4", "h5", "h6", "a"
};

void GazetteHtmlTagName(const char *tag, size_t len, char *out, size_t cap)
{
    size_t start = 0;
    size_t n     = 0;

    if (out == NULL || cap == 0) {
        return;
    }
    out[0] = '\0';
    if (tag == NULL) {
        return;
    }

    if (start < len && tag[start] == '/') {
        start++;
    }
    while (start + n < len && n + 1 < cap) {
        char c = tag[start + n];

        if (c >= 'A' && c <= 'Z') {
            c = (char)(c - 'A' + 'a');
        } else if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))) {
            break;
        }
        out[n] = c;
        n++;
    }
    out[n] = '\0';
}

static int NameIn(const char *name, const char *const *list, size_t count)
{
    size_t i;

    if (name == NULL || name[0] == '\0') {
        return 0;
    }
    for (i = 0; i < count; i++) {
        if (strcmp(name, list[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

int GazetteHtmlIsBlockTag(const char *name)
{
    return NameIn(name, kBlockTags, sizeof kBlockTags / sizeof kBlockTags[0]);
}

static int IsSkipTag(const char *name)
{
    return NameIn(name, kSkipTags, sizeof kSkipTags / sizeof kSkipTags[0]);
}

static char Lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

static int IsContainerTag(const char *name)
{
    return NameIn(name, kContainerTags,
                  sizeof kContainerTags / sizeof kContainerTags[0]);
}

/* See gazette_extract.h. Being wrong here means missing an unwanted block
   rather than eating a wanted one, which is why it can afford to be strict
   about quoting and about the whitespace before the name. */
int GazetteHtmlAttr(const char *tag, size_t len, const char *name,
                    const char **value, size_t *valueLen)
{
    size_t nameLen = strlen(name);
    size_t i;

    for (i = 1; i + nameLen + 2 < len; i++) {
        size_t at;
        char   quote;

        if (tag[i - 1] != ' ' && tag[i - 1] != '\t' && tag[i - 1] != '\n' &&
            tag[i - 1] != '\r') {
            continue;
        }
        /* Both sides lowered: HTML attribute names arrive in any case, and
           an OPML file writes "xmlUrl" with a capital in the middle of it. */
        for (at = 0; at < nameLen; at++) {
            if (Lower(tag[i + at]) != Lower(name[at])) {
                break;
            }
        }
        if (at < nameLen) {
            continue;
        }

        at = i + nameLen;
        while (at < len && (tag[at] == ' ' || tag[at] == '\t')) {
            at++;
        }
        if (at >= len || tag[at] != '=') {
            continue;                   /* a different attribute, or a flag */
        }
        at++;
        while (at < len && (tag[at] == ' ' || tag[at] == '\t')) {
            at++;
        }
        if (at >= len || (tag[at] != '"' && tag[at] != '\'')) {
            continue;
        }
        quote = tag[at++];

        *value = tag + at;
        while (at < len && tag[at] != quote) {
            at++;
        }
        *valueLen = (size_t)(tag + at - *value);
        return 1;
    }
    return 0;
}

/* 1 when any of the words appears in the value, compared case-insensitively
   and as a substring — see kUnwantedMarkers for why a substring. */
static int ValueHasAny(const char *value, size_t len,
                       const char *const *words, size_t count)
{
    size_t w;

    for (w = 0; w < count; w++) {
        size_t wordLen = strlen(words[w]);
        size_t i;

        if (wordLen > len) {
            continue;
        }
        for (i = 0; i + wordLen <= len; i++) {
            size_t k;

            for (k = 0; k < wordLen; k++) {
                if (Lower(value[i + k]) != words[w][k]) {
                    break;
                }
            }
            if (k == wordLen) {
                return 1;
            }
        }
    }
    return 0;
}

/*
 * A link that is a shop's, with the site's cut in its address. On its own
 * in a list item it is the affiliate box a story ends with, and goes; in a
 * sentence it is the product's name, and stays — the caller looks at where
 * it stands.
 */
static int TagIsAffiliateLink(const char *tag, size_t len)
{
    const char *href;
    size_t      hrefLen;

    if (!GazetteHtmlAttr(tag, len, "href", &href, &hrefLen)) {
        return 0;
    }
    if (gz_contains_ci(href, hrefLen, "amzn.to/")) {
        return 1;
    }
    if (gz_contains_ci(href, hrefLen, "amazon.") &&
        gz_contains_ci(href, hrefLen, "tag=")) {
        return 1;
    }
    return 0;
}

/* 1 when any of the words is one of the value's space-separated tokens,
   compared case-insensitively and whole. */
static int ValueHasWord(const char *value, size_t len,
                        const char *const *words, size_t count)
{
    size_t i = 0;

    while (i < len) {
        size_t start, w;

        while (i < len && value[i] == ' ') {
            i++;
        }
        start = i;
        while (i < len && value[i] != ' ') {
            i++;
        }
        for (w = 0; w < count; w++) {
            size_t wordLen = strlen(words[w]);

            if (i - start == wordLen &&
                gz_strnicmp(value + start, words[w], wordLen) == 0) {
                return 1;
            }
        }
    }
    return 0;
}

static int ParagraphIsPlug(const char *s, size_t len)
{
    size_t v;
    int    verb = 0;

    for (v = 0; v < sizeof kPlugVerbs / sizeof kPlugVerbs[0]; v++) {
        if (gz_starts_ci(s, len, kPlugVerbs[v])) {
            verb = 1;
            break;
        }
    }
    if (!verb || len > 200) {
        return 0;                   /* a plug is a line, not a paragraph */
    }
    if (s[len - 1] == ':') {
        return 1;
    }
    return ValueHasAny(s, len, kPlugOutlets,
                       sizeof kPlugOutlets / sizeof kPlugOutlets[0]);
}

/* Whether this tag opens one of the blocks a page wraps around its article. */
static int TagIsUnwanted(const char *tag, size_t len, const char *name)
{
    static const char *const kNamed[] = { "class", "id", "data-track" };
    const char *value;
    size_t      valueLen;
    size_t      i;

    if (!IsContainerTag(name)) {
        return 0;
    }

    for (i = 0; i < sizeof kNamed / sizeof kNamed[0]; i++) {
        if (GazetteHtmlAttr(tag, len, kNamed[i], &value, &valueLen) &&
            ValueHasAny(value, valueLen, kUnwantedMarkers,
                        sizeof kUnwantedMarkers /
                        sizeof kUnwantedMarkers[0])) {
            return 1;
        }
    }

    if (GazetteHtmlAttr(tag, len, "role", &value, &valueLen) &&
        ValueHasAny(value, valueLen, kUnwantedRoles,
                    sizeof kUnwantedRoles / sizeof kUnwantedRoles[0])) {
        return 1;
    }
    return 0;
}

static int IsRawTextTag(const char *name)
{
    return NameIn(name, kRawTextTags,
                  sizeof kRawTextTags / sizeof kRawTextTags[0]);
}

/* Whether this tag opens the block the page says its article is in. */
static int TagIsContent(const char *tag, size_t len, const char *name)
{
    static const char *const kNamed[] = { "class", "id" };
    static const char *const kMain[]  = { "main" };
    static const char *const kBody[]  = { "articlebody" };
    const char *value;
    size_t      valueLen;
    size_t      i;

    if (strcmp(name, "article") == 0 || strcmp(name, "main") == 0) {
        return 1;
    }
    if (!IsContainerTag(name)) {
        return 0;
    }
    if (GazetteHtmlAttr(tag, len, "role", &value, &valueLen) &&
        ValueHasAny(value, valueLen, kMain, 1)) {
        return 1;
    }
    if (GazetteHtmlAttr(tag, len, "itemprop", &value, &valueLen) &&
        ValueHasAny(value, valueLen, kBody, 1)) {
        return 1;
    }
    for (i = 0; i < sizeof kNamed / sizeof kNamed[0]; i++) {
        if (GazetteHtmlAttr(tag, len, kNamed[i], &value, &valueLen) &&
            (ValueHasAny(value, valueLen, kContentMarkers,
                         sizeof kContentMarkers /
                         sizeof kContentMarkers[0]) ||
             ValueHasWord(value, valueLen, kContentWords,
                          sizeof kContentWords /
                          sizeof kContentWords[0]))) {
            return 1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* The extractor                                                       */
/* ------------------------------------------------------------------ */

enum {
    kStateText = 0,
    kStateTag,
    kStateComment,
    kStateRaw,          /* inside a script or a style, hunting "</name"   */
    kStateRawEnd        /* found it; running out the rest of the close tag */
};

void GazetteExtractInit(GazetteExtract *e)
{
    if (e == NULL) {
        return;
    }
    memset(e, 0, sizeof *e);
    e->state = kStateText;
}

const char *GazetteExtractText(const GazetteExtract *e)
{
    return (e != NULL) ? e->scratch : "";
}

int GazetteExtractPhotoCount(const GazetteExtract *e)
{
    return (e != NULL) ? e->photoCount : 0;
}

const GazettePhotoRef *GazetteExtractPhoto(const GazetteExtract *e, int i)
{
    if (e == NULL || i < 0 || i >= e->photoCount) {
        return NULL;
    }
    return &e->photos[i];
}

/* ------------------------------------------------------------------ */
/* Output                                                              */
/* ------------------------------------------------------------------ */

static void PutChar(GazetteExtract *e, char c)
{
    if (e->outLen + 1 >= sizeof e->out) {
        e->full = 1;
        return;
    }
    e->out[e->outLen++] = c;
}

/* The marks at the head of a paragraph, and the ones that open a face:
   neither wants a space after it, and a paragraph mark with nothing after
   it was a paragraph with nothing in it. */
static int IsParagraphMark(char c)
{
    return c == (char)kGazettePhotoMarker || c == (char)kGazetteMarkHeading ||
           c == (char)kGazetteMarkListItem || c == (char)kGazetteMarkQuote;
}

static int IsOpeningMark(char c)
{
    return c == (char)kGazetteMarkBoldOn || c == (char)kGazetteMarkItalicOn ||
           c == (char)kGazetteMarkLinkOn;
}

/*
 * A run of whitespace in the source is not a paragraph break — HTML is
 * formatted for the person editing it. Every whitespace character becomes a
 * space here, so the only newlines in the buffer are the ones a block element
 * put there, and the flattener downstream can trust them.
 */
static void PutText(GazetteExtract *e, char c)
{
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f') {
        /* Leading space, or a second one: the flattener would drop it, and
           not writing it leaves room for text instead. A space right after
           a paragraph's mark is a leading space too. */
        if (e->outLen == 0 || e->out[e->outLen - 1] == ' ' ||
            e->out[e->outLen - 1] == '\n' ||
            IsParagraphMark(e->out[e->outLen - 1])) {
            return;
        }
        PutChar(e, ' ');
        e->tagSpace = 0;
        return;
    }
    /* "the link ." and "the link 's": the space an inline tag put before
       its punctuation is not one a writer put there. */
    if (e->tagSpace &&
        (c == '.' || c == ',' || c == ':' || c == ';' || c == '!' ||
         c == '?' || c == ')' || c == '\'') &&
        e->outLen >= 1 && e->out[e->outLen - 1] == ' ') {
        e->outLen--;
    }
    e->tagSpace = 0;
    PutChar(e, c);
}

/* A paragraph boundary, written the way the feed parser's stripper writes
   one: replacing the space before it rather than following it, never at the
   very start, and never twice in a row. */
static void PutBreak(GazetteExtract *e)
{
    /* A line of nothing but marks — an empty heading, a list item whose
       only content was turned away, a face opened and closed on nothing —
       is no line at all. The photo marker is the one mark that is. */
    {
        size_t i = e->outLen;

        while (i > 0 && GazetteIsMark(e->out[i - 1]) &&
               e->out[i - 1] != (char)kGazettePhotoMarker) {
            i--;
        }
        if (i < e->outLen && (i == 0 || e->out[i - 1] == '\n')) {
            e->outLen = i;
        }
    }
    if (e->outLen == 0 || e->out[e->outLen - 1] == '\n') {
        return;
    }
    if (e->out[e->outLen - 1] == ' ') {
        e->outLen--;
    }
    PutChar(e, '\n');
    e->tagSpace = 0;
}

/* ------------------------------------------------------------------ */
/* Photos                                                              */
/* ------------------------------------------------------------------ */

/*
 * What an <img> is when it is not a photograph: the site's logo, the
 * author's avatar, a share button, a tracking pixel, the spinner a lazy
 * loader shows first. Matched as substrings against the class, the source
 * and the alt text, the way the unwanted blocks are.
 */
static const char *const kFurnitureWords[] = {
    "icon", "logo", "avatar", "gravatar", "badge", "pixel", "spinner",
    "loader", "loading", "placeholder", "sprite", "emoji", "button",
    "tracking", "1x1", "spacer", "blank.", "transparent", "lazy.gif"
};

/* Photos[] from the article's block on: what came before was the page's. */
static void ResetArticlePhotos(GazetteExtract *e)
{
    e->photoCount       = 0;
    e->photosBeforeBody = 0;
}

static int PhotoRoom(const GazetteExtract *e)
{
    return e->photoCount < kGazetteMaxPhotos;
}

/* A URL is one Gazette can fetch and QuickTime can draw: absolute or
   relative to the page, not inline data, not a vector. */
static int PhotoURLUsable(const char *value, size_t len)
{
    if (len == 0 || len >= kGazettePhotoURLLen) {
        return 0;
    }
    if (gz_starts_ci(value, len, "data:") || gz_starts_ci(value, len, "blob:") ||
        gz_starts_ci(value, len, "javascript:")) {
        return 0;
    }
    if (gz_contains_ci(value, len, ".svg")) {
        return 0;
    }
    return 1;
}

static int SamePhotoURL(const char *a, const char *b, size_t bLen)
{
    return strlen(a) == bLen && memcmp(a, b, bLen) == 0;
}

/* The first address in a srcset: "a.jpg 320w, b.jpg 640w" names the
   smallest first, and the smallest is the one for a modem. */
static void FirstOfSrcset(const char **value, size_t *len)
{
    size_t n = 0;

    while (n < *len && (*value)[n] == ' ') {
        n++;
    }
    *value += n;
    *len   -= n;
    n = 0;
    while (n < *len && (*value)[n] != ' ' && (*value)[n] != ',') {
        n++;
    }
    *len = n;
}

/*
 * An <img> in the article. Its address may be in src, or — on a page that
 * loads its pictures lazily — in one of the data- attributes the script
 * would have copied into src, or in a srcset with no src at all. The
 * furniture is turned away by its size and by its name, and what is left
 * takes a marker in the text at the place it stood.
 */
static int NoteImage(GazetteExtract *e)
{
    /* The lazy ones first: where a page carries both, src is the grey
       stand-in the script would have replaced. */
    static const char *const kSources[] = {
        "data-src", "data-lazy-src", "data-original", "src", "srcset",
        "data-srcset"
    };
    static const char *const kNamed[] = { "class", "alt" };
    const char *value = NULL;
    size_t      valueLen = 0;
    const char *other;
    size_t      otherLen;
    size_t      i;
    int         k;

    if (!PhotoRoom(e)) {
        return 0;
    }

    for (i = 0; i < sizeof kSources / sizeof kSources[0]; i++) {
        if (GazetteHtmlAttr(e->tag, e->tagLen, kSources[i],
                            &value, &valueLen) && valueLen > 0) {
            if (strstr(kSources[i], "srcset") != NULL) {
                FirstOfSrcset(&value, &valueLen);
            }
            if (PhotoURLUsable(value, valueLen)) {
                break;
            }
        }
        value = NULL;
    }
    if (value == NULL) {
        return 0;
    }

    /* Anything that says it is under a hundred pixels on a side is an
       icon, whatever it calls itself. */
    for (i = 0; i < 2; i++) {
        if (GazetteHtmlAttr(e->tag, e->tagLen, i == 0 ? "width" : "height",
                            &other, &otherLen)) {
            long px = gz_parse_dec(other, otherLen, 0);

            if (px > 0 && px < 100) {
                return 0;
            }
        }
    }

    if (ValueHasAny(value, valueLen, kFurnitureWords,
                    sizeof kFurnitureWords / sizeof kFurnitureWords[0])) {
        return 0;
    }
    for (i = 0; i < sizeof kNamed / sizeof kNamed[0]; i++) {
        if (GazetteHtmlAttr(e->tag, e->tagLen, kNamed[i], &other, &otherLen) &&
            ValueHasAny(other, otherLen, kFurnitureWords,
                        sizeof kFurnitureWords / sizeof kFurnitureWords[0])) {
            return 0;
        }
    }

    /* A picture the page repeats: once is enough. */
    for (k = 0; k < e->photoCount; k++) {
        if (SamePhotoURL(e->photos[k].url, value, valueLen)) {
            return 0;
        }
    }

    k = e->photoCount;
    gz_copy_n(e->photos[k].url, sizeof e->photos[k].url, value, valueLen);
    e->photos[k].alt[0] = '\0';
    if (GazetteHtmlAttr(e->tag, e->tagLen, "alt", &other, &otherLen)) {
        gz_copy_n(e->photos[k].alt, sizeof e->photos[k].alt, other, otherLen);
    }
    e->photoCount++;

    /* A paragraph of its own, holding the marker and nothing else. */
    PutBreak(e);
    PutChar(e, (char)kGazettePhotoMarker);
    PutBreak(e);
    return 1;
}

/* ------------------------------------------------------------------ */
/* Tags                                                                */
/* ------------------------------------------------------------------ */

/* The space an inline tag stands for, unless the line or a mark has just
   begun, or a space is already there. */
static void InlineSpace(GazetteExtract *e)
{
    if (e->outLen > 0 && e->out[e->outLen - 1] != ' ' &&
        e->out[e->outLen - 1] != '\n' &&
        !IsParagraphMark(e->out[e->outLen - 1]) &&
        !IsOpeningMark(e->out[e->outLen - 1])) {
        PutChar(e, ' ');
        e->tagSpace = 1;
    }
}

/* Whether nothing but marks has been written on the current line: where
   a list item's shop link stands, bold or not. */
static int AtLineStart(const GazetteExtract *e)
{
    size_t i = e->outLen;

    while (i > 0 && (IsParagraphMark(e->out[i - 1]) ||
                     IsOpeningMark(e->out[i - 1]))) {
        i--;
    }
    return i == 0 || e->out[i - 1] == '\n';
}

static void FinishTag(GazetteExtract *e)
{
    char name[24];

    /*
     * A tag too long for the buffer is one with a great many attributes, and
     * its name was read long before the overflow. What was kept still starts
     * with the name, so it is read from that and the rest is no loss.
     */
    GazetteHtmlTagName(e->tag, e->tagLen, name, sizeof name);

    /*
     * Before anything else, including the skip bookkeeping: an element whose
     * content is not markup has to be left by its own close tag, and that is
     * true whether or not something outside it is already being skipped —
     * a <script> inside <head> would otherwise run away with the page just
     * the same.
     */
    if (!e->closing && IsRawTextTag(name) &&
        !(e->tagLen > 0 && e->tag[e->tagLen - 1] == '/')) {
        size_t n = strlen(name);

        if (n + 3 <= sizeof e->rawPat) {
            e->rawPat[0] = '<';
            e->rawPat[1] = '/';
            memcpy(e->rawPat + 2, name, n);
            e->rawPat[n + 2] = '\0';
            e->rawMatch      = 0;
            e->state         = kStateRaw;

            if (e->skip[0] == '\0') {
                PutBreak(e);
            }
            return;
        }
        /* Cannot happen for the names above; fall through rather than
           silently treating a script as prose. */
    }

    if (e->skip[0] != '\0') {
        /* Inside something being dropped: the only tags that matter are the
           ones that open another of it or close this one. */
        if (strcmp(name, e->skip) == 0) {
            if (e->closing) {
                e->skipDepth--;
                if (e->skipDepth <= 0) {
                    e->skip[0]  = '\0';
                    e->skipDepth = 0;
                    /* What follows is not a continuation of what came
                       before it. */
                    PutBreak(e);
                }
            } else if (e->tagLen == 0 || e->tag[e->tagLen - 1] != '/') {
                e->skipDepth++;
            }
        }
        return;
    }

    /*
     * The article's own block, opening or closing. Opening, once: the page
     * so far was the page, so it goes, and the text starts here. Closing,
     * when the block that opened it does: the rest of the page is the rest
     * of the page, and the text is done.
     */
    if (e->focus[0] != '\0' && strcmp(name, e->focus) == 0) {
        if (e->closing) {
            e->focusDepth--;
            if (e->focusDepth <= 0) {
                e->full = 1;
                return;
            }
        } else if (e->tagLen == 0 || e->tag[e->tagLen - 1] != '/') {
            e->focusDepth++;
        }
    } else if (!e->closing && e->focus[0] == '\0' &&
               TagIsContent(e->tag, e->tagLen, name) &&
               !(e->tagLen > 0 && e->tag[e->tagLen - 1] == '/')) {
        gz_copy_n(e->focus, sizeof e->focus, name, strlen(name));
        e->focusDepth = 1;
        e->outLen     = 0;
        e->bodyStart  = 0;
        e->bodyBegun  = 0;
        ResetArticlePhotos(e);
        return;
    }

    if (!e->closing && strcmp(name, "img") == 0 && NoteImage(e)) {
        return;
    }

    /* The story starts at its first paragraph; see bodyStart. */
    if (!e->closing && !e->bodyBegun && strcmp(name, "p") == 0) {
        PutBreak(e);
        e->bodyBegun        = 1;
        e->bodyStart        = e->outLen;
        e->photosBeforeBody = e->photoCount;
    }

    /*
     * Tables. TextEdit has no grid, so a table is written out. One with a
     * header row — a comparison, which is what a news page's tables are —
     * becomes "Header: cell" paragraphs, each cell under the column it was
     * in; one without becomes rows of cells told apart by a bar. A line
     * break inside a cell is a slash, so the cell stays one line.
     */
    if (strcmp(name, "table") == 0) {
        e->inTable = e->closing ? 0 : 1;
        e->inCell  = 0;
        e->column  = 0;
        e->row     = 0;
        e->headerColumns = 0;
        PutBreak(e);
        return;
    }
    if (e->inTable && strcmp(name, "tr") == 0) {
        if (e->closing) {
            e->row++;
        }
        e->column = 0;
        e->inCell = 0;
        PutBreak(e);
        return;
    }
    if (e->inTable && (strcmp(name, "td") == 0 || strcmp(name, "th") == 0)) {
        if (e->closing) {
            if (e->inCell && e->cellIsHeader) {
                /* The header's text becomes the column's name and comes
                   out of the text: it is said again over every cell. */
                size_t from = e->cellStart;
                size_t n    = e->outLen > from ? e->outLen - from : 0;

                if (n > 0 && e->out[from + n - 1] == ' ') {
                    n--;
                }
                if (e->column < kGazetteTableColumns) {
                    gz_copy_n(e->columnName[e->column],
                              sizeof e->columnName[e->column],
                              e->out + from, n);
                    if (e->column + 1 > e->headerColumns) {
                        e->headerColumns = e->column + 1;
                    }
                }
                e->outLen = from;
            } else if (e->inCell && e->headerColumns > 0) {
                PutBreak(e);
            }
            if (e->inCell) {
                e->column++;
                e->inCell = 0;
            }
            return;
        }
        e->inCell = 1;
        /* A <th> names its column only in the first row. Further down it is
           a row's own label — "Display", "Battery" — and is written out
           like any cell. */
        e->cellIsHeader = (strcmp(name, "th") == 0 && e->row == 0);
        if (e->cellIsHeader) {
            /* nothing: its text is captured at the close */
        } else if (e->headerColumns > 0 && e->column < e->headerColumns &&
                   e->columnName[e->column][0] != '\0') {
            const char *h = e->columnName[e->column];

            PutBreak(e);
            while (*h != '\0') {
                PutText(e, *h++);
            }
            PutChar(e, ':');
            PutChar(e, ' ');
        } else if (e->outLen > 0 && e->out[e->outLen - 1] != '\n') {
            PutText(e, ' ');
            PutChar(e, '|');
            PutChar(e, ' ');
        }
        e->cellStart = e->outLen;
        return;
    }
    if (e->inTable && e->inCell && strcmp(name, "br") == 0) {
        PutText(e, ' ');
        PutChar(e, '/');
        PutChar(e, ' ');
        return;
    }

    if (!e->closing && (IsSkipTag(name) ||
                        TagIsUnwanted(e->tag, e->tagLen, name) ||
                        (strcmp(name, "a") == 0 && AtLineStart(e) &&
                         TagIsAffiliateLink(e->tag, e->tagLen)))) {
        /* "<br/>"-style self-closing: it opens nothing, so there is nothing
           to skip until. */
        if (e->tagLen > 0 && e->tag[e->tagLen - 1] == '/') {
            return;
        }
        gz_copy_n(e->skip, sizeof e->skip, name, strlen(name));
        e->skipDepth = 1;
        PutBreak(e);
        return;
    }

    if (GazetteHtmlIsBlockTag(name)) {
        PutBreak(e);
        /* What kind of paragraph opens: a heading, a list item, a
           quotation — and a paragraph inside a quotation is one too. */
        if (!e->closing) {
            if (name[0] == 'h' && name[1] >= '1' && name[1] <= '6' &&
                name[2] == '\0') {
                PutChar(e, (char)kGazetteMarkHeading);
            } else if (strcmp(name, "li") == 0) {
                PutChar(e, (char)kGazetteMarkListItem);
            } else if (strcmp(name, "blockquote") == 0) {
                e->quoteDepth++;
                PutChar(e, (char)kGazetteMarkQuote);
            } else if (e->quoteDepth > 0 && strcmp(name, "p") == 0) {
                PutChar(e, (char)kGazetteMarkQuote);
            }
        } else if (strcmp(name, "blockquote") == 0 && e->quoteDepth > 0) {
            e->quoteDepth--;
        }
        return;
    }

    /*
     * An inline tag. It still separates words — "a<b>b</b>c" is three —
     * and the ones the reader can show leave a mark either side of their
     * text: bold, italic, and a link's underline. A link is only a link
     * with an address on it.
     */
    {
        char on = 0, off = 0;

        if (strcmp(name, "b") == 0 || strcmp(name, "strong") == 0) {
            on  = (char)kGazetteMarkBoldOn;
            off = (char)kGazetteMarkBoldOff;
        } else if (strcmp(name, "i") == 0 || strcmp(name, "em") == 0) {
            on  = (char)kGazetteMarkItalicOn;
            off = (char)kGazetteMarkItalicOff;
        } else if (strcmp(name, "a") == 0) {
            const char *href;
            size_t      hrefLen;

            if (!e->closing) {
                if (GazetteHtmlAttr(e->tag, e->tagLen, "href", &href,
                                    &hrefLen) && hrefLen > 0) {
                    on = (char)kGazetteMarkLinkOn;
                    e->linkDepth++;
                }
            } else if (e->linkDepth > 0) {
                off = (char)kGazetteMarkLinkOff;
                e->linkDepth--;
            }
        }

        if (!e->closing) {
            InlineSpace(e);
            if (on != 0) {
                PutChar(e, on);
            }
        } else {
            if (off != 0) {
                PutChar(e, off);
            }
            InlineSpace(e);
        }
    }
}

/* ------------------------------------------------------------------ */
/* The scanner                                                         */
/* ------------------------------------------------------------------ */

int GazetteExtractFeed(GazetteExtract *e, const char *data, size_t len)
{
    size_t i;

    if (e == NULL || data == NULL) {
        return 0;
    }
    if (e->full) {
        return 0;
    }

    for (i = 0; i < len; i++) {
        char c = data[i];

        switch (e->state) {
            case kStateText:
                if (c == '<') {
                    e->state       = kStateTag;
                    e->tagLen      = 0;
                    e->tagOverflow = 0;
                    e->closing     = 0;
                    e->dashes      = 0;
                } else if (e->skip[0] == '\0') {
                    PutText(e, c);
                }
                break;

            case kStateTag:
                if (e->tagLen == 0 && c == '/') {
                    e->closing = 1;
                }
                /*
                 * A comment's body may hold '>' freely — and pages hide whole
                 * blocks of markup in one — so it ends at "-->" and nowhere
                 * else. Recognised as soon as "!--" is in hand, which is
                 * before any '>' could have been read.
                 */
                if (e->tagLen == 2 && e->tag[0] == '!' && e->tag[1] == '-' &&
                    c == '-') {
                    e->state  = kStateComment;
                    e->dashes = 0;
                    break;
                }
                if (c == '>') {
                    FinishTag(e);
                    /* FinishTag may have sent us into a script or a style;
                       only a tag that ended normally goes back to text. */
                    if (e->state == kStateTag) {
                        e->state = kStateText;
                    }
                    break;
                }
                if (e->tagLen < sizeof e->tag) {
                    e->tag[e->tagLen++] = c;
                } else {
                    e->tagOverflow = 1;
                }
                break;

            case kStateComment:
                if (c == '-') {
                    e->dashes++;
                } else if (c == '>' && e->dashes >= 2) {
                    e->state = kStateText;
                    e->dashes = 0;
                } else {
                    e->dashes = 0;
                }
                break;

            case kStateRaw:
                if (Lower(c) == e->rawPat[e->rawMatch]) {
                    e->rawMatch++;
                    if (e->rawPat[e->rawMatch] == '\0') {
                        e->state = kStateRawEnd;
                    }
                } else {
                    /* A failed match may itself start the next one. */
                    e->rawMatch = (c == '<') ? 1 : 0;
                }
                break;

            case kStateRawEnd:
                if (c == '>') {
                    e->state    = kStateText;
                    e->rawMatch = 0;
                    if (e->skip[0] == '\0') {
                        PutBreak(e);
                    }
                }
                break;

            default:
                e->state = kStateText;
                break;
        }

        if (e->full) {
            /* No room for more text. The rest of the page would be read and
               thrown away, and on a modem that is real time. */
            return 0;
        }
    }

    return 1;
}

/*
 * Take out the paragraphs that kTrailerStarts names, wherever they stand.
 * Runs on the finished text, one paragraph at a time, closing each gap as
 * it goes; a photo marker is a paragraph too and never matches.
 */
static size_t DropTrailers(char *s, size_t len)
{
    size_t in  = 0;
    size_t out = 0;

    while (in < len) {
        size_t end = in;
        size_t k;
        int    drop = 0;

        while (end < len && s[end] != '\n') {
            end++;
        }
        {
            /* Past the paragraph's own marks, to its words. */
            size_t at = in;

            while (at < end && GazetteIsMark(s[at])) {
                at++;
            }
            for (k = 0; k < sizeof kTrailerStarts / sizeof kTrailerStarts[0];
                 k++) {
                if (gz_starts_ci(s + at, end - at, kTrailerStarts[k])) {
                    drop = 1;
                    break;
                }
            }
            if (!drop && end > at && ParagraphIsPlug(s + at, end - at)) {
                drop = 1;
            }
        }
        if (!drop) {
            if (out > 0) {
                s[out++] = '\n';
            }
            memmove(s + out, s + in, end - in);
            out += end - in;
        }
        in = (end < len) ? end + 1 : end;
    }
    s[out] = '\0';
    return out;
}

size_t GazetteExtractFinish(GazetteExtract *e)
{
    size_t len;

    if (e == NULL) {
        return 0;
    }

    /* The page's header — headline again, byline, lead picture — goes,
       and the pictures that were in it. */
    if (e->bodyBegun && e->bodyStart > 0 && e->bodyStart <= e->outLen) {
        int i, n;

        memmove(e->out, e->out + e->bodyStart, e->outLen - e->bodyStart);
        e->outLen -= e->bodyStart;

        n = e->photosBeforeBody;
        if (n > e->photoCount) {
            n = e->photoCount;
        }
        for (i = 0; i + n < e->photoCount; i++) {
            e->photos[i] = e->photos[i + n];
        }
        e->photoCount -= n;
        e->bodyStart   = 0;
    }
    e->out[e->outLen] = '\0';

    /*
     * The same pipeline a feed summary goes through, minus the strip: the
     * tags are already gone. Entities are decoded once rather than twice,
     * because a page's text is written for a reader — an escaped "&lt;p&gt;"
     * in an article about HTML means to show those characters, and stripping
     * what a second decode revealed would eat it.
     */
    len = GazetteDecodeEntities(e->out, e->outLen);
    len = gz_utf8_to_ascii(e->out, len, e->scratch, sizeof e->scratch);
    len = gz_flatten_lines(e->scratch, len);
    len = DropTrailers(e->scratch, len);

    e->textLen = len;

    /* The captions are text too, and go the same way. The buffer is a
       caption's length, which is nothing next to the two above. */
    {
        int i;

        for (i = 0; i < e->photoCount; i++) {
            char  *alt = e->photos[i].alt;
            char   ascii[kGazettePhotoAltLen];
            size_t n   = GazetteDecodeEntities(alt, strlen(alt));

            n = gz_utf8_to_ascii(alt, n, ascii, sizeof ascii);
            n = gz_flatten_ws(ascii, n);
            gz_copy_n(alt, kGazettePhotoAltLen, ascii, n);
        }
    }
    return len;
}
