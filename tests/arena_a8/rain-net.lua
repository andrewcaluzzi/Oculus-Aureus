-- The RAIN mutator (THUNDERBALL) in co-op and netplay: the slip (both Bonds), the enemies' slips (the mode's RNG) and the grenades'
-- grip are sim, so host and guest must agree tick for tick. Rain on every wave from wave 1, enemies killed 6 s after they spawn (time
-- to run, turn and slip). Run (alone):
--   STAGE=colosseum MODE=rain-net LUATEST= MOVES= sh tests/net_modes.sh net <tag> <port> <ticks>
-- or locally twice with GE_HASHLOG for the state hash (tests/arena_a8/rain-hash.sh).
ARENA_SETTINGS = { waves = 4, boss_every = 0, crowd = 30, start_delay = 2, breather = 2, debug_kill = 6.0, rain_stumble = 0.9,
  mutators = true, mutator_from = 1, mutator_chance = 1, mutator_chance_max = 1, mutator_list = { "rain" } }
include("arena-coop")
