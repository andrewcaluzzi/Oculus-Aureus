#!/usr/bin/env python3
"""SEVERNAYA SNOWFIELD (v2, sniper mode v2): the sniper duel's long open rolling snowfield, about twice as wide and 1.6 times as deep as
the first prototype, split into a grid of bg rooms joined by tall portals (as stock Jungle's outdoor rooms are) so the rooms behind the
camera are not drawn.

    python3 tools/levelgen/build_snowfield.py            -> port/levelgen_snowfield/ (build with -DGE_LEVELGEN_EXTRA=port/levelgen_snowfield
                                                             -DGE_LEVELGEN_EXTRA_NAME=snowfield; GE_LEVELGEN=snowfield picks it)

Not a gen_level.py layout: the ground is a HEIGHTFIELD written here with gen_level.py's own writers (the display list batching, the
deflate, the setup). One height function h(x, z) on one CELL grid gives both the drawn triangles and the stan tiles (the same corners, the
same diagonal), so what Bond sees of the ground and what the walk / the stan line use are the same surface.
- the field: 400 m wide (x) and 550 m long (z) of play. Bond's shoulder at the south end, a shallow basin, long ridgelines and hummocks
  across the sight line (cover, dead ground), hollows to crawl along, the hunters' rise at the north end; gentle slopes in the play area.
  550 m is the most the length can be: a stan tile's corners are 16-bit (x, z within +-32767), and the stan runs to the apron's edge;
- the rooms: RX x RZ rooms on a grid; each room's vertices are stored relative to its centre (the bg room position), because the drawn
  ground runs past 32767 too; the portals are vertical rectangles on the room grid lines, under the ground to over the skyline;
- the perimeter: the ground rises (a walkable apron) into snow slopes with rock outcrops; the stan ends at their foot, so nobody walks off
  the map; on the crest a ring of Surface's own mountain skyline (its four stacked translucent strips, drawn in the rooms' second display
  lists with Surface's own render state) stands against the sky;
- textures by number (Surface's snow 1264, rock 1263 on the steep faces, the skyline strips 1198..1201): the texels are read from the
  ROM at run time, nothing ROM-derived is written;
- the sky and fog are Surface's own record from the ROM's table (bgfog.c, the levelgen slot when the level is "snowfield"), with the far
  plane and the fog band moved out for the long sight lines; the camera matrix goes to the renderer in floats on this level (the world's
  coordinates pass the fixed-point matrix's range at the scope's zoom: bondview2.c, gfx_pc.cpp).
The setup gives Bond nothing at the start (the stock levelgen setup's PP7 is left out: the duel's loadout is the rifle and binoculars).
"""
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.dirname(HERE)
ROOT = os.path.dirname(TOOLS)
sys.path.insert(0, TOOLS)
import gen_level as gl      # noqa: E402
import gen_flatland as gf   # noqa: E402

NAME = "levelgen"
OUT = os.path.join(ROOT, "port", "levelgen_snowfield")

CELL = 750                      # one grid for the drawn ground and the stan (7.5 m)
PW, PL, RC = 20000, 27500, 9000  # the play area's half width (x), half length (z), its corner radius
APRON = 2600                    # the walkable rise past the play area (the stan ends here: the cliff foot)
CLIFF = 4200                    # the cliff's horizontal run
RING = APRON + CLIFF + 300      # the skyline ring
DRAW = RING + 1100              # nothing is drawn past this
BASE = 1400                     # every height is above this much
X0, X1 = -27750, 27750          # the grid (multiples of CELL)
Z0, Z1 = -36000, 36000
RX, RZ = 4, 6                   # bg rooms: RX x RZ
STAN_MAX = 32000                # a stan corner's x, z must fit a 16-bit coordinate

SNOW, ROCK = 1264, 1263
SKY_STRIPS = [1201, 1200, 1199, 1198]  # bottom to top (Surface's stacking)
K_SNOW = 0.064 * 32.0           # ST units per world unit (32 per texel)
K_ROCK = 0.035 * 32.0
BAND = 1000                     # one skyline strip's height
SEG = 2900                      # one skyline image's width
SX, SZ, SH = 2.0, 1.62, 1.45    # the first prototype's field, stretched (x, z) and raised


def g(v, c, s):
    return math.exp(-((v - c) / float(s)) ** 2)


def smooth(t):
    t = max(0.0, min(1.0, t))
    return t * t * (3 - 2 * t)


def seg_dist(x, z, a, b):
    ax, az = a
    bx, bz = b
    dx, dz = bx - ax, bz - az
    t = max(0.0, min(1.0, ((x - ax) * dx + (z - az) * dz) / float(dx * dx + dz * dz)))
    return math.hypot(x - (ax + t * dx), z - (az + t * dz)), t


def sdf_plain(x, z):
    """distance outside the rounded play rectangle (negative inside)"""
    qx, qz = abs(x) - (PW - RC), abs(z) - (PL - RC)
    return math.hypot(max(qx, 0.0), max(qz, 0.0)) + min(max(qx, qz), 0.0) - RC


def sdf(x, z):
    """the same with a natural wobble in the boundary (smaller at the ends, where the stan meets its 16-bit limit)"""
    wob = 1000.0 * math.sin(x / 3300.0 + 1.3) * math.cos(z / 4100.0) + 700.0 * math.sin(z / 2600.0 + x / 5100.0)
    return sdf_plain(x, z) + wob * (0.45 + 0.55 * smooth((PL - abs(z)) / 4000.0))


# the first prototype's features (its own coordinates; stretched by SX, SZ)
RIDGES = [  # (a, b, height, half width): long ridgelines across the sight line
    ((-9500, -7600), (7200, -4000), 650, 1300),
    ((-4200, 7000), (9500, 2400), 850, 1600),
    ((-9200, 1400), (-3400, 3600), 450, 1100),
]
HUMPS = [   # (x, z, height, sigma)
    (6500, -10000, 550, 1900), (-6500, 11000, 750, 2400), (3000, 12600, 420, 1300), (-2200, -11400, 300, 1500),
    (-1000, 500, -450, 5000), (7800, 9000, 380, 1800),
]
# new ones, in the new coordinates: more cover on the wide flanks and in the long middle
RIDGES2 = [
    ((-19000, 9000), (-9000, 15500), 700, 2200),
    ((10000, -3000), (19500, 3000), 600, 2000),
    ((-17000, -16000), (-7000, -12500), 520, 1800),
]
HUMPS2 = [  # (x, z, height, sigma)
    (-15000, -2000, 650, 2600), (15500, 12500, 700, 2600), (-1500, 17500, 480, 2000), (9000, -15500, 420, 2200),
    (-8500, 1500, 380, 1700), (5500, 1500, 360, 1600), (16000, -20000, 500, 2400), (-16000, 21000, 600, 2600),
]
HOLLOWS = [  # (a, b, depth, half width): shallow runs of dead ground to crawl along
    ((-6000, -20500), (-11000, -6000), 260, 1100), ((7000, -20000), (12500, -8500), 240, 1000), ((-3000, 3000), (3500, 9500), 220, 900),
]


def field_old(x, z):
    y = 900.0 * g(z, -14800, 3500) + 1300.0 * g(z, 15600, 4500) - 300.0 * g(z, -1500, 6500)
    for a, b, hh, w in RIDGES:
        d, t = seg_dist(x, z, a, b)
        y += hh * math.exp(-(d / float(w)) ** 2) * (0.35 + 0.65 * math.sin(math.pi * t))
    for hx, hz, hh, s in HUMPS:
        y += hh * g(x, hx, s) * g(z, hz, s)
    return y


def field(x, z):
    y = SH * field_old(x / SX, z / SZ)
    for a, b, hh, w in RIDGES2:
        d, t = seg_dist(x, z, a, b)
        y += hh * math.exp(-(d / float(w)) ** 2) * (0.35 + 0.65 * math.sin(math.pi * t))
    for hx, hz, hh, s in HUMPS2:
        y += hh * g(x, hx, s) * g(z, hz, s)
    for a, b, dep, w in HOLLOWS:
        d, t = seg_dist(x, z, a, b)
        y -= dep * math.exp(-(d / float(w)) ** 2) * (0.4 + 0.6 * math.sin(math.pi * t))
    y += 150.0 * math.sin(x / 3500.0 + 0.4) * math.sin(z / 4100.0 + 0.7) + 90.0 * math.sin((x + 1.3 * z) / 2600.0)
    y += 45.0 * math.sin(x / 1300.0 + z / 1700.0) * math.cos(z / 1500.0 - x / 2300.0)
    return y


def height(x, z):
    y = BASE + 450 + field(x, z)
    s = sdf(x, z)
    if s > 0:
        y += 0.16 * min(s, APRON)
    if s > APRON:
        c = min(1.0, (s - APRON) / float(CLIFF))
        sm = c * c * (3 - 2 * c)
        y += 3400.0 * sm * (0.75 + 0.25 * math.sin(x / 2700.0) * math.cos(z / 2300.0)) + 260.0 * math.sin(x / 900.0) * math.sin(z / 1100.0) * c
        if s > APRON + CLIFF:
            y += 0.12 * (s - APRON - CLIFF)
    return y


# ------------------------------------------------------------------- grid ---
NX, NZ = (X1 - X0) // CELL, (Z1 - Z0) // CELL
XS = [X0 + CELL * i for i in range(NX + 1)]
ZS = [Z0 + CELL * j for j in range(NZ + 1)]
HG = [[int(round(height(x, z))) for x in XS] for z in ZS]     # HG[j][i]
RWX, RWZ = (X1 - X0) / float(RX), (Z1 - Z0) / float(RZ)


def H(i, j):
    return HG[j][i]


def cell_centre(i, j):
    return XS[i] + CELL / 2.0, ZS[j] + CELL / 2.0


def cell_of(x, z):
    return int((x - X0) // CELL), int((z - Z0) // CELL)


def room_at(x, z):
    ix = min(RX - 1, max(0, int((x - X0) // RWX)))
    iz = min(RZ - 1, max(0, int((z - Z0) // RWZ)))
    return 1 + ix + RX * iz


def room_of_cell(i, j):
    return room_at(*cell_centre(i, j))


def room_centre(r):
    ix, iz = (r - 1) % RX, (r - 1) // RX
    return int(round(X0 + RWX * (ix + 0.5))), int(round(Z0 + RWZ * (iz + 0.5)))


def is_stan(i, j):
    if not (0 <= i < NX and 0 <= j < NZ):
        return False
    return sdf(*cell_centre(i, j)) <= APRON


def is_drawn(i, j):
    return min(sdf(XS[a], ZS[b]) for a in (i, i + 1) for b in (j, j + 1)) <= DRAW


def P(i, j):
    return (XS[i], H(i, j), ZS[j])


def ground_y(x, z):
    """the drawn surface's height at (x, z): the cell's triangle (diagonal (x0,z0)-(x1,z1)), as drawn and as the stan"""
    i, j = cell_of(x, z)
    i, j = max(0, min(NX - 1, i)), max(0, min(NZ - 1, j))
    fx, fz = (x - XS[i]) / float(CELL), (z - ZS[j]) / float(CELL)
    h00, h10, h01, h11 = H(i, j), H(i + 1, j), H(i, j + 1), H(i + 1, j + 1)
    if fx >= fz:   # the triangle (x0,z0) (x1,z1) (x1,z0)
        return h00 + fx * (h10 - h00) + fz * (h11 - h10)
    return h00 + fz * (h01 - h00) + fx * (h11 - h01)


def normal(t):
    a, b, c = t
    u = [b[k] - a[k] for k in range(3)]
    v = [c[k] - a[k] for k in range(3)]
    n = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
    ln = math.sqrt(sum(q * q for q in n)) or 1.0
    n = tuple(q / ln for q in n)
    return n if n[1] >= 0 else tuple(-q for q in n)


SUN = (0.55, 0.45, -0.70)
_sl = math.sqrt(sum(q * q for q in SUN))
SUN = tuple(q / _sl for q in SUN)
TINT = (222, 230, 250)


def vnormal(i, j):
    i0, i1 = max(0, i - 1), min(NX, i + 1)
    j0, j1 = max(0, j - 1), min(NZ, j + 1)
    dx = (H(i1, j) - H(i0, j)) / float(XS[i1] - XS[i0])
    dz = (H(i, j1) - H(i, j0)) / float(ZS[j1] - ZS[j0])
    ln = math.sqrt(dx * dx + 1 + dz * dz)
    return (-dx / ln, 1 / ln, -dz / ln)


VCOL = {}


def vcol(i, j):
    k = (i, j)
    if k not in VCOL:
        n = vnormal(i, j)
        d = max(0.0, n[0] * SUN[0] + n[1] * SUN[1] + n[2] * SUN[2])
        b = 0.50 + 0.62 * d
        VCOL[k] = tuple(max(0, min(255, int(round(c * b)))) for c in TINT)
    return VCOL[k]


def st_of(p, tex, n):
    x, y, z = p
    if tex == SNOW or abs(n[1]) > 0.8:
        k = K_SNOW if tex == SNOW else K_ROCK
        return x * k, z * k
    k = K_ROCK   # the rock's strata: slanted and waved with the position, so the bands are not ruler-straight
    u = z if abs(n[0]) > abs(n[2]) else x
    return u * k, (-y + 0.2 * u + 700.0 * math.sin(x / 2600.0 + z / 3900.0)) * k


# -------------------------------------------------------------- the meshes ---
class RoomMesh:
    def __init__(self):
        self.pri = {}            # tex -> [triangle]; a triangle = 3 x (x, y, z, s, t, (r, g, b)) in world coordinates
        self.sec = {}            # tex -> [triangle] (the skyline)


MESH = {r: RoomMesh() for r in range(1, RX * RZ + 1)}


def build_ground():
    drawn = 0
    for j in range(NZ):
        for i in range(NX):
            if not is_drawn(i, j):
                continue
            drawn += 1
            m = MESH[room_of_cell(i, j)]
            for t in (((i, j), (i + 1, j + 1), (i + 1, j)), ((i, j), (i, j + 1), (i + 1, j + 1))):
                pts = [P(*k) for k in t]
                n = normal(pts)
                tex = SNOW if n[1] > 0.66 else ROCK
                m.pri.setdefault(tex, []).append([p + st_of(p, tex, n) + (vcol(*k),) for p, k in zip(pts, t)])
    return drawn


def ring_points():
    """the skyline ring: the play rectangle offset by RING (rounded corners), points about SEG apart, counter-clockwise seen from above"""
    hw, hl, r = PW - RC, PL - RC, RC + RING
    pts = []

    def arc(cx, cz, a0, a1):
        n = max(2, int(math.ceil(abs(a1 - a0) * r / SEG)))
        for k in range(n):
            a = a0 + (a1 - a0) * k / float(n)
            pts.append((cx + r * math.cos(a), cz + r * math.sin(a)))

    def line(ax, az, bx, bz):
        ln = math.hypot(bx - ax, bz - az)
        n = max(1, int(round(ln / SEG)))
        for k in range(n):
            pts.append((ax + (bx - ax) * k / float(n), az + (bz - az) * k / float(n)))
    line(hw + r, -hl, hw + r, hl)
    arc(hw, hl, 0.0, math.pi / 2)
    line(hw, hl + r, -hw, hl + r)
    arc(-hw, hl, math.pi / 2, math.pi)
    line(-hw - r, hl, -hw - r, -hl)
    arc(-hw, -hl, math.pi, 1.5 * math.pi)
    line(-hw, -hl - r, hw, -hl - r)
    arc(hw, -hl, 1.5 * math.pi, 2 * math.pi)
    return pts


def build_skyline():
    pts = ring_points()
    n = len(pts)
    white = (230, 230, 230)
    quads = 0
    for k in range(n):
        a, b = pts[k], pts[(k + 1) % n]
        ya, yb = height(*a) - 500.0, height(*b) - 500.0
        m = MESH[room_at((a[0] + b[0]) / 2.0, (a[1] + b[1]) / 2.0)]
        for q, tex in enumerate(SKY_STRIPS):
            a0, a1 = ya + q * BAND, ya + (q + 1) * BAND
            b0, b1 = yb + q * BAND, yb + (q + 1) * BAND
            quad = [(a[0], a1, a[1], 0, 512, white), (b[0], b1, b[1], 2048, 512, white), (b[0], b0, b[1], 2048, 0, white),
                    (a[0], a0, a[1], 0, 0, white)]
            m.sec.setdefault(tex, []).append([quad[0], quad[1], quad[2]])
            m.sec.setdefault(tex, []).append([quad[0], quad[2], quad[3]])
            quads += 1
    return quads, n


# ------------------------------------------------------------------- bg ---
PRI_HEAD = [(0xE7000000, 0), (0xBA001402, 0x00100000), (0xB900031D, 0x0C182078), (0xFC26A004, 0x1F1093FF), (0xBA001001, 0x00010000),
            (0xBB002801, 0xFFFFFFFF)]
PRI_MID = [(0xBA001102, 0), (0xBA000C02, 0x00002000), (0xB6000000, 0x00003000), (0xFB000000, 0x000000FF)]
# Surface's own translucent skyline state (its sec display lists): alpha-blended render mode, its combiner and texture command
SEC_HEAD = [(0xE7000000, 0), (0xBA001402, 0x00100000), (0xB900031D, 0x0C1849D8), (0xFC26E404, 0x1F10FFFF), (0xBA001001, 0x00010000),
            (0xBB002801, 0xFFFFFFFF)]
SEC_MID = [(0xBA001102, 0x00040000), (0xBA000C02, 0x00002000), (0xB6000000, 0x00003000), (0xFB000000, 0x000000FF)]
ST_LIMIT = 30000


def emit_tris(tris, nv0, origin):
    """vertex bytes and display list words of a run of triangles of one texture: shared vertices, up to 16 a load, drawn by tri4
    commands; positions relative to the room's origin; each load's texture coordinates shifted by whole 128-texel periods (seamless)"""
    vbytes, dl = b"", b""
    nv = nv0
    verts, index, btris = [], {}, []
    smin = tmin = 1e18
    smax = tmax = -1e18
    ox, oz = origin

    def flush():
        nonlocal vbytes, dl, nv, verts, index, btris, smin, smax, tmin, tmax
        if not verts:
            return
        ds = gl.ST_ALIGN * math.floor(smin / gl.ST_ALIGN)
        dt = gl.ST_ALIGN * math.floor(tmin / gl.ST_ALIGN)
        for (x, y, z, s, t, col) in verts:
            si, tj = int(round(s - ds)), int(round(t - dt))
            assert -32768 <= si <= 32767 and -32768 <= tj <= 32767, (si, tj)
            vx, vy, vz = int(round(x - ox)), int(round(y)), int(round(z - oz))
            assert -32768 <= vx <= 32767 and -32768 <= vy <= 32767 and -32768 <= vz <= 32767, (vx, vy, vz)
            vbytes += gf.vtx(vx, vy, vz, si, tj, tuple(col) + (0xFF,))
        n = len(verts)
        dl += gf.gfx(0x04000000 | ((n - 1) << 20) | (n * 16), 0x0E000000 + nv * 16)
        nv += n
        for k in range(0, len(btris), 4):
            dl += gl.tri4_from(btris[k:k + 4])
        verts, index, btris = [], {}, []
        smin = tmin = 1e18
        smax = tmax = -1e18

    for t in tris:
        keys = [(int(round(p[0])), int(round(p[1])), int(round(p[2])), round(p[3], 1), round(p[4], 1), p[5]) for p in t]
        new = len(set(k for k in keys if k not in index))
        s0, s1 = min(smin, min(p[3] for p in t)), max(smax, max(p[3] for p in t))
        t0, t1 = min(tmin, min(p[4] for p in t)), max(tmax, max(p[4] for p in t))
        if len(verts) + new > 16 or (s1 - gl.ST_ALIGN * math.floor(s0 / gl.ST_ALIGN)) > ST_LIMIT or \
                (t1 - gl.ST_ALIGN * math.floor(t0 / gl.ST_ALIGN)) > ST_LIMIT:
            flush()
            s0, s1 = min(p[3] for p in t), max(p[3] for p in t)
            t0, t1 = min(p[4] for p in t), max(p[4] for p in t)
        smin, smax, tmin, tmax = s0, s1, t0, t1
        ids = []
        for k, p in zip(keys, t):
            if k not in index:
                index[k] = len(verts)
                verts.append(p)
            ids.append(index[k])
        btris.append(tuple(ids))
    flush()
    return vbytes, dl, nv


def room_binaries(r):
    m = MESH[r]
    origin = room_centre(r)
    vb, pri, sec = b"", b"", b""
    nv = 0
    for w in PRI_HEAD:
        pri += gf.gfx(*w)
    for ti, tex in enumerate(sorted(m.pri)):
        pri += gf.gfx(0xC0080002, tex)
        if ti == 0:
            for w in PRI_MID:
                pri += gf.gfx(*w)
        v, d, nv = emit_tris(m.pri[tex], nv, origin)
        vb += v
        pri += d
    pri += gf.gfx(0xB8000000, 0)
    if m.sec:
        for w in SEC_HEAD:
            sec += gf.gfx(*w)
        for ti, tex in enumerate(sorted(m.sec)):
            sec += gf.gfx(0xC01B8001, 0xFA9A1000 | tex)
            if ti == 0:
                for w in SEC_MID:
                    sec += gf.gfx(*w)
            v, d, nv = emit_tris(m.sec[tex], nv, origin)
            vb += v
            sec += d
        sec += gf.gfx(0xB8000000, 0)
    return vb, pri, sec


def portals():
    """the tall portals between neighbouring rooms (vertical rectangles on the room grid lines, from under the ground to over the
    skyline's top), in world coordinates: (index, room a, room b, 4 points)"""
    out = []
    y0, y1 = -2000.0, 20000.0
    for iz in range(RZ):
        for ix in range(RX):
            a = 1 + ix + RX * iz
            z0, z1 = Z0 + RWZ * iz, Z0 + RWZ * (iz + 1)
            x0, x1 = X0 + RWX * ix, X0 + RWX * (ix + 1)
            if ix + 1 < RX:
                out.append((len(out), a, a + 1, [(x1, y1, z0), (x1, y1, z1), (x1, y0, z1), (x1, y0, z0)]))
            if iz + 1 < RZ:
                out.append((len(out), a, a + RX, [(x0, y1, z1), (x1, y1, z1), (x1, y0, z1), (x0, y0, z1)]))
    return out


def gen_bg(describe):
    rooms = [r for r in sorted(MESH) if MESH[r].pri or MESH[r].sec]
    assert rooms == list(range(1, RX * RZ + 1)), "every room must have geometry (the room ids are the grid)"
    ports = portals()
    out = [gl.HEADER_NOTE.replace("gen_level.py", "levelgen/build_snowfield.py") % ("bg_%s_all_p.c" % NAME, describe),
           '#include "../../assets/obseg/bg/bg_all_p.h"', '#include "bg_%s_all_p.h"' % NAME, "",
           "struct bg_header header = {0, &room_data_table, &portal_data_table, &global_visibility_commands, 0};", "",
           "struct room_data_table_entry room_data_table[] = {", "    {0},"]
    bins = {r: room_binaries(r) for r in rooms}
    for r in rooms:
        sec = "&sec_mapping_binary_%d" % r if bins[r][2] else "0"
        cx, cz = room_centre(r)
        out.append("    {&point_table_binary_%d, &pri_mapping_binary_%d, %s, %d.0, 0.0, %d.0},   /* cell %d,%d */" % (
            r, r, sec, cx, cz, (r - 1) % RX, (r - 1) // RX))
    out += ["    {&point_table_binary_end, &pri_mapping_binary_end, &pri_mapping_binary_end, 0.0, 0.0, 0.0},", "    {0}", "};", "",
            "u32 global_visibility_commands[] ={0x10000, 0};", "", "struct portal_data_table_entry portal_data_table[] = {"]
    for idx, a, b, _ in ports:
        out.append("    {&portal_%d, %d, %d, 0}," % (idx, a, b))
    out += ["    {0}", "};", ""]
    for idx, a, b, pts in ports:
        out.append("struct portal_4_point portal_%d = {4, 0, 0, 0, %s};" % (idx, ", ".join("%.1f, %.1f, %.1f" % p for p in pts)))
    out.append("")
    stats = {}
    for r in rooms:
        vb, pri, sec = bins[r]
        stats[r] = (len(vb) // 16, len(vb) + len(pri) + len(sec))
        out.append(gf.fmt_words("point_table_binary_%d" % r, gf.be_words(gf.deflate_1172(vb))))
        out.append("")
    out += ["u32 point_table_binary_end[] = {\n    0\n};", ""]
    for r in rooms:   # stock order: every point table, then every display list (bg.c sizes each from the next one's start)
        out.append(gf.fmt_words("pri_mapping_binary_%d" % r, gf.be_words(gf.deflate_1172(bins[r][1]))))
        out.append("")
        if bins[r][2]:
            out.append(gf.fmt_words("sec_mapping_binary_%d" % r, gf.be_words(gf.deflate_1172(bins[r][2]))))
            out.append("")
    out += ["u32 pri_mapping_binary_end[] = {\n    0, 0, 0, 0\n};", ""]
    guard = "_BG_%s_ALL_P_H_" % NAME.upper()
    hdr = ["#ifndef " + guard, "#define " + guard, "", "#include <ultra64.h>", ""]
    hdr += ["extern u32 point_table_binary_%d[];" % r for r in rooms] + ["extern u32 point_table_binary_end[];"]
    hdr += ["extern u32 pri_mapping_binary_%d[];" % r for r in rooms]
    hdr += ["extern u32 sec_mapping_binary_%d[];" % r for r in rooms if bins[r][2]]
    hdr += ["extern u32 pri_mapping_binary_end[];"]
    hdr += ["extern struct portal_4_point portal_%d;" % idx for idx, _, _, _ in ports]
    hdr += ["#endif", ""]
    return "\n".join(out), "\n".join(hdr), stats, len(ports)


# ------------------------------------------------------------------ stan ---
def build_stan():
    """tiles grouped by room (stan.c wants each room's tiles in one run)"""
    by = {}
    for j in range(NZ):
        for i in range(NX):
            if is_stan(i, j):
                by.setdefault(room_of_cell(i, j), []).append((i, j))
    tris, index = [], {}
    Y = (lambda x, z: HG[(z - Z0) // CELL][(x - X0) // CELL])
    for r in sorted(by):
        for i, j in by[r]:
            for sub, t in enumerate(gl.cell_tris(XS[i], ZS[j], XS[i + 1], ZS[j + 1], Y)):
                for p in t:
                    assert abs(p[0]) <= STAN_MAX and abs(p[2]) <= STAN_MAX and 0 <= p[1] <= STAN_MAX, ("stan corner past 16 bits", p)
                index[(i, j, sub)] = len(tris)
                tris.append((r, t))
    return tris, index


def gen_stan(describe, tris):
    edges = gl.stan_edges(tris)
    for v in edges.values():
        assert len(v) <= 2

    def link(n, k):
        t = tris[n][1]
        v = edges[frozenset((t[k], t[(k + 1) % 3]))]
        return 0 if len(v) == 1 else gl.LINK_BASE + 4 * (v[0] if v[1] == n else v[1])

    sym = "Tbg_%s_all_p_stanZ" % NAME
    out = [gl.HEADER_NOTE.replace("gen_level.py", "levelgen/build_snowfield.py") % (sym + ".c", describe),
           "#include <ultra64.h>", "#include <game/stan.h>", "", "// forward declarations", "StandTile tile_0;", "",
           "StandFileHeader %s = {" % sym, "    NULL,", "    &tile_0,", "    {0x00, 0x00, 0x00, 0x00}", "};", "",
           "/* one tile a line (the 2 MB file limit): T(n, room, x y z link of each corner) */",
           "#define T(n, r, x0, y0, z0, l0, x1, y1, z1, l1, x2, y2, z2, l2) StandTile tile_##n = {(((n) + 1) << 8) | 1, r, 0x0, 0xe, 0xe, 0xe, "
           "3, 0x0, 0x1, 0x2, {{x0, y0, z0, l0}, {x1, y1, z1, l1}, {x2, y2, z2, l2}}};", ""]
    for n, (room, t) in enumerate(tris):
        out.append("T(%d,%d,%s)" % (n, room, ",".join("%d,%d,%d,%d" % (t[k][0], t[k][1], t[k][2], link(n, k)) for k in range(3))))
    out.append("StandTile tile_%d = {0x000000, 0x00, 0x0, 0x0, 0x0, 0x0, 0, 0x0, 0x0, 0x0};" % len(tris))
    out.append("")
    out.append('StandFileFooter footer = {\n    "unstric",\n    NULL,\n    NULL,\n    NULL,\n    NULL\n};')
    out.append("")
    return "\n".join(out), sum(1 for v in edges.values() if len(v) == 2)


# ------------------------------------------------------------------ pads ---
class Room:
    pass


ROOMS = {}
for _r in range(1, RX * RZ + 1):
    _R = Room()
    _R.id, _R.name = _r, "s%d%d" % ((_r - 1) % RX, (_r - 1) // RX)
    ROOMS[_r] = _R


def tri_y(t, x, z):
    (ax, ay, az), (bx, by, bz), (cx, cy, cz) = t
    det = (bz - cz) * (ax - cx) + (cx - bx) * (az - cz)
    l1 = ((bz - cz) * (x - cx) + (cx - bx) * (z - cz)) / float(det)
    l2 = ((cz - az) * (x - cx) + (ax - cx) * (z - cz)) / float(det)
    return l1 * ay + l2 * by + (1 - l1 - l2) * cy


def tile_for(tris, index, x0, z0):
    for dx, dz in ((0, 0), (37, 13), (-29, 41), (43, -31), (-41, -17), (90, 60), (-80, -70)):
        x, z = x0 + dx, z0 + dz
        i, j = cell_of(x, z)
        for sub in (0, 1):
            n = index.get((i, j, sub))
            if n is not None and gl.inside_xz((x, z), tris[n][1], 6.0):
                return n, x, z
    return None, x0, z0


def make_pad(tris, index, x, z, kind, name=None, look=(0.0, 1.0)):
    n, x, z = tile_for(tris, index, x, z)
    if n is None:
        return None
    p = gl.Pad()
    p.room = ROOMS[tris[n][0]]
    p.x, p.z, p.tile, p.kind, p.name = int(round(x)), int(round(z)), n, kind, name
    p.y = int(round(tri_y(tris[n][1], p.x, p.z)))
    ln = math.hypot(look[0], look[1]) or 1.0
    p.look = (look[0] / ln, look[1] / ln)
    return p


def seg_on_stan(ax, az, bx, bz):
    n = max(2, int(math.hypot(bx - ax, bz - az) // 150))
    for k in range(n + 1):
        x = ax + (bx - ax) * k / float(n)
        z = az + (bz - az) * k / float(n)
        if not is_stan(*cell_of(x, z)):
            return False
    return True


def build_pads(tris, index):
    """the waypoint pads (a grid every 3 cells, 22.5 m), the named perches and the start, joined"""
    pads, grid = [], {}
    for j in range(1, NZ, 3):
        for i in range(1, NX, 3):
            if is_stan(i, j) and all(is_stan(i + a, j + b) for a in (-1, 0, 1) for b in (-1, 0, 1)):
                x, z = cell_centre(i, j)
                p = make_pad(tris, index, x + 60, z - 110, "grid")
                if p is None:
                    continue
                p.id = len(pads)
                pads.append(p)
                grid[(i, j)] = p
    for nm, (x, z), face in NAMED:
        p = make_pad(tris, index, x, z, "named", nm, (face[0] - x, face[1] - z))
        if p is None:
            sys.exit("pad %s at (%d, %d): not on the stan" % (nm, x, z))
        p.id = len(pads)
        pads.append(p)
    st = [n for n in NAMED if n[0] == "b1"][0]
    start = make_pad(tris, index, st[1][0], st[1][1], "start", "start", (st[2][0] - st[1][0], st[2][1] - st[1][1]))
    start.id = len(pads)
    pads.append(start)
    adj = {p.id: set() for p in pads}
    for (i, j), p in grid.items():
        for a, b in ((3, 0), (0, 3), (3, 3), (3, -3)):
            q = grid.get((i + a, j + b))
            if q and seg_on_stan(p.x, p.z, q.x, q.z):
                adj[p.id].add(q.id)
                adj[q.id].add(p.id)
    gp = list(grid.values())
    for p in pads:
        if p.kind == "grid":
            continue
        for q in sorted(gp, key=lambda q: math.hypot(q.x - p.x, q.z - p.z))[:3]:
            if seg_on_stan(p.x, p.z, q.x, q.z):
                adj[p.id].add(q.id)
                adj[q.id].add(p.id)
        if not adj[p.id]:
            sys.exit("pad %s has no waypoint link" % p.name)
    seen, todo = {pads[0].id}, [pads[0].id]
    while todo:
        u = todo.pop()
        for v in adj[u]:
            if v not in seen:
                seen.add(v)
                todo.append(v)
    if len(seen) != len(pads):
        sys.exit("waypoints: %d pads not connected" % (len(pads) - len(seen)))
    group = {p.id: 0 for p in pads}
    return pads, grid, start, adj, group, {0: set()}, {0: [p.id for p in pads]}


def gen_lua(pads, start):
    o = ["-- Generated by tools/levelgen/build_snowfield.py - do not edit; rerun the script.",
         "-- SEVERNAYA SNOWFIELD (v2) for mode scripts: include(\"levelgen_layout_snowfield\") (modding/levelgen.md).",
         "LEVEL = {", '  name = "levelgen",', '  variant = "snowfield",', "  version = 2,", "  start_pad = %d," % start.id, "  rooms = {"]
    for r, R in sorted(ROOMS.items()):
        o.append('    ["%s"] = { id = %d, pads = { %s }, named = { %s } },' % (
            R.name, r, ", ".join(str(p.id) for p in pads if p.room is R and p.kind == "grid"),
            ", ".join('["%s"] = %d' % (p.name, p.id) for p in pads if p.room is R and p.kind == "named")))
    o += ["  },", "  room_name = { %s }," % ", ".join('[%d] = "%s"' % (r, R.name) for r, R in sorted(ROOMS.items())), "  doors = {},",
          "  grid = { %s }," % ", ".join(str(p.id) for p in pads if p.kind == "grid"),
          "  pads = { %s }," % ", ".join('["%s"] = %d' % (p.name, p.id) for p in pads if p.name),
          "  terrain = { cell = %d, play = { %d, %d, %d, %d }, apron = %d }," % (CELL, -PW, -PL, PW, PL, APRON),
          "}", ""]
    return "\n".join(o)


def strip_start_gun(setup):
    """the stock levelgen intro gives Bond a silenced PP7 and its ammo: the duel's loadout is its own (the mode gives the rifle)"""
    a = "    /* Type = StartWeapon; index = 2 */\n    _mkword(0, _mkshort(0, 1)), 5, -1, 0,\n"
    b = "    /* Type = StartAmmo; index = 3 */\n    _mkword(0, _mkshort(0, 2)), 1, 100, 0,\n"
    assert a in setup and b in setup, "the setup's intro changed shape"
    return setup.replace(a, "").replace(b, "").replace("/* Type = EndIntro; index = 4 */", "/* Type = EndIntro; index = 2 */")


# ---------------------------------------------------------- offline sight ---
def terrain_clear(a, b, step=150):
    (ax, ay, az), (bx, by, bz) = a, b
    n = max(2, int(math.hypot(bx - ax, bz - az) / step))
    for k in range(1, n):
        f = k / float(n)
        x, y, z = ax + (bx - ax) * f, ay + (by - ay) * f, az + (bz - az) * f
        if ground_y(x, z) > y:
            return False
    return True


# Named pads (new coordinates). Bond's shoulder (south, high ground); the midfield (behind the ridgelines' crests, 150 to 380 m); the
# flanks; the hunters' rise (north, 400 to 500 m). Each faces the other end. The duel's perch list is in tools/sniper_defs.lua.
SOUTH, NORTH = (0, -24000), (0, 24000)
NAMED = (
    [("b%d" % (k + 1), (x, -23650), NORTH) for k, x in enumerate((0, -5000, 5000, -10400, 10400))] +
    [("m%d" % (k + 1), xz, SOUTH) for k, xz in enumerate(((-12000, -9070), (-3000, -7450), (9600, -7290), (-13600, 3560), (3000, 7940),
                                                           (13200, 5350), (-6000, 11000), (16000, 14250), (-16500, -4000), (16500, -2500)))] +
    [("f%d" % (k + 1), xz, SOUTH) for k, xz in enumerate(((-14000, 18140), (-8400, 20090), (-2000, 22030), (5600, 20570), (11200, 21700),
                                                           (16000, 19440), (-5200, 25300), (4000, 25500), (-17000, 23500), (17200, 24200)))] +
    [("w%d" % (k + 1), xz, SOUTH) for k, xz in enumerate(((-18000, 9500), (-17500, 15000)))] +
    [("e%d" % (k + 1), xz, SOUTH) for k, xz in enumerate(((17800, 8800), (18000, 2000)))] +
    [("r%d" % (k + 1), xz, NORTH) for k, xz in enumerate(((-14000, -26000), (-3000, -14000), (6000, -16500), (14000, -26000), (0, -2000),
                                                           (-10000, 22000)))]
)


def main():
    os.makedirs(OUT, exist_ok=True)
    describe = "SEVERNAYA SNOWFIELD v2: heightfield %d x %d m (play %d x %d m), cell %d, %d x %d rooms" % (
        (X1 - X0) // 100, (Z1 - Z0) // 100, 2 * PW // 100, 2 * PL // 100, CELL, RX, RZ)
    drawn = build_ground()
    quads, nseg = build_skyline()
    tris, index = build_stan()
    if len(tris) > gl.MAX_TILES:
        sys.exit("%d stan tiles > %d" % (len(tris), gl.MAX_TILES))
    pads, grid, start, adj, group, gadj, members = build_pads(tris, index)
    bgc, bgh, stats, nports = gen_bg(describe)
    stan, nlinks = gen_stan(describe, tris)
    setup = gl.gen_setup(NAME, pads, start, adj, group, gadj, members, describe, None)
    setup = strip_start_gun(setup.replace("tools/gen_level.py", "tools/levelgen/build_snowfield.py"))
    lua = gen_lua(pads, start)
    for fn, text in (("bg_%s_all_p.c" % NAME, bgc), ("bg_%s_all_p.h" % NAME, bgh), ("Tbg_%s_all_p_stanZ.c" % NAME, stan),
                     ("Usetup%sZ.c" % NAME, setup), ("levelgen_layout.lua", lua)):
        with open(os.path.join(OUT, fn), "w", newline="\n") as f:
            f.write(text)
    ys = [y for row in HG for y in row]
    print(describe)
    print("  drawn cells %d, stan tiles %d (%d links), skyline %d quads on %d segments, portals %d" % (drawn, len(tris), nlinks, quads, nseg, nports))
    tot = sum(s[1] for s in stats.values())
    print("  bg: %d vertices, %d bytes uncompressed (largest room %d bytes)" % (sum(s[0] for s in stats.values()), tot, max(s[1] for s in stats.values())))
    print("  pads %d (grid %d), start %d at (%d, %d, %d); heights %d..%d" % (len(pads), len(grid), start.id, start.x, start.y, start.z, min(ys), max(ys)))
    b = [p for p in pads if p.name == "b1"][0]
    for p in pads:
        if p.kind == "named":
            eye = (b.x, b.y + 165, b.z)
            head = (p.x, p.y + 150, p.z)
            print("   %-4s pad %4d room %2d (%6d, %5d, %6d)  %3.0f m from b1  line to b1's eye %s" % (
                p.name, p.id, p.room.id, p.x, p.y, p.z, math.hypot(p.x - b.x, p.z - b.z) / 100,
                "clear" if terrain_clear(eye, head) else "blocked"))
    for fn in sorted(os.listdir(OUT)):
        print("  %-28s %8d bytes" % (fn, os.path.getsize(os.path.join(OUT, fn))))


if __name__ == "__main__":
    main()
