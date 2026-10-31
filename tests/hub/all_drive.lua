-- The consolidated build's hub smoke (tests/hub/win_all.sh copies this to Data/Scripts/zz_all_drive.lua: it runs at every stage start).
-- GE_HUBGO_<WHAT>=1 picks what the hub opens (Lua has no os library: mode.env_flag), through the real menu items (M.activate, as
-- hub_drive.lua's job card does):
--   ARENA                the ARENA tile, BEGIN: an Arena run on the Colosseum
--   BANK / CASINO        the HEIST tile, that job's card, no planning board, BEGIN THE JOB
--   RIDGE / SURFACE / SNOWFIELD / JUNGLE   the SNIPER DUEL tile, that stage's card, BEGIN THE DUEL
--   DAMPLUS              the DAM+ tile, BEGIN THE MISSION (the stock Dam mission with GE_DAMPLUS)
--   GOLDENEYE            the GOLDENEYE 007 tile: the Files screen
--   SHOTS                pictures: the hub, the SNIPER DUEL card (each stage's card picked in turn), the DAM+ card; then nothing
-- GE_HUBKILL=1: in the run, Bond is killed 25 seconds in (the run's end hands back to the hub: its debrief).
local name = mode.name()
local GO = ""
for _, g in ipairs({ { "ARENA", "arena" }, { "BANK", "heist:bank" }, { "CASINO", "heist:casino" }, { "RIDGE", "sniper:ridge" },
                     { "SURFACE", "sniper:surface" }, { "SNOWFIELD", "sniper:snowfield" }, { "JUNGLE", "sniper:jungle" },
                     { "DAMPLUS", "damplus" }, { "GOLDENEYE", "goldeneye" }, { "SHOTS", "shots" } }) do
  if mode.env_flag("GE_HUBGO_" .. g[1]) then GO = g[2] end
end
local SELECT, RIGHT = 16, 8
local function log(fmt, ...) print(string.format("ge: hub: ALLTEST " .. fmt, ...)) end
local function shot(tag) test.screenshot(); log("shot %s", tag) end

local function tile(id)
  local M = HUB_MENU
  for i, it in ipairs(M.items) do if it.act == "tile" and it.tile == id then M.cursor = i; M.dirty = true; return it end end
end

local function card(M, key, val)
  for i, it in ipairs(M.items) do if it[key] == val then M.cursor = i; M.dirty = true; return it end end
end

local function plan_for(go)
  local what, arg = go:match("^(%a+):?(%a*)$")
  local P = {}
  if what == "shots" then
    P = {
      { 1.4, function() tile("arena") end }, { 1.8, function() shot("hub") end },
      { 2.0, function() HUB_MENU.activate(tile("sniper")) end },
      { 3.0, function() shot("sniper-menu") end },
    }
    local t = 3.4
    for _, id in ipairs(SNIPER_DEFS.STAGE_ORDER) do
      P[#P + 1] = { t, function() local it = card(SNIPER_MENU, "stage", id); SNIPER_MENU.activate(it) end }
      P[#P + 1] = { t + 0.6, function() shot("sniper-card-" .. id) end }
      t = t + 1.0
    end
    P[#P + 1] = { t, function() SNIPER_MENU.activate(card(SNIPER_MENU, "act", "quit")) end }
    P[#P + 1] = { t + 0.8, function() tile("damplus"); HUB_MENU.dirty = true end }
    P[#P + 1] = { t + 1.2, function() shot("hub-damplus-tile") end }
    P[#P + 1] = { t + 1.4, function() HUB_MENU.activate(tile("damplus")) end }
    P[#P + 1] = { t + 2.4, function() shot("damplus-menu") end }
    P[#P + 1] = { t + 2.6, function() log("shots done") end }
  elseif what == "arena" then
    P = { { 1.6, function() HUB_MENU.activate(tile("arena")) end }, { 2.8, function() test.plan_input(0, SELECT) end } }   -- BEGIN (the cursor's start)
  elseif what == "heist" then
    P = {
      { 1.6, function() HUB_MENU.activate(tile("heist")) end },
      { 2.6, function()
          local M = HEIST_MENU
          for _, x in ipairs(M.items) do if x.act == "job" and x.job == arg then M.activate(x, SELECT) end end
        end },
      { 3.2, function()
          local M = HEIST_MENU
          M.set("planning", M.find_choice("planning", false))
          log("heist card: job %s", tostring(M.job))
          local it = M.items[M.find_act("start") or 1]
          M.activate(it, SELECT)
        end },
    }
  elseif what == "sniper" then
    P = {
      { 1.6, function() HUB_MENU.activate(tile("sniper")) end },
      { 2.6, function() SNIPER_MENU.activate(card(SNIPER_MENU, "stage", arg)) end },
      { 3.0, function() log("sniper card: %s", arg); SNIPER_MENU.activate(card(SNIPER_MENU, "act", "start")) end },
    }
  elseif what == "damplus" then
    P = {
      { 1.6, function() HUB_MENU.activate(tile("damplus")) end },
      { 2.6, function() DAMPLUS_MENU.activate(card(DAMPLUS_MENU, "act", "start")) end },
    }
  elseif what == "goldeneye" then
    P = { { 1.6, function() HUB_MENU.activate(tile("goldeneye")) end } }
  end
  return P
end

if name == "hub" then
  local t, i, P = 0, 1, nil
  on("ui_tick", function(dt)
    if not HUB_MENU then return end
    if not P then
      local c = ui.current()
      log("hub up: lobby '%s', go '%s'", tostring(c and c.lobby), GO)
      if c and c.lobby ~= "" and c.lobby ~= nil then P = {} else P = plan_for(GO) end
    end
    t = t + dt
    local s = P[i]
    while s and t >= s[1] do i = i + 1; s[2](); s = P[i] end
  end)
else
  local kill = mode.env_flag("GE_HUBKILL") and 25 or nil
  local snap = mode.env_flag("GE_HUBSHOT") and 12 or nil   -- (GE_HUBSHOT=1: one picture of the run, 12 s in)
  local t, done, snapped = 0, false, false
  log("run up: mode '%s'", tostring(name))
  on("tick", function(dt)
    t = t + dt
    if kill and not done and t >= kill then done = true; log("killing Bond at %.0f s", t); test.coop_kill(0) end
    if snap and not snapped and t >= snap then snapped = true; shot("run") end
  end)
end
