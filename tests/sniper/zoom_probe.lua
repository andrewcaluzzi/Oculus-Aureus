-- The zoom bug probe (sniper v2): Bond on his start with the REAL sniper scope forced on (test.aim) at a set zoom, pitching up step by step,
-- one picture a step. With GE_SKY_PROBE=1 the sky code logs the eye height against the sky (cloud plane) height and which screen corners
-- look up into it. Stage: the snowfield (GE_STAGE=levelgen GE_LEVELGEN=snowfield) or stock Surface (Bond on pad 53, looking at the far slope).
local LG = levelgen and levelgen.current and levelgen.current()
local SNOW = LG == "snowfield" or LG == "jungle"   -- (a generated sniper stage: Bond on b1 looking at f3)
local P = nil
if SNOW then include("levelgen_layout_" .. LG); P = LEVEL.pads end
local steps = { { 7, -2 }, { 7, 1 }, { 7, 3 }, { 15, 1 }, { 30, 1 }, { 0, 1 }, { 0, 5 }, { 0, 12 }, { 7, 12 } }   -- { zoom fov (0 = not aiming), pitch }
local i, ph, t = 0, 0, 0
local yaw = SNOW and 0 or 200
on("tick", function(dt)
  t = t + dt
  if t < 1.5 then return end
  if i == 0 then
    local pad = SNOW and P.b1 or 53
    bond.warp(0, pad)
    pcall(bond.give, 0, "sniperrifle", 60)
    pcall(bond.equip, 0, "sniperrifle")
    if SNOW and P.f3 then
      local x0, _, z0 = pads.pos(P.b1)
      local x1, _, z1 = pads.pos(P.f3)
      yaw = math.deg(math.atan(-(x1 - x0), (z1 - z0)))
    end
    i, ph = 1, -8
    print(string.format("ZPROBE start: %s, Bond on pad %d, yaw %.1f", SNOW and LG or "surface", pad, yaw))
    return
  end
  ph = ph + 1
  if ph < 0 then return end
  local s = steps[i]
  if not s then return end
  if ph == 1 then
    test.aim(0, s[1] > 0, s[1])
    bench.look(0, yaw, s[2])
  elseif ph == 4 then
    bench.look(0, yaw, s[2])
  elseif ph == 6 then
    bench.shot()
    local _, _, p, _, ey = pcall(gadget.aim, 0)
    local a, it, fov = sniper.view(0)
    print(string.format("ZPROBE shot %d: zoom %d pitch %d -> view pitch %.1f, eye y %.0f, aiming %s item %s fov %.1f, rooms drawn %s", i, s[1], s[2], p or -99,
      ey or -1, tostring(a), tostring(it), fov or -1, table.concat(test.rooms_drawn(0) or {}, ",")))
  elseif ph == 7 then
    i, ph = i + 1, 0
  end
end)
