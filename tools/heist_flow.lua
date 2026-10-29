-- heist_flow.lua: the run side of the heist's start menu and debrief (shipped as Data/Scripts/modes/_heist_flow.lua; the generic menu
-- layer, port/src/port_uimenu.c). tools/mode_heist.lua includes it (with _heist_theme and _heist_menu_defs) before it reads
-- HEIST_SETTINGS. It:
--   * applies the settings the start menu (or AGAIN) launched this run with: the hand-off's text (ui.current) goes into HEIST_SETTINGS
--     UNDER a script's own (a test mode that sets HEIST_SETTINGS keeps its values)
--   * arms what follows the run: when it ends (the getaway, BUSTED, an abort from the watch) the bank loads again as the lobby (mode
--     heist-menu) on the debrief, instead of the stock mission report; the old full-screen debrief panel is not shown then
--   * builds the debrief's result from tools/heist_debrief.lua's report (HEIST.debrief_end's table) and the run's own state
-- Off (the stock end screen and the old debrief panel, nothing armed) in netplay, in automated runs (GE_FRAMES, GE_PLAYERBOT,
-- GE_INPUTPLAY) unless GE_HEIST_MENU=1, and with HEIST_SETTINGS.postgame = false. The result is Lua sim state; its text crosses the
-- stage load through C (ui.result).

local D = HEIST_MENU_DEFS
local U = UI_MENU
pcall(include, "_hub_defs")   -- the main menu hub's (a build without it has none): where a hub session's run returns to
HEIST_FLOW = {}
local F = HEIST_FLOW

local name = mode.name()
local automated = mode.env_flag("GE_FRAMES") or mode.env_flag("GE_PLAYERBOT") or mode.env_flag("GE_INPUTPLAY")
local cur = ui and ui.current() or nil
if cur and cur.mode ~= name then cur = nil end
local HS0 = type(HEIST_SETTINGS) == "table" and HEIST_SETTINGS or nil

F.on = ui ~= nil and U ~= nil and D ~= nil and not ui.netplay() and (not automated or mode.env_flag("GE_HEIST_MENU"))
       and not (HS0 and HS0.postgame == false)

-- the launched settings, under the script's own
local launched = cur and cur.settings ~= "" and U and U.parse(cur.settings) or nil
if launched then
  HEIST_SETTINGS = HEIST_SETTINGS or {}
  for k, v in pairs(launched) do if HEIST_SETTINGS[k] == nil then HEIST_SETTINGS[k] = v end end
  if launched.invincible and mods and mods.set then pcall(mods.set, "invincible", true) end
end
F.cheat = launched and launched.cheat or false
F.custom = launched and launched.custom or false
local here = D and D.job_here() or { id = "bank", lg = "bank", name = "JANUS BANK" }
F.job = (cur and cur.preset ~= "" and cur.preset) or here.id
local DIFFN = D and D.T.difficulty or { "AGENT", "SECRET AGENT", "00 AGENT", "007" }
F.label = (cur and cur.label ~= "" and cur.label) or string.format("%s  -  %s", here.name, DIFFN[mission.difficulty() + 1] or "")
F.settings_text = cur and cur.settings or ""
print(string.format("ge: heist: flow %s: mode %s job %s%s%s", F.on and "on" or "off", name, F.job,
  launched and string.format(", launched settings (%d bytes)", #F.settings_text) or "", cur and string.format(" via the menu (seq %d)", cur.seq or 0) or ""))
if launched then
  local keys = {}
  for k in pairs(launched) do keys[#keys + 1] = k end
  table.sort(keys)
  local parts = {}
  for _, k in ipairs(keys) do local v = launched[k]; if type(v) ~= "table" then parts[#parts + 1] = k .. "=" .. tostring(v) end end
  print("ge: heist: flow settings: " .. table.concat(parts, " "))
end

-- the run's launch, for AGAIN: the same mode, settings, difficulty and variables (the same level, the same deal number, the partner)
-- (a session the main menu hub started: the run is on the levelgen slot, and the hub's Control stage hosts the lobby that follows)
local function from_hub() return HUB_DEFS ~= nil and HUB_DEFS.from_hub() end

local function retry_spec()
  local stage = from_hub() and "levelgen" or nil
  if cur then
    return { mode = cur.mode, settings = cur.settings, preset = cur.preset, label = cur.label, difficulty = cur.difficulty or mission.difficulty(),
             env = cur.env or {}, stage = stage }
  end
  return { mode = name, settings = "", preset = F.job, label = F.label, difficulty = mission.difficulty(),
           env = { GE_LEVELGEN = levelgen and levelgen.current() or nil }, stage = stage }
end

local function lobby(page)
  if from_hub() then return HUB_DEFS.lobby("heist", page, F.job, F.label) end
  return { mode = "heist-menu", lobby = page, preset = F.job, label = F.label, env = { GE_LEVELGEN = levelgen and levelgen.current() or nil } }
end

local H   -- HEIST, once mode_heist has made it
local function base_result()
  local S = H and H.S or {}
  return { seq = cur and cur.seq or 0, job = F.job, label = F.label, difficulty = mission.difficulty(), cheat = F.cheat, custom = F.custom,
           crew = bond.count(), bar_value = S.bar_value or 0, money_value = S.money_value or 0 }
end

-- an abort from the watch (or anything else that ends the stage before the job does): the lobby on the debrief, JOB ABORTED
local function abort_result()
  local r = base_result()
  r.aborted, r.win = true, false
  if H then
    r.time = math.floor((H.elapsed or 0) * 100 + 0.5) / 100
    r.loot, r.bars, r.cash, r.alarm = H.loot or 0, H.bars or 0, H.cash or 0, H.hot and true or false
    r.alarm_at = H.hot_at and math.floor(H.hot_at) or nil
  end
  return r
end

local function crew_counts()
  local out, dead = 0, 0
  for p = 0, bond.count() - 1 do
    if bond.dead(p) then dead = dead + 1
    elseif H and H.zones and ((H.zones.exit_a and zone.contains(H.zones.exit_a, bond.id(p))) or (H.zones.exit_b and zone.contains(H.zones.exit_b, bond.id(p)))) then
      out = out + 1
    end
  end
  return out, dead
end

-- mode_heist.lua finish(): the job is over (R = tools/heist_debrief.lua's report, nil when the debrief is off)
function F.finish(win, msg, R, job)
  if not F.on then return end
  H = HEIST
  local S = H.S or {}
  local r = base_result()
  if job then r.job = job end
  R = R or {}
  local out, dead = crew_counts()
  r.win, r.aborted, r.msg = win and true or false, false, tostring(msg or "")
  r.rating = R.rating or (win and "COWBOY" or "BUSTED")
  r.payout = R.payout or H.score or 0
  r.loot, r.bars, r.cash = R.loot or H.loot or 0, R.bars or H.bars or 0, R.cash or H.cash or 0
  r.bonus, r.fines, r.planning, r.bribes = R.quiet_bonus or 0, R.fines or 0, R.planning or 0, R.bribe_cost or 0
  r.time = R.time or math.floor((H.elapsed or 0) * 100 + 0.5) / 100
  r.alarm, r.alarm_at = (R.alarm ~= nil) and R.alarm or (H.hot and true or false), R.alarm_at
  r.spotted, r.blown, r.cams, r.reports, r.bodies = R.spotted or 0, R.cover_blown or 0, R.camera_spots or 0, R.reports or 0, R.bodies_found or 0
  r.guards_ko, r.staff_ko, r.guards_killed = R.guards_ko or 0, R.staff_ko or 0, R.guards_killed or 0
  r.civ_hurt, r.civ_killed = R.civ_hurt or 0, R.civ_killed or (civilian and civilian.killed() or 0)
  r.hostages = R.hostages or 0
  r.crew_out, r.crew_dead = out, dead
  r.keys = R.keys or {}
  r.inside = R.route and #R.route > 0 or false
  r.vault = H.opened_once and true or false
  r.exit = R.exit
  r.route = R.route or {}
  local ml = {}
  for _, m in ipairs(R.medal_list or {}) do ml[#ml + 1] = { m[1], m[3] and true or false } end
  r.medals = ml
  local text = U.serialize(r)
  ui.result(text)
  ui.retry(retry_spec())
  ui.arm(lobby("postgame"))
  F.done = true
  print(string.format("ge: heist: flow finish: %s rating %s payout %d time %.1f loot %d (%d bars, %d cash) alarm %s; debrief armed (%d bytes)",
    win and "WON" or "BUSTED", r.rating, r.payout, r.time or 0, r.loot, r.bars, r.cash, tostring(r.alarm), #text))
end

-- the old full-screen panel (port_debrief.c) is the debrief only when this flow is off
function F.panel() return not F.on end

if F.on then
  local acc = 0
  on("stage_start", function()
    H = HEIST
    if bench and bench.sweep_off then pcall(bench.sweep_off) end
    ui.retry(retry_spec())
    ui.result(U.serialize(abort_result()))
    ui.arm(lobby("postgame"))   -- (an abort from the watch: the debrief says JOB ABORTED)
  end)
  on("tick", function(dt)
    if F.done or not H or H.phase == "off" then return end
    acc = acc + dt
    if acc >= 2 then acc = 0; ui.result(U.serialize(abort_result())) end   -- (the abort's numbers, kept current)
  end)
end
