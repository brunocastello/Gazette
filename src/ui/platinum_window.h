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

#ifdef __cplusplus
extern "C" {
#endif

/* Called when the user picks a feed in the sidebar. */
typedef void (*GazetteUIFeedChosen)(int feedIndex);

Boolean   GazetteUIOpen(GazetteUIFeedChosen onFeedChosen);
void      GazetteUIClose(void);
WindowRef GazetteUIWindow(void);

/* Draw everything. Call between BeginUpdate and EndUpdate. */
void GazetteUIUpdate(void);

/* Re-lay-out after a resize or a zoom, and redraw. */
void GazetteUIResized(void);

void GazetteUIActivate(Boolean active);

/* A click in the content region, in local coordinates. */
void GazetteUIClick(Point where, EventModifiers modifiers);

/* A keystroke. Returns true when the window used it. */
Boolean GazetteUIKey(short key);

/* The line along the bottom. Redraws only when the text actually changes. */
void GazetteUISetStatus(const char *text);

/*
 * The article store has been replaced — by a refresh finishing or a cache
 * being read. Resets the selection and the scroll positions.
 */
void GazetteUIArticlesChanged(void);

/* The preferences' feed list has changed. */
void GazetteUIFeedsChanged(void);

int  GazetteUISelectedFeed(void);
void GazetteUISelectFeed(int index);

#ifdef __cplusplus
}
#endif

#endif /* GAZETTE_PLATINUM_WINDOW_H */
