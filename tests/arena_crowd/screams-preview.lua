-- Scream crowd preview (tests/arena_crowd/run-screams.sh): the arena with crowd_screams = "only" and no wave yet (start_delay 60),
-- firing a kill, a wave clear a second later and then a boss kill through the arena's own react(), so the audio (SDL's disk
-- driver) holds the three bursts. Logs the presentation sounds playing / queued every tick, and quits by GE_FRAMES.
ARENA_SETTINGS = { start_delay = 60, crowd = 0, crowd_impostors = 0, crowd_screams = "only" }
include("arena")

local t, fired = 0, {}
local plan = { { 1.0, "kill" }, { 2.0, "wave_clear" }, { 5.0, "boss_kill" } }
on("tick", function(dt)
  t = t + dt
  for i, p in ipairs(plan) do
    if not fired[i] and t >= p[1] then
      fired[i] = true
      print(string.format("ge: test: t=%.2f fire %s -> %s", t, p[2], tostring(ARENA_HOOKS.react(p[2]))))
    end
  end
  local n, q = sound.sfx_playing()
  print(string.format("ge: test: t=%.2f sfx playing %d queued %d", t, n, q))
end)
