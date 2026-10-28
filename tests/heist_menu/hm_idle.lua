-- AC's playtest (2026-10-28): sitting in the heist start menu for a while raised the alarm. The lobby must freeze the job: nothing may
-- see Bond, no alarm, no camera spot, no damage, the people behind the menu stay as they are. tests/heist_menu/run_idle.sh copies this to
-- Data/Scripts/zz_hm_idle.lua and boots the lobby (GE_MODE=heist-menu) with nobody touching it for IDLE seconds of sim time (default 330).
-- Prints "IDLE PASS|FAIL ..." and "IDLE DONE <n> passed, <m> failed", then exits.
if mode.name() ~= "heist-menu" then return end
local IDLE = 330
local alarms, spots, hurt, t = 0, 0, 0, 0
local first = nil
local pass, fail = 0, 0
local function check(what, ok, extra)
  if ok then pass = pass + 1 else fail = fail + 1 end
  print(string.format("ge: IDLE %s %s %s", ok and "PASS" or "FAIL", what, extra or ""))
end
on("alarm", function(a) if a then alarms = alarms + 1; print(string.format("ge: IDLE alarm at %.1f s", t)) end end)
on("cctv_spotted", function(cam, p) spots = spots + 1; if spots <= 3 then print(string.format("ge: IDLE camera %s spotted Bond at %.1f s", tostring(cam), t)) end end)
on("damage", function(victim) hurt = hurt + 1 end)
local function snapshot()
  local s = {}
  for i, e in ipairs((HEIST_MENU and HEIST_MENU.extras) or {}) do
    local x, z, act = spawn.where(e.chr)
    local st = ai and ai.awareness and ai.awareness(e.chr) or nil
    s[i] = { x = x, z = z, act = act, aware = st }
  end
  return s
end
local done = false
on("tick", function(dt)
  t = t + dt
  if not first and t >= 2 then first = snapshot(); print(string.format("ge: IDLE watching %d people behind the menu", #first)) end
  if not done and t >= IDLE then
    done = true
    local now = snapshot()
    check("no alarm in " .. IDLE .. " s of the menu", alarms == 0 and not alarm.active(), string.format("alarm events %d, active %s", alarms, tostring(alarm.active())))
    check("no camera spotted Bond", spots == 0, tostring(spots))
    check("nobody was hurt", hurt == 0, tostring(hurt))
    local moved, aware = 0, 0
    for i, a in ipairs(first or {}) do
      local b = now[i]
      if not b.x or math.abs(b.x - a.x) > 5 or math.abs(b.z - a.z) > 5 then moved = moved + 1 end
      if (b.aware or 0) > 0 then aware = aware + 1 end
    end
    check("the people behind the menu stayed put", #(first or {}) > 0 and moved == 0, string.format("%d of %d moved", moved, #(first or {})))
    check("nobody became aware of Bond", aware == 0, tostring(aware))
    check("the menu is still up", (ui.state()) == true)
    print(string.format("ge: IDLE DONE %d passed, %d failed", pass, fail))
    if bench and bench.exit then bench.exit() end
  end
end)
