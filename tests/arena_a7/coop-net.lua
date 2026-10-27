-- A7 netplay: the co-op arena over the lockstep link (two people, no bot), host and guest must agree tick for tick. Both Bonds stand
-- still side by side (neutral recorded input); player 1 is put down at 0:03 and player 0 revives him by standing there; then the
-- enemies (enemy_damage=400 in net_modes.sh) put them down for real: downs, revives, bleed-outs and the final wipe all run on both
-- machines. Run: STAGE=colosseum MODE=coop-net LUATEST= MOVES= sh tests/net_modes.sh net a7net <port> <ticks>
ARENA_SETTINGS = { waves = 3, boss_every = 0, crowd = 10, start_delay = 2, breather = 4, coop_bleed = 12, coop_bot = true }
include("arena-coop")
local t, done = 0, false
on("tick", function(dt)
  t = t + dt
  if not done and t >= 3 then done = true; print(string.format("ge: arena: TEST put player 1 down at %.1fs: %s", t, tostring(test.coop_down(1)))) end
end)
