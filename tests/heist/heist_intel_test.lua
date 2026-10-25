-- Heist INTEL board test (designs/bank-heist.md section 19), on the Janus bank v2: the real mode with the board's content
-- (tools/heist_intel.lua). Prints "HEIST PASS|FAIL <name> ..." lines and "HEIST DONE n passed, m failed". With GE_INTEL_SHOTS=1 it also
-- takes one screenshot per page ("HEIST SHOT intel_<n>" before each). Run: tests/heist/run_intel.sh [shots].
--   content    the board builds for v2 without errors: 5 pages, every floor B2..F3 has rooms, every keyed door / safe is on a key card,
--              the static and live layers have items, the plan's checkboxes follow the run (got in, keys, the vault)
--   hints      a Bond at a BLUE door without the card gets "This door needs the BLUE card - the security chief ..."; at the vault door,
--              "Use the door decoder on the vault door"; rate-limited (no repeat within the minute)
HEIST_SETTINGS = { test = true, crack_time = 3, jam_chance = 0, vault_time = 90, briefing = true, guards_post = 0, guards_raiser = 0 }
include("heist")
mods.set("invincible", true)

local pass, fail = 0, 0
local function check(name, ok, extra)
  if ok then pass = pass + 1 else fail = fail + 1 end
  print(string.format("HEIST %s %s %s", ok and "PASS" or "FAIL", name, extra or ""))
end
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
local shots = true   -- (one BMP per page in the working directory; run_intel.sh converts them and deletes the BMPs)

local function body()
  while not HEIST.ready do wait(0.25) end
  wait(1.5)
  local I = HEIST.intel
  check("the board was built", I ~= nil and I.data ~= nil)
  if not I then print("HEIST DONE " .. pass .. " passed, " .. (fail + 1) .. " failed"); return end
  local D = I.data
  -- floors
  local names = {}
  for i, f in ipairs(D.floors) do names[#names + 1] = f.name .. "=" .. #f.rooms end
  check("six floors B2..F3", #D.floors == 6 and D.floors[1].name == "B2" and D.floors[6].name == "F3", table.concat(names, " "))
  local empty = {}
  for _, f in ipairs(D.floors) do if #f.rooms == 0 then empty[#empty + 1] = f.name end end
  check("every floor has rooms", #empty == 0, table.concat(empty, ","))
  local nrooms = 0
  for _, f in ipairs(D.floors) do nrooms = nrooms + #f.rooms end
  check("every room is on a floor", nrooms == #LEVEL.room_name, nrooms .. " of " .. #LEVEL.room_name)
  -- keys
  local keyed, listed = 0, 0
  for _, o in pairs(LEVEL.objects) do
    if (o.type == "door" or o.type == "safe") and (o.keyflags or 0) ~= 0 and o.keyflags ~= 0x40000000 then keyed = keyed + 1 end
  end
  for _, k in ipairs(D.keys) do listed = listed + #k.doors end
  check("every keyed door is on a key card", #D.unlisted == 0 and listed == keyed,
        string.format("%d keyed, %d listed, unlisted: %s", keyed, listed, table.concat(D.unlisted, ",")))
  local summary = {}
  for _, k in ipairs(D.keys) do
    summary[#summary + 1] = k.name .. ":" .. #k.doors
    check("key card " .. k.name .. " opens something", #k.doors > 0, I.opens_text(k))
  end
  print("HEIST info keys " .. table.concat(summary, " "))
  check("cameras on the map", #D.cams == #LEVEL.cameras, #D.cams .. " of " .. #LEVEL.cameras)
  local ns, nl = intel.count(false), intel.count(true)
  check("the static layer has items", ns > 300, tostring(ns))
  check("the live layer has items", nl > 20, tostring(nl))
  -- the briefing is off in automated runs (GE_FRAMES): the view stays shut
  local open = intel.view()
  check("no briefing in an automated run", open == false)
  -- the plan follows the run
  local d = I.plan_done()
  check("plan: not in yet on the street", d[1] == false)
  bond.warp(0, pad("hall_mid"))
  wait(1)
  d = I.plan_done()
  check("plan: got in (the hall)", d[1] == true)
  -- the camera feeds: what a watched camera sees goes on the board (guards on the map, carriers on THE KEYS)
  local n0 = 0
  for _ in pairs(I.seen_role) do n0 = n0 + 1 end
  check("feeds: nothing known before watching", n0 == 0, tostring(n0))
  bond.warp(0, pad("screens"))
  wait(0.5)
  local hallcams = {}
  for cam, area in pairs(HEIST.M.CAM_AREA) do if area == "hall" then hallcams[#hallcams + 1] = cam end end
  table.sort(hallcams)
  for _, cam in ipairs(hallcams) do cctv.view(0, cam); wait(1.0) end
  cctv.view_off(0)
  wait(0.5)
  local ng, nr, roles = 0, 0, {}
  for _ in pairs(I.seen) do ng = ng + 1 end
  for r in pairs(I.seen_role) do nr = nr + 1; roles[#roles + 1] = r end
  check("feeds: the hall cameras put someone on the board", ng + nr > 0, string.format("%d guards, carriers: %s", ng, table.concat(roles, ",")))
  -- hints: a BLUE door without the card
  local mgr = LEVEL.objects.manager_door
  local dw
  for _, x in ipairs(LEVEL.doors) do if x.name == mgr.doorway then dw = x end end
  bond.warp(0, dw.pad_a)                  -- the corridor side of the manager's (BLUE) door
  local got = nil
  for _ = 1, 24 do
    wait(0.25)
    local l = hud.line(3, 0)
    if l:find("needs ") then got = l; break end
  end
  check("hint: a keyed door names its card and carrier", got ~= nil and got:find("needs BLUE") ~= nil and got:find("security chief") ~= nil, got or hud.line(3, 0))
  -- rate limit: no new hint within 8 s
  bond.warp(0, pad("crack"))
  wait(2)
  check("hint: rate-limited (no second hint at once)", not hud.line(3, 0):find("door decoder"), hud.line(3, 0))
  local got2 = nil
  for _ = 1, 60 do
    wait(0.25)
    if hud.line(3, 0):find("door decoder") then got2 = hud.line(3, 0); break end
  end
  check("hint: at the vault door, the door decoder", got2 ~= nil, got2 or hud.line(3, 0))
  -- the crack and the vault tick the plan
  bond.test_gadget(0, "doordecoder")
  local t = 0
  while HEIST.phase ~= "open" and t < 20 do wait(0.25); t = t + 0.25 end
  wait(1)
  d = I.plan_done()
  check("plan: the vault ticked", d[4] == true, HEIST.phase)
  if shots then
    bond.warp(0, pad("hall_mid"))
    face = { 0, -800 }
    wait(1.5)
    for page = 1, 5 do
      intel.test_show(true, page, page == 2 and 3 or 0)
      wait(0.6)
      print("HEIST SHOT intel_" .. page)
      cctv.test_shot()
      wait(0.6)
    end
    intel.test_show(true, 2, 2)
    wait(0.6)
    print("HEIST SHOT intel_2_b1")
    cctv.test_shot()
    wait(0.6)
    intel.test_show(false)
    -- the map's handedness: from the hall's middle facing south (-z), the screen wall's west end (x -1080) must be on the LEFT of the
    -- view, as it is on the schematic (south down, so the board's x runs against the world's x)
    wait(0.6)
    print("HEIST SHOT intel_view_south")
    cctv.test_shot()
    wait(0.6)
  end
  print(string.format("HEIST DONE %d passed, %d failed", pass, fail))
  mission.fail("intel test done")
end

local co = coroutine.wrap(body)
local finished = false
on("tick", function(dt)
  if finished then return end
  local ok, err = pcall(co, dt)
  if not ok then finished = true; print("HEIST intel error: " .. tostring(err)); print(string.format("HEIST DONE %d passed, %d failed", pass, fail + 1)) end
end)
