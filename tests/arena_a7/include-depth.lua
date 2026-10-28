-- Regression (post-release battery): a preset that includes a shipped preset must load. arena.lua pulls in _arena_flow -> _arena_menu_defs ->
-- _arena_theme (three levels), so a test or user preset that includes "arena-coop" / "arena-chaos" (two levels above arena.lua) hit
-- include()'s old depth limit of 4 and the mode died with "include() nested too deep". Run: tests/arena_a7/run_include_depth.sh
ARENA_SETTINGS = { waves = 2, boss_every = 0, crowd = 5, start_delay = 1, breather = 2 }
include("arena-coop-chaos")   -- arena-coop-chaos includes arena (preset -> chaos preset -> arena -> flow -> menu defs -> theme)
