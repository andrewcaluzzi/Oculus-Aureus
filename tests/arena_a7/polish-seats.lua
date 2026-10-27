-- A7: spectator seating. SEAT_GAP (global, set by the wrapper; 0 = the A6 behaviour) and SEAT_CROWD. Counts pairs of spectators standing
-- closer than 100 units (x/z) after they are placed, from their real positions.
ARENA_SETTINGS = { mode = "endless", crowd = SEAT_CROWD or 100, start_delay = 999, ammo_every = 0, supplies = false, pickup_markers = false,
  seat_gap = SEAT_GAP, debug_seats = true }
include("arena")
local t, done = 0, false
on("tick", function(dt)
  t = t + dt
  if not done and t > 3 then
    done = true
    local P = {}
    for id = 8000, 8000 + 400 do
      local x, z = spawn.where(id)
      if x then P[#P + 1] = { x, z } end
    end
    local close, close240, closest = 0, 0, 1e9
    for i = 1, #P do for j = i + 1, #P do
      local d = math.sqrt((P[i][1] - P[j][1]) ^ 2 + (P[i][2] - P[j][2]) ^ 2)
      if d < 100 then close = close + 1 end
      if d < 240 then close240 = close240 + 1 end
      if d < closest then closest = d end
    end end
    print(string.format("ge: seattest: %d spectators placed, %d pairs closer than 100 units, %d pairs closer than 240, closest pair %.0f, duplicate seats = %d", #P, close, close240, closest, close))
  end
end)
