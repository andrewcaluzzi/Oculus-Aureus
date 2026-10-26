-- heist_randomiser.lua: the heist randomiser (designs/bank-heist.md section 25). Every run deals a different job from the sim RNG
-- (math.random: GE_SEED solo, the session seed in co-op and netplay), so the same seed always deals the same job. It works on the job
-- tables (tools/jobs/<job>.lua): it deals a COPY of the job, and the mode, the INTEL board and the first-play guide simply read the dealt
-- job, so none of them can show a stale carrier. Within the job's own definitions:
--   * who carries which card, and on which of his posts: the candidates are the job's `random.roles[KEY]` (each with its words), or,
--     for a job without them, the card's own `either` alternatives (the role and its pad still vary). Each card is dealt ONE carrier; a
--     card that is "one of two" on the board (`random.one_of_two`, GREEN on the bank) shows the carrier and a decoy role;
--   * which cameras are dummies (dead all run: the console and the power never bring them back);
--   * the time lock (how long the vault stays open, how long the crack takes) and the guard roster (a share either side of the count);
--   * which routes are open today: a gate welded shut (`random.seal`), a vent's fans running (`random.vents`: that vent is blocked);
--   * a "job of the day" twist: the generic ones, plus the job's own (`random.delivery`, `random.desk`).
-- Included by tools/mode_heist.lua at the top level. The mode, each guarded by `if H.rand`:
--   H.rand = heist_randomiser{ S, H, job }  at stage start, BEFORE the map loads (nil when S.randomise is off); the mode then uses
--                                          H.rand.job as its JOB
--   H.rand.doors()                         after setup_doors(): the sealed routes, the dummy cameras, the blocked vents
--   H.rand.tick(dt)                        every sim tick (the twist's timed events, the briefing line)
--   H.rand.state()                         appended to the mode's mode.state text (the netplay / co-op state hash)
--   H.rand.dead[cam]                       a dummy camera: the mode never re-enables it
-- Every choice is made here, at stage start, from math.random, in a fixed order over arrays (never pairs()); the choices are sim state
-- (they place chrs and lock doors), so R.state() puts them in the hash. The Lua VM is in the solo snapshot whole, so R restores with it.
-- Settings: S.randomise (true / false) and S.randomiser (a table, every key optional):
--   carriers = true, cameras = true, dummy_chance = 0.2, dummy_max = 8, timelock = true, vault_spread = 0.25, crack_spread = 0.15,
--   roster = true, roster_spread = 0.15, routes = true, seal_chance = 0.35, vent_chance = 0.25, twist = true (or a twist id to force),
--   green = / blue = / red = / gold = a role name to force that carrier (tests)

local KEYS = { GREEN = 0x2, BLUE = 0x4, RED = 0x8, GOLD = 0x10 }
local SEALED = 0x40000000                    -- the vault doors' flag: no card has it and the lockpicks refuse it (a welded gate)

-- the generic twists (any job); a job adds its own from random.delivery / random.desk. weight: draws in a bag
local TWISTS = {
  { id = "none", weight = 3, text = "An ordinary day." },
  { id = "short_staffed", weight = 1, text = "Flu going round: the guard roster is short" },
  { id = "inspection", weight = 1, text = "Head-office inspection: extra guards on the floor" },
  { id = "new_cams", weight = 1, text = "New cameras fitted this week: every camera is live" },
  { id = "timelock_fault", weight = 1, text = "Time-lock fault: the vault stays open longer" },
  { id = "generator", weight = 1, text = "The backup generator is in for service: 3 min to start" },
}

local function copy(t)
  if type(t) ~= "table" then return t end
  local o = {}
  for k, v in pairs(t) do o[k] = copy(v) end
  return o
end
local function the(role) return "the " .. role end

function heist_randomiser(ctx)
  local S, H, BASE = ctx.S, ctx.H, ctx.job
  local L = LEVEL
  if S.randomise ~= true or not BASE then return nil end
  local O = S.randomiser or {}
  local RJ = BASE.random or {}
  local function opt(k, d) if O[k] == nil then return d end return O[k] end
  local function log(fmt, ...) print(string.format("ge: heist: randomiser: " .. fmt, ...)) end
  local J = copy(BASE)                           -- the dealt job (the mode, the board and the guide read this one)
  local R = { job = J, cards = {}, order = {}, dead = {}, dead_list = {}, sealed = {}, vents_off = {}, dl = 0, dl_keys = {} }

  -- the settings the deal scales: from their values before the first deal (the script's S may outlive a stage)
  local B = S._rand_base
  if not B then
    B = { vault_time = S.vault_time, crack_time = S.crack_time, guards_post = S.guards_post, generator_after = S.generator_after }
    S._rand_base = B
  end
  for k, v in pairs(B) do S[k] = v end

  -- the carriers' pad lists: the job's, and its random.carriers (only the pads this layout has)
  for _, k in ipairs(RJ.carrier_order or {}) do
    local list = {}
    for _, n in ipairs(RJ.carriers[k] or {}) do if L.pads[n] then list[#list + 1] = n end end
    J.map.carriers[k] = list
  end
  local function pads_of(key)
    local out = {}
    for _, n in ipairs(J.map.carriers[key] or {}) do if L.pads[n] then out[#out + 1] = n end end
    return out
  end

  -- 1. the twist
  local twists = {}
  for _, t in ipairs(TWISTS) do twists[#twists + 1] = t end
  if RJ.delivery then twists[#twists + 1] = { id = "delivery", weight = 1, text = RJ.delivery.text } end
  if RJ.desk then twists[#twists + 1] = { id = "desk", weight = 1, text = RJ.desk.text } end
  local twist = twists[1]
  if type(opt("twist", true)) == "string" then
    for _, t in ipairs(twists) do if t.id == O.twist then twist = t end end
  elseif opt("twist", true) then
    local total = 0
    for _, t in ipairs(twists) do total = total + t.weight end
    local r = math.random(total)
    for _, t in ipairs(twists) do
      r = r - t.weight
      if r <= 0 then twist = t; break end
    end
  end
  R.twist = twist

  -- 2. the carriers, card by card in the job's order
  local base_role = {}
  for ci, card in ipairs(BASE.cards) do
    local key = card.key
    local list = {}
    local words = BASE.intel.keys[key] or {}
    if RJ.roles and RJ.roles[key] then
      for _, e in ipairs(RJ.roles[key]) do if #pads_of(e.pads) > 0 then list[#list + 1] = e end end
    else
      for _, d in ipairs(card.either or { card }) do
        local e = copy(d)
        e.where, e.how, e.short = e.where or words.where, e.how or words.how, e.short or e.area
        if #pads_of(e.pads) > 0 then list[#list + 1] = e end
      end
    end
    base_role[key] = card.role or (card.either and card.either[1].role)
    local e = list[1]
    local force = O[key:lower()]
    if force then for _, x in ipairs(list) do if x.role == force then e = x end end
    elseif opt("carriers", true) then e = list[math.random(#list)] end
    if twist.id == "desk" and key == RJ.desk.key and #pads_of(RJ.desk.pads) > 0 then e = RJ.desk end
    local c = copy(e)
    c.key, c.flag = key, KEYS[key]
    local pl = pads_of(c.pads)
    c.pad_name = pl[math.random(#pl)]
    c.pad = L.pads[c.pad_name]
    -- the dealt card: one carrier, on one pad (a one-pad list: the mode's own pick has one choice)
    local pk = "_dealt_" .. key
    J.map.carriers[pk] = { c.pad_name }
    J.cards[ci] = { key = key, role = c.role, kind = c.kind, pads = pk, body = c.body, body_setting = c.body_setting, health = c.health,
                    armour = c.armour, area = c.area }
    -- what the plan knows: "one of two" (the carrier and a decoy role, alphabetical), else the carrier by name
    c.roles_shown = { c.role }
    if (RJ.one_of_two or {})[key] and #list > 1 then
      local others = {}
      for _, x in ipairs(list) do if x.role ~= c.role then others[#others + 1] = x end end
      if #others > 0 then
        local d = others[math.random(#others)]
        c.decoy = { role = d.role, where = d.where, short = d.short, how = d.how, tip = d.tip, pads = d.pads }
        c.roles_shown = { c.role, d.role }
        table.sort(c.roles_shown)
      end
    end
    R.cards[key] = c
    R.order[#R.order + 1] = key
  end

  -- 3. the dummy cameras (a fixed index order)
  local cams = {}
  for _, o in pairs(L.objects) do if o.type == "camera" then cams[#cams + 1] = o.index end end
  table.sort(cams)
  if opt("cameras", true) and twist.id ~= "new_cams" then
    local chance, cap = opt("dummy_chance", 0.2), opt("dummy_max", 8)
    for _, i in ipairs(cams) do
      if math.random() < chance and #R.dead_list < cap then R.dead[i] = true; R.dead_list[#R.dead_list + 1] = i end
    end
  end
  R.ncams = #cams

  -- 4. the time lock and the roster
  local function spread(s) return 1 + (math.random() * 2 - 1) * s end
  if opt("timelock", true) then
    local vt = B.vault_time * spread(opt("vault_spread", 0.25))
    if twist.id == "timelock_fault" then vt = vt * 1.5 end
    S.vault_time = math.max(S.klaxon_at + 10, math.floor(vt + 0.5))
    S.crack_time = math.max(3, math.floor(B.crack_time * spread(opt("crack_spread", 0.15)) + 0.5))
  end
  if opt("roster", true) then
    local f = spread(opt("roster_spread", 0.15))
    if twist.id == "short_staffed" then f = f * 0.75 elseif twist.id == "inspection" then f = f * 1.2 end
    S.guards_post = math.floor(B.guards_post * f + 0.5)
  end
  if twist.id == "generator" and (B.generator_after or 0) > 0 then S.generator_after = 180 end

  -- 5. the routes
  if opt("routes", true) then
    for _, n in ipairs(RJ.seal or {}) do
      if L.objects[n] and math.random() < opt("seal_chance", 0.35) then R.sealed[#R.sealed + 1] = n end
    end
    for _, v in ipairs(RJ.vents or {}) do
      if L.pads["vent_" .. v .. "_a"] and math.random() < opt("vent_chance", 0.25) then R.vents_off[#R.vents_off + 1] = v end
    end
  end
  if twist.id == "desk" and RJ.desk.empty_area then            -- nobody in the office today
    for _, place in ipairs(J.map.civ_places or {}) do if place[1] == RJ.desk.empty_area then place[3] = 0 end end
  end

  -- the deal, in one line (the log, the state hash, the tests)
  local parts = {}
  for _, key in ipairs(R.order) do parts[#parts + 1] = string.format("%s=%s@%s", key, R.cards[key].role, R.cards[key].pad_name) end
  local dl = {}
  for _, i in ipairs(R.dead_list) do dl[#dl + 1] = tostring(i) end
  R.sig = string.format("%s twist=%s dummies=%s sealed=%s vents_off=%s vault=%d crack=%d posts=%d gen=%s", table.concat(parts, " "), twist.id,
    #dl > 0 and table.concat(dl, ",") or "-", #R.sealed > 0 and table.concat(R.sealed, ",") or "-",
    #R.vents_off > 0 and table.concat(R.vents_off, ",") or "-", S.vault_time, S.crack_time, S.guards_post, tostring(S.generator_after))
  log("%s: %s", BASE.name or "?", R.sig)
  R.role_order = {}
  for _, key in ipairs(R.order) do R.role_order[#R.role_order + 1] = R.cards[key].role end

  -- ---------------------------------------------------------------------------------------------------------------------------------
  -- the truth in words, written into the dealt job (presentation: the board, the guide and the HUD read them)

  local function who(c)
    if c.kind == "floor" then return c.who or ("nobody: " .. c.where) end
    if c.decoy then return the(c.roles_shown[1]) .. " OR " .. the(c.roles_shown[2]) end
    return the(c.role)
  end
  local function where(c)
    if c.decoy then return "one of two, the plan doesn't say which: " .. c.short .. " / " .. c.decoy.short end
    return c.where
  end
  local function how(c)
    if not c.decoy then return c.how end
    local a, b = c, c.decoy
    if b.role < a.role then a, b = b, a end
    return a.how .. ". " .. b.how .. "."
  end
  local function tip(c)                          -- the guide's line 3 (63 characters)
    if not c.decoy then return c.how end
    local a, b = c, c.decoy
    if b.role < a.role then a, b = b, a end
    return (a.tip or a.how) .. ". " .. (b.tip or b.how)
  end
  local function short_who(c)
    if c.decoy then return c.roles_shown[1] .. " or " .. c.roles_shown[2] end
    if c.kind == "floor" then return c.short_who or c.where end
    return the(c.role) .. " carries it"
  end
  local function name(c) return c.kind == "floor" and (c.label or c.role) or c.role end
  R.who = function(k) return who(R.cards[k]) end

  local JI, JG, JT = J.intel, J.guide, J.text
  for _, key in ipairs(R.order) do
    local c = R.cards[key]
    local k = JI.keys[key]
    if k then k.who, k.where, k.roles, k.how, k.short = who(c), where(c), c.roles_shown, how(c), short_who(c) end
  end
  for _, st in ipairs(JG.steps or {}) do
    local c = R.cards[st.key]
    if c then
      if c.decoy then st.what, st.role = "Next: " .. st.key .. " card - " .. c.roles_shown[1] .. " or " .. c.roles_shown[2], nil
      elseif c.kind == "floor" then st.what, st.role = "Next: " .. st.key .. " card - " .. c.where, nil
      else st.what, st.role, st.none = "Next: " .. st.key .. " card - the " .. c.role, c.role, " (" .. c.short .. ")" end
      st.how = tip(c)
    end
  end
  for _, key in ipairs({ "GREEN", "BLUE", "RED" }) do
    local c = R.cards[key]
    if c then
      JT["need_" .. key:lower()] = key .. " card: " .. (c.decoy and (c.roles_shown[1] .. " or " .. c.roles_shown[2])
                                                   or (c.kind == "floor" and c.where or the(c.role)))
    end
  end
  J.console_roles = R.role_order
  JI.posts, JI.next_posts = {}, {}
  for _, key in ipairs(R.order) do
    local c = R.cards[key]
    local pk = "_dealt_" .. key
    if c.decoy then
      JI.posts[#JI.posts + 1] = { pk, key, key .. "? " .. c.role:upper() }
      JI.posts[#JI.posts + 1] = { c.decoy.pads, key, key .. "? " .. c.decoy.role:upper() }
      JI.next_posts[key] = { pk, c.decoy.pads }
    else
      JI.posts[#JI.posts + 1] = { pk, key, key .. ": " .. name(c):upper() }
      JI.next_posts[key] = { pk }
    end
  end
  JI.operation = "TODAY: " .. twist.text
  if R.cards.BLUE then JI.console_hint = "The camera console needs BLUE (" .. short_who(R.cards.BLUE) .. ")" end

  function R.routes_text()
    local t = {}
    for _, n in ipairs(R.sealed) do t[#t + 1] = ((RJ.seal_words or {})[n] or n:gsub("_", " ")) .. " is welded shut" end
    if #R.vents_off > 0 then t[#t + 1] = "fans running in vent " .. table.concat(R.vents_off, ", ") end
    return #t > 0 and ("Routes: " .. table.concat(t, "; ") .. ".") or "Routes: every way in is open."
  end
  function R.cams_text()
    if #R.dead_list == 0 then return "Every camera is live." end
    return string.format("%d of %d cameras are dummies (grey on the map).", #R.dead_list, R.ncams)
  end
  JI.notes.board = "TODAY: " .. twist.text .. ". " .. R.routes_text() .. " " .. R.cams_text()

  -- THE PLAN: the keys step names today's carriers; the gold step today's GOLD carrier
  local base_plan = BASE.intel.plan
  JI.plan = function(S2, f)
    local steps = base_plan(S2, f)
    local g, b, r = R.cards.GREEN, R.cards.BLUE, R.cards.RED
    if g and b and r and steps[2] then
      steps[2][2] = string.format("GREEN (%s), then BLUE (%s, %s), then RED (%s). Aim at staff to rob them; knock guards out and search them.",
        g.decoy and (g.roles_shown[1] .. " or " .. g.roles_shown[2]) or name(g), name(b), b.short, name(r))
    end
    local au = R.cards.GOLD
    if au and steps[5] and base_role.GOLD and au.role ~= base_role.GOLD then
      local s = steps[5][2]
      local i, j = s:find(base_role.GOLD, 1, true)
      if i then steps[5][2] = s:sub(1, i - 1) .. au.role .. s:sub(j + 1) end
    end
    return steps
  end
  -- PERSONNEL: today's carriers and job in the first four dossiers (random.dossier_slots), GOLD's note in the vault guards' card
  local base_personnel = BASE.intel.personnel
  JI.personnel = function(S2, f)
    local t = base_personnel(S2, f)
    local slots = RJ.dossier_slots or { 1, 2, 3, 4 }
    local i = 1
    for _, key in ipairs({ "GREEN", "BLUE", "RED" }) do
      local c = R.cards[key]
      if c and slots[i] then
        if c.decoy then
          t[slots[i]] = { key .. ": ONE OF TWO", "The " .. c.roles_shown[1] .. " or the " .. c.roles_shown[2] .. " has it.", how(c),
                          "Watch the feeds to find out which." }
        elseif c.kind == "floor" then
          t[slots[i]] = { key .. ": " .. (c.label or c.role):upper(), c.where .. ".", c.how .. ".", c.note or "" }
        else
          t[slots[i]] = { key .. ": " .. c.role:upper(), c.where .. ".", c.how .. ".",
                          c.kind == "civ" and "A civilian: rob, don't kill." or "Judges your disguise like any guard." }
        end
        i = i + 1
      end
    end
    if slots[4] then t[slots[4]] = { "TODAY'S JOB", twist.text .. ".", R.routes_text(), R.cams_text() } end
    local au = R.cards.GOLD
    if au and RJ.vault_slot and t[RJ.vault_slot] and base_role.GOLD and au.role ~= base_role.GOLD then
      local v = t[RJ.vault_slot]
      t[RJ.vault_slot] = { v[1], v[2], v[3], "GOLD is not with them today: " .. the(au.role) .. ", " .. au.short .. "." }
    end
    return t
  end

  -- ---------------------------------------------------------------------------------------------------------------------------------
  -- the mode's hooks

  function R.doors()
    for _, i in ipairs(R.dead_list) do cctv.disable(i) end
    for _, n in ipairs(R.sealed) do
      local o = L.objects[n]
      door.close(o.index); door.set_keyflags(o.index, SEALED)
      if o.pair then door.close(o.pair); door.set_keyflags(o.pair, SEALED) end
    end
    for _, v in ipairs(R.vents_off) do if H.M and H.M.VENTS then H.M.VENTS[v] = nil end end   -- (the guide's vent prompt)
  end

  -- an interlock group over the delivery's doors (section 29: the sally port) opens one of them at a time, but the delivery leaves the
  -- port open: the group is cleared while the car is in (plain doors) and defined again once its doors are shut after the car has gone
  local function interlocks(on)
    local IL = HEIST_INTERLOCK
    for _, g in ipairs(IL and IL.list or {}) do
      local hit = false
      for _, gn in ipairs(g.names or {}) do for _, n in ipairs(RJ.delivery.doors) do if gn == n then hit = true end end end
      if hit then IL.enable(g.name, on) end
    end
  end
  local function delivery_shut()
    for _, n in ipairs(RJ.delivery.doors) do
      local o = L.objects[n]
      local d = o and door.get(o.index)
      if d and d.open > 0 then return false end
    end
    return true
  end
  local function delivery(open)
    if open then interlocks(false) end
    for _, n in ipairs(RJ.delivery.doors) do
      local o = L.objects[n]
      if o then
        -- (the keys come back as they were: the interlock's DOCK KEY on the outer roller, section 29, not the layout's)
        if open then R.dl_keys[n] = door.get(o.index).keyflags; door.set_keyflags(o.index, 0); door.open(o.index)
        else door.close(o.index); door.set_keyflags(o.index, R.dl_keys[n] or RJ.delivery.keyflags or 0) end
      end
    end
  end
  local told, vents_done = false, false
  function R.tick(dt)
    if not vents_done and GADGETS and GADGETS.vents and #GADGETS.vents > 0 then   -- (the gadgets load their vents after the mode's stage start)
      vents_done = true
      local keep = {}
      for _, v in ipairs(GADGETS.vents) do
        local off = false
        for _, id in ipairs(R.vents_off) do if v.id == id then off = true end end
        if not off then keep[#keep + 1] = v end
      end
      GADGETS.vents = keep
    end
    if not H.ready or H.ended then return end
    if not told and H.elapsed >= 9 then
      told = true
      H.flash, H.flash_until = ("TODAY: " .. twist.text):sub(1, 63), H.elapsed + 6
    end
    if twist.id == "delivery" then
      local Dl = RJ.delivery
      if R.dl == 0 and H.elapsed >= Dl.open_at then
        R.dl = 1; delivery(true)
        log("the delivery is in: %s open", table.concat(Dl.doors, ", "))
        H.flash, H.flash_until = Dl.open_text, H.elapsed + 5
      elseif R.dl == 1 and H.elapsed >= Dl.close_at then
        R.dl = 2; delivery(false)
        log("the delivery has gone: %s shut", table.concat(Dl.doors, ", "))
        H.flash, H.flash_until = Dl.close_text, H.elapsed + 4
      elseif R.dl == 2 and delivery_shut() then
        R.dl = 3; interlocks(true)
        log("the delivery's doors are shut: their interlock is back")
      end
    end
  end

  function R.state() return " rnd=" .. R.sig .. " dl=" .. R.dl end
  return R
end
