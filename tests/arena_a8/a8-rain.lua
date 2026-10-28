-- The RAIN mutator (THUNDERBALL; port_arenarain.c, port_arenarain_draw.c, mode_arena_mutators.lua rain). Run by tests/arena_a8/rain.sh:
--   1. Bond's slip: from pad 108 he walks east with the real stick for 2 s, the stick is let go, and the distance he still moves
--      (the coast) is measured; dry first, then with the rain at full (the test key "rain", Insert in Test Keys) from the same pad.
--   2. soldiers placed over the pit (half of them run at Bond) while it rains: the "RAIN slip" lines.
--   3. screenshots: dry, mid roll-in, full rain (four directions as the view turns), mid roll-out, dry again.
-- Logs "A8TEST rain ..." lines; the script checks them.
ARENA_SETTINGS = { mode = "endless", breather = 999, start_delay = 1, ammo_every = 0, test_keys = true }
include("arena")

local t, step = 0, 0
local P = {}
local function pos() local x, _, z = bond.pos(0); return x, z end
local function shot(tag) test.screenshot(); print(string.format("ge: arena: A8TEST rain shot %s at %.2f s, rain %.2f", tag, t, arena.rain())) end
local plan = {
  { 1.0, function() bond.warp(0, 108) end },
  { 1.5, function() shot("dry") end },
  { 2.0, function() P.walk = true; P.x0, P.z0 = pos() end },
  { 4.0, function() P.walk = false; test.walk_stop(); P.xs, P.zs = pos() end },
  { 5.5, function() local x, z = pos(); P.coast_dry = math.sqrt((x - P.xs) ^ 2 + (z - P.zs) ^ 2)
                    print(string.format("ge: arena: A8TEST rain coast dry %.1f", P.coast_dry)) end },
  { 6.0, function() ARENA_TEST_KEY("rain") end },
  { 6.75, function() shot("rollin-mid") end },
  { 8.0, function() bond.warp(0, 108) end },
  { 8.5, function() P.walk = true end },
  { 10.5, function() P.walk = false; test.walk_stop(); P.xs, P.zs = pos() end },
  { 12.0, function() local x, z = pos(); P.coast_wet = math.sqrt((x - P.xs) ^ 2 + (z - P.zs) ^ 2)
                     print(string.format("ge: arena: A8TEST rain coast wet %.1f (dry %.1f)", P.coast_wet, P.coast_dry)) end },
  { 12.5, function() if bench and bench.sweep then bench.sweep(30, 4) end
                     print(string.format("ge: arena: A8TEST rain placed %d", ARENA_HOOKS.pit_foes(12, nil))) end },
  { 15.0, function() shot("full-a") end },
  { 18.0, function() shot("full-b") end },
  { 21.0, function() shot("full-c") end },
  { 24.0, function() shot("full-d") end },
  { 30.0, function() ARENA_TEST_KEY("rain") end },
  { 30.75, function() shot("rollout-mid") end },
  { 33.0, function() shot("after"); print("ge: arena: A8TEST rain done") end },
}
on("ui_tick", function(dt)   -- (every game tick: test.walk is a stick input per tick)
  t = t + dt
  if P.walk then local x, _, z = bond.pos(0); test.walk(0, x + 3000, z) end
  local s = plan[step + 1]
  while s and t >= s[1] do
    step = step + 1
    s[2]()
    s = plan[step + 1]
  end
end)
