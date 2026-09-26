/*
 * Gazette — the application, apart from its window and its menus
 * Copyright (c) 2026 brunocastello
 *
 * What a click means once the shell has worked out which click it was:
 * showing a feed, a group or a standing view; opening an article and
 * fetching its page and pictures; the refresh queue and the clock that
 * starts it; marks, sorting, hiding, searching and the OPML file. Lifted
 * out of src/main.cpp so the Windows shell runs the very same code, which
 * reaches the window only through app/gazette_ui.h.
 *
 * What stays in each shell: the event loop, the menus and their enabling,
 * dialogs, About, the clipboard and the browser. Plain C, no system headers.
 */
#ifndef GAZETTE_APP_H
#define GAZETTE_APP_H

#include "core/gazette_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Whether the network is up is GazetteNetIsUp's to say: the shell starts
 * it, and without it the caches still read and nothing is fetched.
 *
 * The idle slice: every fetch in flight advances by whatever is ready, and
 * the clock may start a refresh. Never blocks. Call from the event loop
 * when there is nothing else to do, and from any modal loop of the shell's.
 */
void GazetteAppPumpNetwork(void);

/* Only the feed refresh's slice — what a dialog's own loop runs, so a fetch
   is not left standing while the dialog is up. */
void GazetteAppPumpRefresh(void);

/* The window's callbacks: a row or a headline has been chosen. */
void GazetteAppShowFeed(int feedIndex);
void GazetteAppShowGroup(int groupIndex);
void GazetteAppShowSmart(int which);
void GazetteAppShowArticle(int articleIndex);

/* Show again whatever is on screen, from the caches. */
void GazetteAppReloadView(void);

/* Refresh: what is selected (the contextual menus, the clock), or every
   feed that is switched on (File > Refresh, the toolbar). */
void GazetteAppRefreshSelection(void);
void GazetteAppRefreshAll(void);

/* The clock starts over — after the preferences change the interval, so a
   shorter one is not already overdue. */
void GazetteAppRestartClock(void);

/* The next fetch of this feed may follow a page to the feed it names: a
   feed just added, or given a new address, might be a site's home page. */
void GazetteAppDiscoverNext(int feedIndex);

/* The Article menu. */
void GazetteAppMarkRead(void);          /* the open one, read <-> unread */
void GazetteAppMarkAllRead(void);       /* or all unread, if none is */
void GazetteAppMarkRange(Boolean below);
void GazetteAppToggleStar(void);
void GazetteAppNextUnread(void);

/* The View menu, less what only the window knows how to hide. */
void GazetteAppSortOrder(Boolean oldestFirst);
void GazetteAppHideReadArticles(void);
void GazetteAppHideReadFeeds(void);
void GazetteAppShowPhotos(void);

/* The Feeds menu: Turn Off / Turn On for what is selected, and Delete once
   the shell has had it confirmed. */
void GazetteAppToggleEnabled(void);
void GazetteAppRemoveSelection(void);

/* Write the preferences now. When that fails -- a full or locked disk --
   the handler is called once (the shell puts up its warning); with none,
   the status line says it. Every save in here goes this way. */
typedef void (*GazetteAppSaveFailed)(void);
void    GazetteAppSetSaveFailed(GazetteAppSaveFailed handler);
Boolean GazetteAppSavePrefs(void);

/* What a dialog's answer does, once the shell has had it: add or change a
   feed (group -1 is the top level), add or rename a group, set the two
   preferences. Each saves at once and brings the window up to date. */
int  GazetteAppSelectedGroup(void);     /* where a new feed starts: -1 top */
void GazetteAppAddFeed(const char *url, const char *title, int group);
void GazetteAppEditFeed(int feedIndex, const char *url, const char *title,
                        int group);
void GazetteAppAddGroup(const char *name);
void GazetteAppRenameGroup(int groupIndex, const char *name);
void GazetteAppSetPreferences(long refreshMinutes, long maxArticles);

/* What the view on screen is called, for the headline list's heading and
   the status line: a feed by the name in the user's list -- which they may
   have changed from the feed's own -- and a group or a standing view by
   its name. */
const char *GazetteAppViewTitle(void);

/* Keep only the articles containing text, and say how many; "" shows them
   all again. The search box on Return, and Find. */
void GazetteAppSearch(const char *text);

/* The feed list as OPML, through the store's Open and Save dialogs. */
void GazetteAppImportOPML(void);
void GazetteAppExportOPML(void);

/* Stop everything in flight and write what was read. The shell closes its
   window and the network after this. */
void GazetteAppShutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* GAZETTE_APP_H */
