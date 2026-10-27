-- Dam+ check (GE_LUA_TEST): where the Night Shift guards are over time (patrols must change rooms). Stock guards stay (not a mode).
local IDS = { 100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111, 112 }
local t, nextdump = 0, 0
local function dump(tag)
  local s = {}
  for _, c in ipairs(IDS) do s[#s + 1] = c .. ":" .. tostring(spawn.room(c)) end
  print("dpcheck " .. tag .. " " .. table.concat(s, " "))
end
on("stage_start", function() dump("start") end)
on("tick", function(dt)
  t = t + dt
  if t >= nextdump then dump(string.format("t%03d", math.floor(t))); nextdump = nextdump + 15 end
  if t > 181 then print("dpcheck done") end
end)
