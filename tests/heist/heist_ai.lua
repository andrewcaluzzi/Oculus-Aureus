-- Guard hearing, awareness and the cameras' acts and bodies (designs/bank-heist.md section 18), on the Janus bank with the real
-- tools/mode_heist.lua. One scenario per run (tests/heist/run_ai.sh):
--   GE_AI_HEAR=1     a slap 300 units behind a guard's back (the hall, emptied): he hears it, turns to it and walks over to look
--   GE_AI_SLAP=1     a real slap (the stock hit path) on a teller 300 behind a guard, Bond out of sight: heard, turned, investigated
--   GE_AI_DOOR=1     the shut BLUE door between the hall and the security office: a slap 600 (and 300) away through it is not heard;
--                    with the door open, 300 is
--   GE_AI_GIVEUP=1   a searching guard looks round, gives up ("must be nothing") and walks back to his post
--   GE_AI_CAMKO=1    a knocked-out teller in the hall camera's view: the camera reports it, the alarm within security_delay
--   GE_AI_CAMACT=1   a slap in a camera's view (holstered, so the camera would not time him) is reported at once (an act)
--   GE_AI_CAMDOWN=1  the same body with every security guard knocked out: nothing
--   GE_AI_PATROL=1   the patrol loops: each route's guard walks it (moves, and comes back round)
--   GE_AI_CHECKIN=1  a guard knocked out where nobody sees: he misses his check-in and a colleague is sent to his post
--   GE_AI_BACKUP=1   a backup call: a key-card carrier nearer the caller holds his post, a posted guard answers
--   GE_AI_LURE=1     a coin 600 behind a guard draws him over; one at the edge of his hearing (860) only turns him
--   GE_AI_CAMWARN=1  Bond at an unsearched body in a camera's view: the AR's red CAMERA warning; cameras off: none
-- GE_AI_SHOTS=1 adds screenshots (cctv.test_shot). Prints "HEIST PASS|FAIL ..." and "HEIST DONE".
local RUN = "hear"
for _, r in ipairs({ "slap", "door", "giveup", "camko", "camact", "camdown", "checkin", "patrol", "backup", "lure", "camwarn" }) do
  if mode.env_flag("GE_AI_" .. r:upper()) then RUN = r end
end
local SHOTS = mode.env_flag("GE_AI_SHOTS")
HEIST_SETTINGS = {
  test = true, station_late = (RUN == "backup" or RUN == "lure" or RUN == "camwarn") and 900 or 8, jam_chance = 0, jam_every = 1000, security_delay = 4, investigators = 0, generator_after = 0,
  ai_tune = { search_time = (RUN == "giveup") and 14 or 30, look = 1.5, search_pads = 1, checkin = (RUN == "checkin") and 6 or 0 },
}
include("heist")
mods.set("invincible", true)
on("stage_start", function() mode.guards_wake(false) end)

local pass, fail = 0, 0
local function check(name, ok, extra)
  if ok then pass = pass + 1 else fail = fail + 1 end
  print(string.format("HEIST %s %s %s", ok and "PASS" or "FAIL", name, extra or ""))
end
local real_fail = mission.fail
mission.win = function() return true end
mission.fail = function() return true end

local function wait(sec)
  local t = 0
  while t < sec do t = t + coroutine.yield(); bond.heal(0, 1, 1) end
end
local function wait_for(sec, fn)
  local t = 0
  while t < sec and not fn() do wait(0.25); t = t + 0.25 end
  return fn(), t
end
local function d2(ax, az, bx, bz) return math.sqrt((ax - bx) ^ 2 + (az - bz) ^ 2) end
local function pad(n) return LEVEL.pads[n] end
local function shot(name)
  if not SHOTS then return end
  print("HEIST SHOT " .. name)
  cctv.test_shot()
  wait(0.4)
end
local barks = {}
on("guard_bark", function(c, kind, text) barks[#barks + 1] = { c = c, kind = kind, text = text, t = HEIST.elapsed } end)
local noises = {}
on("noise", function(x, y, z, r, kind, src) noises[#noises + 1] = { x = x, z = z, r = r, kind = kind, src = src } end)
local function barked(c, kind) for _, b in ipairs(barks) do if b.c == c and (not kind or b.kind == kind) then return b end end end

-- the nearest pad to (x, z) at floor height y (+-100)
local function pad_near(x, y, z)
  local best, bd = nil, 1e9
  for _, p in ipairs(pads.far_from(x, z, 0, 600)) do
    local px, py, pz = pads.pos(p)
    local d = d2(px, pz, x, z)
    if d < bd and math.abs(py - y) < 100 then best, bd = p, d end
  end
  return best
end
local function warp_xz(p, x, y, z)
  local best = pad_near(x, y, z)
  if not best then return false end
  local px, _, pz = pads.pos(best)
  return bond.warp(p, best, x - px, z - pz)
end
-- a test guard standing at (x, z) facing (fx, fz), tracked by the awareness layer (a hall guard by role)
local function guard_at(x, y, z, fx, fz)
  local best = pad_near(x, y, z)
  if not best then return nil end
  local px, _, pz = pads.pos(best)
  local c = spawn.chr(best, { post = { fx, fz }, weapon = "kf7", body = "St_Petersburg_Guard", offset = { x - px, z - pz }, hearing = 1.0 })
  if not c then return nil end
  spawn.role(c, "hall")
  cover.guard(c, "guard")   -- he judges the disguise like the bank's own (a holstered Bond in the street is staff)
  ai.guard(c, 1)
  return c
end
-- spawn.chr on a named pad, or a little off it when the pad's spot is taken
local function spawn_near(pname, opts)
  for _, o in ipairs({ { 0, 0 }, { 0, -60 }, { 60, 0 }, { -60, 0 }, { 0, 60 }, { 0, -110 }, { 110, 0 }, { -110, 0 } }) do
    local oo = {}
    for k, v in pairs(opts) do oo[k] = v end
    if o[1] ~= 0 or o[2] ~= 0 then oo.offset = o end
    local c = spawn.chr(pad(pname), oo)
    if c then return c end
  end
end
local function clear_area(area)
  for _, g in ipairs(HEIST.guards) do if g.area == area then spawn.remove(g.chr) end end
  for _, c in ipairs(HEIST.civs) do if c.area == area then spawn.remove(c.chr) end end
end
local function angdiff(a, b) local d = (a - b) % 360; if d > 180 then d = d - 360 end; return math.abs(d) end
local function heading(fx, fz, tx, tz) return math.deg(math.atan(tx - fx, tz - fz)) end
local function info(c) return ai.info(c) or { state = -1, name = "?", sub = "?", level = 0, face = 0 } end
local function away() bond.warp(0, pad("street_e") or HEIST.M.P.start[1]) end   -- the street: out of every guard's and camera's sight

-- a guard in the emptied hall facing north; a noise (or a slap on a teller) 300 behind him
local function hall_guard()
  clear_area("hall")
  mode.body_check(false)
  away()
  local x, y, z = pads.pos(pad("hall_mid"))
  wait(0.5)
  local c = guard_at(x, y, z, x, z + 500)
  wait(1.5)
  return c, x, y, z
end

local function investigates(c, nx, nz, label)
  local i0 = info(c)
  local turned = wait_for(2.5, function() local i = info(c); return angdiff(i.face, heading(spawn.where(c), select(2, spawn.where(c)), nx, nz)) < 35 end)
  local gx, gz = spawn.where(c)
  local i = info(c)
  check(label .. ": heard (awareness up)", i.state >= 1, string.format("%s %s level %.2f", i.name, i.sub, i.level))
  check(label .. ": he turns to the noise (regardless of facing)", turned,
        string.format("face %.0f, to the noise %.0f", i.face, heading(gx, gz, nx, nz)))
  if SHOTS then warp_xz(0, gx + 380, select(2, pads.pos(pad("hall_mid"))), gz + 120); bond.face(0, gx, gz); wait(0.4); shot("ai_turn") end
  local went = wait_for(14, function() local x, z = spawn.where(c); return x and d2(x, z, nx, nz) < 160 end)
  local x, z = spawn.where(c)
  i = info(c)
  check(label .. ": searching, he walks over to the noise", went and i.state >= 2,
        string.format("%.0f from it, %s %s", x and d2(x, z, nx, nz) or -1, i.name, i.sub))
  if SHOTS then bond.face(0, x, z); wait(0.6); shot("ai_search") end
  check(label .. ": barks (huh / hey / investigate)", barked(c) ~= nil, barked(c) and (barked(c).kind .. ": " .. barked(c).text) or "none")
  away()
end

local function hear_path()
  local c, x, y, z = hall_guard()
  check("a test guard on post", c ~= nil and info(c).state == 0, c and info(c).name or "none")
  if not c then return end
  local nx, nz = x, z - 300
  local heard = noise.emit(nx, y, nz, noise.radius("slap"), "slap")
  check("the slap 300 behind him is heard", heard >= 1 and #noises >= 1 and noises[#noises].kind == "slap",
        string.format("%d guard(s) heard it, radius %.0f", heard, noise.radius("slap")))
  investigates(c, nx, nz, "slap behind his back")
end

local function slap_path()
  local c, x, y, z = hall_guard()
  if not c then check("a test guard", false); return end
  local px = pad_near(x, y, z - 300)
  local px_, _, pz_ = pads.pos(px)
  local v = spawn.chr(px, { behaviour = "civilian", body = "Civilian_1_Female", offset = { x - px_, z - 300 - pz_ } })
  check("a teller 300 behind him", v ~= nil)
  if not v then return end
  wait(1)
  local vx, vz = spawn.where(v)
  ko.test_slap(0, v)   -- Bond is out in the street: the guard hears it, nobody sees who
  local n = noises[#noises]
  check("the slap is a noise where she stands", n ~= nil and n.kind == "slap" and d2(n.x, n.z, vx, vz) < 30, n and n.kind or "none")
  investigates(c, vx, vz, "a real slap")
end

local function door_path()
  mode.body_check(false)
  away()
  local O = LEVEL.objects.blue_hall
  local da, db = door.get(O.index), door.get(O.pair)
  -- the doorway: the two leaves' middle; u = the unit step into the security office (the wall's normal, toward the room's middle)
  local D = { x = (da.x + db.x) / 2, z = (da.z + db.z) / 2 }
  local rr = LEVEL.rooms.sec_office.rect
  local cx, cz = (rr[1] + rr[3]) / 2 - D.x, (rr[2] + rr[4]) / 2 - D.z
  local ux, uz = 0, 0
  if math.abs(cx) > math.abs(cz) then ux = cx > 0 and 1 or -1 else uz = cz > 0 and 1 or -1 end
  check("the BLUE door is shut", door.get(O.index).open < 0.05 and door.get(O.pair).open < 0.05)
  local y = select(2, pads.pos(pad("sec_mid")))
  local c = guard_at(D.x + ux * 300, y, D.z + uz * 300, D.x + ux * 800, D.z + uz * 800)
  wait(1.5)
  check("a test guard in the security office, 300 inside the door, his back to it", c ~= nil, tostring(c))
  if not c then return end
  local rh, rs = LEVEL.rooms.hall.id, LEVEL.rooms.sec_office.id
  print(string.format("HEIST   factor hall -> security office %.2f (shut door), slap radius %.0f", noise.factor(rh, rs), noise.radius("slap")))
  noise.emit(D.x - ux * 300, y, D.z - uz * 300, noise.radius("slap"), "slap")
  wait(1)
  local i = info(c)
  check("a slap 600 away through the shut door is not heard", i.state == 0 and i.heard_ago < 0, string.format("%s, heard %.1f s ago", i.name, i.heard_ago))
  noise.emit(D.x - ux, y, D.z - uz, noise.radius("slap"), "slap")   -- right at the door, 300 from him
  wait(1)
  i = info(c)
  check("... nor 300 away, right behind the shut door", i.state == 0 and i.heard_ago < 0, string.format("%s, heard %.1f s ago", i.name, i.heard_ago))
  door.open(O.index); door.open(O.pair)
  wait_for(3, function() return door.get(O.index).open > 0.9 end)
  print(string.format("HEIST   factor with the door open %.2f", noise.factor(rh, rs)))
  noise.emit(D.x - ux, y, D.z - uz, noise.radius("slap"), "slap")
  wait(0.5)
  i = info(c)
  check("with the door open, 300 away: heard", i.state >= 1 and i.heard_ago >= 0, string.format("%s %s", i.name, i.sub))
end

local function giveup_path()
  local c, x, y, z = hall_guard()
  if not c then check("a test guard", false); return end
  noise.emit(x + 250, y, z - 150, noise.radius("slap"), "slap")
  local s = wait_for(4, function() return info(c).state >= 2 end)
  check("searching", s, info(c).name)
  local t0 = HEIST.elapsed
  local gave = wait_for(25, function() return barked(c, "lost") ~= nil end)
  local i = info(c)
  check("he gives up (must be nothing)", gave and i.state == 0, string.format("%s %s after %.1f s: %s", i.name, i.sub, HEIST.elapsed - t0,
        barked(c, "lost") and barked(c, "lost").text or "-"))
  local back = wait_for(20, function()
    local gx, gz = spawn.where(c)
    print(string.format("HEIST   (returning: at %.0f,%.0f, %s)", gx or -1, gz or -1, info(c).sub))
    return gx and d2(gx, gz, x, z) < 120 and info(c).sub == "none" end)
  wait(2.5)   -- (the turn back to his facing)
  local gx, gz = spawn.where(c)
  i = info(c)
  check("... and walks back to his post and his facing", back and angdiff(i.face, 0) < 40,
        string.format("%.0f from the post, %s, face %.0f", gx and d2(gx, gz, x, z) or -1, i.sub, i.face))
end

local function cam_body(down)
  clear_area("hall")   -- (body_check stays on: with it off nobody, cameras included, finds a body; the hall is empty of guards)
  away()
  local cam = HEIST.M.P.feed_cam
  local spotted = {}
  on("cctv_spotted", function(c, p) spotted[#spotted + 1] = { cam = c, p = p, t = HEIST.elapsed } end)
  if down then
    for _, g in ipairs(HEIST.guards) do
      if g.area == "security" and (g.kind == "post" or g.kind == "raiser") and spawn.alive(g.chr) then spawn.ko(g.chr, 0) end
    end
    wait(1.5)
    check("every security guard knocked out", HEIST.security_up() == 0, tostring(HEIST.security_up()))
  end
  local n0 = #spotted
  local v = spawn_near("cust_1", { behaviour = "civilian", body = "Civilian_2" })
  wait(1)
  check("a teller where the hall camera looks", v ~= nil)
  if not v then return end
  spawn.ko(v, 0)
  local t0 = HEIST.elapsed
  local sp = wait_for(3, function() for k = n0 + 1, #spotted do if spotted[k].p == -1 then return true end end end)
  check("the camera sees the body and reports it", sp, string.format("%d report(s) after %.1f s", #spotted - n0, HEIST.elapsed - t0))
  if down then
    wait(6)
    check("security staff down: no alarm", not HEIST.hot and not alarm.active(), "hot " .. tostring(HEIST.hot))
  else
    local hot = wait_for(HEIST_SETTINGS.security_delay + 2, function() return HEIST.hot end)
    check("the security room raises the alarm within security_delay", hot and HEIST.elapsed - t0 <= HEIST_SETTINGS.security_delay + 1.5,
          string.format("hot %s after %.1f s", tostring(hot), HEIST.elapsed - t0))
  end
end

local function camact_path()
  -- 1. an act: a slap in the camera's view is reported at once (holstered: the camera would not time him)
  mode.body_check(false)
  clear_area("hall")
  local spotted = {}
  on("cctv_spotted", function(c, p) spotted[#spotted + 1] = { cam = c, p = p, t = HEIST.elapsed } end)
  local v = spawn_near("cust_1", { behaviour = "civilian", body = "Civilian_3" })
  check("a teller at the counter", v ~= nil)
  if not v then return end
  wait(1)
  local vx, vz = spawn.where(v)
  local _, vy = pads.pos(pad("cust_1"))
  warp_xz(0, vx + 60, vy, vz)
  bond.face(0, vx, vz)
  wait(1.5)
  check("holstered at the counter: the camera reports nothing", #spotted == 0, tostring(#spotted))
  ko.test_slap(0, v)
  local sp = wait_for(1.5, function() return #spotted > 0 end)
  check("a slap in the camera's view is reported at once (an act)", sp and spotted[1].p == 0, sp and ("camera " .. spotted[1].cam) or "none")
  local hot = wait_for(HEIST_SETTINGS.security_delay + 2, function() return HEIST.hot end)
  check("... and the security room raises the alarm", hot)
end

local function checkin_path()
  mode.body_check(false)
  away()
  local g
  for _, gg in ipairs(HEIST.guards) do if gg.kind == "post" and gg.area == "records" and spawn.alive(gg.chr) then g = gg.chr; break end end
  if not g then for _, gg in ipairs(HEIST.guards) do if gg.kind == "post" and spawn.alive(gg.chr) then g = gg.chr; break end end end
  check("a posted guard", g ~= nil)
  if not g then return end
  spawn.ko(g, 0)
  local gx, gz = spawn.where(g)
  local sent = wait_for(12, function() for _, b in ipairs(barks) do if b.kind == "radio" and b.text:find("answering") then return b end end end)
  local b
  for _, bb in ipairs(barks) do if bb.kind == "radio" and bb.text:find("answering") then b = bb end end
  check("he misses his check-in: a colleague is sent", sent and b ~= nil, b and string.format("chr %d: %s", b.c, b.text) or "none")
  if not b then return end
  local there = wait_for(40, function() local x, z = spawn.where(b.c); return x and d2(x, z, gx, gz) < 250 end)
  local x, z = spawn.where(b.c)
  check("the colleague walks to his post", there, string.format("%.0f from it", x and d2(x, z, gx, gz) or -1))
end

local function patrol_path()
  away()
  local list = {}
  for _, g in ipairs(HEIST.guards) do if g.route then list[#list + 1] = g end end
  check("patrol routes handed out", #list >= 3, tostring(#list))
  local start, far = {}, {}
  for _, g in ipairs(list) do local x, z = spawn.where(g.chr); start[g.chr] = { x, z }; far[g.chr] = 0 end
  for _ = 1, 100 do
    wait(0.25)
    for _, g in ipairs(list) do
      local x, z = spawn.where(g.chr)
      if x then far[g.chr] = math.max(far[g.chr], d2(x, z, start[g.chr][1], start[g.chr][2])) end
    end
  end
  local moved = 0
  for _, g in ipairs(list) do
    print(string.format("HEIST   route %s: guard %d went %.0f from where he started (%s)", g.route, g.chr, far[g.chr], info(g.chr).sub))
    if far[g.chr] > 300 then moved = moved + 1 end
  end
  check("the patrol guards walk their loops (most of them over 3 m in 25 s)", moved >= math.max(1, #list - 1), string.format("%d of %d", moved, #list))

  -- the watchclock (bank v3's M.STATIONS; bank v2 has none, so the test gives the hall tour one): its guard knocked out misses it
  local tour
  for _, t in ipairs(HEIST_AI.tours or {}) do if t.route == "hall" or (not tour and spawn.alive(t.chr)) then tour = t end end
  check("a tour to put a watchclock station on", tour ~= nil)
  if not tour then return end
  tour.stations = { pad("hall_post_sw") or pad("hall_mid") }
  tour.punched, tour.missed = HEIST.elapsed, false
  mode.body_check(false)   -- (nobody finds the body first: a camera or a guard would raise the alarm, and a hot heist has no rounds)
  spawn.ko(tour.chr, 0)
  local b0 = #barks
  local sent = wait_for(HEIST_SETTINGS.station_late + 4, function()
    for k = b0 + 1, #barks do if barks[k].text:find("missed a station") then return true end end end)
  check("he misses his station: a colleague is sent to it", sent and tour.missed, tostring(tour.missed))
end

-- the AR camera warning (tools/heist_ar.lua): Bond next to an unsearched body in a live camera's view gets the red CAMERA diamond;
-- with the cameras off, none
local function camwarn_path()
  mode.body_check(false)   -- (the camera would report the body itself before Bond gets there)
  clear_area("hall")
  local v = spawn_near("cust_1", { behaviour = "civilian", body = "Civilian_3" })
  check("a teller at the counter", v ~= nil)
  if not v then return end
  check("the AR layer is loaded", heist_ar ~= nil and heist_ar.warn ~= nil)
  if not heist_ar then return end
  wait(1)
  spawn.ko(v, 0)
  wait(1)
  check("away from her: no warning", next(heist_ar.warn) == nil)
  local vx, vz = spawn.where(v)
  local _, vy = pads.pos(pad("cust_1"))
  warp_xz(0, vx + 60, vy, vz)
  bond.face(0, vx, vz)
  local w = wait_for(2, function() return next(heist_ar.warn) ~= nil end)
  check("at the body in the camera's view: the CAMERA warning", w, tostring(next(heist_ar.warn)))
  shot("ai_camwarn")
  for _, cam in ipairs(cctv.list()) do cctv.disable(cam) end
  local gone = wait_for(2, function() return next(heist_ar.warn) == nil end)
  check("the cameras off: the warning goes", gone)
  for _, cam in ipairs(cctv.list()) do cctv.enable(cam) end
end

-- backup: a guard calls it (ai.backup, as when he goes into combat); a key-card carrier nearer than the post guard holds his post,
-- the post guard answers. The carrier himself may still call it.
local function backup_path()
  local a, x, y, z = hall_guard()
  if not a then check("a test guard", false); return end
  local b = guard_at(x + 200, y, z, x + 200, z + 500)
  local c = guard_at(x - 350, y, z, x - 350, z + 500)
  check("a caller, a card carrier 200 from him and a posted guard 350 from him", b ~= nil and c ~= nil)
  if not (b and c) then return end
  ai.guard(b, 3)   -- (kind 3: a key-card carrier, as mode_heist.lua registers them)
  wait(1)
  local bx, bz = spawn.where(b)
  local cx, cz = spawn.where(c)
  local n = ai.backup(a)
  check("the caller calls backup and someone answers", n >= 1 and barked(a, "backup") ~= nil, tostring(n) .. " answered")
  check("the posted guard answers (alert, on his way)", info(c).state >= 3 and info(c).sub == "go",
        string.format("%s %s, %s", info(c).name, info(c).sub, barked(c) and barked(c).text or "no bark"))
  check("the card carrier does not answer", barked(b) == nil and info(b).state < 3, string.format("%s %s, %s", info(b).name, info(b).sub,
        barked(b) and barked(b).text or "no bark"))
  wait(4)
  local bx2, bz2 = spawn.where(b)
  local cx2, cz2 = spawn.where(c)
  check("... he holds his post", bx2 and d2(bx, bz, bx2, bz2) < 40 and info(b).sub ~= "go",
        string.format("moved %.0f, %s %s", bx2 and d2(bx, bz, bx2, bz2) or -1, info(b).name, info(b).sub))
  check("... while the posted guard moves up", cx2 and d2(cx, cz, cx2, cz2) > 100, string.format("moved %.0f", cx2 and d2(cx, cz, cx2, cz2) or -1))
  check("the carrier can still call backup himself", ai.backup(b) >= 0 and barked(b, "backup") ~= nil)
end

-- a coin is a lure: inside `lure` of his hearing range (900 x hearing x rooms) a guard walks over; further out he only turns and looks
local function lure_path()
  local c, x, y, z = hall_guard()
  if not c then check("a test guard", false); return end
  -- the longest clear run inside the hall from hall_mid: the coin goes that way, behind his back
  local rr = LEVEL.rooms.hall.rect
  local dirs = { { 1, 0, rr[3] - x }, { -1, 0, x - rr[1] }, { 0, 1, rr[4] - z }, { 0, -1, z - rr[2] } }
  table.sort(dirs, function(p, q) return p[3] > q[3] end)
  local ux, uz, room = dirs[1][1], dirs[1][2], dirs[1][3]
  print(string.format("HEIST   the hall runs %.0f from hall_mid along %d,%d", room, ux, uz))
  spawn.remove(c)
  wait(0.5)
  -- a guard on a hall pad about 6 m from hall_mid (in the hall itself), his back to it; the coin lands at hall_mid
  local hall = LEVEL.rooms.hall.id
  c = nil
  for _, p in ipairs(pads.far_from(x, z, 560, 640)) do
    local px, py, pz = pads.pos(p)
    if math.abs(py - y) < 60 then
      local g = guard_at(px, py, pz, px + (px - x), pz + (pz - z))
      if g then
        wait(0.25)
        if spawn.room(g) == hall then c = g; break end
        spawn.remove(g)
      end
    end
  end
  check("a guard in the hall about 6 m from hall_mid", c ~= nil)
  if not c then return end
  wait(1.5)
  local nx, nz = x, z
  local gx0, gz0 = spawn.where(c)
  local d = d2(gx0, gz0, nx, nz)
  local heard = noise.emit(nx, y, nz, 900, "coin")
  check("a coin " .. math.floor(d) .. " behind him is heard", heard >= 1, tostring(heard))
  local went = wait_for(14, function() local gx, gz = spawn.where(c); return gx and d2(gx, gz, nx, nz) < 160 end)
  local gx, gz = spawn.where(c)
  check("... and he walks over to it (a lure)", went, string.format("%.0f from the coin, %s %s", gx and d2(gx, gz, nx, nz) or -1, info(c).name, info(c).sub))
  spawn.remove(c)
  wait(0.5)
  c = guard_at(x, y, z, x - ux * 500, z - uz * 500)
  wait(1.5)
  local far = 860
  local fx, fz = x + ux * far, z + uz * far
  heard = noise.emit(fx, y, fz, 900, "coin")
  local turned = wait_for(2.5, function() return info(c).state >= 1 end)
  check("a faint coin (860 away) at the edge of his hearing: he turns (suspicious)", heard >= 1 and turned,
        string.format("heard %d, %s %s level %.2f", heard, info(c).name, info(c).sub, info(c).level))
  wait(6)
  gx, gz = spawn.where(c)
  check("... but stays where he is (looks from there)", gx and d2(gx, gz, x, z) < 100,
        string.format("%.0f from his post, %s", gx and d2(gx, gz, x, z) or -1, info(c).sub))
end

local finished = false
local function body()
  local ready = wait_for(25, function() return HEIST.ready end)
  check("mode ready", ready, "run " .. RUN)
  check("the awareness layer is on", ai.on() and noise ~= nil)
  wait(1.5)
  if RUN == "hear" then hear_path() elseif RUN == "slap" then slap_path() elseif RUN == "door" then door_path()
  elseif RUN == "giveup" then giveup_path() elseif RUN == "camko" then cam_body(false) elseif RUN == "camact" then camact_path()
  elseif RUN == "camdown" then cam_body(true)
  elseif RUN == "checkin" then checkin_path() elseif RUN == "patrol" then patrol_path()
  elseif RUN == "backup" then backup_path() elseif RUN == "lure" then lure_path() elseif RUN == "camwarn" then camwarn_path() end
  local c = ai.counts()
  print(string.format("HEIST   counts: barks %d searches %d giveups %d checkins %d backups %d cam acts %d cam bodies %d",
        c.barks, c.searches, c.giveups, c.checkins, c.backups, c.cam_acts, c.cam_bodies))
  print(string.format("HEIST DONE %d passed, %d failed", pass, fail))
  finished = true
  real_fail("ai test done")
end

local co = coroutine.wrap(body)
on("tick", function(dt)
  if finished then return end
  local ok, err = pcall(co, dt)
  if not ok then finished = true; print("HEIST FAIL script error: " .. tostring(err)); print(string.format("HEIST DONE %d passed, %d failed", pass, fail + 1)) end
end)
