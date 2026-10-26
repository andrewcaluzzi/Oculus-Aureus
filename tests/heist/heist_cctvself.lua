-- Heist CCTV self-view test: a Bond watching a camera that covers him sees his own body on the feed (and his co-op partner's).
-- Run: tests/heist/run_cctvself.sh (local co-op, GE_COOP=1: the Bonds have bodies). Screenshots via cctv.test_shot() land in
-- build_pc/heist_cctvself_shots/.
HEIST_SETTINGS = { test = true }
include("heist")
mods.set("invincible", true)
on("stage_start", function() mode.guards_wake(false) end)

local pass, fail = 0, 0
local function check(name, ok, extra)
  if ok then pass = pass + 1 else fail = fail + 1 end
  print(string.format("HEIST %s %s %s", ok and "PASS" or "FAIL", name, extra or ""))
end
local function wait(sec)
  local t = 0
  while t < sec do t = t + coroutine.yield() end
end
local function d2(ax, az, bx, bz) return math.sqrt((ax - bx) ^ 2 + (az - bz) ^ 2) end
local function warp_xz(p, x, z)
  local best, bd = nil, 1e9
  for _, q in ipairs(pads.far_from(x, z, 0, 900)) do
    local px, _, pz = pads.pos(q)
    local d = d2(px, pz, x, z)
    if d < bd then best, bd = q, d end
  end
  if not best then return false end
  local px, _, pz = pads.pos(best)
  return bond.warp(p, best, x - px, z - pz)
end
local function shot(name)
  print("HEIST SHOT " .. name)
  cctv.test_shot()
  wait(0.5)
end

local function body()
  while not HEIST.ready do wait(0.25) end
  wait(1)
  -- Bond 0 at the banking hall's screens, Bond 1 beside him
  local sx, sz = HEIST.M.P.screens_at[1], HEIST.M.P.screens_at[2]
  warp_xz(0, sx, sz)
  bond.face(0, HEIST.M.P.screens_face[1], HEIST.M.P.screens_face[2])
  wait(0.5)
  -- the cameras that see Bond 0 where he stands
  local seen = {}
  for _, cam in ipairs(cctv.list()) do
    if cctv.sees(cam, bond.id(0)) then seen[#seen + 1] = cam end
  end
  check("a camera sees Bond 0 at the hall's screens", #seen > 0, "cams=" .. table.concat(seen, ","))
  local best, bestbonds = nil, -1
  for _, cam in ipairs(seen) do
    cctv.view(0, cam)
    wait(1)
    local r = cctv.render_info()
    print(string.format("HEIST INFO cam %d (%s): %d chrs posed, %d Bonds", cam, tostring(cctv.name(cam)), r and r.chrs or -1, r and r.bonds or -1))
    if r and r.bonds > bestbonds then best, bestbonds = cam, r.bonds end
  end
  check("the feed of a camera that sees Bond 0 draws his own body", bestbonds >= 1, "bonds=" .. tostring(bestbonds))
  if best then
    cctv.view(0, best)
    wait(1)
    shot("self_" .. best)
  end
  cctv.view_off(0)
  wait(0.5)

  -- the security room's camera: Bond 0 on the pad it watches, facing it, Bond 1 beside him; both drawn on the feed
  local cs = LEVEL.objects.cam_security.index
  local g = cctv.get(cs)
  if g and g.lookpad and g.lookpad >= 0 then
    local px, _, pz = pads.pos(g.lookpad)
    bond.warp(0, g.lookpad, 0, 0)
    bond.face(0, g.x, g.z)
    local dx, dz = g.x - px, g.z - pz
    local n = math.max(1, math.sqrt(dx * dx + dz * dz))
    bond.warp(1, g.lookpad, -dz / n * 90, dx / n * 90)   -- 90 to his side
    bond.face(1, g.x, g.z)
    wait(0.5)
    cctv.view(0, cs)
    wait(1.5)
    local r = cctv.render_info()
    print(string.format("HEIST INFO security cam %d: %d chrs posed, %d Bonds", cs, r and r.chrs or -1, r and r.bonds or -1))
    -- (Bond 1 stands in Bond 0's own view, so the canonical world draw draws him; only Bond 0's body is posed by the feed)
    check("the security camera's feed draws Bond 0's own body at its pad", r ~= nil and r.bonds >= 1, "bonds=" .. tostring(r and r.bonds))
    shot("self_security")
    cctv.view_off(0)
    wait(0.5)
  else
    check("the security camera has a look pad", false)
  end
  print(string.format("HEIST DONE %d passed, %d failed", pass, fail))
  finished_ok = true
  mission.fail("cctvself test done")
end

local co = coroutine.wrap(body)
local finished = false
on("tick", function(dt)
  if finished or finished_ok then return end
  local ok, err = pcall(co, dt)
  if not ok then finished = true; print("HEIST cctvself error: " .. tostring(err)); print(string.format("HEIST DONE %d passed, %d failed", pass, fail + 1)) end
end)
