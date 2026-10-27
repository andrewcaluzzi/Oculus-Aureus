-- A7 bleed-out, bleedout "wave": the partner bot (player 1) is put down at 0:05 and nobody revives him (Bond 1 stands still), so he
-- bleeds out after 10 s, stays out (no 10 s co-op respawn) and comes back when the next wave is cleared. Empty waves, a 10 s breather.
-- Expected: "player 1 DOWN", "player 1 BLED OUT ... back after the wave", state down -> out, "player 1 released", "player 1 is back".
-- Run with GE_COOP_GOD=0.
ARENA_SETTINGS = { mode = "endless", boss_every = 0, crowd = 0, start_delay = 1, breather = 10, ammo_every = 0, coop_bleed = 10,
                   coop_bleedout = "wave", debug_queue = function(w) return {} end }
include("arena-coop")
local t, done, moved = 0, false, false
on("tick", function(dt)
  t = t + dt
  if not moved and t >= 4 then   -- far from Bond 1, so standing still he cannot revive him
    moved = true
    local far = pads.far_from(0, 0, 2500, 3500, true)
    print(string.format("ge: arena: TEST warp player 1 to pad %d: %s", far[1], tostring(bond.warp(1, far[1], 37, 41))))
  end
  if not done and t >= 5 then done = true; print(string.format("ge: arena: TEST put player 1 down at %.1fs: %s", t, tostring(test.coop_down(1)))) end
  if math.floor(t * 4) % 8 == 0 then print(string.format("ge: arena: TEST t=%.1f player 1 %s dead %s", t, coop.state(1), tostring(bond.dead(1)))) end
end)
