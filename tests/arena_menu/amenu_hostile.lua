-- A hostile mode script against the arena menu's C side and its table parser (v0.2 hardening; the heist-ui layer's 230e2bc is the
-- model). Run by tests/arena_menu/run_hostile.sh as GE_MODE=amenu-hostile on the Colosseum; every check logs
-- "AMHOSTILE PASS <what>" or "AMHOSTILE FAIL <what>", and the shell checks the preferences file it wrote.
include("_arena_theme")
include("_arena_menu_defs")
local D = ARENA_MENU_DEFS
local function log(ok, what) print(string.format("ge: arena: AMHOSTILE %s %s", ok and "PASS" or "FAIL", what)) end

local done = false
on("tick", function()
  if done then return end
  done = true

  -- 1. INI injection: a newline (or any control character) in a value must never reach the file; good values still do
  local ok = amenu.save_prefs({ good = "SPRINT 10", evil = "x\nGE_MODE=evil\n[arena_menu]", tab = "a\tb", cr = "c\rd",
                                hi = "caf\195\169", long = string.rep("v", 200), ["bad key"] = "1" })
  log(ok == false, "save_prefs reports the refusals (false)")
  local p = amenu.prefs()
  log(p.good == "SPRINT 10", "a good value is kept")
  log(p.evil == nil and p.tab == nil and p.cr == nil and p.hi == nil and p.long == nil, "values with control / non-ASCII / too long refused")
  log(p.GE_MODE == nil, "no injected key")

  -- 2. the table parser: data only
  local t0 = os and os.clock and os.clock() or 0
  local bombs = {
    rep = '("x"):rep(1e9)',
    rep2 = '{a=("x"):rep(2^30)}',
    fn = '{a=function() while true do end end}',
    loop = '{a=(function() while true do end end)()}',
    long = '{a=[[x]]}',
    long2 = '{a=[==[x]==]}',
    method = '{a=""  :rep(9)}',
  }
  for name, text in pairs(bombs) do
    amenu.result(text)
    log(D.parse(amenu.result()) == nil, "parser refuses " .. name)
  end
  local dt = (os and os.clock and os.clock() or 0) - t0
  log(dt < 2.0, string.format("the refusals took %.3f s (no 1 GB string, no endless loop)", dt))
  -- plain data still parses, including '(' ':' and words inside string literals and a bracketed key, and round-trips
  local good = { waves = 10, label = "function (x): y [[z]]", list = { 1, 2.5, "a" }, ["FLOOR KEY"] = 3, flag = true, neg = -4 }
  local text = D.serialize(good)
  local back = D.parse(text)
  log(back ~= nil and back.waves == 10 and back.label == good.label and back["FLOOR KEY"] == 3 and back.list[2] == 2.5 and back.neg == -4,
    "plain data parses and round-trips: " .. text)
  log(D.parse('{ waves = 10, mutators = true, whacky = 1, mode = "fixed" }') ~= nil, "a menu settings text parses")

  -- 3. the hand-off's mode name: only what the mode loader accepts
  amenu.arm({ mode = "../../Data/Saves/x", lobby = "menu" })
  amenu.retry({ mode = "arena;rm -rf", label = "evil" })
  local r = amenu.retry()
  log(r == nil or r.mode ~= "arena;rm -rf", "retry with a bad mode is refused")
  log(amenu.launch({ mode = "arena chaos" }) == false, "launch with a space in the mode is refused")
  log(amenu.launch({ mode = "../arena" }) == false, "launch with a path is refused")
  log(amenu.launch({ mode = "arena\n" }) == false, "launch with a newline is refused")
  amenu.retry({ mode = "arena-chaos", label = "fine", slots = 999999, coop = true })
  r = amenu.retry()
  log(r ~= nil and r.mode == "arena-chaos" and r.slots == nil, "a good retry is kept, its slots clamped")
  print("ge: arena: AMHOSTILE done")
end)
