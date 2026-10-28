-- Whacky over the lockstep link (post-release battery; the whacky branch never had a netplay run): the co-op arena with WHACKY WILD
-- (every whacky mutator may roll, a mutator every wave from wave 2 at 70-95%, bosses included) plus the A7 mutators and affix elites, two
-- people, no bot; host and guest must agree tick for tick (the colour / alpha washes are presentation; the minion launcher, anim rate, line
-- mode, 2x weapons, chr scale and the speed swap are sim). Every enemy dies 3 s after it spawns; player 1 is put down at 3 s.
-- Run (alone): STAGE=colosseum MODE=whacky-net LUATEST= MOVES= sh tests/net_modes.sh net <tag> <port> <ticks>
ARENA_SETTINGS = { waves = 6, boss_every = 0, crowd = 10, start_delay = 2, breather = 2, coop_bleed = 12, debug_kill = 3.0,
  mutators = true, elites = true, affix_from = 1, affix_chance = 0.5, affix_chance_max = 0.5, whacky = 3, coop = true, mode = "fixed" }
include("arena")
local t, done = 0, false
on("tick", function(dt)
  t = t + dt
  if not done and t >= 3 then done = true; print(string.format("ge: arena: TEST put player 1 down at %.1fs: %s", t, tostring(test.coop_down(1)))) end
end)
