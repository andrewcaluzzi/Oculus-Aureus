-- Arena theme: every player-facing name in the arena, Bond-flavoured (AC, 2026-10-27: "make everything Bond themed"). Shipped as
-- Data/Scripts/modes/_arena_theme.lua; arena.lua includes it first and every module reads ARENA_THEME. One table so the menu /
-- post-game (arena_menu) and the whacky waves (arena_whacky) take their strings from the same place.
-- Source: midas-notes/features/arena-bond-theme.md (the idea bank; AC has not picked yet: change any line freely).
-- Rules: readable at a glance; each ladder's order obvious; film titles, GoldenEye names and lines only (nothing invented).
-- Internal keys (left) are what the code, logs and tests use; only the values are shown.

ARENA_THEME = {
  -- ---- kill chains: kills inside the crowd favour's multi_window (2 s); index = kills in the chain ------------------------------
  chain = {
    [2] = "DOUBLE-O",                  -- the 00 prefix
    [3] = "LICENCE TO KILL",           -- the licence
    [4] = "THUNDERBALL",
    [5] = "LIVE AND LET DIE",
    [6] = "THE WORLD IS NOT ENOUGH",   -- the top of the ladder
  },
  chain_more = "THE WORLD IS NOT ENOUGH",   -- 7 and up: "THE WORLD IS NOT ENOUGH x7"

  -- ---- streaks: kills without being hurt; index = streak length (the [30] name repeats every 5 kills after 30) ---------------------
  streak = {
    [5]  = "SHAKEN",
    [10] = "NOT STIRRED",
    [15] = "ON HER MAJESTY'S SERVICE",
    [20] = "NOBODY DOES IT BETTER",
    [30] = "GOLDENEYE",                -- the top: this is GoldenEye
  },
  streak_sub = "%d KILLS UNTOUCHED",   -- the line under a streak callout

  -- ---- single-kill event callouts (the arena detects these today); a chain or streak callout on the same kill wins -----------------
  events = {
    first_kill    = "THE NAME'S BOND",              -- the run's first kill
    headshot      = "FOR YOUR EYES ONLY",           -- eyes
    long_shot     = "THE LIVING DAYLIGHTS",         -- the Bratislava sniper scene (30 m+)
    knife         = "FROM RUSSIA WITH LOVE",        -- Rosa Klebb's shoe knife
    melee         = "KARATE CHOP",                  -- GoldenEye's own slap
    blast         = "THUNDERBALL",                  -- one blast, 2+ kills
    rocket        = "MOONRAKER",                    -- a kill by Bond's own rocket
    golden_gun    = "THE MAN WITH THE GOLDEN GUN",
    own_gun       = "A VIEW TO A KILL",             -- with the victim's own gun (weakest fit, per the bank)
    low_health    = "DIE ANOTHER DAY",              -- a kill on low health (the CLOSE CALL bonus)
    last_enemy    = "NO TIME TO DIE",               -- the last enemy of a wave
    revive        = "YOU ONLY LIVE TWICE",          -- co-op: a partner revived
    streak_broken = "LICENCE REVOKED",              -- a streak of 5+ ended by a hit
    clean_wave    = "DR. NO (DAMAGE)",              -- a wave cleared with nobody hurt (a pun; may be too cute)
  },
  event_subs = {
    streak_broken = "STREAK %d ENDED",
    revive        = "%s IS BACK IN THE GAME",       -- %s = "P1" / "P2"
    last_enemy    = "WAVE %d CLEARED",
  },

  -- ---- the crowd favour: mood low to high (the panel shows it; HUD row 4 "CROWD 64 <mood>") ----------------------------------------
  tiers = { "BORED", "INTRIGUED", "SHAKEN", "STIRRED", "ON THEIR FEET" },

  -- favour toasts (short bookkeeping on the panel; the callouts carry the film titles). key = the internal label
  labels = {
    ["BOOM"]           = "THUNDERBALL",            -- "THUNDERBALL x3"
    ["CLEAN WAVE"]     = "NOT A SCRATCH",
    ["REDEMPTION"]     = "REINSTATED",
    ["SHOT THE CROWD"] = "M IS NOT AMUSED",
    ["WITH A PISTOL"]  = "WITH A WALTHER",         -- the PP7 is the game's Walther PPK
  },

  -- the crowd's announcements (string.format patterns)
  say = {
    gift_ammo    = "COMPLIMENTS OF FELIX LEITER: AMMO%s",   -- %s = " - <why>"
    gift_armour  = "M APPROVES: ARMOUR%s",
    sponsor      = "Q-BRANCH DELIVERY: %s IN THE CENTRE",
    double       = "Q-BRANCH DELIVERY: DOUBLE DAMAGE FOR %d SECONDS",
    hated_elite  = "PERSONA NON GRATA - THE NEXT WAVE IS ELITE",
    hated_now    = "PERSONA NON GRATA - THE GATES OPEN",
    hated_early  = "PERSONA NON GRATA - THE GATES OPEN EARLY",
    redemption   = "REINSTATED",
    thumb_up     = "M APPROVES: FULL HEALTH AND A GIFT",
    thumb_down   = "M DISAPPROVES: NO BREATHER",
    thumb_side   = "M IS UNMOVED",
    emperor_gift = "M'S GIFT",
    health       = "MONEYPENNY'S REGARDS",                  -- (no health pickup in the arena today)
  },

  -- ---- medals: GoldenEye's own difficulty names, bronze to platinum (a hidden tier above could be LICENCE TO KILL) ----------------------
  medals = { "AGENT", "SECRET AGENT", "00 AGENT", "007" },

  -- ---- waves (arena.lua's wave types) and the boss waves -----------------------------------------------------------------------------
  waves = {
    standard    = "JANUS TROOPS",
    knife       = "THE ONATOPP SISTERS",
    swarm       = "SPETSNAZ HORDE",
    ambush      = "SPECTRE AMBUSH",
    sniper      = "SEVERNAYA SNIPERS",
    wild        = "ROGUE AGENTS",
    tank        = "IRON JAWS",
    turbo_knife = "THE TURBO ONATOPP SISTERS",
  },
  bosses = { "BARON SAMEDI", "ALEC TREVELYAN", "JAWS", "THE TRIUMVIRATE" },              -- the four boss waves, in order
  boss_cards = { "LIVE AND LET DIE", "FOR ENGLAND, JAMES?", "THE SPY WHO LOVED ME", "ALL THREE AT ONCE" },   -- the title card under each

  -- ---- the wave panel's words (port_arenahud.c) -------------------------------------------------------------------------------------
  panel = { get_ready = "BRIEFING", fight = "IN THE FIELD", breather = "DEBRIEF", idle_title = "COLOSSEUM", idle_sub = "SPECTRE IS WATCHING" },

  -- ---- Whacky (arena_whacky.lua): the waves' titles and blurbs (internal key = the mutator name), Q-branch flavour; GoldenEye's own
  -- cheat names where one fits (TINY BOND, LINE MODE). The tiny knife throwers are NICK NACKS (the tiny henchman, The Man with the Golden Gun)
  whacky = {
    waves = {
      ghost     = { "SPECTRE", "NOW YOU SEE THEM..." },                                   -- spectre = a ghost
      chameleon = { "ADAPTIVE CAMOUFLAGE", "Q-BRANCH TECH IN THE WRONG HANDS" },          -- the Vanish, Die Another Day
      shadow    = { "NIGHTFIRE", "DARK SHAPES ON THE SAND" },
      glass     = { "ICE PALACE", "ONE SHOT SHATTERS - THEY HIT HARD" },                   -- the ice hotel, Die Another Day
      giants    = { "TINY BOND", "A Q-BRANCH SHRINK RAY MISFIRES" },                       -- the GoldenEye cheat
      fastfwd   = { "TURBO MODE", "EVERYBODY AT DOUBLE SPEED" },                           -- the GoldenEye cheat
      slowmo    = { "SLOW ANIMATION", "...BUT THERE ARE MORE OF THEM" },                   -- the GoldenEye cheat
      rockets   = { "LITTLE NELLIE", "SOMEBODY BROUGHT ROCKETS" },                         -- Q's gyrocopter, You Only Live Twice
      double    = { "DOUBLE AGENT", "Q-BRANCH ISSUE: ONE IN EACH HAND" },
      glitch    = { "GOLDENEYE EMP", "THE SATELLITE IS FIRING" },                          -- the GoldenEye pulse (LINE MODE bursts)
      swap      = { "Q'S WRISTWATCH", "FAST... SLOW... FAST..." },
      crowd     = { "Q-BRANCH AIRDROP", "GIFTS RAIN FROM THE STANDS" },
      minions   = { "NICK NACK LAUNCHER", "EVERY ROCKET HATCHES NICK NACKS" },             -- a Q-branch prototype
      mayhem    = { "NICK NACK'S REVENGE", "THEIR TINY KNIFE THROWERS WANT YOU" },
    },
    launcher       = "NICK NACK LAUNCHER",                       -- the minion launcher, as a pickup / callout
    launcher_sub   = "%d ROCKETS THAT HATCH NICK NACKS",         -- callout sub-lines: 36 characters at most (they are cut)
    launcher_label = "NICKNAK",                                  -- the 7-letter pickup marker
    minion_kill    = "NICK NACK STRIKES",
    minion_kill_sub = "A TINY KNIFE THROWER GOT ONE",
    minion_squad   = "NICK NACK SQUAD x%d",
    minion_squad_sub = "THE TINY ASSASSINS ARE ON A ROLL",
    mayhem_burst   = "INCOMING NICK NACKS!",
    airdrop        = "Q-BRANCH AIRDROP: %s!",                    -- %s = the gift's name below
    gifts = { shotgun = "A SHOTGUN", autoshot = "AN AUTOMATIC SHOTGUN", rcp90 = "AN RC-P90", grenadelauncher = "A GRENADE LAUNCHER",
              armour = "BODY ARMOUR", laser = "A MOONRAKER LASER", rocket = "THE NICK NACK LAUNCHER" },
    golden         = "BARON SAMEDI",
    golden_sub     = "IN SCARAMANGA'S GOLD",
    emp            = "EMP!",
    swap_fast      = "FAST!",
    swap_slow      = "SLOW!",
    swap_sub       = "Q'S WRISTWATCH",
    effect_off     = "Q-BRANCH: STAND DOWN",
  },

  -- ---- the weather mutators (mode_arena_mutators.lua): the RAIN wave's title and its sub-line (Thunderball: the storm), the crowd's
  -- toast when it starts
  weather = {
    rain  = { "THUNDERBALL", "RAIN ON THE SAND - EVERYONE SLIPS" },
    crowd = "THEY HATE THE WEATHER",
  },
}

-- ---- the start menu (the briefing, Q Branch) and the post-game (the debrief): tools/mode_arena_menu.lua, tools/arena_menu_defs.lua ----
-- (the settings rows' own labels and help lines stay with their values in arena_menu_defs.lua)
ARENA_THEME.menu = {
  kicker = "MI6  -  SECTION 00  -  BRIEFING", title = "THE COLOSSEUM", subtitle = "Choose your assignment, 007. The crowd is watching.",
  dossier = "MISSION DOSSIER", begin = "BEGIN MISSION", begin_sub = "FIRE / ENTER / CLICK", begin_desc = "Start the run with these settings. The crowd is waiting.",
  qbranch = "Q BRANCH", qbranch_title = "Q BRANCH",
  qbranch_desc = "Every setting: the assignment, SPECTRE's interference, the field team, the opposition, supplies, the crowd.",
  to_mi6 = "EXIT", to_mi6_desc = "Close the game.", done = "BACK TO THE BRIEFING",
  reset = "RESET TO THE OPERATION", reset_desc = "Every setting back to the chosen operation's (the defaults when it is custom).",
  custom = "UNSANCTIONED OPERATION", coop_note = "  Your partner is the arena bot; no best scores in co-op.",
  footer = "W S / STICK: MOVE    A D / LEFT RIGHT: CHANGE    FIRE / A / ENTER: SELECT    ESC / B: BACK    MOUSE: POINT, CLICK",
  -- the difficulty, easiest first (SECOND = the arena as built). Not the medal names above, so the two never read alike
  difficulty = { "TRAINEE", "FIELD AGENT", "DOUBLE-O", "SUICIDE MISSION" },
  -- the operations (presets): { name, one line }
  ops = {
    classic       = { "OPERATION COLOSSEUM", "Twenty waves, a boss every fifth. The standard assignment." },
    endless       = { "TOMORROW NEVER DIES", "No extraction. Hold the pit for as long as you can." },
    chaos         = { "SPECTRE PROTOCOL", "Twenty waves. SPECTRE rigs the games: mutators and affix elites." },
    chaos_endless = { "CASINO ROYALE", "Endless, with SPECTRE's mutators and elites. The house always wins." },
    sprint        = { "QUANTUM OF SOLACE", "Ten waves, a boss every third. In and out." },
    coop          = { "DOUBLE-O SECTION", "Twenty waves with a double-O partner at your side." },
    coop_chaos    = { "DOUBLE-O VS SPECTRE", "The partner, the mutators, the elites." },
  },
  -- Q Branch's groups
  groups = { RUN = "THE ASSIGNMENT", CHAOS = "SPECTRE INTERFERENCE", PLAYERS = "FIELD TEAM", ENEMIES = "THE OPPOSITION",
             ECONOMY = "Q BRANCH SUPPLIES", CROWD = "THE CROWD", MARKERS = "FIELD INTEL" },
  -- the dossier's lines
  d_pacing = "ASSIGNMENT", d_boss = "BOSS", d_noboss = "NO BOSSES", d_diff = "DIFFICULTY", d_chaos = "SPECTRE", d_whacky = "WHACKY",
  d_team = "FIELD TEAM", d_cap = "OPPOSITION AT ONCE", d_kit = "Q BRANCH KIT", d_heal = "HEAL", d_crowd = "CROWD", d_fav_on = "FAVOUR ON",
  d_fav_off = "FAVOUR OFF", d_best = "BEST ON FILE", d_solo = "SOLO ONLY", d_none = "NO RUNS YET", d_longest = "LONGEST",
  d_fastest = "FASTEST CLEAR", d_medal = "MEDAL", d_runs = "RUNS", d_cleared = "CLEARED", off = "OFF",
  -- the debrief
  debrief = "MI6 DEBRIEF", win = "MISSION COMPLETE", fell = "AGENT DOWN", retry = "AGAIN, 007?", retry_desc = "The same settings, straight back in: ",
  new_assignment = "NEW ASSIGNMENT", new_assignment_desc = "Back to the briefing: change the settings.",
  cleared = "ALL %d WAVES CLEARED IN %s", fell_in = "FELL IN WAVE %d  -  %s", fell_early = "FELL BEFORE THE FIRST WAVE",
  new_best = "NEW BEST: ", first_run = "FIRST RUN ON FILE", coop_scores = "CO-OP: NO BEST SCORES", no_medal = "NO MEDAL", medal = "%s MEDAL",
  best_chip = "NEW BEST", new_medal = "NEW!", chart = "KILLS PER WAVE", legend = "GOLD BOSS   PURPLE MUTATOR   RED THE LAST STAND",
  s_wave = "WAVE REACHED", s_kills = "CONFIRMED KILLS", s_time = "MISSION TIME", s_acc = "ACCURACY", s_heads = "HEADSHOTS",
  s_multi = "BEST MULTI-KILL", s_streak = "BEST STREAK", s_peak = "CROWD PEAK", s_hits = "HITS TAKEN", s_elites = "ELITES ELIMINATED",
  s_mut = "SPECTRE MUTATORS",
}

-- a favour toast's text for an internal label ("STREAK 7" / "BOOM x3" keep their number)
function ARENA_THEME.label(key)
  local L = ARENA_THEME.labels
  if L[key] then return L[key] end
  local base, rest = key:match("^(%u[%u ]-)( [x%d].*)$")
  if base and L[base] then return L[base] .. rest end
  return key
end

-- ---- the callouts' sounds: stock game sound effects by SFX_ID number (bondconstants.h), layered per callout level ------------------
-- { id, volume, pitch, delay seconds }, played with sound.play_sfx (presentation: it never takes a game sound's place). The ids:
--   1 ROCKET_LAUNCH (a whoosh)   4 GRENADE_THROW (a light whoosh)   7 TRAIN_SLIDE_DOOR_SLIDE (the lift's ding, as a bell)
--   11 GUN_TANK2BIG_1 (a heavy thump)   12 GUN_TANK2BIGBIG_1 (the grenade launcher's boom)   117 GUN_B13_M60AMMGUN (the Golden Gun)
--   173 EXPLOSION_4B   180 EXPLOSION_9   261 BIG_CLANK (a gong-like clank)
-- The delays land the impact as the entrance ends (0.42 s + 0.05 s a level). Each callout's level: chains = kills - 1 (max 6),
-- streaks = 1 + streak / 5 (max 6), single-kill events 1 (the first kill 2), the last enemy / a clean wave 2, a revive 3, boss cards 6.
ARENA_THEME.callout_sfx = {
  [1] = { { 4, 0.8, 1.30, 0 } },                                                          -- a quick whoosh
  [2] = { { 1, 0.7, 1.30, 0 }, { 11, 0.6, 1.20, 0.30 } },                                 -- whoosh + thump
  [3] = { { 1, 0.8, 1.15, 0 }, { 261, 0.7, 1.00, 0.35 } },                                -- whoosh + clank
  [4] = { { 1, 0.9, 1.00, 0 }, { 173, 0.7, 1.10, 0.38 }, { 7, 0.5, 0.80, 0.40 } },        -- whoosh + blast + bell
  [5] = { { 12, 0.9, 0.90, 0 }, { 180, 0.8, 1.00, 0.40 }, { 261, 0.6, 0.80, 0.42 }, { 7, 0.6, 0.60, 0.45 } },
  [6] = { { 12, 1.0, 0.80, 0 }, { 180, 1.0, 0.85, 0.45 }, { 117, 0.9, 1.00, 0.47 }, { 173, 0.8, 0.70, 0.55 }, { 7, 0.7, 0.50, 0.60 } },
}

-- every callout goes through here: the centre show (arena.callout, port_arenahud.c) and its layered stock sounds
function ARENA_THEME.callout(text, level, sub, secs)
  if not arena or not arena.callout or not text then return end
  level = math.max(1, math.min(6, level or 1))
  pcall(arena.callout, text, level, sub, secs)
  local layers = ARENA_THEME.callout_sfx[level]
  if layers and sound and sound.play_sfx then
    for _, s in ipairs(layers) do pcall(sound.play_sfx, s[1], s[2], s[3], s[4]) end
  end
end

-- the preview list for the test key (Home / Shift+Home with test_keys on): every callout with its level
function ARENA_THEME.preview_list()
  local T, l = ARENA_THEME, {}
  for k = 2, 6 do l[#l + 1] = { T.chain[k], math.min(6, k - 1), k .. " KILLS" } end
  l[#l + 1] = { T.chain_more .. " x7", 6, "7 KILLS" }
  for _, k in ipairs({ 5, 10, 15, 20, 30 }) do
    if T.streak[k] then l[#l + 1] = { T.streak[k], math.min(6, 1 + k // 5), string.format(T.streak_sub, k) } end
  end
  for _, k in ipairs({ "first_kill", "headshot", "long_shot", "knife", "melee", "blast", "rocket", "golden_gun", "own_gun", "low_health",
                       "last_enemy", "clean_wave", "revive", "streak_broken" }) do
    if T.events[k] then l[#l + 1] = { T.events[k], (k == "revive") and 3 or ((k == "first_kill" or k == "last_enemy" or k == "clean_wave") and 2 or 1), k } end
  end
  for i, b in ipairs(T.bosses) do l[#l + 1] = { b, 6, T.boss_cards[i] } end
  return l
end
