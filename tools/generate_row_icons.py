#!/usr/bin/env python3
"""
Draw the sidebar's own small icons and emit them as Rez source.

Three of them, for the three smart folders that sit above the feed list: a sun
for Today, a blue disc for All Unread, and a yellow star for Starred. The
fourth is the same star again at the size the headline list marks a starred
article with.

Everything else in the sidebar is a *system* icon, taken from Icon Services by
constant — the generic folder, the open folder, the news location, the generic
document — because a reader already knows what those mean and drawing our own
would only make them look foreign. These four have no system equivalent, so
they are ours, and they are drawn here in code for the same reason the Finder
icon is: at sixteen pixels square every pixel carries weight and nothing
survives being resampled from larger art.

    python3 tools/generate_row_icons.py --out Resources/Gazette_row_icons.r
                                        [--ascii]

The palette rule is the Finder icon's: components on the 51-step cube of the
Macintosh 256-colour system palette, where the index is r*36 + g*6 + b on the
descending 0..5 scale, with black the one exception at 255. See
tools/generate_icon.py, which this file deliberately mirrors.

Each shape is one flat colour, with no rim and no shading: that is what the
reference art is, and it is what sixteen pixels square can actually hold. The
1-bit members are therefore the silhouette rather than the dark parts of the
drawing — see family() — so a black and white screen gets three solid shapes
that still tell each other apart by outline alone.
"""

import argparse
import math

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

# Sampled off the reference art and moved to the nearest colour on the cube:
# (255,118,0) -> (255,102,0), (84,148,243) -> (51,153,255) and
# (248,191,46) -> (255,204,51). Each shape is one flat colour with no rim and
# no shading, which is what the reference is and what sixteen pixels square
# can actually hold.
ORANGE = (255, 102, 0)              # the sun
BLUE = (51, 153, 255)               # the unread ring and its centre
GOLD = (255, 204, 51)               # the star


def luminance(c):
    return 0.299 * c[0] + 0.587 * c[1] + 0.114 * c[2]


# --- Rasterising ------------------------------------------------------------

def blank(size):
    return [[CLEAR] * size for _ in range(size)]


def put(px, x, y, colour):
    size = len(px)
    if 0 <= x < size and 0 <= y < size:
        px[y][x] = colour


def fill(px, x0, y0, x1, y1, colour):
    """Inclusive rectangle fill, clipped to the canvas."""
    size = len(px)
    for y in range(max(0, y0), min(size - 1, y1) + 1):
        for x in range(max(0, x0), min(size - 1, x1) + 1):
            px[y][x] = colour


def disc(px, cx, cy, r, colour):
    """A filled circle, by distance from the centre of each pixel."""
    size = len(px)
    for y in range(size):
        for x in range(size):
            dx = x + 0.5 - cx
            dy = y + 0.5 - cy
            if dx * dx + dy * dy <= r * r:
                px[y][x] = colour


def hollow(px, cx, cy, r):
    """Clear a circle back out again — the inside of a ring."""
    size = len(px)
    for y in range(size):
        for x in range(size):
            dx = x + 0.5 - cx
            dy = y + 0.5 - cy
            if dx * dx + dy * dy <= r * r:
                px[y][x] = CLEAR


def polygon(px, points, colour):
    """
    A filled polygon, by even-odd scanline crossing at each pixel's centre.

    Written out rather than reached for in a library because the whole point
    of this file is that the pixels are ours: a star drawn by anything that
    antialiases would arrive with colours that are not on the cube.
    """
    size = len(px)
    n = len(points)
    for y in range(size):
        yc = y + 0.5
        xs = []
        for i in range(n):
            x0, y0 = points[i]
            x1, y1 = points[(i + 1) % n]
            if (y0 <= yc < y1) or (y1 <= yc < y0):
                xs.append(x0 + (yc - y0) * (x1 - x0) / (y1 - y0))
        xs.sort()
        for i in range(0, len(xs) - 1, 2):
            for x in range(size):
                if xs[i] <= x + 0.5 <= xs[i + 1]:
                    px[y][x] = colour


def star_points(cx, cy, outer, inner):
    """Five-pointed, first point straight up."""
    out = []
    for i in range(5):
        a = math.radians(-90 + i * 72)
        out.append((cx + outer * math.cos(a), cy + outer * math.sin(a)))
        a = math.radians(-90 + 36 + i * 72)
        out.append((cx + inner * math.cos(a), cy + inner * math.sin(a)))
    return out


# --- The four icons ---------------------------------------------------------

def draw_today():
    """A sun: a face with eight rays around it, clear of it on every side."""
    px = blank(16)

    # The face: rows and columns 4 through 11, symmetrical about the seam
    # between pixels 7 and 8, which is where a sixteen-wide canvas has its
    # middle. There is no centre pixel, so nothing here may be one pixel wide.
    disc(px, 8.0, 8.0, 4.0, ORANGE)

    # The rays, placed by hand rather than by trigonometry. Two problems with
    # working them out from an angle at this size: a cosine that ought to be
    # zero comes back as 1e-16 and the rounding sends the ray a pixel to one
    # side, and a ray of a given length comes out visibly longer on a diagonal,
    # where a pixel covers half as much again of the ground.
    #
    # So the four on the axes are two pixels long and two wide, straddling the
    # seam, and the four on the corners are a two-pixel staircase reaching the
    # same distance. Each is clear of the face by a pixel, which is what makes
    # the sun read as shining rather than as a cogwheel.
    fill(px, 7, 0, 8, 1, ORANGE)            # up
    fill(px, 7, 14, 8, 15, ORANGE)          # down
    fill(px, 0, 7, 1, 8, ORANGE)            # left
    fill(px, 14, 7, 15, 8, ORANGE)          # right
    for x, y in ((2, 2), (3, 3), (13, 2), (12, 3),
                 (2, 13), (3, 12), (13, 13), (12, 12)):
        put(px, x, y, ORANGE)
    return px


def draw_unread():
    """
    A ring with a disc inside it, the way the reference draws "unread": the
    gap between the two is what keeps it from reading as a plain bullet, and
    it is the one shape here that needs three radii rather than one.
    """
    px = blank(16)
    disc(px, 8.0, 8.0, 7.2, BLUE)
    hollow(px, 8.0, 8.0, 6.1)
    disc(px, 8.0, 8.0, 4.0, BLUE)
    return px


def draw_star(cy, outer, inner):
    """
    Centred horizontally on 8.0 and vertically on a point of its own: a star
    is wider below its middle than above it, so hanging it on the canvas
    centre leaves it looking as though it has slipped upwards.
    """
    px = blank(16)
    polygon(px, star_points(8.0, cy, outer, inner), GOLD)
    return px


# --- Rez emission -----------------------------------------------------------

def hexrows(data, per=16, indent="\t"):
    rows = []
    for i in range(0, len(data), per):
        rows.append(indent + '$"' +
                    " ".join("%02X" % b for b in data[i:i + per]) + '"')
    return "\n".join(rows)


def bitmap(px, predicate):
    """Pack a 1-bit-per-pixel bitmap, MSB first."""
    size = len(px)
    out = bytearray()
    for y in range(size):
        for byte in range(size // 8):
            v = 0
            for bit in range(8):
                if predicate(px[y][byte * 8 + bit]):
                    v |= 0x80 >> bit
            out.append(v)
    return bytes(out)


def family(px, res_id, name):
    """
    The 1-bit member is the shape's silhouette rather than its dark parts.
    Each icon here is one flat colour, and two of the three are light enough
    that a luminance threshold would ink nothing at all and leave a blank
    square on a black and white screen. A solid silhouette is also what the
    system's own small icons did before colour, and the gaps — the sun's rays,
    the ring round the unread disc — survive it, which is what keeps the three
    telling each other apart.
    """
    ink = bitmap(px, lambda c: c is not CLEAR)
    mask = ink
    ics8 = bytes(mac8(c) if c is not CLEAR else 0 for row in px for c in row)

    return ("data 'ics#' (%d, \"%s\", purgeable) {\n%s\n};\n\n"
            "data 'ics8' (%d, \"%s\", purgeable) {\n%s\n};\n"
            % (res_id, name, hexrows(ink + mask),
               res_id, name, hexrows(ics8)))


HEADER = """/*
 * Gazette_row_icons.r - the sidebar's own small icons.
 *
 * DO NOT EDIT. Regenerate with:
 *   python3 tools/generate_row_icons.py --out Resources/Gazette_row_icons.r
 *
 * 16x16 families only: these are drawn in list rows and nowhere else, so
 * there is no 32x32 member to carry. GetIconSuite reads them by ID out of the
 * application's own resource fork and PlotIconSuite draws them, which is the
 * one path that does not need an Icon Services registration -- the system
 * icons beside them in the sidebar have no resource IDs to read, and these
 * have no reason to be registered.
 *
 * Written as raw data blocks rather than Rez icon templates for the reason
 * Gazette_icon.r records: Rez runs against whichever RIncludes the toolchain
 * has linked, and a resource file with no includes does not care which.
 *
 * IDs are the kGazetteIcon* constants in src/ui/platinum_window.c.
 */
"""


def ascii_art(px):
    ramp = " .:-=+*#%@"
    for row in px:
        line = ""
        for c in row:
            line += (" " if c is CLEAR
                     else ramp[min(9, int((255 - luminance(c)) / 26))])
        print(line)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out")
    ap.add_argument("--ascii", action="store_true")
    args = ap.parse_args()

    icons = [
        (128, "Today", draw_today()),
        (129, "All Unread", draw_unread()),
        # The sidebar's star fills its row's icon column; the one the headline
        # list marks an article with sits in a text line and is drawn smaller
        # so that it does not out-shout the headline beside it.
        (130, "Starred", draw_star(8.4, 7.4, 3.8)),
        (131, "Starred Article", draw_star(8.6, 6.0, 3.1)),
    ]

    if args.ascii:
        for res_id, name, px in icons:
            print("%s (%d)" % (name, res_id))
            ascii_art(px)
            print()

    text = HEADER + "\n" + "\n".join(
        family(px, res_id, name) for res_id, name, px in icons)

    if args.out:
        with open(args.out, "w") as f:
            f.write(text)
    elif not args.ascii:
        print(text)


if __name__ == "__main__":
    main()
