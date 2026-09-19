/*
 * Gazette — application resources (Carbon / PowerPC, Mac OS 9)
 * Copyright (c) 2026 brunocastello
 *
 * Rezzed by add_application() *after* Retro68's RetroCarbonAPPL.r template,
 * so the SIZE resource below overrides the template's default partition.
 *
 * These headers come from Apple's Universal Interfaces, staged into the
 * toolchain's RIncludes by the CI workflow.
 */

#include "Processes.r"      /* 'SIZE'          */
#include "Dialogs.r"        /* 'ALRT', 'DITL'  */
#include "MacTypes.r"       /* 'vers'          */

/* Must match the enum in src/ui/gazette_dialogs.c. */
#define kFeedDialogID    129
#define kNameDialogID    130
#define kConfirmAlertID  131
#define kPrefsDialogID   132

/* ------------------------------------------------------------------ */
/* SIZE — memory partition                                             */
/*                                                                     */
/* 8 MB preferred / 4 MB minimum, per the memory discipline Gateway and */
/* Post use. Streaming feed data keeps the resident set well under this;*/
/* the headroom is for CarbonLib, which is hungrier on OS 9 than the    */
/* classic Toolbox was.                                                 */
/* ------------------------------------------------------------------ */

resource 'SIZE' (-1) {
    reserved,
    acceptSuspendResumeEvents,
    reserved,
    canBackground,                  /* keeps polling network I/O in the background */
    doesActivateOnFGSwitch,
    backgroundAndForeground,
    dontGetFrontClicks,
    ignoreAppDiedEvents,
    is32BitCompatible,
    isHighLevelEventAware,
    localAndRemoteHLEvents,
    isStationeryAware,
    dontUseTextEditServices,
    notDisplayManagerAware,
    reserved,
    reserved,
    8 * 1024 * 1024,                /* preferred size */
    4 * 1024 * 1024                 /* minimum size   */
};

/* ------------------------------------------------------------------ */
/* Feed management dialogs (Phase 4)                                   */
/*                                                                     */
/* Real Dialog Manager dialogs from DLOG/DITL rather than anything      */
/* drawn by hand: the buttons are Platinum's, the default ring and the  */
/* keyboard behaviour come from SetDialogDefaultItem and StdFilterProc, */
/* and the text fields are the Toolbox's own with the Edit menu's       */
/* behaviour already in them.                                          */
/*                                                                     */
/* Each dialog carries a 'dlgx' so the Appearance Manager draws it with */
/* the theme background and theme controls. The control *hierarchy* bit */
/* is deliberately off: it would turn the items into embedded controls  */
/* and GetDialogItemText would no longer be the way to read them.       */
/* ------------------------------------------------------------------ */

resource 'DLOG' (kFeedDialogID, "Feed") {
    { 0, 0, 174, 340 },
    movableDBoxProc,            /* a title bar, to say New from Edit */
    invisible,
    noGoAway,
    0x0,
    kFeedDialogID,
    "",
    centerMainScreen
};

resource 'dlgx' (kFeedDialogID) {
    versionZero {
        kDialogFlagsUseThemeBackground | kDialogFlagsUseThemeControls
    }
};

/*
 * What the dialog says, at the top and at the window's own margin rather
 * than the fields'; then the name, the address and the group. Item 1 is the
 * default button and item 2 the cancel one; the code tells the Dialog
 * Manager so, and nothing else depends on it. The Group popup is a user
 * item: the dialog code makes the control, from the groups there are, and
 * the item's draw procedure draws it whenever the dialog is drawn — the
 * Dialog Manager draws its items and nothing else.
 */
resource 'DITL' (kFeedDialogID, "Feed") {
    {
        { 140, 264, 160, 324 },
        Button { enabled, "OK" };

        { 140, 192, 160, 252 },
        Button { enabled, "Cancel" };

        /* The note: a user item, on which the dialog code stands a static
           text control set in the Appearance Manager's small system font —
           a DITL's own text is set in the dialog font, and a 'dftb' did
           not move it. */
        { 12, 16, 40, 324 },
        UserItem { disabled };

        { 52, 16, 68, 84 },
        StaticText { disabled, "Name:" };

        { 50, 88, 66, 324 },
        EditText { enabled, "" };

        { 80, 16, 96, 84 },
        StaticText { disabled, "Address:" };

        { 78, 88, 94, 324 },
        EditText { enabled, "" };

        { 108, 16, 124, 84 },
        StaticText { disabled, "Group:" };

        { 106, 88, 126, 240 },
        UserItem { disabled };
    }
};

resource 'DLOG' (kNameDialogID, "Name") {
    { 0, 0, 116, 320 },
    movableDBoxProc,            /* a title bar: New Group, Edit Group, Find */
    invisible,
    noGoAway,
    0x0,
    kNameDialogID,
    "",
    centerMainScreen
};

resource 'dlgx' (kNameDialogID) {
    versionZero {
        kDialogFlagsUseThemeBackground | kDialogFlagsUseThemeControls
    }
};

resource 'DITL' (kNameDialogID, "Name") {
    {
        { 82, 244, 102, 304 },
        Button { enabled, "OK" };

        { 82, 172, 102, 232 },
        Button { enabled, "Cancel" };

        /* The prompt is set at runtime — one dialog serves New Group, Edit
           Group and Find — and stays in the dialog font: it is the question
           the dialog asks, not a note under it. */
        { 14, 16, 46, 304 },
        StaticText { disabled, "" };

        { 52, 16, 68, 304 },
        EditText { enabled, "" };
    }
};

/*
 * Preferences. A document window with a close box, the way the About
 * window is, rather than a movable modal: it is a window of the
 * application's, opened from the Edit menu, and it closes the way windows
 * close. The dialog code runs it modally all the same and answers the close
 * box itself.
 */
resource 'DLOG' (kPrefsDialogID, "Preferences") {
    { 0, 0, 160, 400 },
    noGrowDocProc,
    invisible,
    goAway,
    0x0,
    kPrefsDialogID,
    "Preferences",
    centerMainScreen
};

resource 'dlgx' (kPrefsDialogID) {
    versionZero {
        kDialogFlagsUseThemeBackground | kDialogFlagsUseThemeControls
    }
};

/* Two numbers, each with a unit after it and a note under it — the note
   a user item the dialog code stands a small-system-font control on, as
   the feed dialog's is, aligned with the label. Item 1 is OK, item 2
   Cancel; 4 and 8 the fields, 6 and 10 the notes. */
resource 'DITL' (kPrefsDialogID, "Preferences") {
    {
        { 126, 324, 146, 384 },
        Button { enabled, "OK" };

        { 126, 252, 146, 312 },
        Button { enabled, "Cancel" };

        { 18, 16, 34, 152 },
        StaticText { disabled, "Refresh feeds every" };

        { 16, 156, 32, 204 },
        EditText { enabled, "" };

        { 18, 212, 34, 384 },
        StaticText { disabled, "minutes" };

        { 38, 16, 52, 384 },
        UserItem { disabled };

        { 68, 16, 84, 152 },
        StaticText { disabled, "Keep at most" };

        { 66, 156, 82, 204 },
        EditText { enabled, "" };

        { 68, 212, 84, 384 },
        StaticText { disabled, "articles per feed" };

        { 88, 16, 102, 384 },
        UserItem { disabled };
    }
};

/* Removing a feed or a group is not undoable, so it asks first. CautionAlert
   draws the caution icon itself; the text starts clear of it. */
resource 'ALRT' (kConfirmAlertID, "Confirm") {
    { 0, 0, 124, 360 },
    kConfirmAlertID,
    {
        OK, visible, silent;
        OK, visible, silent;
        OK, visible, silent;
        OK, visible, silent
    },
    alertPositionMainScreen
};

resource 'alrx' (kConfirmAlertID) {
    versionOne {
        kDialogFlagsUseThemeBackground | kDialogFlagsUseThemeControls,
        0,
        kUseThemeWindow,
        ""
    }
};

resource 'DITL' (kConfirmAlertID, "Confirm") {
    {
        { 90, 280, 110, 340 },
        Button { enabled, "Remove" };

        { 90, 208, 110, 268 },
        Button { enabled, "Cancel" };

        { 12, 70, 80, 340 },
        StaticText { disabled, "^0" };
    }
};

/* ------------------------------------------------------------------ */
/* vers — shown by the Finder's Get Info window                        */
/* ------------------------------------------------------------------ */

/* The number is src/gazette_version.h's, written out because Rez cannot
   read that header. */
resource 'vers' (1) {
    0x00,                           /* major revision, BCD    */
    0x10,                           /* minor revision, BCD    */
    final,                          /* release stage          */
    0x00,                           /* non-final release #    */
    0,                              /* region code: verUS     */
    "0.1.0",
    "0.1.0, Copyright 2026 brunocastello"
};
