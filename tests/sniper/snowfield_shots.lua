-- Pictures of the SEVERNAYA SNOWFIELD (v2) (GE_STAGE=levelgen GE_LEVELGEN=snowfield, a hidden run; the runner converts the BMPs): the field
-- from Bond's start, down the long axis through the real scope, the slopes from the far rise, the perimeter, the rooms drawn per view. Also
-- prints the drawn-ground line (probe.los, both ways, head to eye) from Bond's start to every named perch, and the prop draw distance.
include("levelgen_layout_snowfield")
local P = LEVEL.pads
local function nearest(x0, z0)
  local best, bd
  for _, id in ipairs(LEVEL.grid) do
    local x, _, z = pads.pos(id)
    local d = (x - x0) ^ 2 + (z - z0) ^ 2
    if not bd or d < bd then best, bd = id, d end
  end
  return best
end
local function yaw_to(from, to)
  local x0, _, z0 = pads.pos(from)
  local x1, _, z1 = pads.pos(to)
  return math.deg(math.atan(-(x1 - x0), (z1 - z0)))
end
local function pitch_to(from, to, eye, h)
  local x0, y0, z0 = pads.pos(from)
  local x1, y1, z1 = pads.pos(to)
  return math.deg(math.atan((y1 + (h or 150)) - (y0 + (eye or 165)), math.sqrt((x1 - x0) ^ 2 + (z1 - z0) ^ 2)))
end
local views
local function make_views()
  local east = nearest(23500, 2000)
  views = {
  { tag = "overview", pad = P.b1, yaw = 0, pitch = -3 },
  { tag = "longaxis", pad = P.b1, yaw = yaw_to(P.b1, P.f3), pitch = -1 },
  { tag = "scope-far", pad = P.b1, yaw = yaw_to(P.b1, P.f3), pitch = pitch_to(P.b1, P.f3), fov = 7, foe = P.f3 },
  { tag = "side", pad = P.f1, yaw = yaw_to(P.f1, P.b5), pitch = -4 },
  { tag = "slopes", pad = P.b4, yaw = yaw_to(P.b4, P.m6), pitch = -3 },
  { tag = "perimeter", pad = east, yaw = -90, pitch = 6 },
  }
end
local t, i, ph, foe = 0, 1, 0, nil
on("tick", function(dt)
  t = t + dt
  if t < 1.5 then return end
  if not views then make_views() end
  if i == 1 and ph == 0 then
    print(string.format("SFSHOT fog: props drawn out to %.0f units", probe.fog()))
    local bx, by, bz = pads.pos(P.b1)
    for name, id in pairs(P) do
      if name ~= "start" then
        local x, y, z = pads.pos(id)
        local a = probe.los(bx, by + 165, bz, x, y + 150, z)
        local b = probe.los(x, y + 150, z, bx, by + 165, bz)
        print(string.format("SFSHOT los b1 -> %s (%.0f m): %s / %s", name, math.sqrt((x - bx) ^ 2 + (z - bz) ^ 2) / 100, tostring(a), tostring(b)))
      end
    end
  end
  local v = views[i]
  if not v then return end
  if ph == 0 then
    bond.warp(0, v.pad)
    pcall(bond.give, 0, "sniperrifle", 60)
    pcall(bond.equip, 0, "sniperrifle")
    test.aim(0, v.fov ~= nil, v.fov or 0)
    bench.look(0, v.yaw, v.pitch)
    if v.foe then
      local bx, _, bz = pads.pos(v.pad)
      foe = spawn.chr(v.foe, { skin = "arctic_commando", weapon = "sniperrifle", behaviour = "spectator", face = { bx, bz } })
      if foe then pcall(spawn.scale, foe, 2.0) end
    end
  elseif ph == 3 then
    bench.look(0, v.yaw, v.pitch)
  elseif ph == 6 then
    bench.shot()
    local x, y, z = bond.pos(0)
    print(string.format("SFSHOT %s at (%.0f, %.0f, %.0f), rooms drawn %s", v.tag, x or 0, y or 0, z or 0, table.concat(test.rooms_drawn(0) or {}, ",")))
  elseif ph == 8 then
    if foe then spawn.remove(foe); foe = nil end
    test.aim(0, false, 0)
    i, ph = i + 1, -1
  end
  ph = ph + 1
end)
