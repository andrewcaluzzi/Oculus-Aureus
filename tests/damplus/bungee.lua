-- Dam+ bungee exit (GE_LUA_TEST with GE_DAMPLUS=1, GE_DIFF=0, GE_SPAWNXYZ=-4,0,-3035,180: Bond starts on the bungee platform,
-- room 64 = the room of pad 330). ai_24 must set flag 0x1000, run the jump and the outro; at Agent that completes the mission.
-- Logs DPBUNGEE lines: objective events and Bond's room/height while he drops.
local t, last = 0, nil
on("objective_changed", function(i, st) print(string.format("DPBUNGEE objective %d -> %d (t %.1f)", i, st, t)) end)
on("tick", function(dt)
  t = t + dt
  local x, y, z = bond.pos(0)
  local r = bond.room(0)
  local tag = tostring(r) .. "/" .. (y and string.format("%d", math.floor(y / 1000)) or "-")
  if tag ~= last then
    print(string.format("DPBUNGEE t %.1f room %s y %s", t, tostring(r), y and string.format("%.0f", y) or "-"))
    last = tag
  end
end)
