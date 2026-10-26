-- The heist map reader (designs/bank-heist.md sections 14, 15 and 17; the job table: designs/job2-casino.md section 6): mode_heist.lua's
-- tables (DOORS, AREAS, VAULT_PADS, VAULT_CRATE_PADS, CIV_PLACES, P, CARRIERS, CAM_LABEL / CAM_AREA, GUARD_AREAS, intel_room, VAULT_ROOMS,
-- MAP) are built here, at stage start, from the LAYOUT (LEVEL, tools/gen_level.py's levelgen_layout.lua) and the JOB (the names: a job
-- definition, tools/jobs/<job>.lua -> Data/Scripts/modes/job_<job>.lua). The bank's names live in tools/jobs/bank.lua, the casino's in
-- tools/jobs/casino.lua; the layout names its job (LEVEL.script.heist.job, default "bank").
--   include("levelgen_layout"); include("heist_bank_map")      -- at the script's top level (include() works only there)
--   local JOB = heist_job()                                    -- the layout's job definition (HEIST_JOBS[name])
--   on("stage_start", function() local M = heist_bank_map(JOB) ... end)   -- once the stage is up (it reads pad positions)
-- Shipped as Data/Scripts/modes/heist_bank_map.lua. heist_bank_map() raises on a name the layout lacks.
--
-- Areas are groups of rooms by name; the rooms' own `area` tags (the cover classes) and `zone` tags (public / staff / restricted) come
-- from the layout. A layout's LEVEL.script.heist (bank v3, the casino) carries its own area and camera tables over the job's.

include("job_bank")
pcall(include, "job_casino")

local function need(v, what)
  if v == nil then error("heist_bank_map: the layout has no " .. what) end
  return v
end

-- the layout's job definition: LEVEL.script.heist.job (a name in HEIST_JOBS), else the bank
function heist_job()
  local L = LEVEL
  local name = L and L.script and L.script.heist and L.script.heist.job or "bank"
  return HEIST_JOBS[name] or HEIST_JOBS.bank
end

function heist_bank_map(job)
  local L = LEVEL
  job = job or heist_job()
  local J = job.map
  -- bank v3 (designs/bank-v3.md): the layout carries its own area and camera tables (LEVEL.script.heist, written by
  -- tools/levelgen/build_janus_bank_v3.py); the job's tables stay for a layout without them
  local H = L.script and L.script.heist or {}
  local AREA_DEF, CAMS = H.areas or J.areas, H.cams or J.cams
  local function obj(n) return need(L.objects[n], "object " .. n) end
  local function leaves(n) local o = obj(n); return o.pair and { o.index, o.pair } or { o.index } end
  local function pad(n) return need(L.pads[n], "pad " .. n) end
  local function pads_of(list) local out = {}; for _, n in ipairs(list) do out[#out + 1] = pad(n) end; return out end
  local function xz(n) local x, _, z = pads.pos(pad(n)); return { x, z } end
  local function room(n) return need(L.rooms[n], "room " .. n) end
  local function cat(...)
    local out = {}
    for _, t in ipairs({ ... }) do for _, v in ipairs(t) do out[#out + 1] = v end end
    return out
  end
  -- a grid pad a guard may stand on: not against a furniture block (the counter, a pillar, a shelf: the generator only drops a grid cell
  -- whose centre is inside one), not on a prop (desks, crates, the safe: nothing else keeps pads off them), not in a doorway (a guard
  -- there holds the door open or blocks it)
  local function clear(p, rname)
    local x, _, z = pads.pos(p)
    if x == nil then return false end
    for _, b in ipairs(room(rname).blocks or {}) do
      local r = b.rect
      if x > r[1] - 70 and x < r[3] + 70 and z > r[2] - 70 and z < r[4] + 70 then return false end
    end
    for _, o in pairs(L.objects) do
      if (o.type == "prop" or o.type == "safe") and o.room == rname and o.x and (x - o.x) ^ 2 + (z - o.z) ^ 2 < 120 * 120 then return false end
    end
    for _, d in ipairs(L.doors) do
      if (x - d.x) ^ 2 + (z - d.z) ^ 2 < 140 * 140 then return false end
    end
    return true
  end
  local dropped = 0
  -- an area's pads: its named pads first (posts, patrol points), then every clear grid pad of its rooms
  local function area_pads(named, rooms)
    local out = {}
    for _, n in ipairs(named) do out[#out + 1] = pad(n) end
    for _, r in ipairs(rooms) do
      for _, p in ipairs(room(r).pads) do
        if clear(p, r) then out[#out + 1] = p else dropped = dropped + 1 end
      end
    end
    return out
  end
  local function loot(n) return need(L.loot[n], "loot " .. n) end
  local JD = J.doors
  -- every door of a card tier (by its key flags), double doors' second leaves too, in setup order; not the cells (their own entries)
  local function tier(flags)
    local out = {}
    for _, d in ipairs(L.doors) do
      if d.object and d.keyflags == flags then
        local o
        for n, oo in pairs(L.objects) do if oo.index == d.object then o = n end end
        if o ~= JD.cell_green and o ~= JD.cell_floor then
          out[#out + 1] = d.object
          if d.object2 then out[#out + 1] = d.object2 end
        end
      end
    end
    table.sort(out)
    return out
  end

  local M = {}
  local blast = {}
  for _, n in ipairs(JD.blast) do for _, i in ipairs(leaves(n)) do blast[#blast + 1] = i end end
  M.DOORS = {
    front = leaves(JD.front),
    blast = blast,                             -- the lockdown shutters (closed and BLUE on the alarm)
    green = tier(0x2),                         -- (bank) green_hall (double), green_back, green_aisle, the maintenance door
    blue = tier(0x4),                          -- (bank) blue_hall (double), the manager's, the security wing's back doors, the director's ...
    red = tier(0x8),                           -- (bank) red_stairs (double), red_ramp, the plant gate and the power room
    vault_a = leaves(JD.vault_a),              -- the crack door (antechamber -> vault)
    vault_b = leaves(JD.vault_b),              -- the second vault door
    safe = obj(JD.safe).index,
    cell_floor = obj(JD.cell_floor).index, cell_green = obj(JD.cell_green).index,
  }
  for k, n in pairs(JD.extra or {}) do M.DOORS[k] = obj(n).index end   -- (bank) the alley's dock shutter, the canal tunnel's grate (padlocked)
  M.AREAS = {}
  for _, a in ipairs(AREA_DEF) do
    M.AREAS[#M.AREAS + 1] = { name = a.name, pads = area_pads(a.posts, a.rooms), panel = a.panel and obj(a.panel).pad or nil,
                              share = a.share, raisers = a.raisers }
  end
  M.AREA_LABEL = {}
  for k, v in pairs(J.area_label) do M.AREA_LABEL[k] = v end
  for k, v in pairs(H.area_label or {}) do M.AREA_LABEL[k] = v end
  M.VAULT_ROOMS = { [room(J.vault_room).id] = true }
  M.VAULT_PADS = area_pads(J.vault_pads, { J.vault_room })
  M.VAULT_CRATE_PADS = {}   -- (the crates stand by the walls, off every pad)
  M.CIV_PLACES = {}
  for _, c in ipairs(J.civ_places) do M.CIV_PLACES[#M.CIV_PLACES + 1] = { c[1], pads_of(c[2]), c[3] } end
  for _, c in ipairs(H.civ_extra or {}) do                       -- (bank v3) the safe-deposit attendant, an officer, the cash counters
    M.CIV_PLACES[#M.CIV_PLACES + 1] = { c.area, pads_of(c.pads), c.n }
  end
  -- the card carriers' candidate pads
  M.CARRIERS = {}
  for k, list in pairs(J.carriers) do M.CARRIERS[k] = pads_of(list) end
  local function mon_xz(n) local o = obj(n); return { o.x, o.z } end
  local JP = J.P
  local monitors = {}
  for _, n in ipairs(JP.monitors) do monitors[#monitors + 1] = mon_xz(n) end
  local inside = {}
  for _, n in ipairs(JP.inside) do inside[#inside + 1] = xz(n) end
  local extra_loot = {}
  for k, n in pairs(JP.extra_loot or {}) do extra_loot[k] = loot(n) end
  local cell_loot = {}
  for _, n in ipairs(JP.cell_loot) do cell_loot[#cell_loot + 1] = loot(n) end
  M.P = {
    start = cat({ L.start_pad }, pads_of(JP.start)),
    exit_a = pad(JP.exit_a), exit_b = pad(JP.exit_b),
    crack = pad(JP.crack), crack_xz = xz(JP.crack),
    inside = inside,
    vault = {},                                                  -- the vault is one room: VAULT_ROOMS covers it
    console = pad(JP.console),
    monitors = monitors,
    waves_front = pads_of(JP.waves_front),
    waves_dock = pads_of(JP.waves_dock),
    squad = pads_of(JP.squad),
    barracks = pads_of(JP.barracks),
    heavy = pads_of(JP.heavy),
    safe_loot = loot(JP.safe_loot),
    cell_loot = cell_loot,                                       -- cell_green (GREEN), cell_floor (the floor key)
    vault_loot = loot(JP.vault_loot),                            -- the vault's loot pads: the bars are stacked round each
    extra_loot = extra_loot,                                     -- more loot spots (not yet used by the mode)
    outside = pad(JP.outside),                                   -- (tests) outside the crack zone and the vault
    screens_at = xz(JP.screens_at),                              -- (tests) in front of the screen wall, facing it
    screens_face = { obj(JP.screens_face).x, obj(JP.screens_face).z - 100 },
    feed_cam = obj(JP.feed_cam).index,                           -- (tests) a public-side camera
    exit_c = JP.exit_c and pad(JP.exit_c) or nil,                -- a third way in
    -- the power (section 16; optional: a layout without them has no power cut): the plant room's main breaker and backup generator
    -- (props), the pad in front of the breaker the investigators walk to, and where they come from (the plant corridor outside)
    breaker = L.objects.breaker and { L.objects.breaker.x, L.objects.breaker.z } or nil,
    generator = L.objects.generator and { L.objects.generator.x, L.objects.generator.z } or nil,
    power_to = L.pads.breaker_front, power_way = L.pads.power_way, power_mid = L.pads.power_mid,
    generator_front = L.pads.generator_front,
  }
  M.power_room = L.rooms[J.power_room] and L.rooms[J.power_room].id or nil
  M.MAP = {}
  for k, v in pairs(J.MAP) do M.MAP[k] = v end
  M.MAP.label = H.map_label or J.MAP.label
  M.MAP.no_cams = H.no_cams or J.MAP.no_cams
  -- v3: the interfaces other modules use (designs/bank-v3.md section 8): guard tours (route -> pad ids in walking order, a loop), the
  -- watchclock stations (pad ids), vents (id -> { a, b }) and grapple points (id -> { low, high }); empty on a layout without them
  M.PATROLS, M.STATIONS, M.VENTS, M.GRAPPLES = {}, {}, {}, {}
  for r, names in pairs(H.patrols or {}) do M.PATROLS[r] = pads_of(names) end
  for _, n in ipairs(H.stations or {}) do M.STATIONS[#M.STATIONS + 1] = pad(n) end
  for _, v in ipairs(H.vents or {}) do M.VENTS[v] = { pad("vent_" .. v .. "_a"), pad("vent_" .. v .. "_b") } end
  for _, g in ipairs(H.grapples or {}) do M.GRAPPLES[g] = { pad("grapple_" .. g .. "_low"), pad("grapple_" .. g .. "_high") } end
  M.dropped = dropped                                            -- grid pads kept off (blocks, props, doorways)
  M.intel_room = room(J.intel_room).id                           -- the room with the screens shows the intel box
  M.CAM_LABEL, M.CAM_AREA = {}, {}
  for _, c in ipairs(CAMS) do
    local i = obj(c[1]).index
    M.CAM_LABEL[i], M.CAM_AREA[i] = c[2], c[3]
  end
  M.GUARD_AREAS = H.guard_areas or J.guard_areas
  -- v2: each room `area` tag's cover class from the layout's `zone` tags (public / staff / restricted; the strictest if a tag has two)
  local rank = { public = 1, staff = 2, restricted = 3 }
  M.AREA_CLASS = {}
  for id = 1, #L.room_name do
    local R = L.rooms[L.room_name[id]]
    if R.area and R.zone and (rank[R.zone] or 0) > (rank[M.AREA_CLASS[R.area]] or 0) then M.AREA_CLASS[R.area] = R.zone end
  end
  M.JOB = job
  return M
end
