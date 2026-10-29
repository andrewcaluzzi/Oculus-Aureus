-- The main menu hub scripted test (tests/hub/run.sh copies this to Data/Scripts/zz_hub_drive.lua: it runs at every stage start, in
-- the hub and in the lobbies it opens). Driven with the crew's board events (test.plan_input, sim side). Each stage load is one
-- hand-off, counted by ui.current().seq (the hub's own boot has none):
--   seq 0  the hub on first boot (shot): ARENA
--   seq 1  the arena lobby: its exit row says MAIN MENU (shot): BACK, MAIN MENU
--   seq 2  the hub again: HEIST
--   seq 3  the heist lobby: MAIN MENU
--   seq 4  the hub: SETTINGS (shot): music and sound volume up one step, mouse look X up one step; back; ARENA
--   seq 5  the arena lobby: MAIN MENU
--   seq 6  the hub: the settings it changed are still what the port holds (a stage load later); GOLDENEYE 007: the Files screen
-- GE_HUB_TEST=exit: the hub on first boot picks EXIT and the game closes.
local name = mode.name()
local UP, DOWN, LEFT, RIGHT, SELECT, BACK, START = 1, 2, 4, 8, 16, 32, 64
local function log(fmt, ...) print(string.format("ge: hub: MENUTEST " .. fmt, ...)) end
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

local function hub_state(tag)
  local M = HUB_MENU
  local it = M.items[M.cursor] or {}
  local function row(id) return M.by[id] and M.label(id) or "-" end
  log("%s: page %s cursor %d (%s) | music_volume=%s sfx_volume=%s mouse_sens_x=%s", tag, M.page, M.cursor, tostring(it.label), row("music_volume"),
    row("sfx_volume"), row("mouse_sens_x"))
end

local function hub_plan(seq)
  if mode.env_flag("GE_HUB_TEST") then   -- (the value is not read: any setting of GE_HUB_TEST = the EXIT flow)
    return { { 1.6, "shot", "hub-main" }, { 1.7, function() hub_state("exit flow") end }, { 2.0, DOWN }, { 2.1, DOWN }, { 2.2, DOWN }, { 2.3, DOWN }, { 2.5, SELECT } }
  end
  if seq == 0 then
    return { { 1.6, "shot", "hub-main" }, { 1.7, function() hub_state("boot") end }, { 2.0, SELECT } }                       -- ARENA
  elseif seq == 2 then
    return { { 1.6, function() hub_state("hub 2") end }, { 1.8, DOWN }, { 2.0, SELECT } }                                    -- HEIST
  elseif seq == 4 then
    return { { 1.6, function() hub_state("hub 4") end },
             { 1.8, DOWN }, { 1.9, DOWN }, { 2.0, DOWN }, { 2.2, SELECT },                                                   -- SETTINGS
             { 2.6, function() hub_state("settings before") end },
             { 2.8, RIGHT }, { 2.9, DOWN }, { 3.0, RIGHT }, { 3.1, DOWN }, { 3.3, RIGHT },                                  -- music, sound, mouse X
             { 3.8, "shot", "hub-settings" }, { 3.9, function() hub_state("settings changed") end },
             { 4.2, BACK }, { 4.6, UP }, { 4.7, UP }, { 4.8, UP }, { 5.0, SELECT } }                                         -- main, ARENA
  elseif seq == 6 then
    return { { 1.6, "shot", "hub-after" }, { 1.7, function() hub_state("hub 6") end },
             { 2.0, DOWN }, { 2.1, DOWN }, { 2.3, function() hub_state("goldeneye") end }, { 2.5, SELECT } }                  -- GOLDENEYE 007
  end
  return {}
end

local function lobby_plan(seq, M)
  local function quit_label() for _, x in ipairs(M.items) do if x.act == "quit" then return x.label end end return "?" end
  return { { 1.2, function() log("%s lobby (seq %d): exit row '%s'", name, seq, quit_label()) end },
           { 1.5, "shot", name .. "-from-hub" }, { 1.7, BACK },
           { 2.0, function() local x = M.items[M.cursor]; log("%s lobby: cursor on '%s'", name, tostring(x and x.label)) end }, { 2.3, SELECT } }
end

local cur = ui.current()
local seq = cur and cur.seq or 0
local step
if name == "hub" then
  on("ui_tick", function(dt)
    if not HUB_MENU then return end
    if not step then
      log("hub up: launched with stage '%s' seq %d hub flag %s", tostring(cur and cur.stage or "boot"), seq, tostring(ui.hub()))
      step = run_script(hub_plan(seq))
    end
    step(dt)
  end)
elseif name == "arena-menu" then
  on("ui_tick", function(dt)
    if not ARENA_MENU then return end
    if not step then
      log("arena lobby up: seq %d, launched with stage '%s', hub flag %s", seq, tostring(cur and cur.stage), tostring(ui.hub()))
      step = run_script(lobby_plan(seq, ARENA_MENU))
    end
    step(dt)
  end)
elseif name == "heist-menu" then
  on("ui_tick", function(dt)
    if not HEIST_MENU then return end
    if not step then
      log("heist lobby up: seq %d, launched with stage '%s', level %s, hub flag %s", seq, tostring(cur and cur.stage), levelgen.current(), tostring(ui.hub()))
      step = run_script(lobby_plan(seq, HEIST_MENU))
    end
    step(dt)
  end)
end
