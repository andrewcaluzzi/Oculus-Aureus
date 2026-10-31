-- The SNIPER DUEL v2 on the SEVERNAYA SNOWFIELD or the SNIPER JUNGLE (GE_MODE=sniper-v2 GE_STAGE=levelgen GE_LEVELGEN=snowfield | jungle;
-- shipped as Data/Scripts/modes/sniper-v2.lua; launchers Play Sniper v2.bat, Play Sniper v3.bat, Play Sniper v3 Jungle.bat). A battle
-- from afar: Bond with a sniper rifle and binoculars on the south shoulder of a 400 x 550 m snowfield (or the south ridge of a 408 m
-- jungle valley), a ladder of rounds against named marksmen on the far slopes. The words and numbers are tools/sniper_v2_defs.lua
-- (SNIPER2_DEFS; D.STAGES the per-stage layout, perches and overrides); `SNIPER2_SETTINGS = { key = value }` set before
-- include("sniper-v2") overrides D.S (stage = "jungle" forces a stage).
--
--   ARRIVAL: a marksman takes the farthest free perch in the band (S.perch_band) that Bond cannot see now: outside his view cone (his
--     field of view now, a margin added: zoomed in, the cone is narrow) or hidden by the drawn ground. Nobody appears in view.
--   ON THE PERCH: a real "snipe" chr drawn 2x, aiming at Bond all the time; his shots are held (spawn.hold) until a glint. Every few
--     seconds, when both the AI's line (combat.los) and the drawn ground (probe.los, his scope to Bond's eye) are clear: the GLINT, a
--     small white flare at his scope (sniper.point), then the shot: a coin (the round's hit chance; inside S.close_range it is
--     S.close_hit) decides it (spawn.shotbond). Where he fired from, a quiet marker stays and fades (S.marker_secs).
--   RELOCATION: after his dwell he wants to move; he leaves only while Bond cannot see him, and comes back on another unseen perch
--     after S.hide seconds. Nobody vanishes in view.
--   HITS: S.hits by difficulty (the last one kills), no healing, no armour, no pickups. The rifle's damage is set at the stage start.
--   LOADOUT: the sniper rifle and the binoculars (the camera item: aim to raise them, a dark lens frame, a stronger zoom, no shot, no
--     glint); the weapon cycle steps over the bare hands (they stay as the empty-gun fallback).
--   PRESENTATION: plain small lines (hud.set) and a small panel; no callouts, no sounds unless S.glint_sfx.
--   THE RUN: a round is won when its marksmen are dead; Bond dies and the run ends (the stock mission screen). Launched from the hub's
--     SNIPER DUEL card (tools/mode_sniper_menu.lua), the end hands ui.result / ui.retry / ui.arm back to that lobby, as the v1 duel
--     does: the hub's stage loads again on the debrief, which keeps the best per stage (sniper_bests: snowfield2_*, jungle2_*).

include("_sniper_v2_defs")
include("_ui_menu")
pcall(include, "_hub_defs")
local D, U = SNIPER2_DEFS, UI_MENU
local MODE_NAME = mode.name()
local cur = ui and ui.current() or nil
if cur and cur.mode ~= MODE_NAME then cur = nil end
local launched = cur and cur.settings ~= "" and U.parse(cur.settings) or {}
-- the stage: the generated level the slot holds (GE_LEVELGEN=snowfield | jungle), or SNIPER2_SETTINGS.stage / the launch's stage; its
-- layout, perches, words and numbers (D.STAGES) over the shared ones
local SETTINGS = type(SNIPER2_SETTINGS) == "table" and SNIPER2_SETTINGS or {}
local STAGE_ID = SETTINGS.stage or launched.stage or (levelgen and levelgen.current and levelgen.current()) or "snowfield"
if not D.STAGES[STAGE_ID] then STAGE_ID = "snowfield" end
local SD = D.STAGES[STAGE_ID]
include(SD.layout)

local T = {}
for k, v in pairs(D.T) do T[k] = v end
for k, v in pairs(SD.T or {}) do T[k] = v end
T.place = SD.place or T.place

local S = {}
for k, v in pairs(D.S) do S[k] = v end
for k, v in pairs(SD.S or {}) do S[k] = v end
for k, v in pairs(SETTINGS) do S[k] = v end
local ROUNDS = S.rounds_table or D.ROUNDS
local N_ROUNDS = math.min(S.rounds or #ROUNDS, #ROUNDS)
local ITEM_CAMERA, ITEM_RIFLE = 40, 17

-- the hub's lobby flow (as tools/mode_sniper.lua): off under the tests' automation unless GE_SNIPER_MENU asks for it
local automated = mode.env_flag("GE_FRAMES") or mode.env_flag("GE_PLAYERBOT") or mode.env_flag("GE_INPUTPLAY")
local FLOW = ui ~= nil and HUB_DEFS ~= nil and not ui.netplay() and (not automated or mode.env_flag("GE_SNIPER_MENU")) and S.postgame ~= false
local LABEL = (cur and cur.label ~= "" and cur.label) or ("SNIPER DUEL  -  " .. (SD.place or STAGE_ID))
local function hub_retry()
  ui.retry({ mode = MODE_NAME, settings = cur and cur.settings or U.serialize({ stage = STAGE_ID }), preset = STAGE_ID, label = LABEL,
             stage = "levelgen", env = { GE_LEVELGEN = STAGE_ID } })
end

local function log(fmt, ...) print(string.format("ge: sniper2: " .. fmt, ...)) end

SNIPER2 = { S = S, rounds = N_ROUNDS, stage = STAGE_ID }   -- (the tests read it)
local ST = SNIPER2
ST.phase = "ready"       -- ready | round | between | over
ST.round = 0
ST.foes = {}
ST.glints, ST.shots, ST.relocs, ST.arrivals = 0, 0, 0, 0
ST.hits_taken = 0
ST.perches = {}

local now, round_t0, run_t = 0, 0, 0
local rnd = nil
local ended = false
local bx, by, bz = 0, 0, 0
local stats = { rounds = 0, headshots = 0, round_times = {}, killer = "" }

-- ---------------------------------------------------------------- helpers ---
local function wrap(a) return (a + 180) % 360 - 180 end
local function rand_in(r) return r[1] + math.random() * (r[2] - r[1]) end
local function bond_xyz()
  local x, y, z = bond.pos(0)
  if x then bx, by, bz = x, y, z end
end
local function eye()
  local ok, yaw, pitch, ex, ey, ez = pcall(gadget.aim, 0)
  if ok and ex then return yaw, pitch, ex, ey, ez end
  bond_xyz()
  return nil, nil, bx, by + 165, bz
end
local function bearing_from(x0, z0, x, z) return math.deg(math.atan(-(x - x0), (z - z0))) end   -- the yaw that faces (x, z)
local function clock_of(x, z)
  local yaw, _, ex, _, ez = eye()
  if not yaw then return "" end
  local rel = wrap(bearing_from(ex, ez, x, z) - yaw)   -- positive: to his left
  return string.format(T.clock_fmt, T.clock[math.floor(-rel / 30 + 0.5) % 12 + 1])
end

-- what Bond can see now: the point inside his view cone (his field of view now, the margin added) and not hidden by the drawn ground
local function bond_sees(x, y, z)
  local yaw, pitch, ex, ey, ez = eye()
  if not yaw then return true, "no view" end
  local _, _, fov = sniper.view(0)
  fov = fov or 60
  local dx, dz = x - ex, z - ez
  local hd = math.sqrt(dx * dx + dz * dz)
  local vhalf = fov / 2 + S.view_margin
  local hhalf = math.deg(math.atan(math.tan(math.rad(fov / 2)) * S.view_aspect)) + S.view_margin
  local ry = math.abs(wrap(bearing_from(ex, ez, x, z) - yaw))
  local rp = math.abs(math.deg(math.atan(((y + 160 * S.foe_scale) - ey), hd)) - pitch)
  if ry > hhalf or rp > vhalf + 8 then return false, string.format("out of view (%.0f deg off, fov %.1f)", ry, fov) end
  for _, h in ipairs({ 40, 160 * S.foe_scale, 300 }) do
    if probe.los(ex, ey, ez, x, y + h, z) then return true, string.format("in view (%.0f deg off, fov %.1f)", ry, fov) end
  end
  return false, string.format("in the cone, hidden by the ground (%.0f deg off)", ry)
end

local function notice(text, secs, line)
  line = line or 1
  hud.set(line, text or "")
  ST["notice_until" .. line] = now + (secs or 4)
end

-- ------------------------------------------------------------ the perches ---
local function build_perches()
  local list = {}
  local sx, _, sz = pads.pos(LEVEL.pads[SD.bond[1]])
  for _, name in ipairs(SD.perches) do
    local id = LEVEL.pads[name]
    if id then
      local x, y, z = pads.pos(id)
      local d = math.sqrt((x - sx) ^ 2 + (z - sz) ^ 2) / 100
      if d >= S.perch_band[1] and d <= S.perch_band[2] then
        list[#list + 1] = { name = name, pad = id, x = x, y = y, z = z, d = d }
      end
    end
  end
  table.sort(list, function(a, b) return a.d > b.d end)
  ST.perches = list
  local names = {}
  for _, p in ipairs(list) do names[#names + 1] = string.format("%s %.0f", p.name, p.d) end
  log("perches in the band %d..%d m: %d (%s)", S.perch_band[1], S.perch_band[2], #list, table.concat(names, ", "))
end

local function geom_line(p)   -- his scope to Bond's eye and back: the drawn ground clear
  local _, _, ex, ey, ez = eye()
  local hy = p.y + 155 * S.foe_scale
  return probe.los(p.x, hy, p.z, ex, ey, ez) and probe.los(ex, ey, ez, p.x, hy, p.z)
end

-- the farthest free perch Bond cannot see; those with a line to Bond first (he can engage from them), then the rest
local function choose_perch(f)
  local busy = {}
  for _, o in ipairs(ST.foes) do
    if o ~= f and o.perch and o.st ~= "dead" then busy[o.perch.pad] = o.perch end
  end
  local best, best_why, n_seen = nil, nil, 0
  for pass = 1, 2 do
    for _, p in ipairs(ST.perches) do   -- (sorted: farthest first)
      local ok = not busy[p.pad] and not (f.last and f.last.pad == p.pad)
      if ok then
        for _, q in pairs(busy) do
          if math.sqrt((q.x - p.x) ^ 2 + (q.z - p.z) ^ 2) < 4000 then ok = false end
        end
      end
      if ok then
        local seen, why = bond_sees(p.x, p.y, p.z)
        if seen then
          if pass == 1 then n_seen = n_seen + 1 end
        elseif pass == 2 or geom_line(p) then
          best, best_why = p, why
          break
        end
      end
    end
    if best then break end
  end
  return best, best_why, n_seen
end

-- ------------------------------------------------------------- the foes ---
local function foe_def(f)
  local k = math.min(#D.FOES, rnd.foe + (f.n - 1))
  return D.FOES[rnd.foe] or D.FOES[#D.FOES], (D.FOES[k] or D.FOES[#D.FOES]).skin
end

local function marksman(f, p)
  local _, skin = foe_def(f)
  local c = spawn.chr(p.pad, { skin = skin, weapon = "sniperrifle", behaviour = "snipe", health = S.foe_hp, sight = S.sight, face = { bx, bz } })
  if c then spawn.hold(c, true); pcall(spawn.scale, c, S.foe_scale) end
  return c
end

local function glint_id(f) return f.n - 1 end
local function marker_id(f) return 8 + f.n - 1 end
local function tag_id(f) return 16 + f.n - 1 end

local function arrive(f)
  local p, why, n_seen = choose_perch(f)
  if not p then
    f.t = now + 1
    if now - (f.defer_log or -9) > 4 then f.defer_log = now; log("ARRIVE deferred: foe %d: every free perch is in Bond's view (%d)", f.n, n_seen) end
    return
  end
  bond_xyz()
  local c = marksman(f, p)
  if not c then f.t = now + 0.5; return end
  f.perch, f.chr = p, c
  f.st, f.arrive_at = "scan", now
  f.dwell_end = now + rand_in(rnd.dwell)
  f.volley_at = now + rand_in(S.min_gap)
  f.volleys = 0
  f.meter, f.full_logged = S.detect_start, false
  ST.arrivals = ST.arrivals + 1
  local d = math.sqrt((p.x - bx) ^ 2 + (p.z - bz) ^ 2) / 100
  log("ARRIVE %d: foe %d on %s (pad %d) at %.0f m from Bond, unseen: %s; %d nearer-ranked perches were in view; stays %.0f s",
    ST.arrivals, f.n, p.name, p.pad, d, why, n_seen, f.dwell_end - now)
end

local function start_glint(f)
  local p = f.perch
  bond_xyz()
  local d = math.sqrt((p.x - bx) ^ 2 + (p.z - bz) ^ 2)
  f.close = d < S.close_range
  f.will_hit = math.random() < (f.close and S.close_hit or rnd.hit)
  f.st, f.glint_end = "glint", now + (f.close and math.min(rnd.glint, 0.9) or rnd.glint)
  ST.glints = ST.glints + 1
  local cx, cz = spawn.where(f.chr)   -- (where he really stands: a chr is not always on its pad's centre)
  f.glint_x, f.glint_y, f.glint_z = cx or p.x, p.y + 155 * S.foe_scale, cz or p.z
  if S.glint_sfx and sound and sound.play_sfx then pcall(sound.play_sfx, S.glint_sfx[1], S.glint_sfx[2], S.glint_sfx[3]) end
  log("GLINT %d: foe %d on %s at %.0f m%s, %.1f s before the shot (%s)", ST.glints, f.n, p.name, d / 100, f.close and " (CLOSE RANGE)" or "",
    f.glint_end - now, f.will_hit and "it will hit" or "it will miss")
end

local function release(f)
  spawn.shotbond(f.chr, f.will_hit and 1 or 0)
  spawn.hold(f.chr, false)
  f.st, f.s0, f.fire_at = "fire", spawn.shots(f.chr) or 0, now
end

local function after_shot(f, n)
  spawn.hold(f.chr, true)
  sniper.point(glint_id(f), "off")
  f.volleys = f.volleys + 1
  ST.shots = ST.shots + n
  local p = f.perch
  local mx, mz = spawn.where(f.chr)
  f.mark_x, f.mark_y, f.mark_z, f.mark_t = mx or p.x, p.y + 110 * S.foe_scale, mz or p.z, now
  bond_xyz()
  local d = math.sqrt((p.x - bx) ^ 2 + (p.z - bz) ^ 2) / 100
  notice(string.format(T.shot_from, clock_of(p.x, p.z), math.floor(d)), 4, 2)
  log("SHOT: foe %d fired %d from %s (%.0f m)%s", f.n, n, p.name, d, f.close and " at close range" or "")
  f.st = "scan"
  f.meter, f.full_logged = S.detect_after, false   -- (he has to find Bond again, partly)
  f.volley_at = now + (f.close and rand_in(S.close_gap) or rand_in(S.min_gap))
end

local function leave(f)
  local p = f.perch
  local seen, why = bond_sees(p.x, p.y, p.z)
  if seen then
    if now - (f.leave_log or -9) > 4 then f.leave_log = now; log("foe %d wants to move off %s but Bond can see it (%s): he stays", f.n, p.name, why) end
    return false
  end
  spawn.remove(f.chr)
  ST.relocs = ST.relocs + 1
  log("LEAVE %d: foe %d off %s after %.0f s, unseen: %s", ST.relocs, f.n, p.name, now - f.arrive_at, why)
  if f.tagged then f.tagged = nil; sniper.point(tag_id(f), "off"); log("TAG off: foe %d moved", f.n) end
  f.chr, f.last, f.perch = nil, p, nil
  f.st, f.t = "wait", now + rand_in(S.hide)
  return true
end

local function geom_clear(f) return geom_line(f.perch) end

-- DETECTION: how fast marksman f is spotting Bond now (meter units a second; the meter fills at 1). Bond's stance (prone < crouch <
-- stand), his speed (running high), his scope (a raised rifle scope pointed his way glints to him; the binoculars never do), the
-- distance, the weather and thermal (ST.weather_k, ST.thermal_k: phase 3 sets them). The parts are kept for the debug line.
local function detect_gain(f)
  local _, _, _, stance = sniper.view(0)
  local aiming, item = sniper.view(0)
  local yaw, _, ex, _, ez = eye()
  local p = f.perch
  local d = math.sqrt((p.x - ex) ^ 2 + (p.z - ez) ^ 2)
  local k_st = S.detect_stance[(stance or 0) + 1] or 1
  local k_mv = S.detect_still + (S.detect_run - S.detect_still) * math.min(1, (ST.speed or 0) / S.run_speed)
  local k_sc = 1
  if aiming and item == ITEM_RIFLE and yaw and math.abs(wrap(bearing_from(ex, ez, p.x, p.z) - yaw)) <= S.scope_glint_cone then k_sc = S.detect_scope end
  local k_d = math.max(S.detect_dist_k[1], math.min(S.detect_dist_k[2], S.detect_ref_dist / math.max(d, 100)))
  local k_close = d < S.close_range and S.close_detect or 1
  local gain = S.detect_rate * k_st * k_mv * k_sc * k_d * k_close * (ST.weather_k or 1) * (ST.thermal_k or 1)
  f.dparts = string.format("stance %s x%.2f, move %.0f/s x%.2f, scope x%.1f, %.0f m x%.2f%s, weather x%.2f, thermal x%.2f", tostring(stance), k_st,
    ST.speed or 0, k_mv, k_sc, d / 100, k_d, k_close > 1 and " CLOSE" or "", ST.weather_k or 1, ST.thermal_k or 1)
  return gain
end

local function foe_tick(f, dt)
  if f.st == "wait" then
    if now >= f.t then arrive(f) end
  elseif f.st == "scan" then
    if not f.chr then return end
    bond_xyz()
    local close = math.sqrt((f.perch.x - bx) ^ 2 + (f.perch.z - bz) ^ 2) < S.close_range
    local line = combat.los(f.chr, 0) and geom_clear(f)
    if line then
      f.line_t = now
      f.gain = detect_gain(f)
      f.meter = math.min(1, (f.meter or 0) + f.gain * dt)
    else
      f.gain = -S.detect_decay
      f.dparts = "no line to Bond (hidden)"
      f.meter = math.max(0, (f.meter or 0) - S.detect_decay * dt)
    end
    if f.meter >= 1 and not f.full_logged then
      f.full_logged = true
      log("SPOTTED: foe %d on %s has Bond (%s)", f.n, f.perch.name, f.dparts or "")
    end
    if now >= f.dwell_end and f.volleys >= 1 and not close and f.meter < 1 then
      if not leave(f) then f.dwell_end = now + 1 end
    elseif line and f.meter >= 1 and now >= f.volley_at then
      start_glint(f)
    elseif not line and now - f.arrive_at >= 8 and now - (f.line_t or f.arrive_at) >= 8 then
      leave(f)   -- no line to Bond for 8 s: he moves on (only while unseen)
    end
  elseif f.st == "glint" then
    if now >= f.glint_end then release(f) end
  elseif f.st == "fire" then
    if now - f.fire_at >= S.fire_timeout then after_shot(f, (spawn.shots(f.chr) or 0) - f.s0) end   -- (the shot itself is caught in ui_tick)
  end
end

-- ------------------------------------------------------------ the weather ---
-- THE SNOWSTORM: calm for S.storm_calm seconds, the fog rolls in over S.storm_ramp, holds S.storm_hold, rolls out. The fog band moves
-- from S.fog to S.storm_fog (metres); the marksmen's spotting is x S.storm_detect at full storm, but in a storm they go to thermal
-- (x S.foe_thermal while the storm is past half).
ST.storm = 0
local storm_st, storm_t = "calm", nil
local function storm_tick()
  if not S.storm then return end
  storm_t = storm_t or (now + rand_in(S.storm_calm))
  if storm_st == "calm" and now >= storm_t then
    storm_st, storm_t = "rising", now
    notice(T.storm_in, 4, 2)
    log("STORM: rolling in (%.0f s)", S.storm_ramp)
  elseif storm_st == "rising" then
    ST.storm = math.min(1, (now - storm_t) / S.storm_ramp)
    if ST.storm >= 1 then storm_st, storm_t = "full", now + rand_in(S.storm_hold); log("STORM: full (fog %s)", table.concat({ sniper.fog() }, " .. ")) end
  elseif storm_st == "full" and now >= storm_t then
    storm_st, storm_t = "falling", now
    notice(T.storm_out, 4, 2)
    log("STORM: clearing")
  elseif storm_st == "falling" then
    ST.storm = math.max(0, 1 - (now - storm_t) / S.storm_ramp)
    if ST.storm <= 0 then storm_st, storm_t = "calm", now + rand_in(S.storm_calm); log("STORM: calm (fog %s)", table.concat({ sniper.fog() }, " .. ")) end
  end
  ST.weather_k = 1 + (S.storm_detect - 1) * ST.storm
  ST.thermal_k = ST.storm >= 0.5 and S.foe_thermal or 1
end

local applied_storm = -1
local function storm_fog()   -- (ui_tick: the fog follows the storm level smoothly)
  if not S.storm or math.abs(ST.storm - applied_storm) < 0.004 then return end
  applied_storm = ST.storm
  local a, b = S.fog[1], S.fog[2]
  local c, d = S.storm_fog[1], S.storm_fog[2]
  sniper.fog(a + (c - a) * ST.storm, b + (d - b) * ST.storm)
end

-- THERMAL (Q, the next_inventory key): S.thermal_secs of the world dimmed and every chr Bond has a line to drawn as a warm shape,
-- then S.thermal_cooldown before it can be used again
ST.grunts = {}
local function heat_targets()
  local list = {}
  for _, f in ipairs(ST.foes) do if f.chr and f.perch then list[#list + 1] = { chr = f.chr, y = f.perch.y, h = 1.8 * S.foe_scale } end end
  for _, g in ipairs(ST.grunts) do if g.chr and spawn.alive(g.chr) then list[#list + 1] = { chr = g.chr, h = 1.8 } end end
  return list
end

local function thermal_tick()
  local n = sniper.q_presses(0)
  if n > 0 then
    if ST.thermal_until and now < ST.thermal_until then
      -- (already on)
    elseif now < (ST.thermal_ready or 0) then
      notice(string.format(T.thermal_wait, math.ceil(ST.thermal_ready - now)), 2, 2)
      log("THERMAL: recharging, %.0f s left", ST.thermal_ready - now)
    else
      ST.thermal_until = now + S.thermal_secs
      ST.thermal_ready = ST.thermal_until + S.thermal_cooldown
      sniper.thermal(true, S.thermal_dim)
      notice(T.thermal_on, 2, 2)
      log("THERMAL: on for %.0f s (then %.0f s to recharge)", S.thermal_secs, S.thermal_cooldown)
    end
  end
  if ST.thermal_until and now >= ST.thermal_until then
    ST.thermal_until = nil
    sniper.thermal(false)
    for id = 24, 47 do sniper.point(id, "off") end
    log("THERMAL: off")
  end
  if ST.thermal_until then   -- who is warm and in sight (the drawn ground hides them, the fog does not)
    local _, _, ex, ey, ez = eye()
    ST.heat = {}
    for k, t in ipairs(heat_targets()) do
      if k > 24 then break end
      local x, z = spawn.where(t.chr)
      if x then
        local y = t.y
        if not y then local _, gy = gadget.chr_yaw(t.chr); y = gy or 0 end
        local seen = probe.los(ex, ey, ez, x, y + 100 * t.h / 2, z) or probe.los(ex, ey, ez, x, y + 100 * t.h - 20, z)
        ST.heat[#ST.heat + 1] = { chr = t.chr, id = 23 + k, h = t.h, y = y, seen = seen }
      end
    end
    if now - (ST.heat_log or -9) >= 2 then
      ST.heat_log = now
      local s = {}
      for _, h in ipairs(ST.heat) do s[#s + 1] = string.format("chr %d %s", h.chr, h.seen and "warm" or "behind the ground") end
      log("THERMAL: %d chrs: %s", #ST.heat, table.concat(s, ", "))
    end
  end
end

local function heat_draw()   -- (ui_tick: the shapes follow the chrs)
  if not ST.thermal_until or not ST.heat then return end
  for _, h in ipairs(ST.heat) do
    local x, z = spawn.where(h.chr)
    if x and h.seen then
      sniper.point(h.id, "heat", x, h.y, z, 0.9, h.h)
      if not ST.heat_dbg then ST.heat_dbg = true; log("THERMAL: heat shape %d at (%.0f, %.0f, %.0f), %.1f m tall", h.id, x, h.y or -1, z, h.h) end
    else
      sniper.point(h.id, "off")
    end
  end
end

-- THE FLUSH-OUT (between rounds, the round's `flush`): you started shooting snipers, so they send grunts up the hill to flush you out
-- for the next one: a handful start at the bottom of the hill out of Bond's sight and storm him. The next round begins when they are down.
local function flush_pads(n)
  bond_xyz()
  local good, loose = {}, {}
  for _, id in ipairs(LEVEL.grid) do
    local x, y, z = pads.pos(id)
    local d = math.sqrt((x - bx) ^ 2 + (z - bz) ^ 2)
    if d >= S.flush_dist[1] and d <= S.flush_dist[2] and not bond_sees(x, y, z) then
      if y <= by - S.flush_drop then good[#good + 1] = id else loose[#loose + 1] = id end
    end
  end
  local pool = (#good >= n) and good or loose
  if #pool < n then for _, id in ipairs(good) do pool[#pool + 1] = id end end
  local out = {}
  for _ = 1, n do
    if #pool == 0 then break end
    out[#out + 1] = table.remove(pool, math.random(#pool))
  end
  return out, #good
end

local function start_flush(n)
  ST.phase = "flush"
  ST.flush_t0 = now
  ST.grunts = {}
  sniper.bond_hit(0)   -- (their rifles do their own damage)
  local ids, ngood = flush_pads(n)
  for k, pad in ipairs(ids) do
    local gun = S.flush_guns[math.random(#S.flush_guns)]
    local skin = S.flush_skins[math.random(#S.flush_skins)]
    local ok, c = pcall(spawn.chr, pad, { skin = skin, weapon = gun, behaviour = "rush", accuracy = S.flush_accuracy, health = 1.0,
                                          sight = S.sight, face = { bx, bz } })
    if ok and c then
      local x, y, z = pads.pos(pad)
      ST.grunts[#ST.grunts + 1] = { chr = c, pad = pad }
      local seen, why = bond_sees(x, y, z)
      log("FLUSH grunt %d (chr %d) on pad %d, %.0f m from Bond, %.0f below him, %s (%s), %s", k, c, pad,
        math.sqrt((x - bx) ^ 2 + (z - bz) ^ 2) / 100, (by - y) / 100, seen and "SEEN" or "unseen", why, gun)
    end
  end
  notice(T.flush, 4, 1)
  log("FLUSH: %d grunts sent up the hill (%d downhill pads out of sight)", #ST.grunts, ngood)
  if #ST.grunts == 0 then ST.phase = "between"; ST.next_at = now + S.breather end
end

local function flush_tick()
  local alive, near = 0, 1e9
  bond_xyz()
  for _, g in ipairs(ST.grunts) do
    if g.chr and spawn.alive(g.chr) then
      alive = alive + 1
      local x, z = spawn.where(g.chr)
      if x then near = math.min(near, math.sqrt((x - bx) ^ 2 + (z - bz) ^ 2) / 100) end
    end
  end
  if now - (ST.flush_log or -9) >= 3 then ST.flush_log = now; log("FLUSH: %d alive, the nearest %.0f m from Bond", alive, near < 1e8 and near or -1) end
  hud.panel({ string.format(T.panel_round, ST.round, N_ROUNDS), string.format(T.panel_flush, alive),
              string.format(T.panel_hits, math.max(0, (ST.hits_allowed or 0) - ST.hits_taken)) })
  if alive == 0 or now - ST.flush_t0 >= S.flush_timeout then
    for _, g in ipairs(ST.grunts) do if g.chr and spawn.alive(g.chr) then spawn.remove(g.chr) end end
    log("FLUSH over after %.0f s (%s)", now - ST.flush_t0, alive == 0 and "all down" or "timed out")
    ST.grunts = {}
    ST.phase = "between"
    ST.next_at = now + S.breather
  end
end

-- ------------------------------------------------------------ the rounds ---
local function start_round(i)
  ST.round, rnd = i, ROUNDS[i]
  ST.phase = "round"
  round_t0 = now
  ST.foes, ST.killed = {}, 0
  sniper.bond_hit(ST.hit_frac or 0)   -- (the marksmen's exact hits again, after a flush-out)
  local t0 = now + rand_in(S.first_delay)
  for n = 1, rnd.snipers do
    ST.foes[#ST.foes + 1] = { n = n, st = "wait", t = t0, volleys = 0 }
    t0 = t0 + rand_in(S.second_after)
  end
  pcall(bond.give, 0, "sniperrifle", S.bond_ammo)
  local def = D.FOES[rnd.foe] or D.FOES[#D.FOES]
  notice(string.format(T.round, i, N_ROUNDS, def.name, def.title), 5, 1)
  if rnd.snipers > 1 then notice(string.format(T.two, rnd.snipers), 5, 2) end
  log("ROUND %d/%d: %s, %d marksmen, hit chance %.0f%%, glint %.1f s", i, N_ROUNDS, def.name, rnd.snipers, rnd.hit * 100, rnd.glint)
end

local function end_run(win)
  if ended then return end
  ended = true
  ST.phase = "over"
  for _, f in ipairs(ST.foes) do if f.chr then spawn.remove(f.chr); f.chr = nil end end
  for _, g in ipairs(ST.grunts) do if g.chr and spawn.alive(g.chr) then spawn.remove(g.chr) end end
  ST.grunts = {}
  sniper.clear()
  sniper.thermal(false)
  local fired, hits = bond.shots(0)
  log("END: %s, %d/%d rounds won, %d:%02d, accuracy %d/%d, headshots %d, hits taken %d of %d%s", win and "WON" or "DOWN", stats.rounds, N_ROUNDS,
    math.floor(run_t) // 60, math.floor(run_t) % 60, hits or 0, math.max(fired or 0, hits or 0), stats.headshots, ST.hits_taken, ST.hits_allowed or 0,
    stats.killer ~= "" and (", felled by " .. stats.killer) or "")
  notice(win and T.you_win or T.down, 5, 1)
  ST.end_at = now + (win and 4.0 or 3.0)
  fired, hits = fired or 0, hits or 0
  ST.result = { win = win and true or false, rounds = stats.rounds, total = N_ROUNDS, time = math.floor(run_t * 100 + 0.5) / 100,
                shots = math.max(fired, hits), hits = hits, headshots = stats.headshots, hits_taken = ST.hits_taken, foe = stats.killer,
                label = LABEL, stage = STAGE_ID, round_times = stats.round_times }
  if FLOW then   -- the hub's SNIPER DUEL lobby shows the debrief and keeps the best (tools/mode_sniper_menu.lua)
    ui.result(U.serialize(ST.result))
    hub_retry()
    ui.arm(HUB_DEFS.lobby("sniper", "postgame", STAGE_ID, LABEL))
  end
end

local function round_won()
  stats.rounds = stats.rounds + 1
  stats.round_times[#stats.round_times + 1] = math.floor(now - round_t0 + 0.5)
  log("ROUND %d WON in %.1f s", ST.round, now - round_t0)
  if ST.round >= N_ROUNDS then end_run(true); return end
  notice(string.format(T.round_won, ST.round), 4, 1)
  if (rnd.flush or 0) > 0 then start_flush(rnd.flush); return end
  ST.phase = "between"
  ST.next_at = now + S.breather
end

-- ----------------------------------------------------------------- events ---
local function loadout()
  for _, it in ipairs({ "pp7", "pp7sil", "wppk", "wppksil" }) do pcall(plan.strip, 0, it) end
  pcall(bond.give, 0, "sniperrifle", S.bond_ammo)
  pcall(bond.give, 0, "camera")
  pcall(bond.equip, 0, "sniperrifle")
  pcall(gadget.items, { ITEM_CAMERA })    -- the binoculars' trigger is the script's: no photo, no shot
  sniper.no_unarmed(true)
  sniper.binoculars(true)
  sniper.zoom_limit("sniperrifle", S.scope_min_fov)
  sniper.zoom_limit("camera", S.binoc_min_fov)
  sniper.prone_tune(S.prone_eye, S.prone_speed, S.prone_sway)
  sniper.claim_q(true)   -- (Q is the thermal view; the mouse wheel still swaps the rifle and the binoculars)
end

on("stage_start", function()
  bond_xyz()
  if ar then pcall(ar.on, false) end
  loadout()
  -- one marksman hit takes 1/N of a health bar (N by difficulty; the last one kills): the AI's hit is 0.125 x the rifle's damage x the
  -- difficulty's damage modifier (0.5, 0.75, 1, 1)
  local diff = mission.difficulty() or 0
  local n = S.hits[diff + 1] or 3
  local dmod = ({ 0.5, 0.75, 1.0, 1.0 })[diff + 1] or 1.0
  local frac = 1.0 / n + 0.02
  weapon.set("sniperrifle", "damage", frac / (0.125 * dmod))
  sniper.bond_hit(frac)   -- (exact: the stock damage of an AI hit varies with the part it strikes)
  ST.hit_frac = frac
  ST.hits_allowed = n
  if S.fog then sniper.fog(S.fog[1], S.fog[2]) end
  ST.fog = { sniper.fog() }
  build_perches()
  if FLOW then   -- an abort from the watch comes back to the lobby too
    hub_retry()
    ui.result(U.serialize({ aborted = true, win = false, rounds = 0, total = N_ROUNDS, time = 0, label = LABEL, stage = STAGE_ID }))
    ui.arm(HUB_DEFS.lobby("sniper", "postgame", STAGE_ID, LABEL))
  end
  log("stage start: %s (stage %s), %d rounds; difficulty %d: %d marksman hit(s) kill (one hit takes %.0f%% of a bar); scope zoom limit %.1f deg, binoculars %.1f deg; "
    .. "close range %.0f m (hit %.0f%%); fog %s; storms %s", T.place, STAGE_ID, N_ROUNDS, diff, n, frac * 100, S.scope_min_fov, S.binoc_min_fov,
    S.close_range / 100, S.close_hit * 100, table.concat(ST.fog, " .. "), S.storm and "on" or "off")
  ST.next_at = now + 2
end)

on("ui_tick", function(dt)
  for _, f in ipairs(ST.foes) do   -- one shot a glint: the hold goes back on the tick the shot is fired
    if f.st == "fire" and f.chr then
      local n = (spawn.shots(f.chr) or 0) - f.s0
      if n >= 1 then after_shot(f, n) end
    end
  end
  -- presentation: no gun sight through the binoculars (their own reticle is in the lens frame)
  local aiming, item = sniper.view(0)
  local binoc = aiming and item == ITEM_CAMERA
  if binoc ~= ST.binoc_up and crosshair and crosshair.hide then ST.binoc_up = binoc; pcall(crosshair.hide, binoc and true or false) end
  -- TAGGING: the binoculars held on a marksman for S.tag_secs pin a quiet tag on him until he moves (presentation)
  local yaw, pitch, ex, ey, ez = eye()
  for _, f in ipairs(ST.foes) do
    if binoc and yaw and f.chr and f.perch and not f.tagged then
      local p = f.perch
      local hy = p.y + 100 * S.foe_scale
      local hd = math.sqrt((p.x - ex) ^ 2 + (p.z - ez) ^ 2)
      local dyaw = math.abs(wrap(bearing_from(ex, ez, p.x, p.z) - yaw))
      local dp = math.abs(math.deg(math.atan(hy - ey, hd)) - pitch)
      if dyaw <= S.tag_cone and dp <= S.tag_cone * 1.5 and probe.los(ex, ey, ez, p.x, hy, p.z) then
        f.tag_t = (f.tag_t or 0) + dt
        if f.tag_t >= S.tag_secs then
          f.tagged = true
          local tx, tz = spawn.where(f.chr)
          sniper.point(tag_id(f), "tag", tx or p.x, hy, tz or p.z, 0.8, S.tag_px)
          notice(string.format(T.tagged, math.floor(hd / 100)), 3, 2)
          log("TAG: foe %d on %s tagged after %.1f s on the binoculars (%.0f m)", f.n, p.name, f.tag_t, hd / 100)
        end
      else
        f.tag_t = 0
      end
    else
      f.tag_t = 0
    end
  end
  storm_fog()
  heat_draw()
  -- the glint's pulse, the markers' fade
  for _, f in ipairs(ST.foes) do
    if f.st == "glint" and f.glint_x then
      local a = 0.62 + 0.38 * math.sin(now * 14 + f.n)
      sniper.point(glint_id(f), "glint", f.glint_x, f.glint_y, f.glint_z, a, S.glint_px)
    end
    if f.mark_t then
      local age = now - f.mark_t
      if age >= S.marker_secs then f.mark_t = nil; sniper.point(marker_id(f), "off")
      else sniper.point(marker_id(f), "marker", f.mark_x, f.mark_y, f.mark_z, 0.85 * (1 - age / S.marker_secs), S.marker_px) end
    end
  end
end)

local dbg_t = 0
on("tick", function(dt)
  now = now + dt
  for line = 1, 2 do
    local u = ST["notice_until" .. line]
    if u and now >= u then ST["notice_until" .. line] = nil; hud.set(line, "") end
  end
  if ST.rise_at and now >= ST.rise_at then
    ST.rise_at = nil
    sniper.prone(0, false)
    log("PRONE: up")
  end
  if ended then
    if ST.end_at and now >= ST.end_at then
      ST.end_at = nil
      hud.panel(nil)
      if ST.result and ST.result.win then mission.win("Sniper duel won") else mission.fail("Agent down") end
    end
    return
  end
  storm_tick()
  thermal_tick()
  if ST.phase == "ready" and now >= (ST.next_at or 0) then start_round(1)
  elseif ST.phase == "between" and now >= ST.next_at then start_round(ST.round + 1)
  elseif ST.phase == "flush" then flush_tick()
  elseif ST.phase == "round" then
    run_t = run_t + dt
    local x, _, z = bond.pos(0)   -- Bond's speed (units a second, on the ground), for the detection
    if x and ST.last_x then
      ST.speed = math.sqrt((x - ST.last_x) ^ 2 + (z - ST.last_z) ^ 2) / dt
      if ST.speed > 2000 then ST.speed = 0 end   -- (a warp, not a run)
    end
    ST.last_x, ST.last_z = x, z
    for _, f in ipairs(ST.foes) do foe_tick(f, dt) end
    local left = rnd.snipers - ST.killed
    local th = (ST.thermal_until and T.panel_th_on) or (now < (ST.thermal_ready or 0) and string.format(T.panel_th_wait, math.ceil(ST.thermal_ready - now)))
      or T.panel_th_ready
    hud.panel({ string.format(T.panel_round, ST.round, N_ROUNDS), string.format(T.panel_left, left),
                string.format(T.panel_hits, math.max(0, (ST.hits_allowed or 0) - ST.hits_taken)), th })
  end
  if now - dbg_t >= S.debug_every then
    dbg_t = now
    local aiming, item, fov, stance = sniper.view(0)
    local yaw, pitch = eye()
    local parts = {}
    for _, f in ipairs(ST.foes) do
      local s = string.format("%d:%s", f.n, f.st)
      if f.perch and f.chr then
        local seen = bond_sees(f.perch.x, f.perch.y, f.perch.z)
        s = s .. string.format("@%s%s%s", f.perch.name, seen and "(SEEN)" or "", f.tagged and "(TAGGED)" or "")
        log("DETECT foe %d on %s: meter %.2f, %+.3f/s (%s)", f.n, f.perch.name, f.meter or 0, f.gain or 0, f.dparts or "")
      end
      parts[#parts + 1] = s
    end
    bond_xyz()
    log("DBG t=%.0f bond (%.0f, %.0f, %.0f) yaw %.0f pitch %.0f aiming %s item %s fov %.1f stance %s | %s", now, bx, by, bz, yaw or -1, pitch or -1,
      tostring(aiming), tostring(item), fov or -1, tostring(stance), table.concat(parts, " "))
  end
end)

on("chr_died", function(victim, killer)
  for k, g in ipairs(ST.grunts) do
    if g.chr == victim then log("FLUSH grunt %d down", k); g.chr = nil; return end
  end
  for _, f in ipairs(ST.foes) do
    if f.chr == victim and f.st ~= "dead" then
      f.st, f.chr = "dead", nil
      sniper.point(glint_id(f), "off")
      if f.tagged then f.tagged = nil; sniper.point(tag_id(f), "off") end
      ST.killed = ST.killed + 1
      local hs = spawn.headshot(victim)
      if hs then stats.headshots = stats.headshots + 1 end
      log("KILL: foe %d down on %s%s (%d of %d)", f.n, f.perch and f.perch.name or "?", hs and " by a headshot" or "", ST.killed, rnd.snipers)
      if ST.killed >= rnd.snipers and ST.phase == "round" then round_won() end
      return
    end
  end
end)

on("damage", function(victim, attacker, amount)
  if victim ~= bond.id(0) or (amount or 0) <= 0 then return end
  for _, f in ipairs(ST.foes) do
    if f.chr and f.chr == attacker then
      ST.hits_taken = ST.hits_taken + 1
      stats.killer = (foe_def(f)).name
      local h = bond.vitals(0)
      log("HIT %d of %d on Bond by foe %d from %s (%.2f damage, health now %.2f)", ST.hits_taken, ST.hits_allowed or 0, f.n, f.perch and f.perch.name or "?",
        amount, h or -1)
      notice(string.format(T.hit, math.max(0, (ST.hits_allowed or 0) - ST.hits_taken)), 3, 1)
      return
    end
  end
end)

-- PRONE: the use key (nothing to use out on the snow) goes down flat, or starts getting up (slow: S.prone_rise seconds)
local function set_prone(on)
  if on then
    ST.rise_at = nil
    sniper.prone(0, true)
    notice(T.prone, 2, 2)
    log("PRONE: down")
  elseif sniper.prone(0) and not ST.rise_at then
    ST.rise_at = now + S.prone_rise
    notice(T.rising, 2, 2)
    log("PRONE: getting up (%.1f s)", S.prone_rise)
  end
end
SNIPER2.set_prone = set_prone   -- (the tests)

on("use", function(p, proptype)
  if p ~= 0 or proptype ~= -1 or ended then return end
  if bond.use_key and not bond.use_key(0) then return end   -- (the use bind, not B / reload)
  set_prone(not sniper.prone(0) or ST.rise_at ~= nil)
end)

on("bond_died", function()
  if ended then return end
  end_run(false)
end)
