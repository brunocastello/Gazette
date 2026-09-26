/*
 * gazette_win_dialogs.c - the modal dialogs of feed management, in
 * Windows' own controls.
 *
 * The counterpart of src/ui/gazette_dialogs.c. The fields, their order and
 * their words are the Mac's; the layout is Windows' -- DIALOG templates in
 * Resources/win/Gazette.rc, measured in dialog units with the usual 7-unit
 * margins, OK then Cancel along the bottom right, DIALOGEX with
 * DS_SHELLFONT so 2000 and XP use Tahoma and the earlier versions MS Sans
 * Serif, each its own dialog font. Nothing here draws.
 *
 * What a dialog's answer then does is app/gazette_app.c's, shared with
 * the Mac: this file only asks.
 */

#define WINVER       0x0400
#define _WIN32_WINNT 0x0400
#define _WIN32_IE    0x0300

#include <windows.h>
#include <string.h>

#include "gazette_win.h"
#include "gazette_win_res.h"
#include "app/gazette_app.h"
#include "core/gazette_core.h"

/*
 * DialogBoxParam runs a loop of its own, and while it does Gazette's is
 * not running. A timer on the dialog keeps a refresh moving underneath,
 * at the main loop's pace -- the Mac's dialogs do the same from their
 * filter's null events, and the store's Open and Save dialogs from a
 * timer of their own.
 */
enum {
    kIdleTimer = 1,
    kIdleMs    = 100
};

static void StartIdle(HWND dialog)
{
    (void)SetTimer(dialog, kIdleTimer, kIdleMs, NULL);
}

static BOOL IdleTick(UINT message, WPARAM wParam)
{
    if (message == WM_TIMER && wParam == kIdleTimer) {
        GazetteAppPumpRefresh();
        return TRUE;
    }
    return FALSE;
}

/* Over the window the dialog belongs to, as Windows places a message
   box: DS_CENTER centres on the screen, which on a large desktop puts the
   dialog a long way from what it is about. */
static void CentreOnOwner(HWND dialog)
{
    HWND owner = GetWindow(dialog, GW_OWNER);
    RECT outer, inner, work;
    int  x, y;

    if (owner == NULL || !GetWindowRect(owner, &outer) ||
        !GetWindowRect(dialog, &inner)) {
        return;
    }
    x = outer.left + ((outer.right - outer.left) -
                      (inner.right - inner.left)) / 2;
    y = outer.top + ((outer.bottom - outer.top) -
                     (inner.bottom - inner.top)) / 3;

    /* Kept on the screen: a window dragged half off it would otherwise
       take its dialog along. */
    if (SystemParametersInfoA(SPI_GETWORKAREA, 0, &work, 0)) {
        if (x + (inner.right - inner.left) > work.right) {
            x = work.right - (inner.right - inner.left);
        }
        if (y + (inner.bottom - inner.top) > work.bottom) {
            y = work.bottom - (inner.bottom - inner.top);
        }
        if (x < work.left) {
            x = work.left;
        }
        if (y < work.top) {
            y = work.top;
        }
    }
    SetWindowPos(dialog, NULL, x, y, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

/* Leading and trailing spaces come from pasting far more often than from
   typing, and a URL with either is not the URL the user meant. */
static void Trim(char *s)
{
    size_t len = strlen(s);
    size_t start = 0;

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

static void ReadField(HWND dialog, int item, char *out, size_t cap)
{
    out[0] = '\0';
    GetDlgItemTextA(dialog, item, out, (int)cap);
    Trim(out);
}

/* Start a field with its text, capped at what the caller can take back,
   and put the cursor in it with everything selected. */
static void SetField(HWND dialog, int item, const char *text, size_t cap)
{
    SendDlgItemMessageA(dialog, item, EM_LIMITTEXT, (WPARAM)(cap - 1), 0);
    SetDlgItemTextA(dialog, item, text != NULL ? text : "");
}

static void FocusField(HWND dialog, int item)
{
    HWND field = GetDlgItem(dialog, item);

    SetFocus(field);
    SendMessageA(field, EM_SETSEL, 0, -1);
}

/* ------------------------------------------------------------------ */
/* New Feed / Edit Feed                                                */
/* ------------------------------------------------------------------ */

typedef struct {
    const char *windowTitle;
    char       *url;
    size_t      urlCap;
    char       *title;
    size_t      titleCap;
    int         group;
} FeedAsk;

static INT_PTR CALLBACK FeedDialogProc(HWND dialog, UINT message,
                                       WPARAM wParam, LPARAM lParam)
{
    FeedAsk *ask = (FeedAsk *)GetWindowLongA(dialog, DWL_USER);

    if (IdleTick(message, wParam)) {
        return TRUE;
    }

    switch (message) {
    case WM_INITDIALOG: {
        HWND combo;
        int  groups = GazetteCoreGroupCount();
        int  i;

        ask = (FeedAsk *)lParam;
        SetWindowLongA(dialog, DWL_USER, (LONG)lParam);
        SetWindowTextA(dialog, ask->windowTitle);

        SetField(dialog, IDC_FEED_NAME, ask->title, ask->titleCap);
        SetField(dialog, IDC_FEED_URL, ask->url, ask->urlCap);

        /* The group: the top level, then every group, as the Mac's popup
           lists them. With no groups there is nowhere but the top level,
           and the one choice is shown grey -- a list of one is not a
           choice. */
        combo = GetDlgItem(dialog, IDC_FEED_GROUP);
        SendMessageA(combo, CB_ADDSTRING, 0, (LPARAM)"Top Level");
        for (i = 0; i < groups; i++) {
            SendMessageA(combo, CB_ADDSTRING, 0,
                         (LPARAM)GazetteCoreGroupName(i));
        }
        SendMessageA(combo, CB_SETCURSEL,
                     (WPARAM)((ask->group >= 0 && ask->group < groups)
                                  ? ask->group + 1 : 0), 0);
        EnableWindow(combo, (BOOL)(groups > 0));

        CentreOnOwner(dialog);
        StartIdle(dialog);

        /* The address when there is none yet, the name when editing --
           the field the user is likeliest to have come to change. */
        FocusField(dialog, ask->url[0] == '\0' ? IDC_FEED_URL
                                               : IDC_FEED_NAME);
        return FALSE;               /* the focus is set */
    }

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDOK: {
            int chosen = (int)SendDlgItemMessageA(dialog, IDC_FEED_GROUP,
                                                  CB_GETCURSEL, 0, 0);

            ReadField(dialog, IDC_FEED_URL, ask->url, ask->urlCap);
            ReadField(dialog, IDC_FEED_NAME, ask->title, ask->titleCap);
            ask->group = (chosen > 0) ? chosen - 1 : -1;
            EndDialog(dialog, IDOK);
            return TRUE;
        }
        case IDCANCEL:
            EndDialog(dialog, IDCANCEL);
            return TRUE;
        }
        break;

    case WM_DESTROY:
        KillTimer(dialog, kIdleTimer);
        break;
    }
    return FALSE;
}

BOOL GazetteWinAskFeed(HWND owner, const char *windowTitle,
                       char *url, size_t urlCap,
                       char *title, size_t titleCap, int *group)
{
    FeedAsk ask;

    if (url == NULL || urlCap == 0 || title == NULL || titleCap == 0 ||
        group == NULL) {
        return FALSE;
    }
    ask.windowTitle = windowTitle;
    ask.url         = url;
    ask.urlCap      = urlCap;
    ask.title       = title;
    ask.titleCap    = titleCap;
    ask.group       = *group;

    if (DialogBoxParamA(GetModuleHandleA(NULL),
                        MAKEINTRESOURCEA(IDD_FEED), owner, FeedDialogProc,
                        (LPARAM)&ask) != IDOK) {
        return FALSE;
    }
    *group = ask.group;
    /* An empty address is no feed at all, and OK on one is Cancel, as on
       the Mac. */
    return (BOOL)(url[0] != '\0');
}

/* ------------------------------------------------------------------ */
/* New Group / Edit Group                                              */
/* ------------------------------------------------------------------ */

typedef struct {
    const char *windowTitle;
    const char *prompt;
    char       *name;
    size_t      cap;
} NameAsk;

static INT_PTR CALLBACK NameDialogProc(HWND dialog, UINT message,
                                       WPARAM wParam, LPARAM lParam)
{
    NameAsk *ask = (NameAsk *)GetWindowLongA(dialog, DWL_USER);

    if (IdleTick(message, wParam)) {
        return TRUE;
    }

    switch (message) {
    case WM_INITDIALOG:
        ask = (NameAsk *)lParam;
        SetWindowLongA(dialog, DWL_USER, (LONG)lParam);
        SetWindowTextA(dialog, ask->windowTitle);
        SetDlgItemTextA(dialog, IDC_NAME_PROMPT, ask->prompt);
        SetField(dialog, IDC_NAME_TEXT, ask->name, ask->cap);
        CentreOnOwner(dialog);
        StartIdle(dialog);
        FocusField(dialog, IDC_NAME_TEXT);
        return FALSE;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDOK:
            ReadField(dialog, IDC_NAME_TEXT, ask->name, ask->cap);
            EndDialog(dialog, IDOK);
            return TRUE;
        case IDCANCEL:
            EndDialog(dialog, IDCANCEL);
            return TRUE;
        }
        break;

    case WM_DESTROY:
        KillTimer(dialog, kIdleTimer);
        break;
    }
    return FALSE;
}

BOOL GazetteWinAskName(HWND owner, const char *windowTitle,
                       const char *prompt, char *name, size_t cap)
{
    NameAsk ask;

    if (name == NULL || cap == 0) {
        return FALSE;
    }
    ask.windowTitle = windowTitle;
    ask.prompt      = prompt;
    ask.name        = name;
    ask.cap         = cap;

    if (DialogBoxParamA(GetModuleHandleA(NULL),
                        MAKEINTRESOURCEA(IDD_NAME), owner, NameDialogProc,
                        (LPARAM)&ask) != IDOK) {
        return FALSE;
    }
    return (BOOL)(name[0] != '\0');
}

/* ------------------------------------------------------------------ */
/* Preferences                                                         */
/* ------------------------------------------------------------------ */

typedef struct {
    long minutes;
    long articles;
    long photos;
} PrefsAsk;

/* Six digits is more than either number means: 999999 minutes is nearly
   two years between refreshes. */
enum { kNumberDigits = 6 };

static void SetNumber(HWND dialog, int item, long value)
{
    char text[16];

    wsprintfA(text, "%ld", value);
    SetField(dialog, item, text, kNumberDigits + 1);
}

/* A number as the field holds it: digits, or nothing, which is zero. */
static long ReadNumber(HWND dialog, int item)
{
    BOOL ok = FALSE;
    UINT value = GetDlgItemInt(dialog, item, &ok, FALSE);

    return ok ? (long)value : 0;
}

static INT_PTR CALLBACK PrefsDialogProc(HWND dialog, UINT message,
                                        WPARAM wParam, LPARAM lParam)
{
    PrefsAsk *ask = (PrefsAsk *)GetWindowLongA(dialog, DWL_USER);

    if (IdleTick(message, wParam)) {
        return TRUE;
    }

    switch (message) {
    case WM_INITDIALOG:
        ask = (PrefsAsk *)lParam;
        SetWindowLongA(dialog, DWL_USER, (LONG)lParam);
        SetNumber(dialog, IDC_PREFS_MINUTES, ask->minutes);
        SetNumber(dialog, IDC_PREFS_ARTICLES, ask->articles);
        SetNumber(dialog, IDC_PREFS_PHOTOS, ask->photos);
        CentreOnOwner(dialog);
        StartIdle(dialog);
        FocusField(dialog, IDC_PREFS_MINUTES);
        return FALSE;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDOK:
            ask->minutes  = ReadNumber(dialog, IDC_PREFS_MINUTES);
            ask->articles = ReadNumber(dialog, IDC_PREFS_ARTICLES);
            /* 1 to 3, as the note says; the core clamps the same way. */
            ask->photos   = ReadNumber(dialog, IDC_PREFS_PHOTOS);
            if (ask->photos < 1) {
                ask->photos = 1;
            }
            if (ask->photos > 3) {
                ask->photos = 3;
            }
            EndDialog(dialog, IDOK);
            return TRUE;
        case IDCANCEL:
            EndDialog(dialog, IDCANCEL);
            return TRUE;
        }
        break;

    case WM_DESTROY:
        KillTimer(dialog, kIdleTimer);
        break;
    }
    return FALSE;
}

BOOL GazetteWinAskPreferences(HWND owner, long *refreshMinutes,
                              long *maxArticles, long *maxPhotos)
{
    PrefsAsk ask;

    if (refreshMinutes == NULL || maxArticles == NULL || maxPhotos == NULL) {
        return FALSE;
    }
    ask.minutes  = *refreshMinutes;
    ask.articles = *maxArticles;
    ask.photos   = *maxPhotos;

    if (DialogBoxParamA(GetModuleHandleA(NULL),
                        MAKEINTRESOURCEA(IDD_PREFS), owner, PrefsDialogProc,
                        (LPARAM)&ask) != IDOK) {
        return FALSE;
    }
    *refreshMinutes = ask.minutes;
    *maxArticles    = ask.articles;
    *maxPhotos      = ask.photos;
    return TRUE;
}
