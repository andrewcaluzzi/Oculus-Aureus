-- The banker cover, the security room's camera link and the power cut (designs/bank-heist.md section 16), on the Janus bank with the
-- real tools/mode_heist.lua. One scenario per run (tests/heist/run_cover.sh runs them, in parallel containers if asked):
--   GE_COVER_HALL=1      disguised (slappers in hand), Bond walks the banking hall past its guards: no "Hey!", no alarm; his speed is logged
--   GE_COVER_GUN=1       a gun drawn in a guard's sight: that guard (alone) sees through him; still up after the radio time: the alarm
--   GE_COVER_LOOT=1      a gold bar picked up in a guard's sight: blown at once (loot)
--   GE_COVER_RESTRICTED=1 seen in the security wing: blown within about a second (restricted)
--   GE_COVER_STAFF=1     seen in the offices: the meter builds over seconds, then "Hey!" (staff)
--   GE_COVER_KO=1        a witness knocked out before his radio time: the cover holds, no alarm
--   GE_COVER_CAM=1       a camera times an armed Bond (the hall's guards removed): it reports to the security room, the alarm a delay later
--   GE_COVER_CAMDOWN=1   the same with every security guard knocked out: the camera still sees, nobody raises the alarm
--   GE_COVER_POWER=1     the full hold at the breaker (cabinet, then the main): no alarm can be raised, the cameras are dark (feed static);
--                        the investigators come and one at the breaker puts the power back
--   GE_COVER_INTERRUPT=1 the hold interrupted (Bond walks off): no cut; he comes back and finishes from the saved progress less the setback
--   GE_COVER_GENERATOR=1 the backup generator restores the power after its delay; disabled first, it does not
--   GE_COVER_DOORS=1     the guard door rules: noise inside the vault brings guards to door A and it stays shut; a hall guard cannot open
--                        the BLUE door, a security guard can; a barracks door opens for its own squad
-- GE_COVER_SHOTS=1 adds a few screenshots (cctv.test_shot: "HEIST SHOT <name>" lines). Prints "HEIST PASS|FAIL ..." and "HEIST DONE".
local RUN = "hall"
for _, r in ipairs({ "gun", "loot", "restricted", "staff", "ko", "cam", "camdown", "power", "interrupt", "generator", "doors" }) do
  if mode.env_flag("GE_COVER_" .. r:upper()) then RUN = r end
end
local SHOTS = mode.env_flag("GE_COVER_SHOTS")
HEIST_SETTINGS = {
  test = true, jam_chance = 0, jam_every = 1000,
  security_delay = 4,
  power_cabinet = 2, power_cut = 6, power_setback = 2, repair_time = 4,
  investigate_after = 2, investigators = (RUN == "power") and 1 or 0,
  generator_after = (RUN == "generator") and 8 or 0, generator_time = 3,
}
include("heist")
mods.set("invincible", true)
on("stage_start", function() mode.guards_wake(false) end)

local pass, fail = 0, 0
local function check(name, ok, extra)
  if ok then pass = pass + 1 else fail = fail + 1 end
  print(string.format("HEIST %s %s %s", ok and "PASS" or "FAIL", name, extra or ""))
end
local ended = {}
local real_win, real_fail = mission.win, mission.fail
mission.win = function(msg) ended.win = msg; return true end
mission.fail = function(msg) ended.fail = msg; return true end

local face = nil
local function wait(sec)
  local t = 0
  while t < sec do
    t = t + coroutine.yield()
    bond.heal(0, 1, 1)
    if face then bond.face(0, face[1], face[2]) end
  end
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
  print("HEIST SHOT " .. name .. string.format(" t=%.1f hot=%s meter=%s", HEIST.elapsed, tostring(HEIST.hot), (cover.meter(0))))
  cctv.test_shot()
  wait(0.5)
end

local blown, reports = {}, {}
on("cover_blown", function(g, p, why) blown[#blown + 1] = { g = g, p = p, why = cover.why_name(why), t = HEIST.elapsed } end)
on("cover_report", function(g, p) reports[#reports + 1] = { g = g, p = p, t = HEIST.elapsed } end)

-- Bond 0 onto a point: the nearest pad plus an offset
local function warp_xz(x, z)
  local best, bd = nil, 1e9
  for _, p in ipairs(pads.far_from(x, z, 0, 900)) do
    local px, _, pz = pads.pos(p)
    local d = d2(px, pz, x, z)
    if d < bd then best, bd = p, d end
  end
  if not best then return false end
  local px, _, pz = pads.pos(best)
  return bond.warp(0, best, x - px, z - pz)
end

local function guards(area, kinds)
  local out = {}
  for _, g in ipairs(HEIST.guards) do
    if g.area == area and (not kinds or kinds[g.kind]) and spawn.alive(g.chr) and not spawn.down(g.chr) then out[#out + 1] = g.chr end
  end
  return out
end
local POSTS = { post = true, raiser = true, carrier = true }

-- stand where guard c has Bond in sight, about `dist` away (8 directions round him), facing him; true when his scan sees Bond
local function stand_seen(c, dist)
  local x, z = spawn.where(c)
  if not x then return false end
  for k = 0, 7 do
    local a = k * math.pi / 4
    if warp_xz(x + math.sin(a) * dist, z + math.cos(a) * dist) then
      face = { x, z }
      wait(0.4)
      local s = cover.state(c)
      if s and (s.sees & 1) ~= 0 then return true end
    end
  end
  return false
end

local function witness(c) local s = cover.state(c); return s ~= nil and (s.witness & 1) ~= 0 end
local function blown_by(c) for _, b in ipairs(blown) do if b.g == c then return b end end end

-- ------------------------------------------------------------------------------------------------------------------------------------

local function hall_path()
  local hall = guards("hall", POSTS)
  check("hall guards on post", #hall >= 3, tostring(#hall))
  local watched, maxlvl, maxspeed = false, 0, 0
  local route = { "hall_entry_w", "hall_mid", "cust_3", "cust_2", "cust_1", "cust_5", "cust_4", "hall_entry_e", "flap_s" }
  bond.warp(0, pad(route[1]))
  wait(1)
  for i = 2, #route do
    local x, _, z = pads.pos(pad(route[i]))
    bond.walk(0, x, z)
    for _ = 1, 16 do
      wait(0.25)
      local st, lvl = cover.meter(0)
      if st == "watched" or st == "suspicious" then watched = true end
      if lvl > maxlvl then maxlvl = lvl end
      if cover.speed(0) > maxspeed then maxspeed = cover.speed(0) end
      local bx, _, bz = bond.pos(0)
      if d2(bx, bz, x, z) < 60 then break end
    end
    if i == 8 and SHOTS then bond.walk_stop(); face = { 300, 600 }; wait(0.6); shot("cover_hall"); face = nil end
  end
  bond.walk_stop()
  print(string.format("HEIST   walking the hall: top speed %.0f units/s, highest meter %.2f", maxspeed, maxlvl))
  check("the hall's guards have Bond in sight (the eye is open)", watched)
  check("walking at full stick through a public room keeps the meters low", maxlvl < 0.5, string.format("%.2f", maxlvl))
  check("disguised in the hall: no guard sees through him", #blown == 0, #blown > 0 and (blown[1].why .. " by " .. blown[1].g) or "")
  check("no alarm", not HEIST.hot and not alarm.active())
  check("the stock sight is hidden: no hall guard attacks (no shots fired)", (function()
    for _, c in ipairs(hall) do local f = spawn.fire_stats(c); if f > 0 then return false end end
    return true end)())
end

local function gun_path()
  local hall = guards("hall", { post = true })
  local g
  for _, c in ipairs(hall) do if stand_seen(c, 350) then g = c; break end end
  check("a hall guard has Bond in sight", g ~= nil)
  if not g then return end
  wait(1)
  check("holstered in his sight: no Hey", not witness(g) and #blown == 0)
  bond.equip(0, "pp7sil")
  local hey = wait_for(3, function() return witness(g) end)
  local b = blown_by(g)
  check("a gun drawn in view: that guard sees through him", hey and b ~= nil and b.why == "armed", b and b.why or "none")
  local others = 0
  for _, c in ipairs(hall) do if c ~= g and witness(c) then others = others + 1 end end
  print(string.format("HEIST   %d other hall guard(s) also saw the gun", others))
  check("the guards that did not see it do not know", others < #hall - 1, string.format("%d of %d others", others, #hall - 1))
  -- (section 18: a civilian killed in the guards' crossfire in a camera's view is reported by the camera, and that alarm can come first)
  local cam_body = false
  on("cctv_spotted", function(c, p) if p == -1 then cam_body = true end end)
  local rep = wait_for(8, function() return #reports > 0 or (cam_body and HEIST.hot) end)
  check("still up after the radio time, he raises the alarm (or a camera saw the crossfire's victim first)", rep and HEIST.hot and cover.alert(),
        string.format("reports=%d camera body=%s hot=%s", #reports, tostring(cam_body), tostring(HEIST.hot)))
end

local function loot_path()
  local hall = guards("hall", { post = true })
  local g
  for _, c in ipairs(hall) do if stand_seen(c, 350) then g = c; break end end
  check("a hall guard has Bond in sight", g ~= nil)
  if not g then return end
  wait(1)
  local bx, _, bz = bond.pos(0)
  local best, bd = nil, 1e9
  for _, p in ipairs(pads.far_from(bx, bz, 0, 400)) do local px, _, pz = pads.pos(p); if d2(px, pz, bx, bz) < bd then best, bd = p, d2(px, pz, bx, bz) end end
  local h = spawn.item(best, "goldbar")
  local x, z = spawn.item_pos(h)
  warp_xz(x, z)
  local got = wait_for(2, function() return HEIST.bars > 0 end)
  check("a gold bar picked up", got)
  local hey = wait_for(1.5, function() return #blown > 0 end)
  check("carrying gold in view: blown at once (loot)", hey and blown[1].why == "loot", hey and blown[1].why or "none")
end

local function restricted_path()
  local sec = guards("security", POSTS)
  check("security staff on post", #sec >= 1, tostring(#sec))
  -- straight into the middle of the security office, facing the room: the first scan that has him in a guard's sight starts the meter
  bond.warp(0, pad("sec_mid"))
  face = { 1740, 700 }
  local t0, seen_at = HEIST.elapsed, nil
  local hey = wait_for(6, function()
    for _, c in ipairs(sec) do local s = cover.state(c); if s and (s.sees & 1) ~= 0 and not seen_at then seen_at = HEIST.elapsed end end
    return #blown > 0 end)
  if not hey then for _, c in ipairs(sec) do if stand_seen(c, 300) then seen_at = HEIST.elapsed; break end end; hey = wait_for(3, function() return #blown > 0 end) end
  seen_at = seen_at or t0
  check("seen in the security wing: blown fast (restricted, within 1.5 s of being seen)", hey and blown[1].why == "restricted" and blown[1].t - seen_at <= 1.5,
        hey and string.format("%s %.2f s after he was first seen", blown[1].why, blown[1].t - seen_at) or "none")
end

local function staff_path()
  local off = guards("offices", { post = true })
  check("office guards on post", #off >= 1, tostring(#off))
  local g
  for _, c in ipairs(off) do if stand_seen(c, 420) then g = c; break end end
  check("an office guard has Bond in sight", g ~= nil)
  if not g then return end
  local t0 = HEIST.elapsed
  wait(1.5)
  local s1 = cover.suspicion(g, 0) or 0
  check("staff only: the meter builds", s1 > 0.1 and s1 < 0.8 and #blown == 0, string.format("%.2f after 1.5 s", s1))
  local st = cover.meter(0)
  check("the eye says suspicious", st == "suspicious", st)
  local hey = wait_for(8, function() return #blown > 0 end)
  check("... then Hey! (staff)", hey and (blown[1].why == "staff" or blown[1].why == "close"), hey and string.format("%s after %.1f s", blown[1].why, blown[1].t - t0) or "none")
  if SHOTS then wait(0.3); shot("cover_hey") end
end

local function ko_path()
  mode.body_check(false)          -- (a body found would put everyone on alert: not this test's business)
  local off = guards("offices", { post = true })
  local g
  for _, c in ipairs(off) do if stand_seen(c, 300) then g = c; break end end
  check("an office guard has Bond in sight", g ~= nil)
  if not g then return end
  cover.blow(g, 0)
  check("he sees through Bond", witness(g))
  wait(2)
  spawn.ko(g, 0)
  check("knocked out before his radio time", ko.is(g))
  bond.warp(0, pad("ocorr_n"))   -- away, out of sight of the others
  wait(8)
  check("no report, no alarm, the cover holds", #reports == 0 and not HEIST.hot and not cover.alert(),
        string.format("reports=%d hot=%s alert=%s", #reports, tostring(HEIST.hot), tostring(cover.alert())))
end

local function clear_hall()
  for _, g in ipairs(HEIST.guards) do if g.area == "hall" then spawn.remove(g.chr) end end
  for _, c in ipairs(HEIST.civs) do if c.area == "hall" then spawn.remove(c.chr) end end
end

local function cam_path(down)
  mode.body_check(false)
  clear_hall()
  local cam = HEIST.M.P.feed_cam
  cctv.set_alarm_time(1)
  local sec = guards("security", POSTS)
  check("security staff on post", #sec >= 1, tostring(#sec))
  if down then
    for _, c in ipairs(sec) do spawn.ko(c, 0) end
    check("every security guard knocked out", HEIST.security_up() == 0, tostring(HEIST.security_up()))
  end
  local spotted = nil
  on("cctv_spotted", function(c, p) if not spotted then spotted = HEIST.elapsed end end)
  bond.warp(0, pad("cust_1"))
  wait(1.5)
  check("holstered in the camera's view: it times nothing", spotted == nil and (cctv.get(cam).timer or 0) < 0.5,
        string.format("timer %.2f", cctv.get(cam).timer or -1))
  bond.equip(0, "pp7sil")
  local sp = wait_for(4, function() return spotted ~= nil end)
  check("armed: the camera spots him", sp)
  check("the camera alone raises no alarm", not HEIST.hot and not alarm.active())
  if down then
    wait(6)
    check("all security staff down: no alarm after the delay", not HEIST.hot and not alarm.active() and HEIST.report == nil,
          string.format("hot=%s", tostring(HEIST.hot)))
    check("the camera still sees him (the feed intel works)", cctv.sees(cam, bond.id(0)))
  else
    local hot, t = wait_for(8, function() return HEIST.hot end)
    check("security staff up: the alarm after the delay", hot and HEIST.elapsed - spotted >= 3.5,
          string.format("hot=%s after %.1f s", tostring(hot), HEIST.elapsed - (spotted or 0)))
  end
end

local function to_breaker()
  bond.warp(0, HEIST.M.P.power_to)
  face = HEIST.M.P.breaker
  wait(0.5)
  cctv.test_use(0)
  wait(0.5)
end

local function power_path()
  check("the plant room is restricted", cover.room(HEIST.M.power_room) == "restricted")
  to_breaker()
  check("a use press at the breaker starts the work (the cabinet)", HEIST.power.task == "cabinet", tostring(HEIST.power.task))
  local cut = wait_for(14, function() return HEIST.power.out end)
  check("the full hold cuts the power (cabinet, then the main)", cut and not power.on(), string.format("step=%d task=%s", HEIST.power.step, tostring(HEIST.power.task)))
  face = nil
  local dark = true
  for _, cam in ipairs(cctv.list()) do if not cctv.get(cam).disabled then dark = false end end
  check("every camera is dark", dark)
  alarm.start()
  wait(0.5)
  check("the alarm cannot be raised (no panel, no siren)", not alarm.active() and not HEIST.hot)
  bond.warp(0, pad("screens"))
  wait(0.5)
  cctv.test_use(0)
  wait(1.5)
  local ri = cctv.render_info()
  check("a feed shows static", cctv.viewing(0) ~= nil and ri ~= nil and ri.offline == true,
        string.format("viewing=%s offline=%s", tostring(cctv.viewing(0)), tostring(ri and ri.offline)))
  shot("cover_power_feed")
  cctv.view_off(0)
  wait(1)
  local sent = wait_for(6, function() return #HEIST.power.invest > 0 end)
  check("a guard comes to look at the outage", sent, tostring(#HEIST.power.invest))
  local back = wait_for(40, function() return not HEIST.power.out end)
  check("at the breaker he puts the power back", back and power.on(), string.format("repair %.1f", HEIST.power.repair))
  wait(0.5)
  local live = false
  for _, cam in ipairs(cctv.list()) do if not cctv.get(cam).disabled then live = true end end
  check("the cameras are back", live)
  alarm.start()
  wait(0.5)
  check("and the alarm works again", alarm.active())
end

local function interrupt_path()
  to_breaker()
  wait(1.5)
  local x, z = HEIST.M.P.breaker[1], HEIST.M.P.breaker[2]
  bond.warp(0, pad("power_mid"))                 -- off the panel
  wait(1)
  check("walking off stops the work", HEIST.power.task == nil and not HEIST.power.out, tostring(HEIST.power.task))
  wait(4)
  check("an interrupted hold does not cut the power", power.on() and not HEIST.power.out)
  to_breaker()
  local cut = wait_for(14, function() return HEIST.power.out end)
  check("back at the panel it goes on and cuts", cut)
end

local function generator_path()
  to_breaker()
  local cut = wait_for(14, function() return HEIST.power.out end)
  check("power cut", cut)
  local t0 = HEIST.elapsed
  local back = wait_for(14, function() return not HEIST.power.out end)
  check("the backup generator restores it", back and HEIST.elapsed - t0 >= 7, string.format("after %.1f s", HEIST.elapsed - t0))
  bond.warp(0, HEIST.M.P.generator_front)
  face = HEIST.M.P.generator
  wait(0.5)
  cctv.test_use(0)
  wait(0.5)
  check("a use press at the generator starts work on it", HEIST.power.task == "generator", tostring(HEIST.power.task))
  local off = wait_for(8, function() return HEIST.power.gen_off end)
  check("the generator is disabled", off)
  to_breaker()
  cut = wait_for(14, function() return HEIST.power.out end)
  check("cut again", cut)
  wait(12)
  check("with the generator dead the power stays out", HEIST.power.out and not power.on())
end

-- the guard door rules (AC's playtest: guards who heard Bond in the vault opened the vault door)
local function door_open_max(idx, secs, until_fn)
  local m, t = 0, 0
  while t < secs do
    local d = door.get(idx)
    if d and d.open > m then m = d.open end
    if until_fn and until_fn() then break end
    wait(0.25); t = t + 0.25
  end
  return m
end

local function send(pad_from, role, pad_to)
  local c = spawn.chr(pad_from, { post = true, weapon = "kf7", offset = { 0, 60 } }) or spawn.chr(pad_from, { post = true, weapon = "kf7" })
  if not c then return nil end
  spawn.role(c, role)
  wait(0.5)
  local ok = spawn.go(c, pad_to, true)
  local x, z = spawn.where(c)
  print(string.format("HEIST   sent chr %d (%s) from %.0f,%.0f to pad %d: %s", c, role, x or -1, z or -1, pad_to, tostring(ok)))
  return c
end

local function vault_noise()
  local O = LEVEL.objects
  -- noise inside the time-locked vault: the guards come, the vault doors stay shut
  cover.alert(true)
  bond.warp(0, pad("vault_captain"))
  wait(0.5)
  local r0 = door.guard_stats()
  for _ = 1, 4 do bond.test_noise(0, 40); wait(1) end   -- loud enough for the antechamber (the guards' hearing x 40 x 100 units)
  local ma = door_open_max(O.vault_door_a.index, 14)
  local mb = math.max(door.get(O.vault_door_b.index).open, 0)
  local r1 = door.guard_stats()
  local near = 0
  local cx, cz = HEIST.M.P.crack_xz[1], HEIST.M.P.crack_xz[2]
  for _, g in ipairs(HEIST.guards) do
    if g.area == "vault_ante" and spawn.alive(g.chr) then
      local x, z = spawn.where(g.chr)
      if x and spawn.room(g.chr) == LEVEL.rooms.ante_a.id and d2(x, z, cx, cz) < 450 then near = near + 1 end
    end
  end
  print(string.format("HEIST   vault noise: door A max open %.2f, door B %.2f, refusals %d, guards at door A %d", ma, mb, r1 - r0, near))
  check("noise in the vault: the vault doors stay shut", ma < 0.05 and mb < 0.05, string.format("A %.2f B %.2f", ma, mb))
  check("the guards were refused at the vault door and gather outside it", r1 > r0 and near >= 1, string.format("refused %d, at the door %d", r1 - r0, near))
end

local function doors_path()
  local O = LEVEL.objects
  check("the door rules are on", mode.guard_doors())
  -- a hall guard may not open the BLUE door; a security guard may (first: before the vault noise sends everyone running)
  bond.warp(0, pad("start_1"))
  wait(1)
  -- (by position, 40 inside the room's rect: a chr standing at a doorway can report the room beyond it; bank v2 rooms by name)
  local function inside(c, rname)
    local r = LEVEL.rooms[rname].rect
    local x, z
    if c then x, z = spawn.where(c) end
    return x ~= nil and x > r[1] + 40 and x < r[3] - 40 and z > r[2] + 40 and z < r[4] - 40
  end
  local function in_sec(c) return inside(c, "sec_office") end
  local function in_corr(c) return inside(c, "sec_corr") end      -- out of the barracks, in the security corridor
  local blue = O.blue_hall.index
  local hc = send(pad("hall_post_ne"), "hall", pad("sec_mid"))
  local mh = math.max(door_open_max(blue, 12), door.get(O.blue_hall.pair).open)
  check("a hall guard does not open the BLUE door", hc ~= nil and mh < 0.05 and not in_sec(hc),
        string.format("open %.2f x %s", mh, tostring(hc and spawn.where(hc))))
  if hc then spawn.remove(hc) end
  wait(1)
  local sc = send(pad("hall_post_ne"), "security", pad("sec_mid"))
  local ms = door_open_max(blue, 30, function() return in_sec(sc) end)   -- (bank v2: round the counter through its flap first)
  -- (on screen he opens it; off screen the stock pad hop takes an allowed guard through without moving the door)
  local sx_, sz_ = nil, nil
  if sc then sx_, sz_ = spawn.where(sc) end
  check("a security guard (BLUE by role) goes through it", sc ~= nil and in_sec(sc),
        string.format("open %.2f at %s, %s in room %s", ms, tostring(sx_), tostring(sz_), tostring(sc and LEVEL.room_name[spawn.room(sc) or 0])))
  -- 3. a barracks door opens for the squad that comes out of it (and not for another guard)
  -- (bank v2: the barracks' own door, off the security corridor; its layout tag lets the reinforcements out; the rule set here
  -- is the bare "barracks")
  local pd = O.barracks_door.index
  door.guard_access(pd, "barracks")
  local oc = send(pad("barracks_2"), "corridor", pad("sec_corr_mid"))
  local mo = door_open_max(pd, 10)
  check("the barracks door stays shut for a corridor guard", oc ~= nil and mo < 0.05 and not in_corr(oc), string.format("open %.2f", mo))
  if oc then spawn.remove(oc) end
  wait(1)
  local bc = send(pad("barracks_2"), "barracks", pad("sec_corr_mid"))
  local mp = door_open_max(pd, 15, function() return in_corr(bc) end)
  check("a barracks squad comes out through its own door", bc ~= nil and in_corr(bc), string.format("open %.2f", mp))
  vault_noise()
end

local finished = false
local function body()
  local ready = wait_for(25, function() return HEIST.ready end)
  check("mode ready", ready, "run " .. RUN)
  wait(1.5)
  check("cover on, Bond holstered (the slappers)", cover.on() and bond.weapon(0) == 1 and cover.count() > 20,
        string.format("weapon=%s guards=%d", tostring(bond.weapon(0)), cover.count()))
  if RUN == "hall" then hall_path() elseif RUN == "gun" then gun_path() elseif RUN == "loot" then loot_path()
  elseif RUN == "restricted" then restricted_path() elseif RUN == "staff" then staff_path() elseif RUN == "ko" then ko_path()
  elseif RUN == "cam" then cam_path(false) elseif RUN == "camdown" then cam_path(true) elseif RUN == "power" then power_path()
  elseif RUN == "interrupt" then interrupt_path() elseif RUN == "generator" then generator_path() elseif RUN == "doors" then doors_path() end
  print(string.format("HEIST DONE %d passed, %d failed", pass, fail))
  finished = true
  real_fail("cover test done")
end

local co = coroutine.wrap(body)
on("tick", function(dt)
  if finished then return end
  local ok, err = pcall(co, dt)
  if not ok then finished = true; print("HEIST FAIL script error: " .. tostring(err)); print(string.format("HEIST DONE %d passed, %d failed", pass, fail + 1)) end
end)
