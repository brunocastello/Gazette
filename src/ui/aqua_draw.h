/*
 * Gazette -- Aqua drawing helpers (Mac OS X, osx-legacy build only)
 * Copyright (c) 2026 brunocastello
 *
 * The small pieces of drawing the panes borrow from Aqua that the Mac OS 9
 * build has no use for: flat colour fills, the source list's gradient
 * selection, the unread-count pill, the system's own list header and
 * disclosure triangle. platinum_window.c calls them from its
 * #ifdef GAZETTE_MACOSX sites; nothing here is compiled for Mac OS 9 (this
 * file is in tools/build_osx_legacy.sh's source list, not CMakeLists.txt).
 *
 * Every function leaves the port's colours as it found them.
 */
#ifndef GAZETTE_AQUA_DRAW_H
#define GAZETTE_AQUA_DRAW_H

#include <MacTypes.h>
#include <Quickdraw.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    unsigned char r, g, b;
} AquaRGB;

/* A flat fill in an 8-bit-per-channel colour. */
void AquaPaintRGB(const Rect *r, const AquaRGB *colour);

/* Set the foreground / background colour from the same kind of value. */
void AquaForeRGB(const AquaRGB *colour);
void AquaBackRGB(const AquaRGB *colour);

/*
 * A vertical gradient, top to bottom, one scan line at a time. When topLine
 * is not NULL the first row is that colour instead (the 1-px highlight along
 * the top of a source list's selection).
 */
void AquaFillGradient(const Rect *r, const AquaRGB *top,
                      const AquaRGB *bottom, const AquaRGB *topLine);

/* A pill: a rounded rectangle whose ends are half-circles. */
void AquaPaintPill(const Rect *r, const AquaRGB *colour);

/* The column header Aqua puts over a table (Mail's), active or dimmed. */
void AquaDrawListHeader(const Rect *r, Boolean active);

/* The theme's disclosure triangle, drawn over whatever is behind it
   instead of on a square of the current background. */
void AquaDrawDisclosure(const Rect *r, Boolean open, Boolean active);

#ifdef __cplusplus
}
#endif

#endif
