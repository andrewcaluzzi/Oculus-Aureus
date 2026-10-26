-- A5: does a killed arena enemy drop its weapon, and does Bond pick it up (loose count goes 1 -> 0)?
local t, chr, step, cx, cz = 0, nil, 0, nil, nil
local function nearest_pad(x, z)
  local best, bd = nil, 1e18
  for i = 0, pads.count() - 1 do
    local px, py, pz = pads.pos(i)
    if px and py < 1 and math.abs(px) < 4500 and math.abs(pz) < 4500 then
      local d = (px - x) ^ 2 + (pz - z) ^ 2
      if d < bd then best, bd = i, d end
    end
  end
  return best
end
local function pools(tag)
  local l, h, fw, fp = spawn.pools()
  print(string.format("ge: pickuptest: %s: loose %d held %d free slots %d free props %d", tag, l, h, fw, fp))
end
on("tick", function(dt)
  t = t + dt
  if step == 0 and t > 1 then
    step = 1
    pools("before spawn")
    chr = spawn.chr(nearest_pad(2000, 0), { skin = "russian_soldier", weapon = "ar33", behaviour = "hold", health = 1 })
    print("ge: pickuptest: spawned", tostring(chr))
  elseif step == 1 and t > 3 then
    step = 2
    pools("armed enemy")
    cx, cz = spawn.where(chr)
    print("ge: pickuptest: kill", tostring(spawn.kill(chr)))
  elseif step == 2 and t > 6 then
    step = 3
    pools("after kill")
    local x, z = cx, cz
    print(string.format("ge: pickuptest: corpse at (%s,%s)", tostring(x), tostring(z)))
    if x then
      local pad = nearest_pad(x, z)
      local px, _, pz = pads.pos(pad)
      print("ge: pickuptest: warp", tostring(bond.warp(0, pad, x - px + 37, z - pz + 41)))
      local item, before, after = test.take_drop(0, 800)
      print(string.format("ge: pickuptest: take_drop item %d, ammo of its type %d -> %d", item, before, after))
    end
  elseif step == 3 and t > 8 then
    step = 4
    local item, before, after = test.take_drop(0, 800)
    print(string.format("ge: pickuptest: take_drop item %d, ammo of its type %d -> %d", item, before, after))
    pools("after pickup")
  end
end)
