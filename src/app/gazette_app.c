/*
 * Gazette — the application, apart from its window and its menus
 * Copyright (c) 2026 brunocastello
 *
 * Lifted from src/main.cpp, where it was written for Mac OS 9, so that the
 * Windows shell runs the same code. What changed in the move is only what
 * had to: TickCount became GazetteNetTicks, NewPtrClear GazetteSysAlloc,
 * and the few words in the status line that belong to one system -- its
 * ellipsis and quotes, the Command key, where the network is set up --
 * are the GZ_ names below. Everything else is main.cpp's, comments and all.
 */
#include <stdio.h>
#include <string.h>

#include "app/gazette_app.h"
#include "app/gazette_ui.h"
#include "core/gazette_core.h"
#include "core/gazette_sys.h"
#include "feeds/gazette_feeds.h"
#include "feeds/gazette_index.h"
#include "feeds/gazette_photos.h"
#include "net/gazette_net.h"
#include "portable/gazette_portable.h"
#include "portable/gazette_url.h"
#include "prefs/gazette_opml.h"
#include "store/gazette_store.h"

/*
 * The status line's words that are one system's. Status text is in the
 * system's own character set (see app/gazette_ui.h): the ellipsis and the
 * curly quotes are MacRoman 0xC9, 0xD2 and 0xD3 on the Mac, and
 * Windows-1252 0x85, 0x93 and 0x94 on Windows -- both fonts have them.
 */
#ifdef GAZETTE_WIN32
#define GZ_ELLIPSIS      "\205"
#define GZ_LQUOTE        "\223"
#define GZ_RQUOTE        "\224"
#define GZ_COMMAND       "Ctrl+"
#define GZ_NETWORK_HINT  "check the Network control panel"
#define GZ_DATA_FOLDER   "the Gazette Cache folder, beside Gazette"
#else
#define GZ_ELLIPSIS      "\311"
#define GZ_LQUOTE        "\322"
#define GZ_RQUOTE        "\323"
#define GZ_COMMAND       "Command-"
#define GZ_NETWORK_HINT  "check the TCP/IP control panel"
#define GZ_DATA_FOLDER   "the Gazette Cache folder, inside Preferences"
#endif

static void StartPhotos(void);


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

/* ------------------------------------------------------------------ */
/* The Article menu                                                    */
/* ------------------------------------------------------------------ */

void GazetteAppMarkRead(void)
{
    int                   index   = GazetteUISelectedArticle();
    const GazetteArticle *article = GazetteFeedsArticleAt(index);

    if (article == NULL) {
        return;
    }
    GazetteFeedsMarkRead(index, article->read ? 0 : 1);
    GazetteUIUpdate();
}

void GazetteAppMarkAllRead(void)
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

void GazetteAppMarkRange(Boolean below)
{
    int index = GazetteUISelectedArticle();

    if (GazetteFeedsArticleAt(index) == NULL) {
        return;
    }
    GazetteFeedsMarkRange(index, below ? 1 : 0);

    if (GazetteCoreHideReadArticles()) {
        GazetteUIViewChanged();
    } else {
        GazetteUIUpdate();
    }
}

void GazetteAppToggleStar(void)
{
    int                   index   = GazetteUISelectedArticle();
    const GazetteArticle *article = GazetteFeedsArticleAt(index);

    if (article == NULL) {
        return;
    }
    GazetteFeedsMarkStarred(index, article->starred ? 0 : 1);
    GazetteIndexSave();
    GazetteUIUpdate();
}

void GazetteAppNextUnread(void)
{
    if (!GazetteUINextUnread()) {
        GazetteUISetStatus("Nothing unread below this one.");
    }
}

/* ------------------------------------------------------------------ */
/* The View menu                                                       */
/*                                                                     */
/* Each of these is the same three steps: move the preference, tell     */
/* the store or the window what changed, and save. They are written     */
/* out rather than folded together because what each one has to tell    */
/* is different, and the differences are the whole of the code.         */
/* ------------------------------------------------------------------ */

void GazetteAppSortOrder(Boolean oldestFirst)
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

void GazetteAppHideReadArticles(void)
{
    Boolean wanted = GazetteCoreHideReadArticles() ? false : true;

    GazetteCoreSetHideReadArticles(wanted);
    GazetteFeedsSetHideRead(wanted ? 1 : 0);
    GazetteCoreSavePrefs();

    GazetteUIViewChanged();
    GazetteUISetStatus(wanted ? "Showing unread articles only."
                              : "Showing every article.");
}

void GazetteAppHideReadFeeds(void)
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
void GazetteAppShowPhotos(void)
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

/* ------------------------------------------------------------------ */
/* OPML                                                                */
/*                                                                     */
/* The whole feed list, in the format every other reader speaks. The    */
/* text is portable and host-tested (prefs/gazette_opml.h); choosing    */
/* the file is the store's: Navigation Services on the Mac, the common  */
/* Open and Save As dialogs on Windows.                                 */
/* ------------------------------------------------------------------ */

void GazetteAppImportOPML(void)
{
    char     *text;
    char      message[224];
    long      len     = 0;
    int       added   = 0;
    int       outcome;

    /* 96 KB is too much to put on this stack, and it is wanted for the
       length of one import and no longer. */
    text = (char *)GazetteSysAlloc(kGazetteOPMLMax);
    if (text == NULL) {
        GazetteUISetStatus("Not enough memory to read a feed list.");
        return;
    }

    outcome = GazetteStoreAskAndReadFile("Choose an OPML feed list to import:",
                                        text, kGazetteOPMLMax, &len);
    if (outcome == kGazetteFileDone && len > 0) {
        added = GazetteCoreImportOPML(text, (size_t)len);
    }
    GazetteSysFree(text);

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

void GazetteAppExportOPML(void)
{
    char  *text;
    char   message[224];
    size_t len;

    text = (char *)GazetteSysAlloc(kGazetteOPMLMax);
    if (text == NULL) {
        GazetteUISetStatus("Not enough memory to write a feed list.");
        return;
    }

    len = GazetteOPMLWrite(GazetteCoreGetPrefs(), text, kGazetteOPMLMax);
    if (len == 0) {
        GazetteSysFree(text);
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
                GazetteUISetStatus("Saved as " GZ_LQUOTE "Gazette Feeds.opml"
                                   GZ_RQUOTE " in " GZ_DATA_FOLDER ".");
            } else {
                GazetteUISetStatus(GazetteStoreErrorText());
            }
            break;
        default:
            break;                  /* cancelled */
    }
    GazetteSysFree(text);
}

/* ------------------------------------------------------------------ */
/* Feeds                                                               */
/* ------------------------------------------------------------------ */

static long PrefsMaxArticles(void)
{
    const GazettePrefs *prefs = GazetteCoreGetPrefs();

    return (prefs != NULL) ? prefs->maxArticles : 0;
}

/*
 * Show a feed. Reads the cache first and only reaches for the network when
 * there is nothing cached — so clicking through the sidebar is instant after
 * the first fetch, and works with the machine unplugged.
 */
void GazetteAppShowFeed(int feedIndex)
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

    if (!GazetteCoreFeedEnabled(feedIndex)) {
        /* Off means off: a feed that is switched off is not fetched by being
           looked at, any more than by the clock or by Refresh. */
        GazetteUISetStatus("Nothing cached - this feed is switched off.");
        return;
    }
    if (!GazetteNetIsUp()) {
        GazetteUISetStatus("Nothing cached, and no network - "
                           GZ_NETWORK_HINT ".");
        return;
    }

    GazetteAppRefreshSelection();
}

/*
 * An article has been opened. That is when
 * its own page is fetched: lazily, one at a time, and only for something the
 * user is actually looking at.
 */
void GazetteAppShowArticle(int articleIndex)
{
    const GazetteArticle *article;

    /* Whatever was held is for the article that was open a moment ago. */
    GazetteFeedsFullTextCancel();

    article = GazetteFeedsArticleAt(articleIndex);
    if (article == NULL || article->link[0] == '\0') {
        return;                     /* nothing to fetch: no address */
    }

    /* Read lately and still held: nothing to wait for, and nothing needed
       from the network. The same three steps the pump takes when a page
       lands. */
    if (GazetteFeedsFullTextRecall(articleIndex, article->link)) {
        StartPhotos();
        GazetteUIArticleTextChanged();
        GazetteUISetStatus("Full article.");
        return;
    }
    if (!GazetteNetIsUp()) {
        return;                     /* nothing to fetch it with */
    }

    /*
     * Started, or — if the refresh has the one connection — remembered by the
     * store and started by ResumeFullText the moment the line is free. Either
     * way the answer is yes, the page is coming, which is what the reader
     * pane asks before it decides whether to lay out the summary.
     */
    if (GazetteFeedsFullTextStart(articleIndex, article->link)) {
        GazetteUISetStatus("Reading the full article" GZ_ELLIPSIS);
    }
}

/* Start a page the store held back while the line was busy. */
static void ResumeFullText(void)
{
    if (GazetteFeedsFullTextResume()) {
        GazetteUISetStatus("Reading the full article" GZ_ELLIPSIS);
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
void GazetteAppShowGroup(int groupIndex)
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
                           "press " GZ_COMMAND "R to fetch it.");
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
void GazetteAppShowSmart(int which)
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
                GazetteUISetStatus("No starred articles - " GZ_COMMAND "L stars "
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
 * the queue is still running, which is what tells GazetteAppPumpRefresh to
 * keep the window as it is rather than showing the one feed that just
 * landed.
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
            snprintf(message, sizeof message, "Fetching %s (%d of %d)" GZ_ELLIPSIS,
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
            GazetteAppShowSmart(smart);
            return false;
        }
        if (feed >= 0) {
            GazetteAppShowFeed(feed);
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
 * refresh the row they were opened on, see GazetteAppRefreshSelection.
 */
void GazetteAppRefreshAll(void)
{
    int kind      = 0;
    int selection = 0;
    int i;

    if (GazetteFeedsRefreshGetState() == kGazetteRefreshRunning ||
        QueueRunning()) {
        return;                     /* one connection, one fetch */
    }
    if (!GazetteNetIsUp()) {
        GazetteUISetStatus("No network - " GZ_NETWORK_HINT ".");
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

    /* Whatever the refresh brings, the article being read stays open. */
    GazetteUIKeepPlace();

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
void GazetteAppReloadView(void)
{
    int kind      = 0;
    int selection = 0;

    if (GazetteFeedsCurrentSmart() >= 0) {
        GazetteAppShowSmart(GazetteFeedsCurrentSmart());
    } else if (GazetteUISelection(&kind, &selection) &&
               kind == kGazetteRowGroup) {
        GazetteAppShowGroup(selection);
    } else {
        GazetteAppShowFeed(GazetteUISelectedFeed());
    }
}

/* Refresh the row the contextual menu was opened on: a feed, or every feed
   of a group, or — for a standing view, which has no one feed behind it —
   the lot. */
void GazetteAppRefreshSelection(void)
{
    char message[224];
    int  kind      = 0;
    int  selection = 0;
    int  feedIndex = GazetteUISelectedFeed();

    if (GazetteFeedsRefreshGetState() == kGazetteRefreshRunning) {
        return;                     /* one connection, one fetch */
    }
    if (!GazetteNetIsUp()) {
        GazetteUISetStatus("No network - " GZ_NETWORK_HINT ".");
        return;
    }
    if (GazetteCoreFeedCount() == 0) {
        GazetteUISetStatus("No feeds configured.");
        return;
    }

    /* Whatever the refresh brings — and the automatic one comes through
       here — the article being read stays open, scrolled where it was. */
    GazetteUIKeepPlace();

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

    /* A feed switched off is not fetched, not even when asked by name:
       Turn Off is the promise that nothing goes over the wire for it until
       it is turned on again. Its cache stays readable. */
    if (!GazetteCoreFeedEnabled(feedIndex)) {
        GazetteUISetStatus("This feed is switched off.");
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

    snprintf(message, sizeof message, "Fetching %s" GZ_ELLIPSIS,
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
             "fetching it" GZ_ELLIPSIS);
    GazetteUISetStatus(message);
    return true;
}

void GazetteAppPumpRefresh(void)
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
            gLastRefreshTicks = GazetteNetTicks();
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
            gLastRefreshTicks = GazetteNetTicks();
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
                             "Reading" GZ_ELLIPSIS " %d articles", progress);
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

    if (prefs == NULL || prefs->refreshMinutes <= 0) {
        return;                     /* 0 means manual only */
    }
    if (!GazetteNetIsUp() || GazetteFeedsRefreshGetState() == kGazetteRefreshRunning) {
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
    /* A feed switched off is never fetched; the clock is one more thing
       that leaves it alone. */
    if (!GazetteCoreFeedEnabled(GazetteUISelectedFeed())) {
        return;
    }
    if (gLastRefreshTicks == 0) {
        gLastRefreshTicks = GazetteNetTicks();
        return;
    }

    interval = (unsigned long)prefs->refreshMinutes * 60UL * 60UL;
    if (GazetteNetTicks() - gLastRefreshTicks < interval) {
        return;
    }

    gLastRefreshTicks = GazetteNetTicks();
    GazetteAppRefreshSelection();
}

/* ------------------------------------------------------------------ */
/* The idle slice                                                      */
/* ------------------------------------------------------------------ */

/*
 * This is the one place network I/O advances, and nothing it calls blocks:
 * a pump does whatever work is available this pass and returns. Blocking
 * here would stop the whole machine cooperating, not just Gazette.
 */
void GazetteAppPumpNetwork(void)
{
    GazetteAppPumpRefresh();
    PumpFullText();
    ResumeFullText();
    PumpPhotos();
    ResumePhotos();
    CheckAutoRefresh();
}

void GazetteAppRestartClock(void)
{
    gLastRefreshTicks = GazetteNetTicks();
}

void GazetteAppDiscoverNext(int feedIndex)
{
    gDiscoverFeed = feedIndex;
}

/*
 * Search what is on screen. That is one feed, or — since a group is readable
 * — every feed in a group, so searching a whole section of the sidebar is a
 * matter of selecting it first. It is a filter over what is held rather than
 * a search of the disk: reading a hundred cache files to answer a keystroke
 * is not something these machines should be asked to do.
 *
 * An empty text is how a search is cleared — which is why there is no
 * "Show All Articles" beside Find.
 */
void GazetteAppSearch(const char *text)
{
    char message[224];

    GazetteFeedsSetFilter(text);
    GazetteUIArticlesChanged();

    if (text == NULL || text[0] == '\0') {
        snprintf(message, sizeof message, "%d articles.",
                 GazetteFeedsArticleCount());
    } else if (GazetteFeedsArticleCount() == 0) {
        snprintf(message, sizeof message,
                 "Nothing here contains " GZ_LQUOTE "%s" GZ_RQUOTE ".", text);
    } else {
        snprintf(message, sizeof message, "%d of %d articles contain "
                 GZ_LQUOTE "%s" GZ_RQUOTE ".", GazetteFeedsArticleCount(),
                 GazetteFeedsTotalCount(), text);
    }
    GazetteUISetStatus(message);
}

void GazetteAppShutdown(void)
{
    /* Nothing in flight may outlive the application: cancelling closes the
       connection and frees the parser or the extractor behind it. */
    GazetteFeedsRefreshCancel();
    GazetteFeedsFullTextCancel();
    GazettePhotosCancel();

    /* And what was read in the feed still on screen. */
    GazetteFeedsFlush();
    GazetteIndexSave();
}
