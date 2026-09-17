/*
 * Gazette — the modal dialogs of feed management
 * Copyright (c) 2026 brunocastello
 *
 * See gazette_dialogs.h. Everything here is the Dialog Manager doing the
 * work; this file only fills the items in, runs the modal loop and reads the
 * text back out.
 */

#include "ui/gazette_dialogs.h"

#include <Controls.h>
#include <ControlDefinitions.h>
#include <Dialogs.h>
#include <Events.h>
#include <MacWindows.h>
#include <Menus.h>
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
    kFeedItemTitle = 5,
    kFeedItemURL   = 7
};

/*
 * The Group popup is a control of the dialog's window rather than an item
 * of its DITL: Retro68's Rez cannot compile a Control item, so the code
 * makes it, at the place the DITL leaves for it, and the filter hands it
 * its clicks — ModalDialog only tracks items. kControlPopupButtonProc plus
 * the fixed-width variant; -12345 for the menu ID says there is no menu
 * resource, the menu is handed in by handle.
 */
enum {
    kFeedPopupProc   = 401,
    kPopupNoMenuID   = -12345,
    kFeedPopupTop    = 106,
    kFeedPopupLeft   = 88,
    kFeedPopupBottom = 126,
    kFeedPopupRight  = 240
};

static ControlHandle gFeedPopup;    /* while the feed dialog is up */

/* The Group popup's menu, built here from the groups there are. An ID after
   every menu the shell and the window make. */
enum {
    kGroupPopupMenuID = 150
};

/* Its items: the top level, a divider, then one per group. */
enum {
    kGroupItemTop   = 1,
    kGroupFirstItem = 3
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

    /* A click on the Group popup: ModalDialog tracks only its items, and
       the popup is not one, so it is tracked here — the CDEF runs the menu
       with the -1 action — and reported as no item at all. */
    if (event != NULL && event->what == mouseDown && gFeedPopup != NULL &&
        dialog != NULL) {
        Point         where = event->where;
        ControlHandle hit   = NULL;

        SetPortDialogPort(dialog);
        GlobalToLocal(&where);
        if (FindControl(where, GetDialogWindow(dialog), &hit) != 0 &&
            hit == gFeedPopup) {
            (void)HandleControlClick(gFeedPopup, where, event->modifiers,
                                     (ControlActionUPP)-1L);
            *item = 0;
            return true;
        }
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

/*
 * The Group popup's menu: the top level, a divider, and the groups. Built
 * for each showing, because the groups change; handed to the popup by
 * handle, which is why its CNTL names no menu resource. AppendMenu reads its
 * own metacharacters, so each group goes in as a placeholder and is named
 * afterwards with SetMenuItemText, which interprets nothing.
 */
static MenuRef GroupMenu(const char *const *groups, int count)
{
    MenuRef menu = NewMenu(kGroupPopupMenuID, "\p");
    Str255  name;
    int     i;

    if (menu == NULL) {
        return NULL;
    }
    AppendMenu(menu, "\pTop Level");
    if (count > 0) {
        AppendMenu(menu, "\p(-");
    }
    for (i = 0; i < count; i++) {
        AppendMenu(menu, "\pGroup");
        CopyCStringToPascal(groups[i] != NULL ? groups[i] : "", name);
        SetMenuItemText(menu, (short)CountMenuItems(menu), name);
    }
    InsertMenu(menu, hierMenu);
    return menu;
}

Boolean GazetteAskFeed(GazetteFeedDialog *d)
{
    DialogRef     dialog;
    ControlHandle popup = NULL;
    MenuRef       menu  = NULL;
    Rect          box;
    Str255        title;
    Boolean       ok;

    if (d == NULL || d->url == NULL || d->urlCap == 0 || d->title == NULL ||
        d->titleCap == 0) {
        return false;
    }

    dialog = GetNewDialog(kFeedDialogID, NULL, (WindowRef)-1L);
    if (dialog == NULL) {
        return false;
    }
    CopyCStringToPascal(d->windowTitle != NULL ? d->windowTitle : "Feed",
                        title);
    SetWTitle(GetDialogWindow(dialog), title);

    SetItemText(dialog, kFeedItemTitle, d->title);
    SetItemText(dialog, kFeedItemURL, d->url);

    /* The popup, made here at the place the DITL leaves for it. */
    SetRect(&box, kFeedPopupLeft, kFeedPopupTop, kFeedPopupRight,
            kFeedPopupBottom);
    menu = GroupMenu(d->groups, d->groupCount);
    if (menu != NULL) {
        popup = NewControl(GetDialogWindow(dialog), &box, "\p", true,
                           kPopupNoMenuID, 0, -1, kFeedPopupProc, 0);
    }
    if (popup != NULL) {
        (void)SetControlData(popup, kControlEntireControl,
                             kControlPopupButtonMenuHandleTag,
                             sizeof menu, (Ptr)&menu);
        SetControlMinimum(popup, 1);
        SetControlMaximum(popup, CountMenuItems(menu));
        SetControlValue(popup,
                        (d->group >= 0 && d->group < d->groupCount)
                            ? (short)(kGroupFirstItem + d->group)
                            : kGroupItemTop);
    }
    gFeedPopup = popup;

    /* The name first: it is what the user has in their head, and the one a
       new feed most often leaves empty, so the cursor starts in the field
       they are likeliest to skip past. */
    SelectDialogItemText(dialog,
                         d->url[0] == '\0' ? kFeedItemURL : kFeedItemTitle,
                         0, 32767);

    ok = RunDialog(dialog);
    if (ok) {
        GetItemText(dialog, kFeedItemURL, d->url, d->urlCap);
        GetItemText(dialog, kFeedItemTitle, d->title, d->titleCap);
        Trim(d->url);
        Trim(d->title);
        ok = (d->url[0] != '\0');

        if (popup != NULL && menu != NULL) {
            short chosen = GetControlValue(popup);

            d->group = (chosen >= kGroupFirstItem)
                           ? chosen - kGroupFirstItem : -1;
        }
    }

    gFeedPopup = NULL;
    DisposeDialog(dialog);              /* takes the popup with the window */
    if (menu != NULL) {
        DeleteMenu(kGroupPopupMenuID);
        DisposeMenu(menu);
    }
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
