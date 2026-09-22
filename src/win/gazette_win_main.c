/*
 * gazette_win_main.c - the Windows shell: WinMain, the frame window, the
 * menu bar and the three-pane layout.
 *
 * This is the Windows counterpart of src/main.cpp and src/ui/
 * platinum_window.c, and it is deliberately a different program rather
 * than a port of those two files. The Mac window draws its panes, its
 * grooves and its headers by hand because Mac OS 9 has no control that
 * does the job; Windows does, so the sidebar is a tree view, the headline
 * list is a report-mode list view and the strip along the bottom is a
 * status bar. The layout, the pane order and the widths are Gazette's;
 * the pixels inside each pane belong to the system.
 *
 * Targets: Windows 95, 98, Me, NT 4.0, 2000 and XP, as one ANSI binary.
 *
 *   - ANSI, never Unicode. 95/98/Me implement only the A entry points.
 *   - WINVER 0x0400. Nothing later than NT 4.0 / 95 may be called.
 *   - NT 3.51 and earlier are out of reach: the tree and list views
 *     arrived with the common controls in 95 and NT 4.0. Drawing them
 *     by hand instead would be the Mac window again, which is the one
 *     thing this shell is not supposed to be.
 *   - comctl32.dll is loaded by hand rather than imported. The import
 *     that matters, InitCommonControlsEx, only exists from comctl32
 *     4.70 (Internet Explorer 3), and a static import of a missing
 *     symbol stops the program at load time on a Windows 95 that never
 *     had IE installed -- before any code of ours runs to say why.
 */

#define WINVER       0x0400
#define _WIN32_WINNT 0x0400
#define _WIN32_IE    0x0300

#include <windows.h>
#include <commctrl.h>

#include "gazette_win_res.h"
#include "gazette_version.h"

/* ------------------------------------------------------------------ */
/* Layout constants                                                    */
/*                                                                     */
/* The minimums are the Mac window's (kMinSidebar / kMinList /          */
/* kMinReader in platinum_window.c), rounded to what a Windows control  */
/* needs: a tree view has to keep its expand buttons and a line of      */
/* text, and a list view its first column and its scroll bar.          */
/* ------------------------------------------------------------------ */

#define kSplitterWidth   4      /* Explorer's is 4 pixels wide, so is this */
#define kMinSidebar     96
#define kMinList       180
#define kMinReader     220
#define kDefaultSidebar 168
#define kDefaultList    280

/* The frame opens at this size unless the screen is too small for it.
   640x480 is the floor worth caring about -- it is what a period machine
   and a fresh 86Box profile both come up in. */
#define kDefaultWidth   760
#define kDefaultHeight  540

static const char kMainClass[]     = "GazetteMainWindow";
static const char kSplitterClass[] = "GazetteSplitter";
static const char kReaderClass[]   = "GazetteReader";

static HINSTANCE gInstance;
static HWND      gMainWindow;
static HWND      gSidebar;          /* SysTreeView32 -- groups and feeds  */
static HWND      gHeadlines;        /* SysListView32 -- the headline list */
static HWND      gReader;           /* our own class -- the article       */
static HWND      gSplitLeft;        /* sidebar | headlines                */
static HWND      gSplitRight;       /* headlines | article                */
static HWND      gStatusBar;
static HFONT     gUIFont;

static int  gSidebarWidth = kDefaultSidebar;
static int  gListWidth    = kDefaultList;
static BOOL gSidebarHidden;

/* Set while a splitter has the mouse captured: the distance from the
   pointer to the splitter's own left edge, so the bar does not jump to
   centre itself under the cursor on the first move. */
static int gDragOffset;

static LRESULT CALLBACK MainWndProc(HWND, UINT, WPARAM, LPARAM);
static LRESULT CALLBACK SplitterWndProc(HWND, UINT, WPARAM, LPARAM);
static LRESULT CALLBACK ReaderWndProc(HWND, UINT, WPARAM, LPARAM);

/* ------------------------------------------------------------------ */
/* The common controls                                                 */
/* ------------------------------------------------------------------ */

typedef BOOL (WINAPI *InitCommonControlsExProc)(const INITCOMMONCONTROLSEX *);
typedef void (WINAPI *InitCommonControlsProc)(void);

/*
 * Bring comctl32 in and register the classes this shell asks for.
 *
 * Under the version 6 assembly the manifest names -- which is to say, on
 * XP -- InitCommonControlsEx is not optional: the classes are registered
 * by that call and by nothing else. Before 4.70 the entry point does not
 * exist at all and the DLL registers everything as it loads, so the old
 * InitCommonControls is both sufficient and all there is. Asking the DLL
 * which one it has is the only way to be right on both.
 */
static BOOL InitControls(void)
{
    HMODULE comctl;
    InitCommonControlsExProc initEx;
    InitCommonControlsProc init;
    INITCOMMONCONTROLSEX icc;

    comctl = LoadLibraryA("comctl32.dll");
    if (comctl == NULL) {
        return FALSE;
    }

    initEx = (InitCommonControlsExProc)
             GetProcAddress(comctl, "InitCommonControlsEx");
    if (initEx != NULL) {
        icc.dwSize = sizeof(icc);
        icc.dwICC  = ICC_TREEVIEW_CLASSES | ICC_LISTVIEW_CLASSES |
                     ICC_BAR_CLASSES;
        if (initEx(&icc)) {
            return TRUE;
        }
        /* A version 6 comctl32 that refuses is a real failure; an older
           one may simply not know a flag, and its classes are already
           registered. Fall through either way and let the window
           creation be the thing that reports it. */
    }

    /* The old entry point is fetched by hand as well, and for the same
       reason: writing InitCommonControls() as a call puts the symbol in
       the import table, which is precisely the load-time dependency on
       comctl32 this function exists to avoid. */
    init = (InitCommonControlsProc)
           GetProcAddress(comctl, "InitCommonControls");
    if (init == NULL) {
        return FALSE;
    }

    init();
    return TRUE;
}

/* ------------------------------------------------------------------ */
/* The interface font                                                  */
/*                                                                     */
/* MS Sans Serif on 95 and NT 4, Tahoma on XP -- and whatever the user  */
/* chose, if they changed it. Asking the system is the only way to get  */
/* all three. DEFAULT_GUI_FONT is the fallback, and on a machine where  */
/* SystemParametersInfo fails it is already the right answer.           */
/* ------------------------------------------------------------------ */

static HFONT CreateUIFont(void)
{
    NONCLIENTMETRICSA metrics;

    ZeroMemory(&metrics, sizeof(metrics));
    metrics.cbSize = sizeof(metrics);

    if (SystemParametersInfoA(SPI_GETNONCLIENTMETRICS, sizeof(metrics),
                              &metrics, 0)) {
        HFONT font = CreateFontIndirectA(&metrics.lfMessageFont);
        if (font != NULL) {
            return font;
        }
    }

    return (HFONT)GetStockObject(DEFAULT_GUI_FONT);
}

static void ApplyFont(HWND control)
{
    if (control != NULL && gUIFont != NULL) {
        SendMessage(control, WM_SETFONT, (WPARAM)gUIFont, MAKELPARAM(TRUE, 0));
    }
}

/* ------------------------------------------------------------------ */
/* Layout                                                              */
/*                                                                     */
/* One function places every pane and both splitters, and it is the     */
/* only place that decides a width: a drag sets gSidebarWidth or        */
/* gListWidth and calls this, exactly as TrackDivider only sets a       */
/* number on the Mac side.                                             */
/* ------------------------------------------------------------------ */

static void ClampWidths(int available)
{
    int splitters = gSidebarHidden ? kSplitterWidth : kSplitterWidth * 2;
    int room = available - splitters;

    if (room < 0) {
        room = 0;
    }

    if (gSidebarHidden) {
        /* The sidebar keeps its width while it is away, so that showing
           it again restores the window the user had rather than a
           default. Only the list is clamped here. */
        if (gListWidth > room - kMinReader) {
            gListWidth = room - kMinReader;
        }
        if (gListWidth < kMinList) {
            gListWidth = kMinList;
        }
        return;
    }

    if (gSidebarWidth < kMinSidebar) {
        gSidebarWidth = kMinSidebar;
    }
    if (gListWidth < kMinList) {
        gListWidth = kMinList;
    }

    /* Too little room: the article pane gives way first, then the
       headline list, and the sidebar last -- the same order of
       precedence the Mac window's Layout uses. */
    if (gSidebarWidth + gListWidth + kMinReader > room) {
        gListWidth = room - gSidebarWidth - kMinReader;
        if (gListWidth < kMinList) {
            gListWidth = kMinList;
            gSidebarWidth = room - gListWidth - kMinReader;
            if (gSidebarWidth < kMinSidebar) {
                gSidebarWidth = kMinSidebar;
            }
        }
    }
}

/*
 * The headline list's first column takes whatever the other two leave.
 * A report-mode list view does not do this itself, and a fixed first
 * column is the one thing that makes a resized Windows list look broken.
 */
static void SizeHeadlineColumns(int paneWidth)
{
    int fixed = 110 + 90;               /* From, Date */
    int scrollbar = GetSystemMetrics(SM_CXVSCROLL);
    int headline = paneWidth - fixed - scrollbar - 4;

    if (headline < 80) {
        headline = 80;
    }

    SendMessage(gHeadlines, LVM_SETCOLUMNWIDTH, 0, MAKELPARAM(headline, 0));
}

static void LayoutPanes(HWND hwnd)
{
    RECT client, statusRect;
    HDWP defer;
    int top, bottom, height, x;
    int sidebarWidth, readerWidth, statusHeight;

    if (gStatusBar == NULL || gHeadlines == NULL) {
        return;                         /* still being built */
    }

    GetClientRect(hwnd, &client);

    /* The status bar sizes and places itself; everything else is laid
       out in the space it leaves. */
    SendMessage(gStatusBar, WM_SIZE, 0, 0);
    GetWindowRect(gStatusBar, &statusRect);
    statusHeight = statusRect.bottom - statusRect.top;

    top    = 0;
    bottom = client.bottom - statusHeight;
    height = bottom - top;
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

    defer = BeginDeferWindowPos(5);
    if (defer == NULL) {
        return;
    }

    x = 0;
    if (!gSidebarHidden) {
        defer = DeferWindowPos(defer, gSidebar, NULL, x, top,
                               sidebarWidth, height, SWP_NOZORDER);
        x += sidebarWidth;
        defer = DeferWindowPos(defer, gSplitLeft, NULL, x, top,
                               kSplitterWidth, height, SWP_NOZORDER);
        x += kSplitterWidth;
    }

    defer = DeferWindowPos(defer, gHeadlines, NULL, x, top,
                           gListWidth, height, SWP_NOZORDER);
    x += gListWidth;

    defer = DeferWindowPos(defer, gSplitRight, NULL, x, top,
                           kSplitterWidth, height, SWP_NOZORDER);
    x += kSplitterWidth;

    defer = DeferWindowPos(defer, gReader, NULL, x, top,
                           readerWidth, height, SWP_NOZORDER);

    EndDeferWindowPos(defer);

    /* Hiding is a separate call from placing: a window given a width of
       zero still draws its border. */
    ShowWindow(gSidebar,   gSidebarHidden ? SW_HIDE : SW_SHOW);
    ShowWindow(gSplitLeft, gSidebarHidden ? SW_HIDE : SW_SHOW);

    SizeHeadlineColumns(gListWidth);

    /* The status bar divides itself the same way the window does: the
       message runs the width of the window, and the count sits in a
       fixed part at the right-hand end. */
    {
        int parts[2];

        parts[0] = client.right - 120;
        if (parts[0] < 0) {
            parts[0] = 0;
        }
        parts[1] = -1;
        SendMessage(gStatusBar, SB_SETPARTS, 2, (LPARAM)parts);
    }
}

/* ------------------------------------------------------------------ */
/* The panes                                                           */
/* ------------------------------------------------------------------ */

static void AddSmartView(HWND tree, const char *name)
{
    TV_INSERTSTRUCTA item;

    ZeroMemory(&item, sizeof(item));
    item.hParent      = TVI_ROOT;
    item.hInsertAfter = TVI_LAST;
    item.item.mask    = TVIF_TEXT;
    item.item.pszText = (char *)name;

    SendMessage(tree, TVM_INSERTITEMA, 0, (LPARAM)&item);
}

static void AddColumn(HWND list, int index, const char *title, int width)
{
    LV_COLUMNA column;

    ZeroMemory(&column, sizeof(column));
    column.mask     = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    column.pszText  = (char *)title;
    column.cx       = width;
    column.iSubItem = index;

    SendMessage(list, LVM_INSERTCOLUMNA, (WPARAM)index, (LPARAM)&column);
}

static BOOL CreatePanes(HWND parent)
{
    /* WS_EX_CLIENTEDGE on each pane is what gives the window its Windows
       reading: three sunken wells with the frame's face between them.
       Under the manifest XP draws that edge themed instead of 3D, which
       is the whole point of asking for version 6. */
    gSidebar = CreateWindowExA(
        WS_EX_CLIENTEDGE, WC_TREEVIEWA, NULL,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP |
        TVS_HASBUTTONS | TVS_HASLINES | TVS_LINESATROOT | TVS_SHOWSELALWAYS,
        0, 0, 0, 0, parent, (HMENU)IDC_SIDEBAR, gInstance, NULL);

    gHeadlines = CreateWindowExA(
        WS_EX_CLIENTEDGE, WC_LISTVIEWA, NULL,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP |
        LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
        0, 0, 0, 0, parent, (HMENU)IDC_HEADLINES, gInstance, NULL);

    gReader = CreateWindowExA(
        WS_EX_CLIENTEDGE, kReaderClass, NULL,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL,
        0, 0, 0, 0, parent, (HMENU)IDC_READER, gInstance, NULL);

    gSplitLeft = CreateWindowExA(
        0, kSplitterClass, NULL, WS_CHILD | WS_VISIBLE,
        0, 0, 0, 0, parent, (HMENU)IDC_SPLIT_LEFT, gInstance, NULL);

    gSplitRight = CreateWindowExA(
        0, kSplitterClass, NULL, WS_CHILD | WS_VISIBLE,
        0, 0, 0, 0, parent, (HMENU)IDC_SPLIT_RIGHT, gInstance, NULL);

    gStatusBar = CreateWindowExA(
        0, STATUSCLASSNAMEA, NULL,
        WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
        0, 0, 0, 0, parent, (HMENU)IDC_STATUSBAR, gInstance, NULL);

    if (gSidebar == NULL || gHeadlines == NULL || gReader == NULL ||
        gSplitLeft == NULL || gSplitRight == NULL || gStatusBar == NULL) {
        return FALSE;
    }

    ApplyFont(gSidebar);
    ApplyFont(gHeadlines);
    ApplyFont(gReader);
    ApplyFont(gStatusBar);

    /* Full-row selection is comctl32 4.70 and later. An older DLL does
       not recognise the message and answers zero, which is exactly the
       behaviour wanted: the list simply keeps the 95 selection. */
    SendMessage(gHeadlines, LVM_SETEXTENDEDLISTVIEWSTYLE, 0,
                LVS_EX_FULLROWSELECT);

    AddColumn(gHeadlines, 0, "Headline", 280);
    AddColumn(gHeadlines, 1, "From",     110);
    AddColumn(gHeadlines, 2, "Date",      90);

    /* The three standing views are not feeds and are not placeholder
       content: they are rows the sidebar always has, as they are on the
       Mac side. The feed list below them is empty until the store and
       the preferences are ported. */
    AddSmartView(gSidebar, "Today");
    AddSmartView(gSidebar, "All Unread");
    AddSmartView(gSidebar, "Starred");

    SendMessage(gStatusBar, SB_SETTEXTA, 0, (LPARAM)"Ready");

    return TRUE;
}

/* ------------------------------------------------------------------ */
/* The splitters                                                       */
/*                                                                     */
/* Windows has no splitter control, so this is a child window of the    */
/* frame's own face colour that tracks the mouse -- which is what       */
/* Explorer's is. The drag is live, as Explorer's is too; the Mac       */
/* window's grey ghost line is a Mac OS convention and would read as a  */
/* stuck redraw here.                                                   */
/* ------------------------------------------------------------------ */

static LRESULT CALLBACK SplitterWndProc(HWND hwnd, UINT message,
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
        /* lParam is client-relative to the splitter, so this is how far
           into the bar the pointer went down -- the distance to keep
           between the two for the rest of the drag. */
        gDragOffset = (short)LOWORD(lParam);
        SetCapture(hwnd);
        return 0;

    case WM_MOUSEMOVE: {
        POINT pt;
        HWND parent;
        int id;

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
            int listLeft = (gSidebarHidden ? 0 : gSidebarWidth + kSplitterWidth);
            gListWidth = pt.x - gDragOffset - listLeft;
        }

        LayoutPanes(parent);
        UpdateWindow(parent);
        return 0;
    }

    case WM_LBUTTONUP:
        if (GetCapture() == hwnd) {
            ReleaseCapture();
        }
        return 0;

    case WM_CAPTURECHANGED:
        return 0;
    }

    return DefWindowProc(hwnd, message, wParam, lParam);
}

/* ------------------------------------------------------------------ */
/* The article pane                                                    */
/*                                                                     */
/* Its own class from the start, rather than a static or an edit        */
/* control: what goes here is the Mac reader's laid-out text with its   */
/* photographs, which no stock Windows control draws. For now it is an  */
/* empty well that says so.                                             */
/* ------------------------------------------------------------------ */

static LRESULT CALLBACK ReaderWndProc(HWND hwnd, UINT message,
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
        RECT client;
        HDC dc;
        HFONT previous;

        dc = BeginPaint(hwnd, &ps);
        GetClientRect(hwnd, &client);

        previous = (HFONT)SelectObject(dc, gUIFont);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, GetSysColor(COLOR_GRAYTEXT));
        DrawTextA(dc, "No article selected", -1, &client,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(dc, previous);

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        InvalidateRect(hwnd, NULL, TRUE);
        return 0;
    }

    return DefWindowProc(hwnd, message, wParam, lParam);
}

/* ------------------------------------------------------------------ */
/* Menu commands                                                       */
/* ------------------------------------------------------------------ */

static void ToggleSidebar(HWND hwnd)
{
    HMENU menu = GetMenu(hwnd);

    gSidebarHidden = !gSidebarHidden;

    /* The item says what it would do, as the Mac's does -- not a check
       mark beside the noun. The wording is part of the interface, and
       this shell is the same interface in Windows' controls. */
    ModifyMenuA(menu, IDM_VIEW_SIDEBAR, MF_BYCOMMAND | MF_STRING,
                IDM_VIEW_SIDEBAR,
                gSidebarHidden ? "Show &Sidebar\tCtrl+S"
                               : "Hide &Sidebar\tCtrl+S");
    DrawMenuBar(hwnd);

    LayoutPanes(hwnd);
}

static void ShowAbout(HWND hwnd)
{
    /* A message box, not a dialog, until the application icon is cut for
       Windows: the About box is where that icon is seen largest, and
       there is nothing to put in it yet. */
    MessageBoxA(hwnd,
                "Gazette " GAZETTE_VERSION_STRING "\r\n"
                "RSS / Atom reader\r\n\r\n"
                "Copyright (c) 2026 brunocastello",
                "About Gazette",
                MB_OK | MB_ICONINFORMATION);
}

/* ------------------------------------------------------------------ */
/* The frame window                                                    */
/* ------------------------------------------------------------------ */

static LRESULT CALLBACK MainWndProc(HWND hwnd, UINT message,
                                    WPARAM wParam, LPARAM lParam)
{
    switch (message) {
    case WM_CREATE:
        gMainWindow = hwnd;
        if (!CreatePanes(hwnd)) {
            return -1;
        }
        return 0;

    case WM_SIZE:
        LayoutPanes(hwnd);
        return 0;

    case WM_GETMINMAXINFO: {
        /* Below this the panes would be fighting over their minimums on
           every drag. Refusing the size is cheaper than clamping it. */
        MINMAXINFO *mmi = (MINMAXINFO *)lParam;
        int frame = GetSystemMetrics(SM_CXSIZEFRAME) * 2;

        mmi->ptMinTrackSize.x = kMinSidebar + kMinList + kMinReader +
                                kSplitterWidth * 2 + frame;
        mmi->ptMinTrackSize.y = 320;
        return 0;
    }

    case WM_SETFOCUS:
        SetFocus(gSidebar);
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDM_VIEW_SIDEBAR:
            ToggleSidebar(hwnd);
            return 0;

        case IDM_HELP_ABOUT:
            ShowAbout(hwnd);
            return 0;

        case IDM_FILE_EXIT:
            /* Close, not DestroyWindow: everything that has to happen
               before the window goes belongs in WM_CLOSE, and this way
               there is one path there rather than two. */
            SendMessage(hwnd, WM_CLOSE, 0, 0);
            return 0;
        }
        break;

    case WM_DESTROY:
        if (gUIFont != NULL &&
            gUIFont != (HFONT)GetStockObject(DEFAULT_GUI_FONT)) {
            DeleteObject(gUIFont);
        }
        gUIFont = NULL;
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProc(hwnd, message, wParam, lParam);
}

static BOOL RegisterClasses(void)
{
    WNDCLASSA cls;

    ZeroMemory(&cls, sizeof(cls));
    cls.style         = CS_HREDRAW | CS_VREDRAW;
    cls.lpfnWndProc   = MainWndProc;
    cls.hInstance     = gInstance;
    cls.hIcon         = LoadIconA(gInstance, MAKEINTRESOURCEA(IDI_GAZETTE));
    cls.hCursor       = LoadCursor(NULL, IDC_ARROW);
    cls.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    cls.lpszMenuName  = MAKEINTRESOURCEA(IDR_MAIN_MENU);
    cls.lpszClassName = kMainClass;

    /* The icon resource is not cut yet; the system's default application
       icon is the honest stand-in until it is. */
    if (cls.hIcon == NULL) {
        cls.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    }

    if (!RegisterClassA(&cls)) {
        return FALSE;
    }

    ZeroMemory(&cls, sizeof(cls));
    cls.lpfnWndProc   = SplitterWndProc;
    cls.hInstance     = gInstance;
    cls.hCursor       = LoadCursor(NULL, IDC_SIZEWE);
    cls.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    cls.lpszClassName = kSplitterClass;

    if (!RegisterClassA(&cls)) {
        return FALSE;
    }

    ZeroMemory(&cls, sizeof(cls));
    cls.style         = CS_HREDRAW | CS_VREDRAW;
    cls.lpfnWndProc   = ReaderWndProc;
    cls.hInstance     = gInstance;
    cls.hCursor       = LoadCursor(NULL, IDC_ARROW);
    cls.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    cls.lpszClassName = kReaderClass;

    return RegisterClassA(&cls) != 0;
}

static HWND CreateMainWindow(int showCommand)
{
    RECT work;
    int width  = kDefaultWidth;
    int height = kDefaultHeight;
    HWND hwnd;

    /* Never open larger than the screen: a 640x480 machine would get a
       window whose right-hand pane is off the edge, and the panes would
       be laid out for a width the user cannot see. */
    if (SystemParametersInfoA(SPI_GETWORKAREA, 0, &work, 0)) {
        int maxWidth  = work.right - work.left;
        int maxHeight = work.bottom - work.top;

        if (width > maxWidth) {
            width = maxWidth;
        }
        if (height > maxHeight) {
            height = maxHeight;
        }
    }

    hwnd = CreateWindowExA(
        0, kMainClass, "Gazette",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, width, height,
        NULL, NULL, gInstance, NULL);

    if (hwnd == NULL) {
        return NULL;
    }

    ShowWindow(hwnd, showCommand);
    UpdateWindow(hwnd);
    return hwnd;
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous,
                   LPSTR commandLine, int showCommand)
{
    HWND hwnd;
    HACCEL accelerators;
    MSG message;

    (void)previous;
    (void)commandLine;

    gInstance = instance;

    if (!InitControls()) {
        MessageBoxA(NULL, "Gazette could not load the common controls.",
                    "Gazette", MB_OK | MB_ICONSTOP);
        return 1;
    }

    gUIFont = CreateUIFont();

    if (!RegisterClasses()) {
        MessageBoxA(NULL, "Gazette could not register its windows.",
                    "Gazette", MB_OK | MB_ICONSTOP);
        return 1;
    }

    hwnd = CreateMainWindow(showCommand);
    if (hwnd == NULL) {
        MessageBoxA(NULL, "Gazette could not open its window.",
                    "Gazette", MB_OK | MB_ICONSTOP);
        return 1;
    }

    accelerators = LoadAcceleratorsA(instance,
                                     MAKEINTRESOURCEA(IDR_ACCELERATORS));

    while (GetMessage(&message, NULL, 0, 0) > 0) {
        if (accelerators != NULL &&
            TranslateAcceleratorA(hwnd, accelerators, &message)) {
            continue;
        }
        /* IsDialogMessage would give Tab between the panes, but it also
           eats the keys the list and the tree want. Tab order comes with
           the real key handling, not before it. */
        TranslateMessage(&message);
        DispatchMessage(&message);
    }

    return (int)message.wParam;
}
