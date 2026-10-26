-- The heist's gadgets (designs/bank-heist.md section 21, tools/heist_gadgets.lua) on the Janus bank with the real tools/mode_heist.lua.
-- One scenario per run (tests/heist/run_gadgets.sh):
--   GE_GADGETS_PITON=1      the piton gun: the first line (v3's facade, v2's roof) with the real fire button, abseil back down, every
--                           other line up to its high pad (any layout: by pad name), and the gallery line in a posted guard's view (an
--                           act: cover blown)
--   GE_GADGETS_LOCKPICK=1   a BLUE door picked by holding fire (about 10 s), a GREEN door rushed by taps (fast, noisy)
--   GE_GADGETS_POCKET=1     the security chief's BLUE card lifted from behind; a use press facing a guard is no pickpocket (cover holds)
--   GE_GADGETS_COIN=1       a coin: with the AI layer (section 18) it goes through noise.emit and the hall guard ahead hears it, walks
--                           over, searches, gives up and walks back to his post; without it the gadgets' own pull sends him and back
--   GE_GADGETS_LOOP=1       a camera on a tape loop does not see an armed Bond; when the loop runs out it does
--   GE_GADGETS_DISGUISE=1   a knocked-out guard's uniform: the security office (restricted) without suspicion; lingering next to a
--                           guard there gives him away ("close")
--   GE_GADGETS_VENT=1       the vents: the staff corridor into the security office, the lounge into the lockers
--   GE_GADGETS_GAS=1        the sleep gas knocks out the guard it lands by
--   GE_GADGETS_SAFE=1       the stethoscope at the director's safe: a miss rattles; three presses in the window open it (loot inside)
--   GE_GADGETS_THERMAL=1    the thermal imager: on for its time, then recharging (the boxes through the walls: the screenshot)
--   GE_GADGETS_DET=1        a scripted run through most gadgets for the determinism comparison (run_gadgets.sh det); Bond 1 in co-op too
-- GE_GADGETS_SHOTS=1 adds screenshots ("HEIST SHOT <name>"). Prints "HEIST PASS|FAIL ..." and "HEIST DONE <n> passed, <m> failed".
local RUN = "piton"
for _, r in ipairs({ "lockpick", "pocket", "coin", "loop", "disguise", "vent", "gas", "thermal", "safe", "det" }) do
  if mode.env_flag("GE_GADGETS_" .. r:upper()) then RUN = r end
end
local SHOTS = mode.env_flag("GE_GADGETS_SHOTS")
HEIST_SETTINGS = {
  test = true, jam_chance = 0, jam_every = 1000, security_delay = 4, investigators = 0, generator_after = 0,
  cover = (RUN ~= "lockpick"),
  gadgets = { loop_time = 8, pull_return = 6 },
}
include("heist")
mods.set("invincible", true)
on("stage_start", function() mode.guards_wake(false) end)

local G = GADGETS
local pass, fail = 0, 0
local function check(name, ok, extra)
  if ok then pass = pass + 1 else fail = fail + 1 end
  print(string.format("HEIST %s %s %s", ok and "PASS" or "FAIL", name, extra or ""))
end
local real_fail = mission.fail
mission.win = function(msg) return true end
mission.fail = function(msg) return true end

local face = nil
local function wait(sec)
  local t = 0
  while t < sec do
    t = t + coroutine.yield()
    for p = 0, bond.count() - 1 do bond.heal(p, 1, 1) end
    if face then bond.face(0, face[1], face[2]) end
  end
end
local function wait_for(sec, fn)
  local t = 0
  while t < sec and not fn() do wait(0.25); t = t + 0.25 end
  return fn(), t
end
local function d2(ax, az, bx, bz) return math.sqrt((ax - bx) ^ 2 + (az - bz) ^ 2) end
local function pad(n) return LEVEL.pads[n] end
local function room(n) return LEVEL.rooms[n].id end
local function shot(name)
  if not SHOTS then return end
  print("HEIST SHOT " .. name .. string.format(" t=%.1f", HEIST.elapsed))
  cctv.test_shot()
  wait(0.3)
end
local blown = {}
on("cover_blown", function(g, p, why) blown[#blown + 1] = { g = g, p = p, why = cover.why_name(why), t = HEIST.elapsed } end)

local function warp_xz(p, x, z, y)   -- (y: only pads on that floor)
  local best, bd = nil, 1e9
  for _, pd in ipairs(pads.far_from(x, z, 0, 900)) do
    local px, py, pz = pads.pos(pd)
    local d = d2(px, pz, x, z)
    if d < bd and (not y or math.abs(py - y) < 150) then best, bd = pd, d end
  end
  if not best then return false end
  local px, _, pz = pads.pos(best)
  return bond.warp(p, best, x - px, z - pz)
end
local function guards(area, kinds)
  local out = {}
  for _, g in ipairs(HEIST.guards) do
    if g.area == area and (not kinds or kinds[g.kind]) and spawn.alive(g.chr) and not spawn.down(g.chr) then out[#out + 1] = g.chr end
  end
  return out
end
local POSTS = { post = true, raiser = true, carrier = true }
-- stand where guard c has Bond 0 in sight, about `dist` away (8 directions round him, on his floor), facing him
local function stand_seen(c, dist)
  local x, z = spawn.where(c)
  local _, y = gadget.chr_yaw(c)
  if not x then return false end
  for k = 0, 7 do
    local a = k * math.pi / 4
    if warp_xz(0, x + math.sin(a) * dist, z + math.cos(a) * dist, y) then
      face = { x, z }
      wait(0.5)
      local st = cover.state(c)
      if st and (st.sees & 1) ~= 0 then return true end
    end
  end
  return false
end
-- Bond 0 `dist` from a hall guard on the hall floor (8 directions), facing him; the guard, or nil
local function stand_off_hall(dist, static)
  for _, g in ipairs(HEIST.guards) do
    -- static: not a patrol guard (with the AI layer some posted guards walk tours, g.route, and leave the spot in the next seconds)
    if g.area == "hall" and g.kind == "post" and not (static and g.route) and spawn.alive(g.chr) and not spawn.down(g.chr) then
      local cx, cz = spawn.where(g.chr)
      local _, cy = gadget.chr_yaw(g.chr)
      for k = 0, 7 do
        local a = k * math.pi / 4
        if warp_xz(0, cx + math.sin(a) * dist, cz + math.cos(a) * dist, cy) and bond.room(0) == room("hall") then
          local bx, _, bz = bond.pos(0)
          if math.abs(d2(bx, bz, cx, cz) - dist) < 60 then face = { cx, cz }; return g.chr, cx, cz end
        end
      end
    end
  end
  return nil
end
local function clear_hall(keep)
  for _, g in ipairs(HEIST.guards) do if g.area == "hall" and g.chr ~= keep then spawn.remove(g.chr) end end
  for _, c in ipairs(HEIST.civs) do if c.area == "hall" and c.chr ~= keep then spawn.remove(c.chr) end end
end
local function look_at(p, x, y, z)
  local _, _, bx, by, bz = gadget.aim(p)
  local yaw = math.deg(math.atan(-(x - bx), z - bz))
  if yaw < 0 then yaw = yaw + 360 end
  local pitch = math.deg(math.atan(y - by, d2(x, z, bx, bz)))
  bench.look(p, yaw, pitch)
  return yaw, pitch
end
local function reeled(p, secs)   -- wait for a reel that is under way to finish
  wait_for(1, function() return (gadget.reeling(p)) end)
  return wait_for(secs or 6, function() return not (gadget.reeling(p)) end)
end
local function near_pad(p, name, r)
  local x, _, z = bond.pos(p)
  local px, _, pz = pads.pos(pad(name))
  return x ~= nil and d2(x, z, px, pz) <= (r or 80), x and d2(x, z, px, pz) or -1
end

-- ------------------------------------------------------------------------------------------------------------------------------------

-- the first line to test with the real button: v3's facade, else v2's roof, else the first by name
local function first_line()
  for _, want in ipairs({ "facade", "roof" }) do for _, gp in ipairs(G.pairs) do if gp.id == want then return gp end end end
  return G.pairs[1]
end

-- Bond 0 next to door `idx` on the side of room `rname` (four tries round its middle), facing it
local function stand_at_door(idx, rname)
  local g = door.get(idx)
  for _, o in ipairs({ { 0, 140 }, { 0, -140 }, { 140, 0 }, { -140, 0 } }) do
    if warp_xz(0, g.x + o[1], g.z + o[2], g.y - 60) and bond.room(0) == room(rname) then face = { g.x, g.z }; return true end
  end
  return false
end

local function piton_path()
  bond.equip(0, "pitongun")
  check("the crew carries the piton gun", wait_for(3, function() return bond.weapon(0) == 36 end), tostring(bond.weapon(0)))
  check("the map has its grapple points and vents", #G.pairs >= 4 and #G.vents >= 2, string.format("%d pairs, %d vents", #G.pairs, #G.vents))
  -- 1. the first line with the real fire button, then back down
  local F = first_line()
  bond.warp(0, F.lo)
  local rx, rz = F.h[1], F.h[3]
  face = { rx, rz }
  wait(1)
  local t = G.grapple_target(0)
  check("at the " .. F.id .. " line's foot it is in reach", t ~= nil and t.up and t.pair.id == F.id, t and t.pair.id or "none")
  face = nil
  cctv.test_press("z")
  local started = wait_for(1.5, function() return (gadget.reeling(0)) end)
  check("the fire button fires the line (not the stock grenade)", started, string.format("reels %d", G.stats.grapples))
  wait(0.5)
  bench.look(0, (gadget.aim(0)), 50)   -- (looking up the line for the screenshot)
  wait(0.4)
  local _, _, _, y = gadget.aim(0)
  print(string.format("HEIST   mid-climb: eye y %.0f, yaw %.0f", y or -1, (gadget.aim(0))))
  shot("grapple_" .. F.id)
  local done = reeled(0, 6)
  local x, _, z = bond.pos(0)
  check("the line carries him up to its high pad", done and x and d2(x, z, rx, rz) < 80 and bond.room(0) == F.hroom,
        string.format("room %s, %.0f from the pad", tostring(LEVEL.room_name[bond.room(0) or 0]), x and d2(x, z, rx, rz) or -1))
  shot("grapple_top")
  -- abseil back down: face the low pad
  face = { F.l[1], F.l[3] }
  wait(0.75)
  t = G.grapple_target(0)
  check("at the top the line goes down", t ~= nil and not t.up, t and tostring(t.up) or "none")
  face = nil
  gadget.test_fire(0, 0, 1)
  done = reeled(0, 6)
  x, _, z = bond.pos(0)
  check("abseiled back down to its foot", done and x and d2(x, z, F.l[1], F.l[3]) < 80, tostring(LEVEL.room_name[bond.room(0) or 0]))
  check("unseen on the " .. F.id .. " line: no cover blown", #blown == 0, #blown > 0 and blown[1].why or "")
  -- 3. the hall floor -> the west gallery, in the hall guards' view: an act seen
  bond.warp(0, pad("grapple_gallery_low"))
  local lx, _, lz = pads.pos(pad("grapple_gallery_low"))
  local w = spawn.chr(pad("cust_5"), { post = { lx, lz }, weapon = "kf7", body = "St_Petersburg_Guard", offset = { 60, 0 } })
  check("a test guard posted in the hall, facing the line's foot", w ~= nil)
  if w then cover.guard(w); HEIST.guards[#HEIST.guards + 1] = { chr = w, kind = "post", area = "hall" } end
  face = { 0, 1200 }
  local watched = wait_for(5, function() return cover.seen(0) end)
  check("a hall guard watches the gallery line's foot", watched)
  local hx, _, hz = pads.pos(pad("grapple_gallery_high"))
  face = { hx, hz }
  wait(0.5)
  face = nil
  gadget.test_fire(0, 0, 1)
  done = reeled(0, 6)
  local gp
  for _, q in ipairs(G.pairs) do if q.id == "gallery" then gp = q end end
  check("the hall line: up to the gallery", done and gp and bond.room(0) == gp.hroom, tostring(LEVEL.room_name[bond.room(0)]))
  check("climbing in a hall guard's view blows the cover", #blown > 0 and blown[1].why == "act",
        string.format("watched=%s blown=%d %s", tostring(watched), #blown, blown[1] and blown[1].why or ""))
  -- 4. every other line on the map but the gallery (found by name: any layout), up to its high pad
  for _, gp in ipairs(G.pairs) do
    if gp.id ~= F.id and gp.id ~= "gallery" then
      bond.warp(0, gp.lo)
      face = { gp.h[1], gp.h[3] }
      wait(1)
      face = nil
      gadget.test_fire(0, 0, 1)
      done = reeled(0, 7)
      local hx, _, hz = pads.pos(gp.hi)
      local x, _, z = bond.pos(0)
      check("the " .. gp.id .. " line: up to its high pad", done and x and d2(x, z, hx, hz) < 80 and bond.room(0) == gp.hroom,
            tostring(LEVEL.room_name[bond.room(0) or 0]))
    end
  end
  print(string.format("HEIST   (seen on the other lines: %d cover blow(s); a line in a guard's or camera's view is an act)", #blown))
end

local function lockpick_path()
  local O = LEVEL.objects
  local blue = O.sec_back.index
  check("in the staff corridor at the security office's back door", stand_at_door(blue, "back_corr_e"))
  bond.equip(0, "gaskeyring")
  wait(2)
  check("lockpicks in hand", bond.weapon(0) == 54, tostring(bond.weapon(0)))
  check("the BLUE back door is the lock in reach", G.pick_target(0) == blue or G.pick_target(0) == O.sec_back.pair,
        string.format("%s (want %d)", tostring(G.pick_target(0)), blue))
  check("without the card it is locked", door.get(blue).keyflags == 0x4 and not bond.has_key(0, 0x4))
  local t0 = HEIST.elapsed
  gadget.test_fire(0, 60 * 14)
  wait(5)
  local st = G.picked[blue] or G.picked[O.sec_back.pair]
  print(string.format("HEIST   picking after 5 s: %.1f of %.1f s", st and st.prog or -1, st and st.need or -1))
  shot("lockpick")
  local open, t = wait_for(10, function() return door.get(blue).keyflags == 0 end)
  local took = HEIST.elapsed - t0
  check("held fire picks the BLUE lock in about 10 s", open and took >= 8.5 and took <= 12, string.format("%.1f s", took))
  check("the picked door opens", wait_for(3, function() return door.get(blue).open > 0.5 end), string.format("open %.2f", door.get(blue).open))
  check("a quiet pick makes no noise", G.stats.noises == 0, tostring(G.stats.noises))
  gadget.test_fire(0, 0, 0)
  wait(1)
  -- rushed: the GREEN maintenance door from the canteen, by taps
  local green = O.maint_door.index
  check("in the canteen at the maintenance door", stand_at_door(green, "canteen"))
  wait(1.5)
  check("the GREEN maintenance door is in reach", G.pick_target(0) == green, tostring(G.pick_target(0)))
  t0 = HEIST.elapsed
  gadget.test_fire(0, 0, 12)
  open = wait_for(5, function() return door.get(green).keyflags == 0 end)
  took = HEIST.elapsed - t0
  check("rushed by taps: the GREEN lock (6 s held) opens much faster", open and took < 4, string.format("%.1f s", took))
  check("rushing it rattles: a door noise", G.stats.noises >= 1, tostring(G.stats.noises))
  face = nil
end

local function pocket_path()
  mode.body_check(false)
  local chief = HEIST.carriers["security chief"]
  check("the security chief carries BLUE", chief and chief.chr and select(2, spawn.key_alive(chief.key)), "")
  if not (chief and chief.chr) then return end
  local c = chief.chr
  wait(1)
  local cx, cz = spawn.where(c)
  local yaw = gadget.chr_yaw(c)
  local bx, bz = cx - math.sin(math.rad(yaw)) * 85, cz - math.cos(math.rad(yaw)) * 85
  warp_xz(0, bx, bz)
  face = { cx, cz }
  wait(0.5)
  local fx, fz = spawn.where(c)
  print(string.format("HEIST   chief at (%.0f %.0f) facing %.0f deg; Bond %.0f behind him", fx, fz, yaw, d2(bx, bz, fx, fz)))
  cctv.test_use(0)
  local got = wait_for(1.5, function() return bond.has_key(0, 0x4) end)
  check("from behind, unseen: the BLUE card lifted", got and G.stats.lifts == 1, string.format("lifts %d", G.stats.lifts))
  check("he did not notice (no cover blown)", #blown == 0, #blown > 0 and blown[1].why or "")
  shot("pickpocket")
  -- a guard facing him catches his hand: a test guard in the hall with a card, posted facing south
  local px, _, pz = pads.pos(pad("hall_mid"))
  local g = spawn.chr(pad("hall_mid"), { post = { px, pz - 600 }, weapon = "kf7", body = "St_Petersburg_Guard" })
  check("a test guard on post in the hall", g ~= nil)
  if not g then return end
  spawn.key(g, 0x100)
  cover.guard(g)
  HEIST.guards[#HEIST.guards + 1] = { chr = g, kind = "post", area = "hall" }
  wait(1.5)
  local gx, gz = spawn.where(g)
  yaw = gadget.chr_yaw(g)
  warp_xz(0, gx + math.sin(math.rad(yaw)) * 85, gz + math.cos(math.rad(yaw)) * 85)
  face = { gx, gz }
  wait(0.5)
  local nb = #blown
  cctv.test_use(0)
  wait(1)
  check("facing him: no card", not bond.has_key(0, 0x100))
  -- (until 2026-10-26 this press was "caught" and blew the cover; a press in front of a guard is no pickpocket now: 23c)
  check("facing him the press is no pickpocket: the cover holds", #blown == nb, string.format("%d blown", #blown - nb))
end

local function coin_path()
  bond.equip(0, "creditcard")
  local target, home_x, home_z = stand_off_hall(700)   -- (the coin lands about 540 ahead: near him)
  check("a hall guard 7 m ahead", target ~= nil)
  wait(2)
  check("coins in hand", bond.weapon(0) == 53, tostring(bond.weapon(0)))
  face = nil
  gadget.test_fire(0, 0, 1)
  wait(1.5)
  check("the coin lands and makes a noise", G.stats.coins == 1 and G.stats.noises == 1, string.format("coins %d noises %d", G.stats.coins, G.stats.noises))
  if target and rawget(_G, "noise") and noise.emit and rawget(_G, "ai") and ai.awareness then
    -- the AI layer (section 18) hears it: noise.emit, no pull by the gadgets' own fallback. The guard ahead turns, walks over to look,
    -- searches, gives up and walks back to his post.
    check("the coin went through the AI layer's noise.emit (no fallback pull)", #G.pulled == 0, tostring(#G.pulled))
    local hx, hz = home_x, home_z
    local heard = wait_for(3, function() local st = ai.awareness(target); return st and st >= 1 end)
    check("the guard ahead heard the coin (suspicious or searching)", heard, string.format("state %s", tostring(ai.awareness(target))))
    wait(0.5)
    shot("coin_heard")   -- (his AR bubble: the AI layer's bark)
    local nl = noise.last()
    local gx0, gz0 = spawn.where(target)
    local c0 = (nl and gx0) and d2(gx0, gz0, nl.x, nl.z) or 0
    local left = wait_for(15, function() local x, z = spawn.where(target); return nl and x and d2(x, z, nl.x, nl.z) < math.max(150, c0 - 150) end)
    local x1, z1 = spawn.where(target)
    check("he walks over to where the coin fell", left, string.format("%.0f -> %.0f from the coin, %.0f from his post", c0,
          (nl and x1) and d2(x1, z1, nl.x, nl.z) or -1, x1 and d2(x1, z1, hx, hz) or -1))
    shot("coin_investigates")
    local back = wait_for(60, function()   -- (a search lasts 30 s: run_gadgets.sh gives the coin run the frames)
      local st = ai.awareness(target); local x, z = spawn.where(target)
      return st == 0 and x and d2(x, z, hx, hz) < 150 end)
    local x2, z2 = spawn.where(target)
    check("then he gives up and walks back to his post", back, string.format("state %s, %.0f from his post", tostring(ai.awareness(target)),
          x2 and d2(x2, z2, hx, hz) or -1))
    check("throwing a coin is not suspicious", #blown == 0, #blown > 0 and blown[1].why or "")
    return
  end
  local e = G.pulled[1]
  check("guards come to look", #G.pulled >= 1, tostring(#G.pulled))
  if not e then return end
  local x0, z0 = spawn.where(e.chr)
  local hx, _, hz = pads.pos(e.home)
  wait(4.5)
  local x1, z1 = spawn.where(e.chr)
  check("the pulled guard left his post", d2(x1, z1, hx, hz) > 150, string.format("%.0f from his post", d2(x1, z1, hx, hz)))
  local back = wait_for(20, function() local x, z = spawn.where(e.chr); return #G.pulled == 0 and d2(x, z, hx, hz) < 120 end)
  local x2, z2 = spawn.where(e.chr)
  check("then he walks back to his post", back, string.format("%.0f from his post", d2(x2, z2, hx, hz)))
  check("throwing a coin is not suspicious", #blown == 0, #blown > 0 and blown[1].why or "")
end

local function loop_path()
  mode.body_check(false)
  clear_hall()
  local cam = HEIST.M.P.feed_cam
  cctv.set_alarm_time(1)
  local spotted = nil
  on("cctv_spotted", function(c, p) if c == cam and not spotted then spotted = HEIST.elapsed end end)
  bond.warp(0, pad("hall_post_se"))
  bond.equip(0, "datathief")
  wait(2)
  local c = cctv.get(cam)
  local wy, wp = look_at(0, c.x, c.y, c.z)
  wait(0.3)
  local ay, ap, ax, ay2, az = gadget.aim(0)
  print(string.format("HEIST   camera %d at (%.0f %.0f %.0f); asked yaw %.0f pitch %.0f; Bond at (%.0f %.0f %.0f) yaw %.0f pitch %.0f", cam, c.x, c.y, c.z,
                      wy, wp, ax, ay2, az, ay, ap))
  G.debug = true
  check("the camera is in the tape loop's aim", G.loop_target(0) == cam, tostring(G.loop_target(0)))
  G.debug = false
  gadget.test_fire(0, 0, 1)
  wait(0.75)
  check("the loop is planted", G.loops[cam] ~= nil and G.stats.loops == 1, cctv.name(cam))
  for _, other in ipairs(cctv.list()) do if other ~= cam then cctv.disable(other) end end   -- (only the looped one can see him)
  bond.warp(0, pad("cust_1"))
  bond.equip(0, "pp7sil")
  wait(5)
  check("armed in its view, the looped camera sees nothing", spotted == nil and not HEIST.report, string.format("timer %.2f", cctv.get(cam).timer or -1))
  shot("tapeloop_feed")
  local sp = wait_for(16, function() return spotted ~= nil end)
  check("when the loop runs out it sees him again", sp and G.loops[cam] == nil, string.format("spotted at %s", tostring(spotted)))
end

local function disguise_path()
  mode.body_check(false)
  local hall = guards("hall", { post = true })
  local c = hall[1]
  check("a hall guard to take the uniform from", c ~= nil)
  if not c then return end
  clear_hall(c)
  -- the AI layer's cameras (section 18) report a search of a body as an act: the hall's cameras would bring the alarm and the security
  -- guards out of their office. This scenario is about the uniform, so the cameras are off.
  for _, cam in ipairs(cctv.list()) do cctv.disable(cam) end
  wait(0.5)
  spawn.ko(c, 0)
  wait(4.5)
  local cx, cz = spawn.where(c)
  local _, cy = gadget.chr_yaw(c)
  warp_xz(0, cx + 60, cz, cy)
  face = { cx, cz }
  wait(0.5)
  local bx, _, bz = bond.pos(0)
  local _, by = bond.pos(0)
  print(string.format("HEIST   body at (%.0f %.0f %.0f), Bond %.0f from it (y %.0f), ko %s", cx, cy or -1, cz, d2(bx, bz, cx, cz), by or -1, tostring(ko.is(c))))
  cctv.test_use(0)
  wait(1)
  if not G.searched[c] then print("HEIST   (the use tap did not search; ko.search: " .. tostring(ko.search(0)) .. ")"); wait(0.5) end
  check("the first press searches him", G.searched[c] == true)
  cctv.test_use(0)
  wait(0.75)
  local me = G.players[0]
  check("the second press: changing into his uniform", me.task and me.task.kind == "disguise", me.task and me.task.kind or "none")
  shot("disguise_change")
  local done = wait_for(6, function() return gadget.disguise(0) == 2 end)
  check("4 s later Bond wears a guard's uniform", done and G.stats.disguises == 1)
  face = nil
  -- the security office: restricted (blown within about a second when seen), but not in a guard's uniform
  local sec = guards("security", POSTS)
  local g
  for _, c2 in ipairs(sec) do if stand_seen(c2, 350) then g = c2; break end end
  check("in the security wing a guard has him in sight", g ~= nil, string.format("%d security guards", #sec))
  wait(5)
  check("in uniform: no suspicion in a restricted room (5 s in sight)", #blown == 0, #blown > 0 and blown[1].why or "")
  shot("disguise_secroom")
  -- looked at closely: lingering right next to him
  if g and stand_seen(g, 110) then
    local hey = wait_for(10, function() return #blown > 0 end)
    check("lingering next to a guard, he looks closer and sees through it", hey and blown[1].why == "close", hey and blown[1].why or "none")
  else
    check("next to a security guard", false)
  end
end

local function vent_path()
  local n = 0
  for _, v in ipairs(G.vents) do   -- (by name: any layout)
    bond.warp(0, v.a)
    wait(1)
    cctv.test_use(0)
    if v.id == "vault" then
      wait(1)
      check("the vault ventilator refuses with the power on", not (gadget.reeling(0)) and G.stats.vents == n)
    else
      local done = reeled(0, 6)
      local bx, _, bz = pads.pos(v.b)
      local x, _, z = bond.pos(0)
      n = n + 1
      check("the " .. v.id .. " vent: crawled through", done and x and d2(x, z, bx, bz) < 80, tostring(LEVEL.room_name[bond.room(0) or 0]))
    end
  end
  check("every vent crawled", n >= 2 and G.stats.vents == n, tostring(G.stats.vents))
end

local function gas_path()
  mode.body_check(false)
  bond.equip(0, "bombdefuser")
  local c, cx, cz = stand_off_hall(420, true)
  if not c then check("a hall guard", false); return end
  wait(2)
  face = nil
  gadget.test_fire(0, 0, 1)
  wait(0.5)
  check("the gas is thrown", #G.gas == 1)
  -- every guard standing where it landed is out cold 3 s later (where it lands depends on the line of the throw, not on him)
  local gs = G.gas[1]
  local near = {}
  if gs then
    for _, list in ipairs({ HEIST.guards, HEIST.civs }) do   -- (the gas puts anyone to sleep: guards and staff)
      for _, g in ipairs(list) do
        local gx, gz = spawn.where(g.chr)
        if gx and spawn.alive(g.chr) and not spawn.down(g.chr) and d2(gx, gz, gs.x, gs.z) <= G.T.gas_radius - 40 then near[#near + 1] = g.chr end
      end
    end
  end
  local all = wait_for(5, function()
    if G.stats.gassed == 0 then return false end
    for _, ch in ipairs(near) do if not ko.is(ch) then return false end end
    return true end)
  check("3 s later everyone where it landed is out cold", all and G.stats.gassed >= 1 and #near >= 1,
        string.format("gassed %d, %d in the cloud", G.stats.gassed, #near))
end

local function thermal_path()
  bond.equip(0, "camera")
  check("the thermal imager in hand", wait_for(3, function() return bond.weapon(0) == 40 end), tostring(bond.weapon(0)))
  warp_xz(0, -2600, -300, 0)   -- (office A, west of the hall: its guards are behind two walls)
  face = { 0, -300 }
  wait(1)
  gadget.test_fire(0, 0, 1)
  wait(1)
  local me = G.players[0]
  check("fire: the imager is on", me.xray_until and me.xray_until > HEIST.elapsed, tostring(me.xray_until))
  shot("thermal")
  wait(10)
  check("10 s later it is off and recharging", HEIST.elapsed >= me.xray_until and HEIST.elapsed < me.xray_ready)
  gadget.test_fire(0, 0, 1)
  wait(1)
  check("while recharging, fire does nothing", (G.stats.thermal or 0) == 1, tostring(G.stats.thermal))
end

local function safe_path()
  local sfo = LEVEL.objects.director_safe
  bond.warp(0, pad("dir_safe_front"))
  face = { sfo.x or -400, sfo.z or 4430 }
  bond.equip(0, "gaskeyring")
  wait(2)
  local sf = G.safe_target(0)
  check("the director's safe in reach of the lockpicks", sf ~= nil and sf.idx == sfo.index, sf and sf.name or "none")
  if not sf then return end
  gadget.test_fire(0, 0, 1)
  wait(0.75)
  local me = G.players[0]
  check("fire: listening at the dial", me.crack ~= nil and me.crack.tumbler == 1)
  shot("safe_dial")
  local T = G.T
  -- a miss: half a dial away from the window
  local now = gadget.now(0)
  local c = me.crack
  local function delay_to(frac)   -- ticks from now until the needle is at `frac`
    local ph = ((now - c.start) % T.safe_period)
    local want = math.floor(frac * T.safe_period)
    return (want - ph) % T.safe_period
  end
  local n0 = G.stats.noises
  gadget.test_fire(0, 0, 1, delay_to((c.center + 0.5) % 1) - 1)
  wait(2)
  check("a press outside the window: a miss, it rattles", me.crack.tumbler == 1 and (me.crack.misses or 0) == 1 and G.stats.noises == n0 + 1,
        string.format("tumbler %d misses %s", me.crack.tumbler, tostring(me.crack.misses)))
  for k = 1, T.safe_tumblers do
    c = me.crack
    if not c then break end
    now = gadget.now(0)
    gadget.test_fire(0, 0, 1, delay_to(c.center) - 1)
    wait(2)
  end
  local opened = wait_for(3, function() return door.get(sfo.index).open > 0.3 end)
  check("three tumblers in the window: the safe opens", opened and (G.stats.safes or 0) == 1, string.format("open %.2f", door.get(sfo.index).open))
end

-- the determinism scenario: the same gadgets, the same ticks, every run (Bond 1 in co-op climbs the gallery line and throws a coin)
local function det_path()
  local n = bond.count()
  print("HEIST DET start " .. n)
  local F = first_line()
  bond.warp(0, F.lo)
  if n > 1 then bond.warp(1, pad("grapple_gallery_low")) end
  bond.equip(0, "pitongun")
  if n > 1 then bond.equip(1, "pitongun") end
  wait(2)
  face = { F.h[1], F.h[3] }
  wait(0.5)
  face = nil
  gadget.test_fire(0, 0, 1)
  if n > 1 then gadget.test_fire(1, 0, 1) end
  reeled(0, 6)
  print(string.format("HEIST DET roof room %s t=%.2f", tostring(bond.room(0)), HEIST.elapsed))
  gadget.test_fire(0, 0, 1)
  reeled(0, 6)
  bond.warp(0, pad("cust_4"))
  bond.equip(0, "creditcard")
  if n > 1 then bond.equip(1, "creditcard") end
  wait(2)
  gadget.test_fire(0, 0, 1)
  if n > 1 then gadget.test_fire(1, 0, 1) end
  wait(3)
  print(string.format("HEIST DET coin noises %d pulled %d", G.stats.noises, #G.pulled))
  stand_at_door(LEVEL.objects.sec_back.index, "back_corr_e")
  bond.equip(0, "gaskeyring")
  wait(2)
  face = nil
  gadget.test_fire(0, 0, 4)
  gadget.test_fire(0, 60 * 3)
  wait(4)
  print(string.format("HEIST DET pick noises %d", G.stats.noises))
  local V = G.vents[1]
  for _, v in ipairs(G.vents) do if v.id ~= "vault" then V = v; break end end
  bond.warp(0, V.a)
  wait(0.5)
  cctv.test_use(0)
  reeled(0, 5)
  print(string.format("HEIST DET vent room %s", tostring(bond.room(0))))
  wait(6)
  print(string.format("HEIST DET end t=%.2f hot=%s state=%s", HEIST.elapsed, tostring(HEIST.hot), G.state()))
end

local finished = false
local function body()
  local ready = wait_for(25, function() return HEIST.ready end)
  check("mode ready", ready, "run " .. RUN)
  wait(1.5)
  check("the gadgets are in the inventory", G.players[0] ~= nil and G.players[0].coins == 4, "")
  if RUN == "piton" then piton_path() elseif RUN == "lockpick" then lockpick_path() elseif RUN == "pocket" then pocket_path()
  elseif RUN == "coin" then coin_path() elseif RUN == "loop" then loop_path() elseif RUN == "disguise" then disguise_path()
  elseif RUN == "vent" then vent_path() elseif RUN == "gas" then gas_path() elseif RUN == "thermal" then thermal_path()
  elseif RUN == "safe" then safe_path()
  elseif RUN == "det" then det_path() end
  print(string.format("HEIST DONE %d passed, %d failed", pass, fail))
  finished = true
  if RUN ~= "det" then real_fail("gadgets test done") end
end

local co = coroutine.wrap(body)
on("tick", function(dt)
  if finished then return end
  local ok, err = pcall(co, dt)
  if not ok then finished = true; print("HEIST FAIL script error: " .. tostring(err)); print(string.format("HEIST DONE %d passed, %d failed", pass, fail + 1)) end
end)
