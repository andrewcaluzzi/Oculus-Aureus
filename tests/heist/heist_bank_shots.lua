-- Heist screenshots on the Janus bank (P2.4): the real mode, Bond warped to a few spots, one cctv.test_shot() each (sparse).
-- Run: tests/heist/run_bank_shots.sh (headless). Each shot follows a "HEIST SHOT <name>" line in the log.
--   hall     the banking hall from the blast shutters: guards on post, staff at the counter, the HUD
--   screens  the hall's screen wall (the monitors' pictures)
--   feed     a use press there: the camera feed (the customer-side camera) with its overlay and the intel box
--   vault    the vault open, gold on the floor, the countdown
--   fight    the alarm raised: the lockdown, a wave in the hall shooting at (invincible) Bond
HEIST_SETTINGS = { test = true, crack_time = 3, jam_chance = 0, vault_time = 90, wave_first = 2, wave_interval = 6 }
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
local function pad(n) return LEVEL.pads[n] end
local function look(x, z) face = { x, z } end
local function shot(name)
  print("HEIST SHOT " .. name .. string.format(" t=%.1f phase=%s hot=%s", HEIST.elapsed, HEIST.phase, tostring(HEIST.hot)))
  cctv.test_shot()
  wait(0.5)
end

local function body()
  while not HEIST.ready do wait(0.25) end
  wait(1)
  bond.warp(0, pad("hall_entry_e"))         -- just inside the east blast shutter, looking north-west over the customer side to the counter
  look(300, 600)
  wait(1.5)
  shot("hall")
  bond.warp(0, pad("cust_5"))              -- 200 units back from the screen wall (x -300), looking at it
  look(-300, 0)
  wait(1.5)
  shot("screens")
  bond.warp(0, pad("screens"))
  wait(1)
  cctv.test_use(0)
  wait(1.5)
  shot("feed")
  cctv.view_off(0)
  wait(1)
  face = nil
  bond.warp(0, pad("crack"))
  wait(0.5)
  bond.test_gadget(0, "doordecoder")
  while HEIST.phase ~= "open" do wait(0.25) end
  wait(2)
  bond.warp(0, pad("vault_in_a"))
  look(900, 3850)
  wait(1.5)
  shot("vault")
  bond.warp(0, pad("teller_mid"))           -- behind the counter, looking south over it to the shutters
  look(700, -380)
  alarm.start()
  wait(12)
  shot("fight")
  wait(3)
  shot("fight2")
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
