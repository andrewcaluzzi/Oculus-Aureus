-- Dam+ concept 3 walk-through (GE_MODE=dpisland, GE_DAMPLUS=island, GE_ROOM_AUDIT=1): on the island, Bond walks on the real
-- stick from the island quay (stock pad 80) up the new stair (room 146) to the hut's west balcony, round to its north side, back,
-- in through the upper floor's west door (the stock threshold) to the sniper rifle by the ladder hole, and all the way down again.
-- Checks: every leg arrives in time, room 146 is entered and the upper floor is reached (room 81 at y -214), Bond's height at each
-- Dam+ stop is the floor there (y -428 mid-stair, -214 up top). Prints DPISLE lines and "DPISLE RESULT pass|fail".
-- Pads (island alone): design 367-375 (369 inside the upper floor, 370 the rifle, 371 the landing, 372 mid-stair, 373 the
-- balcony's north side, 374 the balcony by the door, 375 just inside it); stock 80 (the island quay).
local OUT = { 80, 372, 371, 373, 374, 375, 369, 370 }
local ROUTE = {}
for _, p in ipairs(OUT) do ROUTE[#ROUTE + 1] = p end
for i = #OUT - 1, 1, -1 do ROUTE[#ROUTE + 1] = OUT[i] end
local LEG_SECS = 20
local t, leg, legt, phase = 0, 1, 0, "start"
local fails, lastroom, top = 0, nil, false
local rooms_seen = {}
local function fail(msg) fails = fails + 1; print("DPISLE FAIL " .. msg) end

on("tick", function(dt)
  t = t + dt
  if phase == "start" then
    if t < 2 then return end
    bond.warp(0, 80)
    phase = "walk"; legt = 0
    return
  end
  if phase ~= "walk" then return end
  local x, y, z = bond.pos(0)
  if not x then return end
  if y < -680 or y > -170 then fail(string.format("height %.0f out of the floors' range at %.0f %.0f", y, x, z)) end
  local r = bond.room(0)
  if r == 81 and y > -240 then top = true end
  if r ~= lastroom then
    print(string.format("DPISLE room %s -> %s at %.0f %.0f %.0f (t %.1f) drawn %s", tostring(lastroom), tostring(r), x, y, z, t,
      table.concat(test.rooms_drawn(0) or {}, ",")))
    lastroom = r
    rooms_seen[r or -1] = true
  end
  local tx, ty, tz = pads.pos(ROUTE[leg])
  local left = test.walk(0, tx, tz)
  legt = legt + dt
  if left and left < 45 then
    local floor = ty - 51
    print(string.format("DPISLE leg %d reached pad %d in %.1f s (room %s, y %.0f, floor %.0f)", leg, ROUTE[leg], legt, tostring(r), y, floor))
    if ROUTE[leg] >= 367 and math.abs(y - floor) > 30 then fail(string.format("at pad %d Bond is at y %.0f, its floor is %.0f", ROUTE[leg], y, floor)) end
    leg = leg + 1; legt = 0
    if not ROUTE[leg] then
      test.walk_stop()
      if not rooms_seen[146] then fail("never on the stair (room 146)") end
      if not top then fail("never on the upper floor") end
      print("DPISLE RESULT " .. (fails == 0 and "pass" or "fail"))
      phase = "done"
    end
  elseif legt > LEG_SECS then
    fail(string.format("leg %d to pad %d timed out at %.0f %.0f (room %s, %.0f left)", leg, ROUTE[leg], x, z, tostring(r), left or -1))
    print("DPISLE RESULT fail")
    phase = "done"
  end
end)
