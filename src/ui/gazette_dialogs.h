/*
 * Gazette — the modal dialogs of feed management
 * Copyright (c) 2026 brunocastello
 *
 * Real Dialog Manager dialogs, built from the DLOG/DITL resources in
 * Resources/Gazette.r. Nothing here draws: the buttons, the text fields, the
 * default ring and the keyboard behaviour are the Toolbox's, which is the
 * point — a dialog is exactly the kind of thing the system already knows how
 * to make look right.
 */
#ifndef GAZETTE_DIALOGS_H
#define GAZETTE_DIALOGS_H

#include <MacTypes.h>

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ModalDialog runs a loop of its own, and it is the one place Gazette's
 * single cooperative loop is not running. Register what to do with the idle
 * time and a refresh keeps moving while a dialog is up; without it, opening
 * "New Feed" mid-fetch would stall the connection until the dialog closed.
 */
typedef void (*GazetteDialogIdle)(void);
void GazetteDialogsSetIdle(GazetteDialogIdle idle);

/*
 * Ask for a feed's name, address and group. The buffers are used as the
 * starting contents, so this serves editing as well as adding, and the
 * window's title says which it is doing. `groups` names the groups there
 * are, for the popup; `group` is the one to show on the way in and the one
 * chosen on the way out, -1 for the top level. Returns false when the user
 * cancels or leaves the address empty.
 *
 * The Dialog Manager hands text back as a Str255, so 255 characters is the
 * most either field can carry. A URL longer than that has to be pasted into
 * the preferences file by hand -- and one that long is a rarity worth the
 * simplicity here.
 */
typedef struct {
    const char        *windowTitle;     /* "New Feed", "Edit Feed" */
    char              *url;
    size_t             urlCap;
    char              *title;
    size_t             titleCap;
    const char *const *groups;
    int                groupCount;
    int                group;
} GazetteFeedDialog;

Boolean GazetteAskFeed(GazetteFeedDialog *d);

/* Ask for one line of text under a prompt -- a new group, a group's new
   name, a search. The window's title says which; name carries the starting
   contents. */
Boolean GazetteAskName(const char *windowTitle, const char *prompt,
                       char *name, size_t cap);

/* A caution alert with Remove and Cancel. Returns true for Remove. */
Boolean GazetteConfirmRemove(const char *message);

#ifdef __cplusplus
}
#endif

#endif /* GAZETTE_DIALOGS_H */
