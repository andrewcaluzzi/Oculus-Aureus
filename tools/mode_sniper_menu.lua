-- The SNIPER DUEL's lobby: its card page and its debrief (shipped as Data/Scripts/modes/sniper-menu.lua). It is only ever HOSTED by the main menu
-- hub (tools/mode_hub.lua sets SNIPER_MENU_HOST = { back = function } and includes this script, as it does the Arena's and the Heist's lobbies): the
-- hub's Control stage is the backdrop and its ticks drive the menu. The hub calls SNIPER_MENU.host_open(cur) when the tile opens it or a run came
-- back to the debrief, and SNIPER_MENU.host_tick(dt) every ui tick while it is up. MAIN MENU is host.back(); BEGIN and AGAIN are stage loads
-- (the Surface stage, mode sniper). Built on the generic menu layer (tools/ui_menu.lua): the Arena's look.
-- The best is kept in Data/Saves/ui/sniper_bests.ini (ui.prefs, this machine's, solo only): <stage>_rounds / _time / _runs / _wins.

local HOST = SNIPER_MENU_HOST
SNIPER_MENU_HOST = nil
include("_ui_menu")
include("_sniper_defs")
pcall(include, "_hub_defs")
local U, D = UI_MENU, SNIPER_DEFS
local T = D.T
if not HOST or not ui then
  print("ge: sniper: menu: not hosted by the hub: nothing to do")
  return
end

local SFX = { move = 18, change = 43, select = 199, page = 77, back = 78, start = 50, best = 160 }
local result = nil
local news = {}
local M
local picked = D.STAGE_ORDER[1]   -- the card chosen (the ridge by default)
local function K(id) return D.key(id or picked) end
local bests = ui.prefs("sniper_bests") or {}

local function best(id)
  local b = { rounds = tonumber(bests[K(id) .. "_rounds"]) or 0, time = tonumber(bests[K(id) .. "_time"]), runs = tonumber(bests[K(id) .. "_runs"]) or 0,
              wins = tonumber(bests[K(id) .. "_wins"]) or 0 }
  if b.runs == 0 then return nil end
  return b
end

-- the run just played: counted once, when the debrief opens
local function record(r)
  if r.aborted then return end
  local rid = r.stage or picked
  local b = best(rid) or { rounds = 0, runs = 0, wins = 0 }
  b.runs = b.runs + 1
  if r.win then b.wins = b.wins + 1 end
  local better = (r.rounds or 0) > b.rounds or ((r.rounds or 0) == b.rounds and r.win and (not b.time or r.time < b.time))
  if better then news.best = true; b.rounds = r.rounds or 0; b.time = r.win and r.time or b.time end
  bests[K(rid) .. "_runs"], bests[K(rid) .. "_wins"], bests[K(rid) .. "_rounds"] = tostring(b.runs), tostring(b.wins), tostring(b.rounds)
  if b.time then bests[K(rid) .. "_time"] = string.format("%.2f", b.time) end
  ui.save_prefs("sniper_bests", bests)
  print(string.format("ge: sniper: menu: recorded: runs %d wins %d best %d rounds%s", b.runs, b.wins, b.rounds, news.best and " (new best)" or ""))
end

local function best_text(id)
  local b = best(id)
  if not b then return T.d_none end
  return string.format(T.best_hub, b.rounds, #D.ROUNDS)
end

local function page_main()
  local b = best(picked)
  local inf = { { T.d_where, D.STAGES[picked].name }, { T.d_rounds, tostring(#D.ROUNDS) }, { T.d_rifle, T.rifle }, { "", "" } }
  if b then
    inf[#inf + 1] = { T.d_best, string.format("%d / %d", b.rounds, #D.ROUNDS) }
    if b.time and b.rounds >= #D.ROUNDS then inf[#inf + 1] = { T.d_time, U.clock(b.time) } end
    inf[#inf + 1] = { T.d_runs, string.format("%d   WON %d", b.runs, b.wins) }
  else
    inf[#inf + 1] = { T.d_best, T.d_none, 0x9A9080FF }
  end
  local xl, xd = T.to_mi6, T.to_mi6_desc
  local it = {}
  for _, id in ipairs(D.STAGE_ORDER) do
    local sd = D.STAGES[id]
    it[#it + 1] = { kind = "card", label = sd.card or sd.name, value = sd.name, sub = T.blurb, right = best_text(id), chosen = (picked == id), act = "pick", stage = id,
                    desc = T.blurb .. " " .. T.more }
  end
  it[#it + 1] = { kind = "button", label = xl, act = "quit", desc = xd }
  it[#it + 1] = { kind = "primary", col = 2, label = T.begin, sub = T.begin_sub, act = "start", desc = T.begin_desc }
  return { layout = "lobby", items = it, title = T.title, kicker = T.kicker, subtitle = "", info = inf, info_title = T.dossier, info_sub = T.title, cursor_act = "start" }
end

local function page_debrief()
  local r = result or {}
  local win, ab = r.win, r.aborted
  local t = { layout = "debrief", kicker = T.kicker_d, chart = T.chart, legend = T.legend, medal = -1 }
  t.banner = ab and T.abort or (win and T.win or T.fail)
  t.banner_rgba = ab and 0xB8B0A0FF or (win and 0xF0C040FF or 0xE04A30FF)
  t.subtitle = ab and T.abort_line or (win and T.win_line or T.fail_line)
  t.news = news.best and (T.new_best .. string.format("%d / %d ROUNDS", r.rounds or 0, r.total or 0)) or ""
  local acc = (r.shots or 0) > 0 and string.format("%d%%", math.floor(100 * (r.hits or 0) / r.shots + 0.5)) or "-"
  t.cells = {
    { T.s_rounds, string.format("%d / %d", r.rounds or 0, r.total or 0), win and 0x7CC870FF or nil, news.best and true or false },
    { T.s_time, U.clock(r.time or 0) },
    { T.s_acc, acc, nil },
    { T.s_hs, tostring(r.headshots or 0) },
    { T.s_shots, string.format("%d  (%d hit)", r.shots or 0, r.hits or 0) },
    { T.s_hits, tostring(r.hits_taken or 0), (r.hits_taken or 0) > 0 and 0xE04A30FF or nil },
    { T.s_foe, (r.foe and r.foe ~= "") and r.foe or "-" },
  }
  if ab then t.cells = { t.cells[2] } end
  local bars = {}
  for i, secs in ipairs(r.round_times or {}) do bars[#bars + 1] = { math.floor(secs), 1, string.format("ROUND %d", i), U.clock(secs) } end
  t.bars = bars
  t.items = {
    { kind = "primary", label = T.again, act = "again", desc = T.again_desc },
    { kind = "button", label = T.to_mi6, act = "quit", desc = T.to_mi6_desc },
  }
  return t
end

local function sfx(id) if arena and arena.sfx then pcall(arena.sfx, id) end end

local function launch()
  if M.busy then return end
  local label = T.title .. "  -  " .. D.STAGES[picked].name
  M.busy = true
  sfx(SFX.start)
  M.log("BEGIN %s", label)
  local SDm = D.STAGES[picked]
  local env = {}
  for k, v in pairs(SDm.env or {}) do env[k] = v end
  if not ui.launch({ mode = "sniper", settings = U.serialize({ stage = picked }), preset = picked, label = label, stage = SDm.stage, env = env }) then
    M.busy = false
    M.log("launch refused")
  end
end

local function again()
  if M.busy then return end
  local r = ui.retry()
  if not r then M.log("no duel to run again"); return end
  r.lobby = ""
  M.busy = true
  sfx(SFX.start)
  M.log("AGAIN %s", r.label or "")
  if not ui.launch(r) then M.busy = false; M.log("again refused") end
end

M = U.new{
  name = "sniper", rows = {}, groups = {},
  pages = { main = page_main, debrief = page_debrief },
  back = { debrief = "main" },
  footer = T.footer, sfx = sfx, SFX = SFX,
  act = function(M_, a, it)
    if a == "pick" then picked = it.stage; sfx(SFX.select); M.dirty = true
    elseif a == "start" then launch()
    elseif a == "again" then again()
    elseif a == "quit" then
      sfx(SFX.back)
      M.log("MAIN MENU")
      HOST.back()
    end
  end,
}
SNIPER_MENU = M

function M.host_open(c)
  M.busy, M.cursor, M.last, M.dirty = false, 0, {}, true
  if c and D.STAGES[c.preset] then picked = c.preset end
  M.quiet = 0.4   -- (the press that opened it is not ours)
  for k in pairs(news) do news[k] = nil end
  result, M.page = nil, "main"
  if c and c.lobby == "postgame" then
    result = U.parse(ui.result())
    if result then
      record(result)
      M.page = "debrief"
    end
  end
  M.log("lobby: %s (hosted by the hub)", M.page)
  M.show()
  M.cursor = (M.page == "main") and (M.find_act("start") or 1) or 1
  M.show()
  if M.page == "debrief" and news.best then sfx(SFX.best) end
end
function M.host_tick(dt) M.tick(dt); mode.state(M.state_text()) end
