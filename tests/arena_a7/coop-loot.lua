-- A7 loot: one wave of 4 unarmoured AK47 soldiers, each killed 1.5 s after it spawns (at its gate, so the guns lie at the gates), then a
-- 25 s breather. The bot (silenced PP7 at the start) should walk to a dropped AK47 (act loot), pick it up by walking over it and switch
-- to it (weapon 8 = ITEM_AK47). Logs the bot's act and weapon every second. Run with GE_COOP_GOD=0.
local function foe() return { n = 1, spec = { skin = "russian_soldier", weapon = "ak47", behaviour = "hold", health = 0.3, _tier = "normal" } } end
ARENA_SETTINGS = { mode = "endless", boss_every = 0, crowd = 0, start_delay = 2, breather = 25, ammo_every = 0, armour_chance = 0,
                   debug_kill = 1.5, coop_debug = true,
                   debug_queue = function(w) if w == 1 then local q = {} for i = 1, 4 do local e = foe(); e.n = i; q[i] = e end return q end return {} end }
include("arena-coop")
local acc, t = 0, 0
on("tick", function(dt)
  t = t + dt
  acc = acc + dt
  if acc >= 1 then
    acc = 0
    local i = coop.bot_info(1)
    if i then print(string.format("ge: arena: TEST t=%.0f bot act %s weapon %d ammo %d", t, i.act, i.weapon, i.ammo)) end
  end
end)
