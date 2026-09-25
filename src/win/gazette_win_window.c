/*
 * gazette_win_window.c - Gazette's window on Windows.
 *
 * The counterpart of src/ui/platinum_window.c, and laid out to the same
 * measurements: a toolbar across the top, a header over the sidebar and
 * another over the headline list, three columns with a draggable groove
 * between each pair, and a status strip along the foot. The article
 * column has no header -- its headline and byline are the first two
 * paragraphs of the text itself.
 *
 * What differs from the Mac file is only who draws. Mac OS 9 has no
 * control that does any of this, so platinum_window.c draws all of it;
 * Windows has a toolbar, a header, a tree and a list, so they are used.
 * The headline list is the exception: the rows are two lines of text
 * with an icon and a banded date heading, which no stock list draws, so
 * it is a list view that hands its rows back to be drawn here -- the
 * same arrangement as the Mac's List Manager with its own LDEF.
 */

#define WINVER       0x0400
#define _WIN32_WINNT 0x0400
#define _WIN32_IE    0x0300

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <string.h>

#include "gazette_win.h"
#include "gazette_win_res.h"
#include "core/gazette_core.h"
#include "core/gazette_sys.h"
#include "feeds/gazette_feed_parse.h"
#include "feeds/gazette_feeds.h"
#include "feeds/gazette_index.h"
#include "feeds/gazette_photos.h"
#include "portable/gazette_portable.h"

/* TVS_NOHSCROLL is comctl32 5.80's (IE5): no horizontal scroll bar in
   the tree. An older tree ignores the bit, and the labels are fitted to
   the width anyway (FitLabel), so none is ever needed. */
#ifndef TVS_NOHSCROLL
#define TVS_NOHSCROLL 0x8000
#endif

/* I_IMAGENONE is declared for _WIN32_IE 0x0501 and up; the value is what
   comctl32 4.70 already understood as "this button has no image". */
#ifndef I_IMAGENONE
#define I_IMAGENONE (-2)
#endif

/* ------------------------------------------------------------------ */
/* Measurements                                                        */
/*                                                                     */
/* The Mac's, from platinum_window.c, kept in the same order it keeps   */
/* them. The ones that are a font's height there are a font's height    */
/* here too, measured in MeasureFonts().                                */
/* ------------------------------------------------------------------ */

enum {
    kSplitterWidth  = 4,        /* the Mac's groove is six, two of them rule */
    kMinSidebar     = 96,
    kMinList        = 180,
    kMinReader      = 220,
    kDefaultSidebar = 168,
    kDefaultList    = 280,

    kHeadlinePad    = 6,        /* air above and below a headline block */
    kBandPad        = 5,        /* and above and below a date band's words */
    kHeadlineLines  = 2,        /* a headline is always exactly two rows */
    kRowIconGap     = 4,        /* icon to text */
    kRowIndent      = 4,        /* pane edge to icon */

    kBandMargin     = 12,       /* what the rebar's band keeps for itself */
    kEtchedEdge     = 2,        /* the etched edge round the toolbar strip */
    kToolbarGap     = 3         /* window grey between the strip and the panes:
                                   measured off Outlook Express 5, 2026-09-24 */
};

/* ------------------------------------------------------------------ */
/* The toolbar                                                         */
/*                                                                     */
/* The Mac's nine buttons in the Mac's four groups, with the Mac's      */
/* captions. Three of them change what they say and what they wear      */
/* according to what they would do next -- the same pairs, and the      */
/* same rule that a button says what it would do rather than what is    */
/* so.                                                                 */
/* ------------------------------------------------------------------ */

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
    kTBFind,
    kToolbarButtons
};

typedef struct {
    int         command;        /* the menu command it stands for */
    int         icon;
    int         otherIcon;      /* for the toggles; -1 when there is none */
    const char *caption;
    const char *otherCaption;
    BOOL        group;          /* a separator stands before this one */
} ToolSpec;

static const ToolSpec kToolSpec[kToolbarButtons] = {
    { IDM_FILE_NEW_FEED,    kIconNew,          -1,                "New",                NULL,                 FALSE },
    { IDM_VIEW_SIDEBAR,     kIconSidebar,      -1,                "Hide Sidebar",       "Show Sidebar",       TRUE  },
    { IDM_FILE_REFRESH,     kIconRefresh,      -1,                "Refresh",            NULL,                 FALSE },
    { IDM_FEEDS_MARK_ALL,   kIconMarkAllRead,  kIconMarkAllUnread,"Mark All as Read",   "Mark All as Unread", TRUE  },
    { IDM_VIEW_HIDE_READ,   kIconHideRead,     kIconShowRead,     "Hide Read Articles", "Show Read Articles", FALSE },
    { IDM_ARTICLE_UNREAD,   kIconMarkRead,     kIconMarkUnread,   "Mark as Read",       "Mark as Unread",     TRUE  },
    { IDM_ARTICLE_STAR,     kIconStarred,      -1,                "Star Article",       "Unstar Article",     FALSE },
    { IDM_ARTICLE_NEXT,     kIconNextUnread,   -1,                "Next Unread",        NULL,                 FALSE },
    { IDM_ARTICLE_BROWSER,  kIconBrowser,      -1,                "Open in Browser",    NULL,                 FALSE },
    /* Find, in a group of its own at the end: the standard Find dialog
       every Windows program has, in place of the Mac's search field, and
       the same magnifier the Mac's field wears. */
    { IDM_EDIT_FIND,        kIconFind,         -1,                "Find",               NULL,                 TRUE  }
};

/* ------------------------------------------------------------------ */
/* State                                                               */
/* ------------------------------------------------------------------ */

static const char kSplitterClass[] = "GazetteSplitter";
static const char kReaderClass[]   = "GazetteReader";
static const char kPaneClass[]     = "GazettePane";

static HINSTANCE  gInstance;
static HWND       gFrame;
static HWND       gRebar;        /* the band the toolbar sits in; NULL on 4.0 */
static HWND       gToolbar;
static HWND       gSidebarPane;  /* sunken frame holding header and tree */
static HWND       gListPane;     /* the same for the headline list */
static HWND       gSidebarHeader;
static HWND       gListHeader;
static HWND       gSidebar;
static HWND       gHeadlines;
static HWND       gReader;
static HWND       gSplitLeft;
static HWND       gSplitRight;
static HWND       gStatus;

static HIMAGELIST gIcons;        /* every 16x16 icon, in kIcon* order */
static HFONT      gUIFont;

/* The widths the user chose, which are what is remembered, and the widths
   shown, which are those fitted to the window as it is now. Two, so that a
   narrow window -- or a minimized one, which is no width at all -- shows
   narrower columns without forgetting the ones asked for (Bruno, 86Box,
   2026-09-26: widths were lost on minimize and maximize). */
static int  gSidebarWidth = kDefaultSidebar;
static int  gListWidth    = kDefaultList;
static int  gShownSidebar = kDefaultSidebar;
static int  gShownList    = kDefaultList;
static BOOL gSidebarHidden;
static BOOL gToolbarHidden;

static int  gLineHeight;         /* one line of the interface font */
static int  gRowHeight;          /* a headline block: two lines and its air */
static int  gHeaderHeight;
static int  gStatusHeight;
static int  gToolbarHeight;
static int  gDragOffset;
static HFONT gBoldFont;          /* an unread headline */
static int  gHeadingHeight;      /* a date band: one line and its air */

/* Windows' own small icons, from shell32 at run time, appended to gIcons:
   the folder a group is, open when selected, and the document a feed and a
   headline are -- the Mac asks the Icon Services for the same three. */
static int  gFolderIcon     = -1;
static int  gOpenFolderIcon = -1;
static int  gDocIcon        = -1;

/* What the application said to call when a row or a headline is chosen,
   and when the Find dialog asks for a search: the Mac's GazetteUIOpen
   callbacks, so the dependency runs one way, shell to window. */
static GazetteUIFeedChosen    gOnFeedChosen;
static GazetteUIArticleChosen gOnArticleChosen;
static GazetteUIGroupChosen   gOnGroupChosen;
static GazetteUISmartChosen   gOnSmartChosen;
static GazetteUICommandChosen gOnCommand;

/* The selection, as platinum_window.c keeps it: one of a standing view, a
   group or a feed, and the article open in the reader. */
static int  gSelectedFeed    = 0;
static int  gSelectedGroup   = -1;
static int  gSelectedSmart   = -1;
static int  gSelectedArticle = -1;
static int  gSmartCount[kGazetteSmartCount];

/* True while the tree is being filled or its selection set from here, so
   what it reports back is not taken for a click. */
static BOOL gSyncingTree;

/*
 * The headline list's rows, as the Mac's: a day's articles gathered under
 * a band naming the day, so the date comes off the rows. Here a headline
 * is one row two lines tall, where the Mac's List Manager needs a row a
 * line.
 */
enum {
    kHeadlineDate    = 0,
    kHeadlineArticle = 1
};

typedef struct {
    int kind;
    int article;
} HeadlineRow;

static HeadlineRow gHeadRows[kGazetteMaxArticles * 2];
static int         gHeadRowCount;

static int  RowKind(LPARAM param);
static int  RowIndex(LPARAM param);
static void ChooseRow(int kind, int index);
static void HeadlineRowChosen(void);

static LRESULT CALLBACK SplitterProc(HWND, UINT, WPARAM, LPARAM);
static void SyncSidebarRows(void);
static void AdjustToolbarState(void);
static void MeasureToolbar(void);

/* ------------------------------------------------------------------ */
/* Fonts                                                               */
/*                                                                     */
/* MS Sans Serif on 95 and NT 4, Tahoma on XP, and whatever the user    */
/* chose if they changed it -- the equivalent of the Mac's asking for   */
/* the Large System Font rather than naming Charcoal.                   */
/* ------------------------------------------------------------------ */

static void MeasureFonts(HWND frame)
{
    NONCLIENTMETRICSA metrics;
    HDC        dc;
    HFONT      previous;
    TEXTMETRICA text;

    ZeroMemory(&metrics, sizeof(metrics));
    metrics.cbSize = sizeof(metrics);

    if (SystemParametersInfoA(SPI_GETNONCLIENTMETRICS, sizeof(metrics),
                              &metrics, 0)) {
        gUIFont = CreateFontIndirectA(&metrics.lfMessageFont);
    }
    if (gUIFont == NULL) {
        gUIFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    }

    dc = GetDC(frame);
    previous = (HFONT)SelectObject(dc, gUIFont);
    GetTextMetricsA(dc, &text);
    SelectObject(dc, previous);
    ReleaseDC(frame, dc);

    gLineHeight = text.tmHeight;

    /*
     * A headline is always exactly two lines, and the fixed height is
     * what lets the padding and the line spacing be set independently --
     * the reason the Mac file gives for not wrapping to a variable
     * number of rows.
     */
    gRowHeight = kHeadlinePad + gLineHeight * kHeadlineLines + kHeadlinePad;
    gHeadingHeight = kBandPad + gLineHeight + kBandPad;

    /* The same face, bold: a headline not yet read. */
    {
        LOGFONTA face;

        if (GetObjectA(gUIFont, sizeof(face), &face) != 0) {
            face.lfWeight = FW_BOLD;
            gBoldFont = CreateFontIndirectA(&face);
        }
        if (gBoldFont == NULL) {
            gBoldFont = gUIFont;
        }
    }
}

static void ApplyFont(HWND control)
{
    if (control != NULL && gUIFont != NULL) {
        SendMessage(control, WM_SETFONT, (WPARAM)gUIFont, MAKELPARAM(TRUE, 0));
    }
}

HFONT GazetteWindowFont(void)
{
    return gUIFont;
}

/* ------------------------------------------------------------------ */
/* The headers over the two lists                                      */
/*                                                                     */
/* A one-item header control each, which is the Windows control whose   */
/* job this is: a band of the face colour with a rule under it, themed  */
/* on XP and 3D before it. The Mac uses kControlWindowHeaderProc for    */
/* the same two bands. Each item is exactly as wide as its pane, and    */
/* the headline list's is drawn here so it can carry the count at its   */
/* right-hand end without becoming a second column.                     */
/* ------------------------------------------------------------------ */

static HWND MakeHeader(HWND parent, int id)
{
    /* HDS_BUTTONS: the raised face a list view's column header has. */
    return CreateWindowExA(0, WC_HEADERA, NULL,
                           WS_CHILD | WS_VISIBLE | HDS_HORZ | HDS_BUTTONS,
                           0, 0, 0, 0, parent, (HMENU)(INT_PTR)id,
                           gInstance, NULL);
}

/*
 * One item, as wide as the header. Text NULL leaves the text alone and only
 * resizes: saying HDI_TEXT with a NULL pointer is how "U÷w" got into the
 * headline band -- the control read a string from address zero.
 */
static void SetHeaderItem(HWND header, int index, const char *text,
                          int width, int format)
{
    HD_ITEMA item;

    ZeroMemory(&item, sizeof(item));
    item.mask = HDI_WIDTH | HDI_FORMAT;
    item.cxy  = width;
    item.fmt  = format;
    if (text != NULL) {
        item.mask   |= HDI_TEXT;
        item.pszText = (char *)text;
        item.cchTextMax = (int)strlen(text);
    }

    if (SendMessage(header, HDM_GETITEMCOUNT, 0, 0) <= index) {
        SendMessage(header, HDM_INSERTITEMA, (WPARAM)index, (LPARAM)&item);
    } else {
        SendMessage(header, HDM_SETITEMA, (WPARAM)index, (LPARAM)&item);
    }
}

/*
 * The headline band says two things, as the Mac's does: the view's name on
 * the left and its unread count on the right. A header item holds one
 * string with one alignment, so the item is drawn here (HDF_OWNERDRAW, in
 * comctl32 4.0); a second item for the count read as a column, which the
 * Mac's single list of headlines has none of.
 */
static char gListTitle[128] = "Today";
static char gListCount[32];

static void SetListTitle(const char *title, const char *count)
{
    if (title != NULL) {
        lstrcpynA(gListTitle, title, sizeof(gListTitle));
    }
    if (count != NULL) {
        lstrcpynA(gListCount, count, sizeof(gListCount));
    }
    if (gListHeader != NULL) {
        InvalidateRect(gListHeader, NULL, TRUE);
    }
}

static void MeasureHeader(void)
{
    /* A header control answers with the height it wants; nothing else
       knows, because it depends on the theme as well as the font. */
    HD_LAYOUT layout;
    WINDOWPOS position;
    RECT      area;

    SetRect(&area, 0, 0, 200, 200);
    ZeroMemory(&position, sizeof(position));
    layout.prc = &area;
    layout.pwpos = &position;

    gHeaderHeight = 0;
    if (SendMessage(gSidebarHeader, HDM_LAYOUT, 0, (LPARAM)&layout)) {
        gHeaderHeight = position.cy;
    }
    if (gHeaderHeight <= 0) {
        gHeaderHeight = gLineHeight + 6;
    }
}

/* ------------------------------------------------------------------ */
/* Creation                                                            */
/* ------------------------------------------------------------------ */

/*
 * What each button is showing, kept here rather than read back from the
 * control: the toolbar is rebuilt from it whenever a caption has to come or
 * go, and comctl32 4.0 has no call to change a button's caption in place.
 */
static BOOL gToolEnabled[kToolbarButtons];
static BOOL gToolOther[kToolbarButtons];    /* showing its second face */
static BOOL gToolCaptioned[kToolbarButtons];
static int  gToolString[kToolbarButtons][2];/* string-pool index per face */
static int  gToolLimit;                     /* the width last fitted to */
static BOOL gToolHidden[kToolbarButtons];   /* behind the chevron */
static BOOL gToolChevron;                   /* the row has a >> at its end */
static int  gChevronString = -1;

static int ToolIcon(int i)
{
    return (gToolOther[i] && kToolSpec[i].otherIcon >= 0)
               ? kToolSpec[i].otherIcon : kToolSpec[i].icon;
}

/*
 * Every caption any button can show, added once. TB_ADDSTRING takes a run
 * of strings each ended by a NUL, the run by a second, and answers the index
 * of the first; buttons then name their caption by index. (A pointer in
 * iString works too, but only from comctl32 4.70, and 4.0 reads it as an
 * index and draws whatever is there.) Added once and not per rebuild,
 * because 4.0 cannot take strings out again.
 */
static void AddToolStrings(void)
{
    char pool[768];
    int  used = 0;
    int  next = 0;
    int  i, face;
    int  first;

    for (i = 0; i < kToolbarButtons; i++) {
        for (face = 0; face < 2; face++) {
            const char *text = face ? kToolSpec[i].otherCaption
                                    : kToolSpec[i].caption;
            int n;

            gToolString[i][face] = -1;
            if (text == NULL) {
                continue;
            }
            n = lstrlenA(text);
            if (used + n + 2 > (int)sizeof(pool)) {
                continue;
            }
            CopyMemory(pool + used, text, (SIZE_T)n);
            used += n;
            pool[used++] = '\0';
            gToolString[i][face] = next++;
        }
    }
    /* The chevron's caption: a right-pointing double angle, which the
       ANSI code page of every Western Windows has at 0xBB. */
    if (used + 3 <= (int)sizeof(pool)) {
        pool[used++] = (char)0xBB;
        pool[used++] = '\0';
        gChevronString = next++;
    }
    pool[used] = '\0';

    first = (int)SendMessage(gToolbar, TB_ADDSTRINGA, 0, (LPARAM)pool);
    if (gChevronString >= 0) {
        gChevronString = (first >= 0) ? first + gChevronString : -1;
    }
    for (i = 0; i < kToolbarButtons; i++) {
        for (face = 0; face < 2; face++) {
            if (gToolString[i][face] >= 0) {
                gToolString[i][face] = (first >= 0)
                                           ? first + gToolString[i][face] : -1;
            }
        }
    }
}

/* The buttons, made again from the state above. Redraw is held off so the
   row does not flash empty in between. */
static void RebuildToolbar(void)
{
    TBBUTTON buttons[kToolbarButtons * 2];
    int      count = 0;
    int      i;

    SendMessage(gToolbar, WM_SETREDRAW, FALSE, 0);
    while (SendMessage(gToolbar, TB_BUTTONCOUNT, 0, 0) > 0) {
        SendMessage(gToolbar, TB_DELETEBUTTON, 0, 0);
    }

    ZeroMemory(buttons, sizeof(buttons));
    for (i = 0; i < kToolbarButtons; i++) {
        int face = (gToolOther[i] && kToolSpec[i].otherCaption != NULL);

        if (kToolSpec[i].group) {
            buttons[count].iBitmap   = 6;       /* the separator's width */
            buttons[count].fsStyle   = TBSTYLE_SEP;
            buttons[count].fsState   = (BYTE)(gToolHidden[i] ? TBSTATE_HIDDEN
                                                             : 0);
            count++;
        }
        buttons[count].iBitmap   = ToolIcon(i);
        buttons[count].idCommand = kToolSpec[i].command;
        buttons[count].fsState   = (BYTE)((gToolEnabled[i] ? TBSTATE_ENABLED
                                                           : 0) |
                                          (gToolHidden[i] ? TBSTATE_HIDDEN
                                                          : 0));
        /* One width for every button, as Outlook Express's are, so a long
           caption wraps to a second line instead of widening its button. */
        buttons[count].fsStyle   = TBSTYLE_BUTTON;
        buttons[count].iString   = gToolCaptioned[i] ? gToolString[i][face]
                                                     : -1;
        count++;
    }
    /* The chevron, last, when buttons are hidden behind it: a caption and
       no picture (I_IMAGENONE, comctl32 4.70; 4.0 draws the caption under
       an empty square). */
    if (gToolChevron) {
        buttons[count].iBitmap   = I_IMAGENONE;
        buttons[count].idCommand = IDC_TOOL_CHEVRON;
        buttons[count].fsState   = TBSTATE_ENABLED;
        buttons[count].fsStyle   = TBSTYLE_BUTTON | TBSTYLE_AUTOSIZE;
        buttons[count].iString   = gChevronString;
        count++;
    }
    SendMessage(gToolbar, TB_ADDBUTTONS, (WPARAM)count, (LPARAM)buttons);

    SendMessage(gToolbar, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(gToolbar, NULL, TRUE);
}

/* How far right the row reaches: the furthest right edge of any button
   still showing (a hidden one answers an empty rectangle). */
static int ToolbarWidth(void)
{
    int  count = (int)SendMessage(gToolbar, TB_BUTTONCOUNT, 0, 0);
    int  right = 0;
    int  i;
    RECT r;

    for (i = 0; i < count; i++) {
        if (SendMessage(gToolbar, TB_GETITEMRECT, (WPARAM)i, (LPARAM)&r) &&
            r.right > right) {
            right = r.right;
        }
    }
    return right;
}

/*
 * The buttons' width: wide enough for a caption wrapped to two lines, as
 * Outlook Express's are, or just an icon's when captions are off. The same
 * for every button -- TB_SETBUTTONWIDTH (4.71) has no other kind -- which is
 * why captions go all together rather than one at a time.
 */
static void SetToolCaptions(BOOL on)
{
    int i;

    for (i = 0; i < kToolbarButtons; i++) {
        gToolCaptioned[i] = on;
    }
    SendMessage(gToolbar, TB_SETMAXTEXTROWS, on ? 2 : 0, 0);
    SendMessage(gToolbar, TB_SETBUTTONWIDTH, 0,
                on ? MAKELPARAM(gLineHeight * 4, gLineHeight * 5)
                   : MAKELPARAM(0, 0));
}

/*
 * The strip's height, taken again whenever the captions come or go: with
 * captions the buttons are two lines of text under an icon, without them an
 * icon, and the strip follows -- as Outlook Express's does when its text
 * labels are turned off -- so a narrow window has no empty band under a row
 * of icons. From the first button (never hidden), whose top is the bar's
 * padding, given again below it; through the rebar when there is one, since
 * only it knows its own band borders; plus the etched edge the window draws
 * round the strip (GazetteWindowPaint).
 */
static void MeasureToolbar(void)
{
    RECT button;
    int  row = 0;

    if (SendMessage(gToolbar, TB_GETITEMRECT, 0, (LPARAM)&button)) {
        row = button.bottom + button.top;
        if (row < button.bottom + 2) {
            row = button.bottom + 2;
        }
    }
    if (row <= 0) {
        row = gLineHeight * 2 + 16 + 12;
    }

    if (gRebar != NULL) {
        REBARBANDINFOA band;
        UINT           bar;

        ZeroMemory(&band, sizeof(band));
        band.cbSize     = sizeof(band);
        band.fMask      = RBBIM_CHILDSIZE;
        band.cxMinChild = 0;
        band.cyMinChild = row;
        SendMessage(gRebar, RB_SETBANDINFOA, 0, (LPARAM)&band);

        bar = (UINT)SendMessage(gRebar, RB_GETBARHEIGHT, 0, 0);
        if (bar > 0) {
            row = (int)bar;
        }
    }
    gToolbarHeight = row + kEtchedEdge * 2;
}

/*
 * A window narrower than its toolbar, in two steps (Bruno's choice,
 * 2026-09-23): captions first, then Outlook Express 5's chevron.
 *
 * With every button one width, a caption dropped on its own buys nothing
 * back, so the captions go together -- Outlook Express's own "no text
 * labels" -- and the row becomes icons. If icons alone still do not fit,
 * buttons hide from the right behind a >> at the end of the row, which
 * drops down a menu of them. OE's chevron is the rebar's and needs
 * comctl32 5.80; this one is a toolbar button and TrackPopupMenu, which
 * Windows 95 has, so every version gets it. The row keeps its height, set
 * with captions on, so the panes never move.
 *
 * Widen the window and it all comes back, in the reverse order.
 */
static void FitToolbar(int limit)
{
    int i;

    gToolLimit   = limit;
    gToolChevron = FALSE;
    for (i = 0; i < kToolbarButtons; i++) {
        gToolHidden[i] = FALSE;
    }
    SetToolCaptions(TRUE);
    RebuildToolbar();

    if (limit > 0 && ToolbarWidth() > limit) {
        SetToolCaptions(FALSE);
        RebuildToolbar();
    }

    if (limit > 0 && ToolbarWidth() > limit) {
        gToolChevron = TRUE;
        RebuildToolbar();
        while (ToolbarWidth() > limit) {
            /* Never the first two: New and Hide Sidebar are always there
               on the Mac, and a row of only a chevron would say nothing. */
            for (i = kToolbarButtons - 1; i >= 2 && gToolHidden[i]; i--) {
            }
            if (i < 2) {
                break;
            }
            gToolHidden[i] = TRUE;
            RebuildToolbar();
        }
    }
    MeasureToolbar();
}

/* The chevron's menu: every hidden button, as the menu item it stands for,
   greyed when the button is. Chosen, it is sent on as the button would
   have sent it. */
static void ShowToolChevron(HWND frame)
{
    HMENU menu = CreatePopupMenu();
    int   count = (int)SendMessage(gToolbar, TB_BUTTONCOUNT, 0, 0);
    RECT  chevron;
    int   chosen;
    int   i;

    if (menu == NULL) {
        return;
    }
    for (i = 0; i < kToolbarButtons; i++) {
        const char *text;

        if (!gToolHidden[i]) {
            continue;
        }
        text = (gToolOther[i] && kToolSpec[i].otherCaption != NULL)
                   ? kToolSpec[i].otherCaption : kToolSpec[i].caption;
        AppendMenuA(menu, MF_STRING | (gToolEnabled[i] ? 0 : MF_GRAYED),
                    (UINT)kToolSpec[i].command, text);
    }

    SetRectEmpty(&chevron);
    SendMessage(gToolbar, TB_GETITEMRECT, (WPARAM)(count - 1),
                (LPARAM)&chevron);
    MapWindowPoints(gToolbar, HWND_DESKTOP, (POINT *)&chevron, 2);

    chosen = (int)TrackPopupMenu(menu,
                                 TPM_LEFTALIGN | TPM_TOPALIGN |
                                 TPM_RETURNCMD | TPM_NONOTIFY,
                                 chevron.left, chevron.bottom, 0, frame,
                                 NULL);
    DestroyMenu(menu);
    if (chosen != 0) {
        PostMessage(frame, WM_COMMAND, MAKEWPARAM(chosen, 0), 0);
    }
}

/*
 * Find: Windows' own Find dialog, the one Notepad opens, from comdlg32 on
 * every Windows 95. It is modeless -- it stays up while the window is
 * used -- so the message loop hands it its keystrokes (GazetteWindowFind
 * Dialog) and it reports through a message Windows names at run time.
 * Up/Down and "Match whole word only" are hidden: Gazette's search filters
 * the headlines, it does not step through them.
 */
static HWND         gFindDialog;
static FINDREPLACEA gFind;
static char         gFindWhat[128];
static UINT         gFindMessage;

static void ShowFind(HWND frame)
{
    if (gFindDialog != NULL) {
        SetFocus(gFindDialog);
        return;
    }
    ZeroMemory(&gFind, sizeof(gFind));
    gFind.lStructSize   = sizeof(gFind);
    gFind.hwndOwner     = frame;
    gFind.lpstrFindWhat = gFindWhat;
    gFind.wFindWhatLen  = sizeof(gFindWhat);
    gFind.Flags         = FR_DOWN | FR_HIDEUPDOWN | FR_HIDEWHOLEWORD;
    gFindDialog = FindTextA(&gFind);
}

HWND GazetteWindowFindDialog(void)
{
    return gFindDialog;
}

UINT GazetteWindowFindMessage(void)
{
    if (gFindMessage == 0) {
        gFindMessage = RegisterWindowMessageA(FINDMSGSTRINGA);
    }
    return gFindMessage;
}

/*
 * What the Find dialog said. Find Next asks for what was typed; the
 * headline filter it drives joins with the store, so until then the
 * status bar says what is being looked for.
 */
void GazetteWindowFindEvent(const FINDREPLACEA *find)
{
    if (find->Flags & FR_DIALOGTERM) {
        gFindDialog = NULL;
        return;
    }
    /* Find Next is the Mac's Return in the search field: the application
       reads what was typed out through GazetteUISearchText. */
    if ((find->Flags & FR_FINDNEXT) && gOnCommand != NULL) {
        gOnCommand(kGazetteCmdSearch);
    }
}

BOOL GazetteWindowCommand(HWND frame, WPARAM wParam, LPARAM lParam)
{
    int id = LOWORD(wParam);

    (void)lParam;
    if (id == IDC_TOOL_CHEVRON) {
        ShowToolChevron(frame);
        return TRUE;
    }
    if (id == IDM_EDIT_FIND) {
        ShowFind(frame);
        return TRUE;
    }
    if (id == IDC_HEADLINES) {
        if (HIWORD(wParam) == LBN_SELCHANGE) {
            HeadlineRowChosen();
        }
        return TRUE;
    }
    return FALSE;
}

/* RBBS_NOGRIPPER is declared from _WIN32_IE 0x0400; comctl32 4.71 knows
   it, and 4.70 draws the gripper instead. */
#ifndef RBBS_NOGRIPPER
#define RBBS_NOGRIPPER 0x00000100
#endif

static BOOL MakeToolbar(HWND parent)
{
    HWND owner = parent;
    int  i;

    /*
     * The band the toolbar sits in: a rebar, which is the raised strip with
     * an edge round it that Outlook Express's and Internet Explorer's
     * toolbars live in. comctl32 4.70 and later; on 4.0 there is no such
     * class and the toolbar sits on the window itself, as it does in every
     * program written for that version.
     */
    gRebar = CreateWindowExA(
        WS_EX_TOOLWINDOW, REBARCLASSNAMEA, NULL,
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN |
        RBS_VARHEIGHT | RBS_BANDBORDERS | CCS_NODIVIDER | CCS_NORESIZE |
        CCS_NOPARENTALIGN,
        0, 0, 0, 0, parent, NULL, gInstance, NULL);
    if (gRebar != NULL) {
        owner = gRebar;
    }

    /*
     * Outlook Express 5's toolbar, which is Bruno's call for the Windows
     * build (2026-09-23): flat buttons, each caption under its icon and
     * wrapped to two lines at most, etched rules between the groups. The
     * icons are his own sixteen-pixel drawings, kept as drawn.
     *
     * TBSTYLE_FLAT arrived with comctl32 4.70; on an original Windows 95
     * with no Internet Explorer ever installed the buttons come out raised
     * instead. An unknown style bit is ignored rather than refused, so that
     * is the version's look, not a failure.
     */
    gToolbar = CreateWindowExA(
        0, TOOLBARCLASSNAMEA, NULL,
        WS_CHILD | WS_VISIBLE | TBSTYLE_TOOLTIPS | TBSTYLE_FLAT |
        CCS_NODIVIDER | CCS_NORESIZE | CCS_NOPARENTALIGN,
        0, 0, 0, 0, owner, (HMENU)IDC_TOOLBAR, gInstance, NULL);

    if (gToolbar == NULL) {
        return FALSE;
    }

    /* Every toolbar talks to comctl32 in a structure whose size grew
       after 4.0; saying which one we compiled against is how a newer
       DLL knows what it was handed. */
    SendMessage(gToolbar, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
    SendMessage(gToolbar, TB_SETIMAGELIST, 0, (LPARAM)gIcons);

    AddToolStrings();
    for (i = 0; i < kToolbarButtons; i++) {
        gToolEnabled[i] = TRUE;
    }
    SetToolCaptions(TRUE);
    RebuildToolbar();

    if (gRebar != NULL) {
        REBARBANDINFOA band;

        ZeroMemory(&band, sizeof(band));
        band.cbSize     = sizeof(band);
        band.fMask      = RBBIM_CHILD | RBBIM_CHILDSIZE | RBBIM_STYLE;
        band.fStyle     = RBBS_CHILDEDGE | RBBS_NOGRIPPER;
        band.hwndChild  = gToolbar;
        band.cxMinChild = 0;
        band.cyMinChild = gLineHeight * 2 + 16 + 12;
        SendMessage(gRebar, RB_INSERTBANDA, (WPARAM)-1, (LPARAM)&band);
    }
    MeasureToolbar();

    return TRUE;
}

static int AddShellIcon(int index)
{
    HICON small = NULL;
    int   at    = -1;

    if (ExtractIconExA("shell32.dll", index, NULL, &small, 1) > 0 &&
        small != NULL) {
        at = ImageList_AddIcon(gIcons, small);
        DestroyIcon(small);
    }
    return at;
}

BOOL GazetteWindowCreate(HWND frame, HINSTANCE instance)
{
    gInstance = instance;
    gFrame    = frame;

    MeasureFonts(frame);

    /*
     * One image list for every icon Gazette drew for itself, cut from
     * the strip in Resources/win/ui_icons.bmp. Magenta is the colour
     * that means nothing, which is what ImageList_AddMasked reads.
     *
     * LoadBitmap and ImageList_AddMasked rather than
     * ImageList_LoadImage: the latter arrived with comctl32 4.70, and
     * everything here has to work against the 4.0 that shipped on the
     * Windows 95 CD.
     */
    {
        HBITMAP strip = LoadBitmapA(instance, MAKEINTRESOURCEA(IDB_UI_ICONS));

        gIcons = ImageList_Create(16, 16, ILC_COLOR24 | ILC_MASK,
                                  kIconCount, 0);
        if (gIcons == NULL || strip == NULL) {
            return FALSE;
        }
        ImageList_AddMasked(gIcons, strip, RGB(255, 0, 255));
        DeleteObject(strip);
    }

    /* And Windows' own: shell32's closed folder (3), open folder (4) and
       blank page (0) -- the plain sheet with its corner turned, not the
       lined one (1) WordPad's documents wear. The same numbers on every
       version from 95 to XP; ExtractIconEx is in the 95 shell. */
    gFolderIcon     = AddShellIcon(3);
    gOpenFolderIcon = AddShellIcon(4);
    gDocIcon        = AddShellIcon(0);

    if (!MakeToolbar(frame)) {
        return FALSE;
    }

    /*
     * The two lists sit in panes that are sunken wells with their header
     * band inside the edge, as a list view's own column header is and as
     * Outlook Express's are -- a header above the well read as part of the
     * window, not of the list. The pane is a small window of Gazette's that
     * lays the two out and passes their messages up to the frame.
     */
    gSidebarPane = CreateWindowExA(WS_EX_CLIENTEDGE, kPaneClass, NULL,
                                   WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
                                   0, 0, 0, 0, frame, NULL, instance, NULL);
    gListPane    = CreateWindowExA(WS_EX_CLIENTEDGE, kPaneClass, NULL,
                                   WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
                                   0, 0, 0, 0, frame, NULL, instance, NULL);
    if (gSidebarPane == NULL || gListPane == NULL) {
        return FALSE;
    }

    gSidebarHeader = MakeHeader(gSidebarPane, IDC_SIDEBAR_HEADER);
    gListHeader    = MakeHeader(gListPane, IDC_LIST_HEADER);
    ApplyFont(gSidebarHeader);
    ApplyFont(gListHeader);
    MeasureHeader();

    SetHeaderItem(gSidebarHeader, 0, "Feeds", kDefaultSidebar,
                  HDF_STRING | HDF_LEFT);
    SetHeaderItem(gListHeader, 0, NULL, kDefaultList, HDF_OWNERDRAW);

    /*
     * The sidebar is a standard tree, dotted lines and all -- Windows' own
     * way of showing groups, as Outlook Express's folder list does. Its
     * edge is the pane's.
     */
    gSidebar = CreateWindowExA(
        0, WC_TREEVIEWA, NULL,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP |
        TVS_HASBUTTONS | TVS_HASLINES | TVS_LINESATROOT | TVS_SHOWSELALWAYS |
        TVS_NOHSCROLL,
        0, 0, 0, 0, gSidebarPane, (HMENU)IDC_SIDEBAR, instance, NULL);

    /*
     * The headline list draws its own rows -- two lines of text with an
     * icon, and a banded date heading between one day and the next --
     * so it is owner-drawn, with no column header over it. The Mac's
     * has no column header either: it is one list of headlines, not a
     * table of fields, and the header it does have is the band above
     * the pane naming the view.
     *
     * A list box rather than a list view: a date band is one line and a
     * headline two, and a list box with LBS_OWNERDRAWVARIABLE asks the
     * height of every item, on every version of Windows. A list view's
     * rows are all one height.
     */
    gHeadlines = CreateWindowExA(
        0, "LISTBOX", NULL,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL |
        LBS_OWNERDRAWVARIABLE | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
        0, 0, 0, 0, gListPane, (HMENU)IDC_HEADLINES, instance, NULL);

    /* The article's pane: a sunken well with no header -- its headline and
       byline are the text's first lines. No scroll bar until there is text
       to scroll, as Outlook Express's preview has none when it is empty. */
    gReader = CreateWindowExA(
        WS_EX_CLIENTEDGE, kReaderClass, NULL,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        0, 0, 0, 0, frame, (HMENU)IDC_READER, instance, NULL);

    gSplitLeft = CreateWindowExA(0, kSplitterClass, NULL,
                                 WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, frame,
                                 (HMENU)IDC_SPLIT_LEFT, instance, NULL);
    gSplitRight = CreateWindowExA(0, kSplitterClass, NULL,
                                  WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, frame,
                                  (HMENU)IDC_SPLIT_RIGHT, instance, NULL);

    gStatus = CreateWindowExA(0, STATUSCLASSNAMEA, NULL,
                              WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
                              0, 0, 0, 0, frame, (HMENU)IDC_STATUSBAR,
                              instance, NULL);

    if (gSidebar == NULL || gHeadlines == NULL || gReader == NULL ||
        gSplitLeft == NULL || gSplitRight == NULL || gStatus == NULL) {
        return FALSE;
    }

    ApplyFont(gSidebar);
    ApplyFont(gHeadlines);
    ApplyFont(gStatus);

    SendMessage(gSidebar, TVM_SETIMAGELIST, TVSIL_NORMAL, (LPARAM)gIcons);

    /* The sidebar's rows come from the preferences, which are read before
       the window opens; so do whether the sidebar and the toolbar are
       showing at all. */
    gSidebarHidden = GazetteCoreHideSidebar() ? TRUE : FALSE;
    gToolbarHidden = GazetteCoreHideToolbar() ? TRUE : FALSE;
    {
        long sidebar, list;

        if (GazetteCoreColumnWidths(&sidebar, &list)) {
            gSidebarWidth = (int)sidebar;
            gListWidth    = (int)list;
        }
    }
    SyncSidebarRows();

    {
        RECT bar;
        GetWindowRect(gStatus, &bar);
        gStatusHeight = bar.bottom - bar.top;
    }
    GazetteWindowSetCount("");

    AdjustToolbarState();
    return TRUE;
}

void GazetteWindowDestroy(void)
{
    if (gIcons != NULL) {
        ImageList_Destroy(gIcons);
        gIcons = NULL;
    }
    if (gBoldFont != NULL && gBoldFont != gUIFont) {
        DeleteObject(gBoldFont);
    }
    gBoldFont = NULL;
    if (gUIFont != NULL &&
        gUIFont != (HFONT)GetStockObject(DEFAULT_GUI_FONT)) {
        DeleteObject(gUIFont);
    }
    gUIFont = NULL;
}

/* ------------------------------------------------------------------ */
/* Toolbar state                                                       */
/*                                                                     */
/* The Mac's AdjustToolbarState, with the same rule: two buttons are    */
/* always available because there is always something to make and       */
/* always a sidebar to show or hide, and everything else depends on     */
/* there being feeds, articles or an article open. With no feeds yet    */
/* that leaves most of the row greyed, which is what the Mac shows in   */
/* the same state.                                                     */
/* ------------------------------------------------------------------ */

/* Returns whether anything moved, so an unchanged row is left alone:
   rebuilding the toolbar is a flash of every button. */
static BOOL SetToolState(int button, BOOL enabled, BOOL other)
{
    BOOL changed = (gToolEnabled[button] != enabled ||
                    gToolOther[button] != other);

    gToolEnabled[button] = enabled;
    gToolOther[button]   = other;
    return changed;
}

static void AdjustToolbarState(void)
{
    const GazetteArticle *open = GazetteFeedsArticleAt(gSelectedArticle);
    int  feedCount    = GazetteCoreFeedCount();
    int  articleCount = GazetteFeedsArticleCount();
    BOOL unread       = (BOOL)(GazetteFeedsUnreadCount() > 0);
    BOOL articleOpen  = (BOOL)(open != NULL);
    BOOL changed      = FALSE;
    static BOOL built;

    if (gToolbar == NULL) {
        return;
    }

    changed |= SetToolState(kTBNew,        TRUE,                     FALSE);
    changed |= SetToolState(kTBSidebar,    TRUE,                     gSidebarHidden);
    changed |= SetToolState(kTBRefresh,    (BOOL)(feedCount > 0),    FALSE);
    changed |= SetToolState(kTBMarkAll,    (BOOL)(articleCount > 0), !unread);
    changed |= SetToolState(kTBHideRead,   TRUE,
                            GazetteCoreHideReadArticles() ? TRUE : FALSE);
    changed |= SetToolState(kTBMarkRead,   articleOpen,
                            (BOOL)(open != NULL && open->read));
    changed |= SetToolState(kTBStar,       articleOpen,
                            (BOOL)(open != NULL && open->starred));
    changed |= SetToolState(kTBNextUnread, unread,                   FALSE);
    changed |= SetToolState(kTBBrowser,
                            (BOOL)(open != NULL && open->link[0] != '\0'),
                            FALSE);
    changed |= SetToolState(kTBFind,       TRUE,                     FALSE);

    if (!changed && built) {
        return;
    }
    built = TRUE;

    /* A caption that changed can change the row's width, so the fit is
       done again rather than the buttons merely re-enabled. */
    if (gToolLimit > 0) {
        FitToolbar(gToolLimit);
    } else {
        RebuildToolbar();
    }
}

/* ------------------------------------------------------------------ */
/* Layout                                                              */
/* ------------------------------------------------------------------ */

static void ClampWidths(int available)
{
    int splitters = gSidebarHidden ? kSplitterWidth : kSplitterWidth * 2;
    int room = available - splitters;

    if (room < 0) {
        room = 0;
    }

    /* The sidebar gives way first and the article last -- the article
       is what the window is for. With the sidebar put away its width is
       left exactly as it was: hiding a column is not resizing it. */
    gShownSidebar = gSidebarWidth;
    if (!gSidebarHidden) {
        int most = room - kMinList - kMinReader;

        if (gShownSidebar > most) {
            gShownSidebar = most;
        }
        if (gShownSidebar < kMinSidebar) {
            gShownSidebar = kMinSidebar;
        }
    }

    gShownList = gListWidth;
    {
        int used = gSidebarHidden ? 0 : gShownSidebar;
        int most = room - used - kMinReader;

        if (gShownList > most) {
            gShownList = most;
        }
        if (gShownList < kMinList) {
            gShownList = kMinList;
        }
    }
}

/* The status bar's two sections, as Outlook Express's are: what the view
   holds on the left, what the network is doing on the right. */
static void LayoutStatus(int width)
{
    int parts[2];

    parts[0] = width - width / 3;
    parts[1] = -1;
    SendMessage(gStatus, SB_SETPARTS, 2, (LPARAM)parts);
}

void GazetteWindowSetCount(const char *text)
{
    if (gStatus != NULL) {
        SendMessage(gStatus, SB_SETTEXTA, 0, (LPARAM)(text ? text : ""));
    }
}

void GazetteWindowSetStatus(const char *text)
{
    if (gStatus != NULL) {
        SendMessage(gStatus, SB_SETTEXTA, 1, (LPARAM)(text ? text : ""));
    }
}

void GazetteWindowLayout(HWND frame)
{
    RECT client;
    HDWP defer;
    int  top, bottom, x;
    int  sidebarWidth, readerWidth;
    HWND bar = (gRebar != NULL) ? gRebar : gToolbar;

    if (gStatus == NULL) {
        return;
    }

    GetClientRect(frame, &client);

    /* Minimized, the client area is nothing, and laying the panes out in
       it would be laying them out for no one. */
    if (IsIconic(frame) || client.right <= 0 || client.bottom <= 0) {
        return;
    }

    SendMessage(gStatus, WM_SIZE, 0, 0);
    LayoutStatus(client.right);
    ShowWindow(bar, gToolbarHidden ? SW_HIDE : SW_SHOW);

    /* The toolbar fits the width the band gives it: captions off, then
       the chevron, as the window narrows -- and its height follows, so
       this comes before anything is placed under it. */
    if (!gToolbarHidden) {
        int room = client.right - kEtchedEdge * 2 -
                   (gRebar != NULL ? kBandMargin : 0);

        if (room != gToolLimit) {
            FitToolbar(room);
        }
    }

    top    = gToolbarHidden ? 0 : gToolbarHeight + kToolbarGap;
    bottom = client.bottom - gStatusHeight;
    if (bottom < top) {
        bottom = top;
    }

    ClampWidths(client.right);

    sidebarWidth = gSidebarHidden ? 0 : gShownSidebar;
    readerWidth  = client.right - sidebarWidth - gShownList -
                   (gSidebarHidden ? kSplitterWidth : kSplitterWidth * 2);
    if (readerWidth < 0) {
        readerWidth = 0;
    }

    defer = BeginDeferWindowPos(8);
    if (defer == NULL) {
        return;
    }

    if (!gToolbarHidden) {
        /* Inside the etched edge the window draws round the strip. */
        defer = DeferWindowPos(defer, bar, NULL, kEtchedEdge, kEtchedEdge,
                               client.right - kEtchedEdge * 2,
                               gToolbarHeight - kEtchedEdge * 2,
                               SWP_NOZORDER);
    }

    x = 0;
    if (!gSidebarHidden) {
        defer = DeferWindowPos(defer, gSidebarPane, NULL, x, top,
                               sidebarWidth, bottom - top, SWP_NOZORDER);
        x += sidebarWidth;
        defer = DeferWindowPos(defer, gSplitLeft, NULL, x, top,
                               kSplitterWidth, bottom - top, SWP_NOZORDER);
        x += kSplitterWidth;
    }

    defer = DeferWindowPos(defer, gListPane, NULL, x, top,
                           gShownList, bottom - top, SWP_NOZORDER);
    x += gShownList;

    defer = DeferWindowPos(defer, gSplitRight, NULL, x, top,
                           kSplitterWidth, bottom - top, SWP_NOZORDER);
    x += kSplitterWidth;

    /* The article column runs from the top of the content region to the
       status bar, level with the tops of the other two panes. */
    defer = DeferWindowPos(defer, gReader, NULL, x, top,
                           readerWidth, bottom - top, SWP_NOZORDER);

    EndDeferWindowPos(defer);

    ShowWindow(gSidebarPane, gSidebarHidden ? SW_HIDE : SW_SHOW);
    ShowWindow(gSplitLeft, gSidebarHidden ? SW_HIDE : SW_SHOW);

    /* The edge is the frame's own paint: redraw it where the strip is. */
    {
        RECT strip;

        SetRect(&strip, 0, 0, client.right, gToolbarHeight + 1);
        InvalidateRect(frame, &strip, TRUE);
    }
}

/*
 * The etched edge round the toolbar strip -- Outlook Express's toolbar
 * sits in a band framed this way, a light line under the menu and a dark
 * one above the panes. A rebar draws that edge only between two bands, and
 * Gazette's has one, so the window draws it: the same EDGE_ETCHED a rebar
 * uses, in the system's colours on every version.
 */
void GazetteWindowPaint(HWND frame, HDC dc)
{
    RECT client, strip;

    if (gToolbarHidden || gToolbar == NULL) {
        return;
    }
    GetClientRect(frame, &client);
    SetRect(&strip, 0, 0, client.right, gToolbarHeight);
    DrawEdge(dc, &strip, EDGE_ETCHED, BF_RECT);
}

/* The columns as the user last set them, for the preferences. */
void GazetteWindowColumnWidths(int *sidebar, int *list)
{
    *sidebar = gSidebarWidth;
    *list    = gListWidth;
}

void GazetteWindowMinimumSize(POINT *minimum)
{
    int frame = GetSystemMetrics(SM_CXSIZEFRAME) * 2;

    minimum->x = kMinSidebar + kMinList + kMinReader +
                 kSplitterWidth * 2 + frame;
    minimum->y = gToolbarHeight + kToolbarGap + gHeaderHeight +
                 gRowHeight * 3 +
                 gStatusHeight + GetSystemMetrics(SM_CYCAPTION) + 8;
}

/* ------------------------------------------------------------------ */
/* View menu                                                           */
/* ------------------------------------------------------------------ */

/* The frame's menu reads these to word Hide / Show. */
BOOL GazetteWindowSidebarHidden(void)
{
    return gSidebarHidden;
}

BOOL GazetteWindowToolbarHidden(void)
{
    return gToolbarHidden;
}

/* ------------------------------------------------------------------ */
/* The headline list's rows                                            */
/*                                                                     */
/* Two lines of text with an icon, the second line running to the end   */
/* of the title with an ellipsis past it -- the Mac's row model. With   */
/* no feeds there is nothing in the list yet, so this draws the shape   */
/* rather than any headline: what is here is what a row is, ready for   */
/* the store.                                                          */
/* ------------------------------------------------------------------ */

void GazetteWindowMeasureItem(MEASUREITEMSTRUCT *measure)
{
    if (measure->CtlID == IDC_HEADLINES) {
        int row = (int)measure->itemData;

        measure->itemHeight = (UINT)((row >= 0 && row < gHeadRowCount &&
                                      gHeadRows[row].kind == kHeadlineDate)
                                         ? gHeadingHeight : gRowHeight);
    }
}

/*
 * A light grey halfway between the face colour and the window's: the date
 * bands and the rules between headlines, and the rule under the reader's
 * byline. The face colour itself read too heavy against white (Bruno,
 * 2026-09-26). Worked out from the scheme, so it follows the user's
 * colours as every other grey here does.
 */
COLORREF GazetteWindowLightTone(void)
{
    COLORREF face   = GetSysColor(COLOR_BTNFACE);
    COLORREF window = GetSysColor(COLOR_WINDOW);

    return RGB((GetRValue(face) + GetRValue(window)) / 2,
               (GetGValue(face) + GetGValue(window)) / 2,
               (GetBValue(face) + GetBValue(window)) / 2);
}

/* The headline band's one item: the view's name with its count straight
   after it, as the Mac's header has them -- the count in grey, and kept
   whole when the name has to be shortened. */
static void DrawListHeader(const DRAWITEMSTRUCT *draw)
{
    RECT  text = draw->rcItem;
    HFONT previous = (HFONT)SelectObject(draw->hDC, gUIFont);
    SIZE  title, count;
    int   gap = 4;

    SetBkMode(draw->hDC, TRANSPARENT);
    InflateRect(&text, -6, 0);

    count.cx = 0;
    if (gListCount[0] != '\0') {
        GetTextExtentPoint32A(draw->hDC, gListCount, lstrlenA(gListCount),
                              &count);
        if (count.cx + gap > text.right - text.left) {
            count.cx = 0;           /* no room for the number at all */
        }
    }
    if (!GetTextExtentPoint32A(draw->hDC, gListTitle, lstrlenA(gListTitle),
                               &title)) {
        title.cx = 0;
    }

    {
        RECT name  = text;
        int  most  = text.right - text.left - (count.cx ? count.cx + gap : 0);

        if (title.cx > most) {
            title.cx = most;
        }
        name.right = name.left + title.cx;
        SetTextColor(draw->hDC, GetSysColor(COLOR_BTNTEXT));
        DrawTextA(draw->hDC, gListTitle, -1, &name,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX |
                  DT_END_ELLIPSIS);

        if (count.cx > 0) {
            RECT number = text;

            number.left = name.right + gap;
            SetTextColor(draw->hDC, GetSysColor(COLOR_GRAYTEXT));
            DrawTextA(draw->hDC, gListCount, -1, &number,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
    }
    SelectObject(draw->hDC, previous);
}

/*
 * How much of a headline goes on its first line: the longest run of whole
 * words that fits, or -- for one word longer than the line -- as many of
 * its letters as do. The rest goes on the second line, which ends in an
 * ellipsis when even that is not enough: the Mac's WrapTitle and
 * DrawTruncated, with the measuring done by GDI.
 */
static int FirstLine(HDC dc, const char *text, int len, int width)
{
    SIZE size;
    int  best = 0;
    int  i;

    for (i = 1; i <= len; i++) {
        if (i < len && text[i] != ' ') {
            continue;
        }
        if (!GetTextExtentPoint32A(dc, text, i, &size) || size.cx > width) {
            break;
        }
        best = i;
    }
    if (best == 0) {
        for (i = 1; i <= len; i++) {
            if (!GetTextExtentPoint32A(dc, text, i, &size) ||
                size.cx > width) {
                break;
            }
            best = i;
        }
    }
    return best;
}

static void DrawDateBand(HDC dc, const RECT *row, int article)
{
    const GazetteArticle *a = GazetteFeedsArticleAt(article);
    RECT                  text = *row;
    char                  label[32];

    /* A shade darker than the rows, as the Mac's band is -- the light
       tone, halfway to the face colour. */
    {
        HBRUSH band = CreateSolidBrush(GazetteWindowLightTone());

        FillRect(dc, row, band);
        DeleteObject(band);
    }
    if (a == NULL) {
        return;
    }
    GazetteRelativeDay(GazetteFeedsLocalTime(a->date), GazetteSysLocalNow(),
                       label, sizeof label);
    SelectObject(dc, gUIFont);
    SetTextColor(dc, GetSysColor(COLOR_GRAYTEXT));
    text.left += kRowIndent + 2;
    DrawTextA(dc, label, -1, &text,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX |
              DT_END_ELLIPSIS);
}

static void DrawHeadline(HDC dc, const RECT *row, int article, BOOL selected)
{
    const GazetteArticle *a = GazetteFeedsArticleAt(article);
    RECT  line;
    int   textLeft, textRight, first, len;
    const char *rest;

    FillRect(dc, row, (HBRUSH)(selected ? COLOR_HIGHLIGHT + 1
                                        : COLOR_WINDOW + 1));

    /* The rule under a headline, the way the Mac's white line parts
       them -- before the text, so a selected row covers it. */
    if (!selected) {
        HPEN pen = CreatePen(PS_SOLID, 1, GazetteWindowLightTone());
        HPEN old = (HPEN)SelectObject(dc, pen);

        MoveToEx(dc, row->left, row->bottom - 1, NULL);
        LineTo(dc, row->right, row->bottom - 1);
        SelectObject(dc, old);
        DeleteObject(pen);
    }
    if (a == NULL) {
        return;
    }

    /* The document on the first line only, the star at that line's end
       when the article has one; the second line lines up under the words
       rather than sliding under the icon. */
    textLeft  = row->left + kRowIndent + 16 + kRowIconGap;
    textRight = row->right - kRowIndent;
    if (gDocIcon >= 0) {
        ImageList_Draw(gIcons, gDocIcon, dc, row->left + kRowIndent,
                       row->top + kHeadlinePad + (gLineHeight - 16) / 2,
                       ILD_TRANSPARENT);
    }
    if (a->starred) {
        textRight -= 16;
        ImageList_Draw(gIcons, kIconStarredArticle, dc, textRight,
                       row->top + kHeadlinePad + (gLineHeight - 16) / 2,
                       ILD_TRANSPARENT);
        textRight -= kRowIconGap;
    }

    /* Bold while unread, plain once opened; always the text colour. */
    SelectObject(dc, a->read ? gUIFont : gBoldFont);
    SetTextColor(dc, GetSysColor(selected ? COLOR_HIGHLIGHTTEXT
                                          : COLOR_WINDOWTEXT));

    len   = lstrlenA(a->title);
    first = FirstLine(dc, a->title, len, textRight - textLeft);

    SetRect(&line, textLeft, row->top + kHeadlinePad, textRight,
            row->top + kHeadlinePad + gLineHeight);
    DrawTextA(dc, a->title, first, &line,
              DT_LEFT | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

    /* The second line runs full width -- the star is the first line's --
       from the next word to the end, and ends in "..." past the edge. */
    rest = a->title + first;
    while (*rest == ' ') {
        rest++;
    }
    if (*rest != '\0') {
        SetRect(&line, textLeft, row->top + kHeadlinePad + gLineHeight,
                row->right - kRowIndent,
                row->top + kHeadlinePad + gLineHeight * 2);
        DrawTextA(dc, rest, -1, &line,
                  DT_LEFT | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    }
}

void GazetteWindowDrawItem(const DRAWITEMSTRUCT *draw)
{
    HFONT previous;
    int   row;

    if (draw->CtlID == IDC_LIST_HEADER) {
        DrawListHeader(draw);
        return;
    }
    if (draw->CtlID != IDC_HEADLINES) {
        return;
    }

    row = (int)draw->itemID;
    if (row < 0 || row >= gHeadRowCount) {
        return;
    }
    /* The dotted focus rectangle is the list box's to toggle; a whole
       redraw puts it back when the list has the focus. */
    if (draw->itemAction == ODA_FOCUS) {
        DrawFocusRect(draw->hDC, &draw->rcItem);
        return;
    }

    previous = (HFONT)SelectObject(draw->hDC, gUIFont);
    SetBkMode(draw->hDC, TRANSPARENT);

    if (gHeadRows[row].kind == kHeadlineDate) {
        DrawDateBand(draw->hDC, &draw->rcItem, gHeadRows[row].article);
    } else {
        DrawHeadline(draw->hDC, &draw->rcItem, gHeadRows[row].article,
                     (BOOL)(gHeadRows[row].article == gSelectedArticle));
        if (draw->itemState & ODS_FOCUS) {
            DrawFocusRect(draw->hDC, &draw->rcItem);
        }
    }

    SelectObject(draw->hDC, previous);
}

BOOL GazetteWindowNotify(HWND frame, NMHDR *header, LRESULT *result)
{
    /* A row chosen in the sidebar -- by the mouse or the arrow keys, which
       is how a tree reports both. */
    if (header->hwndFrom == gSidebar && header->code == TVN_SELCHANGEDA) {
        NM_TREEVIEWA *tree = (NM_TREEVIEWA *)header;

        if (!gSyncingTree && tree->itemNew.hItem != NULL) {
            ChooseRow(RowKind(tree->itemNew.lParam),
                      RowIndex(tree->itemNew.lParam));
        }
        return TRUE;
    }

    /* A group opened or shut: remembered, as the Mac's triangle is. */
    if (header->hwndFrom == gSidebar && header->code == TVN_ITEMEXPANDEDA) {
        NM_TREEVIEWA *tree = (NM_TREEVIEWA *)header;

        if (!gSyncingTree &&
            RowKind(tree->itemNew.lParam) == kGazetteRowGroup) {
            GazetteCoreSetGroupCollapsed(RowIndex(tree->itemNew.lParam),
                                         (tree->action & TVE_COLLAPSE)
                                             ? true : false);
        }
        return TRUE;
    }

    /* A feed or a group that is switched off is drawn grey, as on the Mac.
       Custom draw is comctl32 4.70's; 4.0 never sends it, and there the
       row is drawn as any other. */
    if (header->hwndFrom == gSidebar && header->code == NM_CUSTOMDRAW) {
        NMTVCUSTOMDRAW *draw = (NMTVCUSTOMDRAW *)header;

        if (draw->nmcd.dwDrawStage == CDDS_PREPAINT) {
            *result = CDRF_NOTIFYITEMDRAW;
            return TRUE;
        }
        if (draw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
            int kind  = RowKind(draw->nmcd.lItemlParam);
            int index = RowIndex(draw->nmcd.lItemlParam);
            BOOL off  = (kind == kGazetteRowFeed &&
                         !GazetteCoreFeedEnabled(index)) ||
                        (kind == kGazetteRowGroup &&
                         !GazetteCoreGroupEnabled(index));

            if (off && !(draw->nmcd.uItemState & CDIS_SELECTED)) {
                draw->clrText = GetSysColor(COLOR_GRAYTEXT);
            }
            *result = CDRF_DODEFAULT;
            return TRUE;
        }
        *result = CDRF_DODEFAULT;
        return TRUE;
    }

    /*
     * The bands are titles, not columns: dragging the edge of one would
     * resize an item that has to stay its pane's width, and double-clicking
     * it would do the same. Both are refused. HDN_BEGINTRACK and
     * HDN_DIVIDERDBLCLICK come in A and W forms depending on the parent
     * window, so both codes are answered.
     */
    if ((header->hwndFrom == gSidebarHeader ||
         header->hwndFrom == gListHeader) &&
        (header->code == HDN_BEGINTRACKA || header->code == HDN_BEGINTRACKW ||
         header->code == HDN_DIVIDERDBLCLICKA ||
         header->code == HDN_DIVIDERDBLCLICKW)) {
        if (result != NULL) {
            *result = TRUE;
        }
        return TRUE;
    }

    return FALSE;
}

/* ------------------------------------------------------------------ */
/* The splitters                                                       */
/*                                                                     */
/* Windows has no splitter control, so this is a child window of the    */
/* face colour that tracks the mouse, which is what Explorer's is. The  */
/* drag is live: the Mac's grey ghost line is a Mac OS convention and   */
/* would read here as a stuck redraw.                                   */
/* ------------------------------------------------------------------ */

static LRESULT CALLBACK SplitterProc(HWND hwnd, UINT message,
                                     WPARAM wParam, LPARAM lParam)
{
    switch (message) {
    case WM_SETCURSOR:
        SetCursor(LoadCursor(NULL, IDC_SIZEWE));
        return TRUE;

    case WM_ERASEBKGND: {
        RECT client;
        GetClientRect(hwnd, &client);
        FillRect((HDC)wParam, &client, (HBRUSH)(COLOR_BTNFACE + 1));
        return TRUE;
    }

    case WM_LBUTTONDOWN:
        gDragOffset = (short)LOWORD(lParam);
        SetCapture(hwnd);
        return 0;

    case WM_MOUSEMOVE: {
        POINT pt;
        HWND  parent;
        int   id;

        if (GetCapture() != hwnd) {
            return 0;
        }

        parent = GetParent(hwnd);
        id = GetDlgCtrlID(hwnd);

        pt.x = (short)LOWORD(lParam);
        pt.y = (short)HIWORD(lParam);
        ClientToScreen(hwnd, &pt);
        ScreenToClient(parent, &pt);

        if (id == IDC_SPLIT_LEFT) {
            gSidebarWidth = pt.x - gDragOffset;
        } else {
            int listLeft = gSidebarHidden ? 0
                                          : gShownSidebar + kSplitterWidth;
            gListWidth = pt.x - gDragOffset - listLeft;
        }

        GazetteWindowLayout(parent);

        /* A drag is a choice of what is on screen: what it asked for past
           the limit is not kept. */
        if (id == IDC_SPLIT_LEFT) {
            gSidebarWidth = gShownSidebar;
        } else {
            gListWidth = gShownList;
        }
        UpdateWindow(parent);
        return 0;
    }

    case WM_LBUTTONUP:
        if (GetCapture() == hwnd) {
            ReleaseCapture();
        }
        return 0;
    }

    return DefWindowProc(hwnd, message, wParam, lParam);
}

/* ------------------------------------------------------------------ */
/* The article pane is gazette_win_reader.c's.                         */
/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
/* The panes                                                           */
/*                                                                     */
/* A sunken well holding a header band over a list, the band inside the */
/* edge the way a list view's own column header is. The pane lays the   */
/* two out and passes what they say -- notifications, owner-draw -- up  */
/* to the frame, which is where the rest of the window listens.         */
/* ------------------------------------------------------------------ */

static void RelabelSidebar(void);

static void LayoutPane(HWND pane)
{
    RECT client;
    HWND header = (pane == gSidebarPane) ? gSidebarHeader : gListHeader;
    HWND body   = (pane == gSidebarPane) ? gSidebar : gHeadlines;
    int  width;

    GetClientRect(pane, &client);
    width = client.right;

    if (header != NULL) {
        MoveWindow(header, 0, 0, width, gHeaderHeight, TRUE);
        /* One item, the band's whole width: a title over the list, never
           the first of several columns. */
        SetHeaderItem(header, 0, NULL, width,
                      (pane == gSidebarPane) ? (HDF_STRING | HDF_LEFT)
                                             : HDF_OWNERDRAW);
    }
    if (body != NULL) {
        MoveWindow(body, 0, gHeaderHeight, width,
                   client.bottom - gHeaderHeight, TRUE);
        /* Paint the list afresh, background and all, wherever it moved. */
        InvalidateRect(body, NULL, TRUE);
    }

    /* The headlines wrap to the list's width as they are drawn, so a new
       width is the repaint above and nothing more. The sidebar's names
       are shortened to fit, so a new width is new labels. */
    if (pane == gSidebarPane) {
        static int lastWidth = -1;

        if (width != lastWidth) {
            lastWidth = width;
            RelabelSidebar();
        }
    }
}

static LRESULT CALLBACK PaneProc(HWND hwnd, UINT message,
                                 WPARAM wParam, LPARAM lParam)
{
    switch (message) {
    case WM_SIZE:
        LayoutPane(hwnd);
        return 0;

    case WM_NOTIFY:
    case WM_DRAWITEM:
    case WM_MEASUREITEM:
    case WM_COMMAND:
        return SendMessage(GetParent(hwnd), message, wParam, lParam);
    }
    return DefWindowProc(hwnd, message, wParam, lParam);
}

BOOL GazetteWindowRegisterClasses(HINSTANCE instance)
{
    WNDCLASSA cls;

    ZeroMemory(&cls, sizeof(cls));
    cls.lpfnWndProc   = SplitterProc;
    cls.hInstance     = instance;
    cls.hCursor       = LoadCursor(NULL, IDC_SIZEWE);
    cls.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    cls.lpszClassName = kSplitterClass;

    if (!RegisterClassA(&cls)) {
        return FALSE;
    }

    ZeroMemory(&cls, sizeof(cls));
    cls.style         = CS_HREDRAW | CS_VREDRAW;
    cls.lpfnWndProc   = GazetteWinReaderProc;
    cls.hInstance     = instance;
    cls.hCursor       = LoadCursor(NULL, IDC_ARROW);
    cls.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    cls.lpszClassName = kReaderClass;

    if (!RegisterClassA(&cls)) {
        return FALSE;
    }

    ZeroMemory(&cls, sizeof(cls));
    cls.lpfnWndProc   = PaneProc;
    cls.hInstance     = instance;
    cls.hCursor       = LoadCursor(NULL, IDC_ARROW);
    cls.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    cls.lpszClassName = kPaneClass;

    return RegisterClassA(&cls) != 0;
}

/* ------------------------------------------------------------------ */
/* The sidebar's rows                                                  */
/*                                                                     */
/* A standard tree: the three standing views, then the groups with      */
/* their feeds inside them and the feeds at the top level, in the       */
/* order the preferences keep. Each item carries what it is -- its      */
/* kind and index -- the Mac's GazetteSidebarRow, packed into the       */
/* item's lParam.                                                       */
/* ------------------------------------------------------------------ */

static LPARAM RowParam(int kind, int index)
{
    return (LPARAM)(((kind & 0xFF) << 16) | (index & 0xFFFF));
}

static int RowKind(LPARAM param)
{
    return (int)((param >> 16) & 0xFF);
}

static int RowIndex(LPARAM param)
{
    return (int)(param & 0xFFFF);
}

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
 * How much room a row's words have: the tree's width less what the tree
 * puts before them -- one indent for the lines at the root, one more per
 * level, the icon -- and the few pixels of air it keeps round the text.
 * Zero while the tree has no width yet, which fits everything.
 */
static int LabelRoom(int level)
{
    RECT client;
    int  indent;

    if (gSidebar == NULL || !GetClientRect(gSidebar, &client) ||
        client.right <= 0) {
        return 0;
    }
    indent = (int)SendMessage(gSidebar, TVM_GETINDENT, 0, 0);
    if (indent <= 0) {
        indent = 19;
    }
    return client.right - indent * (level + 1) - 16 - 3 - 10;
}

/*
 * The name and its count, shortened as the Mac's BuildRowLabel shortens
 * them: the count is kept whole and the name gives way, ending in "..."
 * -- so a long feed name never hides how much is unread in it behind a
 * horizontal scroll bar. Measured in the face the tree will draw it in:
 * bold rows are wider.
 */
static void FitLabel(const char *name, const char *count, BOOL bold,
                     int level, char *out, int cap)
{
    int   room = LabelRoom(level);
    int   len  = lstrlenA(name);
    int   countLen = lstrlenA(count);
    HDC   dc;
    HFONT previous;
    SIZE  size;

    if (len > cap - countLen - 4) {
        len = cap - countLen - 4;
    }
    lstrcpynA(out, name, len + 1);
    lstrcatA(out, count);
    if (room <= 0) {
        return;
    }

    dc = GetDC(gSidebar);
    previous = (HFONT)SelectObject(dc, bold ? gBoldFont : gUIFont);
    if (GetTextExtentPoint32A(dc, out, lstrlenA(out), &size) &&
        size.cx > room) {
        while (len > 0) {
            int cut = len;

            len--;
            cut = len;
            while (cut > 0 && name[cut - 1] == ' ') {
                cut--;              /* no "Top ..." with a space before */
            }
            lstrcpynA(out, name, cut + 1);
            lstrcatA(out, "...");
            lstrcatA(out, count);
            if (!GetTextExtentPoint32A(dc, out, lstrlenA(out), &size) ||
                size.cx <= room) {
                break;
            }
        }
    }
    SelectObject(dc, previous);
    ReleaseDC(gSidebar, dc);
}

/*
 * A row's words and weight. Outlook Express's folder list is the model:
 * a folder with something unread in it is bold, with the count after its
 * name in brackets. The standing views carry their count the same way,
 * never bold -- Today's is how many, not how many unread.
 */
static void RowLabel(int kind, int index, char *out, int cap, BOOL *bold)
{
    const char *name  = "";
    int         count = 0;

    *bold = FALSE;
    switch (kind) {
    case kGazetteRowSmart:
        name  = GazetteCoreSmartName(index);
        count = gSmartCount[index];
        break;
    case kGazetteRowGroup:
        name  = GazetteCoreGroupName(index);
        count = GroupUnread(index);
        *bold = (BOOL)(count > 0);
        break;
    default:
        name  = GazetteCoreFeedTitle(index);
        count = FeedUnread(index);
        *bold = (BOOL)(count > 0);
        break;
    }
    {
        char number[16];
        int  level = (kind == kGazetteRowFeed &&
                      GazetteCoreFeedGroup(index) >= 0) ? 1 : 0;

        number[0] = '\0';
        if (count > 0) {
            wsprintfA(number, " (%d)", count);
        }
        FitLabel(name, number, *bold, level, out, cap);
    }
}

static void SetRowItem(HTREEITEM item, int kind, int index)
{
    TV_ITEMA tv;
    char     label[kGazetteTitleLen + 16];
    BOOL     bold;

    RowLabel(kind, index, label, sizeof(label), &bold);
    ZeroMemory(&tv, sizeof(tv));
    tv.mask      = TVIF_TEXT | TVIF_STATE;
    tv.hItem     = item;
    tv.pszText   = label;
    tv.state     = bold ? TVIS_BOLD : 0;
    tv.stateMask = TVIS_BOLD;
    SendMessage(gSidebar, TVM_SETITEMA, 0, (LPARAM)&tv);
}

static HTREEITEM InsertRow(HTREEITEM parent, int kind, int index)
{
    TV_INSERTSTRUCTA insert;
    char             label[kGazetteTitleLen + 16];
    BOOL             bold;
    int              icon, open;

    RowLabel(kind, index, label, sizeof(label), &bold);
    switch (kind) {
    case kGazetteRowSmart:
        icon = open = kIconToday + index;   /* Today, All Unread, Starred */
        break;
    case kGazetteRowGroup:
        icon = gFolderIcon;
        open = (gOpenFolderIcon >= 0) ? gOpenFolderIcon : gFolderIcon;
        break;
    default:
        icon = open = gDocIcon;
        break;
    }

    ZeroMemory(&insert, sizeof(insert));
    insert.hParent        = parent;
    insert.hInsertAfter   = TVI_LAST;
    insert.item.mask      = TVIF_TEXT | TVIF_PARAM | TVIF_STATE |
                            TVIF_IMAGE | TVIF_SELECTEDIMAGE;
    insert.item.pszText   = label;
    insert.item.lParam    = RowParam(kind, index);
    insert.item.state     = bold ? TVIS_BOLD : 0;
    insert.item.stateMask = TVIS_BOLD;
    insert.item.iImage         = (icon >= 0) ? icon : 0;
    insert.item.iSelectedImage = (open >= 0) ? open : 0;
    return (HTREEITEM)SendMessage(gSidebar, TVM_INSERTITEMA, 0,
                                  (LPARAM)&insert);
}

/* The item showing a row, found by walking the tree: a hundred feeds at
   most, so a walk is cheaper than keeping a second index in step. */
static HTREEITEM FindRow(HTREEITEM from, LPARAM param)
{
    HTREEITEM item = from;

    while (item != NULL) {
        TV_ITEMA  tv;
        HTREEITEM child;

        ZeroMemory(&tv, sizeof(tv));
        tv.mask  = TVIF_PARAM;
        tv.hItem = item;
        if (SendMessage(gSidebar, TVM_GETITEMA, 0, (LPARAM)&tv) &&
            tv.lParam == param) {
            return item;
        }
        child = (HTREEITEM)SendMessage(gSidebar, TVM_GETNEXTITEM,
                                       TVGN_CHILD, (LPARAM)item);
        if (child != NULL) {
            HTREEITEM found = FindRow(child, param);

            if (found != NULL) {
                return found;
            }
        }
        item = (HTREEITEM)SendMessage(gSidebar, TVM_GETNEXTITEM, TVGN_NEXT,
                                      (LPARAM)item);
    }
    return NULL;
}

/* Put the tree's highlight on what is selected, without that reading as a
   click. A feed inside a shut group has no item to highlight; it stays the
   selection, as on the Mac. */
static void ShowSelectionInTree(void)
{
    HTREEITEM root, item = NULL;

    if (gSidebar == NULL) {
        return;
    }
    root = (HTREEITEM)SendMessage(gSidebar, TVM_GETNEXTITEM, TVGN_ROOT, 0);
    if (gSelectedSmart >= 0) {
        item = FindRow(root, RowParam(kGazetteRowSmart, gSelectedSmart));
    } else if (gSelectedGroup >= 0) {
        item = FindRow(root, RowParam(kGazetteRowGroup, gSelectedGroup));
    } else if (gSelectedFeed >= 0) {
        item = FindRow(root, RowParam(kGazetteRowFeed, gSelectedFeed));
    }
    gSyncingTree = TRUE;
    SendMessage(gSidebar, TVM_SELECTITEM, TVGN_CARET, (LPARAM)item);
    if (item != NULL) {
        SendMessage(gSidebar, TVM_ENSUREVISIBLE, 0, (LPARAM)item);
        /* Up and down only. TVM_ENSUREVISIBLE also scrolls sideways to
           bring a label wider than the pane into view -- or any label at
           all while the tree is still 0 x 0 at start-up -- which slid the
           icons and the lines off the left edge. The thumb to 0 rather
           than SB_LEFT, which Wine's tree does not answer. */
        SendMessage(gSidebar, WM_HSCROLL, MAKEWPARAM(SB_THUMBPOSITION, 0), 0);
    }
    gSyncingTree = FALSE;
}

/* Every item's words again: the unread counts have moved. */
static void RelabelRows(HTREEITEM item)
{
    while (item != NULL) {
        TV_ITEMA  tv;
        HTREEITEM child;

        ZeroMemory(&tv, sizeof(tv));
        tv.mask  = TVIF_PARAM;
        tv.hItem = item;
        if (SendMessage(gSidebar, TVM_GETITEMA, 0, (LPARAM)&tv)) {
            SetRowItem(item, RowKind(tv.lParam), RowIndex(tv.lParam));
        }
        child = (HTREEITEM)SendMessage(gSidebar, TVM_GETNEXTITEM, TVGN_CHILD,
                                       (LPARAM)item);
        if (child != NULL) {
            RelabelRows(child);
        }
        item = (HTREEITEM)SendMessage(gSidebar, TVM_GETNEXTITEM, TVGN_NEXT,
                                      (LPARAM)item);
    }
}

static void RelabelSidebar(void)
{
    if (gSidebar != NULL) {
        RelabelRows((HTREEITEM)SendMessage(gSidebar, TVM_GETNEXTITEM,
                                           TVGN_ROOT, 0));
    }
}

/* Which rows "Hide Read Feeds" leaves standing: platinum_window.c's
   ApplyFeedVisibility, word for word. */
static void ApplyFeedVisibility(void)
{
    int i;
    int g;

    if (!GazetteCoreHideReadFeeds()) {
        GazetteCoreShowAllRows();
        return;
    }
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

/* The numbers beside the standing views; Today's reads every cache, so it
   is counted here, when the rows are rebuilt, and not on every repaint. */
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

/* The whole tree made again from the preferences: after a refresh, a
   feed added or removed, or Hide Read Feeds. */
static void SyncSidebarRows(void)
{
    static GazetteSidebarRow sequence[kGazetteMaxFeeds + kGazetteMaxGroups];
    HTREEITEM                groupItem[kGazetteMaxGroups];
    const GazettePrefs      *prefs = GazetteCoreGetPrefs();
    int                      count, i;

    ApplyFeedVisibility();
    CountSmartRows();

    if (gSidebar == NULL || prefs == NULL) {
        return;
    }

    /* No WM_SETREDRAW round this: a tree told not to redraw while items
       go in was left laying them out with no room for the icon or the
       lines (seen under Wine, 2026-09-25), and a sidebar is a few dozen
       rows at most. */
    gSyncingTree = TRUE;
    SendMessage(gSidebar, TVM_DELETEITEM, 0, (LPARAM)TVI_ROOT);

    for (i = 0; i < kGazetteSmartCount; i++) {
        (void)InsertRow(TVI_ROOT, kGazetteRowSmart, i);
    }
    for (i = 0; i < kGazetteMaxGroups; i++) {
        groupItem[i] = NULL;
    }

    count = GazetteCoreSequence(sequence);
    for (i = 0; i < count; i++) {
        int index = sequence[i].index;

        if (sequence[i].kind == kGazetteRowGroup) {
            if (index >= 0 && index < prefs->groupCount &&
                !prefs->groups[index].hidden) {
                groupItem[index] = InsertRow(TVI_ROOT, kGazetteRowGroup,
                                             index);
            }
        } else if (index >= 0 && index < prefs->feedCount &&
                   !prefs->feeds[index].hidden) {
            int       group  = GazetteCoreFeedGroup(index);
            HTREEITEM parent = TVI_ROOT;

            if (group >= 0) {
                parent = (group < kGazetteMaxGroups) ? groupItem[group]
                                                     : NULL;
                if (parent == NULL) {
                    continue;       /* its group is hidden */
                }
            }
            (void)InsertRow(parent, kGazetteRowFeed, index);
        }
    }

    /* Open what was open. After the children are in: an item with none
       has nothing to expand. */
    for (i = 0; i < kGazetteMaxGroups; i++) {
        if (groupItem[i] != NULL && !GazetteCoreGroupCollapsed(i)) {
            SendMessage(gSidebar, TVM_EXPAND, TVE_EXPAND,
                        (LPARAM)groupItem[i]);
        }
    }

    gSyncingTree = FALSE;
    ShowSelectionInTree();
    InvalidateRect(gSidebar, NULL, TRUE);
}

/* Do what a click on this row would do: platinum_window.c's ChooseRow. */
static void ChooseRow(int kind, int index)
{
    if (kind == kGazetteRowSmart) {
        if (index == gSelectedSmart) {
            return;
        }
        gSelectedSmart = index;
        gSelectedGroup = -1;
        if (gOnSmartChosen != NULL) {
            gOnSmartChosen(index);
        }
        return;
    }

    if (kind == kGazetteRowGroup) {
        if (index == gSelectedGroup && gSelectedSmart < 0) {
            return;
        }
        gSelectedGroup = index;
        gSelectedSmart = -1;
        if (gOnGroupChosen != NULL) {
            gOnGroupChosen(index);
        }
        return;
    }

    if (index != gSelectedFeed || gSelectedGroup >= 0 ||
        gSelectedSmart >= 0) {
        gSelectedFeed  = index;
        gSelectedGroup = -1;
        gSelectedSmart = -1;
        if (gOnFeedChosen != NULL) {
            gOnFeedChosen(index);
        }
    }
}

/* ------------------------------------------------------------------ */
/* The headline list                                                   */
/* ------------------------------------------------------------------ */

static void BuildHeadlineRows(void)
{
    long last  = 0;
    int  have  = 0;
    int  count = GazetteFeedsArticleCount();
    int  room  = (int)(sizeof gHeadRows / sizeof gHeadRows[0]);
    int  i;

    gHeadRowCount = 0;
    for (i = 0; i < count && gHeadRowCount + 2 <= room; i++) {
        const GazetteArticle *a = GazetteFeedsArticleAt(i);
        long                  day;

        if (a == NULL) {
            break;
        }
        /* The day the reader's clock would call it, as on the Mac. */
        day = GazetteDayNumber(GazetteFeedsLocalTime(a->date));
        if (!have || day != last) {
            gHeadRows[gHeadRowCount].kind    = kHeadlineDate;
            gHeadRows[gHeadRowCount].article = i;
            gHeadRowCount++;
            last = day;
            have = 1;
        }
        gHeadRows[gHeadRowCount].kind    = kHeadlineArticle;
        gHeadRows[gHeadRowCount].article = i;
        gHeadRowCount++;
    }
}

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

static int ArticleAtRow(int row)
{
    if (row < 0 || row >= gHeadRowCount ||
        gHeadRows[row].kind == kHeadlineDate) {
        return -1;
    }
    return gHeadRows[row].article;
}

/* The list box holds a row number per item and nothing else; each item's
   height is asked for as it goes in (WM_MEASUREITEM). */
static void FillHeadlines(void)
{
    int i;

    if (gHeadlines == NULL) {
        return;
    }
    SendMessage(gHeadlines, WM_SETREDRAW, FALSE, 0);
    SendMessage(gHeadlines, LB_RESETCONTENT, 0, 0);
    for (i = 0; i < gHeadRowCount; i++) {
        SendMessage(gHeadlines, LB_ADDSTRING, 0, (LPARAM)i);
    }
    SendMessage(gHeadlines, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(gHeadlines, NULL, TRUE);
}

static void ShowArticleRow(BOOL reveal)
{
    int row = RowForArticle(gSelectedArticle);

    if (gHeadlines == NULL) {
        return;
    }
    SendMessage(gHeadlines, LB_SETCURSEL, (WPARAM)row, 0);
    if (!reveal && row >= 0) {
        /* A new list opens at its top, whatever is selected in it. */
        SendMessage(gHeadlines, LB_SETTOPINDEX, 0, 0);
    }
    InvalidateRect(gHeadlines, NULL, FALSE);
}

/*
 * The headline band: the view's name, and how many
 * are unread -- or, while a search is on, how many matched. The Mac's
 * GazetteUIUpdate works these out the same way.
 */
static void UpdateHeader(void)
{
    char        header[kGazetteFeedTitleLen + 96];
    char        count[48];
    const char *title = GazetteFeedsTitle();

    if (title[0] == '\0') {
        title = GazetteCoreFeedTitle(gSelectedFeed);
    }
    count[0] = '\0';
    if (GazetteFeedsTotalCount() > 0) {
        int unread = GazetteFeedsUnreadCount();

        if (GazetteFeedsFilter()[0] != '\0') {
            wsprintfA(header, "%s - \"%s\"", title, GazetteFeedsFilter());
            wsprintfA(count, "(%d)", GazetteFeedsArticleCount());
        } else if (unread > 0) {
            lstrcpynA(header, title, sizeof(header));
            wsprintfA(count, "(%d unread)", unread);
        } else {
            lstrcpynA(header, title, sizeof(header));
            wsprintfA(count, "(%d)", GazetteFeedsTotalCount());
        }
    } else {
        lstrcpynA(header, title, sizeof(header));
    }
    SetListTitle(header, count);

    /* And the status bar's left-hand section: what the view holds. */
    if (GazetteFeedsTotalCount() > 0) {
        char holds[64];

        wsprintfA(holds, "%d articles, %d unread",
                  GazetteFeedsArticleCount(), GazetteFeedsUnreadCount());
        GazetteWindowSetCount(holds);
    } else {
        GazetteWindowSetCount("");
    }
}

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

    /* Opening it is what makes it read. */
    if (gSelectedArticle >= 0) {
        GazetteFeedsMarkRead(gSelectedArticle, 1);
    }
    ShowArticleRow(TRUE);

    /* The shell first, then the text: SelectArticle's order on the Mac,
       so a page on its way is waited for rather than the summary laid
       out and replaced. */
    if (gSelectedArticle >= 0 && gOnArticleChosen != NULL) {
        gOnArticleChosen(gSelectedArticle);
    }
    GazetteWinReaderCompose(gSelectedArticle);
    GazetteUIUpdate();
}

/*
 * The list box moved its selection. A date band is not a place to stop:
 * coming down onto one goes on to the headline under it, and coming up
 * onto one goes on to the headline over it.
 */
static void HeadlineRowChosen(void)
{
    int row     = (int)SendMessage(gHeadlines, LB_GETCURSEL, 0, 0);
    int article = ArticleAtRow(row);

    if (article < 0 && row >= 0) {
        int was = RowForArticle(gSelectedArticle);

        article = ArticleAtRow((was > row && row > 0) ? row - 1 : row + 1);
        if (article < 0) {
            article = gSelectedArticle;
        }
    }
    if (article < 0) {
        return;
    }
    if (article == gSelectedArticle) {
        ShowArticleRow(TRUE);
        return;
    }
    SelectArticle(article);
}

/* ------------------------------------------------------------------ */
/* app/gazette_ui.h                                                    */
/* ------------------------------------------------------------------ */

void GazetteWindowSetCallbacks(GazetteUIFeedChosen onFeedChosen,
                               GazetteUIArticleChosen onArticleChosen,
                               GazetteUIGroupChosen onGroupChosen,
                               GazetteUISmartChosen onSmartChosen,
                               GazetteUICommandChosen onCommand)
{
    gOnFeedChosen    = onFeedChosen;
    gOnArticleChosen = onArticleChosen;
    gOnGroupChosen   = onGroupChosen;
    gOnSmartChosen   = onSmartChosen;
    gOnCommand       = onCommand;
}

static char gStatusText[256];

void GazetteUISetStatus(const char *text)
{
    if (text == NULL) {
        text = "";
    }
    if (strcmp(gStatusText, text) == 0) {
        return;
    }
    lstrcpynA(gStatusText, text, sizeof(gStatusText));
    GazetteWindowSetStatus(gStatusText);
}

void GazetteUIUpdate(void)
{
    if (gFrame == NULL) {
        return;
    }
    UpdateHeader();
    RelabelSidebar();
    AdjustToolbarState();
    if (gHeadlines != NULL) {
        InvalidateRect(gHeadlines, NULL, FALSE);
    }
}

static char gKeepLink[kGazetteArticleLinkLen];
static int  gKeepOffset;

void GazetteUIKeepPlace(void)
{
    const GazetteArticle *a = GazetteFeedsArticleAt(gSelectedArticle);

    gKeepLink[0] = '\0';
    gKeepOffset  = 0;
    if (a != NULL && a->link[0] != '\0') {
        lstrcpynA(gKeepLink, a->link, sizeof(gKeepLink));
        gKeepOffset = GazetteWinReaderOffset();
    }
}

void GazetteUIArticlesChanged(void)
{
    int kept = -1;

    if (gFrame == NULL) {
        return;
    }

    if (gKeepLink[0] != '\0') {
        int i;

        for (i = 0; i < GazetteFeedsArticleCount(); i++) {
            const GazetteArticle *a = GazetteFeedsArticleAt(i);

            if (a != NULL && strcmp(a->link, gKeepLink) == 0) {
                kept = i;
                break;
            }
        }
        gKeepLink[0] = '\0';
    }

    if (kept >= 0) {
        gSelectedArticle = kept;
    } else {
        gSelectedArticle = (GazetteFeedsArticleCount() > 0) ? 0 : -1;
    }

    SyncSidebarRows();
    BuildHeadlineRows();
    FillHeadlines();
    ShowArticleRow((BOOL)(kept >= 0));

    if (gSelectedArticle >= 0) {
        GazetteFeedsMarkRead(gSelectedArticle, 1);
    }
    if (gSelectedArticle >= 0 && gOnArticleChosen != NULL) {
        gOnArticleChosen(gSelectedArticle);
    }

    GazetteWinReaderCompose(gSelectedArticle);
    if (kept >= 0 && gKeepOffset > 0) {
        GazetteWinReaderScrollTo(gKeepOffset);
    }
    gKeepOffset = 0;
    GazetteUIUpdate();
}

void GazetteUIArticleTextChanged(void)
{
    GazetteWinReaderCompose(gSelectedArticle);
}

/* Windows draws no photographs yet (TASKS.md W2 step 4); when it does, a
   picture landing recomposes the article where the reader has it. */
void GazetteUIPhotosChanged(void)
{
    int was;

    if (GazettePhotosArticle() != gSelectedArticle) {
        return;
    }
    was = GazetteWinReaderOffset();
    GazetteWinReaderCompose(gSelectedArticle);
    GazetteWinReaderScrollTo(was);
}

void GazetteUIViewChanged(void)
{
    char                  link[kGazetteArticleLinkLen];
    const GazetteArticle *was = GazetteFeedsArticleAt(gSelectedArticle);

    if (gFrame == NULL) {
        return;
    }
    link[0] = '\0';
    if (was != NULL) {
        lstrcpynA(link, was->link, sizeof(link));
    }

    GazetteFeedsRebuildView();

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

    /* Hide Sidebar and Hide Toolbar are View menu moves too. */
    gSidebarHidden = GazetteCoreHideSidebar() ? TRUE : FALSE;
    gToolbarHidden = GazetteCoreHideToolbar() ? TRUE : FALSE;

    SyncSidebarRows();
    BuildHeadlineRows();
    FillHeadlines();
    ShowArticleRow(TRUE);
    GazetteWindowLayout(gFrame);
    GazetteWinReaderCompose(gSelectedArticle);
    GazetteUIUpdate();
}

void GazetteUIFeedsChanged(void)
{
    if (gFrame == NULL) {
        return;
    }
    if (gSelectedFeed >= GazetteCoreFeedCount()) {
        gSelectedFeed = 0;
    }
    if (gSelectedGroup >= GazetteCoreGroupCount()) {
        gSelectedGroup = -1;
    }
    SyncSidebarRows();
    GazetteUIUpdate();
}

int GazetteUISelectedArticle(void)
{
    return gSelectedArticle;
}

const char *GazetteUISelectedArticleLink(void)
{
    const GazetteArticle *a = GazetteFeedsArticleAt(gSelectedArticle);

    return (a != NULL) ? a->link : "";
}

Boolean GazetteUINextUnread(void)
{
    int count = GazetteFeedsArticleCount();
    int i;

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

int GazetteUISelectedFeed(void)
{
    return gSelectedFeed;
}

void GazetteUISelectFeed(int index)
{
    if (index < 0 || index >= GazetteCoreFeedCount()) {
        return;
    }
    gSelectedFeed  = index;
    gSelectedGroup = -1;
    gSelectedSmart = -1;
    ShowSelectionInTree();
}

Boolean GazetteUISelection(int *kind, int *index)
{
    if (kind == NULL || index == NULL || gSelectedSmart >= 0) {
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
    gSelectedSmart = -1;
    ShowSelectionInTree();
}

void GazetteUISelectSmart(int which)
{
    if (which < 0 || which >= kGazetteSmartCount) {
        return;
    }
    ChooseRow(kGazetteRowSmart, which);
    ShowSelectionInTree();
}

/* Windows has no search field: the Find dialog's text stands in for it,
   so Find Next and the application read the same words. */
void GazetteUISearchText(char *out, size_t cap)
{
    if (out == NULL || cap == 0) {
        return;
    }
    lstrcpynA(out, gFindWhat, (int)cap);
}

void GazetteUISetSearchText(const char *text)
{
    lstrcpynA(gFindWhat, (text != NULL) ? text : "", sizeof(gFindWhat));
}
