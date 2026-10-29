-- Probe of Surface (GE_STAGE=surface) for the sniper duel: Bond's start, every pad (position, waypoint or not), the extents. Throwaway
-- exploration (tests/sniper/probe.sh); the real picks are tools/sniper_defs.lua and the rules in tools/mode_sniper.lua.
on("stage_start", function()
  local n = pads.count()
  local bx, by, bz = bond.pos(0)
  print(string.format("SPROBE stage start: %d pads (base %d), bond at (%.0f, %.0f, %.0f)", n, pads.base(), bx or -1, by or -1, bz or -1))
  local wp = {}
  for _, id in ipairs(pads.far_from(0, 0, 0, 1e9, true)) do wp[id] = true end
  local c = 0
  for _ in pairs(wp) do c = c + 1 end
  print(string.format("SPROBE waypoint pads %d", c))
  for i = 0, n - 1 do
    local x, y, z = pads.pos(i)
    if x then
      local dx, dz = bx and (x - bx) or 0, bz and (z - bz) or 0
      print(string.format("SPROBE pad %d %.0f %.0f %.0f wp %d dist %.0f", i, x, y, z, wp[i] and 1 or 0, math.sqrt(dx * dx + dz * dz)))
    end
  end
  print("SPROBE end")
end)
