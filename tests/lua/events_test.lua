-- Test-only: logs every Job 1 event. Run with GE_LUA_TEST=<this file> GE_EVENT_TEST=1 (see modding/lua.md).
local function fmt(...)
  local t = table.pack(...)
  for i = 1, t.n do t[i] = tostring(t[i]) end
  return table.concat(t, " ")
end
for _, ev in ipairs({ "damage", "chr_spawned", "item_pickup", "objective_changed", "chr_died", "stage_start", "bond_died" }) do
  on(ev, function(...) print("EVT " .. ev .. " " .. fmt(...)) end)
end
