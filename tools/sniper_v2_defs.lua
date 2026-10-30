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
  prone = "Prone", rising = "Getting up", tagged = "Tagged  -  %d m",
  storm_in = "Snow coming in", storm_out = "The snow is clearing", thermal_on = "Thermal", thermal_wait = "Thermal recharging  -  %d s",
  flush = "They are sending men up the hill", panel_flush = "MEN ON THE HILL %d",
  panel_th_on = "THERMAL ON", panel_th_wait = "THERMAL %d s", panel_th_ready = "THERMAL READY  (Q)",
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
-- dwell = seconds on a perch before he wants to move (min, max); flush = grunts sent up the hill after the round is won (0: none)
-- (when he fires is the detection meter's: S.detect_*)
D.ROUNDS = {
  { foe = 1, snipers = 1, hit = 0.35, glint = 1.6, dwell = { 18, 26 }, flush = 3 },
  { foe = 2, snipers = 1, hit = 0.45, glint = 1.4, dwell = { 16, 24 }, flush = 0 },
  { foe = 3, snipers = 2, hit = 0.45, glint = 1.4, dwell = { 16, 24 }, flush = 4 },
  { foe = 4, snipers = 1, hit = 0.55, glint = 1.2, dwell = { 14, 20 }, flush = 5 },
  { foe = 5, snipers = 3, hit = 0.50, glint = 1.2, dwell = { 14, 22 }, flush = 0 },
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
  -- DETECTION: each marksman fills a meter on Bond (0..1) while he has a line to him; he glints and fires only when it is full, then
  -- it drops to detect_after. Per second: detect_rate x stance x movement x scope x distance x close x weather x thermal; it decays
  -- at detect_decay a second while Bond is hidden from him.
  detect_rate = 0.25,                       -- the meter a second at the reference: standing, walking pace, unscoped, at detect_ref_dist
  detect_stance = { 1.0, 0.7, 0.5, 0.2 },   -- stand, crouch, squat, prone
  detect_still = 0.5, detect_run = 2.0, run_speed = 450,   -- standing still x0.5 .. at run_speed units a second and faster x2
  detect_scope = 2.0, scope_glint_cone = 20,   -- Bond's raised rifle scope pointed within this many degrees of him: x2 (his lens glints)
  detect_ref_dist = 30000, detect_dist_k = { 0.5, 2.0 },   -- x (ref / distance), clamped
  close_detect = 6,                         -- inside close_range: x6 (he sees you)
  detect_decay = 0.12, detect_start = 0.0, detect_after = 0.45,
  min_gap = { 2, 4 },                       -- seconds at least between two of his shots
  -- PRONE (the use key): the eye 135 units under standing (the squat is 100), crawl speed, steady aim; getting up takes prone_rise s
  prone_eye = -135, prone_speed = 0.22, prone_sway = 0.25, prone_rise = 1.2,
  -- TAGGING: the binoculars held within tag_cone degrees of a marksman for tag_secs pin a tag on him until he moves
  tag_cone = 1.2, tag_secs = 1.0, tag_px = 3,
  -- THE SNOWSTORM: calm storm_calm seconds, rolls in over storm_ramp, holds storm_hold, rolls out. The fog goes to storm_fog (metres,
  -- as fog); the marksmen spot x storm_detect at full storm, but past half they use thermal (x foe_thermal). storm = false: none.
  storm = true, storm_calm = { 70, 110 }, storm_ramp = 10, storm_hold = { 25, 40 }, storm_fog = { 30, 170 },
  storm_detect = 0.3, foe_thermal = 2.0,
  -- THERMAL (Q): seconds on, seconds to recharge after, how dark the world goes
  thermal_secs = 8, thermal_cooldown = 25, thermal_dim = 0.65,
  -- THE FLUSH-OUT (the round's flush): grunts start this far from Bond (units), at least flush_drop below him, out of his sight
  flush_dist = { 6000, 13000 }, flush_drop = 150, flush_timeout = 90, flush_accuracy = 30,
  flush_guns = { "ak47", "kf7", "d5k" }, flush_skins = { "russian_soldier", "russian_infantry", "siberian_guard2", "arctic_commando" },
  breather = 6,             -- seconds between a won round and the next
  fire_timeout = 4.5,       -- a released shot that has not come in this long is given up
  -- the glint: a small white flare at his scope (alpha pulses), the marker after a shot (a quiet diamond that fades out)
  glint_px = 2, marker_secs = 6, marker_px = 3,
  glint_sfx = nil,          -- a stock sound for the glint ({ id, volume, pitch }); nil = silent
  -- the default haze, in metres from the eye: no fog nearer than [1], full fog at [2] (0 = as far as the band reaches). The fog
  -- thickens in 1 / distance: at 220 / 860 it is about 50% at 350 m, 75% at 500 m, 85% at the perimeter (600 m), Surface's soft
  -- edges scaled to the field. The storms (phase 3) move it.
  fog = { 220, 860 },
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

-- THE STAGES: the generated level in the levelgen slot picks one (levelgen.current(), i.e. GE_LEVELGEN; SNIPER2_SETTINGS.stage forces
-- one). Per stage: layout (the levelgen layout the mode includes: LEVEL.pads), perches / bond (named pads), place (the words), key (the
-- best-score key), and any S key (perch_band, fog, storm, storm_fog, flush_dist, ...) and T key (storm_in, flush, ...) to override.
-- Every stage gets the float matrices (the zoom fix) and the lifted 320 m prop cut from the C side (bgfog.c: a levelgen sniper stage).
D.STAGES = {
  snowfield = { layout = "levelgen_layout_snowfield", perches = D.PERCHES, bond = D.BOND, key = D.key, place = "SEVERNAYA SNOWFIELD",
                S = {}, T = {} },
  -- the SNIPER JUNGLE (tools/levelgen/build_jungle.py): a 408 x 408 m valley (play area +-20400 units), Bond's ridge in the south (b1 at
  -- z -17600). Its perches run 249 to 353 m from b1: the hunters' ridge f* (327..353), the flanks w* / e* (261..340), the knolls k*
  -- (249..348). The stan line (the AI's sight: combat.los) is stopped by the trees and the thickets under it, so the list is the
  -- perches whose line to b1 the generator keeps clear (its LANE_PERCHES) plus f3, k5, k7 (checked in game: tools/sniper_defs.lua).
  jungle = { layout = "levelgen_layout_jungle", bond = { "b1" }, key = "jungle2", place = "SNIPER JUNGLE",
             perches = { "f2", "f3", "f4", "f5", "f7", "f9", "w1", "w2", "e1", "e3", "k1", "k2", "k3", "k5", "k7" },
             S = {
               perch_band = { 240, 360 },   -- every jungle perch (the far ones first: the arrival takes the farthest unseen)
               fog = { 200, 900 },          -- Jungle's dark olive fog: 40% at 300 m, 75% at 500 m (a heavier one hides a man at 100 m)
               -- no snowstorm in the jungle. storm = true gives a mist bank instead (the same fog roll-in, the words below); its
               -- band must stay light, Jungle's fog colour is near black
               storm = false, storm_fog = { 60, 260 },
               flush_dist = { 6000, 13000 },   -- the grunts start 60 to 130 m off (the valley floor below Bond's ridge)
             },
             T = { storm_in = "Mist coming in", storm_out = "The mist is lifting", flush = "They are sending men up the ridge",
                   panel_flush = "MEN ON THE RIDGE %d" } },
}
