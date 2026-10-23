-- Hostages are not "guards": the chr search of the guard / escort AI lists (sub_GAME_7F033B38, 'a guard within N units') takes the first living chr
-- in the chr slots, so a hostage spawned BEFORE a guard used to be found first. Flatland: a hostage on pad 113 (400,0), a guard on pad 114 (800,0)
-- and a second guard on pad 99 (800,-400). Run with GE_TEST_CHRSEARCH=60:7300:500 (chr 7300, the first guard, searches within 500 units).
-- Expect: "-> chr 7301 (hostage state -1)" (the other guard); with GE_TEST_HOSTAGEPICKOLD=1 (the old behaviour) "-> chr 7200 (hostage state 0)".
on("stage_start", function()
  local h = spawn.hostage(113)
  local g1 = spawn.guard(114, { weapon = "ak47" })
  local g2 = spawn.guard(99, { weapon = "ak47" })
  print("CS hostage", h, "guards", g1, g2)
end)
