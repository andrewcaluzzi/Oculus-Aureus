-- A7 survival: the co-op arena (co-op scaling) for 6 waves, a boss on wave 5. Bond 1 cannot die (GE_COOP_GOD=0) and does not fight:
-- he stands still, except that when the partner bot is down he walks over to him (real stick input, test.walk) and stands there to
-- revive him. So the bot does all the killing, ammo runs and weapon pickups. Expected: waves cleared one by one, the bot revived when
-- it goes down, no crash, "WON" after wave 6. Bot status every 2 s (coop_debug). No crowd: under llvmpipe on a busy machine the full
-- 100 renders at a few game frames a second (the crowd is decoration, A3/A4 covered it).
ARENA_SETTINGS = { waves = 6, boss_every = 5, start_delay = 4, crowd = 0, coop_debug = true }
include("arena-coop")
local walking = false
on("tick", function(dt)
  if coop.state(1) == "down" then
    local x, _, z = bond.pos(1)
    if x then test.walk(0, x + 30, z + 30); walking = true end
  elseif walking then
    test.walk_stop(); walking = false
  end
end)
