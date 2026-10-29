-- Probe of a stock MP map loaded with its MP setup and the one solo Bond (GE_STAGE=mptemple ...): the pads, the rooms, where Bond
-- starts, the waypoint graph. Throwaway exploration (tests/arena_maps/probe.sh); the real work is tools/arena_maps.lua.
on("stage_start", function()
  local base = pads.base()
  print(string.format("PROBE stage start: %d pads (the map's own: %d)", pads.count(), base))
  local bx, by, bz = bond.pos(0)
  print(string.format("PROBE bond at (%.0f, %.0f, %.0f)", bx or -1, by or -1, bz or -1))
  local n, minx, maxx, minz, maxz, miny, maxy = 0, 1e9, -1e9, 1e9, -1e9, 1e9, -1e9
  local own, node = {}, {}
  for i = 0, pads.count() - 1 do
    local x, y, z = pads.pos(i)
    if x then
      n = n + 1
      minx, maxx, minz, maxz, miny, maxy = math.min(minx, x), math.max(maxx, x), math.min(minz, z), math.max(maxz, z), math.min(miny, y), math.max(maxy, y)
      local band = math.floor(y / 100) * 100
      local t = (i < base) and own or node
      t[band] = (t[band] or 0) + 1
    end
  end
  local wp = pads.far_from(bx or 0, bz or 0, 0, 99999, true)
  print(string.format("PROBE pads with pos %d, x %.0f..%.0f z %.0f..%.0f y %.0f..%.0f, waypoint pads %d", n, minx, maxx, minz, maxz, miny, maxy, #wp))
  for _, name in ipairs({ "own", "node" }) do
    local t = (name == "own") and own or node
    local keys = {}
    for k in pairs(t) do keys[#keys + 1] = k end
    table.sort(keys)
    local parts = {}
    for _, k in ipairs(keys) do parts[#parts + 1] = string.format("%d:%d", k, t[k]) end
    print("PROBE y bands (" .. name .. " pads): " .. table.concat(parts, " "))
  end
  if probe and probe.pads and os.getenv == nil then pcall(probe.pads) end
end)
on("tick", function(dt) end)
