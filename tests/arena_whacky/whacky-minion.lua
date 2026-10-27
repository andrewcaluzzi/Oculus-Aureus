-- Whacky minion launcher, end to end (run.sh minion; GE_CHEATS=invuln; GE_FRAMES ~3600). No waves (start_delay 600).
--  1 s  arm Bond 0 (whacky.launcher, 4 shots, 3 a rocket, scale 0.25, life 14 s), give + equip the rocket launcher
--  3 s  face +x, fire one rocket (test.fire, one tap): it flies to the pit wall and bursts there
--  then when the minions appear, a target soldier is spawned on a pit pad ~1500 units from them (armed, accuracy 0, holds his post)
--  the minions must run at him and kill him with knives (chr_died killer = a minion); then they fade out at the end of their life
-- PASS lines: "WMINTEST spawned n", "WMINTEST kill by minion", "WMINTEST despawn ok". The C log has each minion's model scale and box.
ARENA_SETTINGS = { mode = "endless", start_delay = 600, crowd = 60, crowd_impostors = 200, ammo_every = 0, whacky = true }
include("arena")

local function pad_at(x, z)
  local best, bd = nil, 1e18
  for i = 0, pads.count() - 1 do
    local px, py, pz = pads.pos(i)
    if px and py < 1 and math.abs(px) <= 4500 and math.abs(pz) <= 4500 then
      local d = (px - x) ^ 2 + (pz - z) ^ 2
      if d < bd then best, bd = i, d end
    end
  end
  return best
end

local t, step, target, seen, fired_at, killed = 0, 0, nil, 0, 0, false
on("chr_died", function(victim, killer)
  if target and victim == target then
    killed = true
    print(string.format("ge: whacky: WMINTEST target %d died, killer %s, minion %s at t=%.1f", victim, tostring(killer),
      tostring(killer and killer >= 28000 and killer < 32000), t))
    if killer and killer >= 28000 and killer < 32000 then print("ge: whacky: WMINTEST kill by minion") end
  end
end)
on("tick", function(dt)
  t = t + dt
  if step == 0 and t > 1 then
    step = 1
    print("ge: whacky: WMINTEST arm", whacky.launcher(0, 4, { count = 3, scale = 0.25, life = 14, health = 0.3 }),
      bond.give(0, "rocketlaunch", 8), bond.equip(0, "rocketlaunch"))
  elseif step == 1 and t > 3 then
    step = 2
    local bx, _, bz = bond.pos(0)
    test.face(0, bx + 5000, bz)
    test.press("z")   -- the real trigger (pad 0); test.fire is for gadgets only
    print("ge: whacky: WMINTEST fire", tostring(bond.weapon(0)), "from", math.floor(bx), math.floor(bz))
    fired_at = t
  elseif step == 2 then
    local m = whacky.minions()
    if #m > 0 then
      step = 3
      seen = #m
      local x, z = spawn.where(m[1])
      print(string.format("ge: whacky: WMINTEST spawned %d at t=%.1f (%.1f s after the shot), first at (%.0f, %.0f), launcher left %d",
        #m, t, t - fired_at, x or 0, z or 0, whacky.launcher_shots(0)))
      local bx, _, bz = bond.pos(0)
      local tx, tz = x - 1300, (z or 0) + 700
      if bx and math.abs(tz - bz) < 300 then tz = tz + 600 end   -- not in Bond's line of fire
      target = spawn.chr(pad_at(tx, tz), { skin = "russian_soldier", weapon = "pp7", accuracy = 0, behaviour = "hold", health = 1,
                                           face = { x or 0, z or 0 } })
      print("ge: whacky: WMINTEST target", target, "at pad", pad_at(tx, tz))
    elseif not st_logged and t - fired_at > 1.5 then
      st_logged = true
      print("ge: whacky: WMINTEST 1.5 s after the shot: shots", bond.shots(0), "weapon", tostring(bond.weapon(0)))
    elseif t - fired_at > 8 then
      step = 9
      print("ge: whacky: WMINTEST FAIL no minions 8 s after the shot")
    end
  elseif step == 3 then
    local m = whacky.minions()
    if #m == 0 then
      step = 4
      local s, e = whacky.minion_stats()
      print(string.format("ge: whacky: WMINTEST despawn ok at t=%.1f (spawned %d, expired %d, target killed %s)", t, s, e, tostring(killed)))
    elseif t - fired_at > 40 then
      step = 9
      print("ge: whacky: WMINTEST FAIL minions still there at t=" .. t)
    end
  end
end)
