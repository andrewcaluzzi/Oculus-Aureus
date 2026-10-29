-- The heist's LOBBY: the start menu and the debrief (GE_MODE=heist-menu on the levelgen slot; shipped as
-- Data/Scripts/modes/heist-menu.lua). Play Heist.bat / Play Casino.bat boot into it; every heist run (from here or from a launcher)
-- comes back to it when it ends (tools/heist_flow.lua arms that), on the debrief. Built on the generic menu layer (tools/ui_menu.lua,
-- port/src/port_uimenu*.c): the Arena's look.
--
-- The job's level is up behind the menu, quiet: the camera turns slowly from the middle of the banking hall (the casino: the Salle
-- Europe), a few staff and guards stand about. Nobody fights: the controls are held (plan.hold: the crew's synchronized buttons drive
-- the menu, so the menu is sim state like the planning board) and nothing of the heist runs. BEGIN THE JOB and AGAIN
-- end this stage; the hand-off (port/src/port_uimenu.c) loads the slot again with the run's mode, settings, difficulty and level at
-- once, without the stock menus or the mission report. Choosing the other job's card moves the lobby to that job's level.
--
-- Pages: MAIN (the job cards with their bests, the difficulty, Q BRANCH, BEGIN THE JOB), SETTINGS (Q BRANCH: every setting by group),
-- DEBRIEF (the result, the stats, the objectives, the take, the commendations; AGAIN, NEW JOB, EXIT: closes the game). The choices are kept in
-- Data/Saves/ui/heist_menu.ini, the bests per job and difficulty in Data/Saves/ui/heist_bests.ini (ui.prefs: this machine's, solo only).
-- Test hooks: HEIST_MENU (the menu), GE_HEISTMENU_TEST=1 runs mode heist_flowtest instead of heist (tests/heist_menu).
--
-- HOSTED by the main menu hub (tools/mode_hub.lua sets HEIST_MENU_HOST = { cur =, back = function } and includes this script): the hub's
-- stage (Control) is the backdrop, so there is no job level, staff, camera turn, music or stage handler here, and a job's card only picks
-- the job (no stage load). The hub calls HEIST_MENU.host_open(cur) when the Heist tile opens it (or a run ended and came back to the
-- debrief) and HEIST_MENU.host_tick(dt) every ui tick while it is up. EXIT is MAIN MENU (host.back); BEGIN THE JOB / AGAIN are stage
-- loads, on the levelgen slot (stage = "levelgen").

local HOST = HEIST_MENU_HOST
HEIST_MENU_HOST = nil
include("_ui_menu")
include("_heist_menu_defs")
pcall(include, "_hub_defs")   -- the main menu hub's: when it opened this lobby, EXIT is MAIN MENU (a build without the hub has none)
local U, D = UI_MENU, HEIST_MENU_DEFS
local T, TD = D.T, D.TD
local function exit_text(label, desc) if HUB_DEFS then return HUB_DEFS.exit_text(label, desc) end return label, desc end
if not HOST then
  if levelgen and levelgen.current() ~= levelgen.default() then include("levelgen_layout_" .. levelgen.current()) else include("levelgen_layout") end
end

local cur = HOST and HOST.cur or (ui and ui.current() or nil)
local result = nil
local RUN_MODE = mode.env_flag("GE_HEISTMENU_TEST") and "heist_flowtest" or "heist"
local SFX = { move = 18, change = 43, select = 199, page = 77, back = 78, start = 50, best = 160 }
local BACKDROP = { bank = "hall", casino = "salle_europe" }   -- the room the camera turns in, by job
local MEDAL_RGBA = { [0] = 0x9A9080FF, 0xD08A48FF, 0xD4D8E0FF, 0xF0C040FF, 0xA8ECF8FF }
local FORCE = { [0] = 30, 45, 70, 90 }

local here = HOST and D.JOBS[1] or D.job_here()   -- (the job whose level is up; hosted, none is: the first job's the default pick)
local M
local function job() return D.JOB_BY_ID[M.job] or here end

-- ---- the bests (Data/Saves/ui/heist_bests.ini: <job>_<d>[_custom]_payout / _time / _rating / _runs / _wins) ----
local bests = ui and ui.prefs("heist_bests") or {}
local function best(key)
  local b = { payout = tonumber(bests[key .. "_payout"]), time = tonumber(bests[key .. "_time"]), rating = bests[key .. "_rating"],
              runs = tonumber(bests[key .. "_runs"]) or 0, wins = tonumber(bests[key .. "_wins"]) or 0 }
  if b.runs == 0 then return nil end
  return b
end
local function rank(r) for i, x in ipairs(HEIST_THEME.rating_order) do if x == r then return i end end return 0 end
local function tier(r) local x = r and HEIST_THEME.ratings[r]; return x and x.tier or 0 end

-- the run just played: counted once, when the debrief opens (the lobby on "postgame" loads once per run)
local news = {}
local function record(r)
  if r.cheat or r.aborted then return end
  local key = D.record_key(r.job, r.difficulty, r.custom)
  local b = best(key) or { runs = 0, wins = 0 }
  b.runs = b.runs + 1
  if r.win then
    b.wins = b.wins + 1
    if not b.payout or r.payout > b.payout then news.payout = true; b.payout = r.payout end
    if not b.time or r.time < b.time then news.time = true; b.time = r.time end
    if not b.rating or rank(r.rating) > rank(b.rating) then news.rating = b.rating ~= nil; b.rating = r.rating end
    news.first = b.wins == 1
  end
  bests[key .. "_runs"], bests[key .. "_wins"] = tostring(b.runs), tostring(b.wins)
  if b.payout then bests[key .. "_payout"] = string.format("%d", b.payout) end
  if b.time then bests[key .. "_time"] = string.format("%.2f", b.time) end
  if b.rating then bests[key .. "_rating"] = b.rating end
  if ui then ui.save_prefs("heist_bests", bests) end
  print(string.format("ge: heist: menu: recorded %s: runs %d wins %d best %s%s", key, b.runs, b.wins, tostring(b.payout),
    (news.payout or news.time) and " (new best)" or ""))
end

-- ---- the pages ----
local function card_items()
  local it = {}
  local d = M.val("difficulty")
  for _, j in ipairs(D.JOBS) do
    local ok = D.available(j)
    local b = best(D.record_key(j.id, d, false))
    local right = ""
    if b and b.payout then right = string.format(T.card_best, U.money(b.payout)) elseif ok then right = T.card_none end
    it[#it + 1] = { kind = "card", label = j.name, sub = j.blurb, value = j.place, right = right, medal = b and tier(b.rating) or 0,
                    tag = (not ok) and T.card_missing or ((not HOST and j.lg == here.lg) and T.card_here or ""), chosen = (M.job == j.id), dim = not ok,
                    act = "job", job = j.id, desc = j.blurb .. " " .. j.more .. ((ok and not HOST and j.lg ~= here.lg) and T.card_switch or "") }
  end
  return it
end

local function label_of_run()
  return string.format("%s  -  %s", job().name, M.label("difficulty"))
end

local function info_main()
  local _, d, _, cheat, custom = D.settings(M)
  local inf = {
    { T.d_where, job().place },
    { T.d_diff, M.label("difficulty") .. "   " .. string.format(T.force, FORCE[d] or 45) },
    { T.d_deal, M.val("deal") and (M.val("seed") > 0 and M.label("seed") or M.label("deal")) or M.label("deal") },
    { T.d_crew, M.label("partner") },
    { T.d_cover, M.label("cover") .. (M.val("cover") and ("   " .. M.label("hand")) or "") },
    { T.d_plan, M.label("planning") },
    { T.d_rules, cheat and T.d_test or (custom and T.d_custom or T.d_standard), cheat and 0xE04A30FF or (custom and 0xF0C040FF or nil) },
    { "", "" },
  }
  local b = (not cheat) and best(D.record_key(M.job, d, custom)) or nil
  if b then
    inf[#inf + 1] = { T.d_best, b.payout and U.money(b.payout) or "-" }
    if b.time then inf[#inf + 1] = { T.d_time, U.clock(b.time) } end
    if b.rating then inf[#inf + 1] = { T.d_rating, (HEIST_THEME.ratings[b.rating] or {}).name or b.rating, MEDAL_RGBA[tier(b.rating)] } end
    inf[#inf + 1] = { T.d_runs, string.format("%d   %s %d", b.runs, T.d_won, b.wins) }
  else
    inf[#inf + 1] = { T.d_best, cheat and T.d_test or T.d_none, 0x9A9080FF }
  end
  return inf
end

local function page_main()
  local it = card_items()
  it[#it + 1] = { kind = "choice", row = "difficulty", label = D.BY_ID.difficulty.label, desc = D.BY_ID.difficulty.desc }
  it[#it + 1] = { kind = "button", label = T.qbranch, act = "page:settings", desc = T.qbranch_desc }
  local xl, xd = exit_text(T.to_mi6, T.to_mi6_desc)
  it[#it + 1] = { kind = "button", label = xl, act = "quit", desc = xd }
  it[#it + 1] = { kind = "primary", col = 2, label = T.begin, sub = T.begin_sub, act = "start", desc = M.val("planning") and T.begin_desc or T.begin_desc_noplan }
  return { layout = "lobby", items = it, title = T.title, kicker = T.kicker, subtitle = T.subtitle, info = info_main(),
           info_title = T.dossier, info_sub = job().name, cursor_act = "start", new_medal = TD.new_medal }
end

local function page_settings()
  local it = M.items_for_groups({ GUARDS = {
    { kind = "primary", label = T.done, act = "page:main", desc = T.done_desc },
    { kind = "button", label = T.reset, act = "reset", desc = T.reset_desc } } })
  return { layout = "settings", items = it, title = T.qbranch_title, kicker = T.title .. "  -  " .. label_of_run(), subtitle = "" }
end

local HOW = TD.how
local function page_debrief()
  local r = result or {}
  local win, ab = r.win, r.aborted
  local R = HEIST_THEME.ratings[r.rating or ""] or nil
  local t = { layout = "debrief", kicker = TD.kicker .. "  -  " .. (r.label or ""), best_chip = TD.best_chip, new_medal = TD.new_medal,
              checks_title = TD.checks, chart = TD.chart, legend = TD.legend }
  t.banner = ab and TD.abort or (win and TD.win or TD.fail)
  t.banner_rgba = ab and 0xB8B0A0FF or (win and 0xF0C040FF or 0xE04A30FF)
  if ab then t.subtitle = TD.abort_line
  else t.subtitle = ((not win and (r.msg or "") ~= "") and (r.msg:upper() .. ".  ") or "") .. (R and R.line or "") end
  if ab or not R then t.medal = -1 else t.medal, t.medal_name, t.medal_new = R.tier, string.format(TD.rating, R.name), news.rating and 1 or 0 end
  if r.cheat then t.news = TD.test
  elseif news.payout or news.time then
    local parts = {}
    if news.payout then parts[#parts + 1] = TD.nb_take .. " " .. U.money(r.payout) end
    if news.time then parts[#parts + 1] = TD.nb_time .. " " .. U.clock(r.time) end
    t.news = (news.first and (TD.first .. "   ") or "") .. TD.new_best .. table.concat(parts, "   ")
  else t.news = "" end
  local guards = (r.guards_ko or 0) + (r.guards_killed or 0)
  local crew = r.crew or 1
  t.cells = {
    { TD.s_take, U.money(r.payout or 0), (r.payout or 0) > 0 and 0xF0C040FF or nil, news.payout and true or false },
    { TD.s_time, U.clock(r.time), nil, news.time and true or false },
    { TD.s_alarm, r.alarm and U.clock(r.alarm_at) or TD.never, r.alarm and 0xE04A30FF or 0x7CC870FF },
    { TD.s_spotted, tostring(r.spotted or 0), (r.spotted or 0) > 0 and 0xF0C080FF or nil },
    { TD.s_guards, (r.guards_killed or 0) > 0 and string.format(TD.killed, guards, r.guards_killed) or tostring(guards) },
    { TD.s_civs, string.format("%d / %d", r.civ_hurt or 0, r.civ_killed or 0), ((r.civ_hurt or 0) + (r.civ_killed or 0)) > 0 and 0xE04A30FF or nil },
    { TD.s_hostages, (r.hostages or 0) > 0 and tostring(r.hostages) or TD.none },
    { TD.s_crew, ((r.crew_out or 0) > 0) and string.format(TD.crew_out, r.crew_out, crew) or TD.crew_lost, ((r.crew_out or 0) > 0) and 0x7CC870FF or 0xE04A30FF },
  }
  if ab then t.cells = { t.cells[2], t.cells[3], { TD.s_take, U.money(r.loot or 0) } } end
  local keys = r.keys or {}
  local function st(ok) if ok then return 1 end return 0 end
  t.checks = {
    { TD.c_inside, st(r.inside), "" },
    { string.format(TD.c_card, "GREEN"), st(keys.GREEN), keys.GREEN and (HOW[keys.GREEN] or keys.GREEN:upper()) or "" },
    { string.format(TD.c_card, "BLUE"), st(keys.BLUE), keys.BLUE and (HOW[keys.BLUE] or keys.BLUE:upper()) or "" },
    { string.format(TD.c_card, "RED"), st(keys.RED), keys.RED and (HOW[keys.RED] or keys.RED:upper()) or "" },
    { TD.c_vault, st(r.vault), "" },
    { TD.c_loot, st((r.loot or 0) > 0), (r.loot or 0) > 0 and string.format(TD.loot_line, r.bars or 0, r.cash or 0) or "" },
    { TD.c_out, win and 1 or (ab and 0 or 2), win and (r.exit and r.exit:upper() or "") or "" },
  }
  if ab then t.checks = nil end
  local bars = {}
  local function short(v)   -- $4.95M, $100K
    local a = math.abs(v)
    local s = a >= 1e6 and string.format("$%.2fM", a / 1e6) or (a >= 1e3 and string.format("$%dK", math.floor(a / 1e3 + 0.5)) or string.format("$%d", a))
    return (v < 0 and "-" or "") .. s
  end
  local function bar(v, k, l) if v and v ~= 0 then bars[#bars + 1] = { math.floor(v), k, l, short(v) } end end
  if win then   -- (the take's sums: a busted job takes nothing home)
    bar((r.bars or 0) * (r.bar_value or 0), 1, TD.b_vault)
    bar((r.cash or 0) * (r.money_value or 0), 1, TD.b_cash)
    bar(r.bonus, 1, TD.b_bonus)
    bar(-(r.fines or 0), 3, TD.b_fines)
    bar(-(r.planning or 0), 3, TD.b_plan)
    bar(-(r.bribes or 0), 3, TD.b_bribes)
  end
  t.bars = bars
  local chips, earned = {}, 0
  for _, m in ipairs(r.medals or {}) do chips[#chips + 1] = { m[1], m[2] and 1 or 0 }; if m[2] then earned = earned + 1 end end
  table.sort(chips, function(a, b) if a[2] ~= b[2] then return a[2] > b[2] end return a[1] < b[1] end)
  t.chips = chips
  t.chips_title = string.format(TD.chips, earned, #chips)
  local xl, xd = exit_text(TD.to_mi6, TD.to_mi6_desc)
  t.items = {
    { kind = "primary", label = TD.again, act = "again", desc = TD.again_desc .. (r.label or "") },
    { kind = "button", label = TD.new_job, act = "page:main", desc = TD.new_job_desc },
    { kind = "button", label = xl, act = "quit", desc = xd },
  }
  return t
end

-- ---- actions ----
local function sfx(id) if arena and arena.sfx then pcall(arena.sfx, id) end end

local function save()
  M.save_prefs({ job = M.job })
end

local function launch()
  if M.busy then return end
  local j = job()
  if not D.available(j) then sfx(SFX.back); return end
  local S, d, env, cheat, custom = D.settings(M)
  S.cheat, S.custom = cheat or nil, custom or nil
  env.GE_LEVELGEN = j.lg
  local label = label_of_run()
  local text = U.serialize(S)
  save()
  M.busy = true
  sfx(SFX.start)
  M.log("BEGIN %s (%s): difficulty %d, %d bytes of settings, deal %s, partner %s", label, j.id, d, #text, M.label("seed"), tostring(M.val("partner")))
  if not ui.launch({ mode = RUN_MODE, settings = text, preset = j.id, label = label, difficulty = d, env = env, stage = HOST and "levelgen" or nil }) then
    M.busy = false
    M.log("launch refused")
  end
end

local function again()
  if M.busy then return end
  local r = ui.retry()
  if not r then M.log("no job to run again"); return end
  r.lobby = ""
  if HOST then r.stage = "levelgen" end
  M.busy = true
  sfx(SFX.start)
  M.log("AGAIN %s (mode %s, %d bytes of settings)", r.label or "", r.mode, #(r.settings or ""))
  if not ui.launch(r) then M.busy = false; M.log("again refused") end
end

local function pick_job(id)
  local j = D.JOB_BY_ID[id]
  if not j or not D.available(j) then sfx(SFX.back); return end
  M.job = id
  sfx(SFX.select)
  if not HOST and j.lg ~= here.lg and not M.busy then   -- the other job's level: the lobby moves there (a stage load; hosted, there is no level)
    save()
    M.busy = true
    M.log("job %s: moving the lobby to its level (%s)", id, j.lg)
    M.dirty = true
    if not ui.launch({ mode = "heist-menu", lobby = "menu", preset = id, label = label_of_run(), env = { GE_LEVELGEN = j.lg } }) then
      M.busy = false
      M.log("move refused")
    end
    return
  end
  M.log("job %s", id)
  M.cursor = M.find_act("start") or M.cursor
  M.dirty = true
end

M = U.new{
  name = "heist", prefs = "heist_menu", rows = D.ROWS, groups = D.GROUPS, group_col = D.GROUP_COL, group_label = T.groups,
  pages = { main = page_main, settings = page_settings, debrief = page_debrief },
  back = { settings = "main", debrief = "main" },
  footer = T.footer, sfx = sfx, SFX = SFX,
  act = function(M_, a, it)
    if a == "start" then launch()
    elseif a == "job" then pick_job(it.job)
    elseif a == "again" then again()
    elseif a == "quit" then
      M.busy = true
      sfx(SFX.back)
      save()
      if HOST then   -- the hub hosts this lobby: back to its main page (no stage load)
        M.log("MAIN MENU")
        M.busy = false
        HOST.back()
        return
      end
      M.log("EXIT the game")
      if not ui.exit() then M.busy = false end
    elseif a == "reset" then
      local keep = M.v.difficulty
      M.defaults()
      M.v.difficulty = keep
      sfx(SFX.change)
    end
  end,
}
HEIST_MENU = M
M.job = here.id

-- ---- the stage ----
-- the page it opens on: the saved choices and job, the job a run came back with, the debrief of the run that just ended
local function open_from(c)
  cur = c
  M.load_prefs()
  M.job = here.id
  if cur and cur.preset ~= "" and D.JOB_BY_ID[cur.preset] then M.job = cur.preset end
  if not D.available(job()) then M.job = here.id end
  M.page, result = "main", nil
  if cur and cur.lobby == "postgame" then
    result = U.parse(ui.result())
    if result then
      record(result)
      M.page = "debrief"
    end
  end
  M.log("lobby: %s, job %s%s, %s", M.page, M.job, HOST and " (hosted by the hub)" or string.format(" on the %s level", here.lg),
        cur and ("after " .. (cur.label ~= "" and cur.label or "a run")) or "first boot")
end

if HOST then   -- the hub's stage, music, camera and ticks: it opens this page and calls us (see the header)
  function M.host_open(c)
    M.busy, M.cursor, M.last, M.dirty = false, 0, {}, true
    M.quiet = 0.4   -- (the press that opened it is not ours)
    for k in pairs(news) do news[k] = nil end
    open_from(c)
    M.cursor = (M.page == "main") and (M.find_act("start") or 1) or 1
    M.show()
    M.cursor = (M.page == "main") and (M.find_act("start") or 1) or 1
    M.show()
  end
  function M.host_tick(dt) M.tick(dt); mode.state(M.state_text()) end
  return
end

open_from(cur)

-- the people behind the menu: a few staff and guards standing about, fidgeting (no AI to speak of: spectators)
local FIDGETS = { "yawn", "scratch_leg", "adjust", "chat", "listen", "cough", "look" }
local extras = {}
M.extras = extras   -- (tests)
local function people(room)
  if not (room and room.pads and spawn and spawn.chr) then return end
  local bodies = { { "St_Petersburg_Guard", "kf7" }, { "Civilian_1_Female", "none" }, { "Civilian_2", "none" }, { "St_Petersburg_Guard", "d5k" },
                   { "Civilian_3", "none" }, { "Formal_Wear", "none" }, { "Civilian_4", "none" }, { "St_Petersburg_Guard", "kf7" } }
  local n = #room.pads
  for i, b in ipairs(bodies) do
    local p = room.pads[math.floor(n * (i - 0.5) / #bodies) + 1]
    local ok, c = pcall(spawn.chr, p, { body = b[1], weapon = b[2], behaviour = "spectator" })
    if ok and c then extras[#extras + 1] = { chr = c, next = 1 + math.random() * 6 } end
  end
end

local function centre_pad(room)
  if not room then return LEVEL and LEVEL.start_pad end
  local cx, cz = (room.rect[1] + room.rect[3]) / 2, (room.rect[2] + room.rect[4]) / 2
  local bestp, bd = nil, 1e18
  for _, p in ipairs(room.pads) do
    local x, _, z = pads.pos(p)
    if x then
      local dd = (x - cx) ^ 2 + (z - cz) ^ 2
      if dd < bd then bestp, bd = p, dd end
    end
  end
  return bestp
end

-- the job is frozen behind the menu (AC's playtest 2026-10-28: sitting in the menu raised the alarm: the level's own cameras watched
-- the idle Bond and the stock camera logic sounded it). Every camera off, a spot (should one happen) raises nothing, and no alarm
-- stands; the people here are spectators (no AI list: they see nothing) and nothing of the heist runs. tests/heist_menu/run_idle.sh
local function freeze()
  if cctv then
    if cctv.alarm_on_spot then pcall(cctv.alarm_on_spot, false) end
    for _, cam in ipairs(cctv.list and cctv.list() or {}) do pcall(cctv.disable, cam) end
  end
  if alarm and alarm.active and alarm.active() then alarm.stop() end
end

-- the lobby (start menu and debrief) plays GoldenEye's "Control elevator"; the run is a fresh stage load, so its own level music
-- starts as usual (music.set is per stage and cleared at the next stage start)
local LOBBY_MUSIC = "elevator_control"
local LOBBY_MUSIC_VOL = 0.8   -- 0x6665, the level tracks' default (the elevator track's own default is a quiet 0x3FFF)
if music and music.set then
  local ok, err = pcall(music.set, LOBBY_MUSIC, LOBBY_MUSIC_VOL)
  M.log("lobby music: set %s -> %s", LOBBY_MUSIC, tostring(ok and err or err))
end

on("stage_start", function()
  if music and music.play then   -- (should music.set have been refused or missed, start the track directly)
    local okc, curm = pcall(music.current)
    if not (okc and curm == LOBBY_MUSIC) then M.log("lobby music: playing %s (was %s)", LOBBY_MUSIC, tostring(curm)); pcall(music.play, LOBBY_MUSIC, LOBBY_MUSIC_VOL) end
  end
  plan.hold(true)
  ui.escape_back(true)
  if crosshair and crosshair.hide then pcall(crosshair.hide, true) end
  if ar and ar.reset then pcall(ar.reset) end   -- (the last run's AR waypoints and marks are presentation state that outlives the stage)
  if hud and hud.set then for l = 1, 3 do pcall(hud.set, l, "") end end
  local room = LEVEL and LEVEL.rooms and LEVEL.rooms[BACKDROP[here.id] or "hall"]
  local pad = centre_pad(room)
  for p = 0, bond.count() - 1 do
    if pad then bond.warp(p, pad, p * 80, 0) end
    pcall(bond.equip, p, "fist")
  end
  people(room)
  freeze()
  if bench and bench.sweep then bench.sweep(3, 8) end
  if M.page == "debrief" and result and (news.payout or news.time) then sfx(SFX.best) end
  M.show()
  M.cursor = (M.page == "main") and (M.find_act("start") or 1) or 1
  M.show()
end)

on("ui_tick", function(dt)
  M.tick(dt)
  mode.state(M.state_text())
end)

local t = 0
on("tick", function(dt)
  t = t + dt
  if alarm and alarm.active and alarm.active() then alarm.stop(); M.log("an alarm in the lobby: stopped") end
  for _, s in ipairs(extras) do
    if t >= s.next then
      if spawn.anim and spawn.anim(s.chr, FIDGETS[math.random(#FIDGETS)]) then s.next = t + 5 + math.random() * 10 else s.next = t + 2 end
    end
  end
end)
