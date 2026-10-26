-- The heist DEBRIEF (designs/bank-heist.md section 26): an end-of-heist report that scores the run like a spy mission. Included at the
-- end of tools/mode_heist.lua (S.debrief = false leaves it out); mode_heist's finish() calls HEIST.debrief_end(win, msg, bonus, killed).
--
-- Through the run it keeps the job's numbers from the sim's own events and state (it only reads: nothing here is in mode.state, nothing
-- calls a sim API that changes anything, no math.random): time, loot, the alarm, times spotted (a cover guard saw through a Bond, a
-- camera timed one), radio reports, bodies found, cameras looped / blinded / the power cut, guards and staff knocked out, guards killed,
-- civilians hurt / killed, gadgets used, the route (the way in, the lines and vents, the vault, the exit) and how each card was got
-- (pickpocket, knock-out and search, intimidation, off a body, found). The handlers fire on every machine alike (they are sim events),
-- and the Lua arena is snapshotted, so a rollback replays them; but the numbers only feed the panel and the log line.
--
-- At the end: a rating (GHOST, PROFESSIONAL, COWBOY, CHAOS; BUSTED for a failed job), the crew payout (the mode's score), the stats
-- table, the commendations, a line of flavour, and a full-screen panel in the INTEL board's style (port/src/port_debrief.c: this
-- machine's view; use / INTEL / A / Start dismisses it). A machine-readable line "HEIST DEBRIEF {json}" goes to the log. The best result
-- per difficulty is kept in Data/Saves/heist_records.json next to the exe by the C side (debrief.record: the panel's NEW BEST and its
-- "HEIST RECORD {json}" line). The file is this machine's, and nothing it holds comes back to the script.

local H = HEIST
local S = H.S
local GREEN, BLUE, RED, GOLD, FLOORKEY = 0x2, 0x4, 0x8, 0x10, 0x1
local CARDS = { { GREEN, "GREEN" }, { BLUE, "BLUE" }, { RED, "RED" }, { GOLD, "GOLD" }, { FLOORKEY, "FLOOR KEY" } }
local DIFF_NAME = { [0] = "Agent", [1] = "Secret Agent", [2] = "00 Agent", [3] = "007" }
local SPEEDRUN = { [0] = 360, [1] = 420, [2] = 480, [3] = 540 }   -- seconds, by difficulty: the Speedrun commendation
local RATING_RGBA = { GHOST = 0x2A5E9AFF, PROFESSIONAL = 0x1E6A2EFF, COWBOY = 0xB0601AFF, CHAOS = 0xB01818FF, BUSTED = 0x505050FF }
local FLAVOUR = {
  GHOST = "Monday morning the manager opens an empty vault and blames the time lock. Nobody ever knew you were there.",
  PROFESSIONAL = "In and out. A few raised eyebrows, a guard with a headache, nothing the papers will print.",
  COWBOY = "Sirens, gunsmoke and a full van. It worked. M will want a word.",
  CHAOS = "Half of St Petersburg heard that one. The gold had better be worth the funerals.",
  BUSTED = "The crew never made the van. Q would like his gadgets back.",
}
-- the route's legs: the gadgets' line and vent ids (bank v3: section 21c, 22)
local LEG = { facade = "Facade to roof", lantern_w = "Lantern rappel", lantern_e = "Lantern rappel", gallery = "Gallery line",
              liftshaft = "Lift shaft", vent_records = "Records vent", vent_lounge = "Lounge vent", vent_server = "Server vent",
              vent_vault = "Vault ventilator", vent_sewer = "The dig", vent_plant = "Plant vent" }

local D                                   -- this run's numbers (reset at each stage start)
local function reset()
  D = { koed = {}, ko_guards = 0, ko_staff = 0, killed = 0, hurt = {}, civ_hurt = 0, spotted = 0, blown = 0, cam_spots = 0, bodies = {},
        bodies_found = 0, searched = {}, robbed = {}, keys = {}, pending = {}, route = {}, inside = false, in_vault = false, last_reel = {},
        consoles = 0, cam_off = false, lifts0 = 0, legs = {} }
end
reset()

local function is_bond(id)
  if id == CHR_BOND then return true end
  for p = 0, bond.count() - 1 do if bond.id(p) == id then return true end end
  return false
end
local function in_list(list, c) for _, e in ipairs(list) do if e.chr == c then return true end end return false end
local function money(v)
  local s = tostring(math.floor(v))
  local out = s:reverse():gsub("(%d%d%d)", "%1,"):reverse()
  if out:sub(1, 1) == "," then out = out:sub(2) end
  return "$" .. out
end
local function clock(t) t = math.floor(t or 0); return string.format("%d:%02d", t // 60, t % 60) end
local function leg(name)
  if D.route[#D.route] ~= name and #D.route < 9 then D.route[#D.route + 1] = name end
end
local function gstats() local G = rawget(_G, "GADGETS"); return G and G.stats or {} end

-- ------------------------------------------------------------------------------------------------------------------------------------
-- the events (read only)

on("stage_start", function() reset() end)

on("chr_knocked_out", function(c, p)
  if H.phase == "off" or H.ended or p == nil or p < 0 or D.koed[c] then return end
  D.koed[c] = true
  if in_list(H.civs, c) then D.ko_staff = D.ko_staff + 1 else D.ko_guards = D.ko_guards + 1 end
end)

on("chr_died", function(victim, killer)
  if H.phase == "off" or H.ended or is_bond(victim) or not is_bond(killer) or in_list(H.civs, victim) then return end
  D.killed = D.killed + 1
end)

on("damage", function(victim, attacker, amount)
  if H.phase == "off" or H.ended or D.hurt[victim] or not is_bond(attacker) or not in_list(H.civs, victim) then return end
  D.hurt[victim] = true
  D.civ_hurt = D.civ_hurt + 1
end)

on("cover_blown", function() if not H.ended then D.spotted = D.spotted + 1; D.blown = D.blown + 1 end end)
on("cctv_spotted", function() if not H.ended and H.ready then D.spotted = D.spotted + 1; D.cam_spots = D.cam_spots + 1 end end)
on("body_found", function(g, b)
  if H.ended or D.bodies[b] then return end
  D.bodies[b] = true
  D.bodies_found = D.bodies_found + 1
end)
on("chr_searched", function(c) D.searched[c] = true end)
on("civilian_robbed", function(c) D.robbed[c] = true end)

-- a card picked up: how it was got is settled at the next tick (a pickpocket's lift counts in the gadgets' stats after the pickup)
on("item_pickup", function(objtype, id, qty, p)
  if objtype ~= 4 or H.phase == "off" or H.ended or not H.ready then return end
  D.pending[#D.pending + 1] = { flags = id, p = p, lifts = gstats().lifts or 0, at = H.elapsed }
end)

local function how_got(k)
  if (gstats().lifts or 0) > k.lifts then return "pickpocket" end
  for _, c in pairs(H.carriers) do
    if c.chr and (c.flags & k.flags) ~= 0 then
      if H.traits and H.traits.bribed(c.chr) then return "bribe" end   -- (section 32)
      if D.searched[c.chr] then return "knock-out" end
      if D.robbed[c.chr] then return "intimidation" end
      if not spawn.alive(c.chr) then return "off a body" end
      return "picked up"
    end
  end
  return "found"
end

local function room_area(id)
  local nm = id and LEVEL.room_name and LEVEL.room_name[id]
  local r = nm and LEVEL.rooms[nm]
  return r and r.area or nil
end

on("tick", function()
  if H.phase == "off" or H.ended or not H.ready then return end
  for _, k in ipairs(D.pending) do
    for _, c in ipairs(CARDS) do
      if (k.flags & c[1]) ~= 0 and not D.keys[c[2]] then D.keys[c[2]] = { how = how_got(k), p = k.p, at = k.at } end
    end
  end
  D.pending = {}
  local G = rawget(_G, "GADGETS")
  for p = 0, bond.count() - 1 do
    -- the lines and vents (the gadgets' reel ids)
    local me = G and G.players and G.players[p]
    local r = me and me.reel
    if r and r.id and D.last_reel[p] ~= r then
      D.last_reel[p] = r
      local name = LEG[r.id] or (r.vent and "Vent" or "Piton line")
      leg(name)
      D.legs[r.id] = (D.legs[r.id] or 0) + 1
    elseif not r then
      D.last_reel[p] = nil
    end
    -- the way in: the first room off the street
    if not bond.dead(p) then
      local a = room_area(bond.room(p))
      if not D.inside and a and a ~= "street" then
        D.inside = true
        if #D.route == 0 then leg(a == "hall" and "Front door" or ("Side way in (" .. ((H.M and H.M.AREA_LABEL and H.M.AREA_LABEL[a]) or a) .. ")")) end
      end
      if not D.in_vault and H.in_vault(p) then D.in_vault = true; leg("Vault") end
    end
  end
  -- the security console (the cameras off for cam_off_time)
  local off = H.cam_off_until ~= nil
  if off and not D.cam_off then D.consoles = D.consoles + 1 end
  D.cam_off = off
end)

-- ------------------------------------------------------------------------------------------------------------------------------------
-- a tiny JSON encoder (the log line)

local function jstr(s) return '"' .. tostring(s):gsub('[%c"\\]', function(c) return string.format("\\u%04x", c:byte()) end) .. '"' end
local function jenc(v)
  local t = type(v)
  if t == "table" then
    if #v > 0 or next(v) == nil then
      local out = {}
      for i = 1, #v do out[i] = jenc(v[i]) end
      return "[" .. table.concat(out, ",") .. "]"
    end
    local keys, val = {}, {}
    for k, x in pairs(v) do keys[#keys + 1] = tostring(k); val[tostring(k)] = x end
    table.sort(keys)
    local out = {}
    for _, k in ipairs(keys) do out[#out + 1] = jstr(k) .. ":" .. jenc(val[k]) end
    return "{" .. table.concat(out, ",") .. "}"
  elseif t == "string" then return jstr(v)
  elseif t == "boolean" then return v and "true" or "false"
  elseif t == "number" then
    if v == math.floor(v) and math.abs(v) < 2 ^ 53 then return string.format("%d", math.floor(v)) end
    return string.format("%.2f", v)
  end
  return "null"
end


-- ------------------------------------------------------------------------------------------------------------------------------------
-- the end

local function exit_name()
  for p = 0, bond.count() - 1 do
    if not bond.dead(p) then
      if zone.contains(H.zones.exit_a, bond.id(p)) then return "Van" end
      if zone.contains(H.zones.exit_b, bond.id(p)) then return "Loading dock" end
    end
  end
  return nil
end

local function rate(win, R)
  if not win then return "BUSTED" end
  local clean = R.civ_killed == 0
  if not R.alarm and R.spotted == 0 and R.bodies_found == 0 and R.guards_killed == 0 and clean and R.civ_hurt == 0 then return "GHOST" end
  if not R.alarm and R.guards_killed <= 2 and clean then return "PROFESSIONAL" end
  if clean and R.guards_killed < 15 then return "COWBOY" end
  return "CHAOS"
end

local function card_rows(rows)
  for _, c in ipairs(CARDS) do
    local k = D.keys[c[2]]
    if k then rows[#rows + 1] = { c[2] .. " card", k.how .. (bond.count() > 1 and string.format(" (B%d)", k.p + 1) or "") }
    elseif c[1] ~= FLOORKEY then rows[#rows + 1] = { c[2] .. " card", "-", 0x7088A8FF } end
  end
end

function H.debrief_end(win, msg, bonus, killed)
  local gs = gstats()
  local dif = mission.difficulty()
  local exit = win and exit_name() or nil
  if exit then leg(exit) end
  local gadgets = (gs.grapples or 0) + (gs.picks or 0) + (gs.lifts or 0) + (gs.coins or 0) + (gs.loops or 0) + (gs.disguises or 0)
                  + (gs.vents or 0) + (gs.safes or 0) + (gs.thermal or 0) + ((gs.gassed or 0) > 0 and 1 or 0)
  local R = {
    result = win and "escaped" or "failed", why = msg, difficulty = dif, crew = bond.count(), time = math.floor(H.elapsed * 100 + 0.5) / 100,
    payout = H.score or 0, loot = H.loot, bars = H.bars, cash = H.cash, quiet_bonus = bonus or 0, fines = (killed or 0) * S.civ_fine, planning = H.plan_cost or 0,
    alarm = H.hot and true or false, alarm_at = H.hot_at and math.floor(H.hot_at) or nil, alarms = H.hot and 1 or 0,
    spotted = D.spotted, cover_blown = D.blown, camera_spots = D.cam_spots, reports = H.reports or 0, bodies_found = D.bodies_found,
    cams_looped = gs.loops or 0, console = D.consoles, power_cuts = H.power and H.power.cuts or 0,
    guards_ko = D.ko_guards, staff_ko = D.ko_staff, guards_killed = D.killed, civ_hurt = D.civ_hurt, civ_killed = killed or civilian.killed(),
    gadgets = gadgets, picks = gs.picks or 0, lifts = gs.lifts or 0, safes = gs.safes or 0, grapples = gs.grapples or 0, vents = gs.vents or 0,
    disguises = gs.disguises or 0, coins = gs.coins or 0, route = D.route, exit = exit, keys = {},
  }
  for name, k in pairs(D.keys) do R.keys[name] = k.how end
  R.rating = rate(win, R)
  local speed = SPEEDRUN[dif] or 420
  local med = {
    { "Never Seen", "No guard or camera made you", win and R.spotted == 0 and not R.alarm },
    { "No Bodies", "Nobody died", win and R.guards_killed == 0 and R.civ_killed == 0 },
    { "Quiet Job", "The alarm never sounded", win and not R.alarm },
    { "Pickpocket", "Lifted a card from a pocket", R.lifts > 0 },
    { "Tumbler", "Cracked a safe by stethoscope", R.safes > 0 },
    { "Spider", "Went up or down a piton line", R.grapples > 0 },
    { "Rat Run", "Crawled through a vent", R.vents > 0 },
    { "Locksmith", "Picked a lock", R.picks > 0 },
    { "Quick Change", "Wore a borrowed uniform", R.disguises > 0 },
    { "Lights Out", "Cut the bank's power", R.power_cuts > 0 },
    { "Blind Spot", "Looped or blinded the cameras", R.cams_looped > 0 or R.console > 0 },
    { "Speedrun", "Out in under " .. clock(speed) .. " on " .. (DIFF_NAME[dif] or "?"), win and R.time <= speed },
    { "Full Vault", "Every bar in the vault", win and R.bars >= S.vault_bars },
    { "Clean Hands", "No civilian hurt", win and R.civ_hurt == 0 and R.civ_killed == 0 },
    { "Sandman", "Five knock-outs, no kills", R.guards_ko + R.staff_ko >= 5 and R.guards_killed == 0 and R.civ_killed == 0 },
  }
  if H.traits and H.traits.debrief then H.traits.debrief(R, med) end   -- (section 32: bribes, a veteran's skipped round)
  R.commendations = {}
  for _, m in ipairs(med) do if m[3] then R.commendations[#R.commendations + 1] = m[1] end end
  local line = "HEIST DEBRIEF " .. jenc(R)
  if debrief and debrief.log then debrief.log(line) else print(line) end   -- (print() stops at 511 characters)

  -- the panel
  local rows = {
    { "Result", win and ("ESCAPED" .. (exit and (" (" .. exit:upper() .. ")") or "")) or "FAILED", win and 0x80F080FF or 0xF08080FF },
    { "Time", clock(R.time) },
    { "Loot", string.format("%d bars, %d cash", R.bars, R.cash) },
    { "Alarm", R.alarm and ("raised at " .. clock(R.alarm_at)) or "never", R.alarm and 0xF08080FF or 0x80F080FF },
    { "Times spotted", string.format("%d", R.spotted) .. (R.spotted > 0 and string.format(" (%d guard, %d camera)", R.cover_blown, R.camera_spots) or "") },
    { "Radio reports", tostring(R.reports) },
    { "Bodies found", tostring(R.bodies_found) },
    { "Cameras", string.format("%d looped, console %d, power %d", R.cams_looped, R.console, R.power_cuts) },
    { "Knocked out", string.format("%d guards, %d staff", R.guards_ko, R.staff_ko) },
    { "Guards killed", tostring(R.guards_killed), R.guards_killed > 0 and 0xF0C080FF or nil },
    { "Civilians hurt / killed", string.format("%d / %d", R.civ_hurt, R.civ_killed), (R.civ_hurt + R.civ_killed) > 0 and 0xF08080FF or nil },
    { "Gadgets used", tostring(R.gadgets) },
    { "Locks picked", tostring(R.picks) },
    { "Route", #R.route > 0 and table.concat(R.route, " > ") or "-" },
  }
  card_rows(rows)
  if H.traits and H.traits.rows then H.traits.rows(R, rows) end   -- (section 32)
  local medals = {}
  for _, m in ipairs(med) do medals[#medals + 1] = { m[1]:upper(), m[2], m[3] and true or false } end
  local dn = DIFF_NAME[dif] or "?"
  local note = string.format("Loot %s + quiet bonus %s - fines %s", money(R.loot), money(R.quiet_bonus), money(R.fines))
  if R.planning > 0 then note = note .. string.format(" - planning costs %s", money(R.planning)) end   -- (section 27)
  if (R.bribe_cost or 0) > 0 then note = note .. string.format(" - bribes %s", money(R.bribe_cost)) end   -- (section 32)
  if R.crew > 1 and win then note = note .. string.format(".  %s a head", money(R.payout // R.crew)) end
  if debrief and debrief.set then
    debrief.set{
      kicker = win and "HEIST COMPLETE  -  THE CREW GOT OUT" or ("HEIST FAILED  -  " .. tostring(msg):upper()),
      rating = R.rating, rating_rgba = RATING_RGBA[R.rating], flavour = FLAVOUR[R.rating],
      payout = money(R.payout), payout_note = note,
      title = string.format("JANUS BANK  -  %s  -  %s", dn:upper(), clock(R.time)),
      rows = rows, medals = medals,
    }
    -- the best per difficulty (Data/Saves/heist_records.json): kept in C, this machine's; it fills the NEW BEST stamp and line itself
    -- and logs "HEIST RECORD {json}". Nothing of the file comes back here (the Lua arena is sim state).
    debrief.record{ difficulty = dif, win = win and true or false, payout = R.payout, time = R.time, rating = R.rating, result = R.result }
    debrief.show(true)
  end
  return R
end
