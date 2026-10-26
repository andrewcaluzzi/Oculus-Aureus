-- tools/jobs/casino.lua -> Data/Scripts/modes/job_casino.lua: the heist's SECOND JOB, Le Grand Casino Janus (designs/job2-casino.md).
-- The same table as tools/jobs/bank.lua (read that file for the shape); the layout is tools/levelgen/casino.json (written by
-- tools/levelgen/build_casino.py), which names this job (LEVEL.script.heist.job = "casino") and carries its guard areas, cameras,
-- guard tours, vents and grapple points. Same mechanics as the bank (the card ladder, the cracker and the time lock, the cover, the
-- power cut, the console, waves); different building, people, names and words.

HEIST_JOBS = HEIST_JOBS or {}

HEIST_JOBS.casino = {
  name = "casino",
  check_object = "house_vault_door",

  map = {
    areas = {}, cams = {},                                     -- (the layout carries them: LEVEL.script.heist)
    guard_areas = { "floor", "cage", "security", "catwalk", "corridor", "dock", "salons", "vault_ante", "executive", "offices" },
    area_label = { floor = "gaming floor", cage = "the cage", corridor = "back of house", offices = "pit office", security = "security wing",
                   catwalk = "the catwalks", dock = "loading dock", executive = "executive suite", salons = "Salons Prives",
                   vault_ante = "vault level", vault = "house vault", plantroom = "plant" },
    doors = { front = "casino_doors", blast = { "grille_w", "grille_e" }, vault_a = "house_vault_door", vault_b = "count_vault_door",
              safe = "house_safe", cell_green = "chip_bank_door", cell_floor = "drop_cage_door", extra = { dock_shutter = "dock_shutter" } },
    vault_room = "house_vault",
    vault_pads = { "vault_in_a", "vault_in_b", "vault_manager", "vault_g1", "vault_g2", "vault_g3" },
    intel_room = "salle_europe",                               -- the pit podium's floor monitors are on its south-west wall
    power_room = "power",
    civ_places = {
      { "floor", { "croupier_1", "croupier_2", "croupier_3", "croupier_4", "croupier_5", "player_1", "player_2", "player_3", "player_4",
                   "player_5", "player_6", "player_7", "player_8", "barman", "bar_1" }, 8 },
      { "cage", { "cashier_1", "cashier_2" }, 2 },
      { "salons", { "whale_1", "whale_2", "whale_3", "prive_1" }, 3 },
      { "security", { "surv_1", "surv_2" }, 2 },
      { "vault_ante", { "count_1", "count_2" }, 2 },
      { "executive", { "secretary_1" }, 1 },
    },
    carriers = {
      croupier = { "croupier_1", "croupier_2", "croupier_3" },
      supervisor = { "pit_mid", "floor_post_sw", "floor_post_se" },
      shift = { "pit_office_1", "pit_office_mid", "pit_office_2" },
      director = { "director_desk", "director_1" },
      vault_manager = { "vault_manager", "vault_g3" },
    },
    P = {
      start = { "start_1", "start_2", "start_3" },             -- the place du casino
      exit_a = "limo", exit_b = "dock_exit",
      crack = "crack_point",                                   -- in front of the house vault's door
      inside = { "vault_in_a", "vault_in_b" },
      console = "console",                                     -- the surveillance room (F1, BLUE)
      monitors = { "pit_screen_1", "pit_screen_2", "pit_screen_3", "pit_screen_4", "pit_screen_5", "pit_screen_6" },
      waves_front = { "floor_entry_w", "floor_entry_e", "atrium_mid" },
      waves_dock = { "dock_exit", "dock_mid", "dock_2" },
      squad = { "stair_top", "barracks_1", "barracks_2" },
      barracks = { "barracks_1", "barracks_2", "barracks_3", "guard_1", "guard_2" },
      heavy = { "ante_1", "ante_2", "hard_1", "hard_2" },
      safe_loot = "house_safe_loot", cell_loot = { "chip_bank_cash", "drop_cash" }, vault_loot = "house_reserve",
      extra_loot = { cage = "cage_cash" },
      outside = "ante_1",
      screens_at = "pit_screens", screens_face = "pit_screen_2",
      feed_cam = "cam_floor_s",
    },
    MAP = { name = "casino", label = "Le Grand Casino Janus", manager_area = "executive",
            vault_hint = "RED stairs off the north corridor, down", no_cams = "(lockers, chip bank: no cams)",
            exit_tags = { "LIMO", "DOCK" } },                -- the AR specs' exit waypoints (tools/heist_ar.lua; the bank: VAN, DOCK)
  },

  cards = {
    { key = "GREEN", either = {
        { role = "head croupier", kind = "civ", pads = "croupier", body = "Civilian_1_Female", area = "floor" },
        { role = "floor supervisor", kind = "guard", pads = "supervisor", body = "St_Petersburg_Guard", area = "floor" } } },
    { key = "BLUE", role = "shift manager", kind = "guard", pads = "shift", body_setting = "chief_body", health = 1.5, area = "offices" },
    { key = "RED", role = "casino director", kind = "civ", pads = "director", body_setting = "manager_body", area = "@manager" },
    { key = "GOLD", role = "vault manager", kind = "guard", pads = "vault_manager", body = "Russian_Commandant", health = 2, armour = 1, area = "vault" },
  },
  console_roles = { "head croupier", "floor supervisor", "shift manager", "casino director", "vault manager" },

  text = {
    hud_title = "CASINO",
    not_here = "HEIST MODE: the casino job needs casino.json on the slot (build_job2)",
    not_here_log = "not the casino",
    intro_cover = "LE GRAND CASINO. You're staff: gun holstered. Rob the house.",
    intro_plain = "LE GRAND CASINO JANUS, MONTE CARLO. Rob the house. Hurt no one.",
    need_green = "GREEN card: head croupier or floor supervisor",
    need_blue = "BLUE card: the shift manager (pit office)",
    need_red = "RED card: the casino director (F1)",
    crack = "Crack the house vault (",
    grab = "Empty the house reserve!",
    get_out = "Get out: the limousine or the loading dock",
    alarm_flash = "ALARM! Security is coming - the grilles are down",
    vault_open = "The house vault is open! Take the reserve",
    safe_open = "The high-roller safe: gold!",
    safe_log = "the house safe is open: %d bars",
    negotiator = "Negotiator: Monsieur, we can talk about this. Let them go.",   -- (the hostage stand-off, section 31)
  },

  guide = {
    in_what = "Next: walk in through the casino doors, gun holstered",
    in_how_cover = "Holstered you pass as staff. A drawn gun makes guards look",
    in_how = "Get inside the casino",
    steps = {
      { key = "GREEN", id = "green", what = "Next: GREEN card - head croupier or floor supervisor",
        how = "Croupier: aim a gun at her. Supervisor: slap him twice, USE" },
      { key = "BLUE", id = "blue", what = "Next: BLUE card - the shift manager", role = "shift manager", none = " (pit office)",
        how = "Pit office, off the staff lounge (GREEN): slap twice, USE" },
      { key = "RED", id = "red", what = "Next: RED card - the casino director", role = "casino director", none = " (F1)",
        how = "BLUE core stair up to F1, exec suite: aim, he hands it over" },
    },
    vault_what = "Next: the house vault - RED stairs off the north corridor",
    vault_how = "Pick the DOOR DECODER (next weapon), fire it at the vault door",
  },

  intel = {
    title = "LE GRAND CASINO JANUS, MONTE CARLO  -  CREW EYES ONLY",
    operation = "Operation: break the bank at the casino. The house vault, below the Salons Prives.",
    keys = {
      GREEN = { who = "the head croupier OR the floor supervisor", where = "the Salle Europe: one of the two, the plan doesn't say which",
                roles = { "head croupier", "floor supervisor" },
                how = "Croupier: aim at her till she gives it up. Supervisor: knock him out and search him (or rob him at gunpoint).",
                short = "head croupier or floor supervisor" },
      BLUE = { who = "the shift manager", where = "the pit office, back of house (GREEN)", roles = { "shift manager" },
               how = "Tough (x1.5 health). Catch him alone in the pit office: KO and search.", short = "the shift manager carries it" },
      RED = { who = "the casino director", where = "his office, executive suite, F1 (behind BLUE)", roles = { "casino director" },
              how = "A civilian: aim at him and he hands it over. Don't kill him.", short = "the casino director carries it" },
      GOLD = { who = "the vault manager", where = "inside the house vault (armoured)", roles = { "vault manager" },
               how = "In the vault with its guards: only once it's open. Armoured, double health.", short = "the vault manager carries it" },
      FLOORKEY = { name = "COUNT KEY", who = "nobody: it hangs in the lockers", where = "the staff lockers, back of house", roles = {},
                   how = "Walk into the lockers (staff area) and take it off the board.", short = "it hangs in the staff lockers", word = "the count key" },
    },
    plan = function(S, f)
      local secs = f.secs
      local g = S.cover and "Walk in from the place: you're staff, gun holstered. The gaming rooms are public; back of house costs cover slowly, the cage and security in a second."
                       or "Walk in through the casino doors from the place."
      return {
        { "GET IN", g, "Same doors, gun out. Once the alarm goes the bronze grilles into the gaming rooms drop: BLUE to pass." },
        { "GET THE KEYS", "GREEN (head croupier or floor supervisor), then BLUE (shift manager, pit office), then RED (the director, F1). Rob staff at gunpoint; knock guards out and search them.",
          "Drop the carrier and take the card off the body. Every card opens doors only for the Bond who holds it." },
        { "THE EYE / POWER", string.format("The surveillance room on F1 (BLUE, console): every camera off %s. Or the plant on %s: cabinet %s + main breaker %s.",
            secs(S.cam_off_time), f.floor, secs(S.power_cabinet), secs(S.power_cut)),
          string.format("Power out = no alarm, no cameras. Backup generator in %s unless you kill it (%s).", secs(S.generator_after), secs(S.generator_time)) },
        { "CRACK VAULT", string.format("The house vault, down the RED stairs. Equip the door decoder and fire it at the vault door (or press use). %s of work, only while a Bond is near it.",
            secs(S.crack_time)),
          string.format("It jams (%d%% every %s): fire it again. Two squads come for it even on a quiet run.", math.floor(S.jam_chance * 100 + 0.5), secs(S.jam_every)) },
        { "TAKE THE RESERVE", string.format("Open %s, klaxon at T-%d. %d bars x %s. GOLD (vault manager) opens the high-roller safe in the Salon Prive.", secs(S.vault_time), S.klaxon_at,
            S.vault_bars, f.money(S.bar_value)),
          string.format("At 0 the time lock seals it: anyone inside is TRAPPED (alarm, heavy squad, re-crack %s from inside).", secs(S.recrack_time)) },
        { "GET OUT", "Every living Bond in an exit with the loot: the limousine on the place or the loading dock. No alarm all run = +" .. math.floor(S.quiet_bonus * 100 + 0.5) .. "%.",
          string.format("Waves every %s while hot, the elite response after %s. The dock is never locked down.", secs(S.wave_interval), secs(S.response_after)) },
      }
    end,
    labels = { salle_europe = "SALLE EUROPE", salle_renaissance = "RENAISSANCE", atrium = "ATRIUM", bar = "BAR", cage = "CAGE",
               chip_bank = "CHIPS", cage_mantrap = "MANTRAP", staff_corr = "STAFF", staff_lounge = "LOUNGE", lockers = "LOCKERS",
               pit_office = "PIT OFFICE", kitchen = "KITCHEN", security_corr = "SECURITY", guard_room = "GUARDS", armory = "ARMORY",
               barracks = "BARRACKS", dock = "DOCK", place = "PLACE", alley = "ALLEY", garden = "GARDEN", red_stairs = "RED STAIRS",
               surveillance = "EYE IN THE SKY", catwalk_n = "CATWALK", director = "DIRECTOR", salle_blanche = "SALLE BLANCHE",
               salon_prive = "SALON PRIVE", terrace = "TERRACE", house_vault = "VAULT", vault_ante = "ANTE", soft_count = "SOFT COUNT",
               hard_count = "HARD COUNT", power = "POWER", generator = "GENERATOR", goods_lift = "LIFT" },
    safes = { "house_safe" },
    floor_key = "count_key",
    crack_room = "vault_ante",
    exit_a = "EXIT A: LIMO", exit_b = "EXIT B: DOCK",
    posts = { { "croupier", "GREEN", "GREEN? HEAD CROUPIER" }, { "supervisor", "GREEN", "GREEN? FLOOR SUPERVISOR" }, { "shift", "BLUE", "BLUE: SHIFT MANAGER" },
              { "director", "RED", "RED: DIRECTOR" }, { "vault_manager", "GOLD", "GOLD: VAULT MANAGER" } },
    next_posts = { GREEN = { "croupier", "supervisor" }, BLUE = { "shift" }, RED = { "director" } },
    nokey = { "THE VAULT DOORS: time-locked. Only the cracker (the door decoder) opens them.",
              "PADLOCK: the alley's dock shutter. Shoot it. The goods lift's gate: rusted shut, the piton goes down the shaft.",
              "LOCKDOWN: when the alarm goes the atrium's bronze grilles drop and want BLUE." },
    personnel = function(S, f)
      local secs = f.secs
      return {
        { "CROUPIERS", "At the tables in the Salle Europe, unarmed.", "Aim at one: she surrenders, keep aiming and she hands it over (the head croupier may hold GREEN).", "Civilians: killing them costs " .. f.money(S.civ_fine) .. " each and too many fails the job." },
        { "CASINO DIRECTOR", "His office in the executive suite, F1, behind BLUE.", "Carries RED. Rob him at gunpoint.", "A civilian: hurt him and it counts." },
        { "FLOOR SUPERVISOR", "Walks the pit (if the head croupier hasn't got GREEN, he has).", "Armed. KO and search, or rob him at gunpoint.", "Judges your disguise like any guard." },
        { "SHIFT MANAGER", "The pit office, back of house.", "Carries BLUE. 1.5x health.", "BLUE opens security, the core stair and the eye in the sky." },
        { "GUARDS", string.format("About %d posted on this difficulty: the floor, the cage, back of house, security, the catwalks, the salons, the vault level.", math.floor(S.guards_post * (f.force or 1) + 0.5)),
          "They stand post. They hear shots, see Bonds, find bodies.", "They treat you as staff until something gives you away, then 'Hey!' and a radio call." },
        { "ALARM RAISERS", "A few per area.", "On sight or sound they run for the area's alarm box.", "Drop them before they get there, or cut the power first." },
        { "THE EYE IN THE SKY", "The surveillance room (F1) and its catwalks behind the one-way glass.", "A camera that times you relays to them: alarm " .. secs(S.security_delay) .. " later.", "Knock out every security guard and the cameras raise nothing." },
        { "VAULT GUARDS", "Inside the house vault, with the vault manager (GOLD).", "They wake when the vault opens and come straight at you.", "Armoured manager, double health." },
        { "SQUADS & WAVES", string.format("Two crack squads hit the cracker. Hot: waves every %s.", secs(S.wave_interval)), string.format("After %s hot: the armoured response, both entrances.", secs(S.response_after)),
          "Trapped in the vault: the heavy squad (armour, RC-P90s)." },
      }
    end,
    notes = {
      hold_fire = "Guards won't shoot through the punters or staff. Keep a civilian between you and them - but don't get them killed: the fine and the limit apply.",
      lockdown = "The bronze grilles drop (BLUE to pass). ",
      screens = "The pit podium's floor monitors (Salle Europe, south-west wall): press use there to watch the camera feeds. Guards you see go on the SCHEMATICS.",
      board = "Doesn't pause anything. The tables keep turning while you read. Find a quiet corner.",
    },
    console_hint = "The surveillance console needs BLUE (the shift manager)",
  },

  settings = {
    -- the cover classes come from the rooms' zone tags (the bank's area_class names its own areas)
    area_class = {},
  },
}
