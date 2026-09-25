/*
 * gazette_win_main.c - the Windows shell: WinMain, the frame window, the
 * menus and the About box.
 *
 * The counterpart of src/main.cpp. The window itself is next door in
 * gazette_win_window.c, as the Mac's is in src/ui/platinum_window.c.
 *
 * Targets: Windows 95, 98, Me, NT 4.0, 2000 and XP, as one ANSI binary.
 *
 *   - ANSI, never Unicode. 95/98/Me implement only the A entry points.
 *   - WINVER 0x0400. Nothing later than NT 4.0 / 95 may be called, and
 *     anything from a later comctl32 is fetched with GetProcAddress or
 *     sent as a message a older DLL can ignore.
 *   - NT 3.51 and earlier are out of reach: the tree, the list and the
 *     toolbar arrived with the common controls in 95 and NT 4. Drawing
 *     them by hand instead would be the Mac window again, which is the
 *     one thing this shell is not supposed to be.
 */

#define WINVER       0x0400
#define _WIN32_WINNT 0x0400
#define _WIN32_IE    0x0300

#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <string.h>

#include "gazette_win.h"
#include "gazette_win_res.h"
#include "gazette_version.h"
#include "app/gazette_app.h"
#include "core/gazette_core.h"
#include "feeds/gazette_feeds.h"
#include "feeds/gazette_index.h"
#include "net/gazette_net.h"
#include "store/gazette_store.h"

static const char kMainClass[]  = "GazetteMainWindow";
static const char kAboutClass[] = "GazetteAboutWindow";

/* The frame opens at this size unless the screen is too small for it.
   640x480 is the floor worth caring about -- it is what a period
   machine and a fresh 86Box profile both come up in. */
enum {
    kDefaultWidth  = 760,
    kDefaultHeight = 540
};

static HINSTANCE gInstance;
static HWND      gMainWindow;
static HWND      gAboutWindow;

static LRESULT CALLBACK MainWndProc(HWND, UINT, WPARAM, LPARAM);
static LRESULT CALLBACK AboutWndProc(HWND, UINT, WPARAM, LPARAM);

/* ------------------------------------------------------------------ */
/* The common controls                                                 */
/* ------------------------------------------------------------------ */

typedef BOOL (WINAPI *InitCommonControlsExProc)(const INITCOMMONCONTROLSEX *);

/*
 * Register the control classes this shell asks for.
 *
 * Under the version 6 assembly the manifest names -- which is to say,
 * on XP -- InitCommonControlsEx is not optional: the classes are
 * registered by that call and by nothing else. But the entry point
 * arrived with comctl32 4.70, and a static import of it would stop
 * Gazette at load time on a Windows 95 that never had Internet Explorer
 * installed, before any code of ours ran to say why. So it is fetched
 * by name, and where it is missing the plain InitCommonControls is both
 * sufficient and all there is: a 4.0 DLL registers its classes as it
 * loads.
 *
 * InitCommonControls itself, and the ImageList_* calls the window uses,
 * are all comctl32 4.0 and are linked normally. comctl32.dll shipped on
 * the Windows 95 CD; what did not is anything numbered after it.
 */
static BOOL InitControls(void)
{
    HMODULE comctl;
    InitCommonControlsExProc initEx;
    INITCOMMONCONTROLSEX icc;

    comctl = GetModuleHandleA("comctl32.dll");
    if (comctl == NULL) {
        comctl = LoadLibraryA("comctl32.dll");
    }
    if (comctl == NULL) {
        return FALSE;
    }

    initEx = (InitCommonControlsExProc)
             GetProcAddress(comctl, "InitCommonControlsEx");
    if (initEx != NULL) {
        icc.dwSize = sizeof(icc);
        /* ICC_COOL_CLASSES is the rebar the toolbar sits in. */
        icc.dwICC  = ICC_TREEVIEW_CLASSES | ICC_LISTVIEW_CLASSES |
                     ICC_BAR_CLASSES | ICC_COOL_CLASSES;
        if (initEx(&icc)) {
            return TRUE;
        }
    }

    InitCommonControls();
    return TRUE;
}

/* ------------------------------------------------------------------ */
/* About                                                               */
/*                                                                     */
/* A window of our own, not a MessageBox: the Mac build draws an About  */
/* box and this is the same box. DrawAboutContent() in src/main.cpp     */
/* plots a 32x32 icon centred at the top and then centres seven lines   */
/* at fixed offsets down a 280 by 230 window, and those offsets and     */
/* those words are reproduced here. Only two things change: the family, */
/* to the one Windows has, and the height, because Windows expects an   */
/* OK button where Mac OS closes an About box from its close box. The   */
/* extra height goes below the text so every line still lands where it  */
/* was laid out.                                                       */
/*                                                                     */
/* The arrangement is Gateway's -- src/win32/main_win32.c in that       */
/* project does exactly this for the same reason -- including why the   */
/* sizes are scaled against 96 rather than 72: the Mac's numbers are    */
/* pixels on a 72 dpi screen, so Charcoal 12 is a twelve-pixel em.      */
/* Converting them as points made every line a third taller than its    */
/* counterpart, and the two About boxes carried the same words at       */
/* visibly different sizes.                                            */
/* ------------------------------------------------------------------ */

enum {
    kAboutWidth  = 280,
    kAboutHeight = 272,         /* the Mac's 230 of content, plus the button */
    kAboutButton = 230
};

static const struct {
    int         y;
    int         px;
    int         bold;
    const char *text;
} kAbout[] = {
    {  64, 12, 1, "Gazette " GAZETTE_VERSION_STRING      },
    {  84, 10, 0, "An RSS and Atom reader for Windows"   },
    { 112, 10, 1, "Bruno Castello"                       },
    { 132, 10, 0, "bfcastello@hotmail.com"               },
    { 160, 10, 1, "Engineer: Claude Opus 5"              },
    { 188, 10, 0, "\xA9 Castello Designs, 2026"          },
    { 208, 10, 0, "Built with MinGW-w64"                 }
};

static HFONT AboutFont(int dpi, int px, int weight)
{
    return CreateFontA(-MulDiv(px, dpi, 96), 0, 0, 0, weight, 0, 0, 0,
                       ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       DEFAULT_QUALITY, VARIABLE_PITCH | FF_SWISS,
                       "MS Sans Serif");
}

static LRESULT CALLBACK AboutWndProc(HWND hwnd, UINT message,
                                     WPARAM wParam, LPARAM lParam)
{
    switch (message) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC   dc = BeginPaint(hwnd, &ps);
        RECT  area;
        HFONT title, name, plain, previous;
        HICON icon;
        int   i, dpi, midX;

        GetClientRect(hwnd, &area);
        midX = (area.right - area.left) / 2;

        /*
         * DrawIcon and not DrawIconEx. LoadIcon returns the SM_CXICON
         * image, which is the 32 by 32 the Ex call would be asking for,
         * so the two draw the same pixels here -- but DrawIcon has been
         * in user32 since Windows 3.0.
         */
        icon = LoadIconA(gInstance, MAKEINTRESOURCEA(IDI_GAZETTE));
        if (icon != NULL) {
            DrawIcon(dc, midX - 16, 14, icon);
        }

        dpi   = GetDeviceCaps(dc, LOGPIXELSY);
        title = AboutFont(dpi, 12, FW_BOLD);     /* the application name */
        name  = AboutFont(dpi, 10, FW_BOLD);     /* who wrote it */
        plain = AboutFont(dpi, 10, FW_NORMAL);   /* everything else */

        SetBkMode(dc, TRANSPARENT);
        SetTextAlign(dc, TA_CENTER | TA_BASELINE);
        previous = (HFONT)SelectObject(dc, plain);

        for (i = 0; i < (int)(sizeof(kAbout) / sizeof(kAbout[0])); i++) {
            SelectObject(dc, !kAbout[i].bold ? plain
                             : (kAbout[i].px == 12 ? title : name));
            TextOutA(dc, midX, kAbout[i].y, kAbout[i].text,
                     (int)strlen(kAbout[i].text));
        }

        SelectObject(dc, previous);
        DeleteObject(title);
        DeleteObject(name);
        DeleteObject(plain);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK) {
            DestroyWindow(hwnd);
        }
        return 0;

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        gAboutWindow = NULL;
        return 0;
    }

    return DefWindowProcA(hwnd, message, wParam, lParam);
}

static void ShowAbout(void)
{
    RECT work, frame;
    HWND button;
    int  width, height, x, y;

    if (gAboutWindow != NULL) {         /* already up: bring it forward */
        SetForegroundWindow(gAboutWindow);
        return;
    }

    /* AdjustWindowRect turns the Mac's content size into the outer one,
       so the text lands at the offsets it was laid out for rather than
       however much less the caption leaves. */
    SetRect(&frame, 0, 0, kAboutWidth, kAboutHeight);
    AdjustWindowRect(&frame, WS_CAPTION | WS_SYSMENU, FALSE);
    width  = frame.right - frame.left;
    height = frame.bottom - frame.top;

    if (!SystemParametersInfoA(SPI_GETWORKAREA, 0, &work, 0)) {
        SetRect(&work, 0, 0, 640, 480);
    }
    x = work.left + ((work.right - work.left) - width) / 2;
    y = work.top + ((work.bottom - work.top) - height) / 2;

    gAboutWindow = CreateWindowA(kAboutClass, "About Gazette",
                                 WS_CAPTION | WS_SYSMENU,
                                 x, y, width, height,
                                 gMainWindow, NULL, gInstance, NULL);
    if (gAboutWindow == NULL) {
        return;
    }

    button = CreateWindowA("BUTTON", "OK",
                           WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                           (kAboutWidth - 76) / 2, kAboutButton, 76, 26,
                           gAboutWindow, (HMENU)IDOK, gInstance, NULL);
    if (button != NULL) {
        SendMessage(button, WM_SETFONT, (WPARAM)GazetteWindowFont(),
                    MAKELPARAM(TRUE, 0));
    }

    ShowWindow(gAboutWindow, SW_SHOW);
    SetForegroundWindow(gAboutWindow);
}

/* ------------------------------------------------------------------ */
/* Menus                                                               */
/* ------------------------------------------------------------------ */

static void Enable(HMENU menu, UINT id, BOOL on)
{
    EnableMenuItem(menu, id, MF_BYCOMMAND | (on ? MF_ENABLED : MF_GRAYED));
}

static void Retitle(HMENU menu, UINT id, const char *text)
{
    MENUITEMINFOA info;

    /* SetMenuItemInfo keeps the item's state, which ModifyMenu resets --
       and it is in USER32 on 95 and NT 4. */
    ZeroMemory(&info, sizeof(info));
    info.cbSize     = sizeof(info);
    info.fMask      = MIIM_TYPE;
    info.fType      = MFT_STRING;
    info.dwTypeData = (char *)text;
    SetMenuItemInfoA(menu, id, FALSE, &info);
}

/*
 * The Mac's AdjustMenus, item for item, run as a menu drops down. Items
 * say what they would do rather than what is so -- Hide Sidebar or Show
 * Sidebar, Mark as Read or Mark as Unread -- with Windows' & and its
 * Ctrl key after a tab.
 */
static void AdjustMenus(HMENU menu)
{
    const GazetteArticle *open;
    int  kind  = 0;
    int  index = 0;
    int  at    = GazetteUISelectedArticle();
    int  count = GazetteFeedsArticleCount();
    BOOL any, feedSelected, groupSelected;

    any           = GazetteUISelection(&kind, &index) ? TRUE : FALSE;
    feedSelected  = (BOOL)(any && kind == kGazetteRowFeed);
    groupSelected = (BOOL)(any && kind == kGazetteRowGroup);
    open          = GazetteFeedsArticleAt(at);

    /* File. New Feed and New Group wait for their dialogs. */
    Enable(menu, IDM_FILE_REFRESH,
           (BOOL)(GazetteCoreFeedCount() > 0 &&
                  GazetteFeedsRefreshGetState() != kGazetteRefreshRunning));
    Enable(menu, IDM_FILE_IMPORT, TRUE);
    Enable(menu, IDM_FILE_EXPORT, (BOOL)(GazetteCoreFeedCount() > 0));

    /* Edit. */
    Enable(menu, IDM_EDIT_FIND, (BOOL)(GazetteFeedsTotalCount() > 0));

    /* View. */
    Enable(menu, IDM_VIEW_HIDE_READ, TRUE);
    Enable(menu, IDM_VIEW_HIDE_FEEDS, TRUE);
    CheckMenuItem(menu, IDM_VIEW_HIDE_READ, MF_BYCOMMAND |
                  (GazetteCoreHideReadArticles() ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(menu, IDM_VIEW_HIDE_FEEDS, MF_BYCOMMAND |
                  (GazetteCoreHideReadFeeds() ? MF_CHECKED : MF_UNCHECKED));
    Retitle(menu, IDM_VIEW_SIDEBAR,
            GazetteCoreHideSidebar() ? "Show &Sidebar\tCtrl+S"
                                     : "Hide &Sidebar\tCtrl+S");
    Retitle(menu, IDM_VIEW_TOOLBAR,
            GazetteCoreHideToolbar() ? "Show &Toolbar\tCtrl+T"
                                     : "Hide &Toolbar\tCtrl+T");

    /* Feeds. */
    Enable(menu, IDM_FEEDS_TODAY, TRUE);
    Enable(menu, IDM_FEEDS_UNREAD, TRUE);
    Enable(menu, IDM_FEEDS_STARRED, TRUE);
    Enable(menu, IDM_FEEDS_OLDEST_FIRST, TRUE);
    Retitle(menu, IDM_FEEDS_OLDEST_FIRST,
            GazetteCoreOldestFirst() ? "Show &Newest First"
                                     : "Show &Oldest First");
    Enable(menu, IDM_FEEDS_MARK_ALL, (BOOL)(count > 0));
    Retitle(menu, IDM_FEEDS_MARK_ALL,
            (count > 0 && GazetteFeedsUnreadCount() == 0)
                ? "&Mark All as Unread\tCtrl+K"
                : "&Mark All as Read\tCtrl+K");
    Retitle(menu, IDM_FEEDS_DELETE,
            groupSelected ? "&Delete Group" : "&Delete Feed");
    Enable(menu, IDM_FEEDS_DELETE, (BOOL)(feedSelected || groupSelected));
    Enable(menu, IDM_FEEDS_TURN_OFF, (BOOL)(feedSelected || groupSelected));
    {
        Boolean on = true;

        if (feedSelected) {
            on = GazetteCoreFeedEnabled(index);
        } else if (groupSelected) {
            on = GazetteCoreGroupEnabled(index);
        }
        Retitle(menu, IDM_FEEDS_TURN_OFF, on ? "Turn O&ff" : "Turn O&n");
    }

    /* Article. */
    Enable(menu, IDM_ARTICLE_UNREAD, (BOOL)(open != NULL));
    Retitle(menu, IDM_ARTICLE_UNREAD,
            (open != NULL && !open->read) ? "Mark as &Read\tCtrl+U"
                                          : "Mark as &Unread\tCtrl+U");
    Enable(menu, IDM_ARTICLE_STAR, (BOOL)(open != NULL));
    Retitle(menu, IDM_ARTICLE_STAR,
            (open != NULL && open->starred) ? "Un&star Article\tCtrl+L"
                                            : "&Star Article\tCtrl+L");
    Enable(menu, IDM_ARTICLE_ABOVE, (BOOL)(open != NULL && at > 0));
    Enable(menu, IDM_ARTICLE_BELOW,
           (BOOL)(open != NULL && at >= 0 && at < count - 1));
    Enable(menu, IDM_ARTICLE_NEXT, (BOOL)(GazetteFeedsUnreadCount() > 0));
    Enable(menu, IDM_ARTICLE_BROWSER,
           (BOOL)(GazetteUISelectedArticleLink()[0] != '\0'));
}

/* ------------------------------------------------------------------ */
/* Commands                                                            */
/*                                                                     */
/* What main.cpp's handlers do, where they are not already the          */
/* application's: the preference and the save, or the one call into     */
/* app/gazette_app.c.                                                   */
/* ------------------------------------------------------------------ */

static void HandleHideSidebar(void)
{
    GazetteCoreSetHideSidebar(GazetteCoreHideSidebar() ? false : true);
    GazetteCoreSavePrefs();
    GazetteUIViewChanged();
}

static void HandleHideToolbar(void)
{
    GazetteCoreSetHideToolbar(GazetteCoreHideToolbar() ? false : true);
    GazetteCoreSavePrefs();
    GazetteUIViewChanged();
}

/* Find Next in the Find dialog: the search box's Return, on the Mac. */
static void HandleSearch(void)
{
    char text[128];

    GazetteUISearchText(text, sizeof text);
    GazetteAppSearch(text);
}

/*
 * Hand the article's address to whatever Windows opens web addresses
 * with -- the user's browser, as Internet Config's choice is on the Mac.
 * ShellExecute is in the 95 shell. It answers a number above 32 when it
 * started something.
 */
static void HandleOpenInBrowser(void)
{
    const char *url = GazetteUISelectedArticleLink();

    if (url[0] == '\0') {
        return;
    }
    if ((INT_PTR)ShellExecuteA(gMainWindow, "open", url, NULL, NULL,
                               SW_SHOWNORMAL) > 32) {
        GazetteUISetStatus("Opened in your browser.");
    } else {
        GazetteUISetStatus("No program is set up to open web addresses.");
    }
}

/* Delete, once asked: Windows' own question box, with the Mac's words and
   Windows-1252's curly quotes. */
static void HandleRemove(HWND hwnd)
{
    char message[320];
    int  kind  = 0;
    int  index = 0;

    if (!GazetteUISelection(&kind, &index)) {
        return;
    }
    if (kind == kGazetteRowGroup) {
        wsprintfA(message, "Delete the group \223%s\224? The feeds in it "
                  "are kept - they move to the top of the list.",
                  GazetteCoreGroupName(index));
    } else {
        wsprintfA(message, "Delete the feed \223%s\224? You can subscribe "
                  "to it again at any time.", GazetteCoreFeedTitle(index));
    }
    if (MessageBoxA(hwnd, message, "Gazette",
                    MB_OKCANCEL | MB_ICONQUESTION | MB_DEFBUTTON2) != IDOK) {
        return;
    }
    GazetteAppRemoveSelection();
}

/* The window's own commands -- the Find dialog's Find Next -- by name, as
   the Mac's toolbar sends them. */
static void WindowCommand(int command)
{
    if (command == kGazetteCmdSearch) {
        HandleSearch();
    }
}

static BOOL MenuCommand(HWND hwnd, int id)
{
    switch (id) {
    case IDM_FILE_REFRESH:        GazetteAppRefreshAll();           return TRUE;
    case IDM_FILE_IMPORT:         GazetteAppImportOPML();           return TRUE;
    case IDM_FILE_EXPORT:         GazetteAppExportOPML();           return TRUE;

    case IDM_VIEW_HIDE_READ:      GazetteAppHideReadArticles();     return TRUE;
    case IDM_VIEW_HIDE_FEEDS:     GazetteAppHideReadFeeds();        return TRUE;
    case IDM_VIEW_SIDEBAR:        HandleHideSidebar();              return TRUE;
    case IDM_VIEW_TOOLBAR:        HandleHideToolbar();              return TRUE;

    case IDM_FEEDS_TODAY:   GazetteUISelectSmart(kGazetteSmartToday);   return TRUE;
    case IDM_FEEDS_UNREAD:  GazetteUISelectSmart(kGazetteSmartUnread);  return TRUE;
    case IDM_FEEDS_STARRED: GazetteUISelectSmart(kGazetteSmartStarred); return TRUE;
    case IDM_FEEDS_OLDEST_FIRST:
        GazetteAppSortOrder(GazetteCoreOldestFirst() ? false : true);
        return TRUE;
    case IDM_FEEDS_MARK_ALL:      GazetteAppMarkAllRead();          return TRUE;
    case IDM_FEEDS_TURN_OFF:      GazetteAppToggleEnabled();        return TRUE;
    case IDM_FEEDS_DELETE:        HandleRemove(hwnd);               return TRUE;

    case IDM_ARTICLE_NEXT:        GazetteAppNextUnread();           return TRUE;
    case IDM_ARTICLE_UNREAD:      GazetteAppMarkRead();             return TRUE;
    case IDM_ARTICLE_ABOVE:       GazetteAppMarkRange(false);       return TRUE;
    case IDM_ARTICLE_BELOW:       GazetteAppMarkRange(true);        return TRUE;
    case IDM_ARTICLE_STAR:        GazetteAppToggleStar();           return TRUE;
    case IDM_ARTICLE_BROWSER:     HandleOpenInBrowser();            return TRUE;
    }
    return FALSE;
}

/* ------------------------------------------------------------------ */
/* The frame window                                                    */
/* ------------------------------------------------------------------ */

static LRESULT CALLBACK MainWndProc(HWND hwnd, UINT message,
                                    WPARAM wParam, LPARAM lParam)
{
    /* The Find dialog reports through a message Windows numbers at run
       time, so it cannot be a case label. */
    if (message == GazetteWindowFindMessage()) {
        GazetteWindowFindEvent((const FINDREPLACEA *)lParam);
        return 0;
    }

    switch (message) {
    case WM_PAINT: {
        PAINTSTRUCT paint;
        HDC         dc = BeginPaint(hwnd, &paint);

        GazetteWindowPaint(hwnd, dc);
        EndPaint(hwnd, &paint);
        return 0;
    }

    case WM_CREATE:
        gMainWindow = hwnd;
        if (!GazetteWindowCreate(hwnd, gInstance)) {
            return -1;
        }
        return 0;

    case WM_SIZE:
        GazetteWindowLayout(hwnd);
        return 0;

    case WM_GETMINMAXINFO: {
        MINMAXINFO *mmi = (MINMAXINFO *)lParam;
        POINT minimum;

        GazetteWindowMinimumSize(&minimum);
        if (minimum.x > 0) {
            mmi->ptMinTrackSize = minimum;
        }
        return 0;
    }

    case WM_MEASUREITEM:
        GazetteWindowMeasureItem((MEASUREITEMSTRUCT *)lParam);
        return TRUE;

    case WM_DRAWITEM:
        GazetteWindowDrawItem((const DRAWITEMSTRUCT *)lParam);
        return TRUE;

    case WM_NOTIFY: {
        LRESULT result = 0;

        if (GazetteWindowNotify(hwnd, (NMHDR *)lParam, &result)) {
            return result;
        }
        break;
    }

    case WM_INITMENUPOPUP:
        /* The whole bar is brought up to date whichever menu is opening:
           the accelerators run through the same enabling. */
        AdjustMenus(GetMenu(hwnd));
        return 0;

    case WM_COMMAND:
        if (GazetteWindowCommand(hwnd, wParam, lParam)) {
            return 0;
        }
        /* An accelerator for an item that is greyed does nothing, as a
           greyed menu item does: the bar is adjusted, then asked. */
        if (HIWORD(wParam) == 1) {
            UINT state;

            AdjustMenus(GetMenu(hwnd));
            state = GetMenuState(GetMenu(hwnd), LOWORD(wParam),
                                 MF_BYCOMMAND);
            if (state != (UINT)-1 && (state & (MF_GRAYED | MF_DISABLED))) {
                return 0;
            }
        }
        if (MenuCommand(hwnd, LOWORD(wParam))) {
            return 0;
        }
        switch (LOWORD(wParam)) {
        case IDM_HELP_ABOUT:
            ShowAbout();
            return 0;

        case IDM_FILE_QUIT:
            /* Close, not DestroyWindow: whatever has to happen before
               the window goes belongs in WM_CLOSE, and this way there
               is one path there rather than two. */
            SendMessage(hwnd, WM_CLOSE, 0, 0);
            return 0;
        }
        break;

    case WM_DESTROY:
        /* Everything in flight stopped, and what was read written, before
           the window it would report to goes. */
        GazetteAppShutdown();
        GazetteWindowDestroy();
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

    if (!RegisterClassA(&cls)) {
        return FALSE;
    }

    ZeroMemory(&cls, sizeof(cls));
    cls.style         = CS_HREDRAW | CS_VREDRAW;
    cls.lpfnWndProc   = AboutWndProc;
    cls.hInstance     = gInstance;
    cls.hIcon         = LoadIconA(gInstance, MAKEINTRESOURCEA(IDI_GAZETTE));
    cls.hCursor       = LoadCursor(NULL, IDC_ARROW);
    cls.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    cls.lpszClassName = kAboutClass;

    if (!RegisterClassA(&cls)) {
        return FALSE;
    }

    return GazetteWindowRegisterClasses(gInstance);
}

static HWND CreateMainWindow(int showCommand)
{
    RECT work;
    int  width  = kDefaultWidth;
    int  height = kDefaultHeight;
    HWND hwnd;

    /* Never open larger than the screen: on a 640x480 machine the
       right-hand pane would be off the edge, and the panes would be
       laid out for a width the user cannot see. */
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

    /* WS_CLIPCHILDREN: the frame paints its own grey -- the edge round the
       toolbar strip, the gaps -- and never over the panes, whose white is
       theirs to draw. Without it a frame repaint left the headline list
       grey until something made the list paint again. */
    hwnd = CreateWindowExA(0, kMainClass, "Gazette",
                           WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                           CW_USEDEFAULT, CW_USEDEFAULT, width, height,
                           NULL, NULL, gInstance, NULL);
    if (hwnd == NULL) {
        return NULL;
    }

    ShowWindow(hwnd, showCommand);
    UpdateWindow(hwnd);
    return hwnd;
}

/*
 * How long the loop sleeps when no message is waiting: 100 ms, the Mac's
 * kSleepTicks of 6 at sixty to the second. The same number on both systems
 * means the network advances at the same pace on both, and the pumps above
 * were tuned against it.
 */
enum { kSleepMs = 100 };

/*
 * The idle branch: the one place network I/O advances, as in the Mac's
 * RunGazette. Nothing it calls may block.
 */
static void PumpNetwork(void)
{
    GazetteAppPumpNetwork();
}

/* One message, through the About box and the accelerators first. */
static void HandleMessage(HWND hwnd, HACCEL accelerators, MSG *message)
{
    if (gAboutWindow != NULL && IsDialogMessage(gAboutWindow, message)) {
        return;                     /* Return and Escape close the box */
    }
    if (GazetteWindowFindDialog() != NULL &&
        IsDialogMessage(GazetteWindowFindDialog(), message)) {
        return;                     /* the Find dialog's own keys */
    }
    if (accelerators != NULL &&
        TranslateAcceleratorA(hwnd, accelerators, message)) {
        return;
    }
    TranslateMessage(message);
    DispatchMessage(message);
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous,
                   LPSTR commandLine, int showCommand)
{
    HWND   hwnd;
    HACCEL accelerators;
    MSG    message;

    (void)previous;
    (void)commandLine;

    gInstance = instance;

    if (!InitControls()) {
        MessageBoxA(NULL, "Gazette could not load the common controls.",
                    "Gazette", MB_OK | MB_ICONSTOP);
        return 1;
    }

    if (!RegisterClasses()) {
        MessageBoxA(NULL, "Gazette could not register its windows.",
                    "Gazette", MB_OK | MB_ICONSTOP);
        return 1;
    }

    /* The preferences, then the read marks, before the window: the first
       thing drawn is a sidebar with unread counts in it. The store keeps
       its own copy of the View menu's sort and filter. InitGazette's
       order on the Mac. */
    (void)GazetteCoreInit();
    GazetteIndexLoad();
    GazetteFeedsSetOldestFirst(GazetteCoreOldestFirst() ? 1 : 0);
    GazetteFeedsSetHideRead(GazetteCoreHideReadArticles() ? 1 : 0);

    /* Winsock and Certainly, once. A machine without TCP/IP still opens
       the window and reads its cache; the status line says why nothing
       refreshes, as the Mac's does when Open Transport will not open. */
    (void)GazetteNetInit();

    GazetteWindowSetCallbacks(GazetteAppShowFeed, GazetteAppShowArticle,
                              GazetteAppShowGroup, GazetteAppShowSmart,
                              WindowCommand);

    hwnd = CreateMainWindow(showCommand);
    if (hwnd == NULL) {
        MessageBoxA(NULL, "Gazette could not open its window.",
                    "Gazette", MB_OK | MB_ICONSTOP);
        GazetteCoreShutdown();
        GazetteNetShutdown();
        return 1;
    }

    accelerators = LoadAcceleratorsA(instance,
                                     MAKEINTRESOURCEA(IDR_ACCELERATORS));

    /* The Open and Save As dialogs run a loop of their own; this keeps a
       fetch moving while one is up. */
    GazetteStoreSetIdle(GazetteAppPumpRefresh);

    /* Whatever the last run left cached, so the window has content before
       any network work happens. */
    GazetteAppShowFeed(0);

    /*
     * The message loop is the Mac's WaitNextEvent loop in Win32's words.
     * GetMessage would sleep until the next message and starve the network
     * whenever the user sat still, so: take every message that is waiting,
     * give the network its slice, then sleep until either a message arrives
     * or kSleepMs passes. MsgWaitForMultipleObjects with no handles is
     * exactly that wait, and is in USER32 from Windows 95 and NT 3.1 on.
     */
    for (;;) {
        while (PeekMessageA(&message, NULL, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) {
                /* Writes the preferences if anything changed them --
                   including a first run, which saves the defaults. */
                GazetteCoreShutdown();
                GazetteNetShutdown();
                return (int)message.wParam;
            }
            HandleMessage(hwnd, accelerators, &message);
        }
        PumpNetwork();
        (void)MsgWaitForMultipleObjects(0, NULL, FALSE, kSleepMs,
                                        QS_ALLINPUT);
    }
}
