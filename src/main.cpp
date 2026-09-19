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
#include "ui/platinum_window.h"

/* ------------------------------------------------------------------ */
/* Forward declarations                                                */
/* ------------------------------------------------------------------ */

static Boolean InitGazette(void);
static Boolean BuildMenuBar(void);
static void    InstallAppleEventHandlers(void);
static void    RunGazette(void);
static void    DoExitGazette(void);

static void    HandleEvent(const EventRecord *event);
static void    HandleMouseDown(const EventRecord *event);
static void    HandleMenuChoice(long menuResult);

static void    HandleAbout(void);
static void    HandleQuit(void);
static void    HandleRefreshSelection(void);
static void    HandleRefreshAll(void);
static void    HandlePreferences(void);
static void    ReloadCurrentView(void);
static void    ShowFeed(int feedIndex);
static void    ShowArticle(int articleIndex);
static void    ShowGroup(int groupIndex);
static Boolean AdvanceGroupRefresh(void);
static void    PumpRefresh(void);
static void    PumpFullText(void);
static void    CheckAutoRefresh(void);

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
static void    ShowSmart(int which);
static void    ResumeFullText(void);
static void    HandleMarkRead(void);
static void    HandleMarkAllRead(void);
static void    HandleMarkRange(Boolean below);
static void    HandleToggleStar(void);
static void    HandleNextUnread(void);
static void    HandleOpenInBrowser(void);
static void    HandleSortOrder(Boolean oldestFirst);
static void    HandleHideReadArticles(void);
static void    HandleHideReadFeeds(void);
static void    HandleHideSidebar(void);
static void    HandleHideToolbar(void);
static void    HandleShowPhotos(void);
static void    RememberWindowLayout(void);
static void    StartPhotos(void);
static void    PumpPhotos(void);
static void    ResumePhotos(void);
static void    HandleSearch(void);
static void    ToolbarCommand(int command);
static void    HandleFind(void);
static void    HandleImportOPML(void);
static void    HandleExportOPML(void);

/* ------------------------------------------------------------------ */
/* Application globals                                                 */
/* ------------------------------------------------------------------ */

static Boolean gDone  = false;
static Boolean gNetUp = false;

/* When the running refresh started, so it can be timed, and when the last one
   finished, so auto-refresh knows how long it has been. Both in ticks. */
static unsigned long gLastRefreshTicks;

/*
 * Refreshing a group is the one place Gazette fetches more than one feed, and
 * it does it one after another rather than at once: there is a single
 * connection, and a queue of feeds fetched in turn is the whole of what a
 * "refresh all" needs to be here.
 */
static int gQueue[kGazetteMaxFeeds];
static int gQueueCount;
static int gQueueAt;
static int gQueueGroup = -1;       /* -1 when no group refresh is running */

/* Which standing view a finished queue should gather into, or -1. Refreshing
   with Today, All Unread or Starred selected fetches every enabled feed —
   there is no one feed behind the view — and then asks the question again. */
static int gQueueSmart = -1;

/* And which feed, when Refresh All was asked for with a feed on screen:
   the queue fetches the lot and then shows that feed again. */
static int gQueueFeed = -1;

static Boolean QueueRunning(void)
{
    return (Boolean)(gQueueGroup >= 0 || gQueueSmart >= 0 || gQueueFeed >= 0);
}

/*
 * The feed a discovery attempt is still owed, or -1.
 *
 * Set when a feed is added, cleared the moment the attempt is made. Pasting a
 * site's home page into New Feed is the case this exists for: the fetch comes
 * back as a page rather than a feed, and the page says where its feed is.
 * One attempt, and only for a feed just added, so a feed that has always
 * worked can never be quietly replaced by something it links to.
 */
static int gDiscoverFeed = -1;

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
    kFeedsItemMarkAll   = 5,
    kFeedsItemSort      = 6,      /* Show Oldest First / Show Newest First */
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
    kCtxSmartMarkAll = 2
};
enum {
    kCtxGroupRefresh = 1,
    kCtxGroupMarkAll = 2,
    /* 3 is a divider */
    kCtxGroupEdit    = 4,
    kCtxGroupEnabled = 5,
    /* 6 is a divider */
    kCtxGroupDelete  = 7
};
enum {
    kCtxFeedRefresh  = 1,
    kCtxFeedMarkAll  = 2,
    kCtxFeedSort     = 3,
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
    gNetUp = GazetteNetInit() ? true : false;

    if (!BuildMenuBar()) {
        return false;
    }

    InstallAppleEventHandlers();

    if (!GazetteUIOpen(ShowFeed, ShowArticle, ShowGroup, ShowSmart,
                       ToolbarCommand)) {
        return false;
    }

    /* A modal dialog and a Navigation Services dialog each run a loop of
       their own, and this is what keeps a fetch moving inside them. */
    GazetteDialogsSetIdle(PumpRefresh);
    GazetteStoreSetIdle(PumpRefresh);

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
    /* The sort order stands after Mark All as Read: what is done to a
       list, next to what is done to a list. The item names the order it
       would switch to; AdjustMenus keeps it current. */
    AppendMenu(feedsMenu,
               "\pToday/1;All Unread/2;Starred/3;(-;"
               "Mark All as Read/K;Show Oldest First;(-;"
               "Edit Feed\311;Turn Off;(-;"
               "Delete Feed");
    InsertMenu(feedsMenu, 0);

    /* The contextual menus. In the hierarchical list, which is where
       PopUpMenuSelect wants a menu it is handed. */
    ctx = NewMenu(kMenuCtxSmart, "\p");
    if (ctx == nil) {
        return false;
    }
    AppendMenu(ctx, "\pRefresh;Mark All as Read");
    InsertMenu(ctx, hierMenu);

    ctx = NewMenu(kMenuCtxGroup, "\p");
    if (ctx == nil) {
        return false;
    }
    AppendMenu(ctx, "\pRefresh;Mark All as Read;(-;"
                    "Edit Group\311;Turn Off;(-;Delete Group");
    InsertMenu(ctx, hierMenu);

    ctx = NewMenu(kMenuCtxFeed, "\p");
    if (ctx == nil) {
        return false;
    }
    AppendMenu(ctx, "\pRefresh;Mark All as Read;Show Oldest First;(-;"
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
            /* Idle. This is the one place network I/O advances, and nothing
               it calls blocks: a pump does whatever work is available this
               pass and returns. Blocking here would stop the whole machine
               cooperating, not just Gazette. */
            PumpRefresh();
            PumpFullText();
            ResumeFullText();
            PumpPhotos();
            ResumePhotos();
            CheckAutoRefresh();
        }
        /* Either way, the window has a look at where the mouse is: the
           toolbar's buttons answer it without an event of their own. */
        GazetteUIIdle();
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
                case kFileItemRefresh:  HandleRefreshAll(); break;
                case kFileItemImport:   HandleImportOPML(); break;
                case kFileItemExport:   HandleExportOPML(); break;
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
                case kViewItemHideRead:    HandleHideReadArticles(); break;
                case kViewItemHideFeeds:   HandleHideReadFeeds();    break;
                case kViewItemShowPhotos:  HandleShowPhotos();       break;
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
                case kFeedsItemMarkAll:  HandleMarkAllRead();   break;
                case kFeedsItemSort:
                    HandleSortOrder((Boolean)!GazetteCoreOldestFirst());
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
                case kCtxSmartRefresh: HandleRefreshSelection(); break;
                case kCtxSmartMarkAll: HandleMarkAllRead(); break;
                default: break;
            }
            break;

        case kMenuCtxGroup:
            switch (menuItem) {
                case kCtxGroupRefresh: HandleRefreshSelection(); break;
                case kCtxGroupMarkAll: HandleMarkAllRead();     break;
                case kCtxGroupEnabled: HandleToggleEnabled();   break;
                case kCtxGroupEdit:    HandleEditGroup();       break;
                case kCtxGroupDelete:  HandleRemove();          break;
                default: break;
            }
            break;

        case kMenuCtxFeed:
            switch (menuItem) {
                case kCtxFeedRefresh:  HandleRefreshSelection(); break;
                case kCtxFeedMarkAll:  HandleMarkAllRead();     break;
                case kCtxFeedSort:
                    HandleSortOrder((Boolean)!GazetteCoreOldestFirst());
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
                case kCtxArticleMarkRead:  HandleMarkRead();       break;
                case kCtxArticleStar:      HandleToggleStar();     break;
                case kCtxArticleMarkAbove: HandleMarkRange(false); break;
                case kCtxArticleMarkBelow: HandleMarkRange(true);  break;
                case kCtxArticleCopyURL:   HandleCopyArticleURL(); break;
                case kCtxArticleBrowser:   HandleOpenInBrowser();  break;
                default: break;
            }
            break;

        case kMenuArticle:
            switch (menuItem) {
                case kArticleItemNextUnread: HandleNextUnread();       break;
                case kArticleItemMarkRead:   HandleMarkRead();         break;
                case kArticleItemMarkAbove:  HandleMarkRange(false);   break;
                case kArticleItemMarkBelow:  HandleMarkRange(true);    break;
                case kArticleItemStar:       HandleToggleStar();       break;
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

        PumpRefresh();
        PumpFullText();
        ResumeFullText();
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
            }
            break;

        case kGazetteRowGroup:
            menu = GetMenuHandle(kMenuCtxGroup);
            if (menu == nil) {
                return;
            }
            AdjustRefreshItem(menu, kCtxGroupRefresh);
            AdjustMarkAllItem(menu, kCtxGroupMarkAll);
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
    gDiscoverFeed = added;
    ShowFeed(added);
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
    gDiscoverFeed = index;
    ShowFeed(index);
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
        ShowFeed(index);
        return;
    }

    GazetteCoreSavePrefs();
    GazetteUIFeedsChanged();
    ShowFeed(GazetteUISelectedFeed());
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
 * Full article text on or off. It is a preference rather than a one-off
 * command, so it saves at once and takes effect on the article already open:
 * turning it on and having to click away and back would read as it not
 * working.
 */
static void HandleMarkRead(void)
{
    int                   index   = GazetteUISelectedArticle();
    const GazetteArticle *article = GazetteFeedsArticleAt(index);

    if (article == nil) {
        return;
    }
    GazetteFeedsMarkRead(index, article->read ? 0 : 1);
    GazetteUIUpdate();
}

static void HandleMarkAllRead(void)
{
    /* Which way round follows the list, exactly as the menu item's own text
       does: nothing left unread means the command is the other one. */
    if (GazetteFeedsUnreadCount() > 0) {
        GazetteFeedsMarkAllRead();
    } else {
        GazetteFeedsMarkAllUnread();
    }

    /* With read articles hidden, marking the lot read empties the list — so
       the list has to be re-derived rather than redrawn. */
    if (GazetteCoreHideReadArticles()) {
        GazetteUIViewChanged();
    } else {
        GazetteUIUpdate();
    }
}

static void HandleMarkRange(Boolean below)
{
    int index = GazetteUISelectedArticle();

    if (GazetteFeedsArticleAt(index) == nil) {
        return;
    }
    GazetteFeedsMarkRange(index, below ? 1 : 0);

    if (GazetteCoreHideReadArticles()) {
        GazetteUIViewChanged();
    } else {
        GazetteUIUpdate();
    }
}

static void HandleToggleStar(void)
{
    int                   index   = GazetteUISelectedArticle();
    const GazetteArticle *article = GazetteFeedsArticleAt(index);

    if (article == nil) {
        return;
    }
    GazetteFeedsMarkStarred(index, article->starred ? 0 : 1);
    GazetteIndexSave();
    GazetteUIUpdate();
}

static void HandleNextUnread(void)
{
    if (!GazetteUINextUnread()) {
        GazetteUISetStatus("Nothing unread below this one.");
    }
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

/* ------------------------------------------------------------------ */
/* The View menu                                                       */
/*                                                                     */
/* Each of these is the same three steps: move the preference, tell     */
/* the store or the window what changed, and save. They are written     */
/* out rather than folded together because what each one has to tell    */
/* is different, and the differences are the whole of the code.         */
/* ------------------------------------------------------------------ */

static void HandleSortOrder(Boolean oldestFirst)
{
    if (GazetteCoreOldestFirst() == oldestFirst) {
        return;
    }
    GazetteCoreSetOldestFirst(oldestFirst);
    GazetteFeedsSetOldestFirst(oldestFirst ? 1 : 0);
    GazetteCoreSavePrefs();

    GazetteUIViewChanged();
    GazetteUISetStatus(oldestFirst ? "Oldest articles on top."
                                   : "Newest articles on top.");
}

static void HandleHideReadArticles(void)
{
    Boolean wanted = GazetteCoreHideReadArticles() ? false : true;

    GazetteCoreSetHideReadArticles(wanted);
    GazetteFeedsSetHideRead(wanted ? 1 : 0);
    GazetteCoreSavePrefs();

    GazetteUIViewChanged();
    GazetteUISetStatus(wanted ? "Showing unread articles only."
                              : "Showing every article.");
}

static void HandleHideReadFeeds(void)
{
    Boolean wanted = GazetteCoreHideReadFeeds() ? false : true;

    GazetteCoreSetHideReadFeeds(wanted);
    GazetteCoreSavePrefs();

    GazetteUIViewChanged();
    GazetteUISetStatus(wanted ? "Showing feeds with something unread in them."
                              : "Showing every feed.");
}

/*
 * Photos on or off. Off drops whatever is in flight and closes the gaps in
 * the article on screen; on fetches the pictures of the article that is
 * open, if its page has been read — its list is still held with the text.
 */
static void HandleShowPhotos(void)
{
    Boolean wanted = GazetteCoreShowPhotos() ? false : true;

    GazetteCoreSetShowPhotos(wanted);
    GazetteCoreSavePrefs();

    if (!wanted) {
        GazettePhotosCancel();
        GazetteUIArticleTextChanged();
        GazetteUISetStatus("Photos off.");
    } else {
        StartPhotos();
        GazetteUIArticleTextChanged();
        GazetteUISetStatus("Photos on.");
    }
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
    gLastRefreshTicks = TickCount();
    ReloadCurrentView();
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
    char message[224];

    GazetteUISearchText(text, sizeof text);
    GazetteFeedsSetFilter(text);
    GazetteUIArticlesChanged();

    if (text[0] == '\0') {
        snprintf(message, sizeof message, "%d articles.",
                 GazetteFeedsArticleCount());
    } else if (GazetteFeedsArticleCount() == 0) {
        snprintf(message, sizeof message,
                 "Nothing here contains \322%s\323.", text);
    } else {
        snprintf(message, sizeof message, "%d of %d articles contain "
                 "\322%s\323.", GazetteFeedsArticleCount(),
                 GazetteFeedsTotalCount(), text);
    }
    GazetteUISetStatus(message);
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
        case kGazetteCmdRefresh:          HandleRefreshAll();       break;
        case kGazetteCmdMarkAllRead:      HandleMarkAllRead();      break;
        case kGazetteCmdHideReadArticles: HandleHideReadArticles(); break;
        case kGazetteCmdMarkRead:         HandleMarkRead();         break;
        case kGazetteCmdMarkStarred:      HandleToggleStar();       break;
        case kGazetteCmdNextUnread:       HandleNextUnread();       break;
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
    char message[224];

    snprintf(text, sizeof text, "%s", GazetteFeedsFilter());
    if (!GazetteAskName("Find", "Find articles containing:", text,
                        sizeof text)) {
        return;
    }

    GazetteFeedsSetFilter(text);
    GazetteUISetSearchText(text);
    GazetteUIArticlesChanged();

    /* An empty box is how a search is cleared — which is why there is no
       "Show All Articles" beside this one any more. */
    if (text[0] == '\0') {
        snprintf(message, sizeof message, "%d articles.",
                 GazetteFeedsArticleCount());
        GazetteUISetStatus(message);
        return;
    }

    if (GazetteFeedsArticleCount() == 0) {
        snprintf(message, sizeof message,
                 "Nothing here contains \322%s\323.", text);
    } else {
        snprintf(message, sizeof message, "%d of %d articles contain "
                 "\322%s\323.", GazetteFeedsArticleCount(),
                 GazetteFeedsTotalCount(), text);
    }
    GazetteUISetStatus(message);
}

/* ------------------------------------------------------------------ */
/* OPML                                                                */
/*                                                                     */
/* The whole feed list, in the format every other reader speaks. The    */
/* text is portable and host-tested (prefs/gazette_opml.h); choosing    */
/* the file is Navigation Services, which is the only way to ask for    */
/* one under Carbon.                                                    */
/* ------------------------------------------------------------------ */

static void HandleImportOPML(void)
{
    char     *text;
    char      message[224];
    long      len     = 0;
    int       added   = 0;
    int       outcome;

    /* 96 KB is too much to put on this stack, and it is wanted for the
       length of one import and no longer. */
    text = (char *)NewPtrClear((Size)kGazetteOPMLMax);
    if (text == nil) {
        GazetteUISetStatus("Not enough memory to read a feed list.");
        return;
    }

    outcome = GazetteStoreAskAndReadFile("Choose an OPML feed list to import:",
                                        text, kGazetteOPMLMax, &len);
    if (outcome == kGazetteFileDone && len > 0) {
        added = GazetteCoreImportOPML(text, (size_t)len);
    }
    DisposePtr((Ptr)text);

    if (outcome == kGazetteFileFailed) {
        GazetteUISetStatus(GazetteStoreErrorText());
        return;
    }
    if (outcome == kGazetteFileCancelled) {
        return;                     /* changing your mind needs no report */
    }
    if (added <= 0) {
        /* A file whose feeds are all subscribed already is not a failure;
           saying nothing happened is the whole of the news. */
        GazetteUISetStatus("No new feeds were added.");
        return;
    }

    GazetteCoreSavePrefs();
    GazetteUIFeedsChanged();
    snprintf(message, sizeof message, "%d feed%s added.", added,
             (added == 1) ? "" : "s");
    GazetteUISetStatus(message);
}

static void HandleExportOPML(void)
{
    char  *text;
    char   message[224];
    size_t len;

    text = (char *)NewPtrClear((Size)kGazetteOPMLMax);
    if (text == nil) {
        GazetteUISetStatus("Not enough memory to write a feed list.");
        return;
    }

    len = GazetteOPMLWrite(GazetteCoreGetPrefs(), text, kGazetteOPMLMax);
    if (len == 0) {
        DisposePtr((Ptr)text);
        GazetteUISetStatus("The feed list could not be written.");
        return;
    }

    switch (GazetteStoreAskAndWriteFile("Save the feed list as:",
                                        "Gazette Feeds.opml", text,
                                        (long)len)) {
        case kGazetteFileDone:
            snprintf(message, sizeof message, "%d feeds exported.",
                     GazetteCoreFeedCount());
            GazetteUISetStatus(message);
            break;
        case kGazetteFileFailed:
            /*
             * The dialog would not open. That is no reason for the user to
             * leave without their feed list, so it goes somewhere findable
             * and the status line says exactly where.
             */
            if (GazetteStoreWriteDataFile("Gazette Feeds.opml", text,
                                          (long)len)) {
                GazetteUISetStatus("Saved as ÒGazette Feeds.opmlÓ in the "
                                   "Gazette Cache folder, inside Preferences.");
            } else {
                GazetteUISetStatus(GazetteStoreErrorText());
            }
            break;
        default:
            break;                  /* cancelled */
    }
    DisposePtr((Ptr)text);
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

    /* Whatever was read in the feed being left has to reach its cache file
       before the store is replaced. */
    GazetteFeedsFlush();

    /* A search was about the articles that were on screen; these are not
       them. Leaving it set would make a feed look empty for no visible
       reason. */
    GazetteFeedsSetFilter(NULL);

    GazetteUISelectFeed(feedIndex);

    if (GazetteFeedsLoadCache(feedIndex, GazetteCoreFeedURL(feedIndex),
                              PrefsMaxArticles())) {
        GazetteUIArticlesChanged();
        snprintf(message, sizeof message, "%d articles from the last fetch.",
                 GazetteFeedsArticleCount());
        GazetteUISetStatus(message);
        return;
    }

    /*
     * Nothing cached for this feed. Whatever is still in the store belongs to
     * another feed or to a group, and leaving it on screen under this feed's
     * name would be a lie -- one that a group view makes obvious, since a
     * merged list of ten feeds would sit under a single feed's heading.
     */
    if (GazetteFeedsCurrentFeed() != feedIndex) {
        GazetteFeedsClear();
        GazetteUIArticlesChanged();
    }

    if (!gNetUp) {
        GazetteUISetStatus("Nothing cached, and no network - "
                           "check the TCP/IP control panel.");
        return;
    }

    HandleRefreshSelection();
}

/*
 * An article has been opened. That is when
 * its own page is fetched: lazily, one at a time, and only for something the
 * user is actually looking at.
 */
static void ShowArticle(int articleIndex)
{
    const GazetteArticle *article;

    /* Whatever was held is for the article that was open a moment ago. */
    GazetteFeedsFullTextCancel();

    article = GazetteFeedsArticleAt(articleIndex);
    if (article == nil || article->link[0] == '\0') {
        return;                     /* nothing to fetch: no address */
    }
    if (!gNetUp) {
        return;                     /* nothing to fetch it with */
    }

    /*
     * Started, or — if the refresh has the one connection — remembered by the
     * store and started by ResumeFullText the moment the line is free. Either
     * way the answer is yes, the page is coming, which is what the reader
     * pane asks before it decides whether to lay out the summary.
     */
    if (GazetteFeedsFullTextStart(articleIndex, article->link)) {
        GazetteUISetStatus("Reading the full article\311");
    }
}

/* Start a page the store held back while the line was busy. */
static void ResumeFullText(void)
{
    if (GazetteFeedsFullTextResume()) {
        GazetteUISetStatus("Reading the full article\311");
    }
}

/* Fetch the pictures of the article whose page has just been read, when
   the user wants pictures. */
static void StartPhotos(void)
{
    const GazettePhotoRef *refs;
    int                    count;
    int                    article = GazetteFeedsFullTextArticle();

    if (!GazetteCoreShowPhotos() || article < 0) {
        return;
    }
    count = GazetteFeedsFullTextPhotos(&refs);
    if (count <= 0) {
        return;
    }
    (void)GazettePhotosStart(article, GazetteFeedsFullTextFinalURL(),
                             refs, count);
}

static void ResumePhotos(void)
{
    (void)GazettePhotosResume();
}

/* A slice of the picture fetch. Each picture that lands, or does not, is
   worth a redraw of the pane and nothing more: no status line, because the
   article is already there to read. */
static void PumpPhotos(void)
{
    int before[kGazetteMaxPhotos];
    int count = GazettePhotosCount();
    int i;

    if (GazettePhotosGetState() != kGazetteRefreshRunning) {
        return;
    }
    for (i = 0; i < count; i++) {
        before[i] = GazettePhotosState(i);
    }
    (void)GazettePhotosPump();
    for (i = 0; i < count; i++) {
        if (GazettePhotosState(i) != before[i]) {
            GazetteUIPhotosChanged();
            break;
        }
    }
}

static void PumpFullText(void)
{
    char message[224];

    if (GazetteFeedsFullTextGetState() != kGazetteRefreshRunning) {
        return;
    }

    switch (GazetteFeedsFullTextPump()) {
        case kGazetteRefreshDone:
            /* The pictures go after the text, on the same line: the article
               is readable at once and they fill in behind it. Asked for
               before the pane composes, so it knows to leave them room. */
            StartPhotos();
            /* The pane is showing the summary; this is what swaps it. */
            GazetteUIArticleTextChanged();
            GazetteUISetStatus("Full article.");
            break;

        case kGazetteRefreshFailed:
            /*
             * The pane has been saying it is reading. Now that the page is
             * not coming after all, it falls back to the feed's summary —
             * which is what this call composes, the store having just
             * stopped answering that anything is on its way.
             *
             * Saying why is worth a status line and not worth a dialog: it
             * happens on any paywall, and the article is still readable.
             */
            GazetteUIArticleTextChanged();
            snprintf(message, sizeof message, "Summary only - %s",
                     GazetteFeedsFullTextErrorText());
            GazetteUISetStatus(message);
            break;

        default:
            break;
    }
}

/*
 * A group has been selected: show every article from every enabled feed in
 * it, merged newest first. Read out of the caches, so it is instant and works
 * with the machine unplugged — a group is readable as soon as any one of its
 * feeds has been fetched.
 */
static void ShowGroup(int groupIndex)
{
    char message[224];
    int  count;

    /* The feed being left may have had something read in it. */
    GazetteFeedsFlush();
    GazetteFeedsSetFilter(NULL);

    count = GazetteFeedsLoadGroup(groupIndex, PrefsMaxArticles());
    GazetteUIArticlesChanged();

    if (count == 0) {
        GazetteUISetStatus("Nothing cached in this group yet - "
                           "press Command-R to fetch it.");
        return;
    }
    snprintf(message, sizeof message, "%d articles from %s.", count,
             GazetteCoreGroupName(groupIndex));
    GazetteUISetStatus(message);
}

/*
 * One of the three standing views. Gathered from every enabled feed's cache
 * rather than fetched: they are a question about what is already here, and a
 * reader who wants more presses Command-R, which refreshes the lot.
 */
static void ShowSmart(int which)
{
    char message[224];
    int  count;

    /* The feed being left may have had something read in it. */
    GazetteFeedsFlush();
    GazetteFeedsSetFilter(NULL);

    count = GazetteFeedsLoadSmart(which, PrefsMaxArticles());
    GazetteUIArticlesChanged();

    if (count == 0) {
        switch (which) {
            case kGazetteSmartToday:
                GazetteUISetStatus("Nothing dated today in any feed yet.");
                break;
            case kGazetteSmartStarred:
                GazetteUISetStatus("No starred articles - Command-L stars "
                                   "the one you are reading.");
                break;
            default:
                GazetteUISetStatus("Everything has been read.");
                break;
        }
        return;
    }
    snprintf(message, sizeof message, "%d articles in %s.", count,
             GazetteCoreSmartName(which));
    GazetteUISetStatus(message);
}

/*
 * Start the next feed of a group refresh, or finish it. Returns true while
 * the queue is still running, which is what tells PumpRefresh to keep the
 * window as it is rather than showing the one feed that just landed.
 */
static Boolean AdvanceGroupRefresh(void)
{
    char message[224];

    if (!QueueRunning()) {
        return false;
    }

    while (gQueueAt < gQueueCount) {
        int feed = gQueue[gQueueAt++];

        if (GazetteFeedsRefreshStart(feed, GazetteCoreFeedURL(feed),
                                     PrefsMaxArticles(), 0)) {
            snprintf(message, sizeof message, "Fetching %s (%d of %d)\311",
                     GazetteCoreFeedTitle(feed), gQueueAt, gQueueCount);
            GazetteUISetStatus(message);
            return true;
        }
        /* A feed that will not start is skipped rather than stopping the
           rest of the group. */
    }

    /* Done: the store holds whichever feed came last, so the view has to be
       gathered again from the caches they all just wrote. */
    {
        int group = gQueueGroup;
        int smart = gQueueSmart;
        int feed  = gQueueFeed;
        int count;

        gQueueGroup = -1;
        gQueueSmart = -1;
        gQueueFeed  = -1;
        gQueueCount = 0;
        gQueueAt    = 0;

        if (smart >= 0) {
            ShowSmart(smart);
            return false;
        }
        if (feed >= 0) {
            ShowFeed(feed);
            return false;
        }

        count = GazetteFeedsLoadGroup(group, PrefsMaxArticles());
        GazetteUIArticlesChanged();
        snprintf(message, sizeof message, "%d articles from %s.", count,
                 GazetteCoreGroupName(group));
        GazetteUISetStatus(message);
    }
    return false;
}

/*
 * Refresh every feed that is switched on, in turn, and then show again
 * whatever was on screen — the feed, the group or the standing view. This
 * is what File > Refresh and the toolbar's button do; the contextual menus
 * refresh the row they were opened on, see HandleRefreshSelection.
 */
static void HandleRefreshAll(void)
{
    int kind      = 0;
    int selection = 0;
    int i;

    if (GazetteFeedsRefreshGetState() == kGazetteRefreshRunning ||
        QueueRunning()) {
        return;                     /* one connection, one fetch */
    }
    if (!gNetUp) {
        GazetteUISetStatus("No network - check the TCP/IP control panel.");
        return;
    }

    gQueueCount = 0;
    gQueueAt    = 0;
    for (i = 0; i < GazetteCoreFeedCount(); i++) {
        if (GazetteCoreFeedEnabled(i)) {
            gQueue[gQueueCount++] = i;
        }
    }
    if (gQueueCount == 0) {
        GazetteUISetStatus("No feeds are switched on.");
        return;
    }

    if (GazetteFeedsCurrentSmart() >= 0) {
        gQueueSmart = GazetteFeedsCurrentSmart();
    } else if (GazetteUISelection(&kind, &selection) &&
               kind == kGazetteRowGroup) {
        gQueueGroup = selection;
    } else {
        gQueueFeed = GazetteUISelectedFeed();
    }
    (void)AdvanceGroupRefresh();
}

/* Show again whatever is on screen, from the caches: after the preferences
   change how much of a feed is kept, or a queue has refreshed the lot. */
static void ReloadCurrentView(void)
{
    int kind      = 0;
    int selection = 0;

    if (GazetteFeedsCurrentSmart() >= 0) {
        ShowSmart(GazetteFeedsCurrentSmart());
    } else if (GazetteUISelection(&kind, &selection) &&
               kind == kGazetteRowGroup) {
        ShowGroup(selection);
    } else {
        ShowFeed(GazetteUISelectedFeed());
    }
}

/* Refresh the row the contextual menu was opened on: a feed, or every feed
   of a group, or — for a standing view, which has no one feed behind it —
   the lot. */
static void HandleRefreshSelection(void)
{
    char message[224];
    int  kind      = 0;
    int  selection = 0;
    int  feedIndex = GazetteUISelectedFeed();

    if (GazetteFeedsRefreshGetState() == kGazetteRefreshRunning) {
        return;                     /* one connection, one fetch */
    }
    if (!gNetUp) {
        GazetteUISetStatus("No network - check the TCP/IP control panel.");
        return;
    }
    if (GazetteCoreFeedCount() == 0) {
        GazetteUISetStatus("No feeds configured.");
        return;
    }

    /* A standing view has no one feed behind it, so it refreshes the lot. */
    if (GazetteFeedsCurrentSmart() >= 0) {
        int i;

        gQueueCount = 0;
        gQueueAt    = 0;
        for (i = 0; i < GazetteCoreFeedCount(); i++) {
            if (GazetteCoreFeedEnabled(i)) {
                gQueue[gQueueCount++] = i;
            }
        }
        if (gQueueCount == 0) {
            GazetteUISetStatus("No feeds are switched on.");
            return;
        }
        gQueueSmart = GazetteFeedsCurrentSmart();
        (void)AdvanceGroupRefresh();
        return;
    }

    /* A group refreshes everything in it, in turn. */
    if (GazetteUISelection(&kind, &selection) && kind == kGazetteRowGroup) {
        int i;

        gQueueCount = 0;
        gQueueAt    = 0;
        for (i = 0; i < GazetteCoreFeedCount(); i++) {
            if (GazetteCoreFeedGroup(i) == selection &&
                GazetteCoreFeedEnabled(i)) {
                gQueue[gQueueCount++] = i;
            }
        }
        if (gQueueCount == 0) {
            GazetteUISetStatus("This group has no feeds switched on.");
            return;
        }
        gQueueGroup = selection;
        (void)AdvanceGroupRefresh();
        return;
    }

    if (!GazetteFeedsRefreshStart(feedIndex, GazetteCoreFeedURL(feedIndex),
                                  PrefsMaxArticles(),
                                  (feedIndex == gDiscoverFeed) ? 1 : 0)) {
        snprintf(message, sizeof message, "Failed: %s",
                 GazetteFeedsRefreshErrorText());
        GazetteUISetStatus(message);
        return;
    }

    snprintf(message, sizeof message, "Fetching %s\311",
             GazetteCoreFeedTitle(feedIndex));
    GazetteUISetStatus(message);
}

/*
 * A refresh came back with nothing that looked like a feed, and the document
 * named one. Point the feed at it and try again — which is what turns a
 * pasted home page into a subscription.
 *
 * Returns true when a second refresh was started, so the caller leaves the
 * status line and the window alone until that one lands.
 */
static Boolean TryDiscovery(void)
{
    const char *found = GazetteFeedsDiscoveredURL();
    int         feed  = gDiscoverFeed;
    GazetteURL  base;
    GazetteURL  target;
    char        resolved[kGazetteURLLen];
    char        wasURL[kGazetteURLLen];
    char        message[224];

    /* One attempt, whatever happens below. */
    gDiscoverFeed = -1;

    if (feed < 0 || feed >= GazetteCoreFeedCount() || found[0] == '\0') {
        return false;
    }

    snprintf(wasURL, sizeof wasURL, "%s", GazetteCoreFeedURL(feed));

    /* The href is whatever the page wrote, so it is resolved against the page
       it was found on -- "/feed" and "feed.xml" are both ordinary. */
    if (!GazetteURLSplit(wasURL, strlen(wasURL), &base)) {
        return false;
    }
    if (!GazetteURLResolve(&base, found, strlen(found), &target)) {
        return false;
    }
    if (GazetteURLFormat(&target, resolved, sizeof resolved) == 0) {
        return false;
    }
    if (gz_stricmp(resolved, wasURL) == 0) {
        return false;               /* the page pointed at itself */
    }

    if (!GazetteCoreSetFeedURL(feed, resolved)) {
        /* Already subscribed to under its real address. Say so rather than
           leaving a duplicate that will never load. */
        GazetteUISetStatus("That site's feed is already in the list.");
        return false;
    }
    GazetteFeedsForgetCache(wasURL);
    GazetteIndexForgetFeed(wasURL);
    GazetteCoreSavePrefs();
    GazetteUIFeedsChanged();

    if (!GazetteFeedsRefreshStart(feed, resolved, PrefsMaxArticles(), 0)) {
        return false;
    }
    snprintf(message, sizeof message, "Found a feed on that page - "
             "fetching it\311");
    GazetteUISetStatus(message);
    return true;
}

static void PumpRefresh(void)
{
    static int lastProgress = -1;
    char       message[224];
    Boolean    queued;

    if (GazetteFeedsRefreshGetState() != kGazetteRefreshRunning) {
        return;
    }

    /* Whether what just finished was one feed of a queue. Read before the
       pump, because finishing the queue is what clears it. */
    queued = QueueRunning();

    switch (GazetteFeedsRefreshPump()) {
        case kGazetteRefreshDone:
            gLastRefreshTicks = TickCount();
            lastProgress      = -1;

            /* The feed has said where its site is, or said it again; either
               way the preferences carry it from here, for Open Home Page. */
            if (GazetteFeedsRefreshHome()[0] != '\0' &&
                GazetteCoreSetFeedHome(GazetteFeedsRefreshFeedIndex(),
                                       GazetteFeedsRefreshHome())) {
                GazetteCoreSavePrefs();
            }
            if (queued) {
                /*
                 * Either more of the queue to fetch, or it has just finished
                 * and gathered the group — or the standing view — back
                 * together and said so. Nothing below applies either way: it
                 * is about one feed's refresh landing.
                 *
                 * Asked *before* the call, not after. AdvanceGroupRefresh
                 * clears gQueueGroup on its way out, so the test that used
                 * to be here — for a queue still being set after it returned
                 * false — could never be true, and a finished group refresh
                 * went on to overwrite its own status line with the last
                 * feed's.
                 */
                (void)AdvanceGroupRefresh();
                break;
            }
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
            /* What came back may have been a page that names its feed. */
            if (TryDiscovery()) {
                break;
            }
            /* One feed of a group failing is not the group failing: carry on
               to the next and let the ones that worked show. */
            if (queued) {
                (void)AdvanceGroupRefresh();
                break;
            }
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
    if (QueueRunning()) {
        return;                     /* a queued refresh is already running */
    }
    /* A merged view is on screen — a group, or one of the standing views.
       The clock refreshes one feed, and replacing what is merged with that
       one feed's articles is not what anyone asked for; Command-R on the
       view is. */
    if (GazetteFeedsCurrentGroup() >= 0 || GazetteFeedsCurrentSmart() >= 0) {
        return;
    }
    /* A feed switched off is skipped by the clock, not by the user: asking
       for it explicitly with Refresh still fetches it. */
    if (!GazetteCoreFeedEnabled(GazetteUISelectedFeed())) {
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
    HandleRefreshSelection();
}

/* ------------------------------------------------------------------ */
/* Cleanup                                                             */
/* ------------------------------------------------------------------ */

static void DoExitGazette(void)
{
    /* Nothing in flight may outlive the application: cancelling closes the
       connection and frees the parser or the extractor behind it. */
    GazetteFeedsRefreshCancel();
    GazetteFeedsFullTextCancel();
    GazettePhotosCancel();

    /* And what was read in the feed still on screen. */
    GazetteFeedsFlush();
    GazetteIndexSave();

    GazetteUIClose();

    /* Writes the preferences back out if anything changed them — including a
       first run, which saves the defaults so there is a file to hand-edit. */
    GazetteCoreShutdown();
    GazetteNetShutdown();
}
