#!/usr/bin/env node
/*
 * cli.js - the level editor's model from the command line (node 12+; no packages). Used by tools/levelgen/test_editor_roundtrip.py.
 *
 *   node tools/levelgen/editor/cli.js roundtrip <in.json> <out.json>      read and write back, as the editor's Export does
 *   node tools/levelgen/editor/cli.js validate <in.json> [--footprints f]  the editor's checks, as JSON on stdout
 *   node tools/levelgen/editor/cli.js edit <in.json> <ops.json> <out.json> apply edit ops (edits.js OPS) and write the result
 *
 * --footprints: {model id: [half width, half depth]} of the stock prop models (the round-trip test writes one from your extracted
 * assets with gen_level.prop_half_extents), so the doorway clearance check sees the props as gen_level does.
 */
"use strict";
var fs = require("fs");
var path = require("path");
var LL = require(path.join(__dirname, "layout.js"));
var ED = require(path.join(__dirname, "edits.js"));

function read(p) { return LL.parse(fs.readFileSync(p, "utf8")); }
function write(p, L) { fs.writeFileSync(p, LL.stringify(L)); }

var a = process.argv.slice(2), cmd = a[0];
if (cmd === "roundtrip") {
  write(a[2], read(a[1]));
} else if (cmd === "validate") {
  var k = a.indexOf("--footprints"), opts = {};
  if (k >= 0) opts.footprints = JSON.parse(fs.readFileSync(a[k + 1], "utf8"));
  var r = LL.validate(read(a[1]), opts), M = r.model;
  process.stdout.write(JSON.stringify({
    issues: r.issues,
    counts: M ? { rooms: M.rooms.length, doors: M.doors.length, openings: M.openings.length, portals: M.nportals, cameras: M.ncam } : null
  }, null, 1) + "\n");
} else if (cmd === "edit") {
  var L = read(a[1]);
  var done = ED.apply(L, JSON.parse(fs.readFileSync(a[2], "utf8")));
  write(a[3], L);
  process.stdout.write(JSON.stringify(done) + "\n");
} else {
  process.stderr.write("usage: cli.js roundtrip <in> <out> | validate <in> [--footprints f] | edit <in> <ops> <out>\n");
  process.exit(2);
}
