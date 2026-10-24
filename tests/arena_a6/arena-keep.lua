-- Keep-every-body check (default corpse_keep = -1, corpse_limit = 400): invulnerable Bond, every enemy killed 2 s after it spawns.
-- KEEP_MODE "fixed" (default): the 20 waves of a normal run, all bodies must still be there at the end (~170+).
-- KEEP_MODE "endless": goes past 400 kills; corpses must stop at the cap with no "could not place", REFUSED or pool warnings.
-- It logs "ge: keep: ..." every 10 s (game time): corpses kept, live enemies, chr slots free, kills.
ARENA_SETTINGS = { mode = (KEEP_MODE or "fixed"), boss_every = 5, breather = 1, start_delay = 1, max_alive = 18, max_alive_cap = 24,
  debug_kill = 2.0, first_wave = KEEP_FIRST_WAVE or 1, corpse_keep = KEEP_CORPSE_KEEP }   -- KEEP_CORPSE_KEEP nil = the default (-1)
include("arena")
local t, nextlog = 0, 10
on("tick", function(dt)
  t = t + dt
  if t >= nextlog then
    nextlog = nextlog + 10
    local c, k, a = ARENA_HOOKS.counts()
    local loose, held, fw, fp = spawn.pools()
    print(string.format("ge: keep: t=%.0f spectators %d corpses %d live %d | chr slots free %d, weapon slots free %s, props free %s", t, c, k, a, spawn.free(), tostring(fw), tostring(fp)))
  end
end)
