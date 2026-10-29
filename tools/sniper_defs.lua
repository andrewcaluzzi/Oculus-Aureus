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
  clock_fmt = "%s O'CLOCK",
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
-- the glint shows before he fires; hide = seconds he stays gone after a shot (min, max); rushers = how many come, rush_at = seconds into the
-- round they arrive
D.ROUNDS = {
  { foe = 1, snipers = 1, rushers = 0, hit = 0.35, glint = 1.6, hide = { 5, 7 } },
  { foe = 2, snipers = 1, rushers = 0, hit = 0.45, glint = 1.3, hide = { 4, 6 } },
  { foe = 3, snipers = 1, rushers = 2, hit = 0.50, glint = 1.2, hide = { 4, 6 }, rush_at = 10 },
  { foe = 4, snipers = 2, rushers = 0, hit = 0.55, glint = 1.1, hide = { 4, 6 }, second_at = 4 },
  { foe = 5, snipers = 2, rushers = 3, hit = 0.65, glint = 1.0, hide = { 3.5, 5 }, second_at = 3, rush_at = 12 },
}

D.S = {
  breather = 5,          -- seconds between a won round and the next callout
  first_delay = 3,       -- seconds from the callout to the first glint
  post_shot = 1.0,       -- seconds he stays after his shot (the kill window for a quick Bond) before he moves
  fire_timeout = 4.5,    -- a stand-in that has not fired this long after the glint is moved (no line, or he lost his aim)
  glint_sfx = { 160, 0.9, 1.7 },   -- stock sound id, volume, pitch (BING, high)
  band_lo = 6000, band_hi = 15000, -- a perch is 60 to 150 m from Bond (world units are cm)
  hide_cone = 38,        -- a new perch is at least this many degrees off the centre of Bond's view when it is picked ...
  move_ang = 25,         -- ... and at least this many degrees round from the perch he left
  rush_min = 2800, rush_max = 5500,   -- rushers arrive this far from Bond, out of his cone
  sweep_batch = 12,      -- pads probed per game tick by the sight sweep
  sight = 25000,         -- chr->visionrange for every enemy
  foe_hp = 1.0,          -- marksman health multiplier (1 = a guard; the sniper rifle kills a guard in one body shot)
  hit_damage = 0.42,     -- what one marksman hit takes off Bond, as a fraction of a full health bar (the rifle's damage is set to give this on any difficulty)
  heal_each_round = true,
  bond_ammo = { sniper = 60, pp7 = 80 },
  best_time_key = "time",
}

-- Bond's perch: pads tried in order, the first with the most pads in the band that see it (decided at run time). High ground on the
-- ridge above the start: its sweep (tests/sniper/matrix.sh) saw 60 to 75 pads in the 60 to 150 m band from these.
D.BOND = { pads = { 140, 158, 129, 124, 54, 53, 139 }, yaw_toward = "centroid", pitch = -3 }

-- the enemy perches: any waypoint pad inside the band with a clear line to Bond's perch, at least `y_min` high (the lowest pads are the
-- river bed), minus the banned, with the favoured counted twice
D.PERCH = { y_min = 60, ban = {}, prefer = {} }

function D.key() return "surface" end   -- the best-score key (one stage for now); the file is Data/Saves/ui/sniper_bests.ini
