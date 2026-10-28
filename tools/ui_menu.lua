-- ui_menu.lua: the generic mode menu layer's Lua half (shipped as Data/Scripts/modes/_ui_menu.lua; midas-notes
-- features/ui-design-system.md "Generic ui layer"; fork modding/lua.md "The ui table"). Any mode's lobby builds its menu on this: a
-- start page of cards and rows, a settings page of grouped rows, a debrief; the look is port/src/port_uimenu_draw.c (the arena's).
-- It includes nothing (include() nests four deep at most).
--
--   local M = UI_MENU.new{
--     name = "heist",                 -- the logs' tag; prefs = the preference file (ui.prefs(name)) the rows are remembered in
--     prefs = "heist_menu",
--     rows = { { id =, group =, label =, choices = { { label, value }, ... }, default = <index>, desc =, dim = function(M) end }, ... },
--     groups = { "JOB", ... }, group_col = { JOB = 1, ... }, group_label = { JOB = "THE JOB", ... },
--     pages = { main = function(M) return t end, ... },   -- t = { layout = "lobby" | "settings" | "debrief", items = {...}, title =, ... }
--     back = { settings = "main", ... },                   -- BACK on a page: go there (nil: the cursor jumps to the item with act "quit")
--     act = function(M, act, item) end,                    -- an item's act (the built-in "page:<name>" goes to that page)
--     footer =, sfx = function(id) end, SFX = { move =, change =, select =, page =, back = },
--   }
--   M.v[row id] = choice index;  M.val(id) / M.label(id);  M.change(id, d);  M.go(page);  M.dirty = true (re-push the view)
--   M.load_prefs() / M.save_prefs(extra);  M.items_for_groups(col_of)   -- a settings page's grouped rows
--   M.tick(dt)    from on("ui_tick"): drains the crew's board events (plan.input / plan.pointer) and pushes the view when dirty
--   UI_MENU.serialize(t) / UI_MENU.parse(text)   a Lua table constructor and back (data only: an empty environment)
-- Items: { kind = "header" | "card" | "choice" | "button" | "primary", label, value, sub, right, tag, col = 1 | 2, act, row, desc,
--          dim, chosen, medal, tint }. A choice item's value is its row's label unless given.
-- Keys: up / down move; left / right change a choice (on a lobby page they cross the columns; on a debrief they move); select or
-- start activates; back goes back. The mouse: a = page * 64 + item, b = 1 select, 2 back, 4 / 5 the wheel, 6 hover.
-- Sim state: the menu (M.page, M.cursor, M.v) changes only from the crew's synchronized events; M.state_text() is for mode.state.

UI_MENU = UI_MENU or {}
local U = UI_MENU
local UP, DOWN, LEFT, RIGHT, SELECT, BACK, START = 1, 2, 4, 8, 16, 32, 64
U.EV = { UP = UP, DOWN = DOWN, LEFT = LEFT, RIGHT = RIGHT, SELECT = SELECT, BACK = BACK, START = START }

-- ---- a tiny serializer: numbers, booleans, strings, tables with string keys or a list part ----
local function ser(x)
  local t = type(x)
  if t == "number" then
    if x == math.floor(x) and math.abs(x) < 1e15 then return string.format("%d", x) end
    return string.format("%.10g", x)
  elseif t == "boolean" then return x and "true" or "false"
  elseif t == "string" then return string.format("%q", x)
  elseif t == "table" then
    local parts, keys = {}, {}
    for i = 1, #x do parts[#parts + 1] = ser(x[i]) end
    for k in pairs(x) do if type(k) == "string" then keys[#keys + 1] = k end end
    table.sort(keys)
    for _, k in ipairs(keys) do parts[#parts + 1] = k .. "=" .. ser(x[k]) end
    return "{" .. table.concat(parts, ",") .. "}"
  end
  return "nil"
end
U.serialize = ser

function U.parse(text)
  if type(text) ~= "string" or text == "" then return nil end
  local f = load("return " .. text, "ui_menu", "t", {})
  if not f then return nil end
  local ok, r = pcall(f)
  if ok and type(r) == "table" then return r end
  return nil
end

function U.money(v)
  local s, neg = tostring(math.floor(math.abs(v or 0) + 0.5)), (v or 0) < 0
  local out = s:reverse():gsub("(%d%d%d)", "%1,"):reverse():gsub("^,", "")
  return (neg and "-$" or "$") .. out
end
function U.clock(s) s = math.floor(s or 0); return string.format("%d:%02d", s // 60, s % 60) end

function U.new(spec)
  local M = { spec = spec, page = spec.first or "main", cursor = 0, v = {}, items = {}, dirty = true, busy = false, quiet = spec.quiet or 0.4,
              last = {}, t = 0, page_ids = {} }
  local by = {}
  for i, r in ipairs(spec.rows or {}) do r.index = i; by[r.id] = r; M.v[r.id] = r.default or 1 end
  M.rows, M.by = spec.rows or {}, by
  do   -- page ids for the pointer (sorted names: the same on every machine)
    local names = {}
    for name in pairs(spec.pages) do names[#names + 1] = name end
    table.sort(names)
    for i, n in ipairs(names) do M.page_ids[n] = i end
  end
  local function log(fmt, ...) print(string.format("ge: " .. (spec.name or "ui") .. ": menu: " .. fmt, ...)) end
  M.log = log
  local function sfx(k) if spec.sfx and spec.SFX and spec.SFX[k] then pcall(spec.sfx, spec.SFX[k]) end end
  M.sfx = sfx

  function M.val(id) local r = by[id]; return r.choices[M.v[id] or r.default][2] end
  function M.label(id) local r = by[id]; return r.choices[M.v[id] or r.default][1] end
  function M.set(id, idx) if by[id] and by[id].choices[idx] then M.v[id] = idx; M.dirty = true end end
  function M.find_choice(id, value)
    for i, c in ipairs(by[id].choices) do if c[2] == value then return i end end
  end
  function M.defaults() for _, r in ipairs(M.rows) do M.v[r.id] = r.default or 1 end; M.dirty = true end
  function M.change(id, d)
    local r = by[id]
    M.v[id] = ((M.v[id] or r.default) - 1 + d) % #r.choices + 1
    sfx("change")
    if spec.changed then spec.changed(M, id) end
    M.dirty = true
  end

  -- ---- the saved choices (one line per row: the choice's label) ----
  function M.load_prefs()
    if not (ui and spec.prefs) then return {} end
    local pr = ui.prefs(spec.prefs)
    for _, r in ipairs(M.rows) do
      local want = pr[r.id]
      if want then for i, c in ipairs(r.choices) do if c[1] == want then M.v[r.id] = i end end end
    end
    return pr
  end
  function M.save_prefs(extra)
    if not (ui and spec.prefs) then return end
    local t = {}
    for _, r in ipairs(M.rows) do t[r.id] = M.label(r.id) end
    for k, x in pairs(extra or {}) do t[k] = x end
    ui.save_prefs(spec.prefs, t)
  end

  -- a settings page: every row by group, a header per group, in the group's column
  function M.items_for_groups(extra_after)
    local it = {}
    for _, g in ipairs(spec.groups or {}) do
      local col = (spec.group_col or {})[g] or 1
      it[#it + 1] = { kind = "header", col = col, label = (spec.group_label or {})[g] or g }
      for _, r in ipairs(M.rows) do
        if r.group == g then
          local dim = r.dim and r.dim(M) or false
          it[#it + 1] = { kind = "choice", col = col, label = r.label, row = r.id, desc = r.desc, dim = dim and 1 or 0 }
        end
      end
      if extra_after and extra_after[g] then for _, x in ipairs(extra_after[g]) do x.col = x.col or col; it[#it + 1] = x end end
    end
    return it
  end

  local function selectable(i) local it = M.items[i]; return it and it.kind ~= "header" and (it.dim or 0) == 0 end
  M.selectable = selectable
  local function fix_cursor(dir)
    local n = #M.items
    if n == 0 then M.cursor = 0; return end
    if M.cursor < 1 or M.cursor > n then M.cursor = 1 end
    for _ = 1, n do
      if selectable(M.cursor) then return end
      M.cursor = (M.cursor - 1 + (dir or 1)) % n + 1
    end
  end
  function M.find(pred) for i, it in ipairs(M.items) do if pred(it) then return i end end end
  function M.find_act(act) return M.find(function(it) return it.act == act end) end

  local cur_view
  local function build()
    cur_view = spec.pages[M.page](M) or {}
    M.items = cur_view.items or {}
    for _, it in ipairs(M.items) do
      if it.kind == "choice" and it.row and it.value == nil then it.value = M.label(it.row) end
      if type(it.dim) == "boolean" then it.dim = it.dim and 1 or 0 end
      if type(it.chosen) == "boolean" then it.chosen = it.chosen and 1 or 0 end
    end
  end

  function M.show()
    build()
    fix_cursor(1)
    local t = cur_view
    t.layout = t.layout or "lobby"
    t.page = M.page_ids[M.page] or 1
    t.cursor = M.cursor
    t.items = M.items
    t.footer = t.footer or spec.footer
    local sel = M.items[M.cursor]
    t.desc = t.desc or (sel and sel.desc) or ""
    if ui then ui.view(t) end
    M.dirty = false
  end

  function M.go(page, cursor)
    if page ~= M.page then
      M.last[M.page] = M.cursor
      M.page = page
      build()
      M.cursor = cursor or M.last[page] or 0
      if M.cursor == 0 then
        local want = cur_view.cursor_act and M.find_act(cur_view.cursor_act)
        M.cursor = want or 1
      end
      sfx("page")
      log("page %s", page)
    elseif cursor then M.cursor = cursor end
    M.dirty = true
  end

  local function activate(it, ev)
    if not it then return end
    if it.kind == "choice" and it.row then M.change(it.row, (ev == BACK) and -1 or 1); return end
    local a = it.act
    if not a then return end
    local pg = a:match("^page:(.+)$")
    if pg then sfx("select"); M.go(pg); return end
    if spec.act then spec.act(M, a, it) end
  end
  M.activate = activate

  local function move(d)
    local n = #M.items
    if n == 0 then return end
    local c = M.cursor
    for _ = 1, n do
      c = (c - 1 + d) % n + 1
      if selectable(c) then break end
    end
    if c ~= M.cursor then M.cursor = c; sfx("move"); M.dirty = true end
  end

  function M.step(ev)
    if M.busy then return end
    if ev & UP ~= 0 then move(-1) end
    if ev & DOWN ~= 0 then move(1) end
    local it = M.items[M.cursor]
    if ev & (LEFT | RIGHT) ~= 0 then
      local d = (ev & LEFT ~= 0) and -1 or 1
      local layout = cur_view and cur_view.layout or "lobby"
      if it and it.kind == "choice" and it.row then M.change(it.row, d)
      elseif layout == "debrief" then move(d)
      elseif layout == "lobby" then   -- across the columns: the lists <-> the right-hand column (the primary button)
        if it and it.col == 2 and d < 0 then M.cursor = M.last.col1 or 1; sfx("move"); M.dirty = true
        elseif it and it.col ~= 2 and d > 0 then
          local r = M.find(function(x) return x.col == 2 and x.kind ~= "header" and (x.dim or 0) == 0 end)
          if r then M.last.col1 = M.cursor; M.cursor = r; sfx("move"); M.dirty = true end
        end
      end
    end
    it = M.items[M.cursor]
    if ev & (SELECT | START) ~= 0 then activate(it, SELECT) end
    if ev & BACK ~= 0 then
      local to = (spec.back or {})[M.page]
      if to then sfx("back"); M.go(to)
      else
        local q = M.find_act("quit")
        if q and q ~= M.cursor then M.cursor = q; sfx("back"); M.dirty = true end
      end
    end
  end

  function M.point(a, b)
    if M.busy then return end
    local page, i = a // 64, a % 64
    if b == 4 then M.step(UP); return end
    if b == 5 then M.step(DOWN); return end
    if page ~= M.page_ids[M.page] then return end
    if b == 2 and i == 0 then M.step(BACK); return end
    if i < 1 or i > #M.items or not selectable(i) then return end
    if M.cursor ~= i then M.cursor = i; M.dirty = true; if b == 6 then sfx("move") end end
    if b == 1 then activate(M.items[i], SELECT)
    elseif b == 2 then
      if M.items[i].kind == "choice" then activate(M.items[i], BACK) else M.step(BACK) end
    end
  end

  -- a board event as if pressed (tests)
  function M.press(mask) if plan and plan.held() then test.plan_input(0, mask) end end

  function M.tick(dt)
    M.t = M.t + dt
    M.quiet = M.quiet - dt
    if plan then
      local ev = plan.input(0)
      while ev ~= 0 do
        if M.quiet <= 0 then M.step(ev) end
        ev = plan.input(0)
      end
      local a, b = plan.pointer(0)
      while a do
        if M.quiet <= 0 then M.point(a, b) end
        a, b = plan.pointer(0)
      end
      for p = 1, bond.count() - 1 do   -- (the partners' buttons are held, and do nothing here)
        while plan.input(p) ~= 0 do end
        while plan.pointer(p) do end
      end
    end
    if M.dirty then M.show() end
  end

  function M.state_text()
    local parts = {}
    for _, r in ipairs(M.rows) do parts[#parts + 1] = tostring(M.v[r.id]) end
    return string.format("UIMENU %s %s %d %s", spec.name or "ui", M.page, M.cursor, table.concat(parts, ","))
  end

  return M
end
