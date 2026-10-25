-- The Janus bank v2's headless test (designs/bank-heist.md section 17, modding/levelgen.md). Run: tests/levelgen/run_bank.sh, with
-- tools/levelgen/janus_bank.json generated onto the slot (python3 tools/gen_level.py tools/levelgen/janus_bank.json, then build).
--   1 the layout: counts (rooms, doorways, openings, cameras ...), every double door's two leaves linked, the heist map resolving
--   2 a double door: one leaf opened opens both and shows the room behind; closed, it culls it again
--   3 openings: from the hall the galleries and the upper windows are drawn through the hall's walls, and from a gallery the hall;
--     nobody walks through one (a gallery's rail stops Bond)
--   4 blocks: the teller counter stops Bond, its flap lets him through; a records shelf stops him
--   5 the full route on stick input (bond.walk), every room in order: street, the hall, the GREEN offices, the staff corridor, the grand
--     stair up to the mezzanine, the first floor, the east stair to the second floor, the library and the loggia, up the stair core to
--     the executive floor and the roof, down the core to the ground floor, the BLUE security wing, the RED stairs to the vault level, the
--     vault, the deposit hall, up the service ramp to the dock, the alley, the street; then the plant level: down from the vault corridor,
--     the power room and the generator, the maintenance tunnel to the canal (its padlocked grate), up the embankment to the street. His
--     height on every stair and ramp follows the slope
--   6 guards route between areas and floors: records -> the hall (round the counter through its flap); the deposit hall -> up the
--     service ramp -> the dock; the antechamber -> up the RED stairs; the executive floor -> down the stair core (three storeys) -> the
--     staff corridor
-- Prints "LEVELGEN PASS|FAIL <name> ...", "LEVELGEN SHOT <name>" before each screenshot, and "LEVELGEN DONE <n> passed, <m> failed".
include("levelgen_layout")
include("heist_bank_map")             -- include() works at a script's top level only
mods.set("invincible", true)
mode.keep_locks()

local passed, failed = 0, 0
local function check(name, ok, info)
  if ok then passed = passed + 1 else failed = failed + 1 end
  print(string.format("LEVELGEN %s %s %s", ok and "PASS" or "FAIL", name, info or ""))
end

local face = nil
local function wait(sec)
  local t = 0
  while t < sec do
    t = t + coroutine.yield()
    bond.heal(0, 1, 1)
    if face then bond.face(0, face[1], face[2]) end
  end
end

local function shot(name)
  print("LEVELGEN SHOT " .. name)
  cctv.test_shot()
  wait(0.2)
end

local function room_id(name) return LEVEL.rooms[name].id end
local function rname(id) return LEVEL.room_name[id] or tostring(id) end
local function pad(name) local x, _, z = pads.pos(LEVEL.pads[name]); return { x, z } end
local function padxz(id) local x, _, z = pads.pos(id); return { x, z } end
local function drawn()
  local s = {}
  for _, r in ipairs(bond.rooms_drawn(0)) do s[r] = true end
  return s
end
local function count(t) local n = 0; for _ in pairs(t) do n = n + 1 end; return n end

-- the floor height of a room at (x, z), from LEVEL (level, or a ramp's slope)
local function floor_at(name, x, z)
  local R = LEVEL.rooms[name]
  if not R.ramp then return R.floor end
  local lo, hi, c = R.rect[2], R.rect[4], z
  if R.ramp.axis == "x" then lo, hi, c = R.rect[1], R.rect[3], x end
  local t = math.max(0, math.min(1, (c - lo) / (hi - lo)))
  return R.floor + (R.ramp.to - R.floor) * t
end

-- the doorway between two rooms (the nth if there are several), and the side of it each room's approach pad is on
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

-- route tracking
local rooms_seen = {}
local slope = {}          -- { room, x, z, y, floor } samples on every ramp room
local function track()
  local r = bond.room(0)
  if r and rooms_seen[#rooms_seen] ~= r then rooms_seen[#rooms_seen + 1] = r end
  local n = r and LEVEL.room_name[r]
  if n and LEVEL.rooms[n].ramp then
    local x, y, z = bond.pos(0)
    slope[#slope + 1] = { n, x, z, y, floor_at(n, x, z) }
  end
end

-- walk Bond to a point {x, z} on stick input; true when reached
local function walk_to(p, limit)
  face = nil
  local t = 0
  while true do
    local d = bond.walk(0, p[1], p[2])
    track()
    if d and d < 55 then bond.walk_stop(); return true end     -- (40 left Bond circling a pad he came at sharply, short of it)
    t = t + coroutine.yield()
    bond.heal(0, 1, 1)
    if t > (limit or 25) then bond.walk_stop(); return false end
  end
end

local function open_door(D)
  if not D.object then return end
  door.open(D.object)
  local t = 0
  while t < 4 do
    local g = door.get(D.object)
    if g and g.open >= 0.9 then break end
    t = t + coroutine.yield()
  end
end

-- one route step: {"door", from, to [, nth]} walks through that doorway (opening its door first), {"pad", name} or {"at", x, z}
local stuck = nil
local function step(s)
  if s[1] == "pad" then
    if not walk_to(pad(s[2])) then stuck = "pad " .. s[2]; return false end
  elseif s[1] == "at" then
    if not walk_to({ s[2], s[3] }) then stuck = string.format("point (%d, %d)", s[2], s[3]); return false end
  else
    local D, near, far = doorway(s[2], s[3], s[4])
    if not D then stuck = "no doorway " .. s[2] .. "-" .. s[3]; return false end
    if not walk_to(padxz(near)) then stuck = "before " .. D.name; return false end
    if D.padlock then obj.test_damage(D.padlock, 1000, 0); wait(0.3) end
    open_door(D)
    if not walk_to(padxz(far)) then stuck = "through " .. D.name; return false end
  end
  return true
end

local function D(a, b, n) return { "door", a, b, n } end
-- the building, top to bottom, on foot
local ROUTE_A = {
  D("street", "vestibule"), D("vestibule", "hall", 1),
  { "pad", "cust_4" }, { "pad", "hall_mid" }, { "pad", "cust_2" }, { "pad", "flap_s" }, { "pad", "flap_n" },
  D("hall", "office_corr"), D("office_corr", "corr_w"), D("corr_w", "office_a"), { "pad", "office_a_mid" }, D("office_a", "corr_w"),
  D("corr_w", "office_corr"), D("office_corr", "back_corr_w"), D("back_corr_w", "stair_foot"), D("stair_foot", "stair_grand"),
  D("stair_grand", "mezz"), D("mezz", "corr1_rear_e"), D("corr1_rear_e", "corr1_e"), D("corr1_e", "corr1_ne"), D("corr1_ne", "stair_e"),
  D("stair_e", "corr2_rear"), D("corr2_rear", "library"), { "at", 350, 4400 }, { "at", 350, 3550 }, D("library", "corr2_n", 2),
  D("corr2_n", "corr2_nw"),
  D("corr2_nw", "core_w_2a"), D("core_w_2a", "core_w_2l"), D("core_w_2l", "core_w_2b"), D("core_w_2b", "corr3"), D("corr3", "roof"),
  { "at", 1300, 2500 }, { "pad", "roof_helipad" }, { "at", 1300, 2500 }, D("roof", "corr3"), D("corr3", "core_w_2b"), D("core_w_2b", "core_w_2l"), D("core_w_2l", "core_w_2a"),
  D("core_w_2a", "corr2_nw"), D("corr2_nw", "core_w_1b"), D("core_w_1b", "core_w_1l"), D("core_w_1l", "core_w_1a"), D("core_w_1a", "corr1_nw"),
  D("corr1_nw", "core_w_0b"), D("core_w_0b", "core_w_0l"), D("core_w_0l", "core_w_0a"), D("core_w_0a", "back_corr_w"),
  D("back_corr_w", "hall"), { "pad", "flap_n" }, { "pad", "flap_s" }, { "pad", "flap2_s" }, { "pad", "flap2_n" }, D("hall", "sec_office"), D("sec_office", "back_corr_e"), D("back_corr_e", "stairs"), D("stairs", "b1_lobby"),
  D("b1_lobby", "vault_corr"), D("vault_corr", "ante_a"), { "pad", "crack" }, D("ante_a", "vault"), { "pad", "vault_g2" },
  D("vault", "deposit"), D("deposit", "cell_1"), D("cell_1", "deposit"), D("deposit", "cash_processing"), { "at", 3800, 7050 }, D("cash_processing", "service_ramp"),
  D("service_ramp", "dock"), D("dock", "alley"), D("alley", "street"), { "pad", "van" },
}
local WANT_A = "street>vestibule>hall>office_corr>corr_w>office_a>corr_w>office_corr>back_corr_w>stair_foot>stair_grand>mezz>" ..
  "corr1_rear_e>corr1_e>corr1_ne>stair_e>corr2_rear>library>corr2_n>corr2_nw>core_w_2a>core_w_2l>core_w_2b>corr3>roof>corr3>" ..
  "core_w_2b>core_w_2l>core_w_2a>corr2_nw>core_w_1b>core_w_1l>core_w_1a>corr1_nw>core_w_0b>core_w_0l>core_w_0a>back_corr_w>hall>" ..
  "sec_office>back_corr_e>stairs>b1_lobby>vault_corr>ante_a>vault>deposit>cell_1>deposit>cash_processing>service_ramp>dock>alley>street"
-- the plant level and the tunnel: from the vault corridor down to B2, out along the tunnel to the canal and up to the street; and back
-- in by the maintenance stair to the canteen
local ROUTE_B = {
  D("vault_corr", "stair_b2"), D("stair_b2", "b2_corr"), D("b2_corr", "power"), { "pad", "breaker_front" }, { "pad", "power_mid" }, D("power", "generator"),
  { "pad", "generator_front" }, D("generator", "power"), D("power", "b2_corr"), D("b2_corr", "tunnel_1"), D("tunnel_1", "tunnel_2"),
  D("tunnel_2", "canal"), { "pad", "canal_mid" }, D("canal", "embankment"), D("embankment", "street"),
}
local WANT_B = "vault_corr>stair_b2>b2_corr>power>generator>power>b2_corr>tunnel_1>tunnel_2>canal>embankment>street"
local ROUTE_C = { D("b2_corr", "maint_stair"), D("maint_stair", "canteen"), D("canteen", "back_corr_w") }
local WANT_C = "b2_corr>maint_stair>canteen>back_corr_w"

local function run_route(name, route, want, from_pad)
  bond.warp(0, LEVEL.pads[from_pad])
  wait(0.8)
  rooms_seen = {}
  track()
  local all = true
  for k, st in ipairs(route) do
    if not step(st) then all = false; break end
    if st[1] == "door" then print(string.format("LEVELGEN step %s %d: %s -> %s", name, k, st[2], st[3])) end
  end
  local seq = {}
  for _, r in ipairs(rooms_seen) do seq[#seq + 1] = rname(r) end
  local x, _, z = bond.pos(0)
  check(name, all and table.concat(seq, ">") == want, string.format("rooms %s%s, now at (%.0f, %.0f)", table.concat(seq, ">"),
    all and "" or (" STUCK " .. tostring(stuck)), x, z))
end

-- an unarmed guard from pad `from` routes to Bond; true when he reaches him through the rooms `want`
local function guard_route(name, from, want, limit, extra)
  local g = spawn.chr(LEVEL.pads[from], { weapon = "none", behaviour = "chase" })
  if not g then check(name, false, "no spawn at " .. from); return end
  local seq, t, gx, gz = {}, 0, nil, nil
  local bx, _, bz = bond.pos(0)
  local target = room_id(want:match("([^>]+)$"))
  while t < limit do
    local r = spawn.room(g)
    if r and seq[#seq] ~= r then seq[#seq + 1] = r end
    local x, z = spawn.where(g)
    if not x then break end
    gx, gz = x, z
    bx, _, bz = bond.pos(0)
    if r == target and math.sqrt((x - bx) ^ 2 + (z - bz) ^ 2) < 300 then break end
    t = t + coroutine.yield()
    bond.heal(0, 1, 1)
  end
  local names = {}
  for _, r in ipairs(seq) do names[#names + 1] = rname(r) end
  local got = table.concat(names, ">")
  local ok = got == want and spawn.room(g) == target
  if ok and extra then ok = extra(gx, gz) end
  ok = ok and t < limit
  check(name, ok, string.format("guard rooms %s in %.1f s (want %s), at (%.0f, %.0f), Bond at (%.0f, %.0f)", got, t, want, gx or 0, gz or 0,
    bx, bz))
  face = { gx or bx, gz or bz }
  wait(0.4)
  return g
end

-- the test runs in parts, one process each (run_bank.sh starts them side by side: a walk round six levels is long): the static checks
-- (1-4), route A (GE_BANK_ROUTE_A), routes B and C (GE_BANK_ROUTE_B), the guards (GE_BANK_GUARDS)
local PART = (mode.env_flag("GE_BANK_ROUTE_A") and "a") or (mode.env_flag("GE_BANK_ROUTE_B") and "b") or
             (mode.env_flag("GE_BANK_GUARDS") and "g") or "static"

local function ramp_check(name, want_rooms, lo_max, hi_min)
  local worst, lo, hi, per = 0, 1e9, -1e9, {}
  for _, p in ipairs(slope) do
    worst = math.max(worst, math.abs(p[4] - p[5]))
    per[p[1]] = (per[p[1]] or 0) + 1
    lo, hi = math.min(lo, p[4]), math.max(hi, p[4])
  end
  local ramps = {}
  for n, k in pairs(per) do ramps[#ramps + 1] = n .. " " .. k end
  table.sort(ramps)
  check(name, count(per) >= want_rooms and worst < 16 and lo < lo_max and hi > hi_min,
    string.format("%d ramp rooms walked (%s), worst |y - floor| %.1f, y %.0f..%.0f", count(per), table.concat(ramps, ", "), worst, lo, hi))
end

local guards               -- (part g, below)

-- the end of a part: the mission ends and the game quits (a mission end alone keeps the game running to GE_FRAMES)
local function finish()
  mission.fail("levelgen bank test done")
  wait(0.5)
  bench.exit()
end

local function body()
  wait(1.5)
  if PART == "a" then
    slope = {}
    run_route("bank_walk", ROUTE_A, WANT_A, "start")
    ramp_check("ramp_walk_heights_a", 10, -350, 1400)
    print(string.format("LEVELGEN DONE %d passed, %d failed", passed, failed))
    finish()
    return
  elseif PART == "b" then
    slope = {}
    run_route("plant_and_tunnel_walk", ROUTE_B, WANT_B, "vcorr_w")
    run_route("maintenance_stair_walk", ROUTE_C, WANT_C, "b2_w")
    ramp_check("ramp_walk_heights_b", 3, -800, -50)
    print(string.format("LEVELGEN DONE %d passed, %d failed", passed, failed))
    finish()
    return
  elseif PART == "g" then
    return guards()
  end

  -- 1: the layout
  local nopen = LEVEL.openings and #LEVEL.openings or 0
  check("layout_counts", #LEVEL.room_name == 107 and #LEVEL.doors == 154 and nopen == 28 and #LEVEL.cameras == 27 and #LEVEL.alarms == 13 and
    #LEVEL.monitors == 11 and #LEVEL.safes == 2 and #LEVEL.padlocks == 2,
    string.format("rooms %d doorways %d openings %d cameras %d alarms %d monitors %d safes %d padlocks %d", #LEVEL.room_name, #LEVEL.doors,
      nopen, #LEVEL.cameras, #LEVEL.alarms, #LEVEL.monitors, #LEVEL.safes, #LEVEL.padlocks))
  local zones = { public = 0, staff = 0, restricted = 0 }
  local untagged = {}
  for id = 1, #LEVEL.room_name do
    local R = LEVEL.rooms[LEVEL.room_name[id]]
    if R.zone and zones[R.zone] then zones[R.zone] = zones[R.zone] + 1 else untagged[#untagged + 1] = LEVEL.room_name[id] end
    if not R.area then untagged[#untagged + 1] = LEVEL.room_name[id] .. "(area)" end
  end
  check("every_room_zoned", #untagged == 0 and zones.public > 10 and zones.staff > 30 and zones.restricted > 25,
    string.format("public %d staff %d restricted %d, untagged %s", zones.public, zones.staff, zones.restricted, table.concat(untagged, ",")))
  local pairs_ok, info = true, {}
  for _, n in ipairs({ "front_door", "green_hall", "blue_hall", "red_stairs", "vault_door_a", "vault_door_b" }) do
    local o = LEVEL.objects[n]
    local a, b = o and door.get(o.index), o and o.pair and door.get(o.pair)
    local ok = a and b and a.linked == o.pair and b.linked == o.index and a.live and b.live
    pairs_ok = pairs_ok and ok
    info[#info + 1] = string.format("%s %s/%s linked %s/%s", n, tostring(o and o.index), tostring(o and o.pair),
      tostring(a and a.linked), tostring(b and b.linked))
  end
  check("double_doors_linked", pairs_ok, table.concat(info, "; "))
  local okm, M = pcall(heist_bank_map)
  local n_area_pads, n_cams = 0, 0
  if okm then
    for _, a in ipairs(M.AREAS) do n_area_pads = n_area_pads + #a.pads end
    n_cams = count(M.CAM_LABEL)
  end
  check("heist_map_resolves", okm and #M.AREAS == 14 and #M.DOORS.green >= 3 and #M.DOORS.red >= 3 and #M.DOORS.vault_a == 2 and
    #M.P.monitors == 8 and n_cams == 27 and n_area_pads > 600 and M.P.crack_xz[1] ~= nil and M.P.breaker ~= nil and M.P.power_to ~= nil and
    M.AREA_CLASS.vault_ante == "restricted" and M.AREA_CLASS.hall == "public" and M.AREA_CLASS.offices == "staff",
    okm and string.format("areas %d, doors green %d blue %d red %d vault %d/%d, %d area pads (%d grid pads kept off), %d cameras, crack at (%.0f, %.0f), breaker %s",
      #M.AREAS, #M.DOORS.green, #M.DOORS.blue, #M.DOORS.red, #M.DOORS.vault_a, #M.DOORS.vault_b, n_area_pads, M.dropped or -1, n_cams,
      M.P.crack_xz[1], M.P.crack_xz[2], tostring(M.P.breaker and M.P.breaker[1])) or tostring(M))

  -- 2: a double door culls and shows the room behind; one leaf opened opens both
  local red = LEVEL.objects.red_stairs
  local RD, rnear = doorway("back_corr_e", "stairs")
  bond.warp(0, rnear)
  face = { RD.x, RD.z + 300 }
  wait(0.8)
  local s = drawn()
  local ga, gb = door.get(red.index), door.get(red.pair)
  check("double_door_shut_culls", ga.state == "closed" and gb.state == "closed" and not s[room_id("stairs")],
    string.format("leaves %s/%s, stairs drawn %s", ga.state, gb.state, tostring(s[room_id("stairs")] == true)))
  door.open(red.index)
  wait(2.0)
  ga, gb = door.get(red.index), door.get(red.pair)
  s = drawn()
  check("double_door_one_opens_both", ga.open >= 0.9 and gb.open >= 0.9 and s[room_id("stairs")] == true,
    string.format("open %.2f/%.2f, stairs drawn %s", ga.open, gb.open, tostring(s[room_id("stairs")] == true)))
  door.close(red.index)
  wait(2.5)
  ga, gb = door.get(red.index), door.get(red.pair)
  s = drawn()
  check("double_door_closes_both", ga.open < 0.05 and gb.open < 0.05 and not s[room_id("stairs")],
    string.format("open %.2f/%.2f, stairs drawn %s", ga.open, gb.open, tostring(s[room_id("stairs")] == true)))

  -- 3: openings. From the middle of the hall, looking west: the west arcade (through its arches), the west gallery (through the
  -- balcony openings) and the second floor's corridor (through its windows) are all drawn
  bond.warp(0, LEVEL.pads.hall_post_ne)                 -- (1000, 1900): far enough across the hall to see the top tier's windows
  face = { -1200, 1050 }
  wait(1.0)
  s = drawn()
  check("hall_sees_tiers", s[room_id("aisle_w")] and s[room_id("gallery_w")] and s[room_id("corr2_w")],
    string.format("aisle_w %s gallery_w %s corr2_w %s (%d rooms drawn)", tostring(s[room_id("aisle_w")] == true),
      tostring(s[room_id("gallery_w")] == true), tostring(s[room_id("corr2_w")] == true), count(s)))
  shot("hall_west_tiers")
  bond.warp(0, LEVEL.pads.gal_w_mid)
  face = { 1200, 1050 }
  wait(1.0)
  s = drawn()
  check("gallery_sees_hall", s[room_id("hall")] and s[room_id("gallery_e")] == true,
    string.format("hall %s, gallery_e across the hall %s (%d rooms drawn)", tostring(s[room_id("hall")] == true),
      tostring(s[room_id("gallery_e")] == true), count(s)))
  shot("gallery_w_over_hall")
  local gx0, _, gz0 = bond.pos(0)
  walk_to({ 0, gz0 }, 3)                                -- east, into the balcony's rail
  local x, y, z = bond.pos(0)
  check("rail_stops_bond", bond.room(0) == room_id("gallery_w") and x < -1240 and y > 400,
    string.format("from (%.0f, %.0f) to (%.0f, %.0f) y %.0f", gx0, gz0, x, z, y))
  bond.warp(0, LEVEL.pads.vest_mid)
  face = { 0, -2800 }
  wait(1.0)
  bond.warp(0, LEVEL.pads.gal_s_w)
  face = { -760, -2000 }
  wait(1.0)
  s = drawn()
  check("facade_window_sees_street", s[room_id("street")] == true, string.format("street drawn %s", tostring(s[room_id("street")] == true)))

  -- 4: blocks
  bond.warp(0, LEVEL.pads.cust_2)
  wait(0.5)
  walk_to({ -680, 2500 }, 3)
  x, _, z = bond.pos(0)
  check("counter_blocks", bond.room(0) == room_id("hall") and z < 2200, string.format("at (%.0f, %.0f) after 3 s walking at the counter", x, z))
  local ok = walk_to(pad("flap_s"), 8) and walk_to(pad("flap_n"), 6)
  x, _, z = bond.pos(0)
  check("counter_flap_passes", ok and z > 2280, string.format("at (%.0f, %.0f)", x, z))
  bond.warp(0, LEVEL.pads.rec_aisle)                    -- (-3400, 2600): between shelves 3 and 4
  wait(1.0)
  local sx, _, sz = bond.pos(0)
  walk_to({ sx + 400, sz }, 3)                          -- straight east, into shelf 4 (x -3250..-3190)
  x, _, z = bond.pos(0)
  check("shelf_blocks", bond.room(0) == room_id("records") and x < -3250, string.format("from (%.0f, %.0f) to (%.0f, %.0f)", sx, sz, x, z))

  -- (5, the routes: parts a and b)
  print(string.format("LEVELGEN DONE %d passed, %d failed", passed, failed))
  finish()
end

-- 6: guards route between areas and floors (the doors on their way are unlocked and shut: they open them)
guards = function()
  local red = LEVEL.objects.red_stairs
  bond.warp(0, LEVEL.pads.cust_6)                       -- (300, 700): out on the customer side, well away from the counter
  face = { -825, 2200 }
  wait(0.5)
  local g = guard_route("guard_records_to_hall", "rec_1", "records>office_corr>hall", 60, function(gx, gz) return gz < 2200 end)
  if g then shot("hall_guard_arrived"); spawn.remove(g) end
  door.unlock(LEVEL.objects.red_ramp.index)
  bond.warp(0, LEVEL.pads.dock_1)                       -- (3550, 4850): just inside the RED shutter (a shutter falls again behind him)
  wait(0.5)
  g = guard_route("guard_up_the_ramp", "dep_3", "deposit>cash_processing>service_ramp>dock", 90)
  if g then spawn.remove(g) end
  door.unlock(red.index)
  door.unlock(red.pair)
  bond.warp(0, LEVEL.pads.back_e3)
  wait(0.5)
  g = guard_route("guard_up_the_stairs", "ante_1", "ante_a>vault_corr>b1_lobby>stairs>back_corr_e", 90)
  if g then spawn.remove(g) end
  bond.warp(0, LEVEL.pads.back_1)
  wait(0.5)
  g = guard_route("guard_down_the_core", "exec_w", "corr3>core_w_2b>core_w_2l>core_w_2a>corr2_nw>core_w_1b>core_w_1l>core_w_1a>corr1_nw>" ..
    "core_w_0b>core_w_0l>core_w_0a>back_corr_w", 120)
  if g then spawn.remove(g) end

  print(string.format("LEVELGEN DONE %d passed, %d failed", passed, failed))
  finish()
end

local co = coroutine.wrap(body)
local finished = false
on("tick", function(dt)
  if finished then return end
  local ok, err = pcall(co, dt)
  if not ok then finished = true; print("LEVELGEN error: " .. tostring(err)); print(string.format("LEVELGEN DONE %d passed, %d failed", passed, failed + 1)) end
end)
