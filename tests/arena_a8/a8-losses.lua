-- A8 crowd favour: the losses (and REVENGE). Not invulnerable: Bond is healed every tick instead. Run with GE_AUTOFIRE=600
-- GE_AUTOLOOK=1 (hold Z in bursts from poll 600 while looking at the sky: shots that hit nothing). Wave 1: a gunman with an AK47 and
-- accuracy 100 close to Bond, an unarmed dummy far away (so enemies stay alive), a spectator to shoot. Expected CROWD lines:
--   HIT -1 (the gunman's bullets, at most one per 0.4 s) / REVENGE (the gunman killed within 3 s of his last hit) / SPRAY AND PRAY
--   (8 shots with no hit, spray_shots 8 here) / CAMPING (Bond still for 10 s, then every 5 s) / SHOT THE CROWD -8 + "panic: ..."
--   and no CLEAN WAVE at the clear.
ARENA_SETTINGS = { mode = "endless", boss_every = 0, breather = 3, start_delay = 1, first_wave = 1, ammo_every = 0, armour_chance = 0,
  crowd = { count = 30, drift = 0, roar_every = 9999, sponsor_cooldown = 9999, hated_penalty = "off", points = { spray_shots = 8 } },
  debug_queue = function(w)
    if w ~= 1 then return {} end
    return { { n = 1, gate = 1, spec = { skin = "russian_soldier", weapon = "ak47", behaviour = "hold", health = 0.05, accuracy = 100, _tier = "normal" } },
             { n = 2, gate = 3, spec = { skin = "russian_soldier", weapon = "none", behaviour = "hold", health = 5, _tier = "normal" } } }
  end }
include("arena")

local en, t, phase, hits_seen, revenge_at = {}, 0, 0, 0, nil
on("chr_spawned", function(c) if spawn.alive(c) and t >= 0.9 then en[#en + 1] = c end end)
on("damage", function(victim, attacker, amount)
  if victim <= -2 and en[1] and attacker == en[1] then
    hits_seen = hits_seen + 1
    if hits_seen == 3 and not revenge_at then revenge_at = t + 0.5 end   -- kill him half a second after his third hit
  end
end)

on("tick", function(dt)
  t = t + dt
  bond.heal(0, 1, 0)
  if #en < 2 then return end
  if phase == 0 then   -- stand Bond next to the gunman's gate (he shoots); the spraying / camping run meanwhile
    phase = 1
    local best, bd = nil, 1e18   -- the pit pad nearest (3700, 900): about 1200 units in front of gate 1's mouth, clear of the pillar
    for i = 0, pads.count() - 1 do
      local x, y, z = pads.pos(i)
      if x and y <= 1 then local d = (x - 3700) ^ 2 + (z - 900) ^ 2; if d < bd then best, bd = i, d end end
    end
    bond.warp(0, best, 20, 20)
    print("ge: arena: A8TEST losses: Bond warped, waiting for the gunman's hits")
  elseif phase == 1 and revenge_at and t >= revenge_at then
    phase = 2
    for _ = 1, 30 do if spawn.alive(en[1]) then test.shoot(en[1], "body", "pp7sil", 0) end end
    print("ge: arena: A8TEST losses: gunman shot, alive " .. tostring(spawn.alive(en[1])) .. ", hits seen " .. hits_seen)
  elseif phase == 2 and t > 30 then
    phase = 3
    -- a spectator: the first live one the arena seated (heist's crowd sits in the front rows and arrives over time, so the lab
    -- line's "chr 8000-8040 beyond 6000 units" pick found none there)
    local shot = false
    for _, s in ipairs(ARENA_HOOKS.spectators()) do
      if not shot and spawn.alive(s.chr) then shot = test.shoot(s.chr, "body", "pp7sil", 0); print("ge: arena: A8TEST shot spectator " .. s.chr, shot) end
    end
    if not shot then print("ge: arena: A8TEST shot spectator: none found") end
  elseif phase == 3 and t > 33 then
    phase = 4
    for _ = 1, 400 do if spawn.alive(en[2]) then test.shoot(en[2], "body", "pp7sil", 0) end end
    print("ge: arena: A8TEST losses: dummy shot, alive " .. tostring(spawn.alive(en[2])))
  end
end)
