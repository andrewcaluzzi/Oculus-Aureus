-- The heist start menu's flow test RUN mode (tests/heist_menu/run.sh copies this to Data/Scripts/modes/heist_flowtest.lua; the lobby
-- launches it instead of heist under GE_HEISTMENU_TEST=1). It tells the runs apart by the hand-off's launch count (ui.current().seq:
-- 1 = the first BEGIN; the lobby's own reload after a run counts too, so AGAIN is seq 3):
--   run 1: the existing win-path test (tests/heist/heist_winpath.lua, the real tools/mode_heist.lua): its own HEIST_SETTINGS win over
--          the menu's, the menu's fill the rest. The win path replaces mission.win / mission.fail with recorders; here the real ones
--          are chained back on, so the job really ends and the flow's debrief comes up in the lobby.
--   run 2 (AGAIN): the plain heist with the same settings; Bond is killed after 6 s: BUSTED.
-- Both runs start on the planning board (GE_PLANNING=1): the driver (zz_hm_drive.lua) buys the silenced PP7 and confirms.
local cur = ui and ui.current() or nil
local n = cur and cur.seq or 1
print(string.format("ge: heist: FLOWTEST run seq %d", n))
if n <= 1 and not mode.env_flag("GE_HUB_TEST_KILL") then   -- (the hub test: its one heist run is the plain job, Bond killed)
  local real_win, real_fail = mission.win, mission.fail
  include("heist_winpath")
  local tw, tf = mission.win, mission.fail
  mission.win = function(m) tw(m); return real_win(m) end
  mission.fail = function(m) tf(m); return real_fail(m) end
else
  HEIST_SETTINGS = { test = true, cover = false }
  include("heist")
  local t, done = 0, false
  on("tick", function(dt)
    t = t + dt
    if not done and HEIST and HEIST.ready and t >= 6 then
      done = true
      print("ge: heist: FLOWTEST killing Bond")
      test.coop_kill(0)
    end
  end)
end
