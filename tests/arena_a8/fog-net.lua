-- The LOW VISIBILITY fog in netplay (v0.2: the fade drives g_ScaledFarFogIntensity, which the sim reads, per tick): co-op over the
-- lockstep link with the fog mutator on every wave from wave 1, enemies killed 3 s after they spawn, so the fog fades in and out at
-- each wave. Host and guest must agree tick for tick. Run (alone):
--   STAGE=colosseum MODE=fog-net LUATEST= MOVES= sh tests/net_modes.sh net <tag> <port> <ticks>
ARENA_SETTINGS = { waves = 4, boss_every = 0, crowd = 30, start_delay = 2, breather = 2, debug_kill = 3.0,
  mutators = true, mutator_from = 1, mutator_chance = 1, mutator_chance_max = 1, mutator_list = { "fog" } }
include("arena-coop")
