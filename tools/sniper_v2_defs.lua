-- sniper_v2_defs.lua: the SNIPER DUEL v2's words and tables (shipped as Data/Scripts/modes/_sniper_v2_defs.lua; read by tools/mode_sniper_v2.lua).
-- Defines the global SNIPER2_DEFS. Every number AC may want to try is here; SNIPER2_SETTINGS = { key = value } set before
-- include("sniper-v2") overrides S (the tests use it).
--   T        the player-facing words (plain, small: no centre callouts)
--   FOES     the named marksmen; ROUNDS the ladder (who, how many rifles at once, how hard): edit freely
--   S        the numbers (the perch band, the scope's zoom, the close range, hits by difficulty, timings)
--   PERCHES  the marksmen's perches (named pads of the snowfield layout, LEVEL.pads), filtered by S.perch_band at run time
-- No ROM data lives here: pad names index the generated layout, item names the stock items.

SNIPER2_DEFS = {}
local D = SNIPER2_DEFS

D.T = {
  title = "SNIPER DUEL", place = "SEVERNAYA SNOWFIELD",
  round = "Round %d of %d  -  %s, %s", round_won = "Round %d won", you_win = "All targets down", down = "You are down",
  shot_from = "Shot from %s, %d m", hit = "Hit  -  %d left", moved = "He has moved", two = "%d rifles out there",
  panel_round = "ROUND %d / %d", panel_left = "TARGETS %d", panel_hits = "HITS LEFT %d", panel_time = "TIME %s",
  clock = { "12", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11" }, clock_fmt = "%s o'clock",
}

-- the named marksmen. skin: a name from spawn.skins() (plain troops)
D.FOES = {
  { name = "Anton Vesnin",             title = "the picket",                 skin = "siberian_guard1" },
  { name = "Katya Zorina",             title = "the snow leopard",           skin = "arctic_commando" },
  { name = "Grigori Belkov",           title = "warden of the ridge",        skin = "janus_special_forces" },
  { name = "the Sokolov brothers",     title = "two rifles, one family",     skin = "jungle_commando" },
  { name = "Commandant Vera Morozova", title = "ice queen of Severnaya",     skin = "janus_marine" },
}

-- the ladder: one row a round. foe = FOES index (more rifles take the next foes' skins); snipers = how many marksmen hunt at once;
-- hit = the chance a marksman's aimed shot hits (outside close range); glint = seconds the glint shows before the shot;
-- dwell = seconds on a perch before he wants to move (min, max); volley_gap = seconds between his glints (min, max)
D.ROUNDS = {
  { foe = 1, snipers = 1, hit = 0.35, glint = 1.6, dwell = { 18, 26 }, volley_gap = { 7, 11 } },
  { foe = 2, snipers = 1, hit = 0.45, glint = 1.4, dwell = { 16, 24 }, volley_gap = { 6, 10 } },
  { foe = 3, snipers = 2, hit = 0.45, glint = 1.4, dwell = { 16, 24 }, volley_gap = { 7, 11 } },
  { foe = 4, snipers = 1, hit = 0.55, glint = 1.2, dwell = { 14, 20 }, volley_gap = { 5, 8 } },
  { foe = 5, snipers = 3, hit = 0.50, glint = 1.2, dwell = { 14, 22 }, volley_gap = { 6, 10 } },
}

D.S = {
  -- where they set up: perches this far from Bond's start (metres). The snowfield's perches run 140 to 510 m; the far ones are the
  -- default (AC: "spawn as far away as possible"). 150 to 300 makes a marksman twice the size in the scope.
  perch_band = { 250, 520 },
  -- the scope's strongest zoom (degrees of view; stock 7) and the binoculars' (stronger: smaller is stronger)
  scope_min_fov = 7, binoc_min_fov = 3.5,
  -- close range: inside this (world units, 100 a metre) a marksman's shot hits near-perfectly and he does not wait long
  close_range = 6000, close_hit = 0.95, close_gap = { 1.5, 2.5 },
  -- how many marksman hits Bond survives, by difficulty (Agent, Secret Agent, 00 Agent, 007): the last one kills. No healing, no
  -- armour, no pickups.
  hits = { 4, 3, 2, 1 },
  -- arrival and relocation: never in Bond's view. A perch is "in view" when it is inside his view cone (his field of view now, the
  -- margin added) and the drawn ground does not hide it; view_aspect is the widest screen shape allowed for.
  view_margin = 6, view_aspect = 2.0,
  first_delay = { 3, 6 }, second_after = { 6, 12 },   -- seconds before the first / each next marksman of a round sets up
  hide = { 4, 8 },          -- seconds a marksman is gone between perches
  breather = 6,             -- seconds between a won round and the next
  fire_timeout = 4.5,       -- a released shot that has not come in this long is given up
  -- the glint: a small white flare at his scope (alpha pulses), the marker after a shot (a quiet diamond that fades out)
  glint_px = 2, marker_secs = 6, marker_px = 3,
  glint_sfx = nil,          -- a stock sound for the glint ({ id, volume, pitch }); nil = silent
  -- the default haze, in metres from the eye: no fog nearer than [1], full fog at [2] (0 = as far as the band reaches: it never goes
  -- full on the map). The fog thickens with distance (in 1 / distance): at 180 / 0 a marksman at 500 m is about 45% fogged, the
  -- perimeter about half. The storms (phase 3) move it.
  fog = { 180, 0 },
  foe_scale = 2.0,          -- the marksmen are drawn this much bigger (spawn.scale caps at 2)
  sight = 60000,            -- chr->visionrange for every marksman (units): the far perches are 500 m off
  foe_hp = 1.0,
  bond_ammo = 60,
  debug_every = 2,          -- seconds between the debug lines (Bond's stance, view, each marksman's state)
}

-- the marksmen's perches: the snowfield's named pads (tools/levelgen/build_snowfield.py prints their distances and lines to b1)
D.PERCHES = { "m1", "m2", "m3", "m4", "m5", "m6", "m7", "m8", "m9", "m10", "f1", "f2", "f3", "f4", "f5", "f6", "f7", "f8", "f9", "f10",
              "w1", "w2", "e1", "e2", "r1", "r2", "r3", "r5", "r6" }
D.BOND = { "b1" }
D.key = "snowfield2"   -- the best-score key (ui prefs sniper_bests)
