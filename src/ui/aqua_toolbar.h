/*
 * Gazette -- the Aqua toolbar (Mac OS X, osx-legacy only)
 * Copyright (c) 2026 brunocastello
 *
 * The window's toolbar on OS X is a real HIToolbar in the window's title bar
 * (unified title and toolbar), with an HISearchField at its right-hand end.
 * Nothing in this file is compiled for Mac OS 9: platinum_window.c reaches it
 * through #ifdef GAZETTE_MACOSX, and tools/build_osx_legacy.sh is the only
 * build that lists aqua_toolbar.c.
 *
 * The window has no standard handler and its loop is WaitNextEvent, so the
 * toolbar's clicks and the search field's keystrokes do not arrive by
 * themselves. How this file gets them is written up in docs/osx-port.md,
 * Stage 2, part A.
 */
#ifndef GAZETTE_AQUA_TOOLBAR_H
#define GAZETTE_AQUA_TOOLBAR_H

#ifdef GAZETTE_MACOSX

#include <MacTypes.h>
#include <MacWindows.h>
#include <Events.h>

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The nine commands, as platinum_window.c numbers them (kTB*). The search
   field is not one of them: it has its own callback. */
enum {
    kGazetteAquaItems = 9
};

typedef void (*GazetteAquaItemProc)(int item);
typedef void (*GazetteAquaProc)(void);

/*
 * Builds the toolbar and attaches it to the window. onItem is called with the
 * item's number when one is clicked, onSearch when Return is pressed in the
 * field or its cancel button is clicked, and onToggle when the toolbar button
 * in the title bar is clicked (which is a request to show or hide).
 */
Boolean GazetteAquaToolbarCreate(WindowRef window, GazetteAquaItemProc onItem,
                                 GazetteAquaProc onSearch,
                                 GazetteAquaProc onToggle);
void    GazetteAquaToolbarDispose(void);

/* What item i says, wears and can do. The image name picks a PNG in
   Contents/Resources/toolbar/; with no such file the icon suite iconID in the
   application's resources stands in. Cheap when nothing has changed. */
void GazetteAquaToolbarSetItem(int item, Boolean enabled, const char *label,
                               const char *imageName, short iconID);
void GazetteAquaToolbarSetSearchEnabled(Boolean enabled);

/* Shows or hides the toolbar without animation. */
void GazetteAquaToolbarShow(Boolean show);

/*
 * A mouse-down the shell got from WaitNextEvent. Returns true if the toolbar
 * took it (an item, the field, the toolbar button), in which case the shell
 * does nothing more with it. part is what FindWindow said it was.
 */
Boolean GazetteAquaToolbarMouseDown(WindowRef window, const EventRecord *event,
                                    short part);

/* The search field, in MacRoman. */
void    GazetteAquaSearchText(char *out, size_t cap);
void    GazetteAquaSetSearchText(const char *text);
Boolean GazetteAquaSearchHasFocus(void);

#ifdef __cplusplus
}
#endif

#endif /* GAZETTE_MACOSX */

#endif /* GAZETTE_AQUA_TOOLBAR_H */
