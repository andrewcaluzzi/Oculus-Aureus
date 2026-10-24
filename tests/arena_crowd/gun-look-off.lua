-- gun-look.lua with the impostor crowd off (crowd_impostors = 0): the control for the gun view-model check.
ARENA_SETTINGS = { start_delay = 100000, crowd_impostors = 0 }
include("arena")

local function pad_at(x, z)
  local best, bd = nil, 1e18
  for i = 0, pads.count() - 1 do
    local px, py, pz = pads.pos(i)
    if px and py < 1 then
      local d = (px - x) ^ 2 + (pz - z) ^ 2
      if d < bd then best, bd = i, d end
    end
  end
  return best
end

local t, step = 0, 0
on("tick", function(dt)
  t = t + dt
  if step == 0 and t > 0.5 then
    step = 1
    bond.warp(0, pad_at(0, 0))
  elseif step == 1 and t > 1 then
    step = 2
    bench.look(0, 0, 14)
  end
end)
