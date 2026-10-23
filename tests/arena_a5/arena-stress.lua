-- A5 drop stress test: endless waves, every enemy killed (real death, drops) a couple of seconds after it spawns.
ARENA_SETTINGS = { mode = "endless", boss_every = 5, breather = 1, start_delay = 1, max_alive = 18, max_alive_cap = 24,
  crowd = 100, debug_kill = 2.0, first_wave = 1, supplies = false }
include("arena")
