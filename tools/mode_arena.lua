-- Arena waves (GE_MODE=arena, on the Colosseum: GE_STAGE=colosseum). Shipped to Data/Scripts/modes/arena.lua by the build.
-- Waves of enemies pour out of the four gates; bosses every few waves; pickups after boss waves and now and then.
--
-- Settings: set ARENA_SETTINGS before include("arena") (see arena-endless.lua), or edit S below. EVERY balance number lives in
-- this top block (S, TIERS, TIER_MIX, ELITES and the turbo_* / elite_* / boss_* values): tune here.
--   mode          "fixed" = `waves` waves then the mission is won;  "endless" = keep going until Bond falls
--   waves         last wave in fixed mode (default 20)
--   boss_every    a boss wave every N waves (default 5)
--   breather      seconds between waves (default 12)
--   start_delay   seconds before wave 1
--   max_alive     most enemies alive at once at wave 1; it grows by 1 every max_alive_every waves up to max_alive_cap
--   hp_growth     enemy health (and armour) grows by this fraction per wave (0.10 = +10% a wave)
--   acc_growth    accuracy rating gained per wave (on top of each type's base), capped per type
--   supplies      false = nothing at all between waves (no pickups, no guns)
--   give_guns     false (default): the breather no longer hands out a gun; Bond re-arms from the weapons enemies drop (true = the old rotating gift)
--   give_ammo     false (default): no breather ammo; true = +ammo_frac of each ammo type's max for the guns he carries (no gun is given)
--   max_drops     cap on loose dropped weapons on the floor (default 20; the engine has 30 weapon slots in all and recycles the oldest
--                 itself, this trims explicitly, never the one next to Bond: drop_keep_radius 400). Keep it well under 30: with 25+
--                 guns on the floor no model slot is left for a centre ammo box or armour, so those drops silently fail to appear
--   knife_range / knife_cooldown   the knife throwers' farthest throw (units, 4500) and seconds between throws (1.2); per spawn too
--   sight         chr->visionrange in world units for every enemy (15000); per spawn too
--   engage        (per spawn only) effective firing range in units; default by weapon: rifles 6000, SMGs 3500, pistols 2500
--   pickup_mode   health/armour between waves: "boss" (only after boss waves), "random" (pickup_chance after any wave),
--                 "boss+random" (default), "every" (every wave, the old behaviour), "none". Guns/ammo are separate (gun_every_wave).
--   pickup_chance chance of a random drop after a non-boss wave (0.20). A drop gives health OR armour, never both.
--   health_gifts  false (default, A6): no health gift between waves; Bond heals a little per kill instead (health_per_kill, by enemy tier)
--   ammo_every / ammo_max_center / ammo_ttl / ammo_rounds / centre_radius   (A6) one real MP ammo box (PP7 + KF7 + shotgun rounds, the
--                 counts in ammo_rounds) appears on a pit-centre pad every ammo_every seconds (40), at most ammo_max_center (3)
--                 uncollected, each gone after ammo_ttl (90). It is not a gun: it does not rotate through weapons.
--   armour_center / armour_chance / armour_amount / armour_ttl   (A6) a body armour pickup in the centre after every boss wave and, with
--                 armour_chance (0.25), after other waves; replaces the old armour gift
--   crowd_impostors  (PC) flat camera-facing pictures of the crowd skins on the spare seats behind the real spectators (1000; 0 = none):
--                 no chr slots, no sim cost, one draw call. crowd.cheer() raises their arms (wave clear, boss down)
--   crowd         real spectators in the stands, front rows first (150; crowd_real is the same setting); they appear crowd_rate a second (200: the mode ticks 4 times a second, so 50 a tick),
--                 so a big crowd does not hitch the start. The stage needs the chr slots: GE_COLOSSEUM_SLOTS=<n> (default 600) on a PC that can take 1000+
--   reactions     the crowd cheers and boos (true; false = only the old wave-clear / boss reactions, no sounds). REACT below has, per
--                 trigger: level (a stronger reaction may cut in on a weaker one inside react_gap), frac (share of the impostors that
--                 raise their arms; a boo lowers them instead: the atlas has no boo pose), secs (how long), real (share of the real
--                 spectators that play an animation: cheer = arms up / dance / laugh, boo = fists / folded arms / laugh), sound (a
--                 Data/Sounds name, see modding/arena.md), vol, duck (the other crowd sounds drop to this) and cooldown (seconds).
--                 multikill_n kills inside multikill_window seconds = a multi-kill; big_hit = Bond losing this much of a bar (health +
--                 armour) inside 1 s; low_health = his health falling under this (again once it has been back above it + 0.15);
--                 crowd_volume scales every crowd sound (the game's FX volume applies on top)
--   crowd_screams false (default); true = the crowd also screams: layered game hit yells per trigger (SCREAMS: a kill 2-4, a headshot
--                 or multi-kill 6-10, a wave clear 12-16, a boss kill all 25 + the girls + the crushed yell over an explosion, Bond's
--                 death the evil laugh and a few yells), staggered with random pitch and pan; "only" = the yells instead of the WAVs.
--                 scream_volume (0.8) is their level. Needs nothing in Data/Sounds: the sounds are the game's own. GE_ARENA_SCREAMS=1 = true.
--   corpse_keep   seconds before a dead enemy is removed: -1 (default) = keep every body; 4 = the stock fade-out; above 4 the bodies stay whole until then
--   corpse_limit  when more than this many corpses are kept, the oldest go (400); also the oldest go first whenever chr slots run low, so a wave never starves
--   first_wave    test hook: start at this wave;  debug_track / debug_clear_after: test hooks
--   test_keys     true = the wave-jump hotkeys work (or set GE_ARENA_TESTKEYS=1): PageDown/PageUp = next/back wave and clear the
--                 live enemies, with Shift = start it on top of them (never in co-op or netplay)
--
-- Per-Bond safe: it only reads bond.count/pos(p)/dead(p) and hands pickups to every Bond; math.random is the session's.

local S = {
  mode        = "fixed",
  waves       = 20,
  boss_every  = 5,
  breather    = 12,
  start_delay = 6,
  max_alive       = 16,
  max_alive_every = 2,
  max_alive_cap   = 50,   -- benchmark (i7-8700K): 50 live is free at 144 fps, ~75 before any dip
  hp_growth   = 0.10,
  acc_growth  = 2.5,
  supplies    = true,
  crowd       = 150,
  crowd_impostors = 1000,
  crowd_rate  = 200,

  -- crowd reactions (presentation: the impostors and the sounds; the real spectators' animations follow the same sim events)
  reactions        = true,
  multikill_n      = 3,
  multikill_window = 2.0,
  big_hit          = 0.10,     -- a wave-3 squad shooting Bond takes about 0.06 to 0.10 a second
  low_health       = 0.25,
  react_gap        = 1.0,      -- seconds after a reaction before another of the same or a lower level
  crowd_volume     = 1.0,
  crowd_screams    = false,    -- true: the crowd also screams the game's own hit yells (SCREAMS); "only": the yells instead of the WAVs
  scream_volume    = 0.8,
  -- per trigger, layers of { set, count [, count_hi], vol =, pitch = {lo, hi}, gap = {lo, hi} seconds, delay = seconds } (SCREAM_SETS below)
  SCREAMS = {
    kill       = { { "male", 2, 4 } },
    headshot   = { { "male", 6, 10 } },
    multikill  = { { "male", 6, 10 } },
    wave_clear = { { "male", 12, 16 } },
    boss_kill  = { { "boom", 1, vol = 1.0, pitch = { 0.9, 1.0 } }, { "male", 25, gap = { 0.03, 0.10 } }, { "girl", 4, delay = 0.3 },
                   { "crushed", 1, delay = 0.6, pitch = { 0.9, 1.1 } } },
    bond_died  = { { "laugh", 1, vol = 1.0, pitch = { 0.95, 1.05 } }, { "male", 3, 5, delay = 0.4 } },
  },
  REACT = {
    kill       = { level = 1, frac = 0.12, secs = 1.5, real = 0.05, sound = "cheer_small", vol = 0.45, cooldown = 0.8 },
    headshot   = { level = 2, frac = 0.30, secs = 2.5, real = 0.15, sound = "cheer_big",   vol = 0.65, cooldown = 1.5 },
    multikill  = { level = 3, frac = 0.50, secs = 3.0, real = 0.30, sound = "cheer_big",   vol = 0.80, cooldown = 3.0 },
    wave_clear = { level = 4, frac = 0.80, secs = 4.0, real = 0.60, sound = "cheer_big",   vol = 0.90, cooldown = 0 },
    boss_kill  = { level = 5, frac = 1.00, secs = 5.0, real = 0.75, sound = "cheer_huge",  vol = 1.00, cooldown = 0, duck = 0.5 },
    big_hit    = { level = 2, boo = true, secs = 2.0, real = 0.15, sound = "jeer",    vol = 0.55, cooldown = 4.0 },
    low_health = { level = 3, boo = true, secs = 3.0, real = 0.35, sound = "boo",     vol = 0.75, cooldown = 10.0 },
    bond_died  = { level = 6, boo = true, secs = 5.0, real = 0.85, sound = "boo_big", vol = 1.00, cooldown = 0, duck = 0.25 },
  },

  corpse_keep  = -1,
  corpse_limit = 400,
  test_keys   = false,

  -- pickups
  pickup_mode          = "boss+random",
  pickup_chance        = 0.20,
  pickup_boss_health   = 0.60,   -- after a boss wave: health and armour together (fractions of a full bar)
  pickup_boss_armour   = 0.80,
  pickup_drop_health   = 0.50,   -- a random drop: ONE of these two
  pickup_drop_armour   = 0.60,
  pickup_every_health  = 0.40,   -- pickup_mode "every" (the old per-wave amounts)
  pickup_every_armour  = 0.50,
  health_gifts         = false,  -- A6: false = no health gift between waves (pickup_mode / the random drop give armour only); health comes per kill
  give_guns            = false,  -- the rotating breather weapon (+ AR33 ammo): off by default now, guns come from drops
  give_ammo            = false,  -- ammo for the guns Bond carries, each breather
  ammo_frac            = 0.30,   -- ... this fraction of each ammo type's max
  max_drops            = 20,     -- loose dropped weapons kept on the floor (oldest freed first, never one within drop_keep_radius of Bond)
  drop_keep_radius     = 400,

  -- economy (drop A6): real pickups in the middle of the pit, and health per kill
  ammo_every           = 40,     -- seconds between centre ammo drops (0 = none); runs from wave 1, also through breathers
  ammo_max_center      = 3,      -- most uncollected ammo drops at once; a new one replaces the oldest
  ammo_ttl             = 90,     -- seconds an ammo drop stays on the floor
  ammo_rounds          = { pp7 = 50, kf7 = 60, shotgun = 16 },                   -- rounds one ammo box gives (stock: PP7 20, shotgun 10); set per gun
  centre_radius        = 800,    -- centre pads = the pit pads within this distance of the middle (4 inner at 354, 8 more at 790)
  armour_center        = true,   -- body armour pickup in the centre instead of the A4 armour gift
  armour_chance        = 0.25,   -- chance of one after an ordinary wave (a boss wave always leaves one)
  armour_amount        = 1.0,    -- what the pickup restores, 0..1 of a full bar (Bond keeps the better of this and what he has)
  armour_ttl           = 120,    -- seconds it stays on the floor
  health_per_kill      = { light = 0.03, normal = 0.03, armoured = 0.04, heavy = 0.06, boss = 0.25 },   -- fraction of a full bar per kill, by enemy tier

  -- combat (drop A5)
  knife_range          = 4500,   -- farthest knife throw, units
  knife_cooldown       = 1.2,    -- seconds between a thrower's throws
  sight                = 15000,  -- chr->visionrange for every enemy, world units (the pit is 9000 across)

  -- bosses
  boss_hp_mult    = 2.5,         -- x the base boss health (1 before: they died too fast)
  boss_armour     = 0.5,         -- extra armour as a fraction of the boss' health (armoured chrs are not staggered by hits)
  boss_cycle_mult = 0.35,        -- each later cycle of the 4 bosses is this much tougher

  -- elites: the Bunker 2 alarm reinforcements (Siberian Special Forces + Arctic Commando, with sunglasses) and the Caverns
  -- guards (Janus Special Forces); always armoured or heavy
  elite_from      = 8,           -- first wave they can appear in ordinary waves
  elite_share0    = 0.25,        -- share of an ordinary wave's soldiers at elite_from
  elite_share_inc = 0.05,        -- plus this per wave after it
  elite_share_max = 0.75,

  -- turbo: per-chr animation speed (the Fast Animation cheat on one chr: walk, run, aim, fire and knife throws all speed up)
  turbo_from      = 6,           -- first wave with random turbo enemies
  turbo_share0    = 0.08,        -- share of enemies that are turbo at turbo_from
  turbo_share_inc = 0.03,        -- plus this per wave
  turbo_share_max = 0.40,
  turbo_lo        = 1.25,        -- ordinary turbo animation speed: lo .. min(turbo_hi_max, turbo_hi0 + turbo_hi_inc * wave)
  turbo_hi0       = 1.35,
  turbo_hi_inc    = 0.02,
  turbo_hi_max    = 1.60,
  turbo_knife_from = 14,         -- from this wave the Knife Sisters wave is the TURBO one
  turbo_knife_lo  = 1.60,
  turbo_knife_hi  = 1.80,
}
local REACT_DEFAULT = S.REACT
if type(ARENA_SETTINGS) == "table" then
  for k, v in pairs(ARENA_SETTINGS) do S[k] = v end
  if ARENA_SETTINGS.crowd_real then S.crowd = ARENA_SETTINGS.crowd_real end
end
-- GE_ARENA_SCREAMS=1 turns the scream crowd on without editing a file (presentation only, so a per-machine switch is fine)
if not S.crowd_screams and mode.env_flag("GE_ARENA_SCREAMS") then S.crowd_screams = true end
-- ARENA_SETTINGS.REACT may give just some triggers, or some fields of one: the rest come from the defaults
if S.REACT ~= REACT_DEFAULT then
  local mine = type(S.REACT) == "table" and S.REACT or {}
  S.REACT = {}
  for name, d in pairs(REACT_DEFAULT) do
    local r = {}
    for k, v in pairs(d) do r[k] = v end
    if type(mine[name]) == "table" then for k, v in pairs(mine[name]) do r[k] = v end end
    S.REACT[name] = r
  end
end

-- toughness tiers: hp = x the type's base health, armour = extra stock armour in units of the base 4 damage (grows with the wave
-- like health). Roughly: armoured about 2.5x a normal enemy, heavy about 5x.
local TIERS = {
  light    = { hp = 0.6, armour = 0 },
  normal   = { hp = 1.0, armour = 0 },
  armoured = { hp = 1.0, armour = 1.5 },
  heavy    = { hp = 1.5, armour = 3.5 },
}
local TIER_NAMES = { "light", "normal", "armoured", "heavy" }
local TIER_RANK  = { light = 1, normal = 2, armoured = 3, heavy = 4 }

-- tier mix by wave: { first wave, { light, normal, armoured, heavy } weights }; the last row whose first wave <= w applies
local TIER_MIX = {
  {  1, { 30, 70,  0,  0 } },
  {  4, { 20, 60, 20,  0 } },
  {  8, { 10, 45, 35, 10 } },
  { 13, {  5, 30, 40, 25 } },
  { 18, {  0, 15, 45, 40 } },
}

-- the scary troops (spawn.chr body=/head=; head random). weight = how often.
local ELITES = {
  { weight = 4, body = "Siberian_Special_Forces", sunglasses = true },    -- Bunker 2 alarm reinforcements (black Spetsnaz)
  { weight = 2, body = "Arctic_Commando",         sunglasses = true },    -- Bunker 2 alarm reinforcements (second kind)
  { weight = 3, body = "Janus_Special_Forces" },                          -- the Caverns guards
}
local ELITE_GUNS_EARLY = { "ar33", "d5k", "zmg" }
local ELITE_GUNS_LATE  = { "ar33", "phantom", "rcp90", "autoshot", "d5k" }   -- from wave 12

-- ---------------------------------------------------------------- data -----

local SOLDIER_SKINS = { "russian_soldier", "russian_infantry", "siberian_guard1", "siberian_guard2",
                        "janus_marine", "arctic_commando", "jungle_commando", "janus_special_forces",
                        "st_petersburg_guard", "russian_commandant" }
local SISTER_SKINS  = { "natalya", "natalya_jungle", "xenia", "mayday", "rosika", "sally", "mandy", "vivien" }
local HORDE_SKINS   = { "russian_infantry", "scientist_m", "civilian_m1", "civilian_m2", "terrorist", "biker", "moonraker_m",
                        "scientist_f", "civilian_m3", "st_petersburg_guard" }
local SNIPER_SKINS  = { "janus_special_forces", "jungle_commando", "arctic_commando", "ourumov", "boris", "janus_marine",
                        "siberian_guard1" }
local AMBUSH_SKINS  = { "terrorist", "biker", "siberian_special_forces", "janus_marine", "mishkin", "valentin",
                        "arctic_commando", "jungle_commando", "moonraker_m" }
local WILD_WEAPONS  = { "ak47", "shotgun", "pp7", "d5k", "ar33", "zmg", "dd44", "klobb", "phantom", "rcp90",
                        "cougar", "autoshot", "sniper", "knife", "goldengun", "grenadelauncher" }

-- the order of the ordinary waves (boss waves replace every boss_every'th)
local SEQ = { "standard", "knife", "swarm", "ambush", "sniper", "wild", "tank" }

local TITLES = {
  standard = "THE SOLDIERS", knife = "THE KNIFE SISTERS", swarm = "THE HORDE", ambush = "THE AMBUSH",
  sniper = "THE MARKSMEN", wild = "WILD CARD", tank = "IRON JAWS", turbo_knife = "THE TURBO KNIFE SISTERS",
}

-- pickups: one weapon per breather (cycled)
local SUPPLY_GUNS = { { "ar33", "AR33", 120 }, { "shotgun", "SHOTGUN", 40 }, { "d5k", "D5K", 150 }, { "rcp90", "RC-P90", 200 },
                      { "sniper", "SNIPER", 30 }, { "rocket", "ROCKETS", 12 }, { "phantom", "PHANTOM", 200 }, { "kf7", "KF7", 150 } }

-- the crowd: mostly ordinary people, some from the whole roster
local CROWD_SKINS = { "civilian_f", "civilian_m1", "civilian_m2", "civilian_m3", "scientist_m", "scientist_f", "russian_infantry",
                      "russian_commandant", "naval_officer", "helicopter_pilot", "st_petersburg_guard", "janus_marine",
                      "moonraker_m", "moonraker_f", "biker", "terrorist" }
local FIDGETS = { "yawn", "flies", "scratch_leg", "scratch_butt", "adjust", "sneeze", "chat", "listen", "cough", "laugh", "look" }

-- ------------------------------------------------------------- state -------

local IMPOSTORS = crowd               -- the engine's crowd table (crowd.impostors / crowd.cheer); nil on builds without it
local crowd = {}                      -- spectators: { chr=, next=, react=, react_at= }
local crowd_pads, crowd_i = {}, 1     -- seat pads still to be filled
local crowd_logged = false
local crowd_acc, crowd_fail, crowd_retry = 0, 0, 0
local seats_all, seats_used, seats_planned = {}, {}, {}   -- every stand seat pad; the ones given to the crowd; the ones in crowd_pads
local pit_pads = {}                      -- pit floor pads (not gates, not the middle): benchmark corpses and fighters
local corpse_n = 0                       -- kept corpses (this tick)
local pit_foe_i, pit_order = 0, nil

local gates = { {}, {}, {}, {} }      -- gate -> pad ids, mouth pad first (gate 1 = +x, 2 = +z, 3 = -x, 4 = -z)
local gate_next = { 0, 0, 0, 0 }
local E = {}                          -- tracked enemies: { chr=, boss=, dead_at= }
local queue, qi = {}, 1               -- this wave's spawn list
local wave, wave_title, phase = 0, "", "delay"
local phase_t = 0                     -- seconds in the current phase
local elapsed, kills, ended = 0, 0, false
local flash, flash_t = "", 0
local supply_i = 0
local spawn_fail = 0
local drop_acc, peak_loose, min_slots, min_props, pool_last = 0, 0, 1e9, 1e9, nil   -- drop cap / pool watch

-- ARENA_SETTINGS may give just part of a per-name table: fill the rest from these
local HEALTH_DEFAULT = { light = 0.03, normal = 0.03, armoured = 0.04, heavy = 0.06, boss = 0.25 }
local AMMO_DEFAULT   = { pp7 = 50, kf7 = 60, shotgun = 16, d5k = 90 }

local centre_pads = {}                    -- pit pads near the middle: { id=, d= } (d = distance from the middle)
local IT = {}                             -- live centre pickups: { h=, pad=, kind="ammo"|"armour", label=, born=, ttl= }
local ammo_acc, ammo_i = 0, 0
local healed_total, items_spawned, items_taken, items_expired = 0, 0, 0, 0

local function pick(t) return t[math.random(#t)] end

local function mmss(s) s = math.floor(s); return string.format("%d:%02d", s // 60, s % 60) end

local function alive_count()
  local n = 0
  for _, e in ipairs(E) do if not e.dead_at then n = n + 1 end end
  return n
end

-- remove the oldest kept corpse (the corpse_limit valve, and the slot valve below); false when there is none
local function remove_oldest_corpse()
  local old
  for _, e in ipairs(E) do
    if e.dead_at and not e.gone and (not old or e.dead_at < old.dead_at) then old = e end
  end
  if not old then return false end
  old.gone = true
  spawn.remove(old.chr)
  corpse_n = corpse_n - 1
  return true
end

-- ----------------------------------------------------------------- HUD -----

local function refresh_hud()
  if wave == 0 then
    hud.set(1, "COLOSSEUM")
  else
    local of = (S.mode == "fixed") and ("/" .. S.waves) or ""
    hud.set(1, string.format("WAVE %d%s  %s", wave, of, wave_title))
  end
  local left = alive_count() + (#queue - qi + 1)
  if phase == "fight" then
    hud.set(2, string.format("KILLS %d   LEFT %d   TIME %s", kills, left, mmss(elapsed)))
  else
    hud.set(2, string.format("KILLS %d   TIME %s", kills, mmss(elapsed)))
  end
  hud.set(3, flash)
end

local function say(text, secs)
  flash, flash_t = text, secs or 5
  refresh_hud()
end

-- ------------------------------------------------------------------ crowd --

-- an even sample of n seats: every (#seats / n)'th seat, jittered inside its stride
local function choose_seats(seats, n)
  n = math.min(n, #seats)
  local out = {}
  if n <= 0 then return out end
  local step = #seats / n
  for i = 0, n - 1 do
    local k = math.floor(i * step + math.random() * step) + 1
    if k > #seats then k = #seats end
    out[#out + 1] = seats[k]
  end
  -- shuffle so the stands fill all around rather than in a sweep
  for i = #out, 2, -1 do
    local j = math.random(i)
    out[i], out[j] = out[j], out[i]
  end
  return out
end

local function crowd_spawn_some(max_n)
  local n = 0
  while crowd_i <= #crowd_pads and n < max_n and spawn.free() > S.max_alive + 12 do
    -- a small fixed set of skins only: every extra skin is another model file loaded and kept in the stage pool
    local skin = pick(CROWD_SKINS)
    local pad = crowd_pads[crowd_i]
    local c = spawn.chr(pad, { skin = skin, weapon = "none", behaviour = "spectator" })
    seats_used[pad] = true
    crowd_i = crowd_i + 1
    n = n + 1
    if c then crowd[#crowd + 1] = { chr = c, next = elapsed + 1 + math.random() * 10 }
    else   -- a few seat pads have no floor under them: take another free seat instead
      local alt
      for _ = 1, 60 do
        local p = seats_all[math.random(#seats_all)]
        if not seats_used[p] and not seats_planned[p] then alt = p; break end
      end
      if alt then crowd_pads[#crowd_pads + 1] = alt; seats_planned[alt] = true; crowd_retry = crowd_retry + 1
      else crowd_fail = crowd_fail + 1 end
      print(string.format("ge: arena: crowd seat %d unusable, %s", pad, alt and ("using seat " .. alt) or "no spare seat"))
    end
  end
end

-- (test/benchmark hook) grow the crowd to n spectators in all: more seats, evenly spread among the free ones
local function crowd_set(n)
  local free = {}
  for _, p in ipairs(seats_all) do if not seats_used[p] then free[#free + 1] = p end end
  local want = math.min(n - #crowd_pads, #free)
  if want > 0 then
    for _, p in ipairs(choose_seats(free, want)) do crowd_pads[#crowd_pads + 1] = p; seats_planned[p] = true end
    crowd_logged = false
  end
  return #crowd_pads
end

-- the n lowest seats (front rows first; seats in the same row in random order), shuffled
local function front_seats(seats, n)
  local t = {}
  for _, p in ipairs(seats) do
    local _, y = pads.pos(p)
    t[#t + 1] = { p = p, y = y, r = math.random() }
  end
  table.sort(t, function(a, b) if a.y ~= b.y then return a.y < b.y end return a.r < b.r end)
  local out = {}
  for i = 1, math.min(n, #t) do out[i] = t[i].p end
  for i = #out, 2, -1 do local j = math.random(i); out[i], out[j] = out[j], out[i] end
  return out
end

-- (PC) impostors on up to n seats no real spectator has or will have; returns how many
local function impostors_set(n)
  if not IMPOSTORS then return 0 end
  local free = {}
  for _, p in ipairs(seats_all) do if not seats_used[p] and not seats_planned[p] then free[#free + 1] = p end end
  local xyz = {}
  for _, p in ipairs(choose_seats(free, n)) do
    local x, y, z = pads.pos(p)
    xyz[#xyz + 1] = x; xyz[#xyz + 1] = y; xyz[#xyz + 1] = z
  end
  return IMPOSTORS.impostors(xyz, CROWD_SKINS)
end

local function cheer(secs, frac)
  if IMPOSTORS then IMPOSTORS.cheer(secs, frac) end
end

-- some of the crowd react: `kinds` = list of { animation, share (0..1) }, each spectator rolls once
local function crowd_react(kinds, spread)
  for _, s in ipairs(crowd) do
    local r = math.random()
    local acc_ = 0
    for _, k in ipairs(kinds) do
      acc_ = acc_ + k[2]
      if r < acc_ then s.react, s.react_at = k[1], elapsed + math.random() * (spread or 1.5); break end
    end
  end
end

local react_anims = 0                   -- reaction animations started (log)

local function crowd_tick()
  for _, s in ipairs(crowd) do
    if s.react and elapsed >= s.react_at then
      if spawn.anim(s.chr, s.react, true) then
        react_anims = react_anims + 1
        s.last_react, s.last_react_at = s.react, elapsed   -- (tests watch it return to idle)
      end
      s.react = nil
      s.next = elapsed + 6 + math.random() * 8
    elseif elapsed >= s.next then
      if spawn.anim(s.chr, pick(FIDGETS)) then s.next = elapsed + 5 + math.random() * 12
      else s.next = elapsed + 1 end
    end
  end
end

-- ------------------------------------------------------- crowd reactions --
-- The triggers are sim events (kills, the waves, Bond's vitals and death) and the timing is the mode's own clock, so in co-op and
-- netplay every machine makes the same choices, and the real spectators (sim chrs) play the same animations: math.random here is
-- the session's. The impostors and the sounds are presentation only (the C side picks with its own RNG) and nothing they return
-- steers the game.
local SOUND = sound                     -- the engine's sound table (sound.play_any ...); nil on builds without it
local CHEER_ANIMS = { { "surrender", 0.5 }, { "dance", 0.3 }, { "laugh", 0.2 } }       -- arms up, dance, laugh
local BOO_ANIMS   = { { "fists", 0.55 }, { "cross_arms", 0.25 }, { "laugh", 0.2 } }    -- fists up at the pit, arms folded, a mocking laugh
local SOUND_FALLBACK = { cheer_huge = "cheer_big", boo_big = "boo", jeer = "boo" }
local SOUND_NAMES = { "cheer_small", "cheer_big", "cheer_huge", "jeer", "boo", "boo_big" }
local sound_have = {}                   -- sound name -> variants in Data/Sounds
local react_cur, react_until = 0, -1    -- the level of the reaction under way, and until when it holds weaker ones off
local react_last, react_n = {}, {}      -- trigger -> when it last fired; how many times
local kill_times = {}                   -- recent kills (multi-kill window)
local vit_hist, low_armed = {}, {}      -- per Bond: health + armour over the last second; low-health jeer armed

local function crowd_sound(name, vol, duck)
  if not SOUND or not name then return nil end
  local n = name
  while n do
    if sound_have[n] == nil then sound_have[n] = SOUND.variants(n) end
    if sound_have[n] > 0 then break end
    n = SOUND_FALLBACK[n]
  end
  if not n then return nil end
  return SOUND.play_any(n, vol * (S.crowd_volume or 1), duck or 1)
end
local react_voice                       -- the sound of the reaction under way (a stronger one cutting in fades it out)

-- the scream crowd (crowd_screams): layered game yells through sound.sfx_burst. The C side picks which yells, their pitch, pan and
-- the gaps between them with its own RNG, so math.random (the session's) is never touched here.
local SCREAM_SETS = { male = {}, girl = { "GET_HIT_GIRL1", "GET_HIT_GIRL2", "GET_HIT_GIRL3", "GIRL_GET_HIT1" },
                      crushed = { "CRUSHED_YELL" }, boom = { "EXPLOSION_2A" }, laugh = { "EVIL_LAUGH" } }
for i = 0, 24 do SCREAM_SETS.male[#SCREAM_SETS.male + 1] = "GET_HIT_MALE" .. i end

local function crowd_screams(trigger)
  local layers = S.crowd_screams and SOUND and SOUND.sfx_burst and S.SCREAMS and S.SCREAMS[trigger]
  if not layers then return 0 end
  local n = 0
  for _, l in ipairs(layers) do
    local set = SCREAM_SETS[l[1]] or (type(l[1]) == "table" and l[1])
    if set then
      n = n + SOUND.sfx_burst(set, { l[2] or 1, l[3] or l[2] or 1 }, { vol = (l.vol or S.scream_volume or 0.8) * (S.crowd_volume or 1),
        pitch = l.pitch or { 0.85, 1.25 }, gap = l.gap or { 0.03, 0.15 }, delay = l.delay or 0, pan = l.pan or 0.8 })
    end
  end
  return n
end

-- one reaction: false when its cooldown runs, or a stronger one is under way
local function react(trigger)
  local r = S.reactions and S.REACT[trigger]
  if not r then return false end
  local last = react_last[trigger]
  if last and elapsed - last < (r.cooldown or 0) then return false end
  if elapsed < react_until and (r.level or 1) <= react_cur then return false end
  if elapsed < react_until and react_voice and SOUND then SOUND.stop(react_voice, 0.25) end   -- cut in: the weaker one fades
  react_last[trigger] = elapsed
  react_cur, react_until = r.level or 1, elapsed + math.max(S.react_gap or 0, 0)
  react_n[trigger] = (react_n[trigger] or 0) + 1
  if (r.real or 0) > 0 then   -- the real spectators: each rolls once, a share `real` of them plays one of the animations
    local kinds = {}
    for i, k in ipairs(r.boo and BOO_ANIMS or CHEER_ANIMS) do kinds[i] = { k[1], k[2] * r.real } end
    crowd_react(kinds, math.min(1.5, (r.secs or 2) * 0.4))
  end
  if r.boo then cheer(0, 1)   -- the atlas has no boo pose: the impostors still cheering lower their arms
  elseif (r.frac or 0) > 0 then cheer(r.secs or 2, r.frac) end
  local played, voice
  if S.crowd_screams ~= "only" then played, voice = crowd_sound(r.sound, r.vol or 1, r.duck) end
  react_voice = voice
  local screams = crowd_screams(trigger)
  print(string.format("ge: arena: crowd %s: %s (level %d, impostors %.2f, spectators %.2f, %.1f s) at t=%.2f, sound %s, screams %d",
    r.boo and "boo" or "cheer", trigger, r.level or 1, r.boo and 0 or (r.frac or 0), r.real or 0, r.secs or 2, elapsed, played or "none", screams))
  return true
end

-- an arena enemy died: a boss, a multi-kill, a headshot or an ordinary kill, the strongest that is not cooling down
local function react_kill(victim, boss)
  if boss and react("boss_kill") then return end
  local keep = {}
  for _, t in ipairs(kill_times) do if elapsed - t <= S.multikill_window then keep[#keep + 1] = t end end
  keep[#keep + 1] = elapsed
  kill_times = keep
  if #kill_times >= S.multikill_n and react("multikill") then kill_times = {}; return end
  if spawn.headshot and spawn.headshot(victim) and react("headshot") then return end
  react("kill")
end

-- every tick: a big hit (health + armour down by big_hit inside a second) and low health, per Bond
local function react_vitals()
  for p = 0, bond.count() - 1 do
    local h, a
    if not bond.dead(p) then h, a = bond.vitals(p) end
    if h then
      local tot, hist = h + (a or 0), vit_hist[p] or {}
      local peak = tot
      for _, v in ipairs(hist) do if v > peak then peak = v end end
      if peak - tot >= S.big_hit then react("big_hit"); hist = {} end
      hist[#hist + 1] = tot
      if #hist > 4 then table.remove(hist, 1) end
      vit_hist[p] = hist
      if low_armed[p] == nil then low_armed[p] = h >= S.low_health end
      if low_armed[p] and h < S.low_health then low_armed[p] = false; react("low_health")
      elseif h > S.low_health + 0.15 then low_armed[p] = true end
    else
      vit_hist[p] = nil
    end
  end
end

-- --------------------------------------------------------------- spawning --

local function gate_pad(g)
  local list = gates[g]
  if #list == 0 then return nil end
  gate_next[g] = gate_next[g] + 1
  return list[(gate_next[g] - 1) % #list + 1]
end

-- which gates a spawn may use: all four, the two farthest from Bond, or only mouth pads (snipers)
local function far_gates()
  local bx, _, bz = bond.pos(0)
  local order = { 1, 2, 3, 4 }
  if bx then
    local cx = { 5000, 0, -5000, 0 }
    local cz = { 0, 5000, 0, -5000 }
    table.sort(order, function(a, b)
      local da = (cx[a] - bx) ^ 2 + (cz[a] - bz) ^ 2
      local db = (cx[b] - bx) ^ 2 + (cz[b] - bz) ^ 2
      if da ~= db then return da > db end
      return a < b
    end)
  end
  return { order[1], order[2] }
end

local function spawn_one(entry)
  local g
  if entry.gates == "far" then
    local f = far_gates()
    g = f[math.random(2)]
  elseif entry.gate then
    g = entry.gate
  else
    g = ((entry.n or 0) % 4) + 1
  end
  for _ = 1, #gates[g] do
    local pad = (entry.mouth and gates[g][1 + (entry.n or 0) % 2]) or gate_pad(g)
    if pad then
      local spec = entry.spec
      spec.face = { 0, 0 }
      spec.knife_range = spec.knife_range or S.knife_range            -- A5 combat defaults (a spec may override them)
      spec.knife_cooldown = spec.knife_cooldown or S.knife_cooldown
      spec.sight = spec.sight or S.sight
      local c = spawn.chr(pad, spec)
      if c then
        E[#E + 1] = { chr = c, boss = entry.boss, born = elapsed, tier = spec._tier }
        return true
      end
    end
    if entry.mouth then break end
  end
  return false
end

-- ----------------------------------------------------------- wave design ---

local function clamp(v, lo, hi) return math.max(lo, math.min(hi, v)) end

-- health and armour grow with the wave (also in endless mode: no cap); accuracy gains acc_growth a wave up to its cap
local function hp(w, base) return base * (1 + S.hp_growth * (w - 1)) end
local function acc(w, base, cap) return math.min(cap or 85, base + w * S.acc_growth) end

local function elite_share(w)
  if w < S.elite_from then return 0 end
  return clamp(S.elite_share0 + S.elite_share_inc * (w - S.elite_from), 0, S.elite_share_max)
end

local function turbo_share(w)
  if w < S.turbo_from then return 0 end
  return clamp(S.turbo_share0 + S.turbo_share_inc * (w - S.turbo_from), 0, S.turbo_share_max)
end

-- nil, or a random turbo animation speed (share `mult` times the wave's turbo share)
local function turbo_roll(w, mult)
  if math.random() >= turbo_share(w) * (mult or 1) then return nil end
  local hi = math.min(S.turbo_hi_max, S.turbo_hi0 + S.turbo_hi_inc * w)
  return S.turbo_lo + math.random() * math.max(0, hi - S.turbo_lo)
end

-- a tier from the wave's mix; `min` lifts anything lighter up to it
local function pick_tier(w, min)
  local row = TIER_MIX[1][2]
  for _, r in ipairs(TIER_MIX) do if w >= r[1] then row = r[2] end end
  local total = 0
  for i = 1, 4 do total = total + row[i] end
  local r, tier = math.random() * total, "normal"
  for i = 1, 4 do
    r = r - row[i]
    if r < 0 then tier = TIER_NAMES[i]; break end
  end
  if min and TIER_RANK[tier] < TIER_RANK[min] then tier = min end
  return tier
end

local function pick_elite()
  local total = 0
  for _, e in ipairs(ELITES) do total = total + e.weight end
  local r = math.random() * total
  for _, e in ipairs(ELITES) do
    r = r - e.weight
    if r < 0 then return e end
  end
  return ELITES[1]
end

-- one enemy spec. o: skin | elite=true (a body from ELITES), weapon, tier (else rolled from the mix; min_tier lifts it),
-- hp (extra health multiplier), acc, speed (0..100 rating), beh, knives, anim (turbo animation speed)
local function foe(w, o)
  local tier = o.tier or pick_tier(w, o.min_tier)
  local t = TIERS[tier]
  local mult = o.hp or 1
  local spec = { weapon = o.weapon, health = hp(w, t.hp * mult), accuracy = o.acc, speed = o.speed,
                 behaviour = o.beh or "chase", knives = o.knives, anim_speed = o.anim, _tier = tier }
  if t.armour > 0 then spec.armour = hp(w, t.armour) * mult end
  if o.elite then
    local e = pick_elite()
    spec.body, spec.sunglasses, spec._elite = e.body, e.sunglasses, true
  else
    spec.skin = o.skin
  end
  return spec
end

-- an ordinary soldier, or (from elite_from, by elite_share) one of the scary troops in an armoured/heavy tier
local function soldier(w, o)
  o = o or {}
  local elite = o.elite or (math.random() < elite_share(w))
  local weapon
  if elite then weapon = pick((w >= 12) and ELITE_GUNS_LATE or ELITE_GUNS_EARLY)
  elseif o.weapon then weapon = o.weapon
  else weapon = (w < 3) and "ak47" or pick({ "ak47", "d5k", "ar33", "zmg" }) end
  return foe(w, { skin = pick(SOLDIER_SKINS), elite = elite, min_tier = elite and "armoured" or nil, weapon = weapon,
                  acc = acc(w, 25), speed = 55 + math.random(25) + math.min(15, w), beh = o.beh or "chase",
                  anim = turbo_roll(w, o.turbo_mult) })
end

local function mk_standard(w)
  local q, n = {}, 3 + math.floor(w * 0.9)
  for i = 1, n do q[#q + 1] = { n = i, spec = soldier(w) } end
  return q
end

local function mk_knife(w, turbo)
  local q, n = {}, 3 + math.floor(w * 0.45)
  for i = 1, n do
    local anim
    if turbo then anim = S.turbo_knife_lo + math.random() * (S.turbo_knife_hi - S.turbo_knife_lo)
    else anim = turbo_roll(w, 1.5) end
    q[#q + 1] = { n = i, spec = foe(w, { skin = pick(SISTER_SKINS), weapon = "pp7", knives = true, hp = 0.8, acc = 40,
                                         speed = 80 + math.random(20), anim = anim,
                                         min_tier = turbo and "normal" or nil }) }
  end
  return q
end

local function mk_swarm(w)
  local q, n = {}, 8 + math.floor(w * 1.1)
  for i = 1, n do
    -- weak runners; a few tougher ones in later waves
    local tier = (math.random() < clamp((w - 8) * 0.03, 0, 0.3)) and "armoured" or "normal"
    q[#q + 1] = { n = i, spec = foe(w, { skin = pick(HORDE_SKINS), weapon = pick({ "klobb", "pp7", "zmg" }), hp = 0.35, tier = tier,
                                         acc = acc(w, 15, 55), speed = 80 + math.random(20), beh = "rush", anim = turbo_roll(w, 1.25) }) }
  end
  return q
end

local function mk_ambush(w)
  local q, n = {}, 4 + math.floor(w * 0.6)
  for i = 1, n do
    local elite = math.random() < elite_share(w)
    q[#q + 1] = { n = i, gates = "far", spec = foe(w, { skin = pick(AMBUSH_SKINS), elite = elite, min_tier = elite and "armoured" or nil,
                                                     weapon = pick(elite and ELITE_GUNS_EARLY or { "d5k", "zmg", "ar33" }), hp = 1.2,
                                                     acc = acc(w, 30), speed = 85, beh = "rush", anim = turbo_roll(w) }) }
  end
  return q
end

local function mk_sniper(w)
  local q, n = {}, math.min(6, 2 + math.floor(w / 6))
  for i = 1, n do
    q[#q + 1] = { n = i, gate = ((i - 1) % 4) + 1, mouth = true,
                  spec = foe(w, { skin = pick(SNIPER_SKINS), weapon = "sniper", hp = 1.5, acc = 45, beh = "snipe" }) }
  end
  -- a few soldiers keep Bond moving while the snipers settle
  for i = 1, 3 + math.floor(w / 4) do q[#q + 1] = { n = i, spec = soldier(w, { weapon = "ak47" }) } end
  return q
end

local function mk_wild(w)
  local q, n = {}, 4 + math.floor(w * 0.5)
  local skins = spawn.skins()
  for i = 1, n do
    q[#q + 1] = { n = i, spec = foe(w, { skin = pick(skins), weapon = pick(WILD_WEAPONS), knives = (math.random(5) == 1),
                                         hp = 0.6 + math.random() * 1.4, acc = 20 + math.random(50), speed = 40 + math.random(60),
                                         beh = pick({ "chase", "chase", "rush" }), anim = turbo_roll(w, 1.5) }) }
  end
  return q
end

local function mk_tank(w)
  local q, n = {}, 1 + math.floor(w / 9)
  for i = 1, n do
    q[#q + 1] = { n = i, spec = foe(w, { skin = "jaws", weapon = "rcp90", tier = "heavy", hp = 2.5, acc = 35, speed = 55 }) }
  end
  for i = 1, 2 + math.floor(w / 5) do q[#q + 1] = { n = i, spec = soldier(w, { weapon = "ar33" }) } end
  return q
end

local MK = { standard = mk_standard, knife = mk_knife, swarm = mk_swarm, ambush = mk_ambush, sniper = mk_sniper,
             wild = mk_wild, tank = mk_tank, turbo_knife = function(w) return mk_knife(w, true) end }

-- bosses (cycled; later cycles hit harder). health = base x hp growth x boss_hp_mult, plus boss_armour of that as armour.
-- Every boss comes with armoured elite escorts.
local function boss(w, m, base, skin, weapon, accuracy, speed)
  local health = hp(w, base) * S.boss_hp_mult * m
  return { boss = true, spec = { skin = skin, weapon = weapon, health = health, armour = health * S.boss_armour,
                                 accuracy = accuracy, speed = speed, behaviour = "chase", _tier = "boss" } }
end

local BOSSES = {
  { title = "BOSS: THE BARON",
    make = function(w, m) return { boss(w, m, 10, "baron_samedi", "goldengun", 60, 60) } end },
  { title = "BOSS: THE TRAITOR",
    make = function(w, m) return { boss(w, m, 12, "trevelyan", "rocket", 55, 65) } end },
  { title = "BOSS: IRON JAWS",
    make = function(w, m) return { boss(w, m, 26, "jaws", "rcp90", 45, 60) } end },
  { title = "THE TRIUMVIRATE",
    make = function(w, m)
      return { boss(w, m, 8, "baron_samedi", "goldengun", 60, 60), boss(w, m, 10, "trevelyan", "rocket", 55, 65),
               boss(w, m, 20, "jaws", "rcp90", 45, 60) }
    end },
}

local function is_boss(w) return S.boss_every > 0 and w % S.boss_every == 0 end

local function roman(n)
  local out = ""
  for _, p in ipairs({ { 100, "C" }, { 90, "XC" }, { 50, "L" }, { 40, "XL" }, { 10, "X" }, { 9, "IX" }, { 5, "V" }, { 4, "IV" }, { 1, "I" } }) do
    while n >= p[1] do out = out .. p[2]; n = n - p[1] end
  end
  return out
end

local function kind_for(w)
  local kind = (w == 1) and "standard" or SEQ[(w - 1) % #SEQ + 1]
  if kind == "knife" and w >= S.turbo_knife_from then kind = "turbo_knife" end
  return kind
end

-- a wave's type key and base title
local function key_for(w)
  if is_boss(w) then
    local t = BOSSES[(w // S.boss_every - 1) % #BOSSES + 1].title
    return t, t
  end
  local kind = kind_for(w)
  return kind, TITLES[kind]
end

-- the 2nd wave of a type is "II", the 3rd "III", ...; counted from the wave number so test jumps keep the numbering
local function numbered(w)
  local key, base = key_for(w)
  local k = 0
  for i = 1, w do
    if (key_for(i)) == key then k = k + 1 end
  end
  if k == 1 then return base end
  return base .. " " .. roman(k)
end

local function design(w)
  local q, title
  if type(S.debug_queue) == "function" then   -- test hook: a preset supplies the wave's spawn list (entries as the mk_* functions build them)
    return "TEST " .. w, S.debug_queue(w)
  end
  if is_boss(w) then
    local k = w // S.boss_every
    local b = BOSSES[(k - 1) % #BOSSES + 1]
    local m = 1 + S.boss_cycle_mult * ((k - 1) // #BOSSES)
    title = numbered(w)
    q = b.make(w, m)
    for i = 1, 2 + math.floor(w / 5) do
      q[#q + 1] = { n = i, spec = soldier(w, { elite = true }) }
    end
  else
    local kind = kind_for(w)
    title = numbered(w)
    q = MK[kind](w)
  end
  return title, q
end

-- --------------------------------------------------------------- pickups ----

-- Centre pickups (drop A6): real items on the floor in the middle of the pit (spawn.item), collected by running over them.

local function item_count(kind)
  local n = 0
  for _, it in ipairs(IT) do if it.kind == kind then n = n + 1 end end
  return n
end

-- a free centre pad (no live pickup on it); `inner` = only the 4 innermost
local function free_centre_pad(inner)
  local free = {}
  for _, c in ipairs(centre_pads) do
    if not inner or c.d < 500 then
      local taken = false
      for _, it in ipairs(IT) do if it.pad == c.id then taken = true; break end end
      if not taken then free[#free + 1] = c.id end
    end
  end
  if #free == 0 then return nil end
  return free[math.random(#free)]
end

local function item_drop(it, why)
  spawn.item_remove(it.h)
  if why == "ttl" then items_expired = items_expired + 1 end
  print(string.format("ge: arena: centre %s '%s' removed (%s) after %.0fs", it.kind, it.label, why, elapsed - it.born))
end

function ARENA_CENTRE_ITEMS() return IT end   -- test hook: the live centre pickups ({ h=, pad=, kind=, label=, born=, ttl= })

-- upkeep, every tick: forget what Bond collected, remove what has lain too long
local function items_tick()
  local keep = {}
  for _, it in ipairs(IT) do
    if not spawn.item_alive(it.h) then
      items_taken = items_taken + 1
      print(string.format("ge: arena: centre %s '%s' COLLECTED after %.0fs", it.kind, it.label, elapsed - it.born))
    elseif elapsed - it.born > it.ttl then
      item_drop(it, "ttl")
    else
      keep[#keep + 1] = it
    end
  end
  IT = keep
end

-- kind "ammo": one MP ammo box with rounds for the PP7, KF7 and shotgun; "armour": body armour. Returns true when placed.
local function centre_item(kind)
  if #centre_pads == 0 then return false end
  local what, label, opts, ttl
  if kind == "ammo" then
    if S.ammo_max_center <= 0 then return false end
    while item_count("ammo") >= S.ammo_max_center do   -- the oldest uncollected one makes room
      for k, it in ipairs(IT) do
        if it.kind == "ammo" then item_drop(it, "replaced"); table.remove(IT, k); break end
      end
    end
    what, label = "ammo", "PP7 + KF7 + SHOTGUN"   -- one real MP ammo box holding all three
    opts = { ammo = {} }
    for _, g in ipairs({ "pp7", "kf7", "shotgun" }) do opts.ammo[g] = S.ammo_rounds[g] or AMMO_DEFAULT[g] end
    ttl = S.ammo_ttl
  else
    what, label, opts, ttl = "armour", "ARMOUR", { amount = S.armour_amount }, S.armour_ttl
    for _, it in ipairs(IT) do   -- one at a time: an uncollected one just gets a fresh timer
      if it.kind == "armour" then
        it.born = elapsed
        print("ge: arena: centre armour still on the floor, timer restarted")
        say("ARMOUR DROP", 4)
        return true
      end
    end
  end
  local pad = free_centre_pad(kind == "armour")
  if not pad then return false end
  local ok, h = pcall(spawn.item, pad, what, opts)
  if not ok then print("ge: arena: centre item error:", h); return false end
  if not h then print("ge: arena: centre item '" .. what .. "' could not be placed"); return false end
  IT[#IT + 1] = { h = h, pad = pad, kind = kind, label = label, born = elapsed, ttl = ttl }
  items_spawned = items_spawned + 1
  print(string.format("ge: arena: centre %s '%s' spawned on pad %d (%s%s) t=%s", kind, label, pad, what,
    opts.ammo and (type(opts.ammo) == "table" and ", box" or (", " .. opts.ammo .. " rounds")) or (", amount " .. tostring(opts.amount)), mmss(elapsed)))
  say((kind == "ammo") and "AMMO DROP" or "ARMOUR DROP", 4)
  return true
end

-- health per kill: a fraction of a full health bar to the Bond who got the kill (CHR_NONE / unknown killer: the Bond nearest the victim)
local function kill_heal(victim, killer, e)
  local amt = (S.health_per_kill and S.health_per_kill[e.tier or "normal"]) or HEALTH_DEFAULT[e.tier or "normal"] or 0
  if amt <= 0 then return end
  local p = killer and bond.player(killer)
  if not p then
    if killer ~= nil and killer ~= CHR_NONE then return end   -- another chr's stray shot killed it: nobody earns anything
    local x, z = spawn.where(victim)
    local best, bd = nil, 1e18
    for q = 0, bond.count() - 1 do
      if bond.dead(q) == false then
        local bx, _, bz = bond.pos(q)
        local d = x and bx and ((bx - x) ^ 2 + (bz - z) ^ 2) or 0
        if d < bd then best, bd = q, d end
      end
    end
    p = best
  end
  if p == nil or bond.dead(p) ~= false then return end
  local before = bond.vitals(p)
  bond.heal(p, amt, 0)
  local after = bond.vitals(p)
  healed_total = healed_total + math.max(0, (after or 0) - (before or 0))
  if S.debug_heal then
    print(string.format("ge: arena: KILL HEAL player %d tier %s +%.2f: health %.3f -> %.3f", p, e.tier or "?", amt, before or -1, after or -1))
  end
end

-- Between waves: a rotating gun + AR33 ammo (gun_every_wave), and health/armour by pickup_mode: after a boss wave both
-- (pickup_boss_*), after other waves a pickup_chance of ONE of health or armour (pickup_drop_*). "every" = the old every-wave amounts.
-- A6: health_gifts = false drops every health part (the random drop is then armour only); armour_center = true replaces the armour part
-- with the centre pickup below.
local function give_supplies()
  if not S.supplies then return "" end
  local mode = S.pickup_mode
  local boss_wave = is_boss(wave)
  local h, a, why = 0, 0, nil
  if mode == "every" then
    h, a, why = S.pickup_every_health, S.pickup_every_armour, "every wave"
  elseif boss_wave and (mode == "boss" or mode == "boss+random") then
    h, a, why = S.pickup_boss_health, S.pickup_boss_armour, "boss wave"
  elseif (mode == "random" or mode == "boss+random") and math.random() < S.pickup_chance then
    if S.health_gifts and math.random(2) == 1 then h, why = S.pickup_drop_health, "random drop (health)"
    else a, why = S.pickup_drop_armour, "random drop (armour)" end
  end
  if not S.health_gifts then h = 0 end
  local centre_armour = false
  if S.armour_center and mode ~= "none" then
    a = 0
    centre_armour = boss_wave or math.random() < S.armour_chance
    if centre_armour then centre_armour = centre_item("armour") end
  end
  if h == 0 and a == 0 then why = nil end
  local g
  if S.give_guns or S.gun_every_wave then   -- gun_every_wave: the old name, still honoured
    supply_i = supply_i % #SUPPLY_GUNS + 1
    g = SUPPLY_GUNS[supply_i]
  end
  for p = 0, bond.count() - 1 do
    if bond.dead(p) == false then
      if h > 0 or a > 0 then bond.heal(p, h, a) end
      if g then
        bond.give(p, g[1], g[3])
        bond.give(p, "ar33", 40)
      end
      if S.give_ammo then bond.give_ammo(p, S.ammo_frac) end
    end
  end
  local parts = {}
  if h > 0 then parts[#parts + 1] = "HEALTH" end
  if a > 0 then parts[#parts + 1] = "ARMOUR" end
  if centre_armour then parts[#parts + 1] = "ARMOUR DROP IN THE CENTRE" end
  if g then parts[#parts + 1] = g[2] end
  if S.give_ammo then parts[#parts + 1] = "AMMO" end
  print(string.format("ge: arena: pickups after wave %d: health %.2f armour %.2f gun %s ammo %s centre armour %s (%s)", wave, h, a, g and g[2] or "-", tostring(S.give_ammo and true or false), tostring(centre_armour), why or "none"))
  if #parts == 0 then return "" end
  return "SUPPLIES: " .. table.concat(parts, " + ")
end

-- ------------------------------------------------------------ wave flow ----

local function start_wave(w)
  wave = w
  wave_title, queue = design(w)
  qi = 1
  phase, phase_t = "fight", 0
  say(string.format("WAVE %d: %s", w, wave_title), 6)
  if is_boss(w) then crowd_react({ { "startled", 0.5 } }, 1.0) end
  local tiers, elites, turbos = { light = 0, normal = 0, armoured = 0, heavy = 0, boss = 0 }, 0, 0
  for _, e in ipairs(queue) do
    tiers[e.spec._tier] = tiers[e.spec._tier] + 1
    if e.spec._elite then elites = elites + 1 end
    if e.spec.anim_speed then turbos = turbos + 1 end
  end
  print(string.format("ge: arena: wave %d '%s': %d enemies (light %d, normal %d, armoured %d, heavy %d, boss %d; elite %d, turbo %d)",
    w, wave_title, #queue, tiers.light, tiers.normal, tiers.armoured, tiers.heavy, tiers.boss, elites, turbos))
end

local function final_wave() return S.mode == "fixed" and wave >= S.waves end

local function finish(win)
  if ended then return end
  ended = true
  print(string.format("ge: arena: %s wave %d kills %d time %s", win and "WON" or "OVER", wave, kills, mmss(elapsed)))
  if win then
    say(string.format("ARENA CLEARED! KILLS %d  TIME %s", kills, mmss(elapsed)), 30)
    mission.win(string.format("Arena cleared: %d kills in %s", kills, mmss(elapsed)))
  else
    say(string.format("YOU FELL IN WAVE %d - KILLS %d", wave, kills), 30)
  end
end

local function wave_cleared()
  print(string.format("ge: arena: wave %d cleared, kills %d, t=%s; economy: healed %.2f of a bar, centre items %d spawned / %d collected / %d expired",
    wave, kills, mmss(elapsed), healed_total, items_spawned, items_taken, items_expired))
  if pool_last then
    print(string.format("ge: arena: pools wave %d: loose drops %d, held %d, free weapon slots %d (min %d of 30), free props %d (min %d); peak loose %d",
      wave, pool_last[1], pool_last[2], pool_last[3], min_slots, pool_last[4], min_props, peak_loose))
  end
  if S.reactions then react("wave_clear")   -- the crowd cheers (a boss kill just before covers it)
  else crowd_react({ { "dance", 0.3 }, { "laugh", 0.3 }, { "surrender", 0.15 } }, 1.5); cheer(4, 0.8) end
  if final_wave() then finish(true); return end
  phase, phase_t = "breather", 0
  local sup = give_supplies()
  say(string.format("WAVE %d CLEARED. %s", wave, sup), math.max(4, S.breather - 3))
  if sup == "" then say(string.format("WAVE %d CLEARED", wave), math.max(4, S.breather - 3)) end
end

-- Test jump (the test_key event): start wave `wave + dir` now. clear = remove the live enemies first (spawn.remove: no kills, drops or
-- health) and drop the queued spawns; otherwise they stay and the old queue still spawns after the new wave's.
local function test_jump(dir, clear)
  if ended then return end
  local from, target = wave, math.max(1, wave + dir)
  local before, kills0, healed0 = alive_count(), kills, healed_total
  local loose0 = spawn.pools()
  if clear then
    for _, e in ipairs(E) do
      if not e.dead_at then spawn.remove(e.chr); e.dead_at, e.gone = elapsed, true end
    end
    queue, qi = {}, 1
  end
  local rest = {}
  if not clear then for i = qi, #queue do rest[#rest + 1] = queue[i] end end
  if S.mode == "fixed" and target > S.waves then
    print(string.format("ge: arena: TEST %s past the last wave: win", clear and "next" or "next+"))
    finish(true)
    return
  end
  start_wave(target)
  for _, e in ipairs(rest) do queue[#queue + 1] = e end
  say(string.format("TEST: WAVE %d (%s)", target, clear and "CLEARED" or "STACKED"), 2)
  local loose1 = spawn.pools()
  print(string.format("ge: arena: TEST %s%s: wave %d -> %d '%s', alive %d -> %d, queued kept %d, kills %d -> %d, healed %.2f -> %.2f, loose drops %s -> %s",
    dir > 0 and "next" or "back", clear and "" or "+", from, target, wave_title, before, alive_count(), #rest,
    kills0, kills, healed0, healed_total, tostring(loose0), tostring(loose1)))
end

local TEST_KEYS = { next = { 1, true }, back = { -1, true }, ["next+"] = { 1, false }, ["back+"] = { -1, false } }

local function test_key(name)
  if not (S.test_keys or mode.env_flag("GE_ARENA_TESTKEYS")) then return end
  local k = TEST_KEYS[name]
  if not k or phase == "delay" and wave == 0 and k[1] < 0 then return end
  local ok, err = pcall(test_jump, k[1], k[2])
  if not ok then print("ge: arena: test jump error:", err) end
end

on("test_key", test_key)
function ARENA_TEST_KEY(name) test_key(name) end   -- test hook: a preset script presses the same keys on a timer

-- Benchmark hooks (arena-bench.lua): crowd_set(n) grows the crowd to n; pit_foes(n, kill_after, beh) puts n soldiers on distinct pit pads spread
-- over the floor (kill_after seconds: killed through the damage path, so a real corpse; nil = they stay and fight); counts() = crowd, corpses, alive
ARENA_HOOKS = {
  crowd_set = crowd_set,
  impostors = impostors_set,
  cheer = cheer,
  say = say,
  counts = function() return #crowd, corpse_n, alive_count(), crowd_fail, crowd_retry end,
  -- crowd reactions: react(trigger) fires one now (false = cooling down / held off); reactions() = trigger -> count, animations started;
  -- spectators() = the spectator records ({ chr=, last_react=, last_react_at= }; read only)
  react = react,
  reactions = function() return react_n, react_anims end,
  spectators = function() return crowd end,
  -- live enemies by what they are doing: attacking (ACT_ATTACK, ATTACKWALK, ATTACKROLL), moving (SIDESTEP, RUNPOS, PATROL, GOPOS), other
  acts = function()
    local att, mov, oth = 0, 0, 0
    for _, e in ipairs(E) do
      if not e.dead_at then
        local _, _, act = spawn.where(e.chr)
        if act == 8 or act == 9 or act == 10 then att = att + 1
        elseif act == 11 or act == 13 or act == 14 or act == 15 then mov = mov + 1
        else oth = oth + 1 end
      end
    end
    return att, mov, oth
  end,
  pit_foes = function(n, kill_after, beh)
    if not pit_order then   -- one shuffle: successive calls walk through different pads
      pit_order = {}
      for i, p in ipairs(pit_pads) do pit_order[i] = p end
      for i = #pit_order, 2, -1 do local j = math.random(i); pit_order[i], pit_order[j] = pit_order[j], pit_order[i] end
    end
    local pads = pit_order
    local placed = 0
    for k = 1, n do
      local pad = pads[(pit_foe_i + k - 1) % #pads + 1]
      if spawn.free() <= 8 then break end
      local spec = soldier(5, { beh = beh })
      spec.face = { 0, 0 }
      spec.sight = S.sight
      local c = spawn.chr(pad, spec)
      if c then
        E[#E + 1] = { chr = c, born = elapsed, tier = spec._tier, kill_due = kill_after and (elapsed + kill_after) or nil }
        placed = placed + 1
      end
    end
    pit_foe_i = pit_foe_i + n
    return placed
  end,
}

-- ----------------------------------------------------------- events/tick ---

on("stage_start", function(stage)
  local ok, err = pcall(function()
    local seats = {}
    for i = 0, pads.count() - 1 do
      local x, y, z = pads.pos(i)
      if x and y > 1 then
        seats[#seats + 1] = i               -- a stand seat (the stands are raised); not a gate or pit pad
      elseif x then
        local dc = math.sqrt(x * x + z * z)
        if dc <= S.centre_radius then centre_pads[#centre_pads + 1] = { id = i, d = dc } end
        if dc > 900 and math.abs(x) <= 4500 and math.abs(z) <= 4500 then pit_pads[#pit_pads + 1] = i end
        local g, depth
        if x > 4500 then g, depth = 1, x elseif z > 4500 then g, depth = 2, z
        elseif x < -4500 then g, depth = 3, -x elseif z < -4500 then g, depth = 4, -z end
        if g then
          local list = gates[g]
          list[#list + 1] = { id = i, d = depth }
        end
      end
    end
    for g = 1, 4 do
      table.sort(gates[g], function(a, b) if a.d ~= b.d then return a.d < b.d end return a.id < b.id end)
      for k, e in ipairs(gates[g]) do gates[g][k] = e.id end
    end
    table.sort(centre_pads, function(a, b) if a.d ~= b.d then return a.d < b.d end return a.id < b.id end)
    print(string.format("ge: arena: gate pads %d/%d/%d/%d, mode %s, waves %d, boss every %d; %d centre pads (radius %d)",
      #gates[1], #gates[2], #gates[3], #gates[4], S.mode, S.waves, S.boss_every, #centre_pads, S.centre_radius))
    if #gates[1] == 0 then
      hud.set(1, "ARENA MODE: run it on the Colosseum (GE_STAGE=colosseum)")
      ended = true
      return
    end
    seats_all = seats
    -- the stock game fades a dead chr out and removes it after 1.5 s: to keep bodies longer than the default 4 s (or for good) it must not
    spawn.keep_bodies(S.corpse_keep < 0 or S.corpse_keep > 4)
    crowd_pads = front_seats(seats, S.crowd)
    for _, p in ipairs(crowd_pads) do seats_planned[p] = true end
    local nimp = (S.crowd_impostors or 0) > 0 and impostors_set(S.crowd_impostors) or 0
    print(string.format("ge: arena: crowd %d of %d seats, %d impostors", #crowd_pads, #seats, nimp))
    if SOUND and S.reactions then   -- load the crowd recordings now (none is fine), so the first cheer does not read the disk
      local parts = {}
      for _, n in ipairs(SOUND_NAMES) do sound_have[n] = SOUND.variants(n); parts[#parts + 1] = n .. " " .. sound_have[n] end
      print("ge: arena: crowd sounds in Data/Sounds: " .. table.concat(parts, ", "))
    end
    if S.crowd_screams then
      print(string.format("ge: arena: crowd screams %s (game yells%s)", SOUND and SOUND.sfx_burst and "on" or "unavailable",
        S.crowd_screams == "only" and ", instead of the WAVs" or ""))
    end
    say(string.format("GET READY - WAVE 1 IN %d", S.start_delay), S.start_delay)
  end)
  if not ok then print("ge: arena: setup error:", err); ended = true end
end)

on("chr_died", function(victim, killer)
  for _, e in ipairs(E) do
    if e.chr == victim and not e.dead_at then
      e.dead_at = elapsed
      kills = kills + 1
      local ok, err = pcall(kill_heal, victim, killer, e)
      if not ok then print("ge: arena: kill heal error:", err) end
      if e.boss then say("BOSS DOWN!", 4) end
      if S.reactions then
        local ok2, err2 = pcall(react_kill, victim, e.boss)
        if not ok2 then print("ge: arena: crowd reaction error:", err2) end
      elseif e.boss then
        crowd_react({ { "dance", 0.5 }, { "surrender", 0.2 } }, 1.0)
        cheer(5, 1.0)
      end
      return
    end
  end
end)

on("bond_died", function(killer, player)
  local ok, err = pcall(react, "bond_died")   -- the crowd boos (each Bond that falls, in co-op)
  if not ok then print("ge: arena: crowd reaction error:", err) end
  if bond.all_down() then finish(false) end
end)

local hud_acc, track_acc = 0, 0
local crowd_err = false
local debug_kills = 0
on("tick", function(dt)
  if ended then   -- the crowd keeps reacting (a boo for the fall) while the stage is still up
    elapsed = elapsed + dt
    local ok, err = pcall(crowd_tick)
    if not ok and not crowd_err then crowd_err = true; print("ge: arena: crowd tick error:", err) end
    return
  end
  elapsed = elapsed + dt
  if S.reactions then react_vitals() end
  phase_t = phase_t + dt
  if flash_t > 0 then
    flash_t = flash_t - dt
    if flash_t <= 0 then flash = "" end
  end

  -- safety net: an enemy that vanished without a death event counts as gone; corpses are removed after a while
  local keep_s = S.corpse_keep
  corpse_n = 0
  for _, e in ipairs(E) do
    if not e.dead_at and not spawn.alive(e.chr) then e.dead_at = elapsed; kills = kills + 1 end
    if e.kill_due and not e.dead_at and elapsed >= e.kill_due then e.kill_due = nil; spawn.kill(e.chr) end   -- benchmark corpses
    if e.dead_at and not e.gone then
      if keep_s >= 0 and elapsed - e.dead_at > keep_s then e.gone = true; spawn.remove(e.chr)
      else corpse_n = corpse_n + 1 end
    end
  end
  while S.corpse_limit and corpse_n > S.corpse_limit do   -- too many bodies kept: the oldest go
    if not remove_oldest_corpse() then break end
  end
  if S.debug_kill then   -- test hook (drop stress): kill every enemy this many seconds after it spawned, so there are many real kills and drops
    for _, e in ipairs(E) do
      if not e.dead_at and e.born and elapsed - e.born > S.debug_kill and (not e.kill_at or elapsed - e.kill_at > 2) then
        e.kill_at = elapsed
        debug_kills = debug_kills + 1   -- debug_headshot = n: every n'th of these kills counts as a headshot
        spawn.kill(e.chr, (S.debug_headshot and debug_kills % S.debug_headshot == 0) and "head" or nil)
      end
    end
  end

  -- loose dropped weapons: cap them (oldest first, never the one next to Bond) and watch the pools, once a second
  drop_acc = drop_acc + dt
  if drop_acc >= 1 then
    drop_acc = 0
    if S.max_drops and S.max_drops >= 0 then spawn.trim_drops(S.max_drops, S.drop_keep_radius) end
    if #E > 60 then   -- drop old records now and then so the list stays short
      local keep = {}
      for _, e in ipairs(E) do if not e.gone then keep[#keep + 1] = e end end
      E = keep
    end
    local loose, held, fw, fp = spawn.pools()
    if loose then
      pool_last = { loose, held, fw, fp }
      if loose > peak_loose then peak_loose = loose end
      if fw < min_slots then min_slots = fw end
      if fp < min_props then min_props = fp end
    end
  end


  -- centre pickups (A6): upkeep, and an ammo drop every ammo_every seconds once wave 1 has started
  if #IT > 0 then items_tick() end
  if wave >= 1 and S.supplies and S.ammo_every > 0 then
    ammo_acc = ammo_acc + dt
    if ammo_acc >= S.ammo_every then
      ammo_acc = 0
      centre_item("ammo")
    end
  end

  crowd_acc = crowd_acc + dt * S.crowd_rate   -- the crowd comes in a few at a time (a big one all at once would hitch)
  if crowd_acc >= 1 then
    local n = math.floor(crowd_acc)
    crowd_acc = crowd_acc - n
    crowd_spawn_some(n)
  end
  if not crowd_logged and crowd_i > #crowd_pads then
    crowd_logged = true
    print(string.format("ge: arena: crowd of %d placed at t=%.2f", #crowd, elapsed))
  end
  crowd_tick()

  if phase == "delay" then
    if phase_t >= S.start_delay then start_wave(S.first_wave or 1) end   -- first_wave: test hook (jump straight to a wave)
  elseif phase == "fight" then
    -- trickle the queue in, a couple at a time, keeping at most max_alive on the floor
    local n = 0
    local cap = math.min(S.max_alive_cap, S.max_alive + (wave - 1) // math.max(1, S.max_alive_every))
    -- chr slots running low (a removal lands a frame later, so a few go each tick): the oldest corpses make room, a wave never waits for slots
    if qi <= #queue and spawn.free() <= S.max_alive + 20 then
      for _ = 1, 6 do if not remove_oldest_corpse() then break end end
    end
    while qi <= #queue and alive_count() < cap and n < 2 and spawn.free() > 8 do
      local entry = queue[qi]
      if spawn_one(entry) then qi = qi + 1; n = n + 1; spawn_fail = 0
      else
        spawn_fail = spawn_fail + 1
        if spawn_fail >= 6 then print("ge: arena: could not place an enemy, skipping it"); qi = qi + 1; spawn_fail = 0 end
        break
      end
    end
    if S.debug_clear_after and phase_t >= S.debug_clear_after then   -- test hook: remove whoever is left (counts as cleared)
      for _, e in ipairs(E) do if not e.dead_at then spawn.remove(e.chr) end end
    end
    if qi > #queue and alive_count() == 0 then wave_cleared() end
  elseif phase == "breather" then
    if phase_t >= S.breather then start_wave(wave + 1) end
  end
  hud_acc = hud_acc + dt
  if hud_acc >= 0.5 then hud_acc = 0; refresh_hud() end
  if S.debug_path then   -- test hook: path efficiency. Per enemy: distance walked / straight line from its start, when it first gets within each ring of Bond
    local bx, _, bz = bond.pos(0)
    for _, e in ipairs(E) do
      if not e.dead_at and bx then
        local x, z, act = spawn.where(e.chr)
        if x then
          if not e.px then e.px, e.pz, e.sx, e.sz, e.len, e.t0, e.rings = x, z, x, z, 0, elapsed, {} end
          e.len = e.len + math.sqrt((x - e.px) ^ 2 + (z - e.pz) ^ 2)
          e.px, e.pz = x, z
          local d = math.sqrt((x - bx) ^ 2 + (z - bz) ^ 2)
          for _, ring in ipairs({ 3000, 1500 }) do
            if d < ring and not e.rings[ring] then
              e.rings[ring] = true
              local straight = math.sqrt((x - e.sx) ^ 2 + (z - e.sz) ^ 2)
              print(string.format("ge: arena: PATH chr %d ring %d: start (%.0f,%.0f) end (%.0f,%.0f) bond (%.0f,%.0f) walked %.0f straight %.0f ratio %.2f t=%.1fs",
                e.chr, ring, e.sx, e.sz, x, z, bx, bz, e.len, straight, e.len / math.max(1, straight), elapsed - e.t0))
            end
          end
        end
      end
    end
  end
  if S.debug_track then   -- test hook: log each live enemy's distance to Bond and action type every 2 s, and action flips per second
    for _, e in ipairs(E) do
      if not e.dead_at then
        local _, _, act = spawn.where(e.chr)
        if act then
          if e.last_act and e.last_act ~= act then e.flips = (e.flips or 0) + 1; e.flips_t = e.flips_t or elapsed end
          e.last_act = act
          if e.flips_t and elapsed - e.flips_t >= 1 then
            if e.flips > 2 then print(string.format("ge: arena: FLIPS chr %d: %d action changes in %.1fs (act %d)", e.chr, e.flips, elapsed - e.flips_t, act)) end
            e.flips, e.flips_t = nil, nil
          end
        end
      end
    end
    track_acc = track_acc + dt
    if track_acc >= 2 then
      track_acc = 0
      local bx, _, bz = bond.pos(0)
      for _, e in ipairs(E) do
        if not e.dead_at then
          local x, z, act = spawn.where(e.chr)
          if x and bx then
            print(string.format("ge: arena: track t=%.0f chr %d dist %.0f act %d", elapsed, e.chr, math.sqrt((x - bx) ^ 2 + (z - bz) ^ 2), act))
          end
        end
      end
    end
  end
end)
