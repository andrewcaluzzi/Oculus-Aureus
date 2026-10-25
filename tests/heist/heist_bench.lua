-- Heist frame-time bench (designs/bank-heist.md P2.5): the real tools/mode_heist.lua (any map; written for the Janus bank), measured with
-- the arena benchmark's `bench` API (port_bench.c: per presented frame the frame interval, the game code's longest stretch = "sim", which
-- on PC includes building the display lists, the GL submission and the swap; the draw count; the pools). Run with GE_DIFF=0 (Agent, ~37
-- guards) and GE_DIFF=3 (007, 100+ guards and the waves): tests/heist/run_bench.sh. The report is Data/Logs/arena_bench.txt.
-- Phases (Bond invincible, turning at a constant rate so every direction is drawn):
--   hall    in the banking hall at the screen wall (the most rooms and chrs in view), quiet if the guards let him
--   vault   at the cracker zone a floor down (the vault level)
--   hot     the alarm raised: the lockdown, the waves and (after response_after) the response; measured in the hall once the waves are in
HEIST_SETTINGS = { test = true, response_after = 20 }
include("heist")
mods.set("invincible", true)

local function wait(sec)
  local t = 0
  while t < sec do t = t + coroutine.yield(); bond.heal(0, 1, 1) end
end
local function alive()
  local n = 0
  for _, g in ipairs(HEIST.guards) do if spawn.alive(g.chr) and not spawn.down(g.chr) then n = n + 1 end end
  return n
end
local function phase(label, secs)
  bench.begin()
  bench.tag(1)
  bench.sweep(24, 0)
  wait(secs)
  bench.sweep_off()
  bench.finish(string.format("%s (difficulty %d, %s)", label, mission.difficulty(), tostring(HEIST.map)),
               string.format("guards alive %d (placed %d), civilians %d, chr slots free %d, hot %s, phase %s", alive(), #HEIST.guards, #HEIST.civs,
                             spawn.free(), tostring(HEIST.hot), HEIST.phase))
  local s, g = bench.last()
  print(string.format("HEIST BENCH %s: sim avg %.2f ms, gl avg %.2f ms, guards alive %d, hot %s", label, s, g, alive(), tostring(HEIST.hot)))
end

local function body()
  while not HEIST.ready do wait(0.25) end
  wait(2)
  bench.run_start()
  local P = HEIST.M.P
  local best, bd = nil, 1e9
  for _, p in ipairs(pads.far_from(P.screens_at[1], P.screens_at[2], 0, 600)) do
    local x, _, z = pads.pos(p)
    local d = (x - P.screens_at[1]) ^ 2 + (z - P.screens_at[2]) ^ 2
    if d < bd then best, bd = p, d end
  end
  bond.warp(0, best)
  wait(2)
  phase("hall", 20)
  bond.warp(0, P.crack)
  wait(2)
  phase("vault", 15)
  bond.warp(0, best)
  alarm.start()
  wait(30)                                          -- the waves come in (first after wave_first, then every wave_interval)
  phase("hot", 30)
  print("HEIST BENCH DONE")
  bench.exit()
end

local co = coroutine.wrap(body)
local finished = false
on("tick", function(dt)
  if finished then return end
  local ok, err = pcall(co, dt)
  if not ok then finished = true; print("HEIST BENCH FAIL script error: " .. tostring(err)) end
end)
