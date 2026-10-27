-- A7: best scores, endless mode, a real death. Starts at wave 8 (scores_force records despite the first_wave hook); run with NO cheats
-- so the soldiers kill Bond where he stands. Expected: "OVER wave 8 ...", "SCORES endless: ...", the HUD's last lines (MEDAL: BRONZE
-- for wave 8 with the default medals_endless {5, 10, 20, 30}) and the KILLED IN ACTION report page with the arena lines.
ARENA_SETTINGS = { mode = "endless", boss_every = 0, breather = 2, start_delay = 1, crowd = 20, first_wave = 8, ammo_every = 0,
  armour_chance = 0, scores_force = true, mutators = true, mutator_from = 1, mutator_chance = 1, mutator_chance_max = 1,
  mutator_list = { "one_hit" } }
include("arena")
