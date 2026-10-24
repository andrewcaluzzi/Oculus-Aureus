-- Impostor crowd look test (GE_MODE=crowd-look, GE_STAGE=colosseum, GE_CHEATS=invuln, GE_SHOT=300, GE_FRAMES=1000):
-- shot at 10 s: the stands from the middle of the pit; at 14 s the crowd cheers and Bond is near the north wall, shot at 15 s.
ARENA_SETTINGS = { start_delay = 100000 }
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
  if step == 0 and t > 1 then
    step = 1
    bond.warp(0, pad_at(0, 0))
  elseif step == 1 and t > 3 then
    step = 2
    bench.look(0, 0, 14)
  elseif step == 2 and t > 13 then
    step = 3
    bond.warp(0, pad_at(0, 3600))
    bench.look(0, 0, 24)
  elseif step == 3 and t > 14 then
    step = 4
    if ARENA_HOOKS and ARENA_HOOKS.cheer then ARENA_HOOKS.cheer(6, 1.0) end
    print("ge: look: cheer")
  end
end)
