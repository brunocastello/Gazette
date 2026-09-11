/*
 * Gazette — the Platinum main window
 * Copyright (c) 2026 brunocastello
 *
 * See platinum_window.h. Three panes, two draggable dividers, and a status
 * line.
 *
 * The window is a control hierarchy, not a set of rectangles this file
 * draws into. There is a root control, and every pane is a control embedded
 * in it: the Control Manager decides which one the keyboard is talking to,
 * and each pane draws through the hierarchy.
 *
 * The sidebar, the headline list and the article are **user pane** controls
 * with something of ours inside — a List Manager list for the two lists, a
 * TextEdit record for the article. They were List Box controls (CDEF 22)
 * for a while, which is more of the Toolbox doing the work; but that CDEF
 * always draws a sunken Platinum frame round the whole control, scroll bar
 * included, and always puts its focus ring at that same outer edge, and
 * Outlook Express has neither. OE's resource fork holds no 'CNTL' and no
 * 'ldes' at all — its panes are PowerPlant views drawing themselves — so
 * "use the stock CDEF" and "look like OE" were never the same thing.
 *
 * Only what goes *inside* a cell is ours, through a list definition
 * function handed to CreateCustomList as a ListDefSpec of
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
    /*
     * The draggable border between two panes, measured off Outlook Express.
     * It is not a gap with a line in it — it is a Platinum groove, and the
     * two directions are built differently because of what they abut.
     *
     * Down the side, where it meets the sidebar's scroll bar (whose own
     * edge is the first black line):  white, four grey, black, two grey.
     * Across, where it meets list rows:  two grey, black, white, four grey,
     * black, white.
     */
    kVDividerWidth = 8,
    kHDividerWidth = 8,
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
    kBadgePad       = 5,        /* inside the unread badge, either side */
    kBadgeGap       = 4,        /* between the badge and the name */

    kMinSidebar    = 120,
    kMaxSidebarPad = 160,       /* how much room the right side must keep     */
    kMinListHeight = 3 * kRowHeight,
    kMinReader     = 3 * kReaderLead + 34,  /* the article's header, too */
    kReaderMargin  = 2,         /* above the first line and below the last */

    /*
     * Measured off a screenshot of Outlook Express 5.0.6 rather than
     * guessed at. Every content area in it — both lists and the message
     * pane — sits two pixels in from the window's edges and from each
     * divider, on the window's own grey, and has no frame of any kind. The
     * two pixels of grey *are* the separation.
     */
    kPaneInset     = 2,

    /* The focus border's thickness, and therefore how far a row has to keep
       clear of the edge of the view it is in. */
    kFocusBorder   = 2,

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

/*
 * The two list panes. Each is a user pane control with a List Manager list
 * inside it — not a List Box control, which is what these were until the
 * side-by-side with Outlook Express settled it.
 *
 * CDEF 22 always draws a sunken Platinum frame round the whole control,
 * scroll bar included, and always puts its focus ring at that same outer
 * edge. Neither can be switched off, and OE has neither: its panes are
 * frameless, separated by single rules, and nothing it draws wraps a scroll
 * bar. OE's resource fork has no 'CNTL' and no 'ldes' in it at all — its
 * lists are PowerPlant views drawing themselves, so "use the stock CDEF"
 * and "look like OE" were never the same thing.
 *
 * A user pane is still a real control: it takes the keyboard focus, sits in
 * the Tab order and draws through the hierarchy. It just leaves the border
 * and the ring to us, which is the whole point.
 */
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
static ControlRef gReaderHeaderCtl;

/* The reader is a user pane control, so that it draws through the hierarchy,
   takes the keyboard focus like the two lists and gets a real focus ring
   rather than being a rectangle this file happens to paint. */
static ControlRef gReaderCtl;
static ControlRef gReaderScroll;

/*
 * Which pane control is wearing the focus border.
 *
 * Not the control's value, which is where this used to live: a user pane
 * made with NewControl takes its *feature bits* through the value, so
 * writing a focus flag over them both destroyed the features and made the
 * flag meaningless — the value read 36 from the moment the pane was created,
 * so every pane thought it had the focus until the first click, and none of
 * them had a working one afterwards.
 *
 * Not GetKeyboardFocus either: the focus procedure runs *during*
 * SetKeyboardFocus, before the Control Manager has recorded the change, so
 * asking it from inside the procedure gives the old answer.
 */
static ControlRef gFocusPane;

static ControlActionUPP        gScrollUPP;
static ControlUserPaneDrawUPP  gReaderDrawUPP;
static ControlUserPaneFocusUPP gReaderFocusUPP;
static ControlUserPaneTrackingUPP gReaderTrackUPP;
static ControlUserPaneDrawUPP  gPaneDrawUPP;
static ControlUserPaneFocusUPP gPaneFocusUPP;

/* Pane rectangles, recomputed by Layout() and by nothing else. A list's
   pane is its control's bounds: the frame and the scroll bar are the CDEF's
   business and live inside it, so nothing here has to leave room for them.
   Where a list's rows actually are is a question for the list, and
   GetListViewBounds answers it. */
static Rect gSidebarPane;
static Rect gSidebarHeader;
static Rect gListPane;
static Rect gListHeader;
/*
 * The article's own header, the way OE's message pane has one: a grey bar
 * carrying the headline and the byline, and the white body underneath. The
 * TextEdit record holds the body alone now.
 */
static Rect gReaderHeader;
static char gArticleTitle[kGazetteTitleLen];
static char gArticleByline[192];

static Rect gReaderPane;        /* the body, bar included */
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

/* Two lines of text: the headline and the byline under it. */
static short gReaderHeaderHeight = 34;
static short gReaderLine1        = 13;
static short gReaderLine2        = 26;

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
static ControlRef MakeControl(const Rect *bounds, short procID, short value);
static void DrawFocusBorder(const Rect *view, Boolean on);
static void RefreshFocusBorder(ListHandle list);
static void SetReaderText(void);
static void SizeReader(void);
static void ChooseRow(const GazetteSidebarRow *row);
static int  SelectedRow(void);
static void  SetFocus(short pane);
static short FocusedPane(void);
static Boolean PaneHasFocus(ControlRef control);
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
    /*
     * The status strip is exactly as tall as the grow box, because the grow
     * box sits in it: measured, the box is sixteen pixels and the strip was
     * fourteen, so the box's top edge stood above the strip and cut into the
     * article above. A scroll bar's width is what a grow box is square to,
     * so that is the number.
     *
     * The text is set in the label size rather than the views size — it is a
     * quiet line about what is on screen, and at twelve point it needed a
     * strip taller than OE's whole one — and centred in the strip.
     */
    {
        FontInfo small;

        TextSize(gLabelSize);
        GetFontInfo(&small);
        TextSize(gViewSize);

        gStatusHeight = kScrollWidth - 1;
        gStatusBase   = (short)(small.ascent +
                                (gStatusHeight - small.ascent -
                                 small.descent) / 2);
    }

    /* The article's header holds two lines: the headline, and the byline a
       size down under it. */
    {
        short line = (short)(info.ascent + info.descent + info.leading);

        gReaderLine1        = (short)(kPaneInset + 2 + info.ascent);
        gReaderLine2        = (short)(gReaderLine1 + line);
        gReaderHeaderHeight = (short)(gReaderLine2 + info.descent +
                                      kPaneInset + 3);
    }
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

/*
 * Where the rows go inside a pane. No frame to allow for — the pane has
 * none — only the scroll bar down the right, and a pixel top and bottom
 * because the List Manager draws its bar one pixel taller than the view at
 * each end.
 */
static void ListViewIn(const Rect *pane, Rect *view)
{
    /*
     * The bar goes flush against the pane's right edge, because that is
     * where the article's is and the two have to line up.
     *
     * The view reaches the pane's own bottom, not a pixel short of it. A
     * pixel short left two marks: a line of window grey inside the view
     * along its bottom edge, and — because the List Manager hangs its
     * scroll bar one pixel below the view — the bar's bottom edge landing
     * just above the black rule under it instead of on it, so the two read
     * as a double border.
     *
     * The top keeps its pixel: that is the header's own black rule, which
     * the bar's top edge lands on and the rows must not paint over.
     */
    SetRect(view, pane->left, (short)(pane->top + 1),
            (short)(pane->right - kScrollWidth), pane->bottom);
}

/* Does this pane wear the focus border? */
static Boolean PaneHasFocus(ControlRef control)
{
    return (Boolean)(control != NULL && control == gFocusPane);
}

/* Where a list's rows actually are. */
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
 * A scroll bar with nothing to scroll shows an empty track, not a greyed
 * one: greying is what Platinum does to an *inactive window's* bars, and
 * the List Manager greys its own the moment the range reaches zero. OE's
 * folder list has more room than folders and its bar is drawn normally, so
 * the greying is undone after anything that can change the range.
 */
static void WakeScrollBar(ListHandle list)
{
    ControlRef bar;

    if (list == NULL) {
        return;
    }
    bar = GetListVerticalScrollBar(list);
    if (bar == NULL || GetControlHilite(bar) != 255) {
        return;
    }
    /* Draw1Control as well as HiliteControl: the bar is already on screen in
       its greyed form, and un-greying it without redrawing leaves the grey
       there until something else happens to repaint it. */
    HiliteControl(bar, 0);
    Draw1Control(bar);
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
    WakeScrollBar(list);
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
    RefreshFocusBorder(list);
}

/*
 * Put the focus border back. A cell is drawn across the whole width of the
 * view, so anything that redraws one — a selection moving, a click tracking
 * through the rows — paints over the two pixels of border at either end of
 * that row. Cheaper to restore it afterwards than to teach the list
 * definition function to leave a margin.
 */
static void RefreshFocusBorder(ListHandle list)
{
    ControlRef control;
    Rect       view;

    if (gWindow == NULL || list == NULL) {
        return;
    }
    control = (list == gSidebarList) ? gSidebarCtl
            : (list == gArticleList) ? gArticleCtl : NULL;
    if (!PaneHasFocus(control)) {
        return;
    }
    SetPortWindowPort(gWindow);
    ListView(list, &view);
    DrawFocusBorder(&view, true);
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
/*
 * Move and resize a list pane: the control, then the list inside it.
 * SetListViewBounds places the list and LSize brings its scroll bar along —
 * the List Manager derives the bar's rectangle from the view, so the order
 * matters.
 */
static void SizeListPane(ControlRef control, ListHandle list,
                         const Rect *bounds)
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

    ListViewIn(bounds, &view);
    SetListViewBounds(list, &view);
    LSize((short)(view.right - view.left),
          (short)(view.bottom - view.top), list);

    cell.v = gRowHeight;
    cell.h = (short)(view.right - view.left);
    if (cell.h > 0) {
        LCellSize(cell, list);
    }
    LSetDrawingMode(true, list);
    WakeScrollBar(list);
}

static void Layout(void)
{
    Rect  bounds;
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

    /*
     * Every edge lands *on* the line beside it, never a pixel before it.
     *
     * A pane's scroll bar draws its own edge at the pane's last pixel, and
     * a divider draws its rule down its middle; if the pane stops short,
     * the two lines sit side by side with a pixel of grey between and the
     * whole thing reads as loose. So each pane is extended by one pixel
     * past the rule it meets, and the bar's edge becomes that rule. The
     * same goes for the headers, whose own side and top edges are made to
     * fall on the window's border or the divider's rule rather than beside
     * them.
     *
     * vRule and hRule are those columns and rows, worked out once.
     */
    {
        short split   = (short)(bounds.left + gSidebarWidth);
        short headTop = (short)(bounds.top - 1);
        short headBot = (short)(bounds.top + gHeaderHeight);

        /*
         * The border runs the whole height of the window, top to bottom,
         * rather than starting below the headers — so the two headers stop
         * either side of it and the groove is drawn over the gap between
         * them. It used to stop at the headers and pick up again below,
         * which left the line looking cut.
         */
        /*
         * The headline header starts *on* the groove's black column, so its
         * own left edge line falls on that black rather than beside it.
         * One pixel further right and the two sat side by side, two black
         * lines where the border should be one.
         */
        SetRect(&gSidebarHeader, (short)(bounds.left - 1), headTop,
                split, headBot);
        SetRect(&gListHeader, (short)(split + 5), headTop,
                (short)(bounds.right + 1), headBot);

        SetRect(&gVDivider, split, bounds.top,
                (short)(split + kVDividerWidth), contentBottom);

        /*
         * A pane starts one pixel *inside* the header above it, so that its
         * scroll bar's top edge lands on the header's own black rule rather
         * than drawing a second line directly under it. Now that the header
         * draws that rule, the two were stacking up two pixels thick
         * wherever a bar met a header.
         */
        SetRect(&gSidebarPane, bounds.left, (short)(headBot - 1), split,
                contentBottom);

        listBottom = (short)(headBot +
                             (long)(contentBottom - headBot) *
                             gListShare / 100);
        if (listBottom < headBot + kMinListHeight) {
            listBottom = (short)(headBot + kMinListHeight);
        }
        if (listBottom > contentBottom - kMinReader - kHDividerWidth) {
            listBottom = (short)(contentBottom - kMinReader - kHDividerWidth);
        }

        /*
         * Starting one pixel past the groove's black, so the rows meet the
         * border with nothing between. The groove's own trailing two pixels
         * of grey are covered by the pane, which is what takes the grey
         * edge off the inside of the view.
         */
        SetRect(&gListPane, (short)(split + 6), (short)(headBot - 1),
                (short)(bounds.right + 1), listBottom);

        /*
         * Starting on the vertical groove's own black column, so the two
         * borders meet and turn a corner rather than stopping short of each
         * other with a gap between.
         */
        SetRect(&gHDivider, (short)(split + 5), listBottom,
                (short)(bounds.right + 1),
                (short)(listBottom + kHDividerWidth));

        /*
         * Two pixels up, so the header control's own top line and highlight
         * fall exactly on the groove's closing black and white rather than
         * adding a third line under them. Measured: black, white, then
         * another black — that last one was this.
         */
        SetRect(&gReaderHeader, (short)(split + 5),
                (short)(gHDivider.bottom - 2),
                (short)(bounds.right + 1),
                (short)(gHDivider.bottom - 2 + gReaderHeaderHeight));

        SetRect(&gReaderPane, (short)(split + 6),
                (short)(gReaderHeader.bottom - 1),
                (short)(bounds.right + 1), (short)(contentBottom + 1));

        /*
         * The text starts a pixel inside the pane, the way a list's rows do.
         * Erasing from the pane's own top wiped out the header's black rule
         * across the whole width — the bar's top edge needs to land on that
         * rule, but the article's white must not be painted over it.
         */
        /*
         * A pixel short of the pane at *both* ends, and for two different
         * reasons.
         *
         * At the top the pixel is the header's black rule: the scroll bar's
         * top edge lands on it and the article's white must not paint over
         * it.
         *
         * At the bottom the pixel is the *status strip's* rule. The pane
         * reaches a row past it so the scroll bar's bottom edge lands there,
         * but the text must not — erasing the pane's full height painted the
         * rule white, and only a full window update put it back. Changing
         * article redraws the reader alone, so the border simply vanished
         * until something else repainted the window.
         */
        SetRect(&gReaderRect, gReaderPane.left,
                (short)(gReaderPane.top + 1),
                (short)(gReaderPane.right - kScrollWidth),
                (short)(gReaderPane.bottom - 1));
    }

    /* End to end. The strip's own rule is the line between it and the panes
       above, and every one of their scroll bars ends on it. */
    SetRect(&gStatusRect, bounds.left, contentBottom,
            bounds.right, bounds.bottom);

    SizeListPane(gSidebarCtl, gSidebarList, &gSidebarPane);
    SizeListPane(gArticleCtl, gArticleList, &gListPane);

    if (gSidebarHeaderCtl != NULL) {
        SetControlBounds(gSidebarHeaderCtl, &gSidebarHeader);
    }
    if (gListHeaderCtl != NULL) {
        SetControlBounds(gListHeaderCtl, &gListHeader);
    }
    if (gReaderHeaderCtl != NULL) {
        SetControlBounds(gReaderHeaderCtl, &gReaderHeader);
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
    size_t  used = 0;
    char    when[16];

    if (gWindow == NULL || gReaderTE == NULL) {
        return;
    }

    GetPort(&savePort);
    SetPortWindowPort(gWindow);

    TEDeactivate(gReaderTE);
    gReaderText[0]    = '\0';
    gArticleTitle[0]  = '\0';
    gArticleByline[0] = '\0';

    a = GazetteFeedsArticleAt(gSelectedArticle);
    if (a == NULL) {
        static const char kNothing[] = "Select a headline to read it.";

        used = AppendText(0, kNothing, sizeof kNothing - 1);
        TESetText(gReaderText, (long)used, gReaderTE);
        ApplyRunStyle(0, (long)used, normal, gReadSize);
    } else {
        const char *from = a->source;
        const char *body = a->body;

        /* The headline and the byline go on the header bar, not into the
           text: OE puts its Subject: and From: lines on one and the message
           itself underneath, and the two scroll separately for it. */
        (void)gz_copy_n(gArticleTitle, sizeof gArticleTitle,
                        a->title, strlen(a->title));

        GazetteFormatDate(a->date, UnixNow(), when, sizeof when);

        /* In a group view the articles come from several feeds, so which one
           this is from is worth saying. The feed's own name stands in when
           the article does not name a publisher. */
        if (from[0] == '\0' && GazetteFeedsCurrentGroup() >= 0) {
            from = GazetteCoreFeedTitle(a->feed);
        }
        if (from[0] != '\0' && when[0] != '\0') {
            snprintf(gArticleByline, sizeof gArticleByline, "%s - %s",
                     from, when);
        } else if (from[0] != '\0') {
            snprintf(gArticleByline, sizeof gArticleByline, "%s", from);
        } else {
            snprintf(gArticleByline, sizeof gArticleByline, "%s", when);
        }

        /*
         * The article's own page when it has been fetched and extracted, and
         * the feed's summary otherwise. The store answers which article the
         * held text belongs to, so switching articles cannot show the last
         * one's body under this one's headline.
         */
        if (GazetteFeedsFullTextArticle() == gSelectedArticle) {
            const char *full = GazetteFeedsFullText();

            if (full[0] != '\0') {
                body = full;
            }
        }

        if (body[0] != '\0') {
            used = AppendBody(0, body);
        } else {
            static const char kNone[] = "(This feed carries no summary for "
                                        "this article.)";

            used = AppendText(0, kNone, sizeof kNone - 1);
        }

        TESetText(gReaderText, (long)used, gReaderTE);
        ApplyRunStyle(0, (long)used, normal, gReadSize);
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
       the list's own grey has to be current or the triangle arrives sitting
       in a white square. */
    SetThemeBackground(kThemeBrushListViewBackground, 8, true);
    (void)DrawThemeButton(&box, kThemeDisclosureButton, &info, NULL,
                          NULL, NULL, 0);
    SetThemeBackground(kThemeBrushDialogBackgroundActive, 8, true);
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

/*
 * The unread count, as a rounded badge pinned to the right hand end of the
 * row — a shape rather than a number in brackets, so the eye finds it in a
 * column instead of reading to the end of every name to see whether one is
 * there.
 *
 * Answers how wide it is without drawing, so the name knows how much room
 * it has to be truncated into; pass NULL for the rect to ask only.
 */
static short BadgeWidth(int count)
{
    char  text[16];
    short len;

    if (count <= 0) {
        return 0;
    }
    snprintf(text, sizeof text, "%d", count);
    len = (short)strlen(text);
    return (short)(TextWidth(text, 0, len) + kBadgePad * 2 + kBadgeGap);
}

static void DrawBadge(short right, short baseline, int count)
{
    char     text[16];
    short    len;
    short    w;
    Rect     pill;
    RGBColor fill;
    RGBColor save;

    if (count <= 0) {
        return;
    }
    snprintf(text, sizeof text, "%d", count);
    len = (short)strlen(text);
    w   = (short)TextWidth(text, 0, len);

    pill.right  = right;
    pill.left   = (short)(right - w - kBadgePad * 2);
    pill.top    = (short)(baseline - gRowAscent);
    pill.bottom = (short)(baseline + gRowDescent);

    GetForeColor(&save);

    /* A mid grey the count reads out of in white, which is what the badge
       does everywhere it turns up and what keeps it quiet next to a name. */
    fill.red = fill.green = fill.blue = 150 * 257;
    RGBForeColor(&fill);
    PaintRoundRect(&pill, (short)(pill.bottom - pill.top),
                   (short)(pill.bottom - pill.top));

    ForeColor(whiteColor);
    TextFace(normal);
    MoveTo((short)(pill.left + kBadgePad), baseline);
    DrawText(text, 0, len);

    RGBForeColor(&save);
}

/* "Name (12)", with the count only when there is one. Drawn as one string so
   the truncation takes the name and never the number — the count is the part
   that has to stay legible in a narrow sidebar. */
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
 * The backgrounds, measured off Outlook Express rather than reasoned about:
 *
 *   its folder list   (235,235,235)   kThemeBrushListViewBackground
 *   its message list  (235,235,235)   the same
 *   its message pane  (255,255,255)   white
 *   its chrome        (216,216,216)   the window's own grey
 *
 * So both lists are the theme's list-view background — which is a light
 * grey in Platinum and not white — and only the article, which is text
 * rather than a list, is white. This file had the sidebar on the *dialog*
 * background (216, too dark) and the headline list on white (too light),
 * which is two mistakes in opposite directions.
 */
/*
 * Erase, and *leave the brush set*. It used to put the window's grey back
 * before returning, which meant the very next thing to erase on its own
 * account did so in grey — TEUpdate does exactly that, so the article came
 * out on a grey ground however carefully its rectangle had just been
 * painted white. Whoever wants a different background asks for one.
 */
static void EraseWith(const Rect *r, ThemeBrush brush)
{
    SetThemeBackground(brush, 8, true);
    EraseRect(r);
}

/*
 * One row of the sidebar, laid out the way Outlook Express lays its folder
 * list out: the disclosure triangle's column, then a small icon, then the
 * name. A feed inside a group is indented by one step; a group's own row is
 * the only one that draws a triangle.
 */
/*
 * A row never draws in the two pixels at either edge of the view, because
 * that is where the focus border lives. Redrawing it afterwards is not
 * enough: the List Manager repaints the row under the mouse over and over
 * while a click is held, so the border was being cut through for as long as
 * the button was down and only healed on release.
 */
static void RowRect(const Rect *cell, Rect *out)
{
    *out = *cell;
    out->left  = (short)(out->left + kFocusBorder);
    out->right = (short)(out->right - kFocusBorder);
}

static void DrawSidebarCell(const Rect *full, short row, Boolean selected)
{
    Rect              cellRect;
    const Rect       *cell = &cellRect;
    GazetteSidebarRow r;
    char              label[kGazetteTitleLen + 32];
    short             labelLen = 0;
    short             iconLeft;
    short             textLeft;
    short             baseline;
    short             width;
    short             badge;
    int               unread = 0;
    Boolean           enabled = true;

    RowRect(full, &cellRect);

    EraseWith(cell, kThemeBrushListViewBackground);
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

    /*
     * The weight is decided before the label is measured, because a bold
     * name is wider than a plain one and the selection is drawn to fit.
     *
     * A category is bold; a feed never is. A feed with unread articles used
     * to be bold as well, which said the same thing the badge says and the
     * highlight says — three ways of saying "this one", where one will do.
     */
    if (r.kind == kGazetteRowGroup) {
        TextFace(bold);
        unread = GroupUnread(r.index);
    } else {
        enabled = GazetteCoreFeedEnabled(r.index);
        unread  = enabled ? FeedUnread(r.index) : 0;
        TextFace(normal);
    }

    /* The badge is pinned right, so the name is truncated into what is left
       rather than the two overlapping. */
    badge = BadgeWidth(unread);
    width = BuildRowLabel(r.kind == kGazetteRowGroup
                              ? GazetteCoreGroupName(r.index)
                              : GazetteCoreFeedTitle(r.index),
                          0,
                          (short)(cell->right - kTextInset - textLeft - badge),
                          label, sizeof label, &labelLen);

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

    DrawBadge((short)(cell->right - kTextInset), baseline, unread);

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

static void DrawArticleCell(const Rect *full, short row, Boolean selected)
{
    const GazetteArticle *a = GazetteFeedsArticleAt(row);
    Rect                  cellRect;
    const Rect           *cell = &cellRect;
    char                  when[16];
    short                 baseline;

    RowRect(full, &cellRect);

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

/*
 * A list pane draws itself: its background, its rows, and — when it has the
 * keyboard — a focus ring round the rows *only*. The ring stops short of the
 * scroll bar because that is what Outlook Express does; a ring that wraps a
 * scroll bar is CDEF 22's habit and it is the reason these are user panes.
 */
static void DrawListPane(ControlRef control, ListHandle list, int rows,
                         ThemeBrush brush)
{
    Rect      pane;
    Rect      view;
    RgnHandle clip = NULL;
    RgnHandle rgn  = NULL;

    if (gWindow == NULL || control == NULL || list == NULL) {
        return;
    }
    SetPortWindowPort(gWindow);
    GetControlBounds(control, &pane);
    ListView(list, &view);
    if (view.right <= view.left) {
        return;
    }

    clip = NewRgn();
    if (clip != NULL) {
        GetClip(clip);
    }
    /* Never outside the rows: the scroll bar is a control of its own and
       draws itself, and nothing here may paint over it. */
    ClipRect(&view);

    EraseWith(&view, brush);

    rgn = NewRgn();
    if (rgn != NULL) {
        RectRgn(rgn, &view);
        LUpdate(rgn, list);
        DisposeRgn(rgn);
    }

    /* Below the last row the list has drawn nothing, so the pane's own
       colour goes down over whatever is there. */
    FillListRemainder(list, rows, brush);

    /* LUpdate greys the bar again on its way past, so this goes after it. */
    WakeScrollBar(list);

    DrawFocusBorder(&view, PaneHasFocus(control));

    if (clip != NULL) {
        SetClip(clip);
        DisposeRgn(clip);
    }
}

static pascal void PaneDraw(ControlRef control, SInt16 part)
{
    (void)part;

    if (control == gSidebarCtl) {
        DrawListPane(control, gSidebarList, GazetteCoreSidebarRowCount(),
                     kThemeBrushListViewBackground);
    } else if (control == gArticleCtl) {
        DrawListPane(control, gArticleList, GazetteFeedsArticleCount(),
                     kThemeBrushWhite);
    }
}

static pascal ControlPartCode PaneFocus(ControlRef control,
                                        ControlFocusPart action)
{
    if (control == NULL) {
        return kControlFocusNoPart;
    }
    if (action == kControlFocusNoPart) {
        if (gFocusPane == control) {
            gFocusPane = NULL;
        }
        Draw1Control(control);
        return kControlFocusNoPart;
    }

    gFocusPane = control;
    Draw1Control(control);
    return kControlReaderFocusPart;
}

static void DrawSidebarPane(void)
{
    if (gWindow == NULL || gSidebarCtl == NULL) {
        return;
    }
    SetPortWindowPort(gWindow);
    Draw1Control(gSidebarCtl);
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
 * whenever the focus ring has to change.
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

    /* Round the text only, stopping short of the scroll bar — the same rule
       the two lists follow. */
    DrawFocusBorder(&gReaderRect, PaneHasFocus(gReaderCtl));
}

static void DrawReader(void)
{
    if (gWindow == NULL || gReaderCtl == NULL) {
        return;
    }
    SetPortWindowPort(gWindow);
    Draw1Control(gReaderCtl);
}

/*
 * The focus border. Outlook Express marks the pane the keyboard is talking
 * to with a two-pixel blue rectangle just inside the view's own edges —
 * measured at (91,91,197), two pixels thick, sitting in the outermost two
 * pixels of the rows area and stopping at the scroll bar's left edge rather
 * than going round it.
 *
 * Not DrawThemeFocusRect, which draws Platinum's own focus ring *outside*
 * the rectangle it is given and in the theme's highlight colour. This is
 * OE's, and OE's is what was asked for.
 */
static void DrawFocusBorder(const Rect *view, Boolean on)
{
    RGBColor blue;
    Rect     r = *view;
    short    i;

    if (view->right <= view->left || view->bottom <= view->top) {
        return;
    }

    if (on) {
        blue.red   = 91 * 257;
        blue.green = 91 * 257;
        blue.blue  = 197 * 257;
        RGBForeColor(&blue);
    } else {
        /* Rubbing it out again: the same two pixels in whatever the view is
           backed with, so the rows keep their own colour behind them. */
        return;
    }

    PenNormal();
    for (i = 0; i < 2; i++) {
        FrameRect(&r);
        InsetRect(&r, 1, 1);
    }
    ForeColor(blackColor);
}

/* One line of a groove, in the grey given. */
static void GreyPen(short grey)
{
    RGBColor c;

    c.red = c.green = c.blue = (unsigned short)(grey * 257);
    RGBForeColor(&c);
}

/*
 * The border between two panes, measured off Outlook Express pixel by
 * pixel. It is a Platinum groove, not a gap with a line down it.
 *
 * Down the side it abuts the sidebar's scroll bar, whose own black edge
 * serves as the groove's first line, so it draws: white, four of grey,
 * black, two of grey.
 *
 * Across, it abuts list rows and has to supply both lines itself: two of
 * grey, black, white, four of grey, black, white.
 */
static void DrawVDivider(const Rect *r)
{
    EraseWith(r, kThemeBrushDialogBackgroundActive);

    GreyPen(255);
    MoveTo(r->left, r->top);
    LineTo(r->left, (short)(r->bottom - 1));

    GreyPen(0);
    MoveTo((short)(r->left + 5), r->top);
    LineTo((short)(r->left + 5), (short)(r->bottom - 1));

    ForeColor(blackColor);
}

static void DrawHDivider(const Rect *r)
{
    short i;

    EraseWith(r, kThemeBrushDialogBackgroundActive);

    /* Black on the first row, so the rows and the scroll bar above end
       against it rather than on two pixels of grey. */
    for (i = 0; i < 2; i++) {
        short y = (short)(r->top + (i ? 6 : 0));

        GreyPen(0);
        MoveTo(r->left, y);
        LineTo((short)(r->right - 1), y);

        GreyPen(255);
        MoveTo(r->left, (short)(y + 1));
        LineTo((short)(r->right - 1), (short)(y + 1));
    }
    ForeColor(blackColor);
}

/*
 * The dotted grab handle that says a border can be dragged, measured off
 * Outlook Express: five dots four pixels apart, each one a dark pixel with
 * a white highlight set diagonally below and right of it, sitting one pixel
 * into the groove's grey band. Ours was a single flat black pixel in the
 * middle of the band, which is neither the right colour, the right count,
 * nor the right place.
 */
static void DrawGrabHandle(const Rect *divider, Boolean vertical)
{
    RGBColor dark;
    RGBColor light;
    short    i;

    dark.red  = dark.green  = dark.blue  = 29 * 257;
    light.red = light.green = light.blue = 0xFFFF;

    if (vertical) {
        short x  = (short)(divider->left + 2);
        short at = (short)((divider->top + divider->bottom) / 2 - 8);

        for (i = 0; i < 5; i++) {
            RGBForeColor(&dark);
            MoveTo(x, at);
            LineTo(x, at);
            RGBForeColor(&light);
            MoveTo((short)(x + 1), (short)(at + 1));
            LineTo((short)(x + 1), (short)(at + 1));
            at = (short)(at + 4);
        }
    } else {
        short y  = (short)(divider->top + 3);
        short at = (short)((divider->left + divider->right) / 2 - 8);

        for (i = 0; i < 5; i++) {
            RGBForeColor(&dark);
            MoveTo(at, y);
            LineTo(at, y);
            RGBForeColor(&light);
            MoveTo((short)(at + 1), (short)(y + 1));
            LineTo((short)(at + 1), (short)(y + 1));
            at = (short)(at + 4);
        }
    }
    ForeColor(blackColor);
}

/*
 * The article's headline and byline, drawn on the header bar under the
 * splitter. The headline is one line and truncated, as OE's Subject: line
 * is; the byline is a size down, which is what a label is.
 */
static void DrawReaderHeaderText(void)
{
    short right;

    if (gWindow == NULL) {
        return;
    }
    SetPortWindowPort(gWindow);
    right = (short)(gReaderHeader.right - kTextInset - kScrollWidth);

    SetThemeTextColor(kThemeTextColorWindowHeaderActive, 8, true);

    UseViewFont();
    TextFace(bold);
    MoveTo((short)(gReaderHeader.left + kTextInset + 2),
           (short)(gReaderHeader.top + gReaderLine1));
    DrawTruncated(gArticleTitle,
                  (short)(right - gReaderHeader.left - kTextInset - 2));

    TextFace(normal);
    TextSize(gLabelSize);
    MoveTo((short)(gReaderHeader.left + kTextInset + 2),
           (short)(gReaderHeader.top + gReaderLine2));
    DrawTruncated(gArticleByline,
                  (short)(right - gReaderHeader.left - kTextInset - 2));

    ForeColor(blackColor);
}

/* The bar and the text on it, for when the article has changed. */
static void DrawReaderHeader(void)
{
    if (gWindow == NULL || gReaderHeaderCtl == NULL) {
        return;
    }
    SetPortWindowPort(gWindow);
    Draw1Control(gReaderHeaderCtl);
    DrawReaderHeaderText();
}

/* Just the text. The strip under it is the window's own background. */
static void DrawStatusText(void)
{
    if (gWindow == NULL) {
        return;
    }
    SetPortWindowPort(gWindow);

    /*
     * Flat, with a black line along the top. It was an etched separator,
     * which is a lighter grey than the sidebar's own border carrying on
     * past it — two different lines meeting, and the join showed. One black
     * rule, end to end, is also the line every scroll bar above it ends on.
     */
    EraseWith(&gStatusRect, kThemeBrushDialogBackgroundActive);
    ForeColor(blackColor);
    MoveTo(gStatusRect.left, gStatusRect.top);
    LineTo((short)(gStatusRect.right - 1), gStatusRect.top);

    UseViewFont();
    TextSize(gLabelSize);
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

    /* kThemeBrushDialogBackgroundActive, not the dialog one: this is a
       kDocumentWindowClass window and the two brushes are different greys. */
    SetThemeBackground(kThemeBrushDialogBackgroundActive, 8, true);
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
    DrawVDivider(&gVDivider);
    DrawHDivider(&gHDivider);
    DrawGrabHandle(&gVDivider, true);
    DrawGrabHandle(&gHDivider, false);

    /* The whole control hierarchy in one call — the two lists with their
       frames, scroll bars and focus rings, the two window headers, the
       reader and its bar, and the status placard. */
    DrawControls(gWindow);

    /* Their titles go on top of them: a window header control and a placard
       have no text of their own. */
    DrawHeaderTitle(&gSidebarHeader, "Feeds");
    DrawHeaderTitle(&gListHeader, header);
    DrawReaderHeaderText();
    DrawStatusText();

    /*
     * The grow box lives in the content region, so the application draws it.
     * Clipped to its own corner, though: DrawGrowIcon's older duty is to
     * delimit a window's scroll bar gutters, so left to itself it rules a
     * line all the way along the bottom of the content region and up the
     * right hand side. Gazette's scroll bars are inside its panes and it
     * wants neither line — that stray rule across the status strip was this.
     */
    {
        RgnHandle save = NewRgn();
        Rect      corner;

        if (save != NULL) {
            GetClip(save);
        }
        SetRect(&corner, (short)(bounds.right - kScrollWidth),
                (short)(bounds.bottom - kScrollWidth),
                bounds.right, bounds.bottom);
        ClipRect(&corner);
        DrawGrowIcon(gWindow);
        if (save != NULL) {
            SetClip(save);
            DisposeRgn(save);
        }
    }

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
    DrawReaderHeader();
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
    Rect              rows;
    int               at;

    /* The triangle's own column opens and shuts a group; the rest of the
       line selects it, the way a folder behaves in a list view. */
    if (HitDisclosure(where)) {
        return;
    }

    ListView(gSidebarList, &rows);
    (void)LClick(where, modifiers, gSidebarList);
    RefreshFocusBorder(gSidebarList);

    /* A click on the scroll bar scrolls and nothing else — it must not be
       read as choosing whatever row is still selected. */
    if (!PtInRect(where, &rows)) {
        return;
    }

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
        if (gFocusPane == control) {
            gFocusPane = NULL;
        }
        if (gReaderTE != NULL) {
            SetPortWindowPort(gWindow);
            TEDeactivate(gReaderTE);
            TESetSelect(0, 0, gReaderTE);
        }
        Draw1Control(control);
        return kControlFocusNoPart;
    }

    gFocusPane = control;
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

    /*
     * The dividers are asked first. Each pane now reaches a few pixels into
     * the divider beside it, so that its scroll bar's edge lands on the
     * divider's rule — which means the two overlap, and the divider has to
     * win there or it could never be grabbed.
     */
    if (PtInRect(where, &gVDivider)) {
        TrackDivider(where, true);
        return;
    }
    if (PtInRect(where, &gHDivider)) {
        TrackDivider(where, false);
        return;
    }

    /*
     * The list panes are hit-tested by rectangle rather than by
     * FindControlUnderMouse, because a list's scroll bar is a control of its
     * own sitting inside the pane: the Control Manager would hand back the
     * bar, whereas LClick wants the click whether it landed on a row or on
     * the bar and tracks either.
     */
    if (PtInRect(where, &gSidebarPane)) {
        SetFocus(kRefSidebar);
        SidebarClicked(where, modifiers);
        return;
    }

    if (PtInRect(where, &gListPane)) {
        Rect rows;

        SetFocus(kRefList);
        ListView(gArticleList, &rows);

        (void)LClick(where, modifiers, gArticleList);
        RefreshFocusBorder(gArticleList);

        /*
         * Only a click in the rows opens an article. LClick tracks the
         * scroll bar as readily as it tracks a drag through the rows, and
         * opening whatever happened to stay selected after a scroll would
         * call SelectArticle — which reveals the selection, scrolling the
         * list straight back to where it started. Which is exactly what it
         * did.
         */
        if (!PtInRect(where, &rows)) {
            return;
        }
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

    /*
     * The article's scroll bar sits inside the article's own user pane, and
     * the pane covers it. Asking FindControlUnderMouse which control is
     * under the mouse and testing for the pane first handed every bar click
     * to the pane's tracking procedure, which passed it to TEClick — so the
     * bar selected text instead of scrolling, and the article would not move
     * at all. Its rectangle is asked about first, as the lists' are.
     */
    if (gReaderScroll != NULL && PtInRect(where, &gReaderPane)) {
        Rect bar;

        GetControlBounds(gReaderScroll, &bar);
        if (PtInRect(where, &bar)) {
            SetFocus(kRefReader);
            part = TestControl(gReaderScroll, where);
            if (part == 0) {
                return;                 /* nothing to scroll */
            }
            if (part == kControlIndicatorPart) {
                /*
                 * The thumb tracks itself and leaves its new value in the
                 * control; the article still has to be told to go there.
                 * Redrawing the pane was not enough — it drew the article at
                 * the offset it already had, so the thumb moved and nothing
                 * else did, and only the arrows appeared to work.
                 */
                if (TrackControl(gReaderScroll, where, NULL) ==
                    kControlIndicatorPart) {
                    ScrollReaderTo(GetControlValue(gReaderScroll));
                }
            } else {
                TrackControl(gReaderScroll, where, gScrollUPP);
            }
            return;
        }

        SetFocus(kRefReader);
        ReaderClick(where, modifiers);
        return;
    }

    control = FindControlUnderMouse(where, gWindow, &part);
    (void)control;
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

    /* The lists grey their own scroll bars and selections. */
    if (gSidebarList != NULL) {
        LActivate(active, gSidebarList);
    }
    if (gArticleList != NULL) {
        LActivate(active, gArticleList);
    }
    if (active) {
        /* LActivate decides a bar with nothing to scroll should be greyed;
           Platinum's answer is an empty track drawn normally, so it is put
           back. Measured: OE's edges are black, ours were (75,75,75). */
        WakeScrollBar(gSidebarList);
        WakeScrollBar(gArticleList);
    }

    /* And one call for the rest of the hierarchy. */
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
    DrawReaderHeader();
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
 * ListDefSpec goes straight into CreateCustomList, so no 'LDEF'
 * resource and no RegisterListDefinition are involved — the first is
 * impossible under Carbon and the second would cost CarbonLib 1.5.
 *
 * It starts with no rows; the shell fills it in through
 * GazetteUIFeedsChanged and GazetteUIArticlesChanged.
 */
/*
 * A list pane: a user pane control, and a List Manager list living inside
 * it. The list brings its own scroll bar — a real Control Manager one — and
 * puts it down the right hand edge of the view it is given, which is where
 * OE's sits.
 *
 * The ListDefSpec is the same one CDEF 22 was being handed, so the cells are
 * drawn by exactly the code as before; only the chrome around them changed.
 */
static Boolean MakeListPane(ListDefUPP defProc, const Rect *bounds,
                            ControlRef *outControl, ListHandle *outList)
{
    ListDefSpec spec;
    ListBounds  data;
    Rect        view;
    Point       cell;

    *outControl = NULL;
    *outList    = NULL;

    *outControl = MakeControl(bounds, kControlUserPaneProc,
                              (short)(kControlSupportsFocus |
                                      kControlHandlesTracking));
    if (*outControl == NULL) {
        return false;
    }

    spec.defType    = kListDefUserProcType;
    spec.u.userProc = defProc;

    ListViewIn(bounds, &view);
    SetRect(&data, 0, 0, 1, 0);         /* one column, no rows yet */
    cell.v = gRowHeight;
    cell.h = (short)(view.right - view.left);

    if (CreateCustomList(&view, &data, cell, &spec, gWindow,
                         false, false, false, true, outList) != noErr ||
        *outList == NULL) {
        return false;
    }

    /* One row at a time, and no drag-selecting several. lOnlyOne is a
       negative constant in a byte-wide field, so it is masked rather than
       sign-extended into the flags word. */
    SetListSelectionFlags(*outList, (OptionBits)(lOnlyOne & 0xFF));
    return true;
}

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
    /* kThemeBrushDialogBackgroundActive, not the document one: on Mac OS 9
       a *document* window's background brush is white, and this window's
       chrome is Platinum grey — measured off Outlook Express at
       (216,216,216). Using the document brush is what made the status strip
       and every two-pixel margin come out white. */
    SetThemeWindowBackground(gWindow, kThemeBrushDialogBackgroundActive,
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
    gPaneDrawUPP    = NewControlUserPaneDrawUPP(PaneDraw);
    gPaneFocusUPP   = NewControlUserPaneFocusUPP(PaneFocus);

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
    /*
     * kControlWindowHeaderProc, not the list-view variant. The variant is
     * documented as "for list views — no bottom line", and that is exactly
     * what was missing: measured against OE, its headers close with a grey
     * shadow and then a black rule, and ours stopped at the shadow. The
     * black line under a header is what encloses the list beneath it.
     */
    gSidebarHeaderCtl = MakeControl(&gSidebarHeader,
                                    kControlWindowHeaderProc, 0);
    gListHeaderCtl    = MakeControl(&gListHeader,
                                    kControlWindowHeaderProc, 0);
    gReaderHeaderCtl  = MakeControl(&gReaderHeader,
                                    kControlWindowHeaderProc, 0);

    if (!MakeListPane(gSidebarLDEF, &gSidebarPane, &gSidebarCtl,
                      &gSidebarList) ||
        !MakeListPane(gArticleLDEF, &gListPane, &gArticleCtl,
                      &gArticleList)) {
        GazetteUIClose();
        return false;
    }
    (void)SetControlData(gSidebarCtl, kControlEntireControl,
                         kControlUserPaneDrawProcTag,
                         sizeof gPaneDrawUPP, (Ptr)&gPaneDrawUPP);
    (void)SetControlData(gSidebarCtl, kControlEntireControl,
                         kControlUserPaneFocusProcTag,
                         sizeof gPaneFocusUPP, (Ptr)&gPaneFocusUPP);
    (void)SetControlData(gArticleCtl, kControlEntireControl,
                         kControlUserPaneDrawProcTag,
                         sizeof gPaneDrawUPP, (Ptr)&gPaneDrawUPP);
    (void)SetControlData(gArticleCtl, kControlEntireControl,
                         kControlUserPaneFocusProcTag,
                         sizeof gPaneFocusUPP, (Ptr)&gPaneFocusUPP);

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

    /* The lists own their scroll bars, so they go before the window does. */
    if (gSidebarList != NULL) {
        LDispose(gSidebarList);
    }
    if (gArticleList != NULL) {
        LDispose(gArticleList);
    }
    gSidebarList       = NULL;
    gArticleList       = NULL;
    gSidebarCtl        = NULL;
    gArticleCtl        = NULL;
    gSidebarHeaderCtl  = NULL;
    gListHeaderCtl     = NULL;
    gReaderHeaderCtl   = NULL;
    gReaderCtl         = NULL;
    gFocusPane         = NULL;
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
    if (gPaneDrawUPP != NULL) {
        DisposeControlUserPaneDrawUPP(gPaneDrawUPP);
        gPaneDrawUPP = NULL;
    }
    if (gPaneFocusUPP != NULL) {
        DisposeControlUserPaneFocusUPP(gPaneFocusUPP);
        gPaneFocusUPP = NULL;
    }
}

WindowRef GazetteUIWindow(void)
{
    return gWindow;
}
