-- A7 merged, netplay: the co-op arena with mutators and affix elites over the lockstep link (two people, no bot); host and guest
-- must agree tick for tick. A mutator every wave in list order from wave 1 (KNIVES ONLY first, then PAINTBALL, ONE-HIT KILLS ...),
-- half the enemies affix elites (explosive corpses), every enemy killed 3 s after it spawns. Player 1 is put down at 0:03 and player 0
-- revives him by standing there. Run (alone):
--   STAGE=colosseum MODE=merge-coopchaos-net LUATEST= MOVES= sh tests/net_modes.sh net <tag> <port> <ticks>
ARENA_SETTINGS = { waves = 4, boss_every = 0, crowd = 10, start_delay = 2, breather = 2, coop_bleed = 12, debug_kill = 3.0,
  mutators = true, mutator_from = 1, mutator_chance = 1, mutator_chance_max = 1, mutator_pick = "cycle",
  elites = true, affix_from = 1, affix_chance = 0.5, affix_chance_max = 0.5 }
include("arena-coop-chaos")
local t, done = 0, false
on("tick", function(dt)
  t = t + dt
  if not done and t >= 3 then done = true; print(string.format("ge: arena: TEST put player 1 down at %.1fs: %s", t, tostring(test.coop_down(1)))) end
end)
