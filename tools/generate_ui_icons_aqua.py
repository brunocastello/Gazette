#!/usr/bin/env python3
"""
Gazette's small (16x16) icons in the Aqua-era style, and the resource file
that carries them.

These are the same drawings as the 32x32 toolbar PNGs of
tools/generate_aqua_toolbar_icons.py, which this file builds on (the shapes,
the gradients, the glossy jewels), but each is *drawn* at 16 pixels rather
than scaled down: fewer lines of text on a sheet, badges proportionally
larger, edges laid on whole pixels so that a rim is one crisp line. A
blind downscale of the 32s turns every rim into grey porridge.

They are the art of three places:
  * the Mac OS 9 toolbar (16 px icon over a caption), which keeps its
    layout and changes only its pictures. Its sidebar toggle and its
    browser button get Platinum-flavoured variants (a Platinum window with
    square corners and a close box; a globe with the classic arrow), because
    the Aqua window and the compass are OS X furniture;
  * the sidebar's smart rows (Today, All Unread, Starred), on OS 9 and OS X;
  * the mark on a starred headline.

Output is Resources/Gazette_ui_icons.r: for every ID, 'is32' + 's8mk' (the
24-bit colour and 8-bit alpha of the Mac OS 8.5 Icon Services small icon)
and 'ics8' + 'ics#' as the fallback for anything that cannot read those.

'is32' is RLE-packed per channel. The format (Apple Tech Note / the icns
documentation): the three planes red, green and blue follow one another, each
256 bytes unpacked, each packed on its own and the three packed runs simply
concatenated. A control byte below 128 says "copy the next n+1 bytes"; 128
or above says "repeat the next one byte n-125 times" (so 3 to 130). The
small size has no four-byte header; only 'it32', the 128 px one, does.
's8mk' is the raw 256 alpha bytes.

    python3 tools/generate_ui_icons_aqua.py --out Resources/Gazette_ui_icons.r
        [--proof PATH.html] [--sheets DIR]

The Windows build still draws its own art from tools/generate_ui_icons.py and
is untouched.
"""

import argparse
import base64
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import generate_aqua_toolbar_icons as A   # noqa: E402
from generate_aqua_toolbar_icons import (   # noqa: E402
    Icon, circle, ellipse, rrect, poly, stroke, arc_pts, rot, inter, union,
    inset, vgrad, radial, solid, ball, gloss_cap, star_pts, png_bytes, scaled_png)

S = 16


def shadowed():
    return Icon(S, 0.45)


def rect(x0, y0, x1, y1):
    return rrect(x0, y0, x1, y1, 0)


def hline(ic, x0, x1, row, col):
    ic.add(rect(x0, row, x1, row + 1), solid(*col))


def red16(ic, cx, cy, r):
    ball(ic, cx, cy, r, (255, 160, 140), (206, 28, 24), (140, 14, 14))


def green16(ic, cx, cy, r):
    ball(ic, cx, cy, r, (150, 226, 96), (28, 150, 36), (16, 104, 28))


def blue16(ic, cx, cy, r):
    """A chevron badge: too small at 16 px for a highlight cap and a glyph both."""
    sd = circle(cx, cy, r)
    ic.shape(sd, vgrad(cy - r, cy + r, (84, 132, 232), (18, 58, 168)), rim=(10, 36, 120), w=0.9)


def page16(ic, x, y, w, h, lines=4, fold=3):
    body = [(x, y), (x + w - fold, y), (x + w, y + fold), (x + w, y + h), (x, y + h)]
    ic.shape(poly(body), vgrad(y, y + h, (255, 255, 255), (240, 244, 250), (206, 214, 230)),
             rim=(112, 122, 142))
    ic.add(poly([(x + w - fold, y), (x + w, y + fold), (x + w - fold, y + fold)]),
           vgrad(y, y + fold, (255, 255, 255), (176, 186, 206)))
    row = y + fold + 1
    for i in range(lines):
        yy = row + 2 * i
        if yy > y + h - 2:
            break
        end = x + w - 2 - (2 if i == lines - 1 and i > 0 else 0)
        hline(ic, x + 2, end, yy, (62, 90, 152) if i == 0 else (128, 138, 162))


def check16(ic, cx, cy):
    pts = [(cx - 2.4, cy), (cx - 0.8, cy + 1.8), (cx + 2.6, cy - 1.8)]
    ic.add(stroke([(a, b + 0.6) for a, b in pts], 2.0), solid(0, 60, 20, 0.35))
    ic.add(stroke(pts, 1.9), solid(255, 255, 255))


def plus16(ic, cx, cy):
    ic.add(rect(cx - 2.5, cy - 1, cx + 2.5, cy + 1), solid(255, 255, 255))
    ic.add(rect(cx - 1, cy - 2.5, cx + 1, cy + 2.5), solid(255, 255, 255))


# --- Aqua 16s: the thirteen ---------------------------------------------------

def a_new():
    ic = shadowed()
    page16(ic, 2, 1, 10, 13)
    green16(ic, 11, 11, 4.6)
    plus16(ic, 11, 11)
    return ic


def window16(ic):
    sd = rrect(1, 2, 15, 14, 1.5)
    ic.shape(sd, vgrad(2, 14, (250, 250, 252), (230, 233, 240)), rim=(92, 100, 120))
    ic.add(inter(inset(sd, 1.0), rect(0, 0, 16, 5)), vgrad(2, 5, (240, 242, 246), (176, 182, 196)))
    hline(ic, 2, 14, 5, (100, 108, 126))
    return sd


def a_sidebar_hide():
    ic = shadowed()
    window16(ic)
    ic.add(rect(2, 6, 7, 13), vgrad(6, 13, (212, 223, 241), (168, 185, 213)))
    ic.add(rect(7, 6, 8, 13), solid(96, 108, 138))
    ic.add(rect(3, 7, 6, 9), vgrad(7, 9, (110, 160, 240), (46, 106, 214)))
    blue16(ic, 12, 10.5, 3.8)
    ic.add(stroke([(13.0, 8.8), (11, 10.5), (13.0, 12.2)], 1.5), solid(255, 255, 255))
    return ic


def a_sidebar_show():
    ic = shadowed()
    window16(ic)
    ic.add(rect(2, 6, 4, 13), vgrad(6, 13, (212, 223, 241), (168, 185, 213)))
    ic.add(rect(4, 6, 5, 13), solid(96, 108, 138))
    blue16(ic, 12, 10.5, 3.8)
    ic.add(stroke([(11.0, 8.8), (13, 10.5), (11.0, 12.2)], 1.5), solid(255, 255, 255))
    return ic


def a_refresh():
    ic = shadowed()
    cx, cy, r = 7.0, 9.6, 4.2
    a0 = -55
    body = stroke(arc_pts(cx, cy, r, a0 + 10, 235, 32), 2.6)
    a = math.radians(a0)
    px, py = cx + r * math.cos(a), cy + r * math.sin(a)
    dx, dy = math.sin(a), -math.cos(a)
    nx, ny = -dy, dx
    head = poly([(px + nx * 5.2, py + ny * 5.2), (px - nx * 5.2, py - ny * 5.2),
                 (px + dx * 5.6, py + dy * 5.6)])
    shp = union(body, head)
    ic.shape(shp, vgrad(2, 14, (122, 226, 214), (38, 160, 192), (18, 90, 176)), rim=(14, 76, 140), w=0.9)
    gloss_cap(ic, shp, cx, 3, 9, 6.0, 3.0, a0=0.7, w=1.0)
    return ic


def a_markall(unread):
    ic = shadowed()
    page16(ic, 1, 3, 9, 11, lines=2)
    page16(ic, 4, 1, 9, 11, lines=2)
    if unread:
        red16(ic, 11, 11, 4.0)
    else:
        green16(ic, 11, 11, 4.6)
        check16(ic, 11, 11)
    return ic


def eye16(ic, cx, cy):
    up = [(cx + 5.6 * math.cos(math.pi * i / 16), cy - 3.6 * math.sin(math.pi * i / 16)) for i in range(17)]
    dn = [(cx + 5.6 * math.cos(math.pi * i / 16), cy + 3.6 * math.sin(math.pi * i / 16)) for i in range(15, 0, -1)]
    al = poly(up + dn)
    ic.shape(al, vgrad(cy - 3.6, cy + 3.6, (255, 255, 255), (212, 220, 236)), rim=(60, 76, 106), w=0.9)
    ic.add(inter(inset(al, 0.9), circle(cx, cy, 2.7)), vgrad(cy - 2.7, cy + 2.7, (130, 202, 255), (30, 96, 196)))
    ic.add(circle(cx, cy, 1.2), solid(10, 20, 44))
    ic.add(rect(cx - 1.5, cy - 1.5, cx - 0.5, cy - 0.5), solid(255, 255, 255, 0.95))


def a_hideread(slash):
    ic = shadowed()
    page16(ic, 1, 1, 9, 11, lines=3)
    eye16(ic, 10, 11)
    if slash:
        ln = [(4.5, 7), (14, 14.5)]
        ic.add(stroke(ln, 3.4), solid(255, 255, 255, 0.55))
        ic.add(stroke(ln, 2.0), vgrad(7, 14, (240, 110, 96), (176, 28, 28)))
    return ic


def a_markread():
    ic = shadowed()
    page16(ic, 2, 1, 10, 13)
    green16(ic, 11, 11, 4.8)
    check16(ic, 11, 11)
    return ic


def a_markunread():
    ic = shadowed()
    page16(ic, 2, 1, 10, 13)
    red16(ic, 11, 11, 4.2)
    return ic


def star16(ic, cx, cy, ro, ri, rim_w=1.0):
    sd = poly(star_pts(cx, cy, ro, ri))
    ic.shape(sd, vgrad(cy - ro, cy + ro, (255, 240, 120), (255, 196, 20), (232, 140, 0)),
             rim=(176, 100, 0), w=rim_w)
    ic.add(inset(sd, 0.9), vgrad(cy, cy + ro, (255, 255, 255, 0.0), (255, 235, 150, 0.4)))
    gloss_cap(ic, sd, cx, cy - ro, cy, ro * 0.55, ro * 0.42, a0=0.8, w=0.9)


def a_star():
    ic = shadowed()
    star16(ic, 8, 8.6, 7.2, 3.1)
    return ic


def a_starsmall():
    ic = shadowed()
    star16(ic, 8, 8.3, 5.6, 2.4, 0.9)
    return ic


def a_nextunread():
    ic = shadowed()
    page16(ic, 1, 1, 10, 13)
    sd = poly([(6, 9), (10, 9), (10, 7), (15, 11), (10, 15), (10, 13), (6, 13)])
    ic.shape(sd, vgrad(7, 15, (150, 202, 255), (46, 112, 224), (22, 66, 176)), rim=(16, 52, 140), w=0.9)
    ic.add(inter(inset(sd, 0.9), rect(0, 0, 16, 11)), vgrad(7, 11, (255, 255, 255, 0.7), (255, 255, 255, 0.1)))
    return ic


LOBES16 = [(5.0, 4.5, 1.7), (6.2, 6.0, 1.8), (6.6, 7.8, 1.4), (7.2, 10.0, 1.5), (7.8, 11.8, 1.2),
           (11.4, 4.8, 1.2), (12.2, 6.2, 1.4), (12.0, 8.2, 1.4), (12.2, 10.0, 1.5), (11.4, 11.6, 1.1)]


def globe16(ic, cx, cy, r):
    sd = circle(cx, cy, r)
    ic.shape(sd, radial(cx, cy, r + 3, (120, 196, 255), (14, 66, 170), cx - 2.5, cy - 3.5),
             rim=(12, 44, 120), w=1.0)
    inner = inset(sd, 1.0)
    land = union(*[circle(lx + (cx - 8), ly + (cy - 8), lr) for lx, ly, lr in LOBES16])
    ic.add(inter(inner, land), radial(cx, cy, r + 2, (176, 226, 104), (28, 92, 52), cx - 2.5, cy - 3.5))
    gloss_cap(ic, sd, cx, cy - r, cy, r * 0.75, r * 0.5, a0=0.6, w=1.0)


def a_browser():
    ic = shadowed()
    globe16(ic, 8, 8, 6.5)
    cx = cy = 8
    nrth = rot([(cx, cy - 5.5), (cx + 1.5, cy), (cx - 1.5, cy)], 45, cx, cy)
    sth = rot([(cx, cy + 5.5), (cx + 1.5, cy), (cx - 1.5, cy)], 45, cx, cy)
    ic.add(poly(nrth), vgrad(2, 8, (255, 120, 100), (214, 30, 24)))
    ic.add(poly(sth), vgrad(8, 14, (250, 250, 252), (186, 196, 214)))
    return ic


# --- Mac OS 9 variants --------------------------------------------------------

def px_set(rows, x0, y0, ch):
    out = set()
    for j, row in enumerate(rows):
        for i, c in enumerate(row):
            if c == ch:
                out.add((x0 + i, y0 + j))
    return out


def pixels(cells):
    """A shape of whole pixels, hard-edged."""
    return lambda x, y: -1.0 if (int(math.floor(x)), int(math.floor(y))) in cells else 1.0


ARROW = ["X.......",
         "XX......",
         "XWX.....",
         "XWWX....",
         "XWWWX...",
         "XWWWWX..",
         "XWWWWWX.",
         "XWWWXXXX",
         "XWXWX...",
         "XX.XWX..",
         "X..XWX..",
         "....XX.."]


def o_browser():
    ic = shadowed()
    globe16(ic, 7.5, 7.5, 6.5)
    # the classic arrow cursor over the lower right: black, white inside
    x0, y0 = 8, 4
    blk, wht = px_set(ARROW, x0, y0, 'X'), px_set(ARROW, x0, y0, 'W')
    # a one-pixel white halo so that it reads over the blue
    halo = set()
    for (x, y) in blk | wht:
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                if 0 <= x + dx < S and 0 <= y + dy < S:
                    halo.add((x + dx, y + dy))
    ic.add(pixels(wht), solid(255, 255, 255))
    ic.add(pixels(blk), solid(0, 0, 0))
    return ic


# The Platinum build's own drawings of the sidebar toggle and the refresh ring
# (tools/generate_ui_icons.py: a window with a shaded list pane and a white
# pane, a ring open at the upper right with its head pointing right), redrawn
# with gradients, a gloss and a soft shadow but on the same grid and in the
# same composition.

def sidebar_window(ic, pane):
    ic.shape(rrect(1, 1, 15, 14, 1.0), vgrad(1, 14, (252, 252, 252), (226, 228, 234)),
             rim=(92, 96, 110))
    ic.add(rect(2, 2, 14, 4), vgrad(2, 4, (246, 247, 250), (186, 190, 200)))    # title bar
    ic.add(inter(rect(2, 2, 14, 3), rect(0, 0, 16, 3)), solid(255, 255, 255, 0.7))
    hline(ic, 2, 14, 4, (70, 76, 92))                                           # its rule
    ic.add(rect(2, 5, pane, 13), vgrad(5, 13, (244, 245, 248), (214, 218, 228)))
    hline(ic, 2, 14, 13, (190, 194, 204))
    ic.add(rect(pane, 5, pane + 1, 13), solid(70, 76, 92))                      # divider
    if pane > 4:
        for row in (6, 9, 12):
            ic.add(rect(3, row, pane - 2, row + 1), solid(150, 156, 170))


def o_sidebar_hide():
    ic = shadowed()
    sidebar_window(ic, 7)
    return ic


def o_sidebar_show():
    ic = shadowed()
    sidebar_window(ic, 4)
    return ic


def o_refresh():
    """The old ring: open between about one and four o'clock, a triangle head on its upper end."""
    ic = shadowed()
    cx, cy, r = 7.5, 9.0, 4.4
    ring = stroke(arc_pts(cx, cy, r, 20, 285, 40), 3.0)
    head = poly([(8.5, 1.5), (8.5, 7.5), (13.5, 4.5)])
    shp = union(ring, head)
    ic.shape(shp, vgrad(1, 15, (146, 214, 236), (44, 128, 206), (24, 78, 170)), rim=(14, 56, 130), w=0.9)
    ic.add(inter(inset(shp, 0.9), ellipse(7.5, 5.0, 8.0, 4.2)), vgrad(1, 8, (255, 255, 255, 0.55), (255, 255, 255, 0.0)))
    return ic


# --- Sidebar rows ---------------------------------------------------------------

def a_today():
    ic = shadowed()
    cx, cy = 8, 8
    for k in range(8):
        a = math.radians(k * 45)
        c, s = math.cos(a), math.sin(a)
        ln = [(cx + c * 4.8, cy + s * 4.8), (cx + c * 6.4, cy + s * 6.4)]
        ic.add(stroke(ln, 1.7), solid(224, 128, 10), True)
    sd = circle(cx, cy, 4.0)
    ic.shape(sd, vgrad(4, 12, (255, 240, 120), (255, 196, 20), (232, 140, 0)), rim=(176, 100, 0), w=0.9)
    gloss_cap(ic, sd, cx, 4, 8, 2.8, 1.8, a0=0.8, w=0.9)
    return ic


def a_allunread():
    ic = shadowed()
    page16(ic, 1, 3, 9, 11, lines=2)
    page16(ic, 3, 1, 9, 11, lines=2)
    red16(ic, 11, 11, 4.0)
    return ic


def a_clearsearch():
    """The glass with a red cross struck over its lens: Windows' Clear Search."""
    ic = a_find()
    for col, w in ((solid(255, 255, 255, 0.6), 4.0), (solid(190, 24, 24), 2.4)):
        ic.add(stroke([(4.2, 4.2), (8.8, 8.8)], w), col)
        ic.add(stroke([(8.8, 4.2), (4.2, 8.8)], w), col)
    return ic


def a_find():
    ic = shadowed()
    ic.add(stroke([(10, 10), (14, 14)], 3.0), solid(60, 44, 36), True)
    ic.add(stroke([(10, 10), (14, 14)], 1.6), solid(150, 112, 84))
    lens = circle(6.5, 6.5, 5.0)
    ic.shape(lens, vgrad(1.5, 11.5, (232, 244, 255), (150, 190, 236)), rim=(70, 82, 110), w=1.2)
    gloss_cap(ic, lens, 6.5, 1.5, 6.5, 3.4, 2.6, a0=0.8, w=1.4)
    return ic


# (resource id, name, builder). 260, 261, 269 and 272 are the Mac OS 9 / Windows
# art: the Aqua window and ring and the compass are OS X furniture.
RESOURCES = [
    (256, "Today", a_today),
    (257, "All Unread", a_allunread),
    (258, "Starred", a_star),
    (259, "Starred Article", a_starsmall),
    (260, "Hide Sidebar", o_sidebar_hide),
    (261, "Refresh", o_refresh),
    (262, "Mark All as Read", lambda: a_markall(False)),
    (263, "Mark All as Unread", lambda: a_markall(True)),
    (264, "Hide Read Articles", lambda: a_hideread(True)),
    (265, "Show Read Articles", lambda: a_hideread(False)),
    (266, "Mark as Read", a_markread),
    (267, "Mark as Unread", a_markunread),
    (268, "Next Unread", a_nextunread),
    (269, "Open in Browser", o_browser),
    (270, "New", a_new),
    (271, "Find", a_find),
    (272, "Show Sidebar", o_sidebar_show),
]

# The 16 px twins of the 32 px toolbar PNGs, for the proof sheet.
AQUA16 = [
    ("new", a_new), ("sidebar-hide", a_sidebar_hide), ("sidebar-show", a_sidebar_show),
    ("refresh", a_refresh), ("markall-read", lambda: a_markall(False)),
    ("markall-unread", lambda: a_markall(True)), ("hideread", lambda: a_hideread(True)),
    ("showread", lambda: a_hideread(False)), ("mark-read", a_markread),
    ("mark-unread", a_markunread), ("star", a_star), ("nextunread", a_nextunread),
    ("browser", a_browser),
]
OS9 = [("sidebar-hide-os9", o_sidebar_hide), ("sidebar-show-os9", o_sidebar_show),
       ("refresh-os9", o_refresh),
       ("browser-os9", o_browser)]
SIDEBAR = [("today", a_today), ("allunread", a_allunread), ("starred", a_star),
           ("starredsmall", a_starsmall), ("find", a_find)]


# --- Icon Services formats -------------------------------------------------------

def rgba_bytes(buf):
    out = []
    for row in buf:
        for r, g, b, a in row:
            out.append((max(0, min(255, int(round(r)))), max(0, min(255, int(round(g)))),
                        max(0, min(255, int(round(b)))), max(0, min(255, int(round(a * 255))))))
    return out


def rle_pack(plane):
    """Pack one 256-byte channel the 'is32' way."""
    out = bytearray()
    i, n = 0, len(plane)
    lit = bytearray()

    def flush():
        while lit:
            chunk = lit[:128]
            del lit[:128]
            out.append(len(chunk) - 1)
            out.extend(chunk)

    while i < n:
        j = i
        while j < n and plane[j] == plane[i] and j - i < 130:
            j += 1
        run = j - i
        if run >= 3:
            flush()
            out.append(run + 125)
            out.append(plane[i])
            i = j
        else:
            lit.append(plane[i])
            i += 1
    flush()
    return bytes(out)


def rle_unpack(data, count):
    out = bytearray()
    i = 0
    while len(out) < count:
        c = data[i]
        i += 1
        if c < 128:
            out.extend(data[i:i + c + 1])
            i += c + 1
        else:
            out.extend(bytes([data[i]]) * (c - 125))
            i += 1
    return bytes(out), i


def is32_data(px):
    planes = [bytes(p[k] if p[3] else 0 for p in px) for k in range(3)]
    return b"".join(rle_pack(pl) for pl in planes)


def check_is32(data, px):
    """Round trip: what a reader would decode must equal what we meant."""
    pos = 0
    for k in range(3):
        plane, used = rle_unpack(data[pos:], 256)
        assert len(plane) == 256
        assert plane == bytes(p[k] if p[3] else 0 for p in px), "is32 plane %d" % k
        pos += used
    assert pos == len(data)


LEVELS = [255, 204, 153, 102, 51, 0]
RAMP = [0xEE, 0xDD, 0xBB, 0xAA, 0x88, 0x77, 0x55, 0x44, 0x22, 0x11]


def system_palette():
    pal = []
    for r in LEVELS:
        for g in LEVELS:
            for b in LEVELS:
                pal.append((r, g, b))
    pal = pal[:215]
    pal += [(v, 0, 0) for v in RAMP] + [(0, v, 0) for v in RAMP]
    pal += [(0, 0, v) for v in RAMP] + [(v, v, v) for v in RAMP] + [(0, 0, 0)]
    assert len(pal) == 256
    return pal


PAL = system_palette()


def nearest(c):
    best, bi = 1e18, 0
    for i, p in enumerate(PAL):
        d = (p[0] - c[0]) ** 2 + (p[1] - c[1]) ** 2 + (p[2] - c[2]) ** 2
        if d < best:
            best, bi = d, i
    return bi


def ics8_data(px):
    return bytes(nearest(p[:3]) if p[3] >= 128 else 0 for p in px)


def ics_mask_data(px):
    def pack(pred):
        out = bytearray()
        for y in range(16):
            for byte in range(2):
                v = 0
                for bit in range(8):
                    if pred(px[y * 16 + byte * 8 + bit]):
                        v |= 0x80 >> bit
                out.append(v)
        return bytes(out)
    ink = pack(lambda p: p[3] >= 128 and (0.3 * p[0] + 0.59 * p[1] + 0.11 * p[2]) < 128)
    mask = pack(lambda p: p[3] >= 128)
    return ink + mask


def hexrows(data, per=16):
    return "\n".join('\t$"' + " ".join("%02X" % b for b in data[i:i + per]) + '"'
                     for i in range(0, len(data), per))


HEADER = """/*
 * Gazette_ui_icons.r - the sidebar's and the toolbar's own small icons.
 *
 * DO NOT EDIT. Regenerate with:
 *   python3 tools/generate_ui_icons_aqua.py --out Resources/Gazette_ui_icons.r
 *
 * 16x16 families, drawn at 16 pixels in the Aqua-era style (the Mac OS 9
 * toolbar's sidebar toggle and browser button are Platinum variants). Each
 * ID carries four members:
 *   'is32'  24-bit colour, RLE-packed per channel (no header at this size)
 *   's8mk'  the 8-bit alpha mask that goes with it
 *   'ics8'  the same picture quantised to the system palette
 *   'ics#'  1-bit picture and mask
 * The first two are what Icon Services draws with, with real alpha, through
 * RegisterIconRefFromIconFamily; the others are the fallback, and the whole
 * of what a suite can read if that fails (OwnIcon in platinum_window.c).
 *
 * Written as raw data blocks rather than Rez icon templates for the reason
 * Gazette_icon.r records: Rez runs against whichever RIncludes the toolchain
 * has linked, and a resource file with no includes does not care which.
 *
 * IDs are the kIcon* constants in src/ui/platinum_window.c.
 */
"""


def family_rez(rid, name, buf):
    px = rgba_bytes(buf)
    is32 = is32_data(px)
    check_is32(is32, px)
    s8mk = bytes(p[3] for p in px)
    blocks = [("is32", is32), ("s8mk", s8mk), ("ics8", ics8_data(px)), ("ics#", ics_mask_data(px))]
    return "".join("data '%s' (%d, \"%s\", purgeable) {\n%s\n};\n\n" % (t, rid, name, hexrows(d))
                   for t, d in blocks)


# --- Proof -------------------------------------------------------------------------

def matted(buf, bg, cutoff):
    """What the Windows window makes of an icon: mixed with `bg`, and anything
    under `cutoff` (of 255) left out. See BuildIconList in gazette_win_window.c."""
    out = []
    for row in buf:
        o = []
        for r, g, b, a in row:
            a8 = int(round(a * 255))
            if a8 < cutoff:
                o.append((0, 0, 0, 0.0))
            else:
                k = a8 / 255.0
                o.append((r * k + bg[0] * (1 - k), g * k + bg[1] * (1 - k), b * k + bg[2] * (1 - k), 1.0))
        out.append(o)
    return out


def uri(buf):
    return "data:image/png;base64," + base64.b64encode(png_bytes(buf)).decode()


def proof(path, set32, small):
    def cell32(name, label, buf):
        u = uri(buf)
        return ('<div class="cell"><div class="row"><img src="%s" width="32" height="32">'
                '<img class="px" src="%s" width="64" height="64"></div>'
                '<div class="tb"><img src="%s" width="32" height="32"><span>%s</span></div>'
                '<code>%s</code></div>' % (u, u, u, label, name))

    def cell16(name, buf):
        u = uri(buf)
        return ('<div class="cell16"><div class="row"><img src="%s" width="16" height="16">'
                '<img class="px" src="%s" width="64" height="64"></div>'
                '<div class="row plat"><img src="%s" width="16" height="16"><span>%s</span></div>'
                '<code>%s</code></div>' % (u, u, u, name.split("-")[0].title(), name))

    html = ("""<!doctype html><meta charset="utf-8"><title>Gazette icons: Aqua pass</title>
<style>
body{font:13px "Lucida Grande",sans-serif;margin:24px;background:#fff;color:#222}
.grid{display:flex;flex-wrap:wrap;gap:16px}
.cell{width:150px}.cell16{width:130px}
.row{display:flex;gap:12px;align-items:center;padding:6px;border:1px solid #ddd}
.plat{background:#ddd;margin-top:4px;gap:6px;font:11px Geneva,sans-serif}
.px{image-rendering:pixelated}
.tb{margin-top:6px;height:62px;background:linear-gradient(rgb(232,232,232),rgb(196,196,196));border:1px solid #9a9a9a;
 display:flex;flex-direction:column;align-items:center;justify-content:center;gap:3px}
.tb span,.bar span{font:11px "Lucida Grande",sans-serif;color:#000}
code{font-size:11px;color:#666}
.bar{display:flex;gap:14px;padding:8px 14px;background:linear-gradient(rgb(232,232,232),rgb(196,196,196));border:1px solid #9a9a9a;margin:12px 0;flex-wrap:wrap}
.bar div{display:flex;flex-direction:column;align-items:center;gap:3px}
</style>
<h2>Gazette icons, Aqua pass</h2>
<h3>32 x 32 (OS X toolbar PNGs; unread badges now red)</h3><div class="grid">"""
            + "".join(cell32(n, l, b) for n, l, b in set32)
            + """</div><div class="bar">""" + "".join(
                '<div><img src="%s" width="32" height="32"><span>%s</span></div>' % (uri(b), l)
                for n, l, b in set32)
            + """</div>
<h3>16 x 16, the same thirteen drawn at 16 (1x, 4x nearest-neighbour, and on Platinum grey)</h3>
<div class="grid">""" + "".join(cell16(n, b) for n, b in small["aqua"])
            + """</div>
<h3>Mac OS 9 variants (16 x 16): Platinum window, globe with the classic arrow</h3>
<div class="grid">""" + "".join(cell16(n, b) for n, b in small["os9"])
            + """</div>
<h3>Sidebar rows and starred-headline mark (16 x 16, OS 9 and OS X); Find (the search field's glass)</h3>
<div class="grid">""" + "".join(cell16(n, b) for n, b in small["side"])
            + """</div>
<h3>Redone from the Platinum drawings: sidebar pair and refresh, 16 x 16 (1x and 4x)</h3>
<div class="grid">""" + "".join(cell16(n, b) for n, b in small["os9"] if n != "browser-os9")
            + """</div>
<h3>The Windows toolbar, as BuildIconList makes it (alpha mixed with the button face, fainter than 48/255 left out): classic grey 192,192,192 and XP beige 236,233,216; 1x and 3x</h3>"""
            + "".join(
                '<div class="bar" style="background:rgb(%d,%d,%d);border-color:#808080">%s</div>'
                '<div class="bar" style="background:rgb(%d,%d,%d);border-color:#808080;gap:8px">%s</div>' % (
                    bg + ("".join('<div><img src="%s" width="16" height="16"><span style="font:11px Tahoma,sans-serif">%s</span></div>' % (uri(matted(b, bg, 48)), l) for l, b in small["win"]),)
                    + bg + ("".join('<img class="px" src="%s" width="48" height="48">' % uri(matted(b, bg, 48)) for l, b in small["win"]),))
                for bg in ((192, 192, 192), (236, 233, 216)))
            + """
<h3>The Windows lists (sidebar, headlines) on window white, cutoff 128</h3>
<div class="bar" style="background:#fff;border-color:#808080">""" + "".join(
                '<img src="%s" width="16" height="16">' % uri(matted(b, (255, 255, 255), 128)) for l, b in small["win"])
            + """</div>
<h3>As the Mac OS 9 toolbar will show them (16 px over a caption, on Platinum grey)</h3>
<div class="bar" style="background:#dddddd;border-color:#888">""" + "".join(
                '<div><img src="%s" width="16" height="16"><span style="font:10px Geneva,sans-serif">%s</span></div>' % (uri(b), l)
                for l, b in small["os9bar"]) + "</div>")
    with open(path, "w") as f:
        f.write(html)


def sheet(path, bufs, k, cols, bg=(228, 228, 228)):
    n = len(bufs[0][0])
    rows = (len(bufs) + cols - 1) // cols
    img = [[(bg[0], bg[1], bg[2], 1.0)] * (cols * n) for _ in range(rows * n)]
    for i, b in enumerate(bufs):
        ox, oy = (i % cols) * n, (i // cols) * n
        for y in range(n):
            for x in range(n):
                img[oy + y][ox + x] = b[y][x]
    with open(path, "wb") as f:
        f.write(scaled_png(img, k, bg))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", help="write the Rez source here")
    ap.add_argument("--proof", help="write the HTML proof sheet here")
    ap.add_argument("--sheets", help="directory for sheet.png and sheet16.png")
    ap.add_argument("--png16", help="write every 16 px picture as NAME.png here")
    a = ap.parse_args()

    if a.out:
        text = HEADER + "\n" + "".join(family_rez(rid, nm, fn().render()) for rid, nm, fn in RESOURCES)
        with open(a.out, "w") as f:
            f.write(text.rstrip("\n") + "\n")

    if a.png16:
        os.makedirs(a.png16, exist_ok=True)
        for name, fn in AQUA16 + OS9 + SIDEBAR + [("clearsearch", a_clearsearch)]:
            with open(os.path.join(a.png16, name + ".png"), "wb") as f:
                f.write(png_bytes(fn().render()))

    if a.proof or a.sheets:
        set32 = [(n, l, fn().render()) for n, fn, l in A.ICONS]
        small = {"aqua": [(n, fn().render()) for n, fn in AQUA16],
                 "os9": [(n, fn().render()) for n, fn in OS9],
                 "side": [(n, fn().render()) for n, fn in SIDEBAR]}
        # the OS 9 toolbar, in the order and wording it has
        order = [(261, "Refresh"), (270, "New"), (260, "Hide Sidebar"), (272, "Show Sidebar"),
                 (262, "Mark All Read"), (263, "Mark All Unread"), (264, "Hide Read"),
                 (265, "Show Read"), (266, "Mark Read"), (267, "Mark Unread"), (258, "Star"),
                 (268, "Next Unread"), (269, "Browser")]
        byid = {rid: fn for rid, nm, fn in RESOURCES}
        small["os9bar"] = [(l, byid[rid]().render()) for rid, l in order]
        wl = [(nm, fn().render()) for rid, nm, fn in RESOURCES if rid <= 271] + [("Clear Search", a_clearsearch().render())]
        small["win"] = wl
        if a.proof:
            proof(a.proof, set32, small)
        if a.sheets:
            os.makedirs(a.sheets, exist_ok=True)
            sheet(os.path.join(a.sheets, "sheet.png"), [b for _, _, b in set32], 6, 5)
            all16 = [b for _, b in small["aqua"] + small["os9"] + small["side"]]
            sheet(os.path.join(a.sheets, "sheet16.png"), all16, 10, 7)


if __name__ == "__main__":
    main()
