-- A6: health per kill. Wave 1: one enemy of each tier (light, normal, armoured, heavy) with Bond hurt to 0.20; wave 2: a boss with Bond at 0.90
-- (the +25% must stop at 1.00); wave 3: a normal enemy with Bond at full health (no overheal). Every enemy is killed 1.5 s after it spawns.
-- Expected ge: arena: KILL HEAL lines: 0.20->0.23 (light), ->0.26 (normal), ->0.30 (armoured), ->0.36 (heavy); 0.90->1.00 (boss); 1.00->1.00.
local function foe(tier, boss)
  return { n = 1, boss = boss, spec = { skin = "russian_soldier", weapon = "none", behaviour = "hold", health = 0.2, _tier = tier } }
end
ARENA_SETTINGS = {
  mode = "endless", boss_every = 0, breather = 2, start_delay = 1, crowd = 0, first_wave = 1, ammo_every = 0, armour_chance = 0,
  debug_heal = true, debug_kill = 1.5, health_gifts = false,
  debug_queue = function(w)
    local h = bond.vitals(0)
    if w == 1 then
      bond.heal(0, 0.20 - h, 0)
      return { foe("light"), foe("normal"), foe("armoured"), foe("heavy") }
    elseif w == 2 then
      bond.heal(0, 0.90 - h, 0)
      return { foe("boss", true) }
    elseif w == 3 then
      bond.heal(0, 1.0 - h, 0)
      return { foe("normal") }
    end
    return {}
  end,
}
include("arena")
