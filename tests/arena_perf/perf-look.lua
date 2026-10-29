-- Look check for the arena render fixes (GE_MODE=perf-look, GE_STAGE=colosseum, GE_CHEATS=invuln; run-look.sh): the same views
-- with the chr batch and the far-spectator impostors (crowd.lod) on and off, a bench.shot() each, then the game quits.
--   view N (north wall, looking up at the stands): batch+lod, batch only, lod only, neither; then the crowd cheers, batch+lod again
--   view C (pit centre, looking down at 30 corpses): batch on, batch off
-- The log names each shot in order ("ge: look: shot <n> <name>"); the shot files are shot_<frame>.bmp in the same order.
ARENA_SETTINGS = { start_delay = 100000, supplies = false, ammo_every = 0, pickup_mode = "none", corpse_keep = -1, corpse_limit = 100000 }
include("arena")

local function pad_at(x, z)
  local best, bd = nil, 1e18
  for i = 0, pads.count() - 1 do
    local px, py, pz = pads.pos(i)
    if px and py < 1 then
      local d = (px - x) ^ 2 + (pz - z) ^ 2
      if d < bd then best, bd = i, d end
    end
  end
  return best
end

local LOD = 2500
local SKINS = { "russian_infantry", "janus_marine", "st_petersburg_guard", "naval_officer", "terrorist", "biker" }
local victims = {}
-- 16 bodies on the pit pads within 1200 of the middle (not the middle one), killed a second after they appear
local function ring_of_bodies()
  local n = 0
  for i = 0, pads.count() - 1 do
    local x, y, z = pads.pos(i)
    if x and y < 1 then
      local d = math.sqrt(x * x + z * z)
      if d > 300 and d < 1200 and n < 16 then
        local c = spawn.chr(i, { skin = SKINS[n % #SKINS + 1], weapon = "none", behaviour = "hold" })
        if c then n = n + 1; victims[#victims + 1] = c end
      end
    end
  end
  print("ge: look: bodies placed " .. n)
end
local function kill_all() for _, c in ipairs(victims) do spawn.kill(c) end end
local steps = {
  { at = 1,  fn = function() bond.warp(0, pad_at(0, -3600)); ARENA_HOOKS.pit_foes(30, 1.0, "hold"); ring_of_bodies() end },
  { at = 2.5, fn = kill_all },
  { at = 14, fn = function() bond.warp(0, pad_at(0, 3600)); bench.look(0, 0, 24) end },
  { at = 16, cfg = { true, LOD },  shot = "N batch+lod" },
  { at = 18, cfg = { false, LOD }, shot = "N lod only" },
  { at = 20, cfg = { true, 0 },    shot = "N batch only" },
  { at = 22, cfg = { false, 0 },   shot = "N neither (as shipped)" },
  { at = 23, fn = function() crowd.batch(true); crowd.lod(LOD); ARENA_HOOKS.react("wave_clear") end },
  { at = 24.5, shot = "N cheer batch+lod" },
  { at = 26, fn = function() bond.warp(0, pad_at(0, -2600)); bench.look(0, 0, -14) end },
  { at = 29, cfg = { true, LOD, true },   shot = "C ring of bodies, batch + bake (baked from here)" },
  { at = 31, cfg = { false, LOD, false }, shot = "C ring of bodies, no batch, no bake" },
  { at = 33, cfg = { true, LOD, false },  shot = "C ring of bodies, batch, no bake" },
  -- another place and angle (the bake stays on, so the bodies are drawn from the captures made at C)
  { at = 34, fn = function() crowd.bake(true); bond.warp(0, pad_at(0, 2600)); bench.look(0, 180, -14) end },
  { at = 36.5, shot = "D bodies from the bakes made at C" },
  { at = 38, cfg = { false, LOD, false }, shot = "D bodies, no batch, no bake" },
  { at = 39, fn = function() ARENA_HOOKS.pit_foes(12, nil, "chase") end },
  { at = 44, cfg = { true, LOD, true },   shot = "L live + corpses, all on" },
  { at = 45.5, cfg = { false, LOD, false }, shot = "L live + corpses, all off" },
  { at = 47, fn = function() bench.exit() end },
}
local t, si, nshot = 0, 1, 0
on("tick", function(dt)
  t = t + dt
  local s = steps[si]
  if not s then return end
  if s.cfg and not s.cfg_done and t >= s.at - 1 then   -- set the config a second before the shot
    s.cfg_done = true
    crowd.batch(s.cfg[1])
    crowd.lod(s.cfg[2])
    if s.cfg[3] ~= nil then crowd.bake(s.cfg[3]) end
  end
  if t < s.at then return end
  si = si + 1
  if s.fn then s.fn() end
  if s.shot then
    nshot = nshot + 1
    local c, k, a = ARENA_HOOKS.counts()
    print(string.format("ge: look: shot %d %s (batch %s, lod %d, bake %s; spectators %d, corpses %d, live %d)", nshot, s.shot,
      tostring(crowd.batch()), crowd.lod(), tostring(crowd.bake()), c, k, a))
    bench.shot()
  end
end)
