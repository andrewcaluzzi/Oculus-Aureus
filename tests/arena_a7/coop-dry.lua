-- A7 partner bot dry-fire: one unarmed, tough enemy the test spawns in the open pit; Bond 0 is kept behind the bot (out of its line of fire), so the
-- bot shoots for 30 s, empties magazines (reloads) and runs guns dry (switches). `dry` counts the clicks of its gun on an empty magazine
-- (gunfire.c): at most one per emptied magazine may click before the reload (the fix releases the trigger first: expect 0).
-- Run with run_coop.sh, no GE_COOP_GOD, ~2400 frames. Result line: "ge: arena: TEST dry RESULT PASS|FAIL ...".
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
    print(string.format("ge: arena: TEST dry foe %s at pad %d", tostring(foe), list[1]))
  end
end

local t, acc = 0, 0
local started, done, s0, d0, lastW, switches, lastAmmo, used = false, false, 0, 0, -1, 0, -1, 0
local START, END = 4, 34

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

on("ui_tick", function()
  if not started or done then return end
  local i = coop.bot_info(1)
  if not i or i.target < 0 then return end
  local ex, ez = spawn.where(i.target)
  local bx, _, bz = bond.pos(1)
  if not ex or not bx then return end
  local dx, dz = ex - bx, ez - bz
  local d = math.sqrt(dx * dx + dz * dz)
  if d < 300 then return end
  put0(bx - dx / d * 250, bz - dz / d * 250)
  if lastAmmo >= 0 and i.ammo < lastAmmo then used = used + (lastAmmo - i.ammo) end
  lastAmmo = i.ammo
  if i.weapon ~= lastW then
    if lastW >= 0 then switches = switches + 1 end
    lastW = i.weapon
  end
end)

on("tick", function(dt)
  t = t + dt
  acc = acc + dt
  local i = coop.bot_info(1)
  if not i then return end
  if not foe and t > 2.5 then spawnFoe() end
  if not started and i.target >= 0 and t > START then
    started, s0, d0 = true, i.shots, i.dry
    print(string.format("ge: arena: TEST dry start at t=%.1f weapon %d ammo %d", t, i.weapon, i.ammo))
  elseif started and not done and t > END then
    done = true
    local shots, dry = i.shots - s0, i.dry - d0
    local ok = shots > 20 and used >= 40 and dry <= 1
    print(string.format("ge: arena: TEST dry RESULT %s: shots %d rounds used %d weapon switches %d dry clicks %d", ok and "PASS" or "FAIL",
                        shots, used, switches, dry))
  end
  if acc >= 1 then
    acc = 0
    print(string.format("ge: arena: TEST dry t=%.0f act %s target %d weapon %d ammo %d shots %d dry %d", t, i.act, i.target, i.weapon, i.ammo,
                        i.shots, i.dry))
  end
end)
