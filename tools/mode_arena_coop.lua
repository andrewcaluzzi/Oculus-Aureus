-- Arena co-op preset (GE_MODE=arena-coop with GE_COOP=1 on the Colosseum): the fixed 20-wave arena for two Bonds, the second one
-- played by the arena bot unless a person plays it (netplay). Downed / revive and the wave scaling are in arena_coop.lua.
-- Any ARENA_SETTINGS set before this file (a test preset) win.
ARENA_SETTINGS = ARENA_SETTINGS or {}
ARENA_SETTINGS.coop = true
include("arena")
