-- The arena HUD must not carry anything from one stage into the next (the per-stage HUD): tests/arena_menu/hudstale.sh runs it
-- (GE_MODE=arena-chaos from a launcher, the menu flow on, so the end of the run loads the lobby and Retry loads the run again).
-- Run 1 raises a callout at 10 s that lives 20 s ("STALE CARD") and Bond falls at 14 s, so the card is still alive when the stage ends.
-- g_GlobalTimer restarts at 0 with every stage: if the callout survives, run 2 draws the old card again at its 10 s mark.
-- Both runs take a screenshot at 11 s (the card is up in run 1 and must NOT be in run 2) and log "HUDSTALE" lines; the lobby logs
-- that the arena HUD is off. The script is read by run 1, the lobby, and run 2: each stage start runs it again.
local name = mode.name()
local UP, DOWN, LEFT, RIGHT, SELECT, BACK = 1, 2, 4, 8, 16, 32
local function n() return (scores and scores.enabled() and scores.get("chaos_fixed20_runs")) or 0 end
local function log(fmt, ...) print(string.format("ge: arena: HUDSTALE " .. fmt, ...)) end
local t, i = 0, 0
if name == "arena-menu" then
  local lt, ls = 0, 0
  on("ui_tick", function(dt)
    lt = lt + dt
    if ls == 0 and lt >= 1.0 then ls = 1; log("lobby after run %d: shot", math.floor(n())); test.screenshot() end
    if ls == 1 and lt >= 2.5 then ls = 2; test.plan_input(0, (n() >= 2) and BACK or SELECT) end
    if ls == 2 and lt >= 3.2 and n() >= 2 then ls = 3; test.plan_input(0, BACK) end
    if ls == 3 and lt >= 3.5 then ls = 4; test.plan_input(0, SELECT) end
  end)
elseif name == "arena-chaos" then
  local run = math.floor(n()) + 1
  on("tick", function(dt)
    t = t + dt
    if i == 0 and run == 1 and t >= 10 then i = 1; log("run 1: callout STALE CARD"); arena.callout("STALE CARD", 5, "from run one", 20.0) end
    if i <= 1 and t >= 11 then i = 2; log("run %d: shot at 11 s", run); test.screenshot() end
    if i == 2 and t >= 14 then i = 3; log("run %d: Bond falls", run); test.coop_kill(0) end
  end)
end
