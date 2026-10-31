-- Dam+ walk-through (GE_MODE=dpwalk, GE_DAMPLUS=1): Bond walks on the real stick from the start canyon (room 135) through the
-- start shutter (door 281) into the motor pool (rooms 137, 138) and back. Checks: the shutter refuses Bond while locked, the
-- seam and every new floor carry him (room changes, no drop in height), each leg arrives in time. Prints DPWALK lines and a
-- final "DPWALK RESULT pass|fail".
-- New pads: stock 367, Night Shift 5, then the motor pool's design pads (waypoints W0..W6 = 372..378).
local ROUTE = { 321, 372, 373, 374, 375, 376, 377, 378, 377, 376, 375, 374, 373, 372, 321 }
local LEG_SECS = 25
local t, leg, legt, phase = 0, 1, 0, "start"
local fails, lastroom, miny, maxy = 0, nil, 1e9, -1e9
local rooms_seen = {}
local function fail(msg) fails = fails + 1; print("DPWALK FAIL " .. msg) end

mode.keep_locks()   -- at script load: a mode otherwise unlocks every door a second into the stage

on("tick", function(dt)
  t = t + dt
  if phase == "start" then
    if t < 2 then return end
    bond.warp(0, 321)
    local d = door.get(281)
    print(string.format("DPWALK door281 keyflags=0x%x state=%s", d and d.keyflags or -1, d and d.state or "nil"))
    if door.try(0, 281) then fail("the shutter opened without the key") end
    door.unlock(281)
    phase = "open"; legt = 0
    return
  end
  if phase == "open" then
    legt = legt + dt
    if legt < 0.5 then return end
    local ok = door.try(0, 281)
    print("DPWALK door281 try after unlock: " .. tostring(ok))
    if not ok then fail("the unlocked shutter did not open") end
    phase = "wait"; legt = 0
    return
  end
  if phase == "wait" then
    legt = legt + dt
    if legt > 3 then phase = "walk"; legt = 0 end
    return
  end
  if phase ~= "walk" then return end
  local x, y, z = bond.pos(0)
  if not x then return end
  miny = math.min(miny, y); maxy = math.max(maxy, y)
  local r = bond.room(0)
  if r ~= lastroom then
    print(string.format("DPWALK room %s -> %s at %.0f %.0f %.0f (t %.1f)", tostring(lastroom), tostring(r), x, y, z, t))
    lastroom = r
    rooms_seen[r or -1] = true
  end
  local tx, _, tz = pads.pos(ROUTE[leg])
  local left = test.walk(0, tx, tz)
  legt = legt + dt
  if left and left < 45 then
    print(string.format("DPWALK leg %d reached pad %d in %.1f s (room %s)", leg, ROUTE[leg], legt, tostring(r)))
    leg = leg + 1; legt = 0
    if not ROUTE[leg] then
      test.walk_stop()
      if not rooms_seen[137] then fail("never in room 137") end
      if not rooms_seen[138] then fail("never in room 138") end
      if maxy - miny > 60 then fail(string.format("height varied %.0f..%.0f", miny, maxy)) end
      print(string.format("DPWALK heights %.0f..%.0f", miny, maxy))
      print("DPWALK drawn from the barracks door side: " .. table.concat(test.rooms_drawn(0) or {}, ","))
      print("DPWALK RESULT " .. (fails == 0 and "pass" or "fail"))
      phase = "done"
    end
  elseif legt > LEG_SECS then
    fail(string.format("leg %d to pad %d timed out at %.0f %.0f (room %s, %.0f left)", leg, ROUTE[leg], x, z, tostring(r), left or -1))
    print("DPWALK RESULT fail")
    phase = "done"
  end
end)
