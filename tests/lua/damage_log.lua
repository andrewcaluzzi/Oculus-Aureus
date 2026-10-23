-- Logs every damage / death event (GE_LUA_TEST=<this file>): "D victim attacker amount weapon", "DIED victim killer", "BONDDIED killer player".
-- Used to see that the guards of a mode really shoot (tests/net_lockstep.sh phase 42) and that both machines see the same shots.
-- One line per event with single spaces, so a test can grep and compare it.
local function L(...)
  local t = {}
  for i = 1, select("#", ...) do t[#t + 1] = tostring((select(i, ...))) end
  print(table.concat(t, " "))
end
on("damage", function(v, a, amt, w) L("D", v, a, string.format("%.3f", amt), w) end)
on("chr_died", function(v, k) L("DIED", v, k) end)
on("bond_died", function(k, p) L("BONDDIED", k, p) end)
