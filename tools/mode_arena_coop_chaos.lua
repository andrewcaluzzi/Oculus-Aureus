-- Arena co-op with the A7 extras (GE_MODE=arena-coop-chaos with GE_COOP=1 on the Colosseum): the fixed 20 waves for two Bonds
-- (the second one is the arena bot unless a person plays it, netplay) with wave mutators and affix elites on.
-- Shipped to Data/Scripts/modes/arena-coop-chaos.lua. Any ARENA_SETTINGS set before this file (a test preset) win.
ARENA_SETTINGS = ARENA_SETTINGS or {}
if ARENA_SETTINGS.mode == nil then ARENA_SETTINGS.mode = "fixed" end
if ARENA_SETTINGS.mutators == nil then ARENA_SETTINGS.mutators = true end
if ARENA_SETTINGS.elites == nil then ARENA_SETTINGS.elites = true end
ARENA_SETTINGS.coop = true
include("arena")
