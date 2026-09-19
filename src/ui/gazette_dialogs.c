/*
 * Gazette — the modal dialogs of feed management
 * Copyright (c) 2026 brunocastello
 *
 * See gazette_dialogs.h. Everything here is the Dialog Manager doing the
 * work; this file only fills the items in, runs the modal loop and reads the
 * text back out.
 */

#include "ui/gazette_dialogs.h"

#include "portable/gazette_portable.h"  /* gz_parse_dec */

#include <Controls.h>
#include <ControlDefinitions.h>
#include <Dialogs.h>
#include <Events.h>
#include <MacWindows.h>
#include <Menus.h>
#include <TextUtils.h>

#include <stdio.h>
#include <string.h>

/* Must match the #defines in Resources/Gazette.r. */
enum {
    kFeedDialogID   = 129,
    kNameDialogID   = 130,
    kConfirmAlertID = 131,
    kPrefsDialogID  = 132
};

enum {
    kPrefsItemMinutes      = 4,
    kPrefsItemMinutesNote  = 6,     /* a user item: see DrawItemControl */
    kPrefsItemArticles     = 8,
    kPrefsItemArticlesNote = 10
};

/* Item numbers, in the order the DITLs list them. */
enum {
    kItemOK     = 1,
    kItemCancel = 2
};

enum {
    kFeedItemNote  = 3,             /* a user item: see DrawItemControl */
    kFeedItemTitle = 5,
    kFeedItemURL   = 7,
    kFeedItemGroup = 9              /* likewise */
};

/* A static text control, for the notes: 288 is kControlStaticTextProc. */
enum {
    kNoteProc = 288
};

/*
 * Two kinds of item are controls the code makes rather than items the DITL
 * describes, standing on user items: the Group popup, and the notes. The
 * item is what gets each drawn — the Dialog Manager draws its items and
 * nothing else, and the item's draw procedure draws the control — and the
 * filter hands the popup its clicks, since ModalDialog tracks only items.
 *
 * The notes are controls for their font: a DITL's own static text is set
 * in the dialog font, and a control can be told to use the Appearance
 * Manager's small system font by its meta-number, so it is Geneva 10 on a
 * stock system and whatever the Appearance control panel says otherwise.
 *
 * The popup: kControlPopupButtonProc plus the fixed-width variant; -12345
 * for the menu ID says there is no menu resource, the menu is handed in by
 * handle.
 */
enum {
    kFeedPopupProc   = 401,
    kPopupNoMenuID   = -12345
};

static ControlHandle gFeedPopup;    /* while the feed dialog is up */
static ControlHandle gNotes[2];     /* a dialog's notes, while it is up */
static UserItemUPP   gDrawItem;     /* made once, kept */

/* The user items' draw procedure: whichever control stands on the item. */
static pascal void DrawItemControl(DialogRef dialog, DialogItemIndex item)
{
    Handle handle = NULL;
    Rect   box;
    short  type;

    GetDialogItem(dialog, item, &type, &handle, &box);
    {
        size_t i;

        for (i = 0; i < sizeof gNotes / sizeof gNotes[0]; i++) {
            Rect noteBox;

            if (gNotes[i] == NULL) {
                continue;
            }
            GetControlBounds(gNotes[i], &noteBox);
            if (EqualRect(&noteBox, &box)) {
                Draw1Control(gNotes[i]);
                return;
            }
        }
    }
    if (gFeedPopup != NULL) {
        Draw1Control(gFeedPopup);
    }
}

/* Stand a control on a user item: the item's rectangle is the control's,
   and the item's draw procedure draws it. */
static void StandOnItem(DialogRef dialog, short item, Rect *box)
{
    Handle handle = NULL;
    short  type;

    GetDialogItem(dialog, item, &type, &handle, box);
    if (gDrawItem == NULL) {
        gDrawItem = NewUserItemUPP(DrawItemControl);
    }
    SetDialogItem(dialog, item, userItem, (Handle)gDrawItem, box);
}

/* A note: static text in the small system font, on a user item. */
static ControlHandle MakeNote(DialogRef dialog, short item, const char *text)
{
    ControlHandle       note;
    ControlFontStyleRec style;
    Rect                box;

    StandOnItem(dialog, item, &box);
    note = NewControl(GetDialogWindow(dialog), &box, "\p", true, 0, 0, 0,
                      kNoteProc, 0);
    if (note == NULL) {
        return NULL;
    }
    style.flags = kControlUseFontMask;
    style.font  = kControlFontSmallSystemFont;
    (void)SetControlFontStyle(note, &style);
    (void)SetControlData(note, kControlEntireControl, kControlStaticTextTextTag,
                         (Size)strlen(text != NULL ? text : ""),
                         (Ptr)(text != NULL ? text : ""));
    return note;
}

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

    /*
     * The Preferences window is a document window run modally, and
     * ModalDialog knows nothing of a close box or of dragging one of
     * those: a click in the close box is Cancel, and a drag of the title
     * bar is a drag.
     */
    if (event != NULL && event->what == mouseDown && dialog != NULL) {
        WindowRef hit  = NULL;
        short     part = FindWindow(event->where, &hit);

        if (hit == GetDialogWindow(dialog)) {
            if (part == inGoAway) {
                if (TrackGoAway(hit, event->where)) {
                    *item = kItemCancel;
                }
                return true;
            }
            if (part == inDrag) {
                DragWindow(hit, event->where, NULL);
                *item = 0;
                return true;
            }
        }
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

    gNotes[0] = MakeNote(dialog, kFeedItemNote,
                     "The address of an RSS or Atom feed. Leave the name "
                     "empty to use the feed's own.");

    /* The popup, made where the user item is, and drawn by it. With no
       groups there is nowhere but the top level, and the one choice is
       shown grey: a menu of one is not a choice. */
    StandOnItem(dialog, kFeedItemGroup, &box);
    menu = GroupMenu(d->groups, d->groupCount);
    /* The popup CDEF reads its menu ID from `min` and its title's width
       from `max` — -1 to work it out, and the title is empty — with
       `value` the item it starts on; Gateway's settings window makes its
       popups the same way. */
    if (menu != NULL) {
        popup = NewControl(GetDialogWindow(dialog), &box, "\p", true,
                           0, kPopupNoMenuID, -1, kFeedPopupProc, 0);
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
        if (d->groupCount == 0) {
            DisableMenuItem(menu, kGroupItemTop);
        }
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
    gNotes[0]  = NULL;
    DisposeDialog(dialog);              /* takes its controls with the window */
    if (menu != NULL) {
        DeleteMenu(kGroupPopupMenuID);
        DisposeMenu(menu);
    }
    return ok;
}

Boolean GazetteAskName(const char *windowTitle, const char *prompt,
                       char *name, size_t cap)
{
    DialogRef dialog;
    Str255    title;
    Boolean   ok;

    if (name == NULL || cap == 0) {
        return false;
    }

    dialog = GetNewDialog(kNameDialogID, NULL, (WindowRef)-1L);
    if (dialog == NULL) {
        return false;
    }
    CopyCStringToPascal(windowTitle != NULL ? windowTitle : "", title);
    SetWTitle(GetDialogWindow(dialog), title);

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

/* A number as the field holds it: digits, or nothing, which is zero. */
static long ItemNumber(DialogRef dialog, short item)
{
    char text[32];

    GetItemText(dialog, item, text, sizeof text);
    Trim(text);
    return gz_parse_dec(text, strlen(text), 0);
}

static void SetItemNumber(DialogRef dialog, short item, long value)
{
    char text[32];

    snprintf(text, sizeof text, "%ld", value);
    SetItemText(dialog, item, text);
}

Boolean GazetteAskPreferences(long *refreshMinutes, long *maxArticles)
{
    DialogRef dialog;
    Boolean   ok;

    if (refreshMinutes == NULL || maxArticles == NULL) {
        return false;
    }

    dialog = GetNewDialog(kPrefsDialogID, NULL, (WindowRef)-1L);
    if (dialog == NULL) {
        return false;
    }

    gNotes[0] = MakeNote(dialog, kPrefsItemMinutesNote,
                         "Zero never refreshes by itself.");
    gNotes[1] = MakeNote(dialog, kPrefsItemArticlesNote,
                         "Zero keeps every article the feed offers.");

    SetItemNumber(dialog, kPrefsItemMinutes, *refreshMinutes);
    SetItemNumber(dialog, kPrefsItemArticles, *maxArticles);
    SelectDialogItemText(dialog, kPrefsItemMinutes, 0, 32767);

    ok = RunDialog(dialog);
    if (ok) {
        long minutes  = ItemNumber(dialog, kPrefsItemMinutes);
        long articles = ItemNumber(dialog, kPrefsItemArticles);

        *refreshMinutes = minutes < 0 ? 0 : minutes;
        *maxArticles    = articles < 0 ? 0 : articles;
    }

    gNotes[0] = NULL;
    gNotes[1] = NULL;
    DisposeDialog(dialog);              /* takes the notes with the window */
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
