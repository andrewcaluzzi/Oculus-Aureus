-- Dam+ motor pool (GE_LUA_TEST beside the player bot, tests/damplus/motor_alert.sh): the rooms of the motor-pool guards
-- (chrs 120-124; rooms 137 bay, 138 barracks) and Bond, every change, using only calls that work outside a mode. Bond goes in,
-- is seen, and runs out down the canyon; PASS when at least one motor-pool guard reaches room 135 (out through the shutter) or
-- beyond. Prints DPMOTOR lines and "DPMOTOR RESULT pass|fail" (fail at 300 s).
local MP = { 120, 121, 122, 123, 124 }
local t, done = 0, false
local room, broom = {}, nil
local out = {}

on("tick", function(dt)
  t = t + dt
  if done then return end
  local b = bond.room(0)
  if b ~= broom then
    print(string.format("DPMOTOR t %.1f Bond room %s -> %s", t, tostring(broom), tostring(b)))
    broom = b
  end
  local nout = 0
  for _, c in ipairs(MP) do
    local r = spawn.room(c)
    if r ~= room[c] then
      print(string.format("DPMOTOR t %.1f chr %d room %s -> %s", t, c, tostring(room[c]), tostring(r)))
      room[c] = r
      if r ~= nil and r ~= 137 and r ~= 138 then out[c] = true end
    end
    if out[c] then nout = nout + 1 end
  end
  if nout >= 1 or t > 300 then
    print(string.format("DPMOTOR %d motor-pool guard(s) came out of rooms 137/138 by t %.1f", nout, t))
    print("DPMOTOR RESULT " .. (nout >= 1 and "pass" or "fail"))
    done = true
    bench.exit()
  end
end)
