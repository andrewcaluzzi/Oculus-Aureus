-- A6 crash repro (a shared build, wave 10): centre item records were reused after 12 spawns while the old box/armour was still
-- linked in the object list -> domakedefaultobj read NULL+0x14. Churn boxes and armour fast (spawn every 2 s, expire after 3 s) so
-- dozens of records are used in a minute; a fixed build logs 30+ "item spawned" lines and no EXCEPTION.
ARENA_SETTINGS = {
  mode = "endless", boss_every = 5, breather = 3, start_delay = 1, crowd = 0, first_wave = 1,
  ammo_every = 2, ammo_max_center = 2, ammo_ttl = 3, armour_chance = 1.0, armour_ttl = 2,
  debug_queue = function(w) return {} end,
}
include("arena")
