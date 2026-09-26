/*
 * gazette_win_image.c - a photograph's bytes into pixels Windows can paint.
 *
 * The Mac hands its JPEGs to QuickTime's Graphics Importers. Windows 95 has
 * nothing that reads a JPEG -- OleLoadPicture only learnt to with IE3, and
 * a clean 95 has no IE -- so the decoding is stb_image's (third_party/stb,
 * public domain), roytam1's suggestion (2026-09-26): one file, plain C, no
 * system calls, and small once trimmed to the three formats the engine lets
 * through (gazette_photos.c's LooksLikeAPicture: JPEG, PNG, GIF).
 *
 * A picture is decoded once, shrunk to the most the reader will ever draw
 * it at, and kept as a 24-bit DIB the pane paints with StretchDIBits at
 * every redraw -- the Mac's reason for its GWorld: decoding a large JPEG on
 * a 486 is a second's work, and copying a 500-pixel DIB is nothing.
 */

#define WINVER       0x0400
#define _WIN32_WINNT 0x0400

#include <windows.h>
#include <stdlib.h>
#include <string.h>

#include "gazette_win.h"

/*
 * stb_image, trimmed. The formats are the engine's three. No stdio, no HDR,
 * no gamma: the bytes come from memory and are photographs. No SIMD: the
 * floor is a 486 or a Pentium, neither of which has SSE2. No thread-local
 * failure reason: MinGW would make it an emulated TLS through libgcc, and
 * the one thread here would never read it. No asserts in a release build.
 */
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_GIF
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_NO_SIMD
#define STBI_NO_THREAD_LOCALS
#define STBI_NO_FAILURE_STRINGS
#define STBI_ASSERT(x) ((void)0)
/* A 120 KB file (kGazettePhotoEach) that claims to be larger than this is
   a decompression bomb, not a news photograph. */
#define STBI_MAX_DIMENSIONS 8192
#include "stb/stb_image.h"

/*
 * Shrink by averaging: each pixel of the result is the mean of the block of
 * source pixels it covers, which is the whole of what a photograph needs
 * going down in size -- no ringing, no moire from skipped rows. Alpha is
 * laid over white, the pane's colour, so a PNG with a transparent ground
 * does not come out on black. Rows go in bottom-up and padded to four
 * bytes, the DIB layout every Windows paints.
 */
static void ShrinkInto(const unsigned char *src, int sw, int sh,
                       unsigned char *dst, int dw, int dh, int stride)
{
    int dy, dx;

    for (dy = 0; dy < dh; dy++) {
        int            y0  = (int)((long)dy * sh / dh);
        int            y1  = (int)((long)(dy + 1) * sh / dh);
        unsigned char *out = dst + (long)(dh - 1 - dy) * stride;

        if (y1 <= y0) {
            y1 = y0 + 1;
        }
        for (dx = 0; dx < dw; dx++) {
            int           x0 = (int)((long)dx * sw / dw);
            int           x1 = (int)((long)(dx + 1) * sw / dw);
            unsigned long r = 0, g = 0, b = 0, n = 0;
            int           x, y;

            if (x1 <= x0) {
                x1 = x0 + 1;
            }
            for (y = y0; y < y1; y++) {
                const unsigned char *p = src + ((long)y * sw + x0) * 4;

                for (x = x0; x < x1; x++, p += 4) {
                    unsigned a = p[3];

                    /* Over white: c*a + 255*(1-a). */
                    r += (p[0] * a + 255u * (255u - a)) / 255u;
                    g += (p[1] * a + 255u * (255u - a)) / 255u;
                    b += (p[2] * a + 255u * (255u - a)) / 255u;
                    n++;
                }
            }
            out[dx * 3 + 0] = (unsigned char)(b / n);
            out[dx * 3 + 1] = (unsigned char)(g / n);
            out[dx * 3 + 2] = (unsigned char)(r / n);
        }
    }
}

BOOL GazetteWinDecodePicture(const char *bytes, long len, int maxWidth,
                             int maxHeight, GazetteWinPicture *out)
{
    unsigned char *rgba;
    int            w = 0, h = 0, comp = 0;
    long           dw, dh;
    int            stride;

    if (out == NULL) {
        return FALSE;
    }
    ZeroMemory(out, sizeof(*out));
    if (bytes == NULL || len < 4 || maxWidth < 1 || maxHeight < 1) {
        return FALSE;
    }

    rgba = stbi_load_from_memory((const stbi_uc *)bytes, (int)len,
                                 &w, &h, &comp, 4);
    if (rgba == NULL) {
        return FALSE;
    }

    /* Down to fit, never up: a small picture stays small. */
    dw = w;
    dh = h;
    if (dw > maxWidth) {
        dh = dh * maxWidth / dw;
        dw = maxWidth;
    }
    if (dh > maxHeight) {
        dw = dw * maxHeight / dh;
        dh = maxHeight;
    }
    if (dw < 1) dw = 1;
    if (dh < 1) dh = 1;

    stride    = (int)((dw * 3 + 3) & ~3L);
    out->bits = (unsigned char *)malloc((size_t)stride * (size_t)dh);
    if (out->bits == NULL) {
        stbi_image_free(rgba);
        return FALSE;
    }
    ShrinkInto(rgba, w, h, out->bits, (int)dw, (int)dh, stride);
    stbi_image_free(rgba);

    out->width  = (int)dw;
    out->height = (int)dh;
    out->info.biSize        = sizeof(out->info);
    out->info.biWidth       = (LONG)dw;
    out->info.biHeight      = (LONG)dh;     /* positive: bottom-up */
    out->info.biPlanes      = 1;
    out->info.biBitCount    = 24;
    out->info.biCompression = BI_RGB;
    out->info.biSizeImage   = (DWORD)(stride * dh);
    return TRUE;
}

void GazetteWinFreePicture(GazetteWinPicture *picture)
{
    if (picture != NULL) {
        free(picture->bits);
        ZeroMemory(picture, sizeof(*picture));
    }
}

/*
 * Paint it into a rectangle: at its own size, or smaller when the column
 * has been narrowed since it was decoded. HALFTONE is the smooth shrink,
 * and NT's alone -- 95 refuses it and COLORONCOLOR, which drops rows, is
 * what it gets instead.
 */
void GazetteWinDrawPicture(HDC dc, const GazetteWinPicture *picture,
                           int x, int y, int width, int height)
{
    int oldMode;

    if (picture == NULL || picture->bits == NULL) {
        return;
    }
    if (width == picture->width && height == picture->height) {
        oldMode = GetStretchBltMode(dc);
        SetStretchBltMode(dc, COLORONCOLOR);
    } else {
        oldMode = SetStretchBltMode(dc, HALFTONE);
        if (oldMode == 0) {
            oldMode = SetStretchBltMode(dc, COLORONCOLOR);
        } else {
            SetBrushOrgEx(dc, 0, 0, NULL);  /* HALFTONE's requirement */
        }
    }
    StretchDIBits(dc, x, y, width, height,
                  0, 0, picture->width, picture->height,
                  picture->bits, (const BITMAPINFO *)&picture->info,
                  DIB_RGB_COLORS, SRCCOPY);
    if (oldMode != 0) {
        SetStretchBltMode(dc, oldMode);
    }
}
