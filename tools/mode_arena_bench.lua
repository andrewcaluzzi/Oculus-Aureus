-- Colosseum arena benchmark (GE_MODE=arena-bench, GE_STAGE=colosseum, GE_CHEATS=invuln, GE_COLOSSEUM_SLOTS=1400; the "Arena Benchmark.bat"
-- launcher sets them). Shipped to Data/Scripts/modes/arena-bench.lua by the build.
--
-- Five phases of 30 s steady state each, then the game quits by itself and the report is Data/Logs/arena_bench.txt:
--   A  crowd 100,  0 corpses            B  crowd 100,  250 corpses        C  crowd 300,  250 corpses
--   D  crowd 1000, 250 corpses          E  crowd 1000, 250 corpses + 18 live enemies shooting at Bond
-- Corpses are real: enemies placed on distinct pit pads and killed through the game's damage path (spawn.kill), kept for the whole run
-- (corpse_keep = -1). Bond stands in the middle of the pit and turns at a constant rate; each phase looks at the stands (pitch up) for two
-- half turns and at the floor (pitch down) for two, tagged in the report as "stands" and "floor". No waves run.
-- The settle time after each spawn burst is not measured.
--
-- Live variant (arena-bench-live.lua sets BENCH_LIVE and includes this file): no crowd, no corpses, phases L0 (baseline, nobody) then
-- 25 / 50 / 100 / 200 live soldiers (the arena "chase" behaviour: they walk, path and shoot at the invulnerable Bond, who never fires).
-- The live count is topped up all through each phase. The report adds how many were attacking / moving, and a sim cost per live enemy
-- ((sim avg - the L0 sim avg) / live).
local LIVE = BENCH_LIVE and true or false
ARENA_SETTINGS = {
  start_delay = 100000,      -- no waves: this mode only spawns what the phases ask for
  supplies = false, ammo_every = 0, pickup_mode = "none",
  crowd = LIVE and 0 or 100,
  corpse_keep = LIVE and 4 or -1, corpse_limit = LIVE and 40 or 100000,
}
include("arena")

local MEASURE_S  = 30        -- seconds measured per phase
local SEGMENT_S  = 7.5       -- seconds per view segment (24 deg/s = a half turn)
local TURN_RATE  = 24        -- degrees a second
local PITCH_STANDS, PITCH_FLOOR = 16, -40

local PH = {
  { name = "A", crowd = 100,  corpses = 0,   live = 0,  settle = 4 },
  { name = "B", crowd = 100,  corpses = 250, live = 0,  settle = 4 },
  { name = "C", crowd = 300,  corpses = 250, live = 0,  settle = 5 },
  { name = "D", crowd = 1000, corpses = 250, live = 0,  settle = 8 },
  { name = "E", crowd = 1000, corpses = 250, live = 18, settle = 5 },
}
if LIVE then
  PH = {
    { name = "L0",   crowd = 0, corpses = 0, live = 0,   settle = 4 },
    { name = "L25",  crowd = 0, corpses = 0, live = 25,  settle = 8 },
    { name = "L50",  crowd = 0, corpses = 0, live = 50,  settle = 8 },
    { name = "L100", crowd = 0, corpses = 0, live = 100, settle = 10 },
    { name = "L200", crowd = 0, corpses = 0, live = 200, settle = 12 },
  }
end

local bt, pi, st, st_t = 0, 0, "start", 0          -- bench clock, phase index, step, seconds in the step
local corpses_made = 0
local eng_att, eng_mov, eng_oth, eng_n, eng_acc = 0, 0, 0, 0, 0   -- live enemies by action, summed over the samples of a phase
local base_sim                                      -- sim ms avg of the empty phase (live variant)
local seg_t, seg_i = 0, 0
local pools_acc = 0
local done = false
local summary = {}

local function note(s) print("ge: bench: " .. s); bench.note(s) end

local function middle_pad()
  local best, bd
  for i = 0, pads.count() - 1 do
    local x, y, z = pads.pos(i)
    if x and y <= 1 then
      local d = x * x + z * z
      if not bd or d < bd then best, bd = i, d end
    end
  end
  return best
end

local function say(text) if ARENA_HOOKS and ARENA_HOOKS.say then ARENA_HOOKS.say(text, 600) end end

local function start_phase(n)
  pi, st, st_t = n, "prep", 0   -- (corpses_made and live_made are totals: a phase only adds what the last did not have)
  local p = PH[n]
  ARENA_HOOKS.crowd_set(p.crowd)
  say(string.format("BENCHMARK %s/%d: CROWD %d  CORPSES %d  LIVE %d", p.name, #PH, p.crowd, p.corpses, p.live))
  print(string.format("ge: bench: phase %s: preparing (crowd %d, corpses %d, live %d)", p.name, p.crowd, p.corpses, p.live))
  bench.sweep(TURN_RATE, PITCH_FLOOR)
end

local function counts_text()
  local c, k, a, f, r = ARENA_HOOKS.counts()
  local lo = spawn.pools()
  return c, k, a, f, string.format("spectators %d, corpses %d, live enemies %d, crowd spawns lost %d (seats swapped %d); loose weapon drops %d", c, k, a, f, r, lo or 0)
end

local function end_phase()
  local p = PH[pi]
  local c, k, a, f, txt = counts_text()
  local eng = ""
  if p.live > 0 and eng_n > 0 then
    eng = string.format(" | engaged (avg of %d samples, %.0f live): attacking %.1f, moving %.1f, other %.1f", eng_n, a, eng_att / eng_n, eng_mov / eng_n, eng_oth / eng_n)
  end
  bench.finish(string.format("Phase %s: crowd %d, corpses %d, live enemies %d (%.0f s measured)", p.name, p.crowd, p.corpses, p.live, MEASURE_S),
    txt .. eng .. string.format(" | view: %d segments of %.1f s at %d deg/s (stands pitch %d, floor pitch %d)", math.floor(MEASURE_S / SEGMENT_S), SEGMENT_S, TURN_RATE, PITCH_STANDS, PITCH_FLOOR))
  print(string.format("ge: bench: phase %s done: %s%s", p.name, txt, eng))
  local line = string.format("%s: spectators %d, corpses %d, live %d", p.name, c, k, a)
  if LIVE then
    local sim = bench.last()
    if p.live == 0 then
      base_sim = sim
      line = line .. string.format(", sim %.2f ms (baseline)", sim)
    elseif base_sim then
      local per = (sim - base_sim) * 1000 / p.live
      local txt2 = string.format("per live enemy: sim %.1f us  (= (%.2f - %.2f ms) / %d live)", per, sim, base_sim, p.live)
      bench.note("  " .. txt2)
      print("ge: bench: phase " .. p.name .. " " .. txt2)
      line = line .. string.format(", sim %.2f ms, %.1f us per live enemy", sim, per)
    end
  end
  summary[#summary + 1] = line
end

-- keep p.live soldiers fighting: top up what is missing (spawn.free() stops it before the pool runs out)
local function top_up(p)
  local _, _, a = ARENA_HOOKS.counts()
  if p.live > 0 and a < p.live then ARENA_HOOKS.pit_foes(math.min(10, p.live - a), nil, "chase") end
end

on("tick", function(dt)
  local ok, err = pcall(function()
    if done then return end
    bt = bt + dt
    st_t = st_t + dt
    if st == "start" then
      if bt < 1 then return end   -- let the stage and the arena's own start-up settle
      bench.run_start()
      note((LIVE and "preset arena-bench-live: " or "preset arena-bench: ") .. #PH .. " phases x " .. MEASURE_S .. " s, view sweep " .. TURN_RATE .. " deg/s")
      local pad = middle_pad()
      if pad then bond.warp(0, pad) end
      start_phase(1)
      return
    end
    local p = PH[pi]
    local c, k, a, f = ARENA_HOOKS.counts()   -- spectators placed, corpses kept, live enemies, spectators lost
    if st == "prep" then
      -- spawn the corpses (killed a second after they appear) and the live fighters in small bursts
      if corpses_made < p.corpses then
        corpses_made = corpses_made + ARENA_HOOKS.pit_foes(math.min(20, p.corpses - corpses_made), 1.0, "hold")
      end
      top_up(p)
      local crowd_ok = c + f >= p.crowd
      local made_ok = corpses_made >= p.corpses and a >= p.live
      -- corpses = the dead ones kept; live fighters are counted in `a`
      local dead_ok = k >= p.corpses
      if (crowd_ok and made_ok and dead_ok and st_t >= 2) or st_t > 90 then
        if st_t > 90 then note(string.format("phase %s: gave up waiting (spectators %d of %d, corpses %d of %d)", p.name, c, p.crowd, k, p.corpses)) end
        st, st_t = "settle", 0
      end
    elseif st == "settle" then
      if st_t >= p.settle then
        st, st_t = "measure", 0
        seg_t, seg_i = 0, 0
        eng_att, eng_mov, eng_oth, eng_n, eng_acc = 0, 0, 0, 0, 0
        bench.begin()
        bench.tag(1)
        bench.sweep(TURN_RATE, PITCH_STANDS)
        print(string.format("ge: bench: phase %s: measuring (view: stands)", p.name))
      end
    elseif st == "measure" then
      seg_t = seg_t + dt
      pools_acc = pools_acc + dt
      if pools_acc >= 1 then pools_acc = 0; bench.pools() end   -- keeps the hit-entry high-water mark
      top_up(p)
      if p.live > 0 then
        eng_acc = eng_acc + dt
        if eng_acc >= 2 then   -- how many are actually fighting
          eng_acc = 0
          local at, mv, ot = ARENA_HOOKS.acts()
          eng_att, eng_mov, eng_oth, eng_n = eng_att + at, eng_mov + mv, eng_oth + ot, eng_n + 1
          print(string.format("ge: bench: phase %s: live %d: attacking %d, moving %d, other %d", p.name, at + mv + ot, at, mv, ot))
        end
      end
      if seg_t >= SEGMENT_S and st_t < MEASURE_S then
        seg_t, seg_i = 0, seg_i + 1
        local stands = (seg_i % 2 == 0)
        bench.tag(stands and 1 or 0)
        bench.sweep(TURN_RATE, stands and PITCH_STANDS or PITCH_FLOOR)
        print(string.format("ge: bench: phase %s: view %s", p.name, stands and "stands" or "floor"))
      end
      if st_t >= MEASURE_S then
        end_phase()
        if pi < #PH then
          start_phase(pi + 1)
        else
          done = true
          bench.sweep_off()
          bench.note("")
          bench.note("== Summary ==")
          for _, s in ipairs(summary) do bench.note("  " .. s) end
          bench.note("Report written to Data/Logs/arena_bench.txt - paste it back. (The 'ge: sys:' lines above describe this machine.)")
          st = "end"; st_t = 0
        end
      end
    end
  end)
  if not ok then
    print("ge: bench: ERROR " .. tostring(err))
    pcall(bench.note, "ERROR: " .. tostring(err))
    done = true
    st = "end"
    st_t = 0
  end
end)

-- quit a moment after the last phase (the tick handler above stops at `done`, so a second handler waits)
local quit_t = 0
on("tick", function(dt)
  if st == "end" then
    quit_t = quit_t + dt
    if quit_t >= 1 then bench.exit() end
  end
end)
