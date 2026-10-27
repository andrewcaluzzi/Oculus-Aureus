-- A7 perf: the crowd alone (HEAVY_CROWD spectators, default 200), no enemies (wave 1 never starts).
ARENA_SETTINGS = { mode = "endless", crowd = HEAVY_CROWD or 200, start_delay = 9999, ammo_every = 0, supplies = false, pickup_markers = false }
include("arena")
