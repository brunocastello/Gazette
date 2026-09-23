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
#include <string.h>

#include "gazette_win.h"
#include "gazette_win_res.h"

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

    kHeadlinePad    = 3,        /* air above and below a headline block */
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
static HIMAGELIST gRowSpacer;    /* nothing but a height, see below */
static HFONT      gUIFont;

static int  gSidebarWidth = kDefaultSidebar;
static int  gListWidth    = kDefaultList;
static BOOL gSidebarHidden;
static BOOL gToolbarHidden;

static int  gLineHeight;         /* one line of the interface font */
static int  gRowHeight;          /* a headline block: two lines and its air */
static int  gHeaderHeight;
static int  gStatusHeight;
static int  gToolbarHeight;
static int  gDragOffset;

static LRESULT CALLBACK SplitterProc(HWND, UINT, WPARAM, LPARAM);
static LRESULT CALLBACK ReaderProc(HWND, UINT, WPARAM, LPARAM);
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
    if (find->Flags & FR_FINDNEXT) {
        char status[192];

        wsprintfA(status, "Looking for \"%s\"", find->lpstrFindWhat);
        GazetteWindowSetStatus(status);
    }
}

BOOL GazetteWindowCommand(HWND frame, int id)
{
    if (id == IDC_TOOL_CHEVRON) {
        ShowToolChevron(frame);
        return TRUE;
    }
    if (id == IDM_EDIT_FIND) {
        ShowFind(frame);
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

static void AddSmartView(const char *name, int icon)
{
    TV_INSERTSTRUCTA item;

    ZeroMemory(&item, sizeof(item));
    item.hParent      = TVI_ROOT;
    item.hInsertAfter = TVI_LAST;
    item.item.mask    = TVIF_TEXT | TVIF_IMAGE | TVIF_SELECTEDIMAGE;
    item.item.pszText = (char *)name;
    item.item.iImage  = icon;
    item.item.iSelectedImage = icon;

    SendMessage(gSidebar, TVM_INSERTITEMA, 0, (LPARAM)&item);
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
        TVS_HASBUTTONS | TVS_HASLINES | TVS_LINESATROOT | TVS_SHOWSELALWAYS,
        0, 0, 0, 0, gSidebarPane, (HMENU)IDC_SIDEBAR, instance, NULL);

    /*
     * The headline list draws its own rows -- two lines of text with an
     * icon, and a banded date heading between one day and the next --
     * so it is owner-drawn, with no column header over it. The Mac's
     * has no column header either: it is one list of headlines, not a
     * table of fields, and the header it does have is the band above
     * the pane naming the view.
     */
    gHeadlines = CreateWindowExA(
        0, WC_LISTVIEWA, NULL,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP |
        LVS_REPORT | LVS_NOCOLUMNHEADER | LVS_OWNERDRAWFIXED |
        LVS_SINGLESEL | LVS_SHOWSELALWAYS,
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

    /*
     * A list view in report mode takes its row height from its own
     * image list and not from WM_MEASUREITEM, whatever the
     * documentation says about owner-drawn lists. An image list of the
     * right height with nothing in it is the way to ask for a taller
     * row on every version of comctl32 there is.
     */
    gRowSpacer = ImageList_Create(1, gRowHeight, ILC_COLOR, 1, 1);
    SendMessage(gHeadlines, LVM_SETIMAGELIST, LVSIL_SMALL,
                (LPARAM)gRowSpacer);

    /* One column, the width of the pane: the rows are drawn here, so
       the column is only what the list measures its rows against. */
    {
        LV_COLUMNA column;

        ZeroMemory(&column, sizeof(column));
        column.mask = LVCF_WIDTH;
        column.cx   = kDefaultList;
        SendMessage(gHeadlines, LVM_INSERTCOLUMNA, 0, (LPARAM)&column);
    }

    /* The three standing views, above the feed list, as on the Mac --
       each with the icon Gazette drew for it. */
    AddSmartView("Today", kIconToday);
    AddSmartView("All Unread", kIconAllUnread);
    AddSmartView("Starred", kIconStarred);

    /* Today is the view the window opens on, so it is the row selected --
       the band over the headlines says "Today" and the sidebar agrees. */
    {
        HTREEITEM today = (HTREEITEM)SendMessage(gSidebar, TVM_GETNEXTITEM,
                                                 TVGN_ROOT, 0);
        if (today != NULL) {
            SendMessage(gSidebar, TVM_SELECTITEM, TVGN_CARET, (LPARAM)today);
        }
    }

    {
        RECT bar;
        GetWindowRect(gStatus, &bar);
        gStatusHeight = bar.bottom - bar.top;
    }
    GazetteWindowSetCount("0 article(s)");

    AdjustToolbarState();
    return TRUE;
}

void GazetteWindowDestroy(void)
{
    if (gIcons != NULL) {
        ImageList_Destroy(gIcons);
        gIcons = NULL;
    }
    if (gRowSpacer != NULL) {
        ImageList_Destroy(gRowSpacer);
        gRowSpacer = NULL;
    }
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

static void SetToolState(int button, BOOL enabled, BOOL other)
{
    gToolEnabled[button] = enabled;
    gToolOther[button]   = other;
}

static void AdjustToolbarState(void)
{
    /*
     * Feeds, articles and the open article are the engine's business
     * and the engine is not wired to this shell yet, so the counts are
     * all zero here. Written as the Mac writes it, with the conditions
     * in place, so that wiring the store up is a matter of answering
     * these three questions rather than of rewriting the function.
     */
    int  feedCount    = 0;
    int  articleCount = 0;
    BOOL unread       = FALSE;
    BOOL articleOpen  = FALSE;

    if (gToolbar == NULL) {
        return;
    }

    SetToolState(kTBNew,        TRUE,                     FALSE);
    SetToolState(kTBSidebar,    TRUE,                     gSidebarHidden);
    SetToolState(kTBRefresh,    (BOOL)(feedCount > 0),    FALSE);
    SetToolState(kTBMarkAll,    (BOOL)(articleCount > 0), !unread);
    SetToolState(kTBHideRead,   TRUE,                     FALSE);
    SetToolState(kTBMarkRead,   articleOpen,              FALSE);
    SetToolState(kTBStar,       articleOpen,              FALSE);
    SetToolState(kTBNextUnread, unread,                   FALSE);
    SetToolState(kTBBrowser,    articleOpen,              FALSE);
    SetToolState(kTBFind,       TRUE,                     FALSE);

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
    if (!gSidebarHidden) {
        int most = room - kMinList - kMinReader;

        if (gSidebarWidth > most) {
            gSidebarWidth = most;
        }
        if (gSidebarWidth < kMinSidebar) {
            gSidebarWidth = kMinSidebar;
        }
    }

    {
        int used = gSidebarHidden ? 0 : gSidebarWidth;
        int most = room - used - kMinReader;

        if (gListWidth > most) {
            gListWidth = most;
        }
        if (gListWidth < kMinList) {
            gListWidth = kMinList;
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

    sidebarWidth = gSidebarHidden ? 0 : gSidebarWidth;
    readerWidth  = client.right - sidebarWidth - gListWidth -
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
                           gListWidth, bottom - top, SWP_NOZORDER);
    x += gListWidth;

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

void GazetteWindowToggleSidebar(HWND frame)
{
    gSidebarHidden = !gSidebarHidden;
    AdjustToolbarState();
    GazetteWindowLayout(frame);
}

void GazetteWindowToggleToolbar(HWND frame)
{
    gToolbarHidden = !gToolbarHidden;
    GazetteWindowLayout(frame);
}

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
        measure->itemHeight = (UINT)gRowHeight;
    }
}

/* The headline band's one item: the view's name, and its count at the
   right-hand end. The control has drawn the band; this is the words. */
static void DrawListHeader(const DRAWITEMSTRUCT *draw)
{
    RECT  text = draw->rcItem;
    HFONT previous = (HFONT)SelectObject(draw->hDC, gUIFont);

    SetBkMode(draw->hDC, TRANSPARENT);
    SetTextColor(draw->hDC, GetSysColor(COLOR_BTNTEXT));
    InflateRect(&text, -6, 0);

    if (gListCount[0] != '\0') {
        RECT count = text;
        SIZE extent;

        DrawTextA(draw->hDC, gListCount, -1, &count,
                  DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        if (GetTextExtentPoint32A(draw->hDC, gListCount,
                                  lstrlenA(gListCount), &extent)) {
            text.right -= extent.cx + 8;
        }
    }
    DrawTextA(draw->hDC, gListTitle, -1, &text,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX |
              DT_END_ELLIPSIS);
    SelectObject(draw->hDC, previous);
}

void GazetteWindowDrawItem(const DRAWITEMSTRUCT *draw)
{
    RECT  row;
    HFONT previous;
    BOOL  selected;

    if (draw->CtlID == IDC_LIST_HEADER) {
        DrawListHeader(draw);
        return;
    }
    if (draw->CtlID != IDC_HEADLINES || draw->itemID == (UINT)-1) {
        return;
    }

    row = draw->rcItem;
    selected = (draw->itemState & ODS_SELECTED) != 0;

    FillRect(draw->hDC, &row,
             (HBRUSH)(selected ? COLOR_HIGHLIGHT + 1 : COLOR_WINDOW + 1));

    previous = (HFONT)SelectObject(draw->hDC, gUIFont);
    SetBkMode(draw->hDC, TRANSPARENT);
    SetTextColor(draw->hDC,
                 GetSysColor(selected ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT));

    /* The rule between one headline and the next, the way the sidebar's
       white line separates the Mac's. */
    {
        HPEN pen = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_BTNFACE));
        HPEN old = (HPEN)SelectObject(draw->hDC, pen);

        MoveToEx(draw->hDC, row.left, row.bottom - 1, NULL);
        LineTo(draw->hDC, row.right, row.bottom - 1);
        SelectObject(draw->hDC, old);
        DeleteObject(pen);
    }

    SelectObject(draw->hDC, previous);
}

BOOL GazetteWindowNotify(HWND frame, NMHDR *header, LRESULT *result)
{

    /*
     * Choosing a standing view renames the band over the headline list,
     * as choosing one does on the Mac. It is the only thing the sidebar
     * can say yet.
     */
    if (header->hwndFrom == gSidebar && header->code == TVN_SELCHANGEDA) {
        NM_TREEVIEWA *tree = (NM_TREEVIEWA *)header;
        TV_ITEMA item;
        char     name[64];

        ZeroMemory(&item, sizeof(item));
        item.mask       = TVIF_TEXT;
        item.hItem      = tree->itemNew.hItem;
        item.pszText    = name;
        item.cchTextMax = sizeof(name);

        if (SendMessage(gSidebar, TVM_GETITEMA, 0, (LPARAM)&item)) {
            char title[128];

            SetListTitle(name, NULL);
            /* "Today - Gazette", as Outlook Express's is "Inbox -
               Outlook Express": the view, then the program. */
            wsprintfA(title, "%s - Gazette", name);
            SetWindowTextA(frame, title);
        }
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
                                          : gSidebarWidth + kSplitterWidth;
            gListWidth = pt.x - gDragOffset - listLeft;
        }

        GazetteWindowLayout(parent);
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
/* The article pane                                                    */
/*                                                                     */
/* Its own class from the start rather than an edit control: what goes  */
/* here is the Mac reader's laid-out text with its photographs, which   */
/* no stock Windows control draws. Empty until the store is ported.     */
/* ------------------------------------------------------------------ */

static LRESULT CALLBACK ReaderProc(HWND hwnd, UINT message,
                                   WPARAM wParam, LPARAM lParam)
{
    switch (message) {
    case WM_ERASEBKGND: {
        RECT client;
        GetClientRect(hwnd, &client);
        FillRect((HDC)wParam, &client, (HBRUSH)(COLOR_WINDOW + 1));
        return TRUE;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        return 0;
    }
    }

    return DefWindowProc(hwnd, message, wParam, lParam);
}

/* ------------------------------------------------------------------ */
/* The panes                                                           */
/*                                                                     */
/* A sunken well holding a header band over a list, the band inside the */
/* edge the way a list view's own column header is. The pane lays the   */
/* two out and passes what they say -- notifications, owner-draw -- up  */
/* to the frame, which is where the rest of the window listens.         */
/* ------------------------------------------------------------------ */

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
    }

    /* The headline list's one column is exactly as wide as the list's
       client area, so there is never a horizontal scroll bar under it. */
    if (body == gHeadlines && gHeadlines != NULL) {
        RECT list;

        GetClientRect(gHeadlines, &list);
        SendMessage(gHeadlines, LVM_SETCOLUMNWIDTH, 0,
                    MAKELPARAM(list.right, 0));
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
    cls.lpfnWndProc   = ReaderProc;
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
