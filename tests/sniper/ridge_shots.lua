-- Pictures of SEVERNAYA RIDGE (tests/sniper/ridge_shots.sh; GE_STAGE=levelgen GE_LEVELGEN=ridge): an overview from the valley, the hunters' ridge from Bond's
-- window (a marksman on a perch, the runner's size), Bond's own window and parapet from the valley. bench.look sets the view, bench.shot takes it.
include("levelgen_layout_ridge")
local P = LEVEL.pads
local views = {
  { tag = "overview", pad = P.vp3, yaw = 180, pitch = 4 },                       -- the valley, the hunters' ridge ahead (north is +z: yaw 0 faces +z)
  { tag = "window", pad = P.sp4, yaw = 0, pitch = 0, foe = P.np3 },             -- from Bond's window across the valley, a marksman on np3
  { tag = "parapet", pad = P.vp2, yaw = 180, pitch = 8 },                        -- Bond's parapet and its windows from the valley
}
local t, i, ph, foe = 0, 1, 0, nil
local function look(v)
  local x, y, z = pads.pos(v.pad)
  bond.warp(0, v.pad)
  bench.look(0, v.yaw, v.pitch)
end
on("tick", function(dt)
  t = t + dt
  if t < 1.5 then return end
  local v = views[i]
  if not v then return end
  if ph == 0 then
    look(v)
    if v.foe then
      local bx, by, bz = pads.pos(v.pad)
      foe = spawn.chr(v.foe, { skin = "arctic_commando", weapon = "sniperrifle", behaviour = "spectator", face = { bx, bz } })
      if foe then pcall(spawn.scale, foe, 2.0) end
      print("RSHOT foe chr " .. tostring(foe))
    end
  elseif ph == 2 then
    look(v)
  elseif ph == 4 then
    bench.shot()
    print("RSHOT " .. v.tag)
  elseif ph == 5 then
    if foe then spawn.remove(foe); foe = nil end
    i, ph = i + 1, -1
  end
  ph = ph + 1
end)
