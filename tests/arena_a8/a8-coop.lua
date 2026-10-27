-- A8 crowd favour in co-op (GE_COOP=1, run with tests/arena_a7/run_coop.sh-style env: two Bonds, Bond 2 is the arena bot).
-- 3 waves of soldiers, crowd 30. At 8 s the test lands a headshot as Bond 1 (P1) and one as Bond 2 (P2) on two live enemies; the bot
-- fights the rest itself. Expected: CROWD lines for "player 0" and "player 1" moving ONE favour value; with GE_MODEHUD_LOG=1 line 4
-- shows "P1 HEADSHOT ..." / "P2 HEADSHOT ..." on this machine (both Bonds share the line).
ARENA_SETTINGS = { mode = "fixed", waves = 3, boss_every = 0, breather = 4, start_delay = 2, coop = true,
  crowd = { count = 30 } }
include("arena")

local t, done, en = 0, false, {}
on("chr_spawned", function(c) if t >= 1.9 then en[#en + 1] = c end end)
on("tick", function(dt)
  t = t + dt
  if not done and t > 8 then
    local live = {}
    for _, c in ipairs(en) do if spawn.alive(c) then live[#live + 1] = c end end
    if #live >= 2 then
      done = true
      for _ = 1, 30 do if spawn.alive(live[1]) then test.shoot(live[1], "head", "pp7", 0) end end
      for _ = 1, 30 do if spawn.alive(live[2]) then test.shoot(live[2], "head", "pp7", 1) end end
      print("ge: arena: A8TEST coop: P1 and P2 headshots on " .. live[1] .. " / " .. live[2])
    end
  end
end)
