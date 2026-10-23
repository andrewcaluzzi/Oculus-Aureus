-- A5 test: path efficiency and first-shot range. One AK47 chaser from each listed gate; Bond (GE_CHEATS=invuln) is placed at PATH_BOND and,
-- if PATH_BOND2 is set, jumps there PATH_WARP_AT seconds after the wave starts (the chasers must follow).
-- Wrapper presets (arena-p-open/pillar/move.lua) set the globals, then include this file. Globals: PATH_BEH, PATH_GATES, PATH_WEAPON, PATH_BOND = {x, z}, PATH_BOND2 = {x, z}, PATH_WARP_AT, PATH_LEGACY_KNIFE
local beh = PATH_BEH or "chase"
local gates = PATH_GATES or { 1, 2, 3, 4 }
local bond1 = PATH_BOND or { 2000, 2400 }
ARENA_SETTINGS = { mode = "fixed", waves = 1, boss_every = 0, breather = 2, start_delay = 2, max_alive = 12, debug_clear_after = PATH_SECS or 40,
  debug_path = true, debug_track = false, crowd = 0, supplies = false,
  debug_queue = function(w)
    local q = {}
    for i, g in ipairs(gates) do
      q[#q + 1] = { n = i, gate = g, spec = { skin = "russian_soldier", weapon = PATH_WEAPON or "ak47", health = 50, accuracy = 20, speed = 70, behaviour = beh, knives = PATH_KNIVES, knife_range = ARENA_KNIFE_OLD and 900 or nil, knife_cooldown = ARENA_KNIFE_OLD and 2.5 or nil, _tier = "normal" } }
    end
    return q
  end }
include("arena")

local function nearest_pad(x, z)
  local best, bd = nil, 1e18
  for i = 0, pads.count() - 1 do
    local px, py, pz = pads.pos(i)
    if px and py < 1 and math.abs(px) < 4500 and math.abs(pz) < 4500 then
      local d = (px - x) ^ 2 + (pz - z) ^ 2
      if d < bd then best, bd = i, d end
    end
  end
  return best
end

local function put_bond(spot)
  local pad = nearest_pad(spot[1], spot[2])
  if pad then
    local px, _, pz = pads.pos(pad)
    local ok = bond.warp(0, pad, spot[1] - px + 37, spot[2] - pz + 83)   -- off the pad centre (a tile edge) by a bit
    print(string.format("ge: pathtest: Bond to pad %d for spot (%.0f,%.0f): %s", pad, spot[1], spot[2], tostring(ok)))
  end
end

local t, placed, warped, wave_t0 = 0, false, false, nil
on("tick", function(dt)
  t = t + dt
  if not placed and t > 0.5 then placed = true; put_bond(bond1) end
  if PATH_BOND2 and not warped then
    if not wave_t0 and t > 2.6 then wave_t0 = t end   -- the wave starts at start_delay (2 s)
    if wave_t0 and t - wave_t0 >= (PATH_WARP_AT or 3) then warped = true; put_bond(PATH_BOND2) end
  end
end)
