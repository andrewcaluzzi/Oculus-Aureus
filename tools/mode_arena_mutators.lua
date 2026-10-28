-- Arena wave mutators (drop A7, modding/arena.md). Shipped to Data/Scripts/modes/_arena_mutators.lua and included by arena.lua
-- (a leading "_" keeps it out of the Mods page's Game Mode list). Defines the global ARENA_MUTATORS.new(S) -> M.
--
-- A wave can roll ONE mutator. It is applied when the wave starts (modifiers through mods.set, per-spawn tweaks on the wave's
-- queue, the arena fog) and taken off when the wave is cleared or the run ends: every modifier goes back to the value it had,
-- the fog to the Colosseum's own sky. Settings (S = arena.lua's settings, ARENA_SETTINGS overrides them):
--   mutators          false (default) = never;  true = waves may roll one
--   mutator_from      first wave that can roll one (3)
--   mutator_chance    chance at mutator_from (0.30), + mutator_chance_inc a wave (0.02), at most mutator_chance_max (0.60)
--   mutator_bosses    false (default): boss waves never get one
--   mutator_list      the names to pick from (default: all of DEFS below, in this order)
--   mutator_pick      "random" (default; never the same one twice in a row) | "cycle" (the list in order: tests)
--   mutator_fog_far   LOW VISIBILITY: how far one can see (2600 units; the pit is 9000 across)
--   rain_slip         THUNDERBALL (rain, "everyone slips"): how long Bond's walk takes to catch up with the stick, in seconds at full
--                     rain (0.25; 0 = no slip): he overshoots, stops late and swings wide (port_arenarain.c)
--   rain_grip         ... the grenades' landed-slide friction (0.5: they slide about twice as far; 1 = dry)
--   rain_enemy_speed  ... the wave's enemies move (and act) at this animation speed (0.85)
--   rain_stumble      ... chance that an enemy who turns hard or stops short while running slips (0.35 a time; at most once in 3 s)
--   rain_mood         ... the crowd's favour when it starts ("they hate the weather": -8)
--   rain_lightning    ... LIGHTNING while it rains (true): a flash, a bolt and a stock explosion near a character (an enemy or a Bond)
--   rain_strike_min / rain_strike_max   ... seconds between strikes, a random time in between (2.0 .. 4.0: one every 3 s on average)
--   rain_strike_bond  ... share of the strikes aimed at a Bond (0.35); the rest at a random live enemy (a Bond when there is none)
--   rain_strike_near_min / rain_strike_near   ... how far from the character's feet it lands, units, any direction (100 .. 450;
--                     walked along the floor: a wall or the pit's edge makes it shorter)
--   rain_strike_fuse  ... seconds from the aim to the strike (0.6): the point is where the character WAS and it crackles meanwhile,
--                     so whoever keeps moving gets clear and whoever stands still is in it
--   rain_strike_blast ... the explosion: "small" | "medium" | "grenade" (default: a grenade's, 480-unit damage range) | "remote"
--
-- Names: knives, paintball, one_hit, turbo, fog, dk, tough, bottomless, rain (see DEFS).

ARENA_MUTATORS = {}

local DEFS = {
  -- every enemy throws knives (long range, fast) and never fires a bullet
  knives = { title = "KNIVES ONLY",
    spec = function(spec)
      -- a gunman turned knife thrower keeps his own approach: the A7 polish keep-distance AI (arena_polish.lua, knife_keep) is for
      -- the real knife throwers only (with it the whole wave stood off at 32-38 m and hit Bond 7 times in 40 s instead of 24)
      if not spec.knives and spec.knife_keep == nil then spec.knife_keep = 0 end
      spec.knives, spec.knife_range, spec.knife_cooldown = true, 6000, 0.9
      if spec.weapon == nil or spec.weapon == "none" or spec.weapon == "knife" then spec.weapon = "pp7" end
    end,
    spawned = function(c) spawn.knife_only(c, true) end },
  -- the stock Paintball cheat
  paintball = { title = "PAINTBALL", mods = { paintball = true } },
  -- every hit kills an enemy, but they hit twice as hard
  one_hit = { title = "ONE-HIT KILLS", mods = { one_hit_kills = true, enemy_damage = 200 } },
  -- the stock Turbo cheat (Bond), and every enemy at 1.5x animation speed (the A4 turbo enemies)
  turbo = { title = "TURBO", mods = { turbo = true },
    spec = function(spec) spec.anim_speed = math.max(spec.anim_speed or 1, 1.5) end },
  -- a thick grey fog: the stands vanish, enemies come out of the murk (their sight is a stan ray: they still see Bond)
  fog = { title = "LOW VISIBILITY", fog = true },
  -- the stock DK Mode cheat: big heads, long arms
  dk = { title = "BIG HEADS", mods = { dk_mode = true } },
  -- enemies take half damage
  tough = { title = "IRON SKIN", mods = { enemy_health = 200 } },
  -- infinite ammo for Bond, and half as many enemies again
  bottomless = { title = "BOTTOMLESS CLIPS", mods = { infinite_ammo = true }, extra = 0.5 },
}

-- THUNDERBALL (AC: "everyone slips"): rain. C (port_arenarain.c, arena.rain) rolls it in over 1.5 s: Bond's walk slips, grenades
-- slide further, the sky goes overcast, the picture dims and the streaks fall (port_arenarain_draw.c). Here: the wave's enemies are
-- slower (anim_speed), an enemy who turns hard or stops short while running may slip (a stock hit reaction, spawn.anim; the mode's
-- seeded RNG, so the same on every machine), the crowd sulks, the rain patters (stock water splashes), and lightning strikes near
-- someone every few seconds (below).
local RAIN_SLIPS = { "slip_left", "slip_right", "slip_fall" }
local rain = { seen = {}, t = 0, slips = 0, patter = 0, next_strike = 0, strikes = 0, at_bond = 0 }
local function RS() return ARENA_MUTATORS.S or {} end

-- LIGHTNING (AC: "a massive flash of light, a zap to the ground and an explosion. random spot in the map but close to a random
-- character (inc Bond) so you have to keep moving"). The target, the direction and the distance come from the mode's seeded RNG
-- (math.random, the same on every machine; the direction without trig: a point in the unit square kept inside the circle, scaled
-- with sqrt, which is exact everywhere). arena.strike (port_arenarain.c) walks the point along the floor, holds it for the fuse and
-- sets off a stock explosion there with no owner (it hurts Bond too); the flash, the bolt, the crackle and the thunder are drawn
-- and played by port_arenarain_draw.c (presentation). prefer_bond: always at a Bond (the test key).
local function strike_target(prefer_bond)
  local bonds = {}
  for p = 0, (bond.count() or 1) - 1 do if bond.dead(p) == false then bonds[#bonds + 1] = p end end
  local live = ARENA_HOOKS and ARENA_HOOKS.live and ARENA_HOOKS.live() or {}
  if #bonds > 0 and (prefer_bond or #live == 0 or math.random() < (RS().rain_strike_bond or 0.35)) then
    local p = bonds[math.random(#bonds)]
    return { bond = p }, "bond", p
  end
  if #live == 0 then return nil end
  local c = live[math.random(#live)]
  return c, "chr", c
end

function ARENA_MUTATORS.strike(prefer_bond)
  local S = RS()
  local who, kind, id = strike_target(prefer_bond)
  if who == nil then return nil end
  local dx, dz, d2 = 0, 0, 0
  repeat dx, dz = math.random() * 2 - 1, math.random() * 2 - 1; d2 = dx * dx + dz * dz until d2 > 0.0025 and d2 <= 1
  local lo, hi = S.rain_strike_near_min or 100, S.rain_strike_near or 450
  local r = lo + (hi - lo) * math.random()
  local d = math.sqrt(d2)
  local x, _, z, serial = arena.strike(who, dx / d * r, dz / d * r, S.rain_strike_blast or "grenade", S.rain_strike_fuse or 0.6)
  if not x then
    print(string.format("ge: arena: RAIN lightning at %s %d refused", kind, id))
    return nil
  end
  rain.strikes = rain.strikes + 1
  if kind == "bond" then rain.at_bond = rain.at_bond + 1 end
  print(string.format("ge: arena: RAIN lightning %d at %s %d: lands (%.0f, %.0f), %.0f units off", serial, kind, id, x, z, r))
  return x, z
end
local function weather_text() local w = ARENA_THEME and ARENA_THEME.weather or {} return w end
DEFS.rain = {
  title = (weather_text().rain or {})[1] or "THUNDERBALL",
  spec = function(spec) spec.anim_speed = (spec.anim_speed or 1) * (RS().rain_enemy_speed or 0.85) end,
  start = function()
    local S = RS()
    rain.seen, rain.t, rain.slips, rain.patter, rain.strikes, rain.at_bond = {}, 0, 0, 0, 0, 0
    -- (the first strike once the rain has rolled in: 1.5 s, then the usual wait)
    rain.next_strike = 1.5 + (S.rain_strike_min or 2.0) + ((S.rain_strike_max or 4.0) - (S.rain_strike_min or 2.0)) * math.random()
    arena.rain(S.rain_slip or 0.25, S.rain_grip or 0.5)
    local W = weather_text()
    if ARENA_THEME and ARENA_THEME.callout then pcall(ARENA_THEME.callout, (W.rain or {})[1] or "THUNDERBALL", 3, (W.rain or {})[2]) end
    local CR = ARENA_CROWD and ARENA_CROWD.live
    if CR and CR.mood then CR.mood(S.rain_mood or -8, W.crowd) end
    if ARENA_HOOKS and ARENA_HOOKS.react then pcall(ARENA_HOOKS.react, "weather") end
  end,
  stop = function()
    arena.rain(0)
    print(string.format("ge: arena: RAIN over: %d slips, %d lightning strikes (%d at a Bond)", rain.slips, rain.strikes, rain.at_bond))
  end,
  tick = function(dt)
    local S = RS()
    rain.t = rain.t + dt
    if S.rain_lightning ~= false and rain.t >= rain.next_strike then   -- LIGHTNING: the next strike, and the wait after it
      local lo = S.rain_strike_min or 2.0
      rain.next_strike = rain.t + lo + math.max(0, (S.rain_strike_max or 4.0) - lo) * math.random()
      ARENA_MUTATORS.strike(false)
    end
    if sound and sound.sfx_burst and rain.t >= rain.patter then   -- the patter: quiet stock water splashes, spread over the stereo field
      rain.patter = rain.t + 1.6
      pcall(sound.sfx_burst, { "HIT_BULLET_WATER" }, { 5, 9 }, { vol = { 0.04, 0.10 }, pitch = { 0.7, 1.5 }, gap = { 0.06, 0.28 }, pan = 1 })
    end
    local live = ARENA_HOOKS and ARENA_HOOKS.live and ARENA_HOOKS.live() or {}
    local p = S.rain_stumble or 0.35
    for _, c in ipairs(live) do
      local x, z = spawn.where(c)
      local st = rain.seen[c]
      if x and st and dt > 0 then
        local vx, vz = (x - st.x) / dt, (z - st.z) / dt
        local sp = math.sqrt(vx * vx + vz * vz)
        if st.sp and st.sp > 220 and rain.t - (st.last or -99) > 3 then
          local turn = sp > 80 and (vx * st.vx + vz * st.vz) / (sp * st.sp) < 0.35   -- a turn of ~70 degrees or more
          local stop = sp < st.sp * 0.3
          if (turn or stop) and math.random() < p then
            local anim = RAIN_SLIPS[math.random(#RAIN_SLIPS)]
            if spawn.anim(c, anim, true) then
              st.last = rain.t
              rain.slips = rain.slips + 1
              print(string.format("ge: arena: RAIN slip: chr %d %s (%s, speed %.0f -> %.0f)", c, anim, turn and "turn" or "stop", st.sp, sp))
            end
          end
        end
        st.vx, st.vz, st.sp = vx, vz, sp
      end
      if x then
        st = st or {}
        st.x, st.z = x, z
        rain.seen[c] = st
      end
    end
  end,
}
local ORDER = { "knives", "paintball", "one_hit", "turbo", "fog", "dk", "tough", "bottomless", "rain" }

ARENA_MUTATORS.DEFS, ARENA_MUTATORS.ORDER = DEFS, ORDER

local function copy(t)
  local o = {}
  for k, v in pairs(t) do o[k] = v end
  return o
end

local function modstr(v) return type(v) == "boolean" and tostring(v) or string.format("%d", v) end

function ARENA_MUTATORS.new(S)
  ARENA_MUTATORS.S = S   -- (the rain's settings)
  local M = { cur = nil, name = nil, saved = nil, last = nil, cycle_i = 0, rolled = 0, resets_ok = 0, resets_bad = 0 }
  local function chance(w)
    local c = (S.mutator_chance or 0.30) + (S.mutator_chance_inc or 0.02) * (w - (S.mutator_from or 3))
    return math.max(0, math.min(S.mutator_chance_max or 0.60, c))
  end

  local function pick()
    local list = S.mutator_list or ORDER   -- read at each roll: the Whacky module (loaded after this) may set it from S.whacky
    if #list == 0 then return nil end
    if S.mutator_pick == "cycle" then
      M.cycle_i = M.cycle_i % #list + 1
      return list[M.cycle_i]
    end
    local pool = {}
    for _, v in ipairs(list) do if v ~= M.last or #list == 1 then pool[#pool + 1] = v end end
    return pool[math.random(#pool)]
  end

  -- start of a wave: maybe roll one; returns its title or nil. queue entries' specs are edited in place (before they spawn).
  -- A DEF may also have start(w, queue) / stop() / tick(dt) (the Whacky ones, arena_whacky.lua).
  function M.wave_start(w, queue, boss_wave)
    M.wave_end("new wave")   -- never two at once (a run that skipped the clear: test hooks)
    if not S.mutators or w < (S.mutator_from or 3) or (boss_wave and not S.mutator_bosses) then return nil end
    if math.random() >= chance(w) then return nil end
    return M.apply(pick(), w, queue)
  end

  -- turn mutator `name` on now (the roll above, or a test key mid-wave with an empty queue)
  function M.apply(name, w, queue)
    queue = queue or {}
    M.wave_end("new mutator")
    local d = name and DEFS[name]
    if not d then
      if name then print("ge: arena: mutator '" .. tostring(name) .. "' is unknown (mutator_list)") end
      return nil
    end
    M.cur, M.name, M.last, M.saved = d, name, name, {}
    M.rolled = M.rolled + 1
    for k, v in pairs(d.mods or {}) do
      M.saved[k] = mods.get(k)
      mods.set(k, v)
    end
    if d.fog then arena.fog(S.mutator_fog_far or 2600, 0x70, 0x70, 0x78) end
    if d.extra and d.extra > 0 then
      local n0 = #queue
      for i = 1, math.floor(n0 * d.extra + 0.5) do
        local src = queue[(i - 1) % n0 + 1]
        if not src.boss then
          local e = copy(src)
          e.spec = copy(src.spec)
          e.n = n0 + i
          queue[#queue + 1] = e
        end
      end
    end
    if d.spec then for _, e in ipairs(queue) do if not e.boss then d.spec(e.spec) end end end
    if d.start then d.start(w, queue) end
    local parts = {}
    for k, v in pairs(d.mods or {}) do parts[#parts + 1] = k .. "=" .. modstr(v) .. " (was " .. modstr(M.saved[k]) .. ")" end
    table.sort(parts)
    print(string.format("ge: arena: MUTATOR wave %d: %s (%s)%s%s; %d enemies", w, d.title, name,
      #parts > 0 and (" mods " .. table.concat(parts, ", ")) or "", d.fog and (" fog " .. tostring(arena.fog())) or "", #queue))
    return d.title
  end

  -- a queued enemy has spawned as chr c
  function M.spawned(c, spec, entry)
    if M.cur and M.cur.spawned and not entry.boss then M.cur.spawned(c, spec) end
  end

  -- end of the wave (cleared) or of the run: take it off and check that everything is back
  function M.wave_end(why)
    if not M.cur then return end
    local d, bad = M.cur, {}
    if d.stop then
      local ok, err = pcall(d.stop)
      if not ok then bad[#bad + 1] = "stop: " .. tostring(err) end
    end
    for k, v in pairs(M.saved or {}) do
      mods.set(k, v)
      if mods.get(k) ~= v then bad[#bad + 1] = k .. "=" .. modstr(mods.get(k)) .. " (want " .. modstr(v) .. ")" end
    end
    if d.fog then
      arena.fog(0)
      if arena.fog() ~= 0 then bad[#bad + 1] = "fog " .. tostring(arena.fog()) end
    end
    if #bad == 0 then M.resets_ok = M.resets_ok + 1 else M.resets_bad = M.resets_bad + 1 end
    print(string.format("ge: arena: MUTATOR %s off (%s): reset %s", d.title, why or "?", #bad == 0 and "ok" or ("FAILED: " .. table.concat(bad, ", "))))
    M.cur, M.name, M.saved = nil, nil, nil
  end

  function M.title() return M.cur and M.cur.title or nil end

  -- every mode tick: a mutator with a clock (Whacky: speed swaps, glitch bursts, crowd throws)
  function M.tick(dt)
    if M.cur and M.cur.tick then M.cur.tick(dt) end
  end

  return M
end
