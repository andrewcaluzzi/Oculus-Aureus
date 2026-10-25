-- The Janus bank's map for the heist mode (designs/bank-heist.md sections 14, 15 and 17 (bank v2)): mode_heist.lua's tables (DOORS, AREAS,
-- VAULT_PADS, VAULT_CRATE_PADS, CIV_PLACES, P, CARRIERS, CAM_LABEL / CAM_AREA, GUARD_AREAS, intel_room, VAULT_ROOMS, MAP) are built here,
-- at stage start. Everything is looked up by name in LEVEL (tools/gen_level.py's levelgen_layout.lua, from tools/levelgen/janus_bank.json,
-- written by tools/levelgen/build_janus_bank.py), so the layout can change without touching the mode.
--   include("levelgen_layout"); include("heist_bank_map")      -- at the script's top level (include() works only there)
--   on("stage_start", function() local M = heist_bank_map() ... end)   -- called once the stage is up (it reads pad positions)
-- Shipped as Data/Scripts/modes/heist_bank_map.lua. heist_bank_map() raises on a name the layout lacks.
--
-- Bank v2 (six levels, 107 rooms): the same names as v1 for everything the mode and its tests use (the hall, the tellers and the flap,
-- the GREEN / BLUE / RED doors, the vault doors, the crack pad, the safe, the cells, the console, the screens, the van and the dock),
-- plus the new areas: the mezzanine (the galleries round the hall), the upper offices (F1, F2), the executive floor (F3), the plant level
-- (B2: the power room and the generator, section 16). Areas are groups of rooms by name here; the rooms' own `area` tags (the cover
-- classes) and `zone` tags (public / staff / restricted) come from the layout.

local function need(v, what)
  if v == nil then error("heist_bank_map: the layout has no " .. what) end
  return v
end

-- the guard areas: rooms by name, the named posts first, the alarm box, the share of the posted guards, the raisers
local AREA_DEF = {
  { name = "hall", rooms = { "hall", "vestibule", "aisle_w", "aisle_e" }, posts = { "hall_post_sw", "hall_post_se", "hall_post_ne",
      "hall_post_nw", "hall_mid", "hall_entry_w", "hall_entry_e", "teller_mid", "teller_back", "flap_s", "flap_n", "vest_mid", "aisle_w_s",
      "aisle_e_n" }, panel = "alarm_hall", share = 14, raisers = 3 },
  { name = "mezzanine", rooms = { "gallery_s", "gallery_w", "gallery_e", "corr1_n", "mezz", "stair_foot", "stair_grand" },
    posts = { "gal_w_mid", "gal_e_mid", "gal_s_w", "c1n_mid", "mezz_mid" }, panel = "alarm_mezz", share = 6, raisers = 1 },
  { name = "corridor", rooms = { "back_corr_w", "back_corr_e", "rear_corr", "canteen", "kitchen", "staff_lounge" },
    posts = { "back_w", "back_1", "back_2", "back_3", "back_4", "back_e1", "back_e2", "back_e3", "back_e", "rear_mid" },
    panel = "alarm_corridor", share = 9, raisers = 1 },
  { name = "offices", rooms = { "office_corr", "corr_w", "office_a", "office_b", "meeting_g", "typing_pool" },
    posts = { "ocorr_s", "ocorr_mid", "ocorr_n", "office_a_mid", "office_a_1", "office_a_2", "office_b_1", "office_b_2", "corr_w_mid", "pool_mid" },
    panel = "alarm_offices", share = 7, raisers = 1 },
  { name = "upper1", rooms = { "corr1_w", "corr1_e", "loan_1", "loan_2", "meeting_1", "open_plan_1", "corr1_nw", "training", "advisor_1",
      "advisor_2", "it_office", "corr1_ne", "conf_w", "conf_e", "corr1_rear_w", "corr1_rear_e" },
    posts = { "c1w_mid", "c1e_mid", "op1_mid", "c1nw_e", "c1ne_w" }, panel = "alarm_upper1", share = 6, raisers = 1 },
  { name = "upper2", rooms = { "corr2_s", "corr2_w", "corr2_e", "corr2_n", "open_plan_2w", "open_plan_2e", "accounts", "it_dept", "library",
      "corr2_nw", "corr2_ne", "corr2_rear" }, posts = { "loggia_mid", "c2w_mid", "c2e_mid", "op2_mid", "lib_mid" },
    panel = "alarm_upper2", share = 5, raisers = 1 },
  { name = "executive", rooms = { "corr3", "secretary", "director", "boardroom", "exec_lounge", "roof" },
    posts = { "exec_mid", "exec_w", "director_1", "roof_door" }, panel = "alarm_exec", share = 4, raisers = 1 },
  -- (not the barracks: its door opens for the reinforcements only, guard_access "role:barracks,squad,wave,response"; the squads come
  -- out of it, P.squad / P.barracks)
  { name = "security", rooms = { "sec_office", "sec_corr", "guard_room", "armory", "cash_office", "lockers", "server_room", "comms" },
    posts = { "sec_mid", "sec_1", "sec_2", "guard_1", "guard_2", "guard_3", "sec_corr_mid" },
    panel = "alarm_security", share = 7, raisers = 1 },
  { name = "dock", rooms = { "dock", "alley", "garage" }, posts = { "dock_mid", "dock_1", "dock_2", "dock_3", "alley_s", "alley_dock", "alley_n",
      "garage_2" }, panel = "alarm_dock", share = 10, raisers = 2 },
  { name = "records", rooms = { "records", "archive_1", "archive_2" }, posts = { "rec_1", "rec_2", "rec_3", "rec_4", "rec_aisle" },
    panel = "alarm_corridor", share = 6, raisers = 1 },
  { name = "vault_ante", rooms = { "ante_a", "stairs", "b1_lobby", "vault_corr", "counting_room" }, posts = { "ante_1", "ante_2", "ante_3",
      "ante_4", "stair_top", "stair_mid", "stair_bot", "vcorr_mid", "b1_lobby_1" }, panel = "alarm_vault", share = 14, raisers = 2 },
  { name = "deposit", rooms = { "deposit", "service_ramp", "cash_processing", "deposit_boxes" }, posts = { "dep_mid", "dep_1", "dep_2", "dep_3",
      "ramp_top", "ramp_mid", "ramp_bot", "cashp_3" }, panel = "alarm_b1", share = 7, raisers = 1 },
  { name = "manager", rooms = {}, posts = { "mgr_1" }, panel = nil, share = 1, raisers = 0 },
  { name = "plant", rooms = { "b2_corr", "archive_vault", "tunnel_1" }, posts = { "b2_mid", "b2_e", "deep_1" }, panel = "alarm_plant",
    share = 4, raisers = 0 },   -- (not the power room itself: the power cut's investigators go there, section 16)
}

local CAMS = {
  { "cam_hall_s", "BANKING HALL", "hall" }, { "cam_hall_n", "TELLERS", "hall" }, { "cam_hall_e", "HALL ARCADE", "hall" },
  { "cam_gallery_w", "WEST GALLERY", "mezzanine" }, { "cam_gallery_e", "EAST GALLERY", "mezzanine" }, { "cam_mezz", "GRAND STAIR", "mezzanine" },
  { "cam_offices", "OFFICES", "offices" }, { "cam_typing", "TYPING POOL", "offices" }, { "cam_corr_w", "OFFICE CORRIDOR", "offices" },
  { "cam_corridor", "STAFF CORRIDOR", "corridor" }, { "cam_back_e", "EAST CORRIDOR", "corridor" },
  { "cam_security", "SECURITY", "security" }, { "cam_sec_corr", "SECURITY WING", "security" }, { "cam_server", "SERVER ROOM", "security" },
  { "cam_dock", "LOADING DOCK", "dock" }, { "cam_garage", "GARAGE", "dock" },
  { "cam_corr1_w", "LOAN OFFICES", "upper1" }, { "cam_open_plan_1", "OPEN PLAN 1", "upper1" },
  { "cam_loggia", "LOGGIA", "upper2" }, { "cam_library", "LIBRARY", "upper2" },
  { "cam_exec", "EXECUTIVE FLOOR", "executive" }, { "cam_roof", "ROOF", "executive" },
  { "cam_b1_lobby", "VAULT STAIRS", "vault_ante" }, { "cam_vault_corr", "VAULT CORRIDOR", "vault_ante" },
  { "cam_vault", "VAULT ANTECHAMBER", "vault_ante" }, { "cam_counting", "COUNTING ROOM", "vault_ante" },
  { "cam_plant", "PLANT CORRIDOR", "plant" },
}

function heist_bank_map()
  local L = LEVEL
  local function obj(n) return need(L.objects[n], "object " .. n) end
  local function leaves(n) local o = obj(n); return o.pair and { o.index, o.pair } or { o.index } end
  local function pad(n) return need(L.pads[n], "pad " .. n) end
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
  -- every door of a card tier (by its key flags), double doors' second leaves too, in setup order; not the cells (their own entries)
  local function tier(flags)
    local out = {}
    for _, d in ipairs(L.doors) do
      if d.object and d.keyflags == flags then
        local o
        for n, oo in pairs(L.objects) do if oo.index == d.object then o = n end end
        if o ~= "cell_green" and o ~= "cell_floor" then
          out[#out + 1] = d.object
          if d.object2 then out[#out + 1] = d.object2 end
        end
      end
    end
    table.sort(out)
    return out
  end

  local M = {}
  M.DOORS = {
    front = leaves("front_door"),
    blast = cat(leaves("blast_w"), leaves("blast_e")),
    green = tier(0x2),                         -- green_hall (double), green_back, green_aisle, the maintenance door
    blue = tier(0x4),                          -- blue_hall (double), the manager's, the security wing's back doors, the director's ...
    red = tier(0x8),                           -- red_stairs (double), red_ramp, the plant gate and the power room
    vault_a = leaves("vault_door_a"),          -- the crack door (antechamber -> vault)
    vault_b = leaves("vault_door_b"),          -- the second vault door (vault -> deposit hall)
    safe = obj("mgr_safe").index,
    cell_floor = obj("cell_floor").index, cell_green = obj("cell_green").index,
    dock_shutter = obj("dock_shutter").index,  -- the alley's way in (padlocked; exit B is the dock itself)
    tunnel_grate = obj("tunnel_grate").index,  -- v2: the canal tunnel's way in (padlocked)
  }
  M.AREAS = {}
  for _, a in ipairs(AREA_DEF) do
    M.AREAS[#M.AREAS + 1] = { name = a.name, pads = area_pads(a.posts, a.rooms), panel = a.panel and obj(a.panel).pad or nil,
                              share = a.share, raisers = a.raisers }
  end
  M.AREA_LABEL = { hall = "banking hall", mezzanine = "mezzanine", corridor = "staff corridors", offices = "offices", upper1 = "first floor",
                   upper2 = "second floor", executive = "executive floor", security = "security wing", dock = "loading dock", records = "records",
                   vault_ante = "vault level", deposit = "deposit hall", manager = "manager's office", vault = "vault", plant = "plant level" }
  M.VAULT_ROOMS = { [room("vault").id] = true }
  M.VAULT_PADS = area_pads({ "vault_in_a", "vault_in_b", "vault_captain", "vault_g1", "vault_g2", "vault_g3" }, { "vault" })
  M.VAULT_CRATE_PADS = {}   -- (the crates stand by the walls, off every pad)
  M.CIV_PLACES = {
    { "hall", { pad("teller_1"), pad("teller_2"), pad("teller_3"), pad("teller_4"), pad("cust_1"), pad("cust_2"), pad("cust_3"), pad("cust_4"),
                pad("cust_5"), pad("cust_6"), pad("cust_7"), pad("teller_mid"), pad("teller_back") }, 8 },
    { "offices", { pad("office_a_1"), pad("office_a_2"), pad("office_b_1"), pad("office_b_2"), pad("pool_mid") }, 3 },
    { "upper1", { pad("loan_1_1"), pad("loan_2_1"), pad("advisor_1_1"), pad("advisor_2_1") }, 2 },
    { "security", { pad("sec_1"), pad("sec_2") }, 2 },
    { "records", { pad("rec_1"), pad("rec_2"), pad("rec_3") }, 1 },
    { "manager", { pad("mgr_1"), pad("mgr_desk") }, 2 },
    { "executive", { pad("secretary_1"), pad("director_1") }, 1 },
  }
  -- the card carriers' candidate pads
  M.CARRIERS = {
    teller = { pad("teller_1"), pad("teller_2"), pad("teller_3") },
    captain = { pad("hall_mid"), pad("hall_post_sw"), pad("hall_post_se") },
    chief = { pad("office_a_1"), pad("office_a_mid"), pad("office_b_1"), pad("ocorr_mid") },
    manager = { pad("mgr_desk"), pad("mgr_1") },                -- the branch manager in his own office (behind BLUE)
    vault_captain = { pad("vault_captain"), pad("vault_g3") },
  }
  local function mon_xz(n) local o = obj(n); return { o.x, o.z } end
  M.P = {
    start = { L.start_pad, pad("start_1"), pad("start_2"), pad("start_3") },   -- the street
    exit_a = pad("van"), exit_b = pad("dock_exit"),
    crack = pad("crack"), crack_xz = xz("crack"),                -- in front of vault door A
    inside = { xz("vault_in_a"), xz("vault_in_b") },             -- re-crack zones inside door A and door B
    vault = {},                                                  -- the vault is one room: VAULT_ROOMS covers it
    console = pad("console"),                                    -- the security office's camera console (BLUE)
    monitors = { mon_xz("screen_1"), mon_xz("screen_2"), mon_xz("screen_3"), mon_xz("screen_4"), mon_xz("screen_5"), mon_xz("screen_6"),
                 mon_xz("screen_7"), mon_xz("screen_8") },        -- the hall's security-screen wall (its south wall)
    waves_front = { pad("hall_entry_w"), pad("hall_entry_e"), pad("vest_mid") },   -- just inside the blast shutters
    waves_dock = { pad("dock_exit"), pad("dock_mid"), pad("dock_2") },
    squad = { pad("stair_top"), pad("barracks_1"), pad("barracks_2") },   -- crack squads: down the RED stairs, and out of the barracks
    barracks = { pad("barracks_1"), pad("barracks_2"), pad("barracks_3"), pad("guard_1"), pad("guard_2") },   -- v2: the squads' room
    heavy = { pad("ante_1"), pad("ante_2"), pad("dep_1"), pad("dep_2") },       -- outside the vault doors
    safe_loot = loot("safe_loot"),
    cell_loot = { loot("cell1_cash"), loot("cell2_cash") },      -- cell_green (GREEN), cell_floor (the floor key in records)
    vault_loot = loot("vault_gold"),                             -- the vault's loot pads: the bars are stacked round each
    extra_loot = { cash_office = loot("cash_office_loot"), cash_processing = loot("cashp_loot"), boxes = loot("box_loot"),
                   director = loot("director_loot"), deep = loot("deep_loot") },   -- v2: more loot spots (not yet used by the mode)
    outside = pad("ante_1"),                                     -- (tests) outside the crack zone and the vault
    screens_at = xz("screens"),                                  -- (tests) in front of the screen wall, facing it
    screens_face = { obj("screen_2").x, obj("screen_2").z - 100 },
    feed_cam = obj("cam_hall_s").index,                          -- (tests) the hall's customer-side camera
    exit_c = pad("canal_outfall"),                               -- v2: the canal end of the maintenance tunnel (a third way in)
    -- the power (section 16; optional: a layout without them has no power cut): the plant room's main breaker and backup generator
    -- (props), the pad in front of the breaker the investigators walk to, and where they come from (the plant corridor outside)
    breaker = L.objects.breaker and { L.objects.breaker.x, L.objects.breaker.z } or nil,
    generator = L.objects.generator and { L.objects.generator.x, L.objects.generator.z } or nil,
    power_to = L.pads.breaker_front, power_way = L.pads.power_way, power_mid = L.pads.power_mid,
    generator_front = L.pads.generator_front,
  }
  M.power_room = L.rooms.power and L.rooms.power.id or nil
  M.MAP = { name = "bank", label = "the Janus bank", manager_area = "manager", vault_hint = "down the RED stairs, a floor down",
            no_cams = "(records, deposit: no cams)" }
  M.dropped = dropped                                            -- grid pads kept off (blocks, props, doorways)
  M.intel_room = room("hall").id                                 -- the hall shows the intel box (the screens are there)
  M.CAM_LABEL, M.CAM_AREA = {}, {}
  for _, c in ipairs(CAMS) do
    local i = obj(c[1]).index
    M.CAM_LABEL[i], M.CAM_AREA[i] = c[2], c[3]
  end
  M.GUARD_AREAS = { "hall", "mezzanine", "security", "offices", "upper1", "upper2", "corridor", "dock", "vault_ante", "executive" }
  -- v2: each room `area` tag's cover class from the layout's `zone` tags (public / staff / restricted; the strictest if a tag has two)
  local rank = { public = 1, staff = 2, restricted = 3 }
  M.AREA_CLASS = {}
  for id = 1, #L.room_name do
    local R = L.rooms[L.room_name[id]]
    if R.area and R.zone and (rank[R.zone] or 0) > (rank[M.AREA_CLASS[R.area]] or 0) then M.AREA_CLASS[R.area] = R.zone end
  end
  return M
end
