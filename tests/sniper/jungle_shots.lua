-- Pictures of the SNIPER JUNGLE prototype (GE_MODE=<this> GE_STAGE=levelgen GE_LEVELGEN=jungle, a hidden run, GE_HIDDEN=1; the runner
-- converts the BMPs bench.shot writes in its working directory): Bond's start view (held 5 s so the run's stats line measures it), sight
-- lines to a knoll marksman (295 m) and a ridge marksman (353 m) set just off the crosshair, the ravine, the perimeter, a thicket up close,
-- a far perch looking back at Bond. First, a marksman (shots held) on each perch in turn: the stan line (combat.los) and the drawn line
-- (probe.los both ways) to b1. GE_JSHOTS_NOLOS=1 skips that survey (slow: two seconds a perch); GE_JSHOTS_GALLERY=1 takes the development
-- views instead (marksmen at 26 / 106 / 176 / 249 m, and the texture panels of a JUNGLE_GALLERY=1 build).
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
local function river_z(x) return 1300 * math.sin(x / 5200 + 0.6) + 650 * math.sin(x / 2100 + 2.0) + 400 end
local views
local function make_views()
  local river = nearest(-3000, river_z(-3000) - 1500)
  local east = nearest(18600, 7000)
  -- the foliage up close: a grid pad 6 to 14 m off the thicket nearest (-5000, -6000), looking at it
  local forest, forest_yaw = nearest(-6800, -4200), 30
  local ths = {}
  for _, th in ipairs(LEVEL.thickets or {}) do ths[#ths + 1] = { th[1], th[2], th[3], (th[1] + 5000) ^ 2 + (th[2] + 6000) ^ 2 } end
  table.sort(ths, function(a, b) return a[4] < b[4] end)
  for _, best in ipairs(ths) do
    local pick, pd
    for _, id in ipairs(LEVEL.grid) do
      local x, _, z = pads.pos(id)
      local d = math.sqrt((x - best[1]) ^ 2 + (z - best[2]) ^ 2) - best[3]
      if d > 500 and d < 2200 and (not pd or d < pd) then pick, pd = id, d end
    end
    if pick then
      local x, _, z = pads.pos(pick)
      forest, forest_yaw = pick, math.deg(math.atan(-(best[1] - x), (best[2] - z)))
      break
    end
  end
  views = {
    { tag = "start", pad = P.b1, yaw = 0, pitch = -3, hold = 5 },
    { tag = "sightline", pad = P.b1, yaw = yaw_to(P.b1, P.k3) + 0.8, pitch = pitch_to(P.b1, P.k3) - 0.5, foe = P.k3 },
    { tag = "farline", pad = P.b1, yaw = yaw_to(P.b1, P.f4) + 0.8, pitch = pitch_to(P.b1, P.f4) - 0.5, foe = P.f4 },
    { tag = "river", pad = river, yaw = 70, pitch = -12 },
    { tag = "perimeter", pad = east, yaw = -90, pitch = 8 },
    { tag = "foliage", pad = forest, yaw = forest_yaw, pitch = 4 },
    { tag = "perchback", pad = P.f4, yaw = yaw_to(P.f4, P.b1), pitch = -3 },
    { tag = "nearfoe", pad = P.b1, yaw = yaw_to(P.b1, P.b3), pitch = -2, foe = P.b3, dev = true },
    { tag = "midfoe", pad = P.b1, yaw = yaw_to(P.b1, P.k5), pitch = pitch_to(P.b1, P.k5), foe = P.k5, dev = true },
    { tag = "foe100", pad = P.b1, yaw = yaw_to(P.b1, nearest(0, -7000)), pitch = pitch_to(P.b1, nearest(0, -7000)), foe = nearest(0, -7000), dev = true },
    { tag = "foe200", pad = P.b1, yaw = yaw_to(P.b1, nearest(0, 2500)), pitch = pitch_to(P.b1, nearest(0, 2500)), foe = nearest(0, 2500), dev = true },
    { tag = "gallery1", pad = P.b1, yaw = 30, pitch = -10, dev = true },
    { tag = "gallery2", pad = P.b1, yaw = 0, pitch = -10, dev = true },
    { tag = "gallery3", pad = P.b1, yaw = -30, pitch = -10, dev = true },
  }
  local dev = mode.env_flag("GE_JSHOTS_GALLERY")   -- (development: the texture sample panels of a JUNGLE_GALLERY=1 build)
  local keep = {}
  for _, v in ipairs(views) do if (v.dev or false) == dev then keep[#keep + 1] = v end end
  views = keep
  local tags = {}
  for _, v in ipairs(views) do tags[#tags + 1] = v.tag end
  print("JSHOT views " .. table.concat(tags, ","))
end
local t, i, ph, foe, stand = 0, 1, 0, nil, nil
on("tick", function(dt)
  t = t + dt
  if t < 1.5 then return end
  if not views then make_views() end
  if i == 1 and ph == 0 and not stand then
    -- a marksman (shots held) on each perch in turn, Bond on b1: the stan line (combat.los, the AI's sight: tiles, holes, trees) and the
    -- drawn geometry (probe.los both ways, head to eye: the ground, the boulders)
    print(string.format("JSHOT fog: props drawn out to %.0f units", probe.fog()))
    if mode.env_flag("GE_JSHOTS_NOLOS") then stand = { done = true }; return end   -- (pictures only)
    bond.warp(0, P.b1)
    stand = { k = 0, nclear = 0 }
    for name, id in pairs(P) do
      if name ~= "start" and name:sub(1, 1) ~= "b" then stand[#stand + 1] = { name = name, id = id } end
    end
    table.sort(stand, function(a, b) return a.name < b.name end)
    return
  end
  if stand and not stand.done then
    stand.ticks = (stand.ticks or 0) + 1
    local s = stand[stand.k]
    if s and s.chr and stand.ticks >= 120 then
      local bx, by, bz = pads.pos(P.b1)
      local x, y, z = pads.pos(s.id)
      local st = combat.los(s.chr, 0)
      local a = probe.los(bx, by + 160, bz, x, y + 150, z)
      local b = probe.los(x, y + 150, z, bx, by + 160, bz)
      if st and a and b then stand.nclear = stand.nclear + 1 end
      print(string.format("JSHOT los b1 -> %s (%.0f m): stan %s, drawn %s / %s", s.name, math.sqrt((x - bx) ^ 2 + (z - bz) ^ 2) / 100,
        tostring(st), tostring(a), tostring(b)))
      spawn.remove(s.chr)
      s.chr = nil
    end
    if not s or not s.chr then
      stand.k = stand.k + 1
      s = stand[stand.k]
      if not s then
        print(string.format("JSHOT los: %d of %d perches see b1 on both lines", stand.nclear, #stand))
        stand.done = true
        return
      end
      -- (the duel's own marksman, his shots held: a spectator stand-in has no line at all here)
      local bx, _, bz = pads.pos(P.b1)
      local ok, c = pcall(spawn.chr, s.id, { skin = "jungle_commando", weapon = "sniperrifle", behaviour = "snipe", sight = 40000, face = { bx, bz } })
      s.chr = ok and c or nil
      if s.chr then spawn.hold(s.chr, true); pcall(spawn.scale, s.chr, 2.0) end
      stand.ticks = 0
    end
    return
  end
  local v = views[i]
  if not v then return end
  if ph == 0 then
    bond.warp(0, v.pad)
    bench.look(0, v.yaw, v.pitch)
    if v.foe then
      local bx, _, bz = pads.pos(v.pad)
      foe = spawn.chr(v.foe, { skin = "siberian_guard1", weapon = "sniperrifle", behaviour = "spectator", face = { bx, bz } })   -- (round 1's marksman)
      local ok, err = false, "no chr"
      if foe then ok, err = pcall(spawn.scale, foe, 2.0) end
      print("JSHOT foe chr " .. tostring(foe) .. " scale " .. tostring(ok) .. " " .. tostring(err))
    end
  elseif ph == 3 then
    bench.look(0, v.yaw, v.pitch)
    v.t0 = t
  elseif ph == 5 and v.hold and t - v.t0 < v.hold then
    ph = ph - 1   -- (held on the view so the run's stats line measures it)
  elseif ph == 6 then
    bench.shot()
    local x, y, z = bond.pos(0)
    print(string.format("JSHOT %s at (%.0f, %.0f, %.0f)", v.tag, x or 0, y or 0, z or 0))
  elseif ph == 8 then
    if foe then spawn.remove(foe); foe = nil end
    i, ph = i + 1, -1
  end
  ph = ph + 1
end)
