-- A7 merged: co-op + mutators + elites + polish together. Two Bonds (the partner bot on), 10 fixed waves, a boss on 5 and 10,
-- a mutator EVERY wave in list order (so waves 1-8 get knives, paintball, one_hit, turbo, fog, dk, tough, bottomless; bosses
-- included), half the enemies affix elites (explosive among them), every enemy killed 4 s after it spawns (corpses blow up near
-- the next ones), crowd 20, centre ammo every 15 s. Bond 1 (player 0, GE_COOP_GOD=0: cannot die) stands still and is put down
-- every 20 s, so the bot has to revive him under KNIVES ONLY / ONE-HIT KILLS / explosive corpses. Expected: "WON wave 10", every
-- mutator "reset ok", downs = revives (no bleed-out), no A7 / Lua error, no pool / model-slot warning, no crash.
ARENA_SETTINGS = { waves = 10, boss_every = 5, start_delay = 2, breather = 2, crowd = 20, debug_kill = 4.0, ammo_every = 15,
  mutators = true, mutator_from = 1, mutator_chance = 1, mutator_chance_max = 1, mutator_pick = "cycle", mutator_bosses = true,
  elites = true, affix_from = 1, affix_chance = 0.5, affix_chance_max = 0.5, coop_debug = false }
include("arena-coop-chaos")
local t, next_down = 0, 20
on("tick", function(dt)
  t = t + dt
  if t >= next_down then
    next_down = next_down + 20
    if coop.state(0) == "up" then print(string.format("ge: arena: TEST put player 0 down at %.1fs: %s", t, tostring(test.coop_down(0)))) end
  end
end)
