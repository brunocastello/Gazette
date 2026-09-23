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
#include <string.h>

#include "gazette_win.h"
#include "gazette_win_res.h"
#include "gazette_version.h"
#include "net/gazette_net.h"

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

/*
 * Two items say what they would do rather than what is so, as their
 * Mac counterparts do, so their wording is set here rather than fixed
 * in the resource script.
 */
static void AdjustMenus(HWND hwnd)
{
    HMENU menu = GetMenu(hwnd);

    ModifyMenuA(menu, IDM_VIEW_SIDEBAR, MF_BYCOMMAND | MF_STRING,
                IDM_VIEW_SIDEBAR,
                GazetteWindowSidebarHidden() ? "Show &Sidebar\tCtrl+S"
                                             : "Hide &Sidebar\tCtrl+S");
    ModifyMenuA(menu, IDM_VIEW_TOOLBAR, MF_BYCOMMAND | MF_STRING,
                IDM_VIEW_TOOLBAR,
                GazetteWindowToolbarHidden() ? "Show &Toolbar\tCtrl+T"
                                             : "Hide &Toolbar\tCtrl+T");
    DrawMenuBar(hwnd);
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

    case WM_COMMAND:
        if (GazetteWindowCommand(hwnd, LOWORD(wParam))) {
            return 0;
        }
        switch (LOWORD(wParam)) {
        case IDM_VIEW_SIDEBAR:
            GazetteWindowToggleSidebar(hwnd);
            AdjustMenus(hwnd);
            return 0;

        case IDM_VIEW_TOOLBAR:
            GazetteWindowToggleToolbar(hwnd);
            AdjustMenus(hwnd);
            return 0;

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

    hwnd = CreateWindowExA(0, kMainClass, "Gazette", WS_OVERLAPPEDWINDOW,
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
 * RunGazette. Nothing it calls may block. The refresh, full-text and photo
 * pumps join it as the Windows shell grows the engine's store; until then
 * it has nothing to turn.
 */
static void PumpNetwork(void)
{
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

    hwnd = CreateMainWindow(showCommand);
    if (hwnd == NULL) {
        MessageBoxA(NULL, "Gazette could not open its window.",
                    "Gazette", MB_OK | MB_ICONSTOP);
        return 1;
    }

    accelerators = LoadAcceleratorsA(instance,
                                     MAKEINTRESOURCEA(IDR_ACCELERATORS));

    /* Winsock and Certainly, once. A machine without TCP/IP still opens
       the window and reads its cache; the status line says why nothing
       refreshes, as the Mac's does when Open Transport will not open. */
    (void)GazetteNetInit();

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
