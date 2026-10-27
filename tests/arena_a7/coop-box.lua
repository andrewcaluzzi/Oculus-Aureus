-- A7 check: a centre ammo box and armour appear in a co-op stage (empty waves, a box every 4 s, armour after every wave).
ARENA_SETTINGS = { mode = "endless", boss_every = 0, crowd = 0, start_delay = 1, breather = 2, ammo_every = 4, armour_chance = 1,
                   coop_bot = false, debug_queue = function(w) return {} end }
include("arena-coop")
