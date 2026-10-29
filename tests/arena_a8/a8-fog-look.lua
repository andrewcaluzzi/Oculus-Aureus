-- The LOW VISIBILITY fog (port_arenafx.c, port_roomfog.c; v0.2 after AC's playtest): the test key "fog" (End in Test Keys) turns it
-- on at 3 s and off at 33 s while the view turns, with the default crowd (150 real + impostors); at 10 s soldiers are placed over the
-- pit floor and 12 of them die at 22 s (corpses in the fog). Screenshots (test.screenshot: cwd = the shot folder): before, mid fade-in,
-- full fog, enemies at mid and far distance, corpses, mid fade-out, after. Run: tests/arena_a8/fog-look.sh.
ARENA_SETTINGS = { mode = "endless", breather = 999, start_delay = 1, ammo_every = 0, test_keys = true }
include("arena")

local SHOTS = { { 2.5, "before" }, { 3.75, "fadein-mid" }, { 6.0, "full-a" }, { 9.0, "full-b" }, { 13.0, "enemies-a" }, { 16.0, "enemies-b" },
  { 19.0, "enemies-c" }, { 25.0, "corpses-a" }, { 28.0, "corpses-b" }, { 31.0, "corpses-c" }, { 33.75, "fadeout-mid" }, { 36.0, "after" } }
local t, si, swept, on1, off1, shot = 0, 1, false, false, false, false
on("tick", function(dt)
  t = t + dt
  if not swept and t > 1.0 and bench and bench.sweep then bench.sweep(24, 4); swept = true end
  if not on1 and t >= 3.0 then on1 = true; ARENA_TEST_KEY("fog") end
  if not off1 and t >= 33.0 then off1 = true; ARENA_TEST_KEY("fog") end
  if not shot and t >= 10.0 and ARENA_HOOKS and ARENA_HOOKS.pit_foes then
    -- soldiers spread over the pit floor (near to far): 8 that stay and fight, 12 killed 12 s later (real corpses, in the fog)
    shot = true
    local a = ARENA_HOOKS.pit_foes(8, nil)
    local b = ARENA_HOOKS.pit_foes(12, 12)
    print(string.format("ge: arena: A8TEST fog: placed %d live + %d to die", a, b))
  end
  local s = SHOTS[si]
  if s and t >= s[1] then
    test.screenshot()
    local _, corpses, alive = ARENA_HOOKS.counts()
    print(string.format("ge: arena: A8TEST fog shot %s at %.2f s, fog %s, alive %d, corpses %d", s[2], t, tostring(arena.fog and arena.fog()),
      alive, corpses))
    si = si + 1
  end
end)
