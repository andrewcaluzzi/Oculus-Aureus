-- The generated level's headless test (designs/bank-heist.md P2.1, modding/levelgen.md). Run: tests/levelgen/run.sh.
-- On the layout compiled onto the slot (lshape.json: rooms A, B, C in an L; testbox.json: A and B; the checks that need C are skipped):
--   1 culling: Bond in A facing away from the doorway draws only A; facing the doorway draws A and B, and not C (two walls away)
--   2 Bond WALKS (real stick input, test.walk) from A through the doorway into B: bond.room goes A -> B, never jumps
--   3 from B: A is drawn looking back through the doorway; C through the B-C doorway
--   4 Bond walks back to A; an unarmed guard spawned in the far room routes to him through the doorways (spawn.room: C -> B -> A)
-- Prints "LEVELGEN PASS|FAIL <name> ..." per check, "LEVELGEN SHOT <name>" before each screenshot, and "LEVELGEN DONE <n> passed, <m> failed".
include("levelgen_layout")
mods.set("invincible", true)

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
    if face then test.face(0, face[1], face[2]) end
  end
end

local function rid(name) return LEVEL.rooms[name] and LEVEL.rooms[name].id end
local A, B, C = rid("A"), rid("B"), rid("C")
local D0, D1 = LEVEL.doors[1], LEVEL.doors[2]

local function drawn()
  local s, l = {}, {}
  for _, r in ipairs(test.rooms_drawn(0)) do s[r] = true; l[#l + 1] = tostring(r) end
  return s, table.concat(l, ",")
end

local function shot(name)
  print("LEVELGEN SHOT " .. name)
  test.screenshot()
  wait(0.2)
end

local rooms_seen = {}
local function track_bond()
  local r = bond.room(0)
  if r and rooms_seen[#rooms_seen] ~= r then rooms_seen[#rooms_seen + 1] = r end
end

-- walk Bond through a list of points {x, z}; true if each was reached in time
local function walk(points, limit)
  face = nil
  for _, p in ipairs(points) do
    local t = 0
    while true do
      local d = test.walk(0, p[1], p[2])
      track_bond()
      if d and d < 45 then break end
      t = t + coroutine.yield()
      if t > (limit or 15) then test.walk_stop(); return false, p end
    end
  end
  test.walk_stop()
  return true
end

local function padxz(id) local x, _, z = pads.pos(id); return { x, z } end

local function body()
  wait(1.5)
  local bx, _, bz = bond.pos(0)
  check("start_room", bond.room(0) == A, string.format("bond.room %s at (%.0f, %.0f), A = %s", tostring(bond.room(0)), bx, bz, tostring(A)))

  -- 1: culling from A
  face = { bx + (bx - D0.x), bz + (bz - D0.z) }          -- straight away from the doorway
  wait(0.6)
  local s, l = drawn()
  check("cull_facing_away", s[A] and not s[B] and (not C or not s[C]), "drawn {" .. l .. "}")
  shot("a_facing_away")
  face = { D0.x, D0.z }
  wait(0.6)
  s, l = drawn()
  check("door_shows_b", s[A] and s[B], "drawn {" .. l .. "}")
  if C then check("cull_c_behind_two_walls", not s[C], "drawn {" .. l .. "} C = " .. C) end
  shot("a_looking_through_doorway")

  -- 2: walk A -> B through the doorway
  rooms_seen = {}
  track_bond()
  local bcx = (LEVEL.rooms.B.rect[1] + LEVEL.rooms.B.rect[3]) / 2
  local bcz = (LEVEL.rooms.B.rect[2] + LEVEL.rooms.B.rect[4]) / 2
  -- in front of the doorway, then straight through it (the line between the two approach pads runs through the opening's centre;
  -- stopping on the doorway pad itself, 20 units short of the portal, makes Bond overshoot into B and step back: A>B>A>B)
  local ok, stuck = walk({ padxz(D0.pad_a), padxz(D0.pad_b), { bcx, bcz } }, 20)
  local seq = {}
  for _, r in ipairs(rooms_seen) do seq[#seq + 1] = LEVEL.room_name[r] or tostring(r) end
  bx, _, bz = bond.pos(0)
  check("walk_a_to_b", ok and bond.room(0) == B, string.format("rooms %s, now in %s at (%.0f, %.0f)%s", table.concat(seq, ">"),
    tostring(LEVEL.room_name[bond.room(0)]), bx, bz, ok and "" or string.format(" STUCK before (%.0f, %.0f)", stuck[1], stuck[2])))
  check("walk_rooms_in_order", table.concat(seq, ">") == "A>B", table.concat(seq, ">"))

  -- 3: from B
  face = { D0.x, D0.z }
  wait(0.6)
  s, l = drawn()
  check("b_sees_a_back_through_doorway", s[A] and s[B], "drawn {" .. l .. "}")
  shot("b_looking_back_at_a")
  if C and D1 then
    face = { D1.x, D1.z }
    wait(0.6)
    s, l = drawn()
    check("b_sees_c_through_second_doorway", s[B] and s[C], "drawn {" .. l .. "}")
    shot("b_looking_into_c")
  end

  -- 4: back to A, then a guard from the far room routes to Bond
  local park = LEVEL.pads.a_corner and padxz(LEVEL.pads.a_corner) or padxz(LEVEL.start_pad)
  ok = walk({ padxz(D0.pad_b), padxz(D0.pad_a), park }, 20)
  check("walk_back_to_a", ok and bond.room(0) == A, "now in " .. tostring(LEVEL.room_name[bond.room(0)]))
  local far = LEVEL.pads.c_far or LEVEL.rooms[LEVEL.room_name[#LEVEL.room_name]].pads[1]
  local g = spawn.chr(far, { weapon = "none", behaviour = "chase" })
  check("guard_spawned", g ~= nil, "pad " .. tostring(far) .. " room " .. tostring(g and spawn.room(g)))
  if g then
    local gseq, t = {}, 0
    local gx, gz
    while t < 60 do
      local r = spawn.room(g)
      if r and gseq[#gseq] ~= r then gseq[#gseq + 1] = r end
      local x, z = spawn.where(g)
      gx, gz = x, z
      if r == A then
        local px, _, pz = bond.pos(0)
        if x and math.sqrt((x - px) ^ 2 + (z - pz) ^ 2) < 1000 then break end
        if not x then break end
      end
      t = t + coroutine.yield()
    end
    local names = {}
    for _, r in ipairs(gseq) do names[#names + 1] = LEVEL.room_name[r] or tostring(r) end
    local want = C and "C>B>A" or "B>A"
    check("guard_routes_to_bond", table.concat(names, ">") == want and spawn.room(g) == A,
      string.format("guard rooms %s in %.1f s (want %s)%s", table.concat(names, ">"), t, want,
        gx and string.format(", at (%.0f, %.0f)", gx, gz) or ""))
    face = { gx or D0.x, gz or D0.z }
    wait(0.5)
    shot("a_guard_arrived")
  end
  print(string.format("LEVELGEN DONE %d passed, %d failed", passed, failed))
  mission.fail("levelgen test done")
end

local co = coroutine.wrap(body)
local finished = false
on("tick", function(dt)
  if finished then return end
  local ok, err = pcall(co, dt)
  if not ok then finished = true; print("LEVELGEN error: " .. tostring(err)); print(string.format("LEVELGEN DONE %d passed, %d failed", passed, failed + 1)) end
end)
