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

Boolean   GazetteUIOpen(GazetteUIFeedChosen onFeedChosen,
                        GazetteUIArticleChosen onArticleChosen,
                        GazetteUIGroupChosen onGroupChosen);
void      GazetteUIClose(void);
WindowRef GazetteUIWindow(void);

/* Draw everything. Call between BeginUpdate and EndUpdate. */
void GazetteUIUpdate(void);

/* Re-lay-out after a resize or a zoom, and redraw. */
void GazetteUIResized(void);

void GazetteUIActivate(Boolean active);

/* A click in the content region, in local coordinates. */
void GazetteUIClick(Point where, EventModifiers modifiers);

/*
 * A keystroke, with the modifiers that came with it. Returns true when the
 * window used it. Tab moves the focus between the three panes and the arrow
 * keys drive whichever has it; see the focus notes in platinum_window.c.
 */
Boolean GazetteUIKey(short key, EventModifiers modifiers);

/* The line along the bottom. Redraws only when the text actually changes. */
void GazetteUISetStatus(const char *text);

/*
 * The article store has been replaced — by a refresh finishing or a cache
 * being read. Resets the selection and the scroll positions.
 */
void GazetteUIArticlesChanged(void);

/*
 * The text of the article being read has changed under the window — the full
 * text arrived, or the attempt to get it failed. Re-wraps and redraws the
 * reader pane and nothing else, so the headline list does not flicker and the
 * selection is left where it is.
 */
void GazetteUIArticleTextChanged(void);

/* Which article the reader pane is showing, or -1. */
int GazetteUISelectedArticle(void);

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

#ifdef __cplusplus
}
#endif

#endif /* GAZETTE_PLATINUM_WINDOW_H */
