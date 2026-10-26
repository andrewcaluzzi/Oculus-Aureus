-- Heist guards + civilians test mode (P0 jobs 3-4; in the Janus bank's hall since P2.4). Prints "HEIST PASS|FAIL <name> ..." and
-- "HEIST DONE n passed, m failed". The mode name starts with "heist", so the mode gets the 250-slot reservation (GE_MODE_SLOTS overrides).
-- Run: tests/heist/run_guards.sh (inside the build container, Xvfb on :99).
include("heist_lib")
include("levelgen_layout")
mode.keep_locks()
spawn.keep_bodies(true)

-- the bank's banking hall (designs/bank-heist.md section 14): its customer side is one open floor between the vestibule and the counter
local PD = LEVEL.pads
local PG, PVICTIM, PWITNESS, PBOND = PD.hall_mid, PD.cust_5, PD.hall_post_nw, PD.hall_entry_w   -- (victim and witness 255 apart:
                                                                                                -- within a stock guard's sight of a body)
local PCIV, PARMED, PBYST, PSHOOT = PD.cust_8, PD.teller_back, PD.hall_mid, PD.cust_9   -- (the armed guard behind the counter: in the room, out of sight of Bond;
                                                                                         -- bank v2: the civilian 690 north of Bond, within a loud shot's 1200;
                                                                                         -- the shooter 760 off, as v1's pads stood in its smaller hall)
local PWAVE1, PWAVE2 = PD.vest_mid, PD.hall_post_se
local ALARM_CAND = { LEVEL.objects.alarm_hall.pad }   -- a stock raiser sounds the alarm only at an alarm box: the hall's (its bound pad)
local function ppos(p)   -- pads.pos, or the alarm box's own x, y, z for its bound pad (pads.pos takes floor pads only)
  if p == LEVEL.objects.alarm_hall.pad then local o = LEVEL.objects.alarm_hall; return o.x, o.y, o.z end
  return pads.pos(p)
end
local HALL_PADS = {}
for _, n in ipairs({ "hall_mid", "hall_post_sw", "hall_post_se", "hall_post_ne", "hall_post_nw", "hall_entry_w", "hall_entry_e", "cust_1",
                     "cust_2", "cust_3", "cust_4", "cust_5", "teller_mid", "teller_back", "flap_s", "flap_n" }) do HALL_PADS[#HALL_PADS + 1] = PD[n] end
for _, p in ipairs(LEVEL.rooms.hall.pads) do HALL_PADS[#HALL_PADS + 1] = p end

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
local pin = false
local function wait(sec)
  local t = 0
  while t < sec do
    t = t + coroutine.yield()
    if pin then bond.warp(0, PBOND) end
    if aim then test.face(0, aim[1], aim[2]) end
    if heal then bond.heal(0, 1, 1) end
  end
end
local function dist2(x1, z1, x2, z2) return math.sqrt((x1 - x2) ^ 2 + (z1 - z2) ^ 2) end

local function body()
  wait(1)
  cctv.alarm_on_spot(false)   -- the bank's two hall cameras see Bond: only this test's own alarms count
  for _, c in ipairs(cctv.list()) do cctv.disable(c) end
  bond.give(0, "pp7sil")
  bond.equip(0, "pp7sil")   -- in hand: the slappers no longer count as a gun for the civilians
  local free0 = spawn.free()
  check("chr slots reserved (~250)", free0 >= 240, "free=" .. free0)
  print("HEIST pools (loose, held, free weapon slots, free props): " .. table.concat({ spawn.pools() }, ", "))

  if not mode.env_flag("GE_HEIST_CIV") then
  -- A. hearing: silenced first shot = loudness 1, unsilenced first shot = 3, at the cap 12 (radius = hearing * noise * 100 units).
  -- One guard at a time on the hall's middle pad, Bond warped ~200 units away; the expectation is computed from the measured distance.
  local function hear_case(label, opts, noise)
    opts.behaviour = "hold"; opts.weapon = "none"
    local g = spawn.chr(PG, opts)
    wait(0.3)
    bond.warp(0, PG, -200, 0)
    wait(0.5)
    local bx, _, bz = bond.pos(0)
    local gx, gz = spawn.where(g)
    local d = dist2(bx, bz, gx, gz)
    test.heard(g, true)
    test.shot_noise(0, noise)
    wait(0.1)
    local heard = test.heard(g)
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
  local best, bestd = ALARM_CAND[1], 1e9
  local p54x, _, p54z = pads.pos(PG)
  for _, c in ipairs(ALARM_CAND) do
    local cx, _, cz = ppos(c)
    local dd = dist2(p54x, p54z, cx, cz)
    print(string.format("HEIST pad %d is %.0f from pad 54", c, dd))
    if dd >= 300 and dd < 600 and dd < bestd then best, bestd = c, dd end
  end
  local rpx, _, rpz = ppos(best)
  local g = spawn.chr(PG, { behaviour = "hold", weapon = "none", alarm_pad = best, vision = 20000 })
  check("alarm guard spawned", g ~= nil)
  wait(1)
  bond.warp(0, PG, -200, 0); wait(0.5)
  local sx, sz = spawn.where(g)
  test.shot_noise(0, 3.0)        -- guard hears Bond: the raiser list runs to the pad
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
  local victim = spawn.chr(PVICTIM, { behaviour = "hold", weapon = "none" })
  wait(1)
  spawn.kill(victim)
  wait(4)
  check("victim is down", spawn.down(victim) == true)
  do   -- bank v2: the hall is 38 m long: Bond comes within 9 m of the scene and looks at it (far or off screen, a chr's sight is lazy)
    bond.warp(0, PD.cust_7)
    wait(0.5)
    local vx, vz = spawn.where(victim)
    if vx then aim = { vx, vz } end
    wait(0.5)
  end
  local b0 = #ev.body
  local witness = spawn.chr(PWITNESS, { behaviour = "hold", weapon = "none" })
  local wx0, wz0 = spawn.where(witness)
  local bx, by, bz = bond.pos(0)
  local d0 = dist2(wx0, wz0, bx, bz)
  -- the stock body check is a random roll (sim RNG; run_guards.sh seeds it): the roll repeats while the body is in sight, so give it up
  -- to 20 s rather than banking on the first rolls (bank v2/v3: 6 s failed about 1 run in 3)
  local tb = 0
  while tb < 20 and #ev.body == b0 do wait(0.25); tb = tb + 0.25 end
  print(string.format("HEIST body found after %.2f s", tb))
  check("body_found event fired", #ev.body > b0 and ev.body[b0 + 1][1] == witness and ev.body[b0 + 1][2] == victim,
        #ev.body > b0 and string.format("guard=%d body=%d", ev.body[b0 + 1][1], ev.body[b0 + 1][2]) or "none")
  check("bodies.found counts it", bodies.found() >= 1)
  -- alerted, he comes at Bond: closing in 100 units can take more than the old fixed 2 s (he turns, draws, finds a path first)
  -- (the stock reaction to a seen death is a roll too: he runs at Bond, or runs over to the body first (GE_SEED 1: ACT_RUNPOS to the
  -- corpse, then stands over it, about 90-105 from it). Either is the alert; a witness who stays put is the failure)
  local vx, vz = spawn.where(victim)
  local e0 = vx and dist2(wx0, wz0, vx, vz) or 0
  local d1, e1, tw, acts = d0, e0, 0, {}
  while tw < 8 do
    wait(0.25); tw = tw + 0.25
    local wx1, wz1, act = spawn.where(witness)
    if wx1 then d1 = math.min(d1, dist2(wx1, wz1, bx, bz)); if vx then e1 = math.min(e1, dist2(wx1, wz1, vx, vz)) end end
    if act and acts[#acts] ~= act then acts[#acts + 1] = act end
    if d1 < d0 - 100 then break end
  end
  local ran = false
  for _, a in ipairs(acts) do if a == 13 then ran = true end end   -- (ACT_RUNPOS: GE_SEED 3 stopped 104 from the body)
  check("witness went on alert (ran at Bond, or ran to the body)", d1 < d0 - 100 or (ran and e0 - e1 > 50 and e1 < 150),
        string.format("dist to Bond %.0f -> %.0f, to the body %.0f -> %.0f (%.2f s), acts %s", d0, d1, e0, e1, tw, table.concat(acts, ",")))
  spawn.remove(witness)
  aim = nil

  -- D. reinforcement waves while the alarm is on
  local spawnat = {}
  on("chr_spawned", function(c) local x, z = spawn.where(c); if x then spawnat[c] = { x, z } end end)
  local r = heist.reinforce{ pads = { PWAVE1, PWAVE2 }, interval = 3, per_wave = 3, max_alive = 6, opts = { weapon = "none", behaviour = "chase" } }
  wait(4)
  check("no wave without alarm", r.spawned == 0)
  alarm.start()
  wait(4)
  local nwave = r.spawned
  local epx, _, epz = pads.pos(PWAVE1)
  local epx2, _, epz2 = pads.pos(PWAVE2)
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
  bond.warp(0, PBOND)
  wait(1)
  local civ = spawn.chr(PCIV, { behaviour = "civilian", weapon = "ak47", body = "Civilian_1_Female" })
  check("civilian spawned", civ ~= nil and civilian.state(civ) == "idle")
  local kh = spawn.key(civ, 0x80)
  check("civilian carries a key", kh ~= nil and select(2, spawn.key_alive(kh)) == true)
  local cx, cz = spawn.where(civ)
  local px, _, pz = bond.pos(0)
  local dc = dist2(px, pz, cx, cz)
  print(string.format("HEIST civilian at %.0f,%.0f, Bond %.0f,%.0f dist %.0f", cx, cz, px, pz, dc))
  -- a guard with a gun in the same room does not target it
  local gd = spawn.chr(PARMED, { behaviour = "standard", weapon = "ak47" })
  wait(2)
  check("civilian idle while nobody aims", civilian.state(civ) == "idle" and spawn.alive(civ))
  test.shot_noise(0, 12.0)   -- a loud shot: the civilian cowers (before anyone aims at it)
  wait(0.5)
  check("civilian cowers on gunfire", civilian.state(civ) == "cowering", civilian.state(civ) or "nil")
  wait(5.5)
  check("civilian recovers from cowering", civilian.state(civ) == "idle", civilian.state(civ) or "nil")
  civilian.tune{ range = 3000 }
  -- bare fists are no threat (job 6: the slappers used to count as a gun)
  bond.equip(0, "fist")
  wait(1.5)
  aim = { cx, cz }
  wait(2)
  check("fists: aiming the slappers does not make it surrender", civilian.state(civ) == "idle" and #ev.csurr == 0,
        string.format("%s, weapon %s", civilian.state(civ) or "nil", tostring(bond.weapon(0))))
  civilian.tune{ range = 1 }   -- out of aim range while the PP7 comes out and the bystanders are placed (the view stays on it)
  bond.equip(0, "pp7sil")
  wait(1.5)
  check("bond.equip draws the PP7 again", bond.weapon(0) == 5, "weapon=" .. tostring(bond.weapon(0)))
  -- bystanders: one right beside Bond, one 400 units out but well off the line of fire; neither is being aimed at
  local bys = {}
  local b1 = spawn.chr(PBOND, { behaviour = "civilian", body = "Civilian_2", offset = { 150, 0 } })   -- beside him, ~150 off the line
  local b2 = spawn.chr(PBYST, { behaviour = "civilian", body = "Civilian_2" })   -- ~430 out, ~260 off the line to the civilian
  if b1 then bys[#bys + 1] = b1 end
  if b2 then bys[#bys + 1] = b2 end
  check("two bystander civilians placed", #bys == 2, string.format("b1=%s b2=%s", tostring(b1), tostring(b2)))
  civilian.tune{ range = 3000 }
  wait(1.5)
  check("civilian surrenders when aimed at", civilian.state(civ) == "surrendered" and #ev.csurr >= 1, civilian.state(civ) or "nil")
  local a1, held1 = spawn.key_alive(kh)
  check("key still carried right after surrender", a1 and held1)
  wait(3)
  local a2, held2 = spawn.key_alive(kh)
  check("civilian drops its key after the sustained threat", a2 and not held2 and #ev.crobbed >= 1, string.format("held=%s robbed=%d", tostring(held2), #ev.crobbed))
  local bothered = 0
  for _, b in ipairs(bys) do
    if civilian.state(b) ~= "idle" then bothered = bothered + 1 end
    for _, e in ipairs(ev.csurr) do if e[1] == b then bothered = bothered + 1 end end
    for _, e in ipairs(ev.crobbed) do if e[1] == b then bothered = bothered + 1 end end
  end
  check("bystanders beside / off the line of an armed Bond are not threatened or robbed", bothered == 0, "bothered=" .. bothered)
  for _, b in ipairs(bys) do spawn.remove(b) end
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

  -- G. hold fire (mode.hold_fire; AC delegated, Claude's call 2026-10-25): a guard with a bank civilian in his line of fire to Bond
  -- holds his shots; once the line is clear he fires
  mode.hold_fire(true)
  bond.warp(0, PBOND)
  wait(1)
  pin = true                              -- Bond is held on his pad (the hits would push him about and swing the line of fire)
  local bx, _, bz = bond.pos(0)
  local gx, _, gz = pads.pos(PSHOOT)
  local shooter = spawn.chr(PSHOOT, { behaviour = "snipe", weapon = "kf7", vision = 20000, face = { bx, bz } })
  aim = { gx, gz }
  local t0 = 0
  while t0 < 15 and select(1, test.fire_stats(shooter or -1)) == 0 do wait(0.25); t0 = t0 + 0.25 end
  wait(1)
  local f0, h0 = test.fire_stats(shooter or -1)
  check("hold fire: with a clear line the guard shoots at Bond", shooter ~= nil and f0 > 0 and h0 == 0, string.format("fired %d, held %d (first shot after %.1f s)", f0, h0, t0))
  -- then a civilian steps beside the line, 70 units out from the guard and 25 off it: it does not block his sight of Bond (the stock
  -- sight test would stop him shooting at all), but a shot past it is the crossfire that killed the bot's staff
  if shooter then gx, gz = spawn.where(shooter) end
  local ll = dist2(bx, bz, gx, gz)
  local ux, uz = (bx - gx) / ll, (bz - gz) / ll
  local mx, mz = gx + ux * 70 + uz * 25, gz + uz * 70 - ux * 25
  local near, nd = nil, 1e9               -- the pad nearest the midpoint, then an offset onto the line
  -- (on the guard's floor: bank v2's roof (F3, y 1440) lies over the hall, and the nearest pad by x/z alone was up there, 1440 above the
  -- line of fire, which the hold test rightly ignores as another floor)
  local _, gy = pads.pos(PSHOOT)
  for _, p in ipairs(pads.far_from(mx, mz, 0, 600)) do
    local x, y, z = pads.pos(p)
    local d = dist2(x, z, mx, mz)
    if d < nd and math.abs(y - gy) < 150 then near, nd = p, d end
  end
  local shield
  if near then
    local nx, _, nz = pads.pos(near)
    shield = spawn.chr(near, { behaviour = "civilian", body = "Civilian_3", offset = { mx - nx, mz - nz } })
  end
  local sx, sz = 0, 0
  if shield then sx, sz = spawn.where(shield) end
  local killed0 = civilian.killed()
  local f0b, h0b = test.fire_stats(shooter or -1)
  wait(3)
  local f1, h1 = test.fire_stats(shooter or -1)
  check("hold fire: with a civilian by his line of fire to Bond the guard holds his shots", shooter ~= nil and shield ~= nil and f1 == f0b
        and h1 > h0b and civilian.killed() == killed0, string.format("fired %d, held %d in 3 s; guard %s (%.0f, %.0f), civilian %s %s (%.0f, %.0f), Bond (%.0f, %.0f), killed %d",
        f1 - f0b, h1 - h0b, tostring(shooter), gx, gz, tostring(shield), tostring(shield and civilian.state(shield)), sx, sz, bx, bz, civilian.killed() - killed0))
  if shield then spawn.remove(shield) end
  wait(4)
  local f2, h2 = test.fire_stats(shooter or -1)
  check("... and once the civilian is gone he shoots again", f2 > f1, string.format("fired %d more (held %d)", f2 - f1, h2 - h1))
  if shooter then spawn.remove(shooter) end
  mode.hold_fire(false)
  aim = nil
  pin = false
  wait(1)

  -- F. crowd of guards: many chrs in one stage
  local made = 0
  local hall = HALL_PADS   -- the banking hall's named and grid pads
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
