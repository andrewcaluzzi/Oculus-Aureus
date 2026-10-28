-- The callout safe-area fit (port_arenahud.c calloutFit): fires the widest cards of the preview list (the level-5 "THE WORLD IS NOT
-- ENOUGH" that overran AC's 1280x720 screen, its level-6 "x7" and every boss card) every 2.6 s, with no enemies. Run by
-- tests/arena_a8/callout-fit.sh with GE_CALLOUT_FITLOG=1 (a "callout fit:" line per drawn frame) and GE_CALLOUT_ENTRANCE=0..6
-- (each entrance in turn); the script checks every span against the safe area.
ARENA_SETTINGS = { mode = "endless", boss_every = 0, breather = 999, start_delay = 1, ammo_every = 0, armour_chance = 0, crowd = { count = 20 },
  debug_queue = function() return {} end }
include("arena")

local cards = {}
do
  local l = ARENA_THEME.preview_list()
  for _, e in ipairs(l) do
    if e[2] >= 5 then cards[#cards + 1] = e end
  end
end
local t, next_t, n = 0, 2.0, 0
on("tick", function(dt)
  t = t + dt
  if t >= next_t and n < #cards then
    next_t = t + 2.6
    n = n + 1
    local e = cards[n]
    ARENA_THEME.callout(e[1], e[2], e[3])
    print(string.format("ge: arena: FITTEST card %d/%d: %s (level %d)", n, #cards, e[1], e[2]))
  elseif n >= #cards and t >= next_t then
    print("ge: arena: FITTEST done")
    n = n + 1000
  end
end)
