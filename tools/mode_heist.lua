-- Bank heist (GE_MODE=heist on Bunker 2: GE_STAGE=bunker2). Shipped to Data/Scripts/modes/heist.lua by the build (with heist_lib.lua).
-- designs/bank-heist.md (P0 job 6). The Janus front bank, St Petersburg branch: get in, climb the keycard ladder, crack the vault, carry out
-- every gold bar you can, and get every Bond out alive.
--
-- The flow on Bunker 2 (rooms and pads: designs/bank-heist-bunker2.md):
--   street / van (north tunnel, pad 51)  ->  front door (open)  ->  blast doors (open; LOCKDOWN on alarm: BLUE card to pass)
--   -> banking hall (room 6: tellers, staff, guards, camera)  ->  central corridors (camera)
--   -> GREEN doors: offices (rooms 11/12, camera)        GREEN card: the head teller OR the lobby captain (random each run)
--   -> BLUE doors: security wing (rooms 43/44/46, 2 cameras, the camera console)   BLUE card: the security chief (offices)
--   -> RED doors: the two vault antechambers (east wing, rooms 51/54)   RED card: the branch manager (security wing)
--   -> plant the cracker at vault door A (antechamber room 51): equip the door decoder and fire at the door, or press use on it
--   -> crack it (hold the zone; it jams now and then: get back in and restart it), the vault opens on a timer: grab the gold
--   -> T-15 s klaxon; at 0 the vault doors close and relock and the alarm goes off: anyone inside re-cracks from the inside (alarm + a
--      heavy squad). The manager's safe (GOLD card, the vault captain) and the deposit cells (GREEN / the floor key) hold more.
--   -> exits: A the van (where you came in), B the loading dock (west wing). Every living Bond must be in an exit (coop_exits=first:
--      one is enough), carrying at least something.
-- Fail: every Bond down, or more civilians killed than the stock Frigate rule allows (Agent 4, Secret Agent 2, 00 Agent / 007 1).
-- Knock-outs never count. Score = the loot (bars and cash), +25% for a run that never went loud, minus a fine per civilian killed.
-- The best score this session is kept (HEIST_BEST) and logged ("ge: heist: HIGH SCORE"); modes have no save file.
--
-- Settings: set HEIST_SETTINGS before include("heist") (tests do), or edit S below. Every count, timer and chance is in this block.
-- Co-op: keys are per player (mode.per_player_keys), every Bond must get out, the HUD is per Bond, jams use math.random (the session's
-- generator, the same on both machines); the script reads only per-player state (bond.pos(p), bond.has_key(p, ...)), never the local player.

include("heist_lib")

local S = {
  -- people (at Agent; scaled by DIFF below). About 105 guards are placed on Agent, 120 on Secret Agent: most stand on post, which costs
  -- almost nothing; the vault guards (+8) appear when it opens, the waves come on top.
  guards_post     = 90,      -- posted guards (stand guard; the stock simple guard list: they hear shots, see Bond, react to bodies)
  guards_raiser   = 12,      -- alarm raisers: on sight or sound they run to the area's alarm panel and sound the alarm
  guards_vault    = 8,       -- vault guards: they wake (spawn inside, chasing) when the vault door opens
  civilians       = 18,      -- bank staff (unarmed; aim at one to make it surrender, keep aiming and it hands over what it carries)
  hearing         = 1.0,     -- guard hearing scale (silenced PP7: heard within 100 units on a first shot, 500 at its cap)
  vision          = 3000,    -- guard sight range, units (corridors here are 100-300 wide, rooms 500-1500)
  guard_weapons   = { "kf7", "kf7", "kf7", "d5k", "zmg", "dd44" },
  guard_bodies    = { "St_Petersburg_Guard", "St_Petersburg_Guard", "Russian_Soldier" },
  civ_bodies      = { "Civilian_1_Female", "Civilian_2", "Civilian_3", "Civilian_4" },
  manager_body    = "Formal_Wear",
  chief_body      = "Russian_Commandant",
  -- alarm and reinforcements
  siren_latch     = true,    -- once the alarm is up the heist is "hot" for good: the siren is re-raised whenever it times out
  wave_interval   = 25,      -- seconds between waves while hot
  wave_size       = 3,
  wave_max_alive  = 16,      -- wave guards alive at once (posted guards not counted)
  wave_first      = 6,       -- seconds from the alarm to the first wave
  response_after  = 120,     -- seconds hot before the elite response (armoured Spetsnaz / Janus SF from both entrances)
  response_size   = 3,
  response_interval = 30,
  hostile_cap     = 120,     -- no wave spawns while this many armed chrs are alive (perf: ~75 live is free, idle posts cost little)
  reserve_slots   = 12,
  lockdown        = true,    -- the alarm closes and locks the blast doors at the hall: BLUE card to pass (exit B is always open)
  -- cameras: stock (10 s of unbroken sight on Agent, 5 s above); the security console blinds every camera for cam_off_time seconds
  cam_off_time    = 90,
  -- the cracker (the door decoder: equip it and fire at the vault door, or press use there)
  crack_time      = 90,      -- seconds of progress to open the vault (progress runs only while a Bond is in the zone)
  crack_radius    = 260,
  jam_every       = 15,      -- seconds of progress between jam rolls
  jam_chance      = 0.30,    -- chance of a jam at each roll
  crack_squads    = 2,       -- squads that come for the cracker even in a quiet run (one at the start, one at half way)
  crack_squad_size = 3,
  vault_time      = 60,      -- seconds the vault stays open
  klaxon_at       = 15,      -- the klaxon sounds this many seconds before it closes
  recrack_time    = 45,      -- re-cracking from the inside
  heavy_squad     = 6,       -- armoured squad sent when someone re-cracks from the inside
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
  spawn_per_tick  = 24,      -- chrs placed per 0.25 s at the start (no hitch)
  test            = false,   -- test hooks on (HEIST.* below are always there; this only adds log lines)
}
if type(HEIST_SETTINGS) == "table" then
  for k, v in pairs(HEIST_SETTINGS) do S[k] = v end
end

-- difficulty scaling (mission.difficulty(): 0 Agent .. 3 007): guard count, accuracy, hearing, waves
local DIFF = {
  [0] = { guards = 1.00, acc = 10, hearing = 0.8,  wave = 2, civ_limit = 4 },
  [1] = { guards = 1.15, acc = 20, hearing = 1.0,  wave = 3, civ_limit = 2 },
  [2] = { guards = 1.30, acc = 30, hearing = 1.25, wave = 4, civ_limit = 1 },
  [3] = { guards = 1.45, acc = 40, hearing = 1.5,  wave = 4, civ_limit = 1 },
}

-- cards (key flags). Per player: a card opens doors only for the Bond who picked it up.
local GREEN, BLUE, RED, GOLD, FLOORKEY, VAULT = 0x2, 0x4, 0x8, 0x10, 0x1, 0x40000000
local CARD_NAMES = { { GREEN, "GREEN" }, { BLUE, "BLUE" }, { RED, "RED" }, { GOLD, "GOLD" } }

-- Bunker 2 map data (designs/bank-heist-bunker2.md; doors are setup indices, pads are pad ids)
local DOORS = {
  front = { 72 },
  blast = { 80, 81, 82, 83, 84, 85 },
  green = { 46, 47, 48, 49 },
  blue = { 50, 51, 52, 53 },
  red = { 58, 59, 60, 61 },
  vault_a = { 68, 69 },          -- the crack door (rooms 56/51)
  vault_b = { 70, 71 },          -- the second vault door (rooms 47/54)
  safe = 102,
  cell_floor = 75, cell_green = 77,
}
local AREAS = {   -- name, pads (waypoint pads first), alarm panel pad, posted share, raisers
  { name = "hall",     pads = { 54, 55, 58, 59, 60, 61, 62, 63, 64, 65, 52, 56, 53, 57, 66, 6, 7, 26, 27, 2, 3, 0, 1 }, panel = 64, share = 14, raisers = 2 },
  { name = "central",  pads = { 67, 68, 69, 70, 71, 84, 4 }, panel = 67, share = 9, raisers = 1 },
  { name = "offices",  pads = { 72, 73, 74, 75, 76, 77, 5 }, panel = 74, share = 7, raisers = 1 },
  { name = "security", pads = { 78, 79, 82, 83, 38 }, panel = 82, share = 5, raisers = 1 },
  { name = "west",     pads = { 102, 103, 104, 105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 23, 24, 48 }, panel = 104, share = 14, raisers = 2 },
  { name = "south",    pads = { 90, 92, 93, 101, 117, 10, 11, 15, 21, 22, 47 }, panel = 92, share = 10, raisers = 1 },
  { name = "east",     pads = { 80, 81, 85, 86, 87, 13 }, panel = 86, share = 14, raisers = 3 },
  { name = "cells",    pads = { 91, 96, 99, 100, 16 }, panel = 99, share = 7, raisers = 1 },
  { name = "safe",     pads = { 12 }, panel = nil, share = 1, raisers = 0 },
}
local AREA_LABEL = { hall = "banking hall", central = "central corridor", offices = "offices", security = "security wing",
                     west = "west wing", south = "records", east = "east wing", cells = "deposit cells", vault = "vault", safe = "manager's safe" }
local VAULT_PADS = { 9, 18, 30, 31, 32, 33, 34, 35, 36, 37, 8, 17, 89, 20, 19, 88 }   -- rooms 55, 56, 57, 58, 47
local VAULT_CRATE_PADS = { [30] = true, [31] = true, [32] = true, [33] = true, [34] = true, [35] = true, [36] = true, [37] = true }
local CIV_PLACES = {   -- area, pads (tellers and customers in the hall, office staff, ...)
  { "hall", { 58, 59, 65, 63, 54, 55, 62, 64, 52, 56 }, 8 },
  { "offices", { 72, 73, 75, 76 }, 4 },
  { "security", { 83, 79 }, 2 },
  { "south", { 93, 101, 10 }, 2 },
  { "east", { 87, 13 }, 2 },
}
local P = {
  start = { 51, 118, 119, 120 },          -- the street (north tunnel, room 68)
  exit_a = 51, exit_b = 116,              -- the van; the loading dock (west wing, room 67)
  crack = 85,                             -- the vault antechamber, outside vault door A (room 51, behind the RED doors 58/59)
  crack_xz = { 892, 2913 },               -- the crack zone centre (pad 85; door 68/69 is 170 units north)
  inside = { { 905, 3313 }, { 925, 4567 } },   -- re-crack zones inside door A (pad 89) and door B (pad 88)
  vault = { { 650, 3380, 380 }, { 1400, 3950, 560 }, { 1233, 4671, 200 }, { 925, 4567, 200 }, { 356, 3703, 200 }, { 365, 4200, 220 } },
                                          -- the vault interior (rooms 56, 55, 47, 57, 58) as circles x, z, r
  console = 82,                           -- the security console (room 43)
  waves_front = { 60, 61, 53, 57 },       -- just inside the blast doors (hall north end)
  waves_dock = { 102, 103, 105 },         -- the loading dock (west wing)
  squad = { 86, 87, 13, 80 },             -- east wing: crack squads come in here
  heavy = { 85, 81, 86, 80 },             -- outside the vault doors
  safe_loot = { 12 },                     -- the floor in front of the manager's safe (the bars spill out when it opens)
  cell_loot = { { 95, 97, 98 }, { 49, 94 } },   -- cell 77 (GREEN), cell 75 (floor key)
}
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
}
HEIST = H

local diff = DIFF[1]
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
    if bond.has_key(p, RED) then return "Crack the vault (east wing, RED doors)" end
    if bond.has_key(p, BLUE) then return "RED card: the branch manager" end
    if bond.has_key(p, GREEN) then return "BLUE card: the security chief" end
    return "GREEN card: head teller or lobby captain"
  elseif ph == "cracking" then return string.format("CRACKING %d%%  stay close", math.floor(H.progress * 100))
  elseif ph == "jammed" then return "CRACKER JAMMED: get in the zone, restart it"
  elseif ph == "open" or ph == "closing" then return "Grab the gold!"
  elseif ph == "trapped" then
    if H.trapped[p] then return "TRAPPED: re-crack the vault door from inside" end
    return "Your partner is trapped in the vault"
  elseif ph == "recracking" then return string.format("RE-CRACKING %d%%", math.floor(H.progress * 100))
  end
  return "Get out: the van or the loading dock"
end

local function refresh_hud()
  local status = H.hot and "ALARM" or "quiet"
  if H.cam_off_until and H.elapsed < H.cam_off_until then status = status .. " CAMS OFF" end
  for p = 0, bond.count() - 1 do
    hud.set(1, string.format("JANUS BANK %s  %d bars  %s  [%s]", money(H.loot), H.bars, status, cards_text(p)), p)
    hud.set(2, objective(p), p)
    if H.flash and H.elapsed < H.flash_until then hud.set(3, H.flash, p) else hud.set(3, "", p) end
  end
end

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
  flash("ALARM! Guards are on their way - the hall is locked down", 6)
  lockdown()
  if not alarm.active() then alarm.start() end
end

on("alarm", function(a)
  if a == 1 and not H.klaxon then go_hot("siren") end
end)

on("cctv_spotted", function(cam, p)
  log("camera %d spotted Bond %d", cam, p)
end)

on("body_found", function(g, b, k)
  if not H.hot then flash(k == 1 and "A guard found someone out cold" or "A guard found a body", 4) end
end)

-- ------------------------------------------------------------------------------------------------------------------------------------
-- setup: guards, staff, carriers, loot

local function area_by_name(n) for _, a in ipairs(AREAS) do if a.name == n then return a end end end

local function queue_area_guards()
  local total_share = 0
  for _, a in ipairs(AREAS) do total_share = total_share + a.share end
  local posts = math.floor(S.guards_post * diff.guards + 0.5)
  local raisers = math.floor(S.guards_raiser * diff.guards + 0.5)
  local raiser_left = raisers
  for _, a in ipairs(AREAS) do
    local n = math.floor(posts * a.share / total_share + 0.5)
    for k = 1, n do
      local pad = a.pads[(k - 1) % #a.pads + 1]
      queue[#queue + 1] = function()
        local c = spawn_at(pad, guard_opts({ post = face_for(a, pad) or true }))
        if c then note_guard(c, "post", a.name) end
      end
    end
    local r = math.min(a.raisers, raiser_left)
    raiser_left = raiser_left - r
    for k = 1, r do
      local pad = a.pads[(k * 3) % #a.pads + 1]
      queue[#queue + 1] = function()
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
        local pad = place[2][(k - 1) % #place[2] + 1]
        local area = place[1]
        queue[#queue + 1] = function()
          local c = spawn_at(pad, { behaviour = "civilian", body = pick(S.civ_bodies) })
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
  log("card %s (0x%x) on %s chr %s at pad %d (%s)", role, flags, role, tostring(c), pad, area)
end

local function queue_carriers()
  queue[#queue + 1] = function()
    -- GREEN: the head teller (a civilian at the counter) or the lobby captain (a guard on the hall floor), chosen at random
    if math.random(2) == 1 then
      local pad = pick({ 58, 59, 65 })
      local c = spawn_at(pad, { behaviour = "civilian", body = "Civilian_1_Female" })
      if c then H.civs[#H.civs + 1] = { chr = c, area = "hall" }; H.placed.civ = H.placed.civ + 1 end
      give_card(c, GREEN, "head teller", "hall", pad)
    else
      local pad = pick({ 54, 55, 62 })
      local c = spawn_at(pad, guard_opts({ post = true, body = "St_Petersburg_Guard" }))
      if c then note_guard(c, "carrier", "hall") end
      give_card(c, GREEN, "lobby captain", "hall", pad)
    end
    -- BLUE: the security chief, in the offices
    local pad = pick({ 72, 73, 74, 75, 76 })
    local c = spawn_at(pad, guard_opts({ post = true, body = S.chief_body, health = 1.5 }))
    if c then note_guard(c, "carrier", "offices") end
    give_card(c, BLUE, "security chief", "offices", pad)
    -- RED: the branch manager, in the security wing
    pad = pick({ 78, 79, 83 })
    c = spawn_at(pad, { behaviour = "civilian", body = S.manager_body })
    if c then H.civs[#H.civs + 1] = { chr = c, area = "security" }; H.placed.civ = H.placed.civ + 1 end
    give_card(c, RED, "branch manager", "security", pad)
    -- GOLD: the vault captain, inside the vault (the manager's safe needs it)
    pad = pick({ 9, 18 })
    c = spawn_at(pad, guard_opts({ post = true, body = "Russian_Commandant", health = 2, armour = 1 }))
    if c then note_guard(c, "carrier", "vault") end
    give_card(c, GOLD, "vault captain", "vault", pad)
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
    while n < S.vault_bars and k < S.vault_bars * 4 do
      k = k + 1
      local pad = VAULT_PADS[(k - 1) % #VAULT_PADS + 1]
      local ring = math.floor((k - 1) / #VAULT_PADS) + 1
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
    log("the manager's safe is open: %d bars", n)
    flash("The manager's safe: gold!", 4)
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
local function in_vault(p)
  for _, c in ipairs(P.vault) do if in_circle(p, c, c[3]) then return true end end
  return false
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
    flash("Re-cracking: they heard that - heavy squad incoming!", 5)
    go_hot("re-crack")
    spawn_squad(P.heavy, S.heavy_squad, { behaviour = "chase", body = "Siberian_Special_Forces", sunglasses = true, weapon = "rcp90",
                armour = 2, health = 1.5 }, "heavy squad")
  else
    flash("The cracker is on the door: stay with it!", 4)
    if S.crack_squads > 0 then spawn_squad(P.squad, S.crack_squad_size, { behaviour = "chase" }, "crack squad 1") end
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
  flash("The vault is open! Grab the gold", 5)
  local n = math.floor(S.guards_vault * diff.guards + 0.5)
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
    spawn_squad(P.squad, S.crack_squad_size, { behaviour = "chase" }, "crack squad 2")
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

on("use", function(p, kind, idx, objtype)
  for _, d in ipairs(DOORS.vault_a) do if idx == d then crack_action(p, "use") end end
  for _, d in ipairs(DOORS.vault_b) do if idx == d then crack_action(p, "use") end end
  -- the security console: blinds every camera for cam_off_time seconds and shows who carries the cards
  if bond.has_key(p, BLUE) and H.console_zone and zone.contains(H.console_zone, bond.id(p)) then H.camera_switch(p) end
end)

function H.camera_switch(p)
  for _, cam in ipairs(cctv.list()) do cctv.disable(cam) end
  H.cam_off_until = H.elapsed + S.cam_off_time
  local t = {}
  for _, role in ipairs({ "head teller", "lobby captain", "security chief", "branch manager", "vault captain" }) do
    local c = H.carriers[role]
    if c and c.chr and spawn.alive(c.chr) and c.key and select(2, spawn.key_alive(c.key)) then t[#t + 1] = role .. ": " .. (AREA_LABEL[c.area] or c.area) end
  end
  log("security console used by Bond %d: cameras off for %d s; carriers: %s", p, S.cam_off_time, table.concat(t, ", "))
  flash("Cameras off " .. S.cam_off_time .. " s. " .. (t[1] or "No cards left on anyone"), 8)
  return true
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
      go_hot("vault time lock")
      if any then
        H.phase = "trapped"
        flash("TRAPPED! Re-crack the vault door from the inside", 6)
        log("vault closed with Bond(s) inside: trapped")
      else
        H.phase = "sealed"
        flash("The vault has sealed. Get out!", 5)
        log("vault closed and sealed")
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
  if win and score > HEIST_BEST then HEIST_BEST = score; best = "  NEW BEST" end
  log("%s: %s | loot %s (%d bars, %d cash) + quiet bonus %s - %d civilians x %s = SCORE %s | best this session %s | %d s, alarm %s",
      win and "WIN" or "FAIL", msg, money(H.loot), H.bars, H.cash, money(bonus), killed, money(S.civ_fine), money(score),
      money(HEIST_BEST), math.floor(H.elapsed), H.hot and "yes" or "no")
  if win and best ~= "" then log("HIGH SCORE %s", money(score)) end
  for p = 0, bond.count() - 1 do
    hud.set(1, win and string.format("HEIST COMPLETE  SCORE %s%s", money(score), best) or ("HEIST FAILED: " .. msg), p)
    hud.set(2, string.format("Loot %s  bonus %s  civilians %d  best %s", money(H.loot), money(bonus), killed, money(HEIST_BEST)), p)
    hud.set(3, "", p)
  end
  H.score = score
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
  local first = (mods.get("coop_exits") == 1)
  if inside == living or (first and inside > 0) then finish(true, "the crew got out") end
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

on("stage_start", function(stage)
  if stage ~= 27 then   -- LEVELID 27 = Bunker 2 (GE_STAGE=bunker2)
    hud.set(1, "HEIST MODE: run it on Bunker 2 (GE_STAGE=bunker2)")
    log("not Bunker 2 (stage %d): nothing set up", stage)
    H.phase = "off"
    return
  end
  diff = DIFF[mission.difficulty()] or DIFF[1]
  H.civ_limit = S.civ_limit or diff.civ_limit
  spawn.keep_bodies(true)
  setup_doors()
  for p = 0, bond.count() - 1 do
    bond.warp(p, P.start[p % #P.start + 1], (p >= #P.start) and 60 or 0, 0)
    for _, w in ipairs(S.start_weapons) do bond.give(p, w) end
    bond.give_ammo(p, S.start_ammo)
    bond.equip(p, S.start_weapons[1])
  end
  local ax, ay, az = pads.pos(P.exit_a)
  local bx, by, bz = pads.pos(P.exit_b)
  H.zones.exit_a = zone.add("exit_van", ax, ay, az, S.exit_radius)
  H.zones.exit_b = zone.add("exit_dock", bx, by, bz, S.exit_radius - 50)
  local cx, cy, cz = pads.pos(P.console)
  H.console_zone = zone.add("console", cx, cy, cz, 300)
  queue_carriers()
  queue_area_guards()
  queue_civilians()
  queue_loot()
  queue[#queue + 1] = function()
    H.ready = true
    H.phase = "casing"
    for p = 0, bond.count() - 1 do bond.equip(p, S.start_weapons[1]) end   -- again: the level start can put the slappers back in hand
    log("ready: %d guards (%d posted, %d alarm raisers, %d card carriers), %d civilians, %d cameras, civilian limit %d, difficulty %d, %d free chr slots",
        #H.guards, H.placed.post, H.placed.raiser, H.placed.carrier, H.placed.civ, #cctv.list(), H.civ_limit, mission.difficulty(), spawn.free())
    log("pools: loose %d, held %d, free weapon slots %d, free props %d", spawn.pools())
  end
  local function wave_opts()
    return guard_opts({ behaviour = "chase", body = pick({ "Siberian_Special_Forces", "St_Petersburg_Guard" }), weapon = pick({ "kf7", "d5k", "zmg" }) })
  end
  waves = heist.reinforce{ pads = P.waves_front, interval = S.wave_interval, per_wave = math.max(1, diff.wave), max_alive = S.wave_max_alive,
                           first = S.wave_first, reserve = S.reserve_slots, opts = wave_opts,
                           active = function() return H.hot and not H.ended and hostiles_alive() < S.hostile_cap end }
  response = heist.reinforce{ pads = { P.waves_front[1], P.waves_dock[1], P.waves_front[2], P.waves_dock[2] }, interval = S.response_interval,
                              per_wave = S.response_size, max_alive = S.wave_max_alive, first = 0, reserve = S.reserve_slots,
                              opts = function() return guard_opts({ behaviour = "chase", body = pick({ "Siberian_Special_Forces", "Janus_Special_Forces" }),
                                                                    weapon = pick({ "rcp90", "ar33" }), armour = 1.5, sunglasses = true }) end,
                              active = function() return H.hot and not H.ended and H.hot_at and H.elapsed - H.hot_at >= S.response_after
                                                         and hostiles_alive() < S.hostile_cap end }
  flash("JANUS BANK, ST PETERSBURG. Steal the gold. Hurt no staff.", 8)
  log("stage start: Bunker 2, %d Bond(s), difficulty %d", bond.count(), mission.difficulty())
  refresh_hud()
end)

local hud_acc = 0
on("tick", function(dt)
  if H.phase == "off" or H.ended then return end
  H.elapsed = H.elapsed + dt
  for _ = 1, S.spawn_per_tick do
    if #queue == 0 then break end
    local job = table.remove(queue, 1)
    local ok, err = pcall(job)
    if not ok then log("setup step failed: %s", tostring(err)) end
  end
  if not H.ready then return end
  if H.hot and S.siren_latch and not alarm.active() then alarm.start() end
  if H.cam_off_until and H.elapsed >= H.cam_off_until then
    H.cam_off_until = nil
    for _, cam in ipairs(cctv.list()) do cctv.enable(cam) end
    flash("The cameras are back on", 3)
  end
  crack_tick(dt)
  vault_tick(dt)
  safe_tick()
  fail_tick()
  if not H.ended then exits_tick() end
  hud_acc = hud_acc + dt
  if hud_acc >= 0.5 then hud_acc = 0; if not H.ended then refresh_hud() end end
end)
