-- Dam+ load check (GE_LUA_TEST, GE_DAMPLUS=1, GE_INVIS=1, not a mode, so stock and Dam+ guards are all there): every wing guard
-- spawned in its room, and each wing's patrol leaves its first room within two minutes. Prints DPGUARDS lines and
-- "DPGUARDS RESULT pass|fail".
local EXPECT = {
  -- motor pool (slice 2)
  [120] = { 137 }, [121] = { 137 }, [122] = { 137 }, [123] = { 138 }, [124] = { 138 },
  -- comms annex (concept 5): operator, radio-room guard, deck sentry, tunnel guard, the patrol
  [130] = { 139 }, [131] = { 139 }, [132] = { 140 }, [133] = { 139 }, [134] = { 139 },
  -- west gallery (concept 2): by the hatch, the hall walkway's ends, the vestibule, the patrol
  [140] = { 143 }, [141] = { 142, 143 }, [142] = { 142 }, [143] = { 141 }, [144] = { 141 },
}
local PATROLS = { [122] = {}, [134] = {}, [144] = {} }
local t, fails, checked, done = 0, 0, false, false
local function fail(m) fails = fails + 1; print("DPGUARDS FAIL " .. m) end
on("tick", function(dt)
  t = t + dt
  if not checked and t > 3 then
    checked = true
    for c, rooms in pairs(EXPECT) do
      local r = spawn.room(c)
      local ok = false
      for _, want in ipairs(rooms) do if r == want then ok = true end end
      print(string.format("DPGUARDS chr %d in room %s", c, tostring(r)))
      if not ok then fail(string.format("chr %d in room %s, want %s", c, tostring(r), table.concat(rooms, "/"))) end
    end
  end
  for c, seen in pairs(PATROLS) do
    local r = spawn.room(c)
    if r and not seen[r] then seen[r] = true; print(string.format("DPGUARDS patrol %d enters room %d at %.0f s", c, r, t)) end
  end
  if not done and t > 120 then
    done = true
    for c, seen in pairs(PATROLS) do
      local n = 0
      for _ in pairs(seen) do n = n + 1 end
      if n < 2 then fail(string.format("patrol %d stayed in one room", c)) end
    end
    print("DPGUARDS RESULT " .. (fails == 0 and "pass" or "fail"))
  end
end)
