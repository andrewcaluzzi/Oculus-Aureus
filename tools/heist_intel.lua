-- The heist INTEL board's content (designs/bank-heist.md section 19): the crew's planning board, built from the mode's live data and
-- drawn by port/src/port_intel.c through the intel.* binding (port/luavm/port_luavm_intel.c). Shipped as Data/Scripts/modes/heist_intel.lua.
--   include("heist_intel")                         -- at the script's top level (include() works only there)
--   local I = heist_intel(ctx)                     -- at stage start, once the map is loaded (ctx: see below)
--   I.tick(dt)                                     -- every sim tick (it rebuilds the live layer every 0.5 s and runs the hints)
-- ctx = { S, H, PW, M (heist_bank_map()), keys = { GREEN, BLUE, RED, GOLD, FLOORKEY, VAULT }, flash_p, money, security_up }
--
-- PRESENTATION ONLY. The board never changes the sim: the content is built on the script's own clock whether or not anyone looks (never
-- "when the board is open": that is one machine's input), it only READS the mode's state, and nothing here feeds back into the mode
-- except the hint lines (flash_p, from sim state alone). The view (open, page, floor) lives in C, per machine.
--
-- Pages: 1 THE PLAN (the steps, ticked off live, quiet and loud side by side), 2 SCHEMATICS (the map a floor at a time: rooms by stealth
-- zone, exits, the vault, the safes, the power, the security room, cameras and their cones, keyed doors by colour, stairs, guards' posts,
-- guards seen on a camera feed, the crew live), 3 THE KEYS (a card per key), 4 PERSONNEL (dossiers), 5 NOTES (the rules, from S).

local BW, BH = 640, 480
local MAP = { x0 = 76, y0 = 40, x1 = 512, y1 = 448 }        -- the schematic's drawing area (board units)
local INK, INK2, DIM = 0xE8F0FFFF, 0xB8C8E0FF, 0x7890B8FF
local CARD, CARD_INK, SHADOW, PHOTO = 0xF4F0E4FF, 0x202020FF, 0x00000060, 0x1C2430FF
local NOTE, NOTE_INK = 0xF4E27CFF, 0x302810FF
local STRING_RED, PIN_RED = 0xD02828FF, 0xE03030FF
local ZONE_TINT = { public = 0x4FA8FF38, staff = 0xFFC04048, restricted = 0xFF404058 }
local ZONE_EDGE = { public = 0x9FD0FFC0, staff = 0xFFD890C0, restricted = 0xFF9090D0 }

local function pretty(n) return (n:gsub("_", " ")):upper() end
local function secs(v) return string.format("%d s", math.floor(v + 0.5)) end

function heist_intel(ctx)
  local S, H, PW, M, K = ctx.S, ctx.H, ctx.PW, ctx.M, ctx.keys
  local L = LEVEL
  local I = { data = {} }
  local D = I.data
  local hints_on = S.hints ~= false

  -- ---------------------------------------------------------------------------------------------------------------------------
  -- the data model (tests read I.data): floors, rooms per floor, the keys and what they open

  -- floors: the bank's levels from the rooms' floor heights; the street (and the canal below it) is drawn on the ground floor
  local heights = {}
  for _, r in pairs(L.rooms) do
    if r.area ~= "street" then heights[r.floor] = (heights[r.floor] or 0) + 1 end
  end
  local levels = {}
  for y, n in pairs(heights) do if n >= 2 then levels[#levels + 1] = y end end   -- (a single landing between floors is not a floor)
  table.sort(levels)
  local ground = 1
  for i, y in ipairs(levels) do if y == 0 then ground = i end end
  local names = {}
  for i = 1, #levels do
    local d = i - ground
    names[i] = d < 0 and ("B" .. -d) or (d == 0 and "G" or ("F" .. d))
  end
  local function level_of(y)
    local best = 1
    for i, ly in ipairs(levels) do if y >= ly - 10 then best = i end end
    return best
  end
  local function room_level(r) return r.area == "street" and ground or level_of(r.floor) end
  D.floors = {}
  for i = 1, #levels do D.floors[i] = { name = names[i], y = levels[i], rooms = {} } end
  local room_floor = {}                         -- room id -> floor index
  for name, r in pairs(L.rooms) do
    local f = room_level(r)
    D.floors[f].rooms[#D.floors[f].rooms + 1] = name
    room_floor[r.id] = f
  end
  for _, f in ipairs(D.floors) do table.sort(f.rooms) end
  I.room_floor = room_floor

  -- the keys: every keyed door / safe in the layout, by its card
  local KEYDEF = {
    { flag = K.GREEN, name = "GREEN CARD", col = 0x30D050FF, who = "the head teller OR the lobby captain",
      where = "banking hall: one of the two, the plan doesn't say which", roles = { "head teller", "lobby captain" } },
    { flag = K.BLUE, name = "BLUE CARD", col = 0x4090FFFF, who = "the security chief", where = "the offices behind GREEN", roles = { "security chief" } },
    { flag = K.RED, name = "RED CARD", col = 0xFF3838FF, who = "the branch manager", where = "his own office (behind BLUE)", roles = { "branch manager" } },
    { flag = K.GOLD, name = "GOLD CARD", col = 0xFFD040FF, who = "the vault captain", where = "inside the vault (armoured)", roles = { "vault captain" } },
    { flag = K.FLOORKEY, name = "FLOOR KEY", col = 0xC080FFFF, who = "nobody: it lies on a shelf", where = "records (ground floor)", roles = {} },
  }
  D.keys = {}
  local key_by_flag = {}
  for _, k in ipairs(KEYDEF) do
    local e = { flag = k.flag, name = k.name, col = k.col, who = k.who, where = k.where, roles = k.roles, doors = {}, rooms = {} }
    D.keys[#D.keys + 1] = e
    key_by_flag[k.flag] = e
  end
  local door_at = {}                            -- doorway name -> L.doors entry
  for _, d in ipairs(L.doors) do door_at[d.name] = d end
  D.doors = {}                                  -- every door object: { name, idx, flag, kind, x, z, floor, opens }
  D.unlisted = {}                               -- keyed doors with no card (the test wants none)
  local public = { public = true }
  for oname, o in pairs(L.objects) do
    if o.type == "door" or o.type == "safe" then
      local flag = o.keyflags or 0
      local x, z, floor, opens = o.x, o.z, nil, nil
      local dw = o.doorway and door_at[o.doorway]
      if dw then
        x, z = dw.x, dw.z
        local ra, rb = L.rooms[dw.a], L.rooms[dw.b]
        floor = ra and room_level(ra) or (rb and room_level(rb))
        -- the side the key lets you into: the stricter zone of the two
        local rank = { public = 1, staff = 2, restricted = 3 }
        opens = (rb and ra and (rank[rb.zone] or 0) >= (rank[ra.zone] or 0)) and dw.b or dw.a
      elseif o.room and L.rooms[o.room] then
        floor = room_level(L.rooms[o.room])
        opens = o.room
      end
      local kind = "plain"
      if flag == K.VAULT then kind = "timelock"
      elseif o.padlock then kind = "padlock"
      elseif flag ~= 0 then kind = "key" end
      local e = { name = oname, idx = o.index, pair = o.pair, flag = flag, kind = kind, x = x, z = z, floor = floor, opens = opens,
                  safe = o.type == "safe" }
      D.doors[#D.doors + 1] = e
      if kind == "key" then
        local k = key_by_flag[flag]
        if k then
          k.doors[#k.doors + 1] = e
          if opens then k.rooms[opens] = true end
        else
          D.unlisted[#D.unlisted + 1] = oname
        end
      end
    end
  end
  table.sort(D.doors, function(a, b) return a.name < b.name end)
  local function opens_text(k)
    local t = {}
    for r in pairs(k.rooms) do t[#t + 1] = r end
    table.sort(t)
    local out = {}
    for i, r in ipairs(t) do out[i] = r:gsub("_", " ") end
    return table.concat(out, ", ")
  end
  I.opens_text = opens_text

  -- named spots
  local function obj(n) return L.objects[n] end
  local function pad_xz(p) if not p then return nil end local x, _, z = pads.pos(p); return x and { x, z } or nil end
  local function room_of_xz(x, z, y)
    for name, r in pairs(L.rooms) do
      local q = r.rect
      if x >= q[1] and x <= q[3] and z >= q[2] and z <= q[4] and (y == nil or math.abs(r.floor - y) < 300) then return name, r end
    end
  end
  local function pad_floor(p)
    if not p then return nil end
    local x, y, z = pads.pos(p)
    if not x then return nil end
    local _, r = room_of_xz(x, z, y)
    return r and room_level(r) or level_of(y)
  end

  -- ---------------------------------------------------------------------------------------------------------------------------
  -- the map's transform, per floor (fit to the floor's rooms; the ground floor keeps only the street in front of the bank)

  local XF = {}
  for i, f in ipairs(D.floors) do
    local x0, z0, x1, z1 = 1e9, 1e9, -1e9, -1e9
    for _, n in ipairs(f.rooms) do
      local r = L.rooms[n]
      if r.area ~= "street" then
        x0, z0 = math.min(x0, r.rect[1]), math.min(z0, r.rect[2])
        x1, z1 = math.max(x1, r.rect[3]), math.max(z1, r.rect[4])
      end
    end
    if i == ground then z0 = z0 - 1100 end         -- the street and the van
    local pad_ = 150
    x0, z0, x1, z1 = x0 - pad_, z0 - pad_, x1 + pad_, z1 + pad_
    local w, h = MAP.x1 - MAP.x0, MAP.y1 - MAP.y0
    local s = math.min(w / (x1 - x0), h / (z1 - z0))
    local bx0 = MAP.x0 + (w - (x1 - x0) * s) / 2
    local by0 = MAP.y0 + (h - (z1 - z0) * s) / 2
    XF[i] = { wx1 = x1, wz1 = z1, s = s, bx0 = bx0, by0 = by0 }
  end
  local function to_board(f, x, z)
    local t = XF[f]
    return t.bx0 + (t.wx1 - x) * t.s, t.by0 + (t.wz1 - z) * t.s
  end
  local function in_map(x, y) return x >= MAP.x0 and x <= MAP.x1 and y >= MAP.y0 and y <= MAP.y1 end
  local function clip_rect(a, b, c, d)
    local x0, x1 = math.max(MAP.x0, math.min(a, c)), math.min(MAP.x1, math.max(a, c))
    local y0, y1 = math.max(MAP.y0, math.min(b, d)), math.min(MAP.y1, math.max(b, d))
    if x1 <= x0 or y1 <= y0 then return nil end
    return x0, y0, x1, y1
  end
  I.to_board = to_board

  -- ---------------------------------------------------------------------------------------------------------------------------
  -- drawing helpers (board units)

  local function card(x, y, w, h, opts)
    opts = opts or {}
    intel.rect(x + 3, y + 3, x + w + 3, y + h + 3, SHADOW)
    intel.rect(x, y, x + w, y + h, opts.paper or CARD)
    if opts.photo then
      intel.rect(x + 6, y + 6, x + w - 6, y + 6 + opts.photo, opts.photo_col or PHOTO)
    end
    if opts.pin ~= false then intel.marker(x + w / 2, y + 2, "pin", opts.pin_col or PIN_RED, 4) end
  end
  local function string_line(ax, ay, bx, by) intel.line(ax, ay, bx, by, STRING_RED, 1) end
  local function heading(x, y, t, col) intel.text(x, y, t, col or INK, 1.0, 0, 1) end

  -- ---------------------------------------------------------------------------------------------------------------------------
  -- page 1: THE PLAN

  local STEP_Y, STEP_H = 74, 62
  local function cam_secs() return S.cam_off_time end
  local function plan_steps()
    local g = S.cover and "Walk in the front door: you're staff, gun holstered. The hall is public; staff rooms cost cover slowly, restricted rooms in a second."
                     or "Walk in the front door from the street."
    return {
      { "GET IN", g,
        "Same doors, gun out. Once the alarm goes the hall's blast shutters lock: BLUE card to pass." },
      { "GET THE KEYS", "GREEN (head teller or lobby captain), then BLUE (security chief, offices), then RED (branch manager). Aim at staff to rob them; knock guards out and search them.",
        "Drop the carrier and take the card off the body. Every card opens doors only for the Bond who holds it." },
      { "CAMS / POWER", string.format("Security office console (BLUE, press use): every camera off %s. Or the plant room on %s: cabinet %s + main breaker %s.",
          secs(cam_secs()), names[1] or "B2", secs(S.power_cabinet), secs(S.power_cut)),
        string.format("Power out = no siren, no panels, no cameras. Backup generator in %s unless you kill it (%s).", secs(S.generator_after), secs(S.generator_time)) },
      { "CRACK VAULT", string.format("Antechamber, a floor down behind RED. Equip the door decoder and fire it at vault door A (or press use). %s of work, only while a Bond is near it.",
          secs(S.crack_time)),
        string.format("It jams (%d%% every %s): fire it again. Two squads come for it even on a quiet run.", math.floor(S.jam_chance * 100 + 0.5), secs(S.jam_every)) },
      { "GRAB THE GOLD", string.format("Open %s, klaxon at T-%d. %d bars x %s. GOLD card (vault captain) opens the manager's safe.", secs(S.vault_time), S.klaxon_at,
          S.vault_bars, ctx.money(S.bar_value)),
        string.format("At 0 the time lock seals it: anyone inside is TRAPPED (alarm, heavy squad, re-crack %s from inside).", secs(S.recrack_time)) },
      { "GET OUT", "Every living Bond in an exit with the loot: the van on the street or the loading dock. No alarm all run = +" .. math.floor(S.quiet_bonus * 100 + 0.5) .. "%.",
        string.format("Waves every %s while hot, the elite response after %s. Exit B (dock) is never locked down.", secs(S.wave_interval), secs(S.response_after)) },
    }
  end

  local function draw_plan_static()
    intel.at(1, 0, false)
    heading(28, 40, "THE PLAN")
    intel.text(130, 44, "Operation: relieve the Janus bank of its gold. Six steps. Tick them off.", INK2, 0.9)
    intel.text(176, 62, S.cover and "QUIET  (stay staff, +" .. math.floor(S.quiet_bonus * 100 + 0.5) .. "% bonus)" or "QUIET", 0x9FE0A0FF)
    intel.text(404, 62, "LOUD  (it'll happen)", 0xFF9A9AFF)
    intel.line(172, 72, 172, STEP_Y + 6 * STEP_H - 6, DIM, 1)
    intel.line(398, 72, 398, STEP_Y + 6 * STEP_H - 6, DIM, 1)
    for i, st in ipairs(plan_steps()) do
      local y = STEP_Y + (i - 1) * STEP_H
      intel.line(26, y + STEP_H - 4, 614, y + STEP_H - 4, 0x3A5C90FF, 1)
      intel.frame(30, y + 4, 44, y + 18, INK, 2)
      intel.text(50, y + 6, i .. ". " .. st[1], INK, 1.0)
      intel.text(176, y + 4, st[2], INK2, 0.82, 216)
      intel.text(402, y + 4, st[3], INK2, 0.82, 212)
      if i > 1 then string_line(37, y - STEP_H + 18, 37, y + 4) end
    end
  end

  local function crew_has(flag)
    for p = 0, bond.count() - 1 do if bond.has_key(p, flag) then return p end end
    return nil
  end
  local function plan_done()
    local d = {}
    local inside = false
    for p = 0, bond.count() - 1 do
      local r = bond.room(p)
      local n = r and L.room_name[r]
      if n and L.rooms[n] and L.rooms[n].area ~= "street" then inside = true end
    end
    I.got_in = I.got_in or inside
    d[1] = I.got_in
    d[2] = crew_has(K.RED) ~= nil
    d[3] = PW.out or (H.cam_off_until ~= nil and H.elapsed < H.cam_off_until) or (ctx.security_up and ctx.security_up() == 0)
    d[4] = H.opened_once == true
    d[5] = (H.bars or 0) > 0
    d[6] = H.ended and (H.score or 0) > 0
    return d
  end
  I.plan_done = plan_done

  local function draw_plan_live()
    intel.at(1, 0, true)
    local done = plan_done()
    for i = 1, 6 do
      local y = STEP_Y + (i - 1) * STEP_H
      if done[i] then intel.marker(37, y + 10, "check", 0x60FF60FF, 7) end
    end
    -- the next step (the cameras / power step is optional): framed in red, and ringed on the schematics (draw_map_live)
    local nxt
    for _, i in ipairs({ 1, 2, 4, 5, 6 }) do if not done[i] then nxt = i; break end end
    I.next_step = nxt
    if nxt and not H.ended then
      local y = STEP_Y + (nxt - 1) * STEP_H
      intel.frame(26, y + 1, 614, y + STEP_H - 5, 0xFF5050D0, 2)
      intel.text(50, y + 44, "NEXT", 0xFF6060FF, 0.9)
    end
    -- the keys strip under step 2
    local x = 50
    for _, k in ipairs(D.keys) do
      if k.flag == K.GREEN or k.flag == K.BLUE or k.flag == K.RED then
        local p = crew_has(k.flag)
        intel.rect(x, STEP_Y + STEP_H + 22, x + 34, STEP_Y + STEP_H + 32, p and k.col or 0x00000000 | 0x30)
        intel.frame(x, STEP_Y + STEP_H + 22, x + 34, STEP_Y + STEP_H + 32, k.col, 1)
        x = x + 38
      end
    end
    -- status
    local ph = H.phase
    local st = string.format("NOW: %s   alarm %s   power %s   cams %s   loot %s (%d bars)", ph:upper(), H.hot and "RAISED" or "quiet",
      PW.out and "OUT" or "on", (H.cam_off_until and H.elapsed < H.cam_off_until) and "OFF" or (PW.out and "dark" or "on"), ctx.money(H.loot or 0), H.bars or 0)
    intel.text(28, 456 - 22, st, 0xFFE060FF, 0.9)
  end

  -- ---------------------------------------------------------------------------------------------------------------------------
  -- page 2: SCHEMATICS

  local LABEL = { hall = "BANKING HALL", sec_office = "SECURITY", manager = "MANAGER", vault = "VAULT", ante_a = "ANTE", records = "RECORDS",
                  dock = "DOCK", power = "POWER", generator = "GENERATOR", deposit = "DEPOSIT", stairs = "RED STAIRS", typing_pool = "TYPING",
                  office_a = "OFFICE A", office_b = "OFFICE B", canteen = "CANTEEN", garage = "GARAGE", director = "DIRECTOR", boardroom = "BOARD",
                  street = "STREET", alley = "ALLEY", barracks = "BARRACKS", guard_room = "GUARDS", armory = "ARMORY", server_room = "SERVERS",
                  b1_lobby = "B1 LOBBY", vault_corr = "VAULT CORR", service_ramp = "RAMP", counting_room = "COUNTING", roof = "ROOF",
                  library = "LIBRARY", accounts = "ACCOUNTS", it_dept = "IT", cash_office = "CASH", lockers = "LOCKERS", mezz = "MEZZ" }
  local DOORCOL = { [K.GREEN] = 0x30D050FF, [K.BLUE] = 0x4090FFFF, [K.RED] = 0xFF3838FF, [K.GOLD] = 0xFFD040FF, [K.FLOORKEY] = 0xC080FFFF }

  local cams = {}                                -- { idx, name, x, z, lx, lz, floor }
  for oname, o in pairs(L.objects) do
    if o.type == "camera" then
      local lk = pad_xz(o.lookpad)
      local r = L.rooms[o.room]
      cams[#cams + 1] = { idx = o.index, name = oname, x = o.x, z = o.z, lx = lk and lk[1], lz = lk and lk[2], floor = r and room_level(r) or level_of(o.y or 0) }
    end
  end
  table.sort(cams, function(a, b) return a.idx < b.idx end)
  D.cams = cams

  local posts = nil                              -- guards' posts (the casing): snapshot once the guards stand
  local function snapshot_posts()
    posts = {}
    for _, g in ipairs(H.guards) do
      if g.kind == "post" or g.kind == "raiser" or g.kind == "carrier" then
        local x, z = spawn.where(g.chr)
        local r = spawn.room(g.chr)
        if x and r and room_floor[r] then posts[#posts + 1] = { x = x, z = z, floor = room_floor[r], kind = g.kind } end
      end
    end
  end

  local function draw_map_static()
    intel.map_page(2)
    for f, fl in ipairs(D.floors) do
      local t = XF[f]
      intel.floor(f, fl.name, t.wx1, t.wz1, t.s, t.bx0, t.by0)
    end
    intel.at(2, 0, false)
    heading(530, 40, "SCHEMATICS")
    -- the legend
    local ly = 64
    local function leg(kind, col, text)
      if kind == "zone" then intel.rect(526, ly - 4, 538, ly + 4, col) else intel.marker(532, ly, kind, col, 4) end
      intel.text(544, ly - 5, text, INK2, 0.8)
      ly = ly + 13
    end
    leg("zone", ZONE_TINT.public | 0xA0, "public")
    leg("zone", ZONE_TINT.staff | 0xA0, "staff only")
    leg("zone", ZONE_TINT.restricted | 0xA0, "restricted")
    leg("door", DOORCOL[K.GREEN], "GREEN door")
    leg("door", DOORCOL[K.BLUE], "BLUE door")
    leg("door", DOORCOL[K.RED], "RED door")
    leg("door", DOORCOL[K.GOLD], "GOLD safe")
    leg("door", DOORCOL[K.FLOORKEY], "floor key")
    leg("door", 0x101010FF, "time lock")
    leg("door", 0xB0B0B0FF, "padlocked")
    leg("cam", 0xFFFFFFFF, "camera")
    leg("stairs", INK, "stairs")
    leg("star", 0xFFD040FF, "vault / loot")
    leg("diamond", 0x60FF60FF, "exit")
    leg("tri", 0xFFA030FF, "power")
    leg("box", 0x60E0FFFF, "security rm")
    leg("ring", 0xFFA060FF, "guard post")
    leg("dot", 0xFF4040FF, "guard seen")
    intel.text(526, ly + 4, "Posts are from the casing. Watch a camera feed (the hall's screens) to see guards live.", DIM, 0.75, 92)
    for f, fl in ipairs(D.floors) do
      intel.at(2, f, false)
      intel.text(MAP.x0 + 4, MAP.y0 + 2, "FLOOR " .. fl.name, INK, 1.0, 0, 1)
      -- rooms
      for _, n in ipairs(fl.rooms) do
        local r = L.rooms[n]
        local ax, ay = to_board(f, r.rect[1], r.rect[2])
        local bx, by = to_board(f, r.rect[3], r.rect[4])
        local x0, y0, x1, y1 = clip_rect(ax, ay, bx, by)
        if x0 then
          intel.rect(x0, y0, x1, y1, ZONE_TINT[r.zone] or ZONE_TINT.public)
          intel.frame(x0, y0, x1, y1, ZONE_EDGE[r.zone] or INK2, 1)
          local lab = LABEL[n]
          if lab and x1 - x0 > 26 then intel.text((x0 + x1) / 2 - #lab * 2.6, (y0 + y1) / 2 - 4, lab, INK, 0.72) end
        end
        if r.ramp or n:find("stair") then
          local cx, cy = to_board(f, (r.rect[1] + r.rect[3]) / 2, (r.rect[2] + r.rect[4]) / 2)
          if in_map(cx, cy) then intel.marker(cx, cy + 6, "stairs", INK, 4) end
        end
      end
      -- stairs arriving from another floor (a ramp room whose far end is this floor)
      for _, r in pairs(L.rooms) do
        if r.ramp and r.ramp.to and room_level(r) ~= f and level_of(r.ramp.to) == f then
          local cx, cy = to_board(f, (r.rect[1] + r.rect[3]) / 2, (r.rect[2] + r.rect[4]) / 2)
          local ax, ay = to_board(f, r.rect[1], r.rect[2])
          local bx, by = to_board(f, r.rect[3], r.rect[4])
          local x0, y0, x1, y1 = clip_rect(ax, ay, bx, by)
          if x0 then intel.frame(x0, y0, x1, y1, 0xFFFFFF50, 1) end
          if in_map(cx, cy) then intel.marker(cx, cy, "stairs", 0xFFFFFF90, 4) end
        end
      end
    end
    -- doors
    for _, d in ipairs(D.doors) do
      if d.floor and d.x and d.kind ~= "plain" then
        local x, y = to_board(d.floor, d.x, d.z)
        if in_map(x, y) then
          intel.at(2, d.floor, false)
          local col = d.kind == "timelock" and 0x101010FF or (d.kind == "padlock" and 0xB0B0B0FF or (DOORCOL[d.flag] or 0xFFFFFFFF))
          intel.marker(x, y, "door", col, d.safe and 4 or 3)
        end
      end
    end
    -- the special spots
    local function spot(name, kind, col, label, size)
      local o = obj(name)
      if not o or not o.x then return end
      local f = (o.room and L.rooms[o.room]) and room_level(L.rooms[o.room]) or level_of(o.y or 0)
      local x, y = to_board(f, o.x, o.z)
      if in_map(x, y) then intel.at(2, f, false); intel.marker(x, y, kind, col, size or 5, label) end
    end
    local function spot_pad(p, f, kind, col, label)
      local xz = pad_xz(p)
      if not xz then return end
      local x, y = to_board(f, xz[1], xz[2])
      if in_map(x, y) then intel.at(2, f, false); intel.marker(x, y, kind, col, 5, label) end
    end
    local vr = L.rooms.vault
    if vr then
      local f = room_level(vr)
      local x, y = to_board(f, (vr.rect[1] + vr.rect[3]) / 2, (vr.rect[2] + vr.rect[4]) / 2)
      intel.at(2, f, false); intel.marker(x, y + 8, "star", 0xFFD040FF, 7, "GOLD")
    end
    spot("mgr_safe", "star", 0xFFD040FF, "SAFE", 4)
    spot("director_safe", "star", 0xFFD040FF, "SAFE", 4)
    spot("breaker", "tri", 0xFFA030FF, "BREAKER")
    spot("generator", "tri", 0xFFA030FF, "GENERATOR")
    spot("floor_key", "door", DOORCOL[K.FLOORKEY], "KEY", 3)
    spot_pad(ctx.M.P.console, ground, "box", 0x60E0FFFF, "CONSOLE")
    spot_pad(ctx.M.P.exit_a, ground, "diamond", 0x60FF60FF, "EXIT A: VAN")
    spot_pad(ctx.M.P.exit_b, ground, "diamond", 0x60FF60FF, "EXIT B: DOCK")
    spot_pad(ctx.M.P.crack, (L.rooms.ante_a and room_level(L.rooms.ante_a)) or 1, "x", 0xFFE060FF, "CRACK HERE")
    if ctx.M.P.screens_at then
      local x, y = to_board(ground, ctx.M.P.screens_at[1], ctx.M.P.screens_at[2])
      intel.at(2, ground, false); intel.marker(x, y, "box", 0x60E0FFFF, 3, "SCREENS")
    end
    -- the card holders' posts (from the casing: the plan doesn't say which of the two holds GREEN)
    local C = ctx.M.CARRIERS or {}
    local function post(list, col, label)
      local p = list and list[1]
      local f = p and pad_floor(p)
      if f then spot_pad(p, f, "pin", col, label) end
    end
    post(C.teller, DOORCOL[K.GREEN], "GREEN? HEAD TELLER")
    post(C.captain, DOORCOL[K.GREEN], "GREEN? LOBBY CAPTAIN")
    post(C.chief, DOORCOL[K.BLUE], "BLUE: SECURITY CHIEF")
    post(C.manager, DOORCOL[K.RED], "RED: MANAGER")
    post(C.vault_captain, DOORCOL[K.GOLD], "GOLD: VAULT CAPTAIN")
  end

  -- the next step's place on the map (THE PLAN's red frame): { floor, x, z, label } list
  local function next_spots()
    local out = {}
    local function at_pad(p, label)
      local xz, f = pad_xz(p), p and pad_floor(p)
      if xz and f then out[#out + 1] = { f, xz[1], xz[2], label } end
    end
    local n = I.next_step
    local C = ctx.M.CARRIERS or {}
    if n == 1 then
      local fd = obj("front_door")
      local dw = fd and fd.doorway and door_at[fd.doorway]
      if dw then out[#out + 1] = { ground, dw.x, dw.z, "NEXT: IN HERE" } end
    elseif n == 2 then
      if not crew_has(K.GREEN) then at_pad(C.teller and C.teller[1], "NEXT: GREEN?"); at_pad(C.captain and C.captain[1], "NEXT: GREEN?")
      elseif not crew_has(K.BLUE) then at_pad(C.chief and C.chief[1], "NEXT: BLUE")
      else at_pad(C.manager and C.manager[1], "NEXT: RED") end
    elseif n == 4 then at_pad(ctx.M.P.crack, "NEXT: CRACK")
    elseif n == 5 then
      local vr = L.rooms.vault
      if vr then out[#out + 1] = { room_level(vr), (vr.rect[1] + vr.rect[3]) / 2, (vr.rect[2] + vr.rect[4]) / 2, "NEXT: GOLD" } end
    elseif n == 6 then at_pad(ctx.M.P.exit_a, "NEXT: OUT"); at_pad(ctx.M.P.exit_b, "NEXT: OUT") end
    return out
  end

  local seen = {}                                 -- chr -> { x, z, floor, at }: guards a Bond saw on a camera feed
  local seen_role = {}                            -- card role -> { room, floor, at }: carriers a Bond saw on a camera feed
  I.seen, I.seen_role = seen, seen_role
  local cam_floor = {}
  for _, c in ipairs(cams) do cam_floor[c.idx] = c.floor end
  -- knowledge is the reward for using the cameras: only what a Bond watching a feed sees (on the camera's own floor) goes on the board
  local function on_feed(cam, chr)
    if not (spawn.alive(chr) and not spawn.down(chr) and cctv.sees(cam, chr)) then return nil end
    local r = spawn.room(chr)
    if not (r and room_floor[r] and room_floor[r] == cam_floor[cam]) then return nil end
    return r
  end
  local function scan_feeds()
    for p = 0, bond.count() - 1 do
      local cam = cctv.viewing(p)
      if cam then
        for _, g in ipairs(H.guards) do
          local r = on_feed(cam, g.chr)
          if r then
            local x, z = spawn.where(g.chr)
            if x then seen[g.chr] = { x = x, z = z, floor = room_floor[r], at = H.elapsed } end
          end
        end
        for role, c in pairs(H.carriers) do
          local r = c.chr and on_feed(cam, c.chr)
          if r then seen_role[role] = { room = L.room_name[r], floor = room_floor[r], at = H.elapsed } end
        end
      end
    end
  end

  local function draw_map_live()
    if posts == nil and H.ready then snapshot_posts() end
    for p = 0, bond.count() - 1 do
      local r = bond.room(p)
      if r and room_floor[r] then intel.bond(p, room_floor[r]) end
    end
    -- cameras and their cones (grey while off)
    local off = PW.out or (H.cam_off_until and H.elapsed < H.cam_off_until)
    for _, c in ipairs(cams) do
      local x, y = to_board(c.floor, c.x, c.z)
      if in_map(x, y) then
        intel.at(2, c.floor, true)
        local g = cctv.get(c.idx)
        local dead = off or (g and g.disabled)
        if c.lx then
          local dx, dz = c.lx - c.x, c.lz - c.z
          local len = math.sqrt(dx * dx + dz * dz)
          if len > 1 then
            local reach = math.max(400, math.min(1400, len * 1.4))
            local a = math.atan(dx, dz)
            local p1x, p1y = to_board(c.floor, c.x + math.sin(a - 0.6) * reach, c.z + math.cos(a - 0.6) * reach)
            local p2x, p2y = to_board(c.floor, c.x + math.sin(a + 0.6) * reach, c.z + math.cos(a + 0.6) * reach)
            intel.tri(x, y, p1x, p1y, p2x, p2y, dead and 0x80808030 or 0xFFF06040)
          end
        end
        intel.marker(x, y, "cam", dead and 0x909090FF or 0xFFFFFFFF, 4)
      end
    end
    -- the next step's place, ringed (it throbs at the board's half-second clock)
    for _, s in ipairs(next_spots()) do
      local x, y = to_board(s[1], s[2], s[3])
      if in_map(x, y) then
        intel.at(2, s[1], true)
        intel.marker(x, y, "ring", 0xFF4040FF, (math.floor(H.elapsed * 2) % 2 == 0) and 13 or 10)
        intel.text(x + 14, y + 6, s[4], 0xFF6060FF, 0.85)
      end
    end
    -- guards: their posts (known), live positions only from a camera feed
    for _, q in ipairs(posts or {}) do
      local x, y = to_board(q.floor, q.x, q.z)
      if in_map(x, y) then intel.at(2, q.floor, true); intel.marker(x, y, "ring", 0xFFA060C0, 3) end
    end
    local now = H.elapsed
    for chr, s in pairs(seen) do
      local age = now - s.at
      if age > 60 or not spawn.alive(chr) then seen[chr] = nil
      else
        local x, y = to_board(s.floor, s.x, s.z)
        if in_map(x, y) then
          intel.at(2, s.floor, true)
          intel.marker(x, y, "dot", age < 1 and 0xFF3030FF or 0xFF8080C0, 3, age >= 3 and string.format("%ds", math.floor(age)) or nil)
        end
      end
    end
  end

  -- ---------------------------------------------------------------------------------------------------------------------------
  -- page 3: THE KEYS

  local KEYCARD = { w = 186, h = 196 }
  local function key_pos(i) local c, r = (i - 1) % 3, math.floor((i - 1) / 3); return 26 + c * 200, 44 + r * 206 end
  local function how_text(k)
    if k.flag == K.GREEN then return "Teller: aim at her till she gives it up. Captain: knock him out and search him (or rob him at gunpoint)." end
    if k.flag == K.BLUE then return "The chief is tough (x1.5 health). Catch him alone in the offices: KO and search." end
    if k.flag == K.RED then return "The manager is a civilian: aim at him and he hands it over. Don't kill him." end
    if k.flag == K.GOLD then return "In the vault with the guards: only once it's open. Armoured, double health." end
    return "Walk into records (staff area) and pick it up off the shelf."
  end
  local function draw_keys_static()
    intel.at(3, 0, false)
    for i, k in ipairs(D.keys) do
      local x, y = key_pos(i)
      card(x, y, KEYCARD.w, KEYCARD.h, { photo = 34, photo_col = k.col })
      intel.text(x + 12, y + 14, k.name, 0x101010FF, 1.0, 0, 1)
      intel.text(x + 10, y + 46, "OPENS: " .. (opens_text(k) ~= "" and opens_text(k) or "-") .. " (" .. #k.doors .. ")", CARD_INK, 0.78, KEYCARD.w - 18)
      intel.text(x + 10, y + 100, "CARRIED BY: " .. k.who, 0x8A1010FF, 0.8, KEYCARD.w - 18)
      intel.text(x + 10, y + 122, "POSTED: " .. k.where, CARD_INK, 0.78, KEYCARD.w - 18)
      intel.text(x + 10, y + 146, "HOW: " .. how_text(k), CARD_INK, 0.75, KEYCARD.w - 18)
      if i > 1 then
        local px, py = key_pos(i - 1)
        if i <= 3 or i == 5 then string_line(px + KEYCARD.w / 2, py + 2, x + KEYCARD.w / 2, y + 2) end
      end
    end
    -- the sixth card: what no key opens
    local x, y = key_pos(6)
    card(x, y, KEYCARD.w, KEYCARD.h, { photo = 34, photo_col = 0x101010FF })
    intel.text(x + 12, y + 14, "NO KEY", 0xF0F0F0FF, 1.0, 0, 1)
    intel.text(x + 10, y + 46, "THE VAULT DOORS: time-locked. Only the cracker (the door decoder) opens them.", CARD_INK, 0.78, KEYCARD.w - 18)
    intel.text(x + 10, y + 90, "PADLOCKS: the alley's dock shutter and the canal tunnel's grate. Shoot the padlock.", CARD_INK, 0.78, KEYCARD.w - 18)
    intel.text(x + 10, y + 134, S.lockdown and "LOCKDOWN: when the alarm goes the hall's blast shutters close and want BLUE." or "", 0x8A1010FF, 0.78, KEYCARD.w - 18)
  end
  local function draw_keys_live()
    intel.at(3, 0, true)
    for i, k in ipairs(D.keys) do
      local x, y = key_pos(i)
      local p = crew_has(k.flag)
      local where
      for _, role in ipairs(k.roles) do
        local c = H.carriers[role]
        if c and c.chr then
          local s = seen_role[role]
          local onhim = c.key and select(2, spawn.key_alive(c.key))
          if not onhim then where = role .. ": the card is off him"
          elseif not spawn.alive(c.chr) or spawn.down(c.chr) then where = role .. ": down"
          elseif s then where = string.format("%s: on camera in %s (%s) %ds ago", role, ((s.room or "?"):gsub("_", " ")), names[s.floor] or "?", math.floor(H.elapsed - s.at))
          else where = role .. ": not seen yet (watch the feeds)" end
        end
      end
      if where then intel.text(x + 10, y + KEYCARD.h - 18, where, 0x204080FF, 0.72, KEYCARD.w - 18) end
      if p then
        intel.line(x + 6, y + 6, x + KEYCARD.w - 6, y + KEYCARD.h - 6, 0xD02020D0, 3)
        intel.line(x + KEYCARD.w - 6, y + 6, x + 6, y + KEYCARD.h - 6, 0xD02020D0, 3)
        intel.text(x + 30, y + 80, "GOT IT: BOND " .. (p + 1), 0xD02020FF, 1.0, 0, 1)
      end
    end
  end

  -- ---------------------------------------------------------------------------------------------------------------------------
  -- page 4: PERSONNEL

  local function personnel()
    local t = {
      { "TELLERS", "Behind the counter, unarmed.", "Aim at one: she surrenders, keep aiming and she hands over what she has (the head teller may hold GREEN).", "Civilians: killing them costs " .. ctx.money(S.civ_fine) .. " each and too many fails the job." },
      { "BRANCH MANAGER", "In his own office behind BLUE.", "Carries RED. Rob him at gunpoint.", "A civilian: hurt him and it counts." },
      { "LOBBY CAPTAIN", "On the hall floor (if the head teller hasn't got GREEN, he has).", "Armed guard. KO and search, or rob him at gunpoint.", "Judges your disguise like any guard." },
      { "SECURITY CHIEF", "Posted in the offices.", "Carries BLUE. 1.5x health.", "Guards the camera console's key: lose him and the security room is yours." },
      { "GUARDS", string.format("About %d posted on this difficulty: hall, offices, corridors, security, dock, vault level.", math.floor(S.guards_post * (ctx.force or 1) + 0.5)),
        "They stand post. They hear shots, see Bonds, find bodies.", "They treat you as staff until something gives you away, then 'Hey!' and a radio call." },
      { "ALARM RAISERS", "A few per area.", "On sight or sound they run for the area's alarm panel.", "Drop them before they get there, or cut the power first." },
      { "SECURITY ROOM", "The security wing's staff watch the cameras.", "A camera that times you relays to them: alarm " .. secs(S.security_delay) .. " later.", "Knock them all out and the cameras raise nothing." },
      { "VAULT GUARDS", "Asleep inside the vault, with the vault captain (GOLD).", "They wake when the vault opens and come straight at you.", "Armoured captain, double health." },
      { "SQUADS & WAVES", string.format("Two crack squads hit the cracker. Hot: waves every %s.", secs(S.wave_interval)), string.format("After %s hot: the armoured response, both entrances.", secs(S.response_after)),
        "Trapped in the vault: the heavy squad (armour, RC-P90s)." },
    }
    return t
  end
  local function draw_personnel_static()
    intel.at(4, 0, false)
    for i, d in ipairs(personnel()) do
      local c, r = (i - 1) % 3, math.floor((i - 1) / 3)
      local x, y = 26 + c * 200, 42 + r * 136
      card(x, y, 188, 126, { photo = 18, photo_col = 0x283848FF })
      intel.text(x + 10, y + 9, d[1], 0xF0F0F0FF, 0.95)
      intel.text(x + 10, y + 28, d[2], CARD_INK, 0.75, 170)
      intel.text(x + 10, y + 60, d[3], 0x8A1010FF, 0.75, 170)
      intel.text(x + 10, y + 92, d[4], CARD_INK, 0.72, 170)
      intel.text(x + 150, y + 9, "#" .. string.format("%02d", i), 0x8090A0FF, 0.8)
    end
  end

  -- ---------------------------------------------------------------------------------------------------------------------------
  -- page 5: NOTES (the rules in plain words, from S)

  local function notes()
    local ct = S.cover_tune or {}
    local function tune(k, v) return ct[k] or v end
    local n = {}
    if S.cover then
      n[#n + 1] = { "YOUR COVER", string.format("Instant: loot in hand, a slap / robbery / search seen, a shot. Fast: a gun out (%.1f s), a restricted room (%s s). Slow: staff rooms (%s s), running past a guard, loitering. Alarm or a body found ends all cover.",
        tune("armed", 0.4), tune("restricted", 1), tune("staff", 5)) }
    end
    n[#n + 1] = { "CAMERAS", string.format("They only time a Bond who looks wrong. A spot goes to the security room, which raises the alarm %s later if anyone there is up. The console (BLUE) blinds them all %s.", secs(S.security_delay), secs(S.cam_off_time)) }
    n[#n + 1] = { "THE POWER", string.format("Plant room, %s. Force the cabinet %s, throw the main %s. Leave, get seen or hit: you lose %s. Generator back in %s unless killed; guards come to look after %s.",
      names[1] or "B2", secs(S.power_cabinet), secs(S.power_cut), secs(S.power_setback), secs(S.generator_after), secs(S.investigate_after)) }
    n[#n + 1] = { "HOLD FIRE", S.hold_fire and "Guards won't shoot through bank staff. Keep a civilian between you and them - but don't get them killed: the fine and the limit apply." or "Guards shoot through anyone. Mind the staff." }
    n[#n + 1] = { "ALARM & LOCKDOWN", string.format("Raised by a guard's radio, a panel or the security room. Once up it stays up. %sWaves every %s.", S.lockdown and "The hall's blast shutters lock (BLUE to pass). " or "", secs(S.wave_interval)) }
    n[#n + 1] = { "THE TIME LOCK", string.format("The vault stays open %s, klaxon at T-%d. It seals itself: an empty vault closes quietly, a Bond inside is trapped (re-crack %s).", secs(S.vault_time), S.klaxon_at, secs(S.recrack_time)) }
    n[#n + 1] = { "SCORING", string.format("Gold bar %s, cash bundle %s. +%d%% if the alarm never went. -%s per dead civilian. Knock-outs are free.", ctx.money(S.bar_value), ctx.money(S.money_value),
      math.floor(S.quiet_bonus * 100 + 0.5), ctx.money(S.civ_fine)) }
    n[#n + 1] = { "THE SCREENS", "The hall's screen wall: press use there to watch the camera feeds. Guards you see go on the SCHEMATICS. Carriers you see go on THE KEYS." }
    n[#n + 1] = { "THIS BOARD", "Doesn't pause anything. The bank keeps moving while you read. Find a quiet corner." }
    return n
  end
  local function draw_notes_static()
    intel.at(5, 0, false)
    for i, d in ipairs(notes()) do
      local c, r = (i - 1) % 3, math.floor((i - 1) / 3)
      local x, y = 28 + c * 198 + ((i % 2) * 4), 44 + r * 136 + ((i % 3) * 3)
      card(x, y, 184, 124, { paper = NOTE, pin_col = (i % 2 == 0) and 0x3070E0FF or PIN_RED })
      intel.text(x + 10, y + 10, d[1], 0x8A1010FF, 0.95)
      intel.text(x + 10, y + 28, d[2], NOTE_INK, 0.76, 166)
    end
  end
  I.notes = notes

  -- ---------------------------------------------------------------------------------------------------------------------------
  -- build

  function I.build_static()
    intel.clear()
    intel.enable(true)
    intel.title("JANUS BANK, ST PETERSBURG  -  CREW EYES ONLY")
    intel.page(1, "THE PLAN"); intel.page(2, "SCHEMATICS"); intel.page(3, "THE KEYS"); intel.page(4, "PERSONNEL"); intel.page(5, "NOTES")
    draw_plan_static()
    draw_map_static()
    draw_keys_static()
    draw_personnel_static()
    draw_notes_static()
  end

  function I.build_live()
    intel.clear(true)
    draw_plan_live()
    draw_map_live()
    draw_keys_live()
  end

  -- ---------------------------------------------------------------------------------------------------------------------------
  -- contextual hints (one line in the Bond's own HUD, rate-limited)

  local hint_last = {}                           -- p -> elapsed of his last hint
  local hint_seen = {}                           -- p -> { id -> elapsed }
  local function hint(p, id, text)
    if not hints_on then return false end
    local now = H.elapsed
    hint_seen[p] = hint_seen[p] or {}
    if now - (hint_last[p] or -100) < 8 or now - (hint_seen[p][id] or -1000) < 60 then return false end
    local pf = H.pflash[p]
    if pf and now < pf[2] then return false end  -- never over a live warning (his own, or the crew's)
    if H.flash and now < H.flash_until then return false end
    hint_last[p], hint_seen[p][id] = now, now
    ctx.flash_p(p, text:sub(1, 63), 5)   -- (a HUD line holds 63 characters)
    return true
  end
  I.hint = hint
  local CARD_WORD = { [K.GREEN] = "GREEN", [K.BLUE] = "BLUE", [K.RED] = "RED", [K.GOLD] = "GOLD", [K.FLOORKEY] = "the floor key" }
  local SHORT_WHO = { [K.GREEN] = "head teller or lobby captain", [K.BLUE] = "the security chief carries it", [K.RED] = "the branch manager carries it",
                      [K.GOLD] = "the vault captain carries it", [K.FLOORKEY] = "on a shelf in records" }
  local function hints_tick()
    if not hints_on or H.ended or not H.ready then return end
    for p = 0, bond.count() - 1 do
      if not bond.dead(p) and not cctv.viewing(p) then
        local x, y, z = bond.pos(p)
        if x then
          local done = false
          -- a keyed door in front of him without its card
          for _, d in ipairs(D.doors) do
            if d.kind == "key" and d.x and (d.x - x) ^ 2 + (d.z - z) ^ 2 < 190 * 190 and not bond.has_key(p, d.flag) then
              local g = door.get(d.idx)
              if g and g.open < 0.3 and (d.floor == nil or room_floor[bond.room(p) or -1] == d.floor) then
                done = hint(p, "door" .. d.flag, string.format("This %s needs %s - %s (INTEL)", d.safe and "safe" or "door", CARD_WORD[d.flag] or "a key",
                                                             SHORT_WHO[d.flag] or "see the board"))
                break
              end
            end
          end
          if not done and (H.phase == "casing" or H.phase == "jammed") and ctx.M.P.crack_xz then
            local c = ctx.M.P.crack_xz
            if (c[1] - x) ^ 2 + (c[2] - z) ^ 2 < (S.crack_radius + 100) ^ 2 then
              done = hint(p, "vault" .. H.phase, H.phase == "jammed" and "Cracker jammed: fire the decoder at the vault door again"
                                                               or "Use the door decoder on the vault door (equip it, fire)")
            end
          end
          if not done and ctx.M.P.breaker and not PW.out and not PW.task and (ctx.M.P.breaker[1] - x) ^ 2 + (ctx.M.P.breaker[2] - z) ^ 2 < 260 * 260 then
            done = hint(p, "breaker", string.format("Main breaker: press use (%s cabinet, %s main), unseen", secs(S.power_cabinet), secs(S.power_cut)))
          end
          if not done and H.console_zone and zone.contains(H.console_zone, bond.id(p)) and not (H.cam_off_until and H.elapsed < H.cam_off_until) then
            done = hint(p, "console", bond.has_key(p, K.BLUE) and string.format("Camera console: press use, every camera dark %s", secs(S.cam_off_time))
                                                        or "The camera console needs BLUE (the security chief)")
          end
          if not done and H.near_monitor and H.near_monitor(p) then
            done = hint(p, "screens", "Screens: press use to watch the feeds (guards go on INTEL)")
          end
          if not done then
            local r = bond.room(p)
            local n = r and L.room_name[r]
            local rr = n and L.rooms[n]
            if rr and rr.zone == "restricted" and S.cover and not H.hot then
              hint(p, "restricted", "Restricted area: guards see through you fast here")
            end
          end
        end
      end
    end
  end

  -- ---------------------------------------------------------------------------------------------------------------------------
  -- the clock

  local acc, built, briefed = 0, false, false
  function I.tick(dt)
    acc = acc + dt
    if not built then I.build_static(); built = true end
    if H.ready and not briefed then
      briefed = true
      if S.briefing ~= false then intel.briefing() end
    end
    -- (the feeds are scanned on the half-second clock below)
    if acc >= 0.5 then
      acc = 0
      scan_feeds()
      I.build_live()
      hints_tick()
    end
  end

  return I
end
