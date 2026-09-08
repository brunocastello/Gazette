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

/* Must match kAboutAlertID in src/main.cpp. */
#define kAboutAlertID 128

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
/* About box — plain Platinum alert (Phase 0)                          */
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
    }
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
/* vers — shown by the Finder's Get Info window                        */
/* ------------------------------------------------------------------ */

resource 'vers' (1) {
    0x00,                           /* major revision, BCD    */
    0x10,                           /* minor revision, BCD    */
    development,                    /* release stage          */
    0x00,                           /* non-final release #    */
    0,                              /* region code: verUS     */
    "0.1",
    "0.1, Copyright 2026 brunocastello"
};
