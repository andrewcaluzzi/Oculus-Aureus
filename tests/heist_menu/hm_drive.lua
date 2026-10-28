-- The heist start menu / debrief scripted test (tests/heist_menu/run.sh copies this to Data/Scripts/zz_hm_drive.lua: it runs at every
-- stage start, in the lobby and in the runs). The flow, driven with the crew's board events (test.plan_input, sim side):
--   1. the lobby on first boot: the main page (shot), the difficulty up one (SECRET AGENT), Q BRANCH: the deal AS WRITTEN, guard
--      personalities UNIFORM (shot), back, BEGIN THE JOB
--   2. run 1 (heist_flowtest.lua: the win path): the planning board: buy the silenced PP7, START; the win path wins
--   3. the debrief (shot): AGAIN
--   4. run 2: the same settings (run.sh compares the "flow settings" lines), the planning board again, START; Bond is killed: BUSTED
--   5. the debrief (shot): NEW JOB -> the briefing with the run's settings (shot), then RETURN TO MI6
-- GE_HM_CASINO=1 (run.sh casino): the lobby moves to the casino (its card), BEGIN there, Bond is killed: BUSTED on the casino, quit.
local name = mode.name()
local UP, DOWN, LEFT, RIGHT, SELECT, BACK, START = 1, 2, 4, 8, 16, 32, 64
local function log(fmt, ...) print(string.format("ge: heist: MENUTEST " .. fmt, ...)) end
local t, i = 0, 1

local function run_script(plan)
  return function(dt)
    t = t + dt
    local s = plan[i]
    while s and t >= s[1] do
      i = i + 1
      if type(s[2]) == "number" then test.plan_input(0, s[2])
      elseif s[2] == "shot" then test.screenshot(); log("shot %s", s[3])
      elseif type(s[2]) == "function" then s[2]() end
      s = plan[i]
    end
  end
end

local function state(tag)
  local M = HEIST_MENU
  local parts = {}
  for _, r in ipairs(HEIST_MENU_DEFS.ROWS) do parts[#parts + 1] = r.id .. "=" .. M.label(r.id) end
  local it = M.items[M.cursor] or {}
  log("%s: page %s cursor %d (%s) job %s level %s | %s", tag, M.page, M.cursor, tostring(it.label), M.job, levelgen.current(), table.concat(parts, " "))
  log("%s mouse: menu_up %s", tag, tostring((ui.state())))
end

local function lobby_plan()
  local M = HEIST_MENU
  if mode.env_flag("GE_HM_CASINO") then
    if M.page == "debrief" then   -- back to the briefing, the bank's card: the lobby moves back to the bank
      return { { 2.4, "shot", "debrief-casino" }, { 2.5, function() state("casino debrief") end }, { 3.0, BACK }, { 3.4, LEFT }, { 3.7, SELECT } }
    elseif levelgen.current() == "bank" and ui.current() then   -- the bank again, after the casino: quit
      return { { 1.8, "shot", "menu-bank-again" }, { 1.9, function() state("bank again") end }, { 2.2, BACK }, { 2.5, SELECT } }
    elseif levelgen.current() == "casino" then   -- the difficulty up one (SECRET AGENT), BEGIN on the casino
      return { { 1.8, "shot", "menu-casino" }, { 1.9, function() state("casino lobby") end },
               { 2.0, LEFT }, { 2.1, DOWN }, { 2.2, DOWN }, { 2.3, RIGHT }, { 2.5, function() state("casino begin") end }, { 2.6, DOWN }, { 2.7, RIGHT }, { 2.9, SELECT } }
    end
    return { { 1.5, function() state("bank lobby") end }, { 1.7, LEFT }, { 1.8, DOWN }, { 2.0, SELECT } }          -- the casino card
  end
  if M.page == "debrief" then
    local r = UI_MENU.parse(ui.result()) or {}
    if r.win then
      log("debrief 1: win %s rating %s payout %s", tostring(r.win), tostring(r.rating), tostring(r.payout))
      return { { 2.4, "shot", "debrief-win" }, { 4.0, "shot", "debrief-win-late" }, { 4.1, function() state("debrief 1") end }, { 4.5, SELECT } }   -- AGAIN
    end
    log("debrief 2: win %s aborted %s rating %s", tostring(r.win), tostring(r.aborted), tostring(r.rating))
    return { { 3.0, "shot", "debrief-busted" }, { 3.1, function() state("debrief 2") end },
             { 3.4, RIGHT }, { 3.6, SELECT },                                                                   -- NEW JOB
             { 4.6, "shot", "menu-after" }, { 4.7, function() state("after") end },
             { 5.0, BACK }, { 5.3, SELECT } }                                                                   -- RETURN TO MI6
  end
  return {
    { 1.6, "shot", "menu-main" }, { 1.7, function() state("boot") end },
    { 2.0, LEFT }, { 2.1, DOWN }, { 2.2, DOWN }, { 2.3, RIGHT }, { 2.35, LEFT },                               -- difficulty: change, back to AGENT
    { 2.4, DOWN }, { 2.5, SELECT },                                                                            -- Q BRANCH        (the win path's checks are Agent's)
    { 3.4, DOWN }, { 3.5, RIGHT },                                                                             -- the deal AS WRITTEN
    { 3.6, DOWN }, { 3.7, DOWN }, { 3.8, DOWN }, { 3.9, DOWN }, { 4.0, DOWN }, { 4.1, RIGHT },                 -- personalities UNIFORM
    { 5.0, "shot", "menu-settings" }, { 5.1, function() state("settings") end },
    { 5.3, BACK }, { 6.2, "shot", "menu-main-custom" }, { 6.3, function() state("main") end },
    { 6.5, RIGHT }, { 6.8, SELECT },                                                                           -- BEGIN THE JOB
  }
end

if name == "heist-menu" then
  local step
  on("ui_tick", function(dt)
    if not HEIST_MENU then return end
    if not step then step = run_script(lobby_plan()) end
    step(dt)
  end)
elseif name == "heist_flowtest" or name == "heist" then
  local cur = ui.current()
  local n = cur and cur.seq or 0
  log("run seq %d (%s): %d Bonds, difficulty %d, level %s", n, name, bond.count(), mission.difficulty(), levelgen.current())
  local stated, conf, killed = false, false, false
  on("tick", function(dt)
    t = t + dt
    if not stated and t >= 3 then
      stated = true
      local up, ptr, cap = ui.state()
      log("run %d mouse: menu_up %s gameplay_pointer %s captured %s", n, tostring(up), tostring(ptr), tostring(cap))
    end
    if not conf and PLANNING and PLANNING.on and t >= 2.0 then   -- the planning board: buy the PP7 (the first LOADOUT row), go
      conf = true
      log("planning: buy and confirm")
      test.plan_input(0, SELECT)
      test.plan_input(0, START)
    end
    if mode.env_flag("GE_HM_CASINO") and not killed and t >= 8 and HEIST and HEIST.ready then
      killed = true
      log("casino: killing Bond")
      test.coop_kill(0)
    end
  end)
end
