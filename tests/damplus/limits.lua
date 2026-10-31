-- Dam+ slice 3 (GE_MODE=dplimits, GE_DAMPLUS=1, GE_DAMPLUS_TESTROOMS=10): the raised limits. Ten test rooms (ids 139-148) hang
-- off the barracks in a chain, so Dam has 148 rooms (the most MAXROOMCOUNT 150 allows) and 206 portals (PORTMAX was 200).
-- Bond walks on the stick from the barracks down the chain to its end; every room must be entered in order, drawn and walked
-- on, with no drop in height. Prints DPLIM lines and "DPLIM RESULT pass|fail".
local W5, W6 = 377, 378          -- motor-pool waypoint pads in the barracks (design v 2200 and 2600)
local N = 10
local t, k, legt, phase, fails = 0, 0, 0, "start", 0
local seen, lastroom, miny, maxy = {}, nil, 1e9, -1e9
local function fail(m) fails = fails + 1; print("DPLIM FAIL " .. m) end

local function target(i)         -- centre of test room i (0-based): design v = 2900 + 400 i + 200
  local x5, _, z5 = pads.pos(W5)
  local x6, _, z6 = pads.pos(W6)
  local f = (2900 + 400 * i + 200 - 2600) / 400
  return x6 + (x6 - x5) * f, z6 + (z6 - z5) * f
end

on("tick", function(dt)
  t = t + dt
  if phase == "start" then
    if t < 2 then return end
    bond.warp(0, W6)
    phase = "walk"; legt = 0
    return
  end
  if phase ~= "walk" then return end
  local x, y, z = bond.pos(0)
  if not x then return end
  miny = math.min(miny, y); maxy = math.max(maxy, y)
  local r = bond.room(0)
  if r ~= lastroom then
    print(string.format("DPLIM room %s -> %s at %.0f %.0f %.0f drawn %s", tostring(lastroom), tostring(r), x, y, z,
      table.concat(test.rooms_drawn(0) or {}, ",")))
    lastroom = r
    if r then seen[r] = true end
  end
  local tx, tz = target(k)
  local left = test.walk(0, tx, tz)
  legt = legt + dt
  if left and left < 50 then
    if r ~= 139 + k then fail(string.format("at test room %d's centre but in room %s", 139 + k, tostring(r))) end
    k = k + 1; legt = 0
    if k >= N then
      test.walk_stop()
      for i = 0, N - 1 do if not seen[139 + i] then fail("never in room " .. (139 + i)) end end
      if maxy - miny > 60 then fail(string.format("height varied %.0f..%.0f", miny, maxy)) end
      print(string.format("DPLIM reached room %s, heights %.0f..%.0f", tostring(r), miny, maxy))
      print("DPLIM RESULT " .. (fails == 0 and "pass" or "fail"))
      phase = "done"
    end
  elseif legt > 20 then
    fail(string.format("leg to test room %d timed out in room %s (%.0f left)", 139 + k, tostring(r), left or -1))
    print("DPLIM RESULT fail")
    phase = "done"
  end
end)
