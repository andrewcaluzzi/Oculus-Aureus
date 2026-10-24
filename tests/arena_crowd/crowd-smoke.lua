-- Impostor crowd smoke test: a normal 3-wave arena (crowd 150 + 1000 impostors), every enemy killed 6 s after it spawns.
-- GE_MODE=crowd-smoke GE_STAGE=colosseum GE_CHEATS=invuln; the log ends with "ge: arena: WON wave 3".
ARENA_SETTINGS = { waves = 3, debug_kill = 6 }
include("arena")
