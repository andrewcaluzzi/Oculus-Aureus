#!/usr/bin/env python3
"""Unit tests of tools/gen_level.py's bank v2 features (designs/bank-heist.md section 17), no game needed:

    python3 tests/levelgen/test_gen_level.py

- openings: a portal with no floor through it (no stan link across, no pads, no waypoint), a hole in both rooms' walls at its heights
  only, its rooms on different floors (a gallery over a hall), refused in a ramp's wall or outside a room's wall
- stair cores (`stairwells`): the flights, landings and doorways they expand to, stacked storeys accepted by the exact overlap test,
  a real overlap still refused
- a room whose pads fall in pieces (a block across it) is joined up, or refused when nothing joins it
- the engine limits are checked
- the shipped layouts generate (janus_bank.json: every pad reachable, the portals within PORTMAX)
"""
import json
import os
import subprocess
import sys
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import gen_level as g      # noqa: E402

MATS = {"m": {"tex": 901, "texels_per_unit": 0.75}}


def layout(rooms, doors=(), openings=(), stairwells=(), start=None):
    L = {"name": "levelgen", "defaults": {"floor_mat": "m", "wall_mat": "m", "ceiling_mat": "m", "height": 320}, "materials": MATS,
         "rooms": list(rooms), "doors": list(doors), "openings": list(openings), "stairwells": list(stairwells),
         "start": start or {"room": rooms[0]["name"], "at": [(rooms[0]["rect"][0] + rooms[0]["rect"][2]) // 2,
                                                             (rooms[0]["rect"][1] + rooms[0]["rect"][3]) // 2]}}
    return L


def run(L):
    """Generate L into a temp dir: (returncode, stdout + stderr, out dir)."""
    d = tempfile.mkdtemp(prefix="genlevel_")
    p = os.path.join(d, "layout.json")
    with open(p, "w") as f:
        json.dump(L, f)
    r = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "gen_level.py"), p, "--out", d], capture_output=True, text=True)
    return r.returncode, r.stdout + r.stderr, d


def load(L):
    d = tempfile.mkdtemp(prefix="genlevel_")
    p = os.path.join(d, "layout.json")
    with open(p, "w") as f:
        json.dump(L, f)
    return g.load_layout(p)


HALL = {"name": "hall", "rect": [0, 0, 1200, 1200], "height": 1000}
GAL = {"name": "gal", "rect": [1240, 0, 1700, 1200], "floor": 480, "height": 400}
AISLE = {"name": "aisle", "rect": [1240, 0, 1700, 1200], "floor": 0, "height": 440}


class Openings(unittest.TestCase):
    def test_gallery_over_hall(self):
        # a hall, an aisle beside it, a gallery over the aisle looking into the hall, a stair from the gallery down to a lobby off the hall
        hall = {"name": "hall", "rect": [0, 0, 1200, 2800], "height": 1000}
        st = {"name": "st", "rect": [1240, 1240, 1700, 2400], "floor": 480, "ramp": {"axis": "z", "to": 0}, "height": 400}
        low = {"name": "low", "rect": [1240, 2440, 1700, 2800]}
        doors = [{"rooms": ["hall", "aisle"], "at": 600, "width": 300}, {"rooms": ["gal", "st"]}, {"rooms": ["st", "low"]},
                 {"rooms": ["hall", "low"], "at": 2620}]
        L = layout([hall, AISLE, GAL, st, low], doors=doors, openings=[{"rooms": ["gal", "hall"], "at": 600, "width": 400, "y": [580, 860]}])
        rc, out, d = run(L)
        self.assertEqual(rc, 0, out)
        bg = open(os.path.join(d, "bg_levelgen_all_p.c")).read()
        self.assertIn("{&portal_4, 3, 1, 0},   /* gal~hall */", bg)          # the opening's portal comes after the doorways'
        self.assertIn("1200.0, 860.0, 400.0", bg)                            # ... on the hall's face of the wall, y 580..860
        lua = open(os.path.join(d, "levelgen_layout.lua")).read()
        self.assertIn('name = "gal~hall", a = "gal", b = "hall", portal = 4', lua)
        self.assertIn("4 across rooms", out)                                  # 4 doorways' stan links; none through the opening

    def test_unreachable_gallery_is_refused(self):
        # the gallery has no doorway: an opening gives it no floor link, so its pads cannot be reached
        L = layout([HALL, AISLE, GAL], doors=[{"rooms": ["hall", "aisle"], "at": 600, "width": 300}],
                   openings=[{"rooms": ["gal", "hall"], "at": 600, "width": 400, "y": [580, 860]}])
        rc, out, _ = run(L)
        self.assertNotEqual(rc, 0)
        self.assertIn("not reachable from the start", out)

    def test_unreachable_gallery_is_refused(self):
        # the gallery has no doorway: an opening gives it no floor link, so its pads cannot be reached
        L = layout([HALL, AISLE, GAL], doors=[{"rooms": ["hall", "aisle"], "at": 600, "width": 300}],
                   openings=[{"rooms": ["gal", "hall"], "at": 600, "width": 400, "y": [580, 860]}])
        rc, out, _ = run(L)
        self.assertNotEqual(rc, 0)
        self.assertIn("not reachable from the start", out)

    def test_wall_hole_heights(self):
        L = layout([HALL, AISLE, GAL], openings=[{"rooms": ["gal", "hall"], "at": 600, "width": 400, "y": [580, 860]}])
        _, _, mats, rooms, byname, doors, openings = load(L)
        grids = g.room_grids(rooms, doors, 300)
        for D in openings:
            D.span = sorted(set([D.lo, D.hi]))
        mesh = g.build_room_mesh(byname["hall"], mats, *grids[byname["hall"].id])
        # the hall's x1 wall: quads in the hole's span (z 400..800) exist below 580 and above 860, none between
        quads = [p for ps in mesh.polys.values() for p in ps if all(abs(v[0] - 1200) < 1e-6 for v in p)]
        inside = [p for p in quads if all(400 <= v[2] <= 800 for v in p)]
        ys = sorted(set((min(v[1] for v in p), max(v[1] for v in p)) for p in inside))
        self.assertTrue(ys and all(y1 <= 580 or y0 >= 860 for (y0, y1) in ys), ys)
        self.assertIn((0, 580), ys)
        # the floor grid did not get the hole's edges (they are wall cuts only)
        self.assertNotIn(400, grids[byname["hall"].id][1])

    def test_refused_outside_wall(self):
        L = layout([HALL, AISLE, GAL], openings=[{"rooms": ["gal", "hall"], "at": 600, "width": 400, "y": [580, 900]}])
        rc, out, _ = run(L)
        self.assertNotEqual(rc, 0)
        self.assertIn("is not inside room gal's wall", out)

    def test_refused_in_ramp(self):
        R = {"name": "r", "rect": [1240, 0, 1700, 1200], "ramp": {"axis": "z", "to": 300}}
        L = layout([HALL, R], openings=[{"rooms": ["r", "hall"], "at": 600, "width": 400, "y": [500, 600]}])
        rc, out, _ = run(L)
        self.assertNotEqual(rc, 0)
        self.assertIn("not in a ramp's wall", out)


CORE = {"name": "core", "rect": [0, 400, 1000, 1600], "axis": "z", "levels": [0, 480, 960], "landing": 400, "height": 330}


def core_layout():
    floors = [{"name": "f%d" % k, "rect": [0, 0, 1000, 360], "floor": y, "height": 400} for k, y in enumerate((0, 480, 960))]
    doors = [{"rooms": ["f0", "core_0a"], "at": 240}, {"rooms": ["f1", "core_0b"], "at": 760}, {"rooms": ["f1", "core_1a"], "at": 240},
             {"rooms": ["f2", "core_1b"], "at": 760}]
    return layout(floors, doors=doors, stairwells=[dict(CORE)])


class Stairwells(unittest.TestCase):
    def test_expands_and_generates(self):
        L = core_layout()
        rc, out, d = run(L)
        self.assertEqual(rc, 0, out)
        lua = open(os.path.join(d, "levelgen_layout.lua")).read()
        for n in ("core_0a", "core_0l", "core_0b", "core_1a", "core_1l", "core_1b"):
            self.assertIn('["%s"]' % n, lua)
        self.assertIn('["core_0a"] = { id = 4, rect = { 0, 400, 480, 1160 }, floor = 0, height = 330', lua)
        self.assertIn('ramp = { axis = "z", to = 240, stairs = 12 }', lua)
        self.assertIn('["core_0b"] = { id = 6, rect = { 520, 400, 1000, 1160 }, floor = 480', lua)
        self.assertIn('["core_0l"] = { id = 5, rect = { 0, 1200, 1000, 1600 }, floor = 240', lua)

    def test_exact_overlap(self):
        # the storeys' flights stack 480 apart with 330 high flights: the box test would refuse them, the planes do not
        _, _, _, rooms, byname, _, _ = load(core_layout())
        a, b = byname["core_0a"], byname["core_1a"]
        self.assertTrue(g.yspan(a)[1] > g.yspan(b)[0])
        # a flight taller than a storey does overlap the one above
        bad = core_layout()
        bad["stairwells"][0]["height"] = 480
        rc, out, _ = run(bad)
        self.assertNotEqual(rc, 0)
        self.assertIn("overlap", out)


class Connectivity(unittest.TestCase):
    def test_room_in_pieces_is_joined(self):
        # a long low block across most of the room: the pads either side are joined round its end
        R = {"name": "a", "rect": [0, 0, 1800, 1800], "blocks": [{"name": "wall", "rect": [0, 860, 1500, 940], "height": 100}]}
        rc, out, d = run(layout([R], start={"room": "a", "at": [300, 300]}))
        self.assertEqual(rc, 0, out)

    def test_room_cut_in_two_is_refused(self):
        R = {"name": "a", "rect": [0, 0, 1800, 1800], "blocks": [{"name": "wall", "rect": [0, 860, 1800, 940], "height": 100}]}
        S = {"name": "b", "rect": [0, 1840, 1800, 2400]}
        rc, out, _ = run(layout([R, S], doors=[{"rooms": ["a", "b"], "at": 900}], start={"room": "a", "at": [300, 300]}))
        self.assertNotEqual(rc, 0)
        self.assertIn("fall in pieces", out)


class Limits(unittest.TestCase):
    def test_too_many_rooms(self):
        rooms = [{"name": "r%d" % k, "rect": [k * 400, 0, k * 400 + 360, 360]} for k in range(g.MAX_ROOMS + 1)]
        rc, out, _ = run(layout(rooms))
        self.assertNotEqual(rc, 0)
        self.assertIn("the engine's limits", out)


class Shipped(unittest.TestCase):
    def test_janus_bank(self):
        L = json.load(open(os.path.join(ROOT, "tools", "levelgen", "janus_bank.json")))
        rc, out, d = run(L)
        self.assertEqual(rc, 0, out[-2000:])
        last = [l for l in out.splitlines() if l.startswith("openings:")][0]
        n = int(last.split("portals ")[1].split(" ")[0])
        self.assertLessEqual(n, g.MAX_PORTALS)
        lua = open(os.path.join(d, "levelgen_layout.lua")).read()
        for name in ("power", "generator", "vault", "hall", "gallery_w", "corr3", "roof", "canal"):
            self.assertIn('["%s"] = { id = ' % name, lua)
        for name in ("breaker", "generator", "front_door", "vault_door_a", "red_stairs", "tunnel_grate"):
            self.assertIn('["%s"] = { type = ' % name, lua)


if __name__ == "__main__":
    unittest.main(verbosity=2)
