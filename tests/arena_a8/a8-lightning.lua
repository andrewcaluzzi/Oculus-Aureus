-- THUNDERBALL's lightning (arena.strike; port_arenarain.c, port_arenarain_draw.c, mode_arena_mutators.lua). Run by
-- tests/arena_a8/lightning.sh (or hidden on Windows: GE_MODE=a8-lightning, the script copied into Data/Scripts/modes):
--   1. soldiers over the pit (they fight), then the rain on (the test key "rain", Insert): the strikes come on their own every few
--      seconds, at Bonds and enemies ("RAIN lightning N at bond|chr" and "LIGHTNING N lands" lines);
--   2. the test key "strike" (Shift+Insert): one near Bond now;
--   3. one strike 350 units off Bond with his view turned onto it, and two screenshots of it landing (the flash with the bolt, then
--      the bolt as the flash fades);
--   4. the rain off: "RAIN over: ... lightning strikes (... at a Bond)".
-- Logs "A8TEST lightning ..." lines; the script checks them.
ARENA_SETTINGS = { mode = "endless", breather = 999, start_delay = 1, ammo_every = 0, test_keys = true }
include("arena")

local t, step = 0, 0
local P = {}
local function shot(tag)
  test.screenshot()
  print(string.format("ge: arena: A8TEST lightning shot %s at %.2f s (%.2f s after the aimed strike)", tag, t, t - (P.aimed or t)))
end
local plan = {
  { 1.0, function() bond.warp(0, 108) end },
  { 1.5, function() print(string.format("ge: arena: A8TEST lightning placed %d", ARENA_HOOKS.pit_foes(12, nil))) end },
  { 2.0, function() ARENA_TEST_KEY("rain") end },
  { 14.0, function() ARENA_TEST_KEY("strike") end },
  { 20.0, function() bond.warp(0, 108) end },
  { 20.5, function()
      local x, _, z = bond.pos(0)
      local sx, _, sz, serial = arena.strike({ bond = 0 }, 350, 0, "grenade", 0.6)
      if sx then
        P.aimed = t
        if test.face then pcall(test.face, 0, sx, sz) end
        print(string.format("ge: arena: A8TEST lightning aimed %d from Bond (%.0f, %.0f) to (%.0f, %.0f)", serial, x, z, sx, sz))
      else
        print("ge: arena: A8TEST lightning aimed: refused")
      end
    end },
  { 21.15, function() shot("flash") end },   -- (it lands 37 ticks after the aim: ~0.62 s)
  { 21.21, function() shot("bolt") end },
  { 34.0, function() ARENA_TEST_KEY("rain") end },
  { 37.0, function() print("ge: arena: A8TEST lightning done") end },
}
on("ui_tick", function(dt)
  t = t + dt
  local s = plan[step + 1]
  while s and t >= s[1] do
    step = step + 1
    s[2]()
    s = plan[step + 1]
  end
end)
