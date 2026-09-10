/*
 * Gazette — the Platinum main window
 * Copyright (c) 2026 brunocastello
 *
 * See platinum_window.h. Three panes, two draggable dividers, and a status
 * line.
 *
 * The sidebar and the headline list are List Manager lists. They were drawn
 * by hand through Phase 4 — rows, highlighting, scroll arithmetic and all —
 * which was debt taken deliberately to get the engine working first. A real
 * list brings its own scroll bar, its own hit testing, its own auto-scroll
 * and its own idea of what a selection looks like when the window is not in
 * front, and every one of those was being reimplemented here.
 *
 * They are custom lists: CreateCustomList with a ListDefSpec of
 * kListDefUserProcType, so the list definition function is a callback in
 * this file rather than an 'LDEF' code resource — which Carbon does not
 * allow anyway. The cells carry no data. Both lists are a view onto
 * something the engine already holds in order, so a cell's row number *is*
 * its index into that: the sidebar's rows come from
 * GazetteCoreSidebarRowAt and the headlines from GazetteFeedsArticleAt.
 * Keeping a copy in the cells would only be a second thing to get out of
 * date.
 *
 * The reader pane is a styled TextEdit record. TextEdit is what the Toolbox
 * has for wrapped, styled, selectable prose, and it replaced a greedy word
 * wrapper, a line index and a drawing loop that between them did the same
 * job less well.
 */

#include "ui/platinum_window.h"

#include "core/gazette_core.h"
#include "extract/gazette_extract.h"
#include "feeds/gazette_feeds.h"
#include "feeds/gazette_index.h"
#include "portable/gazette_portable.h"

#include <Appearance.h>
#include <Controls.h>
#include <ControlDefinitions.h>
#include <DateTimeUtils.h>
#include <Fonts.h>
#include <Lists.h>
#include <Quickdraw.h>
#include <QuickdrawText.h>
#include <Scrap.h>
#include <TextEdit.h>

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Metrics                                                             */
/* ------------------------------------------------------------------ */

enum {
    kScrollWidth   = 16,        /* Platinum's scroll bar, including its frame */
    kHeaderHeight  = 17,        /* the placard over each list                 */
    kStatusHeight  = 20,
    kRowHeight     = 14,        /* a list cell: Geneva 9 plus leading         */
    kReaderLead    = 13,        /* what one arrow scrolls the article by      */
    kDividerWidth  = 4,         /* the draggable gap between panes            */
    kTextInset     = 4,
    kDateColumn    = 46,        /* headline text starts here, after the time  */
    kBaseline      = 10,        /* the text baseline inside a cell            */

    /* The sidebar is an outline: a column for the disclosure triangle, then
       one indent for a group's feeds. A top-level feed is a group's sibling,
       so it starts where a group's name does. */
    kTriangleSize   = 12,       /* what the Appearance Manager draws into */
    kTriangleColumn = 14,       /* the triangle's own column, with its gap */
    kGroupIndent    = 12,

    kMinSidebar    = 96,
    kMaxSidebarPad = 160,       /* how much room the right side must keep     */
    kMinListHeight = 3 * kRowHeight,
    kMinReader     = 3 * kReaderLead,
    kReaderMargin  = 2,         /* above the first line and below the last */

    /* Which pane the keyboard is driving. The reader's scroll bar carries
       kRefReader as its control reference, so its action procedure and the
       focus are named the same way. */
    kRefSidebar = 1,
    kRefList    = 2,
    kRefReader  = 3
};

/* Seconds between the Macintosh epoch (1904) and the Unix one (1970). */
enum { kMacToUnixEpoch = 2082844800L };

/* ------------------------------------------------------------------ */
/* State                                                               */
/* ------------------------------------------------------------------ */

static WindowRef              gWindow;
static GazetteUIFeedChosen    gOnFeedChosen;
static GazetteUIArticleChosen gOnArticleChosen;
static GazetteUIGroupChosen   gOnGroupChosen;

static ListHandle gSidebarList;
static ListHandle gArticleList;
static ListDefUPP gSidebarLDEF;
static ListDefUPP gArticleLDEF;

static ControlRef gReaderScroll;
static ControlActionUPP gScrollUPP;

/* Pane rectangles, recomputed by Layout() and by nothing else.
   A "pane" is the framed box; a "rect" is the list's own view inside it,
   which stops short of the scroll bar the List Manager puts down the right
   hand edge. */
static Rect gSidebarPane;
static Rect gSidebarRect;
static Rect gSidebarHeader;
static Rect gListPane;
static Rect gListRect;
static Rect gListHeader;
static Rect gReaderRect;
static Rect gStatusRect;
static Rect gVDivider;          /* between sidebar and the right side */
static Rect gHDivider;          /* between headlines and the article  */

static short gSidebarWidth = 168;
static short gListShare    = 45;    /* percent of the right side given to the
                                       headline list; the article gets the rest */

static int gSelectedFeed    = 0;
static int gSelectedArticle = -1;

/* The group the sidebar has selected, or -1 when the selection is a feed.
   gSelectedFeed keeps its meaning either way: it is the feed the headline
   list and the reader are showing, which a click on a group does not
   change. */
static int gSelectedGroup   = -1;

/*
 * Which pane the arrow keys drive. Tab moves it on, and a click in a pane
 * takes it — the same two ways focus moves in every Platinum application
 * with more than one list in a window.
 */
static short gFocus = kRefList;

static char gStatus[192];

/*
 * The article, staged here and then handed to TextEdit, which keeps its own
 * copy. Room for twice the extractor's output because a paragraph break
 * becomes two carriage returns on the way in, plus the title and the byline.
 */
static TEHandle gReaderTE;
static char     gReaderText[2 * kGazetteExtractMax + 512];

static void Layout(void);
static void SetReaderText(void);
static void SizeReader(void);
static void ChooseRow(const GazetteSidebarRow *row);
static int  SelectedRow(void);
static void SetFocus(short pane);
static void DrawSidebarPane(void);
static void DrawArticlePane(void);
static void DrawReader(void);
static void DrawStatus(void);

/* ------------------------------------------------------------------ */
/* Small helpers                                                       */
/* ------------------------------------------------------------------ */

static short WidthOfN(const char *text, short len)
{
    return (len <= 0) ? 0 : TextWidth(text, 0, len);
}

/*
 * Draw text clipped to a width, ending in an ellipsis when it does not fit.
 * A headline is nearly always too long for its column, and a hard clip mid
 * word reads as a drawing bug rather than as truncation.
 */
static void DrawTruncated(const char *text, short maxWidth)
{
    short len;
    short ellipsis;

    if (text == NULL || maxWidth <= 0) {
        return;
    }

    len = (short)strlen(text);
    if (WidthOfN(text, len) <= maxWidth) {
        DrawText(text, 0, len);
        return;
    }

    /* "\311" is the MacRoman ellipsis, one character rather than three. */
    ellipsis = CharWidth('\311');
    while (len > 0 && WidthOfN(text, len) + ellipsis > maxWidth) {
        len--;
    }
    if (len > 0) {
        DrawText(text, 0, len);
    }
    DrawChar('\311');
}

static long UnixNow(void)
{
    unsigned long macNow = 0;

    GetDateTime(&macNow);
    return (long)macNow - kMacToUnixEpoch;
}

/* ------------------------------------------------------------------ */
/* Talking to a list                                                   */
/* ------------------------------------------------------------------ */

/* How many rows fit, which is what a page key moves by. */
static short ListPageSize(ListHandle list)
{
    ListBounds visible;
    short      rows;

    if (list == NULL) {
        return 1;
    }
    GetListVisibleCells(list, &visible);
    rows = (short)(visible.bottom - visible.top);
    return (rows > 0) ? rows : 1;
}

static int ListRowCount(ListHandle list)
{
    ListBounds data;

    if (list == NULL) {
        return 0;
    }
    GetListDataBounds(list, &data);
    return data.bottom - data.top;
}

/*
 * Add or delete rows until the list is as long as the model behind it. The
 * cells stay empty — see the note at the top of the file about why the row
 * number is the index.
 */
static void SetRowCount(ListHandle list, int count)
{
    int have = ListRowCount(list);

    if (list == NULL || count == have) {
        return;
    }
    if (count < 0) {
        count = 0;
    }
    if (count > have) {
        (void)LAddRow((short)(count - have), (short)have, list);
    } else {
        LDelRow((short)(have - count), (short)count, list);
    }
}

/*
 * Select one row and nothing else, or clear the selection when row is -1
 * (which is what a feed inside a shut group comes to: still the selection,
 * with no row on screen to show it on). lOnlyOne keeps a click to a single
 * cell but says nothing about LSetSelect, so the old selection is cleared
 * here by hand.
 */
static void SelectRow(ListHandle list, int row, Boolean reveal)
{
    Cell cell;

    if (list == NULL) {
        return;
    }

    cell.h = 0;
    cell.v = 0;
    while (LGetSelect(true, &cell, list)) {
        LSetSelect(false, cell, list);
        cell.h = 0;
        cell.v++;
    }

    if (row < 0 || row >= ListRowCount(list)) {
        return;
    }
    cell.h = 0;
    cell.v = (short)row;
    LSetSelect(true, cell, list);
    if (reveal) {
        LAutoScroll(list);
    }
}

static int SelectedListRow(ListHandle list)
{
    Cell cell;

    if (list == NULL) {
        return -1;
    }
    cell.h = 0;
    cell.v = 0;
    return LGetSelect(true, &cell, list) ? cell.v : -1;
}

/* Which cell a point lands in, or false for the empty space below the last
   row. Only the visible cells are worth asking about, and LRect answers with
   an empty rectangle for anything outside the data bounds. */
static Boolean CellAtPoint(ListHandle list, Point where, Cell *out)
{
    ListBounds visible;
    Cell       cell;

    if (list == NULL) {
        return false;
    }
    GetListVisibleCells(list, &visible);

    cell.h = 0;
    for (cell.v = visible.top; cell.v < visible.bottom; cell.v++) {
        Rect r;

        LRect(&r, cell, list);
        if (PtInRect(where, &r)) {
            *out = cell;
            return true;
        }
    }
    return false;
}

/* ------------------------------------------------------------------ */
/* Layout                                                              */
/* ------------------------------------------------------------------ */

/*
 * Move and resize a list. SetListViewBounds places it and LSize brings the
 * scroll bar along with it — the List Manager derives the bar's rectangle
 * from the view, so the order matters. The cell is always as wide as the
 * view, which is what keeps the list from ever wanting to scroll sideways.
 */
static void SizeList(ListHandle list, const Rect *view)
{
    Point cell;

    if (list == NULL) {
        return;
    }
    LSetDrawingMode(false, list);
    SetListViewBounds(list, view);
    LSize((short)(view->right - view->left),
          (short)(view->bottom - view->top), list);
    cell.v = kRowHeight;
    cell.h = (short)(view->right - view->left);
    LCellSize(cell, list);
    LSetDrawingMode(true, list);
}

/* The list's view inside a framed pane: the scroll bar takes the right hand
   edge, and a pixel top and bottom is left for it, because the List Manager
   draws a bar one pixel taller than the view at each end. */
static void ViewInPane(const Rect *pane, Rect *view)
{
    SetRect(view, pane->left, (short)(pane->top + 1),
            (short)(pane->right - kScrollWidth), (short)(pane->bottom - 1));
}

static void Layout(void)
{
    Rect  bounds;
    short rightLeft;
    short listBottom;
    short contentBottom;

    if (gWindow == NULL) {
        return;
    }

    GetWindowPortBounds(gWindow, &bounds);

    contentBottom = (short)(bounds.bottom - kStatusHeight);

    /* The sidebar keeps its width until the window gets too narrow to give
       the right-hand side anything useful. */
    if (gSidebarWidth > bounds.right - bounds.left - kMaxSidebarPad) {
        gSidebarWidth = (short)(bounds.right - bounds.left - kMaxSidebarPad);
    }
    if (gSidebarWidth < kMinSidebar) {
        gSidebarWidth = kMinSidebar;
    }

    SetRect(&gSidebarHeader, bounds.left, bounds.top,
            (short)(bounds.left + gSidebarWidth),
            (short)(bounds.top + kHeaderHeight));
    SetRect(&gSidebarPane, bounds.left,
            (short)(bounds.top + kHeaderHeight),
            (short)(bounds.left + gSidebarWidth), contentBottom);
    ViewInPane(&gSidebarPane, &gSidebarRect);

    SetRect(&gVDivider, (short)(bounds.left + gSidebarWidth), bounds.top,
            (short)(bounds.left + gSidebarWidth + kDividerWidth),
            contentBottom);

    rightLeft = (short)(gVDivider.right);

    listBottom = (short)(bounds.top + kHeaderHeight +
                         (long)(contentBottom - bounds.top - kHeaderHeight) *
                         gListShare / 100);
    if (listBottom < bounds.top + kHeaderHeight + kMinListHeight) {
        listBottom = (short)(bounds.top + kHeaderHeight + kMinListHeight);
    }
    if (listBottom > contentBottom - kMinReader - kDividerWidth) {
        listBottom = (short)(contentBottom - kMinReader - kDividerWidth);
    }

    SetRect(&gListHeader, rightLeft, bounds.top,
            bounds.right, (short)(bounds.top + kHeaderHeight));
    SetRect(&gListPane, rightLeft, (short)(bounds.top + kHeaderHeight),
            bounds.right, listBottom);
    ViewInPane(&gListPane, &gListRect);

    SetRect(&gHDivider, rightLeft, listBottom,
            bounds.right, (short)(listBottom + kDividerWidth));

    SetRect(&gReaderRect, rightLeft, (short)(listBottom + kDividerWidth),
            (short)(bounds.right - kScrollWidth), contentBottom);

    SetRect(&gStatusRect, bounds.left, contentBottom,
            bounds.right, bounds.bottom);

    SizeList(gSidebarList, &gSidebarRect);
    SizeList(gArticleList, &gListRect);

    /* The reader's bar sits in the gutter the pane leaves for it, overlapping
       the pane frame by a pixel the way Platinum does. */
    if (gReaderScroll != NULL) {
        MoveControl(gReaderScroll, (short)(gReaderRect.right - 1),
                    gReaderRect.top);
        SizeControl(gReaderScroll, (short)(kScrollWidth + 1),
                    (short)(gReaderRect.bottom - gReaderRect.top));
    }

    SizeReader();
}

/* ------------------------------------------------------------------ */
/* The reader pane                                                     */
/*                                                                     */
/* One styled TextEdit record holds the article. TextEdit is what the  */
/* Toolbox has for wrapped, styled prose, and it replaces a greedy     */
/* word wrapper, a line index and a drawing loop that between them did */
/* the same job less well: they capped an article at five hundred      */
/* lines, they measured every line again on every redraw, and there    */
/* was no way to select a word of it.                                  */
/*                                                                     */
/* The record is kept inactive unless a drag is selecting something.    */
/* An active TextEdit record with an empty selection draws an          */
/* insertion point, and a caret in a pane that cannot be typed into is */
/* a lie about what the pane is.                                       */
/* ------------------------------------------------------------------ */

/* The view is the pane less its margin. The destination rectangle is the
   same box, and it is its top that moves when the article is scrolled. */
static void ReaderRects(Rect *view)
{
    SetRect(view, (short)(gReaderRect.left + kTextInset),
            (short)(gReaderRect.top + kReaderMargin),
            (short)(gReaderRect.right - kTextInset),
            (short)(gReaderRect.bottom - kReaderMargin));
}

/* How far down the article the view has been scrolled, in pixels. Styled
   text has no one line height to count in, so the reader's scroll bar is
   measured in pixels where the lists' are measured in rows. */
static short ReaderOffset(void)
{
    if (gReaderTE == NULL) {
        return 0;
    }
    return (short)((**gReaderTE).viewRect.top - (**gReaderTE).destRect.top);
}

static short ReaderMaxOffset(void)
{
    long height;
    long view;

    if (gReaderTE == NULL) {
        return 0;
    }
    height = TEGetHeight((**gReaderTE).nLines, 0, gReaderTE);
    view   = (**gReaderTE).viewRect.bottom - (**gReaderTE).viewRect.top;

    if (height <= view) {
        return 0;
    }
    height -= view;
    return (short)((height > 32767) ? 32767 : height);
}

/* A page keeps one line of context, the way the lists' page keys do. */
static short ReaderPage(void)
{
    short page;

    if (gReaderTE == NULL) {
        return kReaderLead;
    }
    page = (short)((**gReaderTE).viewRect.bottom -
                   (**gReaderTE).viewRect.top - kReaderLead);
    return (page < kReaderLead) ? kReaderLead : page;
}

static void SyncReaderScroll(void)
{
    short max = ReaderMaxOffset();

    if (gReaderScroll == NULL) {
        return;
    }
    SetControlMaximum(gReaderScroll, max);
    SetControlValue(gReaderScroll, ReaderOffset());
    HiliteControl(gReaderScroll, (max > 0) ? 0 : 255);
}

/* Scroll to an offset. TEScroll draws the strip that comes into view, which
   is why this needs the port and not just the arithmetic. */
static void ScrollReaderTo(short offset)
{
    RgnHandle save = NULL;
    short     max;
    short     now;

    if (gWindow == NULL || gReaderTE == NULL) {
        return;
    }
    max = ReaderMaxOffset();
    now = ReaderOffset();

    if (offset < 0)   offset = 0;
    if (offset > max) offset = max;
    if (offset == now) {
        return;
    }

    SetPortWindowPort(gWindow);
    save = NewRgn();
    if (save != NULL) {
        GetClip(save);
    }
    ClipRect(&gReaderRect);

    TEScroll(0, (short)(now - offset), gReaderTE);

    if (save != NULL) {
        SetClip(save);
        DisposeRgn(save);
    }
    if (gReaderScroll != NULL) {
        SetControlValue(gReaderScroll, offset);
    }
}

/* ------------------------------------------------------------------ */

static size_t AppendText(size_t used, const char *text, size_t len)
{
    return used + gz_copy_n(gReaderText + used, sizeof gReaderText - used,
                            text, len);
}

static size_t AppendChar(size_t used, char c)
{
    if (used + 1 < sizeof gReaderText) {
        gReaderText[used++] = c;
        gReaderText[used]   = '\0';
    }
    return used;
}

/*
 * The body arrives with its paragraphs marked by newlines. TextEdit breaks
 * on carriage returns, and a paragraph wants a blank line after it, so each
 * run of newlines becomes exactly two — however many the extractor left.
 */
static size_t AppendBody(size_t used, const char *body)
{
    const char *p = body;

    while (*p != '\0') {
        if (*p == '\n') {
            while (*p == '\n') {
                p++;
            }
            if (*p == '\0') {
                break;
            }
            used = AppendChar(used, '\r');
            used = AppendChar(used, '\r');
            continue;
        }
        used = AppendChar(used, *p++);
    }
    return used;
}

/* One of the three weights the pane has always had, applied to a range that
   is already in the record. Setting a style on an insertion point and
   trusting the next TEInsert to pick it up is documented but delicate;
   styling text that is already there cannot be misread. */
static void ApplyRunStyle(long start, long end, short size, short face)
{
    TextStyle style;

    if (gReaderTE == NULL || end <= start) {
        return;
    }
    style.tsFont = kFontIDGeneva;
    style.tsFace = face;
    style.tsSize = size;
    style.tsColor.red   = 0;
    style.tsColor.green = 0;
    style.tsColor.blue  = 0;

    TESetSelect(start, end, gReaderTE);
    TESetStyle(doFont | doFace | doSize, &style, false, gReaderTE);
}

/*
 * Compose the article and hand it to TextEdit. The text is copied out of
 * the store rather than pointed into: a refresh replaces the articles, and
 * a text handle into freed headlines is the kind of bug that shows up as
 * garbage on screen days later.
 */
static void SetReaderText(void)
{
    const GazetteArticle *a;
    GrafPtr savePort;
    Rect    view;
    size_t  used      = 0;
    long    titleEnd  = 0;
    long    bylineEnd = 0;
    char    when[16];

    if (gWindow == NULL || gReaderTE == NULL) {
        return;
    }

    GetPort(&savePort);
    SetPortWindowPort(gWindow);

    TEDeactivate(gReaderTE);
    gReaderText[0] = '\0';

    a = GazetteFeedsArticleAt(gSelectedArticle);
    if (a == NULL) {
        static const char kNothing[] = "Select a headline to read it.";

        used = AppendText(0, kNothing, sizeof kNothing - 1);
        TESetText(gReaderText, (long)used, gReaderTE);
        ApplyRunStyle(0, (long)used, 10, normal);
    } else {
        used     = AppendText(0, a->title, strlen(a->title));
        titleEnd = (long)used;

        GazetteFormatDate(a->date, UnixNow(), when, sizeof when);
        {
            char        byline[192];
            const char *from = a->source;

            /* In a group view the articles come from several feeds, so which
               one this is from is worth saying. The feed's own name stands
               in when the article does not name a publisher. */
            if (from[0] == '\0' && GazetteFeedsCurrentGroup() >= 0) {
                from = GazetteCoreFeedTitle(a->feed);
            }

            if (from[0] != '\0' && when[0] != '\0') {
                snprintf(byline, sizeof byline, "%s - %s", from, when);
            } else if (from[0] != '\0') {
                snprintf(byline, sizeof byline, "%s", from);
            } else {
                snprintf(byline, sizeof byline, "%s", when);
            }

            if (byline[0] != '\0') {
                used = AppendChar(used, '\r');
                used = AppendText(used, byline, strlen(byline));
            }
        }
        bylineEnd = (long)used;

        /*
         * The article's own page when it has been fetched and extracted, and
         * the feed's summary otherwise. The store answers which article the
         * held text belongs to, so switching articles cannot show the last
         * one's body under this one's headline.
         */
        {
            const char *body = a->body;

            if (GazetteFeedsFullTextArticle() == gSelectedArticle) {
                const char *full = GazetteFeedsFullText();

                if (full[0] != '\0') {
                    body = full;
                }
            }

            used = AppendChar(used, '\r');
            used = AppendChar(used, '\r');
            if (body[0] != '\0') {
                used = AppendBody(used, body);
            } else {
                static const char kNone[] = "(This feed carries no summary "
                                            "for this article.)";

                used = AppendText(used, kNone, sizeof kNone - 1);
            }
        }

        TESetText(gReaderText, (long)used, gReaderTE);

        ApplyRunStyle(0, titleEnd, 9, bold);
        ApplyRunStyle(titleEnd, bylineEnd, 9, normal);
        ApplyRunStyle(bylineEnd, (long)used, 10, normal);
    }

    TESetSelect(0, 0, gReaderTE);

    /* Back to the top, and the wrap and the bar back in step with the new
       length. */
    ReaderRects(&view);
    (**gReaderTE).destRect = view;
    TECalText(gReaderTE);
    SyncReaderScroll();

    SetPort(savePort);
}

/* The pane has moved or changed width, so the text has to be laid out again
   in it. The article keeps its place as far as the new wrap allows, which is
   what makes dragging the divider feel like resizing rather than rewinding. */
static void SizeReader(void)
{
    GrafPtr savePort;
    Rect    view;
    short   was;
    short   max;

    if (gWindow == NULL || gReaderTE == NULL) {
        return;
    }

    GetPort(&savePort);
    SetPortWindowPort(gWindow);

    was = ReaderOffset();

    ReaderRects(&view);
    (**gReaderTE).viewRect = view;
    (**gReaderTE).destRect = view;
    TECalText(gReaderTE);

    max = ReaderMaxOffset();
    if (was > max) {
        was = max;
    }
    if (was < 0) {
        was = 0;
    }
    (**gReaderTE).destRect.top = (short)(view.top - was);

    SyncReaderScroll();
    SetPort(savePort);
}

/* ------------------------------------------------------------------ */
/* Scrolling                                                           */
/*                                                                     */
/* Only the reader's bar comes through here. The two lists have scroll  */
/* bars of their own and LClick tracks them.                            */
/* ------------------------------------------------------------------ */

static pascal void ScrollAction(ControlRef control, ControlPartCode part)
{
    short delta = 0;

    if (control == NULL || part == 0 || gReaderTE == NULL) {
        return;
    }

    switch (part) {
        case kControlUpButtonPart:   delta = (short)-kReaderLead;  break;
        case kControlDownButtonPart: delta = kReaderLead;          break;
        case kControlPageUpPart:     delta = (short)-ReaderPage(); break;
        case kControlPageDownPart:   delta = ReaderPage();         break;
        default: return;
    }

    /* Scrolled here rather than invalidated: this runs inside TrackControl's
       own loop, and an update event would not be seen until it returned. */
    ScrollReaderTo((short)(ReaderOffset() + delta));
}

/* ------------------------------------------------------------------ */
/* Drawing                                                             */
/* ------------------------------------------------------------------ */

static void DrawHeader(const Rect *r, const char *text)
{
    Rect inner = *r;

    DrawThemePlacard(r, kThemeStateActive);

    TextFont(kFontIDGeneva);
    TextSize(9);
    TextFace(bold);

    inner.left  = (short)(inner.left + kTextInset + 2);
    inner.right = (short)(inner.right - kTextInset);
    MoveTo(inner.left, (short)(r->top + 12));
    DrawTruncated(text, (short)(inner.right - inner.left));

    TextFace(normal);
}

/*
 * A selected row. A pane the keyboard is driving inverts its selection; one
 * it is not outlines the same rectangle instead. That is the List Manager's
 * own convention for an inactive list, and without it two panes both showing
 * a solid black bar leave no way to tell which one the arrow keys will move.
 */
static void HighlightRow(const Rect *row, Boolean focused)
{
    if (focused) {
        InvertRect(row);
        return;
    }
    PenNormal();
    SetThemeTextColor(kThemeTextColorListView, 8, true);
    FrameRect(row);
    ForeColor(blackColor);
}

/* A white list area with the Platinum list-box frame around it. Used by the
   reader, which is not a list but is framed like one. */
static void BeginListArea(const Rect *r, RgnHandle *saveClip)
{
    Rect frame = *r;

    *saveClip = NewRgn();
    if (*saveClip != NULL) {
        GetClip(*saveClip);
    }

    frame.right = (short)(frame.right + 1);     /* the scroll bar overlaps */
    DrawThemeListBoxFrame(&frame, kThemeStateActive);

    SetThemeBackground(kThemeBrushWhite, 8, true);
    EraseRect(r);
    ClipRect(r);
}

static void EndListArea(RgnHandle saveClip)
{
    SetThemeBackground(kThemeBrushDialogBackgroundActive, 8, true);
    if (saveClip != NULL) {
        SetClip(saveClip);
        DisposeRgn(saveClip);
    }
}

/*
 * Platinum's disclosure triangle, drawn by the Appearance Manager rather
 * than by hand. It is the CDEF's own artwork without the control: a real
 * CDEF 4 would have to be created, moved, hidden and disposed of on every
 * scroll and every collapse, because there is one per group and they travel
 * with the rows. DrawThemeButton gives the same nine pixels and tracks the
 * theme, which drawing it here never did.
 */
static void DrawDisclosure(const Rect *cell, short left, Boolean open)
{
    Rect                box;
    ThemeButtonDrawInfo info;
    short               inset;

    inset = (short)(((cell->bottom - cell->top) - kTriangleSize) / 2);
    SetRect(&box, left, (short)(cell->top + inset),
            (short)(left + kTriangleSize),
            (short)(cell->top + inset + kTriangleSize));

    info.state     = kThemeStateActive;
    info.value     = open ? kThemeDisclosureDown : kThemeDisclosureRight;
    info.adornment = kThemeAdornmentNone;

    (void)DrawThemeButton(&box, kThemeDisclosureButton, &info, NULL,
                          NULL, NULL, 0);
}

/*
 * How many of a feed are unread. The store holds one feed at a time, so for
 * every other row this comes from the index rather than from the articles —
 * and for the feed on screen it comes from the articles, which are the ones
 * that just changed.
 */
static int FeedUnread(int feed)
{
    if (feed == GazetteFeedsCurrentFeed()) {
        return GazetteFeedsUnreadCount();
    }
    return GazetteIndexFeedUnread(GazetteCoreFeedURL(feed));
}

static int GroupUnread(int group)
{
    int total = 0;
    int i;

    for (i = 0; i < GazetteCoreFeedCount(); i++) {
        if (GazetteCoreFeedGroup(i) == group && GazetteCoreFeedEnabled(i)) {
            total += FeedUnread(i);
        }
    }
    return total;
}

/* "Name (12)", with the count only when there is one. Drawn as one string so
   the truncation takes the name and never the number — the count is the part
   that has to stay legible in a narrow sidebar. */
static void DrawRowLabel(const char *name, int unread, short left,
                         short right, short baseline)
{
    char  text[kGazetteTitleLen + 16];
    short width = (short)(right - left);

    if (unread > 0) {
        char count[16];
        short countWidth;

        snprintf(count, sizeof count, " (%d)", unread);
        countWidth = (short)TextWidth(count, 0, (short)strlen(count));

        MoveTo(left, baseline);
        DrawTruncated(name, (short)(width - countWidth));
        DrawText(count, 0, (short)strlen(count));
        return;
    }

    snprintf(text, sizeof text, "%s", name);
    MoveTo(left, baseline);
    DrawTruncated(text, width);
}

/* ------------------------------------------------------------------ */
/* The list definition functions                                       */
/*                                                                     */
/* Called by the List Manager with the port set to the window and the   */
/* clip already narrowed to the list. Both are given a cell rectangle   */
/* and have to leave it looking right: erase it, draw the row the cell  */
/* number names, and highlight it when lSelect says so. Redrawing the   */
/* whole cell on lHiliteMsg as well as lDrawMsg costs one erase and     */
/* saves having a second, subtly different, drawing path.               */
/* ------------------------------------------------------------------ */

static void EraseCell(const Rect *cell)
{
    SetThemeBackground(kThemeBrushWhite, 8, true);
    EraseRect(cell);
    SetThemeBackground(kThemeBrushDialogBackgroundActive, 8, true);
}

static void DrawSidebarCell(const Rect *cell, short row, Boolean selected)
{
    GazetteSidebarRow r;
    short             textLeft;
    short             baseline;

    EraseCell(cell);
    if (!GazetteCoreSidebarRowAt(row, &r)) {
        return;
    }

    TextFont(kFontIDGeneva);
    TextSize(9);
    TextFace(normal);
    baseline = (short)(cell->top + kBaseline);
    textLeft = (short)(cell->left + kTextInset + kTriangleColumn);

    if (r.kind == kGazetteRowGroup) {
        /* The group's own line: triangle, then the name in bold, the way a
           folder reads in a Finder list view. */
        DrawDisclosure(cell, (short)(cell->left + kTextInset),
                       !GazetteCoreGroupCollapsed(r.index));

        SetThemeTextColor(kThemeTextColorListView, 8, true);
        TextFace(bold);
        DrawRowLabel(GazetteCoreGroupName(r.index), GroupUnread(r.index),
                     textLeft, (short)(cell->right - kTextInset), baseline);
    } else {
        int unread;

        if (GazetteCoreFeedGroup(r.index) >= 0) {
            textLeft = (short)(textLeft + kGroupIndent);
        }

        /* A switched-off feed keeps its place and is drawn the way an
           unavailable item is drawn anywhere else in Platinum, so it reads
           as off rather than as missing. */
        SetThemeTextColor(GazetteCoreFeedEnabled(r.index)
                              ? kThemeTextColorListView
                              : kThemeTextColorDialogInactive,
                          8, true);

        /* A feed with something unread is bold, the same signal the headline
           list uses for an unread article. A feed switched off shows no
           count: it is not being fetched, so whatever number was last
           recorded is not news. */
        unread = GazetteCoreFeedEnabled(r.index) ? FeedUnread(r.index) : 0;
        TextFace((unread > 0) ? bold : normal);
        DrawRowLabel(GazetteCoreFeedTitle(r.index), unread, textLeft,
                     (short)(cell->right - kTextInset), baseline);
    }

    TextFace(normal);
    ForeColor(blackColor);

    if (selected) {
        HighlightRow(cell, (Boolean)(gFocus == kRefSidebar));
    }
}

static pascal void SidebarLDEF(short message, Boolean isSelected, Rect *cellRect,
                               Cell cell, short dataOffset, short dataLen,
                               ListHandle list)
{
    (void)dataOffset;
    (void)dataLen;
    (void)list;

    if (message == lDrawMsg || message == lHiliteMsg) {
        DrawSidebarCell(cellRect, cell.v, isSelected);
    }
}

static void DrawArticleCell(const Rect *cell, short row, Boolean selected)
{
    const GazetteArticle *a = GazetteFeedsArticleAt(row);
    char                  when[16];
    short                 baseline;

    EraseCell(cell);
    if (a == NULL) {
        return;
    }

    TextFont(kFontIDGeneva);
    TextSize(9);
    TextFace(normal);
    baseline = (short)(cell->top + kBaseline);

    /* The date sits in a fixed column so the headlines line up; an article
       with no date simply leaves it blank rather than shifting. */
    GazetteFormatDate(a->date, UnixNow(), when, sizeof when);
    if (when[0] != '\0') {
        MoveTo((short)(cell->left + kTextInset), baseline);
        DrawTruncated(when, kDateColumn - kTextInset);
    }

    /* Unread in bold, the way every mail and news reader of the era marked
       one. The date column stays plain either way, so the weight reads as
       being about the headline rather than the row. */
    TextFace(a->read ? normal : bold);
    MoveTo((short)(cell->left + kTextInset + kDateColumn), baseline);
    DrawTruncated(a->title,
                  (short)(cell->right - cell->left - kDateColumn -
                          2 * kTextInset));
    TextFace(normal);

    if (selected) {
        HighlightRow(cell, (Boolean)(gFocus == kRefList));
    }
}

static pascal void ArticleLDEF(short message, Boolean isSelected, Rect *cellRect,
                               Cell cell, short dataOffset, short dataLen,
                               ListHandle list)
{
    (void)dataOffset;
    (void)dataLen;
    (void)list;

    if (message == lDrawMsg || message == lHiliteMsg) {
        DrawArticleCell(cellRect, cell.v, isSelected);
    }
}

/* ------------------------------------------------------------------ */
/* Drawing a pane                                                      */
/* ------------------------------------------------------------------ */

/*
 * The frame, the white behind the rows, and then the list. Erasing the view
 * first is what clears the space below the last row — LUpdate draws cells
 * and nothing else — and the bar is drawn here too, because its value and
 * its range have usually just changed.
 */
static void DrawListPane(ListHandle list, const Rect *pane, const Rect *view)
{
    RgnHandle  rgn;
    ControlRef bar;

    if (gWindow == NULL || list == NULL) {
        return;
    }
    SetPortWindowPort(gWindow);

    DrawThemeListBoxFrame(pane, kThemeStateActive);

    SetThemeBackground(kThemeBrushWhite, 8, true);
    EraseRect(view);
    SetThemeBackground(kThemeBrushDialogBackgroundActive, 8, true);

    rgn = NewRgn();
    if (rgn != NULL) {
        RectRgn(rgn, view);
        LUpdate(rgn, list);
        DisposeRgn(rgn);
    }

    bar = GetListVerticalScrollBar(list);
    if (bar != NULL) {
        Draw1Control(bar);
    }
}

static void DrawSidebarPane(void)
{
    DrawListPane(gSidebarList, &gSidebarPane, &gSidebarRect);
}

static void DrawArticlePane(void)
{
    DrawListPane(gArticleList, &gListPane, &gListRect);

    /* An empty list has no cell to say so in. */
    if (GazetteFeedsArticleCount() == 0) {
        RgnHandle clip = NewRgn();

        if (clip != NULL) {
            GetClip(clip);
        }
        ClipRect(&gListRect);

        TextFont(kFontIDGeneva);
        TextSize(9);
        TextFace(normal);
        MoveTo((short)(gListRect.left + kTextInset),
               (short)(gListRect.top + kBaseline + 1));
        if (GazetteFeedsFilter()[0] != '\0') {
            DrawString("\pNothing here matches - Edit menu, Show All.");
        } else {
            DrawString("\pNo headlines yet - press Command-R.");
        }

        if (clip != NULL) {
            SetClip(clip);
            DisposeRgn(clip);
        }
    }
}

static void DrawReader(void)
{
    RgnHandle clip = NULL;

    if (gWindow == NULL) {
        return;
    }
    SetPortWindowPort(gWindow);

    BeginListArea(&gReaderRect, &clip);
    if (gReaderTE != NULL) {
        Rect view = (**gReaderTE).viewRect;   /* not a pointer into the
                                                 handle, which can move */

        TEUpdate(&view, gReaderTE);
    }
    EndListArea(clip);
}

static void DrawStatus(void)
{
    if (gWindow == NULL) {
        return;
    }
    SetPortWindowPort(gWindow);

    SetThemeBackground(kThemeBrushDialogBackgroundActive, 8, true);
    EraseRect(&gStatusRect);

    TextFont(kFontIDGeneva);
    TextSize(9);
    TextFace(normal);

    MoveTo((short)(gStatusRect.left + kTextInset + 4),
           (short)(gStatusRect.top + 13));
    DrawTruncated(gStatus,
                  (short)(gStatusRect.right - gStatusRect.left -
                          2 * kTextInset - 8));
}

void GazetteUIUpdate(void)
{
    Rect  bounds;
    char  header[kGazetteFeedTitleLen + 32];

    if (gWindow == NULL) {
        return;
    }
    SetPortWindowPort(gWindow);
    GetWindowPortBounds(gWindow, &bounds);

    SetThemeBackground(kThemeBrushDialogBackgroundActive, 8, true);
    EraseRect(&bounds);

    DrawHeader(&gSidebarHeader, "Feeds");

    if (GazetteFeedsTotalCount() > 0) {
        const char *title  = GazetteFeedsTitle();
        int         unread = GazetteFeedsUnreadCount();

        if (title[0] == '\0') {
            title = GazetteCoreFeedTitle(gSelectedFeed);
        }

        if (GazetteFeedsFilter()[0] != '\0') {
            /* While a search is on, what is on screen is the matches, and
               saying so is more use than an unread count of the whole. */
            snprintf(header, sizeof header, "%s - \322%s\323 (%d of %d)",
                     title, GazetteFeedsFilter(),
                     GazetteFeedsArticleCount(), GazetteFeedsTotalCount());
        } else if (unread > 0) {
            /* How many are left to read is the number worth reading; the
               total is only interesting when there is nothing left. */
            snprintf(header, sizeof header, "%s (%d unread of %d)", title,
                     unread, GazetteFeedsTotalCount());
        } else {
            snprintf(header, sizeof header, "%s (%d)", title,
                     GazetteFeedsTotalCount());
        }
    } else {
        snprintf(header, sizeof header, "%s",
                 GazetteCoreFeedTitle(gSelectedFeed));
    }
    DrawHeader(&gListHeader, header);

    /* The dividers, drawn as the Appearance Manager's own separators so they
       track the theme rather than being two hard-coded greys. */
    DrawThemeSeparator(&gVDivider, kThemeStateActive);
    DrawThemeSeparator(&gHDivider, kThemeStateActive);

    DrawSidebarPane();
    DrawArticlePane();
    DrawReader();
    DrawStatus();

    if (gReaderScroll != NULL) {
        Draw1Control(gReaderScroll);
    }
}

/* ------------------------------------------------------------------ */
/* Selection                                                           */
/* ------------------------------------------------------------------ */

static void SelectArticle(int index)
{
    int count = GazetteFeedsArticleCount();

    if (count == 0) {
        gSelectedArticle = -1;
    } else {
        if (index < 0)      index = 0;
        if (index >= count) index = count - 1;
        gSelectedArticle = index;
    }

    /* Opening it is what makes it read — before the draw, so the headline
       loses its bold in the same repaint that highlights it. */
    if (gSelectedArticle >= 0) {
        GazetteFeedsMarkRead(gSelectedArticle, 1);
    }

    SelectRow(gArticleList, gSelectedArticle, true);
    SetReaderText();

    DrawArticlePane();
    DrawReader();
    if (gReaderScroll != NULL) {
        Draw1Control(gReaderScroll);
    }

    /* Last, so the pane is already showing the summary when the shell decides
       whether to go and fetch anything better. */
    if (gSelectedArticle >= 0 && gOnArticleChosen != NULL) {
        gOnArticleChosen(gSelectedArticle);
    }
}

/* ------------------------------------------------------------------ */
/* Dividers                                                            */
/* ------------------------------------------------------------------ */

/*
 * Drag a divider. Tracking the mouse in a loop here is the same shape as
 * DragWindow or GrowWindow: the Toolbox does it this way, it ends when the
 * button comes up, and no network work is pending while a mouse button is
 * held anyway.
 */
static void TrackDivider(Point where, Boolean vertical)
{
    Rect  bounds;
    Point pt;

    GetWindowPortBounds(gWindow, &bounds);
    (void)where;

    while (StillDown()) {
        GetMouse(&pt);

        if (vertical) {
            short want = pt.h;

            if (want < kMinSidebar) {
                want = kMinSidebar;
            }
            if (want > bounds.right - kMaxSidebarPad) {
                want = (short)(bounds.right - kMaxSidebarPad);
            }
            if (want != gSidebarWidth) {
                gSidebarWidth = want;
                Layout();
                GazetteUIUpdate();
            }
        } else {
            short usable = (short)(bounds.bottom - kStatusHeight -
                                   bounds.top - kHeaderHeight);
            short share;

            if (usable <= 0) {
                break;
            }
            share = (short)((long)(pt.v - bounds.top - kHeaderHeight) * 100 /
                            usable);
            if (share < 10)  share = 10;
            if (share > 90)  share = 90;
            if (share != gListShare) {
                gListShare = share;
                Layout();
                GazetteUIUpdate();
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* Events                                                              */
/* ------------------------------------------------------------------ */

/* Do what a click on this row would do. The highlight is the list's own
   business now; this is only the part the rest of the application cares
   about. */
static void ChooseRow(const GazetteSidebarRow *row)
{
    if (row->kind == kGazetteRowGroup) {
        if (row->index == gSelectedGroup) {
            return;
        }
        gSelectedGroup = row->index;
        if (gOnGroupChosen != NULL) {
            gOnGroupChosen(row->index);
        }
        return;
    }

    if (row->index != gSelectedFeed || gSelectedGroup >= 0) {
        gSelectedFeed  = row->index;
        gSelectedGroup = -1;
        if (gOnFeedChosen != NULL) {
            gOnFeedChosen(row->index);
        }
    }
}

/* A group has opened or shut, so the rows below it have moved. */
static void SidebarRowsChanged(void)
{
    int row;

    LSetDrawingMode(false, gSidebarList);
    SetRowCount(gSidebarList, GazetteCoreSidebarRowCount());

    row = (gSelectedGroup >= 0)
              ? GazetteCoreSidebarRowForGroup(gSelectedGroup)
              : GazetteCoreSidebarRowForFeed(gSelectedFeed);
    SelectRow(gSidebarList, row, false);

    LSetDrawingMode(true, gSidebarList);
    DrawSidebarPane();
}

static void SidebarClicked(Point where, EventModifiers modifiers)
{
    Cell              cell;
    GazetteSidebarRow row;
    int               at;

    SetFocus(kRefSidebar);

    /* The triangle's own column opens and shuts a group; the rest of the
       line selects it, the way a folder behaves in a list view. This has to
       be asked before LClick, which would otherwise start tracking a
       selection out of the click. */
    if (CellAtPoint(gSidebarList, where, &cell) &&
        GazetteCoreSidebarRowAt(cell.v, &row) &&
        row.kind == kGazetteRowGroup &&
        where.h < gSidebarRect.left + kTextInset + kTriangleColumn) {
        GazetteCoreSetGroupCollapsed(row.index,
                                     !GazetteCoreGroupCollapsed(row.index));
        SidebarRowsChanged();
        return;
    }

    (void)LClick(where, modifiers, gSidebarList);

    at = SelectedListRow(gSidebarList);
    if (at < 0) {
        /* A click in the empty space below the last row clears the
           selection. Nothing has been chosen, so put the old one back
           rather than leaving the sidebar looking as though nothing is. */
        SelectRow(gSidebarList, SelectedRow(), false);
        DrawSidebarPane();
        return;
    }

    if (GazetteCoreSidebarRowAt(at, &row)) {
        ChooseRow(&row);
    }
}

/*
 * A drag in the article selects text, which is the one thing the pane is for
 * besides being read. TextEdit will not hilite a selection in a record it
 * thinks is inactive, so it is woken for the drag and put back to sleep when
 * the drag turns out to have been a plain click — otherwise the pane would
 * be left showing an insertion point it can do nothing with.
 */
static void ReaderClick(Point where, EventModifiers modifiers)
{
    if (gReaderTE == NULL) {
        return;
    }
    TEActivate(gReaderTE);
    TEClick(where, (Boolean)((modifiers & shiftKey) != 0), gReaderTE);

    if ((**gReaderTE).selStart == (**gReaderTE).selEnd) {
        TEDeactivate(gReaderTE);
    }
}

void GazetteUIClick(Point where, EventModifiers modifiers)
{
    ControlRef       control = NULL;
    ControlPartCode  part;

    if (gWindow == NULL) {
        return;
    }
    SetPortWindowPort(gWindow);

    /* The lists come first, and their panes include their scroll bars: LClick
       tracks a bar as readily as it tracks a drag through the rows, and
       FindControl would otherwise take the click off it. */
    if (PtInRect(where, &gSidebarPane)) {
        SidebarClicked(where, modifiers);
        return;
    }

    if (PtInRect(where, &gListPane)) {
        SetFocus(kRefList);
        (void)LClick(where, modifiers, gArticleList);
        {
            int row = SelectedListRow(gArticleList);

            if (row >= 0) {
                SelectArticle(row);
            } else if (gSelectedArticle >= 0) {
                SelectRow(gArticleList, gSelectedArticle, false);
                DrawArticlePane();
            }
        }
        return;
    }

    if (PtInRect(where, &gVDivider)) {
        TrackDivider(where, true);
        return;
    }
    if (PtInRect(where, &gHDivider)) {
        TrackDivider(where, false);
        return;
    }

    part = FindControl(where, gWindow, &control);
    if (control != NULL && part != 0) {
        /* Only the reader's bar is left to find. */
        SetFocus(kRefReader);

        if (part == kControlIndicatorPart) {
            /* The thumb tracks itself; the pane is redrawn once it lands. */
            if (TrackControl(control, where, NULL) == kControlIndicatorPart) {
                DrawReader();
            }
        } else {
            TrackControl(control, where, gScrollUPP);
        }
        return;
    }

    if (PtInRect(where, &gReaderRect)) {
        SetFocus(kRefReader);
        ReaderClick(where, modifiers);
        return;
    }
}

/* ------------------------------------------------------------------ */
/* The keyboard                                                        */
/*                                                                     */
/* Tab moves the focus on, the arrow keys drive whichever pane has it,  */
/* and the space bar pages the article wherever the focus happens to    */
/* be — reading is what the window is for, and a reader should not have */
/* to aim at a pane first.                                              */
/* ------------------------------------------------------------------ */

/* The row the sidebar's selection is drawn on, or 0 when it has none. */
static int SelectedRow(void)
{
    int row;

    if (gSelectedGroup >= 0) {
        row = GazetteCoreSidebarRowForGroup(gSelectedGroup);
    } else {
        row = GazetteCoreSidebarRowForFeed(gSelectedFeed);
    }
    return (row >= 0) ? row : 0;
}

static void MoveSidebar(int to)
{
    GazetteSidebarRow row;
    int               count = GazetteCoreSidebarRowCount();

    if (count == 0) {
        return;
    }
    if (to < 0) {
        to = 0;
    }
    if (to >= count) {
        to = count - 1;
    }
    if (!GazetteCoreSidebarRowAt(to, &row)) {
        return;
    }

    SelectRow(gSidebarList, to, true);
    ChooseRow(&row);
}

static Boolean SidebarKey(short key)
{
    GazetteSidebarRow row;
    int               at   = SelectedRow();
    short             page = ListPageSize(gSidebarList);

    switch (key) {
        case 0x1E: MoveSidebar(at - 1);    return true;   /* up    */
        case 0x1F: MoveSidebar(at + 1);    return true;   /* down  */
        case 0x0B: MoveSidebar(at - page); return true;   /* pg up */
        case 0x0C: MoveSidebar(at + page); return true;   /* pg dn */
        case 0x01: MoveSidebar(0);         return true;   /* home  */
        case 0x04: MoveSidebar(GazetteCoreSidebarRowCount() - 1);
                   return true;                           /* end   */

        /* Left and right work the disclosure triangle, the way they do in a
           Finder list view. On a feed, left goes out to the group it is in. */
        case 0x1C:                                        /* left  */
            if (!GazetteCoreSidebarRowAt(at, &row)) {
                return true;
            }
            if (row.kind == kGazetteRowGroup) {
                if (!GazetteCoreGroupCollapsed(row.index)) {
                    GazetteCoreSetGroupCollapsed(row.index, true);
                    SidebarRowsChanged();
                }
            } else if (GazetteCoreFeedGroup(row.index) >= 0) {
                MoveSidebar(GazetteCoreSidebarRowForGroup(
                                GazetteCoreFeedGroup(row.index)));
            }
            return true;

        case 0x1D:                                        /* right */
            if (!GazetteCoreSidebarRowAt(at, &row)) {
                return true;
            }
            if (row.kind == kGazetteRowGroup &&
                GazetteCoreGroupCollapsed(row.index)) {
                GazetteCoreSetGroupCollapsed(row.index, false);
                SidebarRowsChanged();
            }
            return true;

        default:
            return false;
    }
}

static Boolean ListKey(short key)
{
    int count = GazetteFeedsArticleCount();

    if (count == 0) {
        return false;
    }

    switch (key) {
        case 0x1E:
            SelectArticle((gSelectedArticle <= 0) ? 0 : gSelectedArticle - 1);
            return true;
        case 0x1F:
            SelectArticle((gSelectedArticle < 0) ? 0 : gSelectedArticle + 1);
            return true;
        case 0x0B:
            SelectArticle(gSelectedArticle - ListPageSize(gArticleList));
            return true;
        case 0x0C:
            SelectArticle(gSelectedArticle + ListPageSize(gArticleList));
            return true;
        case 0x01: SelectArticle(0);          return true;
        case 0x04: SelectArticle(count - 1);  return true;
        default:   return false;
    }
}

/* What the reader's own arrows and page regions do, from the keyboard. */
static Boolean ScrollReader(short delta, Boolean absolute)
{
    if (gReaderTE == NULL) {
        return false;
    }
    ScrollReaderTo(absolute ? delta : (short)(ReaderOffset() + delta));
    return true;                /* used the key, even with nowhere to go */
}

static Boolean ReaderKey(short key)
{
    switch (key) {
        case 0x1E: return ScrollReader((short)-kReaderLead, false);
        case 0x1F: return ScrollReader(kReaderLead, false);
        case 0x0B: return ScrollReader((short)-ReaderPage(), false);
        case 0x0C: return ScrollReader(ReaderPage(), false);
        case 0x01: return ScrollReader(0, true);
        case 0x04: return ScrollReader(32767, true);
        default:   return false;
    }
}

static void SetFocus(short pane)
{
    if (gFocus == pane) {
        return;
    }

    /* A selection left behind in a pane that no longer has the focus is a
       highlight with nothing driving it. */
    if (gFocus == kRefReader && gReaderTE != NULL) {
        SetPortWindowPort(gWindow);
        TEDeactivate(gReaderTE);
        TESetSelect(0, 0, gReaderTE);
    }
    gFocus = pane;

    /* Both lists, because the one losing the focus has to stop looking as
       though it has it. */
    DrawSidebarPane();
    DrawArticlePane();
}

Boolean GazetteUIKey(short key, EventModifiers modifiers)
{
    if (gWindow == NULL) {
        return false;
    }

    if (key == '\t') {
        switch (gFocus) {
            case kRefSidebar: SetFocus(kRefList);    break;
            case kRefList:    SetFocus(kRefReader);  break;
            default:          SetFocus(kRefSidebar); break;
        }
        return true;
    }

    /* The space bar pages the article from anywhere: it is the one key a
       reader reaches for without looking, and making it depend on which pane
       has the focus would be a puzzle rather than a shortcut. */
    if (key == ' ') {
        short page = ReaderPage();

        if ((modifiers & shiftKey) != 0) {
            page = (short)-page;
        }
        return ScrollReader(page, false);
    }

    switch (gFocus) {
        case kRefSidebar: return SidebarKey(key);
        case kRefList:    return ListKey(key);
        default:          return ReaderKey(key);
    }
}

void GazetteUIActivate(Boolean active)
{
    if (gWindow == NULL) {
        return;
    }

    /* LActivate greys a list's scroll bar and redraws its selection the way
       an inactive list shows one. */
    if (gSidebarList != NULL) {
        LActivate(active, gSidebarList);
    }
    if (gArticleList != NULL) {
        LActivate(active, gArticleList);
    }

    /* A selection is only meaningful while the window is in front. */
    if (!active && gReaderTE != NULL) {
        SetPortWindowPort(gWindow);
        TEDeactivate(gReaderTE);
    }

    /* The Control Manager greys the reader's bar for us; 255 is the inactive
       hilite state and 0 the active one. A disabled bar stays disabled. */
    if (gReaderScroll != NULL) {
        HiliteControl(gReaderScroll,
                      (active && GetControlMaximum(gReaderScroll) > 0) ? 0 : 255);
    }
}

void GazetteUIResized(void)
{
    Rect bounds;

    if (gWindow == NULL) {
        return;
    }
    Layout();
    SetPortWindowPort(gWindow);
    GetWindowPortBounds(gWindow, &bounds);
    InvalWindowRect(gWindow, &bounds);
}

/* ------------------------------------------------------------------ */
/* Notifications from the shell                                        */
/* ------------------------------------------------------------------ */

void GazetteUISetStatus(const char *text)
{
    if (text == NULL) {
        text = "";
    }
    if (strcmp(gStatus, text) == 0) {
        return;                             /* nothing to repaint */
    }
    strncpy(gStatus, text, sizeof gStatus - 1);
    gStatus[sizeof gStatus - 1] = '\0';

    if (gWindow != NULL) {
        DrawStatus();
    }
}

void GazetteUIArticlesChanged(void)
{
    if (gWindow == NULL) {
        return;
    }

    gSelectedArticle = (GazetteFeedsArticleCount() > 0) ? 0 : -1;

    LSetDrawingMode(false, gArticleList);
    SetRowCount(gArticleList, GazetteFeedsArticleCount());
    LScroll(0, (short)-ListRowCount(gArticleList), gArticleList);
    SelectRow(gArticleList, gSelectedArticle, false);
    LSetDrawingMode(true, gArticleList);

    Layout();
    SetReaderText();
    if (gSelectedArticle >= 0) {
        GazetteFeedsMarkRead(gSelectedArticle, 1);
    }
    GazetteUIUpdate();

    /* The first article is open now, exactly as if it had been clicked. */
    if (gSelectedArticle >= 0 && gOnArticleChosen != NULL) {
        gOnArticleChosen(gSelectedArticle);
    }
}

void GazetteUIArticleTextChanged(void)
{
    if (gWindow == NULL) {
        return;
    }
    SetReaderText();
    DrawReader();

    /* The scroll bar's own frame is outside the pane DrawReader repaints. */
    if (gReaderScroll != NULL) {
        Draw1Control(gReaderScroll);
    }
}

Boolean GazetteUIReaderHasSelection(void)
{
    if (gReaderTE == NULL) {
        return false;
    }
    return (Boolean)((**gReaderTE).selStart != (**gReaderTE).selEnd);
}

void GazetteUIReaderCopy(void)
{
    if (!GazetteUIReaderHasSelection()) {
        return;
    }
    /* TECopy puts the text on TextEdit's own private scrap; TEToScrap is
       what moves it to the system's, so another application can have it.
       ZeroScrap is not in Carbon — ClearCurrentScrap is what replaced it. */
    (void)ClearCurrentScrap();
    TECopy(gReaderTE);
    (void)TEToScrap();
}

int GazetteUISelectedArticle(void)
{
    return gSelectedArticle;
}

void GazetteUIFeedsChanged(void)
{
    if (gWindow == NULL) {
        return;
    }
    if (gSelectedFeed >= GazetteCoreFeedCount()) {
        gSelectedFeed = 0;
    }
    if (gSelectedGroup >= GazetteCoreGroupCount()) {
        gSelectedGroup = -1;
    }

    LSetDrawingMode(false, gSidebarList);
    SetRowCount(gSidebarList, GazetteCoreSidebarRowCount());
    SelectRow(gSidebarList, SelectedRow(), false);
    LSetDrawingMode(true, gSidebarList);

    Layout();
    GazetteUIUpdate();
}

int GazetteUISelectedFeed(void)
{
    return gSelectedFeed;
}

Boolean GazetteUISelection(int *kind, int *index)
{
    if (kind == NULL || index == NULL) {
        return false;
    }
    if (gSelectedGroup >= 0 && gSelectedGroup < GazetteCoreGroupCount()) {
        *kind  = kGazetteRowGroup;
        *index = gSelectedGroup;
        return true;
    }
    if (gSelectedFeed >= 0 && gSelectedFeed < GazetteCoreFeedCount()) {
        *kind  = kGazetteRowFeed;
        *index = gSelectedFeed;
        return true;
    }
    return false;
}

void GazetteUISelectGroup(int index)
{
    if (index < 0 || index >= GazetteCoreGroupCount()) {
        return;
    }
    gSelectedGroup = index;
    SelectRow(gSidebarList, GazetteCoreSidebarRowForGroup(index), true);
    if (gWindow != NULL) {
        DrawSidebarPane();
    }
}

void GazetteUISelectFeed(int index)
{
    if (index < 0 || index >= GazetteCoreFeedCount()) {
        return;
    }
    gSelectedFeed  = index;
    gSelectedGroup = -1;
    /* -1 when the feed's group is shut: it is still the selection, there is
       just no row to select it on. */
    SelectRow(gSidebarList, GazetteCoreSidebarRowForFeed(index), true);
    if (gWindow != NULL) {
        DrawSidebarPane();
    }
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

/*
 * A list with a definition function of our own rather than an 'LDEF'
 * resource — the only way to have one under Carbon, and the reason this is
 * CreateCustomList and not LNew. It starts with no rows; the shell fills it
 * in through GazetteUIFeedsChanged and GazetteUIArticlesChanged.
 */
static ListHandle MakeList(ListDefUPP defProc, const Rect *view)
{
    ListDefSpec spec;
    ListBounds  data;
    ListHandle  list = NULL;
    Point       cell;

    spec.defType    = kListDefUserProcType;
    spec.u.userProc = defProc;

    SetRect(&data, 0, 0, 1, 0);         /* one column, no rows yet */
    cell.v = kRowHeight;
    cell.h = (short)(view->right - view->left);

    if (CreateCustomList(view, &data, cell, &spec, gWindow,
                         false, false, false, true, &list) != noErr) {
        return NULL;
    }

    /* One row at a time, and no drag-selecting several. lOnlyOne is a
       negative constant in a byte-wide field, so it is masked rather than
       sign-extended into the flags word. */
    SetListSelectionFlags(list, (OptionBits)(lOnlyOne & 0xFF));
    return list;
}

static ControlRef MakeScroll(long reference)
{
    Rect r;

    /* Placed properly by Layout(); this only has to be a legal rectangle. */
    SetRect(&r, 0, 0, kScrollWidth, 64);
    return NewControl(gWindow, &r, "\p", true, 0, 0, 0,
                      kControlScrollBarProc, reference);
}

Boolean GazetteUIOpen(GazetteUIFeedChosen onFeedChosen,
                      GazetteUIArticleChosen onArticleChosen,
                      GazetteUIGroupChosen onGroupChosen)
{
    OSStatus         err;
    Rect             bounds;
    WindowAttributes attrs;

    if (gWindow != NULL) {
        return true;
    }

    gOnFeedChosen    = onFeedChosen;
    gOnArticleChosen = onArticleChosen;
    gOnGroupChosen   = onGroupChosen;

    SetRect(&bounds, 40, 48, 40 + 620, 48 + 420);

    /* No kWindowStandardHandlerAttribute: that installs the Carbon Event
       Manager's standard handler, which would compete with the
       WaitNextEvent loop in main.cpp. */
    attrs = kWindowStandardDocumentAttributes;

    err = CreateNewWindow(kDocumentWindowClass, attrs, &bounds, &gWindow);
    if (err != noErr || gWindow == NULL) {
        gWindow = NULL;
        return false;
    }

    SetWTitle(gWindow, "\pGazette");
    SetThemeWindowBackground(gWindow, kThemeBrushDialogBackgroundActive, false);
    SetPortWindowPort(gWindow);

    gScrollUPP    = NewControlActionUPP(ScrollAction);
    gSidebarLDEF  = NewListDefUPP(SidebarLDEF);
    gArticleLDEF  = NewListDefUPP(ArticleLDEF);

    /* Lay the rectangles out before the lists, so each one is born the size
       it will be drawn at; Layout() then keeps them there. */
    gSelectedFeed    = 0;
    gSelectedArticle = -1;
    gSelectedGroup   = -1;
    Layout();

    gSidebarList = MakeList(gSidebarLDEF, &gSidebarRect);
    gArticleList = MakeList(gArticleLDEF, &gListRect);
    if (gSidebarList == NULL || gArticleList == NULL) {
        GazetteUIClose();
        return false;
    }

    gReaderScroll = MakeScroll(kRefReader);

    /* TEStyleNew remembers the port it was made in, so the window's has to
       be current; the font it is holding becomes the record's default. */
    {
        Rect view;

        ReaderRects(&view);
        TextFont(kFontIDGeneva);
        TextSize(10);
        TextFace(normal);
        gReaderTE = TEStyleNew(&view, &view);
    }
    if (gReaderTE == NULL) {
        GazetteUIClose();
        return false;
    }
    /* The scroll offset is tracked here, so TextEdit must not scroll behind
       our back when a drag runs off the bottom of the pane. */
    TEAutoView(false, gReaderTE);

    SetRowCount(gSidebarList, GazetteCoreSidebarRowCount());
    SelectRow(gSidebarList, SelectedRow(), false);

    Layout();
    SetReaderText();

    ShowWindow(gWindow);
    SelectWindow(gWindow);
    return true;
}

void GazetteUIClose(void)
{
    if (gWindow == NULL) {
        return;
    }

    /* The lists own their scroll bars, so they go before the window does. */
    if (gSidebarList != NULL) {
        LDispose(gSidebarList);
        gSidebarList = NULL;
    }
    if (gArticleList != NULL) {
        LDispose(gArticleList);
        gArticleList = NULL;
    }

    if (gReaderTE != NULL) {
        TEDispose(gReaderTE);
        gReaderTE = NULL;
    }

    /* DisposeWindow takes the remaining controls with it; the UPPs are
       ours. */
    DisposeWindow(gWindow);
    gWindow       = NULL;
    gReaderScroll = NULL;

    if (gScrollUPP != NULL) {
        DisposeControlActionUPP(gScrollUPP);
        gScrollUPP = NULL;
    }
    if (gSidebarLDEF != NULL) {
        DisposeListDefUPP(gSidebarLDEF);
        gSidebarLDEF = NULL;
    }
    if (gArticleLDEF != NULL) {
        DisposeListDefUPP(gArticleLDEF);
        gArticleLDEF = NULL;
    }
}

WindowRef GazetteUIWindow(void)
{
    return gWindow;
}
