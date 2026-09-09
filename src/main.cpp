/*
 * Gazette — Carbon RSS / Atom Reader for Mac OS 9 (PowerPC)
 * Copyright (c) 2026 brunocastello
 *
 * Phase 0: Skeleton — Carbon shell, Platinum window, menus, quit, and the
 * preference/feed list loaded from disk at launch and written back at exit.
 *
 * Built against Apple's Universal Interfaces 3.4 with TARGET_API_MAC_CARBON=1
 * (defined by Retro68's retrocarbon toolchain file, which is what makes these
 * headers expose the Carbon-safe subset of the Toolbox).
 *
 * Retro68 links the interface headers *flat* into the toolchain's include
 * directory, so it is <MacWindows.h>, not <Carbon/MacWindows.h>. The <Carbon.h>
 * umbrella is staged too, but it drags in the whole of ApplicationServices and
 * CoreServices — Quartz, ATSUI, Navigation, ICA — none of which this app
 * touches. Including only what we use keeps the build honest and fast.
 *
 * The event loop is the classic WaitNextEvent loop rather than the Carbon
 * Event Manager. CarbonLib supports it fully on Mac OS 9, it is the single
 * cooperative loop AGENT.md mandates, and its idle branch is where Phase 1
 * will poll non-blocking network I/O without ever blocking the UI.
 */

#include <MacTypes.h>
#include <Quickdraw.h>
#include <QuickdrawText.h>
#include <Fonts.h>
#include <MacWindows.h>
#include <Menus.h>
#include <Dialogs.h>
#include <Events.h>
#include <Appearance.h>
#include <Sound.h>

#include <stdio.h>
#include <string.h>

#include "core/gazette_core.h"
#include "net/gazette_fetch.h"
#include "net/gazette_net.h"

/* ------------------------------------------------------------------ */
/* Forward declarations                                                */
/* ------------------------------------------------------------------ */

static Boolean InitGazette(void);
static Boolean BuildMenuBar(void);
static void    RunGazette(void);
static void    DoExitGazette(void);

static void    HandleEvent(const EventRecord *event);
static void    HandleMouseDown(const EventRecord *event);
static void    HandleMenuChoice(long menuResult);

static void    InvalWholeWindow(WindowRef window);
static void    HandleAbout(void);
static void    HandleQuit(void);
static void    HandleRefresh(void);
static void    PumpFetch(void);
static void    SetStatus(const char *text);
static void    DrawGazetteWindow(WindowRef window);
static void    DrawCString(const char *text);

/* ------------------------------------------------------------------ */
/* Application globals                                                 */
/* ------------------------------------------------------------------ */

static Boolean   gDone       = false;
static WindowRef gMainWindow = nil;

/* Whether GazetteCoreInit() found a preferences file. Only the status line
   cares, but on a first run it is the difference between "these are your
   feeds" and "these are the ones Gazette started you with". */
static Boolean   gHadPrefsFile = false;

/* Whether Open Transport and Certainly came up. On Mac OS 9 a failure here
   almost always means TCP/IP is not configured rather than anything Gazette
   did, so it is worth saying so on screen rather than only at fetch time. */
static Boolean   gNetUp = false;

/* The one fetch in flight, or nil. Phase 2 turns this into a queue over the
   whole feed list; Phase 1 proves a single one runs to completion without
   the event loop ever stopping. */
static GazetteFetch *gFetch = nil;

/* The bottom line of the window. Redrawn only when it actually changes —
   invalidating on every pump would repaint the window many times a second
   for no visible difference. */
static char      gStatus[160] = "";

/* Menu IDs */
enum {
    kMenuApple  = 128,
    kMenuFile   = 129,
    kMenuEdit   = 130,
    kMenuWindow = 131
};

/* Menu item indices, in the order AppendMenu() adds them below. */
enum {
    kAppleItemAbout = 1
};

enum {
    kFileItemRefresh = 1,
    /* 2 is a divider */
    kFileItemClose   = 3,
    /* 4 is a divider */
    kFileItemQuit    = 5
};

enum {
    kWindowItemGazette = 1
};

/* Must match kAboutAlertID in Resources/Gazette.r. */
enum {
    kAboutAlertID = 128
};

/* WaitNextEvent sleep, in ticks. Short enough that Phase 1's network poll
   stays responsive, long enough to be a good cooperative citizen. */
enum {
    kSleepTicks = 10
};

/* ------------------------------------------------------------------ */
/* Entry point                                                         */
/* ------------------------------------------------------------------ */

int main(void)
{
    if (!InitGazette()) {
        SysBeep(30);
        DoExitGazette();
        return 1;
    }

    RunGazette();
    DoExitGazette();

    return 0;
}

/* ------------------------------------------------------------------ */
/* Initialization                                                      */
/*                                                                     */
/* No InitGraf/InitWindows/InitMenus/MaxApplZone here: those are all    */
/* CALL_NOT_IN_CARBON. CarbonLib sets the Toolbox up before main() runs.*/
/* ------------------------------------------------------------------ */

static Boolean InitGazette(void)
{
    OSStatus         err;
    Rect             bounds;
    WindowAttributes attrs;

    InitCursor();

    /* Before anything is drawn: the window's first update event already wants
       the feed list. GazetteCoreInit() falls back to defaults when there is no
       file, so there is nothing here to fail on. */
    gHadPrefsFile = GazetteCoreInit();

    /* Open Transport before the window: InitOpenTransport can put up its own
       dialog if TCP/IP needs loading, and it should not do that over a
       half-drawn window. Failure is not fatal — Gazette still reads its cache
       and its preferences without a network. */
    gNetUp = GazetteNetInit() ? true : false;

    if (!BuildMenuBar()) {
        return false;
    }

    SetRect(&bounds, 60, 60, 60 + 640, 60 + 440);

    /* No kWindowStandardHandlerAttribute: that installs the Carbon Event
       Manager's standard handler, which would compete with the
       WaitNextEvent loop below. */
    attrs = kWindowStandardDocumentAttributes;

    err = CreateNewWindow(kDocumentWindowClass, attrs, &bounds, &gMainWindow);
    if (err != noErr || gMainWindow == nil) {
        return false;
    }

    SetWTitle(gMainWindow, "\pGazette");

    /* Ask the Appearance Manager for the Platinum dialog background rather
       than hard-coding a grey, so the window tracks the user's theme. */
    SetThemeWindowBackground(gMainWindow, kThemeBrushDialogBackgroundActive, false);

    ShowWindow(gMainWindow);
    SelectWindow(gMainWindow);

    return true;
}

/* ------------------------------------------------------------------ */
/* Menu bar — built programmatically, no MBAR/MENU resources needed     */
/*                                                                     */
/* AppendMenu() metacharacters: ';' separates items, '(' disables one,  */
/* a lone '-' is a divider, and '/X' assigns a command key.             */
/* ------------------------------------------------------------------ */

static Boolean BuildMenuBar(void)
{
    MenuRef appleMenu, fileMenu, editMenu, windowMenu;

    /* "\024" is the Apple logo in MacRoman. */
    appleMenu = NewMenu(kMenuApple, "\p\024");
    if (appleMenu == nil) {
        return false;
    }
    /* "\311" is the MacRoman ellipsis. */
    AppendMenu(appleMenu, "\pAbout Gazette\311");
    InsertMenu(appleMenu, 0);

    fileMenu = NewMenu(kMenuFile, "\pFile");
    if (fileMenu == nil) {
        return false;
    }
    AppendMenu(fileMenu, "\pRefresh/R;(-;Close/W;(-;Quit/Q");
    InsertMenu(fileMenu, 0);

    editMenu = NewMenu(kMenuEdit, "\pEdit");
    if (editMenu == nil) {
        return false;
    }
    AppendMenu(editMenu, "\pUndo/Z;(-;Cut/X;Copy/C;Paste/V;Clear");
    InsertMenu(editMenu, 0);

    windowMenu = NewMenu(kMenuWindow, "\pWindow");
    if (windowMenu == nil) {
        return false;
    }
    AppendMenu(windowMenu, "\pGazette");
    InsertMenu(windowMenu, 0);

    DrawMenuBar();

    return true;
}

/* ------------------------------------------------------------------ */
/* Main event loop — one cooperative WaitNextEvent loop                 */
/* ------------------------------------------------------------------ */

static void RunGazette(void)
{
    EventRecord event;

    while (!gDone) {
        if (WaitNextEvent(everyEvent, &event, kSleepTicks, nil)) {
            HandleEvent(&event);
        } else {
            /* Idle. This is the one place network I/O advances, and nothing
               it calls blocks: a pump does whatever work is available this
               pass and returns. Blocking here would stop the whole machine
               cooperating, not just Gazette. */
            PumpFetch();
        }
    }
}

static void HandleEvent(const EventRecord *event)
{
    switch (event->what) {
        case mouseDown:
            HandleMouseDown(event);
            break;

        case keyDown:
        case autoKey:
            /* MenuEvent() does the cmdKey test and the command-key lookup
               itself, and is the Carbon-blessed replacement for MenuKey(). */
            HandleMenuChoice((long)MenuEvent(event));
            break;

        case updateEvt: {
            WindowRef window = (WindowRef)event->message;

            BeginUpdate(window);
            DrawGazetteWindow(window);
            EndUpdate(window);
            break;
        }

        case activateEvt:
            InvalWholeWindow((WindowRef)event->message);
            break;

        default:
            break;
    }
}

static void HandleMouseDown(const EventRecord *event)
{
    WindowRef      window = nil;
    WindowPartCode part;

    part = FindWindow(event->where, &window);

    switch (part) {
        case inMenuBar:
            HandleMenuChoice(MenuSelect(event->where));
            break;

        case inContent:
            if (window != FrontWindow()) {
                SelectWindow(window);
            }
            break;

        case inDrag:
            /* A NULL bounding box means "the whole desktop"; that is legal
               from CarbonLib 1.0 forward. */
            DragWindow(window, event->where, nil);
            break;

        case inGrow: {
            /* GrowWindow/SizeWindow rather than ResizeWindow: the pair is
               available all the way back to CarbonLib 1.0. */
            Rect limits;
            long newSize;

            SetRect(&limits, 320, 240, 32767, 32767);
            newSize = GrowWindow(window, event->where, &limits);
            if (newSize != 0) {
                SizeWindow(window, (short)(newSize & 0xFFFF),
                           (short)(newSize >> 16), true);
                InvalWholeWindow(window);
            }
            break;
        }

        case inZoomIn:
        case inZoomOut:
            if (TrackBox(window, event->where, part)) {
                ZoomWindow(window, part, true);
                InvalWholeWindow(window);
            }
            break;

        case inGoAway:
            if (TrackGoAway(window, event->where)) {
                /* Phase 0 has a single window, so closing it quits. */
                HandleQuit();
            }
            break;

        default:
            break;
    }
}

/* ------------------------------------------------------------------ */
/* Menu dispatch                                                       */
/* ------------------------------------------------------------------ */

static void HandleMenuChoice(long menuResult)
{
    short menuID   = (short)(menuResult >> 16);
    short menuItem = (short)(menuResult & 0xFFFF);

    if (menuID == 0) {
        return;
    }

    switch (menuID) {
        case kMenuApple:
            if (menuItem == kAppleItemAbout) {
                HandleAbout();
            }
            break;

        case kMenuFile:
            if (menuItem == kFileItemRefresh) {
                HandleRefresh();
            } else if (menuItem == kFileItemClose || menuItem == kFileItemQuit) {
                HandleQuit();
            }
            break;

        case kMenuEdit:
            /* Phase 4 wires these up to the reader pane. */
            break;

        case kMenuWindow:
            if (menuItem == kWindowItemGazette && gMainWindow != nil) {
                SelectWindow(gMainWindow);
            }
            break;

        default:
            break;
    }

    HiliteMenu(0);
}

/* ------------------------------------------------------------------ */
/* Menu actions                                                        */
/* ------------------------------------------------------------------ */

static void HandleAbout(void)
{
    (void)Alert(kAboutAlertID, nil);
}

static void HandleQuit(void)
{
    gDone = true;
}

/* ------------------------------------------------------------------ */
/* Fetching                                                            */
/*                                                                     */
/* Phase 1 proves one HTTPS GET runs to completion from the idle branch */
/* without the UI ever stopping. Phase 2 hands the bytes to the feed    */
/* parser instead of counting them.                                     */
/* ------------------------------------------------------------------ */

static void SetStatus(const char *text)
{
    if (strcmp(gStatus, text) == 0) {
        return;                     /* nothing to repaint */
    }
    strncpy(gStatus, text, sizeof gStatus - 1);
    gStatus[sizeof gStatus - 1] = '\0';
    InvalWholeWindow(gMainWindow);
}

/*
 * Where the body goes. Phase 2 replaces this with the RSS/Atom parser fed
 * incrementally; until then the bytes are counted and dropped, which is the
 * honest way to prove the transfer works without pretending to parse it.
 */
static int CountingSink(const char *data, size_t len, void *context)
{
    (void)data;
    (void)len;
    (void)context;
    return 1;
}

static void HandleRefresh(void)
{
    const char *url;
    char        message[160];

    if (gFetch != nil) {
        return;                     /* one at a time in Phase 1 */
    }
    if (!gNetUp) {
        SetStatus("No network - check the TCP/IP control panel.");
        return;
    }
    if (GazetteCoreFeedCount() == 0) {
        SetStatus("No feeds configured.");
        return;
    }

    url = GazetteCoreFeedURL(0);
    gFetch = GazetteFetchStart(url, CountingSink, nil);
    if (gFetch == nil) {
        SetStatus("Could not start the fetch.");
        return;
    }

    snprintf(message, sizeof message, "Fetching %s...", GazetteCoreFeedTitle(0));
    SetStatus(message);
}

static void PumpFetch(void)
{
    char message[160];

    if (gFetch == nil) {
        return;
    }

    switch (GazetteFetchPump(gFetch)) {
        case kGazetteFetchDone:
            snprintf(message, sizeof message,
                     "HTTP %d - %ld bytes from %s",
                     GazetteFetchStatus(gFetch),
                     GazetteFetchBytesRead(gFetch),
                     GazetteFetchFinalURL(gFetch));
            SetStatus(message);
            GazetteFetchDestroy(gFetch);
            gFetch = nil;
            break;

        case kGazetteFetchFailed:
            snprintf(message, sizeof message, "Failed: %s",
                     GazetteFetchErrorText(gFetch));
            SetStatus(message);
            GazetteFetchDestroy(gFetch);
            gFetch = nil;
            break;

        case kGazetteFetchBody:
            snprintf(message, sizeof message, "Reading... %ld bytes",
                     GazetteFetchBytesRead(gFetch));
            SetStatus(message);
            break;

        default:
            break;                  /* connecting, sending, reading headers */
    }
}

/* ------------------------------------------------------------------ */
/* Drawing                                                             */
/*                                                                     */
/* SetThemeWindowBackground() already gave the port the Platinum dialog */
/* pattern, so EraseRect() paints the correct background for us.        */
/* ------------------------------------------------------------------ */

/* InvalWindowRect() takes a real Rect — unlike DragWindow's bounding box,
   NULL is not documented as meaning "everything". */
static void InvalWholeWindow(WindowRef window)
{
    Rect bounds;

    if (window == nil) {
        return;
    }

    GetWindowPortBounds(window, &bounds);
    InvalWindowRect(window, &bounds);
}

/* DrawString() wants a Pascal string; the feed list is plain C, so the
   lengths come from strlen() and go to DrawText() instead. QuickDraw clips
   to the port, so an over-long title stops at the window edge by itself. */
static void DrawCString(const char *text)
{
    size_t len;

    if (text == nil) {
        return;
    }
    len = strlen(text);
    if (len > 32767) {
        len = 32767;
    }
    if (len > 0) {
        DrawText(text, 0, (short)len);
    }
}

static void DrawGazetteWindow(WindowRef window)
{
    GrafPtr savePort;
    Rect    bounds;
    short   line;
    int     count;
    int     i;

    GetPort(&savePort);
    SetPortWindowPort(window);

    GetWindowPortBounds(window, &bounds);
    EraseRect(&bounds);

    TextFont(systemFont);
    TextSize(12);

    line = (short)(bounds.top + 28);
    MoveTo((short)(bounds.left + 16), line);
    if (gHadPrefsFile) {
        DrawString("\pFeeds from Gazette Preferences");
    } else {
        DrawString("\pFeeds (no preferences file yet - defaults)");
    }

    /* Geneva 9 is what Newsstand listed headlines in, and it is what the
       Phase 3 sidebar will use. The feed list is the first thing to wear it. */
    TextFont(kFontIDGeneva);
    TextSize(9);

    count = GazetteCoreFeedCount();
    line  = (short)(bounds.top + 52);

    /* Stop at the status line rather than drawing 64 feeds down through it.
       Phase 3 replaces this with a scrolling sidebar. */
    for (i = 0; i < count && line < bounds.bottom - 28; i++) {
        MoveTo((short)(bounds.left + 16), line);
        DrawCString(GazetteCoreFeedTitle(i));
        line = (short)(line + 14);
    }

    TextFont(systemFont);
    TextSize(12);

    MoveTo((short)(bounds.left + 16), (short)(bounds.bottom - 16));
    if (gStatus[0] != '\0') {
        DrawCString(gStatus);
    } else if (gNetUp) {
        DrawString("\pNetwork ready - press Command-R to fetch the first feed.");
    } else {
        DrawString("\pNo network - check the TCP/IP control panel.");
    }

    SetPort(savePort);
}

/* ------------------------------------------------------------------ */
/* Cleanup                                                             */
/* ------------------------------------------------------------------ */

static void DoExitGazette(void)
{
    /* Writes the preferences back out if anything changed them — including a
       first run, which saves the defaults so there is a file to hand-edit. */
    if (gFetch != nil) {
        GazetteFetchDestroy(gFetch);    /* closes the connection too */
        gFetch = nil;
    }

    GazetteCoreShutdown();
    GazetteNetShutdown();

    if (gMainWindow != nil) {
        DisposeWindow(gMainWindow);
        gMainWindow = nil;
    }
}
