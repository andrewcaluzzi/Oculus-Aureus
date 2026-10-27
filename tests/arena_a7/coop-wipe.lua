-- A7 everybody down: Bond 1 (player 0) is put down at 0:03; then the partner bot takes a lethal hit through the real damage code
-- (GE_COOP_HURT=600:1:50, sim tick 600). With nobody up to revive anyone he dies instead of going down, and the downed Bond 1 dies
-- with him: the stock all-dead failure, the arena logs OVER. No GE_COOP_GOD here.
ARENA_SETTINGS = { mode = "endless", boss_every = 0, crowd = 0, start_delay = 1, breather = 30, ammo_every = 0, coop_bot = false,
                   debug_queue = function(w) return {} end }
include("arena-coop")
local t, done, moved = 0, false, false
on("tick", function(dt)
  t = t + dt
  if not moved and t >= 2 then   -- player 1 (no bot here) away from Bond 1, so standing still he cannot revive him
    moved = true
    local far = pads.far_from(0, 0, 2500, 3500, true)
    print(string.format("ge: arena: TEST warp player 1 to pad %d: %s", far[1], tostring(bond.warp(1, far[1], 37, 41))))
  end
  if not done and t >= 3 then done = true; print(string.format("ge: arena: TEST put player 0 down at %.1fs: %s", t, tostring(test.coop_down(0)))) end
end)
on("bond_died", function(killer, player) print(string.format("ge: arena: TEST bond_died player %s at %.1fs, all_down %s", tostring(player), t, tostring(bond.all_down()))) end)
