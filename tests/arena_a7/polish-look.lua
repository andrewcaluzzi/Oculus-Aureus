-- A7: pickup markers. Centre ammo box, armour and a PP7 on pads ahead of / beside / behind Bond (he starts on pad 108 facing +z). Run with GE_SHOT=N.
ARENA_SETTINGS = { mode = "endless", crowd = 0, start_delay = 999, ammo_every = 0, supplies = true, pickup_markers = true }
include("arena")
local function pad_at(x, z)
  local best, bd = nil, 1e18
  for i = 0, pads.count() - 1 do
    local px, py, pz = pads.pos(i)
    if px and py < 1 then
      local d = (px - x) ^ 2 + (pz - z) ^ 2
      if d < bd then best, bd = i, d end
    end
  end
  return best
end
local t, done = 0, false
on("tick", function(dt)
  t = t + dt
  if not done and t > 1 then
    done = true
    print("ge: look: box", spawn.item(pad_at(-750, 1250), "ammo", { ammo = { pp7 = 50, kf7 = 60, shotgun = 16 } }))
    print("ge: look: armour", spawn.item(pad_at(750, 2250), "armour"))
    print("ge: look: pp7", spawn.item(pad_at(250, 500), "pp7"))
    print("ge: look: behind", spawn.item(pad_at(-1500, -2500), "ammo", { ammo = { pp7 = 50 } }))
  end
end)
