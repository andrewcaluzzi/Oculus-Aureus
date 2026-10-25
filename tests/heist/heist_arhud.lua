-- The AR specs and the diagnostics layer (designs/bank-heist.md section 20) on the Janus bank with the real tools/mode_heist.lua.
-- Run: tests/heist/run_ar.sh (headless). Checks the ar.* / diag.* API and the heist wiring, and with GE_AR_SHOTS=1 takes the showcase
-- screenshots ("HEIST SHOT <name>" lines, cctv.test_shot):
--   ar_keys     a card carrier with his key tag and bracket corners
--   ar_diag     the diagnostics layer over the banking hall (chr lines, sight cones, hearing rings, the panel, the radar)
--   ar_suspect  a guard watching a drawn gun: the "?" over his head filling
--   ar_hey      the same guard seeing through Bond: the red "!" and the "HEY!" bubble
-- Prints "HEIST PASS|FAIL ..." and "HEIST DONE <n> passed, <m> failed".
local SHOTS = mode.env_flag("GE_AR_SHOTS")
HEIST_SETTINGS = { test = true, jam_chance = 0, jam_every = 1000, cover_tune = { armed = 4, radio = 30 } }   -- a slow gun meter: the "?" fills
include("heist")
mods.set("invincible", true)
on("stage_start", function() mode.guards_wake(false) end)

local pass, fail = 0, 0
local function check(name, ok, extra)
  if ok then pass = pass + 1 else fail = fail + 1 end
  print(string.format("HEIST %s %s %s", ok and "PASS" or "FAIL", name, extra or ""))
end
local real_fail = mission.fail
mission.win = function() return true end
mission.fail = function() return true end

local face = nil
local function wait(sec)
  local t = 0
  while t < sec do
    t = t + coroutine.yield()
    bond.heal(0, 1, 1)
    if face then bond.face(0, face[1], face[2], face[3]) end
  end
end
local function wait_for(sec, fn)
  local t = 0
  while t < sec and not fn() do wait(0.25); t = t + 0.25 end
  return fn(), t
end
local function d2(ax, az, bx, bz) return math.sqrt((ax - bx) ^ 2 + (az - bz) ^ 2) end
local function shot(name)
  if not SHOTS then return end
  print("HEIST SHOT " .. name .. string.format(" t=%.1f hot=%s meter=%s", HEIST.elapsed, tostring(HEIST.hot), (cover.meter(0))))
  cctv.test_shot()
  wait(0.3)
end
local function warp_xz(x, z)
  local best, bd = nil, 1e9
  for _, p in ipairs(pads.far_from(x, z, 0, 900)) do
    local px, _, pz = pads.pos(p)
    local d = d2(px, pz, x, z)
    if d < bd then best, bd = p, d end
  end
  if not best then return false end
  local px, _, pz = pads.pos(best)
  return bond.warp(0, best, x - px, z - pz)
end
-- stand about `dist` from chr c where his scan sees Bond, facing him (8 directions round him); true when seen
local function stand_seen(c, dist, need_sight)
  local x, z = spawn.where(c)
  if not x then return false end
  for k = 0, 7 do
    local a = k * math.pi / 4
    if warp_xz(x + math.sin(a) * dist, z + math.cos(a) * dist) then
      face = { x, z }
      wait(0.5)
      local s = cover.state(c)
      if not need_sight or (s and (s.sees & 1) ~= 0) then return true end
    end
  end
  return false
end
local function guards(area, kinds)
  local out = {}
  for _, g in ipairs(HEIST.guards) do
    if g.area == area and (not kinds or kinds[g.kind]) and spawn.alive(g.chr) and not spawn.down(g.chr) then out[#out + 1] = g.chr end
  end
  return out
end

local blown = {}
on("cover_blown", function(g, p) blown[#blown + 1] = g end)

local finished = false
local function body()
  local ready = wait_for(25, function() return HEIST.ready end)
  check("mode ready", ready)
  wait(1.5)
  -- the API
  check("the heist turns the specs on (S.ar)", ar.enabled())
  check("diag starts off", not diag.enabled())
  diag.on(); local don = diag.enabled(); diag.off()
  check("diag.on / diag.off", don and not diag.enabled())
  check("ar.layer reads and sets", ar.layer("radar") == false and ar.layer("radar", true) == true and ar.layer("radar", false) == false)
  check("ar.layer refuses a bad name", not pcall(ar.layer, "nope"))
  local bx, by, bz = bond.pos(0)
  local id = ar.point(bx, by, bz, "TEST", 0x00FFFFFF)
  check("ar.point returns an id; the same label reuses it", id ~= nil and ar.point(bx + 10, by, bz, "TEST") == id, tostring(id))
  ar.unpoint("TEST")
  check("ar.say / ar.mark / ar.outline / ar.clear / ar.noise take their arguments",
        pcall(ar.say, HEIST.guards[1].chr, "test", 1, "radio") and pcall(ar.mark, HEIST.guards[1].chr, { label = "T", rgba = "cyan", bracket = true })
        and pcall(ar.outline, HEIST.guards[1].chr, "gold") and pcall(ar.clear, HEIST.guards[1].chr) and pcall(ar.noise, bx, by, bz, 300, "shot"))
  check("diag.freeze is refused nowhere in solo, and off again", diag.freeze(true) and diag.freeze(false))

  -- a card carrier (a guard): his key tag and brackets
  local carrier
  for _, role in ipairs({ "lobby captain", "security chief" }) do
    local c = HEIST.carriers[role]
    if c and c.chr and spawn.alive(c.chr) and not carrier then carrier = c end
  end
  check("a guard carries a card", carrier ~= nil)
  if carrier then
    local cx, cz = spawn.where(carrier.chr)
    if not stand_seen(carrier.chr, 300, true) then stand_seen(carrier.chr, 300, false) end
    local bx2, _, bz2 = bond.pos(0)
    print(string.format("HEIST   carrier %s chr %d at %.0f %.0f, Bond at %.0f %.0f", carrier.area, carrier.chr, cx or -1, cz or -1, bx2, bz2))
    wait(0.6)
    shot("ar_keys")
  end

  -- the diagnostics over the banking hall
  local hg = guards("hall", { post = true })
  if not (hg[2] and stand_seen(hg[2], 550, true)) then bond.warp(0, LEVEL.pads["hall_entry_e"]); face = { 300, 600 } end
  ar.layer("radar", true)
  diag.on()
  wait(1.2)
  shot("ar_diag")
  diag.off()
  ar.layer("radar", false)

  -- a guard watches a drawn gun: the "?" fills, then "HEY!"
  local g
  for _, c in ipairs(guards("hall", { post = true })) do if stand_seen(c, 330, true) then g = c; break end end
  check("a hall guard has Bond in sight", g ~= nil)
  if g then
    bond.equip(0, "pp7sil")
    local filling = wait_for(4, function() local s = cover.state(g); return s and s.susp >= 0.4 end)
    local s = cover.state(g)
    check("his meter fills with the gun drawn", filling, s and string.format("%.2f", s.susp) or "-")
    shot("ar_suspect")
    local hey = wait_for(6, function() return #blown > 0 end)
    check("he sees through Bond (HEY)", hey)
    wait(0.4)
    shot("ar_hey")
  end
  print(string.format("HEIST DONE %d passed, %d failed", pass, fail))
  finished = true
  real_fail("ar test done")
end

local co = coroutine.wrap(body)
on("tick", function(dt)
  if finished then return end
  local ok, err = pcall(co, dt)
  if not ok then finished = true; print("HEIST FAIL script error: " .. tostring(err)); print(string.format("HEIST DONE %d passed, %d failed", pass, fail + 1)) end
end)
