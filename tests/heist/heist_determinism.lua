-- Heist determinism scenario (designs/bank-heist.md P3): the real tools/mode_heist.lua on Bunker 2 (or on the Janus bank, STAGE=levelgen:
-- the pads come from the mode's map, HEIST.M), driven by a fixed script so two runs with the same seed must be hash-identical tick for
-- tick (tests/heist/run_determinism.sh compares the GE_HASHLOG files).
-- Everything that touches sim state happens at a fixed point of the script (seconds of sim time, never the wall clock):
--   the camera feeds (a use press at the hall's screens, A to cycle, B out; a feed kept open while the waves come), knock-outs (two real
--   slaps through the stock hit path, and spawn.ko), the alarm (hot, the lockdown, the waves), the crack with a forced jam and a restart,
--   the vault open, the loot, the vault closing and sealing.
-- GE_HEIST_DET_P1=1 (co-op): Bond 1 does the knock-outs and holds the crack zone, Bond 0 watches the feeds (per-Bond checks).
-- Prints "HEIST DET <step> tick-time ..." lines (they must match between the runs too) and "HEIST DET DONE".
local P1 = mode.env_flag("GE_HEIST_DET_P1")
HEIST_SETTINGS = {
  test = true,
  randomise = mode.env_flag("GE_HEIST_TEST_RANDOM"),   -- (section 25: GE_HEIST_TEST_RANDOM=1 runs the scenario on a randomised bank)
  crack_time = 14, jam_chance = 0, jam_every = 1000,   -- the jam is forced (HEIST.force_jam) at a fixed point
  vault_time = 16, klaxon_at = 5,
  wave_first = 3, wave_interval = 8,
  hostile_cap = 400,   -- (the ~120 placed guards would hold the waves back at the default cap)
}
-- section 31: GE_HEIST_DET_HOSTAGE=1 takes a hostage (a real use press) round the alarm and lets it go; GE_HEIST_DET_NOHOSTAGES=1 runs
-- with the layer off (S.hostages = false: hash-identical to the default when no hostage is taken)
local HOSTAGE = mode.env_flag("GE_HEIST_DET_HOSTAGE")
if mode.env_flag("GE_HEIST_DET_NOHOSTAGES") then HEIST_SETTINGS.hostages = false end
include("heist")
mods.set("invincible", true)   -- a hundred guards around a warping test Bond

local T = 0
local function note(fmt, ...) print(string.format("HEIST DET %7.2f " .. fmt, T, ...)) end
local function wait(sec)
  local t = 0
  while t < sec do
    local dt = coroutine.yield()
    t = t + dt
  end
end
local function d2(ax, az, bx, bz) return math.sqrt((ax - bx) ^ 2 + (az - bz) ^ 2) end
local function warp_xz(p, x, z)
  local best, bd = nil, 1e9
  for _, pad in ipairs(pads.far_from(x, z, 0, 900)) do
    local px, _, pz = pads.pos(pad)
    local d = d2(px, pz, x, z)
    if d < bd then best, bd = pad, d end
  end
  if not best then return false end
  local px, _, pz = pads.pos(best)
  return bond.warp(p, best, x - px, z - pz)
end

on("cctv_view", function(p, cam, why) note("cctv_view p=%d cam=%d why=%d", p, cam, why) end)
on("chr_knocked_out", function(c, p) note("knocked out chr %d by %d", c, p) end)
on("alarm", function(a) note("alarm %d", a) end)

local worker = P1 and bond.count() > 1 and 1 or 0   -- the Bond who slaps and cracks
local watcher = 0
local MP                                            -- the map's pads (set once the mode is ready)

local function body()
  while not HEIST.ready do wait(0.25) end
  MP = HEIST.M.P   -- the map's pads (Bunker 2, or the bank: heist_bank_map)
  note("ready: %d guards, %d civilians, phase %s", #HEIST.guards, #HEIST.civs, HEIST.phase)
  wait(1)

  -- 1. the camera feeds: a use press at the hall's screen wall, A (next camera), B (out)
  -- (cctv.test_press holds raw buttons on pad 0, which in co-op feeds the LOCAL Bond: the co-op runs switch the local player, so they
  -- cycle and leave through the API; cctv.test_use taps the use action in Bond p's own tick, the same for any local player)
  local coop = bond.count() > 1
  warp_xz(watcher, MP.screens_at[1], MP.screens_at[2])
  bond.face(watcher, MP.screens_face[1], MP.screens_face[2])
  wait(1)
  cctv.test_use(watcher)
  wait(1)
  note("feeds open: viewing %s", tostring(cctv.viewing(watcher)))
  if coop then
    local list = cctv.list()
    cctv.view(watcher, list[2])
  else
    cctv.test_press("a")
  end
  wait(1)
  note("after A: viewing %s", tostring(cctv.viewing(watcher)))
  if coop then cctv.view_off(watcher) else cctv.test_press("b") end
  wait(1)
  note("after B: viewing %s", tostring(cctv.viewing(watcher)))

  -- 2. knock-outs: two real slaps on a civilian in the hall, spawn.ko on a guard
  local civ = HEIST.civs[1] and HEIST.civs[1].chr
  if civ then
    local x, z = spawn.where(civ)
    warp_xz(worker, x + 60, z)
    bond.face(worker, x, z)
    bond.equip(worker, "fist")
    wait(0.5)
    ko.test_slap(worker, civ)
    wait(0.5)
    ko.test_slap(worker, civ)
    wait(1)
    note("civilian %d ko %s", civ, tostring(ko.is(civ)))
  end
  local g = HEIST.guards[5] and HEIST.guards[5].chr
  if g then spawn.ko(g, worker); wait(0.5); note("guard %d ko %s", g, tostring(ko.is(g))) end
  if ai and ai.on() then   -- section 18: the slaps and the thuds were heard; a noise in the hall; who is searching (hashed state)
    local pid = LEVEL and LEVEL.pads and LEVEL.pads.hall_mid
    local hx, hy, hz
    if pid then hx, hy, hz = pads.pos(pid) end
    if hx then noise.emit(hx, hy, hz, 700, "glass") end
    wait(2)
    local c = ai.counts()
    local busy = 0
    for _, gg in ipairs(HEIST.guards) do local st = ai.awareness(gg.chr); if st and st >= 1 then busy = busy + 1 end end
    note("ai searches %d barks %d aware %d", c.searches, c.barks, busy)
  end
  bond.equip(worker, "pp7sil")

  -- 2b. (GE_HEIST_DET_HOSTAGE) a hostage: aim at a civilian until it surrenders, step behind it, a use press takes it
  local hostage
  if HOSTAGE and shield then
    for i = 2, #HEIST.civs do
      local c = HEIST.civs[i].chr
      if spawn.alive(c) and not ko.is(c) then hostage = c; break end
    end
    if hostage then
      local x, z = spawn.where(hostage)
      local f = math.rad(shield.face(hostage))
      wait(1)   -- (the PP7 comes out)
      for _, r in ipairs({ 250, 150, 100 }) do
        warp_xz(worker, x + math.sin(f) * r, z + math.cos(f) * r)
        bond.face(worker, x, z)
        local t = 0
        while t < 2 and civilian.state(hostage) ~= "surrendered" do wait(0.25); t = t + 0.25 end
        if civilian.state(hostage) == "surrendered" then break end
      end
      note("hostage %d %s", hostage, tostring(civilian.state(hostage)))
      warp_xz(worker, x - math.sin(f) * 70, z - math.cos(f) * 70)
      bond.face(worker, x, z)
      wait(0.25)
      cctv.test_use(worker)
      wait(1)
      note("hostage held by %d: %s", worker, tostring(shield.held(worker)))
    end
  end

  -- 3. the alarm: hot, the lockdown, the waves
  alarm.start()
  wait(2)
  note("hot %s", tostring(HEIST.hot))
  if hostage then
    wait(3)
    cctv.test_use(worker)
    wait(1)
    local inf = shield.info(hostage) or {}
    note("hostage released: held %s, %s, %.2f s, counts %d/%d", tostring(shield.held(worker)), tostring(inf.why), inf.secs or -1,
         shield.counts().held_shots, shield.counts().clear_shots)
  end

  -- 4. the crack: plant, a forced jam, the restart, the vault opens
  bond.warp(worker, MP.crack)
  wait(0.5)
  bond.test_gadget(worker, "doordecoder")
  wait(4)
  note("cracking %s %.3f", HEIST.phase, HEIST.progress)
  HEIST.force_jam()
  wait(1.5)
  note("jammed %s %.3f", HEIST.phase, HEIST.progress)
  bond.test_gadget(worker, "doordecoder")
  wait(0.5)
  note("restarted %s", HEIST.phase)

  -- 5. while it cracks: the watcher back on a feed (it is kept open while the waves come)
  if watcher ~= worker then
    HEIST.watch(watcher, MP.feed_cam)
  end
  local t = 0
  while HEIST.phase ~= "open" and t < 40 do wait(0.5); t = t + 0.5 end
  note("vault %s", HEIST.phase)
  if watcher == worker then
    warp_xz(watcher, MP.screens_at[1], MP.screens_at[2])
    bond.face(watcher, MP.screens_face[1], MP.screens_face[2])
    wait(0.5)
    HEIST.watch(watcher, MP.feed_cam)
  end
  wait(1)

  -- 6. loot: a few bars, then the vault closes and seals (nobody inside)
  local n = 0
  for _, it in ipairs(HEIST.loot_items) do
    if it.where == "vault" and n < 4 and spawn.item_alive(it.h) then bond.warp(worker, it.pad, it.dx, it.dz); wait(0.5); n = n + 1 end
  end
  note("loot %d bars %d", HEIST.loot, HEIST.bars)
  bond.warp(worker, MP.outside)
  t = 0
  while HEIST.phase ~= "sealed" and HEIST.phase ~= "trapped" and t < 30 do wait(0.5); t = t + 0.5 end
  note("vault %s, waves %d, squads %d", HEIST.phase, HEIST.squads, #HEIST.guards)
  note("watcher on camera %s", tostring(cctv.viewing(watcher)))
  if not cctv.viewing(watcher) then   -- (solo: the worker left the screens for the loot) back on a feed while the waves come
    warp_xz(watcher, MP.screens_at[1], MP.screens_at[2])
    bond.face(watcher, MP.screens_face[1], MP.screens_face[2])
    wait(0.5)
    HEIST.watch(watcher, MP.feed_cam)
  end
  while true do
    wait(3)
    local down, total = ko.count()
    note("phase %s hot %s viewing %s ko %d/%d guards %d | %s", HEIST.phase, tostring(HEIST.hot), tostring(cctv.viewing(watcher)), down, total,
         #HEIST.guards, HEIST.state_text())
  end
end

local co = coroutine.wrap(body)
local finished = false
on("tick", function(dt)   -- (the Lua tick runs every 15 sim ticks: dt 0.25 s)
  T = T + dt
  if finished then return end
  local ok, err = pcall(co, dt)
  if not ok then finished = true; print("HEIST DET FAIL script error: " .. tostring(err)) end
end)
