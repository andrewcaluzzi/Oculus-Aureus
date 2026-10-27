-- A7: every wave mutator rolls once and resets. Endless, no bosses, a mutator EVERY wave from wave 1, the list in order
-- (mutator_pick "cycle"), every enemy killed 2 s after it spawns. Expected: 8 "MUTATOR wave n: ..." lines (knives, paintball, one_hit,
-- turbo, fog, dk, tough, bottomless), 8 "MUTATOR ... off (cleared): reset ok", and the "A7TEST mods" line (logged whenever any of the
-- touched modifiers or the fog changes) back at the defaults after each wave.
ARENA_SETTINGS = { mode = "endless", boss_every = 0, breather = 2, start_delay = 1, crowd = 30, first_wave = 1, debug_kill = 2.0,
  ammo_every = 0, armour_chance = 0,
  mutators = true, mutator_from = 1, mutator_chance = 1, mutator_chance_max = 1, mutator_pick = "cycle" }
include("arena")
local NAMES = { "paintball", "one_hit_kills", "enemy_damage", "turbo", "dk_mode", "enemy_health", "infinite_ammo" }
local last = ""
on("tick", function()
  local parts = {}
  for _, n in ipairs(NAMES) do parts[#parts + 1] = n .. "=" .. tostring(mods.get(n)) end
  local s = table.concat(parts, " ") .. " fog=" .. tostring(arena.fog())
  if s ~= last then
    last = s
    print("ge: arena: A7TEST mods " .. s)
  end
end)
