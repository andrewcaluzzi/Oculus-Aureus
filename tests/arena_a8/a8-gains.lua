-- A8 crowd favour: every gain in a scripted scenario. Wave 1: 13 "hold" enemies with PP7s and almost no health; the test kills them
-- one step at a time (every 2.5 s, so no accidental multi-kills) through real shots (test.shoot = handles_shot_actors, with the hit part)
-- or a real explosion credited to Bond 0, warping Bond next to the target unless the step wants range. Wave 2: a boss and an affix elite.
-- Run invulnerable (no hits taken). Expected "ge: arena: CROWD kill ..." labels, in order:
--   HEADSHOT (close) / HEADSHOT + LONG SHOT (far, headshot +3 range) / BARE HANDS / KNIFE KILL / THEIR OWN GUN (the PP7 the victims
--   carry; Bond's kit is the silenced PP7, and since the playtest fix it must be the victim's gun, see a8-owngun.lua) / CLOSE CALL /
--   TWIN STRIKE, TRIPLE CROWN (the chain names, with their centre callouts) / BOOM x2 / DISARMED (+ "DISARM chr") / REVENGE is in a8-losses / CLEAN WAVE at the wave clear /
--   GIANT SLAYER + WITH A PISTOL (boss) and ELITE SLAIN + WITH A PISTOL (wave 2); STREAK n from the 3rd kill on.
ARENA_SETTINGS = { mode = "endless", boss_every = 0, breather = 3, start_delay = 1, first_wave = 1, ammo_every = 0, armour_chance = 0,
  max_alive = 20, max_alive_cap = 20,
  crowd = { count = 30, drift = 0, roar_every = 9999, sponsor_cooldown = 9999, thumb = false, points = { camp_secs = 9999 } },
  elites = true, affix_from = 2, affix_chance = 1, affix_chance_max = 1, affix_two_chance = 0, affix_list = { "marksman" },
  debug_queue = function(w)
    local function e(gate, mouth, n) return { n = n, gate = gate, mouth = mouth,
      spec = { skin = "russian_soldier", weapon = "pp7", behaviour = "hold", health = 0.05, accuracy = 0, _tier = "normal" } } end
    if w == 1 then
      local q = {}
      q[#q + 1] = e(1, true, 1); q[#q + 1] = e(1, true, 2)                 -- the blast pair: gate 1's two mouth pads, 500 apart
      for i = 1, 11 do q[#q + 1] = e(2 + (i % 3), false, i) end            -- the rest at gates 2-4
      return q
    elseif w == 2 then
      return { { n = 1, gate = 2, boss = true, spec = { skin = "baron_samedi", weapon = "pp7", behaviour = "hold", health = 0.05, _tier = "boss" } },
               { n = 2, gate = 3, spec = { skin = "russian_soldier", weapon = "pp7", behaviour = "hold", health = 0.05, _tier = "normal" } } }
    end
    return {}
  end }
include("arena")

local mine, wave2, t, step_t, si = {}, {}, 0, 0, 0
on("chr_spawned", function(c) if spawn.alive(c) and t >= 0.9 then   -- not the crowd (all seated at t = 0.25)
  if #mine < 13 then mine[#mine + 1] = c else wave2[#wave2 + 1] = c end end end)

local function near(c, dx)   -- warp Bond 0 beside chr c (the pad nearest it, then an offset)
  local x, z = spawn.where(c)
  local best, bd = nil, 1e18
  for i = 0, pads.count() - 1 do
    local px, py, pz = pads.pos(i)
    if px and py <= 1 then
      local d = (px - x) ^ 2 + (pz - z) ^ 2
      if d > 300 * 300 and d < bd then best, bd = i, d end
    end
  end
  bond.warp(0, best, dx or 20, 20)
end
local function kill(c, part, weapon, p)
  for _ = 1, 60 do
    if not spawn.alive(c) then break end
    test.shoot(c, part, weapon, p or 0)
  end
  print(string.format("ge: arena: A8TEST shot chr %d (%s, %s): alive %s", c, part, weapon, tostring(spawn.alive(c))))
end

local steps = {
  function() near(mine[3]); kill(mine[3], "head", "pp7") end,                       -- HEADSHOT (close)
  function() bond.warp(0, 108, 20, 20); kill(mine[4], "head", "pp7") end,           -- far: HEADSHOT (+range) + LONG SHOT
  function() near(mine[5]); kill(mine[5], "body", "fist") end,                      -- BARE HANDS
  function() near(mine[6]); kill(mine[6], "body", "knife") end,                     -- KNIFE KILL
  function() near(mine[7]); kill(mine[7], "body", "pp7") end,                       -- THEIR OWN GUN (it carries a PP7)
  function() near(mine[8]); bond.heal(0, -0.9, 0); kill(mine[8], "body", "pp7") end,   -- CLOSE CALL (chr_died comes a tick later)
  function() bond.heal(0, 1, 0); near(mine[9]); kill(mine[9], "body", "pp7"); kill(mine[10], "body", "pp7") end,   -- TWIN STRIKE
  function() kill(mine[11], "body", "pp7") end,                                     -- (0.25 s later) TRIPLE CROWN
  function() bond.warp(0, 108, 20, 20); print("ge: arena: A8TEST blast", spawn.explode(mine[1], 18, 0)) end,   -- BOOM x2
  function() near(mine[12]); test.shoot(mine[12], "gun", "pp7", 0) end,            -- DISARMED (+ the gun drops)
  function() print("ge: arena: A8TEST disarmed chr alive", spawn.alive(mine[12])); kill(mine[12], "body", "pp7"); kill(mine[13], "body", "pp7") end,
  function() if wave2[1] then near(wave2[1]); kill(wave2[1], "body", "pp7") end end, -- GIANT SLAYER + WITH A PISTOL
  function() if wave2[2] then near(wave2[2]); kill(wave2[2], "body", "pp7") end end, -- ELITE SLAIN + WITH A PISTOL
}
local gaps = { [8] = 0.25 }   -- the TRIPLE CROWN step comes right after the TWIN STRIKE step

on("tick", function(dt)
  t = t + dt
  if #mine < 13 or t < 3 then return end
  step_t = step_t + dt
  local gap = gaps[si + 1] or 2.5
  if si < #steps and step_t >= gap then
    if si + 1 >= 12 and #wave2 < 2 then return end   -- wait for wave 2
    step_t = 0
    si = si + 1
    print("ge: arena: A8TEST step " .. si)
    steps[si]()
  end
end)
