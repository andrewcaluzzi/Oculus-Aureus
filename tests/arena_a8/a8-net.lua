-- A8 netplay: the co-op arena with the crowd favour on over the lockstep link (two people, no bot); host and guest must agree tick for
-- tick. Every enemy is killed 2.5 s after it spawns by a real shot credited to player 0 or player 1 in turn (headshots, fists, now and
-- then a blast), so both Bonds earn favour, the tiers move, gifts are thrown and sponsors called; bosses on waves 2 and 4 (the thumb).
-- Both Bonds stand still (neutral recorded input): camping losses and hits taken run too. Run (alone):
--   STAGE=colosseum MODE=a8-net LUATEST= MOVES= sh tests/net_modes.sh net <tag> <port> <ticks>
ARENA_SETTINGS = { waves = 4, boss_every = 2, start_delay = 2, breather = 2, coop_bleed = 12, ammo_every = 15,
  crowd = { count = 10, roar_every = 6, sponsor_cooldown = 12 } }
include("arena-coop")
local t, born, k = 0, {}, 0
local PARTS = { "head", "body", "head", "gun" }
local GUNS = { "pp7", "ak47", "fist", "pp7sil" }
on("chr_spawned", function(c) if t >= 1.5 then born[#born + 1] = { c = c, t = t } end end)
on("tick", function(dt)
  t = t + dt
  local keep = {}
  for _, b in ipairs(born) do
    if spawn.alive(b.c) and t - b.t > 2.5 then
      k = k + 1
      local p = k % 2
      if k % 9 == 0 then spawn.explode(b.c, "small", p)
      else
        local part, gun = PARTS[k % #PARTS + 1], GUNS[k % #GUNS + 1]
        for _ = 1, 40 do if spawn.alive(b.c) then test.shoot(b.c, part, gun, p) end end
        if spawn.alive(b.c) then spawn.kill(b.c) end
      end
    elseif spawn.alive(b.c) then
      keep[#keep + 1] = b
    end
  end
  born = keep
end)
