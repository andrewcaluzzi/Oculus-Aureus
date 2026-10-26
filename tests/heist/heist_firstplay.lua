-- First-play screenshots and the guide's checks (2026-10-26 polish, designs/bank-heist.md section 23d): what a brand-new player sees in
-- the first minute of the real heist on the compiled bank. Bond is left where the mode puts him (the street), then warped to the hall.
-- Run: tests/heist/run_firstplay.sh (container, Xvfb :99). Each shot follows a "HEIST SHOT <name>" line, with the HUD lines and the
-- panel printed so the log alone shows the text. Ends with "HEIST DONE <n> passed, <m> failed".
include("heist")

local pass, fail = 0, 0
local function check(name, ok, extra)
  if ok then pass = pass + 1 else fail = fail + 1 end
  print(string.format("HEIST %s %s %s", ok and "PASS" or "FAIL", name, extra or ""))
end
local function wait(sec)
  local t = 0
  while t < sec do t = t + coroutine.yield() end
end
local function shot(name)
  print("HEIST SHOT " .. name .. string.format(" t=%.1f phase=%s", HEIST.elapsed, HEIST.phase))
  for n = 1, 3 do print(string.format("HEIST HUD %d: %s", n, hud.line(n, 0) or "")) end
  for i, l in ipairs(hud.panel_lines() or {}) do print(string.format("HEIST PANEL %d: %s", i, l)) end
  cctv.test_shot()
  wait(0.3)
end

local function body()
  while not HEIST.ready do wait(0.25) end
  wait(2)
  shot("start")                                    -- the street, a few seconds in
  check("guide: line 1 names the INTEL key", hud.line(1, 0):find("TAB: INTEL") ~= nil, hud.line(1, 0))
  check("guide: outside, the next step is the front door", hud.line(2, 0):find("front door") ~= nil, hud.line(2, 0))
  check("guide: the opening line fits the HUD", #hud.line(3, 0) <= 63 and hud.line(3, 0):find("Steal the gold") ~= nil, hud.line(3, 0))
  wait(8)
  check("guide: the key strip shows the INTEL key once the opening line is gone", hud.line(3, 0):find("INTEL board") ~= nil, hud.line(3, 0))
  wait(6)
  shot("t20")                                      -- about 20 s in
  intel.test_show(true, 1, 0)
  wait(1)
  shot("board_plan")                               -- the INTEL board's first page
  intel.test_show(false, 1, 0)
  bond.warp(0, LEVEL.pads["hall_entry_e"])          -- just inside the hall
  bond.face(0, 300, 600)
  wait(3)
  check("guide: inside, the next step is the GREEN card", hud.line(2, 0):find("GREEN card") ~= nil, hud.line(2, 0))
  shot("hall")
  -- the GREEN carrier knocked out next to Bond: the prompt says to search him
  local c = HEIST.carriers["lobby captain"] or HEIST.carriers["head teller"]
  if c and c.chr then
    mode.guards_wake(false)                        -- (a hall guard waking her is heist_ko's business)
    -- (the cover's warnings win HUD line 3 by design, and in the busy hall a guard may see through Bond loitering by the body: GE_SEED
    -- 3, 6, 9, 10 had "HEY! Seen through (loitering)" there. The prompt is this check's business, the cover is heist_cover's)
    cover.on(false)
    local r = spawn.ko(c.chr, 0)
    print(string.format("HEIST   spawn.ko -> %s, ko.is %s", tostring(r), tostring(ko.is(c.chr))))
    wait(4.5)
    local x, z = spawn.where(c.chr)
    local best, bd = nil, 1e9
    for _, pd in ipairs(pads.far_from(x, z, 0, 600)) do
      local px, _, pz = pads.pos(pd)
      local d = (px - x) ^ 2 + (pz - z) ^ 2
      if d < bd then best, bd = pd, d end
    end
    if best then local px, _, pz = pads.pos(best); bond.warp(0, best, x - px + 60, z - pz) end
    wait(1)
    do local alive, held = spawn.key_alive(c.key); local bx, _, bz = bond.pos(0)
       print(string.format("HEIST   carrier ko=%s key alive=%s held=%s, Bond %.0f from him", tostring(ko.is(c.chr)), tostring(alive), tostring(held),
             math.sqrt((bx - x) ^ 2 + (bz - z) ^ 2))) end
    check("guide: by a knocked-out carrier, the prompt says to search him", hud.line(3, 0):find("USE to search") ~= nil, hud.line(3, 0))
    shot("ko_prompt")
  end
  print(string.format("HEIST DONE %d passed, %d failed", pass, fail))
  mission.fail("shots done")
end

local co = coroutine.wrap(body)
local finished = false
on("tick", function(dt)
  if finished then return end
  local ok, err = pcall(co, dt)
  if not ok then finished = true; print("HEIST firstplay error: " .. tostring(err)) end
end)
