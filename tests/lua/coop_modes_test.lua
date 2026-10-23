-- Modes in co-op: a scenario script for tests/net_lockstep.sh phases 40-42 (run with GE_MODE=reset GE_LUA_TEST=<this file>, on Flatland).
-- Everything it prints starts with "T " and must be IDENTICAL on both machines of a netplay session (the test diffs the two logs).
-- Three hostages on pads 113 (400,0), 111 (-400,0) and 157 (0,1200) of Flatland's 15x15 grid; the extraction zone is player 0's start.
-- The inputs (B taps), teleports (GE_COOP_MOVETOPAD) and the final win come from the test harness.
local function T(...)   -- one line "T a b c ..." (single spaces: the test greps it)
  local t = {}
  for i = 1, select("#", ...) do t[#t + 1] = tostring((select(i, ...))) end
  print("T " .. table.concat(t, " "))
end

local zid, H, rescued = nil, {}, 0
local acc, sec = 0, 0
local probe_done = false

on("stage_start", function(stage)
  T("stage", stage, "bonds", bond.count(), "mode", mode.name())
  -- the no-argument form is player 0 on EVERY machine; the explicit forms name the Bond
  T("pos-default", bond.pos())
  T("pos0", bond.pos(0))
  T("pos1", bond.pos(1))
  T("bond-ids", bond.id(0), bond.id(1), bond.player(-3), tostring(bond.player(7)))
  T("dead", tostring(bond.dead()), tostring(bond.dead(1)), tostring(bond.dead(2)))
  local x, y, z = bond.pos()
  zid = zone.add("extract", x, y, z, 250)
  T("zone", zid, zone.name(zid))
  for i, pad in ipairs({ 113, 111, 157 }) do
    local c = spawn.hostage(pad)
    H[i] = c
    if c then zone.watch(c) end
    T("hostage", i, c)
  end
  -- math.random comes from the session seed and pairs() order does not depend on the machine
  T("rand", math.random(1, 1000000), math.random(1, 1000000), string.format("%.9f", math.random()), math.random(0, 7))
  local keys = {}
  for k in pairs({ alpha = 1, beta = 2, gamma = 3, delta = 4, epsilon = 5, zeta = 6, eta = 7, theta = 8 }) do keys[#keys + 1] = k end
  T("pairs-order", table.concat(keys, ","))
  hud.set(1, "MODES IN CO-OP TEST")
  hud.set(2, "you are Bond 1 (host)", 0)
  hud.set(2, "you are Bond 2 (guest)", 1)
  hud.set(3, "everyone sees this")
end)

on("hostage_follow", function(c, p) T("follow", c, "by", p, "leader-in-list", tostring((function() for _, e in ipairs(hostages.list()) do if e.chr == c then return e.leader end end end)())) end)
on("hostage_wait", function(c, p) T("wait", c, "by", p) end)
on("zone_enter", function(z, chr)
  T("zone_enter", z, chr, "bonds-in", table.concat(zone.bonds(z), "+"))
  if z == zid and hostages.state(chr) == "follow" then
    if hostages.extract(chr) then
      rescued = rescued + 1
      T("extracted", chr, rescued)
      if rescued >= 2 then mission.win("test: two rescued") end
    end
  end
end)
on("zone_leave", function(z, chr) T("zone_leave", z, chr) end)
on("chr_died", function(v, k) T("chr_died", v, k) end)
on("bond_died", function(k, p) T("bond_died", k, p) end)

on("tick", function(dt)
  acc = acc + dt
  if acc < 1 then return end
  acc = 0
  sec = sec + 1
  local l = {}
  for _, e in ipairs(hostages.list()) do l[#l + 1] = e.chr .. ":" .. e.state .. ":" .. tostring(e.leader) end
  local a, b, c = bond.pos(0)
  local d, e, f = bond.pos(1)
  T("sec", sec, "hostages", table.concat(l, " "), string.format("p0 %.0f %.0f %.0f p1 %.0f %.0f %.0f", a or -1, b or -1, c or -1, d or -1, e or -1, f or -1),
    "in-zone", table.concat(zone.bonds(zid), "+"), "nearest-to-origin", tostring((bond.nearest(0, 0, 0))))
  -- the leak probe: at 6 s a hostage appears on the pad nearest to bond.pos() (the no-argument form = player 0). With GE_LUA_TEST_LEAKLOCAL=1
  -- on one machine that form reads its own player instead, the two machines spawn on different pads, and the session must desync.
  if sec == 6 and not probe_done then
    probe_done = true
    local x, y, z = bond.pos()
    if x then
      local near = pads.far_from(x, z, 200, 450, true)   -- the ring of pads 400 away (an occupied pad refuses a spawn)
      local c = near[1] and spawn.hostage(near[1])
      T("probe pad", tostring(near[1]), "hostage", tostring(c), "padpos", tostring(near[1] and pads.pos(near[1])))
    else
      T("probe: no position")
    end
  end
end)
