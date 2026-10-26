/*
 * editor.js - the level editor's page (tools/levelgen/editor/index.html, README.md there): draws a layout's levels top down, selects
 * and edits rooms, doorways, openings, pads, objects and the start, checks the layout live (layout.js) and exports the JSON.
 */
(function () {
  "use strict";
  var LL = window.LevelLayout, ED = window.LevelEdits;
  var $ = function (id) { return document.getElementById(id); };

  // ------------------------------------------------------------------------------------------------------------------ state ---
  var S = {
    L: null, file: "layout.json", M: null, issues: [], lv: [0], level: 0, sel: null, tool: "select", snap: 20,
    view: { cx: 0, cz: 0, s: 0.1 }, undo: [], redo: [], readOnly: false, footprints: null, dirty: false,
    layers: { pads: true, objects: true, cams: true, patrols: true, posts: true, gadgets: true, labels: true, others: true, issues: true }
  };
  var ZONE = { public: "#ece6d6", staff: "#d6e4c8", restricted: "#e8c4be" };
  var C = { bg: "#1c1e24", wall: "#2c2c36", grid: "#24262e", grid2: "#2c2f38", other: "#4a4c58", block: "#5f5046", ramp: "#cdc8af",
            open: "#fafafa", opening: "#3cdcf0", cam: "#ff7800", alarm: "#ff1e1e", mon: "#28d2e6", pad: "#5a5a64", loot: "#f0c828",
            text: "#16161a", sel: "#ffd400", err: "#ff3b3b", warn: "#ffae00", post: "#2f7bff", vent: "#c06cff", grapple: "#ff9a3c" };
  var ROUTE = ["#e6194b", "#3cb44b", "#4363d8", "#f58231", "#911eb4", "#46f0f0", "#f032e6", "#bcf60c", "#008080", "#9a6324"];

  // ------------------------------------------------------------------------------------------------------- load and check ---
  function load(text, name) {
    var L;
    try { L = LL.parse(text); } catch (e) { toast("Not a layout: " + e.message); return; }
    if (!L || !Array.isArray(L.rooms)) {
      if (L && typeof L === "object" && !Array.isArray(L) && Object.keys(L).every(function (k) { return /^\d+$/.test(k); })) {
        S.footprints = L; toast("Prop footprints loaded (" + Object.keys(L).length + " models)"); refresh(); return;
      }
      toast("Not a layout: it has no rooms list"); return;
    }
    S.L = L; S.file = name || "layout.json"; S.undo = []; S.redo = []; S.sel = null; S.dirty = false;
    refresh();
    S.level = Math.max(0, S.lv.indexOf(levelOfStart())); renderTabs();
    fit();
    $("file-name").textContent = S.file;
    toast("Loaded " + S.file + ": " + S.M.rooms.length + " rooms");
    if (!S.hashDone) { S.hashDone = true; applyHash(); }
  }

  // deep links: #level=B1&sel=room:vault&tab=issues&ro=1 (sel: room:<name>, pad:<name>, door:<index>, opening:<index>, core:<index>, start:)
  function applyHash() {
    var h = {};
    location.hash.slice(1).split("&").forEach(function (kv) { var p = kv.split("="); if (p[0]) h[decodeURIComponent(p[0])] = decodeURIComponent(p[1] || ""); });
    if (h.ro === "1") setReadOnly(true);
    if (h.ro === "0") setReadOnly(false);
    if (h.level) {
      var k = S.lv.findIndex(function (y) { return (LL.LEVEL_NAMES[String(y)] || "y" + y).toLowerCase() === h.level.toLowerCase(); });
      if (k >= 0) { S.level = k; renderTabs(); fit(onLevel()); }
    }
    var m = h.sel && /^(room|pad|door|opening|core|start):(.*)$/.exec(h.sel);
    if (m) goTo(m[1] === "room" || m[1] === "pad" ? { kind: m[1], name: m[2] } : m[1] === "start" ? { kind: "start" } : { kind: m[1], index: Number(m[2]) });
    if (h.tab) { var b = document.querySelector('.tabs button[data-pane="p-' + h.tab + '"]'); if (b) b.click(); }
    if ("selftest" in h) {         // tools/levelgen/editor/selftest.js (build_standalone.py --selftest inlines it): run once the page has loaded
      var run = function () {
        if (window.LevelEditorSelftest) window.LevelEditorSelftest(h);
        else { var s = document.createElement("script"); s.src = "selftest.js"; s.onload = function () { window.LevelEditorSelftest(h); }; document.body.appendChild(s); }
      };
      if (document.readyState === "complete") setTimeout(run, 0); else window.addEventListener("load", function () { setTimeout(run, 0); });
    }
  }

  function levelOfStart() {
    var st = S.L.start && S.M.byname[S.L.start.room];
    if (!st) return S.lv[0];
    var best = S.lv[0];
    S.lv.forEach(function (y) { if (y <= LL.floorSpan(st)[1]) best = y; });
    return best;
  }

  var pending = 0;
  function refresh(light) {
    if (!S.L) return;
    if (light) { S.M = LL.build(S.L); }
    else {
      var r = LL.validate(S.L, { footprints: S.footprints });
      S.M = r.model || LL.build(S.L); S.issues = r.issues;
    }
    S.lv = LL.levelsOf(S.M);
    if (S.level >= S.lv.length) S.level = S.lv.length - 1;
    renderTabs(); renderStats(); if (!light) renderIssues(); renderInspector(); draw();
  }
  function refreshSoon() { clearTimeout(pending); pending = setTimeout(function () { refresh(); }, 120); }

  // -------------------------------------------------------------------------------------------------------------- undo ---
  function snapshot() { S.undo.push(LL.clone(S.L)); if (S.undo.length > 120) S.undo.shift(); S.redo = []; S.dirty = true; }
  function doEdit(fn) {
    if (!S.L || S.readOnly) return;
    snapshot();
    try { var m = fn(); if (m) toast(m); } catch (e) { S.L = S.undo.pop(); toast(e.message); }
    refresh();
  }
  function undo() { if (!S.undo.length) return; S.redo.push(S.L); S.L = S.undo.pop(); fixSel(); refresh(); toast("Undo"); }
  function redo() { if (!S.redo.length) return; S.undo.push(S.L); S.L = S.redo.pop(); fixSel(); refresh(); toast("Redo"); }
  function fixSel() { if (S.sel && !selTarget(S.sel)) S.sel = null; }

  // -------------------------------------------------------------------------------------------------------------- view ---
  var cv = $("plan"), ctx = cv.getContext("2d"), W = 0, H = 0, DPR = 1, MK = 1;
  function resize() {
    DPR = window.devicePixelRatio || 1;
    var r = cv.getBoundingClientRect();
    W = r.width; H = r.height;
    cv.width = Math.round(W * DPR); cv.height = Math.round(H * DPR);
    draw();
  }
  function sx(x) { return (x - S.view.cx) * S.view.s + W / 2; }
  function sy(z) { return (S.view.cz - z) * S.view.s + H / 2; }
  function wx(px) { return (px - W / 2) / S.view.s + S.view.cx; }
  function wz(py) { return S.view.cz - (py - H / 2) / S.view.s; }
  function snapv(v) { var g = S.snap || 1; return Math.round(v / g) * g; }
  function fit(rooms) {
    rooms = rooms || (S.M ? S.M.rooms : []);
    if (!rooms.length) return;
    var x0 = Infinity, z0 = Infinity, x1 = -Infinity, z1 = -Infinity;
    rooms.forEach(function (R) { x0 = Math.min(x0, R.x0); z0 = Math.min(z0, R.z0); x1 = Math.max(x1, R.x1); z1 = Math.max(z1, R.z1); });
    S.view.cx = (x0 + x1) / 2; S.view.cz = (z0 + z1) / 2;
    S.view.s = Math.min((W - 40) / Math.max(1, x1 - x0), (H - 40) / Math.max(1, z1 - z0));
    draw();
  }
  function focusOn(x0, z0, x1, z1) {
    S.view.cx = (x0 + x1) / 2; S.view.cz = (z0 + z1) / 2;
    var s = Math.min((W - 80) / Math.max(200, x1 - x0), (H - 80) / Math.max(200, z1 - z0));
    S.view.s = Math.min(Math.max(s, 0.02), 1.5);
  }

  // --------------------------------------------------------------------------------------------------- what is on a level ---
  function onLevel() { return S.M ? LL.roomsOnLevel(S.M, S.lv, S.level) : []; }
  function roomOfLevelAt(x, z, rooms) {
    var best = null;
    (rooms || onLevel()).forEach(function (R) {
      if (x >= R.x0 && x <= R.x1 && z >= R.z0 && z <= R.z1 && (!best || (R.x1 - R.x0) * (R.z1 - R.z0) < (best.x1 - best.x0) * (best.z1 - best.z0))) best = R;
    });
    return best;
  }
  function heist() { return (S.L && S.L.script && S.L.script.heist) || null; }
  function padIndex() {   // name -> {x, z, room}
    var out = {};
    S.M.rooms.forEach(function (R) { R.pads_named.forEach(function (p) { out[p.name] = { x: LL.num(p.at[0]), z: LL.num(p.at[1]), R: R, p: p }; }); });
    if (S.L.start && S.M.byname[S.L.start.room]) {
      var R = S.M.byname[S.L.start.room], at = S.L.start.at || [(R.x0 + R.x1) / 2, (R.z0 + R.z1) / 2];
      out.start = { x: LL.num(at[0]), z: LL.num(at[1]), R: R };
    }
    return out;
  }
  function padTags() {    // pad name -> [labels]: guard posts, patrol stops, stations, vents, grapples
    var h = heist(), t = {};
    function add(n, s) { (t[n] = t[n] || []).push(s); }
    if (!h) return t;
    (h.areas || []).forEach(function (a) { (a.posts || []).forEach(function (p) { add(p, "guard post (" + a.name + ")"); }); if (a.panel) add(a.panel, "alarm panel (" + a.name + ")"); });
    Object.keys(h.patrols || {}).forEach(function (r) { (h.patrols[r] || []).forEach(function (p, k) { add(p, "patrol " + r + " stop " + (k + 1)); }); });
    (h.stations || []).forEach(function (p) { add(p, "watchclock station"); });
    (h.civ_extra || []).forEach(function (c) { (c.pads || []).forEach(function (p) { add(p, "civilian (" + c.area + ")"); }); });
    return t;
  }
  function wallPoint(R, wall, along, off) {
    if (wall === "x0") return [R.x0 + off, along];
    if (wall === "x1") return [R.x1 - off, along];
    if (wall === "z0") return [along, R.z0 + off];
    return [along, R.z1 - off];
  }
  function doorBox(D) {   // levelgen_plan.py: across the wall gap, plus 10 each side
    if (D.axis === undefined) return null;
    var f0 = Math.min(D.fa, D.fb), f1 = Math.max(D.fa, D.fb);
    if (D.axis === "x") return [f0 - 10, D.lo, f1 + 10, D.hi];
    return [D.lo, f0 - 10, D.hi, f1 + 10];
  }
  function issuesFor(ref) {
    return S.issues.filter(function (i) {
      var r = i.ref; if (!r || i.level === "info") return false;
      if (ref.kind === "room") return (r.kind === "room" && (r.name === ref.name || r.other === ref.name)) || (r.kind === "pad" && false);
      if (ref.kind === "door") return r.kind === "door" && r.index === ref.index;
      if (ref.kind === "opening") return r.kind === "opening" && r.index === ref.index;
      if (ref.kind === "pad") return r.kind === "pad" && r.name === ref.name;
      if (ref.kind === "object") return r.kind === "object" && r.room === ref.room && r.index === ref.index;
      if (ref.kind === "core") return r.kind === "core" && r.index === ref.index;
      if (ref.kind === "start") return r.kind === "start";
      return false;
    });
  }

  // -------------------------------------------------------------------------------------------------------------- drawing ---
  function rect(x0, z0, x1, z1) { var a = sx(x0), b = sy(z1); ctx.rect(a, b, sx(x1) - a, sy(z0) - b); }
  function fillRect(x0, z0, x1, z1, col) { ctx.fillStyle = col; ctx.beginPath(); rect(x0, z0, x1, z1); ctx.fill(); }
  function dot(x, z, r, col, stroke) { ctx.beginPath(); ctx.arc(sx(x), sy(z), r * MK, 0, Math.PI * 2); ctx.fillStyle = col; ctx.fill(); if (stroke) { ctx.strokeStyle = stroke; ctx.lineWidth = 1.5; ctx.stroke(); } }
  function floorColour(R) {
    if (R.ramp) return C.ramp;
    if (R.zone && ZONE[R.zone]) return ZONE[R.zone];
    return R.y < -100 ? "#b0c4de" : "#ece6d6";
  }

  function draw() {
    ctx.setTransform(DPR, 0, 0, DPR, 0, 0);
    MK = S.view.s < 0.05 ? 0.55 : S.view.s < 0.1 ? 0.75 : 1;           // markers shrink when zoomed far out
    ctx.fillStyle = C.bg; ctx.fillRect(0, 0, W, H);
    if (!S.M) { ctx.fillStyle = "#8a8d99"; ctx.font = "15px system-ui, sans-serif"; ctx.textAlign = "center"; ctx.fillText("Open a layout (tools/levelgen/*.json) or drop one here", W / 2, H / 2); return; }
    drawGrid();
    var mine = onLevel(), set = new Set(mine);
    if (S.layers.others) {
      ctx.strokeStyle = C.other; ctx.lineWidth = 1;
      S.M.rooms.forEach(function (R) { if (!set.has(R)) { ctx.beginPath(); rect(R.x0, R.z0, R.x1, R.z1); ctx.stroke(); } });
    }
    var wallpx = Math.max(2, 15 * S.view.s);
    mine.forEach(function (R) {
      ctx.fillStyle = C.wall; ctx.beginPath();
      ctx.rect(sx(R.x0) - wallpx, sy(R.z1) - wallpx, (R.x1 - R.x0) * S.view.s + 2 * wallpx, (R.z1 - R.z0) * S.view.s + 2 * wallpx); ctx.fill();
    });
    mine.forEach(function (R) {
      fillRect(R.x0, R.z0, R.x1, R.z1, floorColour(R));
      if (R.open_top) fillRect(R.x0, R.z0, R.x1, R.z1, "rgba(120,140,170,0.35)");
      if (R.ramp) {
        var n = R.stairs || 10; ctx.strokeStyle = "#968c6e"; ctx.lineWidth = 1; ctx.beginPath();
        for (var k = 1; k < n; k++) {
          var t = k / n;
          if (R.ramp.axis === "z") { var zz = R.z0 + (R.z1 - R.z0) * t; ctx.moveTo(sx(R.x0), sy(zz)); ctx.lineTo(sx(R.x1), sy(zz)); }
          else { var xx = R.x0 + (R.x1 - R.x0) * t; ctx.moveTo(sx(xx), sy(R.z0)); ctx.lineTo(sx(xx), sy(R.z1)); }
        }
        ctx.stroke();
        // an arrow up the slope
        var up = R.ramp_to > R.y ? 1 : -1, mx = (R.x0 + R.x1) / 2, mz = (R.z0 + R.z1) / 2, L2 = Math.min(R.x1 - R.x0, R.z1 - R.z0) * 0.35;
        var dx = R.ramp.axis === "x" ? up * L2 : 0, dz = R.ramp.axis === "z" ? up * L2 : 0;
        arrow(mx - dx, mz - dz, mx + dx, mz + dz, "#6d6450");
      }
      R.blocks.forEach(function (B) { fillRect(B.x0, B.z0, B.x1, B.z1, C.block); });
    });
    // doorways and openings
    S.M.doors.forEach(function (D, k) {
      if (!D.a || !(set.has(D.a) || set.has(D.b))) return;
      var b = doorBox(D); if (!b) return;
      var o = D.obj, col = !o ? C.open : LL.keyColour(LL.ival(o.keyflags, 0));
      fillRect(b[0], b[1], b[2], b[3], col);
      var cx = (b[0] + b[2]) / 2, cz = (b[1] + b[3]) / 2;
      if (o && o.double) dot(cx, cz, 2.2, "#fff");
      if (o && o.padlock) dot(cx, cz, 4.5, "#ff8c00", "#000");
      if (o && o.type === "shutter" && S.view.s > 0.08) { ctx.strokeStyle = "#000"; ctx.lineWidth = 1; ctx.beginPath(); rect(b[0], b[1], b[2], b[3]); ctx.stroke(); }
    });
    S.M.openings.forEach(function (D) {
      if (!D.a || !(set.has(D.a) || set.has(D.b)) || D.axis === undefined) return;
      var f0 = Math.min(D.fa, D.fb), f1 = Math.max(D.fa, D.fb);
      if (D.axis === "x") fillRect(f0 - 4, D.lo, f1 + 4, D.hi, C.opening); else fillRect(D.lo, f0 - 4, D.hi, f1 + 4, C.opening);
    });
    var pads = padIndex(), tags = padTags();
    // objects
    if (S.layers.objects || S.layers.cams) mine.forEach(function (R) {
      R.objects.forEach(function (o, k) {
        var t = o.type;
        if (t === "camera" && S.layers.cams) drawCamera(R, o, pads);
        if (!S.layers.objects) return;
        if (t === "alarm" || t === "monitor") {
          if (["x0", "x1", "z0", "z1"].indexOf(o.wall) < 0) return;
          var p = wallPoint(R, o.wall, LL.num(o.along), 12);
          ctx.fillStyle = t === "alarm" ? C.alarm : C.mon; var sz = t === "alarm" ? 4 : 2.5;
          ctx.fillRect(sx(p[0]) - sz, sy(p[1]) - sz, 2 * sz, 2 * sz);
        } else if (t === "safe" && o.at) { ctx.fillStyle = "#dcaa14"; ctx.fillRect(sx(LL.num(o.at[0])) - 6, sy(LL.num(o.at[1])) - 6, 12, 12); ctx.strokeStyle = "#000"; ctx.strokeRect(sx(LL.num(o.at[0])) - 6, sy(LL.num(o.at[1])) - 6, 12, 12); }
        else if (t === "key" && o.at) dot(LL.num(o.at[0]), LL.num(o.at[1]), 4.5, LL.keyColour(LL.ival(o.keyflags, 1)), "#000");
        else if (t === "prop" && o.at) {
          var fp = LL.footprint(o, S.footprints);
          if (fp && S.view.s > 0.06) { ctx.strokeStyle = "#96785a"; ctx.lineWidth = 1; ctx.beginPath(); rect(fp[0], fp[1], fp[2], fp[3]); ctx.stroke(); }
          ctx.fillStyle = "#96785a"; ctx.fillRect(sx(LL.num(o.at[0])) - 3, sy(LL.num(o.at[1])) - 3, 6, 6);
        }
      });
    });
    // patrol routes
    var h = heist();
    if (h && S.layers.patrols && h.patrols) {
      Object.keys(h.patrols).forEach(function (r, ri) {
        var ps = (h.patrols[r] || []).map(function (n) { return pads[n]; }).filter(Boolean);
        if (!ps.some(function (p) { return set.has(p.R); })) return;
        ctx.strokeStyle = ROUTE[ri % ROUTE.length]; ctx.lineWidth = 2; ctx.setLineDash([7, 4]); ctx.beginPath();
        ps.forEach(function (p, k) { if (k) ctx.lineTo(sx(p.x), sy(p.z)); else ctx.moveTo(sx(p.x), sy(p.z)); });
        if (ps.length > 2) ctx.closePath();
        ctx.stroke(); ctx.setLineDash([]);
        ps.forEach(function (p, k) { if (set.has(p.R)) { dot(p.x, p.z, 4, ROUTE[ri % ROUTE.length], "#000"); } });
      });
      (h.stations || []).forEach(function (n) { var p = pads[n]; if (p && set.has(p.R)) { ctx.beginPath(); ctx.arc(sx(p.x), sy(p.z), 8, 0, 7); ctx.strokeStyle = "#fff"; ctx.lineWidth = 1.5; ctx.stroke(); } });
    }
    // vents and grapples: dashed lines between their two pads
    if (h && S.layers.gadgets) {
      (h.vents || []).forEach(function (v) { link(pads["vent_" + v + "_a"], pads["vent_" + v + "_b"], C.vent, "vent " + v, set); });
      (h.grapples || []).forEach(function (g) { link(pads["grapple_" + g + "_low"], pads["grapple_" + g + "_high"], C.grapple, "piton " + g, set); });
    }
    // pads
    if (S.layers.pads || S.layers.posts) mine.forEach(function (R) {
      R.pads_named.forEach(function (p) {
        var x = LL.num(p.at[0]), z = LL.num(p.at[1]), tg = tags[p.name] || [];
        var post = tg.some(function (s) { return s.indexOf("guard post") === 0; });
        if (post && S.layers.posts) {
          var X = sx(x), Y = sy(z); ctx.beginPath(); ctx.moveTo(X, Y - 6 * MK); ctx.lineTo(X + 5 * MK, Y + 4 * MK); ctx.lineTo(X - 5 * MK, Y + 4 * MK); ctx.closePath();
          ctx.fillStyle = C.post; ctx.fill(); ctx.strokeStyle = "#fff"; ctx.lineWidth = 1; ctx.stroke();
        } else if (S.layers.pads) dot(x, z, p.loot ? 3 : 2.2, p.loot ? C.loot : C.pad);
      });
    });
    // the start
    if (pads.start && set.has(pads.start.R)) { dot(pads.start.x, pads.start.z, 7, "#fff", "#000"); dot(pads.start.x, pads.start.z, 3, "#000"); }
    // labels
    if (S.layers.labels) {
      ctx.textAlign = "left"; ctx.textBaseline = "top";
      mine.forEach(function (R) {
        var w = (R.x1 - R.x0) * S.view.s; if (w < 34) return;
        var fs = Math.max(9, Math.min(13, w / 9));
        ctx.font = fs + "px ui-monospace, Menlo, Consolas, monospace"; ctx.fillStyle = C.text;
        var label = R.name + (R.ramp ? " (" + R.y + "→" + R.ramp_to + ")" : (R.y ? " y" + R.y : ""));
        var tw = ctx.measureText(label).width;
        if (tw > w - 6) label = R.name;
        if (ctx.measureText(label).width > w - 6) return;
        ctx.fillText(label, sx(R.x0) + 3, sy(R.z1) + 3);
      });
    }
    // problems: red outlines
    if (S.layers.issues) {
      ctx.lineWidth = 2;
      S.issues.forEach(function (i) {
        if (!i.ref || i.level === "info") return;
        ctx.strokeStyle = i.level === "error" ? C.err : C.warn;
        var b = issueBox(i.ref, set, pads); if (!b) return;
        ctx.beginPath(); rect(b[0], b[1], b[2], b[3]); ctx.stroke();
      });
    }
    drawSelection(set, pads);
    if (S.drag && S.drag.kind === "newroom") {
      var d = S.drag; ctx.setLineDash([6, 4]); ctx.strokeStyle = C.sel; ctx.lineWidth = 2; ctx.beginPath(); rect(Math.min(d.x0, d.x1), Math.min(d.z0, d.z1), Math.max(d.x0, d.x1), Math.max(d.z0, d.z1)); ctx.stroke(); ctx.setLineDash([]);
    }
    drawScale();
  }

  function drawGrid() {
    var step = S.view.s > 0.3 ? 100 : S.view.s > 0.06 ? 500 : 1000;
    var x0 = Math.floor(wx(0) / step) * step, x1 = wx(W), z1 = Math.ceil(wz(0) / step) * step, z0 = wz(H);
    ctx.lineWidth = 1;
    for (var x = x0; x <= x1; x += step) { ctx.strokeStyle = x % (step * 5) === 0 ? C.grid2 : C.grid; ctx.beginPath(); ctx.moveTo(Math.round(sx(x)) + 0.5, 0); ctx.lineTo(Math.round(sx(x)) + 0.5, H); ctx.stroke(); }
    for (var z = z1; z >= z0; z -= step) { ctx.strokeStyle = z % (step * 5) === 0 ? C.grid2 : C.grid; ctx.beginPath(); ctx.moveTo(0, Math.round(sy(z)) + 0.5); ctx.lineTo(W, Math.round(sy(z)) + 0.5); ctx.stroke(); }
  }
  function drawScale() {
    var m = [100, 200, 500, 1000, 2000, 5000].find(function (v) { return v * S.view.s >= 60; }) || 5000;
    var px = m * S.view.s, x = 14, y = H - 16;
    ctx.fillStyle = "rgba(20,21,26,0.78)"; ctx.fillRect(x - 8, y - 24, Math.max(px, 120) + 16, 32);
    ctx.strokeStyle = "#c9ccd6"; ctx.lineWidth = 2; ctx.beginPath(); ctx.moveTo(x, y); ctx.lineTo(x + px, y); ctx.moveTo(x, y - 4); ctx.lineTo(x, y + 4); ctx.moveTo(x + px, y - 4); ctx.lineTo(x + px, y + 4); ctx.stroke();
    ctx.fillStyle = "#c9ccd6"; ctx.font = "11px system-ui, sans-serif"; ctx.textAlign = "left"; ctx.textBaseline = "bottom";
    ctx.fillText(m >= 100 ? (m / 100) + " m (" + m + " units)" : m + " units", x, y - 6);
  }
  function arrow(x0, z0, x1, z1, col) {
    var a = sx(x0), b = sy(z0), c = sx(x1), d = sy(z1), ang = Math.atan2(d - b, c - a);
    ctx.strokeStyle = col; ctx.lineWidth = 1.5; ctx.beginPath(); ctx.moveTo(a, b); ctx.lineTo(c, d);
    ctx.lineTo(c - 7 * Math.cos(ang - 0.5), d - 7 * Math.sin(ang - 0.5)); ctx.moveTo(c, d); ctx.lineTo(c - 7 * Math.cos(ang + 0.5), d - 7 * Math.sin(ang + 0.5)); ctx.stroke();
  }
  function link(p, q, col, label, set) {
    if (!p || !q || !(set.has(p.R) || set.has(q.R))) return;
    ctx.strokeStyle = col; ctx.lineWidth = 2; ctx.setLineDash([3, 4]); ctx.beginPath(); ctx.moveTo(sx(p.x), sy(p.z)); ctx.lineTo(sx(q.x), sy(q.z)); ctx.stroke(); ctx.setLineDash([]);
    [p, q].forEach(function (e) {
      var X = sx(e.x), Y = sy(e.z), on = set.has(e.R);
      ctx.beginPath(); ctx.moveTo(X, Y - 6 * MK); ctx.lineTo(X + 6 * MK, Y); ctx.lineTo(X, Y + 6 * MK); ctx.lineTo(X - 6 * MK, Y); ctx.closePath();
      ctx.fillStyle = on ? col : "rgba(0,0,0,0)"; ctx.fill(); ctx.strokeStyle = col; ctx.lineWidth = 1.5; ctx.stroke();
    });
    if (S.view.s > 0.05) { ctx.fillStyle = col; ctx.font = "11px system-ui, sans-serif"; ctx.textAlign = "left"; ctx.textBaseline = "middle"; var e = set.has(p.R) ? p : q; ctx.fillText(label, sx(e.x) + 9, sy(e.z)); }
  }
  function drawCamera(R, o, pads) {
    if (["x0", "x1", "z0", "z1"].indexOf(o.wall) < 0) return;
    var m = wallPoint(R, o.wall, LL.num(o.along), 12), tx, tz;
    var w = o.watch;
    if (typeof w === "string" && pads[w]) { tx = pads[w].x; tz = pads[w].z; }
    else if (Array.isArray(w)) { tx = LL.num(w[0]); tz = LL.num(w[1]); }
    else { tx = (R.x0 + R.x1) / 2; tz = (R.z0 + R.z1) / 2; }
    var X = sx(m[0]), Y = sy(m[1]), TX = sx(tx), TY = sy(tz);
    var ang = Math.atan2(TY - Y, TX - X), rng = Math.hypot(TX - X, TY - Y) + 40 * S.view.s * 10;
    var sw = (o.sweep || [30, -30]).map(LL.num), half = 22 * Math.PI / 180;   // the lens's half view, plus its sweep
    var a0 = ang - Math.max(sw[0], sw[1]) * Math.PI / 180 - half, a1 = ang - Math.min(sw[0], sw[1]) * Math.PI / 180 + half;
    ctx.save();
    ctx.beginPath(); rect(R.x0, R.z0, R.x1, R.z1); ctx.clip();
    ctx.beginPath(); ctx.moveTo(X, Y); ctx.arc(X, Y, rng, a0, a1); ctx.closePath();
    ctx.fillStyle = "rgba(255,120,0,0.2)"; ctx.fill();
    ctx.strokeStyle = "rgba(255,120,0,0.55)"; ctx.lineWidth = 1; ctx.beginPath(); ctx.moveTo(X, Y); ctx.lineTo(TX, TY); ctx.stroke();
    ctx.restore();
    dot(m[0], m[1], 5, C.cam, "#000");
  }
  function issueBox(r, set, pads) {
    if (r.kind === "room" || r.kind === "core") {
      var R = r.kind === "room" ? S.M.byname[r.name] : S.M.rooms.find(function (q) { return q.core === r.index; });
      return R && set.has(R) ? [R.x0, R.z0, R.x1, R.z1] : null;
    }
    if (r.kind === "door" || r.kind === "opening") {
      var D = (r.kind === "door" ? S.M.doors : S.M.openings).find(function (q) { return q.index === r.index; });
      if (!D || !D.a || !(set.has(D.a) || set.has(D.b))) return null;
      var b = doorBox(D);
      if (!b) { var A = D.a, B = D.b; return [Math.min(A.x0, B.x0), Math.min(A.z0, B.z0), Math.max(A.x1, B.x1), Math.max(A.z1, B.z1)]; }
      return [b[0] - 20, b[1] - 20, b[2] + 20, b[3] + 20];
    }
    if (r.kind === "pad" || r.kind === "start") {
      var p = pads[r.kind === "start" ? "start" : r.name];
      return p && set.has(p.R) ? [p.x - 40, p.z - 40, p.x + 40, p.z + 40] : null;
    }
    if (r.kind === "object") {
      var R2 = S.M.byname[r.room]; if (!R2 || !set.has(R2)) return null;
      var o = R2.objects[r.index]; var pt = objPoint(R2, o); if (!pt) return null;
      return [pt[0] - 40, pt[1] - 40, pt[0] + 40, pt[1] + 40];
    }
    return null;
  }
  function objPoint(R, o) {
    if (!o) return null;
    if (o.at) return [LL.num(o.at[0]), LL.num(o.at[1])];
    if (["x0", "x1", "z0", "z1"].indexOf(o.wall) >= 0) return wallPoint(R, o.wall, LL.num(o.along), 12);
    return null;
  }

  // ------------------------------------------------------------------------------------------------------------ selection ---
  function selTarget(sel) {
    if (!sel || !S.M) return null;
    if (sel.kind === "room") return S.M.byname[sel.name] || null;
    if (sel.kind === "core") return (S.L.stairwells || [])[sel.index] || null;
    if (sel.kind === "door") return S.M.doors.find(function (D) { return D.index === sel.index; }) || null;
    if (sel.kind === "opening") return S.M.openings.find(function (D) { return D.index === sel.index; }) || null;
    if (sel.kind === "pad") return padIndex()[sel.name] || null;
    if (sel.kind === "object") { var R = S.M.byname[sel.room]; return R && R.objects[sel.index] ? { R: R, o: R.objects[sel.index] } : null; }
    if (sel.kind === "start") return padIndex().start || null;
    return null;
  }
  function selBox(sel) {
    var t = selTarget(sel); if (!t) return null;
    if (sel.kind === "room") return [t.x0, t.z0, t.x1, t.z1];
    if (sel.kind === "core") { var r = t.rect.map(LL.num); return r; }
    if (sel.kind === "door") { var b = doorBox(t); return b && [b[0] - 15, b[1] - 15, b[2] + 15, b[3] + 15]; }
    if (sel.kind === "opening") { if (t.axis === undefined) return null; var f0 = Math.min(t.fa, t.fb), f1 = Math.max(t.fa, t.fb); return t.axis === "x" ? [f0 - 15, t.lo, f1 + 15, t.hi] : [t.lo, f0 - 15, t.hi, f1 + 15]; }
    if (sel.kind === "pad" || sel.kind === "start") return [t.x - 30, t.z - 30, t.x + 30, t.z + 30];
    if (sel.kind === "object") { var p = objPoint(t.R, t.o); return p && [p[0] - 30, p[1] - 30, p[0] + 30, p[1] + 30]; }
    return null;
  }
  function drawSelection() {
    var b = selBox(S.sel); if (!b) return;
    ctx.strokeStyle = C.sel; ctx.lineWidth = 2; ctx.setLineDash([5, 3]); ctx.beginPath(); rect(b[0], b[1], b[2], b[3]); ctx.stroke(); ctx.setLineDash([]);
    if (S.sel.kind === "room" && !S.readOnly) handles(b).forEach(function (h) { ctx.fillStyle = C.sel; ctx.fillRect(h.px - 5, h.py - 5, 10, 10); ctx.strokeStyle = "#000"; ctx.lineWidth = 1; ctx.strokeRect(h.px - 5, h.py - 5, 10, 10); });
  }
  function handles(b) {
    var xs = { w: b[0], c: (b[0] + b[2]) / 2, e: b[2] }, zs = { s: b[1], c: (b[1] + b[3]) / 2, n: b[3] }, out = [];
    ["n", "c", "s"].forEach(function (v) { ["w", "c", "e"].forEach(function (u) { if (u === "c" && v === "c") return; out.push({ h: (v === "c" ? "" : v) + (u === "c" ? "" : u), px: sx(xs[u]), py: sy(zs[v]) }); }); });
    return out;
  }

  function hit(px, py) {
    var x = wx(px), z = wz(py), mine = onLevel(), set = new Set(mine), tol = 9 / S.view.s, pads = padIndex();
    var best = null, bd = Infinity;
    function cand(sel, d) { if (d < bd) { bd = d; best = sel; } }
    if (pads.start && set.has(pads.start.R)) cand({ kind: "start" }, Math.hypot(pads.start.x - x, pads.start.z - z) - tol * 0.2);
    mine.forEach(function (R) {
      if (S.layers.pads || S.layers.posts) R.pads_named.forEach(function (p) { if (p.loot) return; cand({ kind: "pad", name: p.name }, Math.hypot(LL.num(p.at[0]) - x, LL.num(p.at[1]) - z)); });
      R.objects.forEach(function (o, k) { if (R.core !== undefined) return; var pt = objPoint(R, o); if (pt) cand({ kind: "object", room: R.name, index: k }, Math.hypot(pt[0] - x, pt[1] - z)); });
    });
    if (bd <= tol) return best;
    var hits = [];
    S.M.doors.forEach(function (D) { if (!D.a || !(set.has(D.a) || set.has(D.b))) return; var b = doorBox(D); if (b && x >= b[0] - tol && x <= b[2] + tol && z >= b[1] - tol && z <= b[3] + tol) hits.push(D.core !== undefined ? { kind: "core", index: D.core } : { kind: "door", index: D.index }); });
    S.M.openings.forEach(function (D) { if (!D.a || !(set.has(D.a) || set.has(D.b)) || D.axis === undefined) return; var f0 = Math.min(D.fa, D.fb) - 10, f1 = Math.max(D.fa, D.fb) + 10, b = D.axis === "x" ? [f0, D.lo, f1, D.hi] : [D.lo, f0, D.hi, f1]; if (x >= b[0] - tol && x <= b[2] + tol && z >= b[1] - tol && z <= b[3] + tol) hits.push({ kind: "opening", index: D.index }); });
    if (hits.length) return hits[0];
    var R = roomOfLevelAt(x, z, mine);
    if (R) return R.core !== undefined ? { kind: "core", index: R.core, room: R.name } : { kind: "room", name: R.name };
    return null;
  }

  // --------------------------------------------------------------------------------------------------------------- input ---
  var ptrs = new Map();
  cv.addEventListener("pointerdown", function (e) {
    try { cv.setPointerCapture(e.pointerId); } catch (err) { /* a synthetic pointer (selftest.js) */ }
    ptrs.set(e.pointerId, { x: e.offsetX, y: e.offsetY });
    if (ptrs.size === 2) { S.drag = { kind: "pinch", d0: pinchDist(), s0: S.view.s, c: pinchMid() }; return; }
    var px = e.offsetX, py = e.offsetY, x = wx(px), z = wz(py);
    S.down = { px: px, py: py, t: Date.now() };
    if (!S.M) return;
    if (!S.readOnly && S.tool === "door") { addDoorAt(x, z); return; }
    if (!S.readOnly && S.tool === "pad") { addPadAt(x, z); return; }
    if (!S.readOnly && S.tool === "room") { S.drag = { kind: "newroom", x0: snapv(x), z0: snapv(z), x1: snapv(x), z1: snapv(z) }; return; }
    // a resize handle of the selected room
    if (!S.readOnly && S.sel && S.sel.kind === "room") {
      var b = selBox(S.sel), hs = b ? handles(b) : [];
      var hh = hs.find(function (h) { return Math.abs(h.px - px) <= 8 && Math.abs(h.py - py) <= 8; });
      if (hh) { snapshot(); S.drag = { kind: "resize", h: hh.h, name: S.sel.name }; return; }
    }
    var sel = hit(px, py);
    var same = sel && S.sel && JSON.stringify(sel) === JSON.stringify(S.sel);
    if (!sel) { S.sel = null; renderInspector(); S.drag = { kind: "pan", px: px, py: py, cx: S.view.cx, cz: S.view.cz }; draw(); return; }
    S.sel = sel; renderInspector(); draw();
    if (S.readOnly || (sel.kind === "core" && !same && false)) { S.drag = { kind: "pan", px: px, py: py, cx: S.view.cx, cz: S.view.cz, tap: true }; return; }
    S.drag = { kind: "move", sel: sel, x0: x, z0: z, applied: [0, 0], started: false };
  });
  cv.addEventListener("pointermove", function (e) {
    if (ptrs.has(e.pointerId)) ptrs.set(e.pointerId, { x: e.offsetX, y: e.offsetY });
    var d = S.drag; if (!d) { hover(e.offsetX, e.offsetY); return; }
    var px = e.offsetX, py = e.offsetY, x = wx(px), z = wz(py);
    if (d.kind === "pinch" && ptrs.size === 2) {
      var c = pinchMid(), wx0 = wx(d.c.x), wz0 = wz(d.c.y);
      S.view.s = clampS(d.s0 * pinchDist() / d.d0);
      S.view.cx = wx0 - (c.x - W / 2) / S.view.s; S.view.cz = wz0 + (c.y - H / 2) / S.view.s;
      draw(); return;
    }
    if (d.kind === "pan") { S.view.cx = d.cx - (px - d.px) / S.view.s; S.view.cz = d.cz + (py - d.py) / S.view.s; draw(); return; }
    if (d.kind === "newroom") { d.x1 = snapv(x); d.z1 = snapv(z); draw(); return; }
    if (d.kind === "resize") { resizeTo(d, snapv(x), snapv(z)); return; }
    if (d.kind === "move") {
      if (!d.started) { if (Math.hypot(px - S.down.px, py - S.down.py) < 4) return; d.started = true; snapshot(); }
      moveSel(d, x, z);
    }
  });
  function endPtr(e) {
    ptrs.delete(e.pointerId);
    var d = S.drag; S.drag = null;
    if (!d) return;
    if (d.kind === "pinch") { return; }
    if (d.kind === "newroom") {
      var r = [Math.min(d.x0, d.x1), Math.min(d.z0, d.z1), Math.max(d.x0, d.x1), Math.max(d.z0, d.z1)];
      if (r[2] - r[0] >= 100 && r[3] - r[1] >= 100) addRoom(r);
      draw(); return;
    }
    if (d.kind === "move" && !d.started) return;
    if (d.kind === "move" || d.kind === "resize") refresh();
  }
  cv.addEventListener("pointerup", endPtr);
  cv.addEventListener("pointercancel", endPtr);
  cv.addEventListener("wheel", function (e) {
    e.preventDefault();
    var x = wx(e.offsetX), z = wz(e.offsetY);
    S.view.s = clampS(S.view.s * Math.pow(1.0018, -e.deltaY));
    S.view.cx = x - (e.offsetX - W / 2) / S.view.s; S.view.cz = z + (e.offsetY - H / 2) / S.view.s;
    draw();
  }, { passive: false });
  function clampS(s) { return Math.min(4, Math.max(0.01, s)); }
  function pinchDist() { var a = Array.from(ptrs.values()); return Math.hypot(a[0].x - a[1].x, a[0].y - a[1].y) || 1; }
  function pinchMid() { var a = Array.from(ptrs.values()); return { x: (a[0].x + a[1].x) / 2, y: (a[0].y + a[1].y) / 2 }; }
  function hover(px, py) {
    if (!S.M) return;
    $("coords").textContent = "x " + Math.round(wx(px)) + "  z " + Math.round(wz(py));
  }

  function moveSel(d, x, z) {
    var sel = d.sel, L = S.L;
    var dx = snapv(x - d.x0), dz = snapv(z - d.z0), ddx = dx - d.applied[0], ddz = dz - d.applied[1];
    if (sel.kind === "room") { if (!ddx && !ddz) return; ED.moveRoom(L, sel.name, ddx, ddz); d.applied = [dx, dz]; }
    else if (sel.kind === "core") { if (!ddx && !ddz) return; ED.moveCore(L, sel.index, ddx, ddz); d.applied = [dx, dz]; }
    else if (sel.kind === "door") {
      var D = S.M.doors.find(function (q) { return q.index === sel.index; }); if (!D || D.axis === undefined) return;
      var at = snapv(D.axis === "x" ? z : x); if (LL.num(L.doors[sel.index].at) === at) return;
      L.doors[sel.index].at = at;
    } else if (sel.kind === "opening") {
      var O = S.M.openings.find(function (q) { return q.index === sel.index; }); if (!O || O.axis === undefined) return;
      var at2 = snapv(O.axis === "x" ? z : x); if (LL.num(L.openings[sel.index].at) === at2) return;
      L.openings[sel.index].at = at2;
    } else if (sel.kind === "pad") {
      var nx = snapv(x), nz = snapv(z), R = roomOfLevelAt(nx, nz), f = ED.findPad(L, sel.name);
      if (!f) return;
      ED.movePad(L, sel.name, [nx, nz], R && R.core === undefined ? R.name : null);
    } else if (sel.kind === "object") {
      var R2 = S.M.byname[sel.room], o = R2.objects[sel.index];
      if (o.at) ED.moveFloorObject(L, sel.room, sel.index, [snapv(x), snapv(z)]);
      else {
        var cands = [["x0", Math.abs(x - R2.x0), z], ["x1", Math.abs(x - R2.x1), z], ["z0", Math.abs(z - R2.z0), x], ["z1", Math.abs(z - R2.z1), x]];
        cands.sort(function (a, b) { return a[1] - b[1]; });
        var w = cands[0], lo = w[0][0] === "x" ? R2.z0 : R2.x0, hi = w[0][0] === "x" ? R2.z1 : R2.x1;
        ED.moveWallObject(L, sel.room, sel.index, w[0], Math.min(hi, Math.max(lo, snapv(w[2]))));
      }
    } else if (sel.kind === "start") {
      var R3 = roomOfLevelAt(snapv(x), snapv(z)); if (!R3) return;
      var st = L.start, old = st.at || [0, 0], fx = st.face ? LL.num(st.face[0]) - LL.num(old[0]) : 0, fz = st.face ? LL.num(st.face[1]) - LL.num(old[1]) : 100;
      st.room = R3.name; st.at = [snapv(x), snapv(z)]; if (st.face) st.face = [st.at[0] + fx, st.at[1] + fz];
    }
    refresh(true);
  }
  function resizeTo(d, x, z) {
    var r = S.L.rooms.find(function (q) { return q.name === d.name; }); if (!r) return;
    var c = r.rect.map(LL.num);
    if (d.h.indexOf("w") >= 0) c[0] = Math.min(x, c[2] - 100);
    if (d.h.indexOf("e") >= 0) c[2] = Math.max(x, c[0] + 100);
    if (d.h.indexOf("s") >= 0) c[1] = Math.min(z, c[3] - 100);
    if (d.h.indexOf("n") >= 0) c[3] = Math.max(z, c[1] + 100);
    if (c.join() === r.rect.map(LL.num).join()) return;
    ED.setRect(S.L, d.name, c);
    refresh(true);
  }

  function addDoorAt(x, z) {
    var mine = onLevel().filter(function (R) { return !R.ramp || true; }), wallmax = S.M.wallmax, best = null;
    for (var i = 0; i < mine.length; i++) for (var j = 0; j < mine.length; j++) {
      if (i === j) continue;
      var A = mine[i], B = mine[j], f = LL.facing(A, B, wallmax); if (!f || i > j) continue;
      var f0 = Math.min(f[1], f[2]), f1 = Math.max(f[1], f[2]), lo = f[3], hi = f[4], c = f[0] === "x" ? x : z, al = f[0] === "x" ? z : x;
      var slack = 40 / S.view.s / 4 + 30;
      if (c >= f0 - slack && c <= f1 + slack && al >= lo + 20 && al <= hi - 20) {
        var dist = Math.abs(c - (f0 + f1) / 2);
        if (!best || dist < best.dist) best = { A: A, B: B, f: f, al: al, dist: dist };
      }
    }
    if (!best) { toast("Click on a wall two rooms share"); return; }
    if (best.A.core !== undefined && best.B.core !== undefined) { toast("Both rooms are a stair core's"); return; }
    var w = LL.num((S.L.defaults || {}).door_width) || 180, lo2 = best.f[3] + 20 + w / 2, hi2 = best.f[4] - 20 - w / 2;
    if (hi2 < lo2) { w = Math.max(60, best.f[4] - best.f[3] - 40); lo2 = hi2 = (best.f[3] + best.f[4]) / 2; }
    var at = Math.min(hi2, Math.max(lo2, snapv(best.al)));
    doEdit(function () { return ED.addDoor(S.L, best.A.name, best.B.name, Math.round(at), Math.round(w)); });
    S.sel = { kind: "door", index: S.L.doors.length - 1 }; renderInspector(); draw();
  }
  function addPadAt(x, z) {
    var R = roomOfLevelAt(x, z);
    if (!R) { toast("Click inside a room"); return; }
    if (R.core !== undefined) { toast("A stair core's rooms take no pads here"); return; }
    var k = 1, have = padIndex(); while (have[R.name + "_pad" + k]) k++;
    var name = R.name + "_pad" + k;
    doEdit(function () { return ED.addPad(S.L, R.name, name, [snapv(x), snapv(z)]); });
    S.sel = { kind: "pad", name: name }; renderInspector(); draw();
  }
  function addRoom(r) {
    var like = roomOfLevelAt((r[0] + r[2]) / 2, (r[1] + r[3]) / 2) || onLevel().find(function (R) { return !R.ramp && R.core === undefined; });
    var k = 1; while (S.M.byname["room" + k]) k++;
    doEdit(function () {
      var m = ED.addRoom(S.L, "room" + k, r, like && like.core === undefined ? like.name : null);
      var nr = S.L.rooms[S.L.rooms.length - 1];
      if (!like) nr.floor = S.lv[S.level];
      return m;
    });
    S.sel = { kind: "room", name: "room" + k }; setTool("select"); renderInspector(); draw();
  }
  function deleteSel() {
    var s = S.sel; if (!s || S.readOnly) return;
    if (s.kind === "room") doEdit(function () { return ED.removeRoom(S.L, s.name); });
    else if (s.kind === "door") doEdit(function () { return ED.removeDoor(S.L, s.index); });
    else if (s.kind === "opening") doEdit(function () { return ED.removeOpening(S.L, s.index); });
    else if (s.kind === "pad") doEdit(function () { return ED.removePad(S.L, s.name); });
    else if (s.kind === "object") doEdit(function () { return ED.removeObject(S.L, s.room, s.index); });
    else return;
    S.sel = null; renderInspector(); draw();
  }

  // ---------------------------------------------------------------------------------------------------------- the panels ---
  function renderTabs() {
    var el = $("levels"); el.innerHTML = "";
    S.lv.forEach(function (y, k) {
      var b = document.createElement("button");
      b.textContent = (LL.LEVEL_NAMES[String(y)] || "y" + y);
      b.title = "level at y " + y;
      b.className = k === S.level ? "on" : "";
      b.onclick = function () { S.level = k; renderTabs(); draw(); };
      el.appendChild(b);
    });
  }
  function renderStats() {
    var M = S.M, lim = LL.LIMITS, e = S.issues.filter(function (i) { return i.level === "error"; }).length, w = S.issues.filter(function (i) { return i.level === "warn"; }).length;
    function chip(label, n, max) { return '<span class="chip' + (n > max ? " bad" : n > max * 0.95 ? " near" : "") + '">' + label + " <b>" + n + "</b>/" + max + "</span>"; }
    $("stats").innerHTML = chip("rooms", M.rooms.length, lim.rooms) + chip("portals", M.nportals || (M.doors.length + M.openings.length), lim.portals) +
      chip("cameras", M.ncam || 0, lim.cameras) +
      '<span class="chip ' + (e ? "bad" : "ok") + '">' + (e ? e + " error" + (e > 1 ? "s" : "") : "no errors") + "</span>" +
      (w ? '<span class="chip near">' + w + " warning" + (w > 1 ? "s" : "") + "</span>" : "") + (S.dirty ? '<span class="chip edited">edited</span>' : "");
  }
  function renderIssues() {
    var el = $("issues"); el.innerHTML = "";
    $("issue-count").textContent = S.issues.length ? "(" + S.issues.length + ")" : "";
    if (!S.issues.length) { el.innerHTML = '<p class="muted">Nothing to report: gen_level should accept this layout.</p>'; return; }
    var order = { error: 0, warn: 1, info: 2 };
    S.issues.slice().sort(function (a, b) { return order[a.level] - order[b.level]; }).forEach(function (i) {
      var li = document.createElement("li");
      li.className = i.level;
      li.innerHTML = '<span class="tag">' + (i.level === "error" ? (i.gen ? "gen_level" : "engine") : i.level) + "</span> " + esc(i.msg);
      if (i.ref) li.onclick = function () { goTo(i.ref, true); };
      el.appendChild(li);
    });
  }
  function goTo(ref, scroll) {
    var sel = null;
    if (ref.kind === "room") sel = { kind: "room", name: ref.name };
    else if (ref.kind === "door") sel = S.M.doors.some(function (D) { return D.index === ref.index; }) ? { kind: "door", index: ref.index } : null;
    else if (ref.kind === "opening") sel = { kind: "opening", index: ref.index };
    else if (ref.kind === "pad") sel = { kind: "pad", name: ref.name };
    else if (ref.kind === "object") sel = { kind: "object", room: ref.room, index: ref.index };
    else if (ref.kind === "core") sel = { kind: "core", index: ref.index };
    else if (ref.kind === "start") sel = { kind: "start" };
    if (!sel) return;
    // the level that shows it
    var R = sel.kind === "room" ? S.M.byname[sel.name] : sel.kind === "object" ? S.M.byname[sel.room] : sel.kind === "pad" ? (padIndex()[sel.name] || {}).R :
      sel.kind === "start" ? (padIndex().start || {}).R : sel.kind === "core" ? S.M.rooms.find(function (q) { return q.core === sel.index; }) :
      (selTarget(sel) || {}).a;
    if (R) for (var k = 0; k < S.lv.length; k++) if (LL.roomsOnLevel(S.M, S.lv, k).indexOf(R) >= 0) { S.level = k; break; }
    S.sel = sel; renderTabs();
    var b = selBox(sel) || (R && [R.x0, R.z0, R.x1, R.z1]);
    if (b) focusOn(b[0] - 300, b[1] - 300, b[2] + 300, b[3] + 300);
    renderInspector(); draw();
    if (scroll && window.matchMedia("(max-width: 760px)").matches) document.getElementById("plan").scrollIntoView({ behavior: "smooth" });
  }

  function esc(s) { return String(s).replace(/[&<>"]/g, function (c) { return { "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[c]; }); }
  function field(label, value, onchange, opts) {
    opts = opts || {};
    var row = document.createElement("label"); row.className = "row";
    var span = document.createElement("span"); span.textContent = label; row.appendChild(span);
    if (S.readOnly) {             // view only (phones): the value as text
      var v = document.createElement("b"); v.className = "val";
      var opt = opts.options && opts.options.find(function (o) { return String(o[0]) === String(value); });
      v.textContent = opts.check ? (value ? "yes" : "no") : opt ? opt[1] : (value === undefined || value === null || value === "" ? "—" : String(value));
      row.appendChild(v);
      return row;
    }
    var inp;
    if (opts.options) {
      inp = document.createElement("select");
      opts.options.forEach(function (o) { var op = document.createElement("option"); op.value = o[0]; op.textContent = o[1]; inp.appendChild(op); });
      inp.value = value;
    } else if (opts.check) { inp = document.createElement("input"); inp.type = "checkbox"; inp.checked = !!value; }
    else { inp = document.createElement("input"); inp.type = opts.num ? "number" : "text"; inp.value = value === undefined || value === null ? "" : value; if (opts.num) inp.step = opts.step || S.snap || 1; }
    inp.disabled = S.readOnly || !!opts.ro;
    inp.addEventListener("change", function () {
      var v = opts.check ? inp.checked : opts.num ? (inp.value === "" ? undefined : Number(inp.value)) : inp.value;
      onchange(v);
    });
    row.appendChild(inp);
    return row;
  }
  function info(html) { var p = document.createElement("p"); p.className = "kv"; p.innerHTML = html; return p; }
  function button(label, fn, cls) { var b = document.createElement("button"); b.textContent = label; b.onclick = fn; b.className = (cls || "") + " edit-only"; return b; }

  var RULES = [["0", "unlocked"], ["2", "GREEN"], ["4", "BLUE"], ["8", "RED"], ["16", "GOLD"], ["1", "floor key"], ["3", "GREEN + floor key"], ["1073741824", "VAULT (cracker only)"]];
  function renderInspector() {
    var el = $("inspector"); el.innerHTML = "";
    var s = S.sel, t = selTarget(s);
    if (!s || !t) {
      el.appendChild(info(S.M ? "Tap a room, doorway, pad, camera or the start to see it" + (S.readOnly ? "." : "; drag to move it. Tools: <b>Door</b> clicks a shared wall, <b>Pad</b> a room, <b>Room</b> drags a box.") : "No layout loaded."));
      return;
    }
    var h = document.createElement("h3"); el.appendChild(h);
    var probs = issuesFor(s.kind === "room" ? { kind: "room", name: s.name } : s);
    if (s.kind === "room") {
      var R = t, r = R.src;
      h.textContent = "Room " + R.name;
      el.appendChild(info("id " + R.id + " &middot; " + (R.x1 - R.x0) + " x " + (R.z1 - R.z0) + " &middot; floor " + R.y + (R.ramp ? " &rarr; " + R.ramp_to + " (" + (R.stairs ? R.stairs + " stairs" : "ramp") + " along " + R.ramp.axis + ")" : "") + " &middot; height " + R.h +
        "<br>" + R.doors.length + " doorways, " + R.openings.length + " openings, " + R.pads_named.length + " pads, " + R.objects.length + " objects, " + R.blocks.length + " blocks" + (R.area ? "<br>area " + esc(R.area) : "")));
      el.appendChild(field("name", R.name, function (v) { doEdit(function () { var m = ED.renameRoom(S.L, R.name, v); S.sel = { kind: "room", name: v }; return m; }); }));
      var rc = r.rect.map(LL.num);
      ["x0", "z0", "x1", "z1"].forEach(function (k, i) { el.appendChild(field(k, rc[i], function (v) { doEdit(function () { var c = r.rect.map(LL.num); c[i] = v; return ED.setRect(S.L, R.name, c); }); }, { num: true })); });
      el.appendChild(field("floor", r.floor, function (v) { doEdit(function () { return ED.setRoomField(S.L, R.name, "floor", v); }); }, { num: true }));
      el.appendChild(field("height", r.height, function (v) { doEdit(function () { return ED.setRoomField(S.L, R.name, "height", v); }); }, { num: true }));
      el.appendChild(field("zone", r.zone || "", function (v) { doEdit(function () { return ED.setRoomField(S.L, R.name, "zone", v); }); }, { options: [["", "(none)"], ["public", "public"], ["staff", "staff"], ["restricted", "restricted"]] }));
      el.appendChild(field("area", r.area || "", function (v) { doEdit(function () { return ED.setRoomField(S.L, R.name, "area", v); }); }));
      el.appendChild(field("open top", r.open_top, function (v) { doEdit(function () { return ED.setRoomField(S.L, R.name, "open_top", v ? true : undefined); }); }, { check: true }));
      el.appendChild(button("Delete room and its doorways", deleteSel, "danger"));
    } else if (s.kind === "core") {
      h.textContent = "Stair core " + t.name;
      el.appendChild(info("A switchback stair (" + (t.levels || []).join(", ") + ") that gen_level expands into flights and landings. Drag to move the whole core; its rooms are not edited one by one."));
      var cr = t.rect.map(LL.num);
      ["x0", "z0", "x1", "z1"].forEach(function (k, i) { el.appendChild(field(k, cr[i], function (v) { doEdit(function () { var c = t.rect.map(LL.num); c[i] = v; t.rect = c; return "stair core " + t.name + ": rect"; }); }, { num: true })); });
    } else if (s.kind === "door") {
      var D = t, dd = D.src, o = dd.object || null, kf = o ? LL.ival(o.keyflags, 0) : 0;
      h.innerHTML = "Doorway " + esc(D.name) + ' <span class="sw" style="background:' + (o ? LL.keyColour(kf) : "#fafafa") + '"></span>';
      el.appendChild(info(esc(dd.rooms[0]) + " &harr; " + esc(dd.rooms[1]) + " &middot; portal " + D.portal + (D.axis ? " &middot; wall along " + (D.axis === "x" ? "z" : "x") + " " + D.wlo + ".." + D.whi : "") + (o ? "<br>" + esc(o.type || "slide") + " door <b>" + esc(o.name || "") + "</b>: " + LL.keyLabel(kf) : "<br>open doorway (no door)")));
      el.appendChild(field("at", dd.at, function (v) { doEdit(function () { return ED.setDoorField(S.L, s.index, "at", v); }); }, { num: true }));
      el.appendChild(field("width", dd.width, function (v) { doEdit(function () { return ED.setDoorField(S.L, s.index, "width", v); }); }, { num: true }));
      el.appendChild(field("height", dd.height, function (v) { doEdit(function () { return ED.setDoorField(S.L, s.index, "height", v); }); }, { num: true }));
      el.appendChild(field("name", dd.name, function (v) { doEdit(function () { return ED.setDoorField(S.L, s.index, "name", v); }); }));
      el.appendChild(field("door", o ? (o.type || "slide") : "open", function (v) { doEdit(function () { return ED.setDoorRule(S.L, s.index, { kind: v }); }); }, { options: [["open", "none (open doorway)"], ["slide", "sliding door"], ["shutter", "shutter"]] }));
      if (o) {
        var known = RULES.some(function (r) { return Number(r[0]) === kf; });
        el.appendChild(field("key rule", known ? String(kf) : "custom", function (v) { if (v === "custom") return; doEdit(function () { return ED.setDoorRule(S.L, s.index, { kind: o.type || "slide", keyflags: Number(v) }); }); },
          { options: RULES.concat(known ? [] : [["custom", "0x" + kf.toString(16) + " (custom)"]]) }));
        ["double", "see_through", "padlock", "open"].forEach(function (k) {
          el.appendChild(field(k.replace("_", " "), o[k], function (v) { doEdit(function () { var rule = { kind: o.type || "slide" }; rule[k] = v; return ED.setDoorRule(S.L, s.index, rule); }); }, { check: true }));
        });
        el.appendChild(field("guard access", o.guard_access || "", function (v) { doEdit(function () { return ED.setDoorRule(S.L, s.index, { kind: o.type || "slide", guard_access: v }); }); }));
      }
      el.appendChild(button("Delete doorway", deleteSel, "danger"));
    } else if (s.kind === "opening") {
      var O = t, oo = O.src;
      h.textContent = "Opening " + O.name;
      el.appendChild(info(esc(oo.rooms[0]) + " &harr; " + esc(oo.rooms[1]) + " &middot; a see-through hole (no floor, nobody walks through) &middot; portal " + O.portal));
      el.appendChild(field("at", oo.at, function (v) { doEdit(function () { return ED.setDoorField(S.L, s.index, "at", v, "openings"); }); }, { num: true }));
      el.appendChild(field("width", oo.width, function (v) { doEdit(function () { return ED.setDoorField(S.L, s.index, "width", v, "openings"); }); }, { num: true }));
      el.appendChild(field("y bottom", (oo.y || [])[0], function (v) { doEdit(function () { return ED.setDoorField(S.L, s.index, "y", [v, LL.num(oo.y[1])], "openings"); }); }, { num: true }));
      el.appendChild(field("y top", (oo.y || [])[1], function (v) { doEdit(function () { return ED.setDoorField(S.L, s.index, "y", [LL.num(oo.y[0]), v], "openings"); }); }, { num: true }));
      el.appendChild(button("Delete opening", deleteSel, "danger"));
    } else if (s.kind === "pad" || s.kind === "start") {
      var P = t, tg = s.kind === "pad" ? (padTags()[s.name] || []) : [];
      h.textContent = s.kind === "start" ? "Bond's start" : "Pad " + s.name;
      el.appendChild(info("in " + esc(P.R.name) + " at (" + P.x + ", " + P.z + ")" + (P.p && P.p.loot ? "<br>loot spawn (" + esc(P.p.loot) + ")" : "") + (tg.length ? "<br>" + tg.map(esc).join("<br>") : "")));
      if (s.kind === "pad" && !(P.p && P.p.loot)) {
        el.appendChild(field("name", s.name, function (v) { doEdit(function () { var m = ED.renamePad(S.L, s.name, v); S.sel = { kind: "pad", name: v }; return m; }); }));
        el.appendChild(field("x", P.x, function (v) { doEdit(function () { return ED.movePad(S.L, s.name, [v, P.z]); }); }, { num: true }));
        el.appendChild(field("z", P.z, function (v) { doEdit(function () { return ED.movePad(S.L, s.name, [P.x, v]); }); }, { num: true }));
        el.appendChild(button("Delete pad", deleteSel, "danger"));
      }
    } else if (s.kind === "object") {
      var R4 = t.R, ob = t.o;
      h.textContent = (ob.type || "object") + " " + (ob.name || "#" + s.index);
      el.appendChild(info("in " + esc(R4.name) + (ob.wall ? " &middot; on wall " + esc(ob.wall) + " at " + LL.num(ob.along) : ob.at ? " at (" + LL.num(ob.at[0]) + ", " + LL.num(ob.at[1]) + ")" : "") + (ob.model !== undefined ? " &middot; model " + esc(ob.model) : "")));
      el.appendChild(field("name", ob.name, function (v) { doEdit(function () { return ED.setObjectField(S.L, R4.name, s.index, "name", v); }); }));
      if (ob.wall) {
        el.appendChild(field("wall", ob.wall, function (v) { doEdit(function () { return ED.moveWallObject(S.L, R4.name, s.index, v, LL.num(ob.along)); }); }, { options: [["x0", "x0 (west)"], ["x1", "x1 (east)"], ["z0", "z0 (south)"], ["z1", "z1 (north)"]] }));
        el.appendChild(field("along", ob.along, function (v) { doEdit(function () { return ED.moveWallObject(S.L, R4.name, s.index, ob.wall, v); }); }, { num: true }));
        el.appendChild(field("y", ob.y, function (v) { doEdit(function () { return ED.setObjectField(S.L, R4.name, s.index, "y", v); }); }, { num: true }));
      }
      if (ob.type === "camera") {
        el.appendChild(field("watch pad", typeof ob.watch === "string" ? ob.watch : "", function (v) { doEdit(function () { return ED.setObjectField(S.L, R4.name, s.index, "watch", v); }); }));
        var sw = (ob.sweep || [30, -30]).map(LL.num);
        el.appendChild(field("sweep left", sw[0], function (v) { doEdit(function () { return ED.setObjectField(S.L, R4.name, s.index, "sweep", [v, sw[1]]); }); }, { num: true, step: 5 }));
        el.appendChild(field("sweep right", sw[1], function (v) { doEdit(function () { return ED.setObjectField(S.L, R4.name, s.index, "sweep", [sw[0], v]); }); }, { num: true, step: 5 }));
      }
      if (ob.type === "key" || ob.type === "safe") {
        var kf2 = LL.ival(ob.keyflags, ob.type === "key" ? 1 : 16);
        el.appendChild(field("key", String(kf2), function (v) { doEdit(function () { return ED.setObjectField(S.L, R4.name, s.index, "keyflags", "0x" + Number(v).toString(16)); }); }, { options: RULES.filter(function (r) { return r[0] !== "0"; }) }));
      }
      el.appendChild(button("Delete " + (ob.type || "object"), deleteSel, "danger"));
    }
    if (probs.length) {
      var ul = document.createElement("ul"); ul.className = "probs";
      probs.forEach(function (i) { var li = document.createElement("li"); li.className = i.level; li.textContent = i.msg; ul.appendChild(li); });
      el.appendChild(ul);
    }
  }

  // ------------------------------------------------------------------------------------------------------------- the bar ---
  function setTool(t) {
    S.tool = t;
    document.querySelectorAll("[data-tool]").forEach(function (b) { b.classList.toggle("on", b.dataset.tool === t); });
    cv.style.cursor = t === "select" ? "default" : "crosshair";
  }
  function setReadOnly(v) {
    S.readOnly = v; document.body.classList.toggle("readonly", v);
    $("mode").textContent = v ? "View only" : "Editing"; $("mode").classList.toggle("on", !v);
    if (v) setTool("select");
    renderInspector(); draw();
  }
  function exportJSON() {
    if (!S.L) return;
    var text = LL.stringify(S.L), blob = new Blob([text], { type: "application/json" }), a = document.createElement("a");
    S.lastExport = text;
    a.href = URL.createObjectURL(blob); a.download = S.file; document.body.appendChild(a); a.click();
    setTimeout(function () { URL.revokeObjectURL(a.href); a.remove(); }, 1000);
    var e = S.issues.filter(function (i) { return i.level === "error"; }).length;
    toast("Exported " + S.file + (e ? " (with " + e + " errors: gen_level will refuse it)" : ""));
  }
  var toastT = 0;
  function toast(m) { var el = $("toast"); el.textContent = m; el.classList.add("show"); clearTimeout(toastT); toastT = setTimeout(function () { el.classList.remove("show"); }, 2600); }

  function readFile(f) { var r = new FileReader(); r.onload = function () { load(r.result, f.name); }; r.readAsText(f); }
  $("open").addEventListener("change", function (e) { if (e.target.files[0]) readFile(e.target.files[0]); e.target.value = ""; });
  window.addEventListener("dragover", function (e) { e.preventDefault(); document.body.classList.add("dropping"); });
  window.addEventListener("dragleave", function (e) { if (e.target === document.documentElement || e.clientX <= 0) document.body.classList.remove("dropping"); });
  window.addEventListener("drop", function (e) { e.preventDefault(); document.body.classList.remove("dropping"); Array.from(e.dataTransfer.files).forEach(readFile); });
  $("export").onclick = exportJSON;
  $("undo").onclick = undo; $("redo").onclick = redo;
  $("fit").onclick = function () { fit(onLevel()); };
  $("mode").onclick = function () { setReadOnly(!S.readOnly); };
  $("snap").onchange = function (e) { S.snap = Number(e.target.value); };
  document.querySelectorAll("[data-tool]").forEach(function (b) { b.onclick = function () { setTool(b.dataset.tool); }; });
  document.querySelectorAll("[data-layer]").forEach(function (b) {
    b.checked = S.layers[b.dataset.layer];
    b.onchange = function () { S.layers[b.dataset.layer] = b.checked; draw(); };
  });
  document.querySelectorAll(".tabs button[data-pane]").forEach(function (b) {
    b.onclick = function () {
      document.querySelectorAll(".tabs button[data-pane]").forEach(function (q) { q.classList.toggle("on", q === b); });
      document.querySelectorAll(".pane").forEach(function (p) { p.hidden = p.id !== b.dataset.pane; });
    };
  });
  window.addEventListener("keydown", function (e) {
    if (/INPUT|SELECT|TEXTAREA/.test(document.activeElement.tagName)) return;
    var k = e.key.toLowerCase();
    if ((e.ctrlKey || e.metaKey) && k === "z") { e.preventDefault(); if (e.shiftKey) redo(); else undo(); }
    else if ((e.ctrlKey || e.metaKey) && k === "y") { e.preventDefault(); redo(); }
    else if ((e.ctrlKey || e.metaKey) && k === "s") { e.preventDefault(); exportJSON(); }
    else if (k === "delete" || k === "backspace") deleteSel();
    else if (k === "escape") { S.sel = null; setTool("select"); renderInspector(); draw(); }
    else if (k === "f") fit(onLevel());
    else if (k === "v") setTool("select");
    else if (k === "d") setTool("door");
    else if (k === "p") setTool("pad");
    else if (k === "r") setTool("room");
    else if (/^[1-9]$/.test(k) && Number(k) <= S.lv.length) { S.level = Number(k) - 1; renderTabs(); draw(); }
  });
  window.addEventListener("beforeunload", function (e) { if (S.dirty) { e.preventDefault(); e.returnValue = ""; } });
  window.addEventListener("resize", resize);

  // ------------------------------------------------------------------------------------------------------------ start up ---
  resize();
  setReadOnly(window.matchMedia("(max-width: 760px)").matches || window.matchMedia("(pointer: coarse)").matches);
  var embedded = document.getElementById("sample-layout");
  if (embedded && embedded.textContent.trim()) load(embedded.textContent, embedded.dataset.name || "janus_bank_v3.json");
  else if (location.protocol !== "file:") {
    fetch("../janus_bank_v3.json").then(function (r) { if (!r.ok) throw 0; return r.text(); }).then(function (t) { load(t, "janus_bank_v3.json"); }).catch(function () { draw(); });
  }
  window.LevelEditor = { state: S, load: load, refresh: refresh, goTo: goTo, setLevel: function (k) { S.level = k; renderTabs(); draw(); }, select: function (s) { S.sel = s; renderInspector(); draw(); }, focus: focusOn, draw: draw, setReadOnly: setReadOnly };
})();
