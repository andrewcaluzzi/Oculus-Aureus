-- Crosshair styles (modding/lua.md "Crosshair styles"): the crosshair.* API, the pack, the draw hook, and one screenshot per pack
-- style ("XHAIR SHOT <name>" lines, test.screenshot). Run: tests/crosshair/run.sh api (headless; a mode on Dam, so no guards).
-- Prints "XHAIR PASS|FAIL ..." and "XHAIR DONE <n> passed, <m> failed".
local SHOTS = mode.env_flag("GE_XHAIR_SHOTS")
local pass, fail = 0, 0
local function check(name, ok, extra)
  if ok then pass = pass + 1 else fail = fail + 1 end
  print(string.format("XHAIR %s %s %s", ok and "PASS" or "FAIL", name, extra or ""))
end

-- at load: the API without a stage
local shapes = crosshair.shapes()
check("shapes", #shapes == 9, "n=" .. #shapes)
local names = crosshair.list()
check("pack_list", #names >= 6, table.concat(names, ","))
check("starts_classic", crosshair.current() == "classic")
check("bad_shape_raises", not pcall(crosshair.style, { shape = "bogus" }))
check("bad_weapon_raises", not pcall(crosshair.style, { shape = "dot", weapons = { nosuchgun = { size = 2 } } }))
check("bad_colour_raises", not pcall(crosshair.style, { color = "plaid" }))
check("unknown_pack_style", crosshair.use("nosuchstyle") == false)

local hookCalls, hookOk = 0, true
local frames = 0
local steps = {}
local function shot(name)
  if SHOTS then print("XHAIR SHOT " .. name); test.screenshot() end
end

on("stage_start", function()
  on("draw_crosshair", function(x, y, ctx)
    hookCalls = hookCalls + 1
    if type(x) ~= "number" or type(y) ~= "number" or type(ctx.spread) ~= "number" or type(ctx.line) ~= "function" then hookOk = false end
  end)
  -- the schedule, one step per 0.5 s tick pair
  for _, n in ipairs(names) do
    steps[#steps + 1] = function() crosshair.use(n) end
    steps[#steps + 1] = function() shot(n) end
  end
  steps[#steps + 1] = function() crosshair.use("cross"); test.crosshair_hit(true) end
  steps[#steps + 1] = function() test.crosshair_hit(true); shot("cross_headshot_marker") end
  steps[#steps + 1] = function()
    local d0 = test.crosshair_draws()
    check("draws_counted", d0 > 0, "draws=" .. d0)
    check("hook_called", hookCalls > 0 and hookOk, "calls=" .. hookCalls)
    check("current_player", select(2, crosshair.current()) == "player" and crosshair.current() == "cross")
    check("spread_number", type(crosshair.spread()) == "number" and crosshair.spread() >= 0, string.format("%.2f", crosshair.spread()))
    crosshair.style({ shape = "x", color = "magenta", size = 3 })
    local s, who = crosshair.current()
    check("script_wins", s == "x" and who == "script", tostring(s) .. "/" .. tostring(who))
    crosshair.hide(true)
    steps.hiddenAt = test.crosshair_draws()
  end
  steps[#steps + 1] = function()
    check("hide_stops_drawing", test.crosshair_draws() == steps.hiddenAt and crosshair.hidden(), "draws=" .. test.crosshair_draws())
    crosshair.reset()
    check("reset", crosshair.current() == "cross" and not crosshair.hidden())
    crosshair.use("off")
    check("use_off", crosshair.current() == "classic")
    -- a draw function in a script style
    local drew = 0
    crosshair.style({ shape = "none", draw = function(x, y, ctx) drew = drew + 1; ctx.circle(x, y, 4, 0.5, { 255, 255, 0, 200 }) end })
    steps.drew = function() return drew end
  end
  steps[#steps + 1] = function()
    check("style_draw_fn", steps.drew() > 0, "calls=" .. steps.drew())
    crosshair.style(nil)
    print(string.format("XHAIR DONE %d passed, %d failed", pass, fail))
  end
  bond.heal(0, 1, 1)
end)

local acc, i = 0, 0
on("tick", function(dt)
  bond.heal(0, 1, 1)
  acc = acc + dt
  if acc < 0.5 then return end
  acc = 0
  i = i + 1
  if steps[i] then steps[i]() end
end)
