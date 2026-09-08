/*
 * Gazette — Minimal resource file (Carbon)
 * Copyright (c) 2026 brunocastello
 *
 * Carbon creates menus and windows programmatically, so we only need:
 * - SIZE resource (memory preferences, optional on modern systems)
 * - Icon resources for the app bundle (via .icns instead of resource fork)
 */

include "Types.r"

/* ------------------------------------------------------------------ */
/* SIZE resource — memory preferences (optional, Carbon handles this)  */
/* ------------------------------------------------------------------ */

resource 'SIZE' (0) {
    /* Preferred: 8 MB, Minimum: 4 MB */
};

/* ------------------------------------------------------------------ */
/* Application icon — Carbon uses .icns files in the app bundle        */
/* (built from an iconset directory via iconutil)                      */
/* ------------------------------------------------------------------ */

/* Icon resources are no longer needed in the resource fork.       */
/* Use Resources/Gazette.iconset/ with iconutil to build .icns.    */
