/*
 * Gazette — the Platinum main window
 * Copyright (c) 2026 brunocastello
 *
 * See platinum_window.h. Three panes, two draggable dividers, and a status
 * line.
 *
 * The window is a control hierarchy, not a set of rectangles this file
 * draws into. There is a root control, and the sidebar and the headline
 * list are real List Box controls — CDEF 22, through CreateListBoxControl.
 * The control draws its own Platinum frame and focus ring, carries its own
 * scroll bar, tracks its own clicks and moves its own selection from the
 * keyboard, and it is the Control Manager that decides which of them the
 * keyboard is talking to.
 *
 * Only what goes *inside* a cell is ours, through a list definition
 * function handed to CreateListBoxControl as a ListDefSpec of
 * kListDefUserProcType — a callback in this file rather than an 'LDEF' code
 * resource, which Carbon does not allow anyway. The cells carry no data.
 * Both lists are a view onto something the engine already holds in order,
 * so a cell's row number *is* its index into that: the sidebar's rows come
 * from GazetteCoreSidebarRowAt and the headlines from
 * GazetteFeedsArticleAt. Keeping a copy in the cells would only be a second
 * thing to get out of date.
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
#include <Folders.h>   /* kOnSystemDisk, which GetIconRef wants */
#include <Fonts.h>
#include <Icons.h>
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
    kHeaderHeight  = 17,        /* floor for the header bar; see gHeaderHeight */
    kStatusHeight  = 20,        /* likewise, until the chrome font is measured */
    kRowHeight     = 14,        /* a list cell, until the theme's font is
                                   measured — see gRowHeight                 */
    kReaderLead    = 13,        /* what one arrow scrolls the article by      */
    kDividerWidth  = 4,         /* the draggable gap between panes            */
    kTextInset     = 4,
    kDateColumn    = 46,        /* likewise, until gDateColumn is measured     */
    kBaseline      = 10,        /* likewise, until gRowBaseline is worked out */

    /* The sidebar is an outline: a column for the disclosure triangle, then
       one indent for a group's feeds. A top-level feed is a group's sibling,
       so it starts where a group's name does. */
    kTriangleSize   = 12,       /* what the Appearance Manager draws into */
    kTriangleColumn = 14,       /* the triangle's own column, with its gap */
    kGroupIndent    = 16,
    kIconSize       = 16,       /* the small icon beside a row's name */
    kIconGap        = 3,

    kMinSidebar    = 120,
    kMaxSidebarPad = 160,       /* how much room the right side must keep     */
    kMinListHeight = 3 * kRowHeight,
    kMinReader     = 3 * kReaderLead,
    kReaderMargin  = 2,         /* above the first line and below the last */

    /* Which pane the keyboard is driving. The reader's scroll bar carries
       kRefReader as its control reference, so its action procedure and the
       focus are named the same way. */
    kRefSidebar = 1,
    kRefList    = 2,
    kRefReader  = 3,

    /* A user pane has no parts of its own, so its focus procedure has to
       name one. Any non-zero part will do; this one says what it is for. */
    kControlReaderFocusPart = 1
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

/* The root of the hierarchy. Every other control is embedded in it, which
   is what makes SetKeyboardFocus and the Tab key mean anything. */
static ControlRef gRootControl;

/* The two List Box controls and the List Manager lists inside them. The
   control is what gets moved, drawn, clicked and focused; the list is what
   gets asked about rows and selections. */
static ControlRef gSidebarCtl;
static ControlRef gArticleCtl;
static ListHandle gSidebarList;
static ListHandle gArticleList;
static ListDefUPP gSidebarLDEF;
static ListDefUPP gArticleLDEF;

/* A window header over each list — CDEF 21's list-view variant, which is
   what Platinum puts above a list. Neither carries a title of its own, so
   only their text is drawn here.

   The status strip along the bottom is deliberately *not* a control. Outlook
   Express leaves it flat on the window's own background with a single rule
   above it, and a placard's bevel there is a box drawn round something that
   is not a button. This was a placard for two rounds and it was wrong both
   times. */
static ControlRef gSidebarHeaderCtl;
static ControlRef gListHeaderCtl;

/* The reader is a user pane control, so that it draws through the hierarchy,
   takes the keyboard focus like the two lists and gets a real focus ring
   rather than being a rectangle this file happens to paint. */
static ControlRef gReaderCtl;
static ControlRef gReaderScroll;

static ControlActionUPP        gScrollUPP;
static ControlUserPaneDrawUPP  gReaderDrawUPP;
static ControlUserPaneFocusUPP gReaderFocusUPP;
static ControlUserPaneTrackingUPP gReaderTrackUPP;

/* Pane rectangles, recomputed by Layout() and by nothing else. A list's
   pane is its control's bounds: the frame and the scroll bar are the CDEF's
   business and live inside it, so nothing here has to leave room for them.
   Where a list's rows actually are is a question for the list, and
   GetListViewBounds answers it. */
static Rect gSidebarPane;
static Rect gSidebarHeader;
static Rect gListPane;
static Rect gListHeader;
static Rect gReaderPane;        /* the framed box, bar included */
static Rect gReaderRect;        /* the text inside it, bar excluded */
static Rect gStatusRect;
static Rect gVDivider;          /* between sidebar and the right side */
static Rect gHDivider;          /* between headlines and the article  */

static short gSidebarWidth = 200;
static short gListShare    = 45;    /* percent of the right side given to the
                                       headline list; the article gets the rest */

static int gSelectedFeed    = 0;
static int gSelectedArticle = -1;

/* The group the sidebar has selected, or -1 when the selection is a feed.
   gSelectedFeed keeps its meaning either way: it is the feed the headline
   list and the reader are showing, which a click on a group does not
   change. */
static int gSelectedGroup   = -1;


static char gStatus[192];

/*
 * The fonts, taken from Outlook Express 5.0.6's own 'Txtr' resources rather
 * than reasoned about. OE5 is a PowerPlant application and every piece of
 * text in it names a text-traits resource; there are thirty-seven of them
 * and they say this:
 *
 *   'Txtr' 500 "List font"          applFont  9  plain
 *   'Txtr' 501 "Proportional font"  applFont 12  plain
 *   'Txtr' 502 "Monospaced font"    Monaco    9  plain
 *   'Txtr' 134 "App Bold 9"         applFont  9  bold
 *   'Txtr' 128 "System 0"           systemFont, default size
 *
 * So: the lists, the headings and the labels are the **application font at
 * 9** — Geneva on a stock Mac OS 9 — with bold for emphasis, and the
 * article is the same font at **12**, which is what OE calls its
 * proportional font and sets message bodies in. systemFont (Charcoal) turns
 * up in only a handful of centred captions and nowhere near a list.
 *
 * applFont rather than a hard-coded Geneva: font 1 is "whatever the user's
 * application font is", which is what OE asks for and what GetAppFont
 * answers.
 */
static short gViewFont  = kFontIDGeneva;  /* lists, headings, status */
static short gViewSize  = 12;
static short gReadFont  = kFontIDGeneva;  /* the article's body       */
static short gReadSize  = 12;
static short gLabelSize = 9;              /* the byline under a headline */

static short gRowHeight    = kRowHeight;
static short gRowBaseline  = kBaseline;
static short gRowAscent    = 9;
static short gRowDescent   = 3;

/* The chrome's bars are as tall as the chrome's font needs, not 17 and 20
   because Geneva 9 once fitted in them. */
static short gHeaderHeight = kHeaderHeight;
static short gHeaderBase   = 12;
static short gStatusHeight = kStatusHeight;
static short gStatusBase   = 13;

/* Wide enough for "Sep 00 00:00", measured rather than guessed at 46. */
static short gDateColumn   = kDateColumn;

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
static void  SetFocus(short pane);
static short FocusedPane(void);
static void DrawSidebarPane(void);
static void DrawArticlePane(void);
static void DrawReader(void);
static void DrawStatus(void);
static void DrawStatusText(void);
static void DrawHeaderTitle(const Rect *r, const char *text);

/* ------------------------------------------------------------------ */
/* Small helpers                                                       */
/* ------------------------------------------------------------------ */

/* OE's list font: the two lists, both headings, the status line. */
static void UseViewFont(void)
{
    TextFont(gViewFont);
    TextSize(gViewSize);
    TextFace(normal);
}

/*
 * The background a selected row is painted with — the highlight colour from
 * the Appearance control panel, which is the colour the user has said a
 * selection should be. Painting it is deliberate rather than inverting with
 * the hilite bit: the invert depended on QuickDraw honouring a low-memory
 * flag that is set again by the next drawing call, and when it did not the
 * selection came out with no background at all.
 */
static void FillHighlight(const Rect *box)
{
    RGBColor hilite;
    RGBColor saveBack;

    GetBackColor(&saveBack);
    LMGetHiliteRGB(&hilite);
    RGBBackColor(&hilite);
    EraseRect(box);
    RGBBackColor(&saveBack);
}

/*
 * The font, and everything sized from it: row height, baselines, the two
 * chrome bars and the headline list's date column. Called once, with the
 * window's port current, so that nothing in the layout is a number chosen
 * to suit a font that might not be the one in use.
 */
/*
 * The fonts and every height that follows from them. Called once, with the
 * window's port current.
 */
static void MeasureFonts(void)
{
    FontInfo info;

    /* applFont — 'Txtr' 500 and 501 both ask for it; the sizes are OE's. */
    gViewFont = GetAppFont();
    gReadFont = gViewFont;
    gViewSize  = 12;
    gReadSize  = 12;
    gLabelSize = 9;

    UseViewFont();
    GetFontInfo(&info);
    gRowAscent  = info.ascent;
    gRowDescent = info.descent;

    /* A row is as tall as the font needs or as tall as its icon, whichever
       is more — OE's folder rows are the icon's height plus a pixel. */
    gRowHeight = (short)(info.ascent + info.descent + info.leading + 2);
    if (gRowHeight < kIconSize + 1) {
        gRowHeight = kIconSize + 1;
    }
    gRowBaseline = (short)(info.ascent +
                           ((gRowHeight - info.ascent - info.descent) / 2));
    gDateColumn = (short)(TextWidth("Sep 00 00:00", 0, 12) + kTextInset * 2);

    /* Both chrome bars are set in the same font, so they measure alike. */
    gHeaderHeight = (short)(info.ascent + info.descent + info.leading + 9);
    if (gHeaderHeight < kHeaderHeight) {
        gHeaderHeight = kHeaderHeight;
    }
    gHeaderBase   = (short)(info.ascent +
                            ((gHeaderHeight - info.ascent - info.descent) / 2));
    gStatusHeight = gHeaderHeight;
    gStatusBase   = gHeaderBase;
}

/*
 * Draw text clipped to a width, ending in an ellipsis when it does not fit.
 * A headline is nearly always too long for its column, and a hard clip mid
 * word reads as a drawing bug rather than as truncation.
 *
 * TruncText is the Script Manager's own truncation rather than a loop that
 * counts bytes backwards until the width fits: it shortens the text, puts
 * the ellipsis in, and knows where a character actually ends — which the
 * loop this used to be did not, and would have cut a two-byte character in
 * half on a non-Roman system. It works in place, hence the copy.
 */
static void DrawTruncated(const char *text, short maxWidth)
{
    char  buf[512];
    short len;

    if (text == NULL || maxWidth <= 0) {
        return;
    }

    len = (short)strlen(text);
    if (len > (short)(sizeof buf)) {
        len = (short)(sizeof buf);      /* longer than any column is wide */
    }
    if (len <= 0) {
        return;
    }
    memcpy(buf, text, (size_t)len);

    if (TruncText(maxWidth, buf, &len, truncEnd) == truncErr) {
        return;
    }
    if (len > 0) {
        DrawText(buf, 0, len);
    }
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

/* Where a list's rows actually are, inside its control's frame. */
static void ListView(ListHandle list, Rect *view)
{
    SetRect(view, 0, 0, 0, 0);
    if (list != NULL) {
        GetListViewBounds(list, view);
    }
}

/* Which pane the Control Manager says the keyboard is talking to. */
static short FocusedPane(void)
{
    ControlRef focus = NULL;

    if (gWindow == NULL) {
        return kRefReader;
    }
    GetKeyboardFocus(gWindow, &focus);
    if (focus != NULL && focus == gSidebarCtl) {
        return kRefSidebar;
    }
    if (focus != NULL && focus == gArticleCtl) {
        return kRefList;
    }
    return kRefReader;
}

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
 * Move and resize a list by moving and resizing its control: the CDEF puts
 * the frame, the scroll bar and the rows where they belong inside the new
 * bounds. Only the cell width is left over — the list would otherwise keep
 * the width it was born with and start wanting to scroll sideways.
 */
static void SizeListBox(ControlRef control, ListHandle list, const Rect *bounds)
{
    Rect  view;
    Point cell;

    if (control == NULL) {
        return;
    }
    SetControlBounds(control, bounds);

    if (list == NULL) {
        return;
    }
    LSetDrawingMode(false, list);
    ListView(list, &view);
    cell.v = gRowHeight;
    cell.h = (short)(view.right - view.left);
    if (cell.h > 0) {
        LCellSize(cell, list);
    }
    LSetDrawingMode(true, list);
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

    contentBottom = (short)(bounds.bottom - gStatusHeight);

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
            (short)(bounds.top + gHeaderHeight));
    /* One pixel up, so the list's own top frame line lands on the header's
       bottom edge instead of drawing a second line just below it. */
    SetRect(&gSidebarPane, bounds.left,
            (short)(bounds.top + gHeaderHeight - 1),
            (short)(bounds.left + gSidebarWidth), contentBottom);

    SetRect(&gVDivider, (short)(bounds.left + gSidebarWidth), bounds.top,
            (short)(bounds.left + gSidebarWidth + kDividerWidth),
            contentBottom);

    rightLeft = (short)(gVDivider.right);

    listBottom = (short)(bounds.top + gHeaderHeight +
                         (long)(contentBottom - bounds.top - gHeaderHeight) *
                         gListShare / 100);
    if (listBottom < bounds.top + gHeaderHeight + kMinListHeight) {
        listBottom = (short)(bounds.top + gHeaderHeight + kMinListHeight);
    }
    if (listBottom > contentBottom - kMinReader - kDividerWidth) {
        listBottom = (short)(contentBottom - kMinReader - kDividerWidth);
    }

    SetRect(&gListHeader, rightLeft, bounds.top,
            bounds.right, (short)(bounds.top + gHeaderHeight));
    SetRect(&gListPane, rightLeft, (short)(bounds.top + gHeaderHeight - 1),
            bounds.right, listBottom);

    SetRect(&gHDivider, rightLeft, listBottom,
            bounds.right, (short)(listBottom + kDividerWidth));

    /* The same shape as a list box: one framed box holding the text and the
       scroll bar, rather than a framed box with a bar bolted to its side. */
    SetRect(&gReaderPane, rightLeft, (short)(listBottom + kDividerWidth),
            bounds.right, contentBottom);
    SetRect(&gReaderRect, (short)(gReaderPane.left + 1),
            (short)(gReaderPane.top + 1),
            (short)(gReaderPane.right - kScrollWidth),
            (short)(gReaderPane.bottom - 1));

    /* End to end, and flush with the panes above: the placard's own top
       edge is the line between them, so there is nothing to leave a gap
       for. The grow box is drawn on top of its right hand corner. */
    SetRect(&gStatusRect, bounds.left, contentBottom,
            bounds.right, bounds.bottom);

    SizeListBox(gSidebarCtl, gSidebarList, &gSidebarPane);
    SizeListBox(gArticleCtl, gArticleList, &gListPane);

    if (gSidebarHeaderCtl != NULL) {
        SetControlBounds(gSidebarHeaderCtl, &gSidebarHeader);
    }
    if (gListHeaderCtl != NULL) {
        SetControlBounds(gListHeaderCtl, &gListHeader);
    }
    if (gReaderCtl != NULL) {
        SetControlBounds(gReaderCtl, &gReaderPane);
    }

    /* Inside the frame, down the right hand edge — where a List Box control
       puts its own, so all three panes read as the same thing. */
    if (gReaderScroll != NULL) {
        MoveControl(gReaderScroll, (short)(gReaderPane.right - kScrollWidth),
                    gReaderPane.top);
        SizeControl(gReaderScroll, kScrollWidth,
                    (short)(gReaderPane.bottom - gReaderPane.top));
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
static void ApplyRunStyle(long start, long end, short face, short size)
{
    TextStyle style;

    if (gReaderTE == NULL || end <= start) {
        return;
    }
    style.tsFont = gReadFont;
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
        ApplyRunStyle(0, (long)used, normal, gReadSize);
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

        /* One font throughout; the headline is the only thing set apart,
           and weight is enough to do it. */
        ApplyRunStyle(0, titleEnd, bold, gReadSize);
        ApplyRunStyle(titleEnd, bylineEnd, normal, gLabelSize);
        ApplyRunStyle(bylineEnd, (long)used, normal, gReadSize);
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
/* Only the reader's bar comes through here. Each list's bar is inside  */
/* its List Box control and the CDEF tracks it.                         */
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

/*
 * The title over a pane. The bar itself is a window header control — CDEF
 * 21, the list-view variant, which is the widget Platinum puts above a list
 * and not the placard this used to draw. The control has no title of its
 * own, so the text goes on top of it in the theme's small system font.
 */
static void DrawHeaderTitle(const Rect *r, const char *text)
{
    Rect inner = *r;

    UseViewFont();
    TextFace(bold);
    SetThemeTextColor(kThemeTextColorWindowHeaderActive, 8, true);

    inner.left  = (short)(inner.left + kTextInset + 2);
    inner.right = (short)(inner.right - kTextInset);

    MoveTo(inner.left, (short)(r->top + gHeaderBase));
    DrawTruncated(text, (short)(inner.right - inner.left));

    TextFace(normal);
    ForeColor(blackColor);
}

/*
 * A selected row. A pane the keyboard is driving inverts its selection; one
 * it is not outlines the same rectangle instead. That is the List Manager's
 * own convention for an inactive list, and without it two panes both showing
 * a solid black bar leave no way to tell which one the arrow keys will move.
 */
/*
 * The box a sidebar row's name sits in — what the selection is painted
 * behind, rather than a bar across the whole row.
 */
static void LabelBox(const Rect *cell, short left, short width,
                     short baseline, Rect *box)
{
    SetRect(box, (short)(left - 2), (short)(baseline - gRowAscent - 1),
            (short)(left + width + 2), (short)(baseline + gRowDescent + 1));
    if (box->top < cell->top) {
        box->top = cell->top;
    }
    if (box->bottom > cell->bottom) {
        box->bottom = cell->bottom;
    }
    if (box->right > cell->right) {
        box->right = cell->right;
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

    /* DrawThemeButton erases its own bounds with the current background, so
       the sidebar's grey has to be current or the triangle arrives sitting
       in a white square. */
    SetThemeBackground(kThemeBrushDialogBackgroundActive, 8, true);
    (void)DrawThemeButton(&box, kThemeDisclosureButton, &info, NULL,
                          NULL, NULL, 0);
    SetThemeBackground(kThemeBrushDocumentWindowBackground, 8, true);
}

/*
 * The small icon beside a row's name, the way Outlook Express and every
 * Finder list view put one there. These are the system's own icons through
 * Icon Services rather than artwork of ours: a folder for a group, opened
 * when the group is, and the Internet news location icon for a feed, which
 * is what Mac OS 9 already uses to mean "a news source at a URL".
 *
 * The IconRefs are the system's; they are got once and never released,
 * because they live as long as the window does and releasing a shared
 * system icon on every row draw would be the wrong trade.
 */
static IconRef gFolderIcon;
static IconRef gOpenFolderIcon;
static IconRef gFeedIcon;

static void LoadRowIcons(void)
{
    (void)GetIconRef(kOnSystemDisk, kSystemIconsCreator,
                     kGenericFolderIcon, &gFolderIcon);
    (void)GetIconRef(kOnSystemDisk, kSystemIconsCreator,
                     kOpenFolderIcon, &gOpenFolderIcon);
    (void)GetIconRef(kOnSystemDisk, kSystemIconsCreator,
                     kInternetLocationNewsIcon, &gFeedIcon);
}

static void ReleaseRowIcons(void)
{
    if (gFolderIcon != NULL) {
        (void)ReleaseIconRef(gFolderIcon);
        gFolderIcon = NULL;
    }
    if (gOpenFolderIcon != NULL) {
        (void)ReleaseIconRef(gOpenFolderIcon);
        gOpenFolderIcon = NULL;
    }
    if (gFeedIcon != NULL) {
        (void)ReleaseIconRef(gFeedIcon);
        gFeedIcon = NULL;
    }
}

/* Plot one, centred in the row, dimmed when the feed it stands for is off. */
static void DrawRowIcon(const Rect *cell, short left, IconRef icon,
                        Boolean enabled)
{
    Rect box;
    short top;

    if (icon == NULL) {
        return;
    }
    top = (short)(cell->top + ((cell->bottom - cell->top - kIconSize) / 2));
    SetRect(&box, left, top, (short)(left + kIconSize),
            (short)(top + kIconSize));

    (void)PlotIconRef(&box, kAlignAbsoluteCenter,
                      enabled ? kTransformNone : kTransformDisabled,
                      kIconServicesNormalUsageFlag, icon);
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
/* ASCII upper-casing: the group names are already transliterated, so there
   is nothing here for a locale to disagree with. */
static void UpperCase(const char *in, char *out, size_t cap)
{
    size_t i = 0;

    for (i = 0; i + 1 < cap && in[i] != '\0'; i++) {
        char c = in[i];

        out[i] = (char)((c >= 'a' && c <= 'z') ? (c - 'a' + 'A') : c);
    }
    out[i] = '\0';
}

/*
 * Compose "Name (12)" into out, truncated to fit maxWidth, and answer how
 * wide it came out. It has to be built and measured before anything is
 * drawn, because the selection is painted *behind* it and needs to know how
 * far it reaches.
 *
 * The name is what gets truncated and never the number: the count is the
 * part that has to stay legible in a narrow sidebar.
 */
static short BuildRowLabel(const char *name, int unread, short maxWidth,
                           char *out, size_t cap, short *outLen)
{
    char  count[16];
    short countLen   = 0;
    short countWidth = 0;
    short len;

    count[0] = '\0';
    if (unread > 0) {
        snprintf(count, sizeof count, " (%d)", unread);
        countLen   = (short)strlen(count);
        countWidth = (short)TextWidth(count, 0, countLen);
    }

    len = (short)strlen(name);
    if (len > (short)(cap - sizeof count - 1)) {
        len = (short)(cap - sizeof count - 1);
    }
    if (len < 0) {
        len = 0;
    }
    memcpy(out, name, (size_t)len);

    if (TruncText((short)(maxWidth - countWidth), out, &len, truncEnd) ==
        truncErr) {
        len = 0;
    }
    if (countLen > 0) {
        memcpy(out + len, count, (size_t)countLen);
        len = (short)(len + countLen);
    }

    *outLen = len;
    return (short)TextWidth(out, 0, len);
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

/*
 * The two lists do not share a background. The sidebar is Platinum grey —
 * it is a place to choose from, like the Finder's or Newsstand's own
 * sidebar, and grey is what says so. The headline list and the article are
 * white, because they hold the thing being read.
 */
static void EraseWith(const Rect *r, ThemeBrush brush)
{
    SetThemeBackground(brush, 8, true);
    EraseRect(r);
    SetThemeBackground(kThemeBrushDocumentWindowBackground, 8, true);
}

/*
 * One row of the sidebar, laid out the way Outlook Express lays its folder
 * list out: the disclosure triangle's column, then a small icon, then the
 * name. A feed inside a group is indented by one step; a group's own row is
 * the only one that draws a triangle.
 */
static void DrawSidebarCell(const Rect *cell, short row, Boolean selected)
{
    GazetteSidebarRow r;
    char              label[kGazetteTitleLen + 32];
    short             labelLen = 0;
    short             iconLeft;
    short             textLeft;
    short             baseline;
    short             width;
    Boolean           enabled = true;

    EraseWith(cell, kThemeBrushDialogBackgroundActive);
    if (!GazetteCoreSidebarRowAt(row, &r)) {
        return;
    }

    UseViewFont();
    baseline = (short)(cell->top + gRowBaseline);
    iconLeft = (short)(cell->left + kTextInset + kTriangleColumn);

    if (r.kind == kGazetteRowFeed && GazetteCoreFeedGroup(r.index) >= 0) {
        iconLeft = (short)(iconLeft + kGroupIndent);
    }
    textLeft = (short)(iconLeft + kIconSize + kIconGap);

    /* The weight is decided before the label is measured, because a bold
       name is wider than a plain one and the selection is drawn to fit. */
    if (r.kind == kGazetteRowGroup) {
        char caps[kGazetteTitleLen];

        /* A category is set in capitals, the way a section head is. */
        UpperCase(GazetteCoreGroupName(r.index), caps, sizeof caps);
        TextFace(bold);
        width = BuildRowLabel(caps, GroupUnread(r.index),
                              (short)(cell->right - kTextInset - textLeft),
                              label, sizeof label, &labelLen);
    } else {
        int unread;

        enabled = GazetteCoreFeedEnabled(r.index);
        unread  = enabled ? FeedUnread(r.index) : 0;
        TextFace((unread > 0) ? bold : normal);
        width = BuildRowLabel(GazetteCoreFeedTitle(r.index), unread,
                              (short)(cell->right - kTextInset - textLeft),
                              label, sizeof label, &labelLen);
    }

    /*
     * The selection goes down first, so the name is drawn on top of it
     * rather than the other way round. Only the pane the keyboard is driving
     * paints it: an unfocused list outlines the same box instead, which is
     * the List Manager's own convention for a list that is not in charge.
     */
    /*
     * Always the highlight colour, never an outline. A box drawn round the
     * name in black reads as a bug rather than as a selection — which is
     * exactly how it read — and the highlight colour is what the user chose
     * for a selection whether or not this list happens to have the focus.
     */
    if (selected) {
        Rect box;

        LabelBox(cell, textLeft, width, baseline, &box);
        FillHighlight(&box);
    }

    if (r.kind == kGazetteRowGroup) {
        Boolean open = (Boolean)!GazetteCoreGroupCollapsed(r.index);

        DrawDisclosure(cell, (short)(cell->left + kTextInset), open);
        DrawRowIcon(cell, iconLeft, open ? gOpenFolderIcon : gFolderIcon, true);
    } else {
        DrawRowIcon(cell, iconLeft, gFeedIcon, enabled);
    }

    /* A switched-off feed is drawn the way an unavailable item is drawn
       anywhere else in Platinum, so it reads as off rather than as missing. */
    SetThemeTextColor(enabled ? kThemeTextColorListView
                              : kThemeTextColorDialogInactive,
                      8, true);
    if (labelLen > 0) {
        MoveTo(textLeft, baseline);
        DrawText(label, 0, labelLen);
    }

    TextFace(normal);
    ForeColor(blackColor);
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

    EraseWith(cell, kThemeBrushWhite);
    if (a == NULL) {
        return;
    }

    /* A selected headline gets a background across the whole row — it is a
       line in a list of them, not a name to be boxed. Down first, so the
       headline is drawn on top of it. */
    if (selected) {
        FillHighlight(cell);
    }

    UseViewFont();
    baseline = (short)(cell->top + gRowBaseline);
    SetThemeTextColor(kThemeTextColorListView, 8, true);

    /* Unread in bold — the whole row of it, date included, because the
       weight is about the article and not about the headline. */
    TextFace(a->read ? normal : bold);

    /* The date sits in a fixed column so the headlines line up; an article
       with no date simply leaves it blank rather than shifting. */
    GazetteFormatDate(a->date, UnixNow(), when, sizeof when);
    if (when[0] != '\0') {
        MoveTo((short)(cell->left + kTextInset), baseline);
        DrawTruncated(when, (short)(gDateColumn - kTextInset));
    }

    MoveTo((short)(cell->left + kTextInset + gDateColumn), baseline);
    DrawTruncated(a->title,
                  (short)(cell->right - cell->left - gDateColumn -
                          2 * kTextInset));

    TextFace(normal);
    ForeColor(blackColor);
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
 * A list pane is one control, so drawing it is one call. The frame, the
 * white behind the rows, the rows themselves, the scroll bar and the focus
 * ring are all the CDEF's — which is the whole point of it being a control.
 */
/*
 * The space below the last row. The List Box CDEF erases its whole view
 * before the rows are drawn, and it uses its own background to do it — so
 * the sidebar's grey and the headline list's white have to be put back over
 * whatever is left after the last cell.
 */
static void FillListRemainder(ListHandle list, int rows, ThemeBrush brush)
{
    Rect view;
    Rect rest;

    ListView(list, &view);
    if (view.right <= view.left) {
        return;
    }

    rest = view;
    if (rows > 0) {
        Cell cell;
        Rect last;

        cell.h = 0;
        cell.v = (short)(rows - 1);
        LRect(&last, cell, list);
        if (last.bottom > rest.top) {
            rest.top = last.bottom;
        }
    }
    if (rest.bottom > rest.top) {
        EraseWith(&rest, brush);
    }
}

static void DrawSidebarPane(void)
{
    if (gWindow == NULL || gSidebarCtl == NULL) {
        return;
    }
    SetPortWindowPort(gWindow);
    Draw1Control(gSidebarCtl);
    FillListRemainder(gSidebarList, GazetteCoreSidebarRowCount(),
                      kThemeBrushDialogBackgroundActive);
}

static void DrawArticlePane(void)
{
    Rect      view;
    RgnHandle clip = NULL;

    if (gWindow == NULL || gArticleCtl == NULL) {
        return;
    }
    SetPortWindowPort(gWindow);
    Draw1Control(gArticleCtl);
    FillListRemainder(gArticleList, GazetteFeedsArticleCount(),
                      kThemeBrushWhite);

    if (GazetteFeedsArticleCount() != 0) {
        return;
    }

    /* An empty list has no cell to say so in. */
    ListView(gArticleList, &view);
    if (view.right <= view.left) {
        return;
    }

    clip = NewRgn();
    if (clip != NULL) {
        GetClip(clip);
    }
    ClipRect(&view);

    UseViewFont();
    SetThemeTextColor(kThemeTextColorListView, 8, true);
    MoveTo((short)(view.left + kTextInset),
           (short)(view.top + gRowBaseline + 1));
    if (GazetteFeedsFilter()[0] != '\0') {
        DrawString("\pNothing here matches - Edit menu, Show All.");
    } else {
        DrawString("\pNo headlines yet - press Command-R.");
    }
    ForeColor(blackColor);

    if (clip != NULL) {
        SetClip(clip);
        DisposeRgn(clip);
    }
}

/*
 * The reader's contents. This is the user pane control's drawing procedure,
 * so the Control Manager calls it — from DrawControls, from Draw1Control and
 * whenever the focus ring has to change — and the pane is drawn through the
 * hierarchy like everything else rather than painted over the top of it.
 */
static pascal void ReaderDraw(ControlRef control, SInt16 part)
{
    RgnHandle clip = NULL;

    (void)control;
    (void)part;

    if (gWindow == NULL) {
        return;
    }
    SetPortWindowPort(gWindow);

    /*
     * The frame goes round the whole pane, scroll bar included, which is
     * what a List Box control's frame does. DrawThemeListBoxFrame draws
     * just *outside* the rectangle it is given, so it is given one inset by
     * a pixel — otherwise the reader's frame lands a pixel outside its own
     * bounds and collides with the divider above it, which is what made its
     * borders look unlike the two lists'.
     */
    {
        Rect frame = gReaderPane;

        InsetRect(&frame, 1, 1);
        DrawThemeListBoxFrame(&frame, kThemeStateActive);
    }

    clip = NewRgn();
    if (clip != NULL) {
        GetClip(clip);
    }
    EraseWith(&gReaderRect, kThemeBrushWhite);
    ClipRect(&gReaderRect);

    if (gReaderTE != NULL) {
        Rect view = (**gReaderTE).viewRect;   /* not a pointer into the
                                                 handle, which can move */

        TEUpdate(&view, gReaderTE);
    }

    if (clip != NULL) {
        SetClip(clip);
        DisposeRgn(clip);
    }

    /* The ring the two lists get from their CDEF, drawn here because a user
       pane has no idea what it contains. It is still the Appearance
       Manager's ring, not a rectangle of our own devising. */
    (void)DrawThemeFocusRect(&gReaderPane,
                             (Boolean)(gReaderCtl != NULL &&
                                       GetControlValue(gReaderCtl) != 0));
}

static void DrawReader(void)
{
    if (gWindow == NULL || gReaderCtl == NULL) {
        return;
    }
    SetPortWindowPort(gWindow);
    Draw1Control(gReaderCtl);
}

/* The dotted grab handle in a splitter: five pairs of pixels, centred. */
static void DrawGrabHandle(const Rect *divider)
{
    short mid = (short)((divider->top + divider->bottom) / 2);
    short at  = (short)((divider->left + divider->right) / 2 - 14);
    short i;

    if (divider->bottom - divider->top < 3) {
        return;
    }
    SetThemeTextColor(kThemeTextColorDialogActive, 8, true);
    for (i = 0; i < 5; i++) {
        MoveTo(at, mid);
        LineTo((short)(at + 1), mid);
        at = (short)(at + 6);
    }
    ForeColor(blackColor);
}

/* Just the text. The strip under it is the window's own background. */
static void DrawStatusText(void)
{
    if (gWindow == NULL) {
        return;
    }
    SetPortWindowPort(gWindow);

    /* Flat, on the window's background, with one rule along the top — the
       line between the panes and the strip, and the only edge it has. */
    EraseWith(&gStatusRect, kThemeBrushDocumentWindowBackground);
    {
        Rect rule;

        SetRect(&rule, gStatusRect.left, gStatusRect.top,
                gStatusRect.right, (short)(gStatusRect.top + 1));
        DrawThemeSeparator(&rule, kThemeStateActive);
    }

    UseViewFont();
    SetThemeTextColor(kThemeTextColorDialogActive, 8, true);
    MoveTo((short)(gStatusRect.left + kTextInset + 4),
           (short)(gStatusRect.top + gStatusBase));
    DrawTruncated(gStatus,
                  (short)(gStatusRect.right - gStatusRect.left -
                          2 * kTextInset - 8));
    ForeColor(blackColor);
}

/* For when only the status line has changed. */
static void DrawStatus(void)
{
    DrawStatusText();
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

    /* kThemeBrushDocumentWindowBackground, not the dialog one: this is a
       kDocumentWindowClass window and the two brushes are different greys. */
    SetThemeBackground(kThemeBrushDocumentWindowBackground, 8, true);
    EraseRect(&bounds);


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

    /* The dividers, drawn as the Appearance Manager's own separators so they
       track the theme rather than being two hard-coded greys, and the
       horizontal one carries the row of dots Outlook Express puts in a
       splitter to say that it can be dragged. */
    DrawThemeSeparator(&gVDivider, kThemeStateActive);
    DrawThemeSeparator(&gHDivider, kThemeStateActive);
    DrawGrabHandle(&gHDivider);

    /* The whole control hierarchy in one call — the two lists with their
       frames, scroll bars and focus rings, the two window headers, the
       reader and its bar, and the status placard. */
    DrawControls(gWindow);

    /*
     * The List Box CDEF erases its whole view with its own background before
     * the rows go down, so below the last row the sidebar comes out white
     * rather than grey. This was being put right in DrawSidebarPane, which a
     * full update does not go through — so on every redraw of the window the
     * sidebar's empty half went back to white.
     */
    FillListRemainder(gSidebarList, GazetteCoreSidebarRowCount(),
                      kThemeBrushDialogBackgroundActive);
    FillListRemainder(gArticleList, GazetteFeedsArticleCount(),
                      kThemeBrushWhite);

    /* Their titles go on top of them: a window header control and a placard
       have no text of their own. */
    DrawHeaderTitle(&gSidebarHeader, "Feeds");
    DrawHeaderTitle(&gListHeader, header);
    DrawStatusText();

    /* The grow box lives in the content region, so it is the application
       that draws it. Without this the bottom right corner is simply blank,
       which is the one part of a Platinum window a user looks for. */
    DrawGrowIcon(gWindow);

    /* An empty headline list has no cell to say so in. */
    if (GazetteFeedsArticleCount() == 0) {
        DrawArticlePane();
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
            short usable = (short)(bounds.bottom - gStatusHeight -
                                   bounds.top - gHeaderHeight);
            short share;

            if (usable <= 0) {
                break;
            }
            share = (short)((long)(pt.v - bounds.top - gHeaderHeight) * 100 /
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

/* Did the click land in a group row's disclosure triangle? That column is
   the one part of the sidebar the control must not be allowed to track,
   because it opens and shuts rather than selects. */
static Boolean HitDisclosure(Point where)
{
    Cell              cell;
    GazetteSidebarRow row;
    Rect              view;

    ListView(gSidebarList, &view);
    if (where.h >= view.left + kTextInset + kTriangleColumn) {
        return false;
    }
    if (!CellAtPoint(gSidebarList, where, &cell)) {
        return false;
    }
    if (!GazetteCoreSidebarRowAt(cell.v, &row) ||
        row.kind != kGazetteRowGroup) {
        return false;
    }

    GazetteCoreSetGroupCollapsed(row.index,
                                 !GazetteCoreGroupCollapsed(row.index));
    SidebarRowsChanged();
    return true;
}

static void SidebarClicked(Point where, EventModifiers modifiers)
{
    GazetteSidebarRow row;
    int               at;

    /* The triangle's own column opens and shuts a group; the rest of the
       line selects it, the way a folder behaves in a list view. */
    if (HitDisclosure(where)) {
        return;
    }

    (void)HandleControlClick(gSidebarCtl, where, modifiers, NULL);

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

/* The user pane's tracking procedure: a click that HandleControlClick has
   routed to the reader. */
static pascal ControlPartCode ReaderTrack(ControlRef control, Point startPt,
                                          ControlActionUPP actionProc)
{
    (void)control;
    (void)actionProc;

    ReaderClick(startPt, 0);
    return kControlNoPart;
}

/*
 * The user pane's focus procedure. A user pane has no parts, so the control's
 * value is used to remember whether it has the focus — that is what
 * ReaderDraw asks when it decides whether to draw the ring.
 */
static pascal ControlPartCode ReaderFocus(ControlRef control,
                                          ControlFocusPart action)
{
    if (control == NULL) {
        return kControlFocusNoPart;
    }

    if (action == kControlFocusNoPart) {
        SetControlValue(control, 0);
        if (gReaderTE != NULL) {
            SetPortWindowPort(gWindow);
            TEDeactivate(gReaderTE);
            TESetSelect(0, 0, gReaderTE);
        }
        Draw1Control(control);
        return kControlFocusNoPart;
    }

    SetControlValue(control, 1);
    Draw1Control(control);
    return kControlReaderFocusPart;
}

void GazetteUIClick(Point where, EventModifiers modifiers)
{
    ControlRef      control = NULL;
    ControlPartCode part;

    if (gWindow == NULL) {
        return;
    }
    SetPortWindowPort(gWindow);

    control = FindControlUnderMouse(where, gWindow, &part);

    if (control != NULL && control == gSidebarCtl) {
        SetFocus(kRefSidebar);
        SidebarClicked(where, modifiers);
        return;
    }

    if (control != NULL && control == gArticleCtl) {
        SetFocus(kRefList);
        (void)HandleControlClick(gArticleCtl, where, modifiers, NULL);
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

    if (control != NULL && control == gReaderCtl) {
        SetFocus(kRefReader);
        (void)HandleControlClick(gReaderCtl, where, modifiers, NULL);
        return;
    }

    if (control != NULL && control == gReaderScroll && part != 0) {
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

    if (PtInRect(where, &gVDivider)) {
        TrackDivider(where, true);
        return;
    }
    if (PtInRect(where, &gHDivider)) {
        TrackDivider(where, false);
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

/*
 * Hand the keyboard to a pane. All three are controls, so this is the
 * Control Manager's own SetKeyboardFocus throughout: the lists' CDEF draws
 * their rings and the reader's focus procedure draws its own. Dropping the
 * old pane's selection is that procedure's business too, which is why there
 * is nothing about TextEdit left here.
 */
static void SetFocus(short pane)
{
    ControlRef want;

    if (gWindow == NULL || FocusedPane() == pane) {
        return;
    }

    switch (pane) {
        case kRefSidebar: want = gSidebarCtl; break;
        case kRefList:    want = gArticleCtl; break;
        default:          want = gReaderCtl;  break;
    }
    if (want != NULL) {
        (void)SetKeyboardFocus(gWindow, want, kControlFocusNextPart);
    }
}

Boolean GazetteUIKey(short key, EventModifiers modifiers)
{
    if (gWindow == NULL) {
        return false;
    }

    if (key == '\t') {
        /* All three panes are controls, so Tab is the Control Manager's own
           walk of the hierarchy rather than a switch statement here. */
        if ((modifiers & shiftKey) != 0) {
            (void)ReverseKeyboardFocus(gWindow);
        } else {
            (void)AdvanceKeyboardFocus(gWindow);
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

    switch (FocusedPane()) {
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

    /* One call for the whole hierarchy: the CDEFs grey their own frames,
       scroll bars and selections. */
    if (gRootControl != NULL) {
        if (active) {
            ActivateControl(gRootControl);
        } else {
            DeactivateControl(gRootControl);
        }
    }

    /* A selection is only meaningful while the window is in front. */
    if (!active && gReaderTE != NULL) {
        SetPortWindowPort(gWindow);
        TEDeactivate(gReaderTE);
    }

    /* ActivateControl has just re-enabled the reader's bar along with
       everything else in the hierarchy, so a bar with nothing to scroll has
       to be put back to sleep. 255 is the inactive hilite state, 0 the
       active one. */
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
 * A List Box control with a list definition function of our own. The
 * ListDefSpec goes straight into CreateListBoxControl, so no 'LDEF'
 * resource and no RegisterListDefinition are involved — the first is
 * impossible under Carbon and the second would cost CarbonLib 1.5.
 *
 * It starts with no rows; the shell fills it in through
 * GazetteUIFeedsChanged and GazetteUIArticlesChanged.
 */
static Boolean MakeListBox(ListDefUPP defProc, const Rect *bounds,
                           ControlRef *outControl, ListHandle *outList)
{
    ListDefSpec spec;
    ControlRef  control = NULL;
    ListHandle  list    = NULL;

    *outControl = NULL;
    *outList    = NULL;

    spec.defType    = kListDefUserProcType;
    spec.u.userProc = defProc;

    if (CreateListBoxControl(gWindow, bounds,
                             false,             /* not auto-sizing        */
                             0, 1,              /* no rows yet, one column */
                             false, true,       /* vertical bar only      */
                             gRowHeight,
                             (short)(bounds->right - bounds->left -
                                     kScrollWidth),
                             false,             /* no grow box corner     */
                             &spec, &control) != noErr || control == NULL) {
        return false;
    }

    if (GetControlData(control, kControlEntireControl,
                       kControlListBoxListHandleTag, sizeof list,
                       (Ptr)&list, NULL) != noErr || list == NULL) {
        DisposeControl(control);
        return false;
    }

    /* One row at a time, and no drag-selecting several. lOnlyOne is a
       negative constant in a byte-wide field, so it is masked rather than
       sign-extended into the flags word. */
    SetListSelectionFlags(list, (OptionBits)(lOnlyOne & 0xFF));

    *outControl = control;
    *outList    = list;
    return true;
}

/*
 * A control from its procID rather than from a Create…Control call.
 *
 * NewControl is "in CarbonLib 1.0 and later"; CreateWindowHeaderControl,
 * CreatePlacardControl and CreateUserPaneControl are all "1.1 and later",
 * and they do nothing NewControl cannot with the same CDEF. Using them
 * bought three entry points that a CFM loader has to resolve at launch, and
 * an import an installed CarbonLib does not export is what produces
 *
 *     The application "Gazette" could not be opened because "CarbonLib"
 *     could not be found.
 *
 * — a famously misleading message that means "a symbol was missing", not
 * "the file was missing". Fewer new entry points, fewer ways to fail.
 *
 * A user pane's feature bits go in the control's value, which is where a
 * 'CNTL' resource puts them and what the CDEF reads at creation.
 */
static ControlRef MakeControl(const Rect *bounds, short procID, short value)
{
    return NewControl(gWindow, bounds, "\p", true, value, 0, 0, procID, 0);
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
    SetThemeWindowBackground(gWindow, kThemeBrushDocumentWindowBackground,
                             false);
    SetPortWindowPort(gWindow);

    /* Everything else is embedded in this. Without a root control there is
       no hierarchy for SetKeyboardFocus to move a focus around. */
    if (CreateRootControl(gWindow, &gRootControl) != noErr) {
        DisposeWindow(gWindow);
        gWindow = NULL;
        return false;
    }

    gScrollUPP      = NewControlActionUPP(ScrollAction);
    gSidebarLDEF    = NewListDefUPP(SidebarLDEF);
    gArticleLDEF    = NewListDefUPP(ArticleLDEF);
    gReaderDrawUPP  = NewControlUserPaneDrawUPP(ReaderDraw);
    gReaderFocusUPP = NewControlUserPaneFocusUPP(ReaderFocus);
    gReaderTrackUPP = NewControlUserPaneTrackingUPP(ReaderTrack);

    /* Before anything is laid out: every height in the layout comes from
       the font. */
    MeasureFonts();
    LoadRowIcons();

    /* Lay the rectangles out before the lists, so each one is born the size
       it will be drawn at; Layout() then keeps them there. */
    gSelectedFeed    = 0;
    gSelectedArticle = -1;
    gSelectedGroup   = -1;
    Layout();

    /* The headers first, so that they are behind the lists in the hierarchy
       and a list's frame wins where the two meet by a pixel. */
    gSidebarHeaderCtl = MakeControl(&gSidebarHeader,
                                    kControlWindowListViewHeaderProc, 0);
    gListHeaderCtl    = MakeControl(&gListHeader,
                                    kControlWindowListViewHeaderProc, 0);

    if (!MakeListBox(gSidebarLDEF, &gSidebarPane, &gSidebarCtl, &gSidebarList) ||
        !MakeListBox(gArticleLDEF, &gListPane, &gArticleCtl, &gArticleList)) {
        GazetteUIClose();
        return false;
    }

    /*
     * The reader. kControlSupportsFocus puts it in the Tab order and
     * kControlHandlesTracking sends it its own clicks; without the first,
     * AdvanceKeyboardFocus would skip straight past the pane a reader spends
     * all their time in.
     */
    gReaderCtl = MakeControl(&gReaderPane, kControlUserPaneProc,
                             (short)(kControlSupportsFocus |
                                     kControlHandlesTracking));
    if (gReaderCtl == NULL) {
        GazetteUIClose();
        return false;
    }
    (void)SetControlData(gReaderCtl, kControlEntireControl,
                         kControlUserPaneDrawProcTag,
                         sizeof gReaderDrawUPP, (Ptr)&gReaderDrawUPP);
    (void)SetControlData(gReaderCtl, kControlEntireControl,
                         kControlUserPaneFocusProcTag,
                         sizeof gReaderFocusUPP, (Ptr)&gReaderFocusUPP);
    (void)SetControlData(gReaderCtl, kControlEntireControl,
                         kControlUserPaneTrackingProcTag,
                         sizeof gReaderTrackUPP, (Ptr)&gReaderTrackUPP);

    gReaderScroll = MakeScroll(kRefReader);

    /* TEStyleNew remembers the port it was made in, so the window's has to
       be current; the font it is holding becomes the record's default. */
    {
        Rect view;

        ReaderRects(&view);
        TextFont(gReadFont);
        TextSize(gReadSize);
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

    /* The headline list is where a reader starts, so it takes the keyboard. */
    (void)SetKeyboardFocus(gWindow, gArticleCtl, kControlFocusNextPart);

    ShowWindow(gWindow);
    SelectWindow(gWindow);
    return true;
}

void GazetteUIClose(void)
{
    if (gWindow == NULL) {
        return;
    }

    /* The List Box controls own their lists; disposing the window disposes
       the controls, and each one takes its ListHandle with it. */
    gSidebarList       = NULL;
    gArticleList       = NULL;
    gSidebarCtl        = NULL;
    gArticleCtl        = NULL;
    gSidebarHeaderCtl  = NULL;
    gListHeaderCtl     = NULL;
    gReaderCtl         = NULL;
    gRootControl       = NULL;

    if (gReaderTE != NULL) {
        TEDispose(gReaderTE);
        gReaderTE = NULL;
    }
    ReleaseRowIcons();

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
    if (gReaderDrawUPP != NULL) {
        DisposeControlUserPaneDrawUPP(gReaderDrawUPP);
        gReaderDrawUPP = NULL;
    }
    if (gReaderFocusUPP != NULL) {
        DisposeControlUserPaneFocusUPP(gReaderFocusUPP);
        gReaderFocusUPP = NULL;
    }
    if (gReaderTrackUPP != NULL) {
        DisposeControlUserPaneTrackingUPP(gReaderTrackUPP);
        gReaderTrackUPP = NULL;
    }
}

WindowRef GazetteUIWindow(void)
{
    return gWindow;
}
