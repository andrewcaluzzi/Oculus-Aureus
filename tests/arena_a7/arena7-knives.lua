-- A7: KNIVES ONLY. Wave 1 (soldiers with AK47s) under the knives mutator for 40 s. Run with NO cheats (invincibility stops the damage
-- events): the test heals Bond to full every tick instead. Expected: many "enemy_knives chr ... threw" lines and every "A7TEST bond hit"
-- a knife (the knife's fixed amount), none of the small AK47 bullet hits the control run (arena7-knivesctl.lua) shows.
ARENA_SETTINGS = { mode = "endless", boss_every = 0, breather = 2, start_delay = 1, crowd = 0, first_wave = 1, ammo_every = 0,
  armour_chance = 0, mutators = (A7_KNIFE_CONTROL ~= true), mutator_from = 1, mutator_chance = 1, mutator_chance_max = 1,
  mutator_list = { "knives" } }
include("arena")
local hits = 0
on("damage", function(victim, attacker, amount, weapon)
  if victim == CHR_BOND then
    hits = hits + 1
    print(string.format("ge: arena: A7TEST bond hit %d by %s amount %.3f weapon %s", hits, tostring(attacker), amount, tostring(weapon)))
  end
end)
on("tick", function() if bond.dead(0) == false then bond.heal(0, 1, 1) end end)
