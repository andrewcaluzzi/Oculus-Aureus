-- A7 partner bot skill (coop_bot_skill): the same seeded scene at a given step; run it at step 1 and at step 5 (coop-skill1.lua sets
-- COOP_SKILL_TEST = 1 and includes this) and compare (tests/arena_a7/run_skill.sh). Unarmed enemies the test spawns one at a time in the
-- open pit, 1800-2600 units from the bot (inside every step's range), each removed if still alive after 12 s; the next comes 0.5 s
-- after one dies or goes. The bot carries a KF7 with its ammo topped up every 2 s (so ammo never decides it). Bond 0 is kept behind
-- the bot, out of its line of fire. Over 40 s: foes, kills, the bot's real rounds fired and hits (bond.shots), and the mean time from
-- a foe's spawn to the bot's first round at it.
-- Run with run_coop.sh, ~3200 frames. Result line: "ge: arena: TEST skill RESULT step N: foes F kills K fired S hits H first M ticks".
local STEP = COOP_SKILL_TEST or 5
ARENA_SETTINGS = { mode = "endless", boss_every = 0, crowd = 0, start_delay = 2, breather = 60, ammo_every = 0, armour_chance = 0,
                   coop_bot_skill = STEP, debug_queue = function(w) return {} end }
include("arena-coop")

local START, LEN, FOE_LIFE = 4, 40, 12
local t, ticks, acc = 0, 0, 0
local foe, foeTick, foeK, nextAt, firstSeen = nil, 0, 0, START, false
local foes, kills, firstSum, firstN = 0, 0, 0, 0
local fired0, hits0, lastFired = nil, nil, 0
local done = false

local function spawnFoe()
  local bx, _, bz = bond.pos(1)
  if not bx then return end
  local list = pads.far_from(bx, bz, 1800, 2600, true)
  if #list == 0 then return end
  foeK = foeK + 1
  local pad = list[1 + (foeK * 7) % #list]
  foe = spawn.chr(pad, { skin = "russian_soldier", weapon = "none", behaviour = "hold", health = 3 })
  if foe then
    foes, foeTick, firstSeen = foes + 1, ticks, false
    print(string.format("ge: arena: TEST skill foe %d chr %d at pad %d (t=%.1f)", foes, foe, pad, t))
  end
end

on("chr_died", function(victim)
  if foe and victim == foe then
    kills = kills + 1
    foe, nextAt = nil, t + 0.5
  end
end)

-- Bond 0 to (x, z): a warp from the nearest pad
local function put0(x, z)
  local best, bd, bx, bz
  for _, id in ipairs(pads.far_from(x, z, 0, 1500)) do
    local px, _, pz = pads.pos(id)
    if px then
      local d = (px - x) ^ 2 + (pz - z) ^ 2
      if not bd or d < bd then best, bd, bx, bz = id, d, px, pz end
    end
  end
  if best then bond.warp(0, best, x - bx, z - bz) end
end

on("ui_tick", function()
  ticks = ticks + 1
  if done or t < START then return end
  local fired = bond.shots(1)
  if foe and not firstSeen and fired and fired > lastFired then   -- the first round since this foe spawned
    firstSeen = true
    firstSum, firstN = firstSum + (ticks - foeTick), firstN + 1
  end
  lastFired = fired or lastFired
  if not foe then return end
  local ex, ez = spawn.where(foe)
  local bx, _, bz = bond.pos(1)
  if not ex or not bx then return end
  local dx, dz = ex - bx, ez - bz
  local d = math.sqrt(dx * dx + dz * dz)
  if d > 300 then put0(bx - dx / d * 250, bz - dz / d * 250) end
end)

on("stage_start", function()
  bond.give(1, "kf7")
  bond.equip(1, "kf7")
end)

on("tick", function(dt)
  t = t + dt
  acc = acc + dt
  if done then return end
  if acc >= 2 then
    acc = 0
    bond.give_ammo(1, 1.0)
  end
  if t < START then return end
  if not fired0 then fired0, hits0 = bond.shots(1); lastFired = fired0 end
  if foe and ticks - foeTick > FOE_LIFE * 60 then
    spawn.remove(foe)
    foe, nextAt = nil, t + 0.5
  end
  if not foe and t >= nextAt and t < START + LEN then spawnFoe() end
  if t >= START + LEN then
    done = true
    local fired, hits = bond.shots(1)
    local mean = firstN > 0 and firstSum / firstN or -1
    print(string.format("ge: arena: TEST skill RESULT step %d: foes %d kills %d fired %d hits %d first %.1f ticks (%d foes shot at)", STEP, foes,
                        kills, fired - fired0, hits - hits0, mean, firstN))
  end
end)
