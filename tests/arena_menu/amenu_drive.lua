-- The arena start menu / post-game scripted test (tests/arena_menu/run.sh copies this to Data/Scripts/zz_amenu_drive.lua: it runs at
-- every stage start, in the lobby and in the runs). The flow it drives, with the crew's board events (test.plan_input, sim side):
--   1. the lobby on first boot (the start menu): pick SPRINT 10, difficulty 00 AGENT, then ADVANCED: wave mutators ON, whacky MILD;
--      back, ENTER THE ARENA (screenshots of the main page and the advanced page)
--   2. run 1: the test keys jump the 10 waves (GE_ARENA_TESTKEYS=1): a WIN, the post-game after the crowd's cheer
--   3. the post-game (screenshot): RETRY
--   4. run 2: the same settings (run.sh compares the "flow settings" lines); Bond is killed (test.coop_kill): the post-game again
--   5. the post-game (screenshot): CHANGE SETTINGS -> the start menu with the run's settings (screenshot), then QUIT TO TITLE
-- The runs are told apart by the in-memory best-score counter of the run's key (GE_NOSCORESAVE: never written).
local name = mode.name()
local UP, DOWN, LEFT, RIGHT, SELECT, BACK = 1, 2, 4, 8, 16, 32
local KEY = "custom_fixed10_00"
local function runs() return (scores and scores.enabled() and scores.get(KEY .. "_runs")) or 0 end
local function log(fmt, ...) print(string.format("ge: arena: MENUTEST " .. fmt, ...)) end
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
  local M = ARENA_MENU
  local D = ARENA_MENU_DEFS
  local parts = {}
  for _, r in ipairs(D.ROWS) do parts[#parts + 1] = r.id .. "=" .. D.label(M.v, r.id) end
  local p = D.preset_of(M.v)
  log("%s mouse: menu_up %s", tag, tostring((amenu.state())))
  log("%s: screen %s cursor %d preset %s key %s | %s", tag, M.screen, M.cursor, p and p.id or "custom", D.score_key(M.v), table.concat(parts, " "))
end

if mode.env_flag("GE_AMENU_BATTEST") then
  -- the launcher variant (run.sh <frames> bat): GE_MODE=arena-chaos as Play Arena (Chaos).bat starts it -> Bond killed -> the debrief
  -- -> AGAIN, 007? (the same preset mode again) -> killed -> the debrief -> quit
  local function n() return (scores and scores.enabled() and scores.get("chaos_fixed20_runs")) or 0 end
  if name == "arena-menu" then
    on("ui_tick", run_script({ { 2.0, function() state(string.format("bat postgame %d", math.floor(n()))) end },
      { 2.5, function() if n() >= 2 then test.plan_input(0, BACK) else test.plan_input(0, SELECT) end end },
      { 3.2, function() if n() >= 2 then test.plan_input(0, BACK) end end },
      { 3.5, function() if n() >= 2 then test.plan_input(0, SELECT) end end } }))
  elseif name == "arena-chaos" then
    on("tick", function(dt)
      t = t + dt
      if i == 1 and t >= 4 then i = 2; log("bat run %d", n() + 1); test.coop_kill(0) end
    end)
  end
elseif mode.env_flag("GE_AMENU_COOPTEST") then
  -- the co-op variant (run.sh coop): DOUBLE-O SECTION from the menu -> a run with two Bonds (the bot) -> both killed -> the debrief -> quit
  if name == "arena-menu" then
    local go
    on("ui_tick", function(dt)
      if not go then
        local post = ARENA_MENU and ARENA_MENU.screen == "postgame"
        log("coop lobby, %s", post and "debrief" or "briefing")
        go = run_script(post and {
          { 2.4, "shot", "postgame-coop" }, { 2.5, function() state("coop postgame") end }, { 2.8, BACK }, { 3.4, BACK }, { 3.7, SELECT } } or {
          { 1.0, DOWN }, { 1.1, DOWN }, { 1.2, DOWN }, { 1.3, DOWN }, { 1.4, DOWN }, { 1.5, DOWN }, { 1.6, SELECT },   -- DOUBLE-O SECTION
          { 2.0, function() state("coop pick") end }, { 2.2, SELECT } })                                          -- BEGIN MISSION
      end
      go(dt)
    end)
  elseif name == "arena" then
    on("tick", function(dt)
      t = t + dt
      if i == 1 and t >= 6 then
        i = 2
        log("coop run: %d Bonds, partner bot %s", bond.count(), tostring(coop and coop.bot_info and true))
        test.coop_kill(1)
        test.coop_kill(0)
      end
    end)
  end
elseif name == "arena-menu" then
  local n = runs()
  log("lobby, runs so far %d", n)
  local plan
  if n == 0 then
    plan = {
      { 1.6, "shot", "menu-main" }, { 1.7, function() state("boot") end },
      { 2.0, DOWN }, { 2.1, DOWN }, { 2.2, DOWN }, { 2.3, DOWN }, { 2.4, DOWN }, { 2.5, SELECT },   -- SPRINT 10
      { 2.7, LEFT }, { 2.8, DOWN }, { 2.9, DOWN }, { 3.0, DOWN }, { 3.1, DOWN }, { 3.2, DOWN }, { 3.3, DOWN }, { 3.4, DOWN },
      { 3.5, RIGHT },                                                                                -- difficulty -> 00 AGENT
      { 3.6, DOWN }, { 3.7, SELECT },                                                                -- ADVANCED SETTINGS
      { 4.6, "shot", "menu-advanced" },
      { 4.8, DOWN }, { 4.9, DOWN }, { 5.0, DOWN }, { 5.1, DOWN }, { 5.2, DOWN }, { 5.3, RIGHT },      -- wave mutators ON
      { 5.4, DOWN }, { 5.5, DOWN }, { 5.6, DOWN }, { 5.7, RIGHT },                                   -- whacky MILD
      { 6.2, "shot", "menu-advanced-changed" }, { 6.3, function() state("advanced") end },
      { 6.5, BACK }, { 7.3, "shot", "menu-main-custom" }, { 7.4, function() state("main") end },
      { 7.6, RIGHT }, { 7.8, SELECT },                                                               -- ENTER THE ARENA
    }
  elseif n == 1 then
    plan = { { 2.4, "shot", "postgame-win" }, { 2.5, function() state("postgame 1") end }, { 3.4, SELECT } }   -- RETRY
  else
    plan = {
      { 2.4, "shot", "postgame-fell" }, { 2.5, function() state("postgame 2") end },
      { 3.0, RIGHT }, { 3.2, SELECT },                                                               -- CHANGE SETTINGS
      { 4.2, "shot", "menu-after" }, { 4.3, function() state("after") end },
      { 4.6, BACK }, { 4.9, SELECT },                                                                -- QUIT TO TITLE
    }
  end
  on("ui_tick", run_script(plan))
elseif name == "arena" then
  local n = runs()
  log("run %d", n + 1)
  local presses, at = 0, 30.0
  local stated = false
  local foes = {}
  on("chr_spawned", function(c) if t > 4 then foes[#foes + 1] = c end end)   -- (the crowd is placed in the first second)
  on("tick", function(dt)
    t = t + dt
    if not stated and t >= 3 then   -- AC's playtest: the lobby's menu kept the mouse in the run (no fire). It must be gone here.
      stated = true
      local up, ptr, cap = amenu.state()
      log("run %d mouse: menu_up %s gameplay_pointer %s captured %s", n + 1, tostring(up), tostring(ptr), tostring(cap))
    end
    if n == 0 and t < 30 then   -- run 1: fight for a while (real kills: the debrief's stats and bars), mostly headshots
      local k = 0
      for _, c in ipairs(foes) do
        if spawn.alive(c) and k < 2 then
          k = k + 1
          for s = 1, 40 do if spawn.alive(c) then test.shoot(c, (s % 3 == 0) and "body" or "head", "kf7", 0) end end
        end
      end
    elseif n == 0 and t >= at and presses < 12 then   -- then jump the waves to the win
      presses = presses + 1
      at = t + 0.5
      ARENA_TEST_KEY("next")
    elseif n >= 1 and t >= 4 and presses == 0 then   -- run 2: Bond falls
      presses = 1
      log("killing Bond")
      test.coop_kill(0)
    end
  end)
end
