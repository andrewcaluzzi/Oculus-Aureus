-- Does the AI's line (combat.los: the stan tiles) agree with the drawn geometry (probe.los: the background hit test a bullet uses, every room)?
-- With Bond on each candidate pad (SGEO_BOND = "158,140,..." or the defs' list) every waypoint pad in the 60 to 150 m band is tried: the stan line
-- (a stand-in on the pad, combat.los) and the geometry line from Bond's eye to the head (+165) and to the rifle (+140), both ways. Prints
-- "SGEO bond <pad>: stan n, geometry-too m, ids ..." and the failing ones, the survey behind SNIPER_DEFS.PERCH.pads. tests/sniper/geom.sh
local list = {}
if SGEO_BOND then for n in tostring(SGEO_BOND):gmatch("%d+") do list[#list + 1] = tonumber(n) end else list = { 158, 140, 129, 124, 54, 53, 139 } end
local lo, hi = SGEO_LO or 6000, SGEO_HI or 15000
local bi, st, wait, group = 0, "next", 0, nil
local ids, res, eye, B = {}, nil, nil, nil
local k = 1

on("stage_start", function()
  for _, id in ipairs(pads.far_from(0, 0, 0, 1e9, true)) do ids[#ids + 1] = id end
  table.sort(ids)
  if tostring(SGEO_BOND) == "high" then list = {}; for _, id in ipairs(ids) do local _, y = pads.pos(id); if y >= (SGEO_Y or 250) then list[#list + 1] = id end end end
  print(string.format("SGEO start: %d waypoint pads, %d Bond candidates", #ids, #list))
end)

local function geom(ex, ey, ez, x, y, z)
  for _, h in ipairs({ 165, 140 }) do
    if not probe.los(ex, ey, ez, x, y + h, z) then return false end
    if not probe.los(x, y + h, z, ex, ey, ez) then return false end
  end
  return true
end

on("ui_tick", function()
  if #ids == 0 then return end
  if st == "next" then
    bi = bi + 1
    B = list[bi]
    if not B then print("SGEO done"); st = "done"; return end
    bond.warp(0, B)
    res, k, wait, st = { stan = {}, both = {}, fail = {} }, 1, 4, "settle"
  elseif st == "settle" then
    wait = wait - 1
    if wait <= 0 then
      local _, _, ex, ey, ez = gadget.aim(0)
      eye = { ex, ey, ez }
      local function t(x2, y2, z2) local c, r = probe.los(ex, ey, ez, x2, y2, z2); return tostring(c) .. "/r" .. r end
      print(string.format("SGEO sanity: 1 m ahead %s, 20 m ahead %s, 100 m ahead %s, 5 m down %s, 100 m down %s, 20 m up %s", t(ex + 100, ey, ez), t(ex + 2000, ey, ez), t(ex + 10000, ey, ez), t(ex, ey - 500, ez), t(ex, ey - 10000, ez), t(ex, ey + 2000, ez)))
      st = "spawn"
    end
  elseif st == "spawn" then
    if k > #ids then
      local parts, fails = {}, {}
      table.sort(res.both, function(a, b) return a[1] < b[1] end)
      for _, r in ipairs(res.both) do parts[#parts + 1] = string.format("%d@%.0f", r[1], r[2]) end
      for _, r in ipairs(res.fail) do fails[#fails + 1] = string.format("%d@%.0f", r[1], r[2]) end
      print(string.format("SGEO bond %d eye (%.0f, %.0f, %.0f): stan sees %d, stan AND geometry %d, stan-only (the glitch) %d", B, eye[1], eye[2], eye[3], #res.stan,
        #res.both, #res.fail))
      print("SGEO both " .. B .. ": " .. table.concat(parts, " "))
      print("SGEO glitch " .. B .. ": " .. table.concat(fails, " "))
      st = "next"
      return
    end
    group = {}
    local px, _, pz = pads.pos(B)
    for _ = 1, 12 do
      local pad = ids[k]
      if not pad then break end
      k = k + 1
      local x, y, z = pads.pos(pad)
      local d = math.sqrt((x - px) ^ 2 + (z - pz) ^ 2)
      if pad ~= B and d >= lo and d <= hi and y >= 60 then
        local ok, c = pcall(spawn.chr, pad, { behaviour = "spectator", weapon = "none" })
        if ok and c then group[#group + 1] = { pad = pad, chr = c, x = x, y = y, z = z, d = d } end
      end
    end
    st = "ask"
  elseif st == "ask" then
    for _, g in ipairs(group) do
      if combat.los(g.chr, 0) then
        res.stan[#res.stan + 1] = g.pad
        if geom(eye[1], eye[2], eye[3], g.x, g.y, g.z) then res.both[#res.both + 1] = { g.pad, g.d } else res.fail[#res.fail + 1] = { g.pad, g.d } end
      end
      spawn.remove(g.chr)
    end
    group, st = nil, "spawn"
  end
end)
