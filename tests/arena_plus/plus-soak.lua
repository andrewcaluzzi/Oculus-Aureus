-- Arena plus soak (run.sh with TEST=plus-soak): the 20-wave chaos arena (mutators, affix elites, bosses every 5) with everything of arena
-- plus on and the drop chance raised to 12%. Every enemy is killed by Bond 0 through real shots 3 s after it spawns; Bond walks onto
-- each power-up a second after it lands (so every kind is collected, over mutators and sponsor gifts that share enemy_health) and
-- rolls the crate when he can afford it. The cards are taken at once (an automated run). Invulnerable.
-- Expected: "WON wave 20", every MUTATOR "reset ok", enemy_health back to 100 at the end, no Lua / A7 error, the PLUSSOAK summary.
ARENA_SETTINGS = { mode = "fixed", waves = 20, boss_every = 5, breather = 2, start_delay = 1, ammo_every = 20,
  mutators = true, mutator_from = 2, mutator_chance = 0.7, mutator_chance_max = 0.7,
  elites = true, affix_from = 1, affix_chance = 0.35, affix_chance_max = 0.35,
  crowd = { count = 100, roar_every = 10, sponsor_cooldown = 30 },
  plus = { pu_chance = 0.12 } }
include("arena")

local PL = ARENA_PLUS and ARENA_PLUS.live
local t, born, seen_kind, since = 0, {}, {}, nil
local PARTS = { "head", "body", "body", "limb", "gun" }
local GUNS = { "pp7sil", "ak47", "pp7", "fist", "knife", "ar33", "dd44" }
local drops_seen, rolls = 0, 0
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
            if spawn.alive(c) then spawn.kill(c) end
          end
        end
      else
        born[c] = nil
      end
    end
  end
  if PL then
    local f = PL.field()
    if f then
      if since == nil or since.h ~= f.h then since = { h = f.h, t = t }; drops_seen = drops_seen + 1; seen_kind[f.kind] = (seen_kind[f.kind] or 0) + 1 end
      if t - since.t > 1 and not PL.held() then bond.warp(0, f.pad, f.dx, f.dz) end
    end
    if PL.credits() >= PL.price() + 50 and PL.crate() and math.random(8) == 1 and not PL.held() then
      PL.roll(0); rolls = rolls + 1
    end
  end
end)
on("bond_died", function() print("PLUSSOAK bond died") end)
local last = 0
local function PLUSSOAK_SUMMARY()
  local s = PL.summary()
  local kinds = {}
  for k, v in pairs(seen_kind) do kinds[#kinds + 1] = k .. "=" .. v end
  table.sort(kinds)
  print(string.format("PLUSSOAK summary: earned %d spent %d, drops %d (%s), collected %d, nukes %d (%d killed), crate rolls %d (duds %d, golden %d), cards %d, enemy_health %s",
    s.earned, s.spent, s.drops, table.concat(kinds, " "), s.collected, s.nukes, s.nuked, s.rolls, s.duds, s.goldens, s.ncards, tostring(mods.get("enemy_health"))))
end

on("tick", function(dt) if PL and t - last >= 20 then last = t; PLUSSOAK_SUMMARY() end end)   -- (the last one before the exit is the run's)
