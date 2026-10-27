-- Dam+ concept 4 walk-through (GE_MODE=dpspill, GE_DAMPLUS=spill, GE_ROOM_AUDIT=1): Bond walks on the real stick from room 91
-- (the underground chamber under the bungee platform) through the doorway cut into its south wall, down the stair inside the
-- dam (144), through the passage cut in the downstream face and along the balcony (145), and back. Checks: every leg arrives in
-- time, rooms 144, 145 and 91 are entered, Bond's height at each Dam+ stop is the floor there (y -642, -1032 mid-stair, -1422
-- on the balcony). Prints DPSPILL lines and "DPSPILL RESULT pass|fail". Pads (spill alone): 367-372 its waypoints; stock 198
-- (room 91).
local OUT = { 198, 367, 368, 369, 370, 371, 372 }
local ROUTE = {}
for _, p in ipairs(OUT) do ROUTE[#ROUTE + 1] = p end
for i = #OUT - 1, 1, -1 do ROUTE[#ROUTE + 1] = OUT[i] end
local LEG_SECS = 25
local t, leg, legt, phase = 0, 1, 0, "start"
local fails, lastroom = 0, nil
local rooms_seen = {}
local function fail(msg) fails = fails + 1; print("DPSPILL FAIL " .. msg) end

on("tick", function(dt)
  t = t + dt
  if phase == "start" then
    if t < 2 then return end
    bond.warp(0, 198)
    phase = "walk"; legt = 0
    return
  end
  if phase ~= "walk" then return end
  local x, y, z = bond.pos(0)
  if not x then return end
  if y < -1460 or y > -600 then fail(string.format("height %.0f out of the floors' range at %.0f %.0f", y, x, z)) end
  local r = bond.room(0)
  if r ~= lastroom then
    print(string.format("DPSPILL room %s -> %s at %.0f %.0f %.0f (t %.1f) drawn %s", tostring(lastroom), tostring(r), x, y, z, t,
      table.concat(test.rooms_drawn(0) or {}, ",")))
    lastroom = r
    rooms_seen[r or -1] = true
  end
  local tx, ty, tz = pads.pos(ROUTE[leg])
  local left = test.walk(0, tx, tz)
  legt = legt + dt
  if left and left < 45 then
    local floor = ty - 51
    print(string.format("DPSPILL leg %d reached pad %d in %.1f s (room %s, y %.0f, floor %.0f)", leg, ROUTE[leg], legt, tostring(r), y, floor))
    if ROUTE[leg] >= 367 and math.abs(y - floor) > 30 then fail(string.format("at pad %d Bond is at y %.0f, its floor is %.0f", ROUTE[leg], y, floor)) end
    leg = leg + 1; legt = 0
    if not ROUTE[leg] then
      test.walk_stop()
      for _, rr in ipairs({ 144, 145, 91 }) do if not rooms_seen[rr] then fail("never in room " .. rr) end end
      print("DPSPILL RESULT " .. (fails == 0 and "pass" or "fail"))
      phase = "done"
    end
  elseif legt > LEG_SECS then
    fail(string.format("leg %d to pad %d timed out at %.0f %.0f (room %s, %.0f left)", leg, ROUTE[leg], x, z, tostring(r), left or -1))
    print("DPSPILL RESULT fail")
    phase = "done"
  end
end)
