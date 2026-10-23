-- A6: do spawn.item pickups draw? PP7, KF7 and armour on the pads 500-700 units in front of Bond (he starts on pad 108 facing +z). Run with GE_SHOT=N.
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
    print("ge: look: pp7", spawn.item(pad_at(-750, 250), "pp7"))
    print("ge: look: armour", spawn.item(pad_at(-250, 250), "armour"))
    print("ge: look: kf7", spawn.item(pad_at(250, 250), "kf7"))
  end
end)
