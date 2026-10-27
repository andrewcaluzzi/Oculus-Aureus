-- Arena elite enemies with affixes (drop A7, modding/arena.md). Shipped to Data/Scripts/modes/_arena_elites.lua and included by
-- arena.lua. Defines the global ARENA_ELITES.new(S) -> X.
--
-- Diablo-style: an ordinary enemy (never a boss, never the crowd) may roll to be an ELITE with 1 or 2 affixes. Elites are tougher
-- (affix_hp), drawn a little bigger (affix_scale), counted on the HUD ("ELITES 2") and announced when they die
-- ("ELITE DOWN: SWIFT EXPLOSIVE"). (Not the A4 Spetsnaz / Caverns "elite" troops, whose settings are elite_from / elite_share*:
-- those are a choice of body; an affix elite can be any enemy, including one of them.)
--   elites            false (default) = none;  true = on
--   affix_from        first wave with affix elites (3)
--   affix_chance      chance per enemy at affix_from (0.04), + affix_chance_inc a wave (0.012), at most affix_chance_max (0.25)
--   affix_two_chance  chance an elite has two affixes (0.20 + 0.02 a wave after affix_from, at most 0.60)
--   affix_list        the affixes to pick from (default all: armoured, swift, knives, explosive, regen, marksman)
--   affix_hp          health x this for every elite (1.3)
--   affix_scale       elites are drawn this much bigger (1.15; 1 = off)
--   affix_regen       regen: fraction of full health healed per second (0.05; wounds only, not armour)
--   affix_explosion   explosive: "small" | "medium" (default) | "grenade";  affix_explode_delay seconds after the death (0.7)
--   affix_armour      armoured: extra armour as a multiple of the enemy's health (2.0)

ARENA_ELITES = {}

local AFFIX = {
  armoured  = { name = "ARMOURED", spec = function(s, S) s.armour = (s.armour or 0) + (S.affix_armour or 2.0) * (s.health or 1) end },
  swift     = { name = "SWIFT",    spec = function(s) s.anim_speed = math.max(s.anim_speed or 1, 1.45); s.speed = 100 end },
  knives    = { name = "KNIFE THROWER", spec = function(s)   -- keeps its gun AND its movement (no A7 polish keep-distance for it)
    if not s.knives and s.knife_keep == nil then s.knife_keep = 0 end
    s.knives = true; s.knife_cooldown = 0.8 end },
  explosive = { name = "EXPLOSIVE" },
  regen     = { name = "REGENERATING" },
  marksman  = { name = "MARKSMAN", spec = function(s) s.accuracy = 100 end },
}
local ORDER = { "armoured", "swift", "knives", "explosive", "regen", "marksman" }
ARENA_ELITES.AFFIX, ARENA_ELITES.ORDER = AFFIX, ORDER

function ARENA_ELITES.new(S)
  local X = { live = {}, pending = {}, made = 0, killed = 0, exploded = 0, healed = 0 }
  local list = S.affix_list or ORDER

  local function clamp(v, lo, hi) return math.max(lo, math.min(hi, v)) end

  local function chance(w)
    return clamp((S.affix_chance or 0.04) + (S.affix_chance_inc or 0.012) * (w - (S.affix_from or 3)), 0, S.affix_chance_max or 0.25)
  end

  local function two_chance(w)
    return clamp((S.affix_two_chance or 0.20) + 0.02 * (w - (S.affix_from or 3)), 0, 0.60)
  end

  -- the wave's queue: mark some entries elite (before they spawn). Returns how many.
  function X.wave_start(w, queue)
    X.live, X.pending = {}, {}
    if not S.elites or w < (S.affix_from or 3) or #list == 0 then return 0 end
    local n = 0
    for _, e in ipairs(queue) do
      if not e.boss and e.spec.behaviour ~= "spectator" and math.random() < chance(w) then
        local a1 = list[math.random(#list)]
        local affixes = { a1 }
        if #list > 1 and math.random() < two_chance(w) then
          local a2 = a1
          while a2 == a1 do a2 = list[math.random(#list)] end
          affixes[2] = a2
        end
        local s = e.spec
        s.health = (s.health or 1) * (S.affix_hp or 1.3)
        if s.armour then s.armour = s.armour * (S.affix_hp or 1.3) end
        local names = {}
        for _, a in ipairs(affixes) do
          local d = AFFIX[a]
          if d then
            if d.spec then d.spec(s, S) end
            names[#names + 1] = d.name
          end
        end
        e.affixes, e.affix_name = affixes, table.concat(names, " ")
        n = n + 1
      end
    end
    return n
  end

  function X.spawned(c, spec, entry)
    if not entry.affixes then return end
    local t = { chr = c, name = entry.affix_name, born = 0 }
    for _, a in ipairs(entry.affixes) do t[a] = true end
    X.live[c] = t
    X.made = X.made + 1
    if (S.affix_scale or 1.15) ~= 1 then spawn.scale(c, S.affix_scale or 1.15) end
    print(string.format("ge: arena: ELITE chr %d: %s (hp x%.1f, armour x%.1f, anim %.2f, acc %s%s)", c, t.name, spec.health or 1,
      spec.armour or 0, spec.anim_speed or 1, tostring(spec.accuracy), spec.knives and ", knives" or ""))
  end

  -- an enemy died: returns the HUD text for an elite, else nil
  function X.died(victim, now)
    local t = X.live[victim]
    if not t then return nil end
    X.live[victim] = nil
    X.killed = X.killed + 1
    if t.explosive then X.pending[#X.pending + 1] = { chr = victim, at = now + (S.affix_explode_delay or 0.7) } end
    return "ELITE DOWN: " .. t.name
  end

  function X.count()
    local n = 0
    for c, _ in pairs(X.live) do if spawn.alive(c) then n = n + 1 end end
    return n
  end

  -- every tick: regenerate, and set off the explosive corpses whose fuse has run out
  function X.tick(dt, now)
    for c, t in pairs(X.live) do
      if t.regen and spawn.heal(c, (S.affix_regen or 0.05) * dt) then X.healed = X.healed + 1 end
    end
    if #X.pending > 0 then
      local keep = {}
      for _, p in ipairs(X.pending) do
        if now >= p.at then
          if spawn.explode(p.chr, S.affix_explosion or "medium") then X.exploded = X.exploded + 1
          else print(string.format("ge: arena: ELITE chr %d: the corpse was gone, no explosion", p.chr)) end
        else
          keep[#keep + 1] = p
        end
      end
      X.pending = keep
    end
  end

  return X
end
