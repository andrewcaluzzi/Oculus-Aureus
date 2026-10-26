/*
 * layout.js - the level editor's model: a JSON reader/writer that round-trips the generator's files byte for byte, the stair-core
 * expansion, and the generator's checks ported from tools/gen_level.py (tools/levelgen/editor/README.md).
 *
 * No DOM: the browser editor (index.html) and node (cli.js, tools/levelgen/test_editor_roundtrip.py) both load it.
 *
 * Every check that gen_level.py makes says so with `gen: true` and carries `py`, a piece of the message gen_level.py prints for
 * the same fault, so the round-trip test can hold the two to agreeing. Checks the generator does not make (the camera limit, script
 * pad names, warnings) are `gen: false`.
 */
(function (root, factory) {
  if (typeof module === "object" && module.exports) module.exports = factory();
  else root.LevelLayout = factory();
})(typeof self !== "undefined" ? self : this, function () {
  "use strict";

  // ------------------------------------------------------------------------------------------------------- the engine's limits ---
  // tools/gen_level.py MAX_*; cameras: port_heist.c CCTV_BOND_CAMS / CCTV_OV_MAX (tests/levelgen/test_gen_level.py holds v3 to 32)
  var LIMITS = { rooms: 140, portals: 190, tiles: 16380, pads: 10000, coord: 32000, cameras: 32 };

  // ---------------------------------------------------------------------------------------- JSON exactly as Python writes it ---
  // json.dump(L, f, indent=1) + "\n" (build_janus_bank_v3.py). A float whose value is whole ("intensity": 1.0) must come back as
  // 1.0, so the reader boxes it in a PyFloat; every other number is a plain JS number (ints stay ints).
  function PyFloat(v) { this.v = v; }
  PyFloat.prototype.valueOf = function () { return this.v; };
  PyFloat.prototype.toJSON = function () { return this.v; };

  function parse(text) {
    var i = 0, n = text.length;
    function err(m) { throw new SyntaxError("JSON: " + m + " at offset " + i); }
    function ws() { while (i < n) { var c = text.charCodeAt(i); if (c === 32 || c === 10 || c === 13 || c === 9) i++; else break; } }
    function value() {
      ws();
      var c = text[i];
      if (c === "{") {
        i++; var o = {}; ws();
        if (text[i] === "}") { i++; return o; }
        for (;;) {
          ws(); if (text[i] !== '"') err("expected a key");
          var k = str(); ws(); if (text[i] !== ":") err("expected :"); i++;
          o[k] = value(); ws();
          if (text[i] === ",") { i++; continue; }
          if (text[i] === "}") { i++; return o; }
          err("expected , or }");
        }
      }
      if (c === "[") {
        i++; var a = []; ws();
        if (text[i] === "]") { i++; return a; }
        for (;;) {
          a.push(value()); ws();
          if (text[i] === ",") { i++; continue; }
          if (text[i] === "]") { i++; return a; }
          err("expected , or ]");
        }
      }
      if (c === '"') return str();
      if (text.startsWith("true", i)) { i += 4; return true; }
      if (text.startsWith("false", i)) { i += 5; return false; }
      if (text.startsWith("null", i)) { i += 4; return null; }
      var m = /^-?(?:0|[1-9]\d*)(\.\d+)?([eE][-+]?\d+)?/.exec(text.slice(i, i + 64));
      if (!m) err("unexpected " + JSON.stringify(c));
      i += m[0].length;
      var v = Number(m[0]);
      if ((m[1] || m[2]) && Number.isInteger(v)) return new PyFloat(v);
      return v;
    }
    function str() {
      var s = "", j = i + 1;
      for (;;) {
        var k = text.indexOf('"', j);
        if (k < 0) err("unterminated string");
        var bs = 0, q = k - 1;
        while (q >= j && text[q] === "\\") { bs++; q--; }
        if (bs % 2 === 0) { s = JSON.parse(text.slice(i, k + 1)); i = k + 1; return s; }
        j = k + 1;
      }
    }
    var v = value(); ws();
    if (i !== n) err("trailing text");
    return v;
  }

  function pyFloat(x) {
    // Python's float repr: the shortest digits that round-trip (as JS has), fixed notation when 1e-4 <= |x| < 1e16, else d.ddde+XX
    if (!isFinite(x)) return x > 0 ? "Infinity" : x < 0 ? "-Infinity" : "NaN";
    if (x === 0) return (1 / x < 0) ? "-0.0" : "0.0";
    var sign = x < 0 ? "-" : "", e = Math.abs(x).toExponential();          // "d.ddde+X"
    var mm = /^(\d)(?:\.(\d+))?e([-+]\d+)$/.exec(e);
    var digits = mm[1] + (mm[2] || ""), exp = parseInt(mm[3], 10), decpt = exp + 1;
    if (decpt > -4 && decpt <= 16) {
      if (decpt <= 0) return sign + "0." + "0".repeat(-decpt) + digits;
      if (decpt >= digits.length) return sign + digits + "0".repeat(decpt - digits.length) + ".0";
      return sign + digits.slice(0, decpt) + "." + digits.slice(decpt);
    }
    var ex = Math.abs(exp), es = (ex < 10 ? "0" : "") + ex;
    return sign + digits[0] + (digits.length > 1 ? "." + digits.slice(1) : "") + "e" + (exp < 0 ? "-" : "+") + es;
  }

  function pyStr(s) {
    // json.dumps with ensure_ascii: \" \\ \n \r \t \b \f, other control and non-ASCII characters as \uXXXX (UTF-16 units)
    var out = '"';
    for (var k = 0; k < s.length; k++) {
      var c = s.charCodeAt(k), ch = s[k];
      if (ch === '"') out += '\\"';
      else if (ch === "\\") out += "\\\\";
      else if (ch === "\n") out += "\\n";
      else if (ch === "\r") out += "\\r";
      else if (ch === "\t") out += "\\t";
      else if (ch === "\b") out += "\\b";
      else if (ch === "\f") out += "\\f";
      else if (c < 0x20 || c > 0x7e) out += "\\u" + ("000" + c.toString(16)).slice(-4);
      else out += ch;
    }
    return out + '"';
  }

  function dump(v, ind) {
    if (v === null || v === undefined) return "null";
    if (v === true) return "true";
    if (v === false) return "false";
    if (v instanceof PyFloat) return pyFloat(v.v);
    if (typeof v === "number") return Number.isInteger(v) && Math.abs(v) < 1e21 ? String(v) : pyFloat(v);
    if (typeof v === "string") return pyStr(v);
    var pad = "\n" + " ".repeat(ind + 1), end = "\n" + " ".repeat(ind);
    if (Array.isArray(v)) {
      if (!v.length) return "[]";
      return "[" + v.map(function (x) { return pad + dump(x, ind + 1); }).join(",") + end + "]";
    }
    var keys = Object.keys(v).filter(function (k) { return v[k] !== undefined; });
    if (!keys.length) return "{}";
    return "{" + keys.map(function (k) { return pad + pyStr(k) + ": " + dump(v[k], ind + 1); }).join(",") + end + "}";
  }

  function stringify(L) { return dump(L, 0) + "\n"; }

  function clone(v) {
    if (v === null || typeof v !== "object") return v;
    if (v instanceof PyFloat) return new PyFloat(v.v);
    if (Array.isArray(v)) return v.map(clone);
    var o = {};
    for (var k in v) if (Object.prototype.hasOwnProperty.call(v, k)) o[k] = clone(v[k]);
    return o;
  }

  // ---------------------------------------------------------------------------------------------- Python's number semantics ---
  function num(v) { return v instanceof PyFloat ? v.v : (typeof v === "string" ? Number(v) : v); }
  function int(v) {                      // Python int(): a string in base 10, a number truncated toward zero
    if (typeof v === "string") { var t = v.trim(); if (!/^[-+]?\d+$/.test(t)) throw new Error("invalid literal for int(): " + v); return parseInt(t, 10); }
    v = num(v);
    if (typeof v === "boolean") return v ? 1 : 0;
    return Math.trunc(v);
  }
  function ival(v, d) {                  // gen_level.ival: "0x..." strings in any base
    if (v === undefined || v === null) return d === undefined ? 0 : d;
    if (typeof v === "string") { var t = v.trim().toLowerCase(), neg = t[0] === "-"; if (neg || t[0] === "+") t = t.slice(1);
      var r = t.startsWith("0x") ? parseInt(t.slice(2), 16) : t.startsWith("0o") ? parseInt(t.slice(2), 8) : t.startsWith("0b") ? parseInt(t.slice(2), 2) : parseInt(t, 10);
      return neg ? -r : r; }
    return Math.trunc(num(v));
  }
  function fdiv(a, b) { return Math.floor(a / b); }       // Python //
  function pyRound(x) {                                     // Python 3 round(): half to even
    var f = Math.floor(x), d = x - f;
    if (d === 0.5) return f % 2 === 0 ? f : f + 1;
    return Math.round(x);
  }
  function f0(x) {                                          // "%.0f"
    var r = pyRound(x);
    return (r === 0 && (x < 0 || 1 / x < 0)) ? "-0" : String(r);
  }
  function pyRepr(v) {                                      // %r of what a layout holds
    if (v === undefined || v === null) return "None";
    if (typeof v === "string") return v.indexOf("'") >= 0 && v.indexOf('"') < 0 ? '"' + v + '"' : "'" + v.replace(/\\/g, "\\\\").replace(/'/g, "\\'") + "'";
    if (v === true) return "True";
    if (v === false) return "False";
    if (v instanceof PyFloat) return pyFloat(v.v);
    if (typeof v === "number") return Number.isInteger(v) ? String(v) : pyFloat(v);
    return JSON.stringify(v);
  }
  function get(o, k, d) { return (o && Object.prototype.hasOwnProperty.call(o, k)) ? o[k] : d; }

  // ------------------------------------------------------------------------------------------------- gen_level.expand_stairwells ---
  // Returns the rooms and doors a stair core expands to (named <name>_<k>a / _<k>l / _<k>b). Each generated room carries
  // `_core` = the index of its stairwells entry. Errors go to `issues` (gen_level stops on them).
  function expandStairwells(L, issues) {
    var rooms = [], doors = [];
    (L.stairwells || []).forEach(function (sw, si) {
      var name = sw.name, ax = get(sw, "axis", "z");
      var r = sw.rect.map(int), x0 = r[0], z0 = r[1], x1 = r[2], z1 = r[3];
      var g = int(get(sw, "wall", 40)), land = int(get(sw, "landing", 400)), h = int(get(sw, "height", 330));
      var lv = sw.levels.map(int);
      var rising = lv.every(function (v, k) { return k === 0 || v > lv[k - 1]; });
      if ((ax !== "x" && ax !== "z") || lv.length < 2 || !rising) {
        issues.push(issue(true, "stairwell " + name + ": axis x or z and levels rising", { kind: "core", index: si }));
        return;
      }
      var lo = ax === "z" ? z0 : x0, hi = ax === "z" ? z1 : x1;
      var c0 = ax === "z" ? x0 : z0, c1 = ax === "z" ? x1 : z1;
      var mid = fdiv(c0 + c1, 2);
      var lanes = [[c0, mid - fdiv(g, 2)], [mid + g - fdiv(g, 2), c1]];
      var flHi = hi - land - g;
      if (flHi - lo < 300 || Math.min(lanes[0][1] - lanes[0][0], lanes[1][1] - lanes[1][0]) < 200) {
        issues.push(issue(true, "stairwell " + name + ": too small (flights " + (flHi - lo) + " long, lanes [(" + lanes[0].join(", ") +
          "), (" + lanes[1].join(", ") + ")])", { kind: "core", index: si }));
        return;
      }
      function rect(along, across) { return ax === "z" ? [across[0], along[0], across[1], along[1]] : [along[0], across[0], along[1], across[1]]; }
      var keys = {};
      ["floor_mat", "wall_mat", "ceiling_mat", "light", "chr_light", "area", "zone"].forEach(function (k) { if (k in sw) keys[k] = sw[k]; });
      var dw = int(get(sw, "door_width", 200));
      for (var k = 0; k + 1 < lv.length; k++) {
        var ya = lv[k], yb = lv[k + 1], yl = fdiv(ya + yb, 2);
        var ramp = { axis: ax, stairs: int(get(sw, "stairs", 12)) };
        var rm = get(sw, "riser_mat", get(sw, "wall_mat"));
        if (rm !== undefined && rm !== null) ramp.riser_mat = rm;
        var mk = function (suffix, rc, floor, rp) {
          var o = Object.assign({}, keys, { name: name + "_" + k + suffix, rect: rc, floor: floor, height: h });
          if (rp) o.ramp = Object.assign({}, rp, { to: yl });
          Object.defineProperty(o, "_core", { value: si, enumerable: false });
          return o;
        };
        var a = mk("a", rect([lo, flHi], lanes[0]), ya, ramp);
        var b = mk("b", rect([lo, flHi], lanes[1]), yb, ramp);
        var l = mk("l", rect([flHi + g, hi], [c0, c1]), yl, null);
        rooms.push(a, l, b);
        doors.push({ rooms: [a.name, l.name], at: fdiv(lanes[0][0] + lanes[0][1], 2), width: dw, _core: si },
                   { rooms: [l.name, b.name], at: fdiv(lanes[1][0] + lanes[1][1], 2), width: dw, _core: si });
      }
    });
    return { rooms: rooms, doors: doors };
  }

  function issue(gen, msg, ref, level, py) {
    return { level: level || "error", gen: !!gen, msg: msg, py: py === undefined ? (gen ? msg : null) : py, ref: ref || null };
  }

  // ------------------------------------------------------------------------------------------------------------ the model ---
  // build(L): the layout as gen_level.load_layout reads it (rooms R with ints, ramps, blocks; doors D with their axis, faces, span),
  // plus every problem found on the way. Unlike gen_level it carries on past a fault, so the editor can list them all.
  function build(L, opts) {
    opts = opts || {};
    var issues = [];
    var d = L.defaults || {}, mats = L.materials || {};
    var ex = expandStairwells(L, issues);
    var rawRooms = (L.rooms || []).map(function (r, k) { return { src: r, index: k }; }).concat(ex.rooms.map(function (r) { return { src: r, core: r._core }; }));
    var rooms = [], byname = {};
    rawRooms.forEach(function (rr, k) {
      var r = rr.src, R = { src: r, index: rr.index, core: rr.core, name: r.name, id: k + 1, ok: true };
      var ref = { kind: "room", name: r.name };
      var rc = (r.rect || [0, 0, 0, 0]).map(int);
      R.x0 = rc[0]; R.z0 = rc[1]; R.x1 = rc[2]; R.z1 = rc[3];
      if (!(R.x0 < R.x1 && R.z0 < R.z1)) { issues.push(issue(true, "room " + R.name + ": rect must be [x0, z0, x1, z1] with x0 < x1 and z0 < z1", ref)); R.ok = false; }
      R.y = int(get(r, "floor", get(d, "floor", 0)));
      R.h = int(get(r, "height", get(d, "height", 320)));
      ["floor", "wall", "ceiling"].forEach(function (part) {
        var m = get(r, part + "_mat", get(d, part + "_mat"));
        if (!(typeof m === "string" && m in mats)) issues.push(issue(true, "room " + R.name + ": unknown " + part + " material " + pyRepr(m), ref));
        R[part + "_mat"] = m;
      });
      R.wall_mats = Object.assign({}, get(r, "wall_mats", {}));
      Object.keys(R.wall_mats).forEach(function (side) {
        var m = R.wall_mats[side];
        if (["x0", "x1", "z0", "z1"].indexOf(side) < 0 || !(m in mats))
          issues.push(issue(true, "room " + R.name + ": wall_mats " + side + " = " + pyRepr(m) + ": a side is x0, x1, z0 or z1 and a known material", ref));
      });
      R.ramp = get(r, "ramp", null);
      if (R.ramp !== null && R.ramp !== undefined) {
        if (R.ramp.axis !== "x" && R.ramp.axis !== "z") { issues.push(issue(true, "room " + R.name + ": ramp axis must be x or z", ref)); R.ramp = null; R.ramp_to = R.y; R.stairs = 0; }
        else { R.ramp_to = int(R.ramp.to); R.stairs = int(get(R.ramp, "stairs", 0)); }
      } else { R.ramp = null; R.ramp_to = R.y; R.stairs = 0; }
      R.open_top = !!get(r, "open_top", false);
      R.area = get(r, "area"); R.zone = get(r, "zone");
      R.blocks = [];
      (get(r, "blocks", []) || []).forEach(function (b, k2) {
        var bc = b.rect.map(int);
        var B = { src: b, name: get(b, "name", R.name + "_block" + (k2 + 1)), x0: bc[0], z0: bc[1], x1: bc[2], z1: bc[3], h: int(get(b, "height", 110)) };
        B.mat = get(b, "mat", R.wall_mat); B.top_mat = get(b, "top_mat", B.mat);
        [B.mat, B.top_mat].forEach(function (m) { if (!(m in mats)) issues.push(issue(true, "room " + R.name + " block " + B.name + ": unknown material " + pyRepr(m), ref)); });
        if (!(R.x0 <= B.x0 && B.x0 < B.x1 && B.x1 <= R.x1 && R.z0 <= B.z0 && B.z0 < B.z1 && B.z1 <= R.z1))
          issues.push(issue(true, "room " + R.name + " block " + B.name + ": not inside the room", ref));
        if (R.ramp) issues.push(issue(true, "room " + R.name + ": blocks on a ramp are not supported", ref));
        R.blocks.push(B);
      });
      R.pads_named = (get(r, "pads", []) || []).slice();
      R.objects = get(r, "objects", []) || [];
      R.loot = get(r, "loot", []) || [];
      R.loot.forEach(function (lt) { (lt.at || []).forEach(function (at, k3) { R.pads_named.push({ name: lt.name + "_" + (k3 + 1), at: at, loot: lt.name }); }); });
      R.doors = []; R.openings = [];
      if (byname[R.name]) issues.push(issue(true, "duplicate room name " + R.name, ref));
      else byname[R.name] = R;
      rooms.push(R);
    });
    // overlaps: rooms on different floors may share x, z when one is wholly above the other (exact for ramps)
    for (var i = 0; i < rooms.length; i++) {
      var a = rooms[i];
      for (var j = i + 1; j < rooms.length; j++) {
        var b = rooms[j];
        if (a.x0 < b.x1 && b.x0 < a.x1 && a.z0 < b.z1 && b.z0 < a.z1) {
          var cx = [Math.max(a.x0, b.x0), Math.min(a.x1, b.x1)], cz = [Math.max(a.z0, b.z0), Math.min(a.z1, b.z1)];
          var pts = [[cx[0], cz[0]], [cx[0], cz[1]], [cx[1], cz[0]], [cx[1], cz[1]]];
          var above = function (u, v) { return pts.every(function (p) { return floorAt(u, p[0], p[1]) - halfStep(u) >= floorAt(v, p[0], p[1]) + v.h; }); };
          if (!(above(a, b) || above(b, a))) issues.push(issue(true, "rooms " + a.name + " and " + b.name + " overlap", { kind: "room", name: a.name, other: b.name }));
        }
      }
    }
    var wallmax = int(get(d, "max_wall", 200));
    var doors = [];
    var rawDoors = (L.doors || []).map(function (dd, k) { return { src: dd, index: k }; }).concat(ex.doors.map(function (dd) { return { src: dd, core: dd._core }; }));
    rawDoors.forEach(function (rd, k) {
      var dd = rd.src, D = { src: dd, index: rd.index, core: rd.core, kind: "door", portal: k };
      var na = dd.rooms[0], nb = dd.rooms[1];
      var ref = rd.core !== undefined ? { kind: "core", index: rd.core } : { kind: "door", index: rd.index };
      D.name = get(dd, "name", na + "-" + nb);
      var a2 = byname[na], b2 = byname[nb];
      if (!a2 || !b2) { issues.push(issue(true, "door " + D.name + ": no room named " + (a2 ? nb : na), ref, "error", "KeyError")); return; }
      D.a = a2; D.b = b2;
      D.w = int(get(dd, "width", get(d, "door_width", 180)));
      D.h = int(get(dd, "height", get(d, "door_height", 240)));
      var f = facing(a2, b2, wallmax);
      if (!f) { issues.push(issue(true, "door " + na + "-" + nb + ": the rooms have no facing walls within " + wallmax + " units", ref)); D.bad = true; doors.push(D); return; }
      D.axis = f[0]; D.fa = f[1]; D.fb = f[2]; var lo = f[3], hi = f[4];
      D.wlo = lo; D.whi = hi;
      var at = int(get(dd, "at", fdiv(lo + hi, 2)));
      D.at = at;
      D.lo = at - fdiv(D.w, 2); D.hi = at - fdiv(D.w, 2) + D.w;
      if (D.lo < lo + 20 || D.hi > hi - 20) { issues.push(issue(true, "door " + na + "-" + nb + ": the opening " + D.lo + ".." + D.hi + " is not on the shared wall " + lo + ".." + hi + " (20 units margin)", ref)); D.bad = true; }
      [a2, b2].forEach(function (R) {
        if (R.ramp && R.ramp.axis !== D.axis) { issues.push(issue(true, "door " + na + "-" + nb + ": on a side wall of the ramp " + R.name + " (doorways go on a ramp's end walls)", ref)); D.bad = true; }
      });
      var fa = D.axis === "x" ? floorAt(a2, D.fa, at) : floorAt(a2, at, D.fa);
      var fb = D.axis === "x" ? floorAt(b2, D.fb, at) : floorAt(b2, at, D.fb);
      if (fa !== fb) { issues.push(issue(true, "door " + na + "-" + nb + ": the floors differ at the doorway (" + fa + " and " + fb + ")", ref)); D.bad = true; }
      D.y = fa;
      var fr = get(dd, "frame", get(d, "door_frame"));
      D.frame = null;
      if (fr) {
        fr = typeof fr === "object" ? fr : { mat: fr };
        if (!(fr.mat in mats)) issues.push(issue(true, "door " + D.name + ": unknown frame material " + pyRepr(fr.mat), ref));
        D.frame = { mat: fr.mat, w: int(get(fr, "width", get(d, "frame_width", 14))), depth: int(get(fr, "depth", get(d, "frame_depth", 6))) };
      }
      if (D.h + (D.frame ? D.frame.w : 0) > Math.min(a2.h, b2.h)) issues.push(issue(true, "door " + na + "-" + nb + ": taller than a room", ref));
      D.obj = get(dd, "object", null);
      a2.doors.push(D); b2.doors.push(D); doors.push(D);
    });
    var openings = [];
    (L.openings || []).forEach(function (oo, k) {
      var ref = { kind: "opening", index: k };
      var a3 = byname[oo.rooms[0]], b3 = byname[oo.rooms[1]];
      var D = { src: oo, index: k, kind: "opening", portal: doors.length + k };
      D.name = get(oo, "name", oo.rooms[0] + "~" + oo.rooms[1]);
      if (!a3 || !b3) { issues.push(issue(true, "opening " + D.name + ": no room named " + (a3 ? oo.rooms[1] : oo.rooms[0]), ref, "error", "KeyError")); return; }
      D.a = a3; D.b = b3;
      var f = facing(a3, b3, wallmax);
      if (!f) { issues.push(issue(true, "opening " + D.name + ": the rooms have no facing walls within " + wallmax + " units", ref)); D.bad = true; openings.push(D); return; }
      D.axis = f[0]; D.fa = f[1]; D.fb = f[2]; var lo = f[3], hi = f[4];
      D.w = int(get(oo, "width", 300));
      var at = int(get(oo, "at", fdiv(lo + hi, 2)));
      D.at = at; D.lo = at - fdiv(D.w, 2); D.hi = at - fdiv(D.w, 2) + D.w;
      if (D.lo < lo + 20 || D.hi > hi - 20) { issues.push(issue(true, "opening " + D.name + ": " + D.lo + ".." + D.hi + " is not on the shared wall " + lo + ".." + hi + " (20 units margin)", ref)); D.bad = true; }
      var y = (oo.y || [0, 0]).map(int), y0 = y[0], y1 = y[1];
      D.y = y0; D.h = y1 - y0;
      var fr = get(oo, "frame");
      D.frame = null;
      if (fr) {
        fr = typeof fr === "object" ? fr : { mat: fr };
        if (!(fr.mat in mats)) issues.push(issue(true, "opening " + D.name + ": unknown frame material " + pyRepr(fr.mat), ref));
        D.frame = { mat: fr.mat, w: int(get(fr, "width", get(d, "frame_width", 14))) };
      }
      var fw = D.frame ? D.frame.w : 0;
      [a3, b3].forEach(function (R) {
        if (R.ramp) { issues.push(issue(true, "opening " + D.name + ": not in a ramp's wall (" + R.name + ")", ref)); D.bad = true; }
        else if (!(R.y <= y0 && y0 < y1 && y1 + fw <= R.y + R.h)) { issues.push(issue(true, "opening " + D.name + ": y " + y0 + ".." + y1 + " (+ frame " + fw + ") is not inside room " + R.name + "'s wall " + R.y + ".." + (R.y + R.h), ref)); D.bad = true; }
      });
      a3.openings.push(D); b3.openings.push(D); openings.push(D);
    });
    return { L: L, d: d, mats: mats, rooms: rooms, byname: byname, doors: doors, openings: openings, issues: issues, wallmax: wallmax };
  }

  function facing(a, b, wallmax) {
    if (0 < b.x0 - a.x1 && b.x0 - a.x1 <= wallmax) return ["x", a.x1, b.x0, Math.max(a.z0, b.z0), Math.min(a.z1, b.z1)];
    if (0 < a.x0 - b.x1 && a.x0 - b.x1 <= wallmax) return ["x", a.x0, b.x1, Math.max(a.z0, b.z0), Math.min(a.z1, b.z1)];
    if (0 < b.z0 - a.z1 && b.z0 - a.z1 <= wallmax) return ["z", a.z1, b.z0, Math.max(a.x0, b.x0), Math.min(a.x1, b.x1)];
    if (0 < a.z0 - b.z1 && a.z0 - b.z1 <= wallmax) return ["z", a.z0, b.z1, Math.max(a.x0, b.x0), Math.min(a.x1, b.x1)];
    return null;
  }

  function floorAt(R, x, z) {
    if (!R.ramp) return R.y;
    var lo, hi, c;
    if (R.ramp.axis === "x") { lo = R.x0; hi = R.x1; c = x; } else { lo = R.z0; hi = R.z1; c = z; }
    var t = Math.min(1.0, Math.max(0.0, (c - lo) / (hi - lo)));
    return Math.trunc(pyRound(R.y + (R.ramp_to - R.y) * t));
  }
  function halfStep(R) { return R.stairs ? Math.ceil(Math.abs(R.ramp_to - R.y) / (2.0 * R.stairs)) : 0; }
  function yspan(R) { return [Math.min(R.y, R.ramp_to), Math.max(R.y, R.ramp_to) + R.h]; }

  // a floor prop's footprint (gen_level.footprint): `ext` = {model: [half x, half z]} read off the stock models (cli.js --footprints;
  // the editor has none unless you load one, since the numbers come from your extracted assets)
  function footprint(o, ext) {
    var at = o.at.map(num), face = (o.face || [at[0], at[1] + 100]).map(num), hx, hz;
    if (o.size) { hx = num(o.size[0]) / 2.0; hz = num(o.size[1]) / 2.0; }
    else {
      var e = ext && ext[String(ival(o.model))];
      if (!e) return null;
      hx = e[0]; hz = e[1];
    }
    var fx = face[0] - at[0], fz = face[1] - at[1], ln = Math.hypot(fx, fz) || 1.0;
    fx /= ln; fz /= ln;
    var ex = Math.abs(fz) * hx + Math.abs(fx) * hz, ez = Math.abs(fx) * hx + Math.abs(fz) * hz;
    return [at[0] - ex, at[1] - ez, at[0] + ex, at[1] + ez];
  }

  // ------------------------------------------------------------------------------------------------------------- validate ---
  function validate(L, opts) {
    opts = opts || {};
    var M;
    try { M = build(L, opts); } catch (e) { return { model: null, issues: [issue(true, "layout: " + e.message, null, "error", "")] }; }
    var issues = M.issues, d = M.d, rooms = M.rooms, doors = M.doors.filter(function (D) { return D.a; }), openings = M.openings;
    var nportals = M.doors.length + openings.length;
    if (rooms.length > LIMITS.rooms || nportals > LIMITS.portals)
      issues.push(issue(true, "the engine's limits: " + rooms.length + " rooms (max " + LIMITS.rooms + ": MAXROOMCOUNT 150 less room 0 and the end entry, with a margin), " +
        nportals + " portals (max " + LIMITS.portals + ": PORTMAX 200)", { kind: "limits" }));
    var name = L.name;
    if (!(typeof name === "string" && /^[a-z0-9]+$/.test(name) && /[a-z]/.test(name))) issues.push(issue(true, "name must be lower-case letters and digits", { kind: "limits" }));
    if (rooms.some(function (R) { return Math.max(Math.abs(R.x0), Math.abs(R.x1), Math.abs(R.z0), Math.abs(R.z1), Math.abs(R.y) + R.h, Math.abs(R.ramp_to) + R.h) > LIMITS.coord; }))
      issues.push(issue(true, "coordinates outside +-" + LIMITS.coord + " (bg vertices and stan points are s16)", { kind: "limits" }, "error", "coordinates within +-"));
    // P2.4: no furniture in a doorway's way
    var clr = num(get(d, "door_clearance", 60)), depth = num(get(d, "door_approach", 110)) + clr, unchecked = 0, seenProp = new Set();
    doors.forEach(function (D) {
      if (D.axis === undefined) return;
      [[D.a, D.fa], [D.b, D.fb]].forEach(function (rf) {
        var R = rf[0], face = rf[1];
        var inward = (D.axis === "x" ? (R.x0 + R.x1) / 2.0 : (R.z0 + R.z1) / 2.0) > face ? 1 : -1;
        var aa = [face, face + inward * depth].sort(function (p, q) { return p - q; });
        var zone = D.axis === "x" ? [aa[0], D.lo - clr, aa[1], D.hi + clr] : [D.lo - clr, aa[0], D.hi + clr, aa[1]];
        var boxes = R.blocks.map(function (B) { return ["block " + B.name, [B.x0, B.z0, B.x1, B.z1]]; });
        R.objects.forEach(function (o) {
          if (o.type !== "prop") return;
          var fp = footprint(o, opts.footprints);
          if (!fp) { if (seenProp.has(o)) return; seenProp.add(o); unchecked++; if (opts.footprints) issues.push(issue(true, "prop " + get(o, "name") + ": no footprint for model " + pyRepr(o.model) + " (give it \"size\": [width, depth])", { kind: "object", room: R.name, name: o.name })); return; }
          boxes.push(["prop " + get(o, "name", "?"), fp]);
        });
        boxes.forEach(function (bx) {
          var b = bx[1];
          if (b[0] < zone[2] && zone[0] < b[2] && b[1] < zone[3] && zone[1] < b[3]) {
            var m = bx[0] + " in room " + R.name + " (" + f0(b[0]) + ".." + f0(b[2]) + " x " + f0(b[1]) + ".." + f0(b[3]) + ") is within " + Math.trunc(clr) +
              " units of doorway " + D.name + " (keep-out " + f0(zone[0]) + ".." + f0(zone[2]) + " x " + f0(zone[1]) + ".." + f0(zone[3]) + ")";
            issues.push(issue(true, m, { kind: "door", index: D.index, room: R.name }));
          }
        });
      });
    });
    if (unchecked && !opts.footprints) issues.push(issue(false, unchecked + " prop footprints near doorways are not checked here (their sizes come from your extracted models; gen_level checks them)", null, "info"));
    // pads: on their room's floor (gen_level.build_pads: a stan tile under them). The editor's test is the room's rect (plus its
    // doorways' floors), less its blocks: close to gen_level's, which also takes a tile up to 40 units off.
    var named = {};
    function onFloor(R, x, z) {
      if (x >= R.x0 && x <= R.x1 && z >= R.z0 && z <= R.z1) return true;
      return R.doors.some(function (D) {
        if (D.a !== R || D.axis === undefined) return false;
        var f0_ = Math.min(D.fa, D.fb), f1_ = Math.max(D.fa, D.fb);
        return D.axis === "x" ? (x >= f0_ && x <= f1_ && z >= D.lo && z <= D.hi) : (z >= f0_ && z <= f1_ && x >= D.lo && x <= D.hi);
      });
    }
    function inBlock(R, x, z) { return R.blocks.find(function (B) { return B.x0 < x && x < B.x1 && B.z0 < z && z < B.z1; }); }
    rooms.forEach(function (R) {
      R.pads_named.forEach(function (p) {
        var x = int(p.at[0]), z = int(p.at[1]), ref = { kind: "pad", room: R.name, name: p.name };
        if (!onFloor(R, x, z)) issues.push(issue(true, "pad " + p.name + " at (" + x + ", " + z + "): no floor in room " + R.name, ref));
        else if (inBlock(R, x, z)) issues.push(issue(false, "pad " + p.name + " at (" + x + ", " + z + ") stands in block " + inBlock(R, x, z).name + " of room " + R.name + " (gen_level may refuse it)", ref, "warn"));
        if (named[p.name]) issues.push(issue(false, "pad name " + p.name + " is used twice (rooms " + named[p.name].name + " and " + R.name + ")", ref, "warn"));
        named[p.name] = R;
      });
    });
    var st = L.start || {}, SR = M.byname[st.room];
    if (!SR) issues.push(issue(true, "start: no room named " + st.room, { kind: "start" }, "error", "KeyError"));
    else {
      var sat = st.at || [fdiv(SR.x0 + SR.x1, 2), fdiv(SR.z0 + SR.z1, 2)];
      var sx = int(sat[0]), sz = int(sat[1]);
      if (!onFloor(SR, sx, sz)) issues.push(issue(true, "pad start at (" + sx + ", " + sz + "): no floor in room " + SR.name, { kind: "start" }));
      named.start = SR;
    }
    // objects (gen_level.build_objects)
    var ncam = 0;
    doors.forEach(function (D) {
      var o = D.obj; if (!o) return;
      var ref = { kind: "door", index: D.index };
      var kind = get(o, "type", "slide");
      if (kind !== "slide" && kind !== "shutter") issues.push(issue(true, "door " + D.name + ": type must be slide or shutter", ref));
      else if (o.double && kind !== "slide") issues.push(issue(true, "door " + D.name + ": a double door slides", ref));
    });
    rooms.forEach(function (R) {
      R.objects.forEach(function (o, k) {
        var kind = o.type, nm = get(o, "name", R.name + "_" + kind + (k + 1)), ref = { kind: "object", room: R.name, index: k, name: nm };
        if (kind === "camera" || kind === "alarm" || kind === "monitor") {
          if (["x0", "x1", "z0", "z1"].indexOf(o.wall) < 0) { issues.push(issue(true, "room " + R.name + ": wall must be x0, x1, z0 or z1, not " + pyRepr(o.wall), ref)); return; }
          var al = num(o.along), lo = o.wall[0] === "x" ? R.z0 : R.x0, hi = o.wall[0] === "x" ? R.z1 : R.x1;
          if (!(al >= lo && al <= hi)) issues.push(issue(false, kind + " " + nm + ": along " + al + " is off its wall " + o.wall + " (" + lo + ".." + hi + ")", ref, "warn"));
          if (kind === "camera") {
            ncam++;
            if (typeof o.watch === "string" && !named[o.watch]) issues.push(issue(true, "camera " + nm + ": no pad named " + o.watch, ref));
          }
        } else if (kind === "safe") {
          if (R.ramp) issues.push(issue(true, "safe " + nm + ": not on a ramp", ref));
        } else if (kind !== "key" && kind !== "prop") {
          issues.push(issue(true, "room " + R.name + ": unknown object type " + pyRepr(kind), ref));
        }
      });
    });
    if (ncam > LIMITS.cameras) issues.push(issue(false, "cameras: " + ncam + " (max " + LIMITS.cameras + ": port_heist.c CCTV_BOND_CAMS, the cctv overrides)", { kind: "limits" }));
    // reachability: every room's pads from the start (gen_level: "waypoints: n of m pads are not reachable from the start"). A
    // room's pads are one graph (gen_level joins or refuses them), so this is the room graph through the doorways.
    if (SR) {
      var seen = {}, todo = [SR];
      seen[SR.name] = true;
      while (todo.length) {
        var u = todo.pop();
        u.doors.forEach(function (D) { [D.a, D.b].forEach(function (v) { if (!seen[v.name]) { seen[v.name] = true; todo.push(v); } }); });
      }
      var minc = int(get(d, "pad_min_cell", 150));
      var lost = rooms.filter(function (R) { return !seen[R.name] && (R.doors.length || R.pads_named.length || (R.x1 - R.x0 >= minc && R.z1 - R.z0 >= minc)); });
      lost.forEach(function (R) { issues.push(issue(true, "room " + R.name + " is not reachable from the start (no doorway path from " + SR.name + ")", { kind: "room", name: R.name }, "error", "not reachable from the start")); });
    }
    // script pads (build_janus_bank_v3.check_names): every pad the heist tables name exists
    var h = L.script && L.script.heist;
    if (h && typeof h === "object") {
      var have = Object.assign({}, named);
      rooms.forEach(function (R) { R.objects.forEach(function (o) { if (o.name) have[o.name] = R; }); });
      doors.forEach(function (D) { if (D.obj && D.obj.name) have[D.obj.name] = D.a; });
      var want = [];
      (h.areas || []).forEach(function (a) { want = want.concat(a.posts || []); if (a.panel) want.push(a.panel); });
      (h.cams || []).forEach(function (c) { want.push(c[0]); });
      (h.civ_extra || []).forEach(function (c) { want = want.concat(c.pads || []); });
      Object.keys(h.patrols || {}).forEach(function (k) { want = want.concat(h.patrols[k]); });
      want = want.concat(h.stations || []);
      (h.vents || []).forEach(function (v) { want.push("vent_" + v + "_a", "vent_" + v + "_b"); });
      (h.grapples || []).forEach(function (g) { want.push("grapple_" + g + "_low", "grapple_" + g + "_high"); });
      var miss = {};
      want.forEach(function (w) { if (!have[w]) miss[w] = true; });
      Object.keys(miss).sort().forEach(function (w) { issues.push(issue(false, "script.heist names pad or object " + w + ", which is not in the layout", { kind: "script", name: w }, "warn")); });
      (h.areas || []).forEach(function (a) { (a.rooms || []).forEach(function (rn) { if (!M.byname[rn]) issues.push(issue(false, "script.heist area " + a.name + " names room " + rn + ", which is not in the layout", { kind: "script", name: rn }, "warn")); }); });
    }
    M.ncam = ncam; M.nportals = nportals; M.named = named;
    return { model: M, issues: issues };
  }

  // ------------------------------------------------------------------------------------------------------------- helpers ---
  var KEYS = [ // (mask, colour, label): tools/levelgen_plan.py KEYS
    [0x40000000, "#9646c8", "VAULT (cracker)"], [0x10, "#dcaa14", "GOLD"], [0x8, "#d72828", "RED"], [0x4, "#285adc", "BLUE"],
    [0x2, "#1ea03c", "GREEN"], [0x1, "#8c5a28", "floor key"]];
  function keyColour(kf) { for (var k = 0; k < KEYS.length; k++) if (kf & KEYS[k][0]) return KEYS[k][1]; return "#787878"; }
  function keyLabel(kf) { if (!kf) return "unlocked"; return KEYS.filter(function (k) { return kf & k[0]; }).map(function (k) { return k[2]; }).join(" + ") || "0x" + kf.toString(16); }
  var LEVEL_NAMES = { "-840": "B2", "-420": "B1", "0": "G", "480": "F1", "960": "F2", "1440": "F3" };
  function floorSpan(R) { return [Math.min(R.y, R.ramp_to), Math.max(R.y, R.ramp_to)]; }
  function levelsOf(M) {   // levelgen_plan.levels_of: floors of level rooms that carry at least two rooms
    var c = {};
    M.rooms.forEach(function (R) { if (!R.ramp) c[R.y] = (c[R.y] || 0) + 1; });
    var lv = Object.keys(c).map(Number).filter(function (y) { return c[y] >= 2; }).sort(function (a, b) { return a - b; });
    return lv.length ? lv : [0];
  }
  function roomsOnLevel(M, lv, k) {
    if (lv.length <= 1) return M.rooms.slice();
    var y = lv[k], nxt = k + 1 < lv.length ? lv[k + 1] : 1e9;
    return M.rooms.filter(function (R) { var s = floorSpan(R); return s[0] < nxt && s[1] >= y; });
  }

  return {
    LIMITS: LIMITS, PyFloat: PyFloat, parse: parse, stringify: stringify, clone: clone, pyFloat: pyFloat,
    expandStairwells: expandStairwells, build: build, validate: validate, facing: facing, floorAt: floorAt, footprint: footprint,
    num: num, int: int, ival: ival, KEYS: KEYS, keyColour: keyColour, keyLabel: keyLabel, LEVEL_NAMES: LEVEL_NAMES,
    floorSpan: floorSpan, levelsOf: levelsOf, roomsOnLevel: roomsOnLevel, yspan: yspan
  };
});
