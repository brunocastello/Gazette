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
    "footer", "widget"
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
    "blockquote", "p", "span", "h1", "h2", "h3", "h4", "h5", "h6"
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

/*
 * The value of one attribute out of a tag's raw text. Returns 1 and points
 * *value at it, or 0 when the tag does not carry it.
 *
 * Deliberately small-minded about HTML: the name has to be preceded by
 * whitespace so "data-track" does not match inside "x-data-track", the value
 * has to be quoted, and anything else is treated as absent. Every page that
 * matters writes attributes that way, and being wrong here means missing an
 * unwanted block, not eating a wanted one.
 */
static int FindAttr(const char *tag, size_t len, const char *name,
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
        for (at = 0; at < nameLen; at++) {
            if (Lower(tag[i + at]) != name[at]) {
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
        if (FindAttr(tag, len, kNamed[i], &value, &valueLen) &&
            ValueHasAny(value, valueLen, kUnwantedMarkers,
                        sizeof kUnwantedMarkers /
                        sizeof kUnwantedMarkers[0])) {
            return 1;
        }
    }

    if (FindAttr(tag, len, "role", &value, &valueLen) &&
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
           not writing it leaves room for text instead. */
        if (e->outLen == 0 || e->out[e->outLen - 1] == ' ' ||
            e->out[e->outLen - 1] == '\n') {
            return;
        }
        PutChar(e, ' ');
        return;
    }
    PutChar(e, c);
}

/* A paragraph boundary, written the way the feed parser's stripper writes
   one: replacing the space before it rather than following it, never at the
   very start, and never twice in a row. */
static void PutBreak(GazetteExtract *e)
{
    if (e->outLen == 0 || e->out[e->outLen - 1] == '\n') {
        return;
    }
    if (e->out[e->outLen - 1] == ' ') {
        e->outLen--;
    }
    PutChar(e, '\n');
}

/* ------------------------------------------------------------------ */
/* Tags                                                                */
/* ------------------------------------------------------------------ */

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

    if (!e->closing && (IsSkipTag(name) ||
                        TagIsUnwanted(e->tag, e->tagLen, name))) {
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
    } else if (e->outLen > 0 && e->out[e->outLen - 1] != ' ' &&
               e->out[e->outLen - 1] != '\n') {
        /* An inline tag still separates words: "a<b>b</b>c" is three. */
        PutChar(e, ' ');
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

size_t GazetteExtractFinish(GazetteExtract *e)
{
    size_t len;

    if (e == NULL) {
        return 0;
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

    e->textLen = len;
    return len;
}
