/*
 * gazette_win_reader.c - the article pane on Windows.
 *
 * The counterpart of the reader half of src/ui/platinum_window.c. There it
 * is a TextEdit record with style runs; Windows has no stock control that
 * sets a heading, a byline and a body of mixed bold, italic and underlined
 * words and scrolls them -- a rich edit control is not on a clean Windows
 * 95 -- so this pane lays the text out itself: words wrapped across runs of
 * a few faces, a line at a time, into a list of lines it paints and
 * scrolls. It is small because the job is: no editing, one column, the
 * article's photographs set into it between paragraphs, and a selection
 * to copy from, as the Mac's TextEdit pane has.
 *
 * What goes in is composed exactly as the Mac composes it (SetReaderText
 * and AppendBody there): the headline, the byline, a rule, then the
 * article's own page when it has been read and the feed's summary when it
 * could not be, with the extractor's marks turned into faces. The words
 * are the engine's, which are ASCII; the only character added here is a
 * middle dot for a list item's bullet. Nothing from 0x80 to 0x9F: Windows
 * 95's MS Sans Serif has no glyphs there and draws black bars.
 */

#define WINVER       0x0400
#define _WIN32_WINNT 0x0400
#define _WIN32_IE    0x0300

#include <windows.h>
#include <string.h>

#include "gazette_win.h"
#include "core/gazette_core.h"
#include "extract/gazette_extract.h"
#include "feeds/gazette_feed_parse.h"
#include "feeds/gazette_feeds.h"
#include "feeds/gazette_photos.h"
#include "portable/gazette_portable.h"

/* WM_MOUSEWHEEL is Windows 98 and NT 4's; on 95 it simply never arrives. */
#ifndef WM_MOUSEWHEEL
#define WM_MOUSEWHEEL 0x020A
#endif

enum {
    kMargin      = 8,       /* the Mac's kReaderMargin and kTextInset, near
                               enough, in Windows' larger pixels */
    kRuleAir     = 4,       /* the Mac's kReaderRuleAir */
    kTitleLeading = 3,      /* extra height on each line of the headline */
    kTitleGap    = 4,       /* between the headline and the byline */
    kBodyLeading = 3,       /* extra height on each line of the article */
    kMaxRuns     = 1024,
    kMaxPieces   = 8192,
    kMaxLines    = 4096,
    kWheelLines  = 3,
    /* A photograph fills the column up to this, as on the Mac
       (kPhotoMaxWidth / kPhotoMaxHeight there). */
    kPhotoMaxWidth  = 560,
    kPhotoMaxHeight = 420,
    kCaptionGap     = 3     /* between a picture and its caption */
};

/* The faces a run can wear. The low three bits are the body's inline
   styles; the kinds above them are the two paragraphs that are not body. */
enum {
    kFaceBold      = 1,
    kFaceItalic    = 2,
    kFaceUnderline = 4,
    kFaceTitle     = 8,     /* the headline: the UI font, bold, larger */
    kFaceByline    = 16,    /* under it: the date and who wrote it */
    kFaceCount     = 32
};

typedef struct {
    int           start;
    int           end;
    unsigned char face;
} Run;

typedef struct {
    int           start;
    int           len;
    int           x;
    unsigned char face;
} Piece;

typedef struct {
    int start;              /* the text offset it begins at */
    int first;              /* its first piece */
    int count;
    int top;                /* from the top of the text, unscrolled */
    int height;
    int baseline;           /* from the line's top */
} Line;

static char  gText[2 * kGazetteExtractMax + 1024];
static int   gTextLen;
static Run   gRuns[kMaxRuns];
static int   gRunCount;
static Piece gPieces[kMaxPieces];
static int   gPieceCount;
static Line  gLines[kMaxLines];
static int   gLineCount;

/*
 * The selection, as offsets into gText: where the drag began and where it
 * is now, either way round. Empty when the two are the same. It survives a
 * recomposition of the same text -- a picture landing while text is
 * selected -- and nothing else.
 */
static int   gSelAnchor;
static int   gSelCaret;
static BOOL  gSelecting;        /* the mouse is down and dragging */
static int   gComposedArticle = -2;
static int   gParaStart;        /* the paragraph being laid out */

enum { kSelectTimer = 1, kSelectTick = 50 };

static int   gRuleY = -1;   /* where the rule under the byline goes, or -1 */
static int   gLeading;      /* extra height on each line of this paragraph */
static int   gTextHeight;
static int   gScroll;
static int   gLaidWidth = -1;

/*
 * The photographs, as they stand in the article. The text holds an empty
 * paragraph where each one goes, and the layout gives that paragraph the
 * picture's height instead of a line -- the Mac reserves a run of empty
 * lines in its TextEdit record and paints over them, and this is the same
 * idea without TextEdit in the way.
 *
 * A decoded picture is kept across recompositions -- the pane is composed
 * again each time a picture lands -- and goes when the article does.
 */
typedef struct {
    GazetteWinPicture picture;  /* decoded; bits NULL until then */
    BOOL              undrawable;   /* could not be read: takes no space */
    int               para;     /* offset of its empty paragraph, or -1 */
    BOOL              placeholder;  /* still coming: a grey field */
    BOOL              captioned;
    RECT              box;      /* laid out, from the top of the text */
} ReaderPhoto;

static ReaderPhoto gPhotos[kGazetteMaxPhotos];
static int         gPhotoArticle = -1;  /* whose the pictures are */

static HWND  gPane;
static HFONT gFonts[kFaceCount];
static int   gAscent[kFaceCount];
static int   gHeight[kFaceCount];

/* ------------------------------------------------------------------ */
/* Faces                                                               */
/* ------------------------------------------------------------------ */

/*
 * Every face is the interface font -- MS Sans Serif, Tahoma on XP,
 * whatever the user chose -- with a weight, a slant or a line under it.
 * The Mac's reader is set in the application font the headline list uses,
 * at the list's size, and the same rule here keeps the two panes reading
 * as one window. The headline is the only thing larger.
 */
static HFONT FaceFont(int face)
{
    LOGFONTA base;
    HFONT    ui = GazetteWindowFont();

    if (gFonts[face] != NULL) {
        return gFonts[face];
    }
    if (ui == NULL || GetObjectA(ui, sizeof(base), &base) == 0) {
        return (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    }

    if (face & kFaceTitle) {
        base.lfWeight = FW_BOLD;
        /* A third larger: enough to be the headline, not so much that a
           bitmap face on 95 falls back to a blocky scale. */
        base.lfHeight = (base.lfHeight * 4) / 3;
    } else {
        base.lfWeight    = (face & kFaceBold) ? FW_BOLD : FW_NORMAL;
        base.lfItalic    = (BYTE)((face & kFaceItalic) ? 1 : 0);
        base.lfUnderline = (BYTE)((face & kFaceUnderline) ? 1 : 0);
    }
    gFonts[face] = CreateFontIndirectA(&base);
    if (gFonts[face] == NULL) {
        gFonts[face] = ui;
    }
    return gFonts[face];
}

static void MeasureFace(HDC dc, int face)
{
    TEXTMETRICA metrics;

    if (gHeight[face] > 0) {
        return;
    }
    SelectObject(dc, FaceFont(face));
    if (GetTextMetricsA(dc, &metrics)) {
        gAscent[face] = metrics.tmAscent;
        gHeight[face] = metrics.tmHeight;
    } else {
        gAscent[face] = 10;
        gHeight[face] = 13;
    }
}

/* ------------------------------------------------------------------ */
/* Photographs                                                         */
/* ------------------------------------------------------------------ */

static void ForgetPhotos(void)
{
    int i;

    for (i = 0; i < kGazetteMaxPhotos; i++) {
        GazetteWinFreePicture(&gPhotos[i].picture);
        gPhotos[i].undrawable = FALSE;
        gPhotos[i].para       = -1;
    }
    gPhotoArticle = -1;
}

/*
 * Whether a picture takes space in the article, decoding it the first time
 * it is asked about once its bytes are here. While it is still coming it
 * stands as a placeholder, so the page jumps no more than it must when the
 * real one lands; not coming, or unreadable, it takes none and the text
 * closes over it -- the Mac's PhotoSize.
 */
static BOOL PhotoTakesSpace(int slot)
{
    ReaderPhoto *p = &gPhotos[slot];

    if (p->undrawable) {
        return FALSE;
    }
    switch (GazettePhotosState(slot)) {
    case kGazettePhotoLoaded:
        if (p->picture.bits == NULL) {
            long        len   = 0;
            const char *bytes = GazettePhotosData(slot, &len);

            if (!GazetteWinDecodePicture(bytes, len, kPhotoMaxWidth,
                                         kPhotoMaxHeight, &p->picture)) {
                p->undrawable = TRUE;
                return FALSE;
            }
        }
        p->placeholder = FALSE;
        return TRUE;

    case kGazettePhotoPending:
        p->placeholder = TRUE;
        return TRUE;

    default:
        return FALSE;
    }
}

/* The slot whose picture stands in the empty paragraph at this offset. */
static int PhotoAt(int offset)
{
    int i;

    for (i = 0; i < kGazetteMaxPhotos; i++) {
        if (gPhotos[i].para == offset) {
            return i;
        }
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/* Composing                                                           */
/* ------------------------------------------------------------------ */

static void AppendText(const char *text, int len)
{
    int room = (int)sizeof(gText) - 1 - gTextLen;

    if (len > room) {
        len = room;
    }
    if (len > 0) {
        memcpy(gText + gTextLen, text, (size_t)len);
        gTextLen += len;
    }
    gText[gTextLen] = '\0';
}

static void AppendChar(char c)
{
    AppendText(&c, 1);
}

/* A run from the end of the last one to here, when it wears anything. */
static void NoteRun(int *runStart, unsigned char *runFace, unsigned char face)
{
    if (*runStart < gTextLen && gRunCount < kMaxRuns) {
        gRuns[gRunCount].start = *runStart;
        gRuns[gRunCount].end   = gTextLen;
        gRuns[gRunCount].face  = *runFace;
        gRunCount++;
    }
    *runStart = gTextLen;
    *runFace  = face;
}

/*
 * The body, with the extractor's marks read out of it: paragraphs end at
 * newlines, and each becomes one line break here -- the gap between
 * paragraphs is laid out rather than typed. The Mac's AppendBody. A
 * paragraph that is only the photo marker is where a picture stood: when
 * the picture takes space it becomes an empty paragraph the layout makes
 * the picture's height; when it does not -- not coming, photos off,
 * unreadable -- it is dropped and the text closes over it.
 */
static void AppendBody(const char *body, BOOL photos)
{
    const char   *p        = body;
    int           first    = 1;
    int           marker   = 0;
    int           runStart = gTextLen;
    unsigned char runFace  = 0;

    while (*p != '\0') {
        const char   *end = p;
        const char   *q;
        unsigned char paragraph = 0;
        unsigned char inline_   = 0;
        int           bullet    = 0;
        int           any       = 0;

        while (*end != '\0' && *end != '\n') {
            end++;
        }

        if (end - p == 1 && *p == (char)kGazettePhotoMarker) {
            int slot = marker++;

            if (photos && slot < GazettePhotosCount() &&
                slot < kGazetteMaxPhotos && PhotoTakesSpace(slot)) {
                if (!first) {
                    AppendChar('\n');
                }
                first = 0;
                NoteRun(&runStart, &runFace, 0);
                gPhotos[slot].para      = gTextLen;
                gPhotos[slot].captioned =
                    (BOOL)(GazettePhotosCaption(slot)[0] != '\0');
            }
            p = end;
            while (*p == '\n') {
                p++;
            }
            continue;
        }

        for (q = p; q < end && GazetteIsMark(*q); q++) {
            if (*q == (char)kGazetteMarkHeading) {
                paragraph |= kFaceBold;
            } else if (*q == (char)kGazetteMarkQuote) {
                paragraph |= kFaceItalic;
            } else if (*q == (char)kGazetteMarkListItem) {
                bullet = 1;
            }
        }
        for (q = p; q < end; q++) {
            if (!GazetteIsMark(*q)) {
                any = 1;
                break;
            }
        }

        if (any) {
            if (!first) {
                AppendChar('\n');
            }
            first = 0;
            NoteRun(&runStart, &runFace, paragraph);
            if (bullet) {
                /* The middle dot, 0xB7: Windows-1252's bullet is 0x95,
                   in the range 95's MS Sans Serif draws as black bars. */
                AppendChar('\267');
                AppendChar(' ');
            }
        }

        for (q = p; q < end; q++) {
            char c = *q;

            if (GazetteIsMark(c)) {
                unsigned char was = inline_;

                switch ((unsigned char)c) {
                    case kGazetteMarkBoldOn:    inline_ |= kFaceBold;      break;
                    case kGazetteMarkBoldOff:   inline_ &= ~kFaceBold;     break;
                    case kGazetteMarkItalicOn:  inline_ |= kFaceItalic;    break;
                    case kGazetteMarkItalicOff: inline_ &= ~kFaceItalic;   break;
                    case kGazetteMarkLinkOn:    inline_ |= kFaceUnderline; break;
                    case kGazetteMarkLinkOff:   inline_ &= ~kFaceUnderline;break;
                    default: break;
                }
                if (any && inline_ != was) {
                    NoteRun(&runStart, &runFace,
                            (unsigned char)(paragraph | inline_));
                }
                continue;
            }
            AppendChar(c);
        }

        p = end;
        while (*p == '\n') {
            p++;
        }
    }
    NoteRun(&runStart, &runFace, 0);
}

/* What was composed: headline and byline end where the body begins. */
static int gBodyStart;

void GazetteWinReaderCompose(int article)
{
    const GazetteArticle *a = GazetteFeedsArticleAt(article);
    int                   wasLen = gTextLen;
    int                   wasArticle = gComposedArticle;

    gTextLen    = 0;
    gText[0]    = '\0';
    gRunCount   = 0;
    gBodyStart  = 0;
    gScroll     = 0;
    gLaidWidth  = -1;

    /* The pictures' places are composed afresh below; the decoded ones are
       kept only while they are still this article's. */
    if (gPhotoArticle != GazettePhotosArticle()) {
        ForgetPhotos();
        gPhotoArticle = GazettePhotosArticle();
    }
    {
        int i;

        for (i = 0; i < kGazetteMaxPhotos; i++) {
            gPhotos[i].para = -1;
        }
    }

    if (a == NULL) {
        static const char kNothing[] = "Select a headline to read it.";
        int               start      = 0;
        unsigned char     face       = 0;

        AppendText(kNothing, (int)sizeof(kNothing) - 1);
        NoteRun(&start, &face, 0);
    } else {
        const char   *from = a->source;
        const char   *body = a->body;
        char          when[64];
        char          byline[kGazetteArticleSourceLen + kGazetteTitleLen + 96];
        int           start = 0;
        unsigned char face  = kFaceTitle;

        /* The headline and the byline are the article's first two
           paragraphs, and scroll with it, as on the Mac. */
        AppendText(a->title, (int)strlen(a->title));
        AppendChar('\n');
        NoteRun(&start, &face, kFaceByline);

        GazetteFormatLongDate(GazetteFeedsLocalTime(a->date), when,
                              sizeof when);
        if (from[0] == '\0' && GazetteFeedsCurrentGroup() >= 0) {
            from = GazetteCoreFeedTitle(a->feed);
        }
        if (when[0] != '\0' && from[0] != '\0') {
            wsprintfA(byline, "%s by %s", when, from);
        } else if (from[0] != '\0') {
            wsprintfA(byline, "by %s", from);
        } else {
            lstrcpynA(byline, when, sizeof(byline));
        }
        AppendText(byline, lstrlenA(byline));
        AppendChar('\n');
        NoteRun(&start, &face, 0);
        gBodyStart = gTextLen;

        /* The page when it has been read, nothing while it is coming, and
           the summary when it could not be had -- SetReaderText's rule. */
        if (GazetteFeedsFullTextArticle() == article) {
            const char *full = GazetteFeedsFullText();

            if (full[0] != '\0') {
                body = full;
            }
        } else if (GazetteFeedsFullTextComing(article)) {
            body = NULL;
        }

        if (body == NULL) {
            static const char kWaiting[] = "Reading the full article...";

            AppendText(kWaiting, (int)sizeof(kWaiting) - 1);
            NoteRun(&start, &face, 0);
        } else if (body[0] != '\0') {
            /* Pictures only in the page they came from, and only when the
               job fetching them is this article's. */
            AppendBody(body, (BOOL)(GazetteCoreShowPhotos() &&
                                    body != a->body &&
                                    GazettePhotosArticle() == article));
        } else {
            static const char kNone[] = "(This feed carries no summary for "
                                        "this article.)";

            AppendText(kNone, (int)sizeof(kNone) - 1);
            NoteRun(&start, &face, 0);
        }
    }

    /* The same article at the same length keeps what was selected in it;
       anything else starts with nothing selected. */
    gComposedArticle = article;
    if (article != wasArticle || gTextLen != wasLen) {
        gSelAnchor = gSelCaret = 0;
    }

    if (gPane != NULL) {
        GazetteWinReaderLayout();
        InvalidateRect(gPane, NULL, TRUE);
    }
}

/* ------------------------------------------------------------------ */
/* Laying out                                                          */
/* ------------------------------------------------------------------ */

static unsigned char FaceAt(int at, int *runEnd)
{
    int i;

    for (i = 0; i < gRunCount; i++) {
        if (at >= gRuns[i].start && at < gRuns[i].end) {
            *runEnd = gRuns[i].end;
            return gRuns[i].face;
        }
    }
    *runEnd = gTextLen;
    return 0;
}

static int Width(HDC dc, int face, int start, int len)
{
    SIZE size;

    if (len <= 0) {
        return 0;
    }
    SelectObject(dc, FaceFont(face));
    if (!GetTextExtentPoint32A(dc, gText + start, len, &size)) {
        return 0;
    }
    return size.cx;
}

/* Close the line being built: its height is its tallest face's. */
static void EndLine(HDC dc, int *y, int firstPiece, int emptyFace)
{
    Line *line;
    int   ascent  = 0;
    int   descent = 0;
    int   i;

    if (gLineCount >= kMaxLines) {
        return;
    }
    line = &gLines[gLineCount];
    line->first = firstPiece;
    line->count = gPieceCount - firstPiece;
    line->start = (line->count > 0) ? gPieces[firstPiece].start : gParaStart;

    if (line->count == 0) {
        MeasureFace(dc, emptyFace);
        ascent  = gAscent[emptyFace];
        descent = gHeight[emptyFace] - gAscent[emptyFace];
    }
    for (i = firstPiece; i < gPieceCount; i++) {
        int f = gPieces[i].face;

        MeasureFace(dc, f);
        if (gAscent[f] > ascent) {
            ascent = gAscent[f];
        }
        if (gHeight[f] - gAscent[f] > descent) {
            descent = gHeight[f] - gAscent[f];
        }
    }
    line->top      = *y;
    /* The paragraph's leading, half above the words and half below, so
       lines stand further apart without the text sliding in its line. */
    line->baseline = ascent + gLeading / 2;
    line->height   = ascent + descent + gLeading;
    *y += line->height;
    gLineCount++;
}

/* A piece of text on the line, merged into the last when it is the same
   face and follows on. */
static void AddPiece(int start, int len, int x, unsigned char face,
                     int lineFirst)
{
    if (len <= 0) {
        return;
    }
    if (gPieceCount > lineFirst) {
        Piece *last = &gPieces[gPieceCount - 1];

        if (last->face == face && last->start + last->len == start) {
            last->len += len;
            return;
        }
    }
    if (gPieceCount >= kMaxPieces) {
        return;
    }
    gPieces[gPieceCount].start = start;
    gPieces[gPieceCount].len   = len;
    gPieces[gPieceCount].x     = x;
    gPieces[gPieceCount].face  = face;
    gPieceCount++;
}

/*
 * One paragraph, from start to end, wrapped at width. A word is whatever
 * lies between spaces, and may change face part-way -- a link inside a
 * sentence -- so it is measured as the fragments it is made of. A word
 * wider than the whole line goes on a line of its own and is clipped.
 */
static void LayoutParagraph(HDC dc, int start, int end, int left, int width,
                            int *y)
{
    int lineFirst = gPieceCount;
    int x         = 0;
    int at        = start;
    int emptyFace = 0;

    gParaStart = start;

    {
        int runEnd;
        emptyFace = FaceAt(start, &runEnd);
    }

    while (at < end) {
        int wordStart = at;
        int spaceEnd;
        int wordEnd;
        int spaceWidth = 0;
        int wordWidth  = 0;
        int p;

        /* The spaces before the word, then the word. */
        spaceEnd = at;
        while (spaceEnd < end && gText[spaceEnd] == ' ') {
            spaceEnd++;
        }
        wordEnd = spaceEnd;
        while (wordEnd < end && gText[wordEnd] != ' ') {
            wordEnd++;
        }

        for (p = wordStart; p < spaceEnd; ) {
            int runEnd;
            int f   = FaceAt(p, &runEnd);
            int stop = (runEnd < spaceEnd) ? runEnd : spaceEnd;

            spaceWidth += Width(dc, f, p, stop - p);
            p = stop;
        }
        for (p = spaceEnd; p < wordEnd; ) {
            int runEnd;
            int f   = FaceAt(p, &runEnd);
            int stop = (runEnd < wordEnd) ? runEnd : wordEnd;

            wordWidth += Width(dc, f, p, stop - p);
            p = stop;
        }

        if (x > 0 && x + spaceWidth + wordWidth > width) {
            /* To the next line; the spaces that would have led into the
               word are dropped at the break. */
            EndLine(dc, y, lineFirst, emptyFace);
            lineFirst = gPieceCount;
            x = 0;
            wordStart = spaceEnd;
            spaceWidth = 0;
        } else if (x == 0) {
            /* No spaces at the start of a line. */
            wordStart  = spaceEnd;
            spaceWidth = 0;
        }

        for (p = wordStart; p < wordEnd; ) {
            int runEnd;
            int f    = FaceAt(p, &runEnd);
            int stop = (runEnd < wordEnd) ? runEnd : wordEnd;
            int w    = Width(dc, f, p, stop - p);

            AddPiece(p, stop - p, left + x, (unsigned char)f, lineFirst);
            x += w;
            p = stop;
        }
        at = wordEnd;
    }
    EndLine(dc, y, lineFirst, emptyFace);
}

static void LayoutWidth(HDC dc, int width)
{
    int y     = kMargin;
    int start = 0;
    int para  = 0;

    gPieceCount = 0;
    gLineCount  = 0;
    gRuleY      = -1;

    if (width < 40) {
        width = 40;
    }

    while (start <= gTextLen) {
        int end = start;
        int slot;

        while (end < gTextLen && gText[end] != '\n') {
            end++;
        }

        /* A picture's paragraph: the picture's height, and its caption's,
           then the blank line a paragraph is followed by. */
        slot = (end == start) ? PhotoAt(start) : -1;
        if (slot >= 0) {
            ReaderPhoto *ph     = &gPhotos[slot];
            int          column = (width < kPhotoMaxWidth) ? width
                                                           : kPhotoMaxWidth;
            int          w, h;

            if (ph->placeholder) {
                w = column;
                h = w * 2 / 3;
                if (h > kPhotoMaxHeight) {
                    h = kPhotoMaxHeight;
                }
            } else {
                w = ph->picture.width;
                h = ph->picture.height;
                if (w > column) {               /* the column narrowed */
                    h = (int)((long)h * column / w);
                    w = column;
                }
            }
            /* In the middle of the column when it does not fill it. */
            ph->box.left   = kMargin + (width - w) / 2;
            ph->box.top    = y;
            ph->box.right  = ph->box.left + w;
            ph->box.bottom = y + h;
            y += h;
            MeasureFace(dc, kFaceByline);
            if (ph->captioned) {
                y += kCaptionGap + gHeight[kFaceByline];
            }
            if (end < gTextLen) {
                MeasureFace(dc, 0);
                y += gHeight[0];
            }
            start = end + 1;
            continue;
        }

        if (end == start && end >= gTextLen) {
            break;
        }

        /* The headline, the byline and the article each set their own line
           height (Bruno, 2026-09-26): a little air in the headline's lines
           and the body's, none added to the byline's one. */
        if (gBodyStart > 0 && start == 0) {
            gLeading = kTitleLeading;
        } else if (gBodyStart > 0 && end + 1 == gBodyStart) {
            gLeading = 0;
        } else {
            gLeading = kBodyLeading;
        }

        LayoutParagraph(dc, start, end, kMargin, width, &y);

        /* After the headline, a little space before the byline; after the
           byline, the rule and its air; after a body paragraph, one blank
           line, as the Mac's two returns make. */
        if (gBodyStart > 0 && start == 0) {
            y += kTitleGap;
        } else if (end + 1 == gBodyStart && gBodyStart > 0) {
            MeasureFace(dc, 0);
            y += kRuleAir;
            gRuleY = y;
            y += 1 + kRuleAir + gHeight[0] / 2;
        } else if (start >= gBodyStart && end < gTextLen) {
            MeasureFace(dc, 0);
            y += gHeight[0];
        }
        para++;
        start = end + 1;
    }
    (void)para;
    gTextHeight = y + kMargin;
}

static void SyncScrollBar(int page)
{
    SCROLLINFO info;
    int        most = gTextHeight - page;

    if (most < 0) {
        most = 0;
    }
    if (gScroll > most) {
        gScroll = most;
    }
    if (gScroll < 0) {
        gScroll = 0;
    }

    ZeroMemory(&info, sizeof(info));
    info.cbSize = sizeof(info);
    info.fMask  = SIF_RANGE | SIF_PAGE | SIF_POS;
    info.nMin   = 0;
    info.nMax   = (gTextHeight > page) ? gTextHeight - 1 : 0;
    info.nPage  = (UINT)page;
    info.nPos   = gScroll;
    SetScrollInfo(gPane, SB_VERT, &info, TRUE);
}

/*
 * Lay the text out at the pane's width. The scroll bar comes and goes with
 * the length -- no bar on an article that fits, as Outlook Express's
 * preview has none when there is nothing to scroll -- and taking its room
 * narrows the text, so a text that only overflows once it has lost the
 * bar's width is laid out twice.
 */
void GazetteWinReaderLayout(void)
{
    RECT client;
    HDC  dc;
    int  full, page, bar;
    DWORD style;

    if (gPane == NULL) {
        return;
    }
    GetClientRect(gPane, &client);
    style = (DWORD)GetWindowLongA(gPane, GWL_STYLE);
    bar   = GetSystemMetrics(SM_CXVSCROLL);
    full  = client.right + ((style & WS_VSCROLL) ? bar : 0);
    page  = client.bottom;

    dc = GetDC(gPane);
    LayoutWidth(dc, full - kMargin * 2);
    if (gTextHeight > page) {
        LayoutWidth(dc, full - bar - kMargin * 2);
    }
    ReleaseDC(gPane, dc);

    gLaidWidth = full;
    SyncScrollBar(page);
}

int GazetteWinReaderOffset(void)
{
    return gScroll;
}

void GazetteWinReaderScrollTo(int offset)
{
    RECT client;

    if (gPane == NULL) {
        return;
    }
    GetClientRect(gPane, &client);
    gScroll = offset;
    SyncScrollBar(client.bottom);
    InvalidateRect(gPane, NULL, TRUE);
}

/* ------------------------------------------------------------------ */
/* Painting and scrolling                                              */
/* ------------------------------------------------------------------ */

static int SelStart(void)
{
    return (gSelAnchor < gSelCaret) ? gSelAnchor : gSelCaret;
}

static int SelEnd(void)
{
    return (gSelAnchor < gSelCaret) ? gSelCaret : gSelAnchor;
}

/* A run of one piece, from start for len, drawn at x: selected or not. */
static void PaintRun(HDC dc, const Piece *piece, int start, int len,
                     int top, const Line *line, BOOL selected)
{
    int x = piece->x + Width(dc, piece->face, piece->start,
                             start - piece->start);

    if (len <= 0) {
        return;
    }
    SelectObject(dc, FaceFont(piece->face));
    if (selected) {
        RECT band;

        band.left   = x;
        band.top    = top;
        band.right  = x + Width(dc, piece->face, start, len);
        band.bottom = top + line->height;
        FillRect(dc, &band, GetSysColorBrush(COLOR_HIGHLIGHT));
        SelectObject(dc, FaceFont(piece->face));
        SetTextColor(dc, GetSysColor(COLOR_HIGHLIGHTTEXT));
    } else {
        SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
    }
    TextOutA(dc, x, top + line->baseline, gText + start, len);
}

/* A piece, in up to three runs: before the selection, in it, after it --
   the system's highlight colours, as an edit control shows a selection. */
static void PaintPiece(HDC dc, const Piece *piece, int top, const Line *line)
{
    int end = piece->start + piece->len;
    int a   = SelStart();
    int b   = SelEnd();

    if (a >= b || b <= piece->start || a >= end) {
        PaintRun(dc, piece, piece->start, piece->len, top, line, FALSE);
        return;
    }
    if (a < piece->start) {
        a = piece->start;
    }
    if (b > end) {
        b = end;
    }
    PaintRun(dc, piece, piece->start, a - piece->start, top, line, FALSE);
    PaintRun(dc, piece, a, b - a, top, line, TRUE);
    PaintRun(dc, piece, b, end - b, top, line, FALSE);
}

static void Paint(HDC dc, const RECT *client, const RECT *dirty)
{
    int i;

    FillRect(dc, dirty, (HBRUSH)(COLOR_WINDOW + 1));
    SetBkMode(dc, TRANSPARENT);
    SetTextAlign(dc, TA_BASELINE | TA_LEFT);

    for (i = 0; i < gLineCount; i++) {
        const Line *line = &gLines[i];
        int         top  = line->top - gScroll;
        int         k;

        if (top + line->height < dirty->top) {
            continue;
        }
        if (top > dirty->bottom) {
            break;
        }
        for (k = line->first; k < line->first + line->count; k++) {
            PaintPiece(dc, &gPieces[k], top, line);
        }
    }

    /* The pictures over their paragraphs: a grey field with a darker edge
       while one is coming, the picture once it is here, and its caption
       on the line under it in the byline's face. */
    for (i = 0; i < kGazetteMaxPhotos; i++) {
        const ReaderPhoto *ph = &gPhotos[i];
        RECT               box, caption, hit;

        if (ph->para < 0) {
            continue;
        }
        box = ph->box;
        OffsetRect(&box, 0, -gScroll);
        caption = box;
        caption.top    = box.bottom + kCaptionGap;
        caption.bottom = caption.top + gHeight[kFaceByline];

        if (IntersectRect(&hit, &box, dirty)) {
            if (ph->placeholder) {
                HBRUSH grey  = CreateSolidBrush(GazetteWindowLightTone());
                HBRUSH frame = GetSysColorBrush(COLOR_BTNSHADOW);

                FillRect(dc, &box, grey);
                FrameRect(dc, &box, frame);
                DeleteObject(grey);
            } else {
                GazetteWinDrawPicture(dc, &ph->picture, box.left, box.top,
                                      box.right - box.left,
                                      box.bottom - box.top);
            }
        }
        if (ph->captioned && IntersectRect(&hit, &caption, dirty)) {
            const char *alt = GazettePhotosCaption(i);

            SetTextAlign(dc, TA_TOP | TA_LEFT);
            SelectObject(dc, FaceFont(kFaceByline));
            SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
            /* Cut to the picture's width with three full stops -- not the
               ellipsis character, which 95 draws as a black bar. */
            DrawTextA(dc, alt, -1, &caption,
                      DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
            SetTextAlign(dc, TA_BASELINE | TA_LEFT);
        }
    }

    /* The rule between the byline and the article, travelling with the
       text as the Mac's does. */
    if (gRuleY >= 0) {
        HPEN pen = CreatePen(PS_SOLID, 1, GazetteWindowLightTone());
        HPEN old = (HPEN)SelectObject(dc, pen);
        int  y   = gRuleY - gScroll;

        MoveToEx(dc, kMargin, y, NULL);
        LineTo(dc, client->right - kMargin, y);
        SelectObject(dc, old);
        DeleteObject(pen);
    }
}

static int  LineStep(void);

static void ScrollBy(int delta)
{
    RECT client;
    int  was = gScroll;

    GetClientRect(gPane, &client);
    gScroll += delta;
    SyncScrollBar(client.bottom);
    if (gScroll != was) {
        ScrollWindowEx(gPane, 0, was - gScroll, NULL, NULL, NULL, NULL,
                       SW_INVALIDATE | SW_ERASE);
        UpdateWindow(gPane);
    }
}

/* ------------------------------------------------------------------ */
/* Selecting and copying                                               */
/* ------------------------------------------------------------------ */

/*
 * The text offset under a point in the pane: the line whose band it is in,
 * then the character whose nearer half it is in. Above the text is its
 * start, below it its end; a picture's band belongs to the line after it.
 */
static int OffsetAt(int x, int y)
{
    static int dx[1024];
    const Line *line = NULL;
    HDC         dc;
    int         i, k, at;

    y += gScroll;
    if (gLineCount == 0 || y < gLines[0].top) {
        return 0;
    }
    for (i = 0; i < gLineCount; i++) {
        if (y < gLines[i].top + gLines[i].height) {
            line = &gLines[i];
            break;
        }
    }
    if (line == NULL) {
        return gTextLen;
    }
    if (line->count == 0) {
        return line->start;
    }
    if (x <= gPieces[line->first].x) {
        return gPieces[line->first].start;
    }

    dc = GetDC(gPane);
    at = -1;
    for (k = line->first; k < line->first + line->count && at < 0; k++) {
        const Piece *piece = &gPieces[k];
        int          len   = piece->len;
        int          fit   = 0;
        SIZE         size;

        if (len > (int)(sizeof dx / sizeof dx[0])) {
            len = (int)(sizeof dx / sizeof dx[0]);
        }
        SelectObject(dc, FaceFont(piece->face));
        if (!GetTextExtentExPointA(dc, gText + piece->start, len, 32767,
                                   &fit, dx, &size)) {
            continue;
        }
        if (x >= piece->x + size.cx) {
            continue;                       /* past this piece */
        }
        /* The character whose middle is right of the point: before it. */
        for (i = 0; i < len; i++) {
            int left  = (i == 0) ? 0 : dx[i - 1];
            int right = dx[i];

            if (x - piece->x < (left + right) / 2) {
                break;
            }
        }
        at = piece->start + i;
    }
    ReleaseDC(gPane, dc);

    if (at < 0) {
        const Piece *last = &gPieces[line->first + line->count - 1];

        at = last->start + last->len;
    }
    return at;
}

static void SetCaret(int offset)
{
    if (offset != gSelCaret) {
        gSelCaret = offset;
        InvalidateRect(gPane, NULL, FALSE);
    }
}

/* A word: the run of characters around the offset that are not spaces or
   paragraph breaks. */
static void SelectWord(int at)
{
    int a = at;
    int b = at;

    while (a > 0 && gText[a - 1] != ' ' && gText[a - 1] != '\n') {
        a--;
    }
    while (b < gTextLen && gText[b] != ' ' && gText[b] != '\n') {
        b++;
    }
    gSelAnchor = a;
    gSelCaret  = b;
    InvalidateRect(gPane, NULL, FALSE);
}

BOOL GazetteWinReaderHasSelection(void)
{
    return (BOOL)(SelStart() < SelEnd());
}

void GazetteWinReaderSelectAll(void)
{
    gSelAnchor = 0;
    gSelCaret  = gTextLen;
    if (gPane != NULL) {
        InvalidateRect(gPane, NULL, FALSE);
    }
}

/*
 * The selection onto the clipboard as plain text in the ANSI code page
 * (CF_TEXT, which every Windows reads). A line break in the article's text
 * is a paragraph: CRLF after the headline and the byline, and a blank line
 * between the body's paragraphs, as they stand on screen. A picture's
 * empty paragraph adds nothing.
 */
BOOL GazetteWinReaderCopy(HWND owner)
{
    int     a = SelStart();
    int     b = SelEnd();
    int     i, n = 0;
    HGLOBAL block;
    char   *out;

    if (a >= b) {
        return FALSE;
    }
    block = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE,
                        (SIZE_T)(b - a) * 4 + 1);
    if (block == NULL) {
        return FALSE;
    }
    out = (char *)GlobalLock(block);
    if (out == NULL) {
        GlobalFree(block);
        return FALSE;
    }
    for (i = a; i < b; i++) {
        if (gText[i] != '\n') {
            out[n++] = gText[i];
            continue;
        }
        if (i > a && gText[i - 1] == '\n') {
            continue;                       /* a picture's paragraph */
        }
        out[n++] = '\r';
        out[n++] = '\n';
        if (i >= gBodyStart && gBodyStart > 0) {
            out[n++] = '\r';
            out[n++] = '\n';
        }
    }
    out[n] = '\0';
    GlobalUnlock(block);

    if (!OpenClipboard(owner)) {
        GlobalFree(block);
        return FALSE;
    }
    EmptyClipboard();
    if (SetClipboardData(CF_TEXT, block) == NULL) {
        GlobalFree(block);
        CloseClipboard();
        return FALSE;
    }
    CloseClipboard();
    return TRUE;
}

/*
 * The article's contextual menu (Bruno, 2026-09-26): an edit menu, as a
 * piece of text has on Windows, with only what a text that cannot be
 * edited can do -- Copy what is selected, Select All -- and the rest there
 * and greyed, so it reads as the menu it is. The Mac's is the same menu.
 */
enum {
    kReaderUndo = 1,
    kReaderCut,
    kReaderCopy,
    kReaderPaste,
    kReaderDelete,
    kReaderSelectAll
};

static void ReaderContextMenu(LPARAM where)
{
    HMENU menu = CreatePopupMenu();
    POINT pt;
    int   chosen;

    if (menu == NULL) {
        return;
    }
    AppendMenuA(menu, MF_STRING | MF_GRAYED, kReaderUndo, "&Undo");
    AppendMenuA(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(menu, MF_STRING | MF_GRAYED, kReaderCut, "Cu&t");
    AppendMenuA(menu, MF_STRING |
                (GazetteWinReaderHasSelection() ? 0 : MF_GRAYED),
                kReaderCopy, "&Copy");
    AppendMenuA(menu, MF_STRING | MF_GRAYED, kReaderPaste, "&Paste");
    AppendMenuA(menu, MF_STRING | MF_GRAYED, kReaderDelete, "&Delete");
    AppendMenuA(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(menu, MF_STRING | (gTextLen > 0 ? 0 : MF_GRAYED),
                kReaderSelectAll, "Select &All");

    /* From the keyboard -- the menu key, Shift+F10 -- there is no point,
       and the menu opens at the pane's corner. */
    if (where == (LPARAM)-1) {
        pt.x = kMargin;
        pt.y = kMargin;
        ClientToScreen(gPane, &pt);
    } else {
        pt.x = (short)LOWORD(where);
        pt.y = (short)HIWORD(where);
    }
    chosen = (int)TrackPopupMenu(menu,
                                 TPM_LEFTALIGN | TPM_TOPALIGN |
                                 TPM_RIGHTBUTTON | TPM_RETURNCMD |
                                 TPM_NONOTIFY,
                                 pt.x, pt.y, 0, gPane, NULL);
    DestroyMenu(menu);

    if (chosen == kReaderCopy) {
        (void)GazetteWinReaderCopy(gPane);
    } else if (chosen == kReaderSelectAll) {
        GazetteWinReaderSelectAll();
    }
}

/* While the mouse is held beyond the pane's top or bottom, the text scrolls
   under it and the selection follows. */
static void DragSelect(void)
{
    RECT  client;
    POINT pt;

    GetCursorPos(&pt);
    ScreenToClient(gPane, &pt);
    GetClientRect(gPane, &client);
    if (pt.y < 0) {
        ScrollBy(-LineStep());
    } else if (pt.y >= client.bottom) {
        ScrollBy(LineStep());
    }
    SetCaret(OffsetAt(pt.x, pt.y));
}

static int LineStep(void)
{
    return (gHeight[0] > 0) ? gHeight[0] : 13;
}

LRESULT CALLBACK GazetteWinReaderProc(HWND hwnd, UINT message,
                                      WPARAM wParam, LPARAM lParam)
{
    switch (message) {
    case WM_CREATE:
        gPane = hwnd;
        if (gTextLen == 0) {
            GazetteWinReaderCompose(-1);
        }
        return 0;

    case WM_DESTROY: {
        int i;

        for (i = 0; i < kFaceCount; i++) {
            if (gFonts[i] != NULL && gFonts[i] != GazetteWindowFont()) {
                DeleteObject(gFonts[i]);
            }
            gFonts[i]  = NULL;
            gHeight[i] = 0;
        }
        gPane = NULL;
        return 0;
    }

    case WM_SIZE: {
        RECT  client;
        DWORD style = (DWORD)GetWindowLongA(hwnd, GWL_STYLE);
        int   full;

        GetClientRect(hwnd, &client);
        full = client.right +
               ((style & WS_VSCROLL) ? GetSystemMetrics(SM_CXVSCROLL) : 0);
        /* A scroll bar coming or going resizes the pane too; only a real
           change of width is worth laying the text out again for. */
        if (full != gLaidWidth) {
            GazetteWinReaderLayout();
            InvalidateRect(hwnd, NULL, TRUE);
        } else {
            SyncScrollBar(client.bottom);
        }
        return 0;
    }

    case WM_ERASEBKGND:
        return TRUE;            /* WM_PAINT fills every pixel it paints */

    case WM_PAINT: {
        PAINTSTRUCT ps;
        RECT        client;
        HDC         dc = BeginPaint(hwnd, &ps);
        HDC         mem;
        HBITMAP     bitmap, oldBitmap;
        HFONT       oldFont;

        GetClientRect(hwnd, &client);

        /* Off screen and then across in one go: text laid out a piece at a
           time flickers as it scrolls otherwise. */
        mem    = CreateCompatibleDC(dc);
        bitmap = (mem != NULL)
                     ? CreateCompatibleBitmap(dc, client.right, client.bottom)
                     : NULL;
        if (mem != NULL && bitmap != NULL) {
            oldBitmap = (HBITMAP)SelectObject(mem, bitmap);
            oldFont   = (HFONT)SelectObject(mem, GetStockObject(SYSTEM_FONT));
            Paint(mem, &client, &ps.rcPaint);
            BitBlt(dc, ps.rcPaint.left, ps.rcPaint.top,
                   ps.rcPaint.right - ps.rcPaint.left,
                   ps.rcPaint.bottom - ps.rcPaint.top,
                   mem, ps.rcPaint.left, ps.rcPaint.top, SRCCOPY);
            SelectObject(mem, oldFont);
            SelectObject(mem, oldBitmap);
        } else {
            oldFont = (HFONT)SelectObject(dc, GetStockObject(SYSTEM_FONT));
            Paint(dc, &client, &ps.rcPaint);
            SelectObject(dc, oldFont);
        }
        if (bitmap != NULL) {
            DeleteObject(bitmap);
        }
        if (mem != NULL) {
            DeleteDC(mem);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_VSCROLL: {
        RECT client;
        int  page;

        GetClientRect(hwnd, &client);
        page = client.bottom - LineStep();
        if (page < LineStep()) {
            page = LineStep();
        }
        switch (LOWORD(wParam)) {
        case SB_LINEUP:        ScrollBy(-LineStep());               break;
        case SB_LINEDOWN:      ScrollBy(LineStep());                break;
        case SB_PAGEUP:        ScrollBy(-page);                     break;
        case SB_PAGEDOWN:      ScrollBy(page);                      break;
        case SB_TOP:           ScrollBy(-gScroll);                  break;
        case SB_BOTTOM:        ScrollBy(gTextHeight);               break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION: {
            /* The 32-bit position, not the 16 bits in the message: a long
               article is taller than 65535 pixels. */
            SCROLLINFO info;

            ZeroMemory(&info, sizeof(info));
            info.cbSize = sizeof(info);
            info.fMask  = SIF_TRACKPOS;
            if (GetScrollInfo(hwnd, SB_VERT, &info)) {
                ScrollBy(info.nTrackPos - gScroll);
            }
            break;
        }
        }
        return 0;
    }

    case WM_MOUSEWHEEL: {
        short turn = (short)HIWORD(wParam);

        ScrollBy(-(turn / 120) * kWheelLines * LineStep());
        return 0;
    }

    case WM_LBUTTONDOWN: {
        int at = OffsetAt((short)LOWORD(lParam), (short)HIWORD(lParam));

        SetFocus(hwnd);
        /* Shift extends what is selected; a plain press starts afresh. */
        if (!(wParam & MK_SHIFT)) {
            gSelAnchor = at;
        }
        gSelCaret  = at;
        gSelecting = TRUE;
        SetCapture(hwnd);
        SetTimer(hwnd, kSelectTimer, kSelectTick, NULL);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }

    case WM_LBUTTONDBLCLK:
        SetFocus(hwnd);
        SelectWord(OffsetAt((short)LOWORD(lParam), (short)HIWORD(lParam)));
        return 0;

    case WM_MOUSEMOVE:
        if (gSelecting) {
            SetCaret(OffsetAt((short)LOWORD(lParam), (short)HIWORD(lParam)));
        }
        return 0;

    case WM_TIMER:
        if (wParam == kSelectTimer && gSelecting) {
            DragSelect();
        }
        return 0;

    case WM_LBUTTONUP:
    case WM_CAPTURECHANGED:
        if (gSelecting) {
            gSelecting = FALSE;
            KillTimer(hwnd, kSelectTimer);
            if (GetCapture() == hwnd) {
                ReleaseCapture();
            }
        }
        return 0;

    case WM_SETCURSOR:
        /* The text cursor over the text, as over any text you can select. */
        if (LOWORD(lParam) == HTCLIENT) {
            SetCursor(LoadCursor(NULL, IDC_IBEAM));
            return TRUE;
        }
        break;

    case WM_CONTEXTMENU:
        ReaderContextMenu(lParam);
        return 0;

    case WM_CHAR:
        if (wParam == 1) {                  /* Ctrl+A */
            GazetteWinReaderSelectAll();
            return 0;
        }
        break;

    case WM_GETDLGCODE:
        return DLGC_WANTARROWS;

    case WM_KEYDOWN: {
        RECT client;

        GetClientRect(hwnd, &client);
        switch (wParam) {
        case VK_UP:    ScrollBy(-LineStep());                     return 0;
        case VK_DOWN:  ScrollBy(LineStep());                      return 0;
        case VK_PRIOR: ScrollBy(-(client.bottom - LineStep()));   return 0;
        case VK_NEXT:
        case VK_SPACE: ScrollBy(client.bottom - LineStep());      return 0;
        case VK_HOME:  ScrollBy(-gScroll);                        return 0;
        case VK_END:   ScrollBy(gTextHeight);                     return 0;
        }
        break;
    }
    }

    return DefWindowProc(hwnd, message, wParam, lParam);
}
