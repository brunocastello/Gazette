/*
 * gazette_win_reader.c - the article pane on Windows.
 *
 * The counterpart of the reader half of src/ui/platinum_window.c. There it
 * is a TextEdit record with style runs; Windows has no stock control that
 * sets a heading, a byline and a body of mixed bold, italic and underlined
 * words and scrolls them -- a rich edit control is not on a clean Windows
 * 95 -- so this pane lays the text out itself: words wrapped across runs of
 * a few faces, a line at a time, into a list of lines it paints and
 * scrolls. It is small because the job is: no editing, no selection yet,
 * one column.
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
#include "portable/gazette_portable.h"

/* WM_MOUSEWHEEL is Windows 98 and NT 4's; on 95 it simply never arrives. */
#ifndef WM_MOUSEWHEEL
#define WM_MOUSEWHEEL 0x020A
#endif

enum {
    kMargin      = 8,       /* the Mac's kReaderMargin and kTextInset, near
                               enough, in Windows' larger pixels */
    kRuleAir     = 4,       /* the Mac's kReaderRuleAir */
    kMaxRuns     = 1024,
    kMaxPieces   = 8192,
    kMaxLines    = 4096,
    kWheelLines  = 3
};

/* The faces a run can wear. The low three bits are the body's inline
   styles; the kinds above them are the two paragraphs that are not body. */
enum {
    kFaceBold      = 1,
    kFaceItalic    = 2,
    kFaceUnderline = 4,
    kFaceTitle     = 8,     /* the headline: the UI font, bold, larger */
    kFaceByline    = 16,    /* under it, in grey */
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

static int   gRuleY = -1;   /* where the rule under the byline goes, or -1 */
static int   gTextHeight;
static int   gScroll;
static int   gLaidWidth = -1;

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
 * paragraphs is laid out rather than typed. The Mac's AppendBody, with
 * one difference while Windows has no photographs: a picture's marker is
 * dropped and the text closes over it, which is what the Mac does too
 * when photos are off.
 */
static void AppendBody(const char *body)
{
    const char   *p        = body;
    int           first    = 1;
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

    gTextLen    = 0;
    gText[0]    = '\0';
    gRunCount   = 0;
    gBodyStart  = 0;
    gScroll     = 0;
    gLaidWidth  = -1;

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
            AppendBody(body);
        } else {
            static const char kNone[] = "(This feed carries no summary for "
                                        "this article.)";

            AppendText(kNone, (int)sizeof(kNone) - 1);
            NoteRun(&start, &face, 0);
        }
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
    line->baseline = ascent;
    line->height   = ascent + descent;
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

        while (end < gTextLen && gText[end] != '\n') {
            end++;
        }
        if (end == start && end >= gTextLen) {
            break;
        }

        LayoutParagraph(dc, start, end, kMargin, width, &y);

        /* After the headline and the byline, the rule and its air; after a
           body paragraph, one blank line, as the Mac's two returns make. */
        if (end + 1 == gBodyStart && gBodyStart > 0) {
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
            const Piece *piece = &gPieces[k];

            SelectObject(dc, FaceFont(piece->face));
            SetTextColor(dc, GetSysColor((piece->face & kFaceByline)
                                             ? COLOR_GRAYTEXT
                                             : COLOR_WINDOWTEXT));
            TextOutA(dc, piece->x, top + line->baseline,
                     gText + piece->start, piece->len);
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

    case WM_LBUTTONDOWN:
        SetFocus(hwnd);
        return 0;

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
