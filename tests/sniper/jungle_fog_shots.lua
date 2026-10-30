-- Pictures for the jungle's metre-based haze (GE_MODE=<this> GE_STAGE=levelgen GE_LEVELGEN=jungle, a hidden run, GE_HIDDEN=1; bench.shot
-- writes BMPs in the working directory): Bond's start view (held 5 s so the run's stats line measures the frame rate and draws), the east
-- perimeter (the foliage wall), and a marksman on the knoll k3 (295 m) set just off the crosshair (the picture is cropped in for the scope
-- look). JFOG = { start_m, full_m } (a global set before this file runs; the harness prepends it) overrides the default haze via sniper.fog.
include("levelgen_layout_jungle")
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
local function pitch_to(from, to)   -- eye to the head
  local x0, y0, z0 = pads.pos(from)
  local x1, y1, z1 = pads.pos(to)
  return math.deg(math.atan((y1 + 150) - (y0 + 160), math.sqrt((x1 - x0) ^ 2 + (z1 - z0) ^ 2)))
end
local views
local function make_views()
  local east = nearest(18600, 7000)
  views = {
    { tag = "start", pad = P.b1, yaw = 0, pitch = -3, hold = 5 },
    { tag = "perimeter", pad = east, yaw = -90, pitch = 8 },
    { tag = "scope300", pad = P.b1, yaw = yaw_to(P.b1, P.k3) + 0.8, pitch = pitch_to(P.b1, P.k3) - 0.5, foe = P.k3, hold = 2 },
  }
  local tags = {}
  for _, v in ipairs(views) do tags[#tags + 1] = v.tag end
  print("JFSHOT views " .. table.concat(tags, ","))
end
local t, i, ph, foe, set = 0, 1, 0, nil, false
on("tick", function(dt)
  t = t + dt
  if t < 1.5 then return end
  if not set then
    set = true
    if JFOG and sniper and sniper.fog then sniper.fog(JFOG[1], JFOG[2]) end
    print("JFSHOT fog " .. tostring(sniper and sniper.fog and table.concat({ sniper.fog() }, " .. ") or "n/a") .. " fog props " .. tostring(probe.fog()))
  end
  if not views then make_views() end
  local v = views[i]
  if not v then return end
  if ph == 0 then
    bond.warp(0, v.pad)
    bench.look(0, v.yaw, v.pitch)
    if v.foe then
      local bx, _, bz = pads.pos(v.pad)
      foe = spawn.chr(v.foe, { skin = "jungle_commando", weapon = "sniperrifle", behaviour = "spectator", face = { bx, bz } })
      local sok = foe and pcall(spawn.scale, foe, 2.0)
      local fx, _, fz = pads.pos(v.foe)
      print(string.format("JFSHOT foe chr %s scale %s at %.0f m", tostring(foe), tostring(sok), math.sqrt((fx - bx) ^ 2 + (fz - bz) ^ 2) / 100))
    end
  elseif ph == 3 then
    bench.look(0, v.yaw, v.pitch)
    v.t0 = t
  elseif ph == 5 and v.hold and t - v.t0 < v.hold then
    ph = ph - 1   -- (held on the view so the run's stats line measures it)
  elseif ph == 6 then
    bench.shot()
    local x, y, z = bond.pos(0)
    print(string.format("JFSHOT %s at (%.0f, %.0f, %.0f)", v.tag, x or 0, y or 0, z or 0))
  elseif ph == 8 then
    if foe then spawn.remove(foe); foe = nil end
    i, ph = i + 1, -1
  end
  ph = ph + 1
end)
