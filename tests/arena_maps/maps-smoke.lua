-- Smoke of the arena on one stock multiplayer map (tests/arena_maps/run.sh <map>; the runner puts `MAPSMOKE_MAP = "<id>"` in front of this file).
-- A short fixed run (6 waves, a boss every 3rd), Bond invulnerable (GE_CHEATS=invuln), fast-forward. Bond "kills" an enemy with real shots
-- (test.shoot) once it has been within 18 m of him for 2 s (or has stood firing at him for 6 s: the AI stops at its range). An enemy that
-- makes no 250-unit headway for 30 s and is not firing is STUCK (the number to watch); one still on its way after 90 s is LATE (a long
-- way round); both are shot so the run goes on. Everything is printed as "MAPSMOKE ..." lines for
-- the runner (and read from the arena's own "ge: arena: ..." lines): spawns, per-enemy outcome, the end.
local CHAOS = mode.env_flag("GE_MAPSMOKE_CHAOS")   -- mutators, affix elites and Whacky on, every wave
ARENA_SETTINGS = { map = MAPSMOKE_MAP, mode = "fixed", waves = 6, boss_every = 3, breather = 2, start_delay = 1, ammo_every = 20,
                   mutators = CHAOS, elites = CHAOS, mutator_from = 1, mutator_chance = 0.8, mutator_chance_max = 0.8, affix_from = 1, affix_chance = 0.3,
                   affix_chance_max = 0.3, whacky = CHAOS and 2 or nil, coop = mode.env_flag("GE_MAPSMOKE_COOP") or nil,   -- (co-op: GE_COOP=1 too)
                   debug_track = mode.env_flag("GE_MAPSMOKE_TRACK"), debug_path = mode.env_flag("GE_MAPSMOKE_TRACK") }
include("arena")

local t = 0
local born = {}        -- chr -> { t0, min, near, wave_n }
local spawned, killed, never, late, engaged = 0, 0, 0, 0, 0
local PARTS = { "head", "body", "body", "limb" }
on("chr_spawned", function(c)
  if t < 0.9 then return end
  local x, z = spawn.where(c)
  local bx, _, bz = bond.pos(0)
  local d = (x and bx) and math.sqrt((x - bx) ^ 2 + (z - bz) ^ 2) or -1
  spawned = spawned + 1
  born[c] = { t0 = t, min = d, near = nil }
  print(string.format("MAPSMOKE spawn chr %d at (%.0f, %.0f) %.0f from Bond", c, x or -1, z or -1, d))
end)

local function shoot_dead(c)
  for _ = 1, 40 do if spawn.alive(c) then test.shoot(c, PARTS[math.random(#PARTS)], "pp7", 0) end end
  if spawn.alive(c) then spawn.kill(c) end
end

local SHOTS = mode.env_flag("GE_MAPSMOKE_SHOTS")   -- diagnosis: an enemy that stands still for 10 s gets Bond beside it and a picture
local SHOT1 = mode.env_flag("GE_MAPSMOKE_SHOT1")   -- the picture of the report: one frame mid-wave (Bond put 5 m from the nearest enemy, looking at it)
local shots_taken, shot_at, shot1_done = 0, nil, false
local function shot1(bx, bz)
  local best, bd
  for c in pairs(born) do
    local x, z = spawn.where(c)
    if x then
      local d = math.sqrt((x - bx) ^ 2 + (z - bz) ^ 2)
      if d > 600 and (not bd or d < bd) then best, bd = c, d end
    end
  end
  if not best then return end
  local x, z = spawn.where(best)
  local near = pads.far_from(x, z, 380, 900, true)
  if #near == 0 then return end
  local pick = near[(#near + 1) // 2]   -- (a pad in the middle of the ring: the nearest are often through a wall)
  local px, _, pz = pads.pos(pick)
  bond.warp(0, pick)
  if bench and bench.look then pcall(bench.look, 0, math.deg(math.atan(-(x - px), (z - pz))), -4) end
  shot_at, shot1_done = t + 0.7, true
  print(string.format("MAPSMOKE shot1: chr %d at (%.0f, %.0f), Bond put on pad %d", best, x, z, pick))
end
on("tick", function(dt)
  t = t + dt
  local bx, _, bz = bond.pos(0)
  if shot_at and t >= shot_at then shot_at = nil; if bench and bench.shot then bench.shot() end end
  if SHOT1 and not shot1_done and t >= 22 and bx then shot1(bx, bz) end
  for c, b in pairs(born) do
    if not spawn.alive(c) then
      born[c] = nil
    else
      local x, z, act = spawn.where(c)
      if SHOTS and x and not shot_at and shots_taken < 3 and not b.shot then
        if not b.px or (x - b.px) ^ 2 + (z - b.pz) ^ 2 > 120 ^ 2 then b.px, b.pz, b.pt = x, z, t
        elseif t - b.pt > 10 then
          local near = pads.far_from(x, z, 200, 800, true)
          if #near > 0 then
            b.shot = true
            shots_taken = shots_taken + 1
            bond.warp(0, near[1])
            local px, _, pz = pads.pos(near[1])
            if bench and bench.look then pcall(bench.look, 0, math.deg(math.atan(-(x - px), (z - pz))), -8) end   -- (forward is (-sin yaw, cos yaw))
            shot_at = t + 0.8
            print(string.format("MAPSMOKE shot %d: chr %d stands at (%.0f, %.0f) act %s, Bond put on pad %d", shots_taken, c, x, z, tostring(act), near[1]))
          end
        end
      end
      if x and bx then
        local d = math.sqrt((x - bx) ^ 2 + (z - bz) ^ 2)
        if d < b.min then b.min = d end
        if d < 1800 then b.near = b.near or t else b.near = nil end
        if act == 8 or act == 9 or act == 10 then b.engaged = b.engaged or t end   -- (ATTACK, ATTACKWALK, ATTACKROLL: it has Bond in sight and fires)
        if not b.sx or (x - b.sx) ^ 2 + (z - b.sz) ^ 2 > 250 ^ 2 or b.engaged then b.sx, b.sz, b.st = x, z, t end   -- (the last place it made headway)
        local age = t - b.t0
        if b.near and t - b.near >= 2 then
          killed = killed + 1
          print(string.format("MAPSMOKE kill chr %d after %.1f s (closest %.0f)", c, age, b.min))
          shoot_dead(c); born[c] = nil
        elseif b.engaged and t - b.engaged >= 6 then   -- stopped at range and firing: the AI's own choice, not a stall
          engaged = engaged + 1
          print(string.format("MAPSMOKE kill chr %d after %.1f s: firing from %.0f", c, age, d))
          shoot_dead(c); born[c] = nil
        elseif t - b.st >= 30 or age >= 90 then   -- no headway for 30 s (stuck), or a long way round (a cave's winding path): killed after 90 s
          local stuck = t - b.st >= 30
          if stuck then never = never + 1 else late = late + 1 end
          print(string.format("MAPSMOKE %s chr %d at (%.0f, %.0f) after %.0f s: closest %.0f, now %.0f from Bond, no headway for %.0f s",
            stuck and "STUCK" or "LATE", c, x, z, age, b.min, d, t - b.st))
          shoot_dead(c); born[c] = nil
        end
      end
    end
  end
end)

on("bond_died", function() print("MAPSMOKE bond died") end)
local dlast = -100
if mode.env_flag("GE_MAPSMOKE_DOORS") then   -- diagnosis: the doors' state every 5 s
  on("tick", function(dt)
    if t - dlast >= 5 then
      dlast = t
      local parts = {}
      for _, i in ipairs(door.list()) do local d = door.get(i); parts[#parts + 1] = string.format("%d:%s/%.2f", i, d.state, d.open) end
      print("MAPSMOKE doors t=" .. math.floor(t) .. " " .. table.concat(parts, " "))
    end
  end)
end
local last = -100
on("tick", function(dt)
  if t - last >= 30 then
    last = t
    print(string.format("MAPSMOKE summary t=%.0f: spawned %d, killed near Bond %d, firing from range %d, late %d, STUCK %d, alive %d", t, spawned, killed, engaged, late, never,
      (function() local n = 0 for _ in pairs(born) do n = n + 1 end return n end)()))
  end
end)
