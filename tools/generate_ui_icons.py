#!/usr/bin/env python3
"""
Draw Gazette's own small icons — the sidebar's and the toolbar's — and emit
them as Rez source.

The sidebar's three are for the standing folders above the feed list: a sun
for Today, a blue ring for All Unread, and a gold star for Starred, plus the
star again at the size a starred headline is marked with.

The toolbar's are one per button. Every one of them is a verb rather than a
thing, which is the hard part of drawing them: "mark all as read" has no
object in the world to picture, so the set leans on one small vocabulary
instead — a stack of bars is the list, a blue dot is unread, an open ring is
read, and a check is "all of them".

Everything else in the window is a *system* icon, taken from Icon Services by
constant — the generic folder, the open folder, the news location, the generic
document — because a reader already knows what those mean and drawing our own
would only make them look foreign. These have no system equivalent, so they
are ours, and they are drawn here in code for the same reason the Finder icon
is: at sixteen pixels square every pixel carries weight and nothing survives
being resampled from larger art.

    python3 tools/generate_ui_icons.py --out Resources/Gazette_ui_icons.r
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
BLUE = (51, 153, 255)               # unread: the sidebar's ring, and the dot
GOLD = (255, 204, 51)               # the star

# The toolbar's structural ink. Dark enough to read on Platinum's grey and on
# a pressed button's darker grey, and the one colour most of these glyphs are
# in: a toolbar of eight differently coloured shapes is a circus, so colour is
# spent only where it carries meaning — blue for unread, gold for starred.
INK = (51, 51, 51)


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


def frame(px, x0, y0, x1, y1, colour):
    """Inclusive one-pixel border."""
    for x in range(x0, x1 + 1):
        put(px, x, y0, colour)
        put(px, x, y1, colour)
    for y in range(y0, y1 + 1):
        put(px, x0, y, colour)
        put(px, x1, y, colour)


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


# --- The toolbar's icons ----------------------------------------------------
#
# One per button, and each one drawn to say what the button *does* rather than
# what it acts on. The three that toggle have two drawings apiece, because a
# button whose label changes and whose picture does not is a button that lies
# half the time.

def draw_sidebar():
    """A pane with a band down its left edge: the sidebar, in a window."""
    px = blank(16)
    frame(px, 1, 2, 14, 13, INK)
    fill(px, 2, 3, 5, 12, INK)
    return px


def draw_refresh():
    """
    A circular arrow: a ring with a gap cut out of its top right, and a head
    on the near side of the gap pointing the way it was going. The gap is what
    makes it an arrow rather than a doughnut, and which side the head is on is
    what makes it turn clockwise.
    """
    px = blank(16)
    disc(px, 8.0, 8.0, 6.8, INK)
    hollow(px, 8.0, 8.0, 4.4)

    # Clear the arc from a little east of north round to due east.
    for y in range(16):
        for x in range(16):
            if px[y][x] is CLEAR:
                continue
            a = math.degrees(math.atan2(y + 0.5 - 8.0, x + 0.5 - 8.0))
            if -95.0 <= a <= 0.0:
                px[y][x] = CLEAR

    # The head, on the northern end of what is left, pointing the way the arc
    # was going. A triangle with its base against the arc and its apex east of
    # it: three pixels of run is all there is room for and all it needs.
    for i in range(4):
        fill(px, 8 + i, 2 - (3 - i), 8 + i, 2 + (3 - i), INK)
    return px


def draw_bars(px, colour):
    """The stack of lines that means "the list" in three of these."""
    fill(px, 1, 3, 8, 4, colour)
    fill(px, 1, 7, 8, 8, colour)
    fill(px, 1, 11, 8, 12, colour)


def draw_check(px, colour):
    """A tick, two pixels thick, in the right hand third."""
    for i in range(3):
        fill(px, 9 + i, 8 + i, 9 + i, 9 + i, colour)
    for i in range(5):
        fill(px, 11 + i, 10 - i, 11 + i, 11 - i, colour)


def draw_mark_all_read():
    px = blank(16)
    draw_bars(px, INK)
    draw_check(px, INK)
    return px


def draw_mark_all_unread():
    px = blank(16)
    draw_bars(px, INK)
    disc(px, 12.5, 8.0, 3.0, BLUE)
    return px


def draw_eye(px, colour):
    """
    An almond with a pupil in it.

    Drawn column by column from a half-height that follows a parabola, and
    each column joined to the one before it.

    A parabola rather than the ellipse this started as: an ellipse is flat
    across the middle and blunt at the ends, which at sixteen pixels comes out
    as a rounded rectangle with a blob in it — a camera, not an eye. The
    parabola tapers all the way to its ends, which is what gives the almond
    its corners. Joining each column to the one before matters for the same
    reason either way: taking the two edge pixels on their own leaves the
    outline dotted wherever the curve climbs faster than a pixel a column.
    """
    prev = None
    for x in range(1, 15):
        t = (x + 0.5 - 8.0) / 7.0
        h = 4.2 * (1.0 - t * t)
        top = int(8.0 - h)
        bot = int(8.0 + h) - 1
        if prev is not None:
            fill(px, x, min(top, prev[0]), x, max(top, prev[0]), colour)
            fill(px, x, min(bot, prev[1]), x, max(bot, prev[1]), colour)
        else:
            put(px, x, top, colour)
            put(px, x, bot, colour)
        prev = (top, bot)
    disc(px, 8.0, 8.0, 1.8, colour)


def draw_hide_read():
    """
    Hiding the ones that have been read: an eye struck through. The slash
    carries a pixel of clearance either side of it, so that it reads as
    crossing the eye rather than as part of it — cleared first and drawn
    second, which is the only order that leaves the clearance under the line.
    """
    px = blank(16)
    draw_eye(px, INK)

    for i in range(1, 15):
        put(px, i, i, CLEAR)
        put(px, i, i + 1, CLEAR)
        put(px, i + 1, i, CLEAR)
    for i in range(1, 15):
        put(px, i, i, INK)
    return px


def draw_show_read():
    """And showing them again: the same eye, open."""
    px = blank(16)
    draw_eye(px, INK)
    return px


def draw_unread_dot():
    """Mark as Unread: a filled dot, which is what unread looks like."""
    px = blank(16)
    disc(px, 8.0, 8.0, 5.2, BLUE)
    return px


def draw_read_ring():
    """Mark as Read: the same dot emptied out."""
    px = blank(16)
    disc(px, 8.0, 8.0, 5.2, BLUE)
    hollow(px, 8.0, 8.0, 3.4)
    return px


def draw_next_unread():
    """A dot with an arrow under it: on to the next one not yet read."""
    px = blank(16)
    disc(px, 8.0, 3.5, 3.0, BLUE)
    fill(px, 7, 7, 8, 10, INK)
    for i in range(4):
        fill(px, 4 + i, 10 + i, 11 - i, 10 + i, INK)
    return px


def draw_browser():
    """
    A window with an arrow leaving it through the top right — which is what
    every "open this somewhere else" has looked like since before this machine
    was built. The window is small and low so the arrow has room to be an
    arrow; at this size the two cannot both be full height.
    """
    px = blank(16)
    frame(px, 0, 6, 9, 15, INK)
    fill(px, 1, 7, 8, 8, INK)           # its title bar

    # The shaft, out through the corner, and the head at the end of it.
    for i in range(6):
        put(px, 8 + i, 7 - i, INK)
        put(px, 9 + i, 7 - i, INK)
    fill(px, 10, 0, 15, 1, INK)         # the head's top edge
    fill(px, 14, 0, 15, 5, INK)         # and its right edge
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
 * Gazette_ui_icons.r - the sidebar's and the toolbar's own small icons.
 *
 * DO NOT EDIT. Regenerate with:
 *   python3 tools/generate_ui_icons.py --out Resources/Gazette_ui_icons.r
 *
 * 16x16 families only: these are drawn in list rows and on toolbar buttons
 * and nowhere else, so there is no 32x32 member to carry. GetIconSuite reads them by ID out of the
 * application's own resource fork and PlotIconSuite draws them, which is the
 * one path that does not need an Icon Services registration -- the system
 * icons beside them in the sidebar have no resource IDs to read, and these
 * have no reason to be registered.
 *
 * Written as raw data blocks rather than Rez icon templates for the reason
 * Gazette_icon.r records: Rez runs against whichever RIncludes the toolchain
 * has linked, and a resource file with no includes does not care which.
 *
 * IDs are the kIcon* constants in src/ui/platinum_window.c.
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

        # The toolbar. The three pairs are the toggles: each button shows the
        # drawing for what it would do next, the way its menu item shows the
        # words for it.
        (132, "Sidebar", draw_sidebar()),
        (133, "Refresh", draw_refresh()),
        (134, "Mark All as Read", draw_mark_all_read()),
        (135, "Mark All as Unread", draw_mark_all_unread()),
        (136, "Hide Read Articles", draw_hide_read()),
        (137, "Show Read Articles", draw_show_read()),
        (138, "Mark as Read", draw_read_ring()),
        (139, "Mark as Unread", draw_unread_dot()),
        (140, "Next Unread", draw_next_unread()),
        (141, "Open in Browser", draw_browser()),
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
