-- A7: how spawn.scale and the LOW VISIBILITY fog look (run with GE_SHOT=600 GE_FRAMES=620; figheight.py measures the figures). No waves
-- (start_delay 60), crowd 100. At 1 s three soldiers (same body and head) stand 1000 units ahead of Bond, at x -500 / 0 / +500 from him,
-- spawn.scale 1 / 1.15 / 1/1.15. At 5 s the fog comes on (arena.fog 2600), at 8 s it goes off again.
-- Mind: +x is on the screen's LEFT when Bond faces +z. Result (shot 600, in the fog): screen right (x1) 105 px, centre (x1.15) 120 px,
-- screen left (x0.87) 91 px: the drawn size follows the scale.
ARENA_SETTINGS = { mode = "endless", start_delay = 60, crowd = 100, ammo_every = 0 }
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
    local bx, _, bz = bond.pos(0)
    local function one(dx, sc)   -- the same body and head, 1000 units ahead of Bond, dx to the side, drawn x sc
      local c = spawn.chr(pad_at(bx + dx, bz + 1000), { body = "Russian_Soldier", head = "Male_Karl", weapon = "none", behaviour = "spectator",
                                                        face = { bx, bz } })
      if c and sc ~= 1 then spawn.scale(c, sc) end
      return c
    end
    print("ge: arena: A7TEST look left x1", one(-500, 1), "centre x1.15", one(0, 1.15), "right x1/1.15", one(500, 1 / 1.15))
  elseif step == 1 and t > 5 then
    step = 2
    print("ge: arena: A7TEST look fog", arena.fog(2600), arena.fog())
  elseif step == 2 and t > 8 then
    step = 3
    print("ge: arena: A7TEST look fog off", arena.fog(0), arena.fog())
  end
end)
