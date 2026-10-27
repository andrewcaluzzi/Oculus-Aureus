-- A7: affix elites. Endless, a boss every 5 waves, elites from wave 1 with a 60% chance per enemy, every enemy killed 4 s after it
-- spawns, crowd 30. Expected: "ELITE chr N: <affixes>" lines (one or two of ARMOURED SWIFT KNIFE THROWER EXPLOSIVE REGENERATING
-- MARKSMAN; elites have hp x1.3 and the affix's numbers in the line), "ELITE DOWN" flashes, "explosion type 12 at chr" lines for the
-- explosive ones, no elite on the boss (wave 5: the boss chr is never in an ELITE line), and no crash.
ARENA_SETTINGS = { mode = "endless", boss_every = 5, breather = 2, start_delay = 1, crowd = 30, first_wave = 1, debug_kill = 4.0,
  ammo_every = 0, armour_chance = 0,
  elites = true, affix_from = 1, affix_chance = 0.6, affix_chance_max = 0.6 }
include("arena")
