-- heist_hostage.lua (test) - hostages and the human shield (designs/bank-heist.md section 31), the real mode on the bank, one scenario per
-- run (tests/heist/run_hostage.sh):
--   GE_HG_GRAB=1      a grab needs a compliant civilian (an idle one: no), a one-handed gun (the slappers: no), Bond beside or behind it
--                     (in front: no); then the hostage is pinned in front of him, its own list off; USE lets it go (it cowers); a
--                     two-handed gun lets go; a hostage killed while held is hurt and fined
--   GE_HG_FIRE=1      a guard with the hostage in his line holds every shot (0 shots fired with it in the line, counted here from the
--                     guard's own fire stats and the positions); turned side-on (a clear line) he fires; a combat guard goes round
--   GE_HG_STANDOFF=1  the alarm, the police on the timer; a hostage in a police officer's line: the stand-off (30 s): no police wave in
--                     it, they come again after it; once only for that hostage
--   GE_HG_DEBRIEF=1   a quiet run with a hostage taken and let go: the debrief fines it, caps the rating at COWBOY, Hostile Takeover
--   GE_HG_OFF=1       S.hostages = false: the layer is off, a use press behind a surrendered civilian does nothing
-- Prints "HEIST PASS|FAIL ..." and "HEIST DONE".
local RUN = "grab"
for _, r in ipairs({ "fire", "standoff", "debrief", "off" }) do
  if mode.env_flag("GE_HG_" .. r:upper()) then RUN = r end
end
HEIST_SETTINGS = {
  test = true, jam_chance = 0, jam_every = 1000, investigators = 0, generator_after = 0,
  wave_first = 999, response_after = 9999,
  ai_tune = { checkin = 0, backup = 0 },
  combat = { police = RUN == "standoff", police_at = 4, police_interval = 5, police_size = 1, police_max = 12,
             tune = { flank_chance = 0, retreat_hp = 0, lose = 12 } },
  hostages = { tune = { drop = 0 } },
}
if RUN == "off" then HEIST_SETTINGS.hostages = false end
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
local REPORT
if HEIST.debrief_end then
  local orig = HEIST.debrief_end
  HEIST.debrief_end = function(...) REPORT = orig(...); return REPORT end
end

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
local barks = {}
on("guard_bark", function(c, kind, text) barks[#barks + 1] = { c = c, kind = kind, text = text } end)
local function said(c, pat) for _, b in ipairs(barks) do if (not c or b.c == c) and (not pat or tostring(b.text):find(pat)) then return b end end end

local function pad_near(x, y, z)
  local best, bd = nil, 1e9
  for _, p in ipairs(pads.far_from(x, z, 0, 600)) do
    local px, py, pz = pads.pos(p)
    local dd = d2(px, pz, x, z)
    if dd < bd and math.abs(py - y) < 100 then best, bd = p, dd end
  end
  return best
end
local function warp_xz(p, x, y, z)
  local best = pad_near(x, y, z)
  if not best then return false end
  local px, _, pz = pads.pos(best)
  return bond.warp(p, best, x - px, z - pz)
end
local function clear_area(area, civs_too)
  for _, g in ipairs(HEIST.guards) do if g.area == area then spawn.remove(g.chr) end end
  if civs_too then for _, c in ipairs(HEIST.civs) do if c.area == area then spawn.remove(c.chr) end end end
end
local function civ_in(area)
  for _, c in ipairs(HEIST.civs) do
    if c.area == area and spawn.alive(c.chr) and not ko.is(c.chr) and not (HEIST.carriers and false) then return c.chr end
  end
end
-- the civilian's facing, as a unit vector
local function fwd(c) local f = math.rad(shield.face(c)); return math.sin(f), math.cos(f) end
-- Bond 0 at `dist` from civilian c on its side `side` (1 front, -1 behind, 0 beside), facing it
local function stand(c, side, dist)
  local x, z = spawn.where(c)
  local _, y = bond.pos(0)
  local fx, fz = fwd(c)
  local ox, oz = fx * side, fz * side
  if side == 0 then ox, oz = fz, -fx end
  warp_xz(0, x + ox * dist, y, z + oz * dist)
  bond.face(0, x, z)
end
local function surrender(c)
  bond.equip(0, "pp7sil")
  stand(c, 1, 300)
  return wait_for(4, function() return civilian.state(c) == "surrendered" end)
end
-- the whole grab: surrendered, then behind it, a use press (the C path a use press at nothing takes)
local function take(c)
  if not surrender(c) then return false end
  stand(c, -1, 70)
  wait(0.1)
  return shield.grab(0)
end

-- the hostage of Bond 0 is in the line from guard g to Bond 0 (the same test as the layer's, from the positions)
local function in_line(g, h)
  local gx, gz = spawn.where(g)
  local bx, _, bz = bond.pos(0)
  local hx, hz = spawn.where(h)
  if not (gx and hx) then return false end
  local dx, dz = bx - gx, bz - gz
  local len2 = dx * dx + dz * dz
  local t = ((hx - gx) * dx + (hz - gz) * dz) / len2
  local ex, ez = hx - gx - t * dx, hz - gz - t * dz
  return t > 0 and t < 1 and ex * ex + ez * ez <= 34 * 34   -- (a body is about 30 wide: shield.tune line 34)
end

local function grab_path()
  clear_area("hall")
  local c = civ_in("hall")
  check("a civilian in the hall", c ~= nil)
  if not c then return end
  -- 1. idle (not surrendered): no
  bond.equip(0, "pp7sil")
  stand(c, -1, 70)
  wait(0.25)
  check("an idle civilian cannot be grabbed", civilian.state(c) == "idle" and not shield.grab(0) and shield.held(0) == nil, tostring(civilian.state(c)))
  -- 2. surrendered, behind it, slappers in hand: no ("nogun")
  check("aimed at, it surrenders", surrender(c), tostring(civilian.state(c)))
  bond.equip(0, "fist"); wait(1.2)
  stand(c, -1, 70)
  wait(0.25)
  local ok = shield.grab(0)
  check("no gun drawn (the slappers): no grab", not ok and shield.last(0) == "nogun", shield.last(0))
  -- 3. in front of it with the gun: no ("front")
  bond.equip(0, "pp7sil"); wait(1.2)
  stand(c, 1, 70)
  wait(0.25)
  ok = shield.grab(0)
  check("in front of it: no grab (get beside or behind)", not ok and shield.last(0) == "front", shield.last(0))
  -- 4. beside it with the PP7: yes
  stand(c, 0, 70)
  wait(0.1)
  ok = shield.grab(0)
  check("beside it, the PP7 drawn, a use press: a hostage", ok and shield.held(0) == c and shield.holder(c) == 0, tostring(shield.held(0)))
  wait(1)
  local pf = HEIST.pflash[0]
  check("the HUD tells him", pf and tostring(pf[1]):find("HOSTAGE") ~= nil, pf and pf[1] or "none")
  local bx, _, bz = bond.pos(0)
  local hx, hz = spawn.where(c)
  check("pinned in front of him (about 45 units)", d2(bx, bz, hx, hz) > 15 and d2(bx, bz, hx, hz) < 70, string.format("%.0f units", d2(bx, bz, hx, hz)))
  -- he turns: it stays in front
  bond.face(0, bx + 500, bz)
  wait(0.5)
  hx, hz = spawn.where(c)
  check("he turns east: it is east of him", hx - bx > 25 and math.abs(hz - bz) < 25, string.format("dx %.0f dz %.0f", hx - bx, hz - bz))
  -- 5. USE again lets go; it cowers
  ok = shield.grab(0)   -- (a use press at nothing: while holding, the release)
  bond.face(0, bx - 500, bz)   -- (his gun off it: pointed at, a cowering civilian puts its hands up again)
  wait(0.5)
  local inf = shield.info(c) or {}
  check("USE again: let go, unharmed", shield.held(0) == nil and inf.why == "use" and inf.clean, string.format("%s %s", tostring(inf.why), tostring(inf.clean)))
  check("... it cowers", civilian.state(c) == "cowering", tostring(civilian.state(c)))
  -- 6. a two-handed gun lets go
  stand(c, -1, 70)
  wait(0.1)
  ok = shield.grab(0)
  check("re-taken (cowering is compliant)", ok and shield.held(0) == c)
  bond.give(0, "kf7")
  bond.equip(0, "kf7"); wait(1.2)
  wait(0.75)
  inf = shield.info(c) or {}
  check("a two-handed gun (KF7): let go", shield.held(0) == nil and inf.why == "weapon", tostring(inf.why))
  -- 7. a hostage killed while held: hurt, released, fined
  bond.equip(0, "pp7sil"); wait(1.2)
  stand(c, -1, 70)
  wait(0.1)
  ok = shield.grab(0)
  local k0 = civilian.killed()
  spawn.kill(c)
  wait(0.75)
  inf = shield.info(c) or {}
  local R = HEIST.hostage_report()
  check("a hostage killed while held: let go, hurt", ok and shield.held(0) == nil and inf.hurt and inf.why == "hostage_down", tostring(inf.why))
  check("... the death counts and the report fines it (hurt fine)", civilian.killed() == k0 + 1 and R.hurt == 1 and R.fines >= HEIST_HOSTAGES.T.hurt_fine,
        string.format("killed %d, fines %d (%d taken, %d takes, %.1f s)", civilian.killed(), R.fines, R.taken, R.takes, R.secs))
end

local function fire_path()
  clear_area("hall")
  cover.alert(true)
  local c = civ_in("hall")
  if not c then check("a civilian in the hall", false); return end
  check("a hostage", take(c))
  -- a stock guard (not the combat layer: he stands and shoots) 450..900 from Bond on a hall pad he sees; Bond faces him (the hostage
  -- between them)
  local bx, by, bz = bond.pos(0)
  local g
  for _, p in ipairs(pads.far_from(bx, bz, 450, 900)) do
    local px, py, pz = pads.pos(p)
    if math.abs(py - by) < 60 then
      g = spawn.chr(p, { post = { bx, bz }, weapon = "kf7", body = "St_Petersburg_Guard", accuracy = 0 })
      if g then
        wait(0.25)
        if combat.los(g) and spawn.room(g) == LEVEL.rooms.hall.id then break end
        spawn.remove(g); g = nil
      end
    end
  end
  check("a guard in Bond's line, the hostage between them", g ~= nil)
  if not g then return end
  shield.tune{ flank = 0 }
  local gx, gz = spawn.where(g)
  local f0, h0 = spawn.fire_stats(g)
  local through, held_ticks, fired_last = 0, 0, f0
  local t = 0
  while t < 10 do
    bond.face(0, gx, gz)
    wait(0.25); t = t + 0.25
    local f = spawn.fire_stats(g)
    if f > fired_last and in_line(g, c) then through = through + (f - fired_last) end
    if in_line(g, c) then held_ticks = held_ticks + 1 end
    fired_last = f
  end
  local f1, h1 = spawn.fire_stats(g)
  check("facing him, the hostage in his line: 0 shots fired through it", through == 0 and held_ticks >= 30,
        string.format("%d shots in line, %d of 40 samples in line, fired %d held %d", through, held_ticks, f1 - f0, h1 - h0))
  check("... he holds his shots (the hold counted)", h1 - h0 > 0 and shield.counts().held_shots > 0, string.format("held %d", h1 - h0))
  check("... and barks (\"Hold your fire!\" / \"Let them go!\")", said(g, "Hold your fire") ~= nil or said(g, "Let ") ~= nil,
        said(g) and said(g).text or "none")
  check("... the hostage is unhurt", not (shield.info(c) or {}).hurt and spawn.alive(c))
  if mode.env_flag("GE_HG_SHOT") then   -- the screenshot (section 31): the shield, the AR bracket, the guard holding his fire
    local a = math.atan(gx - bx, gz - bz) + math.rad(14)   -- (a little off him: the hostage still in his line, both in view)
    bond.face(0, bx + math.sin(a) * 500, bz + math.cos(a) * 500)
    ar.say(g, "Hold your fire! He's got a hostage!", 3, "hey")
    wait(0.5)
    cctv.test_shot()
    print("HEIST SHOT hostage")
    wait(0.25)
  end
  -- side-on: the hostage out of his line, he fires
  local ux, uz = (gx - bx) / d2(gx, gz, bx, bz), (gz - bz) / d2(gx, gz, bx, bz)
  local sx, sz = bx + uz * 500, bz - ux * 500
  local f2 = spawn.fire_stats(g)
  local c0 = shield.counts().clear_shots
  t = 0
  local clear_samples = 0
  while t < 8 do
    bond.face(0, sx, sz)
    wait(0.25); t = t + 0.25
    if not in_line(g, c) then clear_samples = clear_samples + 1 end
  end
  local f3 = spawn.fire_stats(g)
  check("side-on (a clear line): he fires", f3 > f2 and shield.counts().clear_shots > c0 and clear_samples >= 24,
        string.format("fired %d, clear shots %d, %d of 32 samples clear", f3 - f2, shield.counts().clear_shots - c0, clear_samples))
  -- a combat guard held in line goes round for a clear shot
  combat.guard(g, 1)
  shield.tune{ flank = 1, flank_after = 1 }
  local fl0 = shield.counts().flanks
  local went = wait_for(10, function() bond.face(0, gx, gz); return shield.counts().flanks > fl0 end)
  check("a combat guard with the hostage in his line goes round for a clear shot", went, string.format("flanks %d, %s", shield.counts().flanks,
        combat.info(g) and combat.info(g).name or "?"))
  if went then
    local x0, z0 = spawn.where(g)
    local f4 = spawn.fire_stats(g)
    local moved = wait_for(16, function() bond.face(0, gx, gz); return not in_line(g, c) and spawn.fire_stats(g) > f4 end)
    local x1, z1 = spawn.where(g)
    check("... and fires from there, the hostage out of his line", moved, string.format("fired %d, moved %.0f, %s", spawn.fire_stats(g) - f4,
          d2(x0, z0, x1, z1), combat.info(g) and combat.info(g).name or "?"))
  end
  shield.grab(0)
end

local function standoff_path()
  local M = HEIST.M
  bond.warp(0, M.P.start[1])
  bond.equip(0, "pp7sil")
  alarm.start()
  check("the alarm: hot", wait_for(2, function() return HEIST.hot end))
  local P = HEIST_COMBAT.police
  check("the police controller is up", P ~= nil)
  if not P then return end
  check("police on the timer", wait_for(8, function() return combat.stage() >= 1 and P.spawned >= 1 end), P.spawned .. " spawned")
  local cop
  for chr in pairs(P.mine) do cop = cop or chr end
  -- a spot the first officer sees, a civilian placed there, made to surrender and taken
  local cx, cz = spawn.where(cop)
  local _, cy = pads.pos(M.P.waves_front[1])
  local spot
  for _, p in ipairs(pads.far_from(cx, cz, 500, 1100)) do
    local px, py, pz = pads.pos(p)
    if math.abs(py - cy) < 80 then
      bond.warp(0, p)
      wait(0.1)
      if combat.los(cop) then spot = p; break end
    end
  end
  check("a spot the officer sees", spot ~= nil)
  if not spot then return end
  local sx, _, sz = pads.pos(spot)
  bond.warp(0, M.P.start[1])   -- (out of the way while the civilian is set up)
  local civ = spawn.chr(spot, { behaviour = "civilian", body = "Civilian_2" })
  wait(0.5)
  check("a civilian at the spot", civ ~= nil and civilian.state(civ) ~= nil)
  if not civ then return end
  local s0 = P.spawned
  local ok = take(civ)
  check("taken as a hostage", ok)
  if not ok then return end
  local function face_cop() local x, z = spawn.where(cop); if x then bond.face(0, x, z) end end
  local stood, st = wait_for(6, function() face_cop(); return (shield.info(civ) or {}).standoff end)
  check("in the officer's line: the stand-off", stood and HEIST.standoff == 30 and HEIST_HOSTAGES.standoffs == 1,
        string.format("standoff %s, clock +%s, after %.1f s", tostring(stood), tostring(HEIST.standoff), st))
  check("... the negotiator", (HEIST.flash or ""):find("talk about this") ~= nil, tostring(HEIST.flash))
  local s1, t1 = P.spawned, HEIST.elapsed
  wait_for(26, function() face_cop(); return P.spawned > s1 end)
  check("no police wave during the stand-off (every 5 s before it)", P.spawned == s1 and HEIST.standoff_hold(),
        string.format("spawned %d -> %d in %.0f s (before: %d by the grab)", s1, P.spawned, HEIST.elapsed - t1, s0))
  local back = wait_for(12, function() face_cop(); return P.spawned > s1 end)
  check("after it the police come again", back and not HEIST.standoff_hold(), string.format("spawned %d at +%.0f s", P.spawned, HEIST.elapsed - t1))
  wait(6)
  check("once per hostage: no second stand-off", HEIST.standoff == 30 and HEIST_HOSTAGES.standoffs == 1, tostring(HEIST.standoff))
  -- let go, unharmed; out with the gold: Negotiator
  shield.grab(0)
  wait(0.5)
  local inf = shield.info(civ) or {}
  check("let go unharmed", shield.held(0) == nil and inf.clean, tostring(inf.why))
  HEIST.loot = HEIST.loot + 150000
  bond.warp(0, M.P.exit_b)
  local won = wait_for(5, function() return ended.win ~= nil end)
  local has = function(t, n) for _, v in ipairs(t or {}) do if v == n then return true end end return false end
  check("out: Negotiator and Hostile Takeover", won and REPORT and has(REPORT.commendations, "Negotiator") and has(REPORT.commendations, "Hostile Takeover"),
        REPORT and table.concat(REPORT.commendations, ",") or "no report")
end

local function debrief_path()
  for _, g in ipairs(HEIST.guards) do if g.area == "hall" or g.area == "street" then spawn.remove(g.chr) end end
  for _, cam in ipairs(cctv.list()) do cctv.disable(cam) end
  local c = civ_in("hall")
  if not c then check("a civilian in the hall", false); return end
  check("a hostage (quiet: nobody sees)", take(c))
  wait(12)
  shield.grab(0)
  wait(0.5)
  local inf = shield.info(c) or {}
  check("let go after about 12 s", shield.held(0) == nil and inf.secs > 11 and inf.secs < 14, string.format("%.1f s", inf.secs or -1))
  local R = HEIST.hostage_report()
  local want = math.floor((HEIST_HOSTAGES.T.fine_take + inf.secs / 60 * HEIST_HOSTAGES.T.fine_minute) / 1000 + 0.5) * 1000
  check("the fine: one take + the minutes held", R.fines == want and R.fines > 0, string.format("%d (want %d)", R.fines, want))
  HEIST.loot = HEIST.loot + 300000
  bond.warp(0, HEIST.M.P.exit_a)
  local won = wait_for(5, function() return ended.win ~= nil end)
  check("out with the gold", won and REPORT ~= nil, tostring(ended.win))
  if not REPORT then return end
  local has = function(t, n) for _, v in ipairs(t or {}) do if v == n then return true end end return false end
  check("no alarm, nobody killed (else PROFESSIONAL or GHOST)", not REPORT.alarm and REPORT.guards_killed == 0 and REPORT.civ_killed == 0,
        string.format("alarm %s killed %d/%d", tostring(REPORT.alarm), REPORT.guards_killed, REPORT.civ_killed))
  check("rating capped: COWBOY", REPORT.rating == "COWBOY", REPORT.rating)
  check("the payout docked: loot + quiet bonus - the hostage fine", HEIST.score == HEIST.loot + math.floor(HEIST.loot * 0.25) - R.fines
        and REPORT.payout == HEIST.score and REPORT.hostage_fines == R.fines,
        string.format("score %d loot %d fines %d", HEIST.score, HEIST.loot, R.fines))
  check("Hostile Takeover, no Negotiator (no stand-off)", has(REPORT.commendations, "Hostile Takeover") and not has(REPORT.commendations, "Negotiator"),
        table.concat(REPORT.commendations, ","))
end

local function off_path()
  check("S.hostages = false: the layer is off", shield and not shield.on())
  clear_area("hall")
  local c = civ_in("hall")
  if not c then check("a civilian in the hall", false); return end
  check("aimed at, it surrenders", surrender(c))
  stand(c, -1, 70)
  wait(0.1)
  check("behind it, PP7 drawn: no hostage (the old game)", not shield.grab(0) and shield.held(0) == nil and #shield.list() == 0)
  check("no HOSTAGES card on the board, no report", HEIST.hostage_report == nil and HEIST.hostage_note == nil)
end

local finished = false
local function body()
  local ready = wait_for(25, function() return HEIST.ready end)
  check("mode ready", ready, "run " .. RUN)
  if RUN ~= "off" then check("the hostage layer is on", shield ~= nil and shield.on()) end
  wait(1.5)
  if RUN == "grab" then grab_path() elseif RUN == "fire" then fire_path() elseif RUN == "standoff" then standoff_path()
  elseif RUN == "debrief" then debrief_path() elseif RUN == "off" then off_path() end
  local k = shield.counts()
  print(string.format("HEIST   counts: grabs %d releases %d refused %d held_shots %d clear_shots %d drops %d flanks %d barks %d",
        k.grabs, k.releases, k.refused, k.held_shots, k.clear_shots, k.drops, k.flanks, k.barks))
  print(string.format("HEIST DONE %d passed, %d failed", pass, fail))
  finished = true
  real_fail("hostage test done")
end

local co = coroutine.wrap(body)
on("tick", function(dt)
  if finished then return end
  local ok, err = pcall(co, dt)
  if not ok then finished = true; print("HEIST FAIL script error: " .. tostring(err)); print(string.format("HEIST DONE %d passed, %d failed", pass, fail + 1)) end
end)
