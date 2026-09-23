#!/usr/bin/env python3
"""
Cut Gazette's icons for Windows.

Not a second set of drawings: this imports the two generators that draw
the Mac's -- tools/generate_icon.py for the application icon and
tools/generate_ui_icons.py for the sidebar's and the toolbar's -- and
writes the same pixels out in the formats Win32 reads. Every icon is
therefore drawn in exactly one place, and a change to a grid reaches
both builds by regenerating.

    python3 tools/generate_win_assets.py --out Resources/win
                                         [--proof proof.json]

Writes:

    Gazette.ico     the application icon: 32x32 and 16x16, 24-bit with a
                    1-bit AND mask. No 32-bit alpha member -- these are
                    hard-edged pixel drawings with nothing to blend, and
                    a 32-bit member is not read before XP anyway.
    ui_icons.bmp    every 16x16 icon in one strip, in resource-id order,
                    which is the order the GAZETTE_ICON_* constants in
                    src/win/gazette_win.h give. Magenta is the
                    transparent colour, as it is in every Win32 image
                    list that predates alpha.

Both are committed rather than generated during the build: windres reads
them, and a build machine has no Python guarantee.
"""

import argparse
import json
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import generate_icon as app          # noqa: E402
import generate_ui_icons as ui       # noqa: E402

# The colour a Win32 image list treats as nothing. Magenta because no
# icon uses it -- the usual reason, and the reason it looks like that.
MAGENTA = (255, 0, 255)


def ui_pixels(px):
    """A generate_ui_icons grid of letters, as rows of RGB or None."""
    return [[ui.PAL[ch] for ch in row] for row in px]


# --- BMP --------------------------------------------------------------------

def bmp_rows(pixels, transparent=MAGENTA):
    """Bottom-up 24-bit rows, each padded to a four-byte boundary."""
    height = len(pixels)
    width = len(pixels[0])
    stride = (width * 3 + 3) // 4 * 4
    out = bytearray()

    for y in range(height - 1, -1, -1):
        row = bytearray()
        for x in range(width):
            colour = pixels[y][x]
            r, g, b = transparent if colour is None else colour
            row += bytes((b, g, r))
        row += bytes(stride - len(row))
        out += row

    return bytes(out)


def write_bmp(path, pixels):
    width = len(pixels[0])
    height = len(pixels)
    bits = bmp_rows(pixels)

    info = struct.pack("<IiiHHIIiiII", 40, width, height, 1, 24, 0,
                       len(bits), 2835, 2835, 0, 0)
    header = struct.pack("<2sIHHI", b"BM", 14 + len(info) + len(bits),
                         0, 0, 14 + len(info))

    with open(path, "wb") as f:
        f.write(header + info + bits)


# --- ICO --------------------------------------------------------------------

def and_mask(pixels):
    """
    One bit per pixel, set where the icon is transparent, bottom-up and
    padded to four bytes a row.

    This is the half that makes an icon an icon on every Windows there
    is: the colour bits are drawn through it, so a set bit leaves what
    was behind alone.
    """
    height = len(pixels)
    width = len(pixels[0])
    stride = (width + 31) // 32 * 4
    out = bytearray()

    for y in range(height - 1, -1, -1):
        row = bytearray(stride)
        for x in range(width):
            if pixels[y][x] is None:
                row[x // 8] |= 0x80 >> (x % 8)
        out += row

    return bytes(out)


def ico_image(pixels):
    """One image's BITMAPINFOHEADER, colour bits and mask."""
    width = len(pixels[0])
    height = len(pixels)
    colour = bmp_rows(pixels, transparent=(0, 0, 0))
    mask = and_mask(pixels)

    # The height in an icon's header is the colour bits and the mask
    # together, which is why it is twice the icon's own.
    info = struct.pack("<IiiHHIIiiII", 40, width, height * 2, 1, 24, 0,
                       len(colour) + len(mask), 0, 0, 0, 0)
    return info + colour + mask


def write_ico(path, images):
    count = len(images)
    blobs = [ico_image(px) for px in images]

    out = bytearray(struct.pack("<HHH", 0, 1, count))
    offset = 6 + 16 * count

    for px, blob in zip(images, blobs):
        width = len(px[0])
        height = len(px)
        out += struct.pack("<BBBBHHII",
                           0 if width >= 256 else width,
                           0 if height >= 256 else height,
                           0,        # colours in the palette: 0, it is 24-bit
                           0,        # reserved
                           1,        # planes
                           24,       # bits per pixel
                           len(blob), offset)
        offset += len(blob)

    for blob in blobs:
        out += blob

    with open(path, "wb") as f:
        f.write(bytes(out))


# --- The proof sheet --------------------------------------------------------

def proof(icons, app32, app16):
    """
    The grids as JSON, for the artifact that shows Bruno what was cut
    before any of it is committed. Colours as hex, transparent as null.
    """
    def rows(pixels):
        return [["#%02x%02x%02x" % c if c is not None else None for c in row]
                for row in pixels]

    return {
        "application": {"32": rows(app32), "16": rows(app16)},
        "icons": [{"id": res_id, "name": name, "grid": rows(ui_pixels(px))}
                  for res_id, name, px in icons],
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True,
                    help="directory the .ico and .bmp are written to")
    ap.add_argument("--proof", help="write the grids as JSON for review")
    args = ap.parse_args()

    icons = ui.icon_list()
    app32, app16 = app.draw32(), app.draw16()

    # One strip, sixteen pixels tall and as wide as it needs: an image
    # list is loaded from exactly this and cut up by its own height.
    strip = [[None] * (16 * len(icons)) for _ in range(16)]
    for index, (_, _, px) in enumerate(icons):
        pixels = ui_pixels(px)
        for y in range(16):
            for x in range(16):
                strip[y][index * 16 + x] = pixels[y][x]

    os.makedirs(args.out, exist_ok=True)

    strip_path = os.path.join(args.out, "ui_icons.bmp")
    write_bmp(strip_path, strip)
    print("wrote %s (%d icons)" % (strip_path, len(icons)))

    ico_path = os.path.join(args.out, "Gazette.ico")
    write_ico(ico_path, [app32, app16])
    print("wrote %s" % ico_path)

    if args.proof:
        with open(args.proof, "w") as f:
            json.dump(proof(icons, app32, app16), f)
        print("wrote %s" % args.proof)


if __name__ == "__main__":
    main()
