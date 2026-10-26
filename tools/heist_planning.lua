-- heist_planning.lua: the PLANNING phase before the heist clock starts (designs/bank-heist.md section 27). Included from the end of
-- tools/mode_heist.lua, before the gadgets (it sets the gadgets' loadout to nothing until the plan says otherwise); shipped as
-- Data/Scripts/modes/heist_planning.lua.
--
-- At the stage start the crew sits in the van with the planning board up: the INTEL board (port_intel.c's plan mode) showing ONLY
-- its three pages of its own (the five job pages - THE PLAN, SCHEMATICS, THE KEYS, PERSONNEL, NOTES - are hidden until the confirm;
-- after it the plan's tabs are gone and Tab / Back opens the job's five as ever):
--   LOADOUT  buy the crew's kit from a budget (the budget grows with the difficulty: a bigger job pays for a bigger prep)
--   ENTRY    the way in: the front door, the staff entrance (a forged badge), the roof (piton), a canal tunnel dig (costs time), the
--            sally port (only when today's job has a cash delivery: HEIST.job.sally or S.plan_sally)
--   CREW     extras: an inside contact (the GREEN card in your pocket), a power cut on a timer, a time-lock tamper
-- and a CONFIRM row on each. The heist clock waits (H.elapsed does not run), nobody moves (plan.hold: port_planning.c takes every
-- Bond's stick and buttons), the bank is placed but nothing of the heist's own runs (H.ready is held back). Confirm: the kit is given,
-- Bond is put at the entry, the extras are armed, the clock starts. The spend comes off the payout (finish(): H.plan_cost; the debrief
-- shows "planning costs $X").
--
-- Keys (the crew's synchronized buttons, so the plan is sim state: port_planning.c): tabs = the INTEL board's page keys (, and ., the
-- bumpers) or the stick / D-pad left and right; rows = the stick / D-pad up and down (W / S); FIRE (or A) buys / chooses; USE (or B)
-- sells; START or the CONFIRM row: go. While the board is up Bond takes no input at all (no move, look, fire, use: the board has it).
-- Mouse: a cursor; hover highlights a row or tab; left click buys / chooses (the CONFIRM row: go), right click sells / drops, a click
-- on a tab switches, the wheel moves the row. A click is local UI, but what it does reaches the plan as a session command
-- (PORT_NETCMD_PLAN_POINT -> plan.pointer), the same tick on both machines in netplay.
--
-- Co-op: one plan for the crew, made by Bond 1 (the host); the shared budget buys each item for every Bond; the partners' buttons are
-- held until the plan is confirmed. Netplay-safe: the plan is Lua sim state (the arena: the rollback snapshot) and rides in mode.state
-- (the state hash); the board is drawn from it and never read back.
--
-- On / off: S.planning (default true). Off in every automated run (GE_FRAMES, GE_PLAYERBOT, GE_INPUTPLAY) unless GE_PLANNING=1;
-- S.planning = false keeps today's start exactly (this file then returns at once and changes nothing).

local H = HEIST
local S = H.S
local function log(fmt, ...) print(string.format("ge: heist: plan: " .. fmt, ...)) end

local forced = mode.env_flag("GE_PLANNING")
local automated = mode.env_flag("GE_FRAMES") or mode.env_flag("GE_PLAYERBOT") or mode.env_flag("GE_INPUTPLAY")
if S.planning == false or (automated and not forced) or type(plan) ~= "table" then return end

-- ---------------------------------------------------------------------------------------------------------------------------------
-- the catalogue (prices are for the crew: every Bond gets one of each)

local ITEMS = {
  { id = "pp7",      name = "SILENCED PP7",    price = 15000, max = 1, give = "pp7sil",      desc = "The quiet pistol. Holstered, the guards never see it." },
  { id = "d5k",      name = "SUPPRESSED D5K",  price = 40000, max = 1, give = "mp5ksil",     desc = "A suppressed SMG for when it goes loud." },
  { id = "armour",   name = "BODY ARMOUR",     price = 35000, max = 1,                       desc = "A full vest under the suit." },
  { id = "ammo",     name = "SPARE MAGAZINES", price = 10000, max = 1,                       desc = "Every gun you carry, full." },
  { id = "piton",    name = "PITON GUN",       price = 40000, max = 1, give = "pitongun",    desc = "Up a facade, down the lift shaft (grapple points)." },
  { id = "lockpick", name = "LOCKPICKS",       price = 30000, max = 1, give = "gaskeyring",  desc = "Pick keyed doors; a stethoscope at the safes." },
  { id = "coins",    name = "THROWING COINS",  price = 5000,  max = 6, give = "creditcard",  desc = "A clink where you want a guard to look." },
  { id = "loops",    name = "TAPE LOOPS",      price = 20000, max = 3, give = "datathief",   desc = "One camera sees a recording for 60 s." },
  { id = "gas",      name = "SLEEP GAS",       price = 45000, max = 2, give = "bombdefuser", desc = "Everyone near where it lands: out cold." },
  { id = "thermal",  name = "THERMAL IMAGER",  price = 35000, max = 1, give = "camera",      desc = "Ten seconds of everyone through the walls." },
  { id = "disguise", name = "DISGUISE KIT",    price = 30000, max = 1,                       desc = "Change into a knocked-out guard's uniform." },
}
local ENTRIES = {
  { id = "front", name = "FRONT DOOR", price = 0,
    desc = "Walk in off the street as a customer. Your cover: bank staff, gun holstered. The guards see what they expect." },
  { id = "staff", name = "STAFF ENTRANCE", price = 60000, pad = "alley_staff", disguise = 1, door = "staff_outer", room = "staff_entry",
    desc = "A forged staff badge: the GREEN staff door in the alley opens for you, the mantrap counts as a staff room, staff rooms are yours." },
  { id = "roof", name = "THE ROOF", price = 25000, pad = "roof_w", needs = "piton",
    desc = "Dropped on the roof at night. The lantern lines rappel into the hall; the roof door needs BLUE. Needs the piton gun." },
  { id = "dig", name = "CANAL TUNNEL DIG", price = 20000, pad = "deep_1", time = 240,
    desc = "Dig in from the canal to the deep archive, under the vault, by the sewer vent. The dig costs 4:00 on the clock." },
  { id = "sally", name = "SALLY PORT", price = 90000, pad = "sally_in", disguise = 2, job = true,
    desc = "Ride in with the cash-in-transit van in the security firm's uniform. Only when today's job has a cash delivery." },
}
local EXTRAS = {
  { id = "contact", name = "INSIDE CONTACT", price = 80000,
    desc = "The GREEN card's carrier is on the payroll: the card is in Bond 1's pocket when the job starts." },
  { id = "power", name = "POWER CUT ON A TIMER", price = 100000, at = 120,
    desc = "A man at the substation drops the mains 2:00 in: no alarm, no cameras, until the generator kicks in." },
  { id = "timelock", name = "TIME-LOCK TAMPER", price = 70000, add = 30,
    desc = "The vault's time lock is fixed: the vault stays open 30 s longer." },
}
local BUDGET = { [0] = 300000, [1] = 350000, [2] = 400000, [3] = 450000 }   -- by mission.difficulty(); S.plan_budget overrides

-- ---------------------------------------------------------------------------------------------------------------------------------
-- the start the plan replaces: no kit until the plan is confirmed (the decoder, the vault's cracker, is always in the van)

local START_AMMO = S.start_ammo or 0
S.briefing = false                       -- (the planning board is the briefing)
S.start_weapons = { "doordecoder" }
S.start_ammo = 0
do
  local g = {}
  if type(S.gadgets) == "table" then for k, v in pairs(S.gadgets) do g[k] = v end end
  g.piton, g.lockpick, g.coins, g.loops, g.gas, g.thermal, g.disguise = false, false, 0, 0, 0, false, false
  S.gadgets = g
end

local UP, DOWN, LEFT, RIGHT, SELECT, BACK, START = 1, 2, 4, 8, 16, 32, 64
local P = { on = false, tab = 1, rows = {}, floor = 3, have = {}, entry = "front", extras = {}, spent = 0, budget = 0, msg = "", msg_bad = false,
            confirmed = false, ready = false, built = false, dirty = true, base = 0, power_at = nil, at = nil, ticks = 0 }
H.plan = P
PLANNING = P   -- (tests)
P.ITEMS, P.ENTRIES, P.EXTRAS = ITEMS, ENTRIES, EXTRAS
P.EV = { UP = UP, DOWN = DOWN, LEFT = LEFT, RIGHT = RIGHT, SELECT = SELECT, BACK = BACK, START = START }

local function money(v)
  local s, neg = tostring(math.floor(math.abs(v) + 0.5)), v < 0
  local out = s:reverse():gsub("(%d%d%d)", "%1,"):reverse():gsub("^,", "")
  return (neg and "-$" or "$") .. out
end
local function find(list, id) for i, x in ipairs(list) do if x.id == id then return x, i end end end
local function entry_open(e)
  if e.pad and not (LEVEL and LEVEL.pads and LEVEL.pads[e.pad]) then return false, "NOT ON THIS MAP" end
  -- (today's cash delivery is the randomiser's job of the day, the "delivery" twist: H.rand.twist; H.job was never set)
  if e.job and not (S.plan_sally or (H.rand and H.rand.twist and H.rand.twist.id == "delivery")) then return false, "NOT ON TODAY'S JOB" end
  return true
end

local function cost()
  local c = 0
  for _, it in ipairs(ITEMS) do c = c + (P.have[it.id] or 0) * it.price end
  c = c + (find(ENTRIES, P.entry) or ENTRIES[1]).price
  for _, x in ipairs(EXTRAS) do if P.extras[x.id] then c = c + x.price end end
  return c
end
P.cost = cost

local function say(text, bad) P.msg, P.msg_bad = text, bad and true or false end
local function afford(extra, what)
  if cost() + extra <= P.budget then return true end
  say(string.format("Over budget: %s costs %s, %s left", what, money(extra), money(P.budget - cost())), true)
  return false
end

-- the tabs: the job's pages (1 .. base), then LOADOUT, ENTRY, CREW
local function tab_kind(t) return t == P.base + 1 and "loadout" or (t == P.base + 2 and "entry" or (t == P.base + 3 and "crew" or "job")) end
local function list_of(kind) return kind == "loadout" and ITEMS or (kind == "entry" and ENTRIES or (kind == "crew" and EXTRAS or nil)) end
local function row_count(kind) local l = list_of(kind); return l and #l + 1 or 0 end   -- (+ the CONFIRM row)

local function buy(it)
  local n = P.have[it.id] or 0
  if it.max == 1 and n > 0 then return false end
  if n >= it.max then say(string.format("%s: %d is the most the van holds", it.name, it.max), true); return false end
  if not afford(it.price, it.name) then return false end
  P.have[it.id] = n + 1
  say(string.format("Bought %s%s", it.name, it.max > 1 and string.format(" (%d)", n + 1) or ""))
  return true
end
local function sell(it)
  local n = P.have[it.id] or 0
  if n <= 0 then return false end
  local e = find(ENTRIES, P.entry)
  if e and e.needs == it.id and n == 1 then say(string.format("%s needs the %s", e.name, it.name), true); return false end
  P.have[it.id] = n - 1
  say(string.format("Sold %s", it.name))
  return true
end
local function choose_entry(e)
  local ok, why = entry_open(e)
  if not ok then say(e.name .. ": " .. why:lower(), true); return false end
  if P.entry == e.id then return false end
  local old = find(ENTRIES, P.entry)
  local need = e.price - old.price
  local dep = e.needs and (P.have[e.needs] or 0) == 0 and find(ITEMS, e.needs) or nil
  if dep then need = need + dep.price end
  if cost() + need > P.budget then
    say(string.format("Over budget: %s%s costs %s more, %s left", e.name, dep and (" with the " .. dep.name) or "", money(need), money(P.budget - cost())), true)
    return false
  end
  if dep then P.have[dep.id] = 1 end
  P.entry = e.id
  say(string.format("Entry: %s%s", e.name, dep and (" (and the " .. dep.name .. ")") or ""))
  return true
end
local function toggle_extra(x)
  if P.extras[x.id] then P.extras[x.id] = nil; say("Dropped " .. x.name); return true end
  if not afford(x.price, x.name) then return false end
  P.extras[x.id] = true
  say("Hired: " .. x.name)
  return true
end

-- ---------------------------------------------------------------------------------------------------------------------------------
-- confirm: the plan becomes the start

local function warp_to(p, padname)
  local pad = LEVEL.pads[padname]
  if not pad then return false end
  return bond.warp(p, pad, (p % 2) * 60, (p // 2) * 60)
end

local function apply()
  local G = GADGETS
  local e = find(ENTRIES, P.entry) or ENTRIES[1]
  P.on, P.confirmed = false, true
  P.spent = cost()
  plan.hold(false)
  H.ready = P.ready
  H.phase = "casing"
  if not S.cover then H.hand = ((P.have.pp7 or 0) > 0 and "pp7sil") or ((P.have.d5k or 0) > 0 and "mp5ksil") or H.hand end   -- (no cover: drawn)
  for p = 0, bond.count() - 1 do
    for _, it in ipairs(ITEMS) do
      local n = P.have[it.id] or 0
      if n > 0 and it.give then bond.give(p, it.give) end
    end
    if (P.have.pp7 or 0) > 0 then bond.give_ammo(p, START_AMMO) end
    if (P.have.ammo or 0) > 0 then bond.give_ammo(p, 1) end
    if (P.have.armour or 0) > 0 then bond.heal(p, 0, 1) end
    local me = G and G.players[p]
    if me then me.coins, me.loops, me.gas = P.have.coins or 0, P.have.loops or 0, P.have.gas or 0 end
    if e.pad then warp_to(p, e.pad) end
    if e.disguise then gadget.disguise(p, e.disguise) end
    bond.equip(p, H.hand)
  end
  if G and G.T then
    G.T.piton, G.T.lockpick, G.T.thermal = (P.have.piton or 0) > 0, (P.have.lockpick or 0) > 0, (P.have.thermal or 0) > 0
    G.T.coins, G.T.loops, G.T.gas, G.T.disguise = P.have.coins or 0, P.have.loops or 0, P.have.gas or 0, (P.have.disguise or 0) > 0
  end
  if e.door and LEVEL.objects[e.door] then
    local o = LEVEL.objects[e.door]
    door.set_keyflags(o.index, 0)
    if o.pair then door.set_keyflags(o.pair, 0) end
  end
  if e.room and LEVEL.rooms[e.room] and S.cover then cover.room(LEVEL.rooms[e.room].id, "staff") end
  if e.time then H.elapsed = H.elapsed + e.time end
  if P.extras.contact then
    local c = H.carriers["head teller"] or H.carriers["lobby captain"]
    local flags = (c and c.chr and spawn.alive(c.chr)) and gadget.lift_key(0, c.chr) or 0
    log("inside contact: %s's card (0x%x) to Bond 1", c and (H.carriers["head teller"] and "the head teller" or "the lobby captain") or "nobody", flags)
  end
  if P.extras.power and H.M and H.M.P and H.M.P.breaker then P.power_at = H.elapsed + (find(EXTRAS, "power").at or 120) end
  if P.extras.timelock then S.vault_time = S.vault_time + (find(EXTRAS, "timelock").add or 30) end
  H.plan_cost = P.spent
  P.at = P.ticks
  plan.view(false)
  if S.intel == false then intel.enable(false) end
  local kit = {}
  for _, it in ipairs(ITEMS) do
    local n = P.have[it.id] or 0
    if n > 0 then kit[#kit + 1] = it.id .. (it.max > 1 and ("x" .. n) or "") end
  end
  local ex = {}
  for _, x in ipairs(EXTRAS) do if P.extras[x.id] then ex[#ex + 1] = x.id end end
  log("CONFIRMED at plan tick %d: entry %s, kit [%s], extras [%s], spent %d of %d, clock %.0f s", P.ticks, e.id, table.concat(kit, ","),
      table.concat(ex, ","), P.spent, P.budget, H.elapsed)
  for p = 0, bond.count() - 1 do
    H.pflash[p] = { string.format("THE PLAN IS ON: %s. %s spent.", e.name, money(P.spent)), H.elapsed + 6 }
  end
  P.dirty = true
end

-- ---------------------------------------------------------------------------------------------------------------------------------
-- the board (presentation: built from the plan's state, never read back)

local INK, RED, PAPER, MANILA, DIM = 0x202020FF, 0x9A1010FF, 0xF2EBD0EE, 0xE8D7A8FF, 0x7088A8FF

local function header(page, title, sub)
  plan.at(page)
  intel.rect(24, 38, 616, 72, MANILA)
  intel.frame(24, 38, 616, 72, 0x6B4A2CFF, 1)
  intel.text(32, 43, title, RED, 1.0, 0, 1)
  intel.text(32, 61, sub, INK, 0.7)
  local left = P.budget - cost()
  intel.text(380, 43, string.format("BUDGET %s", money(P.budget)), INK, 0.8)
  intel.text(380, 57, string.format("SPENT %s   LEFT %s", money(cost()), money(left)), left >= 0 and 0x106010FF or RED, 0.8)
end

local function hit(page, row, y0, y1)   -- (the mouse's hit box for that row: port_intel.c hit-tests the cursor against it)
  if plan.hit and not P.confirmed then plan.hit(page, row, 30, y0, 610, y1) end
end

local function row_box(y0, y1, cur, chosen, dim)
  intel.rect(30, y0, 610, y1, dim and 0xB8B098C0 or (cur and 0xFFF6C0FF or PAPER))
  if chosen then intel.rect(30, y0, 36, y1, 0x208020FF) end
  if cur and not P.confirmed then
    intel.frame(30, y0, 610, y1, RED, 2)
    intel.tri(18, y0 + 2, 18, y1 - 2, 27, (y0 + y1) / 2, RED)
  end
end

local function confirm_row(page, y0, cur, row)
  plan.at(page)
  hit(page, row, y0, y0 + 24)
  if P.confirmed then
    intel.rect(30, y0, 610, y0 + 24, 0x205020FF)
    intel.text(44, y0 + 6, string.format("CONFIRMED  -  THE PLAN IS ON  -  %s SPENT (OFF THE PAYOUT)", money(P.spent)), 0xF2EBD0FF, 0.9)
    return
  end
  intel.rect(30, y0, 610, y0 + 24, cur and 0xC02020FF or RED)
  if cur then intel.frame(30, y0, 610, y0 + 24, 0xFFE060FF, 2); intel.tri(18, y0 + 2, 18, y0 + 22, 27, y0 + 12, RED) end
  intel.text(44, y0 + 6, string.format("CONFIRM THE PLAN AND GO   (%s OFF THE PAYOUT)", money(cost())), 0xFFFFFFFF, 0.9)
end

local function footer_msg(page, y)
  plan.at(page)
  if P.msg ~= "" then intel.text(32, y, P.msg, P.msg_bad and 0xFF6060FF or 0xFFE060FF, 0.85) end
end

local function summary(page, y)
  plan.at(page)
  local kit = {}
  for _, it in ipairs(ITEMS) do
    local n = P.have[it.id] or 0
    if n > 0 then kit[#kit + 1] = it.name .. (it.max > 1 and (" x" .. n) or "") end
  end
  local ex = {}
  for _, x in ipairs(EXTRAS) do if P.extras[x.id] then ex[#ex + 1] = x.name end end
  intel.text(32, y, "KIT: " .. (#kit > 0 and table.concat(kit, ", ") or "nothing but the door decoder"), 0xD8E4F4FF, 0.72, 580)
  intel.text(32, y + 22, string.format("ENTRY: %s    EXTRAS: %s", (find(ENTRIES, P.entry) or ENTRIES[1]).name,
             #ex > 0 and table.concat(ex, ", ") or "none"), 0xD8E4F4FF, 0.72, 580)
end

local function draw_loadout(page)
  header(page, "LOADOUT", bond.count() > 1 and "Buy the crew's kit. Every Bond gets one of each." or "Buy the crew's kit from the budget.")
  local cur = (tab_kind(P.tab) == "loadout") and P.rows.loadout or 0
  for i, it in ipairs(ITEMS) do
    local y0 = 78 + (i - 1) * 25
    local n = P.have[it.id] or 0
    hit(page, i, y0, y0 + 22)
    row_box(y0, y0 + 22, cur == i, n > 0)
    intel.text(44, y0 + 6, it.name .. (it.max > 1 and string.format("  x%d", n) or ""), n > 0 and 0x106010FF or INK, 0.85)
    intel.text(206, y0 + 7, it.desc, 0x404040FF, 0.68)
    intel.text(530, y0 + 6, money(it.price) .. (it.max > 1 and " EA" or ""), INK, 0.8)
    if n > 0 and it.max == 1 then intel.marker(596, y0 + 11, "check", 0x208020FF, 6) end
  end
  confirm_row(page, 78 + #ITEMS * 25 + 2, cur == #ITEMS + 1, #ITEMS + 1)
  footer_msg(page, 386)
  summary(page, 404)
end

local function draw_entry(page)
  header(page, "ENTRY", "The way in sets where the crew starts and how they look.")
  local cur = (tab_kind(P.tab) == "entry") and P.rows.entry or 0
  for i, e in ipairs(ENTRIES) do
    local y0 = 78 + (i - 1) * 54
    local ok, why = entry_open(e)
    hit(page, i, y0, y0 + 50)
    row_box(y0, y0 + 50, cur == i, P.entry == e.id, not ok)
    intel.text(44, y0 + 6, e.name, ok and INK or 0x606060FF, 1.0, 0, 1)
    intel.text(250, y0 + 8, e.price > 0 and money(e.price) or "FREE", INK, 0.85)
    if e.time then intel.text(340, y0 + 8, string.format("+%d:%02d ON THE CLOCK", e.time // 60, e.time % 60), RED, 0.8) end
    if not ok then intel.text(420, y0 + 8, why, RED, 0.8) end
    if P.entry == e.id then intel.text(520, y0 + 8, "CHOSEN", 0x208020FF, 0.9) end
    intel.text(44, y0 + 26, e.desc, 0x404040FF, 0.72, 550)
  end
  confirm_row(page, 78 + #ENTRIES * 54 + 2, cur == #ENTRIES + 1, #ENTRIES + 1)
  footer_msg(page, 386)
  summary(page, 404)
end

local function draw_crew(page)
  header(page, "CREW EXTRAS", "Favours you can buy. Each one is paid for whether you need it or not.")
  local cur = (tab_kind(P.tab) == "crew") and P.rows.crew or 0
  for i, x in ipairs(EXTRAS) do
    local y0 = 78 + (i - 1) * 60
    hit(page, i, y0, y0 + 54)
    row_box(y0, y0 + 54, cur == i, P.extras[x.id])
    intel.text(44, y0 + 6, x.name, INK, 1.0, 0, 1)
    intel.text(330, y0 + 8, money(x.price), INK, 0.85)
    if P.extras[x.id] then intel.text(520, y0 + 8, "HIRED", 0x208020FF, 0.9) end
    intel.text(44, y0 + 28, x.desc, 0x404040FF, 0.72, 550)
  end
  confirm_row(page, 78 + #EXTRAS * 60 + 2, cur == #EXTRAS + 1, #EXTRAS + 1)
  footer_msg(page, 386)
  summary(page, 404)
end

local function footer_text()
  return string.format("%s LEFT  CLICK/FIRE: BUY  R-CLICK/USE: SELL  WHEEL W/S: ROWS  , . LB/RB: TABS  START: GO",
                       money(P.budget - cost()))
end

local function build()
  local b = P.base
  if b == 0 then intel.enable(true); intel.title("JANUS BANK, ST PETERSBURG  -  THE PLAN") end
  intel.page(b + 1, "LOADOUT"); intel.page(b + 2, "ENTRY"); intel.page(b + 3, "CREW")
  if plan.pages then plan.pages(b + 1, 3) end   -- (planning: only these three tabs; after the confirm only the job's pages)
  plan.clear()
  draw_loadout(b + 1)
  draw_entry(b + 2)
  draw_crew(b + 3)
  if P.on then plan.view(true, P.tab, P.floor, footer_text()) end
  P.dirty, P.built = false, true
end

-- ---------------------------------------------------------------------------------------------------------------------------------
-- the input (Bond 1's; the partners' are taken and dropped) and the tick

local function step(ev)
  local t = P.tab - P.base                    -- (only the plan's three tabs while planning: the job's pages come after the confirm)
  if t < 1 or t > 3 then t = 1 end
  if ev & LEFT ~= 0 then t = (t - 2) % 3 + 1; P.dirty = true end
  if ev & RIGHT ~= 0 then t = t % 3 + 1; P.dirty = true end
  P.tab = P.base + t
  local kind = tab_kind(P.tab)
  local n = row_count(kind)
  if n > 0 then
    local r = P.rows[kind] or 1
    if ev & UP ~= 0 then r = (r - 2) % n + 1; P.dirty = true end
    if ev & DOWN ~= 0 then r = r % n + 1; P.dirty = true end
    P.rows[kind] = r
    local list = list_of(kind)
    local x = list[r]
    if ev & SELECT ~= 0 then
      if x == nil then P.go = true
      elseif kind == "loadout" then if (P.have[x.id] or 0) > 0 and x.max == 1 then sell(x) else buy(x) end
      elseif kind == "entry" then choose_entry(x)
      else toggle_extra(x) end
      P.dirty = true
    end
    if ev & BACK ~= 0 and x and kind == "loadout" then sell(x); P.dirty = true end
    if ev & BACK ~= 0 and x and kind == "crew" and P.extras[x.id] then toggle_extra(x); P.dirty = true end
  elseif P.tab == 2 and P.base >= 2 then   -- SCHEMATICS: up / down the floors (B2 .. F3)
    if ev & UP ~= 0 then P.floor = math.min(6, P.floor + 1); P.dirty = true end
    if ev & DOWN ~= 0 then P.floor = math.max(1, P.floor - 1); P.dirty = true end
  end
  if ev & START ~= 0 then P.go = true end
end

-- a board click (plan.pointer: a session command, so sim input on every machine): a = page * 64 + row, b = 1 select, 2 back, 3 tab,
-- 4 / 5 the wheel (a row up / down on the shown tab)
local function point(a, b)
  local page, row = a // 64, a % 64
  if page < P.base + 1 or page > P.base + 3 then return end
  if page ~= P.tab then P.tab = page; P.dirty = true end
  if b == 4 then step(UP); return end
  if b == 5 then step(DOWN); return end
  local kind = tab_kind(P.tab)
  if row >= 1 and row <= row_count(kind) then
    if P.rows[kind] ~= row then P.rows[kind] = row; P.dirty = true end
    if b == 1 then step(SELECT) elseif b == 2 then step(BACK) end
  end
end

function P.tick(dt)
  if P.confirmed then
    if P.power_at and H.ready and H.elapsed >= P.power_at then
      P.power_at = nil
      if H.cut_power then H.cut_power("the planned cut") end
    end
    if P.dirty then build() end
    return false
  end
  if not P.on then return false end
  P.ticks = P.ticks + 1
  H.elapsed = H.elapsed - dt                  -- the clock waits
  if H.ready then P.ready = true; H.ready = false end
  H.phase = "planning"
  local ev = plan.input(0)                    -- (one Bond tick's presses at a time, in order)
  while ev ~= 0 do step(ev); ev = plan.input(0) end
  if plan.pointer then                        -- (then Bond 1's mouse clicks, in order)
    local a, b = plan.pointer(0)
    while a do point(a, b); a, b = plan.pointer(0) end
    for p = 1, bond.count() - 1 do while plan.pointer(p) do end end
  end
  for p = 1, bond.count() - 1 do while plan.input(p) ~= 0 do end end
  if P.go then
    P.go = false
    if P.ready then apply(); build(); return not H.ready end
    say("The crew is still getting into position - a moment", true)
    P.dirty = true
  end
  if P.dirty or not P.built then build() end
  return true
end

function P.state()
  local h = {}
  for _, it in ipairs(ITEMS) do h[#h + 1] = tostring(P.have[it.id] or 0) end
  local x = {}
  for _, e in ipairs(EXTRAS) do x[#x + 1] = P.extras[e.id] and "1" or "0" end
  return string.format(" PL[%s%s t%d r%d/%d/%d f%d e=%s h=%s x=%s sp=%d/%d pw=%s n=%d]", P.on and "on" or "off", P.confirmed and "C" or "",
    P.tab, P.rows.loadout or 1, P.rows.entry or 1, P.rows.crew or 1, P.floor, P.entry, table.concat(h), table.concat(x), cost(), P.budget,
    tostring(P.power_at), P.ticks)
end
do   -- (the plan rides in the heist's hashed state text: the gadgets' tick sends H.state_text() .. their own)
  local base = H.state_text
  H.state_text = function() return base() .. P.state() end
end

on("stage_start", function(stage)
  if H.phase == "off" then H.plan = nil; return end
  -- the ENTRY tab offers only the ways in this map has (their pads): the entries are the bank's, so the casino gets its front door
  for i = #ENTRIES, 1, -1 do
    local e = ENTRIES[i]
    if e.pad and not (LEVEL and LEVEL.pads and LEVEL.pads[e.pad]) then table.remove(ENTRIES, i) end
  end
  if not find(ENTRIES, P.entry) then P.entry = ENTRIES[1].id end
  P.base = (S.intel ~= false) and 5 or 0
  P.budget = S.plan_budget or BUDGET[mission.difficulty()] or BUDGET[1]
  P.tab = P.base + 1
  P.on = true
  P.msg = "The van is parked round the corner. Plan the job, then CONFIRM."
  plan.hold(true)
  for p = 0, bond.count() - 1 do          -- (the stage's own start gun: the plan sells the guns)
    for _, w in ipairs({ "pp7sil", "pp7" }) do plan.strip(p, w) end
    bond.equip(p, H.hand)
  end
  local ids = {}
  for _, e in ipairs(ENTRIES) do ids[#ids + 1] = e.id end
  log("planning: budget %d (difficulty %d), %d tabs, entries %s", P.budget, mission.difficulty(), P.base + 3, table.concat(ids, ","))
end)
