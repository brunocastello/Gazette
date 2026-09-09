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
#include <TextUtils.h>

#include <stdio.h>
#include <string.h>

#include "core/gazette_core.h"
#include "feeds/gazette_feeds.h"
#include "net/gazette_net.h"
#include "ui/gazette_dialogs.h"
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
static void    ShowArticle(int articleIndex);
static void    PumpRefresh(void);
static void    PumpFullText(void);
static void    CheckAutoRefresh(void);

static void    AdjustMenus(void);
static void    HandleNewFeed(void);
static void    HandleNewGroup(void);
static void    HandleEditFeed(void);
static void    HandleRename(void);
static void    HandleRemove(void);
static void    HandleToggleEnabled(void);
static void    HandleMoveToGroup(short item);
static void    HandleToggleFullText(void);
static void    HandleMarkRead(void);
static void    HandleMarkAllRead(void);

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
    kMenuFeeds  = 131,
    kMenuWindow = 132,

    /* The hierarchical menu hanging off "Move to Group". Its ID has to be in
       the hierarchical range and unique among menus, nothing more. */
    kMenuMoveTo = 133
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

/* Feeds menu items, in the order AppendMenu() adds them below. */
enum {
    kFeedsItemNewFeed  = 1,
    kFeedsItemNewGroup = 2,
    /* 3 is a divider */
    kFeedsItemEdit     = 4,
    kFeedsItemRename   = 5,
    kFeedsItemRemove   = 6,
    /* 7 is a divider */
    kFeedsItemEnabled  = 8,
    kFeedsItemMoveTo   = 9,
    /* 10 is a divider */
    kFeedsItemFullText = 11,
    /* 12 is a divider */
    kFeedsItemMarkRead = 13,
    kFeedsItemMarkAll  = 14
};

/* Move to Group: the top level, a divider, then one item per group. */
enum {
    kMoveToItemTop   = 1,
    kMoveToFirstGroup = 3
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

    if (!GazetteUIOpen(ShowFeed, ShowArticle)) {
        return false;
    }

    /* A modal dialog runs a loop of its own, and this is what keeps a fetch
       moving inside it. See gazette_dialogs.h. */
    GazetteDialogsSetIdle(PumpRefresh);

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
    MenuRef appleMenu, fileMenu, editMenu, feedsMenu, moveToMenu, windowMenu;

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

    feedsMenu = NewMenu(kMenuFeeds, "\pFeeds");
    if (feedsMenu == nil) {
        return false;
    }
    AppendMenu(feedsMenu,
               "\pNew Feed\311/N;New Group\311;(-;"
               "Edit Feed\311;Rename\311;Remove;(-;"
               "Turn Off;Move to Group;(-;"
               "Full Article Text/T;(-;"
               "Mark as Unread/U;Mark All as Read");
    InsertMenu(feedsMenu, 0);

    /* "Move to Group" is a hierarchical item: the submenu goes in with
       hierMenu (-1) as its "before" menu, which is what tells the Menu
       Manager it hangs off another item rather than sitting in the bar.
       Its contents are rebuilt in AdjustMenus, because the groups change. */
    moveToMenu = NewMenu(kMenuMoveTo, "\pMove to Group");
    if (moveToMenu == nil) {
        return false;
    }
    InsertMenu(moveToMenu, hierMenu);
    SetMenuItemHierarchicalID(feedsMenu, kFeedsItemMoveTo, kMenuMoveTo);

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
            PumpFullText();
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
            long choice;

            /* Before the lookup, not after: a command key for a disabled item
               must not fire, and what is disabled depends on the selection. */
            AdjustMenus();
            choice = (long)MenuEvent(event);

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
            AdjustMenus();
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
            /* The text fields in the dialogs get the Edit menu's behaviour
               from the Dialog Manager; the reader pane is still to come. */
            break;

        case kMenuFeeds:
            switch (menuItem) {
                case kFeedsItemNewFeed:  HandleNewFeed();       break;
                case kFeedsItemNewGroup: HandleNewGroup();      break;
                case kFeedsItemEdit:     HandleEditFeed();      break;
                case kFeedsItemRename:   HandleRename();        break;
                case kFeedsItemRemove:   HandleRemove();        break;
                case kFeedsItemEnabled:  HandleToggleEnabled(); break;
                case kFeedsItemFullText: HandleToggleFullText(); break;
                case kFeedsItemMarkRead: HandleMarkRead();       break;
                case kFeedsItemMarkAll:  HandleMarkAllRead();    break;
                default: break;
            }
            break;

        case kMenuMoveTo:
            HandleMoveToGroup(menuItem);
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
 * Bring the Feeds menu into line with what the sidebar has selected, and
 * rebuild the groups under "Move to Group". Called just before the menus can
 * be seen — a click in the bar, and a command key — because an item that can
 * do nothing should be grey before it is read, not after it is chosen.
 */
static void AdjustMenus(void)
{
    MenuRef feeds  = GetMenuHandle(kMenuFeeds);
    MenuRef moveTo = GetMenuHandle(kMenuMoveTo);
    int     kind   = 0;
    int     index  = 0;
    Boolean any;
    Boolean feedSelected;
    Str255  itemText;
    int     i;

    if (feeds == nil) {
        return;
    }

    any          = GazetteUISelection(&kind, &index);
    feedSelected = (any && kind == kGazetteRowFeed);

    if (any) {
        MacEnableMenuItem(feeds, kFeedsItemRename);
        MacEnableMenuItem(feeds, kFeedsItemRemove);
    } else {
        DisableMenuItem(feeds, kFeedsItemRename);
        DisableMenuItem(feeds, kFeedsItemRemove);
    }

    /* The address, the on/off switch and the group are all a feed's: a group
       has no address and does not nest inside another. */
    if (feedSelected) {
        MacEnableMenuItem(feeds, kFeedsItemEdit);
        MacEnableMenuItem(feeds, kFeedsItemEnabled);
        MacEnableMenuItem(feeds, kFeedsItemMoveTo);
        SetMenuItemText(feeds, kFeedsItemEnabled,
                        GazetteCoreFeedEnabled(index) ? "\pTurn Off"
                                                      : "\pTurn On");
    } else {
        DisableMenuItem(feeds, kFeedsItemEdit);
        DisableMenuItem(feeds, kFeedsItemEnabled);
        DisableMenuItem(feeds, kFeedsItemMoveTo);
        SetMenuItemText(feeds, kFeedsItemEnabled, "\pTurn Off");
    }

    /* A preference, not a command: it shows its state with a check mark the
       way every other toggle in the menu bar does. */
    {
        const GazettePrefs *prefs = GazetteCoreGetPrefs();

        MacCheckMenuItem(feeds, kFeedsItemFullText,
                         (prefs != nil && prefs->fullText) ? true : false);
    }

    /* The read/unread pair follows the article, not the feed. The first item
       says what it would do, so it reads as one command rather than two. */
    {
        const GazetteArticle *article =
            GazetteFeedsArticleAt(GazetteUISelectedArticle());

        if (article != nil) {
            MacEnableMenuItem(feeds, kFeedsItemMarkRead);
            SetMenuItemText(feeds, kFeedsItemMarkRead,
                            article->read ? "\pMark as Unread"
                                          : "\pMark as Read");
        } else {
            DisableMenuItem(feeds, kFeedsItemMarkRead);
            SetMenuItemText(feeds, kFeedsItemMarkRead, "\pMark as Unread");
        }

        if (GazetteFeedsUnreadCount() > 0) {
            MacEnableMenuItem(feeds, kFeedsItemMarkAll);
        } else {
            DisableMenuItem(feeds, kFeedsItemMarkAll);
        }
    }

    if (moveTo == nil) {
        return;
    }

    /* The groups change under this menu, so it is rebuilt rather than
       patched; at most kGazetteMaxGroups items, once per menu click. */
    while (CountMenuItems(moveTo) > 0) {
        DeleteMenuItem(moveTo, 1);
    }
    AppendMenu(moveTo, "\pTop Level");
    if (GazetteCoreGroupCount() > 0) {
        AppendMenu(moveTo, "\p(-");
    }
    for (i = 0; i < GazetteCoreGroupCount(); i++) {
        /* AppendMenu reads its own metacharacters, so a group called "-" or
           one starting with "(" would arrive as a divider or a disabled item.
           Appending a placeholder and setting the text after it is in is the
           way past that — SetMenuItemText interprets nothing. */
        AppendMenu(moveTo, "\pGroup");
        CopyCStringToPascal(GazetteCoreGroupName(i), itemText);
        SetMenuItemText(moveTo, (short)CountMenuItems(moveTo), itemText);
    }

    /* Where the feed already is, is not somewhere to move it to. */
    if (feedSelected) {
        int group = GazetteCoreFeedGroup(index);

        if (group < 0) {
            DisableMenuItem(moveTo, kMoveToItemTop);
        } else {
            DisableMenuItem(moveTo, (MenuItemIndex)(kMoveToFirstGroup + group));
        }
    }
}

static void HandleNewFeed(void)
{
    char url[kGazetteURLLen];
    char title[kGazetteTitleLen];
    int  kind  = 0;
    int  index = 0;
    int  group = -1;
    int  added;

    url[0]   = '\0';
    title[0] = '\0';

    /* A new feed lands where the user is looking: in the selected group, or
       beside the selected feed in whatever group that is in. */
    if (GazetteUISelection(&kind, &index)) {
        group = (kind == kGazetteRowGroup) ? index
                                           : GazetteCoreFeedGroup(index);
    }

    if (!GazetteAskFeed(url, sizeof url, title, sizeof title)) {
        return;
    }

    added = GazetteCoreAddFeed(url, title, group);
    if (added < 0) {
        GazetteUISetStatus("That feed is already in the list, or the list "
                           "is full.");
        return;
    }

    GazetteCoreSavePrefs();
    GazetteUIFeedsChanged();
    ShowFeed(added);
}

static void HandleNewGroup(void)
{
    char name[kGazetteGroupLen];
    int  group;

    name[0] = '\0';
    if (!GazetteAskName("Name for the new group:", name, sizeof name)) {
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
    char url[kGazetteURLLen];
    char title[kGazetteTitleLen];
    char wasURL[kGazetteURLLen];
    int  kind  = 0;
    int  index = 0;

    if (!GazetteUISelection(&kind, &index) || kind != kGazetteRowFeed) {
        return;
    }

    snprintf(url, sizeof url, "%s", GazetteCoreFeedURL(index));
    snprintf(title, sizeof title, "%s", GazetteCoreFeedTitle(index));
    snprintf(wasURL, sizeof wasURL, "%s", url);

    if (!GazetteAskFeed(url, sizeof url, title, sizeof title)) {
        return;
    }

    if (strcmp(url, wasURL) != 0) {
        if (!GazetteCoreSetFeedURL(index, url)) {
            GazetteUISetStatus("Another feed already has that address.");
            return;
        }
        /* The old address's cache is keyed by an address nothing points at
           any more. */
        GazetteFeedsForgetCache(wasURL);
    }
    GazetteCoreRenameFeed(index, title);

    GazetteCoreSavePrefs();
    GazetteUIFeedsChanged();

    /* A new address is a different feed with a different cache file, so this
       reads that one — or fetches it when there is nothing cached yet. */
    ShowFeed(index);
}

static void HandleRename(void)
{
    int kind  = 0;
    int index = 0;

    if (!GazetteUISelection(&kind, &index)) {
        return;
    }

    if (kind == kGazetteRowGroup) {
        char name[kGazetteGroupLen];

        snprintf(name, sizeof name, "%s", GazetteCoreGroupName(index));
        if (!GazetteAskName("Name for this group:", name, sizeof name)) {
            return;
        }
        GazetteCoreRenameGroup(index, name);
    } else {
        char title[kGazetteTitleLen];

        snprintf(title, sizeof title, "%s", GazetteCoreFeedTitle(index));
        if (!GazetteAskName("Name for this feed:", title, sizeof title)) {
            return;
        }
        GazetteCoreRenameFeed(index, title);
    }

    GazetteCoreSavePrefs();
    GazetteUIFeedsChanged();
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
                 "Remove the group \322%s\323? The feeds in it are kept - "
                 "they move to the top of the list.",
                 GazetteCoreGroupName(index));
        if (!GazetteConfirmRemove(message)) {
            return;
        }
        GazetteCoreRemoveGroup(index);
    } else {
        char url[kGazetteURLLen];

        snprintf(message, sizeof message,
                 "Remove the feed \322%s\323? You can subscribe to it again "
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

    if (!GazetteUISelection(&kind, &index) || kind != kGazetteRowFeed) {
        return;
    }

    GazetteCoreSetFeedEnabled(index, !GazetteCoreFeedEnabled(index));
    GazetteCoreSavePrefs();
    GazetteUIFeedsChanged();
}

/*
 * Full article text on or off. It is a preference rather than a one-off
 * command, so it saves at once and takes effect on the article already open:
 * turning it on and having to click away and back would read as it not
 * working.
 */
static void HandleToggleFullText(void)
{
    const GazettePrefs *prefs = GazetteCoreGetPrefs();
    Boolean             wanted;

    if (prefs == nil) {
        return;
    }
    wanted = prefs->fullText ? false : true;

    GazetteCoreSetFullText(wanted);
    GazetteCoreSavePrefs();

    if (!wanted) {
        /* Back to the feed's own summary, and drop what was fetched. */
        GazetteFeedsFullTextCancel();
        GazetteUIArticleTextChanged();
        GazetteUISetStatus("Showing the summary each feed provides.");
        return;
    }
    ShowArticle(GazetteUISelectedArticle());
}

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
    GazetteFeedsMarkAllRead();
    GazetteUIUpdate();
}

static void HandleMoveToGroup(short item)
{
    int kind  = 0;
    int index = 0;
    int group;
    int moved;

    if (!GazetteUISelection(&kind, &index) || kind != kGazetteRowFeed) {
        return;
    }

    if (item == kMoveToItemTop) {
        group = -1;
    } else if (item >= kMoveToFirstGroup) {
        group = item - kMoveToFirstGroup;
    } else {
        return;                     /* the divider */
    }

    /* The last position in the list; the preferences then put the feed at the
       end of the group it now belongs to, which is where a new one goes. */
    moved = GazetteCoreMoveFeed(index, GazetteCoreFeedCount() - 1, group);
    if (moved < 0) {
        return;
    }

    GazetteCoreSavePrefs();
    GazetteUIFeedsChanged();
    GazetteUISelectFeed(moved);
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

/*
 * An article has been opened. With the full-text preference on, that is when
 * its own page is fetched: lazily, one at a time, and only for something the
 * user is actually looking at.
 */
static void ShowArticle(int articleIndex)
{
    const GazettePrefs   *prefs = GazetteCoreGetPrefs();
    const GazetteArticle *article;

    /* Whatever was held is for the article that was open a moment ago. */
    GazetteFeedsFullTextCancel();

    if (prefs == nil || !prefs->fullText) {
        return;
    }
    article = GazetteFeedsArticleAt(articleIndex);
    if (article == nil || article->link[0] == '\0') {
        return;
    }
    if (!gNetUp) {
        return;                     /* the summary is already on screen */
    }
    /* A refresh has the connection. The summary stands; asking again is a
       click away. */
    if (GazetteFeedsRefreshGetState() == kGazetteRefreshRunning) {
        return;
    }

    if (GazetteFeedsFullTextStart(articleIndex, article->link)) {
        GazetteUISetStatus("Reading the full article\311");
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
            /* The pane is showing the summary; this is what swaps it. */
            GazetteUIArticleTextChanged();
            GazetteUISetStatus("Full article.");
            break;

        case kGazetteRefreshFailed:
            /* The summary is still on screen and stays there. Saying why is
               worth a status line and not worth a dialog: it happens on any
               paywall, and the article is still readable. */
            snprintf(message, sizeof message, "Summary only - %s",
                     GazetteFeedsFullTextErrorText());
            GazetteUISetStatus(message);
            break;

        default:
            break;
    }
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
    HandleRefresh();
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

    /* And what was read in the feed still on screen. */
    GazetteFeedsFlush();

    GazetteUIClose();

    /* Writes the preferences back out if anything changed them — including a
       first run, which saves the defaults so there is a file to hand-edit. */
    GazetteCoreShutdown();
    GazetteNetShutdown();
}
