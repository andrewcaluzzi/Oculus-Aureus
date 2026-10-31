-- Dam+ after the alarm (GE_LUA_TEST, not a mode, so the level's guards, objectives and AI are all there; run by
-- tests/damplus/alarm_watch.sh, which judges the result). Uses only calls that work outside a mode (spawn.room, test.heard,
-- bond.room, test.press, bench.exit).
--   * the alarm reinforcements: spawned chrs get chrnums from 5000 up (chr.c player1_guardID), so a chrnum 5000+ with a prop is
--     a guard that really appeared: each one is logged the tick it is first seen ("reinforcement chr N appeared");
--   * the motor-pool guards (chrs 120-124, rooms 137/138): every room change, and whether they heard Bond's burst (test.press
--     "z", the real input path, at FIRE s; Bond stands at the start in room 135, beside the shutter).
-- The script's clock starts later than the stage's, so the shell orders events by the log, not by these times.
local MP = { 120, 121, 122, 123, 124 }
local FIRE = 8
local ENDT = 240
local t, fired, done = 0, 0, false
local room, heard = {}, {}
local seen = {}

on("tick", function(dt)
  t = t + dt
  if done then return end
  if t >= FIRE and fired < 12 and t >= FIRE + fired * 0.5 then
    test.press("z")
    fired = fired + 1
    if fired == 1 then print(string.format("DPALARM t %.1f Bond in room %s fires", t, tostring(bond.room(0)))) end
  end
  for _, c in ipairs(MP) do
    local r = spawn.room(c)
    if r ~= room[c] then
      print(string.format("DPALARM t %.1f chr %d room %s -> %s", t, c, tostring(room[c]), tostring(r)))
      room[c] = r
    end
    if not heard[c] and test.heard(c) then
      heard[c] = true
      print(string.format("DPALARM t %.1f chr %d heard Bond (room %s)", t, c, tostring(r)))
    end
  end
  for n = 5000, 5099 do
    if not seen[n] then
      local r = spawn.room(n)
      if r ~= nil then
        seen[n] = true
        print(string.format("DPALARM t %.1f reinforcement chr %d appeared in room %d", t, n, r))
      end
    end
  end
  if t >= ENDT then
    for _, c in ipairs(MP) do
      print(string.format("DPALARM chr %d: last room %s, heard %s", c, tostring(room[c]), tostring(heard[c] or false)))
    end
    print("DPALARM done")
    done = true
    bench.exit()
  end
end)
