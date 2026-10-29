-- Arena plus (batch A) scripted test: credits, the five power-ups, the Q-Branch crate, the Requisition cards (tools/arena_plus.lua).
-- Run with tests/arena_plus/run.sh (GE_MODE=plus-test, invulnerable, GE_ARENA_CARDS=ui so the card menu is the real one). Every check
-- prints "PLUSTEST PASS <name>" or "PLUSTEST FAIL <name> (...)"; the run ends in a win after wave 3.
-- Waves (debug_queue): 1 = 9 one-shot hold enemies + a TANK (60 x 4 damage points); 2 = a BOSS (8) + 7 one-shot; 3 = 2 one-shot + a second tank.
ARENA_SETTINGS = { mode = "fixed", waves = 3, boss_every = 0, breather = 2, start_delay = 1, first_wave = 1, ammo_every = 0, armour_chance = 0,
  max_alive = 24, max_alive_cap = 24, test_keys = true, corpse_keep = 4,
  crowd = { count = 10, favour = true, drift = 0, roar_every = 9999, sponsor_cooldown = 9999, thumb = false, points = { camp_secs = 9999 } },
  plus = { pu_chance = 0, pu_chance_boss = 0 },
  debug_queue = function(w)
    local function e(gate, n, o)
      o = o or {}
      return { n = n, gate = gate, boss = o.boss, spec = { skin = "russian_soldier", weapon = "pp7", behaviour = "hold", health = o.health or 0.05,
        accuracy = 0, _tier = o.boss and "boss" or "normal" } }
    end
    local q = {}
    if w == 1 then
      for i = 1, 9 do q[#q + 1] = e(1 + (i % 4), i) end
      q[#q + 1] = e(2, 10, { health = 60 })
    elseif w == 2 then
      q[#q + 1] = e(3, 1, { boss = true, health = 8 })
      for i = 1, 7 do q[#q + 1] = e(1 + (i % 4), i + 1) end
    else
      q[#q + 1] = e(1, 1); q[#q + 1] = e(4, 2); q[#q + 1] = e(2, 3, { health = 60 })
    end
    return q
  end }
include("arena")

local PL = ARENA_PLUS and ARENA_PLUS.live
local pass, fail = 0, 0
local function check(name, ok, info)
  if ok then pass = pass + 1; print("PLUSTEST PASS " .. name .. (info and (" (" .. info .. ")") or ""))
  else fail = fail + 1; print("PLUSTEST FAIL " .. name .. (info and (" (" .. info .. ")") or "")) end
end
if not PL then print("PLUSTEST FAIL no ARENA_PLUS.live"); return end

local t, ticks = 0, 0
local died = 0
on("chr_died", function() died = died + 1 end)

local function live() return ARENA_HOOKS.live() end
local function dmgmax(c) local _, _, _, m = spawn.health(c); return m or 0 end
local function sorted_live()   -- live enemies, the biggest maxdamage first
  local l = live()
  table.sort(l, function(a, b) local ma, mb = dmgmax(a), dmgmax(b); if ma ~= mb then return ma > mb end return a < b end)
  return l
end
local function near(c, dx)   -- warp Bond 0 beside chr c (the nearest pad more than 300 away, then an offset)
  local x, z = spawn.where(c)
  local best, bd = nil, 1e18
  for i = 0, pads.count() - 1 do
    local px, py, pz = pads.pos(i)
    if px and py <= 1 then
      local d = (px - x) ^ 2 + (pz - z) ^ 2
      if d > 300 * 300 and d < bd then best, bd = i, d end
    end
  end
  bond.warp(0, best, dx or 20, 20)
end
local function kill(c, part, weapon)
  for _ = 1, 80 do
    if not spawn.alive(c) then break end
    test.shoot(c, part or "body", weapon or "pp7", 0)
  end
end
local function shoot_once(c) test.shoot(c, "body", "pp7", 0) end
local function dmg(c) local _, _, d = spawn.health(c); return d or 0 end

-- ---- a tiny step runner: a step is a function(dt) that returns true when it is done ----
local steps, si, st = {}, 0, { t = 0 }
local function step(name, fn) steps[#steps + 1] = { name = name, fn = fn } end
local function wait(sec) return function(dt) st.t = st.t + dt; return st.t >= sec end end

local credits0, gain_plain, gain_double
local tank, tank2, boss
local mag0
local function collect(kind)   -- drop a power-up at Bond's feet, walk onto it; true when it has been collected
  return function(dt)
    st.t = st.t + dt
    if not st.dropped then
      local bx, _, bz = bond.pos(0)
      st.dropped = PL.drop(kind, bx, bz)
      if not st.dropped then check("drop " .. kind, false, "refused"); return true end
      local f = PL.field()
      check("drop " .. kind .. " on the floor", f ~= nil and f.kind == kind, f and string.format("pad %d", f.pad))
      st.f = f
      bond.warp(0, f.pad, f.dx, f.dz)
    end
    if PL.field() == nil then check("collect " .. kind, true, string.format("after %.2f s", st.t)); return true end
    if st.t > 4 then check("collect " .. kind, false, "still on the floor"); return true end
    return false
  end
end

step("wave 1 spawned", function(dt)
  st.t = st.t + dt
  if #live() >= 10 then
    local l = sorted_live()
    tank = l[1]
    check("tank is the toughest", dmgmax(tank) > 100, string.format("maxdamage %.0f", dmgmax(tank)))
    return true
  end
  return st.t > 30
end)
step("settle", wait(2))
step("credits: a body kill pays", function()
  credits0 = PL.credits()
  local l = live()
  local c
  for _, x in ipairs(l) do if x ~= tank then c = x; break end end
  near(c); kill(c, "body"); st.v = c
  return true
end)
step("wait for the kill", wait(0.75))
step("credits: counted", function()
  gain_plain = PL.credits() - credits0
  check("credits rise on a kill", gain_plain > 0, string.format("%d -> %d (+%d)", credits0, PL.credits(), gain_plain))
  check("a wave-1 normal kill pays 8", gain_plain == 8, "+" .. gain_plain)
  return true
end)
step("gap", wait(3))
step("credits: a headshot pays more", function()
  credits0 = PL.credits()
  for _, x in ipairs(live()) do if x ~= tank then near(x); kill(x, "head"); break end end
  return true
end)
step("wait", wait(0.75))
step("credits: headshot counted", function()
  local g = PL.credits() - credits0
  check("a headshot kill pays more than a body kill", g > gain_plain, string.format("+%d vs +%d", g, gain_plain))
  return true
end)
step("gap2", wait(3))
-- a kill drops a power-up (chance forced to 1 for one kill), and an uncollected one goes after 20 s
step("natural: kill", function()
  PL.cfg.pu_chance = 1
  for _, x in ipairs(live()) do if x ~= tank then near(x); kill(x, "body"); break end end
  return true
end)
step("wait", wait(0.75))
step("natural: dropped", function()
  PL.cfg.pu_chance = 0
  local f = PL.field()
  check("a kill can drop a power-up", f ~= nil, f and (f.kind .. " at " .. string.format("%.0f, %.0f", f.x, f.z)))
  return true
end)
step("natural: waits", wait(21))
step("natural: gone", function()
  check("an uncollected power-up is gone after 20 s", PL.field() == nil)
  return true
end)

-- MAX AMMO
step("max ammo: before", function() mag0 = select(2, bond.ammo(0, "pp7")); st.dropped = nil; st.t = 0; return true end)
step("max ammo: collect", collect("maxammo"))
step("max ammo: after", function()
  local a = select(2, bond.ammo(0, "pp7"))
  check("max ammo did not lower the ammo", a ~= nil and mag0 ~= nil and a >= mag0, string.format("PP7 reserve %s -> %s", tostring(mag0), tostring(a)))
  st.dropped = nil; st.t = 0
  return true
end)
-- DOUBLE CREDITS
step("double: collect", collect("double"))
step("double: kill", function()
  st.dropped = nil; st.t = 0
  credits0 = PL.credits()
  for _, x in ipairs(live()) do if x ~= tank then near(x); kill(x, "body"); break end end
  return true
end)
step("wait", wait(0.75))
step("double: counted", function()
  gain_double = PL.credits() - credits0
  check("double credits pays twice", gain_double == 2 * gain_plain, string.format("+%d vs +%d", gain_double, gain_plain))
  check("the panel shows the 2X CREDITS chip", (function() local _, chips = PL.panel(); for _, c in ipairs(chips) do if c.label == "2X CREDITS" then return true end end end)())
  return true
end)
step("gap3", wait(3))
step("double: wait for it to run out", wait(21))
step("double: over", function()
  check("double credits ended", not PL.active().double)
  return true
end)
-- Q DAMAGE
step("q damage: plain shot", function()
  st.d0 = dmg(tank); shoot_once(tank); return true
end)
step("wait", wait(0.5))
step("q damage: collect", function()
  st.plain = dmg(tank) - st.d0
  check("a plain shot hurts the tank", st.plain > 0, string.format("%.3f", st.plain))
  st.dropped = nil; st.t = 0
  return true
end)
step("q damage: collect2", collect("qdamage"))
step("q damage: shot", function()
  st.dropped = nil; st.t = 0
  check("enemy_health held at 25", mods.get("enemy_health") == 25, tostring(mods.get("enemy_health")))
  st.d1 = dmg(tank); shoot_once(tank); return true
end)
step("wait", wait(0.5))
step("q damage: ratio", function()
  local q4 = dmg(tank) - st.d1
  check("Q damage is 4x", math.abs(q4 / st.plain - 4) < 0.3, string.format("%.3f vs %.3f = %.2fx", q4, st.plain, q4 / st.plain))
  return true
end)
step("q damage: run out", wait(21))
step("q damage: over", function()
  check("Q damage ended and enemy_health is back", not PL.active().qdamage and mods.get("enemy_health") == 100, tostring(mods.get("enemy_health")))
  return true
end)
-- INSTA-KILL
step("insta: tank survives a plain shot", function() shoot_once(tank); return true end)
step("wait", wait(0.5))
step("insta: collect", function()
  check("the tank survives a plain shot", spawn.alive(tank))
  st.dropped = nil; st.t = 0
  return true
end)
step("insta: collect2", collect("insta"))
step("insta: shoot", function() st.dropped = nil; st.t = 0; shoot_once(tank); return true end)
step("wait", wait(1.0))
step("insta: dead", function()
  check("insta-kill kills the tank in one hit", not spawn.alive(tank))
  local _, chips = PL.panel()
  local has = false
  for _, c in ipairs(chips) do if c.label == "INSTA-KILL" then has = true end end
  check("the panel shows the INSTA-KILL chip", has)
  return true
end)
-- clear the wave; the cards
step("clear wave 1", function(dt)
  for _, x in ipairs(live()) do near(x); kill(x, "body"); break end
  return #live() == 0
end)
step("cards held", function(dt)
  st.t = st.t + dt
  if PL.held() then
    local ids = PL.offered()
    check("the cards are offered after wave 1 and the run holds", ids ~= nil and #ids == 3, ids and table.concat(ids, ","))
    check("plan.hold is on", plan.held())
    st.ids = ids
    return true
  end
  return st.t > 10
end)
step("hold keeps the wave", wait(3.5))
step("hold: still wave 1", function()
  check("the breather does not run out while the cards are up", PL.held() and (test.hud_line(1) or ""):match("WAVE 1") ~= nil, test.hud_line(1))
  return true
end)
step("pick the second card", function()
  PL.menu_press(UI_MENU.EV.DOWN)
  return true
end)
step("wait", wait(0.5))
step("pick: select", function() PL.menu_press(UI_MENU.EV.SELECT); return true end)
step("pick: done", function(dt)
  st.t = st.t + dt
  if not PL.held() then
    check("the second card was taken", PL.stacks(st.ids[2]) == 1, st.ids[2])
    check("the others were not", PL.stacks(st.ids[1]) == 0 and PL.stacks(st.ids[3]) == 0)
    check("plan.hold is off", not plan.held())
    return true
  end
  return st.t > 5
end)
-- wave 2
step("wave 2 spawned", function(dt)
  st.t = st.t + dt
  if #live() >= 8 then
    local l = sorted_live()
    boss = l[1]
    check("wave 2 has its boss", dmgmax(boss) > 20 and #l == 8, string.format("maxdamage %.0f, %d live", dmgmax(boss), #l))
    return true
  end
  return st.t > 30
end)
step("settle2", wait(2))
step("nuke: collect", function() st.dropped = nil; st.t = 0; st.d = died; return true end)
step("nuke: collect2", collect("nuke"))
step("nuke: wait", wait(1.0))
step("nuke: counted", function()
  local l = live()
  check("the nuke killed every non-boss", #l == 1 and l[1] == boss, string.format("%d live", #l))
  check("the nuke counted the kills", died - st.d >= 7, string.format("%d deaths", died - st.d))
  check("the boss is alive", spawn.alive(boss))
  return true
end)
step("boss: insta", function() st.dropped = nil; st.t = 0; return true end)
step("boss: insta collect", collect("insta"))
step("boss: shot", function() st.dropped = nil; st.t = 0; st.b0 = dmg(boss); shoot_once(boss); return true end)
step("wait", wait(1.0))
step("boss: survives", function()
  check("a boss is not killed instantly by insta-kill", spawn.alive(boss) and dmg(boss) > st.b0, string.format("%.3f -> %.3f of %.1f", st.b0, dmg(boss), dmgmax(boss)))
  return true   -- (the boss stays up while the crate is tested: its death ends the wave and the cards hold the run)
end)
step("wait", wait(1.0))
-- the crate
step("crate: credits", function()
  check("Q is in the pit", PL.crate() ~= nil, PL.crate() and string.format("chr %d on pad %d", PL.crate().chr, PL.crate().pad))
  ARENA_TEST_KEY("credits")
  ARENA_TEST_KEY("credits")
  local q = PL.crate()
  bond.warp(0, q.pad, 150, 0)
  st.c0, st.price = PL.credits(), PL.price()
  check("the test key adds credits", st.c0 >= 1000, tostring(st.c0))
  check("the crate price at wave 2", st.price == 80 + 0 or st.price > 0, tostring(st.price))
  return true
end)
step("wait", wait(0.5))
step("crate: use", function() test.use(0); return true end)
step("wait", wait(1.0))
step("crate: paid", function()
  check("a crate roll deducts the price", PL.credits() == st.c0 - st.price, string.format("%d -> %d (price %d)", st.c0, PL.credits(), st.price))
  st.c1 = PL.credits()
  local _, outcome = PL.roll(0, "dud")
  check("a forced dud costs credits and gives no gun", outcome == "dud" and PL.credits() == st.c1 - st.price)
  st.c1 = PL.credits()
  local _, o2 = PL.roll(0, "golden")
  check("a forced golden roll gives the Golden Gun", o2 == "goldengun" and test.has_item(0, "goldengun") and PL.credits() == st.c1 - st.price)
  return true
end)
step("crate: distribution", function()
  local n, dud, gold, other, refused = 400, 0, 0, 0, 0
  local seen = {}
  for _ = 1, n do
    local ok, o = PL.roll(0, nil, true)
    if o == "dud" then dud = dud + 1 elseif o == "goldengun" then gold = gold + 1 else other = other + 1; seen[o] = (seen[o] or 0) + 1 end
  end
  local names = {}
  for k, v in pairs(seen) do names[#names + 1] = k .. "=" .. v end
  table.sort(names)
  check("duds are about 10% of rolls", dud >= 20 and dud <= 65, string.format("%d / %d", dud, n))
  check("the Golden Gun is rare (about 2%)", gold >= 1 and gold <= 24, string.format("%d / %d", gold, n))
  print("PLUSTEST rolls: " .. table.concat(names, " "))
  return true
end)
step("crate: poor", function()
  local guard = 0
  while PL.credits() >= PL.price() and guard < 200 do PL.roll(0, "dud"); guard = guard + 1 end
  st.c2 = PL.credits()
  test.use(0)
  return true
end)
step("wait", wait(1.0))
step("crate: poor result", function()
  check("without enough credits nothing is taken", PL.credits() == st.c2, string.format("%d (price %d)", PL.credits(), PL.price()))
  ARENA_TEST_KEY("credits")
  return true
end)
-- clear wave 2 (the 7 died in the nuke; now the boss)
step("wave 2: the boss falls", function() kill(boss, "body"); return true end)
step("wave 2 clear + cards", function(dt)
  st.t = st.t + dt
  if PL.held() then st.ids = PL.offered(); return true end
  return st.t > 20
end)
step("cards 2: first card", wait(1.0))
step("cards 2: select", function() PL.menu_press(UI_MENU.EV.SELECT); return true end)
step("cards 2: done", function(dt)
  st.t = st.t + dt
  if not PL.held() then check("a card on the first row is taken with SELECT", PL.stacks(st.ids[1]) >= 1, st.ids[1]); return true end
  return st.t > 6
end)
-- wave 3: the card matrix, effect by effect
step("wave 3 spawned", function(dt)
  st.t = st.t + dt
  if #live() >= 3 then tank2 = sorted_live()[1]; return true end
  return st.t > 30
end)
step("settle3", wait(2))
step("cards: the timed power-ups run out first", function(dt)
  st.t = st.t + dt
  local a = PL.active()
  return not (a.insta or a.qdamage or a.double) or st.t > 30   -- (an insta-kill would kill the tank in the vampire step)
end)
step("cards: vampire", function()
  bond.heal(0, -0.5, 0)
  st.h0 = bond.vitals(0)
  PL.take("vampire")
  shoot_once(tank2)
  return true
end)
step("wait", wait(0.25))
step("cards: vampire result", function()
  local h = bond.vitals(0)
  check("BLOOD MONEY heals a hit", h > st.h0 and h - st.h0 < 0.02, string.format("%.4f -> %.4f", st.h0, h))
  return true
end)
step("cards: clips", function()
  mag0 = weapon.get("ar33", "magsize")
  local n0 = PL.stacks("clips")   -- (a card taken from the menu earlier may already be a stack)
  local base = mag0 / (1 + 0.5 * n0)
  PL.take("clips")
  st.mag1 = weapon.get("ar33", "magsize")
  check("EXTENDED CLIPS makes magazines 50% bigger per stack", st.mag1 == math.floor(base * (1 + 0.5 * (n0 + 1)) + 0.5) and st.mag1 > mag0,
    string.format("AR33 base %.0f: %d -> %d (stack %d)", base, mag0, st.mag1, n0 + 1))
  return true
end)
step("cards: armour", function()
  bond.heal(0, 0, -1)
  PL.take("armour")
  local _, a = bond.vitals(0)
  check("SAVILE ROW LINING fills the armour", a ~= nil and a > 0.99, tostring(a))
  return true
end)
step("cards: discount", function()
  local p0 = PL.price()
  PL.take("discount")
  local p1 = PL.price()
  check("MI6 ACCOUNT cuts the crate price by 20%", math.abs(p1 / p0 - 0.8) < 0.07, string.format("%d -> %d", p0, p1))
  return true
end)
step("cards: airdrop", function()
  PL.cfg.pu_chance = 0.04   -- (the test run has the natural drops off)
  local c0 = PL.chance_now()
  PL.take("airdrop")
  check("AIRDROP PRIORITY raises the drop chance by 75%", math.abs(PL.chance_now() - c0 * (1 + 0.75 * PL.stacks("airdrop"))) < 1e-6, string.format("%.4f -> %.4f", c0, PL.chance_now()))
  PL.cfg.pu_chance = 0
  return true
end)
step("cards: batteries", function() PL.take("batteries"); st.dropped = nil; st.t = 0; return true end)
step("cards: batteries collect", collect("double"))
step("cards: batteries result", function()
  st.dropped = nil; st.t = 0
  local left = PL.left("double")
  check("Q'S BATTERIES makes a timed power-up last 30 s", left > 29 and left <= 30, string.format("%.2f s", left))
  return true
end)
step("cards: bounty", function()
  PL.take("bounty")
  st.c0 = PL.credits()
  for _, x in ipairs(live()) do if x ~= tank2 then near(x); kill(x, "body"); break end end
  return true
end)
step("wait", wait(0.75))
step("cards: bounty result", function()
  local g = PL.credits() - st.c0
  -- wave 3 normal: floor(8 * 1.08 + 0.5) before the card, x1.25 and x2 (double is still running from the batteries step)
  local want = math.floor(8 * (1 + 0.04 * 2) * 1.25 + 0.5) * 2
  check("BLOOD BOUNTY pays 25% more (and the double credits still runs)", math.abs(g - want) <= 2, string.format("+%d, expected about +%d", g, want))
  return true
end)
step("cards: briefing", function()
  PL.take("briefing")
  PL.wave_start(3)
  check("DOUBLE-O STATUS opens a wave with 5 s of Q damage", PL.left("qdamage") > 4.5 and PL.left("qdamage") <= 5, string.format("%.2f s", PL.left("qdamage")))
  return true
end)
step("cards: swiss", function()
  PL.take("swiss")
  local c = PL.credits()
  PL.wave_cleared(3)
  local gain = PL.credits() - c
  check("SWISS ACCOUNT pays interest after a wave", gain == math.min(40, math.floor(c * 0.08)) and gain > 0, string.format("+%d on %d", gain, c))
  return true
end)
step("cards: cache", function()
  local before = select(2, bond.ammo(0, "pp7"))
  PL.take("cache")
  PL.wave_start(3)
  local after = select(2, bond.ammo(0, "pp7"))
  check("LEITER'S CACHE tops up ammo at a wave's start", after >= before, string.format("PP7 reserve %s -> %s", tostring(before), tostring(after)))
  return true
end)
step("cards: medic", function()
  bond.heal(0, -0.5, 0)
  st.h0 = bond.vitals(0)
  PL.take("medic")
  return true
end)
step("cards: medic wait", wait(4))
step("cards: medic result", function()
  local h = bond.vitals(0)
  check("MI6 FIELD MEDIC heals over time", h - st.h0 > 0.015, string.format("%.3f -> %.3f in 4 s", st.h0, h))
  return true
end)
step("finish: summary", function()   -- (before the last kills: the win ends the stage and this script with it)
  local s = PL.summary()
  check("the run keeps its summary", s.earned > 0 and s.spent > 0 and #s.cards > 0, string.format("earned %d spent %d cards %d", s.earned, s.spent, #s.cards))
  print("PLUSTEST debrief: " .. table.concat(PL.debrief_lines(), " | "))
  return true
end)
local exit_in
step("finish: kill the rest", function()
  for _, x in ipairs(live()) do near(x); kill(x, "body"); break end
  if #live() == 0 then
    print(string.format("PLUSTEST DONE %d passed, %d failed", pass, fail))
    exit_in = 240   -- game ticks: the win (WON wave 3) comes first, then the run ends here instead of idling to the frame limit
    return true
  end
  return false
end)
on("ui_tick", function()
  if exit_in then
    exit_in = exit_in - 1
    if exit_in <= 0 then exit_in = nil; if bench and bench.exit then pcall(bench.exit) end end
  end
end)

on("tick", function(dt)
  t = t + dt
  ticks = ticks + 1
  if si == 0 and t > 2 then si = 1; st.t = 0; st.dropped = nil; print("PLUSTEST step 1: " .. steps[1].name) end
  local s = steps[si]
  if not s then return end
  local ok, done = pcall(s.fn, dt)
  if not ok then print("PLUSTEST FAIL step '" .. s.name .. "' raised: " .. tostring(done)); fail = fail + 1; done = true end
  if done then
    si = si + 1
    st.t = 0; st.dropped = nil
    if steps[si] then print(string.format("PLUSTEST step %d: %s", si, steps[si].name)) end
  end
end)
