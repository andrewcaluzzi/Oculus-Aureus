-- A8 crowd favour: THEIR OWN GUN compares the killing gun with the gun the victim carried, by weapon, not by ammo type (AC's playtest:
-- an RC-P90 kill on a D5K soldier, both 9 mm, counted). Wave 1: four "hold" soldiers, killed one by one through real shots
-- (test.shoot = handles_shot_actors) with Bond invulnerable and his start kit the silenced PP7:
--   1  D5K soldier, killed with the RC-P90            -> not their own gun (same ammo, other gun)
--   2  D5K soldier, killed with the D5K               -> THEIR OWN GUN
--   3  RC-P90 soldier, disarmed first (its gun drops), then killed with the RC-P90 -> THEIR OWN GUN (a dropped gun stays its own)
--   4  D5K soldier, killed with the silenced PP7      -> not their own gun (Bond's start kit)
-- Ends with "A8TEST OWNGUN PASS" or "A8TEST OWNGUN FAIL ...". Run: tests/arena_a8/run.sh a8-owngun a8-owngun 2400
local seen = {}   -- chr -> true when its "CROWD kill" line had THEIR OWN GUN
local real_print = print
print = function(...)   -- the crowd module logs through print: watch its kill lines
  local s = table.concat((function(...) local t = {} for i = 1, select("#", ...) do t[i] = tostring(select(i, ...)) end return t end)(...), "\t")
  local c = s:match("CROWD kill chr (%d+) by player")
  if c then seen[tonumber(c)] = s:find("THEIR OWN GUN", 1, true) ~= nil end
  real_print(...)
end

local function soldier(gun) return { skin = "russian_soldier", weapon = gun, behaviour = "hold", health = 0.05, accuracy = 0, _tier = "normal" } end
ARENA_SETTINGS = { mode = "endless", boss_every = 0, breather = 30, start_delay = 1, first_wave = 1, ammo_every = 0, armour_chance = 0,
  crowd = { count = 10, drift = 0, roar_every = 9999, sponsor_cooldown = 9999, thumb = false, points = { camp_secs = 9999 } },
  debug_queue = function(w)
    if w ~= 1 then return {} end
    return { { n = 1, gate = 2, spec = soldier("d5k") }, { n = 2, gate = 3, spec = soldier("d5k") },
             { n = 3, gate = 4, spec = soldier("rcp90") }, { n = 4, gate = 2, spec = soldier("d5k") } }
  end }
include("arena")

local mine, t, step_t, si = {}, 0, 0, 0
on("chr_spawned", function(c) if spawn.alive(c) and t >= 0.9 and #mine < 4 then mine[#mine + 1] = c end end)

local function kill(c, weapon)
  local g1, g2 = spawn.owned_guns(c)
  for _ = 1, 60 do
    if not spawn.alive(c) then break end
    test.shoot(c, "body", weapon, 0)
  end
  real_print(string.format("ge: arena: A8TEST owngun chr %d owned %s/%s killed with %s: alive %s", c, tostring(g1), tostring(g2), weapon,
    tostring(spawn.alive(c))))
end
local expect = { false, true, true, false }
local steps = {
  function() kill(mine[1], "rcp90") end,
  function() kill(mine[2], "d5k") end,
  function() real_print("ge: arena: A8TEST owngun disarm", test.shoot(mine[3], "gun", "pp7sil", 0)) end,
  function() local g = spawn.owned_guns(mine[3]); real_print("ge: arena: A8TEST owngun after the disarm chr " .. mine[3] .. " owns " .. tostring(g))
             kill(mine[3], "rcp90") end,
  function() kill(mine[4], "pp7sil") end,
  function()
    local bad = {}
    for i, c in ipairs(mine) do
      if (seen[c] or false) ~= expect[i] then bad[#bad + 1] = string.format("#%d chr %d own=%s want %s", i, c, tostring(seen[c]), tostring(expect[i])) end
    end
    real_print(#bad == 0 and "ge: arena: A8TEST OWNGUN PASS (4 kills)" or ("ge: arena: A8TEST OWNGUN FAIL " .. table.concat(bad, "; ")))
  end,
}
on("tick", function(dt)
  t = t + dt
  if #mine < 4 or t < 3 then return end
  step_t = step_t + dt
  if si < #steps and step_t >= 2.5 then   -- 2.5 s apart: no multi-kills
    step_t, si = 0, si + 1
    steps[si]()
  end
end)
