-- Hostage rescue (GE_MODE=hostage). Shipped to Data/Scripts/modes/hostage.lua by the build.
-- Find the hostages, press the action button next to one to make it follow (press again to
-- make it wait), and lead them back to Bond's start point. Guards shoot at you; crossfire kills hostages.
--
-- Solo, co-op and netplay: the script runs on every machine of a session and must see the same world
-- everywhere, so it only reads shared state (bond.pos(0), bond.count(), hostage leaders) and never the local
-- player. In co-op either Bond can toggle a hostage (a hostage follows the Bond that told it to; pressing it
-- from the other Bond hands it over), and a following hostage that reaches the extraction zone is extracted.
-- math.random, if you add it, comes from the session seed and is the same on both machines.

local S = {
  hostages       = 4,     -- hostages to spawn (max 16)
  guards_hold    = 4,     -- guards standing around the hostages
  guards_patrol  = 2,     -- guards that walk a level patrol path (hold position if the path is missing)
  need           = 3,     -- hostages that must be extracted to win (the mission also fails once fewer than this are alive)
  lose_if_dead   = 2,     -- this many dead hostages fails the mission
  holding_min_dist = 3000,-- the holding area is at least this far (XZ) from Bond's start
  extract_radius = 250,   -- extraction zone radius at Bond's start
  guard_weapon   = "kf7", -- any name spawn.guard accepts (ak47 shotgun wppk mp5k m16 uzi tt33 kf7 pp7 ...)
  time_limit     = nil,   -- seconds, or nil for none
  patrol_paths   = { 0, 1, 2, 3 }, -- stage path ids tried for patrol guards (a missing id means hold)
  cluster_radius = 700,   -- how far around the holding pad hostages/guards are placed (grows if too few pads)
}
if type(HOSTAGE_SETTINGS) == "table" then   -- optional override hook (used by tests and by preset modes such as hostage-flatland)
  for k, v in pairs(HOSTAGE_SETTINGS) do S[k] = v end
end

local H = {}            -- chr -> true for our hostages
local dead, rescued, total = 0, 0, 0
local following_seen = false
local ended = false
local zid, elapsed = nil, 0

local function d2(ax, az, bx, bz) local dx, dz = ax - bx, az - bz; return math.sqrt(dx * dx + dz * dz) end

local function alive()
  return total - dead - rescued
end

local function refresh_hud()
  local led = {}                                  -- hostages each Bond leads (follow state)
  for _, e in ipairs(hostages.list()) do
    if e.state == "follow" and e.leader then led[e.leader] = (led[e.leader] or 0) + 1 end
  end
  hud.set(1, string.format("HOSTAGES  rescued %d/%d - alive %d", rescued, S.need, total - dead))
  local obj
  if ended then obj = ""
  elseif rescued > 0 or following_seen then obj = "Bring them to the extraction"
  else obj = "Find the hostages" end
  if S.time_limit then
    local left = math.max(0, math.floor(S.time_limit - elapsed))
    obj = string.format("%s   %d:%02d", obj, math.floor(left / 60), left % 60)
  end
  hud.set(2, obj)                                 -- every player
  if not ended and bond.count() > 1 then          -- co-op: each Bond sees how many it leads (line 3 is left to the follow/wait flash)
    for p = 0, bond.count() - 1 do
      hud.set(2, string.format("%s  (you lead %d)", obj, led[p] or 0), p)
    end
  end
end

local function check_end()
  if ended then return end
  if rescued >= S.need then
    ended = true
    refresh_hud()
    print("ge: hostage: WIN rescued", rescued)
    mission.win("All hostages rescued")
  elseif dead >= S.lose_if_dead then
    ended = true
    refresh_hud()
    print("ge: hostage: FAIL dead", dead)
    mission.fail("Too many hostages died")
  elseif total - dead < S.need then   -- can no longer reach the target
    ended = true
    print("ge: hostage: FAIL not enough hostages left")
    mission.fail("Too many hostages died")
  end
end

local function note_dead(chr)
  if H[chr] == "dead" or not H[chr] then return end
  H[chr] = "dead"
  dead = dead + 1
  print("ge: hostage: hostage", chr, "died; dead", dead, "alive", total - dead)
  refresh_hud()
  check_end()
end

local function try_extract(chr)
  if ended or H[chr] ~= true then return end
  if hostages.state(chr) ~= "follow" then return end
  if hostages.extract(chr) then
    H[chr] = "done"
    rescued = rescued + 1
    print("ge: hostage: extracted", chr, "rescued", rescued .. "/" .. S.need)
    refresh_hud()
    check_end()
  end
end

on("stage_start", function(stage)
  local ok, err = pcall(function()
    -- the reference point is player 0's start (every Bond starts together); never "the local player"
    local bx, by, bz = bond.pos(0)
    if not bx then print("ge: hostage: no bond pos, aborting setup"); return end
    print("ge: hostage: bonds", bond.count())

    -- extraction zone at Bond's start
    zid = zone.add("extract", bx, by, bz, S.extract_radius)

    -- pick the holding pad: the nearest pad at least holding_min_dist away, with some variety
    local min = S.holding_min_dist
    local far = pads.far_from(bx, bz, min, nil, true)
    if not far or #far == 0 then far = pads.far_from(bx, bz, min) end
    while (not far or #far == 0) and min > 300 do
      min = min * 0.6
      far = pads.far_from(bx, bz, min, nil, true)
      print("ge: hostage: no pad that far, relaxing min distance to", math.floor(min))
    end
    if not far or #far == 0 then
      print("ge: hostage: no far pads at all, nothing to spawn")
      hud.set(1, "HOSTAGE MODE: no holding area on this level")
      return
    end
    local list = {}
    for _, p in ipairs(far) do
      local x, _, z = pads.pos(p)
      if x then list[#list + 1] = { id = p, d = d2(x, z, bx, bz) } end
    end
    table.sort(list, function(a, b) if a.d ~= b.d then return a.d < b.d end return a.id < b.id end)
    -- among pads within 1.5x the minimum, pick one by a stage-seeded index; else the nearest qualifying
    local cand = {}
    for _, e in ipairs(list) do if e.d <= min * 1.5 then cand[#cand + 1] = e end end
    if #cand == 0 then cand = { list[1] } end
    local pick = cand[(stage * 7 + 3) % #cand + 1]
    local hx, hy, hz = pads.pos(pick.id)
    print(string.format("ge: hostage: holding pad %d at distance %d (min %d, %d candidates)",
      pick.id, math.floor(pick.d), math.floor(min), #cand))

    -- cluster around the holding pad, growing the radius until there are enough pads
    local want = S.hostages + S.guards_hold + S.guards_patrol
    local radius = S.cluster_radius
    local cl
    repeat
      cl = pads.far_from(hx, hz, 0, radius, true)
      radius = radius * 1.6
    until (cl and #cl >= want) or radius > 6000
    if not cl or #cl == 0 then cl = { pick.id } end
    local cluster = {}
    for pass = 1, 2 do   -- pass 1 keeps pads on the same floor; pass 2 (if too few) takes any
      for _, p in ipairs(cl) do
        local x, y, z = pads.pos(p)
        if x and (pass == 2 or math.abs(y - hy) < 200) then cluster[#cluster + 1] = { id = p, d = d2(x, z, hx, hz) } end
      end
      if #cluster >= want then break end
      cluster = {}
    end
    -- (a tie on distance goes to the lower pad id: table.sort is not stable, and both machines must pick alike)
    table.sort(cluster, function(a, b) if a.d ~= b.d then return a.d < b.d end return a.id < b.id end)
    print("ge: hostage: cluster pads", #cluster, "wanted", want)

    -- hostages on the innermost pads, holding guards next, patrol guards on the outermost
    local idx = 1
    local function nextpad(from_end)
      if #cluster == 0 then return nil end
      local e
      if from_end then e = cluster[#cluster - (from_end - 1) % #cluster] else e = cluster[(idx - 1) % #cluster + 1]; idx = idx + 1 end
      return e.id
    end
    for i = 1, S.hostages do
      local c = spawn.hostage(nextpad())
      if c then H[c] = true; total = total + 1; zone.watch(c) end
    end
    local g_hold, g_pat = 0, 0
    for i = 1, S.guards_hold do
      if spawn.guard(nextpad(), { weapon = S.guard_weapon }) then g_hold = g_hold + 1 end
    end
    for i = 1, S.guards_patrol do
      local path = S.patrol_paths[i]
      if spawn.guard(nextpad(i), { patrol = path, weapon = S.guard_weapon }) then g_pat = g_pat + 1 end
    end
    if S.need > total then S.need = total end
    print(string.format("ge: hostage: setup done: %d hostages, %d holding guards, %d patrol guards, zone %s r=%d, need %d, lose at %d dead",
      total, g_hold, g_pat, tostring(zid), S.extract_radius, S.need, S.lose_if_dead))
  end)
  if not ok then print("ge: hostage: setup error (ignored):", err) end
  refresh_hud()
end)

on("hostage_follow", function(chr, player)
  following_seen = true
  print("ge: hostage: follow", chr, "led by player", player)
  refresh_hud()
end)

on("hostage_wait", function(chr, player)
  refresh_hud()
end)

on("zone_enter", function(z, chr)
  if z == zid and H[chr] == true then try_extract(chr) end
end)

on("chr_died", function(victim, killer)
  if H[victim] then note_dead(victim) end
end)

local acc = 0
on("tick", function(dt)
  if ended then return end
  elapsed = elapsed + dt
  -- safety nets: missed events
  for _, e in ipairs(hostages.list()) do
    if H[e.chr] == true then
      if e.state == "dead" then note_dead(e.chr)
      elseif e.state == "follow" and zid and zone.contains(zid, e.chr) then try_extract(e.chr) end
    end
  end
  if S.time_limit and elapsed >= S.time_limit then
    ended = true
    print("ge: hostage: FAIL time up")
    mission.fail("Time ran out")
    return
  end
  acc = acc + dt
  if acc >= 1 then acc = 0; refresh_hud() end
end)
