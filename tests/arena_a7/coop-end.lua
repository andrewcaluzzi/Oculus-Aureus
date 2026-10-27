-- A7 bleed-out, bleedout "end": the partner bot (player 1) is put down at 0:05, nobody revives him, he bleeds out after 8 s and that
-- ends the run (every Bond dies, the stock co-op all-dead failure; the arena logs OVER). Run with GE_COOP_GOD=0 (the end kills him anyway).
ARENA_SETTINGS = { mode = "endless", boss_every = 0, crowd = 0, start_delay = 1, breather = 10, ammo_every = 0, coop_bleed = 8,
                   coop_bleedout = "end", debug_queue = function(w) return {} end }
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
end)
on("bond_died", function(killer, player) print(string.format("ge: arena: TEST bond_died player %s at %.1fs, all_down %s", tostring(player), t, tostring(bond.all_down()))) end)
