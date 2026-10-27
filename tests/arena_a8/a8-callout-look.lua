-- The arena HUD's look (port_arenahud.c): the wave panel with a mutator tag and the crowd bar, and a centre callout. Wave 1 rolls a
-- mutator (cycle: KNIVES ONLY first); six "hold" soldiers; at t=6 four of them die inside the chain window (a QUAD FURY callout,
-- level 3), and test.screenshot() captures 0.2 s later; at t=9 the last two die one by one, at t=12 a second shot (the panel only).
-- Run: tests/arena_a8/run.sh a8-callout-look a8-callout-look 900 (cwd for the shots: build_tests/a8/shots/a8-callout-look)
local function soldier() return { skin = "russian_soldier", weapon = "kf7", behaviour = "hold", health = 0.05, accuracy = 0, _tier = "normal" } end
ARENA_SETTINGS = { mode = "fixed", waves = 20, boss_every = 0, breather = 30, start_delay = 1, first_wave = 1, ammo_every = 0, armour_chance = 0,
  mutators = true, mutator_from = 1, mutator_chance = 1, mutator_chance_max = 1, mutator_pick = "cycle",
  crowd = { count = 40 },
  debug_queue = function(w)
    if w ~= 1 then return {} end
    local q = {}
    for i = 1, 6 do q[#q + 1] = { n = i, gate = 1 + (i % 4), spec = soldier() } end
    return q
  end }
include("arena")

local mine, t, step = {}, 0, 0
on("chr_spawned", function(c) if spawn.alive(c) and t >= 0.9 and #mine < 6 then mine[#mine + 1] = c end end)
local function kill(c) for _ = 1, 40 do if spawn.alive(c) then test.shoot(c, "body", "pp7sil", 0) end end end
on("tick", function(dt)
  t = t + dt
  if step == 0 and t >= 6 and #mine >= 6 then
    step = 1
    for i = 1, 4 do kill(mine[i]) end
    print("ge: arena: A8TEST callout-look: four kills at t=" .. string.format("%.2f", t))
  elseif step == 1 and t >= 6.45 then
    step = 2
    test.screenshot()
    print("ge: arena: A8TEST callout-look: shot 1 (callout)")
  elseif step == 2 and t >= 9 then
    step = 3
    kill(mine[5])
  elseif step == 3 and t >= 12 then
    step = 4
    test.screenshot()
    print("ge: arena: A8TEST callout-look: shot 2 (panel)")
  end
end)
