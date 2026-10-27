-- Arena polish (drop A7). Shipped to Data/Scripts/modes/_arena_polish.lua and run with include("_arena_polish") from arena.lua.
-- Kept in its own file so the polish stays out of the way of mode_arena.lua. Settings come from ARENA_SETTINGS (set before include("arena")):
--   pickup_markers   true (default): markers over pickups. Presentation only (nothing in the sim or its hash reads it). Drop A8 (v2, after
--                    AC's playtest: "messy/slow with knife throwers"): the good stuff (centre ammo box, armour, crowd / sponsor gifts) gets
--                    a small diamond + "AMMO 12m"; dropped guns a faint dot close up; markers that overlap on screen merge with a count:
--     marker_fade      8      seconds after a pickup appears its marker fades out (0 = never) ...
--     marker_near      1500   ... and it shows again when you are this close (units; 100 = 1 m)
--     marker_drops     "near" dropped guns: "near" (a dot within marker_drop_near), false (none), "always"
--     marker_knives    false  thrown knives on the floor: false (none), "near", "always"
--     marker_drop_near 1000
--     marker_merge     26     markers closer than this on screen (px of the 640-wide console space) become one with a count
--     marker_edge      true   off-screen edge diamonds for the good stuff (never for drops)
--     marker_max       10     draw budget: at most this many markers after merging
--     marker_style     "v2"   "a7" = the old look (a beacon over every pickup), for before/after comparisons
--   knife_keep       true (default): knife throwers hold about knife_keep_dist from Bond and throw, backing off when he comes closer than 28 m (
--                    the engage list m_ArenaKnifeKeep); false: the A5 behaviour (they run in while throwing)
--   knife_keep_dist  3300 units (they stand 28-38 m away; a retreat runs to a pad 2800-3900 from Bond that can see him)
--   seat_gap         240 units: no two spectators closer than this on the same stand tier
--   debug_seats      true: log the duplicate-seat count of the plain sample (the A6 method) and of the final seating

local A = type(ARENA_SETTINGS) == "table" and ARENA_SETTINGS or {}
local function opt(k, d) if A[k] ~= nil then return A[k] end return d end

local P = {
  pickup_markers  = opt("pickup_markers", true),
  knife_keep      = opt("knife_keep", true),
  knife_keep_dist = opt("knife_keep_dist", 3300),
  seat_gap        = opt("seat_gap", 240),
}
ARENA_POLISH = P

-- ---------------------------------------------------------------- knives -----

-- a knife thrower's spawn spec: hold the distance (a spec may set knife_keep itself, 0 = close in)
function P.tweak_spec(spec)
  if spec.knives and P.knife_keep and spec.knife_keep == nil then spec.knife_keep = P.knife_keep_dist end
end

-- ----------------------------------------------------------------- seats -----

local function pos_of(id)
  local x, y, z = pads.pos(id)
  return x, y, z
end

-- two seats overlap when they are on the same tier (heights within 60) and closer than gap in x/z
local function overlap(a, b, gap)
  return math.abs(a.y - b.y) < 60 and (a.x - b.x) ^ 2 + (a.z - b.z) ^ 2 < gap * gap
end

local function dup_count(list, gap)
  local n = 0
  for i = 1, #list do
    for j = i + 1, #list do
      if overlap(list[i], list[j], gap) then n = n + 1 end
    end
  end
  return n
end

-- the A6 sample: every (#seats / n)'th seat, jittered inside its stride, shuffled
local function plain_sample(seats, n)
  local out = {}
  local step = #seats / n
  for i = 0, n - 1 do
    local k = math.floor(i * step + math.random() * step) + 1
    if k > #seats then k = #seats end
    out[#out + 1] = seats[k]
  end
  return out
end

-- Choose n of the seat pads so that no two spectators share a spot: the even sample first, then any pick that overlaps an already
-- accepted one (or repeats a pad) is replaced by a random unused seat that overlaps none. Fewer than n come back only when the stands
-- have no room left for another one at seat_gap.
function P.choose_seats(seats, n)
  n = math.min(n, #seats)
  local out = {}
  if n <= 0 then return out end
  local info = {}
  for _, id in ipairs(seats) do
    local x, y, z = pos_of(id)
    info[id] = { id = id, x = x or 0, y = y or 0, z = z or 0 }
  end
  local gap = P.seat_gap
  local first = plain_sample(seats, n)
  local before = 0
  if A.debug_seats then
    local l = {}
    for _, id in ipairs(first) do l[#l + 1] = info[id] end
    before = dup_count(l, gap)
  end
  local taken, used = {}, {}
  local function fits(s)
    for _, t in ipairs(taken) do if overlap(s, t, gap) then return false end end
    return true
  end
  local replaced = 0
  for _, id in ipairs(first) do
    local s = info[id]
    if not used[id] and fits(s) then
      taken[#taken + 1] = s; used[id] = true
    end
  end
  -- fill the places that lost their seat from the unused seats, in random order
  if #taken < n then
    local pool = {}
    for _, id in ipairs(seats) do if not used[id] then pool[#pool + 1] = id end end
    for i = #pool, 2, -1 do local j = math.random(i); pool[i], pool[j] = pool[j], pool[i] end
    for _, id in ipairs(pool) do
      if #taken >= n then break end
      local s = info[id]
      if fits(s) then taken[#taken + 1] = s; used[id] = true; replaced = replaced + 1 end
    end
  end
  for _, s in ipairs(taken) do out[#out + 1] = s.id end
  for i = #out, 2, -1 do   -- shuffle so the stands fill all around rather than in a sweep
    local j = math.random(i)
    out[i], out[j] = out[j], out[i]
  end
  local after = dup_count(taken, gap)
  print(string.format("ge: arena: seats: %d of %d wanted placed from %d, %d replaced, duplicate pairs (closer than %d on one tier): plain sample %s, final %d",
    #out, n, #seats, replaced, gap, A.debug_seats and tostring(before) or "n/a", after))
  return out
end

-- ------------------------------------------------------------ pickup markers --

local sync_acc = 0
on("stage_start", function(stage)
  if P.pickup_markers then
    local ok, err = pcall(spawn.markers, true, { fade = opt("marker_fade", 8), near = opt("marker_near", 1500),
      drops = opt("marker_drops", "near"), knives = opt("marker_knives", false), drop_near = opt("marker_drop_near", 1000),
      merge = opt("marker_merge", 26), edge = opt("marker_edge", true), max = opt("marker_max", 10), legacy = opt("marker_style", "v2") == "a7" })
    if not ok then print("ge: arena: pickup markers unavailable:", err) end
  end
end)
on("tick", function(dt)
  if not P.pickup_markers then return end
  sync_acc = sync_acc + dt
  if sync_acc >= 0.1 then
    sync_acc = 0
    pcall(spawn.marker_sync)
  end
end)
