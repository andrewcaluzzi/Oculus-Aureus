-- DAM+'s card on the main menu hub (shipped as Data/Scripts/modes/damplus-menu.lua). Only ever HOSTED by the hub (tools/mode_hub.lua sets
-- DAMPLUS_MENU_HOST = { back = function } and includes this script, as it does the Sniper Duel's lobby): the hub's Control stage is the
-- backdrop and its ticks drive it. One page: the difficulty cards, BEGIN, MAIN MENU. BEGIN is a stage load of the STOCK Dam mission
-- (ui.launch mode "0": no mode script, so Dam keeps its guards, objectives and briefing) with GE_DAMPLUS set for that launch
-- (port/src/port_damplus.c reads it at the stage load). No cheats. When the mission is over the stock report and the Files screen follow
-- (no script runs on Dam to bring it back here) and the launch's GE_DAMPLUS is cleared (port_uimenu.c). Words: HUB_DEFS.DAMPLUS.

local HOST = DAMPLUS_MENU_HOST
DAMPLUS_MENU_HOST = nil
include("_ui_menu")
include("_hub_defs")
local U = UI_MENU
local DP = HUB_DEFS.DAMPLUS
local T = DP.T
if not HOST or not ui then
  print("ge: damplus: menu: not hosted by the hub: nothing to do")
  return
end

local SFX = { move = 18, change = 43, select = 199, page = 77, back = 78, start = 50 }
local M
local picked = DP.default_diff

local function page_main()
  local d = DP.DIFFS[picked]
  local inf = { { T.d_where, T.where }, { T.d_diff, d.name }, { T.d_what, T.what }, { T.d_cheats, T.cheats } }
  local it = {}
  for i, df in ipairs(DP.DIFFS) do
    it[#it + 1] = { kind = "card", label = df.name, value = T.where, sub = df.blurb, right = "", chosen = (picked == i), act = "pick", diff = i,
                    desc = df.blurb .. " " .. T.after }
  end
  it[#it + 1] = { kind = "button", label = T.to_mi6, act = "quit", desc = T.to_mi6_desc }
  it[#it + 1] = { kind = "primary", col = 2, label = T.begin, sub = T.begin_sub, act = "start", desc = T.begin_desc .. " " .. T.after }
  return { layout = "lobby", items = it, title = T.title, kicker = T.kicker, subtitle = "", info = inf, info_title = T.dossier, info_sub = T.title,
           cursor_act = "start" }
end

local function sfx(id) if arena and arena.sfx then pcall(arena.sfx, id) end end

local function launch()
  if M.busy then return end
  local d = DP.DIFFS[picked]
  local label = T.title .. "  -  " .. d.name
  M.busy = true
  sfx(SFX.start)
  M.log("BEGIN %s (stock %s, GE_DAMPLUS=%s, difficulty %d)", label, DP.stage, DP.parts, d.diff)
  if not ui.launch({ mode = "0", settings = "", preset = "", label = label, stage = DP.stage, difficulty = d.diff, env = { GE_DAMPLUS = DP.parts } }) then
    M.busy = false
    M.log("launch refused")
  end
end

M = U.new{
  name = "damplus", rows = {}, groups = {},
  pages = { main = page_main },
  back = {},
  footer = T.footer, sfx = sfx, SFX = SFX,
  act = function(M_, a, it)
    if a == "pick" then picked = it.diff; sfx(SFX.select); M.dirty = true
    elseif a == "start" then launch()
    elseif a == "quit" then
      sfx(SFX.back)
      M.log("MAIN MENU")
      HOST.back()
    end
  end,
}
DAMPLUS_MENU = M

function M.host_open(c)
  M.busy, M.cursor, M.last, M.dirty = false, 0, {}, true
  M.quiet = 0.4   -- (the press that opened it is not ours)
  M.page = "main"
  M.log("lobby: main (hosted by the hub)")
  M.show()
  M.cursor = M.find_act("start") or 1
  M.show()
end
function M.host_tick(dt) M.tick(dt); mode.state(M.state_text()) end
