-- Heist screenshot tour (P0 job 6): the real mode, Bond warped to a few spots and held there so GE_SHOT captures them.
-- Run: tests/heist/run_shots.sh (headless; GE_SHOT=<frames> writes shot_NNNNN.bmp in build_pc/).
--   ~0-8 s   the street, the briefing on the HUD        ~8-16 s  the banking hall: guards on post, staff at the counter
--   ~16-24 s the security wing                          ~24-34 s the vault: open, gold on the floor, the countdown
HEIST_SETTINGS = { crack_time = 3, jam_chance = 0, vault_time = 90 }
include("heist")
mods.set("invincible", true)

local face = nil
local function wait(sec)
  local t = 0
  while t < sec do
    t = t + coroutine.yield()
    bond.heal(0, 1, 1)
    if face then bond.face(0, face[1], face[2]) end
  end
end
local function look_at_pad(p) local x, _, z = pads.pos(p); face = { x, z } end

local function body()
  while not HEIST.ready do wait(0.25) end
  print("HEIST SHOT street " .. HEIST.elapsed)
  wait(7)
  bond.warp(0, 63)            -- the hall's south end, looking north over the floor
  look_at_pad(61)
  print("HEIST SHOT hall " .. HEIST.elapsed)
  wait(8)
  bond.warp(0, 83)            -- security wing (room 43, the camera console)
  look_at_pad(82)
  print("HEIST SHOT security " .. HEIST.elapsed)
  wait(8)
  bond.warp(0, 85)
  face = nil
  bond.test_gadget(0, "doordecoder")
  while HEIST.phase ~= "open" do wait(0.25) end
  wait(1.5)
  bond.warp(0, 89)            -- inside vault door A, looking into the gold room
  look_at_pad(31)
  print("HEIST SHOT vault " .. HEIST.elapsed)
  wait(10)
  print("HEIST SHOT done")
  mission.fail("shots done")
end

local co = coroutine.wrap(body)
local finished = false
on("tick", function(dt)
  if finished then return end
  local ok, err = pcall(co, dt)
  if not ok then finished = true; print("HEIST shots error: " .. tostring(err)) end
end)
