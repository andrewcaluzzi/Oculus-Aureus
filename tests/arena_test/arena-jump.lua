-- Test keys: presses the four wave-jump keys on a timer (ARENA_TEST_KEY = the same path as the PageUp/PageDown handler), first a fixed
-- script (across the wave 5 boss and back, and "back" at wave 1), then random jumps until SOAK seconds have passed. Run with
-- GE_MODE=arena-jump GE_STAGE=colosseum GE_CHEATS=invuln GE_FRAMES=20000; the mode logs every jump ("ge: arena: TEST ...") and the pool watch.
local SOAK = 300
ARENA_SETTINGS = { mode = "endless", boss_every = 5, breather = 3, start_delay = 1, crowd = 20, max_alive = 12,
  debug_kill = 7.0, ammo_every = 15, test_keys = true }
include("arena")
local FIXED = { { 6, "next+" }, { 10, "next" }, { 14, "next" }, { 18, "next" }, { 22, "back+" }, { 26, "next+" }, { 30, "back" },
                { 33, "back" }, { 36, "back" }, { 39, "back" }, { 42, "back" }, { 46, "next" }, { 50, "next" }, { 54, "next" },
                { 58, "next+" }, { 62, "next+" }, { 66, "back" } }
local KEYS = { "next", "back", "next+", "back+" }
local t, i, nextrand, done = 0, 1, 70, false
local OFF = mode.env_flag("GE_JUMP_OFF")   -- control run: same load, no key presses
on("tick", function(dt)
  t = t + dt
  if OFF then
    if t >= SOAK and not done then done = true; print("ge: jumptest: DONE (control) at t=" .. math.floor(t)) end
    return
  end
  while FIXED[i] and t >= FIXED[i][1] do
    print("ge: jumptest: t=" .. math.floor(t) .. " key " .. FIXED[i][2])
    ARENA_TEST_KEY(FIXED[i][2])
    i = i + 1
  end
  if t >= nextrand and t < SOAK then
    ARENA_TEST_KEY(KEYS[math.random(#KEYS)])
    nextrand = t + 1.5 + math.random() * 5
  end
  if t >= SOAK and not done then
    done = true
    print("ge: jumptest: DONE at t=" .. math.floor(t))
  end
end)
