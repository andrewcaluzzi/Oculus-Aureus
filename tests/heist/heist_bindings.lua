-- Heist bindings test mode (P0 job 2; on the Janus bank since P2.4): exercises door.*, spawn.key / key_at, bond.has_key, alarm.*, cctv.*,
-- the alarm / cctv_spotted / use / gadget_used / item_pickup events and hud.countdown. Prints "HEIST PASS|FAIL <name> ..." lines and
-- "HEIST DONE n/m". Run: tests/heist/run.sh (Xvfb, build_pc built with tools/levelgen/janus_bank.json on the levelgen slot).
-- The doors, cameras and pads come from LEVEL by name.
include("levelgen_layout")
mode.keep_locks()        -- the doors keep their keys
mode.per_player_keys(true)

local pass, fail = 0, 0
local function check(name, ok, extra)
  if ok then pass = pass + 1 else fail = fail + 1 end
  print(string.format("HEIST %s %s %s", ok and "PASS" or "FAIL", name, extra or ""))
end

local ev = { alarm = {}, pickup = {}, spotted = {}, use = {}, gadget = {}, died = {} }
on("alarm", function(a) ev.alarm[#ev.alarm + 1] = a; print("HEIST event alarm " .. a) end)
on("item_pickup", function(t, id, q, p) ev.pickup[#ev.pickup + 1] = { t, id, q, p }; print(string.format("HEIST event item_pickup type=%d id=%d qty=%d player=%s", t, id, q, tostring(p))) end)
on("cctv_spotted", function(c, p) ev.spotted[#ev.spotted + 1] = { c, p }; print(string.format("HEIST event cctv_spotted camera=%d player=%d", c, p)) end)
on("use", function(p, k, i, o) ev.use[#ev.use + 1] = { p, k, i, o }; print(string.format("HEIST event use player=%d proptype=%d idx=%d objtype=%d", p, k, i, o)) end)
on("gadget_used", function(p, it, i, o) ev.gadget[#ev.gadget + 1] = { p, it, i, o }; print(string.format("HEIST event gadget_used player=%d item=%d idx=%d objtype=%d", p, it, i, o)) end)
on("chr_died", function(v) ev.died[v] = true end)

local function wait(sec)
  local t = 0
  while t < sec do t = t + coroutine.yield() end
end

local function walk_to(x, z, maxsec)
  local t = 0
  while t < maxsec do
    local d = bond.walk(0, x, z)
    if d and d < 25 then bond.walk_stop(); return true end
    local bx, by, bz = bond.pos(0)
    if math.floor(t * 4) % 8 == 0 then print(string.format("HEIST walk d=%.0f bond=%.0f,%.0f,%.0f target=%.0f,%.0f", d or -1, bx or 0, by or 0, bz or 0, x, z)) end
    t = t + coroutine.yield()
  end
  bond.walk_stop()
  return false
end

local function last_pickup()
  return ev.pickup[#ev.pickup]
end

local O, PAD = LEVEL.objects, LEVEL.pads

local function body()
  wait(1)
  print("HEIST begin, bonds=" .. bond.count())

  -- door.list / get / lock / unlock / set_keyflags (cell_floor: the deposit cell that takes the floor key, 0x1)
  local doors = door.list()
  local want = #LEVEL.safes
  for _, o in pairs(O) do if o.type == "door" then want = want + (o.pair and 2 or 1) end end
  check("door.list", #doors == want, "count=" .. #doors .. " want " .. want)
  local cell = O.cell_floor.index
  local d = door.get(cell)
  check("door.get cell", d and d.cell and d.keyflags == 1 and d.state == "closed", d and (d.state .. " key=" .. d.keyflags) or "nil")
  door.unlock(cell); check("door.unlock", door.get(cell).keyflags == 0)
  door.lock(cell, 3); check("door.lock", door.get(cell).keyflags == 3)
  door.unlock(cell, 2); check("door.unlock(flags)", door.get(cell).keyflags == 1)
  door.set_keyflags(cell, 1); check("door.set_keyflags", door.get(cell).keyflags == 1)
  check("door.get missing", door.get(9999) == nil)

  -- door.open ignores the lock (Bond has no key)
  check("door.open started", door.open(cell) == true)
  wait(3)
  d = door.get(cell)
  check("door.open result", d.state == "open" or d.open > 0.5, d.state .. " open=" .. string.format("%.2f", d.open))
  door.close(cell); wait(4)
  d = door.get(cell)
  check("door.close result", d.state == "closed" or d.open < 0.5, d.state .. " open=" .. string.format("%.2f", d.open))

  -- alarm
  check("alarm idle", alarm.active() == false)
  alarm.start(); wait(0.5)
  check("alarm.start", alarm.active() == true and ev.alarm[1] == 1, "events=" .. table.concat(ev.alarm, ","))
  alarm.stop(); wait(0.5)
  check("alarm.stop", alarm.active() == false and ev.alarm[2] == 0, "events=" .. table.concat(ev.alarm, ","))

  -- cctv: the hall's customer-side camera looks at the pad cust_1
  local cams = cctv.list()
  check("cctv.list", #cams == #LEVEL.cameras, "count=" .. #cams)
  local cam = O.cam_hall_s.index
  local c = cctv.get(cam)
  check("cctv.get", c and c.live and not c.disabled and c.alarm_time == 0, c and string.format("room=%d face=%.0f", c.room, c.face) or "nil")
  check("cctv.set_alarm_time", cctv.set_alarm_time(2, cam) == true and cctv.get(cam).alarm_time == 2)
  for _, other in ipairs(cams) do if other ~= cam then cctv.disable(other) end end   -- only this camera may see Bond
  check("cctv.disable", cctv.disable(cam) == true and cctv.get(cam).disabled == true)
  bond.warp(0, O.cam_hall_s.lookpad)
  wait(4)
  check("disabled camera is blind", #ev.spotted == 0 and cctv.get(cam).timer == 0 and not alarm.active(), "spotted=" .. #ev.spotted)
  check("cctv.enable", cctv.enable(cam) == true and cctv.get(cam).disabled == false)
  local t0 = #ev.spotted
  wait(4)
  check("cctv_spotted event", #ev.spotted > t0 and ev.spotted[t0 + 1][1] == cam and ev.spotted[t0 + 1][2] == 0,
        "spotted=" .. (#ev.spotted - t0) .. " alarm=" .. tostring(alarm.active()))
  check("cctv alarm fired after the override time", alarm.active() == true)
  alarm.stop()
  cctv.alarm_on_spot(false)
  cctv.set_alarm_time(0, cam)
  cctv.disable(cam)  -- keep it quiet for the rest of the test

  -- keyed guard in the staff corridor: spawn.key on a chr, drop on death, pick up; the RED stairs door needs 0x8
  local red = O.red_stairs.index
  bond.warp(0, PAD.back_4)
  wait(1)
  local g = spawn.chr(PAD.back_3, { behaviour = "hold", weapon = "none" })
  check("spawn.chr", g ~= nil)
  local kh = spawn.key(g, 8)
  check("spawn.key", kh ~= nil)
  local alive, held = spawn.key_alive(kh)
  check("key carried", alive and held)
  check("bond lacks the key", bond.has_key(0, 8) == false and bond.keys(0) == 0)
  check("door.try refused without the key", door.try(0, red) == false, "(the RED stairs door needs 0x8)")
  spawn.kill(g)
  wait(2)
  local a2, h2, kx, kz = spawn.key_alive(kh)
  check("key dropped on death", a2 and not h2 and ev.died[g], string.format("at %.0f,%.0f", kx or 0, kz or 0))
  local arrived = walk_to(kx, kz, 15)
  wait(1)
  local pk = last_pickup()
  check("key picked up", arrived and pk and pk[1] == 4 and pk[2] == 8 and pk[4] == 0, pk and string.format("type=%d flags=%d player=%d", pk[1], pk[2], pk[4]) or "no pickup event")
  check("bond.has_key", bond.has_key(0, 8) == true and bond.has_key(1, 8) == false and bond.keys(0) == 8)
  check("door.try with the key", door.try(0, red) == true)
  check("use event", ev.use[#ev.use] and ev.use[#ev.use][1] == 0 and ev.use[#ev.use][3] == red and ev.use[#ev.use][4] == 1,
        ev.use[#ev.use] and ("idx=" .. ev.use[#ev.use][3] .. " objtype=" .. ev.use[#ev.use][4]) or "none")

  -- floor key
  local fh = spawn.key_at(PAD.back_3, 0x40)
  check("spawn.key_at", fh ~= nil)
  local fa, fhd, fx, fz = spawn.key_alive(fh)
  check("floor key lies on the floor", fa and not fhd)
  walk_to(fx, fz, 15)
  wait(1)
  check("floor key picked up", bond.has_key(0, 0x40) == true and bond.keys(0) == 0x48, "keys=" .. bond.keys(0))

  -- gadget event plumbing
  check("bond.test_gadget", bond.test_gadget(0, "doordecoder") == true and #ev.gadget == 1 and ev.gadget[1][1] == 0)

  -- countdown
  check("countdown idle", hud.countdown_left() == nil)
  hud.countdown(30); wait(1.5)
  local left = hud.countdown_left()
  check("hud.countdown", left ~= nil and left < 30 and left > 27, string.format("left=%.2f", left or -1))
  hud.countdown(nil)
  check("hud.countdown hide", hud.countdown_left() == nil)

  print(string.format("HEIST DONE %d passed, %d failed", pass, fail))
  if fail == 0 then mission.win("heist bindings ok") else mission.fail("heist bindings failed") end
end

local co = coroutine.wrap(body)
local finished = false
on("tick", function(dt)
  if finished then return end
  local ok, err = pcall(co, dt)
  if not ok then finished = true; print("HEIST FAIL script error: " .. tostring(err)) end
end)
