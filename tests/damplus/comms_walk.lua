-- Dam+ concept 5 walk-through (GE_MODE=dpcomms, GE_DAMPLUS=comms, GE_ROOM_AUDIT=1): Bond walks on the real stick from the valley
-- yard (room 111) through the doorway cut into its west wall, down the tunnel to the radio room (139), up the stair to the
-- blockhouse and onto the roof deck (140), and back to the yard. Checks: every leg arrives in time, both new rooms are entered,
-- Bond's height at each stop is the floor there (yard and radio room -107, stair top and deck +333, nothing in between but the
-- stair), and the doorway and the blockhouse door are drawn (rooms drawn at each stop are printed). "DRAW UNLOADED" in the log
-- (GE_ROOM_AUDIT) is checked by the runner. Prints DPCOMMS lines and a final "DPCOMMS RESULT pass|fail".
-- Pads: stock 367; comms pads (GE_DAMPLUS=comms alone): 367 tunnel, 368 radio room, 369 by the stair opening, 370 stair foot,
-- 371 blockhouse, 372 deck.
-- Pad 121: the stock yard waypoint the annex's waypoints join.
local ROUTE = { 121, 367, 368, 369, 370, 371, 372, 371, 370, 369, 368, 367, 121 }
local LEG_SECS = 25
local t, leg, legt, phase = 0, 1, 0, "start"
local fails, lastroom = 0, nil
local rooms_seen = {}
local function fail(msg) fails = fails + 1; print("DPCOMMS FAIL " .. msg) end

on("tick", function(dt)
  t = t + dt
  if phase == "start" then
    if t < 2 then return end
    bond.warp(0, 121)
    phase = "walk"; legt = 0
    return
  end
  if phase ~= "walk" then return end
  local x, y, z = bond.pos(0)
  if not x then return end
  if y < -140 or y > 370 then fail(string.format("height %.0f out of the floors' range at %.0f %.0f", y, x, z)) end
  local r = bond.room(0)
  if r ~= lastroom then
    print(string.format("DPCOMMS room %s -> %s at %.0f %.0f %.0f (t %.1f) drawn %s", tostring(lastroom), tostring(r), x, y, z, t,
      table.concat(test.rooms_drawn(0) or {}, ",")))
    lastroom = r
    rooms_seen[r or -1] = true
  end
  local tx, ty, tz = pads.pos(ROUTE[leg])
  local left = test.walk(0, tx, tz)
  legt = legt + dt
  if left and left < 45 then
    local floor = ty - 51
    print(string.format("DPCOMMS leg %d reached pad %d in %.1f s (room %s, y %.0f, floor %.0f)", leg, ROUTE[leg], legt, tostring(r), y, floor))
    if math.abs(y - floor) > 30 then fail(string.format("at pad %d Bond is at y %.0f, its floor is %.0f", ROUTE[leg], y, floor)) end
    leg = leg + 1; legt = 0
    if not ROUTE[leg] then
      test.walk_stop()
      if not rooms_seen[139] then fail("never in room 139") end
      if not rooms_seen[140] then fail("never in room 140") end
      print("DPCOMMS RESULT " .. (fails == 0 and "pass" or "fail"))
      phase = "done"
    end
  elseif legt > LEG_SECS then
    fail(string.format("leg %d to pad %d timed out at %.0f %.0f (room %s, %.0f left)", leg, ROUTE[leg], x, z, tostring(r), left or -1))
    print("DPCOMMS RESULT fail")
    phase = "done"
  end
end)
