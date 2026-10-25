-- The Janus bank's headless test (designs/bank-heist.md P2.3, modding/levelgen.md). Run: tests/levelgen/run_bank.sh, with
-- tools/levelgen/janus_bank.json generated onto the slot (python3 tools/gen_level.py tools/levelgen/janus_bank.json, then build).
--   1 the layout: object counts, every double door's two leaves linked to each other
--   2 a double door: one leaf opened opens both and shows the room behind; closed, it culls it again
--   3 blocks: the teller counter stops Bond, the gap in it lets him through; a records shelf stops him
--   4 the full route on stick input (bond.walk): street, front door, hall (through the counter's flap), the GREEN offices, the staff
--     corridor, the BLUE security wing, records, the manager's office, down the RED stairs, the vault antechamber, the vault, the
--     deposit hall and a cell, up the service ramp, the dock, the padlocked shutter to the alley, back to the van. Every room in order,
--     and his height on the stairs and the ramp follows the slope
--   5 guards route between areas: records -> staff corridor -> the hall's customer side (round the counter through its flap);
--     the deposit hall -> up the service ramp -> the dock; the antechamber -> up the stairs -> the staff corridor
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
local slope = {}          -- { room, x, z, y, floor } samples on the stairs and the ramp
local function track()
  local r = bond.room(0)
  if r and rooms_seen[#rooms_seen] ~= r then rooms_seen[#rooms_seen + 1] = r end
  local n = r and LEVEL.room_name[r]
  if n == "stairs" or n == "service_ramp" then
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
    if d and d < 40 then bond.walk_stop(); return true end
    t = t + coroutine.yield()
    bond.heal(0, 1, 1)
    if t > (limit or 20) then bond.walk_stop(); return false end
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

local ROUTE = {
  { "door", "street", "vestibule" },
  { "door", "vestibule", "hall", 1 },
  { "at", 700, 100 }, { "at", 1000, 330 }, { "pad", "flap_s" }, { "pad", "flap_n" },
  { "door", "hall", "office_corr" },
  { "door", "office_corr", "office_a" }, { "pad", "office_a_mid" }, { "door", "office_a", "office_corr" },
  { "door", "office_corr", "back_corr" },
  { "door", "back_corr", "hall" },
  { "door", "hall", "sec_office" },
  { "door", "sec_office", "guard_room" }, { "door", "guard_room", "sec_office" },
  { "door", "sec_office", "hall" },
  { "door", "hall", "back_corr" },
  { "door", "back_corr", "records" }, { "at", -660, 1400 }, { "at", -660, 1640 }, { "pad", "rec_aisle" },
  { "at", -660, 1640 }, { "at", -660, 1400 }, { "door", "records", "back_corr" },
  { "door", "back_corr", "manager" }, { "pad", "safe_front" }, { "door", "manager", "back_corr" },
  { "door", "back_corr", "stairs" }, { "door", "stairs", "ante_a" },
  { "pad", "crack" }, { "door", "ante_a", "vault" }, { "at", 900, 3600 },
  { "door", "vault", "deposit" }, { "door", "deposit", "cell_1" }, { "door", "cell_1", "deposit" },
  { "door", "deposit", "service_ramp" }, { "door", "service_ramp", "dock" },
  { "door", "dock", "alley" }, { "door", "alley", "street" }, { "pad", "van" },
}
local WANT = "street>vestibule>hall>office_corr>office_a>office_corr>back_corr>hall>sec_office>guard_room>sec_office>hall>back_corr>" ..
             "records>back_corr>manager>back_corr>stairs>ante_a>vault>deposit>cell_1>deposit>service_ramp>dock>alley>street"

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
    bx, _, bz = bond.pos(0)                -- an unarmed chaser's blows push Bond about: measure to where he is now
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

local function body()
  wait(1.5)

  -- 1: the layout
  check("layout_counts", #LEVEL.cameras == 7 and #LEVEL.alarms == 6 and #LEVEL.monitors == 10 and #LEVEL.safes == 1 and
    #LEVEL.padlocks == 1 and #LEVEL.room_name == 20 and #LEVEL.doors == 23,
    string.format("cameras %d alarms %d monitors %d safes %d padlocks %d rooms %d doorways %d", #LEVEL.cameras, #LEVEL.alarms,
      #LEVEL.monitors, #LEVEL.safes, #LEVEL.padlocks, #LEVEL.room_name, #LEVEL.doors))
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
  -- the heist mode's map (tools/heist_bank_map.lua, P2.4): every name it asks the layout for resolves
  local okm, M = pcall(heist_bank_map)
  local n_area_pads, n_cams = 0, 0
  if okm then
    for _, a in ipairs(M.AREAS) do n_area_pads = n_area_pads + #a.pads end
    for _ in pairs(M.CAM_LABEL) do n_cams = n_cams + 1 end
  end
  check("heist_map_resolves", okm and #M.DOORS.green == 3 and #M.DOORS.red == 3 and #M.DOORS.vault_a == 2 and #M.P.monitors == 8 and
    n_cams == 7 and n_area_pads > 150 and M.P.crack_xz[1] ~= nil,   -- (P2.4: grid pads by furniture and doorways are kept off the posts)
    okm and string.format("doors green %d blue %d red %d vault %d/%d, %d area pads (%d grid pads kept off), %d cameras, crack at (%.0f, %.0f)",
      #M.DOORS.green, #M.DOORS.blue, #M.DOORS.red, #M.DOORS.vault_a, #M.DOORS.vault_b, n_area_pads, M.dropped or -1, n_cams, M.P.crack_xz[1],
      M.P.crack_xz[2]) or tostring(M))

  -- 2: a double door culls and shows the stairs; one leaf opened opens both
  local red = LEVEL.objects.red_stairs
  bond.warp(0, LEVEL.pads.back_3, 450, 45)              -- in front of the RED doors (x 900 on the corridor's north wall)
  face = { 900, 1290 }
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
  shot("red_stairs_open")
  door.close(red.index)
  wait(2.5)
  ga, gb = door.get(red.index), door.get(red.pair)
  s = drawn()
  check("double_door_closes_both", ga.open < 0.05 and gb.open < 0.05 and not s[room_id("stairs")],
    string.format("open %.2f/%.2f, stairs drawn %s", ga.open, gb.open, tostring(s[room_id("stairs")] == true)))

  -- 3: blocks
  bond.warp(0, LEVEL.pads.cust_2)
  wait(0.5)
  walk_to({ 800, 650 }, 3)
  local x, _, z = bond.pos(0)
  check("counter_blocks", bond.room(0) == room_id("hall") and z < 420, string.format("at (%.0f, %.0f) after 3 s walking at the counter", x, z))
  face = { 700, 900 }
  wait(0.3)
  shot("hall_counter")
  local ok = walk_to(pad("flap_s"), 8) and walk_to(pad("flap_n"), 6)
  x, _, z = bond.pos(0)
  check("counter_flap_passes", ok and z > 500, string.format("at (%.0f, %.0f)", x, z))
  local RD, _, rin = doorway("back_corr", "records")
  bond.warp(0, rin)                                     -- (-1100, 1400): inside the records door, in front of shelf 1
  wait(1.0)                                             -- (a warped Bond keeps his last walk's momentum for a moment)
  local sx, _, sz = bond.pos(0)
  walk_to({ sx, sz + 300 }, 3)                          -- straight north, into the shelf (z 1500..1560)
  x, _, z = bond.pos(0)
  check("shelf_blocks", bond.room(0) == room_id("records") and sz < 1500 and z < 1500 and sx > -1540 and sx < -820,
    string.format("from (%.0f, %.0f) to (%.0f, %.0f)", sx, sz, x, z))

  -- 4: the full route from the street
  bond.warp(0, LEVEL.start_pad)
  wait(0.8)
  rooms_seen, slope = {}, {}
  track()
  local t0 = 0
  local all = true
  for i, st in ipairs(ROUTE) do
    if not step(st) then all = false; break end
    if st[1] == "door" and st[3] == "ante_a" then
      face = { 900, 1290 }
      wait(0.4)
      shot("stairs_from_bottom")
    end
  end
  local seq = {}
  for _, r in ipairs(rooms_seen) do seq[#seq + 1] = rname(r) end
  x, _, z = bond.pos(0)
  check("bank_walk", all and table.concat(seq, ">") == WANT, string.format("rooms %s%s, now at (%.0f, %.0f)", table.concat(seq, ">"),
    all and "" or (" STUCK " .. tostring(stuck)), x, z))
  local worst, n_st, n_rp, lo = 0, 0, 0, 0
  for _, p in ipairs(slope) do
    worst = math.max(worst, math.abs(p[4] - p[5]))
    if p[1] == "stairs" then n_st = n_st + 1 else n_rp = n_rp + 1 end
    lo = math.min(lo, p[4])
  end
  check("ramp_walk_heights", n_st >= 8 and n_rp >= 8 and worst < 16 and lo < -250,
    string.format("%d samples on the stairs, %d on the ramp, worst |y - floor| %.1f, lowest y %.0f", n_st, n_rp, worst, lo))
  face = { 700, -800 }
  wait(0.4)
  shot("street_back_at_van")

  -- 5: guards route between areas (the doors on their way are unlocked and shut: they open them)
  bond.warp(0, LEVEL.pads.cust_2)
  face = { 1325, 420 }
  wait(0.5)
  local g = guard_route("guard_records_to_hall", "rec_3", "records>back_corr>hall", 60, function(gx, gz) return gz < 420 end)
  if g then shot("hall_guard_arrived"); spawn.remove(g) end
  door.unlock(LEVEL.objects.red_ramp.index)
  bond.warp(0, LEVEL.pads.dock_mid)
  face = { 1900, 2060 }
  wait(0.5)
  g = guard_route("guard_up_the_ramp", "dep_3", "deposit>service_ramp>dock", 60)
  if g then spawn.remove(g) end
  door.unlock(red.index)
  door.unlock(red.pair)
  bond.warp(0, LEVEL.pads.back_3)
  face = { 900, 1290 }
  wait(0.5)
  g = guard_route("guard_up_the_stairs", "ante_1", "ante_a>stairs>back_corr", 60)
  if g then spawn.remove(g) end

  print(string.format("LEVELGEN DONE %d passed, %d failed", passed, failed))
  mission.fail("levelgen bank test done")
end

local co = coroutine.wrap(body)
local finished = false
on("tick", function(dt)
  if finished then return end
  local ok, err = pcall(co, dt)
  if not ok then finished = true; print("LEVELGEN error: " .. tostring(err)); print(string.format("LEVELGEN DONE %d passed, %d failed", passed, failed + 1)) end
end)
