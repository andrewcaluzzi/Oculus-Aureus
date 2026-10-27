-- Post-release battery: the 20-wave CO-OP CHAOS soak. Two Bonds (the partner bot), 20 fixed waves, a boss every 5, mutators (70% of the
-- non-boss waves from wave 2) and affix elites (35%), whacky NORMAL on top, crowd 50, every enemy killed 4 s after it spawns, Bond 1
-- (player 0, GE_COOP_GOD=0: cannot die) stands still and is put down every 25 s so the bot revives him. Expected: "WON wave 20", every
-- mutator "reset ok", downs = revives, no Lua error / pool warning / crash, free chr slots steady.
-- Run: tests/arena_a7/run_coop.sh o-coopchaos-soak coopchaos-soak 30000 "GE_COOP_GOD=0"
ARENA_SETTINGS = { waves = 20, boss_every = 5, start_delay = 2, breather = 2, crowd = 50, debug_kill = 4.0, ammo_every = 20, coop = true,
  mode = "fixed", mutators = true, mutator_from = 2, mutator_chance = 0.7, mutator_chance_max = 0.7,
  elites = true, affix_from = 1, affix_chance = 0.35, affix_chance_max = 0.35, whacky = 2, coop_debug = false }
include("arena")
local t, next_down = 0, 25
on("tick", function(dt)
  t = t + dt
  if t >= next_down then
    next_down = next_down + 25
    if coop.state(0) == "up" then print(string.format("ge: arena: TEST put player 0 down at %.1fs: %s", t, tostring(test.coop_down(0)))) end
  end
end)
