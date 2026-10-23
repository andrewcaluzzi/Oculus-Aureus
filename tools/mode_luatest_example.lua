-- tiny API test mode (GE_MODE=luatest)
print("mode", mode.name(), "pads", pads.count())
local t = 0
on("stage_start", function(stage)
  local bx, by, bz = bond.pos()
  print("stage_start", stage, "bond", bx, by, bz)
  local far = pads.far_from(bx, bz, 400, 3000)
  print("far pads", #far)
  local h = spawn.hostage(far[1])
  local g = spawn.guard(far[#far], { weapon = "ak47" })
  print("spawned hostage", h, "guard", g)
  zone.add("start", bx, by, bz, 250)
  hud.set(1, "LUATEST mode")
  hud.set(2, "hostages: " .. #hostages.list())
  for i, e in ipairs(hostages.list()) do print("hostage", e.chr, e.state) end
end)
on("zone_enter", function(z, c) print("zone_enter", zone.name(z), c) end)
on("zone_leave", function(z, c) print("zone_leave", zone.name(z), c) end)
on("chr_died", function(v, k) print("chr_died", v, k) end)
on("tick", function(dt)
  t = t + dt
  if t >= 3 and not done then
    done = true
    print("badhandler next")
    nosuchfunction()
  end
end)
on("tick", function(dt)
  t2 = (t2 or 0) + dt
  if t2 >= 6 and not won then
    won = true
    print("win now t=", t)
    print("mission.win ->", mission.win("luatest done"))
    print("mission.win again ->", mission.win("again"))
  end
end)
on("tick", function(dt) bad = bad and bad + 1 or 1; if bad == 2 then error("boom") end end)
