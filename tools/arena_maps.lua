-- arena_maps.lua: the stock multiplayer maps the arena can be fought on (shipped as Data/Scripts/modes/_arena_maps.lua; modding/arena.md
-- "Maps"). Included by the start menu's definitions (the choices of the ARENA row), by the run side and by mode_arena.lua. Defines the
-- global ARENA_MAPS:
--   LIST        the maps, the Colosseum first: id, stage (the name front.c's stage table knows: "mptemple" loads Temple's MULTIPLAYER setup
--               with the one solo Bond), key (the best-scores suffix: the Colosseum's is empty, so its bests stay what they were), floors
--               (a window of pad heights { lo, hi }: Stack and Basement are the upper and lower storeys of the Library's geometry)
--   BY_ID       id -> its entry
--   D           the numbers (spawn distances, the view cone, the stuck test ...); ARENA_SETTINGS.map_cfg = { key = value } overrides them
--   new(S, id)  -> R, the map for one run (nil for the Colosseum): R.build() at stage start, then R.spawn_pad / R.centre_pads / R.crate ...
--
-- No ROM data lives here. Everything about a map's floor is read from the stage when it loads: its pads, and the path graph the port
-- builds from the floor (port/src/port_mpnav.c: the MP setups have no waypoints; pads.far_from(.., true) lists the pads a chr can walk to
-- and pads.base() says which of the pads are the map's own). The picks below are rules, not tables: the spawn pad is any walkable pad out
-- of Bond's sight and not too near, the centre of the drops is the map's own pad nearest the middle of the floor Bond starts on, Q stands
-- on one of its pads a little way off. The player-visible words are ARENA_THEME.maps (tools/arena_theme.lua).

ARENA_MAPS = {}
local M = ARENA_MAPS

M.LIST = {
  { id = "colosseum", key = "" },
  { id = "temple",   stage = "mptemple",   key = "temple" },
  { id = "complex",  stage = "mpcomplex",  key = "complex" },
  { id = "caves",    stage = "mpcaves",    key = "caves" },
  { id = "library",  stage = "mplibrary",  key = "library" },
  { id = "stack",    stage = "mpstack",    key = "stack",    floors = { -150, 100000 } },
  { id = "basement", stage = "mpbasement", key = "basement", floors = { -100000, -150 } },
  { id = "facility", stage = "mpfacility", key = "facility" },
}
M.BY_ID = {}
for i, m in ipairs(M.LIST) do m.index = i; M.BY_ID[m.id] = m end

M.D = {
  spawn_min     = 1500,   -- an enemy appears at least this far (15 m) from every Bond ...
  cone          = 62,     -- ... and outside the cone he faces, this many degrees either side of the centre of his view
  spawn_max     = 7000,   -- an ordinary enemy appears no farther than this (a long walk is a dull wave) while the map has a pad that near
  spawn_far     = 3600,   -- an ambush (gates = "far") comes from at least this far; relaxed when the map has no pad that far
  snipe_min     = 2600,   -- a marksman stands at least this far, and at most snipe_max
  snipe_max     = 9000,
  recent        = 8,      -- the last spawns remembered ...
  recent_gap    = 650,    -- ... and no new one within this of them (the spawns spread over the map)
  level         = 260,    -- two pads this far apart in height are on different storeys
  centre_radius = 700,    -- centre pads = the pads within this of the middle one, on its storey
  centre_max    = 10,
  crate_near    = 900,    -- Q's pad is this far from the centre at least ...
  crate_want    = 1900,   -- ... and as near this as the map allows
  crate_bond    = 800,    -- ... and this far from Bond's start
  stuck_secs    = 22,     -- an enemy that has moved less than stuck_move in this long, with no Bond within stuck_near, is moved
  stuck_move    = 160,
  stuck_near    = 2200,
  gun           = { "pp7", 100 },   -- what Bond starts with (the MP setups arm him with his fists only)
}

local function merge(base, over)
  local o = {}
  for k, v in pairs(base) do o[k] = v end
  if type(over) == "table" then for k, v in pairs(over) do o[k] = v end end
  return o
end

local function dist2(ax, az, bx, bz) return (ax - bx) ^ 2 + (az - bz) ^ 2 end

-- the map for one run; nil for the Colosseum (or an unknown id)
function M.new(S, id)
  local def = M.BY_ID[id]
  if not def or not def.stage then return nil end
  local C = merge(M.D, S and S.map_cfg)
  local TH = type(ARENA_THEME) == "table" and ARENA_THEME.maps and ARENA_THEME.maps[id] or {}
  local R = { id = id, def = def, name = TH[1] or id:upper(), stage = def.stage, key = def.key, cfg = C,
              cands = {}, floor_pads = {}, centre_pads = {}, centre = nil, crate = nil, start = nil, base = 0, level_y = 0 }
  local recent = {}            -- { x, z } of the last spawns
  local used_n, relaxed = 0, 0
  local info = {}              -- pad id -> { x, y, z }

  -- the walkable pads of this map's floor, once the stage is up
  function R.build()
    R.base = pads.base and pads.base() or pads.count()
    local lo, hi = -1e9, 1e9
    if def.floors then lo, hi = def.floors[1], def.floors[2] end
    local bx, by, bz = bond.pos(0)
    -- where Bond starts: the map's own pad nearest him (its height is the storey the drops and Q belong to)
    local sp, sd = nil, 1e18
    for i = 0, R.base - 1 do
      local x, y, z = pads.pos(i)
      if x and bx then
        local d = dist2(x, z, bx, bz)
        if d < sd then sp, sd = i, d end
      end
    end
    local sy = 0
    if sp then local _, y = pads.pos(sp); sy = y end
    R.level_y = sy
    R.start = sp and { id = sp, x = bx, z = bz, y = sy } or nil
    local ids = pads.far_from(0, 0, 0, 1e9, true)
    table.sort(ids)
    for _, id_ in ipairs(ids) do
      local x, y, z = pads.pos(id_)
      if x and y >= lo and y < hi then
        local rec = { id = id_, x = x, y = y, z = z, own = id_ < R.base }
        R.cands[#R.cands + 1] = rec
        R.floor_pads[#R.floor_pads + 1] = rec
        info[id_] = rec
      end
    end
    -- the middle of the floor Bond starts on: the map's own pad nearest the mean of that storey's pads (any pad if it has none)
    local sx, sz, n = 0, 0, 0
    for _, c in ipairs(R.cands) do
      if math.abs(c.y - sy) < C.level then sx, sz, n = sx + c.x, sz + c.z, n + 1 end
    end
    if n > 0 then
      sx, sz = sx / n, sz / n
      local best, bd
      for _, c in ipairs(R.cands) do
        if math.abs(c.y - sy) < C.level then
          local d = dist2(c.x, c.z, sx, sz) + (c.own and 0 or 250 ^ 2)   -- (a pad of the map's own is the better spot: a little nearer)
          if not bd or d < bd or (d == bd and c.id < best.id) then best, bd = c, d end
        end
      end
      R.centre = best
      local list = {}
      for _, c in ipairs(R.cands) do
        local d = math.sqrt(dist2(c.x, c.z, best.x, best.z))
        if math.abs(c.y - best.y) < C.level and d <= C.centre_radius then list[#list + 1] = { id = c.id, d = d } end
      end
      table.sort(list, function(a, b) if a.d ~= b.d then return a.d < b.d end return a.id < b.id end)
      for i = 1, math.min(#list, C.centre_max) do R.centre_pads[i] = list[i] end
      -- Q: one of the map's own pads a good way from the centre and from Bond's start, as near crate_want as there is
      local qbest, qd
      for _, c in ipairs(R.cands) do
        if c.own and math.abs(c.y - sy) < C.level and c.id ~= best.id then
          local dc = math.sqrt(dist2(c.x, c.z, best.x, best.z))
          local db = bx and math.sqrt(dist2(c.x, c.z, bx, bz)) or 1e9
          if dc >= C.crate_near and db >= C.crate_bond then
            local score = math.abs(dc - C.crate_want)
            if not qd or score < qd or (score == qd and c.id < qbest.id) then qbest, qd = c, score end
          end
        end
      end
      R.crate = qbest
    end
    return #R.cands
  end

  function R.info(pad) return info[pad] end

  -- the Bonds that can see an enemy come: { x, y, z, yaw }
  local function bonds()
    local t = {}
    for p = 0, bond.count() - 1 do
      if bond.dead(p) == false then
        local x, y, z = bond.pos(p)
        if x then
          local yaw
          if gadget and gadget.aim then
            local ok, a = pcall(gadget.aim, p)
            if ok then yaw = a end
          end
          t[#t + 1] = { x = x, y = y, z = z, yaw = yaw }
        end
      end
    end
    return t
  end
  R.bonds = bonds

  local function in_cone(b, x, z)
    if not b.yaw then return false end
    local dx, dz = x - b.x, z - b.z
    local d = math.sqrt(dx * dx + dz * dz)
    if d < 1 then return true end
    local fx, fz = -math.sin(math.rad(b.yaw)), math.cos(math.rad(b.yaw))
    local c = (dx * fx + dz * fz) / d
    return math.deg(math.acos(math.max(-1, math.min(1, c)))) < C.cone
  end

  local function remember(x, z)
    recent[#recent + 1] = { x, z }
    if #recent > C.recent then table.remove(recent, 1) end
  end

  -- the pad for one enemy: entry = { gates = "far" | nil, mouth = true | nil }; skip = pads already refused this try. The tiers: out of every
  -- Bond's cone and 15 m off, then just 15 m off, then anything but his feet (relaxed counts, for the log). Returns a pad id and Bond's
  -- position (for the facing), or nil.
  function R.spawn_pad(entry, skip)
    local bs = bonds()
    if #bs == 0 then return nil end
    local lo_d, hi_d = C.spawn_min, C.spawn_max
    if entry.gates == "far" then lo_d, hi_d = C.spawn_far, 1e9
    elseif entry.mouth then lo_d, hi_d = C.snipe_min, C.snipe_max end
    for tier = 1, 4 do
      local list = {}
      for _, c in ipairs(R.cands) do
        if not (skip and skip[c.id]) then
          local near, seen = 1e18, false
          for _, b in ipairs(bs) do
            local d = math.sqrt(dist2(c.x, c.z, b.x, b.z))
            if d < near then near = d end
            if in_cone(b, c.x, c.z) then seen = true end
          end
          local ok
          if tier == 1 then ok = near >= lo_d and near <= hi_d and not seen
          elseif tier == 2 then ok = near >= C.spawn_min and not seen
          elseif tier == 3 then ok = near >= C.spawn_min
          else ok = near >= 400 end
          if ok and tier <= 2 then   -- spread: not on top of the last few
            for _, r in ipairs(recent) do
              if dist2(c.x, c.z, r[1], r[2]) < C.recent_gap ^ 2 then ok = false; break end
            end
          end
          if ok then list[#list + 1] = c end
        end
      end
      if #list > 0 then
        local c = list[math.random(#list)]
        if tier > 1 then relaxed = relaxed + 1 end
        remember(c.x, c.z)
        used_n = used_n + 1
        local nb, nd = bs[1], 1e18
        for _, b in ipairs(bs) do
          local d = dist2(c.x, c.z, b.x, b.z)
          if d < nd then nb, nd = b, d end
        end
        return c.id, nb.x, nb.z
      end
    end
    return nil
  end

  function R.stats() return used_n, relaxed end

  -- The doors stand open for the whole run. A chr opens a door only when it comes within 2 m of the door's centre, and the doors of the
  -- multiplayer maps are wider than that from their faces: the chasers stopped dead in front of them. Called at the start and every second.
  function R.doors_open()
    if not (door and door.list and door.get and door.snap) then return 0 end
    local n = 0
    for _, i in ipairs(door.list()) do
      local d = door.get(i)
      if d and d.state ~= "open" and d.state ~= "opening" then
        if door.snap(i, true) then n = n + 1 end
      end
    end
    return n
  end
  return R
end
