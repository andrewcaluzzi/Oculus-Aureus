-- Heist guards + civilians test mode (P0 jobs 3-4) on Bunker 2. Prints "HEIST PASS|FAIL <name> ..." and "HEIST DONE n passed, m failed".
-- The mode name starts with "heist", so the mode gets the 250-slot reservation (GE_MODE_SLOTS overrides).
-- Run: tests/heist/run_guards.sh (inside the build container, Xvfb on :99).
include("heist_lib")
mode.keep_locks()
spawn.keep_bodies(true)

local pass, fail = 0, 0
local function check(name, ok, extra)
  if ok then pass = pass + 1 else fail = fail + 1 end
  print(string.format("HEIST %s %s %s", ok and "PASS" or "FAIL", name, extra or ""))
end

local ev = { alarm = {}, body = {}, ckilled = {}, csurr = {}, crobbed = {} }
on("alarm", function(a) ev.alarm[#ev.alarm + 1] = a; print("HEIST event alarm " .. a) end)
on("body_found", function(g, b) ev.body[#ev.body + 1] = { g, b }; print(string.format("HEIST event body_found guard=%d body=%d", g, b)) end)
on("civilian_killed", function(c, p) ev.ckilled[#ev.ckilled + 1] = { c, p }; print(string.format("HEIST event civilian_killed chr=%d killer=%d", c, p)) end)
on("civilian_surrendered", function(c, p) ev.csurr[#ev.csurr + 1] = { c, p }; print(string.format("HEIST event civilian_surrendered chr=%d player=%d", c, p)) end)
on("civilian_robbed", function(c, p) ev.crobbed[#ev.crobbed + 1] = { c, p }; print(string.format("HEIST event civilian_robbed chr=%d player=%d", c, p)) end)

local aim = nil
local heal = false
local function wait(sec)
  local t = 0
  while t < sec do
    t = t + coroutine.yield()
    if aim then bond.face(0, aim[1], aim[2]) end
    if heal then bond.heal(0, 1, 1) end
  end
end
local function dist2(x1, z1, x2, z2) return math.sqrt((x1 - x2) ^ 2 + (z1 - z2) ^ 2) end

local function body()
  wait(1)
  bond.give(0, "pp7sil")
  local free0 = spawn.free()
  check("chr slots reserved (~250)", free0 >= 240, "free=" .. free0)
  print("HEIST pools (loose, held, free weapon slots, free props): " .. table.concat({ spawn.pools() }, ", "))

  if not mode.env_flag("GE_HEIST_CIV") then
  -- A. hearing: silenced first shot = loudness 1, unsilenced first shot = 3, at the cap 12 (radius = hearing * noise * 100 units).
  -- One guard at a time on pad 54, Bond warped ~200 units away; the expectation is computed from the measured distance.
  local function hear_case(label, opts, noise)
    opts.behaviour = "hold"; opts.weapon = "none"
    local g = spawn.chr(54, opts)
    wait(0.3)
    bond.warp(0, 54, -200, 0)
    wait(0.5)
    local bx, _, bz = bond.pos(0)
    local gx, gz = spawn.where(g)
    local d = dist2(bx, bz, gx, gz)
    spawn.heard(g, true)
    bond.test_noise(0, noise)
    wait(0.1)
    local heard = spawn.heard(g)
    local radius = (opts.hearing or 1) * noise * 100
    check("hearing " .. label, heard == (d < radius), string.format("d=%.0f radius=%.0f heard=%s", d, radius, tostring(heard)))
    spawn.remove(g)
    wait(0.5)
    return heard
  end
  local h1 = hear_case("silenced first shot (noise 1)", {}, 1.0)
  local h2 = hear_case("unsilenced first shot (noise 3)", {}, 3.0)
  hear_case("hearing=0.2 vs noise 3", { hearing = 0.2 }, 3.0)
  hear_case("hearing=5 vs silenced shot", { hearing = 5 }, 1.0)
  check("silenced is quieter than unsilenced at the same distance", (not h1) and h2)

  -- B. alarm guard runs to its pad and sounds the alarm
  alarm.stop(); wait(1)
  check("alarm idle before the alarm guard acts", not alarm.active())
  local a0 = #ev.alarm
  local best, bestd = 55, 1e9
  local p54x, _, p54z = pads.pos(54)
  for _, c in ipairs({ 55, 58, 59, 60, 61, 62, 63, 64, 65 }) do
    local cx, _, cz = pads.pos(c)
    local dd = dist2(p54x, p54z, cx, cz)
    print(string.format("HEIST pad %d is %.0f from pad 54", c, dd))
    if dd >= 300 and dd < 600 and dd < bestd then best, bestd = c, dd end
  end
  local rpx, _, rpz = pads.pos(best)
  local g = spawn.chr(54, { behaviour = "hold", weapon = "none", alarm_pad = best, vision = 20000 })
  check("alarm guard spawned", g ~= nil)
  wait(1)
  bond.warp(0, 54, -200, 0); wait(0.5)
  local sx, sz = spawn.where(g)
  bond.test_noise(0, 3.0)        -- guard hears Bond: the raiser list runs to the pad
  local arrived, t = false, 0
  while t < 25 and not alarm.active() do
    local gx, gz = spawn.where(g)
    if gx and dist2(gx, gz, rpx, rpz) < 120 then arrived = true end
    wait(0.5); t = t + 0.5
    if t % 5 == 0 then local qx, qz = spawn.where(g); print(string.format("HEIST   t=%.0f guard %.0f,%.0f act=%s pad %.0f,%.0f", t, qx or 0, qz or 0, tostring(select(3, spawn.where(g))), rpx, rpz)) end
  end
  local gx, gz = spawn.where(g)
  print(string.format("HEIST alarm guard: start %.0f,%.0f pad %.0f,%.0f now %.0f,%.0f after %.1fs", sx, sz, rpx, rpz, gx or 0, gz or 0, t))
  check("alarm guard ran to the pad", arrived or alarm.active(), string.format("dist to pad %.0f", gx and dist2(gx, gz, rpx, rpz) or -1))
  check("alarm fired (event + active)", alarm.active() and ev.alarm[a0 + 1] == 1, "events=" .. table.concat(ev.alarm, ","))
  spawn.remove(g)
  wait(1.5)
  alarm.stop()
  if mode.env_flag("GE_HEIST_ALARM") then print(string.format("HEIST DONE (alarm only) %d passed, %d failed", pass, fail)); mission.fail("alarm only"); return end
  wait(1)

  -- C. body found: a corpse already lying there; a witness comes in later, notices it, and goes on alert
  local victim = spawn.chr(58, { behaviour = "hold", weapon = "none" })
  wait(1)
  spawn.kill(victim)
  wait(4)
  check("victim is down", spawn.down(victim) == true)
  local b0 = #ev.body
  local witness = spawn.chr(59, { behaviour = "hold", weapon = "none" })
  local wx0, wz0 = spawn.where(witness)
  local bx, by, bz = bond.pos(0)
  local d0 = dist2(wx0, wz0, bx, bz)
  wait(3)
  local wx1, wz1 = spawn.where(witness)
  check("body_found event fired", #ev.body > b0 and ev.body[b0 + 1][1] == witness and ev.body[b0 + 1][2] == victim,
        #ev.body > b0 and string.format("guard=%d body=%d", ev.body[b0 + 1][1], ev.body[b0 + 1][2]) or "none")
  check("bodies.found counts it", bodies.found() >= 1)
  local d1 = dist2(wx1, wz1, bx, bz)
  check("witness went on alert (ran at Bond)", d1 < d0 - 100, string.format("dist to Bond %.0f -> %.0f", d0, d1))
  spawn.remove(witness)

  -- D. reinforcement waves while the alarm is on
  local spawnat = {}
  on("chr_spawned", function(c) local x, z = spawn.where(c); if x then spawnat[c] = { x, z } end end)
  local r = heist.reinforce{ pads = { 50, 25 }, interval = 3, per_wave = 3, max_alive = 6, opts = { weapon = "none", behaviour = "chase" } }
  wait(4)
  check("no wave without alarm", r.spawned == 0)
  alarm.start()
  wait(4)
  local nwave = r.spawned
  local epx, _, epz = pads.pos(50)
  local epx2, _, epz2 = pads.pos(25)
  local atent = 0
  for chr in pairs(r.mine) do
    local x, z = (spawnat[chr] or {})[1], (spawnat[chr] or {})[2]
    if x and (dist2(x, z, epx, epz) < 300 or dist2(x, z, epx2, epz2) < 300) then atent = atent + 1 end
  end
  check("wave spawned at the entrance while alarm on", nwave >= 3 and atent == nwave, string.format("spawned=%d at_entrance=%d alive=%d waves=%d", nwave, atent, r.alive(), r.waves))
  wait(8)
  check("wave cap respected", r.alive() <= 6 and r.spawned >= 4, string.format("alive=%d spawned=%d", r.alive(), r.spawned))
  alarm.stop()
  r.stop()
  local before = r.spawned
  wait(5)
  check("no more waves after alarm off", r.spawned == before)
  for chr in pairs(r.mine) do spawn.remove(chr) end
  wait(1)

  end
  -- E. civilians
  heal = true
  bond.warp(0, 61)
  wait(1)
  local civ = spawn.chr(62, { behaviour = "civilian", weapon = "ak47", body = "Civilian_1_Female" })
  check("civilian spawned", civ ~= nil and civilian.state(civ) == "idle")
  local kh = spawn.key(civ, 0x80)
  check("civilian carries a key", kh ~= nil and select(2, spawn.key_alive(kh)) == true)
  local cx, cz = spawn.where(civ)
  local px, _, pz = bond.pos(0)
  local dc = dist2(px, pz, cx, cz)
  print(string.format("HEIST civilian at %.0f,%.0f, Bond %.0f,%.0f dist %.0f", cx, cz, px, pz, dc))
  -- a guard with a gun in the same room does not target it
  local gd = spawn.chr(63, { behaviour = "standard", weapon = "ak47" })
  wait(2)
  check("civilian idle while nobody aims", civilian.state(civ) == "idle" and spawn.alive(civ))
  bond.test_noise(0, 12.0)   -- a loud shot: the civilian cowers (before anyone aims at it)
  wait(0.5)
  check("civilian cowers on gunfire", civilian.state(civ) == "cowering", civilian.state(civ) or "nil")
  wait(5.5)
  check("civilian recovers from cowering", civilian.state(civ) == "idle", civilian.state(civ) or "nil")
  civilian.tune{ range = 3000 }
  aim = { cx, cz }
  wait(1.5)
  check("civilian surrenders when aimed at", civilian.state(civ) == "surrendered" and #ev.csurr >= 1, civilian.state(civ) or "nil")
  local a1, held1 = spawn.key_alive(kh)
  check("key still carried right after surrender", a1 and held1)
  wait(3)
  local a2, held2 = spawn.key_alive(kh)
  check("civilian drops its key after the sustained threat", a2 and not held2 and #ev.crobbed >= 1, string.format("held=%s robbed=%d", tostring(held2), #ev.crobbed))
  aim = nil
  print("HEIST step: aim off")
  civilian.tune{ range = 1 }   -- nobody in aim range any more
  print("HEIST step: faced away")
  wait(4.5)
  check("civilian lowers its hands when nobody aims", civilian.state(civ) == "idle", civilian.state(civ) or "nil")
  check("guards do not target the civilian", spawn.alive(civ) and civilian.killed() == 0, "killed=" .. civilian.killed())
  spawn.remove(gd)
  spawn.kill(civ)
  wait(1.5)
  check("civilian death counter + event", civilian.killed() == 1 and #ev.ckilled == 1 and ev.ckilled[1][1] == civ, string.format("killed=%d events=%d", civilian.killed(), #ev.ckilled))

  -- F. crowd of guards: many chrs in one stage
  local made = 0
  local hall = { 0, 1, 2, 3, 6, 7, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66 }   -- control-room hall pads
  for round = 1, 12 do
    for _, pad in ipairs(hall) do
      local c = spawn.chr(pad, { behaviour = "chase", weapon = "none" })
      if c then made = made + 1 end
    end
    wait(0.4)
  end
  wait(3)
  print("HEIST pools after crowd: " .. table.concat({ spawn.pools() }, ", "))
  check("150+ guards spawned in one stage", made >= 150, string.format("made=%d free=%d", made, spawn.free()))

  print(string.format("HEIST DONE %d passed, %d failed", pass, fail))
  if fail == 0 then mission.win("heist guards ok") else mission.fail("heist guards failed") end
end

local co = coroutine.wrap(body)
local finished = false
on("tick", function(dt)
  if finished then return end
  local ok, err = pcall(co, dt)
  if not ok then finished = true; print("HEIST FAIL script error: " .. tostring(err)) end
end)
