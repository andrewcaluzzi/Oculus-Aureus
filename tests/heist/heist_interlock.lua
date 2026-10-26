-- Interlock test (designs/bank-heist.md section 29): the real tools/mode_heist.lua on bank v3 with tools/heist_interlock.lua, driven
-- headless through HEIST / HEIST_INTERLOCK. tests/heist/run_interlock.sh runs one scenario per process (GE_IL_<RUN>=1; none: basic):
--   basic     the rule for a Bond and for a script: one door at a time, the refused door queues and opens itself after the hold
--   guards    four guards walk alley <-> back corridor through the mantrap for 150 s: they all cross, nobody deadlocks, never two open
--   tailgate  a Bond follows a guard through (the camera misses him), then again with the camera sure to see (lockdown)
--   verify    an undisguised Bond alone in the mantrap is flagged (lockdown, the security room called); disguised he passes; the
--             override cylinder (lockpicks, hold FIRE) opens a door in a lockdown
--   power     the power cut: the mantrap fails safe (released, keys off, both doors may open), the sally port fails secure (frozen);
--             back on, everything relocks. The armoured car's cycle and the dock key.
--   win       the solo win via the staff entrance: GREEN, in through the mantrap, the vault, out through the mantrap's dock door
-- Every run samples every script tick (4 Hz) that no two doors of a group are open together (outside the power cut and the override), and checks the
-- C side's own count (interlock.get(g).violations) at the end. Prints "HEIST PASS|FAIL ..." and "HEIST DONE n passed, m failed".
local RUN = "basic"
for _, r in ipairs({ "guards", "tailgate", "verify", "power", "win" }) do
  if mode.env_flag("GE_IL_" .. r:upper()) then RUN = r end
end
local COVER = (RUN == "tailgate" or RUN == "verify")
HEIST_SETTINGS = {
  test = true,
  cover = COVER,
  crack_time = 12, jam_chance = 0, jam_every = 1000, vault_time = 45, klaxon_at = 5,
  briefing = false,
  interlock = { car_first = (RUN == "power") and 20 or 100000, lock = (RUN == "verify") and 40 or 12 },   -- (verify: the lockdown outlasts the override)
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
mission.win = function(msg) ended.win = msg; print("HEIST event mission.win " .. tostring(msg)); return true end
mission.fail = function(msg) ended.fail = msg; print("HEIST event mission.fail " .. tostring(msg)); return true end

local IL = HEIST_INTERLOCK
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

-- the watcher: every tick, no two doors of a group open (unless the power is out on a fail-safe group, or an override runs)
local both, both_ok_window, sampled = 0, false, 0
local function watch_tick()
  if not IL or #IL.list == 0 then return end
  for _, g in ipairs(IL.list) do
    if g.id then
      local n = 0
      for _, d in ipairs(g.doors) do local dg = door.get(d); if dg and dg.open > 0.02 then n = n + 1 end end
      sampled = sampled + 1
      if n >= 2 and not both_ok_window then
        both = both + 1
        if both <= 3 then print(string.format("HEIST   two doors of %s open at %.2f", g.name, HEIST.elapsed)) end
      end
    end
  end
end

local function G(name) return IL.by_name[name] end
local function open(d) local g = door.get(d); return g and g.open > 0.02 end
local function shut(d) local g = door.get(d); return g and g.open <= 0.0 and (g.state == "closed") end
local function pad_of(doorway, side)
  for _, D in ipairs(LEVEL.doors) do if D.name == doorway then return side == "a" and D.pad_a or D.pad_b end end
end
local ALLEY_SIDE = function() return pad_of("alley-staff_entry", "a") end
local IN_OUTER = function() return pad_of("alley-staff_entry", "b") end
local CORR_SIDE = function() return pad_of("back_corr_e-staff_entry", "a") end
local IN_INNER = function() return pad_of("back_corr_e-staff_entry", "b") end
local DOCK_IN = function() return pad_of("staff_entry-dock", "a") end
local DOCK_OUT = function() return pad_of("staff_entry-dock", "b") end

local function give_key(flags)
  local x, _, z = bond.pos(0)
  local pads_here = pads.far_from(x, z, 0, 400)
  spawn.key_at(pads_here[1], flags)
  bond.warp(0, pads_here[1])
  return wait_for(3, function() return bond.has_key(0, flags) end)
end

local function setup_checks()
  check("two interlock groups from the layout (mantrap, sally port)", IL and #IL.list == 2 and G("staff_mantrap") and G("sally_port")
        and G("staff_mantrap").id and G("sally_port").id, IL and tostring(#IL.list) or "no HEIST_INTERLOCK")
  local m = G("staff_mantrap")
  check("the mantrap: outer, inner and the dock door; 3 s hold; fail-safe", m and #m.doors == 3 and m.hold == 3 and m.fail == "safe")
  local s = G("sally_port")
  check("the sally port's outer roller wants the DOCK KEY and no guard opens it", s and door.get(s.outer).keyflags == 0x20)
  local w = interlock.label(m.outer)
  check("AR word on a ready mantrap door: INTERLOCK", w == "INTERLOCK", tostring(w))
end

-- ------------------------------------------------------------------------------------------------------------------------------------
local function basic()
  setup_checks()
  local m = G("staff_mantrap")
  bond.warp(0, ALLEY_SIDE())
  wait(0.5)
  check("no GREEN: the outer door refuses (the badge reader)", door.try(0, m.outer) == false)
  check("GREEN picked up", give_key(0x2))
  bond.warp(0, ALLEY_SIDE())
  wait(0.5)
  check("GREEN opens the outer door", door.try(0, m.outer) == true)
  wait(0.5)
  bond.warp(0, IN_INNER())                 -- inside, at the inner door, while the outer still stands open
  wait(0.3)
  check("the inner door refuses while the outer one is open", door.try(0, m.inner) == false and not open(m.inner))
  local w = interlock.label(m.inner)
  check("... AR word WAIT (QUEUED)", w == "WAIT (QUEUED)", tostring(w))
  door.open(m.dock_door or m.doors[3])
  wait(0.1)
  check("a script's door.open on the dock door is refused too", not open(m.doors[3]))
  local closed = wait_for(8, function() return shut(m.outer) end)
  check("the outer door closes itself (the doors-closing timer)", closed)
  local v = wait_for(1, function() return interlock.get(m.id).phase == "verifying" end)
  check("then VERIFYING (the hold)", v, interlock.get(m.id).phase)
  check("... AR word VERIFYING > NEXT on the queued inner door", (interlock.label(m.inner) or ""):find("VERIFYING") ~= nil, tostring(interlock.label(m.inner)))
  local t0 = HEIST.elapsed
  local opened = wait_for(6, function() return open(m.inner) end)
  local held = HEIST.elapsed - t0
  check("the queued inner door opens itself after the hold (GREEN badge verified)", opened and held >= 2.0 and held <= 4.5, string.format("%.2f s", held))
  check("the outer door stayed shut while the inner one opened", shut(m.outer))
  bond.warp(0, CORR_SIDE())
  wait(0.5)
  check("the Bond badged through: verified, not flagged", IL.stats.passes >= 1 and IL.stats.flags == 0, string.format("passes %d flags %d", IL.stats.passes, IL.stats.flags))
  -- the sally port: a script's order for both rollers at once
  local s = G("sally_port")
  door.open(s.inner)
  wait(0.2)
  door.open(s.outer)
  wait(1)
  check("sally port: the inner roller opens, the outer one is held", open(s.inner) and not open(s.outer))
  local q = wait_for(25, function() return open(s.outer) end)
  check("... and the outer one opens after the inner shut and the hold", q and not open(s.inner))
end

-- ------------------------------------------------------------------------------------------------------------------------------------
local function guards()
  setup_checks()
  local m = G("staff_mantrap")
  bond.warp(0, LEVEL.pads.van)             -- far away
  local ends = { LEVEL.pads.alley_staff, LEVEL.pads.back_e3 }
  local gs = {}
  for i = 1, 4 do
    local from = (i % 2 == 1) and 1 or 2
    local c = spawn.chr(ends[from], { weapon = "none", post = true, offset = { (i > 2) and 60 or 0, 0 } })
    if c then
      spawn.role(c, "security")
      gs[#gs + 1] = { chr = c, to = 3 - from, crossings = 0, last = spawn.room(c), sent = HEIST.elapsed }
      spawn.go(c, ends[3 - from], false)
    end
  end
  check("four walkers placed", #gs == 4, tostring(#gs))
  local alley, corr = LEVEL.rooms.alley.id, LEVEL.rooms.back_corr_e.id
  local t = 0
  while t < 150 do
    wait(0.5); t = t + 0.5
    if t % 15 == 0 then
      local s = {}
      for i, g in ipairs(gs) do local x, z = spawn.where(g.chr); s[#s + 1] = string.format("%d:%s@%.0f,%.0f>%d", i, LEVEL.room_name[spawn.room(g.chr) or 0] or "?", x or 0, z or 0, g.to) end
      print("HEIST   walkers " .. table.concat(s, " ") .. "  mantrap " .. interlock.get(m.id).phase)
    end
    for _, g in ipairs(gs) do
      local r = spawn.room(g.chr)
      if (r == alley or r == corr) and r ~= g.last and (g.last == alley or g.last == corr or g.last == LEVEL.rooms.staff_entry.id) then
        if (g.to == 1 and r == alley) or (g.to == 2 and r == corr) then
          g.crossings = g.crossings + 1
        end
      end
      if r == alley or r == corr then g.last = r end
      local tx, _, tz = pads.pos(ends[g.to])
      local x, z = spawn.where(g.chr)
      if x and d2(x, z, tx, tz) < 120 then
        g.to = 3 - g.to
        spawn.go(g.chr, ends[g.to], false); g.sent = HEIST.elapsed
      elseif HEIST.elapsed - g.sent > 40 then
        spawn.go(g.chr, ends[g.to], false); g.sent = HEIST.elapsed
      end
    end
  end
  local tot, minc, txt = 0, 99, {}
  for _, g in ipairs(gs) do tot = tot + g.crossings; minc = math.min(minc, g.crossings); txt[#txt + 1] = tostring(g.crossings) end
  local info = interlock.get(m.id)
  check("every walker crossed the mantrap at least twice in 150 s (no deadlock)", minc >= 2, "crossings " .. table.concat(txt, ","))
  check("... many cycles, queued opens used", info.cycles >= 6 and info.queued >= 1, string.format("cycles %d queued %d refused %d", info.cycles, info.queued, info.refused))
  print(string.format("HEIST   guards: %d crossings, cycles %d, queued %d, refused %d", tot, info.cycles, info.queued, info.refused))
end

-- ------------------------------------------------------------------------------------------------------------------------------------
local function tailgate()
  setup_checks()
  local m = G("staff_mantrap")
  for _, cam in ipairs(cctv.list()) do if cam ~= m.cam then cctv.disable(cam) end end   -- only the mantrap's camera watches
  local function one(spot, label)
    IL.T.tailgate_spot = spot
    bond.warp(0, ALLEY_SIDE())
    wait(0.5)
    local c = spawn.chr(LEVEL.pads.alley_staff, { weapon = "none", post = true, offset = { 0, 120 } })
    spawn.role(c, "security")
    cover.guard(c, "security")             -- a staffer of the bank: he takes Bond for staff too
    spawn.go(c, LEVEL.pads.back_e3, false)
    local got = wait_for(20, function() return open(m.outer) end)
    check(label .. ": the staffer badges the outer door", got)
    local by = interlock.get(m.id).by
    -- (a chr's cycle: his, or a patrol's that badged the same door first: the perimeter tour passes the alley)
    check(label .. ": the cycle is a staffer's (a chr badged it)", by ~= nil and by >= 0, string.format("by %s, our staffer %s", tostring(by), tostring(c)))
    wait(0.5)
    bond.warp(0, IN_OUTER())              -- in behind him before it shuts
    local f0, t0 = IL.stats.flags, IL.stats.tailgates
    local v = wait_for(10, function() return interlock.get(m.id).phase == "verifying" or interlock.get(m.id).phase == "lockdown" end)
    wait(0.5)
    check(label .. ": verify runs with Bond inside", v and bond.room(0) == LEVEL.rooms.staff_entry.id, interlock.get(m.id).phase)
    check(label .. ": counted as a tailgate", IL.stats.tailgates == t0 + 1, tostring(IL.stats.tailgates))
    return IL.stats.flags - f0, c
  end
  local flagged, c1 = one(0, "tailgate (unseen)")
  check("tailgate (unseen): not flagged, no lockdown", flagged == 0 and interlock.get(m.id).phase ~= "lockdown")
  local inner = wait_for(10, function() return open(m.inner) end)
  check("tailgate (unseen): the inner door opens for the staffer, Bond walks on", inner)
  bond.warp(0, CORR_SIDE())
  wait(6)
  spawn.kill(c1)
  wait(8)
  local f2 = one(1, "tailgate (caught)")
  check("tailgate (caught): the camera flags him: lockdown, the security room called", f2 == 1 and interlock.get(m.id).phase == "lockdown"
        and (HEIST.report ~= nil or HEIST.hot), string.format("flags %d phase %s report %s", f2, interlock.get(m.id).phase, tostring(HEIST.report ~= nil)))
  check("tailgate (caught): AR word LOCKDOWN", interlock.label(m.inner) == "LOCKDOWN", tostring(interlock.label(m.inner)))
end

-- ------------------------------------------------------------------------------------------------------------------------------------
local function verify()
  setup_checks()
  local m = G("staff_mantrap")
  for _, cam in ipairs(cctv.list()) do if cam ~= m.cam then cctv.disable(cam) end end
  -- in from the back corridor (the inner door has no reader on the inside), no GREEN, no disguise
  bond.warp(0, CORR_SIDE())
  wait(0.5)
  check("the inner door opens from the corridor (no reader)", door.try(0, m.inner) == true)
  wait(0.4)
  bond.warp(0, IN_INNER())
  local v = wait_for(10, function() return interlock.get(m.id).phase == "lockdown" end)
  check("undisguised, unbadged, alone, watched: VERIFY FAILED, lockdown", v and IL.stats.flags == 1, string.format("phase %s flags %d", interlock.get(m.id).phase, IL.stats.flags))
  check("... the security room was called (the camera report)", HEIST.report ~= nil or HEIST.hot)
  HEIST.report = nil
  check("lockdown: the inner door will not open", door.try(0, m.inner) == false and not open(m.inner))
  -- the override: lockpicks, hold FIRE at the inner door
  bond.equip(0, "gaskeyring")
  wait(1)
  bond.face(0, door.get(m.inner).x, door.get(m.inner).z)
  local t0 = HEIST.elapsed
  gadget.test_fire(0, 60 * (IL.T.pick_time + 2))
  local o = wait_for(IL.T.pick_time + 4, function() return open(m.inner) end)
  check("the override cylinder (lockpicks, hold FIRE) opens the inner door in a lockdown", o and IL.stats.overrides == 1,
        string.format("%.1f s, overrides %d", HEIST.elapsed - t0, IL.stats.overrides))
  check("... slowly (at least pick_time)", HEIST.elapsed - t0 >= IL.T.pick_time - 0.5)
  bond.warp(0, CORR_SIDE())
  wait(12)
  -- disguised: passes
  gadget.disguise(0, 2)
  wait_for(40, function() return interlock.get(m.id).phase == "ready" end)
  check("the mantrap is ready again", interlock.get(m.id).phase == "ready", interlock.get(m.id).phase)
  local p0, f0 = IL.stats.passes, IL.stats.flags
  check("disguised: the inner door opens", door.try(0, m.inner) == true)
  wait(0.4)
  bond.warp(0, IN_INNER())
  wait_for(10, function() return interlock.get(m.id).phase == "verifying" end)
  wait(0.5)
  check("disguised as a guard: VERIFYING passes, no flag", IL.stats.passes == p0 + 1 and IL.stats.flags == f0, string.format("passes %d flags %d", IL.stats.passes, IL.stats.flags))
  gadget.disguise(0, 0)
end

-- ------------------------------------------------------------------------------------------------------------------------------------
local function powercut()
  setup_checks()
  local m, s = G("staff_mantrap"), G("sally_port")
  -- the dock key and the armoured car first (car_first = 20)
  local hasfore = wait_for(5, function() return IL.foreman ~= nil end)
  check("the dock foreman carries the DOCK KEY", hasfore and IL.dock_key, tostring(IL.foreman))
  local car = wait_for(30, function() return open(s.outer) end)
  check("the armoured car: the outer roller goes up", car and IL.stats.cars == 1 and s.car_cycle)
  local inr = wait_for(30, function() return open(s.inner) end)
  check("... then, with the outer shut, the inner one", inr and shut(s.outer))
  wait_for(20, function() return s.carstate.step == 0 end)
  check("... and the car is in (cycle over)", s.carstate.step == 0 and not s.car_cycle)
  wait_for(15, function() return interlock.get(s.id).phase == "ready" and interlock.get(m.id).phase == "ready" end)
  -- the power cut
  both_ok_window = true
  HEIST.cut_power("test")
  wait(0.5)
  check("power out: the mantrap is RELEASED (fail-safe), its badge reader off", interlock.get(m.id).phase == "released" and door.get(m.outer).keyflags == 0
        and interlock.label(m.outer) == "RELEASED", string.format("%s k%x", interlock.get(m.id).phase, door.get(m.outer).keyflags))
  bond.warp(0, ALLEY_SIDE())
  wait(0.3)
  check("released: the outer door opens with no card", door.try(0, m.outer) == true)
  bond.warp(0, CORR_SIDE())
  wait(0.3)
  check("released: and the inner one at the same time (egress)", door.try(0, m.inner) == true)
  wait(1)
  check("released: both stand open", open(m.outer) and open(m.inner))
  door.open(s.inner)
  wait(1)
  check("power out: the sally port is frozen (fail-secure): NO POWER, nothing moves", interlock.get(s.id).phase == "nopower" and not open(s.inner)
        and interlock.label(s.inner) == "NO POWER", interlock.get(s.id).phase)
  HEIST.restore_power("test")
  wait(6)
  both_ok_window = false
  check("power back: the mantrap relocks (GREEN on the outer door again)", door.get(m.outer).keyflags == 0x2 and interlock.get(m.id).phase ~= "released",
        string.format("k%x %s", door.get(m.outer).keyflags, interlock.get(m.id).phase))
  wait_for(12, function() return shut(m.outer) and shut(m.inner) end)
  check("... and the doors-closing timer shuts both", shut(m.outer) and shut(m.inner))
end

-- ------------------------------------------------------------------------------------------------------------------------------------
local function win()
  setup_checks()
  local m = G("staff_mantrap")
  bond.warp(0, ALLEY_SIDE())
  check("GREEN for the staff entrance", give_key(0x2))
  bond.warp(0, ALLEY_SIDE())
  wait(0.5)
  check("in by the staff entrance: the outer door", door.try(0, m.outer) == true)
  wait(0.6)
  bond.warp(0, IN_INNER())
  wait(0.3)
  door.try(0, m.inner)                    -- queued
  local ok = wait_for(12, function() return open(m.inner) end)
  check("... the inner door after the hold", ok and shut(m.outer))
  bond.warp(0, CORR_SIDE())
  -- the vault
  bond.warp(0, HEIST.M.P.crack)
  wait(0.5)
  bond.test_gadget(0, "doordecoder")
  check("the crack runs", wait_for(3, function() return HEIST.phase == "cracking" end))
  check("the vault opens", wait_for(40, function() return HEIST.phase == "open" end), HEIST.phase)
  local n = 0
  for _, it in ipairs(HEIST.loot_items) do
    if it.where == "vault" and spawn.item_alive(it.h) and n < 4 then bond.warp(0, it.pad, it.dx, it.dz); wait(0.5); n = n + 1 end
  end
  check("gold in hand", HEIST.bars >= 1, tostring(HEIST.bars))
  -- out through the mantrap's dock door: inner (corridor side) -> the mantrap -> the dock door -> exit B
  wait_for(15, function() return interlock.get(m.id).phase == "ready" end)
  bond.warp(0, CORR_SIDE())
  wait(0.5)
  check("out: the inner door from the corridor", door.try(0, m.inner) == true)
  wait(0.6)
  bond.warp(0, DOCK_IN())
  wait(0.3)
  door.try(0, m.doors[3])
  local dk = wait_for(14, function() return open(m.doors[3]) end)
  check("... the dock door after the hold (GREEN verified)", dk and shut(m.inner) and IL.stats.flags == 0, string.format("flags %d", IL.stats.flags))
  bond.warp(0, DOCK_OUT())
  wait(0.5)
  bond.warp(0, HEIST.M.P.exit_b)
  local won = wait_for(6, function() return ended.win ~= nil end)
  check("the win via the staff entrance", won and HEIST.ended, tostring(ended.win))
end

local finished = false
local function body()
  local ready = wait_for(25, function() return HEIST.ready end)
  check("mode ready", ready, "phase=" .. tostring(HEIST.phase))
  wait(1.5)
  if RUN == "guards" then guards() elseif RUN == "tailgate" then tailgate() elseif RUN == "verify" then verify()
  elseif RUN == "power" then powercut() elseif RUN == "win" then win() else basic() end
  wait(0.5)
  local v = 0
  for _, g in ipairs(IL.list) do if g.id then v = v + interlock.get(g.id).violations end end
  check("never two doors of a group open at once (sampled every script tick, 4 Hz)", both == 0 and sampled > 100, string.format("%d of %d samples", both, sampled))
  check("... and the C side counted no violation (it checks every sim tick)", v == 0, tostring(v))
  print(string.format("HEIST DONE %d passed, %d failed", pass, fail))
  finished = true
  if ended.win then real_win(ended.win) else real_fail(ended.fail or "interlock test done") end
end

local co = coroutine.wrap(body)
on("tick", function(dt)
  watch_tick()
  if finished then return end
  local ok, err = pcall(co, dt)
  if not ok then finished = true; print("HEIST FAIL script error: " .. tostring(err)); print(string.format("HEIST DONE %d passed, %d failed", pass, fail + 1)) end
end)
