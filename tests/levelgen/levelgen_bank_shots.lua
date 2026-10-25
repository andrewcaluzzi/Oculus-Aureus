-- The Janus bank v2's signature spaces, one screenshot each (designs/bank-heist.md section 17). Run: tests/levelgen/run_bank_shots.sh
-- (headless, Xvfb; build_pc built with tools/levelgen/janus_bank.json on the slot). No guards: the architecture only. Each shot follows a
-- "LEVELGEN SHOT <name>" line in the log; build_pc/levelgen_bank_shots_v2/shot_NNNNN.bmp (convert with tests/bmp2png.py).
include("levelgen_layout")
mods.set("invincible", true)

local view = nil         -- { yaw, pitch } held every tick (bench.look: degrees, the stock vv_theta / vv_verta)
local function wait(sec)
  local t = 0
  while t < sec do
    t = t + coroutine.yield()
    bond.heal(0, 1, 1)
    if view then bench.look(0, view[1], view[2]) end
  end
end

-- stand on a pad (or at an offset from it) and look at (x, z) with a pitch (degrees, up positive)
local function stand(padname, x, z, pitch, dx, dz)
  bond.warp(0, LEVEL.pads[padname], dx or 0, dz or 0)
  wait(0.6)
  local bx, _, bz = bond.pos(0)
  local yaw = math.deg(math.atan(-(x - bx), z - bz))
  if yaw < 0 then yaw = yaw + 360 end
  view = { yaw, pitch or 0 }
  wait(1.4)                                   -- the rooms in view load (a few a frame)
  local r = bond.room(0)
  return r and LEVEL.room_name[r] or "?"
end

local function shot(name, where)
  print(string.format("LEVELGEN SHOT %s (Bond in %s, %d rooms drawn)", name, where, #bond.rooms_drawn(0)))
  cctv.test_shot()
  wait(0.3)
end

local SHOTS = {
  -- name, pad, look at x, z, pitch, offset
  { "hall_from_gallery", "gal_w_mid", 1200, 1600, -12 },        -- the triple-height hall from the west gallery's balcony
  { "hall_floor_north", "hall_entry_w", 0, 3000, 12, 400, 0 },  -- from the blast shutters: the pillars, the galleries, the stair arch
  { "hall_floor_up", "hall_mid", -1200, 900, 32 },              -- up the west wall: arcade, gallery openings, the second floor's windows
  { "grand_stair", "stair_foot", 0, 4600, 10 },                 -- up the grand stair to the mezzanine
  { "mezzanine_down", "mezz_mid", 0, 3000, -18 },               -- back down the grand stair into the hall
  { "loggia_into_hall", "loggia_mid", -600, 0, -25, -600, 40 }, -- the second floor's loggia: down into the hall through its window
  { "gallery_south_street", "gal_s_w", -1100, -2100, -32, 140, -80 },   -- through the facade window: down to the street and the van
  { "open_plan_1", "op1_s", -3900, 3000, 0 },                   -- the first floor's open-plan office
  { "library", "lib_mid", 1800, 3900, 0, 0, -150 },             -- the second floor's library: the reading tables, the stacks
  { "roof", "roof_w", 4400, 2000, 4 },                          -- the roof at night
  { "garage", "garage_1", 3900, 3000, 4 },                      -- the double-height garage
  { "vault_corridor", "vcorr_w", 3360, 5620, 0 },               -- the vault level's corridor
  { "vault", "vault_in_a", 0, 7880, 0 },                        -- the vault
  { "plant_room", "power_1", -3800, 8400, 4 },                  -- the plant room: dynamos, the transformer
  { "tunnel", "tunnel_1w", 5440, 7260, 0 },                     -- the maintenance tunnel
  { "canal", "canal_s", 9080, 8400, 6 },                        -- the canal under the night sky
}

local function body()
  wait(1.5)
  for _, s in ipairs(SHOTS) do
    local where = stand(s[2], s[3], s[4], s[5], s[6], s[7])
    shot(s[1], where)
  end
  print("LEVELGEN DONE shots")
  mission.fail("levelgen bank shots done")
  wait(0.5)
  bench.exit()                                -- (a mission end alone keeps the game running to GE_FRAMES)
end

local co = coroutine.wrap(body)
local finished = false
on("tick", function(dt)
  if finished then return end
  local ok, err = pcall(co, dt)
  if not ok then finished = true; print("LEVELGEN error: " .. tostring(err)); print("LEVELGEN DONE shots") end
end)
