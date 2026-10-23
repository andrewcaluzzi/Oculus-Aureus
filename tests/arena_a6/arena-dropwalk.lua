-- A6: why did the A5 warp test not pick up a killed enemy's dropped gun? Two armed enemies are killed in turn; for the first Bond is WARPED
-- beside the drop (as arena-pickup.lua did, but waiting 8 s instead of calling the pickup function), for the second he WALKS onto it.
-- Logged: loose weapons on the floor (spawn.pools) and Bond's ammo for the gun before/after.
local t, st, st_t, k = 0, "wait", 0, 0
local chr, cx, cz
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
local function loose() local l = spawn.pools(); return l end
local function ammo() local has, n = bond.ammo(0, "ar33"); return string.format("has %s reserve %s", tostring(has), tostring(n)) end
local MODES = { "warp", "walk" }
on("tick", function(dt)
  t = t + dt; st_t = st_t + dt
  if st == "wait" and t > 2 then
    k = k + 1
    if k > #MODES then print("ge: droptest: ALL DONE"); st = "done"; return end
    local pad = nearest_pad(2000 + k * 300, (k == 1) and 0 or 700)
    chr = spawn.chr(pad, { skin = "russian_soldier", weapon = "ar33", behaviour = "hold", health = 1 })
    print(string.format("ge: droptest: #%d (%s) spawned chr %s on pad %s, loose %d", k, MODES[k], tostring(chr), tostring(pad), loose()))
    st, st_t = "kill", 0
  elseif st == "kill" and st_t > 2 then
    cx, cz = spawn.where(chr)
    print(string.format("ge: droptest: #%d kill %s at (%.0f,%.0f)", k, tostring(spawn.kill(chr)), cx, cz))
    st, st_t = "dropped", 0
  elseif st == "dropped" and st_t > 4 then
    print(string.format("ge: droptest: #%d drop lying: loose %d | %s", k, loose(), ammo()))
    if MODES[k] == "warp" then
      local pad = nearest_pad(cx, cz)
      local px, _, pz = pads.pos(pad)
      print("ge: droptest: warp", tostring(bond.warp(0, pad, cx - px + 37, cz - pz + 41)))   -- exactly as A5: 37/41 beside the corpse
    end
    st, st_t = MODES[k], 0
  elseif st == "warp" then
    if st_t > 8 then
      print(string.format("ge: droptest: #%d (warp) after 8s: loose %d | %s", k, loose(), ammo()))
      st, st_t = "wait", 0; t = 0
    end
  elseif st == "walk" then
    local d = bond.walk(0, cx, cz)
    if loose() == 0 or st_t > 20 or d < 40 then
      if st_t > 3 or loose() == 0 then
        bond.walk_stop()
        print(string.format("ge: droptest: #%d (walk) after %.1fs, dist %.0f: loose %d | %s", k, st_t, d, loose(), ammo()))
        st, st_t = "wait", 0; t = 0
      end
    end
  end
end)
