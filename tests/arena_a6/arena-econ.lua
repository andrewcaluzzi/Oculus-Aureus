-- A6: the centre schedule with empty waves (a wave every ~3 s): ammo drops every 10 s, at most 2 uncollected (the oldest is replaced),
-- 45 s lifetime; armour after every boss wave (every 5th) and 50% of the others. Nothing is collected here (no one walks), so the
-- ge: arena: centre ... spawned / removed (replaced|ttl) lines show the schedule, the replacement and the expiry.
ARENA_SETTINGS = {
  mode = "endless", boss_every = 5, breather = 3, start_delay = 1, crowd = 0, first_wave = 1,
  ammo_every = 10, ammo_max_center = 2, ammo_ttl = 45, armour_chance = 0.5, armour_ttl = 20,
  debug_queue = function(w) return {} end,
}
include("arena")
