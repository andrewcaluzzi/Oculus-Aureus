-- arena_menu_defs.lua: what the arena's start menu offers (shipped as Data/Scripts/modes/_arena_menu_defs.lua; modding/arena.md "Start
-- menu and post-game"). Included by the lobby (tools/mode_arena_menu.lua) and by the run side (tools/arena_flow.lua). Defines the
-- global ARENA_MENU_DEFS:
--   ROWS     every setting: id, label, group, choices { { label, value }, ... }, default (a choice index), desc; scored = true when it
--            changes the game (only those decide whether the settings are still a preset, and the best-score key)
--   PRESETS  the cards on the main page: id, name, desc, set = { row id = choice index } (the rest are the defaults), key = the
--            best-score key (arena_scores.ini prefix)
--   settings(values) -> ARENA_SETTINGS table, coop (bool), slots (GE_COLOSSEUM_SLOTS or nil)
--   preset_of(values) -> the preset whose scored rows match (nil = custom);  score_key(values) -> the arena_scores.ini prefix
--   serialize(t) -> a Lua table constructor;  parse(text) -> the table (load with an empty environment: data only)
--
-- THE WHACKY INTERFACE (for the whacky-effects mutators, branch arena-whacky): the menu row "whacky" sets ARENA_SETTINGS.whacky =
-- false (off) or an intensity 1 (mild), 2 (normal), 3 (wild). A whacky script reads S.whacky (nil or false = off).

ARENA_MENU_DEFS = {}
local D = ARENA_MENU_DEFS

-- THE WORDS: every player-facing string of the menu and the debrief is in the shared theme table, ARENA_THEME.menu
-- (tools/arena_theme.lua; AC: the whole arena Bond-themed), with the medal names (ARENA_THEME.medals). Only the settings rows' own
-- labels and help lines are here, beside their values.
include("_arena_theme")
local T = ARENA_THEME.menu
D.T = T

local ROWS = {
  -- RUN
  { id = "pacing", group = "RUN", label = "PACING", scored = true, default = 1,
    choices = { { "20 WAVES", { mode = "fixed", waves = 20 } }, { "10 WAVES", { mode = "fixed", waves = 10 } },
                { "30 WAVES", { mode = "fixed", waves = 30 } }, { "ENDLESS", { mode = "endless" } } },
    desc = "Fixed: clear the last wave to win. Endless: the waves never stop, they only get harder." },
  { id = "boss", group = "RUN", label = "BOSS WAVES", scored = true, default = 1,
    choices = { { "EVERY 5", 5 }, { "EVERY 3", 3 }, { "EVERY 10", 10 }, { "NONE", 0 } },
    desc = "The Baron, the Traitor, Iron Jaws and the Triumvirate take every Nth wave." },
  { id = "difficulty", group = "RUN", label = "DIFFICULTY", scored = true, default = 2,
    choices = { { T.difficulty[1], 0 }, { T.difficulty[2], 1 }, { T.difficulty[3], 2 }, { T.difficulty[4], 3 } },
    desc = "How fast the opposition toughens and sharpens, how many come at once, how much a kill heals. " .. T.difficulty[2] .. " is the arena as built." },
  { id = "start_delay", group = "RUN", label = "GET READY", scored = false, default = 2,
    choices = { { "3 S", 3 }, { "6 S", 6 }, { "10 S", 10 } }, desc = "Seconds before wave 1." },
  { id = "breather", group = "RUN", label = "BREATHER", scored = true, default = 2,
    choices = { { "SHORT 6 S", 6 }, { "NORMAL 12 S", 12 }, { "LONG 20 S", 20 } }, desc = "The pause between waves." },
  -- CHAOS
  { id = "mutators", group = "CHAOS", label = "WAVE MUTATORS", scored = true, default = 1,
    choices = { { "OFF", false }, { "ON", true } },
    desc = "Waves may roll a mutator: knives only, paintball, one-hit kills, turbo, fog, big heads, iron skin, bottomless clips." },
  { id = "elites", group = "CHAOS", label = "AFFIX ELITES", scored = true, default = 1,
    choices = { { "OFF", false }, { "ON", true } },
    desc = "Ordinary enemies may roll affixes: armoured, swift, knife thrower, explosive, regenerating, marksman." },
  { id = "intensity", group = "CHAOS", label = "CHAOS INTENSITY", scored = true, default = 2,
    choices = { { "MILD", 1 }, { "NORMAL", 2 }, { "WILD", 3 }, { "MAX", 4 } },
    desc = "How often mutators and elites roll. MAX: a mutator every wave, bosses included." },
  { id = "whacky", group = "CHAOS", label = "WHACKY EFFECTS", scored = true, default = 1,
    choices = { { "OFF", false }, { "MILD", 1 }, { "NORMAL", 2 }, { "WILD", 3 } },
    desc = "Visual mayhem on the enemies (tints, ghosts and stranger things), from the whacky module when it is installed." },
  -- PLAYERS
  { id = "players", group = "PLAYERS", label = "PLAYERS", scored = true, default = 1,
    choices = { { "SOLO", false }, { "00 PARTNER (BOT)", true } },
    desc = "Co-op: a second Bond played by the arena bot (fights, loots, revives you). Waves are bigger. No best scores in co-op." },
  { id = "downed", group = "PLAYERS", label = "CO-OP DOWNED", scored = true, default = 1,
    choices = { { "BACK NEXT WAVE", "wave" }, { "OUT FOR THE RUN", "run" }, { "RUN ENDS", "end" }, { "NO REVIVES", "off" } },
    desc = "Co-op: a Bond out of health goes down; stand by him 3 s to revive. If he bleeds out (30 s): this." },
  -- (not scored: co-op keeps no best scores, and a solo run must not turn custom over it)
  { id = "bot_skill", group = "PLAYERS", label = "BOT SKILL", scored = false, default = 4,
    choices = { { "ROOKIE", 1 }, { "AGENT", 2 }, { "SECRET AGENT", 3 }, { "00 AGENT", 4 }, { "007", 5 } },
    desc = "Co-op: how good your partner is. Lower: slower to react and turn, a shakier aim, a shorter reach. 007: the sharpest." },
  -- ENEMIES
  { id = "cap", group = "ENEMIES", label = "ENEMIES AT ONCE", scored = true, default = 2,
    choices = { { "FEW 10", 10 }, { "NORMAL 16", 16 }, { "MANY 24", 24 }, { "SWARM 32", 32 } },
    desc = "How many enemies the pit holds at wave 1; it grows by one every two waves." },
  { id = "knife_keep", group = "ENEMIES", label = "KNIVES KEEP DISTANCE", scored = true, default = 1,
    choices = { { "ON", true }, { "OFF", false } }, desc = "Knife throwers hold 28-38 m and throw, backing off when you close in." },
  { id = "corpses", group = "ENEMIES", label = "BODIES", scored = false, default = 1,
    choices = { { "KEEP ALL", -1 }, { "FADE", 4 } }, desc = "Keep every body in the pit, or let them fade like the stock game." },
  -- ECONOMY
  { id = "loadout", group = "ECONOMY", label = "LOADOUT", scored = true, default = 1,
    choices = { { "SCAVENGER", 1 }, { "SUPPLIED", 2 }, { "STOCKED", 3 } },
    desc = "Scavenger: guns only from the dead. Supplied: a gun each breather. Stocked: a gun and ammo each breather." },
  { id = "healing", group = "ECONOMY", label = "HEALING", scored = true, default = 1,
    choices = { { "PER KILL", 1 }, { "GIFTS", 2 }, { "BOTH", 3 } },
    desc = "Per kill: each kill heals a little. Gifts: health between waves instead. Both: the two." },
  { id = "centre", group = "ECONOMY", label = "CENTRE DROPS", scored = true, default = 2,
    choices = { { "SCARCE", 1 }, { "NORMAL", 2 }, { "GENEROUS", 3 }, { "OFF", 0 } },
    desc = "The ammo box in the middle of the pit (every 60 / 40 / 25 s) and the armour after waves." },
  -- CROWD
  { id = "crowd", group = "CROWD", label = "CROWD", scored = false, default = 3,
    choices = { { "EMPTY", 0 }, { "SMALL 50", 50 }, { "NORMAL 150", 150 }, { "BIG 300", 300 }, { "HUGE 500", 500 } },
    desc = "Real spectators in the front rows (they cheer, boo and fidget). A big crowd costs frame time." },
  { id = "impostors", group = "CROWD", label = "BACK ROWS", scored = false, default = 1,
    choices = { { "FULL", 1000 }, { "EMPTY", 0 } }, desc = "The flat crowd on the seats behind the real spectators (free)." },
  { id = "favour", group = "CROWD", label = "CROWD FAVOUR", scored = true, default = 1,
    choices = { { "ON", true }, { "OFF", false } },
    desc = "Stylish kills win the crowd (gifts, sponsors); camping and spraying lose it (rushers, no breather)." },
  { id = "screams", group = "CROWD", label = "CROWD SCREAMS", scored = false, default = 1,
    choices = { { "OFF", false }, { "ON", true }, { "ONLY", "only" } },
    desc = "The crowd also yells with the game's own hit sounds (ONLY: instead of any Data/Sounds recordings)." },
  -- MARKERS
  { id = "markers", group = "MARKERS", label = "PICKUP MARKERS", scored = false, default = 1,
    choices = { { "ON", "v2" }, { "CLASSIC", "a7" }, { "OFF", false } },
    desc = "Diamonds and labels on the good pickups (ammo, armour, gifts). CLASSIC: a beacon over every pickup." },
  { id = "drop_markers", group = "MARKERS", label = "DROPPED GUNS", scored = false, default = 1,
    choices = { { "NEAR", "near" }, { "ALWAYS", "always" }, { "OFF", false } }, desc = "A faint dot on dropped guns." },
}
D.ROWS = ROWS
D.BY_ID = {}
for i, r in ipairs(ROWS) do r.index = i; D.BY_ID[r.id] = r end
D.GROUPS = { "RUN", "CHAOS", "PLAYERS", "ENEMIES", "ECONOMY", "CROWD", "MARKERS" }
D.GROUP_COL = { RUN = 1, CHAOS = 1, PLAYERS = 1, ENEMIES = 2, ECONOMY = 2, CROWD = 2, MARKERS = 2 }
-- the operations (presets): names and lines are ARENA_THEME.menu.ops; `short` is the launcher-style name for the logs
D.PRESETS = {
  { id = "classic", short = "CLASSIC 20", set = {}, key = "fixed20" },
  { id = "endless", short = "ENDLESS", set = { pacing = 4 }, key = "endless" },
  { id = "chaos", short = "CHAOS", set = { mutators = 2, elites = 2 }, key = "chaos_fixed20" },
  { id = "chaos_endless", short = "CHAOS ENDLESS", set = { pacing = 4, mutators = 2, elites = 2 }, key = "chaos_endless" },
  { id = "sprint", short = "SPRINT 10", set = { pacing = 2, boss = 2 }, key = "fixed10" },
  { id = "coop", short = "CO-OP", set = { players = 2 }, key = "coop_fixed20" },
  { id = "coop_chaos", short = "CO-OP CHAOS", set = { players = 2, mutators = 2, elites = 2 }, key = "coop_chaos_fixed20" },
}
for _, p in ipairs(D.PRESETS) do local o = T.ops[p.id] or {}; p.name, p.desc = o[1] or p.short, o[2] or "" end
D.PRESET_BY_ID = {}
for i, p in ipairs(D.PRESETS) do p.index = i; D.PRESET_BY_ID[p.id] = p end
-- the launchers' modes -> their preset (a run started from a .bat still gets the post-game, Retry and "change settings" on its preset)
D.MODE_PRESET = { arena = "classic", ["arena-endless"] = "endless", ["arena-chaos"] = "chaos", ["arena-chaos-endless"] = "chaos_endless",
                  ["arena-coop"] = "coop", ["arena-coop-chaos"] = "coop_chaos" }

function D.defaults()
  local v = {}
  for _, r in ipairs(ROWS) do v[r.id] = r.default end
  return v
end

function D.apply_preset(v, id)
  local p = D.PRESET_BY_ID[id]
  if not p then return v end
  for _, r in ipairs(ROWS) do
    if r.scored and r.id ~= "difficulty" then v[r.id] = p.set[r.id] or r.default end
  end
  return v
end

local function val(v, id) local r = D.BY_ID[id]; return r.choices[v[id] or r.default][2] end
D.val = val
function D.label(v, id) local r = D.BY_ID[id]; return r.choices[v[id] or r.default][1] end

-- the preset whose scored rows (difficulty aside) match these values; nil = custom
function D.preset_of(v)
  for _, p in ipairs(D.PRESETS) do
    local ok = true
    for _, r in ipairs(ROWS) do
      if r.scored and r.id ~= "difficulty" and (v[r.id] or r.default) ~= (p.set[r.id] or r.default) then ok = false; break end
    end
    if ok then return p end
  end
  return nil
end

local DIFF_SUFFIX = { [0] = "_trainee", [1] = "", [2] = "_00", [3] = "_007" }
D.DIFF_SUFFIX = DIFF_SUFFIX
function D.score_key(v)
  local p = D.preset_of(v)
  local base
  if p then base = p.key
  else
    local pace = val(v, "pacing")
    base = "custom_" .. (pace.mode == "endless" and "endless" or ("fixed" .. pace.waves))
  end
  return base .. DIFF_SUFFIX[val(v, "difficulty")]
end

-- the menu's values -> ARENA_SETTINGS (only what differs from the arena's own defaults is set, so a classic run is a plain run)
function D.settings(v)
  local S = {}
  local pace = val(v, "pacing")
  S.mode = pace.mode
  if pace.waves then S.waves = pace.waves end
  S.boss_every = val(v, "boss")
  local d = val(v, "difficulty")
  local cap = val(v, "cap")
  local heal = 1.0
  if d == 0 then S.hp_growth, S.acc_growth, S.boss_hp_mult, heal, cap = 0.07, 1.6, 2.0, 1.5, math.floor(cap * 0.75 + 0.5)
  elseif d == 2 then S.hp_growth, S.acc_growth, S.boss_hp_mult, heal, cap = 0.13, 3.2, 3.0, 0.8, math.floor(cap * 1.15 + 0.5)
  elseif d == 3 then S.hp_growth, S.acc_growth, S.boss_hp_mult, heal, cap, S.elite_from = 0.16, 4.0, 3.5, 0.6, math.floor(cap * 1.3 + 0.5), 5 end
  S.max_alive = cap
  S.max_alive_cap = math.max(cap, math.floor(cap * 3.1 + 0.5))
  S.start_delay = val(v, "start_delay")
  S.breather = val(v, "breather")
  if d == 0 then S.breather = S.breather + 3 end
  -- chaos
  S.mutators, S.elites = val(v, "mutators"), val(v, "elites")
  local k = val(v, "intensity")
  if k == 1 then S.mutator_chance, S.mutator_chance_inc, S.mutator_chance_max = 0.18, 0.01, 0.35
    S.affix_chance, S.affix_chance_inc, S.affix_chance_max = 0.025, 0.008, 0.15
  elseif k == 3 then S.mutator_from, S.mutator_chance, S.mutator_chance_inc, S.mutator_chance_max = 2, 0.45, 0.03, 0.80
    S.affix_from, S.affix_chance, S.affix_chance_inc, S.affix_chance_max = 2, 0.07, 0.018, 0.40
  elseif k == 4 then S.mutator_from, S.mutator_chance, S.mutator_chance_inc, S.mutator_chance_max, S.mutator_bosses = 1, 1.0, 0, 1.0, true
    S.affix_from, S.affix_chance, S.affix_chance_inc, S.affix_chance_max = 1, 0.12, 0.02, 0.50 end
  S.whacky = val(v, "whacky")
  -- players
  local coop = val(v, "players")
  if coop then
    S.coop = true
    local dn = val(v, "downed")
    if dn == "off" then S.coop_down = false else S.coop_bleedout = dn end
    S.coop_bot_skill = val(v, "bot_skill")
  end
  -- enemies
  S.knife_keep = val(v, "knife_keep")
  S.corpse_keep = val(v, "corpses")
  -- economy
  local lo = val(v, "loadout")
  S.give_guns, S.give_ammo = lo >= 2, lo >= 3
  local he = val(v, "healing")
  if he >= 2 then S.health_gifts = true; S.pickup_mode = "every" end
  local hk = (he == 2) and 0 or heal
  if hk ~= 1.0 then
    S.health_per_kill = { light = 0.03 * hk, normal = 0.03 * hk, armoured = 0.04 * hk, heavy = 0.06 * hk, boss = 0.25 * hk }
  end
  local ce = val(v, "centre")
  if ce == 0 then S.ammo_every, S.armour_center = 0, false
  elseif ce == 1 then S.ammo_every, S.armour_chance = 60, 0.10
  elseif ce == 3 then S.ammo_every, S.armour_chance, S.ammo_max_center = 25, 0.50, 4 end
  -- crowd
  S.crowd_real = val(v, "crowd")
  S.crowd_impostors = val(v, "impostors")
  S.crowd_favour = val(v, "favour")
  S.crowd_screams = val(v, "screams")
  -- markers
  local m = val(v, "markers")
  S.pickup_markers = m and true or false
  if m then S.marker_style = m end
  S.marker_drops = val(v, "drop_markers")
  S.score_key = D.score_key(v)
  local slots = nil
  if S.crowd_real > 150 then slots = 600 + (S.crowd_real - 150) + 100 end
  return S, coop, slots
end

-- ---- a tiny serializer: numbers, booleans, strings, tables with string keys or a list part ----
local RESERVED = {}
for w in ("and break do else elseif end false for function goto if in local nil not or repeat return then true until while"):gmatch("%a+") do RESERVED[w] = true end
local function ser(x)
  local t = type(x)
  if t == "number" then
    if x == math.floor(x) and math.abs(x) < 1e15 then return string.format("%d", x) end
    return string.format("%.10g", x)
  elseif t == "boolean" then return x and "true" or "false"
  elseif t == "string" then return string.format("%q", x)
  elseif t == "table" then
    local parts, keys = {}, {}
    for i = 1, #x do parts[#parts + 1] = ser(x[i]) end
    for k in pairs(x) do if type(k) == "string" then keys[#keys + 1] = k end end
    table.sort(keys)
    for _, k in ipairs(keys) do   -- (a key that is not a plain name goes in brackets, so it reads back)
      parts[#parts + 1] = ((k:match("^[%a_][%w_]*$") and not RESERVED[k]) and k or ("[" .. string.format("%q", k) .. "]")) .. "=" .. ser(x[k])
    end
    return "{" .. table.concat(parts, ",") .. "}"
  end
  return "nil"
end
D.serialize = ser

-- SECURITY: the text crosses stages (amenu.current().settings, amenu.result()) and any mode script can write it, mods included. An
-- empty environment is not enough: the string metatable is still reachable (("x"):rep(1e9) builds a gigabyte string) and a
-- function constructor can loop forever. So the text must be DATA: outside string literals no '(' (no call, no method on a
-- literal), no ':' and no 'function', no long strings ([[ ]]: %q never writes them); at most 16 KB.
local function data_only(text)
  if #text > 16384 then return false end
  local i, n = 1, #text
  while i <= n do
    local c = text:sub(i, i)
    if c == '"' or c == "'" then   -- a string literal: skip it (escapes included)
      i = i + 1
      while i <= n and text:sub(i, i) ~= c do
        if text:sub(i, i) == "\\" then i = i + 1 end
        i = i + 1
      end
    elseif c == "[" and text:match("^%[=*%[", i) then return false
    elseif c == "(" or c == ":" then return false
    elseif c:match("[%a_]") then
      local w = text:match("^[%w_]+", i)
      if w == "function" then return false end
      i = i + #w - 1
    end
    i = i + 1
  end
  return true
end
D.data_only = data_only

function D.parse(text)
  if type(text) ~= "string" or text == "" then return nil end
  if not data_only(text) then print("ge: arena: menu: refused a table text that is not plain data"); return nil end
  local f = load("return " .. text, "arena_menu", "t", {})
  if not f then return nil end
  local ok, r = pcall(f)
  if ok and type(r) == "table" then return r end
  return nil
end

D.MEDALS = ARENA_THEME.medals
