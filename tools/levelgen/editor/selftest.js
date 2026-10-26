/*
 * selftest.js - drives the level editor's page as a user would (synthetic pointer and key events) and writes what it found into
 * <pre id="selftest">, for a headless browser's --dump-dom (tools/levelgen/editor/README.md, "Testing"). Loaded by the #selftest
 * deep link. With #selftest&keep=1 it leaves a broken edit on screen for a screenshot (the vault moved onto the deposit hall).
 */
window.LevelEditorSelftest = function (hash) {
  "use strict";
  var E = window.LevelEditor, S = E.state, LL = window.LevelLayout, out = [], fails = 0;
  function ok(cond, what) { out.push((cond ? "PASS " : "FAIL ") + what); if (!cond) fails++; }
  var cv = document.getElementById("plan"), box = cv.getBoundingClientRect();
  function sxy(x, z) { return [box.left + (x - S.view.cx) * S.view.s + box.width / 2, box.top + (S.view.cz - z) * S.view.s + box.height / 2]; }
  function ptr(type, x, z, id) {
    var p = sxy(x, z);
    cv.dispatchEvent(new PointerEvent(type, { clientX: p[0], clientY: p[1], pointerId: id || 7, bubbles: true, isPrimary: true, button: 0 }));
  }
  function key(k, opts) { window.dispatchEvent(new KeyboardEvent("keydown", Object.assign({ key: k, bubbles: true }, opts || {}))); }
  function room(n) { return S.M.byname[n]; }
  try {
    var sample = document.getElementById("sample-layout");
    var original = sample && sample.textContent.trim() ? sample.textContent.replace(/<\\\//g, "</") : LL.stringify(S.L);
    ok(LL.stringify(S.L) === original, "the loaded layout writes back byte for byte (" + original.length + " bytes)");
    ok(!S.issues.some(function (i) { return i.level === "error"; }), "no errors on the shipped layout (" + S.issues.length + " notes)");
    E.setReadOnly(false);

    // select the hall by tapping it, then drag it 200 units east
    E.setLevel(S.lv.indexOf(0));
    E.focus(-1500, -1000, 1500, 3200); E.draw(); box = cv.getBoundingClientRect();
    var hall = room("hall"), cx = (hall.x0 + hall.x1) / 2, cz = hall.z0 + 300;
    ptr("pointerdown", cx, cz); ptr("pointerup", cx, cz);
    ok(S.sel && S.sel.kind === "room" && S.sel.name === "hall", "a tap selects the hall (" + JSON.stringify(S.sel) + ")");
    ok(/Room hall/.test(document.getElementById("inspector").textContent), "the inspector shows the hall");
    var before = room("hall").x0, padBefore = LL.num(LL.clone(S.L.rooms.find(function (r) { return r.name === "hall"; }).pads[0].at[0]));
    ptr("pointerdown", cx, cz); ptr("pointermove", cx + 100, cz); ptr("pointermove", cx + 200, cz); ptr("pointerup", cx + 200, cz);
    ok(room("hall").x0 === before + 200, "dragging moves the hall 200 east (x0 " + before + " -> " + room("hall").x0 + ")");
    ok(LL.num(S.L.rooms.find(function (r) { return r.name === "hall"; }).pads[0].at[0]) === padBefore + 200, "its pads move with it");
    ok(S.issues.some(function (i) { return i.level === "error" && /hall/.test(i.msg); }), "the checks see the moved hall (" +
      (S.issues.find(function (i) { return i.level === "error"; }) || {}).msg + ")");
    key("z", { ctrlKey: true });
    ok(LL.stringify(S.L) === original, "undo restores the layout byte for byte");

    // the door tool: click the middle of a shared wall with no doorway on it yet
    var M = S.M, pair = null;
    var mine = LL.roomsOnLevel(M, S.lv, S.level).filter(function (R) { return R.core === undefined && !R.ramp; });
    for (var i = 0; i < mine.length && !pair; i++) for (var j = 0; j < mine.length && !pair; j++) {
      var f = i !== j && LL.facing(mine[i], mine[j], M.wallmax);
      if (!f || f[4] - f[3] < 500) continue;
      var mid = (f[3] + f[4]) / 2;
      var taken = M.doors.concat(M.openings).some(function (D) { return D.a && ((D.a === mine[i] && D.b === mine[j]) || (D.a === mine[j] && D.b === mine[i])) && D.lo < mid + 150 && D.hi > mid - 150; });
      if (!taken) pair = { a: mine[i], b: mine[j], f: f, mid: mid };
    }
    ok(!!pair, "found a shared wall to put a doorway in" + (pair ? " (" + pair.a.name + " / " + pair.b.name + ")" : ""));
    if (pair) {
      var nd = S.L.doors.length, c = (pair.f[1] + pair.f[2]) / 2;
      E.focus(pair.a.x0, pair.a.z0, pair.a.x1, pair.a.z1); E.draw(); box = cv.getBoundingClientRect();
      document.querySelector('[data-tool="door"]').click();
      var X = pair.f[0] === "x" ? c : pair.mid, Z = pair.f[0] === "x" ? pair.mid : c;
      ptr("pointerdown", X, Z); ptr("pointerup", X, Z);
      ok(S.L.doors.length === nd + 1, "the door tool adds a doorway " + (S.L.doors[nd] ? JSON.stringify(S.L.doors[nd]) : ""));
      ok(!S.issues.some(function (i) { return i.level === "error"; }), "the new doorway passes the checks");
      var rule = document.querySelector("#inspector select");
      ok(!!rule, "the inspector offers the door's rule");
      if (rule) { rule.value = "slide"; rule.dispatchEvent(new Event("change")); }
      ok(S.L.doors[nd] && S.L.doors[nd].object && S.L.doors[nd].object.type === "slide", "setting it to a sliding door adds the door object");
      var sels = Array.from(document.querySelectorAll("#inspector select")), kr = sels[1];
      if (kr) { kr.value = "8"; kr.dispatchEvent(new Event("change")); }
      ok(S.L.doors[nd] && S.L.doors[nd].object && S.L.doors[nd].object.keyflags === "0x8", "the key rule RED writes keyflags 0x8");
      document.querySelector('[data-tool="select"]').click();
      key("z", { ctrlKey: true }); key("z", { ctrlKey: true }); key("z", { ctrlKey: true });
      ok(LL.stringify(S.L) === original, "three undos take the doorway out again");
    }

    // delete a pad a camera watches: the camera's check fails
    var watched = null;
    S.M.rooms.some(function (R) { return R.objects.some(function (o) { if (o.type === "camera" && typeof o.watch === "string" && S.L.rooms.some(function (r) { return (r.pads || []).some(function (p) { return p.name === o.watch; }); })) { watched = o; return true; } return false; }); });
    if (watched) {
      E.goTo({ kind: "pad", name: watched.watch });
      key("Delete");
      ok(S.issues.some(function (i) { return i.level === "error" && i.msg.indexOf("no pad named " + watched.watch) >= 0; }), "deleting pad " + watched.watch + " breaks camera " + watched.name);
      key("z", { ctrlKey: true });
    }

    // export: the file it downloads is the layout
    var got = null, url0 = URL.createObjectURL;
    URL.createObjectURL = function (b) { got = b; return "blob:selftest"; };
    HTMLAnchorElement.prototype._click = HTMLAnchorElement.prototype.click;
    HTMLAnchorElement.prototype.click = function () {};
    document.getElementById("export").click();
    URL.createObjectURL = url0; HTMLAnchorElement.prototype.click = HTMLAnchorElement.prototype._click;
    var finish = function (text) {
      ok(text === original, "Export downloads the layout byte for byte (" + (text || "").length + " bytes)");
      if (hash.keep) {
        // a broken edit to look at: the vault dragged onto the deposit hall
        E.setLevel(S.lv.indexOf(-420));
        var v = room("vault"), dep = room("deposit");
        if (v && dep) { window.LevelEdits.moveRoom(S.L, "vault", Math.round((dep.x0 - v.x0) / 20) * 20, Math.round((dep.z0 + 200 - v.z0) / 20) * 20); S.dirty = true; E.refresh(); }
        E.goTo({ kind: "room", name: "vault" });
        E.focus(v.x0 - 2600, v.z0 - 2200, v.x1 + 2600, v.z1 + 600); E.draw();
        document.querySelector('.tabs button[data-pane="p-issues"]').click();
      }
      var pre = document.createElement("pre"); pre.id = "selftest";
      pre.textContent = out.join("\n") + "\nSELFTEST " + (fails ? "FAIL " + fails : "PASS") + " (" + out.length + " checks)";
      if (!hash.keep) document.body.appendChild(pre); else { pre.hidden = true; document.body.appendChild(pre); }
      document.title = "SELFTEST " + (fails ? "FAIL" : "PASS");
    };
    finish(got ? S.lastExport : null);
  } catch (e) {
    out.push("FAIL exception: " + e.message + "\n" + e.stack);
    var pre = document.createElement("pre"); pre.id = "selftest"; pre.textContent = out.join("\n") + "\nSELFTEST FAIL"; document.body.appendChild(pre);
  }
};
