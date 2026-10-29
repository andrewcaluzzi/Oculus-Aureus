-- Pictures of the hub's SNIPER DUEL card and of the duel's debrief (tests/sniper/shots.sh; copied to build_pc/Data/Scripts/zz_sniper_shots.lua, which
-- runs at every mode start and does nothing outside the hub). On a first boot it moves to the SNIPER DUEL tile (two DOWN presses) and takes the
-- card page once the hub's dossier shows it; after a run (the hub loads on lobby 'sniper/postgame') it takes the debrief once it is up.
if mode.name() ~= "hub" then return end
local t, step = 0, 0
local UP, DOWN, SELECT = 1, 2, 16
local function shot(tag) test.screenshot(); print("SNSHOT " .. tag) end
on("ui_tick", function(dt)
  if not HUB_MENU then return end
  t = t + dt
  local cur = ui.current()
  local debrief = cur and cur.lobby == "sniper/postgame"
  if debrief then
    if step == 0 and t >= 3.0 then step = 1; shot("debrief") end
    return
  end
  if step == 0 and t >= 1.5 then step = 1; test.plan_input(0, DOWN) end
  if step == 1 and t >= 1.8 then step = 2; test.plan_input(0, DOWN) end
  if step == 2 and t >= 2.4 then step = 3; shot("hub-card-sniper-dossier") end
  if step == 3 and t >= 2.8 then step = 4; test.plan_input(0, SELECT) end
  if step == 4 and t >= 4.0 then step = 5; shot("sniper-card") end
  if step == 5 and t >= 4.6 and mode.env_flag("GE_SNIPER_BEGIN") then step = 6; print("SNSHOT begin"); test.plan_input(0, SELECT) end   -- BEGIN THE DUEL
end)
