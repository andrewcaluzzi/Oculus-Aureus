-- The arena's LOBBY: the start menu and the post-game (GE_MODE=arena-menu on the Colosseum; shipped as
-- Data/Scripts/modes/arena-menu.lua; modding/arena.md "Start menu and post-game"). Play Arena.bat boots into it; every arena run
-- (from here or from a launcher) comes back to it when it ends (tools/arena_flow.lua arms that), on the post-game.
--
-- The Colosseum is up behind the menu: the crowd in the stands, the camera turning slowly from the middle of the pit. Nobody fights:
-- the controls are held (plan.hold: the crew's synchronized buttons drive the menu, so the menu is sim state like the heist's
-- planning board) and no wave starts. Starting a run, Retry and Quit end this stage; the hand-off (port/src/port_arenamenu.c) loads
-- the Colosseum again with the run's mode and settings at once, without the stock menus or the mission report.
--
-- Pages: MAIN (the presets with their bests and medals, the difficulty, ENTER THE ARENA), ADVANCED (every setting, by group), and the
-- POST-GAME (the result, the stats, kills per wave; RETRY, CHANGE SETTINGS, QUIT TO TITLE). The choices are remembered in
-- Data/Saves/arena_menu.ini (amenu.prefs: one line per setting, the choice's label; "preset" too).
--
-- Keys: up / down (stick, D-pad, W / S, C-up / C-down, the mouse wheel) move; left / right (stick, D-pad, C-left / C-right, the
-- weapon keys, the bumpers) change a setting; fire or A selects; B, USE or Escape goes back; START (Enter) selects too. The mouse
-- points and clicks (right click = back / the previous value). Rendering: port/src/port_arenamenu_draw.c (port_hudstyle.c).
-- Test hooks: ARENA_MENU (the live state), ARENA_MENU.press(mask) (a board event as if pressed), ARENA_MENU.go(screen).

include("_arena_menu_defs")
local D = ARENA_MENU_DEFS
local UP, DOWN, LEFT, RIGHT, SELECT, BACK, START = 1, 2, 4, 8, 16, 32, 64

local M = { screen = "main", cursor = 0, v = D.defaults(), dirty = true, t = 0, quiet = 0.4, busy = false, last = { main = 0 } }
ARENA_MENU = M
local cur = amenu and amenu.current() or nil
local result = nil
local function log(fmt, ...) print(string.format("ge: arena: menu: " .. fmt, ...)) end
local function sfx(id) if arena and arena.sfx then pcall(arena.sfx, id) end end
local SFX = { move = 18, change = 43, select = 199, page = 77, back = 78, start = 50, best = 160 }

-- ---- the saved choices ----
local function load_prefs()
  local pr = amenu and amenu.prefs() or {}
  for _, r in ipairs(D.ROWS) do
    local want = pr[r.id]
    if want then
      for i, c in ipairs(r.choices) do if c[1] == want then M.v[r.id] = i end end
    end
  end
  M.saved_preset = pr.preset
  return pr
end

local function save_prefs()
  local t = {}
  for _, r in ipairs(D.ROWS) do t[r.id] = D.label(M.v, r.id) end
  local p = D.preset_of(M.v)
  t.preset = p and p.id or "custom"
  if amenu then amenu.save_prefs(t) end
end

-- ---- the bests (arena_scores.ini, solo only) ----
local DIFF_SUFFIX = D.DIFF_SUFFIX
local T = D.T
local function mmss(s) s = math.floor(s or 0); return string.format("%d:%02d", s // 60, s % 60) end
local function best(key)
  if not (scores and scores.enabled and scores.enabled()) then return nil end
  local b = {}
  for _, f in ipairs({ "best_wave", "best_kills", "best_time", "best_medal", "runs", "clears" }) do b[f] = scores.get(key .. "_" .. f) end
  if not b.runs and not b.best_wave then return nil end
  return b
end
local function preset_key(p) return p.key .. DIFF_SUFFIX[D.val(M.v, "difficulty")] end
local function preset_endless(p) return (p.set.pacing or 1) == 4 end
local function best_line(p)
  local b = best(preset_key(p))
  if not b then return "", 0 end
  local medal = math.floor(b.best_medal or 0)
  if not preset_endless(p) and (b.clears or 0) > 0 and b.best_time then return "BEST " .. mmss(b.best_time), medal end
  return string.format("BEST WAVE %d", math.floor(b.best_wave or 0)), medal
end
local function medal_name(m) return m > 0 and string.format(T.medal, D.MEDALS[m]) or T.no_medal end

-- ---- the pages ----
local MEDAL_RGBA = { [0] = 0x9A9080FF, 0xD08A48FF, 0xD4D8E0FF, 0xF0C040FF, 0xA8ECF8FF }

local function chaos_line()
  local mu, el = D.val(M.v, "mutators"), D.val(M.v, "elites")
  if not mu and not el then return T.off end
  return string.format("%s%s%s (%s)", mu and "MUTATORS" or "", (mu and el) and " + " or "", el and "ELITES" or "", D.label(M.v, "intensity"))
end

local function run_label()
  local p = D.preset_of(M.v)
  local d = D.val(M.v, "difficulty")
  return (p and p.name or T.custom) .. (d ~= 1 and (" - " .. D.label(M.v, "difficulty")) or ""), p
end

local function items_main()
  local it = {}
  local pnow = D.preset_of(M.v)
  for _, p in ipairs(D.PRESETS) do
    local line, medal = best_line(p)
    it[#it + 1] = { kind = "preset", label = p.name, sub = p.desc, right = line, medal = medal, chosen = (pnow == p), act = "preset", preset = p.id,
                    desc = p.desc .. (p.id:match("^coop") and T.coop_note or "") }
  end
  it[#it + 1] = { kind = "choice", label = D.BY_ID.difficulty.label, value = D.label(M.v, "difficulty"), row = "difficulty", desc = D.BY_ID.difficulty.desc }
  it[#it + 1] = { kind = "button", label = T.qbranch, act = "advanced", desc = T.qbranch_desc }
  it[#it + 1] = { kind = "button", label = T.to_mi6, act = "quit", desc = T.to_mi6_desc }
  it[#it + 1] = { kind = "primary", col = 2, label = T.begin, sub = T.begin_sub, act = "start", desc = T.begin_desc }
  return it
end

local function info_main()
  local label, p = run_label()
  local inf = {
    { T.d_pacing, D.label(M.v, "pacing") .. ((D.val(M.v, "boss") > 0) and ("   " .. T.d_boss .. " " .. D.label(M.v, "boss")) or ("   " .. T.d_noboss)) },
    { T.d_diff, D.label(M.v, "difficulty") },
    { T.d_chaos, chaos_line() },
    { T.d_whacky, D.label(M.v, "whacky") },
    { T.d_team, D.label(M.v, "players") },
    { T.d_cap, D.label(M.v, "cap") },
    { T.d_kit, D.label(M.v, "loadout") .. "   " .. T.d_heal .. " " .. D.label(M.v, "healing") },
    { T.d_crowd, D.label(M.v, "crowd") .. "   " .. ((D.val(M.v, "favour")) and T.d_fav_on or T.d_fav_off) },
    { "", "" },
  }
  local b = best(D.score_key(M.v))
  if D.val(M.v, "players") then
    inf[#inf + 1] = { T.d_best, T.d_solo, 0x9A9080FF }
  elseif b then
    local endless = D.val(M.v, "pacing").mode == "endless"
    inf[#inf + 1] = { T.d_best, string.format("WAVE %d   KILLS %d", math.floor(b.best_wave or 0), math.floor(b.best_kills or 0)) }
    if b.best_time then inf[#inf + 1] = { endless and T.d_longest or T.d_fastest, mmss(b.best_time) } end
    local m = math.floor(b.best_medal or 0)
    inf[#inf + 1] = { T.d_medal, m > 0 and D.MEDALS[m] or T.no_medal, MEDAL_RGBA[m] }
    inf[#inf + 1] = { T.d_runs, string.format("%d   %s %d", math.floor(b.runs or 0), T.d_cleared, math.floor(b.clears or 0)) }
  else
    inf[#inf + 1] = { T.d_best, T.d_none, 0x9A9080FF }
  end
  return inf, label
end

local function items_advanced()
  local it = {}
  for _, g in ipairs(D.GROUPS) do
    local col = D.GROUP_COL[g]
    it[#it + 1] = { kind = "header", col = col, label = T.groups[g] or g }
    for _, r in ipairs(D.ROWS) do
      if r.group == g then
        local dim = (r.id == "downed" and not D.val(M.v, "players")) or (r.id == "intensity" and not D.val(M.v, "mutators") and not D.val(M.v, "elites"))
        it[#it + 1] = { kind = "choice", col = col, label = r.label, value = D.label(M.v, r.id), row = r.id, desc = r.desc, dim = dim and 1 or 0 }
      end
    end
    if g == "PLAYERS" then
      it[#it + 1] = { kind = "primary", col = 1, label = T.done, act = "main", desc = T.done .. " (B / Escape)." }
      it[#it + 1] = { kind = "button", col = 1, label = T.reset, act = "reset", desc = T.reset_desc }
    end
  end
  return it
end

local function result_items()
  local r = result or {}
  return {
    { kind = "primary", label = T.retry, act = "retry", desc = T.retry_desc .. (r.label or "") },
    { kind = "button", label = T.new_assignment, act = "main", desc = T.new_assignment_desc },
    { kind = "button", label = T.to_mi6, act = "quit", desc = T.to_mi6_desc },
  }
end

local function postgame_view(t)
  local r = result or {}
  local news = {}
  local nw, nk, nt = false, false, false
  for _, n in ipairs(r.news or {}) do
    news[#news + 1] = n
    if n:match("^WAVE") then nw = true elseif n:match("^KILLS") then nk = true elseif n:match("^TIME") then nt = true end
  end
  local acc = (r.shots or 0) > 0 and string.format("%d%%", math.floor(100 * (r.hits or 0) / r.shots + 0.5)) or "-"
  local wave = (r.waves or 0) > 0 and string.format("%d / %d", r.wave or 0, r.waves) or tostring(r.wave or 0)
  local info = {
    { T.s_wave, wave, nil, nw },
    { T.s_kills, tostring(r.kills or 0), nil, nk },
    { T.s_time, mmss(r.time), nil, nt },
    { T.s_acc, acc },
    { T.s_heads, tostring(r.headshots or 0) },
    { T.s_multi, tostring(r.multi or 0) },
    { T.s_streak, tostring(r.streak or 0) },
    { T.s_peak, (r.peak or -1) >= 0 and tostring(r.peak) or T.off },
    { T.s_hits, tostring(r.hits_taken or 0) },
  }
  if (r.elites or 0) > 0 or (r.mutators or 0) > 0 then
    info[#info + 1] = { T.s_elites, tostring(r.elites or 0) }
    info[#info + 1] = { T.s_mut, tostring(r.mutators or 0) }
  end
  local bars = {}
  for _, w in ipairs(r.wave_list or {}) do bars[#bars + 1] = { w[1], w[3] } end
  t.banner = r.win and T.win or T.fell
  t.banner_rgba = r.win and 0xF0C040FF or 0xE04A30FF
  t.kicker = T.debrief .. "  -  " .. (r.label or "")
  t.subtitle = r.win and string.format(T.cleared, r.waves or 0, mmss(r.time))
               or ((r.wave or 0) < 1 and T.fell_early or string.format(T.fell_in, r.wave or 0, r.title or ""))
  t.medal = r.coop and -1 or (r.medal or 0)
  t.medal_name = medal_name(r.medal or 0)
  t.medal_new = r.medal_new and 1 or 0
  t.news = (#news > 0 and (T.new_best .. table.concat(news, "   "))) or (r.first and T.first_run or (r.coop and T.coop_scores or ""))
  t.chart, t.legend, t.best_chip, t.new_medal = T.chart, T.legend, T.best_chip, T.new_medal
  t.info, t.bars = info, bars
end

local function build_items()
  if M.screen == "main" then M.items = items_main()
  elseif M.screen == "advanced" then M.items = items_advanced()
  else M.items = result_items() end
end

local function selectable(i) local it = M.items[i]; return it and it.kind ~= "header" and (it.dim or 0) == 0 end

local function fix_cursor(dir)
  local n = #M.items
  if n == 0 then M.cursor = 0; return end
  if M.cursor < 1 or M.cursor > n then M.cursor = 1 end
  for _ = 1, n do
    if selectable(M.cursor) then return end
    M.cursor = (M.cursor - 1 + (dir or 1)) % n + 1
  end
end

local function find_item(pred) for i, it in ipairs(M.items) do if pred(it) then return i end end end

local function show()
  build_items()
  fix_cursor(1)
  local t = { screen = M.screen, cursor = M.cursor, items = M.items,
              footer = T.footer }
  local sel = M.items[M.cursor]
  t.desc = sel and sel.desc or ""
  if M.screen == "main" then
    t.title, t.kicker = T.title, T.kicker
    t.subtitle = T.subtitle
    t.info, t.info_sub = info_main()
    t.info_title = T.dossier
  elseif M.screen == "advanced" then
    t.title, t.kicker = T.qbranch_title, T.title .. "  -  " .. (run_label())
    t.subtitle = ""
  else
    postgame_view(t)
  end
  amenu.view(t)
  mode.state(string.format("AMENU %s %d %s", M.screen, M.cursor, D.serialize(M.v)))
  M.dirty = false
end

local function go(screen)
  if screen ~= M.screen then
    M.last[M.screen] = M.cursor
    M.screen = screen
    build_items()
    M.cursor = M.last[screen] or 0
    if M.cursor == 0 then
      if screen == "main" then
        local p = D.preset_of(M.v)
        M.cursor = find_item(function(it) return it.act == "start" end) or 1
        if not p then M.cursor = find_item(function(it) return it.act == "advanced" end) or M.cursor end
      else M.cursor = 1 end
    end
    sfx(SFX.page)
    log("page %s", screen)
  end
  M.dirty = true
end
M.go = go

-- ---- actions ----
local function launch()
  if M.busy then return end
  local S, coop, slots = D.settings(M.v)
  local label, p = run_label()
  local text = D.serialize(S)
  save_prefs()
  M.busy = true
  sfx(SFX.start)
  if crowd and crowd.cheer then pcall(crowd.cheer, 3, 1.0) end
  log("START %s (%s): coop %s slots %s key %s, %d bytes", label, p and p.id or "custom", tostring(coop), tostring(slots), S.score_key, #text)
  if not amenu.launch({ mode = "arena", settings = text, preset = p and p.id or "custom", label = label, coop = coop, slots = slots }) then
    M.busy = false
    log("launch refused")
  end
end

local function retry()
  if M.busy then return end
  local r = amenu.retry()
  if not r then log("no run to retry"); return end
  r.lobby = ""
  M.busy = true
  sfx(SFX.start)
  if crowd and crowd.cheer then pcall(crowd.cheer, 3, 1.0) end
  log("RETRY %s (mode %s, %d bytes of settings)", r.label or "", r.mode, #(r.settings or ""))
  if not amenu.launch(r) then M.busy = false; log("retry refused") end
end

local function change_row(row, d)
  local r = D.BY_ID[row]
  M.v[row] = ((M.v[row] or r.default) - 1 + d) % #r.choices + 1
  sfx(SFX.change)
  M.dirty = true
end

local function activate(it, ev)
  if not it then return end
  if it.kind == "choice" then change_row(it.row, (ev == BACK) and -1 or 1); return end
  local a = it.act
  if a == "preset" then
    D.apply_preset(M.v, it.preset)
    sfx(SFX.select)
    log("preset %s", it.preset)
    M.cursor = find_item(function(x) return x.act == "start" end) or M.cursor
    M.dirty = true
  elseif a == "start" then launch()
  elseif a == "advanced" then go("advanced")
  elseif a == "main" then go("main")
  elseif a == "quit" then
    M.busy = true
    sfx(SFX.back)
    log("QUIT to the title")
    if not amenu.quit() then M.busy = false end
  elseif a == "retry" then retry()
  elseif a == "reset" then
    local p = D.preset_of(M.v)
    local keep = M.v.difficulty
    if not p then M.v = D.defaults(); M.v.difficulty = keep else D.apply_preset(M.v, p.id) end
    sfx(SFX.change)
    M.dirty = true
  end
end

local function move(d)
  local n = #M.items
  if n == 0 then return end
  local c = M.cursor
  for _ = 1, n do
    c = (c - 1 + d) % n + 1
    if selectable(c) then break end
  end
  if c ~= M.cursor then M.cursor = c; sfx(SFX.move); M.dirty = true end
end

local function step(ev)
  if M.busy then return end
  local it = M.items[M.cursor]
  if ev & UP ~= 0 then move(-1) end
  if ev & DOWN ~= 0 then move(1) end
  it = M.items[M.cursor]
  if ev & (LEFT | RIGHT) ~= 0 then
    local d = (ev & LEFT ~= 0) and -1 or 1
    if it and it.kind == "choice" then change_row(it.row, d)
    elseif M.screen == "postgame" then move(d)
    elseif M.screen == "main" then   -- across the columns: the lists <-> ENTER THE ARENA
      if it and it.col == 2 and d < 0 then M.cursor = M.last.main_col1 or 1; sfx(SFX.move); M.dirty = true
      elseif it and it.col ~= 2 and d > 0 then M.last.main_col1 = M.cursor; M.cursor = find_item(function(x) return x.act == "start" end) or M.cursor; sfx(SFX.move); M.dirty = true end
    end
  end
  it = M.items[M.cursor]
  if ev & (SELECT | START) ~= 0 then activate(it, SELECT) end
  if ev & BACK ~= 0 then
    if M.screen == "advanced" then go("main")
    elseif M.screen == "postgame" then go("main")
    else M.cursor = find_item(function(x) return x.act == "quit" end) or M.cursor; sfx(SFX.back); M.dirty = true end
  end
end

-- a mouse event (plan.pointer, a session command): a = page * 64 + item, b = 1 select, 2 back, 4 / 5 wheel, 6 hover
local SCREEN_ID = { main = 1, advanced = 2, postgame = 3 }
local function point(a, b)
  if M.busy then return end
  local page, i = a // 64, a % 64
  if b == 4 then step(UP); return end
  if b == 5 then step(DOWN); return end
  if page ~= SCREEN_ID[M.screen] then return end
  if b == 2 and i == 0 then step(BACK); return end
  if i < 1 or i > #M.items or not selectable(i) then return end
  if M.cursor ~= i then M.cursor = i; M.dirty = true; if b == 6 then sfx(SFX.move) end end
  if b == 1 then activate(M.items[i], SELECT)
  elseif b == 2 then
    if M.items[i].kind == "choice" then activate(M.items[i], BACK) else step(BACK) end
  end
end

function M.press(mask) if plan and plan.held() then test.plan_input(0, mask) end end

-- ---- the crowd behind the menu (a light copy of the arena's: real spectators in the front rows, impostors behind) ----
local CROWD_SKINS = { "civilian_f", "civilian_m1", "civilian_m2", "civilian_m3", "scientist_m", "scientist_f", "russian_infantry",
                      "russian_commandant", "naval_officer", "helicopter_pilot", "st_petersburg_guard", "janus_marine",
                      "moonraker_m", "moonraker_f", "biker", "terrorist" }
local FIDGETS = { "yawn", "flies", "scratch_leg", "adjust", "chat", "listen", "cough", "laugh", "look" }
local seats, queue, qi, spec = {}, {}, 1, {}

local function crowd_setup()
  local all = {}
  local cx, cz, best_d = nil, nil, 1e18
  for i = 0, pads.count() - 1 do
    local x, y, z = pads.pos(i)
    if x and y > 1 then all[#all + 1] = { p = i, y = y, r = math.random() }
    elseif x then
      local d = x * x + z * z
      if d < best_d then best_d, cx = d, i end
    end
  end
  table.sort(all, function(a, b) if a.y ~= b.y then return a.y < b.y end return a.r < b.r end)
  local n = math.min(D.val(M.v, "crowd"), 150, #all)
  for i = 1, n do queue[#queue + 1] = all[i].p end
  if D.val(M.v, "impostors") > 0 and crowd and crowd.impostors then
    local xyz = {}
    local step = math.max(1, (#all - n) / 1000)
    local k = n + 1
    while k <= #all and #xyz < 3000 do
      local x, y, z = pads.pos(all[math.floor(k)].p)
      xyz[#xyz + 1] = x; xyz[#xyz + 1] = y; xyz[#xyz + 1] = z
      k = k + step
    end
    pcall(crowd.impostors, xyz, CROWD_SKINS)
  end
  return cx
end

local function crowd_tick()
  local n = 0
  while qi <= #queue and n < 40 and spawn.free() > 20 do
    local c = spawn.chr(queue[qi], { skin = CROWD_SKINS[math.random(#CROWD_SKINS)], weapon = "none", behaviour = "spectator" })
    if c then spec[#spec + 1] = { chr = c, next = M.t + 1 + math.random() * 8 } end
    qi = qi + 1
    n = n + 1
  end
  for _, s in ipairs(spec) do
    if s.react and M.t >= s.react_at then spawn.anim(s.chr, s.react, true); s.react = nil; s.next = M.t + 5 + math.random() * 6
    elseif M.t >= s.next then
      if spawn.anim(s.chr, FIDGETS[math.random(#FIDGETS)]) then s.next = M.t + 5 + math.random() * 12 else s.next = M.t + 1 end
    end
  end
end

local function crowd_react(kinds)
  for _, s in ipairs(spec) do
    if math.random() < 0.6 then s.react, s.react_at = kinds[math.random(#kinds)], M.t + math.random() * 1.5 end
  end
end

-- ---- the stage ----
load_prefs()
if cur and cur.preset ~= "" and cur.preset ~= "custom" and D.PRESET_BY_ID[cur.preset] then
  local p = D.preset_of(M.v)
  if not p or p.id ~= cur.preset then D.apply_preset(M.v, cur.preset) end
end
if cur and cur.lobby == "postgame" then
  result = D.parse(amenu.result())
  if result then M.screen = "postgame" end
end
log("lobby: %s, preset %s, %s", M.screen, (D.preset_of(M.v) or { id = "custom" }).id, cur and ("after " .. (cur.label ~= "" and cur.label or "a run")) or "first boot")

on("stage_start", function()
  plan.hold(true)
  amenu.escape_back(true)
  if arena and arena.hud then pcall(arena.hud, false) end   -- (the run's styled HUD stays on across the stage load otherwise)
  if crosshair and crosshair.hide then pcall(crosshair.hide, true) end
  local centre = crowd_setup()
  for p = 0, bond.count() - 1 do
    if centre then bond.warp(p, centre, p * 80, 0) end
    pcall(bond.equip, p, "fist")
  end
  if bench and bench.sweep then bench.sweep(4, 7) end
  if M.screen == "postgame" and result then
    if result.win then pcall(crowd.cheer, 6, 1.0) else pcall(crowd.cheer, 0.1, 0.0) end
  end
  build_items()
  M.cursor = (M.screen == "main") and (find_item(function(x) return x.act == "start" end) or 1) or 1
  show()
end)

on("ui_tick", function(dt)
  M.t = M.t + dt
  M.quiet = M.quiet - dt
  local ev = plan.input(0)
  while ev ~= 0 do
    if M.quiet <= 0 then step(ev) end
    ev = plan.input(0)
  end
  local a, b = plan.pointer(0)
  while a do
    if M.quiet <= 0 then point(a, b) end
    a, b = plan.pointer(0)
  end
  for p = 1, bond.count() - 1 do
    while plan.input(p) ~= 0 do end
    while plan.pointer(p) do end
  end
  if M.dirty then show() end
end)

on("tick", function(dt)
  crowd_tick()
  if M.screen == "postgame" and result and not M.reacted and M.t > 0.5 then
    M.reacted = true
    if result.win then crowd_react({ "dance", "surrender", "laugh" }) else crowd_react({ "fists", "cross_arms", "laugh" }) end
    if result.news and #result.news > 0 then sfx(SFX.best) end
  end
end)
