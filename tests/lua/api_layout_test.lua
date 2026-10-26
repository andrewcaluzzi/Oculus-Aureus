-- The Lua API's layout after the 2026-10-27 clean-up (modding/lua.md "Conventions" and "Renamed and removed"). Not shipped.
-- Run: GE_LUA_TEST=tests/lua/api_layout_test.lua GE_STAGE=dam GE_SKIPINTRO=1 GE_FRAMES=200 ./ProjectMidas | grep APITEST
-- Ends with "APITEST DONE <n> passed, <m> failed". Checks names only (the argument orders are covered by the heist suites:
-- heist_cover, heist_gadgets, heist_traits).
local pass, fail = 0, 0
local function check(what, ok)
  if ok then pass = pass + 1 else fail = fail + 1; print("APITEST FAIL " .. what) end
end

-- every test hook lives in the test table
local TEST = { "heard", "fire_stats", "gadget_used", "face", "shot_noise", "slap", "obj_damage", "cctv_info", "press", "screenshot",
  "use", "hud_panel", "hud_line", "rooms_drawn", "take_drop", "walk", "walk_stop", "fire", "intel_count", "intel_show", "intel_view",
  "plan_input", "plan_mouse", "plan_count", "plan_tabs", "has_item", "crosshair_draws", "crosshair_hit", "debrief_open",
  "debrief_record_info", "debrief_path", "grab", "chr_face", "freeze" }
check("test is a table", type(test) == "table")
for _, n in ipairs(TEST) do check("test." .. n, type(test[n]) == "function") end
local count = 0
for _ in pairs(test) do count = count + 1 end
check("test has exactly " .. #TEST .. " functions (has " .. count .. ")", count == #TEST)

-- the old names are gone
local GONE = { spawn = { "heard", "fire_stats" }, bond = { "test_gadget", "face", "test_noise", "rooms_drawn", "take_drop", "walk", "walk_stop" },
  ko = { "test_slap" }, obj = { "test_damage" }, cover = { "test_act" }, cctv = { "render_info", "test_press", "test_shot", "test_use" },
  hud = { "panel_lines", "line" }, gadget = { "test_fire" }, intel = { "count", "test_show", "view" },
  plan = { "test_input", "test_mouse", "count", "tabs", "has" }, crosshair = { "draws", "test_hit" },
  debrief = { "is_open", "record_info", "records_path" }, shield = { "grab", "face" }, diag = { "freeze" } }
for lib, names in pairs(GONE) do
  for _, n in ipairs(names) do check(lib .. "." .. n .. " is gone", _G[lib][n] == nil) end
end

-- production names that stay, or were renamed in place
local KEEP = { cover = { "act", "blow", "suspicion" }, ai = { "trait_bribe", "trait_observe" }, bond = { "warp", "ammo" },
  spawn = { "kill" }, plan = { "view", "hit", "pointer", "strip" }, intel = { "briefing" }, shield = { "release", "last" },
  diag = { "on", "off", "text" } }
for lib, names in pairs(KEEP) do
  for _, n in ipairs(names) do check(lib .. "." .. n .. " is there", type(_G[lib][n]) == "function") end
end

-- the unknown-event error lists every event (it was a hand-written list that had gone stale)
local ok, err = pcall(on, "no_such_event", function() end)
check("on() refuses an unknown event", not ok)
for _, ev in ipairs({ "guard_bark", "noise", "chr_knocked_out", "cover_report", "draw_crosshair" }) do
  check("the error names " .. ev, type(err) == "string" and err:find(ev, 1, true) ~= nil)
end

print(string.format("APITEST DONE %d passed, %d failed", pass, fail))
