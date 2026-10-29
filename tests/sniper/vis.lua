-- How far away is a man drawn and still seen? (tests/sniper/vis.sh) Bond on his perch; a marksman put on the listed perches nearest 40, 55, 70, 90, 110 m
-- in turn, Bond looking at him, a picture of each (bench.shot; the runner turns them into full and 3x crops). Prints "SVIS ..." lines.
include("_sniper_defs")
local D = SNIPER_DEFS
local want = { 4000, 5500, 7000, 9000, 11000 }
local t, step, chr, list = 0, 0, nil, nil
local bp = D.BOND.pads[1]
on("stage_start", function()
  bond.warp(0, bp)
  print(string.format("SVIS fog: props drawn out to %.0f units", probe.fog()))
end)
on("tick", function(dt)
  t = t + dt
  if t < 2 then return end
  local bx, _, bz = pads.pos(bp)
  if not list then
    list = {}
    for _, w in ipairs(want) do
      local best, bd
      for _, id in ipairs(D.PERCH.pads) do
        local x, y, z = pads.pos(id)
        local d = math.sqrt((x - bx) ^ 2 + (z - bz) ^ 2)
        if not bd or math.abs(d - w) < bd then best, bd = id, math.abs(d - w) end
      end
      list[#list + 1] = best
    end
  end
  local phase = (step % 3)
  local i = step // 3 + 1
  if i > #list then return end
  local pad = list[i]
  local x, y, z = pads.pos(pad)
  if phase == 0 then
    chr = spawn.chr(pad, { skin = "arctic_commando", weapon = "sniperrifle", behaviour = "spectator", face = { bx, bz } })
    local _, _, _, ey = gadget.aim(0)
    local d = math.sqrt((x - bx) ^ 2 + (z - bz) ^ 2)
    bench.look(0, math.deg(math.atan(-(x - bx), (z - bz))), -math.deg(math.atan(((ey or 0) - (y + 20)) / d)))
    print(string.format("SVIS %d m: pad %d chr %s", math.floor(d / 100), pad, tostring(chr)))
  elseif phase == 1 then
    bench.shot()
  else
    if chr then spawn.remove(chr) end
  end
  step = step + 1
end)
