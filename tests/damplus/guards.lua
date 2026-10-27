-- Dam+ guards and key (GE_LUA_TEST with GE_DAMPLUS=1 GE_CHEATS=invuln; not a mode, so every guard is there).
-- 1. the shutter refuses Bond; 2. kill chr 6 (the truck guard), walk onto his key: Bond holds 0x80 and the shutter opens;
-- 3. the motor-pool patrol (chr 122) moves between rooms 137 and 138; 4. a shot in the bay: the motor-pool guards react and
-- come at Bond (rooms over time). Prints DPGUARD lines and "DPGUARD RESULT pass|fail".
local MP = { 120, 121, 122, 123, 124 }
local t, step, fails = 0, 0, 0
local patrol_rooms = {}
local function fail(m) fails = fails + 1; print("DPGUARD FAIL " .. m) end
local function rooms(tag)
  local s = {}
  for _, c in ipairs(MP) do s[#s + 1] = c .. ":" .. tostring(spawn.room(c)) end
  print("DPGUARD " .. tag .. " " .. table.concat(s, " "))
end

on("tick", function(dt)
  t = t + dt
  local r122 = spawn.room(122)
  if r122 then patrol_rooms[r122] = true end
  if step == 0 and t > 2 then
    rooms("start")
    if door.try(0, 281) then fail("the shutter opened without the key") end
    print("DPGUARD kill chr 6: " .. tostring(spawn.kill(6)))
    step = 1
  elseif step == 1 and t > 5 then
    bond.warp(0, 10)
    step = 2
  elseif step == 2 and t > 8 then
    local keys = bond.keys(0) or 0
    print(string.format("DPGUARD Bond keys 0x%x", keys))
    if (keys & 0x80) == 0 then fail("no shutter key after walking onto chr 6's drop") end
    bond.warp(0, 321)
    step = 3
  elseif step == 3 and t > 9 then
    local ok = door.try(0, 281)
    print("DPGUARD shutter with the key: " .. tostring(ok))
    if not ok then fail("the shutter did not open with the key") end
    step = 4
  elseif step == 4 and t > 40 then
    rooms("t40")
    if not (patrol_rooms[137] and patrol_rooms[138]) then fail("the motor-pool patrol did not visit both rooms") end
    bond.warp(0, 373)
    test.shot_noise(0, 1.0)
    step = 5
  elseif step == 5 and math.floor(t) % 5 == 0 and t < 70 and t - math.floor(t) < 0.26 then
    rooms(string.format("t%02d", math.floor(t)))
  elseif step == 5 and t > 70 then
    local near = 0
    local bx, _, bz = bond.pos(0)
    for _, c in ipairs(MP) do
      local x, z = spawn.where(c)
      if spawn.room(c) == 137 then near = near + 1 end
    end
    print("DPGUARD guards in the bay after the shot: " .. near)
    print("DPGUARD RESULT " .. (fails == 0 and "pass" or "fail"))
    step = 6
  end
end)
