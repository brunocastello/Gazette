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
    dBoxProc,                   /* BISECT: was movableDBoxProc */
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

        /* The Group popup is not an item: the dialog code makes it, at
           { 106, 88, 126, 240 }, from the groups there are. */
    }
};

/*
 * The description is set a size down from the fields, in the application
 * font: it is a note, not a label. One entry per item, and only the third
 * says anything: flags $0005 (font and size), font 3 (Geneva), size 10,
 * then style, mode, justification, two colours and an empty font name.
 *
 * Raw bytes rather than the Dialogs.r template, for the reason the icons
 * are: Retro68's Rez falls over on the template's switch-inside-an-array,
 * and a data block does not care which Rez reads it. The layout is the
 * template's, version 0, item by item, nothing aligned.
 */
/* version 0, eight items; 1 and 2 as they are; 3 Geneva 10 (flags $0005,
   font 3, size 10, the rest zero, an empty name); 4 to 8 as they are. */
data 'dftb' (kFeedDialogID, "Feed") {
    $"0000 0008 0000 0000 0001 0005 0003 000A 0000 0000 0000 0000 0000 0000"
    $"0000 0000 0000 0000 0000 0000 0000 0000 00"
};

/* The number is src/gazette_version.h's, written out because Rez cannot
   read that header. */
resource 'vers' (1) {
    0x00,                           /* major revision, BCD    */
    0x10,                           /* minor revision, BCD    */
    development,                    /* BISECT: was final */
    0x00,                           /* non-final release #    */
    0,                              /* region code: verUS     */
    "0.1.0",
    "0.1.0, Copyright 2026 brunocastello"
};
