-- Pictures of the SEVERNAYA SNOWFIELD prototype (GE_STAGE=levelgen GE_LEVELGEN=snowfield, a hidden run; the runner converts the BMPs): the field
-- from Bond's start, down the long axis, the slopes from the side, the perimeter, a marksman on a far perch. Also prints the drawn-ground line
-- (probe.los, both ways, head to eye) from Bond's start to every named perch, and the prop draw distance. bench.look sets the view.
include("levelgen_layout_snowfield")
local P = LEVEL.pads
local function nearest(x0, z0)
  local best, bd
  for _, id in ipairs(LEVEL.rooms.field.pads) do
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
local views
local function make_views()
  local east = nearest(13500, 1000)
  views = {
  { tag = "overview", pad = P.b1, yaw = 0, pitch = -4 },
  { tag = "longaxis", pad = P.b1, yaw = yaw_to(P.b1, P.f3), pitch = -1 },
  { tag = "side", pad = P.f1, yaw = yaw_to(P.f1, P.b5), pitch = -6 },
  { tag = "slopes", pad = P.b4, yaw = yaw_to(P.b4, P.m6), pitch = -5 },
  { tag = "perimeter", pad = east, yaw = -90, pitch = 6 },
  { tag = "marksman", pad = P.b1, yaw = yaw_to(P.b1, P.m5), pitch = -1, foe = P.m5 },
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
    for _, n in ipairs({ "m1", "m2", "m3", "m4", "m5", "m6", "m7", "m8", "f1", "f2", "f3", "f4", "f5", "f6", "f7", "f8" }) do
      local x, y, z = pads.pos(P[n])
      local a = probe.los(bx, by + 160, bz, x, y + 150, z)
      local b = probe.los(x, y + 150, z, bx, by + 160, bz)
      print(string.format("SFSHOT los b1 -> %s (%.0f m): %s / %s", n, math.sqrt((x - bx) ^ 2 + (z - bz) ^ 2) / 100, tostring(a), tostring(b)))
    end
  end
  local v = views[i]
  if not v then return end
  if ph == 0 then
    bond.warp(0, v.pad)
    bench.look(0, v.yaw, v.pitch)
    if v.foe then
      local bx, _, bz = pads.pos(v.pad)
      foe = spawn.chr(v.foe, { skin = "arctic_commando", weapon = "sniperrifle", behaviour = "spectator", face = { bx, bz } })
      local ok, err = false, "no chr"
      if foe then ok, err = pcall(spawn.scale, foe, 2.0) end
      print("SFSHOT foe chr " .. tostring(foe) .. " scale " .. tostring(ok) .. " " .. tostring(err))
    end
  elseif ph == 3 then
    bench.look(0, v.yaw, v.pitch)
  elseif ph == 6 then
    bench.shot()
    local x, y, z = bond.pos(0)
    print(string.format("SFSHOT %s at (%.0f, %.0f, %.0f)", v.tag, x or 0, y or 0, z or 0))
  elseif ph == 8 then
    if foe then spawn.remove(foe); foe = nil end
    i, ph = i + 1, -1
  end
  ph = ph + 1
end)
