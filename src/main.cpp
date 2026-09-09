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
#include <DateTimeUtils.h>
#include <Appearance.h>
#include <Sound.h>

#include <stdio.h>
#include <string.h>

#include "core/gazette_core.h"
#include "feeds/gazette_feeds.h"
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
static void    PumpRefresh(void);
static void    SetStatus(const char *text);
static void    HandleScrollKey(short key);
static short   VisibleRows(WindowRef window);
static void    DrawGazetteWindow(WindowRef window);
static void    DrawCString(const char *text);
static short   StringWidthC(const char *text);

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

/* First article drawn. Phase 3 replaces this with a real scrolling list
   control; until then the arrow and page keys move it, which is enough to
   read a hundred headlines on real hardware. */
static int       gScrollTop = 0;

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

/*
 * Seconds between the Macintosh epoch (1 January 1904) and the Unix one
 * (1 January 1970). GetDateTime counts from the former; every date the feed
 * parser produces counts from the latter, and the list draws both.
 */
enum {
    kMacToUnixEpoch = 2082844800L
};

/* Article list metrics, in pixels. Geneva 9 is what Newsstand listed
   headlines in, and 12 points of leading is what it gave them. */
enum {
    kListTop     = 46,
    kListLeft    = 16,
    kRowHeight   = 12,
    kListBottom  = 26,      /* space kept clear for the status line */
    kDateColumn  = 84       /* headline starts here, after the date */
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
            PumpRefresh();
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
               where the list's own keys are handled. */
            long choice = (long)MenuEvent(event);

            if (choice != 0) {
                HandleMenuChoice(choice);
            } else if ((event->modifiers & cmdKey) == 0) {
                HandleScrollKey((short)(event->message & charCodeMask));
            }
            break;
        }

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

static void HandleRefresh(void)
{
    const GazettePrefs *prefs;
    char                message[160];

    if (GazetteFeedsRefreshGetState() == kGazetteRefreshRunning) {
        return;                     /* one at a time until Phase 3 */
    }
    if (!gNetUp) {
        SetStatus("No network - check the TCP/IP control panel.");
        return;
    }
    if (GazetteCoreFeedCount() == 0) {
        SetStatus("No feeds configured.");
        return;
    }

    prefs = GazetteCoreGetPrefs();

    if (!GazetteFeedsRefreshStart(GazetteCoreFeedURL(0),
                                  prefs ? prefs->maxArticles : 0)) {
        snprintf(message, sizeof message, "Failed: %s",
                 GazetteFeedsRefreshErrorText());
        SetStatus(message);
        return;
    }

    gScrollTop = 0;
    snprintf(message, sizeof message, "Fetching %s...", GazetteCoreFeedTitle(0));
    SetStatus(message);
}

static void PumpRefresh(void)
{
    static int lastProgress = -1;
    char       message[160];

    if (GazetteFeedsRefreshGetState() != kGazetteRefreshRunning) {
        return;
    }

    switch (GazetteFeedsRefreshPump()) {
        case kGazetteRefreshDone:
            snprintf(message, sizeof message, "%d articles from %s",
                     GazetteFeedsArticleCount(),
                     GazetteFeedsTitle()[0] ? GazetteFeedsTitle()
                                            : GazetteCoreFeedTitle(0));
            SetStatus(message);
            lastProgress = -1;
            InvalWholeWindow(gMainWindow);
            break;

        case kGazetteRefreshFailed:
            snprintf(message, sizeof message, "Failed: %s",
                     GazetteFeedsRefreshErrorText());
            SetStatus(message);
            lastProgress = -1;
            InvalWholeWindow(gMainWindow);
            break;

        case kGazetteRefreshRunning: {
            /* Only while headlines are actually arriving: a status line
               rewritten on every pass would repaint the window many times a
               second to say the same thing. */
            int progress = GazetteFeedsRefreshProgress();

            if (progress != lastProgress) {
                lastProgress = progress;
                if (progress > 0) {
                    snprintf(message, sizeof message,
                             "Reading... %d articles", progress);
                    SetStatus(message);
                }
            }
            break;
        }

        default:
            break;
    }
}

/* ------------------------------------------------------------------ */
/* Scrolling                                                           */
/* ------------------------------------------------------------------ */

static short VisibleRows(WindowRef window)
{
    Rect  bounds;
    short usable;

    if (window == nil) {
        return 1;
    }
    GetWindowPortBounds(window, &bounds);
    usable = (short)(bounds.bottom - bounds.top - kListTop - kListBottom);
    if (usable < kRowHeight) {
        return 1;
    }
    return (short)(usable / kRowHeight);
}

static void HandleScrollKey(short key)
{
    int count = GazetteFeedsArticleCount();
    int page  = VisibleRows(gMainWindow);
    int top   = gScrollTop;
    int limit = count - page;

    if (limit < 0) {
        limit = 0;
    }

    switch (key) {
        case 0x1E: top -= 1;    break;      /* up arrow    */
        case 0x1F: top += 1;    break;      /* down arrow  */
        case 0x0B: top -= page; break;      /* page up     */
        case 0x0C: top += page; break;      /* page down   */
        case 0x01: top  = 0;    break;      /* home        */
        case 0x04: top  = limit; break;     /* end         */
        default:   return;
    }

    if (top > limit) top = limit;
    if (top < 0)     top = 0;

    if (top != gScrollTop) {
        gScrollTop = top;
        InvalWholeWindow(gMainWindow);
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
/* TextWidth() over a C string, for right-aligning. Same length clamp as
   DrawCString, and the same reason. */
static short StringWidthC(const char *text)
{
    size_t len;

    if (text == nil) {
        return 0;
    }
    len = strlen(text);
    if (len > 32767) {
        len = 32767;
    }
    return (len == 0) ? 0 : TextWidth(text, 0, (short)len);
}

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
    short   rows;
    int     count;
    int     i;
    unsigned long macNow = 0;
    long          unixNow;

    GetPort(&savePort);
    SetPortWindowPort(window);

    GetWindowPortBounds(window, &bounds);
    EraseRect(&bounds);

    count = GazetteFeedsArticleCount();

    /* GetDateTime counts from the Macintosh epoch and fills a pointer; the
       parser's dates count from the Unix one. Read it once for the whole
       list rather than once a row. */
    GetDateTime(&macNow);
    unixNow = (long)macNow - kMacToUnixEpoch;

    /* Header: the feed's own title if it gave one, else the name from the
       preferences file. */
    TextFont(systemFont);
    TextSize(12);
    MoveTo((short)(bounds.left + kListLeft), (short)(bounds.top + 24));
    if (count > 0) {
        DrawCString(GazetteFeedsTitle()[0] ? GazetteFeedsTitle()
                                           : GazetteCoreFeedTitle(0));
    } else if (GazetteCoreFeedCount() > 0) {
        DrawCString(GazetteCoreFeedTitle(0));
    } else {
        DrawString("\pNo feeds configured");
    }

    /* A hairline under the header, the way a Platinum list is ruled off. */
    MoveTo((short)(bounds.left + kListLeft), (short)(bounds.top + 32));
    LineTo((short)(bounds.right - kListLeft), (short)(bounds.top + 32));

    /* Headlines. Geneva 9 is what Newsstand listed them in. */
    TextFont(kFontIDGeneva);
    TextSize(9);

    rows = VisibleRows(window);
    line = (short)(bounds.top + kListTop);

    if (count == 0) {
        MoveTo((short)(bounds.left + kListLeft), line);
        if (gNetUp) {
            DrawString("\pPress Command-R to fetch headlines.");
        } else {
            DrawString("\pNo network - check the TCP/IP control panel.");
        }
    }

    for (i = gScrollTop; i < count && i < gScrollTop + rows; i++) {
        const GazetteArticle *a = GazetteFeedsArticleAt(i);
        char                  when[16];

        if (a == NULL) {
            break;
        }

        /* The date column is fixed so the headlines line up; a missing date
           simply leaves it blank rather than shifting the row. GetDateTime
           gives "now" so a stale article can be shown with its year. */
        GazetteFormatDate(a->date, unixNow, when, sizeof when);
        if (when[0] != '\0') {
            MoveTo((short)(bounds.left + kListLeft), line);
            DrawCString(when);
        }

        MoveTo((short)(bounds.left + kListLeft + kDateColumn), line);
        DrawCString(a->title);

        line = (short)(line + kRowHeight);
    }

    /* Status line, and the position in the list when it does not all fit. */
    TextFont(systemFont);
    TextSize(12);

    MoveTo((short)(bounds.left + kListLeft), (short)(bounds.bottom - 10));
    if (gStatus[0] != '\0') {
        DrawCString(gStatus);
    } else if (gNetUp) {
        DrawString("\pNetwork ready - press Command-R to fetch headlines.");
    } else {
        DrawString("\pNo network - check the TCP/IP control panel.");
    }

    if (count > rows) {
        char position[48];

        snprintf(position, sizeof position, "%d-%d of %d",
                 gScrollTop + 1,
                 (gScrollTop + rows < count) ? gScrollTop + rows : count,
                 count);
        MoveTo((short)(bounds.right - kListLeft - StringWidthC(position)),
               (short)(bounds.bottom - 10));
        DrawCString(position);
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
    /* A refresh in flight must not outlive the application: cancelling it
       closes the connection and frees the parser. */
    GazetteFeedsRefreshCancel();

    GazetteCoreShutdown();
    GazetteNetShutdown();

    if (gMainWindow != nil) {
        DisposeWindow(gMainWindow);
        gMainWindow = nil;
    }
}
