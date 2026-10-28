-- Overnight endless soak (post-release battery): the A8 soak's killer on the ENDLESS chaos arena (mutators + elites, a boss every 5), crowd
-- 100, favour on, invulnerable. Expected: wave 25+ reached (log "wave 25 "), every MUTATOR "reset ok", no Lua error / pool warning,
-- chr slots free stable (spawn lines), a CROWD summary. Run: tests/arena_a8/run.sh o-endless a8-endless-soak 30000
ARENA_SETTINGS = { mode = "endless", boss_every = 5, breather = 2, start_delay = 1, ammo_every = 20,
  mutators = true, mutator_from = 2, mutator_chance = 0.7, mutator_chance_max = 0.7,
  elites = true, affix_from = 1, affix_chance = 0.35, affix_chance_max = 0.35,
  crowd = { count = 100, roar_every = 10, sponsor_cooldown = 30 }, whacky = 2 }
include("arena")

local t, born = 0, {}
local PARTS = { "head", "body", "body", "limb", "gun" }
local GUNS = { "pp7sil", "ak47", "pp7", "fist", "knife", "ar33", "dd44" }
on("chr_spawned", function(c) if t >= 0.9 then born[c] = t end end)
on("tick", function(dt)
  t = t + dt
  local n = 0
  for c, b in pairs(born) do
    if t - b > 3 then
      if spawn.alive(c) then
        n = n + 1
        if n <= 4 then
          if math.random(12) == 1 then spawn.explode(c, "small", 0)
          else
            local part, gun = PARTS[math.random(#PARTS)], GUNS[math.random(#GUNS)]
            for _ = 1, 40 do if spawn.alive(c) then test.shoot(c, part, gun, 0) end end
            if spawn.alive(c) then spawn.kill(c) end   -- armour soaks a few shots: finish it the old way (no credit)
          end
        end
      else
        born[c] = nil
      end
    end
  end
end)
