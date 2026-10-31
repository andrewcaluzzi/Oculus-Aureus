-- hub_defs.lua: the main menu hub's words and tables (shipped as Data/Scripts/modes/_hub_defs.lua). Read by the hub (tools/mode_hub.lua)
-- and, through HUB_DEFS.exit_text / HUB_DEFS.lobby, by the Arena and Heist run flows (a hub session's run comes back to the hub's stage).
-- Defines the global HUB_DEFS:
--   T          every player-facing word (the arena's MI6 tone: tools/arena_theme.lua, tools/heist_theme.lua)
--   STAGE      the backdrop: the stock stage the hub runs on and where its camera sits. ONE table entry: swap in Surface by changing it
--   OPTIONS    the settings page: the port's own options (ui.opt), by group
--   TILES      the top level: id, the lobby it hosts (the Arena's, the Heist's, the Sniper Duel's), or none (GoldenEye: the Files screen)
--   exit_text(label, desc) -> label, desc    a lobby's EXIT row: MAIN MENU when the hub opened this session
--   lobby(host, page, preset, label) -> t    the launch that brings a run back to the hub's stage on a hosted lobby's page
--   lobby_of(text) -> host, page             that launch's lobby text read back ("arena/postgame")
--   from_hub() -> bool                       the hub opened this session
-- Nothing here reads a file or needs the other mode scripts; a package without the hub simply lacks it (the lobbies pcall the include).

HUB_DEFS = {}
local D = HUB_DEFS

D.T = {
  kicker = "MI6  -  SECTION 00  -  OPERATIONS", title = "OCULUS AUREUS", subtitle = "Choose your assignment, 007.",
  dossier = "ASSIGNMENT", settings = "SETTINGS", settings_title = "SETTINGS", exit = "EXIT",
  settings_desc = "Sound, mouse, view and HUD. They apply at once and are kept for the next time.",
  exit_desc = "Close the game.", done = "BACK TO THE MAIN MENU", done_desc = "Back to the main menu (B / Escape).",
  footer = "W S / STICK: MOVE    A D / LEFT RIGHT: CHANGE    FIRE / A / ENTER: SELECT    ESC / B: BACK    MOUSE: POINT, CLICK",
  back_label = "MAIN MENU", back_desc = "Back to the main menu.",
  d_mode = "OPERATION", d_where = "WHERE", d_best = "BEST ON FILE", d_none = "NO RUNS YET", d_solo = "SOLO ONLY",
  groups = { AUDIO = "SOUND", CONTROLS = "CONTROLS", VIEW = "VIEW AND HUD" },
  tiles = {
    arena = { name = "ARENA", place = "THE COLOSSEUM + 7 MAPS", blurb = "Wave after wave, SPECTRE rigging the games. Alone or with a partner.",
              more = "The Colosseum, or the Temple, Complex, Caves, Library, Stack, Basement and Facility. Presets, mutators, elites; Q Branch has every setting." },
    heist = { name = "HEIST", place = "JANUS BANK AND LE GRAND CASINO", blurb = "Plan the job in the van, then rob Janus: keys, cameras, the vault.",
              more = "Stealth or loud. Two jobs, every setting in Q Branch." },
    sniper = { name = "SNIPER DUEL", place = "RIDGE, SURFACE, SNOWFIELD, JUNGLE  -  PROTOTYPE", blurb = "One rifle and a named hunter on the far slopes. Four places to fight.",
               more = "Five rounds. The ridge and Surface: a glint shows where he has set up, shoot first or get down. The snowfield and the jungle: hunters out to 500 m who glint only once they have you; go prone, use thermal, tag them through the binoculars." },
    damplus = { name = "DAM+", place = "BYELOMORYE DAM, EXPANDED", blurb = "The Dam mission with more guards, new rooms and a way to the island.",
                more = "A motor pool, a comms annex, a gallery inside the wall, the spillway balcony and the island lookout. Same objectives. No cheats." },
    goldeneye = { name = "GOLDENEYE 007", place = "THE ORIGINAL", blurb = "The campaign and stock multiplayer, from the Files screen.",
                  more = "Missions, multiplayer, the cheats, and the port's own options." },
  },
  best_arena = "BEST %s", best_arena_wave = "WAVE %d", best_heist = "BEST %s", best_sniper = "BEST %d / %d ROUNDS", best_none = "NO RUNS YET",
}

-- The backdrop. `id` is a name front.c's stage table knows (the names of GE_STAGE); the camera stands on `pad` and pans `pan` degrees
-- either way of `yaw` (a positive yaw turns left), one sweep every `period` seconds. Pitch in degrees (down is negative).
-- Control: the big world-map screen wall of the control room (room 45). The music is the Control elevator track the lobbies use.
-- To use Surface instead: id = "surface", a pad and a yaw from a camera tour (pads.pos lists them), music of your choice.
D.STAGE = {
  id = "control", name = "CONTROL",
  pad = 101, yaw = -17, pan = 12, period = 70, pitch = -2,   -- pad 101 (-387, 70, -22): the wall dead ahead, the consoles in front of it
  music = "elevator_control", volume = 0.8,
}

-- the settings page: { option key (ui.opt), label, group, help }
D.OPTIONS = {
  { "music_volume", "MUSIC VOLUME", "AUDIO", "How loud the music plays." },
  { "sfx_volume", "SOUND VOLUME", "AUDIO", "How loud sound effects play." },
  { "mouse_sens_x", "MOUSE LOOK X", "CONTROLS", "Mouse look speed, left and right." },
  { "mouse_sens_y", "MOUSE LOOK Y", "CONTROLS", "Mouse look speed, up and down." },
  { "look_updown", "LOOK UP / DOWN", "CONTROLS", "Upright: push up to look up. Reverse: inverted." },
  { "hfov_wide", "FIELD OF VIEW", "VIEW", "Horizontal view angle on a widescreen picture. 90 is the original." },
  { "always_crosshair", "ALWAYS CROSSHAIR", "VIEW", "A small target at the gun's aim point when you are not aiming." },
  { "mission_timer", "MISSION TIMER", "VIEW", "The elapsed time at the bottom left in solo missions." },
  { "show_fps", "FPS COUNTER", "VIEW", "Frames per second in the top left corner during play." },
}
D.GROUPS = { "AUDIO", "CONTROLS", "VIEW" }
D.GROUP_COL = { AUDIO = 1, CONTROLS = 1, VIEW = 2 }

-- the top level: id, the lobby it hosts (mode_hub.lua runs the Arena's start menu and the Heist's on this stage: no stage load between
-- the menus; only a run loads its own stage)
D.TILES = {
  { id = "arena", host = "arena" },
  { id = "heist", host = "heist" },
  { id = "sniper", host = "sniper" },
  { id = "damplus", host = "damplus" },   -- (its card picks the difficulty; BEGIN starts the STOCK Dam mission with GE_DAMPLUS: tools/mode_damplus_menu.lua)
  { id = "goldeneye" },   -- (ui.quit: the stock front end)
}

-- DAM+: the stock Dam mission (no mode script, so its own guards and objectives) with GE_DAMPLUS for that one launch. The Dam+ parts
-- (port/src/port_damplus.c: 1 = all of them, or a comma list of night, motor, comms, gallery, spill, island) and the difficulties
D.DAMPLUS = {
  stage = "dam", parts = "1",
  T = { kicker = "MI6  -  SECTION 00  -  DAM+", title = "DAM+", dossier = "DOSSIER", d_where = "WHERE", d_diff = "DIFFICULTY", d_what = "WHAT IS NEW",
        where = "BYELOMORYE DAM", what = "5 AREAS + NIGHT SHIFT", d_cheats = "CHEATS", cheats = "NONE",
        begin = "BEGIN THE MISSION", begin_sub = "STOCK DAM, EXPANDED", begin_desc = "Start the Dam mission with every Dam+ area open.",
        to_mi6 = "MAIN MENU", to_mi6_desc = "Back to the main menu.",
        after = "When the mission is over, the stock report and the Files screen follow (restart the game for this menu).",
        footer = "W S / STICK: MOVE    FIRE / A / ENTER: SELECT    ESC / B: BACK    MOUSE: POINT, CLICK" },
  DIFFS = {
    { name = "AGENT", blurb = "The fewest objectives, the softest guards.", diff = 0 },
    { name = "SECRET AGENT", blurb = "More objectives, tougher guards.", diff = 1 },
    { name = "00 AGENT", blurb = "All four objectives. The way the Dam+ launchers play it.", diff = 2 },
    { name = "007", blurb = "All four objectives, the hardest guards.", diff = 3 },
  },
  default_diff = 3,   -- (the DIFFS index the card starts on: 00 AGENT)
}

function D.from_hub() return ui ~= nil and ui.hub ~= nil and ui.hub() end

function D.exit_text(label, desc)
  if D.from_hub() then return D.T.back_label, D.T.back_desc end
  return label, desc
end

-- the launch that brings a run back to the hub, on a hosted lobby's page ("menu" / "postgame"): the run side (arena_flow, heist_flow) arms it
function D.lobby(host, page, preset, label)
  return { mode = "hub", stage = D.STAGE.id, lobby = host .. "/" .. page, preset = preset or "", label = label or "" }
end

-- a hub launch's lobby text -> the host and its page ("arena/postgame" -> "arena", "postgame"); nil for a plain launch
function D.lobby_of(text)
  if type(text) ~= "string" then return nil end
  local host, page = text:match("^(%a+)/(%a+)$")
  return host, page
end
