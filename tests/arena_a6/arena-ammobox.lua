-- A6: the centre ammo box (spawn.item "ammo"): draws 700 units in front of Bond (run with GE_SHOT=100), then Bond walks onto it
-- and his reserve of the three ammo types should rise. GE_MODE=arena-ammobox GE_STAGE=colosseum GE_SKIPINTRO=1 GE_CHEATS=invuln
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
local function reserves()
  local _, a = bond.ammo(0, "pp7"); local _, b = bond.ammo(0, "kf7"); local _, c = bond.ammo(0, "shotgun")
  return string.format("pp7 %s kf7 %s shotgun %s", tostring(a), tostring(b), tostring(c))
end
local t, h, before = 0, nil, nil
on("tick", function(dt)
  t = t + dt
  if not h and t > 1 then
    h = spawn.item(pad_at(0, 700), "ammo", { ammo = { pp7 = 50, kf7 = 60, shotgun = 16 } })
    before = reserves()
    print("ge: ammobox: spawned handle", tostring(h), "alive", tostring(spawn.item_alive(h or 0)), "| before:", before)
  elseif h and t > 4 then
    local x, z = spawn.item_pos(h)
    if x then
      test.walk(0, x, z)
      if t > 30 then print("ge: ammobox: TIMEOUT"); test.walk_stop(); h = nil; t = -1e9 end
    else
      test.walk_stop()
      print("ge: ammobox: COLLECTED at", t, "| after:", reserves(), "| before:", before)
      h = nil; t = -1e9
    end
  end
end)
