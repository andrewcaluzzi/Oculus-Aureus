-- A7: explosive death, many at once. Every enemy is an EXPLOSIVE elite (affix_list = explosive only), killed 3 s after it spawns, so
-- the corpses blow up near the next ones out of the gate (chain kills: "ELITE DOWN" with killer CHR_NONE). Waves 1-8, crowd 100.
-- Expected: one "explosion type 12" line per elite death (unless its corpse was already gone), no crash, no pool warnings.
ARENA_SETTINGS = { mode = "endless", boss_every = 0, breather = 1, start_delay = 1, crowd = 100, first_wave = 1, debug_kill = 3.0,
  ammo_every = 0, armour_chance = 0,
  elites = true, affix_from = 1, affix_chance = 1, affix_chance_max = 1, affix_two_chance = 0, affix_list = { "explosive" } }
include("arena")
