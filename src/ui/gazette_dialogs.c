/*
 * Gazette — the modal dialogs of feed management
 * Copyright (c) 2026 brunocastello
 *
 * See gazette_dialogs.h. Everything here is the Dialog Manager doing the
 * work; this file only fills the items in, runs the modal loop and reads the
 * text back out.
 */

#include "ui/gazette_dialogs.h"

#include <Dialogs.h>
#include <Events.h>
#include <MacWindows.h>
#include <TextUtils.h>

#include <string.h>

/* Must match the #defines in Resources/Gazette.r. */
enum {
    kFeedDialogID   = 129,
    kNameDialogID   = 130,
    kConfirmAlertID = 131
};

/* Item numbers, in the order the DITLs list them. */
enum {
    kItemOK     = 1,
    kItemCancel = 2
};

enum {
    kFeedItemURL   = 4,
    kFeedItemTitle = 6
};

enum {
    kNameItemPrompt = 3,
    kNameItemText   = 4
};

static GazetteDialogIdle gIdle;

void GazetteDialogsSetIdle(GazetteDialogIdle idle)
{
    gIdle = idle;
}

/* ------------------------------------------------------------------ */
/* The filter                                                          */
/* ------------------------------------------------------------------ */

/*
 * Null events are the whole reason this exists: they are the modal loop's
 * idle time, and pumping the fetch from here is what keeps a refresh alive
 * while a dialog is up. Everything else goes to StdFilterProc, which is what
 * turns Return into the default button and Escape into Cancel — behaviour
 * worth having from the system rather than reimplemented here.
 */
static pascal Boolean GazetteDialogFilter(DialogRef dialog, EventRecord *event,
                                          DialogItemIndex *item)
{
    if (event != NULL && event->what == nullEvent) {
        if (gIdle != NULL) {
            gIdle();
        }
        return false;
    }
    return StdFilterProc(dialog, event, item);
}

/* ------------------------------------------------------------------ */
/* Item text                                                           */
/* ------------------------------------------------------------------ */

static void SetItemText(DialogRef dialog, short item, const char *text)
{
    Handle handle = NULL;
    Rect   box;
    short  type;
    Str255 pascalText;

    GetDialogItem(dialog, item, &type, &handle, &box);
    if (handle == NULL) {
        return;
    }
    CopyCStringToPascal((text != NULL) ? text : "", pascalText);
    SetDialogItemText(handle, pascalText);
}

static void GetItemText(DialogRef dialog, short item, char *out, size_t cap)
{
    Handle handle = NULL;
    Rect   box;
    short  type;
    Str255 pascalText;

    if (out == NULL || cap == 0) {
        return;
    }
    out[0] = '\0';

    GetDialogItem(dialog, item, &type, &handle, &box);
    if (handle == NULL) {
        return;
    }
    GetDialogItemText(handle, pascalText);
    if ((size_t)pascalText[0] >= cap) {
        pascalText[0] = (unsigned char)(cap - 1);
    }
    CopyPascalStringToC(pascalText, out);
}

/* Leading and trailing spaces come from pasting far more often than from
   typing, and a URL with either is not the URL the user meant. */
static void Trim(char *s)
{
    size_t len;
    size_t start = 0;

    if (s == NULL) {
        return;
    }
    len = strlen(s);
    while (len > start && (s[len - 1] == ' ' || s[len - 1] == '\t')) {
        len--;
    }
    while (start < len && (s[start] == ' ' || s[start] == '\t')) {
        start++;
    }
    if (start > 0) {
        memmove(s, s + start, len - start);
    }
    s[len - start] = '\0';
}

/* ------------------------------------------------------------------ */
/* The modal loop                                                      */
/* ------------------------------------------------------------------ */

/* Run a dialog until OK or Cancel, and say which. Common to both dialogs
   because neither has an item that does anything before the loop ends. */
static Boolean RunDialog(DialogRef dialog)
{
    ModalFilterUPP  filter;
    DialogItemIndex item = 0;

    SetDialogDefaultItem(dialog, kItemOK);
    SetDialogCancelItem(dialog, kItemCancel);
    SetDialogTracksCursor(dialog, true);

    ShowWindow(GetDialogWindow(dialog));

    filter = NewModalFilterUPP(GazetteDialogFilter);
    do {
        ModalDialog(filter, &item);
    } while (item != kItemOK && item != kItemCancel);

    if (filter != NULL) {
        DisposeModalFilterUPP(filter);
    }
    return (item == kItemOK);
}

/* ------------------------------------------------------------------ */
/* The dialogs                                                         */
/* ------------------------------------------------------------------ */

Boolean GazetteAskFeed(char *url, size_t urlCap, char *title, size_t titleCap)
{
    DialogRef dialog;
    Boolean   ok;

    if (url == NULL || urlCap == 0 || title == NULL || titleCap == 0) {
        return false;
    }

    dialog = GetNewDialog(kFeedDialogID, NULL, (WindowRef)-1L);
    if (dialog == NULL) {
        return false;
    }

    SetItemText(dialog, kFeedItemURL, url);
    SetItemText(dialog, kFeedItemTitle, title);
    SelectDialogItemText(dialog, kFeedItemURL, 0, 32767);

    ok = RunDialog(dialog);
    if (ok) {
        GetItemText(dialog, kFeedItemURL, url, urlCap);
        GetItemText(dialog, kFeedItemTitle, title, titleCap);
        Trim(url);
        Trim(title);
        ok = (url[0] != '\0');
    }

    DisposeDialog(dialog);
    return ok;
}

Boolean GazetteAskName(const char *prompt, char *name, size_t cap)
{
    DialogRef dialog;
    Boolean   ok;

    if (name == NULL || cap == 0) {
        return false;
    }

    dialog = GetNewDialog(kNameDialogID, NULL, (WindowRef)-1L);
    if (dialog == NULL) {
        return false;
    }

    SetItemText(dialog, kNameItemPrompt, prompt);
    SetItemText(dialog, kNameItemText, name);
    SelectDialogItemText(dialog, kNameItemText, 0, 32767);

    ok = RunDialog(dialog);
    if (ok) {
        GetItemText(dialog, kNameItemText, name, cap);
        Trim(name);
        ok = (name[0] != '\0');
    }

    DisposeDialog(dialog);
    return ok;
}

Boolean GazetteConfirmRemove(const char *message)
{
    ModalFilterUPP filter;
    Str255         pascalText;
    short          item;

    CopyCStringToPascal((message != NULL) ? message : "", pascalText);
    ParamText(pascalText, "\p", "\p", "\p");

    /* The same filter: an alert stops the event loop exactly as a dialog
       does, and a fetch in flight should not notice either. */
    filter = NewModalFilterUPP(GazetteDialogFilter);
    item   = CautionAlert(kConfirmAlertID, filter);
    if (filter != NULL) {
        DisposeModalFilterUPP(filter);
    }

    /* Item 1 is Remove, and Alert() has already made it the default. */
    return (item == kItemOK);
}
