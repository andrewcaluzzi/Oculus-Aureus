-- A7: the REGENERATING affix. One unarmed, holding enemy that is always a regen elite; 3 s after it spawns the test wounds it to 40% health
-- (test.hurt: blasts always kill a chr outright, so they cannot test this), then logs its health every 0.5 s. Expected "A7TEST regen"
-- lines: 0.400 at the wound, then +0.010 every 0.5 s (affix_regen 2% of full health a second here) up to 1.000, where it stays.
ARENA_SETTINGS = { mode = "endless", boss_every = 0, breather = 2, start_delay = 1, crowd = 0, first_wave = 1, ammo_every = 0,
  armour_chance = 0, elites = true, affix_from = 1, affix_chance = 1, affix_chance_max = 1, affix_two_chance = 0, affix_list = { "regen" },
  affix_regen = 0.02,
  debug_queue = function(w)
    if w > 1 then return {} end
    return { { n = 1, spec = { skin = "russian_soldier", weapon = "none", behaviour = "hold", health = 2, _tier = "normal" } } }
  end }
include("arena")
local chr, born, t, acc, hurt = nil, 0, 0, 0, false
on("chr_spawned", function(c) if not chr then chr, born = c, t end end)
on("tick", function(dt)
  t = t + dt
  if not chr then return end
  if not hurt and t - born >= 3 then
    hurt = true
    print("ge: arena: A7TEST regen wound", test.hurt(chr, 0.6))
  end
  acc = acc + dt
  if acc >= 0.5 and t - born < 40 then
    acc = 0
    local h, a = spawn.health(chr)
    if h then print(string.format("ge: arena: A7TEST regen t=%.1f health %.3f armour %.2f", t - born, h, a)) end
  end
end)
