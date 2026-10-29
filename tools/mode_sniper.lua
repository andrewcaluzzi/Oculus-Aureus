-- The SNIPER DUEL (GE_MODE=sniper on the stock Surface stage; shipped as Data/Scripts/modes/sniper.lua). A prototype: Bond on the ridge above the
-- Severnaya start with a sniper rifle and a PP7, a ladder of rounds against named marksmen on the far slopes (60 to 150 m). The words and numbers
-- are tools/sniper_defs.lua (SNIPER_DEFS); `SNIPER_SETTINGS = { key = value }` set before include("sniper") overrides D.S (the tests use it).
--
--   THE SIGHT SWEEP (stage start, about 5 s): for each of Bond's candidate perches (D.BOND.pads) Bond is warped there and a harmless stand-in is
--     put on every waypoint pad in the 60 to 150 m band in turn; combat.los (the stan line the AI's own sight uses) says which pads see him. The
--     perch that is seen from the most pads is Bond's; those pads are the marksmen's perches. Nothing about the map is stored: only the pad
--     ids Bond's candidates come from, and pads can be banned or favoured in the defs.
--   THE GLINT: a marksman in the "wait" state is nowhere. When his time is up a perch is picked (out of the middle of Bond's view and a way round
--     from the last one) and a stand-in (invincible, harmless) takes it with a marker over the rifle (ar.point, an arrow on the screen edge when
--     it is off screen) and a chime. After the round's glint seconds the stand-in is swapped for the real marksman (behaviour "snipe": holds,
--     fires at Bond whenever the stan line is clear), who aims and fires.
--   RELOCATE: spawn.shots (the port counts every queued shot, hit or miss) says he has fired; post_shot seconds later he is removed and waits
--     hide seconds, then takes another perch. The kill window is the stand-in's swap to his shot, and again right after the shot.
--   RUSHERS: rounds with rushers send "rush" troops from the band 28 to 55 m off, out of Bond's cone, a few seconds in.
--   THE RUN: a round is won when its marksmen are dead; the next callout comes after the breather. Bond dies: the run ends. The end hands
--   ui.result / ui.arm to the hub's SNIPER DUEL lobby (tools/mode_sniper_menu.lua), which shows the debrief and keeps the best.

include("_sniper_defs")
include("_ui_menu")
pcall(include, "_hub_defs")
local D, U = SNIPER_DEFS, UI_MENU
local T = D.T
local name = mode.name()

local S = {}
for k, v in pairs(D.S) do S[k] = v end
local cur = ui and ui.current() or nil
if cur and cur.mode ~= name then cur = nil end
local launched = cur and cur.settings ~= "" and U.parse(cur.settings) or nil
for _, over in ipairs({ type(SNIPER_SETTINGS) == "table" and SNIPER_SETTINGS or {}, launched or {} }) do
  for k, v in pairs(over) do S[k] = v end
end
local ROUNDS = S.rounds_table or D.ROUNDS
local N_ROUNDS = math.min(S.rounds or #ROUNDS, #ROUNDS)

local automated = mode.env_flag("GE_FRAMES") or mode.env_flag("GE_PLAYERBOT") or mode.env_flag("GE_INPUTPLAY")
local FLOW = ui ~= nil and HUB_DEFS ~= nil and not ui.netplay() and (not automated or mode.env_flag("GE_SNIPER_MENU")) and S.postgame ~= false
local LABEL = (cur and cur.label ~= "" and cur.label) or (T.title .. "  -  " .. D.STAGE.name)
local function log(fmt, ...) print(string.format("ge: sniper: " .. fmt, ...)) end

SNIPER = { S = S, rounds = N_ROUNDS }   -- (the tests read it)
local ST = SNIPER
ST.phase = "sweep"       -- sweep | ready | round | between | over
ST.round = 0
ST.foes, ST.rushers = {}, {}
ST.bond_pad = nil
ST.perches = {}
ST.glints, ST.shots, ST.relocs = 0, 0, 0   -- counters for the log and the tests

local now, round_t0, run_t = 0, 0, 0
local stats = { rounds = 0, headshots = 0, hits_taken = 0, round_times = {}, killer = "" }
local ban, prefer = {}, {}
for _, p in ipairs(D.PERCH.ban or {}) do ban[p] = true end
for _, p in ipairs(D.PERCH.prefer or {}) do prefer[p] = true end
local bx, by, bz = 0, 0, 0
local rnd = nil                                -- the current round's table
local ended = false

-- ---------------------------------------------------------------- helpers ---
local function wrap(a) a = (a + 180) % 360 - 180; return a end
local function bearing(x, z) return math.deg(math.atan(-(x - bx), (z - bz))) end   -- the yaw that faces (x, z) (forward is (-sin, cos))
local function view_yaw()
  local ok, yaw = pcall(gadget.aim, 0)
  if ok and yaw then return yaw end
  return nil
end
local function clock_of(b, fmt)   -- "2 O'CLOCK": the perch's bearing against where Bond looks now
  local v = view_yaw()
  if not v then return "" end
  local rel = wrap(b - v)   -- positive: to his left
  local h = math.floor(-rel / 30 + 0.5) % 12
  return string.format(fmt or T.clock_fmt, T.clock[h + 1])
end
local function rand_in(r) return r[1] + math.random() * (r[2] - r[1]) end
local function sfx(spec) if sound and sound.play_sfx and spec then pcall(sound.play_sfx, spec[1], spec[2], spec[3]) end end
local function callout(text, level, sub, secs)
  if arena and arena.callout and text then pcall(arena.callout, text, level or 3, sub, secs or 3.0) end
  hud.set(1, text)
  hud.set(2, sub or "")
end
local toast_until = 0
local function toast(text, secs) hud.set(3, text or ""); toast_until = now + (secs or 3.5) end
local function clockstr(t) t = math.floor(t); return string.format("%d:%02d", t // 60, t % 60) end

local function give_kit()
  pcall(bond.give, 0, "pp7", S.bond_ammo.pp7)
  pcall(bond.give, 0, "sniperrifle", S.bond_ammo.sniper)
  pcall(bond.equip, 0, "sniperrifle")
end

-- ---------------------------------------------------------- the sight sweep ---
-- a coroutine stepped from ui_tick (every game tick): warp, settle, probe the band's pads in batches, ask combat.los, remove the stand-ins
local function sweep_for(bp)
  local px, py, pz = pads.pos(bp)
  local seen = {}
  local ids = pads.far_from(px, pz, S.band_lo, S.band_hi, true)
  table.sort(ids)
  local i = 1
  while i <= #ids do
    local group = {}
    for _ = 1, S.sweep_batch do
      local pad = ids[i]
      if not pad then break end
      i = i + 1
      local x, y, z = pads.pos(pad)
      if x and y >= D.PERCH.y_min and not ban[pad] then
        local ok, c = pcall(spawn.chr, pad, { behaviour = "spectator", weapon = "none" })
        if ok and c then group[#group + 1] = { pad = pad, chr = c, x = x, y = y, z = z } end
      end
    end
    coroutine.yield()
    for _, g in ipairs(group) do
      if combat.los(g.chr, 0) then
        seen[#seen + 1] = { pad = g.pad, x = g.x, y = g.y, z = g.z, d = math.sqrt((g.x - px) ^ 2 + (g.z - pz) ^ 2), prefer = prefer[g.pad] }
      end
      spawn.remove(g.chr)
    end
  end
  return seen, px, py, pz
end

local sweep_co = coroutine.create(function()
  local best, bseen
  for _, bp in ipairs(D.BOND.pads) do
    local px = pads.pos(bp)
    if px then
      bond.warp(0, bp)
      for _ = 1, 4 do coroutine.yield() end
      local seen = sweep_for(bp)
      log("sweep: Bond's perch pad %d sees %d perches in the %d to %d m band", bp, #seen, S.band_lo // 100, S.band_hi // 100)
      if not best or #seen > #bseen then best, bseen = bp, seen end
      if S.perch_enough and #seen >= S.perch_enough then break end
    end
  end
  if not best then return nil end
  bond.warp(0, best)
  for _ = 1, 4 do coroutine.yield() end
  return best, bseen
end)

-- ------------------------------------------------------------ the perches ---
local function bond_xyz()
  local x, y, z = bond.pos(0)
  if x then bx, by, bz = x, y, z end
end

-- pick a perch for foe f: not the other foes' pads or the last one; out of the middle of Bond's view and round from the last; relaxed in steps
local function choose_perch(f)
  bond_xyz()
  local v = view_yaw()
  local busy = {}
  for _, o in ipairs(ST.foes) do if o ~= f and o.perch then busy[o.perch.pad] = true end end
  for step = 1, 4 do
    local cone = (step == 1 and S.hide_cone) or (step == 2 and S.hide_cone / 2) or 0
    local gap = (step <= 2 and S.move_ang) or (step == 3 and S.move_ang / 2) or 0
    local list = {}
    for _, p in ipairs(ST.perches) do
      if not busy[p.pad] and not (f.last_pad == p.pad) then
        local b = bearing(p.x, p.z)
        local okv = not v or math.abs(wrap(b - v)) >= cone
        local okg = not f.last_bearing or math.abs(wrap(b - f.last_bearing)) >= gap
        if okv and okg then
          list[#list + 1] = p
          if p.prefer then list[#list + 1] = p end
        end
      end
    end
    if #list > 0 then
      local p = list[math.random(#list)]
      if step > 1 then log("perch pad %d picked with the rules relaxed to step %d", p.pad, step) end
      return p, bearing(p.x, p.z)
    end
  end
  return nil
end

-- ------------------------------------------------------------- the foes ---
local function foe_def(f)
  local def = D.FOES[rnd.foe] or D.FOES[#D.FOES]
  local skin = def.skin
  if f.n == 2 then skin = (D.FOES[rnd.foe + 1] or def).skin end
  return def, skin
end

local function glint_label(f) return "GLINT" .. (f.n > 1 and (" " .. f.n) or "") end

local function standin(f)
  local _, skin = foe_def(f)
  return spawn.chr(f.perch.pad, { skin = skin, weapon = "sniperrifle", behaviour = "spectator", face = { bx, bz } })
end

-- the shot's fate is a coin tossed here (the seeded RNG): the stock accuracy build-up (chr->shotbondsum, a hit when it reaches 1) grows by a
-- few hundredths a shot at 60 to 150 m, so a marksman who fires once per perch would never hit; a start of 1 makes his first shot the hit
local function marksman(f)
  local _, skin = foe_def(f)
  if f.will_hit == nil then f.will_hit = math.random() < rnd.hit end
  return spawn.chr(f.perch.pad, { skin = skin, weapon = "sniperrifle", behaviour = "snipe", shotbond = f.will_hit and 1 or 0, health = S.foe_hp,
                                  sight = S.sight, face = { bx, bz } })
end

local function start_glint(f)
  local p, b = choose_perch(f)
  if not p then f.t = now + 1; return end
  f.perch, f.bearing = p, b
  local c = standin(f)
  if not c then f.perch = nil; f.t = now + 0.5; return end
  f.chr, f.st, f.glint_end = c, "glint", now + rnd.glint
  ST.glints = ST.glints + 1
  if ar then
    pcall(ar.on, true)
    pcall(ar.point, p.x, p.y + 150, p.z, glint_label(f), 0xFFE060FF)
  end
  sfx(S.glint_sfx)
  toast(string.format(T.glint, clock_of(b), math.floor(p.d / 100)))
  callout(string.format(T.glint_big, clock_of(b, T.clock_fmt_big)), 2, string.format(T.glint_m, math.floor(p.d / 100)), math.max(1.2, rnd.glint + 0.4))
  log("GLINT %d: foe %d on pad %d at %.0f m, bearing %.0f, %s, %.1f s before the shot", ST.glints, f.n, p.pad, p.d / 100, b, clock_of(b), rnd.glint)
end

local function fire_stage(f)
  spawn.remove(f.chr)
  f.chr = nil
  if ar then pcall(ar.unpoint, glint_label(f)) end
  local c = marksman(f)
  if not c then f.glint_end = now + 0.25; return end   -- (a retry a tick later: the pad was only just freed)
  f.chr, f.st, f.swap_at, f.fired = c, "fire", now, false
  log("foe %d: the marksman takes the shot from pad %d (chr %d)", f.n, f.perch.pad, c)
end

local function relocate(f, why)
  ST.relocs = ST.relocs + 1
  if f.chr then spawn.remove(f.chr) end
  f.chr = nil
  if ar then pcall(ar.unpoint, glint_label(f)) end
  f.last_pad, f.last_bearing = f.perch and f.perch.pad, f.bearing
  f.perch, f.st, f.will_hit = nil, "wait", nil
  f.t = now + rand_in(rnd.hide)
  toast(T.gone)
  log("RELOCATE %d: foe %d leaves pad %s (%s), back in %.1f s", ST.relocs, f.n, tostring(f.last_pad), why, f.t - now)
end

local function foe_tick(f)
  if f.st == "wait" then
    if now >= f.t then start_glint(f) end
  elseif f.st == "glint" then
    if now >= f.glint_end then fire_stage(f) end
  elseif f.st == "fire" then
    if not f.chr then return end
    local n = spawn.shots(f.chr)
    if n and n > 0 and not f.fired then
      f.fired, f.fired_at = true, now
      ST.shots = ST.shots + 1
      log("SHOT: foe %d fired from pad %d, %.1f s after the swap", f.n, f.perch.pad, now - f.swap_at)
    end
    if f.fired and now - f.fired_at >= S.post_shot then relocate(f, "fired")
    elseif not f.fired and now - f.swap_at >= S.fire_timeout then relocate(f, "no shot in " .. S.fire_timeout .. " s") end
  end
end

-- ------------------------------------------------------------ the rushers ---
local function spawn_rushers(n)
  bond_xyz()
  local v = view_yaw()
  local ids = pads.far_from(bx, bz, S.rush_min, S.rush_max, true)
  local pool, loose = {}, {}
  for _, id in ipairs(ids) do
    local x, y, z = pads.pos(id)
    if x and math.abs(y - by) < 900 then
      loose[#loose + 1] = id
      if not v or math.abs(wrap(bearing(x, z) - v)) >= S.hide_cone then pool[#pool + 1] = id end
    end
  end
  if #pool < n then pool = loose end
  local made = 0
  for _ = 1, n do
    if #pool == 0 then break end
    local k = math.random(#pool)
    local pad = table.remove(pool, k)
    local gun = D.RUSHER_GUNS[math.random(#D.RUSHER_GUNS)]
    local skin = D.RUSHER_SKINS[math.random(#D.RUSHER_SKINS)]
    local ok, c = pcall(spawn.chr, pad, { skin = skin, weapon = gun, behaviour = "rush", accuracy = 25 + 3 * ST.round, health = 1.0, sight = S.sight,
                                         face = { bx, bz } })
    if ok and c then
      made = made + 1
      ST.rushers[#ST.rushers + 1] = c
      log("rusher %d (chr %d) on pad %d with %s", made, c, pad, gun)
    end
  end
  if made > 0 then toast(T.rushers); callout(T.rushers, 3, "", 2.5) end
  return made
end

local function clear_rushers()
  for _, c in ipairs(ST.rushers) do if spawn.alive(c) then spawn.remove(c) end end
  ST.rushers = {}
end

-- ------------------------------------------------------------ the rounds ---
local function new_foe(n, t0)
  return { n = n, st = "wait", t = t0 }
end

local function start_round(i)
  ST.round, rnd = i, ROUNDS[i]
  ST.phase = "round"
  round_t0 = now
  ST.foes, ST.killed = {}, 0
  ST.rush_done = (rnd.rushers or 0) == 0
  local def = D.FOES[rnd.foe] or D.FOES[#D.FOES]
  for n = 1, rnd.snipers do
    ST.foes[#ST.foes + 1] = new_foe(n, now + S.first_delay + (n > 1 and (rnd.second_at or 4) or 0))
  end
  if S.heal_each_round then pcall(bond.heal, 0, 1, 0.5) end
  give_kit()
  callout(string.format(T.round, i, N_ROUNDS) .. "  -  " .. def.name, 4, def.title, 4.0)
  log("ROUND %d/%d: %s, %s: %d marksmen, hit chance %.0f%%, glint %.1f s, %d rushers", i, N_ROUNDS, def.name, def.title, rnd.snipers, rnd.hit * 100, rnd.glint, rnd.rushers or 0)
end

local function end_run(win)
  if ended then return end
  ended = true
  ST.phase = "over"
  if ar then pcall(ar.reset) end
  clear_rushers()
  for _, f in ipairs(ST.foes) do if f.chr then spawn.remove(f.chr); f.chr = nil end end
  local fired, hits = bond.shots(0)
  fired, hits = fired or 0, hits or 0
  fired = math.max(fired, hits)   -- (a hit is a shot: the counters of a scripted test shot would otherwise show more hits than shots)
  local r = { win = win and true or false, rounds = stats.rounds, total = N_ROUNDS, time = math.floor(run_t * 100 + 0.5) / 100, shots = fired, hits = hits,
              headshots = stats.headshots, hits_taken = stats.hits_taken, foe = stats.killer, label = LABEL, stage = D.key(), round_times = stats.round_times }
  log("END: %s, %d/%d rounds won, %s, accuracy %d/%d, headshots %d, hits taken %d%s", win and "WON" or "DOWN", r.rounds, r.total, clockstr(run_t), hits, fired,
    r.headshots, r.hits_taken, r.foe ~= "" and (", felled by " .. r.foe) or "")
  ST.result = r
  if FLOW then
    ui.result(U.serialize(r))
    ui.retry({ mode = name, settings = cur and cur.settings or "", preset = "surface", label = LABEL, stage = D.STAGE.id })
    ui.arm(HUB_DEFS.lobby("sniper", "postgame", "surface", LABEL))
  end
  ST.end_at = now + (win and 4.0 or 2.5)
end

local function round_won()
  stats.rounds = stats.rounds + 1
  stats.round_times[#stats.round_times + 1] = math.floor(now - round_t0 + 0.5)
  clear_rushers()
  if ar then pcall(ar.reset) end
  log("ROUND %d WON in %.1f s", ST.round, now - round_t0)
  if ST.round >= N_ROUNDS then
    callout(T.you_win, 6, string.format("%d / %d", stats.rounds, N_ROUNDS), 4.0)
    end_run(true)
    return
  end
  ST.phase = "between"
  ST.next_at = now + S.breather
  callout(string.format(T.round_won, ST.round), 3, "", 3.0)
end

-- ----------------------------------------------------------------- events ---
on("stage_start", function()
  bond_xyz()
  if ar then
    pcall(ar.on, true)
    for _, l in ipairs({ "suspicion", "speech", "keys", "doors", "cameras", "radar" }) do pcall(ar.layer, l, false) end
    pcall(ar.layer, "points", true)
    pcall(ar.reset)
  end
  if bench and bench.sweep_off then pcall(bench.sweep_off) end
  if arena and arena.hud then   -- the Arena's styled HUD: the centre callouts (rounds, the glint) and the plate; the mode rows are hidden under it
    pcall(arena.hud, true)
    if arena.panel then pcall(arena.panel, { wave = 0, idle_title = T.title, idle_sub = D.STAGE.name, phase = "delay" }) end
  end
  give_kit()
  -- one marksman hit takes hit_damage of a health bar: the AI's hit is 0.125 x the rifle's damage x the difficulty's damage modifier (0.5, 0.75, 1, 1)
  local dmod = ({ 0.5, 0.75, 1.0, 1.0 })[(mission.difficulty() or 0) + 1] or 1.0
  local dmg = S.hit_damage / (0.125 * dmod)
  weapon.set("sniperrifle", "damage", dmg)
  log("difficulty %d: the sniper rifle's damage is %.2f, one hit takes %.0f%% of a health bar", mission.difficulty() or 0, dmg, S.hit_damage * 100)
  if FLOW then   -- an abort from the watch comes back to the lobby too
    ui.retry({ mode = name, settings = cur and cur.settings or "", preset = "surface", label = LABEL, stage = D.STAGE.id })
    ui.result(U.serialize({ aborted = true, win = false, rounds = 0, total = N_ROUNDS, time = 0, label = LABEL, stage = D.key() }))
    ui.arm(HUB_DEFS.lobby("sniper", "postgame", "surface", LABEL))
  end
  callout(T.get_ready, 3, T.get_ready_sub, 5.0)
  log("stage start: %s, %d rounds, Bond at pad %s, the sight sweep begins", D.STAGE.name, N_ROUNDS, tostring(bond.pos(0) and "?" or "?"))
end)

on("ui_tick", function(dt)
  if ST.phase ~= "sweep" or not sweep_co then return end
  local ok, pad, seen = coroutine.resume(sweep_co)
  if not ok then log("sweep error: %s", tostring(pad)); sweep_co = nil; return end
  if coroutine.status(sweep_co) == "dead" then
    sweep_co = nil
    if not pad then
      log("NO PERCH: none of Bond's candidate pads exists on this stage")
      hud.set(1, "SNIPER DUEL: run it on Surface (GE_STAGE=surface)")
      ended = true
      return
    end
    ST.bond_pad, ST.perches = pad, seen
    bond_xyz()
    local cx, cz = 0, 0
    for _, p in ipairs(seen) do cx, cz = cx + p.x, cz + p.z end
    if #seen > 0 and bench and bench.look then
      cx, cz = cx / #seen, cz / #seen
      pcall(bench.look, 0, bearing(cx, cz), D.BOND.pitch)
    end
    log("Bond's perch is pad %d (%.0f, %.0f, %.0f) with %d enemy perches in the band", pad, bx, by, bz, #seen)
    ST.phase = "ready"
    ST.next_at = now + 1.0
  end
end)

on("tick", function(dt)
  now = now + dt
  if ended then
    if ST.end_at and now >= ST.end_at then
      ST.end_at = nil
      pcall(hud.panel, nil)   -- (the panel and the styled HUD would otherwise still be up on the hub's stage)
      hud.clear()
      if arena and arena.hud then pcall(arena.hud, false) end
      if ST.result and ST.result.win then mission.win("Sniper duel won") else mission.fail("Agent down") end
    end
    return
  end
  if toast_until > 0 and now >= toast_until then toast_until = 0; hud.set(3, "") end
  if ST.phase == "ready" and now >= ST.next_at then start_round(1)
  elseif ST.phase == "between" and now >= ST.next_at then start_round(ST.round + 1)
  elseif ST.phase == "round" then
    run_t = run_t + dt
    for _, f in ipairs(ST.foes) do foe_tick(f) end
    if not ST.rush_done and now - round_t0 >= (rnd.rush_at or 8) then
      ST.rush_done = true
      spawn_rushers(rnd.rushers)
    end
    -- the panel
    local snipers_left = rnd.snipers - ST.killed
    local rl = 0
    for _, c in ipairs(ST.rushers) do if spawn.alive(c) then rl = rl + 1 end end
    local def = D.FOES[rnd.foe] or D.FOES[#D.FOES]
    local fired, hits = bond.shots(0)
    if arena and arena.panel then   -- the styled HUD's plate (top left), in its idle form: the round and the name
      pcall(arena.panel, { wave = 0, idle_title = string.format(T.hud_plate, ST.round, N_ROUNDS), idle_sub = def.name, phase = "fight" })
    end
    hud.panel({ string.format(T.hud_round, ST.round, N_ROUNDS, def.name), def.title, string.format(T.hud_left, snipers_left, rl),
                string.format(T.hud_time, clockstr(run_t), fired or 0, hits or 0) })
  end
end)

on("chr_died", function(victim, killer)
  for _, f in ipairs(ST.foes) do
    if f.chr == victim and f.st ~= "dead" then
      f.st, f.chr = "dead", nil
      ST.killed = ST.killed + 1
      local hs = spawn.headshot(victim)
      if hs then stats.headshots = stats.headshots + 1 end
      if ar then pcall(ar.unpoint, glint_label(f)) end
      log("KILL: foe %d down%s (%d of %d)", f.n, hs and " by a headshot" or "", ST.killed, rnd.snipers)
      if ST.killed >= rnd.snipers and ST.phase == "round" then round_won() end
      return
    end
  end
end)

on("damage", function(victim, attacker, amount)
  if victim ~= bond.id(0) or (amount or 0) <= 0 then return end
  stats.hits_taken = stats.hits_taken + 1
  for _, f in ipairs(ST.foes) do
    if f.chr and f.chr == attacker then
      local def = foe_def(f)
      stats.killer = def.name
      log("HIT on Bond by foe %d (%.2f damage)", f.n, amount)
    end
  end
end)

on("bond_died", function()
  if ended then return end
  if stats.killer == "" and rnd then stats.killer = "THE RUSHERS" end
  end_run(false)
end)
