-- heist_combat.lua (test) - the combat layer and the heat (designs/bank-heist.md section 24), the real mode on the bank, one scenario per
-- run (tests/heist/run_combat.sh):
--   GE_CB_COVER=1    a guard in the hall under fire runs to a cover point that breaks Bond's line, peeks out and shoots (with the line),
--                    ducks back; Bond walks round him: he moves to new cover
--   GE_CB_FLANK=1    two guards near each other in a fight pair up: one suppresses from cover, the other runs round Bond
--   GE_CB_RETREAT=1  a hurt guard (retreat_hp above his health) falls back to cover further from Bond
--   GE_CB_HEAT=1     the alarm: police at the street / dock on the heat timer, they engage; SWAT later, the RED doors shut, the exits stay
--                    open and the crew still gets out
-- Prints "HEIST PASS|FAIL ..." and "HEIST DONE".
local RUN = "cover"
for _, r in ipairs({ "flank", "retreat", "heat" }) do
  if mode.env_flag("GE_CB_" .. r:upper()) then RUN = r end
end
HEIST_SETTINGS = {
  test = true, jam_chance = 0, jam_every = 1000, investigators = 0, generator_after = 0, station_late = 900,
  wave_first = (RUN == "heat") and 999 or 999, response_after = (RUN == "heat") and 16 or 9999,
  ai_tune = { checkin = 0, backup = 0 },
  combat = { police = RUN == "heat", police_at = 4, police_interval = 6, police_size = 2, police_max = 4,
             tune = { flank_chance = (RUN == "flank") and 1 or 0, retreat_hp = (RUN == "retreat") and 1.01 or 0, lose = 12 } },
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
local ended = {}
mission.win = function(msg) ended.win = msg; return true end
mission.fail = function(msg) ended.fail = msg; return true end

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
local barks = {}
on("guard_bark", function(c, kind, text) barks[#barks + 1] = { c = c, kind = kind, text = text } end)
local function said(c, pat) for _, b in ipairs(barks) do if b.c == c and (not pat or tostring(b.text):find(pat)) then return b end end end
local function info(c) return combat.info(c) or { state = -1, name = "?", role = "?", peeks = 0, covers = 0, flanks = 0, cover_x = 0, cover_z = 0 } end
local function heading(fx, fz, tx, tz) return math.deg(math.atan(tx - fx, tz - fz)) end
local function angdiff(a, b) local d = (a - b) % 360; if d > 180 then d = d - 360 end; return math.abs(d) end

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
local function guard_at(x, y, z, fx, fz)
  local best = pad_near(x, y, z)
  if not best then return nil end
  local px, _, pz = pads.pos(best)
  local c = spawn.chr(best, { post = { fx, fz }, weapon = "kf7", body = "St_Petersburg_Guard", offset = { x - px, z - pz }, hearing = 1.0, accuracy = 0 })
  if not c then return nil end
  spawn.role(c, "hall")
  ai.guard(c, 1)
  combat.guard(c, 1)
  return c
end
local function clear_area(area)
  for _, g in ipairs(HEIST.guards) do if g.area == area then spawn.remove(g.chr) end end
  for _, c in ipairs(HEIST.civs) do if c.area == area then spawn.remove(c.chr) end end
end

-- Bond at one end of the emptied hall, gun drawn, no disguise; guard(s) about 900 along the hall facing him
local function hall_fight(n)
  clear_area("hall")
  cover.alert(true)
  local rr = LEVEL.rooms.hall.rect
  local _, y = pads.pos(pad("hall_mid"))
  local long_x = (rr[3] - rr[1]) >= (rr[4] - rr[2])
  local bx, bz, gx, gz
  if long_x then bx, bz, gx, gz = rr[1] + 250, (rr[2] + rr[4]) / 2, rr[1] + 1150, (rr[2] + rr[4]) / 2
  else bx, bz, gx, gz = (rr[1] + rr[3]) / 2, rr[2] + 250, (rr[1] + rr[3]) / 2, rr[2] + 1150 end
  warp_xz(0, bx, y, bz)
  bond.equip(0, "pp7sil")
  wait(0.5)
  local px, _, pz = bond.pos(0)
  local out = {}
  -- on hall pads (waypoints: open floor), 700..1100 from Bond, that Bond can see, at least 200 apart
  local hall = LEVEL.rooms.hall.id
  for _, p in ipairs(pads.far_from(px, pz, 700, 1100)) do
    if #out >= n then break end
    local qx, qy, qz = pads.pos(p)
    local ok = math.abs(qy - y) < 60
    for _, c in ipairs(out) do local cx, cz = spawn.where(c); if d2(cx, cz, qx, qz) < 200 or d2(cx, cz, qx, qz) > 700 then ok = false end end
    if ok then
      local c = guard_at(qx, qy, qz, px, pz)
      if c then
        wait(0.25)
        if spawn.room(c) == hall and combat.los(c) then out[#out + 1] = c else spawn.remove(c) end
      end
    end
  end
  test.face(0, gx, gz)
  wait(1)
  return out, px, pz
end

local function cover_path()
  local gs, bx, bz = hall_fight(1)
  local c = gs[1]
  check("a test guard in the hall, tracked by the combat layer", c ~= nil and combat.info(c) ~= nil and info(c).name == "idle", c and info(c).name or "none")
  if not c then return end
  local x0, z0 = spawn.where(c)
  check("Bond has the line to him at the start", combat.los(c), string.format("%.0f apart", d2(x0, z0, bx, bz)))
  combat.under_fire(c)
  local hid = wait_for(8, function() return info(c).name == "hide" end)
  local x, z = spawn.where(c)
  local i = info(c)
  check("under fire, he runs to cover and crouches", hid and i.covers >= 1, string.format("%s, moved %.0f, cover %.0f,%.0f", i.name, d2(x, z, x0, z0), i.cover_x, i.cover_z))
  check("... at the cover point", d2(x, z, i.cover_x, i.cover_z) < 110, string.format("%.0f from it", d2(x, z, i.cover_x, i.cover_z)))
  check("... which breaks Bond's line of sight", hid and not combat.los(c), "los " .. tostring(combat.los(c)))
  check("... and barks (\"Taking fire! Get to cover!\")", said(c, "cover") ~= nil, said(c, "cover") and said(c, "cover").text or "none")
  local los_shooting = false
  local peeked = wait_for(10, function()
    if info(c).name == "shoot" and combat.los(c) then los_shooting = true end
    return info(c).peeks >= 1 and los_shooting
  end)
  check("he peeks out and shoots, with the line to Bond", peeked, string.format("peeks %d, %s", info(c).peeks, info(c).name))
  local back = wait_for(8, function() return info(c).name == "hide" end)
  check("... and ducks back into cover", back and not combat.los(c), info(c).name)
  -- Bond walks round him: find a spot with the line to him in cover
  local cx, cz = spawn.where(c)
  local _, y = bond.pos(0)
  local found = false
  for k = 0, 15 do
    local a = k * math.pi / 8
    for _, r in ipairs({ 350, 500, 650 }) do
      if warp_xz(0, cx + math.sin(a) * r, y, cz + math.cos(a) * r) then
        wait(0.1)
        if combat.los(c) then found = true; break end
      end
    end
    if found then break end
  end
  check("(Bond finds a spot that sees him in cover)", found)
  if not found then return end
  local m0 = combat.counts().moves
  local cov0 = info(c).covers
  local moved = wait_for(6, function() return combat.counts().moves > m0 or info(c).covers > cov0 or info(c).name == "shoot" end)
  check("flanked in cover, he moves (or shoots it out)", moved, string.format("moves %d->%d, covers %d->%d, %s", m0, combat.counts().moves, cov0, info(c).covers, info(c).name))
end

local function flank_path()
  local gs, bx, bz = hall_fight(2)
  local a, b = gs[1], gs[2]
  check("two test guards in the hall", a ~= nil and b ~= nil)
  if not (a and b) then return end
  combat.under_fire(a)
  combat.under_fire(b)
  local paired = wait_for(10, function() return combat.counts().flanks >= 1 end)
  local f, s = a, b
  if info(b).role == "flank" then f, s = b, a end
  check("in a fight together, they pair up", paired and info(f).role == "flank" and info(s).role == "suppress" and info(f).pair == s,
        string.format("roles %s / %s, flanks %d", info(a).role, info(b).role, combat.counts().flanks))
  if not paired then return end
  check("... with the lines (\"Cover me!\" / \"Covering!\")", said(f, "Cover me") ~= nil and said(s, "Covering") ~= nil)
  local px, _, pz = bond.pos(0)
  local fx0, fz0 = spawn.where(f)
  local sx0, sz0 = spawn.where(s)
  local h0 = heading(px, pz, sx0, sz0)
  local around = wait_for(10, function()
    local x, z = spawn.where(f)
    return x and angdiff(heading(px, pz, x, z), h0) >= 40 and info(f).name ~= "flank"
  end)
  local fx, fz = spawn.where(f)
  check("the flanker goes round Bond (40+ degrees from his partner's side)", around,
        string.format("%.0f deg round, moved %.0f, %s", angdiff(heading(px, pz, fx, fz), h0), d2(fx, fz, fx0, fz0), info(f).name))
  local sx, sz = spawn.where(s)
  check("... while his partner holds his cover and shoots", d2(sx, sz, sx0, sz0) < 260 and info(s).peeks >= 1,
        string.format("moved %.0f, peeks %d, %s", d2(sx, sz, sx0, sz0), info(s).peeks, info(s).name))
  check("the flanker fires from the flank", info(f).peeks >= 1, "peeks " .. info(f).peeks)
end

local function retreat_path()
  local gs, bx, bz = hall_fight(1)
  local c = gs[1]
  if not c then check("a test guard", false); return end
  combat.under_fire(c)
  wait_for(3, function() return info(c).name ~= "idle" end)
  local x0, z0 = spawn.where(c)   -- where he was when he broke off (his own list may have walked him in first)
  local d0 = d2(x0, z0, bx, bz)
  local went = wait_for(8, function() return info(c).retreated and info(c).name == "hide" end)
  local x, z = spawn.where(c)
  check("hurt (past retreat_hp), he falls back to cover further from Bond", went and d2(x, z, bx, bz) > d0 + 100,
        string.format("%.0f -> %.0f from Bond, %s", d0, d2(x, z, bx, bz), info(c).name))
  check("... \"I'm hit! Falling back!\"", said(c, "Falling back") ~= nil)
  check("... out of Bond's line", not combat.los(c))
end

local function heat_path()
  local M = HEIST.M
  bond.warp(0, M.P.start[1])
  bond.equip(0, "pp7sil")
  alarm.start()
  check("the alarm: hot", wait_for(2, function() return HEIST.hot end))
  local P = HEIST_COMBAT.police
  check("the police controller is up", P ~= nil)
  if not P then return end
  local in1 = wait_for(8, function() return combat.stage() >= 1 and P.spawned >= 1 end)
  check("police arrive on the heat timer (4 s in the test)", in1, string.format("stage %d, %d spawned at %.1f s", combat.stage(), P.spawned, HEIST.elapsed - HEIST.hot_at))
  -- Bond where the first officer can see him
  local cop
  for chr in pairs(P.mine) do cop = cop or chr end
  local engaged = false
  if cop then
    local cx, cz = spawn.where(cop)
    local _, cy = pads.pos(M.P.waves_front[1])
    for _, p in ipairs(pads.far_from(cx, cz, 600, 1100)) do
      local px, py, pz = pads.pos(p)
      if math.abs(py - cy) < 80 then
        bond.warp(0, p)
        wait(0.1)
        if combat.los(cop) then break end
      end
    end
    engaged = wait_for(15, function()
      for chr in pairs(P.mine) do if info(chr).covers >= 1 or info(chr).name ~= "idle" then return true end end
      return false
    end)
  end
  check("... and engage with the combat layer (cover)", engaged, string.format("covers %d, peeks %d", combat.counts().covers, combat.counts().peeks))
  check("the police megaphone line", HEIST_COMBAT.said_police and #barks > 0)
  local swat = wait_for(20, function() return combat.stage() >= 2 end)
  check("SWAT on the heat timer", swat, string.format("stage %d at %.1f s", combat.stage(), HEIST.elapsed - HEIST.hot_at))
  wait(2)
  local shut = true
  for _, d in ipairs(M.DOORS.red or {}) do local g = door.get(d); if g and g.state == "open" then shut = false end end
  check("lockdown: the vault-stair (RED) doors swing shut", shut)
  local exits_ok = true
  for _, d in ipairs(M.DOORS.front or {}) do local g = door.get(d); if not g or g.keyflags ~= 0 then exits_ok = false end end
  local dock = LEVEL.objects.dock_door
  if dock and door.get(dock.index).keyflags ~= 0 then exits_ok = false end
  check("the exits stay open (no card on the front doors or the dock door)", exits_ok)
  check("police waves keep coming", P.spawned >= 2, P.spawned .. " spawned")
  -- the crew gets out with the gold
  HEIST.loot = HEIST.loot + 150000
  bond.warp(0, M.P.exit_b)
  local won = wait_for(5, function() return ended.win ~= nil end)
  check("escape at the dock with the heat on: the crew gets out", won and HEIST.ended, tostring(ended.win))
end

local finished = false
local function body()
  local ready = wait_for(25, function() return HEIST.ready end)
  check("mode ready", ready, "run " .. RUN)
  check("the combat layer is on", combat ~= nil and combat.on())
  wait(1.5)
  if RUN == "cover" then cover_path() elseif RUN == "flank" then flank_path() elseif RUN == "retreat" then retreat_path()
  elseif RUN == "heat" then heat_path() end
  local c = combat.counts()
  print(string.format("HEIST   counts: covers %d peeks %d flanks %d retreats %d moves %d releases %d barks %d searches %d fails %d",
        c.covers, c.peeks, c.flanks, c.retreats, c.moves, c.releases, c.barks, c.searches, c.fails))
  print(string.format("HEIST DONE %d passed, %d failed", pass, fail))
  finished = true
  real_fail("combat test done")
end

local co = coroutine.wrap(body)
on("tick", function(dt)
  if finished then return end
  local ok, err = pcall(co, dt)
  if not ok then finished = true; print("HEIST FAIL script error: " .. tostring(err)); print(string.format("HEIST DONE %d passed, %d failed", pass, fail + 1)) end
end)
