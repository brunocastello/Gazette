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
#include "feeds/gazette_photos.h"
#include "portable/gazette_portable.h"

#include <Appearance.h>
#include <Controls.h>
#include <ControlDefinitions.h>
#include <DateTimeUtils.h>
#include <Drag.h>     /* WaitMouseMoved: a press that becomes a drag */
#include <Events.h>   /* GetMouse and StillDown, for the toolbar's buttons */
#include <Folders.h>   /* kOnSystemDisk, which GetIconRef wants */
#include <Fonts.h>
#include <Gestalt.h>            /* whether QuickTime is there at all */
#include <Icons.h>
#include <ImageCompression.h>   /* the Graphics Importers: a JPEG into a GWorld */
#include <Movies.h>             /* EnterMovies, which the importers stand on */
#include <QDOffscreen.h>
#include <QuickTimeComponents.h> /* kQTFileTypeJPEG and its neighbours */
#include <Lists.h>
#include <Menus.h>
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
    kTextInset     = 4,
    kBaseline      = 10,        /* likewise, until gRowBaseline is worked out */

    /* The sidebar is an outline: a column for the disclosure triangle, then
       one indent for a group's feeds. A top-level feed is a group's sibling,
       so it starts where a group's name does. */
    kTriangleSize   = 12,       /* what the Appearance Manager draws into */
    kTriangleColumn = 14,       /* the triangle's own column, with its gap */
    kGroupIndent    = 16,
    kIconSize       = 16,       /* the small icon beside a row's name */
    kIconGap        = 3,
    kCountGap       = 8,        /* between the name and its unread count */

    kMinSidebar    = 120,
    kMinList       = 220,       /* divider to divider, the middle column      */
    kMinReader     = 200,       /* what is left for the article               */
    kTitleBarHeight = 22,       /* a document window's, on Platinum           */
    kReaderMargin  = 6,         /* above the first line and below the last */
    /*
     * And down either side of it. Wider than kTextInset, which is what a list
     * row uses: a row is a line of a table and wants to start near its edge,
     * whereas a column of prose read at length wants air between the words
     * and the frame. It is also what the rule under the byline is drawn to,
     * so the two stay in step.
     */
    kReaderSide    = 10,
    /*
     * The empty line the article carries between its byline and its text,
     * with the rule across the middle of it, is set this much above the
     * body's size: the rule wants air on both sides of it, and an empty
     * line's height is the only thing that can give it any. Nothing is
     * drawn in it — only its metrics are ever used.
     */
    kReaderRuleAir = 4,

    /*
     * A photograph in the article fills the column, the way NetNewsWire's
     * stylesheet has it — max-width 100%, height auto — scaled down to fit
     * and never up, and centred when it is narrower than the column. These
     * two bound the offscreen world it is decoded into rather than the
     * look: a column wider than this shows the picture at this size, in
     * the middle, and three worlds of 560 by 420 at 16 bits are 1.4 MB,
     * which is what an 8 MB partition can spare for pictures.
     */
    kPhotoMaxWidth  = 560,
    kPhotoMaxHeight = 420,
    kPhotoFrameGrey = 170,      /* the placeholder's edge */

    kMaxTitleLines = 3,         /* a headline wraps, but not without end   */
    kHeadlineLines = 2,         /* and in the list, always exactly two     */
    kHeadlinePad   = 5,         /* the least air above the first line and
                                   below the second; the row's own height
                                   usually leaves more — see MeasureFonts  */

    /*
     * (221,221,221): the band a date heading is drawn on, and the rule
     * between the article's title and its text. A shade under the list-view
     * background the rows sit on, which is what makes a heading read as a
     * band rather than as a gap, and there is no Appearance brush for it.
     */
    kBandGrey      = 221,

    /*
     * The toolbar. Measured off Outlook Express 5.0.6's, which is twenty-four
     * rows of the window's own grey with a black rule under it — the same
     * rule the pane headers draw along their own tops, so the two meet as one
     * line and the panes below need no arithmetic of their own.
     *
     * Two more rows than OE's, because OE's buttons carry a label beside the
     * icon and stand on a shorter box; these are icons alone and want the air
     * instead.
     */
    kToolbarHeight = 27,        /* 26 of grey, and the rule under it */
    kToolbarButton = 22,        /* a button's height; its width is its caption's */
    kToolbarPad    = 6,         /* window edge to the first button */
    kToolbarGap    = 0,         /* between buttons of one group: OE has none */
    kToolbarGroup  = 2,         /* either side of the separator between two */
    kToolInset     = 4,         /* a button's edge to its icon */
    kToolIconText  = 3,         /* its icon to its caption */
    kToolPadRight  = 5,         /* its caption to its edge */
    kToolGlyph     = 7,         /* the menu triangle after New, and */
    kToolGlyphGap  = 4,         /* the room between the caption and it */
    kToolbarCaptionSize = 9,    /* the application font at 9, as OE's is */
    kSearchWidth   = 100,       /* the text area; the frame is outside it */
    kSearchFontSize = 10,
    kSearchFrame   = 3,         /* how far outside its bounds an Edit Text
                                   draws its frame, and what has to be kept
                                   clear of it on every side */
    kSearchIconGap = 4,         /* the glass to the frame */
    kToolbarNewMenuID = 136,    /* after main.cpp's 128..135 */

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
static GazetteUISmartChosen   gOnSmartChosen;
static GazetteUICommandChosen gOnCommand;

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
/*
 * The toolbar's buttons, in the order they are laid out. Each one carries a
 * command from platinum_window.h that the shell maps onto the handler its
 * menu item already uses.
 *
 * They are drawn and tracked here rather than being Bevel Buttons, which is
 * the one place this window draws what a control might have — because no
 * control draws this. The look is Outlook Express's: a button is flat until
 * the mouse is over it, raises a one-pixel Platinum frame while it is, and
 * goes inset for as long as it is held down. A Bevel Button wears its frame
 * all the time, and a row of nine framed tiles is a different toolbar.
 */
enum {
    kTBNew = 0,
    kTBSidebar,
    kTBRefresh,
    kTBMarkAll,
    kTBHideRead,
    kTBMarkRead,
    kTBStar,
    kTBNextUnread,
    kTBBrowser,
    kToolbarButtons
};

/* One icon: the system's by reference, or ours by suite. Declared here
   because a toolbar button holds one; the lists' are further down. */
typedef struct {
    IconRef ref;                /* the system's, or NULL */
    Handle  suite;              /* ours, or NULL */
} RowIcon;

typedef struct {
    Rect        bounds;
    short       wide;       /* with the caption it is wearing */
    short       narrow;     /* icon only */
    Boolean     captioned;  /* which of those it is showing just now */
    const char *caption;    /* what it says just now */
    short       iconID;     /* and what it wears */
    RowIcon     icon;
    Boolean     enabled;
    Boolean     dirty;      /* changed since it was last drawn */
} ToolButton;

static ToolButton gToolBtn[kToolbarButtons];
static short      gToolHover   = -1;    /* the button under the mouse, or -1 */
static Boolean    gToolbarStale;        /* a caption changed: lay the row out again */
static short      gToolPressed = -1;    /* the one held down, or -1 */
static MenuRef    gNewMenu;             /* New Feed... and New Group... */
static ControlRef gSearchCtl;
static Rect       gToolbarRect;     /* the grey, and the rule along its foot */
static Rect       gToolbarSep[3];   /* between one group and the next */
static short      gToolbarSepCount;
static Rect       gFindIconRect;    /* the glass beside the search field */
static short      gSearchHeight;    /* the field's text area: the font's own height */

static Rect gSidebarPane;
static Rect gSidebarHeader;
static Rect gListPane;
static Rect gListHeader;
/*
 * The article's headline and its byline, staged here on the way into the
 * TextEdit record. They are the first two paragraphs of the article itself
 * — there is no header bar over the reader any more — so they scroll with
 * the text, a selection can take them, and the column of white runs from
 * the top of the window to the status strip without a rule across it.
 */
static char gArticleTitle[kGazetteTitleLen];
static char gArticleByline[256];

static Rect gReaderPane;        /* the article, scroll bar included */
static Rect gReaderRect;        /* the text inside it, bar excluded */
static Rect gStatusRect;
static Rect gVDivider;          /* between sidebar and the right side */
static Rect gVDivider2;         /* between headlines and the article  */

static short gSidebarWidth = 180;
static short gListWidth    = 300;   /* divider to divider: the middle column */

static int gSelectedFeed    = 0;
static int gSelectedArticle = -1;

/* The group the sidebar has selected, or -1 when the selection is a feed.
   gSelectedFeed keeps its meaning either way: it is the feed the headline
   list and the reader are showing, which a click on a group does not
   change. */
static int gSelectedGroup   = -1;

/* Which of the three standing views is open, or -1 when the sidebar's
   selection is a feed or a group. */
static int gSelectedSmart   = -1;

/* What goes in the count column beside each of them. Worked out when the
   sidebar's rows are rebuilt — see CountSmartRows — because one of the three
   costs a read of every cache and a drawing routine is no place for that. */
static int gSmartCount[kGazetteSmartCount];


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
/*
 * The Appearance control panel's Large System Font — Charcoal unless the
 * user has said otherwise. The sidebar is chrome: it is what you navigate
 * with rather than what you read, so it is set in the system's own face,
 * and so are the headings over the panes.
 */
static short gSysFont   = 0;              /* 0 is the system font */
static short gSysSize   = 12;

static short gViewFont  = kFontIDGeneva;  /* lists, headings, status */
static short gViewSize  = 12;
static short gReadFont  = kFontIDGeneva;  /* the article's body       */
static short gReadSize  = 12;
static short gLabelSize  = 9;             /* the byline under a headline */
static short gStatusSize = 10;            /* the strip along the bottom  */

static short gRowHeight    = kRowHeight;
static short gRowBaseline  = kBaseline;
static short gHeadRowHeight    = kRowHeight;  /* the headline list's own */
static short gHeadRowBaseline  = kBaseline;   /* a headline's first line  */
static short gHeadContBaseline = kBaseline;   /* and its second           */
static short gHeadingBaseline  = kBaseline;   /* a date heading, centred  */
static short gHeadAscent       = 12;          /* what a headline's line is
                                                 tall in, above its baseline */
static short gRowAscent    = 9;
static short gRowDescent   = 3;

/* The chrome's bars are as tall as the chrome's font needs, not 17 and 20
   because Geneva 9 once fitted in them. */
static short gHeaderHeight = kHeaderHeight;
static short gHeaderBase   = 12;
static short gStatusHeight = kStatusHeight;
static short gStatusBase   = 13;

/*
 * Two line heights, and nothing else: the body's, and the taller empty line
 * the article carries between its byline and its text. TextEdit lays the
 * article out — these are wanted only to place the rule in the middle of
 * that empty line.
 */
static short gReaderLine = 16;
static short gReaderGap  = 21;

/* Where the body starts in gReaderText, which is the character the rule is
   placed from. Zero when there is no article open. */
static short gReaderBodyStart;

/*
 * The photographs, as they stand in the text on screen. Each takes a run of
 * empty lines — TextEdit cannot hold a picture, so the article carries the
 * space and ReaderDraw fills it, the way the rule above the body is drawn
 * across an empty line — and remembers where that run starts, which is what
 * TEGetPoint turns into a place on screen after any scroll.
 *
 * A decoded picture is kept across recompositions: the pane is composed
 * again each time a picture lands, and a JPEG decoded on a G3 is not
 * something to do three times over. It goes when the article does.
 */
typedef struct {
    GWorldPtr world;            /* decoded at display size; NULL until then */
    short     width;            /* of the world */
    short     height;
    Boolean   undrawable;       /* QuickTime could not read it; no space */
    short     start;            /* offset of its first reserved line, or -1 */
    short     lines;            /* how many it was given */
    short     airAbove;         /* of those, how many before the picture */
    Boolean   placeholder;      /* grey while it is still coming */
    Boolean   captioned;        /* a line of alt text under it */
} ReaderPhoto;

static ReaderPhoto gPhotos[kGazetteMaxPhotos];
static int         gPhotoArticle = -1;  /* whose the worlds are */
static Boolean     gMoviesEntered;


/*
 * The article, staged here and then handed to TextEdit, which keeps its own
 * copy. Room for twice the extractor's output because a paragraph break
 * becomes two carriage returns on the way in, plus the title and the byline.
 */
/*
 * The headline list's rows are no longer its articles. A day's worth are
 * gathered under a heading — "Today", "Yesterday", "1 week ago" — and the
 * dates come off the rows themselves, because printing the same date down
 * forty rows says nothing and the exact minute belongs in the article, which
 * is where it is.
 *
 * So the list has two kinds of row and this maps between them: what a row
 * is, and which row an article is on.
 */
enum {
    kHeadlineDate    = 0,
    kHeadlineArticle = 1,       /* a headline's first line, with the icon  */
    kHeadlineCont    = 2        /* and the rest of it, under the first     */
};

typedef struct {
    short kind;
    short article;              /* for kHeadlineArticle and kHeadlineCont */
    short start;                /* this line's slice of the headline      */
    short len;
} HeadlineRow;

/* Every article, plus at most one heading each. */
static HeadlineRow gHeadRows[kGazetteMaxArticles * (kMaxTitleLines + 1)];
static int         gHeadRowCount;

static TEHandle gReaderTE;
static char     gReaderText[2 * kGazetteExtractMax + 512];

static void Layout(void);
static void ListViewIn(const Rect *pane, Rect *view);
static short ListRowHeight(ListHandle list);
static int  RowForArticle(int article);
static void SetRowCount(ListHandle list, int count);
static void SelectRow(ListHandle list, int row, Boolean reveal);
static void ReflowHeadlines(void);
static void ListView(ListHandle list, Rect *view);
static void PlaceListScrollBar(ListHandle list, const Rect *pane);
static void DrawListScrollBar(ListHandle list, ControlRef paneCtl);
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
static void DrawHeaderTitle(const Rect *r, const char *text,
                            const char *count);
static void DrawReaderRule(void);
static void DrawReaderPhotos(void);
static void ApplyRunStyle(long start, long end, short face, short size);
static void ForgetPhotos(void);
static void PaintGrey(const Rect *r, short grey);
static void ReaderHiliteColours(void);
static void GreyPen(short grey);
static void SyncSidebarRows(void);
static void MakeToolbar(void);
static void DrawToolbar(void);
static void ToolbarButtonPressed(int button);
static void AdjustToolbarState(void);
static void DrawToolButton(int i);
static void TrackToolButton(int i);
static void PopNewMenu(void);

/* ------------------------------------------------------------------ */
/* Small helpers                                                       */
/* ------------------------------------------------------------------ */

/* The Large System Font: the sidebar and the headings over the panes. */
static void UseSysFont(void)
{
    TextFont(gSysFont);
    TextSize(gSysSize);
    TextFace(normal);
}

/* OE's list font: the headline list, the article, the status line. */
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
    gLabelSize  = 9;
    gStatusSize = 10;

    /*
     * And the Large System Font, whatever the Appearance control panel has
     * been set to — Charcoal on a stock Mac OS 9. GetFNum answers 0 for a
     * name it does not know, and font 0 *is* the system font, so a failure
     * here lands on the right answer anyway.
     */
    {
        Str255 name;
        SInt16 points = 0;
        Style  face   = 0;

        gSysFont = 0;
        gSysSize = 12;
        if (GetThemeFont(kThemeSystemFont, smSystemScript, name, &points,
                         &face) == noErr) {
            if (name[0] != 0) {
                short id = 0;

                GetFNum(name, &id);
                gSysFont = id;
            }
            if (points > 0) {
                gSysSize = points;
            }
        }
    }

    /* The sidebar's rows are set in it, so its metrics are the row's. */
    UseSysFont();
    GetFontInfo(&info);
    gRowAscent  = info.ascent;
    gRowDescent = info.descent;

    /* A row is as tall as the font needs or as tall as its icon, whichever
       is more — OE's folder rows are the icon's height plus a pixel. */
    /*
     * Newsstand's own sidebar measures twenty-five pixels a row in Geneva
     * 12, which is nine more than the font needs. Rows ruled a white line
     * apart want that room: at three the lines sat almost on the text.
     */
    gRowHeight = (short)(info.ascent + info.descent + info.leading + 11);
    if (gRowHeight < kIconSize + 1) {
        gRowHeight = kIconSize + 1;
    }
    gRowBaseline = (short)(info.ascent +
                           ((gRowHeight - info.ascent - info.descent) / 2));

    /*
     * Every headline in the list is two rows tall, whether it needs the
     * second line or not. That is what buys the padding: with each article
     * the same height, the space above the first line, the gap down to the
     * second and the space below it can be chosen separately — a block of
     * one or two uniform rows could only ever have one number for all three.
     *
     * The two lines sit a line-height and a couple of pixels apart, with the
     * rule in the space under the second.
     *
     * The row is the sidebar's row height and not a number of its own, so a
     * date heading — which is one row — comes out exactly as tall as a feed
     * in the list beside it and sits on the same baseline. An article is two
     * of those rows, and the air above its first line and below its second
     * is whatever the two lines leave over, shared between them. kHeadlinePad
     * is that air's floor rather than its value: a view font tall enough to
     * fill the sidebar's row on its own pushes the row taller instead.
     */
    {
        short step  = (short)(info.ascent + info.descent + info.leading + 2);
        short lines = (short)(info.ascent + info.descent + step + 1);
        short least = (short)((lines + 2 * kHeadlinePad + 1) / 2);
        short pad;

        gHeadRowHeight = gRowHeight;
        if (gHeadRowHeight < least) {
            gHeadRowHeight = least;
        }
        if (gHeadRowHeight < kIconSize + 1) {
            gHeadRowHeight = (short)(kIconSize + 1);   /* the icon's floor */
        }

        pad = (short)((2 * gHeadRowHeight - lines) / 2);
        if (pad < kHeadlinePad) {
            pad = kHeadlinePad;
        }

        /* The first line sits a pad below the top of its row. The second is
           a line-height below the first, which lands above the top of its
           own row — hence the subtraction. */
        gHeadAscent       = info.ascent;
        gHeadRowBaseline  = (short)(pad + info.ascent);
        gHeadContBaseline = (short)(gHeadRowBaseline + step - gHeadRowHeight);

        /* A date heading is a row on its own, set in the same face at the
           same size as a feed name, so it takes the sidebar's baseline and
           lands level with one. */
        gHeadingBaseline  = gRowBaseline;
    }
    /* The headings are the system font too, so they measure in it. */
    UseSysFont();
    GetFontInfo(&info);
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

        TextSize(gStatusSize);
        GetFontInfo(&small);
        TextSize(gViewSize);

        /* Fixed: the strip is as deep as a scroll bar less a pixel whatever
           the text in it measures, so a size up only re-centres it. */
        gStatusHeight = kScrollWidth - 1;
        gStatusBase   = (short)(small.ascent +
                                (gStatusHeight - small.ascent -
                                 small.descent) / 2);
    }

    /*
     * The article's body font. TextEdit wraps and stacks the article itself,
     * headline and byline included, so the only measurement wanted here is
     * the one the rule between them is placed by: an empty line's height,
     * and the ascent that says where the line after it begins.
     */
    {
        FontInfo body;
        FontInfo gap;

        TextFont(gReadFont);
        TextSize(gReadSize);
        GetFontInfo(&body);

        TextSize((short)(gReadSize + kReaderRuleAir));
        GetFontInfo(&gap);

        gReaderLine = (short)(body.ascent + body.descent + body.leading);
        gReaderGap  = (short)(gap.ascent + gap.descent + gap.leading);

        TextFont(gViewFont);
        TextSize(gViewSize);
    }
}

/*
 * Break text into lines that each fit in a width, at word boundaries. Gives
 * back where each line starts and how long it is, and the number of lines —
 * never more than kMaxTitleLines, because a headline long enough to fill the
 * bar would leave nothing to read underneath it. Whatever is left over stays
 * on the last line for the drawing to truncate.
 *
 * The font has to be set before calling: the fit is measured, not guessed.
 */
static short WrapTitle(const char *text, short width, short maxLines,
                       short *starts, short *lens)
{
    short len;
    short at = 0;
    short n  = 0;

    if (text == NULL || width <= 0) {
        return 0;
    }
    len = (short)strlen(text);

    while (at < len && n < maxLines) {
        short fit   = -1;       /* end of the last whole word that fitted   */
        short first = -1;       /* end of the first, fits or not            */
        short k     = at;

        for (;;) {
            short was = k;

            while (k < len && text[k] == ' ') {
                k++;
            }
            while (k < len && text[k] != ' ') {
                k++;
            }
            if (k <= was) {
                break;          /* nothing left to add */
            }
            if (first < 0) {
                first = k;
            }
            if (TextWidth(text, at, (short)(k - at)) > width) {
                break;
            }
            fit = k;
            if (k >= len) {
                break;
            }
        }

        /* A single word wider than the bar goes on its own line anyway and
           is cut there, rather than dragging the rest of the headline with
           it. */
        if (fit < 0) {
            fit = (first > at) ? first : len;
        }

        starts[n] = at;
        lens[n]   = (short)(fit - at);
        n++;

        at = fit;
        while (at < len && text[at] == ' ') {
            at++;
        }
    }
    return n;
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
            (short)(pane->right - kScrollWidth + 1), pane->bottom);
}

/*
 * Where the rows go: the frame, less the two pixels at the top and the
 * bottom that the focus border is drawn in.
 *
 * The List Manager scrolls by copying pixels — ScrollRect over its own view
 * rectangle — so anything drawn inside that rectangle travels with the rows.
 * That is what dragged the blue top edge down into the middle of the list
 * and left it there, under whatever row it landed on, and keeping the
 * border's horizontal runs out of the rectangle is what stops it.
 *
 * The vertical runs stay inside, deliberately. A list scrolls up and down,
 * and a solid blue column shifted up or down is still a solid blue column —
 * only the strip newly uncovered at one end needs repainting, and the row
 * drawn into it does that itself. Insetting the sides instead would leave
 * two pixels of background at either edge that a selected row could not
 * reach, so a selection would stop short of the frame whenever the pane
 * was not focused and the blue was not there to cover for it.
 */
static void ListRowsIn(const Rect *pane, Rect *view)
{
    ListViewIn(pane, view);
    view->top    = (short)(view->top + kFocusBorder);
    view->bottom = (short)(view->bottom - kFocusBorder);
}

/* The frame a list is drawn in — its rows, plus the border around them. */
static void ListFrame(ListHandle list, Rect *frame)
{
    ListView(list, frame);
    frame->top    = (short)(frame->top - kFocusBorder);
    frame->bottom = (short)(frame->bottom + kFocusBorder);
}

/*
 * Put the scroll bar where the frame says it goes: hard against the frame's
 * right edge, and running the pane's full height.
 *
 * The List Manager hangs its bar one pixel clear of the view rectangle it
 * was given, which left a column of background between the border and the
 * bar; and that rectangle is inset at top and bottom by the border, which
 * would have the bar come up short at both ends. Neither is left to it.
 */
static void PlaceListScrollBar(ListHandle list, const Rect *pane)
{
    ControlRef bar;
    Rect       frame;
    Rect       want;

    if (list == NULL) {
        return;
    }
    bar = GetListVerticalScrollBar(list);
    if (bar == NULL) {
        return;
    }
    ListViewIn(pane, &frame);
    SetRect(&want, frame.right, (short)(frame.top - 1),
            (short)(frame.right + kScrollWidth), (short)(frame.bottom + 1));
    SetControlBounds(bar, &want);
}

/* How tall one row of this list is. The two lists do not agree. */
static short ListRowHeight(ListHandle list)
{
    return (list == gArticleList) ? gHeadRowHeight : gRowHeight;
}

/* Does this pane wear the focus border? */
/* Is the window the one in front? A bar is only greyed because of this. */
static Boolean gActive = true;

static Boolean PaneHasFocus(ControlRef control)
{
    return (Boolean)(control != NULL && control == gFocusPane);
}

/*
 * Rebuild the headline rows from the articles. They arrive in date order, so
 * a heading is needed wherever the day changes and nowhere else — one pass,
 * no sorting.
 */
/*
 * How much width a headline has to wrap inside: the row, less the icon's
 * column on the left and the inset on either side. Every line of a headline
 * gets the same width, because every line starts where the first one does —
 * the second line lines up with the words above it, not under the icon.
 */
static short HeadlineTextWidth(void)
{
    Rect  frame;
    short width;

    ListViewIn(&gListPane, &frame);
    width = (short)(frame.right - frame.left -
                    (kFocusBorder + kTextInset + kIconSize + kIconGap) -
                    (kFocusBorder + kTextInset));
    return (width > 0) ? width : 0;
}

/* Where a row's text begins, measured from the row's left edge. */
static short HeadlineTextInset(void)
{
    return (short)(kTextInset + kIconSize + kIconGap);
}

static void BuildHeadlineRows(void)
{
    long  now   = UnixNow();
    long  last  = 0;
    int   have  = 0;
    int   i;
    int   count = GazetteFeedsArticleCount();
    short width = HeadlineTextWidth();
    int   room  = (int)(sizeof gHeadRows / sizeof gHeadRows[0]);

    (void)now;
    gHeadRowCount = 0;

    /* Measured in the face a headline is drawn in, and in the heavier of its
       two weights: an unread headline is bold, and wrapping it to the plain
       width would let it run past the row when it was marked unread. */
    if (gWindow != NULL) {
        SetPortWindowPort(gWindow);
        UseViewFont();
        TextFace(bold);
    }

    for (i = 0; i < count; i++) {
        const GazetteArticle *a = GazetteFeedsArticleAt(i);
        short                 starts[kMaxTitleLines];
        short                 lens[kMaxTitleLines];
        short                 lines;
        short                 n;
        long                  day;

        if (a == NULL) {
            break;
        }
        if (gHeadRowCount + kMaxTitleLines + 1 > room) {
            break;
        }

        /* Grouped by the day the reader's clock would call it, which is the
           day the article's own byline says — not the UTC one the feed
           happened to stamp it with. */
        day = GazetteDayNumber(GazetteFeedsLocalTime(a->date));
        if (!have || day != last) {
            gHeadRows[gHeadRowCount].kind    = kHeadlineDate;
            gHeadRows[gHeadRowCount].article = (short)i;
            gHeadRows[gHeadRowCount].start   = 0;
            gHeadRows[gHeadRowCount].len     = 0;
            gHeadRowCount++;
            last = day;
            have = 1;
        }

        lines = WrapTitle(a->title, width, kHeadlineLines, starts, lens);
        if (lines < 1) {
            lines     = 1;
            starts[0] = 0;
            lens[0]   = (short)strlen(a->title);
        }
        (void)n;

        /*
         * Two rows whether or not the headline fills them. A list where a
         * one-line headline is half the height of a two-line one reads as
         * ragged; every article the same height reads as a list.
         */
        gHeadRows[gHeadRowCount].kind    = kHeadlineArticle;
        gHeadRows[gHeadRowCount].article = (short)i;
        gHeadRows[gHeadRowCount].start   = starts[0];
        gHeadRows[gHeadRowCount].len     = lens[0];
        gHeadRowCount++;

        /*
         * The second row is drawn from its start to the end of the headline
         * rather than from its slice, so a headline too long for two lines
         * ends in an ellipsis. Where it fitted on one, the start is the end
         * of the string and nothing is drawn.
         */
        gHeadRows[gHeadRowCount].kind    = kHeadlineCont;
        gHeadRows[gHeadRowCount].article = (short)i;
        if (lines >= 2) {
            gHeadRows[gHeadRowCount].start = starts[1];
            gHeadRows[gHeadRowCount].len   = lens[1];
        } else {
            gHeadRows[gHeadRowCount].start = (short)strlen(a->title);
            gHeadRows[gHeadRowCount].len   = 0;
        }
        gHeadRowCount++;
    }

    if (gWindow != NULL) {
        TextFace(normal);
    }
}

/*
 * Rebuild the rows for the width the list is now. Called when the column has
 * finished changing width rather than while it is changing: wrapping every
 * headline costs a measurement per word, and a divider drag would pay it
 * once a pixel. In between, a row's text is drawn truncated to the cell, so
 * a stale wrap is clipped rather than spilling.
 */
static void ReflowHeadlines(void)
{
    if (gWindow == NULL || gArticleList == NULL) {
        return;
    }
    BuildHeadlineRows();
    LSetDrawingMode(false, gArticleList);
    SetRowCount(gArticleList, gHeadRowCount);
    SelectRow(gArticleList, RowForArticle(gSelectedArticle), false);
    LSetDrawingMode(true, gArticleList);
}

/* The article a row shows, or -1 for a heading. */
static int ArticleAtRow(int row)
{
    if (row < 0 || row >= gHeadRowCount) {
        return -1;
    }
    /* A continuation line belongs to its headline as much as the first line
       does: clicking the second half of a title selects that article. */
    if (gHeadRows[row].kind == kHeadlineDate) {
        return -1;
    }
    return gHeadRows[row].article;
}

/* The row an article is on, or -1. */
static int RowForArticle(int article)
{
    int i;

    for (i = 0; i < gHeadRowCount; i++) {
        if (gHeadRows[i].kind == kHeadlineArticle &&
            gHeadRows[i].article == article) {
            return i;
        }
    }
    return -1;
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
    /*
     * Only the hilite. Drawing is the caller's, because this is reached
     * from places where the port's clip has no business including a scroll
     * bar — and a redraw that is clipped away still counts as done.
     */
    HiliteControl(bar, 0);
}

/*
 * Draw a list's scroll bar, from scratch, taking nothing on trust.
 *
 * Leaving this to DrawControls did not survive the window losing the front:
 * the update that follows erases the whole window and then expects the
 * hierarchy to put itself back, and the bar did not come back with it. So
 * everything it depends on is stated here rather than assumed — where it
 * sits, that it is visible, that it is not still greyed from the deactivate,
 * and that the clip in force is the window rather than whatever the last
 * thing to draw narrowed it to.
 */
static void DrawListScrollBar(ListHandle list, ControlRef paneCtl)
{
    ControlRef bar;
    Rect       pane;
    Rect       bounds;
    RgnHandle  save = NULL;

    if (gWindow == NULL || list == NULL || paneCtl == NULL) {
        return;
    }
    bar = GetListVerticalScrollBar(list);
    if (bar == NULL) {
        return;
    }

    SetPortWindowPort(gWindow);
    GetControlBounds(paneCtl, &pane);
    PlaceListScrollBar(list, &pane);

    save = NewRgn();
    if (save != NULL) {
        GetClip(save);
    }
    GetWindowPortBounds(gWindow, &bounds);
    ClipRect(&bounds);

    /*
     * Stated outright, not asked about first.
     *
     * LActivate takes the bar away when the window goes to the back, and
     * asking whether it needs putting back is how it stayed away: the
     * answer came from the same state that was wrong. Both of these are set
     * whatever they already say — visible, and awake if and only if the
     * window is in front. Nothing to scroll is not a reason to grey one;
     * Platinum's answer to that is an empty track with its arrows still on.
     */
    SetControlVisibility(bar, true, false);
    HiliteControl(bar, (SInt16)(gActive ? 0 : 255));
    Draw1Control(bar);

    if (save != NULL) {
        SetClip(save);
        DisposeRgn(save);
    }
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
    ListFrame(list, &view);
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

    ListRowsIn(bounds, &view);
    SetListViewBounds(list, &view);
    LSize((short)(view.right - view.left),
          (short)(view.bottom - view.top), list);

    cell.v = ListRowHeight(list);
    cell.h = (short)(view.right - view.left);
    if (cell.h > 0) {
        LCellSize(cell, list);
    }
    PlaceListScrollBar(list, bounds);
    LSetDrawingMode(true, list);
    WakeScrollBar(list);
}

/*
 * Where the toolbar's buttons go: left to right in four groups, an etched
 * separator between one group and the next, and the search field pinned to
 * the right hand end with the glass beside it — Outlook Express's row, with
 * our verbs on it.
 *
 * Each button is as wide as its caption needs, settled once in MakeToolbar
 * for the wider of the two captions a toggling button can wear, so that a
 * button changing its words does not move the ones beside it.
 *
 * When the window is too narrow for the whole row, the captions come off —
 * as OE's do — from the right hand end, one button at a time, until it
 * fits: the buttons nearest the left keep their words longest, and a button
 * without its caption is still its icon, which is still a button. Only when
 * every caption is gone and the row still does not fit does the field give
 * way, since a field a few pixels narrow is still a field and a button
 * pushed off the edge is gone.
 */
static void LayoutToolbar(const Rect *bounds)
{
    /* The first button of each group after the first. */
    static const short kGroupStart[3] = { kTBSidebar, kTBMarkAll, kTBMarkRead };
    short top = (short)(gToolbarRect.top +
                        ((kToolbarHeight - 1 - kToolbarButton) / 2));
    short at;
    short limit;
    short g;
    short i;

    /* The glass, its gap, the field and its frame, and a group's worth of
       air before them: everything left of that is the buttons'. */
    limit = (short)(bounds->right - kToolbarPad - kSearchFrame -
                    kSearchWidth - kSearchFrame - kSearchIconGap -
                    kIconSize - kToolbarGroup);

    for (i = 0; i < kToolbarButtons; i++) {
        gToolBtn[i].captioned = true;
    }
    for (;;) {
        /* Each separator stands where a gap would have: its two columns
           and the air either side, less the gap it replaces. */
        short need = (short)(bounds->left + kToolbarPad +
                             3 * (2 * kToolbarGroup + 2 - kToolbarGap));
        short last = -1;

        for (i = 0; i < kToolbarButtons; i++) {
            need = (short)(need + (gToolBtn[i].captioned ? gToolBtn[i].wide
                                                         : gToolBtn[i].narrow) +
                           kToolbarGap);
            if (gToolBtn[i].captioned) {
                last = i;
            }
        }
        if (need <= limit || last < 0) {
            break;
        }
        gToolBtn[last].captioned = false;
    }

    gToolbarSepCount = 0;
    at = (short)(bounds->left + kToolbarPad);
    g  = 0;
    for (i = 0; i < kToolbarButtons; i++) {
        short width = gToolBtn[i].captioned ? gToolBtn[i].wide
                                            : gToolBtn[i].narrow;

        if (g < 3 && i == kGroupStart[g]) {
            /*
             * The line itself is Platinum's etched separator — a dark column
             * and a white one, measured off Outlook Express's toolbar, and
             * the same pair the window's own grooves are built from.
             */
            short left = (short)(at - kToolbarGap + kToolbarGroup);

            SetRect(&gToolbarSep[gToolbarSepCount], left,
                    (short)(gToolbarRect.top + 1), (short)(left + 2),
                    (short)(gToolbarRect.bottom - 2));
            gToolbarSepCount++;
            at = (short)(left + 2 + kToolbarGroup);
            g++;
        }
        SetRect(&gToolBtn[i].bounds, at, top, (short)(at + width),
                (short)(top + kToolbarButton));
        at = (short)(at + width + kToolbarGap);
    }

    /*
     * The glass and the field, from the right hand end. The field's bounds
     * are its text area — the Edit Text CDEF draws its frame three pixels
     * outside them, and its focus ring outside that — so the frame is what
     * is kept clear of the window's edge and of the glass, and the bounds
     * are the font's own height, centred on the buttons.
     */
    {
        short right    = (short)(bounds->right - kToolbarPad - kSearchFrame);
        short left     = (short)(right - kSearchWidth);
        short iconLeft = (short)(left - kSearchFrame - kSearchIconGap -
                                 kIconSize);
        short fieldTop = (short)(top + (kToolbarButton - gSearchHeight) / 2);

        if (iconLeft < at) {
            iconLeft = at;
            left     = (short)(iconLeft + kIconSize + kSearchIconGap +
                               kSearchFrame);
        }
        SetRect(&gFindIconRect, iconLeft,
                (short)(top + (kToolbarButton - kIconSize) / 2),
                (short)(iconLeft + kIconSize),
                (short)(top + (kToolbarButton - kIconSize) / 2 + kIconSize));

        if (gSearchCtl != NULL) {
            Rect r;

            SetRect(&r, left, fieldTop, right,
                    (short)(fieldTop + gSearchHeight));
            if (r.right <= r.left) {
                r.right = r.left;       /* nothing left to draw in */
            }
            SetControlBounds(gSearchCtl, &r);
        }
    }
}

static void Layout(void)
{
    Rect    bounds;
    short   contentBottom;
    short   width;
    short   toolbar;
    Boolean noSidebar;

    if (gWindow == NULL) {
        return;
    }

    GetWindowPortBounds(gWindow, &bounds);

    contentBottom = (short)(bounds.bottom - gStatusHeight);
    noSidebar     = GazetteCoreHideSidebar();
    toolbar       = GazetteCoreHideToolbar() ? 0 : kToolbarHeight;

    SetRect(&gToolbarRect, bounds.left, bounds.top, bounds.right,
            (short)(bounds.top + toolbar));

    /*
     * Three columns side by side, so two widths to settle and a minimum for
     * each of the three. The sidebar gives way first and the article last:
     * the article is what the window is for, and the sidebar is the column
     * whose contents are shortest.
     *
     * With the sidebar put away it is two columns, and its own width is left
     * exactly as it was — hiding a column is not the same as resizing it, and
     * bringing it back should bring it back where it was.
     */
    {
        short room = (short)(bounds.right - bounds.left);
        short most;

        most = (short)(room - kMinList - kMinReader);
        if (gSidebarWidth > most) {
            gSidebarWidth = most;
        }
        if (gSidebarWidth < kMinSidebar) {
            gSidebarWidth = kMinSidebar;
        }

        width = noSidebar ? (short)(-kVDividerWidth + 2) : gSidebarWidth;

        most = (short)(room - width - kMinReader);
        if (gListWidth > most) {
            gListWidth = most;
        }
        if (gListWidth < kMinList) {
            gListWidth = kMinList;
        }
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
        /*
         * With the sidebar hidden, split is carried off the left edge far
         * enough that the headline list's own arithmetic — split + 5 for its
         * header, split + 6 for its pane — lands on bounds.left - 1 and
         * bounds.left, which is exactly where the sidebar's used to. The
         * column that was first is simply not there, and nothing else in
         * here has to know.
         */
        short split   = (short)(bounds.left + width);
        short split2  = (short)(split + gListWidth);
        /*
         * The row the black rule is on — the window frame's when there is
         * no toolbar, the toolbar's own when there is — and the headers
         * begin *on* it. A Window Header rules a black line of its own
         * along its first row, so started on the rule that line and the
         * rule are one; started a row lower (which was tried) the two
         * stacked into a line two pixels thick. The article's column begins
         * on the rule's row too, for the reason its own comment gives.
         */
        short top     = (short)(bounds.top + toolbar);
        short rule    = (short)(top - 1);
        short headTop = rule;
        short headBot = (short)(top + gHeaderHeight);

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
        SetRect(&gListHeader, (short)(split + 5), headTop, split2, headBot);

        /* An empty rectangle is a divider that cannot be drawn on and cannot
           be grabbed — PtInRect answers false for every point in one — which
           is what the hidden sidebar's groove has to be. */
        /* From the rule's row, so the groove cuts through the rule the way
           OE's does rather than starting beneath it. */
        if (noSidebar) {
            SetRect(&gVDivider, 0, 0, 0, 0);
        } else {
            SetRect(&gVDivider, split, rule,
                    (short)(split + kVDividerWidth), contentBottom);
        }
        SetRect(&gVDivider2, split2, rule,
                (short)(split2 + kVDividerWidth), contentBottom);

        /*
         * A pane starts one pixel *inside* the header above it, so that its
         * scroll bar's top edge lands on the header's own black rule rather
         * than drawing a second line directly under it. Now that the header
         * draws that rule, the two were stacking up two pixels thick
         * wherever a bar met a header.
         */
        /*
         * A pixel short of the groove, because the bar's right edge is meant
         * to *be* the line the header above it draws — and the header ends
         * its own black at split - 1, with the groove's white highlight on
         * split. Reaching all the way to split put the bar's black column on
         * that highlight instead, so the line down the side of the window
         * stepped a pixel sideways where the header ended and the list
         * began.
         *
         * The headline list has the same arithmetic and gets away with it:
         * its pane ends a pixel past the window's right edge, so the column
         * in question is clipped away and never drawn.
         */
        SetRect(&gSidebarPane, bounds.left, (short)(headBot - 1),
                (short)(split - 1), contentBottom);

        /*
         * Starting one pixel past the groove's black, so the rows meet the
         * border with nothing between. The groove's own trailing two pixels
         * of grey are covered by the pane, which is what takes the grey
         * edge off the inside of the view.
         *
         * It ends a pixel short of the second groove for the same reason the
         * sidebar does: that is where the bar's own black column has to
         * land, on the line the header above it draws. This used to reach
         * bounds.right + 1 and was right only because the extra column fell
         * off the edge of the window.
         */
        SetRect(&gListPane, (short)(split + 6), (short)(headBot - 1),
                (short)(split2 - 1), contentBottom);

        /*
         * The article has no header over it. Its headline and its byline are
         * the first two paragraphs of the text, so the column of white runs
         * from the top of the content region — level with where the other
         * two columns' headers begin — down to the status strip, with
         * nothing ruled across it.
         *
         * It starts on the rule's row, a pixel above the content region, for the
         * reason the two lists start a pixel inside their headers: the
         * scroll bar draws its own black edge along the pane's first row,
         * and the window frame already rules a black line there. Starting at
         * bounds.top put the bar's edge directly under that line and the top
         * of the article's scroll bar came out two pixels thick.
         */
        SetRect(&gReaderPane, (short)(split2 + 6), rule,
                (short)(bounds.right + 1), (short)(contentBottom + 1));

        /*
         * A pixel short of the pane at the bottom, and no longer at the top.
         *
         * The bottom pixel is the *status strip's* rule. The pane reaches a
         * row past it so the scroll bar's bottom edge lands there, but the
         * text must not — erasing the pane's full height painted the rule
         * white, and only a full window update put it back. Changing article
         * redraws the reader alone, so the border simply vanished until
         * something else repainted the window.
         *
         * The top pixel is the window frame's own rule, which the pane
         * reaches over so the scroll bar's edge can land on it. The text
         * must not: erasing from the pane's top would paint that line white.
         */
        SetRect(&gReaderRect, gReaderPane.left,
                (short)(gReaderPane.top + 1),
                (short)(gReaderPane.right - kScrollWidth),
                (short)(gReaderPane.bottom - 1));

        LayoutToolbar(&bounds);
    }

    /* End to end. The strip's own rule is the line between it and the panes
       above, and every one of their scroll bars ends on it. */
    SetRect(&gStatusRect, bounds.left, contentBottom,
            bounds.right, bounds.bottom);

    /*
     * Hidden rather than merely laid out off the edge. A control with an
     * inverted rectangle is still a control: DrawControls visits it, the
     * Control Manager offers it the keyboard, and its list keeps a scroll bar
     * it would place somewhere. Saying it is invisible settles all three.
     */
    if (gSidebarCtl != NULL) {
        SetControlVisibility(gSidebarCtl, !noSidebar, false);
    }
    if (gSidebarHeaderCtl != NULL) {
        SetControlVisibility(gSidebarHeaderCtl, !noSidebar, false);
    }
    {
        /* The buttons are drawn by the bar and go with it; the field is a
           control and has to be told. */
        Boolean showing = (Boolean)!GazetteCoreHideToolbar();

        if (gSearchCtl != NULL) {
            SetControlVisibility(gSearchCtl, showing, false);
        }
    }

    if (noSidebar && gFocusPane == gSidebarCtl) {
        /* The keyboard cannot be left in a pane that is not there. */
        (void)SetKeyboardFocus(gWindow, gArticleCtl, kControlFocusNextPart);
    }

    SizeListPane(gSidebarCtl, gSidebarList, &gSidebarPane);
    SizeListPane(gArticleCtl, gArticleList, &gListPane);

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

/*
 * The view is the pane less the focus border, and no less: text is clipped
 * where the frame is, not short of it. Insetting the view by the margin
 * instead cut a line of text off and then left a band of white between the
 * cut and the border, which reads as the article stopping early rather than
 * as it running on.
 *
 * The margin belongs to the destination rectangle: the first line starts a
 * margin below the top of the view and the scrollable height carries a
 * margin at each end, so the article has its air at top and bottom without
 * the view giving any up.
 */
static void ReaderRects(Rect *view)
{
    SetRect(view, (short)(gReaderRect.left + kReaderSide),
            (short)(gReaderRect.top + kFocusBorder),
            (short)(gReaderRect.right - kReaderSide),
            (short)(gReaderRect.bottom - kFocusBorder));
}

/* Where the first line of the article sits when it is scrolled to the top. */
static short ReaderTextTop(const Rect *view)
{
    return (short)(view->top + kReaderMargin);
}

/* How far down the article the view has been scrolled, in pixels. Styled
   text has no one line height to count in, so the reader's scroll bar is
   measured in pixels where the lists' are measured in rows. */
static short ReaderOffset(void)
{
    if (gReaderTE == NULL) {
        return 0;
    }
    return (short)(ReaderTextTop(&(**gReaderTE).viewRect) -
                   (**gReaderTE).destRect.top);
}

static short ReaderMaxOffset(void)
{
    long height;
    long view;

    if (gReaderTE == NULL) {
        return 0;
    }
    /* The margin at each end scrolls with the article, so it counts towards
       how far there is to scroll. */
    height = TEGetHeight((**gReaderTE).nLines, 0, gReaderTE) +
             2 * kReaderMargin;
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

    /*
     * Left active even with nothing to scroll. Making it inactive empties
     * the bar — no arrows, no thumb, just a hollow track — which is what a
     * window that is not in front looks like, and reads as the bar having
     * gone away. An active bar with no range keeps its arrows and drops the
     * thumb, which is what the Finder shows for a window whose contents
     * fit.
     */
    HiliteControl(gReaderScroll, 0);
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

    /*
     * Say what the strip coming into view is to be erased with. TEScroll
     * erases it with the port's background, and the port's background is
     * whatever the last thing to draw happened to leave behind — the
     * sidebar's grey, if that was it — which is where the wrong colour
     * behind a scrolled article came from. It also redraws any selection in
     * that strip, which wants the same background for the same reason.
     */
    ReaderHiliteColours();

    TEScroll(0, (short)(now - offset), gReaderTE);

    /*
     * TEScroll blits what was already on screen, rule included, and redraws
     * only the strip that has come into view — and it redraws it as text,
     * which the rule is not. So it goes back on afterwards: over the blitted
     * copy it is a no-op, and in the new strip it is the only thing that
     * puts it there. The pictures likewise.
     */
    DrawReaderRule();
    DrawReaderPhotos();

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

/* ------------------------------------------------------------------ */
/* Photographs                                                         */
/* ------------------------------------------------------------------ */

/* Whether QuickTime is installed. Every Mac OS 9 has it, but a Carbon
   application is asked to look before it calls, and this is the look. */
static Boolean HaveQuickTime(void)
{
    static Boolean checked, have;
    long           version;

    if (!checked) {
        checked = true;
        have    = (Gestalt(gestaltQuickTimeVersion, &version) == noErr &&
                   version != 0);
        if (have && !gMoviesEntered) {
            gMoviesEntered = (EnterMovies() == noErr);
            have           = gMoviesEntered;
        }
    }
    return have;
}

/* The width a picture may be drawn at in the column as it stands. */
static short PhotoColumnWidth(void)
{
    short width = (short)(gReaderRect.right - gReaderRect.left -
                          2 * kReaderSide);

    if (width > kPhotoMaxWidth) {
        width = kPhotoMaxWidth;
    }
    if (width < 32) {
        width = 32;
    }
    return width;
}

static void DisposePhotoWorld(ReaderPhoto *p)
{
    if (p->world != NULL) {
        DisposeGWorld(p->world);
        p->world = NULL;
    }
    p->width = p->height = 0;
}

/* The worlds go with the article they were decoded for. */
static void ForgetPhotos(void)
{
    int i;

    for (i = 0; i < kGazetteMaxPhotos; i++) {
        DisposePhotoWorld(&gPhotos[i]);
        gPhotos[i].undrawable = false;
        gPhotos[i].start      = -1;
        gPhotos[i].lines      = 0;
    }
    gPhotoArticle = -1;
}

/*
 * Decode one picture into an offscreen world at the size it will be drawn.
 * The Graphics Importer reads the bytes out of a Handle, scales as it
 * decodes, and leaves pixels the pane can CopyBits at every redraw — which
 * is the whole reason for decoding once rather than drawing the JPEG each
 * time: a 2000-pixel photograph is a second's work on a G3, and CopyBits of
 * a 300-pixel one is nothing.
 */
static Boolean DecodePhoto(ReaderPhoto *p, const char *bytes, long len)
{
    Handle                   h    = NULL;
    GraphicsImportComponent  gi   = 0;
    GWorldPtr                world = NULL;
    Rect                     natural, dest;
    OSType                   kind;
    long                     w, hgt, maxW;
    Boolean                  ok = false;

    if (!HaveQuickTime() || bytes == NULL || len < 4) {
        return false;
    }
    kind = ((unsigned char)bytes[0] == 0xFF) ? kQTFileTypeJPEG
         : ((unsigned char)bytes[0] == 0x89) ? kQTFileTypePNG
         :                                     kQTFileTypeGIF;

    if (PtrToHand(bytes, &h, len) != noErr || h == NULL) {
        return false;
    }
    if (OpenADefaultComponent(GraphicsImporterComponentType, kind, &gi) != noErr ||
        gi == 0) {
        DisposeHandle(h);
        return false;
    }

    if (GraphicsImportSetDataHandle(gi, h) == noErr &&
        GraphicsImportGetNaturalBounds(gi, &natural) == noErr) {
        w    = natural.right - natural.left;
        hgt  = natural.bottom - natural.top;
        maxW = PhotoColumnWidth();

        if (w > 0 && hgt > 0) {
            /* Down to fit, never up: a small picture stays small. */
            if (w > maxW) {
                hgt = hgt * maxW / w;
                w   = maxW;
            }
            if (hgt > kPhotoMaxHeight) {
                w   = w * kPhotoMaxHeight / hgt;
                hgt = kPhotoMaxHeight;
            }
            if (w < 1)   w = 1;
            if (hgt < 1) hgt = 1;

            SetRect(&dest, 0, 0, (short)w, (short)hgt);
            if (NewGWorld(&world, 16, &dest, NULL, NULL, 0) == noErr &&
                world != NULL) {
                CGrafPtr  savePort;
                GDHandle  saveDevice;

                GetGWorld(&savePort, &saveDevice);
                SetGWorld(world, NULL);
                LockPixels(GetGWorldPixMap(world));
                EraseRect(&dest);
                (void)GraphicsImportSetGWorld(gi, world, NULL);
                (void)GraphicsImportSetBoundsRect(gi, &dest);
                (void)GraphicsImportSetQuality(gi, codecHighQuality);
                ok = (GraphicsImportDraw(gi) == noErr);
                UnlockPixels(GetGWorldPixMap(world));
                SetGWorld(savePort, saveDevice);

                if (ok) {
                    p->world  = world;
                    p->width  = (short)w;
                    p->height = (short)hgt;
                } else {
                    DisposeGWorld(world);
                }
            }
        }
    }

    CloseComponent(gi);
    DisposeHandle(h);
    return ok;
}

/*
 * The size a picture takes on screen, whether it is here or not. While it is
 * still coming the placeholder stands at the column's width and a photograph's
 * proportion, so the page does not jump more than it must when the real one
 * lands. Returns false when the slot takes no space at all: not coming, or
 * QuickTime could not read it.
 */
static Boolean PhotoSize(int slot, short *width, short *height)
{
    ReaderPhoto *p = &gPhotos[slot];
    const char  *bytes;
    long         len;

    if (p->undrawable) {
        return false;
    }
    switch (GazettePhotosState(slot)) {
        case kGazettePhotoLoaded:
            if (p->world == NULL) {
                bytes = GazettePhotosData(slot, &len);
                if (!DecodePhoto(p, bytes, len)) {
                    p->undrawable = true;
                    return false;
                }
            }
            *width         = p->width;
            *height        = p->height;
            p->placeholder = false;
            return true;

        case kGazettePhotoPending:
            *width  = PhotoColumnWidth();
            *height = (short)(*width * 2 / 3);
            if (*height > kPhotoMaxHeight) {
                *height = kPhotoMaxHeight;
            }
            p->placeholder = true;
            return true;

        default:
            return false;
    }
}

/*
 * Reserve a picture's lines in the text: a run of empty paragraphs tall
 * enough for it, its air, and a caption when it has one. airAbove is the
 * empty lines before the picture itself — one for a picture in the flow,
 * which wants the same gap a paragraph gets; none for the lead, which sits
 * directly under the rule.
 */
static size_t AppendPhotoRun(size_t used, int slot, short height,
                             short airAbove)
{
    ReaderPhoto *p     = &gPhotos[slot];
    short        line  = gReaderLine > 0 ? gReaderLine : 1;
    short        lines = (short)(airAbove + (height + line - 1) / line + 1);
    short        i;

    p->captioned = (GazettePhotosCaption(slot)[0] != '\0');
    if (p->captioned) {
        lines++;
    }
    p->start    = (short)used;
    p->lines    = lines;
    p->airAbove = airAbove;

    for (i = 0; i < lines; i++) {
        used = AppendChar(used, '\r');
    }
    return used;
}

/* Which slot the k-th marker in the text stands for. */
static int PhotoSlotForMarker(int k)
{
    return (k >= 0 && k < GazettePhotosCount()) ? k : -1;
}

/*
 * The faces the body wears, as runs over the composed text. The extractor
 * left marks in the text — see kGazetteMarkHeading and its neighbours —
 * and AppendBody takes each out as it copies, noting where the face it
 * switches begins and ends; SetReaderText applies the runs once the text
 * is in the record. A heading is a bold paragraph, a quotation an italic
 * one, and the inline marks add to whatever the paragraph is. (Not
 * StyleRun: TextEdit has one of those already.)
 */
typedef struct {
    short start;
    short end;
    short face;
} ReaderFace;

enum { kMaxStyleRuns = 400 };

static ReaderFace gStyleRuns[kMaxStyleRuns];
static int      gStyleRunCount;

/* Close the run in progress, if it wore anything, and start the next. */
static void NoteFace(size_t at, short *runStart, short *runFace, short face)
{
    if (*runFace != normal && (size_t)*runStart < at &&
        gStyleRunCount < kMaxStyleRuns) {
        gStyleRuns[gStyleRunCount].start = *runStart;
        gStyleRuns[gStyleRunCount].end   = (short)at;
        gStyleRuns[gStyleRunCount].face  = *runFace;
        gStyleRunCount++;
    }
    *runStart = (short)at;
    *runFace  = face;
}

/*
 * The body arrives with its paragraphs marked by newlines. TextEdit breaks
 * on carriage returns, and a paragraph wants a blank line after it, so each
 * run of newlines becomes exactly two — however many the extractor left.
 *
 * A paragraph that is only the photo marker is where a picture stood in the
 * article. When the picture takes space it becomes its run of empty lines,
 * with the blank line that would have separated the paragraphs folded into
 * the run's air; when it does not — not coming, photos off, unreadable —
 * the paragraph is dropped and the text closes over it.
 *
 * The style marks come out as the text is copied; see ReaderFace. A list
 * item gets a bullet in front of it, which is the one thing about a list
 * TextEdit can show.
 */
static size_t AppendBody(size_t used, const char *body)
{
    const char *p        = body;
    int         marker   = 0;
    Boolean     first    = true;
    Boolean     afterRun = false;
    short       inline_  = 0;           /* bold, italic, underline, as set */
    short       runStart = (short)used;
    short       runFace  = normal;

    gStyleRunCount = 0;

    while (*p != '\0') {
        const char *end = p;
        const char *q;
        short       paragraph = normal;
        Boolean     bullet    = false;
        Boolean     any       = false;

        while (*end != '\0' && *end != '\n') {
            end++;
        }

        if (end - p == 1 && *p == (char)kGazettePhotoMarker) {
            int   slot = PhotoSlotForMarker(marker++);
            short w, h;

            if (slot >= 0 && PhotoSize(slot, &w, &h)) {
                if (!first && !afterRun) {
                    used = AppendChar(used, '\r');   /* ends the paragraph */
                }
                NoteFace(used, &runStart, &runFace, normal);
                used     = AppendPhotoRun(used, slot, h, first ? 0 : 1);
                first    = false;
                afterRun = true;
            }
            p = end;
            while (*p == '\n') {
                p++;
            }
            continue;
        }

        /* A face does not run past its paragraph: the extractor closes
           what it opens within one, and a mark that got away — a link
           wrapped round a picture and its caption — would otherwise
           underline everything after it. */
        inline_ = 0;

        /* What kind of paragraph, from the marks at its head; then whether
           there are any words in it at all. */
        for (q = p; q < end && GazetteIsMark(*q); q++) {
            if (*q == (char)kGazetteMarkHeading) {
                paragraph |= bold;
            } else if (*q == (char)kGazetteMarkQuote) {
                paragraph |= italic;
            } else if (*q == (char)kGazetteMarkListItem) {
                bullet = true;
            }
        }
        for (q = p; q < end; q++) {
            if (!GazetteIsMark(*q)) {
                any = true;
                break;
            }
        }

        if (any) {
            if (!first && !afterRun) {
                used = AppendChar(used, '\r');
                used = AppendChar(used, '\r');
            }
            first    = false;
            afterRun = false;
            NoteFace(used, &runStart, &runFace, (short)(paragraph | inline_));
            if (bullet) {
                used = AppendChar(used, '\245');   /* MacRoman bullet */
                used = AppendChar(used, ' ');
            }
        }

        for (q = p; q < end; q++) {
            char c = *q;

            if (GazetteIsMark(c)) {
                short was = inline_;

                switch ((unsigned char)c) {
                    case kGazetteMarkBoldOn:    inline_ |= bold;       break;
                    case kGazetteMarkBoldOff:   inline_ &= ~bold;      break;
                    case kGazetteMarkItalicOn:  inline_ |= italic;     break;
                    case kGazetteMarkItalicOff: inline_ &= ~italic;    break;
                    case kGazetteMarkLinkOn:    inline_ |= underline;  break;
                    case kGazetteMarkLinkOff:   inline_ &= ~underline; break;
                    default: break;
                }
                if (any && inline_ != was) {
                    NoteFace(used, &runStart, &runFace,
                             (short)(paragraph | inline_));
                }
                continue;
            }
            used = AppendChar(used, c);
        }

        p = end;
        while (*p == '\n') {
            p++;
        }
    }
    NoteFace(used, &runStart, &runFace, normal);
    return used;
}

/* The faces AppendBody noted, over text that is now in the record. */
static void ApplyStyleRuns(void)
{
    int i;

    for (i = 0; i < gStyleRunCount; i++) {
        ApplyRunStyle(gStyleRuns[i].start, gStyleRuns[i].end,
                      gStyleRuns[i].face, gReadSize);
    }
}

/*
 * Paint the pictures over their reserved lines. TEGetPoint answers with the
 * bottom of the line a character is on, so the run's first line begins one
 * line above that, and the picture begins airAbove lines further down. A
 * picture still coming is a grey field with a darker edge; a picture here is
 * its world, copied at its own size, or shrunk to the column if the column
 * has since been dragged narrower than it.
 */
static void DrawReaderPhotos(void)
{
    Rect  view;
    int   i;

    if (gReaderTE == NULL || GazettePhotosArticle() < 0 ||
        GazettePhotosArticle() != gSelectedArticle) {
        return;
    }
    view = (**gReaderTE).viewRect;

    for (i = 0; i < kGazetteMaxPhotos; i++) {
        ReaderPhoto *p = &gPhotos[i];
        Point        where;
        Rect         box;
        short        top, w, h, room;

        if (p->start < 0 || p->lines <= 0) {
            continue;
        }
        where = TEGetPoint(p->start, gReaderTE);
        top   = (short)(where.v - gReaderLine + p->airAbove * gReaderLine);

        if (p->placeholder || p->world == NULL) {
            if (!p->placeholder) {
                continue;
            }
            w = PhotoColumnWidth();
            h = (short)(w * 2 / 3);
            if (h > kPhotoMaxHeight) {
                h = kPhotoMaxHeight;
            }
        } else {
            w = p->width;
            h = p->height;
        }

        /* The column may have narrowed since the picture was decoded. */
        room = (short)(view.right - view.left);
        if (w > room && w > 0) {
            h = (short)((long)h * room / w);
            w = room;
        }

        /* In the middle of the column when it does not fill it: a
           portrait, or a small picture, or a column wider than the world
           it was decoded into. */
        {
            short left = (short)(view.left + (room - w) / 2);

            SetRect(&box, left, top, (short)(left + w), (short)(top + h));
        }
        if (box.bottom <= view.top || box.top >= view.bottom) {
            continue;                       /* scrolled out of the pane */
        }

        if (p->placeholder) {
            PaintGrey(&box, kBandGrey);
            GreyPen(kPhotoFrameGrey);
            FrameRect(&box);
        } else {
            Rect     src;
            RGBColor black = { 0, 0, 0 };
            RGBColor white = { 0xFFFF, 0xFFFF, 0xFFFF };

            SetRect(&src, 0, 0, p->width, p->height);
            /* Black on white, or CopyBits colourises the picture with
               whatever the port's colours were last set to. */
            RGBForeColor(&black);
            RGBBackColor(&white);
            LockPixels(GetGWorldPixMap(p->world));
            CopyBits(GetPortBitMapForCopyBits(p->world),
                     GetPortBitMapForCopyBits(GetWindowPort(gWindow)),
                     &src, &box, srcCopy, NULL);
            UnlockPixels(GetGWorldPixMap(p->world));
        }

        /* The caption, on the line under it, in the byline's face. */
        if (p->captioned) {
            const char *alt = GazettePhotosCaption(i);
            RgnHandle   clip = NewRgn();
            Rect        capBox;
            FontInfo    fi;
            RGBColor    black = { 0, 0, 0 };

            SetRect(&capBox, box.left, (short)(top + h),
                    box.right, (short)(top + h + gReaderLine));
            if (clip != NULL) {
                GetClip(clip);
            }
            ClipRect(&capBox);
            TextFont(gViewFont);
            TextSize(gLabelSize);
            TextFace(normal);
            GetFontInfo(&fi);
            RGBForeColor(&black);
            MoveTo(box.left, (short)(top + h + fi.ascent + 2));
            DrawText(alt, 0, (short)strlen(alt));
            if (clip != NULL) {
                SetClip(clip);
                DisposeRgn(clip);
            }
        }
    }
    ForeColor(blackColor);
}

/* One of the three weights the pane has always had, applied to a range that
   is already in the record. Setting a style on an insertion point and
   trusting the next TEInsert to pick it up is documented but delicate;
   styling text that is already there cannot be misread. */
static void ApplyRunFont(long start, long end, short font, short face,
                         short size)
{
    TextStyle style;

    if (gReaderTE == NULL || end <= start) {
        return;
    }
    style.tsFont = font;
    style.tsFace = face;
    style.tsSize = size;
    style.tsColor.red   = 0;
    style.tsColor.green = 0;
    style.tsColor.blue  = 0;

    TESetSelect(start, end, gReaderTE);
    TESetStyle(doFont | doFace | doSize, &style, false, gReaderTE);
}

/* The body's own face, which is what most of an article is set in. */
static void ApplyRunStyle(long start, long end, short face, short size)
{
    ApplyRunFont(start, end, gReadFont, face, size);
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
    size_t  used     = 0;
    size_t  titleEnd = 0;
    size_t  byline   = 0;
    size_t  gap      = 0;
    char    when[64];

    if (gWindow == NULL || gReaderTE == NULL) {
        return;
    }

    GetPort(&savePort);
    SetPortWindowPort(gWindow);

    TEDeactivate(gReaderTE);
    gReaderText[0]    = '\0';
    gArticleTitle[0]  = '\0';
    gArticleByline[0] = '\0';
    gReaderBodyStart  = 0;

    gStyleRunCount = 0;             /* the runs are the body's, composed below */

    /* The pictures' places are composed afresh below; the decoded ones are
       kept only while they are still this article's. */
    if (gPhotoArticle != GazettePhotosArticle()) {
        ForgetPhotos();
        gPhotoArticle = GazettePhotosArticle();
    }
    {
        int i;

        for (i = 0; i < kGazetteMaxPhotos; i++) {
            gPhotos[i].start = -1;
            gPhotos[i].lines = 0;
        }
    }

    a = GazetteFeedsArticleAt(gSelectedArticle);
    if (a == NULL) {
        static const char kNothing[] = "Select a headline to read it.";

        used = AppendText(0, kNothing, sizeof kNothing - 1);
        TESetText(gReaderText, (long)used, gReaderTE);
        ApplyRunStyle(0, (long)used, normal, gReadSize);
    } else {
        const char *from = a->source;
        const char *body = a->body;

        /* The headline and the byline are the article's own first two
           paragraphs rather than a bar above it: they are set in the text
           and they scroll with it. */
        (void)gz_copy_n(gArticleTitle, sizeof gArticleTitle,
                        a->title, strlen(a->title));

        /* Written out in full, and on the reader's clock: a feed stamps its
           articles in UTC and the hour under a headline should be the hour
           the reader's own Macintosh would have shown. */
        GazetteFormatLongDate(GazetteFeedsLocalTime(a->date), when,
                              sizeof when);

        /* In a group view the articles come from several feeds, so which one
           this is from is worth saying. The feed's own name stands in when
           the article does not name a publisher. */
        if (from[0] == '\0' && GazetteFeedsCurrentGroup() >= 0) {
            from = GazetteCoreFeedTitle(a->feed);
        }
        if (when[0] != '\0' && from[0] != '\0') {
            snprintf(gArticleByline, sizeof gArticleByline, "%s by %s",
                     when, from);
        } else if (from[0] != '\0') {
            snprintf(gArticleByline, sizeof gArticleByline, "by %s", from);
        } else {
            snprintf(gArticleByline, sizeof gArticleByline, "%s", when);
        }

        /*
         * The article's own page when it has been fetched and extracted, and
         * the feed's summary otherwise. The store answers which article the
         * held text belongs to, so switching articles cannot show the last
         * one's body under this one's headline.
         *
         * And while the page is still coming, neither: the pane says it is
         * reading and shows nothing else. The summary is what an article
         * falls back to when its page is behind a paywall or cannot be had —
         * it is not a first draft, and laying it out only to replace it a
         * second later is the flicker this avoids.
         */
        if (GazetteFeedsFullTextArticle() == gSelectedArticle) {
            const char *full = GazetteFeedsFullText();

            if (full[0] != '\0') {
                body = full;
            }
        } else if (GazetteFeedsFullTextComing(gSelectedArticle)) {
            body = NULL;
        }

        /*
         * Headline, byline, an empty line, then the article. The empty line
         * is what the rule between the headline and the text is drawn
         * across: TextEdit has no way to put a line between two paragraphs,
         * so the article carries the space and DrawReaderRule fills it —
         * which is also what makes the rule travel with the text when the
         * pane is scrolled.
         */
        used     = AppendText(0, gArticleTitle, strlen(gArticleTitle));
        used     = AppendChar(used, '\r');
        titleEnd = used;
        used     = AppendText(used, gArticleByline, strlen(gArticleByline));
        used     = AppendChar(used, '\r');
        byline   = used;
        used     = AppendChar(used, '\r');
        gReaderBodyStart = (short)used;
        gap              = used;

        if (body == NULL) {
            static const char kWaiting[] = "Reading the full article\311";

            used = AppendText(used, kWaiting, sizeof kWaiting - 1);
        } else if (body[0] != '\0') {
            used = AppendBody(used, body);
        } else {
            static const char kNone[] = "(This feed carries no summary for "
                                        "this article.)";

            used = AppendText(used, kNone, sizeof kNone - 1);
        }

        TESetText(gReaderText, (long)used, gReaderTE);

        /*
         * The body's face over the whole of it first, and then the two
         * paragraphs that are not the body. Each paragraph's own carriage
         * return is styled with it, because a line's height is the tallest
         * style on it and the return is the last thing on the line.
         */
        ApplyRunStyle(0, (long)used, normal, gReadSize);
        ApplyRunFont(0, (long)titleEnd, gSysFont, normal, gSysSize);
        ApplyRunFont((long)titleEnd, (long)byline, gViewFont, normal,
                     gLabelSize);
        ApplyRunFont((long)byline, (long)gap, gReadFont, normal,
                     (short)(gReadSize + kReaderRuleAir));
        /* And what the body's markup asked for, over the body. */
        ApplyStyleRuns();
    }

    TESetSelect(0, 0, gReaderTE);

    /* Back to the top, and the wrap and the bar back in step with the new
       length. */
    ReaderRects(&view);
    (**gReaderTE).viewRect = view;
    (**gReaderTE).destRect = view;
    (**gReaderTE).destRect.top = ReaderTextTop(&view);
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
    (**gReaderTE).destRect.top = ReaderTextTop(&view);
    TECalText(gReaderTE);

    max = ReaderMaxOffset();
    if (was > max) {
        was = max;
    }
    if (was < 0) {
        was = 0;
    }
    (**gReaderTE).destRect.top = (short)(ReaderTextTop(&view) - was);

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
/*
 * A header's title, and after it — in the grey the sidebar's unread counts
 * are set in, so the two read as the same kind of number — its count, when
 * it has one. The count is drawn whole and the title gives way to it: a
 * long feed name is truncated before the number is.
 */
static void DrawHeaderTitle(const Rect *r, const char *text,
                            const char *count)
{
    Rect  inner = *r;
    short room;
    short countWidth = 0;

    if (r->right <= r->left) {
        return;                 /* a column that is not being drawn */
    }

    UseSysFont();
    SetThemeTextColor(kThemeTextColorWindowHeaderActive, 8, true);

    inner.left  = (short)(inner.left + kTextInset + 2);
    inner.right = (short)(inner.right - kTextInset);
    room        = (short)(inner.right - inner.left);

    if (count != NULL && count[0] != '\0') {
        countWidth = (short)(TextWidth(count, 0, (short)strlen(count)) +
                             kCountGap / 2);
        if (countWidth > room) {
            countWidth = 0;         /* no room for the number at all */
        }
    }

    MoveTo(inner.left, (short)(r->top + gHeaderBase));
    DrawTruncated(text, (short)(room - countWidth));

    if (countWidth > 0) {
        RGBColor grey;
        Point    pen;

        GetPen(&pen);
        grey.red = grey.green = grey.blue = 90 * 257;
        RGBForeColor(&grey);
        MoveTo((short)(pen.h + kCountGap / 2), pen.v);
        DrawText(count, 0, (short)strlen(count));
    }

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
/*
 * A row's icon is one of two things, and they are drawn by two different
 * calls. The four above are the system's, reached through Icon Services by
 * constant and carried as IconRefs. The three standing views and the mark on
 * a starred headline are ours, drawn by tools/generate_ui_icons.py into
 * Resources/Gazette_ui_icons.r, and read out of the application's own
 * resource fork as icon suites. So is every one of the toolbar's, though
 * those go to the Bevel Button CDEF by resource ID rather than through here.
 *
 * Suites rather than a registration with Icon Services: GetIconSuite takes a
 * resource ID, which is exactly what we have, and registering four icons
 * under a creator code so that they could be asked for the same way as the
 * system's would be ceremony with nothing at the end of it.
 */
static RowIcon gFolderIcon;
static RowIcon gOpenFolderIcon;
static RowIcon gFeedIcon;
static RowIcon gDocIcon;
static RowIcon gSmartIcon[kGazetteSmartCount];
static RowIcon gStarIcon;       /* the mark on a starred headline */
static RowIcon gFindIcon;       /* the glass beside the toolbar's search field */

/* Resource IDs, and they have to agree with tools/generate_ui_icons.py. */
enum {
    kIconToday        = 128,
    kIconAllUnread    = 129,
    kIconStarred      = 130,
    kIconStarredSmall = 131,

    /* The toolbar's. The three pairs are the buttons that toggle. For now
       both halves of a pair carry the same picture — the second state's has
       not been drawn — and the generator emits it twice so that the two IDs
       stay distinct for the day it is. */
    kIconSidebar       = 132,
    kIconRefresh       = 133,
    kIconMarkAllRead   = 134,
    kIconMarkAllUnread = 135,
    kIconHideRead      = 136,
    kIconShowRead      = 137,
    kIconMarkRead      = 138,
    kIconMarkUnread    = 139,
    kIconNextUnread    = 140,
    kIconBrowser       = 141,

    /* Drawn and approved, and waiting on the toolbar that will wear them:
       a New button at the head of the row, and a Find beside the search
       field. */
    kIconNew           = 142,
    kIconFind          = 143
};

static void SystemIcon(RowIcon *out, OSType which)
{
    out->suite = NULL;
    out->ref   = NULL;
    (void)GetIconRef(kOnSystemDisk, kSystemIconsCreator, which, &out->ref);
}

static void OwnIcon(RowIcon *out, short resID)
{
    out->ref   = NULL;
    out->suite = NULL;
    (void)GetIconSuite(&out->suite, resID, svAllAvailableData);
}

static void LoadRowIcons(void)
{
    SystemIcon(&gFolderIcon, kGenericFolderIcon);
    SystemIcon(&gOpenFolderIcon, kOpenFolderIcon);
    SystemIcon(&gFeedIcon, kInternetLocationNewsIcon);
    SystemIcon(&gDocIcon, kGenericDocumentIcon);

    OwnIcon(&gSmartIcon[kGazetteSmartToday], kIconToday);
    OwnIcon(&gSmartIcon[kGazetteSmartUnread], kIconAllUnread);
    OwnIcon(&gSmartIcon[kGazetteSmartStarred], kIconStarred);
    OwnIcon(&gStarIcon, kIconStarredSmall);
    OwnIcon(&gFindIcon, kIconFind);
}

static void ReleaseRowIcon(RowIcon *icon)
{
    if (icon->ref != NULL) {
        (void)ReleaseIconRef(icon->ref);
        icon->ref = NULL;
    }
    if (icon->suite != NULL) {
        (void)DisposeIconSuite(icon->suite, true);
        icon->suite = NULL;
    }
}

static void ReleaseRowIcons(void)
{
    int i;

    ReleaseRowIcon(&gFolderIcon);
    ReleaseRowIcon(&gOpenFolderIcon);
    ReleaseRowIcon(&gFeedIcon);
    ReleaseRowIcon(&gDocIcon);
    for (i = 0; i < kGazetteSmartCount; i++) {
        ReleaseRowIcon(&gSmartIcon[i]);
    }
    ReleaseRowIcon(&gStarIcon);
    ReleaseRowIcon(&gFindIcon);
    for (i = 0; i < kToolbarButtons; i++) {
        ReleaseRowIcon(&gToolBtn[i].icon);
        gToolBtn[i].iconID = 0;
    }
}

/*
 * Plot one at a given top, dimmed when the feed it stands for is off. The two
 * callers below settle where that top is; everything from here down is the
 * same either way.
 */
static void PlotRowIcon(short left, short top, const RowIcon *icon,
                        Boolean enabled)
{
    Rect box;

    if (icon == NULL || (icon->ref == NULL && icon->suite == NULL)) {
        return;
    }
    SetRect(&box, left, top, (short)(left + kIconSize),
            (short)(top + kIconSize));

    /*
     * Icon Services does not clip the way QuickDraw does. A row only half on
     * screen at the bottom of a scrolled list had its text clipped properly
     * and its icon drawn whole, which left an icon sitting under the last
     * row with nothing beside it. If the icon does not fit inside what is
     * currently clipped, it is not drawn at all.
     */
    {
        RgnHandle clip = NewRgn();

        if (clip != NULL) {
            Rect limit;

            GetClip(clip);
            GetRegionBounds(clip, &limit);
            DisposeRgn(clip);

            if (box.top < limit.top || box.bottom > limit.bottom) {
                return;
            }
        }
    }

    if (icon->suite != NULL) {
        (void)PlotIconSuite(&box, kAlignAbsoluteCenter,
                            enabled ? kTransformNone : kTransformDisabled,
                            icon->suite);
    } else if (icon->ref != NULL) {
        (void)PlotIconRef(&box, kAlignAbsoluteCenter,
                          enabled ? kTransformNone : kTransformDisabled,
                          kIconServicesNormalUsageFlag, icon->ref);
    }
}

/*
 * Centred in a row's full height — which is what the sidebar wants, where a
 * row is one line of text and the icon belongs beside it.
 *
 * The row's height and not the rectangle's: the List Manager truncates the
 * last cell's rectangle at the foot of the view, so centring in that walked
 * the icon upwards a pixel at a time as a divider was dragged.
 */
static void DrawRowIcon(const Rect *cell, short left, const RowIcon *icon,
                        Boolean enabled, short rowHeight)
{
    PlotRowIcon(left, (short)(cell->top + ((rowHeight - kIconSize) / 2)),
                icon, enabled);
}

/*
 * Centred on one line of text rather than on the row, for the headline list —
 * where a row is half of a two-line block and the icon belongs beside the
 * *first* line, not floating between the two.
 *
 * The line's middle is half an ascent above its baseline: the ascent is the
 * part of the line the letters are actually in, and the descent below the
 * baseline is nearly empty. Centring on the whole of ascent plus descent puts
 * the icon a pixel or two low, and centring on the row — which is what this
 * used to do — puts it several pixels high, because the row carries all its
 * padding above the first line and none of it below.
 */
static void DrawLineIcon(short left, short baseline, short ascent,
                         const RowIcon *icon, Boolean enabled)
{
    PlotRowIcon(left, (short)(baseline - ascent / 2 - kIconSize / 2),
                icon, enabled);
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
 * The unread count, pinned to the right hand end of the row and set in the
 * same face, weight and size as the name beside it, a shade of grey down.
 *
 * It was a filled badge for a while. QuickDraw's rounded rectangle cannot
 * make a convincing capsule at this size — the corner ovals have four or
 * five pixels to turn in and the ends come out square — and a number
 * centred by hand inside a shape that small never quite sits right. Text
 * aligned to a common right edge does the same job: the eye still finds the
 * column, and nothing has to be drawn to fake a shape the toolbox does not
 * have.
 */
static void CountText(int count, char *out, size_t cap)
{
    snprintf(out, cap, "(%d)", (count > 0) ? count : 0);
}

/*
 * The count is set in the system font whatever the row beside it is in, so
 * a column of them stays a column: the categories are in that face already
 * and the feed names are not, and a count that changed face with its row
 * would read as two different columns.
 */
static short CountWidth(int count)
{
    char text[16];

    CountText(count, text, sizeof text);
    UseSysFont();
    return (short)(TextWidth(text, 0, (short)strlen(text)) + kCountGap);
}

static void DrawCount(short right, short baseline, int count, Boolean enabled)
{
    char     text[16];
    short    len;
    RGBColor grey;
    RGBColor save;

    CountText(count, text, sizeof text);
    len = (short)strlen(text);

    UseSysFont();
    GetForeColor(&save);
    if (enabled) {
        grey.red = grey.green = grey.blue = 90 * 257;
        RGBForeColor(&grey);
    } else {
        SetThemeTextColor(kThemeTextColorDialogInactive, 8, true);
    }

    MoveTo((short)(right - TextWidth(text, 0, len)), baseline);
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
 * A flat fill in one grey, for the band and the rule that are not any theme
 * brush. Painted rather than erased: erasing would leave the port holding a
 * background colour nothing else wants, which is the trap EraseWith exists
 * to make explicit.
 */
static void PaintGrey(const Rect *r, short grey)
{
    RGBColor c;
    RGBColor save;

    GetForeColor(&save);
    c.red = c.green = c.blue = (unsigned short)(grey * 257);
    RGBForeColor(&c);
    PaintRect(r);
    RGBForeColor(&save);
}

/*
 * One row of the sidebar, laid out the way Outlook Express lays its folder
 * list out: the disclosure triangle's column, then a small icon, then the
 * name. A feed inside a group is indented by one step; a group's own row is
 * the only one that draws a triangle.
 */
/*
 * Where a row's content goes: clear of the two pixels at either side that
 * the focus border is drawn in. The row's background still fills the whole
 * rectangle — a selection reaches the frame, and the border is painted back
 * over it — but nothing is written under where the border will be.
 */
static void RowRect(const Rect *cell, Rect *out)
{
    *out = *cell;
    out->left  = (short)(out->left + kFocusBorder);
    out->right = (short)(out->right - kFocusBorder);
}

/* The focus border's colour, painted into a rectangle. */
static void FillFocusColour(const Rect *r)
{
    RGBColor blue;
    RGBColor save;

    GetForeColor(&save);
    blue.red   = 91 * 257;
    blue.green = 91 * 257;
    blue.blue  = 197 * 257;
    RGBForeColor(&blue);
    PaintRect(r);
    RGBForeColor(&save);
}

/*
 * Put back the two pixels of border a full-width row has painted over. A row
 * is redrawn on its own often enough — a selection moving, a click tracking,
 * a strip uncovered by a scroll — that waiting for the pane to be redrawn
 * would leave the border notched for as long as the mouse was down.
 */
static void RestoreRowFocusEdges(const Rect *full, ControlRef owner)
{
    Rect edge;

    if (!PaneHasFocus(owner)) {
        return;
    }
    SetRect(&edge, full->left, full->top,
            (short)(full->left + kFocusBorder), full->bottom);
    FillFocusColour(&edge);
    SetRect(&edge, (short)(full->right - kFocusBorder), full->top,
            full->right, full->bottom);
    FillFocusColour(&edge);
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
    RestoreRowFocusEdges(full, gSidebarCtl);

    /*
     * A white line along the bottom of every row. Both Newsstand and
     * Outlook Express rule one — measured at each list's own row pitch, a
     * single white pixel as the last row of each cell — and it is what stops
     * a column of names reading as one block of text.
     */
    ForeColor(whiteColor);
    MoveTo(cell->left, (short)(cell->bottom - 1));
    LineTo((short)(cell->right - 1), (short)(cell->bottom - 1));
    ForeColor(blackColor);

    if (!GazetteCoreSidebarRowAt(row, &r)) {
        return;
    }

    UseSysFont();
    baseline = (short)(cell->top + gRowBaseline);
    iconLeft = (short)(cell->left + kTextInset + kTriangleColumn);

    if (r.kind == kGazetteRowFeed && GazetteCoreFeedGroup(r.index) >= 0) {
        iconLeft = (short)(iconLeft + kGroupIndent);
    }
    textLeft = (short)(iconLeft + kIconSize + kIconGap);

    if (r.kind == kGazetteRowSmart) {
        unread = (r.index >= 0 && r.index < kGazetteSmartCount)
                     ? gSmartCount[r.index] : 0;
    } else if (r.kind == kGazetteRowFeed) {
        enabled = GazetteCoreFeedEnabled(r.index);
        unread  = enabled ? FeedUnread(r.index) : 0;
    } else {
        unread = GroupUnread(r.index);
    }

    /*
     * The count first, in the system font it is always set in, and then the
     * row's own face — a category in the system font, a feed name in the
     * one the articles are read in. Nothing in the sidebar is bold: the
     * highlight says which feed is open and the count says how much is in
     * it, and weight had nothing left to say.
     */
    badge = CountWidth(unread);

    /* A standing view is set in the system font the groups are: it is one of
       the window's own lines rather than one of the reader's. */
    if (r.kind == kGazetteRowFeed) {
        UseViewFont();
    } else {
        UseSysFont();
    }

    /* The badge is pinned right, so the name is truncated into what is left
       rather than the two overlapping. */
    width = BuildRowLabel(r.kind == kGazetteRowSmart
                              ? GazetteCoreSmartName(r.index)
                              : (r.kind == kGazetteRowGroup
                                     ? GazetteCoreGroupName(r.index)
                                     : GazetteCoreFeedTitle(r.index)),
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

    if (r.kind == kGazetteRowSmart) {
        /* No disclosure triangle: there is nothing under it to disclose. */
        DrawRowIcon(cell, iconLeft, &gSmartIcon[r.index], true, gRowHeight);
    } else if (r.kind == kGazetteRowGroup) {
        Boolean open = (Boolean)!GazetteCoreGroupCollapsed(r.index);

        DrawDisclosure(cell, (short)(cell->left + kTextInset), open);
        DrawRowIcon(cell, iconLeft, open ? &gOpenFolderIcon : &gFolderIcon,
                    true, gRowHeight);
    } else {
        DrawRowIcon(cell, iconLeft, &gFeedIcon, enabled, gRowHeight);
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

    DrawCount((short)(cell->right - kTextInset), baseline, unread, enabled);

    TextFace(normal);
    ForeColor(blackColor);
}

/*
 * Draw a cell with the port clipped to the list's own view.
 *
 * The List Manager hands the definition function a rectangle it worked out
 * from the view as it was, and that rectangle can outlive the view: drag a
 * divider and the list is resized under it, so a row that was the last one
 * on screen is now past the bottom — and it was still being drawn there,
 * over the divider and into the pane below. Clipping here rather than
 * trusting whoever called in means a row can never leave its list, however
 * it was reached.
 */
static void ClipToList(ListHandle list, RgnHandle *saved)
{
    Rect view;

    *saved = NewRgn();
    if (*saved != NULL) {
        GetClip(*saved);
    }
    ListView(list, &view);
    if (view.right > view.left) {
        Rect now;
        RgnHandle rgn = NewRgn();

        if (rgn != NULL) {
            GetClip(rgn);
            GetRegionBounds(rgn, &now);
            DisposeRgn(rgn);

            /* Narrow to whichever is smaller, so an update region that is
               already tighter than the view is not widened. */
            if (now.left   > view.left)   view.left   = now.left;
            if (now.top    > view.top)    view.top    = now.top;
            if (now.right  < view.right)  view.right  = now.right;
            if (now.bottom < view.bottom) view.bottom = now.bottom;
        }
        ClipRect(&view);
    }
}

static void UnclipList(RgnHandle saved)
{
    if (saved != NULL) {
        SetClip(saved);
        DisposeRgn(saved);
    }
}

/*
 * Is this row wholly in view? Only the last one can fail: the List Manager
 * scrolls by whole cells, so the top of the list is always aligned and it is
 * the bottom that runs out of room.
 */
static Boolean RowFullyVisible(ListHandle list, const Rect *cellRect)
{
    Rect view;

    ListView(list, &view);

    /*
     * Measured from the row's top plus a whole row, because the rectangle
     * handed in has already been cut off at the foot of the view — so its
     * own bottom always looks as though it fits, however little of the row
     * is really there.
     */
    return (Boolean)(cellRect->top + ListRowHeight(list) <= view.bottom);
}

static pascal void SidebarLDEF(short message, Boolean isSelected, Rect *cellRect,
                               Cell cell, short dataOffset, short dataLen,
                               ListHandle list)
{
    RgnHandle saved = NULL;

    (void)dataOffset;
    (void)dataLen;

    if (message != lDrawMsg && message != lHiliteMsg) {
        return;
    }
    ClipToList(list, &saved);
    if (RowFullyVisible(list, cellRect)) {
        DrawSidebarCell(cellRect, cell.v, isSelected);
    } else {
        EraseWith(cellRect, kThemeBrushListViewBackground);
    }
    UnclipList(saved);
}

/* A day's heading: the relative day, in the system font, grey. */
static void DrawDateHeading(const Rect *cell, int article)
{
    const GazetteArticle *a = GazetteFeedsArticleAt(article);
    char                  label[32];
    RGBColor              grey;
    RGBColor              save;

    if (a == NULL) {
        return;
    }
    GazetteRelativeDay(GazetteFeedsLocalTime(a->date), UnixNow(),
                       label, sizeof label);
    if (label[0] == '\0') {
        return;
    }

    UseSysFont();
    GetForeColor(&save);
    grey.red = grey.green = grey.blue = 110 * 257;
    RGBForeColor(&grey);

    MoveTo((short)(cell->left + kTextInset + 2),
           (short)(cell->top + gHeadingBaseline));
    DrawTruncated(label, (short)(cell->right - cell->left - kTextInset * 2));

    RGBForeColor(&save);
}

static void DrawArticleCell(const Rect *full, short row, Boolean selected)
{
    const GazetteArticle *a;
    Rect                  cellRect;
    const Rect           *cell = &cellRect;
    short                 baseline;
    short                 textLeft;
    short                 textRight;
    int                   article;

    RowRect(full, &cellRect);

    /*
     * Erased end to end, not just across the inset: otherwise a row that has
     * just lost its selection keeps two stripes of highlight where the focus
     * border sits.
     *
     * The rows are on the list-view background the sidebar's are on, so the
     * two lists read as one pair rather than as a grey column beside a white
     * one. A date heading is banded a shade darker again, which is the only
     * thing in either list that is not a theme brush.
     */
    if (row < 0 || row >= gHeadRowCount) {
        EraseWith(full, kThemeBrushListViewBackground);
        return;
    }
    if (gHeadRows[row].kind == kHeadlineDate) {
        PaintGrey(full, kBandGrey);
    } else {
        EraseWith(full, kThemeBrushListViewBackground);
    }

    baseline = (short)(cell->top +
                       ((gHeadRows[row].kind == kHeadlineCont)
                            ? gHeadContBaseline : gHeadRowBaseline));

    if (gHeadRows[row].kind == kHeadlineDate) {
        /* The band is painted end to end like every other row, so the two
           columns of focus border go back over it. */
        RestoreRowFocusEdges(full, gArticleCtl);
        DrawDateHeading(cell, gHeadRows[row].article);
        TextFace(normal);
        ForeColor(blackColor);
        return;
    }

    article = gHeadRows[row].article;
    a       = GazetteFeedsArticleAt(article);
    if (a == NULL) {
        return;
    }

    /*
     * A headline that wrapped occupies more than one row, so whether this
     * row is part of the selection is a question about the article rather
     * than about the row — the List Manager only ever selects one cell, and
     * a selection that covered the first line of a headline and not the
     * second would read as a drawing fault.
     */
    /*
     * A white rule under the last line of each headline — under the whole
     * of it, not between the lines of one, which is what tells a headline
     * that wrapped apart from two that did not. The same line the sidebar
     * rules its feeds apart with, drawn the same way and at the same pitch,
     * now that the two lists share a background.
     *
     * Before the highlight, so a selected headline covers its own rule
     * rather than having a line drawn across it.
     */
    if (row + 1 >= gHeadRowCount || gHeadRows[row + 1].kind != kHeadlineCont) {
        ForeColor(whiteColor);
        MoveTo(cell->left, (short)(cell->bottom - 1));
        LineTo((short)(cell->right - 1), (short)(cell->bottom - 1));
        ForeColor(blackColor);
    }

    (void)selected;
    if (article == gSelectedArticle) {
        /* Filled end to end — the whole width of the row, frame to frame,
           not the part the text sits in. Down first, so the headline is
           drawn on top of it; and the border's two pixels go straight back
           over either end. */
        FillHighlight(full);
    }
    RestoreRowFocusEdges(full, gArticleCtl);

    /* The document icon goes on the first line only, and the rest of the
       headline lines up with the words above it rather than sliding under
       the icon. The date is gone from the row: it is in the heading above
       and, to the minute, in the article itself. */
    if (gHeadRows[row].kind == kHeadlineArticle) {
        DrawLineIcon((short)(cell->left + kTextInset), baseline, gHeadAscent,
                     &gDocIcon, true);
    }
    textLeft  = (short)(cell->left + HeadlineTextInset());
    textRight = (short)(cell->right - kTextInset);

    /*
     * The star goes at the right hand end of a headline's first line, and
     * takes its room out of that line rather than out of both: a headline
     * that wraps has its whole second line either way. It is the same star
     * the sidebar's Starred view carries, drawn smaller so that it marks the
     * headline rather than competing with it.
     */
    if (a->starred && gHeadRows[row].kind == kHeadlineArticle) {
        textRight = (short)(textRight - kIconSize);
        DrawLineIcon(textRight, baseline, gHeadAscent, &gStarIcon, true);
        textRight = (short)(textRight - kIconGap);
    }

    /*
     * The same face the feed names are set in, and always black: bold while
     * unread, plain once it has been opened. Weight says it without taking
     * the headline's colour away, which is what greying it did.
     */
    UseViewFont();
    TextFace(a->read ? normal : bold);
    ForeColor(blackColor);

    MoveTo(textLeft, baseline);
    if (gHeadRows[row].kind == kHeadlineCont) {
        /* The rest of the headline, truncated — this is the line that ends
           in an ellipsis when two were not enough for it. */
        DrawTruncated(a->title + gHeadRows[row].start,
                      (short)(textRight - textLeft));
    } else {
        /*
         * Just this line's words. Truncated as well as sliced, so that a
         * wrap left over from the width the column used to be is clipped at
         * the cell rather than running out of it.
         */
        char  line[kGazetteTitleLen];
        short len = gHeadRows[row].len;

        if (len < 0) {
            len = 0;
        }
        if (len > (short)(sizeof line - 1)) {
            len = (short)(sizeof line - 1);
        }
        memcpy(line, a->title + gHeadRows[row].start, (size_t)len);
        line[len] = '\0';
        DrawTruncated(line, (short)(textRight - textLeft));
    }

    TextFace(normal);
    ForeColor(blackColor);
}

static pascal void ArticleLDEF(short message, Boolean isSelected, Rect *cellRect,
                               Cell cell, short dataOffset, short dataLen,
                               ListHandle list)
{
    RgnHandle saved = NULL;

    (void)dataOffset;
    (void)dataLen;

    if (message != lDrawMsg && message != lHiliteMsg) {
        return;
    }
    ClipToList(list, &saved);

    /*
     * A row with only part of its height left draws nothing but its
     * background. Half a row of text is untidy; half a row whose icon is
     * drawn whole — because Icon Services does not clip — is worse, and
     * refusing the icon on its own made it blink in and out as a divider was
     * dragged past the boundary. Leaving the strip empty is steady, and the
     * List Manager scrolls by whole rows so nothing is ever out of reach.
     */
    if (RowFullyVisible(list, cellRect)) {
        DrawArticleCell(cellRect, cell.v, isSelected);
    } else {
        EraseWith(cellRect, kThemeBrushListViewBackground);
    }
    UnclipList(saved);
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
    ListFrame(list, &view);
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

    DrawFocusBorder(&view, PaneHasFocus(control));

    if (clip != NULL) {
        SetClip(clip);
        DisposeRgn(clip);
    }

    /*
     * The bar, now that the clip is back. It could not be done above: the
     * clip was narrowed to the rows precisely so that nothing in here paints
     * over the bar, and a control asked to redraw itself inside that clip
     * draws nothing at all — while still counting as redrawn. So the bar was
     * un-greyed without ever being repainted, WakeScrollBar saw it was no
     * longer greyed and did nothing on every later pass, and the pixels sat
     * there untouched until something erased them. Then the bar was simply
     * gone until the window was rebuilt, which is why it came back on
     * relaunch.
     *
     * LUpdate greys a bar with nothing to scroll on its way past, so waking
     * it goes after that and the redraw after both.
     */
    WakeScrollBar(list);
    DrawListScrollBar(list, control);
}

static pascal void PaneDraw(ControlRef control, SInt16 part)
{
    (void)part;

    if (control == gSidebarCtl) {
        DrawListPane(control, gSidebarList, GazetteCoreSidebarRowCount(),
                     kThemeBrushListViewBackground);
    } else if (control == gArticleCtl) {
        DrawListPane(control, gArticleList, gHeadRowCount,
                     kThemeBrushListViewBackground);
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
           (short)(view.top + gHeadRowBaseline + 1));
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
 * The rule between the article's headline and its text.
 *
 * Drawn rather than set: TextEdit has nothing that puts a line between two
 * paragraphs, so the article carries an empty line there and this goes
 * across the middle of it. Placed off the body's first character, which is
 * the one thing TextEdit will answer about where a line has ended up — so
 * it follows the text when the pane scrolls instead of standing still while
 * the article moves under it.
 *
 * Not end to end: it spans the text's own measure and stops where a line of
 * the article would, which leaves the margin on either side of it clear.
 */
static void DrawReaderRule(void)
{
    Rect  view;
    Rect  line;
    Point where;
    short top;

    if (gReaderTE == NULL || gReaderBodyStart <= 0) {
        return;
    }
    view = (**gReaderTE).viewRect;
    if (view.right <= view.left) {
        return;
    }

    /*
     * TEGetPoint answers with the *bottom* of the line the character is on —
     * measured against a screenshot rather than taken from Inside Macintosh,
     * which does not say which end it means and where the two differ by a
     * descent. So: back off the body's own line for the top of it, back off
     * the empty line above that, and the rule goes across that line's
     * middle. Which is what makes the space above the rule and the space
     * below it the same.
     */
    where = TEGetPoint(gReaderBodyStart, gReaderTE);
    top   = (short)(where.v - gReaderLine - gReaderGap + gReaderGap / 2);

    if (top < view.top || top >= view.bottom) {
        return;                 /* scrolled out of the pane */
    }
    SetRect(&line, view.left, top, view.right, (short)(top + 1));
    PaintGrey(&line, kBandGrey);
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

        ReaderHiliteColours();
        TEUpdate(&view, gReaderTE);
    }
    DrawReaderRule();
    DrawReaderPhotos();

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

/* ------------------------------------------------------------------ */
/* The toolbar                                                         */
/*                                                                     */
/* Nine buttons drawn and tracked here, and an Edit Text for the search */
/* field, which the Control Manager draws and takes the keystrokes for. */
/* The buttons are ours because their look is Outlook Express's, and   */
/* nothing in the Control Manager draws a button that is flat until the */
/* mouse reaches it: see the notes on gToolBtn.                        */
/* ------------------------------------------------------------------ */

/*
 * What each button wears and says. The second icon and caption are for the
 * buttons that toggle — each shows what it would do next, the way its menu
 * item does — and the width is settled for the wider of the two captions so
 * that the row holds still when one of them changes its words.
 */
typedef struct {
    short       icon;
    short       otherIcon;      /* or 0 */
    const char *caption;
    const char *otherCaption;   /* or NULL */
} ToolSpec;

static const ToolSpec kToolSpec[kToolbarButtons] = {
    { kIconNew,         0,                  "New",                NULL },
    { kIconSidebar,     0,                  "Hide Sidebar",       "Show Sidebar" },
    { kIconRefresh,     0,                  "Refresh",            NULL },
    { kIconMarkAllRead, kIconMarkAllUnread, "Mark All as Read",   "Mark All as Unread" },
    { kIconHideRead,    kIconShowRead,      "Hide Read Articles", "Show Read Articles" },
    { kIconMarkRead,    kIconMarkUnread,    "Mark as Read",       "Mark as Unread" },
    { kIconStarred,     0,                  "Star Article",       "Unstar Article" },
    { kIconNextUnread,  0,                  "Next Unread",        NULL },
    { kIconBrowser,     0,                  "Open in Browser",    NULL }
};

/* The captions' font, in the current port. */
static void UseCaptionFont(void)
{
    TextFont(gViewFont);
    TextSize(kToolbarCaptionSize);
    TextFace(normal);
}

static short CaptionWidth(const char *text)
{
    if (text == NULL) {
        return 0;
    }
    return TextWidth(text, 0, (short)strlen(text));
}

/*
 * How wide a button is with a given caption on it: the same air either side
 * of the icon and the words on every button, whatever the words are. A
 * toggling button therefore changes width with its words, and the row is
 * laid out again when one does — the buttons beside it move by the
 * difference, which is the price of the padding being the same on all of
 * them. The port's font has to be the captions' when this is called.
 */
static short ToolWidth(int i, const char *caption)
{
    short width = (short)(kToolInset + kIconSize + kToolIconText +
                          CaptionWidth(caption) + kToolPadRight);

    if (i == kTBNew) {
        width = (short)(width + kToolGlyphGap + kToolGlyph);
    }
    return width;
}

/*
 * What a button wears. The suite is loaded once per picture and kept until
 * it changes, which for most of them is never.
 */
static void SetToolIcon(ToolButton *b, short resID)
{
    if (b->iconID == resID) {
        return;
    }
    ReleaseRowIcon(&b->icon);
    OwnIcon(&b->icon, resID);
    b->iconID = resID;
    b->dirty  = true;
}

static void SetToolState(int i, Boolean enabled, const char *caption,
                         short resID)
{
    ToolButton *b = &gToolBtn[i];

    if (b->enabled != enabled) {
        b->enabled = enabled;
        b->dirty   = true;
    }
    if (b->caption != caption &&
        (b->caption == NULL || strcmp(b->caption, caption) != 0)) {
        b->caption = caption;
        b->dirty   = true;
        UseCaptionFont();
        b->wide       = ToolWidth(i, caption);
        gToolbarStale = true;
    }
    SetToolIcon(b, resID);
}

/* The row laid out again after a caption changed its width. */
static void RelayoutToolbar(void)
{
    Rect bounds;

    if (gWindow == NULL) {
        return;
    }
    GetWindowPortBounds(gWindow, &bounds);
    LayoutToolbar(&bounds);
    gToolbarStale = false;
}

static void MakeToolbar(void)
{
    Rect r;
    int  i;

    /* Widths first, in the captions' font, with the window's port current:
       MeasureFonts has already settled which font that is. */
    UseCaptionFont();
    for (i = 0; i < kToolbarButtons; i++) {
        const ToolSpec *spec = &kToolSpec[i];

        gToolBtn[i].wide   = ToolWidth(i, spec->caption);
        gToolBtn[i].narrow = (short)(kToolInset + kIconSize + kToolInset);
        if (i == kTBNew) {
            gToolBtn[i].narrow = (short)(gToolBtn[i].narrow + kToolGlyphGap +
                                         kToolGlyph);
        }
        gToolBtn[i].captioned = true;
        gToolBtn[i].caption   = spec->caption;
        gToolBtn[i].enabled = true;
        gToolBtn[i].dirty   = true;
        SetToolIcon(&gToolBtn[i], spec->icon);
    }
    gToolHover   = -1;
    gToolPressed = -1;

    /*
     * New is a menu, not a command: a feed or a group, and the button pops
     * the choice under itself. The menu lives in the hierarchical list —
     * where PopUpMenuSelect can find it and the menu bar cannot — under an
     * ID after the shell's own.
     */
    gNewMenu = NewMenu(kToolbarNewMenuID, "\pNew");
    if (gNewMenu != NULL) {
        AppendMenu(gNewMenu, "\pNew Feed\311;New Group\311");
        InsertMenu(gNewMenu, hierMenu);
    }

    /*
     * The field is set a size smaller than the lists, at 10, the way OE's
     * Find is a small thing at the end of the row rather than another
     * button's worth of it. The application font, as everywhere else —
     * Geneva on a stock system. Its bounds are exactly as tall as that font
     * is: an Edit Text's bounds are its text area, and the frame it draws
     * around them is its own business.
     */
    {
        FontInfo info;

        TextFont(gViewFont);
        TextSize(kSearchFontSize);
        TextFace(normal);
        GetFontInfo(&info);
        gSearchHeight = (short)(info.ascent + info.descent);
    }

    SetRect(&r, 0, 0, kSearchWidth, gSearchHeight);
    gSearchCtl = MakeControl(&r, kControlEditTextProc, 0);
    if (gSearchCtl != NULL) {
        ControlFontStyleRec style;

        style.flags = kControlUseFontMask | kControlUseSizeMask;
        style.font  = gViewFont;
        style.size  = kSearchFontSize;
        (void)SetControlFontStyle(gSearchCtl, &style);
    }
}

static void DisposeToolbar(void)
{
    if (gNewMenu != NULL) {
        DeleteMenu(kToolbarNewMenuID);
        DisposeMenu(gNewMenu);
        gNewMenu = NULL;
    }
    gToolHover   = -1;
    gToolPressed = -1;
}

/*
 * One button, in whichever state it is in.
 *
 * At rest it is its icon and its caption on the bar's own grey and nothing
 * else. Under the mouse it gains Platinum's raised frame — white along the
 * top and left, dark along the bottom and right — and nothing else changes.
 * Held down, the frame turns over and the grey behind it darkens, which is
 * what every pressed Platinum button does. Disabled, the icon is plotted
 * dimmed and the caption in the grayish text mode, which is how the Control
 * Manager greys a title, and the mouse is not answered.
 */
static void DrawToolButton(int i)
{
    ToolButton *b = &gToolBtn[i];
    Rect        r = b->bounds;
    Boolean     pressed = (Boolean)(gToolPressed == i);
    Boolean     raised  = (Boolean)(gToolHover == i && b->enabled && !pressed);
    FontInfo    info;
    short       baseline;
    short       x;

    if (gWindow == NULL || GazetteCoreHideToolbar()) {
        return;
    }
    SetPortWindowPort(gWindow);

    if (pressed) {
        PaintGrey(&r, 0xBB);
    } else {
        EraseWith(&r, kThemeBrushDialogBackgroundActive);
    }

    if (raised || pressed) {
        /* The frame: light on one diagonal, dark on the other, and which is
           which is the whole difference between raised and pressed. */
        if (pressed) {
            GreyPen(0x55);
        } else {
            ForeColor(whiteColor);
        }
        MoveTo(r.left, (short)(r.bottom - 1));
        LineTo(r.left, r.top);
        LineTo((short)(r.right - 1), r.top);
        if (pressed) {
            ForeColor(whiteColor);
        } else {
            GreyPen(0x55);
        }
        MoveTo((short)(r.right - 1), (short)(r.top + 1));
        LineTo((short)(r.right - 1), (short)(r.bottom - 1));
        LineTo((short)(r.left + 1), (short)(r.bottom - 1));
        ForeColor(blackColor);
    }

    x = (short)(r.left + kToolInset);
    PlotRowIcon(x, (short)(r.top + (kToolbarButton - kIconSize) / 2),
                &b->icon, b->enabled);
    x = (short)(x + kIconSize + kToolIconText);

    UseCaptionFont();
    GetFontInfo(&info);
    baseline = (short)(r.top +
                       (kToolbarButton - (info.ascent + info.descent)) / 2 +
                       info.ascent);
    ForeColor(blackColor);
    TextMode(b->enabled ? srcOr : grayishTextOr);
    MoveTo(x, baseline);
    if (b->captioned && b->caption != NULL) {
        DrawText(b->caption, 0, (short)strlen(b->caption));
    }

    if (i == kTBNew) {
        /* The menu's triangle: seven wide, four tall, point down, centred
           on the button's middle row, in the caption's own grey when the
           button is off. */
        short left = (short)(r.right - kToolPadRight - kToolGlyph);
        short top  = (short)(r.top + kToolbarButton / 2 - 2);
        short k;

        if (!b->enabled) {
            GreyPen(0x77);
        }
        for (k = 0; k < 4; k++) {
            MoveTo((short)(left + k), (short)(top + k));
            LineTo((short)(left + kToolGlyph - 1 - k), (short)(top + k));
        }
        ForeColor(blackColor);
    }
    TextMode(srcOr);
    b->dirty = false;
}

/*
 * The bar itself: the window's own grey, a black rule along its foot, the
 * etched separators between the groups, the buttons, and the glass beside
 * the search field. The field is a control and DrawControls paints it.
 *
 * The rule is the toolbar's own and not the headers'. Both draw on the same
 * row — the headers' top edges land there — but the article has no header,
 * and without this its column would begin on nothing.
 */
static void DrawToolbar(void)
{
    Rect rule;
    int  i;

    if (gWindow == NULL || gToolbarRect.bottom <= gToolbarRect.top) {
        return;
    }
    SetPortWindowPort(gWindow);

    EraseWith(&gToolbarRect, kThemeBrushDialogBackgroundActive);

    for (i = 0; i < gToolbarSepCount; i++) {
        Rect sep = gToolbarSep[i];

        /* Measured off Outlook Express: (91,91,91) then white, a pixel clear
           of the grey at either end. It is Platinum's etched separator, the
           same pair the window's own grooves are built from. */
        GreyPen(91);
        MoveTo(sep.left, sep.top);
        LineTo(sep.left, (short)(sep.bottom - 1));
        ForeColor(whiteColor);
        MoveTo((short)(sep.left + 1), sep.top);
        LineTo((short)(sep.left + 1), (short)(sep.bottom - 1));
        ForeColor(blackColor);
    }

    for (i = 0; i < kToolbarButtons; i++) {
        DrawToolButton(i);
    }
    PlotRowIcon(gFindIconRect.left, gFindIconRect.top, &gFindIcon, true);

    SetRect(&rule, gToolbarRect.left, (short)(gToolbarRect.bottom - 1),
            gToolbarRect.right, gToolbarRect.bottom);
    ForeColor(blackColor);
    PaintRect(&rule);

    /*
     * The grooves cut through the rule, as OE's do: on the rule's row a
     * groove is its white column, four of grey and its black, and the rule
     * stops either side of it. The grooves themselves are drawn before the
     * bar — the headers that follow cover their inner columns — so the bar
     * puts their first row back over the rule it has just painted.
     */
    {
        const Rect *groove[2];
        int         g;

        groove[0] = &gVDivider;
        groove[1] = &gVDivider2;
        for (g = 0; g < 2; g++) {
            const Rect *r = groove[g];
            Rect        cap;

            if (r->right <= r->left || r->top != rule.top) {
                continue;
            }
            SetRect(&cap, (short)(r->left + 1), rule.top,
                    (short)(r->left + 5), rule.bottom);
            EraseWith(&cap, kThemeBrushDialogBackgroundActive);
            ForeColor(whiteColor);
            MoveTo(r->left, rule.top);
            LineTo(r->left, rule.top);
            ForeColor(blackColor);
        }
    }
}

/*
 * Which buttons can do anything, and which picture and words the ones that
 * toggle are wearing. The same decisions AdjustMenus makes for the menu bar,
 * written out again here rather than shared with it: the menu bar is the
 * shell's and this is the window's, and what they have in common is the
 * question, not the code that answers it.
 *
 * Only the state. Whoever calls this draws whatever it changed — the full
 * update draws the whole bar anyway, and anything else draws the buttons it
 * dirtied and nothing more.
 */
static void AdjustToolbarState(void)
{
    const GazetteArticle *open;
    int                   count;
    Boolean               unread;
    Boolean               hidden;

    if (gWindow == NULL) {
        return;
    }
    SetPortWindowPort(gWindow);     /* a changed caption is measured here */

    open   = GazetteFeedsArticleAt(gSelectedArticle);
    count  = GazetteFeedsArticleCount();
    unread = (Boolean)(GazetteFeedsUnreadCount() > 0);
    hidden = GazetteCoreHideReadArticles();

    /* Two that are always available: there is always something to make,
       and always a sidebar to show or hide. */
    SetToolState(kTBNew, true, kToolSpec[kTBNew].caption,
                 kToolSpec[kTBNew].icon);
    SetToolState(kTBSidebar, true,
                 GazetteCoreHideSidebar() ? kToolSpec[kTBSidebar].otherCaption
                                          : kToolSpec[kTBSidebar].caption,
                 kToolSpec[kTBSidebar].icon);
    SetToolState(kTBRefresh, (Boolean)(GazetteCoreFeedCount() > 0),
                 kToolSpec[kTBRefresh].caption, kToolSpec[kTBRefresh].icon);

    SetToolState(kTBMarkAll, (Boolean)(count > 0),
                 unread ? kToolSpec[kTBMarkAll].caption
                        : kToolSpec[kTBMarkAll].otherCaption,
                 unread ? kToolSpec[kTBMarkAll].icon
                        : kToolSpec[kTBMarkAll].otherIcon);
    SetToolState(kTBHideRead, true,
                 hidden ? kToolSpec[kTBHideRead].otherCaption
                        : kToolSpec[kTBHideRead].caption,
                 hidden ? kToolSpec[kTBHideRead].otherIcon
                        : kToolSpec[kTBHideRead].icon);

    SetToolState(kTBMarkRead, (Boolean)(open != NULL),
                 (open != NULL && open->read)
                     ? kToolSpec[kTBMarkRead].otherCaption
                     : kToolSpec[kTBMarkRead].caption,
                 (open != NULL && open->read)
                     ? kToolSpec[kTBMarkRead].otherIcon
                     : kToolSpec[kTBMarkRead].icon);
    SetToolState(kTBStar, (Boolean)(open != NULL),
                 (open != NULL && open->starred)
                     ? kToolSpec[kTBStar].otherCaption
                     : kToolSpec[kTBStar].caption,
                 kToolSpec[kTBStar].icon);
    SetToolState(kTBNextUnread, unread, kToolSpec[kTBNextUnread].caption,
                 kToolSpec[kTBNextUnread].icon);
    SetToolState(kTBBrowser,
                 (Boolean)(open != NULL && open->link[0] != '\0'),
                 kToolSpec[kTBBrowser].caption, kToolSpec[kTBBrowser].icon);

    /* A button that has just gone dead is not under the mouse any more,
       whatever the mouse thinks. */
    if (gToolHover >= 0 && !gToolBtn[gToolHover].enabled) {
        gToolHover = -1;
    }

    if (gSearchCtl != NULL) {
        HiliteControl(gSearchCtl,
                      (short)(GazetteFeedsTotalCount() > 0 ? 0 : 255));
    }
}

/*
 * The state above, and then only the buttons it changed redrawn to show it
 * — unless a caption changed its width, when the row is laid out and drawn
 * again whole. A full window update draws the whole bar after settling the
 * state, so it does not come through here; anything that changes the state
 * on its own — opening an article, mostly — does.
 */
void GazetteUIAdjustToolbar(void)
{
    int i;

    AdjustToolbarState();

    if (gWindow == NULL || GazetteCoreHideToolbar()) {
        return;
    }
    SetPortWindowPort(gWindow);
    if (gToolbarStale) {
        RelayoutToolbar();
        DrawToolbar();
    } else {
        for (i = 0; i < kToolbarButtons; i++) {
            if (gToolBtn[i].dirty) {
                DrawToolButton(i);
            }
        }
    }
    if (gSearchCtl != NULL) {
        Draw1Control(gSearchCtl);
    }
}

/*
 * The mouse has moved, or may have: called from the event loop whenever it
 * has a moment. The button under the mouse raises its frame and the one it
 * has just left drops it, and that is all hovering is — no delay, and no
 * tooltip, which OE has and which nothing here needs with the captions on
 * the buttons.
 *
 * Not while a button is being held down, when the press is tracking the
 * mouse itself; not while another window is in front, when the mouse is
 * theirs; and not while the window is in the background.
 */
void GazetteUIIdle(void)
{
    Point p;
    short over = -1;
    short i;

    if (gWindow == NULL || !gActive || GazetteCoreHideToolbar() ||
        gToolPressed >= 0 || FrontWindow() != gWindow) {
        return;
    }
    SetPortWindowPort(gWindow);
    GetMouse(&p);

    if (PtInRect(p, &gToolbarRect)) {
        for (i = 0; i < kToolbarButtons; i++) {
            if (gToolBtn[i].enabled && PtInRect(p, &gToolBtn[i].bounds)) {
                over = i;
                break;
            }
        }
    }
    if (over != gToolHover) {
        short was = gToolHover;

        gToolHover = over;
        if (was >= 0) {
            DrawToolButton(was);
        }
        if (over >= 0) {
            DrawToolButton(over);
        }
    }
}

/*
 * A press on a button: inset while the mouse stays on it, flat again the
 * moment it leaves, and the command fires only if it is let go on the
 * button — which is what every button on this machine has done since 1984,
 * and what the Control Manager would have done for us had it been drawing.
 */
static void TrackToolButton(int i)
{
    ToolButton *b = &gToolBtn[i];
    Boolean     inside = true;

    if (!b->enabled) {
        return;
    }
    if (i == kTBNew) {
        PopNewMenu();
        return;
    }

    gToolHover   = (short)i;
    gToolPressed = (short)i;
    DrawToolButton(i);

    while (StillDown()) {
        Point   p;
        Boolean now;

        GetMouse(&p);
        now = PtInRect(p, &b->bounds);
        if (now != inside) {
            inside       = now;
            gToolPressed = (short)(now ? i : -1);
            DrawToolButton(i);
        }
    }

    gToolPressed = -1;
    if (!inside) {
        gToolHover = -1;
    }
    DrawToolButton(i);

    if (inside) {
        ToolbarButtonPressed(i);
    }
}

/*
 * New: the menu drops from under the button, and the button stays pressed
 * for as long as the menu is up. The choice comes back as a menu ID and an
 * item, and only our own menu's items are anyone's business.
 */
static void PopNewMenu(void)
{
    ToolButton *b = &gToolBtn[kTBNew];
    Point       where;
    long        choice;

    if (gNewMenu == NULL) {
        return;
    }

    gToolHover   = kTBNew;
    gToolPressed = kTBNew;
    DrawToolButton(kTBNew);

    where.h = b->bounds.left;
    where.v = b->bounds.bottom;
    LocalToGlobal(&where);
    choice = PopUpMenuSelect(gNewMenu, where.v, where.h, 0);

    gToolPressed = -1;
    gToolHover   = -1;
    DrawToolButton(kTBNew);

    if ((short)(choice >> 16) == kToolbarNewMenuID && gOnCommand != NULL) {
        switch ((short)(choice & 0xFFFF)) {
            case 1:  gOnCommand(kGazetteCmdNewFeed);  break;
            case 2:  gOnCommand(kGazetteCmdNewGroup); break;
            default: break;
        }
    }
}

void GazetteUISearchText(char *out, size_t cap)
{
    Size size = 0;

    if (out == NULL || cap == 0) {
        return;
    }
    out[0] = '\0';
    if (gSearchCtl == NULL) {
        return;
    }
    if (GetControlData(gSearchCtl, kControlEntireControl,
                       kControlEditTextTextTag, (Size)(cap - 1), (Ptr)out,
                       &size) != noErr) {
        size = 0;
    }
    if (size < 0 || (size_t)size >= cap) {
        size = (Size)(cap - 1);
    }
    out[size] = '\0';
}

void GazetteUISetSearchText(const char *text)
{
    if (gSearchCtl == NULL) {
        return;
    }
    if (text == NULL) {
        text = "";
    }
    (void)SetControlData(gSearchCtl, kControlEntireControl,
                         kControlEditTextTextTag, (Size)strlen(text),
                         (Ptr)text);
    if (gWindow != NULL && !GazetteCoreHideToolbar()) {
        SetPortWindowPort(gWindow);
        Draw1Control(gSearchCtl);
    }
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
    TextSize(gStatusSize);
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
    char  count[32];

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
            snprintf(header, sizeof header, "%s - \322%s\323",
                     title, GazetteFeedsFilter());
            snprintf(count, sizeof count, "(%d)",
                     GazetteFeedsArticleCount());
        } else if (unread > 0) {
            /* How many are left to read is the number worth reading; the
               total is only interesting when there is nothing left. */
            snprintf(header, sizeof header, "%s", title);
            snprintf(count, sizeof count, "(%d unread)", unread);
        } else {
            snprintf(header, sizeof header, "%s", title);
            snprintf(count, sizeof count, "(%d)", GazetteFeedsTotalCount());
        }
    } else {
        snprintf(header, sizeof header, "%s",
                 GazetteCoreFeedTitle(gSelectedFeed));
        count[0] = '\0';
    }

    /* The dividers, drawn as the Appearance Manager's own separators so they
       track the theme rather than being two hard-coded greys, and the
       horizontal one carries the row of dots Outlook Express puts in a
       splitter to say that it can be dragged. */
    if (!GazetteCoreHideSidebar()) {
        DrawVDivider(&gVDivider);
        DrawGrabHandle(&gVDivider, true);
    }
    DrawVDivider(&gVDivider2);
    DrawGrabHandle(&gVDivider2, true);

    /* The state first, so the bar is drawn in the state it should be in
       rather than the one it was left in; and before DrawControls, so the
       field is drawn on the bar rather than under it. */
    AdjustToolbarState();
    if (gToolbarStale) {
        RelayoutToolbar();
    }
    DrawToolbar();

    /* The whole control hierarchy in one call — the two lists with their
       frames, scroll bars and focus rings, the two window headers, the
       reader and its bar, and the status placard. */
    DrawControls(gWindow);

    /* Their titles go on top of them: a window header control and a placard
       have no text of their own. */
    DrawHeaderTitle(&gSidebarHeader, "Feeds", NULL);
    DrawHeaderTitle(&gListHeader, header, count);
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
    if (gHeadRowCount == 0) {
        DrawArticlePane();
    }

    /* The bars last and by name. The erase at the top of this took them with
       everything else, and coming back through DrawControls did not put them
       back. */
    if (!GazetteCoreHideSidebar()) {
        DrawListScrollBar(gSidebarList, gSidebarCtl);
    }
    DrawListScrollBar(gArticleList, gArticleCtl);
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

    SelectRow(gArticleList, RowForArticle(gSelectedArticle), true);

    /*
     * The shell first, and then the text. It is the shell that goes and asks
     * for the article's own page, and whether it has is what decides what the
     * pane puts on screen — so asking it afterwards, which is what this used
     * to do, meant the summary was always laid out once before anything could
     * say it was not wanted.
     */
    if (gSelectedArticle >= 0 && gOnArticleChosen != NULL) {
        gOnArticleChosen(gSelectedArticle);
    }

    SetReaderText();

    DrawArticlePane();
    DrawReader();
    if (gReaderScroll != NULL) {
        Draw1Control(gReaderScroll);
    }

    /* Half the toolbar is about the article that is open, so it moves with
       the selection rather than waiting for the next full redraw. */
    GazetteUIAdjustToolbar();
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
/*
 * Drag one of the two grooves. Both are vertical now, so the only difference
 * between them is which width the mouse is setting; Layout does the clamping,
 * so the rule about what each column may not go below lives in one place.
 */
static void TrackDivider(Point where, Boolean second)
{
    Rect  bounds;
    Point pt;

    GetWindowPortBounds(gWindow, &bounds);
    (void)where;

    while (StillDown()) {
        GetMouse(&pt);

        if (second) {
            short want = (short)(pt.h - bounds.left - gSidebarWidth);

            if (want != gListWidth) {
                gListWidth = want;
                Layout();
                GazetteUIUpdate();
            }
        } else {
            short want = (short)(pt.h - bounds.left);

            if (want != gSidebarWidth) {
                gSidebarWidth = want;
                Layout();
                GazetteUIUpdate();
            }
        }
    }

    /* Now that it has stopped moving, the headlines are wrapped to the width
       they actually have. */
    ReflowHeadlines();
    GazetteUIUpdate();
}

/* ------------------------------------------------------------------ */
/* Events                                                              */
/* ------------------------------------------------------------------ */

/*
 * A toolbar button has been pressed. Each one names a command, and the shell
 * maps that onto the handler its menu item already uses — so a button and its
 * menu item are the same act and there is nowhere for them to disagree.
 */
static void ToolbarButtonPressed(int button)
{
    static const int kCommands[kToolbarButtons] = {
        0,                      /* New is a menu; see PopNewMenu */
        kGazetteCmdHideSidebar, kGazetteCmdRefresh, kGazetteCmdMarkAllRead,
        kGazetteCmdHideReadArticles, kGazetteCmdMarkRead,
        kGazetteCmdMarkStarred, kGazetteCmdNextUnread, kGazetteCmdOpenInBrowser
    };

    if (button < 0 || button >= kToolbarButtons || gOnCommand == NULL ||
        kCommands[button] == 0) {
        return;
    }
    gOnCommand(kCommands[button]);
}

/* Do what a click on this row would do. The highlight is the list's own
   business now; this is only the part the rest of the application cares
   about. */
static void ChooseRow(const GazetteSidebarRow *row)
{
    if (row->kind == kGazetteRowSmart) {
        if (row->index == gSelectedSmart) {
            return;
        }
        gSelectedSmart = row->index;
        gSelectedGroup = -1;
        if (gOnSmartChosen != NULL) {
            gOnSmartChosen(row->index);
        }
        return;
    }

    if (row->kind == kGazetteRowGroup) {
        if (row->index == gSelectedGroup && gSelectedSmart < 0) {
            return;
        }
        gSelectedGroup = row->index;
        gSelectedSmart = -1;
        if (gOnGroupChosen != NULL) {
            gOnGroupChosen(row->index);
        }
        return;
    }

    if (row->index != gSelectedFeed || gSelectedGroup >= 0 ||
        gSelectedSmart >= 0) {
        gSelectedFeed  = row->index;
        gSelectedGroup = -1;
        gSelectedSmart = -1;
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

    row = SelectedRow();
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

/* ------------------------------------------------------------------ */
/* Dragging a row                                                      */
/*                                                                     */
/* A feed or a group picked up and put down somewhere else in the      */
/* sidebar, the way the Finder's list view reorders: a press that       */
/* moves is a drag, an insertion line follows the mouse between the    */
/* rows, a closed group under the mouse is framed to say "into here",  */
/* the list scrolls when the mouse reaches its edge, and letting go    */
/* moves the row. The core's MoveFeed and MoveGroup were written for   */
/* exactly this, so what is here is the tracking and the arithmetic    */
/* that turns a place between two rows into a place in the model.      */
/*                                                                     */
/* Tracked here rather than through the Drag Manager, which is built   */
/* for carrying things between applications and draws a translucent   */
/* outline to do it. A row moving within its own list is the Finder's  */
/* own gesture, and the Finder draws it this way.                      */
/* ------------------------------------------------------------------ */

typedef struct {
    Boolean      valid;
    Boolean      into;      /* into a group: its row is framed */
    Boolean      inner;     /* the line is drawn at a group's feeds' indent */
    int          row;       /* the framed group's row, when into */
    int          gap;       /* else before row `gap`; the row count means after the last */
    GazettePlace place;     /* what the drop means to the model */
} DropSpot;

static Boolean SameDropSpot(const DropSpot *a, const DropSpot *b)
{
    if (a->valid != b->valid) {
        return false;
    }
    if (!a->valid) {
        return true;
    }
    return (Boolean)(a->into == b->into && a->inner == b->inner &&
                     (a->into ? a->row == b->row : a->gap == b->gap));
}

static Boolean SamePlace(GazettePlace a, GazettePlace b)
{
    return (Boolean)(a.where == b.where &&
                     (a.where == kGazettePlaceListStart ||
                      a.where == kGazettePlaceListEnd || a.ref == b.ref));
}

/* Where a group's feeds' text begins, which is where the line for a place
   inside a group is drawn from, and the column that says which of the two
   places a gap can mean the mouse means. */
static short InnerIndent(const Rect *view)
{
    return (short)(view->left + kTextInset + kTriangleColumn + kGroupIndent);
}

/*
 * Where between the rows the mouse is: the gap above the row it is on when
 * it is in that row's upper part, the gap below it otherwise. Above the
 * first visible row is the gap above it; below the last, the gap below. A
 * group's own row has three parts rather than two — its middle is a place
 * of its own, into the group, and comes back as `into`.
 */
static void GapAtPoint(Point where, DropSpot *out)
{
    ListBounds visible;
    Rect       view;
    Cell       cell;
    int        count = GazetteCoreSidebarRowCount();

    out->valid = true;
    out->into  = false;
    out->inner = false;
    out->row   = -1;
    ListView(gSidebarList, &view);
    GetListVisibleCells(gSidebarList, &visible);

    cell.h = 0;
    for (cell.v = visible.top; cell.v < visible.bottom && cell.v < count;
         cell.v++) {
        GazetteSidebarRow row;
        Rect              r;

        LRect(&r, cell, gSidebarList);
        if (!PtInRect(where, &r)) {
            continue;
        }
        if (GazetteCoreSidebarRowAt(cell.v, &row) &&
            row.kind == kGazetteRowGroup) {
            short quarter = (short)((r.bottom - r.top) / 4);

            if (where.v < r.top + quarter) {
                out->gap = cell.v;
            } else if (where.v >= r.bottom - quarter) {
                out->gap = cell.v + 1;
            } else {
                out->into = true;
                out->row  = cell.v;
                out->gap  = cell.v;
            }
            return;
        }
        out->gap = (where.v < (r.top + r.bottom) / 2) ? cell.v : cell.v + 1;
        return;
    }
    if (where.v < view.top) {
        out->gap = visible.top;
    } else {
        out->gap = (visible.bottom < count) ? visible.bottom : count;
    }
}

/* Whether a feed is the last of its group's rows: the row after it is not
   one of the group's feeds. */
static Boolean LastRowOfGroup(int row, int group)
{
    GazetteSidebarRow next;

    if (!GazetteCoreSidebarRowAt(row + 1, &next)) {
        return true;
    }
    return (Boolean)(next.kind != kGazetteRowFeed ||
                     GazetteCoreFeedGroup(next.index) != group);
}

/* The row after the last of a group's rows — its own, if it is shut. */
static int RowAfterGroup(int groupRow, int group)
{
    GazetteSidebarRow row;
    int               r = groupRow + 1;

    while (GazetteCoreSidebarRowAt(r, &row) && row.kind == kGazetteRowFeed &&
           GazetteCoreFeedGroup(row.index) == group) {
        r++;
    }
    return r;
}

/*
 * The place a row is at now, said the way a drop is said: what it stands
 * after. A drop that names the same place is not a move.
 */
static GazettePlace CurrentPlace(const GazetteSidebarRow *what)
{
    GazetteSidebarRow above;
    GazettePlace      place;
    int               row;

    place.where = kGazettePlaceListStart;
    place.ref   = 0;

    row = (what->kind == kGazetteRowFeed)
              ? GazetteCoreSidebarRowForFeed(what->index)
              : GazetteCoreSidebarRowForGroup(what->index);
    if (row <= 0 || !GazetteCoreSidebarRowAt(row - 1, &above)) {
        return place;
    }

    switch (above.kind) {
        case kGazetteRowSmart:
            break;
        case kGazetteRowFeed:
            if (what->kind == kGazetteRowFeed &&
                GazetteCoreFeedGroup(above.index) ==
                    GazetteCoreFeedGroup(what->index)) {
                place.where = kGazettePlaceAfterFeed;
                place.ref   = above.index;
            } else if (GazetteCoreFeedGroup(above.index) < 0) {
                place.where = kGazettePlaceAfterFeed;
                place.ref   = above.index;
            } else {
                place.where = kGazettePlaceAfterGroup;
                place.ref   = GazetteCoreFeedGroup(above.index);
            }
            break;
        case kGazetteRowGroup:
            if (what->kind == kGazetteRowFeed &&
                GazetteCoreFeedGroup(what->index) == above.index) {
                place.where = kGazettePlaceGroupStart;
                place.ref   = above.index;
            } else {
                place.where = kGazettePlaceAfterGroup;
                place.ref   = above.index;
            }
            break;
        default:
            break;
    }
    return place;
}

/*
 * What a gap means for a feed, and where the line for it is drawn. The row
 * above the gap decides: after a standing view is the top of the list; after
 * a top-level feed is after it; after a group's feed is after it, in the
 * group — unless it is the group's last, when the gap can mean two things
 * and the mouse says which, the way it does in the Finder: at the feeds'
 * indent, after the feed and in the group; to the left of it, below the
 * group and out of it. After an open group's own row is the top of the
 * group; after a shut group's row is below the group. And the middle of a
 * group's row, open or shut, is the group itself, at the end.
 */
static Boolean ResolveFeedDrop(DropSpot *spot, int from, Point where)
{
    GazetteSidebarRow before;
    Rect              view;
    int               gap = spot->gap;

    ListView(gSidebarList, &view);

    if (spot->into) {
        if (!GazetteCoreSidebarRowAt(spot->row, &before) ||
            before.kind != kGazetteRowGroup) {
            return false;
        }
        spot->place.where = kGazettePlaceGroupEnd;
        spot->place.ref   = before.index;
        return (Boolean)(!(GazetteCoreFeedGroup(from) == before.index &&
                           LastRowOfGroup(GazetteCoreSidebarRowForFeed(from),
                                          before.index)));
    }

    /* Nothing goes above, or between, the three standing views: a drop up
       there is a drop at the top of the feeds. */
    if (gap < kGazetteSmartCount) {
        gap = kGazetteSmartCount;
        spot->gap = gap;
    }
    if (!GazetteCoreSidebarRowAt(gap - 1, &before)) {
        return false;
    }

    switch (before.kind) {
        case kGazetteRowSmart:
            spot->place.where = kGazettePlaceListStart;
            spot->place.ref   = 0;
            break;

        case kGazetteRowFeed: {
            int group = GazetteCoreFeedGroup(before.index);

            if (before.index == from) {
                return false;
            }
            if (group >= 0 && LastRowOfGroup(gap - 1, group) &&
                where.h < InnerIndent(&view)) {
                spot->place.where = kGazettePlaceAfterGroup;
                spot->place.ref   = group;
            } else {
                spot->place.where = kGazettePlaceAfterFeed;
                spot->place.ref   = before.index;
                spot->inner       = (Boolean)(group >= 0);
            }
            break;
        }

        case kGazetteRowGroup:
            if (GazetteCoreGroupCollapsed(before.index)) {
                spot->place.where = kGazettePlaceAfterGroup;
                spot->place.ref   = before.index;
            } else {
                spot->place.where = kGazettePlaceGroupStart;
                spot->place.ref   = before.index;
                spot->inner       = true;
            }
            break;

        default:
            return false;
    }
    return true;
}

/*
 * What a gap means for a group. A group lands only among the top-level
 * things: after a standing view is the top; after a top-level feed is after
 * it; after anything of another group — its row or one of its feeds — is
 * below that group, and the line is drawn there, under the group's last
 * row, wherever in the group the mouse was.
 */
static Boolean ResolveGroupDrop(DropSpot *spot, int from)
{
    GazetteSidebarRow before;
    int               gap = spot->gap;
    int               other;

    spot->into  = false;
    spot->inner = false;
    if (gap < kGazetteSmartCount) {
        gap = kGazetteSmartCount;
        spot->gap = gap;
    }
    if (!GazetteCoreSidebarRowAt(gap - 1, &before)) {
        return false;
    }

    switch (before.kind) {
        case kGazetteRowSmart:
            spot->place.where = kGazettePlaceListStart;
            spot->place.ref   = 0;
            return true;

        case kGazetteRowFeed:
            other = GazetteCoreFeedGroup(before.index);
            if (other < 0) {
                spot->place.where = kGazettePlaceAfterFeed;
                spot->place.ref   = before.index;
                return true;
            }
            break;

        case kGazetteRowGroup:
            other = before.index;
            break;

        default:
            return false;
    }

    if (other == from) {
        return false;               /* somewhere in its own rows */
    }
    spot->place.where = kGazettePlaceAfterGroup;
    spot->place.ref   = other;
    spot->gap = RowAfterGroup(GazetteCoreSidebarRowForGroup(other), other);
    return true;
}

/*
 * The mark for a spot, drawn in XOR so that drawing it again takes it away:
 * a line two pixels tall across the gap, from the indent of the level the
 * drop lands at, or a frame round a group's row. Nothing is drawn for a gap
 * that has scrolled out of view.
 */
static void ToggleDropSpot(const DropSpot *spot)
{
    ListBounds visible;
    Rect       view;
    Rect       r;
    Cell       cell;
    int        count = GazetteCoreSidebarRowCount();

    if (!spot->valid) {
        return;
    }
    ListView(gSidebarList, &view);
    GetListVisibleCells(gSidebarList, &visible);
    cell.h = 0;

    PenNormal();
    PenMode(patXor);

    if (spot->into) {
        if (spot->row < visible.top || spot->row >= visible.bottom) {
            PenNormal();
            return;
        }
        cell.v = (short)spot->row;
        LRect(&r, cell, gSidebarList);
        PenSize(2, 2);
        FrameRect(&r);
    } else {
        short y;
        short left = (short)(view.left + kTextInset);

        if (spot->inner) {
            left = InnerIndent(&view);
        }
        if (spot->gap < count) {
            if (spot->gap < visible.top || spot->gap >= visible.bottom) {
                PenNormal();
                return;
            }
            cell.v = (short)spot->gap;
            LRect(&r, cell, gSidebarList);
            y = r.top;
        } else {
            if (count == 0 || count - 1 < visible.top ||
                count - 1 >= visible.bottom) {
                PenNormal();
                return;
            }
            cell.v = (short)(count - 1);
            LRect(&r, cell, gSidebarList);
            y = r.bottom;
        }
        SetRect(&r, left, (short)(y - 1), view.right, (short)(y + 1));
        PaintRect(&r);
    }
    PenNormal();
}

static void TrackSidebarDrag(const GazetteSidebarRow *what)
{
    DropSpot      shown;
    DropSpot      spot;
    GazettePlace  now = CurrentPlace(what);
    unsigned long lastScroll = 0;
    int           moved;

    shown.valid = false;

    while (StillDown()) {
        ListBounds visible;
        Rect       view;
        Point      p;
        int        count = GazetteCoreSidebarRowCount();

        GetMouse(&p);
        ListView(gSidebarList, &view);
        GetListVisibleCells(gSidebarList, &visible);

        /* At either edge the list scrolls a row at a time, a few ticks
           apart, with the mark lifted while it does. */
        if (TickCount() - lastScroll >= 4) {
            short step = 0;

            if (p.v < view.top && visible.top > 0) {
                step = -1;
            } else if (p.v >= view.bottom && visible.bottom < count) {
                step = 1;
            }
            if (step != 0) {
                ToggleDropSpot(&shown);
                shown.valid = false;
                LScroll(0, step, gSidebarList);
                lastScroll = TickCount();
            }
        }

        GapAtPoint(p, &spot);
        if (what->kind == kGazetteRowFeed) {
            spot.valid = ResolveFeedDrop(&spot, what->index, p);
        } else {
            spot.valid = ResolveGroupDrop(&spot, what->index);
        }
        /* The place it is in already is not somewhere to move it to. */
        if (spot.valid && SamePlace(spot.place, now)) {
            spot.valid = false;
        }
        if (!SameDropSpot(&spot, &shown)) {
            ToggleDropSpot(&shown);
            ToggleDropSpot(&spot);
            shown = spot;
        }
    }
    ToggleDropSpot(&shown);

    if (!shown.valid) {
        return;
    }

    if (what->kind == kGazetteRowFeed) {
        moved = GazetteCoreMoveFeed(what->index, shown.place);
    } else {
        moved = GazetteCoreMoveGroup(what->index, shown.place);
    }
    if (moved < 0) {
        return;
    }
    (void)GazetteCoreSavePrefs();
    GazetteUIFeedsChanged();

    /* The row that was carried stays chosen, where it landed. A feed is
       chosen the way a click chooses it, so what the list shows is the
       feed the sidebar says it is. */
    if (what->kind == kGazetteRowFeed) {
        GazetteUIChooseRow(kGazetteRowFeed, moved);
    } else {
        GazetteUISelectGroup(moved);
    }
}

static void SidebarClicked(Point where, EventModifiers modifiers)
{
    GazetteSidebarRow row;
    Rect              rows;
    Cell              cell;
    int               at;

    /* The triangle's own column opens and shuts a group; the rest of the
       line selects it, the way a folder behaves in a list view. */
    if (HitDisclosure(where)) {
        return;
    }

    /* A press on a feed or a group that moves before it lets go is a drag.
       The three standing views stay where they are, and are not picked up.
       WaitMouseMoved waits until the mouse has moved or the button is up,
       so the click below tracks a press that is still down or takes the
       release, either of which LClick is happy with. */
    if (CellAtPoint(gSidebarList, where, &cell) &&
        GazetteCoreSidebarRowAt(cell.v, &row) &&
        row.kind != kGazetteRowSmart) {
        Point global = where;

        LocalToGlobal(&global);
        if (WaitMouseMoved(global)) {
            TrackSidebarDrag(&row);
            return;
        }
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
/*
 * Tracked here rather than by TEClick.
 *
 * TEClick is the documented way to do this and it did not work: a drag
 * through the article left selStart equal to selEnd, so nothing highlighted
 * and Copy stayed grey. It is documented to be called "when a mouse-down
 * occurs in the view rectangle of the edit record, and the edit record is
 * active", and rather than keep guessing which of its preconditions this
 * window was failing to meet, the loop is written out: TEGetOffset answers
 * which character a point is on, TESetSelect moves the selection and redraws
 * it, and neither has anything to be wrong about.
 *
 * It buys two things as well. The point is clamped into the view, so a drag
 * that wanders into the margin or past the end of the text keeps extending
 * the selection instead of stopping dead at the edge. And a drag that leaves
 * the pane scrolls it, which TEAutoView(false) — set because the scroll
 * offset is this file's to know — takes away from TEClick.
 */
/*
 * Say what the article is drawn on, and in what colour a selection in it is
 * to be marked, before TextEdit draws either.
 *
 * This is why a selection could be made and not seen. TextEdit marks one by
 * inverting, and a QuickDraw invert with the hilite bit clear changes only
 * the pixels that match the port's **background** colour — those become the
 * port's hilite colour, and every other pixel is left exactly as it was.
 *
 * EraseWith deliberately leaves whichever brush drew last sitting in the
 * port; that is the whole point of it, and it is documented where it is
 * defined. So by the time a click reaches the article the background is
 * whatever drew most recently, which is nearly always the headline list's
 * grey — and inverting an article that is drawn on white against a
 * background colour of grey matches nothing, changes nothing, and leaves a
 * selection that is really there and completely invisible.
 *
 * It is the same trap FillHighlight exists to step round in the two lists,
 * arrived at from the other direction.
 */
static void ReaderHiliteColours(void)
{
    RGBColor hilite;

    SetThemeBackground(kThemeBrushWhite, 8, true);

    /* And the colour to mark it in, rather than trusting whatever the port
       was born with. LMGetHiliteRGB is what the Appearance control panel's
       highlight colour arrives as, and it is what the lists already use. */
    LMGetHiliteRGB(&hilite);
    HiliteColor(&hilite);
}

static void ReaderClick(Point where, EventModifiers modifiers)
{
    TEHandle te = gReaderTE;
    Rect     view;
    Point    pt;
    short    anchor;
    short    at;
    short    last;

    if (te == NULL || gWindow == NULL) {
        return;
    }
    SetPortWindowPort(gWindow);
    ReaderHiliteColours();
    view = (**te).viewRect;
    if (view.right <= view.left) {
        return;
    }

    pt = where;
    if (pt.h < view.left)   pt.h = view.left;
    if (pt.h > view.right)  pt.h = (short)(view.right - 1);
    if (pt.v < view.top)    pt.v = view.top;
    if (pt.v > view.bottom) pt.v = (short)(view.bottom - 1);

    TEActivate(te);

    /*
     * Shift extends what is already there, which means the anchor is the far
     * end of the existing selection rather than the point just clicked.
     */
    at = TEGetOffset(pt, te);
    if ((modifiers & shiftKey) != 0 && (**te).selStart != (**te).selEnd) {
        anchor = (at <= (**te).selStart) ? (**te).selEnd : (**te).selStart;
    } else {
        anchor = at;
    }
    TESetSelect((long)((anchor < at) ? anchor : at),
                (long)((anchor < at) ? at : anchor), te);
    last = at;

    while (StillDown()) {
        GetMouse(&pt);

        /*
         * Outside the view above or below, the article scrolls and the
         * selection keeps going — the same gesture every Macintosh text view
         * has. Clamped afterwards, so the offset asked for is one that is on
         * screen now that it has scrolled.
         */
        if (pt.v < view.top) {
            ScrollReaderTo((short)(ReaderOffset() - kReaderLead));
        } else if (pt.v > view.bottom) {
            ScrollReaderTo((short)(ReaderOffset() + kReaderLead));
        }

        if (pt.h < view.left)   pt.h = view.left;
        if (pt.h > view.right)  pt.h = (short)(view.right - 1);
        if (pt.v < view.top)    pt.v = view.top;
        if (pt.v > view.bottom) pt.v = (short)(view.bottom - 1);

        at = TEGetOffset(pt, te);
        if (at != last) {
            TESetSelect((long)((anchor < at) ? anchor : at),
                        (long)((anchor < at) ? at : anchor), te);
            last = at;
        }
    }

    if ((**te).selStart == (**te).selEnd) {
        TEDeactivate(te);
        return;
    }

    /*
     * Drawn again from scratch now that the drag has finished: the pane is
     * erased white and TEUpdate lays the text and the highlight down
     * together, so whatever the running invert left behind, what is on screen
     * at the end is what TextEdit says the selection is.
     */
    DrawReader();
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
            /* Taking a highlight away is the same invert as putting it
               there, so it wants the same background under it. */
            SetPortWindowPort(gWindow);
            ReaderHiliteColours();
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
     * The toolbar first, because it is above everything. Its buttons are
     * ours and track their own press; the search field is a control and the
     * Control Manager takes the click.
     */
    if (PtInRect(where, &gToolbarRect)) {
        ControlRef hit;
        int        i;

        for (i = 0; i < kToolbarButtons; i++) {
            if (PtInRect(where, &gToolBtn[i].bounds)) {
                TrackToolButton(i);
                return;
            }
        }
        hit = FindControlUnderMouse(where, gWindow, &part);
        if (hit != NULL && hit == gSearchCtl) {
            (void)SetKeyboardFocus(gWindow, gSearchCtl,
                                   kControlFocusNextPart);
            (void)HandleControlClick(hit, where, modifiers, NULL);
        }
        return;                     /* the bar itself, or a gap in it */
    }

    /*
     * The dividers are asked first. Each pane now reaches a few pixels into
     * the divider beside it, so that its scroll bar's edge lands on the
     * divider's rule — which means the two overlap, and the divider has to
     * win there or it could never be grabbed.
     */
    if (PtInRect(where, &gVDivider)) {
        TrackDivider(where, false);
        return;
    }
    if (PtInRect(where, &gVDivider2)) {
        TrackDivider(where, true);
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
            int article = ArticleAtRow(SelectedListRow(gArticleList));

            if (article >= 0) {
                SelectArticle(article);
            } else if (gSelectedArticle >= 0) {
                /* A heading, or the empty space below the last row. Neither
                   is an article, so the selection goes back where it was. */
                SelectRow(gArticleList, RowForArticle(gSelectedArticle),
                          false);
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

    if (gSelectedSmart >= 0) {
        row = GazetteCoreSidebarRowForSmart(gSelectedSmart);
    } else if (gSelectedGroup >= 0) {
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

    /*
     * The search box has the keyboard, so it gets the keystroke — all of them
     * but Return, which is what says the search is finished, and Escape,
     * which empties the box. Asked first, because every key below this means
     * something else entirely while something is being typed.
     */
    {
        ControlRef focus = NULL;

        (void)GetKeyboardFocus(gWindow, &focus);
        if (focus != NULL && focus == gSearchCtl) {
            if (key == '\r' || key == 3) {         /* Return, Enter */
                if (gOnCommand != NULL) {
                    gOnCommand(kGazetteCmdSearch);
                }
                return true;
            }
            if (key == 0x1B) {                     /* Escape */
                GazetteUISetSearchText("");
                if (gOnCommand != NULL) {
                    gOnCommand(kGazetteCmdSearch);
                }
                return true;
            }
            SetPortWindowPort(gWindow);
            (void)HandleControlKey(focus, 0, (SInt16)key, modifiers);
            return true;
        }
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
    gActive = active;

    /* Whatever was under the mouse is not ours to raise any more. */
    if (!active && gToolHover >= 0) {
        short was = gToolHover;

        gToolHover = -1;
        DrawToolButton(was);
    }

    /* The lists grey their own scroll bars and selections. */
    if (gSidebarList != NULL) {
        LActivate(active, gSidebarList);
    }
    if (gArticleList != NULL) {
        LActivate(active, gArticleList);
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

    /* Inactive only when the window itself is not in front — having nothing
       to scroll is not a reason to empty the bar. 255 is the inactive hilite
       state, 0 the active one. */
    if (gReaderScroll != NULL) {
        HiliteControl(gReaderScroll, active ? 0 : 255);
    }

    /*
     * The lists last, and drawn rather than only ungreyed.
     *
     * LActivate decides a bar with nothing to scroll should be greyed, and
     * Platinum's answer is an empty track drawn normally -- so it has to be
     * put back. But ungreying it before the hierarchy is activated is worse
     * than not doing it at all: ActivateControl will not repaint a control
     * it finds already active, so the bar stays hollow on screen while
     * reading as awake, and WakeScrollBar, seeing it ungreyed, never tries
     * again. That is the whole of why the feeds list's bar was there at
     * launch and gone the first time the window came back to the front.
     */
    if (active) {
        DrawListScrollBar(gSidebarList, gSidebarCtl);
        DrawListScrollBar(gArticleList, gArticleCtl);
    }
}

void GazetteUIColumnWidths(short *sidebar, short *list)
{
    *sidebar = gSidebarWidth;
    *list    = gListWidth;
}

void GazetteUIResized(void)
{
    Rect bounds;

    if (gWindow == NULL) {
        return;
    }
    Layout();
    ReflowHeadlines();
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

    /* A refresh moves the unread counts, and those are what decide which
       feeds the sidebar is drawing. */
    SyncSidebarRows();

    /* The rows are rebuilt before the list is told how many there are: a
       day's heading is a row too. */
    BuildHeadlineRows();

    LSetDrawingMode(false, gArticleList);
    SetRowCount(gArticleList, gHeadRowCount);
    LScroll(0, (short)-ListRowCount(gArticleList), gArticleList);
    SelectRow(gArticleList, RowForArticle(gSelectedArticle), false);
    LSetDrawingMode(true, gArticleList);

    Layout();
    if (gSelectedArticle >= 0) {
        GazetteFeedsMarkRead(gSelectedArticle, 1);
    }

    /* The first article is open now, exactly as if it had been clicked —
       which means the shell is asked before the text is composed, for the
       reason SelectArticle gives. */
    if (gSelectedArticle >= 0 && gOnArticleChosen != NULL) {
        gOnArticleChosen(gSelectedArticle);
    }

    SetReaderText();
    GazetteUIUpdate();
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

/*
 * A picture has landed, or has turned out not to be coming. The article is
 * composed again with the space it now needs, keeping its place: the reader
 * may be halfway down it, and a photograph arriving is no reason to send
 * them back to the top.
 */
void GazetteUIPhotosChanged(void)
{
    GrafPtr savePort;
    Rect    view;
    short   was, max;

    if (gWindow == NULL || gReaderTE == NULL) {
        return;
    }
    if (GazettePhotosArticle() != gSelectedArticle) {
        return;
    }

    GetPort(&savePort);
    SetPortWindowPort(gWindow);

    was = ReaderOffset();
    SetReaderText();

    max = ReaderMaxOffset();
    if (was > max) {
        was = max;
    }
    if (was < 0) {
        was = 0;
    }
    ReaderRects(&view);
    (**gReaderTE).destRect.top = (short)(ReaderTextTop(&view) - was);
    SyncReaderScroll();

    SetPort(savePort);
    DrawReader();
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

/*
 * Which sidebar lines "Hide Read Feeds" leaves standing. The engine keeps no
 * unread counts of its own — they come from the index, by feed URL — so the
 * window is what works this out and the row model reads the answer back.
 *
 * Two things are kept whatever is left in them: the feed being read, and the
 * group being read. Hiding the column out from under the reader the moment
 * they finish the last article in it is not what the option means.
 */
static void ApplyFeedVisibility(void)
{
    int i;
    int g;

    if (!GazetteCoreHideReadFeeds()) {
        GazetteCoreShowAllRows();
        return;
    }

    /* Groups are hidden first and brought back by their feeds, so a group is
       shown exactly when it still has a line under it — or is the one open. */
    for (g = 0; g < GazetteCoreGroupCount(); g++) {
        GazetteCoreSetGroupHidden(g, true);
    }
    for (i = 0; i < GazetteCoreFeedCount(); i++) {
        Boolean keep = (Boolean)(i == gSelectedFeed || FeedUnread(i) > 0);
        int     group;

        GazetteCoreSetFeedHidden(i, (Boolean)!keep);
        group = GazetteCoreFeedGroup(i);
        if (keep && group >= 0) {
            GazetteCoreSetGroupHidden(group, false);
        }
    }
    if (gSelectedGroup >= 0) {
        GazetteCoreSetGroupHidden(gSelectedGroup, false);
    }
}

/*
 * What goes beside the three standing views.
 *
 * Two of the three are free. The index keeps an unread count for every feed —
 * which is where the number beside a feed comes from already — and it knows
 * how many articles are starred. Only "Today" has to be counted, and counting
 * it means reading every cache, so it is done here, once, when the rows are
 * rebuilt, and not in the drawing routine that runs once a row.
 *
 * The starred number is the whole set, which can be a little ahead of what
 * the Starred view would show: an article stays starred after the feed it
 * came from is removed. That is the right way round for it to be wrong.
 */
static void CountSmartRows(void)
{
    int total = 0;
    int i;

    for (i = 0; i < GazetteCoreFeedCount(); i++) {
        if (GazetteCoreFeedEnabled(i)) {
            total += FeedUnread(i);
        }
    }

    gSmartCount[kGazetteSmartToday]   = GazetteFeedsCountToday();
    gSmartCount[kGazetteSmartUnread]  = total;
    gSmartCount[kGazetteSmartStarred] = GazetteIndexStarredCount();
}

/* The sidebar's rows, after whatever has just changed what is in them. */
static void SyncSidebarRows(void)
{
    ApplyFeedVisibility();
    CountSmartRows();

    if (gSidebarList == NULL) {
        return;
    }
    LSetDrawingMode(false, gSidebarList);
    SetRowCount(gSidebarList, GazetteCoreSidebarRowCount());
    SelectRow(gSidebarList, SelectedRow(), false);
    LSetDrawingMode(true, gSidebarList);
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

    SyncSidebarRows();

    Layout();
    GazetteUIUpdate();
}

/*
 * Something in the View menu has moved. Everything downstream of it is
 * re-derived: which feeds are drawn, which articles are in the list and in
 * what order, and where the three columns are.
 *
 * The article being read is followed across the change rather than being
 * dropped — its index in the list means something different on the other side
 * of a sort, and losing the reader's place because they turned the list over
 * would be the most annoying possible way to answer the command.
 */
void GazetteUIViewChanged(void)
{
    const GazetteArticle *was = NULL;
    char                  link[kGazetteArticleLinkLen];

    if (gWindow == NULL) {
        return;
    }

    link[0] = '\0';
    was     = GazetteFeedsArticleAt(gSelectedArticle);
    if (was != NULL) {
        (void)gz_copy_n(link, sizeof link, was->link, strlen(was->link));
    }

    GazetteFeedsRebuildView();

    /* Where that article has ended up, if it is still in the list at all. */
    gSelectedArticle = -1;
    if (link[0] != '\0') {
        int i;

        for (i = 0; i < GazetteFeedsArticleCount(); i++) {
            const GazetteArticle *a = GazetteFeedsArticleAt(i);

            if (a != NULL && strcmp(a->link, link) == 0) {
                gSelectedArticle = i;
                break;
            }
        }
    }
    if (gSelectedArticle < 0 && GazetteFeedsArticleCount() > 0) {
        gSelectedArticle = 0;
    }

    SyncSidebarRows();

    BuildHeadlineRows();
    LSetDrawingMode(false, gArticleList);
    SetRowCount(gArticleList, gHeadRowCount);
    SelectRow(gArticleList, RowForArticle(gSelectedArticle), true);
    LSetDrawingMode(true, gArticleList);

    Layout();
    SetReaderText();
    GazetteUIUpdate();
}

/*
 * The next headline below the one being read that has not been read. Forward
 * only and no wrap: "next" means further down the list, and a command that
 * silently jumped back to the top would take the reader somewhere they had
 * already been. Returns false when there is nothing after this one.
 */
Boolean GazetteUINextUnread(void)
{
    int count = GazetteFeedsArticleCount();
    int i;

    if (gWindow == NULL) {
        return false;
    }
    for (i = (gSelectedArticle < 0) ? 0 : gSelectedArticle + 1;
         i < count; i++) {
        const GazetteArticle *a = GazetteFeedsArticleAt(i);

        if (a != NULL && !a->read) {
            SelectArticle(i);
            return true;
        }
    }
    return false;
}

/* Which article is open, by its address — what "Open in Browser" needs, and
   the only thing above this header that wants an article's link. */
const char *GazetteUISelectedArticleLink(void)
{
    const GazetteArticle *a = GazetteFeedsArticleAt(gSelectedArticle);

    return (a != NULL) ? a->link : "";
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
    if (gSelectedSmart >= 0) {
        /* A standing view is not a feed and not a group, so nothing in the
           Feeds menu applies to it and this says so by saying nothing. */
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

void GazetteUISelectSmart(int which)
{
    GazetteSidebarRow row;

    if (which < 0 || which >= kGazetteSmartCount) {
        return;
    }
    row.kind  = kGazetteRowSmart;
    row.index = which;

    /* Through ChooseRow so that picking a standing view from the Article
       menu and clicking its row in the sidebar are the same act — the
       callback, the state and the highlight all move together. */
    ChooseRow(&row);
    SelectRow(gSidebarList, GazetteCoreSidebarRowForSmart(which), true);
    if (gWindow != NULL) {
        DrawSidebarPane();
    }
}

void GazetteUISelectGroup(int index)
{
    if (index < 0 || index >= GazetteCoreGroupCount()) {
        return;
    }
    gSelectedGroup = index;
    gSelectedSmart = -1;
    SelectRow(gSidebarList, GazetteCoreSidebarRowForGroup(index), true);
    if (gWindow != NULL) {
        DrawSidebarPane();
    }
}

Boolean GazetteUISidebarRowAt(Point where, int *kind, int *index)
{
    GazetteSidebarRow row;
    Rect              rows;
    Cell              cell;

    if (gWindow == NULL || gSidebarList == NULL || GazetteCoreHideSidebar()) {
        return false;
    }
    ListView(gSidebarList, &rows);
    if (!PtInRect(where, &rows) || !CellAtPoint(gSidebarList, where, &cell)) {
        return false;
    }
    if (!GazetteCoreSidebarRowAt(cell.v, &row)) {
        return false;
    }
    if (kind != NULL) {
        *kind = row.kind;
    }
    if (index != NULL) {
        *index = row.index;
    }
    return true;
}

Boolean GazetteUIArticleRowAt(Point where, int *index)
{
    Rect rows;
    Cell cell;
    int  article;

    if (gWindow == NULL || gArticleList == NULL ||
        !PtInRect(where, &gListPane)) {
        return false;
    }
    ListView(gArticleList, &rows);
    if (!PtInRect(where, &rows) || !CellAtPoint(gArticleList, where, &cell)) {
        return false;
    }
    article = ArticleAtRow(cell.v);
    if (article < 0) {
        return false;               /* a date heading, or empty space */
    }
    if (index != NULL) {
        *index = article;
    }
    return true;
}

void GazetteUIChooseArticle(int index)
{
    if (gWindow == NULL || GazetteFeedsArticleAt(index) == NULL) {
        return;
    }
    SetFocus(kRefList);
    SelectArticle(index);
}

void GazetteUIChooseRow(int kind, int index)
{
    GazetteSidebarRow row;
    int               at;

    if (gWindow == NULL) {
        return;
    }
    row.kind  = kind;
    row.index = index;

    switch (kind) {
        case kGazetteRowSmart: at = GazetteCoreSidebarRowForSmart(index); break;
        case kGazetteRowGroup: at = GazetteCoreSidebarRowForGroup(index); break;
        case kGazetteRowFeed:  at = GazetteCoreSidebarRowForFeed(index);  break;
        default:               return;
    }
    if (at < 0) {
        return;
    }
    ChooseRow(&row);
    SelectRow(gSidebarList, at, true);
    DrawSidebarPane();
}

void GazetteUISelectFeed(int index)
{
    if (index < 0 || index >= GazetteCoreFeedCount()) {
        return;
    }
    gSelectedFeed  = index;
    gSelectedGroup = -1;
    gSelectedSmart = -1;
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
                            short rowHeight,
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

    ListRowsIn(bounds, &view);
    SetRect(&data, 0, 0, 1, 0);         /* one column, no rows yet */
    cell.v = rowHeight;
    cell.h = (short)(view.right - view.left);

    if (CreateCustomList(&view, &data, cell, &spec, gWindow,
                         false, false, false, true, outList) != noErr ||
        *outList == NULL) {
        return false;
    }
    PlaceListScrollBar(*outList, bounds);

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

/*
 * Where the window opens: where it stood last time, if that is still
 * somewhere on the screen, and its usual place otherwise.
 *
 * The screen the preferences were written on need not be the one they are
 * read on — a PowerBook comes home to a monitor, a resolution changes — and
 * a window whose title bar is off every screen is a window nobody can drag
 * back. So the rectangle is cut down to the main screen first, and then the
 * title bar is asked whether some real piece of it, thirty-two pixels or so,
 * still falls on a screen; GetGrayRgn is every screen at once. A window that
 * was on a second monitor that is no longer there fails that test and opens
 * where a first run does.
 */
static void OpeningBounds(Rect *bounds)
{
    long  left, top, width, height;
    Rect  screen, strip;
    short mbar;

    SetRect(bounds, 40, 48, 40 + kGazetteMinWindowWidth + 200,
            48 + kGazetteMinWindowHeight + 160);

    if (!GazetteCoreWindowBounds(&left, &top, &width, &height)) {
        return;
    }

    screen = (*GetMainDevice())->gdRect;
    mbar   = GetMBarHeight();

    /* The size first: it need not be where it was to be as big as it was,
       and a window larger than the screen it now finds itself on is cut
       down to fit under the menu bar. */
    if (width > screen.right - screen.left) {
        width = screen.right - screen.left;
    }
    if (height > screen.bottom - screen.top - mbar - kTitleBarHeight) {
        height = screen.bottom - screen.top - mbar - kTitleBarHeight;
    }
    if (width < kGazetteMinWindowWidth) {
        width = kGazetteMinWindowWidth;
    }
    if (height < kGazetteMinWindowHeight) {
        height = kGazetteMinWindowHeight;
    }

    /* Then the place. A title bar that is off the screen cannot be grabbed,
       and this asks for enough of it to grab. */
    SetRect(&strip, (short)(left + 32), (short)(top - kTitleBarHeight),
            (short)(left + width - 32), (short)(top - 2));
    if (strip.right <= strip.left || !RectInRgn(&strip, GetGrayRgn())) {
        return;
    }

    SetRect(bounds, (short)left, (short)top, (short)(left + width),
            (short)(top + height));
}

Boolean GazetteUIOpen(GazetteUIFeedChosen onFeedChosen,
                      GazetteUIArticleChosen onArticleChosen,
                      GazetteUIGroupChosen onGroupChosen,
                      GazetteUISmartChosen onSmartChosen,
                      GazetteUICommandChosen onCommand)
{
    OSStatus         err;
    Rect             bounds;
    WindowAttributes attrs;
    long             sidebar, list;

    if (gWindow != NULL) {
        return true;
    }

    gOnFeedChosen    = onFeedChosen;
    gOnSmartChosen   = onSmartChosen;
    gOnCommand       = onCommand;
    gOnArticleChosen = onArticleChosen;
    gOnGroupChosen   = onGroupChosen;

    OpeningBounds(&bounds);

    /* The columns as they were left. Layout clamps them to the window they
       find themselves in, so a width saved from a wider window comes back
       as wide as this one allows. */
    if (GazetteCoreColumnWidths(&sidebar, &list)) {
        gSidebarWidth = (short)sidebar;
        gListWidth    = (short)list;
    }

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
    gSelectedSmart   = -1;
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
    MakeToolbar();

    gSidebarHeaderCtl = MakeControl(&gSidebarHeader,
                                    kControlWindowHeaderProc, 0);
    gListHeaderCtl    = MakeControl(&gListHeader,
                                    kControlWindowHeaderProc, 0);

    if (!MakeListPane(gSidebarLDEF, &gSidebarPane, gRowHeight, &gSidebarCtl,
                      &gSidebarList) ||
        !MakeListPane(gArticleLDEF, &gListPane, gHeadRowHeight, &gArticleCtl,
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
    DisposeToolbar();
    gSearchCtl         = NULL;
    gSidebarHeaderCtl  = NULL;
    gListHeaderCtl     = NULL;
    gReaderCtl         = NULL;
    gFocusPane         = NULL;
    gRootControl       = NULL;

    ForgetPhotos();
    if (gMoviesEntered) {
        ExitMovies();
        gMoviesEntered = false;
    }

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
