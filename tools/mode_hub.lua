-- The MAIN MENU HUB (GE_MODE=hub on the stock Control stage; shipped as Data/Scripts/modes/hub.lua). Play Midas.bat boots into it. It is
-- the top level over every mode: ARENA and HEIST hand off to their lobbies (arena-menu, heist-menu) exactly as their own launchers
-- start them, GOLDENEYE 007 goes to the stock front end (the Files screen: the campaign, stock multiplayer), SETTINGS is the port's own
-- options, EXIT closes the game. Built on the generic menu layer (tools/ui_menu.lua, port/src/port_uimenu*.c): the Arena's look.
--
-- The stage is the backdrop: the stock Control stage, the big world-map screen wall in front of the camera, which pans slowly. Nobody
-- fights (the mode removes the guards): the controls are held (plan.hold: the crew's synchronized buttons drive the menu, so the menu
-- is sim state like the lobbies') and Bond stands there, unarmed. The stage, the camera and the music are ONE table entry
-- (HUB_DEFS.STAGE in tools/hub_defs.lua). The music is the Control elevator track the lobbies play.
--
-- A lobby the hub opened (ui.hub() true) has MAIN MENU where it has EXIT, and it comes back here: a stage load (HUB_DEFS.to_hub). A lobby
-- started by its own launcher is as it always was. The hub is off under netplay (the menu flow is solo and local co-op only).
--
-- SETTINGS: the port's own options (ui.opt / ui.opt_set: port_options.c, the table the Options pages use). Each applies the moment it
-- changes and is written to midas.ini by the port itself, so they hold across the stage loads and the next launch.
-- Test hooks: HUB_MENU (the menu), tests/hub/run.sh.

include("_ui_menu")
include("_hub_defs")
pcall(include, "_arena_menu_defs")   -- (the Arena's best, for its tile; a build without the arena just has no best line)
local U, D = UI_MENU, HUB_DEFS
local T, ST = D.T, D.STAGE

if not ui or ui.netplay() then
  print("ge: hub: menu: off (netplay, or no ui layer)")
  return
end

local SFX = { move = 18, change = 43, select = 199, page = 77, back = 78, start = 50 }
local MEDAL_RGBA = { [0] = 0x9A9080FF, 0xD08A48FF, 0xD4D8E0FF, 0xF0C040FF, 0xA8ECF8FF }
local cur = ui.current()
local M

-- ---- the tiles' status lines: the Arena's best (arena_scores.ini) and the Heist's (ui prefs heist_bests), solo only ----
local function arena_best()
  local AD = ARENA_MENU_DEFS
  if not (AD and scores and scores.enabled and scores.enabled()) then return nil end
  local bm, bw, runs = 0, 0, 0
  for _, p in ipairs(AD.PRESETS) do
    if not p.id:match("^coop") then
      for d = 0, 3 do
        local k = p.key .. AD.DIFF_SUFFIX[d]
        local m, w, r = scores.get(k .. "_best_medal") or 0, scores.get(k .. "_best_wave") or 0, scores.get(k .. "_runs") or 0
        runs = runs + r
        if m > bm or (m == bm and w > bw) then bm, bw = m, w end
      end
    end
  end
  if runs == 0 and bw == 0 then return nil end
  return { medal = math.floor(bm), wave = math.floor(bw), name = bm > 0 and AD.MEDALS[math.floor(bm)] or nil }
end

local function heist_best()
  local b = ui.prefs("heist_bests")
  local top
  for k, v in pairs(b) do
    if k:match("^[a-z0-9]+_%d_payout$") then
      local n = tonumber(v)
      if n and (not top or n > top) then top = n end
    end
  end
  return top
end

local function status(id)   -- text, medal tier (0..4)
  if id == "arena" then
    local a = arena_best()
    if not a then return T.best_none, 0 end
    return string.format(T.best_arena, a.name or string.format(T.best_arena_wave, a.wave)), a.medal
  elseif id == "heist" then
    local h = heist_best()
    if not h then return T.best_none, 0 end
    return string.format(T.best_heist, U.money(h)), 0
  end
  return "", 0
end

-- ---- the settings rows: the port's options that exist (ui.opt answers nil for one the build lacks: no row) ----
local rows, opt_of = {}, {}
for _, o in ipairs(D.OPTIONS) do
  local info = ui.opt(o[1])
  if info and info.n > 0 then
    local choices = {}
    for i, text in ipairs(info.text) do choices[i] = { text:upper(), i } end
    rows[#rows + 1] = { id = o[1], group = o[3], label = o[2], desc = o[4], default = info.index, choices = choices }
    opt_of[o[1]] = info
  end
end

-- ---- the pages ----
local sel = D.TILES[1].id   -- the tile the dossier describes (the last one the cursor stood on)

local function page_main()
  local it = {}
  local at = M and M.items[M.cursor]
  if at and at.act == "tile" then sel = at.tile end
  for _, tile in ipairs(D.TILES) do
    local th = T.tiles[tile.id]
    local right, medal = status(tile.id)
    it[#it + 1] = { kind = "card", label = th.name, value = th.place, sub = th.blurb, right = right, medal = medal, act = "tile", tile = tile.id,
                    chosen = (sel == tile.id), desc = th.blurb .. " " .. th.more }
  end
  it[#it + 1] = { kind = "button", label = T.settings, act = "page:settings", desc = T.settings_desc }
  it[#it + 1] = { kind = "button", label = T.exit, act = "quit", desc = T.exit_desc }
  local th = T.tiles[sel]
  local right, medal = status(sel)
  local inf = { { T.d_mode, th.name }, { T.d_where, th.place }, { "", "" } }
  if sel == "arena" or sel == "heist" then inf[#inf + 1] = { T.d_best, right, MEDAL_RGBA[medal] } end
  return { layout = "lobby", items = it, title = T.title, kicker = T.kicker, subtitle = T.subtitle, info = inf, info_title = T.dossier,
           info_sub = th.name, cursor_act = "tile" }
end

local function page_settings()
  local it = M.items_for_groups({ VIEW = { { kind = "primary", label = T.done, act = "page:main", desc = T.done_desc } } })
  return { layout = "settings", items = it, title = T.settings_title, kicker = T.title .. "  -  " .. T.settings, subtitle = "" }
end

-- ---- actions ----
local function sfx(id) if arena and arena.sfx then pcall(arena.sfx, id) end end

local function open(id)
  if M.busy then return end
  local tile
  for _, t in ipairs(D.TILES) do if t.id == id then tile = t end end
  M.busy = true
  sfx(SFX.start)
  if not tile.launch then   -- GOLDENEYE 007: the stock front end's Files screen
    M.log("%s: to the Files screen", id)
    if not ui.quit() then M.busy = false; M.log("quit refused") end
    return
  end
  local L = tile.launch
  M.log("%s: %s on the %s stage", id, L.mode, L.stage)
  ui.hub(true)   -- (the lobby it opens has MAIN MENU where it has EXIT)
  if not ui.launch({ mode = L.mode, stage = L.stage, lobby = "menu", preset = "", label = T.tiles[id].name, env = L.env }) then
    ui.hub(false)
    M.busy = false
    M.log("launch refused")
  end
end

M = U.new{
  name = "hub", rows = rows, groups = D.GROUPS, group_col = D.GROUP_COL, group_label = T.groups,
  pages = { main = page_main, settings = page_settings },
  back = { settings = "main" },
  footer = T.footer, sfx = sfx, SFX = SFX,
  changed = function(M_, id)   -- a settings row stepped: the option takes it now (and the port writes midas.ini)
    local ok = ui.opt_set(id, M.v[id])
    M.log("setting %s = %s (%s)", id, M.label(id), ok and "applied" or "REFUSED")
  end,
  act = function(M_, a, it)
    if a == "tile" then open(it.tile)
    elseif a == "quit" then
      M.busy = true
      sfx(SFX.back)
      M.log("EXIT the game")
      if not ui.exit() then M.busy = false end
    end
  end,
}
HUB_MENU = M
M.log("lobby: %s, %d settings, %s", M.page, #rows, cur and ("after " .. (cur.label ~= "" and cur.label or "a launch")) or "first boot")

-- ---- the stage: the music, Bond held, the camera on the wall ----
local MUSIC_SET = false
if music and music.set then
  local ok, err = pcall(music.set, ST.music, ST.volume)
  MUSIC_SET = ok
  M.log("music: set %s -> %s", ST.music, tostring(ok and err or err))
end

on("stage_start", function()
  if music and music.play and MUSIC_SET then   -- (should music.set have been missed, start the track directly)
    local okc, curm = pcall(music.current)
    if not (okc and curm == ST.music) then pcall(music.play, ST.music, ST.volume) end
  end
  ui.hub(false)   -- (the hub is the top: a lobby sets it again when it opens one)
  plan.hold(true)
  ui.escape_back(true)
  if crosshair and crosshair.hide then pcall(crosshair.hide, true) end
  if ar and ar.reset then pcall(ar.reset) end
  if hud and hud.set then for l = 1, 3 do pcall(hud.set, l, "") end end
  if arena and arena.hud then pcall(arena.hud, false) end
  for p = 0, bond.count() - 1 do
    bond.warp(p, ST.pad, p * 80, 0)
    pcall(bond.equip, p, "fist")
  end
  M.log("stage start: %s, camera on pad %d", ST.name, ST.pad)
  M.show()
  M.cursor = M.find_act("tile") or 1
  M.show()
end)

local clock = 0
on("ui_tick", function(dt)
  M.tick(dt)
  mode.state(M.state_text())
  clock = clock + dt   -- the camera pans between yaw - pan and yaw + pan
  local yaw = ST.yaw + ST.pan * math.sin(clock * 2 * math.pi / ST.period)
  for p = 0, bond.count() - 1 do pcall(bench.look, p, yaw, ST.pitch) end
end)
