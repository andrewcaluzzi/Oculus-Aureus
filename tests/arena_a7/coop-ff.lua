-- A7 partner bot friendly fire: one unarmed enemy the test spawns in the open pit (it cannot hurt anybody), so Bond 0 can only be hurt
-- by the bot. Before phase 1 the bot fights from beside Bond 0 as it likes (the first build of this test caught a pistol shot passing
-- 63 units from his centre hitting him there).
-- Phase 1 (12 s): every game tick Bond 0 is put on the bot's line of fire, 350 units (at most a third of the way) from the bot toward
-- the enemy (the bot strafes; he follows). The bot must hold fire: no shots, `blocked` counts the ticks it wanted to fire but did not.
-- Phase 2 (8 s): Bond 0 stands behind the bot: the bot fires (control: the scene does let it shoot). Bond 0 must be unhurt throughout.
-- Run with run_coop.sh, no GE_COOP_GOD, ~1700 frames. Result line: "ge: arena: TEST ff RESULT PASS|FAIL ...".
ARENA_SETTINGS = { mode = "endless", boss_every = 0, crowd = 0, start_delay = 2, breather = 60, ammo_every = 0, armour_chance = 0,
                   coop_debug = true, debug_queue = function(w) return {} end }
include("arena-coop")

-- the enemy: spawned by the test in the open pit 1200-1800 from the bot (a gate enemy is out of its sight most of the time)
local foe
local function spawnFoe()
  local bx, _, bz = bond.pos(1)
  if not bx then return end
  local list = pads.far_from(bx, bz, 1200, 1800, true)
  if #list > 0 then
    foe = spawn.chr(list[1], { skin = "russian_soldier", weapon = "none", behaviour = "hold", health = 400 })
    print(string.format("ge: arena: TEST ff foe %s at pad %d", tostring(foe), list[1]))
  end
end

local t, acc = 0, 0
local phase, s1, b1, s2, hurt = 0, 0, 0, 0, false
local P1_START, P2_START, END = 4, 16, 24

-- Bond 0 to (x, z): a warp from the nearest pad (a far offset can land on another tile and fall back to the pad centre)
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

-- every game tick: keep Bond 0 where the phase wants him
on("ui_tick", function()
  if phase ~= 1 and phase ~= 2 then return end
  local i = coop.bot_info(1)
  if not i or i.target < 0 then return end
  local ex, ez = spawn.where(i.target)
  local bx, _, bz = bond.pos(1)
  if not ex or not bx then return end
  local dx, dz = ex - bx, ez - bz
  local d = math.sqrt(dx * dx + dz * dz)
  if d < 300 then return end
  if phase == 1 then
    local k = math.min(d / 3, 350) / d   -- near the bot: in its line of fire, and close enough that it does not walk after him
    put0(bx + dx * k, bz + dz * k)
  else
    put0(bx - dx / d * 250, bz - dz / d * 250)
  end
end)

on("tick", function(dt)
  t = t + dt
  acc = acc + dt
  local i = coop.bot_info(1)
  local h = bond.vitals(0)
  if not i then return end
  if not foe and t > 2.5 then spawnFoe() end
  if h and h < 0.999 then hurt = true end   -- (any phase: only the bot can hurt him)
  if phase == 0 and i.target >= 0 and t > P1_START then
    phase, s1, b1 = 1, i.shots, i.blocked
    print(string.format("ge: arena: TEST ff phase 1 at t=%.1f target %d", t, i.target))
  elseif phase == 1 and t > P2_START then
    s1, b1 = i.shots - s1, i.blocked - b1
    phase, s2 = 2, i.shots
    print(string.format("ge: arena: TEST ff phase 2 at t=%.1f (phase 1: shots %d blocked %d hurt %s)", t, s1, b1, tostring(hurt)))
  elseif phase == 2 and t > END then
    s2 = i.shots - s2
    phase = 3
    local ok = (s1 == 0) and (b1 > 0) and (not hurt) and (s2 > 0)
    print(string.format("ge: arena: TEST ff RESULT %s: blocked phase shots %d blocked %d hurt %s; clear phase shots %d", ok and "PASS" or "FAIL",
                        s1, b1, tostring(hurt), s2))
  end
  if acc >= 1 then
    acc = 0
    print(string.format("ge: arena: TEST ff t=%.0f phase %d act %s target %d shots %d blocked %d hp0 %.3f weapon %d", t, phase, i.act, i.target,
                        i.shots, i.blocked, h or -1, i.weapon))
  end
end)
