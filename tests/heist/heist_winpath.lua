-- Heist mode test (P0 job 6): the real tools/mode_heist.lua on Bunker 2, driven headless through its test window (HEIST).
-- Three runs (tests/heist/run_winpath.sh does all three), one scenario each:
--   default                  the solo win path: every card (knock-out + search), the tiered doors, the crack with one forced jam and a
--                            restart (and the zone rule: no progress and no restart outside it), the vault, the loot, the exit, the win
--   GE_HEIST_TEST_TRAPPED=1  the vault closes on Bond: trapped, alarm, re-crack from the inside (heavy squad), out, win
--   GE_HEIST_TEST_FAIL=1     civilians: a knock-out never counts; one death past the limit fails the mission
-- Prints "HEIST PASS|FAIL <name> ..." and "HEIST DONE n passed, m failed" (the mission then ends with the mode's own win / fail).
local TRAPPED = mode.env_flag("GE_HEIST_TEST_TRAPPED")
local FAILRUN = mode.env_flag("GE_HEIST_TEST_FAIL")
HEIST_SETTINGS = {
  test = true,
  crack_time = 12, jam_chance = 0, jam_every = 1000,     -- the jam is forced through HEIST.force_jam()
  vault_time = TRAPPED and 14 or 45, klaxon_at = 5, recrack_time = 8,
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

-- warp Bond 0 onto a point: the nearest pad plus an offset
local function warp_xz(x, z)
  local best, bd = nil, 1e9
  for _, p in ipairs(pads.far_from(x, z, 0, 900)) do
    local px, _, pz = pads.pos(p)
    local d = d2(px, pz, x, z)
    if d < bd then best, bd = p, d end
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
  local s
  for _, o in ipairs({ { -50, 0 }, { 50, 0 }, { 0, 50 }, { 0, -50 }, { 0, 0 } }) do
    warp_xz(x + o[1], z + o[2])
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
  if kx then warp_xz(kx, kz) end
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
      else warp_xz(it.x, it.z); wait(0.5); if not spawn.item_alive(it.h) then got = got + 1 end end
    end
  end
  return got, HEIST.bars + HEIST.cash - n0
end

local function win_path()
  -- the setup
  check("over 100 guards placed", #HEIST.guards >= 100, string.format("guards=%d (posted %d, raisers %d, carriers %d)", #HEIST.guards,
        HEIST.placed.post, HEIST.placed.raiser, HEIST.placed.carrier))
  check("bank staff placed", HEIST.placed.civ >= 15, "civilians=" .. HEIST.placed.civ)
  check("six cameras live", #cctv.list() == 6)
  check("tiered doors: GREEN offices, BLUE security, RED antechambers, vault, GOLD safe",
        door.get(46).keyflags == 0x2 and door.get(49).keyflags == 0x2 and door.get(50).keyflags == 0x4 and door.get(53).keyflags == 0x4
        and door.get(58).keyflags == 0x8 and door.get(61).keyflags == 0x8 and door.get(68).keyflags == 0x40000000
        and door.get(71).keyflags == 0x40000000 and door.get(102).keyflags == 0x10 and door.get(72).keyflags == 0)
  check("vault doors are no longer one-way", door.one_way(68) == false and door.one_way(71) == false and door.one_way(58) == false)
  check("loot in the vault and the cells", #HEIST.loot_items >= 30, "items=" .. #HEIST.loot_items)
  check("Bond starts with the silenced PP7 in hand and the cracker", bond.weapon(0) == 5, "weapon=" .. tostring(bond.weapon(0)))
  check("phase casing", HEIST.phase == "casing", HEIST.phase)
  check("per-player keys: no card, the GREEN door refuses", door.try(0, 46) == false)

  -- the ladder
  take_card(0x2, "GREEN")
  bond.warp(0, 68)                       -- room 13, at the offices door pair 48/49
  wait(0.5)
  check("GREEN opens the office doors", door.try(0, 48) == true)
  take_card(0x4, "BLUE")
  bond.warp(0, 69)                       -- room 14, at the security doors 50/51
  wait(0.5)
  check("BLUE opens the security wing", door.try(0, 50) == true)
  bond.warp(0, 82)                       -- room 43: the security console (a use press there with BLUE)
  wait(0.5)
  door.try(0, 52)
  wait(0.5)
  local cam = cctv.get(cctv.list()[1])
  check("the security console blinds the cameras", HEIST.cam_off_until ~= nil and cam and cam.disabled, "until=" .. tostring(HEIST.cam_off_until))
  take_card(0x8, "RED")
  bond.warp(0, 86)                       -- room 49, at the antechamber doors 58/59
  wait(0.5)
  check("RED opens the vault antechamber", door.try(0, 58) == true)

  -- the crack, with a forced jam and the zone rule
  bond.warp(0, 85)
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
  bond.warp(0, 86)
  wait(2)
  check("no progress with nobody in the zone", math.abs(HEIST.progress - p0) < 0.03, string.format("%.2f -> %.2f", p0, HEIST.progress))
  bond.warp(0, 85)
  wait(1)
  check("forced jam", HEIST.force_jam() == true and HEIST.phase == "jammed")
  local pj = HEIST.progress
  wait(2)
  check("a jammed cracker makes no progress", HEIST.progress == pj and HEIST.phase == "jammed")
  bond.warp(0, 86)
  wait(0.5)
  bond.test_gadget(0, "doordecoder")
  wait(0.5)
  check("restart refused outside the zone", HEIST.phase == "jammed", HEIST.phase)
  bond.warp(0, 85)
  wait(0.5)
  bond.test_gadget(0, "doordecoder")
  wait(0.5)
  check("restarted back in the zone", HEIST.phase == "cracking" and HEIST.jams == 1, HEIST.phase)
  local opened = crack_to_open()
  check("the vault opens", opened, "phase=" .. HEIST.phase)
  wait(1.5)
  local da = door.get(68)
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
    bond.warp(0, 12)                     -- room 26, at the safe
    wait(0.5)
    check("GOLD opens the manager's safe", door.try(0, 102) == true)
    local spilled = wait_for(4, function() for _, it in ipairs(HEIST.loot_items) do if it.where == "safe" then return true end end return false end)
    check("the safe's gold appears when it opens", spilled)
    local b0 = HEIST.bars
    grab("safe")
    check("safe gold collected", HEIST.bars >= b0 + 3, string.format("%d -> %d", b0, HEIST.bars))
  end

  -- a deposit cell: GREEN opens cell 77 (the cash bundles inside)
  bond.warp(0, 91)                       -- room 39, the cell corridor
  wait(0.5)
  check("GREEN opens deposit cell 77", door.try(0, 77) == true)
  wait(1.5)
  local c0 = HEIST.cash
  grab("cell_green")
  check("cash from the cell", HEIST.cash > c0, string.format("%d -> %d", c0, HEIST.cash))

  -- the exit
  bond.warp(0, 51)
  local won = wait_for(4, function() return ended.win ~= nil end)
  check("exit at the van wins", won and HEIST.ended, tostring(ended.win))
  local fine = civilian.killed() * 100000
  local bonus = HEIST.hot and 0 or math.floor(HEIST.loot * 0.25)
  check("score = loot + quiet bonus - civilian fines; best this session", HEIST.score == math.max(0, HEIST.loot + bonus - fine) and HEIST_BEST == HEIST.score,
        string.format("score=%s loot=%d killed=%d best=%s", tostring(HEIST.score), HEIST.loot, civilian.killed(), tostring(HEIST_BEST)))
end

local function trapped_path()
  check("phase casing", HEIST.phase == "casing", HEIST.phase)
  bond.warp(0, 85)
  wait(0.5)
  bond.test_gadget(0, "doordecoder")
  check("crack opens the vault", crack_to_open(), HEIST.phase)
  local _, n = grab("vault", 2)
  check("bars grabbed", n >= 2, "n=" .. n)
  bond.warp(0, 89)                        -- inside door A
  wait(0.5)
  check("Bond is inside the vault", HEIST.in_vault(0))
  local closing = wait_for(10, function() return HEIST.phase == "closing" end)
  check("klaxon before the close", closing and alarm.active(), HEIST.phase)
  local trapped = wait_for(10, function() return HEIST.phase == "trapped" end)
  check("the vault closes on Bond: trapped", trapped and HEIST.trapped[0] == true, HEIST.phase)
  check("the time lock raises the alarm (hot)", HEIST.hot and alarm.active())
  wait(2.5)
  local da = door.get(68)
  check("vault door A closed and relocked", da and da.open < 0.05 and da.keyflags == 0x40000000, da and string.format("%s %.2f", da.state, da.open) or "nil")
  local sq = HEIST.squads
  check("vault door A does not open from the inside", door.try(0, 68) == false)
  wait(0.5)
  local da2 = door.get(68)
  check("... and stays shut", da2 and da2.open < 0.05, da2 and string.format("%.2f", da2.open) or "nil")
  if HEIST.phase ~= "recracking" then bond.test_gadget(0, "doordecoder") end   -- (the use press itself restarts the cracker)
  wait(0.5)
  check("re-crack from the inside", HEIST.phase == "recracking", HEIST.phase)
  check("heavy squad sent", HEIST.squads == sq + 1)
  local out = wait_for(15, function() return HEIST.phase == "escaped" end)
  check("re-crack done: escaped", out, HEIST.phase)
  wait(1.5)
  da = door.get(68)
  check("vault door A open again", da and da.open > 0.1, da and string.format("%.2f", da.open) or "nil")
  bond.warp(0, 116)                       -- exit B, the loading dock
  local won = wait_for(4, function() return ended.win ~= nil end)
  check("exit at the loading dock wins", won, tostring(ended.win))
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
  check("mode ready", ready, "phase=" .. tostring(HEIST.phase))
  wait(1.5)
  if TRAPPED then trapped_path() elseif FAILRUN then fail_path() else win_path() end
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
