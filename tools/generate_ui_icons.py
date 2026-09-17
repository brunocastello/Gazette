#!/usr/bin/env python3
"""
Draw Gazette's own small icons — the sidebar's and the toolbar's — and emit
them as Rez source.

The sidebar's three are for the standing folders above the feed list: a sun
for Today, a newspaper with an unread dot for All Unread, and a gold star for
Starred, plus the star again at the size a starred headline is marked with.

The toolbar's are one per button, and a New and a Find that the toolbar will
grow into. They are drawn in the idiom of Outlook Express 5's toolbar, pixel
for pixel the way that art is built: a one-pixel black outline on the bottom
and right of everything, a grey one on the top and left of anything white,
flat fills from the system palette with a single paler tone for a highlight,
and a badge — a plus, a check, a cross, a dot — overlapping the corner of the
object it qualifies rather than sitting inside it. The newspaper is the
object most of them qualify, the way OE's is the envelope.

Everything else in the window is a *system* icon, taken from Icon Services by
constant — the generic folder, the open folder, the news location, the generic
document — because a reader already knows what those mean and drawing our own
would only make them look foreign. These have no system equivalent, so they
are ours, and they are drawn here as sixteen-by-sixteen grids of letters for
the same reason the Finder icon is drawn in code: at this size every pixel
carries weight and nothing survives being resampled from larger art. The two
that are round — the refresh ring and the green button — are rasterised from
a radius and then outlined, because a hand-drawn circle at this size is a
lumpy one.

    python3 tools/generate_ui_icons.py --out Resources/Gazette_ui_icons.r
                                       [--ascii]

The palette rule is the Finder icon's: colours on the Macintosh 256-colour
system palette, which is the 51-step cube (index r*36 + g*6 + b on the
descending 0..5 scale) plus the red, green, blue and grey ramps in the last
forty entries, with black at 255. See tools/generate_icon.py, which this file
deliberately mirrors.
"""

import argparse
import math

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
    if g == b == 0 and r in RAMP:
        return 215 + RAMP.index(r)
    if r == b == 0 and g in RAMP:
        return 225 + RAMP.index(g)
    if r == g == 0 and b in RAMP:
        return 235 + RAMP.index(b)
    if r == g == b and r in RAMP:
        return 245 + RAMP.index(r)
    raise SystemExit("colour %r is not on the system palette" % (rgb,))


# --- Palette ----------------------------------------------------------------
#
# One letter per colour, and the letters are what the drawings below are
# written in. Sampled off OE's toolbar and named for the job each does there:
# the paper is white shaded with 'C' inside its dark edges and outlined in 'A'
# on the light side; the four blues are OE's lavender ramp; the rest is spent
# only where it means something — green for read, red for unread and for
# hiding, gold for starred.

CLEAR = None

PAL = {
    '.': CLEAR,
    'D': (0, 0, 0),             # black: the outline on the dark side
    'B': (255, 255, 255),       # white
    'C': (0xEE, 0xEE, 0xEE),    # paper, shaded inside its dark edges
    'F': (204, 204, 204),       # light grey: the shade column, the title bar
    'A': (0x77, 0x77, 0x77),    # the outline on the light side of white things
    'I': (153, 153, 153),       # text lines
    'J': (102, 102, 102),       # the headline
    'L': (204, 204, 255),       # blue, pale: a read newspaper's picture
    'M': (153, 153, 255),       # blue, light: highlights on the blues below
    'N': (102, 102, 204),       # blue, mid: the refresh ring, the picture
    'P': (51, 102, 255),        # blue, bright: the plus on New
    'G': (0, 204, 0),           # green
    'H': (0, 153, 0),           # green, dark: the shadow side of green
    'g': (102, 255, 102),       # green, light: the lit side, and the earth's land
    'R': (255, 0, 0),           # red
    'S': (153, 0, 0),           # red, dark
    'r': (255, 153, 153),       # red, light: the dot's highlight
    'Y': (255, 204, 0),         # gold
    'y': (255, 255, 153),       # gold, light: the star's and the sun's lit side
    'o': (255, 153, 0),         # orange: their shadow side, and the sun's rays
    'X': (204, 51, 0),          # red-orange: the shadow down the glass's handle
    'T': (204, 255, 255),       # the glass
    't': (153, 204, 204),       # the glass, where the light is not
    'U': (51, 153, 255),        # ocean
    'u': (153, 204, 255),       # ocean, where the light is
    'V': (0, 102, 204),         # ocean, deep
}


def luminance(c):
    return 0.299 * c[0] + 0.587 * c[1] + 0.114 * c[2]


# --- Grids ------------------------------------------------------------------

def grid(text):
    """Sixteen rows of sixteen letters, each a key of PAL."""
    rows = text.strip("\n").split("\n")
    if len(rows) != 16 or any(len(r) != 16 for r in rows):
        raise SystemExit("a grid is sixteen rows of sixteen")
    for r in rows:
        for ch in r:
            if ch not in PAL:
                raise SystemExit("no colour is called %r" % ch)
    return [list(r) for r in rows]


def badge(text):
    """A smaller grid, laid over a larger one by overlay()."""
    return [list(r) for r in text.strip("\n").split("\n")]


def overlay(base, top, x0, y0):
    """The badge's drawn pixels over the base's, at an offset."""
    out = [row[:] for row in base]
    for y, row in enumerate(top):
        for x, ch in enumerate(row):
            if ch != '.':
                out[y0 + y][x0 + x] = ch
    return out


def outline(g):
    """A black pixel on every empty one that touches the drawing."""
    out = [row[:] for row in g]
    for y in range(16):
        for x in range(16):
            if g[y][x] != '.':
                continue
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                nx, ny = x + dx, y + dy
                if 0 <= nx < 16 and 0 <= ny < 16 and g[ny][nx] != '.':
                    out[y][x] = 'D'
                    break
    return out


def shade_gold(g):
    """
    The light on a gold shape: pale where an edge faces up or left, orange
    where one faces down or right, and the fill's own colour between.
    Written as a rule over the outline rather than by hand so that the star
    and the sun catch the light from the same side.
    """
    out = [row[:] for row in g]
    for y in range(16):
        for x in range(16):
            if g[y][x] != 'Y':
                continue
            up = g[y - 1][x] if y else 'D'
            left = g[y][x - 1] if x else 'D'
            down = g[y + 1][x] if y < 15 else 'D'
            right = g[y][x + 1] if x < 15 else 'D'
            if left == 'D' and x <= 7:
                out[y][x] = 'y'
            elif up == 'D' and x <= 6:
                out[y][x] = 'y'
            elif down == 'D' and (x >= 7 or y >= 12):
                out[y][x] = 'o'
            elif right == 'D' and y >= 9:
                out[y][x] = 'o'
    return out


# --- The newspaper and its badges ---------------------------------------------
#
# The one object most of the toolbar qualifies. A white sheet outlined the OE
# way, a shaded fold down its left edge, a two-line headline, three lines of
# text beside a picture, and two more lines under them.

NEWSPAPER = grid("""
................
.AAAAAAAAAAAAA..
.ACIBBBBBBBBBFD.
.ACIBJJJJJJJBFD.
.ACIBJJJJJJJBFD.
.ACIBBBBBBBBBFD.
.ACIBIIIBNNNBFD.
.ACIBBBBBNNNBFD.
.ACIBIIIBNNNBFD.
.ACIBBBBBBBBBFD.
.ACIBIIIIIIIBFD.
.ACIBBBBBBBBBFD.
.ACIBIIIIIIIBFD.
.AFFFFFFFFFFFFD.
.DDDDDDDDDDDDDD.
................
""")

# The same sheet once it has been read: everything on it faded to the paper's
# own greys, the picture to pale lavender.
NEWSPAPER_READ = grid("""
................
.AAAAAAAAAAAAA..
.ACFBBBBBBBBBFD.
.ACFBFFFFFFFBFD.
.ACFBFFFFFFFBFD.
.ACFBBBBBBBBBFD.
.ACFBFFFBLLLBFD.
.ACFBBBBBLLLBFD.
.ACFBFFFBLLLBFD.
.ACFBBBBBBBBBFD.
.ACFBFFFFFFFBFD.
.ACFBBBBBBBBBFD.
.ACFBFFFFFFFBFD.
.AFFFFFFFFFFFFD.
.DDDDDDDDDDDDDD.
................
""")

# The badges, each with its own black outline, laid over a corner of the
# sheet the way OE's pencil lies over the corner of its New sheet.
PLUS = badge("""
..DDDD..
..DMPD..
DDDMPDDD
DMMMPPPD
DPPPPPPD
DDDPPDDD
..DPPD..
..DDDD..
""")

CHECK = badge("""
......DD
.....DGD
DD..DGGD
DGD.DGD.
DGGDGHD.
.DGGHD..
..DHD...
...D....
""")

CROSS = badge("""
DD....DD
DRD..DRD
.DRDDRD.
..DRRD..
..DRSD..
.DRDDSD.
DRD..DSD
DD....DD
""")

DOT = badge("""
.DDD.
DrRRD
DRRRD
DRSSD
.DDD.
""")


# --- The sidebar's --------------------------------------------------------------

def draw_today():
    """
    A sun: a face with eight rays, drawn as one silhouette and outlined as
    one, which is what keeps it in the family with the star beside it. The
    rays touch the face rather than standing clear of it, because with an
    outline round everything a gap of a pixel is a gap of black.
    """
    g = [['.'] * 16 for _ in range(16)]
    cx = cy = 8.0
    for y in range(16):
        for x in range(16):
            if math.hypot(x + 0.5 - cx, y + 0.5 - cy) <= 3.6:
                g[y][x] = 'Y'

    # The four on the axes, two wide and three long, and the four on the
    # corners, a two-wide staircase reaching about as far.
    for x, y in ((7, 1), (8, 1), (7, 2), (8, 2), (7, 3), (8, 3),
                 (7, 12), (8, 12), (7, 13), (8, 13), (7, 14), (8, 14),
                 (1, 7), (1, 8), (2, 7), (2, 8), (3, 7), (3, 8),
                 (12, 7), (12, 8), (13, 7), (13, 8), (14, 7), (14, 8),
                 (3, 3), (4, 4), (3, 4), (4, 3),
                 (12, 3), (11, 4), (12, 4), (11, 3),
                 (3, 12), (4, 11), (3, 11), (4, 12),
                 (12, 12), (11, 11), (11, 12), (12, 11)):
        g[y][x] = 'o'

    # The face lit from the upper left, like the star.
    for y in range(16):
        for x in range(16):
            if g[y][x] != 'Y':
                continue
            d = math.hypot(x + 0.5 - cx, y + 0.5 - cy)
            a = math.degrees(math.atan2(y + 0.5 - cy, x + 0.5 - cx))
            if d > 2.4 and (a < -60 or a > 150):
                g[y][x] = 'y'
            elif d > 2.4 and -30 < a < 120:
                g[y][x] = 'o'
    return outline(g)


def draw_star():
    """
    The star, cut from the reference sprite: fifteen wide, its top point on
    the canvas's middle column, wider below its waist than above it.
    """
    return shade_gold(grid("""
.......D........
......DYD.......
......DYD.......
.....DYYYD......
.....DYYYD......
DDDDDDYYYDDDDDD.
.DYYYYYYYYYYYD..
..DYYYYYYYYYD...
...DYYYYYYYD....
...DYYYYYYYD....
..DYYYYDYYYYD...
..DYYYDDDYYYD...
.DYYYD...DYYYD..
.DYDD.....DDYD..
.DD.........DD..
................
"""))


def draw_star_small():
    """
    The same star at the size a headline is marked with: eleven wide, so that
    it sits in a text line without out-shouting the headline beside it.
    """
    return shade_gold(grid("""
................
................
.......D........
......DYD.......
......DYD.......
..DDDDDYDDDDD...
...DYYYYYYYD....
....DYYYYYD.....
....DYYYYYD.....
...DYYDDDYYD....
...DYDD.DDYD....
...DD.....DD....
................
................
................
................
"""))


# --- The toolbar's --------------------------------------------------------------

def draw_new():
    """The newspaper with a plus over its lower right corner."""
    return overlay(NEWSPAPER, PLUS, 8, 8)


def draw_sidebar():
    """
    A Platinum window with its sidebar showing: built the way the newspaper
    is, grey outline on the light side and black on the dark, a shade column
    and row inside the dark edges. A two-row title bar with a rule under it,
    a shaded pane with three feed lines, and a white pane beside it.
    """
    return grid("""
................
.AAAAAAAAAAAAA..
.AFFFFFFFFFFFFD.
.AFFFFFFFFFFFFD.
.ADDDDDDDDDDDDD.
.ACCCCCDBBBBBFD.
.ACIIICDBBBBBFD.
.ACCCCCDBBBBBFD.
.ACCCCCDBBBBBFD.
.ACIIICDBBBBBFD.
.ACCCCCDBBBBBFD.
.ACCCCCDBBBBBFD.
.ACIIICDBBBBBFD.
.AFFFFFFFFFFFFD.
.DDDDDDDDDDDDDD.
................
""")


def draw_refresh():
    """
    A circular arrow: a ring rasterised from a true circle, opened between
    about one and four o'clock, with a right-pointing triangle on its upper
    end. The ring sits a pixel and a half low so that the triangle's tip has
    its outline inside the frame. Paler along the top left, the way the rest
    of the blues are lit.
    """
    cx, cy = 7.5, 9.0
    ss = 8
    r_in, r_out = 3.9, 5.9
    gap0, gap1 = -75, 20                 # degrees; 0 is east, -90 north

    g = [['.'] * 16 for _ in range(16)]
    for y in range(16):
        for x in range(16):
            cover = 0
            for sy in range(ss):
                for sx in range(ss):
                    px = x + (sx + 0.5) / ss
                    py = y + (sy + 0.5) / ss
                    d = math.hypot(px - cx, py - cy)
                    a = math.degrees(math.atan2(py - cy, px - cx))
                    if r_in <= d <= r_out and not (gap0 <= a <= gap1):
                        cover += 1
            if cover >= ss * ss // 2:
                g[y][x] = 'N'

    # The head: a triangle with its base down column 9, rows 1 to 7, and its
    # apex three columns east, sitting on the ring's northern end.
    bx, y0, y1 = 9, 1, 7
    for k in range((y1 - y0) // 2 + 1):
        for y in range(y0 + k, y1 - k + 1):
            g[y][bx + k] = 'N'

    for y in range(16):
        for x in range(16):
            if g[y][x] == 'N' and x < 9:
                d = math.hypot(x + 0.5 - cx, y + 0.5 - cy)
                a = math.degrees(math.atan2(y + 0.5 - cy, x + 0.5 - cx))
                if d < 4.9 and -200 < a < -70:
                    g[y][x] = 'M'
    return outline(g)


def draw_mark_all_read():
    """The newspaper with a check over its lower right corner."""
    return overlay(NEWSPAPER, CHECK, 8, 8)


def draw_hide_read():
    """A read newspaper with a cross in the corner the check goes in."""
    return overlay(NEWSPAPER_READ, CROSS, 8, 8)


def draw_mark_read():
    """
    A round green button with a white check on it: a fourteen-pixel disc
    from a true circle, a one-pixel bevel lit from the upper left, and the
    check centred with clear green all round it.
    """
    cx = cy = 7.5
    ss = 8
    g = [['.'] * 16 for _ in range(16)]
    for y in range(16):
        for x in range(16):
            fill = edge = 0
            for sy in range(ss):
                for sx in range(ss):
                    d = math.hypot(x + (sx + 0.5) / ss - cx,
                                   y + (sy + 0.5) / ss - cy)
                    if d <= 6.6:
                        fill += 1
                    elif d <= 7.6:
                        edge += 1
            if fill >= ss * ss // 2:
                g[y][x] = 'G'
            elif fill + edge >= ss * ss // 2:
                g[y][x] = 'D'

    for y in range(16):
        for x in range(16):
            if g[y][x] != 'G':
                continue
            d = math.hypot(x + 0.5 - cx, y + 0.5 - cy)
            a = math.degrees(math.atan2(y + 0.5 - cy, x + 0.5 - cx))
            if d > 5.6:
                if a < -55 or a > 145:
                    g[y][x] = 'g'
                elif -35 < a < 125:
                    g[y][x] = 'H'
    # Two pixels the rule leaves standing alone where light turns to dark.
    for x, y in ((10, 2), (4, 12)):
        g[y][x] = 'G'

    return overlay(g, badge("""
.......BB
......BB.
.....BB..
BB..BB...
.BBBB....
..BB.....
"""), 3, 5)


def draw_next_unread():
    """The newspaper with an unread dot over its top right corner."""
    return overlay(NEWSPAPER, DOT, 10, 0)


def draw_browser():
    """
    The earth, with an arrow rising from under it into its middle: ocean
    lit from the upper left and deep down the right, land in the bright
    green, the arrow in gold.
    """
    return grid("""
.....DDDDD......
...DDuuuUUDD....
..DuuuUggUUUD...
.DuuUgggggUUUD..
.DuUgggUggUUVD..
DuUUggUUUUgUUVD.
DuUUUgUDDUUUUVD.
DUUUUUDYYDUUUVD.
DUUggDYYYYDUUVD.
.DUgDYYYYYYDVD..
.DUUDDDYYDDDUD..
..DUUUDYYDUUD...
...DDDDYYDDD....
......DYYD......
......DYYD......
......DDDD......
""")


def draw_find():
    """
    A magnifying glass the way Sherlock draws its own: the lens up and to
    the right, round, its rim grey on the near side and dark on the far, the
    glass pale with a highlight and deeper toward the lower right; a gold
    ferrule, and a handle down to the lower left with its shadow along it.
    """
    return grid("""
.......DDDDD....
.....DDIIIIIDD..
....DIITTTTTIJD.
....DITBBTTTTJD.
...DITBTTTTTTtJD
...DITTTTTTTttJD
...DITTTTTTtttJD
....DITTTttttJD.
....DIJtttttJJD.
.....DDJJJJJDD..
...DYYDDDDDDD...
..DooXD.........
.DooXD..........
DooXD...........
DoXD............
DDD.............
""")


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
    The 1-bit member is the drawing's dark half — the outline and whatever is
    darker than mid-grey — over a mask that is the whole silhouette. Every
    one of these is an outlined shape now, so on a black and white screen the
    outline alone is the picture, which is what the system's own small icons
    did before colour and what a solid silhouette would throw away.
    """
    colours = [[PAL[ch] for ch in row] for row in px]
    ink = bitmap(colours, lambda c: c is not CLEAR and luminance(c) < 128)
    mask = bitmap(colours, lambda c: c is not CLEAR)
    ics8 = bytes(mac8(c) if c is not CLEAR else 0
                 for row in colours for c in row)

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
    for row in px:
        print("".join(row))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out")
    ap.add_argument("--ascii", action="store_true")
    args = ap.parse_args()

    icons = [
        (128, "Today", draw_today()),
        # All Unread wears the same picture as Next Unread: they are the same
        # idea, the not-yet-read, and one is the view of it and the other the
        # step to it.
        (129, "All Unread", draw_next_unread()),
        (130, "Starred", draw_star()),
        (131, "Starred Article", draw_star_small()),

        # The toolbar. The three pairs are the buttons that toggle. Each pair
        # wears the one picture for now — the second state's drawing has not
        # been designed yet — so a button's caption, not its picture, is what
        # says which way it will go next.
        (132, "Sidebar", draw_sidebar()),
        (133, "Refresh", draw_refresh()),
        (134, "Mark All as Read", draw_mark_all_read()),
        (135, "Mark All as Unread", draw_mark_all_read()),
        (136, "Hide Read Articles", draw_hide_read()),
        (137, "Show Read Articles", draw_hide_read()),
        (138, "Mark as Read", draw_mark_read()),
        (139, "Mark as Unread", draw_mark_read()),
        (140, "Next Unread", draw_next_unread()),
        (141, "Open in Browser", draw_browser()),
        (142, "New", draw_new()),
        (143, "Find", draw_find()),
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
