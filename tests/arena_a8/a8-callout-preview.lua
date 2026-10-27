-- The callout show (port_arenahud.c): presses the preview test key (ARENA_TEST_KEY "callout", the same as Home) every 2.6 s, so every
-- chain, streak, event and boss card plays in turn, and takes a few screenshots mid-animation (test.screenshot: cwd = the shot folder).
-- No enemies (empty waves). Run with GE_ARENA_TESTKEYS=1: tests/arena_a8/run.sh a8-callout-preview a8-callout-preview 6000 invuln
-- "GE_ARENA_TESTKEYS=1". Logs "TEST callout preview i/n: <text> (level L)" and "A8TEST preview shot ..." lines.
ARENA_SETTINGS = { mode = "endless", boss_every = 0, breather = 999, start_delay = 1, ammo_every = 0, armour_chance = 0, crowd = { count = 20 },
  test_keys = true, debug_queue = function() return {} end }
include("arena")

-- which presses get shots, and when after the press (seconds): an entrance, a hold and an exit of a few tiers
local SHOTS = { [3] = { 0.22 }, [5] = { 0.30, 1.4 }, [6] = { 0.25, 2.95 }, [11] = { 0.20 }, [27] = { 0.35, 1.2 } }
local t, next_t, n, pending = 0, 3.0, 0, {}
on("tick", function(dt)
  t = t + dt
  if t >= next_t then
    next_t = t + 2.6
    n = n + 1
    ARENA_TEST_KEY("callout")
    for _, d in ipairs(SHOTS[n] or {}) do pending[#pending + 1] = { at = t + d, n = n, d = d } end
  end
  for i = #pending, 1, -1 do
    local p = pending[i]
    if t >= p.at then
      test.screenshot()
      print(string.format("ge: arena: A8TEST preview shot: press %d, %.2f s in", p.n, p.d))
      table.remove(pending, i)
    end
  end
end)
