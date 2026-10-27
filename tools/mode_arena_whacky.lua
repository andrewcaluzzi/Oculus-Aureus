-- Arena, 20 fixed waves, WHACKY: nearly every wave rolls a mutator, from the Whacky set (arena_whacky.lua: ghosts, chameleons, shadow
-- troops, glass cannons, giants, fast forward, slow motion, rocket party, double trouble, glitch, speed swap, the crowd's gifts, minion
-- madness, tiny mayhem) and the A7 set; affix elites glow in their affix's colour, Baron Samedi comes in gold, and every boss wave
-- ends with the crowd throwing Bond the minion launcher. Shipped to Data/Scripts/modes/arena-whacky.lua (GE_MODE=arena-whacky).
ARENA_SETTINGS = {
  mode = "fixed", mutators = true, elites = true, whacky = true,
  mutator_from = 2, mutator_chance = 0.70, mutator_chance_inc = 0.03, mutator_chance_max = 0.95, mutator_bosses = true,
  mutator_list = { "ghost", "chameleon", "shadow", "glass", "giants", "fastfwd", "slowmo", "rockets", "double", "glitch", "swap",
                   "crowd", "minions", "mayhem", "knives", "paintball", "one_hit", "turbo", "fog", "dk", "tough", "bottomless" },
  affix_from = 2, affix_chance = 0.08,
}
include("arena")
