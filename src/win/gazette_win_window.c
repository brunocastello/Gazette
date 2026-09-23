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
#include <string.h>

#include "gazette_win.h"
#include "gazette_win_res.h"

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

    kSearchWidth    = 104,      /* the field, not counting its frame */
    kSearchGap      = 6         /* toolbar's right-hand air */
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
    { IDM_ARTICLE_BROWSER,  kIconBrowser,      -1,                "Open in Browser",    NULL,                 FALSE }
};

/* ------------------------------------------------------------------ */
/* State                                                               */
/* ------------------------------------------------------------------ */

static const char kSplitterClass[] = "GazetteSplitter";
static const char kReaderClass[]   = "GazetteReader";

static HINSTANCE  gInstance;
static HWND       gFrame;
static HWND       gToolbar;
static HWND       gSearch;
static HWND       gFindIcon;     /* the glass beside the field */
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
/* the same two bands. The headline list's carries a count at its       */
/* right-hand end, so that one has two items.                          */
/* ------------------------------------------------------------------ */

static HWND MakeHeader(HWND parent, int id)
{
    return CreateWindowExA(0, WC_HEADERA, NULL,
                           WS_CHILD | WS_VISIBLE | HDS_HORZ,
                           0, 0, 0, 0, parent, (HMENU)(INT_PTR)id,
                           gInstance, NULL);
}

static void SetHeaderItem(HWND header, int index, const char *text,
                          int width, BOOL rightAligned)
{
    HD_ITEMA item;

    ZeroMemory(&item, sizeof(item));
    item.mask    = HDI_TEXT | HDI_WIDTH | HDI_FORMAT;
    item.pszText = (char *)text;
    item.cxy     = width;
    item.fmt     = HDF_STRING | (rightAligned ? HDF_RIGHT : HDF_LEFT);

    if (SendMessage(header, HDM_GETITEMA, (WPARAM)index, (LPARAM)&item) == 0 &&
        SendMessage(header, HDM_GETITEMCOUNT, 0, 0) <= index) {
        SendMessage(header, HDM_INSERTITEMA, (WPARAM)index, (LPARAM)&item);
    } else {
        SendMessage(header, HDM_SETITEMA, (WPARAM)index, (LPARAM)&item);
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

static BOOL MakeToolbar(HWND parent)
{
    TBBUTTON buttons[kToolbarButtons + 4];
    int count = 0;
    int i;

    /*
     * TBSTYLE_LIST puts the caption beside the icon rather than under
     * it, and TBSTYLE_FLAT makes a button flat until the mouse is over
     * it -- which is the Mac toolbar's behaviour exactly, and Outlook
     * Express's before it. Both arrived with comctl32 4.70, so on an
     * original Windows 95 with no Internet Explorer ever installed the
     * bar comes out raised with its captions underneath. That is the
     * one place this window looks its age, and it is the version's, not
     * ours: an unknown style bit is ignored rather than refused.
     */
    gToolbar = CreateWindowExA(
        0, TOOLBARCLASSNAMEA, NULL,
        WS_CHILD | WS_VISIBLE | TBSTYLE_TOOLTIPS | TBSTYLE_FLAT |
        TBSTYLE_LIST | CCS_NODIVIDER | CCS_NORESIZE | CCS_NOPARENTALIGN,
        0, 0, 0, 0, parent, (HMENU)IDC_TOOLBAR, gInstance, NULL);

    if (gToolbar == NULL) {
        return FALSE;
    }

    /* Every toolbar talks to comctl32 in a structure whose size grew
       after 4.0; saying which one we compiled against is how a newer
       DLL knows what it was handed. */
    SendMessage(gToolbar, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
    SendMessage(gToolbar, TB_SETIMAGELIST, 0, (LPARAM)gIcons);

    ZeroMemory(buttons, sizeof(buttons));
    for (i = 0; i < kToolbarButtons; i++) {
        if (kToolSpec[i].group) {
            buttons[count].iBitmap   = 6;       /* the separator's width */
            buttons[count].fsStyle   = TBSTYLE_SEP;
            buttons[count].idCommand = 0;
            count++;
        }
        buttons[count].iBitmap   = kToolSpec[i].icon;
        buttons[count].idCommand = kToolSpec[i].command;
        buttons[count].fsState   = TBSTATE_ENABLED;
        buttons[count].fsStyle   = TBSTYLE_BUTTON;
        buttons[count].iString   = (INT_PTR)kToolSpec[i].caption;
        count++;
    }

    SendMessage(gToolbar, TB_ADDBUTTONS, (WPARAM)count, (LPARAM)buttons);
    SendMessage(gToolbar, TB_AUTOSIZE, 0, 0);

    {
        RECT bar;
        GetWindowRect(gToolbar, &bar);
        gToolbarHeight = bar.bottom - bar.top;
    }

    /*
     * The search field and the glass beside it, at the right-hand end,
     * the way the Mac has them: a small field rather than another
     * button's worth of the row. They are children of the frame rather
     * than of the toolbar so that the toolbar's own layout never moves
     * them.
     */
    gFindIcon = CreateWindowExA(0, "STATIC", NULL,
                                WS_CHILD | WS_VISIBLE | SS_ICON | SS_CENTERIMAGE,
                                0, 0, 16, 16, parent,
                                (HMENU)IDC_FINDICON, gInstance, NULL);
    if (gFindIcon != NULL) {
        HICON glass = ImageList_GetIcon(gIcons, kIconFind, ILD_TRANSPARENT);
        if (glass != NULL) {
            SendMessage(gFindIcon, STM_SETICON, (WPARAM)glass, 0);
        }
    }

    gSearch = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", NULL,
                              WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                              ES_AUTOHSCROLL,
                              0, 0, 0, 0, parent, (HMENU)IDC_SEARCH,
                              gInstance, NULL);
    ApplyFont(gSearch);

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

    gSidebarHeader = MakeHeader(frame, IDC_SIDEBAR_HEADER);
    gListHeader    = MakeHeader(frame, IDC_LIST_HEADER);
    ApplyFont(gSidebarHeader);
    ApplyFont(gListHeader);
    MeasureHeader();

    SetHeaderItem(gSidebarHeader, 0, "Feeds", 100, FALSE);
    SetHeaderItem(gListHeader, 0, "Today", 100, FALSE);
    SetHeaderItem(gListHeader, 1, "", 60, TRUE);

    gSidebar = CreateWindowExA(
        0, WC_TREEVIEWA, NULL,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER |
        TVS_HASBUTTONS | TVS_HASLINES | TVS_LINESATROOT | TVS_SHOWSELALWAYS,
        0, 0, 0, 0, frame, (HMENU)IDC_SIDEBAR, instance, NULL);

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
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER |
        LVS_REPORT | LVS_NOCOLUMNHEADER | LVS_OWNERDRAWFIXED |
        LVS_SINGLESEL | LVS_SHOWSELALWAYS,
        0, 0, 0, 0, frame, (HMENU)IDC_HEADLINES, instance, NULL);

    gReader = CreateWindowExA(
        0, kReaderClass, NULL,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER | WS_VSCROLL,
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

    {
        RECT bar;
        GetWindowRect(gStatus, &bar);
        gStatusHeight = bar.bottom - bar.top;
    }

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

static void SetToolState(int button, BOOL enabled, const char *caption,
                         int icon)
{
    TBBUTTONINFOA info;

    if (gToolbar == NULL) {
        return;
    }

    ZeroMemory(&info, sizeof(info));
    info.cbSize  = sizeof(info);
    info.dwMask  = TBIF_STATE | TBIF_IMAGE | TBIF_TEXT;
    info.fsState = (BYTE)(enabled ? TBSTATE_ENABLED : 0);
    info.iImage  = icon;
    info.pszText = (char *)caption;

    /* TBIF_TEXT and TB_SETBUTTONINFO are comctl32 4.71. Where they are
       missing the caption simply stays as it was created, which is the
       first of each toggle's two -- wrong only while the state it names
       is not the state it is in, and never wrong about what it does. */
    SendMessage(gToolbar, TB_SETBUTTONINFOA, (WPARAM)kToolSpec[button].command,
                (LPARAM)&info);
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

    SetToolState(kTBNew, TRUE, kToolSpec[kTBNew].caption,
                 kToolSpec[kTBNew].icon);
    SetToolState(kTBSidebar, TRUE,
                 gSidebarHidden ? kToolSpec[kTBSidebar].otherCaption
                                : kToolSpec[kTBSidebar].caption,
                 kToolSpec[kTBSidebar].icon);
    SetToolState(kTBRefresh, (BOOL)(feedCount > 0),
                 kToolSpec[kTBRefresh].caption, kToolSpec[kTBRefresh].icon);
    SetToolState(kTBMarkAll, (BOOL)(articleCount > 0),
                 unread ? kToolSpec[kTBMarkAll].caption
                        : kToolSpec[kTBMarkAll].otherCaption,
                 unread ? kToolSpec[kTBMarkAll].icon
                        : kToolSpec[kTBMarkAll].otherIcon);
    SetToolState(kTBHideRead, TRUE, kToolSpec[kTBHideRead].caption,
                 kToolSpec[kTBHideRead].icon);
    SetToolState(kTBMarkRead, articleOpen, kToolSpec[kTBMarkRead].caption,
                 kToolSpec[kTBMarkRead].icon);
    SetToolState(kTBStar, articleOpen, kToolSpec[kTBStar].caption,
                 kToolSpec[kTBStar].icon);
    SetToolState(kTBNextUnread, unread, kToolSpec[kTBNextUnread].caption,
                 kToolSpec[kTBNextUnread].icon);
    SetToolState(kTBBrowser, articleOpen, kToolSpec[kTBBrowser].caption,
                 kToolSpec[kTBBrowser].icon);

    EnableWindow(gSearch, feedCount > 0);
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

void GazetteWindowLayout(HWND frame)
{
    RECT client;
    HDWP defer;
    int top, bottom, height, x;
    int sidebarWidth, readerWidth;

    if (gStatus == NULL) {
        return;
    }

    GetClientRect(frame, &client);

    SendMessage(gStatus, WM_SIZE, 0, 0);
    ShowWindow(gToolbar, gToolbarHidden ? SW_HIDE : SW_SHOW);
    ShowWindow(gSearch, gToolbarHidden ? SW_HIDE : SW_SHOW);
    ShowWindow(gFindIcon, gToolbarHidden ? SW_HIDE : SW_SHOW);

    top    = gToolbarHidden ? 0 : gToolbarHeight;
    bottom = client.bottom - gStatusHeight;
    height = bottom - top - gHeaderHeight;
    if (height < 0) {
        height = 0;
    }

    ClampWidths(client.right);

    sidebarWidth = gSidebarHidden ? 0 : gSidebarWidth;
    readerWidth  = client.right - sidebarWidth - gListWidth -
                   (gSidebarHidden ? kSplitterWidth : kSplitterWidth * 2);
    if (readerWidth < 0) {
        readerWidth = 0;
    }

    defer = BeginDeferWindowPos(10);
    if (defer == NULL) {
        return;
    }

    if (!gToolbarHidden) {
        int searchRight = client.right - kSearchGap;
        int searchLeft  = searchRight - kSearchWidth;
        int fieldHeight = gLineHeight + 6;
        int fieldTop    = (gToolbarHeight - fieldHeight) / 2;

        defer = DeferWindowPos(defer, gToolbar, NULL, 0, 0,
                               searchLeft - 16 - kSearchGap * 2,
                               gToolbarHeight, SWP_NOZORDER);
        defer = DeferWindowPos(defer, gFindIcon, NULL,
                               searchLeft - 16 - kSearchGap,
                               (gToolbarHeight - 16) / 2, 16, 16,
                               SWP_NOZORDER);
        defer = DeferWindowPos(defer, gSearch, NULL, searchLeft, fieldTop,
                               kSearchWidth, fieldHeight, SWP_NOZORDER);
    }

    x = 0;
    if (!gSidebarHidden) {
        defer = DeferWindowPos(defer, gSidebarHeader, NULL, x, top,
                               sidebarWidth, gHeaderHeight, SWP_NOZORDER);
        defer = DeferWindowPos(defer, gSidebar, NULL, x, top + gHeaderHeight,
                               sidebarWidth, height, SWP_NOZORDER);
        x += sidebarWidth;
        defer = DeferWindowPos(defer, gSplitLeft, NULL, x, top,
                               kSplitterWidth, bottom - top, SWP_NOZORDER);
        x += kSplitterWidth;
    }

    defer = DeferWindowPos(defer, gListHeader, NULL, x, top,
                           gListWidth, gHeaderHeight, SWP_NOZORDER);
    defer = DeferWindowPos(defer, gHeadlines, NULL, x, top + gHeaderHeight,
                           gListWidth, height, SWP_NOZORDER);
    x += gListWidth;

    defer = DeferWindowPos(defer, gSplitRight, NULL, x, top,
                           kSplitterWidth, bottom - top, SWP_NOZORDER);
    x += kSplitterWidth;

    /* The article column has no header: it runs from the top of the
       content region to the status strip, level with where the other
       two columns' headers begin. */
    defer = DeferWindowPos(defer, gReader, NULL, x, top,
                           readerWidth, bottom - top, SWP_NOZORDER);

    EndDeferWindowPos(defer);

    ShowWindow(gSidebar, gSidebarHidden ? SW_HIDE : SW_SHOW);
    ShowWindow(gSidebarHeader, gSidebarHidden ? SW_HIDE : SW_SHOW);
    ShowWindow(gSplitLeft, gSidebarHidden ? SW_HIDE : SW_SHOW);

    /* The header's two items divide the band: the view's name takes
       what the count does not. */
    SetHeaderItem(gListHeader, 1, "", 60, TRUE);
    SetHeaderItem(gListHeader, 0, NULL, gListWidth - 60, FALSE);

    SendMessage(gHeadlines, LVM_SETCOLUMNWIDTH, 0, MAKELPARAM(gListWidth, 0));
}

void GazetteWindowMinimumSize(POINT *minimum)
{
    int frame = GetSystemMetrics(SM_CXSIZEFRAME) * 2;

    minimum->x = kMinSidebar + kMinList + kMinReader +
                 kSplitterWidth * 2 + frame;
    minimum->y = gToolbarHeight + gHeaderHeight + gRowHeight * 3 +
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

void GazetteWindowDrawItem(const DRAWITEMSTRUCT *draw)
{
    RECT  row;
    HFONT previous;
    BOOL  selected;

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
    (void)frame;
    (void)result;

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
            SetHeaderItem(gListHeader, 0, name, gListWidth - 60, FALSE);
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
        HDC dc = BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        return 0;
    }
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

    return RegisterClassA(&cls) != 0;
}
