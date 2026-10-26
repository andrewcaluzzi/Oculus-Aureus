/*
 * edits.js - the level editor's edit operations on a layout (the generator's JSON, edited in place). No DOM: index.html and cli.js
 * (the round-trip test's `edit` command) share them. Each takes the layout L and returns a short description, or throws.
 * Coordinates are world units; callers snap them first.
 */
(function (root, factory) {
  if (typeof module === "object" && module.exports) module.exports = factory(require("./layout.js"));
  else root.LevelEdits = factory(root.LevelLayout);
})(typeof self !== "undefined" ? self : this, function (LL) {
  "use strict";
  var num = LL.num;

  function room(L, name) {
    var r = (L.rooms || []).find(function (r) { return r.name === name; });
    if (!r) throw new Error("no room " + name + " (stair-core rooms are edited through their core)");
    return r;
  }
  function shiftXZ(p, dx, dz) { if (Array.isArray(p) && p.length >= 2 && typeof num(p[0]) === "number") { p[0] = num(p[0]) + dx; p[1] = num(p[1]) + dz; } }

  // move a room and everything in it: pads, loot, blocks, floor objects, wall objects' `along`, lamps, a camera's [x, z] watch, the start
  function moveRoom(L, name, dx, dz) {
    var r = room(L, name);
    r.rect = [num(r.rect[0]) + dx, num(r.rect[1]) + dz, num(r.rect[2]) + dx, num(r.rect[3]) + dz];
    (r.pads || []).forEach(function (p) { shiftXZ(p.at, dx, dz); shiftXZ(p.face, dx, dz); });
    (r.loot || []).forEach(function (lt) { (lt.at || []).forEach(function (a) { shiftXZ(a, dx, dz); }); });
    (r.blocks || []).forEach(function (b) { b.rect = [num(b.rect[0]) + dx, num(b.rect[1]) + dz, num(b.rect[2]) + dx, num(b.rect[3]) + dz]; });
    (r.objects || []).forEach(function (o) {
      if (o.at) { shiftXZ(o.at, dx, dz); shiftXZ(o.face, dx, dz); }
      if (o.along !== undefined) o.along = num(o.along) + (o.wall && o.wall[0] === "x" ? dz : dx);
      if (Array.isArray(o.watch)) shiftXZ(o.watch, dx, dz);
    });
    if (r.light && Array.isArray(r.light.lamps)) r.light.lamps.forEach(function (a) { shiftXZ(a, dx, dz); });
    if (L.start && L.start.room === name) { shiftXZ(L.start.at, dx, dz); shiftXZ(L.start.face, dx, dz); }
    // its doorways and openings slide with it along their walls (the validator says when one is then off the other room's wall)
    (L.doors || []).concat(L.openings || []).forEach(function (d) {
      if (d.rooms.indexOf(name) < 0 || d.at === undefined) return;
      var other = (L.rooms || []).find(function (q) { return q.name === (d.rooms[0] === name ? d.rooms[1] : d.rooms[0]); });
      if (!other) return;
      var ox0 = num(other.rect[0]), oz0 = num(other.rect[1]), ox1 = num(other.rect[2]), oz1 = num(other.rect[3]);
      var rx0 = num(r.rect[0]) - dx, rz0 = num(r.rect[1]) - dz, rx1 = num(r.rect[2]) - dx, rz1 = num(r.rect[3]) - dz;
      var xwall = (ox0 >= rx1 || rx0 >= ox1);              // the shared wall runs along z (before the move): `at` is a z
      d.at = num(d.at) + (xwall ? dz : dx);
    });
    return "move room " + name + " by (" + dx + ", " + dz + ")";
  }

  function moveCore(L, index, dx, dz) {
    var c = L.stairwells[index];
    c.rect = [num(c.rect[0]) + dx, num(c.rect[1]) + dz, num(c.rect[2]) + dx, num(c.rect[3]) + dz];
    return "move stair core " + c.name;
  }

  // set a room's rect (resize); its contents stay where they are
  function setRect(L, name, rect) {
    var r = room(L, name);
    var x0 = Math.min(rect[0], rect[2]), x1 = Math.max(rect[0], rect[2]), z0 = Math.min(rect[1], rect[3]), z1 = Math.max(rect[1], rect[3]);
    r.rect = [x0, z0, x1, z1];
    return "resize room " + name;
  }

  // rename a room and every reference to it: doors, openings, the start, the heist areas
  function renameRoom(L, from, to) {
    if (!to || from === to) return null;
    if ((L.rooms || []).some(function (r) { return r.name === to; })) throw new Error("a room is already named " + to);
    room(L, from).name = to;
    (L.doors || []).concat(L.openings || []).forEach(function (d) { d.rooms = d.rooms.map(function (n) { return n === from ? to : n; }); });
    if (L.start && L.start.room === from) L.start.room = to;
    var h = L.script && L.script.heist;
    if (h && Array.isArray(h.areas)) h.areas.forEach(function (a) { if (Array.isArray(a.rooms)) a.rooms = a.rooms.map(function (n) { return n === from ? to : n; }); });
    return "rename room " + from + " to " + to;
  }

  function addRoom(L, name, rect, like) {
    if ((L.rooms || []).some(function (r) { return r.name === name; })) throw new Error("a room is already named " + name);
    var r = { name: name, rect: rect.slice() };
    var src = like && (L.rooms || []).find(function (q) { return q.name === like; });
    if (src) ["floor", "height", "area", "zone", "floor_mat", "wall_mat", "ceiling_mat"].forEach(function (k) { if (k in src) r[k] = LL.clone(src[k]); });
    L.rooms.push(r);
    return "add room " + name;
  }

  function removeRoom(L, name) {
    var k = L.rooms.findIndex(function (r) { return r.name === name; });
    if (k < 0) throw new Error("no room " + name);
    L.rooms.splice(k, 1);
    L.doors = (L.doors || []).filter(function (d) { return d.rooms.indexOf(name) < 0; });
    if (L.openings) L.openings = L.openings.filter(function (d) { return d.rooms.indexOf(name) < 0; });
    return "remove room " + name + " and its doorways";
  }

  // a doorway between two rooms at `at` along their shared wall (key order as the generator writes doors: rooms, at, width)
  function addDoor(L, a, b, at, width) {
    var d = { rooms: [a, b], at: at, width: width || num((L.defaults || {}).door_width) || 180 };
    L.doors = L.doors || [];
    // a second doorway between the same rooms gets its own name (LEVEL.doors names them; the default is "a-b")
    var names = L.doors.map(function (x) { return x.name || x.rooms[0] + "-" + x.rooms[1]; });
    if (names.indexOf(a + "-" + b) >= 0) { var k = 2; while (names.indexOf(a + "-" + b + "_" + k) >= 0) k++; d.name = a + "-" + b + "_" + k; }
    L.doors.push(d);
    return "add doorway " + a + "-" + b;
  }

  function removeDoor(L, index) {
    var d = L.doors.splice(index, 1)[0];
    return "remove doorway " + (d.name || d.rooms.join("-"));
  }

  function removeOpening(L, index) {
    var d = L.openings.splice(index, 1)[0];
    return "remove opening " + (d.name || d.rooms.join("~"));
  }

  // a door's rule: kind "open" (no door), "slide" or "shutter"; keyflags a number (0 = unlocked); other fields as given
  function setDoorRule(L, index, rule) {
    var d = L.doors[index];
    if (rule.kind === "open") { delete d.object; return "doorway " + (d.name || d.rooms.join("-")) + ": no door"; }
    var o = d.object || (d.object = { name: (d.name || d.rooms.join("_")).replace(/[^A-Za-z0-9_]/g, "_") + "_door", type: rule.kind });
    o.type = rule.kind;
    if (rule.keyflags !== undefined) {
      if (rule.keyflags) o.keyflags = "0x" + rule.keyflags.toString(16);
      else delete o.keyflags;
    }
    ["double", "see_through", "padlock", "open"].forEach(function (k) { if (rule[k] !== undefined) { if (rule[k]) o[k] = true; else delete o[k]; } });
    if (rule.guard_access !== undefined) { if (rule.guard_access) o.guard_access = rule.guard_access; else delete o.guard_access; }
    return "door " + (o.name || "") + ": " + o.type + " " + LL.keyLabel(LL.ival(o.keyflags, 0));
  }

  function setDoorField(L, index, key, value, list) {
    var d = (list === "openings" ? L.openings : L.doors)[index];
    if (value === undefined || value === null || value === "") delete d[key]; else d[key] = value;
    return (list === "openings" ? "opening " : "doorway ") + (d.name || d.rooms.join("-")) + ": " + key;
  }

  function findPad(L, name) {
    for (var i = 0; i < (L.rooms || []).length; i++) {
      var r = L.rooms[i], ps = r.pads || [];
      for (var k = 0; k < ps.length; k++) if (ps[k].name === name) return { room: r, pad: ps[k], index: k };
    }
    return null;
  }

  function addPad(L, roomName, name, at) {
    if (findPad(L, name)) throw new Error("a pad is already named " + name);
    var r = room(L, roomName);
    r.pads = r.pads || [];
    r.pads.push({ name: name, at: [at[0], at[1]], face: [at[0], at[1] + 100] });
    return "add pad " + name;
  }

  function removePad(L, name) {
    var f = findPad(L, name);
    if (!f) throw new Error("no pad " + name);
    f.room.pads.splice(f.index, 1);
    return "remove pad " + name;
  }

  // move a pad (its facing point keeps its offset); it may change room (into whichever room `toRoom` names)
  function movePad(L, name, at, toRoom) {
    var f = findPad(L, name);
    if (!f) throw new Error("no pad " + name);
    var p = f.pad, dx = at[0] - num(p.at[0]), dz = at[1] - num(p.at[1]);
    shiftXZ(p.at, dx, dz); shiftXZ(p.face, dx, dz);
    if (toRoom && toRoom !== f.room.name) {
      var r = room(L, toRoom);
      f.room.pads.splice(f.index, 1);
      r.pads = r.pads || [];
      r.pads.push(p);
    }
    return "move pad " + name;
  }

  function renamePad(L, from, to) {
    if (!to || from === to) return null;
    if (findPad(L, to)) throw new Error("a pad is already named " + to);
    var f = findPad(L, from);
    if (!f) throw new Error("no pad " + from);
    f.pad.name = to;
    // references: camera watch, the heist tables (posts, panels, patrols, stations, civilians)
    (L.rooms || []).forEach(function (r) { (r.objects || []).forEach(function (o) { if (o.watch === from) o.watch = to; }); });
    var h = L.script && L.script.heist;
    function sub(a) { return Array.isArray(a) ? a.map(function (n) { return n === from ? to : n; }) : a; }
    if (h) {
      (h.areas || []).forEach(function (a) { a.posts = sub(a.posts); if (a.panel === from) a.panel = to; });
      (h.civ_extra || []).forEach(function (c) { c.pads = sub(c.pads); });
      if (h.patrols) Object.keys(h.patrols).forEach(function (k) { h.patrols[k] = sub(h.patrols[k]); });
      if (h.stations) h.stations = sub(h.stations);
    }
    return "rename pad " + from + " to " + to;
  }

  // a wall object (camera, alarm, monitor) onto wall `wall` of its room at `along`
  function moveWallObject(L, roomName, index, wall, along) {
    var o = room(L, roomName).objects[index];
    o.wall = wall; o.along = along;
    return "move " + o.type + " " + (o.name || index);
  }

  function moveFloorObject(L, roomName, index, at) {
    var o = room(L, roomName).objects[index], dx = at[0] - num(o.at[0]), dz = at[1] - num(o.at[1]);
    shiftXZ(o.at, dx, dz); shiftXZ(o.face, dx, dz);
    return "move " + o.type + " " + (o.name || index);
  }

  function setObjectField(L, roomName, index, key, value) {
    var o = room(L, roomName).objects[index];
    if (value === undefined || value === null || value === "") delete o[key]; else o[key] = value;
    return o.type + " " + (o.name || index) + ": " + key;
  }

  function removeObject(L, roomName, index) {
    var r = room(L, roomName), o = r.objects.splice(index, 1)[0];
    return "remove " + o.type + " " + (o.name || index);
  }

  function setRoomField(L, name, key, value) {
    var r = room(L, name);
    if (value === undefined || value === null || value === "") delete r[key]; else r[key] = value;
    return "room " + name + ": " + key;
  }

  // run a list of ops [{op, args...}] (cli.js edit, the tests)
  var OPS = {
    moveRoom: function (L, o) { return moveRoom(L, o.room, o.dx, o.dz); },
    setRect: function (L, o) { return setRect(L, o.room, o.rect); },
    renameRoom: function (L, o) { return renameRoom(L, o.from, o.to); },
    addRoom: function (L, o) { return addRoom(L, o.name, o.rect, o.like); },
    removeRoom: function (L, o) { return removeRoom(L, o.room); },
    addDoor: function (L, o) { return addDoor(L, o.a, o.b, o.at, o.width); },
    removeDoor: function (L, o) { return removeDoor(L, o.index); },
    setDoorRule: function (L, o) { return setDoorRule(L, o.index, o.rule); },
    addPad: function (L, o) { return addPad(L, o.room, o.name, o.at); },
    removePad: function (L, o) { return removePad(L, o.name); },
    movePad: function (L, o) { return movePad(L, o.name, o.at, o.toRoom); },
    renamePad: function (L, o) { return renamePad(L, o.from, o.to); },
    moveWallObject: function (L, o) { return moveWallObject(L, o.room, o.index, o.wall, o.along); },
    moveFloorObject: function (L, o) { return moveFloorObject(L, o.room, o.index, o.at); }
  };
  function apply(L, ops) { return ops.map(function (o) { if (!OPS[o.op]) throw new Error("unknown op " + o.op); return OPS[o.op](L, o); }); }

  return {
    moveRoom: moveRoom, moveCore: moveCore, setRect: setRect, renameRoom: renameRoom, addRoom: addRoom, removeRoom: removeRoom,
    addDoor: addDoor, removeDoor: removeDoor, removeOpening: removeOpening, setDoorRule: setDoorRule, setDoorField: setDoorField,
    findPad: findPad, addPad: addPad, removePad: removePad, movePad: movePad, renamePad: renamePad,
    moveWallObject: moveWallObject, moveFloorObject: moveFloorObject, setObjectField: setObjectField, removeObject: removeObject,
    setRoomField: setRoomField, apply: apply
  };
});
