-- A7: best scores, fixed mode. 2 waves, each removed 3 s after it starts (debug_clear_after), so the run is won in about 10 s and the
-- report page follows. scores_force records despite the test hook. Run with GE_NOSCORESAVE=0 and GE_SCOREFILE=<a test file>.
-- Expected: "SCORES fixed2: wave 2 ..." with the new-best flags; the file holds fixed2_best_wave/kills/time/medal/runs/clears;
-- the HUD's last lines "MEDAL: PLATINUM" and "FIRST RUN RECORDED" / "NEW BEST: ..." / "BEST: ..."; the report page lists the lines.
ARENA_SETTINGS = { mode = "fixed", waves = 2, boss_every = 0, breather = 1, start_delay = 1, crowd = 0, debug_clear_after = 3,
  ammo_every = 0, armour_chance = 0, scores_force = true }
include("arena")
on("stage_start", function()
  local all = scores.all()
  local keys = {}
  for k, v in pairs(all) do keys[#keys + 1] = k .. "=" .. tostring(v) end
  table.sort(keys)
  print("ge: arena: A7TEST scores at start (" .. tostring(scores.enabled()) .. ", " .. scores.path() .. "): " .. table.concat(keys, " "))
end)
