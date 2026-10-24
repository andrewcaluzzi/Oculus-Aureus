-- Crowd reactions test (tests/arena_crowd/run-react.sh). A short arena: wave 2 is a boss wave, every enemy is killed
-- debug_kill seconds after it spawns (every 4th as a headshot), so kills, multi-kills, a headshot, the boss kill and the wave clears
-- all fire. With GE_CHEATS=invuln it is won; without, set GE_CROWD_TEST_DIE=1: it starts at the armed wave 3 and the enemies live long enough to shoot Bond down
-- (big hit, low health, the death boo). Once a second it logs how many real spectators are mid-animation.
local die = mode.env_flag("GE_CROWD_TEST_DIE")
ARENA_SETTINGS = { waves = 3, boss_every = 2, start_delay = 3, breather = 8, crowd = 80, crowd_impostors = 400,
                   debug_kill = die and 400 or 4, debug_headshot = 4 }
if die then ARENA_SETTINGS.first_wave = 3 end   -- armed enemies: their bursts land big hits
if mode.env_flag("GE_CROWD_TEST_MISSING") then   -- a reaction whose sound file does not exist: it plays nothing
  ARENA_SETTINGS.REACT = { kill = { sound = "no_such_sound" } }
end
include("arena")

local acc, t = 0, 0
on("tick", function(dt)
  t = t + dt
  acc = acc + dt
  if acc < 1 then return end
  acc = 0
  local ids = ARENA_HOOKS.spectators()
  local busy, gone, kinds = 0, 0, {}
  for _, s in ipairs(ids) do
    local idle = spawn.idle(s.chr)
    if idle == nil then gone = gone + 1 elseif not idle then
      busy = busy + 1
      if s.last_react then kinds[s.last_react] = (kinds[s.last_react] or 0) + 1 end
    end
  end
  local parts = {}
  for k, n in pairs(kinds) do parts[#parts + 1] = k .. " " .. n end
  table.sort(parts)
  local h, a = bond.vitals(0)
  local _, anims = ARENA_HOOKS.reactions()
  print(string.format("ge: test: t=%.0f spectators %d animating %d (last reaction: %s) gone %d; reaction anims started %d; bond health %.2f armour %.2f; sounds playing %d",
    t, #ids, busy, table.concat(parts, ", "), gone, anims, h or -1, a or -1, sound and sound.playing() or -1))
end)
