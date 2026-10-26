-- Screenshots of the casino job (designs/job2-casino.md): the real mode on build_job2, Bond warped to a few spots, one cctv.test_shot()
-- each (sparse). Run: tests/heist/run_job2.sh shots. Each shot follows a "HEIST SHOT <name>" line in the log.
--   floor     the Salle Europe from the Renaissance's arch: the tables, croupiers and punters, guards on post, the HUD
--   catwalk   the eye in the sky: the west catwalk, looking through the one-way glass down onto the floor
--   intel     the INTEL board: THE PLAN (this job's words), then the SCHEMATICS of the ground floor
--   vault     the house vault open: the reserve on the floor, the countdown
HEIST_SETTINGS = { test = true, crack_time = 3, jam_chance = 0, vault_time = 90, briefing = false }
include("heist")
mods.set("invincible", true)
on("stage_start", function() mode.guards_wake(false) end)

local face = nil
local function wait(sec)
  local t = 0
  while t < sec do
    t = t + coroutine.yield()
    bond.heal(0, 1, 1)
    if face then bond.face(0, face[1], face[2]) end
  end
end
local function pad(n) return LEVEL.pads[n] end
local function look(x, z) face = { x, z } end
local function shot(name)
  print("HEIST SHOT " .. name .. string.format(" t=%.1f phase=%s", HEIST.elapsed, HEIST.phase))
  cctv.test_shot()
  wait(0.5)
end

local finished = false
local function body()
  while not HEIST.ready do wait(0.25) end
  wait(2)
  bond.warp(0, pad("floor_entry_w"))
  wait(0.3)
  bond.warp(0, LEVEL.pads.player_1, 300, -250)   -- inside the Salle Europe's south-west arch, looking north-east over the tables
  look(600, 3000)
  wait(1.5)
  shot("floor")
  bond.warp(0, pad("grapple_catwalk_high"))
  look(0, 2350)
  wait(1.5)
  shot("catwalk")
  face = nil
  intel.test_show(true, 1, 0)
  wait(1)
  shot("intel_plan")
  intel.test_show(true, 2, 2)
  wait(1)
  shot("intel_map")
  intel.test_show(false)
  bond.warp(0, pad("crack_point"))
  wait(0.5)
  bond.equip(0, "doordecoder")
  wait(1)
  bond.test_gadget(0, "doordecoder")
  local t = 0
  while HEIST.phase ~= "open" and t < 20 do wait(0.25); t = t + 0.25 end
  wait(2)
  bond.warp(0, pad("vault_in_a"))
  look(0, 7400)
  wait(1.5)
  shot("vault")
  print("HEIST SHOTS DONE")
  finished = true
  mission.fail("shots done")
end

local co = coroutine.wrap(body)
on("tick", function(dt)
  if finished then return end
  local ok, err = pcall(co, dt)
  if not ok then finished = true; print("HEIST FAIL script error: " .. tostring(err)) end
end)
