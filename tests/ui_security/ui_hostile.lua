-- A HOSTILE mode script against the generic menu layer (tests/ui_security/run.sh copies this to Data/Scripts/modes/ui_hostile.lua;
-- GE_MODE=ui_hostile). Mode scripts include third-party mods, so everything a script hands to C must be checked there:
--   stage 1: preference files: a "../" name, an over-long name, a name that would be another file (midas, arena_menu: they must land in
--            the layer's own Data/Saves/ui folder), keys and values with CR / LF / control characters; then a launch with a bad mode
--            name, then a launch whose GE_* variables try GE_UIMENU_DIR, GE_LUA_TEST, GE_MODS, GE_ROMDATA, GE_PROFILE, GE_NOMENUSAVE, a
--            replay file for the puppet and a seed with a shell character, beside one good GE_LEVELGEN
--   stage 2: none of the refused variables reached the environment; ui.current().env holds GE_LEVELGEN only; quit
-- Prints "UISEC PASS|FAIL <what>" and "UISEC DONE <n> passed, <m> failed".
local pass, fail = 0, 0
local function check(what, ok, extra)
  if ok then pass = pass + 1 else fail = fail + 1 end
  print(string.format("ge: UISEC %s %s %s", ok and "PASS" or "FAIL", what, extra or ""))
end
local cur = ui.current()
include("_ui_menu")

if not cur then
  -- the table texts that cross stages (ui.result, a launch's settings) are parsed as data only
  local P = UI_MENU.parse
  check("parse: a method on a string literal is refused", P('{a=("x"):rep(100000000)}') == nil)
  check("parse: a called function constructor is refused", P('{a=(function() while true do end end)()}') == nil)
  check("parse: a function value is refused", P('{f=function() end}') == nil)
  check("parse: a long string is refused", P('{a=[[x]]}') == nil)
  local rt = P(UI_MENU.serialize({ ["FLOOR KEY"] = "lifted", ["end"] = 1, good = 2, s = "a(b):c function [[x]]", list = { 1, 2 } }))
  check("parse: plain data round-trips (odd keys, parentheses and keywords inside strings)",
    rt ~= nil and rt["FLOOR KEY"] == "lifted" and rt["end"] == 1 and rt.good == 2 and rt.s == "a(b):c function [[x]]" and rt.list[2] == 2)
  check("a '../' file name is refused", ui.save_prefs("../evil", { a = "1" }) == false)
  check("a '..' name to read is empty", next(ui.prefs("../../midas")) == nil)
  check("an over-long file name is refused", ui.save_prefs(string.rep("a", 40), { a = "1" }) == false)
  check("a capital / dotted name is refused", ui.save_prefs("Midas.ini", { a = "1" }) == false)
  ui.save_prefs("midas", { harmless = "1" })        -- (allowed: it must land in Data/Saves/ui/midas.ini, never midas.ini; run.sh checks)
  ui.save_prefs("arena_menu", { harmless = "1" })   -- (likewise: Data/Saves/ui/arena_menu.ini, never the arena's file)
  local ok = ui.save_prefs("hostile", { good = "ok", bad = "x\nevil=1", cr = "a\rb", tab = "a\tb", ["k\nj"] = "v", ["k=j"] = "v", nul = "a\0b" })
  check("a save with bad keys and values reports the refusal", ok == false)
  local p = ui.prefs("hostile")
  check("the good line is kept", p.good == "ok", tostring(p.good))
  check("no value with a newline, CR or tab is kept", p.bad == nil and p.cr == nil and p.tab == nil)
  check("a NUL ends the value (nothing after it is kept)", p.nul == nil or p.nul == "a", tostring(p.nul))
  check("no injected line", p.evil == nil)
  check("no key with a newline or '='", p["k\nj"] == nil and p["k=j"] == nil and p.k == nil and p.j == nil)
  local t = 0
  local stage = 0
  on("tick", function(dt)
    t = t + dt
    if stage == 0 and t >= 1 then
      stage = 1
      check("a launch with a path for a mode is refused", ui.launch({ mode = "../../evil" }) == false)
      check("a launch with a dotted mode is refused", ui.launch({ mode = "heist.lua" }) == false)
      local launched = ui.launch({ mode = "ui_hostile", lobby = "stage2", difficulty = 99, env = {
        GE_UIMENU_DIR = "/tmp/pwn", GE_LUA_TEST = "/tmp/pwn.lua", GE_MODS = "x", GE_ROMDATA = "0", GE_PROFILE = "pwn", GE_NOMENUSAVE = "1",
        GE_COOP_P2 = "replay:/etc/passwd", GE_SEED = "7;rm", GE_LEVELGEN = "bank" } })
      check("the launch itself goes (its bad variables dropped)", launched == true)
    end
  end)
else
  local env = cur.env or {}
  local keys = {}
  for k in pairs(env) do keys[#keys + 1] = k end
  table.sort(keys)
  check("only the allowed variable reached the next stage", #keys == 1 and env.GE_LEVELGEN == "bank", table.concat(keys, ","))
  check("GE_UIMENU_DIR not set", not mode.env_flag("GE_UIMENU_DIR"))
  check("GE_LUA_TEST not set", not mode.env_flag("GE_LUA_TEST"))
  check("GE_MODS not set", not mode.env_flag("GE_MODS"))
  check("GE_PROFILE not set", not mode.env_flag("GE_PROFILE"))
  check("GE_NOMENUSAVE not set", not mode.env_flag("GE_NOMENUSAVE"))
  check("GE_COOP_P2 not set (no replay file)", not mode.env_flag("GE_COOP_P2"))
  check("GE_SEED kept as it was (7)", cur.env.GE_SEED == nil)
  check("the difficulty clamped (99 -> left as it was)", cur.difficulty == nil)
  print(string.format("ge: UISEC DONE %d passed, %d failed", pass, fail))
  local t = 0
  local quit = false
  on("tick", function(dt) t = t + dt; if not quit and t >= 1 then quit = true; ui.quit() end end)
end
if not cur then print(string.format("ge: UISEC STAGE1 %d passed, %d failed", pass, fail)) end
