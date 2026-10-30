-- Smoke of the SNIPER DUEL v2 on the snowfield (a hidden run: tests/sniper/win_smoke_v2.sh). The real ladder (fewer rounds and shorter
-- waits by default), Bond on b1. The test turns Bond's view round every few seconds (so arrivals and departures meet a moving view
-- cone) and checks every arrival and departure itself: at the moment a marksman appears or vanishes it asks whether Bond could see the
-- spot (its own copy of the rule: the view cone at the current field of view, then the drawn ground) and logs SMOKE VIOLATION if so.
-- Env:
--   (default)          the win path: Bond invulnerable (GE_CHEATS=invuln); each marksman is shot dead once he has fired twice
--   GE_SNIPER_LOSE=1   the loss path: Bond mortal, nobody shoots back, the marksmen hit often: Bond dies after S.hits[difficulty] hits
--   GE_SNIPER_CLOSE=1  close range: perches 85 to 110 m and Bond walked up to within 60 m of one; logs the hit coin per shot
--   GE_SNIPER_SHOTS=1  pictures: the glint and the after-shot marker through the scope, the binoculars
SNIPER2_SETTINGS = { first_delay = { 1, 2 }, second_after = { 2, 4 }, hide = { 2, 3 }, breather = 2 }
local LOSE = mode.env_flag("GE_SNIPER_LOSE")
local CLOSE = mode.env_flag("GE_SNIPER_CLOSE")
local SHOTS = mode.env_flag("GE_SNIPER_SHOTS")
if LOSE then
  local hard = { foe = 1, snipers = 1, hit = 0.9, glint = 1.0, dwell = { 30, 40 }, volley_gap = { 2, 3 } }
  SNIPER2_SETTINGS.rounds_table = { hard, hard, hard }
end
if CLOSE then
  SNIPER2_SETTINGS.perch_band = { 85, 110 }
  local near = { foe = 1, snipers = 1, hit = 0.2, glint = 1.0, dwell = { 60, 80 }, volley_gap = { 3, 4 } }
  SNIPER2_SETTINGS.rounds_table = { near }
end
local DETECT = mode.env_flag("GE_SNIPER_DETECT")   -- the detection meter: Bond stands, runs, scopes, goes prone, hides
if DETECT then
  local slow = { foe = 1, snipers = 1, hit = 0.0, glint = 1.0, dwell = { 200, 200 }, volley_gap = { 3, 4 } }
  SNIPER2_SETTINGS.rounds_table = { slow }
  SNIPER2_SETTINGS.detect_after = 0.0
end
local P3 = mode.env_flag("GE_SNIPER_P3")   -- phase 3: a quick storm cycle, the thermal view (Q pressed by the test), the flush-out
if P3 then
  SNIPER2_SETTINGS.storm_calm, SNIPER2_SETTINGS.storm_hold, SNIPER2_SETTINGS.storm_ramp = { 6, 8 }, { 10, 12 }, 4
  SNIPER2_SETTINGS.thermal_cooldown = 10
end
include("sniper-v2")
local ST, S = SNIPER2, SNIPER2.S

-- grunts of a flush-out: shot once they have been coming for 10 s (the log shows them closing in first); the thermal presses
local grunt_seen = {}
local p3_t, q_done, snap_th, snap_storm = 0, {}, false, false
on("tick", function(dt)
  p3_t = p3_t + dt
  if ST.phase == "flush" then
    for _, g in ipairs(ST.grunts or {}) do
      if g.chr and spawn.alive(g.chr) then
        grunt_seen[g.chr] = grunt_seen[g.chr] or p3_t
        if not LOSE and p3_t - grunt_seen[g.chr] >= 10 then
          for _ = 1, 20 do if spawn.alive(g.chr) then test.shoot(g.chr, "body", "ak47", 0) end end
          if spawn.alive(g.chr) then spawn.kill(g.chr) end
          print("SMOKE shot a grunt down")
        end
      end
    end
  end
  if P3 then
    for _, at in ipairs({ 20, 22, 36 }) do   -- on, again while on, again after it went off (recharging or ready)
      if p3_t >= at and not q_done[at] then q_done[at] = true; test.sniper_q(0); print(string.format("SMOKE pressed Q at %.0f s", p3_t)) end
    end
    if mode.env_flag("GE_SNIPER_SHOTS") then
      if ST.thermal_until and not snap_th and ST.heat and #ST.heat > 0 then
        local f = ST.foes[1]
        if f and f.perch then
          local _, _, _, ex, ey, ez = pcall(gadget.aim, 0)
          local d = math.sqrt((f.perch.x - ex) ^ 2 + (f.perch.z - ez) ^ 2)
          test.aim(0, true, 12)
          pcall(bench.look, 0, math.deg(math.atan(-(f.perch.x - ex), (f.perch.z - ez))), math.deg(math.atan((f.perch.y + 200) - ey, d)))
        end
        snap_th = p3_t
      end
      if snap_th and snap_th ~= true and p3_t >= snap_th + 0.75 then snap_th = true; bench.shot(); print("SMOKE shot thermal") end
      if (ST.storm or 0) >= 1 and not snap_storm then snap_storm = true; bench.shot(); print("SMOKE shot storm") end
    end
  end
end)

-- the detection run: phases of 7 s once the marksman is on his perch with a line to Bond; the gain (meter a second) averaged per phase
if DETECT then
  local phases = { "stand", "run", "scope", "prone", "hidden", "tag" }
  local tag_init = false
  local ph_i, ph_t, sum, n, x0, z0, hide_pad = 0, 0, 0, 0, nil, nil, nil
  local tt = 0
  on("tick", function(dt)
    tt = tt + dt
    local f = ST.foes[1]
    if not (f and f.chr and f.perch) then pcall(bench.look, 0, 180, 0); return end
    local _, _, _, ex, ey, ez = pcall(gadget.aim, 0)
    if ph_i == 0 then
      if not f.line_t then return end
      ph_i, ph_t = 1, tt
      x0, _, z0 = bond.pos(0)
      print(string.format("SMOKE detect: foe on %s, phases begin", f.perch.name))
    end
    local name = phases[ph_i]
    if not name then return end
    local p = f.perch
    local yaw = math.deg(math.atan(-(p.x - ex), (p.z - ez)))
    local pitch = math.deg(math.atan((p.y + 300) - ey, math.sqrt((p.x - ex) ^ 2 + (p.z - ez) ^ 2)))
    if name == "stand" then pcall(bench.look, 0, yaw, pitch)
    elseif name == "run" then
      local tx = x0 + ((math.floor((tt - ph_t) / 2) % 2 == 0) and 2500 or -2500)
      test.walk(0, tx, z0)
    elseif name == "scope" then test.walk_stop(); test.aim(0, true, 7); pcall(bench.look, 0, yaw, pitch)
    elseif name == "prone" then test.aim(0, false, 0); if not sniper.prone(0) then ST.set_prone(true) end
    elseif name == "hidden" then
      if not hide_pad then   -- a pad near Bond the marksman's perch cannot see (the drawn ground in the way)
        local bx, _, bz = bond.pos(0)
        for _, id in ipairs(LEVEL.grid) do
          local x, y, z = pads.pos(id)
          if math.sqrt((x - bx) ^ 2 + (z - bz) ^ 2) < 6000 and not probe.los(p.x, p.y + 310, p.z, x, y + 60, z) and not probe.los(p.x, p.y + 310, p.z, x, y + 165, z) then hide_pad = id; break end
        end
        if hide_pad then bond.warp(0, hide_pad); print("SMOKE detect: hidden on pad " .. hide_pad) else print("SMOKE detect: no hidden pad near") end
      end
    elseif name == "tag" then   -- back on b1, standing, the binoculars on him
      if not tag_init then
        tag_init = true
        ST.set_prone(false)
        bond.warp(0, LEVEL.pads.b1)
        pcall(bond.equip, 0, "camera")
      end
      if tt - ph_t >= 2 then test.aim(0, true, S.binoc_min_fov); pcall(bench.look, 0, yaw, math.deg(math.atan((p.y + 200) - ey, math.sqrt((p.x - ex) ^ 2 + (p.z - ez) ^ 2)))) end
    end
    if tt - ph_t >= 1.5 then sum, n = sum + (f.gain or 0), n + 1 end   -- (1.5 s to settle into the stance / speed)
    if (name == "tag" or name == "prone") and tt - ph_t >= 5.5 and not ST["shot_" .. name] and mode.env_flag("GE_SNIPER_SHOTS") then
      ST["shot_" .. name] = true
      if name == "prone" then pcall(bench.look, 0, yaw, pitch) end
      bench.shot()
      print("SMOKE shot " .. name)
    end
    if tt - ph_t >= 7 then
      local _, _, _, fy = bond.pos(0)
      local bxx, byy = bond.pos(0)
      print(string.format("SMOKE DETECT %s: average %+.3f a second, meter %.2f, Bond's eye %.0f over his feet (%s)", name, n > 0 and sum / n or 0,
        f.meter or 0, (ey or 0) - (byy or 0), f.dparts or ""))
      ph_i, ph_t, sum, n = ph_i + 1, tt, 0, 0
      if name == "run" then test.walk_stop() end
    end
  end)
end
local t = 0
local yaws = { 0, 60, 150, -120, 20, -40, 180, 90 }
local look_i, look_t = 0, 0
local seen_state = {}
local fired_by = {}
local violations = 0
local closewarp = false
local shot_at = {}
local function snap(tag, at) shot_at[#shot_at + 1] = { tag = tag, at = at } end

local function wrap(a) return (a + 180) % 360 - 180 end
-- the test's own copy of the visibility rule (deliberately written apart from the mode's)
local function visible(x, y, z)
  local _, yaw, pitch, ex, ey, ez = pcall(gadget.aim, 0)
  if not ex then return false end
  local _, _, fov = sniper.view(0)
  local dx, dz = x - ex, z - ez
  local b = math.deg(math.atan(-dx, dz))
  local half_h = math.deg(math.atan(math.tan(math.rad(fov / 2)) * 1.78))
  if math.abs(wrap(b - yaw)) > half_h then return false end
  local p = math.deg(math.atan((y + 300) - ey, math.sqrt(dx * dx + dz * dz)))
  if math.abs(p - pitch) > fov / 2 + 4 then return false end
  return probe.los(ex, ey, ez, x, y + 320, z) or probe.los(ex, ey, ez, x, y + 160, z)
end

on("tick", function(dt)
  t = t + dt
  for i = #shot_at, 1, -1 do
    if t >= shot_at[i].at then
      bench.shot()
      print("SMOKE shot " .. shot_at[i].tag)
      if shot_at[i].tag == "marker" then ST.marker_done_t = t end
      table.remove(shot_at, i)
    end
  end
  if ST.phase ~= "round" then return end
  if CLOSE and not closewarp and not (ST.foes[1] and ST.foes[1].chr) then pcall(bench.look, 0, 180, 0) end   -- (look away: he may set up)
  -- walk Bond up to the marksman once he is on his perch and the appearance has been checked (a warp: the duel itself never moves Bond)
  if CLOSE and not closewarp and ST.foes[1] and seen_state[ST.round .. ":1"] then
    for _, f in ipairs(ST.foes) do
      if f.perch and f.chr then
        local best, bd
        for _, id in ipairs(LEVEL.grid) do
          local x, _, z = pads.pos(id)
          local d = math.sqrt((x - f.perch.x) ^ 2 + (z - f.perch.z) ^ 2)
          if d > 3000 and d < 5500 and (not bd or d < bd) then best, bd = id, d end
        end
        if best then
          bond.warp(0, best)
          closewarp = true
          local x, _, z = pads.pos(best)
          pcall(bench.look, 0, math.deg(math.atan(-(f.perch.x - x), (f.perch.z - z))), 0)
          print(string.format("SMOKE close: Bond warped to pad %d, %.0f m from foe %d on %s", best, bd / 100, f.n, f.perch.name))
        end
      end
    end
  end
  for _, f in ipairs(ST.foes) do
    local key = ST.round .. ":" .. f.n
    local here = f.chr ~= nil and spawn.alive(f.chr) and f.perch ~= nil
    local was = seen_state[key]
    if here and not was then
      local v = visible(f.perch.x, f.perch.y, f.perch.z)
      print(string.format("SMOKE appear: foe %d on %s, Bond could see it: %s", f.n, f.perch.name, tostring(v)))
      if v then violations = violations + 1; print("SMOKE VIOLATION: appeared in view") end
      seen_state[key] = { x = f.perch.x, y = f.perch.y, z = f.perch.z, name = f.perch.name }
    elseif not here and was and f.st ~= "dead" then
      local v = visible(was.x, was.y, was.z)
      print(string.format("SMOKE vanish: foe %d off %s, Bond could see it: %s", f.n, was.name, tostring(v)))
      if v then violations = violations + 1; print("SMOKE VIOLATION: vanished in view") end
      seen_state[key] = nil
    elseif not here and was then
      seen_state[key] = nil
    end
    if f.st == "glint" and SHOTS and not f.snapped and ST.round == 1 then
      f.snapped = true
      local _, _, _, ex, ey, ez = pcall(gadget.aim, 0)
      local d = math.sqrt((f.perch.x - ex) ^ 2 + (f.perch.z - ez) ^ 2)
      pcall(bench.look, 0, math.deg(math.atan(-(f.perch.x - ex), (f.perch.z - ez))), math.deg(math.atan((f.glint_y - ey), d)))
      test.aim(0, true, S.scope_min_fov)
      snap("glint", t + 0.5)
    end
    if f.mark_t and SHOTS and not f.snapped_mark and ST.round == 1 and t - (f.snapped_t or 0) > 0 then
      f.snapped_mark = true
      snap("marker", t + 1.5)
    end
    -- the win path: shoot him once he has fired twice
    if not LOSE and not CLOSE and not DETECT and here and f.volleys >= 2 and f.st == "scan" then
      for _ = 1, 40 do if spawn.alive(f.chr) then test.shoot(f.chr, "body", "sniperrifle", 0) end end
      if f.chr and spawn.alive(f.chr) then spawn.kill(f.chr) end
      print(string.format("SMOKE killed foe %d of round %d", f.n, ST.round))
    end
  end
  -- the view turns round every 3 s, after the checks above (not while a picture is being lined up)
  if not CLOSE and not DETECT and (not SHOTS or ST.glints == 0) and t - look_t >= 3 then
    look_t = t
    look_i = look_i % #yaws + 1
    pcall(bench.look, 0, yaws[look_i], 0)
  end
end)

on("stage_start", function()
  print(string.format("SMOKE start: difficulty %d, hits allowed %s", mission.difficulty() or -1, tostring(S.hits[(mission.difficulty() or 0) + 1])))
end)

local binoc_done = false
on("tick", function()
  if SHOTS and not binoc_done and ST.marker_done_t and t > ST.marker_done_t + 1 then
    binoc_done = t
    test.aim(0, false, 0)
    pcall(bond.equip, 0, "camera")
  end
  if SHOTS and binoc_done and not ST.binoc_snap and t > binoc_done + 3 then
    ST.binoc_snap = true
    test.aim(0, true, S.binoc_min_fov)
    snap("binoculars", t + 1.0)
  end
end)

on("bond_died", function() print(string.format("SMOKE bond died: hits taken %d of %d, violations %d", ST.hits_taken, ST.hits_allowed or -1, violations)) end)
