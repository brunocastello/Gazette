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

/* The half of this interface that is the same on every system — the
   callbacks, the toolbar's commands, and everything src/app/ asks of a
   window — is declared there; what follows is the Toolbox's half. */
#include "app/gazette_ui.h"

#ifdef __cplusplus
extern "C" {
#endif

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

/* ------------------------------------------------------------------ */
/* The toolbar                                                         */
/* ------------------------------------------------------------------ */

/*
 * Bring the toolbar's buttons into line with what the window is showing —
 * which of them can do anything, and which picture the three that toggle are
 * wearing. The same job AdjustMenus does for the menu bar, and called at the
 * same moments.
 */
void GazetteUIAdjustToolbar(void);

/*
 * The reader pane is a TextEdit record, so a drag in it selects text. These
 * two are the Edit menu's Copy: whether it should be enabled, and what it
 * does. Nothing else in the window has a selection to copy.
 */
Boolean GazetteUIReaderHasSelection(void);
void    GazetteUIReaderCopy(void);

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
