-- The heist DEBRIEF (designs/bank-heist.md section 26, tools/heist_debrief.lua) on the bank with the real tools/mode_heist.lua.
-- Two scenarios (tests/heist/run_debrief.sh runs both, the records file deleted first):
--   default              a quiet job: the security chief's BLUE card lifted from behind (pickpocket), the cameras blinded at the
--                        console, the crack, three bars, out of the vault before the time lock, the van. Expect a high rating
--                        (GHOST / PROFESSIONAL), no alarm, the quiet commendations, BLUE "pickpocket", the payout = the mode's score,
--                        NEW BEST on a fresh records file; one screenshot of the panel ("HEIST SHOT debrief")
--   GE_DEBRIEF_LOUD=1    a loud job: the alarm up from the start, a civilian killed, the crack, the bars, the van. Expect a lower
--                        rating, the alarm counted, no quiet commendations, the records file's run count up, no NEW BEST
-- Prints "HEIST PASS|FAIL ..." and "HEIST DONE n passed, m failed"; the mode itself prints "HEIST DEBRIEF {json}" (the shell parses it).
local LOUD = mode.env_flag("GE_DEBRIEF_LOUD")
HEIST_SETTINGS = {
  test = true,
  cover = false,   -- (as the win path's quiet run: the test warps an armed Bond through restricted rooms)
  crack_time = 12, jam_chance = 0, jam_every = 1000, vault_time = 14, klaxon_at = 5,
}
include("heist")
mods.set("invincible", true)
on("stage_start", function() mode.guards_wake(false); mode.body_check(false) end)

local pass, fail = 0, 0
local function check(name, ok, extra)
  if ok then pass = pass + 1 else fail = fail + 1 end
  print(string.format("HEIST %s %s %s", ok and "PASS" or "FAIL", name, extra or ""))
end
local ended = {}
local real_win, real_fail = mission.win, mission.fail
mission.win = function(msg) ended.win = msg; return true end   -- (the stage runs on, so the panel can be photographed)
mission.fail = function(msg) ended.fail = msg; return true end
local R
local real_end = HEIST.debrief_end
HEIST.debrief_end = function(...) R = real_end(...); return R end

local function wait(sec)
  local t = 0
  while t < sec do t = t + coroutine.yield(); bond.heal(0, 1, 1) end
end
local function wait_for(sec, fn)
  local t = 0
  while t < sec and not fn() do wait(0.25); t = t + 0.25 end
  return fn(), t
end
local function d2(ax, az, bx, bz) return math.sqrt((ax - bx) ^ 2 + (az - bz) ^ 2) end
local function warp_xz(x, z)
  local best, bd = nil, 1e9
  for _, p in ipairs(pads.far_from(x, z, 0, 900)) do
    local px, _, pz = pads.pos(p)
    if d2(px, pz, x, z) < bd then best, bd = p, d2(px, pz, x, z) end
  end
  if not best then return false end
  local px, _, pz = pads.pos(best)
  return bond.warp(0, best, x - px, z - pz)
end
local function has(list, name) for _, v in ipairs(list or {}) do if v == name then return true end end return false end
local function grab(maxn)
  local got = 0
  for _, it in ipairs(HEIST.loot_items) do
    if it.where == "vault" and spawn.item_alive(it.h) and got < maxn then
      bond.warp(0, it.pad, it.dx, it.dz)
      wait(0.5)
      if not spawn.item_alive(it.h) then got = got + 1 end
    end
  end
  return got
end

local function job()
  local W = HEIST.M.P
  if LOUD then
    alarm.start()
    wait(1)
    check("loud: the alarm is up (hot)", HEIST.hot)
    local civ
    for _, c in ipairs(HEIST.civs) do if spawn.alive(c.chr) and c.area == "hall" then civ = c.chr; break end end
    if civ then spawn.kill(civ) end
    wait(1)
    check("loud: a civilian is dead", civilian.killed() == 1, tostring(civilian.killed()))
  else
    -- the pickpocket: behind the security chief (as tests/heist/heist_gadgets.lua "pocket")
    local chief = HEIST.carriers["security chief"]
    local c = chief and chief.chr
    if c then
      local cx, cz = spawn.where(c)
      local yaw = gadget.chr_yaw(c)
      warp_xz(cx - math.sin(math.rad(yaw)) * 85, cz - math.cos(math.rad(yaw)) * 85)
      bond.face(0, cx, cz)
      wait(0.5)
      bond.face(0, cx, cz)
      cctv.test_use(0)
      check("quiet: the BLUE card lifted from behind", wait_for(1.5, function() return bond.has_key(0, 0x4) end))
    end
    HEIST.camera_switch(0)
  end
  bond.warp(0, W.crack)
  wait(0.5)
  bond.test_gadget(0, "doordecoder")
  check("the vault opens", wait_for(30, function() return HEIST.phase == "open" end), HEIST.phase)
  check("bars grabbed", grab(3) >= 3, "bars " .. HEIST.bars)
  bond.warp(0, W.outside)
  wait(0.5)
  wait_for(20, function() return HEIST.phase == "sealed" end)
  wait(0.5)
  bond.warp(0, W.exit_a)
  check("the crew gets out (win)", wait_for(4, function() return ended.win ~= nil end), tostring(ended.win))
end

local finished = false
local function body()
  local ready = wait_for(20, function() return HEIST.ready end)
  check("mode ready", ready)
  wait(1.5)
  local _, runs0 = debrief.record_info(0)   -- (tests only: one machine's file)
  job()
  local new_best, runs1, _, best, saved = debrief.record_info(0)
  check("the debrief ran at the end", R ~= nil)
  if R then
    print(string.format("HEIST   rating %s, payout %d, alarm %s, spotted %d, commendations: %s", R.rating, R.payout, tostring(R.alarm),
          R.spotted, table.concat(R.commendations, ", ")))
    check("the payout is the mode's score", R.payout == HEIST.score and R.payout > 0, string.format("%d vs %s", R.payout, tostring(HEIST.score)))
    check("the stats: the bars and the time", R.bars == HEIST.bars and R.time > 10, string.format("bars %d, time %.1f", R.bars, R.time))
    check("the route ends at the van", R.route[#R.route] == "Van" and R.exit == "Van", table.concat(R.route, " > "))
    if LOUD then
      check("loud: a low rating (COWBOY / CHAOS)", R.rating == "CHAOS" or R.rating == "COWBOY", R.rating)
      check("loud: the alarm is counted", R.alarm == true and R.alarms == 1 and R.alarm_at ~= nil, tostring(R.alarm_at))
      check("loud: the civilian death is counted and fined", R.civ_killed == 1 and R.fines == 100000, string.format("%d, %d", R.civ_killed, R.fines))
      check("loud: no quiet commendations", not has(R.commendations, "Quiet Job") and not has(R.commendations, "Never Seen")
            and not has(R.commendations, "No Bodies") and not has(R.commendations, "Clean Hands"), table.concat(R.commendations, ","))
      check("loud: no NEW BEST (the quiet run paid more)", new_best == false or runs0 == 0, tostring(new_best))
    else
      check("quiet: a high rating (GHOST / PROFESSIONAL)", R.rating == "GHOST" or R.rating == "PROFESSIONAL", R.rating)
      check("quiet: no alarm, never spotted", R.alarm == false and R.alarms == 0 and R.spotted == 0, string.format("spotted %d", R.spotted))
      check("quiet: the quiet commendations (Never Seen, No Bodies, Quiet Job, Clean Hands)", has(R.commendations, "Never Seen")
            and has(R.commendations, "No Bodies") and has(R.commendations, "Quiet Job") and has(R.commendations, "Clean Hands"),
            table.concat(R.commendations, ","))
      check("quiet: Pickpocket and Blind Spot (the console)", has(R.commendations, "Pickpocket") and has(R.commendations, "Blind Spot"),
            table.concat(R.commendations, ","))
      check("quiet: BLUE got by pickpocket", R.keys.BLUE == "pickpocket", tostring(R.keys.BLUE))
      check("quiet: Speedrun (under 6:00 on Agent)", has(R.commendations, "Speedrun"), string.format("%.0f s", R.time))
      check("quiet: the quiet bonus is in the payout", R.quiet_bonus > 0 and R.payout == R.loot + R.quiet_bonus, tostring(R.quiet_bonus))
      check("quiet: NEW BEST on a fresh records file", runs0 > 0 or new_best == true, tostring(new_best))
    end
    check("the records file is written and counts the run", saved and runs1 == runs0 + 1 and best > 0,
          string.format("%s: runs %d -> %d, best %.0f", debrief.records_path(), runs0, runs1, best))
    check("the panel is up on this machine", debrief.is_open())
  end
  wait(1)
  if not LOUD then print("HEIST SHOT debrief"); cctv.test_shot(); wait(0.5) end
  print(string.format("HEIST DONE %d passed, %d failed", pass, fail))
  finished = true
  if ended.win then real_win(ended.win) elseif ended.fail then real_fail(ended.fail) else real_fail("debrief test: no ending") end
end

local co = coroutine.wrap(body)
on("tick", function(dt)
  if finished then return end
  local ok, err = pcall(co, dt)
  if not ok then finished = true; print("HEIST FAIL script error: " .. tostring(err)); print(string.format("HEIST DONE %d passed, %d failed", pass, fail + 1)) end
end)
