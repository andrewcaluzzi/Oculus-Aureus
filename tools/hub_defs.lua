-- hub_defs.lua: the main menu hub's words and tables (shipped as Data/Scripts/modes/_hub_defs.lua). Read by the hub (tools/mode_hub.lua)
-- and, through HUB_DEFS.exit_text / HUB_DEFS.to_hub, by the Arena and Heist lobbies (a lobby the hub opened has MAIN MENU where it has EXIT).
-- Defines the global HUB_DEFS:
--   T          every player-facing word (the arena's MI6 tone: tools/arena_theme.lua, tools/heist_theme.lua)
--   STAGE      the backdrop: the stock stage the hub runs on and where its camera sits. ONE table entry: swap in Surface by changing it
--   OPTIONS    the settings page: the port's own options (ui.opt), by group
--   TILES      the top level: id, the mode it opens, its stage, the launch it hands off with
--   exit_text(label, desc) -> label, desc    a lobby's EXIT row: MAIN MENU when the hub opened this session
--   to_hub() -> bool                         end the lobby's stage and load the hub
--   from_hub() -> bool                       the hub opened this session
-- Nothing here reads a file or needs the other mode scripts; a package without the hub simply lacks it (the lobbies pcall the include).

HUB_DEFS = {}
local D = HUB_DEFS

D.T = {
  kicker = "MI6  -  SECTION 00  -  OPERATIONS", title = "PROJECT MIDAS", subtitle = "Choose your assignment, 007.",
  dossier = "ASSIGNMENT", settings = "SETTINGS", settings_title = "SETTINGS", exit = "EXIT",
  settings_desc = "Sound, mouse, view and HUD. They apply at once and are kept for the next time.",
  exit_desc = "Close the game.", done = "BACK TO THE MAIN MENU", done_desc = "Back to the main menu (B / Escape).",
  footer = "W S / STICK: MOVE    A D / LEFT RIGHT: CHANGE    FIRE / A / ENTER: SELECT    ESC / B: BACK    MOUSE: POINT, CLICK",
  back_label = "MAIN MENU", back_desc = "Back to the main menu: the stage loads again.",
  d_mode = "OPERATION", d_where = "WHERE", d_best = "BEST ON FILE", d_none = "NO RUNS YET", d_solo = "SOLO ONLY",
  groups = { AUDIO = "SOUND", CONTROLS = "CONTROLS", VIEW = "VIEW AND HUD" },
  tiles = {
    arena = { name = "ARENA", place = "THE COLOSSEUM", blurb = "Wave after wave in the pit, SPECTRE rigging the games. Alone or with a partner.",
              more = "Presets, mutators, elites, the crowd. Q Branch has every setting." },
    heist = { name = "HEIST", place = "JANUS BANK AND LE GRAND CASINO", blurb = "Plan the job in the van, then rob Janus: keys, cameras, the vault.",
              more = "Stealth or loud. Two jobs, every setting in Q Branch." },
    goldeneye = { name = "GOLDENEYE 007", place = "THE ORIGINAL", blurb = "The campaign and stock multiplayer, from the Files screen.",
                  more = "Missions, multiplayer, the cheats, and the port's own options." },
  },
  best_arena = "BEST %s", best_arena_wave = "WAVE %d", best_heist = "BEST %s", best_none = "NO RUNS YET",
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

-- the top level: id, the launch the tile hands off with (mode, stage, env), as the mode's own .bat starts it
D.TILES = {
  { id = "arena", launch = { mode = "arena-menu", stage = "colosseum" } },
  { id = "heist", launch = { mode = "heist-menu", stage = "levelgen", env = { GE_LEVELGEN = "bank" } } },
  { id = "goldeneye" },   -- (ui.quit: the stock front end)
}

function D.from_hub() return ui ~= nil and ui.hub ~= nil and ui.hub() end

function D.exit_text(label, desc)
  if D.from_hub() then return D.T.back_label, D.T.back_desc end
  return label, desc
end

function D.to_hub()
  return ui.launch({ mode = "hub", stage = D.STAGE.id, lobby = "menu", label = D.T.back_label })
end
