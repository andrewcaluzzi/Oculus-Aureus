-- tools/jobs/bank.lua -> Data/Scripts/modes/job_bank.lua: the Janus bank's JOB DEFINITION for the heist mode (designs/job2-casino.md
-- section 6: the job table). Everything the heist mode, its map reader and its INTEL board / first-play guide used to hard-code about
-- the bank lives here: the layout names (areas, cameras, doors, pads), the card carriers (roles, bodies, posts), and the job's words.
-- The values are the ones mode_heist.lua, heist_bank_map.lua, heist_intel.lua and heist_guide.lua had before the job table (zero
-- behaviour change for the bank). A second job (tools/jobs/casino.lua) fills the same table for its own layout; the mode picks the job
-- named by the layout (LEVEL.script.heist.job, default "bank"): heist_job() in tools/heist_bank_map.lua.
--
-- The shape (every key a job sets; a casino-style job copies this file and changes the values):
--   map       the layout's names: areas / cams / guard_areas (used when the layout's LEVEL.script.heist has none), doors, rooms, pads
--   cards     the card ladder the mode spawns (in this order; `either` = one of these, chosen by the sim RNG)
--   text      the mode's HUD and flash lines
--   guide     the first-play guide's steps (tools/heist_guide.lua)
--   intel     the INTEL board's words (tools/heist_intel.lua)
--   settings  overrides of the mode's settings table S (applied before HEIST_SETTINGS); the bank has none

HEIST_JOBS = HEIST_JOBS or {}

HEIST_JOBS.bank = {
  name = "bank",
  -- the object the mode checks for at stage start (no such object: not this job's level)
  check_object = "vault_door_a",

  -- ----------------------------------------------------------------------------------------------------------------------------------
  map = {
    -- the guard areas (bank v2; bank v3 carries its own in LEVEL.script.heist.areas): rooms by name, the named posts first, the alarm
    -- box, the share of the posted guards, the raisers
    areas = {
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
    },
    cams = {
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
    },
    guard_areas = { "hall", "mezzanine", "security", "offices", "upper1", "upper2", "corridor", "dock", "vault_ante", "executive" },
    area_label = { hall = "banking hall", mezzanine = "mezzanine", corridor = "staff corridors", offices = "offices", upper1 = "first floor",
                   upper2 = "second floor", executive = "executive floor", security = "security wing", dock = "loading dock", records = "records",
                   vault_ante = "vault level", deposit = "deposit hall", manager = "manager's office", vault = "vault", plant = "plant level" },
    -- the doors the mode drives (object names): the front, the lockdown shutters, the time-locked pair, the GOLD safe, the two cells;
    -- `extra` are looked up into M.DOORS too (the padlocked ways in)
    doors = { front = "front_door", blast = { "blast_w", "blast_e" }, vault_a = "vault_door_a", vault_b = "vault_door_b", safe = "mgr_safe",
              cell_green = "cell_green", cell_floor = "cell_floor", extra = { dock_shutter = "dock_shutter", tunnel_grate = "tunnel_grate" } },
    vault_room = "vault",                                      -- the room the time lock seals (VAULT_ROOMS)
    vault_pads = { "vault_in_a", "vault_in_b", "vault_captain", "vault_g1", "vault_g2", "vault_g3" },
    intel_room = "hall",                                       -- the room with the screens (the intel box shows there)
    power_room = "power",
    -- civilians: { area, pad names, count }
    civ_places = {
      { "hall", { "teller_1", "teller_2", "teller_3", "teller_4", "cust_1", "cust_2", "cust_3", "cust_4", "cust_5", "cust_6", "cust_7",
                  "teller_mid", "teller_back" }, 8 },
      { "offices", { "office_a_1", "office_a_2", "office_b_1", "office_b_2", "pool_mid" }, 3 },
      { "upper1", { "loan_1_1", "loan_2_1", "advisor_1_1", "advisor_2_1" }, 2 },
      { "security", { "sec_1", "sec_2" }, 2 },
      { "records", { "rec_1", "rec_2", "rec_3" }, 1 },
      { "manager", { "mgr_1", "mgr_desk" }, 2 },
      { "executive", { "secretary_1", "director_1" }, 1 },
    },
    -- the card carriers' candidate pads (keys are the `pads` of the cards below)
    carriers = {
      teller = { "teller_1", "teller_2", "teller_3" },
      captain = { "hall_mid", "hall_post_sw", "hall_post_se" },
      chief = { "office_a_1", "office_a_mid", "office_b_1", "ocorr_mid" },
      manager = { "mgr_desk", "mgr_1" },                       -- the branch manager in his own office (behind BLUE)
      vault_captain = { "vault_captain", "vault_g3" },
    },
    -- the mode's places (pad names unless said): M.P
    P = {
      start = { "start_1", "start_2", "start_3" },             -- after the layout's start pad (the street)
      exit_a = "van", exit_b = "dock_exit",
      crack = "crack",                                         -- in front of vault door A
      inside = { "vault_in_a", "vault_in_b" },                 -- re-crack zones inside door A and door B
      console = "console",                                     -- the security office's camera console (BLUE)
      monitors = { "screen_1", "screen_2", "screen_3", "screen_4", "screen_5", "screen_6", "screen_7", "screen_8" },   -- objects
      waves_front = { "hall_entry_w", "hall_entry_e", "vest_mid" },   -- just inside the blast shutters
      waves_dock = { "dock_exit", "dock_mid", "dock_2" },
      squad = { "stair_top", "barracks_1", "barracks_2" },     -- crack squads: down the RED stairs, and out of the barracks
      barracks = { "barracks_1", "barracks_2", "barracks_3", "guard_1", "guard_2" },
      heavy = { "ante_1", "ante_2", "dep_1", "dep_2" },        -- outside the vault doors
      safe_loot = "safe_loot", cell_loot = { "cell1_cash", "cell2_cash" }, vault_loot = "vault_gold",   -- LEVEL.loot names
      extra_loot = { cash_office = "cash_office_loot", cash_processing = "cashp_loot", boxes = "box_loot", director = "director_loot",
                     deep = "deep_loot" },
      outside = "ante_1",                                      -- (tests) outside the crack zone and the vault
      screens_at = "screens", screens_face = "screen_2",       -- (tests) in front of the screen wall (a pad), facing it (an object)
      feed_cam = "cam_hall_s",                                 -- (tests) the hall's customer-side camera (an object)
      exit_c = "canal_outfall",                                -- v2: the canal end of the maintenance tunnel (a third way in)
    },
    MAP = { name = "bank", label = "the Janus bank", manager_area = "manager", vault_hint = "down the RED stairs, a floor down",
            no_cams = "(records, deposit: no cams)" },
  },

  -- ----------------------------------------------------------------------------------------------------------------------------------
  -- the card ladder (mode_heist.lua queue_carriers, in this order). kind "civ" (a civilian who hands it over at gunpoint) or "guard"
  -- (posted; KO and search). body: a body name, or body_setting = the S key that holds it. pads: a key of map.carriers.
  cards = {
    { key = "GREEN", either = {
        { role = "head teller", kind = "civ", pads = "teller", body = "Civilian_1_Female", area = "hall" },
        { role = "lobby captain", kind = "guard", pads = "captain", body = "St_Petersburg_Guard", area = "hall" } } },
    { key = "BLUE", role = "security chief", kind = "guard", pads = "chief", body_setting = "chief_body", health = 1.5, area = "offices" },
    { key = "RED", role = "branch manager", kind = "civ", pads = "manager", body_setting = "manager_body", area = "@manager" },  -- @: MAP.manager_area
    { key = "GOLD", role = "vault captain", kind = "guard", pads = "vault_captain", body = "Russian_Commandant", health = 2, armour = 1, area = "vault" },
  },
  -- the security console's report (who carries what), in this order
  console_roles = { "head teller", "lobby captain", "security chief", "branch manager", "vault captain" },

  -- ----------------------------------------------------------------------------------------------------------------------------------
  text = {
    hud_title = "JANUS BANK",
    not_here = "HEIST MODE: run it on the Janus bank (GE_STAGE=levelgen, janus_bank.json on the slot)",
    not_here_log = "not the Janus bank",
    intro_cover = "JANUS BANK. You're staff: gun holstered. Steal the gold.",
    intro_plain = "JANUS BANK, ST PETERSBURG. Steal the gold. Hurt no staff.",
    need_green = "GREEN card: head teller or lobby captain",
    need_blue = "BLUE card: the security chief",
    need_red = "RED card: the branch manager",
    crack = "Crack the vault (",                               -- .. MAP.vault_hint .. ")"
    grab = "Grab the gold!",
    get_out = "Get out: the van or the loading dock",
    alarm_flash = "ALARM! Guards are on their way - the hall is locked down",
    vault_open = "The vault is open! Grab the gold",
    safe_open = "The manager's safe: gold!",
    safe_log = "the manager's safe is open: %d bars",
    negotiator = "Negotiator: We can talk about this. Nobody else has to get hurt.",   -- (the hostage stand-off, section 31)
  },

  -- ----------------------------------------------------------------------------------------------------------------------------------
  guide = {
    in_what = "Next: walk in the bank's front door, gun holstered",
    in_how_cover = "Holstered you pass as staff. A drawn gun makes guards look",
    in_how = "Get inside the bank",
    -- one per card the crew needs before the vault, in order: what (line 2), how (line 3); role: a carrier whose room is named after
    -- `what` (in brackets; `none` when he is nowhere)
    steps = {
      { key = "GREEN", id = "green", what = "Next: GREEN card - head teller or lobby captain (hall)",
        how = "Teller: aim a gun at her. Captain: slap him twice, USE to search" },
      { key = "BLUE", id = "blue", what = "Next: BLUE card - the security chief", role = "security chief", none = " (offices)",
        how = "Catch him alone: slap twice (he's tough), then USE to search" },
      { key = "RED", id = "red", what = "Next: RED card - the branch manager", role = "branch manager", none = "",
        how = "Aim a gun at him and he hands it over (BLUE opens the offices)" },
    },
    vault_what = "Next: the vault - down the RED stairs, a floor down",
    vault_how = "Pick the DOOR DECODER (next weapon), fire it at vault door A",
  },

  -- ----------------------------------------------------------------------------------------------------------------------------------
  intel = {
    title = "JANUS BANK, ST PETERSBURG  -  CREW EYES ONLY",
    operation = "Operation: relieve the Janus bank of its gold. Six steps. Tick them off.",
    keys = {
      GREEN = { who = "the head teller OR the lobby captain", where = "banking hall: one of the two, the plan doesn't say which",
                roles = { "head teller", "lobby captain" },
                how = "Teller: aim at her till she gives it up. Captain: knock him out and search him (or rob him at gunpoint).",
                short = "head teller or lobby captain" },
      BLUE = { who = "the security chief", where = "the offices behind GREEN", roles = { "security chief" },
               how = "The chief is tough (x1.5 health). Catch him alone in the offices: KO and search.", short = "the security chief carries it" },
      RED = { who = "the branch manager", where = "his own office (behind BLUE)", roles = { "branch manager" },
              how = "The manager is a civilian: aim at him and he hands it over. Don't kill him.", short = "the branch manager carries it" },
      GOLD = { who = "the vault captain", where = "inside the vault (armoured)", roles = { "vault captain" },
               how = "In the vault with the guards: only once it's open. Armoured, double health.", short = "the vault captain carries it" },
      FLOORKEY = { name = "FLOOR KEY", who = "nobody: it lies on a shelf", where = "records (ground floor)", roles = {},
                   how = "Walk into records (staff area) and pick it up off the shelf.", short = "on a shelf in records", word = "the floor key" },
    },
    -- THE PLAN's six steps: function(S, f) with f = { secs, money, floor (the lowest floor's name) } -> { { title, quiet, loud } x 6 }
    plan = function(S, f)
      local secs = f.secs
      local g = S.cover and "Walk in the front door: you're staff, gun holstered. The hall is public; staff rooms cost cover slowly, restricted rooms in a second."
                       or "Walk in the front door from the street."
      return {
        { "GET IN", g,
          "Same doors, gun out. Once the alarm goes the hall's blast shutters lock: BLUE card to pass." },
        { "GET THE KEYS", "GREEN (head teller or lobby captain), then BLUE (security chief, offices), then RED (branch manager). Aim at staff to rob them; knock guards out and search them.",
          "Drop the carrier and take the card off the body. Every card opens doors only for the Bond who holds it." },
        { "CAMS / POWER", string.format("Security office console (BLUE, press use): every camera off %s. Or the plant room on %s: cabinet %s + main breaker %s.",
            secs(S.cam_off_time), f.floor, secs(S.power_cabinet), secs(S.power_cut)),
          string.format("Power out = no siren, no panels, no cameras. Backup generator in %s unless you kill it (%s).", secs(S.generator_after), secs(S.generator_time)) },
        { "CRACK VAULT", string.format("Antechamber, a floor down behind RED. Equip the door decoder and fire it at vault door A (or press use). %s of work, only while a Bond is near it.",
            secs(S.crack_time)),
          string.format("It jams (%d%% every %s): fire it again. Two squads come for it even on a quiet run.", math.floor(S.jam_chance * 100 + 0.5), secs(S.jam_every)) },
        { "GRAB THE GOLD", string.format("Open %s, klaxon at T-%d. %d bars x %s. GOLD card (vault captain) opens the manager's safe.", secs(S.vault_time), S.klaxon_at,
            S.vault_bars, f.money(S.bar_value)),
          string.format("At 0 the time lock seals it: anyone inside is TRAPPED (alarm, heavy squad, re-crack %s from inside).", secs(S.recrack_time)) },
        { "GET OUT", "Every living Bond in an exit with the loot: the van on the street or the loading dock. No alarm all run = +" .. math.floor(S.quiet_bonus * 100 + 0.5) .. "%.",
          string.format("Waves every %s while hot, the elite response after %s. Exit B (dock) is never locked down.", secs(S.wave_interval), secs(S.response_after)) },
      }
    end,
    -- SCHEMATICS: room labels, the spots (objects), the exits, the crack's room, the carriers' posts
    labels = { hall = "BANKING HALL", sec_office = "SECURITY", manager = "MANAGER", vault = "VAULT", ante_a = "ANTE", records = "RECORDS",
               dock = "DOCK", power = "POWER", generator = "GENERATOR", deposit = "DEPOSIT", stairs = "RED STAIRS", typing_pool = "TYPING",
               office_a = "OFFICE A", office_b = "OFFICE B", canteen = "CANTEEN", garage = "GARAGE", director = "DIRECTOR", boardroom = "BOARD",
               street = "STREET", alley = "ALLEY", barracks = "BARRACKS", guard_room = "GUARDS", armory = "ARMORY", server_room = "SERVERS",
               b1_lobby = "B1 LOBBY", vault_corr = "VAULT CORR", service_ramp = "RAMP", counting_room = "COUNTING", roof = "ROOF",
               library = "LIBRARY", accounts = "ACCOUNTS", it_dept = "IT", cash_office = "CASH", lockers = "LOCKERS", mezz = "MEZZ" },
    safes = { "mgr_safe", "director_safe" },
    floor_key = "floor_key",
    crack_room = "ante_a",
    exit_a = "EXIT A: VAN", exit_b = "EXIT B: DOCK",
    posts = { { "teller", "GREEN", "GREEN? HEAD TELLER" }, { "captain", "GREEN", "GREEN? LOBBY CAPTAIN" }, { "chief", "BLUE", "BLUE: SECURITY CHIEF" },
              { "manager", "RED", "RED: MANAGER" }, { "vault_captain", "GOLD", "GOLD: VAULT CAPTAIN" } },
    -- THE PLAN's red ring for the keys step: the carriers' posts (map.carriers keys) for each card
    next_posts = { GREEN = { "teller", "captain" }, BLUE = { "chief" }, RED = { "manager" } },
    nokey = { "THE VAULT DOORS: time-locked. Only the cracker (the door decoder) opens them.",
              "PADLOCKS: the alley's dock shutter and the canal tunnel's grate. Shoot the padlock.",
              "LOCKDOWN: when the alarm goes the hall's blast shutters close and want BLUE." },
    -- PERSONNEL: function(S, f) with f = { secs, money, force } -> { { name, where, what, note } x 9 }
    personnel = function(S, f)
      local secs = f.secs
      return {
        { "TELLERS", "Behind the counter, unarmed.", "Aim at one: she surrenders, keep aiming and she hands over what she has (the head teller may hold GREEN).", "Civilians: killing them costs " .. f.money(S.civ_fine) .. " each and too many fails the job." },
        { "BRANCH MANAGER", "In his own office behind BLUE.", "Carries RED. Rob him at gunpoint.", "A civilian: hurt him and it counts." },
        { "LOBBY CAPTAIN", "On the hall floor (if the head teller hasn't got GREEN, he has).", "Armed guard. KO and search, or rob him at gunpoint.", "Judges your disguise like any guard." },
        { "SECURITY CHIEF", "Posted in the offices.", "Carries BLUE. 1.5x health.", "Guards the camera console's key: lose him and the security room is yours." },
        { "GUARDS", string.format("About %d posted on this difficulty: hall, offices, corridors, security, dock, vault level.", math.floor(S.guards_post * (f.force or 1) + 0.5)),
          "They stand post. They hear shots, see Bonds, find bodies.", "They treat you as staff until something gives you away, then 'Hey!' and a radio call." },
        { "ALARM RAISERS", "A few per area.", "On sight or sound they run for the area's alarm panel.", "Drop them before they get there, or cut the power first." },
        { "SECURITY ROOM", "The security wing's staff watch the cameras.", "A camera that times you relays to them: alarm " .. secs(S.security_delay) .. " later.", "Knock them all out and the cameras raise nothing." },
        { "VAULT GUARDS", "Asleep inside the vault, with the vault captain (GOLD).", "They wake when the vault opens and come straight at you.", "Armoured captain, double health." },
        { "SQUADS & WAVES", string.format("Two crack squads hit the cracker. Hot: waves every %s.", secs(S.wave_interval)), string.format("After %s hot: the armoured response, both entrances.", secs(S.response_after)),
          "Trapped in the vault: the heavy squad (armour, RC-P90s)." },
      }
    end,
    -- NOTES: the job's words in four of the rule cards
    notes = {
      hold_fire = "Guards won't shoot through bank staff. Keep a civilian between you and them - but don't get them killed: the fine and the limit apply.",
      lockdown = "The hall's blast shutters lock (BLUE to pass). ",
      screens = "The hall's screen wall: press use there to watch the camera feeds. Guards you see go on the SCHEMATICS. Carriers you see go on THE KEYS.",
      board = "Doesn't pause anything. The bank keeps moving while you read. Find a quiet corner.",
    },
    console_hint = "The camera console needs BLUE (the security chief)",
  },
}
