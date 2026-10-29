-- A long arena-plus run's post-game, for a screenshot (tests/arena_plus/debrief_shot.sh): 20 waves, elites and mutators, credits, ten
-- Requisition cards. Runs as a zz script in the arena lobby (GE_MODE=arena-menu): it hands the lobby a made-up result.
local cards = { { "EXTENDED CLIPS", 4 }, { "Q'S BATTERIES", 3 }, { "DOUBLE-O STATUS", 3 }, { "LEITER'S CACHE", 2 }, { "SAVILE ROW LINING", 1 },
                { "MI6 FIELD MEDIC", 1 }, { "BLOOD MONEY", 2 }, { "AIRDROP PRIORITY", 2 }, { "SWISS ACCOUNT", 1 }, { "BLOOD BOUNTY", 1 } }
local waves = {}
for w = 1, 20 do waves[w] = { 6 + w, 30 + w, (w % 5 == 0) and 1 or ((w % 3 == 0) and 2 or 0) } end
local res = { win = false, wave = 17, waves = 20, kills = 187, time = 1423, shots = 900, hits = 410, headshots = 66, multi = 5, streak = 31, damage = 400,
              hits_taken = 44, peak = 82, medal = 2, news = { "WAVE 17", "KILLS 187" }, first = false, recorded = true, medal_new = true, elites = 14,
              mutators = 6, label = "SPECTRE PROTOCOL", preset = "chaos", coop = false, credits = 6112, spent = 4380, cards = cards, wave_list = waves,
              title = "THE LAST STAND" }
local t, done, shot = 0, false, false
on("ui_tick", function(dt)
  t = t + dt
  if not done and t > 1.0 and ARENA_MENU and ARENA_MENU.test_postgame then
    done = true
    ARENA_MENU.test_postgame(ARENA_MENU_DEFS.serialize(res))
  end
  if done and not shot and t > 3.5 then shot = true; test.screenshot(); print("DEBRIEFSHOT taken") end
end)
