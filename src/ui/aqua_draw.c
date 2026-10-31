/*
 * Gazette -- Aqua drawing helpers (Mac OS X, osx-legacy build only)
 * Copyright (c) 2026 brunocastello
 *
 * See aqua_draw.h. Flat headers, nothing newer than Mac OS X 10.4.
 */
#include "ui/aqua_draw.h"

#include <Appearance.h>

static void ToRGBColor(const AquaRGB *c, RGBColor *out)
{
    out->red   = (unsigned short)(c->r * 257);
    out->green = (unsigned short)(c->g * 257);
    out->blue  = (unsigned short)(c->b * 257);
}

void AquaForeRGB(const AquaRGB *colour)
{
    RGBColor c;

    ToRGBColor(colour, &c);
    RGBForeColor(&c);
}

void AquaBackRGB(const AquaRGB *colour)
{
    RGBColor c;

    ToRGBColor(colour, &c);
    RGBBackColor(&c);
}

void AquaPaintRGB(const Rect *r, const AquaRGB *colour)
{
    RGBColor save;

    if (r->right <= r->left || r->bottom <= r->top) {
        return;
    }
    GetForeColor(&save);
    AquaForeRGB(colour);
    PaintRect(r);
    RGBForeColor(&save);
}

void AquaFillGradient(const Rect *r, const AquaRGB *top,
                      const AquaRGB *bottom, const AquaRGB *topLine)
{
    RGBColor save;
    short    rows = (short)(r->bottom - r->top);
    short    y;

    if (r->right <= r->left || rows <= 0) {
        return;
    }
    GetForeColor(&save);
    for (y = 0; y < rows; y++) {
        AquaRGB c;
        Rect    line;
        long    t = (rows > 1) ? ((long)y * 256) / (rows - 1) : 0;

        if (y == 0 && topLine != NULL) {
            c = *topLine;
        } else {
            c.r = (unsigned char)(top->r + ((bottom->r - top->r) * t) / 256);
            c.g = (unsigned char)(top->g + ((bottom->g - top->g) * t) / 256);
            c.b = (unsigned char)(top->b + ((bottom->b - top->b) * t) / 256);
        }
        AquaForeRGB(&c);
        SetRect(&line, r->left, (short)(r->top + y), r->right,
                (short)(r->top + y + 1));
        PaintRect(&line);
    }
    RGBForeColor(&save);
}

void AquaPaintPill(const Rect *r, const AquaRGB *colour)
{
    RGBColor save;
    short    h = (short)(r->bottom - r->top);

    if (r->right <= r->left || h <= 0) {
        return;
    }
    GetForeColor(&save);
    AquaForeRGB(colour);
    PaintRoundRect(r, h, h);
    RGBForeColor(&save);
}

void AquaDrawListHeader(const Rect *r, Boolean active)
{
    ThemeButtonDrawInfo info;

    if (r->right <= r->left || r->bottom <= r->top) {
        return;
    }
    info.state     = active ? kThemeStateActive : kThemeStateInactive;
    info.value     = kThemeButtonOff;
    info.adornment = kThemeAdornmentNone;
    (void)DrawThemeButton(r, kThemeListHeaderButton, &info, NULL, NULL,
                          NULL, 0);
}

/* Erases nothing: the triangle goes on top of the row's own background,
   which may be a gradient and not any colour the port holds. */
static pascal void NoErase(const Rect *bounds, UInt32 eraseData,
                           SInt16 depth, Boolean isColorDev)
{
    (void)bounds;
    (void)eraseData;
    (void)depth;
    (void)isColorDev;
}

void AquaDrawDisclosure(const Rect *r, Boolean open, Boolean active)
{
    static ThemeEraseUPP noErase = NULL;
    ThemeButtonDrawInfo  info;

    if (noErase == NULL) {
        noErase = NewThemeEraseUPP(NoErase);
    }
    info.state     = active ? kThemeStateActive : kThemeStateInactive;
    info.value     = open ? kThemeDisclosureDown : kThemeDisclosureRight;
    info.adornment = kThemeAdornmentNone;
    (void)DrawThemeButton(r, kThemeDisclosureButton, &info, NULL, noErase,
                          NULL, 0);
}
