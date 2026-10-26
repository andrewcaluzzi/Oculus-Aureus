-- Heist input fixes (designs/bank-heist.md 23c, branch `inputfix`): the use press and the pickpocket, and drawing the gun from the opening
-- slappers, through the real input path (test.press holds raw N64 buttons on pad 0; test.use taps the Port use action).
-- tests/heist/run_inputfix.sh (GE_SEED: the seed; the mode picks the carriers' pads and the GREEN carrier by it).
-- Prints "HEIST PASS|FAIL ..." and "HEIST DONE <n> passed, <m> failed".
--   1. a use press in front of a guard who faces Bond, nothing in reach: no pickpocket, the cover holds (B and the use key)
--   2. B behind each standing carrier, facing him: the `use` event fires and the card is lifted, on every try (TRIES per carrier)
--   3. from the opening slappers, A (the pad's next weapon) draws the PP7; the next-weapon action does too
HEIST_SETTINGS = {
  test = true, jam_chance = 0, jam_every = 1000, security_delay = 4, investigators = 0, generator_after = 0,
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

mission.win = function(msg) return true end
mission.fail = function(msg) return true end

local face = nil
local function wait(sec)
  local t = 0
  while t < sec do
    t = t + coroutine.yield()
    bond.heal(0, 1, 1)
    if face then test.face(0, face[1], face[2]) end
  end
end
local function wait_for(sec, fn)
  local t = 0
  while t < sec and not fn() do wait(0.1); t = t + 0.1 end
  return fn(), t
end
local function d2(ax, az, bx, bz) return math.sqrt((ax - bx) ^ 2 + (az - bz) ^ 2) end
local function warp_xz(p, x, z, y)
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
local blown = {}
on("cover_blown", function(g, p, why) blown[#blown + 1] = { g = g, p = p, why = cover.why_name(why) } end)
local uses = {}
on("use", function(p, kind, idx, objtype) uses[#uses + 1] = { p = p, idx = idx, key = bond.use_key(p) } end)

-- Bond 0 `dist` from chr c along `deg` off his facing (0 = in front of him, 180 = behind), on his floor, facing him
local function stand_by(c, dist, deg)
  local cx, cz = spawn.where(c)
  local yaw, cy = gadget.chr_yaw(c)
  if not cx then return false end
  local a = math.rad(yaw + deg)
  local ok = warp_xz(0, cx + math.sin(a) * dist, cz + math.cos(a) * dist, cy)
  face = { cx, cz }
  return ok
end

local function front_case()
  -- a lone test guard in the hall, posted facing the spot Bond stands on; no carrier near
  local P = LEVEL.pads
  local g
  for _, pn in ipairs({ "hall_mid", "cust_5", "cust_1", "hall_post_nw" }) do   -- (the first free one: a carrier may stand on hall_mid)
    if not g and P[pn] then
      local px, _, pz = pads.pos(P[pn])
      g = spawn.chr(P[pn], { post = { px, pz - 600 }, weapon = "kf7", body = "St_Petersburg_Guard" })
    end
  end
  check("a test guard on post in the hall", g ~= nil)
  if not g then return end
  cover.guard(g)
  HEIST.guards[#HEIST.guards + 1] = { chr = g, kind = "post", area = "hall" }
  wait(1.5)
  -- first from behind (before Bond hangs about in his face, which makes a guard suspicious): looking away no pickpocket, then the lift
  spawn.key(g, 0x100)
  stand_by(g, 85, 180)
  local bx, _, bz = bond.pos(0)
  local gx, gz = spawn.where(g)
  face = { bx + (bx - gx) * 4, bz + (bz - gz) * 4 }
  wait(0.6)
  test.press("b")
  wait(1)
  check("behind him, looking away: no pickpocket", not bond.has_key(0, 0x100))
  stand_by(g, 85, 180)
  wait(0.6)
  test.press("b")
  wait(1)
  check("behind him, facing his back: the lift", bond.has_key(0, 0x100))
  -- then in front of him, his pocket empty now: B, the use key
  for _, how in ipairs({ "b", "use" }) do
    stand_by(g, 85, 0)
    wait(0.6)
    local nb, nu, lifts = #blown, #uses, G.stats.lifts
    if how == "b" then test.press("b") else test.use(0) end
    wait(1)
    local fired = #uses > nu and uses[#uses].idx < 0
    check("in front of a facing guard, " .. how .. " with nothing in reach: the use event fired", fired, string.format("%d use events", #uses - nu))
    check("in front of a facing guard, " .. how .. ": no pickpocket, the cover holds", #blown == nb and G.stats.lifts == lifts,
          string.format("blown %d, lifts %d", #blown - nb, G.stats.lifts - lifts))
  end
  spawn.remove(g)
end

local TRIES = 3
local ROLES = { "lobby captain", "security chief", "branch manager" }
local function standing(cr) return cr and cr.chr and spawn.alive(cr.chr) and not spawn.down(cr.chr) end
local function press_behind(cr)
  stand_by(cr.chr, 85, 180)
  wait(0.6)
  local nu, lifts = #uses, G.stats.lifts
  test.press("b")
  wait(0.8)
  return #uses - nu, G.stats.lifts - lifts
end
local function behind_case()
  -- first the lift from each carrier (a repeat press on a guard who is suspicious by then is caught, which would put the cover on
  -- alert for the rest), then the repeat presses: each must still be a use event
  local fired = {}
  for _, role in ipairs(ROLES) do
    local cr = HEIST.carriers[role]
    if standing(cr) then
      local nb0 = #blown
      local ev, lifts = press_behind(cr)
      print(string.format("HEIST   %s: use events %d, lifts %d, has card %s", role, ev, lifts, tostring(bond.has_key(0, cr.flags))))
      check("behind the " .. role .. ", B: the use event fired", ev > 0, tostring(ev))
      check("behind the " .. role .. ", B: the card lifted", bond.has_key(0, cr.flags), string.format("lifts %d", lifts))
      check("behind the " .. role .. ": no cover blown", #blown == nb0, #blown > nb0 and blown[#blown].why or "")
      fired[role] = ev > 0 and 1 or 0
    else
      print("HEIST   (no standing " .. role .. " this seed)")
    end
  end
  for _, role in ipairs(ROLES) do
    local cr = HEIST.carriers[role]
    if fired[role] and standing(cr) then
      for k = 2, TRIES do if press_behind(cr) > 0 then fired[role] = fired[role] + 1 end end
      check("behind the " .. role .. ", B on every try: a use event", fired[role] == TRIES, string.format("%d of %d", fired[role], TRIES))
    end
  end
  face = nil
end

local function draw_case()
  -- the opening hand: the slappers (S.start_hand "fist"); A draws the PP7 and back with the previous weapon
  local w0 = bond.weapon(0)
  check("the crew starts holstered (the slappers)", w0 == 0 or w0 == 1, tostring(w0))
  test.press("a")
  local drew = wait_for(3, function() return bond.weapon(0) == 5 end)
  check("A from the opening slappers draws the PP7", drew, tostring(bond.weapon(0)))
  bond.equip(0, "fist")
  wait(1.5)
  test.press("a")
  drew = wait_for(3, function() return bond.weapon(0) == 5 end)
  check("A again after holstering: the PP7", drew, tostring(bond.weapon(0)))
  bond.equip(0, "fist")
  wait(1.5)
end

local finished = false
local function body()
  local ready = wait_for(25, function() return HEIST.ready end)
  check("mode ready", ready, "")
  wait(1.5)
  draw_case()
  front_case()
  behind_case()
  print(string.format("HEIST DONE %d passed, %d failed", pass, fail))
  finished = true
  bench.exit()   -- (the process ends here, not at GE_FRAMES)
end

local co = coroutine.wrap(body)
on("tick", function(dt)
  if finished then return end
  local ok, err = pcall(co, dt)
  if not ok then finished = true; print("HEIST FAIL script error: " .. tostring(err)); print(string.format("HEIST DONE %d passed, %d failed", pass, fail + 1)) end
end)
