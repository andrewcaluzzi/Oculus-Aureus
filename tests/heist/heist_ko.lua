-- Heist knock-out test mode (P0 job 5) on Bunker 2. Prints "HEIST PASS|FAIL <name> ..." and "HEIST DONE n passed, m failed".
-- The mode name starts with "heist": 250 extra chr slots and slaps_to_ko defaults to 2.
-- Run: tests/heist/run_ko.sh (inside the build container, Xvfb on :99).
-- Slaps go through ko.test_slap, which calls the stock hit path (handles_shot_actors with ITEM_FIST, as Bond's punch does).
mode.keep_locks()
spawn.keep_bodies(true)

local pass, fail = 0, 0
local function check(name, ok, extra)
  if ok then pass = pass + 1 else fail = fail + 1 end
  print(string.format("HEIST %s %s %s", ok and "PASS" or "FAIL", name, extra or ""))
end

local ev = { ko = {}, woken = {}, searched = {}, body = {}, ckilled = {}, died = {}, alarm = {} }
on("chr_knocked_out", function(c, p) ev.ko[#ev.ko + 1] = { c, p }; print(string.format("HEIST event chr_knocked_out chr=%d player=%d", c, p)) end)
on("chr_woken", function(c, g) ev.woken[#ev.woken + 1] = { c, g }; print(string.format("HEIST event chr_woken chr=%d by=%d", c, g)) end)
on("chr_searched", function(c, p) ev.searched[#ev.searched + 1] = { c, p }; print(string.format("HEIST event chr_searched chr=%d player=%d", c, p)) end)
on("body_found", function(g, b, k) ev.body[#ev.body + 1] = { g, b, k }; print(string.format("HEIST event body_found guard=%d body=%d ko=%d", g, b, k)) end)
on("civilian_killed", function(c, p) ev.ckilled[#ev.ckilled + 1] = { c, p }; print(string.format("HEIST event civilian_killed chr=%d killer=%d", c, p)) end)
on("chr_died", function(v, k) ev.died[#ev.died + 1] = { v, k }; print(string.format("HEIST event chr_died victim=%d killer=%d", v, k)) end)
on("alarm", function(a) ev.alarm[#ev.alarm + 1] = a; print("HEIST event alarm " .. a) end)

local heal = false
local function wait(sec)
  local t = 0
  while t < sec do
    t = t + coroutine.yield()
    if heal then bond.heal(0, 1, 1) end
  end
end
local function dist2(x1, z1, x2, z2) return math.sqrt((x1 - x2) ^ 2 + (z1 - z2) ^ 2) end
local function died(chr) for _, d in ipairs(ev.died) do if d[1] == chr then return true end end return false end

local function body()
  wait(1)
  heal = true
  cctv.alarm_on_spot(false)   -- Bunker 2's cameras must not sound the alarm under the alarm.stop check
  check("slaps_to_ko defaults to 2 in a heist mode", mode.slaps_to_ko() == 2, "n=" .. tostring(mode.slaps_to_ko()))

  -- A. civilian: 1 slap is not a KO, 2 slaps are; a KO is not a death; it stays down; searching it drops its key; a gun still kills it
  bond.warp(0, 61)
  wait(1)
  local civ = spawn.chr(62, { behaviour = "civilian", body = "Civilian_1_Female" })
  local kh = spawn.key(civ, 0x80)
  wait(1)
  check("civilian spawned with a key", civ ~= nil and kh ~= nil and select(2, spawn.key_alive(kh)) == true)
  local hit1 = ko.test_slap(0, civ)
  wait(0.5)
  check("1 slap: counted, not KO", hit1 and ko.slaps(civ) == 1 and not ko.is(civ) and spawn.alive(civ) and #ev.ko == 0,
        string.format("hit=%s slaps=%d ko=%s", tostring(hit1), ko.slaps(civ), tostring(ko.is(civ))))
  wait(1)
  ko.test_slap(0, civ)
  wait(0.5)
  check("2 slaps: KO + chr_knocked_out(chr, player 0)", ko.is(civ) and #ev.ko == 1 and ev.ko[1][1] == civ and ev.ko[1][2] == 0,
        string.format("ko=%s events=%d", tostring(ko.is(civ)), #ev.ko))
  check("KO'd civilian reports knocked_out", civilian.state(civ) == "knocked_out", civilian.state(civ) or "nil")
  check("KO is not a death", spawn.alive(civ) and civilian.killed() == 0 and #ev.ckilled == 0 and not died(civ),
        string.format("alive=%s killed=%d ckilled_ev=%d", tostring(spawn.alive(civ)), civilian.killed(), #ev.ckilled))
  local down, total = ko.count()
  check("ko.count", down == 1 and total == 1, string.format("down=%d total=%d", down, total))
  local xs, zs = spawn.where(civ)
  wait(4.5)   -- the fall: 117 frames at half speed (~3.9 s); the animation carries the body ~100 units forward
  local x0, z0, a0 = spawn.where(civ)
  print(string.format("HEIST the fall carried the body %.0f units", dist2(xs, zs, x0, z0)))
  wait(6)
  local x1, z1, a1 = spawn.where(civ)
  check("KO'd chr stays down (6 s after the fall, same pose and spot)", ko.is(civ) and spawn.down(civ) and a1 == a0 and dist2(x0, z0, x1, z1) < 5 and civilian.killed() == 0,
        string.format("act %s -> %s moved %.0f", tostring(a0), tostring(a1), dist2(x0, z0, x1, z1)))
  ko.test_slap(0, civ)
  check("a slap on a KO'd chr does nothing more", ko.is(civ) and #ev.ko == 1)
  local _, held = spawn.key_alive(kh)
  check("its key stays on it", held == true)
  check("search out of reach finds nothing", ko.search(0) == nil)
  bond.warp(0, 62, -60, 0)
  wait(0.5)
  local bx, _, bz = bond.pos(0)
  print(string.format("HEIST Bond %.0f from the KO'd civilian", dist2(bx, bz, x1, z1)))
  local s = ko.search(0)
  wait(0.5)
  local alive2, held2 = spawn.key_alive(kh)
  check("search drops its key (chr_searched)", s == civ and alive2 and not held2 and #ev.searched == 1, string.format("s=%s held=%s", tostring(s), tostring(held2)))
  spawn.kill(civ)
  wait(1.5)
  check("killing a KO'd civilian is a death", civilian.killed() == 1 and #ev.ckilled == 1 and not ko.is(civ), string.format("killed=%d", civilian.killed()))

  -- B. slaps_to_ko(0): slaps are stock hits again
  mode.slaps_to_ko(0)
  local g0 = spawn.chr(56, { behaviour = "hold", weapon = "none", health = 50 })
  wait(1)
  ko.test_slap(0, g0)
  ko.test_slap(0, g0)
  wait(0.5)
  check("slaps_to_ko(0): no counting, no KO", ko.slaps(g0) == 0 and not ko.is(g0))
  spawn.remove(g0)
  mode.slaps_to_ko(2)
  wait(1)

  -- C. a guard KO'd; with guards_wake off a witness finds it (body_found ko=1) and it stays down
  bond.warp(0, 50)
  wait(1)
  mode.guards_wake(false)
  local v1 = spawn.chr(58, { behaviour = "hold", weapon = "none" })
  wait(1)
  ko.test_slap(0, v1); wait(0.25); ko.test_slap(0, v1)
  wait(2)
  check("guard KO'd", ko.is(v1) and spawn.down(v1))
  local b0 = #ev.body
  local w1 = spawn.chr(59, { behaviour = "hold", weapon = "none" })
  wait(3)
  local e = ev.body[b0 + 1]
  check("witness fires body_found(guard, body, ko=1) for the KO'd chr", e ~= nil and e[1] == w1 and e[2] == v1 and e[3] == 1,
        e and string.format("guard=%d body=%d ko=%d", e[1], e[2], e[3]) or "none")
  check("guards_wake(false): nobody comes, it stays down", ko.waker(v1) == nil and ko.is(v1))
  spawn.remove(w1)
  spawn.remove(v1)
  wait(1)

  -- D. stretch: guards_wake on (default): the witness walks over and wakes it
  mode.guards_wake(true)
  local v2 = spawn.chr(58, { behaviour = "hold", weapon = "none" })
  wait(1)
  ko.test_slap(0, v2); wait(0.25); ko.test_slap(0, v2)
  wait(2)
  local vx, vz = spawn.where(v2)
  b0 = #ev.body
  local w0 = #ev.woken
  local w2 = spawn.chr(59, { behaviour = "hold", weapon = "none" })
  local wx0, wz0 = spawn.where(w2)
  wait(1.5)
  check("waker assigned", ko.waker(v2) == w2, "waker=" .. tostring(ko.waker(v2)))
  local t = 0
  while t < 20 and #ev.woken == w0 do wait(0.5); t = t + 0.5 end
  local wx1, wz1 = spawn.where(w2)
  print(string.format("HEIST waker %.0f -> %.0f from the body after %.1fs", dist2(wx0, wz0, vx, vz), dist2(wx1, wz1, vx, vz), t))
  local we = ev.woken[w0 + 1]
  check("guard walked over and woke it (chr_woken)", we ~= nil and we[1] == v2 and we[2] == w2 and not ko.is(v2) and spawn.alive(v2),
        we and string.format("chr=%d by=%d", we[1], we[2]) or "none")
  local _, _, woken = ko.count()
  check("ko.count counts the wake", woken == 1)
  wait(2)
  local ux, uz, act = spawn.where(v2)
  check("woken chr is up and not dead", spawn.alive(v2) and not ko.is(v2) and not spawn.down(v2), "act=" .. tostring(act))
  check("woken chr goes on alert (on the move)", act ~= 3 and (act == 13 or act == 15 or act == 8 or act == 9 or dist2(ux, uz, vx, vz) > 50),
        string.format("act=%s moved %.0f", tostring(act), dist2(ux, uz, vx, vz)))
  check("still no civilian or KO deaths", civilian.killed() == 1 and not died(v2))
  -- the woken guard can be knocked out again
  ko.test_slap(0, v2); wait(0.25); ko.test_slap(0, v2)
  wait(0.5)
  check("knocked out again after waking", ko.is(v2) and select(2, ko.count()) == 4)
  spawn.remove(v2)
  spawn.remove(w2)
  wait(1)

  -- E. alarm.stop sticks while a raiser's alarm animation is still playing
  alarm.stop(); wait(1)
  local best, bestd = 55, 1e9
  local p54x, _, p54z = pads.pos(54)
  for _, c in ipairs({ 55, 58, 59, 60, 61, 62, 63, 64, 65 }) do
    local cx, _, cz = pads.pos(c)
    local dd = dist2(p54x, p54z, cx, cz)
    if dd >= 300 and dd < 600 and dd < bestd then best, bestd = c, dd end
  end
  local g = spawn.chr(54, { behaviour = "hold", weapon = "none", alarm_pad = best, vision = 20000 })
  wait(1)
  bond.warp(0, 54, -200, 0); wait(0.5)
  bond.test_noise(0, 3.0)
  t = 0
  while t < 25 and not alarm.active() do wait(0.25); t = t + 0.25 end
  check("raiser sounded the alarm", alarm.active(), string.format("after %.1fs", t))
  alarm.stop()
  local back = false
  for _ = 1, 16 do
    wait(0.25)
    if alarm.active() then back = true end
  end
  check("alarm.stop() sticks while the raiser is still in its animation", not back and spawn.alive(g), "events=" .. table.concat(ev.alarm, ","))
  spawn.remove(g)

  print(string.format("HEIST DONE %d passed, %d failed", pass, fail))
  if fail == 0 then mission.win("heist ko ok") else mission.fail("heist ko failed") end
end

local co = coroutine.wrap(body)
local finished = false
on("tick", function(dt)
  if finished then return end
  local ok, err = pcall(co, dt)
  if not ok then finished = true; print("HEIST FAIL script error: " .. tostring(err)) end
end)
