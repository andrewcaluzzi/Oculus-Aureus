-- Report-page safety: a 63-character debrief line with no space, and one with bytes the font has no glyph for, must render on the
-- Colosseum's report page without overflowing the stock textWrap's 32-byte word buffer (front.c -> textrelated.c).
-- 1 fixed wave removed 3 s after it starts (debug_clear_after), so the run is won in a few seconds and the report page follows.
-- Expected log: two "ge: arena: debrief line N cleaned" lines (the 63-char run broken with spaces; 4 bytes dropped from the other),
-- "ge: arena: WON", then the frame limit with exit 0 (no crash on the report page). GE_SHOT shows the page.
local LONG = string.rep("W", 63)
local BAD = "BAD\1\127\200\255BYTES-" .. string.rep("x", 40)
local real = arena.debrief
arena.debrief = function(lines)   -- the scores module's lines, with the two test lines in front
  if type(lines) == "table" and #lines > 0 then
    local t = { LONG, BAD }
    for _, l in ipairs(lines) do t[#t + 1] = l end
    lines = t
    print("ge: arena: A7TEST debrief with a " .. #LONG .. "-char line and a bad-byte line")
  end
  return real(lines)
end
ARENA_SETTINGS = { mode = "fixed", waves = 1, boss_every = 0, breather = 1, start_delay = 1, crowd = 0, debug_clear_after = 3,
  ammo_every = 0, armour_chance = 0, scores_force = true }
include("arena")
