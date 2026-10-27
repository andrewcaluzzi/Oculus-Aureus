-- A7 revive: Bond 1 (player 0, standing still) is put down at 0:14 and 0:40, mid-wave; the partner bot must walk over and revive
-- him (3 s within 170 units) before the 30 s bleed-out. Expected: "player 0 DOWN" then "player 0 REVIVED" twice, the HUD lines
-- (GE_MODEHUD_LOG=1) "DOWN ... BEING REVIVED n%", and the waves go on. Run with GE_COOP_GOD=0 GE_MODEHUD_LOG=1.
ARENA_SETTINGS = { waves = 4, boss_every = 0, crowd = 0, start_delay = 2, breather = 4, coop_debug = true }
include("arena-coop")
local t, downs = 0, { 14, 40 }
on("tick", function(dt)
  t = t + dt
  for i, at in ipairs(downs) do
    if at and t >= at then
      downs[i] = false
      print(string.format("ge: arena: TEST put player 0 down at %.1fs: %s", t, tostring(test.coop_down(0))))
    end
  end
  local s, left, frac = coop.state(0)
  if s == "down" then print(string.format("ge: arena: TEST t=%.2f player 0 down, %.1fs left, revive %.0f%%", t, left, frac * 100)) end
end)
