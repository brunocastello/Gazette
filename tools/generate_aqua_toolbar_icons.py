#!/usr/bin/env python3
"""
Draw Gazette's toolbar icons for the Aqua (osx-legacy) build: twelve 32x32
RGBA PNGs in the idiom of a Mac OS X Tiger toolbar. Lit from above and the
front, a soft gradient on every surface, a thin darker rim, a glossy highlight
on the glassy things (badges, the star, the globe), and a soft drop shadow
under the whole icon. Saturated, but not neon.

The subjects are the twelve the Platinum toolbar already has (see
tools/generate_ui_icons.py) and use the same metaphors: the newspaper sheet
that most of them qualify, a badge in its corner, a ring arrow, a star, a
globe. Nothing here is traced from Apple's art.

Pure Python 3, standard library only. Shapes are signed-distance functions in
pixel units, so edges are anti-aliased analytically (coverage = 0.5 - distance)
and fills are evaluated per pixel, which is why a gradient stays smooth at
32x32. PNGs are written with zlib and struct.

    python3 tools/generate_aqua_toolbar_icons.py [--out Resources/osx/toolbar]
                                                 [--proof PATH.html]
                                                 [--enlarged DIR]
"""

import argparse
import base64
import math
import os
import struct
import zlib

N = 32

# --- Signed distance shapes (negative inside) -------------------------------

def circle(cx, cy, r):
    return lambda x, y: math.hypot(x - cx, y - cy) - r


def ellipse(cx, cy, rx, ry):
    m = min(rx, ry)
    return lambda x, y: (math.hypot((x - cx) / rx, (y - cy) / ry) - 1.0) * m


def rrect(x0, y0, x1, y1, r):
    cx, cy = (x0 + x1) / 2.0, (y0 + y1) / 2.0
    hw, hh = (x1 - x0) / 2.0 - r, (y1 - y0) / 2.0 - r

    def sd(x, y):
        qx, qy = abs(x - cx) - hw, abs(y - cy) - hh
        return (math.hypot(max(qx, 0), max(qy, 0)) + min(max(qx, qy), 0)) - r
    return sd


def _segdist(px, py, ax, ay, bx, by):
    dx, dy = bx - ax, by - ay
    l2 = dx * dx + dy * dy
    t = 0.0 if l2 == 0 else max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / l2))
    return math.hypot(px - ax - t * dx, py - ay - t * dy)


def poly(pts):
    n = len(pts)

    def sd(x, y):
        d = 1e9
        inside = False
        for i in range(n):
            ax, ay = pts[i]
            bx, by = pts[(i + 1) % n]
            d = min(d, _segdist(x, y, ax, ay, bx, by))
            if (ay > y) != (by > y) and x < (bx - ax) * (y - ay) / (by - ay) + ax:
                inside = not inside
        return -d if inside else d
    return sd


def stroke(pts, w):
    """An open polyline with round caps and joins."""
    h = w / 2.0

    def sd(x, y):
        d = 1e9
        for i in range(len(pts) - 1):
            d = min(d, _segdist(x, y, pts[i][0], pts[i][1], pts[i + 1][0], pts[i + 1][1]))
        return d - h
    return sd


def arc_pts(cx, cy, r, a0, a1, steps=40):
    return [(cx + r * math.cos(math.radians(a0 + (a1 - a0) * i / steps)),
             cy + r * math.sin(math.radians(a0 + (a1 - a0) * i / steps)))
            for i in range(steps + 1)]


def rot(pts, deg, cx, cy):
    c, s = math.cos(math.radians(deg)), math.sin(math.radians(deg))
    return [(cx + (x - cx) * c - (y - cy) * s, cy + (x - cx) * s + (y - cy) * c)
            for x, y in pts]


def inter(a, b):
    return lambda x, y: max(a(x, y), b(x, y))


def union(*s):
    return lambda x, y: min(f(x, y) for f in s)


def inset(a, d):
    return lambda x, y: a(x, y) + d


# --- Fills (return r, g, b, a: colour 0..255, alpha 0..1) ---------------------

def vgrad(y0, y1, *stops):
    """Vertical gradient; stops are colours (r,g,b) or (r,g,b,a), evenly spaced."""
    st = [tuple(s) + ((1.0,) if len(s) == 3 else ()) for s in stops]

    def f(x, y):
        t = 0.0 if y1 == y0 else max(0.0, min(1.0, (y - y0) / (y1 - y0)))
        k = t * (len(st) - 1)
        i = min(int(k), len(st) - 2)
        u = k - i
        a, b = st[i], st[i + 1]
        return tuple(a[j] + (b[j] - a[j]) * u for j in range(4))
    return f


def radial(cx, cy, r, inner, outer, fx, fy):
    """Radial gradient whose light sits at the focus (fx, fy)."""
    def f(x, y):
        t = max(0.0, min(1.0, math.hypot(x - fx, y - fy) / r))
        return tuple(inner[j] + (outer[j] - inner[j]) * t for j in range(3)) + (1.0,)
    return f


def solid(r, g, b, a=1.0):
    return lambda x, y: (r, g, b, a)


# --- Canvas -----------------------------------------------------------------

class Icon:
    def __init__(self, n=N, shadow_alpha=0.6):
        self.layers = []
        self.n = n
        self.shadow_alpha = shadow_alpha

    def add(self, sd, fill, shadow=False):
        self.layers.append((sd, fill, shadow))

    def shape(self, sd, fill, rim=None, w=1.0, shadow=True):
        """A filled shape with a darker rim line drawn just inside its edge."""
        if rim is not None:
            self.add(sd, solid(*rim), shadow)
            self.add(inset(sd, w), fill)
        else:
            self.add(sd, fill, shadow)

    def render(self):
        # Drop shadow: union coverage of every shadow-casting layer, blurred.
        mask = [[0.0] * self.n for _ in range(self.n)]
        for sd, fill, sh in self.layers:
            if not sh:
                continue
            for y in range(self.n):
                for x in range(self.n):
                    c = max(0.0, min(1.0, 0.5 - sd(x + 0.5, y + 0.5)))
                    if c > mask[y][x]:
                        mask[y][x] = c
        for _ in range(2):
            mask = blur(mask, self.n)
        buf = [[(20.0, 24.0, 50.0, mask[y - 1][x] * self.shadow_alpha if y > 0 else 0.0)
                for x in range(self.n)] for y in range(self.n)]
        for sd, fill, sh in self.layers:
            for y in range(self.n):
                for x in range(self.n):
                    px, py = x + 0.5, y + 0.5
                    cov = 0.5 - sd(px, py)
                    if cov <= 0:
                        continue
                    r, g, b, a = fill(px, py)
                    a = a * min(1.0, cov)
                    if a <= 0:
                        continue
                    dr, dg, db, da = buf[y][x]
                    oa = a + da * (1 - a)
                    buf[y][x] = ((r * a + dr * da * (1 - a)) / oa,
                                 (g * a + dg * da * (1 - a)) / oa,
                                 (b * a + db * da * (1 - a)) / oa, oa)
        return buf


def blur(m, n):
    out = [[0.0] * n for _ in range(n)]
    for y in range(n):
        for x in range(n):
            s = 0.0
            for dy in (-1, 0, 1):
                for dx in (-1, 0, 1):
                    yy, xx = y + dy, x + dx
                    if 0 <= yy < n and 0 <= xx < n:
                        s += m[yy][xx] * (2 if dx == 0 and dy == 0 else 1)
            out[y][x] = s / 10.0
    return out


def png_bytes(buf):
    raw = bytearray()
    for row in buf:
        raw.append(0)
        for r, g, b, a in row:
            raw += bytes((max(0, min(255, int(round(r)))), max(0, min(255, int(round(g)))),
                          max(0, min(255, int(round(b)))), max(0, min(255, int(round(a * 255))))))

    def chunk(t, d):
        c = struct.pack(">I", len(d)) + t + d
        return c + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    return (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", struct.pack(">IIBBBBB", len(buf[0]), len(buf), 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b""))


def scaled_png(buf, k, bg=(228, 228, 228)):
    """Nearest-neighbour enlargement over a flat grey, for looking at pixels."""
    def over(p):
        r, g, b, a = p
        return (r * a + bg[0] * (1 - a), g * a + bg[1] * (1 - a), b * a + bg[2] * (1 - a), 1.0)
    big = [[over(buf[y // k][x // k]) for x in range(len(buf[0]) * k)] for y in range(len(buf) * k)]
    return png_bytes(big)


# --- Pieces -----------------------------------------------------------------

def page(ic, x, y, w, h, deg=0.0, lines=4, fold=5.0):
    """A sheet of newsprint, folded at the top right, lines of text on it."""
    cx, cy = x + w / 2.0, y + h / 2.0
    body = [(x, y), (x + w - fold, y), (x + w, y + fold), (x + w, y + h), (x, y + h)]
    foldp = [(x + w - fold, y), (x + w, y + fold), (x + w - fold, y + fold)]
    body, foldp = rot(body, deg, cx, cy), rot(foldp, deg, cx, cy)
    ic.shape(poly(body), vgrad(y, y + h, (255, 255, 255), (242, 245, 250), (208, 216, 230)),
             rim=(112, 122, 142))
    ic.add(poly(foldp), vgrad(y, y + fold, (255, 255, 255), (178, 188, 206)))
    ly = y + fold + 1.2 if lines > 3 else y + 3.6
    for i in range(lines):
        yy = ly + i * 3.0
        if yy > y + h - 3:
            break
        ex = x + w - 2.5 - (3.5 if i == lines - 1 else 0)
        seg = rot([(x + 2.5, yy), (ex, yy)], deg, cx, cy)
        col = (70, 94, 150, 0.9) if i == 0 else (128, 138, 160, 0.8)
        ic.add(stroke(seg, 1.7 if i == 0 else 1.1), solid(*col))


def gloss_cap(ic, sd, cx, top, bot, rx, ry, a0=0.85, w=1.2):
    """The Aqua highlight: a white cap on the upper part of a shape, fading down."""
    cap = inter(inset(sd, w), ellipse(cx, top + ry, rx, ry))
    ic.add(cap, vgrad(top, bot, (255, 255, 255, a0), (255, 255, 255, 0.1)))


def ball(ic, cx, cy, r, top, bot, rim):
    """A glossy Aqua jewel: dark rim, gradient body, light pooled below, white cap."""
    sd = circle(cx, cy, r)
    ic.shape(sd, vgrad(cy - r, cy + r, top, bot), rim=rim, w=1.0)
    ic.add(inset(sd, 1.0), vgrad(cy, cy + r, (255, 255, 255, 0.0), (255, 255, 255, 0.28)))
    gloss_cap(ic, sd, cx, cy - r, cy, r * 0.78, r * 0.52)


def green_ball(ic, cx, cy, r):
    ball(ic, cx, cy, r, (150, 226, 96), (28, 150, 36), (16, 104, 28))


def blue_ball(ic, cx, cy, r):
    ball(ic, cx, cy, r, (150, 196, 255), (30, 98, 214), (20, 56, 150))


def red_ball(ic, cx, cy, r):
    ball(ic, cx, cy, r, (255, 160, 140), (206, 28, 24), (140, 14, 14))


def check_mark(ic, cx, cy, s=1.0):
    pts = [(cx - 3.2 * s, cy + 0.2 * s), (cx - 1.0 * s, cy + 2.6 * s), (cx + 3.4 * s, cy - 2.8 * s)]
    ic.add(stroke([(a, b + 0.7) for a, b in pts], 2.8 * s), solid(0, 60, 20, 0.35))
    ic.add(stroke(pts, 2.6 * s), solid(255, 255, 255))


def plus_mark(ic, cx, cy):
    for dx, dy, col in ((0, 0.7, solid(0, 60, 20, 0.35)), (0, 0, solid(255, 255, 255))):
        w = 2.8 if dy else 2.6
        ic.add(stroke([(cx - 3, cy + dy), (cx + 3, cy + dy)], w), col)
        ic.add(stroke([(cx, cy - 3 + dy), (cx, cy + 3 + dy)], w), col)


def star_pts(cx, cy, ro, ri, deg=-90.0):
    p = []
    for i in range(10):
        r = ro if i % 2 == 0 else ri
        a = math.radians(deg + i * 36)
        p.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    return p


# --- The twelve ---------------------------------------------------------------

def icon_new():
    ic = Icon()
    page(ic, 5, 3, 18, 23, lines=4)
    green_ball(ic, 22.5, 22.5, 7)
    plus_mark(ic, 22.5, 22.5)
    s = [(26.5, 2.5), (27.4, 4.6), (29.5, 5.5), (27.4, 6.4), (26.5, 8.5), (25.6, 6.4), (23.5, 5.5), (25.6, 4.6)]
    ic.shape(poly(s), vgrad(2.5, 8.5, (255, 250, 180), (255, 190, 40)), rim=(214, 140, 10), w=0.6, shadow=False)
    return ic


def window(ic):
    sd = rrect(3, 5, 29, 27, 2.6)
    ic.shape(sd, vgrad(5, 27, (250, 250, 252), (230, 233, 240)), rim=(92, 100, 120))
    tb = inter(inset(sd, 1.0), rrect(0, 0, 32, 10.5, 0))
    ic.add(tb, vgrad(5, 10.5, (240, 242, 246), (172, 178, 192)))        # brushed title bar
    ic.add(stroke([(4, 10.5), (28, 10.5)], 1.0), solid(100, 108, 126, 0.9))
    for i, c in enumerate(((236, 96, 84), (240, 188, 62), (110, 200, 84))):
        ic.add(circle(7.0 + i * 3.4, 7.8, 1.3), vgrad(6.5, 9, c, tuple(v * 0.72 for v in c)))


def source_pane(ic, x1):
    ic.add(rrect(4, 11, x1, 26, 0), vgrad(11, 26, (212, 223, 241), (168, 185, 213)))
    ic.add(stroke([(x1, 11), (x1, 26)], 1.0), solid(96, 108, 138, 0.9))


def icon_sidebar_hide():
    ic = Icon()
    window(ic)
    source_pane(ic, 13.2)
    ic.add(rrect(5, 13, 12, 16.2, 1.0), vgrad(13, 16.2, (110, 160, 240), (46, 106, 214)))
    for yy in (19.5, 22.5):
        ic.add(stroke([(5.5, yy), (11, yy)], 1.4), solid(90, 104, 134, 0.8))
    for yy in (14.5, 18.5):
        ic.add(stroke([(16, yy), (26, yy)], 1.2), solid(150, 158, 176, 0.8))
    blue_ball(ic, 24, 22.5, 5.4)                     # chevron: the pane goes left
    ic.add(stroke([(25.3, 19.9), (22.4, 22.5), (25.3, 25.1)], 2.2), solid(255, 255, 255))
    return ic


def icon_sidebar_show():
    ic = Icon()
    window(ic)
    source_pane(ic, 7.2)
    for yy in (14.5, 18.5):
        ic.add(stroke([(10, yy), (26, yy)], 1.2), solid(150, 158, 176, 0.8))
    blue_ball(ic, 24, 22.5, 5.4)                     # chevron: the pane comes back
    ic.add(stroke([(22.7, 19.9), (25.6, 22.5), (22.7, 25.1)], 2.2), solid(255, 255, 255))
    return ic


def icon_refresh():
    ic = Icon()
    cx, cy, r = 15.5, 17.0, 8.6
    a0 = -55                                          # where the arrow head sits
    body = stroke(arc_pts(cx, cy, r, a0 + 8, 235, 48), 4.4)
    a = math.radians(a0)
    px, py = cx + r * math.cos(a), cy + r * math.sin(a)
    dx, dy = math.sin(a), -math.cos(a)                # direction of travel at the head
    nx, ny = -dy, dx
    head = poly([(px + nx * 6.8, py + ny * 6.8), (px - nx * 6.8, py - ny * 6.8),
                 (px + dx * 8.0, py + dy * 8.0)])
    shp = union(body, head)
    ic.shape(shp, vgrad(6, 27, (122, 226, 214), (38, 160, 192), (18, 90, 176)), rim=(14, 76, 140))
    gloss_cap(ic, shp, cx, 6, 16, 12, 5.8, a0=0.75)
    return ic


def stack(ic):
    page(ic, 4, 7, 15, 19, deg=-9, lines=3, fold=4)
    page(ic, 8, 4.5, 16, 20, deg=3, lines=3, fold=4)


def icon_markall_read():
    ic = Icon()
    stack(ic)
    green_ball(ic, 22.5, 22.5, 7)
    check_mark(ic, 22.5, 22.5)
    return ic


def icon_markall_unread():
    ic = Icon()
    stack(ic)
    red_ball(ic, 22.5, 22.5, 6.2)
    return ic


def eye(ic, cx, cy, is_open):
    if is_open:
        up = [(cx + 10.5 * math.cos(math.pi * i / 20), cy - 6.5 * math.sin(math.pi * i / 20)) for i in range(21)]
        dn = [(cx + 10.5 * math.cos(math.pi * i / 20), cy + 6.5 * math.sin(math.pi * i / 20)) for i in range(19, 0, -1)]
        almond = poly(up + dn)
        ic.shape(almond, vgrad(cy - 6.5, cy + 6.5, (255, 255, 255), (212, 220, 236)), rim=(60, 76, 106))
        ic.add(inter(inset(almond, 1.0), circle(cx, cy, 4.7)),
               vgrad(cy - 4.7, cy + 4.7, (130, 202, 255), (30, 96, 196)))
        ic.add(circle(cx, cy, 2.2), solid(10, 20, 44))
        ic.add(circle(cx - 1.4, cy - 1.7, 1.1), solid(255, 255, 255, 0.95))


def icon_hideread():
    ic = Icon()
    page(ic, 4, 2, 17, 21, lines=4)
    eye(ic, 19.5, 21.0, True)
    # the slash: a pale halo under a red bar, so it reads over the eye
    ln = [(11.0, 15.0), (28.0, 27.0)]
    ic.add(stroke(ln, 4.4), solid(255, 255, 255, 0.55))
    ic.add(stroke(ln, 2.8),vgrad(14, 28, (240, 110, 96), (176, 28, 28)))
    return ic


def icon_showread():
    ic = Icon()
    page(ic, 4, 2, 17, 21, lines=4)
    eye(ic, 19.5, 21.0, True)
    return ic


def icon_mark_read():
    ic = Icon()
    page(ic, 5, 3, 18, 23, lines=4)
    green_ball(ic, 22, 22, 7.6)
    check_mark(ic, 22, 22, 1.1)
    return ic


def icon_mark_unread():
    ic = Icon()
    page(ic, 5, 3, 18, 23, lines=4)
    red_ball(ic, 22, 22, 6.2)
    return ic


def icon_star():
    ic = Icon()
    sd = poly(star_pts(16, 17.2, 13.8, 5.8))
    ic.shape(sd, vgrad(3, 29, (255, 240, 120), (255, 196, 20), (232, 140, 0)), rim=(176, 100, 0), w=1.1)
    ic.add(inset(sd, 1.0), vgrad(17, 29, (255, 255, 255, 0.0), (255, 235, 150, 0.45)))
    gloss_cap(ic, sd, 16, 3, 16, 9.5, 7.2)
    return ic


def icon_nextunread():
    ic = Icon()
    page(ic, 4, 3, 18, 23, lines=4)
    sd = poly([(15, 17), (22, 17), (22, 13), (30, 21), (22, 29), (22, 25), (15, 25)])
    ic.shape(sd, vgrad(13, 29, (150, 202, 255), (46, 112, 224), (22, 66, 176)), rim=(16, 52, 140))
    cap = inter(inset(sd, 1.2), rrect(0, 0, 32, 20.8, 0))
    ic.add(cap, vgrad(13, 21, (255, 255, 255, 0.75), (255, 255, 255, 0.1)))
    return ic


def icon_browser():
    ic = Icon()
    cx, cy, r = 16, 16, 13
    sd = circle(cx, cy, r)
    ic.shape(sd, radial(cx, cy, 18, (120, 196, 255), (14, 66, 170), cx - 4, cy - 6), rim=(12, 44, 120), w=1.2)
    inner = inset(sd, 1.2)
    # Land: overlapping lobes, an Americas-like shape on the left and an
    # Europe/Africa hint on the right edge, darker toward the limb so it wraps.
    lobes = [(9.5, 8.5, 3.0), (11.5, 10.5, 3.2), (12.5, 13.5, 2.6), (10.0, 12.0, 2.2),
             (13.2, 17.0, 1.8), (14.0, 20.0, 2.8), (15.2, 23.2, 2.2), (15.8, 25.6, 1.4),
             (21.5, 9.2, 2.2), (23.5, 10.8, 2.6), (22.4, 14.0, 2.4), (24.2, 16.6, 2.8),
             (23.0, 19.6, 3.0), (22.0, 22.4, 2.2)]
    land = union(*[circle(*l) for l in lobes])
    ic.add(inter(inner, land), radial(cx, cy, 14, (176, 226, 104), (28, 92, 52), cx - 4, cy - 6))
    ic.add(inter(inner, stroke([(cx - 13, cy), (cx + 13, cy)], 0.6)), solid(255, 255, 255, 0.25))
    ic.add(inter(inner, stroke([(cx, cy - 13), (cx, cy + 13)], 0.6)), solid(255, 255, 255, 0.25))
    gloss_cap(ic, sd, cx, 3, 16, 10.5, 7.2, a0=0.7)
    # a compass needle across the globe, turned 45 degrees
    nrth = rot([(cx, cy - 10.5), (cx + 2.6, cy), (cx - 2.6, cy)], 45, cx, cy)
    sth = rot([(cx, cy + 10.5), (cx + 2.6, cy), (cx - 2.6, cy)], 45, cx, cy)
    ic.add(poly([(x + 0.5, y + 0.8) for x, y in nrth + []]), solid(0, 20, 60, 0.35))
    ic.add(poly(nrth), vgrad(5, 16, (255, 120, 100), (214, 30, 24)))
    ic.add(poly(sth), vgrad(16, 27, (250, 250, 252), (186, 196, 214)))
    ic.add(circle(cx, cy, 1.5), vgrad(cy - 1.5, cy + 1.5, (255, 255, 255), (150, 160, 180)))
    return ic


ICONS = [
    ("new", icon_new, "New Feed"),
    ("sidebar-hide", icon_sidebar_hide, "Hide Sidebar"),
    ("sidebar-show", icon_sidebar_show, "Show Sidebar"),
    ("refresh", icon_refresh, "Refresh"),
    ("markall-read", icon_markall_read, "Mark All Read"),
    ("markall-unread", icon_markall_unread, "Mark All Unread"),
    ("hideread", icon_hideread, "Hide Read"),
    ("showread", icon_showread, "Show Read"),
    ("mark-read", icon_mark_read, "Mark Read"),
    ("mark-unread", icon_mark_unread, "Mark Unread"),
    ("star", icon_star, "Star"),
    ("nextunread", icon_nextunread, "Next Unread"),
    ("browser", icon_browser, "Open in Browser"),
]


def uri(buf):
    return "data:image/png;base64," + base64.b64encode(png_bytes(buf)).decode()


def proof(rendered, path):
    cells = []
    for name, label, buf in rendered:
        u = uri(buf)
        cells.append(
            '<div class="cell"><div class="row"><img src="%s" width="32" height="32">'
            '<img class="px" src="%s" width="64" height="64"></div>'
            '<div class="tb"><img src="%s" width="32" height="32"><span>%s</span></div>'
            '<code>%s</code></div>' % (u, u, u, label, name))
    bar = "".join('<div><img src="%s" width="32" height="32"><span>%s</span></div>' % (uri(b), l)
                  for _, l, b in rendered)
    html = ("""<!doctype html><meta charset="utf-8"><title>Aqua toolbar icons</title>
<style>
body{font:13px "Lucida Grande",sans-serif;margin:24px;background:#fff;color:#222}
.grid{display:flex;flex-wrap:wrap;gap:16px}
.cell{width:150px}
.row{display:flex;gap:12px;align-items:center;padding:6px;border:1px solid #ddd}
.px{image-rendering:pixelated}
.tb{margin-top:6px;height:62px;background:linear-gradient(#e8e8e8,#c4c4c4);border:1px solid #9a9a9a;
 display:flex;flex-direction:column;align-items:center;justify-content:center;gap:3px}
.tb span,.bar span{font:11px "Lucida Grande",sans-serif;color:#000}
code{font-size:11px;color:#666}
.bar{display:flex;gap:14px;padding:8px 14px;background:linear-gradient(#e8e8e8,#c4c4c4);border:1px solid #9a9a9a;margin:20px 0;flex-wrap:wrap}
.bar div{display:flex;flex-direction:column;align-items:center;gap:3px}
</style>
<h3>Gazette Aqua toolbar icons, 32x32: 1x, 2x (nearest), in a toolbar</h3>
<div class="grid">""" + "".join(cells) + """</div>
<h3>As one toolbar</h3><div class="bar">""" + bar + "</div>")
    with open(path, "w") as f:
        f.write(html)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default="Resources/osx/toolbar")
    ap.add_argument("--proof")
    ap.add_argument("--enlarged", help="directory for 8x preview PNGs")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    rendered = []
    for name, fn, label in ICONS:
        buf = fn().render()
        rendered.append((name, label, buf))
        with open(os.path.join(a.out, name + ".png"), "wb") as f:
            f.write(png_bytes(buf))
        if a.enlarged:
            os.makedirs(a.enlarged, exist_ok=True)
            with open(os.path.join(a.enlarged, name + ".png"), "wb") as f:
                f.write(scaled_png(buf, 8))
    if a.proof:
        proof(rendered, a.proof)


if __name__ == "__main__":
    main()
