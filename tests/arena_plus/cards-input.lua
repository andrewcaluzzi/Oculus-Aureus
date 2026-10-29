-- The Requisition cards under REAL input (tests/arena_plus/run_cards_input.sh drives it with xdotool into the game window, real time).
-- AC's playtest (wave 9): "I think I was shooting or clicking (mouse down) when I killed the last enemy, and the Requisition screen was
-- unclickable." The run: one enemy per wave, killed 2.5 s after it shows; the cards open; the script logs what happened and the shell
-- presses things:
--   CARDSTEST arm k     the shell may hold a button now (it is held when the last enemy dies)
--   CARDSTEST open k    the cards are up (the shell clicks / types)
--   CARDSTEST taken k   a card was taken and the run went on; CARDSTEST STUCK k after 60 s of the cards up
ARENA_SETTINGS = { mode = "fixed", waves = 8, boss_every = 0, breather = 1, start_delay = 1, first_wave = 1, ammo_every = 0, armour_chance = 0,
  max_alive = 4, max_alive_cap = 4, test_keys = true, corpse_keep = 2,
  crowd = { count = 6, favour = false, drift = 0, roar_every = 9999, sponsor_cooldown = 9999, thumb = false, points = { camp_secs = 9999 } },
  plus = { pu_chance = 0, pu_chance_boss = 0 },
  debug_queue = function(w)
    return { { n = 1, gate = 1 + (w % 4), spec = { skin = "russian_soldier", weapon = "pp7", behaviour = "hold", health = 0.05, accuracy = 0,
                                                      _tier = "normal" } } }
  end }
include("arena")

local PL = ARENA_PLUS and ARENA_PLUS.live
local function log(fmt, ...) print(string.format("CARDSTEST " .. fmt, ...)) end
if not PL then log("FAIL no ARENA_PLUS.live"); return end

local k, phase, t, since = 0, "wait", 0, 0
on("tick", function(dt) end)
on("ui_tick", function(dt)
  t = t + dt
  since = since + dt
  local live = ARENA_HOOKS.live()
  if phase == "wait" and #live > 0 then
    k, phase, since = k + 1, "arm", 0
    log("arm %d", k)
  elseif phase == "arm" and since >= 2.5 and #live > 0 then
    for _ = 1, 80 do if spawn.alive(live[1]) then test.shoot(live[1], "body", "pp7", 0) end end
    phase, since = "dead", 0
    log("kill %d", k)
  elseif phase == "dead" and PL.held() then
    phase, since = "open", 0
    local up, ptr = ui.state()
    log("open %d (menu_up %s gameplay_pointer %s)", k, tostring(up), tostring(ptr))
  elseif phase == "open" then
    if k == 1 and not SHOT and since >= 2 then SHOT = true; test.screenshot() end
    if not PL.held() then
      log("taken %d after %.1f s", k, since)
      phase, since = "wait", 0
    elseif since > 25 then
      log("STUCK %d (taking a card from the script so the next case can run)", k)
      PL.pick(1)
      phase, since = "wait", 0
    end
  end
end)
