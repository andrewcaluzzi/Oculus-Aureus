-- The REAL sniper scope at full zoom on a marksman at about 150 m, 250 m, 350 m and the farthest clear perch (sniper v2, AC's open
-- question on target size). Bond on b1 with the rifle; test.aim forces aim mode at the zoom limit (SCOPE_FOV, default 7 degrees, the
-- stock limit; GE_SCOPE_STRONG=1 also takes the same views at 3.5 degrees); a marksman (arctic_commando, drawn 2x as the duel draws
-- them) stands on a grid pad at that distance whose drawn line to Bond is clear; the view is aimed at his chest. One picture a view,
-- and the same view unzoomed. The runner names them scope-150 ... and noscope-150 ...
include("levelgen_layout_snowfield")
local P = LEVEL.pads
local FOVS = { 7 }
if mode.env_flag("GE_SCOPE_STRONG") then FOVS = { 7, 3.5 } end
local bx, by, bz
local function pick(dist)
  local best, bd
  for _, id in ipairs(LEVEL.grid) do
    local x, y, z = pads.pos(id)
    local d = math.sqrt((x - bx) ^ 2 + (z - bz) ^ 2)
    if math.abs(d - dist) < 1500 and probe.los(bx, by + 165, bz, x, y + 300, z) and probe.los(bx, by + 165, bz, x, y + 120, z) then
      local off = math.abs(x - bx) + math.abs(d - dist) * 2
      if not bd or off < bd then best, bd = id, off end
    end
  end
  return best
end
local views
local t, i, ph, foe = 0, 1, 0, nil
on("tick", function(dt)
  t = t + dt
  if t < 1.5 then return end
  if not views then
    bond.warp(0, P.b1)
    pcall(bond.give, 0, "sniperrifle", 60)
    pcall(bond.equip, 0, "sniperrifle")
    bx, by, bz = pads.pos(P.b1)
    views = {}
    for _, d in ipairs({ 15000, 25000, 35000 }) do
      local id = pick(d)
      if id then for _, f in ipairs(FOVS) do views[#views + 1] = { pad = id, tag = string.format("%d", d // 100), fov = f } end end
    end
    for _, f in ipairs(FOVS) do views[#views + 1] = { pad = P.f10, tag = "far", fov = f } end
    views[#views + 1] = { pad = P.f10, tag = "far", fov = 0 }
    views[#views + 1] = { pad = pick(25000), tag = "250", fov = 0 }
    return
  end
  local v = views[i]
  if not v then return end
  if ph == 0 then
    local x, y, z = pads.pos(v.pad)
    foe = spawn.chr(v.pad, { skin = "arctic_commando", weapon = "sniperrifle", behaviour = "spectator", face = { bx, bz } })
    if foe then pcall(spawn.scale, foe, 2.0) end
    v.x, v.y, v.z = x, y, z
    v.d = math.sqrt((x - bx) ^ 2 + (z - bz) ^ 2) / 100
    v.yaw = math.deg(math.atan(-(x - bx), (z - bz)))
    v.pitch = math.deg(math.atan((y + 220) - (by + 165), v.d * 100))
    test.aim(0, v.fov > 0, v.fov)
    bench.look(0, v.yaw, v.pitch)
  elseif ph == 4 then
    bench.look(0, v.yaw, v.pitch)
  elseif ph == 8 then
    bench.shot()
    local fx, fz = spawn.where(foe or -1)
    local _, _, fov = sniper.view(0)
    print(string.format("SCOPESHOT %s%s: marksman chr %s on pad %d at %.0f m (where %s, %s), fov %.1f, rooms %s", v.fov > 0 and "scope-" or "noscope-",
      v.tag .. (v.fov > 0 and v.fov < 7 and "-strong" or ""), tostring(foe), v.pad, v.d, tostring(fx), tostring(fz), fov or -1, table.concat(test.rooms_drawn(0) or {}, ",")))
  elseif ph == 9 then
    if foe then spawn.remove(foe); foe = nil end
    i, ph = i + 1, -1
  end
  ph = ph + 1
end)
