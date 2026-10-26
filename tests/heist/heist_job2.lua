-- The heist's second job (designs/job2-casino.md): Le Grand Casino Janus, on the levelgen slot of build_job2 (casino.json,
-- GE_LEVELGEN_DIR=port/levelgen_casino). The real tools/mode_heist.lua, driven headless through its test window (HEIST), the way
-- tests/heist/heist_winpath.lua drives the bank. Proves the mode runs a second job from its job definition (tools/jobs/casino.lua):
--   1 the level loads and the mode picks the casino job (not the bank) from the layout
--   2 the INTEL board and the first-play guide describe this job (its people, its keys, its rooms, its exits)
--   3 guards, civilians, card carriers and the 29 cameras spawn in the casino's areas; the casino's door tiers are set
--   4 a stick-input WALK (bond.walk) from the place to the house vault's door: the atrium, the Salle Renaissance, the Salle Europe,
--     the north corridor, the RED stairs, the vault corridor, the antechamber (doors opened by the script: reachability, not cards);
--     and up the security stair core to the surveillance room's console
--   5 the scripted win path: GREEN, BLUE, RED off their carriers (knock-out + search), the tiers they open, the console, the crack,
--     the house reserve, the high-roller safe (GOLD), the chip bank (GREEN), out at the limousine: the mode's win
-- tests/heist/run_job2.sh runs it. Prints "HEIST PASS|FAIL <name> ..." and "HEIST DONE n passed, m failed".
HEIST_SETTINGS = {
  test = true,
  cover = false,   -- the stock sight (the win path warps an armed Bond through restricted rooms; tests/heist/heist_cover.lua has the cover)
  crack_time = 12, jam_chance = 0, jam_every = 1000,
  vault_time = 45, klaxon_at = 5, recrack_time = 8,
  briefing = false,
}
include("heist")
mods.set("invincible", true)
on("stage_start", function() mode.guards_wake(false) end)

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
local function has(s, sub) return type(s) == "string" and s:find(sub, 1, true) ~= nil end

local PAD_ROOM = nil
local function pad_room(p)
  if not PAD_ROOM then
    PAD_ROOM = {}
    for id = 1, #LEVEL.room_name do
      local R = LEVEL.rooms[LEVEL.room_name[id]]
      for _, q in ipairs(R.pads) do PAD_ROOM[q] = id end
      for _, q in pairs(R.named) do PAD_ROOM[q] = id end
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
local function approach(a, b)   -- the pad on a's side of the doorway a-b
  for _, D in ipairs(LEVEL.doors) do
    if D.a == a and D.b == b then return D.pad_a end
    if D.a == b and D.b == a then return D.pad_b end
  end
end

local function carrier(flags)
  for role, c in pairs(HEIST.carriers) do if c.flags == flags then return c, role end end
end
local function take_card(flags, label)
  local c, role = carrier(flags)
  check(label .. " carrier exists (" .. tostring(role) .. ")", c ~= nil and c.chr ~= nil and c.key ~= nil)
  if not c then return false end
  spawn.ko(c.chr, 0)
  wait(4.5)
  local x, z = spawn.where(c.chr)
  local croom = spawn.room(c.chr)
  local s
  for _, o in ipairs({ { -50, 0 }, { 50, 0 }, { 0, 50 }, { 0, -50 }, { 0, 0 } }) do
    warp_xz(x + o[1], z + o[2], croom)
    wait(0.5)
    s = ko.search(0)
    if s then break end
  end
  wait(0.5)
  local alive, held, kx, kz = spawn.key_alive(c.key)
  if kx then warp_xz(kx, kz, croom) end
  local got = wait_for(4, function() return bond.has_key(0, flags) end)
  check(label .. " card taken off the " .. tostring(role), got, string.format("search=%s keys=0x%x", tostring(s), bond.keys(0)))
  return got
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
local function all_flags(list, flags)
  for _, d in ipairs(list) do local g = door.get(d); if not g or g.keyflags ~= flags then return false end end
  return #list > 0
end

-- ---------------------------------------------------------------------------------------------------------------------------------
-- 1-3: the job, the board, the people

local function job_checks()
  local M, J = HEIST.M, HEIST.M.JOB
  check("the layout names the casino job and the mode runs it", LEVEL.script.heist.job == "casino" and J.name == "casino" and HEIST.map == "casino",
        string.format("layout job %s, mode job %s, map %s", tostring(LEVEL.script.heist.job), tostring(J.name), tostring(HEIST.map)))
  check("no bank names on this level (no vault_door_a, no hall)", LEVEL.objects.vault_door_a == nil and LEVEL.rooms.hall == nil)
  check("the vault is the house vault (VAULT_ROOMS)", M.VAULT_ROOMS[LEVEL.rooms.house_vault.id] == true)
  check("the screens are the pit podium's monitors in the Salle Europe", M.intel_room == LEVEL.rooms.salle_europe.id and #M.P.monitors == 6)
  -- the INTEL board's content (presentation: I.data, the notes) and the guide
  local I = HEIST.intel
  check("INTEL: the board exists", I ~= nil and I.data ~= nil)
  if I then
    local K = I.data.keys
    check("INTEL keys: GREEN is the head croupier or the floor supervisor", has(K[1].who, "croupier") and has(K[1].who, "floor supervisor"), K[1].who)
    check("INTEL keys: BLUE the shift manager, RED the casino director, GOLD the vault manager",
          has(K[2].who, "shift manager") and has(K[3].who, "casino director") and has(K[4].who, "vault manager"),
          string.format("%s / %s / %s", K[2].who, K[3].who, K[4].who))
    check("INTEL keys: the fifth key is the count key (the lockers), and it opens the drop cage", K[5].name == "COUNT KEY" and #K[5].doors == 1
          and K[5].doors[1].name == "drop_cage_door", K[5].name .. " " .. tostring(K[5].doors[1] and K[5].doors[1].name))
    check("INTEL: every keyed door is on a card", #I.data.unlisted == 0, table.concat(I.data.unlisted, ","))
    local tl = 0
    for _, d in ipairs(I.data.doors) do if d.kind == "timelock" then tl = tl + 1 end end
    check("INTEL: the two time-locked vault doors (four leaves' objects)", tl == 2, "timelock doors " .. tl)
    local n = I.notes()
    local screens = ""
    for _, c in ipairs(n) do if c[1] == "THE SCREENS" then screens = c[2] end end
    check("INTEL notes: the screens are the pit podium's (this job's words)", has(screens, "pit podium"), screens)
    check("INTEL floors: B1, G, F1", #I.data.floors == 3 and I.data.floors[1].name == "B1" and I.data.floors[3].name == "F1",
          string.format("%d floors", #I.data.floors))
  end
  local g = HEIST.guide
  local obj = g and g.objective(0) or ""
  check("guide: the first step is the casino's doors", has(obj, "casino doors"), obj)
  -- the people
  local want = HEIST.want or { post = -100, raiser = -100 }
  check("guards placed for the difficulty (Agent: ~30 posted)", math.abs(HEIST.placed.post - want.post) <= 3 and HEIST.placed.post >= 25
        and HEIST.placed.raiser == want.raiser, string.format("posted %d of %d, raisers %d of %d, carriers %d", HEIST.placed.post, want.post,
        HEIST.placed.raiser, want.raiser, HEIST.placed.carrier))
  check("four card carriers (the guards among them counted as carriers)", HEIST.placed.carrier >= 2 and carrier(0x2) and carrier(0x4) and carrier(0x8) and carrier(0x10),
        "carriers " .. HEIST.placed.carrier)
  check("staff and punters placed", HEIST.placed.civ >= 15, "civilians " .. HEIST.placed.civ)
  local sides = {}
  for _, gg in ipairs(HEIST.guards) do sides[gg.area] = (sides[gg.area] or 0) + 1 end
  check("guards in the casino's areas (floor, cage, security, catwalks, salons, vault level, dock)",
        (sides.floor or 0) >= 4 and (sides.cage or 0) >= 1 and (sides.security or 0) >= 2 and (sides.catwalk or 0) >= 1 and (sides.salons or 0) >= 1
        and (sides.vault_ante or 0) >= 3 and (sides.dock or 0) >= 1,
        string.format("floor %s cage %s security %s catwalk %s salons %s vault %s dock %s", tostring(sides.floor), tostring(sides.cage), tostring(sides.security),
                      tostring(sides.catwalk), tostring(sides.salons), tostring(sides.vault_ante), tostring(sides.dock)))
  local dir = HEIST.carriers["casino director"]
  check("the casino director sits in his office (F1, behind BLUE)", dir and dir.chr and spawn.room(dir.chr) == LEVEL.rooms.director.id
        and door.get(LEVEL.objects.director_door.index).keyflags == 0x4)
  check("29 cameras live, the eye in the sky among them", #cctv.list() == 29 and #LEVEL.cameras == 29
        and cctv.name(LEVEL.objects.cam_surveillance.index) == "EYE IN THE SKY", "cameras " .. #cctv.list())
  local D = M.DOORS
  check("the tiers: GREEN (cage, pit office), BLUE (security, the core, the eye, the suite), RED (vault stairs, plant), the vault, GOLD",
        all_flags(D.green, 0x2) and all_flags(D.blue, 0x4) and all_flags(D.red, 0x8) and all_flags(D.vault_a, 0x40000000)
        and all_flags(D.vault_b, 0x40000000) and door.get(D.safe).keyflags == 0x10 and all_flags(D.front, 0),
        string.format("green %d blue %d red %d", #D.green, #D.blue, #D.red))
  check("the interfaces: 4 guard tours, 3 vents, 3 grapple points", M.PATROLS.floor and #M.PATROLS.floor == 6 and M.VENTS.count and M.VENTS.salon
        and M.VENTS.vault and M.GRAPPLES.catwalk and M.GRAPPLES.terrace and M.GRAPPLES.goodslift)
  check("loot: the house reserve and the cells", #HEIST.loot_items >= 30, "items " .. #HEIST.loot_items)
end

-- ---------------------------------------------------------------------------------------------------------------------------------
-- 4: on foot

local function doorway(a, b, nth)
  local k = 0
  for _, D in ipairs(LEVEL.doors) do
    if (D.a == a and D.b == b) or (D.a == b and D.b == a) then
      k = k + 1
      if k == (nth or 1) then
        if D.a == a then return D, D.pad_a, D.pad_b end
        return D, D.pad_b, D.pad_a
      end
    end
  end
end
local seen = {}
local function walk_to(x, z, limit)
  local t = 0
  while true do
    local d = bond.walk(0, x, z)
    local r = bond.room(0)
    if r then seen[LEVEL.room_name[r] or "?"] = true end
    if d and d < 55 then bond.walk_stop(); return true end
    t = t + coroutine.yield()
    bond.heal(0, 1, 1)
    if t > (limit or 25) then bond.walk_stop(); return false end
  end
end
local function padxz(id) local x, _, z = pads.pos(id); return x, z end
local function through(a, b, nth)
  local D, near, far = doorway(a, b, nth)
  if not D then return false, "no doorway " .. a .. "-" .. b end
  local x, z = padxz(near)
  if not walk_to(x, z) then return false, "before " .. D.name end
  if D.object then
    door.open(D.object)
    wait_for(4, function() local g = door.get(D.object); return g and g.open >= 0.9 end)
  end
  x, z = padxz(far)
  if not walk_to(x, z) then return false, "through " .. D.name end
  return true
end
local function route(name, steps, final_pad)
  local ok, why = true, nil
  for _, s in ipairs(steps) do
    if s[1] == "at" then ok = walk_to(s[2], s[3]); why = not ok and string.format("to (%d, %d)", s[2], s[3]) or nil
    else ok, why = through(s[1], s[2], s[3]) end
    if not ok then break end
  end
  if ok and final_pad then
    local x, z = padxz(LEVEL.pads[final_pad])
    ok = walk_to(x, z)
    if not ok then why = "to " .. final_pad end
  end
  local rn = bond.room(0)
  check("walk: " .. name, ok, (why or "") .. " ending in " .. tostring(rn and LEVEL.room_name[rn]))
  return ok
end

local function walk_checks()
  bond.warp(0, LEVEL.start_pad)
  wait(0.5)
  route("the place to the house vault's door, on foot", {
    { "place", "atrium" }, { "atrium", "salle_renaissance", 1 }, { "salle_renaissance", "salle_europe", 2 }, { "at", -500, 1300 }, { "at", -500, 3150 }, { "salle_europe", "stair_foot" },
    { "stair_foot", "north_corr_e" }, { "north_corr_e", "red_stairs" }, { "red_stairs", "vault_corr" }, { "vault_corr", "vault_ante" } }, "crack_point")
  local x, y, z = bond.pos(0)
  check("walk: Bond is down on the vault level, in the cracker's zone", bond.room(0) == LEVEL.rooms.vault_ante.id and y < -300
        and d2(x, z, HEIST.M.P.crack_xz[1], HEIST.M.P.crack_xz[2]) < 260, string.format("y %.0f", y))
  route("back up and the security stair core to the surveillance console (F1)", {
    { "vault_ante", "vault_corr" }, { "vault_corr", "red_stairs" }, { "red_stairs", "north_corr_e" }, { "north_corr_e", "core_e_0a" },
    { "core_e_0a", "core_e_0l" }, { "core_e_0l", "core_e_0b" }, { "core_e_0b", "surv_corr" }, { "surv_corr", "surveillance" } }, "console")
  local _, y2 = bond.pos(0)
  check("walk: Bond is up in the eye in the sky", bond.room(0) == LEVEL.rooms.surveillance.id and y2 > 400, string.format("y %.0f", y2))
  route("the surveillance room onto the catwalks over the Salle Europe", {
    { "surveillance", "catwalk_e" }, { "catwalk_e", "catwalk_n" }, { "catwalk_n", "catwalk_w" } }, "grapple_catwalk_high")
end

-- ---------------------------------------------------------------------------------------------------------------------------------
-- 5: the win path

local function win_path()
  local O, P = LEVEL.objects, HEIST.M.P
  check("phase casing", HEIST.phase == "casing", HEIST.phase)
  check("per-player keys: no card, the cage's outer door refuses", door.try(0, O.cage_outer.index) == false)
  take_card(0x2, "GREEN")
  bond.warp(0, approach("staff_corr", "cage_mantrap"))
  wait(0.5)
  check("GREEN opens the cage mantrap", door.try(0, O.cage_outer.index) == true)
  bond.warp(0, approach("staff_lounge", "pit_office"))
  wait(0.5)
  check("GREEN opens the pit office", door.try(0, O.pit_office_door.index) == true)
  take_card(0x4, "BLUE")
  bond.warp(0, approach("north_corr_e", "core_e_0a"))
  wait(0.5)
  check("BLUE opens the security stair core", door.try(0, O.core_door.index) == true)
  bond.warp(0, LEVEL.pads.console)
  wait(0.5)
  cctv.test_use(0)
  wait(0.5)
  local cam = cctv.get(cctv.list()[1])
  check("the surveillance console blinds every camera", HEIST.cam_off_until ~= nil and cam and cam.disabled)
  bond.warp(0, approach("exec_corr", "director"))
  wait(0.5)
  check("BLUE opens the director's office", door.try(0, O.director_door.index) == true)
  take_card(0x8, "RED")
  bond.warp(0, approach("north_corr_e", "red_stairs"))
  wait(0.5)
  check("RED opens the vault stairs", door.try(0, O.vault_stairs_door.index) == true)
  bond.warp(0, P.crack)
  wait(0.5)
  bond.equip(0, "doordecoder")
  wait(1)
  bond.test_gadget(0, "doordecoder")
  wait(0.5)
  check("the cracker starts at the house vault's door", HEIST.phase == "cracking", HEIST.phase)
  local opened = wait_for(30, function() return HEIST.phase == "open" end)
  check("the house vault opens", opened, HEIST.phase)
  wait(1.5)
  local _, counted = grab("vault")
  check("the house reserve collected", HEIST.bars >= 20 and counted >= 20, string.format("bars %d", HEIST.bars))
  if take_card(0x10, "GOLD") then
    bond.warp(0, LEVEL.pads.prive_safe_front)
    wait(0.5)
    check("GOLD opens the high-roller safe (Salon Prive)", door.try(0, O.house_safe.index) == true)
    local spilled = wait_for(4, function() for _, it in ipairs(HEIST.loot_items) do if it.where == "safe" then return true end end return false end)
    check("the safe's gold appears", spilled)
    grab("safe")
  end
  bond.warp(0, approach("cage", "chip_bank"))
  wait(0.5)
  check("GREEN opens the chip bank", door.try(0, O.chip_bank_door.index) == true)
  wait(1.5)
  local c0 = HEIST.cash
  grab("cell_green")
  check("cash from the chip bank", HEIST.cash > c0, string.format("%d -> %d", c0, HEIST.cash))
  bond.warp(0, P.exit_a)
  local won = wait_for(4, function() return ended.win ~= nil end)
  check("out at the limousine: the casino job wins", won and HEIST.ended and HEIST.score > 0, tostring(ended.win))
end

local finished = false
local function body()
  local ready = wait_for(20, function() return HEIST.ready end)
  check("mode ready on the casino", ready, "phase=" .. tostring(HEIST.phase) .. " map=" .. tostring(HEIST.map))
  wait(1.5)
  if ready then
    job_checks()
    walk_checks()
    win_path()
  end
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
