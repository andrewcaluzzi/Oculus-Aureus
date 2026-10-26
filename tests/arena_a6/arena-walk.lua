-- A6: does Bond collect a spawn.item pickup by WALKING onto it (real stick input, test.walk), and what does a warp do?
-- GE_MODE=arena-walk GE_STAGE=colosseum GE_SKIPINTRO=1 GE_CHEATS=invuln (no waves: this is a bare script)
local t, ti, st, st_t = 0, 0, "wait", 0
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
local TESTS = {
  { item = "pp7",     at = { 1500, 0 },     mode = "walk" },
  { item = "shotgun", at = { 0, 1500 },     mode = "walk", opts = { ammo = 37 } },
  { item = "kf7",     at = { -1500, 0 },    mode = "walk" },
  { item = "armour",  at = { 0, -1500 },    mode = "walk", opts = { amount = 0.8 } },
  { item = "pp7",     at = { 1500, 0 },     mode = "walk" },          -- Bond already has a PP7: ammo goes up
  { item = "shotgun", at = { 0, 1500 },     mode = "warp", off = { 20, 20 } },   -- a warped Bond, 20/20 off the pad centre (a tile of its own)
  { item = "shotgun", at = { 0, 1500 },     mode = "warp", off = { 0, 0 } },     -- the A5 failure? exactly on the pad centre = on a tile edge
}
-- between tests Bond walks back to the pit centre (a straight line from the centre along an axis is clear of the pillars / low walls)
local function home()
  local d = test.walk(0, 0, 0)
  if d and d < 120 then test.walk_stop(); return true end
  return false
end
local cur
local function vit() local h, a = bond.vitals(0); return string.format("health %.2f armour %.2f", h or -1, a or -1) end
local function ammo(item)
  if item == "armour" then return vit() end
  local has, n = bond.ammo(0, item)
  return string.format("has %s reserve %s", tostring(has), tostring(n))
end
on("tick", function(dt)
  t = t + dt
  st_t = st_t + dt
  if st == "wait" and t > 1 and home() then st, st_t = "next", 0 end
  if st == "next" then
    ti = ti + 1
    cur = TESTS[ti]
    if not cur then print("ge: walktest: ALL DONE"); st = "done"; return end
    local pad = nearest_pad(cur.at[1], cur.at[2])
    cur.pad = pad
    cur.h = spawn.item(pad, cur.item, cur.opts)
    cur.x, cur.z = spawn.item_pos(cur.h or 0)
    print(string.format("ge: walktest: #%d %s %s: handle %s pad %d at (%s,%s) alive %s | before: %s", ti, cur.mode, cur.item, tostring(cur.h), pad,
      tostring(cur.x), tostring(cur.z), tostring(spawn.item_alive(cur.h or 0)), ammo(cur.item)))
    cur.before = ammo(cur.item)
    st, st_t = "settle", 0
  elseif st == "settle" and st_t > 2 then
    print(string.format("ge: walktest: #%d settled, alive %s", ti, tostring(spawn.item_alive(cur.h or 0))))
    if cur.mode == "warp" then
      print("ge: walktest: warp", tostring(bond.warp(0, cur.pad, cur.off[1], cur.off[2])))
    end
    st, st_t = cur.mode, 0
  elseif st == "walk" then
    local x, z = spawn.item_pos(cur.h)
    local bx, _, bz = bond.pos(0)
    if x then
      local d = test.walk(0, x, z)
      if math.floor(st_t * 2) ~= math.floor((st_t - dt) * 2) then
        print(string.format("ge: walktest: #%d walking, dist to item %.0f (bond %.0f,%.0f)", ti, d, bx, bz))
      end
    else
      test.walk_stop()
      print(string.format("ge: walktest: #%d COLLECTED after %.1fs | after: %s (before: %s)", ti, st_t, ammo(cur.item), cur.before))
      st, st_t = "wait", 0; t = 0
    end
    if x and st_t > 25 then
      test.walk_stop()
      print(string.format("ge: walktest: #%d TIMEOUT, still on the floor | %s", ti, ammo(cur.item)))
      spawn.item_remove(cur.h)
      st, st_t = "wait", 0; t = 0
    end
  elseif st == "warp" then
    if not spawn.item_alive(cur.h) then
      print(string.format("ge: walktest: #%d (warp) COLLECTED after %.1fs | after: %s (before: %s)", ti, st_t, ammo(cur.item), cur.before))
      st, st_t = "wait", 0; t = 0
    elseif st_t > 6 then
      print(string.format("ge: walktest: #%d (warp) NOT collected in 6s | %s", ti, ammo(cur.item)))
      spawn.item_remove(cur.h)
      st, st_t = "wait", 0; t = 0
    end
  end
end)
