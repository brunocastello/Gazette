/*
 * Gazette — HTML to readable text
 * Copyright (c) 2026 brunocastello
 *
 * PORTABLE: no system headers, host-tested (constraint 5).
 *
 * Two things live here. The small one is what Gazette knows about HTML tags —
 * an element's name, and whether it ends a paragraph — which the feed parser
 * borrows for the markup inside a description, so there is one list of block
 * elements in the application rather than two that drift.
 *
 * The large one is the extractor: the article page itself, turned into
 * something the reader pane can lay out. It is incremental for the same
 * reason the feed parser is — the fetch hands over 4 KB at a time and a news
 * page is hundreds of KB — and every token may be split across a chunk
 * boundary, including in the middle of a tag name or a comment terminator.
 *
 * It is not a readability engine and is not trying to be (AGENT.md scopes
 * that out). It throws away the elements that are never prose — script,
 * style, navigation, the page furniture — turns block elements into paragraph
 * breaks, and puts what is left through the same decode / transliterate /
 * flatten pipeline the feed summaries go through. What comes out of a news
 * page is the article plus some of the page's own chatter around it, which is
 * a great deal better than the alternative and costs no judgement about which
 * <div> is the interesting one.
 */
#ifndef GAZETTE_EXTRACT_H
#define GAZETTE_EXTRACT_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* What Gazette knows about tags                                       */
/* ------------------------------------------------------------------ */

/*
 * The element name out of a tag's raw inner text — everything between the '<'
 * and the '>' — lowercased, with any leading '/' and all attributes dropped.
 * Writes "" for a comment, a doctype, or anything else that is not an
 * element. cap of 24 holds every name either caller cares about.
 */
void GazetteHtmlTagName(const char *tag, size_t len, char *out, size_t cap);

/* 1 when an element ends a paragraph: p, div, li, br, the headings and the
   rest of the block-level list a page or a feed summary actually uses. */
int GazetteHtmlIsBlockTag(const char *name);

/*
 * The value of one attribute out of a tag's raw inner text. Returns 1 and
 * points *value into the tag, with *valueLen its length; 0 when the tag does
 * not carry it.
 *
 * The name is matched without regard to case — HTML writes attributes in any
 * case and OPML writes "xmlUrl" with a capital in the middle of it. Otherwise
 * deliberately small-minded: the name must be preceded by whitespace, so
 * "url" does not match inside "xmlUrl", and the value must be quoted. Every
 * generator that matters writes attributes that way.
 */
int GazetteHtmlAttr(const char *tag, size_t len, const char *name,
                    const char **value, size_t *valueLen);

/* ------------------------------------------------------------------ */
/* The extractor                                                       */
/* ------------------------------------------------------------------ */

enum {
    /*
     * The most text Gazette keeps from one article page. About 2600 words,
     * which covers a news story comfortably and truncates a long feature —
     * the right way round, since the alternative is letting a page decide how
     * much of an 8 MB partition it gets. The fetch is abandoned the moment
     * this fills, so a 2 MB page is not downloaded to be thrown away.
     */
    kGazetteExtractMax = 16384,

    /* Below this, the page yielded nothing worth replacing the feed's own
       summary with — a paywall stub, a cookie wall, a redirect notice. */
    kGazetteExtractMin = 200,

    /*
     * What a page says about itself, for when its body says nothing: the
     * description in its head. A page built by JavaScript ships an empty
     * body and a full description — a Mastodon post is the whole post,
     * a docs site a summary — and that is the article for a reader with
     * no JavaScript. Kept at less than the text, and asked for less: a
     * description is a paragraph, and one a sentence long is still it.
     */
    kGazetteDescriptionMax = 1024,
    kGazetteDescriptionMin = 80,

    /*
     * How much a page's header can come to. The text before the article's
     * first paragraph — the headline said again, a category, a byline, a
     * caption — is dropped as the header it is; but a page whose article is
     * not written in paragraphs would lose the article that way, so above
     * this it is not a header and stays. Measured: news headers run to
     * about 300, and the pages that write in <div>s start at about 3000.
     */
    kGazetteHeaderMax = 800,

    /*
     * The photos. Three is a news story's worth — the lead picture and a
     * couple more — and it is also what a modem and an 8 MB partition can
     * be asked to carry for an article somebody may only glance at.
     */
    kGazetteMaxPhotos   = 10,     /* Preferences chooses 1..10; 3 by default */
    kGazettePhotoURLLen = 512,
    kGazettePhotoAltLen = 160,

    /*
     * Where a photo stands in the text. A paragraph made of this one byte
     * marks the place an <img> stood in the article: the k-th marker is the
     * k-th photo. It is below every printable character and
     * survives the pipeline untouched — the transliterator passes ASCII
     * through and the flattener knows only whitespace — and the reader
     * pane turns it into the space the picture is drawn in.
     */
    kGazettePhotoMarker = 1,

    /*
     * The marks that carry what little of the markup the reader can show:
     * the bytes below a space that no text uses, the way the photo marker
     * is. Three stand at the head of a paragraph and say what kind it is;
     * the rest come in pairs and switch a face on and off in the middle of
     * one. None is whitespace to the flattener, and all pass through the
     * transliterator, so they reach the reader pane as written — which
     * takes them out again and styles the text between them.
     */
    kGazetteMarkHeading   = 2,      /* the paragraph is a heading: bold */
    kGazetteMarkListItem  = 3,      /* a list item: a bullet before it */
    kGazetteMarkQuote     = 4,      /* a quotation: italic */
    kGazetteMarkBoldOn    = 5,
    kGazetteMarkBoldOff   = 6,
    kGazetteMarkItalicOn  = 14,
    kGazetteMarkItalicOff = 15,
    kGazetteMarkLinkOn    = 16,     /* underlined */
    kGazetteMarkLinkOff   = 17,

    /* Columns a table's header row is remembered for; see FinishTag. */
    kGazetteTableColumns = 4
};

/* Whether a byte is one of the marks above, the photo marker included. */
#define GazetteIsMark(c) ((unsigned char)(c) < 0x20 && (c) != '\n' && \
                          (c) != '\r' && (c) != '\t')

/* One picture the page carries: where it is, as the page wrote it (resolved
   against the page's own address by whoever fetches it), and what the page
   said it showed. */
typedef struct {
    char url[kGazettePhotoURLLen];
    char alt[kGazettePhotoAltLen];
} GazettePhotoRef;

typedef struct {
    int    state;
    int    closing;                 /* the tag being read starts with '/' */

    /*
     * The tag as read, attributes and all — not just its name, because which
     * block a <div> opens is written in its class and its id and nowhere
     * else. 1 KB covers the tags that carry a marker, and a <meta> whose
     * content is a whole description; a marker further into one than that
     * is missed, which costs an unwanted block and nothing worse.
     */
    char   tag[1024];
    size_t tagLen;
    int    tagOverflow;             /* a tag longer than tag[] can hold */

    char   skip[24];                /* element being skipped, "" when none */
    int    skipDepth;
    int    skipSoft;                /* skipped for its class, not its name */

    /* The page's description of itself, as its head wrote it; see
       GazetteExtractUsable. */
    char   description[kGazetteDescriptionMax];
    int    fellBack;                /* the text is the description */

    /* The element the page says its article is in, once one has opened:
       what came before it is thrown away, and its close ends the text. */
    char   focus[24];
    int    focusDepth;

    int    dashes;                  /* '-' run seen while inside a comment */

    /* "</script" and how much of it has matched, while inside an element
       whose content is not markup. */
    char   rawPat[16];
    size_t rawMatch;

    /*
     * The text as it is scraped, with entities still in it, and the scratch
     * the transliteration pass writes into. Both are in the struct rather
     * than on the stack: this runs several frames inside the fetch pump, and
     * 8 KB of locals there is not something a Mac OS 9 stack should be asked
     * for.
     */
    char   out[kGazetteExtractMax];
    char   scratch[kGazetteExtractMax];
    size_t outLen;
    size_t textLen;                 /* the finished text, after Finish */
    int    full;

    /*
     * The pictures: the <img>s met in the article's body, in order, each
     * with a marker in the text. A page that names its article block starts
     * the list over when the block opens, the way the text starts over —
     * what came before was the page — and the ones met before the body's
     * first paragraph go with the header they were in; see bodyStart.
     */
    GazettePhotoRef photos[kGazetteMaxPhotos];
    int    photoCount;

    /*
     * Where the article's own text begins: the offset of its first <p>. A
     * news page opens its article block with the headline again, a
     * category, a byline, the lead picture, and the story starts at the
     * first paragraph. Everything before that is dropped at Finish, photos
     * included; a page with no <p> at all keeps the lot.
     */
    size_t bodyStart;
    int    bodyBegun;
    int    bodyMarked;              /* set by a body block's name, not a <p> */
    int    photosBeforeBody;

    /* The last byte written was a space an inline tag put there, not one
       the page wrote: punctuation that follows closes up to the word. */
    int    tagSpace;

    int    quoteDepth;              /* inside <blockquote>, how deep */
    int    linkDepth;               /* inside <a href>, how deep */

    /* A table being written out; see the table notes in FinishTag. */
    int    inTable;
    int    inCell;
    int    cellIsHeader;
    int    column;
    int    row;
    int    headerColumns;
    size_t cellStart;
    char   columnName[kGazetteTableColumns][48];
} GazetteExtract;

void GazetteExtractInit(GazetteExtract *e);

/* Feed the next run of page bytes. Returns 0 once there is no room for more,
   which is the fetch's signal to stop reading — the same contract the feed
   parser's sink has. */
int GazetteExtractFeed(GazetteExtract *e, const char *data, size_t len);

/*
 * No more bytes are coming. Runs the pipeline over what was scraped — decode
 * entities, transliterate to ASCII, flatten to paragraphs — and returns the
 * finished length, which is 0 when the page held nothing usable.
 */
size_t GazetteExtractFinish(GazetteExtract *e);

/* The finished text. "" before Finish. */
const char *GazetteExtractText(const GazetteExtract *e);

/*
 * Whether what Finish produced is worth putting in place of the feed's own
 * summary: kGazetteExtractMin of the page's text, or — when the page's
 * body came to less than that — the page's description of itself, if it
 * has one worth reading. A paywall stub, a cookie wall or a redirect notice
 * fails both, and the summary stands.
 */
int GazetteExtractUsable(GazetteExtract *e);

/* The pictures, once Finish has run — their alt text goes through the same
   pipeline as the text then. The k-th marker in the text is the k-th. */
int                    GazetteExtractPhotoCount(const GazetteExtract *e);
const GazettePhotoRef *GazetteExtractPhoto(const GazetteExtract *e, int i);

#ifdef __cplusplus
}
#endif

#endif /* GAZETTE_EXTRACT_H */
