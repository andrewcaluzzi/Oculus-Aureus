-- A7 soak: the full 20 fixed waves (bosses every 5) with mutators (70% of the non-boss waves from wave 2) and affix elites (35% of the
-- enemies from wave 1) on, crowd 100, centre ammo every 20 s, every enemy killed 3 s after it spawns (many deaths, explosive corpses,
-- drops). Run invulnerable. Expected: "WON wave 20", every "MUTATOR ... off": "reset ok", no crash / A7 error / pool warning.
ARENA_SETTINGS = { mode = "fixed", waves = 20, boss_every = 5, breather = 1, start_delay = 1, crowd = 100, debug_kill = 3.0,
  ammo_every = 20,
  mutators = true, mutator_from = 2, mutator_chance = 0.7, mutator_chance_max = 0.7,
  elites = true, affix_from = 1, affix_chance = 0.35, affix_chance_max = 0.35 }
include("arena")
