-- Bank heist (GE_MODE=heist, GE_STAGE=levelgen: the generated Janus bank, tools/levelgen/janus_bank.json on the levelgen slot; the
-- "Play Heist.bat" launcher). Shipped to Data/Scripts/modes/heist.lua by the build with heist_lib.lua, levelgen_layout.lua and
-- heist_bank_map.lua. designs/bank-heist.md (the bank: sections 14 and 15). The Janus front bank, St Petersburg branch: get in, climb
-- the keycard ladder, crack the vault, carry out every gold bar you can, and get every Bond out alive. The map's tables (doors, areas,
-- pads, cameras) come from LEVEL by name (heist_bank_map(), at stage start). (P0 to P2.3 ran on Bunker 2; since P2.4 the heist is the
-- bank only.)
--
-- The flow:
--   the street (the van)  ->  the front double door (open)  ->  the vestibule's two blast shutters (open; LOCKDOWN on alarm: BLUE)
--   -> the banking hall (tellers, customers, guards, 2 cameras, the screen wall: the camera feeds) and the counter's flap
--   -> GREEN doors: the offices (camera)            GREEN card: the head teller OR the lobby captain (random each run)
--   -> BLUE doors: the security office (camera console) and the manager's office    BLUE card: the security chief (offices)
--   -> RED: the double door down the stairs to the vault level     RED card: the branch manager (in his own office)
--   -> plant the cracker at vault door A in the antechamber (camera): equip the door decoder and fire at the door, or press use on it
--   -> crack it (hold the zone; it jams now and then: get back in and restart it), the vault opens on a timer: grab the gold
--   -> T-15 s klaxon; at 0 the vault doors close and relock: anyone inside is trapped (alarm + a heavy squad) and re-cracks from the
--      inside. The manager's safe (GOLD card, the vault captain) and the deposit cells (GREEN / the floor key in records) hold more.
--   -> exits: A the van, B the loading dock (from inside through the staff corridor, from the alley through the padlocked shutter; from
--      the deposit hall up the service ramp and its RED shutter). Every living Bond must be in an exit (in co-op too, whatever
--      coop_exits says), and the crew must carry at least something.
-- Fail: every Bond down, or more civilians killed than the stock Frigate rule allows (Agent 4, Secret Agent 2, 00 Agent / 007 1).
-- Knock-outs never count. Score = the loot (bars and cash), +25% for a run that never went loud, minus a fine per civilian killed.
-- The best score this session is kept (HEIST_BEST) and logged ("ge: heist: HIGH SCORE"); modes have no save file.
--
-- Settings: set HEIST_SETTINGS before include("heist") (tests do), or edit S below. Every count, timer and chance is in this block.
-- Co-op: keys are per player (mode.per_player_keys), every Bond must get out, the HUD is per Bond, jams use math.random (the session's
-- generator, the same on both machines); the script reads only per-player state (bond.pos(p), bond.has_key(p, ...)), never the local player.

include("heist_lib")
include("levelgen_layout")                     -- LEVEL: the generated level's layout (include() works at the top level only)
include("heist_bank_map")                      -- heist_bank_map(): the mode's tables from LEVEL by name
include("heist_ai")                            -- HEIST_AI: the guards' hearing, awareness and barks (section 18)
pcall(include, "heist_combat")                 -- HEIST_COMBAT: the LOUD route: cover, flanks, police and SWAT (section 24; optional)
include("heist_intel")                         -- heist_intel(ctx): the INTEL board's content and the hints (section 19)
include("heist_guide")                         -- heist_guide(ctx): the first-play objective line, key strip and prompts (section 23d)
include("heist_randomiser")                    -- heist_randomiser(ctx): every run a different job (section 25; S.randomise)
pcall(include, "heist_ar")                     -- the AR specs and diagnostics (section 20; presentation only, optional)
local STAGE_LEVELGEN = 52                      -- LEVELID_LEE: the generated level's slot
local JOB = heist_job()                        -- the job definition the layout names (tools/jobs/<job>.lua; designs/job2-casino.md 6)
local JT = JOB.text
local BASE_JOB = JOB                           -- (section 25: with the randomiser on, JOB is the day's dealt copy of it)

local S = {
  -- people: the FULL force (007). Each difficulty takes its `force` share of these (difficulty below): Agent about 30 posted guards,
  -- Secret Agent 45, 00 Agent 70, 007 all 90 (100+ placed with the raisers and carriers). Most stand on post, which costs almost
  -- nothing; the vault guards appear when it opens, the waves come on top.
  guards_post     = 90,      -- posted guards (stand guard; the stock simple guard list: they hear shots, see Bond, react to bodies)
  guards_raiser   = 12,      -- alarm raisers: on sight or sound they run to the area's alarm panel and sound the alarm
  guards_vault    = 8,       -- vault guards: they wake (spawn inside, chasing) when the vault door opens
  civilians       = 18,      -- bank staff (unarmed; aim at one to make it surrender, keep aiming and it hands over what it carries)
  hearing         = 1.0,     -- guard hearing scale (silenced PP7: heard within 100 units on a first shot, 500 at its cap)
  ai              = true,    -- hearing, awareness, barks, check-ins, backup (section 18); hearing_* / ai_tune: tools/heist_ai.lua
  combat          = {},      -- the combat layer and the heat (section 24): false = stock fighting; keys: tools/heist_combat.lua
  vision          = 3000,    -- guard sight range, units (corridors here are 100-300 wide, rooms 500-1500)
  guard_weapons   = { "kf7", "kf7", "kf7", "d5k", "zmg", "dd44" },
  guard_bodies    = { "St_Petersburg_Guard", "St_Petersburg_Guard", "Russian_Soldier" },
  civ_bodies      = { "Civilian_1_Female", "Civilian_2", "Civilian_3", "Civilian_4" },
  manager_body    = "Formal_Wear",
  chief_body      = "Russian_Commandant",
  -- alarm and reinforcements
  siren_latch     = true,    -- once the alarm is up the heist is "hot" for good: the siren is re-raised whenever it times out
  wave_interval   = 25,      -- seconds between waves while hot (each wave: the difficulty's `wave` guards)
  wave_max_alive  = 16,      -- wave guards alive at once at the full force (posted guards not counted); x force
  wave_first      = 6,       -- seconds from the alarm to the first wave
  response_after  = 120,     -- seconds hot before the elite response (armoured Spetsnaz / Janus SF from both entrances)
  response_size   = 3,       -- x force (at least 1)
  response_interval = 30,
  hostile_cap     = 120,     -- no wave spawns while this many armed chrs are alive (perf: ~75 live is free, idle posts cost little)
  reserve_slots   = 12,
  lockdown        = true,    -- the alarm closes and locks the blast doors at the hall: BLUE card to pass (exit B is always open)
  -- cameras: stock (10 s of unbroken sight on Agent, 5 s above); the security console blinds every camera for cam_off_time seconds
  cam_off_time    = 90,
  -- the screen wall in the banking hall: use a screen to watch the camera feeds (cctv.view); the intel box shows in the hall or on a feed
  monitor_reach   = 170,     -- units from a screen at which a use press watches the feeds
  -- the cracker (the door decoder: equip it and fire at the vault door, or press use there)
  crack_time      = 90,      -- seconds of progress to open the vault (progress runs only while a Bond is in the zone)
  crack_radius    = 260,
  jam_every       = 15,      -- seconds of progress between jam rolls
  jam_chance      = 0.30,    -- chance of a jam at each roll
  crack_squads    = 2,       -- squads that come for the cracker even in a quiet run (one at the start, one at half way)
  crack_squad_size = 3,      -- x force (at least 1)
  vault_time      = 60,      -- seconds the vault stays open
  klaxon_at       = 15,      -- the klaxon sounds this many seconds before it closes
  recrack_time    = 45,      -- re-cracking from the inside
  heavy_squad     = 6,       -- armoured squad sent when the time lock traps a Bond inside; x force (at least 2)
  -- the time lock: closing on an EMPTY vault is quiet (no alarm: a clean run stays clean). Only a Bond trapped inside raises the alarm
  -- and brings the heavy squad. (AC delegated, Claude's call 2026-10-25: designs/bank-heist.md section 8.)
  timelock_alarm_empty = false,
  -- guards hold their fire while a bank civilian (standing, cowering or knocked out) is within ~40 units of their line of fire, short
  -- of the target; they keep aiming and shoot when it clears (AC delegated, Claude's call 2026-10-25: the bot lost most Agent runs to
  -- guards' crossfire killing the 5th civilian). The civilian-death count is unchanged.
  hold_fire       = true,
  -- loot
  bar_value       = 150000,
  money_value     = 25000,
  vault_bars      = 30,
  safe_bars       = 3,
  cell_money      = 8,
  quiet_bonus     = 0.25,    -- +25% of the loot for a run that never went loud
  civ_fine        = 100000,  -- score lost per civilian killed
  civ_limit       = nil,     -- civilians that may die; nil = the stock Frigate rule by difficulty (Agent 4, SA 2, 00/007 1)
  exit_radius     = 300,
  start_weapons   = { "pp7sil", "doordecoder" },
  start_ammo      = 0.5,     -- extra PP7 ammo, fraction of the max
  -- the banker cover (designs/bank-heist.md section 16, AC 2026-10-25): the crew starts as bank staff, the gun holstered (the slappers in
  -- hand). A guard treats a Bond as staff until something gives him away; port_heist.c keeps a suspicion meter per guard and per Bond.
  -- Instant: loot carried, a slap / a robbery / a search / a shot seen (or a shot heard). Fast: a gun in hand (0.4 s), a restricted
  -- room (1 s). Slow: a staff-only room (5 s), running close past a guard (4 s), lingering right next to one (4 s after 2 s). Full meter:
  -- "Hey!", that guard alone turns on that Bond and radios it in after cover_tune.radio (5 s) unless he is down first. The alarm or a
  -- body found ends every disguise. Cameras only time a Bond who looks wrong (a gun, loot, an act, a restricted room) and report to the
  -- security room, whose staff raise the alarm security_delay seconds later; with all of them down a camera raises nothing.
  cover           = true,
  start_hand      = "fist",  -- what the crew holds at the start ("fist" = the slappers: holstered; "pp7sil" = drawn)
  area_class      = { street = "public", hall = "public", offices = "staff", corridor = "staff", records = "staff", manager = "staff",
                      dock = "staff", security = "restricted", vault_stairs = "restricted", vault_ante = "restricted", vault = "restricted",
                      deposit = "restricted", service_ramp = "restricted", power = "restricted" },   -- by the layout's room `area`
  cover_tune      = {},      -- cover.tune{...} overrides (range 1800, cone 75, armed 0.4, restricted 1, staff 5, run 4, run_speed 450,
                             -- run_range 400, close 150, linger 2, close_time 4, decay 12, radio 5, act 1.5)
  security_delay  = 6,       -- seconds from a camera's spot to the security room's alarm (if anyone in the security wing is still up)
  -- the power (section 16): cut the main breaker in the plant room and no alarm can be raised at all (no panel, no siren, no camera:
  -- the feeds show static). An arduous job: force the cabinet, then throw the main; the work stops (and loses power_setback seconds)
  -- when the worker leaves the panel, is seen by a guard or is hurt. A backup generator restores the power generator_after seconds
  -- later unless it is disabled too, and guards come to look at the outage: one at the breaker for repair_time puts the power back. A
  -- witness who tries to raise the alarm while the power is out calls it in when the power comes back, if he is still up.
  power_cabinet   = 8,
  power_cut       = 50,
  power_setback   = 10,
  power_reach     = 150,     -- units from the breaker / the generator
  generator_after = 90,      -- 0 = no backup generator
  generator_time  = 20,
  investigators   = 2,       -- x force (at least 1); 0 = nobody comes
  investigate_after = 10,
  repair_time     = 15,
  -- guard door rules (AC's playtest 2026-10-25: guards who heard Bond in the vault walked over and opened the vault door). A guard opens
  -- a door on his route only if his role may: "any" (stock), "never", or a list of roles. A guard's role is his post's area (hall,
  -- offices, corridor, security, dock, records, vault_ante, deposit, manager), or "squad" (crack and heavy squads), "vault" (the vault
  -- guards), "wave", "response"; "barracks" is for reinforcements that pour out of a barracks door (none on this map yet: section 16).
  -- Keyed by the DOORS groups of tools/heist_bank_map.lua. A refused guard walks into the shut door and waits there.
  guard_doors     = true,
  door_access     = {
    front = "any", blast = "any",
    green = { "hall", "offices", "corridor", "security", "records", "manager", "dock", "wave", "response", "squad" },   -- GREEN: staff
    blue  = { "security", "offices", "wave", "response" },                       -- BLUE: the security staff (and the chief's offices)
    red   = { "vault_ante", "deposit", "vault", "squad", "response" },           -- RED: the vault level's guards, the squads sent down
    vault_a = "never", vault_b = "never",                                        -- the time-locked vault: nobody
    safe = "never", cell_green = "never", cell_floor = "never", dock_shutter = "never",
  },
  -- the crew's gadgets (tools/heist_gadgets.lua, designs/bank-heist.md section 21): nil = the default loadout (piton gun, lockpicks, 4 coins,
  -- 2 tape loops, 1 sleep gas, pickpocket, disguise, vents); a table overrides any of it, e.g. { coins = 0, gas = 0 } or { piton = false }
  gadgets         = nil,
  -- the INTEL board (section 19; Tab / Back): the crew's plan, schematics, keys, personnel and notes. Presentation only.
  intel           = true,    -- false: no board at all (the determinism proof compares the two)
  hints           = true,    -- one-line contextual hints (a keyed door, the vault door, the breaker, the console, the screens)
  briefing        = true,    -- open the board once at the start ("press INTEL to close"; skipped in automated runs)
  noise_rings     = false,   -- the AR specs' noise rings: a ripple on the floor for every noise (steps, doors, slaps, shots); off for now
  spawn_per_tick  = 24,      -- chrs placed per 0.25 s at the start (no hitch)
  test            = false,   -- test hooks on (HEIST.* below are always there; this only adds log lines)
  ar              = true,    -- the Q-Branch specs (tools/heist_ar.lua, section 20): suspicion "?" / "!", bubbles, key brackets, waypoints
  interlock       = true,    -- the mantrap and sally-port interlocks (tools/heist_interlock.lua, section 29); a table tunes them
  -- the randomiser (tools/heist_randomiser.lua, section 25): who carries which card (within the job's deck, JOB.random), dummy cameras,
  -- the time lock, the roster, sealed routes, the job of the day; from the sim RNG (same seed, same deal). false = the job exactly as
  -- written. A script that sets HEIST_SETTINGS without this key (the tests) gets the fixed job. randomiser = { ... } tunes it.
  randomise       = true,
  randomiser      = nil,
  debrief         = true,    -- the end-of-heist DEBRIEF (tools/heist_debrief.lua, section 26): rating, payout, stats, commendations, records
  planning        = true,    -- the PLANNING board before the clock starts (tools/heist_planning.lua, section 27); off in automated runs
  -- guard personalities (tools/heist_traits.lua, section 32): rookies, veterans, by-the-book, bribable guards, dealt from the sim RNG.
  -- false = uniform guards exactly as before. A script that sets HEIST_SETTINGS without this key (the tests) gets them off.
  personalities   = true,
  bribe_cost      = 50000,   -- a bribe (USE face to face with a bribable guard who suspects nothing) comes off the crew's payout
  -- difficulty (mission.difficulty(): 0 Agent .. 3 007). force: the share of the full force above (posted guards, raisers, vault
  -- guards, wave cap, squads, the response); acc: the guards' accuracy rating (0 = stock, 5 = 1.5x, 10 = 2x: the stock aim is scaled by
  -- 1 + acc / 10); wave: guards per wave. (AC delegated, Claude's call 2026-10-25, revisit: the player bot won 10/10 with the carriers
  -- only, 8/10 with 20 posted, 0/10 with 45, so Agent is ~30 posted at stock aim and should be clearly winnable by a careful human.)
  difficulty = {
    [0] = { force = 0.334, acc = 0,  hearing = 0.8,  wave = 2, civ_limit = 4 },   -- Agent: ~30 posted, stock accuracy
    [1] = { force = 0.5,   acc = 5,  hearing = 1.0,  wave = 3, civ_limit = 2 },   -- Secret Agent: ~45, 1.5x
    [2] = { force = 0.778, acc = 10, hearing = 1.25, wave = 4, civ_limit = 1 },   -- 00 Agent: ~70, 2x
    [3] = { force = 1.0,   acc = 10, hearing = 1.5,  wave = 4, civ_limit = 1 },   -- 007: the full force (100+ placed), 2x
  },
}
for k, v in pairs(JOB.settings or {}) do S[k] = v end   -- the job's own settings (the bank has none), under a test's
if type(HEIST_SETTINGS) == "table" then
  for k, v in pairs(HEIST_SETTINGS) do S[k] = v end
  if HEIST_SETTINGS.randomise == nil then S.randomise = false end   -- (section 25: the tests opt in)
  if HEIST_SETTINGS.personalities == nil then S.personalities = false end   -- (section 32: the tests opt in)
end
local DIFF = S.difficulty

-- cards (key flags). Per player: a card opens doors only for the Bond who picked it up.
local GREEN, BLUE, RED, GOLD, FLOORKEY, VAULT = 0x2, 0x4, 0x8, 0x10, 0x1, 0x40000000
local CARD_NAMES = { { GREEN, "GREEN" }, { BLUE, "BLUE" }, { RED, "RED" }, { GOLD, "GOLD" } }

-- the map: filled at stage start from heist_bank_map() (tools/heist_bank_map.lua; the shapes are documented there): doors are setup
-- indices, pads are pad ids. DOORS, AREAS (name, pads, alarm panel pad, posted share, raisers), AREA_LABEL, VAULT_ROOMS, VAULT_PADS,
-- VAULT_CRATE_PADS, CIV_PLACES (area, pads, count), P (start, exits, crack, monitors, wave / squad pads, loot ...), CARRIERS (the card
-- carriers' candidate pads), CAM_LABEL / CAM_AREA (by camera index), GUARD_AREAS (the areas a camera covers), MAP (labels), intel_room.
local DOORS, AREAS, AREA_LABEL, VAULT_ROOMS, VAULT_PADS, VAULT_CRATE_PADS, CIV_PLACES, P, CARRIERS
local CAM_LABEL, CAM_AREA, GUARD_AREAS, MAP, intel_room = {}, {}, {}, { name = "none" }, nil
local OFFS = { { 0, 0 }, { 110, 0 }, { -110, 0 }, { 0, 110 }, { 0, -110 }, { 110, 110 }, { -110, -110 }, { 110, -110 }, { -110, 110 },
               { 200, 0 }, { -200, 0 }, { 0, 200 }, { 0, -200 } }

-- ------------------------------------------------------------------------------------------------------------------------------------
-- state (HEIST is the test window: the win-path test reads it and calls the hooks)

HEIST_BEST = HEIST_BEST or 0     -- the best score this session (Lua state survives stage reloads; there is no save API for modes)

local H = {
  phase = "setup",     -- setup, casing, cracking, jammed, open, closing (T-klaxon), sealed, trapped, recracking, escaped
  hot = false, hot_at = nil, elapsed = 0, ended = false, ready = false,
  loot = 0, bars = 0, cash = 0, carried = {},
  progress = 0, recrack = false, crack_by = nil, jams = 0, next_jam = 0,
  vault_left = nil, opened_once = false, klaxon = false,
  carriers = {}, guards = {}, civs = {}, loot_items = {}, zones = {},
  placed = { post = 0, raiser = 0, vault = 0, carrier = 0, civ = 0 },
  cam_off_until = nil, flash = nil, flash_until = 0, civ_limit = 2, trapped = {},
  squads = 0,
  pflash = {},         -- per Bond: { text, until } (the cover's "Hey!" is his alone)
  report = nil,        -- a camera's spot on its way through the security room: { at, cam, p }
  blown = 0, reports = 0,
}
HEIST = H
H.S = S                -- (the settings, for tools/heist_gadgets.lua)
-- the power (section 16): out, the cabinet step, the work under way, the generator, the investigators, witnesses waiting for the mains
local PW = { out = false, step = 0, task = nil, by = nil, prog = 0, saved = { cabinet = 0, breaker = 0, generator = 0 }, hp = nil,
             cut_at = nil, gen_off = false, invest_at = nil, invest = {}, repair = 0, pending = {}, cuts = 0 }
H.power = PW

local diff = DIFF[1]
-- a full-force count at this difficulty's share (never below `min`)
local function scaled(n, min) return math.max(min or 0, math.floor(n * (diff.force or 1) + 0.5)) end
local queue = {}            -- spawn jobs, placed spawn_per_tick per tick
local waves, response
local function d2(ax, az, bx, bz) local dx, dz = ax - bx, az - bz; return math.sqrt(dx * dx + dz * dz) end
local function pick(t) return t[math.random(#t)] end
local function money(v)
  local s = tostring(math.floor(v))
  local out = s:reverse():gsub("(%d%d%d)", "%1,"):reverse()
  if out:sub(1, 1) == "," then out = out:sub(2) end
  return "$" .. out
end
local function log(fmt, ...) print(string.format("ge: heist: " .. fmt, ...)) end
local function flash(text, secs) H.flash = text; H.flash_until = H.elapsed + (secs or 4) end
local function flash_p(p, text, secs) H.pflash[p] = { text, H.elapsed + (secs or 4) } end

-- ------------------------------------------------------------------------------------------------------------------------------------
-- spawning helpers

local function spawn_at(pad, opts)
  for k = 1, #OFFS do
    local o = {}
    for kk, v in pairs(opts) do o[kk] = v end
    if OFFS[k][1] ~= 0 or OFFS[k][2] ~= 0 then o.offset = { OFFS[k][1], OFFS[k][2] } end
    local c = spawn.chr(pad, o)
    if c then return c end
  end
  return nil
end

local function guard_opts(extra)
  local o = { weapon = pick(S.guard_weapons), body = pick(S.guard_bodies), hearing = S.hearing * diff.hearing, vision = S.vision,
              accuracy = diff.acc }
  for k, v in pairs(extra or {}) do o[k] = v end
  return o
end

local function face_for(area, pad)
  local other = area.pads[math.random(#area.pads)]
  if other == pad then other = area.pads[(math.random(#area.pads) % #area.pads) + 1] end
  local x, _, z = pads.pos(other)
  return x and { x, z } or nil
end

local function note_guard(c, kind, area)
  H.guards[#H.guards + 1] = { chr = c, kind = kind, area = area }
  H.placed[kind] = (H.placed[kind] or 0) + 1
  -- the bank's own guards judge the disguise (squads, waves and the vault guards come for a known crew: they do not)
  if S.cover and (kind == "post" or kind == "raiser" or kind == "carrier" or kind == "investigator") then
    cover.guard(c, area == "security" and "security" or "guard")
  end
  -- the door rules: which keyed doors this guard may open on his way (S.door_access)
  spawn.role(c, (kind == "squad" and "squad") or (kind == "vault" and "vault") or area)
  HEIST_AI.guard(c, kind)
  if HEIST_COMBAT then HEIST_COMBAT.guard(c, kind) end
end

-- ------------------------------------------------------------------------------------------------------------------------------------
-- doors

local function set_doors(list, flags)
  for _, d in ipairs(list) do
    door.one_way(d, false)
    door.set_keyflags(d, flags)
  end
end

local function setup_doors()
  set_doors(DOORS.front, 0)
  set_doors(DOORS.blast, 0)
  set_doors(DOORS.green, GREEN)
  set_doors(DOORS.blue, BLUE)
  set_doors(DOORS.red, RED)
  set_doors(DOORS.vault_a, VAULT)
  set_doors(DOORS.vault_b, VAULT)
  door.set_keyflags(DOORS.safe, GOLD)
  door.set_keyflags(DOORS.cell_green, GREEN)
  door.set_keyflags(DOORS.cell_floor, FLOORKEY)
  -- who among the guards may open which door (S.door_access by DOORS group; a double door is both leaves)
  mode.guard_doors(S.guard_doors and true or false)
  if S.guard_doors then
    for group, rule in pairs(S.door_access) do
      local list = DOORS[group]
      if type(list) == "number" then list = { list } end
      for _, d in ipairs(list or {}) do door.guard_access(d, rule) end
    end
    -- bank v2: a door the layout tags itself (LEVEL.objects[name].guard_access: "any" | "never" | "role:a,b" | "barracks") takes that
    -- rule instead (both leaves of a double door); the bank v2 layout tags every door (designs/bank-heist.md section 17b)
    for _, o in pairs(LEVEL.objects or {}) do
      local g = o.guard_access
      if g and (o.type == "door" or o.type == "safe") then
        local rule = g
        if g:sub(1, 5) == "role:" then
          rule = {}
          for r in g:sub(6):gmatch("[^,]+") do rule[#rule + 1] = r end
        elseif g == "barracks" then
          rule = { "barracks" }
        end
        door.guard_access(o.index, rule)
        if o.pair then door.guard_access(o.pair, rule) end
      end
    end
  end
end

local vault_want, vault_since = nil, 0
local function vault_doors(open)
  vault_want, vault_since = open, H.elapsed
  for _, list in ipairs({ DOORS.vault_a, DOORS.vault_b }) do
    for _, d in ipairs(list) do
      if open then door.open(d) else door.close(d) end
    end
  end
end

-- every tick: keep the vault doors the way the vault wants them. They would auto-close, and a chr in the doorway stops a moving door,
-- so 1.5 s after the order the time lock snaps a door that has not got there (door.snap).
local function hold_vault_doors(now)
  if vault_want == nil then return end
  local late = now - vault_since > 1.5
  for _, list in ipairs({ DOORS.vault_a, DOORS.vault_b }) do
    for _, d in ipairs(list) do
      local g = door.get(d)
      if g then
        if vault_want then
          if late and g.open < 0.5 then door.snap(d, true)
          elseif g.state == "closing" or g.state == "closed" then door.open(d) end
        elseif g.open > 0.02 then
          if late then door.snap(d, false)
          elseif g.state == "opening" or g.state == "open" then door.close(d) end
        end
      end
    end
  end
end

-- ------------------------------------------------------------------------------------------------------------------------------------
-- HUD

local function cards_text(p)
  local t = {}
  for _, c in ipairs(CARD_NAMES) do
    if bond.has_key(p, c[1]) then t[#t + 1] = c[2]:sub(1, 1) else t[#t + 1] = "-" end
  end
  return table.concat(t)
end

local function objective(p)
  local ph = H.phase
  if ph == "casing" then
    if bond.has_key(p, RED) then return JT.crack .. MAP.vault_hint .. ")" end
    if bond.has_key(p, BLUE) then return JT.need_red end
    if bond.has_key(p, GREEN) then return JT.need_blue end
    return JT.need_green
  elseif ph == "cracking" then return string.format("CRACKING %d%%  stay close", math.floor(H.progress * 100))
  elseif ph == "jammed" then return "CRACKER JAMMED: get in the zone, restart it"
  elseif ph == "open" or ph == "closing" then return JT.grab
  elseif ph == "trapped" then
    if H.trapped[p] then return "TRAPPED: re-crack the vault door from inside" end
    return "Your partner is trapped in the vault"
  elseif ph == "recracking" then return string.format("RE-CRACKING %d%%", math.floor(H.progress * 100))
  end
  return JT.get_out
end

local function refresh_hud()
  local status = H.hot and "ALARM" or "quiet"
  if PW.out then status = status .. " POWER OUT"
  elseif H.cam_off_until and H.elapsed < H.cam_off_until then status = status .. " CAMS OFF" end
  for p = 0, bond.count() - 1 do
    if cctv.viewing(p) then
      -- on the camera feeds: only the feed's overlay and the intel box (the mode's lines would sit over the picture)
      hud.set(1, "", p); hud.set(2, "", p); hud.set(3, "", p)
    else
      local gd = H.guide                                  -- the first-play guide (tools/heist_guide.lua): presentation only
      local l1 = string.format("%s %s  %d bars  %s  [%s]", JT.hud_title, money(H.loot), H.bars, status, cards_text(p))
      hud.set(1, gd and gd.tag(l1) or l1, p)
      hud.set(2, gd and gd.objective(p) or objective(p), p)
      local pf = H.pflash[p]
      if pf and H.elapsed < pf[2] then hud.set(3, pf[1], p)
      elseif H.flash and H.elapsed < H.flash_until then hud.set(3, H.flash, p) else hud.set(3, gd and gd.line3(p) or "", p) end
    end
  end
end
H.refresh_hud = refresh_hud

-- ------------------------------------------------------------------------------------------------------------------------------------
-- alarm, waves

local function hostiles_alive()
  local n = 0
  for _, g in ipairs(H.guards) do
    if spawn.alive(g.chr) and not spawn.down(g.chr) then n = n + 1 end
  end
  return n + (waves and waves.alive() or 0) + (response and response.alive() or 0)
end

local function lockdown()
  if not S.lockdown then return end
  for _, d in ipairs(DOORS.blast) do door.close(d); door.set_keyflags(d, BLUE) end
  log("lockdown: blast doors closed, BLUE card to pass")
end

local function go_hot(why)
  if H.hot or H.ended then return end
  H.hot = true
  H.hot_at = H.elapsed
  log("ALARM (%s) at %.0f s: hot", why, H.elapsed)
  flash(PW.out and "ALARM! (no siren: the power is out) Guards are on their way" or JT.alarm_flash, 6)
  lockdown()
  if S.cover then cover.alert(true) end   -- the alarm ends every disguise
  if not alarm.active() then alarm.start() end   -- (does nothing while the power is out; the latch sounds it when it comes back)
end

on("alarm", function(a)
  if a == 1 and not H.klaxon then go_hot("siren") end
end)

-- the security room's staff: the guards posted in the security wing (the camera link needs one of them up)
local function security_up()
  local n = 0
  for _, g in ipairs(H.guards) do
    if g.area == "security" and (g.kind == "post" or g.kind == "raiser") and spawn.alive(g.chr) and not spawn.down(g.chr) then n = n + 1 end
  end
  return n
end
H.security_up = security_up

on("cctv_spotted", function(cam, p)
  log("camera %d spotted Bond %d", cam, p)
  if not S.cover then go_hot("camera"); return end   -- (the stock alarm also goes off; this catches a spot while the klaxon sounds)
  -- the cover: a camera reports to the security room; its staff raise the alarm security_delay seconds later
  if H.hot or H.ended or H.report then return end
  if security_up() == 0 then
    log("camera %d saw Bond %d, but nobody in the security room is up to raise it", cam, p)
    flash_p(p, "A camera saw you - but nobody is watching the screens", 3)
    return
  end
  H.report = { at = H.elapsed + S.security_delay, cam = cam, p = p }
  log("camera %d saw Bond %d: the security room will raise the alarm in %d s", cam, p, S.security_delay)
  flash_p(p, "A camera saw you! The security room will raise the alarm", 4)
  for _, g in ipairs(H.guards) do   -- the relay is heard from the security wing (a burst of radio)
    if g.area == "security" and spawn.alive(g.chr) and not spawn.down(g.chr) then cover.radio(g.chr, "relay"); break end
  end
end)

local function security_tick()
  local r = H.report
  if not r or H.elapsed < r.at then return end
  H.report = nil
  if H.hot or H.ended then return end
  if PW.out then log("the security room's camera report died with the power"); return end
  if security_up() == 0 then log("the security room went quiet before it raised the alarm (camera %d)", r.cam); return end
  go_hot(string.format("the security room saw camera %d", r.cam))
end

on("body_found", function(g, b, k)
  if not H.hot then flash(k == 1 and "A guard found someone out cold - everyone is on alert" or "A guard found a body - everyone is on alert", 4) end
  if S.cover and not cover.alert() then cover.alert(true); log("body %d found by guard %d: everyone alert, no more cover", b, g) end
end)

-- the cover (section 16): a guard saw through a Bond; still up after the radio time, he reports it
local WHY = { armed = "your gun", loot = "the loot", act = "what you did", restricted = "off limits", staff = "no business here",
              running = "running", close = "loitering", heard = "the shot", hurt = "you hit him", script = "he knows" }
on("cover_blown", function(g, p, why)
  local w = cover.why_name(why)
  H.blown = H.blown + 1
  log("cover blown: guard %d saw through Bond %d (%s)", g, p, w)
  if not H.hot then flash_p(p, "HEY! Seen through (" .. (WHY[w] or w) .. "): drop him before he radios", 5) end
end)

on("cover_report", function(g, p)
  if H.hot or H.ended then return end
  H.reports = H.reports + 1
  if PW.out then
    PW.pending[#PW.pending + 1] = g
    log("guard %d tried to raise the alarm on Bond %d: the power is out", g, p)
    flash_p(p, "He's at the alarm - but the power is out", 4)
    return
  end
  go_hot(string.format("guard %d raised the alarm on Bond %d", g, p))
end)

-- ------------------------------------------------------------------------------------------------------------------------------------
-- setup: guards, staff, carriers, loot

local function area_by_name(n) for _, a in ipairs(AREAS) do if a.name == n then return a end end end

-- the pads the card carriers took (queue_carriers runs first): the posts, raisers and staff placed after them skip those pads, so a
-- carrier stands alone where the board cases him. Before (GE_SEED 5), a posted guard landed 110 units off the security chief (spawn_at's
-- offset), and every pickpocket from behind the chief was seen by him
local carrier_pads = {}
-- (only a chr whose pad a carrier took moves, to the list's next pad: renumbering the whole list moved every post after it, and the
-- layout-bound checks of seeds 12-14 moved with them)
local function clear_of_carriers(list, i)
  for k = 0, #list - 1 do
    local p = list[(i - 1 + k) % #list + 1]
    if not carrier_pads[p] then return p end
  end
  return list[(i - 1) % #list + 1]
end

local function queue_area_guards()
  local total_share = 0
  for _, a in ipairs(AREAS) do total_share = total_share + a.share end
  local posts = scaled(S.guards_post, 0)
  local raisers = scaled(S.guards_raiser, 0)
  H.want = { post = posts, raiser = raisers }
  local raiser_left = raisers
  for _, a in ipairs(AREAS) do
    local n = math.floor(posts * a.share / total_share + 0.5)
    for k = 1, n do
      queue[#queue + 1] = function()
        local pad = clear_of_carriers(a.pads, k)
        local c = spawn_at(pad, guard_opts({ post = face_for(a, pad) or true }))
        if c then note_guard(c, "post", a.name) end
      end
    end
    local r = math.min(a.raisers, raiser_left)
    raiser_left = raiser_left - r
    for k = 1, r do
      queue[#queue + 1] = function()
        local pad = clear_of_carriers(a.pads, (k * 3) % #a.pads + 1)
        local c = spawn_at(pad, guard_opts({ behaviour = "hold", alarm_pad = a.panel }))
        if c then note_guard(c, "raiser", a.name) end
      end
    end
  end
end

local function queue_civilians()
  local n = math.floor(S.civilians + 0.5)
  local made = 0
  while made < n do
    local before = made
    for _, place in ipairs(CIV_PLACES) do
      for k = 1, place[3] do
        if made >= n then break end
        made = made + 1
        local list, i = place[2], (k - 1) % #place[2] + 1
        local area = place[1]
        queue[#queue + 1] = function()
          local c = spawn_at(clear_of_carriers(list, i), { behaviour = "civilian", body = pick(S.civ_bodies) })
          if c then H.civs[#H.civs + 1] = { chr = c, area = area }; H.placed.civ = H.placed.civ + 1 end
        end
      end
    end
    if made == before then break end
  end
end

local function give_card(c, flags, role, area, pad)
  local h = c and spawn.key(c, flags) or nil
  H.carriers[role] = { chr = c, key = h, flags = flags, area = area, pad = pad }
  carrier_pads[pad] = true
  log("card %s (0x%x) on %s chr %s at pad %d (%s)", role, flags, role, tostring(c), pad, area)
end

-- the card ladder from the job (JOB.cards; the bank: GREEN the head teller or the lobby captain, chosen at random; BLUE the security
-- chief in the offices; RED the branch manager in his own office behind BLUE; GOLD the vault captain inside the vault)
local KEYS = { GREEN = GREEN, BLUE = BLUE, RED = RED, GOLD = GOLD, FLOORKEY = FLOORKEY }
local function place_carrier(card, d)
  local area = d.area == "@manager" and MAP.manager_area or d.area
  local body = d.body_setting and S[d.body_setting] or d.body
  local pad = pick(CARRIERS[d.pads])
  local c
  if d.kind == "floor" then   -- (section 25: a card lying on its pad, no carrier)
    H.carriers[d.role] = { key = spawn.key_at(pad, KEYS[card.key]), flags = KEYS[card.key], area = area, pad = pad, floor = true }
    carrier_pads[pad] = true
    log("card %s (0x%x) lies at pad %d (%s)", d.role, KEYS[card.key], pad, area)
    return
  elseif d.kind == "civ" then
    c = spawn_at(pad, { behaviour = "civilian", body = body })
    if c then H.civs[#H.civs + 1] = { chr = c, area = area }; H.placed.civ = H.placed.civ + 1 end
  else
    c = spawn_at(pad, guard_opts({ post = true, body = body, health = d.health, armour = d.armour }))
    if c then note_guard(c, "carrier", area) end
  end
  give_card(c, KEYS[card.key], d.role, area, pad)
end
local function queue_carriers()
  queue[#queue + 1] = function()
    carrier_pads = {}
    for _, card in ipairs(JOB.cards) do
      if card.either then place_carrier(card, card.either[math.random(#card.either)]) else place_carrier(card, card) end
    end
  end
end

local LOOT_OFFS = { { 0, 0 }, { 30, 0 }, { 0, 30 }, { -30, 0 }, { 0, -30 }, { 30, 30 } }
local CRATE_OFFS = { { 120, 0 }, { -120, 0 }, { 0, 120 }, { 0, -120 } }

local function place_loot(pad, kind, offs, where)
  for _, o in ipairs(offs) do
    local h = spawn.item(pad, kind, { offset = o })
    if h then
      local x, z = spawn.item_pos(h)
      H.loot_items[#H.loot_items + 1] = { h = h, pad = pad, dx = o[1], dz = o[2], kind = kind, where = where, x = x, z = z }
      return h
    end
  end
  return nil
end

local function queue_loot()
  queue[#queue + 1] = function()
    local n, k = 0, 0
    local list = P.vault_loot or VAULT_PADS   -- the bank: its named loot pads (LEVEL.loot.vault_gold), stacks of bars round each
    while n < S.vault_bars and k < S.vault_bars * 4 do
      k = k + 1
      local pad = list[(k - 1) % #list + 1]
      local ring = math.floor((k - 1) / #list) + 1
      local offs
      if VAULT_CRATE_PADS[pad] then offs = { CRATE_OFFS[(ring - 1) % #CRATE_OFFS + 1] } else offs = { LOOT_OFFS[(ring - 1) % #LOOT_OFFS + 1] } end
      if place_loot(pad, "goldbar", offs, "vault") then n = n + 1 end
    end
    local m = 0
    for ci, list in ipairs(P.cell_loot) do
      for j = 1, math.ceil(S.cell_money / 2) do
        if m >= S.cell_money then break end
        local pad = list[(j - 1) % #list + 1]
        if place_loot(pad, "money", { LOOT_OFFS[math.floor((j - 1) / #list) % #LOOT_OFFS + 1] }, ci == 1 and "cell_green" or "cell_floor") then m = m + 1 end
      end
    end
    log("loot placed: %d gold bars in the vault, %d cash bundles in the cells (%d in all)", n, m, #H.loot_items)
  end
end

local safe_done = false
local function safe_tick()
  if safe_done then return end
  local d = door.get(DOORS.safe)
  if d and d.open > 0.3 then
    safe_done = true
    local n = 0
    for i = 1, S.safe_bars do
      if place_loot(P.safe_loot[(i - 1) % #P.safe_loot + 1], "goldbar", { LOOT_OFFS[(i - 1) % #LOOT_OFFS + 1] }, "safe") then n = n + 1 end
    end
    log(JT.safe_log, n)
    flash(JT.safe_open, 4)
  end
end

-- ------------------------------------------------------------------------------------------------------------------------------------
-- the cracker and the vault

local function in_circle(p, c, r)
  local x, _, z = bond.pos(p)
  return x ~= nil and not bond.dead(p) and d2(x, z, c[1], c[2]) <= r
end
local function in_crack_zone(p) return in_circle(p, P.crack_xz, S.crack_radius) end
local function in_inside_zone(p)
  for _, c in ipairs(P.inside) do if in_circle(p, c, S.crack_radius) then return true end end
  return false
end
local function in_vault(p)   -- by room (Bunker 2's circle test once missed a corner and sealed a Bond in for good)
  return not bond.dead(p) and VAULT_ROOMS[bond.room(p)] == true
end
H.in_vault = in_vault
local function anyone(fn) for p = 0, bond.count() - 1 do if fn(p) then return true end end return false end

local function spawn_squad(list, n, extra, label)
  local made = 0
  for i = 1, n do
    local c = spawn_at(list[(i - 1) % #list + 1], guard_opts(extra))
    if c then note_guard(c, "squad", label); made = made + 1 end
  end
  H.squads = H.squads + 1
  log("%s: %d sent", label, made)
  return made
end

local function crack_start(p, recrack)
  H.phase = recrack and "recracking" or "cracking"
  H.recrack = recrack
  H.progress = 0
  H.next_jam = S.jam_every
  H.crack_by = p
  local secs = recrack and S.recrack_time or S.crack_time
  hud.countdown(secs)
  log("%s started by Bond %d (%d s)", recrack and "re-crack" or "crack", p, secs)
  if recrack then
    flash("Re-cracking: hold on, the heavy squad is coming!", 5)
    go_hot("re-crack")   -- (already hot: the time lock raised it when it trapped the crew)
  else
    flash("The cracker is on the door: stay with it!", 4)
    if S.crack_squads > 0 then spawn_squad(P.squad, scaled(S.crack_squad_size, 1), { behaviour = "chase" }, "crack squad 1") end
  end
end

local function crack_resume(p)
  H.phase = H.recrack and "recracking" or "cracking"
  local secs = (H.recrack and S.recrack_time or S.crack_time) * (1 - H.progress)
  hud.countdown(secs)
  log("cracker restarted by Bond %d at %.0f%%", p, H.progress * 100)
  flash("Cracker restarted", 3)
end

function H.force_jam()
  if H.phase ~= "cracking" and H.phase ~= "recracking" then return false end
  H.phase = "jammed"
  H.jams = H.jams + 1
  hud.countdown(nil)
  log("the cracker JAMMED at %.0f%% (jam %d)", H.progress * 100, H.jams)
  flash("CRACKER JAMMED! Get in the zone and restart it", 5)
  return true
end

local function vault_open_now()
  H.phase = "open"
  H.opened_once = true
  H.vault_left = S.vault_time
  vault_doors(true)
  hud.countdown(S.vault_time)
  log("VAULT OPEN: %d s", S.vault_time)
  flash(JT.vault_open, 5)
  local n = scaled(S.guards_vault, 0)
  for i = 1, n do
    local c = spawn_at(VAULT_PADS[(i * 5) % #VAULT_PADS + 1], guard_opts({ behaviour = "chase" }))
    if c then note_guard(c, "vault", "vault") end
  end
end

local function crack_done()
  hud.countdown(nil)
  if H.recrack then
    H.phase = "escaped"
    H.trapped = {}
    vault_doors(true)
    log("re-crack done: the vault doors are open for good")
    flash("You're out! Now get out of the bank", 5)
  else
    vault_open_now()
  end
end

local function crack_tick(dt)
  local ph = H.phase
  if ph ~= "cracking" and ph ~= "recracking" then return end
  local zone_ok = H.recrack and anyone(in_inside_zone) or anyone(in_crack_zone)
  if not zone_ok then
    hud.countdown(nil)   -- the clock stops while nobody holds the zone; it comes back with the time left
    H.paused = true
    return
  end
  if H.paused then
    H.paused = false
    hud.countdown((H.recrack and S.recrack_time or S.crack_time) * (1 - H.progress))
  end
  local total = H.recrack and S.recrack_time or S.crack_time
  H.progress = math.min(1, H.progress + dt / total)
  local secs_done = H.progress * total
  if not H.recrack and S.crack_squads > 1 and not H.half_squad and H.progress >= 0.5 then
    H.half_squad = true
    spawn_squad(P.squad, scaled(S.crack_squad_size, 1), { behaviour = "chase" }, "crack squad 2")
  end
  if H.progress >= 1 then crack_done(); return end
  if secs_done >= H.next_jam then
    H.next_jam = H.next_jam + S.jam_every
    if S.jam_chance > 0 and math.random() < S.jam_chance then H.force_jam() end
  end
end

-- a Bond fires the decoder (gadget_used) or presses use: start / restart the cracker if he is where it goes
local function crack_action(p, how)
  if H.ended then return end
  local ph = H.phase
  if ph == "casing" and in_crack_zone(p) then
    crack_start(p, false)   -- the antechamber is behind the RED doors: being here is the card check
  elseif ph == "jammed" then
    if (H.recrack and in_inside_zone(p)) or (not H.recrack and in_crack_zone(p)) then crack_resume(p) end
  elseif ph == "trapped" and H.trapped[p] and in_inside_zone(p) then
    crack_start(p, true)
  end
end

on("gadget_used", function(p, item, idx, objtype)
  if item == 38 or item == 44 then crack_action(p, "gadget") end   -- door decoder, safe cracker case
end)

-- ------------------------------------------------------------------------------------------------------------------------------------
-- the camera feeds (designs/bank-heist.md P1 T2) and the control-room intel (P1 T1)

H.cctv_left = {}       -- per Bond: when he last left a feed (a held use press must not re-open it)
H.cctv_last = {}       -- per Bond: the camera he watched last (the screens reopen on it)
H.seen = {}            -- card role -> { area, at } where a working camera last saw its carrier

local function near_monitor(p)
  local x, _, z = bond.pos(p)
  if x == nil or bond.room(p) ~= intel_room then return false end
  for _, m in ipairs(P.monitors) do
    if d2(x, z, m[1], m[2]) <= S.monitor_reach then return true end
  end
  return false
end
H.near_monitor = near_monitor

function H.watch(p, cam)
  local list = cctv.list()
  cam = cam or H.cctv_last[p] or list[1]
  if not cam or not cctv.view(p, cam) then return false end
  return true
end

on("cctv_view", function(p, cam, why)
  if cam >= 0 then
    H.cctv_last[p] = cam
    if S.test then log("Bond %d watches camera %d (%s, why %d)", p, cam, cctv.name(cam), why) end
  else
    H.cctv_left[p] = H.elapsed
    if why == 3 then flash("Hit! You look up from the screens", 3) end
    if S.test then log("Bond %d left the feeds (why %d)", p, why) end
  end
  if not H.ended then refresh_hud() end   -- at once: the mode's lines go while he watches and come back when he leaves
end)

on("use", function(p, kind, idx, objtype)
  if idx < 0 or objtype == 3 then H.power_use(p) end   -- the breaker / the generator (plain props: a use press there has no target)
  for _, d in ipairs(DOORS.vault_a) do if idx == d then crack_action(p, "use") end end
  for _, d in ipairs(DOORS.vault_b) do if idx == d then crack_action(p, "use") end end
  -- the security console: blinds every camera for cam_off_time seconds and shows who carries the cards
  -- once per activation: a use press while the cameras are already off does nothing (it used to restart the switch every press)
  if bond.has_key(p, BLUE) and H.console_zone and zone.contains(H.console_zone, bond.id(p))
     and not (H.cam_off_until and H.elapsed < H.cam_off_until) then H.camera_switch(p) end
  -- the hall's screens: watch the camera feeds (a press of the use action at a screen; MONITOR / MULTI_MONITOR / RACK or nothing in
  -- reach). Only the use action: B is also reload, and a reload next to the screens must not open them (B still leaves the feeds).
  if (idx < 0 or objtype == 10 or objtype == 11 or objtype == 12) and bond.use_key(p) and not cctv.viewing(p) and near_monitor(p)
     and H.elapsed - (H.cctv_left[p] or -100) > 0.75 then
    H.watch(p)
  end
end)

-- the floor rule of the INTEL board's feed scan (tools/heist_intel.lua): cctv.sees tests the cone and the line in x/z only, so a camera
-- "sees" a chr on the floor above or below; a sighting counts only with the chr's room on the camera's own floor (the layout's levels)
local cam_floor_of, room_floor_of
local function floors_build()
  local heights, levels = {}, {}
  for _, r in pairs(LEVEL.rooms) do if r.area ~= "street" then heights[r.floor] = (heights[r.floor] or 0) + 1 end end
  for y, n in pairs(heights) do if n >= 2 then levels[#levels + 1] = y end end
  table.sort(levels)
  local ground = 1
  for i, y in ipairs(levels) do if y == 0 then ground = i end end
  local function level_of(y)
    local best = 1
    for i, ly in ipairs(levels) do if y >= ly - 10 then best = i end end
    return best
  end
  local function room_level(r) return r.area == "street" and ground or level_of(r.floor) end
  cam_floor_of, room_floor_of = {}, {}
  for _, r in pairs(LEVEL.rooms) do room_floor_of[r.id] = room_level(r) end
  for _, o in pairs(LEVEL.objects) do
    if o.type == "camera" then
      local r = LEVEL.rooms[o.room]
      cam_floor_of[o.index] = r and room_level(r) or level_of(o.y or 0)
    end
  end
end
local function cam_sees_on_floor(cam, chr)
  if not cctv.sees(cam, chr) then return false end
  if not cam_floor_of then floors_build() end
  local r = spawn.room(chr)
  return r ~= nil and cam_floor_of[cam] ~= nil and room_floor_of[r] == cam_floor_of[cam]
end
H.cam_sees_on_floor = cam_sees_on_floor

-- every 0.5 s: where the working cameras see the card carriers now (6 cameras x 5 carriers)
local function intel_scan()
  for role, c in pairs(H.carriers) do
    if c.chr and spawn.alive(c.chr) then
      for _, cam in ipairs(cctv.list()) do
        if cam_sees_on_floor(cam, c.chr) then H.seen[role] = { area = CAM_AREA[cam] or c.area, at = H.elapsed }; break end
      end
    end
  end
end

local CARD_ROLES = { { GREEN, "GREEN" }, { BLUE, "BLUE" }, { RED, "RED" }, { GOLD, "GOLD" } }

local function cams_up()
  local up, n = 0, 0
  local covered = {}
  for _, cam in ipairs(cctv.list()) do
    n = n + 1
    local g = cctv.get(cam)
    if g and not g.disabled then up = up + 1; covered[CAM_AREA[cam] or "?"] = true end
  end
  return up, n, covered
end

local function vault_text()
  local ph = H.phase
  if ph == "cracking" or ph == "recracking" then return string.format("%s %d%%", ph == "recracking" and "RE-CRACK" or "CRACKING", math.floor(H.progress * 100))
  elseif ph == "jammed" then return "CRACKER JAMMED"
  elseif ph == "open" or ph == "closing" then return string.format("OPEN %ds", math.max(0, math.floor(H.vault_left or 0)))
  elseif ph == "trapped" then return "SEALED - crew inside"
  elseif ph == "sealed" or ph == "escaped" then return "sealed"
  end
  return "time-locked"
end

function H.intel_lines(p)
  local up, n, covered = cams_up()
  local t = {}
  t[#t + 1] = string.format("%s  cams %d/%d  alarm: %s", cctv.viewing(p) and "CAMERA FEEDS" or "CONTROL ROOM", up, n,
                            PW.out and "NO POWER" or (H.hot and "RAISED" or "quiet"))
  t[#t + 1] = "VAULT: " .. vault_text()
  for _, cr in ipairs(CARD_ROLES) do
    local c, role
    for r, cc in pairs(H.carriers) do if cc.flags == cr[1] and (cc.chr or cc.floor) then c, role = cc, r end end
    local where
    if not c then where = "-"
    elseif c.floor then where = spawn.key_alive(c.key) and ((AREA_LABEL[c.area] or c.area) .. " (on the desk)") or "taken"   -- (section 25)
    elseif not (c.key and select(2, spawn.key_alive(c.key))) then where = "card not on him"
    elseif not spawn.alive(c.chr) or spawn.down(c.chr) then where = "down"
    else
      local s = H.seen[role]
      if s and H.elapsed - s.at < 1 then where = (AREA_LABEL[s.area] or s.area) .. " (live)"
      elseif s then where = string.format("%s %ds ago", AREA_LABEL[s.area] or s.area, math.floor(H.elapsed - s.at))
      else where = "unknown" end
    end
    t[#t + 1] = string.format("%s %s: %s", cr[2], role or "?", where)
  end
  local counts = {}
  for _, g in ipairs(H.guards) do
    if spawn.alive(g.chr) and not spawn.down(g.chr) then counts[g.area] = (counts[g.area] or 0) + 1 end
  end
  local gl = {}
  for _, a in ipairs(GUARD_AREAS) do
    gl[#gl + 1] = string.format("%s %s", a, covered[a] and tostring(counts[a] or 0) or "?")
  end
  t[#t + 1] = "GUARDS " .. table.concat(gl, "  ", 1, 3)
  t[#t + 1] = "  " .. table.concat(gl, "  ", 4) .. "  " .. MAP.no_cams
  return t
end

local function intel_tick()
  intel_scan()
  for p = 0, bond.count() - 1 do
    if not H.ended and (cctv.viewing(p) or bond.room(p) == intel_room) then hud.panel(H.intel_lines(p), p) else hud.panel(nil, p) end
  end
end

function H.camera_switch(p)
  for _, cam in ipairs(cctv.list()) do cctv.disable(cam) end
  H.cam_off_until = H.elapsed + S.cam_off_time
  local t = {}
  for _, role in ipairs(JOB.console_roles) do
    local c = H.carriers[role]
    if c and c.chr and spawn.alive(c.chr) and c.key and select(2, spawn.key_alive(c.key)) then t[#t + 1] = role .. ": " .. (AREA_LABEL[c.area] or c.area) end
  end
  log("security console used by Bond %d: cameras off for %d s; carriers: %s", p, S.cam_off_time, table.concat(t, ", "))
  flash("Cameras off " .. S.cam_off_time .. " s. " .. (t[1] or "No cards left on anyone"), 8)
  return true
end

-- ------------------------------------------------------------------------------------------------------------------------------------
-- the power (section 16): the plant room's main breaker and the backup generator

local function near_xz(p, xz)
  if not xz then return false end
  local x, y, z = bond.pos(p)
  return x ~= nil and not bond.dead(p) and not cctv.viewing(p) and d2(x, z, xz[1], xz[2]) <= S.power_reach
end

local function set_cams(on)
  for _, cam in ipairs(cctv.list()) do if not on then cctv.disable(cam) elseif not (H.rand and H.rand.dead[cam]) then cctv.enable(cam) end end
end

function H.cut_power(why)
  if PW.out then return false end
  PW.out = true
  PW.cuts = PW.cuts + 1
  PW.cut_at = H.elapsed
  PW.repair = 0
  power.set(false)
  if alarm.active() then H.klaxon = true; alarm.stop(); H.klaxon = false end
  set_cams(false)
  H.report = nil                                   -- the screens are dead: a camera report on its way dies
  if S.investigators > 0 and P.power_way then PW.invest_at = H.elapsed + S.investigate_after end
  log("POWER CUT (%s) at %.0f s: no alarm can be raised, the cameras are dark; generator %s", why or "breaker", H.elapsed,
      PW.gen_off and "disabled" or (S.generator_after > 0 and string.format("in %d s", S.generator_after) or "none"))
  flash("POWER CUT: the alarm is dead, the cameras are dark" .. ((PW.gen_off or S.generator_after <= 0) and "" or
        string.format(" (generator in %d s)", S.generator_after)), 6)
  return true
end

function H.restore_power(why)
  if not PW.out then return false end
  PW.out = false
  PW.step = 0                                      -- the breaker is set again: a new cut starts from the cabinet (left open: quick)
  PW.saved.breaker = 0
  power.set(true)
  if not (H.cam_off_until and H.elapsed < H.cam_off_until) then set_cams(true) end
  log("POWER BACK (%s) at %.0f s", why, H.elapsed)
  flash("The power is back (" .. why .. "): cameras and alarms are live", 5)
  -- a witness who tried the alarm in the dark calls it in now, if he is still up
  for _, g in ipairs(PW.pending) do
    if spawn.alive(g) and not spawn.down(g) and not H.hot then go_hot(string.format("guard %d called it in when the power came back", g)) end
  end
  PW.pending = {}
  if H.hot and S.siren_latch and not alarm.active() then alarm.start() end
  return true
end

local function power_need(task) return task == "cabinet" and S.power_cabinet or (task == "breaker" and S.power_cut or S.generator_time) end

local function power_start(p, task)
  PW.task, PW.by = task, p
  PW.prog = PW.saved[task] or 0
  PW.hp = select(1, bond.vitals(p))
  log("Bond %d works on the %s (%.0f of %d s)", p, task, PW.prog, power_need(task))
  flash_p(p, task == "cabinet" and "Forcing the breaker cabinet: stay with it, stay unseen" or
             (task == "breaker" and "Cutting the main breaker: stay with it, stay unseen" or "Disabling the backup generator"), 4)
end

local function power_stop(why)
  local p, task = PW.by, PW.task
  PW.saved[task] = math.max(0, PW.prog - S.power_setback)
  PW.task, PW.by = nil, nil
  log("power work (%s) interrupted: %s (%.0f s kept)", task, why, PW.saved[task])
  flash_p(p, "Interrupted (" .. why .. "): press use at the panel to go on", 4)
end
H.power_stop = power_stop

-- a use press in the plant room: the breaker (cabinet, then the main) or the generator
local function power_use(p)
  if H.ended or PW.task then return false end
  if near_xz(p, P.breaker) and not PW.out then
    power_start(p, PW.step == 0 and "cabinet" or "breaker")
    return true
  elseif near_xz(p, P.generator) and not PW.gen_off and S.generator_after > 0 then
    power_start(p, "generator")
    return true
  end
  return false
end
H.power_use = power_use

local function power_tick(dt)
  if PW.task then
    local p, task = PW.by, PW.task
    local hp = select(1, bond.vitals(p))
    local xz = task == "generator" and P.generator or P.breaker
    if bond.dead(p) or not near_xz(p, xz) then power_stop("you left the panel")
    elseif S.cover and cover.seen(p) then power_stop("a guard can see you")
    elseif hp and PW.hp and hp < PW.hp - 0.001 then power_stop("you were hit")
    else
      PW.hp = hp
      PW.prog = PW.prog + dt
      if PW.prog >= power_need(task) then
        PW.saved[task] = 0
        PW.task, PW.by = nil, nil
        if task == "cabinet" then
          PW.step = 1
          log("the breaker cabinet is open (Bond %d)", p)
          power_start(p, "breaker")                -- straight on to the main
        elseif task == "breaker" then
          H.cut_power("Bond " .. p .. " threw the main breaker")
        else
          PW.gen_off = true
          log("the backup generator is disabled (Bond %d)", p)
          flash_p(p, "The backup generator is dead: the power stays out", 4)
        end
      end
    end
  end
  if PW.out then
    if not PW.gen_off and S.generator_after > 0 and H.elapsed >= PW.cut_at + S.generator_after then H.restore_power("the backup generator") end
  end
  if PW.out and PW.invest_at and H.elapsed >= PW.invest_at then
    PW.invest_at = nil
    local n = scaled(S.investigators, 1)
    for i = 1, n do
      local c = spawn_at(P.power_way, guard_opts({ post = true }))
      if c then
        note_guard(c, "investigator", "corridor")
        PW.invest[#PW.invest + 1] = c
        spawn.go(c, P.power_to, false)
      end
    end
    log("%d guard(s) sent to look at the outage", #PW.invest)
  end
  if PW.out and #PW.invest > 0 then
    local fixing = false
    for _, c in ipairs(PW.invest) do
      if spawn.alive(c) and not spawn.down(c) then
        local x, z = spawn.where(c)
        if x and d2(x, z, P.breaker[1], P.breaker[2]) <= S.power_reach + 60 then fixing = true end
      end
    end
    if fixing then
      PW.repair = PW.repair + dt
      if PW.repair >= S.repair_time then H.restore_power("a guard reset the breaker") end
    end
  end
end

local function vault_tick(dt)
  local ph = H.phase
  if ph == "open" or ph == "closing" then
    H.vault_left = H.vault_left - dt
    if ph == "open" and H.vault_left <= S.klaxon_at then
      H.phase = "closing"
      H.klaxon = true
      if not alarm.active() then alarm.start() end   -- the klaxon (the siren, without the alarm response until 0)
      H.klaxon = false
      flash(string.format("VAULT CLOSING IN %d SECONDS!", S.klaxon_at), 5)
      log("klaxon: %d s left", S.klaxon_at)
    end
    if H.vault_left <= 0 then
      hud.countdown(nil)
      vault_doors(false)
      H.trapped = {}
      local any = false
      for p = 0, bond.count() - 1 do
        if in_vault(p) then H.trapped[p] = true; any = true end
      end
      if any then
        -- a Bond shut in: the alarm, and the heavy squad comes for him (re-crack from the inside to get out)
        H.phase = "trapped"
        go_hot("vault time lock: crew trapped")
        spawn_squad(P.heavy, scaled(S.heavy_squad, 2), { behaviour = "chase", body = "Siberian_Special_Forces", sunglasses = true, weapon = "rcp90",
                    armour = 2, health = 1.5 }, "heavy squad")
        flash("TRAPPED! Re-crack the vault door from the inside", 6)
        log("vault closed with Bond(s) inside: trapped")
      else
        -- an empty vault seals quietly: a clean run stays clean (the klaxon's siren is switched off again unless the heist is hot)
        H.phase = "sealed"
        if S.timelock_alarm_empty then go_hot("vault time lock")
        elseif not H.hot and alarm.active() then H.klaxon = true; alarm.stop(); H.klaxon = false end
        flash("The vault has sealed. Get out!", 5)
        log("vault closed and sealed (%s)", H.hot and "the heist is hot" or "quiet")
      end
    end
  end
  hold_vault_doors(H.elapsed)
end

-- ------------------------------------------------------------------------------------------------------------------------------------
-- loot, civilians, exits, the end

on("item_pickup", function(objtype, id, qty, p)
  if objtype ~= 3 or (id ~= 79 and id ~= 78) then return end   -- PROPDEF_PROP, ITEM_GOLDBAR / ITEM_MONEY
  local v = (id == 79) and S.bar_value or S.money_value
  H.loot = H.loot + v
  H.carried[p] = (H.carried[p] or 0) + v
  if id == 79 then H.bars = H.bars + 1 else H.cash = H.cash + 1 end
  if S.cover then cover.carrying(p, true) end   -- a staffer with an armful of gold: any guard who sees him sees through him
  flash(string.format("+%s  (%s)", money(v), id == 79 and "gold bar" or "cash"), 2)
  if S.test then log("loot: Bond %d +%d (total %d)", p, v, H.loot) end
end)

local function finish(win, msg)
  if H.ended then return end
  H.ended = true
  hud.countdown(nil)
  local killed = civilian.killed()
  local bonus = (not H.hot and win) and math.floor(H.loot * S.quiet_bonus) or 0
  local score = math.max(0, H.loot + bonus - killed * S.civ_fine)
  if not win then score = 0 end
  local best = ""
  if win and H.plan_cost then score = math.max(0, score - H.plan_cost) end   -- (the planning spend, section 27)
  if win and H.traits then score = math.max(0, score - H.traits.cost()) end   -- (the bribes, section 32)
  if win and score > HEIST_BEST then HEIST_BEST = score; best = "  NEW BEST" end
  log("%s: %s | loot %s (%d bars, %d cash) + quiet bonus %s - %d civilians x %s = SCORE %s | best this session %s | %d s, alarm %s",
      win and "WIN" or "FAIL", msg, money(H.loot), H.bars, H.cash, money(bonus), killed, money(S.civ_fine), money(score),
      money(HEIST_BEST), math.floor(H.elapsed), H.hot and "yes" or "no")
  if win and best ~= "" then log("HIGH SCORE %s", money(score)) end
  for p = 0, bond.count() - 1 do
    hud.set(1, win and string.format("HEIST COMPLETE  SCORE %s%s", money(score), best) or ("HEIST FAILED: " .. msg), p)
    hud.set(2, string.format("Loot %s  bonus %s  civilians %d  best %s", money(H.loot), money(bonus), killed, money(HEIST_BEST)), p)
    hud.set(3, "", p)
    hud.panel(nil, p)
    cctv.view_off(p)
  end
  H.score = score
  if H.debrief_end then local ok, e = pcall(H.debrief_end, win, msg, bonus, killed); if not ok then log("debrief failed: %s", tostring(e)) end end   -- (section 26)
  if win then mission.win(string.format("Heist complete: %s (best %s)", money(score), money(HEIST_BEST)))
  else mission.fail(msg) end
end
H.finish = finish

local function exits_tick()
  if H.loot <= 0 then return end
  local n, inside, living = bond.count(), 0, 0
  for p = 0, n - 1 do
    if not bond.dead(p) then
      living = living + 1
      if zone.contains(H.zones.exit_a, bond.id(p)) or zone.contains(H.zones.exit_b, bond.id(p)) then inside = inside + 1 end
    end
  end
  if living == 0 then return end
  -- every living Bond must be out (AC's answer 8), whatever coop_exits says; a down Bond waiting to respawn does not hold the crew
  if inside == living then finish(true, "the crew got out") end
end

local function fail_tick()
  if bond.all_down() then finish(false, "every Bond is down"); return end
  if civilian.killed() > H.civ_limit then finish(false, string.format("too many civilians killed (%d)", civilian.killed())) end
end

on("civilian_killed", function(c, p)
  local k = civilian.killed()
  log("civilian %d killed (by %d): %d of %d allowed", c, p, k, H.civ_limit)
  flash(string.format("A civilian is dead! (%d/%d)", k, H.civ_limit), 4)
  fail_tick()
end)

on("chr_knocked_out", function(c, p)
  if S.test then log("chr %d knocked out by %d", c, p) end
end)

-- ------------------------------------------------------------------------------------------------------------------------------------
-- stage start and the tick

mode.keep_locks()
mode.per_player_keys()
mode.hold_fire(S.hold_fire)

-- the map's tables from LEVEL (heist_bank_map(): it reads pad positions, so the stage must be up)
local function load_map()
  local M = heist_bank_map(JOB)
  DOORS, AREAS, AREA_LABEL, VAULT_ROOMS, VAULT_PADS, VAULT_CRATE_PADS = M.DOORS, M.AREAS, M.AREA_LABEL, M.VAULT_ROOMS, M.VAULT_PADS, M.VAULT_CRATE_PADS
  CIV_PLACES, P, CARRIERS, CAM_LABEL, CAM_AREA, GUARD_AREAS = M.CIV_PLACES, M.P, M.CARRIERS, M.CAM_LABEL, M.CAM_AREA, M.GUARD_AREAS
  intel_room, MAP = M.intel_room, M.MAP
  H.M = M
end

on("stage_start", function(stage)
  if stage ~= STAGE_LEVELGEN or type(LEVEL) ~= "table" or type(LEVEL.objects) ~= "table" or LEVEL.objects[JOB.check_object] == nil then
    hud.set(1, JT.not_here)
    log("%s (stage %d): nothing set up", JT.not_here_log, stage)
    H.phase = "off"
    return
  end
  H.rand = heist_randomiser{ S = S, H = H, job = BASE_JOB }   -- section 25: today's deal (nil when S.randomise is off)
  JOB = H.rand and H.rand.job or BASE_JOB
  JT = JOB.text
  load_map()
  H.map = MAP.name
  diff = DIFF[mission.difficulty()] or DIFF[1]
  H.civ_limit = S.civ_limit or diff.civ_limit
  spawn.keep_bodies(true)
  setup_doors()
  if H.rand then H.rand.doors() end
  -- the cover (section 16): every room classed by its layout area; cameras report to the security room instead of the alarm
  cover.on(S.cover and true or false)
  if S.cover then
    cover.tune(S.cover_tune or {})
    for _, r in pairs(LEVEL.rooms) do cover.room(r.id, S.area_class[r.area] or r.zone or "public") end   -- (bank v2: else the room's zone)
    cctv.alarm_on_spot(false)
  end
  HEIST_AI.setup(S, H)
  if HEIST_COMBAT and HEIST_COMBAT.setup then HEIST_COMBAT.setup{ S = S, H = H, P = P, DOORS = DOORS, flash = flash, hostiles = hostiles_alive, guard_opts = guard_opts } end
  H.hand = S.cover and S.start_hand or S.start_weapons[1]
  for p = 0, bond.count() - 1 do
    bond.warp(p, P.start[p % #P.start + 1], (p >= #P.start) and 60 or 0, 0)
    for _, w in ipairs(S.start_weapons) do bond.give(p, w) end
    bond.give_ammo(p, S.start_ammo)
    bond.equip(p, H.hand)
  end
  local ax, ay, az = pads.pos(P.exit_a)
  local bx, by, bz = pads.pos(P.exit_b)
  H.zones.exit_a = zone.add("exit_van", ax, ay, az, S.exit_radius)
  H.zones.exit_b = zone.add("exit_dock", bx, by, bz, S.exit_radius - 50)
  local cx, cy, cz = pads.pos(P.console)
  H.console_zone = zone.add("console", cx, cy, cz, 300)
  for cam, label in pairs(CAM_LABEL) do cctv.name(cam, label) end
  queue_carriers()
  queue_area_guards()
  queue_civilians()
  queue_loot()
  queue[#queue + 1] = function()
    H.ready = true
    H.phase = "casing"
    for p = 0, bond.count() - 1 do bond.equip(p, H.hand) end   -- again: the level start can put the slappers back in hand
    log("ready: %d guards (%d posted, %d alarm raisers, %d card carriers), %d civilians, %d cameras, civilian limit %d, difficulty %d, %d free chr slots",
        #H.guards, H.placed.post, H.placed.raiser, H.placed.carrier, H.placed.civ, #cctv.list(), H.civ_limit, mission.difficulty(), spawn.free())
    log("pools: loose %d, held %d, free weapon slots %d, free props %d", spawn.pools())
  end
  local function wave_opts()
    return guard_opts({ behaviour = "chase", body = pick({ "Siberian_Special_Forces", "St_Petersburg_Guard" }), weapon = pick({ "kf7", "d5k", "zmg" }) })
  end
  waves = heist.reinforce{ pads = P.waves_front, interval = S.wave_interval, per_wave = math.max(1, diff.wave), max_alive = scaled(S.wave_max_alive, 4),
                           first = S.wave_first, reserve = S.reserve_slots, opts = wave_opts, on_spawn = function(c) spawn.role(c, "wave"); if HEIST_COMBAT then HEIST_COMBAT.guard(c, "wave") end end,
                           active = function() return H.hot and not H.ended and hostiles_alive() < S.hostile_cap end }
  response = heist.reinforce{ pads = { P.waves_front[1], P.waves_dock[1], P.waves_front[2], P.waves_dock[2] }, interval = S.response_interval,
                              per_wave = scaled(S.response_size, 1), max_alive = scaled(S.wave_max_alive, 4), first = 0, reserve = S.reserve_slots,
                              on_spawn = function(c) spawn.role(c, "response"); if HEIST_COMBAT then HEIST_COMBAT.guard(c, "response") end end,
                              opts = function() return guard_opts({ behaviour = "chase", body = pick({ "Siberian_Special_Forces", "Janus_Special_Forces" }),
                                                                    weapon = pick({ "rcp90", "ar33" }), armour = 1.5, sunglasses = true }) end,
                              active = function() return H.hot and not H.ended and H.hot_at and H.elapsed - H.hot_at >= S.response_after
                                                         and hostiles_alive() < S.hostile_cap end }
  flash(S.cover and JT.intro_cover or JT.intro_plain, 8)
  log("stage start: %s, %d Bond(s), difficulty %d", MAP.label, bond.count(), mission.difficulty())
  refresh_hud()
  if S.intel ~= false then H.intel = heist_intel{ S = S, H = H, PW = PW, M = H.M, job = JOB, force = diff.force, flash_p = flash_p, money = money, security_up = security_up,
                         keys = { GREEN = GREEN, BLUE = BLUE, RED = RED, GOLD = GOLD, FLOORKEY = FLOORKEY, VAULT = VAULT } } end
  if S.guide ~= false then H.guide = heist_guide{ S = S, H = H, PW = PW, M = H.M, job = JOB, keys = { GREEN = GREEN, BLUE = BLUE, RED = RED, GOLD = GOLD, FLOORKEY = FLOORKEY } } end
end)

-- the script's sim state for the co-op / netplay state hash (mode.state): a script-state desync shows at once, and the dump names it
local function state_text()
  local carried = {}
  for p = 0, bond.count() - 1 do carried[#carried + 1] = tostring(H.carried[p] or 0) .. (H.trapped[p] and "T" or "") end
  return string.format("%s%s hot=%s@%s t=%.4f prog=%.5f jams=%d nj=%.2f by=%s vl=%s loot=%d/%d/%d carried=%s sq=%d g=%d c=%d w=%d/%d r=%d/%d off=%s q=%d hs=%s ps=%s safe=%s",
    H.phase, H.ended and "(ended)" or "", tostring(H.hot), tostring(H.hot_at), H.elapsed, H.progress, H.jams, H.next_jam, tostring(H.crack_by), tostring(H.vault_left),
    H.loot, H.bars, H.cash, table.concat(carried, ","), H.squads, #H.guards, #H.civs, waves and waves.spawned or 0, waves and waves.waves or 0,
    response and response.spawned or 0, response and response.waves or 0, tostring(H.cam_off_until), #queue, tostring(H.half_squad),
    tostring(H.paused), tostring(safe_done))
    .. string.format(" cov=%s/%d/%d rep=%s pw=%s/%d/%s/%s/%.3f/%s/%s gen=%s inv=%d/%s fix=%.2f pend=%d",
      tostring(cover.alert()), H.blown, H.reports, H.report and string.format("%.2f", H.report.at) or "-", tostring(PW.out), PW.step,
      tostring(PW.task), tostring(PW.by), PW.prog, tostring(PW.cut_at), tostring(PW.saved.cabinet + PW.saved.breaker + PW.saved.generator),
      tostring(PW.gen_off), #PW.invest, tostring(PW.invest_at), PW.repair, #PW.pending)
    .. (H.rand and H.rand.state() or "")   -- (section 25: today's deal)
end
H.state_text = state_text

local hud_acc = 0
on("tick", function(dt)
  if H.phase ~= "off" then mode.state(state_text()) end
  if H.phase == "off" or H.ended then return end
  H.elapsed = H.elapsed + dt
  for _ = 1, S.spawn_per_tick do
    if #queue == 0 then break end
    local job = table.remove(queue, 1)
    local ok, err = pcall(job)
    if not ok then log("setup step failed: %s", tostring(err)) end
  end
  if H.intel then H.intel.tick(dt) end   -- the INTEL board's content and the hints (presentation; tools/heist_intel.lua)
  if H.plan and H.plan.tick(dt) then return end   -- the PLANNING phase holds the start (tools/heist_planning.lua, section 27)
  if H.rand then H.rand.tick(dt) end     -- section 25: the job of the day's timed events
  if not H.ready then return end
  if H.hot and S.siren_latch and not alarm.active() then alarm.start() end
  if H.cam_off_until and H.elapsed >= H.cam_off_until then
    H.cam_off_until = nil
    if not PW.out then
      for _, cam in ipairs(cctv.list()) do if not (H.rand and H.rand.dead[cam]) then cctv.enable(cam) end end   -- (a dummy stays dead)
      flash("The cameras are back on", 3)
    end
  end
  security_tick()
  HEIST_AI.tick()
  if HEIST_COMBAT and HEIST_COMBAT.tick then HEIST_COMBAT.tick() end
  if P.breaker then power_tick(dt) end
  crack_tick(dt)
  vault_tick(dt)
  safe_tick()
  fail_tick()
  if not H.ended then exits_tick() end
  hud_acc = hud_acc + dt
  if hud_acc >= 0.5 then hud_acc = 0; if not H.ended then refresh_hud(); intel_tick() end end
end)

-- the PLANNING phase (designs/bank-heist.md section 27): before the gadgets (it empties their loadout until the plan is confirmed)
include("heist_planning")
-- the crew's gadgets (designs/bank-heist.md section 21): their own handlers, after the heist's (their state rides in mode.state)
include("heist_gadgets")
-- the door interlocks: the staff entrance mantrap and the sally port (designs/bank-heist.md section 29; their state rides in mode.state)
if S.interlock ~= false then include("heist_interlock") end
-- guard personalities (designs/bank-heist.md section 32): the deal, bribes, the dossiers, the AR tags (their state is in C, hashed)
if S.personalities then include("heist_traits") end
-- the end-of-heist DEBRIEF (designs/bank-heist.md section 26): reads the run's events, scores it at finish() (presentation only)
if S.debrief ~= false then include("heist_debrief") end
-- the AR specs and diagnostics (tools/heist_ar.lua): after every handler above, so the map is loaded when its stage_start runs
if heist_ar and heist_ar.attach then heist_ar.attach(H, S) end
