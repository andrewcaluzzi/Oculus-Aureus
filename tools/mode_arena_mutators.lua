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
--
-- Names: knives, paintball, one_hit, turbo, fog, dk, tough, bottomless (see DEFS).

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
local ORDER = { "knives", "paintball", "one_hit", "turbo", "fog", "dk", "tough", "bottomless" }

ARENA_MUTATORS.DEFS, ARENA_MUTATORS.ORDER = DEFS, ORDER

local function copy(t)
  local o = {}
  for k, v in pairs(t) do o[k] = v end
  return o
end

local function modstr(v) return type(v) == "boolean" and tostring(v) or string.format("%d", v) end

function ARENA_MUTATORS.new(S)
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
