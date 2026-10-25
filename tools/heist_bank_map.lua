-- The Janus bank's map for the heist mode (designs/bank-heist.md sections 14 and 15): mode_heist.lua's tables (DOORS, AREAS, VAULT_PADS,
-- VAULT_CRATE_PADS, CIV_PLACES, P, CARRIERS, CAM_LABEL / CAM_AREA, GUARD_AREAS, intel_room, VAULT_ROOMS, MAP) are built here, at stage
-- start. Everything is looked up by name in LEVEL (tools/gen_level.py's levelgen_layout.lua, from tools/levelgen/janus_bank.json), so
-- the layout can change without touching the mode. (Their shapes are those of the Bunker 2 tables the mode had until P2.4.)
--   include("levelgen_layout"); include("heist_bank_map")      -- at the script's top level (include() works only there)
--   on("stage_start", function() local M = heist_bank_map() ... end)   -- called once the stage is up (it reads pad positions)
-- Shipped as Data/Scripts/modes/heist_bank_map.lua. heist_bank_map() raises on a name the layout lacks.

local function need(v, what)
  if v == nil then error("heist_bank_map: the layout has no " .. what) end
  return v
end

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

  local M = {}
  -- doors (setup indices; a double door is both leaves). Bunker 2: front 72, blast 80-85, green 46-49, blue 50-53, red 58-61,
  -- vault_a 68/69, vault_b 70/71, safe 102, cell_floor 75, cell_green 77
  M.DOORS = {
    front = leaves("front_door"),
    blast = cat(leaves("blast_w"), leaves("blast_e")),
    green = cat(leaves("green_hall"), leaves("green_back")),
    blue = cat(leaves("blue_hall"), leaves("manager_door")),
    red = cat(leaves("red_stairs"), leaves("red_ramp")),
    vault_a = leaves("vault_door_a"),          -- the crack door (antechamber -> vault)
    vault_b = leaves("vault_door_b"),          -- the second vault door (vault -> deposit hall)
    safe = obj("mgr_safe").index,
    cell_floor = obj("cell_floor").index, cell_green = obj("cell_green").index,
    dock_shutter = obj("dock_shutter").index,  -- new: the alley's way in (padlocked; exit B is the dock itself)
  }
  -- areas: name, pads (waypoint pads first), alarm panel pad (an alarm box's bound pad: the stock raiser runs to the box), share,
  -- raisers. Bunker 2's hall / central / offices / security / west / south / east / cells / safe become hall / corridor / offices /
  -- security / dock / records / vault_ante / deposit / manager.
  M.AREAS = {
    { name = "hall", pads = area_pads({ "hall_post_sw", "hall_post_se", "hall_post_ne", "hall_post_nw", "hall_mid", "hall_entry_w",
        "hall_entry_e", "teller_mid", "teller_back", "flap_s", "flap_n", "vest_mid" }, { "hall", "vestibule" }),
      panel = obj("alarm_hall").pad, share = 14, raisers = 3 },   -- (12 raisers in all at the full force, as on Bunker 2)
    { name = "corridor", pads = area_pads({ "back_w", "back_1", "back_2", "back_3", "back_4", "back_e" }, { "back_corr" }),
      panel = obj("alarm_corridor").pad, share = 9, raisers = 1 },
    { name = "offices", pads = area_pads({ "ocorr_s", "ocorr_mid", "ocorr_n", "office_a_mid", "office_a_1", "office_a_2", "office_a_3",
        "office_b_1", "office_b_2", "office_b_3" }, { "office_corr", "office_a", "office_b" }),
      panel = obj("alarm_offices").pad, share = 7, raisers = 1 },
    { name = "security", pads = area_pads({ "sec_mid", "sec_1", "sec_2", "guard_1", "guard_2", "guard_3" }, { "sec_office", "guard_room" }),
      panel = obj("alarm_security").pad, share = 5, raisers = 1 },
    { name = "dock", pads = area_pads({ "dock_mid", "dock_1", "dock_2", "dock_3", "alley_s", "alley_dock", "alley_n" }, { "dock", "alley" }),
      panel = obj("alarm_dock").pad, share = 12, raisers = 2 },
    { name = "records", pads = area_pads({ "rec_1", "rec_2", "rec_3", "rec_4", "rec_aisle" }, { "records" }),
      panel = obj("alarm_corridor").pad, share = 8, raisers = 1 },
    { name = "vault_ante", pads = area_pads({ "ante_1", "ante_2", "ante_3", "ante_4", "stair_top", "stair_mid", "stair_bot" },
        { "ante_a", "stairs" }), panel = obj("alarm_vault").pad, share = 14, raisers = 2 },
    { name = "deposit", pads = area_pads({ "dep_mid", "dep_1", "dep_2", "dep_3", "ramp_top", "ramp_mid", "ramp_bot" },
        { "deposit", "service_ramp" }), panel = obj("alarm_vault").pad, share = 7, raisers = 1 },
    { name = "manager", pads = area_pads({ "mgr_1" }, {}), panel = nil, share = 1, raisers = 0 },
  }
  M.AREA_LABEL = { hall = "banking hall", corridor = "staff corridor", offices = "offices", security = "security wing", dock = "loading dock",
                   records = "records", vault_ante = "vault antechamber", deposit = "deposit hall", manager = "manager's office", vault = "vault" }
  M.VAULT_ROOMS = { [room("vault").id] = true }
  M.VAULT_PADS = area_pads({ "vault_in_a", "vault_in_b", "vault_captain", "vault_g1", "vault_g2", "vault_g3" }, { "vault" })
  M.VAULT_CRATE_PADS = {}   -- (Bunker 2's crate pads, whose bars go a step aside; the bank's crates stand by the walls, off every pad)
  M.CIV_PLACES = {
    { "hall", { pad("teller_1"), pad("teller_2"), pad("teller_3"), pad("cust_1"), pad("cust_2"), pad("cust_3"), pad("cust_4"), pad("cust_5"),
                pad("teller_mid"), pad("teller_back") }, 8 },
    { "offices", { pad("office_a_1"), pad("office_a_2"), pad("office_b_1"), pad("office_b_2") }, 4 },
    { "security", { pad("sec_1"), pad("sec_2") }, 2 },
    { "records", { pad("rec_1"), pad("rec_2"), pad("rec_3") }, 2 },
    { "manager", { pad("mgr_1"), pad("mgr_desk") }, 2 },
  }
  -- the card carriers' candidate pads (Bunker 2: teller 58/59/65, captain 54/55/62, chief 72-76, manager 78/79/83, vault captain 9/18)
  M.CARRIERS = {
    teller = { pad("teller_1"), pad("teller_2"), pad("teller_3") },
    captain = { pad("hall_mid"), pad("hall_post_sw"), pad("hall_post_se") },
    chief = { pad("office_a_1"), pad("office_a_mid"), pad("office_b_1"), pad("ocorr_mid") },
    manager = { pad("mgr_desk"), pad("mgr_1") },                -- the branch manager sits in his own office now (behind BLUE)
    vault_captain = { pad("vault_captain"), pad("vault_g3") },
  }
  local function mon_xz(n) local o = obj(n); return { o.x, o.z } end
  M.P = {
    start = { L.start_pad, pad("start_1"), pad("start_2"), pad("start_3") },   -- the street
    exit_a = pad("van"), exit_b = pad("dock_exit"),
    crack = pad("crack"), crack_xz = xz("crack"),                -- in front of vault door A
    inside = { xz("vault_in_a"), xz("vault_in_b") },             -- re-crack zones inside door A and door B
    vault = {},                                                  -- the vault is one room: VAULT_ROOMS covers it (no circles needed)
    console = pad("console"),                                    -- the security office's camera console (BLUE)
    monitors = { mon_xz("screen_1"), mon_xz("screen_2"), mon_xz("screen_3"), mon_xz("screen_4"), mon_xz("screen_5"), mon_xz("screen_6"),
                 mon_xz("screen_7"), mon_xz("screen_8") },        -- the hall's security-screen wall
    waves_front = { pad("hall_entry_w"), pad("hall_entry_e"), pad("vest_mid") },   -- just inside the blast shutters
    waves_dock = { pad("dock_exit"), pad("dock_mid"), pad("dock_2") },
    squad = { pad("stair_top"), pad("back_3"), pad("back_4") },               -- crack squads come down the RED stairs
    heavy = { pad("ante_1"), pad("ante_2"), pad("dep_1"), pad("dep_2") },       -- outside the vault doors
    safe_loot = loot("safe_loot"),
    cell_loot = { loot("cell1_cash"), loot("cell2_cash") },      -- cell_green (GREEN), cell_floor (the floor key in records)
    vault_loot = loot("vault_gold"),                             -- the vault's loot pads: the bars are stacked round each
    outside = pad("ante_1"),                                     -- (tests) outside the crack zone and the vault
    screens_at = xz("screens"),                                  -- (tests) in front of the screen wall, facing it
    screens_face = { obj("screen_2").x, (obj("screen_2").z + obj("screen_3").z) / 2 },
    feed_cam = obj("cam_hall_s").index,                          -- (tests) the hall's customer-side camera
  }
  M.MAP = { name = "bank", label = "the Janus bank", manager_area = "manager", vault_hint = "down the RED stairs",
            no_cams = "(records, deposit: no cams)" }
  M.dropped = dropped                                            -- grid pads kept off (blocks, props, doorways)
  M.intel_room = room("hall").id                                 -- the hall shows the intel box (the screens are there)
  M.CAM_LABEL, M.CAM_AREA = {}, {}
  for _, c in ipairs({ { "cam_hall_s", "BANKING HALL", "hall" }, { "cam_hall_n", "TELLERS", "hall" }, { "cam_offices", "OFFICES", "offices" },
                       { "cam_corridor", "STAFF CORRIDOR", "corridor" }, { "cam_security", "SECURITY", "security" },
                       { "cam_dock", "LOADING DOCK", "dock" }, { "cam_vault", "VAULT ANTECHAMBER", "vault_ante" } }) do
    local i = obj(c[1]).index
    M.CAM_LABEL[i], M.CAM_AREA[i] = c[2], c[3]
  end
  M.GUARD_AREAS = { "hall", "offices", "security", "corridor", "dock", "vault_ante" }   -- the areas a camera covers
  return M
end
