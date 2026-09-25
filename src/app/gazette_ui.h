/*
 * Gazette — what the application asks of its window, on every system
 * Copyright (c) 2026 brunocastello
 *
 * The part of the window's interface that src/app/ calls: the status line,
 * the selection, and "something under you has changed". It is plain C with
 * no system headers, so gazette_app.c is one source for both shells:
 * src/ui/platinum_window.c answers it on Mac OS 9 and
 * src/win/gazette_win_window.c on Windows.
 *
 * Everything that is only one system's -- events, points, window refs, the
 * About window -- stays in that shell's own header (platinum_window.h,
 * gazette_win.h). Text crossing this line is in the system's own character
 * set: MacRoman on the Mac, Windows-1252 on Windows.
 */
#ifndef GAZETTE_UI_H
#define GAZETTE_UI_H

#include <stddef.h>

/* Boolean, and kGazetteRowFeed / kGazetteRowGroup: the sidebar's
   selection is one or the other, and the row model that says so is
   portable code. */
#include "core/gazette_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* What the window calls back with                                     */
/* ------------------------------------------------------------------ */

/* Called when the user picks a feed in the sidebar. */
typedef void (*GazetteUIFeedChosen)(int feedIndex);

/* Called when the user opens an article — by clicking it or by arrowing onto
   it. What the shell does with that is fetch the full text, when the
   preference asks for it; the window itself has no opinion. */
typedef void (*GazetteUIArticleChosen)(int articleIndex);

/* Called when the user picks a group. A group is a place to read from, not
   only a place to keep feeds: what the shell does with this is show every
   article in it. */
typedef void (*GazetteUIGroupChosen)(int groupIndex);

/* Called when the user picks one of the three standing views at the top of
   the sidebar — Today, All Unread, Starred. See kGazetteSmartToday and its
   neighbours in prefs/gazette_prefs.h. */
typedef void (*GazetteUISmartChosen)(int which);

/*
 * The toolbar's commands. Every one of them is something a menu item already
 * does, and the shell maps these onto the very same handlers — which is the
 * point of naming them rather than giving the window a callback per button:
 * a toolbar that does *almost* what its menu item does is worse than no
 * toolbar, and there is nowhere here for the two to drift apart.
 */
enum {
    kGazetteCmdHideSidebar = 1,
    kGazetteCmdRefresh,
    kGazetteCmdMarkAllRead,
    kGazetteCmdHideReadArticles,
    kGazetteCmdMarkRead,
    kGazetteCmdMarkStarred,
    kGazetteCmdNextUnread,
    kGazetteCmdOpenInBrowser,
    kGazetteCmdSearch,          /* the search box, on Return */
    kGazetteCmdNewFeed,         /* the two halves of the New button's menu */
    kGazetteCmdNewGroup
};

typedef void (*GazetteUICommandChosen)(int command);

/* ------------------------------------------------------------------ */
/* What the application tells the window                               */
/* ------------------------------------------------------------------ */

/* The line along the bottom. Redraws only when the text actually changes. */
void GazetteUISetStatus(const char *text);

/* Draw everything again: read or starred marks have moved, and nothing
   else has. */
void GazetteUIUpdate(void);

/*
 * The article store has been replaced — by a refresh finishing or a cache
 * being read. Resets the selection and the scroll positions, unless
 * GazetteUIKeepPlace was called first.
 */
void GazetteUIArticlesChanged(void);

/*
 * Before a refresh: note the open article, by its link, and how far down the
 * reader is scrolled. The next GazetteUIArticlesChanged selects that article
 * again and puts the reader back where it was, if the article is still in
 * the list — an auto-refresh must not pull the reader off what they are
 * reading. Once only: the note is used, or dropped, by that next call.
 */
void GazetteUIKeepPlace(void);

/*
 * The text of the article being read has changed under the window — the full
 * text arrived, or the attempt to get it failed. Re-wraps and redraws the
 * reader pane and nothing else, so the headline list does not flicker and the
 * selection is left where it is.
 */
void GazetteUIArticleTextChanged(void);

/* A photograph has landed or been given up on: the article is composed
   again with the space it needs, keeping the reader's place. */
void GazetteUIPhotosChanged(void);

/*
 * Something in the View menu has moved: the sort order, what is hidden, or
 * whether the sidebar is there. Everything downstream is re-derived, and the
 * article being read is followed across the change rather than dropped.
 */
void GazetteUIViewChanged(void);

/* The preferences' feed list has changed. */
void GazetteUIFeedsChanged(void);

/* ------------------------------------------------------------------ */
/* What the window is showing                                          */
/* ------------------------------------------------------------------ */

/* Which article the reader pane is showing, or -1. */
int GazetteUISelectedArticle(void);

/* Its address, or "" — what "Open in Browser" hands to the browser. */
const char *GazetteUISelectedArticleLink(void);

/*
 * Open the next unread headline below the one being read. Forward only and no
 * wrap. False when there is nothing after this one, which is what the caller
 * says so in the status line.
 */
Boolean GazetteUINextUnread(void);

int  GazetteUISelectedFeed(void);
void GazetteUISelectFeed(int index);

/*
 * What the sidebar has selected: kGazetteRowFeed with an index into the feeds,
 * or kGazetteRowGroup with one into the groups. Returns false when there is
 * nothing to select, which is a sidebar with no feeds and no groups in it.
 */
Boolean GazetteUISelection(int *kind, int *index);
void    GazetteUISelectGroup(int index);

/* Open one of the three standing views, as if its row had been clicked.
   This is what the Feeds menu's Today, All Unread and Starred do. */
void    GazetteUISelectSmart(int which);

/*
 * What is typed in the search box, and what to put in it. The window owns the
 * field; the application owns what searching means. A window with no box
 * (Windows asks with its own Find dialog) answers "" and ignores the text.
 */
void GazetteUISearchText(char *out, size_t cap);
void GazetteUISetSearchText(const char *text);

#ifdef __cplusplus
}
#endif

#endif /* GAZETTE_UI_H */
