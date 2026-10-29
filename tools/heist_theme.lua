-- Heist theme: every player-facing word of the heist's start menu and debrief (the generic menu layer's heist pages), Bond-flavoured
-- in the arena's MI6 tone (tools/arena_theme.lua): the job is against Janus. Shipped as Data/Scripts/modes/_heist_theme.lua; the lobby
-- (tools/mode_heist_menu.lua), its rows (tools/heist_menu_defs.lua) and the run side (tools/heist_flow.lua) read HEIST_THEME.
-- Internal keys (left) are what the code, the logs and the tests use; only the values are shown. Change any line freely.

HEIST_THEME = {
  -- ---- the briefing (the start menu) --------------------------------------------------------------------------------------------
  menu = {
    kicker = "MI6  -  SECTION 00  -  OPERATION BRIEFING", title = "THE JANUS JOB",
    subtitle = "Janus washes its money through a bank and a casino. Empty them, 007.",
    dossier = "JOB DOSSIER", begin = "BEGIN THE JOB", begin_sub = "FIRE / ENTER / CLICK",
    begin_desc = "To the planning board in the van, then in. The clock starts when the plan is confirmed.",
    begin_desc_noplan = "Straight in with the standard kit (the planning board is off in Q BRANCH).",
    qbranch = "Q BRANCH", qbranch_title = "Q BRANCH",
    qbranch_desc = "Every setting: the job, the crew, the opposition, security, the civilians, the kit, field tests.",
    to_mi6 = "EXIT", to_mi6_desc = "Close the game.", done = "BACK TO THE BRIEFING",
    done_desc = "Back to the briefing (B / Escape).",
    reset = "RESET TO STANDARD", reset_desc = "Every setting back to the job as written (the difficulty stays).",
    footer = "W S / STICK: MOVE    A D / LEFT RIGHT: CHANGE    FIRE / A / ENTER: SELECT    ESC / B: BACK    MOUSE: POINT, CLICK",
    -- the difficulty: GoldenEye's own names, easiest first
    difficulty = { "AGENT", "SECRET AGENT", "00 AGENT", "007" },
    -- the dossier card
    d_job = "THE JOB", d_where = "LOCATION", d_diff = "DIFFICULTY", d_force = "OPPOSITION", d_deal = "THE DAY'S DEAL",
    d_crew = "FIELD TEAM", d_cover = "COVER", d_plan = "PLANNING", d_rules = "RULES", d_best = "BEST TAKE", d_time = "FASTEST",
    d_rating = "BEST RATING", d_runs = "JOBS", d_won = "CLEAN GETAWAYS", d_none = "NO JOBS ON FILE", d_test = "FIELD TEST: NO RECORDS",
    d_standard = "AS WRITTEN", d_custom = "Q BRANCH CHANGES", force = "~%d GUARDS",
    -- the job cards
    card_best = "BEST %s", card_none = "NO JOBS ON FILE", card_missing = "NOT IN THIS BUILD", card_here = "BACKDROP",
    card_switch = "  Select it to move the briefing there.",
    switching = "Moving the briefing to %s...",
    -- the groups of Q BRANCH
    groups = { JOB = "THE JOB", CREW = "THE FIELD TEAM", GUARDS = "THE OPPOSITION", SECURITY = "SECURITY", CIVILIANS = "CIVILIANS",
               KIT = "Q BRANCH KIT", TEST = "FIELD TESTS" },
  },

  -- ---- the jobs (the cards): the name, the place, one line ---------------------------------------------------------------------------
  jobs = {
    bank = { name = "JANUS BANK", place = "ST PETERSBURG BRANCH",
             blurb = "Janus's front bank. Climb the keycard ladder, crack the vault.",
             more = "Out by the van or the loading dock." },
    casino = { name = "LE GRAND CASINO JANUS", place = "MONTE CARLO",
               blurb = "The house vault under the Salons Prives.",
               more = "Croupier, shift manager, director; out by the limousine or the dock." },
  },

  -- ---- the debrief -----------------------------------------------------------------------------------------------------------------
  debrief = {
    kicker = "MI6 DEBRIEF", win = "JOB COMPLETE", fail = "BUSTED", abort = "JOB ABORTED",
    abort_line = "The crew pulled out. Nobody gets paid, nobody gets caught.",
    rating = "RATING: %s", no_rating = "NO RATING",
    again = "AGAIN, 007?", again_desc = "The same job, the same settings, straight back in: ",
    new_job = "NEW JOB", new_job_desc = "Back to the briefing: another job, other settings.",
    to_mi6 = "EXIT", to_mi6_desc = "Close the game.",
    new_best = "NEW BEST: ", first = "FIRST JOB ON FILE", test = "FIELD TEST: NO RECORDS", best_chip = "NEW BEST", new_medal = "NEW!",
    nb_take = "TAKE", nb_time = "TIME", nb_rating = "RATING",
    checks = "OBJECTIVES", chips = "COMMENDATIONS  %d / %d", chart = "THE TAKE", legend = "GOLD THE LOOT    RED WHAT IT COST",
    -- the stat cells
    s_take = "THE TAKE", s_time = "JOB TIME", s_alarm = "ALARM", s_spotted = "TIMES SPOTTED", s_guards = "GUARDS DOWN",
    s_civs = "CIVILIANS HURT", s_hostages = "HOSTAGES", s_crew = "THE CREW",
    never = "NEVER", none = "NONE", crew_out = "%d / %d OUT", crew_lost = "LOST", killed = "%d (%d KILLED)",
    -- the objectives
    c_inside = "GET INSIDE", c_card = "%s CARD", c_vault = "CRACK THE VAULT", c_loot = "SECURE THE LOOT", c_out = "GET THE CREW OUT",
    loot_line = "%d BARS  %d CASH",
    -- how a card was got (the debrief's words, by its key)
    how = { pickpocket = "LIFTED", ["knock-out"] = "KNOCK-OUT", intimidation = "AT GUNPOINT", ["off a body"] = "OFF A BODY",
            ["picked up"] = "PICKED UP", found = "FOUND", bribe = "BRIBED" },
    -- the take's bars
    b_vault = "LOOT", b_cash = "CASH", b_bonus = "QUIET", b_fines = "FINES", b_plan = "PLANNING", b_bribes = "BRIBES",
  },

  -- the ratings, best last; tier = the medal colour (0 none .. 4 platinum); line = the debrief's line under the banner
  ratings = {
    GHOST = { name = "GHOST", tier = 4, line = "Monday morning the manager opens an empty vault and blames the time lock. Nobody ever knew you were there." },
    PROFESSIONAL = { name = "PROFESSIONAL", tier = 3, line = "In and out. A few raised eyebrows, a guard with a headache, nothing the papers will print." },
    COWBOY = { name = "COWBOY", tier = 2, line = "Sirens, gunsmoke and a full van. It worked. M will want a word." },
    CHAOS = { name = "CHAOS", tier = 1, line = "Half the city heard that one. The gold had better be worth the funerals." },
    BUSTED = { name = "BUSTED", tier = 0, line = "The crew never made the van. Q would like his gadgets back." },
  },
  rating_order = { "BUSTED", "CHAOS", "COWBOY", "PROFESSIONAL", "GHOST" },
}
