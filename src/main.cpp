/*
 * Gazette — Carbon RSS / Atom Reader for Mac OS 9 (PowerPC)
 * Copyright (c) 2026 brunocastello
 *
 * The shell: initialise, build the menus, run one cooperative event loop,
 * drive the refresh from its idle branch, and shut down. Everything the
 * Toolbox knows about the interface itself lives in ui/platinum_window.c;
 * everything about feeds lives behind core/gazette_core.h. This file is the
 * seam between the two and holds no state of its own beyond what the loop
 * needs.
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
 * cooperative loop AGENT.md mandates, and its idle branch is where network
 * I/O is polled without ever blocking the UI.
 */

#include <MacTypes.h>
#include <Quickdraw.h>
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
#include "feeds/gazette_feeds.h"
#include "net/gazette_net.h"
#include "ui/platinum_window.h"

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

static void    HandleAbout(void);
static void    HandleQuit(void);
static void    HandleRefresh(void);
static void    ShowFeed(int feedIndex);
static void    PumpRefresh(void);
static void    CheckAutoRefresh(void);

/* ------------------------------------------------------------------ */
/* Application globals                                                 */
/* ------------------------------------------------------------------ */

static Boolean gDone  = false;
static Boolean gNetUp = false;

/* When the running refresh started, so it can be timed, and when the last one
   finished, so auto-refresh knows how long it has been. Both in ticks. */
static unsigned long gLastRefreshTicks;

/* Menu IDs */
enum {
    kMenuApple  = 128,
    kMenuFile   = 129,
    kMenuEdit   = 130,
    kMenuWindow = 131
};

enum {
    kAppleItemAbout = 1
};

/* Menu item indices, in the order AppendMenu() adds them below. */
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

/* WaitNextEvent sleep, in ticks. Short enough that the network poll stays
   responsive, long enough to be a good cooperative citizen. */
enum {
    kSleepTicks = 6
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
    InitCursor();

    /* Preferences before anything is drawn: the window's first update event
       already wants the feed list. */
    (void)GazetteCoreInit();

    /* Open Transport before the window, because InitOpenTransport can put up
       a dialog of its own if TCP/IP needs loading and should not do that over
       a half-drawn window. Failure is not fatal — the cache still reads. */
    gNetUp = GazetteNetInit() ? true : false;

    if (!BuildMenuBar()) {
        return false;
    }

    if (!GazetteUIOpen(ShowFeed)) {
        return false;
    }

    /* Show whatever the last run left cached, so the window has content
       before any network work happens — which on a machine with no
       connection is the whole of what Gazette can do. */
    ShowFeed(0);

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
            PumpRefresh();
            CheckAutoRefresh();
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
        case autoKey: {
            /* MenuEvent() does the cmdKey test and the command-key lookup
               itself, and is the Carbon-blessed replacement for MenuKey(). It
               returns 0 for anything that is not a menu command, which is
               where the window's own keys are handled. */
            long choice = (long)MenuEvent(event);

            if (choice != 0) {
                HandleMenuChoice(choice);
            } else if ((event->modifiers & cmdKey) == 0) {
                (void)GazetteUIKey((short)(event->message & charCodeMask));
            }
            break;
        }

        case updateEvt: {
            WindowRef window = (WindowRef)event->message;

            BeginUpdate(window);
            if (window == GazetteUIWindow()) {
                GazetteUIUpdate();
            }
            EndUpdate(window);
            break;
        }

        case activateEvt:
            if ((WindowRef)event->message == GazetteUIWindow()) {
                GazetteUIActivate((event->modifiers & activeFlag) != 0);
                GazetteUIResized();     /* redraws, and re-hilites the frames */
            }
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
            } else if (window == GazetteUIWindow()) {
                Point local = event->where;

                SetPortWindowPort(window);
                GlobalToLocal(&local);
                GazetteUIClick(local, event->modifiers);
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

            SetRect(&limits, 420, 260, 32767, 32767);
            newSize = GrowWindow(window, event->where, &limits);
            if (newSize != 0) {
                SizeWindow(window, (short)(newSize & 0xFFFF),
                           (short)(newSize >> 16), true);
                GazetteUIResized();
            }
            break;
        }

        case inZoomIn:
        case inZoomOut:
            if (TrackBox(window, event->where, part)) {
                ZoomWindow(window, part, true);
                GazetteUIResized();
            }
            break;

        case inGoAway:
            if (TrackGoAway(window, event->where)) {
                /* One window, so closing it quits. */
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
            } else if (menuItem == kFileItemClose ||
                       menuItem == kFileItemQuit) {
                HandleQuit();
            }
            break;

        case kMenuEdit:
            /* Phase 4 wires these up to the reader pane. */
            break;

        case kMenuWindow:
            if (menuItem == kWindowItemGazette &&
                GazetteUIWindow() != nil) {
                SelectWindow(GazetteUIWindow());
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
/* Feeds                                                               */
/* ------------------------------------------------------------------ */

static long PrefsMaxArticles(void)
{
    const GazettePrefs *prefs = GazetteCoreGetPrefs();

    return (prefs != nil) ? prefs->maxArticles : 0;
}

/*
 * Show a feed. Reads the cache first and only reaches for the network when
 * there is nothing cached — so clicking through the sidebar is instant after
 * the first fetch, and works with the machine unplugged.
 */
static void ShowFeed(int feedIndex)
{
    char message[224];

    if (feedIndex < 0 || feedIndex >= GazetteCoreFeedCount()) {
        GazetteUISetStatus("No feeds configured.");
        return;
    }

    GazetteUISelectFeed(feedIndex);

    if (GazetteFeedsLoadCache(feedIndex, GazetteCoreFeedURL(feedIndex),
                              PrefsMaxArticles())) {
        GazetteUIArticlesChanged();
        snprintf(message, sizeof message, "%d articles from the last fetch.",
                 GazetteFeedsArticleCount());
        GazetteUISetStatus(message);
        return;
    }

    if (!gNetUp) {
        GazetteUIArticlesChanged();
        GazetteUISetStatus("Nothing cached, and no network - "
                           "check the TCP/IP control panel.");
        return;
    }

    HandleRefresh();
}

static void HandleRefresh(void)
{
    char message[224];
    int  feedIndex = GazetteUISelectedFeed();

    if (GazetteFeedsRefreshGetState() == kGazetteRefreshRunning) {
        return;                     /* one at a time until Phase 4 */
    }
    if (!gNetUp) {
        GazetteUISetStatus("No network - check the TCP/IP control panel.");
        return;
    }
    if (GazetteCoreFeedCount() == 0) {
        GazetteUISetStatus("No feeds configured.");
        return;
    }

    if (!GazetteFeedsRefreshStart(feedIndex, GazetteCoreFeedURL(feedIndex),
                                  PrefsMaxArticles())) {
        snprintf(message, sizeof message, "Failed: %s",
                 GazetteFeedsRefreshErrorText());
        GazetteUISetStatus(message);
        return;
    }

    snprintf(message, sizeof message, "Fetching %s\311",
             GazetteCoreFeedTitle(feedIndex));
    GazetteUISetStatus(message);
}

static void PumpRefresh(void)
{
    static int lastProgress = -1;
    char       message[224];

    if (GazetteFeedsRefreshGetState() != kGazetteRefreshRunning) {
        return;
    }

    switch (GazetteFeedsRefreshPump()) {
        case kGazetteRefreshDone:
            gLastRefreshTicks = TickCount();
            lastProgress      = -1;
            GazetteUIArticlesChanged();
            snprintf(message, sizeof message, "%d articles from %s",
                     GazetteFeedsArticleCount(),
                     GazetteFeedsTitle()[0]
                         ? GazetteFeedsTitle()
                         : GazetteCoreFeedTitle(GazetteUISelectedFeed()));
            GazetteUISetStatus(message);
            break;

        case kGazetteRefreshFailed:
            gLastRefreshTicks = TickCount();
            lastProgress      = -1;
            snprintf(message, sizeof message, "Failed: %s",
                     GazetteFeedsRefreshErrorText());
            GazetteUISetStatus(message);
            break;

        case kGazetteRefreshRunning: {
            /* Only while headlines are actually arriving: a status line
               rewritten on every pass would repaint many times a second to
               say the same thing. */
            int progress = GazetteFeedsRefreshProgress();

            if (progress != lastProgress) {
                lastProgress = progress;
                if (progress > 0) {
                    snprintf(message, sizeof message,
                             "Reading\311 %d articles", progress);
                    GazetteUISetStatus(message);
                }
            }
            break;
        }

        default:
            break;
    }
}

/*
 * Automatic refresh. Deliberately modest: it refreshes the feed being looked
 * at, and only that one, on the interval in the preferences. Walking the
 * whole list in the background would be a queue, several connections and a
 * policy about what to do when one fails — Phase 4's problem, not this one.
 */
static void CheckAutoRefresh(void)
{
    const GazettePrefs *prefs = GazetteCoreGetPrefs();
    unsigned long       interval;

    if (prefs == nil || prefs->refreshMinutes <= 0) {
        return;                     /* 0 means manual only */
    }
    if (!gNetUp || GazetteFeedsRefreshGetState() == kGazetteRefreshRunning) {
        return;
    }
    if (gLastRefreshTicks == 0) {
        gLastRefreshTicks = TickCount();
        return;
    }

    interval = (unsigned long)prefs->refreshMinutes * 60UL * 60UL;
    if (TickCount() - gLastRefreshTicks < interval) {
        return;
    }

    gLastRefreshTicks = TickCount();
    HandleRefresh();
}

/* ------------------------------------------------------------------ */
/* Cleanup                                                             */
/* ------------------------------------------------------------------ */

static void DoExitGazette(void)
{
    /* A refresh in flight must not outlive the application: cancelling it
       closes the connection and frees the parser. */
    GazetteFeedsRefreshCancel();

    GazetteUIClose();

    /* Writes the preferences back out if anything changed them — including a
       first run, which saves the defaults so there is a file to hand-edit. */
    GazetteCoreShutdown();
    GazetteNetShutdown();
}
