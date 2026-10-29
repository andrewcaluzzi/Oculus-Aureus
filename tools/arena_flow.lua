-- arena_flow.lua: the run side of the arena's start menu and post-game (shipped as Data/Scripts/modes/_arena_flow.lua; modding/arena.md
-- "Start menu and post-game"). mode_arena.lua includes it first thing, before it reads ARENA_SETTINGS. It:
--   * applies the settings the start menu (or Retry) launched this run with: the hand-off text from amenu.current() goes into
--     ARENA_SETTINGS over the preset file's own (port/src/port_arenamenu.c keeps it across the stage load)
--   * gives a launcher's run (Play Arena (Chaos).bat ...) its preset's best-score key, so the menu shows bests per preset
--   * keeps the run's stats (headshots, best multi-kill, best streak, accuracy, damage taken, the crowd's peak, kills and time per wave)
--   * arms what follows the run: when it ends (death, the win, an abort from the watch) the Colosseum loads again as the lobby
--     (mode arena-menu) on the post-game (or, after an abort, the start menu) instead of the stock mission report. A win waits
--     win_delay seconds (the crowd's cheer) before the mission ends.
-- Off (the stock end screen, nothing armed) in netplay, in the benchmark presets, in automated runs (GE_FRAMES, GE_PLAYERBOT,
-- GE_INPUTPLAY) unless GE_ARENA_MENU=1, and with ARENA_SETTINGS.postgame = false. The stats are Lua sim state; the result text
-- crosses the stage load through C.

include("_arena_menu_defs")   -- (it includes the maps' table, tools/arena_maps.lua, too)
pcall(include, "_hub_defs")   -- the main menu hub's (a build without it has none): where a hub session's run returns to
local D = ARENA_MENU_DEFS
ARENA_FLOW = {}
local F = ARENA_FLOW

local name = mode.name()
local automated = mode.env_flag("GE_FRAMES") or mode.env_flag("GE_PLAYERBOT") or mode.env_flag("GE_INPUTPLAY")
local cur = amenu and amenu.current() or nil
if cur and cur.mode ~= name then cur = nil end
local AS = type(ARENA_SETTINGS) == "table" and ARENA_SETTINGS or nil

F.on = amenu ~= nil and not amenu.netplay() and not name:match("^arena%-bench") and (not automated or mode.env_flag("GE_ARENA_MENU"))
       and not (AS and AS.postgame == false)
local WIN_DELAY = (AS and AS.win_delay) or 4.0

-- the launched settings, over whatever the preset file set
local launched = cur and cur.settings ~= "" and D.parse(cur.settings) or nil
if launched then
  ARENA_SETTINGS = ARENA_SETTINGS or {}
  for k, v in pairs(launched) do ARENA_SETTINGS[k] = v end
  AS = ARENA_SETTINGS
end
local preset_id = (cur and cur.preset ~= "" and cur.preset) or D.MODE_PRESET[name]
local preset = preset_id and D.PRESET_BY_ID[preset_id]
if not launched and preset and preset_id ~= "classic" and preset_id ~= "endless" then   -- (a launcher's run: its preset's bests)
  ARENA_SETTINGS = ARENA_SETTINGS or {}
  if ARENA_SETTINGS.score_key == nil then ARENA_SETTINGS.score_key = preset.key end
  AS = ARENA_SETTINGS
end
F.label = (cur and cur.label ~= "" and cur.label) or (preset and preset.name) or string.upper(name)
F.preset = preset_id or ""
F.settings_text = cur and cur.settings or ""
print(string.format("ge: arena: flow %s: mode %s preset %s%s%s", F.on and "on" or "off", name, F.preset,
  launched and string.format(", launched settings (%d bytes)", #F.settings_text) or "", cur and (" via " .. (cur.lobby ~= "" and cur.lobby or "launch")) or ""))
if launched then
  local keys = {}
  for k in pairs(launched) do keys[#keys + 1] = k end
  table.sort(keys)
  local parts = {}
  for _, k in ipairs(keys) do local v = launched[k]; if type(v) ~= "table" then parts[#parts + 1] = k .. "=" .. tostring(v) end end
  print("ge: arena: flow settings: " .. table.concat(parts, " "))
end

-- ---- the stats ----
local now = 0
local ST = { headshots = 0, multi = 0, streak = 0, damage = 0, waves = {} }
local per = {}
local cur_wave = nil
local function B(p) local b = per[p]; if not b then b = { chain = 0, last = -1e9, streak = 0 }; per[p] = b end; return b end

function F.wave_start(w, title, boss, mut, kills, t)
  cur_wave = { w = w, title = title, boss = boss, mut = mut and true or false, k0 = kills, t0 = t }
end

function F.wave_cleared(w, kills, t)
  if cur_wave and cur_wave.w == w then
    ST.waves[#ST.waves + 1] = { kills - cur_wave.k0, math.floor(t - cur_wave.t0 + 0.5), cur_wave.boss and 1 or (cur_wave.mut and 2 or 0) }
    cur_wave = nil
  end
end

-- the run's launch, for Retry: the same mode, the same settings, the same players
-- (a session the main menu hub started: its run is on the Colosseum slot, and the hub's Control stage hosts the lobby that follows)
local function from_hub() return HUB_DEFS ~= nil and HUB_DEFS.from_hub() end

-- a run on a stock multiplayer map (settings.map, tools/arena_maps.lua) has the map's stage; its retry loads that again and the lobby
-- that follows it is on the Colosseum (the hub's own stage in a hub session)
local MAPDEF = AS and AS.map and ARENA_MAPS.BY_ID[AS.map] or nil
if MAPDEF and not MAPDEF.stage then MAPDEF = nil end

local function retry_spec()
  return { mode = name, settings = F.settings_text, preset = F.preset, label = F.label, coop = mode.env_flag("GE_COOP"),
           slots = cur and cur.slots or nil, stage = (MAPDEF and MAPDEF.stage) or (from_hub() and "colosseum" or nil) }
end

local function lobby(page)
  if from_hub() then
    local l = HUB_DEFS.lobby("arena", page, F.preset, F.label)
    l.coop = false
    return l
  end
  return { mode = "arena-menu", lobby = page, preset = F.preset, label = F.label, coop = false, stage = MAPDEF and "colosseum" or nil }
end

function F.finish(win, r)
  if not F.on then return nil end
  local sc = r.sc or {}
  local fired, hits = 0, 0
  for p = 0, bond.count() - 1 do
    local f, h = bond.shots(p)
    fired, hits = fired + (f or 0), hits + (h or 0)
  end
  if cur_wave and not win then   -- the wave he fell in
    ST.waves[#ST.waves + 1] = { r.kills - cur_wave.k0, math.floor(r.time - cur_wave.t0 + 0.5), 3 }
  end
  local news = {}
  for _, n in ipairs(sc.news or {}) do news[#news + 1] = n end
  local live = ARENA_CROWD and ARENA_CROWD.live
  local res = {
    win = win and true or false, wave = r.wave, waves = r.waves or 0, kills = r.kills, time = r.time, title = r.title or "",
    shots = fired, hits = hits, headshots = ST.headshots, multi = ST.multi, streak = ST.streak, damage = math.floor(ST.damage * 100 + 0.5), hits_taken = ST.hits_taken or 0,
    peak = live and live.on and math.floor((live.peak or 0) + 0.5) or -1, medal = sc.medal or 0, news = news,
    first = sc.hud2 == "FIRST RUN RECORDED", recorded = sc.hud2 ~= nil and sc.hud2 ~= "",
    medal_new = F.prev_medal ~= nil and (sc.medal or 0) > F.prev_medal,
    elites = r.elites or 0, mutators = r.mutators or 0, label = F.label, preset = F.preset, coop = bond.count() > 1,
    key = (AS and AS.score_key) or "",
  }
  if r.plus then   -- arena plus: the credits and the requisitions ({ { name, count }, ... }, the few most stacked)
    res.credits, res.spent, res.cards = r.plus.earned, r.plus.spent, r.plus.cards
  end
  local list = {}
  local from = math.max(1, #ST.waves - 63)
  for i = from, #ST.waves do list[#list + 1] = ST.waves[i] end
  res.wave_list = list
  local text = D.serialize(res)
  amenu.result(text)
  amenu.retry(retry_spec())
  amenu.arm(lobby("postgame"))
  print(string.format("ge: arena: flow finish: %s wave %d kills %d time %.1f acc %d/%d hs %d multi %d streak %d peak %d medal %d; post-game armed (%d bytes)",
    win and "WON" or "FELL", r.wave, r.kills, r.time, hits, fired, ST.headshots, ST.multi, ST.streak, res.peak, res.medal, #text))
  if win then
    F.win_at = now + WIN_DELAY
    F.win_text = string.format("Arena cleared: %d kills in %d:%02d", r.kills, math.floor(r.time) // 60, math.floor(r.time) % 60)
    return true
  end
  return nil
end

if F.on then
  on("stage_start", function()
    if bench and bench.sweep_off then pcall(bench.sweep_off) end
    if scores and scores.enabled and scores.enabled() and ARENA_SCORES and ARENA_SCORES.prefix then   -- (the debrief's NEW medal)
      local a = AS or {}
      local key = ARENA_SCORES.prefix({ score_key = a.score_key, mode = a.mode or "fixed", waves = a.waves or 20, map = a.map })
      F.prev_medal = scores.get(key .. "_best_medal") or 0
    end
    amenu.retry(retry_spec())
    amenu.arm(lobby("menu"))   -- (an abort from the watch: back to the start menu)
  end)
  on("tick", function(dt)
    now = now + dt
    if F.win_at and now >= F.win_at then
      F.win_at = nil
      mission.win(F.win_text)
    end
  end)
  on("chr_died", function(victim, killer)
    local part, _, p = spawn.last_hit(victim)
    if p == nil and killer ~= nil and killer ~= CHR_NONE then p = bond.player(killer) end
    if p == nil then return end
    if part == "head" then ST.headshots = ST.headshots + 1 end
    local b = B(p)
    if now - b.last <= 2.0 then b.chain = b.chain + 1 else b.chain = 1 end
    b.last = now
    b.streak = b.streak + 1
    if b.chain > ST.multi then ST.multi = b.chain end
    if b.streak > ST.streak then ST.streak = b.streak end
  end)
  on("damage", function(victim, attacker, amount)
    local p = bond.player(victim)
    if p == nil or (amount or 0) <= 0 then return end
    B(p).streak = 0
    ST.damage = ST.damage + amount
    ST.hits_taken = (ST.hits_taken or 0) + 1
  end)
end
