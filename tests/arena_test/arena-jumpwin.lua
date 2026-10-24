-- Test keys, fixed mode: 3 waves, then "next" past the last wave must win the mission (ge: arena: WON). Run with
-- GE_MODE=arena-jumpwin GE_STAGE=colosseum GE_CHEATS=invuln GE_FRAMES=1500.
ARENA_SETTINGS = { mode = "fixed", waves = 3, boss_every = 5, breather = 3, start_delay = 1, crowd = 0, test_keys = true }
include("arena")
local t, done = 0, {}
on("tick", function(dt)
  t = t + dt
  for _, k in ipairs({ { 5, "next" }, { 8, "next+" }, { 11, "next" }, { 14, "next" } }) do
    if t >= k[1] and not done[k[1]] then done[k[1]] = true; ARENA_TEST_KEY(k[2]) end
  end
end)
