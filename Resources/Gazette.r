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
#include "Dialogs.r"        /* 'ALRT', 'DITL', 'dftb' */
#include "Controls.r"       /* 'CNTL'          */
#include "MacTypes.r"       /* 'vers'          */

/* Must match the enum in src/ui/gazette_dialogs.c. */
#define kFeedDialogID    129
#define kNameDialogID    130
#define kConfirmAlertID  131
#define kFeedGroupCNTL   129    /* the feed dialog's Group popup */

/* The popup takes no menu resource: -12345 tells the CDEF so, and the
   dialog code builds the menu from the groups there are and hands it in. */
#define kPopupNoMenu     -12345
#define kPopupFixedWidth 401    /* kControlPopupButtonProc + fixed width */

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

resource 'ALRT' (kAboutAlertID, "About Gazette") {
    { 80, 90, 244, 450 },
    kAboutAlertID,
    {   /* four stages, all silent: an About box should never beep */
        /* [1] */
        OK, visible, silent;
        /* [2] */
        OK, visible, silent;
        /* [3] */
        OK, visible, silent;
        /* [4] */
        OK, visible, silent
    },
    /* Apple's Dialogs.r defaults ALRT_RezTemplateVersion to 1, so the
       System 7 positioning field is part of the template and must be
       supplied -- omitting it is "not enough values specified". */
    alertPositionMainScreen
};

resource 'DITL' (kAboutAlertID, "About Gazette") {
    {
        /* Item 1 must be the default button for Alert(). */
        { 132, 280, 152, 340 },
        Button { enabled, "OK" };

        { 14, 20, 34, 340 },
        StaticText { disabled, "Gazette 0.1" };

        { 40, 20, 60, 340 },
        StaticText { disabled, "RSS / Atom reader for Mac OS 9" };

        { 62, 20, 82, 340 },
        StaticText { disabled, "Carbon / PowerPC" };

        { 96, 20, 116, 340 },
        StaticText { disabled, "Copyright 2026 brunocastello" };
    }
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
    "",                         /* set when the dialog is opened */
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
 * Manager so, and nothing else depends on it.
 */
resource 'DITL' (kFeedDialogID, "Feed") {
    {
        { 140, 264, 160, 324 },
        Button { enabled, "OK" };

        { 140, 192, 160, 252 },
        Button { enabled, "Cancel" };

        { 12, 16, 40, 324 },
        StaticText { disabled,
                     "The address of an RSS or Atom feed. Leave the name "
                     "empty to use the feed's own." };

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
        Control { enabled, kFeedGroupCNTL };
    }
};

/* The description is set a size down from the fields, in the application
   font: it is a note, not a label. One entry per item, and only the
   third says anything. */
resource 'dftb' (kFeedDialogID) {
    versionZero {
        {
            skipItem {},
            skipItem {},
            dataItem { kDialogFontUseFontMask | kDialogFontUseSizeMask,
                       3 /* Geneva */, 10, 0, 0, 0,
                       0, 0, 0, 0, 0, 0, "" },
            skipItem {},
            skipItem {},
            skipItem {},
            skipItem {},
            skipItem {},
            skipItem {}
        }
    }
};

/* The Group popup: the width of its rectangle, and no title of its own —
   the label beside it is a static text item, aligned with the others. */
resource 'CNTL' (kFeedGroupCNTL, "Group") {
    { 106, 88, 126, 240 },
    kPopupNoMenu,               /* value: the menu ID, and there is none */
    visible,
    -1,                         /* max: work the title's width out; it is empty */
    0,                          /* min */
    kPopupFixedWidth,
    0,
    ""
};

resource 'DLOG' (kNameDialogID, "Name") {
    { 0, 0, 116, 320 },
    dBoxProc,
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

        /* The prompt is set at runtime — one dialog serves "New Group",
           "Rename Group" and "Rename Feed". */
        { 14, 16, 46, 304 },
        StaticText { disabled, "" };

        { 52, 16, 68, 304 },
        EditText { enabled, "" };
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
