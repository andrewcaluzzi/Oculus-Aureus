-- A6 soak: endless waves, every enemy killed 2 s after it spawns (many real kills, drops, health per kill), centre ammo every 6 s and armour after
-- 50% of the waves, and a collector: Bond walks (real stick input) to the nearest centre pickup and takes it. Watch for pool / ModelHitEntry
-- warnings, REFUSED spawns, crashes, and that the centre items are collected / expire / get replaced.
ARENA_SETTINGS = { mode = "endless", boss_every = 5, breather = 1, start_delay = 1, max_alive = 18, max_alive_cap = 24,
  crowd = 100, debug_kill = 2.0, first_wave = 1,
  ammo_every = 6, ammo_max_center = 3, ammo_ttl = 25, armour_chance = 0.5, armour_ttl = 30 }
include("arena")
local walking, took = false, 0
on("tick", function(dt)
  local items = ARENA_CENTRE_ITEMS()
  local bx, _, bz = bond.pos(0)
  local best, bd
  if bx then
    for _, it in ipairs(items) do
      local x, z = spawn.item_pos(it.h)
      if x then
        local d = (x - bx) ^ 2 + (z - bz) ^ 2
        if not bd or d < bd then best, bd = { x = x, z = z, it = it }, d end
      end
    end
  end
  if best then
    test.walk(0, best.x, best.z)
    walking = true
  elseif walking then
    test.walk_stop()
    walking = false
  end
end)
