-- The main menu hub scripted test (tests/hub/run.sh copies this to Data/Scripts/zz_hub_drive.lua: it runs at every stage start, in
-- the hub and in the runs it starts). Driven with the crew's board events (test.plan_input, sim side). The lobbies are HOSTED on the
-- hub's Control stage: the only stage loads are the boot and the runs. A counter in a ui pref file (memory only) tells the loads apart:
--   step 1  the hub on first boot (shot): ARENA (hosted) -> its pages -> MAIN MENU -> HEIST (hosted): the other job's card -> MAIN MENU ->
--           SETTINGS (a change) -> ARENA -> BEGIN: an Arena run                                   [no stage load until here]
--   step 2  the run: Bond is killed
--   step 3  the hub: the Arena post-game, hosted (shot): RETRY
--   step 4  the second run: Bond is killed
--   step 5  the hub: the post-game again: CHANGE SETTINGS, MAIN MENU -> HEIST (hosted) -> BEGIN THE JOB: a Heist run
--   step 6  the run (GE_HUB_TEST_KILL: heist_flowtest kills Bond): BUSTED
--   step 7  the hub: the Heist debrief, hosted (shot): MAIN MENU; the settings changed in step 1 are still there; GOLDENEYE 007: the Files screen
-- GE_HUB_TEST=exit: the hub on first boot picks EXIT and the game closes.
local name = mode.name()
local UP, DOWN, LEFT, RIGHT, SELECT, BACK, START = 1, 2, 4, 8, 16, 32, 64
local function log(fmt, ...) print(string.format("ge: hub: MENUTEST " .. fmt, ...)) end
local t, i = 0, 1

-- (this file runs once at the VM's start and once per stage: only the stage start counts)
local step = 0
on("stage_start", function()
  step = (tonumber(ui.prefs("hubtest").step) or 0) + 1
  ui.save_prefs("hubtest", { step = tostring(step) })
end)

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

local function music_now() local ok, m = pcall(music.current); return ok and tostring(m) or "?" end

local function hub_state(tag)
  local M = HUB_MENU
  local it = M.items[M.cursor] or {}
  local function row(id) return M.by[id] and M.label(id) or "-" end
  log("%s: page %s cursor %d (%s) | music_volume=%s sfx_volume=%s mouse_sens_x=%s", tag, M.page, M.cursor, tostring(it.label), row("music_volume"),
    row("sfx_volume"), row("mouse_sens_x"))
end

local function arena_state(tag)
  local M = ARENA_MENU
  local it = M.items[M.cursor] or {}
  log("%s: arena screen %s cursor %d (%s) busy %s | music %s | menu_up %s", tag, M.screen, M.cursor, tostring(it.label), tostring(M.busy), music_now(),
    tostring((amenu.state())))
end

local function heist_state(tag)
  local M = HEIST_MENU
  local it = M.items[M.cursor] or {}
  local ok, up = pcall(ui.state)
  log("%s: heist page %s cursor %d (%s) job %s planning %s | music %s | menu_up %s", tag, M.page, M.cursor, tostring(it.label), tostring(M.job),
    tostring(M.val("planning")), music_now(), tostring(ok and up))
end

local function hub_plan()
  if mode.env_flag("GE_HUB_TEST") then   -- (the value is not read: any setting of GE_HUB_TEST = the EXIT flow)
    return { { 1.6, "shot", "hub-main" }, { 1.7, function() hub_state("exit flow") end }, { 2.0, DOWN }, { 2.1, DOWN }, { 2.2, DOWN }, { 2.3, DOWN }, { 2.4, DOWN }, { 2.5, SELECT } }
  end
  if step == 1 then
    return {
      { 1.6, "shot", "hub-main" }, { 1.7, function() hub_state("boot") end },
      { 2.0, SELECT },                                                                                                -- ARENA (hosted)
      { 2.8, function() arena_state("arena hosted") end },
      { 3.0, function() ARENA_MENU.go("advanced") end }, { 3.4, function() arena_state("arena advanced") end },
      { 3.6, "shot", "arena-in-control" },
      { 3.8, BACK }, { 4.2, function() arena_state("arena back on main") end },
      { 4.4, BACK }, { 4.6, function() arena_state("arena exit row") end }, { 4.8, SELECT },                          -- MAIN MENU (no load)
      { 5.4, function() hub_state("hub after arena") end },
      { 5.6, DOWN }, { 5.8, SELECT },                                                                                 -- HEIST (hosted)
      { 6.6, function() heist_state("heist hosted") end }, { 6.7, "shot", "heist-in-control" },
      { 6.9, function()                                                                                               -- the other job's card
          local M = HEIST_MENU
          for _, x in ipairs(M.items) do if x.act == "job" and x.job == "casino" then M.activate(x, SELECT) end end
        end },
      { 7.3, function() heist_state("heist casino card") end },
      { 7.5, BACK }, { 7.9, function() heist_state("heist exit row") end }, { 8.1, SELECT },                          -- MAIN MENU (no load)
      { 8.7, function() hub_state("hub after heist") end },
      { 8.8, DOWN }, { 8.9, DOWN }, { 9.0, DOWN }, { 9.2, SELECT },                                                   -- SETTINGS (past SNIPER DUEL and GOLDENEYE 007)
      { 9.6, function() hub_state("settings before") end },
      { 9.8, RIGHT }, { 9.9, DOWN }, { 10.0, RIGHT }, { 10.1, DOWN }, { 10.3, RIGHT },                                -- music, sound, mouse X
      { 10.8, "shot", "hub-settings" }, { 10.9, function() hub_state("settings changed") end },
      { 11.2, BACK }, { 11.5, UP }, { 11.6, UP }, { 11.7, UP }, { 11.8, UP }, { 12.0, SELECT },                       -- main, ARENA (hosted)
      { 12.8, function() arena_state("arena again") end },
      { 13.0, SELECT },                                                                                               -- BEGIN: the run
    }
  elseif step == 3 then
    return { { 2.4, "shot", "arena-debrief" }, { 2.5, function() arena_state("arena debrief 1") end }, { 3.0, SELECT } }   -- RETRY
  elseif step == 5 then
    return {
      { 2.5, function() arena_state("arena debrief 2") end },
      { 2.8, BACK }, { 3.2, function() arena_state("arena new assignment") end },                                      -- the start menu again
      { 3.4, BACK }, { 3.6, SELECT }, { 4.2, function() hub_state("hub after debrief") end },                          -- MAIN MENU (no load)
      { 4.4, DOWN }, { 4.6, SELECT },                                                                                 -- HEIST (hosted)
      { 5.4, function()
          local M = HEIST_MENU
          M.set("planning", M.find_choice("planning", false))                                                         -- no planning board
          heist_state("heist start menu")
        end },
      { 5.6, RIGHT }, { 5.8, function() heist_state("heist begin") end }, { 6.0, SELECT },                            -- BEGIN THE JOB
    }
  elseif step == 7 then
    return {
      { 2.4, "shot", "heist-debrief" }, { 2.5, function() heist_state("heist debrief") end },
      { 2.8, DOWN }, { 2.9, DOWN }, { 3.1, function() heist_state("heist exit row") end }, { 3.3, SELECT },           -- MAIN MENU (no load)
      { 3.9, function() hub_state("hub 7") end },
      { 4.0, DOWN }, { 4.1, DOWN }, { 4.3, function() hub_state("goldeneye") end }, { 4.5, SELECT },                  -- GOLDENEYE 007 (past SNIPER DUEL)
    }
  end
  return {}
end

if name == "hub" then
  local go
  on("ui_tick", function(dt)
    if not HUB_MENU then return end
    if not go then
      local ac, uc = amenu.current(), ui.current()
      log("hub up: step %d, launched by %s lobby '%s' hub flag %s, music %s", step, ac and "amenu" or (uc and "ui" or "boot"),
        tostring((ac or uc or {}).lobby), tostring(ui.hub()), music_now())
      go = run_script(hub_plan())
    end
    go(dt)
  end)
elseif name == "arena" then
  local killed = false
  on("tick", function(dt)
    t = t + dt
    if not killed and t >= 4 then
      killed = true
      log("arena run (step %d): killing Bond, stage %s", step, tostring(mode.name()))
      test.coop_kill(0)
    end
  end)
elseif name == "heist_flowtest" or name == "heist" then
  log("heist run (step %d) up", step)
end
