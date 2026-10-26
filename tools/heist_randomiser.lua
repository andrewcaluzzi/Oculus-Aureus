-- heist_randomiser.lua: the heist randomiser (designs/bank-heist.md section 25). Every run deals a different bank from the sim RNG
-- (math.random: GE_SEED solo, the session seed in co-op and netplay), so the same seed always deals the same bank. Within real-bank rules:
--   * who carries which key card, and where they stand (GREEN: the head teller, the lobby captain, the assistant manager on the officers'
--     platform or the records clerk; BLUE: the security chief, the duty sergeant on his break or the staff-entrance guard; RED: the branch
--     manager, the duty officer or the chief cashier; GOLD: the vault captain or the head counter). Each carrier is reachable with the
--     cards before his own (checked against bank v3's doors: designs/bank-heist.md section 25);
--   * which cameras are dummies (dead all run: the console and the power never bring them back);
--   * the time lock (how long the vault stays open, how long the crack takes) and the guard roster (a share either side of the
--     difficulty's count);
--   * which routes are open today: the goods-lift gate welded shut, a vent's fans running (that vent is blocked);
--   * a "job of the day" twist (an armoured-car delivery opens the sally port, the manager is out and RED is in his desk, ...).
-- Included by tools/mode_heist.lua at the top level (include("heist_randomiser")). The mode calls, each guarded by `if H.rand`:
--   H.rand = heist_randomiser(ctx)    at stage start, after the map loads and before anything is placed (nil when S.randomise is off)
--   H.rand.doors()                    after setup_doors(): the sealed routes, the dummy cameras
--   H.rand.carriers(cx)               the spawn job that places the carriers instead of queue_carriers' own (cx: the mode's spawn helpers)
--   H.rand.tick(dt)                   every sim tick (the twist's timed events, the briefing line)
--   H.rand.state()                    appended to the mode's mode.state text (the netplay / co-op state hash)
--   H.rand.dead[cam]                  a dummy camera: the mode never re-enables it
-- The INTEL board (tools/heist_intel.lua, ctx.rand) and the first-play guide (tools/heist_guide.lua, H.rand) read the truth from R.cards
-- and the R.* text helpers, so neither shows a stale carrier. Every choice is made here, at stage start, from math.random, in a fixed order
-- over arrays (never pairs()); the choices are sim state (they place chrs and lock doors), so R.state() puts them in the hash. The Lua VM
-- is in the solo snapshot whole, so R restores with it.
-- Settings: S.randomise (true / false) and S.randomiser (a table, every key optional):
--   carriers = true, cameras = true, dummy_chance = 0.2, dummy_max = 8, timelock = true, vault_spread = 0.25, crack_spread = 0.15,
--   roster = true, roster_spread = 0.15, routes = true, lift_chance = 0.35, vent_chance = 0.25, twist = true (or a twist id to force),
--   green = / blue = / red = / gold = a role name to force that carrier (tests)

local GREEN, BLUE, RED, GOLD = 0x2, 0x4, 0x8, 0x10
local SEALED = 0x40000000                    -- the vault doors' flag: no card has it and the lockpicks refuse it (a welded gate)
local ORDER = { GREEN, BLUE, RED, GOLD }
local CARD_NAME = { [GREEN] = "GREEN", [BLUE] = "BLUE", [RED] = "RED", [GOLD] = "GOLD" }

-- the candidates per card. kind: "civ" (rob at gunpoint), "guard" (KO and search), "floor" (a key on a pad). pads: the layout's names,
-- tried in order of the draw; a role whose pads the layout lacks is left out. body "@manager" / "@chief": S.manager_body / S.chief_body.
local ROLES = {
  [GREEN] = {
    { role = "head teller", kind = "civ", area = "hall", pads = { "teller_1", "teller_2", "teller_3" }, body = "Civilian_1_Female",
      where = "behind the counter (hall)", short = "counter", how = "Teller: aim a gun at her till she hands it over", tip = "Teller: aim a gun at her" },
    { role = "lobby captain", kind = "guard", area = "hall", pads = { "hall_mid", "hall_post_sw", "hall_post_se" }, body = "St_Petersburg_Guard",
      where = "the hall floor", short = "hall", how = "Captain: slap him twice, USE to search", tip = "Captain: slap x2, USE to search" },
    { role = "assistant manager", kind = "civ", area = "hall", pads = { "officer_desk", "platform_1" }, body = "Civilian_2",
      where = "the officers' platform (hall)", short = "platform", how = "Asst manager: aim a gun at him till he hands it over", tip = "Asst manager: aim at him" },
    { role = "records clerk", kind = "civ", area = "records", pads = { "rec_1", "rec_2", "rec_3" }, body = "Civilian_1_Female",
      where = "records (ground floor, staff)", short = "records", how = "Clerk: aim a gun at her till she hands it over", tip = "Clerk: aim a gun at her" },
  },
  [BLUE] = {
    { role = "security chief", kind = "guard", area = "offices", pads = { "office_a_1", "office_a_mid", "office_b_1", "ocorr_mid" }, body = "@chief",
      health = 1.5, where = "the offices behind GREEN", short = "offices", how = "Catch him alone: slap twice (he's tough), then USE to search" },
    { role = "duty sergeant", kind = "guard", area = "corridor", pads = { "canteen_1", "canteen_2", "lounge_1" }, body = "Russian_Soldier",
      health = 1.25, where = "on his break: the canteen or the staff lounge", short = "canteen", how = "On his break: slap him twice, then USE to search" },
    { role = "entrance guard", kind = "guard", area = "security", pads = { "entry_post" }, body = "St_Petersburg_Guard",
      where = "the staff entrance (off the dock)", short = "staff entrance", how = "At the staff entrance: slap him twice, USE to search" },
  },
  [RED] = {
    { role = "branch manager", kind = "civ", area = "manager", pads = { "mgr_desk", "mgr_1" }, body = "@manager",
      where = "his own office (behind BLUE)", short = "manager", how = "Aim a gun at him and he hands it over (BLUE opens the offices)" },
    { role = "duty officer", kind = "guard", area = "security", pads = { "sec_mid", "sec_2" }, body = "Russian_Commandant", health = 1.5,
      where = "the security office (behind BLUE)", short = "security", how = "Tough (x1.5): slap him twice out of sight, USE to search" },
    { role = "chief cashier", kind = "civ", area = "security", pads = { "cash_1", "cash_2" }, body = "Civilian_4",
      where = "the cash office (behind BLUE, by the security room)", short = "cash office", how = "Aim a gun at her and she hands it over" },
  },
  [GOLD] = {
    { role = "vault captain", kind = "guard", area = "vault", pads = { "vault_captain", "vault_g3" }, body = "Russian_Commandant", health = 2, armour = 1,
      where = "inside the vault (armoured)", short = "vault", how = "In the vault with the guards: only once it's open. Armoured, double health." },
    { role = "head counter", kind = "civ", area = "vault_ante", pads = { "count_1", "count_2" }, body = "Civilian_3",
      where = "the counting room (vault level, behind RED)", short = "counting room", how = "A civilian in the counting room: aim at him, he hands it over" },
  },
}
-- the twist "the manager is out": RED lies in his desk (no carrier)
local DESK = { role = "manager's desk", kind = "floor", area = "manager", pads = { "mgr_desk", "mgr_1" }, where = "the manager's office (behind BLUE): in his desk",
               short = "manager", how = "Nobody to rob: walk over the card on his desk" }

-- the twists (the job of the day). weight: draws in a bag; apply(R) changes the deal. Text: line 1 on the board and the HUD (63 chars max).
local TWISTS = {
  { id = "none", weight = 3, text = "An ordinary day at the bank." },
  { id = "delivery", weight = 1, text = "Armoured-car delivery 1:00-3:00: the sally port is open" },
  { id = "manager_out", weight = 1, text = "The manager is out: RED is in his desk" },
  { id = "short_staffed", weight = 1, text = "Flu going round: the guard roster is short" },
  { id = "inspection", weight = 1, text = "Head-office inspection: extra guards on the floor" },
  { id = "new_cams", weight = 1, text = "New cameras fitted this week: every camera is live" },
  { id = "timelock_fault", weight = 1, text = "Time-lock fault: the vault stays open longer" },
  { id = "generator", weight = 1, text = "The backup generator is in for service: 3 min to start" },
}
local VENT_IDS = { "records", "lounge", "server", "plant", "sewer" }   -- (not the vault ventilator: the power rules it)

local ROLE_TIP = {}
for _, e in ipairs(ROLES[GREEN]) do ROLE_TIP[e.role] = e.tip end
local function the(role) return "the " .. role end

function heist_randomiser(ctx)
  local S, H, M = ctx.S, ctx.H, ctx.M
  local L = LEVEL
  if S.randomise ~= true then return nil end
  local O = S.randomiser or {}
  local function opt(k, d) if O[k] == nil then return d end return O[k] end
  local function log(fmt, ...) print(string.format("ge: heist: randomiser: " .. fmt, ...)) end
  local R = { cards = {}, dead = {}, dead_list = {}, sealed = {}, vents_off = {}, dl = 0 }

  -- the settings the deal scales: from their values before the first deal (the script's S may outlive a stage)
  local B = S._rand_base
  if not B then
    B = { vault_time = S.vault_time, crack_time = S.crack_time, guards_post = S.guards_post, generator_after = S.generator_after }
    S._rand_base = B
  end
  for k, v in pairs(B) do S[k] = v end

  -- 1. the twist
  local twist = TWISTS[1]
  if type(opt("twist", true)) == "string" then
    for _, t in ipairs(TWISTS) do if t.id == O.twist then twist = t end end
  elseif opt("twist", true) then
    local total = 0
    for _, t in ipairs(TWISTS) do total = total + t.weight end
    local r = math.random(total)
    for _, t in ipairs(TWISTS) do
      r = r - t.weight
      if r <= 0 then twist = t; break end
    end
  end
  R.twist = twist

  -- 2. the carriers
  local function have_pads(e)
    local ids = {}
    for _, n in ipairs(e.pads) do if L.pads[n] then ids[#ids + 1] = { n, L.pads[n] } end end
    return ids
  end
  local function deal(flag, e)
    local ids = have_pads(e)
    local p = ids[math.random(#ids)]
    local c = {}
    for k, v in pairs(e) do c[k] = v end
    c.flag, c.pad_name, c.pad = flag, p[1], p[2]
    if c.body == "@manager" then c.body = S.manager_body elseif c.body == "@chief" then c.body = S.chief_body end
    return c
  end
  for _, flag in ipairs(ORDER) do
    local list = {}
    for _, e in ipairs(ROLES[flag]) do if #have_pads(e) > 0 then list[#list + 1] = e end end
    local e = list[1]
    local force = O[CARD_NAME[flag]:lower()]
    if force then for _, x in ipairs(list) do if x.role == force then e = x end end
    elseif opt("carriers", true) then e = list[math.random(#list)] end
    if flag == RED and twist.id == "manager_out" and #have_pads(DESK) > 0 then e = DESK end
    local c = deal(flag, e)
    -- what the plan knows: GREEN is "one of two" (as the fixed bank's), the rest by name
    c.roles_shown = { c.role }
    if flag == GREEN and #list > 1 then
      local others = {}
      for _, x in ipairs(list) do if x.role ~= c.role then others[#others + 1] = x end end
      local d = others[math.random(#others)]
      c.decoy = { role = d.role, where = d.where, short = d.short, how = d.how, pad = L.pads[d.pads[1]] }
      c.roles_shown = { c.role, d.role }
      table.sort(c.roles_shown)
    end
    R.cards[flag] = c
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
    if L.objects.lift_gate and math.random() < opt("lift_chance", 0.35) then R.sealed[#R.sealed + 1] = "lift_gate" end
    for _, v in ipairs(VENT_IDS) do
      if L.pads["vent_" .. v .. "_a"] and math.random() < opt("vent_chance", 0.25) then R.vents_off[#R.vents_off + 1] = v end
    end
  end
  for _, v in ipairs(R.vents_off) do if M.VENTS then M.VENTS[v] = nil end end      -- (the guide's vent prompt)
  if twist.id == "manager_out" then                                                 -- nobody in the manager's office today
    for _, place in ipairs(M.CIV_PLACES or {}) do if place[1] == "manager" then place[3] = 0 end end
  end

  -- the deal, in one line (the log, the state hash, the tests)
  local parts = {}
  for _, flag in ipairs(ORDER) do
    local c = R.cards[flag]
    parts[#parts + 1] = string.format("%s=%s@%s", CARD_NAME[flag], c.role, c.pad_name)
  end
  local dl = {}
  for _, i in ipairs(R.dead_list) do dl[#dl + 1] = tostring(i) end
  R.sig = string.format("%s twist=%s dummies=%s sealed=%s vents_off=%s vault=%d crack=%d posts=%d gen=%s", table.concat(parts, " "), twist.id,
    #dl > 0 and table.concat(dl, ",") or "-", #R.sealed > 0 and table.concat(R.sealed, ",") or "-",
    #R.vents_off > 0 and table.concat(R.vents_off, ",") or "-", S.vault_time, S.crack_time, S.guards_post, tostring(S.generator_after))
  log("%s", R.sig)
  R.role_order = {}
  for _, flag in ipairs(ORDER) do R.role_order[#R.role_order + 1] = R.cards[flag].role end

  -- ---------------------------------------------------------------------------------------------------------------------------------
  -- the mode's hooks

  function R.doors()
    for _, i in ipairs(R.dead_list) do cctv.disable(i) end
    for _, n in ipairs(R.sealed) do
      local o = L.objects[n]
      door.close(o.index); door.set_keyflags(o.index, SEALED)
      if o.pair then door.close(o.pair); door.set_keyflags(o.pair, SEALED) end
    end
  end

  function R.carriers(cx)
    for _, flag in ipairs(ORDER) do
      local e = R.cards[flag]
      local c
      if e.kind == "floor" then
        local h = spawn.key_at(e.pad, flag)
        H.carriers[e.role] = { chr = nil, key = h, flags = flag, area = e.area, pad = e.pad, floor = true }
        log("card %s (0x%x) lies at pad %d (%s)", e.role, flag, e.pad, e.area)
      elseif e.kind == "civ" then
        c = cx.spawn_at(e.pad, { behaviour = "civilian", body = e.body })
        if c then H.civs[#H.civs + 1] = { chr = c, area = e.area }; H.placed.civ = H.placed.civ + 1 end
        cx.give_card(c, flag, e.role, e.area, e.pad)
      else
        c = cx.spawn_at(e.pad, cx.guard_opts({ post = true, body = e.body, health = e.health, armour = e.armour }))
        if c then cx.note_guard(c, "carrier", e.area) end
        cx.give_card(c, flag, e.role, e.area, e.pad)
      end
    end
    -- the armoured-car crew (the delivery twist): two guards at the sally port
    if twist.id == "delivery" then
      for _, n in ipairs({ "sally_mid", "garage_4" }) do
        if L.pads[n] then
          local c = cx.spawn_at(L.pads[n], cx.guard_opts({ post = true, body = "St_Petersburg_Guard" }))
          if c then cx.note_guard(c, "post", "dock") end
        end
      end
    end
  end

  local SALLY = { "garage_shutter", "sally_inner" }
  local function sally(open)
    for _, n in ipairs(SALLY) do
      local o = L.objects[n]
      if o then
        if open then door.set_keyflags(o.index, 0); door.open(o.index) else door.close(o.index); door.set_keyflags(o.index, BLUE) end
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
      if R.dl == 0 and H.elapsed >= 60 then
        R.dl = 1; sally(true)
        log("the armoured car is in: the sally port is open")
        H.flash, H.flash_until = "The armoured car is in: the sally port is open", H.elapsed + 5
      elseif R.dl == 1 and H.elapsed >= 180 then
        R.dl = 2; sally(false)
        log("the armoured car has gone: the sally port is shut")
        H.flash, H.flash_until = "The armoured car has gone: the sally port is shut", H.elapsed + 4
      end
    end
  end

  function R.state() return " rnd=" .. R.sig .. " dl=" .. R.dl end

  -- ---------------------------------------------------------------------------------------------------------------------------------
  -- the truth in words (the board, the guide, the mode's own lines). Presentation: these only read R.

  function R.who(flag)
    local c = R.cards[flag]
    if not c then return nil end
    if c.kind == "floor" then return "nobody: the manager is out, it's in his desk" end
    if c.decoy then return the(c.roles_shown[1]) .. " OR " .. the(c.roles_shown[2]) end
    return the(c.role)
  end
  function R.where(flag)
    local c = R.cards[flag]
    if c.decoy then return "one of the two, the plan doesn't say which: " .. c.where .. " / " .. c.decoy.where end
    return c.where
  end
  function R.how(flag)
    local c = R.cards[flag]
    if not c then return nil end
    if c.decoy then
      local a, b = c, c.decoy
      if b.role < a.role then a, b = b, a end
      return a.how .. ". " .. b.how .. "."
    end
    return c.how
  end
  -- the INTEL board's key cards (heist_intel KEYDEF: who / where / roles)
  function R.keydef(KEYDEF)
    for _, k in ipairs(KEYDEF) do
      if R.cards[k.flag] then k.who, k.where, k.roles = R.who(k.flag), R.where(k.flag), R.cards[k.flag].roles_shown end
    end
  end
  function R.short_who(flag)
    local c = R.cards[flag]
    if c.decoy then return c.roles_shown[1] .. " or " .. c.roles_shown[2] end
    if c.kind == "floor" then return "it's in the manager's desk" end
    return the(c.role) .. " carries it"
  end
  function R.plan_keys()
    local g, b, r = R.cards[GREEN], R.cards[BLUE], R.cards[RED]
    local function nm(c) return c.kind == "floor" and "the manager's desk" or c.role end
    return string.format("GREEN (%s), then BLUE (%s, %s), then RED (%s). Aim at staff to rob them; knock guards out and search them.",
      g.decoy and (g.roles_shown[1] .. " or " .. g.roles_shown[2]) or g.role, nm(b), b.short, nm(r))
  end
  -- the map's carrier pins: { pads, flag, label }
  function R.posts()
    local out = {}
    for _, flag in ipairs(ORDER) do
      local c = R.cards[flag]
      if c.decoy then
        out[#out + 1] = { { c.pad }, flag, "GREEN? " .. c.role:upper() }
        out[#out + 1] = { { c.decoy.pad }, flag, "GREEN? " .. c.decoy.role:upper() }
      else
        out[#out + 1] = { { c.pad }, flag, CARD_NAME[flag] .. ": " .. (c.kind == "floor" and "DESK" or c.role:upper()) }
      end
    end
    return out
  end
  -- the next step's ring on the map (THE PLAN's step 2): the pads for the card the crew lacks
  function R.next_pads(flag)
    local c = R.cards[flag]
    if c.decoy then return { c.pad, c.decoy.pad } end
    return { c.pad }
  end
  -- PERSONNEL dossiers that replace the fixed ones (by index): 1-3 the three cards' carriers, 4 today's job, 8 the vault guards
  function R.dossiers()
    local t = {}
    local g = R.cards[GREEN]
    if g.decoy then
      t[1] = { "GREEN: " .. g.roles_shown[1]:upper() .. " / " .. g.roles_shown[2]:upper(), "One of the two has it: " .. g.short .. " or " .. g.decoy.short .. ".",
               R.how(GREEN), "Civilians: hurt them and it counts. Watch the feeds to find who." }
    else
      t[1] = { "GREEN: " .. g.role:upper(), g.where .. ".", g.how .. ".", "" }
    end
    for i, flag in ipairs({ BLUE, RED }) do
      local c = R.cards[flag]
      if c.kind == "floor" then
        t[i + 1] = { CARD_NAME[flag] .. ": THE MANAGER IS OUT", c.where .. ".", c.how .. ".", "His office is behind BLUE." }
      else
        t[i + 1] = { CARD_NAME[flag] .. ": " .. c.role:upper(), c.where .. ".", c.how .. ".",
                     c.kind == "civ" and "A civilian: rob, don't kill." or "Judges your disguise like any guard." }
      end
    end
    t[4] = { "TODAY'S JOB", twist.text .. ".", R.routes_text(), R.cams_text() }
    local au = R.cards[GOLD]
    if au.role ~= "vault captain" then
      t[8] = { "VAULT GUARDS", "Asleep inside the vault.", "They wake when the vault opens and come straight at you.",
               "GOLD is not with them today: " .. the(au.role) .. ", " .. au.short .. "." }
    end
    return t
  end
  function R.routes_text()
    local t = {}
    for _, n in ipairs(R.sealed) do if n == "lift_gate" then t[#t + 1] = "the lift-shaft gate is welded shut" end end
    if #R.vents_off > 0 then t[#t + 1] = "fans running in vent " .. table.concat(R.vents_off, ", ") end
    return #t > 0 and ("Routes: " .. table.concat(t, "; ") .. ".") or "Routes: every way in is open."
  end
  function R.cams_text()
    if #R.dead_list == 0 then return "Every camera is live." end
    return string.format("%d of %d cameras are dummies (grey on the map).", #R.dead_list, R.ncams)
  end
  function R.note()
    return { "TODAY", twist.text .. ". " .. R.routes_text() .. " " .. R.cams_text() .. string.format(" Vault open %d s.", S.vault_time) }
  end
  -- the first-play guide's step line and its how-to (tools/heist_guide.lua); room(role) -> the carrier's room now, or nil
  function R.guide_line(flag, room)
    local c = R.cards[flag]
    if c.decoy then return "Next: GREEN card - " .. c.roles_shown[1] .. " or " .. c.roles_shown[2] end
    if c.kind == "floor" then return "Next: " .. CARD_NAME[flag] .. " card - in the manager's desk (behind BLUE)" end
    local r = room(c.role)
    return "Next: " .. CARD_NAME[flag] .. " card - the " .. c.role .. " (" .. (r or c.short) .. ")"
  end
  function R.guide_how(flag)
    local c = R.cards[flag]
    if c.decoy then
      local a, b = c, ROLE_TIP[c.decoy.role] or c.decoy.how
      if c.decoy.role < c.role then return b .. ". " .. (a.tip or a.how) end
      return (a.tip or a.how) .. ". " .. b
    end
    return c.how
  end
  -- the mode's own objective line (the guide off)
  function R.objective(has)
    for _, flag in ipairs({ GREEN, BLUE, RED }) do
      if not has(flag) then
        local c = R.cards[flag]
        if c.decoy then return "GREEN card: " .. c.roles_shown[1] .. " or " .. c.roles_shown[2] end
        if c.kind == "floor" then return CARD_NAME[flag] .. " card: in the manager's desk" end
        return CARD_NAME[flag] .. " card: the " .. c.role
      end
    end
    return nil
  end
  return R
end
