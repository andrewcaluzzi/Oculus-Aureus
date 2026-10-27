-- A7 perf: a heavy wave: crowd HEAVY_CROWD (default 200) + up to 40 enemies at wave 12. Bond is invulnerable (GE_CHEATS=invuln).
ARENA_SETTINGS = { mode = "endless", first_wave = 12, crowd = HEAVY_CROWD or 200, start_delay = 2, breather = 4, max_alive = 40, max_alive_cap = 40,
  boss_every = 0, ammo_every = 0, supplies = false, pickup_markers = false }
include("arena")
