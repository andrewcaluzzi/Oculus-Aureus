-- Whacky: one effect per run, for a look and a smoke check (run.sh fx <name>: GE_WFX_<NAME>=1 picks it; GE_SHOT=300 GE_FRAMES=620).
-- Wave 1 (at 2 s) rolls exactly that mutator (chance 1, list {name}). At 1 s three display chrs stand 900 units ahead of Bond at
-- x -400 / 0 / +400 (the same body and head, spectators: they stay put) and get the effect's per-chr look, so the shot shows it up
-- close. Two extra names: "elites" (the three get the armoured / swift+knives / explosive glow) and "golden" (the champion's gold).
-- Log: "WFXTEST <name> ..." lines; "WFXTEST done" at 8 s with the chrfx count, the rate and line mode.
local NAMES = { "ghost", "chameleon", "shadow", "glass", "giants", "fastfwd", "slowmo", "rockets", "double", "glitch", "swap", "crowd",
                "minions", "mayhem", "elites", "golden" }
local pick = "ghost"
for _, n in ipairs(NAMES) do if mode.env_flag("GE_WFX_" .. string.upper(n)) then pick = n end end
local list = (pick == "elites" or pick == "golden") and { "chameleon" } or { pick }
ARENA_SETTINGS = { mode = "endless", start_delay = 2, breather = 60, crowd = 60, crowd_impostors = 300, ammo_every = 0, whacky = true,
                   mutators = true, mutator_from = 1, mutator_chance = 1, mutator_chance_max = 1, mutator_list = list, elites = false }
if pick == "elites" or pick == "golden" then ARENA_SETTINGS.mutators = false end
include("arena")

local function pad_at(x, z)
  local best, bd = nil, 1e18
  for i = 0, pads.count() - 1 do
    local px, py, pz = pads.pos(i)
    if px and py < 1 then
      local d = (px - x) ^ 2 + (pz - z) ^ 2
      if d < bd then best, bd = i, d end
    end
  end
  return best
end

local t, step, shown = 0, 0, {}
on("tick", function(dt)
  t = t + dt
  if step == 0 and t > 1 then
    step = 1
    local bx, _, bz = bond.pos(0)
    for k, dx in ipairs({ -400, 0, 400 }) do
      local c = spawn.chr(pad_at(bx + dx, bz + 900), { body = "Russian_Soldier", head = "Male_Karl", weapon = "none", behaviour = "spectator",
                                                      face = { bx, bz } })
      shown[k] = c
    end
    local WH, D = ARENA_WHACKY.last, ARENA_MUTATORS.DEFS[pick]
    for k, c in ipairs(shown) do
      if c then
        if pick == "elites" then
          local aff = ({ { "armoured" }, { "swift", "knives" }, { "explosive" } })[k]
          WH.base_look({ c = c, affixes = aff })
        elseif pick == "golden" then
          WH.base_look({ c = c, boss = true, weapon = "goldengun" })
        elseif D and D.spawned then
          D.spawned(c, {})
        end
      end
    end
    if pick == "minions" or pick == "mayhem" then   -- a close-up group of Nick Nacks between Bond and the display chrs
      print("ge: whacky: WFXTEST minions_at", whacky.minions_at(0, bx + 150, bz + 450, 3))
    end
    print(string.format("ge: whacky: WFXTEST %s display chrs %s %s %s, chrfx %d", pick, tostring(shown[1]), tostring(shown[2]),
      tostring(shown[3]), chrfx.count()))
  elseif step == 1 and t > 8 then
    step = 2
    print(string.format("ge: whacky: WFXTEST done %s: chrfx %d, anim rate %.2f, line mode %s, minions %d, launcher %d", pick, chrfx.count(),
      whacky.anim_rate(), tostring(whacky.linemode()), #whacky.minions(), whacky.launcher_shots(0)))
  end
end)
