-- Heist mode test (P0 job 6; on the Janus bank since P2.4, GE_STAGE=levelgen): the real tools/mode_heist.lua, driven headless through its
-- test window (HEIST). The doors, pads and rooms come from the mode's map tables (HEIST.M) and LEVEL, by name. Bank checks: the cameras,
-- the manager behind BLUE, posts clear of the furniture, the feeds from the hall's screens, exit B, tellers in a crossfire.
-- Five runs (tests/heist/run_winpath.sh does all of them), one scenario each:
--   default                  the solo win path: every card (knock-out + search), the tiered doors, the crack with one forced jam and a
--                            restart (and the zone rule: no progress and no restart outside it), the vault, the loot, the exit, the win
--   GE_HEIST_TEST_TRAPPED=1  the vault closes on Bond: trapped, alarm, re-crack from the inside (heavy squad), out, win
--   GE_HEIST_TEST_FAIL=1     civilians: a knock-out never counts; one death past the limit fails the mission
--   GE_HEIST_TEST_QUIET=1    the quiet win: crack, grab, leave the vault before the time lock; the empty vault seals with no alarm, and
--                            the exit pays the quiet bonus
--   GE_HEIST_TEST_COUNT=1    (with GE_DIFF=3) the full force on 007: 100+ guards placed
-- Prints "HEIST PASS|FAIL <name> ..." and "HEIST DONE n passed, m failed" (the mission then ends with the mode's own win / fail).
local TRAPPED = mode.env_flag("GE_HEIST_TEST_TRAPPED")
local FAILRUN = mode.env_flag("GE_HEIST_TEST_FAIL")
local QUIET = mode.env_flag("GE_HEIST_TEST_QUIET")
local COUNT = mode.env_flag("GE_HEIST_TEST_COUNT")
HEIST_SETTINGS = {
  test = true,
  cover = false,   -- the stock sight, as before section 16: this test warps an armed Bond through restricted rooms (tests/heist/heist_cover.lua has the cover)
  crack_time = 12, jam_chance = 0, jam_every = 1000,     -- the jam is forced through HEIST.force_jam()
  vault_time = (TRAPPED or QUIET) and 14 or 45, klaxon_at = 5, recrack_time = 8,
}
include("heist")
mods.set("invincible", true)   -- a hundred guards are awake around a warping test Bond
on("stage_start", function() mode.guards_wake(false) end)   -- a guard waking a carrier mid-search is heist_ko's business, not this test's

local pass, fail = 0, 0
local function check(name, ok, extra)
  if ok then pass = pass + 1 else fail = fail + 1 end
  print(string.format("HEIST %s %s %s", ok and "PASS" or "FAIL", name, extra or ""))
end

local ended = {}
local real_win, real_fail = mission.win, mission.fail
local function summary() print(string.format("HEIST DONE %d passed, %d failed", pass, fail)) end
mission.win = function(msg) ended.win = msg; print("HEIST event mission.win " .. tostring(msg)); return true end
mission.fail = function(msg) ended.fail = msg; print("HEIST event mission.fail " .. tostring(msg)); return true end

local function wait(sec)
  local t = 0
  while t < sec do
    t = t + coroutine.yield()
    bond.heal(0, 1, 1)
  end
end
local function wait_for(sec, fn)
  local t = 0
  while t < sec and not fn() do wait(0.25); t = t + 0.25 end
  return fn(), t
end
local function d2(ax, az, bx, bz) return math.sqrt((ax - bx) ^ 2 + (az - bz) ^ 2) end

-- warp Bond 0 onto a point: the nearest pad plus an offset. Bank v2: floors stack (the offices under the loan offices, the vault over the
-- deep archive), so the nearest pad in x, z may be a floor away: with `room` (an id) Bond lands in that room, trying pads nearest first
local PAD_ROOM = nil
local function pad_room(p)
  if not PAD_ROOM then
    PAD_ROOM = {}
    for id = 1, #LEVEL.room_name do
      local R = LEVEL.rooms[LEVEL.room_name[id]]
      for _, q in ipairs(R.pads) do PAD_ROOM[q] = id end
      for _, q in pairs(R.named) do PAD_ROOM[q] = id end
    end
    for _, D in ipairs(LEVEL.doors) do
      PAD_ROOM[D.pad_a], PAD_ROOM[D.pad_b], PAD_ROOM[D.pad] = LEVEL.rooms[D.a].id, LEVEL.rooms[D.b].id, LEVEL.rooms[D.a].id
    end
  end
  return PAD_ROOM[p]
end
local function warp_xz(x, z, room)
  local best, bd = nil, 1e9
  for _, p in ipairs(pads.far_from(x, z, 0, 900)) do
    local px, _, pz = pads.pos(p)
    local d = d2(px, pz, x, z)
    if d < bd and (not room or pad_room(p) == room) then best, bd = p, d end
  end
  if not best then return false end
  local px, _, pz = pads.pos(best)
  return bond.warp(0, best, x - px, z - pz)
end

local function carrier(flags)
  for _, role in ipairs({ "head teller", "lobby captain", "security chief", "branch manager", "vault captain" }) do
    local c = HEIST.carriers[role]
    if c and c.flags == flags then return c, role end
  end
end

-- knock the card carrier out, search him, pick the card up
local function take_card(flags, label)
  local c, role = carrier(flags)
  check(label .. " carrier exists (" .. tostring(role) .. ")", c ~= nil and c.chr ~= nil and c.key ~= nil)
  if not c then return false end
  check(label .. " carrier holds the card", select(2, spawn.key_alive(c.key)) == true)
  spawn.ko(c.chr, 0)
  wait(4.5)
  local x, z = spawn.where(c.chr)
  local croom = spawn.room(c.chr)
  local s
  for _, o in ipairs({ { -50, 0 }, { 50, 0 }, { 0, 50 }, { 0, -50 }, { 0, 0 } }) do
    warp_xz(x + o[1], z + o[2], croom)
    wait(0.5)
    local bx, _, bz = bond.pos(0)
    s = ko.search(0)
    print(string.format("HEIST   search try at %.0f from the body: %s", d2(bx, bz, x, z), tostring(s)))
    if s then break end
  end
  wait(0.5)
  local alive, held, kx, kz = spawn.key_alive(c.key)
  check(label .. ": knocked out and searched, the card drops", s == c.chr and ((alive and not held) or bond.has_key(0, flags)),
        string.format("search=%s held=%s", tostring(s), tostring(held)))
  if kx then
    warp_xz(kx, kz, croom)
    -- (the card can land outside the body's room: a carrier down in a doorway. Then no pad of croom is near it and Bond stood far from
    -- the card: the one unseeded GREEN miss in section 23b, after the lobby captain had left his post on a backup call. Then by x, z)
    local bx, _, bz = bond.pos(0)
    if d2(bx, bz, kx, kz) > 60 then warp_xz(kx, kz) end
  end
  do local bx, _, bz = bond.pos(0)
     print(string.format("HEIST   card at %s,%s (room %s), Bond at %.0f,%.0f (room %s), %.0f apart", tostring(kx and math.floor(kx)),
           tostring(kz and math.floor(kz)), tostring(croom), bx, bz, tostring(bond.room(0)), kx and d2(bx, bz, kx, kz) or -1)) end
  local got = wait_for(4, function() return bond.has_key(0, flags) end)
  check(label .. " card picked up (bond.has_key)", got, string.format("keys=0x%x", bond.keys(0)))
  return got
end

local function crack_to_open()
  local ok = wait_for(30, function() return HEIST.phase == "open" end)
  return ok
end

local function grab(where, maxn)
  local n0, got = HEIST.bars + HEIST.cash, 0
  for _, it in ipairs(HEIST.loot_items) do
    if it.where == where and spawn.item_alive(it.h) and (not maxn or got < maxn) then
      bond.warp(0, it.pad, it.dx, it.dz)
      wait(0.5)
      if not spawn.item_alive(it.h) then got = got + 1
      else warp_xz(it.x, it.z, bond.room(0)); wait(0.5); if not spawn.item_alive(it.h) then got = got + 1 end end
    end
  end
  return got, HEIST.bars + HEIST.cash - n0
end

-- the bank's doors, pads and rooms (HEIST.M / LEVEL by name); filled once the mode is ready
local W = {}
local BANK = true
local function setup_map()
  local M = HEIST.M
  W.D, W.P = M.DOORS, M.P
  W.crack, W.outside, W.exit_a, W.exit_b = M.P.crack, M.P.outside, M.P.exit_a, M.P.exit_b
  do
    local O = LEVEL.objects
    local function approach(doorway) for _, d in ipairs(LEVEL.doors) do if d.name == doorway then return d.pad_a end end end
    W.cams = #LEVEL.cameras                                   -- (bank v2: 27)
    W.green_door, W.office_at, W.office_door = O.green_hall.index, approach("hall-office_corr"), O.green_hall.index
    W.sec_at, W.sec_door = approach("hall-sec_office"), O.blue_hall.index
    W.console_at = LEVEL.pads.console
    W.console_press = function() cctv.test_use(0) end         -- a real use press (nothing in reach: the console zone takes it)
    W.mgr_at, W.mgr_door = approach("back_corr_w-manager"), O.manager_door.index
    W.red_at, W.red_door = approach("back_corr_e-stairs"), O.red_stairs.index
    W.safe_at, W.safe_door = LEVEL.pads.safe_front, O.mgr_safe.index
    W.cell_at, W.cell_door = approach("deposit-cell_1"), O.cell_green.index
    W.inside = LEVEL.pads.vault_in_a
    W.vault_a = O.vault_door_a.index
    do                                                        -- the vault's south-west corner, far from both doors (from LEVEL)
      local r = LEVEL.rooms.vault.rect
      W.corner = { r[1] + 40, r[2] + 40 }
    end
    W.corner_ok = function(room) return room == LEVEL.rooms.vault.id end
    W.corner_label = "Bond stands in the vault's far corner"
  end
end

local function all_flags(list, flags)
  for _, d in ipairs(list) do local g = door.get(d); if not g or g.keyflags ~= flags then return false end end
  return #list > 0
end

-- the bank's own checks (P2.4): the map's people and rooms
local function bank_checks()
  local mgr = HEIST.carriers["branch manager"]
  check("bank: the branch manager sits in his own office (behind BLUE)", mgr and mgr.chr and spawn.room(mgr.chr) == LEVEL.rooms.manager.id
        and door.get(W.mgr_door).keyflags == 0x4, mgr and string.format("room %s", tostring(spawn.room(mgr.chr))) or "no manager")
  local bad, posts = {}, 0
  for _, g in ipairs(HEIST.guards) do
    if g.kind == "post" and spawn.alive(g.chr) then
      posts = posts + 1
      local x, z = spawn.where(g.chr)
      for _, R in pairs(LEVEL.rooms) do
        for _, b in ipairs(R.blocks or {}) do
          local r = b.rect
          if x > r[1] - 10 and x < r[3] + 10 and z > r[2] - 10 and z < r[4] + 10 then bad[#bad + 1] = b.name end
        end
      end
    end
  end
  check("bank: no posted guard stands in a furniture block", posts > 20 and #bad == 0, string.format("%d posts, in blocks: %s", posts, table.concat(bad, ",")))
  local sides = {}
  for _, g in ipairs(HEIST.guards) do sides[g.area] = (sides[g.area] or 0) + 1 end
  check("bank: guards posted in every area (hall, corridor, offices, security, dock, records, antechamber, deposit)",
        (sides.hall or 0) >= 3 and (sides.corridor or 0) >= 2 and (sides.offices or 0) >= 2 and (sides.security or 0) >= 1 and (sides.dock or 0) >= 2
        and (sides.records or 0) >= 1 and (sides.vault_ante or 0) >= 2 and (sides.deposit or 0) >= 1,
        string.format("hall %s corridor %s offices %s security %s dock %s records %s ante %s deposit %s", tostring(sides.hall), tostring(sides.corridor),
        tostring(sides.offices), tostring(sides.security), tostring(sides.dock), tostring(sides.records), tostring(sides.vault_ante), tostring(sides.deposit)))
  check("bank: exit B: the staff door to the dock needs no card, the alley shutter is padlocked",
        door.get(LEVEL.objects.dock_door.index).keyflags == 0 and LEVEL.objects.dock_shutter.padlock ~= nil)
  -- the camera feeds from the hall's screen wall: a real use press there
  local sx, sz = W.P.screens_at[1], W.P.screens_at[2]
  bond.warp(0, LEVEL.pads.screens)
  bond.face(0, W.P.screens_face[1], W.P.screens_face[2])
  wait(1)
  check("bank: Bond at the screens is in the intel room, near a screen", HEIST.near_monitor(0), string.format("room %s at %.0f, %.0f", tostring(bond.room(0)), sx, sz))
  cctv.test_use(0)
  wait(1)
  local v = cctv.viewing(0)
  check("bank: a use press at the hall's screens opens the camera feeds", v ~= nil, "viewing " .. tostring(v))
  cctv.view_off(0)
  wait(1)
end

local function win_path()
  -- the setup
  local want = HEIST.want or { post = -100, raiser = -100 }
  check("guards placed for the difficulty (Agent: ~30 posted, 4 raisers)", mission.difficulty() == 0 and math.abs(HEIST.placed.post - 30) <= 3
        and math.abs(HEIST.placed.post - want.post) <= 3 and HEIST.placed.raiser == want.raiser,
        string.format("difficulty %d, guards=%d (posted %d of %d, raisers %d of %d, carriers %d)", mission.difficulty(), #HEIST.guards,
        HEIST.placed.post, want.post, HEIST.placed.raiser, want.raiser, HEIST.placed.carrier))
  check("bank staff placed", HEIST.placed.civ >= 15, "civilians=" .. HEIST.placed.civ)
  check(W.cams .. " cameras live", #cctv.list() == W.cams, "cameras=" .. #cctv.list())
  local D = W.D
  check("tiered doors: GREEN offices, BLUE security, RED vault approach, vault, GOLD safe",
        all_flags(D.green, 0x2) and all_flags(D.blue, 0x4) and all_flags(D.red, 0x8) and all_flags(D.vault_a, 0x40000000)
        and all_flags(D.vault_b, 0x40000000) and door.get(D.safe).keyflags == 0x10 and all_flags(D.front, 0))
  check("vault doors are no longer one-way", door.one_way(D.vault_a[1]) == false and door.one_way(D.vault_b[#D.vault_b]) == false
        and door.one_way(D.red[1]) == false)
  check("loot in the vault and the cells", #HEIST.loot_items >= 30, "items=" .. #HEIST.loot_items)
  check("Bond starts with the silenced PP7 in hand and the cracker", bond.weapon(0) == 5, "weapon=" .. tostring(bond.weapon(0)))
  check("phase casing", HEIST.phase == "casing", HEIST.phase)
  check("per-player keys: no card, the GREEN door refuses", door.try(0, W.green_door) == false)
  if BANK then bank_checks() end

  -- the ladder
  take_card(0x2, "GREEN")
  bond.warp(0, W.office_at)
  wait(0.5)
  check("GREEN opens the office doors", door.try(0, W.office_door) == true)
  take_card(0x4, "BLUE")
  bond.warp(0, W.sec_at)
  wait(0.5)
  check("BLUE opens the security wing", door.try(0, W.sec_door) == true)
  bond.warp(0, W.console_at)
  wait(0.5)
  W.console_press()
  wait(0.5)
  local cam = cctv.get(cctv.list()[1])
  check("the security console blinds the cameras", HEIST.cam_off_until ~= nil and cam and cam.disabled, "until=" .. tostring(HEIST.cam_off_until))
  local until0 = HEIST.cam_off_until
  wait(1)
  W.console_press()                      -- more use presses in the console zone while the cameras are off: ignored
  wait(0.5)
  W.console_press()
  wait(0.5)
  check("a second console press while the cameras are off does nothing", HEIST.cam_off_until == until0,
        string.format("until %s -> %s", tostring(until0), tostring(HEIST.cam_off_until)))
  if BANK then
    bond.warp(0, W.mgr_at)               -- the staff corridor, at the manager's door
    wait(0.5)
    check("bank: BLUE opens the manager's office", door.try(0, W.mgr_door) == true)
  end
  take_card(0x8, "RED")
  bond.warp(0, W.red_at)
  wait(0.5)
  check("RED opens the vault approach", door.try(0, W.red_door) == true)

  -- the crack, with a forced jam and the zone rule
  bond.warp(0, W.crack)
  wait(0.5)
  bond.equip(0, "doordecoder")
  wait(1)
  bond.test_gadget(0, "doordecoder")
  wait(0.5)
  check("the cracker starts in the zone", HEIST.phase == "cracking", HEIST.phase)
  check("crack squad sent", HEIST.squads >= 1)
  wait(3)
  local p0 = HEIST.progress
  check("progress runs while Bond holds the zone", p0 > 0.15, string.format("%.2f", p0))
  bond.warp(0, W.outside)
  wait(2)
  check("no progress with nobody in the zone", math.abs(HEIST.progress - p0) < 0.03, string.format("%.2f -> %.2f", p0, HEIST.progress))
  bond.warp(0, W.crack)
  wait(1)
  check("forced jam", HEIST.force_jam() == true and HEIST.phase == "jammed")
  local pj = HEIST.progress
  wait(2)
  check("a jammed cracker makes no progress", HEIST.progress == pj and HEIST.phase == "jammed")
  bond.warp(0, W.outside)
  wait(0.5)
  bond.test_gadget(0, "doordecoder")
  wait(0.5)
  check("restart refused outside the zone", HEIST.phase == "jammed", HEIST.phase)
  bond.warp(0, W.crack)
  wait(0.5)
  bond.test_gadget(0, "doordecoder")
  wait(0.5)
  check("restarted back in the zone", HEIST.phase == "cracking" and HEIST.jams == 1, HEIST.phase)
  local opened = crack_to_open()
  check("the vault opens", opened, "phase=" .. HEIST.phase)
  wait(1.5)
  local da = door.get(W.vault_a)
  check("vault door A is opening/open", da and da.open > 0.1, da and string.format("%s %.2f", da.state, da.open) or "nil")
  check("countdown running", (hud.countdown_left() or 0) > 30, tostring(hud.countdown_left()))

  -- the loot (walking onto one bar picks up the ones next to it too)
  local _, counted = grab("vault")
  local nvault = 0
  for _, it in ipairs(HEIST.loot_items) do if it.where == "vault" then nvault = nvault + 1 end end
  check("every gold bar in the vault collected", HEIST.bars == nvault and counted == nvault, string.format("counted %d of %d, bars %d", counted, nvault, HEIST.bars))
  check("score adds up", HEIST.loot == HEIST.bars * 150000 + HEIST.cash * 25000, "loot=" .. HEIST.loot)

  -- the manager's safe: GOLD from the vault captain
  if take_card(0x10, "GOLD") then
    bond.warp(0, W.safe_at)                -- at the safe (Bunker 2: room 26)
    wait(0.5)
    check("GOLD opens the manager's safe", door.try(0, W.safe_door) == true)
    local spilled = wait_for(4, function() for _, it in ipairs(HEIST.loot_items) do if it.where == "safe" then return true end end return false end)
    check("the safe's gold appears when it opens", spilled)
    local b0 = HEIST.bars
    grab("safe")
    check("safe gold collected", HEIST.bars >= b0 + 3, string.format("%d -> %d", b0, HEIST.bars))
  end

  -- a deposit cell: GREEN opens one (Bunker 2: cell 77; the bank: cell_green), the cash bundles inside
  bond.warp(0, W.cell_at)                -- at the GREEN cell (Bunker 2: room 39, the cell corridor)
  wait(0.5)
  check("GREEN opens the GREEN deposit cell", door.try(0, W.cell_door) == true)
  wait(1.5)
  local c0 = HEIST.cash
  grab("cell_green")
  check("cash from the cell", HEIST.cash > c0, string.format("%d -> %d", c0, HEIST.cash))

  -- the tellers in a crossfire (P2.4): Bond stands among the staff behind the counter with the alarm up; the guards' misses used to land
  -- round him and kill the tellers (4 in ~10 s in the screenshot run). They hold their fire now while a civilian is beside him.
  local function shots()
    local f, h = 0, 0
    for c = 8000, 8400 do local a, b = spawn.fire_stats(c); f, h = f + (a or 0), h + (b or 0) end
    return f, h
  end
  if not alarm.active() then alarm.start() end
  local tx, _, tz = pads.pos(LEVEL.pads.teller_2)
  warp_xz(tx + 40, tz + 60)
  local k0 = civilian.killed()
  local f0, h0 = shots()
  wait(15)
  local f1, h1 = shots()
  check("bank: Bond among the tellers with the alarm up: the guards hold their fire, no teller dies", civilian.killed() == k0 and h1 > h0,
        string.format("killed %d, shots fired %d, held %d in 15 s", civilian.killed() - k0, f1 - f0, h1 - h0))

  -- the exit
  bond.warp(0, W.exit_a)
  local won = wait_for(4, function() return ended.win ~= nil end)
  check("exit at the van wins", won and HEIST.ended, tostring(ended.win))
  local fine = civilian.killed() * 100000
  local bonus = HEIST.hot and 0 or math.floor(HEIST.loot * 0.25)
  check("score = loot + quiet bonus - civilian fines; best this session", HEIST.score == math.max(0, HEIST.loot + bonus - fine) and HEIST_BEST == HEIST.score,
        string.format("score=%s loot=%d killed=%d best=%s", tostring(HEIST.score), HEIST.loot, civilian.killed(), tostring(HEIST_BEST)))
end

local function trapped_path()
  check("phase casing", HEIST.phase == "casing", HEIST.phase)
  bond.warp(0, W.crack)
  wait(0.5)
  bond.test_gadget(0, "doordecoder")
  check("crack opens the vault", crack_to_open(), HEIST.phase)
  local _, n = grab("vault", 2)
  check("bars grabbed", n >= 2, "n=" .. n)
  -- Bunker 2: the west end of room 56 (x below ~300 along z 3141): the old circle test missed it, and a Bond there at the time lock was
  -- "sealed" in for good with no re-crack (the player bot found it). He counts as inside by his room now. The bank: a far corner.
  warp_xz(W.corner[1], W.corner[2], LEVEL.rooms.vault.id)
  wait(0.5)
  local wx, _, wz = bond.pos(0)
  check(W.corner_label, W.corner_ok(bond.room(0), wx),
        string.format("room %s at (%.0f, %.0f)", tostring(bond.room(0)), wx, wz))
  check("Bond is inside the vault", HEIST.in_vault(0))
  local sq = HEIST.squads
  local closing = wait_for(10, function() return HEIST.phase == "closing" end)
  check("klaxon before the close", closing and alarm.active(), HEIST.phase)
  local trapped = wait_for(10, function() return HEIST.phase == "trapped" end)
  check("the vault closes on Bond in the west end: trapped", trapped and HEIST.trapped[0] == true, HEIST.phase)
  check("the time lock raises the alarm (hot) when it traps a Bond", HEIST.hot and alarm.active())
  check("... and sends the heavy squad", HEIST.squads == sq + 1, string.format("squads %d -> %d", sq, HEIST.squads))
  bond.warp(0, W.inside)                      -- to the re-crack zone inside door A
  wait(2.5)
  local da = door.get(W.vault_a)
  check("vault door A closed and relocked", da and da.open < 0.05 and da.keyflags == 0x40000000, da and string.format("%s %.2f", da.state, da.open) or "nil")
  check("vault door A does not open from the inside", door.try(0, W.vault_a) == false)
  wait(0.5)
  local da2 = door.get(W.vault_a)
  check("... and stays shut", da2 and da2.open < 0.05, da2 and string.format("%.2f", da2.open) or "nil")
  if HEIST.phase ~= "recracking" then bond.test_gadget(0, "doordecoder") end   -- (the use press itself restarts the cracker)
  wait(0.5)
  check("re-crack from the inside", HEIST.phase == "recracking", HEIST.phase)
  local out = wait_for(15, function() return HEIST.phase == "escaped" end)
  check("re-crack done: escaped", out, HEIST.phase)
  -- (the heavy squad crowds door A from outside on the bank and stops the leaves; the mode snaps a blocked vault door after 1.5 s)
  wait_for(3, function() local g = door.get(W.vault_a); return g and g.open > 0.1 end)
  da = door.get(W.vault_a)
  check("vault door A open again", da and da.open > 0.1, da and string.format("%.2f", da.open) or "nil")
  bond.warp(0, W.exit_b)                     -- exit B, the loading dock
  local won = wait_for(4, function() return ended.win ~= nil end)
  check("exit at the loading dock wins", won, tostring(ended.win))
end

-- the quiet win (AC delegated, Claude's call 2026-10-25): the time lock closing on an empty vault raises no alarm
local function quiet_path()
  check("phase casing, quiet", HEIST.phase == "casing" and not HEIST.hot, HEIST.phase)
  if BANK then
    -- the bank's vault-approach camera watches the cracker's spot (Agent: 10 s of sight raises the alarm; the crack takes longer), so a
    -- quiet run switches the cameras off at the security console first (BLUE: what a player does; the ladder is the win path's job)
    HEIST.camera_switch(0)
    local cv = cctv.get(LEVEL.objects.cam_vault.index)
    check("bank: the security console blinds the vault camera before the crack", cv and cv.disabled and HEIST.cam_off_until ~= nil)
  end
  bond.warp(0, W.crack)
  wait(0.5)
  bond.test_gadget(0, "doordecoder")
  check("crack opens the vault", crack_to_open(), HEIST.phase)
  local _, n = grab("vault", 3)
  check("bars grabbed", n >= 3, "n=" .. n)
  bond.warp(0, W.outside)                      -- out of the vault, in the east wing
  wait(0.5)
  check("Bond is out of the vault", not HEIST.in_vault(0))
  local sealed = wait_for(20, function() return HEIST.phase == "sealed" end)
  check("the empty vault seals", sealed, HEIST.phase)
  wait(1)
  check("the time lock on an empty vault raises no alarm", not HEIST.hot and not alarm.active(),
        string.format("hot=%s alarm=%s", tostring(HEIST.hot), tostring(alarm.active())))
  -- (sealed is the close order: a crack-squad guard in the doorway stops the moving door, and the time lock snaps it 1.5 s after the
  -- order (hold_vault_doors). GE_SEED 5: 0.11 open 1 s after it)
  wait_for(1.5, function() local g = door.get(W.vault_a); return g and g.open < 0.05 end)
  local da = door.get(W.vault_a)
  check("vault door A shut and relocked", da and da.open < 0.05 and da.keyflags == 0x40000000, da and string.format("%.2f", da.open) or "nil")
  bond.warp(0, W.exit_a)                        -- exit A, the van
  local won = wait_for(4, function() return ended.win ~= nil end)
  check("a quiet exit wins", won and not HEIST.hot, tostring(ended.win))
  local bonus = math.floor(HEIST.loot * 0.25)
  check("the quiet bonus is paid (+25%)", HEIST.score == HEIST.loot + bonus - civilian.killed() * 100000 and bonus > 0,
        string.format("score=%s loot=%d bonus=%d", tostring(HEIST.score), HEIST.loot, bonus))
end

-- the full force on 007 (GE_DIFF=3)
local function count_path()
  local want = HEIST.want or { post = -100, raiser = -100 }
  check("007: the full force, 100+ guards placed", mission.difficulty() == 3 and #HEIST.guards >= 100 and math.abs(HEIST.placed.post - 90) <= 3
        and HEIST.placed.raiser == 12, string.format("difficulty %d, guards=%d (posted %d of %d, raisers %d of %d)", mission.difficulty(),
        #HEIST.guards, HEIST.placed.post, want.post, HEIST.placed.raiser, want.raiser))
  ended.fail = "count run done"
end

local function fail_path()
  local civs = {}
  for _, c in ipairs(HEIST.civs) do if spawn.alive(c.chr) then civs[#civs + 1] = c.chr end end
  check("civilians to work with", #civs >= HEIST.civ_limit + 2, string.format("%d civilians, limit %d", #civs, HEIST.civ_limit))
  spawn.ko(civs[1], 0)
  wait(1)
  check("a knock-out is not a casualty", ko.is(civs[1]) and civilian.killed() == 0 and not HEIST.ended)
  for i = 2, HEIST.civ_limit + 1 do spawn.kill(civs[i]); wait(1) end
  check("deaths up to the limit do not fail", civilian.killed() == HEIST.civ_limit and not HEIST.ended and ended.fail == nil,
        string.format("killed=%d limit=%d", civilian.killed(), HEIST.civ_limit))
  spawn.kill(civs[HEIST.civ_limit + 2])
  local failed = wait_for(4, function() return ended.fail ~= nil end)
  check("one more death fails the mission", failed and tostring(ended.fail):find("civilians") ~= nil, tostring(ended.fail))
end

local finished = false
local function body()
  local ready = wait_for(20, function() return HEIST.ready end)
  check("mode ready", ready, "phase=" .. tostring(HEIST.phase) .. " map=" .. tostring(HEIST.map))
  if ready then setup_map() end
  wait(1.5)
  if TRAPPED then trapped_path() elseif FAILRUN then fail_path() elseif QUIET then quiet_path() elseif COUNT then count_path() else win_path() end
  summary()
  finished = true
  if ended.win then real_win(ended.win) elseif ended.fail then real_fail(ended.fail) else real_fail("heist test: no ending") end
end

local co = coroutine.wrap(body)
on("tick", function(dt)
  if finished then return end
  local ok, err = pcall(co, dt)
  if not ok then finished = true; print("HEIST FAIL script error: " .. tostring(err)); summary() end
end)
