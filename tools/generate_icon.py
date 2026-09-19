#!/usr/bin/env python3
"""
Draw Gazette's Finder icon and emit it as Rez source.

Classic Mac icons are hand-placed pixels, not scaled art: at 32x32 every pixel
carries weight, so the shapes are drawn here in code rather than exported from
a drawing program and resampled.

The design is a folded newspaper seen face on, with a second sheet showing
behind it, drawn by the same rules as the sixteen-pixel newspaper on the
toolbar's buttons (tools/generate_ui_icons.py) so the two are one object:
Outlook Express's outline, grey on the light side and black on the dark, a
shaded fold, a headline, a picture in the toolbar's blue and lines of text.
The two sheets are what stop it reading as a generic document at 16x16.

    python3 tools/generate_icon.py --out Resources/Gazette_icon.r [--ascii]

Colours are restricted to the 6x6x6 cube of the Macintosh 256-colour system
palette (components from 0, 51, 102, 153, 204, 255), where the palette index
is simply r*36 + g*6 + b on the descending 0..5 scale. That makes the mapping
exact without needing the whole system colour table; pure black is the one
exception, living at index 255.
"""

import argparse

# --- Macintosh 8-bit system palette --------------------------------------

LEVELS = [255, 204, 153, 102, 51, 0]

# The forty-one entries after the cube: four ramps of the values the cube
# skips, then black. The cube's own black slot (215) is not black at all,
# which is why black is looked up last and lands at 255.
RAMP = [0xEE, 0xDD, 0xBB, 0xAA, 0x88, 0x77, 0x55, 0x44, 0x22, 0x11]


def mac8(rgb):
    """Palette index for a colour on the system palette."""
    if rgb == (0, 0, 0):
        return 255
    r, g, b = rgb
    if r in LEVELS and g in LEVELS and b in LEVELS:
        return LEVELS.index(r) * 36 + LEVELS.index(g) * 6 + LEVELS.index(b)
    if r == g == b and r in RAMP:
        return 245 + RAMP.index(r)
    raise SystemExit("colour %r is not on the system palette" % (rgb,))


# --- Palette ----------------------------------------------------------------
#
# The toolbar's colours, by the names generate_ui_icons.py gives them, so the
# Finder icon and the sixteen-pixel newspaper on every toolbar button are the
# same object drawn by the same rules: a one-pixel outline, grey on the top
# and left of a white thing and black on its bottom and right; paper shaded
# a shade inside its dark edges; the headline in the dark grey, the text in
# the light one, the picture in the blue.

CLEAR = None
DARK = (0, 0, 0)                    # 'D': the outline on the dark side
LIGHT_EDGE = (0x77, 0x77, 0x77)     # 'A': the outline on the light side
PAPER = (255, 255, 255)             # 'B'
PAPER_SHADE = (0xEE, 0xEE, 0xEE)    # 'C': inside the light edge
SHADE = (204, 204, 204)             # 'F': inside the dark edges, the fold
TEXT = (153, 153, 153)              # 'I'
HEADLINE = (102, 102, 102)          # 'J'
PICTURE = (102, 102, 204)           # 'N'
PICTURE_L = (153, 153, 255)         # 'M': the light on it
BACK = (204, 204, 204)              # the sheet showing behind

BLACK = DARK


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


# --- The drawing ------------------------------------------------------------
#
# The 32x32 is Bruno's own, pixel for pixel, written as a grid of letters
# the way the toolbar's icons are; the 16x16 is drawn from it by the same
# rules. One letter per colour:

GRID_PAL = {
    '.': CLEAR,
    'a': LIGHT_EDGE,        # the outline on the light side
    'b': BACK,              # the sheet behind
    'c': DARK,              # the outline on the dark side
    'd': PAPER,
    'e': HEADLINE,
    'f': PICTURE,
    'g': TEXT,
    'h': PICTURE_L,
}


def grid(text, size):
    rows = text.strip("\n").split("\n")
    if len(rows) != size or any(len(r) != size for r in rows):
        raise SystemExit("a grid is %d rows of %d" % (size, size))
    return [[GRID_PAL[ch] for ch in row] for row in rows]


ICON32 = """
................................
................................
.......aaaaaaaaaaaaaaaaaaaaa....
.......abbbbbbbbbbbbbbbbbbbbc...
.......abbbbbbbbbbbbbbbbbbbbc...
...aaaaaaaaaaaaaaaaaaaaabbbbc...
...addddddddddddddddddddcbbbc...
...addddddddddddddddddddcbbbc...
...addeeeeeeeeeeeeeeeeddcbbbc...
...addeeeeeeeeeeeeeeeeddcbbbc...
...addddddddddddddddddddcbbbc...
...addeeeeeeeeeeeeeeeeddcbbbc...
...addeeeeeeeeeeeeeeeeddcbbbc...
...addddddddddddddddddddcbbbc...
...addddddddddddddddddddcbbbc...
...addfffffffdggggggggddcbbbc...
...addfhhhfffdddddddddddcbbbc...
...addfhhhfffdggggggggddcbbbc...
...addfffffffdddddddddddcbbbc...
...addfffffffdggggggggddcbbbc...
...addfffffffdddddddddddcbbbc...
...addfffffffdggggggggddcbbbc...
...addddddddddddddddddddcbbbc...
...addddddddddddddddddddcbbbc...
...addggggggggggggggggddcbbbc...
...addddddddddddddddddddcbbbc...
...addggggggggggggddddddccccc...
...addddddddddddddddddddc.......
...addddddddddddddddddddc.......
....ccccccccccccccccccccc.......
................................
................................
"""

ICON16 = """
................
....aaaaaaaaaa..
....abbbbbbbbbc.
..aaaaaaaaaabbc.
..addddddddcbbc.
..adeeeeeedcbbc.
..addddddddcbbc.
..adffdggddcbbc.
..adfhdddddcbbc.
..adffdggddcbbc.
..addddddddcbbc.
..adggggggdcccc.
..addddddddc....
..adggggdddc....
...ccccccccc....
................
"""


def draw32():
    return grid(ICON32, 32)


def draw16():
    return grid(ICON16, 16)


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
 * A folded newspaper, face on, with a second sheet behind it, drawn the way
 * the toolbar's newspaper is. Written as raw data blocks rather than
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
