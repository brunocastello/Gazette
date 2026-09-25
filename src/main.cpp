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
#include <Icons.h>              /* PlotIconID, for the About window */
#include <MacWindows.h>
#include <Menus.h>
#include <Dialogs.h>
#include <Events.h>
#include <Appearance.h>
#include <Sound.h>
#include <AppleEvents.h>
#include <AERegistry.h>
#include <MacMemory.h>
#include <TextUtils.h>
#include <InternetConfig.h>     /* ICLaunchURL, for Open in Browser */
#include <Scrap.h>              /* the clipboard, for Copy Feed URL */

#include <stdio.h>
#include <string.h>

#include "gazette_version.h"
#include "core/gazette_core.h"
#include "feeds/gazette_feeds.h"
#include "feeds/gazette_index.h"
#include "feeds/gazette_photos.h"
#include "portable/gazette_portable.h"
#include "portable/gazette_url.h"
#include "prefs/gazette_opml.h"
#include "store/gazette_store.h"
#include "net/gazette_net.h"
#include "ui/gazette_dialogs.h"
#include "app/gazette_app.h"
#include "ui/platinum_window.h"

/* ------------------------------------------------------------------ */
/* Forward declarations                                                */
/* ------------------------------------------------------------------ */

static Boolean InitGazette(void);
static Boolean BuildMenuBar(void);
static void    InstallAppleEventHandlers(void);
static void    RunGazette(void);
static void    PumpNetwork(void);
static void    DoExitGazette(void);

static void    HandleEvent(const EventRecord *event);
static void    HandleMouseDown(const EventRecord *event);
static void    HandleMenuChoice(long menuResult);

static void    HandleAbout(void);
static void    HandleQuit(void);
static void    HandlePreferences(void);

static void    AdjustMenus(void);
static void    HandleNewFeed(void);
static void    HandleNewGroup(void);
static void    HandleEditFeed(void);
static void    HandleEditGroup(void);
static void    HandleEdit(void);
static void    HandleRemove(void);
static void    HandleToggleEnabled(void);
static void    HandleOpenHomePage(void);
static void    HandleCopyFeedURL(void);
static void    HandleCopyHomeURL(void);
static Boolean OpenURL(const char *url);
static void    AdjustMarkAllItem(MenuRef menu, MenuItemIndex item);
static void    AdjustRefreshItem(MenuRef menu, MenuItemIndex item);
static void    ShowSidebarContextMenu(int kind, int index, Point global);
static void    ShowArticleContextMenu(int index, Point global);
static void    HandleCopyArticleURL(void);
static void    HandleOpenInBrowser(void);
static void    HandleHideSidebar(void);
static void    HandleHideToolbar(void);
static void    RememberWindowLayout(void);
static void    HandleSearch(void);
static void    ToolbarCommand(int command);
static void    HandleFind(void);

/* ------------------------------------------------------------------ */
/* Application globals                                                 */
/* ------------------------------------------------------------------ */

static Boolean gDone  = false;

/* Menu IDs */
enum {
    kMenuApple   = 128,
    kMenuFile    = 129,
    kMenuEdit    = 130,
    kMenuView    = 131,
    kMenuFeeds   = 132,
    kMenuArticle = 133,

    /* The hierarchical menus. Their IDs have to be unique among menus and
       nothing more; none of them sits in the bar. 136 is the toolbar's New
       menu, over in platinum_window.c. */

    /*
     * The contextual menus — one per kind of sidebar row and one for a
     * headline, built once and adjusted before each showing, the way the
     * bar's are. A standing view gets the two commands that mean anything
     * for it; a group and a feed get what the Feeds menu offers them, in
     * the order the eye wants it, plus the two things only a feed has: a
     * site to open and addresses to copy. Starred gets no menu at all. A
     * headline gets the Article menu's commands for the one under the
     * mouse, and its address to copy.
     */
    kMenuCtxSmart   = 139,
    kMenuCtxGroup   = 140,
    kMenuCtxFeed    = 142,
    kMenuCtxArticle = 145
};

enum {
    kAppleItemAbout = 1
};

/* Menu item indices, in the order AppendMenu() adds them below. */
enum {
    kFileItemNewFeed  = 1,
    kFileItemNewGroup = 2,
    /* 3 is a divider */
    kFileItemRefresh  = 4,
    /* 5 is a divider */
    kFileItemImport   = 6,
    kFileItemExport   = 7,
    /* 8 is a divider */
    kFileItemQuit     = 9
};

/*
 * Edit menu items. Undo, Cut, Paste and Clear are permanently grey: there is
 * nothing in this window that can be typed into, and a dialog that is up runs
 * its own loop with the menu bar out of reach. They are there because an Edit
 * menu without them reads as broken, not because they will ever do anything.
 * Copy is the exception — the reader pane holds a TextEdit record and a drag
 * in it selects text.
 */
enum {
    /* 1 Undo, 2 divider, 3 Cut, 5 Paste, 6 Clear, 7 divider */
    kEditItemCopy  = 4,
    kEditItemFind  = 8,
    /* 9 is a divider */
    kEditItemPrefs = 10
};

/* View menu items. */
enum {
    kViewItemGroupByFeed = 1,     /* grey until a later phase */
    kViewItemHideRead    = 2,
    kViewItemHideFeeds   = 3,
    kViewItemShowPhotos  = 4,
    /* 5 is a divider */
    kViewItemHideSidebar = 6,
    kViewItemHideToolbar = 7
};

/*
 * Feeds menu items. The three standing views lead it — choosing one here
 * selects its row, so the two ways of reaching them cannot disagree — then
 * Mark All as Read for whatever is showing, then what is done to the
 * selected feed or group. Edit and Delete are named for what is selected:
 * "Edit Feed…" or "Edit Group…", "Delete Feed" or "Delete Group". There is
 * no Rename, because editing is renaming.
 */
enum {
    kFeedsItemToday     = 1,
    kFeedsItemAllUnread = 2,
    kFeedsItemStarred   = 3,
    /* 4 is a divider */
    kFeedsItemSort      = 5,      /* Show Oldest First / Show Newest First */
    kFeedsItemMarkAll   = 6,
    /* 7 is a divider */
    kFeedsItemEdit      = 8,
    kFeedsItemEnabled   = 9,
    /* 10 is a divider */
    kFeedsItemDelete    = 11
};

/* The contextual menus' items. Each begins with Refresh, then Mark All as
   Read: what is done to a row most often, nearest the mouse. */
enum {
    kCtxSmartRefresh = 1,
    kCtxSmartSort    = 2,
    kCtxSmartMarkAll = 3
};
enum {
    kCtxGroupRefresh = 1,
    kCtxGroupSort    = 2,
    kCtxGroupMarkAll = 3,
    /* 4 is a divider */
    kCtxGroupEdit    = 5,
    kCtxGroupEnabled = 6,
    /* 7 is a divider */
    kCtxGroupDelete  = 8
};
enum {
    kCtxFeedRefresh  = 1,
    kCtxFeedSort     = 2,
    kCtxFeedMarkAll  = 3,
    /* 4 is a divider */
    kCtxFeedHome     = 5,
    /* 6 is a divider */
    kCtxFeedCopyURL  = 7,
    kCtxFeedCopyHome = 8,
    /* 9 is a divider */
    kCtxFeedEdit     = 10,
    kCtxFeedEnabled  = 11,
    /* 12 is a divider */
    kCtxFeedDelete   = 13
};
enum {
    kCtxArticleMarkRead  = 1,
    kCtxArticleStar      = 2,
    kCtxArticleMarkAbove = 3,
    kCtxArticleMarkBelow = 4,
    /* 5 is a divider */
    kCtxArticleCopyURL   = 6,
    /* 7 is a divider */
    kCtxArticleBrowser   = 8
};

/* Article menu items: what is done to the article that is open, and the
   step to the next one that is not read. */
enum {
    kArticleItemNextUnread = 1,
    /* 2 is a divider */
    kArticleItemMarkRead   = 3,
    kArticleItemMarkAbove  = 4,
    kArticleItemMarkBelow  = 5,
    /* 6 is a divider */
    kArticleItemStar       = 7,
    /* 8 is a divider */
    kArticleItemBrowser    = 9
};

/*
 * The About window: the application icon, then alternating Charcoal and
 * Geneva lines for the name, the author and the credits, in a document
 * window with a close box and no OK button — the Mac OS 9 convention, and
 * SimpleText's — laid out the way Gateway's is.
 */
enum {
    kAboutWidth  = 280,
    kAboutHeight = 230,
    kAboutIconID = 128,             /* the Finder icon's family */
    kFontGeneva  = 3
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

    /*
     * Before any control, menu or window exists. Without this the Appearance
     * Manager hands the application the *pre-Appearance* control definitions
     * — System 7's — and every scroll bar, list frame and placard draws flat
     * and square instead of Platinum. It is the one call that decides whether
     * the whole interface looks like Mac OS 9 or like 1991, and it was
     * missing: the scroll bars looked hand-drawn because in effect they were
     * being drawn by a CDEF that predates the theme.
     *
     * Carbon applications are documented as being Appearance clients
     * implicitly, but that is Mac OS X. On Mac OS 9 CarbonLib sits on top of
     * the real Appearance Manager and the registration is still wanted.
     */
    (void)RegisterAppearanceClient();

    /* And the Contextual Menu Manager's client too: without this, on Mac OS
       8 and 9, IsShowContextualMenuClick never says yes. */
    (void)InitContextualMenus();

    /* Preferences before anything is drawn: the window's first update event
       already wants the feed list. */
    (void)GazetteCoreInit();

    /* And the index before that, because the first thing drawn is a sidebar
       with unread counts in it. */
    GazetteIndexLoad();

    /* The store keeps its own copy of what the View menu is holding, because
       it is the store that shapes the list. Handing it over here is what
       makes the two agree before the first article is loaded. */
    GazetteFeedsSetOldestFirst(GazetteCoreOldestFirst() ? 1 : 0);
    GazetteFeedsSetHideRead(GazetteCoreHideReadArticles() ? 1 : 0);

    /* Open Transport before the window, because InitOpenTransport can put up
       a dialog of its own if TCP/IP needs loading and should not do that over
       a half-drawn window. Failure is not fatal — the cache still reads. */
    (void)GazetteNetInit();

    if (!BuildMenuBar()) {
        return false;
    }

    InstallAppleEventHandlers();

    if (!GazetteUIOpen(GazetteAppShowFeed, GazetteAppShowArticle,
                       GazetteAppShowGroup, GazetteAppShowSmart,
                       ToolbarCommand)) {
        return false;
    }

    /* A modal dialog and a Navigation Services dialog each run a loop of
       their own, and this is what keeps a fetch moving inside them. */
    GazetteDialogsSetIdle(GazetteAppPumpRefresh);
    GazetteStoreSetIdle(GazetteAppPumpRefresh);

    /* Show whatever the last run left cached, so the window has content
       before any network work happens — which on a machine with no
       connection is the whole of what Gazette can do. */
    GazetteAppShowFeed(0);

    return true;
}

/* ------------------------------------------------------------------ */
/* Menu bar — built programmatically, no MBAR/MENU resources needed     */
/*                                                                     */
/* AppendMenu() metacharacters: ';' separates items, '(' disables one,  */
/* a lone '-' is a divider, and '/X' assigns a command key.             */
/* ------------------------------------------------------------------ */

/*
 * The command keys the menus below name, and the three that carry a modifier
 * as well. Mac OS 9's Menu Manager holds the extra modifiers separately from
 * the character, which is what SetMenuItemModifiers is for; AppendMenu's "/X"
 * can only ever say Command.
 *
 * The pairs are chosen so that the second of each pair is the first with
 * Shift on it and means a narrower version of the same thing: Hide Read
 * Articles and Hide Read Feeds, Mark All as Read and the two halves of the
 * list either side of the article being read.
 */
static void SetShiftKey(MenuRef menu, short item)
{
    SetMenuItemModifiers(menu, (MenuItemIndex)item,
                         (UInt8)(kMenuShiftModifier));
}

static void SetOptionKey(MenuRef menu, short item)
{
    SetMenuItemModifiers(menu, (MenuItemIndex)item,
                         (UInt8)(kMenuOptionModifier));
}

static Boolean BuildMenuBar(void)
{
    MenuRef appleMenu, fileMenu, editMenu, viewMenu;
    MenuRef feedsMenu, articleMenu;
    MenuRef ctx;

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
    AppendMenu(fileMenu,
               "\pNew Feed\311/N;New Group\311/G;(-;"
               "Refresh/R;(-;"
               "Import Feeds\311;Export Feeds\311/E;(-;"
               "Quit/Q");
    InsertMenu(fileMenu, 0);

    editMenu = NewMenu(kMenuEdit, "\pEdit");
    if (editMenu == nil) {
        return false;
    }
    /* The four inert ones are disabled here rather than in AdjustMenus: they
       are never enabled, so there is nothing for AdjustMenus to decide. */
    AppendMenu(editMenu,
               "\p(Undo/Z;(-;(Cut/X;Copy/C;(Paste/V;(Clear;(-;"
               "Find\311/F;(-;Preferences\311");
    /* Command-semicolon, set afterwards: a semicolon in AppendMenu's text
       is the character that separates items. */
    SetItemCmd(editMenu, kEditItemPrefs, ';');
    InsertMenu(editMenu, 0);

    viewMenu = NewMenu(kMenuView, "\pView");
    if (viewMenu == nil) {
        return false;
    }
    AppendMenu(viewMenu,
               "\p(Group by Feed;Hide Read Articles/H;Hide Read Feeds/H;"
               "Show Photos;(-;"
               "Hide Sidebar/S;Hide Toolbar/T");
    SetShiftKey(viewMenu, kViewItemHideFeeds);
    InsertMenu(viewMenu, 0);

    feedsMenu = NewMenu(kMenuFeeds, "\pFeeds");
    if (feedsMenu == nil) {
        return false;
    }
    /* No Move: a feed or a group is moved by dragging it, which is the
       one gesture that can say where. */
    /* The sort order stands before Mark All as Read, as it does after
       Refresh in the contextual menus. The item names the order it would
       switch to; AdjustMenus keeps it current. */
    AppendMenu(feedsMenu,
               "\pToday/1;All Unread/2;Starred/3;(-;"
               "Show Oldest First;Mark All as Read/K;(-;"
               "Edit Feed\311;Turn Off;(-;"
               "Delete Feed");
    InsertMenu(feedsMenu, 0);

    /* The contextual menus. In the hierarchical list, which is where
       PopUpMenuSelect wants a menu it is handed. */
    ctx = NewMenu(kMenuCtxSmart, "\p");
    if (ctx == nil) {
        return false;
    }
    AppendMenu(ctx, "\pRefresh;Show Oldest First;Mark All as Read");
    InsertMenu(ctx, hierMenu);

    ctx = NewMenu(kMenuCtxGroup, "\p");
    if (ctx == nil) {
        return false;
    }
    AppendMenu(ctx, "\pRefresh;Show Oldest First;Mark All as Read;(-;"
                    "Edit Group\311;Turn Off;(-;Delete Group");
    InsertMenu(ctx, hierMenu);

    ctx = NewMenu(kMenuCtxFeed, "\p");
    if (ctx == nil) {
        return false;
    }
    AppendMenu(ctx, "\pRefresh;Show Oldest First;Mark All as Read;(-;"
                    "Open Home Page;(-;"
                    "Copy Feed URL;Copy Home Page URL;(-;"
                    "Edit Feed\311;Turn Off;(-;Delete Feed");
    InsertMenu(ctx, hierMenu);

    ctx = NewMenu(kMenuCtxArticle, "\p");
    if (ctx == nil) {
        return false;
    }
    AppendMenu(ctx, "\pMark as Read;Star Article;Mark Above as Read;"
                    "Mark Below as Read;(-;Copy Article URL;(-;"
                    "Open in Browser");
    InsertMenu(ctx, hierMenu);

    articleMenu = NewMenu(kMenuArticle, "\pArticle");
    if (articleMenu == nil) {
        return false;
    }
    AppendMenu(articleMenu,
               "\pNext Unread//;(-;"
               "Mark as Unread/U;Mark Above as Read/K;Mark Below as Read/K;(-;"
               "Star Article/L;(-;"
               "Open in Browser/B");
    SetShiftKey(articleMenu, kArticleItemMarkAbove);
    SetOptionKey(articleMenu, kArticleItemMarkBelow);
    InsertMenu(articleMenu, 0);

    DrawMenuBar();

    return true;
}

/* ------------------------------------------------------------------ */
/* Apple Events                                                        */
/*                                                                     */
/* The four of the required suite, which every Mac OS application is   */
/* expected to answer whether or not it does anything with them. The   */
/* SIZE resource has said isHighLevelEventAware since Phase 0, so the  */
/* system was entitled to send these all along.                        */
/* ------------------------------------------------------------------ */

static pascal OSErr AEQuit(const AppleEvent *event, AppleEvent *reply,
                           SInt32 refCon)
{
    (void)event; (void)reply; (void)refCon;

    /* Not an immediate exit: the loop ends after this pass and the ordinary
       shutdown runs, which is what writes the preferences and the index. */
    gDone = true;
    return noErr;
}

/* Launched, or clicked when already running. There is one window and it is
   already open, so both mean "bring it forward". */
static pascal OSErr AEOpenApp(const AppleEvent *event, AppleEvent *reply,
                              SInt32 refCon)
{
    (void)event; (void)reply; (void)refCon;

    if (GazetteUIWindow() != nil) {
        SelectWindow(GazetteUIWindow());
    }
    return noErr;
}

/* Gazette opens no documents. Answering errAEEventNotHandled is what tells
   the Finder so, rather than leaving it waiting on a reply. */
static pascal OSErr AENotHandled(const AppleEvent *event, AppleEvent *reply,
                                 SInt32 refCon)
{
    (void)event; (void)reply; (void)refCon;

    return errAEEventNotHandled;
}

static void InstallAppleEventHandlers(void)
{
    /* The UPPs outlive this function and are never disposed: they live as
       long as the application does, and the application ending is what
       releases them. */
    AEEventHandlerUPP quitUPP    = NewAEEventHandlerUPP(AEQuit);
    AEEventHandlerUPP openUPP    = NewAEEventHandlerUPP(AEOpenApp);
    AEEventHandlerUPP ignoredUPP = NewAEEventHandlerUPP(AENotHandled);

    if (quitUPP != nil) {
        (void)AEInstallEventHandler(kCoreEventClass, kAEQuitApplication,
                                    quitUPP, 0, false);
    }
    if (openUPP != nil) {
        (void)AEInstallEventHandler(kCoreEventClass, kAEOpenApplication,
                                    openUPP, 0, false);
        (void)AEInstallEventHandler(kCoreEventClass, kAEReopenApplication,
                                    openUPP, 0, false);
    }
    if (ignoredUPP != nil) {
        (void)AEInstallEventHandler(kCoreEventClass, kAEOpenDocuments,
                                    ignoredUPP, 0, false);
        (void)AEInstallEventHandler(kCoreEventClass, kAEPrintDocuments,
                                    ignoredUPP, 0, false);
    }
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
            PumpNetwork();
        }
        /* Either way, the window has a look at where the mouse is: the
           toolbar's buttons answer it without an event of their own. */
        GazetteUIIdle();
    }
}

/*
 * Idle. This is the one place network I/O advances, and nothing it calls
 * blocks: a pump does whatever work is available this pass and returns.
 * Blocking here would stop the whole machine cooperating, not just Gazette.
 * The About window's own loop calls it too, so a picture half-fetched when
 * About opens is not left with its connection standing still.
 */
static void PumpNetwork(void)
{
    GazetteAppPumpNetwork();
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
            long choice;

            /* Before the lookup, not after: a command key for a disabled item
               must not fire, and what is disabled depends on the selection. */
            AdjustMenus();
            choice = (long)MenuEvent(event);

            if (choice != 0) {
                HandleMenuChoice(choice);
            } else if ((event->modifiers & cmdKey) == 0) {
                (void)GazetteUIKey((short)(event->message & charCodeMask),
                                   event->modifiers);
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

        case kHighLevelEvent:
            /*
             * The required suite arrives this way and nowhere else. Without
             * this branch the events were simply dropped, so anything that
             * asks Gazette to quit rather than clicking its close box — a
             * dock, the Finder at shutdown, an AppleScript — was ignored.
             */
            (void)AEProcessAppleEvent(event);
            break;

        case activateEvt:
            if ((WindowRef)event->message == GazetteUIWindow()) {
                GazetteUIActivate((event->modifiers & activeFlag) != 0);
                GazetteUIResized();     /* redraws, and re-hilites the frames */
            }
            break;

        case osEvt:
            /*
             * Switching to another application and back is not an
             * activateEvt. That one is for a window changing places with
             * another of *this* application's windows; the whole application
             * going to the back and coming forward again arrives here, as a
             * suspend or a resume.
             *
             * Without this branch the window was deactivated on the way out
             * — the Window Manager does that much itself — and nothing ever
             * told it it had come back. So its controls stayed greyed, and a
             * scroll bar greyed with nothing to scroll draws as an empty
             * track and reads as having gone away.
             *
             * The message's high byte says which kind of osEvt this is;
             * mouse-moved events arrive the same way and are not this.
             */
            if (((event->message >> 24) & 0xFF) == suspendResumeMessage &&
                GazetteUIWindow() != nil) {
                Boolean resumed =
                    (Boolean)((event->message & resumeFlag) != 0);

                GazetteUIActivate(resumed);
                if (resumed) {
                    GazetteUIResized();
                }
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
            AdjustMenus();
            HandleMenuChoice(MenuSelect(event->where));
            break;

        case inContent:
            if (window != FrontWindow()) {
                SelectWindow(window);
            } else if (window == GazetteUIWindow()) {
                Point local = event->where;
                int   kind  = 0;
                int   index = 0;

                SetPortWindowPort(window);
                GlobalToLocal(&local);

                /* Control-click, or a second mouse button that the mouse's
                   driver reports as one: a menu for the sidebar row it is
                   over, and nothing for anywhere else. */
                if (IsShowContextualMenuClick(event)) {
                    if (GazetteUISidebarRowAt(local, &kind, &index)) {
                        ShowSidebarContextMenu(kind, index, event->where);
                    } else if (GazetteUIArticleRowAt(local, &index)) {
                        ShowArticleContextMenu(index, event->where);
                    }
                    break;
                }
                GazetteUIClick(local, event->modifiers);
                /* A click may have been a divider drag. */
                RememberWindowLayout();
            }
            break;

        case inDrag:
            /* A NULL bounding box means "the whole desktop"; that is legal
               from CarbonLib 1.0 forward. */
            DragWindow(window, event->where, nil);
            RememberWindowLayout();
            break;

        case inGrow: {
            /* GrowWindow/SizeWindow rather than ResizeWindow: the pair is
               available all the way back to CarbonLib 1.0. */
            Rect limits;
            long newSize;

            SetRect(&limits, kGazetteMinWindowWidth, kGazetteMinWindowHeight,
                    32767, 32767);
            newSize = GrowWindow(window, event->where, &limits);
            if (newSize != 0) {
                SizeWindow(window, (short)(newSize & 0xFFFF),
                           (short)(newSize >> 16), true);
                GazetteUIResized();
                RememberWindowLayout();
            }
            break;
        }

        case inZoomIn:
        case inZoomOut:
            if (TrackBox(window, event->where, part)) {
                ZoomWindow(window, part, true);
                GazetteUIResized();
                RememberWindowLayout();
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
            switch (menuItem) {
                case kFileItemNewFeed:  HandleNewFeed();    break;
                case kFileItemNewGroup: HandleNewGroup();   break;
                case kFileItemRefresh:  GazetteAppRefreshAll(); break;
                case kFileItemImport:   GazetteAppImportOPML(); break;
                case kFileItemExport:   GazetteAppExportOPML(); break;
                case kFileItemQuit:     HandleQuit();       break;
                default: break;
            }
            break;

        case kMenuEdit:
            /* Undo, Cut, Paste and Clear are never enabled, so they never
               arrive here. Copy is the window's own: the reader pane holds a
               TextEdit record and a drag in it selects text. */
            if (menuItem == kEditItemCopy) {
                GazetteUIReaderCopy();
            } else if (menuItem == kEditItemPrefs) {
                HandlePreferences();
            } else if (menuItem == kEditItemFind) {
                HandleFind();
            }
            break;

        case kMenuView:
            switch (menuItem) {
                case kViewItemHideRead:    GazetteAppHideReadArticles(); break;
                case kViewItemHideFeeds:   GazetteAppHideReadFeeds();    break;
                case kViewItemShowPhotos:  GazetteAppShowPhotos();       break;
                case kViewItemHideSidebar: HandleHideSidebar();      break;
                case kViewItemHideToolbar: HandleHideToolbar();      break;
                default: break;
            }
            break;

        case kMenuFeeds:
            switch (menuItem) {
                case kFeedsItemToday:
                    GazetteUISelectSmart(kGazetteSmartToday);
                    break;
                case kFeedsItemAllUnread:
                    GazetteUISelectSmart(kGazetteSmartUnread);
                    break;
                case kFeedsItemStarred:
                    GazetteUISelectSmart(kGazetteSmartStarred);
                    break;
                case kFeedsItemMarkAll:  GazetteAppMarkAllRead();   break;
                case kFeedsItemSort:
                    GazetteAppSortOrder((Boolean)!GazetteCoreOldestFirst());
                    break;
                case kFeedsItemEdit:     HandleEdit();          break;
                case kFeedsItemEnabled:  HandleToggleEnabled(); break;
                case kFeedsItemDelete:   HandleRemove();        break;
                default: break;
            }
            break;

        /* The sidebar's contextual menus. Each item is a menu bar item's
           handler and nothing else, for the reason the toolbar's are. */
        case kMenuCtxSmart:
            switch (menuItem) {
                case kCtxSmartRefresh: GazetteAppRefreshSelection(); break;
                case kCtxSmartSort:
                    GazetteAppSortOrder((Boolean)!GazetteCoreOldestFirst());
                    break;
                case kCtxSmartMarkAll: GazetteAppMarkAllRead(); break;
                default: break;
            }
            break;

        case kMenuCtxGroup:
            switch (menuItem) {
                case kCtxGroupRefresh: GazetteAppRefreshSelection(); break;
                case kCtxGroupSort:
                    GazetteAppSortOrder((Boolean)!GazetteCoreOldestFirst());
                    break;
                case kCtxGroupMarkAll: GazetteAppMarkAllRead();     break;
                case kCtxGroupEnabled: HandleToggleEnabled();   break;
                case kCtxGroupEdit:    HandleEditGroup();       break;
                case kCtxGroupDelete:  HandleRemove();          break;
                default: break;
            }
            break;

        case kMenuCtxFeed:
            switch (menuItem) {
                case kCtxFeedRefresh:  GazetteAppRefreshSelection(); break;
                case kCtxFeedMarkAll:  GazetteAppMarkAllRead();     break;
                case kCtxFeedSort:
                    GazetteAppSortOrder((Boolean)!GazetteCoreOldestFirst());
                    break;
                case kCtxFeedHome:     HandleOpenHomePage();    break;
                case kCtxFeedCopyURL:  HandleCopyFeedURL();     break;
                case kCtxFeedCopyHome: HandleCopyHomeURL();     break;
                case kCtxFeedEnabled:  HandleToggleEnabled();   break;
                case kCtxFeedEdit:     HandleEditFeed();        break;
                case kCtxFeedDelete:   HandleRemove();          break;
                default: break;
            }
            break;

        case kMenuCtxArticle:
            switch (menuItem) {
                case kCtxArticleMarkRead:  GazetteAppMarkRead();       break;
                case kCtxArticleStar:      GazetteAppToggleStar();     break;
                case kCtxArticleMarkAbove: GazetteAppMarkRange(false); break;
                case kCtxArticleMarkBelow: GazetteAppMarkRange(true);  break;
                case kCtxArticleCopyURL:   HandleCopyArticleURL(); break;
                case kCtxArticleBrowser:   HandleOpenInBrowser();  break;
                default: break;
            }
            break;

        case kMenuArticle:
            switch (menuItem) {
                case kArticleItemNextUnread: GazetteAppNextUnread();       break;
                case kArticleItemMarkRead:   GazetteAppMarkRead();         break;
                case kArticleItemMarkAbove:  GazetteAppMarkRange(false);   break;
                case kArticleItemMarkBelow:  GazetteAppMarkRange(true);    break;
                case kArticleItemStar:       GazetteAppToggleStar();       break;
                case kArticleItemBrowser:    HandleOpenInBrowser();    break;
                default: break;
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

static void DrawCentredCString(short centreX, short baseline, const char *text)
{
    short len   = (short)strlen(text);
    short width = TextWidth(text, 0, len);

    MoveTo((short)(centreX - width / 2), baseline);
    DrawText(text, 0, len);
}

static void DrawAboutContent(WindowRef w)
{
    Rect     box;
    Rect     iconRect;
    RGBColor platinum;
    Str255   fontName;
    short    charcoal;
    short    midX;

    SetPortWindowPort(w);
    GetWindowPortBounds(w, &box);

    /* Platinum. The window's own grey rather than white: an About box
       painted white reads as a document rather than as part of the system. */
    platinum.red = platinum.green = platinum.blue = 0xDDDD;
    RGBBackColor(&platinum);
    EraseRect(&box);
    ForeColor(blackColor);

    midX = (short)(box.left + (box.right - box.left) / 2);

    SetRect(&iconRect, (short)(midX - 16), (short)(box.top + 14),
            (short)(midX + 16), (short)(box.top + 46));
    (void)PlotIconID(&iconRect, kAlignNone, kTransformNone, kAboutIconID);

    /* Charcoal is a TrueType face rather than a fixed classic font ID, so it
       is looked up by name; GetFNum answers 0 — the system font — when it is
       not installed, which lands on the right answer. */
    CopyCStringToPascal("Charcoal", fontName);
    GetFNum(fontName, &charcoal);
    TextFace(normal);

    TextFont(charcoal);
    TextSize(12);
    DrawCentredCString(midX, (short)(box.top + 64),
                       "Gazette " GAZETTE_VERSION_STRING);

    TextFont(kFontGeneva);
    TextSize(10);
    DrawCentredCString(midX, (short)(box.top + 84),
                       "An RSS and Atom reader for Mac OS 9");

    TextFont(charcoal);
    TextSize(12);
    DrawCentredCString(midX, (short)(box.top + 112), "Bruno Castello");

    TextFont(kFontGeneva);
    TextSize(10);
    DrawCentredCString(midX, (short)(box.top + 132), "bfcastello@hotmail.com");

    TextFont(charcoal);
    TextSize(12);
    DrawCentredCString(midX, (short)(box.top + 160), "Engineer: Claude Opus 5");

    TextFont(kFontGeneva);
    TextSize(10);
    DrawCentredCString(midX, (short)(box.top + 188),
                       "\251 Castello Designs, 2026");
    DrawCentredCString(midX, (short)(box.top + 208), "Built with Retro68");
}

/*
 * The box runs its own loop, the way Gateway's does: it stays in front,
 * closes on its close box, Return, Enter or Escape, and lets the main
 * window repaint under it. The network keeps moving while it is up, so a
 * refresh in flight does not notice.
 */
static void HandleAbout(void)
{
    BitMap      screen;
    Rect        bounds;
    Rect        limit;
    Str255      title;
    WindowRef   about;
    EventRecord event;
    Boolean     done = false;
    short       left;
    short       top;

    GetQDGlobalsScreenBits(&screen);
    left = (short)((screen.bounds.right - screen.bounds.left - kAboutWidth) / 2);
    top  = (short)((screen.bounds.bottom - screen.bounds.top - kAboutHeight) / 3);
    SetRect(&bounds, left, top, (short)(left + kAboutWidth),
            (short)(top + kAboutHeight));

    CopyCStringToPascal("About Gazette", title);
    about = NewCWindow(nil, &bounds, title, true, noGrowDocProc,
                       (WindowRef)-1L, true, 0);
    if (about == nil) {
        return;
    }
    SelectWindow(about);

    while (!done && !gDone) {
        (void)WaitNextEvent(everyEvent, &event, kSleepTicks, nil);

        switch (event.what) {
            case updateEvt:
                if ((WindowRef)event.message == about) {
                    BeginUpdate(about);
                    DrawAboutContent(about);
                    EndUpdate(about);
                } else {
                    HandleEvent(&event);
                }
                break;

            case keyDown:
            case autoKey: {
                char c = (char)(event.message & charCodeMask);

                if (c == '\r' || c == 3 || c == 27) {
                    done = true;
                }
                break;
            }

            case mouseDown: {
                WindowRef win = nil;
                short     part = FindWindow(event.where, &win);

                if (win != about) {
                    break;                  /* About stays in front */
                }
                if (part == inGoAway) {
                    if (TrackGoAway(about, event.where)) {
                        done = true;
                    }
                } else if (part == inDrag) {
                    limit = screen.bounds;
                    InsetRect(&limit, 4, 4);
                    DragWindow(about, event.where, &limit);
                }
                break;
            }

            default:
                break;
        }

        PumpNetwork();
    }

    DisposeWindow(about);
}

static void HandleQuit(void)
{
    gDone = true;
}

/*
 * Note where the window stands and where its dividers are, so that the next
 * launch opens it the same way. Called after anything that can have moved
 * either — a drag, a grow, a zoom, a click that turned out to be a divider
 * drag. Saves at once, like everything else here, but only when a number
 * has actually changed: most clicks move nothing, and cost nothing here.
 */
static void RememberWindowLayout(void)
{
    WindowRef window = GazetteUIWindow();
    Rect      bounds;
    short     sidebar, list;
    Boolean   changed = false;

    if (window == nil) {
        return;
    }

    /* The content rectangle, in global coordinates — what CreateNewWindow
       takes back on the next launch. */
    if (GetWindowBounds(window, kWindowContentRgn, &bounds) == noErr) {
        if (GazetteCoreSetWindowBounds(bounds.left, bounds.top,
                                       bounds.right - bounds.left,
                                       bounds.bottom - bounds.top)) {
            changed = true;
        }
    }

    GazetteUIColumnWidths(&sidebar, &list);
    if (GazetteCoreSetColumnWidths(sidebar, list)) {
        changed = true;
    }

    if (changed) {
        GazetteCoreSavePrefs();
    }
}

/* ------------------------------------------------------------------ */
/* Feed management                                                     */
/*                                                                     */
/* The dialogs are the Dialog Manager's (ui/gazette_dialogs.h), the     */
/* model is the preferences', and this is the wiring between them: read */
/* the sidebar's selection, ask, mutate, save, redraw.                  */
/*                                                                     */
/* Every one of these saves immediately rather than leaving it to the   */
/* quit. On a cooperative machine the application that is edited and    */
/* then crashed by something else is normal, and a subscription that    */
/* did not survive that is a subscription the user has to remember.     */
/* ------------------------------------------------------------------ */

/*
 * Bring the Feeds menu into line with what the sidebar has selected, and the
 * Article menu with the article that is open. Called just before the menus can
 * be seen — a click in the bar, and a command key — because an item that can
 * do nothing should be grey before it is read, not after it is chosen.
 */
static void AdjustMenus(void)
{
    MenuRef view    = GetMenuHandle(kMenuView);
    MenuRef feeds   = GetMenuHandle(kMenuFeeds);
    MenuRef article = GetMenuHandle(kMenuArticle);
    int     kind    = 0;
    int     index   = 0;
    Boolean any;
    Boolean feedSelected;
    Boolean groupSelected;

    any           = GazetteUISelection(&kind, &index);
    feedSelected  = (any && kind == kGazetteRowFeed);
    groupSelected = (any && kind == kGazetteRowGroup);

    /* ---- View -------------------------------------------------- */
    if (view != nil) {
        MacCheckMenuItem(view, kViewItemHideRead,
                         GazetteCoreHideReadArticles() ? true : false);
        MacCheckMenuItem(view, kViewItemHideFeeds,
                         GazetteCoreHideReadFeeds() ? true : false);
        SetMenuItemText(view, kViewItemShowPhotos,
                        GazetteCoreShowPhotos() ? "\pHide Photos"
                                                : "\pShow Photos");

        /* The item says what it would do, so it reads as one command rather
           than as a check box whose label is only true half the time. */
        if (GazetteCoreHideSidebar()) {
            SetMenuItemText(view, kViewItemHideSidebar, "\pShow Sidebar");
        } else {
            SetMenuItemText(view, kViewItemHideSidebar, "\pHide Sidebar");
        }
        if (GazetteCoreHideToolbar()) {
            SetMenuItemText(view, kViewItemHideToolbar, "\pShow Toolbar");
        } else {
            SetMenuItemText(view, kViewItemHideToolbar, "\pHide Toolbar");
        }
    }

    /* ---- Feeds ------------------------------------------------- */
    if (feeds != nil) {
        Boolean on = true;

        /* Edit and Delete are named for what is selected: a feed's dialog
           and a group's are different dialogs, and the item says which is
           coming; and "Delete Group" is the one that keeps its feeds. */
        SetMenuItemText(feeds, kFeedsItemEdit,
                        groupSelected ? "\pEdit Group\311" : "\pEdit Feed\311");
        SetMenuItemText(feeds, kFeedsItemDelete,
                        groupSelected ? "\pDelete Group" : "\pDelete Feed");
        AdjustMarkAllItem(feeds, kFeedsItemMarkAll);
        SetMenuItemText(feeds, kFeedsItemSort,
                        GazetteCoreOldestFirst() ? "\pShow Newest First"
                                                 : "\pShow Oldest First");
        if (feedSelected || groupSelected) {
            MacEnableMenuItem(feeds, kFeedsItemEdit);
            MacEnableMenuItem(feeds, kFeedsItemDelete);
            MacEnableMenuItem(feeds, kFeedsItemEnabled);
            on = feedSelected ? GazetteCoreFeedEnabled(index)
                              : GazetteCoreGroupEnabled(index);
        } else {
            DisableMenuItem(feeds, kFeedsItemEdit);
            DisableMenuItem(feeds, kFeedsItemDelete);
            DisableMenuItem(feeds, kFeedsItemEnabled);
        }
        SetMenuItemText(feeds, kFeedsItemEnabled,
                        on ? "\pTurn Off" : "\pTurn On");
    }

    /* ---- Article ----------------------------------------------- */
    if (article != nil) {
        int                   at      = GazetteUISelectedArticle();
        const GazetteArticle *open    = GazetteFeedsArticleAt(at);
        int                   count   = GazetteFeedsArticleCount();

        /* The read/unread pair follows the article, not the feed. The first
           item says what it would do, so it reads as one command. */
        if (open != nil) {
            MacEnableMenuItem(article, kArticleItemMarkRead);
            SetMenuItemText(article, kArticleItemMarkRead,
                            open->read ? "\pMark as Unread"
                                       : "\pMark as Read");
            MacEnableMenuItem(article, kArticleItemStar);
            SetMenuItemText(article, kArticleItemStar,
                            open->starred ? "\pUnstar Article"
                                          : "\pStar Article");
        } else {
            DisableMenuItem(article, kArticleItemMarkRead);
            SetMenuItemText(article, kArticleItemMarkRead, "\pMark as Unread");
            DisableMenuItem(article, kArticleItemStar);
            SetMenuItemText(article, kArticleItemStar, "\pStar Article");
        }

        /* Above and below are about where the article sits in the list, so
           the first headline has nothing above it and the last none below. */
        if (open != nil && at > 0) {
            MacEnableMenuItem(article, kArticleItemMarkAbove);
        } else {
            DisableMenuItem(article, kArticleItemMarkAbove);
        }
        if (open != nil && at >= 0 && at < count - 1) {
            MacEnableMenuItem(article, kArticleItemMarkBelow);
        } else {
            DisableMenuItem(article, kArticleItemMarkBelow);
        }

        if (GazetteFeedsUnreadCount() > 0) {
            MacEnableMenuItem(article, kArticleItemNextUnread);
        } else {
            DisableMenuItem(article, kArticleItemNextUnread);
        }

        if (GazetteUISelectedArticleLink()[0] != '\0') {
            MacEnableMenuItem(article, kArticleItemBrowser);
        } else {
            DisableMenuItem(article, kArticleItemBrowser);
        }
    }

    /* ---- Edit -------------------------------------------------- */
    {
        MenuRef edit = GetMenuHandle(kMenuEdit);

        if (edit != nil) {
            if (GazetteUIReaderHasSelection()) {
                MacEnableMenuItem(edit, kEditItemCopy);
            } else {
                DisableMenuItem(edit, kEditItemCopy);
            }
            if (GazetteFeedsTotalCount() > 0) {
                MacEnableMenuItem(edit, kEditItemFind);
            } else {
                DisableMenuItem(edit, kEditItemFind);
            }
        }
    }

}

/*
 * A contextual click on a sidebar row: the row is chosen first, as the
 * Finder chooses what is control-clicked, so that every command on the menu
 * acts on what the menu was opened over — and so that the commands are the
 * menu bar's own handlers, which act on the selection. Then the menu for
 * that kind of row, adjusted the way the bar's is, and the choice through
 * the same dispatcher.
 */
static void ShowSidebarContextMenu(int kind, int index, Point global)
{
    MenuRef       menu;
    long          chosen;

    if (kind == kGazetteRowSmart && index == kGazetteSmartStarred) {
        return;                     /* Starred has nothing to offer */
    }
    GazetteUIChooseRow(kind, index);

    switch (kind) {
        case kGazetteRowSmart:
            menu = GetMenuHandle(kMenuCtxSmart);
            if (menu != nil) {
                AdjustRefreshItem(menu, kCtxSmartRefresh);
                AdjustMarkAllItem(menu, kCtxSmartMarkAll);
                SetMenuItemText(menu, kCtxSmartSort,
                                GazetteCoreOldestFirst() ? "\pShow Newest First"
                                                         : "\pShow Oldest First");
            }
            break;

        case kGazetteRowGroup:
            menu = GetMenuHandle(kMenuCtxGroup);
            if (menu == nil) {
                return;
            }
            AdjustRefreshItem(menu, kCtxGroupRefresh);
            AdjustMarkAllItem(menu, kCtxGroupMarkAll);
            SetMenuItemText(menu, kCtxGroupSort,
                            GazetteCoreOldestFirst() ? "\pShow Newest First"
                                                     : "\pShow Oldest First");
            SetMenuItemText(menu, kCtxGroupEnabled,
                            GazetteCoreGroupEnabled(index) ? "\pTurn Off"
                                                           : "\pTurn On");
            break;

        case kGazetteRowFeed: {
            Boolean home = (Boolean)(GazetteCoreFeedHome(index)[0] != '\0');

            menu = GetMenuHandle(kMenuCtxFeed);
            if (menu == nil) {
                return;
            }
            AdjustRefreshItem(menu, kCtxFeedRefresh);
            AdjustMarkAllItem(menu, kCtxFeedMarkAll);
            SetMenuItemText(menu, kCtxFeedSort,
                            GazetteCoreOldestFirst() ? "\pShow Newest First"
                                                     : "\pShow Oldest First");
            /* The site is learned from the feed on its first refresh; until
               then there is nothing to open or to copy. */
            if (home) {
                MacEnableMenuItem(menu, kCtxFeedHome);
                MacEnableMenuItem(menu, kCtxFeedCopyHome);
            } else {
                DisableMenuItem(menu, kCtxFeedHome);
                DisableMenuItem(menu, kCtxFeedCopyHome);
            }
            SetMenuItemText(menu, kCtxFeedEnabled,
                            GazetteCoreFeedEnabled(index) ? "\pTurn Off"
                                                          : "\pTurn On");
            break;
        }

        default:
            return;
    }
    if (menu == nil) {
        return;
    }

    /*
     * PopUpMenuSelect rather than ContextualMenuSelect. The Contextual Menu
     * Manager puts a Help item at the head of every menu it shows, and on
     * Mac OS 9 it cannot be asked not to — kCMHelpItemRemoveHelp is
     * documented as disabling the item there rather than removing it. The
     * menu is ours and there is no help; the Menu Manager shows it as it is.
     */
    chosen = PopUpMenuSelect(menu, global.v, global.h, 0);
    if ((chosen >> 16) != 0) {
        HandleMenuChoice(chosen);
    }
}

/* Refresh, on a contextual menu: available whenever the toolbar's is —
   there is a feed to ask for, and nothing already being fetched. */
static void AdjustRefreshItem(MenuRef menu, MenuItemIndex item)
{
    if (GazetteCoreFeedCount() > 0 &&
        GazetteFeedsRefreshGetState() != kGazetteRefreshRunning) {
        MacEnableMenuItem(menu, item);
    } else {
        DisableMenuItem(menu, item);
    }
}

/*
 * A contextual click on a headline: the article is chosen first, as a click
 * chooses it, and the menu is the Article menu's commands for it — worded
 * the way that menu words them, for the one under the mouse — and its
 * address to copy.
 */
static void ShowArticleContextMenu(int index, Point global)
{
    MenuRef               menu = GetMenuHandle(kMenuCtxArticle);
    const GazetteArticle *open;
    int                   count;
    long                  chosen;

    if (menu == nil) {
        return;
    }
    GazetteUIChooseArticle(index);

    open  = GazetteFeedsArticleAt(index);
    count = GazetteFeedsArticleCount();
    if (open == nil) {
        return;
    }

    SetMenuItemText(menu, kCtxArticleMarkRead,
                    open->read ? "\pMark as Unread" : "\pMark as Read");
    SetMenuItemText(menu, kCtxArticleStar,
                    open->starred ? "\pUnstar Article" : "\pStar Article");
    if (index > 0) {
        MacEnableMenuItem(menu, kCtxArticleMarkAbove);
    } else {
        DisableMenuItem(menu, kCtxArticleMarkAbove);
    }
    if (index < count - 1) {
        MacEnableMenuItem(menu, kCtxArticleMarkBelow);
    } else {
        DisableMenuItem(menu, kCtxArticleMarkBelow);
    }
    if (open->link[0] != '\0') {
        MacEnableMenuItem(menu, kCtxArticleCopyURL);
        MacEnableMenuItem(menu, kCtxArticleBrowser);
    } else {
        DisableMenuItem(menu, kCtxArticleCopyURL);
        DisableMenuItem(menu, kCtxArticleBrowser);
    }

    chosen = PopUpMenuSelect(menu, global.v, global.h, 0);
    if ((chosen >> 16) != 0) {
        HandleMenuChoice(chosen);
    }
}

/*
 * A Mark All as Read item, which turns round: with something left unread it
 * offers to read the rest, and with nothing left it offers to put it all
 * back, which is the only thing left for it to mean and is more use than a
 * grey line saying the list is finished. The Feeds menu's and the
 * contextual menus' are all this one.
 */
static void AdjustMarkAllItem(MenuRef menu, MenuItemIndex item)
{
    int count = GazetteFeedsArticleCount();

    if (count == 0) {
        DisableMenuItem(menu, item);
        SetMenuItemText(menu, item, "\pMark All as Read");
    } else if (GazetteFeedsUnreadCount() > 0) {
        MacEnableMenuItem(menu, item);
        SetMenuItemText(menu, item, "\pMark All as Read");
    } else {
        MacEnableMenuItem(menu, item);
        SetMenuItemText(menu, item, "\pMark All as Unread");
    }
}

/* The group names, for the feed dialog's popup. */
static int GroupNames(const char **names, int cap)
{
    int n = GazetteCoreGroupCount();
    int i;

    if (n > cap) {
        n = cap;
    }
    for (i = 0; i < n; i++) {
        names[i] = GazetteCoreGroupName(i);
    }
    return n;
}

static void HandleNewFeed(void)
{
    char              url[kGazetteURLLen];
    char              title[kGazetteTitleLen];
    const char       *names[kGazetteMaxGroups];
    GazetteFeedDialog d;
    int               kind  = 0;
    int               index = 0;
    int               group = -1;
    int               added;

    url[0]   = '\0';
    title[0] = '\0';

    /* The dialog opens on the group the user is looking at — the selected
       one, or the selected feed's — and they choose from there. */
    if (GazetteUISelection(&kind, &index)) {
        group = (kind == kGazetteRowGroup) ? index
                                           : GazetteCoreFeedGroup(index);
    }

    d.windowTitle = "New Feed";
    d.url         = url;
    d.urlCap      = sizeof url;
    d.title       = title;
    d.titleCap    = sizeof title;
    d.groups      = names;
    d.groupCount  = GroupNames(names, kGazetteMaxGroups);
    d.group       = group;
    if (!GazetteAskFeed(&d)) {
        return;
    }

    added = GazetteCoreAddFeed(url, title, d.group);
    if (added < 0) {
        GazetteUISetStatus("That feed is already in the list, or the list "
                           "is full.");
        return;
    }

    GazetteCoreSavePrefs();
    GazetteUIFeedsChanged();

    /* A newly added feed gets one attempt at discovery, so pasting a site's
       home page finds the feed on it. */
    GazetteAppDiscoverNext(added);
    GazetteAppShowFeed(added);
}

static void HandleNewGroup(void)
{
    char name[kGazetteGroupLen];
    int  group;

    name[0] = '\0';
    if (!GazetteAskName("New Group", "Name for the new group:", name,
                        sizeof name)) {
        return;
    }

    group = GazetteCoreAddGroup(name);
    if (group < 0) {
        GazetteUISetStatus("No room for another group.");
        return;
    }

    GazetteCoreSavePrefs();
    GazetteUIFeedsChanged();
    GazetteUISelectGroup(group);
}

/* The address as well as the name, in the same dialog adding one uses,
   started with what the feed already has. */
static void HandleEditFeed(void)
{
    char              url[kGazetteURLLen];
    char              title[kGazetteTitleLen];
    char              wasURL[kGazetteURLLen];
    const char       *names[kGazetteMaxGroups];
    GazetteFeedDialog d;
    int               kind  = 0;
    int               index = 0;
    int               wasGroup;

    if (!GazetteUISelection(&kind, &index) || kind != kGazetteRowFeed) {
        return;
    }

    snprintf(url, sizeof url, "%s", GazetteCoreFeedURL(index));
    snprintf(title, sizeof title, "%s", GazetteCoreFeedTitle(index));
    snprintf(wasURL, sizeof wasURL, "%s", url);
    wasGroup = GazetteCoreFeedGroup(index);

    d.windowTitle = "Edit Feed";
    d.url         = url;
    d.urlCap      = sizeof url;
    d.title       = title;
    d.titleCap    = sizeof title;
    d.groups      = names;
    d.groupCount  = GroupNames(names, kGazetteMaxGroups);
    d.group       = wasGroup;
    if (!GazetteAskFeed(&d)) {
        return;
    }

    /* A different group: to the end of it, or of the list, where a feed
       put somewhere by a dialog rather than a drag goes. */
    if (d.group != wasGroup) {
        GazettePlace place;
        int          moved;

        place.where = (d.group < 0) ? kGazettePlaceListEnd
                                    : kGazettePlaceGroupEnd;
        place.ref   = (d.group < 0) ? 0 : d.group;
        moved = GazetteCoreMoveFeed(index, place);
        if (moved >= 0) {
            index = moved;
        }
    }

    if (strcmp(url, wasURL) != 0) {
        if (!GazetteCoreSetFeedURL(index, url)) {
            GazetteUISetStatus("Another feed already has that address.");
            return;
        }
        /* The old address's cache and counts are keyed by an address nothing
           points at any more. */
        GazetteFeedsForgetCache(wasURL);
        GazetteIndexForgetFeed(wasURL);
    }
    GazetteCoreRenameFeed(index, title);

    GazetteCoreSavePrefs();
    GazetteUIFeedsChanged();

    /* A new address is a different feed with a different cache file, so this
       reads that one — or fetches it when there is nothing cached yet. It
       earns a discovery attempt for the same reason a new feed does: what was
       typed may be a home page. */
    GazetteAppDiscoverNext(index);
    GazetteAppShowFeed(index);
}

/* A group has a name and nothing else to edit, so editing one is naming it. */
static void HandleEditGroup(void)
{
    char name[kGazetteGroupLen];
    int  kind  = 0;
    int  index = 0;

    if (!GazetteUISelection(&kind, &index) || kind != kGazetteRowGroup) {
        return;
    }

    snprintf(name, sizeof name, "%s", GazetteCoreGroupName(index));
    if (!GazetteAskName("Edit Group", "Name for this group:", name,
                        sizeof name)) {
        return;
    }
    GazetteCoreRenameGroup(index, name);

    GazetteCoreSavePrefs();
    GazetteUIFeedsChanged();
}

/* Feeds > Edit Feed… / Edit Group…: whichever the sidebar has selected. */
static void HandleEdit(void)
{
    int kind  = 0;
    int index = 0;

    if (!GazetteUISelection(&kind, &index)) {
        return;
    }
    if (kind == kGazetteRowGroup) {
        HandleEditGroup();
    } else if (kind == kGazetteRowFeed) {
        HandleEditFeed();
    }
}

static void HandleRemove(void)
{
    char message[320];
    int  kind  = 0;
    int  index = 0;

    if (!GazetteUISelection(&kind, &index)) {
        return;
    }

    /* "\322" and "\323" are the MacRoman curly quotes, which is what a
       Platinum alert uses around a name. */
    if (kind == kGazetteRowGroup) {
        snprintf(message, sizeof message,
                 "Delete the group \322%s\323? The feeds in it are kept - "
                 "they move to the top of the list.",
                 GazetteCoreGroupName(index));
        if (!GazetteConfirmRemove(message)) {
            return;
        }
        GazetteCoreRemoveGroup(index);
    } else {
        char url[kGazetteURLLen];

        snprintf(message, sizeof message,
                 "Delete the feed \322%s\323? You can subscribe to it again "
                 "at any time.", GazetteCoreFeedTitle(index));
        if (!GazetteConfirmRemove(message)) {
            return;
        }

        /* Copy the address out first: removing shifts the array that pointer
           points into. */
        snprintf(url, sizeof url, "%s", GazetteCoreFeedURL(index));

        /* The cache file is keyed by the address, so nothing would ever go
           looking for it again -- it would just sit in the Gazette Cache
           folder for good. */
        GazetteFeedsForgetCache(url);
        GazetteIndexForgetFeed(url);
        GazetteCoreRemoveFeed(url);

        /* The feed that shuffled up into the gap is the one to show — the
           same place in the list the user was already looking at. */
        GazetteCoreSavePrefs();
        GazetteUIFeedsChanged();
        if (index >= GazetteCoreFeedCount()) {
            index = GazetteCoreFeedCount() - 1;
        }
        GazetteAppShowFeed(index);
        return;
    }

    GazetteCoreSavePrefs();
    GazetteUIFeedsChanged();
    GazetteAppShowFeed(GazetteUISelectedFeed());
}

static void HandleToggleEnabled(void)
{
    int kind  = 0;
    int index = 0;

    if (!GazetteUISelection(&kind, &index)) {
        return;
    }

    if (kind == kGazetteRowFeed) {
        GazetteCoreSetFeedEnabled(index, !GazetteCoreFeedEnabled(index));
    } else if (kind == kGazetteRowGroup) {
        GazetteCoreSetGroupEnabled(index, !GazetteCoreGroupEnabled(index));
    } else {
        return;
    }
    GazetteCoreSavePrefs();
    GazetteUIFeedsChanged();
}

static void HandleOpenHomePage(void)
{
    int kind  = 0;
    int index = 0;

    if (!GazetteUISelection(&kind, &index) || kind != kGazetteRowFeed) {
        return;
    }
    if (GazetteCoreFeedHome(index)[0] == '\0') {
        GazetteUISetStatus("This feed has not said where its site is yet; "
                           "refresh it first.");
        return;
    }
    (void)OpenURL(GazetteCoreFeedHome(index));
}

/* Text onto the clipboard, as the one flavour every other application on
   this machine reads. */
static void CopyTextToClipboard(const char *text)
{
    ScrapRef scrap;

    if (text == NULL || ClearCurrentScrap() != noErr ||
        GetCurrentScrap(&scrap) != noErr) {
        return;
    }
    (void)PutScrapFlavor(scrap, kScrapFlavorTypeText, kScrapFlavorMaskNone,
                         (Size)strlen(text), text);
}

static void HandleCopyFeedURL(void)
{
    int kind  = 0;
    int index = 0;

    if (!GazetteUISelection(&kind, &index) || kind != kGazetteRowFeed) {
        return;
    }
    CopyTextToClipboard(GazetteCoreFeedURL(index));
    GazetteUISetStatus("The feed's address is on the clipboard.");
}

static void HandleCopyArticleURL(void)
{
    const char *url = GazetteUISelectedArticleLink();

    if (url[0] == '\0') {
        return;
    }
    CopyTextToClipboard(url);
    GazetteUISetStatus("The article's address is on the clipboard.");
}

static void HandleCopyHomeURL(void)
{
    int kind  = 0;
    int index = 0;

    if (!GazetteUISelection(&kind, &index) || kind != kGazetteRowFeed ||
        GazetteCoreFeedHome(index)[0] == '\0') {
        return;
    }
    CopyTextToClipboard(GazetteCoreFeedHome(index));
    GazetteUISetStatus("The site's address is on the clipboard.");
}

/*
 * Hand the article's address to whatever the machine calls its browser.
 *
 * Internet Config, rather than an Apple event of our own: ICLaunchURL is what
 * every Mac OS 9 application uses for this, it obeys the helper the user
 * chose in the Internet control panel, and it starts the browser if it is not
 * already running. The hint is empty because the URL carries its own scheme.
 */
/* Hand an address to whatever Internet Config says opens it — the user's
   browser, for anything either of the callers has. */
static Boolean OpenURL(const char *url)
{
    ICInstance ic = nil;
    long       start;
    long       end;
    Boolean    opened;

    if (url == NULL || url[0] == '\0') {
        return false;
    }
    if (ICStart(&ic, 'Gzt9') != noErr || ic == nil) {
        GazetteUISetStatus("Internet Config is not available on this "
                           "Macintosh.");
        return false;
    }

    start  = 0;
    end    = (long)strlen(url);
    opened = (Boolean)(ICLaunchURL(ic, "\p", (Ptr)url, end, &start, &end)
                       == noErr);
    if (opened) {
        GazetteUISetStatus("Opened in your browser.");
    } else {
        GazetteUISetStatus("No application is set up to open that address.");
    }
    (void)ICStop(ic);
    return opened;
}

static void HandleOpenInBrowser(void)
{
    (void)OpenURL(GazetteUISelectedArticleLink());
}

/*
 * The Preferences window. Two numbers: how often the clock refreshes, and
 * how much of a feed is kept. Saved on OK, and the view shown again with
 * the new limit; the clock starts over, so a shorter interval is not
 * already overdue.
 */
static void HandlePreferences(void)
{
    const GazettePrefs *prefs = GazetteCoreGetPrefs();
    long minutes, articles;

    if (prefs == nil) {
        return;
    }
    minutes  = prefs->refreshMinutes;
    articles = prefs->maxArticles;

    if (!GazetteAskPreferences(&minutes, &articles)) {
        return;
    }
    if (minutes == prefs->refreshMinutes && articles == prefs->maxArticles) {
        return;
    }
    GazetteCoreSetRefreshMinutes(minutes);
    GazetteCoreSetMaxArticles(articles);
    GazetteCoreSavePrefs();
    GazetteAppRestartClock();
    GazetteAppReloadView();
}

static void HandleHideSidebar(void)
{
    Boolean wanted = GazetteCoreHideSidebar() ? false : true;

    GazetteCoreSetHideSidebar(wanted);
    GazetteCoreSavePrefs();

    GazetteUIViewChanged();
}

static void HandleHideToolbar(void)
{
    Boolean wanted = GazetteCoreHideToolbar() ? false : true;

    GazetteCoreSetHideToolbar(wanted);
    GazetteCoreSavePrefs();

    GazetteUIViewChanged();
}

/*
 * The search box, when Return has been pressed in it. The same act as Find,
 * and it ends in the same place — an empty box is what clears a search, here
 * as there.
 */
static void HandleSearch(void)
{
    char text[64];

    GazetteUISearchText(text, sizeof text);
    GazetteAppSearch(text);
}

/*
 * A toolbar button. Every one of them is a menu item's handler and nothing
 * else — which is the whole point of the toolbar naming commands rather than
 * carrying its own code: there is no second implementation to keep in step.
 */
static void ToolbarCommand(int command)
{
    switch (command) {
        case kGazetteCmdHideSidebar:      HandleHideSidebar();      break;
        case kGazetteCmdRefresh:          GazetteAppRefreshAll();       break;
        case kGazetteCmdMarkAllRead:      GazetteAppMarkAllRead();      break;
        case kGazetteCmdHideReadArticles: GazetteAppHideReadArticles(); break;
        case kGazetteCmdMarkRead:         GazetteAppMarkRead();         break;
        case kGazetteCmdMarkStarred:      GazetteAppToggleStar();       break;
        case kGazetteCmdNextUnread:       GazetteAppNextUnread();       break;
        case kGazetteCmdOpenInBrowser:    HandleOpenInBrowser();    break;
        case kGazetteCmdSearch:           HandleSearch();           break;
        case kGazetteCmdNewFeed:          HandleNewFeed();          break;
        case kGazetteCmdNewGroup:         HandleNewGroup();         break;
        default: break;
    }
}

/*
 * Search what is on screen. That is one feed, or — since a group is readable
 * — every feed in a group, so searching a whole section of the sidebar is a
 * matter of selecting it first. It is a filter over what is held rather than
 * a search of the disk: reading a hundred cache files to answer a keystroke
 * is not something these machines should be asked to do.
 */
static void HandleFind(void)
{
    char text[64];

    snprintf(text, sizeof text, "%s", GazetteFeedsFilter());
    if (!GazetteAskName("Find", "Find articles containing:", text,
                        sizeof text)) {
        return;
    }
    GazetteUISetSearchText(text);
    GazetteAppSearch(text);
}

/* ------------------------------------------------------------------ */
/* Cleanup                                                             */
/* ------------------------------------------------------------------ */

static void DoExitGazette(void)
{
    /* Everything in flight stopped, and what was read written. */
    GazetteAppShutdown();

    GazetteUIClose();

    /* Writes the preferences back out if anything changed them — including a
       first run, which saves the defaults so there is a file to hand-edit. */
    GazetteCoreShutdown();
    GazetteNetShutdown();
}
