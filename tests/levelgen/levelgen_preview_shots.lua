-- A walk-through of any generated layout, one screenshot per room: Bond's start, then the rooms PREVIEW.rooms names, or else the
-- largest rooms of the layout (tools/levelgen/preview.sh writes PREVIEW into levelgen_preview_cfg.lua). No guards, Bond invincible.
-- Each shot follows a "LEVELGEN SHOT <name>" line in the log; the BMPs land in the working directory (preview.sh converts them).
include("levelgen_layout")
include("levelgen_preview_cfg")
mods.set("invincible", true)
PREVIEW = PREVIEW or {}

local view = nil         -- { yaw, pitch } held every tick (bench.look: degrees)
local function wait(sec)
  local t = 0
  while t < sec do
    t = t + coroutine.yield()
    bond.heal(0, 1, 1)
    if view then bench.look(0, view[1], view[2]) end
  end
end

local function look_at(x, z, pitch)
  local bx, _, bz = bond.pos(0)
  local yaw = math.deg(math.atan(-(x - bx), z - bz))
  if yaw < 0 then yaw = yaw + 360 end
  view = { yaw, pitch or 0 }
end

local function shot(name)
  local r = bond.room(0)
  print(string.format("LEVELGEN SHOT %s (Bond in %s, %d rooms drawn)", name, r and LEVEL.room_name[r] or "?", #bond.rooms_drawn(0)))
  cctv.test_shot()
  wait(0.3)
end

-- stand on the room's first grid pad (its south-west corner) and look across it to the far corner, a little down
local function room_shot(name)
  local R = LEVEL.rooms[name]
  if not R or not R.pads or #R.pads == 0 then print("LEVELGEN SKIP " .. name .. " (no pads)"); return end
  bond.warp(0, R.pads[1], 0, 0)
  wait(0.6)
  local x0, z0, x1, z1 = R.rect[1], R.rect[2], R.rect[3], R.rect[4]
  look_at(x0 + (x1 - x0) * 0.8, z0 + (z1 - z0) * 0.8, -6)
  wait(1.4)                                   -- the rooms in view load (a few a frame)
  shot(name)
end

local function pick_rooms()
  if PREVIEW.rooms and #PREVIEW.rooms > 0 then return PREVIEW.rooms end
  local all = {}
  for id = 1, #LEVEL.room_name do
    local n = LEVEL.room_name[id]
    local R = LEVEL.rooms[n]
    if not R.ramp and R.pads and #R.pads > 0 then
      all[#all + 1] = { n, (R.rect[3] - R.rect[1]) * (R.rect[4] - R.rect[2]), id }
    end
  end
  table.sort(all, function (a, b) if a[2] ~= b[2] then return a[2] > b[2] end return a[3] < b[3] end)
  local out = {}
  for k = 1, math.min(PREVIEW.n or 5, #all) do out[#out + 1] = all[k][1] end
  return out
end

local function body()
  wait(1.5)
  -- the start, looking the way the layout faces Bond
  bond.warp(0, LEVEL.pads.start, 0, 0)
  wait(1.5)
  shot("start")
  for _, n in ipairs(pick_rooms()) do room_shot(n) end
  print("LEVELGEN DONE preview")
  mission.fail("levelgen preview done")
  wait(0.5)
  bench.exit()
end

local co = nil
on("tick", function (dt)
  if not co then co = coroutine.create(body) end
  if coroutine.status(co) ~= "dead" then
    local ok, err = coroutine.resume(co, dt)
    if not ok then print("LEVELGEN error " .. tostring(err)); bench.exit() end
  end
end)
