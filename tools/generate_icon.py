#!/usr/bin/env python3
"""
Draw Gazette's Finder icon and emit it as Rez source.

Classic Mac icons are hand-placed pixels, not scaled art: at 32x32 every pixel
carries weight, so the shapes are drawn here in code rather than exported from
a drawing program and resampled.

The design is a newspaper standing at the isometric angle the classic
system icons share — two pixels along for one up — with the stack of its
pages showing as a thickness on the right: a black masthead band across the
top, a rule under it, a lead picture and bars of text. Newsprint white,
black and Platinum greys put it next to the Mac OS 9 system icons rather
than against them; the thickness is what stops it reading as a flat
document at 16x16.

    python3 tools/generate_icon.py --out Resources/Gazette_icon.r [--ascii]

Colours are restricted to the 6x6x6 cube of the Macintosh 256-colour system
palette (components from 0, 51, 102, 153, 204, 255), where the palette index
is simply r*36 + g*6 + b on the descending 0..5 scale. That makes the mapping
exact without needing the whole system colour table; pure black is the one
exception, living at index 255.
"""

import argparse

# --- Macintosh 8-bit system palette, cube subset -----------------------------

LEVELS = [255, 204, 153, 102, 51, 0]


def mac8(rgb):
    """Palette index for a colour drawn from the cube."""
    if rgb == (0, 0, 0):
        return 255                      # black is not at cube index 215
    try:
        r, g, b = (LEVELS.index(c) for c in rgb)
    except ValueError:
        raise SystemExit("colour %r is not on the 51-step cube" % (rgb,))
    return r * 36 + g * 6 + b


# --- Palette ----------------------------------------------------------------

CLEAR = None
BLACK = (0, 0, 0)
PAPER = (255, 255, 255)             # the front sheet
BACK = (204, 204, 204)              # the sheet showing behind it
BACK_D = (153, 153, 153)            # its shaded edge

# Anything lighter than mid-grey drops out of the 1-bit ICN#/ics#, so the text
# columns and the rule are deliberately dark enough to survive there: in black
# and white they are the only thing that still says "newspaper".
TEXT = (102, 102, 102)              # columns of body text
RULE = (102, 102, 102)              # the rule under the masthead
PHOTO = (102, 102, 102)             # the picture block
PHOTO_L = (153, 153, 153)


def fill(px, x0, y0, x1, y1, colour):
    """Inclusive rectangle fill, clipped to the canvas."""
    size = len(px)
    for y in range(max(0, y0), min(size - 1, y1) + 1):
        for x in range(max(0, x0), min(size - 1, x1) + 1):
            px[y][x] = colour


def frame(px, x0, y0, x1, y1, colour):
    """Inclusive one-pixel border."""
    for x in range(x0, x1 + 1):
        px[y0][x] = colour
        px[y1][x] = colour
    for y in range(y0, y1 + 1):
        px[y][x0] = colour
        px[y][x1] = colour


def hline(px, x0, x1, y, colour):
    for x in range(x0, x1 + 1):
        px[y][x] = colour


# --- Isometric helpers -----------------------------------------------------
#
# The paper stands at the classic pixel-art isometric angle: two pixels along
# for one up, which is the one slope that draws clean at 32 pixels. A face is
# described by its left edge (x = left, running down to `bottom`), its width
# and its height; a point on it is (u, v), u along the slope from the left
# edge and v up from the bottom edge, and lands on screen at
#     x = left + u,  y = bottom - u // 2 - v.

def iso_y(left, bottom, u, v):
    return bottom - u // 2 - v


def iso_point(px, left, bottom, u, v, colour):
    y = iso_y(left, bottom, u, v)
    x = left + u
    if 0 <= x < len(px) and 0 <= y < len(px):
        px[y][x] = colour


def iso_hline(px, left, bottom, u0, u1, v, colour):
    """A line along the slope, at height v."""
    for u in range(u0, u1 + 1):
        iso_point(px, left, bottom, u, v, colour)


def iso_fill(px, left, bottom, u0, u1, v0, v1, colour):
    """A parallelogram on the face: u0..u1 along, v0..v1 up, inclusive."""
    for u in range(u0, u1 + 1):
        for v in range(v0, v1 + 1):
            iso_point(px, left, bottom, u, v, colour)


def iso_frame(px, left, bottom, u0, u1, v0, v1, colour):
    iso_hline(px, left, bottom, u0, u1, v0, colour)
    iso_hline(px, left, bottom, u0, u1, v1, colour)
    for v in range(v0, v1 + 1):
        iso_point(px, left, bottom, u0, v, colour)
        iso_point(px, left, bottom, u1, v, colour)


def put(px, x, y, colour):
    if 0 <= x < len(px) and 0 <= y < len(px):
        px[y][x] = colour


def side_face(px, left, bottom, width, height, depth, colour, edge, dark):
    """The page edges to the right of the front face: `depth` columns going
    down the other slope, the stack of sheets drawn as lines in it."""
    right = left + width - 1
    top_y = iso_y(left, bottom, width - 1, height - 1)
    bot_y = iso_y(left, bottom, width - 1, 0)
    for d in range(1, depth + 1):
        x = right + d
        drop = (d + 1) // 2
        for y in range(top_y + drop, bot_y + drop + 1):
            put(px, x, y, colour)
        # the outline of the slab, top and bottom
        put(px, x, top_y + drop, edge)
        put(px, x, bot_y + drop, edge)
    # the far edge, and a sheet showing in the stack
    x = right + depth
    for y in range(top_y + (depth + 1) // 2, bot_y + (depth + 1) // 2 + 1):
        put(px, x, y, edge)
    x = right + 2
    for y in range(top_y + 2, bot_y + 1):
        put(px, x, y, dark)


def draw32():
    px = [[CLEAR] * 32 for _ in range(32)]

    # The sheet: left edge at x=3 from y=10 down to y=30, twenty columns
    # along the slope, twenty-one rows tall, with four columns of page edge
    # to the right of it. The outermost row and column of the face are its
    # outline.
    L, B, W, H, D = 3, 30, 20, 21, 4

    side_face(px, L, B, W, H, D, BACK, BLACK, BACK_D)
    iso_fill(px, L, B, 0, W - 1, 0, H - 1, PAPER)
    iso_frame(px, L, B, 0, W - 1, 0, H - 1, BLACK)

    # Masthead: a solid band across the top, ruled off from the page. The
    # letters are too small to draw; the band is what a nameplate reads as.
    # Everything on the page is two pixels thick along the slope: one pixel
    # thick, two parallel lines interleave into a checkerboard.
    iso_fill(px, L, B, 2, W - 3, 16, 18, BLACK)
    iso_fill(px, L, B, 2, W - 3, 13, 13, RULE)

    # Lead picture, left column, framed, with a little light in it.
    iso_fill(px, L, B, 2, 8, 3, 10, PHOTO)
    iso_frame(px, L, B, 2, 8, 3, 10, BLACK)
    iso_fill(px, L, B, 3, 5, 8, 9, PHOTO_L)

    # Text beside it: two bars, and one running the width beneath both.
    iso_fill(px, L, B, 10, W - 3, 9, 10, TEXT)
    iso_fill(px, L, B, 10, W - 3, 5, 6, TEXT)
    iso_fill(px, L, B, 2, 13, 1, 1, TEXT)

    return px


def draw16():
    px = [[CLEAR] * 16 for _ in range(16)]

    # Everything halves, and anything that will not survive halving goes: the
    # small icon is the slab, the masthead and two bars of text.
    L, B, W, H, D = 1, 15, 11, 11, 2

    side_face(px, L, B, W, H, D, BACK, BLACK, BACK_D)
    iso_fill(px, L, B, 0, W - 1, 0, H - 1, PAPER)
    iso_frame(px, L, B, 0, W - 1, 0, H - 1, BLACK)

    iso_fill(px, L, B, 1, W - 2, 7, 8, BLACK)
    iso_fill(px, L, B, 1, W - 2, 4, 4, TEXT)
    iso_fill(px, L, B, 1, W - 2, 2, 2, TEXT)

    return px


# --- Rez emission -----------------------------------------------------------

def hexrows(data, per=16, indent="\t"):
    rows = []
    for i in range(0, len(data), per):
        rows.append(indent + '$"' +
                    " ".join("%02X" % b for b in data[i:i + per]) + '"')
    return "\n".join(rows)


def bitmap(px, size, predicate):
    """Pack a 1-bit-per-pixel bitmap, MSB first."""
    out = bytearray()
    for y in range(size):
        for byte in range(size // 8):
            v = 0
            for bit in range(8):
                x = byte * 8 + bit
                if predicate(px[y][x]):
                    v |= 0x80 >> bit
            out.append(v)
    return bytes(out)


def luminance(c):
    return 0.299 * c[0] + 0.587 * c[1] + 0.114 * c[2]


def emit(px32, px16):
    # 1-bit icon: ink where the pixel is dark. Mask: every non-clear pixel.
    icn = bitmap(px32, 32, lambda c: c is not CLEAR and luminance(c) < 150)
    icn_mask = bitmap(px32, 32, lambda c: c is not CLEAR)
    ics = bitmap(px16, 16, lambda c: c is not CLEAR and luminance(c) < 150)
    ics_mask = bitmap(px16, 16, lambda c: c is not CLEAR)

    icl8 = bytes(mac8(c) if c is not CLEAR else 0
                 for row in px32 for c in row)
    ics8 = bytes(mac8(c) if c is not CLEAR else 0
                 for row in px16 for c in row)

    parts = []
    parts.append("""/*
 * Gazette_icon.r - Finder icon for Gazette.
 *
 * DO NOT EDIT. Regenerate with:
 *   python3 tools/generate_icon.py --out Resources/Gazette_icon.r
 *
 * A newspaper standing at the isometric angle, its pages showing as a
 * thickness on the right: a masthead band, a lead picture and bars of text.
 * Written as raw data blocks rather than
 * Rez icon templates because Rez runs against whichever RIncludes the
 * toolchain has linked, and a resource file with no includes does not care
 * which -- the same reasoning Gateway's icon file records.
 *
 * BNDL and FREF tie the icon family to the 'Gzt9' creator so the Finder uses
 * it. If a freshly built copy shows a generic application icon, the desktop
 * database has not caught up: rebuild it by holding Command-Option through
 * startup, or move the application to another folder and back.
 */
""")
    parts.append('data \'ICN#\' (128, "Gazette", purgeable) {\n%s\n};\n'
                 % hexrows(icn + icn_mask))
    parts.append('data \'icl8\' (128, "Gazette", purgeable) {\n%s\n};\n'
                 % hexrows(icl8))
    parts.append('data \'ics#\' (128, "Gazette", purgeable) {\n%s\n};\n'
                 % hexrows(ics + ics_mask))
    parts.append('data \'ics8\' (128, "Gazette", purgeable) {\n%s\n};\n'
                 % hexrows(ics8))

    # FREF: application, icon local ID 0, no name.
    parts.append('data \'FREF\' (128, "Gazette", purgeable) {\n'
                 '\t$"4150 504C"        /* type APPL          */\n'
                 '\t$"0000"             /* local icon list ID */\n'
                 '\t$"00"               /* empty name         */\n'
                 '};\n')

    # BNDL: creator Gzt9, mapping local ID 0 to resource 128 for both lists.
    # Both counts are stored one less than the number of entries.
    parts.append('data \'BNDL\' (128, "Gazette", purgeable) {\n'
                 '\t$"477A 7439"        /* signature Gzt9     */\n'
                 '\t$"0000"             /* version            */\n'
                 '\t$"0001"             /* two type entries   */\n'
                 '\t$"4943 4E23"        /* ICN#               */\n'
                 '\t$"0000"             /* one mapping        */\n'
                 '\t$"0000 0080"        /* local 0 -> 128     */\n'
                 '\t$"4652 4546"        /* FREF               */\n'
                 '\t$"0000"             /* one mapping        */\n'
                 '\t$"0000 0080"        /* local 0 -> 128     */\n'
                 '};\n')

    # The signature resource the bundle points back at.
    parts.append('data \'Gzt9\' (0, "Gazette", purgeable) {\n'
                 '\t$"0B" "Gazette 0.1"\n'
                 '};\n')

    return "\n".join(parts)


def ascii_art(px):
    ramp = " .:-=+*#%@"
    for row in px:
        line = ""
        for c in row:
            line += " " if c is CLEAR else ramp[min(9, int((255 - luminance(c)) / 26))]
        print(line)


def preview(px32, px16, path):
    from PIL import Image
    img = Image.new("RGBA", (32 + 4 + 16, 32), (0, 0, 0, 0))
    for y in range(32):
        for x in range(32):
            c = px32[y][x]
            img.putpixel((x, y), (0, 0, 0, 0) if c is CLEAR else c + (255,))
    for y in range(16):
        for x in range(16):
            c = px16[y][x]
            img.putpixel((36 + x, y), (0, 0, 0, 0) if c is CLEAR else c + (255,))
    img.resize((img.width * 8, img.height * 8), Image.NEAREST).save(path)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--preview")
    ap.add_argument("--ascii", action="store_true")
    args = ap.parse_args()

    px32, px16 = draw32(), draw16()
    open(args.out, "w").write(emit(px32, px16))
    print("wrote", args.out)
    if args.ascii:
        ascii_art(px32)
        print()
        ascii_art(px16)
    if args.preview:
        preview(px32, px16, args.preview)
        print("preview", args.preview)


if __name__ == "__main__":
    main()
