#!/usr/bin/env python3
"""Tests of the level editor's model (tools/levelgen/editor/: layout.js, edits.js, cli.js; README.md there), no game needed:

    py -3 tools/levelgen/test_editor_roundtrip.py        (or python3; needs node on the PATH, else the JS tests are skipped)

- round trip: every shipped layout (tools/levelgen/*.json), read and written back by the editor's serializer, is byte for byte the
  file, and byte for byte Python's own json.dump(indent=1) of it (the generator's writer, the reference); values keep their type
  (1.0 stays a float); a synthetic layout of awkward values (escapes, non-ASCII, tiny and huge floats, empty containers) too
- the editor's checks agree with gen_level.py: the shipped bank v3 passes both (with the prop footprints read from your extracted
  models); each broken layout fails both, and the editor's message for it is the one gen_level prints
- edits: a renamed room, a moved room, a new pad and a door rule still generate; a removed doorway that strands a room is refused by both
"""
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
CLI = os.path.join(HERE, "editor", "cli.js")
NODE = shutil.which("node")
sys.path.insert(0, os.path.join(ROOT, "tools"))
import gen_level as g      # noqa: E402


def tmpdir():
    # gen_level names the layout relative to the repo (os.path.relpath), which fails across Windows drives: keep the temp files on
    # the repo's drive (build_preview/ is ignored)
    base = None
    if os.name == "nt" and os.path.splitdrive(tempfile.gettempdir())[0].lower() != os.path.splitdrive(ROOT)[0].lower():
        base = os.path.join(ROOT, "build_preview", "test_tmp")
        os.makedirs(base, exist_ok=True)
    return tempfile.mkdtemp(prefix="editor_", dir=base)


def node(*args):
    r = subprocess.run([NODE, CLI] + list(args), capture_output=True, text=True, encoding="utf-8")
    if r.returncode != 0:
        raise AssertionError("cli.js %s failed: %s" % (args[0], r.stderr))
    return r.stdout


def py_dump(L):
    """The generator's writer (build_janus_bank_v3.py main)."""
    return json.dumps(L, indent=1) + "\n"


def strict_eq(a, b, path="$"):
    """Equal, and of the same JSON types (1 and 1.0 differ; bools are not ints)."""
    if type(a) is not type(b):
        return "%s: %r (%s) vs %r (%s)" % (path, a, type(a).__name__, b, type(b).__name__)
    if isinstance(a, dict):
        if list(a.keys()) != list(b.keys()):
            return "%s: keys %s vs %s" % (path, list(a.keys()), list(b.keys()))
        for k in a:
            e = strict_eq(a[k], b[k], path + "." + k)
            if e:
                return e
    elif isinstance(a, list):
        if len(a) != len(b):
            return "%s: lengths %d vs %d" % (path, len(a), len(b))
        for i, (x, y) in enumerate(zip(a, b)):
            e = strict_eq(x, y, "%s[%d]" % (path, i))
            if e:
                return e
    elif a != b:
        return "%s: %r vs %r" % (path, a, b)
    return None


def footprints(L):
    """{model: [half x, half z]} for every prop model in L, from your extracted models (gen_level.prop_half_extents)."""
    out = {}
    for r in L["rooms"]:
        for o in r.get("objects", []):
            if o.get("type") == "prop" and "size" not in o:
                m = g.ival(o.get("model"))
                e = g.prop_half_extents(ROOT, m)
                if e is not None:
                    out[str(m)] = list(e)
    return out


def gen(L, d):
    """gen_level.py on L: (returncode, output)."""
    p = os.path.join(d, "layout.json")
    with open(p, "w") as f:
        json.dump(L, f)
    out = os.path.join(d, "out")
    r = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "gen_level.py"), p, "--out", out], capture_output=True, text=True)
    return r.returncode, r.stdout + r.stderr


def js_validate(L, d, fp=None):
    p = os.path.join(d, "layout_js.json")
    with open(p, "w") as f:
        json.dump(L, f, indent=1)
    args = ["validate", p]
    if fp is not None:
        fpp = os.path.join(d, "fp.json")
        with open(fpp, "w") as f:
            json.dump(fp, f)
        args += ["--footprints", fpp]
    return json.loads(node(*args))


MATS = {"m": {"tex": 901, "texels_per_unit": 0.75}, "w": {"tex": 2374, "texels_per_unit": 0.27}}


def small():
    """A hall, an aisle off it, a gallery over the aisle looking into the hall, a stair from the gallery down to a lobby off the hall
    (tests/levelgen/test_gen_level.py's gallery layout), a camera and a pad."""
    hall = {"name": "hall", "rect": [0, 0, 1200, 2800], "height": 1000,
            "pads": [{"name": "hall_mid", "at": [600, 1400], "face": [600, 1500]}],
            "objects": [{"type": "camera", "name": "cam_hall", "wall": "x0", "along": 1400, "y": 280, "watch": "hall_mid"}]}
    aisle = {"name": "aisle", "rect": [1240, 0, 1700, 1200], "floor": 0, "height": 440}
    gal = {"name": "gal", "rect": [1240, 0, 1700, 1200], "floor": 480, "height": 400}
    st = {"name": "st", "rect": [1240, 1240, 1700, 2400], "floor": 480, "ramp": {"axis": "z", "to": 0}, "height": 400}
    low = {"name": "low", "rect": [1240, 2440, 1700, 2800]}
    doors = [{"rooms": ["hall", "aisle"], "at": 600, "width": 300}, {"rooms": ["gal", "st"]}, {"rooms": ["st", "low"]},
             {"rooms": ["hall", "low"], "at": 2620}]
    return {"name": "levelgen", "defaults": {"floor_mat": "m", "wall_mat": "m", "ceiling_mat": "m", "height": 320}, "materials": MATS,
            "rooms": [hall, aisle, gal, st, low], "doors": doors,
            "openings": [{"rooms": ["gal", "hall"], "at": 600, "width": 400, "y": [580, 860]}],
            "start": {"room": "hall", "at": [300, 300]}}


def room(L, name):
    return [r for r in L["rooms"] if r["name"] == name][0]


# each: (name, mutation of small()); every one breaks a gen_level rule
def _overlap(L): room(L, "low")["rect"] = [1100, 2440, 1700, 2800]
def _door_off_wall(L): L["doors"][0]["at"] = 1150
def _door_too_wide(L): L["doors"][0]["width"] = 1300
def _no_facing(L): L["doors"].append({"rooms": ["aisle", "low"]})
def _unknown_mat(L): room(L, "aisle")["floor_mat"] = "marble"
def _bad_rect(L): room(L, "aisle")["rect"] = [1700, 0, 1240, 1200]
def _dup_room(L): room(L, "low")["name"] = "aisle"
def _ramp_side_door(L): L["doors"].append({"rooms": ["hall", "st"], "at": 1800})
def _floors_differ(L): room(L, "low")["floor"] = 40
def _too_tall(L): L["doors"][0]["height"] = 900
def _opening_high(L): L["openings"][0]["y"] = [580, 900]
def _opening_ramp(L): L["openings"].append({"rooms": ["st", "hall"], "at": 1800, "width": 300, "y": [500, 600]})
def _cam_watch(L): room(L, "hall")["objects"][0]["watch"] = "nowhere"
def _cam_wall(L): room(L, "hall")["objects"][0]["wall"] = "north"
def _unknown_obj(L): room(L, "hall")["objects"].append({"type": "turret", "at": [600, 600]})
def _pad_off_floor(L): room(L, "hall")["pads"].append({"name": "lost", "at": [2600, 600]})
def _start_off_floor(L): L["start"]["at"] = [-300, 300]
def _unreachable(L): del L["doors"][3]
def _block_out(L): room(L, "hall")["blocks"] = [{"name": "desk", "rect": [1000, 100, 1300, 300]}]
def _block_in_door(L): room(L, "hall")["blocks"] = [{"name": "desk", "rect": [1000, 500, 1150, 700], "height": 110}]
def _block_on_ramp(L): room(L, "st")["blocks"] = [{"name": "box", "rect": [1300, 1500, 1400, 1600]}]
def _door_type(L): L["doors"][0]["object"] = {"name": "d", "type": "swing"}
def _double_shutter(L): L["doors"][0]["object"] = {"name": "d", "type": "shutter", "double": True}
def _ramp_axis(L): room(L, "st")["ramp"]["axis"] = "y"
def _name(L): L["name"] = "Level-1"
def _frame_mat(L): L["doors"][0]["frame"] = "brass"
def _too_many_rooms(L):
    L["rooms"] += [{"name": "x%d" % k, "rect": [3000 + k * 400, 0, 3000 + k * 400 + 360, 360]} for k in range(g.MAX_ROOMS)]
def _core_small(L): L["stairwells"] = [{"name": "core", "rect": [3000, 0, 3300, 600], "axis": "z", "levels": [0, 480]}]
def _core_levels(L): L["stairwells"] = [{"name": "core", "rect": [3000, 0, 4000, 1600], "axis": "z", "levels": [480, 0]}]
def _no_room_door(L): L["doors"].append({"rooms": ["hall", "attic"]})


BROKEN = [(f.__name__[1:], f) for f in (
    _overlap, _door_off_wall, _door_too_wide, _no_facing, _unknown_mat, _bad_rect, _dup_room, _ramp_side_door, _floors_differ,
    _too_tall, _opening_high, _opening_ramp, _cam_watch, _cam_wall, _unknown_obj, _pad_off_floor, _start_off_floor, _unreachable,
    _block_out, _block_in_door, _block_on_ramp, _door_type, _double_shutter, _ramp_axis, _name, _frame_mat, _too_many_rooms,
    _core_small, _core_levels, _no_room_door)]


@unittest.skipIf(NODE is None, "node is not on the PATH (the editor's JS cannot run headless here)")
class RoundTrip(unittest.TestCase):
    def test_shipped_layouts_byte_identical(self):
        d = tmpdir()
        names = sorted(n for n in os.listdir(HERE) if n.endswith(".json"))
        self.assertIn("janus_bank_v3.json", names)
        for n in names:
            src = os.path.join(HERE, n)
            out = os.path.join(d, n)
            node("roundtrip", src, out)
            with open(src, "rb") as f:
                a = f.read()
            with open(out, "rb") as f:
                b = f.read()
            with open(src) as f:
                L = json.load(f)
            ref = py_dump(L).encode()
            # the editor writes what Python writes; a hand-written layout (lshape.json ...) need not be in Python's format itself
            self.assertEqual(b, ref, "%s: the editor's export differs from json.dump(indent=1)" % n)
            if a == ref:
                self.assertEqual(a, b, n)
            with open(out) as f:
                self.assertIsNone(strict_eq(L, json.load(f)), n)
        with open(os.path.join(HERE, "janus_bank_v3.json"), "rb") as f:
            v3 = f.read()
        with open(os.path.join(d, "janus_bank_v3.json"), "rb") as f:
            self.assertEqual(v3, f.read(), "janus_bank_v3.json: not byte for byte")

    def test_awkward_values(self):
        L = {"name": "levelgen", "s": "quote \" backslash \\ tab \t nl \n cr \r bell \x07 del \x7f e-acute é snow ☃ emoji \U0001F600 / <",
             "f": [1.0, -2.0, 0.5, 0.1, 1e-05, 0.0001, 1.5e-07, 1e16, 1e15, 123456789012.25, -0.0, 3.141592653589793, 2.5e+300],
             "i": [0, -1, 2 ** 40, 7], "b": [True, False, None], "e": [[], {}, [[]], {"x": {}}], "k": {"b": 1, "a": 2, "": 3}}
        d = tmpdir()
        p, q = os.path.join(d, "in.json"), os.path.join(d, "out.json")
        with open(p, "w") as f:
            f.write(py_dump(L))
        node("roundtrip", p, q)
        with open(q) as f:
            got = f.read()
        self.assertEqual(got, py_dump(L))
        self.assertIsNone(strict_eq(L, json.loads(got)))


@unittest.skipIf(NODE is None, "node is not on the PATH")
class Agreement(unittest.TestCase):
    def check(self, L, label, fp=None):
        d = tmpdir()
        rc, out = gen(L, d)
        self.last_out = out
        res = js_validate(L, d, fp)
        errs = [i for i in res["issues"] if i["level"] == "error" and i["gen"]]
        if rc == 0:
            self.assertEqual(errs, [], "%s: gen_level accepts it, the editor says %s" % (label, [e["msg"] for e in errs]))
        else:
            self.assertTrue(errs, "%s: gen_level refuses it (%s), the editor sees nothing" % (label, out.strip().splitlines()[-1]))
            self.assertTrue(any(e["py"] and e["py"] in out for e in errs),
                            "%s: messages differ:\n  gen_level: %s\n  editor: %s" % (label, out.strip().splitlines()[-1], [e["msg"] for e in errs]))
        return rc, res

    def test_small_layout_is_good(self):
        rc, _ = self.check(small(), "small")
        self.assertEqual(rc, 0)

    def test_broken_layouts(self):
        for name, mutate in BROKEN:
            with self.subTest(name):
                L = small()
                mutate(L)
                rc, _ = self.check(L, name)
                self.assertNotEqual(rc, 0, "%s: gen_level accepted it (the case does not break what it says)" % name)

    def test_janus_bank_v3(self):
        with open(os.path.join(HERE, "janus_bank_v3.json")) as f:
            L = json.load(f)
        fp = footprints(L)
        rc, res = self.check(L, "janus_bank_v3", fp)
        self.assertEqual(rc, 0)
        self.assertEqual(res["counts"], {"rooms": 119, "doors": 153, "openings": 33, "portals": 186, "cameras": 31})
        self.assertEqual([i["msg"] for i in res["issues"] if i["level"] != "info"], [])
        # a prop moved into a doorway's way (the P2.4 check, with the stock models' footprints): both refuse it, by the same words
        bad = json.loads(json.dumps(L))
        dd = bad["doors"][0]
        a = room(bad, dd["rooms"][0])
        prop = [o for r in bad["rooms"] for o in r.get("objects", []) if o["type"] == "prop" and "size" not in o][0]
        x0, z0, x1, z1 = a["rect"]
        side = g.facing(*[type("R", (), dict(zip(("x0", "z0", "x1", "z1"), room(bad, n)["rect"])))() for n in dd["rooms"]], 200)
        self.assertIsNotNone(side)
        axis, fa = side[0], side[1]
        at = dd["at"]
        a.setdefault("objects", []).append(dict(prop, name="prop_in_way", at=[fa - 60 if fa == x1 else fa + 60, at] if axis == "x" else
                                                [at, fa - 60 if fa == z1 else fa + 60], face=None))
        a["objects"][-1].pop("face")
        rc, res = self.check(bad, "v3 prop in a doorway", fp)
        self.assertNotEqual(rc, 0)
        # a camera watching a pad that is gone
        bad2 = json.loads(json.dumps(L))
        cam = [o for o in room(bad2, "hall")["objects"] if o["type"] == "camera"][0]
        cam["watch"] = "no_such_pad"
        rc, res = self.check(bad2, "v3 camera watch", fp)
        self.assertNotEqual(rc, 0)


@unittest.skipIf(NODE is None, "node is not on the PATH")
class Edits(unittest.TestCase):
    def edit(self, L, ops):
        d = tmpdir()
        p, o, q = os.path.join(d, "in.json"), os.path.join(d, "ops.json"), os.path.join(d, "out.json")
        with open(p, "w") as f:
            f.write(py_dump(L))
        with open(o, "w") as f:
            json.dump(ops, f)
        node("edit", p, o, q)
        with open(q) as f:
            return json.load(f)

    def test_edits_generate(self):
        L = self.edit(small(), [
            {"op": "renameRoom", "from": "hall", "to": "grand_hall"},
            {"op": "addPad", "room": "grand_hall", "name": "new_post", "at": [900, 900]},
            {"op": "movePad", "name": "hall_mid", "at": [500, 1200]},
            {"op": "setDoorRule", "index": 0, "rule": {"kind": "slide", "keyflags": 2, "see_through": True}},
            {"op": "moveWallObject", "room": "grand_hall", "index": 0, "wall": "z0", "along": 600},
            {"op": "moveRoom", "room": "low", "dx": 0, "dz": 0},
            {"op": "addDoor", "a": "grand_hall", "b": "aisle", "at": 1000, "width": 200},
        ])
        self.assertEqual(L["start"]["room"], "grand_hall")
        self.assertEqual(L["doors"][0]["rooms"], ["grand_hall", "aisle"])
        self.assertEqual(L["doors"][0]["object"]["keyflags"], "0x2")
        self.assertEqual(room(L, "grand_hall")["objects"][0]["wall"], "z0")
        rc, res = Agreement.check(self, L, "edited")
        self.assertEqual(rc, 0, self.last_out[-1500:])

    def test_move_room_takes_its_contents(self):
        L = small()
        room(L, "hall")["blocks"] = [{"name": "desk", "rect": [300, 1800, 500, 1900]}]
        L2 = self.edit(L, [{"op": "moveRoom", "room": "hall", "dx": -100, "dz": 0}])
        h = room(L2, "hall")
        self.assertEqual(h["rect"], [-100, 0, 1100, 2800])
        self.assertEqual(h["pads"][0]["at"], [500, 1400])
        self.assertEqual(h["blocks"][0]["rect"], [200, 1800, 400, 1900])
        self.assertEqual(h["objects"][0]["along"], 1400)              # on an x wall: along is a z, unchanged by an x move
        self.assertEqual(L2["start"]["at"], [200, 300])
        self.assertEqual(L2["doors"][0]["at"], 600)                    # hall-aisle runs along z: an x move leaves it
        # the hall is now 140 from the aisle and the lobby: still within max_wall, so it still generates
        rc, _ = Agreement.check(self, L2, "moved hall")
        self.assertEqual(rc, 0)
        L3 = self.edit(L, [{"op": "moveRoom", "room": "hall", "dx": -300, "dz": 0}])       # 340: no facing walls any more
        rc, _ = Agreement.check(self, L3, "hall moved away")
        self.assertNotEqual(rc, 0)

    def test_removed_door_strands_rooms(self):
        L = self.edit(small(), [{"op": "removeDoor", "index": 3}])
        rc, res = Agreement.check(self, L, "removed door")
        self.assertNotEqual(rc, 0)
        lost = sorted(i["ref"]["name"] for i in res["issues"] if "not reachable" in i["msg"])
        self.assertEqual(lost, ["gal", "low", "st"])


def find_browser():
    """A Chromium-family browser that can run headless (--dump-dom), or None."""
    for n in ("chromium", "chromium-browser", "google-chrome", "msedge", "chrome"):
        p = shutil.which(n)
        if p:
            return p
    for p in (r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe", r"C:\Program Files\Microsoft\Edge\Application\msedge.exe",
              r"C:\Program Files\Google\Chrome\Application\chrome.exe"):
        if os.path.exists(p):
            return p
    return None


BROWSER = find_browser()


@unittest.skipIf(BROWSER is None, "no headless Chromium / Chrome / Edge found")
class Page(unittest.TestCase):
    def test_selftest(self):
        """editor/selftest.js in a headless browser: tap, drag, undo, the door tool and its rule, delete, export (17 checks)."""
        d = tmpdir()
        page = os.path.join(d, "editor.html")
        subprocess.run([sys.executable, os.path.join(HERE, "editor", "build_standalone.py"), page, "--selftest"], check=True, capture_output=True)
        url = "file:///" + page.replace("\\", "/").lstrip("/") + "#selftest"
        r = subprocess.run([BROWSER, "--headless=new", "--disable-gpu", "--no-first-run", "--user-data-dir=" + os.path.join(d, "profile"),
                            "--window-size=1400,900", "--virtual-time-budget=10000", "--dump-dom", url],
                           capture_output=True, text=True, encoding="utf-8", timeout=180)
        dom = r.stdout
        lines = [ln for ln in dom.replace('<pre id="selftest">', "\n").replace("</pre>", "\n").splitlines() if ln.startswith(("PASS ", "FAIL ", "SELFTEST"))]
        self.assertTrue(any(ln.startswith("SELFTEST PASS") for ln in lines), "\n".join(lines) or dom[-3000:])


if __name__ == "__main__":
    unittest.main(verbosity=2)
