/*
 * Gazette — the Platinum main window
 * Copyright (c) 2026 brunocastello
 *
 * The Newsstand layout: a feed list down the left, headlines top right, the
 * article below them, a status line along the bottom. Everything the Toolbox
 * knows about the interface lives behind this header — main.cpp owns the event
 * loop and the menus and nothing else.
 *
 * The window never reaches for the network itself. A click on a feed calls
 * back into the shell, which decides whether that means reading the cache or
 * opening a connection; the dependency runs one way only, and the drawing code
 * stays testable in the only sense that matters here — by looking at it.
 */
#ifndef GAZETTE_PLATINUM_WINDOW_H
#define GAZETTE_PLATINUM_WINDOW_H

#include <MacTypes.h>
#include <MacWindows.h>
#include <Events.h>

#include <stddef.h>

/* For kGazetteRowFeed / kGazetteRowGroup — the sidebar's selection is one or
   the other, and the row model that says so is portable code. */
#include "prefs/gazette_prefs.h"

#ifdef __cplusplus
extern "C" {
#endif

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

Boolean   GazetteUIOpen(GazetteUIFeedChosen onFeedChosen,
                        GazetteUIArticleChosen onArticleChosen,
                        GazetteUIGroupChosen onGroupChosen,
                        GazetteUISmartChosen onSmartChosen,
                        GazetteUICommandChosen onCommand);
void      GazetteUIClose(void);
WindowRef GazetteUIWindow(void);

/* Draw everything. Call between BeginUpdate and EndUpdate. */
void GazetteUIUpdate(void);

/* Re-lay-out after a resize or a zoom, and redraw. */
void GazetteUIResized(void);

/* How wide the sidebar and the headline column are, as the dividers stand.
   The shell reads these to remember them; see RememberWindowLayout there. */
void GazetteUIColumnWidths(short *sidebar, short *list);

/* The smallest the window may be grown to. */
enum {
    kGazetteMinWindowWidth  = 420,
    kGazetteMinWindowHeight = 260
};

void GazetteUIActivate(Boolean active);

/* A click in the content region, in local coordinates. */
void GazetteUIClick(Point where, EventModifiers modifiers);

/*
 * A moment with nothing else to do. The toolbar's buttons raise their frame
 * under the mouse the way Outlook Express's do, and this is where the window
 * looks to see where the mouse is: call it from the event loop's idle branch
 * and after every event, and it costs a GetMouse and a few PtInRects.
 */
void GazetteUIIdle(void);

/*
 * A keystroke, with the modifiers that came with it. Returns true when the
 * window used it. Tab moves the focus between the three panes and the arrow
 * keys drive whichever has it; see the focus notes in platinum_window.c.
 */
Boolean GazetteUIKey(short key, EventModifiers modifiers);

/* The line along the bottom. Redraws only when the text actually changes. */
void GazetteUISetStatus(const char *text);

/* ------------------------------------------------------------------ */
/* The toolbar                                                         */
/* ------------------------------------------------------------------ */

/*
 * What is typed in the search box, and what to put in it. The window owns the
 * field; the shell owns what searching means, so it reads the text out when
 * kGazetteCmdSearch arrives and writes it back when a search is cleared from
 * somewhere else.
 */
void GazetteUISearchText(char *out, size_t cap);
void GazetteUISetSearchText(const char *text);

/*
 * Bring the toolbar's buttons into line with what the window is showing —
 * which of them can do anything, and which picture the three that toggle are
 * wearing. The same job AdjustMenus does for the menu bar, and called at the
 * same moments.
 */
void GazetteUIAdjustToolbar(void);

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

/* Which article the reader pane is showing, or -1. */
int GazetteUISelectedArticle(void);

/* Its address, or "" — what "Open in Browser" hands to Internet Config. */
const char *GazetteUISelectedArticleLink(void);

/*
 * Something in the View menu has moved: the sort order, what is hidden, or
 * whether the sidebar is there. Everything downstream is re-derived, and the
 * article being read is followed across the change rather than dropped.
 */
void GazetteUIViewChanged(void);

/*
 * Open the next unread headline below the one being read. Forward only and no
 * wrap. False when there is nothing after this one, which is what the caller
 * says so in the status line.
 */
Boolean GazetteUINextUnread(void);

/*
 * The reader pane is a TextEdit record, so a drag in it selects text. These
 * two are the Edit menu's Copy: whether it should be enabled, and what it
 * does. Nothing else in the window has a selection to copy.
 */
Boolean GazetteUIReaderHasSelection(void);
void    GazetteUIReaderCopy(void);

/* The preferences' feed list has changed. */
void GazetteUIFeedsChanged(void);

int  GazetteUISelectedFeed(void);
void GazetteUISelectFeed(int index);

/*
 * What the sidebar has selected: kGazetteRowFeed with an index into the feeds,
 * or kGazetteRowGroup with one into the groups. Returns false when there is
 * nothing to select, which is a sidebar with no feeds and no groups in it.
 *
 * A selected group does not change the headline list. It has no articles of
 * its own to show, and throwing away the article the user was reading because
 * they clicked a folder would be worse than leaving it up.
 */
Boolean GazetteUISelection(int *kind, int *index);
void    GazetteUISelectGroup(int index);

/* Open one of the three standing views, as if its row had been clicked.
   This is what the Article menu's Today, All Unread and Starred do. */
void    GazetteUISelectSmart(int which);

/*
 * The sidebar row under a point in the window, for a contextual click: the
 * row's kind (kGazetteRowSmart, kGazetteRowGroup or kGazetteRowFeed) and its
 * index. False when the point is not on a row.
 */
Boolean GazetteUISidebarRowAt(Point where, int *kind, int *index);

/* Choose a row exactly as a click on it would — the callback, the state
   and the highlight all move together. A contextual click selects what it
   is over before its menu appears, the way the Finder's does. */
void    GazetteUIChooseRow(int kind, int index);

/* The same two for the headline list: the article under a point, and
   choosing one as a click does — which opens it, and so reads it. */
Boolean GazetteUIArticleRowAt(Point where, int *index);
void    GazetteUIChooseArticle(int index);

#ifdef __cplusplus
}
#endif

#endif /* GAZETTE_PLATINUM_WINDOW_H */
