#!/usr/bin/env python3
"""SNIPER JUNGLE: a prototype sniper map, a big wide jungle valley (a yay/nay spike: AC wants a jungle "just like Jungle but plenty big/wide",
for long-range duels and, later, camouflaged snipers hiding in it).

    python3 tools/levelgen/build_jungle.py           -> port/levelgen_jungle/ (build with -DGE_LEVELGEN_EXTRA=port/levelgen_jungle
                                                         -DGE_LEVELGEN_EXTRA_NAME=jungle; GE_LEVELGEN=jungle picks it)

A heightfield like tools/levelgen/build_snowfield.py (one height function on one CELL grid gives both the drawn triangles and the stan tiles,
the same corners and the same diagonal), but bigger and split into a 4 x 4 grid of bg rooms joined by tall portals, as stock Jungle's outdoor
rooms are, so the rooms behind the camera are not drawn.
- the valley: about 410 x 410 m of play. Bond's ridge in the south, a meandering dry river ravine across the middle (rock banks, a dark
  bed, boulders; Jungle's bg has no water texture), rocky knolls with flat tops (perches), gullies (dead ground), flank ridges east and
  west, the hunters' ridge in the north (higher than Bond's);
- the perimeter: the ground rises (a walkable apron) into steep hillsides with rock faces; the stan ends at their foot; on the crest a ring
  of Jungle's own foliage wall (its six stacked bands) stands against the sky;
- the thickets: blobs of cells walled by Jungle's foliage bands with a leaf roof, each cell a stan hole (solid jungle: nobody walks in, the
  stan line stops at them), as stock Jungle's paths are walled; they sculpt the valley into lanes and clearings;
- the vegetation is stock Jungle's own props by model number (PROP_JUNGLE3_TREE, PROP_PALM, PROP_PALMTREE, the PROP_PLANT* shrubs) with
  Jungle's own flags: trees collide, stop bullets and block the AI's sight (the stan line tests objects); shrubs are visual-only cover
  (Jungle flags them "AI sees through" and shoot-through); boulders are bg geometry over a stan hole (they block walking, bullets, both
  lines of sight);
- the foliage cards are cut-outs (alpha-tested, z-written), not Jungle's blend: thousands of blended cards would each be one draw call in
  the port's back-to-front sort of translucent triangles;
- textures by number (Jungle's forest floor 2509, its rock 187, its foliage wall 830..825): the texels are read from the ROM at run time,
  nothing ROM-derived is written;
- the sky and fog are Jungle's own record from the ROM's table (bgfog.c, the levelgen slot when the level is "jungle"), with the far plane
  moved out for the long sight lines.
"""
import math
import os
import random
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.dirname(HERE)
ROOT = os.path.dirname(TOOLS)
sys.path.insert(0, TOOLS)
import gen_level as gl      # noqa: E402
import gen_flatland as gf   # noqa: E402

NAME = "levelgen"
OUT = os.path.join(ROOT, "port", "levelgen_jungle")
GALLERY = os.environ.get("JUNGLE_GALLERY")   # (development: a row of texture sample panels by Bond's start)

CELL = 600                      # one grid for the drawn ground and the stan
HALF = 28800                    # the grid spans -HALF..HALF in x and z
RN = int(os.environ.get("JUNGLE_ROOMS", "4"))   # bg rooms: RN x RN
PW = PL = 20400                 # the play area's half width (x) and half length (z)
RC = 9000                       # its corner radius
APRON = 2400                    # the walkable rise past the play area (the stan ends here)
CLIFF = 3600                    # the hillside's horizontal run
RING = APRON + CLIFF + 300      # the foliage-wall ring on the crest
DRAW = RING + 1500              # nothing is drawn past this
BASE = 600

GROUND, ROCK = 2509, 187
WALL = [830, 829, 828, 827, 826, 825]       # Jungle's foliage wall, bottom band to top band
K_GROUND = 3.0                  # ST units per world unit (Jungle's own densities: 31.5 / 28 / 25 ST per bg unit at its 0.0947 scale)
K_ROCK = 2.6
BAND = 600                      # one foliage-wall band's height
SEG = 460                       # one foliage-wall card's width

rng = random.Random(1997)


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
    qx, qz = abs(x) - (PW - RC), abs(z) - (PL - RC)
    return math.hypot(max(qx, 0.0), max(qz, 0.0)) + min(max(qx, qz), 0.0) - RC


def sdf(x, z):
    wob = 800.0 * math.sin(x / 2700.0 + 1.1) * math.cos(z / 3300.0) + 500.0 * math.sin(z / 1900.0 + x / 4300.0)
    return sdf_plain(x, z) + wob


# ---------------------------------------------------------------- terrain ---
def river_z(x):
    return 1300.0 * math.sin(x / 5200.0 + 0.6) + 650.0 * math.sin(x / 2100.0 + 2.0) + 400.0


RIVER_BED = 650                 # the bed's half width
RIVER_BANK = 1700               # the banks' run
RIVER_DEPTH = 1500

KNOLLS = [   # (x, z, height, radius): rocky knolls with flat tops (rock faces on the sides)
    (-8200, -8200, 1000, 2100), (8800, -7400, 850, 1900), (-9800, 8600, 1150, 2200), (6200, 9400, 1050, 2000),
    (600, -10600, 550, 1800), (-1800, 11800, 750, 1700), (13000, 3600, 700, 1600), (-13400, -1800, 650, 1600),
    (14600, -12800, 600, 1500), (-15000, 13800, 700, 1500),
]
GULLIES = [  # (a, b, depth, half width): dead ground running down toward the river
    ((-12500, -18000), (-6000, -2200), 520, 1000), ((4500, -17500), (9500, -2600), 480, 950),
    ((-3500, 17500), (-7000, 3400), 520, 1000), ((11000, 17000), (4500, 3800), 500, 950),
    ((-18000, 4500), (-11000, 1600), 420, 900), ((18000, -4500), (12000, -800), 420, 900),
]


def river_depth(x, z):
    """how far the ravine cuts at (x, z) (0 off it)"""
    d = abs(z - river_z(x))
    fade = smooth((PW + APRON + 2600 - abs(x)) / 4200.0)       # the gorge closes up into the hills at both ends
    if d < RIVER_BED:
        prof = 1.0 - 0.08 * (d / RIVER_BED) ** 2
    elif d < RIVER_BED + RIVER_BANK:
        t = (d - RIVER_BED) / float(RIVER_BANK)
        prof = 0.92 * (1.0 - t) ** 1.6
    else:
        prof = 0.0
    return RIVER_DEPTH * prof * fade


def field(x, z):
    y = 1700.0 * g(z, -18000, 3600) + 2400.0 * g(z, 17200, 4300)                      # Bond's ridge (south), the hunters' ridge (north)
    y += 1300.0 * g(x, -16800, 3200) * (0.65 + 0.35 * math.sin(z / 4100.0))             # the flank ridges
    y += 1300.0 * g(x, 16800, 3200) * (0.65 + 0.35 * math.cos(z / 3700.0 + 1.0))
    y -= 500.0 * g(x, 0, 12000) * g(z, 0, 9000)                                         # the valley's bowl
    for kx, kz, kh, kr in KNOLLS:
        d = math.hypot(x - kx, z - kz) / kr
        y += kh / (1.0 + d ** 6)                                                         # flat top, steep sides
    for a, b, dep, w in GULLIES:
        d, t = seg_dist(x, z, a, b)
        y -= dep * math.exp(-(d / float(w)) ** 2) * (0.4 + 0.6 * math.sin(math.pi * min(1.0, t * 1.1)))
    y += 120.0 * math.sin(x / 2100.0 + 0.4) * math.sin(z / 2500.0 + 0.7) + 70.0 * math.sin((x + 1.3 * z) / 1500.0)
    y += 35.0 * math.sin(x / 530.0 + z / 770.0) * math.cos(z / 610.0 - x / 980.0)
    return y


def height(x, z):
    y = BASE + 1700 + field(x, z) - river_depth(x, z)
    s = sdf(x, z)
    if s > 0:
        y += 0.18 * min(s, APRON)
    if s > APRON:
        c = min(1.0, (s - APRON) / float(CLIFF))
        sm = smooth(c)
        y += 3600.0 * sm * (0.72 + 0.28 * math.sin(x / 2300.0) * math.cos(z / 1900.0)) + 260.0 * math.sin(x / 800.0) * math.sin(z / 950.0) * c
        if s > APRON + CLIFF:
            y += 0.15 * (s - APRON - CLIFF)
    return y


# ------------------------------------------------------------------- grid ---
NX = NZ = 2 * HALF // CELL
XS = [-HALF + CELL * i for i in range(NX + 1)]
ZS = [-HALF + CELL * j for j in range(NZ + 1)]
HG = [[int(round(height(x, z))) for x in XS] for z in ZS]     # HG[j][i]
RSIZE = 2 * HALF // RN


def H(i, j):
    return HG[j][i]


def cell_centre(i, j):
    return XS[i] + CELL / 2.0, ZS[j] + CELL / 2.0


def cell_of(x, z):
    return int((x + HALF) // CELL), int((z + HALF) // CELL)


def room_of_cell(i, j):
    cx, cz = cell_centre(i, j)
    return room_at(cx, cz)


def room_at(x, z):
    ix = min(RN - 1, max(0, int((x + HALF) // RSIZE)))
    iz = min(RN - 1, max(0, int((z + HALF) // RSIZE)))
    return 1 + ix + RN * iz


def in_play(i, j):
    return 0 <= i < NX and 0 <= j < NZ and sdf(*cell_centre(i, j)) <= APRON


def is_drawn(i, j):
    return min(sdf(XS[a], ZS[b]) for a in (i, i + 1) for b in (j, j + 1)) <= DRAW


def P(i, j):
    return (XS[i], H(i, j), ZS[j])


def ground_y(x, z):
    """the drawn (and stan) surface's height at (x, z): the cell's triangle, diagonal (x0,z0)-(x1,z1)"""
    i, j = cell_of(x, z)
    i, j = max(0, min(NX - 1, i)), max(0, min(NZ - 1, j))
    fx, fz = (x - XS[i]) / float(CELL), (z - ZS[j]) / float(CELL)
    h00, h10, h01, h11 = H(i, j), H(i + 1, j), H(i, j + 1), H(i + 1, j + 1)
    if fx >= fz:
        return h00 + (h10 - h00) * fx + (h11 - h10) * fz
    return h00 + (h11 - h01) * fx + (h01 - h00) * fz


# -------------------------------------------------- named perches, Bond ---
# Bond's ridge (south), the hunters' ridge (north, 300 to 370 m: AC wants them as far as possible), the flanks (220 to 330 m), the knolls
# (150 to 250 m). Each faces the other end.
SOUTH, NORTH = (0, -18000), (0, 15000)
NAMED = (
    [("b%d" % (k + 1), xz, NORTH) for k, xz in enumerate(((0, -17600), (-2600, -17400), (2600, -17500), (-5200, -17000), (5200, -17100)))] +
    [("f%d" % (k + 1), xz, SOUTH) for k, xz in enumerate(((-11000, 15600), (-8200, 16600), (-5200, 17200), (-2200, 17600), (1000, 17500),
                                                           (4000, 17100), (7000, 16500), (9800, 15700), (-3600, 15000), (2600, 15000)))] +
    [("w%d" % (k + 1), xz, SOUTH) for k, xz in enumerate(((-16600, 6000), (-17000, 9200), (-16400, 12200), (-16800, 2800)))] +
    [("e%d" % (k + 1), xz, SOUTH) for k, xz in enumerate(((16600, 5600), (17000, 8800), (16400, 11800), (16800, 2400)))] +
    [("k%d" % (k + 1), xz, SOUTH) for k, xz in enumerate(((-9800, 8600), (6200, 9400), (-1800, 11800), (-15000, 13800), (13000, 3600),
                                                           (-9000, 9900), (7300, 10200)))]
)
NAMED_XZ = {n: xz for n, xz, _ in NAMED}
# the sight lanes: from b1 to these perches no hole (thicket, boulder) and no tree stands near the line in x-z (the stan line, the AI's
# sight, walks the tiles flat: it is stopped by a hole or a tree under the line however high the line runs)
LANE_PERCHES = ("f2", "f4", "f5", "f7", "f9", "w1", "w2", "e1", "e3", "k1", "k2", "k3")
LANES = [(nm, xz) for nm, xz, _ in NAMED if nm in LANE_PERCHES]


def lane_dist(x, z):
    bx, bz = NAMED_XZ["b1"]
    return min(seg_dist(x, z, (bx, bz), xz)[0] for _, xz in LANES)


# --------------------------------------------------------------- boulders ---
def place_boulders():
    """(i, j, radius, height): a boulder centred on a play cell (the cell is a stan hole)"""
    out, taken = [], set()

    def ok(i, j):
        if not in_play(i, j) or (i, j) in taken:
            return False
        cx, cz = cell_centre(i, j)
        if any(math.hypot(cx - x, cz - z) < 1500 for x, z in NAMED_XZ.values()) or lane_dist(cx, cz) < 1100:
            return False
        return all(in_play(i + a, j + b) and (i + a, j + b) not in taken for a in (-1, 0, 1) for b in (-1, 0, 1))

    def add(i, j, r, h):
        if ok(i, j):
            taken.add((i, j))
            out.append((i, j, r, h))
    # the river bed and its banks
    for x in range(-PW + 2500, PW - 2500, 1700):
        x2 = x + rng.uniform(-500, 500)
        z2 = river_z(x2) + rng.choice((-1, 1)) * rng.uniform(200, RIVER_BED + 900)
        add(*cell_of(x2, z2), rng.uniform(230, 300), rng.uniform(170, 330))
    # the knolls' rims (cover on the perches' flanks) and scattered ones
    for kx, kz, kh, kr in KNOLLS:
        for _ in range(3):
            a = rng.uniform(0, 2 * math.pi)
            add(*cell_of(kx + math.cos(a) * kr * 0.95, kz + math.sin(a) * kr * 0.95), rng.uniform(240, 300), rng.uniform(220, 380))
    for _ in range(40):
        add(*cell_of(rng.uniform(-PW + 1500, PW - 1500), rng.uniform(-PL + 1500, PL - 1500)), rng.uniform(220, 300), rng.uniform(150, 300))
    return out


BOULDERS = place_boulders()
HOLES = {(i, j) for i, j, _, _ in BOULDERS}


# --------------------------------------------------------------- thickets ---
# Stock Jungle's look: its paths are walled by tall foliage cards. Here the thickets are blobs of cells walled by Jungle's foliage wall (its
# lower bands) with a leaf roof (its top band); the cells are stan holes, so a thicket is solid jungle: nobody walks in, the stan line (the AI's
# sight) stops at it, and the drawn cards hide what is behind. They sculpt the valley into lanes and clearings.
N_THICKETS = 48
THICKET_BANDS = [830, 829, 828, 827]
ROOF = 825


def place_thickets():
    centres, cells = [], set()
    tries = 0
    while len(centres) < N_THICKETS and tries < 5000:
        tries += 1
        x, z = rng.uniform(-PW + 2000, PW - 2000), rng.uniform(-PL + 2000, PL - 2000)
        if sdf(x, z) > -1500 or min(math.hypot(x - nx, z - nz) for nx, nz in NAMED_XZ.values()) < 3600:
            continue
        if lane_dist(x, z) < 2000 or abs(z - river_z(x)) < RIVER_BED + 900:
            continue
        if any(math.hypot(x - cx, z - cz) < 3300 for cx, cz, _, _ in centres):
            continue
        centres.append((x, z, rng.uniform(1100, 2500), rng.uniform(0, 2 * math.pi)))
    for cx, cz, r, ph in centres:
        i0, j0 = cell_of(cx - r * 1.3, cz - r * 1.3)
        i1, j1 = cell_of(cx + r * 1.3, cz + r * 1.3)
        for j in range(j0, j1 + 1):
            for i in range(i0, i1 + 1):
                x, z = cell_centre(i, j)
                a = math.atan2(z - cz, x - cx)
                rr = r * (0.8 + 0.22 * math.sin(3 * a + ph) + 0.12 * math.sin(5 * a + 2 * ph))
                if math.hypot(x - cx, z - cz) > rr or not in_play(i, j) or (i, j) in HOLES or sdf(x, z) > APRON - 900:
                    continue
                if lane_dist(x, z) < 1300 or abs(z - river_z(x)) < RIVER_BED + 500:
                    continue
                if min(math.hypot(x - nx, z - nz) for nx, nz in NAMED_XZ.values()) < 2600:
                    continue
                cells.add((i, j))
    # no lone cells, no one-cell slivers
    for _ in range(2):
        cells = {c for c in cells if sum((c[0] + a, c[1] + b) in cells for a, b in ((1, 0), (-1, 0), (0, 1), (0, -1))) >= 2}
    return cells, centres


THICKET, THICKET_CENTRES = place_thickets()
HOLES |= THICKET


def thicket_dist():
    """grid distance (in cells) of every cell to the nearest thicket cell"""
    dist = {c: 0 for c in THICKET}
    todo = list(THICKET)
    while todo:
        nxt = []
        for i, j in todo:
            d = dist[(i, j)] + 1
            if d > 6:
                continue
            for a, b in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                k = (i + a, j + b)
                if k not in dist:
                    dist[k] = d
                    nxt.append(k)
        todo = nxt
    return dist


TDIST = thicket_dist()


def is_stan(i, j):
    return in_play(i, j) and (i, j) not in HOLES


# ------------------------------------------------------------------ props ---
# (model, extrascales, flags, flags2, collision radius): Jungle's own records
TREE_TYPES = [
    (108, (153, 204, 204, 256), 0x00020101, 0x0, 230),     # PROP_JUNGLE3_TREE, the big trunk
    (109, (204, 256, 256), 0x00020101, 0x0, 100),      # PROP_PALM
    (110, (512, 640), 0x00020101, 0x0, 110),               # PROP_PALMTREE
]
PLANT_TYPES = [
    (316, (102, 153, 153, 204), 0x04000001, 0x0200c100),   # PROP_PLANT2 (healthy, thick)
    (317, (153, 204), 0x04000001, 0x0200c100),             # PROP_PLANT3 (tall leaves)
    (111, (281, 384, 486), 0x04000001, 0x0200c100),        # PROP_PLANT2B (low shrub)
    (315, (153, 204), 0x04000001, 0x0200c100),             # PROP_PLANT11
    (314, (153,), 0x04000001, 0x0200c100),                 # PROP_PLANT1
]


def tree_density(x, z):
    d = 0.28
    td = TDIST.get(cell_of(x, z), 99)
    if td <= 2:
        d += 0.6                                      # the thickets' edges: the forest proper
    elif td <= 4:
        d += 0.3
    dr = abs(z - river_z(x))
    if RIVER_BED + 300 < dr < RIVER_BED + RIVER_BANK + 2600:
        d += 0.25                                     # the river's gallery forest
    elif dr <= RIVER_BED + 300:
        d -= 0.5                                      # the bed itself
    for a, b, dep, w in GULLIES:
        if seg_dist(x, z, a, b)[0] < w * 1.3:
            d += 0.2
    for kx, kz, kh, kr in KNOLLS:
        if math.hypot(x - kx, z - kz) < kr * 0.8:
            d -= 0.45                                 # the knolls' tops are open
    if z < -15500 or z > 14500:
        d -= 0.35                                     # the two ridges are thin
    ld = lane_dist(x, z)
    if ld < 950:
        d -= 1.5                                      # the sight lanes
    elif ld < 1500:
        d -= 0.3
    return d


TREE_SPACING = int(os.environ.get("JUNGLE_TREE_SPACING", "900"))     # the props' sampling grids (the draw count: each prop is a few draws)
SHRUB_SPACING = int(os.environ.get("JUNGLE_SHRUB_SPACING", "1600"))


def place_props():
    trees, plants = [], []
    sp = TREE_SPACING
    for gx in range(-PW, PW + 1, sp):
        for gz in range(-PL, PL + 1, sp):
            x, z = gx + rng.uniform(-330, 330), gz + rng.uniform(-330, 330)
            i, j = cell_of(x, z)
            if not is_stan(i, j) or sdf(x, z) > APRON - 600:
                continue
            if rng.random() > tree_density(x, z):
                continue
            if any(math.hypot(x - nx, z - nz) < 900 for nx, nz in NAMED_XZ.values()):
                continue
            if any(abs(i - bi) <= 1 and abs(j - bj) <= 1 for bi, bj, _, _ in BOULDERS):
                continue
            if any(math.hypot(x - t[0], z - t[1]) < 520 for t in trees[-80:]):
                continue
            u = rng.random()
            tt = TREE_TYPES[0] if u < 0.42 else TREE_TYPES[1] if u < 0.9 else TREE_TYPES[2]
            trees.append((x, z, tt[0], rng.choice(tt[1]), tt[2], tt[3], tt[4], rng.uniform(0, 360)))
    sp = SHRUB_SPACING
    for gx in range(-PW, PW + 1, sp):
        for gz in range(-PL, PL + 1, sp):
            x, z = gx + rng.uniform(-400, 400), gz + rng.uniform(-400, 400)
            i, j = cell_of(x, z)
            if not is_stan(i, j) or sdf(x, z) > APRON - 300:
                continue
            dens = 0.25 + 0.6 * max(0.0, tree_density(x, z) + 0.3 if lane_dist(x, z) > 1100 else 0.35)
            near_named = min(math.hypot(x - nx, z - nz) for nx, nz in NAMED_XZ.values())
            if near_named < 350 or min(math.hypot(x - bx, z - bz) for nm, (bx, bz) in NAMED_XZ.items() if nm[0] == "b") < 900:
                continue
            if near_named < 1200:
                dens = 0.9                                # shrubs by the perches: somewhere to hide
            if rng.random() > min(0.85, dens):
                continue
            if any(math.hypot(x - t[0], z - t[1]) < t[6] + 200 for t in trees):
                continue
            u = rng.random()
            pt = PLANT_TYPES[0] if u < 0.42 else PLANT_TYPES[1] if u < 0.6 else PLANT_TYPES[2] if u < 0.85 else PLANT_TYPES[3] if u < 0.95 else PLANT_TYPES[4]
            plants.append((x, z, pt[0], rng.choice(pt[1]), pt[2], pt[3], 60, rng.uniform(0, 360)))
    return trees, plants


TREES, PLANTS = place_props()


# ---------------------------------------------------------------- shading ---
SUN = (0.45, 0.62, -0.64)
_sl = math.sqrt(sum(q * q for q in SUN))
SUN = tuple(q / _sl for q in SUN)
TINT_G = (176, 150, 116)
TINT_R = (150, 144, 128)
SHADE = {}


def canopy_shade():
    """how many trees stand over each grid vertex (darker forest floor under them)"""
    cnt = {}
    for t in TREES:
        ci, cj = cell_of(t[0], t[1])
        for a in range(-1, 3):
            for b in range(-1, 3):
                k = (ci + a, cj + b)
                d = math.hypot(XS[min(NX, max(0, k[0]))] - t[0], ZS[min(NZ, max(0, k[1]))] - t[1])
                if d < 900:
                    cnt[k] = cnt.get(k, 0) + (1.0 - d / 900.0)
    return cnt


SHADE = canopy_shade()


def vnormal(i, j):
    i0, i1 = max(0, i - 1), min(NX, i + 1)
    j0, j1 = max(0, j - 1), min(NZ, j + 1)
    dx = (H(i1, j) - H(i0, j)) / float(XS[i1] - XS[i0])
    dz = (H(i, j1) - H(i, j0)) / float(ZS[j1] - ZS[j0])
    ln = math.sqrt(dx * dx + 1 + dz * dz)
    return (-dx / ln, 1 / ln, -dz / ln)


def vcol(i, j, tint):
    n = vnormal(i, j)
    d = max(0.0, n[0] * SUN[0] + n[1] * SUN[1] + n[2] * SUN[2])
    b = 0.36 + 0.62 * d
    b *= 1.0 - 0.28 * min(1.0, SHADE.get((i, j), 0.0))
    x, z = XS[i], ZS[j]
    if abs(z - river_z(x)) < RIVER_BED + 150 and river_depth(x, z) > 500:
        b *= 0.6                                      # the ravine's damp bed
    return tuple(max(0, min(255, int(round(c * b)))) for c in tint)


def normal(t):
    a, b, c = t
    u = [b[k] - a[k] for k in range(3)]
    v = [c[k] - a[k] for k in range(3)]
    n = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
    ln = math.sqrt(sum(q * q for q in n)) or 1.0
    n = tuple(q / ln for q in n)
    return n if n[1] >= 0 else tuple(-q for q in n)


def st_of(p, tex, n):
    x, y, z = p
    if tex == GROUND or abs(n[1]) > 0.8:
        k = K_GROUND if tex == GROUND else K_ROCK
        return x * k, z * k
    k = K_ROCK
    u = z if abs(n[0]) > abs(n[2]) else x
    return u * k, (-y + 0.25 * u + 600.0 * math.sin(x / 2300.0 + z / 3700.0)) * k


# -------------------------------------------------------------- the meshes ---
class RoomMesh:
    def __init__(self):
        self.pri = {}            # tex -> [triangle]; a triangle = 3 x (x, y, z, s, t, (r, g, b))
        self.sec = {}            # (group, tex) -> [triangle]

    def tri(self, tex, pts, sec=None):
        (self.sec.setdefault((sec, tex), []) if sec else self.pri.setdefault(tex, [])).append(pts)


MESH = {r: RoomMesh() for r in range(1, RN * RN + 1)}


def build_ground():
    drawn = 0
    for j in range(NZ):
        for i in range(NX):
            if not is_drawn(i, j):
                continue
            drawn += 1
            m = MESH[room_of_cell(i, j)]
            corners = {(a, b): P(a, b) for a in (i, i + 1) for b in (j, j + 1)}
            t1 = ((i, j), (i + 1, j + 1), (i + 1, j))
            t2 = ((i, j), (i, j + 1), (i + 1, j + 1))
            for t in (t1, t2):
                pts = [corners[k] for k in t]
                n = normal(pts)
                tex = GROUND if n[1] > 0.72 else ROCK
                tint = TINT_G if tex == GROUND else TINT_R
                m.tri(tex, [p + st_of(p, tex, n) + (vcol(k[0], k[1], tint),) for p, k in zip(pts, t)])
    return drawn


def build_boulders():
    for i, j, r, h in BOULDERS:
        cx, cz = cell_centre(i, j)
        m = MESH[room_of_cell(i, j)]
        y0 = ground_y(cx, cz) - 60
        n = 7
        a0 = rng.uniform(0, 2 * math.pi)
        base, mid = [], []
        for k in range(n):
            a = a0 + 2 * math.pi * k / n
            rr = r * rng.uniform(0.8, 1.05)
            base.append((cx + math.cos(a) * rr, min(y0, ground_y(cx + math.cos(a) * rr, cz + math.sin(a) * rr) - 40), cz + math.sin(a) * rr))
            a2 = a + math.pi / n
            mid.append((cx + math.cos(a2) * rr * 0.82, y0 + h * rng.uniform(0.5, 0.7), cz + math.sin(a2) * rr * 0.82))
        top = (cx + rng.uniform(-60, 60), y0 + h, cz + rng.uniform(-60, 60))
        tris = []
        for k in range(n):
            b0, b1, m0, m1 = base[k], base[(k + 1) % n], mid[(k - 1) % n], mid[k]
            tris += [(b0, b1, m1), (b0, m1, m0), (m1, top, m0)]
            tris.append((b1, mid[(k + 1) % n], m1))
        for t in tris:
            nn = normal(t)
            d = max(0.0, nn[0] * SUN[0] + nn[1] * SUN[1] + nn[2] * SUN[2])
            col = tuple(int(c * (0.42 + 0.55 * d)) for c in TINT_R)
            m.tri(ROCK, [(p[0], p[1], p[2]) + (p[0] * K_ROCK, (p[1] + p[2] * 0.3) * K_ROCK, col) for p in t])
    return len(BOULDERS)


def build_thickets():
    """each thicket cell's open sides get a wall of Jungle's foliage bands (the cell corners' own heights, so the wall stands on the
    drawn ground exactly where the stan hole begins) and the cell a leaf roof at the wall's top"""
    quads = 0
    top = len(THICKET_BANDS) * BAND
    for i, j in sorted(THICKET):
        m = MESH[room_of_cell(i, j)]
        for (a, b), e in (((-1, 0), ((i, j + 1), (i, j))), ((1, 0), ((i + 1, j), (i + 1, j + 1))),
                          ((0, -1), ((i, j), (i + 1, j))), ((0, 1), ((i + 1, j + 1), (i, j + 1)))):
            if (i + a, j + b) in THICKET:
                continue
            (ia, ja), (ib, jb) = e
            pa, pb = P(ia, ja), P(ib, jb)
            for q, tex in enumerate(THICKET_BANDS):
                ya0, yb0 = pa[1] - 80 + q * BAND, pb[1] - 80 + q * BAND
                c0, c1 = wall_col(q / float(len(THICKET_BANDS))), wall_col((q + 1) / float(len(THICKET_BANDS)))
                p = [(pa[0], ya0 + BAND, pa[2], 8, 1006, c1), (pb[0], yb0 + BAND, pb[2], 1015, 1006, c1),
                     (pb[0], yb0, pb[2], 1015, 17, c0), (pa[0], ya0, pa[2], 8, 17, c0)]
                m.tri(tex, [p[0], p[1], p[2]], sec="wall")
                m.tri(tex, [p[0], p[2], p[3]], sec="wall")
                quads += 1
        c = [P(i, j), P(i + 1, j), P(i + 1, j + 1), P(i, j + 1)]
        st = [(8, 17), (1015, 17), (1015, 1006), (8, 1006)]
        col = (120, 140, 104)
        p = [(v[0], v[1] - 80 + top, v[2], s, t, col) for v, (s, t) in zip(c, st)]
        m.tri(ROOF, [p[0], p[1], p[2]], sec="wall")
        m.tri(ROOF, [p[0], p[2], p[3]], sec="wall")
        quads += 1
    return quads


def ring_points():
    hw, hl, r = PW - RC, PL - RC, RC + RING
    pts = []

    def arc(cx, cz, a0, a1):
        n = max(2, int(math.ceil(abs(a1 - a0) * r / SEG)))
        for k in range(n):
            a = a0 + (a1 - a0) * k / float(n)
            pts.append((cx + r * math.cos(a), cz + r * math.sin(a)))

    def line(ax, az, bx, bz):
        n = max(1, int(round(math.hypot(bx - ax, bz - az) / SEG)))
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
    # follow the wobbly boundary: push each point out along its normal until the sdf reaches RING
    out = []
    for x, z in pts:
        ln = math.hypot(x, z) or 1.0
        ux, uz = x / ln, z / ln
        for _ in range(12):
            s = sdf(x, z) - RING
            if abs(s) < 30:
                break
            x, z = x - ux * s * 0.8, z - uz * s * 0.8
        out.append((x, z))
    return out


def wall_col(f):
    v = 70 + 120 * f ** 0.7
    return (int(v * 0.95), int(v), int(v * 0.88))


def build_wall():
    """Jungle's foliage wall on the crest: six stacked bands (830 at the foot .. 825 at the top), one 32-texel image a card"""
    pts = ring_points()
    n = len(pts)
    quads = 0
    for k in range(n):
        a, b = pts[k], pts[(k + 1) % n]
        ya, yb = ground_y(*a) - 250.0, ground_y(*b) - 250.0
        m = MESH[room_at((a[0] + b[0]) / 2, (a[1] + b[1]) / 2)]
        for q, tex in enumerate(WALL):
            a0, a1 = ya + q * BAND, ya + (q + 1) * BAND
            b0, b1 = yb + q * BAND, yb + (q + 1) * BAND
            c0, c1 = wall_col(q / float(len(WALL))), wall_col((q + 1) / float(len(WALL)))   # one smooth shade up the wall
            p = [(a[0], a1, a[1], 8, 1006, c1), (b[0], b1, b[1], 1015, 1006, c1), (b[0], b0, b[1], 1015, 17, c0), (a[0], a0, a[1], 8, 17, c0)]
            m.tri(tex, [p[0], p[1], p[2]], sec="wall")
            m.tri(tex, [p[0], p[2], p[3]], sec="wall")
            quads += 1
    return quads, n


def build_gallery():
    """development only: one 3 m panel per candidate texture, a row 10 m in front of b1"""
    bx, bz = NAMED_XZ["b1"]
    cands = [2509, 187, 2535, 2345, 830, 829, 828, 827, 826, 825, 2486, 2534]
    for k, tex in enumerate(cands):
        x0 = bx - 2900 + k * 320
        z0 = bz + 1500
        y0 = ground_y(x0, z0) + 20
        m = MESH[room_at(x0, z0)]
        p = [(x0, y0 + 300, z0, 0, 1024, (255, 255, 255)), (x0 + 280, y0 + 300, z0, 1024, 1024, (255, 255, 255)),
             (x0 + 280, y0, z0, 1024, 0, (255, 255, 255)), (x0, y0, z0, 0, 0, (255, 255, 255))]
        m.tri(tex, [p[0], p[1], p[2]], sec="wall" if tex in WALL else None)
        m.tri(tex, [p[0], p[2], p[3]], sec="wall" if tex in WALL else None)
    for q, tex in enumerate(WALL):    # the wall's bands stacked, as on the crest
        x0, z0 = bx + 1200, bz + 1500
        y0 = ground_y(x0, z0) + 20 + q * 300
        m = MESH[room_at(x0, z0)]
        p = [(x0, y0 + 300, z0, 8, 1006, (255, 255, 255)), (x0 + 400, y0 + 300, z0, 1015, 1006, (255, 255, 255)),
             (x0 + 400, y0, z0, 1015, 17, (255, 255, 255)), (x0, y0, z0, 8, 17, (255, 255, 255))]
        m.tri(tex, [p[0], p[1], p[2]], sec="wall")
        m.tri(tex, [p[0], p[2], p[3]], sec="wall")


# ------------------------------------------------------------------- bg ---
PRI_HEAD = [(0xE7000000, 0), (0xBA001402, 0x00100000), (0xB900031D, 0x0C182078), (0xFC26A004, 0x1F1093FF), (0xBA001001, 0x00010000),
            (0xBB002801, 0xFFFFFFFF)]
PRI_MID = [(0xBA001102, 0), (0xBA000C02, 0x00002000), (0xB6000000, 0x00003000), (0xFB000000, 0x000000FF)]
# Jungle's own foliage-wall state (its sec display lists: combiner, texture command), culling off, and the render mode made a cut-out
# (the opaque mode plus CVG_X_ALPHA: alpha-tested, z-written, drawn in batches) instead of Jungle's blend: thousands of blended cards
# go through the port's back-to-front sort of every translucent triangle, one draw call each
SEC_STATE = {
    "wall": ([(0xE7000000, 0), (0xBA001402, 0x00100000), (0xB900031D, 0x0C183078), (0xFC26A004, 0x1F1093FF), (0xBA001001, 0x00010000),
              (0xBB002801, 0xFFFFFFFF)],
             [(0xBA001102, 0), (0xBA000C02, 0x00002000), (0xB6000000, 0x00003000), (0xFB000000, 0x000000FF)], 0xC0180002),
}
ST_LIMIT = 30000


def emit_tris(tris, nv0):
    """vertex bytes and display list words of a run of triangles of one texture: shared vertices, up to 16 a load, every triangle of the
    load drawn by tri4 commands; each load's texture coordinates shifted by a whole number of 128-texel periods (seamless)"""
    vbytes, dl = b"", b""
    nv = nv0
    verts, index, btris = [], {}, []
    smin = tmin = 1e18
    smax = tmax = -1e18

    def flush():
        nonlocal vbytes, dl, nv, verts, index, btris, smin, smax, tmin, tmax
        if not verts:
            return
        ds = gl.ST_ALIGN * math.floor(smin / gl.ST_ALIGN)
        dt = gl.ST_ALIGN * math.floor(tmin / gl.ST_ALIGN)
        for (x, y, z, s, t, col) in verts:
            si, tj = int(round(s - ds)), int(round(t - dt))
            assert -32768 <= si <= 32767 and -32768 <= tj <= 32767, (si, tj)
            vbytes += gf.vtx(int(round(x)), int(round(y)), int(round(z)), si, tj, tuple(col) + (0xFF,))
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


def room_binaries(m):
    vb, pri, sec = b"", b"", b""
    nv = 0
    for w in PRI_HEAD:
        pri += gf.gfx(*w)
    for ti, tex in enumerate(sorted(m.pri)):
        pri += gf.gfx(0xC0080002, tex)
        if ti == 0:
            for w in PRI_MID:
                pri += gf.gfx(*w)
        v, d, nv = emit_tris(m.pri[tex], nv)
        vb += v
        pri += d
    pri += gf.gfx(0xB8000000, 0)
    groups = sorted(set(gname for gname, _ in m.sec))
    for gname in groups:
        head, mid, texcmd = SEC_STATE[gname]
        for w in head:
            sec += gf.gfx(*w)
        for ti, tex in enumerate(sorted(t for gn, t in m.sec if gn == gname)):
            sec += gf.gfx(texcmd, tex)
            if ti == 0:
                for w in mid:
                    sec += gf.gfx(*w)
            v, d, nv = emit_tris(m.sec[(gname, tex)], nv)
            vb += v
            sec += d
    if sec:
        sec += gf.gfx(0xB8000000, 0)
    return vb, pri, sec


def portals():
    """the tall portals between neighbouring rooms (vertical rectangles on the room grid lines, from under the ground to over the
    foliage wall's top): (index, room a, room b, 4 points)"""
    out = []
    y0, y1 = -2000.0, 16000.0
    for iz in range(RN):
        for ix in range(RN):
            a = 1 + ix + RN * iz
            z0, z1 = -HALF + RSIZE * iz, -HALF + RSIZE * (iz + 1)
            x0, x1 = -HALF + RSIZE * ix, -HALF + RSIZE * (ix + 1)
            if ix + 1 < RN:
                f = float(x1)
                out.append((len(out), a, a + 1, [(f, y1, z0), (f, y1, z1), (f, y0, z1), (f, y0, z0)]))
            if iz + 1 < RN:
                f = float(z1)
                out.append((len(out), a, a + RN, [(x0, y1, f), (x1, y1, f), (x1, y0, f), (x0, y0, f)]))
    return out


def gen_bg(describe):
    rooms = sorted(MESH)
    ports = portals()
    out = [gl.HEADER_NOTE.replace("gen_level.py", "levelgen/build_jungle.py") % ("bg_%s_all_p.c" % NAME, describe),
           '#include "../../assets/obseg/bg/bg_all_p.h"', '#include "bg_%s_all_p.h"' % NAME, "",
           "struct bg_header header = {0, &room_data_table, &portal_data_table, &global_visibility_commands, 0};", "",
           "struct room_data_table_entry room_data_table[] = {", "    {0},"]
    bins = {r: room_binaries(MESH[r]) for r in rooms}
    for r in rooms:
        sec = "&sec_mapping_binary_%d" % r if bins[r][2] else "0"
        out.append("    {&point_table_binary_%d, &pri_mapping_binary_%d, %s, 0.0, 0.0, 0.0},   /* cell %d,%d */" % (
            r, r, sec, (r - 1) % RN, (r - 1) // RN))
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
    Y = (lambda x, z: HG[(z + HALF) // CELL][(x + HALF) // CELL])
    for r in sorted(by):
        for i, j in by[r]:
            for sub, t in enumerate(gl.cell_tris(XS[i], ZS[j], XS[i + 1], ZS[j + 1], Y)):
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
    out = [gl.HEADER_NOTE.replace("gen_level.py", "levelgen/build_jungle.py") % (sym + ".c", describe),
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
for _r in range(1, RN * RN + 1):
    R = Room()
    R.id, R.name = _r, "j%d%d" % ((_r - 1) % RN, (_r - 1) // RN)
    ROOMS[_r] = R


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


ALL_TREES = None


def clear_of_trees(ax, az, bx, bz, extra=70):
    for t in ALL_TREES:
        if seg_dist(t[0], t[1], (ax, az), (bx, bz))[0] < t[6] + extra:
            return False
    return True


def seg_on_stan(ax, az, bx, bz):
    n = max(2, int(math.hypot(bx - ax, bz - az) // 120))
    for k in range(n + 1):
        x = ax + (bx - ax) * k / float(n)
        z = az + (bz - az) * k / float(n)
        if not is_stan(*cell_of(x, z)):
            return False
        # a boulder's cell neighbours: keep the line a body's width off the hole
        for a, b in ((60, 0), (-60, 0), (0, 60), (0, -60)):
            if not is_stan(*cell_of(x + a, z + b)):
                return False
    return True


def link_ok(p, q):
    return seg_on_stan(p.x, p.z, q.x, q.z) and clear_of_trees(p.x, p.z, q.x, q.z)


def build_pads(tris, index):
    """the waypoint pads (a grid every 3 cells, clear of trees and holes, then the named perches and the start), joined; then the prop pads"""
    cand = {}
    for j in range(1, NZ, 3):
        for i in range(1, NX, 3):
            if all(is_stan(i + a, j + b) for a in (-1, 0, 1) for b in (-1, 0, 1)):
                x, z = cell_centre(i, j)
                x, z = x + 60, z - 110
                if any(math.hypot(x - t[0], z - t[1]) < t[6] + 120 for t in ALL_TREES):
                    continue
                cand[(i, j)] = (x, z)
    named = [(nm, xz, face) for nm, xz, face in NAMED]
    # links first (on plain coordinates), then keep the component that holds the start
    nodes = [("g", k, xz) for k, xz in cand.items()] + [("n", nm, xz) for nm, xz, _ in named]
    pos = {}
    for kind, key, xz in nodes:
        pos[(kind, key)] = xz
    adj = {k: set() for k in pos}

    class Q:
        pass

    def pt(k):
        o = Q()
        o.x, o.z = pos[k]
        return o
    for (i, j) in cand:
        for a, b in ((3, 0), (0, 3), (3, 3), (3, -3)):
            k2 = ("g", (i + a, j + b))
            if k2 in pos and link_ok(pt(("g", (i, j))), pt(k2)):
                adj[("g", (i, j))].add(k2)
                adj[k2].add(("g", (i, j)))
    gk = [k for k in pos if k[0] == "g"]
    for nm, xz, _ in named:
        k = ("n", nm)
        near = sorted(gk, key=lambda q: math.hypot(pos[q][0] - xz[0], pos[q][1] - xz[1]))[:6]
        for q in near:
            if link_ok(pt(k), pt(q)):
                adj[k].add(q)
                adj[q].add(k)
        for nm2, xz2, _ in named:   # perches of one cluster: their own direct links
            if nm2 != nm and math.hypot(xz2[0] - xz[0], xz2[1] - xz[1]) < 4000 and link_ok(pt(k), pt(("n", nm2))):
                adj[k].add(("n", nm2))
                adj[("n", nm2)].add(k)
    start = ("n", "b1")
    seen, todo = {start}, [start]
    while todo:
        u = todo.pop()
        for v in adj[u]:
            if v not in seen:
                seen.add(v)
                todo.append(v)
    lost = [k for k in pos if k not in seen]
    if any(k[0] == "n" for k in lost):
        sys.exit("named perches cut off from b1: %s" % [k[1] for k in lost if k[0] == "n"])
    keys = [k for k in gk if k in seen] + [("n", nm) for nm, _, _ in named]
    pads, ids = [], {}
    for k in keys:
        if k[0] == "g":
            p = make_pad(tris, index, pos[k][0], pos[k][1], "grid")
        else:
            nm, xz, face = [n for n in named if n[0] == k[1]][0]
            p = make_pad(tris, index, xz[0], xz[1], "named", nm, (face[0] - xz[0], face[1] - xz[1]))
        if p is None:
            sys.exit("pad %s: not on the stan" % (k,))
        p.id = len(pads)
        ids[k] = p.id
        pads.append(p)
    st = [n for n in named if n[0] == "b1"][0]
    start_pad = make_pad(tris, index, st[1][0], st[1][1], "start", "start", (st[2][0] - st[1][0], st[2][1] - st[1][1]))
    start_pad.id = len(pads)
    pads.append(start_pad)
    padj = {p.id: set() for p in pads}
    for k in keys:
        for v in adj[k]:
            if v in ids:
                padj[ids[k]].add(ids[v])
    padj[start_pad.id].add(ids[("n", "b1")])
    padj[ids[("n", "b1")]].add(start_pad.id)
    for p in pads:
        if not padj[p.id]:
            sys.exit("pad %s has no waypoint link" % p.name)
    group = {p.id: 0 for p in pads}
    return pads, padj, group, {0: set()}, {0: [p.id for p in pads]}, start_pad, len(lost)


class Objects:
    def __init__(self, first_pad):
        self.opads, self.bpads, self.recs = [], [], []
        self.next_pad = first_pad


def build_props(tris, index, first_pad):
    O = Objects(first_pad)
    placed = {"tree": 0, "plant": 0}
    for kind, lst in (("tree", TREES), ("plant", PLANTS)):
        for x, z, model, scale, flags, flags2, rad, rot in lst:
            a = math.radians(rot)
            p = make_pad(tris, index, x, z, "object", "%s%d" % (kind, model), (math.sin(a), math.cos(a)))
            if p is None:
                continue
            p.id = O.next_pad
            O.next_pad += 1
            O.opads.append(p)
            O.recs.append(("StandardProp %s %d" % (kind, model), gl.obj_head(gl.PROPDEF["prop"], model, p.id, flags, flags2, 1000, scale)))
            placed[kind] += 1
    return O, placed


def gen_lua(pads, start, nlost):
    o = ["-- Generated by tools/levelgen/build_jungle.py - do not edit; rerun the script.",
         "-- SNIPER JUNGLE for mode scripts: include(\"levelgen_layout_jungle\") (modding/levelgen.md).",
         "LEVEL = {", '  name = "levelgen",', '  variant = "jungle",', "  start_pad = %d," % start.id, "  rooms = {"]
    for r, R in sorted(ROOMS.items()):
        o.append('    ["%s"] = { id = %d, pads = { %s }, named = { %s } },' % (
            R.name, r, ", ".join(str(p.id) for p in pads if p.room is R and p.kind == "grid"),
            ", ".join('["%s"] = %d' % (p.name, p.id) for p in pads if p.room is R and p.kind == "named")))
    o += ["  },", "  room_name = { %s }," % ", ".join('[%d] = "%s"' % (r, R.name) for r, R in sorted(ROOMS.items())), "  doors = {},",
          "  grid = { %s }," % ", ".join(str(p.id) for p in pads if p.kind == "grid"),
          "  pads = { %s }," % ", ".join('["%s"] = %d' % (p.name, p.id) for p in pads if p.name),
          "  terrain = { cell = %d, play = { %d, %d, %d, %d }, apron = %d, river = true },"
          % (CELL, -PW, -PL, PW, PL, APRON),
          "  thickets = { %s },   -- { x, z, radius }: the solid foliage blocks (stan holes)" % ", ".join(
              "{ %d, %d, %d }" % (x, z, r) for x, z, r, _ in THICKET_CENTRES if any(
                  math.hypot(cell_centre(i, j)[0] - x, cell_centre(i, j)[1] - z) < r for i, j in THICKET)),
          "}", ""]
    return "\n".join(o)


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


def thicket_clear(a, b, step=150):
    (ax, ay, az), (bx, by, bz) = a, b
    top = len(THICKET_BANDS) * BAND - 80
    n = max(2, int(math.hypot(bx - ax, bz - az) / step))
    for k in range(1, n):
        f = k / float(n)
        x, y, z = ax + (bx - ax) * f, ay + (by - ay) * f, az + (bz - az) * f
        if cell_of(x, z) in THICKET and ground_y(x, z) + top > y:
            return False
    return True


def holes_clear_2d(a, b, step=100):
    """the stan line's own view: no hole cell (thicket, boulder) anywhere along the line in x, z (it ignores heights)"""
    (ax, _, az), (bx, _, bz) = a, b
    n = max(2, int(math.hypot(bx - ax, bz - az) / step))
    return not any(cell_of(ax + (bx - ax) * k / float(n), az + (bz - az) * k / float(n)) in HOLES for k in range(1, n))


def main():
    global ALL_TREES
    os.makedirs(OUT, exist_ok=True)
    describe = "SNIPER JUNGLE: heightfield %d x %d m (play %d x %d m), cell %d, %d x %d rooms" % (
        2 * HALF // 100, 2 * HALF // 100, 2 * PW // 100, 2 * PL // 100, CELL, RN, RN)
    ALL_TREES = TREES
    drawn = build_ground()
    nb = build_boulders()
    nw = build_thickets()
    quads, nseg = build_wall()
    if GALLERY:
        build_gallery()
    tris, index = build_stan()
    if len(tris) > gl.MAX_TILES:
        sys.exit("%d stan tiles > %d" % (len(tris), gl.MAX_TILES))
    pads, adj, group, gadj, members, start, nlost = build_pads(tris, index)
    O, placed = build_props(tris, index, len(pads))
    bgc, bgh, stats, nports = gen_bg(describe)
    stan, nlinks = gen_stan(describe, tris)
    setup = gl.gen_setup(NAME, pads, start, adj, group, gadj, members, describe, O)
    setup = setup.replace("tools/gen_level.py", "tools/levelgen/build_jungle.py")
    lua = gen_lua(pads, start, nlost)
    for fn, text in (("bg_%s_all_p.c" % NAME, bgc), ("bg_%s_all_p.h" % NAME, bgh), ("Tbg_%s_all_p_stanZ.c" % NAME, stan),
                     ("Usetup%sZ.c" % NAME, setup), ("levelgen_layout.lua", lua)):
        with open(os.path.join(OUT, fn), "w", newline="\n") as f:
            f.write(text)
    ys = [y for row in HG for y in row]
    print(describe)
    print("  drawn cells %d, stan tiles %d (%d links), boulders %d, thickets %d (%d cells, %d quads), foliage wall %d quads on %d segments, "
          "portals %d" % (drawn, len(tris), nlinks, nb, len(THICKET_CENTRES), len(THICKET), nw, quads, nseg, nports))
    print("  props: %d trees, %d shrubs (pads %d .. %d)" % (placed["tree"], placed["plant"], len(pads), O.next_pad - 1))
    tot = sum(s[1] for s in stats.values())
    print("  bg: %d vertices, %d bytes uncompressed (largest room %d bytes)" % (sum(s[0] for s in stats.values()), tot, max(s[1] for s in stats.values())))
    print("  waypoint pads %d (%d grid points dropped: cut off by trees), start %d at (%d, %d, %d); heights %d..%d" % (
        len(pads), nlost, start.id, start.x, start.y, start.z, min(ys), max(ys)))
    b = [p for p in pads if p.name == "b1"][0]
    for p in pads:
        if p.kind == "named" and p.name != "b1":
            eye = (b.x, b.y + 160, b.z)
            head = (p.x, p.y + 150, p.z)
            tc = terrain_clear(eye, head)
            trees = sum(1 for t in TREES if t[2] != 0 and seg_dist(t[0], t[1], (b.x, b.z), (p.x, p.z))[0] < t[6] + 30)
            print("   %-4s pad %4d room %2d (%6d, %5d, %6d)  %3.0f m from b1  ground %s, thickets %s, holes in x-z %s, trunks near the line %d" % (
                p.name, p.id, p.room.id, p.x, p.y, p.z, math.hypot(p.x - b.x, p.z - b.z) / 100, "clear" if tc else "BLOCKED",
                "clear" if thicket_clear(eye, head) else "BLOCKED", "clear" if holes_clear_2d(eye, head) else "CROSSED", trees))
    for fn in sorted(os.listdir(OUT)):
        print("  %-28s %8d bytes" % (fn, os.path.getsize(os.path.join(OUT, fn))))


if __name__ == "__main__":
    main()
