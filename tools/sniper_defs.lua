-- sniper_defs.lua: the SNIPER DUEL's words and tables (shipped as Data/Scripts/modes/_sniper_defs.lua). Read by the run (tools/mode_sniper.lua),
-- its lobby (tools/mode_sniper_menu.lua, hosted by the main menu hub) and the hub's card. Defines the global SNIPER_DEFS:
--   STAGE     the stock stage the duel is fought on (Surface: Severnaya, snow, long sightlines) and its launch name
--   T         every player-facing word (MI6 tone, as tools/arena_theme.lua)
--   S         the numbers (SNIPER_SETTINGS = { key = value } overrides them; the lobby's launch hands a table the same way)
--   FOES      the named snipers: name, title, skin; ROUNDS the ladder (who, how many snipers, rushers, how hard)
--   BOND      Bond's perch: pads tried in order (the first the sight sweep likes), what he carries
--   PERCH     the rules and hand picks for the enemy perches: a band of distance from Bond's perch, a floor height, pads banned or favoured
--   key / best_key   the best-score keys (ui.prefs "sniper_bests": their own file, the Arena's keys are untouched)
-- No ROM data lives here: pad ids are indexes into the stage's pad table (positions come from the stage when it loads), and the picks are
-- checked at run time by a line-of-sight sweep (tools/mode_sniper.lua) so a pad that does not see Bond's perch is never used.

SNIPER_DEFS = {}
local D = SNIPER_DEFS

D.STAGE = { id = "surface", name = "SEVERNAYA" }

D.T = {
  kicker = "MI6  -  SECTION 00  -  SNIPER DUEL", title = "SNIPER DUEL", place = "SEVERNAYA SURFACE",
  blurb = "One rifle, a ridge, and a hunter on the far slopes. Read the glint, or die in the snow.",
  more = "Five rounds against named marksmen. A glint shows where a sniper has set up: take the shot first, or get down. He fires once, then moves. Later rounds bring a second rifle and men sent to flush you out. Prototype.",
  begin = "BEGIN THE DUEL", begin_sub = "FIVE ROUNDS", begin_desc = "Fly to Severnaya and take the ridge. The first hunter is waiting.",
  to_mi6 = "MAIN MENU", to_mi6_desc = "Back to the main menu.",
  footer = "W S / STICK: MOVE    FIRE / A / ENTER: SELECT    ESC / B: BACK    MOUSE: POINT, CLICK",
  d_where = "WHERE", d_rounds = "ROUNDS", d_rifle = "RIFLE", d_best = "BEST ON FILE", d_none = "NO DUELS YET", d_time = "FASTEST CLEAR", d_runs = "DUELS FOUGHT",
  rifle = "SNIPER RIFLE + PP7", best_hub = "BEST %d / %d ROUNDS", hub_none = "NO DUELS YET",
  dossier = "DOSSIER",
  -- the run
  get_ready = "TAKE THE RIDGE", get_ready_sub = "SEVERNAYA - THE HUNTERS ARE COMING", round = "ROUND %d / %d", round_won = "ROUND %d WON", you_win = "THE RIDGE IS YOURS",
  glint = "GLINT  %s  %d m", glint_sub = "HE IS SETTING UP", glint_big = "GLINT  -  %s", glint_m = "%d M  -  GET DOWN OR SHOOT FIRST", spotter = "SPOTTER", rushers = "THEY ARE COMING FOR YOU", gone = "HE HAS MOVED", killed_by = "SHOT BY %s",
  hud_plate = "ROUND %d / %d", hud_round = "ROUND %d/%d  %s", hud_foe = "%s", hud_left = "SNIPERS %d   RUSHERS %d", hud_time = "TIME %s   SHOTS %d  HITS %d",
  clock = { "12", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11" },
  clock_fmt = "%s O'CLOCK", clock_fmt_big = "%s OCLOCK",
  -- the debrief
  win = "TARGETS ELIMINATED", fail = "AGENT DOWN", abort = "DUEL ABORTED", win_line = "The ridge is quiet. M will want the rifle back.",
  fail_line = "The snow keeps what it takes, 007.", abort_line = "You left the ridge.",
  kicker_d = "DEBRIEF  -  SNIPER DUEL", again = "AGAIN, 007?", again_desc = "Take the ridge again.",
  s_rounds = "ROUNDS WON", s_time = "TIME", s_acc = "ACCURACY", s_hs = "HEADSHOTS", s_shots = "SHOTS FIRED", s_hits = "HITS TAKEN", s_foe = "FELLED BY",
  new_best = "NEW BEST   ", chart = "TIME PER ROUND", legend = "ROUND BY ROUND",
}

-- the named marksmen. skin: a name from spawn.skins() (plain troops; the canon characters stay out of it)
D.FOES = {
  { name = "ANTON VESNIN",             title = "THE PICKET",                   skin = "siberian_guard1" },
  { name = "KATYA ZORINA",             title = "THE SNOW LEOPARD",             skin = "arctic_commando" },
  { name = "GRIGORI BELKOV",           title = "THE WARDEN OF THE RIDGE",      skin = "janus_special_forces" },
  { name = "THE SOKOLOV BROTHERS",     title = "TWO RIFLES, ONE FAMILY",       skin = "jungle_commando" },
  { name = "COMMANDANT VERA MOROZOVA", title = "THE ICE QUEEN OF SEVERNAYA",   skin = "janus_marine" },
}
D.RUSHER_SKINS = { "russian_soldier", "russian_infantry", "siberian_guard2", "arctic_commando" }
D.RUSHER_GUNS = { "ak47", "kf7", "d5k" }

-- the ladder. foe = FOES index (the second sniper of a round takes the next foe's skin); hit = the chance that the marksman's shot hits (a coin tossed per perch: the stock accuracy build-up is far too slow at 60 to 150 m for one shot a perch); glint = seconds
-- the glint shows before he fires; dwell = seconds a marksman stays on a perch (min, max), volley_gap = seconds between his glints, volley_shots = shots a glint looses (min, max); rushers = how many come, rush_at = seconds into the
-- round they arrive
D.ROUNDS = {
  { foe = 1, snipers = 1, rushers = 0, hit = 0.35, glint = 1.6, dwell = { 14, 20 }, volley_gap = { 6, 9 }, volley_shots = { 1, 1 } },
  { foe = 2, snipers = 1, rushers = 0, hit = 0.45, glint = 1.3, dwell = { 12, 18 }, volley_gap = { 5, 8 }, volley_shots = { 1, 2 } },
  { foe = 3, snipers = 1, rushers = 2, hit = 0.50, glint = 1.2, dwell = { 12, 16 }, volley_gap = { 5, 8 }, volley_shots = { 1, 2 }, rush_at = 10 },
  { foe = 4, snipers = 2, rushers = 0, hit = 0.55, glint = 1.1, dwell = { 10, 15 }, volley_gap = { 4, 7 }, volley_shots = { 1, 2 }, second_at = 4 },
  { foe = 5, snipers = 2, rushers = 3, hit = 0.65, glint = 1.0, dwell = { 10, 14 }, volley_gap = { 4, 6 }, volley_shots = { 1, 2 }, second_at = 3, rush_at = 12 },
}

D.S = {
  breather = 5,          -- seconds between a won round and the next callout
  first_delay = 3,       -- seconds from the callout to the first glint
  fire_timeout = 4.5,    -- a stand-in that has not fired this long after the glint is moved (no line, or he lost his aim)
  glint_sfx = { 160, 0.9, 1.7 },   -- stock sound id, volume, pitch (BING, high)
  band_lo = 6000, band_hi = 15000, -- a perch is 60 to 150 m from Bond (world units are cm)
  move_min = 1000, move_max = 3500,   -- the next perch is this far (units) from the one he leaves: a run of a few seconds
  run_timeout = 25,      -- a runner that has not arrived by then is put on the new perch
  hide_cone = 38,        -- rushers arrive at least this many degrees off the centre of Bond's view
  rush_min = 2800, rush_max = 5500,   -- rushers arrive this far from Bond, out of his cone
  sweep_batch = 12,      -- pads probed per game tick by the sight sweep
  sight = 25000,         -- chr->visionrange for every enemy
  foe_scale = 1.5,       -- the marksmen are drawn this much bigger (spawn.scale): a man at 40 to 80 m is a speck otherwise
  foe_hp = 1.0,          -- marksman health multiplier (1 = a guard; the sniper rifle kills a guard in one body shot)
  hit_damage = 0.42,     -- what one marksman hit takes off Bond, as a fraction of a full health bar (the rifle's damage is set to give this on any difficulty)
  heal_each_round = true,
  bond_ammo = { sniper = 60, pp7 = 80 },
  best_time_key = "time",
}

-- Bond's perch: pads tried in order, the first with the most pads in the band that see it (decided at run time). High ground on the
-- ridge above the start: its sweep (tests/sniper/matrix.sh) saw 60 to 75 pads in the 60 to 150 m band from these.
D.BOND = { pads = { 53, 54 }, yaw_toward = "centroid", pitch = -3 }

-- the enemy perches: any waypoint pad inside the band with a clear line to Bond's perch, at least `y_min` high (the lowest pads are the
-- river bed), minus the banned, with the favoured counted twice
D.PERCH = { y_min = 60, ban = {}, prefer = {},
  -- the survey (tests/sniper/geom.sh, Bond on pad 53): waypoint pads 38 to 79 m off (a man is drawn and seen no farther than about 65 m: the stage's fog) that the stan line AND the drawn geometry (probe.los, both ways,
  -- eye to head and to rifle) say see Bond. A pad the stan line alone sees is a place a marksman could hit a Bond who cannot see him: left out.
  pads = { 50, 84, 101, 108, 116, 117, 118, 120, 146, 151, 152, 153, 155, 157, 158, 159, 160, 161, 168, 169, 170, 171, 172, 173, 177 } }

-- the stages the duel can be fought on. stage = the name the launch loads (ui.launch stage=), env = the launch's variables, layout = the levelgen layout the
-- mode includes (LEVEL: its named pads), bond_* / perch_* = Bond's candidate perches and the marksmen's, as pad ids (a stock stage) or LEVEL.pads names
D.STAGES = {
  surface = { key = "surface", name = "SEVERNAYA", card = "SURFACE", stage = "surface", bond_pads = D.BOND.pads, perch_pads = D.PERCH.pads, foe_scale = 1.5 },
  ridge = { key = "ridge", name = "SEVERNAYA RIDGE", card = "SEVERNAYA RIDGE", stage = "levelgen", env = { GE_LEVELGEN = "ridge" }, layout = "levelgen_layout_ridge",
            bond_names = { "sp4", "sp3", "sp5", "sp2", "sp6" }, perch_names = { "np1", "np2", "np3", "np4", "np5", "np6", "np7", "vp1", "vp2", "vp3", "vp4", "vp5", "vp6" },
            foe_scale = 2.0, all_perches = true },
  -- the snowfield prototype (tools/levelgen/build_snowfield.py, Play Sniper Snowfield.bat; not on the card: its own build): a 340 m rolling
  -- field; geom_los = a marksman glints / shoots only when the drawn ground (probe.los) is clear too, since the stan line ignores the hills
  snowfield = { key = "snowfield", name = "SEVERNAYA SNOWFIELD", card = "SNOWFIELD", stage = "levelgen", env = { GE_LEVELGEN = "snowfield" },
            layout = "levelgen_layout_snowfield", bond_names = { "b1", "b2", "b3", "b4", "b5" },
            perch_names = { "m3", "m4", "m5", "m6", "m7", "m8", "f1", "f2", "f3", "f4", "f5", "f6", "f7", "f8", "m1", "m2" },
            foe_scale = 2.0, all_perches = true, geom_los = true },
  -- the jungle prototype (tools/levelgen/build_jungle.py, Play Sniper Jungle.bat; not on the card: its own build): a 410 m wide jungle valley.
  -- Every perch is 250 to 355 m from Bond (the hunters' ridge f*, the flank ridges w* / e*, the knolls k*). The stan line (the AI's sight)
  -- is stopped by the trees (props) and the thickets / boulders (stan holes) under it however high it runs, so the listed perches are the
  -- ones whose line to b1 the generator keeps clear of both; geom_los adds the drawn ground
  jungle = { key = "jungle", name = "SNIPER JUNGLE", card = "JUNGLE", stage = "levelgen", env = { GE_LEVELGEN = "jungle" },
            layout = "levelgen_layout_jungle", bond_names = { "b1", "b2", "b3", "b4", "b5" },
            perch_names = { "f2", "f3", "f4", "f5", "f7", "f9", "w1", "w2", "e1", "e3", "k1", "k2", "k3", "k5", "k7" },
            foe_scale = 2.0, all_perches = true, geom_los = true },
}
D.STAGE_ORDER = { "ridge", "surface" }
function D.key(id) return (D.STAGES[id or "surface"] or D.STAGES.surface).key end   -- the best-score key (ui prefs sniper_bests: <key>_rounds ...)
