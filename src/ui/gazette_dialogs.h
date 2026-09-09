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
 * Ask for a feed's address and name. Both buffers are used as the starting
 * contents, so this serves editing as well as adding. Returns false when the
 * user cancels or leaves the address empty.
 *
 * The Dialog Manager hands text back as a Str255, so 255 characters is the
 * most either field can carry. A URL longer than that has to be pasted into
 * the preferences file by hand -- and one that long is a rarity worth the
 * simplicity here.
 */
Boolean GazetteAskFeed(char *url, size_t urlCap, char *title, size_t titleCap);

/* Ask for one line of text under a prompt -- a new group, or a new name for
   a feed or a group. name carries the starting contents. */
Boolean GazetteAskName(const char *prompt, char *name, size_t cap);

/* A caution alert with Remove and Cancel. Returns true for Remove. */
Boolean GazetteConfirmRemove(const char *message);

#ifdef __cplusplus
}
#endif

#endif /* GAZETTE_DIALOGS_H */
