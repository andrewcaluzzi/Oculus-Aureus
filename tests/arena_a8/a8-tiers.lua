-- A8 crowd favour: the tiers and what they do. Invulnerable. A step machine (each step waits for its condition):
--   ROARING (favour set to 65, roar_every 3): a gift thrown near Bond -> Bond walks over it -> "GIFT collected"
--   ADORED (85, sponsor list {armour}): a sponsor gift in the centre -> walked over -> "SPONSOR gift armour collected", armour 1.00
--   double damage (sponsor list {double}): enemy_health 50 while it lasts (double_secs 4 here), back to 100 after
--   HATED (17) in a fight: "an extra pack of 4 now", then a headshot climbs out: "REDEMPTION"
--   wave 2 (a boss) at favour 70 with Bond hurt to 0.30: "THUMB UP", health 1.00, an emperor's gift
--   wave 3 (a boss) at favour 30: "THUMB DOWN": the breather is 1 s (log the gap)
--   wave 4 cleared, then HATED (10) in the breather: "the gates open early", wave 5 has 1 + 4 enemies
ARENA_SETTINGS = { mode = "endless", boss_every = 0, breather = 8, start_delay = 1, first_wave = 1, ammo_every = 0, armour_chance = 0,
  crowd = { count = 30, drift = 0, roar_every = 3, sponsor_cooldown = 1, double_secs = 4, sponsor_list = { "armour" },
            points = { camp_secs = 9999 } },
  debug_queue = function(w)
    local function e(n, boss) return { n = n, gate = 1 + n % 4, boss = boss,
      spec = { skin = boss and "jaws" or "russian_soldier", weapon = "none", behaviour = "hold", health = 0.05, _tier = boss and "boss" or "normal" } } end
    if w == 1 then return { e(1), e(2), e(3) } end
    if w == 2 or w == 3 then return { e(1, true) } end
    if w == 4 or w == 5 then return { e(1) } end
    return {}
  end }
include("arena")

local CR = ARENA_CROWD.live
local t, si, st, target = 0, 1, 0, nil
local spawned = {}
on("chr_spawned", function(c) if t >= 0.9 then spawned[#spawned + 1] = c end end)
local function log(s) print("ge: arena: A8TEST " .. s) end
local function alive_enemies()
  local l = {}
  for _, c in ipairs(spawned) do if spawn.alive(c) then l[#l + 1] = c end end
  return l
end
local function kill(c, part)
  for _ = 1, 60 do if spawn.alive(c) then test.shoot(c, part or "body", "pp7sil", 0) end end
end
local function walk_to(h)   -- true when the item is gone (collected)
  if not spawn.item_alive(h) then test.walk_stop(); return true end
  local x, z = spawn.item_pos(h)
  if x then test.walk(0, x + 15, z + 15) end
  return false
end
local function item_of(kind)
  for _, it in ipairs(CR.live_items()) do if it.kind == kind then return it end end
end

local steps = {
  function() return #alive_enemies() >= 3 end,
  function() CR.set(65); log("set 65 tier " .. select(2, CR.tier())); return true end,
  function() local it = item_of("gift"); if it then target = it.h; log("gift seen " .. it.h); return true end end,
  function() if walk_to(target) then local h, a = bond.vitals(0); log(string.format("gift collected, armour %.2f", a)); return true end end,
  function() CR.set(85); log("set 85 tier " .. select(2, CR.tier())); return true end,
  function() local it = item_of("sponsor"); if it then target = it.h; log("sponsor seen " .. it.what); return true end end,
  function() if walk_to(target) then local h, a = bond.vitals(0); log(string.format("sponsor collected, armour %.2f", a)); return true end end,
  function() CR.C.sponsor_list = { "double" }; CR.set(60); bond.warp(0, 108, 20, 20)
    for _, it in ipairs(CR.live_items()) do if it.kind == "sponsor" then spawn.item_remove(it.h) end end; return true end,
  function() if st > 1.5 then CR.set(90); CR.sponsor(true); log("double: enemy_health " .. tostring(mods.get("enemy_health")) .. string.format(", %.1f s left", CR.double_left()))
    CR.set(60); return true end end,   -- back out of ADORED so no new sponsor comes while the test waits
  function() if CR.double_left() <= 0 and st > 1 then log("double over: enemy_health " .. tostring(mods.get("enemy_health"))); return true end end,
  function() CR.set(17); log("set 17 tier " .. select(2, CR.tier())); return true end,
  function() if st > 1 then local l = alive_enemies(); log("alive after the HATED pack: " .. #l); kill(l[1], "head"); return true end end,
  function() if st > 1 then log("tier now " .. select(2, CR.tier())); return true end end,
  function() for _, c in ipairs(alive_enemies()) do kill(c) end; return st > 3 and #alive_enemies() == 0 end,   -- the pack trickles in
  -- wave 2: the boss, thumbs up
  function() if #alive_enemies() >= 1 and st > 4 then CR.set(70); bond.heal(0, -0.7, 0); log(string.format("boss wave: health %.2f", bond.vitals(0))); return true end end,
  function() kill(alive_enemies()[1]); return true end,
  function() if st > 1 then log(string.format("after thumb: health %.2f", bond.vitals(0))); return true end end,
  -- wave 3: the boss, thumbs down
  function() if #alive_enemies() >= 1 and st > 3 then CR.set(30); kill(alive_enemies()[1]); return true end end,
  -- wave 4: one enemy, then HATED in the breather
  function() if #alive_enemies() >= 1 and st > 1 then CR.set(50); kill(alive_enemies()[1]); return true end end,
  function() if st > 2 then CR.set(10); log("set 10 in the breather, tier " .. select(2, CR.tier())); return true end end,
  function() if st > 6 then local l = alive_enemies(); log("wave 5 alive: " .. #l); for _, c in ipairs(l) do kill(c) end; return true end end,
}

on("tick", function(dt)
  t = t + dt
  st = st + dt
  if si <= #steps and steps[si]() then
    log("step " .. si .. " done at " .. string.format("%.2f", t))
    si = si + 1
    st = 0
  end
end)
