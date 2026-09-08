/*
 * Gazette — Carbon RSS / Atom Reader for Mac OS 9 (PowerPC)
 * Copyright (c) 2026 brunocastello
 *
 * Phase 0: Skeleton — Carbon shell, Platinum window, menus, quit.
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

#include "core/gazette_core.h"

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
static void    DrawGazetteWindow(WindowRef window);

/* ------------------------------------------------------------------ */
/* Application globals                                                 */
/* ------------------------------------------------------------------ */

static Boolean   gDone       = false;
static WindowRef gMainWindow = nil;

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
    kFileItemClose = 1,
    /* 2 is a divider */
    kFileItemQuit  = 3
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
    AppendMenu(fileMenu, "\pClose/W;(-;Quit/Q");
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
            /* Idle. Phase 1 polls non-blocking network I/O from here — it
               must never block, or the whole machine stops cooperating. */
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
            if (menuItem == kFileItemClose || menuItem == kFileItemQuit) {
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

static void DrawGazetteWindow(WindowRef window)
{
    GrafPtr savePort;
    Rect    bounds;

    GetPort(&savePort);
    SetPortWindowPort(window);

    GetWindowPortBounds(window, &bounds);
    EraseRect(&bounds);

    TextFont(systemFont);
    TextSize(12);

    MoveTo(bounds.left + 16, bounds.top + 28);
    DrawString("\pGazette - Phase 0 skeleton");

    MoveTo(bounds.left + 16, bounds.top + 48);
    DrawString("\pSidebar, article list and reader pane arrive in Phase 3.");

    SetPort(savePort);
}

/* ------------------------------------------------------------------ */
/* Cleanup                                                             */
/* ------------------------------------------------------------------ */

static void DoExitGazette(void)
{
    if (gMainWindow != nil) {
        DisposeWindow(gMainWindow);
        gMainWindow = nil;
    }
}
