-- AC's playtest (2026-10-28): a knocked-out guard who was then killed stood back up to play a death animation. He must die where he
-- lies: the lying (KO) pose every tick until he is dead (ACT_DEAD), never a standing death animation, in the same place, his gun dropped.
-- Two guards in the bank's hall, each knocked out (spawn.ko) and then killed: A by gunshots (test.shoot: the shot path,
-- chraction.c triggered_on_shot_hit), B by spawn.kill (the blast path, chrlvExplosionDamage, as a grenade would).
-- tests/heist/run_kokill.sh. Prints "HEIST PASS|FAIL ..." and "HEIST DONE n passed, m failed".
include("levelgen_layout")
mode.keep_locks()
local PD = LEVEL.pads
local pass, fail = 0, 0
local function check(name, ok, extra)
  if ok then pass = pass + 1 else fail = fail + 1 end
  print(string.format("HEIST %s %s %s", ok and "PASS" or "FAIL", name, extra or ""))
end
local died = {}
on("chr_died", function(v) died[v] = true end)
local ACT_DIE, ACT_DEAD = 4, 5

local function wait(sec)
  local t = 0
  while t < sec do t = t + coroutine.yield(); bond.heal(0, 1, 1) end
end

local function one(tag, pad, how)
  local g = spawn.chr(pad, { behaviour = "hold", weapon = "kf7", body = "St_Petersburg_Guard" })
  wait(1)
  check(tag .. ": guard spawned", g ~= nil)
  if not g then return end
  check(tag .. ": knocked out", spawn.ko(g, 0) and ko.is(g))
  wait(4)   -- (the fall takes about 3 s)
  local on0, f0, e0 = test.ko_pose(g)
  check(tag .. ": lying on the KO pose before the kill", on0 == true and f0 >= e0 - 1, string.format("on %s frame %.1f / %.1f", tostring(on0), f0 or -1, e0 or -1))
  local x0, z0 = spawn.where(g)
  local loose0 = spawn.pools()
  if how == "shot" then
    for _ = 1, 40 do if not died[g] then test.shoot(g, "body", "kf7", 0) end end
  else
    spawn.kill(g)
  end
  -- every tick for 3 s: still on the lying pose, at (or held at) its last frame; dead by the end
  local ticks, off, raised, last_act, gone = 0, 0, 0, -1, false
  local t = 0
  while t < 3 do
    t = t + coroutine.yield()
    ticks = ticks + 1
    local on, f, e, act = test.ko_pose(g)
    if on == nil then gone = true; break end
    if not on then off = off + 1 end
    if on and f < e - 1 then raised = raised + 1 end
    last_act = act or last_act
  end
  check(tag .. ": killed (chr_died)", died[g] == true)
  check(tag .. ": the body is still there 3 s later", not gone and t >= 3, string.format("%d script ticks (4 Hz), %.2f s", ticks, t))
  check(tag .. ": never left the lying pose", off == 0, string.format("%d of %d ticks off it", off, ticks))
  check(tag .. ": never rose from its last frame", raised == 0, string.format("%d ticks below the last frame", raised))
  check(tag .. ": ends dead on the floor (ACT_DEAD)", last_act == ACT_DEAD, "actiontype " .. tostring(last_act))
  local x1, z1 = spawn.where(g)
  check(tag .. ": stayed where it lay", x1 ~= nil and math.abs(x1 - x0) < 10 and math.abs(z1 - z0) < 10,
        string.format("moved %.1f", x1 and math.sqrt((x1 - x0) ^ 2 + (z1 - z0) ^ 2) or -1))
  local loose1 = spawn.pools()
  check(tag .. ": the gun dropped", loose1 > loose0, string.format("loose guns %d -> %d", loose0, loose1))
end

local co = coroutine.create(function()
  wait(1)
  spawn.keep_bodies(true)   -- (as the heist does at its stage start: reset by the stage's own start, so not at the top level)
  cctv.alarm_on_spot(false)
  mods.set("invincible", true)
  bond.warp(0, PD.hall_entry_w)
  one("A (shot)", PD.cust_5, "shot")
  one("B (blast / spawn.kill)", PD.hall_post_nw, "kill")
  -- C, the control: a standing guard (never knocked out) shot dead plays a stock death animation, so the 4 Hz sampling above does see
  -- a chr off the lying pose (were the fix gone, A and B would read like this)
  local c = spawn.chr(PD.cust_1, { behaviour = "hold", weapon = "kf7", body = "St_Petersburg_Guard" })
  wait(1)
  for _ = 1, 40 do if c and not died[c] then test.shoot(c, "body", "kf7", 0) end end
  local seen_off, t = 0, 0
  while t < 3 do
    t = t + coroutine.yield()
    local on = c and test.ko_pose(c)
    if on == false then seen_off = seen_off + 1 end
  end
  check("C (control: no KO) shot dead: off the lying pose, as a stock death is", c ~= nil and died[c] == true and seen_off > 0,
        string.format("%d samples off it", seen_off))
  print(string.format("HEIST DONE %d passed, %d failed", pass, fail))
end)
on("tick", function(dt)
  if coroutine.status(co) ~= "dead" then
    local ok, err = coroutine.resume(co, dt)
    if not ok then print("HEIST FAIL script error: " .. tostring(err)); fail = fail + 1 end
  end
end)
