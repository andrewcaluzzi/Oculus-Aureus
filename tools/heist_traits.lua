-- heist_traits.lua - guard personalities (designs/bank-heist.md section 32; stream `personalities`). Included at the end of
-- tools/mode_heist.lua when S.personalities is on (the default in play; a test's HEIST_SETTINGS without the key leaves it off, so every
-- old suite runs exactly as before). The work is in C (port/src/port_traits.c, Lua ai.trait*: port/luavm/port_luavm_traits.c); this file
-- deals the traits, takes the bribes, feeds the camera sightings in, and draws the dossiers (INTEL), the AR tags and the debrief's part.
--
-- Each of the bank's own guards (posted, raisers, card carriers) gets one trait from the sim RNG (math.random: GE_SEED solo, the session
-- seed in co-op and netplay), in placement order, once, when the mode is ready: the same seed deals the same guards.
--   ROOKIE      jumpy: hears and suspects faster, radios a mere suspicion in (a colleague comes to look), bad aim, falls back early
--   VETERAN     lazy: slow to react ("Probably nothing."), skips watchclock stations on his tour (a gap), but deadly once engaged
--   BY-THE-BOOK searches every noise fully, his missed check-in is noticed early, calls more backup
--   BRIBABLE    a USE press face to face while he suspects nothing: he takes S.bribe_cost off the crew's payout, looks away for good and
--               hands over his card if he carries one (refuses once he is suspicious; the debrief lists it)
--
-- Settings (S): personalities (true), bribe_cost (50000), traits_deck ({ none = 4, rookie = 2, veteran = 2, book = 2, bribable = 1 }:
-- weights; a job may carry its own as JOB.random.traits), traits_tune (ai.trait_tune names: skip 0.5, radio_cooldown 20, radio_range
-- 1600, acc_rookie -40, acc_veteran 10), bribe_reach (150).
-- Presentation (the INTEL dossiers, the AR tags) only reads; the deal, bribes, observations and skips are sim state in C (hashed).

HEIST_TRAITS = HEIST_TRAITS or {}
local T = HEIST_TRAITS
local H = HEIST
local S = H.S
H.traits = T

local ORDER = { "none", "rookie", "veteran", "book", "bribable" }
local DECK = { none = 4, rookie = 2, veteran = 2, book = 2, bribable = 1 }
local LABEL = { rookie = "ROOKIE", veteran = "VETERAN", book = "BY-THE-BOOK", bribable = "BRIBABLE", none = "STEADY" }
local NOTE = { rookie = "Rookie - jumpy", veteran = "Veteran - lazy, deadly", book = "By the book", bribable = "Has a price", none = "Steady - no quirks" }
local TAG_RGBA = { rookie = 0xFFE040FF, veteran = 0xFF9020FF, book = 0x60E8FFFF, bribable = 0xFFD040FF }
local DEALT_KIND = { post = true, raiser = true, carrier = true }

local function log(fmt, ...) print(string.format("ge: heist: traits: " .. fmt, ...)) end
local function money(v)
  local s = tostring(math.floor(v))
  local out = s:reverse():gsub("(%d%d%d)", "%1,"):reverse()
  if out:sub(1, 1) == "," then out = out:sub(2) end
  return "$" .. out
end

T.on = false
T.dealt = false
T.marked = {}        -- chr -> label drawn (presentation bookkeeping for the AR tags; nothing in the sim reads it)

local function deck()
  local job = type(heist_job) == "function" and heist_job() or nil
  local d = S.traits_deck or (job and job.random and job.random.traits) or DECK
  local list, total = {}, 0
  for _, name in ipairs(ORDER) do
    local w = d[name] or 0
    if w > 0 then list[#list + 1] = { name, w }; total = total + w end
  end
  return list, total
end

function T.deal()
  T.dealt = true
  local list, total = deck()
  if total <= 0 then return end
  local n = 0
  for _, g in ipairs(H.guards) do
    if DEALT_KIND[g.kind] and spawn.alive(g.chr) then
      local r, pick = math.random(total), "none"
      for _, e in ipairs(list) do
        if r <= e[2] then pick = e[1]; break end
        r = r - e[2]
      end
      ai.trait(g.chr, pick)
      n = n + 1
    end
  end
  local st = ai.trait_stats()
  log("dealt %d guards: %d rookies, %d veterans, %d by-the-book, %d bribable, %d steady", n, st.rookie, st.veteran, st.book, st.bribable, st.none)
end

on("stage_start", function()
  T.dealt, T.marked = false, {}
  T.on = S.personalities ~= false and H.phase ~= "off" and ai ~= nil and ai.trait_enable ~= nil
  if not T.on then return end
  ai.trait_enable(true)
  ai.trait_tune(S.traits_tune or {})
  if H.M and H.M.STATIONS then ai.trait_stations(H.M.STATIONS) end
end)

-- who is who on the board: the card carriers by their role, everyone else by his post's area
local function who(chr)
  for role, c in pairs(H.carriers or {}) do if c.chr == chr then return (role:gsub("_", " ")):upper() end end
  for _, g in ipairs(H.guards) do
    if g.chr == chr then
      local a = H.M and H.M.AREA_LABEL and H.M.AREA_LABEL[g.area] or g.area or "?"
      return string.format("#%d %s", chr, (tostring(a):gsub("_", " ")):upper())
    end
  end
  return "#" .. chr
end
T.who = who

-- ---------------------------------------------------------------------------------------------------------------------------------
-- the bribe: USE face to face with a guard (he faces you, you look at him) within reach

local function wrap(a) a = a % 360; if a > 180 then a = a - 360 end; return a end
local function bribe_mark(p)
  if not (gadget and gadget.aim and gadget.chr_yaw) then return nil end
  local yaw, _, x, _, z, feet = gadget.aim(p)
  if not x then return nil end
  local fx, fz = -math.sin(math.rad(yaw)), math.cos(math.rad(yaw))
  local reach = S.bribe_reach or 150
  local best, bd
  for _, g in ipairs(ai.traits()) do
    local c = g.chr
    if g.trait == "bribable" and spawn.alive(c) and not spawn.down(c) then
      local cx, cz = spawn.where(c)
      local d = cx and math.sqrt((cx - x) ^ 2 + (cz - z) ^ 2)
      if d and d <= reach and (not bd or d < bd) then
        local gyaw, cy = gadget.chr_yaw(c)
        local off = math.abs(wrap(math.deg(math.atan(x - cx, z - cz)) - (gyaw or 0)))
        local look = d > 1 and ((cx - x) * fx + (cz - z) * fz) / d or 1
        if off <= 80 and look >= 0.7 and not (cy and feet and math.abs(cy - feet) > 200) then best, bd = c, d
        elseif S.test then log("bribe: chr %d at %.0f not facing (off %.0f, look %.2f, dy %.0f)", c, d, off, look, (cy and feet) and (cy - feet) or 0) end
      end
    end
  end
  return best
end

local bribed_card = {}   -- chr -> the card flags he handed over (the debrief's "how")
function T.bribed(chr) return bribed_card[chr] ~= nil end

local function flash_p(p, text, secs) H.pflash[p] = { text, H.elapsed + (secs or 4) } end

-- Bond p offers money to the guard in front of him: true if there was a bribable guard to offer it to
function T.try_bribe(p)
  if not T.on or not T.dealt or H.phase == "off" or H.ended or not H.ready then return false end
  local c = bribe_mark(p)
  if not c or ai.trait(c) ~= "bribable" then return false end
  local cost = S.bribe_cost or 50000
  local r = ai.trait_bribe(c, p, cost)
  if r == "ok" then
    local flags = gadget.lift_key and gadget.lift_key(p, c) or 0
    if flags > 0 then bribed_card[c] = flags end
    ai.bark(c, "lost", flags > 0 and "Here. You never saw me." or "Didn't see a thing.")
    flash_p(p, string.format("Paid him %s: he looks away%s", money(cost), flags > 0 and " - and slips you his card" or ""), 5)
    log("Bond %d bribed chr %d (%s), card 0x%x", p, c, money(cost), flags)
  elseif r == "refused" then
    flash_p(p, "He won't take money now - he's on to something", 3)
  elseif r == "already" then
    flash_p(p, "He's already been paid", 2)
  end
  return true, r, c
end

on("use", function(p, kind, idx, objtype)
  if idx < 0 then T.try_bribe(p) end
end)

-- ---------------------------------------------------------------------------------------------------------------------------------
-- the clock: the deal, the camera sightings, the AR tags

local acc = 0
on("tick", function(dt)
  if not T.on or H.phase == "off" or H.ended or not H.ready then return end
  if not T.dealt then T.deal() end
  acc = acc + dt
  if acc < 0.5 then return end
  acc = 0
  local list = ai.traits()
  -- a guard a Bond watches on a camera feed counts as seen (direct sight is scanned in C)
  for p = 0, bond.count() - 1 do
    local cam = cctv.viewing(p)
    if cam then
      for _, e in ipairs(list) do
        if ((e.seen_by >> p) & 1) == 0 and spawn.alive(e.chr) and not spawn.down(e.chr) and cctv.sees(cam, e.chr) then ai.trait_observe(e.chr, p) end
      end
    end
  end
  -- the AR specs: a small tag under each guard the crew has had a look at (presentation; Shift+F3 hides it with the specs)
  if ar and ar.mark then
    for _, e in ipairs(list) do
      if e.observed and e.trait ~= "none" then
        local label = e.bribed and "PAID OFF" or LABEL[e.trait]
        local up = spawn.alive(e.chr) and not spawn.down(e.chr)
        if up and T.marked[e.chr] ~= label then
          ar.mark(e.chr, { label = label, rgba = e.bribed and 0x80FF80FF or TAG_RGBA[e.trait] or 0xFFFFFFFF })
          T.marked[e.chr] = label
        elseif not up and T.marked[e.chr] then
          ar.mark(e.chr, { label = "" })
          T.marked[e.chr] = nil
        end
      end
    end
  end
end)

-- ---------------------------------------------------------------------------------------------------------------------------------
-- INTEL: the GUARD FILE dossier on PERSONNEL (heist_intel.lua build_live calls this after its own live layer)

function T.intel()
  if not T.on or not intel then return end
  local list = ai.traits()
  local seen, paid = {}, 0
  for _, e in ipairs(list) do
    if e.observed then seen[#seen + 1] = e end
    if e.bribed then paid = paid + 1 end
  end
  local x, y, w, h = 226, 178, 188, 126   -- over the GUARDS dossier (card 5): the guards you have watched replace the generic note
  intel.at(4, 0, true)
  intel.rect(x + 3, y + 3, x + w + 3, y + h + 3, 0x00000060)
  intel.rect(x, y, x + w, y + h, 0xEDE3C8FF)
  intel.rect(x + 6, y + 6, x + w - 6, y + 24, 0x5A2020FF)
  intel.marker(x + w / 2, y + 2, "pin", 0xD02020FF, 4)
  intel.text(x + 10, y + 9, "GUARD FILE", 0xF0F0F0FF, 0.95)
  intel.text(x + 118, y + 10, string.format("%d/%d", #seen, #list), 0xE0C0C0FF, 0.75)
  if #seen == 0 then
    intel.text(x + 10, y + 30, "Nobody watched yet. Get a guard in sight, or on a camera feed, and his habits go on file here.", 0x202020FF, 0.72, 170)
    intel.text(x + 10, y + 84, "Rookies jump, veterans skip their rounds, some have a price (USE, face to face).", 0x8A1010FF, 0.68, 170)
    return
  end
  local max = 8
  for i = 1, math.min(#seen, max) do
    local e = seen[i]
    local up = spawn.alive(e.chr) and not spawn.down(e.chr)
    local note = e.bribed and "Paid off" or NOTE[e.trait] or e.trait
    local col = e.bribed and 0x206020FF or ((e.trait == "none") and 0x404040FF or 0x8A1010FF)
    intel.text(x + 10, y + 18 + i * 11, string.format("%s: %s%s", who(e.chr), note, up and "" or " (down)"), up and col or 0x808080FF, 0.66)
  end
  if #seen > max then intel.text(x + 10, y + 18 + (max + 1) * 11, string.format("+%d more", #seen - max), 0x404040FF, 0.66) end
  if paid > 0 then intel.text(x + 120, y + h - 12, string.format("paid off: %d", paid), 0x206020FF, 0.62) end
end

-- ---------------------------------------------------------------------------------------------------------------------------------
-- the payout and the DEBRIEF (heist_debrief.lua calls these; the bribes come off the payout in mode_heist.lua finish())

function T.cost()
  if not T.on then return 0 end
  return ai.trait_stats().bribe_cost or 0
end

function T.debrief(R, med)
  if not T.on then return end
  local st = ai.trait_stats()
  R.bribes, R.bribe_cost, R.clocked_out, R.skips, R.rookie_radios = st.bribes, st.bribe_cost, st.clocked, st.skips, st.radios
  R.observed = st.observed
  med[#med + 1] = { "Paid Off", "Bought a guard's blind eye", st.bribes > 0 }
  med[#med + 1] = { "Clocked Out", "Slipped through a veteran's skipped round", st.clocked > 0 }
  med[#med + 1] = { "Profiler", "Watched every guard on the file", st.observed > 0 and st.observed >= #ai.traits() }
end

function T.rows(R, rows)
  if not T.on then return end
  rows[#rows + 1] = { "Bribes", R.bribes > 0 and string.format("%d (%s)", R.bribes, money(R.bribe_cost)) or "none", R.bribes > 0 and 0xF0C080FF or nil }
end
